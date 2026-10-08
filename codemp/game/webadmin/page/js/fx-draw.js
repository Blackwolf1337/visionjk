// ---- the effects drawn

// A primitive of an effect is a few quads with a shader of the game, and its colour is the colour of their
// vertices: that is how the engine draws it, and what rgbGen vertex and alphaGen vertex of a script take. So the
// quads of everything that lives are written into one buffer in every frame, shader by shader, and drawn with
// the stages of the script renderer (drawStage). What a quad is made of is the engine's: RB_SurfaceSprite,
// RB_SurfaceOrientedQuad, RB_SurfaceLine, RB_SurfaceCylinder and RB_SurfaceElectricity in rd-vanilla/tr_surface.cpp.

const FX_MAX_QUADS = 16384;		// four vertices each, and their numbers are 16 bits
const FX_STRIDE = 24;			// a vertex: its place and its texture coordinates as floats, its colour as four bytes

const fxStats = { live: 0, quads: 0, batches: 0, played: 0, dropped: 0 };

const fxData = new ArrayBuffer(FX_MAX_QUADS * 4 * FX_STRIDE), fxFloats = new Float32Array(fxData), fxBytes = new Uint8Array(fxData);
const fxColor = new Uint8Array(4);	// the colour of the vertices that are written next
let fxVerts = 0, fxBad = false;		// how many are written, and whether one of them was no number
let fxDrawn = 0;					// the time of the frame the effects were drawn in last

const fxVao = gl.createVertexArray(), fxVbo = gl.createBuffer();
gl.bindVertexArray(fxVao);
gl.bindBuffer(gl.ARRAY_BUFFER, fxVbo);
gl.bufferData(gl.ARRAY_BUFFER, fxData.byteLength, gl.DYNAMIC_DRAW);
gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, FX_STRIDE, 0);
gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 2, gl.FLOAT, false, FX_STRIDE, 12);
gl.enableVertexAttribArray(3); gl.vertexAttribPointer(3, 4, gl.UNSIGNED_BYTE, true, FX_STRIDE, 20);
// every quad is two triangles of its vertices 0 1 2 and 2 1 3, the front is where they run clockwise
gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, gl.createBuffer());
gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, Uint16Array.from({ length: FX_MAX_QUADS * 6 }, (_, i) => (i / 6 | 0) * 4 + [0, 1, 2, 2, 1, 3][i % 6]), gl.STATIC_DRAW);
gl.bindVertexArray(null);

function fxVertex(x, y, z, s, t) {
	const o = fxVerts * 6, c = fxVerts * FX_STRIDE + 20;
	if (!(x - x === 0 && y - y === 0 && z - z === 0)) fxBad = true;
	fxFloats[o] = x; fxFloats[o + 1] = y; fxFloats[o + 2] = z; fxFloats[o + 3] = s; fxFloats[o + 4] = t;
	fxBytes[c] = fxColor[0]; fxBytes[c + 1] = fxColor[1]; fxBytes[c + 2] = fxColor[2]; fxBytes[c + 3] = fxColor[3];
	fxVerts++;
}

// RB_AddQuadStamp: a quad around a place, left and up reach from its middle to its edges
function fxStamp(o, lx, ly, lz, ux, uy, uz) {
	fxVertex(o[0] + lx + ux, o[1] + ly + uy, o[2] + lz + uz, 0, 0);
	fxVertex(o[0] - lx + ux, o[1] - ly + uy, o[2] - lz + uz, 1, 0);
	fxVertex(o[0] + lx - ux, o[1] + ly - uy, o[2] + lz - uz, 0, 1);
	fxVertex(o[0] - lx - ux, o[1] - ly - uy, o[2] - lz - uz, 1, 1);
}

// RB_SurfaceSprite and RB_SurfaceOrientedQuad: the quad of a particle, turned by its rotation in the plane of
// left and up
function fxSprite(e, left, up, radius) {
	const a = Math.PI * e.rot / 180, s = Math.sin(a) * radius, c = Math.cos(a) * radius;
	fxStamp(e.org, left[0] * c - up[0] * s, left[1] * c - up[1] * s, left[2] * c - up[2] * s, up[0] * c + left[0] * s, up[1] * c + left[1] * s, up[2] * c + left[2] * s);
}

