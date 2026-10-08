/* ------------------------------------------------------------------ */
/* weapons, lightsabers and the first person view                      */
/* ------------------------------------------------------------------ */

// attach point matrix from a tag triangle, like G2_ProcessSurfaceBolt (3x4, row major)
function tagMatrix(tag, bones) {
	const p = [[0, 0, 0], [0, 0, 0], [0, 0, 0]];
	for (let j = 0; j < 3; j++) {
		const x = tag.pos[j * 3], y = tag.pos[j * 3 + 1], z = tag.pos[j * 3 + 2];
		if (!bones) { p[j] = [x, y, z]; continue; }
		for (let k = 0; k < 4; k++) {
			const w = tag.weights[j * 4 + k];
			if (!w) continue;
			const o = tag.bones[j * 4 + k] * 12;
			p[j][0] += w * (bones[o] * x + bones[o + 1] * y + bones[o + 2] * z + bones[o + 3]);
			p[j][1] += w * (bones[o + 4] * x + bones[o + 5] * y + bones[o + 6] * z + bones[o + 7]);
			p[j][2] += w * (bones[o + 8] * x + bones[o + 9] * y + bones[o + 10] * z + bones[o + 11]);
		}
	}
	const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
	const norm = v => { const l = Math.hypot(v[0], v[1], v[2]) || 1; return [v[0] / l, v[1] / l, v[2] / l]; };
	const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
	const longest = sub(p[1], p[0]), shortest = sub(p[0], p[2]);	// sides[0] and sides[2]
	const a1 = norm(shortest);
	let a0 = norm(longest);
	const d = a0[0] * a1[0] + a0[1] * a1[1] + a0[2] * a1[2];
	a0 = norm([a0[0] - d * a1[0], a0[1] - d * a1[1], a0[2] - d * a1[2]]);
	const a2 = norm(cross(longest, shortest)), o = p[2];
	return new Float32Array([a1[0], a0[0], -a2[0], o[0], a1[1], a0[1], -a2[1], o[1], a1[2], a0[2], -a2[2], o[2]]);
}

// 3x4 row major to 4x4 column major
const to44 = m => new Float32Array([m[0], m[4], m[8], 0, m[1], m[5], m[9], 0, m[2], m[6], m[10], 0, m[3], m[7], m[11], 1]);

// where a Ghoul2 model stands in the world (it looks down -Y, the game's yaw 0 is +X)
function entityMatrix(x, y, z, yaw, scale) {
	const a = yaw + Math.PI / 2, c = Math.cos(a) * scale, s = Math.sin(a) * scale;
	return new Float32Array([c, s, 0, 0, -s, c, 0, 0, 0, 0, scale, 0, x, y, z, 1]);
}

// where an entity draws its model: the axes of the game for its angles (AnglesToAxis), scaled, at its origin
function anglesMatrix(pos, angles, scale) {
	const r = Math.PI / 180, sp = Math.sin(angles[0] * r), cp = Math.cos(angles[0] * r), sy = Math.sin(angles[1] * r), cy = Math.cos(angles[1] * r),
		sr = Math.sin(angles[2] * r), cr = Math.cos(angles[2] * r);
	return new Float32Array([
		cp * cy * scale, cp * sy * scale, -sp * scale, 0,
		(sr * sp * cy - cr * sy) * scale, (sr * sp * sy + cr * cy) * scale, sr * cp * scale, 0,
		(cr * sp * cy + sr * sy) * scale, (cr * sp * sy - sr * cy) * scale, cr * cp * scale, 0,
		pos[0], pos[1], pos[2], 1]);
}

// a static model (weapon or saber hilt), GLM or MD3, with its tags
const staticModels = new Map();
function loadStaticModel(path) {
	return cached(staticModels, path, async () => {
		const buf = await fetchGameFile(path);
		let surfs, tags = {};
		if (/\.glm$/i.test(path)) {
			const parsed = parseGlmSkinned(buf);
			if (!parsed) return null;
			surfs = parsed.surfs;
			tags = parsed.tags;
		} else {
			surfs = parseMd3(buf);
			if (!surfs) return null;
		}
		const meshes = [];
		for (const s of surfs) {
			const vao = gl.createVertexArray();
			gl.bindVertexArray(vao);
			[[0, s.pos, 3], [1, s.norm, 3], [2, s.uv, 2]].forEach(([loc, data, size]) => {
				const b = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, b); gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
				gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, size, gl.FLOAT, false, 0, 0);
			});
			const ib = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, s.idx, gl.STATIC_DRAW);
			gl.bindVertexArray(null);
			const shader = s.shader && s.shader !== '[nomaterial]' ? s.shader : '';
			meshes.push({ vao, count: s.idx.length, tex: shader ? await mainTexture(shader) : grayTex, shaderName: shader, mat: null });
		}
		const tagMats = {};
		for (const [name, tag] of Object.entries(tags)) tagMats[name] = tagMatrix(tag, null);
		return { meshes, tags: tagMats };
	});
}

