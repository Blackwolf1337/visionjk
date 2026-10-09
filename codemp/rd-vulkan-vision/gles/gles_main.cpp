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

cvar_t		*r_glesDebug;		// 1: glGetError() after every frame. It waits for the device, which costs a browser its speed.

/*
A machine or a browser that can not run this device is no error of the game, as with the Vulkan device (see
VK_CanNotRun() in vk_main.cpp): VK_InitVulkan() says why in the console and returns qfalse, with an empty glconfig.
On the desktop the engine goes on with rd-vanilla then.
*/
static qboolean VK_CanNotRun( const char *fmt, ... ) {
	va_list args;
	char reason[256];

	va_start( args, fmt );
	Q_vsnprintf( reason, sizeof( reason ), fmt, args );
	va_end( args );
	ri.Printf( PRINT_ALL, "rd-vulkan-vision can not run here with OpenGL ES: %s\n", reason );
	memset( &vk, 0, sizeof( vk ) );
	memset( &glConfig, 0, sizeof( glConfig ) );
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
VK_InitVulkan

Called by R_Init() in the place of InitOpenGL(), the name is the one of the Vulkan device. The window and
everything on the device live until the window is destroyed (vid_restart), a map change keeps them.

qfalse: this machine can not run the device, see VK_CanNotRun().
===============
*/
qboolean VK_InitVulkan( void ) {
	GLint major = 0, align = 1, maxTextureSize = 0;
	const char *extensions;

	r_glesDebug = ri.Cvar_Get( "r_glesDebug", "0", CVAR_ARCHIVE_ND, "Ask OpenGL ES for its errors after every frame (slow in a browser)" );
	if ( VK_InitShared() ) {
		return qtrue;
	}

	windowDesc_t windowDesc = { GRAPHICS_API_OPENGL };

	windowDesc.gl.majorVersion = 3;
	windowDesc.gl.minorVersion = 0;
	windowDesc.gl.profile = GLPROFILE_ES;
	memset( &glConfig, 0, sizeof( glConfig ) );
	memset( &glConfigExt, 0, sizeof( glConfigExt ) );
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
	// the gamma pass reads the frame pixel for pixel, and so does the copy to the window
	VK_CreateTarget( &vk.color, vk.width, vk.height, GL_RGBA8, GL_NEAREST );
	VK_CreateTarget( &vk.final, vk.width, vk.height, GL_RGBA8, GL_NEAREST );
	VK_CreateTarget( &vk.depth, vk.width, vk.height, GL_DEPTH24_STENCIL8, GL_NEAREST );
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
	memset( &vk, 0, sizeof( vk ) );
	memset( &glConfig, 0, sizeof( glConfig ) );
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
	ri.Printf( PRINT_ALL, "frame: %i x %i, uniform blocks every %i bytes: a draw takes %i, a transform %i, a skeleton starts every %i bones\n",
		vk.width, vk.height, vk.uniformAlign, vk.drawStride, vk.transformStride, vk.boneAlign );
	ri.Printf( PRINT_ALL, "textures: %i of %i, commands of the last frame: %i\n", numImages, VK_MAX_IMAGES, vk.statCommands );
	VK_FrameInfo_f();
	if ( r_glesDebug->integer ) {
		ri.Printf( PRINT_ALL, "errors of OpenGL ES so far: %i\n", vk.statErrors );
	}
}
