/*
===========================================================================
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

// tr_material.cpp: what the material debug menu of the client asks and changes (refexport_t ext.Material*): the
// shader under the crosshair, the shaders of the level by area, and the material of a stage, which all shaders of
// that name share (stageMaterial_t, R_MaterialFor), so a change shows the same frame.

#include "tr_local.h"

#define PICK_RANGE		16384.0f
#define MAX_VARIANTS	64			// lightmap variants of one shader name looked at

// the view of the level of the last frame, the ray of a pick from its middle
static vec3_t			levelViewOrigin;
static vec3_t			levelViewAxis[3];
static const world_t	*levelViewWorld;

// square units of the surfaces of the level per shader, and the shaders of the level and all shaders as one handle
// per name, made again when the level or the number of shaders changed
static float			shaderArea[MAX_SHADERS];
static float			nameArea[MAX_SHADERS];
static qhandle_t		levelList[MAX_SHADERS];
static qhandle_t		allList[MAX_SHADERS];
static int				levelCount, allCount;
static const world_t	*listWorld;
static int				listShaders = -1;

void R_MaterialLevelView( const vec3_t origin, const vec3_t axis[3] )
{
	VectorCopy( origin, levelViewOrigin );
	VectorCopy( axis[0], levelViewAxis[0] );
	VectorCopy( axis[1], levelViewAxis[1] );
	VectorCopy( axis[2], levelViewAxis[2] );
	levelViewWorld = tr.world;
}

/*
===============
The triangles of a surface of the level, to func until it answers qfalse
===============
*/
typedef qboolean ( *triangleFunc_t )( const float *a, const float *b, const float *c, void *context );

static void SurfaceTriangles( const surfaceType_t *data, triangleFunc_t func, void *context )
{
	switch ( *data ) {
	case SF_FACE: {
		const srfSurfaceFace_t *face = (const srfSurfaceFace_t *)data;
		const int *indices = (const int *)( (const byte *)face + face->ofsIndices );

		for ( int i = 0; i + 2 < face->numIndices; i += 3 ) {
			if ( !func( face->points[indices[i]], face->points[indices[i + 1]], face->points[indices[i + 2]], context ) ) {
				return;
			}
		}
		break;
	}
	case SF_GRID: {
		const srfGridMesh_t *grid = (const srfGridMesh_t *)data;

		for ( int j = 0; j < grid->height - 1; j++ ) {
			for ( int i = 0; i < grid->width - 1; i++ ) {
				const drawVert_t *v = &grid->verts[j * grid->width + i];

				if ( !func( v[0].xyz, v[1].xyz, v[grid->width].xyz, context )
					|| !func( v[1].xyz, v[grid->width + 1].xyz, v[grid->width].xyz, context ) ) {
					return;
				}
			}
		}
		break;
	}
	case SF_TRIANGLES: {
		const srfTriangles_t *tris = (const srfTriangles_t *)data;

		for ( int i = 0; i + 2 < tris->numIndexes; i += 3 ) {
			if ( !func( tris->verts[tris->indexes[i]].xyz, tris->verts[tris->indexes[i + 1]].xyz,
				tris->verts[tris->indexes[i + 2]].xyz, context ) ) {
				return;
			}
		}
		break;
	}
	default:
		break;
	}
}

/*
===============
Areas and the lists of shaders
===============
*/
static qboolean AddTriangleArea( const float *a, const float *b, const float *c, void *context )
{
	vec3_t e1, e2, n;

	VectorSubtract( b, a, e1 );
	VectorSubtract( c, a, e2 );
	CrossProduct( e1, e2, n );
	*(float *)context += 0.5f * VectorLength( n );
	return qtrue;
}

// the variant of a shader name that stands for the name in the lists: the one with the lowest index
static qboolean FirstOfName( const shader_t *sh )
{
	shader_t *variants[MAX_VARIANTS];
	const int count = R_ShadersNamed( sh->name, variants, MAX_VARIANTS );

	for ( int i = 0; i < count; i++ ) {
		if ( variants[i]->index < sh->index ) {
			return qfalse;
		}
	}
	return qtrue;
}

