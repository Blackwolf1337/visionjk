/*
===========================================================================
Copyright (C) 2013 - 2015, OpenJK contributors
Copyright (C) 2026, VisionJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
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

// cm_export.cpp -- the solid surfaces of the level as triangles, for code that needs the level as a mesh and
// not as something to trace through: the sound of client/spatial/snd_spatial.cpp hands it to a ray tracer of its own,
// which runs in other threads, where CM_BoxTrace() must not be called (it marks the brushes it has tested).
//
// Only the world. The brush models that move (doors, lifts) are left out.

#include "cm_local.h"
#include "cm_patch.h"

#define EXPORT_EPSILON		0.1f
#define EXPORT_MIN_AREA		0.01f	// twice the area squared of a triangle that is still handed on

// a polygon as a fan of triangles, counter clockwise when seen from where the normal points
static int CM_ExportWinding( const winding_t *w, const vec3_t normal, const CCMShader *shader, cmTriangleFunc_t emit, void *data ) {
	int count = 0;

	for ( int i = 1; i + 1 < w->numpoints; i++ ) {
		vec3_t d1, d2, cross;

		VectorSubtract( w->p[i], w->p[0], d1 );
		VectorSubtract( w->p[i + 1], w->p[0], d2 );
		CrossProduct( d1, d2, cross );
		if ( VectorLengthSquared( cross ) < EXPORT_MIN_AREA ) {
			continue;
		}
		if ( emit ) {
			if ( DotProduct( cross, normal ) >= 0.0f ) {
				emit( w->p[0], w->p[i], w->p[i + 1], shader->shader, shader->surfaceFlags, data );
			} else {
				emit( w->p[0], w->p[i + 1], w->p[i], shader->shader, shader->surfaceFlags, data );
			}
		}
		count++;
	}
	return count;
}

// Every side of a brush is its plane cut down by the planes of the other sides. The bevel planes a brush has
// for box traces touch it at an edge or a corner only: nothing of them is left after the cuts.
static winding_t *CM_ExportSide( const cbrush_t *b, int side ) {
	const cplane_t *plane = b->sides[side].plane;
	vec3_t normal;
	winding_t *w;

	VectorCopy( plane->normal, normal );
	w = BaseWindingForPlane( normal, plane->dist );
	for ( int j = 0; j < b->numsides && w; j++ ) {
		vec3_t other;

		if ( j == side || b->sides[j].plane == plane ) {
			continue;
		}
		// what is behind the other side stays
		VectorNegate( b->sides[j].plane->normal, other );
		ChopWindingInPlace( &w, other, -b->sides[j].plane->dist, EXPORT_EPSILON );
	}
	return w;
}

// a side nothing is drawn on and that says nothing of its own of what it is made of: caulk, a clip brush
static qboolean CM_ExportHidden( const CCMShader *shader ) {
	return (qboolean)( ( shader->surfaceFlags & SURF_NODRAW ) && !( shader->surfaceFlags & ( SURF_SKY | MATERIAL_MASK ) ) );
}

// the shader of the first side of a brush that is seen, NULL if it has none (a clip brush)
static const CCMShader *CM_ExportDrawnShader( const cbrush_t *b ) {
	for ( int i = 0; i < b->numsides; i++ ) {
		const CCMShader *shader = &cmg.shaders[b->sides[i].shaderNum];
		winding_t *w;
		int left;

		if ( CM_ExportHidden( shader ) ) {
			continue;
		}
		// a bevel plane has a shader that is not hidden, and nothing is left of it
		w = CM_ExportSide( b, i );
		if ( !w ) {
			continue;
		}
		left = CM_ExportWinding( w, b->sides[i].plane->normal, shader, NULL, NULL );
		FreeWinding( w );
		if ( left ) {
			return shader;
		}
	}
	return NULL;
}

// A wall is a brush with a texture on the side that is seen and caulk on the others. For who asks what a wall
// is made of it is one thing: a side that is not drawn is handed on with the shader of the first side of its
// brush that is. In a brush with no such side every side keeps its own.
static int CM_ExportBrush( const cbrush_t *b, cmTriangleFunc_t emit, void *data ) {
	const CCMShader *drawn = NULL;
	qboolean looked = qfalse;
	int count = 0;

	for ( int i = 0; i < b->numsides; i++ ) {
		const CCMShader *shader = &cmg.shaders[b->sides[i].shaderNum];
		winding_t *w = CM_ExportSide( b, i );

		if ( !w ) {
			continue;
		}
		if ( CM_ExportHidden( shader ) ) {
			if ( !looked ) {
				drawn = CM_ExportDrawnShader( b );
				looked = qtrue;
			}
			if ( drawn ) {
				shader = drawn;
			}
		}
		count += CM_ExportWinding( w, b->sides[i].plane->normal, shader, emit, data );
		FreeWinding( w );
	}
	return count;
}

// A curve is facets for the collision: a plane each, and border planes around it (see CM_DrawDebugSurface)
static int CM_ExportPatch( const cPatch_t *patch, cmTriangleFunc_t emit, void *data ) {
	const patchCollide_t *pc = patch->pc;
	int count = 0;

	for ( int i = 0; i < pc->numFacets; i++ ) {
		const facet_t *facet = &pc->facets[i];
		vec3_t normal;
		winding_t *w;

		if ( facet->surfacePlane < 0 ) {
			continue;
		}
		VectorCopy( pc->planes[facet->surfacePlane].plane, normal );
		w = BaseWindingForPlane( normal, pc->planes[facet->surfacePlane].plane[3] );
		for ( int j = 0; j < facet->numBorders && w; j++ ) {
			float border[4];

			if ( facet->borderPlanes[j] == facet->surfacePlane ) {
				continue;
			}
			VectorCopy4( pc->planes[facet->borderPlanes[j]].plane, border );
			if ( !facet->borderInward[j] ) {
				VectorNegate( border, border );
				border[3] = -border[3];
			}
			ChopWindingInPlace( &w, border, border[3], EXPORT_EPSILON );
		}
		if ( w ) {
			count += CM_ExportWinding( w, normal, &cmg.shaders[patch->shaderNum], emit, data );
			FreeWinding( w );
		}
	}
	return count;
}

/*
===============
CM_ExportSolidTriangles

Calls emit for every triangle of the solid brushes and curves of the world, with the name and the surface flags
of its shader, and returns how many there are. Without emit it only counts. 0 if no level is loaded.
===============
*/
int CM_ExportSolidTriangles( cmTriangleFunc_t emit, void *data ) {
	byte *moving;
	int count = 0;

	if ( cmg.numBrushes <= 0 || cmg.numSubModels <= 0 ) {
		return 0;
	}

	// what belongs to a brush model other than the world
	moving = (byte *)Z_Malloc( cmg.numBrushes + cmg.numSurfaces + 1, TAG_TEMP_WORKSPACE, qtrue );
	for ( int i = 1; i < cmg.numSubModels; i++ ) {
		const cLeaf_t *leaf = &cmg.cmodels[i].leaf;

		for ( int k = 0; k < leaf->numLeafBrushes; k++ ) {
			const int n = cmg.leafbrushes[leaf->firstLeafBrush + k];

			if ( n >= 0 && n < cmg.numBrushes ) {
				moving[n] = 1;
			}
		}
		for ( int k = 0; k < leaf->numLeafSurfaces; k++ ) {
			const int n = cmg.leafsurfaces[leaf->firstLeafSurface + k];

			if ( n >= 0 && n < cmg.numSurfaces ) {
				moving[cmg.numBrushes + n] = 1;
			}
		}
	}

	for ( int i = 0; i < cmg.numBrushes; i++ ) {
		if ( !moving[i] && ( cmg.brushes[i].contents & CONTENTS_SOLID ) ) {
			count += CM_ExportBrush( &cmg.brushes[i], emit, data );
		}
	}
	for ( int i = 0; i < cmg.numSurfaces; i++ ) {
		const cPatch_t *patch = cmg.surfaces[i];

		if ( patch && patch->pc && !moving[cmg.numBrushes + i] && ( patch->contents & CONTENTS_SOLID ) ) {
			count += CM_ExportPatch( patch, emit, data );
		}
	}
	Z_Free( moving );
	return count;
}

