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

// vk_main.cpp: instance, device, swapchain, start and end of the renderer

#include "vk_local.h"

vkState_t	vk;

static cvar_t	*v_vkDebug;			// 1: validation layer and its messages, if the layer is installed
static cvar_t	*r_swapIntervalVk;	// r_swapInterval of the engine
static cvar_t	*r_multisampleVk;	// r_ext_multisample of the engine
static cvar_t	*v_rayTracing;		// 0: no ray tracing even where the device has it (vk_rt.cpp)
static cvar_t	*v_hdr;				// the frame in 16 bit floats, tone mapped at the end of the view of the level

static char		vk_versionString[64];
static char		vk_driverString[64];

void VK_Fatal( const char *what, VkResult result ) {
	Com_Error( ERR_FATAL, "Vulkan: %s failed with %i", what, (int)result );
}

void VK_WaitIdle( void ) {
	if ( vk.device ) {
		qvkDeviceWaitIdle( vk.device );
	}
}

/*
===============
VK_StageTime

VisionJK: how long the steps of starting and stopping the renderer take, with developer 1, one line a step since
the step before (name NULL: only starts the clock). For their stall at vid_restart after switching ray tracing off
(2026-11-08); kept for the next one.
===============
*/
void VK_StageTime( const char *name ) {
	static int last;
	const int now = ri.Milliseconds();

	if ( name ) {
		ri.Printf( PRINT_DEVELOPER, "renderer start and stop: %s %i ms\n", name, now - last );
	}
	last = now;
}

/*
A machine that can not run this renderer (no Vulkan, an older one, no device that fits): VK_InitVulkan() says why
in the console and returns qfalse, with the reason as glConfig.renderer_string and no window size, and the engine
stops with it (CL_InitRenderer() of the client). It is said by returning, not by an error the engine catches: the
Windows builds link libgcc statically, an exception does not get out of this library there. What fails once a
device was found is an error as before.
*/
static qboolean VK_CanNotRun( const char *fmt, ... ) {
	static char reason[256];
	va_list args;

	va_start( args, fmt );
	Q_vsnprintf( reason, sizeof( reason ), fmt, args );
	va_end( args );
	ri.Printf( PRINT_ALL, "rd-vulkan-vision can not run here: %s\n", reason );
	glConfig.renderer_string = reason;
	return qfalse;
}

// r_swapInterval changed since the swapchain was made
qboolean VK_SwapIntervalChanged( void ) {
	return (qboolean)( r_swapIntervalVk->modified && r_swapIntervalVk->integer != vk.swapInterval );
}

static VKAPI_ATTR VkBool32 VKAPI_CALL VK_DebugMessage( VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT *data, void *user ) {
	vk.validationMessages++;
	// not with PRINT_ERROR: CL_RefPrintf() of the engine knows three levels and drops every other one
	ri.Printf( PRINT_ALL, "%sVULKAN: %s\n",
		( severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ) ? S_COLOR_RED : S_COLOR_YELLOW, data->pMessage );
	return VK_FALSE;
}

/*
===============
VK_CreateInstance
===============
*/
/*
===============================================================================

THE FUNCTIONS OF VULKAN

===============================================================================
*/

#define X( name )	PFN_##name q##name;
VK_FUNCTIONS_GLOBAL( X )
VK_FUNCTIONS_INSTANCE( X )
VK_FUNCTIONS_DEVICE( X )
VK_FUNCTIONS_RT( X )
#undef X
PFN_vkGetInstanceProcAddr		qvkGetInstanceProcAddr;
PFN_vkEnumerateInstanceVersion	qvkEnumerateInstanceVersion;

static void VK_MissingFunction( const char *name ) {
	Com_Error( ERR_FATAL, "Vulkan: the library has no %s.\nYour graphics driver is too old or incomplete for this game. Update your graphics driver.", name );
}

// the window is there, so its library of Vulkan is loaded
static qboolean VK_LoadGlobalFunctions( void ) {
	qvkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)ri.WIN_VK_GetInstanceProcAddr();
	if ( !qvkGetInstanceProcAddr ) {
		return VK_CanNotRun( "no library of Vulkan was loaded for the window" );
	}
#define X( name )	q##name = (PFN_##name)qvkGetInstanceProcAddr( VK_NULL_HANDLE, #name ); if ( !q##name ) { return VK_CanNotRun( "the library of Vulkan has no %s", #name ); }
	VK_FUNCTIONS_GLOBAL( X )
