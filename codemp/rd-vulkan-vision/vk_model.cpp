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

// vk_model.cpp: Ghoul2 models skinned on the device, and MD3 models with one frame
//
// rd-vanilla moves every vertex of a player model by its bones on the CPU each frame (RB_SurfaceGhoul). Here
// the vertices of a Ghoul2 surface go into a buffer on the device the first time the surface is drawn, as they
// are in the model file, with the numbers of their bones in the skeleton and their weights. Each frame only the
// bones of a skeleton are copied into the frame, shaders/skinned.vert does the rest. The bones themselves still
// come from the bone cache of tr_ghoul2.cpp, so animation, bolts and collision are what they always were.
//
// Surfaces with gore on them keep the old way: the gore needs the moved vertices on the CPU.
//
// An MD3 model with one frame (items, weapons, most models of a level) goes the same way: its vertices are put
// into the same buffer with one bone that leaves them where they are. MD3 models with more frames are still
// blended on the CPU.

#include "vk_local.h"

#include <unordered_map>

static std::unordered_map<const void *, int>	vk_modelFirstIndex;		// of an mdxmSurface_t or md3Surface_t, -1: not on the device
static vkBuffer_t		vk_modelVertexes;			// vkSkinVertex_t, filled from the front
static VkDeviceSize		vk_modelUsed;
static vkBuffer_t		vk_modelIndexes;			// the triangles of those surfaces, as indexes into the whole buffer
static VkDeviceSize		vk_modelIndexUsed;

// the surfaces of the batch that is collected, as runs in the index buffer
static vkIndexRange_t	vk_batchRanges[VK_MAX_BATCH_RANGES];
static int				vk_numBatchRanges;

// the skeletons whose bones are in the frame already
#define VK_MAX_FRAME_SKELETONS	256
static struct {
	const CBoneCache	*cache;
	uint32_t			base;
} vk_frameSkeletons[VK_MAX_FRAME_SKELETONS];
static int				vk_numFrameSkeletons;
static int				vk_identityBone;			// where the bone that does nothing is in the frame, -1: not yet

void VK_InitModels( void ) {
	VK_CreateBuffer( &vk_modelVertexes, VK_MODEL_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, qfalse );
	VK_CreateBuffer( &vk_modelIndexes, VK_MODEL_INDEX_BYTES, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, qfalse );
	vk_modelUsed = vk_modelIndexUsed = 0;
}

void VK_ShutdownModels( void ) {
	VK_DestroyBuffer( &vk_modelVertexes );
	VK_DestroyBuffer( &vk_modelIndexes );
	vk_modelFirstIndex.clear();
	vk_modelUsed = vk_modelIndexUsed = 0;
}

/*
===============
VK_ModelsChanged

R_Init() ran: models may have been freed and others loaded to the same place, so nothing that is known about
a surface holds any more
===============
*/
void VK_ModelsChanged( void ) {
	if ( vk.device ) {
		qvkDeviceWaitIdle( vk.device );
	}
	vk_modelFirstIndex.clear();
	vk_modelUsed = vk_modelIndexUsed = 0;
}

void VK_ModelsBeginFrame( void ) {
	vk_numFrameSkeletons = 0;
	vk_identityBone = -1;
}

// the weight of one of the first bones of a vertex: ten bits, eight of them in BoneWeightings (the function of
// this name in tr_ghoul2.cpp is static to that file)
static float VK_BoneWeight( const mdxmVertex_t *v, int weightNum ) {
	int weight = v->BoneWeightings[weightNum];

	weight |= ( v->uiNmWeightsAndBoneIndexes >> ( iG2_BONEWEIGHT_TOPBITS_SHIFT + ( weightNum * 2 ) ) ) & iG2_BONEWEIGHT_TOPBITS_AND;
	return fG2_BONEWEIGHT_RECIPROCAL_MULT * weight;
}

// The triangles of a surface whose vertices begin at firstVertex of the vertex buffer. They are stored as indexes
// into the whole buffer, so a batch only has to say which run of them it wants. Returns where they begin.
static int VK_ModelIndexes( const int *triangles, int numIndexes, int firstVertex ) {
	const VkDeviceSize size = (VkDeviceSize)numIndexes * sizeof( uint32_t );
	const int first = (int)( vk_modelIndexUsed / sizeof( uint32_t ) );
	VkDeviceSize staged;
	VkBufferCopy region;
	uint32_t *out = (uint32_t *)VK_Stage( size, &staged );

	for ( int i = 0; i < numIndexes; i++ ) {
		out[i] = (uint32_t)( triangles[i] + firstVertex );
	}
	region.srcOffset = staged;
	region.dstOffset = vk_modelIndexUsed;
	region.size = size;
	qvkCmdCopyBuffer( VK_UploadCmd(), vk.staging.buffer, vk_modelIndexes.buffer, 1, &region );
	vk_modelIndexUsed += size;
	return first;
}

