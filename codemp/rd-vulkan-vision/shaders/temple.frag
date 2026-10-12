#version 460
// The Vision Temple (RE_SetTempleScene, focus/cg_focus.c of the cgame): a place above the clouds that is worked out
// here for every pixel, nothing of it is a model or a texture. A sea of clouds with a clearing in it, in the
// clearing a round podium of gold with groups of columns around it and a way that leads to it, rings of light,
// and a low sun behind the greatest of them. The pass fills the frame of a view without a level before the
// things of that view are drawn, so what the cgame adds (the others, as wisps) lies on it.
//
// Lengths are units of the game, z is up, the sun stands in +x. The cgame walks on the same places: the podium and
// how far one can go are numbers in focus/cg_focus.c that have to match the ones here (podium(), cloudTop()).
//
// How it is made, second version (2026-10-12, the first one looked cheap: fog for clouds, gold without a
// highlight, rings that were white bands):
// - The clouds are a volume. Their tops are heights worked out from folded noise, a ray is marched to where it
//   enters them (in steps as long as what is left above the tops allows) and then through them in short steps,
//   each lit by whether the way to the sun is free a little further on, until nothing shows through any more.
// - The gold is a metal: a highlight of the sun by its roughness, the sky and the clouds mirrored in it, the
//   light of the rings on it, shadows the columns throw on each other, and dark where things meet. Its surface
//   has flutes and engraved lines that only the shading knows.
// - A ring is strands of light with heads that run around it and tails behind them, and a glow around all of it.
// - What is far goes over into a haze, bright things run out softly (a film curve).
//
// VIEW: 0: xyz the eye, w seconds. 1: xyz where it looks, w how much of the place is there, 0 to 1 (it comes out
// of light). 2: xyz to the right, as long as the tangent of half the angle of view, w how much is worked out
// (r_templeDetail: 2 all of it, 1 less of what costs most, 0 the least that still is the place). 3: xyz up,
// as long as the tangent of half the angle of view, w 1: no clouds here, the level has them as a volume of its
// own (volume.frag). 4: x and y give the depth of a thing d ahead as the view's
// projection does, x - y / d: what stands in the place (player models) is drawn after it with that depth. z 1:
// no gold and no light of the rings here either: the level has the gold as surfaces (generic.frag) and the
// light as a volume that comes after them, and this pass is the sky behind it all.
#include "dialect.glsl"

#ifdef VK_GLES
uniform vec4 u_view[5];
#define VIEW( i )	u_view[i]
#else
layout( push_constant ) uniform Push {
	vec4	view[5];
} push;
#define VIEW( i )	push.view[i]
#endif

VARYING( 0 ) in vec2 v_texCoord;
layout( location = 0 ) out vec4 o_color;


// the clouds (vision.glsl): they leave the terrace around the podium and the way to it free
#define CLOUD_ROOF	266.0
#define CLOUD_DISC	vec3( 0.0, 0.0, 208.0 )
#define CLOUD_LANE	vec3( -524.0, -170.0, 28.0 )
#include "vision.glsl"
#include "light.glsl"
#include "gold.glsl"

/*
===============================================================================

A pixel

===============================================================================
*/

void main() {
	float seconds = VIEW( 0 ).w;
	vec3 ro = VIEW( 0 ).xyz;
	vec2 at = vec2( v_texCoord.x * 2.0 - 1.0, 1.0 - v_texCoord.y * 2.0 );
	vec3 rd = normalize( VIEW( 1 ).xyz + at.x * VIEW( 2 ).xyz + at.y * VIEW( 3 ).xyz );
	// another number for every pixel, for the steps through the clouds
	float jitter = fract( 52.9829189 * fract( dot( gl_FragCoord.xy, vec2( 0.06711056, 0.00583715 ) ) ) );
	float t = 0.0;
	float tEnd = 1.0e9;
	int what = 0;				// 1: the ray ends on gold
	float ringNear = 1.0e9;		// how near the ray came to the line of a ring
	// the level has the gold and the light itself: the sky alone
	bool level = VIEW( 4 ).z > 0.5;

	// The gold and the rings stand inside a ball around the temple: a ray that misses it is not marched at all,
	// one that meets it only through it.
	vec3 mid = ro - vec3( 60.0, 0.0, 150.0 );
	float b = dot( mid, rd );
	float inBall = b * b - dot( mid, mid ) + 650.0 * 650.0;

	if ( inBall > 0.0 && !level ) {
		float tOut = -b + sqrt( inBall );

		t = max( -b - sqrt( inBall ), 0.0 );
		for ( int i = 0; i < 90; i++ ) {
			if ( t > tOut ) {
				break;
			}
			vec3 p = ro + rd * t;
			float dGold = gold( p );
			float dRing = rings( p );
			float d = min( dGold, dRing );

			ringNear = min( ringNear, dRing );
			if ( dGold < 0.03 + t * 0.0005 ) {
				what = 1;
				tEnd = t;
				break;
			}
			// (a ring is light: the ray goes through its line, in short steps while it is near it)
			t += min( max( d, 1.5 ), 80.0 );
		}
	}

	vec3 color;

	if ( what == 1 ) {
		color = goldLit( ro + rd * tEnd, rd, seconds, VIEW( 2 ).w );
		color = mix( color, haze( rd ), 1.0 - exp( -tEnd / 6000.0 ) );
	} else {
		color = sky( rd, seconds, true, VIEW( 2 ).w );
	}

	// the clouds in front of that, and the light of the rings in front of them
	// (not where the level has them as a volume of its own, which is drawn after what stands in it: volume.frag)
	vec4 sea = VIEW( 3 ).w > 0.5 ? vec4( 0.0, 0.0, 0.0, 1.0 ) : clouds( ro, rd, 0.0, tEnd, seconds, jitter, VIEW( 2 ).w );

	color = sea.rgb + sea.a * color;
	if ( !level ) {
		color += templeLight( ro, rd, tEnd, seconds, sea.a, ringNear );
	}

	// one arrives out of light
	color = mix( vec3( 7.0, 6.0, 4.4 ), color, smoothstep( 0.0, 1.0, VIEW( 1 ).w ) );
	// a little darker towards the corners
	color *= 1.0 - 0.22 * dot( at, at );
	color = film( color * 0.62 );
	color += ( hash( gl_FragCoord.xy ) - 0.5 ) / 255.0;
	o_color = vec4( color, 1.0 );
	// the gold is in the way of what is drawn behind it, sky and clouds of nothing
	gl_FragDepth = what == 1 ? clamp( VIEW( 4 ).x - VIEW( 4 ).y / max( tEnd * dot( rd, VIEW( 1 ).xyz ), 0.001 ), 0.0, 1.0 ) : 1.0;
}
