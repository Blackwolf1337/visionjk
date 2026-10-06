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

// vk_gl.cpp: the drawing rd-vanilla does in the shared files with immediate mode and with state and matrices
// of its own: the sides of the sky box and the sun (tr_sky.cpp), the beam and axis surfaces (tr_surface.cpp),
// debug polygons (tr_main.cpp), the automap (tr_world.cpp). The calls collect vertices and state, glEnd()
// draws them like any other geometry.
void VK_GL_Begin( int mode );
void VK_GL_End( void );
void VK_GL_Vertex( float x, float y, float z );
void VK_GL_TexCoord( float s, float t );
void VK_GL_Color( float r, float g, float b, float a );
void VK_GL_DepthRange( float zNear, float zFar );
void VK_GL_PushMatrix( void );
void VK_GL_PopMatrix( void );
void VK_GL_LoadMatrix( const float *m );
void VK_GL_Translate( float x, float y, float z );

#undef qglBegin
#define qglBegin( mode )			VK_GL_Begin( mode )
#undef qglEnd
#define qglEnd()					VK_GL_End()
#undef qglVertex3f
#define qglVertex3f( x, y, z )		VK_GL_Vertex( x, y, z )
#undef qglVertex3fv
#define qglVertex3fv( v )			VK_GL_Vertex( ( v )[0], ( v )[1], ( v )[2] )
#undef qglTexCoord2fv
#define qglTexCoord2fv( st )		VK_GL_TexCoord( ( st )[0], ( st )[1] )
#undef qglColor3f
#define qglColor3f( r, g, b )		VK_GL_Color( r, g, b, 1.0f )
#undef qglColor4f
#define qglColor4f( r, g, b, a )	VK_GL_Color( r, g, b, a )
#undef qglDepthRange
#define qglDepthRange( n, f )		VK_GL_DepthRange( n, f )
#undef qglPushMatrix
#define qglPushMatrix()				VK_GL_PushMatrix()
#undef qglPopMatrix
#define qglPopMatrix()				VK_GL_PopMatrix()
#undef qglLoadMatrixf
#define qglLoadMatrixf( m )			VK_GL_LoadMatrix( m )
#undef qglLoadIdentity
#define qglLoadIdentity()			VK_GL_LoadMatrix( NULL )
#undef qglTranslatef
#define qglTranslatef( x, y, z )	VK_GL_Translate( x, y, z )

// What is left has no meaning here: line width and display lists, the stencil setup of the overdraw counter and
// the error check (tr_cmds.cpp), the depth read of a flare (tr_surface.cpp), the level shot (tr_init.cpp)
#undef qglLineWidth
#define qglLineWidth( ... )			((void)0)
#undef qglCallList
#define qglCallList( ... )			((void)0)
#undef qglEnable
#define qglEnable( ... )			((void)0)
#undef qglDisable
#define qglDisable( ... )			((void)0)
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
