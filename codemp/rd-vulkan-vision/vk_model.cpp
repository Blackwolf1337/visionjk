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

// vk_model.cpp: Ghoul2 models skinned on the device
//
// rd-vanilla moves every vertex of a player model by its bones on the CPU each frame (RB_SurfaceGhoul). Here
// the vertices of a Ghoul2 surface go into a buffer on the device the first time the surface is drawn, as they
// are in the model file, with the numbers of their bones in the skeleton and their weights. Each frame only the
// bones of a skeleton are copied into the frame, shaders/skinned.vert does the rest. The bones themselves still
// come from the bone cache of tr_ghoul2.cpp, so animation, bolts and collision are what they always were.
//
// Surfaces with gore on them keep the old way: the gore needs the moved vertices on the CPU.

#include "vk_local.h"

#include <unordered_map>

static std::unordered_map<const mdxmSurface_t *, int>	vk_skinFirstVertex;
static vkBuffer_t		vk_modelVertexes;			// vkSkinVertex_t, filled from the front
static VkDeviceSize		vk_modelUsed;

// the skeletons whose bones are in the frame already
#define VK_MAX_FRAME_SKELETONS	256
static struct {
	const CBoneCache	*cache;
	uint32_t			base;
} vk_frameSkeletons[VK_MAX_FRAME_SKELETONS];
static int				vk_numFrameSkeletons;

void VK_InitModels( void ) {
	VK_CreateBuffer( &vk_modelVertexes, VK_MODEL_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, qfalse );
	vk_modelUsed = 0;
}

void VK_ShutdownModels( void ) {
	VK_DestroyBuffer( &vk_modelVertexes );
	vk_skinFirstVertex.clear();
	vk_modelUsed = 0;
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
		vkDeviceWaitIdle( vk.device );
	}
	vk_skinFirstVertex.clear();
	vk_modelUsed = 0;
}

void VK_ModelsBeginFrame( void ) {
	vk_numFrameSkeletons = 0;
}

// the weight of one of the first bones of a vertex: ten bits, eight of them in BoneWeightings (the function of
// this name in tr_ghoul2.cpp is static to that file)
static float VK_BoneWeight( const mdxmVertex_t *v, int weightNum ) {
	int weight = v->BoneWeightings[weightNum];

	weight |= ( v->uiNmWeightsAndBoneIndexes >> ( iG2_BONEWEIGHT_TOPBITS_SHIFT + ( weightNum * 2 ) ) ) & iG2_BONEWEIGHT_TOPBITS_AND;
	return fG2_BONEWEIGHT_RECIPROCAL_MULT * weight;
}

/*
===============
VK_SkinnedSurface

The first vertex of the surface in the buffer of the models, put there now if this is the first time.
-1: it can not go there (no room, or more bones than a byte can number).
===============
*/
int VK_SkinnedSurface( const mdxmSurface_t *surface ) {
	const auto found = vk_skinFirstVertex.find( surface );
	const VkDeviceSize size = (VkDeviceSize)surface->numVerts * sizeof( vkSkinVertex_t );
	const mdxmVertex_t *v = (const mdxmVertex_t *)( (const byte *)surface + surface->ofsVerts );
	const mdxmVertexTexCoord_t *texCoords = (const mdxmVertexTexCoord_t *)&v[surface->numVerts];
	const int *boneReferences = (const int *)( (const byte *)surface + surface->ofsBoneReferences );
	VkDeviceSize staged;
	VkBufferCopy region;
	vkSkinVertex_t *out;
	int first;

	if ( found != vk_skinFirstVertex.end() ) {
		return found->second;
	}
	if ( !vk_modelVertexes.buffer || vk_modelUsed + size > VK_MODEL_BYTES ) {
		return -1;
	}
	for ( int i = 0; i < surface->numBoneReferences; i++ ) {
		if ( boneReferences[i] < 0 || boneReferences[i] > 255 ) {
			vk_skinFirstVertex[surface] = -1;
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
	vkCmdCopyBuffer( VK_UploadCmd(), vk.staging.buffer, vk_modelVertexes.buffer, 1, &region );
	first = (int)( vk_modelUsed / sizeof( vkSkinVertex_t ) );
	vk_modelUsed += size;
	vk_skinFirstVertex[surface] = first;
	return first;
}

/*
===============
VK_UploadSkinnedBatch

The indexes of a batch of Ghoul2 surfaces into the frame, and the bones of their skeleton if they are not there
yet. bones: where the skeleton starts, counted in rows of the bone buffer.
===============
*/
qboolean VK_UploadSkinnedBatch( CBoneCache *boneCache, uint32_t *bones ) {
	vkFrame_t *frame = vk.frame;
	const VkDeviceSize offset = 0;
	int i;

	if ( !frame || !boneCache ) {
		return qfalse;
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
	return VK_UploadIndexes( vk_modelVertexes.buffer, &offset, 1, tess.indexes, tess.numIndexes );
}