#undef X
	qvkEnumerateInstanceVersion = (PFN_vkEnumerateInstanceVersion)qvkGetInstanceProcAddr( VK_NULL_HANDLE, "vkEnumerateInstanceVersion" );
	return qtrue;
}

static qboolean VK_LoadInstanceFunctions( void ) {
#define X( name )	q##name = (PFN_##name)qvkGetInstanceProcAddr( vk.instance, #name ); if ( !q##name ) { return VK_CanNotRun( "the library of Vulkan has no %s", #name ); }
	VK_FUNCTIONS_INSTANCE( X )
#undef X
	return qtrue;
}

static void VK_LoadDeviceFunctions( void ) {
#define X( name )	q##name = (PFN_##name)qvkGetDeviceProcAddr( vk.device, #name ); if ( !q##name ) { VK_MissingFunction( #name ); }
	VK_FUNCTIONS_DEVICE( X )
	if ( vk.rayTracing ) {
		VK_FUNCTIONS_RT( X )
	}
#undef X
}

// qfalse: this machine can not run the renderer, see VK_CanNotRun()
static qboolean VK_CreateInstance( void ) {
	const char *extensions[16];
	const char *layers[1];
	int numExtensions, numLayers = 0;
	uint32_t count, version = VK_API_VERSION_1_0;
	VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	VkResult result;

	if ( !VK_LoadGlobalFunctions() ) {
		return qfalse;
	}
	if ( qvkEnumerateInstanceVersion ) {
		qvkEnumerateInstanceVersion( &version );
	}
	if ( version < VK_API_VERSION_1_3 ) {
		return VK_CanNotRun( "it needs Vulkan 1.3, this system has %i.%i", VK_API_VERSION_MAJOR( version ), VK_API_VERSION_MINOR( version ) );
	}

	numExtensions = ri.WIN_VK_InstanceExtensions( extensions, ARRAY_LEN( extensions ) - 1 );
	if ( !numExtensions ) {
		return VK_CanNotRun( "the window system names no instance extensions" );
	}

	vk.validation = qfalse;
	if ( v_vkDebug->integer ) {
		VkLayerProperties available[64];

		count = ARRAY_LEN( available );
		qvkEnumerateInstanceLayerProperties( &count, available );
		for ( uint32_t i = 0; i < count; i++ ) {
			if ( !strcmp( available[i].layerName, "VK_LAYER_KHRONOS_validation" ) ) {
				vk.validation = qtrue;
			}
		}
		if ( vk.validation ) {
			layers[numLayers++] = "VK_LAYER_KHRONOS_validation";
			extensions[numExtensions++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
		} else {
			ri.Printf( PRINT_WARNING, "v_vkDebug: the layer VK_LAYER_KHRONOS_validation is not installed\n" );
		}
	}

	app.pApplicationName = "VisionJK";
	app.pEngineName = "rd-vulkan-vision";
	app.apiVersion = VK_API_VERSION_1_3;
	info.pApplicationInfo = &app;
	info.enabledExtensionCount = (uint32_t)numExtensions;
	info.ppEnabledExtensionNames = extensions;
	info.enabledLayerCount = (uint32_t)numLayers;
	info.ppEnabledLayerNames = layers;
	// fails with VK_ERROR_INCOMPATIBLE_DRIVER where the library is there and no driver for it
	result = qvkCreateInstance( &info, NULL, &vk.instance );
	if ( result != VK_SUCCESS ) {
		vk.instance = VK_NULL_HANDLE;
		return VK_CanNotRun( "vkCreateInstance failed with %i", (int)result );
	}
	if ( !VK_LoadInstanceFunctions() ) {
		return qfalse;
	}

	if ( vk.validation ) {
		VkDebugUtilsMessengerCreateInfoEXT debug = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
		PFN_vkCreateDebugUtilsMessengerEXT create = (PFN_vkCreateDebugUtilsMessengerEXT)
			qvkGetInstanceProcAddr( vk.instance, "vkCreateDebugUtilsMessengerEXT" );

		debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debug.pfnUserCallback = VK_DebugMessage;
		if ( create ) {
			create( vk.instance, &debug, NULL, &vk.messenger );
		}
	}
	return qtrue;
}

/*
===============
VK_PickDevice

The first device that can do what the renderer needs and can present to the window, a discrete card before
the others
===============
*/
static qboolean VK_DeviceUsable( VkPhysicalDevice device, uint32_t *queueFamily ) {
	VkPhysicalDeviceProperties props;
	VkQueueFamilyProperties families[32];
	uint32_t count = ARRAY_LEN( families );
	VkPhysicalDeviceVulkan13Features f13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	VkPhysicalDeviceVulkan12Features f12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &f13 };
	VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f12 };

	qvkGetPhysicalDeviceProperties( device, &props );
	if ( props.apiVersion < VK_API_VERSION_1_3 ) {
		return qfalse;
	}
	qvkGetPhysicalDeviceFeatures2( device, &features );
	if ( !features.features.shaderClipDistance || !f13.dynamicRendering || !f13.shaderDemoteToHelperInvocation
		|| !f12.runtimeDescriptorArray || !f12.shaderSampledImageArrayNonUniformIndexing
		|| !f12.descriptorBindingPartiallyBound || !f12.descriptorBindingSampledImageUpdateAfterBind
		|| !f12.descriptorBindingUpdateUnusedWhilePending ) {
		return qfalse;
	}
	qvkGetPhysicalDeviceQueueFamilyProperties( device, &count, families );
	for ( uint32_t i = 0; i < count; i++ ) {
		VkBool32 present = VK_FALSE;

		qvkGetPhysicalDeviceSurfaceSupportKHR( device, i, vk.surface, &present );
		if ( present && ( families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT ) ) {
			*queueFamily = i;
			return qtrue;
		}
	}
	return qfalse;
}

