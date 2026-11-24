// The level under the pixels of the view, from the depth of the scene: for the passes that lay something onto the
// level after it is drawn (wet.frag, paint.frag). Before it is included W names the numbers of the pass, whose
// first eleven rows are the view as VK_DrawWet() puts it (wet.vert), and u_depth the depth of the scene.

// A distance from the eye in green, blue and alpha of a mask (shaders/rtshadow.frag, smshadow.frag, rtaccum.frag), 24
// bits up to 8192 units
vec3 PackDistance( float d ) {
	uint v = uint( clamp( d / 8192.0, 0.0, 1.0 ) * 16777215.0 );

	return vec3( float( ( v >> 16u ) & 255u ), float( ( v >> 8u ) & 255u ), float( v & 255u ) ) / 255.0;
}

float UnpackDistance( vec3 c ) {
	uvec3 b = uvec3( round( c * 255.0 ) );

	return float( ( b.x << 16u ) | ( b.y << 8u ) | b.z ) / 16777215.0 * 8192.0;
}

// how far in front of the eye the scene is at a pixel of the target
float SceneDistance( vec2 pixel ) {
	ivec2 size = textureSize( u_depth, 0 );
	float depth = texelFetch( u_depth, clamp( ivec2( pixel ), ivec2( 0 ), size - 1 ), 0 ).r;

	// (both are below zero: x / ( depth - 1 ) and a bit)
	return W[6].w / min( depth + W[7].w, -1e-7 );
}

// from the eye through a place of the target, as long as one unit in front of the eye
vec3 SceneRay( vec2 pixel ) {
	vec4 rect = W[8];
	vec2 ndc = vec2( ( pixel.x - rect.x ) / rect.z * 2.0 - 1.0, 1.0 - ( pixel.y - rect.y ) / rect.w * 2.0 );

	return W[5].xyz - W[6].xyz * ( ndc.x * W[4].w ) + W[7].xyz * ( ndc.y * W[5].w );
}

// the point of the level at the middle of a pixel of the target
vec3 ScenePoint( vec2 pixel ) {
	return W[4].xyz + SceneRay( pixel ) * SceneDistance( pixel );
}

// a place of the level as a pixel of the target, z: how far in front of the eye, -1 behind it
vec3 OnTarget( vec3 p ) {
	vec4 clip = mat4( W[0], W[1], W[2], W[3] ) * vec4( p, 1.0 );
	vec4 rect = W[8];

	if ( clip.w < 1.0 ) {
		return vec3( 0.0, 0.0, -1.0 );
	}
	vec2 ndc = clip.xy / clip.w;
	return vec3( rect.x + ( ndc.x * 0.5 + 0.5 ) * rect.z, rect.y + ( 0.5 - ndc.y * 0.5 ) * rect.w, dot( p - W[4].xyz, W[5].xyz ) );
}

// The way the surface faces at the point p of a pixel, towards the eye: from the neighbours on the side where the
// depth goes on smoothly. (The slopes of the block of pixels mix the floor with what stands on it, a player, and a
// ring of pixels around him would not be on the floor.) fallback where the neighbours give no plane.
vec3 SceneSurface( vec2 pixel, vec3 p, vec3 fallback ) {
	vec3 pl = ScenePoint( pixel - vec2( 1.0, 0.0 ) ), pr = ScenePoint( pixel + vec2( 1.0, 0.0 ) );
	vec3 pd = ScenePoint( pixel - vec2( 0.0, 1.0 ) ), pu = ScenePoint( pixel + vec2( 0.0, 1.0 ) );
	vec3 dx = ( abs( dot( pr - p, W[5].xyz ) ) < abs( dot( p - pl, W[5].xyz ) ) ) ? pr - p : p - pl;
	vec3 dy = ( abs( dot( pu - p, W[5].xyz ) ) < abs( dot( p - pd, W[5].xyz ) ) ) ? pu - p : p - pd;
	vec3 surface = cross( dx, dy );

	surface = ( dot( surface, surface ) > 1e-10 ) ? normalize( surface ) : fallback;
	return ( dot( surface, W[4].xyz - p ) < 0.0 ) ? -surface : surface;
}

// A point in a trough of the bumps of a tessellated stage (generic.tese) lies up to 8 units below the triangles the rays
// know, and its rays hit the floor from below: the place on the floor right above it. Only a surface less than 9 units
// above: a fixed lift of 12 put the points on the floor beside a low bench inside it (jagged shadows round its seats,
// their picture 2026-11-24). Needs LevelHitAt (a shader that traces rays).
#define ON_LEVEL_DEPTH	9.0
#define OnLevel( p, normal ) OnLevelWith( p, normal, LevelHitAt( p + normal * ( ON_LEVEL_DEPTH + 0.5 ), -normal, ON_LEVEL_DEPTH ) )
vec3 OnLevelWith( vec3 p, vec3 normal, float t ) {
	return t > 0.0 ? p + normal * ( ON_LEVEL_DEPTH + 0.5 - t ) : p;
}

// The way the surface faces at p as the lightmap knows it: SceneSurface from neighbours about units away on each side,
// so the bumps of a tessellated stage (generic.tese, up to 16 units high) average out. On those bumps the way of the
// pixel itself turned the back of each ripple away from the sun, and the shadow of a player left it out (white streaks
// across his shadow on the sand of mp/ffa3, their picture 2026-11-24). A neighbour on something else (the player
// standing there, a wall) faces too differently from the pixel and is not taken; with none on a side the pixel's own.
vec3 SceneSurfaceWide( vec2 pixel, vec3 p, vec3 fallback, float units ) {
	vec3 near = SceneSurface( pixel, p, fallback );
	float perPixel = max( min( distance( ScenePoint( pixel - vec2( 1.0, 0.0 ) ), p ), distance( ScenePoint( pixel + vec2( 1.0, 0.0 ) ), p ) ), 0.01 );
	float r = clamp( units / perPixel, 2.0, 96.0 );
	vec3 q[4];
	bool on[4];
	for ( int i = 0; i < 4; i++ ) {
		vec2 at = pixel + ( i < 2 ? vec2( i == 0 ? -r : r, 0.0 ) : vec2( 0.0, i == 2 ? -r : r ) );

		q[i] = ScenePoint( at );
		on[i] = distance( q[i], p ) < units * 3.0 && dot( SceneSurface( at, q[i], near ), near ) > 0.5;
	}
	vec3 dx = ( on[0] && on[1] ) ? q[1] - q[0] : ( on[1] ? q[1] - p : ( on[0] ? p - q[0] : vec3( 0.0 ) ) );
	vec3 dy = ( on[2] && on[3] ) ? q[3] - q[2] : ( on[3] ? q[3] - p : ( on[2] ? p - q[2] : vec3( 0.0 ) ) );
	vec3 surface = cross( dx, dy );

	if ( dot( surface, surface ) < 1e-6 ) {
		return near;
	}
	surface = normalize( surface );
	return ( dot( surface, W[4].xyz - p ) < 0.0 ) ? -surface : surface;
}
