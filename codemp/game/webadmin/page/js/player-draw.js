// ---- the players and the NPCs drawn (CG_Player in cg_players.c)
//
// A player of the state (expandPlayers in js/state.js) has what a client draws him by: the frame the server has
// his legs and his torso at, the flags of his entity, his powerups and force powers. An NPC has the same in "f".
// Here: the animations as the client game sets them on the bones (CG_PlayerAnimation), brought to the frame of
// the server with every poll, the angles of js/g2-angles.js, the skeleton of js/g2.js, and the model drawn where
// CG_Player puts it. What is drawn around the model is in js/player-fx.js.

// BG_SaberStartTransAnim (bg_panimate.c): what the speed of a saber animation is scaled by. The scale a saber
// file gives its animations (animSpeedScale) is not known here: it is in the speed the feed tells of a frame.
function plSaberStartTransAnim(cent, anim, animSpeed, t) {
	const larm = cent.brokenLimbs & (1 << 1), rarm = cent.brokenLimbs & (1 << 2);
	if (t.trans.some(r => r[0] >= 0 && anim >= r[0] && anim <= r[1])) {
		// a transition: faster in the fast style, slower in the strong one (saberAnimLevel)
		if (cent.saberLevel === 1) animSpeed *= 1.5;
		else if (cent.saberLevel === 3) animSpeed *= 0.75;
		if (rarm) animSpeed *= 0.5; else if (larm) animSpeed *= 0.65;
	} else if (cent.brokenLimbs && t.saberFirst >= 0 && anim >= t.saberFirst && anim <= t.saberLast) {
		if (rarm) animSpeed *= 0.5; else if (larm) animSpeed *= 0.65;
	}
	return animSpeed;
}

// CG_SetLerpFrameAnimation: an animation is set on the bones of the legs ("model_root") or of the torso
// ("lower_lumbar", and "Motion" of a humanoid), from its first frame to the one after its last, looping or
// holding its last frame, blended from the pose before over 100 ms (200 around a flip, not at all around a death).
function plSetLerpFrameAnimation(inst, cent, lf, newAnimation, animSpeedMult, torsoOnly, flipState, time) {
	const m = inst.model, ci = inst.ci, t = g2AnimTables();
	let flags = BONE_ANIM_OVERRIDE_FREEZE, blendTime = 100;
	const oldSpeed = lf.animationSpeed, oldAnim = lf.animation ? lf.animationNumber : -1;
	if (m.animIndex > 0) ci.brokenLimbs = cent.brokenLimbs;
	lf.animationNumber = newAnimation;
	const anim = g2Anim(m, newAnimation);
	lf.animation = anim;
	// an animation its skeleton does not have: let be (not for humanoids)
	if (m.animIndex > 1 && anim.first === 0 && anim.count === 0) return;

	let animSpeed = 50 / anim.frameLerp, firstFrame, lastFrame, resumeFrame = false, beginFrame = -1;
	if (anim.loop !== -1) flags = BONE_ANIM_OVERRIDE_LOOP;
	if (animSpeed < 0) { lastFrame = anim.first; firstFrame = anim.first + anim.count; } else { firstFrame = anim.first; lastFrame = anim.first + anim.count; }
	flags |= BONE_ANIM_BLEND;		// cg_animBlend
	if (t.death.has(newAnimation) || (oldAnim !== -1 && t.death.has(oldAnim))) flags &= ~BONE_ANIM_BLEND;
	if ((flags & BONE_ANIM_BLEND) && (t.flipping.has(newAnimation) || (oldAnim !== -1 && t.flipping.has(oldAnim)))) blendTime = 200;
	animSpeed = plSaberStartTransAnim(cent, newAnimation, animSpeed * animSpeedMult, t);

	// the same animation at another speed goes on from the frame it is at
	if (torsoOnly) {
		if (lf.animationTorsoSpeed !== animSpeedMult && newAnimation === oldAnim && flipState === lf.lastFlip) resumeFrame = true;
		lf.animationTorsoSpeed = animSpeedMult;
	} else {
		if (lf.animationSpeed !== animSpeedMult && newAnimation === oldAnim && flipState === lf.lastFlip) resumeFrame = true;
		lf.animationSpeed = animSpeedMult;
	}
	// a vehicle animates its root bone only
	if (cent.vehicle) { g2SetBoneAnim(inst, 'model_root', firstFrame, lastFrame, flags, animSpeed, time, beginFrame, blendTime); return; }

	if (torsoOnly && !inst.noLumbar) {
		if (resumeFrame) beginFrame = Math.trunc(g2GetBoneFrame(inst, 'lower_lumbar', time));
		// the legs already run this animation: the torso takes their frame, against a wobbly spine
		const legsFrame = g2GetBoneFrame(inst, 'model_root', time);
		if (cent.torsoAnim === cent.legsAnim && legsFrame >= anim.first && legsFrame <= anim.first + anim.count) beginFrame = Math.trunc(legsFrame);
		if (firstFrame > lastFrame || ci.torsoAnim === newAnimation) beginFrame = -1;
		g2SetBoneAnim(inst, 'lower_lumbar', firstFrame, lastFrame, flags, animSpeed, time, beginFrame, blendTime);
		ci.torsoAnim = newAnimation;
	} else {
		if (resumeFrame) beginFrame = Math.trunc(g2GetBoneFrame(inst, 'model_root', time));
		if (beginFrame < firstFrame || beginFrame > lastFrame) beginFrame = -1;
		if (cent.torsoAnim === cent.legsAnim && (ci.legsAnim !== newAnimation || oldSpeed !== animSpeed)) {
			// the torso already runs it: the legs take its frame
			const oldBeginFrame = beginFrame;
			beginFrame = Math.trunc(g2GetBoneFrame(inst, 'lower_lumbar', time));
			if (beginFrame < firstFrame || beginFrame > lastFrame) beginFrame = oldBeginFrame;
		}
		g2SetBoneAnim(inst, 'model_root', firstFrame, lastFrame, flags, animSpeed, time, beginFrame, blendTime);
		ci.legsAnim = newAnimation;
	}
	if (m.animIndex <= 1 && cent.torsoAnim === newAnimation && !inst.noLumbar) g2SetBoneAnim(inst, 'Motion', firstFrame, lastFrame, flags, animSpeed, time, beginFrame, blendTime);
}

