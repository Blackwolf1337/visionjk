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

// no prototypes: the functions are pointers that are filled at run time, see vk_funcs.h
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "vk_funcs.h"

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

// a state bit next to the GLS_* ones of rd-vanilla, which have no such thing: the indexes are pairs, each a line
#define VK_GLS_LINES		0x00002000
#define VK_MAX_BONES		16384		// bones of all skeletons of one frame
#define VK_MODEL_BYTES		( 32 << 20 )	// vertices of the Ghoul2 models of a level
#define VK_MODEL_INDEX_BYTES	( 16 << 20 )	// and their triangles
#define VK_MAX_BATCH_RANGES	64				// runs of indexes a batch of model surfaces can have

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
	float		lightDir[4];		// in model space, entity lighting. w: 1 for a model, else 0
	float		ambientLight[4];
	float		directedLight[4];
	float		clipPlane[4];		// in world space, for the view through a portal or mirror: xyz normal, w distance. Zero: none
} vkTransform_t;

#define VKD_ATEST_MASK		0x00000007u		// 0 none, 1 GT_0, 2 LT_80, 3 GE_80, 4 GE_C0
#define VKD_TEXENV_SHIFT	3				// second texture: 0 none, 1 modulate, 2 add, 3 decal, 4 replace
#define VKD_TEXENV_MASK		0x00000038u
#define VKD_TCGEN0_SHIFT	6				// texCoordGen_t of the first texture
#define VKD_TCGEN1_SHIFT	10				// and of the second
#define VKD_TCGEN_MASK		0xfu
#define VKD_RGBGEN_SHIFT	14				// 0: base + vertex, 1: diffuse light, 2: diffuse light times entity
#define VKD_RGBGEN_MASK		0x3u
#define VKD_AGEN_SHIFT		16				// 0: base + vertex, 1: specular, 2: portal
#define VKD_AGEN_MASK		0x7u
#define VKD_DLIGHT			0x00080000u		// the pass that adds one dynamic light, see generic.frag
#define VKD_FOG_SHIFT		20				// fog over the whole stage as glFog did it: 0 none, 1 exp2, 2 linear
#define VKD_ACFF_SHIFT		22				// acff_t: the color fades with the way through a fog volume
#define VKD_DEFORM_SHIFT	24				// deformVertexes: 0 none, 1 along the normal, 2 all vertices by one vector

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
	float		fogParm[4];			// exp2: x: density. linear: x: start, y: end. z: fog volume, where the eye is (eyeT)
	float		fogVec[8];			// fog volume: the distance and the depth vector of RB_CalcFogTexCoords()
	float		deformWave[4];		// along the normal by x + y * wave( z + dot( deformVec.xyz, position ) + deformVec.w * s ), w: genFunc_t
	float		deformVec[4];		// for a move, xyz: by how much
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
	VkFormat		format;			// 32 bit RGBA, or blocks: BC1 without alpha, BC3 with
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

#define VK_MAX_FLARES		256

typedef struct vkStencil_s {	// what the draws of stencil shadows need beyond the state bits, in the words of OpenGL
	qboolean	test;				// GL_STENCIL_TEST
	int			func, ref;			// glStencilFunc
	int			ops[2][3];			// front and back: when the stencil test fails, when the depth test fails, when both pass
	qboolean	depthLess;			// glDepthFunc( GL_LESS )
} vkStencil_t;

typedef struct vkIndexRange_s {	// a run of indexes in a buffer on the device
	uint32_t	first;
	uint32_t	count;
} vkIndexRange_t;

typedef struct vkProbe_s {		// one pixel of the depth of a frame that is read back for a flare
	int			flare;				// which one
	int			x, y;				// in the depth target
	float		eyeZ;				// how far in front of the eye the flare was
	float		projection[3];		// [10], [11] and [14] of the projection matrix of that view
} vkProbe_t;

