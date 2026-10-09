// ---- a line through the map: where it hits first (CM_BoxTrace of the engine, for a line)
//
// For what the client game traces: the shadow under a player, a mark on a wall, the camera behind a player.
// Stand-in: nothing is hit.

// From start to end. null when nothing is in the way, else { fraction, pos, normal }: how far along the line
// (0 to 1), where, and the normal of what was hit.
function traceWorld(start, end) { return null; }
