/* ------------------------------------------------------------------ */
/* BSP loading                                                         */
/* ------------------------------------------------------------------ */

const SURF_SKY = 0x2000, SURF_NODRAW = 0x200000;
const MST_PLANAR = 1, MST_PATCH = 2, MST_TRIANGLE_SOUP = 3;
const SKIP_SHADER = /(^|\/)(nodraw|caulk|clip|trigger|hint|skip|areaportal|playerclip|botclip|weapclip|origin)|system\/|skies\//i;
const VERT_SIZE = 80, SURF_SIZE = 148, SHADER_SIZE = 72;

function parseBsp(buffer) {
	const dv = new DataView(buffer);
	const ident = String.fromCharCode(dv.getUint8(0), dv.getUint8(1), dv.getUint8(2), dv.getUint8(3));
	if (ident !== 'RBSP') throw new Error('not a Jedi Academy map (' + ident + ')');
	const lump = n => ({ ofs: dv.getInt32(8 + n * 8, true), len: dv.getInt32(12 + n * 8, true) });
	// The entities of the map file as the text they are: the client game reads its own entities from it (the sky
	// portal, the models of misc_model_static) and the keys of the worldspawn, and so does the page.
	const entLump = lump(0), entityText = new TextDecoder('latin1').decode(new Uint8Array(buffer, entLump.ofs, Math.max(0, entLump.len))).replace(/\0+$/, '');

	const sl = lump(1), shaders = [];
	for (let o = sl.ofs; o < sl.ofs + sl.len; o += SHADER_SIZE) {
		let name = '';
		for (let i = 0; i < 64; i++) { const ch = dv.getUint8(o + i); if (!ch) break; name += String.fromCharCode(ch); }
		shaders.push({ name, flags: dv.getInt32(o + 64, true), contents: dv.getInt32(o + 68, true) });
	}

	const vl = lump(10), numVerts = vl.len / VERT_SIZE;
	const il = lump(11), indexes = new Int32Array(buffer.slice(il.ofs, il.ofs + il.len));
	const ml = lump(7);
	const bounds = { mins: [0, 1, 2].map(i => dv.getFloat32(ml.ofs + i * 4, true)), maxs: [0, 1, 2].map(i => dv.getFloat32(ml.ofs + 12 + i * 4, true)) };
	// every brush model: the world first, then the ones an entity names with *1, *2, ...
	const models = [];
	for (let o = ml.ofs; o + 40 <= ml.ofs + ml.len; o += 40) {
		models.push({ mins: [0, 1, 2].map(i => dv.getFloat32(o + i * 4, true)), maxs: [0, 1, 2].map(i => dv.getFloat32(o + 12 + i * 4, true)),
			firstSurface: dv.getInt32(o + 24, true), numSurfaces: dv.getInt32(o + 28, true),
			firstBrush: dv.getInt32(o + 32, true), numBrushes: dv.getInt32(o + 36, true) });
	}
	// The brushes: what the game collides with, seen or not. Kept as they are in the file (a plane is a normal
	// and a distance, a brush its first side, how many it has and its shader, a side its plane and two more
	// numbers). Their edges are worked out when a view of the editor asks for them.
	const pl = lump(2), bl = lump(8), bsl = lump(9);
	const brushes = {
		planes: new Float32Array(buffer.slice(pl.ofs, pl.ofs + (pl.len & ~3))),
		brushes: new Int32Array(buffer.slice(bl.ofs, bl.ofs + (bl.len & ~3))),
		sides: new Int32Array(buffer.slice(bsl.ofs, bsl.ofs + (bsl.len & ~3)))
	};


	const vert = i => {
		const o = vl.ofs + i * VERT_SIZE;
		return {
			p: [dv.getFloat32(o, true), dv.getFloat32(o + 4, true), dv.getFloat32(o + 8, true)],
			st: [dv.getFloat32(o + 12, true), dv.getFloat32(o + 16, true)],
			lm: [dv.getFloat32(o + 20, true), dv.getFloat32(o + 24, true)],
			n: [dv.getFloat32(o + 52, true), dv.getFloat32(o + 56, true), dv.getFloat32(o + 60, true)],
			c: [dv.getUint8(o + 64), dv.getUint8(o + 65), dv.getUint8(o + 66), dv.getUint8(o + 67)]
		};
	};

	const ll = lump(14), lmSize = 128 * 128 * 3, lightmaps = [];
	for (let o = ll.ofs; o + lmSize <= ll.ofs + ll.len; o += lmSize) lightmaps.push(new Uint8Array(buffer, o, lmSize));

	// batches per shader + lightmap, and per brush model: one that is an entity (a door, a lift) is drawn where
	// that entity is, the world (0) where the file has it
	const batches = new Map();
	const pickPos = [], pickIdx = [], pickModel = [], pickSurf = [];
	// what a surface is made of, for the click that asks: its shader, lightmap, kind, fog, brush model and size
	const surfs = [];
	const sfl = lump(13);
	let sky = '';
	const surfModel = new Uint16Array(Math.floor(sfl.len / SURF_SIZE));
	models.forEach((m, n) => { if (n) surfModel.fill(n, Math.max(0, m.firstSurface), Math.min(surfModel.length, m.firstSurface + m.numSurfaces)); });

	for (let o = sfl.ofs; o < sfl.ofs + sfl.len; o += SURF_SIZE) {
		const shaderNum = dv.getInt32(o, true), fogNum = dv.getInt32(o + 4, true), type = dv.getInt32(o + 8, true);
		const firstVert = dv.getInt32(o + 12, true), nv = dv.getInt32(o + 16, true);
		const firstIndex = dv.getInt32(o + 20, true), ni = dv.getInt32(o + 24, true);
		const lmNum = dv.getInt32(o + 36, true);
		const pw = dv.getInt32(o + 140, true), ph = dv.getInt32(o + 144, true);
		const shader = shaders[shaderNum];
		// the sky is not a surface here: its shader is drawn around the eye
		if (shader && (shader.flags & SURF_SKY) && !sky) sky = shader.name;
		if (!shader || (shader.flags & (SURF_SKY | SURF_NODRAW)) || SKIP_SHADER.test(shader.name)) continue;
		if (type !== MST_PLANAR && type !== MST_TRIANGLE_SOUP && type !== MST_PATCH) continue;

		let verts, tris;
		if (type === MST_PATCH) {
			[verts, tris] = tessellatePatch(vert, firstVert, pw, ph);
		} else {
			verts = [];
			for (let i = 0; i < nv; i++) verts.push(vert(firstVert + i));
			tris = Array.from(indexes.subarray(firstIndex, firstIndex + ni));
		}
		if (!tris.length) continue;

		const model = surfModel[(o - sfl.ofs) / SURF_SIZE];
		const key = shaderNum + ':' + (lmNum >= 0 && lmNum < lightmaps.length ? lmNum : -1) + ':' + fogNum + ':' + model;
		let b = batches.get(key);
		if (!b) {
			b = { shader: shaderNum, fog: fogNum, model, lightmap: lmNum >= 0 && lmNum < lightmaps.length ? lmNum : -1, pos: [], st: [], lm: [], col: [], norm: [], idx: [] };
			batches.set(key, b);
		}
		const base = b.pos.length / 3;
		for (const v of verts) {
			b.pos.push(v.p[0], v.p[1], v.p[2]);
			b.st.push(v.st[0], v.st[1]);
			b.lm.push(v.lm[0], v.lm[1]);
			b.col.push(v.c[0], v.c[1], v.c[2], v.c[3]);
			b.norm.push(v.n[0], v.n[1], v.n[2]);
		}
		for (const t of tris) b.idx.push(base + t);

		// solid surfaces for right-click picking
		const pbase = pickPos.length / 3;
		for (const v of verts) pickPos.push(v.p[0], v.p[1], v.p[2]);
		for (const t of tris) pickIdx.push(pbase + t);
		for (let i = 0; i < tris.length; i += 3) { pickModel.push(model); pickSurf.push(surfs.length); }
		surfs.push({ shader: shaderNum, lm: lmNum, type, fog: fogNum, model, verts: verts.length, tris: tris.length / 3, patch: type === MST_PATCH ? [pw, ph] : null });
	}

	// The fogs (R_LoadFogs in tr_bsp.cpp): a shader with fogParms and the brush that is filled with it. No brush
	// means the whole map. The side of the brush that is seen from outside is the plane the fog ends at, kept as
	// the engine keeps it: looking into the fog.
	const fl = lump(12), fogs = [];
	for (let o = fl.ofs; o + 72 <= fl.ofs + fl.len; o += 72) {
		let name = '';
		for (let i = 0; i < 64; i++) { const ch = dv.getUint8(o + i); if (!ch) break; name += String.fromCharCode(ch); }
		const brush = dv.getInt32(o + 64, true), side = dv.getInt32(o + 68, true);
		const pn = brush >= 0 && side >= 0 ? brushes.sides[(brushes.brushes[brush * 3] + side) * 3] * 4 : -1, P = brushes.planes;
		fogs.push({ shader: name, global: brush === -1, plane: pn >= 0 && pn + 3 < P.length ? [-P[pn], -P[pn + 1], -P[pn + 2], -P[pn + 3]] : [0, 0, 0, 0] });
	}
	// The light grid: the light a model gets where it stands (R_LoadLightGrid in tr_bsp.cpp). The map is cut into
	// cells, 64 x 64 x 128 units unless the worldspawn says otherwise, every cell names its sample in the array,
	// and a sample is the ambient and the directed light of four styles, the styles, and where the light comes from.
	const el = lump(0), gdl = lump(15), gal = lump(17);
	const worldspawn = /\{([^}]*)\}/.exec(new TextDecoder('latin1').decode(new Uint8Array(buffer, el.ofs, Math.min(el.len, 8192))));
	const gridKey = worldspawn && /"gridsize"\s+"([^"]*)"/i.exec(worldspawn[1]);
	const cell = gridKey ? gridKey[1].trim().split(/\s+/).map(Number) : [];
	const gridSize = cell.length === 3 && cell.every(v => v > 0) ? cell : [64, 64, 128];
	const gridOrigin = gridSize.map((g, i) => g * Math.ceil(models[0].mins[i] / g));
	const grid = {
		size: gridSize, origin: gridOrigin,
		bounds: gridSize.map((g, i) => Math.floor((g * Math.floor(models[0].maxs[i] / g) - gridOrigin[i]) / g) + 1),
		data: new Uint8Array(buffer.slice(gdl.ofs, gdl.ofs + gdl.len)),
		array: new Uint16Array(buffer.slice(gal.ofs, gal.ofs + (gal.len & ~1)))
	};
	const stats = { surfaces: Math.floor(sfl.len / SURF_SIZE), vertices: Math.floor(numVerts), indexes: indexes.length, shaders: shaders.length, lightmaps: lightmaps.length,
		brushModels: models.length, brushes: brushes.brushes.length / 3, planes: brushes.planes.length / 4, bytes: buffer.byteLength };
	// the triangles of one brush model after the other: a click asks each where its entity is
	const pickRuns = [];
	for (let i = 0; i < pickModel.length; i++) {
		const run = pickRuns[pickRuns.length - 1];
		if (run && run[0] === pickModel[i]) run[2] = (i + 1) * 3; else pickRuns.push([pickModel[i], i * 3, (i + 1) * 3]);
	}
	return { entityText, shaders, lightmaps, batches: [...batches.values()], bounds, sky, models, brushes, fogs, grid, stats, surfs, pick: { pos: new Float32Array(pickPos), idx: new Uint32Array(pickIdx), model: new Uint16Array(pickModel), surf: new Uint32Array(pickSurf), runs: pickRuns } };
}

