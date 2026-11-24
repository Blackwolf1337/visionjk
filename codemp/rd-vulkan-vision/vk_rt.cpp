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

// vk_rt.cpp: ray tracing on the devices that have it (vk.rayTracing, VK_KHR_ray_query)
//
// The level keeps its lightmaps: the light of the world, its shadows included, was worked out when the map was
// compiled. What moves gets its shadows here, by rays from the pixels of the view towards the lights of the level.
// The rays go through acceleration structures:
// - one of the solid surfaces of the level, built once per level from their own copy of the positions (the buffer
//   of vk_world.cpp is laid out for drawing);
// - one for each player who casts a shadow (RF_SHADOW_PLANE), built each frame from his surfaces as the view drew
//   them: shaders/rtskin.comp moves their vertices by the bones of his skeleton into positions of the frame;
// - one for each brush model of the level (doors, lifts, platforms), built once per level like the level;
// - the top level over all of them each frame, which the shaders that trace read: the level with mask 1, the
//   players with mask 2, each with the place of his entity, the brush models with mask 4 where theirs are now (only
//   the moving lights trace against them: the lightmaps and the cells were made with them where the map has them).
//
// The passes run where the wet places do (RB_DrawWet()): after what is solid, before anything transparent, outside
// a target, so the builds of the frame are recorded right before them in its command buffer. Every build of a
// frame first says how much scratch memory it needs, then the scratch buffer is made large enough once, before
// anything that uses it is recorded.
//
// cg_shadows 2: shaders/rtshadow.frag traces from every pixel of the view towards the light of the level there (the
// sun where it reaches the point, else the direction of the light grid) against the players and writes how much they
// block into a mask; shaders/rtresolve.frag softens it over the surface the pixel lies on and multiplies it into the
// frame.
//
// v_rtDebug shows what the rays see: 1 whether a ray from each pixel towards the sun of the level gets out
// (lit where it does: outdoors that is the shadow the lightmaps have), 2 how far a ray from the eye through each
// pixel goes against the depth of the scene (black where they agree, red where the ray stops short of the depth,
// green where it goes past it), 3 the rays towards the sun against the players alone: their sun shadows.

#include "vk_local.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

static const uint32_t vk_rtDebugFrag[] =
#include "shaders/rtdebug_frag.inc"
;
static const uint32_t vk_rtSkinComp[] =
#include "shaders/rtskin_comp.inc"
;
static const uint32_t vk_rtShadowFrag[] =
#include "shaders/rtshadow_frag.inc"
;
static const uint32_t vk_rtResolveFrag[] =
#include "shaders/rtresolve_frag.inc"
;
static const uint32_t vk_smDepthVert[] =
#include "shaders/smdepth_vert.inc"
;
static const uint32_t vk_smShadowFrag[] =
#include "shaders/smshadow_frag.inc"
;
static const uint32_t vk_smLightsFrag[] =
#include "shaders/smlights_frag.inc"
;
static const uint32_t vk_rtLightMaskFrag[] =
#include "shaders/rtlightmask_frag.inc"
;
static const uint32_t vk_rtAccumFrag[] =
#include "shaders/rtaccum_frag.inc"
;
static const uint32_t vk_rtCellsComp[] =
#include "shaders/rtcells_comp.inc"
;
static const uint32_t vk_rtLampsComp[] =
#include "shaders/rtlamps_comp.inc"
;

#define VK_RT_MAX_INSTANCES			256
#define VK_RT_MAX_BRUSHES			( VK_RT_MAX_INSTANCES - 1 - VK_RT_MAX_CASTERS )	// brush entities in one frame
#define VK_RT_BRUSH_RANGE			3072.0f			// from the eye: further out none of the lights that cast shadows reaches them
#define VK_RT_MAX_CASTERS			16				// players with a shadow in one frame
#define VK_RT_LAMP_RAYS				256				// panels a frame asks whether a player sees them (shaders/rtlamps.comp)
#define VK_RT_LAMP_ASKED			16				// of them for one player
#define VK_RT_MAX_CASTER_SURFACES	64				// surfaces of one of them, saber and weapon included
#define VK_RT_MAX_QUEUED			( VK_RT_MAX_CASTERS * VK_RT_MAX_CASTER_SURFACES )	// of players not drawn in the view
#define VK_RT_HIDDEN_RANGE			1024.0f			// from the eye: further out their shadows are not worth the bones
#define VK_RT_SKIN_VERTEXES			( 256 * 1024 )	// their skinned vertices in one frame
#define VK_RT_LOCAL_INDEXES			( 1024 * 1024 )	// the triangles of the model surfaces they have, counted in the surface
#define VK_RT_CASTER_BYTES			( 32 << 20 )	// their acceleration structures in one frame
#define VK_RT_ACCEL_ALIGN			256				// where an acceleration structure may begin in its buffer

// the shadow maps: tiles of one atlas, one for each caster (in the light of the grid at his feet, without ray tracing)
// and for the moving lights: the first VK_RT_TUBES a cube (six) at each of VK_SM_POINTS points along it (a blade: near
// the hilt, the middle, the tip; a point light uses the first alone), the others, points only, one cube each
#define VK_SM_TILE					256
#define VK_SM_COLUMNS				8
#define VK_SM_POINTS				3
#define VK_RT_LIGHTS					4				// moving lights with shadows in one frame, one channel of the mask each
#define VK_RT_TUBES					2				// of them blades at most
#define VK_SM_TILES					( VK_RT_MAX_CASTERS + VK_RT_TUBES * VK_SM_POINTS * 6 + ( VK_RT_LIGHTS - VK_RT_TUBES ) * 6 )
#define VK_SM_LIGHT_TILE( k, point )	( VK_RT_MAX_CASTERS + ( ( k ) < VK_RT_TUBES ? ( ( k ) * VK_SM_POINTS + ( point ) ) * 6 \
										: VK_RT_TUBES * VK_SM_POINTS * 6 + ( ( k ) - VK_RT_TUBES ) * 6 ) )
#define VK_SM_ROWS					( ( VK_SM_TILES + VK_SM_COLUMNS - 1 ) / VK_SM_COLUMNS )
#define VK_SM_FIRST_ROW				47				// of the rt globals: four a tile, its view and projection
#define VK_RT_POWER_ROW				( VK_SM_FIRST_ROW + VK_SM_TILES * 4 + 4 )	// how strong each moving light is (rtresolve.frag)
#define VK_SM_OWN_LIGHT				56.0f			// a point of light this near a player can be his own weapon (VK_SM_DrawAtlas)
#define VK_RT_LIGHT_ROW( k )		( ( k ) < 2 ? 38 + ( k ) * 2 : VK_SM_FIRST_ROW + VK_SM_TILES * 4 + ( ( k ) - 2 ) * 2 )	// two a light

cvar_t	*v_rtDebug;
static cvar_t	*v_rtShadows;		// 0: the players cast no traced shadows
static cvar_t	*v_rtShadowRays;	// rays a pixel
static cvar_t	*v_rtSunSize;		// degrees of the sun, how soft its shadows get
static cvar_t	*v_rtLightSize;		// the same for the light of the light grid
static cvar_t	*v_rtLamps;			// 0: the shadows indoors go by the light grid alone, not by the panels that glow
static cvar_t	*v_rtMaskScale;		// 2: the rays and the masks of the moving lights at half the size of the frame

typedef struct vkAccel_s {
	VkAccelerationStructureKHR	as;
	vkBuffer_t					buffer;
	VkDeviceAddress				address;
} vkAccel_t;

typedef struct vkCasterSurface_s {
	const mdxmSurface_t			*surface;
	CBoneCache					*bones;
} vkCasterSurface_t;

typedef struct vkRtLight_s {		// a moving light whose players' shadows are traced
	qboolean					active;
	qboolean					inSolid;		// a point inside a wall or floor: an explosion on what it hit
	float						power;			// how bright, as the light pass has it
	qboolean					tube;
	vec3_t						start, end;		// a point light has both at its place
	float						range;			// how far it reaches from that point or tube
} vkRtLight_t;

typedef struct vkCaster_s {
	int							entityNum;
	vec3_t						origin;
	vec3_t						axis[3];
	int							numSurfaces;
	vkCasterSurface_t			surfaces[VK_RT_MAX_CASTER_SURFACES];
	// filled when the frame builds them
	VkAccelerationStructureKHR	as;
	VkDeviceAddress				address;
	VkDeviceSize				scratch;
} vkCaster_t;

static struct {
	// the level
	const world_t				*world;			// what the structure of the level was built for
	int							worldLoads;		// and r_worldLoads then: the pointer is the same for every level
	qboolean					levelReady;		// built, or its build is recorded before the first use
	qboolean					levelPending;	// its build still has to be recorded this frame
	vkBuffer_t					positions;		// the inputs of its build
	vkBuffer_t					indexes;
	uint32_t					numVertexes, numTriangles;
	VkDeviceSize				levelScratch;
	vkAccel_t					level;
	// the brush models (VK_RT_Brushes): their triangles in one pair of buffers, each its own structure
	vkBuffer_t					brushPositions, brushIndexes;
	uint32_t					brushVertexes;
	vkBuffer_t					grid;			// the light grid of the level, three vec4 a cell (shaders/rtshadow.frag)
	// the light patches of the level (VK_RT_Patches), three vec4 each: middle and power, the side it lights and the
	// radius of its disk, how far it goes from its middle; for every cell of the grid its list of patches
	// (VK_RT_LIST_WORDS) and the light of that list with whether the sun reaches it (two floats), from rtcells.comp
	vkBuffer_t					patches;
	vkBuffer_t					cellLists, cellLight;
	int							cellState;		// 1 to be worked out, 2 worked out in the frame cellFrame, 0 done or none
	int							cellFrame;
	VkPipelineLayout			cellsLayout;
	VkPipeline					cellsPipeline;
	VkShaderModule				cellsShader;
	// whether the players see the panels near them (shaders/rtlamps.comp with the layout of the cells), asked in a
	// frame and read when its place comes round again: three vec4 a question, then a float an answer
	VkPipeline					lampsPipeline;
	VkShaderModule				lampsShader;
	vkBuffer_t					lampRays[VK_FRAMES];
	int							lampAsked[VK_FRAMES];
	vec3_t						lampAskedFrom[VK_FRAMES][VK_RT_LAMP_RAYS];	// where the player stood
	int							lampAskedPatch[VK_FRAMES][VK_RT_LAMP_RAYS];

	vkBuffer_t					scratch;		// for every build of a frame
	VkDeviceSize				scratchAlign;

	// the players who cast a shadow in this frame, as the view of the level drew them
	vkCaster_t					casters[VK_RT_MAX_CASTERS];
	int							numCasters;
	int							castersFrame;	// backEnd.viewParms.frameCount they belong to
	vkBuffer_t					localIndexes;	// the triangles of their surfaces, counted from the first vertex of each
	uint32_t					localIndexesUsed;
	std::unordered_map<const void *, uint32_t>	*localFirst;	// of an mdxmSurface_t in localIndexes
	vkBuffer_t					skinned[VK_FRAMES];		// their positions in a frame
	vkBuffer_t					arena[VK_FRAMES];		// their acceleration structures in a frame
	VkAccelerationStructureKHR	arenaAs[VK_FRAMES][VK_RT_MAX_CASTERS];
	VkPipelineLayout			skinLayout;
	VkPipeline					skinPipeline;
	VkShaderModule				skinShader;

	// the top level, one for each frame in flight
	vkAccel_t					top[VK_FRAMES];
	vkBuffer_t					instances[VK_FRAMES];
	VkDeviceSize				topScratch;

	VkDescriptorPool			pool;
	VkDescriptorSetLayout		setLayout;
	VkDescriptorSet				sets[VK_FRAMES];
	VkPipelineLayout			layout;
	VkShaderModule				debugFragment;
	VkPipeline					debugPipeline;

	// the shadows of the players: the mask of this frame and of the one before (frameNumber picks), how far the
	// points were from the eye with it; the view of the frame before for shaders/rtshadow.frag to find them again
	vkTarget_t					mask[2];
	VkDescriptorSet				maskSets[2];
	VkDescriptorSet				lightMaskSets[2];	// the masks of the moving lights for shaders/rtresolve.frag
	// the rays of this frame alone (rtshadow.frag, smshadow.frag), laid onto the mask of the frame before by
	// shaders/rtaccum.frag (its layout: the sets of the frame, of the raw mask, of the depth, of the mask before)
	vkTarget_t					raw;
	VkDescriptorSet				rawSet;
	VkPipelineLayout			accumLayout;
	VkShaderModule				accumFragment;
	VkPipeline					accumPipeline;
	float						previous[5][4];		// its view and projection, its eye
	qboolean					previousValid;
	VkShaderModule				shadowFragment, resolveFragment;
	VkPipeline					shadowPipeline, resolvePipeline;
	int							frameNumber;	// the noise of the rays changes with it

	// The shadows of the players from the moving lights (shaders/smlights.frag, from the cubes of the shadow maps on
	// both ways): for the strongest lights (VK_RT_PickLights), how much the players and, with rays, the level take of
	// each at every pixel, one channel a light. Made after the solid part of the
	// level, so the light passes of the NEXT frame read it (generic.frag DynamicLight), through the texture table
	// (tr.rtLightImage), at the place the point had in the view it was made in. One mask is written while the
	// other is read.
	vkTarget_t					lightMask[2];
	int							lightShown[2];	// the texture slot each was last written into, -1: none
	int							lightWrite;		// the one this frame writes
	vkRtLight_t					lights[VK_RT_LIGHTS];		// the lights of this frame's mask
	vkRtLight_t					readLights[VK_RT_LIGHTS];	// the lights of the mask the light passes read now
	// The surfaces of the players the view does not draw (VK_RT_QueueCaster, front end), taken by VK_RT_Pass after
	// those it drew, so a full list keeps the players on the screen.
	struct {
		const trRefEntity_t		*ent;
		CBoneCache				*bones;
		const mdxmSurface_t		*surface;
	}							queued[VK_RT_MAX_QUEUED];
	int							numQueued, queuedFrame;
	float						readView[5][4];	// the view and projection it was made with, its rect on the target
	qboolean					readValid;

	// A device without ray tracing (or v_rayTracing 0): the same masks from shadow maps of the players, drawn from
	// the skinned positions into an atlas (VK_SM_*), read by shaders/smshadow.frag and smlights.frag in place of the
	// rays. The level casts nothing there: its shadows are in the lightmaps.
	qboolean					maps;
	vkTarget_t					atlas;
	VkPipelineLayout			depthLayout;
	VkPipeline					depthPipeline;
	VkPipeline					depthLevelPipeline;		// the level into the cubes of the moving lights (VK_SM_BuildLevel)
	vkBuffer_t					mapPositions, mapIndexes;
	VkShaderModule				depthVertex;
	VkShaderModule				smShadowFragment, smLightsFragment;
	VkPipeline					smShadowPipeline, smLightsPipeline;
} rt;

static void VK_SM_Create( void );

// a combined image sampler into binding of a set
static void VK_WriteSetBinding( VkDescriptorSet set, uint32_t binding, VkSampler sampler, VkImageView view, VkImageLayout layout ) {
	VkDescriptorImageInfo	image = { sampler, view, layout };
	VkWriteDescriptorSet	write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

	write.dstSet = set;
	write.dstBinding = binding;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &image;
	qvkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
}

static VkDeviceAddress VK_RT_Address( const vkBuffer_t *buf ) {
	VkBufferDeviceAddressInfo info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };

	info.buffer = buf->buffer;
	return qvkGetBufferDeviceAddress( vk.device, &info );
}

static VkDeviceSize VK_RT_Align( VkDeviceSize size, VkDeviceSize alignment ) {
	return ( size + alignment - 1 ) / alignment * alignment;
}

static VkAccelerationStructureKHR VK_RT_NewAccel( const vkBuffer_t *buffer, VkDeviceSize offset, VkDeviceSize size,
	VkAccelerationStructureTypeKHR type, VkDeviceAddress *address ) {
	VkAccelerationStructureCreateInfoKHR info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
	VkAccelerationStructureDeviceAddressInfoKHR addressInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
	VkAccelerationStructureKHR as;

	info.buffer = buffer->buffer;
	info.offset = offset;
	info.size = size;
	info.type = type;
	VK_CHECK( qvkCreateAccelerationStructureKHR( vk.device, &info, NULL, &as ) );
	addressInfo.accelerationStructure = as;
	*address = qvkGetAccelerationStructureDeviceAddressKHR( vk.device, &addressInfo );
	return as;
}

static void VK_RT_CreateAccel( vkAccel_t *accel, VkAccelerationStructureTypeKHR type, VkDeviceSize size ) {
	VK_CreateBuffer( &accel->buffer, size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
		| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, qfalse );
	accel->as = VK_RT_NewAccel( &accel->buffer, 0, size, type, &accel->address );
}

static void VK_RT_DestroyAccel( vkAccel_t *accel ) {
	if ( accel->as ) {
		qvkDestroyAccelerationStructureKHR( vk.device, accel->as, NULL );
	}
	VK_DestroyBuffer( &accel->buffer );
	memset( accel, 0, sizeof( *accel ) );
}

// The scratch buffer at least size bytes past the alignment the device wants for it. Only before anything of
// the frame that uses it is recorded: a larger one replaces it when the frames before are done.
static void VK_RT_EnsureScratch( VkDeviceSize size ) {
	if ( rt.scratch.size < size + rt.scratchAlign ) {
		VK_WaitIdle();
		VK_DestroyBuffer( &rt.scratch );
		VK_CreateBuffer( &rt.scratch, size + rt.scratchAlign, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
			| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, qfalse );
	}
}

static VkDeviceAddress VK_RT_ScratchAt( VkDeviceSize offset ) {
	return VK_RT_Align( VK_RT_Address( &rt.scratch ), rt.scratchAlign ) + offset;
}

static void VK_RT_Barrier( VkCommandBuffer cmd, VkPipelineStageFlags srcStage, VkAccessFlags srcAccess,
	VkPipelineStageFlags dstStage, VkAccessFlags dstAccess ) {
	VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };

	barrier.srcAccessMask = srcAccess;
	barrier.dstAccessMask = dstAccess;
	qvkCmdPipelineBarrier( cmd, srcStage, dstStage, 0, 1, &barrier, 0, NULL, 0, NULL );
}

// a build that comes after another one: it may read what that wrote and use the same scratch memory
static void VK_RT_BuildAfterBuild( VkCommandBuffer cmd ) {
	VK_RT_Barrier( cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR );
}

static void VK_RT_Triangles( VkAccelerationStructureGeometryKHR *geometry, VkDeviceAddress vertexes, uint32_t numVertexes,
	VkDeviceAddress indexes ) {
	memset( geometry, 0, sizeof( *geometry ) );
	geometry->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry->geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geometry->flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	geometry->geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	geometry->geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	geometry->geometry.triangles.vertexData.deviceAddress = vertexes;
	geometry->geometry.triangles.vertexStride = sizeof( float ) * 3;
	geometry->geometry.triangles.maxVertex = numVertexes - 1;
	geometry->geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
	geometry->geometry.triangles.indexData.deviceAddress = indexes;
}

/*
===============================================================================

THE LEVEL

===============================================================================
*/

