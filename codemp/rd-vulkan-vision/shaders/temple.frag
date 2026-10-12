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
// as long as the tangent of half the angle of view. 4: x and y give the depth of a thing d ahead as the view's
// projection does, x - y / d: what stands in the place (player models) is drawn after it with that depth.
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

const vec3	SUN = vec3( 0.99504, 0.0, 0.09950 );
const vec3	SUN_LIGHT = vec3( 2.6, 1.55, 0.80 );		// what it gives to what it shines on
const vec3	GOLD = vec3( 1.0, 0.71, 0.29 );				// what the metal mirrors, head on
const float	SECTOR = 0.5235988;							// the columns stand in twelve groups around the podium
const vec3	RING_GREAT = vec3( 340.0, 0.0, 175.0 );
const vec3	RING_LEFT = vec3( 150.0, 430.0, 135.0 );
const vec3	RING_RIGHT = vec3( 150.0, -430.0, 135.0 );
const vec3	RING_HALO = vec3( 0.0, 0.0, 330.0 );
const vec3	AXIS_LEFT = vec3( 0.5, 0.8660254, 0.0 );
const vec3	AXIS_RIGHT = vec3( 0.5, -0.8660254, 0.0 );
const float	CLOUD_ROOF = 266.0;							// no cloud is higher than this

/*
===============================================================================

Noise

===============================================================================
*/

float hash( vec2 p ) {
	p = fract( p * vec2( 0.1031, 0.1030 ) );
	p += dot( p, p.yx + 33.33 );
	return fract( ( p.x + p.y ) * p.x );
}

float noise( vec2 p ) {
	vec2 i = floor( p );
	vec2 f = fract( p );

	f = f * f * ( 3.0 - 2.0 * f );
	return mix( mix( hash( i ), hash( i + vec2( 1.0, 0.0 ) ), f.x ),
		mix( hash( i + vec2( 0.0, 1.0 ) ), hash( i + vec2( 1.0, 1.0 ) ), f.x ), f.y );
}

// from one layer to the next: twice as fine, and turned, so the grid of the noise does not show
vec2 finer( vec2 p ) {
	return vec2( 1.6 * p.x - 1.2 * p.y, 1.2 * p.x + 1.6 * p.y ) + vec2( 7.3, 2.9 );
}

// layers of noise, each half as strong as the one before: 0 to 1
float layered( vec2 p, int layers ) {
	float sum = 0.0;
	float part = 0.5;
	float all = 0.0;

	for ( int i = 0; i < layers; i++ ) {
		sum += part * noise( p );
		all += part;
		p = finer( p );
		part *= 0.5;
	}
	return sum / all;
}

// the same folded: round heaps with creases between them, as a cloud has them. 0 to 1.
float heaped( vec2 p, int layers ) {
	float sum = 0.0;
	float part = 0.5;
	float all = 0.0;

	for ( int i = 0; i < layers; i++ ) {
		sum += part * abs( 2.0 * noise( p ) - 1.0 );
		all += part;
		p = finer( p );
		part *= 0.54;
	}
	return sum / all;
}

/*
===============================================================================

The sky

===============================================================================
*/

// how much a direction looks towards the side of the sun, 0 to 1
float sunward( vec3 rd ) {
	float s = 0.5 + 0.5 * rd.x / max( length( rd.xy ), 0.0001 );

	return s * s;
}

// the haze where the sky meets the clouds: gold towards the sun, a cool grey away from it
vec3 haze( vec3 rd ) {
	return mix( vec3( 0.075, 0.09, 0.13 ), vec3( 0.58, 0.20, 0.04 ), sunward( rd ) );
}

