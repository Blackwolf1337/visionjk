// ---- the effects that are playing: what the feed tells of them, and how their parts move

// The page plays the effects as a client does. Which and where comes with the state feed (WA_StateEffects): the
// entities that are effects and loop (CG_FX in cgame/cg_ents.c), and what the game played once (EV_PLAY_EFFECT
// and EV_PLAY_EFFECT_ID in cgame/cg_event.c). What an effect does then is the engine's: client/FxScheduler.cpp
// (PlayEffect, CreateEffect), client/FxUtil.cpp (FX_Add...) and client/FxPrimitives.cpp (Update, Die).
// Not done: what a primitive runs into (usePhysics, impactFx: it goes through walls and floors), the models of
// emitters (useModel, and paperPhysics which is for them), wind, and the forks of electricity.

const FX_MAX_LIVE = 3000, FX_MAX_SCHEDULED = 2000;
const FX_THIN_FROM = 2000;		// with more alive than this the counts of an effect get smaller
const FX_FAR = 8192;			// nothing is played further from the eye
const FX_WAIT_MS = 500;			// how long what was asked for waits for its file
const FX_TRACE_DIST = 16384;	// FX_MAX_TRACE_DIST
const FX_STATE_ONE_SHOT_LIMIT = 10;	// bg_public.h: a state below it plays once, 20 is the loop

// What EV_PLAY_EFFECT plays for a number of effectTypes_t (bg_public.h): the switch of CG_EntityEvent, and the
// names cgame registers for it (CG_RegisterEffects and CG_RegisterGraphics in cg_main.c).
const FX_GAME_TYPES = [null, 'emplaced/dead_smoke', 'emplaced/explode', 'turret/explode', 'sparks/spark_explosion', 'tripMine/explosion',
	'detpack/explosion', 'flechette/alt_blow', 'stunBaton/flesh_impact', 'demp2/altDetonate', 'turret/explode', 'sparks/spark_exp_nosnd',
	'env/water_impact', 'env/acid_splash', 'env/lava_splash', 'materials/mud_large', 'materials/sand_large', 'materials/dirt_large',
	'materials/snow_large', 'materials/gravel_large'];

// The effects are drawn in the administration, and in the editor while "Effects" is set. They are drawn with the
// shader scripts of the game: not before those are read.
const fxOn = () => (appMode !== 'editor' || $('dbgFx').checked) && scripts.state === 2;

const fxFiles = new Map();		// name -> { state, prims }: state 0 its file is on its way, 1 it is read, 2 there is none
const fxLive = [];				// the primitives that live
let fxSchedule = [];			// the ones whose delay is not over: { at, p, org, axis }
let fxQueue = [];				// what was asked for since the last step: { fx, org, axis, asked }
let fxUnnamed = [];				// played once by a number of CS_EFFECTS: { id, org, dir, asked }
let fxTime = 0;					// the clock of the effects in msec, it runs with fxAdvance
let fxDepth = 0;				// effects that are being started from within one another

const fxRunners = new Map();	// entity -> { index, state, delay, random, pos, dir, next, fire }
let fxMap = '', fxRev = 0, fxSeq = 0;
const fxHeard = new Map();		// what of fxPlay was taken, and when
let fxNames = [], fxNamesAsked = 0, fxNamesBusy = false;

const fxTraces = new Map();		// the rays of the last second
let fxRays = 0;					// how many more rays may be sent against the map until the next poll

const fxPick = (lo, hi) => lo === hi ? lo : lo + Math.random() * (hi - lo);
const fxVal = r => fxPick(r[0], r[1]);
const fxVec = v => [fxPick(v[0], v[3]), fxPick(v[1], v[4]), fxPick(v[2], v[5])];
const fxCross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const fxUnit = v => { const l = Math.hypot(v[0], v[1], v[2]); return l ? [v[0] / l, v[1] / l, v[2] / l] : [0, 0, 0]; };
// a vector that is given along three axes
const fxAlong = (ax, v) => [0, 1, 2].map(i => ax[0][i] * v[0] + ax[1][i] * v[1] + ax[2][i] * v[2]);
// MakeNormalVectors of the engine: a right and an up for a forward, which is taken as it comes
function fxNormals(fwd) {
	const r = [fwd[2], -fwd[0], fwd[1]], d = r[0] * fwd[0] + r[1] * fwd[1] + r[2] * fwd[2];
	const right = fxUnit([r[0] - d * fwd[0], r[1] - d * fwd[1], r[2] - d * fwd[2]]);
	return [right, fxCross(right, fwd)];
}
// RotatePointAroundVector of the engine
function fxTurn(dir, point, degrees) {
	const a = -degrees * Math.PI / 180, s = Math.sin(a), c = Math.cos(a), t = 1 - c, x = dir[0], y = dir[1], z = dir[2];
	return [
		point[0] * (t * x * x + c) + point[1] * (t * x * y + s * z) + point[2] * (t * x * z - s * y),
		point[0] * (t * x * y - s * z) + point[1] * (t * y * y + c) + point[2] * (t * y * z + s * x),
		point[0] * (t * x * z + s * y) + point[1] * (t * y * z - s * x) + point[2] * (t * z * z + c)];
}

