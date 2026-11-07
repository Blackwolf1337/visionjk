// ---- marks on the map (CG_ImpactMark and CG_AddMarks in cg_marks.c with R_MarkFragments of the engine): the
// shadow under a player, what a shot or a blade leaves on a wall
//
// A mark is a square with a shader that is laid onto the map: the engine cuts it against the triangles of the
// surfaces near it, and what is left of it on each triangle is drawn as a polygon with that shader. A shadow is
// such a mark for one frame (CG_PlayerShadow in cg_players.c), the others stay ten seconds and fade in the last.
// The game has three ways to leave one, and each has its function here:
//   impactMark    CG_ImpactMark: the shadow, a footprint
//   decalMark     RE_AddDecalToScene of the renderer (tr_decals.cpp): the decal of an effect file. These stay
//                 until the hundredth after them takes their place, and fade then.
//   saberMarks    CG_CreateSaberMarks: the burn a blade leaves where it is drawn along a wall, with its glow.
//                 It cuts a polygon of its own (markFragments) and fills in its marks itself (markKeep).

const MARK_MAX_FRAGMENTS = 128, MARK_MAX_POINTS = 384;	// CG_ImpactMark: what R_MarkFragments may hand back
const MARK_POLY_VERTS = 10;			// MAX_VERTS_ON_POLY of cg_local.h: the corners a polygon of a mark keeps
const MARK_CHOP_VERTS = 64;			// MAX_VERTS_ON_POLY of tr_marks.cpp: the corners a polygon may have while it is cut
const MARK_MAX_SURFACES = 64;		// the surfaces R_MarkFragments looks at
const MARK_MAX_POLYS = 256;			// MAX_MARK_POLYS: the marks that stay
const MARK_MAX_NOW = 2048;			// the polygons of one frame that are kept until that frame is drawn
const MARK_TOTAL_TIME = 10000, MARK_FADE_TIME = 1000;
const MARK_DECALS = 100, MARK_DECAL_FADE_TIME = 1000;	// r_markcount and DECAL_FADE_TIME of tr_decals.cpp
// surfaceflags.h: a shader that takes no marks, and one that is a fog
const MARK_SURF_NONE = 0x80000 | 0x100000, MARK_CONTENTS_FOG = 0x8;
// CG_PlayerShadow: the shader, how far down the ground is looked for, the box that is moved down (cg_shadows 1)
const MARK_SHADOW = 'markShadow', MARK_SHADOW_DISTANCE = 128, MARK_SHADOW_MINS = [-15, -15, 0], MARK_SHADOW_MAXS = [15, 15, 2];
// bg_public.h and teams.h, as CG_PlayerShadow reads them
const MARK_EF_DEAD = 1 << 1, MARK_EF2_SHIP_DEATH = 1 << 7, MARK_PW_CLOAKED = 11, MARK_CLASS_REMOTE = 39, MARK_CLASS_SEEKER = 41;
// CG_CreateSaberMarks: how wide the burn of a blade is to each side, its two shaders (cg_main.c), how old its glow is made
const MARK_SABER_RADIUS = 0.65, MARK_RIVET = 'gfx/damage/rivetmark', MARK_SABER_GLOW = 'gfx/effects/saberDamageGlow', MARK_SABER_GLOW_AGE = 8500;
const MARK_STRIDE = 24, MARK_MAX_VERTS = 16384;		// a vertex as the effects have it: place, st, four bytes of colour

const marksAlive = [];		// the marks of the client game that stay, the newest first: { time, alphaFade, color, poly }
let marksNow = [];			// the polygons of this frame only (shadows): { shader, n, pos, st, rgba }
let markTime = 0;			// cg.time of the frame that is drawn
// The decals of the renderer (tr_decals.cpp): the ones that stay, and the ones that fade because their place was
// taken. Each list is a ring of MARK_DECALS places, { time, fadetime, poly } or null, and where the next one goes.
const markDecals = [new Array(MARK_DECALS).fill(null), new Array(MARK_DECALS).fill(null)], markDecalHead = [0, 0];
const markStats = { polys: 0, shadows: 0, batches: 0 };