// The sun. No disc with an edge: a small burning heart with tongues of flame around it, light that runs out
// softly, long thin rays in two sets that shimmer, rays of it through the haze, a faint ring of light far around
// it, and a streak along the horizon. fine: all of that. Else only the light around it, for a mirror of gold.
vec3 sunLight( vec3 rd, float seconds, bool fine ) {
	float mu = dot( rd, SUN );

	if ( mu < 0.0 ) {
		return vec3( 0.0 );
	}
	// how far from its middle a direction is, about in radians, and where around it
	float off = sqrt( max( 2.0 - 2.0 * mu, 0.0 ) );
	vec3 light = vec3( 1.0, 0.44, 0.12 ) * 0.42 * exp( -off * 4.5 ) + vec3( 1.0, 0.70, 0.36 ) * 1.3 * exp( -off * 17.0 );

	if ( !fine ) {
		return light + vec3( 1.0, 0.9, 0.7 ) * 6.0 * exp( -off * off * 900.0 );
	}
	// (where around it: as an angle for what repeats around it, as a direction for the noise, which would have a
	// seam where an angle begins anew)
	vec2 beside = vec2( rd.y, rd.z - SUN.z );
	vec2 way = beside / max( length( beside ), 0.00001 );
	float around = atan( beside.y, beside.x );
	// its heart, and the flames that lick out of it
	float flames = layered( beside * 36.0 - way * seconds * 0.22, 4 );

	light += vec3( 1.0, 0.96, 0.86 ) * 26.0 * exp( -off * off * 2400.0 );
	light += vec3( 1.0, 0.62, 0.22 ) * 9.0 * flames * flames * exp( -off * 34.0 );
	// rays as a lens draws them: six long ones and ten short ones that turn a little, each shimmering
	float shimmer = 0.55 + 0.45 * noise( way * 5.0 + vec2( seconds * 0.7, 3.0 ) );
	float longRays = pow( abs( cos( around * 3.0 + 0.4 ) ), 110.0 ) * exp( -off * 7.5 );
	float shortRays = pow( abs( cos( around * 5.0 + 1.3 + 0.08 * sin( seconds * 0.5 ) ) ), 240.0 ) * exp( -off * 13.0 );

	light += vec3( 1.0, 0.86, 0.60 ) * ( 1.5 * longRays + 1.0 * shortRays ) * shimmer;
	// rays through the haze, broad and soft
	light += vec3( 1.0, 0.46, 0.13 ) * pow( mu, 14.0 ) * 0.42 * noise( way * 3.2 + vec2( seconds * 0.04, 0.0 ) ) * noise( way * 8.0 + vec2( 4.0, seconds * 0.03 ) );
	// a ring of light far around it, red inside and pale outside
	light += vec3( 1.0, 0.40, 0.12 ) * 0.10 * exp( -pow( ( off - 0.330 ) / 0.020, 2.0 ) ) + vec3( 0.9, 0.8, 0.7 ) * 0.07 * exp( -pow( ( off - 0.352 ) / 0.016, 2.0 ) );
	// and the streak along the horizon
	light += vec3( 1.0, 0.66, 0.30 ) * exp( -abs( rd.z - SUN.z ) * 90.0 ) * pow( mu, 20.0 ) * 0.40;
	return light;
}

// The sky in a direction. fine: with its clouds and stars, which a mirror of rough gold does not show.
vec3 sky( vec3 rd, float seconds, bool fine, float detail ) {
	float up = max( rd.z, 0.0 );
	float mu = max( dot( rd, SUN ), 0.0 );
	float side = sunward( rd );
	vec3 c = mix( haze( rd ), mix( vec3( 0.07, 0.025, 0.05 ), vec3( 0.30, 0.065, 0.010 ), side ), smoothstep( 0.0, 0.26, up ) );

	c = mix( c, vec3( 0.006, 0.006, 0.020 ), smoothstep( 0.16, 0.74, up ) );
	if ( fine && rd.z > 0.01 ) {
		// a plane far above, a little bent down towards the horizon so it does not run out in a line
		vec2 uv = rd.xy / ( rd.z + 0.14 );
		// Clouds in the sky: many layers of noise, so their edges are ragged down to the pixel. The side of a
		// heap that faces the sun burns, the other one is dark, and where a cloud is thin the light of the sun
		// comes through it when it stands behind.
		vec2 at = uv * 0.50 + vec2( seconds * 0.005, 0.0 ) + 0.25 * vec2( noise( uv * 0.7 + 3.1 ), noise( uv * 0.7 + 8.4 ) );
		float f = layered( at, detail > 1.5 ? 6 : 4 );
		float cover = smoothstep( 0.50, 0.66, f );
		float facing = smoothstep( -0.015, 0.075, f - layered( at + vec2( 0.11, 0.0 ), detail > 0.5 ? 4 : 3 ) );
		float thin = cover * ( 1.0 - smoothstep( 0.52, 0.78, f ) );
		vec3 fire = mix( vec3( 0.11, 0.028, 0.020 ) * ( 0.45 + side ), vec3( 1.45, 0.46, 0.07 ) * ( 0.18 + 0.42 * side + 0.45 * mu * mu ), facing );

		fire += vec3( 1.9, 0.92, 0.26 ) * thin * ( 0.04 + 0.16 * side + 0.85 * mu * mu * mu );
		vec2 cell = floor( uv * 150.0 );
		float star = step( 0.9955, hash( cell ) ) * smoothstep( 0.30, 0.70, up ) * ( 1.0 - cover );

		c += vec3( 0.9, 0.85, 0.8 ) * star * ( 0.45 + 0.55 * sin( seconds * 2.0 + hash( cell + 3.0 ) * 40.0 ) );
		// a breath of colour in the dark between the stars
		c += vec3( 0.05, 0.02, 0.07 ) * smoothstep( 0.45, 0.8, layered( uv * 0.35 + 9.0, 3 ) ) * smoothstep( 0.3, 0.8, up );
		c = mix( c, fire, cover * smoothstep( 0.01, 0.14, rd.z ) * 0.94 );
	}
	return c + sunLight( rd, seconds, fine );
}

