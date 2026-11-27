#version 460

// The openings of the portals of the portal gun (vk_portal.cpp VK_PortalDraw, cgame/portal/cg_portal_draw.c). One pass
// over the part of the view the portals cover, after the solid level: a pixel finds the portals its ray meets inside
// their ellipse with nothing solid in front (the wall they sit on lies at that distance, give or take the depth), and
// takes the nearest. Inside it shows the picture of the view through the portal made for the view being drawn
// (R_PortalViews of tr_main.cpp), read where that view saw the point: the pixel itself, the picture was drawn for the
// same eye in the same frame. A rim of the colour of the portal runs round the ellipse, a few units in and out of it,
// and flickers along it; a portal without a picture (closed, too deep, or not among the nearest) shows a swirl of its
// colour turning in.
//
// W (globals.portal): rows 0-10 the view as the passes over the level have it (VK_ViewRows); row 11 how many portals,
// the seconds, 1 for v_portalDebug 2; then ten rows a portal: its middle / half its width, its left / half its height,
// its up / the entry of its picture in the texture table + 1 (0 none), its colour / a number of its own, the matrix
// of the view its picture was drawn for (four rows), that view's rect in pixels (left, top, width, height), the part of
// the frame its picture holds (left, top, right, bottom over the size of the frame).

#extension GL_EXT_nonuniform_qualifier : require

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
	vec4	smoke[359];
	vec4	ripple[256];
	vec4	glassDebug[12];
	vec4	portal[172];	// VK_PORTAL_VEC4S of vk_local.h
} globals;
layout( set = 1, binding = 0 ) uniform sampler2D u_depth;	// the depth of the scene, read pixel for pixel
layout( set = 3, binding = 0 ) uniform sampler2D u_textures[];

#define W		globals.portal

layout( location = 0 ) out vec4 o_color;

#include "noise.glsl"
#include "scene.glsl"

#define HEAD			11
#define PORTAL( i )		( 12 + ( i ) * 10 )
#define RIM_IN			3.0		// how far into the ellipse the rim covers the picture, in units
#define RIM_OUT			6.0		//	and how far out of it it glows (PORTAL_RIM of vk_portal.cpp)
#define RIM_BRIGHT		1.6		// the colour of the rim times this, light above white where the frame keeps it (v_hdr)
#define FACE_RELIEF		8.0		// PORTAL_FACE_RELIEF of rd-common/tr_types.h

