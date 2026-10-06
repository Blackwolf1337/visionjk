/*
===========================================================================
Copyright (C) 2026, OpenJK contributors

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
// as before: sky, deformVertexes, surface sprites, color generators that are not in the vertex shader, vertex
// lit surfaces with more than one light style.
//
// Patches are drawn with all the rows and columns they were subdivided into. rd-vanilla leaves rows and columns
// out with the distance (r_lodCurveError), so a patch far away is a little rounder here than there.

#include "vk_local.h"

#include <unordered_map>

static std::unordered_map<const void *, int>	vk_firstVertex;		// of a surface in the buffer
static vkBuffer_t		vk_worldVertexes;
static VkDeviceSize		vk_worldOffsets[4];			// where xyz, normal, texCoords and colors start in it
static qboolean			vk_worldValid;
// the batch in tess: 0: vertices and indexes in the tess arrays, 1: indexes into the buffer of the level,
// 2: indexes into the buffer of the models (vk_model.cpp), all of one skeleton (NULL: MD3 meshes with one frame)
static int				vk_batchMode;

cvar_t	*r_vkStatic;		// 0: nothing from buffers on the device, every vertex through the tess arrays as in rd-vanilla
static CBoneCache		*vk_batchBones;

qboolean VK_BatchIsStatic( void ) {
	return (qboolean)( vk_batchMode != 0 );
}

qboolean VK_BatchIsSkinned( void ) {
	return (qboolean)( vk_batchMode == 2 );
}

// RB_BeginSurface(): the first surface of the batch decides again
void VK_BatchBegin( void ) {
	vk_batchMode = 0;
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
	vk_firstVertex.clear();
	vk_worldValid = qfalse;
}

static void VK_BuildWorld( void ) {
	const world_t *world = tr.world;
	int numVertexes = 0, vertex = 0;
	VkDeviceSize sizes[4], total = 0;
	vkBuffer_t staging;
	VkBufferCopy region;

	vkDeviceWaitIdle( vk.device );
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
	sizes[0] = sizes[1] = (VkDeviceSize)numVertexes * sizeof( vec4_t );
	sizes[2] = (VkDeviceSize)numVertexes * sizeof( vec2_t ) * NUM_TEX_COORDS;
	sizes[3] = (VkDeviceSize)numVertexes * 4;
	for ( int i = 0; i < 4; i++ ) {
		vk_worldOffsets[i] = total;
		total += ( sizes[i] + 15 ) & ~(VkDeviceSize)15;
	}
	VK_CreateBuffer( &staging, total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, qtrue );
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
				// the colors of the first light style, see VK_StaticColorScale() for the rest
				memcpy( colors[vertex], &v[VERTEX_COLOR], 4 );
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
			}
		}
	}

	VK_CreateBuffer( &vk_worldVertexes, total, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, qfalse );
	region.srcOffset = region.dstOffset = 0;
	region.size = total;
	vkCmdCopyBuffer( VK_UploadCmd(), staging.buffer, vk_worldVertexes.buffer, 1, &region );
	VK_FlushUploads();
	VK_DestroyBuffer( &staging );
	ri.Printf( PRINT_DEVELOPER, "rd-vulkan-vision: %i vertices of %i surfaces of the level on the device, %i KB\n",
		numVertexes, (int)vk_firstVertex.size(), (int)( total >> 10 ) );
}

/*
===============
VK_ShaderIsStatic

Whether the stages of the shader of the batch can be drawn without the vertices on the CPU
===============
*/
static qboolean VK_ShaderIsStatic( void ) {
	const shader_t *shader = tess.shader;

	if ( !r_vkStatic->integer ) {
		return qfalse;
	}
	if ( shader->sky || shader->numDeforms || r_fullbright->integer || tess.fading ) {
		return qfalse;
	}
	// rd-vanilla adds up the colors of all light styles of a vertex lit surface, the buffer has the first
	if ( shader->lightmapIndex[0] == LIGHTMAP_BY_VERTEX && shader->styles[0] < LS_UNUSED && shader->styles[1] < LS_UNUSED ) {
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
		if ( pStage->ss || pStage->alphaGen == AGEN_BLEND ) {
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

/*
===============
VK_AddDeviceMesh

A surface of a model whose vertices are in the buffer of the models (vk_model.cpp) into the batch: a Ghoul2
surface with the bones of its skeleton, or an MD3 surface with one frame (bones NULL: it stays where it is).
qfalse: it has to take the way through the tess arrays.
===============
*/
static qboolean VK_AddDeviceMesh( CBoneCache *bones, const void *mesh, qboolean md3, const int *triangles, int numIndexes ) {
	glIndex_t *out;
	int first;

	if ( !tess.numIndexes && !tess.numVertexes ) {
		vk_batchMode = VK_ShaderIsStatic() ? 2 : 0;
		vk_batchBones = bones;
	} else if ( vk_batchMode == 2 && vk_batchBones != bones ) {
		// a batch has the bones of one skeleton
		shader_t *shader = tess.shader;
		const int fogNum = tess.fogNum;

		RB_EndSurface();
		RB_BeginSurface( shader, fogNum );
		vk_batchMode = 2;
		vk_batchBones = bones;
	}
	if ( vk_batchMode != 2 ) {
		return qfalse;
	}
	first = md3 ? VK_StaticMeshSurface( (const md3Surface_t *)mesh ) : VK_SkinnedSurface( (const mdxmSurface_t *)mesh );
	if ( first < 0 ) {
		return qfalse;
	}
	RB_CHECKOVERFLOW( 0, numIndexes );
	vk_batchMode = 2;		// an overflow began a new batch
	vk_batchBones = bones;
	out = tess.indexes + tess.numIndexes;
	for ( int i = 0; i < numIndexes; i++ ) {
		out[i] = triangles[i] + first;
	}
	tess.numIndexes += numIndexes;
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
	if ( *surface == SF_MDX ) {
		CRenderableSurface *rs = (CRenderableSurface *)surface;
		const mdxmSurface_t *mesh = rs->surfaceData;

		// gore on a model needs its vertices on the CPU
		if ( rs->boneCache && mesh && !rs->alternateTex && !rs->goreChain
			&& VK_AddDeviceMesh( rs->boneCache, mesh, qfalse, (const int *)( (const byte *)mesh + mesh->ofsTriangles ), mesh->numTriangles * 3 ) ) {
			return;
		}
	} else if ( *surface == SF_MD3 ) {
		const md3Surface_t *mesh = (const md3Surface_t *)surface;

		// a model with one frame is a mesh that does not move. With more frames it is blended on the CPU.
		if ( mesh->numFrames == 1
			&& VK_AddDeviceMesh( NULL, mesh, qtrue, (const int *)( (const byte *)mesh + mesh->ofsTriangles ), mesh->numTriangles * 3 ) ) {
			return;
		}
	}
	if ( ( *surface == SF_FACE || *surface == SF_TRIANGLES || *surface == SF_GRID ) && tr.world
		&& !( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		if ( !vk_worldValid ) {
			VK_BuildWorld();
		}
		if ( !tess.numIndexes && !tess.numVertexes ) {
			vk_batchMode = VK_ShaderIsStatic() ? 1 : 0;
		}
		if ( vk_batchMode == 1 ) {
			const auto found = vk_firstVertex.find( surface );

			if ( found != vk_firstVertex.end() ) {
				const int first = found->second;
				glIndex_t *out;

				if ( *surface == SF_FACE ) {
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

/*
===============
VK_UploadStaticBatch

The indexes of a static batch into the frame, the vertices are the ones of the level or of the models.
bones: for a batch of a model, where its skeleton is in the frame
===============
*/
qboolean VK_UploadStaticBatch( uint32_t *bones ) {
	if ( vk_batchMode == 2 ) {
		return VK_UploadSkinnedBatch( vk_batchBones, bones );
	}
	return VK_UploadIndexes( vk_worldVertexes.buffer, vk_worldOffsets, 4, tess.indexes, tess.numIndexes );
}