// CG_RunLerpFrame: the animation is set anew when it is another one, when the same one starts again (its flip
// bit turned), or when it has to run at another speed (CG_FirstAnimFrame)
function plRunLerpFrame(inst, cent, lf, flipState, newAnimation, speedScale, torsoOnly, time) {
	if (newAnimation !== lf.animationNumber || cent.brokenLimbs !== inst.ci.brokenLimbs || lf.lastFlip !== flipState || !lf.animation
		|| (torsoOnly ? lf.animationTorsoSpeed : lf.animationSpeed) !== speedScale) {
		plSetLerpFrameAnimation(inst, cent, lf, newAnimation, speedScale, torsoOnly, flipState, time);
	}
	lf.lastFlip = flipState;
}

// CG_PlayerAnimation: legs and torso, faster with force speed or rage
function plPlayerAnimation(inst, cent, time) {
	const t = g2AnimTables();
	let speedScale = 1;
	if (t.running.has(cent.legsAnim) || t.walking.has(cent.legsAnim)) {
		if (cent.forceActive & (1 << PG.FP_RAGE)) speedScale = 1.3;
		else if (cent.forceActive & (1 << PG.FP_SPEED)) speedScale = 1.7;
	}
	plRunLerpFrame(inst, cent, inst.legs, cent.legsFlip, cent.legsAnim, speedScale, false, time);
	// a vehicle has no torso animation
	if (!cent.vehicle) plRunLerpFrame(inst, cent, inst.torso, cent.torsoFlip, cent.torsoAnim, (cent.forceActive & (1 << PG.FP_RAGE)) ? 1.7 : 1, true, time);
}

// The frame the server has a skeleton at (the state feed, G2API_GetBoneAnim there): the bone that runs that
// animation here is put at that frame for the time of the poll and runs on at the speed the server has. Ghoul2
// tells of an animation that runs backwards the frame above where it is and how far it is below that one.
function plSyncBone(inst, boneName, animNum, frame, speed, pollTime) {
	const bone = g2Bone(inst, boneName, false);
	if (!bone || !(bone.flags & (BONE_ANIM_OVERRIDE_LOOP | BONE_ANIM_OVERRIDE)) || !(frame >= 0)) return false;
	const a = g2Anim(inst.model, animNum);
	if (Math.min(bone.startFrame, bone.endFrame) !== a.first || Math.max(bone.startFrame, bone.endFrame) !== a.first + a.count) return false;
	if (speed && (speed < 0) === (bone.animSpeed < 0)) bone.animSpeed = speed;
	if (!bone.animSpeed) return false;
	const at = bone.animSpeed < 0 ? 2 * Math.floor(frame) - frame : frame;
	bone.startTime = pollTime - (at - bone.startFrame) * 50 / bone.animSpeed;
	return true;
}

