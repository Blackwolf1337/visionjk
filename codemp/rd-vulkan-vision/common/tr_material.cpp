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
			// of the texture the material was made from, not of a made albedo
			ms->luma = ( st->mat && st->mat->baseImage ) ? st->mat->baseImage->averageLuma : st->bundle[0].image->averageLuma;
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
	R_DefaultMaterialGen( &out->gen );
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
	out->gen = mat->gen;
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
	image_t *texture = NULL;

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
		if ( !texture ) {
			// the texture of the stage, which the maps are made from
			const textureBundle_t *bundle = ( st->bundle[0].isLightmap && st->bundle[1].image ) ? &st->bundle[1] : &st->bundle[0];

			texture = bundle->image;
		}
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
	// (a made map, *gen..., is not a file: it stays, gen says whether it is made)
	if ( ( fields & MATP_NORMALMAP ) && Q_strncmp( in->normalMap, "*gen", 4 ) ) {
		mat->normalMap = MaterialImage( in->normalMap, sh );
		mat->normalHeight = mat->normalMap && in->normalHeight;
	}
	if ( ( fields & MATP_RMOMAP ) && Q_strncmp( in->rmoMap, "*gen", 4 ) ) {
		mat->rmoMap = MaterialImage( in->rmoMap, sh );
		mat->ormOrder = mat->rmoMap && in->ormOrder;
		mat->rmoRelative = false;
	}
	if ( fields & MATP_GEN ) {
		// made again from the texture the record had them from (the stage shows the albedo now), into its images
		mat->gen = in->gen;
		R_MakeMaterialMaps( mat, mat->baseImage ? NULL : texture );
		for ( int i = 0; i < count; i++ ) {
			shaderStage_t *st = FindStage( variants[i], sourceStage );

			if ( st ) {
				R_MaterialStageImage( st );
			}
		}
	}
	return qtrue;
}

/*
===============
The text of a shader with its materials, and saving it
===============
*/

// what a stage line of a material says, which the text gets again from the record
static const char *const materialWords[] = { "normalMap", "normalHeightMap", "rmoMap", "ormMap", "roughness", "gloss",
	"metalness", "parallaxDepth", "parallaxPlane", "albedoGen", "normalGen", "rmoGen", "specular", "wrapLight", "sparkle", NULL };

