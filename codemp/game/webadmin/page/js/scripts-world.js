// the sky of the engine (tr_sky.cpp): a box around the eye, and the layers of clouds on a dome above it
const SKY_SIDES = [[3, -1, 2], [-3, 1, 2], [1, 3, 2], [-1, -3, 2], [-2, -1, 3], [2, -1, -3]];
const SKY_SUFFIX = ['rt', 'lf', 'bk', 'ft', 'up', 'dn'];
const SKY_CUTS = 8, SKY_RADIUS = 4096;

function skyVec(s, t, side) {
	const b = [s, t, 1];
	return SKY_SIDES[side].map(k => k < 0 ? -b[-k - 1] : b[k - 1]);
}

// where a direction meets the layer of clouds, as texture coordinates
function cloudSt(v, height) {
	const R = SKY_RADIUS, x = v[0], y = v[1], z = v[2];
	const p = (1 / (2 * (x * x + y * y + z * z))) * (-2 * z * R + 2 * Math.sqrt(z * z * R * R + 2 * x * x * R * height + x * x * height * height
		+ 2 * y * y * R * height + y * y * height * height + 2 * z * z * R * height + z * z * height * height));
	const px = x * p, py = y * p, pz = z * p + R, l = Math.hypot(px, py, pz);
	return [Math.acos(px / l), Math.acos(py / l)];
}

function buildSky(name) {
	// without a script there is nothing that says what the sky is
	if (!scripts.bodies.has(scKey(name))) return null;
	const mat = materialFor(name, true);
	if (!mat.sky && !mat.stages.length) return null;
	const N = SKY_CUTS, pos = [], st = [], cst = [], idx = [], height = mat.sky ? mat.sky.height : 512;
	for (let side = 0; side < 6; side++) {
		const base = pos.length / 3;
		for (let j = 0; j <= N; j++) {
			for (let i = 0; i <= N; i++) {
				const s = i / N * 2 - 1, t = j / N * 2 - 1, v = skyVec(s, t, side);
				pos.push(v[0] * SKY_RADIUS, v[1] * SKY_RADIUS, v[2] * SKY_RADIUS);
				st.push((s + 1) / 2, 1 - (t + 1) / 2);
				cst.push(...cloudSt(v, height));
			}
		}
		for (let j = 0; j < N; j++) {
			for (let i = 0; i < N; i++) {
				const a = base + j * (N + 1) + i;
				idx.push(a, a + 1, a + N + 1, a + 1, a + N + 2, a + N + 1);
			}
		}
	}
	const posBuf = gl.createBuffer(), ib = gl.createBuffer();
	gl.bindBuffer(gl.ARRAY_BUFFER, posBuf);
	gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(pos), gl.STATIC_DRAW);
	const side = coords => {
		const vao = gl.createVertexArray(), buf = gl.createBuffer();
		gl.bindVertexArray(vao);
		gl.bindBuffer(gl.ARRAY_BUFFER, posBuf);
		gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
		gl.bindBuffer(gl.ARRAY_BUFFER, buf);
		gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(coords), gl.STATIC_DRAW);
		gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 2, gl.FLOAT, false, 0, 0);
		gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib);
		if (coords === st) gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array(idx), gl.STATIC_DRAW);
		gl.bindVertexArray(null);
		return vao;
	};
	return {
		boxVao: side(st), cloudVao: side(cst), perSide: N * N * 6, stages: mat.stages,
		box: mat.sky && mat.sky.box ? SKY_SUFFIX.map(suffix => Object.assign(newStage(), { maps: [mat.sky.box + '_' + suffix], clamp: true })) : null
	};
}

// The quads of a shader with deformVertexes autosprite or autosprite2, which the engine turns to the viewer in
// every frame (AutospriteDeform and Autosprite2Deform in tr_shade_calc.cpp). What does not depend on the viewer
// is worked out here, once, the rest is done by the vertex shader. Four vertices and six indexes are a quad.
// autosprite: a flare. The quad is made new around its middle, as wide as the old one, the whole image on it.
//   aPos is the middle, aLm how far along the left and the up of the view a corner is.
// autosprite2: a beam or a chain. It turns around its long axis: aPos is the middle of the short edge a vertex is
//   on, aNorm the long axis and aLm.x how far to which side of it the vertex is.
// null: the surfaces are no quads, they stay as they are (the engine warns then).
const EDGE_VERTS = [[0, 1], [0, 2], [0, 3], [1, 2], [1, 3], [2, 3]];

