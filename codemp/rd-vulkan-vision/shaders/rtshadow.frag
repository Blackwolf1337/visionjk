#version 460
#extension GL_EXT_ray_query : require

// cg_shadows 2 on a device that traces rays (vk_rt.cpp): how much of the light of the level the players take from
// each pixel of the view, into the shadow mask (red). The lights at a point are the sun and the light patches of the
// list of the grid cell it is in (the textures that give light, VK_RT_Patches; the list has those the cell sees),
// each weighed by the light it gives the point without anything in the way, in the units of the light grid. Every
// ray picks one of them by its weight and a point on it (in the cone of the sun, on the disk of a patch): a ray
// against the level says whether that light reaches the point, one against the players alone (mask 2) whether a
// player takes it. The darkness is the light the players take over all the light there, the light that reaches the
// point and the ambient light of the grid: next to a bright lamp the shadow is dark, from a weak far one faint, and
// every lamp gives its own shadow in its own direction, sharp where the player stands, softer the farther he is by
// the size of the lamp. Where no light of the list reaches the point the direction of the grid stands in for one.
// Surfaces that turn away from a light lie in the shade of the level already and get nothing from it.
//
// The numbers are the rt ones of the globals: the view as the wet places have it (rows 0-8), then
//   11 the direction to the sun, w 1 when the level has one
//   12 where the light grid begins, w how far a player may be from a point and still shadow it
//   13 1 / the size of a cell of the grid, w a number that changes every frame (the noise of the rays)
//   14 the cells of the grid along x, y, z, w the tangent of half the cone of the sun
//   15 x rays a pixel, y the tangent of half the cone of the grid light, z the scale of the light of the patches
//      (0: they are not used), w the strength of the sun

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
// three vec4 a light patch: middle and power, the side it lights and the radius of its disk, how far it goes
layout( set = 1, binding = 2, std430 ) readonly buffer Patches {
	vec4	patches[];
};
// sixteen words a cell: the numbers of up to eight patches it sees (all bits: none), then the light of each
layout( set = 1, binding = 3, std430 ) readonly buffer Lists {
	uint	lists[];
};
layout( set = 2, binding = 0 ) uniform sampler2D u_depth;	// the depth of the scene, read pixel for pixel

#define W		globals.rt
#define LIST	8
#define NONE	0xffffffffu

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

// a direction in the cone of half width tangent around dir
vec3 InCone( vec3 dir, float tangent, uint k ) {
	vec3 t = normalize( cross( dir, abs( dir.z ) < 0.9 ? vec3( 0.0, 0.0, 1.0 ) : vec3( 1.0, 0.0, 0.0 ) ) );
	vec3 b = cross( dir, t );
	float r = sqrt( Noise( k * 3u ) ) * tangent;
	float a = Noise( k * 3u + 1u ) * 6.2831853;

	return normalize( dir + t * ( cos( a ) * r ) + b * ( sin( a ) * r ) );
}

// the cell of the grid nearest p, -1: outside the grid or in a wall
int CellAt( vec3 p ) {
	ivec3 bounds = ivec3( W[14].xyz );
	ivec3 c = ivec3( floor( ( p - W[12].xyz ) * W[13].xyz + 0.5 ) );

	if ( any( lessThan( c, ivec3( 0 ) ) ) || any( greaterThanEqual( c, bounds ) ) ) {
		return -1;
	}
	int cell = c.x + c.y * bounds.x + c.z * bounds.x * bounds.y;
	return cells[cell * 3].w != 0.0 ? cell : -1;
}