// The surfaces whose shader stops light: solid and drawn. Glass, water, fog, portals and the sky let it through,
// and so do surfaces cut out by the alpha of their texture (grates, leaves), which need a test of that alpha the
// rays do not make yet.
static qboolean VK_RT_StopsLight( const shader_t *shader ) {
	if ( !shader || ( shader->surfaceFlags & ( SURF_SKY | SURF_NODRAW ) ) || shader->sort != SS_OPAQUE
		|| shader->numUnfoggedPasses <= 0 ) {
		return qfalse;
	}
	for ( int i = 0; i < shader->numUnfoggedPasses; i++ ) {
		if ( shader->stages[i].active && ( shader->stages[i].stateBits & GLS_ATEST_BITS ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

// the triangles of a face, triangle mesh or patch of the level, added to the lists
static void VK_RT_SurfaceTriangles( const msurface_t *surf, std::vector<float> &positions, std::vector<uint32_t> &indexes ) {
	const uint32_t base = (uint32_t)( positions.size() / 3 );

	if ( *surf->data == SF_FACE ) {
		const srfSurfaceFace_t	*face = (const srfSurfaceFace_t *)surf->data;
		const int				*faceIndexes = (const int *)( (const byte *)face + face->ofsIndices );

		for ( int j = 0; j < face->numPoints; j++ ) {
			positions.insert( positions.end(), face->points[j], face->points[j] + 3 );
		}
		for ( int j = 0; j < face->numIndices; j++ ) {
			indexes.push_back( base + (uint32_t)faceIndexes[j] );
		}
	} else if ( *surf->data == SF_TRIANGLES ) {
		const srfTriangles_t *tris = (const srfTriangles_t *)surf->data;

		for ( int j = 0; j < tris->numVerts; j++ ) {
			positions.insert( positions.end(), tris->verts[j].xyz, tris->verts[j].xyz + 3 );
		}
		for ( int j = 0; j < tris->numIndexes; j++ ) {
			indexes.push_back( base + (uint32_t)tris->indexes[j] );
		}
	} else if ( *surf->data == SF_GRID ) {
		const srfGridMesh_t *grid = (const srfGridMesh_t *)surf->data;

		for ( int j = 0; j < grid->width * grid->height; j++ ) {
			positions.insert( positions.end(), grid->verts[j].xyz, grid->verts[j].xyz + 3 );
		}
		for ( int y = 0; y < grid->height - 1; y++ ) {
			for ( int x = 0; x < grid->width - 1; x++ ) {
				const uint32_t v = base + (uint32_t)( y * grid->width + x );

				indexes.push_back( v );
				indexes.push_back( v + (uint32_t)grid->width );
				indexes.push_back( v + 1 );
				indexes.push_back( v + 1 );
				indexes.push_back( v + (uint32_t)grid->width );
				indexes.push_back( v + (uint32_t)grid->width + 1 );
			}
		}
	}
}

// the side each vertex of a surface shows, in the order of VK_RT_SurfaceTriangles: the plane of a face, the normals
// of the vertices of the others
static void VK_RT_SurfaceNormals( const msurface_t *surf, std::vector<float> &normals ) {
	if ( *surf->data == SF_FACE ) {
		const srfSurfaceFace_t *face = (const srfSurfaceFace_t *)surf->data;

		for ( int j = 0; j < face->numPoints; j++ ) {
			normals.insert( normals.end(), face->plane.normal, face->plane.normal + 3 );
		}
	} else if ( *surf->data == SF_TRIANGLES ) {
		const srfTriangles_t *tris = (const srfTriangles_t *)surf->data;

		for ( int j = 0; j < tris->numVerts; j++ ) {
			normals.insert( normals.end(), tris->verts[j].normal, tris->verts[j].normal + 3 );
		}
	} else if ( *surf->data == SF_GRID ) {
		const srfGridMesh_t *grid = (const srfGridMesh_t *)surf->data;

		for ( int j = 0; j < grid->width * grid->height; j++ ) {
			normals.insert( normals.end(), grid->verts[j].normal, grid->verts[j].normal + 3 );
		}
	}
}

// the triangles of the solid surfaces of the world model (not doors, lifts and the other brush models, which move)
static void VK_RT_LevelTriangles( std::vector<float> &positions, std::vector<uint32_t> &indexes ) {
	const bmodel_t *model = &tr.world->bmodels[0];

	for ( int i = 0; i < model->numSurfaces; i++ ) {
		if ( VK_RT_StopsLight( model->firstSurface[i].shader ) ) {
			VK_RT_SurfaceTriangles( &model->firstSurface[i], positions, indexes );
		}
	}
}

/*
===============
VK_RT_Brushes

Doors, lifts and the other brush models of the level (the inline models *1, *2...). Their triangles are in the space
of the model, so one structure (or one range of the mesh of the shadow maps) serves wherever the entity is in a
frame. Without them a lift stood in the light of a saber like glass, and its edge threw no shadow on the floor.
===============
*/
typedef struct {
	uint32_t	firstIndex, numIndexes;		// in rt.brushIndexes, or in rt.mapIndexes for the shadow maps
	vkAccel_t	accel;
	VkDeviceSize	scratch;
} vkBrush_t;

typedef struct {
	int			brush;
	vec3_t		origin, axis[3];
	vec3_t		center;
	float		radius;
} vkBrushEntity_t;

static std::vector<vkBrush_t>	vk_brushes;		// by the number of the model, 0 (the world) left empty
static vkBrushEntity_t			vk_brushEnts[VK_RT_MAX_BRUSHES];
static int						vk_numBrushEnts;

// the brush entities of the view of the level near the eye, where they are in this frame
static void VK_RT_BrushEntities( void ) {
	vk_numBrushEnts = 0;
	if ( !tr.world || vk_brushes.empty() ) {
		return;
	}
	for ( int i = 0; i < backEnd.refdef.num_entities && vk_numBrushEnts < VK_RT_MAX_BRUSHES; i++ ) {
		const trRefEntity_t	*ent = &backEnd.refdef.entities[i];
		const model_t		*model;

		// (not RF_NOSHADOW: the game gives it to every mover, for the old stencil shadows)
		if ( ent->e.reType != RT_MODEL || ( ent->e.renderfx & RF_FIRST_PERSON ) ) {
			continue;
		}
		model = R_GetModelByHandle( ent->e.hModel );
		if ( !model || model->type != MOD_BRUSH || !model->bmodel || model->bspInstance ) {
			continue;
		}
		const int brush = (int)( model->bmodel - tr.world->bmodels );
		if ( brush <= 0 || brush >= (int)vk_brushes.size() || !vk_brushes[brush].numIndexes ) {
			continue;
		}
		vkBrushEntity_t	*out = &vk_brushEnts[vk_numBrushEnts];
		vec3_t			middle;

		VectorAdd( model->bmodel->bounds[0], model->bmodel->bounds[1], middle );
		VectorScale( middle, 0.5f, middle );
		VectorCopy( ent->e.origin, out->center );
		for ( int k = 0; k < 3; k++ ) {
			VectorMA( out->center, middle[k], ent->e.axis[k], out->center );
		}
		out->radius = 0.5f * Distance( model->bmodel->bounds[0], model->bmodel->bounds[1] );
		if ( Distance( out->center, backEnd.viewParms.ori.origin ) - out->radius > VK_RT_BRUSH_RANGE ) {
			continue;
		}
		out->brush = brush;
		VectorCopy( ent->e.origin, out->origin );
		for ( int k = 0; k < 3; k++ ) {
			VectorCopy( ent->e.axis[k], out->axis[k] );
		}
		vk_numBrushEnts++;
	}
}

/*
===============
VK_SM_BuildLevel

Without rays the level goes into the cubes of the moving lights as a mesh of its own, so walls and pillars hide them.
Its triangles are sorted into chunks of VK_SM_CHUNK units, so a cube draws what lies near its light. They all turn
the side they show to the right-handed normal (a shader that shows both sides has them twice), for VK_SM_Create to
keep only what faces the light. Only while no frame is on its way.
===============
*/
#define VK_SM_CHUNK		256.0f

typedef struct {
	vec3_t		mins, maxs;
	uint32_t	firstIndex, numIndexes;
} vkMapChunk_t;

static std::vector<vkMapChunk_t>	vk_mapChunks;
static std::vector<uint32_t>		vk_mapNear;		// the chunks in reach of the point of the cube being drawn

// a triangle turned so its right-handed normal is the side it shows, qfalse for one without area
static qboolean VK_SM_Turned( const std::vector<float> &positions, const std::vector<float> &normals, const uint32_t *in, uint32_t *v ) {
	const float	*a = &positions[in[0] * 3], *b = &positions[in[1] * 3], *c = &positions[in[2] * 3];
	const float	*na = &normals[in[0] * 3], *nb = &normals[in[1] * 3], *nc = &normals[in[2] * 3];
	vec3_t		e1, e2, cross, side;

	VectorSubtract( b, a, e1 );
	VectorSubtract( c, a, e2 );
	CrossProduct( e1, e2, cross );
	if ( VectorLengthSquared( cross ) < 0.0001f ) {
		return qfalse;
	}
	VectorAdd( na, nb, side );
	VectorAdd( side, nc, side );
	const qboolean turned = (qboolean)( DotProduct( cross, side ) < 0.0f );

	v[0] = in[0];
	v[1] = in[turned ? 2 : 1];
	v[2] = in[turned ? 1 : 2];
	return qtrue;
}

static void VK_SM_BuildLevel( void ) {
	typedef struct {
		uint64_t	cell;
		uint32_t	v[3];
	} mapTriangle_t;
	std::vector<float>			positions, normals;
	std::vector<uint32_t>		indexes, sorted;
	std::vector<mapTriangle_t>	triangles;
	const bmodel_t				*model = &tr.world->bmodels[0];

	for ( int i = 0; i < model->numSurfaces; i++ ) {
		const msurface_t	*surf = &model->firstSurface[i];
		const size_t		first = indexes.size();

		if ( !VK_RT_StopsLight( surf->shader ) ) {
			continue;
		}
		VK_RT_SurfaceTriangles( surf, positions, indexes );
		VK_RT_SurfaceNormals( surf, normals );
		for ( size_t t = first; t + 2 < indexes.size(); t += 3 ) {
			mapTriangle_t	tri;

			if ( !VK_SM_Turned( positions, normals, &indexes[t], tri.v ) ) {
				continue;
			}
			const float *a = &positions[tri.v[0] * 3], *b = &positions[tri.v[1] * 3], *c = &positions[tri.v[2] * 3];

			tri.cell = 0;
			for ( int k = 0; k < 3; k++ ) {
				const int at = (int)floorf( ( a[k] + b[k] + c[k] ) / ( 3.0f * VK_SM_CHUNK ) );

				tri.cell = ( tri.cell << 20 ) | (uint64_t)( at + 0x80000 );
			}
			triangles.push_back( tri );
			if ( surf->shader->cullType == CT_TWO_SIDED ) {
				const uint32_t other = tri.v[1];

				tri.v[1] = tri.v[2];
				tri.v[2] = other;
				triangles.push_back( tri );
			}
		}
	}
	std::sort( triangles.begin(), triangles.end(), []( const mapTriangle_t &x, const mapTriangle_t &y ) { return x.cell < y.cell; } );
	sorted.reserve( triangles.size() * 3 );
	for ( size_t t = 0; t < triangles.size(); t++ ) {
		if ( !t || triangles[t].cell != triangles[t - 1].cell ) {
			vkMapChunk_t chunk;

			ClearBounds( chunk.mins, chunk.maxs );
			chunk.firstIndex = (uint32_t)sorted.size();
			chunk.numIndexes = 0;
			vk_mapChunks.push_back( chunk );
		}
		vkMapChunk_t &chunk = vk_mapChunks.back();

		for ( int k = 0; k < 3; k++ ) {
			sorted.push_back( triangles[t].v[k] );
			AddPointToBounds( &positions[triangles[t].v[k] * 3], chunk.mins, chunk.maxs );
		}
		chunk.numIndexes += 3;
	}
	// the brush models after the chunks, a range each, the sides turned the same way (VK_SM_DrawAtlas places them)
	vk_brushes.assign( Q_max( tr.world->numBModels, 1 ), vkBrush_t() );
	for ( int b = 1; b < tr.world->numBModels; b++ ) {
		const bmodel_t	*brush = &tr.world->bmodels[b];
		vkBrush_t		*out = &vk_brushes[b];

		out->firstIndex = (uint32_t)sorted.size();
		for ( int i = 0; i < brush->numSurfaces; i++ ) {
			const msurface_t	*surf = &brush->firstSurface[i];
			const size_t		first = indexes.size();

			if ( !VK_RT_StopsLight( surf->shader ) ) {
				continue;
			}
			VK_RT_SurfaceTriangles( surf, positions, indexes );
			VK_RT_SurfaceNormals( surf, normals );
			for ( size_t t = first; t + 2 < indexes.size(); t += 3 ) {
				uint32_t v[3];

				if ( VK_SM_Turned( positions, normals, &indexes[t], v ) ) {
					sorted.insert( sorted.end(), v, v + 3 );
					if ( surf->shader->cullType == CT_TWO_SIDED ) {
						sorted.push_back( v[0] );
						sorted.push_back( v[2] );
						sorted.push_back( v[1] );
					}
				}
			}
		}
		out->numIndexes = (uint32_t)sorted.size() - out->firstIndex;
	}
	if ( sorted.empty() ) {
		vk_mapChunks.clear();
		vk_brushes.clear();
		return;
	}
	VK_CreateBuffer( &rt.mapPositions, positions.size() * sizeof( float ), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, qtrue );
	memcpy( rt.mapPositions.mapped, positions.data(), positions.size() * sizeof( float ) );
	VK_CreateBuffer( &rt.mapIndexes, sorted.size() * sizeof( uint32_t ), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, qtrue );
	memcpy( rt.mapIndexes.mapped, sorted.data(), sorted.size() * sizeof( uint32_t ) );
	ri.Printf( PRINT_DEVELOPER, "shadow maps: the level has %i triangles in %i chunks\n", (int)triangles.size(), (int)vk_mapChunks.size() );
}

/*
===============
VK_RT_UploadGrid

The light grid of the level for shaders/rtshadow.frag, every cell as R_SetupEntityLightingGrid() reads it with
the first light style: ambient light (w 0 for a cell in a wall), directed light, the direction it comes from.
Both frames' sets name it. Only while no frame is on its way.
===============
*/
static void VK_RT_UploadGrid( void ) {
	const world_t	*world = tr.world;
	const int		numCells = world->lightGridData ? world->lightGridBounds[0] * world->lightGridBounds[1] * world->lightGridBounds[2] : 0;
	float			*out;

	VK_DestroyBuffer( &rt.grid );
	VK_CreateBuffer( &rt.grid, sizeof( float ) * 12 * Q_max( numCells, 1 ), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, qtrue );
	out = (float *)rt.grid.mapped;
	memset( out, 0, sizeof( float ) * 12 * Q_max( numCells, 1 ) );
	for ( int i = 0; i < numCells; i++, out += 12 ) {
		const mgrid_t	*data;
		int				lat, lng;

		if ( world->lightGridArray[i] >= world->numGridArrayElements ) {
			continue;
		}
		data = world->lightGridData + world->lightGridArray[i];
		if ( data->styles[0] == LS_LSNONE ) {
			continue;
		}
		for ( int j = 0; j < 3; j++ ) {
			out[j] = data->ambientLight[0][j];
			out[4 + j] = data->directLight[0][j];
		}
		out[3] = 1.0f;
		lat = data->latLong[1] * ( FUNCTABLE_SIZE / 256 );
		lng = data->latLong[0] * ( FUNCTABLE_SIZE / 256 );
		out[8] = tr.sinTable[( lat + ( FUNCTABLE_SIZE / 4 ) ) & FUNCTABLE_MASK] * tr.sinTable[lng];
		out[9] = tr.sinTable[lat] * tr.sinTable[lng];
		out[10] = tr.sinTable[( lng + ( FUNCTABLE_SIZE / 4 ) ) & FUNCTABLE_MASK];
	}
	for ( int i = 0; i < VK_FRAMES; i++ ) {
		VkDescriptorBufferInfo	info;
		VkWriteDescriptorSet	write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

		info.buffer = rt.grid.buffer;
		info.offset = 0;
		info.range = VK_WHOLE_SIZE;
		write.dstSet = rt.sets[i];
		write.dstBinding = 1;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		write.pBufferInfo = &info;
		qvkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
	}
}

/*
===============================================================================

THE LIGHTS OF THE LEVEL

The maps keep no lights: q3map2 leaves the light entities out of the map it compiles. What shows where light
comes from are the textures that give it: a stage that glows (the glow of the frame), a stage added onto its
surface that is not the lightmap (lamps, light strips, screens), or a shader that gave light when the map was lit
(q3map_surfacelight). How bright such a texture is comes from its pixels (image_t::averageLuma) and the colour of
the stage. When a level is loaded:
- the triangles of every surface with such a shader become patches, near each other on one surface (up to
  VK_RT_PATCH_SIZE), each with its middle, the side it lights, its size and its power (brightness times area);
- shaders/rtcells.comp finds for every cell of the light grid the patches that give it the most light and that it
  sees (a ray to each with nothing of the level between), eight at most, how much light those give it and whether
  the sun reaches it;
- the light of the patches is scaled once for the level so that, at the cells the sun does not reach, it matches
  the directed light of the grid (least squares): then it is in the units of the grid, like its ambient light and
  the sun.
shaders/rtshadow.frag takes the list of the cell a pixel is in. Until the cells are done (a few frames after the
level, once the frame that worked them out has run) the shadows go by the sun and the direction of the grid.

===============================================================================
*/

#define VK_RT_MAX_PATCHES	4096
#define VK_RT_PATCH_SIZE	32.0f		// triangles of one surface this near each other are one patch
#define VK_RT_CELL_LIST		8			// patches in the list of a cell
#define VK_RT_LIST_WORDS	( VK_RT_CELL_LIST * 2 )	// a list: the numbers of the patches, then their light

typedef struct vkPatch_s {
	vec3_t			origin;			// summed by area until the patch is done, then its middle
	vec3_t			normal;			// summed, then the side it lights (zero: all sides)
	vec3_t			mins, maxs;
	float			area;
	float			emission;		// brightness of its shader
	int				surface;		// in the world model
} vkPatch_t;

static std::vector<vkPatch_t>	*vk_patches;
static float		vk_sunStrength;		// the directed light of the grid where the sun shines
static float		vk_patchScale;		// the light of the patches to the units of the grid, 0: not known yet

// the direction a cell of the grid has its directed light from, as R_SetupEntityLightingGrid() reads it
static void VK_RT_CellDirection( const mgrid_t *data, vec3_t dir ) {
	const int lat = data->latLong[1] * ( FUNCTABLE_SIZE / 256 );
	const int lng = data->latLong[0] * ( FUNCTABLE_SIZE / 256 );

	dir[0] = tr.sinTable[( lat + ( FUNCTABLE_SIZE / 4 ) ) & FUNCTABLE_MASK] * tr.sinTable[lng];
	dir[1] = tr.sinTable[lat] * tr.sinTable[lng];
	dir[2] = tr.sinTable[( lng + ( FUNCTABLE_SIZE / 4 ) ) & FUNCTABLE_MASK];
}

// the colour of a stage that does not depend on the light of the level, 0 for one that does (vertex colours)
static float VK_RT_StageColor( const shaderStage_t *stage ) {
	switch ( stage->rgbGen ) {
	case CGEN_IDENTITY:
	case CGEN_IDENTITY_LIGHTING:
	case CGEN_WAVEFORM:
	case CGEN_ENTITY:
		return 1.0f;
	case CGEN_CONST:
		return ( stage->constantColor[0] * 0.3f + stage->constantColor[1] * 0.59f + stage->constantColor[2] * 0.11f ) / 255.0f;
	default:
		return 0.0f;
	}
}

/* How much light the surfaces of a shader give, 0: none. A shader with a material of the material tool of jka.vision
   says it itself (emissive <light> <r> <g> <b> [noshadows], in the units of q3map_surfacelight; one that gives light
   but no shadows is no patch, its light stays a part of the light of the grid). For the others it is guessed from
   what glows of them and from q3map_surfacelight. The patches cast no shadows of the players since 2026-11-08
   (the lights they guessed did not agree with the lightmaps): they only give the strength of the sun in the units
   of the grid (VK_RT_FitPatches). */
static float VK_RT_Emission( const shader_t *shader ) {
	float emission = 0.0f;

	if ( !shader || ( shader->surfaceFlags & ( SURF_SKY | SURF_NODRAW ) ) || shader->sort == SS_PORTAL || shader->fogParms
		|| ( shader->contentFlags & ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_FOG ) ) ) {
		return 0.0f;
	}
	if ( shader->emissiveSet ) {
		if ( !shader->emissiveShadows ) {
			return 0.0f;
		}
		// (q3map_surfacelight 1000 gives about the light of a white texture, as below)
		return shader->emissive[3] * 0.001f * ( shader->emissive[0] * 0.3f + shader->emissive[1] * 0.59f + shader->emissive[2] * 0.11f );
	}
	for ( int i = 0; i < shader->numUnfoggedPasses; i++ ) {
		const shaderStage_t	*stage = &shader->stages[i];
		const image_t		*image = stage->bundle[0].image;
		const uint32_t		src = stage->stateBits & GLS_SRCBLEND_BITS, dst = stage->stateBits & GLS_DSTBLEND_BITS;

		if ( !stage->active || !image || stage->bundle[0].isLightmap ) {
			continue;
		}
		if ( stage->glow || ( dst == GLS_DSTBLEND_ONE && ( src == GLS_SRCBLEND_ONE || src == GLS_SRCBLEND_SRC_ALPHA ) ) ) {
			emission += image->averageLuma * VK_RT_StageColor( stage );
		}
	}
	// (q3map_surfacelight 1000 gives about the light of a white texture)
	if ( shader->surfaceLight > 0.0f ) {
		emission += shader->surfaceLight * 0.001f;
	}
	return emission;
}

/*
===============
VK_RT_Patches

The patches of the level, the most powerful VK_RT_MAX_PATCHES of them.
===============
*/
static void VK_RT_Patches( std::vector<vkPatch_t> &patches ) {
	const bmodel_t			*model = &tr.world->bmodels[0];
	std::vector<float>		positions, normals;
	std::vector<uint32_t>	indexes;

	for ( int i = 0; i < model->numSurfaces; i++ ) {
		const msurface_t	*surf = &model->firstSurface[i];
		const float			emission = VK_RT_Emission( surf->shader );
		const size_t		first = patches.size();

		if ( emission <= 0.0f ) {
			continue;
		}
		positions.clear();
		normals.clear();
		indexes.clear();
		VK_RT_SurfaceTriangles( surf, positions, indexes );
		VK_RT_SurfaceNormals( surf, normals );
		for ( size_t t = 0; t + 2 < indexes.size(); t += 3 ) {
			const float	*v[3] = { &positions[indexes[t] * 3], &positions[indexes[t + 1] * 3], &positions[indexes[t + 2] * 3] };
			vec3_t		ab, ac, cross, center, side;
			size_t		j;

			VectorSubtract( v[1], v[0], ab );
			VectorSubtract( v[2], v[0], ac );
			CrossProduct( ab, ac, cross );
			const float area = VectorLength( cross ) * 0.5f;

			if ( area < 0.01f ) {
				continue;
			}
			// The side it lights is the one the surface shows, as the map has it. Not the winding: the triangles of the
			// maps go clockwise seen from the side they show, so the cross product points into the wall, and every
			// panel on a wall lit the side behind it: standing before one, it did not count (2026-11-08).
			if ( normals.size() == positions.size() ) {
				vec3_t shown = { 0.0f, 0.0f, 0.0f };

				for ( int k = 0; k < 3; k++ ) {
					VectorAdd( shown, &normals[indexes[t + k] * 3], shown );
				}
				if ( DotProduct( shown, cross ) < 0.0f ) {
					VectorNegate( cross, cross );
				}
			}
			VectorCopy( cross, side );
			VectorNormalize( side );
			for ( int k = 0; k < 3; k++ ) {
				center[k] = ( v[0][k] + v[1][k] + v[2][k] ) / 3.0f;
			}
			// with a patch of the surface near it that faces alike
			for ( j = first; j < patches.size(); j++ ) {
				vec3_t middle, patchSide;

				VectorScale( patches[j].origin, 1.0f / patches[j].area, middle );
				VectorCopy( patches[j].normal, patchSide );
				VectorNormalize( patchSide );
				if ( Distance( middle, center ) < VK_RT_PATCH_SIZE && DotProduct( patchSide, side ) > 0.7f ) {
					break;
				}
			}
			if ( j == patches.size() ) {
				vkPatch_t patch;

				memset( &patch, 0, sizeof( patch ) );
				ClearBounds( patch.mins, patch.maxs );
				patch.emission = emission;
				patch.surface = i;
				patches.push_back( patch );
			}
			VectorMA( patches[j].origin, area, center, patches[j].origin );
			VectorAdd( patches[j].normal, cross, patches[j].normal );
			patches[j].area += area;
			for ( int k = 0; k < 3; k++ ) {
				AddPointToBounds( v[k], patches[j].mins, patches[j].maxs );
			}
		}
	}
	for ( vkPatch_t &patch : patches ) {
		const float length = VectorLength( patch.normal );

		VectorScale( patch.origin, 1.0f / patch.area, patch.origin );
		// (the cross products are twice the area: a flat patch has a sum of twice its area, a bent one less)
		if ( length < patch.area ) {
			VectorClear( patch.normal );
		} else {
			VectorScale( patch.normal, 1.0f / length, patch.normal );
		}
	}
	if ( patches.size() > VK_RT_MAX_PATCHES ) {
		std::sort( patches.begin(), patches.end(), []( const vkPatch_t &a, const vkPatch_t &b ) {
			return a.emission * a.area > b.emission * b.area;
		} );
		patches.resize( VK_RT_MAX_PATCHES );
	}
}

/*
===============
VK_RT_FitPatches

What shaders/rtcells.comp found for every cell (rt.cellLight: the light of the patches in its list, 1 when the sun
reaches it) against the light grid: the scale of the light of the patches, and the strength of the sun.
===============
*/
static void VK_RT_FitPatches( void ) {
	const world_t	*world = tr.world;
	const float		*light = (const float *)rt.cellLight.mapped;
	const int		numCells = world->lightGridBounds[0] * world->lightGridBounds[1] * world->lightGridBounds[2];
	vec3_t			sun;
	double			sumDE = 0.0, sumEE = 0.0, sunSum = 0.0, sunAll = 0.0;
	int				fitted = 0, sunCount = 0, sunAllCount = 0, valid = 0, listed = 0;

	VectorCopy( tr.sunDirection, sun );
	VectorNormalize( sun );
	for ( int i = 0; i < numCells; i++ ) {
		const mgrid_t	*data;
		vec3_t			dir;

		if ( world->lightGridArray[i] >= world->numGridArrayElements ) {
			continue;
		}
		data = world->lightGridData + world->lightGridArray[i];
		if ( data->styles[0] == LS_LSNONE ) {
			continue;
		}
		const float directed = data->directLight[0][0] * 0.3f + data->directLight[0][1] * 0.59f + data->directLight[0][2] * 0.11f;

		valid++;
		listed += light[i * 2] > 0.0f ? 1 : 0;
		if ( light[i * 2 + 1] > 0.5f ) {
			// the sun shines there: what the grid has from its direction is the sun
			VK_RT_CellDirection( data, dir );
			sunAll += directed;
			sunAllCount++;
			if ( DotProduct( dir, sun ) > 0.98f ) {
				sunSum += directed;
				sunCount++;
			}
		} else if ( light[i * 2] > 0.0f && directed > 4.0f ) {
			sumDE += (double)directed * light[i * 2];
			sumEE += (double)light[i * 2] * light[i * 2];
			fitted++;
		}
	}
	vk_patchScale = sumEE > 0.0 ? (float)( sumDE / sumEE ) : 0.0f;
	vk_sunStrength = sunCount ? (float)( sunSum / sunCount ) : ( sunAllCount ? (float)( sunAll / sunAllCount ) : 0.0f );
	ri.Printf( PRINT_DEVELOPER, "ray tracing: %i light patches, %i of %i cells see some, scale %g from %i cells, the sun %.0f (%i cells)\n",
		(int)vk_patches->size(), listed, valid, vk_patchScale, fitted, vk_sunStrength, sunCount );
}

static void VK_RT_TrianglesBuildInfo( VkAccelerationStructureGeometryKHR *geometry, VkAccelerationStructureBuildGeometryInfoKHR *build,
	const vkBuffer_t *positions, uint32_t numVertexes, const vkBuffer_t *indexes ) {
	VK_RT_Triangles( geometry, VK_RT_Address( positions ), numVertexes, VK_RT_Address( indexes ) );
	memset( build, 0, sizeof( *build ) );
	build->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	build->type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	build->flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	build->mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build->geometryCount = 1;
	build->pGeometries = geometry;
}

// the inputs of a build that the CPU writes once and the device reads once, and the structure itself
static VkDeviceSize VK_RT_PrepareTriangles( const std::vector<float> &positions, const std::vector<uint32_t> &indexes, vkBuffer_t *positionBuffer,
	vkBuffer_t *indexBuffer, vkAccel_t *accel ) {
	VkAccelerationStructureGeometryKHR				geometry;
	VkAccelerationStructureBuildGeometryInfoKHR		build;
	VkAccelerationStructureBuildSizesInfoKHR		sizes = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	const VkBufferUsageFlags						input = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
		| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	const uint32_t									numTriangles = (uint32_t)( indexes.size() / 3 );

	VK_CreateBuffer( positionBuffer, positions.size() * sizeof( float ), input, qtrue );
	memcpy( positionBuffer->mapped, positions.data(), positions.size() * sizeof( float ) );
	VK_CreateBuffer( indexBuffer, indexes.size() * sizeof( uint32_t ), input, qtrue );
	memcpy( indexBuffer->mapped, indexes.data(), indexes.size() * sizeof( uint32_t ) );
	VK_RT_TrianglesBuildInfo( &geometry, &build, positionBuffer, (uint32_t)( positions.size() / 3 ), indexBuffer );
	qvkGetAccelerationStructureBuildSizesKHR( vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &numTriangles, &sizes );
	VK_RT_CreateAccel( accel, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, sizes.accelerationStructureSize );
	return sizes.buildScratchSize;
}

static void VK_RT_DestroyLevel( void ) {
	VK_RT_DestroyAccel( &rt.level );
	for ( vkBrush_t &brush : vk_brushes ) {
		VK_RT_DestroyAccel( &brush.accel );
	}
	vk_brushes.clear();
	vk_numBrushEnts = 0;
	VK_DestroyBuffer( &rt.brushPositions );
	VK_DestroyBuffer( &rt.brushIndexes );
	rt.brushVertexes = 0;
	VK_DestroyBuffer( &rt.positions );
	VK_DestroyBuffer( &rt.indexes );
	VK_DestroyBuffer( &rt.patches );
	VK_DestroyBuffer( &rt.cellLists );
	VK_DestroyBuffer( &rt.cellLight );
	VK_DestroyBuffer( &rt.mapPositions );
	VK_DestroyBuffer( &rt.mapIndexes );
	vk_mapChunks.clear();
	rt.levelReady = rt.levelPending = qfalse;
	rt.cellState = 0;
	vk_patchScale = 0.0f;
	vk_sunStrength = 0.0f;
}

// a storage buffer of the level into binding of the sets of both frames (only while no frame is on its way)
static void VK_RT_WriteLevelSet( uint32_t binding, const vkBuffer_t *buffer ) {
	for ( int i = 0; i < VK_FRAMES; i++ ) {
		VkDescriptorBufferInfo	info;
		VkWriteDescriptorSet	write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

		info.buffer = buffer->buffer;
		info.offset = 0;
		info.range = VK_WHOLE_SIZE;
		write.dstSet = rt.sets[i];
		write.dstBinding = binding;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		write.pBufferInfo = &info;
		qvkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
	}
}

// the build of one brush model: its range of the shared buffers
static void VK_RT_BrushBuildInfo( const vkBrush_t *brush, VkAccelerationStructureGeometryKHR *geometry,
	VkAccelerationStructureBuildGeometryInfoKHR *build ) {
	VK_RT_TrianglesBuildInfo( geometry, build, &rt.brushPositions, rt.brushVertexes, &rt.brushIndexes );
	geometry->geometry.triangles.indexData.deviceAddress += brush->firstIndex * sizeof( uint32_t );
}

// the triangles of the brush models and a structure for each (VK_RT_Brushes), the largest scratch one needs
static VkDeviceSize VK_RT_PrepareBrushes( void ) {
	std::vector<float>		positions;
	std::vector<uint32_t>	indexes;
	const VkBufferUsageFlags	input = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
		| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	VkDeviceSize			scratch = 0;
	int						built = 0;

	vk_brushes.assign( Q_max( tr.world->numBModels, 1 ), vkBrush_t() );
	for ( int b = 1; b < tr.world->numBModels; b++ ) {
		const bmodel_t *brush = &tr.world->bmodels[b];

		vk_brushes[b].firstIndex = (uint32_t)indexes.size();
		for ( int i = 0; i < brush->numSurfaces; i++ ) {
			if ( VK_RT_StopsLight( brush->firstSurface[i].shader ) ) {
				VK_RT_SurfaceTriangles( &brush->firstSurface[i], positions, indexes );
			}
		}
		vk_brushes[b].numIndexes = (uint32_t)indexes.size() - vk_brushes[b].firstIndex;
	}
	if ( indexes.empty() ) {
		vk_brushes.clear();
		return 0;
	}
	rt.brushVertexes = (uint32_t)( positions.size() / 3 );
	VK_CreateBuffer( &rt.brushPositions, positions.size() * sizeof( float ), input, qtrue );
	memcpy( rt.brushPositions.mapped, positions.data(), positions.size() * sizeof( float ) );
	VK_CreateBuffer( &rt.brushIndexes, indexes.size() * sizeof( uint32_t ), input, qtrue );
	memcpy( rt.brushIndexes.mapped, indexes.data(), indexes.size() * sizeof( uint32_t ) );
	for ( vkBrush_t &brush : vk_brushes ) {
		VkAccelerationStructureGeometryKHR			geometry;
		VkAccelerationStructureBuildGeometryInfoKHR	build;
		VkAccelerationStructureBuildSizesInfoKHR	sizes = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		const uint32_t								numTriangles = brush.numIndexes / 3;

		if ( !numTriangles ) {
			brush.numIndexes = 0;
			continue;
		}
		VK_RT_BrushBuildInfo( &brush, &geometry, &build );
		qvkGetAccelerationStructureBuildSizesKHR( vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &numTriangles, &sizes );
		VK_RT_CreateAccel( &brush.accel, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, sizes.accelerationStructureSize );
		brush.scratch = sizes.buildScratchSize;
		scratch = Q_max( scratch, sizes.buildScratchSize );
		built++;
	}
	ri.Printf( PRINT_DEVELOPER, "ray tracing: %i brush models with %i solid triangles\n", built, (int)( indexes.size() / 3 ) );
	return scratch;
}

/*
===============
VK_RT_PrepareLevel

Once per level: the triangles of the level and their acceleration structure, whose build VK_RT_RecordLevel()
records into the command buffer of the frame, the light patches and the lists of the cells that shaders/rtcells.comp
works out after it. The inputs stay until the next level, the build reads them when that frame runs.
===============
*/
static void VK_RT_PrepareLevel( void ) {
	std::vector<float>		positions;
	std::vector<uint32_t>	indexes;
	const VkBufferUsageFlags	traced = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	const world_t			*world = tr.world;
	const int				numCells = world->lightGridData ? world->lightGridBounds[0] * world->lightGridBounds[1] * world->lightGridBounds[2] : 0;
	float					*out;

	VK_WaitIdle();
	VK_RT_DestroyLevel();
	VK_RT_UploadGrid();
	rt.world = tr.world;
	rt.worldLoads = r_worldLoads;
	rt.levelScratch = 0;
	rt.previousValid = qfalse;
	if ( rt.maps ) {
		VK_SM_BuildLevel();		// no structure to build: the cubes of the moving lights draw it
		return;
	}
	vk_patches->clear();

	VK_RT_LevelTriangles( positions, indexes );
	rt.numVertexes = (uint32_t)( positions.size() / 3 );
	rt.numTriangles = (uint32_t)( indexes.size() / 3 );
	if ( !rt.numTriangles ) {
		return;
	}
	rt.levelScratch = VK_RT_PrepareTriangles( positions, indexes, &rt.positions, &rt.indexes, &rt.level );
	rt.levelScratch = Q_max( rt.levelScratch, VK_RT_PrepareBrushes() );

	VK_RT_Patches( *vk_patches );
	VK_CreateBuffer( &rt.patches, sizeof( float ) * 12 * Q_max( (int)vk_patches->size(), 1 ), traced, qtrue );
	out = (float *)rt.patches.mapped;
	for ( const vkPatch_t &patch : *vk_patches ) {
		VectorCopy( patch.origin, out );
		out[3] = patch.emission * patch.area;
		VectorCopy( patch.normal, out + 4 );
		out[7] = Com_Clamp( 1.0f, 32.0f, sqrtf( patch.area / M_PI ) );
		out[8] = 0.5f * Distance( patch.mins, patch.maxs );
		out[9] = out[10] = out[11] = 0.0f;
		out += 12;
	}
	VK_CreateBuffer( &rt.cellLists, sizeof( uint32_t ) * VK_RT_LIST_WORDS * Q_max( numCells, 1 ), traced, qtrue );
	VK_CreateBuffer( &rt.cellLight, sizeof( float ) * 2 * Q_max( numCells, 1 ), traced, qtrue );
	// (no patch in any list until rtcells.comp has worked them out)
	memset( rt.cellLists.mapped, 0xff, sizeof( uint32_t ) * VK_RT_LIST_WORDS * Q_max( numCells, 1 ) );
	VK_RT_WriteLevelSet( 2, &rt.patches );
	VK_RT_WriteLevelSet( 3, &rt.cellLists );
	rt.cellState = ( numCells && !vk_patches->empty() ) ? 1 : 0;
	rt.levelPending = qtrue;
	ri.Printf( PRINT_DEVELOPER, "ray tracing: the level has %u solid triangles, %i light patches, %i cells\n", rt.numTriangles,
		(int)vk_patches->size(), numCells );
}

static void VK_RT_RecordLevel( VkCommandBuffer cmd ) {
	VkAccelerationStructureGeometryKHR				geometry;
	VkAccelerationStructureBuildGeometryInfoKHR		build;
	VkAccelerationStructureBuildRangeInfoKHR		range;
	const VkAccelerationStructureBuildRangeInfoKHR	*ranges = &range;

	VK_RT_TrianglesBuildInfo( &geometry, &build, &rt.positions, rt.numVertexes, &rt.indexes );
	build.dstAccelerationStructure = rt.level.as;
	build.scratchData.deviceAddress = VK_RT_ScratchAt( 0 );
	memset( &range, 0, sizeof( range ) );
	range.primitiveCount = rt.numTriangles;
	qvkCmdBuildAccelerationStructuresKHR( cmd, 1, &build, &ranges );
	VK_RT_BuildAfterBuild( cmd );
	// the brush models one after the other in the same scratch memory
	for ( const vkBrush_t &brush : vk_brushes ) {
		if ( !brush.numIndexes ) {
			continue;
		}
		VK_RT_BrushBuildInfo( &brush, &geometry, &build );
		build.dstAccelerationStructure = brush.accel.as;
		build.scratchData.deviceAddress = VK_RT_ScratchAt( 0 );
		range.primitiveCount = brush.numIndexes / 3;
		qvkCmdBuildAccelerationStructuresKHR( cmd, 1, &build, &ranges );
		VK_RT_BuildAfterBuild( cmd );
	}
	rt.levelPending = qfalse;
	rt.levelReady = qtrue;
}

// VisionJK: the sun is another one (ext.SetLightOverride): which cells see it is worked out again
void VK_RT_SunChanged( void ) {
	if ( rt.levelReady && rt.cellState == 0 && vk_patches && !vk_patches->empty() ) {
		rt.cellState = 1;
	}
}

// shaders/rtcells.comp over the cells of the grid, after the top level of the frame holds the level
static void VK_RT_TraceCells( VkCommandBuffer cmd ) {
	const world_t			*world = tr.world;
	const VkDeviceAddress	patches = VK_RT_Address( &rt.patches ), grid = VK_RT_Address( &rt.grid );
	const VkDeviceAddress	lists = VK_RT_Address( &rt.cellLists ), light = VK_RT_Address( &rt.cellLight );
	const uint32_t			numCells = (uint32_t)( world->lightGridBounds[0] * world->lightGridBounds[1] * world->lightGridBounds[2] );
	struct {
		uint32_t			addresses[8];
		float				origin[4];
		float				size[4];
		uint32_t			bounds[4];		// w: how many patches
		float				sun[4];			// w: 1 when the level has one
	} push;

	push.addresses[0] = (uint32_t)patches;
	push.addresses[1] = (uint32_t)( patches >> 32 );
	push.addresses[2] = (uint32_t)grid;
	push.addresses[3] = (uint32_t)( grid >> 32 );
	push.addresses[4] = (uint32_t)lists;
	push.addresses[5] = (uint32_t)( lists >> 32 );
	push.addresses[6] = (uint32_t)light;
	push.addresses[7] = (uint32_t)( light >> 32 );
	VectorCopy( world->lightGridOrigin, push.origin );
	push.origin[3] = 0.0f;
	VectorCopy( world->lightGridSize, push.size );
	push.size[3] = 0.0f;
	for ( int i = 0; i < 3; i++ ) {
		push.bounds[i] = (uint32_t)world->lightGridBounds[i];
	}
	push.bounds[3] = (uint32_t)vk_patches->size();
	VectorCopy( tr.sunDirection, push.sun );
	VectorNormalize( push.sun );
	push.sun[3] = VectorLengthSquared( tr.sunDirection ) > 0.0f ? 1.0f : 0.0f;

	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rt.cellsPipeline );
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rt.cellsLayout, 0, 1, &rt.sets[vk.frameIndex], 0, NULL );
	qvkCmdPushConstants( cmd, rt.cellsLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( push ), &push );
	qvkCmdDispatch( cmd, ( numCells + 63 ) / 64, 1, 1 );
	// the shadows of the next frames read the lists, the CPU what it found when the frame is done
	VK_RT_Barrier( cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_HOST_READ_BIT | VK_ACCESS_SHADER_READ_BIT );
	rt.cellState = 2;
	rt.cellFrame = vk.frameIndex;
}

/*
===============================================================================

THE PLAYERS

===============================================================================
*/

// a surface of the player ent into his acceleration structure
static void VK_RT_CasterSurface( const trRefEntity_t *ent, CBoneCache *bones, const mdxmSurface_t *surface ) {
	const int	entityNum = (int)( ent - backEnd.refdef.entities );
	vkCaster_t	*caster = NULL;

	if ( rt.castersFrame != backEnd.viewParms.frameCount ) {
		rt.castersFrame = backEnd.viewParms.frameCount;
		rt.numCasters = 0;
	}
	for ( int i = 0; i < rt.numCasters; i++ ) {
		if ( rt.casters[i].entityNum == entityNum ) {
			caster = &rt.casters[i];
			break;
		}
	}
	if ( !caster ) {
		if ( rt.numCasters == VK_RT_MAX_CASTERS ) {
			return;
		}
		caster = &rt.casters[rt.numCasters++];
		caster->entityNum = entityNum;
		caster->numSurfaces = 0;
		VectorCopy( ent->e.origin, caster->origin );
		VectorCopy( ent->e.axis[0], caster->axis[0] );
		VectorCopy( ent->e.axis[1], caster->axis[1] );
		VectorCopy( ent->e.axis[2], caster->axis[2] );
	}
	// a surface of several shaders, or in several batches, goes in once
	for ( int i = 0; i < caster->numSurfaces; i++ ) {
		if ( caster->surfaces[i].surface == surface && caster->surfaces[i].bones == bones ) {
			return;
		}
	}
	if ( caster->numSurfaces < VK_RT_MAX_CASTER_SURFACES ) {
		caster->surfaces[caster->numSurfaces].surface = surface;
		caster->surfaces[caster->numSurfaces].bones = bones;
		caster->numSurfaces++;
	}
}

/*
===============
VK_RT_AddCaster

A Ghoul2 surface went into a batch of the device (VK_AddDeviceMesh): it also goes into the acceleration structure
of its player when his entity casts a shadow and this is the view of the level.
===============
*/
void VK_RT_AddCaster( CBoneCache *bones, const mdxmSurface_t *surface ) {
	const trRefEntity_t *ent = backEnd.currentEntity;

	// (a body cut in two is no caster: each half would cast the whole body)
	if ( !rt.skinPipeline || !ent || ent == &tr.worldEntity || !( ent->e.renderfx & RF_SHADOW_PLANE ) || ent->cut
		|| backEnd.viewParms.isPortal || ( backEnd.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) ) ) {
		return;
	}
	VK_RT_CasterSurface( ent, bones, surface );
}