function buildSprites(b, kind) {
	const nv = b.verts, tri = b.tri;
	if ((nv & 3) || tri.length !== (nv >> 2) * 6) return null;
	const read = (buf, into) => { gl.bindBuffer(gl.ARRAY_BUFFER, buf); gl.getBufferSubData(gl.ARRAY_BUFFER, 0, into); return into; };
	const pos = read(b.posBuf, new Float32Array(nv * 3)), st = read(b.stBuf, new Float32Array(nv * 2)), col = read(b.colBuf, new Uint8Array(nv * 4));
	const at = new Float32Array(nv * 3), off = new Float32Array(nv * 2), axis = new Float32Array(nv * 3);
	let idx = tri;
	if (kind === 1) {
		// RB_AddQuadStamp: left and up, right and up, right and down, left and down
		const CORNER = [[1, 1], [-1, 1], [-1, -1], [1, -1]], ST = [0, 0, 1, 0, 1, 1, 0, 1];
		idx = new Uint32Array(tri.length);
		for (let i = 0, q = 0; i < nv; i += 4, q += 6) {
			const mid = [0, 1, 2].map(c => (pos[i * 3 + c] + pos[i * 3 + 3 + c] + pos[i * 3 + 6 + c] + pos[i * 3 + 9 + c]) / 4);
			const radius = Math.hypot(pos[i * 3] - mid[0], pos[i * 3 + 1] - mid[1], pos[i * 3 + 2] - mid[2]) * 0.707;
			for (let j = 0; j < 4; j++) {
				const v = i + j;
				at.set(mid, v * 3);
				off[v * 2] = CORNER[j][0] * radius; off[v * 2 + 1] = CORNER[j][1] * radius;
				st[v * 2] = ST[j * 2]; st[v * 2 + 1] = ST[j * 2 + 1];
				// the colour of the first vertex is the one of the whole quad
				for (let c = 0; c < 4; c++) col[v * 4 + c] = col[i * 4 + c];
			}
			idx.set([i, i + 1, i + 3, i + 3, i + 1, i + 2], q);
		}
	} else {
		at.set(pos);
		for (let i = 0, q = 0; i < nv; i += 4, q += 6) {
			// the two shortest edges are the ends of the quad
			const nums = [0, 0], lengths = [999999, 999999];
			for (let j = 0; j < 6; j++) {
				const a = (i + EDGE_VERTS[j][0]) * 3, c = (i + EDGE_VERTS[j][1]) * 3;
				const l = (pos[a] - pos[c]) ** 2 + (pos[a + 1] - pos[c + 1]) ** 2 + (pos[a + 2] - pos[c + 2]) ** 2;
				if (l < lengths[0]) { nums[1] = nums[0]; lengths[1] = lengths[0]; nums[0] = j; lengths[0] = l; }
				else if (l < lengths[1]) { nums[1] = j; lengths[1] = l; }
			}
			const mid = nums.map(n => [0, 1, 2].map(c => (pos[(i + EDGE_VERTS[n][0]) * 3 + c] + pos[(i + EDGE_VERTS[n][1]) * 3 + c]) / 2));
			const major = [mid[1][0] - mid[0][0], mid[1][1] - mid[0][1], mid[1][2] - mid[0][2]];
			for (let j = 0; j < 2; j++) {
				const v1 = i + EDGE_VERTS[nums[j]][0], v2 = i + EDGE_VERTS[nums[j]][1], l = 0.5 * Math.sqrt(lengths[j]);
				// which way round the edge is used says to which side its ends go
				let k = 0;
				for (; k < 5; k++) if (tri[q + k] === v1 && tri[q + k + 1] === v2) break;
				at.set(mid[j], v1 * 3); at.set(mid[j], v2 * 3);
				off[v1 * 2] = k === 5 ? l : -l;
				off[v2 * 2] = k === 5 ? -l : l;
			}
			for (let j = 0; j < 4; j++) axis.set(major, (i + j) * 3);
		}
	}
	const vao = gl.createVertexArray();
	gl.bindVertexArray(vao);
	const attr = (loc, data, size, type, normalized) => {
		gl.bindBuffer(gl.ARRAY_BUFFER, gl.createBuffer());
		gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
		gl.enableVertexAttribArray(loc);
		gl.vertexAttribPointer(loc, size, type, normalized, 0, 0);
	};
	attr(0, at, 3, gl.FLOAT, false);
	attr(1, st, 2, gl.FLOAT, false);
	attr(2, off, 2, gl.FLOAT, false);
	attr(3, col, 4, gl.UNSIGNED_BYTE, true);
	attr(4, axis, 3, gl.FLOAT, false);
	gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, gl.createBuffer());
	gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, idx, gl.STATIC_DRAW);
	gl.bindVertexArray(null);
	return { vao, count: idx.length };
}