// the model if it is loaded, else starts loading it and returns null
const staticReady = new Map();
function staticNow(path) {
	if (staticReady.has(path)) return staticReady.get(path);
	if (!staticModels.has(path)) loadStaticModel(path).then(m => staticReady.set(path, m));
	return null;
}

// MD3 tags of the first frame: name -> 3x4 matrix
function md3Tags(buf) {
	const dv = new DataView(buf), tags = {};
	const n = dv.getInt32(80, true), o = dv.getInt32(96, true);
	for (let i = 0; i < n; i++) {
		const t = o + i * 112, org = [0, 1, 2].map(k => dv.getFloat32(t + 64 + k * 4, true));
		const ax = [0, 1, 2].map(r => [0, 1, 2].map(k => dv.getFloat32(t + 76 + (r * 3 + k) * 4, true)));
		// columns are the tag's forward, left, up axes
		tags[readStr(dv, t, 64).toLowerCase()] = new Float32Array([ax[0][0], ax[1][0], ax[2][0], org[0], ax[0][1], ax[1][1], ax[2][1], org[1], ax[0][2], ax[1][2], ax[2][2], org[2]]);
	}
	return tags;
}
const handTag = cached(new Map(), 'hand', async () => md3Tags(await fetchGameFile('models/weapons2/stun_baton/baton_hand.md3'))['tag_weapon'] || null);

function drawStatic(model, world) {
	if (modelsScripted()) { drawModelScripted(model.meshes, world); gl.useProgram(flatProg.p); return; }
	gl.useProgram(skinProg.p);
	gl.uniformMatrix4fv(skinProg.u.uMvp, false, viewProj);
	gl.uniformMatrix4fv(skinProg.u.uModel, false, world);
	gl.uniform3fv(skinProg.u.uTint, [0, 0, 0]);
	gl.uniform1f(skinProg.u.uTintAmount, 0);
	gl.uniform1i(skinProg.u.uTex, 0);
	gl.activeTexture(gl.TEXTURE0);
	for (const mesh of model.meshes) {
		gl.bindTexture(gl.TEXTURE_2D, mesh.tex);
		gl.bindVertexArray(mesh.vao);
		gl.drawElements(gl.TRIANGLES, mesh.count, gl.UNSIGNED_INT, 0);
	}
	gl.useProgram(flatProg.p);
}

/* glowing saber blades */

const SABER_COLORS = [[1, 0.15, 0.1], [1, 0.45, 0.05], [1, 0.95, 0.15], [0.15, 1, 0.2], [0.15, 0.4, 1], [0.8, 0.15, 1]];
const bladeProg = compile(`#version 300 es
layout(location=0) in vec3 aPos; layout(location=1) in vec2 aUv;
uniform mat4 uMvp; out vec2 vUv;
void main() { vUv = aUv; gl_Position = uMvp * vec4(aPos, 1.0); }`,
`#version 300 es
precision mediump float;
in vec2 vUv; uniform vec3 uColor; uniform float uRound; out vec4 o;
void main() {
	// a blade: bright along its middle, with a soft tip. uRound: a ball of light instead, bright in its middle
	float a = uRound > 0.5 ? max(0.0, 1.0 - length(vec2(vUv.x, vUv.y * 2.0 - 1.0))) : 1.0 - abs(vUv.x);
	float tip = uRound > 0.5 ? 1.0 : smoothstep(1.0, 0.92, vUv.y);
	float glow = a * a * tip;
	float core = smoothstep(0.62, 0.85, a) * tip * (uRound > 0.5 ? max(uColor.r, max(uColor.g, uColor.b)) : 1.0);
	o = vec4((uColor * glow * 1.4 + vec3(core)) , 1.0);
}`);
const bladeVao = gl.createVertexArray(), bladeBuf = gl.createBuffer();
gl.bindVertexArray(bladeVao);
gl.bindBuffer(gl.ARRAY_BUFFER, bladeBuf);
gl.bufferData(gl.ARRAY_BUFFER, 4 * 5 * 4, gl.DYNAMIC_DRAW);
gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 20, 0);
gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 2, gl.FLOAT, false, 20, 12);
gl.bindVertexArray(null);

