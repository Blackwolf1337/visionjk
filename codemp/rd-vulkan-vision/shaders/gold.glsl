// gold.glsl -- the gold of the Vision Temple: where it is (a podium of five steps on a terrace, a way to it,
// groups of columns), and how a place on it is lit. Included behind vision.glsl and light.glsl by temple.frag,
// which finds the gold of a pixel by marching, and by generic.frag, which shades the surfaces of a level whose
// shader script says gold (q3map_vision): tools/temple/build.py makes those surfaces from the same numbers, and
// the shadows and the dark where things meet are worked out from the shapes here, not from the surfaces.

const vec3	GOLD = vec3( 1.0, 0.71, 0.29 );				// what the metal mirrors, head on
const float	SECTOR = 0.5235988;							// the columns stand in twelve groups around the podium

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
