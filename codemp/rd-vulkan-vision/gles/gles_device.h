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

// gles_device.h: the OpenGL ES 3 device of rd-vulkan-vision. Included by vk_local.h in the place of vk_device.h
// when RD_VISION_GLES is defined, see the top of that file for what a device header is. The rest of this file is
// for the files of the device: gles_main.cpp, gles_buffer.cpp, gles_program.cpp, gles_texture.cpp, gles_cmd.cpp.
//
// The device is written for WebGL2, which is OpenGL ES 3.0 with a few things less, and runs as it is on OpenGL
// ES 3 of the desktop. What it does in another way than the Vulkan device:
// - What a frame draws is not handed to OpenGL ES when it is said. It is written down (gles_cmd.cpp) and played
//   when the frame ends, after everything the frame put into its buffers went to the device in one piece each.
//   The buffers of a frame are memory of the engine for that reason, as the mapped buffers of Vulkan are for the
//   shared files.
// - A draw call finds its vkDraw_t, its vkTransform_t and its bones in uniform blocks that are bound to the place
//   of the buffers of the frame where they lie. Such a place has to be a multiple of what the device asks for, so
//   the entries lie further apart than they are long (VK_DRAW_STRIDE, VK_TRANSFORM_STRIDE, VK_BONE_ALIGN).
// - The targets have their top row first, as the images of Vulkan have it, and OpenGL ES has the bottom row first.
//   So the picture is drawn upside down (the vertex shaders turn y, the front face is clockwise for it) and
//   turned again when it is copied to the window. The shared files then need to know nothing about it.
// - WebGL reads no depth back. Whether a flare is covered is asked with a query: is a point at its place and
//   depth seen. The answer comes some frames later, as the depth of the Vulkan device does.
// The names of OpenGL ES come from gles_gl.h, which common/qgl.h includes.

// ------------------------------------------------------------------------------------------------------------
// what the shared files take from a device
// ------------------------------------------------------------------------------------------------------------

#define VK_FRAMES			1			// a frame is played when it ends, nothing of it is on its way after that
#define VK_DRAW_STRIDE		( vk.drawStride )
#define VK_TRANSFORM_STRIDE	( vk.transformStride )
#define VK_BONE_ALIGN		( vk.boneAlign )
#define VK_DEVICE_BASE_VERTEX	0		// WebGL2 has no draw call that names the vertex its indexes count from

typedef size_t				vkSize_t;
typedef GLenum				vkFormat_t;
typedef int					vkBufferUsage_t;

#define VKC_NONE			0
#define VKC_FRONT			1
#define VKC_BACK			2

#define VKF_RGBA8			GL_RGBA8
#define VKF_BC1				GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define VKF_BC3				GL_COMPRESSED_RGBA_S3TC_DXT5_EXT

// A buffer of WebGL is bound to the target it was first bound to and to no other one, so what a buffer is for
// decides once and for all: vertices, indexes, uniform blocks. VKB_STAGING is memory of the engine alone.
#define VKB_VERTEXES		1
#define VKB_INDEXES			2
#define VKB_STAGING			3
#define VKB_UNIFORMS		4			// of this device: the transforms, draws and bones of a frame

typedef struct vkBuffer_s {
	GLuint			buffer;			// zero: nothing on the device
	GLenum			target;			// GL_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER or GL_UNIFORM_BUFFER, for life
	vkSize_t		size;
	byte			*mapped;		// host visible: memory of the engine, see GLES_UploadFrame() and VK_CopyBuffer()
} vkBuffer_t;

typedef struct vkTarget_s {		// something to render into
	GLuint			texture;
	GLuint			framebuffer;		// to draw into it alone and to read it, made when it is first asked for
	GLuint			framebufferDepth;	// to draw into it with vk.depth
	// With several samples a pixel (vk.samples) vk.color, vk.glow and vk.depth have a renderbuffer of that many, and
	// the two with color a framebuffer of theirs with the one of vk.depth: that is what is drawn into then. The
	// texture gets the average when something is about to read it.
	GLuint			renderbuffer;
	GLuint			framebufferSamples;
	qboolean		unresolved;			// the renderbuffer was drawn into since the texture got its average
	int				width, height;
} vkTarget_t;

typedef struct vkDeviceImage_s {	// what an entry of the texture table is on the device, see vkImage_t
	GLuint			image;
} vkDeviceImage_t;

// What a frame asked the device about its flares, see VK_CopyProbeDepths(). The answers are fetched when they are
// there, which is a frame or more later, so a few frames can wait for theirs.
#define GLES_PROBE_SETS		4

typedef struct glesProbeSet_s {
	GLuint			queries[VK_MAX_FLARES];
	short			flare[VK_MAX_FLARES];	// which flare a query is about
	int				count;					// that many were asked
	qboolean		pending;				// and their answers have not been fetched
} glesProbeSet_t;

typedef struct vkDeviceFrame_s {	// see vkFrame_t
	vkSize_t		streamOffsets[4];	// where the four streams of frame->vertexes start, as the frame bound them
	glesProbeSet_t	probeSets[GLES_PROBE_SETS];
	int				probeSet;			// the one the frame that is recorded asks with
} vkDeviceFrame_t;

// ------------------------------------------------------------------------------------------------------------
// what only the files of this device see
// ------------------------------------------------------------------------------------------------------------

