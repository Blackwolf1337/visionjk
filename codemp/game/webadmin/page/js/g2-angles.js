// ---- how a player model is turned (BG_G2PlayerAngles in bg_pmove.c, called by CG_G2PlayerAngles in cg_players.c):
// the legs follow where he moves, the spine takes up what is between them and where he looks, neck and head too
//
// The model stands by the angles of its legs. Everything above is turned by bone angles that Ghoul2 lays over
// the animation (g2SetBoneAngles): lower_lumbar, upper_lumbar, thoracic, cervical, cranium.

// The checks of the game that say what kind of animation one is (bg_panimate.c, bg_pmove.c), by the names of
// anims.h. Made from the switch statements of those functions.
const G2_ANIM_LISTS = {
	// BG_FlippingAnim
	flipping: 'BOTH_FJSS_TR_BL BOTH_ALORA_SPIN_SLASH BOTH_FLIP_F BOTH_FLIP_B BOTH_FLIP_L BOTH_FLIP_R BOTH_WALL_RUN_RIGHT_FLIP BOTH_WALL_RUN_LEFT_FLIP ' +
		'BOTH_WALL_FLIP_RIGHT BOTH_WALL_FLIP_LEFT BOTH_FLIP_BACK1 BOTH_FLIP_BACK2 BOTH_FLIP_BACK3 BOTH_WALL_FLIP_BACK1 BOTH_WALL_RUN_RIGHT BOTH_WALL_RUN_LEFT ' +
		'BOTH_WALL_RUN_RIGHT_STOP BOTH_WALL_RUN_LEFT_STOP BOTH_BUTTERFLY_LEFT BOTH_BUTTERFLY_RIGHT BOTH_BUTTERFLY_FL1 BOTH_BUTTERFLY_FR1 BOTH_ARIAL_LEFT ' +
		'BOTH_ARIAL_RIGHT BOTH_ARIAL_F1 BOTH_CARTWHEEL_LEFT BOTH_CARTWHEEL_RIGHT BOTH_JUMPFLIPSLASHDOWN1 BOTH_JUMPFLIPSTABDOWN BOTH_JUMPATTACK6 ' +
		'BOTH_JUMPATTACK7 BOTH_FORCEWALLRUNFLIP_END BOTH_FORCEWALLRUNFLIP_ALT BOTH_FLIP_ATTACK7 BOTH_A7_SOULCAL',
	// BG_SpinningSaberAnim
	spinningSaber: 'BOTH_ALORA_SPIN_SLASH BOTH_T1_BR_BL BOTH_T1__R__L BOTH_T1__R_BL BOTH_T1_TR_BL BOTH_T1_BR_TL BOTH_T1_BR__L BOTH_T1_TL_BR BOTH_T1__L_BR BOTH_T1__L__R ' +
		'BOTH_T1_BL_BR BOTH_T1_BL__R BOTH_T1_BL_TR BOTH_T2_BR__L BOTH_T2_BR_BL BOTH_T2__R_BL BOTH_T2__L_BR BOTH_T2_BL_BR BOTH_T2_BL__R BOTH_T3_BR__L ' +
		'BOTH_T3_BR_BL BOTH_T3__R_BL BOTH_T3__L_BR BOTH_T3_BL_BR BOTH_T3_BL__R BOTH_T4_BR__L BOTH_T4_BR_BL BOTH_T4__R_BL BOTH_T4__L_BR BOTH_T4_BL_BR ' +
		'BOTH_T4_BL__R BOTH_T5_BR_BL BOTH_T5__R__L BOTH_T5__R_BL BOTH_T5_TR_BL BOTH_T5_BR_TL BOTH_T5_BR__L BOTH_T5_TL_BR BOTH_T5__L_BR BOTH_T5__L__R ' +
		'BOTH_T5_BL_BR BOTH_T5_BL__R BOTH_T5_BL_TR BOTH_T6_BR_TL BOTH_T6__R_TL BOTH_T6__R__L BOTH_T6__R_BL BOTH_T6_TR_TL BOTH_T6_TR__L BOTH_T6_TR_BL ' +
		'BOTH_T6_T__TL BOTH_T6_T__BL BOTH_T6_TL_BR BOTH_T6__L_BR BOTH_T6__L__R BOTH_T6_TL__R BOTH_T6_TL_TR BOTH_T6__L_TR BOTH_T6__L_T_ BOTH_T6_BL_T_ ' +
		'BOTH_T6_BR__L BOTH_T6_BR_BL BOTH_T6_BL_BR BOTH_T6_BL__R BOTH_T6_BL_TR BOTH_T7_BR_TL BOTH_T7_BR__L BOTH_T7_BR_BL BOTH_T7__R__L BOTH_T7__R_BL ' +
		'BOTH_T7_TR__L BOTH_T7_T___R BOTH_T7_TL_BR BOTH_T7__L_BR BOTH_T7__L__R BOTH_T7_BL_BR BOTH_T7_BL__R BOTH_T7_BL_TR BOTH_T7_TL_TR BOTH_T7_T__BR ' +
		'BOTH_T7__L_TR BOTH_V7_BL_S7 BOTH_A2_STABBACK1 BOTH_ATTACK_BACK BOTH_CROUCHATTACKBACK1 BOTH_BUTTERFLY_LEFT BOTH_BUTTERFLY_RIGHT BOTH_FJSS_TR_BL ' +
		'BOTH_FJSS_TL_BR BOTH_JUMPFLIPSLASHDOWN1 BOTH_JUMPFLIPSTABDOWN',
	// BG_InSpecialJump
	specialJump: 'BOTH_ALORA_SPIN_SLASH BOTH_JUMP_BACKKICK_SPIN BOTH_JUMP_BACKFLIP_ATCK BOTH_FLIP_STAB BOTH_WALL_RUN_RIGHT BOTH_WALL_RUN_RIGHT_STOP ' +
		'BOTH_WALL_RUN_RIGHT_FLIP BOTH_WALL_RUN_LEFT BOTH_WALL_RUN_LEFT_STOP BOTH_WALL_RUN_LEFT_FLIP BOTH_WALL_FLIP_RIGHT BOTH_WALL_FLIP_LEFT BOTH_FLIP_BACK1 ' +
		'BOTH_FLIP_BACK2 BOTH_FLIP_BACK3 BOTH_WALL_FLIP_BACK1 BOTH_BUTTERFLY_LEFT BOTH_BUTTERFLY_RIGHT BOTH_BUTTERFLY_FL1 BOTH_BUTTERFLY_FR1 BOTH_FJSS_TR_BL ' +
		'BOTH_FJSS_TL_BR BOTH_FORCELEAP2_T__B_ BOTH_JUMPFLIPSLASHDOWN1 BOTH_JUMPFLIPSTABDOWN BOTH_JUMPATTACK6 BOTH_JUMPATTACK7 BOTH_ARIAL_LEFT BOTH_ARIAL_RIGHT ' +
		'BOTH_ARIAL_F1 BOTH_CARTWHEEL_LEFT BOTH_CARTWHEEL_RIGHT BOTH_FORCELONGLEAP_START BOTH_FORCELONGLEAP_ATTACK BOTH_FORCEWALLRUNFLIP_START ' +
		'BOTH_FORCEWALLRUNFLIP_END BOTH_FORCEWALLRUNFLIP_ALT BOTH_FLIP_ATTACK7 BOTH_FLIP_HOLD7 BOTH_FLIP_LAND BOTH_A7_SOULCAL',
	// BG_InDeathAnim
	death: 'BOTH_DEATH1 BOTH_DEATH2 BOTH_DEATH3 BOTH_DEATH4 BOTH_DEATH5 BOTH_DEATH6 BOTH_DEATH7 BOTH_DEATH8 BOTH_DEATH9 BOTH_DEATH10 BOTH_DEATH11 BOTH_DEATH12 ' +
		'BOTH_DEATH13 BOTH_DEATH14 BOTH_DEATH14_UNGRIP BOTH_DEATH14_SITUP BOTH_DEATH15 BOTH_DEATH16 BOTH_DEATH17 BOTH_DEATH18 BOTH_DEATH19 BOTH_DEATH20 ' +
		'BOTH_DEATH21 BOTH_DEATH22 BOTH_DEATH23 BOTH_DEATH24 BOTH_DEATH25 BOTH_DEATHFORWARD1 BOTH_DEATHFORWARD2 BOTH_DEATHFORWARD3 BOTH_DEATHBACKWARD1 ' +
		'BOTH_DEATHBACKWARD2 BOTH_DEATH1IDLE BOTH_LYINGDEATH1 BOTH_STUMBLEDEATH1 BOTH_FALLDEATH1 BOTH_FALLDEATH1INAIR BOTH_FALLDEATH1LAND BOTH_DEAD1 BOTH_DEAD2 ' +
		'BOTH_DEAD3 BOTH_DEAD4 BOTH_DEAD5 BOTH_DEAD6 BOTH_DEAD7 BOTH_DEAD8 BOTH_DEAD9 BOTH_DEAD10 BOTH_DEAD11 BOTH_DEAD12 BOTH_DEAD13 BOTH_DEAD14 BOTH_DEAD15 ' +
		'BOTH_DEAD16 BOTH_DEAD17 BOTH_DEAD18 BOTH_DEAD19 BOTH_DEAD20 BOTH_DEAD21 BOTH_DEAD22 BOTH_DEAD23 BOTH_DEAD24 BOTH_DEAD25 BOTH_DEADFORWARD1 ' +
		'BOTH_DEADFORWARD2 BOTH_DEADBACKWARD1 BOTH_DEADBACKWARD2 BOTH_LYINGDEAD1 BOTH_STUMBLEDEAD1 BOTH_FALLDEAD1LAND BOTH_DEADFLOP1 BOTH_DEADFLOP2 ' +
		'BOTH_DISMEMBER_HEAD1 BOTH_DISMEMBER_TORSO1 BOTH_DISMEMBER_LLEG BOTH_DISMEMBER_RLEG BOTH_DISMEMBER_RARM BOTH_DISMEMBER_LARM',
	// BG_InRollAnim
	roll: 'BOTH_ROLL_F BOTH_ROLL_B BOTH_ROLL_R BOTH_ROLL_L',
	// BG_SaberInSpecialAttack
	saberSpecialAttack: 'BOTH_MELEE_BACKKICK BOTH_MELEE_SPINKICK BOTH_BACK_FALLING_GETUP BOTH_JUMP_BACKKICK_SPIN BOTH_JUMP_BACKFLIP_ATCK BOTH_FLIP_STAB BOTH_A2_STABBACK1 ' +
		'BOTH_ATTACK_BACK BOTH_CROUCHATTACKBACK1 BOTH_ROLL_STAB BOTH_BUTTERFLY_LEFT BOTH_BUTTERFLY_RIGHT BOTH_BUTTERFLY_FL1 BOTH_BUTTERFLY_FR1 BOTH_FJSS_TR_BL ' +
		'BOTH_FJSS_TL_BR BOTH_LUNGE2_B__T_ BOTH_FORCELEAP2_T__B_ BOTH_JUMPFLIPSLASHDOWN1 BOTH_JUMPFLIPSTABDOWN BOTH_JUMPATTACK6 BOTH_JUMPATTACK7 ' +
		'BOTH_SPINATTACK6 BOTH_SPINATTACK7 BOTH_FORCELONGLEAP_ATTACK BOTH_VS_ATR_S BOTH_VS_ATL_S BOTH_VT_ATR_S BOTH_VT_ATL_S BOTH_A7_KICK_F BOTH_A7_KICK_B ' +
		'BOTH_A7_KICK_R BOTH_A7_KICK_L BOTH_A7_KICK_S BOTH_A7_KICK_BF BOTH_A7_KICK_RL BOTH_A7_KICK_F_AIR BOTH_A7_KICK_B_AIR BOTH_A7_KICK_R_AIR ' +
		'BOTH_A7_KICK_L_AIR BOTH_STABDOWN BOTH_STABDOWN_STAFF BOTH_STABDOWN_DUAL BOTH_A6_SABERPROTECT BOTH_A7_SOULCAL BOTH_A1_SPECIAL BOTH_A2_SPECIAL ' +
		'BOTH_A3_SPECIAL BOTH_FLIP_ATTACK7 BOTH_PULL_IMPALE_STAB BOTH_PULL_IMPALE_SWING BOTH_ALORA_SPIN_SLASH BOTH_A6_FB BOTH_A6_LR BOTH_A7_HILT',
	// BG_InKnockDown
	knockDown: 'BOTH_BACK_FALLING BOTH_BACK_FALLING_GETUP BOTH_JUMP_BACKFLIP_ATCKEE_FALL BOTH_KNOCKDOWN1 BOTH_KNOCKDOWN2 BOTH_KNOCKDOWN3 BOTH_KNOCKDOWN4 ' +
		'BOTH_KNOCKDOWN5 BOTH_GETUP1 BOTH_GETUP2 BOTH_GETUP3 BOTH_GETUP4 BOTH_GETUP5 BOTH_FORCE_GETUP_F1 BOTH_FORCE_GETUP_F2 BOTH_FORCE_GETUP_B1 ' +
		'BOTH_FORCE_GETUP_B2 BOTH_FORCE_GETUP_B3 BOTH_FORCE_GETUP_B4 BOTH_FORCE_GETUP_B5 BOTH_GETUP_BROLL_B BOTH_GETUP_BROLL_F BOTH_GETUP_BROLL_L ' +
		'BOTH_GETUP_BROLL_R BOTH_GETUP_FROLL_B BOTH_GETUP_FROLL_F BOTH_GETUP_FROLL_L BOTH_GETUP_FROLL_R',
	// BG_SaberLockBreakAnim
	lockBreak: 'BOTH_BF1BREAK BOTH_BF2BREAK BOTH_CWCIRCLEBREAK BOTH_CCWCIRCLEBREAK BOTH_LK_S_DL_S_B_1_L BOTH_LK_S_DL_S_B_1_W BOTH_LK_S_DL_T_B_1_L BOTH_LK_S_DL_T_B_1_W ' +
		'BOTH_LK_S_ST_S_B_1_L BOTH_LK_S_ST_S_B_1_W BOTH_LK_S_ST_T_B_1_L BOTH_LK_S_ST_T_B_1_W BOTH_LK_S_S_S_B_1_L BOTH_LK_S_S_S_B_1_W BOTH_LK_S_S_T_B_1_L ' +
		'BOTH_LK_S_S_T_B_1_W BOTH_LK_DL_DL_S_B_1_L BOTH_LK_DL_DL_S_B_1_W BOTH_LK_DL_DL_T_B_1_L BOTH_LK_DL_DL_T_B_1_W BOTH_LK_DL_ST_S_B_1_L ' +
		'BOTH_LK_DL_ST_S_B_1_W BOTH_LK_DL_ST_T_B_1_L BOTH_LK_DL_ST_T_B_1_W BOTH_LK_DL_S_S_B_1_L BOTH_LK_DL_S_S_B_1_W BOTH_LK_DL_S_T_B_1_L BOTH_LK_DL_S_T_B_1_W ' +
		'BOTH_LK_ST_DL_S_B_1_L BOTH_LK_ST_DL_S_B_1_W BOTH_LK_ST_DL_T_B_1_L BOTH_LK_ST_DL_T_B_1_W BOTH_LK_ST_ST_S_B_1_L BOTH_LK_ST_ST_S_B_1_W ' +
		'BOTH_LK_ST_ST_T_B_1_L BOTH_LK_ST_ST_T_B_1_W BOTH_LK_ST_S_S_B_1_L BOTH_LK_ST_S_S_B_1_W BOTH_LK_ST_S_T_B_1_L BOTH_LK_ST_S_T_B_1_W',
	// PM_RunningAnim
	running: 'BOTH_RUN1 BOTH_RUN2 BOTH_RUN_STAFF BOTH_RUN_DUAL BOTH_RUNBACK1 BOTH_RUNBACK2 BOTH_RUNBACK_STAFF BOTH_RUNBACK_DUAL BOTH_RUN1START BOTH_RUN1STOP ' +
		'BOTH_RUNSTRAFE_LEFT1 BOTH_RUNSTRAFE_RIGHT1',
	// PM_WalkingAnim
	walking: 'BOTH_WALK1 BOTH_WALK2 BOTH_WALK_STAFF BOTH_WALK_DUAL BOTH_WALK5 BOTH_WALK6 BOTH_WALK7 BOTH_WALKBACK1 BOTH_WALKBACK2 BOTH_WALKBACK_STAFF ' +
		'BOTH_WALKBACK_DUAL',
	// BG_InRoll2
	roll2: 'BOTH_GETUP_BROLL_B BOTH_GETUP_BROLL_F BOTH_GETUP_BROLL_L BOTH_GETUP_BROLL_R BOTH_GETUP_FROLL_B BOTH_GETUP_FROLL_F BOTH_GETUP_FROLL_L ' +
		'BOTH_GETUP_FROLL_R BOTH_ROLL_F BOTH_ROLL_B BOTH_ROLL_R BOTH_ROLL_L',
};
// BG_SaberInSpecial: the saber moves (saberMoveName_t) that are special attacks, LS_A_BACKSTAB to LS_HILT_BASH
// and the six of JA+ after LS_REFLECT_LL
const g2SaberInSpecial = move => (move >= 11 && move <= 61) || (move >= 162 && move <= 167);
// WeaponReadyAnim of bg_misc.c: the animation a torso stands in with each weapon
const G2_WEAPON_READY_ANIM = ['TORSO_DROPWEAP1', 'TORSO_WEAPONREADY3', 'TORSO_WEAPONREADY3', 'BOTH_STAND2', 'TORSO_WEAPONREADY2', 'TORSO_WEAPONREADY3', 'TORSO_WEAPONREADY3',
	'TORSO_WEAPONREADY3', 'TORSO_WEAPONREADY3', 'TORSO_WEAPONREADY3', 'TORSO_WEAPONREADY3', 'TORSO_WEAPONREADY3', 'TORSO_WEAPONREADY10', 'TORSO_WEAPONREADY10',
	'TORSO_WEAPONREADY10', 'TORSO_WEAPONREADY3', 'TORSO_WEAPONREADY2', 'BOTH_STAND1', 'TORSO_WEAPONREADY1'];
