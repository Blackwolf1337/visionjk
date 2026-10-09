// ---- which effect a weapon has for its muzzle, its shot and what it hits

// weapon_t (bg_weapons.h)
const WP_NONE = 0, WP_STUN_BATON = 1, WP_MELEE = 2, WP_SABER = 3, WP_BRYAR_PISTOL = 4, WP_BLASTER = 5, WP_DISRUPTOR = 6, WP_BOWCASTER = 7, WP_REPEATER = 8,
	WP_DEMP2 = 9, WP_FLECHETTE = 10, WP_ROCKET_LAUNCHER = 11, WP_THERMAL = 12, WP_TRIP_MINE = 13, WP_DET_PACK = 14, WP_CONCUSSION = 15, WP_BRYAR_OLD = 16,
	WP_EMPLACED_GUN = 17, WP_TURRET = 18, WP_NUM_WEAPONS = 19;

// By weapon, as cgame has them: muzzle is what comes out of the weapon of the one who fired (muzzleEffect of
// CG_RegisterWeapon in cg_weaponinit.c, played by CG_AddPlayerWeapon), shot what the missile is in the air, played
// anew in every frame (missileTrailFunc, the fx_*.c files). What a shot leaves where it ends is fxHitNames below.
const FX_SHOTS = {
	4: { muzzle: 'bryar/muzzle_flash', shot: 'bryar/shot' },
	5: { muzzle: 'blaster/muzzle_flash', shot: 'blaster/shot' },
	6: { muzzle: 'disruptor/muzzle_flash' },
	7: { muzzle: 'bowcaster/muzzle_flash', shot: 'bowcaster/shot' },
	8: { muzzle: 'repeater/muzzle_flash', shot: 'repeater/projectile' },
	9: { muzzle: 'demp2/muzzle_flash', shot: 'demp2/projectile' },
	10: { muzzle: 'flechette/muzzle_flash', shot: 'flechette/shot' },
	11: { muzzle: 'rocket/muzzle_flash', shot: 'rocket/shot' },
	12: {},
	13: {},
	14: {},
	15: { muzzle: 'concussion/muzzle_flash', shot: 'concussion/shot' },
	18: { shot: 'turret/shot' }
};
// the old pistol is the pistol, the gun that stands is the blaster
FX_SHOTS[16] = FX_SHOTS[4];
FX_SHOTS[17] = FX_SHOTS[5];
// the other way to fire, where it differs (altMuzzleEffect, altMissileTrailFunc)
const FX_SHOTS_ALT = {
	8: { muzzle: 'repeater/muzzle_flash', shot: 'repeater/alt_projectile' },
	9: { muzzle: 'demp2/muzzle_flash' },
	10: { muzzle: 'flechette/muzzle_flash', shot: 'flechette/alt_shot' },
	11: { muzzle: 'rocket/altmuzzle_flash', shot: 'rocket/shot' },
	15: { muzzle: 'concussion/altmuzzle_flash', shot: 'concussion/shot' }
};
const FX_PLAYS_UP = 'thermal/shockwave';	// the ring of a thermal detonator lies flat whatever it hit
const FX_FLESH_DIST = 40;					// a shot that ended this near to a player hit him
const FX_MUZZLE_DIST = 80;					// one that came by this near to a player was fired by him: he has moved on since

const fxShot = (weapon, alt) => (alt && FX_SHOTS_ALT[weapon]) || FX_SHOTS[weapon] || null;

