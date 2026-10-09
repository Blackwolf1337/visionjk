// ---- the effect files of the game (.efx) read into what the page plays

// An effect is a list of primitives: each says how many of a thing are made, when, where, how they move and how
// their size, colour and alpha change while they live. What the engine makes of a file is in
// client/FxScheduler.cpp (ParseEffect) and client/FxTemplate.cpp (ParsePrimitive), the names and the numbers
// here are taken from there.

// the bits of "flags" (FxPrimitives.h). Bolting to a model (relative) is not there: the page has no bolts.
const FX_USE_MODEL = 0x01000000, FX_USE_PHYSICS = 0x02000000, FX_USE_BBOX = 0x04000000, FX_USE_ALPHA = 0x08000000, FX_EMIT_FX = 0x10000000,
	FX_DEATH_RUNS_FX = 0x20000000, FX_SET_SHADER_TIME = 0x00400000;
const FX_FLAGS = {
	usemodel: FX_USE_MODEL, usebbox: FX_USE_BBOX, usephysics: FX_USE_PHYSICS, expensivephysics: 0x00800000, ghoul2collision: 0x00020000 | FX_USE_PHYSICS | 0x00800000,
	ghoul2decals: 0x00040000, impactkills: 0x40000000, impactfx: 0x80000000, deathfx: FX_DEATH_RUNS_FX, usealpha: FX_USE_ALPHA, emitfx: FX_EMIT_FX,
	depthhack: 0x00100000, setshadertime: FX_SET_SHADER_TIME, paperphysics: 0x00010000, localizedflash: 0x00010000, playerview: 0x00010000
};
// For electricity three of them mean something else: useModel that it gets thin towards its end, usePhysics that
// it forks, useBBox that it grows.
const FX_TAPER = FX_USE_MODEL, FX_GROW = FX_USE_BBOX;

// the bits of "spawnFlags" (FxScheduler.h)
const FX_ORG_ON_SPHERE = 0x1, FX_AXIS_FROM_SPHERE = 0x2, FX_ORG_ON_CYLINDER = 0x4, FX_ORG2_FROM_TRACE = 0x10, FX_TRACE_IMPACT_FX = 0x20, FX_ORG2_IS_OFFSET = 0x40,
	FX_CHEAP_ORG_CALC = 0x100, FX_CHEAP_ORG2_CALC = 0x200, FX_VEL_IS_ABSOLUTE = 0x400, FX_ACCEL_IS_ABSOLUTE = 0x800, FX_RAND_ROT_AROUND_FWD = 0x1000,
	FX_EVEN_DISTRIBUTION = 0x2000, FX_RGB_COMPONENT_INTERP = 0x4000;
const FX_SPAWN_FLAGS = {
	orgonsphere: FX_ORG_ON_SPHERE, axisfromsphere: FX_AXIS_FROM_SPHERE, orgoncylinder: FX_ORG_ON_CYLINDER, org2fromtrace: FX_ORG2_FROM_TRACE,
	traceimpactfx: FX_TRACE_IMPACT_FX, org2isoffset: FX_ORG2_IS_OFFSET, cheaporgcalc: FX_CHEAP_ORG_CALC, cheaporg2calc: FX_CHEAP_ORG2_CALC,
	absolutevel: FX_VEL_IS_ABSOLUTE, absoluteaccel: FX_ACCEL_IS_ABSOLUTE, randrotaroundfwd: FX_RAND_ROT_AROUND_FWD, evendistribution: FX_EVEN_DISTRIBUTION,
	rgbcomponentinterpolation: FX_RGB_COMPONENT_INTERP, affectedbywind: 0x10000
};

// The flags of a group (alpha, rgb, size, length, size2) say how its value goes from start to end. They are four
// bits, and each group has its own four in the flags of the primitive, at its shift.
const FX_LINEAR = 0x1, FX_RAND = 0x2, FX_NONLINEAR = 0x4, FX_WAVE = 0x8, FX_CLAMP = 0xC, FX_PARM_MASK = 0xC;
const FX_GROUP_FLAGS = { linear: FX_LINEAR, nonlinear: FX_NONLINEAR, wave: FX_WAVE, random: FX_RAND, clamp: FX_CLAMP };
const FX_GROUPS = { alpha: ['alpha', 0], rgb: ['rgb', 4], size: ['size', 8], width: ['size', 8], length: ['length', 12], height: ['length', 12], size2: ['size2', 16], width2: ['size2', 16] };

