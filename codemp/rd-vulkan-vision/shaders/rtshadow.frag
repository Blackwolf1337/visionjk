#version 460
#extension GL_EXT_ray_query : require

// cg_shadows 2 on a device that traces rays (vk_rt.cpp): how much of the light of the level the players take from
// each pixel of the view, into the shadow mask (red). The lights of the level (the sun, and the fixtures that lit the
// light grid, VK_RT_LightsFromCells) are weighed by what each gives the point without anything in the way, in the units
// of the grid; every ray picks one of them by its weight and a point on it (in the cone of the sun, on the disk of a
// fixture). A ray against the level says whether the light reaches the point, one against the players alone (mask 2)
// whether a player takes it: the part of the light that reaches it which the players take. Soft by the size of the
// lights: sharp where a player stands on the point, wider the farther he is; several lamps give several shadows.
// How much that darkens is the share of directed light in the light of the grid there. Where none of the lights the
// rays picked reaches the point, the direction of the grid stands in for one. Surfaces that turn away from the light lie in the
// shade of the level already and get nothing.
//
// The numbers are the rt ones of the globals: the view as the wet places have it (rows 0-8), then
//   11 the direction to the sun, w 1 when the level has one
//   12 where the light grid begins, w how far a player may be from a point and still shadow it
//   13 1 / the size of a cell of the grid, w a number that changes every frame (the noise of the rays)
//   14 the cells of the grid along x, y, z, w the tangent of half the cone of the sun
//   15 x rays a pixel, y the tangent of half the cone of the grid light, z how many lights, w the strength of the sun

layout( set = 0, binding = 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
	vec4	rt[16];
} globals;
layout( set = 1, binding = 0 ) uniform accelerationStructureEXT u_top;
// three vec4 a cell: ambient light (w 1, 0 in a wall), directed light, its direction
layout( set = 1, binding = 1, std430 ) readonly buffer Grid {
	vec4	cells[];
};
// three vec4 a light (a fixture of the level, VK_RT_LightsFromCells): origin, 1 / strength, radius of the disk the
// rays aim at, reach, how far the fixture goes from its middle / the side it lights
layout( set = 1, binding = 2, std430 ) readonly buffer Lights {
	vec4	lights[];
};
layout( set = 2, binding = 0 ) uniform sampler2D u_depth;	// the depth of the scene, read pixel for pixel

#define W	globals.rt

layout( location = 0 ) out vec4 o_color;

#include "scene.glsl"

float Luma( vec3 c ) {
	return dot( c, vec3( 0.3, 0.59, 0.11 ) );
}

// any triangle of what mask selects between origin and tMax along dir
bool Blocked( vec3 origin, vec3 dir, float tMax, uint mask ) {
	rayQueryEXT query;

	rayQueryInitializeEXT( query, u_top, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT, mask, origin, 0.5, dir, tMax );
	while ( rayQueryProceedEXT( query ) ) {
	}
	return rayQueryGetIntersectionTypeEXT( query, true ) != gl_RayQueryCommittedIntersectionNoneEXT;
}

// the light grid at p between its eight cells, leaving out those in walls as R_SetupEntityLightingGrid() does
void GridAt( vec3 p, out float ambient, out float directed, out vec3 dir ) {
	vec3 g = ( p - W[12].xyz ) * W[13].xyz;
	ivec3 bounds = ivec3( W[14].xyz );
	ivec3 base = ivec3( floor( g ) );
	vec3 f = fract( g );
	float total = 0.0;

	ambient = directed = 0.0;
	dir = vec3( 0.0 );
	for ( int i = 0; i < 8; i++ ) {
		ivec3 corner = ivec3( i & 1, ( i >> 1 ) & 1, ( i >> 2 ) & 1 );
		ivec3 c = clamp( base + corner, ivec3( 0 ), bounds - 1 );
		int cell = ( c.x + c.y * bounds.x + c.z * bounds.x * bounds.y ) * 3;
		vec4 a = cells[cell];
		vec3 w3 = mix( 1.0 - f, f, vec3( corner ) );
		float w = w3.x * w3.y * w3.z;

		if ( a.w == 0.0 ) {
			continue;
		}
		ambient += w * Luma( a.rgb );
		directed += w * Luma( cells[cell + 1].rgb );
		dir += w * cells[cell + 2].xyz;
		total += w;
	}
	if ( total > 0.0 ) {
		ambient /= total;
		directed /= total;
	} else {
		ambient = 1.0;
		directed = 0.0;
	}
}

// a number in [0, 1) for this pixel, frame and ray
float Noise( uint k ) {
	uvec3 v = uvec3( uvec2( gl_FragCoord.xy ), uint( W[13].w ) * 16u + k );

	v = v * 1664525u + 1013904223u;
	v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
	v ^= v >> 16u;
	v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
	return float( v.x & 0xffffffu ) / 16777216.0;
}