/*
===============
VK_RT_WantsHidden

Casters came only from what the view draws, so the player himself in first person and everyone just outside the
picture threw no shadow into it. Front end: such a player, near enough to matter, in the view of the level.
===============
*/
qboolean VK_RT_WantsHidden( const trRefEntity_t *ent ) {
	return (qboolean)( rt.skinPipeline && VK_RT_Wanted() && ( ent->e.renderfx & RF_SHADOW_PLANE ) && !ent->cut
		&& !tr.viewParms.isPortal && !tr.viewParms.isPuddle && !tr.viewParms.probeFace
		&& !( tr.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) )
		&& DistanceSquared( ent->e.origin, tr.viewParms.ori.origin ) < VK_RT_HIDDEN_RANGE * VK_RT_HIDDEN_RANGE );
}

// front end: a surface of such a player. It stays out of the list of surfaces, which draws what it is given.
void VK_RT_QueueCaster( const trRefEntity_t *ent, CBoneCache *bones, const mdxmSurface_t *surface ) {
	if ( !VK_RT_WantsHidden( ent ) ) {
		return;
	}
	if ( rt.queuedFrame != tr.viewParms.frameCount ) {
		rt.queuedFrame = tr.viewParms.frameCount;
		rt.numQueued = 0;
	}
	if ( rt.numQueued < VK_RT_MAX_QUEUED ) {
		rt.queued[rt.numQueued].ent = ent;
		rt.queued[rt.numQueued].bones = bones;
		rt.queued[rt.numQueued].surface = surface;
		rt.numQueued++;
	}
}

