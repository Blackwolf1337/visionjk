#version 460

// One pass of the blur of the glow: four samples around the pixel, as the vertex and fragment programs of
// rd-vanilla take them (tr_arb.cpp). Each pass moves the samples further out. PARM: xy: how far out the samples
// are, in texture coordinates. z: the weight of each.

#include "post.glsl"

void main() {
	vec2 o = PARM.xy;
	vec3 color = texture( u_image, v_texCoord + vec2( -o.x, -o.y ) ).rgb + texture( u_image, v_texCoord + vec2( -o.x, o.y ) ).rgb
		+ texture( u_image, v_texCoord + vec2( o.x, -o.y ) ).rgb + texture( u_image, v_texCoord + o ).rgb;

	o_color = vec4( color * PARM.z, 1.0 );
}
