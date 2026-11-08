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

// vk_model.cpp: Ghoul2 models skinned on the device, and MD3 models with one frame
//
// rd-vanilla moves every vertex of a player model by its bones on the CPU each frame (RB_SurfaceGhoul). Here
// the vertices of a Ghoul2 surface go into a buffer on the device the first time the surface is drawn, as they
// are in the model file, with the numbers of their bones in the skeleton and their weights. Each frame only the
// bones of a skeleton are copied into the frame, shaders/skinned.vert does the rest. The bones themselves still
// come from the bone cache of tr_ghoul2.cpp, so animation, bolts and collision are what they always were.
//
// A saber mark on a model (gore) is a few triangles of the surface it lies on with texture coordinates of its
// own. rd-vanilla takes their vertices from the surface each frame when it has moved it by its bones. Here the
// vertices of a mark go into a buffer of the marks the first time it is drawn, with the bones and weights they
// have in the surface, and are moved by the same vertex shader. Only a mark that grows or fades is still made
// on the CPU (G2_SkinGore in tr_ghoul2.cpp).
//
// An MD3 model with one frame (items, weapons, most models of a level) goes the same way: its vertices are put
// into the same buffer with one bone that leaves them where they are. MD3 models with more frames are still
// blended on the CPU.

#include "vk_local.h"

#include <unordered_map>

static std::unordered_map<const void *, int>	vk_modelFirstIndex;		// of an mdxmSurface_t or md3Surface_t, -1: not on the device
static std::unordered_map<const void *, int>	vk_modelFirstVertex;	// of an mdxmSurface_t on the device
static vkBuffer_t		vk_modelVertexes;			// vkSkinVertex_t, filled from the front
static vkSize_t			vk_modelUsed;
static vkBuffer_t		vk_modelIndexes;			// the triangles of those surfaces, as indexes into the whole buffer
static vkSize_t			vk_modelIndexUsed;

// Marks come and go all the time, so their vertices have two buffers: when the one in use is nearly full the
// other one is begun, and every mark that is still drawn goes into that one again. A frame the device still
// works on may read the old one, which is not written to before the buffers change places the next time.
#define VK_GORE_BYTES	( 4 << 20 )
// by the data of a mark (CRenderableSurface::alternateTex): where its vertices begin in the buffer in use. On
// the heap and never freed: the marks tell when their data goes (VK_GoreFreed), also when the library is
// unloaded and things of this file may be gone already.
static std::unordered_map<const void *, int>	*vk_goreFirstVertex;
static vkBuffer_t		vk_goreVertexes[2];			// vkSkinVertex_t, filled from the front
static int				vk_goreBuffer;				// the one in use
static vkSize_t			vk_goreUsed;
static int				vk_goreFrames;				// frames since the two changed places

// the surfaces of the batch that is collected, as runs in the index buffer
static vkIndexRange_t	vk_batchRanges[VK_MAX_BATCH_RANGES];
static int				vk_numBatchRanges;

// the skeletons whose bones are in the frame already
#define VK_MAX_FRAME_SKELETONS	256
static struct {
	const CBoneCache	*cache;
	uint32_t			base;
	uint32_t			cut;		// rows of the cut of a part of a cut body (G2_VJK_CutData), 0: none
} vk_frameSkeletons[VK_MAX_FRAME_SKELETONS];

const float *G2_VJK_CutData( CBoneCache *boneCache, int *numBones );
static int				vk_numFrameSkeletons;
static int				vk_identityBone;			// where the bone that does nothing is in the frame, -1: not yet

// the bone the next skeleton of the frame starts at: a device may want that to be a multiple of VK_BONE_ALIGN
static int VK_SkeletonStart( const vkFrame_t *frame ) {
	return ( frame->numBones + VK_BONE_ALIGN - 1 ) / VK_BONE_ALIGN * VK_BONE_ALIGN;
}

