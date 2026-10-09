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
		// mdxaSkel_t: name, flags, parent, BasePoseMat, BasePoseMatInv (3x4 each)
		const base = new Float32Array(12), baseInv = new Float32Array(12);
		for (let k = 0; k < 12; k++) { base[k] = dv.getFloat32(o + 72 + k * 4, true); baseInv[k] = dv.getFloat32(o + 120 + k * 4, true); }
		skel.push({ name: readStr(dv, o, 64), parent: dv.getInt32(o + 68, true), base, baseInv });
	}
	// a bone by its name, as G2_Find_Bone looks it up (no matter the case)
	const byName = new Map();
	skel.forEach((b, i) => { if (!byName.has(b.name.toLowerCase())) byName.set(b.name.toLowerCase(), i); });
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
	return { dv, numFrames, ofsFrames, numBones, ofsPool, skel, order, byName };
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

// animation.cfg: name -> { first, count, loop, fps, frameLerp } (BG_ParseAnimationFile: the time of a frame in ms
// is rounded away from zero, and below zero for an animation that runs backwards)
function parseAnimCfg(text) {
	const map = new Map();
	for (const line of text.split(/\r?\n/)) {
		const m = line.trim().match(/^([A-Z0-9_]+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?[\d.]+)/);
		if (!m) continue;
		const fps = +m[5] || 1;
		map.set(m[1], { first: +m[2], count: +m[3], loop: +m[4], fps, frameLerp: fps < 0 ? Math.floor(1000 / fps) : Math.ceil(1000 / fps) });
	}
	return map;
}
// an animation the file does not have, as the game has it then
const G2_NO_ANIM = { first: 0, count: 0, loop: -1, fps: 10, frameLerp: 100 };
const g2Anim = (model, num) => model.cfg.get(model.anims[num]) || G2_NO_ANIM;

const g2 = { animNames: null, glas: new Map(), cfgs: new Map(), models: new Map(), instances: new Map() };