// What CG_Player does for the model of a player or an NPC in a frame: the angles, the animations, the skeleton.
// cent is what the game has of the entity (plPlayerCent, plNpcCent), time the time of the game the animations
// run by (g2Time), wall the clock of the page. Returns where the model is drawn (g2Place), and leaves in inst
// its bones, its vertices and cent.lerpOrigin, the origin CG_Player goes on with.
function plPose(inst, cent, time, wall) {
	// cg.frametime, for what swings
	const frametime = inst.wall === undefined ? 0 : Math.max(0, wall - inst.wall);
	inst.wall = wall;
	// a model that is scaled keeps its feet on the ground (not a vehicle)
	const origin = cent.origin.slice();
	if (!cent.vehicle && cent.scale !== 1) origin[2] += 24 * (cent.scale - 1);
	cent.lerpOrigin = origin;
	const legsAngles = g2PlayerAngles(inst, cent, time, frametime);
	plPlayerAnimation(inst, cent, time);
	// with a new poll: the frames of the server (none there: on from what the bones have)
	if (inst.synced !== lastPoll) {
		inst.synced = lastPoll;
		const at = state ? state.time || 0 : 0;
		inst.fed = [plSyncBone(inst, 'model_root', cent.legsAnim, cent.legsFrame, cent.legsSpeed, at),
			!cent.vehicle && plSyncBone(inst, inst.noLumbar ? 'model_root' : 'lower_lumbar', cent.torsoAnim, cent.torsoFrame, cent.torsoSpeed, at)];
		if (!cent.vehicle && !inst.noLumbar) plSyncBone(inst, 'Motion', cent.torsoAnim, cent.torsoFrame, cent.torsoSpeed, at);
	}
	g2Skeleton(inst, time);
	g2Skin(inst);
	inst.lastTime = time;
	return inst.place = g2Place(origin, legsAngles, cent.scale);
}

// a player of the state as the entity the client game draws (centity_t with its entityState_t), k: how far the
// frame is between the last two polls
function plPlayerCent(id, p, k) {
	const d = p.data;
	return { number: id, npc: false, vehicle: false, vehType: 0, vehicleNum: 0, forceFrame: 0, team: d.team,
		origin: p.draw, angles: [lerpAngle(p.prevPitch ?? d.pitch, d.pitch, k), lerpAngle(p.prevYaw ?? d.yaw, d.yaw, k), 0], vel: d.vel || [0, 0, 0],
		legsAnim: d.legsAnim, torsoAnim: d.torsoAnim, legsFlip: d.legsFlip || 0, torsoFlip: d.torsoFlip || 0, weapon: d.weapon,
		eFlags: d.eFlags ?? (d.dead ? PG.EF_DEAD : 0), eFlags2: d.eFlags2 || 0, powerups: d.powerups || 0, forceActive: d.forceActive || 0,
		saberMove: d.saberMove || 0, ground: d.ground ?? PG.ENTITYNUM_WORLD, brokenLimbs: d.brokenLimbs || 0, saberLevel: d.saberLevel || 0, moveDir: d.moveDir,
		legsFrame: d.legsFrame ?? -1, legsSpeed: d.legsSpeed || 0, torsoFrame: d.torsoFrame ?? -1, torsoSpeed: d.torsoSpeed || 0, scale: (d.scale || 100) / 100 };
}

// An NPC of the state the same way. The state has no place before for it: the instance keeps where it was at the
// poll before. A vehicle says its kind in "veh" (vehicleType_t), when the game module tells it.
function plNpcCent(n, inst, k) {
	if (inst.npcPoll !== lastPoll) {
		inst.npcPoll = lastPoll;
		inst.from = inst.to || { pos: n.pos, angles: n.angles || [0, n.yaw, 0] };
		inst.to = { pos: n.pos, angles: n.angles || [0, n.yaw, 0] };
	}
	const f = n.f || [], a = inst.from, b = inst.to, vehicle = n.class === PG.CLASS_VEHICLE;
	return { number: n.id, npc: true, vehicle, vehType: vehicle ? n.veh || 0 : 0, vehicleNum: 0, forceFrame: 0, team: '',
		origin: a.pos.map((v, i) => v + (b.pos[i] - v) * k), angles: a.angles.map((v, i) => lerpAngle(v, b.angles[i], k)), vel: n.vel || [0, 0, 0],
		legsAnim: n.legsAnim, torsoAnim: n.torsoAnim, legsFlip: (f[7] || 0) & 1, torsoFlip: ((f[7] || 0) & 2) >> 1, weapon: n.weapon || 0,
		eFlags: f[4] ?? (n.dead ? PG.EF_DEAD : 0), eFlags2: f[5] || 0, powerups: f[6] || 0, forceActive: 0, saberMove: 0, ground: f[8] ?? PG.ENTITYNUM_WORLD,
		brokenLimbs: 0, saberLevel: 0, moveDir: undefined, legsFrame: f[0] ?? -1, legsSpeed: f[1] || 0, torsoFrame: f[2] ?? -1, torsoSpeed: f[3] || 0, scale: (n.scale || 100) / 100 };
}