void VK_InitModels( void ) {
	VK_CreateBuffer( &vk_modelVertexes, VK_MODEL_BYTES, VKB_MODELS, qfalse );
	VK_CreateBuffer( &vk_modelIndexes, VK_MODEL_INDEX_BYTES, VKB_INDEXES, qfalse );
	vk_modelUsed = vk_modelIndexUsed = 0;
	for ( int i = 0; i < 2; i++ ) {
		VK_CreateBuffer( &vk_goreVertexes[i], VK_GORE_BYTES, VKB_VERTEXES, qfalse );
	}
	if ( !vk_goreFirstVertex ) {
		vk_goreFirstVertex = new std::unordered_map<const void *, int>;
	}
	vk_goreFirstVertex->clear();
	vk_goreUsed = 0;
}

void VK_ShutdownModels( void ) {
	VK_DestroyBuffer( &vk_modelVertexes );
	VK_DestroyBuffer( &vk_modelIndexes );
	vk_modelFirstIndex.clear();
	vk_modelFirstVertex.clear();
	vk_modelUsed = vk_modelIndexUsed = 0;
	VK_DestroyBuffer( &vk_goreVertexes[0] );
	VK_DestroyBuffer( &vk_goreVertexes[1] );
	if ( vk_goreFirstVertex ) {
		vk_goreFirstVertex->clear();
	}
	vk_goreUsed = 0;
}

/*
===============
VK_ModelsChanged

R_Init() ran: models may have been freed and others loaded to the same place, so nothing that is known about
a surface holds any more
===============
*/
void VK_ModelsChanged( void ) {
	VK_WaitIdle();
	vk_modelFirstIndex.clear();
	vk_modelFirstVertex.clear();
	vk_modelUsed = vk_modelIndexUsed = 0;
	if ( vk_goreFirstVertex ) {
		vk_goreFirstVertex->clear();
	}
	vk_goreUsed = 0;
}

void VK_ModelsBeginFrame( void ) {
	vk_numFrameSkeletons = 0;
	vk_identityBone = -1;

	// only here: a mark that was put into the batch of a frame keeps its place until that frame is drawn
	if ( vk_goreUsed > VK_GORE_BYTES / 4 * 3 && vk_goreFrames > VK_FRAMES && vk_goreFirstVertex ) {
		vk_goreBuffer ^= 1;
		vk_goreFirstVertex->clear();
		vk_goreUsed = 0;
		vk_goreFrames = 0;
	}
	vk_goreFrames++;
}

// the weight of one of the first bones of a vertex: ten bits, eight of them in BoneWeightings (the function of
// this name in tr_ghoul2.cpp is static to that file)
static float VK_BoneWeight( const mdxmVertex_t *v, int weightNum ) {
	int weight = v->BoneWeightings[weightNum];

	weight |= ( v->uiNmWeightsAndBoneIndexes >> ( iG2_BONEWEIGHT_TOPBITS_SHIFT + ( weightNum * 2 ) ) ) & iG2_BONEWEIGHT_TOPBITS_AND;
	return fG2_BONEWEIGHT_RECIPROCAL_MULT * weight;
}

// a vertex of a Ghoul2 surface as the vertex shader wants it, without texture coordinates. out is zeroed.
static void VK_SkinVertex( const mdxmVertex_t *v, const int *boneReferences, vkSkinVertex_t *out ) {
	const int numWeights = G2_GetVertWeights( v );
	float total = 0.0f;

	VectorCopy( v->vertCoords, out->xyz );
	VectorCopy( v->normal, out->normal );
	// the last weight is what the others leave
	for ( int k = 0; k < numWeights; k++ ) {
		const float weight = ( k < numWeights - 1 ) ? VK_BoneWeight( v, k ) : 1.0f - total;

		total += weight;
		out->bones[k] = (byte)boneReferences[G2_GetVertBoneIndex( v, k )];
		out->weights[k] = (uint16_t)( Com_Clamp( 0.0f, 1.0f, weight ) * 65535.0f + 0.5f );
	}
}

