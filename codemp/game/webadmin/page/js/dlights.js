// ---- the dynamic lights of a frame: what a blade, a shot or an effect throws on the map and on models
// (R_AddLightToScene; ProjectDlightTexture2 in tr_shade.cpp for the map, R_SetupEntityLighting in tr_light.cpp
// for a model)
//
// Stand-in: there are none yet.

// Before anything of a frame is drawn: the lights of this frame are found. The engine has all of them before it
// draws, the page draws as it goes, so they can not be added while the things that shine are drawn.
function gatherDlights(now) {}

// The light of a place with the lights of the frame added, as R_SetupEntityLighting adds them to what the light
// grid gave. out: what gridLight filled (ambient, directed, dir).
function dlightsOn(pos, out) {}

// after the solid surfaces of the map: the map lit by the lights of the frame
function drawDlights(now) {}