// An effect by the name the game has for it. Its file is asked for the first time it is named, and with it the
// files of the effects it plays itself, which the engine registers when it reads an effect.
function fxGet(name) {
	// as RegisterEffect has it: the name without its ending, the file below effects/
	const key = name.toLowerCase().replace(/\\/g, '/').replace(/\.[^./]*$/, '');
	let fx = fxFiles.get(key);
	if (!fx) {
		const path = (key.startsWith('effects') ? key : 'effects/' + key) + '.efx';
		fx = { state: 0, prims: [] };
		fxFiles.set(key, fx);
		// a name of the wind (*wind) is no file, and the server hands out no name it would have to cut
		if (key[0] === '*' || path.length > 63 || /\.\.|:/.test(path)) { fx.state = 2; return fx; }
		fetchGameFile(path, true).then(text => {
			fx.prims = fxParse(text).prims;
			fx.state = 1;
			for (const p of fx.prims) for (const child of [...p.playFx, ...p.emitFx, ...p.deathFx, ...p.impactFx]) fxGet(child);
			if (fxOn()) fxWarm(fx);
		}, () => { fx.state = 2; });
	}
	return fx;
}

// Whether the images of the shaders of an effect are there, or never will be. Asking this asks for them: a stage
// that blends is not drawn before its image is read, and an effect that plays once would not be seen the first
// time. The images are those of the map (loadMap lets go of them).
function fxWarm(fx) {
	if (fx.warm === loadedMap) return true;
	let there = true;
	for (const p of fx.prims) for (const name of p.shaders) for (const st of materialFor(name, 'fx').stages) {
		if (!st.lightmap && !st.white && stageImages(st).some(h => !h.ready && !h.failed)) there = false;
	}
	if (there) fx.warm = loadedMap;
	return there;
}

// an effect that can be played now and seen: its file is read and its images are there
const fxReady = fx => fx.state === 1 && fxWarm(fx);

// The effect a primitive plays from one of its lists of names (playFx, emitFx, deathFx, impactFx). The engine
// has in such a list the files in front of the first one that is missing, and picks one of them. null: none, or
// a file is still on its way.
function fxChild(names) {
	let n = 0;
	for (; n < names.length; n++) {
		const state = fxGet(names[n]).state;
		if (state === 0) return null;
		if (state === 2) break;
	}
	return n ? fxGet(names[Math.floor(Math.random() * n)]) : null;
}

// The effect a primitive plays as it moves or when it dies, if it does: its flags say so (emitFx, deathFx), or its
// list of names did, which the engine takes for the flag when it found every file of the list.
function fxOwn(e, bit, names) {
	if (!(e.flags & bit) && !(e.p.listed & bit && names.every(name => fxGet(name).state === 1))) return null;
	return fxChild(names);
}