/*
===============
VK_SkinnedSurface

Where the triangles of the surface begin in the index buffer of the models. Its vertices and triangles are put
into the buffers now if this is the first time.
-1: it can not go there (no room, or more bones than a byte can number).
===============
*/
int VK_SkinnedSurface( const mdxmSurface_t *surface ) {
	const auto found = vk_modelFirstIndex.find( surface );
	const VkDeviceSize size = (VkDeviceSize)surface->numVerts * sizeof( vkSkinVertex_t );
	const mdxmVertex_t *v = (const mdxmVertex_t *)( (const byte *)surface + surface->ofsVerts );
	const mdxmVertexTexCoord_t *texCoords = (const mdxmVertexTexCoord_t *)&v[surface->numVerts];
	const int *boneReferences = (const int *)( (const byte *)surface + surface->ofsBoneReferences );
	VkDeviceSize staged;
	VkBufferCopy region;
	vkSkinVertex_t *out;
	int first;

	if ( found != vk_modelFirstIndex.end() ) {
		return found->second;
	}
	if ( !vk_modelVertexes.buffer || vk_modelUsed + size > VK_MODEL_BYTES
		|| vk_modelIndexUsed + (VkDeviceSize)surface->numTriangles * 3 * sizeof( uint32_t ) > VK_MODEL_INDEX_BYTES ) {
		return -1;
	}
	for ( int i = 0; i < surface->numBoneReferences; i++ ) {
		if ( boneReferences[i] < 0 || boneReferences[i] > 255 ) {
			vk_modelFirstIndex[surface] = -1;
			return -1;
		}
	}

	out = (vkSkinVertex_t *)VK_Stage( size, &staged );
	memset( out, 0, (size_t)size );
	for ( int i = 0; i < surface->numVerts; i++, v++, out++ ) {
		const int numWeights = G2_GetVertWeights( v );
		float total = 0.0f;

		VectorCopy( v->vertCoords, out->xyz );
		VectorCopy( v->normal, out->normal );
		out->st[0] = texCoords[i].texCoords[0];
		out->st[1] = texCoords[i].texCoords[1];
		// the last weight is what the others leave
		for ( int k = 0; k < numWeights; k++ ) {
			const float weight = ( k < numWeights - 1 ) ? VK_BoneWeight( v, k ) : 1.0f - total;

			total += weight;
			out->bones[k] = (byte)boneReferences[G2_GetVertBoneIndex( v, k )];
			out->weights[k] = (uint16_t)( Com_Clamp( 0.0f, 1.0f, weight ) * 65535.0f + 0.5f );
		}
	}

	region.srcOffset = staged;
	region.dstOffset = vk_modelUsed;
	region.size = size;
	qvkCmdCopyBuffer( VK_UploadCmd(), vk.staging.buffer, vk_modelVertexes.buffer, 1, &region );
	first = VK_ModelIndexes( (const int *)( (const byte *)surface + surface->ofsTriangles ), surface->numTriangles * 3,
		(int)( vk_modelUsed / sizeof( vkSkinVertex_t ) ) );
	vk_modelUsed += size;
	vk_modelFirstIndex[surface] = first;
	return first;
}

/*
===============
VK_StaticMeshSurface

The same for a surface of an MD3 model with one frame: its vertices as LerpMeshVertexes() of rd-vanilla
decodes them, hung on one bone
===============
*/
int VK_StaticMeshSurface( const md3Surface_t *surface ) {
	const auto found = vk_modelFirstIndex.find( surface );
	const VkDeviceSize size = (VkDeviceSize)surface->numVerts * sizeof( vkSkinVertex_t );
	const short *xyz = (const short *)( (const byte *)surface + surface->ofsXyzNormals );
	const float *st = (const float *)( (const byte *)surface + surface->ofsSt );
	VkDeviceSize staged;
	VkBufferCopy region;
	vkSkinVertex_t *out;
	int first;

	if ( found != vk_modelFirstIndex.end() ) {
		return found->second;
	}
	if ( !vk_modelVertexes.buffer || vk_modelUsed + size > VK_MODEL_BYTES
		|| vk_modelIndexUsed + (VkDeviceSize)surface->numTriangles * 3 * sizeof( uint32_t ) > VK_MODEL_INDEX_BYTES ) {
		return -1;
	}
	out = (vkSkinVertex_t *)VK_Stage( size, &staged );
	memset( out, 0, (size_t)size );
	for ( int i = 0; i < surface->numVerts; i++, xyz += 4, out++ ) {
		// the normal is two angles in a short
		const unsigned lat = ( ( xyz[3] >> 8 ) & 0xff ) * ( FUNCTABLE_SIZE / 256 );
		const unsigned lng = ( xyz[3] & 0xff ) * ( FUNCTABLE_SIZE / 256 );

		VectorSet( out->xyz, xyz[0] * MD3_XYZ_SCALE, xyz[1] * MD3_XYZ_SCALE, xyz[2] * MD3_XYZ_SCALE );
		out->normal[0] = tr.sinTable[( lat + ( FUNCTABLE_SIZE / 4 ) ) & FUNCTABLE_MASK] * tr.sinTable[lng];
		out->normal[1] = tr.sinTable[lat] * tr.sinTable[lng];
		out->normal[2] = tr.sinTable[( lng + ( FUNCTABLE_SIZE / 4 ) ) & FUNCTABLE_MASK];
		out->st[0] = st[i * 2];
		out->st[1] = st[i * 2 + 1];
		out->weights[0] = 65535;
	}

	region.srcOffset = staged;
	region.dstOffset = vk_modelUsed;
	region.size = size;
	qvkCmdCopyBuffer( VK_UploadCmd(), vk.staging.buffer, vk_modelVertexes.buffer, 1, &region );
	first = VK_ModelIndexes( (const int *)( (const byte *)surface + surface->ofsTriangles ), surface->numTriangles * 3,
		(int)( vk_modelUsed / sizeof( vkSkinVertex_t ) ) );
	vk_modelUsed += size;
	vk_modelFirstIndex[surface] = first;
	return first;
}

