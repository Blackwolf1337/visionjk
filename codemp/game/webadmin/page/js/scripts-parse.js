const SC_BLEND = {
	gl_one: gl.ONE, gl_zero: gl.ZERO, gl_dst_color: gl.DST_COLOR, gl_one_minus_dst_color: gl.ONE_MINUS_DST_COLOR,
	gl_src_color: gl.SRC_COLOR, gl_one_minus_src_color: gl.ONE_MINUS_SRC_COLOR, gl_src_alpha: gl.SRC_ALPHA,
	gl_one_minus_src_alpha: gl.ONE_MINUS_SRC_ALPHA, gl_dst_alpha: gl.DST_ALPHA, gl_one_minus_dst_alpha: gl.ONE_MINUS_DST_ALPHA,
	gl_src_alpha_saturate: gl.SRC_ALPHA_SATURATE
};
// shaderSort_t of the engine
const SC_SORT = { portal: 1, sky: 2, opaque: 3, decal: 4, seethrough: 5, banner: 6, inside: 7, mid_inside: 8, middle: 9, mid_outside: 10, outside: 11, underwater: 13, additive: 15, nearest: 21 };
const SC_SORT_OPAQUE = 3, SC_SORT_DECAL = 4, SC_SORT_SEE_THROUGH = 5, SC_SORT_BANNER = 6, SC_SORT_BLEND = 14;

// state: 0 not asked for, 1 on their way, 2 there, 3 the server has none to give (an older game module)
const scripts = { state: 0, bodies: new Map(), defs: new Map(), materials: new Map() };

const scNum = s => { const v = parseFloat(s); return Number.isFinite(v) ? v : 0; };
const scKey = name => name.toLowerCase().replace(/\\/g, '/').replace(/\.(tga|jpg|png)$/, '');

// The shaders of a .shader file: a name and what stands between its braces, kept as text until a map uses it.
// The first text of a name stays, as in the engine.
function scanShaderFile(text, into) {
	const re = /\/\/[^\n]*|\/\*[\s\S]*?\*\/|"[^"\n]*"|[{}]|[^\s{}]+/g;
	let name = null, depth = 0, start = 0;
	for (let m; (m = re.exec(text)); ) {
		const t = m[0];
		if (t[0] === '/' && (t[1] === '/' || t[1] === '*')) continue;
		if (t === '{') {
			if (depth++ === 0) start = re.lastIndex;
		} else if (t === '}') {
			if (depth > 0 && --depth === 0 && name !== null) {
				const key = scKey(name);
				if (!into.has(key)) into.set(key, text.slice(start, m.index));
				name = null;
			}
		} else if (depth === 0) {
			name = t[0] === '"' ? t.slice(1, -1) : t;
		}
	}
}

async function loadShaderScripts() {
	if (scripts.state) return;
	scripts.state = 1;
	try {
		const res = await fetch('api/shaders', { cache: 'no-store' });
		if (!res.ok) throw new Error('HTTP ' + res.status);
		// the engine puts the files together last one first, so that is the one whose text of a shader counts
		const files = (await res.json()).files.slice().reverse(), texts = new Array(files.length);
		// The server does not answer for a moment while it changes the map. A file that could not be fetched then
		// is asked for again, a few rounds, before the map is drawn without its shaders.
		let todo = files.map((_, i) => i);
		for (let round = 0; todo.length && round < 6; round++) {
			if (round) await new Promise(done => setTimeout(done, 2500));
			const queue = todo;
			todo = [];
			const worker = async () => {
				while (queue.length) {
					const i = queue.shift();
					try {
						const file = await fetch('api/file?name=' + encodeURIComponent('shaders/' + files[i]));
						if (file.ok) texts[i] = await file.text(); else if (file.status !== 404) todo.push(i);
					} catch (e) {
						todo.push(i);
					}
				}
			};
			await Promise.all([1, 2, 3, 4, 5, 6].map(worker));
		}
		for (const text of texts) if (text) scanShaderFile(text, scripts.bodies);
		scripts.state = 2;
	} catch (e) {
		scripts.state = 3;
	}
	prepareScripted(world);
}

