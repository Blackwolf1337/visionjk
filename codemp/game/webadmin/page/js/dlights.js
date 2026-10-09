// ---- the dynamic lights of a frame: what a blade, a shot or an effect throws on the map and on models
// (R_AddLightToScene; ProjectDlightTexture2 in tr_shade.cpp for the map, R_SetupEntityLighting in tr_light.cpp
// for a model)
//
// The engine has all the lights of a frame before it draws, the page draws as it goes: so they are found first
// (gatherDlights), a model takes them with the light of its place (dlightsOn), and the map gets them as one more
// pass over the triangles they reach, after its solid surfaces are drawn (drawDlights).

const MAX_DLIGHTS = 32;				// the engine keeps no more for a frame
const DLIGHT_AT_RADIUS = 16;		// R_SetupEntityLighting: this much light is added at the edge of a light
const DLIGHT_MINIMUM_RADIUS = 16;	// and no light is taken to be nearer than this
const DL_CELL = 128;				// the side of a cell the triangles of the map are sorted into
const DL_MAX_VERTS = 98304;			// vertices of lit triangles in a frame
const DL_STRIDE = 32;				// a vertex: its place, its texture coordinates, those of the light, its colour

// the lights of the frame: { origin, radius, color (0 to 1) }
const dlights = [];
let dlFrame = 1;					// counts the frames that lit the map
// What other files add to the lights of a frame: functions that are called with the time of the frame and call
// dlightAdd. A file registers one while the page loads.
const dlightSources = [];
const dlStats = { lights: 0, tris: 0, draws: 0 };

// RE_AddLightToScene. The additive kind of light the engine also has is one cgame never asks for.
function dlightAdd(origin, radius, color) {
	if (dlights.length >= MAX_DLIGHTS || !(radius > 0)) return;
	dlights.push({ origin: [origin[0], origin[1], origin[2]], radius, color: [color[0], color[1], color[2]] });
}

// CG_PlayerPowerups: who has the quad or carries a flag shines
const DL_POWERUPS = [[1, [0.2, 0.2, 1]], [4, [1, 0.2, 0.2]], [5, [0.2, 0.2, 1]], [6, [1, 1, 1]]];	// PW_QUAD, the red, the blue and the neutral flag
function dlPlayers() {
	for (const p of players.values()) {
		const d = p.data;
		if (!d.powerups || d.team === 'spectator' || !clientSees(p.draw, 16, 25, 41)) continue;
		// 200 and up to 31 more, anew in every frame (rand() & 31)
		for (const [bit, color] of DL_POWERUPS) if (d.powerups & 1 << bit) dlightAdd(p.draw, 200 + (refCam ? 15.5 : Math.floor(Math.random() * 32)), color);
	}
}

// Before anything of a frame is drawn: the lights of this frame are found. The blades of the sabers are worked
// out with them (js/sabers.js), because a blade is a light.
function gatherDlights(now) {
	dlights.length = 0;
	if (!state) return;
	sabersPrepare(now);
	shotLights(now);
	fxLights();
	dlPlayers();
	for (const add of dlightSources) add(now);
	dlStats.lights = dlights.length;
}

// The light of a place with the lights of the frame added, as R_SetupEntityLighting adds them to what the light
// grid gave. out: what gridLight filled (ambient, directed, dir), colours from 0 to 1. Every light adds to the
// directed light by how near it is, and pulls the direction the light comes from towards itself.
function dlightsOn(pos, out) {
	if (!dlights.length) return;
	const d = Math.hypot(out.directed[0], out.directed[1], out.directed[2]) * 255, dir = [out.dir[0] * d, out.dir[1] * d, out.dir[2] * d];
	for (const dl of dlights) {
		const x = dl.origin[0] - pos[0], y = dl.origin[1] - pos[1], z = dl.origin[2] - pos[2], len = Math.hypot(x, y, z);
		const near = len < DLIGHT_MINIMUM_RADIUS ? DLIGHT_MINIMUM_RADIUS : len, add = DLIGHT_AT_RADIUS * dl.radius * dl.radius / (near * near);
		for (let c = 0; c < 3; c++) out.directed[c] += add * dl.color[c] / 255;
		if (len) { dir[0] += add * x / len; dir[1] += add * y / len; dir[2] += add * z / len; }
	}
	const l = Math.hypot(dir[0], dir[1], dir[2]);
	if (l) { out.dir[0] = dir[0] / l; out.dir[1] = dir[1] / l; out.dir[2] = dir[2] / l; }
}