// qfalse: this machine can not run the renderer, see VK_CanNotRun()
static qboolean VK_PickDevice( void ) {
	VkPhysicalDevice devices[16];
	uint32_t count = ARRAY_LEN( devices );
	uint32_t family = 0;

	qvkEnumeratePhysicalDevices( vk.instance, &count, devices );
	vk.physicalDevice = VK_NULL_HANDLE;
	for ( int pass = 0; pass < 2 && !vk.physicalDevice; pass++ ) {
		for ( uint32_t i = 0; i < count; i++ ) {
			VkPhysicalDeviceProperties props;

			qvkGetPhysicalDeviceProperties( devices[i], &props );
			if ( pass == 0 && props.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ) {
				continue;
			}
			if ( VK_DeviceUsable( devices[i], &family ) ) {
				vk.physicalDevice = devices[i];
				vk.queueFamily = family;
				break;
			}
		}
	}
	if ( !vk.physicalDevice ) {
		return VK_CanNotRun( "no graphics device with Vulkan 1.3 and descriptor indexing" );
	}
	qvkGetPhysicalDeviceProperties( vk.physicalDevice, &vk.properties );
	qvkGetPhysicalDeviceMemoryProperties( vk.physicalDevice, &vk.memoryProperties );
	return qtrue;
}

/*
===============
VK_RayTracingUsable

The device builds acceleration structures and its shaders trace rays through them (VK_KHR_ray_query): RTX 20 and
later, Radeon RX 6000 and later, Intel Arc and many newer integrated GPUs. The shadows of the players are traced
there (vk_rt.cpp); every other device draws them without.
===============
*/
static qboolean VK_RayTracingUsable( void ) {
	const char *needed[] = { VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME,
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME };
	VkExtensionProperties available[512];
	uint32_t count = ARRAY_LEN( available );
	VkPhysicalDeviceRayQueryFeaturesKHR query = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
	VkPhysicalDeviceAccelerationStructureFeaturesKHR accel = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &query };
	VkPhysicalDeviceVulkan12Features f12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &accel };
	VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f12 };

	if ( !v_rayTracing->integer ) {
		return qfalse;
	}
	// (VK_INCOMPLETE leaves the list as long as the array, the ones needed are rarely at its end)
	if ( qvkEnumerateDeviceExtensionProperties( vk.physicalDevice, NULL, &count, available ) < 0 ) {
		return qfalse;
	}
	for ( size_t i = 0; i < ARRAY_LEN( needed ); i++ ) {
		uint32_t j;

		for ( j = 0; j < count && strcmp( available[j].extensionName, needed[i] ); j++ ) {
		}
		if ( j == count ) {
			return qfalse;
		}
	}
	qvkGetPhysicalDeviceFeatures2( vk.physicalDevice, &features );
	return (qboolean)( accel.accelerationStructure && query.rayQuery && f12.bufferDeviceAddress );
}