// Where the way from one place to another ends at the map, { end, normal }: normal is how the surface lies it
// ends at, null when it ends at nothing. null: not known, no ray is left for it. A ray goes against every
// triangle of the map (js/pick.js), so what it found is kept for a second, and only a few are sent between two
// polls: an effect that loops asks for the same way again and again.
function fxTrace(from, to) {
	const key = from.map(Math.round).join(' ') + '|' + to.map(v => Math.round(v / 8)).join(' ');
	let tr = fxTraces.get(key);
	if (tr && tr.until > fxTime) return tr;
	if (fxRays <= 0 || !world) return tr || null;
	fxRays--;
	const d = [to[0] - from[0], to[1] - from[1], to[2] - from[2]], len = Math.hypot(d[0], d[1], d[2]), dir = fxUnit(d);
	const hit = len ? pickGround(from, dir, false) : null;
	tr = { until: fxTime + 1000, end: to.slice(), normal: null };
	if (hit && Math.hypot(hit[0] - from[0], hit[1] - from[1], hit[2] - from[2]) <= len) {
		// the triangle that was hit, turned to where the ray came from
		const p = world.pick.pos, idx = world.pick.idx, a = idx[pickTri * 3] * 3, b = idx[pickTri * 3 + 1] * 3, c = idx[pickTri * 3 + 2] * 3;
		const n = fxUnit(fxCross([p[b] - p[a], p[b + 1] - p[a + 1], p[b + 2] - p[a + 2]], [p[c] - p[a], p[c + 1] - p[a + 1], p[c + 2] - p[a + 2]]));
		tr.end = hit;
		tr.normal = n[0] * dir[0] + n[1] * dir[1] + n[2] * dir[2] > 0 ? [-n[0], -n[1], -n[2]] : n;
	}
	if (fxTraces.size > 512) fxTraces.clear();
	fxTraces.set(key, tr);
	return tr;
}

// Plays an effect of the game at a place: fwd is the way it plays, as the client hands it to the engine. It
// starts with the next step of the effects, or when its file is there if that is soon.
function fxPlay(name, org, fwd) {
	if (!name || !fxOn() || !(Math.hypot(org[0] - eye[0], org[1] - eye[1], org[2] - eye[2]) < FX_FAR)) return;
	const fx = fxGet(name);
	if (fx.state === 2) return;
	fxStats.played++;
	if (fxQueue.length >= 256) { fxQueue.shift(); fxStats.dropped++; }
	// CFxScheduler::PlayEffect with a forward: that vector as it is, and two that stand on it
	fxQueue.push({ fx, org: org.slice(0, 3), axis: [fwd.slice(0, 3), ...fxNormals(fwd)], asked: performance.now() });
}

// CFxScheduler::PlayEffect: every primitive of the effect is made as often as its count says, at once or when
// its delay is over.
function fxStart(fx, org, axis) {
	if (fxDepth > 8) return;
	fxDepth++;
	// near the most that may live the counts get smaller, as fx_countScale of the game makes them
	const room = Math.min(1, (FX_MAX_LIVE - fxLive.length) / (FX_MAX_LIVE - FX_THIN_FROM));
	for (const p of fx.prims) {
		let count = Math.floor(fxVal(p.count) * room + 0.5);
		if (p.count[0] >= 1 && count < 1) count = 1;
		// evenDistribution spreads them over the width of the delay, from now on
		const even = p.spawnFlags & FX_EVEN_DISTRIBUTION, step = even ? Math.abs(p.delay[1] - p.delay[0]) / count : 0;
		for (let t = 0; t < count; t++) {
			const delay = Math.trunc(even ? t * step : fxVal(p.delay));
			if (delay < 1) fxCreate(p, org, axis, -delay);
			else if (fxSchedule.length < FX_MAX_SCHEDULED) fxSchedule.push({ at: fxTime + delay, p, org, axis });
			else fxStats.dropped++;
		}
	}
	fxDepth--;
}