// ---- the map lit by the lights of the frame (ProjectDlightTexture2, r_dlightStyle 1)

// tr.dlightImage (R_CreateDlightImage): gfx/2d/dlight without mipmaps and clamped, or a blob the engine makes
// itself when the file is not there
const dlImage = { tex: null, asked: false };
function dlTexture() {
	if (!dlImage.asked) {
		dlImage.asked = true;
		const blob = () => {
			const S = 16, data = new Uint8Array(S * S * 4);
			for (let x = 0; x < S; x++) for (let y = 0; y < S; y++) {
				const d = (S / 2 - 0.5 - x) ** 2 + (S / 2 - 0.5 - y) ** 2, b = Math.trunc(4000 / d);
				data.fill(b > 255 ? 255 : b < 75 ? 0 : b, (y * S + x) * 4, (y * S + x) * 4 + 3);
				data[(y * S + x) * 4 + 3] = 255;
			}
			return makeTexture(S, S, data, { mip: false, clamp: true });
		};
		fetchGameFile('gfx/2d/dlight.tga').then(buf => {
			const img = decodeTga(buf);
			dlImage.tex = img ? makeTexture(img.w, img.h, img.data, { mip: false, clamp: true }) : blob();
		}, () => { dlImage.tex = blob(); });
	}
	return dlImage.tex;
}

// What of a shader the pass of a light is drawn with, by material: null for one the engine does not light (a sort
// above opaque, surfaceparm nodlight or sky; and here one whose vertices a deform moves). Else the stage of that
// pass: the image of the first stage that does not blend and is no lightmap and has no tcMod, times the image of
// the light, times the colour of the vertices, added to what is there where the surface was drawn. A shader with
// no such stage: the image of the light alone, multiplied into what is there and added to it.
const dlStages = new WeakMap();
function dlStageOf(mat, name) {
	let st = dlStages.get(mat);
	if (st !== undefined) return st;
	const body = scripts.bodies.get(scKey(name)) || '';
	st = null;
	if (mat.sort <= SC_SORT_OPAQUE && !mat.sky && !mat.deforms.length && !/^\s*surfaceparm\s+(nodlight|sky)\b/im.test(body)) {
		const plain = s => !!s && !s.isLightmap && !s.tcMods.length && s.tcGen !== 2 && s.tcGen !== 3 && (s.maps.length > 0 || s.white || !!s.tex);
		const light = Object.assign(newStage(), { tex: 1, tcGen: 1 });
		const pass = { blend: [gl.ONE, gl.ONE], depthWrite: false, depthEqual: true, rgbGen: CGEN.vertex, alphaGen: AGEN.identity, rgbMode: 1 };
		for (const s of mat.stages) {
			if (s.merged || s.blend || !(plain(s) || plain(s.multi))) continue;
			const src = plain(s) ? s : s.multi;
			st = Object.assign(newStage(), { maps: src.maps, animFreq: src.animFreq, oneShot: src.oneShot, clamp: src.clamp, white: src.white, tex: src.tex, multi: light, env: 1 }, pass);
			break;
		}
		if (!st) st = Object.assign(light, pass, { alone: true, blend: [gl.DST_COLOR, gl.ONE] });
	}
	dlStages.set(mat, st);
	return st;
}

// the vertices of a batch of the map as the page has them on the graphics card, read back once
const dlBatches = new WeakMap();
function dlBatch(b) {
	let data = dlBatches.get(b);
	if (!data) {
		const read = (buf, into) => { gl.bindBuffer(gl.ARRAY_BUFFER, buf); gl.getBufferSubData(gl.ARRAY_BUFFER, 0, into); return into; };
		// which of its triangles are those of patches: they change with the distance and are not in the cells
		const grids = world.grids.filter(g => g.idxBuf === b.idxBuf).sort((x, y) => x.first - y.first);
		data = { pos: read(b.posBuf, new Float32Array(b.verts * 3)), st: read(b.stBuf, new Float32Array(b.verts * 2)), grids, stamp: new Uint32Array(b.tri.length / 3) };
		dlBatches.set(b, data);
	}
	return data;
}

