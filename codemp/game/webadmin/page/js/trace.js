// ---- a line through the map: where it hits first (CM_BoxTrace of the engine, qcommon/cm_trace.cpp)
//
// For what the client game traces: the shadow under a player, a mark on a wall, the camera behind a player.
// The brushes of the map file are what the game collides with, and the tree of the file with the brushes of each
// of its leafs says which of them are near a line (world.tree, bspTree in js/world-ents.js).
// A curved surface is collided with as the triangles the page draws of it. The engine makes a cut of its own of
// the curve for that, coarser, and collides with the facets of that cut (CM_TraceThroughPatchCollide): here every
// triangle is such a facet, without the bevels the engine adds for a box. Which curves are in a leaf the map
// file says too, and the page does not keep that list: a curve is in every leaf its box reaches into, which are
// those and maybe a few more, and a curve that is tried without need changes nothing. Curves of a shader that is
// not drawn are not in the page at all.
// Only the map itself: not the brush models that are entities (doors, lifts), which the game clips against apart.

const TRACE_CLIP_EPSILON = 0.125;	// SURFACE_CLIP_EPSILON
// what stops what (bg_public.h): MASK_SOLID, and MASK_PLAYERSOLID which adds the clip brushes for players
const TRACE_MASK_SOLID = 0x1 | 0x1000, TRACE_MASK_PLAYERSOLID = 0x1 | 0x10 | 0x100 | 0x1000;

// BoxOnPlaneSide: 1 the box is in front of plane p of P, 2 behind it, 3 on both sides
function traceBoxSide(P, p, mins, maxs) {
	const o = p * 4, nx = P[o], ny = P[o + 1], nz = P[o + 2], d = P[o + 3];
	const axis = nx === 1 ? 0 : ny === 1 ? 1 : nz === 1 ? 2 : 3;
	if (axis < 3) return d <= mins[axis] ? 1 : d >= maxs[axis] ? 2 : 3;
	const far = nx * (nx < 0 ? mins[0] : maxs[0]) + ny * (ny < 0 ? mins[1] : maxs[1]) + nz * (nz < 0 ? mins[2] : maxs[2]);
	const near = nx * (nx < 0 ? maxs[0] : mins[0]) + ny * (ny < 0 ? maxs[1] : mins[1]) + nz * (nz < 0 ? maxs[2] : mins[2]);
	return (far >= d ? 1 : 0) | (near < d ? 2 : 0);
}

// CM_BoxLeafnums: calls found with every leaf of the tree a box reaches into, in the order of the tree
function traceBoxLeafs(tree, mins, maxs, found, node = 0) {
	const N = tree.nodes;
	if (!N.length) return;
	while (node >= 0) {
		const s = traceBoxSide(tree.planes, N[node * 9], mins, maxs);
		if (s === 1) node = N[node * 9 + 1];
		else if (s === 2) node = N[node * 9 + 2];
		else { traceBoxLeafs(tree, mins, maxs, found, N[node * 9 + 1]); node = N[node * 9 + 2]; }
	}
	found(-1 - node);
}

// lists by leaf as two arrays: where the list of a leaf begins, and the lists one after the other
function traceLeafLists(perLeaf) {
	const first = new Int32Array(perLeaf.length + 1);
	let n = 0;
	perLeaf.forEach((l, i) => { first[i] = n; n += l ? l.length : 0; });
	first[perLeaf.length] = n;
	const list = new Int32Array(n);
	perLeaf.forEach((l, i) => { if (l) list.set(l, first[i]); });
	return { first, list };
}