// What R_MarkFragments needs of the map that is shown, made once: the surfaces of the map by leaf, as
// R_BoxSurfaces_r finds them (the map file lists the surfaces of a leaf, which the page does not keep: a surface
// is in every leaf its box reaches into, see js/trace.js), what each is, and the plane of a face. The flags of
// a shader are the ones the map file has for it. What a shader marks as not to be marked is left out here.
let markMapNow = null;
function markMap() {
	const tm = traceMap();
	if (!tm) return null;
	if (markMapNow && markMapNow.world === world) return markMapNow;
	const surfs = world.surfs, n = surfs.length, pick = world.pick, tree = world.tree, leafs = tree.leafs.length / 12 | 0;
	const kind = new Uint8Array(n), planes = new Float32Array(n * 4), perLeaf = new Array(leafs).fill(null), mins = [0, 0, 0], maxs = [0, 0, 0];
	surfs.forEach((s, at) => {
		if (!s || s.model !== 0 || tm.surfFirst[at] < 0) return;
		const sh = world.shaders[s.shader] || {};
		if ((sh.flags & MARK_SURF_NONE) || (sh.contents & MARK_CONTENTS_FOG)) return;
		const first = tm.surfFirst[at] * 3, end = first + tm.surfTris[at] * 3;
		mins.fill(Infinity); maxs.fill(-Infinity);
		for (let i = first; i < end; i++) {
			const v = pick.idx[i] * 3;
			for (let k = 0; k < 3; k++) { mins[k] = Math.min(mins[k], pick.pos[v + k]); maxs[k] = Math.max(maxs[k], pick.pos[v + k]); }
		}
		// 1 a face, 2 a grid, 3 a mesh of triangles (which takes a place in the list and gets no mark: r_marksOnTriangleMeshes 0)
		kind[at] = s.type === MST_PLANAR ? 1 : s.type === MST_PATCH ? 2 : 3;
		if (kind[at] === 1) {
			// The plane of a face. The map file has its normal, which the page does not keep: it is the one of its
			// first triangle that has an area, towards the side that is seen.
			let found = false;
			for (let i = first; i < end && !found; i += 3) {
				const a = pick.idx[i] * 3, b = pick.idx[i + 1] * 3, c = pick.idx[i + 2] * 3, p = pick.pos;
				const ux = p[a] - p[b], uy = p[a + 1] - p[b + 1], uz = p[a + 2] - p[b + 2], vx = p[c] - p[b], vy = p[c + 1] - p[b + 1], vz = p[c + 2] - p[b + 2];
				const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx, l = Math.hypot(nx, ny, nz);
				if (l < 1e-6) continue;
				planes[at * 4] = nx / l; planes[at * 4 + 1] = ny / l; planes[at * 4 + 2] = nz / l;
				planes[at * 4 + 3] = (nx * p[a] + ny * p[a + 1] + nz * p[a + 2]) / l;
				found = true;
			}
			if (!found) { kind[at] = 0; return; }
		}
		traceBoxLeafs(tree, mins, maxs, leaf => { if (leaf < leafs) (perLeaf[leaf] || (perLeaf[leaf] = [])).push(at); });
	});
	markMapNow = { world, kind, planes, leafSurfs: traceLeafLists(perLeaf), seen: new Int32Array(n), count: 0 };
	return markMapNow;
}

// R_ChopPolyBehindPlane: what of a polygon lies in front of a plane, n corners of from into to. How many it has then.
const markClip = [new Float32Array((MARK_CHOP_VERTS + 2) * 3), new Float32Array((MARK_CHOP_VERTS + 2) * 3)];
const markDists = new Float32Array(MARK_CHOP_VERTS + 4), markSides = new Uint8Array(MARK_CHOP_VERTS + 4);
function markChop(from, n, to, nx, ny, nz, dist, epsilon) {
	// one that might get too many corners is dropped
	if (n >= MARK_CHOP_VERTS - 2) return 0;
	let front = 0, back = 0;
	for (let i = 0; i < n; i++) {
		const d = markDists[i] = from[i * 3] * nx + from[i * 3 + 1] * ny + from[i * 3 + 2] * nz - dist;
		if (d > epsilon) { markSides[i] = 0; front++; } else if (d < -epsilon) { markSides[i] = 1; back++; } else markSides[i] = 2;
	}
	markSides[n] = markSides[0]; markDists[n] = markDists[0];
	if (!front) return 0;
	if (!back) { to.set(from.subarray(0, n * 3)); return n; }
	let m = 0;
	for (let i = 0; i < n; i++) {
		const p = i * 3;
		if (markSides[i] === 2) { to[m * 3] = from[p]; to[m * 3 + 1] = from[p + 1]; to[m * 3 + 2] = from[p + 2]; m++; continue; }
		if (markSides[i] === 0) { to[m * 3] = from[p]; to[m * 3 + 1] = from[p + 1]; to[m * 3 + 2] = from[p + 2]; m++; }
		if (markSides[i + 1] === 2 || markSides[i + 1] === markSides[i]) continue;
		// the point where this edge goes through the plane
		const q = (i + 1) % n * 3, d = markDists[i] - markDists[i + 1], k = d === 0 ? 0 : markDists[i] / d;
		to[m * 3] = from[p] + k * (from[q] - from[p]); to[m * 3 + 1] = from[p + 1] + k * (from[q + 1] - from[p + 1]); to[m * 3 + 2] = from[p + 2] + k * (from[q + 2] - from[p + 2]);
		m++;
	}
	return m;
}

