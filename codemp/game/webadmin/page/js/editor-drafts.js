// ---- the drafts: entities that are only in the page until they are placed

// the marker of a draft on the map
function drawDraftMark(pos, c, sel, now) {
	drawMesh(cubeMesh, pos[0], pos[1], pos[2], 0, sel ? 1.6 : 1, sel ? [1, 0.2, 0.3, 1] : [c[0], c[1], c[2], 1]);
}
