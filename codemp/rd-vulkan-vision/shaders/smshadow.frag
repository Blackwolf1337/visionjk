#version 460

// cg_shadows 2 on a device without ray tracing (vk_rt.cpp, the shadow maps): the same mask as shaders/rtshadow.frag
// writes (red: how much the players darken the point, green to alpha: how far it is from the eye), from the tiles of
// the atlas in place of rays. Each player has a tile seen along the light of the grid at his feet; a point darkens
// where it lies behind him in that tile. How dark follows the light of the grid at him, as the rays have it (dark
// where the light comes from one side, faint where it is ambient), and the shadow fades out within a few steps of
// his feet. The sun is not used here (rows[11].w 0): the maps do not know where the level hides it.
// Where panels that glow light him (v_rtLamps, vk_rt.cpp VK_RT_PickLamps) he casts from each of them by its share, as
// the rays do (shaders/rtshadow.frag GridShadow), from a tile of his own seen from that panel; the grid has the rest.
//
// rows 47 + 4 t: the view and projection of tile t (VK_SM_FIRST_ROW), the tiles 256 pixels in rows of 8
// rows 308 + 4 i: the panels of player i, where each is (xyz), then their shares (xyz) and the share of the grid (w)

layout( set = 0, binding = 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
	vec4	rt[372];
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
#define LAMPS		308		// VK_RT_LAMP_ROW
#define LAMP_ROWS	4
#define LAMP_TILES	88		// VK_SM_LAMP_TILE( 0, 0 )
#define LAMP_PLAYERS	8	// VK_SM_LAMP_CASTERS
#define EXTENT		80.0	// VK_SM_EXTENT
#define DEPTH		640.0	// VK_SM_DEPTH

layout( location = 0 ) out vec4 o_color;

#include "scene.glsl"

#include "grid.glsl"


// how much of tile t lies in front of the place ndc of it (0 to 1, 3 x 3 pixels around it)
float TileDepthShadow( int t, vec3 ndc ) {
	if ( any( greaterThan( abs( ndc.xy ), vec2( 0.98 ) ) ) || ndc.z <= 0.0 || ndc.z >= 1.0 ) {
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

// how much of tile t lies in front of the point q
float TileShadow( int t, vec3 q ) {
	vec4 clip = mat4( W[47 + t * 4], W[48 + t * 4], W[49 + t * 4], W[50 + t * 4] ) * vec4( q, 1.0 );

	return clip.w <= 0.0 ? 0.0 : TileDepthShadow( t, clip.xyz / clip.w );
}

// the same for the tile t of a player around middle seen from a panel in the direction dir (vk_rt.cpp VK_SM_LampView,
// worked out the same way)
float LampShadow( int t, vec3 middle, vec3 dir, vec3 q ) {
	vec3 f = -dir;
	vec3 r = normalize( cross( f, abs( f.z ) > 0.99 ? vec3( 1.0, 0.0, 0.0 ) : vec3( 0.0, 0.0, 1.0 ) ) );
	vec3 u = cross( r, f );
	vec3 d = q - middle;

	return TileDepthShadow( t, vec3( dot( d, r ) / EXTENT, dot( d, u ) / EXTENT, ( dot( d, f ) + DEPTH ) * 0.5 / DEPTH ) );
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
		float ratio = directed / ( ambient + directed + 1.0 );
		float strength = min( ratio, 0.85 );
		vec4 shares = W[LAMPS + i * LAMP_ROWS + 3];
		float taken = 0.0;
		// the share of the grid (all of it without panels)
		if ( shares.w > 0.01 && dot( gridDir, gridDir ) > 1e-4 ) {
			float facing = dot( normal, normalize( gridDir ) );
			if ( facing > 0.0 ) {
				taken += strength * shares.w * TileShadow( i, q ) * smoothstep( 0.0, 0.25, facing );
			}
		}
		// the panels, each from its tile (the first LAMP_PLAYERS players have them)
		for ( int k = 0; k < 3 && i < LAMP_PLAYERS; k++ ) {
			float share = shares[k];
			if ( share <= 0.01 ) {
				continue;
			}
			vec3 lamp = W[LAMPS + i * LAMP_ROWS + k].xyz;
			vec3 to = lamp - middle;
			if ( dot( to, to ) < 1.0 ) {
				continue;
			}
			vec3 dir = normalize( to );
			float facing = dot( normal, dir );
			if ( facing <= 0.0 ) {
				continue;
			}
			// (a panel right by him gives a clear shadow even where the grid has the light mostly ambient, as rtshadow.frag)
			float near = 1.0 - smoothstep( 96.0, 192.0, length( to ) );
			taken += max( strength, 0.6 * near ) * share * LampShadow( LAMP_TILES + i * 3 + k, middle, dir, q ) * smoothstep( 0.0, 0.25, facing );
		}
		dark = max( dark, taken * fade );
	}
	return dark;
}

#include "rtmask.glsl"

void main() {
	vec2 pixel = FramePixel();
	vec3 p = ScenePoint( pixel );
	vec3 eye = W[4].xyz;
	float d = distance( p, eye );

	if ( d > 8192.0 ) {
		o_color = vec4( 0.0, PackDistance( 8192.0 ) );
		return;
	}
	if ( d < VIEW_WEAPON_RANGE ) {
		o_color = vec4( 0.0, PackDistance( d ) );
		return;
	}
	// (what the frames before had is added by shaders/rtaccum.frag)
	float mask = Shadow( p, SceneSurface( pixel, p, normalize( eye - p ) ) );
	o_color = vec4( mask, PackDistance( d ) );
}