// the batches of the map in the order and with the stages the game draws them
function prepareScripted(w) {
	if (scripts.state !== 2 || !w) return;
	const list = [];
	w.batches.forEach((b, i) => {
		const mat = materialFor(b.shaderName, !!b.lm);
		if (mat.nodraw || !mat.stages.length) return;
		const e = { b, mat, i, vao: b.vao, count: b.count, sprite: 0 };
		const kind = mat.deforms.some(d => d.kind === 'autosprite') ? 1 : mat.deforms.some(d => d.kind === 'autosprite2') ? 2 : 0;
		if (kind) {
			if (b.sprites === undefined) b.sprites = buildSprites(b, kind);
			if (b.sprites) Object.assign(e, { vao: b.sprites.vao, count: b.sprites.count, sprite: kind });
		}
		list.push(e);
	});
	list.sort((x, y) => x.mat.sort - y.mat.sort || x.i - y.i);
	// The fogs of the map, from the scripts of their shaders. exp2: sqrt( -ln( 1 / 255 ) ) over the depth, the
	// thickness the engine gives OpenGL for the fog of the whole map.
	w.fogInfo = w.fogs.map(f => {
		const fog = scripts.bodies.has(scKey(f.shader)) ? materialFor(f.shader, true).fog : null;
		return fog ? { color: fog.color, exp2: 2.354 / fog.depth, scale: 1 / fog.depth, global: f.global, plane: f.plane } : null;
	});
	w.globalFog = w.fogInfo.find(f => f && f.global) || null;
	// only what is solid enough is fogged (a sort up to see-through), as the engine decides it
	for (const e of list) e.fog = e.b.fog >= 0 && e.mat.sort <= SC_SORT_SEE_THROUGH && !e.mat.noFog ? w.fogInfo[e.b.fog] || null : null;
	w.skyDraw = w.sky ? buildSky(w.sky) : null;
	w.scripted = list;
}

const stTcA = new Float32Array(3), stTcB = new Float32Array(3), stTurb = new Float32Array(2), stColor = new Float32Array(4);

// Which side of a surface is drawn (GL_Cull in tr_backend.cpp): 0 the front, 1 the back, 2 both. The front of a
// triangle of the game is the side its corners run clockwise on. Seen from outside, a room is open that way:
// its walls and its ceiling face inwards.
const WAVE_FUNC = { sin: 1, triangle: 2, square: 3, sawtooth: 4, inversesawtooth: 5 };
const dfWave = new Float32Array(4), dfBulge = new Float32Array(4), dfMove = new Float32Array(3);

