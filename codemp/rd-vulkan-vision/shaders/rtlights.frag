#version 460
#extension GL_EXT_ray_query : require

// The shadows of the players from the moving lights (vk_rt.cpp, after the trace of the mask of shaders/rtshadow.frag):
// for the two lights of rows 38-41, how much of each the players take at the point seen in each pixel, red and
// green. A light that does not reach the point, or whose way to it passes no player (the spheres of rows 21 on), is
// done without a ray. The others trace a few rays to points of the light (along the tube of a blade, a little around
// a point light) against the players alone (mask 2), ending a little before the light so the hilt in the hand does
// not take it all. The light passes of the next frame read this (generic.frag DynamicLight).
//
// rows 38 + 2k: the start of light k and its reach (0: none), 39 + 2k: its end, w 1 for a tube

layout( set = 0, binding = 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
	vec4	rt[47];
} globals;
layout( set = 1, binding = 0 ) uniform accelerationStructureEXT u_top;
layout( set = 2, binding = 0 ) uniform sampler2D u_depth;	// the depth of the scene, read pixel for pixel

#define W		globals.rt

layout( location = 0 ) out vec4 o_color;

#include "scene.glsl"

bool Blocked( vec3 origin, vec3 dir, float tMax, uint mask ) {
	rayQueryEXT query;

	rayQueryInitializeEXT( query, u_top, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT, mask, origin, 0.5, dir, tMax );
	while ( rayQueryProceedEXT( query ) ) {
	}
	return rayQueryGetIntersectionTypeEXT( query, true ) != gl_RayQueryCommittedIntersectionNoneEXT;
}

float Noise( uint k ) {
	uvec3 v = uvec3( uvec2( gl_FragCoord.xy ), uint( W[13].w ) * 16u + k );

	v = v * 1664525u + 1013904223u;
	v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
	v ^= v >> 16u;
	v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
	return float( v.x & 0xffffffu ) / 16777216.0;
}

// whether the way from origin to the point target passes near a player
bool NearCaster( vec3 origin, vec3 target ) {
	int count = int( W[21].x );
	vec3 dir = target - origin;
	float len = length( dir );

	dir /= max( len, 1e-3 );
	for ( int i = 0; i < count; i++ ) {
		vec4 s = W[22 + i];
		float t = clamp( dot( s.xyz - origin, dir ), 0.0, len );
		vec3 closest = origin + dir * t - s.xyz;

		if ( dot( closest, closest ) < s.w * s.w ) {
			return true;
		}
	}
	return false;
}

vec3 NearestOnSegment( vec3 a, vec3 b, vec3 p ) {
	vec3 ab = b - a;
	return a + ab * clamp( dot( p - a, ab ) / max( dot( ab, ab ), 1e-4 ), 0.0, 1.0 );
}

// how much of light k the players take at p, facing normal
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
	vec3 origin = p + normal * 1.5;
	// the near end, the middle and the far end of a blade; a point light is one point
	if ( !NearCaster( origin, nearest ) && !( b.w > 0.0 && ( NearCaster( origin, a.xyz ) || NearCaster( origin, b.xyz ) ) ) ) {
		return 0.0;
	}
	int count = int( W[15].x );
	float blocked = 0.0;
	for ( int r = 0; r < count; r++ ) {
		vec3 target;
		if ( b.w > 0.0 ) {
			target = mix( a.xyz, b.xyz, ( float( r ) + Noise( uint( r ) + uint( k ) * 7u ) ) / float( count ) );
		} else {
			target = a.xyz + ( vec3( Noise( uint( r ) * 3u ), Noise( uint( r ) * 3u + 1u ), Noise( uint( r ) * 3u + 2u ) ) - 0.5 ) * 8.0;
		}
		vec3 to = target - origin;
		float len = length( to );
		blocked += Blocked( origin, to / max( len, 1e-3 ), max( len - 6.0, 1.0 ), 0x02u ) ? 1.0 : 0.0;
	}
	return blocked / float( count );
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