// CFxScheduler::CreateEffect: one primitive is made. origin and axis are those of the effect, late is how many
// msec after its time it is made.
function fxCreate(p, origin, axis, late) {
	const sf = p.spawnFlags, type = p.type;
	let ax = axis;
	if (sf & FX_RAND_ROT_AROUND_FWD) {
		const right = fxTurn(axis[0], axis[1], Math.random() * 360);
		ax = [axis[0], right, fxCross(axis[0], right)];
	}

	// where it is: along the axes of the effect unless cheapOrgCalc says along those of the map
	let org = fxVec(p.origin);
	if (!(sf & FX_CHEAP_ORG_CALC)) org = fxAlong(ax, org);
	for (let i = 0; i < 3; i++) org[i] += origin[i];
	if (sf & FX_ORG_ON_SPHERE) {
		// somewhere on a ball, or on an egg that is radius wide and height high
		const x = Math.random() * Math.PI * 2, y = Math.random() * Math.PI, width = fxVal(p.radius), height = fxVal(p.height);
		const at = [Math.sin(x) * width * Math.sin(y), Math.cos(x) * width * Math.sin(y), Math.cos(y) * height];
		for (let i = 0; i < 3; i++) org[i] += at[i];
		// its forward is away from the middle then
		if (sf & FX_AXIS_FROM_SPHERE) { const fwd = fxUnit(at); ax = [fwd, ...fxNormals(fwd)]; }
	} else if (sf & FX_ORG_ON_CYLINDER) {
		// somewhere on the wall of a tube around the forward of the effect
		const r = fxVal(p.radius), h = (Math.random() * 2 - 1) * 0.5 * fxVal(p.height);
		const at = fxTurn(ax[0], [0, 1, 2].map(i => ax[1][i] * r + ax[0][i] * h), Math.random() * 360);
		for (let i = 0; i < 3; i++) org[i] += at[i];
		if (sf & FX_AXIS_FROM_SPHERE) {
			const fwd = fxUnit(at), right = fxCross(fwd[2] === 1 ? [0, 1, 0] : [0, 0, 1], fwd);
			ax = [fwd, right, fxCross(fwd, right)];
		}
	}

	if (type === 'fxrunner') {
		const fx = fxChild(p.playFx);
		if (fx) fxStart(fx, org, ax);
		return;
	}
	if (fxLive.length >= FX_MAX_LIVE) { fxStats.dropped++; return; }

	const life = Math.trunc(fxVal(p.life));
	// The parm of a group as the FX_Add... of FxUtil.cpp make it: for a wave how fast it swings, for nonlinear
	// and clamp a time in the life of the primitive, which the file gives in percent of that life.
	const parm = (g, shift) => {
		const mode = p.flags >> shift & FX_PARM_MASK, v = fxVal(g.parm);
		return mode === FX_WAVE ? v * 3.14159 * 0.001 : mode ? v * 0.01 * life + fxTime : 0;
	};
	const group = (g, shift) => [fxVal(g.start), fxVal(g.end), parm(g, shift)];
	const e = { p, type, flags: p.flags, t0: fxTime, t1: fxTime + life, org, shader: p.shaders.length ? p.shaders[Math.floor(Math.random() * p.shaders.length)] : '',
		size: group(p.size, 8), alpha: group(p.alpha, 0) };
	// rgbComponentInterpolation: a colour on the line between the two of the file, else each part for itself
	const k = sf & FX_RGB_COMPONENT_INTERP ? Math.random() : -1, rgb = v => k < 0 ? fxVec(v) : [0, 1, 2].map(i => v[i] + k * (v[i + 3] - v[i]));
	e.rgb = [rgb(p.rgb.start), rgb(p.rgb.end), parm(p.rgb, 4)];

	if (type === 'particle' || type === 'orientedparticle' || type === 'tail' || type === 'emitter') {
		// how it moves: along the axes of the effect too, unless absoluteVel and absoluteAccel say otherwise.
		// Gravity is always that of the map.
		e.vel = fxVec(p.vel);
		if (!(sf & FX_VEL_IS_ABSOLUTE)) e.vel = fxAlong(ax, e.vel);
		e.accel = fxVec(p.accel);
		if (!(sf & FX_ACCEL_IS_ABSOLUTE)) e.accel = fxAlong(ax, e.accel);
		e.accel[2] += fxVal(p.gravity);
		if (late > 0) {
			// it is late: to where it would be by now, as the engine works that out
			const ft = late * 0.001, t2 = ft * ft * 0.5;
			for (let i = 0; i < 3; i++) { e.vel[i] += ft * e.accel[i]; org[i] += ft * e.vel[i] + t2 * e.vel[i]; }
		}
	}
	if (type === 'line' || type === 'electricity') {
		const cheap = sf & FX_CHEAP_ORG2_CALC, o2 = fxVec(p.origin2);
		if (sf & FX_ORG2_FROM_TRACE) {
			// it ends where its forward meets the map
			let to = [0, 1, 2].map(i => org[i] + FX_TRACE_DIST * ax[0][i]);
			if (sf & FX_ORG2_IS_OFFSET) to = to.map((v, i) => v + (cheap ? o2 : fxAlong(ax, o2))[i]);
			const tr = fxTrace(org, to);
			if (!tr) { fxStats.dropped++; return; }
			e.org2 = tr.end;
			const fx = sf & FX_TRACE_IMPACT_FX && tr.normal ? fxChild(p.impactFx) : null;
			if (fx) fxStart(fx, tr.end, [tr.normal, ...fxNormals(tr.normal)]);
		} else {
			e.org2 = (cheap ? o2 : fxAlong(ax, o2)).map((v, i) => v + origin[i]);
		}
	}

	if (type === 'particle' || type === 'orientedparticle') {
		e.rot = fxVal(p.rotation);
		e.rotDelta = fxVal(p.rotationDelta);
		if (type === 'orientedparticle') e.normal = ax[0];
	} else if (type === 'tail') {
		e.length = group(p.length, 12);
		e.old = org.slice();
	} else if (type === 'cylinder') {
		e.normal = ax[0];
		e.size2 = group(p.size2, 16);
		e.length = group(p.length, 12);
		// org2fromTrace on a cylinder: it is as long as the way to the map
		e.traced = sf & FX_ORG2_FROM_TRACE ? -1 : 0;
	} else if (type === 'electricity') {
		e.chaos = fxVal(p.elasticity);
		e.seed = Math.floor(Math.random() * 1265536);
	} else if (type === 'emitter') {
		// its angles are those of the file on top of the way the effect looks (vectoangles)
		const fwd = ax[0], turn = fxVec(p.angle);
		e.angles = [turn[0] - Math.atan2(fwd[2], Math.hypot(fwd[0], fwd[1])) * 180 / Math.PI, turn[1] + Math.atan2(fwd[1], fwd[0]) * 180 / Math.PI, turn[2]];
		e.angleDelta = fxVec(p.angleDelta);
		e.density = fxVal(p.density);
		e.variance = fxVal(p.variance);
		e.emitted = fxTime;
	}
	fxLive.push(e);
}

