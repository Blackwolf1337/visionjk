#version 460

// cg_shadows 2 (vk_rt.cpp): the shadow mask of this frame (shaders/rtshadow.frag or smshadow.frag: the mask in red,
// how far the point is from the eye in green, blue and alpha) laid onto the mask of the frame before, so the few rays
// a pixel has become many over a few frames. The point of a pixel is looked up where it was in the view of the frame
// before, and if that pixel saw the same point (by its distance) a fifth of this frame goes onto it.
//
// What the frame before had may only lie within what the pixels around this one have now (3 x 3): a shadow that moved
// is gone at once where it was and there at once where it came to. Laid on as it was, the shadow of a player who walked
// took about ten frames to follow him and smeared behind (2026-11-08). Where a pixel and its neighbours disagree (the
// soft edge, the grain of the few rays) the range is wide and the frames before still smooth it.

layout( set = 0, binding = 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
	vec4	rt[319];
} globals;
layout( set = 1, binding = 0 ) uniform sampler2D u_raw;		// the mask of this frame, read pixel for pixel
layout( set = 2, binding = 0 ) uniform sampler2D u_depth;	// the depth of the scene, read pixel for pixel
layout( set = 3, binding = 0 ) uniform sampler2D u_history;	// the mask of the frame before, read pixel for pixel

#define W		globals.rt

layout( location = 0 ) out vec4 o_color;

#include "scene.glsl"

float UnpackDistance( vec3 c ) {
	uvec3 b = uvec3( round( c * 255.0 ) );

	return float( ( b.x << 16u ) | ( b.y << 8u ) | b.z ) / 16777215.0 * 8192.0;
}

void main() {
	ivec2 pixel = ivec2( gl_FragCoord.xy );
	ivec2 size = textureSize( u_raw, 0 );
	vec4 now = texelFetch( u_raw, pixel, 0 );
	float mask = now.r;

	// the range of this frame around the pixel
	float lo = mask, hi = mask;
	for ( int y = -1; y <= 1; y++ ) {
		for ( int x = -1; x <= 1; x++ ) {
			float v = texelFetch( u_raw, clamp( pixel + ivec2( x, y ), ivec2( 0 ), size - 1 ), 0 ).r;
			lo = min( lo, v );
			hi = max( hi, v );
		}
	}

	// onto what the frame before had for this point, where it saw it
	if ( W[20].w > 0.0 ) {
		vec3 p = ScenePoint( gl_FragCoord.xy );
		vec4 clip = mat4( W[16], W[17], W[18], W[19] ) * vec4( p, 1.0 );

		if ( clip.w > 1.0 ) {
			vec2 ndc = clip.xy / clip.w;
			vec4 rect = W[8];
			ivec2 at = ivec2( floor( vec2( rect.x + ( ndc.x * 0.5 + 0.5 ) * rect.z, rect.y + ( 0.5 - ndc.y * 0.5 ) * rect.w ) ) );

			if ( all( greaterThanEqual( at, ivec2( rect.xy ) ) ) && all( lessThan( at, ivec2( rect.xy + rect.zw ) ) ) ) {
				vec4 before = texelFetch( u_history, at, 0 );
				float then = distance( p, W[20].xyz );

				if ( abs( UnpackDistance( before.gba ) - then ) < then * 0.02 + 2.0 ) {
					mask = mix( clamp( before.r, lo, hi ), mask, 0.2 );
				}
			}
		}
	}
	o_color = vec4( mask, now.gba );
}
