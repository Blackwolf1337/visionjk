// ---- the items of a map as the client draws them (CG_Item in cg_ents.c)
//
// An item of the state (expandItems in js/state.js) is its entity, its place in the list of the items of the game
// (itemDef gives what the game says of it), the flags of its entity, its place and its angles.
// Stand-in: a model that turns, where the item is.

// the items of the map, each as its model that turns as in the game, or as a diamond until that is read
function drawItems(now) {
	if (state) {
		const spin = now / 600;
		for (const it of state.items) {
			// one the game does not draw either: taken, and not to come back as a ghost
			if (it.flags & EF_NODRAW) continue;
			// the real item model, spinning like in the game, until it loaded a colored diamond
			const info = itemDef(it);
			const model = info && info.model ? staticNow(info.model) : null;
			if (model) {
				const c = Math.cos(spin), sn = Math.sin(spin);
				drawStatic(model, new Float32Array([c, sn, 0, 0, -sn, c, 0, 0, 0, 0, 1, 0, it.pos[0], it.pos[1], it.pos[2] + 4 + Math.sin(spin + it.id) * 2, 1]));
				gl.useProgram(flatProg.p);
				gl.uniformMatrix4fv(flatProg.u.uMvp, false, viewProj);
			} else {
				const col = ITEM_COLORS[info ? info.type : it.type] || [1, 1, 1];
				drawMesh(diamondMesh, it.pos[0], it.pos[1], it.pos[2] + 6 + Math.sin(spin + it.id) * 3, spin, 1, [col[0], col[1], col[2], 1]);
			}
		}
	}
}
