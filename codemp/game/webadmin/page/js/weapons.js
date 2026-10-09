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

/* streaks of light: what the plain view shows of a blade or a bolt */

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

// What is drawn after everything opaque, collected while the frame goes on. A saber blade: { org, dir, len, radius,
// color } (and lenMax, packed, mine where js/sabers.js made it), drawn as the game draws it (sbDraw) or as a streak
// in the plain view. A streak of a shot or a flash: { org, dir, len, radius, rgb, round }.
const blades = [];
// What is in the air (shots of /api/state), as far as it can be shown without the effects of the game: a bolt is
// a streak of light in about the colour of its effect, and what the game draws as a model is that model.
// [length, radius, colour] by weapon, and by the other way to fire it.
// With the effects (js/fx-weapons.js) the streak is the effect of the shot and the model stays.
const SABER_SPIN = 800;		// degrees a second, the turn the game gives a saber that was thrown
const SHOT_BOLT = {
	4: [20, 2.5, [1, 0.6, 0.15]], 5: [28, 2.5, [1, 0.15, 0.1]], 7: [24, 3, [0.2, 1, 0.25]], 8: [14, 2, [1, 0.9, 0.5]], 9: [20, 4, [0.5, 0.7, 1]],
	10: [10, 1.5, [1, 0.75, 0.3]], 11: [44, 4, [1, 0.55, 0.15]], 15: [40, 4, [0.6, 0.8, 1]], 16: [20, 2.5, [1, 0.6, 0.15]], 17: [28, 3, [1, 0.2, 0.15]], 18: [24, 2.5, [1, 0.3, 0.2]]
};
const SHOT_BOLT_ALT = { 8: [12, 7, [0.3, 0.5, 1]], 9: [16, 12, [0.5, 0.7, 1]], 10: [8, 3, [1, 0.75, 0.3]] };
// missileModel and altMissileModel of CG_RegisterWeapon (cg_weaponinit.c), by weapon. A trip mine and a det pack
// are no missiles for the clients: they come as models of the state.
const SHOT_MODEL = { 10: 'models/weapons2/golan_arms/projectileMain.md3', 11: 'models/weapons2/merr_sonn/projectile.md3', 12: 'models/weapons2/thermal/thermal_proj.md3' };
const SHOT_MODEL_ALT = { 10: 'models/weapons2/golan_arms/projectile.md3', 11: SHOT_MODEL[11], 12: SHOT_MODEL[12] };
// missileDlight and its colour: only a rocket shines, fired either way
const SHOT_LIGHT = { 11: [125, [1, 1, 0.5]] };
const EF_MISSILE_STICK = 1 << 22;

// where a shot of the state is now, on its way from where it was at the last poll
const shotPos = (m, k) => [m.prev[0] + (m[3] - m.prev[0]) * k, m.prev[1] + (m[4] - m.prev[1]) * k, m.prev[2] + (m[5] - m.prev[2]) * k];

// CG_Missile: the lights of what is in the air, for the lights of the frame
function shotLights(now) {
	const k = Math.min(1, (performance.now() - lastPoll) / pollInterval);
	for (const m of (state && state.shots) || []) {
		const light = SHOT_LIGHT[m[1]];
		if (light && m.prev) dlightAdd(shotPos(m, k), light[0], light[1]);
	}
}

// PerpendicularVector and RotateAroundDirection of the engine: the three axes of something that looks along fwd
// and is turned around that by yaw degrees
function aroundDirection(fwd, yaw) {
	let at = 0;
	for (let i = 1; i < 3; i++) if (Math.abs(fwd[i]) < Math.abs(fwd[at])) at = i;
	if (!(Math.abs(fwd[at]) < 1)) at = 0;
	const d = fwd[at], p = [-d * fwd[0], -d * fwd[1], -d * fwd[2]];
	p[at] += 1;
	let right = fxUnit(p);
	if (yaw) right = fxTurn(fwd, right, yaw);
	return [fwd, right, fxCross(fwd, right)];
}