/*
===============================================================================

The clouds

===============================================================================
*/

// How high the clouds stand at a place, and in heap how high that is among what they can be there, 0 to 1 (their
// hollows are 0). They lap at the edge of what one walks on, the terrace around the podium and the way to it,
// and heap up right beside it, as high as a man and higher: one stands among them. Farther out they pile up as
// high as towers.
float cloudTop( vec2 xy, float seconds, int layers, out float heap ) {
	float r = length( xy );
	float far = smoothstep( 470.0, 1350.0, r );
	vec2 drift = vec2( seconds * 0.004, seconds * 0.0015 );
	float low = far < 1.0 ? heaped( xy * 0.0044 + drift * 2.0, layers ) : 0.0;
	float high = far > 0.0 ? heaped( xy * 0.0018 + drift + 5.0, layers ) : 0.0;
	// how far outside of the gold a place is
	float outside = min( r - 208.0, max( abs( xy.y ) - 28.0, max( -524.0 - xy.x, xy.x + 170.0 ) ) );
	float rise = smoothstep( 2.0, 84.0, outside );

	heap = mix( low, high, far );
	return mix( mix( -11.0 + 8.0 * low, -24.0 + 92.0 * low, rise ), -90.0 + 350.0 * high * high, far );
}

// the small stuff of a cloud at a place: what crinkles its edge like a cauliflower. About -0.5 to 0.5.
float crinkle( vec3 p ) {
	// (each layer leans another way with the height: one way for all of them shows as streaks along it)
	return 0.52 * noise( p.xy * 0.034 + p.z * vec2( 0.027, 0.039 ) ) + 0.30 * noise( p.xy * 0.078 + p.z * vec2( -0.051, 0.024 ) + 11.0 )
		+ 0.18 * noise( p.yx * 0.170 + p.z * vec2( 0.070, -0.090 ) + 23.0 ) - 0.5;
}

