// ---- what of the 2D of a client depends on the world it looks into (cgame/cg_draw.c): the crosshair and what it
// points at, the name under it
//
// Not ported, each draws nothing: the radar (CG_DrawRadar), the lock of the rocket launcher on a target
// (CG_DrawRocketLocking) and the brackets around entities (CG_DrawBracketedEntities, siege and vehicles).

// WP_MuzzlePoint of game/bg_weapons.c: forward, right, up
const HUD_MUZZLE = [[0, 0, 0], [0, 8, 0], [0, 8, 0], [8, 16, 0], [12, 6, -6], [12, 6, -6], [12, 6, -6], [12, 2, -6], [12, 4.5, -6], [12, 6, -6], [12, 6, -6], [12, 8, -4], [12, 0, -4], [12, 0, -10], [12, 0, -4], [12, 6, -6], [12, 6, -6]];
const HUD_PW_CLOAKED = 11, HUD_FADE_TIME = 200;

// AngleVectors: forward and right of pitch and yaw (roll 0)
function hudAngleVectors(pitch, yaw) {
	const p = pitch * Math.PI / 180, y = yaw * Math.PI / 180, sp = Math.sin(p), cp = Math.cos(p), sy = Math.sin(y), cy = Math.cos(y);
	return { fwd: [cp * cy, cp * sy, -sp], right: [sy, -cy, 0] };
}

// CG_CalcMuzzlePoint for the player the client looks through the eyes of: where the line of his crosshair begins
function hudCalcMuzzlePoint(cg) {
	const ps = cg.ps, w = ps.weapon;
	let wm = HUD_MUZZLE[w] || [0, 0, 0];
	if (w === HUD_WP.DISRUPTOR || w === HUD_WP.STUN_BATON || w === HUD_WP.MELEE || w === HUD_WP.SABER) wm = [0, 0, 0];
	const third = cg.renderingThirdPerson, p = players.get(cg.d.id);
	const gun = third ? (p ? p.draw : ps.origin) : eye, ax = third ? hudAngleVectors(ps.viewangles[0], ps.viewangles[1]) : hudAngleVectors(view.pitch, view.yaw);
	const m = [gun[0] + ax.fwd[0] * wm[0] + ax.right[0] * wm[1], gun[1] + ax.fwd[1] * wm[0] + ax.right[1] * wm[1], gun[2] + ax.fwd[2] * wm[0] + ax.right[2] * wm[1]];
	m[2] += third ? ps.viewheight + wm[2] : wm[2];
	return m;
}

// Where a line enters the box of a player (his bounds of the game: 15 to each side, 24 below and 40 above his
// origin, 16 above when he crouches). The fraction of the line, or 1 when it misses. The game goes on from the
// box to the triangles of his model (CG_G2Trace with cg_dynamicCrosshairPrecision): not done here.
function hudLineBox(start, end, org, top) {
	const mins = [org[0] - 15, org[1] - 15, org[2] - 24], maxs = [org[0] + 15, org[1] + 15, org[2] + top];
	let t0 = 0, t1 = 1;
	for (let i = 0; i < 3; i++) {
		const d = end[i] - start[i];
		if (Math.abs(d) < 1e-9) { if (start[i] < mins[i] || start[i] > maxs[i]) return 1; continue; }
		let a = (mins[i] - start[i]) / d, b = (maxs[i] - start[i]) / d;
		if (a > b) [a, b] = [b, a];
		t0 = Math.max(t0, a); t1 = Math.min(t1, b);
		if (t0 > t1) return 1;
	}
	return t0;
}

// CG_WorldCoordToScreenCoordFloat: where a place of the map is on the 640 x 480 screen, null behind the eye. The
// camera of the page is the refdef of the game (the same angle of view), so its matrix says it.
function hudWorldToScreen(p) {
	const c = M4.transform(viewProj, p);
	if (c[3] <= 0.001) return null;
	return [(c[0] / c[3] * 0.5 + 0.5) * 640, (0.5 - c[1] / c[3] * 0.5) * 480];
}