static float AreaOfName( const char *name )
{
	shader_t *variants[MAX_VARIANTS];
	const int count = R_ShadersNamed( name, variants, MAX_VARIANTS );
	float area = 0.0f;

	for ( int i = 0; i < count; i++ ) {
		area += shaderArea[variants[i]->index];
	}
	return area;
}

static int QDECL CompareArea( const void *a, const void *b )
{
	const float da = nameArea[*(const qhandle_t *)a], db = nameArea[*(const qhandle_t *)b];

	if ( da != db ) {
		return da > db ? -1 : 1;
	}
	return *(const qhandle_t *)a - *(const qhandle_t *)b;
}

static void MakeLists( void )
{
	if ( listWorld == tr.world && listShaders == tr.numShaders ) {
		return;
	}
	listWorld = tr.world;
	listShaders = tr.numShaders;

	memset( shaderArea, 0, sizeof( shaderArea ) );
	if ( tr.world ) {
		for ( int i = 0; i < tr.world->numsurfaces; i++ ) {
			const msurface_t *surf = &tr.world->surfaces[i];

			if ( surf->shader ) {
				SurfaceTriangles( surf->data, AddTriangleArea, &shaderArea[surf->shader->index] );
			}
		}
	}

	levelCount = allCount = 0;
	// 0 is the default shader, which a handle of 0 means none
	for ( int i = 1; i < tr.numShaders; i++ ) {
		const shader_t *sh = tr.shaders[i];

		if ( !sh || !FirstOfName( sh ) ) {
			continue;
		}
		nameArea[i] = AreaOfName( sh->name );
		allList[allCount++] = i;
		if ( nameArea[i] > 0.0f ) {
			levelList[levelCount++] = i;
		}
	}
	qsort( levelList, levelCount, sizeof( levelList[0] ), CompareArea );
}

int RE_MaterialList( qboolean all, int first, qhandle_t *out, int max )
{
	MakeLists();

	const qhandle_t *list = all ? allList : levelList;
	const int count = all ? allCount : levelCount;

	for ( int i = 0; i < max && first + i < count; i++ ) {
		out[i] = list[first + i];
	}
	return count;
}

qhandle_t RE_MaterialFind( const char *name )
{
	char stripped[MAX_QPATH];
	shader_t *variants[MAX_VARIANTS];

	COM_StripExtension( name, stripped, sizeof( stripped ) );

	const int count = R_ShadersNamed( stripped, variants, MAX_VARIANTS );
	qhandle_t best = 0;

	for ( int i = 0; i < count; i++ ) {
		if ( !best || variants[i]->index < best ) {
			best = variants[i]->index;
		}
	}
	return best;
}

/*
===============
The pick
===============
*/
typedef struct {
	vec3_t		start, dir;			// dir is the whole ray
	float		fraction;			// of the nearest hit so far
	vec3_t		tri[3];				// its triangle
	qboolean	hit;
} pickRay_t;

// Moeller and Trumbore, from both sides
static qboolean PickTriangle( const float *a, const float *b, const float *c, void *context )
{
	pickRay_t *ray = (pickRay_t *)context;
	vec3_t e1, e2, p, s, q;

	VectorSubtract( b, a, e1 );
	VectorSubtract( c, a, e2 );
	CrossProduct( ray->dir, e2, p );

	const float det = DotProduct( e1, p );
	if ( fabsf( det ) < 1e-8f ) {
		return qtrue;
	}
	const float inv = 1.0f / det;

	VectorSubtract( ray->start, a, s );
	const float u = DotProduct( s, p ) * inv;
	if ( u < 0.0f || u > 1.0f ) {
		return qtrue;
	}
	CrossProduct( s, e1, q );
	const float v = DotProduct( ray->dir, q ) * inv;
	if ( v < 0.0f || u + v > 1.0f ) {
		return qtrue;
	}
	const float t = DotProduct( e2, q ) * inv;
	if ( t >= 0.0f && t < ray->fraction ) {
		ray->fraction = t;
		VectorCopy( a, ray->tri[0] );
		VectorCopy( b, ray->tri[1] );
		VectorCopy( c, ray->tri[2] );
		ray->hit = qtrue;
	}
	return qtrue;
}