// CG_Missile: how the model of a missile is turned. It looks the way it flies and spins around that, a quarter
// of a degree for every msec (half a degree for one that sticks). One that lies still looks the way its angles
// say when it has any (a mine on a wall), else up. The game takes the speed it was fired with for the way and,
// lying still, turns it by a time of its entity: the state has the speed of now and no such time.
function missileMatrix(m, pos) {
	const speed = Math.hypot(m[6], m[7], m[8]), stick = (m[12] | 0) & EF_MISSILE_STICK, r = Math.PI / 180;
	let fwd = [0, 0, 1], yaw = 0;
	if (speed) { fwd = [m[6] / speed, m[7] / speed, m[8] / speed]; yaw = frameNow * (stick ? 0.5 : 0.25) % 360; }
	else if (m[9] || m[10]) fwd = [Math.cos(m[9] * r) * Math.cos(m[10] * r), Math.cos(m[9] * r) * Math.sin(m[10] * r), -Math.sin(m[9] * r)];
	const ax = aroundDirection(fwd, yaw);
	return new Float32Array([...ax[0], 0, ...ax[1], 0, ...ax[2], 0, pos[0], pos[1], pos[2], 1]);
}

// Where a shot ended: a flash for a moment, when the page has no effect of the game to show for it. How far it
// reaches and how long it lasts (ms) for what goes off with a bang, by weapon and by the other way to fire it.
const SHOT_BANG = { 11: [60, 450], 12: [70, 500], 14: [90, 550], 15: [60, 450] }, SHOT_BANG_ALT = { 8: [45, 400], 9: [60, 450], 10: [35, 350] };
const flashes = [];		// { pos, weapon, alt, t }

function drawShots(k) {
	const now = performance.now(), fx = fxOn(), up = flashes.length ? cameraAxes().up : null;
	for (let i = flashes.length - 1; i >= 0; i--) {
		const f = flashes[i], bang = (f.alt && SHOT_BANG_ALT[f.weapon]) || SHOT_BANG[f.weapon] || [7, 130], age = (now - f.t) / bang[1];
		if (age >= 1) { flashes.splice(i, 1); continue; }
		// with the effects of the game it is what its weapon leaves there, played once
		if (fx && f.played === undefined) f.played = fxShotEnds(f);
		if (fx && f.played) continue;
		// it grows to its size at once and fades
		const r = bang[0] * Math.min(1, 0.4 + age * 2), fade = (1 - age) * (1 - age);
		const rgb = ((f.alt && SHOT_BOLT_ALT[f.weapon]) || SHOT_BOLT[f.weapon] || [0, 0, [1, 0.7, 0.3]])[2].map(v => Math.min(1, v + 0.25) * fade);
		// a square that faces the camera, as wide as it is high (a blade is 2.2 times its radius to each side)
		blades.push({ org: [f.pos[0] - up[0] * r, f.pos[1] - up[1] * r, f.pos[2] - up[2] * r], dir: up, len: r * 2, radius: r / 2.2, rgb, round: true });
	}
	for (const m of (state && state.shots) || []) {
		const weapon = m[1], pos = shotPos(m, k);
		const speed = Math.hypot(m[6], m[7], m[8]), dir = speed ? [m[6] / speed, m[7] / speed, m[8] / speed] : [0, 0, 1];
		const path = (m[2] ? SHOT_MODEL_ALT : SHOT_MODEL)[weapon], model = path ? staticNow(path) : null;
		if (model) drawStatic(model, missileMatrix(m, pos));
		if (fx && fxShotFlies(m, pos, dir)) continue;
		const bolt = (m[2] && SHOT_BOLT_ALT[weapon]) || SHOT_BOLT[weapon] || (model ? null : [14, 2, [1, 1, 1]]);
		// the streak ends where the shot is: the fire of a rocket is behind it
		if (bolt) blades.push({ org: [pos[0] - dir[0] * bolt[0], pos[1] - dir[1] * bolt[0], pos[2] - dir[2] * bolt[0]], dir, len: bolt[0], radius: bolt[1], rgb: bolt[2] });
	}
}