// What a trace needs of the map that is shown, made once: the planes with what the engine keeps of them, the
// brushes with their boxes and contents, and the curved surfaces of the map by leaf. Also which triangles of
// world.pick are which surface (surfFirst, surfTris), for the marks.
let traceMapNow = null;
function traceMap() {
	if (!world || !world.tree || !world.brushes || !world.tree.nodes.length || !world.tree.leafBrushes) return null;
	if (traceMapNow && traceMapNow.world === world) return traceMapNow;
	const tree = world.tree, P = world.brushes.planes, B = world.brushes.brushes, S = world.brushes.sides, np = P.length >> 2, leafs = tree.leafs.length / 12 | 0;
	// cplane_t: the corner of a box that is nearest to the plane (signbits)
	const signbits = new Uint8Array(np);
	for (let i = 0; i < np; i++) signbits[i] = (P[i * 4] < 0 ? 1 : 0) | (P[i * 4 + 1] < 0 ? 2 : 0) | (P[i * 4 + 2] < 0 ? 4 : 0);

	// CMod_LoadBrushes and CM_BoundBrush: the box of a brush is its first six sides
	const nb = B.length / 3 | 0, bounds = new Float32Array(nb * 6), contents = new Int32Array(nb);
	for (let b = 0; b < nb; b++) {
		const side = B[b * 3], sides = B[b * 3 + 1];
		if (sides < 6 || (side + sides) * 3 > S.length) continue;
		for (let i = 0; i < 3; i++) {
			bounds[b * 6 + i] = -P[S[(side + i * 2) * 3] * 4 + 3];
			bounds[b * 6 + 3 + i] = P[S[(side + i * 2 + 1) * 3] * 4 + 3];
		}
		contents[b] = (world.shaders[B[b * 3 + 2]] || {}).contents | 0;
	}

	// the triangles of every surface, and the curved ones of the map as patches (CMod_LoadPatches)
	const pick = world.pick, surfFirst = new Int32Array(world.surfs.length).fill(-1), surfTris = new Int32Array(world.surfs.length);
	for (let t = 0; t < pick.surf.length; t++) { const s = pick.surf[t]; if (surfFirst[s] < 0) surfFirst[s] = t; surfTris[s]++; }
	const patches = [], patchLeaf = new Array(leafs).fill(null);
	world.surfs.forEach((s, at) => {
		if (!s || s.type !== MST_PATCH || s.model !== 0 || surfFirst[at] < 0) return;
		const sh = world.shaders[s.shader] || {}, box = [Infinity, Infinity, Infinity, -Infinity, -Infinity, -Infinity];
		for (let i = surfFirst[at] * 3, e = i + surfTris[at] * 3; i < e; i++) {
			const v = pick.idx[i] * 3;
			for (let k = 0; k < 3; k++) { box[k] = Math.min(box[k], pick.pos[v + k]); box[3 + k] = Math.max(box[3 + k], pick.pos[v + k]); }
		}
		const n = patches.length;
		patches.push({ first: surfFirst[at], tris: surfTris[at], contents: sh.contents | 0, surfaceFlags: sh.flags | 0, box, planes: null });
		traceBoxLeafs(tree, box, box.slice(3), leaf => { if (leaf < leafs) (patchLeaf[leaf] || (patchLeaf[leaf] = [])).push(n); });
	});

	traceMapNow = { world, tree, P, B, S, signbits, bounds, contents, patches, leafPatches: traceLeafLists(patchLeaf),
		surfFirst, surfTris, checked: new Int32Array(nb), patchChecked: new Int32Array(patches.length), check: 0 };
	return traceMapNow;
}

// traceWork_t of the engine, one for all traces, and the trace_t that is filled
const traceWork = { start: [0, 0, 0], end: [0, 0, 0], size: [[0, 0, 0], [0, 0, 0]], offsets: Array.from({ length: 8 }, () => [0, 0, 0]), extents: [0, 0, 0],
	bounds: [[0, 0, 0], [0, 0, 0]], contents: 0, isPoint: false, enterFrac: 0, leaveFrac: 0, clipplane: -1, leadside: -1, getout: false, startout: false };
let traceNow = null;

// CM_PlaneCollision: one side of a brush against the trace. False: the trace is in front of that side all the way.
function tracePlaneCollision(m, side) {
	const tw = traceWork, P = m.P, plane = m.S[side * 3], o = plane * 4, nx = P[o], ny = P[o + 1], nz = P[o + 2], off = tw.offsets[m.signbits[plane]];
	// the plane moved out by the corner of the box that is nearest to it
	const dist = P[o + 3] - (off[0] * nx + off[1] * ny + off[2] * nz);
	const d1 = tw.start[0] * nx + tw.start[1] * ny + tw.start[2] * nz - dist, d2 = tw.end[0] * nx + tw.end[1] * ny + tw.end[2] * nz - dist;
	if (d2 > 0) tw.getout = true;
	if (d1 > 0) tw.startout = true;
	if (d1 > 0 && (d2 >= TRACE_CLIP_EPSILON || d2 >= d1)) return false;
	if (d1 <= 0 && d2 <= 0) return true;
	if (d1 > d2) {
		// it comes in through this side
		let f = d1 - TRACE_CLIP_EPSILON;
		if (f < 0) {
			f = 0;
			if (f > tw.enterFrac) { tw.enterFrac = f; tw.clipplane = plane; tw.leadside = side; }
		} else if (f > tw.enterFrac * (d1 - d2)) {
			tw.enterFrac = f / (d1 - d2); tw.clipplane = plane; tw.leadside = side;
		}
	} else {
		// it goes out through it
		let f = d1 + TRACE_CLIP_EPSILON;
		if (f < d1 - d2) {
			f = 1;
			if (f < tw.leaveFrac) tw.leaveFrac = f;
		} else if (f > tw.leaveFrac * (d1 - d2)) {
			tw.leaveFrac = f / (d1 - d2);
		}
	}
	return true;
}