const blades = [];	// collected while drawing models, drawn after everything opaque
// What is in the air (shots of /api/state), as far as it can be shown without the effects of the game: a bolt is
// a streak of light in about the colour of its effect, and what the game draws as a model (a rocket, a thermal
// detonator, a det pack) is that model. [length, radius, colour] by weapon, and by the other way to fire it.
const SABER_SPIN = 800;		// degrees a second, the turn the game gives a saber that was thrown
const SHOT_BOLT = {
	4: [20, 2.5, [1, 0.6, 0.15]], 5: [28, 2.5, [1, 0.15, 0.1]], 7: [24, 3, [0.2, 1, 0.25]], 8: [14, 2, [1, 0.9, 0.5]], 9: [20, 4, [0.5, 0.7, 1]],
	10: [10, 1.5, [1, 0.75, 0.3]], 11: [44, 4, [1, 0.55, 0.15]], 15: [40, 4, [0.6, 0.8, 1]], 16: [20, 2.5, [1, 0.6, 0.15]], 17: [28, 3, [1, 0.2, 0.15]], 18: [24, 2.5, [1, 0.3, 0.2]]
};
const SHOT_BOLT_ALT = { 8: [12, 7, [0.3, 0.5, 1]], 9: [16, 12, [0.5, 0.7, 1]], 10: [8, 3, [1, 0.75, 0.3]] };
const SHOT_MODEL = { 11: 'models/weapons2/merr_sonn/projectile.md3', 12: 'models/weapons2/thermal/thermal_proj.md3', 14: 'models/weapons2/detpack/det_pack.md3' };

// Where a shot ended: a flash for a moment, since the page has no explosion of the game to show. How far it
// reaches and how long it lasts (ms) for what goes off with a bang, by weapon and by the other way to fire it.
const SHOT_BANG = { 11: [60, 450], 12: [70, 500], 14: [90, 550], 15: [60, 450] }, SHOT_BANG_ALT = { 8: [45, 400], 9: [60, 450], 10: [35, 350] };
const flashes = [];		// { pos, weapon, alt, t }

function drawShots(k) {
	const now = performance.now(), up = flashes.length ? cameraAxes().up : null;
	for (let i = flashes.length - 1; i >= 0; i--) {
		const f = flashes[i], bang = (f.alt && SHOT_BANG_ALT[f.weapon]) || SHOT_BANG[f.weapon] || [7, 130], age = (now - f.t) / bang[1];
		if (age >= 1) { flashes.splice(i, 1); continue; }
		// it grows to its size at once and fades
		const r = bang[0] * Math.min(1, 0.4 + age * 2), fade = (1 - age) * (1 - age);
		const rgb = ((f.alt && SHOT_BOLT_ALT[f.weapon]) || SHOT_BOLT[f.weapon] || [0, 0, [1, 0.7, 0.3]])[2].map(v => Math.min(1, v + 0.25) * fade);
		// a square that faces the camera, as wide as it is high (a blade is 2.2 times its radius to each side)
		blades.push({ org: [f.pos[0] - up[0] * r, f.pos[1] - up[1] * r, f.pos[2] - up[2] * r], dir: up, len: r * 2, radius: r / 2.2, rgb, round: true });
	}
	for (const m of (state && state.shots) || []) {
		const weapon = m[1], pos = [m.prev[0] + (m[3] - m.prev[0]) * k, m.prev[1] + (m[4] - m.prev[1]) * k, m.prev[2] + (m[5] - m.prev[2]) * k];
		const speed = Math.hypot(m[6], m[7], m[8]), dir = speed ? [m[6] / speed, m[7] / speed, m[8] / speed] : [0, 0, 1];
		const model = SHOT_MODEL[weapon] ? staticNow(SHOT_MODEL[weapon]) : null;
		if (model) {
			// its nose where it flies to
			const yaw = Math.atan2(dir[1], dir[0]) * 180 / Math.PI, pitch = -Math.atan2(dir[2], Math.hypot(dir[0], dir[1])) * 180 / Math.PI;
			drawStatic(model, anglesMatrix(pos, [pitch, yaw, 0], 1));
		}
		const bolt = (m[2] && SHOT_BOLT_ALT[weapon]) || SHOT_BOLT[weapon] || (model ? null : [14, 2, [1, 1, 1]]);
		// the streak ends where the shot is: the fire of a rocket is behind it
		if (bolt) blades.push({ org: [pos[0] - dir[0] * bolt[0], pos[1] - dir[1] * bolt[0], pos[2] - dir[2] * bolt[0]], dir, len: bolt[0], radius: bolt[1], rgb: bolt[2] });
	}
}

