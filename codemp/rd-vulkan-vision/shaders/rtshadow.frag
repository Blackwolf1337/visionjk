#version 460
#extension GL_EXT_ray_query : require

// cg_shadows 2 on a device that traces rays (vk_rt.cpp): how much of the light of the level the players take from
// each pixel of the view, into the shadow mask (red). The light at a point is the sun where it reaches it, else the
// direction of the light grid there (Shadow below). First the ray towards that light is tested against spheres
// around the players without tracing anything: a pixel whose light passes none of them is done. The others trace
// a few rays in the cone of the light against the players alone (mask 2), and the sun one ray against the level
// (mask 1) to know it shines there. Surfaces that turn away from the light lie in the shade of the level already
// and get nothing.
//
// The numbers are the rt ones of the globals: the view as the wet places have it (rows 0-8), then
//   11 the direction to the sun, w 1 when the level has one
//   12 where the light grid begins, w how far a player may be from a point and still shadow it
//   13 1 / the size of a cell of the grid, w a number that changes every frame (the noise of the rays)
//   14 the cells of the grid along x, y, z, w the tangent of half the cone of the sun
//   15 x rays a pixel, y the tangent of half the cone of the grid light, z 0 (was the scale of the light patches),
//      w the strength of the sun
//   16-19 the view and projection of the frame before, 20 its eye, w 1 when the mask of that frame can be used
//   21 x how many players follow, 22-37 each as a sphere: middle and radius
//
// The mask of the frame before (set 3, the same pixels: the mask in red, how far the point was from the eye in green,
// blue and alpha) is where this frame's rays are added to: the point seen in a pixel is looked up where it was then,
// and if that pixel saw the same point (by its distance), a fifth of this frame goes onto it. So the few rays a pixel
// has become many over a few frames.

