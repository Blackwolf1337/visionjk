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

// snd_spatial_gpu.cpp -- the rays of the reflections traced on the graphics card (v_spatialGpu). See
// snd_spatial.cpp.
//
// Steam Audio traces them with Embree on the CPU. With many rays that kept several cores busy all the time in a
// level (2026-10-28: 8192 rays and 32 bounces, 2.6 cores, 90 % of it in Embree). Valve's library has a GPU path
// only on Windows (Radeon Rays, OpenCL), but it takes a ray tracer from outside: a scene of the type
// IPL_SCENETYPE_CUSTOM hands every batch of rays to a callback. Here that callback traces them with the ray queries
// of Vulkan (VK_KHR_ray_query) in a compute shader, shaders/spatial_rays.comp.
//
// Only the reflections are traced here, in a simulator of their own (spatial.gpuSimulator). What is in the way of
// a sound is asked every frame in the thread of the game, and a ray there that waited for the graphics card would
// hold up the frame: that and the paths stay with Embree.
//
// Steam Audio asks for the closest walls of the rays of a bounce, then for each source whether the way from those
// walls to it is free, and adds up the echo between the batches. Each batch waits for the graphics card in the
// thread of Steam Audio that asked: that thread does nothing meanwhile, the CPU rests, and the thread of the game
// never waits for it. Each such thread has a slot of its own (buffers, a command buffer, a fence), the queue is
// shared under a lock. A trip to the graphics card and back takes far longer than the trace (2026-10-28, RTX 4070
// SUPER next to the renderer: 15 us of work in 600 us), so the batch of the closest walls traces the ways to the
// sources too, and the batches of the sources are answered from it (S_SpatialGpuAnyHits): one trip a bounce.
//
// The device is one of its own, apart from the renderer: it serves next to OpenGL too, and no frame waits for it.
// Vulkan is loaded here as a library, not through the window. The level is built on the graphics card in the
// thread of the reflections, at the start of its first pass there (S_SpatialGpuBeginPass), and nothing waits for
// that either: the batches after it are behind it in the queue.

#include "snd_spatial_local.h"

#if defined(JP_SPATIAL) && defined(JP_SPATIAL_GPU)

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <mutex>

#define SPATIAL_GPU_SLOTS		16			// threads that may trace at once: those of Steam Audio and the one that runs the pass
#define SPATIAL_GPU_BATCH		SPATIAL_MAX_RAYS	// rays a slot takes to the graphics card at a time
#define SPATIAL_GPU_GROUP		64			// local_size_x of the shader
#define SPATIAL_GPU_WAIT		2000000000ull	// ns a batch may take, longer: the device is taken as lost
#define SPATIAL_GPU_FAR			1e30f		// a ray of Steam Audio that has no end gets this one: Vulkan wants a number
#define SPATIAL_GPU_SOURCES		( 1 + SPATIAL_REFLECT_SLOTS )	// of a pass: the reverb and the slots
#define SPATIAL_GPU_NEAR		1e-6f		// square metres a way to a source may be off from the one the shader traced

static_assert( SPATIAL_GPU_SOURCES <= 32, "MAX_SOURCES of shaders/spatial_rays.comp" );

static const char *spatialVulkanLibraries[] = {
#if defined(_WIN32)
	"vulkan-1.dll",
#elif defined(MACOS_X)
	"libvulkan.1.dylib",
	"libMoltenVK.dylib",
#else
	"libvulkan.so.1",
	"libvulkan.so",
#endif
	NULL
};

static const char *spatialGpuExtensions[] = {
	VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
	VK_KHR_RAY_QUERY_EXTENSION_NAME,
	VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
};

// SPIR-V of shaders/spatial_rays.comp, glslc writes it as an initializer list while building
static const uint32_t spatialRaysComp[] =
#include "spatial_rays_comp.inc"
;

// the functions of Vulkan this file calls, asked by name as the renderer does (vk_funcs.h)
#define SPATIAL_VK_INSTANCE( X ) \
	X( vkCreateDevice ) \
	X( vkDestroyInstance ) \
	X( vkEnumerateDeviceExtensionProperties ) \
	X( vkEnumeratePhysicalDevices ) \
	X( vkGetDeviceProcAddr ) \
	X( vkGetPhysicalDeviceFeatures2 ) \
	X( vkGetPhysicalDeviceMemoryProperties ) \
	X( vkGetPhysicalDeviceProperties ) \
	X( vkGetPhysicalDeviceProperties2 ) \
	X( vkGetPhysicalDeviceQueueFamilyProperties )

#define SPATIAL_VK_DEVICE( X ) \
	X( vkAllocateCommandBuffers ) \
	X( vkAllocateDescriptorSets ) \
	X( vkAllocateMemory ) \
	X( vkBeginCommandBuffer ) \
	X( vkBindBufferMemory ) \
	X( vkCmdBindDescriptorSets ) \
	X( vkCmdBindPipeline ) \
	X( vkCmdBuildAccelerationStructuresKHR ) \
	X( vkCmdDispatch ) \
	X( vkCmdPipelineBarrier ) \
	X( vkCmdPushConstants ) \
	X( vkCreateAccelerationStructureKHR ) \
	X( vkCreateBuffer ) \
	X( vkCreateCommandPool ) \
	X( vkCreateComputePipelines ) \
	X( vkCreateDescriptorPool ) \
	X( vkCreateDescriptorSetLayout ) \
	X( vkCreateFence ) \
	X( vkCreatePipelineLayout ) \
	X( vkCreateShaderModule ) \
	X( vkDestroyAccelerationStructureKHR ) \
	X( vkDestroyBuffer ) \
	X( vkDestroyCommandPool ) \
	X( vkDestroyDescriptorPool ) \
	X( vkDestroyDescriptorSetLayout ) \
	X( vkDestroyDevice ) \
	X( vkDestroyFence ) \
	X( vkDestroyPipeline ) \
	X( vkDestroyPipelineLayout ) \
	X( vkDestroyShaderModule ) \
	X( vkDeviceWaitIdle ) \
	X( vkEndCommandBuffer ) \
	X( vkFreeCommandBuffers ) \
	X( vkFreeMemory ) \
	X( vkGetAccelerationStructureBuildSizesKHR ) \
	X( vkGetAccelerationStructureDeviceAddressKHR ) \
	X( vkGetBufferDeviceAddress ) \
	X( vkGetBufferMemoryRequirements ) \
	X( vkGetDeviceQueue ) \
	X( vkGetFenceStatus ) \
	X( vkMapMemory ) \
	X( vkQueueSubmit ) \
	X( vkResetCommandBuffer ) \
	X( vkResetFences ) \
	X( vkUpdateDescriptorSets ) \
	X( vkWaitForFences )

static PFN_vkGetInstanceProcAddr	qvkGetInstanceProcAddr;
static PFN_vkCreateInstance			qvkCreateInstance;
static PFN_vkEnumerateInstanceVersion	qvkEnumerateInstanceVersion;
#define X( name )	static PFN_##name q##name;
SPATIAL_VK_INSTANCE( X )
SPATIAL_VK_DEVICE( X )
#undef X

// a buffer with memory of its own. mapped: where the CPU sees it, NULL for one only the graphics card has
typedef struct {
	VkBuffer		buffer;
	VkDeviceMemory	memory;
	void			*mapped;
	VkDeviceAddress	address;
	VkDeviceSize	size;
} spatialGpuBuffer_t;

// a ray as the shader takes it: tmin in origin[3], tmax in direction[3]
typedef struct {
	float	origin[4];
	float	direction[4];
} spatialGpuRay_t;

// and what it gives back, distance -1: nothing was hit. A closest wall also gives the point the ways to the
// sources begin at, and which of them are blocked (bit k: spatialGpu.sources[k])
typedef struct {
	float		distance;
	int			triangle;
	float		normal[3];
	float		point[3];
	uint32_t	blocked;
} spatialGpuHit_t;

static_assert( sizeof( spatialGpuRay_t ) == 32 && sizeof( spatialGpuHit_t ) == 36, "the layout of shaders/spatial_rays.comp (std430)" );

// One thread of Steam Audio that traces: its rays and hits, its command buffer and the fence it waits on
typedef struct {
	spatialGpuBuffer_t	rays, hits;
	VkCommandPool		pool;
	VkCommandBuffer		cmd;
	VkFence				done;
	VkDescriptorSet		set;
	int					level;			// spatialGpu.levelNumber its set has, 0: none yet
	int					walls;			// rays of the last batch of closest walls, whose ways to the sources are in hits
} spatialGpuSlot_t;