void main() {
	vec3 p = ScenePoint( gl_FragCoord.xy );
	vec3 eye = W[4].xyz;
	vec3 view = normalize( eye - p );
	vec3 normal = SceneSurface( gl_FragCoord.xy, p, view );
	float ambient, directed;
	vec3 gridDir;

	o_color = vec4( 0.0, 0.0, 0.0, 1.0 );
	if ( distance( p, eye ) > 8192.0 ) {
		return;
	}
	// (a little off the surface: the cells on it may be in the wall it is)
	GridAt( p + normal * 8.0, ambient, directed, gridDir );
	vec3 origin = p + normal * 1.5;
	vec3 sun = W[11].xyz;
	int count = int( W[15].x );
	float scale = W[15].z;

	// the lights at the point and what each gives it; the sun only where it shines (else most rays would go to it
	// and find the roof)
	float sunWeight = ( W[11].w > 0.0 && W[15].w > 0.0 ) ? W[15].w * max( dot( normal, sun ), 0.0 ) : 0.0;
	if ( sunWeight > 0.0 && Blocked( origin, sun, 65536.0, 0x01u ) ) {
		sunWeight = 0.0;
	}
	float weights[LIST];
	uint which[LIST];
	float total = sunWeight;
	int cell = scale > 0.0 ? CellAt( p + normal * 12.0 ) : -1;

	for ( int k = 0; k < LIST; k++ ) {
		weights[k] = 0.0;
		which[k] = cell >= 0 ? lists[cell * 16 + k] : NONE;
		if ( which[k] == NONE ) {
			continue;
		}
		vec4 a = patches[which[k] * 3u], b = patches[which[k] * 3u + 1u];
		vec3 to = a.xyz - p;
		float d2 = dot( to, to );

		to *= inversesqrt( max( d2, 1e-4 ) );
		float side = dot( b.xyz, b.xyz ) > 0.5 ? max( dot( b.xyz, -to ), 0.0 ) : 1.0;
		weights[k] = scale * a.w * side * max( dot( normal, to ), 0.0 ) / max( d2, b.w * b.w + 16.0 );
		total += weights[k];
	}

	if ( total > 0.0 ) {
		float reached = 0.0, taken = 0.0;

		for ( int k = 0; k < count; k++ ) {
			float pick = Noise( uint( k ) * 3u + 2u ) * total;
			vec3 dir;
			float len;

			if ( pick < sunWeight ) {
				dir = InCone( sun, W[14].w, uint( k ) );
				len = 65536.0;
			} else {
				int j = 0;

				pick -= sunWeight;
				for ( ; j < LIST - 1; j++ ) {
					pick -= weights[j];
					if ( pick < 0.0 ) {
						break;
					}
				}
				vec4 a = patches[which[j] * 3u];
				vec3 to = a.xyz - origin;
				float d = length( to );

				// a point on its disk, the disk facing the point; the ray against the level ends where the patch
				// begins, which is the level itself when it is solid
				dir = InCone( to / d, patches[which[j] * 3u + 1u].w / max( d, 1.0 ), uint( k ) );
				len = max( d - patches[which[j] * 3u + 2u].x - 1.0, 1.0 );
			}
			if ( !Blocked( origin, dir, len, 0x01u ) ) {
				reached += 1.0;
				taken += Blocked( origin, dir, min( len, W[12].w ), 0x02u ) ? 1.0 : 0.0;
			}
		}
		if ( reached > 0.0 ) {
			// the light taken and the light that reaches, both in the units of the grid
			float takenLight = total * taken / float( count );
			float reachedLight = total * reached / float( count );

			o_color.r = min( takenLight / ( reachedLight + ambient + 1.0 ), 0.9 );
			return;
		}
	}

	// no light of the list reaches it: the direction of the grid
	if ( dot( gridDir, gridDir ) < 1e-4 ) {
		return;
	}
	vec3 light = normalize( gridDir );
	float facing = dot( normal, light );
	if ( facing <= 0.0 ) {
		return;
	}
	float strength = min( directed / ( ambient + directed + 1.0 ), 0.85 );
	float blocked = 0.0;
	for ( int k = 0; k < count; k++ ) {
		blocked += Blocked( origin, InCone( light, W[15].y, uint( k ) ), W[12].w, 0x02u ) ? 1.0 : 0.0;
	}
	o_color.r = strength * blocked / float( count ) * smoothstep( 0.0, 0.25, facing );
}
