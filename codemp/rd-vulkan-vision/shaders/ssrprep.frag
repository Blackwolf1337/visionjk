#version 460

// v_ssr (vk_post.cpp VK_SsrPrep): the level as it is drawn, with how far each pixel is in front of the eye in alpha,
// for the screen reflections of the next frame (generic.frag ScreenReflection). PARM: xy: the depth of a pixel to its
// distance, x / ( depth + y ), as the depth of field has it.

#include "post.glsl"

BINDING( 1, 0 ) uniform sampler2D u_depth;

void main() {
	ivec2 pixel = ivec2( gl_FragCoord.xy );
	float depth = texelFetch( u_depth, pixel, 0 ).r;

	o_color = vec4( texelFetch( u_image, pixel, 0 ).rgb, PARM.x / min( depth + PARM.y, -1e-7 ) );
}
