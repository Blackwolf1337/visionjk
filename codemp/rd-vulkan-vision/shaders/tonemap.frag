#version 460

// v_tonemap (vk_post.cpp VK_Tonemap): the view of the level, in the 16 bit frame of v_hdr where light above white
// stayed, onto the screen at the end of the view, before the 2D. It reads a copy of the frame (vk.screen) and writes
// the frame. The values are those of the game (not linear light) times 2 ^ overbright bits, as the gamma pass will
// show them; they go back in the same space.
//   1: the original look up to 0.6, above it a soft shoulder that reaches white only far above it: a saber, a glow,
//      several lights on one wall glow out instead of clipping
//   2: AgX (Sobotka; the polynomial fit of Wrensch 2023), the filmic curve of modern games, in linear light: richer
//      highlights, less saturated whites, a little more contrast
// push.parm: x the exposure (v_exposure), y the curve, z 2 ^ overbright bits.

layout( push_constant ) uniform Push {
	vec4	metrics;
	vec4	parm;
} push;

layout( set = 0, binding = 0 ) uniform sampler2D u_frame;

layout( location = 0 ) in vec2 v_texCoord;
layout( location = 0 ) out vec4 o_color;

vec3 Shoulder( vec3 x ) {
	const float k = 0.6;

	return mix( x, k + ( 1.0 - k ) * ( 1.0 - exp( -( x - k ) / ( 1.0 - k ) ) ), step( k, x ) );
}

vec3 AgxContrast( vec3 x ) {
	vec3 x2 = x * x, x4 = x2 * x2;

	return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

// linear light in, linear light for the display out
vec3 Agx( vec3 color ) {
	const mat3 inset = mat3( 0.842479062253094, 0.0423282422610123, 0.0423756549057051,
		0.0784335999999992, 0.878468636469772, 0.0784336,
		0.0792237451477643, 0.0791661274605434, 0.879142973793104 );
	const mat3 outset = mat3( 1.19687900512017, -0.0528968517574562, -0.0529716355144438,
		-0.0980208811401368, 1.15190312990417, -0.0980434501171241,
		-0.0990297440797205, -0.0989611768448433, 1.15107367264116 );
	const float minEv = -12.47393, maxEv = 4.026069;
	vec3 v = inset * max( color, vec3( 1e-10 ) );

	v = ( clamp( log2( v ), minEv, maxEv ) - minEv ) / ( maxEv - minEv );
	v = outset * AgxContrast( v );
	return pow( max( v, vec3( 0.0 ) ), vec3( 2.2 ) );
}

void main() {
	vec3 color = texture( u_frame, v_texCoord ).rgb * push.parm.z * push.parm.x;

	// (whatever else wrote a NaN into the frame, it goes no further)
	if ( any( isnan( color ) ) || any( isinf( color ) ) ) {
		color = vec3( 0.0 );
	}
	color = max( color, vec3( 0.0 ) );

	if ( push.parm.y > 1.5 ) {
		// (AgX puts the grey of a scene at 0.18 in linear light: the frame's 0.5 is about that)
		color = pow( Agx( pow( color, vec3( 2.2 ) ) * 1.0 ), vec3( 1.0 / 2.2 ) );
	} else {
		color = Shoulder( color );
	}
	o_color = vec4( color / push.parm.z, 1.0 );
}