// R_AddMarkFragments: the triangle in markClip[0] is cut by all the planes around the mark, and what is left of
// it is one more fragment
function markAddFragment(planes, numPlanes, out) {
	let n = 3, ping = 0;
	for (let i = 0; i < numPlanes && n; i++, ping ^= 1) n = markChop(markClip[ping], n, markClip[ping ^ 1], planes[i * 4], planes[i * 4 + 1], planes[i * 4 + 2], planes[i * 4 + 3], 0.5);
	if (!n || n + out.points > MARK_MAX_POINTS) return;
	out.fragments.push(markClip[ping].slice(0, n * 3));
	out.points += n;
}

// R_MarkFragments: a polygon (points, a flat list of its corners) is pushed along projection onto the map. The
// answer is the list of what is left of it on every triangle it meets, each a flat list of corners.
const markPlanes = new Float32Array((MARK_CHOP_VERTS + 2) * 4);
function markFragments(points, projection) {
	const mm = markMap(), out = { fragments: [], points: 0 };
	if (!mm) return out.fragments;
	const numPoints = Math.min(MARK_CHOP_VERTS, points.length / 3 | 0), pl = Math.hypot(projection[0], projection[1], projection[2]) || 1;
	const dir = [projection[0] / pl, projection[1] / pl, projection[2] / pl], mins = [Infinity, Infinity, Infinity], maxs = [-Infinity, -Infinity, -Infinity];
	// the box around the mark, where it is, where it is pushed to and a little in front of the surface
	for (let i = 0; i < points.length; i += 3) {
		for (let k = 0; k < 3; k++) {
			const p = points[i + k], a = p + projection[k], b = p - 20 * dir[k];
			mins[k] = Math.min(mins[k], p, a, b); maxs[k] = Math.max(maxs[k], p, a, b);
		}
	}
	// the planes around it: one through every edge along the way it is pushed, one in front and one behind
	const P = markPlanes;
	for (let i = 0; i < numPoints; i++) {
		const a = i * 3, b = (i + 1) % numPoints * 3;
		const v1x = points[b] - points[a], v1y = points[b + 1] - points[a + 1], v1z = points[b + 2] - points[a + 2], v2x = -projection[0], v2y = -projection[1], v2z = -projection[2];
		let nx = v1y * v2z - v1z * v2y, ny = v1z * v2x - v1x * v2z, nz = v1x * v2y - v1y * v2x;
		const l = Math.hypot(nx, ny, nz) || 1;
		nx /= l; ny /= l; nz /= l;
		P[i * 4] = nx; P[i * 4 + 1] = ny; P[i * 4 + 2] = nz; P[i * 4 + 3] = nx * points[a] + ny * points[a + 1] + nz * points[a + 2];
	}
	const along = dir[0] * points[0] + dir[1] * points[1] + dir[2] * points[2], np = numPoints * 4;
	P[np] = dir[0]; P[np + 1] = dir[1]; P[np + 2] = dir[2]; P[np + 3] = along - 32;
	P[np + 4] = -dir[0]; P[np + 5] = -dir[1]; P[np + 6] = -dir[2]; P[np + 7] = -along - 20;
	const numPlanes = numPoints + 2;

	// R_BoxSurfaces_r: the surfaces of the leafs the box reaches into, each once. A face only when its plane goes
	// through the box and it faces the mark.
	const list = [], stamp = ++mm.count, kind = mm.kind, ls = mm.leafSurfs;
	traceBoxLeafs(world.tree, mins, maxs, leaf => {
		for (let k = ls.first[leaf]; k < ls.first[leaf + 1] && list.length < MARK_MAX_SURFACES; k++) {
			const s = ls.list[k];
			if (mm.seen[s] === stamp) continue;
			mm.seen[s] = stamp;
			if (kind[s] === 1 && (traceBoxSide(mm.planes, s, mins, maxs) !== 3 || mm.planes[s * 4] * dir[0] + mm.planes[s * 4 + 1] * dir[1] + mm.planes[s * 4 + 2] * dir[2] > -0.5)) continue;
			list.push(s);
		}
	});

	const tm = traceMap(), pos = world.pick.pos, idx = world.pick.idx, clip = markClip[0];
	for (const s of list) {
		if (kind[s] === 3) continue;
		for (let t = 0; t < tm.surfTris[s]; t++) {
			const i = (tm.surfFirst[s] + t) * 3;
			for (let j = 0; j < 3; j++) { const v = idx[i + j] * 3; clip[j * 3] = pos[v]; clip[j * 3 + 1] = pos[v + 1]; clip[j * 3 + 2] = pos[v + 2]; }
			if (kind[s] === 2) {
				// A triangle of a grid has to face the mark, the second one of a cell a little less than the first.
				// They are in the order of the cells, two for each, as the engine goes through them.
				const ux = clip[0] - clip[3], uy = clip[1] - clip[4], uz = clip[2] - clip[5], vx = clip[6] - clip[3], vy = clip[7] - clip[4], vz = clip[8] - clip[5];
				const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx, l = Math.hypot(nx, ny, nz) || 1;
				if (!((nx * dir[0] + ny * dir[1] + nz * dir[2]) / l < (t & 1 ? -0.05 : -0.1))) continue;
			}
			markAddFragment(P, numPlanes, out);
			if (out.fragments.length === MARK_MAX_FRAGMENTS) return out.fragments;
		}
	}
	return out.fragments;
}

