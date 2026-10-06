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

// vk_main.cpp: instance, device, swapchain, start and end of the renderer

#include "vk_local.h"

vkState_t	vk;

static cvar_t	*r_vkDebug;			// 1: validation layer and its messages, if the layer is installed
static cvar_t	*r_swapIntervalVk;	// r_swapInterval of the engine
static cvar_t	*r_multisampleVk;	// r_ext_multisample of the engine

static char		vk_versionString[64];
static char		vk_driverString[64];

void VK_Fatal( const char *what, VkResult result ) {
	Com_Error( ERR_FATAL, "Vulkan: %s failed with %i", what, (int)result );
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
#undef X
PFN_vkGetInstanceProcAddr		qvkGetInstanceProcAddr;
PFN_vkEnumerateInstanceVersion	qvkEnumerateInstanceVersion;

static void VK_MissingFunction( const char *name ) {
	Com_Error( ERR_FATAL, "Vulkan: the library has no %s.\nSet cl_renderer to rd-vanilla.", name );
}

// the window is there, so its library of Vulkan is loaded
static void VK_LoadGlobalFunctions( void ) {
	qvkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)ri.WIN_VK_GetInstanceProcAddr();
	if ( !qvkGetInstanceProcAddr ) {
		Com_Error( ERR_FATAL, "Vulkan: no library of Vulkan was loaded for the window.\nSet cl_renderer to rd-vanilla." );
	}
#define X( name )	q##name = (PFN_##name)qvkGetInstanceProcAddr( VK_NULL_HANDLE, #name ); if ( !q##name ) { VK_MissingFunction( #name ); }
	VK_FUNCTIONS_GLOBAL( X )
#undef X
	qvkEnumerateInstanceVersion = (PFN_vkEnumerateInstanceVersion)qvkGetInstanceProcAddr( VK_NULL_HANDLE, "vkEnumerateInstanceVersion" );
}

static void VK_LoadInstanceFunctions( void ) {
#define X( name )	q##name = (PFN_##name)qvkGetInstanceProcAddr( vk.instance, #name ); if ( !q##name ) { VK_MissingFunction( #name ); }
	VK_FUNCTIONS_INSTANCE( X )
#undef X
}

static void VK_LoadDeviceFunctions( void ) {
#define X( name )	q##name = (PFN_##name)qvkGetDeviceProcAddr( vk.device, #name ); if ( !q##name ) { VK_MissingFunction( #name ); }
	VK_FUNCTIONS_DEVICE( X )
#undef X
}

static void VK_CreateInstance( void ) {
	const char *extensions[16];
	const char *layers[1];
	int numExtensions, numLayers = 0;
	uint32_t count, version = VK_API_VERSION_1_0;
	VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };

	VK_LoadGlobalFunctions();
	if ( qvkEnumerateInstanceVersion ) {
		qvkEnumerateInstanceVersion( &version );
	}
	if ( version < VK_API_VERSION_1_3 ) {
		Com_Error( ERR_FATAL, "rd-vulkan-vision needs Vulkan 1.3, this system has %i.%i.\n"
			"Set cl_renderer to rd-vanilla.", VK_API_VERSION_MAJOR( version ), VK_API_VERSION_MINOR( version ) );
	}

	numExtensions = ri.WIN_VK_InstanceExtensions( extensions, ARRAY_LEN( extensions ) - 1 );
	if ( !numExtensions ) {
		Com_Error( ERR_FATAL, "Vulkan: the window system names no instance extensions" );
	}

	vk.validation = qfalse;
	if ( r_vkDebug->integer ) {
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
			ri.Printf( PRINT_WARNING, "r_vkDebug: the layer VK_LAYER_KHRONOS_validation is not installed\n" );
		}
	}

	app.pApplicationName = "OpenJK";
	app.pEngineName = "rd-vulkan-vision";
	app.apiVersion = VK_API_VERSION_1_3;
	info.pApplicationInfo = &app;
	info.enabledExtensionCount = (uint32_t)numExtensions;
	info.ppEnabledExtensionNames = extensions;
	info.enabledLayerCount = (uint32_t)numLayers;
	info.ppEnabledLayerNames = layers;
	VK_CHECK( qvkCreateInstance( &info, NULL, &vk.instance ) );
	VK_LoadInstanceFunctions();

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

static void VK_PickDevice( void ) {
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
		Com_Error( ERR_FATAL, "rd-vulkan-vision found no graphics device with Vulkan 1.3 and descriptor indexing.\n"
			"Set cl_renderer to rd-vanilla." );
	}
	qvkGetPhysicalDeviceProperties( vk.physicalDevice, &vk.properties );
	qvkGetPhysicalDeviceMemoryProperties( vk.physicalDevice, &vk.memoryProperties );
}