static void VK_CreateDevice( void ) {
	const float priority = 1.0f;
	const char *extensions[4] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	uint32_t numExtensions = 1;
	VkDeviceQueueCreateInfo queue = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	VkPhysicalDeviceRayQueryFeaturesKHR query = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
	VkPhysicalDeviceAccelerationStructureFeaturesKHR accel = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &query };
	VkPhysicalDeviceVulkan13Features f13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	VkPhysicalDeviceVulkan12Features f12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &f13 };
	VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f12 };
	VkPhysicalDeviceFeatures available;
	VkDeviceCreateInfo info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &features };
	VkCommandPoolCreateInfo pool = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };

	qvkGetPhysicalDeviceFeatures( vk.physicalDevice, &available );
	features.features.samplerAnisotropy = available.samplerAnisotropy;
	vk.anisotropy = (qboolean)( available.samplerAnisotropy != VK_FALSE );
	features.features.fillModeNonSolid = available.fillModeNonSolid;
	vk.wireframe = (qboolean)( available.fillModeNonSolid != VK_FALSE );
	features.features.tessellationShader = available.tessellationShader;
	vk.tessellation = (qboolean)( available.tessellationShader != VK_FALSE );
	features.features.textureCompressionBC = available.textureCompressionBC;
	vk.bcTextures = (qboolean)( available.textureCompressionBC != VK_FALSE && r_ext_compressed_textures->integer );
	features.features.shaderClipDistance = VK_TRUE;
	f13.dynamicRendering = VK_TRUE;
	// glslc turns the discard of a fragment shader into this for Vulkan 1.3
	f13.shaderDemoteToHelperInvocation = VK_TRUE;
	f12.runtimeDescriptorArray = VK_TRUE;
	f12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	f12.descriptorBindingPartiallyBound = VK_TRUE;
	f12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
	f12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
	vk.rayTracing = VK_RayTracingUsable();
	// the players are skinned for their shadows by a compute shader that finds its buffers by their addresses, on
	// the shadow maps of a device without ray tracing as well (vk_rt.cpp)
	{
		VkPhysicalDeviceVulkan12Features has12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		VkPhysicalDeviceFeatures2 has = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &has12 };

		qvkGetPhysicalDeviceFeatures2( vk.physicalDevice, &has );
		vk.deviceAddress = (qboolean)( has12.bufferDeviceAddress != VK_FALSE );
		f12.bufferDeviceAddress = has12.bufferDeviceAddress;
	}
	if ( vk.rayTracing ) {
		extensions[numExtensions++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
		extensions[numExtensions++] = VK_KHR_RAY_QUERY_EXTENSION_NAME;
		extensions[numExtensions++] = VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
		accel.accelerationStructure = VK_TRUE;
		query.rayQuery = VK_TRUE;
		f12.bufferDeviceAddress = VK_TRUE;
		f13.pNext = &accel;
	}

	queue.queueFamilyIndex = vk.queueFamily;
	queue.queueCount = 1;
	queue.pQueuePriorities = &priority;
	info.queueCreateInfoCount = 1;
	info.pQueueCreateInfos = &queue;
	info.enabledExtensionCount = numExtensions;
	info.ppEnabledExtensionNames = extensions;
	VK_CHECK( qvkCreateDevice( vk.physicalDevice, &info, NULL, &vk.device ) );
	VK_LoadDeviceFunctions();
	qvkGetDeviceQueue( vk.device, vk.queueFamily, 0, &vk.queue );

	pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool.queueFamilyIndex = vk.queueFamily;
	VK_CHECK( qvkCreateCommandPool( vk.device, &pool, NULL, &vk.commandPool ) );
}

static void VK_DestroySwapSemaphores( void ) {
	for ( uint32_t i = 0; i < VK_MAX_SWAP_IMAGES; i++ ) {
		if ( vk.swapReady[i] ) {
			qvkDestroySemaphore( vk.device, vk.swapReady[i], NULL );
			vk.swapReady[i] = VK_NULL_HANDLE;
		}
	}
}

