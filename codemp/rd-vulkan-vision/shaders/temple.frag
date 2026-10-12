#version 460
// The Vision Temple (RE_SetTempleScene, focus/cg_focus.c of the cgame): a place above the clouds that is worked out
// here for every pixel, nothing of it is a model or a texture. A sea of clouds to walk on, a round podium of gold
// with pillars around it, rings of light, and a low sun behind the greatest of them. The pass fills the frame of
// a view without a level before the things of that view are drawn, so what the cgame adds (the others, as wisps)
// lies on it.
//
// Lengths are units of the game, z is up, the sun stands in +x. The cgame walks on the same places: the podium and
// how far one can go are numbers in focus/cg_focus.c that have to match the ones here.
//
// VIEW: 0: xyz the eye, w seconds. 1: xyz where it looks, w how much of the place is there, 0 to 1 (it comes out
// of light). 2: xyz to the right, as long as the tangent of half the angle of view. 3: xyz up, the same.
#include "dialect.glsl"

#ifdef VK_GLES
uniform vec4 u_view[4];
#define VIEW( i )	u_view[i]
#else
layout( push_constant ) uniform Push {
	vec4	view[4];
} push;
#define VIEW( i )	push.view[i]
#endif

VARYING( 0 ) in vec2 v_texCoord;
layout( location = 0 ) out vec4 o_color;

const vec3	SUN = vec3( 0.99504, 0.0, 0.09950 );
const float	SECTOR = 0.5235988;			// the pillars stand in twelve groups around the podium
const vec3	RING_GREAT = vec3( 340.0, 0.0, 175.0 );
const vec3	RING_LEFT = vec3( 150.0, 430.0, 135.0 );
const vec3	RING_RIGHT = vec3( 150.0, -430.0, 135.0 );
const vec3	RING_HALO = vec3( 0.0, 0.0, 330.0 );
const vec3	AXIS_LEFT = vec3( 0.5, 0.8660254, 0.0 );
const vec3	AXIS_RIGHT = vec3( 0.5, -0.8660254, 0.0 );

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

// layers of it, each twice as fine and half as strong: 0 to 1
float clouds( vec2 p, int layers ) {
	float sum = 0.0;
	float part = 0.5;
	float all = 0.0;

	for ( int i = 0; i < layers; i++ ) {
		sum += part * noise( p );
		all += part;
		p = p * 2.03 + vec2( 17.0, 9.0 );
		part *= 0.5;
	}
	return sum / all;
}

// an upright cylinder that stands on z 0 and is h high
float cylinder( vec3 p, float r, float h ) {
	vec2 d = vec2( length( p.xy ) - r, abs( p.z - 0.5 * h ) - 0.5 * h );

	return min( max( d.x, d.y ), 0.0 ) + length( max( d, 0.0 ) );
}

float slab( vec3 p, vec3 half_ ) {
	vec3 d = abs( p ) - half_;

	return min( max( d.x, max( d.y, d.z ) ), 0.0 ) + length( max( d, 0.0 ) );
}

// a ring of radius R around the origin, in the plane across axis
float ring( vec3 p, vec3 axis, float R, float r ) {
	float along = dot( p, axis );

	return length( vec2( along, length( p - along * axis ) - R ) ) - r;
}

// the pillars of one group, and no group on the way in and before the great ring
float pillars( vec3 p ) {
	float sector = floor( atan( p.y, p.x ) / SECTOR + 0.5 );

	if ( abs( sector ) < 0.5 || abs( sector ) > 5.5 ) {
		return 60.0;
	}
	float c = cos( sector * SECTOR );
	float s = sin( sector * SECTOR );
	vec3 q = vec3( c * p.x + s * p.y, c * p.y - s * p.x, p.z );
	float tall = 0.75 + 0.6 * hash( vec2( sector + 20.0, 7.0 ) );
	float d = cylinder( q - vec3( 255.0, 0.0, 0.0 ), 5.0, 250.0 * tall );

	d = min( d, cylinder( q - vec3( 276.0, 16.0, 0.0 ), 4.0, 170.0 * tall ) );
	d = min( d, cylinder( q - vec3( 238.0, -14.0, 0.0 ), 3.5, 110.0 * tall ) );
	return min( d, cylinder( q - vec3( 290.0, -10.0, 0.0 ), 3.0, 70.0 * tall ) );
}

// what is of gold: the podium in three steps, the way to it, the pillars
float gold( vec3 p ) {
	float d = cylinder( p, 170.0, 8.0 );

	d = min( d, cylinder( p, 140.0, 16.0 ) );
	d = min( d, cylinder( p, 110.0, 24.0 ) );
	d = min( d, slab( p - vec3( -345.0, 0.0, 1.0 ), vec3( 175.0, 26.0, 1.5 ) ) );
	return min( d, pillars( p ) );
}