static void VK_RT_TakeQueued( void ) {
	if ( rt.queuedFrame == backEnd.viewParms.frameCount ) {
		for ( int i = 0; i < rt.numQueued; i++ ) {
			VK_RT_CasterSurface( rt.queued[i].ent, rt.queued[i].bones, rt.queued[i].surface );
		}
	}
	rt.numQueued = 0;
}

// R_Init() ran: the surfaces of the models may be others at the same addresses
void VK_RT_ModelsChanged( void ) {
	rt.numQueued = 0;
	if ( rt.localFirst ) {
		rt.localFirst->clear();
	}
	rt.localIndexesUsed = 0;
	rt.numCasters = 0;
}

// where the triangles of a surface counted from its first vertex are in localIndexes, put there the first time
static qboolean VK_RT_LocalIndexes( const mdxmSurface_t *surface, uint32_t *first ) {
	const auto found = rt.localFirst->find( surface );
	const uint32_t count = (uint32_t)surface->numTriangles * 3;
	const int *triangles = (const int *)( (const byte *)surface + surface->ofsTriangles );

	if ( found != rt.localFirst->end() ) {
		*first = found->second;
		return qtrue;
	}
	if ( rt.localIndexesUsed + count > VK_RT_LOCAL_INDEXES ) {
		return qfalse;
	}
	*first = rt.localIndexesUsed;
	for ( uint32_t i = 0; i < count; i++ ) {
		( (uint32_t *)rt.localIndexes.mapped )[*first + i] = (uint32_t)triangles[i];
	}
	rt.localIndexesUsed += count;
	( *rt.localFirst )[surface] = *first;
	return qtrue;
}

// what one surface of a caster is built from in this frame
typedef struct vkCasterPart_s {
	uint32_t		skinned;		// first position in rt.skinned of the frame
	uint32_t		numVertexes;
	uint32_t		localFirst;		// first index in rt.localIndexes
	uint32_t		numTriangles;
	uint32_t		firstVertex;	// in the buffer of the models
	uint32_t		skeleton;		// rows of the bone buffer
} vkCasterPart_t;

static vkCasterPart_t								vk_parts[VK_RT_MAX_CASTERS][VK_RT_MAX_CASTER_SURFACES];
static int											vk_numParts[VK_RT_MAX_CASTERS];
static VkAccelerationStructureGeometryKHR			vk_partGeometry[VK_RT_MAX_CASTERS][VK_RT_MAX_CASTER_SURFACES];
static VkAccelerationStructureBuildRangeInfoKHR		vk_partRange[VK_RT_MAX_CASTERS][VK_RT_MAX_CASTER_SURFACES];
static VkAccelerationStructureBuildGeometryInfoKHR	vk_casterBuild[VK_RT_MAX_CASTERS];

static void VK_RT_CasterBuildInfo( int c ) {
	const VkDeviceAddress skinned = VK_RT_Address( &rt.skinned[vk.frameIndex] );
	const VkDeviceAddress local = VK_RT_Address( &rt.localIndexes );
	VkAccelerationStructureBuildGeometryInfoKHR *build = &vk_casterBuild[c];

	for ( int i = 0; i < vk_numParts[c]; i++ ) {
		const vkCasterPart_t *part = &vk_parts[c][i];

		VK_RT_Triangles( &vk_partGeometry[c][i], skinned + (VkDeviceAddress)part->skinned * sizeof( float ) * 3, part->numVertexes,
			local + (VkDeviceAddress)part->localFirst * sizeof( uint32_t ) );
		memset( &vk_partRange[c][i], 0, sizeof( vk_partRange[c][i] ) );
		vk_partRange[c][i].primitiveCount = part->numTriangles;
	}
	memset( build, 0, sizeof( *build ) );
	build->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	build->type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	build->flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
	build->mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build->geometryCount = (uint32_t)vk_numParts[c];
	build->pGeometries = vk_partGeometry[c];
}

/*
===============
VK_RT_PrepareCasters

The parts of the casters of this frame and their acceleration structures in the arena of the frame. Returns the
scratch memory all their builds need together (they are built at the same time). Casters that do not fit are
left out.
===============
*/
static VkDeviceSize VK_RT_PrepareCasters( void ) {
	const int		frame = vk.frameIndex;
	VkDeviceSize	arenaUsed = 0, scratch = 0;
	uint32_t		skinnedUsed = 0;
	int				kept = 0;

	// the structures of the frame that used this arena before are no longer read, its fence was waited for
	for ( int i = 0; i < VK_RT_MAX_CASTERS; i++ ) {
		if ( rt.arenaAs[frame][i] ) {
			qvkDestroyAccelerationStructureKHR( vk.device, rt.arenaAs[frame][i], NULL );
			rt.arenaAs[frame][i] = VK_NULL_HANDLE;
		}
	}
	if ( rt.castersFrame != backEnd.viewParms.frameCount ) {
		rt.numCasters = 0;
	}

	for ( int c = 0; c < rt.numCasters; c++ ) {
		vkCaster_t									*caster = &rt.casters[c];
		uint32_t									counts[VK_RT_MAX_CASTER_SURFACES];
		VkAccelerationStructureBuildSizesInfoKHR	sizes = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		const uint32_t								skinnedStart = skinnedUsed;
		int											n = 0;

		for ( int i = 0; i < caster->numSurfaces; i++ ) {
			const mdxmSurface_t	*surface = caster->surfaces[i].surface;
			vkCasterPart_t		*part = &vk_parts[kept][n];
			const vkBuffer_t	*vertexes;
			const int			firstVertex = VK_SkinnedFirstVertex( surface, &vertexes );

			if ( firstVertex < 0 || surface->numTriangles <= 0 || skinnedUsed + (uint32_t)surface->numVerts > VK_RT_SKIN_VERTEXES
				|| !VK_RT_LocalIndexes( surface, &part->localFirst )
				|| !VK_SkeletonBones( caster->surfaces[i].bones, &part->skeleton ) ) {
				continue;
			}
			part->skinned = skinnedUsed;
			part->numVertexes = (uint32_t)surface->numVerts;
			part->numTriangles = (uint32_t)surface->numTriangles;
			part->firstVertex = (uint32_t)firstVertex;
			counts[n] = part->numTriangles;
			skinnedUsed += part->numVertexes;
			n++;
		}
		if ( !n ) {
			skinnedUsed = skinnedStart;
			continue;
		}
		vk_numParts[kept] = n;
		if ( rt.maps ) {
			// the shadow maps draw the parts as they are, nothing is built
			if ( kept != c ) {
				rt.casters[kept] = *caster;
			}
			kept++;
			continue;
		}
		VK_RT_CasterBuildInfo( kept );
		qvkGetAccelerationStructureBuildSizesKHR( vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &vk_casterBuild[kept],
			counts, &sizes );
		if ( arenaUsed + sizes.accelerationStructureSize > VK_RT_CASTER_BYTES ) {
			skinnedUsed = skinnedStart;
			continue;
		}
		if ( kept != c ) {
			rt.casters[kept] = *caster;
		}
		rt.casters[kept].as = rt.arenaAs[frame][kept] = VK_RT_NewAccel( &rt.arena[frame], arenaUsed, sizes.accelerationStructureSize,
			VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, &rt.casters[kept].address );
		rt.casters[kept].scratch = scratch;
		arenaUsed = VK_RT_Align( arenaUsed + sizes.accelerationStructureSize, VK_RT_ACCEL_ALIGN );
		scratch = VK_RT_Align( scratch + sizes.buildScratchSize, rt.scratchAlign );
		kept++;
	}
	rt.numCasters = kept;
	return scratch;
}

// the vertices of every part moved by its bones into the positions of the frame
static void VK_RT_SkinCasters( VkCommandBuffer cmd ) {
	const vkBuffer_t		*vertexes;
	const VkDeviceAddress	skinned = VK_RT_Address( &rt.skinned[vk.frameIndex] );
	VkDeviceAddress			models, bones;
	struct {
		uint32_t			vertexes[2];
		uint32_t			bones[2];
		uint32_t			positions[2];
		uint32_t			skeleton;
		uint32_t			count;
	} push;

	if ( !rt.numCasters ) {
		return;
	}
	VK_SkinnedFirstVertex( NULL, &vertexes );
	models = VK_RT_Address( vertexes );
	bones = VK_RT_Address( &vk.frame->bones );
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rt.skinPipeline );
	for ( int c = 0; c < rt.numCasters; c++ ) {
		for ( int i = 0; i < vk_numParts[c]; i++ ) {
			const vkCasterPart_t	*part = &vk_parts[c][i];
			const VkDeviceAddress	from = models + (VkDeviceAddress)part->firstVertex * sizeof( vkSkinVertex_t );
			const VkDeviceAddress	to = skinned + (VkDeviceAddress)part->skinned * sizeof( float ) * 3;

			push.vertexes[0] = (uint32_t)from;
			push.vertexes[1] = (uint32_t)( from >> 32 );
			push.bones[0] = (uint32_t)bones;
			push.bones[1] = (uint32_t)( bones >> 32 );
			push.positions[0] = (uint32_t)to;
			push.positions[1] = (uint32_t)( to >> 32 );
			push.skeleton = part->skeleton;
			push.count = part->numVertexes;
			qvkCmdPushConstants( cmd, rt.skinLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( push ), &push );
			qvkCmdDispatch( cmd, ( part->numVertexes + 63 ) / 64, 1, 1 );
		}
	}
	// the builds read the positions, or the shadow maps draw them
	if ( rt.maps ) {
		VK_RT_Barrier( cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT );
	} else {
		VK_RT_Barrier( cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_SHADER_READ_BIT );
	}
}

static void VK_RT_RecordCasters( VkCommandBuffer cmd ) {
	const VkAccelerationStructureBuildRangeInfoKHR *ranges[VK_RT_MAX_CASTERS];

	if ( !rt.numCasters ) {
		return;
	}
	for ( int c = 0; c < rt.numCasters; c++ ) {
		// (VK_RT_CasterBuildInfo again: the geometry of a caster that was left out may be in its place)
		VK_RT_CasterBuildInfo( c );
		vk_casterBuild[c].dstAccelerationStructure = rt.casters[c].as;
		vk_casterBuild[c].scratchData.deviceAddress = VK_RT_ScratchAt( rt.casters[c].scratch );
		ranges[c] = vk_partRange[c];
	}
	qvkCmdBuildAccelerationStructuresKHR( cmd, (uint32_t)rt.numCasters, vk_casterBuild, ranges );
	VK_RT_BuildAfterBuild( cmd );
}