// CM_TraceThroughBrush
function traceThroughBrush(m, b) {
	const tw = traceWork, tr = traceNow, side = m.B[b * 3], sides = m.B[b * 3 + 1], box = m.bounds, o = b * 6;
	tw.enterFrac = -1; tw.leaveFrac = 1; tw.clipplane = -1;
	if (!sides) return;
	if (tw.bounds[0][0] > box[o + 3] || tw.bounds[0][1] > box[o + 4] || tw.bounds[0][2] > box[o + 5]
		|| tw.bounds[1][0] < box[o] || tw.bounds[1][1] < box[o + 1] || tw.bounds[1][2] < box[o + 2]) return;
	tw.getout = false; tw.startout = false; tw.leadside = -1;
	// the latest time the trace comes in through a side and the earliest time it goes out through one
	for (let i = 0; i < sides; i++) if (!tracePlaneCollision(m, side + i)) return;
	if (!tw.startout) {
		// it began inside the brush
		tr.startsolid = true;
		if (!tw.getout) { tr.allsolid = true; tr.fraction = 0; }
		tw.enterFrac = 0;
		return;
	}
	if (tw.enterFrac < tw.leaveFrac && tw.enterFrac > -1 && tw.enterFrac < tr.fraction) {
		if (tw.enterFrac < 0) tw.enterFrac = 0;
		const p = tw.clipplane * 4;
		tr.fraction = tw.enterFrac;
		tr.normal[0] = m.P[p]; tr.normal[1] = m.P[p + 1]; tr.normal[2] = m.P[p + 2]; tr.dist = m.P[p + 3];
		tr.surfaceFlags = (m.world.shaders[m.S[tw.leadside * 3 + 1]] || {}).flags | 0;
		tr.contents = m.contents[b];
	}
}

// the planes of the triangles of a patch, made when a trace first comes near it: the side that is seen is the front
function tracePatchPlanes(m, patch) {
	const pos = m.world.pick.pos, idx = m.world.pick.idx, out = new Float32Array(patch.tris * 4);
	for (let t = 0; t < patch.tris; t++) {
		const i = (patch.first + t) * 3, a = idx[i] * 3, b = idx[i + 1] * 3, c = idx[i + 2] * 3;
		const ux = pos[a] - pos[b], uy = pos[a + 1] - pos[b + 1], uz = pos[a + 2] - pos[b + 2], vx = pos[c] - pos[b], vy = pos[c + 1] - pos[b + 1], vz = pos[c + 2] - pos[b + 2];
		let nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
		const l = Math.hypot(nx, ny, nz);
		// one without an area is none
		if (l < 1e-6) { out[t * 4] = NaN; continue; }
		nx /= l; ny /= l; nz /= l;
		out[t * 4] = nx; out[t * 4 + 1] = ny; out[t * 4 + 2] = nz; out[t * 4 + 3] = nx * pos[a] + ny * pos[a + 1] + nz * pos[a + 2];
	}
	return out;
}