// DoLine2: a strip from a to b that is w1 wide to each side at a and w2 at b, across is the way it is wide
function fxStrip(a, b, across, w1, w2) {
	fxVertex(a[0] + across[0] * w1, a[1] + across[1] * w1, a[2] + across[2] * w1, 0, 0);
	fxVertex(a[0] - across[0] * w1, a[1] - across[1] * w1, a[2] - across[2] * w1, 1, 0);
	fxVertex(b[0] + across[0] * w2, b[1] + across[1] * w2, b[2] + across[2] * w2, 0, 1);
	fxVertex(b[0] - across[0] * w2, b[1] - across[1] * w2, b[2] - across[2] * w2, 1, 1);
}

// RB_SurfaceLine: a line is a strip that shows its flat side to the eye
const fxAcross = (a, b) => fxUnit(fxCross([a[0] - eye[0], a[1] - eye[1], a[2] - eye[2]], [b[0] - eye[0], b[1] - eye[1], b[2] - eye[2]]));

// RB_SurfaceCylinder: a ring of quads around the way it points, size2 wide where it begins and size where it ends.
// The nearer it is the more quads it has, 8 to 32.
function fxCylinder(e, radius) {
	const n = e.normal, length = e.traced || fxNow(e, 12, e.length), size2 = fxNow(e, 16, e.size2);
	const top = [e.org[0] + n[0] * length, e.org[1] + n[1] * length, e.org[2] + n[2] * length];
	const far = Math.hypot((e.org[0] + top[0]) / 2 - eye[0], (e.org[1] + top[1]) / 2 - eye[1], (e.org[2] + top[2]) / 2 - eye[2]);
	const segments = Math.min(32, Math.max(8, Math.floor(32 * (1 - far / 1024)))), up = fxNormals(n)[1];
	let lower = null, upper = null;
	for (let i = 0; i <= segments; i++) {
		const spoke = fxTurn(n, up, 360 / segments * (i % segments)), wasLower = lower, wasUpper = upper;
		upper = [e.org[0] + spoke[0] * size2, e.org[1] + spoke[1] * size2, e.org[2] + spoke[2] * size2];
		lower = [top[0] + spoke[0] * radius, top[1] + spoke[1] * radius, top[2] + spoke[2] * radius];
		if (!i) continue;
		const s0 = (i - 1) / segments, s1 = i / segments;
		fxVertex(wasLower[0], wasLower[1], wasLower[2], s0, 0);
		fxVertex(lower[0], lower[1], lower[2], s1, 0);
		fxVertex(wasUpper[0], wasUpper[1], wasUpper[2], s0, 1);
		fxVertex(upper[0], upper[1], upper[2], s1, 1);
	}
}