// PerpendicularVector of the engine: a vector that stands on src, towards the axis src is furthest from
function markPerpendicular(src) {
	let pos = 0, least = 1;
	for (let i = 0; i < 3; i++) if (Math.abs(src[i]) < least) { pos = i; least = Math.abs(src[i]); }
	const inv = 1 / (src[0] * src[0] + src[1] * src[1] + src[2] * src[2]), d = src[pos] * inv, out = [-d * src[0] * inv, -d * src[1] * inv, -d * src[2] * inv];
	out[pos] += 1;
	return fxUnit(out);
}

// The polygons of a mark, as CG_ImpactMark and RE_AddDecalToScene both make them: a square of that radius around
// origin on the surface whose normal is dir, turned by orientation (degrees), cut against the map. The colour
// (0 to 1) becomes the bytes of the vertices.
function markPolys(shader, origin, dir, orientation, r, g, b, a, radius) {
	// (a normal of no length is what a trace hands back that began in something solid: no surface to lie on)
	if (!shader || !(radius > 0) || !world || !(dir[0] || dir[1] || dir[2])) return [];
	// the axes of the image on the surface
	const fwd = fxUnit(dir), up = fxTurn(fwd, markPerpendicular(fwd), orientation), right = fxCross(fwd, up), scale = 0.5 / radius;
	const points = new Float32Array(12), sr = [-1, 1, 1, -1], su = [-1, -1, 1, 1];
	for (let k = 0; k < 4; k++) for (let i = 0; i < 3; i++) points[k * 3 + i] = origin[i] + sr[k] * radius * right[i] + su[k] * radius * up[i];
	const rgba = [r * 255 & 255, g * 255 & 255, b * 255 & 255, a * 255 & 255], polys = [];
	for (const f of markFragments(points, [dir[0] * -20, dir[1] * -20, dir[2] * -20])) {
		// a polygon of a mark has an upper limit of corners
		const n = Math.min(MARK_POLY_VERTS, f.length / 3), st = new Float32Array(n * 2);
		for (let j = 0; j < n; j++) {
			const dx = f[j * 3] - origin[0], dy = f[j * 3 + 1] - origin[1], dz = f[j * 3 + 2] - origin[2];
			st[j * 2] = 0.5 + (dx * right[0] + dy * right[1] + dz * right[2]) * scale;
			st[j * 2 + 1] = 0.5 + (dx * up[0] + dy * up[1] + dz * up[2]) * scale;
		}
		polys.push({ shader, n, pos: f, st, rgba: rgba.slice() });
	}
	return polys;
}

