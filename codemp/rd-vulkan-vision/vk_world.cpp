/*
===========================================================================
Copyright (C) 2026, OpenJK contributors
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

// vk_world.cpp: the faces, triangle meshes and patches of the level in buffers on the device
//
// rd-vanilla copies every visible surface of the level into the tess arrays each frame, vertex by vertex
// (RB_SurfaceFace, RB_SurfaceTriangles, RB_SurfaceGrid). Here the vertices of those surfaces are put into one buffer on the
// device when the level is first drawn, in the layout of the tess arrays. A batch that is made of such surfaces
// only collects their indexes each frame, the vertices are never touched again.
//
// Not every batch can be drawn that way. What still needs the vertices on the CPU goes through the tess arrays
// as before: sky, the deformVertexes stage.glsl does not do, surface sprites, alphaGen blend, vertex
// lit surfaces with more than one light style.
//
// Patches are drawn with all the rows and columns they were subdivided into. rd-vanilla leaves rows and columns
// out with the distance (r_lodCurveError), so a patch far away is a little rounder here than there.

#include "vk_local.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

static std::unordered_map<const void *, int>	vk_firstVertex;		// of a surface in the buffer
static std::unordered_set<const void *>		vk_stampSurfaces;	// autosprite: drawn as quads that face the eye
static vkBuffer_t		vk_worldVertexes;
static vkSize_t			vk_worldOffsets[4];			// where xyz, normal, texCoords and colors start in it
static qboolean			vk_worldValid;
// the batch in tess: 0: vertices and indexes in the tess arrays, 1: indexes into the buffer of the level,
// 2: runs of indexes in the buffer of the models (vk_model.cpp), all of one skeleton (NULL: MD3 meshes with one frame),
// 3: indexes into the buffer of the saber marks on models, all of one skeleton,
// 4: sprites, the glow of blades, oriented quads and lines, as records for shaders/sprite.vert (VK_AddDeviceSprites)
static int				vk_batchMode;

// The quads of a batch, records of shaders/sprite.vert (VK_QuadRecord)
static vec4_t			vk_sprites[VK_MAX_BATCH_SPRITES * VK_SPRITE_ROWS];
static int				vk_numSprites;

cvar_t	*v_vkSprites;		// 0: sprites built on the CPU as in rd-vanilla

// The surface sprites of the level (grass): placed once when the level is loaded, four vertices each, and the
// indexes of their two triangles. For every surface the run of them for each of its stages.
typedef struct vkGrassSurface_s {
	int					first[MAX_SHADER_STAGES];	// in sprites
	int					count[MAX_SHADER_STAGES];
} vkGrassSurface_t;
#define VK_MAX_BATCH_GRASS		1024
static vkBuffer_t		vk_grassVertexes;
static vkBuffer_t		vk_grassIndexes;
static std::unordered_map<const void *, vkGrassSurface_t>	vk_grassSurfaces;
static const vkGrassSurface_t	*vk_batchGrass[VK_MAX_BATCH_GRASS];	// of the surfaces in the batch
static int				vk_numBatchGrass;
static vkIndexRange_t	vk_grassRanges[VK_MAX_BATCH_GRASS];

cvar_t	*v_vkGrass;			// 0: surface sprites on the CPU as in rd-vanilla, the surface under them too

cvar_t	*v_vkStatic;		// 0: nothing from buffers on the device, every vertex through the tess arrays as in rd-vanilla
static CBoneCache		*vk_batchBones;
static float			vk_batchGoreScale = 1.0f;	// of the one saber mark of a batch that grows or fades
static float			vk_batchGoreFade = 1.0f;

qboolean VK_BatchIsStatic( void ) {
	return (qboolean)( vk_batchMode != 0 );
}

qboolean VK_BatchIsSkinned( void ) {
	return (qboolean)( vk_batchMode == 2 || vk_batchMode == 3 );
}

// A batch of one saber mark that grows or fades: how much its texture is scaled around its middle, and its fade as
// RB_SurfaceGhoul() reads it (below 1 the alpha, from 2 to 3 the alpha and how bright it may be). qfalse: neither.
qboolean VK_BatchGore( float *scale, float *fade ) {
	*scale = vk_batchGoreScale;
	*fade = vk_batchGoreFade;
	return (qboolean)( vk_batchMode == 3 && ( vk_batchGoreScale != 1.0f || vk_batchGoreFade != 1.0f ) );
}

qboolean VK_BatchIsSprites( void ) {
	return (qboolean)( vk_batchMode == 4 );
}

// RB_BeginSurface(): the first surface of the batch decides again
void VK_BatchBegin( void ) {
	vk_batchMode = 0;
	vk_numSprites = 0;
	vk_numBatchGrass = 0;
	vk_batchGoreScale = vk_batchGoreFade = 1.0f;
	VK_ModelBatchBegin();
}

/*
===============
VK_WorldChanged

R_Init() ran: another level is about to be loaded, or the same one again
===============
*/
void VK_WorldChanged( void ) {
	vk_worldValid = qfalse;
}

void VK_ShutdownWorld( void ) {
	VK_DestroyBuffer( &vk_worldVertexes );
	VK_DestroyBuffer( &vk_grassVertexes );
	VK_DestroyBuffer( &vk_grassIndexes );
	vk_firstVertex.clear();
	vk_stampSurfaces.clear();
	vk_grassSurfaces.clear();
	vk_worldValid = qfalse;
}

// a stage whose surface sprites grass.vert draws: they stand on the level and do not move by themselves
static qboolean VK_GrassStage( const shaderStage_t *pStage ) {
	return (qboolean)( pStage->ss && ( pStage->ss->surfaceSpriteType == SURFSPRITE_VERTICAL
		|| pStage->ss->surfaceSpriteType == SURFSPRITE_FLATTENED || pStage->ss->surfaceSpriteType == SURFSPRITE_ORIENTED ) );
}