// A brush model of the collision model, 0: the world. Its triangles are a stretch of those of the level.
typedef struct {
	int								firstTriangle, numTriangles;
	VkAccelerationStructureKHR		blas;
	VkDeviceSize					offset, scratchOffset;	// in blasMemory and scratch
	VkDeviceSize					size, scratchSize;
	qboolean						present;	// in the scene now, as in the top scene of Steam Audio
	VkTransformMatrixKHR			transform;
	qboolean						filled;		// S_SpatialGpuMesh() put its walls in, the rest of the level has none of it
} spatialGpuMesh_t;

// A level as the graphics card has it. Filled while it loads, built at the start of its first pass.
typedef struct {
	int								numMeshes;
	spatialGpuMesh_t				*meshes;		// Z_Malloc, numMeshes
	int								numTriangles;
	byte							*materials;		// Z_Malloc, SPATIAL_* of each triangle, for the hits
	spatialGpuBuffer_t				vertices;		// three a triangle, Steam Audio's metres and axes
	spatialGpuBuffer_t				triangles;		// their normals, for the shader
	spatialGpuBuffer_t				blasMemory, scratch;
	spatialGpuBuffer_t				tlasMemory, tlasScratch, instances;
	VkAccelerationStructureKHR		tlas;
	qboolean						built;			// its build was handed to the queue
	qboolean						placed;			// a brush model moved since the top level was last built
	VkCommandBuffer					cmd;			// of its builds, from spatialGpu.buildPool
	VkFence							buildDone;
	int								number;			// counts the levels, so a slot knows when its set is old
} spatialGpuLevel_t;

typedef struct {
	void				*library;
	VkInstance			instance;
	VkPhysicalDevice	physical;
	VkDevice			device;
	VkQueue				queue;
	std::mutex			queueLock;			// the queue is used by the threads of Steam Audio and the one of the pass
	uint32_t			queueFamily;
	VkPhysicalDeviceMemoryProperties	memory;
	VkDeviceSize		scratchAlign;
	char				name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
	char				why[128];			// why there is no device, for v_spatialInfo

	VkDescriptorSetLayout	setLayout;
	VkPipelineLayout	pipelineLayout;
	VkPipeline			pipeline;
	VkDescriptorPool	descriptorPool;
	VkCommandPool		buildPool;			// of the builds of the levels, in the thread of the pass only

	spatialGpuSlot_t	slots[SPATIAL_GPU_SLOTS];
	spatialGpuBuffer_t	sources;			// where the sources of the pass are, written while no batch runs
	IPLVector3			source[SPATIAL_GPU_SOURCES];	//	and the same for the CPU
	int					numSources;
	std::atomic<int>	numSlots;			// taken by threads so far
	int					generation;			// counts the devices: a thread that had a slot of an older one takes a new one

	spatialGpuLevel_t	level, nextLevel;
	int					levelNumber;
	std::atomic<int>	lost;				// GPU_LOST_*: something failed in a pass, no more passes here
	int					lostSaid;			//	and the thread of the game said so

	// for v_spatialInfo: the batches of the pass that runs and how long they took from the queue back to Steam
	// Audio, and the same of the last finished pass (S_SpatialGpuBeginPass)
	std::atomic<int>	batches, batchUsec, answered;
	int					lastBatches, lastBatchUsec, lastAnswered;
} spatialGpuState_t;

// what made the passes leave the graphics card. Set in the threads of Steam Audio, which print nothing
enum {
	GPU_LOST_NONE,
	GPU_LOST_BUILD,			// the level could not be built
	GPU_LOST_SUBMIT,		// the queue did not take a batch
	GPU_LOST_WAIT,			// a batch did not come back in SPATIAL_GPU_WAIT
	GPU_LOST_SLOTS			// more threads traced than there are slots
};

static const char *spatialGpuLostWhy[] = {
	"",
	"it could not build the level",
	"its queue did not take the rays",
	"it did not answer in time",
	"more threads traced at once than it has room for"
};

static spatialGpuState_t	spatialGpu;
static int					spatialGpuGeneration;

static void S_SpatialGpuWhy( const char *fmt, ... ) {
	va_list args;

	va_start( args, fmt );
	Q_vsnprintf( spatialGpu.why, sizeof( spatialGpu.why ), fmt, args );
	va_end( args );
}

/*
===============================================================================

BUFFERS

===============================================================================
*/

// the first memory type with all of want, of those prefer the one that also has like. -1: none
static int S_SpatialGpuMemoryType( uint32_t bits, VkMemoryPropertyFlags want, VkMemoryPropertyFlags like ) {
	int found = -1;

	for ( uint32_t i = 0; i < spatialGpu.memory.memoryTypeCount; i++ ) {
		const VkMemoryPropertyFlags flags = spatialGpu.memory.memoryTypes[i].propertyFlags;

		if ( !( bits & ( 1u << i ) ) || ( flags & want ) != want ) {
			continue;
		}
		if ( ( flags & like ) == like ) {
			return (int)i;
		}
		if ( found < 0 ) {
			found = (int)i;
		}
	}
	return found;
}

static void S_SpatialGpuFreeBuffer( spatialGpuBuffer_t *b ) {
	if ( b->buffer ) {
		qvkDestroyBuffer( spatialGpu.device, b->buffer, NULL );
	}
	if ( b->memory ) {
		qvkFreeMemory( spatialGpu.device, b->memory, NULL );
	}
	memset( b, 0, sizeof( *b ) );
}

// A buffer the CPU writes or reads (host qtrue: coherent, mapped for as long as it lives, on the graphics card
// where it has such memory, or with a cache for reading back with like), or one only the graphics card has
static qboolean S_SpatialGpuBuffer( spatialGpuBuffer_t *b, VkDeviceSize size, VkBufferUsageFlags usage, qboolean host, VkMemoryPropertyFlags like ) {
	VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	VkMemoryAllocateFlagsInfo flags = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
	VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	VkMemoryRequirements req;
	const VkMemoryPropertyFlags want = host ? ( VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ) : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	int type;

	memset( b, 0, sizeof( *b ) );
	info.size = Q_max( size, (VkDeviceSize)16 );
	info.usage = usage;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if ( qvkCreateBuffer( spatialGpu.device, &info, NULL, &b->buffer ) != VK_SUCCESS ) {
		b->buffer = VK_NULL_HANDLE;
		return qfalse;
	}
	qvkGetBufferMemoryRequirements( spatialGpu.device, b->buffer, &req );
	type = S_SpatialGpuMemoryType( req.memoryTypeBits, want, like );
	if ( type < 0 ) {
		S_SpatialGpuFreeBuffer( b );
		return qfalse;
	}
	alloc.allocationSize = req.size;
	alloc.memoryTypeIndex = (uint32_t)type;
	if ( usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ) {
		flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
		alloc.pNext = &flags;
	}
	if ( qvkAllocateMemory( spatialGpu.device, &alloc, NULL, &b->memory ) != VK_SUCCESS ) {
		b->memory = VK_NULL_HANDLE;
		S_SpatialGpuFreeBuffer( b );
		return qfalse;
	}
	if ( qvkBindBufferMemory( spatialGpu.device, b->buffer, b->memory, 0 ) != VK_SUCCESS
		|| ( host && qvkMapMemory( spatialGpu.device, b->memory, 0, VK_WHOLE_SIZE, 0, &b->mapped ) != VK_SUCCESS ) ) {
		S_SpatialGpuFreeBuffer( b );
		return qfalse;
	}
	if ( usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ) {
		VkBufferDeviceAddressInfo where = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };

		where.buffer = b->buffer;
		b->address = qvkGetBufferDeviceAddress( spatialGpu.device, &where );
	}
	b->size = size;
	return qtrue;
}

static VkDeviceSize S_SpatialGpuAlign( VkDeviceSize value, VkDeviceSize align ) {
	return ( value + align - 1 ) & ~( align - 1 );
}

/*
===============================================================================

THE DEVICE

===============================================================================
*/

static qboolean S_SpatialGpuHasExtensions( VkPhysicalDevice physical ) {
	VkExtensionProperties have[512];
	uint32_t count = ARRAY_LEN( have );

	if ( qvkEnumerateDeviceExtensionProperties( physical, NULL, &count, have ) < 0 ) {
		return qfalse;
	}
	for ( size_t i = 0; i < ARRAY_LEN( spatialGpuExtensions ); i++ ) {
		uint32_t k;

		for ( k = 0; k < count && strcmp( have[k].extensionName, spatialGpuExtensions[i] ); k++ ) {
		}
		if ( k == count ) {
			return qfalse;
		}
	}
	return qtrue;
}

static qboolean S_SpatialGpuHasFeatures( VkPhysicalDevice physical ) {
	VkPhysicalDeviceAccelerationStructureFeaturesKHR as = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
	VkPhysicalDeviceRayQueryFeaturesKHR rq = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
	VkPhysicalDeviceVulkan12Features v12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };

	features.pNext = &v12;
	v12.pNext = &as;
	as.pNext = &rq;
	qvkGetPhysicalDeviceFeatures2( physical, &features );
	return (qboolean)( as.accelerationStructure && rq.rayQuery && v12.bufferDeviceAddress );
}