// the lines of a script as words, a brace is a line of its own
function scriptLines(body) {
	const lines = [], re = /\n|\/\/[^\n]*|\/\*[\s\S]*?\*\/|"[^"\n]*"|[{}]|[^\s{}]+/g;
	let cur = null;
	for (let m; (m = re.exec(body)); ) {
		const t = m[0];
		if (t === '\n') { cur = null; continue; }
		if (t[0] === '/' && (t[1] === '/' || t[1] === '*')) { if (t.includes('\n')) cur = null; continue; }
		if (t === '{' || t === '}') { lines.push([t]); cur = null; continue; }
		if (!cur) lines.push(cur = []);
		cur.push(t[0] === '"' ? t.slice(1, -1) : t);
	}
	return lines;
}

function scWave(a, i) {
	return { func: (a[i] || 'sin').toLowerCase(), base: scNum(a[i + 1]), amp: scNum(a[i + 2]), phase: scNum(a[i + 3]), freq: scNum(a[i + 4]) };
}

function waveValue(w, t) {
	const x = w.phase + t * w.freq, f = x - Math.floor(x);
	let v = 1;
	if (w.func === 'sin') v = Math.sin(x * Math.PI * 2);
	else if (w.func === 'triangle') v = f < 0.25 ? f * 4 : f < 0.75 ? 2 - f * 4 : f * 4 - 4;
	else if (w.func === 'square') v = f < 0.5 ? 1 : -1;
	else if (w.func === 'sawtooth') v = f;
	else if (w.func === 'inversesawtooth') v = 1 - f;
	else if (w.func === 'noise') v = Math.random() * 2 - 1;
	return w.base + w.amp * v;
}

// What the colour or the alpha of a stage is: a number, a wave, or what the vertices carry. null: white, which
// is also what stands here for the gens that ask the entity or the light grid.
function scGen(a, rgb) {
	const v = (a[1] || '').toLowerCase();
	if (v === 'wave') return { wave: scWave(a, 2) };
	if (v === 'const') {
		const n = a.slice(2).map(t => t.replace(/[()]/g, '')).filter(Boolean).map(scNum);
		return { value: rgb ? [n[0] || 0, n[1] || 0, n[2] || 0] : (n[0] || 0) };
	}
	if (v === 'vertex' || v === 'exactvertex') return { vertex: 1, exact: v === 'exactvertex' };
	// lightingDiffuseEntity is that light times the colour of the entity, which is white here
	if (rgb && (v === 'lightingdiffuse' || v === 'lightingdiffuseentity')) return { diffuse: true };
	if (!rgb && v === 'lightingspecular') return { vertex: 3 };
	if (v === 'oneminusvertex') return { vertex: 2, exact: true };
	return null;
}

function newStage() {
	return { maps: [], images: null, animFreq: 0, clamp: false, lightmap: false, white: false, skip: false,
		src: gl.ONE, dst: gl.ZERO, blend: null, alphaFunc: 0, depthEqual: false, depthWrite: true, depthWriteAsked: false,
		rgb: null, alpha: null, tcGen: 0, tcMods: [] };
}

function stageLine(st, k, a) {
	const v = (a[1] || '').toLowerCase();
	if (k === 'map' || k === 'clampmap') {
		if (v === '$lightmap') st.lightmap = true;
		else if (v === '$whiteimage' || v === '*white') st.white = true;
		else if (a[1]) st.maps = [a[1]];
		st.clamp = k === 'clampmap';
	} else if (k === 'animmap' || k === 'clampanimmap' || k === 'oneshotanimmap') {
		st.animFreq = scNum(a[1]);
		st.maps = a.slice(2);
		st.clamp = k !== 'animmap';
	} else if (k === 'videomap' || k === 'surfacesprites') {
		st.skip = true;
	} else if (k === 'blendfunc') {
		if (v === 'add') { st.src = gl.ONE; st.dst = gl.ONE; }
		else if (v === 'filter') { st.src = gl.DST_COLOR; st.dst = gl.ZERO; }
		else if (v === 'blend') { st.src = gl.SRC_ALPHA; st.dst = gl.ONE_MINUS_SRC_ALPHA; }
		else { st.src = SC_BLEND[v] ?? gl.ONE; st.dst = SC_BLEND[(a[2] || '').toLowerCase()] ?? gl.ONE; }
	} else if (k === 'alphafunc') {
		st.alphaFunc = { gt0: 1, lt128: 2, ge128: 3, ge192: 4 }[v] || 0;
	} else if (k === 'depthfunc') {
		st.depthEqual = v === 'equal';
	} else if (k === 'depthwrite') {
		st.depthWriteAsked = true;
	} else if (k === 'rgbgen') {
		st.rgb = scGen(a, true);
	} else if (k === 'alphagen') {
		st.alpha = scGen(a, false);
	} else if (k === 'tcgen' || k === 'texgen') {
		st.tcGen = v === 'lightmap' ? 1 : v === 'environment' ? 2 : 0;
	} else if (k === 'tcmod') {
		const kind = v, n = a.slice(2).map(scNum);
		if (kind === 'stretch') st.tcMods.push({ kind, wave: scWave(a, 2) });
		else if (kind === 'scroll' || kind === 'scale' || kind === 'rotate' || kind === 'transform' || kind === 'turb') st.tcMods.push({ kind, n });
	}
}