static qboolean IsMaterialWord( const char *word ) {
	for ( int i = 0; materialWords[i]; i++ ) {
		if ( !Q_stricmp( word, materialWords[i] ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

typedef struct {
	char	*buf;
	int		size, length;
	qboolean full;
} textOut_t;

static void QDECL Out( textOut_t *out, const char *fmt, ... ) __attribute__ ( ( format( printf, 2, 3 ) ) );
static void QDECL Out( textOut_t *out, const char *fmt, ... ) {
	va_list args;
	int written;

	if ( out->full ) {
		return;
	}
	va_start( args, fmt );
	written = Q_vsnprintf( out->buf + out->length, out->size - out->length, fmt, args );
	va_end( args );
	if ( written < 0 || written >= out->size - out->length ) {
		out->full = qtrue;
		out->buf[out->size - 1] = '\0';
		return;
	}
	out->length += written;
}

static void OutIndent( textOut_t *out, int depth ) {
	for ( int i = 0; i < depth; i++ ) {
		Out( out, "\t" );
	}
}

// a made map is not a file: its name is not written (gen says it is made)
static qboolean IsFileImage( const image_t *image ) {
	return (qboolean)( image && Q_strncmp( image->imgName, "*gen", 4 ) );
}

// the lines of a material, at depth
static void OutMaterial( textOut_t *out, const stageMaterial_t *mat, int depth ) {
	const materialGen_t *g = &mat->gen;

	if ( g->flags & MATGEN_ALBEDO ) {
		OutIndent( out, depth );
		Out( out, "albedoGen %02x%02x%02x %g %g\n", Com_Clampi( 0, 255, (int)( g->tint[0] * 255.0f + 0.5f ) ),
			Com_Clampi( 0, 255, (int)( g->tint[1] * 255.0f + 0.5f ) ), Com_Clampi( 0, 255, (int)( g->tint[2] * 255.0f + 0.5f ) ),
			g->delight, g->saturation );
	}
	if ( g->flags & MATGEN_NORMAL ) {
		OutIndent( out, depth );
		Out( out, "normalGen %g %g %g%s\n", g->normalStrength, g->normalBlur, g->normalDetail, g->normalInvert ? " invert" : "" );
	} else if ( IsFileImage( mat->normalMap ) ) {
		OutIndent( out, depth );
		Out( out, "%s %s\n", mat->normalHeight ? "normalHeightMap" : "normalMap", mat->normalMap->imgName );
	}
	if ( g->flags & MATGEN_RMO ) {
		OutIndent( out, depth );
		Out( out, "rmoGen %g %g\n", g->rmoVariation, g->rmoOcclusion );
	} else if ( IsFileImage( mat->rmoMap ) ) {
		OutIndent( out, depth );
		Out( out, "%s %s\n", mat->ormOrder ? "ormMap" : "rmoMap", mat->rmoMap->imgName );
	}
	if ( mat->pbr ) {
		OutIndent( out, depth );
		Out( out, "roughness %g\n", mat->roughness );
		OutIndent( out, depth );
		Out( out, "metalness %g\n", mat->metalness );
		if ( mat->parallaxDepth > 0.0f ) {
			OutIndent( out, depth );
			Out( out, "parallaxDepth %g\n", mat->parallaxDepth );
			OutIndent( out, depth );
			Out( out, "parallaxPlane %g\n", mat->parallaxPlane );
		}
	}
	if ( mat->specular[0] || mat->specular[1] || mat->specular[2] ) {
		OutIndent( out, depth );
		Out( out, "specular %g %g %g\n", mat->specular[0], mat->specular[1], mat->specular[2] );
	}
	if ( mat->wrapLight ) {
		OutIndent( out, depth );
		Out( out, "wrapLight %g\n", mat->wrapLight );
	}
	if ( mat->sparkle ) {
		OutIndent( out, depth );
		Out( out, "sparkle %g\n", mat->sparkle );
	}
}

// a word as the parser reads it again: in quotes when it has a space
static void OutWord( textOut_t *out, const char *word, qboolean first ) {
	Out( out, strchr( word, ' ' ) ? "%s\"%s\"" : "%s%s", first ? "" : " ", word );
}

/*
===============
RE_MaterialText

The text of the shader with the materials it has now: the loaded text line by line with the material lines of each
stage given again from its record (before the closing brace of the stage, or after implicitMap); a shader without
text gets implicitMap with the material of the stage of its texture. *file: where it came from ("" without text).
qfalse when it does not fit.
===============
*/
static qboolean MaterialText( shader_t *sh, char *buf, int size, const char **file ) {
	textOut_t out = { buf, size, 0, qfalse };
	const char *text = R_ShaderTextOf( sh->name, file );

	buf[0] = '\0';
	Out( &out, "%s\n{\n", sh->name );
	if ( !text ) {
		const stageMaterial_t *mat = R_MaterialFor( sh->name, 1, qfalse, NULL );

		Out( &out, "\timplicitMap -\n" );
		if ( mat ) {
			OutMaterial( &out, mat, 1 );
		}
		Out( &out, "}\n" );
		return (qboolean)!out.full;
	}

	const char *p = text;
	char *token = COM_ParseExt( &p, qtrue );
	int depth = 1, stage = 0;

	if ( Q_stricmp( token, "{" ) ) {
		return qfalse;
	}
	while ( depth > 0 ) {
		token = COM_ParseExt( &p, qtrue );
		if ( !token[0] ) {
			return qfalse;
		}
		if ( !Q_stricmp( token, "{" ) ) {
			OutIndent( &out, depth );
			Out( &out, "{\n" );
			depth++;
			continue;
		}
		if ( !Q_stricmp( token, "}" ) ) {
			if ( depth == 2 ) {
				const stageMaterial_t *mat = R_MaterialFor( sh->name, stage, qfalse, NULL );

				if ( mat ) {
					OutMaterial( &out, mat, 2 );
				}
				stage++;
			}
			depth--;
			OutIndent( &out, depth );
			Out( &out, "}\n" );
			continue;
		}
		// a line: its first word and the rest of it
		const qboolean material = IsMaterialWord( token );
		const qboolean implicit = ( depth == 1 && !Q_stricmp( token, "implicitMap" ) ) ? qtrue : qfalse;

		if ( !material ) {
			OutIndent( &out, depth );
			OutWord( &out, token, qtrue );
		}
		while ( ( token = COM_ParseExt( &p, qfalse ) )[0] ) {
			if ( !material ) {
				OutWord( &out, token, qfalse );
			}
		}
		if ( !material ) {
			Out( &out, "\n" );
		}
		if ( implicit ) {
			// the stages of implicitMap, with a lightmap: the texture is the second (DefaultStages)
			const stageMaterial_t *mat = R_MaterialFor( sh->name, stage + 1, qfalse, NULL );

			if ( mat ) {
				OutMaterial( &out, mat, 1 );
			}
			stage += 2;
		}
	}
	return (qboolean)!out.full;
}

qboolean RE_MaterialText( qhandle_t shader, char *buf, int size, char *file, int fileSize ) {
	const char *from = "";

	if ( size > 0 ) {
		buf[0] = '\0';
	}
	if ( fileSize > 0 ) {
		file[0] = '\0';
	}
	if ( shader <= 0 || shader >= tr.numShaders || size < 2 ) {
		return qfalse;
	}
	const qboolean ok = MaterialText( tr.shaders[shader], buf, size, &from );

	if ( fileSize > 0 ) {
		Q_strncpyz( file, from, fileSize );
	}
	return ok;
}

// the same picture file (the check may load it with other mip settings, so by name)
static qboolean SameFile( const image_t *a, const image_t *b ) {
	const char *na = IsFileImage( a ) ? a->imgName : "", *nb = IsFileImage( b ) ? b->imgName : "";

	return (qboolean)!Q_stricmp( na, nb );
}

// the material the text gives a stage is the one the record has (as far as the text keeps it)
static qboolean SameMaterial( const stageMaterial_t *a, const stageMaterial_t *b ) {
	const float e = 0.0005f;

	if ( !a || !b ) {
		// a record that says nothing is the same as none
		const stageMaterial_t *m = a ? a : b;

		return (qboolean)( !m || ( !m->pbr && !m->gen.flags && !IsFileImage( m->normalMap ) && !IsFileImage( m->rmoMap )
			&& !m->specular[0] && !m->specular[1] && !m->specular[2] && !m->wrapLight && !m->sparkle ) );
	}
	if ( a->pbr != b->pbr || a->gen.flags != b->gen.flags ) {
		return qfalse;
	}
	if ( a->pbr && ( fabsf( a->roughness - b->roughness ) > e || fabsf( a->metalness - b->metalness ) > e
		|| fabsf( a->parallaxDepth - b->parallaxDepth ) > e || ( a->parallaxDepth > 0.0f && fabsf( a->parallaxPlane - b->parallaxPlane ) > e ) ) ) {
		return qfalse;
	}
	if ( !( a->gen.flags & MATGEN_NORMAL ) && !SameFile( a->normalMap, b->normalMap ) ) {
		return qfalse;
	}
	if ( !( a->gen.flags & MATGEN_RMO ) && !SameFile( a->rmoMap, b->rmoMap ) ) {
		return qfalse;
	}
	return qtrue;
}

/*
===============
RE_MaterialSave

The text of the shader with its materials into shaders/zz_mat_<file>.oshader of the profile (<file> the shader file
it came from, textures for a shader without text), in place of the one of that name there or added. Overrides are
read last when a level loads, so the next load gives what is shown now. The text is parsed first and its materials
compared with the records: qfalse and err when it would not give the same.
===============
*/
qboolean RE_MaterialSave( qhandle_t shader, char *path, int pathSize, char *err, int errSize ) {
	static char text[32768], merged[262144];
	const stageMaterial_t *parsed[MATERIAL_MAX_STAGES];
	char stem[MAX_QPATH], file[MAX_QPATH];
	const char *from = "";

	err[0] = '\0';
	path[0] = '\0';
	if ( shader <= 0 || shader >= tr.numShaders ) {
		Com_sprintf( err, errSize, "no such shader" );
		return qfalse;
	}
	shader_t *sh = tr.shaders[shader];

	if ( !MaterialText( sh, text, sizeof( text ), &from ) ) {
		Com_sprintf( err, errSize, "the text of %s is too long", sh->name );
		return qfalse;
	}
	if ( !R_CheckShaderText( text, sh->lightmapIndex, parsed, err, errSize ) ) {
		return qfalse;
	}
	for ( int s = 0; s < MATERIAL_MAX_STAGES; s++ ) {
		if ( !SameMaterial( parsed[s], R_MaterialFor( sh->name, s, qfalse, NULL ) ) ) {
			Com_sprintf( err, errSize, "the text would not give stage %d the material it has now, not saved", s );
			return qfalse;
		}
	}

	// the file: the stem of the shader file without the prefixes of material files
	COM_StripExtension( from[0] ? from : "textures", stem, sizeof( stem ) );
	const char *bare = stem;

	if ( !Q_stricmpn( bare, "zz_mat_", 7 ) ) {
		bare += 7;
	} else if ( !Q_stricmpn( bare, "vjk_", 4 ) ) {
		bare += 4;
	}
	Com_sprintf( file, sizeof( file ), "shaders/zz_mat_%s.oshader", bare );
	Q_strncpyz( path, file, pathSize );

	// the shaders of the file that are there, but the one of this name
	textOut_t out = { merged, (int)sizeof( merged ), 0, qfalse };
	void *old = NULL;
	const long oldLength = ri.FS_ReadFile( file, &old );

	Out( &out, "// materials of the material debug menu (v_matdebug) for %s, read after the shader files\n\n", from[0] ? from : "textures without a shader" );
	if ( oldLength > 0 && old ) {
		const char *p = (const char *)old;

		while ( 1 ) {
			const char *start;
			char *token;

			// (a comment before a name stays with the name before it, so the line of the header is not kept twice)
			token = COM_ParseExt( &p, qtrue );
			if ( !token[0] ) {
				break;
			}
			char name[MAX_QPATH];

			Q_strncpyz( name, token, sizeof( name ) );
			start = p;
			SkipBracedSection( &p, 0 );
			if ( Q_stricmp( name, sh->name ) ) {
				Out( &out, "%s%.*s\n\n", name, (int)( p - start ), start );
			}
		}
	}
	if ( old ) {
		ri.FS_FreeFile( old );
	}
	Out( &out, "%s", text );
	if ( out.full ) {
		Com_sprintf( err, errSize, "%s is too long", file );
		return qfalse;
	}
	ri.FS_WriteFile( file, merged, out.length );
	return qtrue;
}
