#version 460

// The scope of a weapon (VK_ScopePass), the part that reads the depth of the scene, at half the size of the frame:
// how far out of focus a pixel is, and where the level has an edge. u_image is not read. PARM: xy: the depth of a
// pixel to its distance from the eye, x / ( depth + y ), as in focus.frag. zw: the middle of the lens in texture coordinates, which is focused on.

#include "post.glsl"

BINDING( 1, 0 ) uniform sampler2D u_depth;

float Distance( vec2 at ) {
	return PARM.x / ( texture( u_depth, at ).r + PARM.y );
}

void main() {
	vec2 texel = 1.0 / vec2( textureSize( u_depth, 0 ) );
	float dist = Distance( v_texCoord );

	// The lens focuses on what the crosshair is on: the nearest of five samples there, so a thin pole or a player
	// in front of a wall is what is sharp.
	float focus = Distance( PARM.zw );
	focus = min( focus, Distance( PARM.zw + vec2( 6.0, 0.0 ) * texel ) );
	focus = min( focus, Distance( PARM.zw - vec2( 6.0, 0.0 ) * texel ) );
	focus = min( focus, Distance( PARM.zw + vec2( 0.0, 6.0 ) * texel ) );
	focus = min( focus, Distance( PARM.zw - vec2( 0.0, 6.0 ) * texel ) );
	// a share of the distance in focus, so far away more of the depth is sharp, as with a real lens
	float blur = clamp( abs( dist - focus ) / ( focus * 0.3 + 48.0 ), 0.0, 1.0 );

	// An edge of the level is where the distance jumps against the neighbours. In logarithms, so an edge far away
	// counts as much as one near by.
	float here = log2( max( dist, 1.0 ) );
	float jump = abs( log2( max( Distance( v_texCoord + vec2( texel.x, 0.0 ) ), 1.0 ) ) - here )
		+ abs( log2( max( Distance( v_texCoord - vec2( texel.x, 0.0 ) ), 1.0 ) ) - here )
		+ abs( log2( max( Distance( v_texCoord + vec2( 0.0, texel.y ) ), 1.0 ) ) - here )
		+ abs( log2( max( Distance( v_texCoord - vec2( 0.0, texel.y ) ), 1.0 ) ) - here );
	float edge = smoothstep( 0.04, 0.3, jump );

	o_color = vec4( blur, edge, 0.0, 1.0 );
}