static void VK_CreateDevice( void ) {
	const float priority = 1.0f;
	const char *extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	VkDeviceQueueCreateInfo queue = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	VkPhysicalDeviceVulkan13Features f13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	VkPhysicalDeviceVulkan12Features f12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &f13 };
	VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f12 };
	VkPhysicalDeviceFeatures available;
	VkDeviceCreateInfo info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &features };
	VkCommandPoolCreateInfo pool = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };

	qvkGetPhysicalDeviceFeatures( vk.physicalDevice, &available );
	features.features.samplerAnisotropy = available.samplerAnisotropy;
	features.features.fillModeNonSolid = available.fillModeNonSolid;
	vk.wireframe = (qboolean)( available.fillModeNonSolid != VK_FALSE );
	features.features.shaderClipDistance = VK_TRUE;
	f13.dynamicRendering = VK_TRUE;
	// glslc turns the discard of a fragment shader into this for Vulkan 1.3
	f13.shaderDemoteToHelperInvocation = VK_TRUE;
	f12.runtimeDescriptorArray = VK_TRUE;
	f12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	f12.descriptorBindingPartiallyBound = VK_TRUE;
	f12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
	f12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;

	queue.queueFamilyIndex = vk.queueFamily;
	queue.queueCount = 1;
	queue.pQueuePriorities = &priority;
	info.queueCreateInfoCount = 1;
	info.pQueueCreateInfos = &queue;
	info.enabledExtensionCount = ARRAY_LEN( extensions );
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

/*
===============
VK_InitVulkan

Called by R_Init() in the place of InitOpenGL(). The window and everything on the device live until the
window is destroyed (vid_restart), a map change keeps them.
===============
*/
void VK_InitVulkan( void ) {
	r_vkDebug = ri.Cvar_Get( "r_vkDebug", "0", CVAR_ARCHIVE_ND | CVAR_LATCH, "Vulkan validation layer and its messages" );
	r_swapIntervalVk = ri.Cvar_Get( "r_swapInterval", "0", CVAR_ARCHIVE_ND, "" );
	r_multisampleVk = ri.Cvar_Get( "r_ext_multisample", "0", CVAR_ARCHIVE_ND | CVAR_LATCH, "" );
	r_vkStatic = ri.Cvar_Get( "r_vkStatic", "1", CVAR_CHEAT, "Level and models from buffers on the device. 0: all vertices are built on the CPU every frame" );

	// the builtin images are made again, the copy of the frame has to be put into the texture table again
	vk.screenSlot = -1;
	// the buffers of the level and of the models are filled again when they are first drawn
	VK_WorldChanged();
	VK_ModelsChanged();
	VK_ClearFlares();
	if ( glConfig.vidWidth ) {
		return;
	}

	windowDesc_t windowDesc = { GRAPHICS_API_VULKAN };

	memset( &glConfig, 0, sizeof( glConfig ) );
	memset( &glConfigExt, 0, sizeof( glConfigExt ) );
	memset( &vk, 0, sizeof( vk ) );
	window = ri.WIN_Init( &windowDesc, &glConfig );
	vk.width = glConfig.vidWidth;
	vk.height = glConfig.vidHeight;

	VK_CreateInstance();
	if ( !ri.WIN_VK_CreateSurface( vk.instance, &vk.surface ) ) {
		Com_Error( ERR_FATAL, "Vulkan: no surface for the window" );
	}
	VK_PickDevice();
	VK_CreateDevice();
	VK_InitMemory();
	VK_CreateSwapchain();

	VK_CreateTarget( &vk.color, vk.width, vk.height, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT );
	VK_CreateTarget( &vk.final, vk.width, vk.height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT );
	VK_CreateTarget( &vk.depth, vk.width, vk.height, VK_DepthFormat(),
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, VK_SAMPLE_COUNT_1_BIT );
	// anti aliasing: the scene goes into targets with several samples a pixel, see VK_BeginTarget()
	vk.samples = VK_SampleCount();
	if ( vk.samples > VK_SAMPLE_COUNT_1_BIT ) {
		VK_CreateTarget( &vk.colorMS, vk.width, vk.height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT, vk.samples );
		VK_CreateTarget( &vk.depthMS, vk.width, vk.height, VK_DepthFormat(), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, vk.samples );
	}

	VK_InitPipelines();
	VK_InitPost();
	VK_InitFrames();
	VK_InitImages();
	VK_InitModels();

	// what the engine, cgame and ui want to know about the renderer
	Com_sprintf( vk_versionString, sizeof( vk_versionString ), "Vulkan %i.%i.%i", VK_API_VERSION_MAJOR( vk.properties.apiVersion ),
		VK_API_VERSION_MINOR( vk.properties.apiVersion ), VK_API_VERSION_PATCH( vk.properties.apiVersion ) );
	Com_sprintf( vk_driverString, sizeof( vk_driverString ), "vendor 0x%04x, driver 0x%x", vk.properties.vendorID,
		vk.properties.driverVersion );
	glConfig.renderer_string = vk.properties.deviceName;
	glConfig.vendor_string = vk_driverString;
	glConfig.version_string = vk_versionString;
	glConfig.extensions_string = "";
	glConfigExt.originalExtensionString = "";
	glConfig.maxTextureSize = Q_min( (int)vk.properties.limits.maxImageDimension2D, 8192 );
	glConfig.maxActiveTextures = NUM_TEXTURE_BUNDLES;
	glConfig.maxTextureFilterAnisotropy = vk.properties.limits.maxSamplerAnisotropy;
	glConfig.colorBits = 32;
	glConfig.depthBits = 24;
	glConfig.stencilBits = 8;
	glConfig.textureCompression = TC_NONE;
	glConfig.textureEnvAddAvailable = qtrue;
	glConfig.clampToEdgeAvailable = qtrue;
	glConfig.stereoEnabled = qfalse;
	// gamma and overbright are one pass over the finished frame (shaders/gamma.frag), not a ramp of the display
	glConfig.deviceSupportsGamma = qfalse;
	glConfigExt.doGammaCorrectionWithShaders = qtrue;
	glConfigExt.doStencilShadowsInOneDrawcall = qtrue;	// front and back faces have stencil operations of their own

	ri.Printf( PRINT_ALL, "Vulkan device: %s, %s\n", vk.properties.deviceName, vk_versionString );
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
	qvkDeviceWaitIdle( vk.device );
	if ( !destroyWindow ) {
		return;
	}

	VK_ShutdownWorld();
	VK_ShutdownModels();
	VK_ShutdownImages();
	VK_ShutdownFrames();
	VK_ShutdownPost();
	VK_ShutdownPipelines();
	VK_DestroyTarget( &vk.color );
	VK_DestroyTarget( &vk.final );
	VK_DestroyTarget( &vk.depth );
	VK_DestroyTarget( &vk.colorMS );
	VK_DestroyTarget( &vk.depthMS );
	VK_DestroySwapchain();
	VK_ShutdownMemory();
	qvkDestroyCommandPool( vk.device, vk.commandPool, NULL );
	qvkDestroyDevice( vk.device, NULL );
	qvkDestroySurfaceKHR( vk.instance, vk.surface, NULL );
	if ( vk.messenger ) {
		PFN_vkDestroyDebugUtilsMessengerEXT destroy = (PFN_vkDestroyDebugUtilsMessengerEXT)
			qvkGetInstanceProcAddr( vk.instance, "vkDestroyDebugUtilsMessengerEXT" );

		if ( destroy ) {
			destroy( vk.instance, vk.messenger, NULL );
		}
	}
	qvkDestroyInstance( vk.instance, NULL );
	memset( &vk, 0, sizeof( vk ) );
	memset( &glConfig, 0, sizeof( glConfig ) );
}