// The triangles of the map that stand still, sorted into cells: of every batch that is lit and is no brush model
// of an entity, without its patches. A cell lists them as the number of the batch times 2^26 plus the place of
// the triangle in the index list of the batch.
let dlIndex = null;
function dlBuildIndex() {
	const cells = new Map(), ents = [], grids = [], models = [];
	for (const e of world.scripted) {
		if (e.sprite || !dlStageOf(e.mat, e.b.shaderName)) continue;
		if (e.b.model) { models.push(e); continue; }
		const data = dlBatch(e.b), pos = data.pos, tri = e.b.tri, n = ents.length;
		ents.push(e);
		let g = 0;
		for (let i = 0; i + 2 < tri.length; i += 3) {
			while (g < data.grids.length && i >= data.grids[g].first + (data.grids[g].width - 1) * (data.grids[g].height - 1) * 6) g++;
			if (g < data.grids.length && i >= data.grids[g].first) continue;
			const a = tri[i] * 3, b = tri[i + 1] * 3, c = tri[i + 2] * 3, lo = [0, 0, 0], hi = [0, 0, 0];
			for (let k = 0; k < 3; k++) {
				lo[k] = Math.floor(Math.min(pos[a + k], pos[b + k], pos[c + k]) / DL_CELL) + 512;
				hi[k] = Math.floor(Math.max(pos[a + k], pos[b + k], pos[c + k]) / DL_CELL) + 512;
			}
			for (let x = lo[0]; x <= hi[0]; x++) for (let y = lo[1]; y <= hi[1]; y++) for (let z = lo[2]; z <= hi[2]; z++) {
				const key = (x * 1024 + y) * 1024 + z;
				let list = cells.get(key);
				if (!list) cells.set(key, list = []);
				list.push(n * 0x4000000 + i);
			}
		}
		for (const grid of data.grids) grids.push({ e, grid, data });
	}
	dlIndex = { list: world.scripted, cells, ents, grids, models };
}

// The box of a patch (meshBounds of the engine) and the triangles it is drawn with now, which change with the
// distance (updateGridLod in js/map.js): the columns and rows whose error is not above the one of the frame.
const dlGrids = new WeakMap(), dlCols = new Uint8Array(256), dlRows = new Uint8Array(256);
function dlGrid(g, pos) {
	let info = dlGrids.get(g);
	if (!info) {
		const mins = [Infinity, Infinity, Infinity], maxs = [-Infinity, -Infinity, -Infinity];
		for (let i = g.base * 3, end = (g.base + g.width * g.height) * 3; i < end; i += 3) for (let k = 0; k < 3; k++) {
			if (pos[i + k] < mins[k]) mins[k] = pos[i + k];
			if (pos[i + k] > maxs[k]) maxs[k] = pos[i + k];
		}
		dlGrids.set(g, info = { mins, maxs, low: -1, high: -1, idx: new Uint32Array((g.width - 1) * (g.height - 1) * 6), count: 0 });
	}
	if (info.low !== g.low || info.high !== g.high) {
		let nc = 1, nr = 1;
		for (let i = 1; i < g.width - 1; i++) if (g.widthLodError[i] <= g.low) dlCols[nc++] = i;
		for (let i = 1; i < g.height - 1; i++) if (g.heightLodError[i] <= g.low) dlRows[nr++] = i;
		dlCols[nc++] = g.width - 1; dlRows[nr++] = g.height - 1;
		info.count = gridIndexes(info.idx, 0, g.base, g.width, dlCols, nc, dlRows, nr);
		info.low = g.low; info.high = g.high;
	}
	return info;
}

const dlBuffer = new ArrayBuffer(DL_MAX_VERTS * DL_STRIDE), dlFloats = new Float32Array(dlBuffer), dlBytes = new Uint8Array(dlBuffer);
let dlVerts = 0, dlVao = null, dlVbo = null;