// The kinds of primitive the engine knows. An effect holds 24 of them, the page keeps the ones it draws, that
// play other effects or that shine: not sound, camerashake, decal and flash.
const FX_TYPES_KNOWN = ['particle', 'line', 'tail', 'sound', 'cylinder', 'electricity', 'emitter', 'decal', 'orientedparticle', 'fxrunner', 'light', 'camerashake', 'flash'];
const FX_TYPES_KEPT = new Set(['particle', 'orientedparticle', 'line', 'tail', 'cylinder', 'electricity', 'emitter', 'fxrunner', 'light']);
const FX_MAX_COMPONENTS = 24;

// The keys of a primitive that are a range of numbers (2) or of vectors (3), and the field they set. A range
// "a b" is a value between the two that is picked when a primitive is made, a vector has six numbers for it,
// first the three lowest.
const FX_KEYS = {
	count: ['count', 2], life: ['life', 2], delay: ['delay', 2], bounce: ['elasticity', 2], intensity: ['elasticity', 2], gravity: ['gravity', 2],
	density: ['density', 2], variance: ['variance', 2], radius: ['radius', 2], height: ['height', 2], rotation: ['rotation', 2], rotationdelta: ['rotationDelta', 2],
	origin: ['origin', 3], origin2: ['origin2', 3], angle: ['angle', 3], angles: ['angle', 3], angledelta: ['angleDelta', 3], velocity: ['vel', 3], vel: ['vel', 3],
	acceleration: ['accel', 3], accel: ['accel', 3]
};
// the lists of other effects a primitive names, the field they fill and the flag a list sets that was read whole
const FX_LISTS = { playfx: ['playFx', 0], emitfx: ['emitFx', FX_EMIT_FX], deathfx: ['deathFx', FX_DEATH_RUNS_FX], impactfx: ['impactFx', 0] };

// The text as the engine reads it (GenericParser2.cpp): a name, then the rest of its line as the value. A value
// of { opens a group that ends with }, a value of [ a list with one item a line that ends with ].
function fxGroups(text) {
	const n = text.length;
	let i = 0;
	// over what stands in front of a word: blanks, line ends and comments. False at the end of the text.
	const skip = () => {
		for (;;) {
			while (i < n && text.charCodeAt(i) <= 32) i++;
			if (i >= n) return false;
			if (text[i] === '/' && text[i + 1] === '/') { while (i < n && text[i] !== '\n') i++; }
			else if (text[i] === '/' && text[i + 1] === '*') { const end = text.indexOf('*/', i + 2); i = end < 0 ? n : end + 2; }
			else return true;
		}
	};
	// the next word, or with line everything up to the end of the line. '' at the end of the text.
	const next = line => {
		if (!skip()) return '';
		const from = i;
		if (text[i] === '"') {
			const end = text.indexOf('"', i + 1);
			i = end < 0 ? n : end + 1;
			return text.slice(from + 1, end < 0 ? n : end);
		}
		if (line) {
			while (i < n && text[i] !== '\n' && text[i] !== '\r' && !(text[i] === '/' && (text[i + 1] === '/' || text[i + 1] === '*'))) i++;
			// the engine cuts tabs off the end, not blanks
			return text.slice(from, i).replace(/[\x00-\x1f]+$/, '');
		}
		while (i < n && text.charCodeAt(i) > 32) i++;
		return text.slice(from, i);
	};
	const group = () => {
		const g = { pairs: [], groups: [] };
		for (;;) {
			const name = next(false);
			if (!name || name === '}') return g;
			const value = next(true);
			if (value === '{') g.groups.push([name.toLowerCase(), group()]);
			else if (value === '[') {
				const items = [];
				for (let item; (item = next(true)) && item !== ']'; ) items.push(item);
				g.pairs.push([name.toLowerCase(), items]);
			} else g.pairs.push([name.toLowerCase(), [value]]);
		}
	};
	return group();
}

// what a table has for a word of a file, which may be any word: not what every object has (constructor)
const fxWord = (table, word) => Object.hasOwn(table, word) ? table[word] : undefined;

