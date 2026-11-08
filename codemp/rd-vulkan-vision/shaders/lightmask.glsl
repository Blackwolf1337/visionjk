// The mask of the shadows from the moving lights (vk_rt.cpp): shaders/smlights.frag (the players from the cubes of
// the shadow maps, every device) and shaders/rtlightmask.frag (the same and the level by rays, LEVEL_RAYS, where the
// device traces rays) include it.

layout( set = 0, binding = 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
	vec4	rt[319];
} globals;
layout( set = 1, binding = 4 ) uniform sampler2D u_atlas;	// the depth of the shadow maps
layout( set = 2, binding = 0 ) uniform sampler2D u_depth;	// the depth of the scene, read pixel for pixel

#define W		globals.rt
#define TILE	256.0
#define COLUMNS	8

layout( location = 0 ) out vec4 o_color;

#include "scene.glsl"

vec3 NearestOnSegment( vec3 a, vec3 b, vec3 p ) {
	vec3 ab = b - a;
	return a + ab * clamp( dot( p - a, ab ) / max( dot( ab, ab ), 1e-4 ), 0.0, 1.0 );
}

// how much of a cube (its six tiles from first on, around center) lies in front of the point q, 3 x 3 pixels
float CubeShadow( int first, vec3 center, vec3 q ) {
	vec3 d = q - center, ad = abs( d );
	int face;
	if ( ad.x >= ad.y && ad.x >= ad.z ) {
		face = d.x > 0.0 ? 0 : 1;
	} else if ( ad.y >= ad.z ) {
		face = d.y > 0.0 ? 2 : 3;
	} else {
		face = d.z > 0.0 ? 4 : 5;
	}
	int t = first + face;
	vec4 clip = mat4( W[47 + t * 4], W[48 + t * 4], W[49 + t * 4], W[50 + t * 4] ) * vec4( q, 1.0 );
	if ( clip.w <= 4.0 ) {
		return 0.0;
	}
	vec3 ndc = clip.xyz / clip.w;
	if ( ndc.z >= 1.0 ) {
		return 0.0;
	}
	vec2 corner = vec2( float( t % COLUMNS ), float( t / COLUMNS ) ) * TILE;
	ivec2 at = ivec2( corner + clamp( ndc.xy * 0.5 + 0.5, 0.0, 0.999 ) * TILE );
	ivec2 lo = ivec2( corner ), hi = ivec2( corner ) + ivec2( int( TILE ) - 1 );
	float blocked = 0.0;
	for ( int y = -1; y <= 1; y++ ) {
		for ( int x = -1; x <= 1; x++ ) {
			blocked += ( texelFetch( u_atlas, clamp( at + ivec2( x, y ), lo, hi ), 0 ).r < ndc.z - 0.0005 ) ? 1.0 : 0.0;
		}
	}
	return blocked / 9.0;
}

// how much the level hides of the light from a to b (a point when b is a) at q, by rays against it (mask 1) with
// LEVEL_RAYS (the device traces rays, shaders/rtlightmask.frag); without, nothing: the light of a saber went through
// the walls, pillars and ledges between it and a surface (2026-11-08). A blade is a line of light: each pixel takes
// four points of its own along it (one in each quarter, at another place in each pixel and frame), so the shadow of a
// pillar fades out over what the blade spans; from three points the same for every pixel it ended in a hard line.
// Only the points in front of the surface (p, normal) count: a blade that dips into the floor has points under it,
// which give it no light anyway, and every ray to them hit the floor itself, so the floor round the blade was
// shadowed (their picture 2026-11-08: a dark hole in the light where the tip touched the floor).
float LevelTaken( vec3 q, vec3 p, vec3 normal, vec3 a, vec3 b ) {
#ifdef LEVEL_RAYS
	const int POINTS = 4;
	uvec3 v = uvec3( uvec2( gl_FragCoord.xy ), uint( W[13].w ) );
	float hidden = 0.0, counted = 0.0;

	v = v * 1664525u + 1013904223u;
	v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
	v ^= v >> 16u;
	for ( int i = 0; i < POINTS; i++ ) {
		v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
		float along = ( float( i ) + float( v.x & 0xffffu ) / 65536.0 ) / float( POINTS );
		vec3 point = mix( a, b, along );
		vec3 to = point - q;
		float d = length( to );

		if ( dot( normal, point - p ) <= 0.0 ) {
			continue;
		}
		counted += 1.0;
		if ( d > 2.0 && LevelBlocks( q, to / d, d - 2.0 ) ) {
			hidden += 1.0;
		}
	}
	return counted > 0.0 ? hidden / counted : 0.0;
#else
	return 0.0;
#endif
}

// how much of light k the players and the level take at p: the players from the cubes at three points along a blade
// (as VK_SM_DrawAtlas has them), blended, so their shadow is soft along the blade and sweeps as it turns; the level by
// LevelTaken. What the one does not take the other may.
float LightShadow( int k, vec3 p, vec3 normal ) {
	vec4 a = W[38 + k * 2], b = W[39 + k * 2];
	float range = a.w;

	if ( range <= 0.0 ) {
		return 0.0;
	}
	vec3 nearest = NearestOnSegment( a.xyz, b.xyz, p );
	if ( distance( nearest, p ) >= range || dot( normal, nearest - p ) < -12.0 ) {
		return 0.0;
	}
	vec3 q = p + normal * 3.0;
	float players = 0.0;
	if ( b.w <= 0.0 ) {
		players = CubeShadow( 16 + k * 18, a.xyz, q );
		return 1.0 - ( 1.0 - players ) * ( 1.0 - LevelTaken( q, p, normal, a.xyz, a.xyz ) );
	}
	// (where along the blade the three are this frame: rows[21] y, z, w, they jump every frame, VK_SM_DrawAtlas)
	for ( int i = 0; i < 3; i++ ) {
		players += CubeShadow( 16 + ( k * 3 + i ) * 6, mix( a.xyz, b.xyz, W[21][1 + i] ), q );
	}
	return 1.0 - ( 1.0 - players / 3.0 ) * ( 1.0 - LevelTaken( q, p, normal, a.xyz, b.xyz ) );
}

void main() {
	vec3 p = ScenePoint( gl_FragCoord.xy );
	vec3 eye = W[4].xyz;

	if ( distance( p, eye ) > 8192.0 ) {
		o_color = vec4( 0.0 );
		return;
	}
	vec3 normal = SceneSurface( gl_FragCoord.xy, p, normalize( eye - p ) );
	o_color = vec4( LightShadow( 0, p, normal ), LightShadow( 1, p, normal ), 0.0, 0.0 );
}
