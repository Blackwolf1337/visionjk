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

// rd-vulkan-vision: everything that touches the GPU. The front end (loaders, shader parser, Ghoul2, scene) is
// built from rd-vanilla, see vk_hooks.h.
//
// The renderer is a shared part and a device. The shared part decides what is drawn and in which order, and names
// no type and no function of a graphics library: vk_backend.cpp, vk_shade.cpp, vk_gl.cpp, vk_world.cpp,
// vk_model.cpp, vk_frame.cpp, vk_image.cpp, vk_post.cpp, vk_bc.cpp, vk_stubs.cpp and the shaders. A device is the
// files that talk to such a library. For Vulkan: vk_main.cpp, vk_memory.cpp, vk_pipeline.cpp, vk_cmd.cpp,
// vk_texture.cpp, with vk_device.h and vk_funcs.h. For OpenGL ES 3, which is what a browser has as WebGL2: the
// files in gles/. That device takes no textures in blocks yet, so vk_bc.cpp is built with the Vulkan device only.
//
// This header includes exactly one device header: vk_device.h, or gles/gles_device.h when RD_VISION_GLES is
// defined. It comes after the constants and the structures the shaders read and before the state, and gives:
// - constants: VK_FRAMES, VK_DRAW_STRIDE, VK_TRANSFORM_STRIDE, VK_BONE_ALIGN, VK_DEVICE_BASE_VERTEX, the cull modes
//   VKC_*, the texture formats VKF_* and the buffer usages VKB_*
// - types the shared files hand around: vkSize_t, vkFormat_t, vkBufferUsage_t, vkBuffer_t, vkTarget_t
// - vkDeviceImage_t, vkDeviceFrame_t and vkDeviceState_t, which vkImage_t, vkFrame_t and vkState_t below are
//   derived from. So what a device keeps per texture, per frame and as its state are members of vk.images[],
//   vk.frame and vk like the shared ones, and no file has to know which part a member is in.
// What the shared files read of these: vkBuffer_t::buffer (zero: there is none), size and mapped,
// vkTarget_t::width and height, vkDeviceImage_t::image (zero: the slot has nothing on the device),
// vkDeviceState_t::device (zero: the device can not draw yet) and staging (what VK_Stage() hands out pieces of).
// The functions a device has for the shared files are the last block of this header.

#include "tr_local.h"

#define VK_MAX_IMAGES		4096		// size of the texture table the shaders index, image_t::texnum is the index
#define VK_MAX_DRAWS		32768		// draws of one frame, each is one shader stage of one batch
#define VK_MAX_TRANSFORMS	4096		// views times entities of one frame
#define VK_VERTEX_BYTES		( 32 << 20 )	// geometry of one frame that is built on the CPU
#define VK_INDEX_BYTES		( 8 << 20 )

// a state bit next to the GLS_* ones of rd-vanilla, which have no such thing: the indexes are pairs, each a line
#define VK_GLS_LINES		0x00002000
#define VK_MAX_BONES		16384		// bones of all skeletons of one frame
#define VK_MODEL_BYTES		( 32 << 20 )	// vertices of the Ghoul2 models of a level
#define VK_MODEL_INDEX_BYTES	( 16 << 20 )	// and their triangles
#define VK_MAX_BATCH_RANGES	64				// runs of indexes a batch of model surfaces can have
#define VK_MAX_FLARES		256				// flares the renderer keeps an answer for, and probes of the depth in a frame

// ------------------------------------------------------------------------------------------------------------
// what the shaders read, shaders/common.glsl has the same layout (std430, and std140 as GLSL ES)
// ------------------------------------------------------------------------------------------------------------

typedef struct vkGlobals_s {	// once per frame
	float		gamma[4];			// x: 1 / r_gamma, y: 2 ^ overbright bits, z: identity light
	float		screen[4];			// x, y: size of the frame in pixels
} vkGlobals_t;