// quadratic bezier patch, 3x3 control blocks
function tessellatePatch(vert, firstVert, w, h) {
	const L = 6, verts = [], tris = [];
	const lerp3 = (a, b, c, t, f) => {
		const it = 1 - t;
		return a.map((_, i) => it * it * f(a, i) + 2 * it * t * f(b, i) + t * t * f(c, i));
	};
	const mix = (a, b, c, t) => {
		const it = 1 - t, k0 = it * it, k1 = 2 * it * t, k2 = t * t;
		return {
			p: [0, 1, 2].map(i => k0 * a.p[i] + k1 * b.p[i] + k2 * c.p[i]),
			st: [0, 1].map(i => k0 * a.st[i] + k1 * b.st[i] + k2 * c.st[i]),
			lm: [0, 1].map(i => k0 * a.lm[i] + k1 * b.lm[i] + k2 * c.lm[i]),
			n: [0, 1, 2].map(i => k0 * a.n[i] + k1 * b.n[i] + k2 * c.n[i]),
			c: [0, 1, 2, 3].map(i => k0 * a.c[i] + k1 * b.c[i] + k2 * c.c[i])
		};
	};
	for (let py = 0; py + 2 < h; py += 2) {
		for (let px = 0; px + 2 < w; px += 2) {
			const cp = [];
			for (let j = 0; j < 3; j++) for (let i = 0; i < 3; i++) cp.push(vert(firstVert + (py + j) * w + px + i));
			const base = verts.length;
			for (let j = 0; j <= L; j++) {
				const v = j / L;
				const rows = [0, 1, 2].map(r => mix(cp[r], cp[3 + r], cp[6 + r], v));
				for (let i = 0; i <= L; i++) verts.push(mix(rows[0], rows[1], rows[2], i / L));
			}
			for (let j = 0; j < L; j++) {
				for (let i = 0; i < L; i++) {
					const a = base + j * (L + 1) + i, b = a + 1, c = a + L + 1, d = c + 1;
					tris.push(a, c, b, b, c, d);
				}
			}
		}
	}
	return [verts, tris];
}