// The numbers of the game the player code checks (bg_public.h, q_shared.h, teams.h, bg_vehicles.h, bg_weapons.h):
// flags of an entity, powerups, force powers, the kinds of a vehicle
const PG = Object.freeze({
	EF_DEAD: 1 << 1, EF_NODRAW: 1 << 8, EF_TALK: 1 << 13, EF_CONNECTION: 1 << 14, EF_BODYPUSH: 1 << 19, EF_SEEKERDRONE: 1 << 21, EF_DISINTEGRATION: 1 << 26, EF_INVULNERABLE: 1 << 27,
	EF2_HYPERSPACE: 1 << 5, EF2_SHIP_DEATH: 1 << 7,
	PW_QUAD: 1, PW_BATTLESUIT: 2, PW_PULL: 3, PW_REDFLAG: 4, PW_BLUEFLAG: 5, PW_NEUTRALFLAG: 6, PW_SHIELDHIT: 7, PW_SPEEDBURST: 8, PW_DISINT_4: 9, PW_SPEED: 10, PW_CLOAKED: 11,
	PW_FORCE_ENLIGHTENED_LIGHT: 12, PW_FORCE_ENLIGHTENED_DARK: 13, PW_FORCE_BOON: 14, PW_YSALAMIRI: 15,
	FP_SPEED: 2, FP_GRIP: 6, FP_RAGE: 8, FP_PROTECT: 9, FP_ABSORB: 10, FP_SEE: 14,
	ENTITYNUM_NONE: 1023, ENTITYNUM_WORLD: 1022, MAX_CLIENTS: 32, WP_SABER: 3, CLASS_VEHICLE: 53,
	VH_WALKER: 1, VH_FIGHTER: 2, VH_SPEEDER: 3, VH_ANIMAL: 4
});