typedef struct vkTransform_s {	// once per view and entity
	float		mvp[16];			// OpenGL clip space, the vertex shader turns it into the one of the device
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

typedef enum {
	VKP_GENERIC,					// vertices in the layout of the tess arrays, one shader stage
	VKP_SKINNED,					// vertices of a Ghoul2 surface, moved by their bones in the vertex shader
	VKP_NUM_PROGRAMS
} vkProgram_t;

// what a draw sets on the device beyond its program and blend function, as one word, so that a draw only sets what
// differs from the draw before it
#define VKS_DEPTH_TEST		0x01u
#define VKS_DEPTH_WRITE		0x02u
#define VKS_DEPTH_EQUAL		0x04u
#define VKS_OFFSET			0x08u
#define VKS_DEPTH_LESS		0x40u
#define VKS_STENCIL			0x80u
#define VKS_CULL_SHIFT		4		// VKC_*, 2 bits

#ifdef RD_VISION_GLES
#include "gles/gles_device.h"
#else
#include "vk_device.h"
#endif

typedef struct vkImage_s : vkDeviceImage_t {	// one entry of the texture table
	image_t			*owner;			// NULL: free
	int				width, height;
	int				mipLevels;
	vkFormat_t		format;			// 32 bit RGBA, or blocks: BC1 without alpha, BC3 with
	int				minFilter, magFilter, wrap;		// OpenGL names, as tr_image.cpp hands them over
	float			anisotropy;
	qboolean		uploaded;		// all levels are there and the layout is the one for sampling
} vkImage_t;

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

typedef struct vkFrame_s : vkDeviceFrame_t {
	vkBuffer_t		vertexes;
	vkBuffer_t		indexes;
	vkBuffer_t		globals;
	vkBuffer_t		transforms;
	vkBuffer_t		draws;
	vkBuffer_t		bones;

	int				numVertexes;		// in the four streams of vertices built on the CPU
	vkSize_t		indexUsed;
	int				numTransforms;
	int				numDraws;
	int				numBones;
	vkProbe_t		probe[VK_MAX_FLARES];
	int				numProbes;
	int				numProbesCopied;	// that many have their copy in the commands of the frame
} vkFrame_t;

#define VK_MAX_STAT_SHADERS	64

typedef struct vkStatShader_s {
	const shader_t	*shader;			// only compared, it may be gone when the name is printed
	char			name[MAX_QPATH];
	int				indexes;
} vkStatShader_t;

typedef struct vkState_s : vkDeviceState_t {
	int							width, height;		// size of the frame, glConfig.vidWidth and vidHeight
	vkTarget_t					color;				// the frame as the game draws it
	vkTarget_t					depth;
	vkTarget_t					final;				// after gamma, what goes to the window and into screenshots
	vkTarget_t					screen;				// a copy of the frame as it was a moment ago, for surfaces that bend what is behind them
	int							screenSlot;			// the entry of the texture table that shows it, -1: none yet
	vkTarget_t					glow;				// r_DynamicGlow: the stages that glow, drawn again over the depth of the scene
	vkTarget_t					blur[2];			// and blurred, r_DynamicGlowWidth x r_DynamicGlowHeight
	qboolean					finalValid;			// a frame has been rendered into it

	vkImage_t					images[VK_MAX_IMAGES];
	image_t						*boundImage;		// GL_Bind(), the image tr_image.cpp is uploading

	vkFrame_t					frames[VK_FRAMES];
	int							frameIndex;
	vkFrame_t					*frame;				// the frame being recorded, NULL between frames
	qboolean					rendering;			// the frame is being drawn into, until VK_EndFrame() ends its last target
	uint32_t					boundState;			// the dynamic state that is set, see VK_DrawTess()
	qboolean					stateValid;
	int							numBoundIndexes;	// the batch that was set up last: that many indexes
	uint32_t					firstIndex;			// from here in the index buffer
	int32_t						vertexOffset;		// counted from this vertex
	const vkStencil_t			*stencil;			// of the draws that follow, NULL: no stencil test, depth as the state bits say
	const vkIndexRange_t		*ranges;			// or, for surfaces of models, the runs of indexes to draw
	int							numRanges;			// 0: numBoundIndexes from firstIndex
	int							viewport[4];		// x, y, width, height as OpenGL counts them
	qboolean					overflowed;			// this frame has more geometry than fits, said once

	// what the frame was made of, for gfxinfo: [0] the frame being recorded, [1] the last finished one
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
} vkState_t;

extern vkState_t vk;

// ------------------------------------------------------------------------------------------------------------
// the shared files
// ------------------------------------------------------------------------------------------------------------

// vk_image.cpp
void		VK_InitImages( void );
void		VK_ShutdownImages( void );
void		VK_UpdateImage( image_t *image, int width, int height, const byte *rgba );
void		VK_CountImages( int *numImages, int *numBlockImages );

// vk_bc.cpp
void		VK_BC_Init( void );
void		VK_BC_Shutdown( void );
vkSize_t	VK_BC_Size( vkFormat_t format, int width, int height );
void		VK_BC_Compress( vkFormat_t format, const byte *rgba, int width, int height, byte *out );

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
qboolean	VK_InitShared( void );
void		VK_InitConfig( void );
void		VK_Splash( void );

// vk_post.cpp
void		VK_InitPost( void );
void		VK_BeginGlow( void );
void		VK_EndGlow( void );
void		VK_CaptureFrame( void );
void		VK_ClearFlares( void );
void		VK_CopyProbes( void );
void		VK_ReadProbes( vkFrame_t *frame );

// vk_frame.cpp
void		VK_BeginFrame( void );
void		VK_EndFrame( void );
void		VK_WriteVideoFrame( void );
void		VK_SetViewport( int x, int y, int width, int height, float minDepth, float maxDepth );
void		VK_FrameInfo_f( void );
int			VK_AddTransform( const vkTransform_t *transform );
void		VK_SetDepthRange( float minDepth, float maxDepth );
qboolean	VK_UploadGeometry( const vec4_t *xyz, const vec4_t *normal, const vec2_t ( *texCoords )[NUM_TEX_COORDS],
				const color4ub_t *colors, int numVerts, const glIndex_t *indexes, int numIndexes );
qboolean	VK_UploadIndexes( const vkBuffer_t *vertexBuffer, const vkSize_t *offsets, int numBindings, const glIndex_t *indexes, int numIndexes );
qboolean	VK_UploadTess( void );
void		VK_TessColors( const color4ub_t *colors );
void		VK_DrawTess( const vkDraw_t *draw, vkProgram_t program, uint32_t stateBits, int cullMode, qboolean polygonOffset );

// ------------------------------------------------------------------------------------------------------------
// what a device has for the shared files, by the Vulkan file it is in. A rectangle is x, y, width, height as
// OpenGL counts them: y from the lower edge of the frame.
// ------------------------------------------------------------------------------------------------------------

// vk_main.cpp. With vk_hooks.h: VK_InitVulkan(), VK_ShutdownVulkan(), VK_GfxInfo_f().
// nothing the device was handed is still being worked on when this returns. Also called before there is a device.
void		VK_WaitIdle( void );

// vk_memory.cpp
// hostVisible: buf->mapped is memory the shared files write, and what is in it when a frame is presented is what
// the draws of that frame read. Else the buffer is filled with VK_CopyBuffer().
void		VK_CreateBuffer( vkBuffer_t *buf, vkSize_t size, vkBufferUsage_t usage, qboolean hostVisible );
void		VK_DestroyBuffer( vkBuffer_t *buf );

// vk_texture.cpp
// the device part of VK_InitImages() and VK_ShutdownImages(). The second one destroys every image of the table.
void		VK_InitTextures( void );
void		VK_ShutdownTextures( void );
// Room for size bytes that are on their way to the device, offset is where they are in vk.staging. They have to be
// handed on with VK_CopyBuffer() or VK_UploadImageLevel() before the next call, which may reuse the room.
byte		*VK_Stage( vkSize_t size, vkSize_t *offset );
// size bytes of a host visible buffer into another buffer, there before a draw reads them
void		VK_CopyBuffer( const vkBuffer_t *src, vkSize_t srcOffset, vkBuffer_t *dst, vkSize_t dstOffset, vkSize_t size );
// everything that was handed over with the two is on the device when this returns
void		VK_FlushUploads( void );
// A texture for a slot of the table. VK_DestroyImage() leaves the slot and its parameters, vi->image is zero after it.
void		VK_CreateImage( vkImage_t *vi, int width, int height, int mipLevels, vkFormat_t format );
void		VK_DestroyImage( vkImage_t *vi );
// The levels of a texture follow, the first one first. created: VK_CreateImage() just made it, else it has contents.
void		VK_BeginImageUpload( vkImage_t *vi, qboolean created );
// one level in the format of the image, its bytes are at staged of VK_Stage()
void		VK_UploadImageLevel( vkImage_t *vi, int level, int width, int height, vkSize_t staged );
void		VK_EndImageUpload( vkImage_t *vi );
// the slot of the table shows its image from now on, with the filters, the wrap mode and the anisotropy vk.images[slot] has
void		VK_WriteTexture( int slot );
// the slot of the table shows the target from now on: filtered, clamped, and for vk.screen with an alpha of 1
void		VK_ShowTarget( int slot, const vkTarget_t *target );
// vk.glow and vk.screen in the size of the frame, vk.blur[] in the size given. vk.color, vk.depth and vk.final are
// made when the device starts.
void		VK_CreatePostTargets( int blurWidth, int blurHeight );

// vk_cmd.cpp
// The device is through with what this frame recorded the last time: its buffers can be written and VK_ProbeDepth()
// answers for it. From here on the frame records.
void		VK_BeginCommands( vkFrame_t *frame );
// What is drawn from now on lands in these targets, depth may be NULL. colorLoad: 0 what is in the target does not
// matter, 1 clear it to zero, 2 keep it. depthLoad: keep the depth and the stencil too, else they are cleared to 1
// and 0. Nothing of the state of a draw survives it, the shared files set stateValid to qfalse.
void		VK_BeginTarget( vkTarget_t *color, vkTarget_t *depth, int colorLoad, qboolean depthLoad );
void		VK_EndTarget( void );
// the buffers of the frame and the texture table are what the draws read: at the start of a frame, and after VK_AddTarget()
void		VK_BindFrameSets( void );
// viewport and scissor of the draws that follow, the scissor cut to the frame
void		VK_ViewportAndScissor( int x, int y, int width, int height, float minDepth, float maxDepth );
// a rectangle of the target that is drawn into, cut to the frame: depth to 1 and stencil to 0, or the color
void		VK_ClearDepth( int x, int y, int width, int height );
void		VK_ClearColor( int x, int y, int width, int height, const float *color );
// The vertices of the draws that follow: numBindings streams of one buffer, each from its offset. 4: xyz, normal,
// texCoords, colors in the layout of the tess arrays (VKP_GENERIC). 1: vkSkinVertex_t (VKP_SKINNED).
void		VK_BindVertexBuffer( const vkBuffer_t *buffer, const vkSize_t *offsets, int numBindings );
// one of those streams from another offset
void		VK_BindVertexStream( int binding, const vkBuffer_t *buffer, vkSize_t offset );
// indexes of 32 bits
void		VK_BindIndexBuffer( const vkBuffer_t *buffer );
// One draw with what is bound: vk.numRanges runs of vk.ranges, or vk.numBoundIndexes indexes from vk.firstIndex
// that count from vk.vertexOffset (with VK_DEVICE_BASE_VERTEX 0 the indexes hold that already). drawIndex: its
// vkDraw_t in the draws of the frame. stateBits: its blend function, GLS_POLYMODE_LINE and VK_GLS_LINES count.
// state: VKS_*, changed: the bits of it that differ from the draw before. With VKS_STENCIL also vk.stencil.
void		VK_Draw( vkProgram_t program, uint32_t stateBits, uint32_t state, uint32_t changed, uint32_t drawIndex );
// The end of a frame, no target is begun: vk.color through shaders/gamma.frag with these four numbers into vk.final,
// vk.final to the window, and everything the frame recorded to the device.
void		VK_PresentFrame( const float *gamma );
// A rectangle of vk.final as it is on the device now: 4 bytes a pixel, red first, the top row first. Stays until
// the next call.
const byte	*VK_ReadFrame( int x, int y, int width, int height );
// One pass of shaders/blur.frag with these four numbers over the whole of target, reading source. No target is begun
// before and after.
void		VK_BlurPass( vkTarget_t *source, vkTarget_t *target, const float *parm );
// source over the whole of the target that is drawn into, added to what is there (soft: GL_ONE, GL_ONE_MINUS_SRC_COLOR)
void		VK_AddTarget( vkTarget_t *source, qboolean soft );
// all of source into dest, which is as large. No target is begun.
void		VK_CopyTarget( vkTarget_t *source, vkTarget_t *dest );
// The depth of vk.depth at the pixels of count probes of the frame, from probe[first] on (x and y count from the
// upper left corner). No target is begun. VK_ProbeDepth() has it when VK_BeginCommands() gets this frame again:
// what the depth buffer holds, 0 to 1.
void		VK_CopyProbeDepths( int first, int count );
float		VK_ProbeDepth( const vkFrame_t *frame, int probe );
