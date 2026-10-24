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

#pragma once

// vk_device.h: the Vulkan device of rd-vulkan-vision. Included by vk_local.h and by nothing else, see the top of
// that file for what a device header is and what the shared files take from it. The rest of this file is for the
// files that talk to Vulkan: vk_main.cpp, vk_memory.cpp, vk_pipeline.cpp, vk_cmd.cpp, vk_texture.cpp.

// no prototypes: the functions are pointers that are filled at run time, see vk_funcs.h
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "vk_funcs.h"

// ------------------------------------------------------------------------------------------------------------
// what the shared files take from a device
// ------------------------------------------------------------------------------------------------------------

#define VK_FRAMES			2			// frames in flight
// the draws and the transforms of a frame are arrays the shaders index, an entry is as large as its structure
#define VK_DRAW_STRIDE		( sizeof( vkDraw_t ) )
#define VK_TRANSFORM_STRIDE	( sizeof( vkTransform_t ) )
#define VK_BONE_ALIGN		1			// a skeleton may start at any bone of the frame: the shader adds where it starts
#define VK_DEVICE_BASE_VERTEX	1		// a draw call names the vertex its indexes count from

typedef VkDeviceSize		vkSize_t;
typedef VkFormat			vkFormat_t;
typedef VkBufferUsageFlags	vkBufferUsage_t;

// VkCullModeFlags
#define VKC_NONE			VK_CULL_MODE_NONE
#define VKC_FRONT			VK_CULL_MODE_FRONT_BIT
#define VKC_BACK			VK_CULL_MODE_BACK_BIT

#define VKF_RGBA8			VK_FORMAT_R8G8B8A8_UNORM
#define VKF_BC1				VK_FORMAT_BC1_RGB_UNORM_BLOCK
#define VKF_BC3				VK_FORMAT_BC3_UNORM_BLOCK

// vertices and indexes that stay on the device are copied there from a staging buffer (VK_CopyBuffer)
#define VKB_VERTEXES		( VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT )
#define VKB_INDEXES			( VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT )
#define VKB_STAGING			VK_BUFFER_USAGE_TRANSFER_SRC_BIT

typedef struct vkAlloc_s {
	int				block;			// -1: nothing allocated
	VkDeviceSize	offset;
	VkDeviceSize	size;
} vkAlloc_t;

typedef struct vkBuffer_s {
	VkBuffer		buffer;
	vkAlloc_t		alloc;
	VkDeviceSize	size;
	byte			*mapped;		// host visible buffers stay mapped
} vkBuffer_t;

typedef struct vkTarget_s {		// something to render into
	VkImage			image;
	VkImageView		view;
	vkAlloc_t		alloc;
	VkFormat		format;
	VkImageLayout	layout;
	int				width, height;
} vkTarget_t;

typedef struct vkDeviceImage_s {	// what an entry of the texture table is on the device, see vkImage_t
	VkImage			image;
	VkImageView		view;
	vkAlloc_t		alloc;
} vkDeviceImage_t;

typedef struct vkDeviceFrame_s {	// see vkFrame_t
	VkCommandBuffer	cmd;
	VkFence			fence;
	VkSemaphore		acquired;
	VkDescriptorSet	set;			// globals, transforms and draws of this frame
	vkBuffer_t		probes;			// 4 bytes of depth per probe, written by the device
} vkDeviceFrame_t;

// ------------------------------------------------------------------------------------------------------------
// what only the files of this device see
// ------------------------------------------------------------------------------------------------------------

#define VK_MAX_SWAP_IMAGES	8
#define VK_STAGING_BYTES	( 32 << 20 )
#define VK_UPLOAD_SLOTS		2			// upload batches: one is filled while the one before may still be copied
#define VK_MAX_SAMPLERS		32
#define VK_MAX_PIPELINES	256

typedef struct vkPipeline_s {
	uint32_t		key;
	VkPipeline		pipeline;
} vkPipeline_t;