/*
===============
VK_PlaceGrass

The surface sprites of one stage on the triangles of one surface of the level, as RB_DrawVerticalSurfaceSprites()
and RB_DrawOrientedSurfaceSprites() of tr_surfacesprites.cpp place them each frame: the same steps over each
triangle and the same numbers of randomchart, so every sprite stands where it stands there. What they do with the
view (the alpha from the distance, the wind) is left to grass.vert.
===============
*/
static void VK_PlaceGrass( const surfaceSprite_t *ss, const vec4_t *xyz, const vec4_t *normal, const color4ub_t *colors,
	const int *indexes, int numIndexes, std::vector<vkGrassVertex_t> &out ) {
	const qboolean oriented = (qboolean)( ss->surfaceSpriteType == SURFSPRITE_ORIENTED );
	const float minNormal = ss->facing ? 0.99f : 0.5f;

	for ( int t = 0; t + 2 < numIndexes; t += 3 ) {
		const int corners[3] = { indexes[t], indexes[t + 1], indexes[t + 2] };
		const float *v1 = xyz[corners[0]], *v2 = xyz[corners[1]], *v3 = xyz[corners[2]];
		qboolean facing = qtrue;
		vec2_t vec1to2, vec1to3;
		float triarea, step;
		int rightvectorcount = 0;

		for ( int i = 0; i < 3 && facing; i++ ) {
			const float up = normal[corners[i]][2];

			if ( oriented ) {
				facing = (qboolean)( up >= minNormal );
			} else {
				facing = (qboolean)( ss->facing ? up <= -0.5f : up >= 0.5f );	// hanging down, or pointing up
			}
		}
		if ( !facing ) {
			continue;
		}
		vec1to2[0] = v2[0] - v1[0];
		vec1to2[1] = v2[1] - v1[1];
		vec1to3[0] = v3[0] - v1[0];
		vec1to3[1] = v3[1] - v1[1];
		triarea = fabs( vec1to3[0] * vec1to2[1] - vec1to3[1] * vec1to2[0] );
		if ( triarea <= 1.0 ) {
			continue;
		}
		step = ss->density * Q_rsqrt( triarea );

		byte randomindex = (byte)( v1[0] + v1[1] + v2[0] + v2[1] + v3[0] + v3[1] );
		const byte randominterval = (byte)( v1[0] + v2[1] + v3[2] ) | 0x03;

		for ( float posi = 0; posi < 1.0; posi += step ) {
			for ( float posj = 0; posj < ( 1.0 - posi ); posj += step ) {
				float fa, fb, fc, fade, width, height;
				vec3_t point;
				vec2_t skew = { 0.0f, 0.0f };
				byte randomindex2;

				// the vertical ones draw both numbers before they look at them, the oriented ones one at a time
				fa = posi + randomchart[randomindex] * step;
				randomindex += randominterval;
				if ( oriented && fa > 1.0 ) {
					continue;
				}
				fb = posj + randomchart[randomindex] * step;
				randomindex += randominterval;
				if ( !oriented ) {
					rightvectorcount = ( rightvectorcount + 1 ) & 3;
				}
				if ( fa > 1.0 || fb > ( 1.0 - fa ) ) {
					continue;
				}
				fc = 1.0 - fa - fb;

				fade = randomchart[randomindex];
				randomindex += randominterval;
				if ( oriented ) {
					randomindex += randominterval;
				}

				randomindex2 = randomindex;
				width = ss->width * ( 1.0 + ( ss->variance[0] * randomchart[randomindex2] ) );
				height = ss->height * ( 1.0 + ( ss->variance[1] * randomchart[randomindex2++] ) );
				if ( randomchart[randomindex2++] > 0.5 ) {
					width = -width;
				}
				if ( !oriented && ss->vertSkew != 0 ) {
					skew[0] = height * ( ( ss->vertSkew * 2.0f * randomchart[randomindex2++] ) - ss->vertSkew );
					skew[1] = height * ( ( ss->vertSkew * 2.0f * randomchart[randomindex2++] ) - ss->vertSkew );
				}

				VectorScale( v1, fa, point );
				VectorMA( point, fb, v2, point );
				VectorMA( point, fc, v3, point );

				// sum of w * |corner - point|^2: with it the squared distance of the point to the eye is the one rd-vanilla
				// interpolates from the corners
				vec3_t d1, d2, d3;
				VectorSubtract( v1, point, d1 );
				VectorSubtract( v2, point, d2 );
				VectorSubtract( v3, point, d3 );
				const float spread = fa * DotProduct( d1, d1 ) + fb * DotProduct( d2, d2 ) + fc * DotProduct( d3, d3 );
				const float light = colors[corners[0]][2] * fa + colors[corners[1]][2] * fb + colors[corners[2]][2] * fc;

				for ( int c = 0; c < 4; c++ ) {
					vkGrassVertex_t v;

					VectorCopy( point, v.xyz );
					v.light = light;
					v.width = width;
					v.height = height;
					v.skew[0] = skew[0];
					v.skew[1] = skew[1];
					v.fade = fade;
					v.spread = spread;
					v.parm = (float)( c + 4 * rightvectorcount );
					v.unused = 0.0f;
					out.push_back( v );
				}
			}
		}
	}
}

// the triangles of a surface of the level, numbered in the vertex arrays of VK_BuildWorld(), as VK_AddSurface() has them
static void VK_SurfaceTriangles( const surfaceType_t *data, int first, std::vector<int> &out ) {
	out.clear();
	if ( *data == SF_FACE ) {
		const srfSurfaceFace_t *face = (const srfSurfaceFace_t *)data;
		const unsigned int *indices = (const unsigned int *)( (const char *)face + face->ofsIndices );

		for ( int i = 0; i < face->numIndices; i++ ) {
			out.push_back( (int)indices[i] + first );
		}
	} else if ( *data == SF_GRID ) {
		const srfGridMesh_t *grid = (const srfGridMesh_t *)data;

		for ( int row = 0; row < grid->height - 1; row++ ) {
			for ( int col = 0; col < grid->width - 1; col++ ) {
				const int v1 = first + row * grid->width + col + 1;
				const int v2 = v1 - 1;
				const int v3 = v2 + grid->width;
				const int v4 = v3 + 1;
				const int tri[6] = { v2, v3, v1, v1, v3, v4 };

				out.insert( out.end(), tri, tri + 6 );
			}
		}
	} else if ( *data == SF_TRIANGLES ) {
		const srfTriangles_t *tri = (const srfTriangles_t *)data;

		for ( int i = 0; i < tri->numIndexes; i++ ) {
			out.push_back( tri->indexes[i] + first );
		}
	}
}

/*
===============
VK_BuildGrass

The surface sprites of every surface of the level that has them, from the vertex arrays VK_BuildWorld() made
===============
*/
static void VK_BuildGrass( const vec4_t *xyz, const vec4_t *normal, const color4ub_t *colors ) {
	const world_t *world = tr.world;
	std::vector<vkGrassVertex_t> vertexes;
	std::vector<int> triangles;
	vkBuffer_t staging;

	for ( int i = 0; i < world->numsurfaces; i++ ) {
		const msurface_t *surf = &world->surfaces[i];
		const auto found = vk_firstVertex.find( surf->data );
		vkGrassSurface_t grass;
		qboolean any = qfalse;

		if ( found == vk_firstVertex.end() || !surf->shader ) {
			continue;
		}
		memset( &grass, 0, sizeof( grass ) );
		for ( int s = 0; s < surf->shader->numUnfoggedPasses; s++ ) {
			const shaderStage_t *pStage = &surf->shader->stages[s];

			if ( !pStage->active || !VK_GrassStage( pStage ) ) {
				continue;
			}
			if ( !any ) {
				VK_SurfaceTriangles( surf->data, found->second, triangles );
			}
			grass.first[s] = (int)( vertexes.size() / 4 );
			VK_PlaceGrass( pStage->ss, xyz, normal, colors, triangles.data(), (int)triangles.size(), vertexes );
			grass.count[s] = (int)( vertexes.size() / 4 ) - grass.first[s];
			any = qtrue;
		}
		if ( any ) {
			vk_grassSurfaces[surf->data] = grass;
		}
	}
	if ( vertexes.empty() ) {
		return;
	}

	const int numSprites = (int)( vertexes.size() / 4 );
	const vkSize_t vertexBytes = (vkSize_t)vertexes.size() * sizeof( vkGrassVertex_t );
	const vkSize_t indexBytes = (vkSize_t)numSprites * 6 * sizeof( glIndex_t );

	VK_CreateBuffer( &staging, vertexBytes + indexBytes, VKB_STAGING, qtrue );
	memcpy( staging.mapped, vertexes.data(), (size_t)vertexBytes );
	// bottom right, top right, top left and bottom right, top left, bottom left: GL_QUADS as rd-vanilla draws them
	glIndex_t *out = (glIndex_t *)( staging.mapped + vertexBytes );
	for ( int i = 0; i < numSprites; i++, out += 6 ) {
		const glIndex_t first = (glIndex_t)( i * 4 );

		out[0] = first;
		out[1] = first + 1;
		out[2] = first + 2;
		out[3] = first;
		out[4] = first + 2;
		out[5] = first + 3;
	}
	VK_CreateBuffer( &vk_grassVertexes, vertexBytes, VKB_VERTEXES, qfalse );
	VK_CreateBuffer( &vk_grassIndexes, indexBytes, VKB_INDEXES, qfalse );
	VK_CopyBuffer( &staging, 0, &vk_grassVertexes, 0, vertexBytes );
	VK_CopyBuffer( &staging, vertexBytes, &vk_grassIndexes, 0, indexBytes );
	VK_FinishUploads();
	VK_DestroyBuffer( &staging );
	ri.Printf( PRINT_DEVELOPER, "rd-vulkan-vision: %i surface sprites on %i surfaces of the level, %i KB\n",
		numSprites, (int)vk_grassSurfaces.size(), (int)( ( vertexBytes + indexBytes ) >> 10 ) );
}