// The lists as sets of animation numbers, and the numbers of the animations the code names, once the names of
// the animations are read (api/anims).
let g2Tables = null;
function g2AnimTables() {
	if (g2Tables || !g2.animList) return g2Tables;
	const num = new Map(g2.animList.map((n, i) => [n, i])), t = { num };
	for (const [key, names] of Object.entries(G2_ANIM_LISTS)) t[key] = new Set(names.split(' ').map(n => num.get(n)).filter(v => v !== undefined));
	t.weaponReady = G2_WEAPON_READY_ANIM.map(n => num.get(n));
	const n = name => num.has(name) ? num.get(name) : -1;
	t.BOTH_STAND1 = n('BOTH_STAND1');
	// the ranges the game checks: saber attacks and transitions (BG_SaberStartTransAnim, PM_InSaberAnim), the
	// moves on a ledge of JA+ (BG_InLedgeMove)
	t.saberFirst = n('BOTH_A1_T__B_'); t.rollStab = n('BOTH_ROLL_STAB'); t.saberLast = n('BOTH_H1_S1_BR');
	t.trans = [['BOTH_T1_BR__R', 'BOTH_T1_BL_TL'], ['BOTH_T2_BR__R', 'BOTH_T2_BL_TL'], ['BOTH_T3_BR__R', 'BOTH_T3_BL_TL']].map(r => [n(r[0]), n(r[1])]);
	t.ledgeFirst = n('BOTH_LEDGE_GRAB'); t.ledgeLast = n('BOTH_LEDGE_MERCPULL');
	// the legs of one who goes backwards (PM_Footsteps with PMF_BACKWARDS_RUN)
	t.back = new Set(['BOTH_RUNBACK1', 'BOTH_RUNBACK2', 'BOTH_RUNBACK_STAFF', 'BOTH_RUNBACK_DUAL', 'BOTH_WALKBACK1', 'BOTH_WALKBACK2', 'BOTH_WALKBACK_STAFF', 'BOTH_WALKBACK_DUAL'].map(n).filter(v => v >= 0));
	return g2Tables = t;
}
const g2InLedgeMove = (t, anim) => t.ledgeFirst >= 0 && anim >= t.ledgeFirst && anim <= t.ledgeLast;