float rings( vec3 p ) {
	float d = ring( p - RING_GREAT, vec3( 1.0, 0.0, 0.0 ), 190.0, 5.5 );

	d = min( d, ring( p - RING_LEFT, AXIS_LEFT, 118.0, 4.0 ) );
	d = min( d, ring( p - RING_RIGHT, AXIS_RIGHT, 118.0, 4.0 ) );
	return min( d, ring( p - RING_HALO, vec3( 0.0, 0.0, 1.0 ), 105.0, 3.0 ) );
}

vec3 goldNormal( vec3 p ) {
	const vec2 e = vec2( 0.35, -0.35 );

	return normalize( e.xyy * gold( p + e.xyy ) + e.yyx * gold( p + e.yyx ) + e.yxy * gold( p + e.yxy ) + e.xxx * gold( p + e.xxx ) );
}

// the sky in a direction: the light of the sun low in +x, clouds that burn in it, stars where it is dark
vec3 sky( vec3 rd, float seconds ) {
	float up = clamp( rd.z, 0.0, 1.0 );
	float toSun = max( dot( rd, SUN ), 0.0 );
	vec3 c = mix( vec3( 1.0, 0.60, 0.22 ), vec3( 0.80, 0.34, 0.10 ), smoothstep( 0.0, 0.24, up ) );

	c = mix( c, vec3( 0.17, 0.07, 0.08 ), smoothstep( 0.18, 0.85, up ) );
	if ( rd.z > 0.015 ) {
		vec2 uv = rd.xy / rd.z;
		float cover = smoothstep( 0.50, 0.80, clouds( uv * 1.2 + vec2( seconds * 0.004, 0.0 ), 5 ) ) * smoothstep( 0.015, 0.20, rd.z );
		vec3 lit = mix( vec3( 0.93, 0.34, 0.07 ), vec3( 1.0, 0.82, 0.50 ), pow( toSun, 3.0 ) );
		vec2 cell = floor( uv * 140.0 );
		float star = step( 0.9965, hash( cell ) ) * smoothstep( 0.35, 0.8, up );

		c += vec3( 1.0, 0.95, 0.85 ) * star * ( 0.5 + 0.5 * sin( seconds * 2.0 + hash( cell + 3.0 ) * 40.0 ) );
		c = mix( c, lit, cover * 0.72 );
	}
	// the sun, the light around it, and rays of it through the haze
	vec3 side = vec3( 0.0, 1.0, 0.0 );
	vec3 over = cross( SUN, side );
	float around = atan( dot( rd, over ), dot( rd, side ) );
	float rays = noise( vec2( around * 7.0, seconds * 0.05 ) ) * noise( vec2( around * 19.0 + 4.0, seconds * 0.03 ) );

	c += vec3( 1.0, 0.72, 0.36 ) * pow( toSun, 7.0 ) * ( 0.45 + 0.9 * rays );
	c += vec3( 1.0, 0.92, 0.74 ) * pow( toSun, 80.0 ) * 1.5;
	c += vec3( 1.0, 1.0, 0.94 ) * pow( toSun, 900.0 ) * 6.0;
	return c;
}

// The light around a ring, and a shimmer inside it, as the ray goes through its plane. tMax: where the ray ends.
vec3 ringLight( vec3 ro, vec3 rd, float tMax, vec3 center, vec3 axis, float R, float seconds ) {
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
	float d = abs( r - R );
	float glow = 0.9 * exp( -d * 0.16 ) + 0.35 * exp( -d * 0.035 );
	float inside = smoothstep( R, R * 0.2, r ) * 0.10 * ( 0.6 + 0.4 * noise( p.yz * 0.05 + p.xy * 0.05 + seconds * 0.3 ) );

	return vec3( 1.0, 0.86, 0.55 ) * glow + vec3( 1.0, 0.92, 0.72 ) * inside;
}

// the curtain of light that falls from the halo over the podium: where the ray goes through it
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

			if ( p.z > 24.0 && p.z < 330.0 ) {
				float streak = noise( vec2( atan( p.y, p.x ) * 22.0, p.z * 0.01 - seconds * 0.35 ) );

				light += streak * streak * smoothstep( 24.0, 120.0, p.z ) * 0.16;
			}
		}
	}
	return light;
}

