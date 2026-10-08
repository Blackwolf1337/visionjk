/* ------------------------------------------------------------------ */
/* Ghoul2 player and NPC models with animation                         */
/* ------------------------------------------------------------------ */

// GLM with bone weights, LOD 0
function parseGlmSkinned(buf) {
	const dv = new DataView(buf);
	if (readStr(dv, 0, 4) !== '2LGM') return null;
	const animName = readStr(dv, 72, 64), numSurfaces = dv.getInt32(152, true), ofsLods = dv.getInt32(148, true), hier = 164;
	const info = [];
	for (let i = 0; i < numSurfaces; i++) {
		const e = hier + dv.getInt32(hier + i * 4, true);
		info.push({ name: readStr(dv, e, 64), flags: dv.getUint32(e + 64, true), shader: readStr(dv, e + 68, 64) });
	}
	const surfs = [], tags = {};
	for (let i = 0; i < numSurfaces; i++) {
		const s = ofsLods + 4 + dv.getInt32(ofsLods + 4 + i * 4, true);
		const h = info[dv.getInt32(s + 4, true)] || info[i];
		const isTag = h.name.startsWith('*');
		if (!isTag && ((h.flags & 2) || /_off$/i.test(h.name))) continue;
		const nv = dv.getInt32(s + 12, true), ofsV = dv.getInt32(s + 16, true), nt = dv.getInt32(s + 20, true), ofsT = dv.getInt32(s + 24, true);
		const nRefs = dv.getInt32(s + 28, true), ofsRefs = dv.getInt32(s + 32, true);
		const refs = [];
		for (let r = 0; r < nRefs; r++) refs.push(dv.getInt32(s + ofsRefs + r * 4, true));
		const pos = new Float32Array(nv * 3), norm = new Float32Array(nv * 3), uv = new Float32Array(nv * 2), idx = new Uint32Array(nt * 3);
		const bones = new Uint16Array(nv * 4), weights = new Float32Array(nv * 4);
		for (let v = 0; v < nv; v++) {
			const b = s + ofsV + v * 32;
			for (let k = 0; k < 3; k++) { norm[v * 3 + k] = dv.getFloat32(b + k * 4, true); pos[v * 3 + k] = dv.getFloat32(b + 12 + k * 4, true); }
			const packed = dv.getUint32(b + 24, true), n = (packed >>> 30) + 1;
			let total = 0;
			for (let k = 0; k < n; k++) {
				bones[v * 4 + k] = refs[(packed >>> (5 * k)) & 31] || 0;
				let w;
				if (k === n - 1) w = 1 - total;
				else { w = (dv.getUint8(b + 28 + k) | ((packed >>> (12 + k * 2)) & 0x300)) / 1023; total += w; }
				weights[v * 4 + k] = w;
			}
			const t = s + ofsV + nv * 32 + v * 8;
			uv[v * 2] = dv.getFloat32(t, true); uv[v * 2 + 1] = dv.getFloat32(t + 4, true);
		}
		for (let k = 0; k < nt * 3; k++) idx[k] = dv.getInt32(s + ofsT + k * 4, true);
		// tag surfaces are attach points: the first three vertices give the matrix
		if (isTag) { if (nv >= 3) tags[h.name.toLowerCase()] = { pos, bones, weights }; continue; }
		surfs.push({ name: h.name, shader: h.shader, pos, norm, uv, idx, bones, weights });
	}
	return { animName, surfs, tags };
}

// GLA skeleton and compressed frames
function parseGla(buf) {
	const dv = new DataView(buf);
	if (readStr(dv, 0, 4) !== '2LGA') return null;
	const numFrames = dv.getInt32(76, true), ofsFrames = dv.getInt32(80, true), numBones = dv.getInt32(84, true);
	const ofsPool = dv.getInt32(88, true), skelBase = 100;
	const skel = [];
	for (let i = 0; i < numBones; i++) {
		const o = skelBase + dv.getInt32(skelBase + i * 4, true);
		skel.push({ name: readStr(dv, o, 64), parent: dv.getInt32(o + 68, true) });
	}
	// parents first, the file order does not promise that
	const order = [], seen = new Uint8Array(numBones);
	const visit = b => {
		if (seen[b]) return;
		seen[b] = 1;
		const p = skel[b].parent;
		if (p >= 0 && p < numBones) visit(p);
		order.push(b);
	};
	for (let i = 0; i < numBones; i++) visit(i);
	return { dv, numFrames, ofsFrames, numBones, ofsPool, skel, order };
}