// qfalse when the ray can not reach the box before the nearest hit so far
static qboolean PickBox( const pickRay_t *ray, const vec3_t mins, const vec3_t maxs )
{
	float t0 = 0.0f, t1 = ray->fraction;

	for ( int i = 0; i < 3; i++ ) {
		if ( fabsf( ray->dir[i] ) < 1e-8f ) {
			if ( ray->start[i] < mins[i] || ray->start[i] > maxs[i] ) {
				return qfalse;
			}
			continue;
		}
		float a = ( mins[i] - ray->start[i] ) / ray->dir[i];
		float b = ( maxs[i] - ray->start[i] ) / ray->dir[i];

		if ( a > b ) {
			const float t = a;
			a = b;
			b = t;
		}
		t0 = ( a > t0 ) ? a : t0;
		t1 = ( b < t1 ) ? b : t1;
		if ( t0 > t1 ) {
			return qfalse;
		}
	}
	return qtrue;
}

// the surfaces a pick stops at: what is drawn, the sky too, not fog or nodraw
static qboolean Pickable( const shader_t *sh )
{
	if ( !sh || ( sh->contentFlags & CONTENTS_FOG ) || ( sh->surfaceFlags & SURF_NODRAW ) ) {
		return qfalse;
	}
	return (qboolean)( sh->numUnfoggedPasses > 0 || sh->sky );
}

qboolean RE_MaterialPick( const vec3_t start, const vec3_t end, materialPick_t *out )
{
	pickRay_t ray;
	const msurface_t *best = NULL;

	memset( out, 0, sizeof( *out ) );
	if ( !tr.world ) {
		return qfalse;
	}
	memset( &ray, 0, sizeof( ray ) );
	if ( start ) {
		VectorCopy( start, ray.start );
		VectorSubtract( end, start, ray.dir );
	} else {
		if ( levelViewWorld != tr.world ) {
			return qfalse;
		}
		VectorCopy( levelViewOrigin, ray.start );
		VectorScale( levelViewAxis[0], PICK_RANGE, ray.dir );
	}
	ray.fraction = 1.0f;

	for ( int i = 0; i < tr.world->numsurfaces; i++ ) {
		const msurface_t *surf = &tr.world->surfaces[i];

		if ( !Pickable( surf->shader ) ) {
			continue;
		}
		if ( *surf->data == SF_FACE ) {
			// both ends on one side of the plane of the face
			const cplane_t *plane = &( (const srfSurfaceFace_t *)surf->data )->plane;
			const float d0 = DotProduct( ray.start, plane->normal ) - plane->dist;
			const float d1 = d0 + DotProduct( ray.dir, plane->normal ) * ray.fraction;

			if ( ( d0 > 0.0f && d1 > 0.0f ) || ( d0 < 0.0f && d1 < 0.0f ) ) {
				continue;
			}
		} else if ( *surf->data == SF_GRID ) {
			const srfGridMesh_t *grid = (const srfGridMesh_t *)surf->data;

			if ( !PickBox( &ray, grid->meshBounds[0], grid->meshBounds[1] ) ) {
				continue;
			}
		} else if ( *surf->data == SF_TRIANGLES ) {
			const srfTriangles_t *tris = (const srfTriangles_t *)surf->data;

			if ( !PickBox( &ray, tris->bounds[0], tris->bounds[1] ) ) {
				continue;
			}
		} else {
			continue;
		}
		const qboolean before = ray.hit;
		const float fraction = ray.fraction;

		SurfaceTriangles( surf->data, PickTriangle, &ray );
		if ( ray.hit && ( !before || ray.fraction < fraction ) ) {
			best = surf;
		}
	}
	if ( !best ) {
		return qfalse;
	}

	vec3_t e1, e2;

	VectorSubtract( ray.tri[1], ray.tri[0], e1 );
	VectorSubtract( ray.tri[2], ray.tri[0], e2 );
	CrossProduct( e1, e2, out->normal );
	VectorNormalize( out->normal );
	if ( DotProduct( out->normal, ray.dir ) > 0.0f ) {
		VectorInverse( out->normal );
	}
	out->shader = best->shader->index;
	VectorMA( ray.start, ray.fraction, ray.dir, out->point );
	out->distance = ray.fraction * VectorLength( ray.dir );
	return qtrue;
}

