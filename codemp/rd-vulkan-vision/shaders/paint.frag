#version 460

// The sprayed pictures of RE_AddPaint (VK_DrawWet() in vk_post.cpp, the quads of paint.vert, the rows there): the
// level under the pixel from the depth of the scene, and where it lies on the wall of a picture, inside it and
// facing it, the picture is laid onto it as spray paint. Not a sticker: a decal projected from the depth as
// deferred decals are (Kim, "Screen Space Decals", the box test; Doom 2016 fades its decals where the surface
// turns away), with the level of the mip map chosen from the size of a pixel on the wall and not from the slopes of
// the pixel block, which break at the edges of the box (Wronski, "Fixing screen-space deferred decals", 2015).
//
// The paint takes the light of the level at the picture and the relief of the wall under it: the wall's brightness
// at the pixel against the brightness around it says where its pores, joints and grain are, and thin paint lets
// more of it through. The edge of the picture is overspray: grain on the edge and a mist of fine dots around it in
// the colour of the edge. While it is fresh the paint is darker and richer and has a wet sheen, it dries to matte,
// and on a wall heavy paint runs down in a few drips that grow until it dries. When it is sprayed it comes on from
// the top in passes of the can, with a ragged front.

#include "dialect.glsl"

#ifndef VK_GLES
#extension GL_EXT_nonuniform_qualifier : require
#endif

#include "noise.glsl"

BINDING( 1, 0 ) uniform sampler2D u_image;		// the copy of the frame
BINDING( 2, 0 ) uniform sampler2D u_depth;		// the depth of the scene, read pixel for pixel

// VK_PAINT_VEC4S of vk_local.h
#ifdef VK_GLES
uniform vec4 u_paint[459];
#define W	u_paint
uniform sampler2D u_picture;
#define PICTURE( index_ )	u_picture
#else
BINDING( 0, 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
} globals;
#define W	globals.paint
BINDING( 3, 0 ) uniform sampler2D u_textures[];
#define PICTURE( index_ )	u_textures[nonuniformEXT( index_ )]
#endif

VARYING( 0 ) flat in uint v_decal;
layout( location = 0 ) out vec4 o_color;

#include "scene.glsl"

#define DRIP_COLUMNS	36.0	// across the picture: where a drip can run
#define DRIP_SHARE		0.7		//	the columns above this of the hash have none
#define DRIP_LONGEST	0.62	// of half the side of the picture, at the end
#define WET_DARKEN		1.25	// fresh paint: its colour to this power, darker and richer

uint	g_texture;
vec4	g_frame;
float	g_cell;

// the picture at q (-1 to 1 across and up) at a level of its mip map; nothing outside it. Half a texel of that
// level in from the edge of the frame, so nothing of the frame next to it in the sheet comes in.
vec4 Picture( vec2 q, float lod ) {
	vec2 uv = vec2( q.x * 0.5 + 0.5, 0.5 - q.y * 0.5 );

	if ( uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0 ) {
		return vec4( 0.0 );
	}
	vec2 margin = g_frame.zw * ( 0.5 * exp2( lod ) / g_cell );
	return textureLod( PICTURE( g_texture ), g_frame.xy + clamp( uv * g_frame.zw, margin, g_frame.zw - margin ), lod );
}

float Luma( vec3 c ) {
	return dot( c, vec3( 0.299, 0.587, 0.114 ) );
}

