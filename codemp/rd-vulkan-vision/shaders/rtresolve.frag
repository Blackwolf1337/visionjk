#version 460

// cg_shadows 2 (vk_rt.cpp): the shadow mask of shaders/rtshadow.frag laid onto the view of the level, softened over
// the pixels around each one that lie on the same surface by their depth, so the few rays a pixel had become a soft
// shadow. Multiplied into the frame.

layout( set = 0, binding = 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
	vec4	rt[38];
} globals;
layout( set = 1, binding = 0 ) uniform sampler2D u_mask;		// the shadow mask, read pixel for pixel
layout( set = 2, binding = 0 ) uniform sampler2D u_depth;	// the depth of the scene, read pixel for pixel

#define W	globals.rt

layout( location = 0 ) out vec4 o_color;

#include "scene.glsl"

#define TAPS	16
const vec2 DISK[TAPS] = vec2[TAPS](
	vec2( -0.94201624, -0.39906216 ), vec2( 0.94558609, -0.76890725 ), vec2( -0.09418410, -0.92938870 ),
	vec2( 0.34495938, 0.29387760 ), vec2( -0.91588581, 0.45771432 ), vec2( -0.81544232, -0.87912464 ),
	vec2( -0.38277543, 0.27676845 ), vec2( 0.97484398, 0.75648379 ), vec2( 0.44323325, -0.97511554 ),
	vec2( 0.53742981, -0.47373420 ), vec2( -0.26496911, -0.41893023 ), vec2( 0.79197514, 0.19090188 ),
	vec2( -0.24188840, 0.99706507 ), vec2( -0.81409955, 0.91437590 ), vec2( 0.19984126, 0.78641367 ),
	vec2( 0.14383161, -0.14100790 ) );

void main() {
	ivec2 size = textureSize( u_mask, 0 );
	float d0 = SceneDistance( gl_FragCoord.xy );
	// about two units of the level around the pixel (the rays of a few frames are in the mask already)
	float radius = clamp( 2.5 / ( d0 * W[5].w ) * W[8].w * 0.5, 1.0, 6.0 );
	float sum = texelFetch( u_mask, ivec2( gl_FragCoord.xy ), 0 ).r;
	float weights = 1.0;

	for ( int i = 0; i < TAPS; i++ ) {
		vec2 pixel = gl_FragCoord.xy + DISK[i] * radius;
		ivec2 at = clamp( ivec2( pixel ), ivec2( 0 ), size - 1 );
		float d = SceneDistance( pixel );
		float w = exp( -abs( d - d0 ) / ( 0.02 * d0 + 1.0 ) );

		sum += w * texelFetch( u_mask, at, 0 ).r;
		weights += w;
	}
	o_color = vec4( vec3( 1.0 - sum / weights ), 1.0 );
}
