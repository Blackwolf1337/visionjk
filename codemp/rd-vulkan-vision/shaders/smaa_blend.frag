#version 460

// v_smaa, last pass: every pixel mixed with its neighbours by the weights of the pass before, read from a copy of
// the frame and written into the frame.

#include "smaa.glsl"

layout( set = 0, binding = 0 ) uniform sampler2D u_color;
layout( set = 1, binding = 0 ) uniform sampler2D u_weights;

void main() {
	vec4 offset;

	SMAANeighborhoodBlendingVS( v_texCoord, offset );
	o_color = SMAANeighborhoodBlendingPS( v_texCoord, offset, u_color, u_weights );
}