// The graphics card with ray queries, one of its own before one in the processor. A queue for compute that is
// not the one of the graphics, where it has one, so the traces do not have to wait for the frames of the renderer.
static qboolean S_SpatialGpuPickDevice( void ) {
	VkPhysicalDevice devices[16];
	uint32_t count = ARRAY_LEN( devices );
	int best = -1, bestScore = -1;

	if ( qvkEnumeratePhysicalDevices( spatialGpu.instance, &count, devices ) < 0 || !count ) {
		S_SpatialGpuWhy( "Vulkan has no device" );
		return qfalse;
	}
	for ( uint32_t i = 0; i < count; i++ ) {
		VkPhysicalDeviceProperties props;
		int score;

		qvkGetPhysicalDeviceProperties( devices[i], &props );
		if ( props.apiVersion < VK_API_VERSION_1_2 || !S_SpatialGpuHasExtensions( devices[i] ) || !S_SpatialGpuHasFeatures( devices[i] ) ) {
			continue;
		}
		score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 1 : 0;
		if ( score > bestScore ) {
			best = (int)i;
			bestScore = score;
		}
	}
	if ( best < 0 ) {
		S_SpatialGpuWhy( "no graphics card with ray queries (VK_KHR_ray_query)" );
		return qfalse;
	}
	spatialGpu.physical = devices[best];

	{
		VkPhysicalDeviceProperties props;
		VkQueueFamilyProperties families[32];
		uint32_t numFamilies = ARRAY_LEN( families );
		int family = -1;

		qvkGetPhysicalDeviceProperties( spatialGpu.physical, &props );
		Q_strncpyz( spatialGpu.name, props.deviceName, sizeof( spatialGpu.name ) );
		qvkGetPhysicalDeviceQueueFamilyProperties( spatialGpu.physical, &numFamilies, families );
		for ( uint32_t i = 0; i < numFamilies; i++ ) {
			if ( !( families[i].queueFlags & VK_QUEUE_COMPUTE_BIT ) ) {
				continue;
			}
			if ( family < 0 || !( families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT ) ) {
				family = (int)i;
			}
			if ( !( families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT ) ) {
				break;
			}
		}
		if ( family < 0 ) {
			S_SpatialGpuWhy( "%s has no queue for compute", spatialGpu.name );
			return qfalse;
		}
		spatialGpu.queueFamily = (uint32_t)family;
	}
	{
		VkPhysicalDeviceAccelerationStructurePropertiesKHR as = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
		VkPhysicalDeviceProperties2 props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };

		props.pNext = &as;
		qvkGetPhysicalDeviceProperties2( spatialGpu.physical, &props );
		spatialGpu.scratchAlign = Q_max( (VkDeviceSize)as.minAccelerationStructureScratchOffsetAlignment, (VkDeviceSize)256 );
	}
	qvkGetPhysicalDeviceMemoryProperties( spatialGpu.physical, &spatialGpu.memory );
	return qtrue;
}

static qboolean S_SpatialGpuCreateDevice( void ) {
	VkPhysicalDeviceAccelerationStructureFeaturesKHR as = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
	VkPhysicalDeviceRayQueryFeaturesKHR rq = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
	VkPhysicalDeviceVulkan12Features v12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	VkDeviceQueueCreateInfo queue = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	VkDeviceCreateInfo info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	const float priority = 1.0f;

	v12.bufferDeviceAddress = VK_TRUE;
	v12.pNext = &as;
	as.accelerationStructure = VK_TRUE;
	as.pNext = &rq;
	rq.rayQuery = VK_TRUE;
	queue.queueFamilyIndex = spatialGpu.queueFamily;
	queue.queueCount = 1;
	queue.pQueuePriorities = &priority;
	info.pNext = &v12;
	info.queueCreateInfoCount = 1;
	info.pQueueCreateInfos = &queue;
	info.enabledExtensionCount = ARRAY_LEN( spatialGpuExtensions );
	info.ppEnabledExtensionNames = spatialGpuExtensions;
	if ( qvkCreateDevice( spatialGpu.physical, &info, NULL, &spatialGpu.device ) != VK_SUCCESS ) {
		spatialGpu.device = VK_NULL_HANDLE;
		S_SpatialGpuWhy( "%s gives no device", spatialGpu.name );
		return qfalse;
	}
#define X( name )	q##name = (PFN_##name)qvkGetDeviceProcAddr( spatialGpu.device, #name ); if ( !q##name ) { S_SpatialGpuWhy( "the driver has no %s", #name ); return qfalse; }
	SPATIAL_VK_DEVICE( X )
#undef X
	qvkGetDeviceQueue( spatialGpu.device, spatialGpu.queueFamily, 0, &spatialGpu.queue );
	return qtrue;
}

static qboolean S_SpatialGpuCreateInstance( void ) {
	VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	uint32_t version = VK_API_VERSION_1_0;

	for ( int i = 0; spatialVulkanLibraries[i] && !spatialGpu.library; i++ ) {
		spatialGpu.library = Sys_LoadLibrary( spatialVulkanLibraries[i] );
	}
	if ( !spatialGpu.library ) {
		S_SpatialGpuWhy( "no Vulkan on this system" );
		return qfalse;
	}
	qvkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)Sys_LoadFunction( spatialGpu.library, "vkGetInstanceProcAddr" );
	if ( !qvkGetInstanceProcAddr ) {
		S_SpatialGpuWhy( "the library of Vulkan has no vkGetInstanceProcAddr" );
		return qfalse;
	}
	qvkCreateInstance = (PFN_vkCreateInstance)qvkGetInstanceProcAddr( VK_NULL_HANDLE, "vkCreateInstance" );
	qvkEnumerateInstanceVersion = (PFN_vkEnumerateInstanceVersion)qvkGetInstanceProcAddr( VK_NULL_HANDLE, "vkEnumerateInstanceVersion" );
	if ( !qvkCreateInstance || !qvkEnumerateInstanceVersion || qvkEnumerateInstanceVersion( &version ) != VK_SUCCESS || version < VK_API_VERSION_1_2 ) {
		S_SpatialGpuWhy( "Vulkan 1.2 or newer is needed" );
		return qfalse;
	}
	app.pApplicationName = "VisionJK sound";
	app.apiVersion = VK_API_VERSION_1_2;
	info.pApplicationInfo = &app;
	if ( qvkCreateInstance( &info, NULL, &spatialGpu.instance ) != VK_SUCCESS ) {
		spatialGpu.instance = VK_NULL_HANDLE;
		S_SpatialGpuWhy( "Vulkan gives no instance" );
		return qfalse;
	}
#define X( name )	q##name = (PFN_##name)qvkGetInstanceProcAddr( spatialGpu.instance, #name ); if ( !q##name ) { S_SpatialGpuWhy( "the library of Vulkan has no %s", #name ); return qfalse; }
	SPATIAL_VK_INSTANCE( X )
#undef X
	return qtrue;
}

static qboolean S_SpatialGpuCreatePipeline( void ) {
	VkDescriptorSetLayoutBinding bindings[5];
	VkDescriptorSetLayoutCreateInfo setInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	VkPushConstantRange push = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 3 * sizeof( uint32_t ) };
	VkPipelineLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	VkShaderModuleCreateInfo moduleInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	VkComputePipelineCreateInfo pipelineInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	VkDescriptorPoolSize sizes[2] = { { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, SPATIAL_GPU_SLOTS }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 * SPATIAL_GPU_SLOTS } };
	VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	VkShaderModule module;
	VkResult result;

	memset( bindings, 0, sizeof( bindings ) );
	for ( int i = 0; i < 5; i++ ) {
		bindings[i].binding = (uint32_t)i;
		bindings[i].descriptorType = i ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		bindings[i].descriptorCount = 1;
		bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	}
	setInfo.bindingCount = 5;
	setInfo.pBindings = bindings;
	if ( qvkCreateDescriptorSetLayout( spatialGpu.device, &setInfo, NULL, &spatialGpu.setLayout ) != VK_SUCCESS ) {
		spatialGpu.setLayout = VK_NULL_HANDLE;
		return qfalse;
	}
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &spatialGpu.setLayout;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &push;
	if ( qvkCreatePipelineLayout( spatialGpu.device, &layoutInfo, NULL, &spatialGpu.pipelineLayout ) != VK_SUCCESS ) {
		spatialGpu.pipelineLayout = VK_NULL_HANDLE;
		return qfalse;
	}
	moduleInfo.codeSize = sizeof( spatialRaysComp );
	moduleInfo.pCode = spatialRaysComp;
	if ( qvkCreateShaderModule( spatialGpu.device, &moduleInfo, NULL, &module ) != VK_SUCCESS ) {
		return qfalse;
	}
	pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pipelineInfo.stage.module = module;
	pipelineInfo.stage.pName = "main";
	pipelineInfo.layout = spatialGpu.pipelineLayout;
	result = qvkCreateComputePipelines( spatialGpu.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &spatialGpu.pipeline );
	qvkDestroyShaderModule( spatialGpu.device, module, NULL );
	if ( result != VK_SUCCESS ) {
		spatialGpu.pipeline = VK_NULL_HANDLE;
		return qfalse;
	}
	poolInfo.maxSets = SPATIAL_GPU_SLOTS;
	poolInfo.poolSizeCount = 2;
	poolInfo.pPoolSizes = sizes;
	if ( qvkCreateDescriptorPool( spatialGpu.device, &poolInfo, NULL, &spatialGpu.descriptorPool ) != VK_SUCCESS ) {
		spatialGpu.descriptorPool = VK_NULL_HANDLE;
		return qfalse;
	}
	return qtrue;
}