// the angles of q_math.c
const g2Angle360 = a => (360 / 65536) * (Math.trunc(a * (65536 / 360)) & 65535);
const g2AngleMod = g2Angle360;
const g2Angle180 = a => { a = g2Angle360(a); return a > 180 ? a - 360 : a; };
const g2AngleDelta = (a, b) => g2Angle180(a - b);
function g2AngleSubtract(a1, a2) {
	let a = (a1 - a2) % 360;
	while (a > 180) a -= 360;
	while (a < -180) a += 360;
	return a;
}
function g2VecToAngles(v) {
	let yaw, pitch;
	if (v[1] === 0 && v[0] === 0) {
		yaw = 0;
		pitch = v[2] > 0 ? 90 : 270;
	} else {
		yaw = v[0] ? Math.atan2(v[1], v[0]) * 180 / Math.PI : v[1] > 0 ? 90 : 270;
		if (yaw < 0) yaw += 360;
		pitch = Math.atan2(v[2], Math.hypot(v[0], v[1])) * 180 / Math.PI;
		if (pitch < 0) pitch += 360;
	}
	return [-pitch, yaw, 0];
}

// BG_SwingAngles: an angle swings to where it should be, faster the further away it is, and is never left more
// than clampTolerance behind. part[angle] is the angle, part[swinging] whether it swings.
function g2SwingAngles(destination, swingTolerance, clampTolerance, speed, part, angle, swinging, frametime) {
	let swing, move, scale;
	if (!part[swinging]) {
		// see if a swing should be started
		swing = g2AngleSubtract(part[angle], destination);
		if (swing > swingTolerance || swing < -swingTolerance) part[swinging] = true;
	}
	if (!part[swinging]) return 0;
	// the speed by how far it is, so that it does not seem so linear
	swing = g2AngleSubtract(destination, part[angle]);
	scale = Math.abs(swing);
	if (scale < swingTolerance * 0.5) scale = 0.5;
	else if (scale < swingTolerance) scale = 1.0;
	else scale = 2.0;
	if (swing >= 0) {
		move = frametime * scale * speed;
		if (move >= swing) { move = swing; part[swinging] = false; }
		part[angle] = g2AngleMod(part[angle] + move);
	} else {
		move = frametime * scale * -speed;
		if (move <= swing) { move = swing; part[swinging] = false; }
		part[angle] = g2AngleMod(part[angle] + move);
	}
	// clamp to no more than tolerance
	swing = g2AngleSubtract(destination, part[angle]);
	if (swing > clampTolerance) part[angle] = g2AngleMod(destination - (clampTolerance - 1));
	else if (swing < -clampTolerance) part[angle] = g2AngleMod(destination + (clampTolerance - 1));
	return swing;
}