void main() {
	float seconds = VIEW( 0 ).w;
	vec3 ro = VIEW( 0 ).xyz;
	vec2 at = vec2( v_texCoord.x * 2.0 - 1.0, 1.0 - v_texCoord.y * 2.0 );
	vec3 rd = normalize( VIEW( 1 ).xyz + at.x * VIEW( 2 ).xyz + at.y * VIEW( 3 ).xyz );
	// the floor of clouds is the plane z 0
	float tFloor = rd.z < -0.0001 ? ro.z / -rd.z : 1.0e9;
	float t = 0.0;
	int what = 0;			// 1 gold, 2 a ring

	for ( int i = 0; i < 96; i++ ) {
		vec3 p = ro + rd * t;
		float dGold = gold( p );
		float dRing = rings( p );
		float d = min( dGold, dRing );

		if ( d < 0.03 + t * 0.0006 ) {
			what = dRing < dGold ? 2 : 1;
			break;
		}
		t += min( d, 70.0 );
		if ( t > tFloor || t > 3000.0 ) {
			break;
		}
	}

	vec3 color;
	float tEnd;

	if ( what != 0 && t < tFloor ) {
		vec3 p = ro + rd * t;

		tEnd = t;
		if ( what == 2 ) {
			color = vec3( 1.9, 1.7, 1.25 );
		} else {
			vec3 n = goldNormal( p );
			vec3 back = reflect( rd, n );
			// what the gold mirrors: the sky, or the clouds below it
			vec3 mirrored = back.z > 0.0 ? sky( back, seconds ) : vec3( 0.80, 0.52, 0.30 );
			float edge = pow( 1.0 - max( dot( -rd, n ), 0.0 ), 3.0 );
			float lit = max( dot( n, SUN ), 0.0 );

			color = vec3( 1.0, 0.72, 0.30 ) * ( mirrored * ( 0.50 + 0.50 * edge ) + vec3( 1.0, 0.82, 0.55 ) * lit * 0.55 + 0.10 );
			// a little dark where the gold meets the clouds
			color *= 0.75 + 0.25 * smoothstep( 0.0, 5.0, p.z );
		}
		color = mix( color, vec3( 1.0, 0.62, 0.26 ), 1.0 - exp( -t / 5200.0 ) );
	} else if ( tFloor < 1.0e8 ) {
		vec3 p = ro + rd * tFloor;
		vec2 uv = p.xy * 0.0036 + vec2( seconds * 0.006, seconds * 0.002 );
		float billow = clouds( uv, 5 );
		// lit from the side of the sun: brighter where the cloud falls away towards it
		float relief = billow - clouds( uv + vec2( 0.10, 0.0 ), 3 );
		float far = length( p.xy );

		tEnd = tFloor;
		color = mix( vec3( 0.50, 0.25, 0.13 ), vec3( 1.0, 0.86, 0.68 ), smoothstep( 0.25, 0.80, billow + relief * 2.2 ) );
		// the light of the podium and of the way to it lies on the clouds around them
		color += vec3( 1.0, 0.70, 0.30 ) * ( 0.22 * exp( -far / 240.0 ) + 0.10 * exp( -abs( p.y ) / 60.0 ) * step( p.x, 0.0 ) * exp( -far / 600.0 ) );
		// far away the clouds go over into the light of the horizon
		color = mix( color, sky( normalize( vec3( rd.xy, 0.001 ) ), seconds ), 1.0 - exp( -tFloor / 2400.0 ) );
	} else {
		tEnd = 1.0e9;
		color = sky( rd, seconds );
	}

	color += ringLight( ro, rd, tEnd, RING_GREAT, vec3( 1.0, 0.0, 0.0 ), 190.0, seconds );
	color += ringLight( ro, rd, tEnd, RING_LEFT, AXIS_LEFT, 118.0, seconds ) * 0.8;
	color += ringLight( ro, rd, tEnd, RING_RIGHT, AXIS_RIGHT, 118.0, seconds ) * 0.8;
	color += ringLight( ro, rd, tEnd, RING_HALO, vec3( 0.0, 0.0, 1.0 ), 105.0, seconds ) * 0.7;
	color += vec3( 1.0, 0.85, 0.55 ) * curtain( ro, rd, tEnd, seconds );

	// one arrives out of light
	color = mix( vec3( 1.6, 1.45, 1.15 ), color, smoothstep( 0.0, 1.0, VIEW( 1 ).w ) );
	// bright things run out softly, and the sky has no steps
	color = 1.0 - exp( -color * 1.55 );
	color += ( hash( gl_FragCoord.xy ) - 0.5 ) / 255.0;
	o_color = vec4( color, 1.0 );
}