/*
===============
Shaders and their materials
===============
*/
static shaderStage_t *FindStage( shader_t *sh, int sourceStage )
{
	for ( int i = 0; i < sh->numUnfoggedPasses; i++ ) {
		if ( sh->stages[i].active && sh->stages[i].sourceStage == sourceStage ) {
			return &sh->stages[i];
		}
	}
	return NULL;
}

static int StageFlags( const shaderStage_t *st )
{
	const uint32_t dst = st->stateBits & GLS_DSTBLEND_BITS;
	int flags = 0;

	if ( st->bundle[0].isLightmap || ( st->bundle[1].image && st->bundle[1].isLightmap ) ) {
		flags |= MATS_LIGHTMAP;
	}
	if ( dst == GLS_DSTBLEND_ONE || dst == GLS_DSTBLEND_ONE_MINUS_SRC_COLOR ) {
		flags |= MATS_ADD;
	} else if ( st->stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) {
		flags |= MATS_BLEND;
	}
	if ( st->glow ) {
		flags |= MATS_GLOW;
	}
	if ( st->mat ) {
		flags |= MATS_MATERIAL;
	}
	if ( st->rgbGen == CGEN_LIGHTING_DIFFUSE || st->rgbGen == CGEN_LIGHTING_DIFFUSE_ENTITY ) {
		flags |= MATS_ENTITY;
	}
	return flags;
}

qboolean RE_MaterialInfo( qhandle_t shader, materialInfo_t *out )
{
	memset( out, 0, sizeof( *out ) );
	if ( shader <= 0 || shader >= tr.numShaders ) {
		return qfalse;
	}
	MakeLists();

	shader_t *sh = tr.shaders[shader];

	out->shader = shader;
	Q_strncpyz( out->name, sh->name, sizeof( out->name ) );
	if ( sh->explicitlyDefined ) {
		out->flags |= MATI_SCRIPT;
	}
	if ( sh->sky ) {
		out->flags |= MATI_SKY;
	}
	if ( sh->contentFlags & CONTENTS_FOG ) {
		out->flags |= MATI_FOG;
	}
	if ( !sh->numUnfoggedPasses ) {
		out->flags |= MATI_NODRAW;
	}
	out->area = AreaOfName( sh->name );
	if ( out->area > 0.0f ) {
		out->flags |= MATI_LEVEL;
	}
	if ( sh->emissiveSet ) {
		memcpy( out->emissive, sh->emissive, sizeof( out->emissive ) );
	}
	out->surfaceLight = sh->surfaceLight;

	for ( int i = 0; i < sh->numUnfoggedPasses && out->numStages < MATERIAL_MAX_STAGES; i++ ) {
		const shaderStage_t *st = &sh->stages[i];
		materialStage_t *ms = &out->stages[out->numStages];

		if ( !st->active ) {
			continue;
		}
		ms->sourceStage = st->sourceStage;
		ms->flags = StageFlags( st );
		if ( st->bundle[0].isLightmap ) {
			Q_strncpyz( ms->image, "$lightmap", sizeof( ms->image ) );
		} else if ( st->bundle[0].image ) {
			Q_strncpyz( ms->image, st->bundle[0].image->imgName, sizeof( ms->image ) );
		}
		out->numStages++;
	}
	return qtrue;
}

// the stage in the shader or, when this variant merged it away, in another of that name
static shaderStage_t *FindStageOfName( shader_t *sh, int sourceStage )
{
	shaderStage_t *st = FindStage( sh, sourceStage );

	if ( !st ) {
		shader_t *variants[MAX_VARIANTS];
		const int count = R_ShadersNamed( sh->name, variants, MAX_VARIANTS );

		for ( int i = 0; i < count && !st; i++ ) {
			st = FindStage( variants[i], sourceStage );
		}
	}
	return st;
}

