#version 460

// The shadows of the players from the moving lights on every device (vk_rt.cpp, the shadow maps; rays to them cost
// over a millisecond with a saber in the hand): the mask (red and green: how much the players take of light 0 and 1),
// from cubes of six tiles around each light (a blade has three along it, one in each third, at a new place every frame).
// The light passes of the next frame read it (generic.frag DynamicLight).
//
// rows 38 + 2k: the start of light k and its reach (0: none), 39 + 2k: its end; rows 47 + 4 t: the view and
// projection of tile t, the cubes of light k in the tiles 16 + 18 k on, six each (+x, -x, +y, -y, +z, -z)

layout( set = 0, binding = 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
	vec4	rt[271];
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

// how much of light k the players take at p: a blade from three points along it (as VK_SM_DrawAtlas has its cubes), blended, so the shadow is soft along the blade and sweeps as it turns
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
	if ( b.w <= 0.0 ) {
		return CubeShadow( 16 + k * 18, a.xyz, q );
	}
	// (where along the blade the three are this frame: rows[21] y, z, w, they jump every frame, VK_SM_DrawAtlas)
	float blocked = 0.0;
	for ( int i = 0; i < 3; i++ ) {
		blocked += CubeShadow( 16 + ( k * 3 + i ) * 6, mix( a.xyz, b.xyz, W[21][1 + i] ), q );
	}
	return blocked / 3.0;
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
