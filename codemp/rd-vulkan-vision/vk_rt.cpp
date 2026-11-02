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
// The rays go through acceleration structures: one of the solid surfaces of the level, built once per level from
// their own copy of the positions (the buffer of vk_world.cpp is laid out for drawing), and one over all of them
// each frame (the top level), which the shaders that trace read.
//
// The passes run where the wet places do (RB_DrawWet()): after what is solid, before anything transparent, outside
// a target, so building the top level can be recorded right before them in the command buffer of the frame.
//
// v_rtDebug shows what the rays see: 1 whether a ray from each pixel towards the sun of the level gets out
// (lit where it does: outdoors that is the shadow the lightmaps have), 2 how far a ray from the eye through each
// pixel goes against the depth of the scene (black where they agree, red where the ray stops short of the
// depth, green where it goes past it).

#include "vk_local.h"

#include <vector>

static const uint32_t vk_rtDebugFrag[] =
#include "shaders/rtdebug_frag.inc"
;

#define VK_RT_MAX_INSTANCES		64

cvar_t	*v_rtDebug;

typedef struct vkAccel_s {
	VkAccelerationStructureKHR	as;
	vkBuffer_t					buffer;
	VkDeviceAddress				address;
} vkAccel_t;

static struct {
	// the level
	const world_t				*world;			// what the structure of the level was built for
	qboolean					worldReady;		// built, and its build is in the command buffer of a frame before
	vkBuffer_t					positions;		// the inputs of its build
	vkBuffer_t					indexes;
	uint32_t					numVertexes, numTriangles;
	vkAccel_t					level;
	vkBuffer_t					scratch;		// for every build, as large as the largest needs
	VkDeviceSize				scratchAlign;

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
} rt;

static VkDeviceAddress VK_RT_Address( const vkBuffer_t *buf ) {
	VkBufferDeviceAddressInfo info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };

	info.buffer = buf->buffer;
	return qvkGetBufferDeviceAddress( vk.device, &info );
}

static void VK_RT_CreateAccel( vkAccel_t *accel, VkAccelerationStructureTypeKHR type, VkDeviceSize size ) {
	VkAccelerationStructureCreateInfoKHR info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
	VkAccelerationStructureDeviceAddressInfoKHR address = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };

	VK_CreateBuffer( &accel->buffer, size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
		| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, qfalse );
	info.buffer = accel->buffer.buffer;
	info.size = size;
	info.type = type;
	VK_CHECK( qvkCreateAccelerationStructureKHR( vk.device, &info, NULL, &accel->as ) );
	address.accelerationStructure = accel->as;
	accel->address = qvkGetAccelerationStructureDeviceAddressKHR( vk.device, &address );
}

static void VK_RT_DestroyAccel( vkAccel_t *accel ) {
	if ( accel->as ) {
		qvkDestroyAccelerationStructureKHR( vk.device, accel->as, NULL );
	}
	VK_DestroyBuffer( &accel->buffer );
	memset( accel, 0, sizeof( *accel ) );
}

// the scratch buffer at least size bytes past the alignment the device wants for it
static VkDeviceAddress VK_RT_Scratch( VkDeviceSize size ) {
	if ( rt.scratch.size < size + rt.scratchAlign ) {
		// (only at a level change or for the first top level: nothing that used the old one is on its way)
		VK_WaitIdle();
		VK_DestroyBuffer( &rt.scratch );
		VK_CreateBuffer( &rt.scratch, size + rt.scratchAlign, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
			| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, qfalse );
	}
	return ( VK_RT_Address( &rt.scratch ) + rt.scratchAlign - 1 ) & ~( rt.scratchAlign - 1 );
}

// what builds write, what the next build or a shader reads
static void VK_RT_BuildBarrier( VkCommandBuffer cmd, VkPipelineStageFlags next ) {
	VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };

	barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
	qvkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, next, 0, 1, &barrier, 0, NULL, 0, NULL );
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