// The triangles of a surface whose vertices begin at firstVertex of the vertex buffer. They are stored as indexes
// into the whole buffer, so a batch only has to say which run of them it wants. Returns where they begin.
static int VK_ModelIndexes( const int *triangles, int numIndexes, int firstVertex ) {
	const vkSize_t size = (vkSize_t)numIndexes * sizeof( uint32_t );
	const int first = (int)( vk_modelIndexUsed / sizeof( uint32_t ) );
	vkSize_t staged;
	uint32_t *out = (uint32_t *)VK_Stage( size, &staged );

	for ( int i = 0; i < numIndexes; i++ ) {
		out[i] = (uint32_t)( triangles[i] + firstVertex );
	}
	VK_CopyBuffer( &vk.staging, staged, &vk_modelIndexes, vk_modelIndexUsed, size );
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
	const vkSize_t size = (vkSize_t)surface->numVerts * sizeof( vkSkinVertex_t );
	const mdxmVertex_t *v = (const mdxmVertex_t *)( (const byte *)surface + surface->ofsVerts );
	const mdxmVertexTexCoord_t *texCoords = (const mdxmVertexTexCoord_t *)&v[surface->numVerts];
	const int *boneReferences = (const int *)( (const byte *)surface + surface->ofsBoneReferences );
	vkSize_t staged;
	vkSkinVertex_t *out;
	int first;

	if ( found != vk_modelFirstIndex.end() ) {
		return found->second;
	}
	if ( !vk_modelVertexes.buffer || vk_modelUsed + size > VK_MODEL_BYTES
		|| vk_modelIndexUsed + (vkSize_t)surface->numTriangles * 3 * sizeof( uint32_t ) > VK_MODEL_INDEX_BYTES ) {
		return -1;
	}
	for ( int i = 0; i < surface->numBoneReferences; i++ ) {
		if ( boneReferences[i] < 0 || boneReferences[i] > 255 ) {
			vk_modelFirstIndex[surface] = -1;
			return -1;
		}
	}

	ri.LoadPush( "model buffers", NULL );
	out = (vkSkinVertex_t *)VK_Stage( size, &staged );
	memset( out, 0, (size_t)size );
	for ( int i = 0; i < surface->numVerts; i++, v++, out++ ) {
		VK_SkinVertex( v, boneReferences, out );
		out->st[0] = texCoords[i].texCoords[0];
		out->st[1] = texCoords[i].texCoords[1];
	}

	VK_CopyBuffer( &vk.staging, staged, &vk_modelVertexes, vk_modelUsed, size );
	first = VK_ModelIndexes( (const int *)( (const byte *)surface + surface->ofsTriangles ), surface->numTriangles * 3,
		(int)( vk_modelUsed / sizeof( vkSkinVertex_t ) ) );
	ri.LoadPop();
	vk_modelFirstVertex[surface] = (int)( vk_modelUsed / sizeof( vkSkinVertex_t ) );
	vk_modelUsed += size;
	vk_modelFirstIndex[surface] = first;
	return first;
}

// where the vertices of a Ghoul2 surface that VK_SkinnedSurface() put on the device begin in the buffer of the
// models, -1: not there. vertexes: that buffer.
int VK_SkinnedFirstVertex( const mdxmSurface_t *surface, const vkBuffer_t **vertexes ) {
	const auto found = vk_modelFirstVertex.find( surface );

	*vertexes = &vk_modelVertexes;
	return ( found != vk_modelFirstVertex.end() ) ? found->second : -1;
}

