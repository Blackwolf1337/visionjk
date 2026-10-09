#version 460

// The finished frame goes through this once. It is what the gamma ramp of the display does for rd-vanilla:
// r_gamma, then the overbright bits as a shift. PARM: x: 1 / r_gamma, y: 2 ^ overbright bits.

#include "post.glsl"

void main() {
	vec3 color = texture( u_image, v_texCoord ).rgb;

	o_color = vec4( min( pow( color, vec3( PARM.x ) ) * PARM.y, vec3( 1.0 ) ), 1.0 );
}