typedef struct vkDeviceState_s {	// see vkState_t
	VkInstance					instance;
	VkDebugUtilsMessengerEXT	messenger;
	VkPhysicalDevice			physicalDevice;
	VkPhysicalDeviceProperties	properties;
	VkPhysicalDeviceMemoryProperties	memoryProperties;
	VkDevice					device;
	uint32_t					queueFamily;
	VkQueue						queue;
	VkCommandPool				commandPool;

	VkSurfaceKHR				surface;
	VkSwapchainKHR				swapchain;
	VkFormat					swapFormat;
	VkExtent2D					swapExtent;
	uint32_t					numSwapImages;
	VkImage						swapImages[VK_MAX_SWAP_IMAGES];
	// signaled when the image of that number can be shown. One for an image and not one for a frame: the swapchain
	// may hold on to it until that image is acquired again.
	VkSemaphore					swapReady[VK_MAX_SWAP_IMAGES];
	int							swapInterval;		// what the swapchain was made for
	int							swapStallTime;		// ri.Milliseconds() when a frame waited for an image in vain, 0: the last wait got one

	VkSampleCountFlagBits		samples;			// r_ext_multisample. More than one: the scene is drawn into the three targets below
	vkTarget_t					colorMS, depthMS, glowMS;	// and each pass ends by resolving them into color, depth and glow

	VkDescriptorPool			descriptorPool;
	VkDescriptorSetLayout		frameSetLayout;
	VkDescriptorSetLayout		textureSetLayout;
	VkDescriptorSetLayout		postSetLayout;
	VkPipelineLayout			pipelineLayout;
	VkPipelineLayout			postPipelineLayout;
	VkDescriptorSet				textureSet;
	VkDescriptorSet				postSet;
	VkSampler					postSampler;
	VkDescriptorSet				glowSet;
	VkDescriptorSet				blurSets[2];
	VkDescriptorSet				softSets[2];
	VkSampler					linearSampler;
	// the depth of the scene as a texture, for the depth of field (VK_FocusPass)
	qboolean					depthReadable;		// the device lets its format be read in a shader
	VkImageView					depthView;			// of the depth alone, without the stencil
	VkDescriptorSet				depthSet;
	// tay_renderScale: the targets the views of the level are drawn into at their own size (VK_CreateSceneTargets),
	// traded with color, colorMS, depth, depthMS, glow, glowMS and their sets while those views are drawn
	vkTarget_t					sceneColor, sceneColorMS, sceneDepth, sceneDepthMS, sceneGlow, sceneGlowMS;
	VkImageView					sceneDepthView;
	VkDescriptorSet				scenePostSet, sceneGlowSet, sceneDepthSet;
	VkDescriptorSet				sceneCopySet;		// the color of those views, filtered, for VK_DrawScene()
	VkPipeline					scenePipeline;		// it over a rectangle of the frame as it is
	VkPipelineLayout			focusPipelineLayout;	// two sets of the kind of postSet: a target and the depth
	VkPipelineLayout			scopePipelineLayout;	// four sets of that kind and sixteen numbers (VK_ScopePass)
	VkDescriptorSet				screenSet;			// vk.screen for it

	VkShaderModule				vertexShaders[VKP_NUM_PROGRAMS];
	VkShaderModule				fragmentShaders[VKP_NUM_PROGRAMS];
	VkShaderModule				postVertexShader;
	VkShaderModule				gammaFragmentShader;
	VkPipeline					gammaPipeline;
	VkShaderModule				blurFragmentShader;
	VkShaderModule				copyFragmentShader;
	VkPipeline					blurPipeline;
	VkPipeline					glowPipelines[3];	// a target onto the frame, vkAdd_t: added, added softly, by its alpha
	VkShaderModule				focusFragmentShader;
	VkPipeline					focusPipeline;
	VkShaderModule				scopePrepFragmentShader, scopeFragmentShader;
	VkPipeline					scopePrepPipeline, scopePipeline;
	vkPipeline_t				pipelines[VK_MAX_PIPELINES];
	int							numPipelines;
	uint32_t					lastPipelineKey;	// what VK_FindPipeline() was asked last and what it answered
	VkPipeline					lastPipeline;

	VkSampler					samplers[VK_MAX_SAMPLERS];
	int							samplerKeys[VK_MAX_SAMPLERS][4];
	int							numSamplers;

	// Uploads wait in the command buffer of a slot until something needs them on the GPU, and are submitted
	// without waiting for them. The staging buffer of the slot that is filled is vk.staging, the others are
	// kept in the slots; a slot is filled again when the fence of its last batch says the device read it.
	struct {
		VkCommandBuffer			cmd;
		VkFence					fence;
		vkBuffer_t				staging;	// of the slots that are not filled now
		qboolean				pending;	// submitted, its fence not looked at since
	}							uploads[VK_UPLOAD_SLOTS];
	int							uploadSlot;		// the slot that is filled
	VkCommandBuffer				uploadCmd;		//	its command buffer,
	qboolean					uploadOpen;		//	which is being recorded
	vkBuffer_t					staging;
	VkDeviceSize				stagingUsed;

	VkPipeline					boundPipeline;
	VkBuffer					boundVertexBuffer;	// what the draws read: the streams of the frame, the level or the models
	VkBuffer					boundIndexBuffer;

	int							statSwapchains;		// how many swapchains this window has had
	int							statFramesNotShown;	// frames that found no image of the swapchain to go to

	qboolean					validation;
	int							validationMessages;
} vkDeviceState_t;

#define VK_CHECK( call ) \
	do { VkResult vkCheckResult = ( call ); \
		if ( vkCheckResult != VK_SUCCESS ) VK_Fatal( #call, vkCheckResult ); } while ( 0 )

// vk_main.cpp
void		VK_Fatal( const char *what, VkResult result );
qboolean	VK_SwapIntervalChanged( void );
void		VK_CreateSwapchain( void );
void		VK_DestroySwapchain( void );

// vk_memory.cpp
void		VK_InitMemory( void );
void		VK_ShutdownMemory( void );
void		VK_AllocMemory( const VkMemoryRequirements *req, VkMemoryPropertyFlags flags, vkAlloc_t *alloc );
void		VK_FreeMemory( vkAlloc_t *alloc );
VkDeviceMemory VK_MemoryOf( const vkAlloc_t *alloc );
void		VK_MemoryInfo_f( void );

// vk_texture.cpp
VkCommandBuffer VK_UploadCmd( void );
void		VK_CreateTarget( vkTarget_t *target, int width, int height, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
				VkSampleCountFlagBits samples );
void		VK_DestroyTarget( vkTarget_t *target );
void		VK_SetLayout( VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to );
void		VK_TargetLayout( VkCommandBuffer cmd, vkTarget_t *target, VkImageLayout to );
void		VK_DestroyPostTargets( void );
// a combined image sampler set of one image
void		VK_WriteSet( VkDescriptorSet set, VkSampler sampler, VkImageView view, VkImageLayout layout );

// vk_pipeline.cpp
void		VK_InitPipelines( void );
void		VK_ShutdownPipelines( void );
VkPipeline	VK_FindPipeline( vkProgram_t program, uint32_t stateBits );

// vk_cmd.cpp
void		VK_InitFrames( void );
void		VK_ShutdownFrames( void );
