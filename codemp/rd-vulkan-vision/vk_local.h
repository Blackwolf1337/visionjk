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

#pragma once

// rd-vulkan-vision: everything that touches the GPU. The front end (loaders, shader parser, Ghoul2, scene) is
// built from rd-vanilla, see vk_hooks.h.

#include "tr_local.h"

#include <vulkan/vulkan.h>

#define VK_MAX_IMAGES		4096		// size of the texture table the shaders index, image_t::texnum is the index
#define VK_FRAMES			2			// frames in flight
#define VK_MAX_SWAP_IMAGES	8
#define VK_MAX_DRAWS		32768		// draws of one frame, each is one shader stage of one batch
#define VK_MAX_TRANSFORMS	4096		// views times entities of one frame
#define VK_VERTEX_BYTES		( 32 << 20 )	// geometry of one frame that is built on the CPU
#define VK_INDEX_BYTES		( 8 << 20 )
#define VK_STAGING_BYTES	( 32 << 20 )
#define VK_MAX_SAMPLERS		32
#define VK_MAX_PIPELINES	256
#define VK_MAX_BONES		16384		// bones of all skeletons of one frame
#define VK_MODEL_BYTES		( 32 << 20 )	// vertices of the Ghoul2 models of a level

// ------------------------------------------------------------------------------------------------------------
// what the shaders read, shaders/common.glsl has the same layout (std430)
// ------------------------------------------------------------------------------------------------------------

typedef struct vkGlobals_s {	// once per frame
	float		gamma[4];			// x: 1 / r_gamma, y: 2 ^ overbright bits, z: identity light
	float		screen[4];			// x, y: size of the frame in pixels
} vkGlobals_t;

typedef struct vkTransform_s {	// once per view and entity
	float		mvp[16];			// OpenGL clip space, the vertex shader turns it into the one of Vulkan
	float		model[16];			// model to world
	float		viewOrigin[4];		// in model space
	float		lightDir[4];		// in model space, entity lighting
	float		ambientLight[4];
	float		directedLight[4];
} vkTransform_t;

#define VKD_ATEST_MASK		0x00000007u		// 0 none, 1 GT_0, 2 LT_80, 3 GE_80, 4 GE_C0
#define VKD_TEXENV_SHIFT	3				// second texture: 0 none, 1 modulate, 2 add, 3 decal, 4 replace
#define VKD_TEXENV_MASK		0x00000038u
#define VKD_TCGEN0_SHIFT	6				// texCoordGen_t of the first texture
#define VKD_TCGEN1_SHIFT	10				// and of the second
#define VKD_TCGEN_MASK		0xfu
#define VKD_RGBGEN_SHIFT	14				// 0: base + vertex, 1: diffuse light, 2: diffuse light times entity
#define VKD_RGBGEN_MASK		0x3u
#define VKD_AGEN_SHIFT		16				// 0: base + vertex, 1: specular, 2: portal, 3: dot, 4: one minus dot
#define VKD_AGEN_MASK		0x7u
#define VKD_DLIGHT			0x00080000u		// the pass that adds one dynamic light, see generic.frag
#define VKD_FOG_SHIFT		20				// fog over the whole stage as glFog did it: 0 none, 1 exp2, 2 linear

typedef struct vkDraw_s {		// once per draw
	uint32_t	transform;			// index into the transforms of the frame
	uint32_t	tex0, tex1;			// indices into the texture table
	uint32_t	flags;				// VKD_*
	float		baseColor[4];		// color = baseColor + vertColor * vertex color
	float		vertColor[4];
	float		texMat0[8];			// two rows: s' = dot( row0.xy, st ) + row0.z, row0.w: phase of the turbulence
	float		texTurb0[4];		// s' += x * sinS + y * sinT, t' += z * sinS + w * sinT
	float		texMat1[8];
	float		texTurb1[4];
	float		tcGenVec0[8];		// TCGEN_VECTOR
	float		tcGenVec1[8];
	float		parm[4];			// x: portal range, y: time of the shader. Dynamic light: its origin in model space, w: radius
	float		fogColor[4];
	float		fogParm[4];			// exp2: x: density. linear: x: start, y: end
	uint32_t	bones;				// skinned: where the skeleton starts in the bones of the frame, in rows
	uint32_t	unused[3];
} vkDraw_t;

typedef struct vkSkinVertex_s {	// a vertex of a Ghoul2 surface as it lies on the device, shaders/skinned.vert
	float		xyz[3];
	float		normal[3];
	float		st[2];
	byte		bones[4];			// numbers of the bones in the skeleton
	uint16_t	weights[4];			// 65535 is all of it
	uint32_t	unused;
} vkSkinVertex_t;

