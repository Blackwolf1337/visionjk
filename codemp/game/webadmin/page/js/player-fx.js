// ---- what a client draws at a player besides his model (cg_players.c): the sprite above his head, the shells
// of force powers and powerups, the flag he carries
//
// CG_Player adds the model of a player to the scene more than once: a shell is the same model with a shader of
// its own (customShader) and the colour of its entity. Here plDrawModel is that part of CG_Player, in its order.
// The sprites are collected while the players are drawn and drawn when everything see-through of the map is
// there (drawPlayerSprites, called by a frame).

// The shaders and models the client game registers for this (cg_main.c)
const PL_MEDIA = Object.freeze({
	balloon: 'gfx/mp/chat_icon', connection: 'gfx/2d/net', teamRed: 'sprites/team_red', teamBlue: 'sprites/team_blue',
	playerShieldDamage: 'gfx/misc/personalshield', protect: 'gfx/misc/forceprotect', forceShell: 'powerups/forceshell', sightShell: 'powerups/sightshell',
	ysalired: 'powerups/ysaliredshell', ysaliblue: 'powerups/ysaliblueshell', ysalimari: 'powerups/ysalimarishell', boon: 'powerups/boonshell',
	endarkenment: 'powerups/endarkenmentshell', enlightenment: 'powerups/enlightenmentshell', invulnerability: 'powerups/invulnerabilityshell',
	electricBody: 'gfx/misc/electric', electricBody2: 'gfx/misc/fullbodyelectric2', cloaked: 'gfx/effects/cloakedShader',
	halfShieldModel: 'models/weaphits/testboom.md3', seekerModel: 'models/items/remote.md3',
	redFlag: 'models/flags/r_flag.md3', blueFlag: 'models/flags/b_flag.md3', redFlagYsal: 'models/flags/r_flag_ysal.md3', blueFlagYsal: 'models/flags/b_flag_ysal.md3'
});
const PL_GT_TEAM = 6, PL_GT_SIEGE = 7, PL_GT_CTY = 9, PL_SPEED_TRAIL_DISTANCE = 6;

const plSprites = [];		// the sprites of this frame: { org, shader, radius, rgba: the colour of its entity (0 to 255), white without }

// CG_PlayerFloatSprite: a sprite 48 above the origin of a player, 10 to each side
function plFloatSprite(cent, shader) {
	const o = cent.lerpOrigin;
	plAddSprite({ org: [o[0], o[1], o[2] + 48], shader, radius: 10 });
}

// The lightning and the drain of the Force (CG_Player, activeForcePass of the entity: the level of his lightning,
// or of his drain plus three). An effect at his left hand along the way his torso looks, played anew in every
// frame of a client: a bolt to where it meets something, wide from the third level on. The drain comes from the
// right hand in half of the frames while both hands are out (BOTH_FORCE_2HANDEDLIGHTNING_HOLD). JA+: the flame of
// a merc instead of lightning (EF_FLAME), the hand of the drain grab, and its own wide drain.
// Not here: a client that fights a private duel does not see the force effects of the others
// (CG_JPForceFxVisible).
const PL_FORCE_FX = { lightning: 'force/lightning', lightningWide: 'force/lightningwide', drain: 'mp/drain', drainWide: 'mp/drainwide_new', drainHand: 'force/drain_hand', flame: 'env/flame_jet' };
const plForceFrames = new Map();		// the name of such an effect -> what fxFrames made for it, with the plays of this frame

// An effect the client game plays at a player anew in every one of its frames (fxFramePlays of js/fx-run.js): how
// often in this frame of the page is asked once for all who have it. id: what tells the places of one effect
// apart, times: how often the game plays it there in a frame.
function plEveryFrame(name, id, org, fwd, times = 1) {
	let c = plForceFrames.get(name);
	if (!c) plForceFrames.set(name, c = fxFrames());
	if (c.at !== frameNow) { c.at = frameNow; c.plays = fxFramePlays(c, frameNow, fxOn() && fxReady(fxGet(name))); }
	for (let i = 0; i < times; i++) fxFramePlay(c, id, c.plays, name, org, fwd);
}