// How much of the start of a value is left now, 1 at the start and 0 at the end: UpdateSize, UpdateSize2,
// UpdateLength, UpdateRGB and UpdateAlpha of FxPrimitives.cpp all work it out this way. shift says whose group
// it is (the four flags of the group in the flags of the primitive), parm is the parm of that group as fxCreate
// made it. linear runs evenly through the life. nonlinear holds the start until the time parm and runs to the
// end from there, clamp runs to the end until the time parm and holds it, and with linear each of the two is
// half of the result. wave swings by the cosine.
function fxFade(e, shift, parm) {
	const f = e.flags >> shift, mode = f & FX_PARM_MASK;
	let k = f & FX_LINEAR ? 1 - (fxTime - e.t0) / (e.t1 - e.t0) : 1, k2 = 1;
	if (mode === FX_NONLINEAR) {
		if (fxTime > parm) k2 = 1 - (fxTime - parm) / (e.t1 - parm);
		k = f & FX_LINEAR ? k * 0.5 + k2 * 0.5 : k2;
	} else if (mode === FX_WAVE) {
		k *= Math.cos((fxTime - e.t0) * parm);
	} else if (mode === FX_CLAMP) {
		k2 = fxTime < parm ? (parm - fxTime) / (parm - e.t0) : 0;
		k = f & FX_LINEAR ? k * 0.5 + k2 * 0.5 : k2;
	}
	return k;
}

// A size or a length of a primitive now: v is [start, end, parm]. With "random" it is somewhere between that and
// the end, anew every time.
function fxNow(e, shift, v) {
	let k = fxFade(e, shift, v[2]);
	if (e.flags >> shift & FX_RAND) k *= Math.random();
	return v[0] * k + v[1] * (1 - k);
}

// CParticle::UpdateOrigin, without what it runs into
function fxStep(e, sec) {
	for (let i = 0; i < 3; i++) { e.vel[i] += sec * e.accel[i]; e.org[i] += sec * e.vel[i]; }
}

// CEmitter::Draw: the effect of an emitter is played for every stretch of its way, density long and by variance
// more or less. from and vel are where it was and how fast before this step: the way since it played last is
// gone over from there in steps of 12 msec.
function fxEmit(e, fx, from, vel) {
	const stretch = () => (e.density + (Math.random() * 2 - 1) * e.variance) ** 2;
	let step = stretch(), dif = 0, axis = null;
	for (let t = e.emitted; t <= fxTime; t += 12) {
		dif += 12;
		const ft = dif * 0.001, k = ft + ft * ft * 0.5;
		const v = [vel[0] + ft * e.accel[0], vel[1] + ft * e.accel[1], vel[2] + ft * e.accel[2]];
		const at = [from[0] + k * v[0], from[1] + k * v[1], from[2] + k * v[2]];
		if ((at[0] - from[0]) ** 2 + (at[1] - from[1]) ** 2 + (at[2] - from[2]) ** 2 < step) continue;
		step = stretch();
		if (!axis) {
			// the way the emitter is turned (AnglesToAxis)
			const m = anglesMatrix(e.org, e.angles, 1);
			axis = [[m[0], m[1], m[2]], [m[4], m[5], m[6]], [m[8], m[9], m[10]]];
		}
		fxStart(fx, at, axis);
		from = at; vel = v; dif = 0; e.emitted = t;
	}
}