vec4 clouds( vec3 ro, vec3 rd, float tMax, float seconds, float jitter, float detail ) {
	float tEnd = rd.z > 0.0001 ? min( tMax, ( CLOUD_ROOF - ro.z ) / rd.z ) : tMax;
	float t = 2.0 + 8.0 * jitter;
	float through = 1.0;
	vec3 light = vec3( 0.0 );
	float mu = max( dot( rd, SUN ), 0.0 );
	vec3 far = haze( rd );
	// seen against the sun a cloud shines through at its edges
	float forward = 0.75 + 1.5 * pow( mu, 5.0 );
	float heap, heapSun;

	for ( int i = 0; i < 64; i++ ) {
		if ( t > tEnd || through < 0.03 ) {
			break;
		}
		vec3 p = ro + rd * t;
		// (near by the crinkle is the small stuff, the heights need no fifth layer for it)
		int layers = ( t > 4200.0 || detail < 0.5 ) ? 3 : 4;
		float soft = 5.0 + t * 0.014;
		float above = p.z - cloudTop( p.xy, seconds, layers, heap );

		if ( above > soft + 8.0 ) {
			// nothing here: on by a part of what is left above the tops, which are steep
			t += max( ( above - soft - 8.0 ) * 0.34, 3.0 + t * 0.012 );
			continue;
		}
		// (near by the edge of a cloud is crinkled, far away that is finer than a pixel)
		float fray = t < 1800.0 ? crinkle( p ) : 0.0;

		above += fray * 22.0;
		float dense = clamp( ( soft - above ) / ( 2.0 * soft ), 0.0, 1.0 );
		float pace = 4.0 + t * 0.022;
		// Is the way to the sun free a little further on, or is a heap in it? The sun stands low: what is
		// higher than the ray there by a hand's breadth shades this place.
		vec3 q = p + SUN * ( 64.0 + t * 0.04 );
		float shade = smoothstep( -5.0, 9.0 + t * 0.01, cloudTop( q.xy, seconds, 3, heapSun ) - q.z );

		// (and close by, where a small heap of its own stands in the light of a place)
		if ( t < 900.0 && detail > 1.5 ) {
			q = p + SUN * 20.0;
			shade = max( shade, 0.85 * smoothstep( -2.0, 6.0, cloudTop( q.xy, seconds, 3, heapSun ) - q.z + fray * 22.0 ) );
		}
		// what the sun gives, what the sky gives from above, and less of both deep inside and in the hollows
		vec3 lit = vec3( 1.55, 0.98, 0.58 ) * ( 1.0 - shade ) * forward * ( 0.30 + 0.70 * ( 1.0 - dense ) )
			* ( 0.42 + 0.95 * heap ) * ( 1.0 - 1.3 * fray );
		float open = clamp( 0.5 + above / ( 2.0 * soft ), 0.0, 1.0 ) * ( 0.22 + 0.78 * smoothstep( 0.04, 0.50, heap ) );
		float cover = 1.0 - exp( -dense * dense * pace * 0.13 );

		lit += mix( vec3( 0.022, 0.012, 0.018 ), vec3( 0.135, 0.090, 0.115 ), open );
		// far away it goes over into the haze
		lit = mix( lit, far, 1.0 - exp( -t / 4200.0 ) );
		light += through * cover * lit;
		through *= 1.0 - cover;
		t += pace;
	}
	return vec4( light, through );
}

/*
===============================================================================

What is of gold

===============================================================================
*/

// an upright cylinder that stands on z 0 and is h high
float cylinder( vec3 p, float r, float h ) {
	vec2 d = vec2( length( p.xy ) - r, abs( p.z - 0.5 * h ) - 0.5 * h );

	return min( max( d.x, d.y ), 0.0 ) + length( max( d, 0.0 ) );
}

float slab( vec3 p, vec3 half_ ) {
	vec3 d = abs( p ) - half_;

	return min( max( d.x, max( d.y, d.z ) ), 0.0 ) + length( max( d, 0.0 ) );
}

// a column: a shaft with a ball on its top
float column( vec3 p, float r, float h ) {
	return min( cylinder( p, r, h ), length( p - vec3( 0.0, 0.0, h + r * 0.5 ) ) - r * 1.3 );
}

// The podium: five round steps, each 6 high and 10 deep, the top one 130 wide. (Temple_Ground() of the cgame.)
float podium( vec3 p ) {
	float r = length( p.xy );
	float d = 1.0e9;

	if ( r > 190.0 ) {
		return r - 172.0;
	}
	for ( int i = 0; i < 5; i++ ) {
		float high = 3.0 * float( i + 1 );
		vec2 q = abs( vec2( r, p.z - high ) ) - vec2( 170.0 - 10.0 * float( i ), high );

		d = min( d, min( max( q.x, q.y ), 0.0 ) + length( max( q, 0.0 ) ) );
	}
	return d - 0.5;
}

// the way to it, with a kerb at each side
float way( vec3 p ) {
	vec3 q = vec3( p.x + 345.0, abs( p.y ), p.z );

	return min( slab( q - vec3( 0.0, 0.0, 1.0 ), vec3( 175.0, 26.0, 1.5 ) ),
		slab( q - vec3( 0.0, 27.5, 2.5 ), vec3( 175.0, 2.0, 3.0 ) ) - 0.4 );
}