// CG_AllocMark with what the client game fills in: a polygon that stays as a mark and fades (CG_AddMarks). pos and
// st are its corners as flat lists. color is the colour of the mark as the game keeps it, which CG_AddMarks
// writes into the bytes of the vertices as it is: CG_ImpactMark keeps 0 to 1 there, the marks of a blade
// (CG_CreateSaberMarks) 0 to 255. age: how long ago it counts as made, in msec (the glow of a blade is made 8500
// old, so that it is gone long before the burn). rgba: the bytes of the vertices until CG_AddMarks sets them.
function markKeep(shader, pos, st, color, alphaFade, age = 0, rgba = [0, 0, 0, 255]) {
	// when all are in use the oldest go, all of the same time
	if (marksAlive.length >= MARK_MAX_POLYS) {
		const time = marksAlive[marksAlive.length - 1].time;
		while (marksAlive.length && marksAlive[marksAlive.length - 1].time === time) marksAlive.pop();
	}
	marksAlive.unshift({ time: markTime - age, alphaFade: !!alphaFade, color: color.slice(), poly: { shader, n: Math.min(MARK_POLY_VERTS, pos.length / 3 | 0), pos, st, rgba: rgba.slice() } });
}

// A mark is left, with the arguments of CG_ImpactMark (cg_marks at 1): the name of its shader, where it is and the
// normal of the surface there, how far it is turned (degrees), its colour (0 to 1), whether it fades by its alpha
// or by its colour, its radius, and whether it is one of this frame only (a shadow) or stays and fades.
function impactMark(shader, origin, dir, orientation, r, g, b, a, alphaFade, radius, temporary) {
	for (const p of markPolys(shader, origin, dir, orientation, r, g, b, a, radius)) {
		if (!temporary) markKeep(shader, p.pos, p.st, [r, g, b, a], alphaFade, 0, p.rgba);
		// (what is asked for while no frame draws the marks, in the plain look of the editor, does not pile up)
		else if (marksNow.length < MARK_MAX_NOW) marksNow.push(p);
	}
}

// CG_CreateSaberMarks (cg_saberDynamicMarks at 0, its default): the burn of a blade on the map, from where it
// touched it a frame ago (start) to where it does now (end), normal is the one of the surface there. A strip
// between the two, a little longer and 0.65 wide to each side, laid onto the map and kept twice: as the burn,
// which fades by its alpha after nine seconds, and as a glow over it that is made old and is gone after one and a half.
function saberMarks(start, end, normal) {
	if (!world || !(normal[0] || normal[1] || normal[2])) return;
	const along = fxUnit([end[0] - start[0], end[1] - start[1], end[2] - start[2]]), across = fxCross(along, normal), r = MARK_SABER_RADIUS, points = new Float32Array(12);
	for (let i = 0; i < 3; i++) {
		points[i] = start[i] - r * along[i] - r * across[i];
		points[3 + i] = end[i] + r * along[i] - r * across[i];
		points[6 + i] = end[i] + r * along[i] + r * across[i];
		points[9 + i] = start[i] - r * along[i] + r * across[i];
	}
	const mid = [(end[0] + start[0]) * 0.5, (end[1] + start[1]) * 0.5, (end[2] + start[2]) * 0.5];
	for (const f of markFragments(points, [-normal[0], -normal[1], -normal[2]])) {
		const n = Math.min(MARK_POLY_VERTS, f.length / 3), st = new Float32Array(n * 2);
		for (let j = 0; j < n; j++) {
			// how far the image is stretched is thrown anew for every corner, as the game does it
			const dx = f[j * 3] - mid[0], dy = f[j * 3 + 1] - mid[1], dz = f[j * 3 + 2] - mid[2];
			st[j * 2] = 0.5 + (dx * along[0] + dy * along[1] + dz * along[2]) * (0.05 + Math.random() * 0.03);
			st[j * 2 + 1] = 0.5 + (dx * across[0] + dy * across[1] + dz * across[2]) * (0.15 + Math.random() * 0.05);
		}
		// the game keeps these colours as bytes (0 to 255), and its glow has the same number for blue and alpha
		markKeep(MARK_RIVET, f, st, [255, 255, 255, 255], true);
		const low = Math.random() * 15;
		markKeep(MARK_SABER_GLOW, f, st, [215 + Math.random() * 40, 96 + Math.random() * 32, low, low], false, MARK_SABER_GLOW_AGE);
	}
}