/*
===============
VK_CreateSwapchain

Also after the window surface changed. The game does not draw into the swapchain, it draws into targets of its
own size and the result is copied over, so the swapchain may have any size and format.
===============
*/
void VK_CreateSwapchain( void ) {
	VkSurfaceCapabilitiesKHR caps;
	VkSurfaceFormatKHR formats[64];
	VkPresentModeKHR modes[16];
	uint32_t numFormats = ARRAY_LEN( formats ), numModes = ARRAY_LEN( modes );
	VkSwapchainCreateInfoKHR info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
	VkSwapchainKHR old = vk.swapchain;

	VK_CHECK( qvkGetPhysicalDeviceSurfaceCapabilitiesKHR( vk.physicalDevice, vk.surface, &caps ) );
	qvkGetPhysicalDeviceSurfaceFormatsKHR( vk.physicalDevice, vk.surface, &numFormats, formats );
	qvkGetPhysicalDeviceSurfacePresentModesKHR( vk.physicalDevice, vk.surface, &numModes, modes );
	if ( !numFormats ) {
		Com_Error( ERR_FATAL, "Vulkan: the window surface has no formats" );
	}

	// the engine applies its own gamma, so no sRGB format
	info.imageFormat = formats[0].format;
	info.imageColorSpace = formats[0].colorSpace;
	for ( uint32_t i = 0; i < numFormats; i++ ) {
		if ( formats[i].format == VK_FORMAT_B8G8R8A8_UNORM || formats[i].format == VK_FORMAT_R8G8B8A8_UNORM ) {
			info.imageFormat = formats[i].format;
			info.imageColorSpace = formats[i].colorSpace;
			break;
		}
	}

	// r_swapInterval 0: as fast as it goes, without tearing if the driver has that
	vk.swapInterval = r_swapIntervalVk->integer;
	r_swapIntervalVk->modified = qfalse;
	info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	if ( !vk.swapInterval ) {
		for ( uint32_t i = 0; i < numModes; i++ ) {
			if ( modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR ) {
				info.presentMode = modes[i];
				break;
			}
			if ( modes[i] == VK_PRESENT_MODE_MAILBOX_KHR ) {
				info.presentMode = modes[i];
			}
		}
	}

	if ( caps.currentExtent.width != 0xffffffffu ) {
		info.imageExtent = caps.currentExtent;
	} else {
		info.imageExtent.width = Com_Clampi( (int)caps.minImageExtent.width, (int)caps.maxImageExtent.width, glConfig.vidWidth );
		info.imageExtent.height = Com_Clampi( (int)caps.minImageExtent.height, (int)caps.maxImageExtent.height, glConfig.vidHeight );
	}
	info.surface = vk.surface;
	info.minImageCount = caps.minImageCount + 1;
	if ( caps.maxImageCount && info.minImageCount > caps.maxImageCount ) {
		info.minImageCount = caps.maxImageCount;
	}
	info.imageArrayLayers = 1;
	info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.preTransform = caps.currentTransform;
	info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	if ( !( caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR ) ) {
		info.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
	}
	info.clipped = VK_TRUE;
	info.oldSwapchain = old;

	if ( !info.imageExtent.width || !info.imageExtent.height ) {
		// minimized, there is nothing to present to
		vk.swapchain = VK_NULL_HANDLE;
	} else {
		VK_CHECK( qvkCreateSwapchainKHR( vk.device, &info, NULL, &vk.swapchain ) );
	}
	if ( old ) {
		qvkDestroySwapchainKHR( vk.device, old, NULL );
	}
	// only now, the old swapchain may have held on to them
	VK_DestroySwapSemaphores();
	vk.swapFormat = info.imageFormat;
	vk.swapExtent = info.imageExtent;
	vk.numSwapImages = 0;
	vk.swapStallTime = 0;
	vk.statSwapchains++;
	ri.Printf( PRINT_DEVELOPER, "swapchain: %i x %i, present mode %i\n", (int)info.imageExtent.width, (int)info.imageExtent.height,
		(int)info.presentMode );
	if ( vk.swapchain ) {
		const VkSemaphoreCreateInfo semaphore = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

		vk.numSwapImages = VK_MAX_SWAP_IMAGES;
		VK_CHECK( qvkGetSwapchainImagesKHR( vk.device, vk.swapchain, &vk.numSwapImages, vk.swapImages ) );
		for ( uint32_t i = 0; i < vk.numSwapImages; i++ ) {
			VK_CHECK( qvkCreateSemaphore( vk.device, &semaphore, NULL, &vk.swapReady[i] ) );
		}
	}
}

void VK_DestroySwapchain( void ) {
	if ( vk.swapchain ) {
		qvkDestroySwapchainKHR( vk.device, vk.swapchain, NULL );
		vk.swapchain = VK_NULL_HANDLE;
	}
	VK_DestroySwapSemaphores();
}

static VkFormat VK_DepthFormat( void ) {
	const VkFormat candidates[] = { VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT };

	for ( size_t i = 0; i < ARRAY_LEN( candidates ); i++ ) {
		VkFormatProperties props;

		qvkGetPhysicalDeviceFormatProperties( vk.physicalDevice, candidates[i], &props );
		if ( props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT ) {
			return candidates[i];
		}
	}
	Com_Error( ERR_FATAL, "Vulkan: no depth format with stencil" );
	return VK_FORMAT_UNDEFINED;
}