/*
===============
VK_GoreSurface

A saber mark on a surface of a model: where its vertices begin in the buffer of the marks. They are put there
now if this is the first time the mark is drawn since the buffer was begun.
goreData, as G2_misc.cpp makes it: the number of vertices, the number of triangles, for every vertex its number
in the surface, room for 8 floats a vertex (position and normal as the bones move them, not used on this way),
2 texture coordinates a vertex, the triangles.
-1: it can not go there.
===============
*/
int VK_GoreSurface( const mdxmSurface_t *surface, const float *goreData ) {
	const int *data = (const int *)goreData;
	const int numVerts = data[0];
	const int *which = data + 2;
	const float *st = (const float *)( which + numVerts * 9 );
	const vkSize_t size = (vkSize_t)numVerts * sizeof( vkSkinVertex_t );
	const mdxmVertex_t *v = (const mdxmVertex_t *)( (const byte *)surface + surface->ofsVerts );
	const int *boneReferences = (const int *)( (const byte *)surface + surface->ofsBoneReferences );
	vkSize_t staged;
	vkSkinVertex_t *out;
	int first;

	if ( !vk_goreFirstVertex ) {
		return -1;
	}
	const auto found = vk_goreFirstVertex->find( goreData );
	if ( found != vk_goreFirstVertex->end() ) {
		return found->second;
	}
	if ( !vk_goreVertexes[vk_goreBuffer].buffer || numVerts <= 0 || vk_goreUsed + size > VK_GORE_BYTES ) {
		return -1;
	}
	// a surface that can not go to the device has bones a byte can not number
	if ( VK_SkinnedSurface( surface ) < 0 ) {
		return -1;
	}
	for ( int i = 0; i < numVerts; i++ ) {
		if ( which[i] < 0 || which[i] >= surface->numVerts ) {
			return -1;
		}
	}

	out = (vkSkinVertex_t *)VK_Stage( size, &staged );
	memset( out, 0, (size_t)size );
	for ( int i = 0; i < numVerts; i++, out++ ) {
		VK_SkinVertex( &v[which[i]], boneReferences, out );
		out->st[0] = st[i * 2];
		out->st[1] = st[i * 2 + 1];
	}
	VK_CopyBuffer( &vk.staging, staged, &vk_goreVertexes[vk_goreBuffer], vk_goreUsed, size );
	first = (int)( vk_goreUsed / sizeof( vkSkinVertex_t ) );
	vk_goreUsed += size;
	( *vk_goreFirstVertex )[goreData] = first;
	return first;
}

