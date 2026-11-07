// ---- what a mover draws besides its brushes (CG_Mover in cg_ents.c): the model of its model2 key
//
// A place of a brush model (applyMovers in js/state.js) says the model that is drawn with it (model2, '' for
// none) and the frame its shaders are at (-1 unless the entity has EF_SHADER_ANIM, js/scripts-world.js takes it).
// The game draws that model with the origin and the axes of the mover, so it moves and turns with it.

const MOVER_AT_REST = new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);

// after the solid surfaces of the map: the models of the movers, where the movers are in this frame
function drawMoverModels(now) {
	if (!movers) return;
	for (const [n, places] of movers) {
		let mats = null;
		for (let i = 0; i < places.length; i++) {
			const m = places[i];
			// A mover has no Ghoul2 instance (CG_SetGhoul2Info hands on the one of its entity, which is none), and
			// without one the renderer draws nothing of a .glm.
			if (!m.model2 || /\.glm$/i.test(m.model2)) continue;
			const model = staticNow(m.model2);
			if (!model) continue;
			// the matrices of this frame, in the order of the places. None for a mover where the map file has it.
			mats = mats || moverPlaces(n, now);
			let place = mats[i] || MOVER_AT_REST;
			// iModelScale of the entity scales the second model (ScaleModelAxis), where the feed says it
			const scale = m.scale && m.scale !== 100 ? m.scale / 100 : 1;
			if (scale !== 1) { place = place.slice(); for (let k = 0; k < 12; k++) if ((k & 3) !== 3) place[k] *= scale; }
			// with EF_SHADER_ANIM the second model is left at the first image of its shaders (skinNum 0, RF_SETANIMINDEX)
			stFrame = m.frame >= 0 ? 0 : -1;
			drawStatic(model, place);
		}
	}
	stFrame = -1;
}
