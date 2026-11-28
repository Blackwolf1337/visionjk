/*
===========================================================================
Copyright (C) 2026, VisionJK contributors

This file is part of the VisionJK source code.

VisionJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// cg_ribbon.c -- lines of light as ribbons that face the eye (issue #19): each layer a strip of quads whose pieces share
// their edges, so a line bends without gaps; gathered by shader and handed to the renderer as polygons in few calls.
// A piece seen end on is dimmed (it lies over itself as a white blob), and no layer gets thinner than a pixel or two
// (it would crawl) nor brighter for being widened.

#include "cgame/cg_local.h"
#include "cgame/force/cg_ribbon.h"

#define RIBBON_SHADERS	4
#define RIBBON_QUADS	1024	// pieces of one shader gathered before they go to the renderer

static struct {
	qhandle_t	shader;
	int			numQuads;
	polyVert_t	verts[RIBBON_QUADS * 4];
} batches[RIBBON_SHADERS];

static void Ribbon_FlushBatch( int b ) {
	if ( batches[b].numQuads ) {
		trap->R_AddPolysToScene( batches[b].shader, 4, batches[b].verts, batches[b].numQuads );
		batches[b].numQuads = 0;
	}
}

void CG_RibbonFlush( void ) {
	int b;

	for ( b = 0; b < RIBBON_SHADERS; b++ ) {
		Ribbon_FlushBatch( b );
	}
}

// room for one more piece of shader
static polyVert_t *Ribbon_Quad( qhandle_t shader ) {
	int b, free = -1;

	for ( b = 0; b < RIBBON_SHADERS; b++ ) {
		if ( batches[b].shader == shader ) {
			break;
		}
		if ( free < 0 && !batches[b].numQuads ) {
			free = b;
		}
	}
	if ( b == RIBBON_SHADERS ) {
		if ( free < 0 ) {
			CG_RibbonFlush();
			free = 0;
		}
		b = free;
		batches[b].shader = shader;
	}
	if ( batches[b].numQuads >= RIBBON_QUADS ) {
		Ribbon_FlushBatch( b );
	}
	return &batches[b].verts[batches[b].numQuads++ * 4];
}

static void Ribbon_Corner( polyVert_t *v, const vec3_t at, const vec3_t side, float half, float s, float t, const vec3_t rgb ) {
	int c;

	VectorMA( at, half, side, v->xyz );
	v->st[0] = s;
	v->st[1] = t;
	for ( c = 0; c < 3; c++ ) {
		v->modulate[c] = (byte)( Com_Clamp( 0.0f, 1.0f, rgb[c] ) * 255.0f );
	}
	v->modulate[3] = 255;
}

void CG_RibbonDraw( const ribbon_t *r, const ribbonLayer_t *layers, int numLayers ) {
	const float	pixelScale = 2.0f * tanf( DEG2RAD( cg.refdef.fov_x ) * 0.5f ) / Q_max( cg.refdef.width, 1 );
	vec3_t		sides[RIBBON_POINTS], rgb[RIBBON_POINTS];
	float		dist[RIBBON_POINTS], facing[RIBBON_POINTS], half[RIBBON_POINTS];
	const int	n = Q_min( r->numPoints, RIBBON_POINTS );
	int			i, l;

	if ( n < 2 ) {
		return;
	}
	for ( i = 0; i < n; i++ ) {
		vec3_t along, eye;

		VectorSubtract( r->p[Q_min( i + 1, n - 1 )], r->p[Q_max( i - 1, 0 )], along );
		VectorSubtract( r->p[i], cg.refdef.vieworg, eye );
		dist[i] = VectorLength( eye );
		CrossProduct( along, eye, sides[i] );
		facing[i] = VectorLength( sides[i] ) / Q_max( VectorLength( along ) * dist[i], 1e-6f );
		facing[i] = 0.2f + 0.8f * Com_Clamp( 0.0f, 1.0f, facing[i] * 2.5f );
		if ( VectorNormalize( sides[i] ) < 1e-6f ) {
			VectorSet( sides[i], 0.0f, 0.0f, 1.0f );
		}
	}
	for ( l = 0; l < numLayers; l++ ) {
		const ribbonLayer_t *layer = &layers[l];

		if ( !layer->shader || layer->level <= 0.0f ) {
			continue;
		}
		for ( i = 0; i < n; i++ ) {
			const float want = layer->halfWidth * r->width[i];
			const float least = layer->minPixels * pixelScale * dist[i];
			float b = layer->level * r->bright[i] * facing[i];

			if ( want < least ) {
				b *= sqrtf( Q_max( want, 0.0f ) / least );
				half[i] = least;
			} else {
				half[i] = want;
			}
			VectorScale( layer->color, b, rgb[i] );
		}
		for ( i = 0; i < n - 1; i++ ) {
			polyVert_t *v = Ribbon_Quad( layer->shader );

			Ribbon_Corner( &v[0], r->p[i], sides[i], -half[i], 0.0f, 0.0f, rgb[i] );
			Ribbon_Corner( &v[1], r->p[i], sides[i], half[i], 1.0f, 0.0f, rgb[i] );
			Ribbon_Corner( &v[2], r->p[i + 1], sides[i + 1], half[i + 1], 1.0f, 1.0f, rgb[i + 1] );
			Ribbon_Corner( &v[3], r->p[i + 1], sides[i + 1], -half[i + 1], 0.0f, 1.0f, rgb[i + 1] );
		}
	}
}

void CG_RibbonSprite( qhandle_t shader, const vec3_t at, float radius, const vec3_t color, float level ) {
	refEntity_t	re;
	int			i;

	if ( !shader || level <= 0.0f ) {
		return;
	}
	memset( &re, 0, sizeof( re ) );
	re.reType = RT_SPRITE;
	VectorCopy( at, re.origin );
	re.radius = radius;
	re.customShader = shader;
	for ( i = 0; i < 3; i++ ) {
		re.shaderRGBA[i] = (byte)Com_Clamp( 0.0f, 255.0f, color[i] * level * 255.0f );
	}
	re.shaderRGBA[3] = 255;
	re.renderfx = RF_NOSHADOW;
	trap->R_AddRefEntityToScene( &re );
}

void CG_RibbonBasis( const vec3_t fwd, vec3_t right, vec3_t up ) {
	const vec3_t z = { 0.0f, 0.0f, 1.0f };

	CrossProduct( fwd, z, right );
	if ( VectorNormalize( right ) < 0.01f ) {
		const vec3_t x = { 1.0f, 0.0f, 0.0f };

		CrossProduct( fwd, x, right );
		VectorNormalize( right );
	}
	CrossProduct( right, fwd, up );
}