// one bone of one frame as a 3x4 matrix (row major, like mdxaBone_t)
function glaBone(gla, frame, bone, out) {
	const dv = gla.dv, io = gla.ofsFrames + (frame * gla.numBones + bone) * 3;
	const index = dv.getUint8(io) | (dv.getUint8(io + 1) << 8) | (dv.getUint8(io + 2) << 16);
	const c = gla.ofsPool + index * 14;
	const w = dv.getUint16(c, true) / 16383 - 2, x = dv.getUint16(c + 2, true) / 16383 - 2;
	const y = dv.getUint16(c + 4, true) / 16383 - 2, z = dv.getUint16(c + 6, true) / 16383 - 2;
	const tx = 2 * x, ty = 2 * y, tz = 2 * z;
	const twx = tx * w, twy = ty * w, twz = tz * w, txx = tx * x, txy = ty * x, txz = tz * x, tyy = ty * y, tyz = tz * y, tzz = tz * z;
	out[0] = 1 - (tyy + tzz); out[1] = txy - twz; out[2] = txz + twy; out[3] = dv.getUint16(c + 8, true) / 64 - 512;
	out[4] = txy + twz; out[5] = 1 - (txx + tzz); out[6] = tyz - twx; out[7] = dv.getUint16(c + 10, true) / 64 - 512;
	out[8] = txz - twy; out[9] = tyz + twx; out[10] = 1 - (txx + tyy); out[11] = dv.getUint16(c + 12, true) / 64 - 512;
}

// a * b for 3x4 matrices with an implied 0 0 0 1 row
function mul34(a, b, out) {
	for (let r = 0; r < 3; r++) {
		const a0 = a[r * 4], a1 = a[r * 4 + 1], a2 = a[r * 4 + 2], a3 = a[r * 4 + 3];
		out[r * 4] = a0 * b[0] + a1 * b[4] + a2 * b[8];
		out[r * 4 + 1] = a0 * b[1] + a1 * b[5] + a2 * b[9];
		out[r * 4 + 2] = a0 * b[2] + a1 * b[6] + a2 * b[10];
		out[r * 4 + 3] = a0 * b[3] + a1 * b[7] + a2 * b[11] + a3;
	}
}

// animation.cfg: name -> { first, count, loop, fps }
function parseAnimCfg(text) {
	const map = new Map();
	for (const line of text.split(/\r?\n/)) {
		const m = line.trim().match(/^([A-Z0-9_]+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?[\d.]+)/);
		if (m) map.set(m[1], { first: +m[2], count: +m[3], loop: +m[4], fps: +m[5] });
	}
	return map;
}

const g2 = { animNames: null, glas: new Map(), cfgs: new Map(), models: new Map(), instances: new Map() };

// The server does not answer while it changes the map. A file that was asked for just then is asked for again,
// for half a minute: without that a model whose files were on their way stays a placeholder until the page is
// loaded again. An answer that says the file is not there is final.
async function fetchGameFile(path, asText) {
	for (let attempt = 0; ; attempt++) {
		try {
			const res = await fetch('api/file?name=' + encodeURIComponent(path));
			if (!res.ok) throw Object.assign(new Error(path + ': HTTP ' + res.status), { final: true });
			return await (asText ? res.text() : res.arrayBuffer());
		} catch (e) {
			if (e.final || attempt >= 10) throw e;
			await new Promise(done => setTimeout(done, 3000));
		}
	}
}

function cached(map, key, make) {
	if (!map.has(key)) map.set(key, make().catch(e => { console.warn(e); return null; }));
	return map.get(key);
}

