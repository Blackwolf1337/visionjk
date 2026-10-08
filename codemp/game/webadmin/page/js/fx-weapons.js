// ---- which effect a weapon has for its muzzle, its shot and what it hits

// The server says nothing about these: a client that is sent a shot plays them itself. By weapon, as cgame has
// them: muzzle is what comes out of the weapon of the player who fired (cg_weaponinit.c, CG_AddPlayerWeapon),
// shot what the missile is in the air, played anew in every frame (the fx_*.c files), wall and flesh what it
// leaves where it ends (CG_MissileHitWall and CG_MissileHitPlayer in cg_weapons.c). A weapon without wall leaves
// nothing a client makes: a trip mine and a det pack go off through the game, which the page is told like every
// effect that is played once.
const FX_SHOTS = {
	4: { muzzle: 'bryar/muzzle_flash', shot: 'bryar/shot', wall: ['bryar/wall_impact'], flesh: ['bryar/flesh_impact'] },
	5: { muzzle: 'blaster/muzzle_flash', shot: 'blaster/shot', wall: ['blaster/wall_impact'], flesh: ['blaster/flesh_impact'] },
	7: { muzzle: 'bowcaster/muzzle_flash', shot: 'bowcaster/shot', wall: ['bowcaster/explosion'], flesh: ['bowcaster/explosion'] },
	8: { muzzle: 'repeater/muzzle_flash', shot: 'repeater/projectile', wall: ['repeater/wall_impact'], flesh: ['repeater/flesh_impact'] },
	9: { muzzle: 'demp2/muzzle_flash', shot: 'demp2/projectile', wall: ['demp2/wall_impact'], flesh: ['demp2/flesh_impact'] },
	10: { muzzle: 'flechette/muzzle_flash', shot: 'flechette/shot', wall: ['flechette/wall_impact'], flesh: ['flechette/flesh_impact'] },
	11: { muzzle: 'rocket/muzzle_flash', shot: 'rocket/shot', wall: ['rocket/explosion'], flesh: ['rocket/explosion'] },
	12: { wall: ['thermal/explosion', 'thermal/shockwave'], flesh: ['thermal/explosion', 'thermal/shockwave'] },
	13: {},
	14: {},
	15: { muzzle: 'concussion/muzzle_flash', shot: 'concussion/shot', wall: ['concussion/explosion'], flesh: ['concussion/explosion'] },
	18: { shot: 'turret/shot', wall: ['bryar/wall_impact'], flesh: ['bryar/flesh_impact'] }
};
// the old pistol is the pistol, the gun that stands is the blaster
FX_SHOTS[16] = FX_SHOTS[4];
FX_SHOTS[17] = FX_SHOTS[5];
// The other way to fire, where it differs. The ball of the flechette and the blast of the DEMP 2 go off through
// the game too.
const FX_SHOTS_ALT = {
	8: { muzzle: 'repeater/muzzle_flash', shot: 'repeater/alt_projectile', wall: ['repeater/concussion'], flesh: ['repeater/concussion'] },
	9: { muzzle: 'demp2/muzzle_flash' },
	10: { muzzle: 'flechette/muzzle_flash', shot: 'flechette/alt_shot' },
	11: { muzzle: 'rocket/altmuzzle_flash', shot: 'rocket/shot', wall: ['rocket/explosion'], flesh: ['rocket/explosion'] },
	15: { muzzle: 'concussion/altmuzzle_flash', shot: 'concussion/shot', wall: ['concussion/explosion'], flesh: ['concussion/explosion'] }
};
const FX_PLAYS_UP = 'thermal/shockwave';	// the ring of a thermal detonator lies flat whatever it hit
const FX_FLESH_DIST = 40;					// a shot that ended this near to a player hit him
const FX_MUZZLE_DIST = 80;					// one that came by this near to a player was fired by him: he has moved on since

const fxShot = (weapon, alt) => (alt && FX_SHOTS_ALT[weapon]) || FX_SHOTS[weapon] || null;

// The player nearest to a stretch of a way, when he is nearer to it than far: from is where it begins, dir the
// way it runs and reach how long it is. { along, far }: how far along the way he is next to it, and how near.
function fxPlayerOn(from, dir, reach, far) {
	let found = null;
	for (const p of players.values()) {
		if (p.data.dead || p.data.team === 'spectator') continue;
		const to = [p.draw[0] - from[0], p.draw[1] - from[1], p.draw[2] - from[2]];
		const along = Math.min(reach, Math.max(0, to[0] * dir[0] + to[1] * dir[1] + to[2] * dir[2]));
		const off = Math.hypot(to[0] - dir[0] * along, to[1] - dir[1] * along, to[2] - dir[2] * along);
		if (off < far && (!found || off < found.far)) found = { along, far: off };
	}
	return found;
}

// A shot in the air (m of state.shots) at pos, flying along dir: the effect of the muzzle once, and the effect
// of the shot. False: its weapon has none for the air, or the effect is not read (yet).
function fxShotFlies(m, pos, dir) {
	const w = fxShot(m[1], m[2]);
	if (!w) return false;
	if (!m.seen) {
		m.seen = true;
		if (m.fresh && w.muzzle) {
			// Who fired it is not told: it is the player the way behind it leads back to, as far as a shot gets in
			// two polls. The muzzle is a little in front of him. What a thing of the map fired has no flash in the
			// game either.
			const speed = Math.hypot(m[6], m[7], m[8]), back = [-dir[0], -dir[1], -dir[2]], from = fxPlayerOn([m[3], m[4], m[5]], back, speed * pollInterval / 500, FX_MUZZLE_DIST);
			if (from) fxPlay(w.muzzle, [0, 1, 2].map(i => m[3 + i] + back[i] * Math.max(0, from.along - 24)), dir);
		}
		// what it will leave where it ends, so that the file is there by then
		for (const name of w.wall ? [...w.wall, ...w.flesh] : []) fxGet(name);
	}
	if (!w.shot || !fxReady(fxGet(w.shot))) return false;
	fxPlay(w.shot, pos, dir);
	return true;
}

// Where a shot ended (f of flashes): the effect its weapon leaves on a player when one is on the last stretch of
// its way, else on the wall that way runs into, facing as the wall does. A ray finds the wall while fxTrace has
// one left, without one it is played where the shot was last. True: done, or the weapon leaves nothing. False:
// the page has no effect for it.
function fxShotEnds(f) {
	const w = fxShot(f.weapon, f.alt);
	if (!w) return false;
	if (!w.wall) return true;
	if ([...w.wall, ...w.flesh].some(name => fxGet(name).state === 2)) return false;
	const speed = Math.hypot(f.vel[0], f.vel[1], f.vel[2]), dir = speed ? f.vel.map(v => v / speed) : [0, 0, 1];
	// f.pos is the middle of the way it would have gone in the poll it ended in
	const reach = speed * pollInterval / 1000, from = f.pos.map((v, i) => v - dir[i] * reach / 2), hit = fxPlayerOn(from, dir, reach, FX_FLESH_DIST);
	let names = w.flesh, at = hit && from.map((v, i) => v + dir[i] * hit.along), normal = speed > 1 ? [-dir[0], -dir[1], -dir[2]] : [0, 0, 1];
	if (!hit) {
		const tr = speed > 1 ? fxTrace(from, from.map((v, i) => v + dir[i] * (reach + 64))) : null;
		names = w.wall;
		at = tr && tr.normal ? tr.end : f.pos;
		if (tr && tr.normal) normal = tr.normal;
	}
	for (const name of names) fxPlay(name, at, name === FX_PLAYS_UP ? [0, 0, 1] : normal);
	return true;
}
