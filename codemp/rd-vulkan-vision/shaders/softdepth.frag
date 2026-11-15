#version 460

// v_softParticles (vk_post.cpp VK_SoftDepth): how far in front of the eye the solid level is at each pixel, into a
// target of its own once the solid level of the view is drawn. The sprites of the effects that come after it read it
// (generic.frag SoftFade) and fade out where they would cut into a wall or the floor. PARM: xy the depth of a pixel to
// its distance, x / ( depth + y ), as the depth of field has it.

#include "post.glsl"

BINDING( 1, 0 ) uniform sampler2D u_depth;

void main() {
	float depth = texelFetch( u_depth, ivec2( gl_FragCoord.xy ), 0 ).r;

	o_color = vec4( PARM.x / min( depth + PARM.y, -1e-7 ), 0.0, 0.0, 1.0 );
}