// BG_UpdateLookAngles: the angles the head looks by, held within what a neck can do and brought there slowly
// while he looks at something (lookingDebounceTime)
function g2UpdateLookAngles(lookingDebounceTime, lastHeadAngles, time, lookAngles, lookSpeed, minPitch, maxPitch, minYaw, maxYaw, minRoll, maxRoll) {
	const fFrameInter = 0.1;
	if (lookingDebounceTime > time) {
		lookAngles[0] = Math.max(minPitch, Math.min(maxPitch, lookAngles[0]));
		lookAngles[1] = Math.max(minYaw, Math.min(maxYaw, lookAngles[1]));
		lookAngles[2] = Math.max(minRoll, Math.min(maxRoll, lookAngles[2]));
		const old = lastHeadAngles.slice(), diff = [0, 1, 2].map(i => g2Angle180(lookAngles[i] - old[i]));
		if (diff[0] || diff[1] || diff[2]) for (let i = 0; i < 3; i++) lookAngles[i] = g2Angle180(old[i] + diff[i] * fFrameInter * lookSpeed);
	}
	for (let i = 0; i < 3; i++) lastHeadAngles[i] = lookAngles[i];
}

// the bone angles of the game: every one of them laid over the animation, around the axes of a humanoid bone
const g2SetHumanBoneAngles = (inst, bone, angles, time) => g2SetBoneAngles(inst, bone, angles, BONE_ANGLES_POSTMULT, G2_POSITIVE_X, G2_NEGATIVE_Y, G2_NEGATIVE_Z, 0, time);