// ------------------------------------------------------------------------------------------------------------

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

typedef struct vkImage_s {		// one entry of the texture table
	image_t			*owner;			// NULL: free
	VkImage			image;
	VkImageView		view;
	vkAlloc_t		alloc;
	int				width, height;
	int				mipLevels;
	int				minFilter, magFilter, wrap;		// OpenGL names, as tr_image.cpp hands them over
	float			anisotropy;
	qboolean		uploaded;		// all levels are there and the layout is the one for sampling
} vkImage_t;

typedef struct vkTarget_s {		// something to render into
	VkImage			image;
	VkImageView		view;
	vkAlloc_t		alloc;
	VkFormat		format;
	VkImageLayout	layout;
	int				width, height;
} vkTarget_t;

typedef struct vkFrame_s {
	VkCommandBuffer	cmd;
	VkFence			fence;
	VkSemaphore		acquired;
	VkSemaphore		rendered;
	VkDescriptorSet	set;			// globals, transforms and draws of this frame

	vkBuffer_t		vertexes;
	vkBuffer_t		indexes;
	vkBuffer_t		globals;
	vkBuffer_t		transforms;
	vkBuffer_t		draws;
	vkBuffer_t		bones;

	VkDeviceSize	vertexUsed;
	VkDeviceSize	indexUsed;
	int				numTransforms;
	int				numDraws;
	int				numBones;
} vkFrame_t;

typedef enum {
	VKP_GENERIC,					// vertices in the layout of the tess arrays, one shader stage
	VKP_SKINNED,					// vertices of a Ghoul2 surface, moved by their bones in the vertex shader
	VKP_NUM_PROGRAMS
} vkProgram_t;

typedef struct vkPipeline_s {
	uint32_t		key;
	VkPipeline		pipeline;
} vkPipeline_t;

typedef struct vkState_s {
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
	int							swapInterval;		// what the swapchain was made for

	int							width, height;		// size of the frame, glConfig.vidWidth and vidHeight
	vkTarget_t					color;				// the frame as the game draws it
	vkTarget_t					depth;
	vkTarget_t					final;				// after gamma, what goes to the window and into screenshots
	vkTarget_t					glow;				// r_DynamicGlow: the stages that glow, drawn again over the depth of the scene
	vkTarget_t					blur[2];			// and blurred, r_DynamicGlowWidth x r_DynamicGlowHeight
	qboolean					finalValid;			// a frame has been rendered into it

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
	VkSampler					linearSampler;

	VkShaderModule				vertexShaders[VKP_NUM_PROGRAMS];
	VkShaderModule				fragmentShaders[VKP_NUM_PROGRAMS];
	VkShaderModule				postVertexShader;
	VkShaderModule				gammaFragmentShader;
	VkPipeline					gammaPipeline;
	VkShaderModule				blurFragmentShader;
	VkShaderModule				copyFragmentShader;
	VkPipeline					blurPipeline;
	VkPipeline					glowPipelines[2];	// the blurred glow onto the frame: added, or added softly
	vkPipeline_t				pipelines[VK_MAX_PIPELINES];
	int							numPipelines;

	vkImage_t					images[VK_MAX_IMAGES];
	image_t						*boundImage;		// GL_Bind(), the image tr_image.cpp is uploading
	VkSampler					samplers[VK_MAX_SAMPLERS];
	int							samplerKeys[VK_MAX_SAMPLERS][4];
	int							numSamplers;

	// uploads wait in one command buffer until something needs them on the GPU
	VkCommandBuffer				uploadCmd;
	qboolean					uploadOpen;
	vkBuffer_t					staging;
	VkDeviceSize				stagingUsed;

	vkFrame_t					frames[VK_FRAMES];
	int							frameIndex;
	vkFrame_t					*frame;				// the frame being recorded, NULL between frames
	qboolean					rendering;			// inside vkCmdBeginRendering of the frame
	VkPipeline					boundPipeline;
	uint32_t					boundState;			// the dynamic state that is set, see VK_DrawTess()
	qboolean					stateValid;
	VkDeviceSize				tessColorOffset;	// where VK_UploadTess() put the vertex colors of the batch
	VkDeviceSize				tessTexCoordOffset;	// and the texture coordinates
	int							numBoundIndexes;	// of the geometry that was uploaded last
	int							viewport[4];		// x, y, width, height as OpenGL counts them
	qboolean					overflowed;			// this frame has more geometry than fits, said once

	// what the frame was made of, for gfxinfo: [0] the frame being recorded, [1] the last finished one
	int							statDraws[2];
	int							statCpuVertexes[2];	// vertices that were built on the CPU and uploaded
	int							statCpuIndexes[2];
	int							statStaticIndexes[2];	// indexes into vertices that are on the device

	qboolean					validation;
	int							validationMessages;
} vkState_t;