// The server does not answer while it changes the map. A file that was asked for just then is asked for again,
// for half a minute: without that a model whose files were on their way stays a placeholder until the page is
// loaded again. An answer that says the file is not there is final.
async function fetchGameFile(path, asText) {
	// the game takes a name with a / in front as the one without it, and some of its own classes write it so
	path = path.replace(/^[\/\\]+/, '');
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

// What a client loads for a player or an NPC: the folder of the model and its skin. A player says "kyle/default"
// (CG_RegisterClientModelname), an NPC the full path models/players/x/model.glm, with "*skin" at its end
// (CG_HandleAppendedSkin) or with the skin its vehicle names (CG_G2AnimEntModelLoad). A skin is a name
// (model_<name>.skin) or three files "head|torso|lower". In a team game a player has the skin of his team.
function modelKey(model, team, vehSkin) {
	let dir, skin = 'default';
	if (/^models\//i.test(model)) {
		const star = model.lastIndexOf('*');
		if (star >= 0) { skin = model.slice(star + 1) || 'default'; model = model.slice(0, star); }
		dir = model.replace(/\/[^/]*$/, '');
		if (vehSkin) skin = vehSkin;
	} else {
		const name = model || 'kyle', slash = name.indexOf('/');
		dir = 'models/players/' + ((slash < 0 ? name : name.slice(0, slash)) || 'kyle');
		if (slash >= 0 && name.slice(slash + 1)) skin = name.slice(slash + 1);
	}
	if (team === 'red' || team === 'blue') skin = team;
	return { dir, skin, key: dir + '|' + skin };
}

// A skin file as the engine reads it (RE_RegisterIndividualSkin): surface, shader. The names of the surfaces in
// lower case; a surface named "..._off" that is given "*off" is left out, any other loses its "_off".
function g2ParseSkin(text, into) {
	let n = 0;
	for (const line of text.split(/\r?\n/)) {
		const comma = line.indexOf(',');
		if (comma < 0) continue;
		let surf = line.slice(0, comma).trim().toLowerCase();
		const tex = line.slice(comma + 1).trim().replace(/^"|"$/g, '');
		if (!surf || surf.startsWith('tag_')) continue;
		if (surf.endsWith('_off')) {
			if (tex === '*off') continue;
			surf = surf.slice(0, -4);
		}
		into[surf] = tex;
		n++;
	}
	return n;
}

// the files of a skin: one, or the three of "head|torso|lower" (RE_SplitSkins)
async function g2LoadSkin(dir, skinName) {
	const skin = {}, parts = skinName.split('|').filter(Boolean);
	const files = skinName.includes('|') ? (parts.length >= 3 ? parts.slice(0, 3).map(p => dir + '/' + p + '.skin') : []) : [dir + '/model_' + skinName + '.skin'];
	let n = 0;
	for (const f of files) {
		try { n += g2ParseSkin(await fetchGameFile(f, true), skin); } catch (e) { /* this part of the skin is not there */ }
	}
	return n ? skin : null;
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

	// skin: surface -> shader, "*off" hides a surface. Without the skin that was asked for: the default one.
	const skin = (await g2LoadSkin(dir, skinName)) || (skinName !== 'default' && await g2LoadSkin(dir, 'default')) || {};

	const surfs = [];
	for (const s of parsed.surfs) {
		const tex = skin[s.name.toLowerCase()] || (s.shader !== '[nomaterial]' ? s.shader : '');
		if (/^\*off$/i.test(tex)) continue;
		surfs.push({ ...s, texName: tex });
	}

	// textures in the main GL context
	const textures = [];
	for (const s of surfs) textures.push(s.texName ? await mainTexture(s.texName) : grayTex);
	const anims = await g2.animNames;
	if (anims && !g2.animList) g2.animList = anims;
	// which set of animations the skeleton has, as the game numbers them (BG_ParseAnimationFile): 0 the humanoid
	// one, 1 the one of the rocket trooper, above that every other
	const animIndex = /players\/_humanoid\//i.test(glaPath) ? 0 : /players\/rockettrooper\//i.test(glaPath) ? 1 : 2;
	return { surfs, textures, gla, cfg: cfg || new Map(), anims, tags: parsed.tags, animIndex, glaPath, dir, skinName };
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
					// mip levels, filter and anisotropy as the engine has them (js/gl.js)
					finishTexture(bmp.width, bmp.height, {});
					return t;
				}
			} catch (e) { /* next extension */ }
		}
		return grayTex;
	}).then(t => t || grayTex);
}

// per entity: GPU buffers, the bone list of Ghoul2 and what the client game keeps of it between frames
function g2Instance(id, model, team, vehSkin) {
	const k = modelKey(model, team, vehSkin);
	let inst = g2.instances.get(id);
	if (inst && inst.key === k.key) return inst;
	// legs and torso are the lerpFrame_t of the client game, ci what its clientInfo_t keeps for the animations and
	// the angles, pe the swing of legs and torso (playerEntity_t). Filled by js/player-draw.js and js/g2-angles.js.
	inst = { key: k.key, model: null, blist: [], calc: [], touch: null, touchNow: 0, bones: null,
		legs: g2LerpFrame(), torso: g2LerpFrame(), ci: { legsAnim: 0, torsoAnim: 0, brokenLimbs: 0, corrTime: 0, lastHeadAngles: [0, 0, 0], lookTime: 0 }, pe: null, turAngles: [0, 0, 0], synced: -1 };
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
			const n = m.gla.numBones;
			inst.bones = new Float32Array(n * 12);
			inst.touch = new Int32Array(n);
			for (let i = 0; i < n; i++) inst.calc.push({ newFrame: 0, currentFrame: 0, backlerp: 0, blendFrame: 0, blendOldFrame: 0, blendMode: false, blendLerp: 0 });
			// a model without that bone has no torso of its own (cent->noLumbar), one without a face no face
			inst.noLumbar = !m.gla.byName.has('lower_lumbar');
			inst.noFace = !m.gla.byName.has('face');
			inst.model = m;
		});
	return inst;
}

