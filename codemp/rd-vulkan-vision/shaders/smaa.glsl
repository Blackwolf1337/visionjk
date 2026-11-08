// What the three passes of v_smaa share (VK_SmaaPass of vk_cmd.cpp): SMAA 1x of lib/smaa, its preset High, with
// the threshold of its edges from the renderer. For the Vulkan device only: SMAA.hlsl is no GLSL ES.
//
// The passes run over the whole target with shaders/post.vert. What SMAA does in its vertex shaders (the places
// of the neighbours) is done in the fragment shaders here, from the same texture coordinate: a few additions a
// pixel. The texture coordinates have the top row at 0, as the targets have it, so SMAA reads its lookup textures
// the way it was made for (Direct3D has the same).
//
// push.metrics: 1 / width, 1 / height, width, height of the frame. push.parm.x: the threshold of the edges in
// the colours of the frame.

layout( push_constant ) uniform Push {
	vec4	metrics;
	vec4	parm;
} push;

#define SMAA_GLSL_4
#define SMAA_RT_METRICS			push.metrics
#define SMAA_THRESHOLD			push.parm.x
#define SMAA_MAX_SEARCH_STEPS	16
#define SMAA_MAX_SEARCH_STEPS_DIAG	8
#define SMAA_CORNER_ROUNDING	25
#include "SMAA.hlsl"

layout( location = 0 ) in vec2 v_texCoord;
layout( location = 0 ) out vec4 o_color;