// "kyle/default" or a full models/players/x/model.glm path, plus the team for the skin
function modelKey(model, team) {
	let dir, skin = 'default';
	if (/^models\//i.test(model)) {
		dir = model.replace(/\/[^/]*$/, '');
	} else {
		const [m, s] = (model || 'kyle').split('/');
		dir = 'models/players/' + (m || 'kyle');
		if (s) skin = s;
	}
	if (team === 'red' || team === 'blue') skin = team;
	return { dir, skin, key: dir + '|' + skin };
}

async function loadG2Model(dir, skinName) {
	if (!g2.animNames) g2.animNames = cached(new Map(), 'anims', async () => (await (await fetch('api/anims')).json()).anims);
	const parsed = parseGlmSkinned(await fetchGameFile(dir + '/model.glm'));
	if (!parsed) return null;
	const glaPath = parsed.animName + '.gla';
	const gla = await cached(g2.glas, glaPath, async () => parseGla(await fetchGameFile(glaPath)));
	if (!gla) return null;
	const cfgDir = parsed.animName.replace(/\/[^/]*$/, '');
	const cfg = await cached(g2.cfgs, cfgDir, async () => parseAnimCfg(await fetchGameFile(cfgDir + '/animation.cfg', true)));

	// skin: surface -> texture, "*off" hides a surface
	let skin = {};
	for (const name of [skinName, 'default']) {
		try {
			const text = await fetchGameFile(dir + '/model_' + name + '.skin', true);
			for (const line of text.split(/\r?\n/)) {
				const [surf, tex] = line.split(',').map(x => x && x.trim());
				if (surf && tex) skin[surf.toLowerCase()] = tex;
			}
			break;
		} catch (e) { /* try the default skin */ }
	}

	const surfs = [];
	for (const s of parsed.surfs) {
		const tex = skin[s.name.toLowerCase()] || (s.shader !== '[nomaterial]' ? s.shader : '');
		if (/^\*off$/i.test(tex)) continue;
		surfs.push({ ...s, texName: tex });
	}

	// bones that follow the torso animation: lower_lumbar and everything below it in the tree
	const torso = new Uint8Array(gla.numBones);
	const lumbar = gla.skel.findIndex(b => b.name.toLowerCase() === 'lower_lumbar');
	for (let i = 0; i < gla.numBones; i++) {
		for (let b = i; b >= 0; b = gla.skel[b].parent) if (b === lumbar) { torso[i] = 1; break; }
	}

	// textures in the main GL context
	const textures = [];
	for (const s of surfs) textures.push(s.texName ? await mainTexture(s.texName) : grayTex);
	return { surfs, textures, gla, cfg: cfg || new Map(), torso, anims: await g2.animNames, tags: parsed.tags };
}

const mainTextures = new Map();
function mainTexture(name) {
	const base = name.replace(/\.(tga|jpg|png)$/i, '');
	return cached(mainTextures, base, async () => {
		for (const ext of ['.jpg', '.png', '.tga']) {
			try {
				const data = await fetchGameFile(base + ext);
				if (ext === '.tga') {
					const img = decodeTga(data);
					if (img) return makeTexture(img.w, img.h, img.data);
				} else {
					const bmp = await createImageBitmap(new Blob([data]));
					const t = gl.createTexture();
					gl.bindTexture(gl.TEXTURE_2D, t);
					gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, bmp);
					gl.generateMipmap(gl.TEXTURE_2D);
					gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
					return t;
				}
			} catch (e) { /* next extension */ }
		}
		return grayTex;
	}).then(t => t || grayTex);
}

