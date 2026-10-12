// light.glsl -- the light that stands in the air of the Vision Temple: four rings of strands of light, the threads
// that fall from the highest of them, the light over the middle of the podium and sparks in the air. Included
// behind vision.glsl by temple.frag (the whole place in one pass), by volume.frag (a volume of a level whose
// shader script says light, q3map_vision) and by generic.frag, whose gold is lit by the rings (gold.glsl).

const vec3	RING_GREAT = vec3( 340.0, 0.0, 175.0 );
const vec3	RING_LEFT = vec3( 150.0, 430.0, 135.0 );
const vec3	RING_RIGHT = vec3( 150.0, -430.0, 135.0 );
const vec3	RING_HALO = vec3( 0.0, 0.0, 330.0 );
const vec3	AXIS_LEFT = vec3( 0.5, 0.8660254, 0.0 );
const vec3	AXIS_RIGHT = vec3( 0.5, -0.8660254, 0.0 );

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

// How near a ray comes to the line in the middle of a ring before tMax. The rings stand inside a ball around the
// temple: a ray that misses it is not marched.
float ringNearest( vec3 ro, vec3 rd, float tMax ) {
	vec3 mid = ro - vec3( 60.0, 0.0, 150.0 );
	float b = dot( mid, rd );
	float inBall = b * b - dot( mid, mid ) + 650.0 * 650.0;
	float near = 1.0e9;

	if ( inBall > 0.0 ) {
		float tOut = min( -b + sqrt( inBall ), tMax );
		float t = max( -b - sqrt( inBall ), 0.0 );

		for ( int i = 0; i < 90; i++ ) {
			if ( t > tOut ) {
				break;
			}
			float d = rings( ro + rd * t );

			near = min( near, d );
			// (in short steps while the ray is near a line)
			t += min( max( d, 1.5 ), 80.0 );
		}
	}
	return near;
}

// All of that light as a ray sees it up to tMax, to be added to what is there. through: how much comes through
// the clouds in front (they dim what is behind them), ringNear: how near the ray came to the line of a ring.
vec3 templeLight( vec3 ro, vec3 rd, float tMax, float seconds, float through, float ringNear ) {
	vec3 light = ringLight( ro, rd, tMax, seconds ) * ( 0.35 + 0.65 * through );

	light += vec3( 1.0, 0.40, 0.06 ) * ( 0.95 / ( 1.0 + ringNear * ringNear * 0.008 ) + 0.10 / ( 1.0 + ringNear * 0.03 ) ) * through;
	light += vec3( 1.0, 0.80, 0.46 ) * curtain( ro, rd, tMax, seconds ) * 0.50;
	light += vec3( 1.0, 0.84, 0.56 ) * beam( ro, rd, tMax ) * 0.22;

	// sparks of gold in the air, where one looks along the clouds
	vec2 air = vec2( atan( rd.y, rd.x ) * 30.0, rd.z * 46.0 + seconds * 0.12 );
	vec2 cell = floor( air );
	float mote = hash( cell );
	vec2 off = fract( air ) - 0.5 - ( vec2( hash( cell + 1.3 ), hash( cell + 7.1 ) ) - 0.5 ) * 0.7;

	return light + vec3( 1.0, 0.80, 0.42 ) * step( 0.90, mote ) * ( 0.5 + 0.5 * sin( seconds * ( 1.0 + 3.0 * mote ) + mote * 40.0 ) )
		* exp( -dot( off, off ) * 1400.0 ) * smoothstep( 0.30, 0.0, abs( rd.z - 0.05 ) ) * 1.6 * through;
}
