#version 460

// The screen effects (VK_ScreenFxPass), the part that reads the depth of the scene, at half the size of the frame:
// the distance of every pixel from the eye, the nearest of the four it covers, as log2( distance ) / 16 in two
// channels of eight bits (r the upper part, g the lower), and in b the edges of the level. u_image is not read.
// PARM: xy: the depth of a pixel to its distance from the eye, x / ( depth + y ), as in focus.frag.

#include "post.glsl"

BINDING( 1, 0 ) uniform sampler2D u_depth;

float LogDistance( float depth ) {
	return log2( max( PARM.x / ( depth + PARM.y ), 1.0 ) );
}

void main() {
	vec2 texel = 0.5 / vec2( textureSize( u_depth, 0 ) );
	float d0 = texture( u_depth, v_texCoord + vec2( -texel.x, -texel.y ) ).r;
	float d1 = texture( u_depth, v_texCoord + vec2( texel.x, -texel.y ) ).r;
	float d2 = texture( u_depth, v_texCoord + vec2( -texel.x, texel.y ) ).r;
	float d3 = texture( u_depth, v_texCoord + vec2( texel.x, texel.y ) ).r;
	// the nearest of the four, so a wall in front of a source wins
	float depth = min( min( d0, d1 ), min( d2, d3 ) );
	float dist = PARM.x / ( depth + PARM.y );
	float t = clamp( log2( max( dist, 1.0 ) ) / 16.0, 0.0, 1.0 ) * 255.0;
	// The edges of the level for Force Sight, where the distance jumps within the pixels this one covers and the
	// ones next to them. In logarithms, so an edge far away counts as much as one near by. It is the one channel that
	// may be filtered, so its lines come out smooth.
	float l0 = LogDistance( d0 ), l1 = LogDistance( d1 ), l2 = LogDistance( d2 ), l3 = LogDistance( d3 );
	float l4 = LogDistance( texture( u_depth, v_texCoord + vec2( 3.0 * texel.x, 0.0 ) ).r );
	float l5 = LogDistance( texture( u_depth, v_texCoord - vec2( 3.0 * texel.x, 0.0 ) ).r );
	float l6 = LogDistance( texture( u_depth, v_texCoord + vec2( 0.0, 3.0 * texel.y ) ).r );
	float l7 = LogDistance( texture( u_depth, v_texCoord - vec2( 0.0, 3.0 * texel.y ) ).r );
	float high = max( max( max( l0, l1 ), max( l2, l3 ) ), max( max( l4, l5 ), max( l6, l7 ) ) );
	float low = min( min( min( l0, l1 ), min( l2, l3 ) ), min( min( l4, l5 ), min( l6, l7 ) ) );

	o_color = vec4( floor( t ) / 255.0, fract( t ), smoothstep( 0.08, 0.4, high - low ), 1.0 );
}