// CM_TracePointThroughPatchCollide: a line against the facets of a patch, which have no volume. A facet is hit
// when the line comes to its plane from the front, and is inside the planes through its edges where it does.
function tracePointThroughPatch(m, patch) {
	const tw = traceWork, tr = traceNow, box = patch.box, S = tw.start, E = tw.end;
	if (tw.bounds[0][0] > box[3] || tw.bounds[0][1] > box[4] || tw.bounds[0][2] > box[5] || tw.bounds[1][0] < box[0] || tw.bounds[1][1] < box[1] || tw.bounds[1][2] < box[2]) return;
	const planes = patch.planes || (patch.planes = tracePatchPlanes(m, patch)), pos = m.world.pick.pos, idx = m.world.pick.idx;
	for (let t = 0; t < patch.tris; t++) {
		const nx = planes[t * 4], ny = planes[t * 4 + 1], nz = planes[t * 4 + 2], dist = planes[t * 4 + 3];
		if (nx !== nx) continue;
		const d1 = S[0] * nx + S[1] * ny + S[2] * nz - dist, d2 = E[0] * nx + E[1] * ny + E[2] * nz - dist;
		// from behind, along it, away from it, or further than what was hit already
		if (d1 <= 0 || d1 === d2) continue;
		const at = d1 / (d1 - d2);
		if (at <= 0 || at > tr.fraction) continue;
		let j = 0;
		for (; j < 3; j++) {
			const i = (patch.first + t) * 3, p = idx[i + j] * 3, q = idx[i + (j + 1) % 3] * 3, r = idx[i + (j + 2) % 3] * 3;
			const ex = pos[q] - pos[p], ey = pos[q + 1] - pos[p + 1], ez = pos[q + 2] - pos[p + 2];
			// the plane through this edge that stands on the triangle, facing away from the third corner
			let ox = ey * nz - ez * ny, oy = ez * nx - ex * nz, oz = ex * ny - ey * nx;
			if (ox * (pos[r] - pos[p]) + oy * (pos[r + 1] - pos[p + 1]) + oz * (pos[r + 2] - pos[p + 2]) > 0) { ox = -ox; oy = -oy; oz = -oz; }
			const od = ox * pos[p] + oy * pos[p + 1] + oz * pos[p + 2], e1 = S[0] * ox + S[1] * oy + S[2] * oz - od, e2 = E[0] * ox + E[1] * oy + E[2] * oz - od;
			let cross = e1 === e2 ? 99999 : e1 / (e1 - e2);
			if (cross <= 0) cross = 99999;
			// it begins outside of this edge and is still there at the plane, or inside and is out by then
			if (e1 > 0 ? cross > at : cross < at) break;
		}
		if (j < 3) continue;
		tr.fraction = Math.max(0, (d1 - TRACE_CLIP_EPSILON) / (d1 - d2));
		tr.normal[0] = nx; tr.normal[1] = ny; tr.normal[2] = nz; tr.dist = dist;
		tr.surfaceFlags = patch.surfaceFlags;
		tr.contents = patch.contents;
	}
}

// CM_CheckFacetPlane: a plane of a facet against the trace, the plane as nx ny nz dist. 0: the trace is in front
// of it all the way, 1: it counts, 2: and the trace comes in through it later than through the ones before.
function traceFacetPlane(nx, ny, nz, dist) {
	const tw = traceWork, d1 = tw.start[0] * nx + tw.start[1] * ny + tw.start[2] * nz - dist, d2 = tw.end[0] * nx + tw.end[1] * ny + tw.end[2] * nz - dist;
	if (d1 > 0 && (d2 >= TRACE_CLIP_EPSILON || d2 >= d1)) return 0;
	if (d1 <= 0 && d2 <= 0) return 1;
	if (d1 > d2) {
		let f = (d1 - TRACE_CLIP_EPSILON) / (d1 - d2);
		if (f < 0) f = 0;
		if (f > tw.enterFrac) { tw.enterFrac = f; return 2; }
	} else {
		let f = (d1 + TRACE_CLIP_EPSILON) / (d1 - d2);
		if (f > 1) f = 1;
		if (f < tw.leaveFrac) tw.leaveFrac = f;
	}
	return 1;
}