typedef struct vkFrame_s {
	VkCommandBuffer	cmd;
	VkFence			fence;
	VkSemaphore		acquired;
	VkDescriptorSet	set;			// globals, transforms and draws of this frame

	vkBuffer_t		vertexes;
	vkBuffer_t		indexes;
	vkBuffer_t		globals;
	vkBuffer_t		transforms;
	vkBuffer_t		draws;
	vkBuffer_t		bones;
	vkBuffer_t		probes;			// 4 bytes of depth per probe, written by the device

	int				numVertexes;		// in the four streams of vertices built on the CPU
	VkDeviceSize	indexUsed;
	int				numTransforms;
	int				numDraws;
	int				numBones;
	vkProbe_t		probe[VK_MAX_FLARES];
	int				numProbes;
	int				numProbesCopied;	// that many have their copy in the commands of the frame
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

#define VK_MAX_STAT_SHADERS	64

typedef struct vkStatShader_s {
	const shader_t	*shader;			// only compared, it may be gone when the name is printed
	char			name[MAX_QPATH];
	int				indexes;
} vkStatShader_t;

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
	// signaled when the image of that number can be shown. One for an image and not one for a frame: the swapchain
	// may hold on to it until that image is acquired again.
	VkSemaphore					swapReady[VK_MAX_SWAP_IMAGES];
	int							swapInterval;		// what the swapchain was made for
	int							swapStallTime;		// ri.Milliseconds() when a frame waited for an image in vain, 0: the last wait got one

	int							width, height;		// size of the frame, glConfig.vidWidth and vidHeight
	vkTarget_t					color;				// the frame as the game draws it
	vkTarget_t					depth;
	VkSampleCountFlagBits		samples;			// r_ext_multisample. More than one: the scene is drawn into the three targets below
	vkTarget_t					colorMS, depthMS, glowMS;	// and each pass ends by resolving them into color, depth and glow
	vkTarget_t					final;				// after gamma, what goes to the window and into screenshots
	vkTarget_t					screen;				// a copy of the frame as it was a moment ago, for surfaces that bend what is behind them
	int							screenSlot;			// the entry of the texture table that shows it, -1: none yet
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
	uint32_t					lastPipelineKey;	// what VK_FindPipeline() was asked last and what it answered
	VkPipeline					lastPipeline;

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
	VkBuffer					boundVertexBuffer;	// what the draws read: the streams of the frame, the level or the models
	VkBuffer					boundIndexBuffer;
	int							numBoundIndexes;	// the batch that was set up last: that many indexes
	uint32_t					firstIndex;			// from here in the index buffer
	int32_t						vertexOffset;		// counted from this vertex
	const vkStencil_t			*stencil;			// of the draws that follow, NULL: no stencil test, depth as the state bits say
	const vkIndexRange_t		*ranges;			// or, for surfaces of models, the runs of indexes to draw
	int							numRanges;			// 0: numBoundIndexes from firstIndex
	int							viewport[4];		// x, y, width, height as OpenGL counts them
	qboolean					overflowed;			// this frame has more geometry than fits, said once

	// what the frame was made of, for gfxinfo: [0] the frame being recorded, [1] the last finished one
	int							statSwapchains;		// how many swapchains this window has had
	int							statFramesNotShown;	// frames that found no image of the swapchain to go to
	int							statDraws[2];
	int							statTransforms[2];
	int							statBones[2];
	int							statCpuVertexes[2];	// vertices that were built on the CPU and uploaded
	int							statCpuIndexes[2];
	int							statStaticIndexes[2];	// indexes into vertices that are on the device
	vkStatShader_t				statCpuShaders[2][VK_MAX_STAT_SHADERS];	// whose batches were built on the CPU
	int							statNumCpuShaders[2];

	qboolean					wireframe;			// the device can draw triangles as lines (r_showtris)
	qboolean					anisotropy;			// and filter textures along the direction they are seen in
	qboolean					bcTextures;			// r_ext_compress_textures on a device that takes BC1 and BC3 blocks
	double						statBcPixels;		// what packing textures into blocks has cost since the window was made
	double						statBcSeconds;
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
void		VK_CreateTarget( vkTarget_t *target, int width, int height, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
				VkSampleCountFlagBits samples );
void		VK_DestroyTarget( vkTarget_t *target );
void		VK_SetLayout( VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to );
void		VK_TargetLayout( VkCommandBuffer cmd, vkTarget_t *target, VkImageLayout to );

// vk_bc.cpp
void		VK_BC_Init( void );
void		VK_BC_Shutdown( void );
VkDeviceSize VK_BC_Size( VkFormat format, int width, int height );
void		VK_BC_Compress( VkFormat format, const byte *rgba, int width, int height, byte *out );

// vk_pipeline.cpp
void		VK_InitPipelines( void );
void		VK_ShutdownPipelines( void );
VkPipeline	VK_FindPipeline( vkProgram_t program, uint32_t stateBits );

// vk_gl.cpp
const float *VK_GL_ModelView( void );
void		VK_GL_ResetMatrix( void );

// vk_world.cpp
extern cvar_t	*r_vkStatic;
extern cvar_t	*r_vkMerge;
void		VK_WorldChanged( void );
void		VK_ShutdownWorld( void );
void		VK_BatchBegin( void );
qboolean	VK_BatchIsStatic( void );
qboolean	VK_DeformOnDevice( const shader_t *shader );
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
int			VK_GoreSurface( const mdxmSurface_t *surface, const float *goreData );
qboolean	VK_UploadGoreBatch( CBoneCache *boneCache, uint32_t *bones );
void		VK_ModelBatchBegin( void );
qboolean	VK_ModelBatchAdd( int firstIndex, int numIndexes );
qboolean	VK_UploadSkinnedBatch( CBoneCache *boneCache, uint32_t *bones );

// vk_shade.cpp
extern bool	g_bRenderGlowingObjects;	// the pass that draws only the stages that glow
extern cvar_t	*r_aviMotionJpegQuality;	// tr_init.cpp
extern bool	g_bDynamicGlowSupported;
extern bool	tr_stencilled;				// a surface of this view has marked its shape in the stencil buffer, see RB_DistortionFill()
void		VK_InvalidateTransform( void );
void		VK_TransformChanged( void );
uint32_t	VK_CurrentTransform( void );
void		VK_SetRefraction( qboolean on, int x, int y, int size );
void		VK_SetProjection( const float *projection );

// vk_backend.cpp
void		VK_Splash( void );

// vk_post.cpp
void		VK_InitPost( void );
void		VK_ShutdownPost( void );
void		VK_BeginGlow( void );
void		VK_EndGlow( void );
void		VK_CaptureFrame( void );
void		VK_WriteVideoFrame( void );
void		VK_ClearFlares( void );
void		VK_CopyProbes( void );
void		VK_ReadProbes( vkFrame_t *frame );

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
void		VK_BindVertexBuffer( VkBuffer buffer, const VkDeviceSize *offsets, int numBindings );
void		VK_BindIndexBuffer( VkBuffer buffer );
qboolean	VK_UploadGeometry( const vec4_t *xyz, const vec4_t *normal, const vec2_t ( *texCoords )[NUM_TEX_COORDS],
				const color4ub_t *colors, int numVerts, const glIndex_t *indexes, int numIndexes );
qboolean	VK_UploadIndexes( VkBuffer vertexBuffer, const VkDeviceSize *offsets, int numBindings, const glIndex_t *indexes, int numIndexes );
qboolean	VK_UploadTess( void );
void		VK_TessColors( const color4ub_t *colors );
void		VK_DrawTess( const vkDraw_t *draw, vkProgram_t program, uint32_t stateBits, int cullType, qboolean polygonOffset );