function drawBlades() {
	if (!blades.length) return;
	gl.useProgram(bladeProg.p);
	gl.uniformMatrix4fv(bladeProg.u.uMvp, false, viewProj);
	gl.enable(gl.BLEND);
	gl.blendFunc(gl.ONE, gl.ONE);
	gl.depthMask(false);
	gl.bindVertexArray(bladeVao);
	for (const b of blades) {
		const mid = [0, 1, 2].map(k => b.org[k] + b.dir[k] * b.len / 2);
		const toEye = [0, 1, 2].map(k => eye[k] - mid[k]);
		let side = [b.dir[1] * toEye[2] - b.dir[2] * toEye[1], b.dir[2] * toEye[0] - b.dir[0] * toEye[2], b.dir[0] * toEye[1] - b.dir[1] * toEye[0]];
		const l = Math.hypot(...side) || 1, w = Math.max(3, b.radius) * 2.2;
		side = side.map(v => v / l * w);
		const tip = [0, 1, 2].map(k => b.org[k] + b.dir[k] * b.len);
		gl.bindBuffer(gl.ARRAY_BUFFER, bladeBuf);
		gl.bufferSubData(gl.ARRAY_BUFFER, 0, new Float32Array([
			b.org[0] - side[0], b.org[1] - side[1], b.org[2] - side[2], -1, 0,
			b.org[0] + side[0], b.org[1] + side[1], b.org[2] + side[2], 1, 0,
			tip[0] - side[0], tip[1] - side[1], tip[2] - side[2], -1, 1,
			tip[0] + side[0], tip[1] + side[1], tip[2] + side[2], 1, 1]));
		gl.uniform3fv(bladeProg.u.uColor, b.rgb || SABER_COLORS[b.color] || SABER_COLORS[4]);
		gl.uniform1f(bladeProg.u.uRound, b.round ? 1 : 0);
		gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
	}
	gl.depthMask(true);
	gl.disable(gl.BLEND);
	gl.useProgram(flatProg.p);
	blades.length = 0;
}

function weaponModelPath(weapon) {
	for (const it of classInfo.values()) if (it.type === 1 && it.tag === weapon) return it;
	return null;
}

// the weapon or the sabers in the hands of an animated player
function drawHeldWeapons(inst, d, world) {
	const m = inst.model;
	if (!m || d.dead) return;
	const hands = [m.tags['*r_hand'], m.tags['*l_hand']];
	if (d.weapon === 3) {
		(d.sabers || []).forEach((saber, si) => drawSaber(saber, si, d, inst));
		return;
	}
	const it = weaponModelPath(d.weapon);
	if (!it || !hands[0] || !it.model) return;
	const w = staticNow(it.model);
	if (w) drawStatic(w, M4.mul(world, to44(tagMatrix(hands[0], inst.bones))));
}

// Sabers come from the server's muzzle point and direction, the same numbers the game
// uses, smoothed between the polls. The hilt sits on the blade's axis below it.
function drawSaber(saber, si, d, inst) {
	if (si > 1 || (si === 0 && d.saberInFlight)) return;
	const h = staticNow(saber.model);
	const prev = inst.prevSabers && inst.prevSabers[si];
	const k = Math.min(1, (performance.now() - lastPoll) / pollInterval);
	saber.blades.forEach((blade, bi) => {
		const pb = prev && prev.blades[bi];
		let org = blade.org, dir = blade.dir;
		if (!dir || (!dir[0] && !dir[1] && !dir[2])) return;
		if (pb && pb.dir && (pb.dir[0] || pb.dir[1] || pb.dir[2])) {
			org = org.map((v, i) => pb.org[i] + (v - pb.org[i]) * k);
			dir = dir.map((v, i) => pb.dir[i] + (v - pb.dir[i]) * k);
		}
		const l = Math.hypot(dir[0], dir[1], dir[2]) || 1;
		dir = dir.map(v => v / l);
		if (bi === 0 && h) {
			// hilt: its +Z is the blade axis, the blade tag sits at the muzzle point
			const tag = h.tags['*blade1'];
			const tz = tag ? tag[11] : 6;
			let side = Math.abs(dir[2]) < 0.9 ? [0, 0, 1] : [1, 0, 0];
			let x = [side[1] * dir[2] - side[2] * dir[1], side[2] * dir[0] - side[0] * dir[2], side[0] * dir[1] - side[1] * dir[0]];
			const xl = Math.hypot(...x); x = x.map(v => v / xl);
			const y = [dir[1] * x[2] - dir[2] * x[1], dir[2] * x[0] - dir[0] * x[2], dir[0] * x[1] - dir[1] * x[0]];
			const o = org.map((v, i) => v - dir[i] * tz);
			drawStatic(h, new Float32Array([...x, 0, ...y, 0, ...dir, 0, o[0], o[1], o[2], 1]));
		}
		// saberHolstered 1 turns off the second blade or the second saber, 2 all of them
		if (d.saberHolstered === 2 || (d.saberHolstered === 1 && (bi > 0 || si > 0))) return;
		blades.push({ org, dir, len: blade.length, radius: blade.radius, color: blade.color });
	});
}