// One triangle in the light of one lamp, as ProjectDlightTexture2 works it out: nothing when the lamp is behind
// it or further from its plane than its radius. Else the image of the light is laid flat onto the plane of the
// triangle, as large as the ball of the light cuts that plane, and as bright as the plane is near. o: where the
// lamp is in the space of the vertices, rgb: its colour times 255.
function dlTriangle(pos, st, a, b, c, o, radius, rgb) {
	const A = a * 3, B = b * 3, C = c * 3;
	// which side of the cube around the lamp each corner is outside of
	let clip = 63;
	for (let v = 0; v < 3; v++) {
		const p = v === 0 ? A : v === 1 ? B : C, dx = o[0] - pos[p], dy = o[1] - pos[p + 1], dz = o[2] - pos[p + 2];
		clip &= (dx < -radius ? 1 : dx > radius ? 2 : 0) | (dy < -radius ? 4 : dy > radius ? 8 : 0) | (dz < -radius ? 16 : dz > radius ? 32 : 0);
	}
	if (clip || dlVerts + 3 > DL_MAX_VERTS) return;
	let e1x = pos[A] - pos[B], e1y = pos[A + 1] - pos[B + 1], e1z = pos[A + 2] - pos[B + 2];
	let e2x = pos[C] - pos[B], e2y = pos[C + 1] - pos[B + 1], e2z = pos[C + 2] - pos[B + 2];
	let nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
	const nn = nx * nx + ny * ny + nz * nz;
	let fac = nx * (o[0] - pos[A]) + ny * (o[1] - pos[A + 1]) + nz * (o[2] - pos[A + 2]);
	if (fac <= 0 || nn < 1e-8) return;
	const nl = Math.sqrt(nn);
	nx /= nl; ny /= nl; nz /= nl; fac /= nl;
	if (fac >= radius) return;
	const modulate = 1 - fac * fac / (radius * radius), scale = 0.5 / Math.sqrt(radius * radius - fac * fac);
	// the two ways the image lies in the plane: along the longer of two sides of the triangle, and across it
	const l1 = e1x * e1x + e1y * e1y + e1z * e1z, l2 = e2x * e2x + e2y * e2y + e2z * e2z;
	if (l1 > l2) {
		const l = Math.sqrt(l1);
		e1x /= l; e1y /= l; e1z /= l;
		e2x = e1y * nz - e1z * ny; e2y = e1z * nx - e1x * nz; e2z = e1x * ny - e1y * nx;
	} else {
		const l = Math.sqrt(l2);
		e2x /= l; e2y /= l; e2z /= l;
		e1x = ny * e2z - nz * e2y; e1y = nz * e2x - nx * e2z; e1z = nx * e2y - ny * e2x;
	}
	const tc = [0, 0, 0, 0, 0, 0];
	for (let v = 0; v < 3; v++) {
		const p = v === 0 ? A : v === 1 ? B : C, dx = pos[p] - o[0], dy = pos[p + 1] - o[1], dz = pos[p + 2] - o[2];
		tc[v * 2] = (dx * e1x + dy * e1y + dz * e1z) * scale + 0.5;
		tc[v * 2 + 1] = (dx * e2x + dy * e2y + dz * e2z) * scale + 0.5;
	}
	// all of it to one side of the image: the light does not reach it
	if ((tc[0] < 0 && tc[2] < 0 && tc[4] < 0) || (tc[0] > 1 && tc[2] > 1 && tc[4] > 1) || (tc[1] < 0 && tc[3] < 0 && tc[5] < 0) || (tc[1] > 1 && tc[3] > 1 && tc[5] > 1)) return;
	// the colour as the bytes the engine makes of it
	const r = Math.trunc(rgb[0] * modulate) & 255, g = Math.trunc(rgb[1] * modulate) & 255, bl = Math.trunc(rgb[2] * modulate) & 255;
	for (let v = 0; v < 3; v++) {
		const i = v === 0 ? a : v === 1 ? b : c, f = dlVerts * (DL_STRIDE / 4), by = dlVerts * DL_STRIDE + 28;
		dlFloats[f] = pos[i * 3]; dlFloats[f + 1] = pos[i * 3 + 1]; dlFloats[f + 2] = pos[i * 3 + 2];
		dlFloats[f + 3] = st[i * 2]; dlFloats[f + 4] = st[i * 2 + 1];
		dlFloats[f + 5] = tc[v * 2]; dlFloats[f + 6] = tc[v * 2 + 1];
		dlBytes[by] = r; dlBytes[by + 1] = g; dlBytes[by + 2] = bl; dlBytes[by + 3] = 255;
		dlVerts++;
	}
}