// the columns of one group, and no group on the way in and before the great ring
float columns( vec3 p ) {
	float r = length( p.xy );
	// they stand in a band around the podium: away from it this is as good as their distance
	float band = max( 222.0 - r, r - 312.0 );

	if ( band > 24.0 ) {
		return band;
	}
	float sector = floor( atan( p.y, p.x ) / SECTOR + 0.5 );

	if ( abs( sector ) < 0.5 || abs( sector ) > 5.5 ) {
		return 60.0;
	}
	float c = cos( sector * SECTOR );
	float s = sin( sector * SECTOR );
	vec3 q = vec3( c * p.x + s * p.y, c * p.y - s * p.x, p.z );
	float tall = 0.7 + 0.7 * hash( vec2( sector + 20.0, 7.0 ) );
	float d = column( q - vec3( 255.0, 0.0, 0.0 ), 6.5, 270.0 * tall );

	d = min( d, column( q - vec3( 272.0, 15.0, 0.0 ), 5.5, 196.0 * tall ) );
	d = min( d, column( q - vec3( 240.0, -13.0, 0.0 ), 5.0, 150.0 * tall ) );
	d = min( d, column( q - vec3( 286.0, -9.0, 0.0 ), 4.5, 118.0 * tall ) );
	d = min( d, column( q - vec3( 262.0, -24.0, 0.0 ), 4.5, 88.0 * tall ) );
	d = min( d, column( q - vec3( 246.0, 22.0, 0.0 ), 4.0, 62.0 * tall ) );
	return min( d, column( q - vec3( 297.0, 10.0, 0.0 ), 3.5, 42.0 * tall ) );
}

// and a small group under each of the two rings at the sides
float sideColumns( vec3 p ) {
	vec3 q = vec3( p.x - 150.0, abs( p.y ) - 430.0, p.z );
	float r = length( q.xy );

	if ( r > 60.0 ) {
		return r - 36.0;
	}
	float d = column( q, 5.0, 86.0 );

	d = min( d, column( q - vec3( 14.0, 6.0, 0.0 ), 4.0, 58.0 ) );
	d = min( d, column( q - vec3( -12.0, 9.0, 0.0 ), 4.0, 68.0 ) );
	d = min( d, column( q - vec3( 6.0, -13.0, 0.0 ), 3.5, 42.0 ) );
	return min( d, column( q - vec3( -9.0, -10.0, 0.0 ), 3.5, 30.0 ) );
}

// the terrace the podium stands on: what one walks on around it (Temple_Keep() of the cgame)
float terrace( vec3 p ) {
	return cylinder( p + vec3( 0.0, 0.0, 3.0 ), 208.0, 4.5 ) - 0.5;
}

float gold( vec3 p ) {
	return min( min( podium( p ), terrace( p ) ), min( way( p ), min( columns( p ), sideColumns( p ) ) ) );
}

vec3 goldNormal( vec3 p ) {
	const vec2 e = vec2( 0.25, -0.25 );

	return normalize( e.xyy * gold( p + e.xyy ) + e.yyx * gold( p + e.yyx ) + e.yxy * gold( p + e.yxy ) + e.xxx * gold( p + e.xxx ) );
}

/*
===============================================================================

The rings

===============================================================================
*/

// a ring of radius R around the origin, in the plane across axis
float ring( vec3 p, vec3 axis, float R, float r ) {
	float along = dot( p, axis );

	return length( vec2( along, length( p - along * axis ) - R ) ) - r;
}

// how far it is to the line in the middle of the nearest ring
float rings( vec3 p ) {
	float d = ring( p - RING_GREAT, vec3( 1.0, 0.0, 0.0 ), 190.0, 0.0 );

	d = min( d, ring( p - RING_LEFT, AXIS_LEFT, 118.0, 0.0 ) );
	d = min( d, ring( p - RING_RIGHT, AXIS_RIGHT, 118.0, 0.0 ) );
	return min( d, ring( p - RING_HALO, vec3( 0.0, 0.0, 1.0 ), 105.0, 0.0 ) );
}