// The jetpack on the back of a player (CG_Player, EF_JETPACK): its model on the *chestg bolt of his model, not
// on the dead and not from inside his own head. While it is on (EF_JETPACK_ACTIVE) the jet effect at its two
// nozzles in every frame of a client, twice while it burns (EF_JETPACK_FLAMING). A nozzle is a bolt on a surface
// of the jetpack model, moved by 9.5 and 13.5 against two of its axes, as the game has it.
const PL_JETPACK = 'models/weapons2/jetpack/model.glm', PL_JET_FX = 'boba/jet';
let plJetBolts = null;		// the two bolts in the space of the jetpack model: null not asked for, false none (yet)
function plJetpack(inst, cent, place, ownView) {
	if (!(cent.eFlags & PG.EF_JETPACK) || (cent.eFlags & PG.EF_DEAD) || !inst.model || !inst.bones) return;
	const tag = inst.model.tags['*chestg'];
	if (!tag) return;
	const at = M4.mul(place, to44(tagMatrix(tag, inst.bones))), model = staticNow(PL_JETPACK);
	if (model && !ownView) drawStatic(model, at);
	if (plJetBolts === null) {
		plJetBolts = false;
		// The two are no tags and no bones but surfaces of the model that are drawn (torso_ljet, torso_rjet): a bolt on
		// such a surface is made of its first three vertices (G2_ProcessSurfaceBolt2), as the one of a tag surface.
		fetchGameFile(PL_JETPACK).then(buf => {
			const glm = parseGlmSkinned(buf), bolts = [];
			for (const name of ['torso_ljet', 'torso_rjet']) {
				const surf = glm && glm.surfs.find(sf => sf.name.toLowerCase() === name);
				if (!surf || surf.pos.length < 9) return;
				bolts.push(tagMatrix({ pos: surf.pos.subarray(0, 9) }, null));
			}
			plJetBolts = bolts;
		}, () => {});
	}
	if (!(cent.eFlags & PG.EF_JETPACK_ACTIVE) || !plJetBolts) return;
	plJetBolts.forEach((base, n) => {
		// the axes the code of the game calls POSITIVE_X and NEGATIVE_Y of a bolt (see drawHilt in js/weapons.js)
		const f = M4.mul(at, to44(base)), x = fxUnit([-f[4], -f[5], -f[6]]), negY = fxUnit([-f[0], -f[1], -f[2]]);
		const first = n === 0 ? negY : x, second = n === 0 ? x : negY;
		plEveryFrame(PL_JET_FX, cent.number * 2 + n, [0, 1, 2].map(i => f[12 + i] - 9.5 * first[i] - 13.5 * second[i]), second, cent.eFlags & PG.EF_JETPACK_FLAMING ? 2 : 1);
	});
}

// CG_ForceElectrocution: the bolts that jump off one who is electrified. In every frame of a client one of seven
// places of his body is taken by chance (elbows, hands, feet, and the bone lower_lumbar for the torso) and a way
// from there: one of two axes of that bolt (what cgame reads as NEGATIVE_X and NEGATIVE_Y of it), bent by up to
// 0.4 each way, 40 to 80 long. A bolt is there when that way meets the map, to where it does, and in six of a
// hundred frames when it does not: gfx/misc/blueLine, 1.5 wide to 4, growing to its end and thin towards it, for
// 100 to 150 msec. A try belongs to a frame of a client, the last one to now and each one before it to a frame
// earlier: its bolt is that much older. While the time of the comparison stands: a pair of one moment is taken
// after seven seconds in which the game has a frame for every msec and goes on by 70 (tools/panelshot/
// steps_ref.js), so there are that many tries, a msec apart. No bolt is old enough then to be full grown.
const PL_SHOCK_BOLTS = ['*r_arm_elbow', '*l_hand', '*r_hand', '*l_leg_foot', '*r_leg_foot', 'lower_lumbar', '*l_arm_elbow'], PL_SHOCK_SHADER = 'gfx/misc/blueLine';
const PL_SHOCK_REF = 70;
let plShockFrames = null;		// (the parts of the effects come after this one in the page: nothing of them is used before a frame)
function plElectrocution(inst, cent, place) {
	const model = inst.model, c = plShockFrames || (plShockFrames = fxFrames());
	if (!model || !inst.bones || cent.vehicle) return;
	if (c.at !== frameNow) { c.at = frameNow; c.plays = fxFramePlays(c, frameNow, fxOn()); }
	let tries = c.plays, first = -1;
	if (tries < 0) { first = c.done.get(cent.number) || 0; tries = Math.min(PL_SHOCK_REF - first, 30); c.done.set(cent.number, first + tries); }
	for (let n = 0; n < tries; n++) {
		const name = PL_SHOCK_BOLTS[Math.floor(Math.random() * 7)], tag = model.tags[name], bone = tag ? undefined : model.gla.byName.get(name);
		let at = null;
		if (tag) at = M4.mul(place, to44(tagMatrix(tag, inst.bones)));
		else if (bone !== undefined) { const m = new Float32Array(12); mul34(inst.bones.subarray(bone * 12, bone * 12 + 12), model.gla.skel[bone].base, m); at = M4.mul(place, to44(m)); }
		// without the bolt: from his origin, any way (the code of the game for what has no such bolts)
		const org = at ? [at[12], at[13], at[14]] : cent.lerpOrigin.slice(0, 3);
		let dir = [Math.random() * 2 - 1, Math.random() * 2 - 1, Math.random() * 2 - 1];
		if (at) dir = (Math.random() > 0.5 ? fxUnit([at[4], at[5], at[6]]) : fxUnit([-at[0], -at[1], -at[2]])).map(v => v + (Math.random() * 2 - 1) * 0.4);
		const len = Math.random() * 40 + 40, end = [org[0] + dir[0] * len, org[1] + dir[1] * len, org[2] + dir[2] * len], tr = traceWorld(org, end, 1);
		if (!tr && !(Math.random() > 0.94)) continue;
		const bolt = fxAddElectricity(org, tr ? tr.pos.slice(0, 3) : end, 1.5, 4, 0, 1, 0.5, 0, FX_WHITE, FX_WHITE, 0, 5, Math.random() * 50 + 100, PL_SHOCK_SHADER, FX_ALPHA_LINEAR | FX_SIZE_LINEAR | FX_USE_PHYSICS | FX_GROW | FX_TAPER);
		if (bolt) bolt.born = first < 0 ? (tries - 1 - n) * FX_FRAME_MSEC : first + n;
	}
}