// lerpFrame_t of the client game, as far as it is used with Ghoul2: no animation yet
const g2LerpFrame = () => ({ animationNumber: 0, animation: null, animationSpeed: 0, animationTorsoSpeed: 0, lastFlip: 0 });

/* ---- the bone list of a Ghoul2 instance (rd-vanilla/G2_bones.cpp) ---- */

const BONE_ANGLES_PREMULT = 0x0001, BONE_ANGLES_POSTMULT = 0x0002, BONE_ANGLES_REPLACE = 0x0004, BONE_ANGLES_TOTAL = 0x0007;
const BONE_ANIM_OVERRIDE = 0x0008, BONE_ANIM_OVERRIDE_LOOP = 0x0010, BONE_ANIM_OVERRIDE_FREEZE = 0x0040 + 0x0008, BONE_ANIM_BLEND = 0x0080, BONE_ANIM_TOTAL = 0x00d8;
// Eorientations of q_shared.h
const G2_POSITIVE_X = 1, G2_POSITIVE_Z = 2, G2_POSITIVE_Y = 3, G2_NEGATIVE_X = 4, G2_NEGATIVE_Z = 5, G2_NEGATIVE_Y = 6;

// G2_Find_Bone and G2_Add_Bone: the entry of a bone in the list, made when it is not there. null: no such bone.
function g2Bone(inst, boneName, add) {
	const num = inst.model.gla.byName.get(boneName.toLowerCase());
	if (num === undefined) return null;
	if (!inst.blist[num] && add) {
		inst.blist[num] = { boneNumber: num, flags: 0, startFrame: 0, endFrame: 0, startTime: 0, pauseTime: 0, animSpeed: 0,
			blendFrame: 0, blendLerpFrame: 0, blendTime: 0, blendStart: 0, boneBlendTime: 0, boneBlendStart: 0, matrix: new Float32Array(12) };
	}
	return inst.blist[num] || null;
}

// G2_TimingModel (tr_ghoul2.cpp): the frame an animation of a bone is at at a time, the frame after it and how far
// it is between the two. Into out (currentFrame, newFrame, backlerp), which keeps what it had when the animation
// has run out and neither loops nor freezes.
function g2TimingModel(bone, currentTime, out) {
	const animSpeed = bone.animSpeed;
	let time = ((bone.pauseTime ? bone.pauseTime : currentTime) - bone.startTime) / 50;
	if (time < 0) time = 0;
	let newFrame_g = bone.startFrame + time * animSpeed;
	const animSize = bone.endFrame - bone.startFrame, endFrame = bone.endFrame, loop = bone.flags & BONE_ANIM_OVERRIDE_LOOP;
	if (animSize) {
		// did we run off the end?
		if ((animSpeed > 0 && newFrame_g > endFrame - 1) || (animSpeed < 0 && newFrame_g < endFrame + 1)) {
			if (loop) {
				// back within the bounds of the animation
				if (animSpeed < 0) {
					if (newFrame_g < endFrame + 1 && newFrame_g >= endFrame) {
						out.backlerp = (endFrame + 1) - newFrame_g;
						out.currentFrame = endFrame;
						out.newFrame = bone.startFrame;
					} else {
						if (newFrame_g <= endFrame + 1) newFrame_g = endFrame + (newFrame_g - endFrame) % animSize - animSize;
						out.backlerp = Math.ceil(newFrame_g) - newFrame_g;
						out.currentFrame = Math.ceil(newFrame_g);
						out.newFrame = out.currentFrame <= endFrame + 1 ? bone.startFrame : out.currentFrame - 1;
					}
				} else {
					if (newFrame_g > endFrame - 1 && newFrame_g < endFrame) {
						out.backlerp = newFrame_g - Math.trunc(newFrame_g);
						out.currentFrame = Math.trunc(newFrame_g);
						out.newFrame = bone.startFrame;
					} else {
						if (newFrame_g >= endFrame) newFrame_g = endFrame + (newFrame_g - endFrame) % animSize - animSize;
						out.backlerp = newFrame_g - Math.trunc(newFrame_g);
						out.currentFrame = Math.trunc(newFrame_g);
						out.newFrame = newFrame_g >= endFrame - 1 ? bone.startFrame : out.currentFrame + 1;
					}
				}
			} else if ((bone.flags & BONE_ANIM_OVERRIDE_FREEZE) === BONE_ANIM_OVERRIDE_FREEZE) {
				// it holds its last frame
				out.currentFrame = animSpeed > 0 ? bone.endFrame - 1 : bone.endFrame + 1;
				out.newFrame = out.currentFrame;
				out.backlerp = 0;
			} else {
				bone.flags &= ~BONE_ANIM_TOTAL;
			}
		} else if (animSpeed > 0) {
			out.currentFrame = Math.trunc(newFrame_g);
			out.backlerp = newFrame_g - out.currentFrame;
			out.newFrame = out.currentFrame + 1;
			// on the last frame: on to the first one only when it loops
			if (out.newFrame >= endFrame) out.newFrame = loop ? bone.startFrame : bone.endFrame - 1;
		} else {
			out.backlerp = Math.ceil(newFrame_g) - newFrame_g;
			out.currentFrame = Math.ceil(newFrame_g);
			if (out.currentFrame > bone.startFrame) {
				out.currentFrame = bone.startFrame;
				out.newFrame = out.currentFrame;
				out.backlerp = 0;
			} else {
				out.newFrame = out.currentFrame - 1;
				if (out.newFrame < endFrame + 1) out.newFrame = loop ? bone.startFrame : bone.endFrame + 1;
			}
		}
	} else {
		out.currentFrame = animSpeed < 0 ? bone.endFrame + 1 : bone.endFrame - 1;
		if (out.currentFrame < 0) out.currentFrame = 0;
		out.newFrame = out.currentFrame;
		out.backlerp = 0;
	}
}