// The strands of light of a ring, where the ray goes through its plane before tMax: each with a head that runs
// around it and a tail behind the head, some one way and some the other, and a glow around all of them.
vec3 strands( vec3 ro, vec3 rd, float tMax, vec3 center, vec3 axis, float R, float seconds, float seed ) {
	float along = dot( rd, axis );

	if ( abs( along ) < 0.0001 ) {
		return vec3( 0.0 );
	}
	float t = dot( center - ro, axis ) / along;

	if ( t <= 0.0 || t > tMax ) {
		return vec3( 0.0 );
	}
	vec3 p = ro + rd * t - center;
	float r = length( p );

	if ( r > R * 1.6 ) {
		return vec3( 0.0 );
	}
	vec3 u = normalize( cross( axis, vec3( 0.0, 0.0, 1.0 ) + axis.zxy * 0.001 ) );
	float where = atan( dot( p, cross( axis, u ) ), dot( p, u ) );
	float core = 0.0;
	float glow = 0.0;

	for ( int i = 0; i < 6; i++ ) {
		float fi = float( i );
		float back = mod( fi, 2.0 ) > 0.5 ? -1.0 : 1.0;
		// where this strand runs: around the ring, in and out of it, so the strands cross each other
		float ri = R * ( 0.90 + 0.030 * fi ) + R * 0.028 * sin( where * ( 2.0 + fi ) + back * seconds * ( 0.25 + 0.06 * fi ) + fi * 1.9 + seed );
		float d = abs( r - ri );
		float run = fract( where / 6.28318 * ( 1.0 + mod( fi, 3.0 ) ) + back * seconds * ( 0.045 + 0.017 * fi ) + fi * 0.37 + seed );
		float tail = 0.22 + 0.78 * run * run * run;

		core += tail * exp( -d * d * 0.30 );
		glow += tail * ( 0.75 / ( 1.0 + d * d * 0.010 ) + 0.22 / ( 1.0 + d * 0.05 ) );
	}
	// sparks on the strands, and a shimmer in the middle of the ring
	float spark = step( 0.93, noise( vec2( where * 40.0 + seed * 9.0, floor( seconds * 6.0 ) ) ) ) * exp( -abs( r - R ) * 0.12 );
	float inside = smoothstep( R * 0.9, R * 0.1, r ) * ( 0.035 + 0.035 * noise( vec2( where * 3.0, r * 0.04 - seconds * 0.4 ) ) );

	return vec3( 1.0, 0.74, 0.30 ) * ( core * 2.4 + spark * 2.2 ) + vec3( 1.0, 0.34, 0.045 ) * glow * 0.30 + vec3( 1.0, 0.75, 0.40 ) * inside;
}

// all four of them, as a ray sees them up to tMax
vec3 ringLight( vec3 ro, vec3 rd, float tMax, float seconds ) {
	return strands( ro, rd, tMax, RING_GREAT, vec3( 1.0, 0.0, 0.0 ), 190.0, seconds, 0.0 )
		+ strands( ro, rd, tMax, RING_LEFT, AXIS_LEFT, 118.0, seconds, 2.1 ) * 0.85
		+ strands( ro, rd, tMax, RING_RIGHT, AXIS_RIGHT, 118.0, seconds, 4.3 ) * 0.85
		+ strands( ro, rd, tMax, RING_HALO, vec3( 0.0, 0.0, 1.0 ), 105.0, seconds, 6.2 ) * 0.9;
}

// the light that falls in threads from the halo onto the podium: where the ray goes through that curtain
float curtain( vec3 ro, vec3 rd, float tMax, float seconds ) {
	float a = dot( rd.xy, rd.xy );
	float b = dot( ro.xy, rd.xy );
	float c = dot( ro.xy, ro.xy ) - 105.0 * 105.0;
	float disc = b * b - a * c;
	float light = 0.0;

	if ( a < 0.000001 || disc <= 0.0 ) {
		return 0.0;
	}
	disc = sqrt( disc );
	for ( int i = 0; i < 2; i++ ) {
		float t = ( -b + ( i == 0 ? -disc : disc ) ) / a;

		if ( t > 0.0 && t < tMax ) {
			vec3 p = ro + rd * t;

			if ( p.z > 30.0 && p.z < 330.0 ) {
				float where = atan( p.y, p.x ) * 34.0;
				// thin threads, each as bright as it is, with drops of light that fall along them
				float thread = pow( noise( vec2( where, 3.0 ) ), 4.0 );
				float drops = 0.35 + 0.65 * noise( vec2( where, p.z * 0.035 + seconds * 1.1 + hash( vec2( floor( where ), 1.0 ) ) * 9.0 ) );

				light += thread * drops * pow( smoothstep( 30.0, 330.0, p.z ), 1.4 );
			}
		}
	}
	return light;
}