/*
===============
VK_ModelBatchBegin, VK_ModelBatchAdd

The surfaces of a batch of model surfaces as runs in the index buffer. Surfaces that were put into the buffer one
after the other, as the surfaces of a model are, make one run. qfalse: the batch can not take another run.
===============
*/
void VK_ModelBatchBegin( void ) {
	vk_numBatchRanges = 0;
}

qboolean VK_ModelBatchAdd( int firstIndex, int numIndexes ) {
	vkIndexRange_t *last = vk_numBatchRanges ? &vk_batchRanges[vk_numBatchRanges - 1] : NULL;

	if ( last && last->first + last->count == (uint32_t)firstIndex ) {
		last->count += (uint32_t)numIndexes;
		return qtrue;
	}
	if ( vk_numBatchRanges == VK_MAX_BATCH_RANGES ) {
		return qfalse;
	}
	vk_batchRanges[vk_numBatchRanges].first = (uint32_t)firstIndex;
	vk_batchRanges[vk_numBatchRanges].count = (uint32_t)numIndexes;
	vk_numBatchRanges++;
	return qtrue;
}

// the buffers of the models and the runs of the batch are what the next draws use
static qboolean VK_BindModelBatch( void ) {
	const VkDeviceSize offset = 0;

	if ( !vk_numBatchRanges ) {
		return qfalse;
	}
	VK_BindVertexBuffer( vk_modelVertexes.buffer, &offset, 1 );
	VK_BindIndexBuffer( vk_modelIndexes.buffer );
	vk.ranges = vk_batchRanges;
	vk.numRanges = vk_numBatchRanges;
	vk.statStaticIndexes[0] += tess.numIndexes;
	return qtrue;
}

/*
===============
VK_UploadSkinnedBatch

A batch of Ghoul2 surfaces: its runs of indexes become what is drawn, and the bones of its skeleton go into the
frame if they are not there yet. boneCache NULL: meshes that do not move, they get one bone that leaves them where they are.
bones: where the skeleton starts, counted in rows of the bone buffer.
===============
*/
qboolean VK_UploadSkinnedBatch( CBoneCache *boneCache, uint32_t *bones ) {
	vkFrame_t *frame = vk.frame;
	int i;

	if ( !frame ) {
		return qfalse;
	}
	if ( !boneCache ) {
		if ( vk_identityBone < 0 ) {
			static const float identity[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };

			if ( frame->numBones + 1 > VK_MAX_BONES ) {
				return qfalse;
			}
			memcpy( frame->bones.mapped + sizeof( identity ) * frame->numBones, identity, sizeof( identity ) );
			vk_identityBone = frame->numBones * 3;
			frame->numBones++;
		}
		*bones = (uint32_t)vk_identityBone;
		return VK_BindModelBatch();
	}
	for ( i = 0; i < vk_numFrameSkeletons; i++ ) {
		if ( vk_frameSkeletons[i].cache == boneCache ) {
			break;
		}
	}
	if ( i == vk_numFrameSkeletons ) {
		// a skeleton has at most 256 bones, see VK_SkinnedSurface()
		if ( frame->numBones + 256 > VK_MAX_BONES ) {
			return qfalse;
		}
		const int numBones = G2_BoneMatrices( boneCache, (float *)( frame->bones.mapped + sizeof( float ) * 12 * frame->numBones ), 256 );

		if ( vk_numFrameSkeletons < VK_MAX_FRAME_SKELETONS ) {
			vk_frameSkeletons[vk_numFrameSkeletons].cache = boneCache;
			vk_frameSkeletons[vk_numFrameSkeletons].base = (uint32_t)frame->numBones * 3;
			vk_numFrameSkeletons++;
		}
		*bones = (uint32_t)frame->numBones * 3;
		frame->numBones += numBones;
	} else {
		*bones = vk_frameSkeletons[i].base;
	}
	return VK_BindModelBatch();
}
