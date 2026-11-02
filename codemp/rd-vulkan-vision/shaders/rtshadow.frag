#version 460
#extension GL_EXT_ray_query : require

// cg_shadows 2 on a device that traces rays (vk_rt.cpp): how much of the light of the level the players take from
// each pixel of the view, into the shadow mask (red). The light at a point: the sun of the sky shader where a ray
// towards it gets out of the level, else the direction the light grid has there. A few rays in a cone around it
// against the players alone (mask 2) give the part they block, soft by the width of the cone: sharp where a player
// stands on the point, wider the farther he is. How much that darkens is the share of directed light in the light
// of the grid there. Surfaces that turn away from the light lie in the shade of the level already and get nothing.
//
// The numbers are the rt ones of the globals: the view as the wet places have it (rows 0-8), then
//   11 the direction to the sun, w 1 when the level has one
//   12 where the light grid begins, w how far a player may be from a point and still shadow it
//   13 1 / the size of a cell of the grid, w a number that changes every frame (the noise of the rays)
//   14 the cells of the grid along x, y, z, w the tangent of half the cone of the sun
//   15 x rays a pixel, y the tangent of half the cone of the grid light

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
	if ( W[11].w > 0.0 && dot( normal, sun ) > 0.0 && !Blocked( origin, sun, 65536.0, 0x01u ) ) {
		light = sun;
		cone = W[14].w;
	} else if ( dot( gridDir, gridDir ) > 1e-4 ) {
		light = normalize( gridDir );
		cone = W[15].y;
	} else {
		return;
	}
	float facing = dot( normal, light );
	if ( facing <= 0.0 ) {
		return;
	}

	vec3 t = normalize( cross( light, abs( light.z ) < 0.9 ? vec3( 0.0, 0.0, 1.0 ) : vec3( 1.0, 0.0, 0.0 ) ) );
	vec3 b = cross( light, t );
	int count = int( W[15].x );
	float blocked = 0.0;
	for ( int k = 0; k < count; k++ ) {
		// a point in the disk of the cone
		float r = sqrt( Noise( uint( k ) * 2u ) ) * cone;
		float a = Noise( uint( k ) * 2u + 1u ) * 6.2831853;
		vec3 dir = normalize( light + t * ( cos( a ) * r ) + b * ( sin( a ) * r ) );

		blocked += Blocked( origin, dir, W[12].w, 0x02u ) ? 1.0 : 0.0;
	}
	o_color.r = strength * blocked / float( count ) * smoothstep( 0.0, 0.25, facing );
}