// the deformVertexes of a shader for the program that draws it next, at the time t
function setDeform(prog, mat, t) {
	// most have none: nothing to send when the program has none set either
	if (!mat.deforms.length && !prog.deformed) return;
	let func = 0;
	dfWave.fill(0); dfBulge.fill(0); dfMove.fill(0);
	for (const d of mat.deforms) {
		if (d.kind === 'wave') {
			const x = d.wave.phase + t * d.wave.freq;
			func = WAVE_FUNC[d.wave.func] || 1;
			// a wave that does not run is the same for every vertex
			dfWave[0] = d.wave.freq ? d.spread : 0; dfWave[1] = d.wave.base; dfWave[2] = d.wave.amp; dfWave[3] = x - Math.floor(x);
		} else if (d.kind === 'bulge') {
			dfBulge[0] = d.width; dfBulge[1] = d.height; dfBulge[2] = (t * d.speed) % (Math.PI * 2); dfBulge[3] = d.width === 0 && d.speed === 0 ? 1 : 0;
		} else if (d.kind === 'move') {
			const by = waveValue(d.wave, t);
			for (let i = 0; i < 3; i++) dfMove[i] += d.vec[i] * by;
		}
	}
	prog.deformed = mat.deforms.length > 0;
	gl.uniform1i(prog.u.uWaveFunc, func);
	gl.uniform4fv(prog.u.uWave, dfWave);
	gl.uniform4fv(prog.u.uBulge, dfBulge);
	gl.uniform3fv(prog.u.uMove, dfMove);
}

// where the camera looks, and its left and its up, in the map
function cameraAxes() {
	const at = (x, y, z) => { const v = M4.transform(invViewProj, [x, y, z]); return [v[0] / v[3], v[1] / v[3], v[2] / v[3]]; };
	const from = (a, b) => { const d = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], l = Math.hypot(d[0], d[1], d[2]) || 1; return [d[0] / l, d[1] / l, d[2] / l]; };
	const o = at(0, 0, 0);
	return { fwd: from(at(0, 0, -1), at(0, 0, 1)), left: from(o, at(-1, 0, 0)), up: from(o, at(0, 1, 0)) };
}

let cullSides = true;
function setCull(side) {
	if (side === 2 || !cullSides) { gl.disable(gl.CULL_FACE); return; }
	gl.enable(gl.CULL_FACE);
	gl.frontFace(gl.CW);
	gl.cullFace(side === 1 ? gl.FRONT : gl.BACK);
}

const FOG_BLACK = [0, 0, 0], FOG_WHITE = [1, 1, 1];