// RE_FreeDecal: a place of a ring is given up. A decal that stayed fades from now on, in the other ring.
function markDecalFree(type, i) {
	const d = markDecals[type][i];
	if (!d) return;
	if (type === 0) Object.assign(markDecalAlloc(1), { poly: d.poly, time: markTime, fadetime: markTime + MARK_DECAL_FADE_TIME });
	markDecals[type][i] = null;
}

// RE_AllocDecal: the next place of a ring. What is there goes, with all that were made at the same time as it.
function markDecalAlloc(type) {
	const ring = markDecals[type], head = markDecalHead[type], was = ring[head];
	if (was) {
		if (was.time !== markTime) {
			let i = head;
			do {
				if (++i >= MARK_DECALS) i = 0;
				if (!ring[i] || ring[i].time !== was.time) break;
				markDecalFree(type, i);
			} while (i !== head);
		}
		markDecalFree(type, head);
	}
	markDecalHead[type] = head + 1 >= MARK_DECALS ? 0 : head + 1;
	return ring[head] = { time: markTime, fadetime: 0, poly: null };
}

// A decal of the renderer, with the arguments of RE_AddDecalToScene (the same as impactMark has): what an effect
// of the game leaves on a wall (the decal of an .efx file). It stays until its place in the ring is taken, and
// fades then. alphaFade is not looked at, as in the engine.
function decalMark(shader, origin, dir, orientation, r, g, b, a, alphaFade, radius, temporary) {
	for (const p of markPolys(shader, origin, dir, orientation, r, g, b, a, radius)) {
		if (!temporary) markDecalAlloc(0).poly = p;
		else if (marksNow.length < MARK_MAX_NOW) marksNow.push(p);
	}
}

// R_AddDecals: the decals of both rings for this frame, the fading ones by the alpha of their vertices
function markDecalsAdd(into) {
	for (let type = 0; type < 2; type++) {
		const ring = markDecals[type];
		let i = markDecalHead[type];
		do {
			const d = ring[i];
			if (d && d.fadetime) {
				const t = markTime - d.time;
				if (t < MARK_DECAL_FADE_TIME) { d.poly.rgba[3] = 255 * (1 - t / MARK_DECAL_FADE_TIME) & 255; into.push(d.poly); }
				else markDecalFree(type, i);
			} else if (d) {
				into.push(d.poly);
			}
			if (++i >= MARK_DECALS) i = 0;
		} while (i !== markDecalHead[type]);
	}
}

// CG_AddMarks: the marks that stay, for this frame. One is gone after ten seconds and fades in the last of them,
// by its alpha or by its colour. The colour of the vertices is set anew in every frame, and the game writes the
// colour of the mark there as it has it, between 0 and 1, into a byte: a mark that stays has no colour to speak
// of at its vertices, only its alpha. That is what the game draws, so it is what is drawn here.
function marksAdd(t, into) {
	for (let i = marksAlive.length - 1; i >= 0; i--) if (t > marksAlive[i].time + MARK_TOTAL_TIME) marksAlive.splice(i, 1);
	for (const m of marksAlive) {
		const left = m.time + MARK_TOTAL_TIME - t, c = m.poly.rgba;
		if (left < MARK_FADE_TIME) {
			if (m.alphaFade) c[3] = Math.trunc(255 * left / MARK_FADE_TIME) & 255;
			else for (let k = 0; k < 3; k++) c[k] = Math.trunc(m.color[k] * (left / MARK_FADE_TIME)) & 255;
		} else {
			for (let k = 0; k < 3; k++) c[k] = Math.trunc(m.color[k]) & 255;
		}
		into.push(m.poly);
	}
}

// CG_PlayerShadow with cg_shadows at 1: a box is moved down from where he is, and where it comes to rest a round
// shadow is laid onto the map. None when the ground is further than 128 units, for one who is dead or cloaked.
// yaw: how the shadow is turned. radius: 24, 8 for the small droids.
function markShadow(origin, yaw, eFlags, powerups, radius) {
	if ((powerups & 1 << MARK_PW_CLOAKED) || (eFlags & MARK_EF_DEAD)) return false;
	const tr = traceBox(origin, [origin[0], origin[1], origin[2] - MARK_SHADOW_DISTANCE], MARK_SHADOW_MINS, MARK_SHADOW_MAXS, TRACE_MASK_PLAYERSOLID);
	if (tr.fraction === 1 || tr.startsolid || tr.allsolid) return false;
	// it fades with the height: the game hands that on as the colour, the shader of the shadow takes its alpha
	const alpha = 1 - tr.fraction;
	impactMark(MARK_SHADOW, tr.pos, tr.normal, yaw, alpha, alpha, alpha, 1, false, radius, true);
	return true;
}