/*
===============
VK_RT_RecordTop

The top level of this frame: the level with mask 1, each caster with mask 2 where his entity is, the brush models
near the eye with mask 4 where theirs are.
===============
*/
static void VK_RT_RecordTop( VkCommandBuffer cmd ) {
	const int										frame = vk.frameIndex;
	VkAccelerationStructureInstanceKHR				*instances = (VkAccelerationStructureInstanceKHR *)rt.instances[frame].mapped;
	VkAccelerationStructureGeometryKHR				geometry;
	VkAccelerationStructureBuildGeometryInfoKHR		build = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	VkAccelerationStructureBuildRangeInfoKHR		range;
	const VkAccelerationStructureBuildRangeInfoKHR	*ranges = &range;
	uint32_t										count = 0;

	if ( rt.levelReady ) {
		VkAccelerationStructureInstanceKHR *instance = &instances[count++];

		memset( instance, 0, sizeof( *instance ) );
		instance->transform.matrix[0][0] = instance->transform.matrix[1][1] = instance->transform.matrix[2][2] = 1.0f;
		instance->mask = 0x01;
		instance->flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		instance->accelerationStructureReference = rt.level.address;
	}
	for ( int c = 0; c < rt.numCasters && count < VK_RT_MAX_INSTANCES; c++ ) {
		const vkCaster_t					*caster = &rt.casters[c];
		VkAccelerationStructureInstanceKHR	*instance = &instances[count++];

		memset( instance, 0, sizeof( *instance ) );
		// model to world: origin + axis[0] x + axis[1] y + axis[2] z
		for ( int r = 0; r < 3; r++ ) {
			instance->transform.matrix[r][0] = caster->axis[0][r];
			instance->transform.matrix[r][1] = caster->axis[1][r];
			instance->transform.matrix[r][2] = caster->axis[2][r];
			instance->transform.matrix[r][3] = caster->origin[r];
		}
		instance->instanceCustomIndex = (uint32_t)c;
		instance->mask = 0x02;
		instance->flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		instance->accelerationStructureReference = caster->address;
	}
	for ( int i = 0; i < vk_numBrushEnts && rt.levelReady && count < VK_RT_MAX_INSTANCES; i++ ) {
		const vkBrushEntity_t				*ent = &vk_brushEnts[i];
		VkAccelerationStructureInstanceKHR	*instance = &instances[count++];

		memset( instance, 0, sizeof( *instance ) );
		for ( int r = 0; r < 3; r++ ) {
			instance->transform.matrix[r][0] = ent->axis[0][r];
			instance->transform.matrix[r][1] = ent->axis[1][r];
			instance->transform.matrix[r][2] = ent->axis[2][r];
			instance->transform.matrix[r][3] = ent->origin[r];
		}
		instance->mask = 0x04;
		instance->flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		instance->accelerationStructureReference = vk_brushes[ent->brush].accel.address;
	}

	memset( &geometry, 0, sizeof( geometry ) );
	geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	geometry.geometry.instances.data.deviceAddress = VK_RT_Address( &rt.instances[frame] );
	build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
	build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build.geometryCount = 1;
	build.pGeometries = &geometry;
	build.dstAccelerationStructure = rt.top[frame].as;
	build.scratchData.deviceAddress = VK_RT_ScratchAt( 0 );
	memset( &range, 0, sizeof( range ) );
	range.primitiveCount = count;
	qvkCmdBuildAccelerationStructuresKHR( cmd, 1, &build, &ranges );
	VK_RT_Barrier( cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR );
}

/*
===============================================================================

START AND END

===============================================================================
*/

static void VK_RT_CreateTop( void ) {
	VkAccelerationStructureGeometryKHR				geometry;
	VkAccelerationStructureBuildGeometryInfoKHR		build = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	VkAccelerationStructureBuildSizesInfoKHR		sizes = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	const uint32_t									maxInstances = VK_RT_MAX_INSTANCES;

	memset( &geometry, 0, sizeof( geometry ) );
	geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
	build.geometryCount = 1;
	build.pGeometries = &geometry;
	qvkGetAccelerationStructureBuildSizesKHR( vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build,
		&maxInstances, &sizes );
	rt.topScratch = sizes.buildScratchSize;

	for ( int i = 0; i < VK_FRAMES; i++ ) {
		VkWriteDescriptorSetAccelerationStructureKHR	accelWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
		VkWriteDescriptorSet							write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &accelWrite };

		VK_RT_CreateAccel( &rt.top[i], VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, sizes.accelerationStructureSize );
		VK_CreateBuffer( &rt.instances[i], sizeof( VkAccelerationStructureInstanceKHR ) * VK_RT_MAX_INSTANCES,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, qtrue );
		accelWrite.accelerationStructureCount = 1;
		accelWrite.pAccelerationStructures = &rt.top[i].as;
		write.dstSet = rt.sets[i];
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		qvkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
	}
}

static void VK_RT_CreateCasters( void ) {
	// (the shadow maps draw the positions with the triangles of localIndexes)
	const VkBufferUsageFlags		input = ( rt.maps ? 0 : VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR )
		| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

	rt.localFirst = new std::unordered_map<const void *, uint32_t>;
	VK_CreateBuffer( &rt.localIndexes, sizeof( uint32_t ) * VK_RT_LOCAL_INDEXES, input, qtrue );
	for ( int i = 0; i < VK_FRAMES; i++ ) {
		VK_CreateBuffer( &rt.skinned[i], sizeof( float ) * 3 * VK_RT_SKIN_VERTEXES, input | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, qfalse );
		if ( !rt.maps ) {
			VK_CreateBuffer( &rt.arena[i], VK_RT_CASTER_BYTES, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
				| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, qfalse );
		}
	}

	// shaders/rtskin.comp finds everything by its address, the push constants hold them
	rt.skinLayout = VK_CreateComputeLayout( sizeof( uint32_t ) * 8, NULL );
	rt.skinShader = VK_CreateShader( vk_rtSkinComp, sizeof( vk_rtSkinComp ) );
	rt.skinPipeline = VK_CreateComputePipeline( rt.skinShader, rt.skinLayout );
}

// shaders/rtcells.comp
static void VK_RT_CreateLights( void ) {
	vk_patches = new std::vector<vkPatch_t>;

	// the top level from the set of the frame, the rest by address and in numbers in the push constants
	rt.cellsLayout = VK_CreateComputeLayout( sizeof( uint32_t ) * 24, &rt.setLayout );
	rt.cellsShader = VK_CreateShader( vk_rtCellsComp, sizeof( vk_rtCellsComp ) );
	rt.cellsPipeline = VK_CreateComputePipeline( rt.cellsShader, rt.cellsLayout );

	// shaders/rtlamps.comp in the same layout
	rt.lampsShader = VK_CreateShader( vk_rtLampsComp, sizeof( vk_rtLampsComp ) );
	rt.lampsPipeline = VK_CreateComputePipeline( rt.lampsShader, rt.cellsLayout );
	for ( int i = 0; i < VK_FRAMES; i++ ) {
		VK_CreateBuffer( &rt.lampRays[i], ( sizeof( float ) * 12 + sizeof( float ) ) * VK_RT_LAMP_RAYS,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, qtrue );
		rt.lampAsked[i] = 0;
	}
}

static void VK_RT_CreateLayouts( void ) {
	VkDescriptorSetLayoutBinding			bindings[5];
	VkDescriptorSetLayoutCreateInfo			setInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	VkDescriptorPoolSize					sizes[3];
	VkDescriptorPoolCreateInfo				pool = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	VkDescriptorSetLayout					layouts[VK_FRAMES > 2 ? VK_FRAMES : 2];
	VkDescriptorSetAllocateInfo				alloc = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	VkPipelineLayoutCreateInfo				layout = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	// the globals of the frame, the top level and what the level has, the depth of the scene, the mask before
	VkDescriptorSetLayout					passSets[4];

	// the top level, the light grid, the light patches of the level and the lists of the cells; with the shadow maps
	// the light grid and the atlas in binding 4
	memset( bindings, 0, sizeof( bindings ) );
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	bindings[2] = bindings[1];
	bindings[2].binding = 2;
	bindings[3] = bindings[1];
	bindings[3].binding = 3;
	if ( rt.maps ) {
		bindings[0] = bindings[1];
		bindings[1].binding = 4;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		setInfo.bindingCount = 2;
	} else {
		// (the atlas of the shadow maps, which the shadows from the moving lights read on both ways)
		bindings[4] = bindings[1];
		bindings[4].binding = 4;
		bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		setInfo.bindingCount = 5;
	}
	setInfo.pBindings = bindings;
	VK_CHECK( qvkCreateDescriptorSetLayout( vk.device, &setInfo, NULL, &rt.setLayout ) );

	// those for each frame, and the shadow mask as a texture
	if ( rt.maps ) {
		sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		sizes[0].descriptorCount = VK_FRAMES;
		sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		sizes[1].descriptorCount = VK_FRAMES + 5;
		pool.poolSizeCount = 2;
	} else {
		sizes[0].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		sizes[0].descriptorCount = VK_FRAMES;
		sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		sizes[1].descriptorCount = VK_FRAMES * 3;
		sizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		sizes[2].descriptorCount = VK_FRAMES + 5;
		pool.poolSizeCount = 3;
	}
	pool.maxSets = VK_FRAMES + 5;
	pool.pPoolSizes = sizes;
	VK_CHECK( qvkCreateDescriptorPool( vk.device, &pool, NULL, &rt.pool ) );
	for ( int i = 0; i < VK_FRAMES; i++ ) {
		layouts[i] = rt.setLayout;
	}
	alloc.descriptorPool = rt.pool;
	alloc.descriptorSetCount = VK_FRAMES;
	alloc.pSetLayouts = layouts;
	VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, rt.sets ) );
	layouts[0] = layouts[1] = vk.postSetLayout;
	alloc.descriptorSetCount = 2;
	VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, rt.maskSets ) );
	VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, rt.lightMaskSets ) );
	alloc.descriptorSetCount = 1;
	VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &rt.rawSet ) );

	passSets[0] = vk.frameSetLayout;
	passSets[1] = rt.setLayout;
	passSets[2] = vk.postSetLayout;
	passSets[3] = vk.postSetLayout;
	layout.setLayoutCount = 4;
	layout.pSetLayouts = passSets;
	VK_CHECK( qvkCreatePipelineLayout( vk.device, &layout, NULL, &rt.layout ) );
	passSets[1] = vk.postSetLayout;
	VK_CHECK( qvkCreatePipelineLayout( vk.device, &layout, NULL, &rt.accumLayout ) );
}

void VK_RT_Init( void ) {
	VkPhysicalDeviceAccelerationStructurePropertiesKHR	accel = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
	VkPhysicalDeviceProperties2							props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &accel };

	v_rtDebug = ri.Cvar_Get( "v_rtDebug", "0", CVAR_CHEAT,
		"What the rays of the ray tracing see. 1: towards the sun, 2: from the eye against the depth, 3: the players towards the sun, "
		"4: the light patches, 5: the direction of the strongest light" );
	v_rtShadows = ri.Cvar_Get( "v_rtShadows", "1", CVAR_ARCHIVE_ND, "Traced shadows of the players where the graphics card traces rays (cg_shadows 2). 0: none" );
	v_rtShadowRays = ri.Cvar_Get( "v_rtShadowRays", "4", CVAR_ARCHIVE_ND, "Rays a pixel for the traced shadows of the players, 1 to 16" );
	v_rtSunSize = ri.Cvar_Get( "v_rtSunSize", "1.5", CVAR_ARCHIVE_ND, "How large the sun is in degrees: the softness of its traced shadows" );
	v_rtLightSize = ri.Cvar_Get( "v_rtLightSize", "8", CVAR_ARCHIVE_ND, "How large the light of the light grid is in degrees: the softness of the traced shadows indoors" );
	v_rtMaskScale = ri.Cvar_Get( "v_rtMaskScale", "2", CVAR_ARCHIVE_ND, "The shadows of the players (cg_shadows 2) and of the saber light worked out at half the size of the frame (2, a quarter of the cost) or at its full size (1)" );
	v_rtLamps = ri.Cvar_Get( "v_rtLamps", "1", CVAR_ARCHIVE_ND, "Traced shadows of the players indoors from the panels that glow, where the light grid says the light comes from one. 0: from the light grid alone" );
	memset( &rt, 0, sizeof( rt ) );
	// the players are skinned by their addresses on both ways; without ray tracing they cast their shadows from shadow
	// maps (VK_SM_*)
	if ( !vk.depthReadable || !vk.deviceAddress ) {
		return;
	}
	rt.maps = (qboolean)!vk.rayTracing;
	VK_RT_CreateLayouts();
	VK_RT_CreateCasters();
	rt.resolveFragment = VK_CreateShader( vk_rtResolveFrag, sizeof( vk_rtResolveFrag ) );
	rt.resolvePipeline = VK_CreatePipeline( vk.postVertexShader, rt.resolveFragment, GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO, 8, VKP_GENERIC );
	// the mask of this frame onto the one before, into a target of its own (7)
	rt.accumFragment = VK_CreateShader( vk_rtAccumFrag, sizeof( vk_rtAccumFrag ) );
	rt.accumPipeline = VK_CreatePipeline( vk.postVertexShader, rt.accumFragment, 0, 7, VKP_GENERIC, rt.accumLayout );
	// the shadow maps: the shadows from the moving lights on both ways (rays to them cost over a millisecond with a
	// saber in the hand, the cube of a light 0.03 ms), everything without ray tracing
	VK_SM_Create();
	if ( rt.maps ) {
		return;
	}
	qvkGetPhysicalDeviceProperties2( vk.physicalDevice, &props );
	rt.scratchAlign = accel.minAccelerationStructureScratchOffsetAlignment ? accel.minAccelerationStructureScratchOffsetAlignment : 1;

	VK_RT_CreateTop();
	VK_RT_CreateLights();
	rt.debugFragment = VK_CreateShader( vk_rtDebugFrag, sizeof( vk_rtDebugFrag ) );
	// (8: over the level with the samples of the frame, nothing from a vertex buffer)
	rt.debugPipeline = VK_CreatePipeline( vk.postVertexShader, rt.debugFragment, 0, 8, VKP_GENERIC, rt.layout );

	// the mask: a pass into a target of its own (7: one sample, nothing blended); laid on by the depth with the sets
	// of the screen effects (8), multiplied into the frame
	rt.shadowFragment = VK_CreateShader( vk_rtShadowFrag, sizeof( vk_rtShadowFrag ) );
	rt.shadowPipeline = VK_CreatePipeline( vk.postVertexShader, rt.shadowFragment, 0, 7, VKP_GENERIC, rt.layout );
}

// The rays of the shadows of the players and the masks of the moving lights are drawn at half the size of the frame
// (each pixel for the upper left one of its four, shaders/rtmask.glsl), a quarter of the pixels: the shadows are soft,
// and the frames before smooth them (rtaccum.frag). At the full size (v_rtMaskScale 1) the rays cost 0.68 ms against
// 0.18, laying them on 0.33 against 0.17 and the moving lights 0.17 against 0.11 at 2560 x 1440.
static int VK_RT_MaskScale( void ) {
	return Com_Clampi( 1, 2, v_rtMaskScale->integer );
}

// a pass over all of a target of its own size
static void VK_RT_WholeTarget( VkCommandBuffer cmd, const vkTarget_t *target ) {
	VkViewport	viewport = { 0.0f, 0.0f, (float)target->width, (float)target->height, 0.0f, 1.0f };
	VkRect2D	scissor = { { 0, 0 }, { (uint32_t)target->width, (uint32_t)target->height } };

	qvkCmdSetViewport( cmd, 0, 1, &viewport );
	qvkCmdSetScissor( cmd, 0, 1, &scissor );
}