// what the engine settles when a stage is read to its end
function finishStage(st) {
	// one and zero is no blending. A stage that blends leaves the depth alone unless it says otherwise.
	st.blend = st.src === gl.ONE && st.dst === gl.ZERO ? null : [st.src, st.dst];
	st.depthWrite = !st.blend || st.depthWriteAsked;
	if (st.lightmap) st.tcGen = 1;
	return st;
}

function parseScript(body) {
	const def = { stages: [], sort: 0, polygonOffset: false, nodraw: false, sky: null, fog: null, noFog: false, cull: 0, deforms: [] };
	let st = null, depth = 0;
	for (const a of scriptLines(body)) {
		const k = a[0].toLowerCase();
		if (k === '{') {
			if (depth++ === 0) st = newStage();
		} else if (k === '}') {
			if (depth > 0 && --depth === 0 && st) {
				if (!st.skip) def.stages.push(finishStage(st));
				st = null;
			}
		} else if (depth === 1 && st) {
			stageLine(st, k, a);
		} else if (depth === 0) {
			if (k === 'surfaceparm') { if ((a[1] || '').toLowerCase() === 'nodraw') def.nodraw = true; }
			else if (k === 'skyparms') def.sky = { box: a[1] && a[1] !== '-' ? a[1] : '', height: scNum(a[2]) || 512 };
			else if (k === 'sort') def.sort = SC_SORT[(a[1] || '').toLowerCase()] || scNum(a[1]);
			else if (k === 'polygonoffset') def.polygonOffset = true;
			else if (k === 'cull') {
				// which side is drawn: the front unless the script says the back or both
				const v = (a[1] || '').toLowerCase();
				def.cull = v === 'none' || v === 'twosided' || v === 'disable' ? 2 : v === 'back' || v === 'backside' || v === 'backsided' ? 1 : 0;
			}
			else if (k === 'deformvertexes') {
				const kind = (a[1] || '').toLowerCase(), div = scNum(a[2]);
				// a wave over a length of 0 is not allowed, the engine takes this for it
				if (kind === 'wave') def.deforms.push({ kind, spread: div ? 1 / div : 100, wave: scWave(a, 3) });
				else if (kind === 'bulge') def.deforms.push({ kind, width: scNum(a[2]), height: scNum(a[3]), speed: scNum(a[4]) });
				else if (kind === 'move') def.deforms.push({ kind, vec: [scNum(a[2]), scNum(a[3]), scNum(a[4])], wave: scWave(a, 5) });
				else if (kind === 'autosprite' || kind === 'autosprite2') def.deforms.push({ kind });
			}
			else if (k === 'noglfog') def.noFog = true;
			else if (k === 'fogparms') {
				// ( r g b ) and the depth at which nothing is seen through it any more
				const n = a.slice(1).map(t => t.replace(/[()]/g, '')).filter(Boolean).map(scNum);
				def.fog = { color: [n[0] || 0, n[1] || 0, n[2] || 0], depth: Math.max(1, n[3] || 0) };
			}
		}
	}
	return def;
}