static qboolean S_SpatialGpuCreateSlot( spatialGpuSlot_t *s ) {
	VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	VkCommandBufferAllocateInfo cmdInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	VkDescriptorSetAllocateInfo setInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };

	if ( !S_SpatialGpuBuffer( &s->rays, SPATIAL_GPU_BATCH * sizeof( spatialGpuRay_t ), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, qtrue, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT )
		|| !S_SpatialGpuBuffer( &s->hits, SPATIAL_GPU_BATCH * sizeof( spatialGpuHit_t ), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, qtrue, VK_MEMORY_PROPERTY_HOST_CACHED_BIT ) ) {
		return qfalse;
	}
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = spatialGpu.queueFamily;
	if ( qvkCreateCommandPool( spatialGpu.device, &poolInfo, NULL, &s->pool ) != VK_SUCCESS ) {
		s->pool = VK_NULL_HANDLE;
		return qfalse;
	}
	cmdInfo.commandPool = s->pool;
	cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmdInfo.commandBufferCount = 1;
	if ( qvkAllocateCommandBuffers( spatialGpu.device, &cmdInfo, &s->cmd ) != VK_SUCCESS
		|| qvkCreateFence( spatialGpu.device, &fenceInfo, NULL, &s->done ) != VK_SUCCESS ) {
		s->done = VK_NULL_HANDLE;
		return qfalse;
	}
	setInfo.descriptorPool = spatialGpu.descriptorPool;
	setInfo.descriptorSetCount = 1;
	setInfo.pSetLayouts = &spatialGpu.setLayout;
	return (qboolean)( qvkAllocateDescriptorSets( spatialGpu.device, &setInfo, &s->set ) == VK_SUCCESS );
}

static void S_SpatialGpuFreeLevel( spatialGpuLevel_t *level ) {
	if ( spatialGpu.device ) {
		for ( int i = 0; i < level->numMeshes; i++ ) {
			if ( level->meshes[i].blas ) {
				qvkDestroyAccelerationStructureKHR( spatialGpu.device, level->meshes[i].blas, NULL );
			}
		}
		if ( level->tlas ) {
			qvkDestroyAccelerationStructureKHR( spatialGpu.device, level->tlas, NULL );
		}
		if ( level->buildDone ) {
			qvkDestroyFence( spatialGpu.device, level->buildDone, NULL );
		}
		// a level that had builds is done with them by now (S_SpatialGpuIdle)
		if ( level->cmd ) {
			qvkFreeCommandBuffers( spatialGpu.device, spatialGpu.buildPool, 1, &level->cmd );
		}
		S_SpatialGpuFreeBuffer( &level->vertices );
		S_SpatialGpuFreeBuffer( &level->triangles );
		S_SpatialGpuFreeBuffer( &level->blasMemory );
		S_SpatialGpuFreeBuffer( &level->scratch );
		S_SpatialGpuFreeBuffer( &level->tlasMemory );
		S_SpatialGpuFreeBuffer( &level->tlasScratch );
		S_SpatialGpuFreeBuffer( &level->instances );
	}
	if ( level->meshes ) {
		Z_Free( level->meshes );
	}
	if ( level->materials ) {
		Z_Free( level->materials );
	}
	memset( level, 0, sizeof( *level ) );
}

/*
===============
S_SpatialGpuShutdown

With the sound system, after the thread of the reflections and the simulator that calls into here are gone. The
one place this file waits for the graphics card: for a build of a level it may still be doing.
===============
*/
void S_SpatialGpuShutdown( void ) {
	if ( spatialGpu.device ) {
		qvkDeviceWaitIdle( spatialGpu.device );
		S_SpatialGpuFreeLevel( &spatialGpu.level );
		S_SpatialGpuFreeLevel( &spatialGpu.nextLevel );
		for ( int i = 0; i < SPATIAL_GPU_SLOTS; i++ ) {
			spatialGpuSlot_t *s = &spatialGpu.slots[i];

			S_SpatialGpuFreeBuffer( &s->rays );
			S_SpatialGpuFreeBuffer( &s->hits );
			if ( s->done ) {
				qvkDestroyFence( spatialGpu.device, s->done, NULL );
			}
			if ( s->pool ) {
				qvkDestroyCommandPool( spatialGpu.device, s->pool, NULL );
			}
		}
		if ( spatialGpu.buildPool ) {
			qvkDestroyCommandPool( spatialGpu.device, spatialGpu.buildPool, NULL );
		}
		S_SpatialGpuFreeBuffer( &spatialGpu.sources );
		if ( spatialGpu.descriptorPool ) {
			qvkDestroyDescriptorPool( spatialGpu.device, spatialGpu.descriptorPool, NULL );
		}
		if ( spatialGpu.pipeline ) {
			qvkDestroyPipeline( spatialGpu.device, spatialGpu.pipeline, NULL );
		}
		if ( spatialGpu.pipelineLayout ) {
			qvkDestroyPipelineLayout( spatialGpu.device, spatialGpu.pipelineLayout, NULL );
		}
		if ( spatialGpu.setLayout ) {
			qvkDestroyDescriptorSetLayout( spatialGpu.device, spatialGpu.setLayout, NULL );
		}
		qvkDestroyDevice( spatialGpu.device, NULL );
	} else {
		S_SpatialGpuFreeLevel( &spatialGpu.level );
		S_SpatialGpuFreeLevel( &spatialGpu.nextLevel );
	}
	if ( spatialGpu.instance ) {
		qvkDestroyInstance( spatialGpu.instance, NULL );
	}
	if ( spatialGpu.library ) {
		Sys_UnloadLibrary( spatialGpu.library );
	}
	spatialGpu.library = NULL;
	spatialGpu.instance = VK_NULL_HANDLE;
	spatialGpu.physical = VK_NULL_HANDLE;
	spatialGpu.device = VK_NULL_HANDLE;
	spatialGpu.queue = VK_NULL_HANDLE;
	spatialGpu.setLayout = VK_NULL_HANDLE;
	spatialGpu.pipelineLayout = VK_NULL_HANDLE;
	spatialGpu.pipeline = VK_NULL_HANDLE;
	spatialGpu.descriptorPool = VK_NULL_HANDLE;
	spatialGpu.buildPool = VK_NULL_HANDLE;
	memset( spatialGpu.slots, 0, sizeof( spatialGpu.slots ) );
	spatialGpu.numSlots.store( 0 );
	spatialGpu.lost.store( GPU_LOST_NONE );
	spatialGpu.lostSaid = GPU_LOST_NONE;
	spatialGpu.name[0] = '\0';
}

/*
===============
S_SpatialGpuInit

When the sound system starts, with v_spatial: the device, the shader and the slots of the threads. qfalse: no
graphics card here traces the rays, S_SpatialGpuWhy() says why, and the reflections stay with Embree.
===============
*/
qboolean S_SpatialGpuInit( void ) {
	VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };

	S_SpatialGpuShutdown();
	spatialGpu.why[0] = '\0';
	if ( !S_SpatialGpuCreateInstance() || !S_SpatialGpuPickDevice() || !S_SpatialGpuCreateDevice() ) {
		S_SpatialGpuShutdown();
		return qfalse;
	}
	if ( !S_SpatialGpuCreatePipeline() ) {
		S_SpatialGpuWhy( "%s does not take the shader of the rays", spatialGpu.name );
		S_SpatialGpuShutdown();
		return qfalse;
	}
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = spatialGpu.queueFamily;
	if ( qvkCreateCommandPool( spatialGpu.device, &poolInfo, NULL, &spatialGpu.buildPool ) != VK_SUCCESS ) {
		spatialGpu.buildPool = VK_NULL_HANDLE;
		S_SpatialGpuWhy( "%s gives no command pool", spatialGpu.name );
		S_SpatialGpuShutdown();
		return qfalse;
	}
	if ( !S_SpatialGpuBuffer( &spatialGpu.sources, SPATIAL_GPU_SOURCES * 4 * sizeof( float ), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, qtrue, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ) ) {
		S_SpatialGpuWhy( "%s has no memory for the rays", spatialGpu.name );
		S_SpatialGpuShutdown();
		return qfalse;
	}
	for ( int i = 0; i < SPATIAL_GPU_SLOTS; i++ ) {
		if ( !S_SpatialGpuCreateSlot( &spatialGpu.slots[i] ) ) {
			S_SpatialGpuWhy( "%s has no memory for the rays", spatialGpu.name );
			S_SpatialGpuShutdown();
			return qfalse;
		}
	}
	spatialGpu.generation = ++spatialGpuGeneration;
	return qtrue;
}