extern vkState_t vk;

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
void		VK_CreateBuffer( vkBuffer_t *buf, VkDeviceSize size, VkBufferUsageFlags usage, qboolean hostVisible );
void		VK_DestroyBuffer( vkBuffer_t *buf );
void		VK_MemoryInfo_f( void );

// vk_image.cpp
void		VK_InitImages( void );
void		VK_ShutdownImages( void );
void		VK_FlushUploads( void );
VkCommandBuffer VK_UploadCmd( void );
byte		*VK_Stage( VkDeviceSize size, VkDeviceSize *offset );
void		VK_UpdateImage( image_t *image, int width, int height, const byte *rgba );
void		VK_CreateTarget( vkTarget_t *target, int width, int height, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect );
void		VK_DestroyTarget( vkTarget_t *target );
void		VK_SetLayout( VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to );
void		VK_TargetLayout( VkCommandBuffer cmd, vkTarget_t *target, VkImageLayout to );

// vk_pipeline.cpp
void		VK_InitPipelines( void );
void		VK_ShutdownPipelines( void );
VkPipeline	VK_FindPipeline( vkProgram_t program, uint32_t stateBits );

// vk_gl.cpp
const float *VK_GL_ModelView( void );
void		VK_GL_ResetMatrix( void );

// vk_world.cpp
extern cvar_t	*r_vkStatic;
void		VK_WorldChanged( void );
void		VK_ShutdownWorld( void );
void		VK_BatchBegin( void );
qboolean	VK_BatchIsStatic( void );
qboolean	VK_BatchIsSkinned( void );
void		VK_AddSurface( surfaceType_t *surface );
qboolean	VK_UploadStaticBatch( uint32_t *bones );
void		VK_StaticColorScale( float *scale );

// vk_model.cpp
void		VK_ModelsChanged( void );
void		VK_InitModels( void );
void		VK_ShutdownModels( void );
void		VK_ModelsBeginFrame( void );
int			VK_SkinnedSurface( const mdxmSurface_t *surface );
int			VK_StaticMeshSurface( const md3Surface_t *surface );
qboolean	VK_UploadSkinnedBatch( CBoneCache *boneCache, uint32_t *bones );

// vk_shade.cpp
extern bool	g_bRenderGlowingObjects;	// the pass that draws only the stages that glow
extern bool	g_bDynamicGlowSupported;
void		VK_InvalidateTransform( void );
void		VK_TransformChanged( void );
uint32_t	VK_CurrentTransform( void );
void		VK_SetProjection( const float *projection );

// vk_post.cpp
void		VK_InitPost( void );
void		VK_ShutdownPost( void );
void		VK_BeginGlow( void );
void		VK_EndGlow( void );

// vk_frame.cpp
void		VK_BeginTarget( VkCommandBuffer cmd, vkTarget_t *color, vkTarget_t *depth, int colorLoad, qboolean depthLoad );
void		VK_BindFrameSets( void );
void		VK_InitFrames( void );
void		VK_ShutdownFrames( void );
void		VK_BeginFrame( void );
void		VK_EndFrame( void );
void		VK_SetViewport( int x, int y, int width, int height, float minDepth, float maxDepth );
void		VK_ClearDepth( int x, int y, int width, int height );
void		VK_ClearColor( int x, int y, int width, int height, const float *color );
int			VK_AddTransform( const vkTransform_t *transform );
void		VK_SetDepthRange( float minDepth, float maxDepth );
qboolean	VK_UploadGeometry( const vec4_t *xyz, const vec4_t *normal, const vec2_t ( *texCoords )[NUM_TEX_COORDS],
				const color4ub_t *colors, int numVerts, const glIndex_t *indexes, int numIndexes );
qboolean	VK_UploadIndexes( VkBuffer vertexBuffer, const VkDeviceSize *offsets, int numBindings, const glIndex_t *indexes, int numIndexes );
qboolean	VK_UploadTess( void );
void		VK_TessColors( const color4ub_t *colors );
void		VK_TessTexCoords( const vec2_t *st );
void		VK_DrawTess( const vkDraw_t *draw, vkProgram_t program, uint32_t stateBits, int cullType, qboolean polygonOffset );