// G2_Get_Bone_Anim_Index: the frame (with its fraction) an animated bone is at, null when it has no animation
const g2Timed = { currentFrame: 0, newFrame: 0, backlerp: 0 };
function g2GetBoneAnim(inst, boneName, currentTime) {
	const bone = g2Bone(inst, boneName, false);
	if (!bone || !(bone.flags & (BONE_ANIM_OVERRIDE_LOOP | BONE_ANIM_OVERRIDE))) return null;
	g2Timed.currentFrame = g2Timed.newFrame = g2Timed.backlerp = 0;
	const flags = bone.flags;
	g2TimingModel(bone, currentTime, g2Timed);
	return { currentFrame: g2Timed.currentFrame + g2Timed.backlerp, startFrame: bone.startFrame, endFrame: bone.endFrame, flags, animSpeed: bone.animSpeed };
}
// G2API_GetBoneFrame: only the frame, 0 without an animation
const g2GetBoneFrame = (inst, boneName, currentTime) => { const a = g2GetBoneAnim(inst, boneName, currentTime); return a ? a.currentFrame : 0; };

// G2_Set_Bone_Anim_Index: a bone is given an animation, from startFrame to endFrame (the one after its last), at
// animSpeed frames per 50 ms, starting at setFrame (-1: at its start). With BONE_ANIM_BLEND the pose it was in is
// kept and blended out over blendTime ms.
function g2SetBoneAnim(inst, boneName, startFrame, endFrame, flags, animSpeed, currentTime, setFrame, blendTime) {
	const bone = g2Bone(inst, boneName, true);
	if (!bone) return false;
	let modFlags = flags;
	if (flags & BONE_ANIM_BLEND) {
		// where the animation it has is now
		const was = g2GetBoneAnim(inst, boneName, currentTime);
		if (was) {
			if (bone.blendStart === currentTime) {
				// a blend that has not started yet is replaced
				bone.blendTime = blendTime;
			} else {
				if (was.animSpeed < 0) {
					bone.blendFrame = Math.floor(was.currentFrame);
					bone.blendLerpFrame = Math.floor(was.currentFrame);
				} else {
					bone.blendFrame = was.currentFrame;
					bone.blendLerpFrame = Math.trunc(was.currentFrame + 1);
					// off the end of the animation it had: its first frame when that one loops, else its last
					if (bone.blendFrame >= was.endFrame) {
						if (bone.flags & BONE_ANIM_OVERRIDE_LOOP) bone.blendFrame = was.startFrame;
						else if (was.endFrame <= 0) bone.blendLerpFrame = 0;
						else bone.blendFrame = was.endFrame - 1;
					}
					if (bone.blendLerpFrame >= was.endFrame) {
						if (bone.flags & BONE_ANIM_OVERRIDE_LOOP) bone.blendLerpFrame = was.startFrame;
						else if (was.endFrame <= 0) bone.blendLerpFrame = 0;
						else bone.blendLerpFrame = was.endFrame - 1;
					}
				}
				bone.blendTime = blendTime;
				bone.blendStart = currentTime;
			}
		} else {
			// it had none: nothing to blend from
			bone.blendFrame = bone.blendLerpFrame = 0;
			bone.blendTime = 0;
			modFlags &= ~BONE_ANIM_BLEND;
		}
	} else {
		bone.blendFrame = bone.blendLerpFrame = 0;
		bone.blendTime = bone.blendStart = 0;
		modFlags &= ~BONE_ANIM_BLEND;
	}
	bone.endFrame = endFrame;
	bone.startFrame = startFrame;
	bone.animSpeed = animSpeed;
	bone.pauseTime = 0;
	bone.startTime = setFrame !== -1 ? currentTime - (setFrame - startFrame) * 50 / animSpeed : currentTime;
	bone.flags = (bone.flags & ~BONE_ANIM_TOTAL) | modFlags;
	return true;
}