// after the solid surfaces of the map: the map lit by the lights of the frame
function drawDlights(now) {
	dlStats.tris = dlStats.draws = 0;
	if (!dlights.length || !world || !world.scripted || scripts.state !== 2) return;
	const image = dlTexture();
	if (!image) return;
	if (!dlIndex || dlIndex.list !== world.scripted) dlBuildIndex();
	const index = dlIndex, lit = new Map(), rgbs = dlights.map(dl => dl.color.map(v => v * 255));
	// what a batch has of lit triangles, in the order of the lights: [light, a, b, c, ...] by batch and place
	const bucket = (e, place) => {
		let per = lit.get(e);
		if (!per) lit.set(e, per = new Map());
		let list = per.get(place);
		if (!list) per.set(place, list = []);
		return list;
	};
	dlights.forEach((dl, n) => {
		const o = dl.origin, r = dl.radius;
		// the triangles that stand still, from the cells the cube around the lamp reaches into
		const lo = [0, 1, 2].map(k => Math.max(0, Math.floor((o[k] - r) / DL_CELL) + 512)), hi = [0, 1, 2].map(k => Math.min(1023, Math.floor((o[k] + r) / DL_CELL) + 512));
		for (let x = lo[0]; x <= hi[0]; x++) for (let y = lo[1]; y <= hi[1]; y++) for (let z = lo[2]; z <= hi[2]; z++) {
			const list = index.cells.get((x * 1024 + y) * 1024 + z);
			if (!list) continue;
			for (const ref of list) {
				const e = index.ents[Math.floor(ref / 0x4000000)], i = ref % 0x4000000, data = dlBatch(e.b);
				// a triangle is in several cells: once for a lamp (the lamps count from 1 anew in every frame)
				const mark = dlFrame * MAX_DLIGHTS + n + 1;
				if (data.stamp[i / 3] === mark) continue;
				data.stamp[i / 3] = mark;
				bucket(e, null).push(n, e.b.tri[i], e.b.tri[i + 1], e.b.tri[i + 2]);
			}
		}
		// the patches: those whose box the light reaches (R_DlightGrid), with the triangles they have now
		for (const { e, grid, data } of index.grids) {
			const info = dlGrid(grid, data.pos);
			if (o[0] - r > info.maxs[0] || o[0] + r < info.mins[0] || o[1] - r > info.maxs[1] || o[1] + r < info.mins[1] || o[2] - r > info.maxs[2] || o[2] + r < info.mins[2]) continue;
			const list = bucket(e, null);
			for (let i = 0; i < info.count; i += 3) list.push(n, info.idx[i], info.idx[i + 1], info.idx[i + 2]);
		}
	});
	dlFrame = (dlFrame + 1) % 100000000;
	// The brush models that are entities (R_DlightBmodel): every lamp that is near enough to the box of the model
	// where it stands now, in the space of the model
	const moved = new Map();
	for (const e of index.models) {
		const places = moverPlaces(e.b.model, now), box = world.models[e.b.model], data = dlBatch(e.b), tri = e.b.tri;
		for (const place of places) {
			let at = moved.get(place);
			if (!at) moved.set(place, at = dlights.map(dl => place ? M4.transform(M4.invert(place), dl.origin).slice(0, 3) : dl.origin));
			dlights.forEach((dl, n) => {
				const o = at[n], r = dl.radius;
				if (box && [0, 1, 2].some(k => o[k] - box.maxs[k] > r || box.mins[k] - o[k] > r)) return;
				const list = bucket(e, place);
				let g = 0;
				for (let i = 0; i + 2 < tri.length; i += 3) {
					while (g < data.grids.length && i >= data.grids[g].first + (data.grids[g].width - 1) * (data.grids[g].height - 1) * 6) g++;
					if (g < data.grids.length && i >= data.grids[g].first) continue;
					list.push(n, tri[i], tri[i + 1], tri[i + 2]);
				}
				for (const grid of data.grids) {
					const info = dlGrid(grid, data.pos);
					for (let i = 0; i < info.count; i += 3) list.push(n, info.idx[i], info.idx[i + 1], info.idx[i + 2]);
				}
			});
		}
	}

	// the vertices of all of it into one buffer, batch by batch
	dlVerts = 0;
	const draws = [];
	for (const [e, per] of lit) {
		const data = dlBatch(e.b);
		for (const [place, list] of per) {
			const from = dlVerts, at = place ? moved.get(place) : null;
			for (let i = 0; i < list.length; i += 4) {
				const dl = dlights[list[i]];
				dlTriangle(data.pos, data.st, list[i + 1], list[i + 2], list[i + 3], at ? at[list[i]] : dl.origin, dl.radius, rgbs[list[i]]);
			}
			if (dlVerts > from) draws.push({ e, place, first: from, count: dlVerts - from });
		}
	}
	dlStats.tris = dlVerts / 3;
	dlStats.draws = draws.length;
	if (!draws.length) return;

	if (!dlVao) {
		dlVao = gl.createVertexArray(); dlVbo = gl.createBuffer();
		gl.bindVertexArray(dlVao);
		gl.bindBuffer(gl.ARRAY_BUFFER, dlVbo);
		gl.bufferData(gl.ARRAY_BUFFER, dlBuffer.byteLength, gl.DYNAMIC_DRAW);
		gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, DL_STRIDE, 0);
		gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 2, gl.FLOAT, false, DL_STRIDE, 12);
		gl.enableVertexAttribArray(2); gl.vertexAttribPointer(2, 2, gl.FLOAT, false, DL_STRIDE, 20);
		gl.enableVertexAttribArray(3); gl.vertexAttribPointer(3, 4, gl.UNSIGNED_BYTE, true, DL_STRIDE, 28);
		// the triangles are written one after the other
		gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, gl.createBuffer());
		gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, Uint32Array.from({ length: DL_MAX_VERTS }, (_, i) => i), gl.STATIC_DRAW);
		gl.bindVertexArray(null);
	}
	// With the program of the map and what it was last given of a place: the depth of a lit triangle has to come
	// out as the very depth the surface was drawn with, the pass is only drawn where the two are equal.
	const program = gl.getParameter(gl.CURRENT_PROGRAM), t = now / 1000, open = viewCulls();
	gl.useProgram(stageProg.p);
	gl.uniform1i(stageProg.u.uTex, 0);
	gl.uniform1i(stageProg.u.uSprite, 0);
	gl.activeTexture(gl.TEXTURE0);
	gl.colorMask(true, true, true, false);
	setDeform(stageProg, NO_DEFORM, t);
	stageBegin(stageProg, null);
	gl.bindBuffer(gl.ARRAY_BUFFER, dlVbo);
	gl.bufferSubData(gl.ARRAY_BUFFER, 0, dlBytes, 0, dlVerts * DL_STRIDE);
	for (const d of draws) {
		const st = dlStageOf(d.e.mat, d.e.b.shaderName);
		// the image of the light is the second image of the pass, or the only one
		(st.alone ? st : st.multi).tex = image;
		gl.uniformMatrix4fv(stageProg.u.uMvp, false, d.place ? M4.mul(viewProj, d.place) : viewProj);
		gl.uniform3fv(stageProg.u.uEye, d.place ? eyeAt(d.place) : eye);
		setCull(open ? d.e.mat.cull : 2);
		if (d.e.mat.polygonOffset) { gl.enable(gl.POLYGON_OFFSET_FILL); gl.polygonOffset(-1, -2); }
		// the fog of the whole map is not on this pass (the engine switches it off for it)
		drawStage(st, t, null, dlVao, d.count, gl.UNSIGNED_INT, d.first * 4, stageProg, null);
		if (d.e.mat.polygonOffset) gl.disable(gl.POLYGON_OFFSET_FILL);
	}
	gl.disable(gl.CULL_FACE);
	gl.disable(gl.BLEND);
	gl.depthMask(true);
	gl.depthFunc(gl.LESS);
	gl.colorMask(true, true, true, true);
	gl.useProgram(program);
}