// CG_MissileHitWall and CG_MissileHitPlayer (cg_weapons.c) with the FX_...HitWall and FX_...HitPlayer they call
// (the fx_*.c files): the names of the effects a shot of a weapon leaves on a wall, or on somebody (player).
// charge is how far the pistol was charged for its other way to fire (es->generic1 of the shot).
function fxHitNames(weapon, altFire, player, charge) {
	switch (weapon) {
	case WP_BRYAR_PISTOL: case WP_BRYAR_OLD:
		if (player) return ['bryar/flesh_impact'];
		// FX_BryarAltHitWall: the power of the shot picks one of three
		return [altFire && (charge === 4 || charge === 5) ? 'bryar/wall_impact3' : altFire && (charge === 2 || charge === 3) ? 'bryar/wall_impact2' : 'bryar/wall_impact'];
	case WP_CONCUSSION: return ['concussion/explosion'];
	case WP_TURRET: return [player ? 'bryar/flesh_impact' : 'bryar/wall_impact'];
	case WP_BLASTER: case WP_EMPLACED_GUN: return [player ? 'blaster/flesh_impact' : 'blaster/wall_impact'];
	case WP_DISRUPTOR: return [player ? 'disruptor/alt_hit' : 'disruptor/alt_miss'];
	case WP_BOWCASTER: return ['bowcaster/explosion'];
	case WP_REPEATER: return [altFire ? 'repeater/concussion' : player ? 'repeater/flesh_impact' : 'repeater/wall_impact'];
	case WP_DEMP2: return [altFire ? 'demp2/altDetonate' : player ? 'demp2/flesh_impact' : 'demp2/wall_impact'];
	// the ball of the other way to fire leaves nothing on a wall
	case WP_FLECHETTE: return player ? ['flechette/flesh_impact'] : altFire ? [] : ['flechette/wall_impact'];
	case WP_ROCKET_LAUNCHER: return ['rocket/explosion'];
	case WP_THERMAL: return ['thermal/explosion', 'thermal/shockwave'];
	}
	return [];
}

// CG_MissileHitWall: what a shot leaves on a wall at origin, dir is how the wall faces
function cgMissileHitWall(weapon, origin, dir, altFire, charge) {
	if (weapon === WP_DISRUPTOR) { fxDisruptorAltMiss(origin, dir); return; }
	for (const name of fxHitNames(weapon, altFire, false, charge)) fxPlay(name, origin, name === FX_PLAYS_UP ? [0, 0, 1] : dir);
}

// CG_MissileHitPlayer: what it leaves on somebody (every body is a humanoid in the game of several players)
function cgMissileHitPlayer(weapon, origin, dir, altFire) {
	for (const name of fxHitNames(weapon, altFire, true, 0)) fxPlay(name, origin, name === FX_PLAYS_UP ? [0, 0, 1] : dir);
}

// FX_AddLine of the engine (client/FxUtil.cpp) as cgame calls it: a line of the effects from start to end that
// lives for killTime msec. size, alpha and rgb each go from their first value to their second as the flags say
// (FX_SIZE_LINEAR and so on: the flags of a group at its shift, as in an effect file), shader is a name.
const FX_ALPHA_LINEAR = FX_LINEAR, FX_SIZE_LINEAR = FX_LINEAR << 8, FX_ALPHA_WAVE = FX_WAVE;
function fxAddLine(start, end, size1, size2, sizeParm, alpha1, alpha2, alphaParm, sRGB, eRGB, rgbParm, killTime, shader, flags) {
	if (!fxOn() || fxLive.length >= FX_MAX_LIVE) return;
	const parm = (v, shift) => { const mode = flags >> shift & FX_PARM_MASK; return mode === FX_WAVE ? v * 3.14159 * 0.001 : mode ? v * 0.01 * killTime + fxTime : 0; };
	fxLive.push({ p: null, type: 'line', flags, t0: fxTime, t1: fxTime + killTime, org: start.slice(0, 3), org2: end.slice(0, 3), shader,
		size: [size1, size2, parm(sizeParm, 8)], alpha: [alpha1, alpha2, parm(alphaParm, 0)], rgb: [sRGB.slice(), eRGB.slice(), parm(rgbParm, 4)] });
}

// ---- fx_disruptor.c
const FX_WHITE = [1, 1, 1];