// AnglesToAxis of the game: forward, left and up for pitch, yaw and roll in degrees
function g2AnglesToAxis(angles) {
	const r = Math.PI / 180, sp = Math.sin(angles[0] * r), cp = Math.cos(angles[0] * r), sy = Math.sin(angles[1] * r), cy = Math.cos(angles[1] * r),
		sr = Math.sin(angles[2] * r), cr = Math.cos(angles[2] * r);
	return [[cp * cy, cp * sy, -sp], [sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, sr * cp], [cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp]];
}

// Create_Matrix (G2_misc.cpp): the axes of angles as the columns of a 3x4 matrix
function g2CreateMatrix(angles, out) {
	const a = g2AnglesToAxis(angles);
	out[0] = a[0][0]; out[4] = a[0][1]; out[8] = a[0][2];
	out[1] = a[1][0]; out[5] = a[1][1]; out[9] = a[1][2];
	out[2] = a[2][0]; out[6] = a[2][1]; out[10] = a[2][2];
	out[3] = out[7] = out[11] = 0;
	return out;
}

// G2_Set_Bone_Angles with G2_Generate_Matrix: a bone is turned by angles besides its animation. up, left and
// forward say which axis of the bone each of the angles turns around.
const g2Gen1 = new Float32Array(12), g2Gen2 = new Float32Array(12);
function g2SetBoneAngles(inst, boneName, angles, flags, up, left, forward, blendTime, currentTime) {
	const bone = g2Bone(inst, boneName, true);
	if (!bone) return false;
	bone.flags = (bone.flags & ~BONE_ANGLES_TOTAL) | flags;
	bone.boneBlendStart = currentTime;
	bone.boneBlendTime = blendTime;
	const skel = inst.model.gla.skel[bone.boneNumber], newAngles = [0, 0, 0];
	if (flags & (BONE_ANGLES_PREMULT | BONE_ANGLES_POSTMULT)) {
		// which of the angles is the yaw of the matrix, which its pitch and which its roll
		switch (up) {
			case G2_NEGATIVE_X: newAngles[1] = angles[2] + 180; break;
			case G2_POSITIVE_X: newAngles[1] = angles[2]; break;
			case G2_NEGATIVE_Y: case G2_POSITIVE_Y: newAngles[1] = angles[0]; break;
			case G2_NEGATIVE_Z: newAngles[1] = angles[1] + 180; break;
			case G2_POSITIVE_Z: newAngles[1] = angles[1]; break;
		}
		switch (left) {
			case G2_NEGATIVE_X: newAngles[0] = angles[2]; break;
			case G2_POSITIVE_X: newAngles[0] = angles[2] + 180; break;
			case G2_NEGATIVE_Y: newAngles[0] = angles[0]; break;
			case G2_POSITIVE_Y: newAngles[0] = angles[0] + 180; break;
			case G2_NEGATIVE_Z: case G2_POSITIVE_Z: newAngles[0] = angles[1]; break;
		}
		switch (forward) {
			case G2_NEGATIVE_X: case G2_POSITIVE_X: newAngles[2] = angles[2]; break;
			case G2_NEGATIVE_Y: newAngles[2] = angles[0]; break;
			case G2_POSITIVE_Y: newAngles[2] = angles[0] + 180; break;
			case G2_NEGATIVE_Z: newAngles[2] = angles[1]; break;
			case G2_POSITIVE_Z: newAngles[2] = angles[1] + 180; break;
		}
		// in the space of the bone: base pose * turn * base pose inverted
		g2CreateMatrix(newAngles, g2Gen1);
		mul34(g2Gen1, skel.baseInv, g2Gen2);
		mul34(skel.base, g2Gen2, bone.matrix);
	} else {
		newAngles[0] = angles[0] + (left === G2_POSITIVE_Y ? 180 : 0); newAngles[1] = angles[1]; newAngles[2] = angles[2];
		g2CreateMatrix(newAngles, g2Gen1);
		const perm = g2Gen2.fill(0), row = o => o === G2_POSITIVE_X || o === G2_NEGATIVE_X ? 0 : o === G2_POSITIVE_Y || o === G2_NEGATIVE_Y ? 1 : 2,
			sign = o => o === G2_NEGATIVE_X || o === G2_NEGATIVE_Y || o === G2_NEGATIVE_Z ? -1 : 1;
		if (forward) perm[row(forward) * 4] = sign(forward);
		if (left) perm[row(left) * 4 + 1] = sign(left);
		if (up) perm[row(up) * 4 + 2] = sign(up);
		mul34(g2Gen1, perm, bone.matrix);
	}
	return true;
}