// per entity: GPU buffers and animation start times
function g2Instance(id, model, team) {
	const k = modelKey(model, team);
	let inst = g2.instances.get(id);
	if (inst && inst.key === k.key) return inst;
	inst = { key: k.key, model: null, legs: { anim: -1, start: 0 }, torso: { anim: -1, start: 0 } };
	g2.instances.set(id, inst);
	cached(g2.models, k.key, () => loadG2Model(k.dir, k.skin))
		.then(m => m || cached(g2.models, 'models/players/kyle|default', () => loadG2Model('models/players/kyle', 'default')))
		.then(m => {
			if (!m || g2.instances.get(id) !== inst) return;
			inst.meshes = m.surfs.map((s, i) => {
				const vao = gl.createVertexArray();
				gl.bindVertexArray(vao);
				const posBuf = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, posBuf); gl.bufferData(gl.ARRAY_BUFFER, s.pos.byteLength, gl.DYNAMIC_DRAW);
				gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
				const normBuf = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, normBuf); gl.bufferData(gl.ARRAY_BUFFER, s.norm.byteLength, gl.DYNAMIC_DRAW);
				gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);
				const uvBuf = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, uvBuf); gl.bufferData(gl.ARRAY_BUFFER, s.uv, gl.STATIC_DRAW);
				gl.enableVertexAttribArray(2); gl.vertexAttribPointer(2, 2, gl.FLOAT, false, 0, 0);
				const ib = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, s.idx, gl.STATIC_DRAW);
				gl.bindVertexArray(null);
				return { vao, posBuf, normBuf, count: s.idx.length, tex: m.textures[i], shaderName: s.texName, mat: null, outPos: new Float32Array(s.pos.length), outNorm: new Float32Array(s.norm.length) };
			});
			inst.bones = new Float32Array(m.gla.numBones * 12);
			inst.model = m;
		});
	return inst;
}

// frame of an animation at a time, with the lerp to the next frame
function animFrame(model, animNum, start, now) {
	const a = model.cfg.get(model.anims[animNum]) || model.cfg.get('BOTH_STAND1') || { first: 0, count: 1, loop: -1, fps: 20 };
	const fps = Math.abs(a.fps) || 20;
	let f = (now - start) / 1000 * fps;
	if (a.count <= 1) f = 0;
	else if (f >= a.count - 1) {
		if (a.loop >= 0 && a.loop < a.count) {
			const span = a.count - a.loop;
			f = a.loop + ((f - a.loop) % span + span) % span;
		} else {
			f = a.count - 1;
		}
	}
	let i0 = Math.floor(f), t = f - i0, i1 = Math.min(i0 + 1, a.count - 1);
	if (a.loop >= 0 && i0 === a.count - 1) i1 = a.loop;
	if (a.fps < 0) { i0 = a.count - 1 - i0; i1 = a.count - 1 - i1; }
	const max = model.gla.numFrames - 1;
	return [Math.min(max, a.first + i0), Math.min(max, a.first + i1), t];
}

const tmpA = new Float32Array(12), tmpB = new Float32Array(12), tmpL = new Float32Array(12);
const identity34 = new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0]);