// CG_ScanForCrosshairEntity (cg_dynamicCrosshair is 1): a line from his muzzle the way he looks, as far as the map
// is drawn. What it hits first is where the crosshair is drawn, and a player it hits is the one that is named.
// The line through the map is traceWorld of js/trace.js (null: nothing in the way).
function hudScanForCrosshairEntity(cg) {
	const ps = cg.ps, L = cg.L, start = hudCalcMuzzlePoint(cg);
	const ax = cg.renderingThirdPerson ? hudAngleVectors(ps.viewangles[0], ps.viewangles[1]) : hudAngleVectors(view.pitch, view.yaw);
	const far = mapEnts().distanceCull, end = [start[0] + ax.fwd[0] * far, start[1] + ax.fwd[1] * far, start[2] + ax.fwd[2] * far];
	const tr = typeof traceWorld === 'function' ? traceWorld(start, end) : null;
	let fraction = tr ? tr.fraction : 1, entityNum = tr ? HUD_ENTITYNUM_WORLD : HUD_ENTITYNUM_NONE;
	for (const [id, p] of players) {
		const o = p.data;
		if (id === ps.clientNum || id === cg.d.id || o.team === 'spectator' || o.dead) continue;
		const f = hudLineBox(start, end, p.draw, o.viewheight && o.viewheight < 20 ? 16 : 40);
		if (f < fraction) { fraction = f; entityNum = id; }
	}
	const endpos = [start[0] + (end[0] - start[0]) * fraction, start[1] + (end[1] - start[1]) * fraction, start[2] + (end[2] - start[2]) * fraction];
	if (ps.persistant[HUD_PERS.TEAM] !== HUD_TEAM_SPECTATOR) {
		if (entityNum < HUD_ENTITYNUM_WORLD) {
			L.crosshairClientNum = entityNum;
			L.crosshairClientTime = cg.time;
			hudDrawCrosshair(cg, endpos, 1);
		} else {
			hudDrawCrosshair(cg, endpos, 0);
		}
	}
	if (entityNum >= HUD_MAX_CLIENTS) return;
	L.crosshairClientNum = entityNum;
	L.crosshairClientTime = cg.time;
}

// CG_DrawCrosshair (cg_drawCrosshair 1, cg_crosshairSize 24, cg_crosshairHealth 0): the image, where the line of
// the crosshair ends, in the colour of what is under it. Not here: the size in a vehicle and while an item is
// picked up, the bars of health under it, the ring around a mover the Force can push.
function hudDrawCrosshair(cg, worldPoint, chEntValid) {
	const ps = cg.ps, L = cg.L;
	if (ps.fallingToDeath || ps.zoomMode !== 0) return;
	if (L.crosshairClientNum >= HUD_ENTITYNUM_WORLD) {
		hudSetColor(null);
	} else if (chEntValid && L.crosshairClientNum < HUD_MAX_CLIENTS) {
		const other = hudClientInfo(L.crosshairClientNum), me = hudClientInfo(ps.clientNum);
		let c;
		if (other && (other.powerups & (1 << HUD_PW_CLOAKED))) c = [1, 1, 1];
		else {
			// allies are green, enemies red
			c = cg.cgs.gametype >= HUD_GT.TEAM && other && me && other.team === me.team ? [0, 1, 0] : [1, 0, 0];
			// grey for everyone but the one he fights a duel with
			if (ps.duelInProgress && L.crosshairClientNum !== ps.duelIndex) c = [0.4, 0.4, 0.4];
		}
		hudSetColor([c[0], c[1], c[2], 1]);
	}
	const w = 24, h = 24;
	let x = 0, y = 0;
	if (worldPoint && Math.hypot(worldPoint[0], worldPoint[1], worldPoint[2])) {
		const at = hudWorldToScreen(worldPoint);
		if (!at) return;
		x = at[0] - 320;
		y = at[1] - 240;
	}
	hudStretchPic(x + 0.5 * (640 - w), y + 0.5 * (480 - h), w, h, 0, 0, 1, 1, hudShader('gfx/2d/crosshaira'));
	hudSetColor(null);
}

// CG_FadeColor: the alpha of something that shows for total ms from start on, null when it does not show
function hudFadeColor(cg, start, total) {
	if (start === 0) return null;
	const t = cg.time - start;
	if (t >= total || t < 0) return null;
	return total - t < HUD_FADE_TIME ? (total - t) / HUD_FADE_TIME : 1;
}

// CG_DrawCrosshairNames: the name of the player under the crosshair, for a second after the line last hit him.
// In FFA the JA+ client plugin shows the name in its own colours.
function hudDrawCrosshairNames(cg) {
	const ps = cg.ps, L = cg.L;
	hudScanForCrosshairEntity(cg);
	if (L.crosshairClientNum >= HUD_MAX_CLIENTS) return;
	const other = hudClientInfo(L.crosshairClientNum);
	if (!other || (other.powerups & (1 << HUD_PW_CLOAKED))) return;
	const alpha = hudFadeColor(cg, L.crosshairClientTime, 1000);
	if (alpha === null) { hudSetColor(null); return; }
	let name = plainName(other.name), base = HUD_CT.RED;
	if (cg.cgs.gametype >= HUD_GT.TEAM) base = hudTeamOf(other) === ps.persistant[HUD_PERS.TEAM] ? HUD_CT.GREEN : HUD_CT.RED;
	if (ps.duelInProgress && L.crosshairClientNum !== ps.duelIndex) base = HUD_CT.BLACK;
	if (cg.cgs.gametype === HUD_GT.FFA) { base = HUD_CT.WHITE; name = other.name; }
	hudDrawProportionalString(320, 170, name, HUD_UI_CENTER, [base[0], base[1], base[2], alpha * 0.5]);
	hudSetColor(null);
}

// CG_DrawRadar, CG_DrawRocketLocking, CG_DrawBracketedEntities: not ported
function hudDrawRadar(cg, y) { return y; }
function hudDrawRocketLocking(cg, lockEntNum, lockTime) {}
function hudDrawBracketedEntities(cg) {}
