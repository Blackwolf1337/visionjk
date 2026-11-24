#version 460

// The shadows from the moving lights on every device, of the players and, without rays, of the level (vk_rt.cpp, the
// shadow maps; rays to the players cost
// over a millisecond with a saber in the hand): the mask (red to alpha: how much the players take of lights 0 to 3),
// from cubes of six tiles around each light (a blade has three along it, one in each third, at a new place every frame).
// The light passes of the next frame read it (generic.frag DynamicLight).
//
// rows 38 + 2k (lights 2 and 3: 303 + 2 (k - 2)): the start of light k and its reach (0: none), the next row its end;
// rows 47 + 4 t: the view and projection of tile t. The cubes of lights 0 and 1 (a blade or a point) in the tiles
// 16 + 18 k on, of the points 2 and 3 in 52 + 6 (k - 2) on, six each (+x, -x, +y, -y, +z, -z)

#include "lightmask.glsl"
