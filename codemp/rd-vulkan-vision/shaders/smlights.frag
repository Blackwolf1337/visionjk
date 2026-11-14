#version 460

// The shadows of the players from the moving lights on every device (vk_rt.cpp, the shadow maps; rays to them cost
// over a millisecond with a saber in the hand): the mask (red and green: how much the players take of light 0 and 1),
// from cubes of six tiles around each light (a blade has three along it, one in each third, at a new place every frame).
// The light passes of the next frame read it (generic.frag DynamicLight).
//
// rows 38 + 2k: the start of light k and its reach (0: none), 39 + 2k: its end; rows 47 + 4 t: the view and
// projection of tile t, the cubes of light k in the tiles 16 + 18 k on, six each (+x, -x, +y, -y, +z, -z)

#include "lightmask.glsl"