// CM_TraceThroughPatch with CM_TraceThroughPatchCollide, for a box: every triangle is a facet, which is its plane,
// the three planes through its edges that stand on it, and its plane again from behind. Each is moved out by the
// box, and a facet is only hit from the front.
function traceThroughPatch(m, patch) {
	const tw = traceWork, tr = traceNow, box = patch.box;
	if (tw.bounds[0][0] > box[3] || tw.bounds[0][1] > box[4] || tw.bounds[0][2] > box[5] || tw.bounds[1][0] < box[0] || tw.bounds[1][1] < box[1] || tw.bounds[1][2] < box[2]) return;
	const planes = patch.planes || (patch.planes = tracePatchPlanes(m, patch)), pos = m.world.pick.pos, idx = m.world.pick.idx, O = tw.offsets;
	const corner = (x, y, z) => O[(x < 0 ? 1 : 0) | (y < 0 ? 2 : 0) | (z < 0 ? 4 : 0)];
	for (let t = 0; t < patch.tris; t++) {
		const nx = planes[t * 4], ny = planes[t * 4 + 1], nz = planes[t * 4 + 2];
		if (nx !== nx) continue;
		tw.enterFrac = -1; tw.leaveFrac = 1;
		// its plane, moved out by the corner of the box that is nearest to it
		let off = corner(nx, ny, nz), offset = off[0] * nx + off[1] * ny + off[2] * nz, bx = nx, by = ny, bz = nz, bd = planes[t * 4 + 3] - offset;
		let hit = traceFacetPlane(nx, ny, nz, bd), j = 0;
		if (!hit) continue;
		for (; j < 3; j++) {
			const i = (patch.first + t) * 3, p = idx[i + j] * 3, q = idx[i + (j + 1) % 3] * 3, r = idx[i + (j + 2) % 3] * 3;
			const ex = pos[q] - pos[p], ey = pos[q + 1] - pos[p + 1], ez = pos[q + 2] - pos[p + 2];
			// the plane through this edge, facing away from the third corner
			let ox = ey * nz - ez * ny, oy = ez * nx - ex * nz, oz = ex * ny - ey * nx;
			const l = Math.hypot(ox, oy, oz);
			if (l < 1e-6) continue;
			const s = (ox * (pos[r] - pos[p]) + oy * (pos[r + 1] - pos[p + 1]) + oz * (pos[r + 2] - pos[p + 2]) > 0 ? -1 : 1) / l;
			ox *= s; oy *= s; oz *= s;
			off = corner(ox, oy, oz);
			const od = ox * pos[p] + oy * pos[p + 1] + oz * pos[p + 2] + Math.abs(off[0] * ox + off[1] * oy + off[2] * oz);
			hit = traceFacetPlane(ox, oy, oz, od);
			if (!hit) break;
			if (hit === 2) { bx = ox; by = oy; bz = oz; bd = od; }
		}
		if (j < 3) continue;
		// never from behind: the plane of the triangle the other way round is its last side
		off = corner(-nx, -ny, -nz);
		hit = traceFacetPlane(-nx, -ny, -nz, -planes[t * 4 + 3] + Math.abs(off[0] * nx + off[1] * ny + off[2] * nz));
		if (!hit || hit === 2) continue;
		if (tw.enterFrac < tw.leaveFrac && tw.enterFrac >= 0 && tw.enterFrac < tr.fraction) {
			tr.fraction = tw.enterFrac;
			tr.normal[0] = bx; tr.normal[1] = by; tr.normal[2] = bz; tr.dist = bd;
			tr.surfaceFlags = patch.surfaceFlags;
			tr.contents = patch.contents;
		}
	}
}

// CM_TraceThroughLeaf: the brushes of a leaf, then its patches, each once in a trace
function traceThroughLeaf(m, leaf) {
	const tw = traceWork, tr = traceNow, L = m.tree.leafs, lb = m.tree.leafBrushes, lp = m.leafPatches;
	for (let k = L[leaf * 12 + 10], end = Math.min(lb.length, k + L[leaf * 12 + 11]); k < end; k++) {
		const b = lb[k];
		if (!(b >= 0 && b < m.checked.length) || m.checked[b] === m.check) continue;
		m.checked[b] = m.check;
		if (!(m.contents[b] & tw.contents)) continue;
		traceThroughBrush(m, b);
		if (!tr.fraction) return;
	}
	for (let k = lp.first[leaf]; k < lp.first[leaf + 1]; k++) {
		const n = lp.list[k], patch = m.patches[n];
		if (m.patchChecked[n] === m.check) continue;
		m.patchChecked[n] = m.check;
		if (!(patch.contents & tw.contents)) continue;
		if (tw.isPoint) tracePointThroughPatch(m, patch); else traceThroughPatch(m, patch);
		if (!tr.fraction) return;
	}
}