void main() {
	uint row = 11u + v_decal * 7u;
	vec3 origin = W[row].xyz;
	float radius = max( W[row].w, 1.0 );
	vec3 n = W[row + 1u].xyz;
	float reveal = W[row + 1u].w;
	vec3 up = W[row + 2u].xyz;
	float fresh = W[row + 2u].w;
	vec3 ambient = W[row + 3u].rgb;
	vec3 directed = W[row + 4u].rgb;
	float seed = W[row + 4u].w;
	vec3 l = W[row + 5u].xyz;
	vec3 right = cross( up, n );
	vec3 eye = W[4].xyz;

	g_texture = uint( W[row + 3u].w + 0.5 );
	g_cell = max( W[row + 5u].w, 4.0 );
	g_frame = W[row + 6u];

	// on the wall of the picture, facing as it does, and inside it or the drips below it
	vec3 p = ScenePoint( gl_FragCoord.xy );
	vec3 rel = p - origin;
	if ( abs( dot( rel, n ) ) > 2.0 + radius * 0.08 ) {
		discard;
	}
	vec3 surface = SceneSurface( gl_FragCoord.xy, p, n );
	float facing = dot( surface, n );
	if ( facing < 0.35 ) {
		discard;
	}
	vec2 q = vec2( dot( rel, right ), dot( rel, up ) ) / radius;
	if ( abs( q.x ) > 1.1 || q.y > 1.1 || q.y < -1.0 - DRIP_LONGEST - 0.05 ) {
		discard;
	}

	// the level of the mip map from the size of the pixel on the wall against the size of a texel there
	vec3 view = normalize( p - eye );
	float pixelOnWall = SceneDistance( gl_FragCoord.xy ) * 2.0 * W[4].w / W[8].z / max( abs( dot( view, n ) ), 0.25 );
	float texelOnWall = 2.0 * radius / g_cell;
	float lodMost = log2( g_cell ) - 2.0;
	float lod = clamp( log2( max( pixelOnWall / texelOnWall, 1e-4 ) ), 0.0, lodMost );
	vec2 su = q * radius;			// units on the wall, for the noise

	vec4 c = Picture( q, lod );
	// the halo of the can: the picture soft, its alpha as how much mist came there
	vec4 soft = Picture( q, min( lod + 3.0, lodMost ) );

	// the edge: grain where the paint thins out, and dots of mist around the picture where they are not smaller
	// than a pixel
	float grain = Noise( su * 2.3 + seed * 97.0 );
	float body = clamp( ( c.a - 0.5 ) * 2.6 + 0.5 + ( grain - 0.5 ) * 0.9 * ( 1.0 - c.a * c.a ), 0.0, 1.0 );
	vec2 dotAt = su * 3.2;
	vec2 dotCell = floor( dotAt );
	uvec3 dh = Pcg3( uvec3( ivec3( ivec2( dotCell ) + 8192, int( seed * 1000.0 ) ) ) );
	vec3 dr = vec3( dh & 0xffffu ) / 65535.0;
	float dotSize = 0.12 + 0.18 * dr.z;
	float speck = 1.0 - smoothstep( dotSize * 0.6, dotSize, length( dotAt - dotCell - 0.2 - 0.6 * dr.xy ) );
	float mistDensity = clamp( soft.a * 1.6 - c.a, 0.0, 1.0 );
	float mist = speck * step( 1.0 - mistDensity * 0.55, dr.x * 0.5 + dr.y * 0.5 ) * ( 1.0 - smoothstep( 0.3, 0.8, pixelOnWall * 3.2 ) );
	vec3 albedo = c.rgb;
	float cover = body;
	if ( mist > cover ) {
		albedo = soft.rgb;
		cover = mist * 0.85;
	}

	// drips on a wall: in a few columns the paint runs down from the lowest heavy part above, a thin line that
	// tapers and ends in a round bead, growing until the paint dries. The edge of the paint above the pixel is looked
	// for in steps up the column and then halved, so the line has no gaps; it is never thinner than a pixel.
	float drip = 0.0;
	if ( abs( n.z ) < 0.6 && fresh > 0.0 ) {
		float column = floor( ( q.x * 0.5 + 0.5 ) * DRIP_COLUMNS );
		uvec3 h = Pcg3( uvec3( ivec3( int( column ) + 512, int( seed * 1000.0 ), 23 ) ) );
		vec3 r = vec3( h & 0xffffu ) / 65535.0;
		float x = ( column + 0.35 + 0.3 * r.z ) / DRIP_COLUMNS * 2.0 - 1.0;
		float run = DRIP_LONGEST * ( 0.25 + 0.75 * r.y ) * smoothstep( 1.0, 0.55, fresh );
		float width = max( 0.8 / DRIP_COLUMNS * ( 0.6 + 0.4 * r.z ), 0.7 * pixelOnWall / radius );
		if ( r.x > DRIP_SHARE && run > width && abs( q.x - x ) < width * 1.6 && Picture( vec2( x, q.y ), lod ).a <= 0.6 ) {
			float lo = 0.0, hi = -1.0;
			for ( int k = 1; k <= 8; k++ ) {
				float t = run * float( k ) / 8.0;
				if ( Picture( vec2( x, q.y + t ), lod ).a > 0.6 ) {
					hi = t;
					break;
				}
				lo = t;
			}
			if ( hi > 0.0 ) {
				for ( int k = 0; k < 4; k++ ) {
					float m = ( lo + hi ) * 0.5;
					if ( Picture( vec2( x, q.y + m ), lod ).a > 0.6 ) {
						hi = m;
					} else {
						lo = m;
					}
				}
				// hi: how far below the paint the pixel is; the line thins, the bead at its end is a circle
				float along = hi / run;
				float w = width * ( 1.0 - 0.45 * along );
				float line = 1.0 - smoothstep( w * 0.55, w, abs( q.x - x ) );
				float bead = width * 1.15;
				float ball = 1.0 - smoothstep( bead * 0.7, bead, length( vec2( q.x - x, hi - ( run - bead ) ) ) );
				drip = hi < run - bead ? line : ball;
				albedo = mix( albedo, Picture( vec2( x, q.y + hi + 0.02 ), lod ).rgb, drip );
			}
		}
	}
	cover = max( cover, drip );
	if ( abs( q.x ) > 1.0 || q.y > 1.0 ) {
		cover = mist * 0.85;
	}

	// the passes of the can while it is sprayed, from the top with a ragged front
	float front = ( 0.5 - q.y * 0.5 ) * 0.7 + Noise( su * 0.35 + seed * 13.0 ) * 0.3;
	cover *= smoothstep( front, front + 0.12, reveal * 1.15 );
	cover *= smoothstep( 0.35, 0.7, facing );
	if ( cover <= 0.003 ) {
		discard;
	}

	// the wall under it: its brightness here against the brightness around it is its relief, and thin paint
	// lets more of it through
	vec2 here = gl_FragCoord.xy * W[9].xy;
	vec3 base = texture( u_image, here ).rgb;
	float around = 0.0;
	for ( int i = 0; i < 8; i++ ) {
		float a = float( i ) * 0.785 + seed * 6.28;
		around += Luma( texture( u_image, ( gl_FragCoord.xy + vec2( cos( a ), sin( a ) ) * ( ( i & 1 ) == 0 ? 3.0 : 7.0 ) ) * W[9].xy ).rgb );
	}
	float relief = clamp( Luma( base ) / max( around / 8.0, 0.02 ), 0.5, 1.7 );
	relief = mix( relief, 1.0, 0.2 + 0.3 * c.a );

	// lit as the wall is, darker and richer while wet
	albedo = mix( albedo, pow( max( albedo, vec3( 0.0 ) ), vec3( WET_DARKEN ) ), fresh * 0.8 );
	float lit = max( dot( surface, l ), 0.0 );
	vec3 color = albedo * min( ambient + directed * lit, vec3( 1.2 ) ) * relief;

	// the sheen of wet paint: a smooth film, Fresnel with F0 0.04, going matte as it dries
	vec3 half_ = normalize( l - view );
	float fresnel = 0.04 + 0.96 * pow( 1.0 - clamp( dot( -view, surface ), 0.0, 1.0 ), 5.0 );
	float gloss = mix( 24.0, 160.0, fresh );
	color += directed * pow( clamp( dot( surface, half_ ), 0.0, 1.0 ), gloss ) * fresh * ( 0.15 + 0.85 * fresnel ) * 1.6;
	color += ambient * fresnel * fresh * 0.25;

	o_color = vec4( color, clamp( cover, 0.0, 1.0 ) );
}