// the two shadow masks in the size of the frame and the targets of the rays and of the moving lights at the size of
// v_rtMaskScale, made again when that changes (only while no frame is on its way); they are readable from the start,
// what is in them is not used until a frame has drawn one
static void VK_RT_EnsureMask( void ) {
	const int scale = VK_RT_MaskScale();
	const int halfWidth = ( vk.width + scale - 1 ) / scale, halfHeight = ( vk.height + scale - 1 ) / scale;

	if ( rt.mask[0].image && rt.mask[0].width == vk.width && rt.mask[0].height == vk.height && rt.raw.width == halfWidth
		&& rt.raw.height == halfHeight ) {
		return;
	}
	VK_WaitIdle();
	for ( int i = 0; i < 2; i++ ) {
		VK_DestroyTarget( &rt.mask[i] );
		VK_CreateTarget( &rt.mask[i], vk.width, vk.height, vk.final.format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT );
		VK_TargetLayout( vk.frame->cmd, &rt.mask[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
		VK_WriteSet( rt.maskSets[i], vk.postSampler, rt.mask[i].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
		VK_DestroyTarget( &rt.lightMask[i] );
		VK_CreateTarget( &rt.lightMask[i], halfWidth, halfHeight, vk.final.format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT );
		VK_TargetLayout( vk.frame->cmd, &rt.lightMask[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
		VK_WriteSet( rt.lightMaskSets[i], vk.postSampler, rt.lightMask[i].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
		rt.lightShown[i] = -1;
	}
	VK_DestroyTarget( &rt.raw );
	VK_CreateTarget( &rt.raw, halfWidth, halfHeight, vk.final.format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT );
	VK_TargetLayout( vk.frame->cmd, &rt.raw, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_WriteSet( rt.rawSet, vk.postSampler, rt.raw.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	rt.previousValid = qfalse;
	rt.readValid = qfalse;
}

void VK_RT_Shutdown( void ) {
	if ( !rt.layout ) {
		return;
	}
	qvkDestroyPipeline( vk.device, rt.debugPipeline, NULL );
	qvkDestroyShaderModule( vk.device, rt.debugFragment, NULL );
	qvkDestroyPipeline( vk.device, rt.shadowPipeline, NULL );
	qvkDestroyPipeline( vk.device, rt.resolvePipeline, NULL );
	qvkDestroyShaderModule( vk.device, rt.shadowFragment, NULL );
	qvkDestroyShaderModule( vk.device, rt.resolveFragment, NULL );
	VK_DestroyTarget( &rt.mask[0] );
	VK_DestroyTarget( &rt.mask[1] );
	VK_DestroyTarget( &rt.lightMask[0] );
	VK_DestroyTarget( &rt.lightMask[1] );
	VK_DestroyTarget( &rt.raw );
	qvkDestroyPipeline( vk.device, rt.accumPipeline, NULL );
	qvkDestroyShaderModule( vk.device, rt.accumFragment, NULL );
	qvkDestroyPipelineLayout( vk.device, rt.accumLayout, NULL );
	VK_DestroyBuffer( &rt.grid );
	qvkDestroyPipeline( vk.device, rt.cellsPipeline, NULL );
	qvkDestroyShaderModule( vk.device, rt.cellsShader, NULL );
	qvkDestroyPipeline( vk.device, rt.lampsPipeline, NULL );
	qvkDestroyShaderModule( vk.device, rt.lampsShader, NULL );
	for ( int i = 0; i < VK_FRAMES; i++ ) {
		VK_DestroyBuffer( &rt.lampRays[i] );
	}
	qvkDestroyPipelineLayout( vk.device, rt.cellsLayout, NULL );
	delete vk_patches;
	vk_patches = NULL;
	VK_DestroyTarget( &rt.atlas );
	qvkDestroyPipeline( vk.device, rt.depthPipeline, NULL );
	qvkDestroyPipeline( vk.device, rt.depthLevelPipeline, NULL );
	qvkDestroyPipeline( vk.device, rt.smShadowPipeline, NULL );
	qvkDestroyPipeline( vk.device, rt.smLightsPipeline, NULL );
	qvkDestroyShaderModule( vk.device, rt.depthVertex, NULL );
	qvkDestroyShaderModule( vk.device, rt.smShadowFragment, NULL );
	qvkDestroyShaderModule( vk.device, rt.smLightsFragment, NULL );
	qvkDestroyPipelineLayout( vk.device, rt.depthLayout, NULL );
	qvkDestroyPipeline( vk.device, rt.skinPipeline, NULL );
	qvkDestroyShaderModule( vk.device, rt.skinShader, NULL );
	qvkDestroyPipelineLayout( vk.device, rt.skinLayout, NULL );
	for ( int i = 0; i < VK_FRAMES; i++ ) {
		for ( int j = 0; j < VK_RT_MAX_CASTERS; j++ ) {
			if ( rt.arenaAs[i][j] ) {
				qvkDestroyAccelerationStructureKHR( vk.device, rt.arenaAs[i][j], NULL );
			}
		}
		VK_DestroyBuffer( &rt.arena[i] );
		VK_DestroyBuffer( &rt.skinned[i] );
		VK_RT_DestroyAccel( &rt.top[i] );
		VK_DestroyBuffer( &rt.instances[i] );
	}
	VK_DestroyBuffer( &rt.localIndexes );
	delete rt.localFirst;
	VK_RT_DestroyLevel();
	VK_DestroyBuffer( &rt.scratch );
	qvkDestroyPipelineLayout( vk.device, rt.layout, NULL );
	qvkDestroyDescriptorPool( vk.device, rt.pool, NULL );
	qvkDestroyDescriptorSetLayout( vk.device, rt.setLayout, NULL );
	memset( &rt, 0, sizeof( rt ) );
}


/*
===============================================================================

THE SHADOW MAPS (a device without ray tracing)

The players are drawn from their skinned positions (VK_RT_SkinCasters) into tiles of one depth atlas: each into a
tile of his own, seen along the light of the grid at his feet (the direction that lit his model, ent->lightDir), and
all near the two moving lights of VK_RT_PickLights into the six tiles of a cube around each. The views of the tiles
go to the shaders in the rt globals from row VK_SM_FIRST_ROW, four a tile. shaders/smshadow.frag and smlights.frag
look the points of the view up in them and write the same masks the rays write.

===============================================================================
*/

// m = a * b, 4 x 4, the columns one after the other
static void VK_SM_Multiply( const float *a, const float *b, float *m ) {
	for ( int c = 0; c < 4; c++ ) {
		for ( int r = 0; r < 4; r++ ) {
			m[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
		}
	}
}

// the matrix whose rows are the four given (a row: xyz and the constant)
static void VK_SM_FromRows( const float *r0, const float *r1, const float *r2, const float *r3, float *m ) {
	const float *rows[4] = { r0, r1, r2, r3 };

	for ( int r = 0; r < 4; r++ ) {
		for ( int c = 0; c < 4; c++ ) {
			m[c * 4 + r] = rows[r][c];
		}
	}
}

#define VK_SM_EXTENT	80.0f		// half the width of the tile of a player, in units
#define VK_SM_DEPTH		640.0f		// how far before and behind him it reaches along the light

// The direction the light of the grid comes from at p, between its eight cells and without those in walls, as
// shaders/rtshadow.frag GridAt has it (rt.grid, VK_RT_UploadGrid); straight up where there is none. Not
// ent->lightDir: R_SetupEntityLighting adds the moving lights to it, and the saber in his hand turned a player's
// light towards itself.
static void VK_RT_GridDirection( const vec3_t p, vec3_t dir ) {
	const world_t	*world = tr.world;
	const float		*cells = (const float *)rt.grid.mapped;
	vec3_t			g;
	int				base[3];
	float			f[3];

	VectorClear( dir );
	if ( world && world->lightGridData && cells ) {
		VectorSubtract( p, world->lightGridOrigin, g );
		for ( int k = 0; k < 3; k++ ) {
			g[k] *= world->lightGridInverseSize[k];
			base[k] = (int)floorf( g[k] );
			f[k] = g[k] - (float)base[k];
		}
		for ( int i = 0; i < 8; i++ ) {
			int c[3];
			float w = 1.0f;

			for ( int k = 0; k < 3; k++ ) {
				const int corner = ( i >> k ) & 1;

				c[k] = Com_Clampi( 0, world->lightGridBounds[k] - 1, base[k] + corner );
				w *= corner ? f[k] : 1.0f - f[k];
			}
			const float *cell = cells + ( c[0] + c[1] * world->lightGridBounds[0] + c[2] * world->lightGridBounds[0] * world->lightGridBounds[1] ) * 12;
			if ( cell[3] == 0.0f ) {
				continue;	// in a wall
			}
			VectorMA( dir, w, cell + 8, dir );
		}
	}
	if ( VectorNormalize( dir ) < 0.001f ) {
		VectorSet( dir, 0.0f, 0.0f, 1.0f );
	}
}

// the tile of a player: looking along the light that comes from dir (towards the light), around center
static void VK_SM_PlayerView( const vec3_t center, const vec3_t dir, float *m ) {
	vec3_t	f, r, u;
	float	r0[4], r1[4], r2[4], r3[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

	VectorNegate( dir, f );
	VectorNormalize( f );
	PerpendicularVector( r, f );
	CrossProduct( r, f, u );
	VectorScale( r, 1.0f / VK_SM_EXTENT, r0 );
	r0[3] = -DotProduct( center, r ) / VK_SM_EXTENT;
	VectorScale( u, 1.0f / VK_SM_EXTENT, r1 );
	r1[3] = -DotProduct( center, u ) / VK_SM_EXTENT;
	// the depth 0 to 1 from VK_SM_DEPTH before him to as far behind
	VectorScale( f, 0.5f / VK_SM_DEPTH, r2 );
	r2[3] = ( -DotProduct( center, f ) + VK_SM_DEPTH ) * 0.5f / VK_SM_DEPTH;
	VK_SM_FromRows( r0, r1, r2, r3, m );
}

// face of a cube around center (0 +x, 1 -x, 2 +y, 3 -y, 4 +z, 5 -z, as smlights.frag picks them), 90 degrees
static void VK_SM_FaceView( const vec3_t center, int face, float zFar, float *m ) {
	const float	zNear = 4.0f;
	const float	a = zFar / ( zFar - zNear ), b = -zFar * zNear / ( zFar - zNear );
	vec3_t		f = { 0.0f, 0.0f, 0.0f }, r, u;
	float		r0[4], r1[4], r2[4], r3[4];

	f[face >> 1] = ( face & 1 ) ? -1.0f : 1.0f;
	PerpendicularVector( r, f );
	CrossProduct( r, f, u );
	VectorCopy( r, r0 );
	r0[3] = -DotProduct( center, r );
	VectorCopy( u, r1 );
	r1[3] = -DotProduct( center, u );
	VectorScale( f, a, r2 );
	r2[3] = -DotProduct( center, f ) * a + b;
	VectorCopy( f, r3 );
	r3[3] = -DotProduct( center, f );
	VK_SM_FromRows( r0, r1, r2, r3, m );
}

// the place of an entity: its axes and origin, from its space into the world
static void VK_SM_Model( const vkCaster_t *caster, float *m ) {
	for ( int c = 0; c < 3; c++ ) {
		VectorCopy( caster->axis[c], m + c * 4 );
		m[c * 4 + 3] = 0.0f;
	}
	VectorCopy( caster->origin, m + 12 );
	m[15] = 1.0f;
}

static void VK_SM_Create( void ) {
	VkPushConstantRange								push = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( float ) * 16 };
	VkPipelineLayoutCreateInfo						layout = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	VkPipelineShaderStageCreateInfo					stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	VkVertexInputBindingDescription					binding = { 0, sizeof( float ) * 3, VK_VERTEX_INPUT_RATE_VERTEX };
	VkVertexInputAttributeDescription				attribute = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
	VkPipelineVertexInputStateCreateInfo			input = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	VkPipelineInputAssemblyStateCreateInfo			assembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	VkPipelineViewportStateCreateInfo				viewport = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	VkPipelineRasterizationStateCreateInfo			raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	VkPipelineMultisampleStateCreateInfo			multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	VkPipelineDepthStencilStateCreateInfo			depth = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	VkPipelineColorBlendStateCreateInfo				blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	const VkDynamicState							dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo				dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	VkPipelineRenderingCreateInfo					rendering = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	VkGraphicsPipelineCreateInfo					info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rendering };

	VK_CreateTarget( &rt.atlas, VK_SM_COLUMNS * VK_SM_TILE, VK_SM_ROWS * VK_SM_TILE, VK_FORMAT_D32_SFLOAT,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, VK_SAMPLE_COUNT_1_BIT );
	for ( int i = 0; i < VK_FRAMES; i++ ) {
		VK_WriteSetBinding( rt.sets[i], 4, vk.postSampler, rt.atlas.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	}

	// the depth of the players, nothing else: one vertex stream of positions, no fragment shader
	layout.pushConstantRangeCount = 1;
	layout.pPushConstantRanges = &push;
	VK_CHECK( qvkCreatePipelineLayout( vk.device, &layout, NULL, &rt.depthLayout ) );
	rt.depthVertex = VK_CreateShader( vk_smDepthVert, sizeof( vk_smDepthVert ) );
	stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
	stage.module = rt.depthVertex;
	stage.pName = "main";
	input.vertexBindingDescriptionCount = 1;
	input.pVertexBindingDescriptions = &binding;
	input.vertexAttributeDescriptionCount = 1;
	input.pVertexAttributeDescriptions = &attribute;
	assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	viewport.viewportCount = viewport.scissorCount = 1;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE;
	raster.lineWidth = 1.0f;
	// (against the stripes of a surface that shadows itself)
	raster.depthBiasEnable = VK_TRUE;
	raster.depthBiasConstantFactor = 2.0f;
	raster.depthBiasSlopeFactor = 2.0f;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	depth.depthTestEnable = depth.depthWriteEnable = VK_TRUE;
	depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamicStates;
	rendering.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
	info.stageCount = 1;
	info.pStages = &stage;
	info.pVertexInputState = &input;
	info.pInputAssemblyState = &assembly;
	info.pViewportState = &viewport;
	info.pRasterizationState = &raster;
	info.pMultisampleState = &multisample;
	info.pDepthStencilState = &depth;
	info.pColorBlendState = &blend;
	info.pDynamicState = &dynamic;
	info.layout = rt.depthLayout;
	VK_CHECK( qvkCreateGraphicsPipelines( vk.device, vk.pipelineCache, 1, &info, NULL, &rt.depthPipeline ) );
	// The level: only the side that faces the light, so a light lying just under the floor it hit (an explosion) still
	// lights the room. VK_SM_FaceView has right x up = -forward, so a triangle with its right-handed normal to the light
	// has a negative area for the device, which counts that as its back: the front is what is culled.
	raster.cullMode = VK_CULL_MODE_FRONT_BIT;
	raster.depthBiasSlopeFactor = 1.5f;		// (more pushes a wall seen edge on behind what stands at its foot)
	VK_CHECK( qvkCreateGraphicsPipelines( vk.device, vk.pipelineCache, 1, &info, NULL, &rt.depthLevelPipeline ) );

	// the masks from the atlas, as the rays write them
	rt.smShadowFragment = VK_CreateShader( vk_smShadowFrag, sizeof( vk_smShadowFrag ) );
	rt.smShadowPipeline = VK_CreatePipeline( vk.postVertexShader, rt.smShadowFragment, 0, 7, VKP_GENERIC, rt.layout );
	// (where the device traces rays the level takes the light of the moving lights as well, shaders/rtlightmask.frag)
	rt.smLightsFragment = rt.maps ? VK_CreateShader( vk_smLightsFrag, sizeof( vk_smLightsFrag ) )
		: VK_CreateShader( vk_rtLightMaskFrag, sizeof( vk_rtLightMaskFrag ) );
	rt.smLightsPipeline = VK_CreatePipeline( vk.postVertexShader, rt.smLightsFragment, 0, 7, VKP_GENERIC, rt.layout );
}

// the parts of caster c into the tile that is set up, with the view m of that tile
static void VK_SM_DrawCaster( VkCommandBuffer cmd, int c, const float *view ) {
	float model[16], mvp[16];

	VK_SM_Model( &rt.casters[c], model );
	VK_SM_Multiply( view, model, mvp );
	qvkCmdPushConstants( cmd, rt.depthLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( mvp ), mvp );
	for ( int i = 0; i < vk_numParts[c]; i++ ) {
		const vkCasterPart_t *part = &vk_parts[c][i];

		qvkCmdDrawIndexed( cmd, part->numTriangles * 3, 1, part->localFirst, (int32_t)part->skinned, 0 );
	}
}

static void VK_SM_Tile( VkCommandBuffer cmd, int tile ) {
	const VkViewport	viewport = { (float)( ( tile % VK_SM_COLUMNS ) * VK_SM_TILE ), (float)( ( tile / VK_SM_COLUMNS ) * VK_SM_TILE ),
		(float)VK_SM_TILE, (float)VK_SM_TILE, 0.0f, 1.0f };
	const VkRect2D		scissor = { { ( tile % VK_SM_COLUMNS ) * VK_SM_TILE, ( tile / VK_SM_COLUMNS ) * VK_SM_TILE }, { VK_SM_TILE, VK_SM_TILE } };

	qvkCmdSetViewport( cmd, 0, 1, &viewport );
	qvkCmdSetScissor( cmd, 0, 1, &scissor );
}

// the chunks of the level around center that this face of its cube sees, in as few draws as they allow
static void VK_SM_DrawLevel( VkCommandBuffer cmd, const float *view, const vec3_t center, int face ) {
	const int	a = face >> 1, b = ( a + 1 ) % 3, c = ( a + 2 ) % 3;
	uint32_t	first = 0, count = 0;

	qvkCmdPushConstants( cmd, rt.depthLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( float ) * 16, view );
	for ( const uint32_t n : vk_mapNear ) {
		const vkMapChunk_t	*chunk = &vk_mapChunks[n];
		vec3_t				lo, hi;

		VectorSubtract( chunk->mins, center, lo );
		VectorSubtract( chunk->maxs, center, hi );
		// (the face is the pyramid where the axis a is at least as far as the two others)
		const float along = ( face & 1 ) ? -lo[a] : hi[a];

		if ( along + hi[b] < 0.0f || along - lo[b] < 0.0f || along + hi[c] < 0.0f || along - lo[c] < 0.0f ) {
			continue;
		}
		if ( count && first + count == chunk->firstIndex ) {
			count += chunk->numIndexes;
			continue;
		}
		if ( count ) {
			qvkCmdDrawIndexed( cmd, count, 1, first, 0, 0 );
		}
		first = chunk->firstIndex;
		count = chunk->numIndexes;
	}
	if ( count ) {
		qvkCmdDrawIndexed( cmd, count, 1, first, 0, 0 );
	}
}

// the chunks within reach of a point, for the six faces of its cube
static void VK_SM_NearChunks( const vec3_t center, float reach ) {
	vk_mapNear.clear();
	for ( size_t n = 0; n < vk_mapChunks.size(); n++ ) {
		const vkMapChunk_t	*chunk = &vk_mapChunks[n];
		float				d = 0.0f;

		for ( int k = 0; k < 3; k++ ) {
			const float out = center[k] < chunk->mins[k] ? chunk->mins[k] - center[k] : ( center[k] > chunk->maxs[k] ? center[k] - chunk->maxs[k] : 0.0f );

			d += out * out;
		}
		if ( d < reach * reach ) {
			vk_mapNear.push_back( (uint32_t)n );
		}
	}
}

/*
===============
VK_SM_DrawAtlas

The players of this frame, and without rays the level around the moving lights, into the atlas, the views of the
tiles into rows (the rt globals of the frame). After
VK_RT_SkinCasters and VK_RT_PickLights. No target is begun before and after.
===============
*/
static void VK_SM_DrawAtlas( VkCommandBuffer cmd, float (*rows)[4] ) {
	VkRenderingAttachmentInfo	depthInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	VkRenderingInfo				info = { VK_STRUCTURE_TYPE_RENDERING_INFO };
	const VkDeviceSize			zero = 0;
	float						view[16];
	struct {
		int		tile;
		vec3_t	center;
		float	zFar;
	}							cubes[VK_SM_TILES / 6];
	int							numCubes = 0;

	VK_TargetLayout( cmd, &rt.atlas, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );
	depthInfo.imageView = rt.atlas.view;
	depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	depthInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthInfo.clearValue.depthStencil.depth = 1.0f;
	info.renderArea.extent.width = (uint32_t)rt.atlas.width;
	info.renderArea.extent.height = (uint32_t)rt.atlas.height;
	info.layerCount = 1;
	info.pDepthAttachment = &depthInfo;
	qvkCmdBeginRendering( cmd, &info );
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rt.depthPipeline );
	vk.boundPipeline = rt.depthPipeline;
	qvkCmdBindVertexBuffers( cmd, 0, 1, &rt.skinned[vk.frameIndex].buffer, &zero );
	qvkCmdBindIndexBuffer( cmd, rt.localIndexes.buffer, 0, VK_INDEX_TYPE_UINT32 );
	vk.boundVertexBuffer = vk.boundIndexBuffer = VK_NULL_HANDLE;

	// each player in the light of the grid at his feet
	for ( int c = 0; c < rt.numCasters; c++ ) {
		vec3_t				center, dir;

		VectorCopy( rt.casters[c].origin, center );
		center[2] += 8.0f;
		VK_RT_GridDirection( center, dir );
		VK_SM_PlayerView( center, dir, view );
		memcpy( rows[VK_SM_FIRST_ROW + c * 4], view, sizeof( view ) );
		VK_SM_Tile( cmd, c );
		VK_SM_DrawCaster( cmd, c, view );
	}
	// every player that a moving light reaches into the cubes along it: a blade casts from three points along it, so
	// the shadow is soft along the blade and sweeps as it turns (smlights.frag blends the three). Each point jumps to
	// a new place in its third of the blade every frame (rows[21] y, z, w for the shader): the light of a blade
	// flickers, and a shadow from fixed points stood still and hard like the old stencil shadows (their word
	// 2026-11-08: they wanted it to jitter with the light).
	float along[VK_SM_POINTS];
	for ( int point = 0; point < VK_SM_POINTS; point++ ) {
		along[point] = ( (float)point + 0.1f + 0.8f * Q_flrand( 0.0f, 1.0f ) ) / (float)VK_SM_POINTS;
		rows[21][1 + point] = along[point];
	}
	for ( int k = 0; k < VK_RT_LIGHTS; k++ ) {
		const vkRtLight_t	*light = &rt.lights[k];

		if ( !light->active ) {
			continue;
		}
		const float zFar = light->range + Distance( light->start, light->end ) + 64.0f;
		for ( int point = 0; point < ( light->tube ? VK_SM_POINTS : 1 ); point++ ) {
			vec3_t center;

			VectorSubtract( light->end, light->start, center );
			VectorMA( light->start, light->tube ? along[point] : 0.0f, center, center );
			// A muzzle flash or the charge of a shot right at a body threw a huge second shadow of the shooter across the
			// room. With rays the shader makes it a light one (lightmask.glsl CubeShadow). Without them the level is in
			// these cubes too and must stay dark behind a wall, so the player nearest to such a point casts nothing from
			// it, anyone else does. A blade keeps its shadow.
			int own = -1;
			if ( rt.maps && !light->tube ) {
				float nearest = VK_SM_OWN_LIGHT;

				for ( int c = 0; c < rt.numCasters; c++ ) {
					const float away = Distance( rt.casters[c].origin, center );

					if ( away < nearest ) {
						nearest = away;
						own = c;
					}
				}
			}
			cubes[numCubes].tile = VK_SM_LIGHT_TILE( k, point );
			VectorCopy( center, cubes[numCubes].center );
			cubes[numCubes].zFar = zFar;
			numCubes++;
			for ( int face = 0; face < 6; face++ ) {
				const int tile = VK_SM_LIGHT_TILE( k, point ) + face;

				VK_SM_FaceView( center, face, zFar, view );
				memcpy( rows[VK_SM_FIRST_ROW + tile * 4], view, sizeof( view ) );
				VK_SM_Tile( cmd, tile );
				for ( int c = 0; c < rt.numCasters; c++ ) {
					if ( c != own && Distance( rt.casters[c].origin, center ) < zFar ) {
						VK_SM_DrawCaster( cmd, c, view );
					}
				}
			}
		}
	}
	if ( !vk_mapChunks.empty() ) {
		qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rt.depthLevelPipeline );
		vk.boundPipeline = rt.depthLevelPipeline;
		qvkCmdBindVertexBuffers( cmd, 0, 1, &rt.mapPositions.buffer, &zero );
		qvkCmdBindIndexBuffer( cmd, rt.mapIndexes.buffer, 0, VK_INDEX_TYPE_UINT32 );
		for ( int i = 0; i < numCubes; i++ ) {
			VK_SM_NearChunks( cubes[i].center, cubes[i].zFar );
			for ( int face = 0; face < 6; face++ ) {
				const int tile = cubes[i].tile + face;

				VK_SM_Tile( cmd, tile );
				VK_SM_DrawLevel( cmd, rows[VK_SM_FIRST_ROW + tile * 4], cubes[i].center, face );
			}
			// the doors and lifts in reach, where they are now
			for ( int b = 0; b < vk_numBrushEnts; b++ ) {
				const vkBrushEntity_t	*ent = &vk_brushEnts[b];
				const vkBrush_t			*brush = &vk_brushes[ent->brush];
				float					model[16], mvp[16];

				if ( Distance( ent->center, cubes[i].center ) - ent->radius > cubes[i].zFar ) {
					continue;
				}
				for ( int c = 0; c < 3; c++ ) {
					VectorCopy( ent->axis[c], model + c * 4 );
					model[c * 4 + 3] = 0.0f;
				}
				VectorCopy( ent->origin, model + 12 );
				model[15] = 1.0f;
				for ( int face = 0; face < 6; face++ ) {
					const int tile = cubes[i].tile + face;

					VK_SM_Multiply( rows[VK_SM_FIRST_ROW + tile * 4], model, mvp );
					VK_SM_Tile( cmd, tile );
					qvkCmdPushConstants( cmd, rt.depthLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( mvp ), mvp );
					qvkCmdDrawIndexed( cmd, brush->numIndexes, 1, brush->firstIndex, 0, 0 );
				}
			}
		}
	}
	qvkCmdEndRendering( cmd );
	VK_TargetLayout( cmd, &rt.atlas, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
}

/*
===============================================================================

THE PASSES

===============================================================================
*/

/*
===============
VK_RT_PickLights

The moving lights with shadows this frame, the brightest and nearest to the eye: a saber in the hand of the player,
the bolt flying past, the flash of a clash or a blast. Blades cost three cubes, so at most VK_RT_TUBES of them, and
they come first in rt.lights. Any light can be hidden by the level (by rays, or in the cubes of the shadow maps); only
where a level has no mesh for the cubes a light has to reach a player to cast anything.
===============
*/
static void VK_RT_PickLights( void ) {
	vkRtLight_t	picked[VK_RT_LIGHTS];
	float		scores[VK_RT_LIGHTS] = { 0.0f };
	int			count = 0, next = 0;

	memset( rt.lights, 0, sizeof( rt.lights ) );
	for ( int l = 0; l < backEnd.refdef.num_dlights; l++ ) {
		const dlight_t *dl = &backEnd.refdef.dlights[l];
		vkRtLight_t light;
		float score, nearest = 1e9f;
		int slot = -1, tubes = 0;

		memset( &light, 0, sizeof( light ) );
		light.active = qtrue;
		light.tube = (qboolean)( dl->lineRange > 0.0f );
		if ( light.tube ) {
			VectorCopy( dl->lineStart, light.start );
			VectorCopy( dl->lineEnd, light.end );
			light.range = dl->lineRange;
		} else {
			VectorCopy( dl->origin, light.start );
			VectorCopy( dl->origin, light.end );
			light.range = dl->radius;
			light.inSolid = R_PointInSolid( dl->origin );
		}
		light.power = ( dl->color[0] * 0.3f + dl->color[1] * 0.59f + dl->color[2] * 0.11f ) * Com_Clamp( 0.0f, 8.0f, v_dlightScale->value );
		if ( rt.maps && vk_mapChunks.empty() ) {
			for ( int c = 0; c < rt.numCasters; c++ ) {
				nearest = Q_min( nearest, Distance( dl->origin, rt.casters[c].origin ) );
			}
			if ( nearest > dl->radius + 64.0f ) {
				continue;
			}
		}
		score = ( dl->color[0] * 0.3f + dl->color[1] * 0.59f + dl->color[2] * 0.11f ) * light.range
			/ ( 1.0f + Distance( dl->origin, backEnd.viewParms.ori.origin ) / 512.0f );
		if ( score <= 0.0f ) {
			continue;
		}
		for ( int i = 0; i < count; i++ ) {
			tubes += picked[i].tube ? 1 : 0;
		}
		if ( light.tube && tubes >= VK_RT_TUBES ) {
			for ( int i = 0; i < count; i++ ) {
				if ( picked[i].tube && ( slot < 0 || scores[i] < scores[slot] ) ) {
					slot = i;
				}
			}
		} else if ( count < VK_RT_LIGHTS ) {
			slot = count++;
			scores[slot] = 0.0f;
		} else {
			for ( int i = 0; i < count; i++ ) {
				if ( slot < 0 || scores[i] < scores[slot] ) {
					slot = i;
				}
			}
		}
		if ( score > scores[slot] ) {
			scores[slot] = score;
			picked[slot] = light;
		}
	}
	for ( int i = 0; i < count; i++ ) {
		if ( picked[i].tube ) {
			rt.lights[next++] = picked[i];
		}
	}
	for ( int i = 0; i < count; i++ ) {
		if ( !picked[i].tube ) {
			rt.lights[next++] = picked[i];
		}
	}
}

/*
===============
VK_RT_PickLamps

Rows 255 on (VK_RT_LAMP_ROW), four for each player i: the three panels that cast his shadow
(shaders/rtshadow.frag GridShadow), where each is and the radius of its disk in w, then their shares of his light
in x, y, z and the share of the direction of the grid in w.

The levels keep no lights: what lit their lightmaps were mostly light entities that q3map2 left out, and a panel
that glows may have given none. The light grid knows from where the light at a player came, but as an average of
the cell. So the panels near him (the lists of the eight cells around him, shaders/rtcells.comp) that give light
from about that side cast his shadow from where they are, each by its share of the light they give him together:
the shadows have the length a lamp gives and spread under one above him. Where no panel fits, the grid casts it.

- How much light a panel gives him is worked out where he stands, every frame, as rtcells.comp does it for the
  middle of a cell (its power, the side it lights, the square of the distance): the light of a cell's list is that
  of its middle, 64 units or more away, and the panel right before him lost to one further off (2026-11-08).
- A panel more than 80 degrees off the grid casts nothing (their choice 2026-11-08: the panels checked by the grid;
  the grid has the average of all the lights, so a panel on one side of a room is often 60 degrees off it), but one
  within 96 units that lights his side counts whatever the grid says, and from there to 192 units less and less so:
  before a panel on a wall its direction is flat and the grid points up.
- Whether he sees it is asked of the device from where he stands (shaders/rtlamps.comp, VK_RT_TraceLamps), the
  answer read when that frame is done, VK_FRAMES later; until then a panel of a list counts as seen (the middle of
  that cell sees it).
- The shares follow over about 0.15 s, so the shadows hand over from lamp to lamp; one panel for all of the light
  made the shadow jump as he walked (2026-11-08).
===============
*/
#define VK_RT_LAMP_ROWS		4
#define VK_RT_LAMP_ROW		( VK_RT_VEC4S - VK_RT_CASTERS * VK_RT_LAMP_ROWS )
#define VK_RT_LAMP_SHOWN	3		// panels a player casts from in the shader
#define VK_RT_LAMP_KEPT		6		// panels he remembers with their shares
#define VK_RT_LAMP_CANDIDATES	( 8 * VK_RT_CELL_LIST + VK_RT_LAMP_KEPT )

// what a player had the frame before: found again by where he stands
typedef struct vkLampMemory_s {
	vec3_t	origin;
	int		count;
	int		lamp[VK_RT_LAMP_KEPT];		// the patches
	float	share[VK_RT_LAMP_KEPT];		// the share of his light of each
} vkLampMemory_t;

// a panel for a player: the light it gives him where he stands, and how far it counts as coming from the side of
// the grid (0 to 1)
static void VK_RT_LampLight( const vkPatch_t *patch, const vec3_t middle, const vec3_t dir, float *light, float *side ) {
	vec3_t		to;
	float		facing = 1.0f;

	*light = *side = 0.0f;
	VectorSubtract( patch->origin, middle, to );
	const float d = VectorNormalize( to );
	if ( d < 1.0f ) {
		return;
	}
	// (a panel lights the side its normal shows, a zero normal all sides)
	if ( VectorLengthSquared( patch->normal ) > 0.5f ) {
		facing = -DotProduct( patch->normal, to );
		if ( facing <= 0.05f ) {
			return;
		}
	}
	const float radius = Com_Clamp( 1.0f, 32.0f, sqrtf( patch->area / M_PI ) );
	*light = patch->emission * patch->area * facing / Q_max( d * d, radius * radius + 16.0f );
	// 1 within 60 degrees of the grid, 0 from 80 on; near it counts anyway
	float t = Com_Clamp( 0.0f, 1.0f, ( DotProduct( to, dir ) - 0.17f ) / ( 0.5f - 0.17f ) );
	float n = Com_Clamp( 0.0f, 1.0f, ( 192.0f - d ) / ( 192.0f - 96.0f ) );
	t = t * t * ( 3.0f - 2.0f * t );
	n = n * n * ( 3.0f - 2.0f * n );
	*side = Q_max( t, n );
}

// what shaders/rtlamps.comp answered for the frame that last had this place (VK_RT_TakeAnswers): from where, which
// patch, how much of it was seen; asked by VK_RT_PickLamps for the players
static vec3_t	answerFrom[VK_RT_LAMP_RAYS];
static int		answerPatch[VK_RT_LAMP_RAYS];
static float	answer[VK_RT_LAMP_RAYS];
static int		answers;

static void VK_RT_TakeAnswers( void ) {
	const int f = vk.frameIndex;

	answers = 0;
	if ( !rt.maps && rt.lampRays[f].mapped ) {
		answers = rt.lampAsked[f];
		memcpy( answerFrom, rt.lampAskedFrom[f], sizeof( vec3_t ) * answers );
		memcpy( answerPatch, rt.lampAskedPatch[f], sizeof( int ) * answers );
		memcpy( answer, (const float *)rt.lampRays[f].mapped + VK_RT_LAMP_RAYS * 12, sizeof( float ) * answers );
	}
	rt.lampAsked[f] = 0;
}

// how much of patch the device saw from (near) from, -1: not asked
static float VK_RT_Seen( int patch, const vec3_t from ) {
	for ( int k = 0; k < answers; k++ ) {
		if ( answerPatch[k] == patch && Distance( answerFrom[k], from ) < 64.0f ) {
			return answer[k];
		}
	}
	return -1.0f;
}

// asks the device how much of patch is seen from from, in the frames after (shaders/rtlamps.comp)
static void VK_RT_AskSeen( int patch, const vec3_t from ) {
	const int	f = vk.frameIndex;
	const vkPatch_t	*p = &( *vk_patches )[patch];

	if ( rt.lampAsked[f] >= VK_RT_LAMP_RAYS || !rt.lampRays[f].mapped ) {
		return;
	}
	const int	q = rt.lampAsked[f]++;
	float		*row = (float *)rt.lampRays[f].mapped + q * 12;

	VectorCopy( from, row );
	row[3] = 0.0f;
	VectorCopy( p->origin, row + 4 );
	row[7] = Com_Clamp( 1.0f, 32.0f, sqrtf( p->area / M_PI ) );
	VectorCopy( p->normal, row + 8 );
	row[11] = 0.0f;
	VectorCopy( from, rt.lampAskedFrom[f][q] );
	rt.lampAskedPatch[f][q] = patch;
}

typedef struct {
	vec3_t	origin, dir;
	float	share;
} vkPlayerLamps_t;

static vkPlayerLamps_t	vk_playerLamps[VK_RT_CASTERS];
static int				vk_numPlayerLamps;

static void VK_RT_PickLamps( float (*rows)[4] ) {
	static vkLampMemory_t	last[VK_RT_CASTERS];
	static int				lastCount;
	static float			lastTime;
	vkLampMemory_t			now[VK_RT_CASTERS];
	const world_t			*world = tr.world;
	const int				count = Q_min( rt.numCasters, VK_RT_CASTERS );
	const int				f = vk.frameIndex;
	const qboolean			ready = (qboolean)( v_rtLamps->integer && !rt.maps && world && world->lightGridData && vk_patches
		&& !vk_patches->empty() && vk_patchScale > 0.0f && rt.cellState == 0 && rt.cellLists.mapped && rt.lampRays[f].mapped );
	const float				dt = Com_Clamp( 0.0f, 0.25f, backEnd.refdef.floatTime - lastTime );
	const float				follow = 1.0f - expf( -dt / 0.15f );

	lastTime = backEnd.refdef.floatTime;

	for ( int i = 0; i < count; i++ ) {
		float					(*out)[4] = &rows[VK_RT_LAMP_ROW + i * VK_RT_LAMP_ROWS];
		vkLampMemory_t			*me = &now[i];
		const vkLampMemory_t	*was = NULL;
		int						candidate[VK_RT_LAMP_CANDIDATES];
		float					light[VK_RT_LAMP_CANDIDATES], side[VK_RT_LAMP_CANDIDATES], score[VK_RT_LAMP_CANDIDATES];
		int						numCandidates = 0;
		vec3_t					middle, dir, eye, g;
		int						base[3];

		memset( out, 0, sizeof( float ) * 4 * VK_RT_LAMP_ROWS );
		out[3][3] = 1.0f;		// all of it the grid
		VectorCopy( rt.casters[i].origin, me->origin );
		me->count = 0;
		if ( !ready ) {
			continue;
		}
		for ( int k = 0; k < lastCount; k++ ) {
			if ( Distance( last[k].origin, me->origin ) < 64.0f ) {
				was = &last[k];
				break;
			}
		}
		VectorCopy( me->origin, middle );
		middle[2] += 8.0f;		// as the sphere of the player (VK_RT_Pass)
		VectorCopy( middle, eye );
		eye[2] += 16.0f;		// where he sees the panels from: his chest
		VK_RT_GridDirection( middle, dir );

		// the panels of the lists of the eight cells around him, and those he had
		VectorSubtract( middle, world->lightGridOrigin, g );
		for ( int k = 0; k < 3; k++ ) {
			base[k] = (int)floorf( g[k] * world->lightGridInverseSize[k] );
		}
		for ( int corner = 0; corner < 8; corner++ ) {
			int c[3];

			for ( int k = 0; k < 3; k++ ) {
				c[k] = Com_Clampi( 0, world->lightGridBounds[k] - 1, base[k] + ( ( corner >> k ) & 1 ) );
			}
			const uint32_t *list = (const uint32_t *)rt.cellLists.mapped
				+ ( c[0] + c[1] * world->lightGridBounds[0] + c[2] * world->lightGridBounds[0] * world->lightGridBounds[1] ) * VK_RT_LIST_WORDS;
			for ( int k = 0; k < VK_RT_CELL_LIST; k++ ) {
				const uint32_t n = list[k];
				int at = 0;

				if ( n >= (uint32_t)vk_patches->size() ) {
					break;		// (all bits: the end of the list)
				}
				while ( at < numCandidates && candidate[at] != (int)n ) {
					at++;
				}
				if ( at == numCandidates ) {
					candidate[numCandidates++] = (int)n;
				}
			}
		}
		for ( int k = 0; was && k < was->count; k++ ) {
			int at = 0;

			while ( at < numCandidates && candidate[at] != was->lamp[k] ) {
				at++;
			}
			if ( at == numCandidates ) {
				candidate[numCandidates++] = was->lamp[k];
			}
		}

		// their light where he stands, by how much he sees of them
		for ( int c = 0; c < numCandidates; c++ ) {
			const float seen = VK_RT_Seen( candidate[c], eye );

			VK_RT_LampLight( &( *vk_patches )[candidate[c]], middle, dir, &light[c], &side[c] );
			score[c] = light[c] * side[c];		// (what is asked of the device below goes by this)
			light[c] *= seen < 0.0f ? 1.0f : seen;
		}
		// sorted by what they would give him if he saw them all
		for ( int a = 0; a < numCandidates; a++ ) {
			for ( int b = a + 1; b < numCandidates; b++ ) {
				if ( score[b] > score[a] ) {
					const int		n = candidate[a];
					const float		l = light[a], sd = side[a], sc = score[a];

					candidate[a] = candidate[b];
					light[a] = light[b];
					side[a] = side[b];
					score[a] = score[b];
					candidate[b] = n;
					light[b] = l;
					side[b] = sd;
					score[b] = sc;
				}
			}
		}
		// the best of them are asked of the device for the frames after
		for ( int c = 0; c < numCandidates && c < VK_RT_LAMP_ASKED && score[c] > 0.0f; c++ ) {
			VK_RT_AskSeen( candidate[c], eye );
		}
		// the shares asked for now: the three best by what he sees of them, of the light they give him together; one
		// only about on the side of the grid leaves the grid the part it does not have
		for ( int c = 0; c < numCandidates; c++ ) {
			score[c] = light[c] * side[c];
		}
		int best[VK_RT_LAMP_SHOWN] = { -1, -1, -1 };
		float total = 0.0f;
		for ( int k = 0; k < VK_RT_LAMP_SHOWN; k++ ) {
			for ( int c = 0; c < numCandidates; c++ ) {
				if ( score[c] > 0.0f && ( best[k] < 0 || score[c] > score[best[k]] ) && ( k < 1 || c != best[0] ) && ( k < 2 || c != best[1] ) ) {
					best[k] = c;
				}
			}
			if ( best[k] >= 0 ) {
				total += light[best[k]];
			}
		}
		// every panel he had and every candidate moves its share towards that
		for ( int c = 0; c < numCandidates; c++ ) {
			float target = 0.0f, share = 0.0f;

			for ( int k = 0; k < VK_RT_LAMP_SHOWN; k++ ) {
				if ( best[k] == c && total > 0.0f ) {
					target = score[c] / total;
				}
			}
			for ( int k = 0; was && k < was->count; k++ ) {
				if ( was->lamp[k] == candidate[c] ) {
					share = was->share[k];
					break;
				}
			}
			share += ( target - share ) * follow;
			if ( share < 0.01f && target <= 0.0f ) {
				continue;	// faded out
			}
			// (kept by the largest shares)
			int at = me->count;
			if ( at == VK_RT_LAMP_KEPT ) {
				at = 0;
				for ( int k = 1; k < VK_RT_LAMP_KEPT; k++ ) {
					if ( me->share[k] < me->share[at] ) {
						at = k;
					}
				}
				if ( me->share[at] >= share ) {
					continue;
				}
			} else {
				me->count++;
			}
			me->lamp[at] = candidate[c];
			me->share[at] = share;
		}
		// the three largest go to the shader; what the others had stays with them, the grid has the rest
		int order[VK_RT_LAMP_KEPT];
		float all = 0.0f;
		for ( int k = 0; k < me->count; k++ ) {
			order[k] = k;
			all += me->share[k];
		}
		for ( int a = 0; a < me->count; a++ ) {
			for ( int b = a + 1; b < me->count; b++ ) {
				if ( me->share[order[b]] > me->share[order[a]] ) {
					const int t = order[a];
					order[a] = order[b];
					order[b] = t;
				}
			}
		}
		float shown = 0.0f;
		for ( int k = 0; k < me->count && k < VK_RT_LAMP_SHOWN; k++ ) {
			shown += me->share[order[k]];
		}
		const float scale = shown > 0.0f ? Q_min( all, 1.0f ) / shown : 0.0f;
		for ( int k = 0; k < me->count && k < VK_RT_LAMP_SHOWN; k++ ) {
			const vkPatch_t *patch = &( *vk_patches )[me->lamp[order[k]]];

			VectorCopy( patch->origin, out[k] );
			out[k][3] = Com_Clamp( 1.0f, 32.0f, sqrtf( patch->area / M_PI ) );
			out[3][k] = me->share[order[k]] * scale;
		}
		out[3][3] = Com_Clamp( 0.0f, 1.0f, 1.0f - all );
	}
	memcpy( last, now, sizeof( vkLampMemory_t ) * count );
	lastCount = count;

	// for the light on the players of the next frame (VK_RT_PlayerLamps): where each one is, the direction to his
	// panels weighted by their shares, and what they share of his light
	vk_numPlayerLamps = 0;
	for ( int i = 0; i < count; i++ ) {
		const float	(*out)[4] = &rows[VK_RT_LAMP_ROW + i * VK_RT_LAMP_ROWS];
		vkPlayerLamps_t	*p = &vk_playerLamps[vk_numPlayerLamps++];
		vec3_t		chest;

		VectorCopy( rt.casters[i].origin, p->origin );
		VectorCopy( p->origin, chest );
		chest[2] += 24.0f;
		VectorClear( p->dir );
		p->share = 0.0f;
		for ( int k = 0; k < VK_RT_LAMP_SHOWN; k++ ) {
			vec3_t to;

			if ( out[3][k] <= 0.0f ) {
				continue;
			}
			VectorSubtract( out[k], chest, to );
			if ( VectorNormalize( to ) < 1.0f ) {
				continue;
			}
			VectorMA( p->dir, out[3][k], to, p->dir );
			p->share += out[3][k];
		}
		if ( VectorNormalize( p->dir ) < 1e-4f ) {
			p->share = 0.0f;
		}
	}
}

/*
===============
VK_RT_PlayerLamps

The panels the traced shadow of the player standing at origin came from in the frame before (VK_RT_PickLamps):
the direction to them from his chest and the share of his light they have; qfalse when no player of that frame
stood within 32 units (or the panels are not traced: shadow maps, v_rtLamps 0)
===============
*/
qboolean VK_RT_PlayerLamps( const vec3_t origin, vec3_t dir, float *share ) {
	for ( int i = 0; i < vk_numPlayerLamps; i++ ) {
		const vkPlayerLamps_t *p = &vk_playerLamps[i];

		if ( p->share > 0.0f && DistanceSquared( p->origin, origin ) < 32.0f * 32.0f ) {
			VectorCopy( p->dir, dir );
			*share = Com_Clamp( 0.0f, 1.0f, p->share );
			return qtrue;
		}
	}
	return qfalse;
}

// shaders/rtlamps.comp over what VK_RT_PickLamps asked in this frame, after the top level of the frame is built
static void VK_RT_TraceLamps( VkCommandBuffer cmd ) {
	const int f = vk.frameIndex;
	struct {
		uint32_t	addresses[4];
		uint32_t	count;
	} push;

	if ( rt.maps || !rt.lampsPipeline || !rt.lampAsked[f] ) {
		return;
	}
	const VkDeviceAddress rays = VK_RT_Address( &rt.lampRays[f] );
	const VkDeviceAddress seen = rays + sizeof( float ) * 12 * VK_RT_LAMP_RAYS;

	push.addresses[0] = (uint32_t)rays;
	push.addresses[1] = (uint32_t)( rays >> 32 );
	push.addresses[2] = (uint32_t)seen;
	push.addresses[3] = (uint32_t)( seen >> 32 );
	push.count = (uint32_t)rt.lampAsked[f];
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rt.lampsPipeline );
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rt.cellsLayout, 0, 1, &rt.sets[f], 0, NULL );
	qvkCmdPushConstants( cmd, rt.cellsLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( push ), &push );
	qvkCmdDispatch( cmd, ( push.count + 63 ) / 64, 1, 1 );
	// the CPU reads the answers when the frame is done
	VK_RT_Barrier( cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT );
}

/*
===============
VK_RT_LightChannel

For the light pass of a moving light in the view of the level: which channel of the mask the light passes read
now has its shadows (1 to 4: red to alpha, 0 none), found by where the light was when the mask was made (this frame: the
light passes wait for the shadows, rb_deferDlights), and the slot of the texture table that shows that mask
===============
*/
int VK_RT_LightChannel( const dlight_t *dl, uint32_t *slot ) {
	if ( !rt.readValid || !VK_RT_Wanted() || !tr.rtLightImage[0] ) {
		return 0;
	}
	const int read = rt.lightWrite ^ 1;

	if ( rt.lightShown[read] != (int)tr.rtLightImage[read]->texnum ) {
		return 0;
	}
	int channel = 0;
	// (a saber swings: the middle of its blade moves up to two hands' width in a frame of a fast swing)
	float best = 48.0f;

	for ( int k = 0; k < VK_RT_LIGHTS; k++ ) {
		const vkRtLight_t *was = &rt.readLights[k];
		vec3_t middle;
		float d;

		if ( !was->active ) {
			continue;
		}
		VectorAdd( was->start, was->end, middle );
		VectorScale( middle, 0.5f, middle );
		d = Distance( middle, dl->origin );
		if ( d < best && (qboolean)( dl->lineRange > 0.0f ) == was->tube ) {
			best = d;
			channel = k + 1;
		}
	}
	if ( channel ) {
		*slot = tr.rtLightImage[read]->texnum;
	}
	return channel;
}

qboolean VK_RT_Ready( void ) {
	return (qboolean)( rt.layout != VK_NULL_HANDLE );
}

// the view of the level asks for a pass that traces
qboolean VK_RT_Wanted( void ) {
	return (qboolean)( rt.layout && tr.world && ( v_rtDebug->integer > 0 || v_rtShadows->integer ) );
}

// The level hides the moving lights also in a frame without a player to cast anything (first person, nobody in sight).
// cg_shadows 2 is the setting of all these shadows; the players carry it in RF_SHADOW_PLANE, the level not.
static qboolean VK_RT_LevelLights( void ) {
	return (qboolean)( ( rt.maps ? !vk_mapChunks.empty() : rt.levelReady ) && backEnd.refdef.num_dlights > 0
		&& ri.Cvar_VariableIntegerValue( "cg_shadows" ) == 2 );
}

// the shadows from the moving lights, for the light passes after them (rt.lights is filled from the front)
static void VK_RT_DrawLightMask( VkCommandBuffer cmd, float (*rows)[4], vkGlobals_t *globals ) {
	if ( !rt.lights[0].active ) {
		rt.readValid = qfalse;
		return;
	}
	const VkDescriptorSet	lightSets[3] = { vk.frame->set, rt.sets[vk.frameIndex], vk.depthSet };
	vkTarget_t				*target = &rt.lightMask[rt.lightWrite];
	const int				slot = (int)tr.rtLightImage[rt.lightWrite]->texnum;

	VK_BeginTarget( target, NULL, 1, qfalse );
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rt.smLightsPipeline );
	vk.boundPipeline = rt.smLightsPipeline;
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rt.layout, 0, 3, lightSets, 0, NULL );
	VK_RT_WholeTarget( cmd, target );
	qvkCmdDraw( cmd, 3, 1, 0, 0 );
	VK_EndTarget();
	VK_TargetLayout( cmd, target, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_GpuMark( "rt light shadows" );
	if ( rt.lightShown[rt.lightWrite] != slot ) {
		VK_ShowTarget( slot, target );
		rt.lightShown[rt.lightWrite] = slot;
	}
	memcpy( rt.readLights, rt.lights, sizeof( rt.readLights ) );
	memcpy( rt.readView, rows[0], sizeof( float ) * 16 );
	memcpy( rt.readView[4], rows[8], sizeof( float ) * 4 );
	rt.readValid = qtrue;
	rt.lightWrite ^= 1;
	// The light passes of the solid surfaces run after this (vk_backend.cpp rb_deferDlights) and read the mask
	// of this frame: they need its view too. (The globals are read when the frame runs on the device, so this
	// is what every draw of the frame sees.)
	memcpy( globals->rt[42], rt.readView, sizeof( rt.readView ) );
}

/*
===============
VK_RT_Pass

Called while vk.color is begun over the level, as VK_DrawWet(); it is begun again afterwards.
===============
*/
void VK_RT_Pass( void ) {
	static float	rows[VK_RT_VEC4S][4];
	VkCommandBuffer	cmd;
	vkGlobals_t		*globals;
	VkDeviceSize	casterScratch;
	int				viewport[4];

	if ( !vk.frame || !VK_RT_Wanted() ) {
		return;
	}
	VK_RT_TakeQueued();
	// Nothing to trace this frame: no player casts a shadow (VK_RT_AddCaster ran during the level), no moving light
	// for the level to hide, the level is built and its cells are traced, no debug view. Splitting the level,
	// resolving its samples and building the top structure cost about 0.1 ms a frame for nothing (2026-11-14).
	if ( v_rtDebug->integer <= 0 && ( rt.castersFrame != backEnd.viewParms.frameCount || !rt.numCasters )
		&& !VK_RT_LevelLights()
		&& rt.world == tr.world && rt.worldLoads == r_worldLoads && !rt.levelPending && rt.cellState == 0 ) {
		rt.numCasters = 0;
		rt.previousValid = qfalse;
		rt.readValid = qfalse;
		return;
	}
	cmd = vk.frame->cmd;
	VK_EndTarget();
	// (the end of the level resolves its samples: a mark of its own, else the builds seem to cost it)
	VK_GpuMark( "level resolve" );

	// what has to be built this frame and how much scratch memory that needs, before anything is recorded
	if ( rt.world != tr.world || rt.worldLoads != r_worldLoads ) {
		VK_RT_PrepareLevel();
	}
	casterScratch = VK_RT_PrepareCasters();
	VK_RT_BrushEntities();
	VK_RT_EnsureMask();
	if ( rt.maps ) {
		// the shadow maps draw the skinned players, nothing is built
		VK_RT_SkinCasters( cmd );
	} else {
		VK_RT_EnsureScratch( Q_max( Q_max( rt.levelPending ? rt.levelScratch : 0, casterScratch ), rt.topScratch ) );
		// the frame that traced the cells is done when its place comes round again
		if ( rt.cellState == 2 && rt.cellFrame == vk.frameIndex ) {
			VK_RT_FitPatches();
			rt.cellState = 0;
		}

		// the builds of the frame before may still use the scratch memory
		VK_RT_Barrier( cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR );
		if ( rt.levelPending ) {
			VK_RT_RecordLevel( cmd );
		}
		VK_RT_SkinCasters( cmd );
		VK_RT_RecordCasters( cmd );
		VK_RT_RecordTop( cmd );
		if ( rt.cellState == 1 && rt.levelReady ) {
			VK_RT_TraceCells( cmd );
		}
	}
	VK_GpuMark( "rt builds" );

	memset( rows, 0, sizeof( rows ) );
	VK_ViewRows( rows, viewport );
	// the sun of the sky shader, straight up where the level has none
	if ( VectorLengthSquared( tr.sunDirection ) > 0.0f ) {
		VectorCopy( tr.sunDirection, rows[11] );
		VectorNormalize( rows[11] );
		rows[11][3] = 1.0f;
	} else {
		VectorSet( rows[11], 0.0f, 0.0f, 1.0f );
	}
	if ( v_rtDebug->integer > 0 ) {
		rows[11][3] = (float)v_rtDebug->integer;
	}
	VectorCopy( tr.world->lightGridOrigin, rows[12] );
	rows[12][3] = 1024.0f;
	VectorCopy( tr.world->lightGridInverseSize, rows[13] );
	rows[13][3] = (float)( rt.frameNumber & 0xffff );
	VectorSet4( rows[14], (float)tr.world->lightGridBounds[0], (float)tr.world->lightGridBounds[1], (float)tr.world->lightGridBounds[2],
		tanf( DEG2RAD( Com_Clamp( 0.0f, 20.0f, v_rtSunSize->value ) * 0.5f ) ) );
	VectorSet4( rows[15], (float)Com_Clampi( 1, 16, v_rtShadowRays->integer ), tanf( DEG2RAD( Com_Clamp( 0.0f, 60.0f, v_rtLightSize->value ) * 0.5f ) ),
		(float)VK_RT_MaskScale(), vk_sunStrength );
	// the moving lights whose shadows are traced this frame (38-41), and the view the mask the light passes read
	// was made in (42-46: view and projection, its rect)
	VK_RT_PickLights();
	for ( int k = 0; k < VK_RT_LIGHTS; k++ ) {
		const int row = VK_RT_LIGHT_ROW( k );

		VectorCopy( rt.lights[k].start, rows[row] );
		rows[row][3] = rt.lights[k].active ? rt.lights[k].range : 0.0f;
		VectorCopy( rt.lights[k].end, rows[row + 1] );
		rows[row + 1][3] = rt.lights[k].tube ? 1.0f : ( rt.lights[k].inSolid ? -1.0f : 0.0f );
		rows[VK_RT_POWER_ROW][k] = rt.lights[k].active ? rt.lights[k].power : 0.0f;
	}
	memcpy( rows[42], rt.readView, sizeof( rt.readView ) );
	// the players as spheres: a pixel whose light passes none of them traces nothing
	rows[21][0] = (float)Q_min( rt.numCasters, VK_RT_CASTERS );
	for ( int i = 0; i < rt.numCasters && i < VK_RT_CASTERS; i++ ) {
		VectorCopy( rt.casters[i].origin, rows[22 + i] );
		rows[22 + i][2] += 8.0f;
		rows[22 + i][3] = 64.0f;		// the model and a saber held out
	}
	VK_RT_TakeAnswers();
	VK_RT_PickLamps( rows );
	VK_RT_TraceLamps( vk.frame->cmd );
	// the view of the frame before, to find the points of this one in its mask
	memcpy( rows[16], rt.previous, sizeof( rt.previous ) );
	rows[20][3] = rt.previousValid ? 1.0f : 0.0f;
	if ( rt.maps ) {
		// The shadow maps know the players alone, not whether the level hides the sun from a point: the sun would
		// darken the shade of the walls too. They take the light of the grid everywhere (smshadow.frag).
		rows[11][3] = 0.0f;
	}
	if ( rt.numCasters || ( rt.maps && VK_RT_LevelLights() ) ) {
		VK_SM_DrawAtlas( cmd, rows );
		VK_GpuMark( "shadow maps" );
	}
	globals = (vkGlobals_t *)vk.frame->globals.mapped;
	memcpy( globals->rt, rows, sizeof( globals->rt ) );

	if ( v_rtDebug->integer > 0 && !rt.maps ) {
		const VkDescriptorSet sets[3] = { vk.frame->set, rt.sets[vk.frameIndex], vk.depthSet };

		VK_BeginOverLevel( cmd );
		qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rt.debugPipeline );
		vk.boundPipeline = rt.debugPipeline;
		qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rt.layout, 0, 3, sets, 0, NULL );
		VK_ViewportAndScissor( viewport[0], viewport[1], viewport[2], viewport[3], 0.0f, 1.0f );
		qvkCmdDraw( cmd, 3, 1, 0, 0 );
		qvkCmdEndRendering( cmd );
	} else if ( !rt.numCasters ) {
		// nothing in the mask of the players this frame: the next one starts it again
		rt.previousValid = qfalse;
		rt.readValid = qfalse;
		if ( VK_RT_LevelLights() ) {
			VK_TargetLayout( cmd, &vk.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL );
			// (no player was drawn into it, maybe ever; the mask only looks at it through the empty rows of its tiles)
			VK_TargetLayout( cmd, &rt.atlas, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
			VK_RT_DrawLightMask( cmd, rows, globals );
			rt.frameNumber++;
		}
	} else {
		const int				now = rt.frameNumber & 1;
		const VkDescriptorSet	traceSets[4] = { vk.frame->set, rt.sets[vk.frameIndex], vk.depthSet, rt.maskSets[now ^ 1] };
		const VkDescriptorSet	accumSets[4] = { vk.frame->set, rt.rawSet, vk.depthSet, rt.maskSets[now ^ 1] };

		// the rays of this frame (rt.raw); they read the depth of the scene, which has to be in the layout for reading
		// first (the validation layer: the set of the depth expects it)
		VK_TargetLayout( cmd, &vk.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL );
		VK_BeginTarget( &rt.raw, NULL, 1, qfalse );
		qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rt.maps ? rt.smShadowPipeline : rt.shadowPipeline );
		vk.boundPipeline = rt.maps ? rt.smShadowPipeline : rt.shadowPipeline;
		qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rt.layout, 0, 4, traceSets, 0, NULL );
		VK_RT_WholeTarget( cmd, &rt.raw );
		qvkCmdDraw( cmd, 3, 1, 0, 0 );
		VK_EndTarget();
		VK_GpuMark( "rt trace" );
		VK_TargetLayout( cmd, &rt.raw, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
		// onto the mask of the frame before, into the mask of this frame (shaders/rtaccum.frag)
		VK_BeginTarget( &rt.mask[now], NULL, 1, qfalse );
		qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rt.accumPipeline );
		vk.boundPipeline = rt.accumPipeline;
		qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rt.accumLayout, 0, 4, accumSets, 0, NULL );
		VK_ViewportAndScissor( viewport[0], viewport[1], viewport[2], viewport[3], 0.0f, 1.0f );
		qvkCmdDraw( cmd, 3, 1, 0, 0 );
		VK_EndTarget();
		VK_TargetLayout( cmd, &rt.mask[now], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );

		VK_RT_DrawLightMask( cmd, rows, globals );

		memcpy( rt.previous, rows[0], sizeof( float ) * 16 );
		VectorCopy( rows[4], rt.previous[4] );
		rt.previousValid = qtrue;
		rt.frameNumber++;

		// softened onto the frame, with what the moving lights just made (VK_RT_DrawLightMask moved lightWrite on)
		const VkDescriptorSet resolveSets[4] = { vk.frame->set, rt.maskSets[now], vk.depthSet, rt.lightMaskSets[rt.lightWrite ^ 1] };

		VK_BeginOverLevel( cmd );
		qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rt.resolvePipeline );
		vk.boundPipeline = rt.resolvePipeline;
		qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.screenFxPipelineLayout, 0, 4, resolveSets, 0, NULL );
		VK_ViewportAndScissor( viewport[0], viewport[1], viewport[2], viewport[3], 0.0f, 1.0f );
		qvkCmdDraw( cmd, 3, 1, 0, 0 );
		qvkCmdEndRendering( cmd );
	}

	VK_BeginTarget( &vk.color, &vk.depth, 2, qtrue );
	// the shaders that draw geometry have other sets and other state
	VK_BindFrameSets();
	vk.stateValid = qfalse;
	VK_SetViewport( vk.viewport[0], vk.viewport[1], vk.viewport[2], vk.viewport[3], 0.0f, 1.0f );
	VK_InvalidateTransform();
}