// RB_SurfaceElectricity with DoBoltSeg and ApplyShape: a bolt goes from its start to its end in steps of 20 units
// and strays from the straight way at each, by its chaos. The way it strays is the same in every frame (its
// seed), and each step is three strips with a kink of their own that is new in every frame.
function fxBolt(e, radius) {
	const start = e.org, d = [e.org2[0] - start[0], e.org2[1] - start[1], e.org2[2] - start[2]], fwd = fxUnit(d);
	let dis = Math.hypot(d[0], d[1], d[2]), seed = e.seed;
	// one that grows reaches its end when its life is over
	if (e.flags & FX_GROW) dis *= Math.min(1, Math.max(0, (fxTime - e.t0) / (e.t1 - e.t0)));
	const end = [start[0] + fwd[0] * dis, start[1] + fwd[1] * dis, start[2] + fwd[2] * dis], across = fxAcross(start, end);
	const sides = fxNormals(fwd), off = [10, 10, 10];
	// Q_crandom of the engine
	const stray = () => { seed = Math.imul(69069, seed) + 1 | 0; return 2 * ((seed & 0xffff) / 0x10000 - 0.5); };
	const kink = () => Math.random() * 2 - 1;
	let old = start, oldRadius = radius, oldPerc = 0;
	for (let i = 20; i <= dis && fxVerts + 12 <= FX_MAX_QUADS * 4; i += 20) {
		const perc = i + 20 > dis ? 1 : i / dis, a = stray() * 3, b = stray() * 7 * e.chaos, c = stray() * 7 * e.chaos;
		for (let k = 0; k < 3; k++) off[k] += fwd[k] * a + sides[0][k] * b + sides[1][k] * c;
		const cur = [0, 1, 2].map(k => (start[k] + off[k]) * (1 - perc) + end[k] * perc);
		// tapered: thin only close to its end
		const newRadius = e.flags & FX_TAPER ? radius * (1 - perc * perc) : radius;
		if (e.flags & FX_TAPER) oldRadius = radius * (1 - oldPerc * oldPerc);

		// ApplyShape: from cur back to old over two points that lie to both sides of the straight way
		const back = [old[0] - cur[0], old[1] - cur[1], old[2] - cur[2]], reach = Math.hypot(back[0], back[1], back[2]) * 0.7, turn = fxNormals(fxUnit(back));
		const sh1 = [0.66 + kink() * 0.1, 0.07 + kink() * 0.025, 0.07 + kink() * 0.025], sh2 = [0.33 + kink() * 0.1, -sh1[1] + kink() * 0.02, -sh1[2] + kink() * 0.02];
		const point = sh => [0, 1, 2].map(k => cur[k] * sh[0] + old[k] * (1 - sh[0]) + turn[0][k] * reach * sh[1] + turn[1][k] * reach * sh[2]);
		const p1 = point(sh1), p2 = point(sh2), r1 = newRadius * 0.666 + oldRadius * 0.333, r2 = newRadius * 0.333 + oldRadius * 0.666;
		fxStrip(cur, p1, across, newRadius, r1);
		fxStrip(p2, p1, across, r1, r2);
		fxStrip(p2, old, across, r2, oldRadius);

		old = cur; oldRadius = newRadius; oldPerc = perc;
	}
}

// The colour of a primitive now into fxColor, as CParticle::UpdateRGB and UpdateAlpha leave it for the vertices.
// With useAlpha the alpha is the alpha of the vertices. Without it the colour is made darker by it and the alpha
// of the vertices stays 0: enough for a shader that adds its image, which is what most effects have.
function fxColour(e) {
	let k = fxFade(e, 4, e.rgb[2]);
	if (e.flags & FX_RAND << 4) k *= Math.random();
	const from = e.rgb[0], to = e.rgb[1];
	let a = fxFade(e, 0, e.alpha[2]);
	a = Math.min(1, Math.max(0, e.alpha[0] * a + e.alpha[1] * (1 - a)));
	if (e.flags & FX_RAND) a *= Math.random();
	// a value that is no number is no colour: fxBuild drops what has one
	if (!(k - k === 0 && a - a === 0)) fxBad = true;
	a = Math.trunc(a * 255);
	for (let i = 0; i < 3; i++) {
		const c = Math.min(255, Math.max(0, Math.trunc((from[i] * k + to[i] * (1 - k)) * 255)));
		fxColor[i] = e.flags & FX_USE_ALPHA ? c : c * a >> 8;
	}
	fxColor[3] = e.flags & FX_USE_ALPHA ? a : 0;
}

// The quads of one primitive as the eye sees it now. view is cameraAxes().
function fxQuads(e, view) {
	const type = e.type, radius = fxNow(e, 8, e.size);
	fxColour(e);
	if (type === 'particle') fxSprite(e, view.left, view.up, radius);
	else if (type === 'orientedparticle') { const flat = fxNormals(e.normal); fxSprite(e, flat[0], flat[1], radius); }
	else if (type === 'line') fxStrip(e.org, e.org2, fxAcross(e.org, e.org2), radius, radius);
	else if (type === 'tail') {
		// CTail::CalcNewEndpoint: as long as its length, back along the way it came
		const back = fxUnit([e.old[0] - e.org[0], e.old[1] - e.org[1], e.old[2] - e.org[2]]), length = fxNow(e, 12, e.length);
		const end = [e.org[0] + back[0] * length, e.org[1] + back[1] * length, e.org[2] + back[2] * length];
		fxStrip(e.org, end, fxAcross(e.org, end), radius, radius);
	} else if (type === 'cylinder') { if (e.traced >= 0) fxCylinder(e, radius); }
	else if (type === 'electricity') fxBolt(e, radius);
}