// the graphics card that traces, NULL: none, or it failed (S_SpatialGpuWhyNot)
const char *S_SpatialGpuName( void ) {
	return ( spatialGpu.device && spatialGpu.lost.load() == GPU_LOST_NONE ) ? spatialGpu.name : NULL;
}

const char *S_SpatialGpuWhyNot( void ) {
	const int lost = spatialGpu.lost.load();

	if ( lost != GPU_LOST_NONE ) {
		return va( "%s: %s", spatialGpu.name, spatialGpuLostWhy[lost] );
	}
	return spatialGpu.why[0] ? spatialGpu.why : "it was not asked for when the sound started";
}

// in the thread of a pass: the first thing that fails says why, the thread of the game tells it (S_SpatialGpuReady)
static void S_SpatialGpuLose( int why ) {
	int none = GPU_LOST_NONE;

	spatialGpu.lost.compare_exchange_strong( none, why );
}

/*
===============================================================================

THE LEVEL

===============================================================================
*/

/*
===============
S_SpatialGpuBeginLevel

S_SpatialLoadMap() begins a level: room for all of its walls, so that S_SpatialGpuMesh() can put each brush model
where it goes. The level waits next to the one in use until S_SpatialGpuTakeLevel().
===============
*/
void S_SpatialGpuBeginLevel( void ) {
	spatialGpuLevel_t *level = &spatialGpu.nextLevel;
	const int numModels = CM_ExportModelCount();

	if ( !spatialGpu.device ) {
		return;
	}
	// one that still waits was never used, so the graphics card never had it
	S_SpatialGpuFreeLevel( level );
	if ( numModels <= 0 ) {
		return;
	}
	level->numMeshes = numModels;
	level->meshes = (spatialGpuMesh_t *)Z_Malloc( numModels * sizeof( spatialGpuMesh_t ), TAG_GENERAL, qtrue );
	for ( int i = 0; i < numModels; i++ ) {
		level->meshes[i].firstTriangle = level->numTriangles;
		level->meshes[i].numTriangles = CM_ExportModelTriangles( i, NULL, NULL );
		level->numTriangles += level->meshes[i].numTriangles;
	}
	if ( !level->numTriangles
		|| !S_SpatialGpuBuffer( &level->vertices, level->numTriangles * 3 * sizeof( IPLVector3 ),
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, qtrue, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT )
		|| !S_SpatialGpuBuffer( &level->triangles, level->numTriangles * 4 * sizeof( float ), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, qtrue, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ) ) {
		if ( level->numTriangles ) {
			Com_Printf( "v_spatialGpu: %s has no memory for the %i triangles of the level, the CPU traces the rays\n", spatialGpu.name, level->numTriangles );
		}
		S_SpatialGpuFreeLevel( level );
		return;
	}
	level->materials = (byte *)Z_Malloc( level->numTriangles, TAG_GENERAL, qtrue );
	level->number = ++spatialGpu.levelNumber;
}

// The walls of one brush model (0: the world) as S_SpatialBuildMesh() made them for Steam Audio, three vertices a
// triangle in its order
void S_SpatialGpuMesh( int model, const IPLVector3 *vertices, const IPLint32 *materials, int count ) {
	spatialGpuLevel_t *level = &spatialGpu.nextLevel;
	spatialGpuMesh_t *mesh;
	IPLVector3 *out;
	float *normals;

	if ( !level->materials || model < 0 || model >= level->numMeshes ) {
		return;
	}
	mesh = &level->meshes[model];
	count = Q_min( count, mesh->numTriangles );
	mesh->numTriangles = count;
	out = (IPLVector3 *)level->vertices.mapped + mesh->firstTriangle * 3;
	normals = (float *)level->triangles.mapped + mesh->firstTriangle * 4;
	memcpy( out, vertices, count * 3 * sizeof( IPLVector3 ) );
	for ( int i = 0; i < count; i++ ) {
		const IPLVector3 *v = &vertices[i * 3];
		vec3_t e1, e2, n;

		VectorSet( e1, v[1].x - v[0].x, v[1].y - v[0].y, v[1].z - v[0].z );
		VectorSet( e2, v[2].x - v[0].x, v[2].y - v[0].y, v[2].z - v[0].z );
		CrossProduct( e1, e2, n );
		// a triangle without area has no side: any normal, the shader must not normalize a zero one
		if ( VectorNormalize( n ) == 0.0f ) {
			VectorSet( n, 0.0f, 1.0f, 0.0f );
		}
		VectorCopy( n, &normals[i * 4] );
		normals[i * 4 + 3] = 0.0f;
		level->materials[mesh->firstTriangle + i] = (byte)Com_Clampi( 0, SPATIAL_NUM_MATERIALS - 1, materials[i] );
	}
	mesh->filled = qtrue;
}

// Where a brush model is in the scene of the simulator, NULL: not in it. As qiplInstancedMeshAdd(), ...Remove() and
// ...UpdateTransform() do to the top scene of the level that waits (next) or of the one in use. The latter only
// while the thread of the reflections is idle: its next pass builds the top level anew.
void S_SpatialGpuPlace( qboolean next, int model, const IPLMatrix4x4 *transform ) {
	spatialGpuLevel_t *level = next ? &spatialGpu.nextLevel : &spatialGpu.level;
	spatialGpuMesh_t *mesh;

	if ( !level->materials || model < 0 || model >= level->numMeshes ) {
		return;
	}
	mesh = &level->meshes[model];
	mesh->present = transform ? qtrue : qfalse;
	if ( transform ) {
		for ( int i = 0; i < 3; i++ ) {
			for ( int j = 0; j < 4; j++ ) {
				mesh->transform.matrix[i][j] = transform->elements[i][j];
			}
		}
	}
	level->placed = qtrue;
}

// Nothing of the level in use is being built on the graphics card: it may be let go of
qboolean S_SpatialGpuIdle( void ) {
	const spatialGpuLevel_t *level = &spatialGpu.level;

	return (qboolean)( !level->built || !level->buildDone || qvkGetFenceStatus( spatialGpu.device, level->buildDone ) != VK_NOT_READY );
}

// S_SpatialTakeScene(), while the thread of the reflections is idle and S_SpatialGpuIdle(): the level that waited
// is the one in use. The one before goes, nothing of it is needed any more.
void S_SpatialGpuTakeLevel( void ) {
	if ( !spatialGpu.device ) {
		return;
	}
	S_SpatialGpuFreeLevel( &spatialGpu.level );
	spatialGpu.level = spatialGpu.nextLevel;
	memset( &spatialGpu.nextLevel, 0, sizeof( spatialGpu.nextLevel ) );
}

// A pass of the reflections may be traced here: there is a device that did not fail, and the level in use has
// walls. In the thread of the game, which says once why the passes left the graphics card.
qboolean S_SpatialGpuReady( void ) {
	const int lost = spatialGpu.lost.load();

	if ( lost != GPU_LOST_NONE && spatialGpu.lostSaid == GPU_LOST_NONE ) {
		spatialGpu.lostSaid = lost;
		Com_Printf( "v_spatialGpu: %s, the CPU traces the rays from now on\n", S_SpatialGpuWhyNot() );
	}
	return (qboolean)( spatialGpu.device && lost == GPU_LOST_NONE && spatialGpu.level.materials && spatialGpu.level.numTriangles > 0 );
}

// the brush models where they are now into the instances of the top level, how many of them are there
static int S_SpatialGpuWriteInstances( spatialGpuLevel_t *level ) {
	VkAccelerationStructureInstanceKHR *out = (VkAccelerationStructureInstanceKHR *)level->instances.mapped;
	int count = 0;

	for ( int i = 0; i < level->numMeshes; i++ ) {
		const spatialGpuMesh_t *mesh = &level->meshes[i];

		if ( !mesh->present || !mesh->blas ) {
			continue;
		}
		VkAccelerationStructureDeviceAddressInfoKHR where = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
		VkAccelerationStructureInstanceKHR *inst = &out[count++];

		where.accelerationStructure = mesh->blas;
		memset( inst, 0, sizeof( *inst ) );
		inst->transform = mesh->transform;
		inst->instanceCustomIndex = (uint32_t)mesh->firstTriangle;
		inst->mask = 0xff;
		inst->flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR | VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
		inst->accelerationStructureReference = qvkGetAccelerationStructureDeviceAddressKHR( spatialGpu.device, &where );
	}
	level->placed = qfalse;
	return count;
}