// The stages a surface of the map is drawn with: the script of its shader, or what the engine makes of an image
// that has none. A surface without a lightmap is lit by the colours of its vertices, with the rules of the
// engine for that (the end of FinishShader). lit: true for a surface with a lightmap, false for one without,
// 'model' for a model, which gets the light of the place where a shader has no script.
function materialFor(name, lit) {
	const key = scKey(name), mkey = key + (lit === 'model' ? '|m' : lit ? '|l' : '|v');
	let mat = scripts.materials.get(mkey);
	if (mat) return mat;

	let def = scripts.defs.get(key);
	if (def === undefined) {
		const body = scripts.bodies.get(key);
		def = body === undefined ? null : parseScript(body);
		scripts.defs.set(key, def);
	}

	let stages;
	if (!def) {
		const image = Object.assign(newStage(), name ? { maps: [name] } : { white: true });
		if (lit === 'model') {
			image.rgb = { diffuse: true };
			stages = [image];
		} else if (lit) {
			Object.assign(image, { src: gl.DST_COLOR, dst: gl.ZERO });
			stages = [Object.assign(newStage(), { lightmap: true }), image];
		} else {
			image.rgb = { vertex: 1, exact: true };
			stages = [image];
		}
		stages.forEach(finishStage);
	} else {
		stages = def.stages.map(st => Object.assign({}, st));
		const lm = lit ? -1 : stages.findIndex(st => st.lightmap);
		if (lit === 'model') {
			// a model has no lightmap: such a stage is white
			for (const st of stages) if (st.lightmap) Object.assign(st, { lightmap: false, white: true });
		} else if (lm === 0 && stages.length > 1) {
			// the stage after the lightmap takes its place, as the first thing that is drawn there
			stages.shift();
			Object.assign(stages[0], { blend: null, depthWrite: true, alphaFunc: 0, depthEqual: false, rgb: { vertex: 1, exact: true }, alpha: null });
		} else if (lm >= 0) {
			Object.assign(stages[lm], { lightmap: false, white: true, rgb: { vertex: 1, exact: true }, alpha: null });
		}
	}

	const first = stages[0];
	mat = {
		stages,
		nodraw: !!def && def.nodraw,
		polygonOffset: !!def && def.polygonOffset,
		sky: def ? def.sky : null,
		fog: def ? def.fog : null,
		noFog: !!def && def.noFog,
		cull: def ? def.cull : 0,
		deforms: def ? def.deforms : [],
		sort: (def && def.sort) || (def && def.polygonOffset ? SC_SORT_DECAL : first && first.blend ? (first.depthWrite ? SC_SORT_SEE_THROUGH : SC_SORT_BLEND) : SC_SORT_OPAQUE)
	};
	scripts.materials.set(mkey, mat);
	return mat;
}

// The image a map editor shows for a shader, for the plain view: the one its script names for that
// (qer_editorimage), or the first one of its stages. null: no script, or the scripts are not there yet.
function editorImage(name) {
	const body = scripts.state === 2 ? scripts.bodies.get(scKey(name)) : undefined;
	if (body === undefined) return null;
	const named = /^\s*qer_editorimage\s+(\S+)/im.exec(body);
	const stage = named ? null : materialFor(name, true).stages.find(st => st.maps.length);
	const image = named ? named[1] : stage ? stage.maps[0] : '';
	return image ? gameImage(image, false) : null;
}

// the images the stages name, a few on their way at a time
const gameImages = new Map();	// name and whether it is clamped -> { tex, alpha, ready, failed }, of the map that is shown
const imageQueue = [];
let imageBusy = 0;

function gameImage(name, clamp) {
	const key = name.toLowerCase() + (clamp ? '|c' : '');
	let h = gameImages.get(key);
	if (!h) {
		h = { tex: grayTex, alpha: false, ready: false, failed: false };
		gameImages.set(key, h);
		imageQueue.push([h, name, clamp]);
		pumpImages();
	}
	return h;
}

function pumpImages() {
	while (imageBusy < 6 && imageQueue.length) {
		const [h, name, clamp] = imageQueue.shift();
		imageBusy++;
		fetchImage(name, clamp).then(img => {
			if (img) { h.tex = img.tex; h.alpha = img.alpha; h.ready = true; } else h.failed = true;
		}, () => {
			// no answer: again in a moment, three times, if the image is still one of the map that is shown
			h.tries = (h.tries || 0) + 1;
			if (h.tries > 3) { h.failed = true; return; }
			setTimeout(() => { if (gameImages.get(name.toLowerCase() + (clamp ? '|c' : '')) === h) { imageQueue.push([h, name, clamp]); pumpImages(); } }, 2500);
		}).finally(() => { imageBusy--; pumpImages(); });
	}
}