/*
===============
VK_GoreFreed

The data of a mark is freed (G2_gore.cpp, G2_misc.cpp). Another mark may get the same address.
===============
*/
void VK_GoreFreed( const void *goreData ) {
	if ( vk_goreFirstVertex ) {
		vk_goreFirstVertex->erase( goreData );
	}
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
	const vkSize_t size = (vkSize_t)surface->numVerts * sizeof( vkSkinVertex_t );
	const short *xyz = (const short *)( (const byte *)surface + surface->ofsXyzNormals );
	const float *st = (const float *)( (const byte *)surface + surface->ofsSt );
	vkSize_t staged;
	vkSkinVertex_t *out;
	int first;

	if ( found != vk_modelFirstIndex.end() ) {
		return found->second;
	}
	if ( !vk_modelVertexes.buffer || vk_modelUsed + size > VK_MODEL_BYTES
		|| vk_modelIndexUsed + (vkSize_t)surface->numTriangles * 3 * sizeof( uint32_t ) > VK_MODEL_INDEX_BYTES ) {
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

	VK_CopyBuffer( &vk.staging, staged, &vk_modelVertexes, vk_modelUsed, size );
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
	const vkSize_t offset = 0;

	if ( !vk_numBatchRanges ) {
		return qfalse;
	}
	VK_BindVertexBuffer( &vk_modelVertexes, &offset, 1 );
	VK_BindIndexBuffer( &vk_modelIndexes );
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
// the bones of a skeleton into the frame if they are not there yet. bones: where it starts, in rows of the bone buffer
static qboolean VK_FrameSkeleton( CBoneCache *boneCache, uint32_t *bones ) {
	vkFrame_t *frame = vk.frame;
	int i;

	for ( i = 0; i < vk_numFrameSkeletons; i++ ) {
		if ( vk_frameSkeletons[i].cache == boneCache ) {
			*bones = vk_frameSkeletons[i].base;
			return qtrue;
		}
	}
	// a skeleton has at most 256 bones, see VK_SkinnedSurface()
	const int first = VK_SkeletonStart( frame );

	if ( first + 256 > VK_MAX_BONES ) {
		vk.statSkeletonsLost[0]++;
		return qfalse;
	}
	const int numBones = G2_BoneMatrices( boneCache, (float *)( frame->bones.mapped + sizeof( float ) * 12 * first ), 256 );
	int cutBones = 0, extra = 0;
	const float *cut = G2_VJK_CutData( boneCache, &cutBones );
	uint32_t cutRows = 0;

	// VisionJK: after the bones the cut of a part of a cut body, two rows a bone (in room of bones of three rows)
	if ( cut && cutBones == numBones ) {
		extra = ( numBones * 2 + 2 ) / 3;
		if ( first + numBones + extra <= VK_MAX_BONES ) {
			memcpy( frame->bones.mapped + sizeof( float ) * 12 * ( first + numBones ), cut, sizeof( float ) * 8 * numBones );
			cutRows = (uint32_t)( first + numBones ) * 3;
		} else {
			extra = 0;
		}
	}
	if ( vk_numFrameSkeletons < VK_MAX_FRAME_SKELETONS ) {
		vk_frameSkeletons[vk_numFrameSkeletons].cache = boneCache;
		vk_frameSkeletons[vk_numFrameSkeletons].base = (uint32_t)first * 3;
		vk_frameSkeletons[vk_numFrameSkeletons].cut = cutRows;
		vk_numFrameSkeletons++;
	}
	*bones = (uint32_t)first * 3;
	frame->numBones = first + numBones + extra;
	vk.statSkeletons[0]++;
	return qtrue;
}

// VisionJK: where the cut of the skeleton that starts at row bones is in the frame, 0: it has none
uint32_t VK_CutBones( uint32_t bones ) {
	for ( int i = 0; i < vk_numFrameSkeletons; i++ ) {
		if ( vk_frameSkeletons[i].base == bones ) {
			return vk_frameSkeletons[i].cut;
		}
	}
	return 0;
}

// the same for those who do not draw (the shadows of the players, vk_rt.cpp)
qboolean VK_SkeletonBones( CBoneCache *boneCache, uint32_t *bones ) {
	return (qboolean)( vk.frame && boneCache && VK_FrameSkeleton( boneCache, bones ) );
}

qboolean VK_UploadSkinnedBatch( CBoneCache *boneCache, uint32_t *bones ) {
	vkFrame_t *frame = vk.frame;

	if ( !frame ) {
		return qfalse;
	}
	if ( !boneCache ) {
		if ( vk_identityBone < 0 ) {
			static const float identity[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
			const int first = VK_SkeletonStart( frame );

			if ( first + 1 > VK_MAX_BONES ) {
				return qfalse;
			}
			memcpy( frame->bones.mapped + sizeof( identity ) * first, identity, sizeof( identity ) );
			vk_identityBone = first * 3;
			frame->numBones = first + 1;
		}
		*bones = (uint32_t)vk_identityBone;
		return VK_BindModelBatch();
	}
	if ( !VK_FrameSkeleton( boneCache, bones ) ) {
		return qfalse;
	}
	return VK_BindModelBatch();
}

/*
===============
VK_UploadGoreBatch

A batch of saber marks on one model: the indexes in tess.indexes count in the buffer of the marks
===============
*/
qboolean VK_UploadGoreBatch( CBoneCache *boneCache, uint32_t *bones ) {
	const vkSize_t offset = 0;

	if ( !vk.frame || !boneCache || !VK_FrameSkeleton( boneCache, bones ) ) {
		return qfalse;
	}
	return VK_UploadIndexes( &vk_goreVertexes[vk_goreBuffer], &offset, 1, tess.indexes, tess.numIndexes );
}