// The shadows of this frame: every player and every NPC the game draws (CG_Player, which is left before the
// shadow for one who is not drawn or died in his ship). From inside the map only of those a client there is sent
// (snapshotSees in js/trace.js, with the box of one who stands). Turned by the way he looks: the game turns it by
// the way his legs stand, and the image of the shadow is round.
function marksShadows() {
	let n = 0;
	const from = camInMap() ? snapshotFrom(eye) : null;
	const drawn = (at, eFlags, eFlags2) => !(eFlags & EF_NODRAW) && !(eFlags2 & MARK_EF2_SHIP_DEATH) && snapshotSees(from, [at[0] - 16, at[1] - 16, at[2] - 25], [at[0] + 16, at[1] + 16, at[2] + 41]);
	for (const p of players.values()) {
		const d = p.data;
		if (d.team === 'spectator' || !drawn(p.draw, d.eFlags | 0, d.eFlags2 | 0)) continue;
		if (markShadow(p.draw, d.yaw || 0, (d.eFlags | 0) | (d.dead ? MARK_EF_DEAD : 0), d.powerups | 0, 24)) n++;
	}
	for (const npc of (state && state.npcs) || []) {
		const f = npc.f || [];
		if (!drawn(npc.pos, f[4] | 0, f[5] | 0)) continue;
		if (markShadow(npc.pos, npc.yaw || 0, (f[4] | 0) | (npc.dead ? MARK_EF_DEAD : 0), f[6] | 0, npc.class === MARK_CLASS_REMOTE || npc.class === MARK_CLASS_SEEKER ? 8 : 24)) n++;
	}
	return n;
}

const markData = new ArrayBuffer(MARK_MAX_VERTS * MARK_STRIDE), markFloats = new Float32Array(markData), markBytes = new Uint8Array(markData);
const markIndex = new Uint16Array(MARK_MAX_VERTS * 3);
const markVao = gl.createVertexArray(), markVbo = gl.createBuffer(), markIbo = gl.createBuffer();
gl.bindVertexArray(markVao);
gl.bindBuffer(gl.ARRAY_BUFFER, markVbo);
gl.bufferData(gl.ARRAY_BUFFER, markData.byteLength, gl.DYNAMIC_DRAW);
gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, MARK_STRIDE, 0);
gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 2, gl.FLOAT, false, MARK_STRIDE, 12);
gl.enableVertexAttribArray(3); gl.vertexAttribPointer(3, 4, gl.UNSIGNED_BYTE, true, MARK_STRIDE, 20);
gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, markIbo);
gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, markIndex.byteLength, gl.DYNAMIC_DRAW);
gl.bindVertexArray(null);

// The polygons of a frame into the buffers, shader by shader in the order of the sort of the shaders: a polygon is
// a fan of triangles around its first corner (RB_SurfacePolychain). Each batch is { mat, fog, first, count }.
function marksBuild(polys) {
	const by = new Map(), fogged = fogShown() && world.fogInfo.length > 0;
	for (const p of polys) {
		// the fog of a polygon (RE_AddPolyToScene): the first one of the map its box reaches into
		let fog = null;
		if (fogged) {
			let x = 0, y = 0, z = 0, far = 0;
			for (let j = 0; j < p.n; j++) { x += p.pos[j * 3]; y += p.pos[j * 3 + 1]; z += p.pos[j * 3 + 2]; }
			x /= p.n; y /= p.n; z /= p.n;
			for (let j = 0; j < p.n; j++) far = Math.max(far, Math.abs(p.pos[j * 3] - x), Math.abs(p.pos[j * 3 + 1] - y), Math.abs(p.pos[j * 3 + 2] - z));
			fog = fogAt([x, y, z], far);
		}
		const key = p.shader + '\n' + (fog ? world.fogInfo.indexOf(fog) : -1);
		let b = by.get(key);
		if (!b) by.set(key, b = { mat: materialFor(p.shader, 'fx'), fog, list: [], first: 0, count: 0 });
		b.list.push(p);
	}
	const order = [...by.values()].filter(b => !b.mat.nodraw && b.mat.stages.length).sort((a, b) => a.mat.sort - b.mat.sort || a.mat.order - b.mat.order);
	let verts = 0, ni = 0;
	for (const b of order) {
		b.first = ni;
		for (const p of b.list) {
			if (verts + p.n > MARK_MAX_VERTS) break;
			for (let j = 0; j < p.n; j++) {
				const o = (verts + j) * 6, c = (verts + j) * MARK_STRIDE + 20;
				markFloats[o] = p.pos[j * 3]; markFloats[o + 1] = p.pos[j * 3 + 1]; markFloats[o + 2] = p.pos[j * 3 + 2];
				markFloats[o + 3] = p.st[j * 2]; markFloats[o + 4] = p.st[j * 2 + 1];
				markBytes[c] = p.rgba[0]; markBytes[c + 1] = p.rgba[1]; markBytes[c + 2] = p.rgba[2]; markBytes[c + 3] = p.rgba[3];
			}
			for (let j = 0; j < p.n - 2; j++) { markIndex[ni++] = verts; markIndex[ni++] = verts + j + 1; markIndex[ni++] = verts + j + 2; }
			verts += p.n;
		}
		b.count = ni - b.first;
	}
	return { batches: order.filter(b => b.count), verts, indexes: ni };
}