// The vertices of everything that lives, into fxFloats and fxBytes: one batch for a shader, the batches in the
// order of the sort of their shaders. Each batch is { mat, t, first, count }: its material, the time its stages
// are drawn at, and where its vertex numbers are. It needs no GL.
function fxBuild(now) {
	fxVerts = fxStats.quads = 0;
	if (!fxLive.length) return [];
	const batches = new Map(), view = cameraAxes();
	for (const e of fxLive) {
		if (!e.shader || e.type === 'emitter') continue;
		// setShaderTime: the stages of the shader run from the time the primitive was made, an animation from its first image
		const own = e.flags & FX_SET_SHADER_TIME, key = own ? e.shader + '\n' + e.t0 : e.shader;
		let b = batches.get(key);
		if (!b) batches.set(key, b = { mat: materialFor(e.shader, 'fx'), t: own ? (fxTime - e.t0) / 1000 : now / 1000, list: [], first: 0, count: 0 });
		b.list.push(e);
	}
	const order = [...batches.values()].filter(b => !b.mat.nodraw && b.mat.stages.length).sort((a, b) => a.mat.sort - b.mat.sort);
	for (const b of order) {
		const from = fxVerts;
		for (const e of b.list) {
			// the most quads one primitive is made of are the 32 of a cylinder
			if (fxVerts + 32 * 4 > FX_MAX_QUADS * 4) break;
			const start = fxVerts;
			fxBad = false;
			fxQuads(e, view);
			if (fxBad) fxVerts = start;
		}
		b.first = from / 4 * 6;
		b.count = (fxVerts - from) / 4 * 6;
	}
	fxStats.quads = fxVerts / 4;
	return order.filter(b => b.count);
}

// The effects of the game: stepped by the time since the last frame and drawn over what is there, behind what is
// nearer. They never write the depth (materialFor).
function drawEffects(now) {
	if (!fxOn()) {
		fxClear();
		fxStats.quads = fxStats.batches = 0;
		fxDrawn = now;
		return;
	}
	fxAdvance(Math.min(100, Math.max(0, now - fxDrawn)));
	fxDrawn = now;
	const batches = fxBuild(now);
	fxStats.batches = batches.length;
	if (!batches.length) return;

	const program = gl.getParameter(gl.CURRENT_PROGRAM);
	gl.useProgram(stageProg.p);
	gl.uniform1i(stageProg.u.uTex, 0);
	gl.uniform3fv(stageProg.u.uEye, eye);
	gl.uniformMatrix4fv(stageProg.u.uMvp, false, viewProj);
	gl.uniform1i(stageProg.u.uSprite, 0);
	gl.activeTexture(gl.TEXTURE0);
	// the alpha of the canvas stays 1 whatever a stage blends, as in the map
	gl.colorMask(true, true, true, false);
	gl.bindBuffer(gl.ARRAY_BUFFER, fxVbo);
	gl.bufferSubData(gl.ARRAY_BUFFER, 0, fxBytes, 0, fxVerts * FX_STRIDE);
	for (const b of batches) {
		setCull(b.mat.cull);
		setDeform(stageProg, b.mat, b.t);
		if (b.mat.polygonOffset) { gl.enable(gl.POLYGON_OFFSET_FILL); gl.polygonOffset(-1, -2); }
		for (const st of b.mat.stages) drawStage(st, b.t, null, fxVao, b.count, gl.UNSIGNED_SHORT, b.first * 2);
		if (b.mat.polygonOffset) gl.disable(gl.POLYGON_OFFSET_FILL);
	}
	setDeform(stageProg, NO_DEFORM, 0);
	gl.disable(gl.CULL_FACE);
	gl.disable(gl.BLEND);
	gl.depthMask(true);
	gl.depthFunc(gl.LESS);
	gl.colorMask(true, true, true, true);
	gl.useProgram(program);
}
