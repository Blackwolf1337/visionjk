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


// gles_main.cpp: the window with its OpenGL ES context, start and end of the device, gfxinfo

#include "vk_local.h"

vkState_t	vk;

cvar_t		*v_glesDebug;		// 1: glGetError() after every frame. It waits for the device, which costs a browser its speed.
static cvar_t	*r_multisampleGles;	// r_ext_multisample of the engine

/*
A machine or a browser that can not run this device is told why, as with the Vulkan device (see
VK_CanNotRun() in vk_main.cpp): VK_InitVulkan() says why in the console and returns qfalse, with an empty glconfig
but for the reason as its renderer_string.
*/
static qboolean VK_CanNotRun( const char *fmt, ... ) {
	static char reason[256];
	va_list args;

	va_start( args, fmt );
	Q_vsnprintf( reason, sizeof( reason ), fmt, args );
	va_end( args );
	ri.Printf( PRINT_ALL, "rd-vulkan-vision can not run here with OpenGL ES: %s\n", reason );
	memset( &vk, 0, sizeof( vk ) );
	memset( &glConfig, 0, sizeof( glConfig ) );
	glConfig.renderer_string = reason;
	return qfalse;
}

// nothing is on its way: what the device was handed, it was handed at once
void VK_WaitIdle( void ) {
}

static int VK_AlignUp( int size, int align ) {
	return ( size + align - 1 ) / align * align;
}

/*
===============
VK_SampleCount

r_ext_multisample as the Vulkan device takes it: 0 or 1 none, else the largest of 2, 4 and 8 samples a pixel that
is not more than asked for and that the device has for a renderbuffer
===============
*/
static int VK_SampleCount( void ) {
	GLint most = 1;

	glGetIntegerv( GL_MAX_SAMPLES, &most );
	for ( int samples = 8; samples > 1; samples >>= 1 ) {
		if ( r_multisampleGles->integer >= samples && most >= samples ) {
			return samples;
		}
	}
	return 1;
}

// the timer of the steps of a start and the pipelines kept in the profile are the Vulkan device's (vk_main.cpp,
// vk_pipeline.cpp); this one has neither
void VK_StageTime( const char *name ) {
}

void VK_SavePipelineCache( void ) {
}

/*
===============
VK_InitVulkan

Called by R_Init() in the place of InitOpenGL(), the name is the one of the Vulkan device. The window and
everything on the device live until the window is destroyed (vid_restart), a map change keeps them.

qfalse: this machine can not run the device, see VK_CanNotRun().
===============
*/
qboolean VK_InitVulkan( void ) {
	GLint major = 0, align = 1, maxTextureSize = 0;
	const char *extensions;

	v_glesDebug = ri.Cvar_Get( "v_glesDebug", "0", CVAR_NONE, "Ask OpenGL ES for its errors after every frame (slow in a browser)" );
	r_multisampleGles = ri.Cvar_Get( "r_ext_multisample", "0", CVAR_ARCHIVE_ND | CVAR_LATCH, "" );
	// The flares of another level are about to get the numbers of the ones the device answered for: what it said and
	// what it has not said yet is about those.
	memset( vk.flareSeen, 0, sizeof( vk.flareSeen ) );
	for ( int i = 0; i < VK_FRAMES; i++ ) {
		for ( int j = 0; j < GLES_PROBE_SETS; j++ ) {
			vk.frames[i].probeSets[j].pending = qfalse;
		}
	}
	if ( VK_InitShared() ) {
		return qtrue;
	}

	windowDesc_t windowDesc = { GRAPHICS_API_OPENGL };

	windowDesc.gl.majorVersion = 3;
	windowDesc.gl.minorVersion = 0;
	windowDesc.gl.profile = GLPROFILE_ES;
	memset( &glConfig, 0, sizeof( glConfig ) );
	memset( &vk, 0, sizeof( vk ) );
	window = ri.WIN_Init( &windowDesc, &glConfig );
	if ( !glConfig.vidWidth ) {
		return VK_CanNotRun( "no window with OpenGL ES 3 could be made" );
	}
	glGetIntegerv( GL_MAJOR_VERSION, &major );
	if ( major < 3 ) {
		return VK_CanNotRun( "it needs OpenGL ES 3, the window has %s", (const char *)glGetString( GL_VERSION ) );
	}
	vk.width = glConfig.vidWidth;
	vk.height = glConfig.vidHeight;

	// A uniform block is bound to a place of a buffer that is a multiple of this. The draws and transforms of a
	// frame lie that far apart, and a skeleton starts where its 48 byte bones meet such a multiple.
	glGetIntegerv( GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align );
	vk.uniformAlign = Q_max( align, 1 );
	vk.drawStride = VK_AlignUp( sizeof( vkDraw_t ), vk.uniformAlign );
	vk.transformStride = VK_AlignUp( sizeof( vkTransform_t ), vk.uniformAlign );
	for ( vk.boneAlign = 1; ( vk.boneAlign * 48 ) % vk.uniformAlign; vk.boneAlign++ ) {
	}

	// the anisotropic filter is an extension, in a browser too
	extensions = (const char *)glGetString( GL_EXTENSIONS );
	if ( extensions && strstr( extensions, "GL_EXT_texture_filter_anisotropic" ) ) {
		glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &vk.maxAnisotropy );
		vk.anisotropy = (qboolean)( vk.maxAnisotropy > 1.0f );
	}

	// the rows of what is handed over and read back follow each other without gaps
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	glDisable( GL_DITHER );
	// the picture is drawn upside down, see gles_device.h: what OpenGL calls counter-clockwise comes clockwise
	glFrontFace( GL_CW );

	if ( !VK_InitPrograms() ) {
		VK_ShutdownPrograms();
		return VK_CanNotRun( "a shader did not compile, see above" );
	}
	// The gamma pass reads the frame pixel for pixel, and so does the copy to the window. Anti aliasing: the scene
	// goes into renderbuffers with several samples a pixel, see gles_texture.cpp.
	vk.samples = VK_SampleCount();
	VK_CreateTarget( &vk.color, vk.width, vk.height, GL_RGBA8, GL_NEAREST, vk.samples );
	VK_CreateTarget( &vk.final, vk.width, vk.height, GL_RGBA8, GL_NEAREST, 1 );
	VK_CreateTarget( &vk.depth, vk.width, vk.height, GL_DEPTH24_STENCIL8, GL_NEAREST, vk.samples );
	vk.depthReadable = qtrue;
	VK_InitPost();
	VK_InitFrames();
	VK_InitImages();
	VK_InitModels();
	vk.device = qtrue;

	// what the engine, cgame and ui want to know about the renderer
	glConfig.renderer_string = (const char *)glGetString( GL_RENDERER );
	glConfig.vendor_string = (const char *)glGetString( GL_VENDOR );
	glConfig.version_string = (const char *)glGetString( GL_VERSION );
	glGetIntegerv( GL_MAX_TEXTURE_SIZE, &maxTextureSize );
	glConfig.maxTextureSize = Q_min( maxTextureSize, 8192 );
	glConfig.maxTextureFilterAnisotropy = vk.anisotropy ? vk.maxAnisotropy : 0.0f;
	VK_InitConfig();

	ri.Printf( PRINT_ALL, "OpenGL ES device: %s, %s\n", glConfig.renderer_string, glConfig.version_string );
	VK_Splash();
	// the Vulkan device shows a frame when it ends. Here the engine does it once a frame for its window
	// (RB_SwapBuffers), which the splash picture does not come by.
	ri.WIN_Present( &window );
	return qtrue;
}