function drawStage(st, t, lm, vao, count, type, offset, prog = stageProg, fog = null) {
	let tex;
	if (st.lightmap) {
		tex = lm || whiteTex;
	} else if (st.white) {
		tex = whiteTex;
	} else {
		if (!st.images) st.images = st.maps.map(name => gameImage(name, st.clamp));
		const n = st.images.length;
		if (!n) return;
		const h = st.images[n > 1 ? ((Math.floor(t * st.animFreq) % n) + n) % n : 0];
		// an image that is not there, or not yet: grey where it covers something, nothing where it would lie over it
		if (!h.ready && st.blend) return;
		tex = h.tex;
	}

	if (st.blend) { gl.enable(gl.BLEND); gl.blendFunc(st.blend[0], st.blend[1]); } else gl.disable(gl.BLEND);
	gl.depthMask(st.depthWrite);
	gl.depthFunc(st.depthEqual ? gl.EQUAL : gl.LEQUAL);

	// the tcMods, each applied to what the ones before it made
	let a = 1, b = 0, c = 0, d = 0, e = 1, f = 0;
	stTurb[0] = 0;
	for (const m of st.tcMods) {
		const n = m.n;
		let k;
		if (m.kind === 'scroll') { const x = n[0] * t, y = n[1] * t; k = [1, 0, x - Math.floor(x), 0, 1, y - Math.floor(y)]; }
		else if (m.kind === 'scale') k = [n[0], 0, 0, 0, n[1], 0];
		else if (m.kind === 'rotate') { const r = -n[0] * t * Math.PI / 180, cs = Math.cos(r), sn = Math.sin(r); k = [cs, -sn, 0.5 - 0.5 * cs + 0.5 * sn, sn, cs, 0.5 - 0.5 * sn - 0.5 * cs]; }
		else if (m.kind === 'stretch') { const w = waveValue(m.wave, t), p = w ? 1 / w : 1; k = [p, 0, 0.5 - 0.5 * p, 0, p, 0.5 - 0.5 * p]; }
		else if (m.kind === 'transform') k = [n[0], n[2], n[4], n[1], n[3], n[5]];
		else { stTurb[0] = n[1]; stTurb[1] = n[2] + t * n[3]; continue; }
		[a, b, c, d, e, f] = [k[0] * a + k[1] * d, k[0] * b + k[1] * e, k[0] * c + k[1] * f + k[2], k[3] * a + k[4] * d, k[3] * b + k[4] * e, k[3] * c + k[4] * f + k[5]];
	}
	stTcA[0] = a; stTcA[1] = b; stTcA[2] = c; stTcB[0] = d; stTcB[1] = e; stTcB[2] = f;

	stColor[0] = stColor[1] = stColor[2] = stColor[3] = 1;
	if (st.rgb && st.rgb.wave) stColor[0] = stColor[1] = stColor[2] = Math.min(1, Math.max(0, waveValue(st.rgb.wave, t)));
	else if (st.rgb && st.rgb.value) { stColor[0] = st.rgb.value[0]; stColor[1] = st.rgb.value[1]; stColor[2] = st.rgb.value[2]; }
	if (st.alpha && st.alpha.wave) stColor[3] = Math.min(1, Math.max(0, waveValue(st.alpha.wave, t)));
	else if (st.alpha && st.alpha.value !== undefined) stColor[3] = st.alpha.value;

	const u = prog.u;
	gl.uniform1i(u.uTcGen, st.tcGen);
	gl.uniform3fv(u.uTcA, stTcA);
	gl.uniform3fv(u.uTcB, stTcB);
	gl.uniform2fv(u.uTurb, stTurb);
	gl.uniform4fv(u.uColor, stColor);
	gl.uniform1i(u.uRgbVertex, st.rgb && st.rgb.vertex ? st.rgb.vertex : st.rgb && st.rgb.diffuse ? 3 : 0);
	// rgbGen vertex brings the alpha of the vertex along unless the stage says what its alpha is
	gl.uniform1i(u.uAlphaVertex, st.alpha ? (st.alpha.vertex || 0) : (st.rgb && st.rgb.vertex === 1 && !st.rgb.exact ? 1 : 0));
	gl.uniform1i(u.uAlphaFunc, st.alphaFunc);
	if (fog) {
		// What a stage adds fades to black in the fog and what it multiplies with to white, so that what is under
		// it stays the colour of the fog. The engine has the two overrides for the same reason.
		const b = st.blend;
		const to = b && b[1] === gl.ONE ? FOG_BLACK : b && ((b[0] === gl.DST_COLOR && b[1] === gl.ZERO) || (b[0] === gl.ZERO && b[1] === gl.SRC_COLOR)) ? FOG_WHITE : fog.color;
		gl.uniform4f(u.uFog, to[0], to[1], to[2], fog.exp2);
	} else {
		gl.uniform4f(u.uFog, 0, 0, 0, 0);
	}
	gl.bindTexture(gl.TEXTURE_2D, tex);
	gl.bindVertexArray(vao);
	gl.drawElements(gl.TRIANGLES, count, type, offset);
}

function drawSky(sky, t) {
	// around the eye wherever it is, behind everything
	const at = new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, eye[0], eye[1], eye[2], 1]);
	gl.uniformMatrix4fv(stageProg.u.uMvp, false, M4.mul(viewProj, at));
	gl.disable(gl.DEPTH_TEST);
	// the sky has no colours at its vertices: white for a stage that asks for them
	gl.vertexAttrib4f(3, 1, 1, 1, 1);
	if (sky.box) {
		for (let side = 0; side < 6; side++) drawStage(sky.box[side], t, null, sky.boxVao, sky.perSide, gl.UNSIGNED_SHORT, side * sky.perSide * 2);
	}
	// the clouds are on every side but the one below
	for (const st of sky.stages) drawStage(st, t, null, sky.cloudVao, sky.perSide * 5, gl.UNSIGNED_SHORT, 0);
	gl.enable(gl.DEPTH_TEST);
}

// Fog is drawn where the camera is in the map, flying or through the eyes of a player. Looking at the map from
// above it would hide the map.
const fogShown = () => cam.fly || view.id >= 0;