// FX_DisruptorMainShot: the beam of a shot, from the muzzle to where it ended
function fxDisruptorMainShot(start, end) {
	fxAddLine(start, end, 0.1, 6, 0, 1, 0, 0, FX_WHITE, FX_WHITE, 0, 150, 'gfx/effects/redLine', FX_SIZE_LINEAR | FX_ALPHA_LINEAR);
}

// FX_DisruptorAltShot: the beam of a shot through the scope, with a second one in it when it was fully charged
function fxDisruptorAltShot(start, end, fullCharge) {
	fxAddLine(start, end, 0.1, 10, 0, 1, 0, 0, FX_WHITE, FX_WHITE, 0, 175, 'gfx/effects/redLine', FX_SIZE_LINEAR | FX_ALPHA_LINEAR);
	if (fullCharge) { const yeller = [0.8, 0.7, 0]; fxAddLine(start, end, 0.1, 7, 0, 1, 0, 0, yeller, yeller, 0, 150, 'gfx/misc/whiteline2', FX_SIZE_LINEAR | FX_ALPHA_LINEAR); }
}

// FX_DisruptorAltMiss: where a shot through the scope ended at a wall, a wisp of smoke that rises from it for four
// seconds (FX_AddBezier, evAddBezier in js/events.js) and the effect of the miss
function fxDisruptorAltMiss(origin, normal) {
	const c1 = [origin[0] + 4 * normal[0], origin[1] + 4 * normal[1], origin[2] + 4 * normal[2]], c2 = [c1[0], c1[1], c1[2] + 12];
	c1[2] += 4;
	const pos = [origin[0] + normal[0], origin[1] + normal[1], origin[2] + normal[2] + 28];
	evAddBezier(origin, pos, c1, [0, 0, 0], c2, [0, 0, 0], 6, 6, 0, 0, 0.2, 0.5, FX_WHITE, FX_WHITE, 0, 4000, 'gfx/effects/smokeTrail', FX_ALPHA_WAVE);
	fxPlay('disruptor/alt_miss', origin, normal);
}

// FX_DisruptorAltHit, FX_DisruptorHitWall and FX_DisruptorHitPlayer
const fxDisruptorAltHit = (origin, normal) => fxPlay('disruptor/alt_hit', origin, normal);
const fxDisruptorHitWall = (origin, normal) => fxPlay('disruptor/wall_impact', origin, normal);
const fxDisruptorHitPlayer = (origin, normal) => fxPlay('disruptor/flesh_impact', origin, normal);

// FX_ConcAltShot (fx_bryarpistol.c): the beam of the other way to fire the concussion rifle
function fxConcAltShot(start, end) {
	const bright = [0.75, 0.5, 1];
	fxAddLine(start, end, 0.1, 10, 0, 1, 0, 0, FX_WHITE, FX_WHITE, 0, 175, 'gfx/effects/blueLine', FX_SIZE_LINEAR | FX_ALPHA_LINEAR);
	fxAddLine(start, end, 0.1, 7, 0, 1, 0, 0, bright, bright, 0, 150, 'gfx/misc/whiteline2', FX_SIZE_LINEAR | FX_ALPHA_LINEAR);
}

// ---- for a game module that tells no events (evOn in js/events.js): the page guesses them from the shots

// The player nearest to a stretch of a way, when he is nearer to it than far: from is where it begins, dir the
// way it runs and reach how long it is. { along, far, id }: how far along the way he is next to it, how near, and
// who he is.
function fxPlayerOn(from, dir, reach, far) {
	let found = null;
	for (const [id, p] of players) {
		if (p.data.dead || p.data.team === 'spectator') continue;
		const to = [p.draw[0] - from[0], p.draw[1] - from[1], p.draw[2] - from[2]];
		const along = Math.min(reach, Math.max(0, to[0] * dir[0] + to[1] * dir[1] + to[2] * dir[2]));
		const off = Math.hypot(to[0] - dir[0] * along, to[1] - dir[1] * along, to[2] - dir[2] * along);
		if (off < far && (!found || off < found.far)) found = { along, far: off, id };
	}
	return found;
}