/*
===============
VK_SampleCount

r_ext_multisample as the engine has it for OpenGL windows: 0 or 1 none, else the largest of 2, 4 and 8 samples
a pixel that is not more than asked for and that the device can do for color and depth
===============
*/
static VkSampleCountFlagBits VK_SampleCount( void ) {
	const VkSampleCountFlags supported = vk.properties.limits.framebufferColorSampleCounts
		& vk.properties.limits.framebufferDepthSampleCounts & vk.properties.limits.framebufferStencilSampleCounts;

	for ( int samples = 8; samples > 1; samples >>= 1 ) {
		if ( r_multisampleVk->integer >= samples && ( supported & (VkSampleCountFlags)samples ) ) {
			return (VkSampleCountFlagBits)samples;		// the bits are the counts
		}
	}
	return VK_SAMPLE_COUNT_1_BIT;
}

// the targets of the frame in the size of the window: what the scene and the 2D are drawn into, and the picture
// after the gamma pass that is copied to the window
// the format of the frame and of everything the level is drawn into: 16 bit floats with v_hdr (light above 1 stays
// until the tone map of vk_post.cpp), else 8 bits
static VkFormat VK_FrameFormat( void ) {
	return v_hdr->integer ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
}

static void VK_CreateFrameTargets( void ) {
	VK_CreateTarget( &vk.color, vk.width, vk.height, VK_FrameFormat(),
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT );
	VK_CreateTarget( &vk.final, vk.width, vk.height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT );
	VK_CreateTarget( &vk.depth, vk.width, vk.height, VK_DepthFormat(),
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
			| ( vk.depthReadable ? VK_IMAGE_USAGE_SAMPLED_BIT : 0 ),
		VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, VK_SAMPLE_COUNT_1_BIT );
	if ( vk.samples > VK_SAMPLE_COUNT_1_BIT ) {
		VK_CreateTarget( &vk.colorMS, vk.width, vk.height, VK_FrameFormat(), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT, vk.samples );
		VK_CreateTarget( &vk.depthMS, vk.width, vk.height, VK_DepthFormat(), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, vk.samples );
	}
}

static void VK_DestroyFrameTargets( void ) {
	VK_DestroyTarget( &vk.color );
	VK_DestroyTarget( &vk.final );
	VK_DestroyTarget( &vk.depth );
	VK_DestroyTarget( &vk.colorMS );
	VK_DestroyTarget( &vk.depthMS );
}