/*
===============
VK_BindGrass

The surface sprites of one stage on the surfaces of the batch: their runs of indexes, and the view and the wind
of this frame for grass.vert in the bone buffer (twelve rows, see there). qfalse: there are none.
===============
*/
qboolean VK_BindGrass( const shaderStage_t *pStage, int stage, uint32_t *bones ) {
	const surfaceSprite_t *ss = pStage->ss;
	vkFrame_t *frame = vk.frame;
	const vkSize_t offset = 0;
	int numRanges = 0;

	if ( !frame || !vk_numBatchGrass || !vk_grassIndexes.buffer ) {
		return qfalse;
	}
	for ( int i = 0; i < vk_numBatchGrass; i++ ) {
		const vkGrassSurface_t *grass = vk_batchGrass[i];

		if ( !grass->count[stage] ) {
			continue;
		}
		const uint32_t first = (uint32_t)grass->first[stage] * 6, count = (uint32_t)grass->count[stage] * 6;

		// surfaces that follow each other in the level often follow each other here
		if ( numRanges && vk_grassRanges[numRanges - 1].first + vk_grassRanges[numRanges - 1].count == first ) {
			vk_grassRanges[numRanges - 1].count += count;
		} else {
			vk_grassRanges[numRanges].first = first;
			vk_grassRanges[numRanges].count = count;
			numRanges++;
		}
	}
	const int start = ( frame->numBones + VK_BONE_ALIGN - 1 ) / VK_BONE_ALIGN * VK_BONE_ALIGN;

	if ( !numRanges || start + 4 > VK_MAX_BONES ) {
		return qfalse;
	}

	// what RB_DrawSurfaceSprites() and the functions it calls work out from the view and the wind
	RB_SurfaceSpriteFrame();
	const float cutdist = ss->fadeMax * rangescalefactor, fadedist = ss->fadeDist * rangescalefactor;
	const uint32_t blend = pStage->stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );
	uint32_t flags = 0;

	if ( ss->surfaceSpriteType == SURFSPRITE_ORIENTED ) {
		flags |= 8u | ( ss->facing ? 16u : 0u );
	} else {
		flags |= ( ss->facing ? 1u : 0u ) | ( ss->surfaceSpriteType == SURFSPRITE_FLATTENED ? 2u : 0u );
		if ( curWindPointActive && ss->wind > 0 ) {
			flags |= 32u;
		}
	}
	if ( blend == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE ) ) {
		flags |= 4u;
	}
	vec4_t *row = (vec4_t *)( frame->bones.mapped + sizeof( float ) * 12 * start );
	VectorSet4( row[0], ssViewOrigin[0], ssViewOrigin[1], ssViewOrigin[2], (float)tr.refdef.time );
	VectorSet4( row[1], ssfwdvector[0], ssfwdvector[1], ssfwdvector[2], curWindSpeed );
	VectorSet4( row[2], curWindGrassDir[0], curWindGrassDir[1], curWindGrassDir[2], curWindPointForce * ss->wind );
	VectorSet4( row[3], curWindPoint[0], curWindPoint[1], curWindPoint[2], (float)flags );
	for ( int i = 0; i < 4; i++ ) {
		VectorSet4( row[4 + i], ssrightvectors[i][0], ssrightvectors[i][1], ssrightvectors[i][2], 0.0f );
	}
	VectorSet4( row[8], fadedist * fadedist, 1.0f / ( cutdist * cutdist - fadedist * fadedist ),
		Q_min( 250.0f / ( cutdist - fadedist ), 1.0f ), ss->fadeScale );		// FADE_RANGE of tr_surfacesprites.cpp
	VectorSet4( row[9], ss->wind, ss->windIdle, 0.0f, 0.0f );
	VectorSet4( row[10], ssViewRight[0], ssViewRight[1], ssViewRight[2], 0.0f );
	VectorSet4( row[11], ssViewUp[0], ssViewUp[1], ssViewUp[2], 0.0f );
	frame->numBones = start + 4;
	*bones = (uint32_t)start * 3;

	VK_BindVertexBuffer( &vk_grassVertexes, &offset, 1 );
	VK_BindIndexBuffer( &vk_grassIndexes );
	vk.vertexOffset = 0;
	vk.ranges = vk_grassRanges;
	vk.numRanges = numRanges;
	return qtrue;
}

