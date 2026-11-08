#version 460

// v_smaa, first pass: where the brightness of the frame jumps between neighbours. Pixels without an edge are
// discarded and stay 0 (the target is cleared).

#include "smaa.glsl"

layout( set = 0, binding = 0 ) uniform sampler2D u_color;

void main() {
	vec4 offset[3];

	SMAAEdgeDetectionVS( v_texCoord, offset );
	o_color = vec4( SMAALumaEdgeDetectionPS( v_texCoord, offset, u_color ), 0.0, 0.0 );
}