layout( set = 0, binding = 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
	vec4	rt[159];
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
layout( set = 3, binding = 0 ) uniform sampler2D u_history;	// the mask of the frame before, read pixel for pixel

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

// whether the ray from origin along dir (up to len, widening by tangent a unit) passes near one of the players:
// their spheres in rows 22 on, as many as row 21 x. Most pixels of a view pass none and trace nothing.
bool NearCaster( vec3 origin, vec3 dir, float len, float tangent ) {
	int count = int( W[21].x );

	for ( int i = 0; i < count; i++ ) {
		vec4 s = W[22 + i];
		float t = clamp( dot( s.xyz - origin, dir ), 0.0, len );
		vec3 closest = origin + dir * t - s.xyz;

		if ( dot( closest, closest ) < ( s.w + tangent * t ) * ( s.w + tangent * t ) ) {
			return true;
		}
	}
	return false;
}

// 1 at the feet of the nearest player, 0 from 160 units on (the shadows from the light of the grid)
float CasterFade( vec3 p ) {
	int count = int( W[21].x );
	float nearest = 1e6;

	for ( int i = 0; i < count; i++ ) {
		// (the middle of the sphere is a little above the origin of the player: his feet are about 32 below it)
		vec3 feet = W[22 + i].xyz - vec3( 0.0, 0.0, 32.0 );
		nearest = min( nearest, distance( p, feet ) );
	}
	return 1.0 - smoothstep( 48.0, 160.0, nearest );
}

// how much the players darken the point p of this pixel, facing normal, by the rays of this frame. The light is the
// one the lightmap was made with as far as the level still knows it: the sun where it reaches the point, else the
// direction of the light grid there. How dark and how sharp follows how much of the light at the point comes from
// that direction: outdoors in the sun dark and sharp, indoors where the grid is mostly ambient light faint and soft.
// (The light patches of the textures that glow cast no shadows since 2026-11-08: the lamps they guessed did not
// agree with the lightmaps, and every one gave a shadow of its own in its own direction.)
float Shadow( vec3 p, vec3 normal ) {
	float ambient, directed;
	vec3 gridDir;

	// (a little off the surface: the cells on it may be in the wall it is)
	GridAt( p + normal * 8.0, ambient, directed, gridDir );
	vec3 origin = p + normal * 1.5;
	vec3 sun = W[11].xyz;
	int count = int( W[15].x );
	float reach = W[12].w;
	// how much of the light comes from the direction of the grid, 0 to 1
	float ratio = directed / ( ambient + directed + 1.0 );

	bool sunUp = W[11].w > 0.0 && W[15].w > 0.0 && dot( normal, sun ) > 0.0;
	bool gridUp = dot( gridDir, gridDir ) > 1e-4;
	vec3 light = gridUp ? normalize( gridDir ) : vec3( 0.0, 0.0, 1.0 );
	float facing = dot( normal, light );
	// the cone of the grid light: wide where the light is mostly ambient, as narrow as v_rtLightSize says where
	// most of it comes from one side
	float gridTangent = W[15].y * mix( 3.0, 1.0, clamp( ratio * 1.5, 0.0, 1.0 ) );

	bool nearSun = sunUp && NearCaster( origin, sun, reach, W[14].w );
	bool nearGrid = gridUp && facing > 0.0 && NearCaster( origin, light, reach, gridTangent );
	if ( !nearSun && !nearGrid ) {
		return 0.0;
	}

	// How much of the sun reaches the point, from rays in a cone against the level. Not one ray: the shade of a
	// wall in the lightmap has a soft edge (a texel of a lightmap is 16 units or more), and a point that took either
	// the sun shadow or the grid one by a single ray cut the shadow of a player along a hard line where it ran into
	// that shade (2026-11-08). The cone is wider than the sun for that: about the softness of the lightmap's edges
	// a few hundred units from the wall.
	float sunReach = 0.0;
	float sunDark = 0.0;
	if ( sunUp ) {
		const float LIGHTMAP_SOFTNESS = 0.06;	// tangent of half the cone of the rays against the level
		float reached = 0.0, taken = 0.0;

		for ( int k = 0; k < count; k++ ) {
			if ( !Blocked( origin, InCone( sun, max( W[14].w, LIGHTMAP_SOFTNESS ), uint( k ) + 16u ), 65536.0, 0x01u ) ) {
				reached += 1.0;
				if ( nearSun && Blocked( origin, InCone( sun, W[14].w, uint( k ) ), reach, 0x02u ) ) {
					taken += 1.0;
				}
			}
		}
		sunReach = reached / float( count );
		if ( reached > 0.0 ) {
			float sunLight = W[15].w * dot( normal, sun );
			sunDark = min( sunLight * ( taken / reached ) / ( sunLight + ambient + 1.0 ), 0.9 );
		}
	}

	// the direction of the grid, for what the sun does not reach
	float gridDark = 0.0;
	if ( nearGrid && sunReach < 1.0 ) {
		float strength = min( ratio, 0.85 );
		float blocked = 0.0;
		for ( int k = 0; k < count; k++ ) {
			blocked += Blocked( origin, InCone( light, gridTangent, uint( k ) ), reach, 0x02u ) ? 1.0 : 0.0;
		}
		// The grid light is many lamps taken together, its direction an average that changes from cell to cell: a
		// long shadow along it is nothing real, and where the direction or the strength changed under it the shadow
		// ended in a line (2026-11-08, mp/ffa3). It is dark at the feet and fades out within a few steps.
		gridDark = strength * blocked / float( count ) * smoothstep( 0.0, 0.25, facing ) * CasterFade( p );
	}
	return mix( gridDark, sunDark, sunReach );
}

// a distance from the eye in green, blue and alpha, 24 bits up to 8192 units
vec3 PackDistance( float d ) {
	uint v = uint( clamp( d / 8192.0, 0.0, 1.0 ) * 16777215.0 );

	return vec3( float( ( v >> 16u ) & 255u ), float( ( v >> 8u ) & 255u ), float( v & 255u ) ) / 255.0;
}

float UnpackDistance( vec3 c ) {
	uvec3 b = uvec3( round( c * 255.0 ) );

	return float( ( b.x << 16u ) | ( b.y << 8u ) | b.z ) / 16777215.0 * 8192.0;
}

void main() {
	vec3 p = ScenePoint( gl_FragCoord.xy );
	vec3 eye = W[4].xyz;
	float d = distance( p, eye );

	if ( d > 8192.0 ) {
		o_color = vec4( 0.0, PackDistance( 8192.0 ) );
		return;
	}
	float mask = Shadow( p, SceneSurface( gl_FragCoord.xy, p, normalize( eye - p ) ) );

	// onto what the frame before had for this point, where it saw it
	if ( W[20].w > 0.0 ) {
		vec4 clip = mat4( W[16], W[17], W[18], W[19] ) * vec4( p, 1.0 );

		if ( clip.w > 1.0 ) {
			vec2 ndc = clip.xy / clip.w;
			vec4 rect = W[8];
			ivec2 at = ivec2( floor( vec2( rect.x + ( ndc.x * 0.5 + 0.5 ) * rect.z, rect.y + ( 0.5 - ndc.y * 0.5 ) * rect.w ) ) );

			if ( all( greaterThanEqual( at, ivec2( rect.xy ) ) ) && all( lessThan( at, ivec2( rect.xy + rect.zw ) ) ) ) {
				vec4 before = texelFetch( u_history, at, 0 );
				float then = distance( p, W[20].xyz );

				if ( abs( UnpackDistance( before.gba ) - then ) < then * 0.02 + 2.0 ) {
					mask = mix( before.r, mask, 0.2 );
				}
			}
		}
	}
	o_color = vec4( mask, PackDistance( d ) );
}