static void S_SpatialGpuTopInfo( spatialGpuLevel_t *level, VkAccelerationStructureGeometryKHR *geometry, VkAccelerationStructureBuildGeometryInfoKHR *info ) {
	memset( geometry, 0, sizeof( *geometry ) );
	geometry->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry->geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geometry->geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	geometry->geometry.instances.data.deviceAddress = level->instances.address;
	memset( info, 0, sizeof( *info ) );
	info->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	info->type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	info->flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	info->mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	info->geometryCount = 1;
	info->pGeometries = geometry;
	info->dstAccelerationStructure = level->tlas;
	info->scratchData.deviceAddress = level->tlasScratch.address;
}

static void S_SpatialGpuMeshInfo( spatialGpuLevel_t *level, const spatialGpuMesh_t *mesh, VkAccelerationStructureGeometryKHR *geometry, VkAccelerationStructureBuildGeometryInfoKHR *info ) {
	memset( geometry, 0, sizeof( *geometry ) );
	geometry->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry->geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geometry->flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	geometry->geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	geometry->geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	geometry->geometry.triangles.vertexData.deviceAddress = level->vertices.address + (VkDeviceSize)mesh->firstTriangle * 3 * sizeof( IPLVector3 );
	geometry->geometry.triangles.vertexStride = sizeof( IPLVector3 );
	geometry->geometry.triangles.maxVertex = (uint32_t)( mesh->numTriangles * 3 - 1 );
	geometry->geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
	memset( info, 0, sizeof( *info ) );
	info->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	info->type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	info->flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	info->mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	info->geometryCount = 1;
	info->pGeometries = geometry;
	info->dstAccelerationStructure = mesh->blas;
	info->scratchData.deviceAddress = level->scratch.address + mesh->scratchOffset;
}

static qboolean S_SpatialGpuCreateStructure( const spatialGpuBuffer_t *memory, VkDeviceSize offset, VkDeviceSize size, VkAccelerationStructureTypeKHR type, VkAccelerationStructureKHR *out ) {
	VkAccelerationStructureCreateInfoKHR info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };

	info.buffer = memory->buffer;
	info.offset = offset;
	info.size = size;
	info.type = type;
	if ( qvkCreateAccelerationStructureKHR( spatialGpu.device, &info, NULL, out ) != VK_SUCCESS ) {
		*out = VK_NULL_HANDLE;
		return qfalse;
	}
	return qtrue;
}

// the structures of the level and their memory: how large they are is asked of the driver
static qboolean S_SpatialGpuCreateStructures( spatialGpuLevel_t *level ) {
	VkAccelerationStructureGeometryKHR geometry;
	VkAccelerationStructureBuildGeometryInfoKHR info;
	VkAccelerationStructureBuildSizesInfoKHR sizes = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	VkDeviceSize total = 0, scratch = 0;
	const uint32_t numInstances = (uint32_t)level->numMeshes;

	for ( int i = 0; i < level->numMeshes; i++ ) {
		spatialGpuMesh_t *mesh = &level->meshes[i];
		const uint32_t count = (uint32_t)mesh->numTriangles;

		if ( !mesh->numTriangles || !mesh->filled ) {
			continue;
		}
		S_SpatialGpuMeshInfo( level, mesh, &geometry, &info );
		qvkGetAccelerationStructureBuildSizesKHR( spatialGpu.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &count, &sizes );
		mesh->offset = total;
		mesh->size = sizes.accelerationStructureSize;
		mesh->scratchOffset = scratch;
		mesh->scratchSize = sizes.buildScratchSize;
		total = S_SpatialGpuAlign( total + sizes.accelerationStructureSize, 256 );
		scratch = S_SpatialGpuAlign( scratch + sizes.buildScratchSize, spatialGpu.scratchAlign );
	}
	if ( !S_SpatialGpuBuffer( &level->blasMemory, total, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, qfalse, 0 )
		|| !S_SpatialGpuBuffer( &level->scratch, scratch, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, qfalse, 0 )
		|| !S_SpatialGpuBuffer( &level->instances, numInstances * sizeof( VkAccelerationStructureInstanceKHR ),
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, qtrue, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ) ) {
		return qfalse;
	}
	for ( int i = 0; i < level->numMeshes; i++ ) {
		spatialGpuMesh_t *mesh = &level->meshes[i];

		if ( mesh->numTriangles && mesh->filled && !S_SpatialGpuCreateStructure( &level->blasMemory, mesh->offset, mesh->size, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, &mesh->blas ) ) {
			return qfalse;
		}
	}

	S_SpatialGpuTopInfo( level, &geometry, &info );
	qvkGetAccelerationStructureBuildSizesKHR( spatialGpu.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &numInstances, &sizes );
	if ( !S_SpatialGpuBuffer( &level->tlasMemory, sizes.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, qfalse, 0 )
		|| !S_SpatialGpuBuffer( &level->tlasScratch, sizes.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, qfalse, 0 )
		|| !S_SpatialGpuCreateStructure( &level->tlasMemory, 0, sizes.accelerationStructureSize, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, &level->tlas ) ) {
		return qfalse;
	}
	return qtrue;
}

// AS builds before what reads them, in this command buffer or in a later one of the queue
static void S_SpatialGpuBuildBarrier( VkCommandBuffer cmd, VkPipelineStageFlags dstStage ) {
	VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };

	barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
	qvkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, dstStage, 0, 1, &barrier, 0, NULL, 0, NULL );
}

// a full build of the top level with the instances as they are now
static void S_SpatialGpuRecordTop( spatialGpuLevel_t *level, VkCommandBuffer cmd ) {
	VkAccelerationStructureGeometryKHR geometry;
	VkAccelerationStructureBuildGeometryInfoKHR info;
	VkAccelerationStructureBuildRangeInfoKHR range;
	const VkAccelerationStructureBuildRangeInfoKHR *ranges = &range;

	memset( &range, 0, sizeof( range ) );
	range.primitiveCount = (uint32_t)S_SpatialGpuWriteInstances( level );
	S_SpatialGpuTopInfo( level, &geometry, &info );
	qvkCmdBuildAccelerationStructuresKHR( cmd, 1, &info, &ranges );
}

static qboolean S_SpatialGpuSubmit( VkCommandBuffer cmd, VkFence fence ) {
	VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	std::lock_guard<std::mutex> lock( spatialGpu.queueLock );

	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;
	return (qboolean)( qvkQueueSubmit( spatialGpu.queue, 1, &submit, fence ) == VK_SUCCESS );
}

// the first build of a level: all of its brush models, then the top level, handed to the queue and not waited for
static qboolean S_SpatialGpuBuildLevel( spatialGpuLevel_t *level ) {
	VkCommandBufferAllocateInfo cmdInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	VkAccelerationStructureGeometryKHR *geometries;
	VkAccelerationStructureBuildGeometryInfoKHR *infos;
	VkAccelerationStructureBuildRangeInfoKHR *ranges;
	const VkAccelerationStructureBuildRangeInfoKHR **rangeOf;
	int count = 0;
	qboolean ok;

	level->built = qtrue;	// one try: a level that fails here is traced by the CPU
	if ( !S_SpatialGpuCreateStructures( level ) ) {
		return qfalse;
	}
	cmdInfo.commandPool = spatialGpu.buildPool;
	cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmdInfo.commandBufferCount = 1;
	if ( qvkAllocateCommandBuffers( spatialGpu.device, &cmdInfo, &level->cmd ) != VK_SUCCESS
		|| qvkCreateFence( spatialGpu.device, &fenceInfo, NULL, &level->buildDone ) != VK_SUCCESS ) {
		level->buildDone = VK_NULL_HANDLE;
		return qfalse;
	}

	geometries = (VkAccelerationStructureGeometryKHR *)Z_Malloc( level->numMeshes * sizeof( *geometries ), TAG_TEMP_WORKSPACE, qtrue );
	infos = (VkAccelerationStructureBuildGeometryInfoKHR *)Z_Malloc( level->numMeshes * sizeof( *infos ), TAG_TEMP_WORKSPACE, qtrue );
	ranges = (VkAccelerationStructureBuildRangeInfoKHR *)Z_Malloc( level->numMeshes * sizeof( *ranges ), TAG_TEMP_WORKSPACE, qtrue );
	rangeOf = (const VkAccelerationStructureBuildRangeInfoKHR **)Z_Malloc( level->numMeshes * sizeof( *rangeOf ), TAG_TEMP_WORKSPACE, qtrue );
	for ( int i = 0; i < level->numMeshes; i++ ) {
		const spatialGpuMesh_t *mesh = &level->meshes[i];

		if ( !mesh->blas ) {
			continue;
		}
		S_SpatialGpuMeshInfo( level, mesh, &geometries[count], &infos[count] );
		ranges[count].primitiveCount = (uint32_t)mesh->numTriangles;
		rangeOf[count] = &ranges[count];
		count++;
	}
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	qvkBeginCommandBuffer( level->cmd, &begin );
	if ( count ) {
		qvkCmdBuildAccelerationStructuresKHR( level->cmd, (uint32_t)count, infos, rangeOf );
	}
	S_SpatialGpuBuildBarrier( level->cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR );
	S_SpatialGpuRecordTop( level, level->cmd );
	ok = (qboolean)( qvkEndCommandBuffer( level->cmd ) == VK_SUCCESS && S_SpatialGpuSubmit( level->cmd, level->buildDone ) );
	Z_Free( rangeOf );
	Z_Free( ranges );
	Z_Free( infos );
	Z_Free( geometries );
	return ok;
}