// no mark is left: the marks of the client game, the decals of the renderer, what was to be drawn in this frame
let markWorld = null;
function marksClear() {
	marksAlive.length = 0;
	marksNow = [];
	for (const ring of markDecals) ring.fill(null);
	markDecalHead[0] = markDecalHead[1] = 0;
}

// after the solid surfaces of the map: the marks, the shadows of this frame among them
function drawMarks(now) {
	if (!state || !world || !world.scripted) return;
	// another map: nothing of the last one stays (CG_InitMarkPolys, RE_ClearDecals)
	if (markWorld !== world) { markWorld = world; marksClear(); }
	markTime = itemTime(now);
	markStats.shadows = marksShadows();
	const polys = marksNow;
	marksNow = [];
	marksAdd(markTime, polys);
	markDecalsAdd(polys);
	markStats.polys = polys.length;
	markStats.batches = 0;
	if (!polys.length) return;
	const built = marksBuild(polys), t = frameNow / 1000;
	markStats.batches = built.batches.length;
	if (!built.batches.length) return;

	const program = gl.getParameter(gl.CURRENT_PROGRAM);
	gl.useProgram(stageProg.p);
	gl.uniform1i(stageProg.u.uTex, 0);
	gl.uniform3fv(stageProg.u.uEye, eye);
	gl.uniformMatrix4fv(stageProg.u.uMvp, false, viewProj);
	gl.uniform1i(stageProg.u.uSprite, 0);
	gl.activeTexture(gl.TEXTURE0);
	gl.colorMask(true, true, true, false);
	gl.bindVertexArray(markVao);
	gl.bindBuffer(gl.ARRAY_BUFFER, markVbo);
	gl.bufferSubData(gl.ARRAY_BUFFER, 0, markBytes, 0, built.verts * MARK_STRIDE);
	gl.bufferSubData(gl.ELEMENT_ARRAY_BUFFER, 0, markIndex, 0, built.indexes);
	// a polygon is drawn as a part of the map: no colour of an entity, no place of one
	stWorld = true;
	for (const b of built.batches) {
		setCull(b.mat.cull);
		setDeform(stageProg, b.mat, t);
		if (b.mat.polygonOffset) { gl.enable(gl.POLYGON_OFFSET_FILL); gl.polygonOffset(-1, -2); }
		for (const st of b.mat.stages) drawStage(st, t, null, markVao, b.count, gl.UNSIGNED_SHORT, b.first * 2, stageProg, b.fog);
		// the fog of a brush, drawn onto it as onto the map (RB_FogPass)
		if (b.fog && !b.fog.global && b.mat.fogPass) drawStage(b.fog.pass[b.mat.fogPass - 1], t, null, markVao, b.count, gl.UNSIGNED_SHORT, b.first * 2, stageProg, b.fog);
		if (b.mat.polygonOffset) gl.disable(gl.POLYGON_OFFSET_FILL);
	}
	stWorld = false;
	setDeform(stageProg, NO_DEFORM, 0);
	gl.disable(gl.CULL_FACE);
	gl.disable(gl.BLEND);
	gl.depthMask(true);
	gl.depthFunc(gl.LESS);
	gl.colorMask(true, true, true, true);
	gl.useProgram(program);
}