/* ---- the skeleton of a frame (CBoneCache::EvalLow and G2_TransformBone in tr_ghoul2.cpp) ---- */

const g2T2 = new Float32Array(12), g2T3 = new Float32Array(12), g2T4 = new Float32Array(12), g2T5 = new Float32Array(12), g2Tmp = new Float32Array(12), g2Tmp2 = new Float32Array(12);
const identity34 = new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0]);

// One bone, after its parent: it takes the frames of its parent unless the bone list gives it an animation of
// its own, and that holds for all bones below it. The matrices stay in the space of the model: the engine turns
// the root by a quarter (its rootMatrix), here the place the model is drawn at does that (g2Place).
function g2EvalBone(inst, b, time) {
	if (inst.touch[b] === inst.touchNow) return;
	inst.touch[b] = inst.touchNow;
	const gla = inst.model.gla, skel = gla.skel[b], parent = skel.parent >= 0 && skel.parent < gla.numBones ? skel.parent : -1, TB = inst.calc[b], bones = inst.bones;
	if (parent >= 0) {
		g2EvalBone(inst, parent, time);
		const par = inst.calc[parent];
		TB.newFrame = par.newFrame; TB.currentFrame = par.currentFrame; TB.backlerp = par.backlerp;
		TB.blendFrame = par.blendFrame; TB.blendOldFrame = par.blendOldFrame; TB.blendMode = par.blendMode; TB.blendLerp = par.blendLerp;
	} else {
		TB.newFrame = TB.currentFrame = TB.backlerp = TB.blendFrame = TB.blendOldFrame = TB.blendLerp = 0;
		TB.blendMode = false;
	}
	const over = inst.blist[b];
	let angleOverride = 0;
	if (over) {
		angleOverride = over.flags & BONE_ANGLES_TOTAL;
		if (over.flags & BONE_ANIM_BLEND) {
			const blendTime = time - over.blendStart;
			// only while there is blend time left, or a blend further up would be spoiled
			if (blendTime >= 0 && blendTime < over.blendTime) {
				TB.blendFrame = over.blendFrame;
				TB.blendOldFrame = over.blendLerpFrame;
				TB.blendLerp = blendTime / over.blendTime;
				TB.blendMode = true;
			} else {
				TB.blendMode = false;
			}
		} else if (over.flags & (BONE_ANIM_OVERRIDE_LOOP | BONE_ANIM_OVERRIDE)) {
			TB.blendMode = false;
		}
		if (over.flags & (BONE_ANIM_OVERRIDE_LOOP | BONE_ANIM_OVERRIDE)) g2TimingModel(over, time, TB);
	}
	const n = gla.numFrames;
	if (!(TB.newFrame >= 0 && TB.newFrame < n)) TB.newFrame = 0;
	if (!(TB.currentFrame >= 0 && TB.currentFrame < n)) TB.currentFrame = 0;
	if (TB.blendFrame < 0 || TB.blendFrame >= n + 1) TB.blendFrame = 0;
	if (!(TB.blendOldFrame >= 0 && TB.blendOldFrame < n)) TB.blendOldFrame = 0;

	// the pose that is blended out of: between its two frames
	if (TB.blendMode) {
		const backlerp = TB.blendFrame - Math.trunc(TB.blendFrame), frontlerp = 1 - backlerp;
		glaBone(gla, Math.min(n - 1, Math.trunc(TB.blendFrame)), b, g2T3);
		glaBone(gla, TB.blendOldFrame, b, g2T4);
		for (let j = 0; j < 12; j++) g2T5[j] = backlerp * g2T3[j] + frontlerp * g2T4[j];
	}
	if (!TB.backlerp) {
		glaBone(gla, TB.currentFrame, b, g2T2);
	} else {
		const frontlerp = 1 - TB.backlerp;
		glaBone(gla, TB.newFrame, b, g2T3);
		glaBone(gla, TB.currentFrame, b, g2T4);
		for (let j = 0; j < 12; j++) g2T2[j] = TB.backlerp * g2T3[j] + frontlerp * g2T4[j];
	}
	if (TB.blendMode) {
		const blendFrontlerp = 1 - TB.blendLerp;
		for (let j = 0; j < 12; j++) g2T2[j] = TB.blendLerp * g2T2[j] + blendFrontlerp * g2T5[j];
	}

	const out = bones.subarray(b * 12, b * 12 + 12), par = parent >= 0 ? bones.subarray(parent * 12, parent * 12 + 12) : identity34;
	if (angleOverride & BONE_ANGLES_REPLACE) {
		// where the animation would put it, then its turn replaced by the one of the list
		mul34(par, g2T2, g2Tmp);
		mul34(g2Tmp, skel.base, g2Tmp2);
		const scale = Math.hypot(g2Tmp2[0], g2Tmp2[1], g2Tmp2[2]);
		for (let r = 0; r < 3; r++) {
			for (let c = 0; c < 3; c++) g2Tmp[r * 4 + c] = over.matrix[r * 4 + c] * scale;
			g2Tmp[r * 4 + 3] = g2Tmp2[r * 4 + 3];
		}
		mul34(g2Tmp, skel.baseInv, out);
	} else if (angleOverride & BONE_ANGLES_PREMULT) {
		mul34(par, over.matrix, out);
	} else {
		mul34(par, g2T2, out);
	}
	if (angleOverride & BONE_ANGLES_POSTMULT) {
		g2Tmp.set(out);
		mul34(g2Tmp, over.matrix, out);
	}
}