/*
===============
S_SpatialGpuBeginPass

In the thread of the reflections, before a pass of the simulator of the graphics card: the first one of a level
builds it there, a later one builds the top level anew when a brush model moved. Neither is waited for, the
batches of the pass are behind them in the queue. A top level that is still being built from the last time keeps
the places it has until the next pass: its instances must not be written meanwhile. The slots get the level in
their sets here, while no batch runs. qfalse: the level can not be traced here, the pass has nothing to hit.
===============
*/
qboolean S_SpatialGpuBeginPass( void ) {
	spatialGpuLevel_t *level = &spatialGpu.level;

	if ( !spatialGpu.device || spatialGpu.lost.load() != GPU_LOST_NONE || !level->materials || !level->numTriangles ) {
		return qfalse;
	}
	spatialGpu.lastBatches = spatialGpu.batches.exchange( 0 );
	spatialGpu.lastBatchUsec = spatialGpu.batchUsec.exchange( 0 );
	spatialGpu.lastAnswered = spatialGpu.answered.exchange( 0 );
	if ( !level->built ) {
		if ( !S_SpatialGpuBuildLevel( level ) ) {
			S_SpatialGpuLose( GPU_LOST_BUILD );
			return qfalse;
		}
	} else if ( level->placed && qvkGetFenceStatus( spatialGpu.device, level->buildDone ) == VK_SUCCESS ) {
		VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };

		qvkResetFences( spatialGpu.device, 1, &level->buildDone );
		qvkResetCommandBuffer( level->cmd, 0 );
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		qvkBeginCommandBuffer( level->cmd, &begin );
		S_SpatialGpuRecordTop( level, level->cmd );
		if ( qvkEndCommandBuffer( level->cmd ) != VK_SUCCESS || !S_SpatialGpuSubmit( level->cmd, level->buildDone ) ) {
			S_SpatialGpuLose( GPU_LOST_SUBMIT );
			return qfalse;
		}
	}

	// the sources as S_SpatialPassInputs() gave them to Steam Audio, in any order: a way is found by where it ends
	spatialGpu.numSources = 0;
	if ( spatialPass.reverb ) {
		spatialGpu.source[spatialGpu.numSources++] = spatialPass.listener.origin;
	}
	for ( int i = 0; i < spatial.numReflectSlots; i++ ) {
		if ( spatialPass.own[i] ) {
			spatialGpu.source[spatialGpu.numSources++] = spatialPass.source[i].origin;
		}
	}
	for ( int i = 0; i < spatialGpu.numSources; i++ ) {
		float *out = (float *)spatialGpu.sources.mapped + i * 4;

		out[0] = spatialGpu.source[i].x;
		out[1] = spatialGpu.source[i].y;
		out[2] = spatialGpu.source[i].z;
		out[3] = 0.0f;
	}

	for ( int i = 0; i < SPATIAL_GPU_SLOTS; i++ ) {
		spatialGpuSlot_t *s = &spatialGpu.slots[i];
		VkWriteDescriptorSetAccelerationStructureKHR as = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
		VkDescriptorBufferInfo buffers[4] = { { s->rays.buffer, 0, VK_WHOLE_SIZE }, { s->hits.buffer, 0, VK_WHOLE_SIZE },
			{ level->triangles.buffer, 0, VK_WHOLE_SIZE }, { spatialGpu.sources.buffer, 0, VK_WHOLE_SIZE } };
		VkWriteDescriptorSet writes[2];

		s->walls = 0;
		if ( s->level == level->number ) {
			continue;
		}
		s->level = level->number;
		as.accelerationStructureCount = 1;
		as.pAccelerationStructures = &level->tlas;
		memset( writes, 0, sizeof( writes ) );
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].pNext = &as;
		writes[0].dstSet = s->set;
		writes[0].dstBinding = 0;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = s->set;
		writes[1].dstBinding = 1;
		writes[1].descriptorCount = 4;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[1].pBufferInfo = buffers;
		qvkUpdateDescriptorSets( spatialGpu.device, 2, writes, 0, NULL );
	}
	return qtrue;
}

/*
===============================================================================

THE RAYS

What Steam Audio calls in its threads while a pass of spatial.gpuSimulator runs.

===============================================================================
*/

// The slot of the calling thread, NULL: no more are free or the device failed. A thread keeps its slot for as
// long as the device lives.
static spatialGpuSlot_t *S_SpatialGpuSlot( void ) {
	static thread_local int generation, slot;

	if ( spatialGpu.lost.load() != GPU_LOST_NONE || !spatialGpu.device ) {
		return NULL;
	}
	if ( generation != spatialGpu.generation ) {
		slot = spatialGpu.numSlots.fetch_add( 1 );
		generation = spatialGpu.generation;
	}
	if ( slot >= SPATIAL_GPU_SLOTS ) {
		S_SpatialGpuLose( GPU_LOST_SLOTS );
		return NULL;
	}
	return &spatialGpu.slots[slot];
}

// One batch through the shader, up to SPATIAL_GPU_BATCH rays. The thread waits here for the graphics card. qfalse:
// it failed, and nothing more is traced here (v_spatialInfo says so, the next pass goes to the CPU).
static qboolean S_SpatialGpuBatch( spatialGpuSlot_t *s, int numRays, const IPLRay *rays, const IPLfloat32 *minDistances, const IPLfloat32 *maxDistances, qboolean anyHit ) {
	VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
	spatialGpuRay_t *out = (spatialGpuRay_t *)s->rays.mapped;
	const uint32_t push[3] = { (uint32_t)numRays, anyHit ? 1u : 0u, (uint32_t)spatialGpu.numSources };
	VkResult result;
	int start;

	for ( int i = 0; i < numRays; i++ ) {
		out[i].origin[0] = rays[i].origin.x;
		out[i].origin[1] = rays[i].origin.y;
		out[i].origin[2] = rays[i].origin.z;
		out[i].origin[3] = minDistances[i];
		out[i].direction[0] = rays[i].direction.x;
		out[i].direction[1] = rays[i].direction.y;
		out[i].direction[2] = rays[i].direction.z;
		out[i].direction[3] = Q_min( maxDistances[i], SPATIAL_GPU_FAR );
	}

	qvkResetCommandBuffer( s->cmd, 0 );
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	qvkBeginCommandBuffer( s->cmd, &begin );
	S_SpatialGpuBuildBarrier( s->cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT );
	qvkCmdBindPipeline( s->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, spatialGpu.pipeline );
	qvkCmdBindDescriptorSets( s->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, spatialGpu.pipelineLayout, 0, 1, &s->set, 0, NULL );
	qvkCmdPushConstants( s->cmd, spatialGpu.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( push ), push );
	qvkCmdDispatch( s->cmd, (uint32_t)( ( numRays + SPATIAL_GPU_GROUP - 1 ) / SPATIAL_GPU_GROUP ), 1, 1 );
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	qvkCmdPipelineBarrier( s->cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0, NULL );
	start = S_SpatialUsec();
	if ( qvkEndCommandBuffer( s->cmd ) != VK_SUCCESS || !S_SpatialGpuSubmit( s->cmd, s->done ) ) {
		S_SpatialGpuLose( GPU_LOST_SUBMIT );
		return qfalse;
	}
	// Steam Audio wants the hits before it goes on: this thread of it waits, the thread of the game does not. A
	// batch that is not back by then may still write into the slot: no slot is used again (S_SpatialGpuSlot).
	result = qvkWaitForFences( spatialGpu.device, 1, &s->done, VK_TRUE, SPATIAL_GPU_WAIT );
	if ( result != VK_SUCCESS ) {
		S_SpatialGpuLose( GPU_LOST_WAIT );
		return qfalse;
	}
	qvkResetFences( spatialGpu.device, 1, &s->done );
	s->walls = anyHit ? 0 : numRays;
	spatialGpu.batches.fetch_add( 1 );
	spatialGpu.batchUsec.fetch_add( S_SpatialUsec() - start );
	return qtrue;
}

