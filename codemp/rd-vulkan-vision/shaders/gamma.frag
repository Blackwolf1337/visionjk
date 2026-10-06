#version 460

// The finished frame goes through this once. It is what the gamma ramp of the display does for rd-vanilla:
// r_gamma, then the overbright bits as a shift.

layout( set = 0, binding = 0 ) uniform sampler2D u_frame;

layout( push_constant ) uniform Push {
	vec4	gamma;		// x: 1 / r_gamma, y: 2 ^ overbright bits
} push;

layout( location = 0 ) in vec2 v_texCoord;
layout( location = 0 ) out vec4 o_color;

void main() {
	vec3 color = texture( u_frame, v_texCoord ).rgb;

	o_color = vec4( min( pow( color, vec3( push.gamma.x ) ) * push.gamma.y, vec3( 1.0 ) ), 1.0 );
}