qboolean RE_MaterialGet( qhandle_t shader, int sourceStage, materialParms_t *out )
{
	memset( out, 0, sizeof( *out ) );
	out->roughness = 0.8f;
	out->parallaxPlane = 0.5f;
	if ( shader <= 0 || shader >= tr.numShaders ) {
		return qfalse;
	}

	const shaderStage_t *st = FindStageOfName( tr.shaders[shader], sourceStage );
	if ( !st ) {
		return qfalse;
	}
	const stageMaterial_t *mat = st->mat;
	if ( !mat ) {
		return qtrue;
	}
	out->pbr = (qboolean)mat->pbr;
	out->roughness = mat->roughness;
	out->metalness = mat->metalness;
	out->parallaxDepth = mat->parallaxDepth;
	out->parallaxPlane = mat->parallaxPlane;
	VectorCopy( mat->specular, out->specular );
	out->wrapLight = mat->wrapLight;
	out->sparkle = mat->sparkle;
	if ( mat->normalMap ) {
		Q_strncpyz( out->normalMap, mat->normalMap->imgName, sizeof( out->normalMap ) );
	}
	out->normalHeight = (qboolean)mat->normalHeight;
	if ( mat->rmoMap ) {
		Q_strncpyz( out->rmoMap, mat->rmoMap->imgName, sizeof( out->rmoMap ) );
	}
	out->ormOrder = (qboolean)mat->ormOrder;
	return qtrue;
}

static image_t *MaterialImage( const char *name, const shader_t *sh )
{
	if ( !name[0] ) {
		return NULL;
	}
	// never compressed: normal and rmo maps look poor in blocks
	return R_FindImageFile( name, (qboolean)!sh->noMipMaps, (qboolean)!sh->noPicMip, qfalse, GL_REPEAT );
}

qboolean RE_MaterialSet( qhandle_t shader, int sourceStage, const materialParms_t *in, int fields )
{
	if ( shader <= 0 || shader >= tr.numShaders ) {
		return qfalse;
	}

	shader_t *sh = tr.shaders[shader];
	shader_t *variants[MAX_VARIANTS];
	const int count = R_ShadersNamed( sh->name, variants, MAX_VARIANTS );
	stageMaterial_t *mat = NULL;

	// the record of the name, given to every variant that has the stage and had no material yet
	for ( int i = 0; i < count; i++ ) {
		shaderStage_t *st = FindStage( variants[i], sourceStage );

		if ( !st ) {
			continue;
		}
		if ( !mat ) {
			mat = R_MaterialFor( sh->name, sourceStage, qtrue, NULL );
		}
		st->mat = mat;
	}
	if ( !mat ) {
		return qfalse;
	}

	if ( fields & MATP_PBR ) {
		mat->pbr = in->pbr ? true : false;
	}
	if ( fields & MATP_ROUGHNESS ) {
		mat->roughness = Com_Clamp( 0.0f, 1.0f, in->roughness );
	}
	if ( fields & MATP_METALNESS ) {
		mat->metalness = Com_Clamp( 0.0f, 1.0f, in->metalness );
	}
	if ( fields & MATP_PARALLAX ) {
		mat->parallaxDepth = Com_Clamp( 0.0f, 0.2f, in->parallaxDepth );
		mat->parallaxPlane = Com_Clamp( 0.0f, 1.0f, in->parallaxPlane );
	}
	if ( fields & MATP_SPECULAR ) {
		VectorCopy( in->specular, mat->specular );
	}
	if ( fields & MATP_WRAPLIGHT ) {
		mat->wrapLight = in->wrapLight;
	}
	if ( fields & MATP_SPARKLE ) {
		mat->sparkle = in->sparkle;
	}
	if ( fields & MATP_NORMALMAP ) {
		mat->normalMap = MaterialImage( in->normalMap, sh );
		mat->normalHeight = mat->normalMap && in->normalHeight;
	}
	if ( fields & MATP_RMOMAP ) {
		mat->rmoMap = MaterialImage( in->rmoMap, sh );
		mat->ormOrder = mat->rmoMap && in->ormOrder;
	}
	return qtrue;
}