function plForcePass(inst, cent, place) {
	const pass = cent.forcePass | 0, model = inst.model;
	if (!pass || cent.vehicle || !inst.pe || !inst.bones || !model) return;
	const t = g2AnimTables(), drain = pass > 3;
	const name = drain ? (cent.torsoAnim === t.BOTH_FORCE_DRAIN_GRAB_START ? PL_FORCE_FX.drainHand : pass - 3 > 2 ? PL_FORCE_FX.drainWide : PL_FORCE_FX.drain)
		: cent.eFlags & PG.EF_FLAME ? PL_FORCE_FX.flame : pass > 2 ? PL_FORCE_FX.lightningWide : PL_FORCE_FX.lightning;
	const tag = model.tags[drain && cent.torsoAnim === t.BOTH_FORCE_2HANDEDLIGHTNING_HOLD && Math.random() < 0.5 ? '*r_hand' : '*l_hand'];
	if (!tag) return;
	const hand = M4.mul(place, to44(tagMatrix(tag, inst.bones))), p = inst.pe.torso.pitchAngle * Math.PI / 180, y = inst.pe.torso.yawAngle * Math.PI / 180;
	plEveryFrame(name, cent.number, [hand[12], hand[13], hand[14]], [Math.cos(p) * Math.cos(y), Math.cos(p) * Math.sin(y), -Math.sin(p)]);
}

// one more sprite for this frame (a frame that never gets to draw them must not let them pile up)
function plAddSprite(s) {
	if (plSprites.length >= 64) plSprites.length = 0;
	plSprites.push(s);
}

// who looks: the player the page looks through the eyes of, null for a view of its own (a spectator of the game)
const plViewer = () => (view.id >= 0 && players.has(view.id) ? players.get(view.id).data : null);

// The sprite of his team above a player in a team game (CG_Player, cg_drawFriend): for one who watches above
// everybody, for a player above those of his own team. Not above the dead, not above NPCs.
function plTeamSprite(cent, isViewer) {
	if (!state || !(state.gametype >= PL_GT_TEAM) || isViewer || cent.npc || (cent.eFlags & PG.EF_DEAD)) return;
	const viewer = plViewer();
	if (viewer && viewer.team !== 'spectator' && viewer.team !== cent.team) return;
	// (a siege map can name sprites of its own for its teams: not read here)
	plFloatSprite(cent, cent.team === 'red' ? PL_MEDIA.teamRed : PL_MEDIA.teamBlue);
}