void main() {
	vec3 eye = W[4].xyz;
	vec3 ray = SceneRay( gl_FragCoord.xy );
	float scene = SceneDistance( gl_FragCoord.xy );
	int count = int( W[HEAD].x );
	float time = W[HEAD].y;
	int best = -1;
	float bestT = 1e30;
	vec2 bestAt = vec2( 0.0 );
	float bestEdge = 0.0;

	// v_portalDebug 2: the part of the frame each picture holds, as a frame of the colour of its portal
	if ( W[HEAD].z > 0.5 ) {
		vec2 uv = gl_FragCoord.xy / globals.screen.xy, pixel = 2.0 / globals.screen.xy;

		for ( int i = 0; i < count; i++ ) {
			vec4 part = W[PORTAL( i ) + 9];

			if ( W[PORTAL( i ) + 2].w > 0.5 && all( greaterThan( uv, part.xy - pixel ) ) && all( lessThan( uv, part.zw + pixel ) )
				&& !( all( greaterThan( uv, part.xy + pixel ) ) && all( lessThan( uv, part.zw - pixel ) ) ) ) {
				o_color = vec4( W[PORTAL( i ) + 3].rgb, 1.0 );
				return;
			}
		}
	}

	for ( int i = 0; i < count; i++ ) {
		int b = PORTAL( i );
		vec3 origin = W[b].xyz, left = W[b + 1].xyz, up = W[b + 2].xyz;
		// (forward, out of the wall: left x up, as AnglesToAxis makes the three)
		vec3 normal = cross( left, up );
		float along = dot( normal, ray );

		// (the ray goes into the face, or the opening is not seen from here)
		if ( along > -1e-5 ) {
			continue;
		}
		// how far along the ray the plane is (the ray is one unit long in front of the eye, as the scene counts)
		float t = dot( origin - eye, normal ) / along;

		// Behind the eye, behind a nearer portal, or something solid in front of the opening. In front is what stands out
		// of the face by more than FACE_RELIEF: up to that the bumps of a tessellated face lie, which are no part of the
		// brush the portal was set on and covered it in patches (their picture 2026-11-27); a player that close is drawn
		// in the picture as well (cg_portal_draw.c). An eye right at the face has the wall cut away by the near plane,
		// and sees through all the same.
		if ( t <= 0.0 || t >= bestT || dot( eye + ray * scene - origin, normal ) > FACE_RELIEF + scene * 0.004 ) {
			continue;
		}
		vec3 d = eye + ray * t - origin;
		vec2 at = vec2( dot( d, left ) / W[b].w, dot( d, up ) / W[b + 1].w );
		float r = length( at );
		// how far inside the ellipse the point is, in units, about: along the way out from its middle, of the half size
		// of the ellipse that way
		float reach = r > 1e-4 ? length( at * vec2( W[b].w, W[b + 1].w ) ) / r : min( W[b].w, W[b + 1].w );
		float edge = ( 1.0 - r ) * reach;

		if ( edge < -RIM_OUT ) {
			continue;
		}
		best = i;
		bestT = t;
		bestAt = at;
		bestEdge = edge;
	}
	if ( best < 0 ) {
		discard;
	}

	int b = PORTAL( best );
	vec3 color = W[b + 3].rgb;
	float seed = W[b + 3].w;
	uint picture = uint( W[b + 2].w + 0.5 );
	float r = length( bestAt );
	vec2 round_ = r > 1e-4 ? bestAt / r : vec2( 1.0, 0.0 );
	vec3 inside = vec3( 0.0 );
	bool shown = false;

	if ( picture > 0u && bestEdge > -1.0 ) {
		vec4 clip = mat4( W[b + 4], W[b + 5], W[b + 6], W[b + 7] ) * vec4( eye + ray * bestT, 1.0 );

		if ( clip.w > 1e-3 ) {
			vec4 rect = W[b + 8], part = W[b + 9];
			vec2 ndc = clip.xy / clip.w;
			vec2 uv = ( rect.xy + vec2( ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5 ) * rect.zw ) / globals.screen.xy;

			inside = texture( u_textures[nonuniformEXT( picture - 1u )], clamp( uv, part.xy, part.zw ) ).rgb;
			shown = true;
		}
	}
	if ( !shown ) {
		// the swirl: arms of its colour that turn in towards the dark middle (from the way round and the way out, so
		// it has no seam where the angle goes round)
		float turn = r * 5.0 - time * 1.7 + seed * 6.2831853;
		vec2 arm = vec2( round_.x * cos( turn ) - round_.y * sin( turn ), round_.x * sin( turn ) + round_.y * cos( turn ) );
		float arms = Fbm( arm * 1.8 + vec2( seed * 13.0, r * 2.0 - time * 0.6 ) );

		inside = color * ( 0.05 + 0.75 * smoothstep( 0.35, 0.85, arms ) * ( 0.25 + r * r ) );
	}
	// the rim: brightest at the edge of the ellipse, its width flickering along it and over time
	float flicker = Noise( round_ * 2.5 + vec2( time * 1.3, seed * 7.0 - time * 0.8 ) );
	float glow = exp( -abs( bestEdge ) / mix( 1.0, 2.8, flicker ) );

	if ( bestEdge >= 0.0 ) {
		float rim = 1.0 - smoothstep( 0.0, RIM_IN, bestEdge );

		o_color = vec4( mix( inside, color * RIM_BRIGHT, rim * 0.85 ) + color * glow * 0.4, 1.0 );
	} else {
		float alpha = glow * ( 1.0 - smoothstep( RIM_OUT * 0.5, RIM_OUT, -bestEdge ) );

		if ( alpha < 0.004 ) {
			discard;
		}
		o_color = vec4( color * RIM_BRIGHT, alpha );
	}
}