/*
===============
VK_GfxInfo_f
===============
*/
void VK_GfxInfo_f( void ) {
	int numImages = 0;

	for ( int i = 0; i < VK_MAX_IMAGES; i++ ) {
		if ( vk.images[i].owner ) {
			numImages++;
		}
	}
	ri.Printf( PRINT_ALL, "\nrd-vulkan-vision\n" );
	ri.Printf( PRINT_ALL, "device: %s\n", vk.properties.deviceName );
	ri.Printf( PRINT_ALL, "version: %s\n", vk_versionString );
	ri.Printf( PRINT_ALL, "driver: %s\n", vk_driverString );
	ri.Printf( PRINT_ALL, "frame: %i x %i with %i samples a pixel, window surface: %i x %i, %i images, swap interval %i\n", vk.width, vk.height,
		(int)vk.samples, (int)vk.swapExtent.width, (int)vk.swapExtent.height, (int)vk.numSwapImages, vk.swapInterval );
	ri.Printf( PRINT_ALL, "swapchains made: %i, frames that were not shown: %i\n", vk.statSwapchains, vk.statFramesNotShown );
	ri.Printf( PRINT_ALL, "textures: %i of %i, pipelines: %i, samplers: %i\n", numImages, VK_MAX_IMAGES, vk.numPipelines,
		vk.numSamplers );
	ri.Printf( PRINT_ALL, "last frame: %i draws, %i triangles from buffers on the device, %i triangles and %i vertices built on the CPU\n",
		vk.statDraws[1], vk.statStaticIndexes[1] / 3, vk.statCpuIndexes[1] / 3, vk.statCpuVertexes[1] );
	ri.Printf( PRINT_ALL, "  %i of %i transforms, %i of %i bones\n", vk.statTransforms[1], VK_MAX_TRANSFORMS, vk.statBones[1], VK_MAX_BONES );
	// the shaders with the most triangles from the CPU
	for ( int n = 0; n < 8; n++ ) {
		vkStatShader_t *most = NULL;

		for ( int i = 0; i < vk.statNumCpuShaders[1]; i++ ) {
			if ( vk.statCpuShaders[1][i].indexes > ( most ? most->indexes : 0 ) ) {
				most = &vk.statCpuShaders[1][i];
			}
		}
		if ( !most ) {
			break;
		}
		ri.Printf( PRINT_ALL, "  on the CPU: %5i triangles %s\n", most->indexes / 3, most->name );
		most->indexes = 0;
	}
	VK_MemoryInfo_f();
	ri.Printf( PRINT_ALL, "validation layer: %s", vk.validation ? "on" : "off" );
	if ( vk.validation ) {
		ri.Printf( PRINT_ALL, ", %i messages so far", vk.validationMessages );
	}
	ri.Printf( PRINT_ALL, "\noverbright bits: %i, gamma: %.2f\n", tr.overbrightBits, r_gamma->value );
}