// BG_G2ClientNeckAngles: what the head looks by, split between the chest, the neck and the head
function g2ClientNeckAngles(inst, time, lookAngles, thoracicAngles, legsAnim, t) {
	const headClampMinAngles = [-25, -55, -10], headClampMaxAngles = [50, 50, 10];
	const ledge = g2InLedgeMove(t, legsAnim);		// JA+: hanging on a ledge the body does not follow the view
	const lA = lookAngles.map((v, i) => Math.max(headClampMinAngles[i], Math.min(headClampMaxAngles[i], v)));
	if (ledge) thoracicAngles[0] = 0;
	else if (thoracicAngles[0]) thoracicAngles[0] = (thoracicAngles[0] + lA[0] * 0.4) * 0.5;		// set by the spine already: both
	else thoracicAngles[0] = lA[0] * 0.4;
	thoracicAngles[1] = thoracicAngles[1] ? (thoracicAngles[1] + lA[1] * 0.1) * 0.5 : lA[1] * 0.1;
	thoracicAngles[2] = thoracicAngles[2] ? (thoracicAngles[2] + lA[2] * 0.1) * 0.5 : lA[2] * 0.1;
	let neckAngles = [lA[0] * 0.2, lA[1] * 0.3, lA[2] * 0.3], headAngles = [lA[0] * 0.4, lA[1] * 0.6, lA[2] * 0.6];
	if (ledge) { neckAngles = [0, 0, 0]; headAngles = [0, 0, 0]; }
	g2SetHumanBoneAngles(inst, 'cranium', headAngles, time);
	g2SetHumanBoneAngles(inst, 'cervical', neckAngles, time);
	g2SetHumanBoneAngles(inst, 'thoracic', thoracicAngles, time);
}