// the light that stands over the middle of the podium: how near the ray comes to that line
float beam( vec3 ro, vec3 rd, float tMax ) {
	float along = dot( rd.xy, rd.xy );

	if ( along < 0.000001 ) {
		return 0.0;
	}
	float t = clamp( -dot( ro.xy, rd.xy ) / along, 0.0, tMax );
	vec3 p = ro + rd * t;

	return exp( -length( p.xy ) * 0.05 ) * smoothstep( 26.0, 90.0, p.z ) * smoothstep( 520.0, 200.0, p.z );
}

/*
===============================================================================

The gold, lit

===============================================================================
*/

// how much of the sun reaches a place on the gold: the columns throw long shadows, it stands low
float sunShadow( vec3 p ) {
	float t = 3.0;
	float lit = 1.0;

	for ( int i = 0; i < 14; i++ ) {
		float d = gold( p + SUN * t );

		lit = min( lit, 9.0 * d / t );
		if ( lit < 0.02 || t > 420.0 ) {
			break;
		}
		t += clamp( d, 3.0, 50.0 );
	}
	return clamp( lit, 0.0, 1.0 );
}

// dark where things stand close to each other
float nearness( vec3 p, vec3 n ) {
	float dark = 0.0;

	dark += ( 2.5 - gold( p + n * 2.5 ) ) * 0.20;
	dark += ( 8.0 - gold( p + n * 8.0 ) ) * 0.07;
	dark += ( 22.0 - gold( p + n * 22.0 ) ) * 0.025;
	return clamp( 1.0 - dark, 0.0, 1.0 );
}

vec3 goldLit( vec3 p, vec3 rd, float seconds, float detail ) {
	vec3 n = goldNormal( p );
	float r = length( p.xy );
	float rough = 0.24;

	// What only the shading knows of the surface. Upright faces have flutes: the columns sixteen each, the
	// steps of the podium many fine ones. Faces that lie flat have lines engraved: rings and rays on the
	// podium, bars across the way.
	if ( abs( n.z ) < 0.6 ) {
		float turn = ( r < 212.0 && p.z < 32.0 ? 0.20 * cos( atan( p.y, p.x ) * 140.0 ) : 0.30 * cos( atan( n.y, n.x ) * 16.0 ) );

		n.xy = vec2( n.x * cos( turn ) - n.y * sin( turn ), n.x * sin( turn ) + n.y * cos( turn ) );
	} else if ( n.z > 0.6 ) {
		if ( p.x < -172.0 && r > 209.0 ) {
			n.x += 0.10 * cos( p.x * 0.42 );
		} else {
			vec2 out_ = p.xy / max( r, 0.001 );

			n.xy += out_ * 0.09 * cos( r * 0.9 ) + vec2( -out_.y, out_.x ) * 0.05 * cos( atan( p.y, p.x ) * 36.0 ) * smoothstep( 20.0, 60.0, r );
		}
		rough = 0.30;
	}
	n = normalize( n );
	// (worn here and polished there)
	rough += 0.10 * noise( p.xy * 0.11 + p.z * 0.07 );

	vec3 v = -rd;
	float nv = max( dot( n, v ), 0.03 );
	float nl = max( dot( n, SUN ), 0.0 );
	vec3 h = normalize( SUN + v );
	float nh = max( dot( n, h ), 0.0 );
	float a = rough * rough;
	float a2 = a * a;
	float spread = nh * nh * ( a2 - 1.0 ) + 1.0;
	float k = a * 0.5;
	// the highlight of the sun: how many of its small mirrors face the middle between sun and eye, how many
	// of them hide each other, and how much more a metal mirrors at a flat angle
	float mirrors = a2 / ( 3.14159 * spread * spread ) * 0.25 / ( ( nl * ( 1.0 - k ) + k ) * ( nv * ( 1.0 - k ) + k ) );
	vec3 flat_ = GOLD + ( 1.0 - GOLD ) * pow( 1.0 - max( dot( v, h ), 0.0 ), 5.0 );
	float sun = nl > 0.0 ? ( detail > 0.5 ? sunShadow( p + n * 0.6 ) : 1.0 ) : 0.0;
	float open = nearness( p, n );
	vec3 color = min( SUN_LIGHT * 3.2 * sun * nl * mirrors * flat_, vec3( 24.0 ) );

	// what it mirrors of the world: the sky, or the clouds under it, less sharply the rougher it is
	vec3 back = reflect( rd, n );
	vec3 world = back.z > -0.02 ? sky( normalize( vec3( back.xy, max( back.z, 0.0 ) ) ), seconds, false, 0.0 ) : vec3( 0.0 );
	vec3 below = mix( vec3( 0.34, 0.20, 0.16 ), vec3( 1.25, 0.82, 0.52 ), sunward( back ) );
	vec3 mirror = GOLD + ( 1.0 - GOLD ) * pow( 1.0 - nv, 5.0 ) * ( 1.0 - rough );

	world = mix( below, world, smoothstep( -0.12, 0.10, back.z ) );
	world = mix( world, vec3( 0.55, 0.26, 0.10 ), rough * 0.55 );
	color += mirror * open * ( world + ringLight( p + n * 0.5, back, 900.0, seconds ) * 0.55 );
	// and a little of the light that falls on it comes back from it in all directions
	color += GOLD * 0.07 * ( SUN_LIGHT * nl * sun + vec3( 0.30, 0.18, 0.14 ) ) * open;
	return color;
}