// for v_spatialInfo: how many batches the last pass on the graphics card had, how long one took on average, and how
// many batches of the sources were answered from those of the walls
void S_SpatialGpuBatchInfo( int *batches, int *usec, int *answered ) {
	*batches = spatialGpu.lastBatches;
	*usec = spatialGpu.lastBatches ? spatialGpu.lastBatchUsec / spatialGpu.lastBatches : 0;
	*answered = spatialGpu.lastAnswered;
}

// Steam Audio's materials as the scene of Embree hands them out: a hit points at the one of its triangle
static IPLMaterial *S_SpatialGpuMaterial( int material ) {
	return (IPLMaterial *)&spatialMaterials[material];
}

static void IPLCALL S_SpatialGpuClosestHits( IPLint32 numRays, const IPLRay *rays, const IPLfloat32 *minDistances, const IPLfloat32 *maxDistances, IPLHit *hits, void *userData ) {
	spatialGpuSlot_t *s = S_SpatialGpuSlot();

	for ( int start = 0; start < numRays; start += SPATIAL_GPU_BATCH ) {
		const int count = Q_min( SPATIAL_GPU_BATCH, numRays - start );
		const qboolean traced = (qboolean)( s && S_SpatialGpuBatch( s, count, rays + start, minDistances + start, maxDistances + start, qfalse ) );
		const spatialGpuHit_t *in = s ? (const spatialGpuHit_t *)s->hits.mapped : NULL;

		for ( int i = 0; i < count; i++ ) {
			IPLHit *hit = &hits[start + i];

			hit->objectIndex = -1;
			hit->material = S_SpatialGpuMaterial( SPATIAL_GENERIC );
			if ( !traced || in[i].distance < 0.0f ) {
				// what failed lets the ray go: the echo is quieter for a pass, not wrong
				hit->distance = INFINITY;
				hit->triangleIndex = hit->materialIndex = -1;
				hit->normal.x = 0.0f;
				hit->normal.y = 1.0f;
				hit->normal.z = 0.0f;
				continue;
			}
			hit->distance = in[i].distance;
			hit->triangleIndex = in[i].triangle;
			hit->materialIndex = spatialGpu.level.materials[in[i].triangle];
			hit->material = S_SpatialGpuMaterial( hit->materialIndex );
			hit->normal.x = in[i].normal[0];
			hit->normal.y = in[i].normal[1];
			hit->normal.z = in[i].normal[2];
		}
	}
}

static float S_SpatialGpuDistanceSquared( const IPLVector3 *a, const float *b ) {
	const float x = a->x - b[0], y = a->y - b[1], z = a->z - b[2];

	return x * x + y * y + z * z;
}

// The ways from the walls of the last batch of this thread to one source, as the shader traced them. Steam Audio asks
// with rays of the same order (BatchedReflectionSimulator::shade), each from the point the shader moved off the
// wall, and ending at the source. qfalse: a ray is not one of those, it goes to the graphics card.
static qboolean S_SpatialGpuAnswer( const spatialGpuSlot_t *s, int numRays, const IPLRay *rays, const IPLfloat32 *maxDistances, IPLuint8 *occluded ) {
	const spatialGpuHit_t *walls = (const spatialGpuHit_t *)s->hits.mapped;
	int source = -1;

	if ( s->walls != numRays ) {
		return qfalse;
	}
	for ( int i = 0; i < numRays; i++ ) {
		const IPLRay *r = &rays[i];
		IPLVector3 end;

		if ( maxDistances[i] < 0.0f ) {
			occluded[i] = 1;
			continue;
		}
		if ( walls[i].distance < 0.0f || S_SpatialGpuDistanceSquared( &r->origin, walls[i].point ) > SPATIAL_GPU_NEAR ) {
			return qfalse;
		}
		end.x = r->origin.x + r->direction.x * maxDistances[i];
		end.y = r->origin.y + r->direction.y * maxDistances[i];
		end.z = r->origin.z + r->direction.z * maxDistances[i];
		// all the rays of a batch go to the same source
		if ( source < 0 ) {
			for ( int k = 0; k < spatialGpu.numSources && source < 0; k++ ) {
				const float at[3] = { spatialGpu.source[k].x, spatialGpu.source[k].y, spatialGpu.source[k].z };

				if ( S_SpatialGpuDistanceSquared( &end, at ) <= SPATIAL_GPU_NEAR ) {
					source = k;
				}
			}
			if ( source < 0 ) {
				return qfalse;
			}
		} else {
			const float at[3] = { spatialGpu.source[source].x, spatialGpu.source[source].y, spatialGpu.source[source].z };

			if ( S_SpatialGpuDistanceSquared( &end, at ) > SPATIAL_GPU_NEAR ) {
				return qfalse;
			}
		}
		occluded[i] = (IPLuint8)( ( walls[i].blocked >> source ) & 1u );
	}
	return qtrue;
}

// A ray whose end is below 0 is blocked without a trace, as Steam Audio's own loop over anyHit() has it
// (CustomScene::anyHits): it marks so the sources that the walls of a ray do not face
static void IPLCALL S_SpatialGpuAnyHits( IPLint32 numRays, const IPLRay *rays, const IPLfloat32 *minDistances, const IPLfloat32 *maxDistances, IPLuint8 *occluded, void *userData ) {
	spatialGpuSlot_t *s = S_SpatialGpuSlot();

	if ( s && S_SpatialGpuAnswer( s, numRays, rays, maxDistances, occluded ) ) {
		spatialGpu.answered.fetch_add( 1 );
		return;
	}

	for ( int start = 0; start < numRays; start += SPATIAL_GPU_BATCH ) {
		const int count = Q_min( SPATIAL_GPU_BATCH, numRays - start );
		const qboolean traced = (qboolean)( s && S_SpatialGpuBatch( s, count, rays + start, minDistances + start, maxDistances + start, qtrue ) );
		const spatialGpuHit_t *in = s ? (const spatialGpuHit_t *)s->hits.mapped : NULL;

		for ( int i = 0; i < count; i++ ) {
			occluded[start + i] = (IPLuint8)( !traced || in[i].distance >= 0.0f );
		}
	}
}

// the one ray versions, which the batched simulator of Steam Audio does not call: for a scene that has them all
static void IPLCALL S_SpatialGpuClosestHit( const IPLRay *ray, IPLfloat32 minDistance, IPLfloat32 maxDistance, IPLHit *hit, void *userData ) {
	S_SpatialGpuClosestHits( 1, ray, &minDistance, &maxDistance, hit, userData );
}

static void IPLCALL S_SpatialGpuAnyHit( const IPLRay *ray, IPLfloat32 minDistance, IPLfloat32 maxDistance, IPLuint8 *occluded, void *userData ) {
	S_SpatialGpuAnyHits( 1, ray, &minDistance, &maxDistance, occluded, userData );
}

// the scene of spatial.gpuSimulator: Steam Audio has no walls in it, it asks these callbacks
void S_SpatialGpuSceneSettings( IPLSceneSettings *settings ) {
	memset( settings, 0, sizeof( *settings ) );
	settings->type = IPL_SCENETYPE_CUSTOM;
	settings->closestHitCallback = S_SpatialGpuClosestHit;
	settings->anyHitCallback = S_SpatialGpuAnyHit;
	settings->batchedClosestHitCallback = S_SpatialGpuClosestHits;
	settings->batchedAnyHitCallback = S_SpatialGpuAnyHits;
}

#elif defined(JP_SPATIAL)

// built without the headers of Vulkan or without glslc, and in a browser: the CPU traces all rays
qboolean S_SpatialGpuInit( void ) { return qfalse; }
void S_SpatialGpuShutdown( void ) {}
const char *S_SpatialGpuName( void ) { return NULL; }
const char *S_SpatialGpuWhyNot( void ) { return "this build has no Vulkan for it"; }
void S_SpatialGpuBeginLevel( void ) {}
void S_SpatialGpuMesh( int model, const IPLVector3 *vertices, const IPLint32 *materials, int count ) {}
void S_SpatialGpuPlace( qboolean next, int model, const IPLMatrix4x4 *transform ) {}
qboolean S_SpatialGpuIdle( void ) { return qtrue; }
void S_SpatialGpuTakeLevel( void ) {}
qboolean S_SpatialGpuReady( void ) { return qfalse; }
qboolean S_SpatialGpuBeginPass( void ) { return qfalse; }
void S_SpatialGpuBatchInfo( int *batches, int *usec, int *answered ) { *batches = *usec = *answered = 0; }
void S_SpatialGpuSceneSettings( IPLSceneSettings *settings ) { memset( settings, 0, sizeof( *settings ) ); }

#endif