// the triangles of the solid surfaces of the world model (not doors, lifts and the other brush models, which move)
static void VK_RT_LevelTriangles( std::vector<float> &positions, std::vector<uint32_t> &indexes ) {
	const bmodel_t *model = &tr.world->bmodels[0];

	for ( int i = 0; i < model->numSurfaces; i++ ) {
		const msurface_t	*surf = model->firstSurface + i;
		const uint32_t		base = (uint32_t)( positions.size() / 3 );

		if ( !VK_RT_StopsLight( surf->shader ) ) {
			continue;
		}
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
}

static void VK_RT_LevelGeometry( VkAccelerationStructureGeometryKHR *geometry ) {
	memset( geometry, 0, sizeof( *geometry ) );
	geometry->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry->geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geometry->flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	geometry->geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	geometry->geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	geometry->geometry.triangles.vertexData.deviceAddress = VK_RT_Address( &rt.positions );
	geometry->geometry.triangles.vertexStride = sizeof( float ) * 3;
	geometry->geometry.triangles.maxVertex = rt.numVertexes - 1;
	geometry->geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
	geometry->geometry.triangles.indexData.deviceAddress = VK_RT_Address( &rt.indexes );
}

/*
===============
VK_RT_BuildLevel

Once per level, recorded into the command buffer of its first frame that traces. Its inputs stay until the next
level: the build reads them when that frame runs.
===============
*/
static void VK_RT_BuildLevel( VkCommandBuffer cmd ) {
	std::vector<float>								positions;
	std::vector<uint32_t>							indexes;
	VkAccelerationStructureGeometryKHR				geometry;
	VkAccelerationStructureBuildGeometryInfoKHR		build = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	VkAccelerationStructureBuildSizesInfoKHR		sizes = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	VkAccelerationStructureBuildRangeInfoKHR		range;
	const VkAccelerationStructureBuildRangeInfoKHR	*ranges = &range;
	const VkBufferUsageFlags						input = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
		| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

	VK_WaitIdle();
	VK_RT_DestroyAccel( &rt.level );
	VK_DestroyBuffer( &rt.positions );
	VK_DestroyBuffer( &rt.indexes );
	rt.world = tr.world;
	rt.worldReady = qfalse;

	VK_RT_LevelTriangles( positions, indexes );
	rt.numVertexes = (uint32_t)( positions.size() / 3 );
	rt.numTriangles = (uint32_t)( indexes.size() / 3 );
	if ( !rt.numTriangles ) {
		return;
	}
	// read once by the build, so they stay where the CPU wrote them
	VK_CreateBuffer( &rt.positions, positions.size() * sizeof( float ), input, qtrue );
	memcpy( rt.positions.mapped, positions.data(), positions.size() * sizeof( float ) );
	VK_CreateBuffer( &rt.indexes, indexes.size() * sizeof( uint32_t ), input, qtrue );
	memcpy( rt.indexes.mapped, indexes.data(), indexes.size() * sizeof( uint32_t ) );

	VK_RT_LevelGeometry( &geometry );
	build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build.geometryCount = 1;
	build.pGeometries = &geometry;
	qvkGetAccelerationStructureBuildSizesKHR( vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build,
		&rt.numTriangles, &sizes );
	VK_RT_CreateAccel( &rt.level, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, sizes.accelerationStructureSize );
	build.dstAccelerationStructure = rt.level.as;
	build.scratchData.deviceAddress = VK_RT_Scratch( sizes.buildScratchSize );

	memset( &range, 0, sizeof( range ) );
	range.primitiveCount = rt.numTriangles;
	qvkCmdBuildAccelerationStructuresKHR( cmd, 1, &build, &ranges );
	VK_RT_BuildBarrier( cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR );
	rt.worldReady = qtrue;
	ri.Printf( PRINT_DEVELOPER, "ray tracing: the level has %u solid triangles\n", rt.numTriangles );
}

/*
===============
VK_RT_BuildTop

The top level of this frame over what was built: the level for now, the players come next.
===============
*/
static void VK_RT_BuildTop( VkCommandBuffer cmd ) {
	const int										frame = vk.frameIndex;
	VkAccelerationStructureInstanceKHR				*instance = (VkAccelerationStructureInstanceKHR *)rt.instances[frame].mapped;
	VkAccelerationStructureGeometryKHR				geometry;
	VkAccelerationStructureBuildGeometryInfoKHR		build = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	VkAccelerationStructureBuildRangeInfoKHR		range;
	const VkAccelerationStructureBuildRangeInfoKHR	*ranges = &range;
	uint32_t										count = 0;

	if ( rt.worldReady ) {
		memset( instance, 0, sizeof( *instance ) );
		instance->transform.matrix[0][0] = instance->transform.matrix[1][1] = instance->transform.matrix[2][2] = 1.0f;
		instance->mask = 0x01;		// the level, shaders/rtdebug.frag
		instance->flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		instance->accelerationStructureReference = rt.level.address;
		count++;
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
	build.scratchData.deviceAddress = VK_RT_Scratch( rt.topScratch );
	memset( &range, 0, sizeof( range ) );
	range.primitiveCount = count;
	qvkCmdBuildAccelerationStructuresKHR( cmd, 1, &build, &ranges );
	VK_RT_BuildBarrier( cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT );
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

static void VK_RT_CreateLayouts( void ) {
	VkDescriptorSetLayoutBinding			binding;
	VkDescriptorSetLayoutCreateInfo			setInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	VkDescriptorPoolSize					size;
	VkDescriptorPoolCreateInfo				pool = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	VkDescriptorSetLayout					layouts[VK_FRAMES];
	VkDescriptorSetAllocateInfo				alloc = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	VkPipelineLayoutCreateInfo				layout = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	// the globals of the frame, the top level, the depth of the scene
	VkDescriptorSetLayout					passSets[3];

	memset( &binding, 0, sizeof( binding ) );
	binding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	setInfo.bindingCount = 1;
	setInfo.pBindings = &binding;
	VK_CHECK( qvkCreateDescriptorSetLayout( vk.device, &setInfo, NULL, &rt.setLayout ) );

	size.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	size.descriptorCount = VK_FRAMES;
	pool.maxSets = VK_FRAMES;
	pool.poolSizeCount = 1;
	pool.pPoolSizes = &size;
	VK_CHECK( qvkCreateDescriptorPool( vk.device, &pool, NULL, &rt.pool ) );
	for ( int i = 0; i < VK_FRAMES; i++ ) {
		layouts[i] = rt.setLayout;
	}
	alloc.descriptorPool = rt.pool;
	alloc.descriptorSetCount = VK_FRAMES;
	alloc.pSetLayouts = layouts;
	VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, rt.sets ) );

	passSets[0] = vk.frameSetLayout;
	passSets[1] = rt.setLayout;
	passSets[2] = vk.postSetLayout;
	layout.setLayoutCount = 3;
	layout.pSetLayouts = passSets;
	VK_CHECK( qvkCreatePipelineLayout( vk.device, &layout, NULL, &rt.layout ) );
}

void VK_RT_Init( void ) {
	VkPhysicalDeviceAccelerationStructurePropertiesKHR	accel = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
	VkPhysicalDeviceProperties2							props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &accel };

	v_rtDebug = ri.Cvar_Get( "v_rtDebug", "0", CVAR_CHEAT, "What the rays of the ray tracing see. 1: towards the sun, 2: from the eye against the depth" );
	memset( &rt, 0, sizeof( rt ) );
	if ( !vk.rayTracing || !vk.depthReadable ) {
		return;
	}
	qvkGetPhysicalDeviceProperties2( vk.physicalDevice, &props );
	rt.scratchAlign = accel.minAccelerationStructureScratchOffsetAlignment ? accel.minAccelerationStructureScratchOffsetAlignment : 1;

	VK_RT_CreateLayouts();
	VK_RT_CreateTop();
	rt.debugFragment = VK_CreateShader( vk_rtDebugFrag, sizeof( vk_rtDebugFrag ) );
	// (8: over the level with the samples of the frame, nothing from a vertex buffer)
	rt.debugPipeline = VK_CreatePipeline( vk.postVertexShader, rt.debugFragment, 0, 8, VKP_GENERIC, rt.layout );
}

