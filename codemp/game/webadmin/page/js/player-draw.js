// ---- the players and the NPCs drawn (CG_Player in cg_players.c)
//
// A player of the state (expandPlayers in js/state.js) has what a client draws him by: the frame the server has
// his legs and his torso at, the flags of his entity, his powerups and force powers. An NPC has the same in "f".
// Stand-in: the model, turned as a whole by his yaw, in the animation from when the page first saw it.

// the players with their models, or as plain figures until those are read
function drawPlayers(now) {
	for (const [id, p] of players) {
		const d = p.data;
		if (d.team === 'spectator') continue;
		const s = (d.scale || 100) / 100, base = p.draw[2] - 24 * s;
		const col = TEAM_COLORS[d.team] || TEAM_COLORS.free;
		const c = d.dead ? [0.35, 0.35, 0.38, 1] : [col[0], col[1], col[2], 1];
		const yaw = d.yaw * Math.PI / 180;
		const inst = g2Instance(id, d.model, d.team);
		const ownView = id === view.id && !view.thirdPerson;
		if (g2Update(inst, d.legsAnim, d.torsoAnim, now)) {
			if (!ownView) {
				g2Draw(inst, p.draw[0], p.draw[1], p.draw[2], yaw, s, col, d.dead ? 0.35 : 0.08);
				drawHeldWeapons(inst, d, entityMatrix(p.draw[0], p.draw[1], p.draw[2], yaw, s));
			}
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

// NPCs with their models
function drawNpcs(now) {
	if (state) {
		for (const n of state.npcs || []) {
			const inst = g2Instance(n.id, n.model, '');
			const sc = (n.scale || 100) / 100;
			if (g2Update(inst, n.legsAnim, n.torsoAnim, now)) g2Draw(inst, n.pos[0], n.pos[1], n.pos[2], n.yaw * Math.PI / 180, sc, [1, 0.4, 1], 0.05);
			else drawMesh(bodyMesh, n.pos[0], n.pos[1], n.pos[2] - 24 * sc, n.yaw * Math.PI / 180, sc, [0.9, 0.4, 0.9, 1]);
		}
	}
}