// BG_G2ClientSpineAngles: the turn between the legs and the view, split up the spine. When legs and torso are
// in different animations the turn the torso animation has of its own (the bone "Motion") is taken out first.
function g2ClientSpineAngles(inst, cent, time, viewAngles, angles, thoracicAngles, ulAngles, llAngles, t) {
	const ciLegs = inst.ci.legsAnim, ciTorso = inst.ci.torsoAnim, legs = cent.legsAnim, torso = cent.torsoAnim;
	viewAngles[1] = g2AngleDelta(cent.angles[1], angles[1]);
	const doCorr = !t.flipping.has(legs) && !t.spinningSaber.has(legs) && !t.spinningSaber.has(torso) && !t.specialJump.has(legs) && !t.specialJump.has(torso)
		&& !t.death.has(legs) && !t.death.has(torso) && !t.roll.has(legs) && !g2SaberInSpecial(cent.saberMove) && !t.saberSpecialAttack.has(torso) && !t.saberSpecialAttack.has(legs)
		&& !t.knockDown.has(torso) && !t.knockDown.has(legs) && !t.knockDown.has(ciTorso) && !t.knockDown.has(ciLegs)
		&& !t.flipping.has(ciLegs) && !t.spinningSaber.has(ciLegs) && !t.spinningSaber.has(ciTorso) && !t.specialJump.has(ciLegs) && !t.specialJump.has(ciTorso)
		&& !t.death.has(ciLegs) && !t.death.has(ciTorso) && !t.saberSpecialAttack.has(ciTorso) && !t.saberSpecialAttack.has(ciLegs)
		&& !(cent.eFlags & PG.EF_DEAD) && legs !== torso && ciLegs !== ciTorso && !cent.vehicleNum;
	if (doCorr) {
		// The bolt on the bone Motion, with no angles of the entity (G2API_GetBoltMatrix_NoRecNoRot): its rows
		// made unit long as G2API_GetBoltMatrix does. Which way it looks is its second column, its right the first.
		const m = g2BoneBolt(inst, 'Motion', time, true);
		if (m) {
			for (let r = 0; r < 3; r++) { const l = Math.hypot(m[r * 4], m[r * 4 + 1], m[r * 4 + 2]) || 1; m[r * 4] /= l; m[r * 4 + 1] /= l; m[r * 4 + 2] /= l; }
			const motionAngles = g2VecToAngles([-m[1], -m[5], -m[9]]);
			motionAngles[2] = -g2VecToAngles([-m[0], -m[4], -m[8]])[0];
			for (let i = 0; i < 3; i++) viewAngles[i] = g2Angle180(viewAngles[i] - g2Angle180(motionAngles[i]));
		}
	}
	// each of these distributions adds up to 1
	thoracicAngles[0] = viewAngles[0] * 0.20;
	llAngles[0] = viewAngles[0] * 0.40;
	ulAngles[0] = viewAngles[0] * 0.40;
	if (g2InLedgeMove(t, legs)) thoracicAngles[0] = llAngles[0] = ulAngles[0] = 0;		// JA+: no bending while hanging on a ledge
	thoracicAngles[1] = viewAngles[1] * 0.20;
	ulAngles[1] = viewAngles[1] * 0.35;
	llAngles[1] = viewAngles[1] * 0.45;
	thoracicAngles[2] = viewAngles[2] * 0.20;
	ulAngles[2] = viewAngles[2] * 0.35;
	llAngles[2] = viewAngles[2] * 0.45;
}

