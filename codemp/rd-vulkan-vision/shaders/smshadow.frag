#version 460

// cg_shadows 2 on a device without ray tracing (vk_rt.cpp, the shadow maps): the same mask as shaders/rtshadow.frag
// writes (red: how much the players darken the point, green to alpha: how far it is from the eye), from the tiles of
// the atlas in place of rays. Each player has a tile seen along the light of the grid at his feet; a point darkens
// where it lies behind him in that tile. How dark follows the light of the grid at him, as the rays have it (dark
// where the light comes from one side, faint where it is ambient), and the shadow fades out within a few steps of
// his feet. The sun is not used here (rows[11].w 0): the maps do not know where the level hides it.
//
// rows 47 + 4 t: the view and projection of tile t (VK_SM_FIRST_ROW), the tiles 256 pixels in rows of 8

layout( set = 0, binding = 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
	vec4	rt[319];
} globals;
// three vec4 a cell: ambient light (w 1, 0 in a wall), directed light, its direction
layout( set = 1, binding = 1, std430 ) readonly buffer Grid {
	vec4	cells[];
};
layout( set = 1, binding = 4 ) uniform sampler2D u_atlas;	// the depth of the shadow maps
layout( set = 2, binding = 0 ) uniform sampler2D u_depth;	// the depth of the scene, read pixel for pixel

#define W		globals.rt
#define TILE	256.0
#define COLUMNS	8

layout( location = 0 ) out vec4 o_color;

#include "scene.glsl"

float Luma( vec3 c ) {
	return dot( c, vec3( 0.3, 0.59, 0.11 ) );
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


// how much of tile t lies in front of the point q (0 to 1, 3 x 3 pixels around it)
float TileShadow( int t, vec3 q ) {
	vec4 clip = mat4( W[47 + t * 4], W[48 + t * 4], W[49 + t * 4], W[50 + t * 4] ) * vec4( q, 1.0 );
	vec3 ndc = clip.xyz / clip.w;

	if ( clip.w <= 0.0 || any( greaterThan( abs( ndc.xy ), vec2( 0.98 ) ) ) || ndc.z <= 0.0 || ndc.z >= 1.0 ) {
		return 0.0;
	}
	vec2 corner = vec2( float( t % COLUMNS ), float( t / COLUMNS ) ) * TILE;
	ivec2 at = ivec2( corner + ( ndc.xy * 0.5 + 0.5 ) * TILE );
	float blocked = 0.0;
	for ( int y = -1; y <= 1; y++ ) {
		for ( int x = -1; x <= 1; x++ ) {
			blocked += ( texelFetch( u_atlas, at + ivec2( x, y ), 0 ).r < ndc.z ) ? 1.0 : 0.0;
		}
	}
	return blocked / 9.0;
}

// The light of each player is the grid at him, as his tile is (shaders/rtshadow.frag GridShadow says why not the
// grid at the point)
float Shadow( vec3 p, vec3 normal ) {
	// (a little off the surface, against the stripes of a surface that shadows itself)
	vec3 q = p + normal * 3.0;
	float dark = 0.0;
	int count = int( W[21].x );

	for ( int i = 0; i < count; i++ ) {
		vec3 middle = W[22 + i].xyz;
		// 1 at his feet (about 32 below the middle), 0 from 160 units on
		float fade = 1.0 - smoothstep( 48.0, 160.0, distance( p, middle - vec3( 0.0, 0.0, 32.0 ) ) );
		if ( fade <= 0.0 ) {
			continue;
		}
		float ambient, directed;
		vec3 gridDir;
		GridAt( middle, ambient, directed, gridDir );
		if ( dot( gridDir, gridDir ) < 1e-4 ) {
			continue;
		}
		float facing = dot( normal, normalize( gridDir ) );
		if ( facing <= 0.0 ) {
			continue;
		}
		float ratio = directed / ( ambient + directed + 1.0 );
		dark = max( dark, min( ratio, 0.85 ) * TileShadow( i, q ) * smoothstep( 0.0, 0.25, facing ) * fade );
	}
	return dark;
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