// all bones of an instance at a time (G2_TransformGhoulBones): inst.bones, one 3x4 matrix per bone
function g2Skeleton(inst, time) {
	inst.touchNow++;
	for (let b = 0, n = inst.model.gla.numBones; b < n; b++) g2EvalBone(inst, b, time);
}

// The matrix of a bolt on a bone (G2_GetBoltMatrixLow): the bone times its base pose, in the space the engine has
// its bones in, which is the one of the model turned by a quarter (the rootMatrix of tr_ghoul2.cpp). Only that
// bone and the ones above it are worked out, if they are not yet for this time.
function g2BoneBolt(inst, boneName, time, fresh) {
	const num = inst.model.gla.byName.get(boneName.toLowerCase());
	if (num === undefined) return null;
	if (fresh) inst.touchNow++;
	g2EvalBone(inst, num, time);
	const m = new Float32Array(12), r = new Float32Array(12);
	mul34(inst.bones.subarray(num * 12, num * 12 + 12), inst.model.gla.skel[num].base, m);
	// rootMatrix: x becomes -y, y becomes x
	for (let c = 0; c < 4; c++) { r[c] = -m[4 + c]; r[4 + c] = m[c]; r[8 + c] = m[8 + c]; }
	return r;
}

// G2API_GetBoltMatrix for a bolt on a bone: where the bone is in the world, for the angles and the origin of its
// entity (the client game hands it cent->turAngles and cent->lerpOrigin). 3x4, in rows: the last column is the
// place, the first the axis the game calls POSITIVE_X. scale: the scale of the model. The bones of this frame.
function g2BoltMatrix(inst, boneName, angles, origin, scale, time) {
	const bolt = g2BoneBolt(inst, boneName, time, false);
	if (!bolt) return null;
	if (scale) { bolt[3] *= scale; bolt[7] *= scale; bolt[11] *= scale; }
	for (let r = 0; r < 3; r++) { const l = Math.hypot(bolt[r * 4], bolt[r * 4 + 1], bolt[r * 4 + 2]) || 1; bolt[r * 4] /= l; bolt[r * 4 + 1] /= l; bolt[r * 4 + 2] /= l; }
	const world = g2CreateMatrix(angles, new Float32Array(12)), m = new Float32Array(12);
	world[3] = origin[0]; world[7] = origin[1]; world[11] = origin[2];
	mul34(world, bolt, m);
	// the quarter turn the code of the game counts on
	for (let r = 0; r < 3; r++) { const x = m[r * 4]; m[r * 4] = -m[r * 4 + 1]; m[r * 4 + 1] = x; }
	return m;
}