// BG_G2PlayerAngles. cent is what the game has of the entity: number, origin, angles (pitch, yaw, roll of where he
// looks), vel, legsAnim, torsoAnim, weapon, eFlags, saberMove, ground, moveDir (angles2[YAW], the way his keys
// move him, 0 to 7), vehicleNum, forceFrame. Returns the angles the model stands by and sets the bone angles.
// The swing of the legs is kept in inst.pe.
function g2BgPlayerAngles(inst, cent, time, frametime, lookAngles) {
	const t = g2AnimTables(), pe = inst.pe, ci = inst.ci, lerpAngles = cent.angles, zero = [0, 0, 0];
	if (cent.vehicleNum || cent.forceFrame || t.lockBreak.has(cent.legsAnim) || t.lockBreak.has(cent.torsoAnim)) {
		// on a vehicle, or in a saber lock: the model as a whole by his yaw, nothing turned above
		const forcedAngles = [0, lerpAngles[1], lerpAngles[2]];
		inst.turAngles = forcedAngles.slice();
		if (cent.number < PG.MAX_CLIENTS) for (const bone of ['lower_lumbar', 'upper_lumbar', 'cranium', 'thoracic', 'cervical']) g2SetHumanBoneAngles(inst, bone, zero, time);
		return forcedAngles;
	}
	if (time + 2000 < ci.corrTime) ci.corrTime = 0;
	const headAngles = [lerpAngles[0], g2AngleMod(lerpAngles[1]), lerpAngles[2]], legsAngles = [0, 0, 0];
	// if not standing still, always point all in the same direction
	if (cent.legsAnim !== t.BOTH_STAND1 || cent.torsoAnim !== t.weaponReady[cent.weapon]) { pe.torso.yawing = true; pe.torso.pitching = true; pe.legs.yawing = true; }
	// Adjust legs for movement dir (dead bodies do not twitch). That is angles2[YAW] of his entity, which the feed
	// does not tell. All this code asks of it is whether he goes backwards to a side (3 or 5), and then his legs
	// are in one of the animations for going backwards: that stands in for it until the feed has it (moveDir).
	const dir = (cent.eFlags & PG.EF_DEAD) ? 0 : cent.moveDir !== undefined ? cent.moveDir : t.back.has(cent.legsAnim) ? 3 : 0;
	// for now, turn torso instantly and let the legs swing to follow
	pe.torso.yawAngle = headAngles[1];

	const velocity = cent.vel.slice();
	if (t.roll2.has(cent.legsAnim) || (cent.weapon === PG.WP_SABER && g2SaberInSpecial(cent.saberMove))) velocity.fill(0);		// no angles from the speed then
	const speed = Math.hypot(velocity[0], velocity[1], velocity[2]);
	if (speed) for (let i = 0; i < 3; i++) velocity[i] /= speed;

	// only show a fraction of the pitch angle in the torso
	g2SwingAngles((headAngles[0] > 180 ? -360 + headAngles[0] : headAngles[0]) * 0.75, 15, 30, 0.1, pe.torso, 'pitchAngle', 'pitching', frametime);
	// (the game leans the legs into the speed here and then clears that roll and pitch again)

	// the legs by the way he moves, as far as the ground carries him
	legsAngles[1] = headAngles[1];
	const onGround = !(cent.ground === PG.ENTITYNUM_NONE || cent.forceFrame);
	const velAng = onGround ? [-velocity[0], -velocity[1], 0] : zero;
	let dif = 0;
	if (velAng[0] || velAng[1]) {
		const velYaw = g2VecToAngles(velAng)[1];
		let degrees_negative, degrees_positive, adddir;
		if (velYaw <= legsAngles[1]) {
			degrees_negative = legsAngles[1] - velYaw;
			degrees_positive = (360 - legsAngles[1]) + velYaw;
		} else {
			degrees_negative = legsAngles[1] + (360 - velYaw);
			degrees_positive = velYaw - legsAngles[1];
		}
		if (degrees_negative < degrees_positive) { dif = degrees_negative; adddir = 0; } else { dif = degrees_positive; adddir = 1; }
		if (dif > 90) dif = 180 - dif;
		if (dif > 60) dif = 60;
		// slight hack for when the player is running backward
		if (dir === 3 || dir === 5) dif = -dif;
		if (adddir) legsAngles[1] -= dif; else legsAngles[1] += dif;
	}
	g2SwingAngles(legsAngles[1], 0, 90, 0.65, pe.legs, 'yawAngle', 'yawing', frametime);
	legsAngles[1] = pe.legs.yawAngle;
	inst.turAngles = legsAngles.slice();

	const viewAngles = [lerpAngles[0] * 0.5, 0, 0], angles = [0, legsAngles[1], 0];
	const thoracicAngles = [0, 0, 0], ulAngles = [0, 0, 0], llAngles = [0, 0, 0];
	g2ClientSpineAngles(inst, cent, time, viewAngles, angles, thoracicAngles, ulAngles, llAngles, t);

	// the head: where he looks against where his eyes are
	const eyeAngles = lerpAngles.map(g2Angle180), look = lookAngles.map(g2Angle180);
	for (let i = 0; i < 3; i++) look[i] = g2AngleSubtract(look[i], eyeAngles[i]);
	g2UpdateLookAngles(ci.lookTime, ci.lastHeadAngles, time, look, 1.5, -50, 50, -70, 70, -30, 30);
	g2ClientNeckAngles(inst, time, look, thoracicAngles, cent.legsAnim, t);

	g2SetHumanBoneAngles(inst, 'lower_lumbar', llAngles, time);
	g2SetHumanBoneAngles(inst, 'upper_lumbar', ulAngles, time);
	g2SetHumanBoneAngles(inst, 'thoracic', thoracicAngles, time);
	return legsAngles;
}

// BG_G2ATSTAngles: the head of a walker turns on its bone "thoracic"
const g2ATSTAngles = (inst, time, angles) => g2SetHumanBoneAngles(inst, 'thoracic', angles, time);

// CG_G2PlayerAngles: the angles the model of a player or an NPC stands by, with the bone angles set. What turns
// like a humanoid goes through BG_G2PlayerAngles, a walker turns its head, everything else stands by its yaw
// (a fighter by all its angles, a speeder with its roll). cent.vehType is the kind of a vehicle (vehicleType_t).
function g2PlayerAngles(inst, cent, time, frametime) {
	const a = cent.angles;
	// (a dead one as a ragdoll: only with the cvar broadsword, which is off)
	if (!inst.pe) {
		// CG_ResetPlayerEntity: legs and torso start where he looks, the torso of an NPC level
		inst.pe = { legs: { yawAngle: a[1], yawing: false, pitchAngle: 0, pitching: false }, torso: { yawAngle: a[1], yawing: false, pitchAngle: cent.npc ? 0 : a[0], pitching: false } };
	}
	if (inst.model.animIndex <= 1) {
		// no look target is known here: he looks where his angles say, level
		return g2BgPlayerAngles(inst, cent, time, frametime, [0, a[1], a[2]]);
	}
	if (cent.vehicle && cent.vehType === PG.VH_WALKER) {
		g2ATSTAngles(inst, time, [a[0], 0, 0]);
		return [0, a[1], a[2]];
	}
	if (cent.vehicle && cent.vehType === PG.VH_FIGHTER) return a.slice();		// pitch and roll count for a fighter
	if (cent.npc && cent.vehicleNum && !cent.vehicle) return a.slice();		// an NPC bolted to a vehicle
	return [0, a[1], cent.vehicle && cent.vehType === PG.VH_SPEEDER ? a[2] : 0];
}
