#version 460

// One pass of the blur of the glow: four samples around the pixel, as the vertex and fragment programs of
// rd-vanilla take them (tr_arb.cpp). Each pass moves the samples further out. PARM: xy: how far out the samples
// are, in texture coordinates. z: the weight of each. w: only what a sample is brighter than this is taken,
// stretched so that white stays white: the first pass of a bloom (VK_BloomFrame). 0: all of it.

#include "post.glsl"

void main() {
	vec2 o = PARM.xy;
	vec3 dark = vec3( PARM.w );
	vec3 color = max( texture( u_image, v_texCoord + vec2( -o.x, -o.y ) ).rgb - dark, 0.0 )
		+ max( texture( u_image, v_texCoord + vec2( -o.x, o.y ) ).rgb - dark, 0.0 )
		+ max( texture( u_image, v_texCoord + vec2( o.x, -o.y ) ).rgb - dark, 0.0 )
		+ max( texture( u_image, v_texCoord + o ).rgb - dark, 0.0 );

	o_color = vec4( color * PARM.z / ( 1.0 - PARM.w ), 1.0 );
}