void VK_RT_Shutdown( void ) {
	if ( !rt.layout ) {
		return;
	}
	qvkDestroyPipeline( vk.device, rt.debugPipeline, NULL );
	qvkDestroyShaderModule( vk.device, rt.debugFragment, NULL );
	for ( int i = 0; i < VK_FRAMES; i++ ) {
		VK_RT_DestroyAccel( &rt.top[i] );
		VK_DestroyBuffer( &rt.instances[i] );
	}
	VK_RT_DestroyAccel( &rt.level );
	VK_DestroyBuffer( &rt.positions );
	VK_DestroyBuffer( &rt.indexes );
	VK_DestroyBuffer( &rt.scratch );
	qvkDestroyPipelineLayout( vk.device, rt.layout, NULL );
	qvkDestroyDescriptorPool( vk.device, rt.pool, NULL );
	qvkDestroyDescriptorSetLayout( vk.device, rt.setLayout, NULL );
	memset( &rt, 0, sizeof( rt ) );
}

/*
===============================================================================

THE PASSES

===============================================================================
*/

// the view of the level asks for a pass that traces
qboolean VK_RT_Wanted( void ) {
	return (qboolean)( rt.layout && tr.world && v_rtDebug->integer > 0 );
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
	int				viewport[4];

	if ( !vk.frame || !VK_RT_Wanted() ) {
		return;
	}
	cmd = vk.frame->cmd;
	VK_EndTarget();
	if ( rt.world != tr.world ) {
		VK_RT_BuildLevel( cmd );
	}
	VK_RT_BuildTop( cmd );

	memset( rows, 0, sizeof( rows ) );
	VK_ViewRows( rows, viewport );
	// the sun of the sky shader, straight up where the level has none
	if ( VectorLengthSquared( tr.sunDirection ) > 0.0f ) {
		VectorCopy( tr.sunDirection, rows[VK_WET_HEADER] );
	} else {
		VectorSet( rows[VK_WET_HEADER], 0.0f, 0.0f, 1.0f );
	}
	rows[VK_WET_HEADER][3] = (float)v_rtDebug->integer;
	globals = (vkGlobals_t *)vk.frame->globals.mapped;
	memcpy( globals->rt, rows, sizeof( globals->rt ) );

	{
		const VkDescriptorSet sets[3] = { vk.frame->set, rt.sets[vk.frameIndex], vk.depthSet };

		VK_BeginOverLevel( cmd );
		qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rt.debugPipeline );
		vk.boundPipeline = rt.debugPipeline;
		qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rt.layout, 0, 3, sets, 0, NULL );
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