/*
===============================================================================

A pixel

===============================================================================
*/

// bright things run out softly and keep their colour (a curve as film has it), then the steps of a screen
vec3 film( vec3 c ) {
	c = clamp( ( c * ( 2.51 * c + 0.03 ) ) / ( c * ( 2.43 * c + 0.59 ) + 0.14 ), 0.0, 1.0 );
	return pow( c, vec3( 0.4545 ) );
}

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
	float ringNear = 1.0e9;		// how near the ray came to such a line

	// The gold and the rings stand inside a ball around the temple: a ray that misses it is not marched at all,
	// one that meets it only through it.
	vec3 mid = ro - vec3( 60.0, 0.0, 150.0 );
	float b = dot( mid, rd );
	float inBall = b * b - dot( mid, mid ) + 650.0 * 650.0;

	if ( inBall > 0.0 ) {
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
	vec4 sea = clouds( ro, rd, tEnd, seconds, jitter, VIEW( 2 ).w );

	color = sea.rgb + sea.a * color;
	color += ringLight( ro, rd, tEnd, seconds ) * ( 0.35 + 0.65 * sea.a );
	color += vec3( 1.0, 0.40, 0.06 ) * ( 0.95 / ( 1.0 + ringNear * ringNear * 0.008 ) + 0.10 / ( 1.0 + ringNear * 0.03 ) ) * sea.a;
	color += vec3( 1.0, 0.80, 0.46 ) * curtain( ro, rd, tEnd, seconds ) * 0.50;
	color += vec3( 1.0, 0.84, 0.56 ) * beam( ro, rd, tEnd ) * 0.22;

	// sparks of gold in the air, where one looks along the clouds
	vec2 air = vec2( atan( rd.y, rd.x ) * 30.0, rd.z * 46.0 + seconds * 0.12 );
	vec2 cell = floor( air );
	float mote = hash( cell );
	vec2 off = fract( air ) - 0.5 - ( vec2( hash( cell + 1.3 ), hash( cell + 7.1 ) ) - 0.5 ) * 0.7;

	color += vec3( 1.0, 0.80, 0.42 ) * step( 0.90, mote ) * ( 0.5 + 0.5 * sin( seconds * ( 1.0 + 3.0 * mote ) + mote * 40.0 ) )
		* exp( -dot( off, off ) * 1400.0 ) * smoothstep( 0.30, 0.0, abs( rd.z - 0.05 ) ) * 1.6 * sea.a;

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