// The Update of a primitive (FxPrimitives.cpp): one that was not made in this very step moves.
function fxMove(e, ms) {
	const sec = ms * 0.001, started = e.t0 < fxTime, type = e.type;
	if (type === 'particle' || type === 'orientedparticle') {
		if (started) fxStep(e, sec);
		// UpdateRotation: it turns, and less and less
		e.rot += ms * 0.01 * e.rotDelta;
		e.rotDelta *= 1 - ms * 0.0007;
	} else if (type === 'tail') {
		// a tail points back to where it was a step ago
		e.old[0] = e.org[0]; e.old[1] = e.org[1]; e.old[2] = e.org[2];
		if (started) fxStep(e, sec);
	} else if (type === 'cylinder') {
		if (e.traced) {
			const tr = fxTrace(e.org, [0, 1, 2].map(i => e.org[i] + FX_TRACE_DIST * e.normal[i]));
			if (tr) e.traced = Math.hypot(tr.end[0] - e.org[0], tr.end[1] - e.org[1], tr.end[2] - e.org[2]);
		}
	} else if (type === 'emitter') {
		const from = e.org.slice(), vel = e.vel.slice();
		if (started) fxStep(e, sec);
		// one that stands still stops turning
		if (from[0] === e.org[0] && from[1] === e.org[1] && from[2] === e.org[2]) for (let i = 0; i < 3; i++) e.angleDelta[i] *= 0.7;
		for (let i = 0; i < 3; i++) e.angles[i] += ms * 0.01 * e.angleDelta[i];
		const fx = fxOwn(e, FX_EMIT_FX, e.p.emitFx);
		if (fx) fxEmit(e, fx, from, vel);
	}
}

// CParticle::Die: what a primitive whose time is over leaves behind. A line and electricity leave nothing.
function fxDie(e) {
	const fx = e.type !== 'line' && e.type !== 'electricity' ? fxOwn(e, FX_DEATH_RUNS_FX, e.p.deathFx) : null;
	if (!fx) return;
	const fwd = fxUnit([Math.random() * 2 - 1, Math.random() * 2 - 1, Math.random() * 2 - 1]);
	fxStart(fx, e.org, [fwd, ...fxNormals(fwd)]);
}

// The names of the effects of the map by their numbers (CS_EFFECTS). The game gives a number to an effect when
// it is first asked for, so the table grows while the map runs: it is read again when a number shows up that is
// not in it, at most twice a second.
function fxAskNames() {
	const now = performance.now(), map = fxMap;
	if (fxNamesBusy || now - fxNamesAsked < 500) return;
	fxNamesBusy = true;
	fxNamesAsked = now;
	fetch('api/list?kind=fxindex', { cache: 'no-store' }).then(res => res.json()).then(json => {
		if (map === fxMap) fxNames = [undefined, ...json.names];
	}).catch(() => { /* it is asked for again */ }).finally(() => { fxNamesBusy = false; });
}

// CG_FX: an entity that is an effect plays it again and again while its state says so, every delay msec and up
// to random more, and once for every change of a state that says once.
function fxLoops() {
	for (const r of fxRunners.values()) {
		if (r.next > fxTime || !r.state || !r.index) continue;
		const name = fxNames[r.index], once = r.state < FX_STATE_ONE_SHOT_LIMIT;
		if (name === undefined) { fxAskNames(); continue; }
		if (once && !r.fire) continue;
		// a loop whose file or whose images are not read yet begins when they are
		if (!once && !fxReady(fxGet(name))) continue;
		r.fire = false;
		r.next = fxTime + r.delay + Math.random() * r.random;
		fxPlay(name, r.pos, r.dir);
	}
	// what was played once by its number, as soon as the name of that number is known
	const now = performance.now();
	fxUnnamed = fxUnnamed.filter(u => {
		const name = fxNames[u.id];
		if (name !== undefined) fxPlay(name, u.org, u.dir); else fxAskNames();
		return name === undefined && now - u.asked < FX_WAIT_MS * 3;
	});
}