/*
===============
VK_ResizeFrame

The window has a new size (the desktop resized it, a tiling window manager among others). The frame, the swapchain
and every target of the size of the window are made anew in it, between two frames, without a vid_restart:
textures, the level and the models stay. The sets that show the targets stay too and are written again.
===============
*/
qboolean VK_ResizeFrame( int width, int height ) {
	if ( !vk.device || vk.frame ) {
		return qfalse;
	}
	qvkDeviceWaitIdle( vk.device );
	VK_DestroyPostTargets();
	VK_DestroyFrameTargets();
	vk.width = glConfig.vidWidth = width;
	vk.height = glConfig.vidHeight = height;
	VK_CreateFrameTargets();
	VK_WriteSet( vk.postSet, vk.postSampler, vk.color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_InitPost();
	vk.finalValid = qfalse;
	// the size Wayland takes the swapchain in, see VK_CreateSwapchain()
	VK_CreateSwapchain();
	ri.Printf( PRINT_DEVELOPER, "window resized to %i x %i\n", width, height );
	return qtrue;
}

/*
===============
VK_InitVulkan

Called by R_Init() in the place of InitOpenGL(). The window and everything on the device live until the
window is destroyed (vid_restart), a map change keeps them.

qfalse: this machine can not run the renderer, see VK_CanNotRun(). glConfig is empty then, which is how the
engine knows. A window that was made stays until RE_Shutdown() destroys it.
===============
*/
static void VK_DestroyInstance( void );

qboolean VK_InitVulkan( void ) {
	v_vkDebug = ri.Cvar_Get( "v_vkDebug", "0", CVAR_LATCH, "Vulkan validation layer and its messages" );
	r_swapIntervalVk = ri.Cvar_Get( "r_swapInterval", "0", CVAR_ARCHIVE_ND, "" );
	r_multisampleVk = ri.Cvar_Get( "r_ext_multisample", "0", CVAR_ARCHIVE_ND | CVAR_LATCH, "" );
	v_hdr = ri.Cvar_Get( "v_hdr", "1", CVAR_ARCHIVE_ND | CVAR_LATCH,
		"The level is drawn with light brighter than white (16 bit floats), and tone mapped to the screen at its end (v_tonemap, v_exposure): sabers, glows and lamps glow out instead of clipping. 0: 8 bits as rd-vanilla" );
	v_rayTracing = ri.Cvar_Get( "v_rayTracing", "1", CVAR_ARCHIVE_ND | CVAR_LATCH,
		"Ray tracing where the graphics card has it, for the shadows of the players. 0: off" );
	if ( VK_InitShared() ) {
		return qtrue;
	}

	windowDesc_t windowDesc = { GRAPHICS_API_VULKAN };

	VK_StageTime( NULL );
	memset( &glConfig, 0, sizeof( glConfig ) );
	memset( &vk, 0, sizeof( vk ) );
	window = ri.WIN_Init( &windowDesc, &glConfig );
	if ( !glConfig.vidWidth ) {
		// WIN_Init() of the engine hands back a window without a size where none with Vulkan can be made
		return VK_CanNotRun( "no window for Vulkan could be made" );
	}
	vk.width = glConfig.vidWidth;
	vk.height = glConfig.vidHeight;
	VK_StageTime( "window" );

	if ( !VK_CreateInstance() ) {
		VK_DestroyInstance();
		return qfalse;
	}
	if ( !ri.WIN_VK_CreateSurface( vk.instance, &vk.surface ) ) {
		vk.surface = VK_NULL_HANDLE;
		VK_DestroyInstance();
		return VK_CanNotRun( "no surface for the window" );
	}
	if ( !VK_PickDevice() ) {
		VK_DestroyInstance();
		return qfalse;
	}
	VK_StageTime( "instance" );
	VK_CreateDevice();
	VK_InitMemory();
	VK_StageTime( "device" );
	VK_CreateSwapchain();

	{
		VkFormatProperties props;

		// the depth of field reads the depth of the scene as a texture
		qvkGetPhysicalDeviceFormatProperties( vk.physicalDevice, VK_DepthFormat(), &props );
		vk.depthReadable = (qboolean)( ( props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT ) != 0 );
	}
	// anti aliasing: the scene goes into targets with several samples a pixel, see VK_BeginRendering()
	vk.samples = VK_SampleCount();
	VK_CreateFrameTargets();
	VK_StageTime( "swapchain and targets" );

	VK_InitPipelines();
	VK_StageTime( "pipelines" );
	VK_InitPost();
	VK_InitFrames();
	VK_InitImages();
	VK_InitModels();
	VK_StageTime( "post, frames, images" );
	VK_RT_Init();
	VK_StageTime( "ray tracing and shadow maps" );
	// the cgame asks for the shadows of the players where the renderer has them (with traced rays, or with shadow maps
	// where the card has no ray tracing, vk_rt.cpp)
	ri.Cvar_Get( "v_rtAvailable", "0", CVAR_ROM, "Whether the renderer draws the shadows of the players itself on this graphics card (cg_shadows 2): by rays, or by shadow maps" );
	ri.Cvar_Set( "v_rtAvailable", VK_RT_Ready() ? "1" : "0" );

	// what the engine, cgame and ui want to know about the renderer
	Com_sprintf( vk_versionString, sizeof( vk_versionString ), "Vulkan %i.%i.%i", VK_API_VERSION_MAJOR( vk.properties.apiVersion ),
		VK_API_VERSION_MINOR( vk.properties.apiVersion ), VK_API_VERSION_PATCH( vk.properties.apiVersion ) );
	Com_sprintf( vk_driverString, sizeof( vk_driverString ), "vendor 0x%04x, driver 0x%x", vk.properties.vendorID,
		vk.properties.driverVersion );
	glConfig.renderer_string = vk.properties.deviceName;
	glConfig.vendor_string = vk_driverString;
	glConfig.version_string = vk_versionString;
	glConfig.maxTextureSize = Q_min( (int)vk.properties.limits.maxImageDimension2D, 8192 );
	glConfig.maxTextureFilterAnisotropy = vk.anisotropy ? vk.properties.limits.maxSamplerAnisotropy : 0.0f;
	VK_InitConfig();

	ri.Printf( PRINT_ALL, "Vulkan device: %s, %s\n", vk.properties.deviceName, vk_versionString );
	VK_Splash();
	return qtrue;
}

/*
===============
VK_ShutdownVulkan

Called by RE_Shutdown() before the window goes. Without destroyWindow (map change) everything stays.
===============
*/
void VK_ShutdownVulkan( qboolean destroyWindow ) {
	if ( !vk.device ) {
		return;
	}
	VK_StageTime( NULL );
	qvkDeviceWaitIdle( vk.device );
	VK_StageTime( "waiting for the device at the end" );
	if ( !destroyWindow ) {
		return;
	}

	VK_RT_Shutdown();
	VK_ShutdownWorld();
	VK_ShutdownModels();
	VK_ShutdownImages();
	VK_ShutdownFrames();
	VK_DestroyPostTargets();
	VK_ShutdownPipelines();
	VK_DestroyFrameTargets();
	VK_DestroySwapchain();
	VK_ShutdownMemory();
	qvkDestroyCommandPool( vk.device, vk.commandPool, NULL );
	qvkDestroyDevice( vk.device, NULL );
	VK_DestroyInstance();
	VK_StageTime( "shutdown" );
}

// The end of the instance and of what hangs on it alone, and of what the renderer knows about the window. Also
// for a start that stopped before it had a device: then some of it was never made.
static void VK_DestroyInstance( void ) {
	if ( vk.instance ) {
		if ( vk.surface && qvkDestroySurfaceKHR ) {
			qvkDestroySurfaceKHR( vk.instance, vk.surface, NULL );
		}
		if ( vk.messenger ) {
			PFN_vkDestroyDebugUtilsMessengerEXT destroy = (PFN_vkDestroyDebugUtilsMessengerEXT)
				qvkGetInstanceProcAddr( vk.instance, "vkDestroyDebugUtilsMessengerEXT" );

			if ( destroy ) {
				destroy( vk.instance, vk.messenger, NULL );
			}
		}
		if ( qvkDestroyInstance ) {
			qvkDestroyInstance( vk.instance, NULL );
		}
	}
	memset( &vk, 0, sizeof( vk ) );
	memset( &glConfig, 0, sizeof( glConfig ) );
}
int G2API_SlotsInUse( void );		// common/G2_API.cpp
int G2API_ListSlots( qboolean print );


/*
===============
VK_GfxInfo_f
===============
*/
void VK_GfxInfo_f( void ) {
	int numImages, numBlockImages;

	VK_CountImages( &numImages, &numBlockImages );
	ri.Printf( PRINT_ALL, "\nrd-vulkan-vision\n" );
	ri.Printf( PRINT_ALL, "device: %s\n", vk.properties.deviceName );
	ri.Printf( PRINT_ALL, "version: %s\n", vk_versionString );
	ri.Printf( PRINT_ALL, "driver: %s\n", vk_driverString );
	ri.Printf( PRINT_ALL, "frame: %i x %i with %i samples a pixel, window surface: %i x %i, %i images, swap interval %i\n", vk.width, vk.height,
		(int)vk.samples, (int)vk.swapExtent.width, (int)vk.swapExtent.height, (int)vk.numSwapImages, vk.swapInterval );
	ri.Printf( PRINT_ALL, "swapchains made: %i, frames that were not shown: %i\n", vk.statSwapchains, vk.statFramesNotShown );
	// they are kept over a restart of the renderer: a number that grows with every map is a leak
	ri.Printf( PRINT_ALL, "ghoul2 instances: %i of 1024 slots in use, %i of them without a model\n", G2API_SlotsInUse(),
		G2API_ListSlots( (qboolean)( ri.Cvar_VariableIntegerValue( "developer" ) != 0 ) ) );
	ri.Printf( PRINT_ALL, "textures: %i of %i, %i of them compressed, pipelines: %i (made in %i ms), samplers: %i\n", numImages,
		VK_MAX_IMAGES, numBlockImages, vk.numPipelines, vk.statPipelineMs, vk.numSamplers );
	VK_FrameInfo_f();
	VK_MemoryInfo_f();
	ri.Printf( PRINT_ALL, "validation layer: %s", vk.validation ? "on" : "off" );
	if ( vk.validation ) {
		ri.Printf( PRINT_ALL, ", %i messages so far", vk.validationMessages );
	}
	ri.Printf( PRINT_ALL, "\noverbright bits: %i, gamma: %.2f\n", tr.overbrightBits, r_gamma->value );
}
