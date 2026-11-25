#version 460

// The mask of the shadows from the moving lights (shaders/lightmask.glsl, drawn at the size v_rtMaskScale says) brought
// to the size of the frame for the light passes that read it pixel for pixel (generic.frag DynamicLight) and for
// shaders/rtresolve.frag: each pixel takes the pixels of the small mask around it that lie on the same surface by
// their depth, as rtresolve.frag does for the players' own shadows. Read without that, a player in the shadow of a
// pillar had a dark rim against the lit floor behind him and the edge of the shadow went in steps (their picture
// 2026-11-26); drawn at the full size instead it cost four times the rays. The edges come out soft.
// Both pictures are two masks high: lights 0 to 3 above, 4 to 7 below (vk_rt.cpp). W[381]: the height of one small
// mask, how many pixels of the frame a pixel of it stands for, -, the height of one mask here.

layout( set = 0, binding = 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
	vec4	rt[382];
} globals;
layout( set = 1, binding = 0 ) uniform sampler2D u_raw;		// the small mask, read pixel for pixel
layout( set = 2, binding = 0 ) uniform sampler2D u_depth;	// the depth of the scene, read pixel for pixel

#define W	globals.rt

layout( location = 0 ) out vec4 o_color;

#include "scene.glsl"

void main() {
	float fullHeight = W[381].w, scale = max( W[381].y, 1.0 );
	int rawHeight = int( W[381].x + 0.5 );
	bool lower = gl_FragCoord.y >= fullHeight;
	vec2 pixel = lower ? gl_FragCoord.xy - vec2( 0.0, fullHeight ) : gl_FragCoord.xy;

	// (the lights are filled from the front: no light 4, none after it)
	if ( lower && W[372].w <= 0.0 ) {
		o_color = vec4( 0.0 );
		return;
	}
	// where the pixel lies among those of the small mask: pixel i of it stands for the pixel i * scale of the frame
	vec2 at = ( pixel - 0.5 ) / scale;
	ivec2 middle = ivec2( floor( at + 0.5 ) );
	ivec2 last = ivec2( textureSize( u_raw, 0 ).x - 1, rawHeight - 1 );
	ivec2 row = ivec2( 0, lower ? rawHeight : 0 );
	// Most of the picture is far from any edge of a shadow: where the pixels at the corners say what the one in the
	// middle says there is nothing to soften, and nothing to ask the depth for (the pass cost 1.5 ms of a frame without
	// this, most of what the shadows of the moving lights cost).
	vec4 centre = texelFetch( u_raw, clamp( middle, ivec2( 0 ), last ) + row, 0 );
	vec4 low = centre, high = centre;
	for ( int i = 0; i < 4; i++ ) {
		vec4 corner = texelFetch( u_raw, clamp( middle + ivec2( ( i & 1 ) * 2 - 1, ( i >> 1 ) * 2 - 1 ), ivec2( 0 ), last ) + row, 0 );

		low = min( low, corner );
		high = max( high, corner );
	}
	vec4 spread = high - low;
	if ( max( max( spread.x, spread.y ), max( spread.z, spread.w ) ) < 0.02 ) {
		o_color = centre;
		return;
	}
	float d0 = SceneDistance( pixel );
	vec4 sum = vec4( 0.0 );
	float weights = 0.0;

	for ( int y = -1; y <= 1; y++ ) {
		for ( int x = -1; x <= 1; x++ ) {
			ivec2 texel = clamp( middle + ivec2( x, y ), ivec2( 0 ), last );
			vec2 off = vec2( texel ) - at;
			float d = SceneDistance( vec2( texel ) * scale + 0.5 );
			float w = exp( -dot( off, off ) * 0.9 ) * exp( -abs( d - d0 ) / ( 0.02 * d0 + 1.0 ) );

			sum += texelFetch( u_raw, texel + row, 0 ) * w;
			weights += w;
		}
	}
	// (nothing around it on its surface: the nearest as it is)
	o_color = weights > 1e-4 ? sum / weights : centre;
}