#define GLES_BLOCK_GLOBALS		0		// where the uniform blocks of the shaders are bound, as the bindings of Vulkan
#define GLES_BLOCK_TRANSFORMS	1
#define GLES_BLOCK_DRAWS		2
#define GLES_BLOCK_BONES		3
#define GLES_BONES_BYTES		12288	// the block of shaders/skinned.vert: 256 bones of three rows of 16 bytes

// of GL_EXT_texture_filter_anisotropic, which is an extension of OpenGL ES 3 and of WebGL2
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT	0x84FF

typedef enum {
	GLP_GENERIC = VKP_GENERIC,		// the programs of geometry have the numbers the shared files know them by
	GLP_SKINNED = VKP_SKINNED,
	GLP_SPRITE = VKP_SPRITE,
	GLP_GRASS = VKP_GRASS,
	GLP_LIQUID = VKP_LIQUID,
	GLP_GAMMA,						// the passes over a whole target, shaders/post.vert with one fragment shader each
	GLP_BLUR,
	GLP_COPY,
	GLP_FOCUS,						// and one that reads the depth of the scene next to its target
	GLP_SCOPEPREP,					// the same for the scope (VK_ScopePrepPass)
	GLP_SCOPE,						// and the scope itself, four textures and four u_parm (VK_ScopePass)
	GLP_SCREENFXPREP,				// the same for the screen effects (VK_ScreenFxPrepPass)
	GLP_SCREENFX,					// and the screen effects, three textures and VK_SCREENFX_VEC4S u_parm (VK_ScreenFxPass)
	GLP_SCREENFXFOG,				// their mist, the depth and the same u_parm (VK_ScreenFxFogPass)
	GLP_SCREENFXSMOKE,				// their smoke, its last frame, prep and the same u_parm (VK_ScreenFxSmokePass)
	GLP_PRESENT,					// the copy again, turned upside down: what the window gets
	GLP_NUM_PROGRAMS
} glesProgram_t;

// the units of the textures the scope reads besides u_image (0), where no other program reads one
#define GLES_UNIT_SOFT		2
#define GLES_UNIT_PREP		3
#define GLES_UNIT_MARK		4
#define GLES_UNIT_FOG		5		// and the mist of the screen effects
#define GLES_UNIT_SMOKE		6		// and their smoke
#define GLES_UNIT_TEX2		7		// the normal map, the rmo map and the light grid of a stage with a material
#define GLES_UNIT_TEX3		8
#define GLES_UNIT_TEX4		9
#define GLES_UNITS			10

typedef enum {						// what a vertex array object is set up for
	GLV_GENERIC,					// the four streams of the tess arrays
	GLV_SKINNED,					// vkSkinVertex_t
	GLV_GRASS,						// vkGrassVertex_t
	GLV_NONE,						// no vertices: the passes over a whole target, and sprites (shaders/sprite.vert)
	GLV_NUM_LAYOUTS
} glesLayout_t;

typedef struct vkDeviceState_s {	// see vkState_t
	qboolean					device;				// the context is there and with it everything the device makes at its start
	int							uniformAlign;		// GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT
	int							drawStride;			// sizeof( vkDraw_t ) rounded up to it
	int							transformStride;
	int							boneAlign;			// a skeleton starts at a multiple of that many bones
	float						maxAnisotropy;		// with vk.anisotropy: how much of the filter there is
	int							samples;			// r_ext_multisample: samples a pixel of the scene and its glow, 1: no renderbuffers
	qboolean					depthReadable;		// the depth of the scene can be read as a texture (VK_FocusPass): always here

	GLuint						programs[GLP_NUM_PROGRAMS];
	GLint						programParm[GLP_NUM_PROGRAMS];	// where u_parm of a pass is, -1: it has none
	GLuint						vertexArrays[GLV_NUM_LAYOUTS];

	// VK_ShowTarget(): the texture a slot of the table shows in the place of its image, 0: its image
	GLuint						shown[VK_MAX_IMAGES];
	vkBuffer_t					staging;			// what VK_Stage() hands out, always from its start
	byte						*readback;			// VK_ReadFrame()
	vkSize_t					readbackSize;

	byte						flareSeen[VK_MAX_FLARES];	// the last answer of the device for a flare: nothing covers it

	int							statCommands;		// of the last frame, for gfxinfo
	int							statCalls;			// and how many calls of OpenGL ES playing it was
	int							statFlares, statFlaresSeen;	// the last answers of the device: for how many flares, how many of them seen
	// what the device has made and not deleted, for gfxinfo and for the end: nothing may be left
	int							statBuffers, statTextures, statFramebuffers, statRenderbuffers, statQueries;
	int							statErrors;			// v_glesDebug: what glGetError() has said since the start
} vkDeviceState_t;

// gles_main.cpp
extern cvar_t	*v_glesDebug;

// gles_buffer.cpp
void		VK_InitFrames( void );
void		VK_ShutdownFrames( void );
void		GLES_BindBuffer( const vkBuffer_t *buf );
int			GLES_UploadFrame( const struct vkFrame_s *frame );

// gles_program.cpp
qboolean	VK_InitPrograms( void );
void		VK_ShutdownPrograms( void );

// gles_texture.cpp
void		VK_CreateTarget( vkTarget_t *target, int width, int height, GLenum format, GLenum filter, int samples );
void		VK_DestroyTarget( vkTarget_t *target );
void		VK_DestroyPostTargets( void );
GLuint		VK_TargetFramebuffer( vkTarget_t *color, vkTarget_t *depth );
GLuint		VK_DepthFramebuffer( void );
GLuint		VK_SlotTexture( uint32_t slot );