/*
===============
VK_ShutdownVulkan

Called by RE_Shutdown() before the window goes. Without destroyWindow (map change) everything stays.
===============
*/
void VK_ShutdownVulkan( qboolean destroyWindow ) {
	if ( !vk.device || !destroyWindow ) {
		return;
	}
	VK_ShutdownWorld();
	VK_ShutdownModels();
	VK_ShutdownImages();
	VK_ShutdownFrames();
	VK_DestroyPostTargets();
	VK_DestroyTarget( &vk.color );
	VK_DestroyTarget( &vk.final );
	VK_DestroyTarget( &vk.depth );
	VK_ShutdownPrograms();
	if ( vk.readback ) {
		Z_Free( vk.readback );
	}
	if ( vk.statBuffers || vk.statTextures || vk.statFramebuffers || vk.statRenderbuffers || vk.statQueries ) {
		ri.Printf( PRINT_ALL, S_COLOR_RED "OpenGL ES: left on the device at its end: %i buffers, %i textures, %i framebuffers, %i renderbuffers, %i queries\n",
			vk.statBuffers, vk.statTextures, vk.statFramebuffers, vk.statRenderbuffers, vk.statQueries );
	}
	memset( &vk, 0, sizeof( vk ) );
	memset( &glConfig, 0, sizeof( glConfig ) );
}

// The targets are named by the commands of a frame that is written down and played at its end, and a page restarts
// the renderer when its size settles anyway: no live resize here.
qboolean VK_ResizeFrame( int width, int height ) {
	return qfalse;
}

/*
===============
VK_GfxInfo_f
===============
*/
void VK_GfxInfo_f( void ) {
	int numImages, numBlockImages;

	VK_CountImages( &numImages, &numBlockImages );
	ri.Printf( PRINT_ALL, "\nrd-vulkan-vision with OpenGL ES\n" );
	ri.Printf( PRINT_ALL, "device: %s\n", glConfig.renderer_string );
	ri.Printf( PRINT_ALL, "version: %s\n", glConfig.version_string );
	ri.Printf( PRINT_ALL, "vendor: %s\n", glConfig.vendor_string );
	ri.Printf( PRINT_ALL, "frame: %i x %i with %i samples a pixel, uniform blocks every %i bytes: a draw takes %i, a transform %i, a skeleton starts every %i bones\n",
		vk.width, vk.height, vk.samples, vk.uniformAlign, vk.drawStride, vk.transformStride, vk.boneAlign );
	// what a level change or a restart leaves more of each time is a leak
	ri.Printf( PRINT_ALL, "on the device: %i buffers, %i textures, %i framebuffers, %i renderbuffers, %i queries\n", vk.statBuffers, vk.statTextures,
		vk.statFramebuffers, vk.statRenderbuffers, vk.statQueries );
	ri.Printf( PRINT_ALL, "textures: %i of %i, the last frame: %i commands, played with %i calls of OpenGL ES\n", numImages, VK_MAX_IMAGES,
		vk.statCommands, vk.statCalls );
	ri.Printf( PRINT_ALL, "flares: the last answers of the device were for %i, %i of them seen\n", vk.statFlares, vk.statFlaresSeen );
	VK_FrameInfo_f();
	if ( v_glesDebug->integer ) {
		ri.Printf( PRINT_ALL, "errors of OpenGL ES so far: %i\n", vk.statErrors );
	}
}