// The map with its scripts, in two parts as the sort of the game has it: the sky and everything solid first,
// then, after the players and the models were drawn, what is see-through (late), so that glass and water lie
// over who stands behind them.
function drawWorldScripted(now, late) {
	const t = now / 1000;
	gl.useProgram(stageProg.p);
	gl.uniform1i(stageProg.u.uTex, 0);
	gl.uniform3fv(stageProg.u.uEye, eye);
	gl.activeTexture(gl.TEXTURE0);
	gl.disable(gl.CULL_FACE);
	// the alpha of the canvas stays 1 whatever a stage blends, or the page would shine through
	gl.colorMask(true, true, true, false);
	setDeform(stageProg, NO_DEFORM, t);
	gl.uniform1i(stageProg.u.uSprite, 0);
	if (!late && world.skyDraw) drawSky(world.skyDraw, t);
	gl.uniformMatrix4fv(stageProg.u.uMvp, false, viewProj);
	const fogged = fogShown();
	let sprite = 0, axes = null, placed = null;
	for (const e of world.scripted) {
		if ((e.mat.sort > SC_SORT_BANNER) !== late) continue;
		// what a model has of this sort or one before it comes first, as the game has the map last
		if (late && lateModels.length && drawLateModels(e.mat.sort)) {
			gl.useProgram(stageProg.p);
			gl.colorMask(true, true, true, false);
		}
		// a brush model that is an entity: where that is, and not at all without one
		const places = moverPlaces(e.b.model, now);
		if (!places.length) continue;
		const fog = fogged ? e.fog : null;
		// a quad that turns to the viewer has no back
		setCull(e.sprite ? 2 : e.mat.cull);
		if (e.sprite && !axes) {
			axes = cameraAxes();
			gl.uniform3fv(stageProg.u.uViewLeft, axes.left);
			gl.uniform3fv(stageProg.u.uViewUp, axes.up);
			gl.uniform3fv(stageProg.u.uViewFwd, axes.fwd);
		}
		if (e.sprite !== sprite) gl.uniform1i(stageProg.u.uSprite, sprite = e.sprite);
		setDeform(stageProg, e.mat, t);
		if (e.mat.polygonOffset) { gl.enable(gl.POLYGON_OFFSET_FILL); gl.polygonOffset(-1, -2); }
		for (const place of places) {
			if (place !== placed) {
				placed = place;
				gl.uniformMatrix4fv(stageProg.u.uMvp, false, place ? M4.mul(viewProj, place) : viewProj);
				gl.uniform3fv(stageProg.u.uEye, place ? eyeAt(place) : eye);
			}
			for (const st of e.mat.stages) drawStage(st, t, e.b.lm, e.vao, e.count, gl.UNSIGNED_INT, 0, stageProg, fog && fog.global ? fog : null);
		}
		// the fog of a brush is a pass of its own over what was just drawn, where the map has it
		if (fog && !fog.global && !e.sprite && !e.mat.deforms.length && !placed) {
			gl.useProgram(fogProg.p);
			gl.uniformMatrix4fv(fogProg.u.uMvp, false, viewProj);
			gl.uniform4fv(fogProg.u.uPlane, fog.plane);
			gl.uniform1f(fogProg.u.uScale, fog.scale);
			gl.uniform1f(fogProg.u.uEyeT, eye[0] * fog.plane[0] + eye[1] * fog.plane[1] + eye[2] * fog.plane[2] - fog.plane[3]);
			gl.uniform3fv(fogProg.u.uColor, fog.color);
			gl.enable(gl.BLEND);
			gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
			gl.depthMask(false);
			gl.depthFunc(gl.EQUAL);
			gl.bindVertexArray(e.b.vao);
			gl.drawElements(gl.TRIANGLES, e.b.count, gl.UNSIGNED_INT, 0);
			gl.useProgram(stageProg.p);
		}
		if (e.mat.polygonOffset) gl.disable(gl.POLYGON_OFFSET_FILL);
	}
	if (late) { drawLateModels(Infinity); deferLate = false; }
	gl.disable(gl.CULL_FACE);
	gl.disable(gl.BLEND);
	gl.depthMask(true);
	gl.depthFunc(gl.LESS);
	gl.colorMask(true, true, true, true);
}