// CG_PlayerSprites: the sign of a broken connection, or the balloon of one who types
function plPlayerSprites(cent) {
	if (cent.eFlags & PG.EF_CONNECTION) { plFloatSprite(cent, PL_MEDIA.connection); return; }
	if (!cent.npc && (cent.eFlags & PG.EF_TALK)) plFloatSprite(cent, PL_MEDIA.balloon);
}

// CG_DrawPlayerSphere: the ball of a powerup around a player, a model that always shows the eye the same side.
// (The second ball the game draws into it bends what is behind it, RF_DISTORTION: not drawn here.)
function plPlayerSphere(cent, scale, shader) {
	if (cent.eFlags & PG.EF_DEAD) return;
	const model = staticNow(PL_MEDIA.halfShieldModel), o = cent.lerpOrigin, org = [o[0], o[1], o[2] + 9];
	const dir = [org[0] - eye[0], org[1] - eye[1], org[2] - eye[2]];
	if (!model || Math.hypot(dir[0], dir[1], dir[2]) <= 0.1) return;
	const ang = g2VecToAngles(dir);
	ang[2] += 180; ang[0] += 180;
	// JA+ client plugin: the sphere has the size of the model
	drawModelScripted(model.meshes, anglesMatrix(org, ang, scale * cent.scale), { shader });
}

// CG_PlayerFlag: the flag a player carries, on his back: at the bone of his lower spine, tilted against where he
// looks, half its size. JA+ draws a shell in the colour of the flag around it.
function plPlayerFlag(inst, cent, path, time) {
	const model = staticNow(path), bolt = g2BoltMatrix(inst, 'lower_lumbar', inst.turAngles, cent.lerpOrigin, cent.scale, time);
	if (!model || !bolt) return;
	const a = cent.angles, yaw = a[1] * Math.PI / 180, boltOrg = [bolt[3], bolt[7], bolt[11] - 12];
	// 8 to his right (AngleVectors of his yaw)
	boltOrg[0] += Math.sin(yaw) * 8; boltOrg[1] -= Math.cos(yaw) * 8;
	const angles = [-a[0] / 2 - 30, g2VecToAngles([bolt[0], bolt[4], bolt[8]])[1] + 270, a[2]];
	const fwd = g2AnglesToAxis(angles)[0], origin = [boltOrg[0] + fwd[0] * 24, boltOrg[1] + fwd[1] * 24, boltOrg[2] + fwd[2] * 24];
	angles[2] += 20;
	const place = anglesMatrix(origin, angles, 0.5);
	// the flag of the other team: one of the red team carries the blue one
	const red = cent.team === 'red';
	drawModelScripted(model.meshes, place, { rgba: [red ? 0 : 1, 0, red ? 1 : 0, 254 / 255], shader: PL_MEDIA.playerShieldDamage });
	drawModelScripted(model.meshes, place);
}

// CG_PlayerPowerups: what a powerup adds behind a player. (The lights of the quad and of the flags are lights of
// the scene, js/dlights.js: not added here.)
function plPlayerPowerups(inst, cent, time) {
	if (!cent.powerups) return;
	const cty = state && state.gametype === PL_GT_CTY;
	if (cent.powerups & (1 << PG.PW_REDFLAG)) plPlayerFlag(inst, cent, cty ? PL_MEDIA.redFlagYsal : PL_MEDIA.redFlag, time);
	if (cent.powerups & (1 << PG.PW_BLUEFLAG)) plPlayerFlag(inst, cent, cty ? PL_MEDIA.blueFlagYsal : PL_MEDIA.blueFlag, time);
}

// the seeker drone that circles the head of a player (EF_SEEKERDRONE in CG_Player)
function plSeeker(cent) {
	const model = staticNow(PL_MEDIA.seekerModel), o = cent.lerpOrigin;
	if (!model) return;
	const angle = ((Math.trunc(frameNow / 12)) & 255) * (Math.PI * 2) / 255;
	let yaw = angle * 180 / Math.PI + 90;
	if (yaw > 360) yaw -= 360;
	drawModelScripted(model.meshes, anglesMatrix([o[0] + Math.cos(angle) * 20, o[1] + Math.sin(angle) * 20, o[2] + 40 + Math.cos(angle) * 5], [Math.sin(angle) * 30, yaw, 0], 1));
}