// The numbers a value starts with, as sscanf reads "%f %f": up to max of them, what follows is not looked at.
const FX_NUMBER = /^\s*([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)/;
function fxNumbers(text, max) {
	const out = [];
	for (let rest = text || '', m; out.length < max && (m = FX_NUMBER.exec(rest)); rest = rest.slice(m[0].length)) out.push(parseFloat(m[1]));
	return out;
}
// A range of numbers [lowest, highest], one number is both. null: the value has none, the field keeps what it has.
function fxRange(text) {
	const v = fxNumbers(text, 2);
	return v.length ? [v[0], v[v.length - 1]] : null;
}
// A range of vectors [x, y, z, x, y, z], three numbers are both. null: not three and not six numbers.
function fxRange3(text) {
	const v = fxNumbers(text, 6);
	return v.length === 6 ? v : v.length === 3 ? v.concat(v) : null;
}
// The bits of the first words of a value that the table knows: the engine reads max words and no more.
function fxBits(text, table, max) {
	let bits = 0;
	for (const word of (text || '').split(/\s+/).filter(Boolean).slice(0, max)) bits |= fxWord(table, word.toLowerCase()) || 0;
	return bits;
}

// a primitive with the values the engine starts from (the constructor of CPrimitiveTemplate)
function fxPrimitive(type) {
	const group = () => ({ start: [1, 1], end: [1, 1], parm: [0, 0] });
	return {
		type, flags: 0, spawnFlags: 0, count: [1, 1], delay: [0, 0], life: [50, 50], elasticity: [0.1, 0.1], radius: [10, 10], height: [10, 10],
		origin: [0, 0, 0, 0, 0, 0], origin2: [0, 0, 0, 0, 0, 0], vel: [0, 0, 0, 0, 0, 0], accel: [0, 0, 0, 0, 0, 0], gravity: [0, 0],
		rotation: [0, 0], rotationDelta: [0, 0], angle: [0, 0, 0, 0, 0, 0], angleDelta: [0, 0, 0, 0, 0, 0], density: [10, 10], variance: [1, 1],
		rgb: { start: [1, 1, 1, 1, 1, 1], end: [1, 1, 1, 1, 1, 1], parm: [0, 0] }, alpha: group(), size: group(), size2: group(), length: group(),
		shaders: [], playFx: [], emitFx: [], deathFx: [], impactFx: [], listed: 0
	};
}

// The text of an effect file as { prims }. It never throws: a line it can not read leaves what was there.
function fxParse(text) {
	const prims = [];
	let known = 0;
	try {
		for (const [type, g] of fxGroups(String(text)).groups) {
			if (!FX_TYPES_KNOWN.includes(type) || ++known > FX_MAX_COMPONENTS || !FX_TYPES_KEPT.has(type)) continue;
			const p = fxPrimitive(type);
			for (const [key, values] of g.pairs) {
				const value = values[0], field = fxWord(FX_KEYS, key), list = fxWord(FX_LISTS, key);
				if (field) {
					const v = field[1] === 2 ? fxRange(value) : fxRange3(value);
					if (v) p[field[0]] = v;
				} else if (list) {
					p[list[0]].push(...values.filter(Boolean));
					p.listed |= values[0] ? list[1] : 0;
				} else if (key === 'shaders' || key === 'shader') p.shaders.push(...values.filter(Boolean));
				else if (key === 'models' || key === 'model') p.flags |= values[0] ? FX_USE_MODEL : 0;
				else if (key === 'min' || key === 'max') p.flags |= fxRange3(value) ? FX_USE_BBOX | FX_USE_PHYSICS : 0;
				else if (key === 'flags' || key === 'flag') p.flags |= fxBits(value, FX_FLAGS, 7);
				else if (key === 'spawnflags' || key === 'spawnflag') p.spawnFlags |= fxBits(value, FX_SPAWN_FLAGS, 7);
			}
			for (const [name, sub] of g.groups) {
				const which = fxWord(FX_GROUPS, name);
				if (!which) continue;
				const into = p[which[0]], vec = which[0] === 'rgb';
				for (const [key, values] of sub.pairs) {
					const value = values[0];
					if (key === 'start' || key === 'end') { const v = vec ? fxRange3(value) : fxRange(value); if (v) into[key] = v; }
					else if (key === 'parm' || key === 'parms') { const v = fxRange(value); if (v) into.parm = v; }
					else if (key === 'flags' || key === 'flag') p.flags |= fxBits(value, FX_GROUP_FLAGS, 4) << which[1];
				}
			}
			prims.push(p);
		}
	} catch (e) { /* what was read up to here is the effect */ }
	return { prims };
}
