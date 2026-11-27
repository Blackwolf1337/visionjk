#version 460

// Rings on the water of the level (wetSpot_t of the kind WET_SURFACE, vk_ripple.cpp VK_RippleDraw, cgame/level/
// cg_ripples.c): where somebody wades, where a shot goes in, where something blows up. The water of a level is a flat
// transparent surface that writes no depth, so the place of a pixel on it is not what the depth of the scene has
// there: it is where the ray of the pixel meets the height of the water, as long as nothing solid lies in front of
// that. One pass over the part of the view the rings cover, after everything transparent is drawn; each pixel adds up
// the slopes the rings near it give the water. With them the picture of the frame is read a little to the side, as
// water with a slope bends what is seen through it and what lies on it, the crests mirror more or less of the room
// than flat water does and catch the light of the level, and where the water was just churned up it froths. Without
// a slope nothing changes, so a ring has no edge.
//
// W (globals.ripple): rows 0-10 the view as the passes over the level have it; row 11: how many surfaces, how many
// rings, the seconds, how many pixels to the side the copy of the frame may be read; then a row a surface (its height, its first ring, how many); then five rows a ring: its
// middle (x, y), how far it runs, how strong / how far through its life (0 to 1), where round it there is water (16
// bits, RIPPLE_SECTORS of cg_ripples.c), its seed / the light of the level there: ambient / directed / from where.

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
} globals;
layout( set = 1, binding = 0 ) uniform sampler2D u_image;	// the copy of the frame
layout( set = 2, binding = 0 ) uniform sampler2D u_depth;	// the depth of the scene, read pixel for pixel
layout( set = 3, binding = 0 ) uniform sampler2D u_faces;	// W[9].w 1 or 2: how far the nearest thing is, glass or solid (vk_post.cpp VK_GlassMask)

#define W		globals.ripple

layout( location = 0 ) out vec4 o_color;

#include "noise.glsl"
// (W[9].z 1: u_depth is the distance of the solid level, so water that was made glass does not hide its rings)
#define SCENE_IS_DISTANCE	( W[9].z > 0.5 )
#include "scene.glsl"

#define SURFACES		4
#define SURFACE( s )	( 12 + ( s ) )
#define RING( i )		( 12 + SURFACES + ( i ) * 5 )
#define SLOPE			0.42	// how steep the water of a ring of full strength gets
#define BEND			0.9		// how far to the side what is seen is read, of the slope times the water behind
#define SHADE			2.0		// how much lighter and darker the two sides of a crest are, for a slope of one
#define GLINT			48.0	// how tight the light of the level is on a crest
#define FAR_OFF			2200.0	// rings further than this from the eye are not drawn, they fade from 0.7 of it on

// how much of ring i is at a place round its middle (d from it, r far): 1 where water is, 0 where the client game
// found none (the rim of a basin with air beyond it; land hides the ring by itself). Eight directions, each asked at
// half the reach and at all of it.
float Sector( uint bits, vec2 d, float r, float reach ) {
	float turn = atan( d.y, d.x ) * ( 8.0 / 6.2831853 );
	float f = fract( turn );
	uint i0 = uint( int( floor( turn ) ) & 7 ), i1 = ( i0 + 1u ) & 7u;
	float out_ = smoothstep( 0.4, 0.9, r / reach );
	float a = mix( float( ( bits >> i0 ) & 1u ), float( ( bits >> ( 8u + i0 ) ) & 1u ), out_ );
	float b = mix( float( ( bits >> i1 ) & 1u ), float( ( bits >> ( 8u + i1 ) ) & 1u ), out_ );

	return mix( a, b, smoothstep( 0.0, 1.0, f ) );
}

// What ring i does to the water at a place: xy the slope it gives it, z how much it froths there, w how much of the
// light of the ring belongs to the place. A ring is a few crests that run out from its middle, fast at first, and
// flatten: ahead of the front the water is still, behind it the crests die away slowly. wobble: a noise of the
// place, -1 to 1.
vec4 Ring( int i, vec2 at, float wobble ) {
	vec4 a = W[RING( i )], b = W[RING( i ) + 1];
	vec2 d = at - a.xy;
	float r2 = dot( d, d );

	if ( r2 >= a.z * a.z ) {
		return vec4( 0.0 );
	}
	// (no ring on real water is a circle: its crests wander a little, each ring its own way)
	float r = sqrt( r2 ) + wobble * ( 0.5 + b.z ) * min( a.z * 0.05, 4.0 );
	float life = b.x;
	float left = ( 1.0 - life ) * ( 1.0 - life );
	float front = a.z * 0.9 * ( 1.0 - left );
	float off = r - front;
	float width = 3.5 + a.z * 0.14 * life;
	float w = off > 0.0 ? width : width * 2.6;
	float packet = exp( -off * off / ( w * w ) );
	// (longer waves from what was larger)
	float k = 6.2831853 / ( 6.5 + a.z * 0.05 );
	float water = b.y > 65534.5 ? 1.0 : Sector( uint( b.y ), d, r, a.z );
	float amp = a.w * left * smoothstep( 0.0, 0.05, life ) * packet * ( 1.0 - smoothstep( 0.82, 1.0, r / a.z ) ) * water;
	// (no direction in the middle itself)
	vec2 slope = d / max( r, 1e-3 ) * smoothstep( 0.0, 2.5, r ) * cos( off * k + b.z * 6.2831853 ) * amp;
	// churned water where it started, for the first third of its life
	float churn = a.w * ( 1.0 - smoothstep( 0.0, 0.33, life ) ) * exp( -r2 / ( a.z * a.z * 0.035 + 30.0 ) ) * water;

	return vec4( slope, churn, max( 1.0 - r / a.z, 0.0 ) * a.w * left );
}