// The model of a player or an NPC as CG_Player adds it to the scene, with what it adds around it. plain: the
// colour and how much of it the plain view of the editor lays over the model, which draws the model alone.
// ownView: the page looks out of his head, then only what is not his body is drawn.
function plDrawModel(inst, cent, place, d, ownView, plain) {
	// legs.radius (CG_RadiusForCent) is the ball that says which fog he is in, legs.shaderRGBA the colour of his entity
	const radius = 64, white = [1, 1, 1, 1], time = inst.lastTime, origin = cent.lerpOrigin;
	if (!modelsScripted()) {
		if (!ownView) g2Draw(inst, place, plain[0], plain[1], { rgba: white, radius });
		return;
	}
	const pw = cent.powerups, fp = cent.forceActive, viewer = plViewer(), isViewer = !cent.npc && view.id === cent.number;
	const shell = (shader, rgba, more) => drawModelScripted(inst.meshes, place, Object.assign({ shader, rgba, radius }, more));
	// what is his body is not drawn from inside his head (RF_THIRD_PERSON)
	const body = !ownView;

	// the half shield of one whose shield took a hit (CG_PlayerHitFX, set by EV_SHIELD_HIT in js/events.js)
	evPlayerHitFX(cent, ownView);
	if (body) plTeamSprite(cent, isViewer);
	if (body && (cent.eFlags & PG.EF_SEEKERDRONE) && !cent.npc) plSeeker(cent);

	// The trail of force speed (cg_speedTrail): the model again where it was a frame and two frames ago, pulled up
	// to a set distance behind him, fainter each time.
	if (!(pw & (1 << PG.PW_SPEED))) inst.trail1 = inst.trail2 = null;
	if (body && (inst.trail1 || inst.trail2)) {
		const dist = Math.trunc(PL_SPEED_TRAIL_DISTANCE * (Math.hypot(cent.vel[0], cent.vel[1], cent.vel[2]) * 0.004));
		const behind = (from, of) => { const v = [from[0] - of[0], from[1] - of[1], from[2] - of[2]], l = Math.hypot(v[0], v[1], v[2]) || 1; return [of[0] + v[0] / l * dist, of[1] + v[1] / l * dist, of[2] + v[2] / l * dist]; };
		const at = (pos, alpha) => { const m = place.slice(); m[12] = pos[0]; m[13] = pos[1]; m[14] = pos[2]; drawModelScripted(inst.meshes, m, { rgba: [1, 1, 1, alpha / 255], radius, forceAlpha: true }); };
		if (inst.trail1) { inst.trail1 = behind(inst.trail1, origin); at(inst.trail1, 100); }
		if (inst.trail2) { inst.trail2 = behind(inst.trail2, inst.trail1 || origin); at(inst.trail2, 50); }
	}

	if (body) plPlayerSprites(cent);

	// the balls of the powerups
	const cty = state && state.gametype === PL_GT_CTY;
	if ((pw & (1 << PG.PW_YSALAMIRI)) || (cty && (pw & ((1 << PG.PW_REDFLAG) | (1 << PG.PW_BLUEFLAG))))) {
		plPlayerSphere(cent, 1.4, cty && (pw & (1 << PG.PW_REDFLAG)) ? PL_MEDIA.ysalired : cty && (pw & (1 << PG.PW_BLUEFLAG)) ? PL_MEDIA.ysaliblue : PL_MEDIA.ysalimari);
	}
	if (pw & (1 << PG.PW_FORCE_BOON)) plPlayerSphere(cent, 2, PL_MEDIA.boon);
	if (pw & (1 << PG.PW_FORCE_ENLIGHTENED_DARK)) plPlayerSphere(cent, 2, PL_MEDIA.endarkenment);
	else if (pw & (1 << PG.PW_FORCE_ENLIGHTENED_LIGHT)) plPlayerSphere(cent, 2, PL_MEDIA.enlightenment);
	if (cent.eFlags & PG.EF_INVULNERABLE) plPlayerSphere(cent, 1, PL_MEDIA.invulnerability);

	// (one who is being disintegrated: CG_Disintegration, not drawn here, the model stays as it is)

	// One who looks with force sight sees the others in yellow (the light under which he sees them, RF_MINLIGHT, is not known to the model drawing here)
	const seen = !!viewer && !isViewer && !!((viewer.forceActive || 0) & (1 << PG.FP_SEE));
	const legs = { rgba: seen ? [1, 1, 0, 1] : white, radius };

	// Cloaked: for two seconds the model fades into the cloak shader and back. Fully cloaked the game draws him as
	// a bending of what is behind him (a render to a texture): here with the cloak shader, as the game does
	// where it can not do that.
	const cloakedNow = !!(pw & (1 << PG.PW_CLOAKED));
	if (cloakedNow !== !!inst.cloaked) { inst.cloaked = cloakedNow; if (inst.cloakSeen) inst.uncloaking = time + 2000; }
	inst.cloakSeen = true;
	if (body && inst.uncloaking > time) {
		if (seen) {
			drawModelScripted(inst.meshes, place, legs);
		} else {
			let perc = (inst.uncloaking - time) / 2000;
			if (cloakedNow) perc = 1 - perc;
			if (perc >= 0 && perc <= 1) {
				shell(PL_MEDIA.cloaked, [perc, perc, perc, 0], { tint: true });
				drawModelScripted(inst.meshes, place, { rgba: [1, 1, 1, 1 - perc], radius, forceAlpha: true });
			}
		}
	} else if (body && cloakedNow) {
		if (seen) drawModelScripted(inst.meshes, place, legs);
		else shell(PL_MEDIA.cloaked, white);
	}
	// the model itself, not of one who is cloaked
	if (body && !cloakedNow) drawModelScripted(inst.meshes, place, legs);

	// for the trail of the next frame
	inst.trail2 = inst.trail1;
	inst.trail1 = (pw & (1 << PG.PW_SPEED)) ? origin.slice() : null;

	// (the weapon in his hands: drawHeldWeapons of js/weapons.js, called by the one who draws him)
	if (!body) return;
	// the flag on his back (CG_PlayerFlag draws none from inside the head of the one who carries it)
	plPlayerPowerups(inst, cent, time);

	// a team power or a drain that has just reached him (teamPowerEffectTime, set by events): his model again in its colour
	const power = evTeamPower(cent);
	if (power) shell(power.shader, power.rgba, { tint: true, forceAlpha: true });
	// force rage: crackling red, one of two shaders by chance in every frame
	if (fp & (1 << PG.FP_RAGE)) shell(Math.random() < 0.5 ? PL_MEDIA.electricBody : PL_MEDIA.electricBody2, [1, 0, 0, 1], { tint: true });
	// force protect is green
	if (fp & (1 << PG.FP_PROTECT)) shell(PL_MEDIA.protect, [0, 128 / 255, 0, 254 / 255]);
	// force absorb is blue, and always shown only on the one who looks (on others when it took something up, an event)
	if (isViewer && (fp & (1 << PG.FP_ABSORB))) shell(PL_MEDIA.playerShieldDamage, [0, 0, 1, 254 / 255]);
	// the aura of the others for one who looks with force sight (cg_auraShell), in the colour of their team
	if (seen) {
		const team = state && state.gametype >= PL_GT_TEAM;
		let rgb = [1, 1, 0];
		if (team && state.gametype === PL_GT_SIEGE) rgb = cent.team !== 'red' && cent.team !== 'blue' ? [1, 1, 0] : cent.team !== viewer.team ? [1, 50 / 255, 50 / 255] : [50 / 255, 1, 50 / 255];
		else if (team) rgb = cent.team === 'red' ? [1, 50 / 255, 50 / 255] : cent.team === 'blue' ? [75 / 255, 75 / 255, 1] : [1, 1, 0];
		// (through walls from the second level of force sight on, RF_NODEPTH: not known to the model drawing here)
		shell(PL_MEDIA.sightShell, [rgb[0], rgb[1], rgb[2], 1]);
	}
	// Electricity (CG_Player, emplacedOwner): while he is electrified (force lightning, a DEMP 2, the stun baton,
	// the freeze of the JA+ admins) his model is drawn again in six of ten frames with one of the two electric
	// shaders, white, fainter in the last half second (the byte the game makes of that number wraps: 255 down to
	// 1). The msec he has left are the ones of the last poll less the time of the game since then.
	const dif = (cent.electrify || 0) - (state ? time - (state.time || 0) : 0);
	if (dif > 0 && (refCam || Math.random() > 0.4)) {
		const v = (dif < 500 ? Math.floor((dif - 500) / 500 * 255) & 255 : 255) / 255;
		shell(Math.random() < 0.5 ? PL_MEDIA.electricBody : PL_MEDIA.electricBody2, [v, v, v, 1], { tint: true });
	}
	if (dif > 0) plElectrocution(inst, cent, place);
	// his shield took a hit: a flicker in a grey of chance
	if (pw & (1 << PG.PW_SHIELDHIT)) { const v = (1 + Math.floor(Math.random() * 255)) / 255; shell(PL_MEDIA.playerShieldDamage, [v, v, v, 1]); }
}