/* first person / gameplay camera */

const view = { id: -1, yaw: 0, pitch: 0 };

function viewAxes(yaw, pitch) {
	const y = yaw * Math.PI / 180, p = pitch * Math.PI / 180;
	const fwd = [Math.cos(p) * Math.cos(y), Math.cos(p) * Math.sin(y), -Math.sin(p)];
	const left = [-Math.sin(y), Math.cos(y), 0];
	const up = [fwd[1] * left[2] - fwd[2] * left[1], fwd[2] * left[0] - fwd[0] * left[2], fwd[0] * left[1] - fwd[1] * left[0]];
	return { fwd, left, up };
}

function lerpAngle(a, b, t) {
	let d = ((b - a) % 360 + 540) % 360 - 180;
	return a + d * t;
}

function setFirstPerson(id) {
	view.id = id;
	document.body.classList.toggle('fp', id >= 0);
	$('fpBtn').classList.toggle('primary', id >= 0);
	if (id >= 0) cam.follow = -1;
}

// camera for the player view: eye at view height, sabers get the game's chase camera
function firstPersonCamera(now) {
	const p = players.get(view.id);
	if (!p || p.data.team === 'spectator') { setFirstPerson(-1); return false; }
	const d = p.data, k = Math.min(1, (now - lastPoll) / pollInterval);
	view.yaw = lerpAngle(p.prevYaw ?? d.yaw, d.yaw, k);
	view.pitch = lerpAngle(p.prevPitch ?? d.pitch, d.pitch, k);
	const ax = viewAxes(view.yaw, view.pitch);
	const head = [p.draw[0], p.draw[1], p.draw[2] + (d.viewheight || 36)];
	view.thirdPerson = d.weapon === 3 || d.dead;
	if (view.thirdPerson) {
		// CG_OffsetThirdPersonView: target is the head plus cg_thirdPersonVertOffset 16,
		// the camera cg_thirdPersonRange 80 behind it along the view, both stop at walls
		const back = ax.fwd.map(v => -v);
		let target = [head[0], head[1], head[2] + 16];
		const up = pickGround(head, [0, 0, 1]);
		if (up && up[2] - head[2] < 20) target[2] = up[2] - 4;
		const hit = pickGround(target, back);
		let dist = 80;
		if (hit) dist = Math.min(dist, Math.hypot(hit[0] - target[0], hit[1] - target[1], hit[2] - target[2]) - 4);
		eye = target.map((v, i) => v + back[i] * Math.max(4, dist));
		viewProj = M4.mul(M4.perspective(fovY(), canvas.width / canvas.height, 4, 65536), M4.lookAt(eye, target, [0, 0, 1]));
	} else {
		eye = head;
		viewProj = M4.mul(M4.perspective(fovY(), canvas.width / canvas.height, 4, 65536), M4.lookAt(eye, head.map((v, i) => v + ax.fwd[i]), ax.up));
	}
	invViewProj = M4.invert(viewProj);
	return true;
}

// cg_fov 80 is horizontal on the game's 4:3 screen, wider windows see more to the sides
function fovY() {
	return 2 * Math.atan(Math.tan(80 * Math.PI / 360) * 0.75);
}

// the gun in front of the camera: hand at the eye, gun on its tag_weapon
function drawViewWeapon(d) {
	if (view.thirdPerson || d.weapon <= 2) return;
	const it = weaponModelPath(d.weapon);
	if (!it || !it.view) return;
	const gun = staticNow(it.view);
	handTag.then(t => { view.handTag = t; });
	if (!gun) return;
	const ax = viewAxes(view.yaw, view.pitch);
	const camWorld = new Float32Array([...ax.fwd, 0, ...ax.left, 0, ...ax.up, 0, eye[0], eye[1], eye[2], 1]);
	const world = view.handTag ? M4.mul(camWorld, to44(view.handTag)) : camWorld;
	gl.clear(gl.DEPTH_BUFFER_BIT);	// the gun is always in front, like RF_DEPTHHACK
	drawStatic(gun, world);
}

function renderHud(d) {
	$('hudName').innerHTML = colorName(d.name);
	drawHud(d);
}