// CM_TraceThroughTree: every leaf the trace touches from p1 to p2, the nearer side of a node first
function traceThroughTree(m, num, p1f, p2f, p1x, p1y, p1z, p2x, p2y, p2z) {
	const tw = traceWork;
	if (traceNow.fraction <= p1f) return;
	if (num < 0) { traceThroughLeaf(m, -1 - num); return; }
	const N = m.tree.nodes, o = N[num * 9] * 4, P = m.P, nx = P[o], ny = P[o + 1], nz = P[o + 2], dist = P[o + 3];
	const axis = nx === 1 ? 0 : ny === 1 ? 1 : nz === 1 ? 2 : 3;
	// how far the two ends are from the plane of the node, and how far the box reaches
	let t1, t2, offset;
	if (axis < 3) {
		t1 = (axis === 0 ? p1x : axis === 1 ? p1y : p1z) - dist;
		t2 = (axis === 0 ? p2x : axis === 1 ? p2y : p2z) - dist;
		offset = tw.extents[axis];
	} else {
		t1 = nx * p1x + ny * p1y + nz * p1z - dist;
		t2 = nx * p2x + ny * p2y + nz * p2z - dist;
		offset = tw.isPoint ? 0 : 2048;
	}
	if (t1 >= offset + 1 && t2 >= offset + 1) { traceThroughTree(m, N[num * 9 + 1], p1f, p2f, p1x, p1y, p1z, p2x, p2y, p2z); return; }
	if (t1 < -offset - 1 && t2 < -offset - 1) { traceThroughTree(m, N[num * 9 + 2], p1f, p2f, p1x, p1y, p1z, p2x, p2y, p2z); return; }
	// the point where it crosses, a little to the near side
	let side, frac, frac2;
	if (t1 < t2) {
		const idist = 1 / (t1 - t2);
		side = 1;
		frac2 = (t1 + offset + TRACE_CLIP_EPSILON) * idist;
		frac = (t1 - offset + TRACE_CLIP_EPSILON) * idist;
	} else if (t1 > t2) {
		const idist = 1 / (t1 - t2);
		side = 0;
		frac2 = (t1 - offset - TRACE_CLIP_EPSILON) * idist;
		frac = (t1 + offset + TRACE_CLIP_EPSILON) * idist;
	} else {
		side = 0; frac = 1; frac2 = 0;
	}
	frac = Math.min(1, Math.max(0, frac));
	traceThroughTree(m, N[num * 9 + 1 + side], p1f, p1f + (p2f - p1f) * frac, p1x, p1y, p1z, p1x + frac * (p2x - p1x), p1y + frac * (p2y - p1y), p1z + frac * (p2z - p1z));
	frac2 = Math.min(1, Math.max(0, frac2));
	traceThroughTree(m, N[num * 9 + 2 - side], p1f + (p2f - p1f) * frac2, p2f, p1x + frac2 * (p2x - p1x), p1y + frac2 * (p2y - p1y), p1z + frac2 * (p2z - p1z), p2x, p2y, p2z);
}

// CM_Trace against the map: a box (mins, maxs, null for a line) is moved from start to end and stops at what has
// one of the contents of mask. Always an answer, as the engine gives it: fraction (1 for all the way), pos
// (endpos), normal and dist of the plane it stopped at (nothing when it began in something solid), startsolid,
// allsolid, and the surfaceFlags and contents of what was hit.
function traceBox(start, end, mins, maxs, mask = TRACE_MASK_SOLID) {
	const tr = { fraction: 1, pos: [end[0], end[1], end[2]], normal: [0, 0, 0], dist: 0, startsolid: false, allsolid: false, surfaceFlags: 0, contents: 0 };
	const m = traceMap(), tw = traceWork;
	if (!m) return tr;
	m.check++;
	traceNow = tr;
	tw.contents = mask;
	// the box around its middle, so that it is the same to both sides
	for (let i = 0; i < 3; i++) {
		const lo = mins ? mins[i] : 0, hi = maxs ? maxs[i] : 0, offset = (lo + hi) * 0.5;
		tw.size[0][i] = lo - offset; tw.size[1][i] = hi - offset;
		tw.start[i] = start[i] + offset; tw.end[i] = end[i] + offset;
	}
	// the corner of the box for every way a plane can face (its signbits)
	for (let c = 0; c < 8; c++) for (let i = 0; i < 3; i++) tw.offsets[c][i] = tw.size[c >> i & 1][i];
	for (let i = 0; i < 3; i++) {
		tw.bounds[0][i] = Math.min(tw.start[i], tw.end[i]) + tw.size[0][i];
		tw.bounds[1][i] = Math.max(tw.start[i], tw.end[i]) + tw.size[1][i];
	}
	const point = tw.size[0][0] === 0 && tw.size[0][1] === 0 && tw.size[0][2] === 0;
	// a point that does not move is tested as a box of no size
	tw.isPoint = point && !(start[0] === end[0] && start[1] === end[1] && start[2] === end[2]);
	for (let i = 0; i < 3; i++) tw.extents[i] = point ? 0 : tw.size[1][i];
	traceThroughTree(m, 0, 0, 1, tw.start[0], tw.start[1], tw.start[2], tw.end[0], tw.end[1], tw.end[2]);
	if (tr.fraction !== 1) for (let i = 0; i < 3; i++) tr.pos[i] = start[i] + tr.fraction * (end[i] - start[i]);
	return tr;
}