// animates and skins one instance, returns false while it is still loading
function g2Update(inst, legsAnim, torsoAnim, now) {
	const m = inst.model;
	if (!m) return false;
	for (const [part, anim] of [[inst.legs, legsAnim], [inst.torso, torsoAnim]]) {
		if (part.anim !== anim) { part.anim = anim; part.start = now; }
	}
	const legs = animFrame(m, inst.legs.anim, inst.legs.start, now);
	const torso = animFrame(m, inst.torso.anim, inst.torso.start, now);
	const gla = m.gla, bones = inst.bones;
	for (const b of gla.order) {
		const fr = m.torso[b] ? torso : legs;
		glaBone(gla, fr[0], b, tmpA);
		glaBone(gla, fr[1], b, tmpB);
		for (let k = 0; k < 12; k++) tmpL[k] = tmpA[k] + (tmpB[k] - tmpA[k]) * fr[2];
		const p = gla.skel[b].parent;
		mul34(p >= 0 ? bones.subarray(p * 12, p * 12 + 12) : identity34, tmpL, bones.subarray(b * 12, b * 12 + 12));
	}

	m.surfs.forEach((s, si) => {
		const mesh = inst.meshes[si], op = mesh.outPos, on = mesh.outNorm;
		for (let v = 0, n = s.pos.length / 3; v < n; v++) {
			const x = s.pos[v * 3], y = s.pos[v * 3 + 1], z = s.pos[v * 3 + 2];
			const nx = s.norm[v * 3], ny = s.norm[v * 3 + 1], nz = s.norm[v * 3 + 2];
			let px = 0, py = 0, pz = 0, qx = 0, qy = 0, qz = 0;
			for (let k = 0; k < 4; k++) {
				const w = s.weights[v * 4 + k];
				if (!w) continue;
				const o = s.bones[v * 4 + k] * 12;
				px += w * (bones[o] * x + bones[o + 1] * y + bones[o + 2] * z + bones[o + 3]);
				py += w * (bones[o + 4] * x + bones[o + 5] * y + bones[o + 6] * z + bones[o + 7]);
				pz += w * (bones[o + 8] * x + bones[o + 9] * y + bones[o + 10] * z + bones[o + 11]);
				qx += w * (bones[o] * nx + bones[o + 1] * ny + bones[o + 2] * nz);
				qy += w * (bones[o + 4] * nx + bones[o + 5] * ny + bones[o + 6] * nz);
				qz += w * (bones[o + 8] * nx + bones[o + 9] * ny + bones[o + 10] * nz);
			}
			op[v * 3] = px; op[v * 3 + 1] = py; op[v * 3 + 2] = pz;
			on[v * 3] = qx; on[v * 3 + 1] = qy; on[v * 3 + 2] = qz;
		}
		gl.bindBuffer(gl.ARRAY_BUFFER, mesh.posBuf); gl.bufferSubData(gl.ARRAY_BUFFER, 0, op);
		gl.bindBuffer(gl.ARRAY_BUFFER, mesh.normBuf); gl.bufferSubData(gl.ARRAY_BUFFER, 0, on);
	});
	return true;
}

const skinProg = compile(`#version 300 es
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNorm; layout(location=2) in vec2 aUv;
uniform mat4 uMvp; uniform mat4 uModel; out vec3 vN; out vec2 vUv;
void main() { vN = mat3(uModel) * aNorm; vUv = aUv; gl_Position = uMvp * uModel * vec4(aPos, 1.0); }`,
`#version 300 es
precision mediump float;
in vec3 vN; in vec2 vUv; uniform sampler2D uTex; uniform vec3 uTint; uniform float uTintAmount; out vec4 o;
void main() {
	vec4 t = texture(uTex, vUv);
	vec3 n = normalize(vN);
	float l = 0.5 + 0.6 * max(dot(n, normalize(vec3(0.4, 0.3, 0.85))), 0.0);
	o = vec4(mix(t.rgb * l, uTint, uTintAmount), 1.0);
}`);

function g2Draw(inst, x, y, z, yaw, scale, tint, tintAmount) {
	// Ghoul2 models look down -Y, the game's yaw 0 is +X
	yaw += Math.PI / 2;
	const c = Math.cos(yaw), s = Math.sin(yaw);
	const place = new Float32Array([c * scale, s * scale, 0, 0, -s * scale, c * scale, 0, 0, 0, 0, scale, 0, x, y, z, 1]);
	// as the game draws him: with the scripts of his skin, and without the colour of his team over it
	if (modelsScripted()) { drawModelScripted(inst.meshes, place); gl.useProgram(flatProg.p); return; }
	gl.useProgram(skinProg.p);
	gl.uniformMatrix4fv(skinProg.u.uMvp, false, viewProj);
	gl.uniformMatrix4fv(skinProg.u.uModel, false, place);
	gl.uniform3fv(skinProg.u.uTint, tint);
	gl.uniform1f(skinProg.u.uTintAmount, tintAmount);
	gl.uniform1i(skinProg.u.uTex, 0);
	gl.activeTexture(gl.TEXTURE0);
	for (const mesh of inst.meshes) {
		gl.bindTexture(gl.TEXTURE_2D, mesh.tex);
		gl.bindVertexArray(mesh.vao);
		gl.drawElements(gl.TRIANGLES, mesh.count, gl.UNSIGNED_INT, 0);
	}
	gl.useProgram(flatProg.p);
}