/*
===============
CM_ExportModelTriangles

The same for a brush model: 0 is the world without the brush models (CM_ExportSolidTriangles), 1 and up the
solid brushes and curves of that brush model (a door, a lift, a func_static), where the map has them: an entity
places them with its origin and angles, as CM_TransformedBoxTrace() does. Returns how many there are.
===============
*/
int CM_ExportModelTriangles( int model, cmTriangleFunc_t emit, void *data ) {
	const cLeaf_t *leaf;
	int count = 0;

	if ( !model ) {
		return CM_ExportSolidTriangles( emit, data );
	}
	if ( model < 0 || model >= cmg.numSubModels ) {
		return 0;
	}
	leaf = &cmg.cmodels[model].leaf;
	for ( int k = 0; k < leaf->numLeafBrushes; k++ ) {
		const int n = cmg.leafbrushes[leaf->firstLeafBrush + k];

		if ( n >= 0 && n < cmg.numBrushes && ( cmg.brushes[n].contents & CONTENTS_SOLID ) ) {
			count += CM_ExportBrush( &cmg.brushes[n], emit, data );
		}
	}
	for ( int k = 0; k < leaf->numLeafSurfaces; k++ ) {
		const int n = cmg.leafsurfaces[leaf->firstLeafSurface + k];
		const cPatch_t *patch = ( n >= 0 && n < cmg.numSurfaces ) ? cmg.surfaces[n] : NULL;

		if ( patch && patch->pc && ( patch->contents & CONTENTS_SOLID ) ) {
			count += CM_ExportPatch( patch, emit, data );
		}
	}
	return count;
}

// how many brush models the level has, the world (0) with them
int CM_ExportModelCount( void ) {
	return cmg.numSubModels;
}