// the vertices of an instance by its bones, into its buffers
function g2Skin(inst) {
	const m = inst.model, bones = inst.bones;
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
}

// The time the animations of the page run by: the time of the game at the last poll (state.time), carried on by
// the clock of the page. Not carried on in the comparison with the game, where the game nearly stands.
function g2Time() {
	const at = state ? state.time || 0 : 0;
	return refCam ? at : at + Math.min(pollInterval * 3, Math.max(0, performance.now() - lastPoll));
}

// Where a Ghoul2 model is drawn: its axes for the angles the client game gives its entity (AnglesToAxis), scaled,
// at its origin. The engine turns the root bone by a quarter, so that the model, which looks down -Y, looks along
// the first axis: that turn is in this matrix, and the bones stay in the space of the model.
function g2Place(origin, angles, scale) {
	const a = g2AnglesToAxis(angles);
	return new Float32Array([a[1][0] * scale, a[1][1] * scale, a[1][2] * scale, 0, -a[0][0] * scale, -a[0][1] * scale, -a[0][2] * scale, 0,
		a[2][0] * scale, a[2][1] * scale, a[2][2] * scale, 0, origin[0], origin[1], origin[2], 1]);
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

// An instance drawn at a place (g2Place). ent is what the game says of its entity (drawModelScripted): its colour,
// the ball that says its fog, a shader instead of the ones of its skin. In the plain view of the editor the model
// is drawn with its images and a little of the colour tint.
function g2Draw(inst, place, tint, tintAmount, ent) {
	// as the game draws him: with the scripts of his skin, and without the colour of his team over it
	if (modelsScripted()) { drawModelScripted(inst.meshes, place, ent); gl.useProgram(flatProg.p); return; }
	if (ent && ent.shader) return;
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
