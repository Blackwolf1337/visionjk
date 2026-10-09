// ---- marks on the map (CG_ImpactMark in cg_marks.c with R_MarkFragments of the engine): the shadow under a
// player, what a shot or a blade leaves on a wall
//
// Stand-in: there are none yet.

// A mark is left, with the arguments of CG_ImpactMark: the name of its shader, where it is and the normal of the
// surface there, how far it is turned (degrees), its colour (0 to 1), whether it fades by its alpha or by its
// colour, its radius, and whether it is one of this frame only (a shadow) or stays and fades.
function impactMark(shader, origin, dir, orientation, r, g, b, a, alphaFade, radius, temporary) {}

// after the solid surfaces of the map: the marks, the shadows of this frame among them
function drawMarks(now) {}
