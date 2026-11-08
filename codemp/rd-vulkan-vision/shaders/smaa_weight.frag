#version 460

// v_smaa, second pass: for every pixel on an edge, how much of its neighbours it takes, from the shape of the
// edge (how long, which way it turns) looked up in the two textures of SMAA.

#include "smaa.glsl"

layout( set = 0, binding = 0 ) uniform sampler2D u_edges;
layout( set = 1, binding = 0 ) uniform sampler2D u_area;
layout( set = 2, binding = 0 ) uniform sampler2D u_search;

void main() {
	vec2 pixcoord;
	vec4 offset[3];

	SMAABlendingWeightCalculationVS( v_texCoord, pixcoord, offset );
	o_color = SMAABlendingWeightCalculationPS( v_texCoord, pixcoord, offset, u_edges, u_area, u_search, vec4( 0.0 ) );
}
