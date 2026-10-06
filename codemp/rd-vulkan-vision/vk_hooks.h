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

// rd-vulkan-vision builds the files of rd-vanilla that hold no graphics code (loaders, the shader parser, Ghoul2,
// the scene) into its own library instead of keeping copies of them. This header is what those files see in
// place of OpenGL. It is included at the end of rd-vanilla/tr_local.h when RD_VULKAN_VISION is defined.

// vk_main.cpp
void VK_InitVulkan( void );
void VK_ShutdownVulkan( qboolean destroyWindow );
void VK_GfxInfo_f( void );

// vk_screenshot.cpp: what tr_init.cpp has between its screenshot commands and the frame buffer
byte *RB_ReadPixels( int x, int y, int width, int height, size_t *offset, int *padlen );
const void *RB_TakeVideoFrameCmd( const void *data );

// vk_image.cpp: the upload of one texture as tr_image.cpp drives it. GL_Bind() picks the image, the levels
// follow, the parameters pick its sampler.
void VK_ImageLevel( int level, int width, int height, const byte *rgba );
void VK_ImageParm( int name, int value );
void VK_ImageDelete( unsigned int texnum );

// The few OpenGL calls left in the shared files, by where they are:
// tr_image.cpp Upload32, R_CreateImage, GL_TextureMode, R_Images_DeleteImageContents
#undef qglTexImage2D
#define qglTexImage2D( target, level, internalFormat, width, height, border, format, type, data ) \
	VK_ImageLevel( level, width, height, (const byte *)(data) )
#undef qglTexParameterf
#define qglTexParameterf( target, name, value )		VK_ImageParm( name, (int)(value) )
#undef qglTexParameterfv
#define qglTexParameterfv( target, name, value )	((void)0)
#undef qglBindTexture
#define qglBindTexture( target, texnum )			((void)0)
#undef qglDeleteTextures
#define qglDeleteTextures( count, texnums )			VK_ImageDelete( *(texnums) )
// two textures in one stage: always there (tr_shader.cpp CollapseMultitexture, tr_image.cpp)
#define qglActiveTextureARB							true

// Drawing with immediate mode and with state of its own: debug polygons (tr_main.cpp), the automap (tr_world.cpp),
// the overdraw counter and the error check (tr_cmds.cpp), the axis and beam surfaces, the display list and the
// depth read of a flare (tr_surface.cpp), the sides of the sky box and the sun (tr_sky.cpp), the level shot
// (tr_init.cpp). None of it reaches the screen here. What of it the game needs is drawn by the back end of this
// renderer in its own way.
#undef qglBegin
#define qglBegin( ... )				((void)0)
#undef qglEnd
#define qglEnd( ... )				((void)0)
#undef qglVertex3f
#define qglVertex3f( ... )			((void)0)
#undef qglVertex3fv
#define qglVertex3fv( ... )			((void)0)
#undef qglTexCoord2fv
#define qglTexCoord2fv( ... )		((void)0)
#undef qglColor3f
#define qglColor3f( ... )			((void)0)
#undef qglColor4f
#define qglColor4f( ... )			((void)0)
#undef qglLineWidth
#define qglLineWidth( ... )			((void)0)
#undef qglCallList
#define qglCallList( ... )			((void)0)
#undef qglEnable
#define qglEnable( ... )			((void)0)
#undef qglDisable
#define qglDisable( ... )			((void)0)
#undef qglDepthRange
#define qglDepthRange( ... )		((void)0)
#undef qglLoadIdentity
#define qglLoadIdentity( ... )		((void)0)
#undef qglLoadMatrixf
#define qglLoadMatrixf( ... )		((void)0)
#undef qglPushMatrix
#define qglPushMatrix( ... )		((void)0)
#undef qglPopMatrix
#define qglPopMatrix( ... )			((void)0)
#undef qglTranslatef
#define qglTranslatef( ... )		((void)0)
#undef qglStencilMask
#define qglStencilMask( ... )		((void)0)
#undef qglClearStencil
#define qglClearStencil( ... )		((void)0)
#undef qglStencilFunc
#define qglStencilFunc( ... )		((void)0)
#undef qglStencilOp
#define qglStencilOp( ... )			((void)0)
#undef qglPixelStorei
#define qglPixelStorei( ... )		((void)0)
#undef qglReadPixels
#define qglReadPixels( ... )		((void)0)
#undef qglTexParameteri
#define qglTexParameteri( ... )		((void)0)
#undef qglGetError
#define qglGetError()				GL_NO_ERROR