// Steps everything by dtMs: what loops, what was asked for, what was waiting for its delay, and every primitive
// that lives.
function fxAdvance(dtMs) {
	fxTime += dtMs;
	fxLoops();

	// What was asked for starts when its file and its images are there, or without the images when they take
	// long. Not what was asked for long ago: the page was not drawn for a while then (its tab was in the back).
	const now = performance.now(), queue = fxQueue;
	fxQueue = [];
	for (const q of queue) {
		const waited = now - q.asked;
		if (waited > FX_WAIT_MS * 3) continue;
		if (waited < FX_WAIT_MS ? fxReady(q.fx) : q.fx.state === 1) fxStart(q.fx, q.org, q.axis);
		else if (waited < FX_WAIT_MS && q.fx.state !== 2) fxQueue.push(q);
	}

	// AddScheduledEffects
	const waiting = fxSchedule;
	fxSchedule = [];
	for (const s of waiting) {
		if (s.at <= fxTime) fxCreate(s.p, s.org, s.axis, fxTime - s.at); else fxSchedule.push(s);
	}

	// FX_Add: one whose time is over dies, the others move. What they play as they do joins the list at its end.
	let n = 0;
	for (let i = 0; i < fxLive.length; i++) {
		const e = fxLive[i];
		if (fxTime > e.t1) { fxDie(e); continue; }
		fxMove(e, dtMs);
		fxLive[n++] = e;
	}
	fxLive.length = n;
	fxStats.live = n;
}

// nothing is playing any more
function fxClear() {
	fxLive.length = 0;
	fxSchedule = [];
	fxQueue = [];
	fxUnnamed = [];
	fxStats.live = 0;
}

// what the page has of the effects, for the poll of the state
function fxQuery() { return '&fx=' + fxRev + '&fxs=' + fxSeq; }

// The effects of an answer of the state feed: the entities that are effects when their list changed (s.fx), and
// what was played once since the number the page sent (s.fxPlay).
function fxTakeState(s) {
	const now = performance.now();
	if (s.map !== fxMap) {
		// another map: nothing of the last one goes on, and the numbers of its effects are no longer these
		fxClear();
		fxRunners.clear();
		fxHeard.clear();
		fxTraces.clear();
		fxNames = [];
		fxRev = fxSeq = 0;
		fxMap = s.map;
	}
	fxRays = 4;

	if (s.fx) {
		// A state that says "once" plays for every change of it, and for an entity that is new. What it is in the
		// first list the page gets is neither: that shot was fired before the page was there. Nor is it played
		// later when the effects are not drawn now.
		const first = !fxRev, on = fxOn(), seen = new Set();
		for (const [id, index, state, delay, random, x, y, z, dx, dy, dz] of s.fx.list) {
			const was = fxRunners.get(id), same = was && was.index === index, once = state > 0 && state < FX_STATE_ONE_SHOT_LIMIT;
			seen.add(id);
			fxRunners.set(id, { index, state, delay, random, pos: [x, y, z], dir: [dx, dy, dz], next: same ? was.next : 0,
				fire: once && on && (same ? was.fire || was.state !== state : !first) });
		}
		for (const id of [...fxRunners.keys()]) if (!seen.has(id)) fxRunners.delete(id);
		fxRev = s.fx.rev;
	}

	if (s.fxSeq !== undefined) {
		for (const [seq, ago, kind, id, x, y, z, dx, dy, dz] of s.fxPlay || []) {
			// One is known by its number and the time of the map it was played at: the answers of two polls
			// that were on their way together both have it, and a module that was loaded anew counts from 1 again.
			const key = seq + ' ' + (s.time - ago);
			if (fxHeard.has(key)) continue;
			fxHeard.set(key, now);
			if (!fxOn()) continue;
			if (kind) fxUnnamed.push({ id, org: [x, y, z], dir: [dx, dy, dz], asked: now });
			else fxPlay(FX_GAME_TYPES[id], [x, y, z], [dx, dy, dz]);
		}
		for (const [key, at] of fxHeard) if (now - at > 5000) fxHeard.delete(key);
		if (fxUnnamed.length > 64) fxUnnamed.splice(0, fxUnnamed.length - 64);
		// the number of the server, also when it is lower than the one here: its module was loaded anew then
		fxSeq = s.fxSeq;
	}
}
