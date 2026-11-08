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
//   15 x rays a pixel, y the tangent of half the cone of the grid light, z 0,
//      w the strength of the sun
//   16-19 the view and projection of the frame before, 20 its eye, w 1 when the mask of that frame can be used
//   21 x how many players follow, 22-37 each as a sphere: middle and radius
//   255 on, four for each player (VK_RT_PickLamps): three panels that cast his shadow (where it is, w the radius of
//      its disk), then their shares of his light in x, y, z and the share of the direction of the grid in w
//
// It writes the rays of this frame alone (the mask in red, how far the point is from the eye in green, blue and alpha);
// shaders/rtaccum.frag lays them onto the frames before, so the few rays a pixel has become many.

layout( set = 0, binding = 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
	vec4	rt[319];
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
#define LAMPS	255
#define LAMP_ROWS	4
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

// how far along dir the ray from origin found something of what mask selects before tMax (one of them, not
// always the nearest), -1 for nothing
float HitAt( vec3 origin, vec3 dir, float tMax, uint mask ) {
	rayQueryEXT query;

	rayQueryInitializeEXT( query, u_top, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT, mask, origin, 0.5, dir, tMax );
	while ( rayQueryProceedEXT( query ) ) {
	}
	return rayQueryGetIntersectionTypeEXT( query, true ) != gl_RayQueryCommittedIntersectionNoneEXT
		? rayQueryGetIntersectionTEXT( query, true ) : -1.0;
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

// whether the ray from origin along dir (up to len, widening by tangent a unit) passes near the sphere s of a player
bool NearSphere( vec3 origin, vec3 dir, float len, float tangent, vec4 s ) {
	float t = clamp( dot( s.xyz - origin, dir ), 0.0, len );
	vec3 closest = origin + dir * t - s.xyz;

	return dot( closest, closest ) < ( s.w + tangent * t ) * ( s.w + tangent * t );
}

// whether it passes near one of the players: their spheres in rows 22 on, as many as row 21 x. Most pixels of a view
// pass none and trace nothing.
bool NearCaster( vec3 origin, vec3 dir, float len, float tangent ) {
	int count = int( W[21].x );

	for ( int i = 0; i < count; i++ ) {
		if ( NearSphere( origin, dir, len, tangent, W[22 + i] ) ) {
			return true;
		}
	}
	return false;
}

// 1 at the feet of player i (about 32 below the middle of his sphere), 0 from 160 units on, up to 256 as far as
// panels cast his shadow (its length is real then)
float FeetFade( vec3 p, int i ) {
	float end = mix( 256.0, 160.0, W[LAMPS + i * LAMP_ROWS + 3].w );
	return 1.0 - smoothstep( 48.0, end, distance( p, W[22 + i].xyz - vec3( 0.0, 0.0, 32.0 ) ) );
}

// how much of the light from dir (up to len, a cone of tangent) player s takes from origin, 0 to 1, by rays
// rays; 0 where the surface turns away from that light. The level counts as well: where it stands between the
// point and the player he casts nothing there (his shadow went through the ledge he stood on, onto the floor below,
// 2026-11-08), and for a light whose place is known (lightKnown: a panel) also where the level hides the light
// from the point anyway (its shadow is in the lightmap already). Not beyond him along the direction of the grid:
// that is an average of many lamps and runs into walls where the light comes from elsewhere.
float TakenFrom( vec3 origin, vec3 normal, vec3 dir, float len, float tangent, vec4 s, int rays, bool lightKnown ) {
	float facing = dot( normal, dir );

	if ( facing <= 0.0 || !NearSphere( origin, dir, len, tangent, s ) ) {
		return 0.0;
	}
	float blocked = 0.0;
	for ( int k = 0; k < rays; k++ ) {
		vec3 ray = InCone( dir, tangent, uint( k ) );
		float atPlayer = HitAt( origin, ray, len, 0x02u );

		if ( atPlayer > 0.0 && !Blocked( origin, ray, lightKnown ? len : atPlayer, 0x01u ) ) {
			blocked += 1.0;
		}
	}
	return blocked / float( rays ) * smoothstep( 0.0, 0.25, facing );
}

// The shadows from the light of the grid, rays from origin. A player's light is shared between up to three panels
// that glow on the side the grid has his light from (rows 255 on, VK_RT_PickLamps), each casting a shadow of its
// share from where it is, soft by its size, and the direction of the grid at him for the rest. One panel for all
// of it made the shadow jump from lamp to lamp as he walked (2026-11-08); the shares change slowly. How dark all of
// it is together is the strength of the grid there.
// Not the grid at the point: its direction is an average of many lamps that changes from cell to cell, and a shadow
// whose every pixel went its own way bent into hooks and ended in lines where the cells changed (2026-11-08, under
// the lamps of mp/ffa3). It is dark at his feet and fades out within a few steps: a long shadow along an averaged
// direction is nothing real.
float GridShadow( vec3 origin, vec3 normal, float reach, int rays ) {
	int count = int( W[21].x );
	float dark = 0.0;

	for ( int i = 0; i < count; i++ ) {
		vec4 s = W[22 + i];
		float fade = FeetFade( origin, i );
		if ( fade <= 0.0 ) {
			continue;
		}
		float ambient, directed;
		vec3 gridDir;
		GridAt( s.xyz, ambient, directed, gridDir );
		// how much of his light comes from one side, 0 to 1
		float ratio = directed / ( ambient + directed + 1.0 );
		vec4 shares = W[LAMPS + i * LAMP_ROWS + 3];
		float strength = min( ratio, 0.85 );
		float taken = 0.0;
		// the share of the grid: its direction, the cone wide where the light is mostly ambient, as narrow as
		// v_rtLightSize says where most of it comes from one side
		if ( shares.w > 0.01 && dot( gridDir, gridDir ) > 1e-4 ) {
			float tangent = W[15].y * mix( 3.0, 1.0, clamp( ratio * 1.5, 0.0, 1.0 ) );
			taken += strength * shares.w * TakenFrom( origin, normal, normalize( gridDir ), reach, tangent, s,
				max( 1, int( ceil( float( rays ) * shares.w ) ) ), false );
		}
		// the panels: towards each from this point, the cone from its disk, the rays end before it (fewer rays for a small
		// share, the frames before add up the rest)
		for ( int k = 0; k < 3; k++ ) {
			float share = shares[k];
			if ( share <= 0.01 ) {
				continue;
			}
			vec4 lamp = W[LAMPS + i * LAMP_ROWS + k];
			vec3 to = lamp.xyz - origin;
			float d = max( length( to ), 1.0 );
			// A panel right by him gives a clear shadow even where the grid has the light mostly ambient: by the
			// grid alone the shadow of the lamp beside him was too faint to see (2026-11-08)
			float near = 1.0 - smoothstep( 96.0, 192.0, distance( lamp.xyz, s.xyz ) );
			// (the cone a quarter of the disk: the whole disk of a panel beside him, 40 units, made the shadow of a
			// player 16 units wide so soft that nothing of it was left, 2026-11-08)
			taken += max( strength, 0.6 * near ) * share * TakenFrom( origin, normal, to / d, min( max( d - lamp.w, 1.0 ), reach ),
				clamp( 0.25 * lamp.w / d, 0.005, 0.06 ), s,
				max( 1, int( ceil( float( rays ) * share ) ) ), true );
		}
		dark = max( dark, taken * fade );
	}
	return dark;
}

// how much the players darken the point p of this pixel, facing normal, by the rays of this frame. The light is the
// one the lightmap was made with as far as the level still knows it: the sun where it reaches the point, else the
// light of the grid at each player (GridShadow). How dark and how sharp follows how much of the light comes from
// that direction: outdoors in the sun dark and sharp, indoors where the grid is mostly ambient light faint and soft.
// (The light patches of the textures that glow cast no shadows since 2026-11-08: the lamps they guessed did not
// agree with the lightmaps, and every one gave a shadow of its own in its own direction.)
float Shadow( vec3 p, vec3 normal ) {
	vec3 origin = p + normal * 1.5;
	vec3 sun = W[11].xyz;
	int count = int( W[15].x );
	float reach = W[12].w;

	bool sunUp = W[11].w > 0.0 && W[15].w > 0.0 && dot( normal, sun ) > 0.0;
	bool nearSun = sunUp && NearCaster( origin, sun, reach, W[14].w );
	// (GridShadow tests each player itself, here only whether one stands near enough)
	bool nearGrid = false;
	for ( int i = 0; i < int( W[21].x ) && !nearGrid; i++ ) {
		nearGrid = FeetFade( origin, i ) > 0.0;
	}
	if ( !nearSun && !nearGrid ) {
		return 0.0;
	}
	float ambient, directed;
	vec3 gridDir;
	// the light at the point, for how bright the sun is against the rest (a little off the surface: the cells on it
	// may be in the wall it is)
	GridAt( p + normal * 8.0, ambient, directed, gridDir );

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

	// the light of the grid, for what the sun does not reach
	float gridDark = ( nearGrid && sunReach < 1.0 ) ? GridShadow( origin, normal, reach, count ) : 0.0;
	return mix( gridDark, sunDark, sunReach );
}

// a distance from the eye in green, blue and alpha, 24 bits up to 8192 units
vec3 PackDistance( float d ) {
	uint v = uint( clamp( d / 8192.0, 0.0, 1.0 ) * 16777215.0 );

	return vec3( float( ( v >> 16u ) & 255u ), float( ( v >> 8u ) & 255u ), float( v & 255u ) ) / 255.0;
}

void main() {
	vec3 p = ScenePoint( gl_FragCoord.xy );
	vec3 eye = W[4].xyz;
	float d = distance( p, eye );

	if ( d > 8192.0 ) {
		o_color = vec4( 0.0, PackDistance( 8192.0 ) );
		return;
	}
	// (what the frames before had is added by shaders/rtaccum.frag)
	float mask = Shadow( p, SceneSurface( gl_FragCoord.xy, p, normalize( eye - p ) ) );
	o_color = vec4( mask, PackDistance( d ) );
}