void main() {
	vec3 eye = W[4].xyz;
	vec3 ray = SceneRay( gl_FragCoord.xy );
	float scene = SceneDistance( gl_FragCoord.xy );
	float time = W[11].z;
	int surfaces = int( W[11].x );

	if ( abs( ray.z ) < 1e-6 ) {
		discard;
	}
	for ( int s = 0; s < surfaces; s++ ) {
		vec4 head = W[SURFACE( s )];
		// (the ray is one unit long in front of the eye, as the distances of the scene are counted)
		float t = ( head.x - eye.z ) / ray.z;

		if ( t <= 0.0 || t >= scene - 0.5 || t > FAR_OFF ) {
			continue;
		}
		// Thick glass in front of the water at the pixel (the nearest thing there is nearer than the water, as generic.frag
		// GlassOwn tells its own face): its rings are drawn before the glass, which bends them with the water (W[9].w
		// 1, vk_ripple.cpp RIPPLE_BEHIND_GLASS), the others after everything transparent (2)
		if ( W[9].w > 0.5 ) {
			bool behindGlass = texelFetch( u_faces, ivec2( gl_FragCoord.xy ), 0 ).r < t - ( 1.0 + t * 0.015 );

			if ( behindGlass != ( W[9].w < 1.5 ) ) {
				continue;
			}
		}
		vec2 at = eye.xy + ray.xy * t;
		float wobble = Noise( at * 0.022 ) * 2.0 - 1.0;
		vec4 sum = vec4( 0.0 );
		vec3 ambient = vec3( 0.0 ), directed = vec3( 0.0 ), lightDir = vec3( 0.0 );
		int first = int( head.y ), last = first + int( head.z );

		for ( int i = first; i < last; i++ ) {
			vec4 ring = Ring( i, at, wobble );

			sum += ring;
			if ( ring.w > 0.0 ) {
				ambient += W[RING( i ) + 2].rgb * ring.w;
				directed += W[RING( i ) + 3].rgb * ring.w;
				lightDir += W[RING( i ) + 4].xyz * ring.w;
			}
		}
		if ( sum.w <= 1e-4 || ( dot( sum.xy, sum.xy ) < 2e-6 && sum.z < 0.003 ) ) {
			continue;
		}
		ambient /= sum.w;
		directed /= sum.w;

		float fade = 1.0 - smoothstep( FAR_OFF * 0.7, FAR_OFF, t );
		// (under the water the eye sees the same surface from below)
		float side = eye.z < head.x ? -1.0 : 1.0;
		vec2 slope = sum.xy * SLOPE * fade;
		vec3 flat_ = vec3( 0.0, 0.0, side );
		vec3 n = normalize( vec3( -slope, 1.0 ) ) * side;
		vec3 view = normalize( ray );
		vec3 p = vec3( at, head.x );

		// what is seen there, read to the side: the further the more water lies behind the surface (the floor of a
		// shallow place hardly moves), but always a little, so the picture on the water itself moves too. Not from
		// a pixel with something solid in front of the water: a leg in it would smear into the ring.
		float behind = clamp( ( scene - t ) * length( ray ), 8.0, 40.0 );
		vec3 on = OnTarget( p - vec3( n.xy, 0.0 ) * behind * BEND );
		vec4 rect = W[8];
		vec2 pixel = gl_FragCoord.xy;

		if ( on.z > 0.0 && on.x >= rect.x && on.y >= rect.y && on.x < rect.x + rect.z && on.y < rect.y + rect.w
			&& SceneDistance( on.xy ) > on.z - 0.5 ) {
			// (no further than the copy of the frame goes round the part that is drawn)
			vec2 aside = on.xy - gl_FragCoord.xy;

			pixel += aside * min( 1.0, W[11].w / max( length( aside ), 1e-3 ) );
		}
		vec3 color = texture( u_image, pixel * W[9].xy ).rgb;

		// The light on it, as far as it differs from flat water. The side of a crest that falls away from the eye is
		// seen at a flatter angle and mirrors more of the room, the side that faces the eye lets more of the water
		// through and is darker: by the law of Fresnel that is little in a dim room, so it is drawn stronger than
		// the law has it (SHADE), as the eye knows rings on water by. And the light of the level caught by the
		// crests that face between it and the eye.
		if ( side > 0.0 ) {
			vec3 l = normalize( lightDir + vec3( 0.0, 0.0, 1e-3 ) );
			vec3 half_ = normalize( l - view );
			vec2 toEye = -view.xy / max( length( view.xy ), 1e-3 );
			float away = dot( slope, toEye );
			vec3 sky = ambient * 0.8 + directed * 0.3;
			float glint = pow( clamp( dot( n, half_ ), 0.0, 1.0 ), GLINT ) - pow( clamp( dot( flat_, half_ ), 0.0, 1.0 ), GLINT );

			color *= 1.0 + clamp( away * SHADE, -0.5, 1.0 );
			color += sky * max( away, 0.0 ) * 0.9;
			color += directed * max( glint, 0.0 ) * 1.6;
		}
		// froth: lighter than the water it lies on, in patches that break up as it goes
		float churn = clamp( sum.z, 0.0, 1.0 ) * fade;
		if ( churn > 0.003 ) {
			float patches = Fbm( at * 0.42 + vec2( time * 0.35, -time * 0.22 ) );
			float froth = smoothstep( 0.62 - churn * 0.5, 0.86 - churn * 0.4, patches + churn * 0.25 );

			color = mix( color, max( color * 1.4, ( ambient * 0.8 + directed * 0.3 ) * 0.8 ) + 0.05, froth * 0.5 );
		}
		o_color = vec4( max( color, 0.0 ), 1.0 );
		return;
	}
	discard;
}
