// The noise of the screen effects (screenfx.frag, screenfxfog.frag, screenfxsmoke.frag). Gradient noise (as
// Perlin's) on a hash of the corners of its grid in whole numbers. The noise made of random values at the corners
// these used before showed blocks along the axes, and its hash made of sin() ran out of digits far out in the level
// and late in the game (the time is in the coordinates of what moves), where it turned into hard blocks.

// pcg3d of Jarzynski and Olano, "Hash Functions for GPU Rendering", 2020
uvec3 Pcg3( uvec3 v ) {
	v = v * 1664525u + 1013904223u;
	v.x += v.y * v.z;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	v ^= v >> 16u;
	v.x += v.y * v.z;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	return v;
}

// 0 to 1 for a place, the same for every place in the same 1/64 of a unit
float Hash( vec2 p ) {
	return float( Pcg3( uvec3( ivec3( ivec2( floor( p * 64.0 ) ), 7 ) ) ).x ) / 4294967295.0;
}

// the slope of the noise at a corner of the grid, in every direction alike
vec2 Gradient2( vec2 corner ) {
	uvec3 h = Pcg3( uvec3( ivec3( ivec2( corner ), 0 ) ) );

	return vec2( h.xy & 0xffffu ) / 32767.5 - 1.0;
}

vec3 Gradient3( vec3 corner ) {
	uvec3 h = Pcg3( uvec3( ivec3( corner ) ) );

	return vec3( h & 0xffffu ) / 32767.5 - 1.0;
}

// eased with the fifth power curve, so the noise has no creases at the lines of its grid; about 0 to 1
float Noise( vec2 p ) {
	vec2 i = floor( p );
	vec2 f = p - i;
	vec2 u = f * f * f * ( f * ( f * 6.0 - 15.0 ) + 10.0 );
	float a = dot( Gradient2( i ), f );
	float b = dot( Gradient2( i + vec2( 1.0, 0.0 ) ), f - vec2( 1.0, 0.0 ) );
	float c = dot( Gradient2( i + vec2( 0.0, 1.0 ) ), f - vec2( 0.0, 1.0 ) );
	float d = dot( Gradient2( i + vec2( 1.0, 1.0 ) ), f - vec2( 1.0, 1.0 ) );

	return clamp( 0.5 + mix( mix( a, b, u.x ), mix( c, d, u.x ), u.y ), 0.0, 1.0 );
}

float Noise3( vec3 p ) {
	vec3 i = floor( p );
	vec3 f = p - i;
	vec3 u = f * f * f * ( f * ( f * 6.0 - 15.0 ) + 10.0 );
	float a = dot( Gradient3( i ), f );
	float b = dot( Gradient3( i + vec3( 1.0, 0.0, 0.0 ) ), f - vec3( 1.0, 0.0, 0.0 ) );
	float c = dot( Gradient3( i + vec3( 0.0, 1.0, 0.0 ) ), f - vec3( 0.0, 1.0, 0.0 ) );
	float d = dot( Gradient3( i + vec3( 1.0, 1.0, 0.0 ) ), f - vec3( 1.0, 1.0, 0.0 ) );
	float e = dot( Gradient3( i + vec3( 0.0, 0.0, 1.0 ) ), f - vec3( 0.0, 0.0, 1.0 ) );
	float g = dot( Gradient3( i + vec3( 1.0, 0.0, 1.0 ) ), f - vec3( 1.0, 0.0, 1.0 ) );
	float h = dot( Gradient3( i + vec3( 0.0, 1.0, 1.0 ) ), f - vec3( 0.0, 1.0, 1.0 ) );
	float k = dot( Gradient3( i + vec3( 1.0, 1.0, 1.0 ) ), f - vec3( 1.0, 1.0, 1.0 ) );

	return 0.5 + 0.5 * mix( mix( mix( a, b, u.x ), mix( c, d, u.x ), u.y ), mix( mix( e, g, u.x ), mix( h, k, u.x ), u.y ), u.z );
}

float Fbm( vec2 p ) {
	float sum = 0.0;
	float amp = 0.5;

	for ( int i = 0; i < 4; i++ ) {
		sum += Noise( p ) * amp;
		p = p * 2.03 + vec2( 1.7, 9.2 );
		amp *= 0.5;
	}
	return sum;
}

float Fbm3( vec3 p ) {
	return Noise3( p ) * 0.5 + Noise3( p * 2.03 + vec3( 1.7, 9.2, 4.1 ) ) * 0.3 + Noise3( p * 4.1 + vec3( 8.3, 2.8, 5.5 ) ) * 0.2;
}
