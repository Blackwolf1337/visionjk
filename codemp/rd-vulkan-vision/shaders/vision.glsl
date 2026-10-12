// vision.glsl -- what more than one picture of the Vision Temple is worked out with: noise, the sky with its low
// sun, the clouds, and the curve a pixel ends with. Included by temple.frag (the whole place in one pass,
// RE_SetTempleScene) and by volume.frag (a volume of a level whose shader script says clouds, q3map_vision).
// Lengths are units of the game, z is up, the sun stands in +x.
//
// The clouds stand around what one walks on. The file that includes this one says where, in front of the include:
//   CLOUD_ROOF		float: no cloud is higher than this
//   CLOUD_DISC		vec3: a round place the clouds leave free, x and y of its middle and its radius. Far from
//					that middle they pile up
//   CLOUD_LANE		vec3: and a straight one along x at the y of that middle, from x, to x, half its width

const vec3	SUN = vec3( 0.99504, 0.0, 0.09950 );
const vec3	SUN_LIGHT = vec3( 2.6, 1.55, 0.80 );		// what it gives to what it shines on

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
// hollows are 0). They lap at the edge of what one walks on (CLOUD_DISC and CLOUD_LANE, in the Vision Temple the
// terrace around the podium and the way to it) and heap up right beside it, as high as a man and higher: one
// stands among them. Farther out they pile up as high as towers.
float cloudTop( vec2 xy, float seconds, int layers, out float heap ) {
	float r = length( xy - CLOUD_DISC.xy );
	float far = smoothstep( 470.0, 1350.0, r );
	vec2 drift = vec2( seconds * 0.004, seconds * 0.0015 );
	float low = far < 1.0 ? heaped( xy * 0.0044 + drift * 2.0, layers ) : 0.0;
	float high = far > 0.0 ? heaped( xy * 0.0018 + drift + 5.0, layers ) : 0.0;
	// how far outside of what one walks on a place is
	float outside = min( r - CLOUD_DISC.z, max( abs( xy.y - CLOUD_DISC.y ) - CLOUD_LANE.z, max( CLOUD_LANE.x - xy.x, xy.x - CLOUD_LANE.y ) ) );
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

// What a ray sees of them from tMin to tMax: rgb their light, a how much of what is behind them comes through.
vec4 clouds( vec3 ro, vec3 rd, float tMin, float tMax, float seconds, float jitter, float detail ) {
	float tEnd = rd.z > 0.0001 ? min( tMax, ( CLOUD_ROOF - ro.z ) / rd.z ) : tMax;
	float t = tMin + 2.0 + 8.0 * jitter;
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

A pixel

===============================================================================
*/

// bright things run out softly and keep their colour (a curve as film has it), then the steps of a screen
vec3 film( vec3 c ) {
	c = clamp( ( c * ( 2.51 * c + 0.03 ) ) / ( c * ( 2.43 * c + 0.59 ) + 0.14 ), 0.0, 1.0 );
	return pow( c, vec3( 0.4545 ) );
}

// What film() was given, as far as a pixel still says it: a white one may have been brighter. For a pass that
// lays something over a frame that went through the curve already.
vec3 unfilm( vec3 c ) {
	vec3 y = min( pow( c, vec3( 2.2 ) ), vec3( 0.995 ) );
	vec3 a = 2.43 * y - 2.51;
	vec3 b = 0.59 * y - 0.03;

	return ( -b - sqrt( max( b * b - 0.56 * a * y, vec3( 0.0 ) ) ) ) / ( 2.0 * a );
}
