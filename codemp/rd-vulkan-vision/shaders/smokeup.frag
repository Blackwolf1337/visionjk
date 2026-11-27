#version 460

// The smoke of the level as a volume (shaders/smoke.frag, marched at half the size of the frame into u_smoke) laid over
// the solid level: each pixel of the frame takes the pixels of the small picture around it that lie as far from the eye
// as it does, so the smoke behind a player does not run over his outline, and it blurs them a little, which takes
// the grain of the marching out. As shaders/rtlightresolve.frag does for the shadows of the moving lights.
// W (globals.smoke): rows 0-10 the view; W[356].x: how many pixels of the frame a pixel of u_smoke stands for.

layout( set = 0, binding = 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
	vec4	rt[382];
	vec4	lights[34];
	vec4	ssr[7];
	vec4	glass[10];		// the views of what lies behind thick glass (VK_GlassRows of vk_post.cpp, VK_GLASS_VEC4S)
	vec4	blades[247];
	vec4	smoke[357];
} globals;
layout( set = 1, binding = 0 ) uniform sampler2D u_smoke;	// the smoke on the way and how much of the frame it hides
layout( set = 2, binding = 0 ) uniform sampler2D u_depth;	// the depth of the scene, read pixel for pixel

#define W		globals.smoke

layout( location = 0 ) out vec4 o_color;

#include "scene.glsl"

void main() {
	float scale = max( W[356].x, 1.0 );
	float d0 = SceneDistance( gl_FragCoord.xy );
	// where the pixel lies among those of the small picture (the middle of its pixel k is at k)
	vec2 at = gl_FragCoord.xy / scale - 0.5;
	ivec2 base = ivec2( floor( at ) );
	ivec2 last = textureSize( u_smoke, 0 ) - 1;
	vec4 sum = vec4( 0.0 );
	float weights = 0.0;

	for ( int y = -1; y <= 2; y++ ) {
		for ( int x = -1; x <= 2; x++ ) {
			ivec2 texel = clamp( base + ivec2( x, y ), ivec2( 0 ), last );
			vec2 off = vec2( base + ivec2( x, y ) ) - at;
			float d = SceneDistance( ( vec2( texel ) + 0.5 ) * scale );
			float w = exp( -dot( off, off ) * 0.7 ) * exp( -abs( d - d0 ) / ( 0.03 * d0 + 2.0 ) );

			sum += texelFetch( u_smoke, texel, 0 ) * w;
			weights += w;
		}
	}
	// (nothing around it at its depth: the nearest as it is)
	o_color = weights > 1e-4 ? sum / weights : texelFetch( u_smoke, clamp( ivec2( at + 0.5 ), ivec2( 0 ), last ), 0 );
	if ( o_color.a < 0.002 ) {
		discard;
	}
}