function drawBlades() {
	if (!blades.length) return;
	// the blades of sabers with the shaders of the game, where the view is the one of the game
	const scripted = modelsScripted(), sabers = scripted ? blades.filter(b => b.color !== undefined) : [];
	if (sabers.length) sbDraw(sabers);
	const streaks = scripted ? blades.filter(b => b.color === undefined) : blades;
	if (streaks.length) {
		gl.useProgram(bladeProg.p);
		gl.uniformMatrix4fv(bladeProg.u.uMvp, false, viewProj);
		gl.enable(gl.BLEND);
		gl.blendFunc(gl.ONE, gl.ONE);
		gl.depthMask(false);
		gl.bindVertexArray(bladeVao);
		for (const b of streaks) {
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
			// a saber in the plain view: a streak in the colour of its light
			gl.uniform3fv(bladeProg.u.uColor, b.rgb || sbLightRgb(b.color, b.packed));
			gl.uniform1f(bladeProg.u.uRound, b.round ? 1 : 0);
			gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
		}
		gl.depthMask(true);
		gl.disable(gl.BLEND);
	}
	gl.useProgram(flatProg.p);
	blades.length = 0;
}

function weaponModelPath(weapon) {
	for (const it of classInfo.values()) if (it.type === 1 && it.tag === weapon) return it;
	return null;
}

// the muzzle of the weapon a player holds, where the page drew it last: client -> { org, dir, now (the time of that frame) }
const wpMuzzles = new Map();

// The weapon or the sabers in the hands of an animated player. A client has them as Ghoul2 models bolted to the
// model of the player (CG_CopyG2WeaponInstance in cg_weapons.c): the world model of the weapon on his right hand
// (the first bolt, *r_hand), the hilt of his first saber there and the hilt of his second on the left (*l_hand).
// A bolted model stands where the bolt is: the matrix of the bolt is its own.
// inst: the Ghoul2 instance of the player with its bones of this frame, world: where his model stands.
function drawHeldWeapons(inst, d, world) {
	const m = inst.model;
	if (!m) return;
	const hand = name => m.tags[name] && inst.bones ? M4.mul(world, to44(tagMatrix(m.tags[name], inst.bones))) : null;
	if (d.weapon === 3) {
		(d.sabers || []).forEach((saber, si) => drawHilt(saber, si, d, hand(si ? '*l_hand' : '*r_hand')));
		return;
	}
	// bare hands hold nothing, and the gun that stands is no model in a hand
	if (d.weapon === 2 || d.weapon === 17) return;
	const it = weaponModelPath(d.weapon), at = hand('*r_hand');
	if (!it || !at || !it.model) return;
	const w = staticNow(it.model);
	if (!w) return;
	drawStatic(w, at);
	// Where its muzzle is (CG_AddPlayerWeapon for a player seen from outside): the *flash bolt of the weapon, and
	// the way a flash leaves it, which cgame reads as POSITIVE_X of the bolt (against its second axis, see drawHilt).
	const flash = w.tags['*flash'];
	if (flash) { const f = M4.mul(at, to44(flash)); wpMuzzles.set(d.id, { org: [f[12], f[13], f[14]], dir: fxUnit([-f[4], -f[5], -f[6]]), now: frameNow }); }
}