// what light i gives at p, facing normal, without anything in the way
float LightWeight( int i, vec3 p, vec3 normal ) {
	vec4 a = lights[i * 3], b = lights[i * 3 + 1], c = lights[i * 3 + 2];
	vec3 to = a.xyz - p;
	float d2 = dot( to, to );

	if ( d2 > b.z * b.z ) {
		return 0.0;
	}
	to *= inversesqrt( max( d2, 1e-4 ) );
	float facing = dot( normal, to );
	float side = dot( c.xyz, c.xyz ) > 0.5 ? max( dot( c.xyz, -to ), 0.0 ) : 1.0;
	return b.x * max( facing, 0.0 ) * side / max( d2, b.y * b.y + 16.0 );
}

// a direction in the cone of half width tangent around dir
vec3 InCone( vec3 dir, float tangent, uint k ) {
	vec3 t = normalize( cross( dir, abs( dir.z ) < 0.9 ? vec3( 0.0, 0.0, 1.0 ) : vec3( 1.0, 0.0, 0.0 ) ) );
	vec3 b = cross( dir, t );
	float r = sqrt( Noise( k * 3u ) ) * tangent;
	float a = Noise( k * 3u + 1u ) * 6.2831853;

	return normalize( dir + t * ( cos( a ) * r ) + b * ( sin( a ) * r ) );
}

void main() {
	vec3 p = ScenePoint( gl_FragCoord.xy );
	vec3 eye = W[4].xyz;
	vec3 view = normalize( eye - p );
	vec3 normal = SceneSurface( gl_FragCoord.xy, p, view );
	float ambient, directed;
	vec3 gridDir;
	vec3 light;
	float cone;

	o_color = vec4( 0.0, 0.0, 0.0, 1.0 );
	if ( distance( p, eye ) > 8192.0 ) {
		return;
	}
	// (a little off the surface: the cells on it may be in the wall it is)
	GridAt( p + normal * 8.0, ambient, directed, gridDir );
	float strength = min( directed / ( ambient + directed + 1.0 ), 0.85 );
	if ( strength < 0.02 ) {
		return;
	}
	vec3 origin = p + normal * 1.5;
	vec3 sun = W[11].xyz;
	int count = int( W[15].x );
	int numLights = int( W[15].z );
	float sunWeight = ( W[11].w > 0.0 && W[15].w > 0.0 ) ? W[15].w * max( dot( normal, sun ), 0.0 ) : 0.0;
	float total = sunWeight;

	for ( int i = 0; i < numLights; i++ ) {
		total += LightWeight( i, p, normal );
	}
	if ( total > 0.0 ) {
		// each ray: a light by its weight, a point on it, then whether it reaches the point and a player takes it
		float reached = 0.0, taken = 0.0;

		for ( int k = 0; k < count; k++ ) {
			float pick = Noise( uint( k ) * 3u + 2u ) * total;
			vec3 dir;
			float len;

			if ( pick < sunWeight ) {
				dir = InCone( sun, W[14].w, uint( k ) );
				len = 65536.0;
			} else {
				int i = 0;

				pick -= sunWeight;
				for ( ; i < numLights - 1; i++ ) {
					pick -= LightWeight( i, p, normal );
					if ( pick < 0.0 ) {
						break;
					}
				}
				vec3 to = lights[i * 3].xyz - origin;
				float d = length( to );

				// a point on its disk, the disk facing the point; the ray against the level ends where the fixture
				// begins, which is in the level itself when it is solid
				dir = InCone( to / d, lights[i * 3 + 1].y / max( d, 1.0 ), uint( k ) );
				len = max( d - lights[i * 3 + 1].w - 2.0, 1.0 );
			}
			if ( !Blocked( origin, dir, len, 0x01u ) ) {
				reached += 1.0;
				taken += Blocked( origin, dir, min( len, W[12].w ), 0x02u ) ? 1.0 : 0.0;
			}
		}
		if ( reached > 0.0 ) {
			o_color.r = strength * taken / reached;
			return;
		}
	}

	// no light of the list reaches it: the direction of the grid
	if ( dot( gridDir, gridDir ) < 1e-4 ) {
		return;
	}
	light = normalize( gridDir );
	cone = W[15].y;
	float facing = dot( normal, light );
	if ( facing <= 0.0 ) {
		return;
	}
	float blocked = 0.0;
	for ( int k = 0; k < count; k++ ) {
		blocked += Blocked( origin, InCone( light, cone, uint( k ) ), W[12].w, 0x02u ) ? 1.0 : 0.0;
	}
	o_color.r = strength * blocked / float( count ) * smoothstep( 0.0, 0.25, facing );
}