// A model the page can not pose (a file it reads wrongly) must not end the frame for everything drawn after it:
// the instance is given up, told once on the console, and nothing is drawn of it from then on.
function plTry(inst, what, draw) {
	if (inst.failed) return;
	try {
		draw();
	} catch (e) {
		inst.failed = String(e && e.stack || e);
		console.warn('the model of ' + what + ' can not be drawn (' + inst.key + '): ' + inst.failed.slice(0, 300));
		gl.bindVertexArray(null);
		gl.useProgram(flatProg.p);
	}
}

// the players with their models, or as plain figures until those are read
function drawPlayers(now) {
	const wall = performance.now(), time = g2Time(), k = Math.min(1, (wall - lastPoll) / pollInterval);
	for (const [id, p] of players) {
		const d = p.data;
		if (d.team === 'spectator') continue;
		// one the server would not send a client that looks from here is not drawn by it (the own player always is)
		if (id !== view.id && !clientSees(p.draw, 16, 25, 41)) continue;
		const s = (d.scale || 100) / 100, base = p.draw[2] - 24 * s;
		const col = TEAM_COLORS[d.team] || TEAM_COLORS.free;
		const c = d.dead ? [0.35, 0.35, 0.38, 1] : [col[0], col[1], col[2], 1];
		const yaw = d.yaw * Math.PI / 180;
		const inst = g2Instance(id, d.model, d.team);
		const ownView = id === view.id && !view.thirdPerson;
		if (inst.model && g2AnimTables()) {
			plTry(inst, 'player ' + id, () => {
				const cent = plPlayerCent(id, p, k), place = plPose(inst, cent, time, wall);
				// CG_Player draws nothing of one who is not to be drawn, and from inside the own head only what is around him
				if (!(cent.eFlags & PG.EF_NODRAW) && !(cent.eFlags2 & PG.EF2_SHIP_DEATH)) {
					plDrawModel(inst, cent, place, d, ownView, [col, d.dead ? 0.35 : 0.08]);
					if (!ownView) drawHeldWeapons(inst, d, place);
				}
			});
		} else if (ownView) {
			// nothing to draw from inside the head
		} else if (d.dead) {
			drawMesh(bodyMesh, p.draw[0], p.draw[1], base, yaw, s * 0.6, c);
		} else {
			drawMesh(bodyMesh, p.draw[0], p.draw[1], base, yaw, s, c);
			drawMesh(headMesh, p.draw[0], p.draw[1], base, yaw, s, [0.85, 0.75, 0.65, 1]);
			drawMesh(noseMesh, p.draw[0], p.draw[1], base, yaw, s, d.saberOn ? [0.6, 0.9, 1, 1] : [0.9, 0.9, 0.9, 1]);
		}
		if (id === selected && view.id < 0) drawMesh(ringMesh, p.draw[0], p.draw[1], base, now / 900, s, [1, 0.15, 0.25, 1]);
	}
}

// NPCs with their models: through the same code as a player (CG_Player draws both)
function drawNpcs(now) {
	if (!state) return;
	const wall = performance.now(), time = g2Time(), k = Math.min(1, (wall - lastPoll) / pollInterval);
	for (const n of state.npcs || []) {
		if (!clientSees(n.pos, 16, 25, 41)) continue;
		const inst = g2Instance(n.id, n.model, '', n.class === PG.CLASS_VEHICLE ? n.skin || 'default' : undefined);
		const sc = (n.scale || 100) / 100;
		if (inst.model && g2AnimTables()) {
			plTry(inst, 'NPC ' + n.id, () => {
				const cent = plNpcCent(n, inst, k), place = plPose(inst, cent, time, wall);
				if (!(cent.eFlags & PG.EF_NODRAW) && !(cent.eFlags2 & PG.EF2_SHIP_DEATH)) plDrawModel(inst, cent, place, null, false, [[1, 0.4, 1], 0.05]);
			});
		} else {
			drawMesh(bodyMesh, n.pos[0], n.pos[1], n.pos[2] - 24 * sc, n.yaw * Math.PI / 180, sc, [0.9, 0.4, 0.9, 1]);
		}
	}
}