// a matrix that stands where at does, turned so that what looked along from looks along to, around the point pivot,
// which then lies at target
function turnedTo(at, pivot, from, to, target) {
	const v = fxCross(from, to), c = from[0] * to[0] + from[1] * to[1] + from[2] * to[2];
	let R;
	if (c < -0.999999) {
		// the other way round: half a turn around something that stands on it
		const n = fxNormals(from)[0];
		R = [2 * n[0] * n[0] - 1, 2 * n[0] * n[1], 2 * n[0] * n[2], 2 * n[1] * n[0], 2 * n[1] * n[1] - 1, 2 * n[1] * n[2], 2 * n[2] * n[0], 2 * n[2] * n[1], 2 * n[2] * n[2] - 1];
	} else {
		const k = 1 / (1 + c);
		R = [c + k * v[0] * v[0], k * v[0] * v[1] - v[2], k * v[0] * v[2] + v[1], k * v[1] * v[0] + v[2], c + k * v[1] * v[1], k * v[1] * v[2] - v[0], k * v[2] * v[0] - v[1], k * v[2] * v[1] + v[0], c + k * v[2] * v[2]];
	}
	const turn = (x, y, z) => [R[0] * x + R[1] * y + R[2] * z, R[3] * x + R[4] * y + R[5] * z, R[6] * x + R[7] * y + R[8] * z];
	const out = new Float32Array(16);
	for (let col = 0; col < 3; col++) out.set(turn(at[col * 4], at[col * 4 + 1], at[col * 4 + 2]), col * 4);
	const o = turn(at[12] - pivot[0], at[13] - pivot[1], at[14] - pivot[2]);
	out[12] = target[0] + o[0]; out[13] = target[1] + o[1]; out[14] = target[2] + o[2]; out[15] = 1;
	return out;
}

// The hilt of a saber in a hand. The blades of the frame are where the server has the bolts of the hilt
// (js/sabers.js), so the hilt is put to its blade: it stands as the hand holds it, turned the shortest way that
// lets its first blade bolt (*blade1) lie where that blade starts and points. A blade starts at the origin of its
// bolt and points against the first axis of the bolt as G2_ProcessSurfaceBolt makes it: that is what cgame reads
// as NEGATIVE_Y, because G2API_GetBoltMatrix swaps the first two axes of what it hands out. While the server
// does not say where the blade is (the saber is being raised, he is dead) the hilt is where the hand is, and the
// blades are taken from there for the next frame.
// hand: the matrix of the bolt of the hand, null when the model has none.
function drawHilt(saber, si, d, hand) {
	if (si > 1 || (si === 0 && d.saberInFlight)) return;
	const h = staticNow(saber.model), st = sbPlayers.get(d.id), now = st && st.now && st.now[si];
	if (!h) return;
	const tags = saber.blades.map((_, bi) => h.tags['*blade' + (bi + 1)] || (bi ? null : h.tags['*flash']) || null);
	const frame = (at, tag) => { const f = M4.mul(at, to44(tag)); return { org: [f[12], f[13], f[14]], dir: fxUnit([-f[0], -f[1], -f[2]]) }; };
	let at = hand;
	if (now && tags[0]) {
		const from = at || new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]), f = frame(from, tags[0]);
		at = turnedTo(from, f.org, f.dir, now.dir, now.org);
	}
	if (!at) return;
	drawStatic(h, at);
	if (!st) return;
	st.hand[si] = hand ? tags.map(tag => tag && frame(hand, tag)) : null;
	// a blade that is taken from the hand is where the hand of this frame has it, not the one of the last
	if (sbFromHand && st.hand[si]) st.hand[si].forEach((held, bi) => { const b = st.blades[si][bi].drawn; if (held && b) { b.org = held.org; b.dir = held.dir; } });
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
		viewProj = M4.mul(gameProjection(canvas.width, canvas.height), M4.lookAt(eye, target, [0, 0, 1]));
	} else {
		eye = head;
		viewProj = M4.mul(gameProjection(canvas.width, canvas.height), M4.lookAt(eye, head.map((v, i) => v + ax.fwd[i]), ax.up));
	}
	invViewProj = M4.invert(viewProj);
	return true;
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