/*
===============
VK_PrepareAutosprites

The quads of a surface whose shader turns them to the eye get in their vertices what Deform() of stage.glsl needs
for it, worked out as AutospriteDeform() and Autosprite2Deform() of tr_shade_calc.cpp do every frame, the view left
out. Autosprite: each corner sits in the middle of its quad, with its number and the radius in lightmap2 and the
texture and color of a quad stamp. Autosprite2: each corner sits in the middle of the short edge it is on, with the
long axis of the quad and its distance from that axis, signed, in lightmap2 and lightmap3.
===============
*/
static void VK_PrepareAutosprites( vec4_t *xyz, vec2_t ( *texCoords )[NUM_TEX_COORDS], color4ub_t *colors ) {
	static const int edgeVerts[6][2] = { { 0, 1 }, { 0, 2 }, { 0, 3 }, { 1, 2 }, { 1, 3 }, { 2, 3 } };
	static const float stampST[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	const world_t *world = tr.world;

	for ( int i = 0; i < world->numsurfaces; i++ ) {
		const msurface_t *surf = &world->surfaces[i];
		const surfaceType_t *data = surf->data;
		const auto found = vk_firstVertex.find( data );
		const unsigned int *indexes;
		int numVerts, numIndexes;

		if ( found == vk_firstVertex.end() || !surf->shader || surf->shader->numDeforms != 1
			|| ( surf->shader->deforms[0]->deformation != DEFORM_AUTOSPRITE && surf->shader->deforms[0]->deformation != DEFORM_AUTOSPRITE2 ) ) {
			continue;
		}
		if ( *data == SF_FACE ) {
			const srfSurfaceFace_t *face = (const srfSurfaceFace_t *)data;

			numVerts = face->numPoints;
			numIndexes = face->numIndices;
			indexes = (const unsigned int *)( (const char *)face + face->ofsIndices );
		} else if ( *data == SF_TRIANGLES ) {
			const srfTriangles_t *tri = (const srfTriangles_t *)data;

			numVerts = tri->numVerts;
			numIndexes = tri->numIndexes;
			indexes = (const unsigned int *)tri->indexes;
		} else {
			continue;
		}
		if ( ( numVerts & 3 ) || numIndexes != ( numVerts >> 2 ) * 6 ) {
			continue;		// rd-vanilla says it is odd and draws what comes of it, here it is drawn as it is
		}

		const int first = found->second;

		if ( surf->shader->deforms[0]->deformation == DEFORM_AUTOSPRITE ) {
			for ( int q = first; q < first + numVerts; q += 4 ) {
				vec3_t mid, delta;

				for ( int k = 0; k < 3; k++ ) {
					mid[k] = 0.25f * ( xyz[q][k] + xyz[q + 1][k] + xyz[q + 2][k] + xyz[q + 3][k] );
				}
				VectorSubtract( xyz[q], mid, delta );
				const float radius = VectorLength( delta ) * 0.707f;

				for ( int c = 0; c < 4; c++ ) {
					VectorCopy( mid, xyz[q + c] );
					VectorSet2( texCoords[q + c][0], stampST[c][0], stampST[c][1] );
					VectorSet2( texCoords[q + c][1], stampST[c][0], stampST[c][1] );
					VectorSet2( texCoords[q + c][3], (float)c, radius );
					memcpy( colors[q + c], colors[q], 4 );
				}
			}
			vk_stampSurfaces.insert( data );
			continue;
		}

		for ( int q = 0; q < numVerts; q += 4 ) {
			const float *quad[4] = { xyz[first + q], xyz[first + q + 1], xyz[first + q + 2], xyz[first + q + 3] };
			float lengths[2] = { 999999, 999999 };
			int nums[2] = { 0, 0 };
			vec3_t mid[2], major, place[4];
			float offset[4] = { 0, 0, 0, 0 };

			// the two shortest edges
			for ( int j = 0; j < 6; j++ ) {
				vec3_t temp;

				VectorSubtract( quad[edgeVerts[j][0]], quad[edgeVerts[j][1]], temp );
				const float l = DotProduct( temp, temp );

				if ( l < lengths[0] ) {
					nums[1] = nums[0];
					lengths[1] = lengths[0];
					nums[0] = j;
					lengths[0] = l;
				} else if ( l < lengths[1] ) {
					nums[1] = j;
					lengths[1] = l;
				}
			}
			for ( int c = 0; c < 4; c++ ) {
				VectorCopy( quad[c], place[c] );
			}
			for ( int j = 0; j < 2; j++ ) {
				const float *v1 = quad[edgeVerts[nums[j]][0]], *v2 = quad[edgeVerts[nums[j]][1]];

				for ( int k = 0; k < 3; k++ ) {
					mid[j][k] = 0.5f * ( v1[k] + v2[k] );
				}
			}
			VectorSubtract( mid[1], mid[0], major );
			for ( int j = 0; j < 2; j++ ) {
				const int e0 = edgeVerts[nums[j]][0], e1 = edgeVerts[nums[j]][1];
				const float l = 0.5 * sqrt( lengths[j] );
				int k;

				// which way round the triangles use the edge says which way each end goes
				for ( k = 0; k < 5; k++ ) {
					if ( indexes[( q >> 2 ) * 6 + k] == (unsigned)( q + e0 ) && indexes[( q >> 2 ) * 6 + k + 1] == (unsigned)( q + e1 ) ) {
						break;
					}
				}
				VectorCopy( mid[j], place[e0] );
				VectorCopy( mid[j], place[e1] );
				offset[e0] = ( k == 5 ) ? l : -l;
				offset[e1] = ( k == 5 ) ? -l : l;
			}
			for ( int c = 0; c < 4; c++ ) {
				VectorCopy( place[c], xyz[first + q + c] );
				VectorSet2( texCoords[first + q + c][3], major[0], major[1] );
				VectorSet2( texCoords[first + q + c][4], major[2], offset[c] );
			}
		}
	}
}

// A vertex lit surface of several light styles: the colors of the styles after the first into the texture
// coordinates of lightmap1 and lightmap2, which it has no use for, three bytes in a float each (exact up to 2^24)
static void VK_PackStyles( const msurface_t *surf, const byte *colors, vec2_t *texCoords ) {
	const shader_t *shader = surf->shader;
	float packed[3] = { 0.0f, 0.0f, 0.0f };

	if ( !shader || shader->lightmapIndex[0] != LIGHTMAP_BY_VERTEX || shader->styles[1] >= LS_UNUSED ) {
		return;
	}
	for ( int k = 1; k < MAXLIGHTMAPS; k++ ) {
		const byte *c = colors + 4 * k;

		packed[k - 1] = (float)( c[0] | ( c[1] << 8 ) | ( c[2] << 16 ) );
	}
	VectorSet2( texCoords[2], packed[0], packed[1] );
	VectorSet2( texCoords[3], packed[2], 0.0f );
}

static void VK_BuildWorld( void ) {
	const world_t *world = tr.world;
	int numVertexes = 0, vertex = 0;
	vkSize_t sizes[4], total = 0;
	vkBuffer_t staging;

	ri.LoadPush( "world buffers", NULL );
	VK_WaitIdle();
	VK_ShutdownWorld();
	vk_worldValid = qtrue;

	for ( int i = 0; i < world->numsurfaces; i++ ) {
		const surfaceType_t *data = world->surfaces[i].data;

		if ( *data == SF_FACE ) {
			numVertexes += ( (const srfSurfaceFace_t *)data )->numPoints;
		} else if ( *data == SF_TRIANGLES ) {
			numVertexes += ( (const srfTriangles_t *)data )->numVerts;
		} else if ( *data == SF_GRID ) {
			numVertexes += ( (const srfGridMesh_t *)data )->width * ( (const srfGridMesh_t *)data )->height;
		}
	}
	if ( !numVertexes ) {
		return;
	}
	sizes[0] = sizes[1] = (vkSize_t)numVertexes * sizeof( vec4_t );
	sizes[2] = (vkSize_t)numVertexes * sizeof( vec2_t ) * NUM_TEX_COORDS;
	sizes[3] = (vkSize_t)numVertexes * 4;
	for ( int i = 0; i < 4; i++ ) {
		vk_worldOffsets[i] = total;
		total += ( sizes[i] + 15 ) & ~(vkSize_t)15;
	}
	VK_CreateBuffer( &staging, total, VKB_STAGING, qtrue );
	memset( staging.mapped, 0, (size_t)total );

	vec4_t *xyz = (vec4_t *)( staging.mapped + vk_worldOffsets[0] );
	vec4_t *normal = (vec4_t *)( staging.mapped + vk_worldOffsets[1] );
	vec2_t ( *texCoords )[NUM_TEX_COORDS] = ( vec2_t ( * )[NUM_TEX_COORDS] )( staging.mapped + vk_worldOffsets[2] );
	color4ub_t *colors = (color4ub_t *)( staging.mapped + vk_worldOffsets[3] );

	for ( int i = 0; i < world->numsurfaces; i++ ) {
		const surfaceType_t *data = world->surfaces[i].data;

		if ( *data == SF_FACE ) {
			const srfSurfaceFace_t *face = (const srfSurfaceFace_t *)data;
			const float *v = face->points[0];

			vk_firstVertex[data] = vertex;
			for ( int j = 0; j < face->numPoints; j++, v += VERTEXSIZE, vertex++ ) {
				VectorCopy( v, xyz[vertex] );
				VectorCopy( face->plane.normal, normal[vertex] );
				VectorSet2( texCoords[vertex][0], v[3], v[4] );
				for ( int k = 0; k < MAXLIGHTMAPS; k++ ) {
					VectorSet2( texCoords[vertex][k + 1], v[VERTEX_LM + k * 2], v[VERTEX_LM + k * 2 + 1] );
				}
				// the colors of the first light style, see VK_StaticColorScale() and VK_PackStyles() for the rest
				memcpy( colors[vertex], &v[VERTEX_COLOR], 4 );
				VK_PackStyles( &world->surfaces[i], (const byte *)&v[VERTEX_COLOR], texCoords[vertex] );
			}
		} else if ( *data == SF_TRIANGLES || *data == SF_GRID ) {
			// both are an array of drawVert_t, the patch row by row
			const drawVert_t *dv = ( *data == SF_GRID ) ? ( (const srfGridMesh_t *)data )->verts : ( (const srfTriangles_t *)data )->verts;
			const int count = ( *data == SF_GRID ) ? ( (const srfGridMesh_t *)data )->width * ( (const srfGridMesh_t *)data )->height
				: ( (const srfTriangles_t *)data )->numVerts;

			vk_firstVertex[data] = vertex;
			for ( int j = 0; j < count; j++, dv++, vertex++ ) {
				VectorCopy( dv->xyz, xyz[vertex] );
				VectorCopy( dv->normal, normal[vertex] );
				VectorSet2( texCoords[vertex][0], dv->st[0], dv->st[1] );
				for ( int k = 0; k < MAXLIGHTMAPS; k++ ) {
					VectorSet2( texCoords[vertex][k + 1], dv->lightmap[k][0], dv->lightmap[k][1] );
				}
				memcpy( colors[vertex], dv->color[0], 4 );
				VK_PackStyles( &world->surfaces[i], dv->color[0], texCoords[vertex] );
			}
		}
	}

	VK_PrepareAutosprites( xyz, texCoords, colors );
	VK_CreateBuffer( &vk_worldVertexes, total, VKB_VERTEXES, qfalse );
	VK_CopyBuffer( &staging, 0, &vk_worldVertexes, 0, total );
	VK_FinishUploads();
	VK_BuildGrass( xyz, normal, colors );
	VK_DestroyBuffer( &staging );
	ri.LoadPop();
	ri.Printf( PRINT_DEVELOPER, "rd-vulkan-vision: %i vertices of %i surfaces of the level on the device, %i KB\n",
		numVertexes, (int)vk_firstVertex.size(), (int)( total >> 10 ) );
}

/*
===============
VK_ShaderIsStatic

Whether the stages of the shader of the batch can be drawn without the vertices on the CPU. world: the batch is of
surfaces of the level, which are prepared for some more (VK_PrepareAutosprites)
===============
*/
static qboolean VK_ShaderIsStatic( qboolean world ) {
	const shader_t *shader = tess.shader;

	if ( !v_vkStatic->integer ) {
		return qfalse;
	}
	// a shadow volume is built from the vertices as the bones have moved them
	if ( shader == tr.shadowShader ) {
		return qfalse;
	}
	// the lines of the normals are made from the vertices of the batch
	if ( r_shownormals->integer ) {
		return qfalse;
	}
	if ( shader->sky || !VK_DeformOnDevice( shader, world ) || r_fullbright->integer || tess.fading ) {
		return qfalse;
	}
	// rd-vanilla adds up the colors of all light styles of a vertex lit surface: the buffer of the level has those
	// of the other styles in place of the lightmaps (VK_PackStyles), the buffer of the models does not
	if ( !world && shader->lightmapIndex[0] == LIGHTMAP_BY_VERTEX && shader->styles[0] < LS_UNUSED && shader->styles[1] < LS_UNUSED ) {
		return qfalse;
	}
	if ( backEnd.currentEntity->e.renderfx & ( RF_DISINTEGRATE1 | RF_DISINTEGRATE2 | RF_VOLUMETRIC ) ) {
		return qfalse;
	}
	for ( int i = 0; i < shader->numUnfoggedPasses; i++ ) {
		const shaderStage_t *pStage = &shader->stages[i];

		if ( !pStage->active ) {
			break;
		}
		if ( pStage->alphaGen == AGEN_BLEND ) {
			return qfalse;
		}
		// surface sprites from the device stand on the level, the ones of brush models move with them
		if ( pStage->ss && pStage->ss->surfaceSpriteType
			&& ( !v_vkGrass->integer || !VK_GrassStage( pStage ) || backEnd.currentEntity != &tr.worldEntity ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

/*
===============
VK_StaticColorScale

What the vertex colors in the buffer have to be multiplied with to be the ones rd-vanilla computes per frame:
a vertex lit surface is its vertex color times the color of its light style (ComputeFinalVertexColor)
===============
*/
void VK_StaticColorScale( float *scale ) {
	const shader_t *shader = tess.shader;

	VectorSet( scale, 1.0f, 1.0f, 1.0f );
	if ( VK_StaticStyles() ) {
		return;		// stage.glsl adds up the styles
	}
	if ( shader->lightmapIndex[0] == LIGHTMAP_BY_VERTEX ) {
		if ( shader->styles[0] < LS_UNUSED ) {
			for ( int i = 0; i < 3; i++ ) {
				scale[i] = styleColors[shader->styles[0]][i] / 256.0f;
			}
		} else {
			VectorClear( scale );
		}
	}
}

// The light styles of a vertex lit surface of the level that has more than one, for Draw.styles: each + 1 in a byte.
// 0: it is none.
uint32_t VK_StaticStyles( void ) {
	const shader_t *shader = tess.shader;
	uint32_t styles = 0;

	if ( vk_batchMode != 1 || shader->lightmapIndex[0] != LIGHTMAP_BY_VERTEX || shader->styles[0] >= LS_UNUSED
		|| shader->styles[1] >= LS_UNUSED ) {
		return 0;
	}
	for ( int k = 0; k < MAXLIGHTMAPS && shader->styles[k] < LS_UNUSED; k++ ) {
		styles |= (uint32_t)( shader->styles[k] + 1 ) << ( 8 * k );
	}
	return styles;
}

// ends the batch and begins another one of the same shader for surfaces on the bones
static void VK_NewModelBatch( CBoneCache *bones, int mode ) {
	shader_t *shader = tess.shader;
	const int fogNum = tess.fogNum;

	RB_EndSurface();
	RB_BeginSurface( shader, fogNum );
	vk_batchMode = mode;
	vk_batchBones = bones;
}

/*
===============
VK_AddDeviceMesh

A surface of a model whose vertices are in the buffer of the models (vk_model.cpp) into the batch: a Ghoul2
surface with the bones of its skeleton, or an MD3 surface with one frame (bones NULL: it stays where it is).
qfalse: it has to take the way through the tess arrays.
===============
*/
static qboolean VK_AddDeviceMesh( CBoneCache *bones, const void *mesh, qboolean md3, int numIndexes ) {
	int first;

	if ( !tess.numIndexes && !tess.numVertexes ) {
		vk_batchMode = VK_ShaderIsStatic( qfalse ) ? 2 : 0;
		vk_batchBones = bones;
	} else if ( vk_batchMode == 2 && vk_batchBones != bones ) {
		// a batch has the bones of one skeleton
		VK_NewModelBatch( bones, 2 );
	}
	if ( vk_batchMode != 2 ) {
		return qfalse;
	}
	first = md3 ? VK_StaticMeshSurface( (const md3Surface_t *)mesh ) : VK_SkinnedSurface( (const mdxmSurface_t *)mesh );
	if ( first < 0 ) {
		return qfalse;
	}
	// the indexes stay on the device, but the batch still counts them
	RB_CHECKOVERFLOW( 0, numIndexes );
	vk_batchMode = 2;		// an overflow began a new batch
	vk_batchBones = bones;
	if ( !VK_ModelBatchAdd( first, numIndexes ) ) {
		VK_NewModelBatch( bones, 2 );
		VK_ModelBatchAdd( first, numIndexes );
	}
	tess.numIndexes += numIndexes;
	return qtrue;
}

/*
===============
VK_AddDeviceGore

A saber mark on a surface of a model into the batch, as indexes into the buffer of the marks (vk_model.cpp).
qfalse: it has to take the way through the tess arrays.
===============
*/
static qboolean VK_AddDeviceGore( const CRenderableSurface *rs ) {
	const int *data = (const int *)rs->alternateTex;
	const int numVerts = data[0], numIndexes = data[1] * 3;
	// behind the numbers of its vertices in the surface, their positions, normals and texture coordinates
	const int *triangles = data + 2 + numVerts * 11;
	glIndex_t *out;
	int first;

	// While it grows its texture coordinates change, while it fades its colors (RB_SurfaceGhoul): a draw of its
	// own then, with that in its numbers (VK_BatchGore). The marks next to it get a batch of theirs.
	const qboolean changing = (qboolean)( rs->scale > 1.0f || rs->fade != 1.0f );

	if ( !tess.numIndexes && !tess.numVertexes ) {
		vk_batchMode = VK_ShaderIsStatic( qfalse ) ? 3 : 0;
		vk_batchBones = rs->boneCache;
	} else if ( vk_batchMode == 3 && ( vk_batchBones != rs->boneCache || changing || vk_batchGoreScale != 1.0f || vk_batchGoreFade != 1.0f ) ) {
		VK_NewModelBatch( rs->boneCache, 3 );
	}
	if ( vk_batchMode != 3 ) {
		return qfalse;
	}
	first = VK_GoreSurface( rs->surfaceData, rs->alternateTex );
	if ( first < 0 ) {
		return qfalse;
	}
	RB_CHECKOVERFLOW( 0, numIndexes );
	vk_batchMode = 3;		// an overflow began a new batch
	vk_batchBones = rs->boneCache;
	vk_batchGoreScale = rs->scale > 1.0f ? rs->scale : 1.0f;
	vk_batchGoreFade = rs->fade;
	out = tess.indexes + tess.numIndexes;
	for ( int i = 0; i < numIndexes; i++ ) {
		out[i] = (glIndex_t)( triangles[i] + first );
	}
	tess.numIndexes += numIndexes;
	return qtrue;
}

/*
===============
VK_AddQuad

One quad into the batch, a record of shaders/sprite.vert: kind and what a, b and c are there. The six indexes
number its four corners.
===============
*/
// one record of shaders/sprite.vert: kind and what a, b, c and d are there
void VK_QuadRecord( vec4_t *row, int kind, const vec3_t a, float aw, const vec3_t b, float bw, const vec3_t c,
	const float *st, const float *color, const vec3_t d ) {
	VectorSet4( row[0], a[0], a[1], a[2], aw );
	VectorSet4( row[1], b[0], b[1], b[2], bw );
	VectorSet4( row[2], c[0], c[1], c[2], (float)kind );
	VectorSet4( row[3], st[0], st[1], st[2], st[3] );
	VectorSet4( row[4], color[0], color[1], color[2], color[3] );
	VectorSet4( row[5], d[0], d[1], d[2], 0.0f );
}

// the two triangles of RB_AddQuadStampExt() over the four corners of a quad that start at first
void VK_QuadIndexes( glIndex_t *out, glIndex_t first ) {
	out[0] = first;
	out[1] = first + 1;
	out[2] = first + 3;
	out[3] = first + 3;
	out[4] = first + 1;
	out[5] = first + 2;
}

/*
===============
VK_QuadView

The records of a draw of sprite.vert into the bone buffer of the frame, behind the view: forward with -1 in a
mirror, left, up and the eye (backEnd.viewParms.ori, as tr_surface.cpp reads it). bones: where that starts, in rows.
===============
*/
qboolean VK_QuadView( const vec4_t *records, int numQuads, uint32_t *bones ) {
	vkFrame_t		*frame = vk.frame;
	const int		rows = 4 + numQuads * VK_SPRITE_ROWS;
	const int		first = ( frame ? frame->numBones + VK_BONE_ALIGN - 1 : 0 ) / VK_BONE_ALIGN * VK_BONE_ALIGN;
	const int		count = ( rows + 2 ) / 3;		// the buffer counts in bones of three rows
	const float		*axis = &backEnd.viewParms.ori.axis[0][0];
	vec4_t			*out;

	if ( !frame || !numQuads || first + count > VK_MAX_BONES ) {
		return qfalse;
	}
	out = (vec4_t *)( frame->bones.mapped + sizeof( float ) * 12 * first );
	VectorSet4( out[0], axis[0], axis[1], axis[2], backEnd.viewParms.isMirror ? -1.0f : 1.0f );
	VectorSet4( out[1], axis[3], axis[4], axis[5], 0.0f );
	VectorSet4( out[2], axis[6], axis[7], axis[8], 0.0f );
	VectorSet4( out[3], backEnd.viewParms.ori.origin[0], backEnd.viewParms.ori.origin[1], backEnd.viewParms.ori.origin[2], 0.0f );
	memcpy( out + 4, records, sizeof( vec4_t ) * VK_SPRITE_ROWS * numQuads );
	frame->numBones = first + count;
	*bones = (uint32_t)first * 3;
	return qtrue;
}

static void VK_AddQuad( int kind, const vec3_t a, float aw, const vec3_t b, float bw, const vec3_t c, const float *st,
	const byte *color, const vec3_t d = vec3_origin ) {
	const glIndex_t	first = (glIndex_t)( vk_numSprites * 4 );

	if ( vk_numSprites == VK_MAX_BATCH_SPRITES || tess.numIndexes + 6 > SHADER_MAX_INDEXES ) {
		// draw what is there and go on with a batch of the same shader
		shader_t *shader = tess.shader;
		const int fogNum = tess.fogNum;

		RB_EndSurface();
		RB_BeginSurface( shader, fogNum );
		vk_batchMode = 4;
		VK_AddQuad( kind, a, aw, b, bw, c, st, color, d );
		return;
	}
	const vec4_t rgba = { color[0] / 255.0f, color[1] / 255.0f, color[2] / 255.0f, color[3] / 255.0f };

	VK_QuadRecord( vk_sprites + vk_numSprites * VK_SPRITE_ROWS, kind, a, aw, b, bw, c, st, rgba, d );
	vk_numSprites++;
	VK_QuadIndexes( tess.indexes + tess.numIndexes, first );
	tess.numIndexes += 6;
}

// a sprite as DoSprite() of rd-vanilla has it: turned by rotation degrees to the eye, the texture over all of it
static void VK_AddSprite( const vec3_t origin, float radius, float rotation, const byte *color ) {
	static const float	st[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	const float			angle = (float)M_PI * rotation / 180.0f;
	vec3_t				turn;

	VectorSet( turn, rotation ? cosf( angle ) : 1.0f, rotation ? sinf( angle ) : 0.0f, 0.0f );
	VK_AddQuad( 0, origin, radius, turn, 0.0f, vec3_origin, st, color );
}

// the entities whose surface sprite.vert makes
static qboolean VK_DeviceQuadType( int reType ) {
	return (qboolean)( reType == RT_SPRITE || reType == RT_SABER_GLOW || reType == RT_ORIENTED_QUAD
		|| reType == RT_LINE || reType == RT_ORIENTEDLINE || reType == RT_CYLINDER );
}

// one entity of those as quads, as its surface function of tr_surface.cpp would build it
static void VK_AddEntityQuads( refEntity_t *e ) {
	static const float	whole[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	float				st[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	vec3_t				left, up;

	switch ( e->reType ) {
	case RT_SPRITE:
		VK_AddSprite( e->origin, e->radius, e->rotation, e->shaderRGBA );
		break;
	case RT_SABER_GLOW:
		// the glow along the blade, from its tip down, and the halo of the hilt. The radius grows on the entity
		// as it does in rd-vanilla.
		for ( float i = e->saberLength; i > 0; i -= e->radius * 0.65f ) {
			vec3_t end;

			VectorMA( e->origin, i, e->axis[0], end );
			VK_AddSprite( end, e->radius, 0.0f, e->shaderRGBA );
			e->radius += 0.017f;
		}
		VK_AddSprite( e->origin, 5.5f + Q_flrand( 0.0f, 1.0f ) * 0.25f, 0.0f, e->shaderRGBA );
		break;
	case RT_ORIENTED_QUAD:
		// the axes of the entity, turned in their plane (RB_SurfaceOrientedQuad)
		if ( e->rotation == 0 ) {
			VectorScale( e->axis[1], e->radius, left );
			VectorScale( e->axis[2], e->radius, up );
		} else {
			const float angle = (float)M_PI * e->rotation / 180.0f;
			const float sn = sinf( angle ), cs = cosf( angle );

			VectorScale( e->axis[1], cs * e->radius, left );
			VectorMA( left, -sn * e->radius, e->axis[2], left );
			VectorScale( e->axis[2], cs * e->radius, up );
			VectorMA( up, sn * e->radius, e->axis[1], up );
		}
		if ( backEnd.viewParms.isMirror ) {
			VectorSubtract( vec3_origin, left, left );
		}
		VK_AddQuad( 1, e->origin, 0.0f, left, 0.0f, up, whole, e->shaderRGBA );
		break;
	case RT_LINE:
		VK_AddQuad( 2, e->origin, e->radius, e->oldorigin, e->radius, vec3_origin, whole, e->shaderRGBA );
		break;
	case RT_ORIENTEDLINE:
		// the texture repeats along the line stscale times
		VectorNormalize( e->axis[1] );
		st[3] = e->data.line.stscale;
		VK_AddQuad( 3, e->origin, e->data.line.width * 0.5f, e->oldorigin, e->data.line.width * 0.5f, e->axis[1], st, e->shaderRGBA );
		break;
	case RT_CYLINDER: {
		// origin is the bottom point, oldorigin the top one, radius and rotation the sizes of the two rings. How
		// many sides it has goes with its distance (RB_SurfaceCylinder), the shader turns the rings.
		vec3_t midpoint, vr, vu, ring1, ring2;
		float length;
		int segments;

		VectorAdd( e->origin, e->oldorigin, midpoint );
		VectorScale( midpoint, 0.5f, midpoint );
		VectorSubtract( midpoint, backEnd.viewParms.ori.origin, midpoint );
		length = VectorNormalize( midpoint ) * ( backEnd.viewParms.fovX / 90.0f );
		segments = (int)( 32 * ( 1 - length / 1024 ) );		// NUM_CYLINDER_SEGMENTS
		segments = Com_Clampi( 8, 32, segments );
		MakeNormalVectors( e->axis[0], vr, vu );
		VectorScale( vu, e->radius, ring1 );
		VectorScale( vu, e->rotation, ring2 );
		for ( int i = 0; i < segments; i++ ) {
			const float lower[4] = { ring1[0], ring1[1], ring1[2], 0.0f };

			VK_AddQuad( 4, e->origin, (float)i, e->oldorigin, (float)segments, e->axis[0], lower, e->shaderRGBA, ring2 );
		}
		break;
	}
	default:
		break;
	}
}

/*
===============
VK_AddBoltSegment

DoLine2() of tr_surface.cpp: a quad from start to end, wide along side by startWidth there and by endWidth at the
end, the texture once over it
===============
*/
qboolean VK_AddBoltSegment( const float *start, const float *end, const float *side, float startWidth, float endWidth ) {
	static const float whole[4] = { 0.0f, 0.0f, 1.0f, 1.0f };

	if ( vk_batchMode != 4 ) {
		return qfalse;
	}
	VK_AddQuad( 3, start, startWidth, end, endWidth, side, whole, backEnd.currentEntity->e.shaderRGBA );
	return qtrue;
}

/*
===============
VK_AddDeviceSprites

An entity whose surface is a few quads, or a chain of small ones (RT_ENT_CHAIN, the sparks and bits of the
effects), as records for shaders/sprite.vert. qfalse: it goes through the tess arrays.
===============
*/
static qboolean VK_AddDeviceSprites( surfaceType_t *surface ) {
	refEntity_t *e = &backEnd.currentEntity->e;

	if ( !v_vkSprites->integer ) {
		return qfalse;
	}
	if ( e->reType == RT_ELECTRICITY ) {
		// The bolt and its forks are worked out by RB_SurfaceElectricity(), which hands each segment of them to
		// VK_AddBoltSegment() while the batch takes quads.
		if ( !tess.numIndexes && !tess.numVertexes ) {
			vk_batchMode = VK_ShaderIsStatic( qfalse ) ? 4 : 0;
		}
		if ( vk_batchMode != 4 ) {
			return qfalse;
		}
		rb_surfaceTable[*surface]( surface );
		return qtrue;
	}
	if ( e->reType == RT_ENT_CHAIN ) {
		const int start = e->uRefEnt.uMini.miniStart, count = e->uRefEnt.uMini.miniCount;

		// A small one has no data.line and no saberLength: rd-vanilla takes those from whatever entity was copied
		// into its tempEnt first. Only what needs neither comes here.
		for ( int i = 0; i < count; i++ ) {
			const int reType = backEnd.refdef.miniEntities[start + i].e.reType;

			if ( reType != RT_SPRITE && reType != RT_ORIENTED_QUAD && reType != RT_LINE && reType != RT_CYLINDER ) {
				return qfalse;
			}
		}
	} else if ( !VK_DeviceQuadType( e->reType ) ) {
		return qfalse;
	}
	if ( !tess.numIndexes && !tess.numVertexes ) {
		vk_batchMode = VK_ShaderIsStatic( qfalse ) ? 4 : 0;
	}
	if ( vk_batchMode != 4 ) {
		return qfalse;
	}
	if ( e->reType == RT_ENT_CHAIN ) {
		// a copy each, as RB_SurfaceEntity() makes one: the surface functions change what they draw
		const int start = e->uRefEnt.uMini.miniStart, count = e->uRefEnt.uMini.miniCount;

		for ( int i = 0; i < count; i++ ) {
			refEntity_t mini = {};

			memcpy( &mini, &backEnd.refdef.miniEntities[start + i].e, sizeof( backEnd.refdef.miniEntities[start + i].e ) );
			VK_AddEntityQuads( &mini );
		}
		return qtrue;
	}
	VK_AddEntityQuads( e );
	return qtrue;
}

/*
===============
VK_AddSurface

A surface of the draw list into the batch: its indexes only if the batch can be static, else through the
surface functions of tr_surface.cpp into the tess arrays
===============
*/
void VK_AddSurface( surfaceType_t *surface ) {
	if ( *surface == SF_ENTITY && VK_AddDeviceSprites( surface ) ) {
		return;
	}
	if ( *surface == SF_MDX ) {
		CRenderableSurface *rs = (CRenderableSurface *)surface;
		const mdxmSurface_t *mesh = rs->surfaceData;

		if ( rs->boneCache && mesh && !rs->alternateTex
			&& VK_AddDeviceMesh( rs->boneCache, mesh, qfalse, mesh->numTriangles * 3 ) ) {
			// R_AddGhoulSurfaces() made this object for the frame and RB_SurfaceGhoul() frees it, which is not
			// called on this way. It stays if the glow pass draws the surface a second time.
			if ( R_GhoulSurfaceDone() ) {
				delete rs;
			}
			return;
		}
		// a saber mark on a surface (gore)
		if ( rs->boneCache && mesh && rs->alternateTex ) {
			if ( VK_AddDeviceGore( rs ) ) {
				return;
			}
			// Through the tess arrays: RB_SurfaceGhoul() copies the vertices of the mark that the surface under
			// it left there when it was moved by its bones on the CPU. That surface may have come from the
			// device, so they are worked out for this mark now.
			G2_SkinGore( rs );
		}
	} else if ( *surface == SF_MD3 ) {
		const md3Surface_t *mesh = (const md3Surface_t *)surface;

		// a model with one frame is a mesh that does not move. With more frames it is blended on the CPU.
		if ( mesh->numFrames == 1
			&& VK_AddDeviceMesh( NULL, mesh, qtrue, mesh->numTriangles * 3 ) ) {
			return;
		}
	}
	if ( ( *surface == SF_FACE || *surface == SF_TRIANGLES || *surface == SF_GRID ) && tr.world
		&& !( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		if ( !vk_worldValid ) {
			VK_BuildWorld();
		}
		if ( !tess.numIndexes && !tess.numVertexes ) {
			vk_batchMode = VK_ShaderIsStatic( qtrue ) ? 1 : 0;
		}
		if ( vk_batchMode == 1 ) {
			const auto found = vk_firstVertex.find( surface );

			if ( found != vk_firstVertex.end() ) {
				const int first = found->second;
				glIndex_t *out;

				if ( vk_stampSurfaces.count( surface ) ) {
					// autosprite: every quad of four vertices is drawn as RB_AddQuadStamp() makes it
					const int numQuads = ( *surface == SF_FACE ) ? ( (srfSurfaceFace_t *)surface )->numPoints / 4
						: ( (srfTriangles_t *)surface )->numVerts / 4;

					RB_CHECKOVERFLOW( 0, numQuads * 6 );
					vk_batchMode = 1;
					out = tess.indexes + tess.numIndexes;
					for ( int q = 0; q < numQuads; q++, out += 6 ) {
						const int v = first + q * 4;

						out[0] = v;
						out[1] = v + 1;
						out[2] = v + 3;
						out[3] = v + 3;
						out[4] = v + 1;
						out[5] = v + 2;
					}
					tess.numIndexes += numQuads * 6;
					tess.dlightBits |= ( *surface == SF_FACE ) ? ( (srfSurfaceFace_t *)surface )->dlightBits
						: ( (srfTriangles_t *)surface )->dlightBits;
				} else if ( *surface == SF_FACE ) {
					srfSurfaceFace_t *face = (srfSurfaceFace_t *)surface;
					const unsigned int *indices = (const unsigned int *)( (const char *)face + face->ofsIndices );

					RB_CHECKOVERFLOW( 0, face->numIndices );
					vk_batchMode = 1;		// an overflow began a new batch
					out = tess.indexes + tess.numIndexes;
					for ( int i = 0; i < face->numIndices; i++ ) {
						out[i] = indices[i] + first;
					}
					tess.numIndexes += face->numIndices;
					tess.dlightBits |= face->dlightBits;
				} else if ( *surface == SF_GRID ) {
					srfGridMesh_t *grid = (srfGridMesh_t *)surface;

					// two triangles per cell, in the order RB_SurfaceGrid() has them
					for ( int row = 0; row < grid->height - 1; row++ ) {
						RB_CHECKOVERFLOW( 0, ( grid->width - 1 ) * 6 );
						vk_batchMode = 1;
						out = tess.indexes + tess.numIndexes;
						for ( int col = 0; col < grid->width - 1; col++, out += 6 ) {
							const int v1 = first + row * grid->width + col + 1;
							const int v2 = v1 - 1;
							const int v3 = v2 + grid->width;
							const int v4 = v3 + 1;

							out[0] = v2;
							out[1] = v3;
							out[2] = v1;
							out[3] = v1;
							out[4] = v3;
							out[5] = v4;
						}
						tess.numIndexes += ( grid->width - 1 ) * 6;
					}
					tess.dlightBits |= grid->dlightBits;
				} else {
					srfTriangles_t *tri = (srfTriangles_t *)surface;

					RB_CHECKOVERFLOW( 0, tri->numIndexes );
					vk_batchMode = 1;
					out = tess.indexes + tess.numIndexes;
					for ( int i = 0; i < tri->numIndexes; i++ ) {
						out[i] = tri->indexes[i] + first;
					}
					tess.numIndexes += tri->numIndexes;
					tess.dlightBits |= tri->dlightBits;
				}
				// its surface sprites are drawn with the batch (VK_BindGrass)
				const auto grass = vk_grassSurfaces.find( surface );

				if ( grass != vk_grassSurfaces.end() && vk_numBatchGrass < VK_MAX_BATCH_GRASS ) {
					vk_batchGrass[vk_numBatchGrass++] = &grass->second;
				}
				return;
			}
		}
	}
	if ( vk_batchMode && tess.numIndexes ) {
		// a surface that needs the tess arrays in a batch that has none: draw what is there and go on
		shader_t *shader = tess.shader;
		const int fogNum = tess.fogNum;

		RB_EndSurface();
		RB_BeginSurface( shader, fogNum );
	}
	vk_batchMode = 0;
	rb_surfaceTable[*surface]( surface );
}

// the quads of the batch for sprite.vert
static qboolean VK_UploadSpriteBatch( uint32_t *bones ) {
	if ( !VK_QuadView( vk_sprites, vk_numSprites, bones ) ) {
		return qfalse;
	}
	return VK_UploadIndexes( NULL, NULL, 0, tess.indexes, tess.numIndexes );
}

/*
===============
VK_UploadStaticBatch

The indexes of a static batch into the frame, the vertices are the ones of the level or of the models.
bones: for a batch of a model, where its skeleton is in the frame
===============
*/
qboolean VK_UploadStaticBatch( uint32_t *bones ) {
	if ( vk_batchMode == 4 ) {
		return VK_UploadSpriteBatch( bones );
	}
	if ( vk_batchMode == 2 ) {
		return VK_UploadSkinnedBatch( vk_batchBones, bones );
	}
	if ( vk_batchMode == 3 ) {
		return VK_UploadGoreBatch( vk_batchBones, bones );
	}
	return VK_UploadIndexes( &vk_worldVertexes, vk_worldOffsets, 4, tess.indexes, tess.numIndexes );
}