// A shot in the air (m of state.shots) at pos, flying along dir: the effect of the shot. Without events also the
// effect of the muzzle, once. False: its weapon has none for the air, or the effect is not read (yet).
function fxShotFlies(m, pos, dir) {
	const w = fxShot(m[1], m[2]);
	if (!w) return false;
	if (!m.seen) {
		m.seen = true;
		// with events the one who fired is told (EV_FIRE_WEAPON, evMuzzleFlash)
		if (m.fresh && w.muzzle && !evOn()) {
			// Who fired it is not told: it is the player the way behind it leads back to, as far as a shot gets in
			// two polls. The flash is at the muzzle of his weapon where the page drew it (the *flash bolt, as
			// CG_AddPlayerWeapon plays it), or a little in front of him when the page did not draw his weapon. What
			// a thing of the map fired has no flash in the game either.
			const speed = Math.hypot(m[6], m[7], m[8]), back = [-dir[0], -dir[1], -dir[2]], from = fxPlayerOn([m[3], m[4], m[5]], back, speed * pollInterval / 500, FX_MUZZLE_DIST);
			const muzzle = from ? wpMuzzles.get(from.id) : null;
			if (muzzle && Math.abs(frameNow - muzzle.now) < 250) fxPlay(w.muzzle, muzzle.org, muzzle.dir);
			else if (from) fxPlay(w.muzzle, [0, 1, 2].map(i => m[3 + i] + back[i] * Math.max(0, from.along - 24)), dir);
		}
		// what it will leave where it ends, so that the file is there by then
		for (const name of [...fxHitNames(m[1], m[2], false, 0), ...fxHitNames(m[1], m[2], true, 0)]) fxGet(name);
	}
	if (!w.shot || !fxReady(fxGet(w.shot))) return false;
	fxPlay(w.shot, pos, dir);
	return true;
}

// Where a shot ended (f of flashes). With events the shot says itself what it leaves and where (EV_MISSILE_HIT,
// EV_MISSILE_MISS). Without them it is guessed: what its weapon leaves on a player when one is on the last stretch
// of its way, else on the wall that way runs into, facing as the wall does. A ray finds the wall while fxTrace has
// one left, without one it is played where the shot was last. True: done, or the weapon leaves nothing. False:
// the page has no effect for it.
function fxShotEnds(f) {
	if (evOn()) return true;
	if (!fxShot(f.weapon, f.alt)) return false;
	// the ball of the flechette and the blast of the DEMP 2 go off by themselves, which only an event tells
	const guess = !(f.alt && (f.weapon === WP_DEMP2 || f.weapon === WP_FLECHETTE)), wall = guess ? fxHitNames(f.weapon, f.alt, false, 0) : [], flesh = guess ? fxHitNames(f.weapon, f.alt, true, 0) : [];
	if (!wall.length) return true;
	if ([...wall, ...flesh].some(name => fxGet(name).state === 2)) return false;
	const speed = Math.hypot(f.vel[0], f.vel[1], f.vel[2]), dir = speed ? f.vel.map(v => v / speed) : [0, 0, 1];
	// f.pos is the middle of the way it would have gone in the poll it ended in
	const reach = speed * pollInterval / 1000, from = f.pos.map((v, i) => v - dir[i] * reach / 2), hit = fxPlayerOn(from, dir, reach, FX_FLESH_DIST);
	let at = hit && from.map((v, i) => v + dir[i] * hit.along), normal = speed > 1 ? [-dir[0], -dir[1], -dir[2]] : [0, 0, 1];
	if (!hit) {
		const tr = speed > 1 ? fxTrace(from, from.map((v, i) => v + dir[i] * (reach + 64))) : null;
		at = tr && tr.normal ? tr.end : f.pos;
		if (tr && tr.normal) normal = tr.normal;
	}
	if (hit) cgMissileHitPlayer(f.weapon, at, normal, f.alt); else cgMissileHitWall(f.weapon, at, normal, f.alt, 0);
	return true;
}
