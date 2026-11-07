#version 460

// One pass of the blur of the glow: four samples around the pixel, as the vertex and fragment programs of
// rd-vanilla take them (tr_arb.cpp). Each pass moves the samples further out. PARM: xy: how far out the samples
// are, in texture coordinates. z: the weight of each. w: above 0, only what is brighter than that counts, the
// first pass of tay_bloom (VK_Bloom).

#include "post.glsl"

vec3 bright( vec2 at ) {
	vec3 color = texture( u_image, at ).rgb;

	if ( PARM.w > 0.0 ) {
		color = max( color - vec3( PARM.w ), vec3( 0.0 ) ) / ( 1.0 - PARM.w );
	}
	return color;
}

void main() {
	vec2 o = PARM.xy;
	vec3 color = bright( v_texCoord + vec2( -o.x, -o.y ) ) + bright( v_texCoord + vec2( -o.x, o.y ) )
		+ bright( v_texCoord + vec2( o.x, -o.y ) ) + bright( v_texCoord + o );

	o_color = vec4( color * PARM.z, 1.0 );
}