// minimal TGA decoder: uncompressed and RLE true color
function decodeTga(buf) {
	const d = new Uint8Array(buf);
	const idLen = d[0], type = d[2], w = d[12] | (d[13] << 8), h = d[14] | (d[15] << 8), bpp = d[16], desc = d[17];
	if ((type !== 2 && type !== 10) || (bpp !== 24 && bpp !== 32)) return null;
	const px = bpp / 8, out = new Uint8Array(w * h * 4);
	let p = 18 + idLen + (d[1] ? (d[5] | (d[6] << 8)) * (d[7] >> 3) : 0), n = 0, alpha = false;
	const put = (o) => {
		out[n * 4] = d[o + 2]; out[n * 4 + 1] = d[o + 1]; out[n * 4 + 2] = d[o];
		out[n * 4 + 3] = px === 4 ? d[o + 3] : 255;
		if (px === 4 && d[o + 3] < 250) alpha = true;
		n++;
	};
	while (n < w * h && p < d.length) {
		if (type === 2) { put(p); p += px; continue; }
		const hdr = d[p++], count = (hdr & 127) + 1;
		if (hdr & 128) { for (let i = 0; i < count && n < w * h; i++) put(p); p += px; }
		else { for (let i = 0; i < count && n < w * h; i++) { put(p); p += px; } }
	}
	// TGA rows go bottom up unless bit 5 of the descriptor is set
	if (!(desc & 0x20)) {
		const row = w * 4, tmp = new Uint8Array(row);
		for (let y = 0; y < h >> 1; y++) {
			const a = y * row, b = (h - 1 - y) * row;
			tmp.set(out.subarray(a, a + row)); out.copyWithin(a, b, b + row); out.set(tmp, b);
		}
	}
	return { w, h, data: out, alpha };
}

