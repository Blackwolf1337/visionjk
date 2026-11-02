#version 460
#extension GL_EXT_ray_query : require

// v_rtDebug (vk_rt.cpp): what the rays see, over the view of the level. The numbers are the rt ones of the globals:
// the view as the wet places have it (rows 0-8), then row 11: the direction to the sun, w the mode.
//   1: a ray from each pixel towards the sun. White where it gets out, dark where the level is in the way: outdoors
//      that has to be the shadow in the lightmaps.
//   2: a ray from the eye through each pixel against the depth of the scene: black where they agree, red where the
//      ray stops in front of the depth (something in the structure that is not drawn), green where it goes past it
//      (something drawn that is not in the structure: what moves, glass, grates, the sky).
//   3: the rays towards the sun against the players alone (mask 2): dark where a player is in the way, their sun
//      shadows, also on themselves.
//   4: the light patches of the level (the textures that give light, VK_RT_Patches) as dots over the scene, larger
//      and whiter the more power they have, dim where something is in front of them.
//   5: the direction of the strongest light in the list of the grid cell of each pixel as a colour (x red, y green,
//      z blue, half grey for 0); black where the cell has none.

layout( set = 0, binding = 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
	vec4	rt[21];
} globals;
layout( set = 1, binding = 0 ) uniform accelerationStructureEXT u_top;
layout( set = 1, binding = 1, std430 ) readonly buffer Grid {
	vec4	cells[];		// three a cell, the first: ambient light, w 0 in a wall
};
layout( set = 1, binding = 2, std430 ) readonly buffer Patches {
	vec4	patches[];		// three a patch: middle and power, the side it lights and its radius, how far it goes
};
layout( set = 1, binding = 3, std430 ) readonly buffer Lists {
	uint	lists[];		// sixteen a cell: up to eight patches (all bits: none), their light
};
layout( set = 2, binding = 0 ) uniform sampler2D u_depth;	// the depth of the scene, read pixel for pixel

#define W	globals.rt

layout( location = 0 ) out vec4 o_color;

#include "scene.glsl"

// the distance to the first triangle of what mask selects along the ray (1 the level, 2 the players), or tMax
float Trace( vec3 origin, vec3 dir, float tMax, uint mask ) {
	rayQueryEXT query;

	rayQueryInitializeEXT( query, u_top, gl_RayFlagsOpaqueEXT, mask, origin, 0.0, dir, tMax );
	while ( rayQueryProceedEXT( query ) ) {
	}
	if ( rayQueryGetIntersectionTypeEXT( query, true ) == gl_RayQueryCommittedIntersectionNoneEXT ) {
		return tMax;
	}
	return rayQueryGetIntersectionTEXT( query, true );
}

void main() {
	vec3 p = ScenePoint( gl_FragCoord.xy );
	int mode = int( W[11].w );

	if ( mode == 4 ) {
		vec3 eye = W[4].xyz;
		vec3 dir = normalize( p - eye );
		float depth = distance( p, eye );

		for ( int i = 0; i < patches.length() / 3; i++ ) {
			vec3 to = patches[i * 3].xyz - eye;
			float power = patches[i * 3].w;
			float t = dot( to, dir );
			float size = 1.5 + 0.003 * t + log2( 1.0 + power ) * 0.3;

			if ( power > 0.0 && t > 0.0 && length( to - dir * t ) < size ) {
				vec3 c = mix( vec3( 1.0, 0.5, 0.1 ), vec3( 1.0, 1.0, 0.9 ), clamp( log2( 1.0 + power ) / 12.0, 0.0, 1.0 ) );

				o_color = vec4( t < depth + size ? c : c * 0.35, 1.0 );
				return;
			}
		}
		discard;
	} else if ( mode == 5 ) {
		vec3 normal = SceneSurface( gl_FragCoord.xy, p, normalize( W[4].xyz - p ) );
		vec3 q = p + normal * 12.0;
		ivec3 bounds = ivec3( W[14].xyz );
		ivec3 c = ivec3( floor( ( q - W[12].xyz ) * W[13].xyz + 0.5 ) );

		o_color = vec4( 0.0, 0.0, 0.0, 1.0 );
		if ( all( greaterThanEqual( c, ivec3( 0 ) ) ) && all( lessThan( c, bounds ) ) ) {
			int cell = c.x + c.y * bounds.x + c.z * bounds.x * bounds.y;
			uint first = lists[cell * 16];

			if ( cells[cell * 3].w != 0.0 && first != 0xffffffffu ) {
				o_color = vec4( normalize( patches[first * 3u].xyz - p ) * 0.5 + 0.5, 1.0 );
			}
		}
	} else if ( mode == 1 || mode == 3 ) {
		vec3 sun = normalize( W[11].xyz );
		vec3 normal = SceneSurface( gl_FragCoord.xy, p, sun );
		// off the surface a little, so the ray does not hit the triangle it starts on
		float t = Trace( p + normal * 1.0, sun, 65536.0, mode == 1 ? 0x01u : 0x02u );

		o_color = vec4( vec3( t >= 65536.0 ? 1.0 : 0.12 ), 1.0 );
	} else {
		vec3 eye = W[4].xyz;
		float depth = distance( p, eye );
		float t = Trace( eye, normalize( p - eye ), depth + 64.0, 0x01u );
		float diff = clamp( ( depth - t ) / 16.0, -1.0, 1.0 );

		o_color = vec4( max( diff, 0.0 ), max( -diff, 0.0 ), 0.0, 1.0 );
	}
}
