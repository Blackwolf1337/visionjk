#version 460

// Saber blades in 3D (vk_post.cpp VK_SaberBlades, ext.AddSaberBladeToScene): over the view of the level, before the
// screen reflections and the bloom, laid onto the frame. Each pixel takes the nearest the ray from the eye through it
// comes to each blade, in the space of the level and not on the screen: a white hot core where it passes within the
// core, a glow in the colour of the blade falling off round it. From the side a blade is a line, seen along it a round
// glowing point; the cgame had drawn two flat strips turned to the eye that fell apart seen along the blade
// (2026-11-08, movie style on their choice). What lies in front of the blade (by the depth of the scene) hides it. The
// core is brighter than white for the bloom of the HDR frame.
//
// globals.blades: 0-10 the view as VK_ViewRows puts it, 11 x how many blades; three rows a blade: its start and the
// radius of the core, its end and the radius of the glow, its colour

layout( set = 0, binding = 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
	vec4	rt[319];
	vec4	lights[34];
	vec4	ssr[7];
	vec4	blades[108];
} globals;
layout( set = 2, binding = 0 ) uniform sampler2D u_depth;	// the depth of the scene, read pixel for pixel

#define W		globals.blades

layout( location = 0 ) out vec4 o_color;

#include "scene.glsl"

void main() {
	vec3 eye = W[4].xyz;
	vec3 p = ScenePoint( gl_FragCoord.xy );
	float sceneDist = distance( p, eye );
	vec3 dir = ( p - eye ) / max( sceneDist, 1e-4 );
	// how large a pixel is one unit before the eye (for the soft edge of the core)
	float pixel = 2.0 * W[5].w / W[8].w;
	int count = min( int( W[11].x ), 32 );
	vec3 light = vec3( 0.0 );

	for ( int i = 0; i < count; i++ ) {
		vec4 a = W[12 + i * 3], b = W[13 + i * 3], c = W[14 + i * 3];
		vec3 seg = b.xyz - a.xyz;
		float len2 = max( dot( seg, seg ), 1e-4 );
		// the nearest points of the ray (eye + dir t) and the blade (a + seg u)
		vec3 r = eye - a.xyz;
		float bd = dot( dir, seg ), dd = dot( dir, r ), ee = dot( seg, r );
		float denom = len2 - bd * bd;
		float u = denom > 1e-4 ? clamp( ( ee - bd * dd ) / denom, 0.0, 1.0 ) : 0.5;
		float t = max( dot( a.xyz + seg * u - eye, dir ), 0.0 );
		u = clamp( dot( eye + dir * t - a.xyz, seg ) / len2, 0.0, 1.0 );
		t = max( dot( a.xyz + seg * u - eye, dir ), 0.0 );
		float d = distance( eye + dir * t, a.xyz + seg * u );
		// hidden where that point lies behind what the pixel shows
		float seen = 1.0 - smoothstep( sceneDist - 1.0, sceneDist + 2.0, t );
		if ( seen <= 0.0 ) {
			continue;
		}
		// (at least a pixel of core and a few of glow, so a blade far away does not get lost)
		float px = pixel * max( t, 1.0 );
		float core = max( a.w, px * 0.6 ), glow = max( b.w, px * 3.0 );
		float hot = 1.0 - smoothstep( core - px, core + px, d );
		float inner = exp( -3.0 * ( d * d ) / ( glow * glow ) );
		float outer = exp( -d / ( glow * 1.5 ) ) * 0.2;
		light += ( c.rgb * ( inner * 1.1 + outer ) + mix( c.rgb, vec3( 1.0 ), 0.85 ) * hot * 2.5 ) * seen;
	}
	o_color = vec4( light, 0.0 );
}