// the buffer of the sprites: four vertices each, a place, texture coordinates and a colour as fx-draw.js has them
let plSpriteGl = null;
function plSpriteBuffers() {
	if (plSpriteGl) return plSpriteGl;
	const max = 64, stride = 24, data = new ArrayBuffer(max * 4 * stride), vao = gl.createVertexArray(), vbo = gl.createBuffer();
	gl.bindVertexArray(vao);
	gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
	gl.bufferData(gl.ARRAY_BUFFER, data.byteLength, gl.DYNAMIC_DRAW);
	gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, stride, 0);
	gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 2, gl.FLOAT, false, stride, 12);
	gl.enableVertexAttribArray(3); gl.vertexAttribPointer(3, 4, gl.UNSIGNED_BYTE, true, stride, 20);
	gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, gl.createBuffer());
	gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, Uint16Array.from({ length: max * 6 }, (_, i) => (i / 6 | 0) * 4 + [0, 1, 2, 2, 1, 3][i % 6]), gl.STATIC_DRAW);
	gl.bindVertexArray(null);
	return plSpriteGl = { max, stride, data, floats: new Float32Array(data), bytes: new Uint8Array(data), vao, vbo };
}

// After everything see-through of the map: the sprites above the players of this frame (RB_SurfaceSprite: a quad
// that faces the eye, its radius to each side, with the stages of its shader). In the fog its place is in.
function drawPlayerSprites(now) {
	if (!plSprites.length) return;
	const list = plSprites.splice(0), b = plSpriteBuffers(), view3 = cameraAxes(), t = frameNow / 1000;
	let n = 0;
	for (const s of list) {
		if (n >= b.max) break;
		const r = s.radius, l = view3.left, u = view3.up, o = s.org;
		// RB_AddQuadStamp
		[[1, 1, 0, 0], [-1, 1, 1, 0], [1, -1, 0, 1], [-1, -1, 1, 1]].forEach(([sl, su, tu, tv], k) => {
			const f = (n * 4 + k) * 6, c = (n * 4 + k) * b.stride + 20;
			for (let i = 0; i < 3; i++) b.floats[f + i] = o[i] + l[i] * r * sl + u[i] * r * su;
			b.floats[f + 3] = tu; b.floats[f + 4] = tv;
			for (let i = 0; i < 4; i++) b.bytes[c + i] = s.rgba ? s.rgba[i] : 255;
		});
		s.first = n++;
	}
	const program = gl.getParameter(gl.CURRENT_PROGRAM);
	gl.useProgram(stageProg.p);
	gl.uniform1i(stageProg.u.uTex, 0);
	gl.uniform3fv(stageProg.u.uEye, eye);
	gl.uniformMatrix4fv(stageProg.u.uMvp, false, viewProj);
	gl.uniform1i(stageProg.u.uSprite, 0);
	gl.activeTexture(gl.TEXTURE0);
	gl.colorMask(true, true, true, false);
	gl.bindBuffer(gl.ARRAY_BUFFER, b.vbo);
	gl.bufferSubData(gl.ARRAY_BUFFER, 0, b.bytes, 0, n * 4 * b.stride);
	for (const s of list) {
		if (s.first === undefined) continue;
		// a shader without a script is one of the 2D kind here (R_RegisterShaderNoMip): the colours of its vertices, blended
		const mat = materialFor(s.shader, 'fx');
		if (mat.nodraw) continue;
		const fog = fogShown() && world && world.fogInfo.length ? fogAt(s.org, s.radius) : null;
		setCull(mat.cull);
		setDeform(stageProg, mat, t);
		for (const st of mat.stages) drawStage(st, t, null, b.vao, 6, gl.UNSIGNED_SHORT, s.first * 12, stageProg, fog);
	}
	setDeform(stageProg, NO_DEFORM, 0);
	gl.bindVertexArray(null);
	gl.disable(gl.CULL_FACE);
	gl.disable(gl.BLEND);
	gl.depthMask(true);
	gl.depthFunc(gl.LESS);
	gl.colorMask(true, true, true, true);
	gl.useProgram(program);
}