// From start to end. null when nothing is in the way, else { fraction, pos, normal }: how far along the line
// (0 to 1), where, and the normal of what was hit (and the rest of traceBox). mask: the contents that stop it.
function traceWorld(start, end, mask = TRACE_MASK_SOLID) {
	const tr = traceBox(start, end, null, null, mask);
	return tr.fraction < 1 ? tr : null;
}

// ---- which entities a client is sent (SV_AddEntitiesVisibleFromPoint in server/sv_snapshot.cpp)
//
// A client only draws the entities of its snapshots, and the server leaves out what can not be seen from where
// its eye is: what is in no cluster of the map that the cluster of the eye sees, and all of them for an eye that
// is in no area of the map (inside a wall). Not known to the page: which doors are shut. The server also leaves
// out what is in an area behind a shut door, here every area counts as open to every other.

// Where a client looks from, as the server takes it: the area of the leaf its eye is in, and the row of the
// cluster of that leaf in the visibility of the map (-1: all of it is seen, CM_ClusterPVS). null without a map.
function snapshotFrom(at) {
	const t = world && world.tree;
	if (!t || !t.nodes.length) return null;
	const N = t.nodes, P = t.planes;
	let n = 0;
	// CM_PointLeafnum: a point on a plane is in front of it
	while (n >= 0) { const p = N[n * 9] * 4; n = N[n * 9 + (P[p] * at[0] + P[p + 1] * at[1] + P[p + 2] * at[2] - P[p + 3] < 0 ? 2 : 1)]; }
	const cluster = t.leafs[(-1 - n) * 12];
	return { area: t.leafs[(-1 - n) * 12 + 1], row: cluster >= 0 && cluster < t.clusters && t.rowBytes ? cluster * t.rowBytes : -1 };
}

// Whether a client there (snapshotFrom) is sent an entity with the box mins to maxs, which is the box the server
// links it with (SV_LinkEntity: its origin with its mins and maxs, one unit more to every side). The leafs that
// box reaches into say its areas and its clusters. Always, without a map.
function snapshotSees(from, mins, maxs) {
	if (!from) return true;
	if (from.area < 0) return false;
	const t = world.tree, L = t.leafs;
	let area = false, seen = false;
	traceBoxLeafs(t, mins, maxs, leaf => {
		if (L[leaf * 12 + 1] >= 0) area = true;
		const c = L[leaf * 12];
		if (c >= 0 && (from.row < 0 || t.vis[from.row + (c >> 3)] & (1 << (c & 7)))) seen = true;
	});
	return area && seen;
}

// Whether a client whose eye is the camera of the page would be sent an entity with this box, asked for every
// entity a frame draws: a client draws nothing of what the server does not send it. From outside of the map (the
// camera around it) everything is drawn. at: its origin, with what its box reaches to every side, below and above.
const snapEye = { world: null, at: [NaN, NaN, NaN], from: null };
function clientSees(at, side = 1, below = 1, above = 1) {
	if (!world || !camInMap()) return true;
	// where the eye is in the map is asked once for an eye and a map, not for every entity
	const e = snapEye;
	if (e.world !== world || e.at[0] !== eye[0] || e.at[1] !== eye[1] || e.at[2] !== eye[2]) { e.world = world; e.at = eye.slice(); e.from = snapshotFrom(eye); }
	return snapshotSees(e.from, [at[0] - side, at[1] - side, at[2] - below], [at[0] + side, at[1] + side, at[2] + above]);
}
