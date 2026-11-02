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

// The files of common/ come from the OpenGL renderer of the game and hold no graphics code of their own (loaders,
// the shader parser, Ghoul2, the scene). This header is what they see in place of OpenGL. It is included at the
// end of common/tr_local.h.

// vk_main.cpp
qboolean VK_InitVulkan( void );		// qfalse: this machine can not run the renderer
void VK_ShutdownVulkan( qboolean destroyWindow );
void VK_GfxInfo_f( void );
// the window has that size now: the frame follows it between two frames. qfalse: this device can not, the engine
// restarts the renderer instead
qboolean VK_ResizeFrame( int width, int height );

// vk_frame.cpp: what tr_init.cpp has between its screenshot commands and the frame buffer
byte *RB_ReadPixels( int x, int y, int width, int height, size_t *offset, int *padlen );
const void *RB_TakeVideoFrameCmd( const void *data );

// vk_post.cpp: what RB_TestZFlare() of tr_surface.cpp asks the depth buffer
qboolean VK_FlareVisible( const float *point, int x, int y, float eyeZ );
// and whether RE_SetScopeEffect() of tr_cmds.cpp is drawn
qboolean VK_HasScope( void );
// whether RE_AddWet() is drawn, and whether its puddles mirror a view of their own (R_PuddleView of tr_main.cpp)
qboolean VK_HasWet( void );
qboolean VK_PuddleMirror( void );

// tr_ghoul2.cpp, for vk_model.cpp: the bone cache is a class of that file
int G2_BoneMatrices( CBoneCache *boneCache, float *out, int maxBones );
void G2_SkinGore( CRenderableSurface *surf );
// tr_surfacesprites.cpp, for the surface sprites vk_world.cpp draws on the device: the wind and the vectors of the
// view of this frame, as RB_DrawSurfaceSprites() works them out before it draws
void RB_SurfaceSpriteFrame( void );
extern float	curWindSpeed, curWindPointForce, rangescalefactor;
extern qboolean	curWindPointActive;
extern vec3_t	curWindGrassDir, curWindPoint, ssrightvectors[4], ssfwdvector, ssViewOrigin, ssViewRight, ssViewUp;
extern const float	randomchart[256];
// vk_gl.cpp, for CWeatherParticleCloud::Render() of tr_WorldEffects.cpp: its particles as records of
// shaders/sprite.vert with the GL state that is set, kind 1 (a quad of two vectors) or 5 (a triangle). qfalse from
// VK_GL_QuadsOn(): they are built corner by corner as in rd-vanilla.
qboolean VK_GL_QuadsOn( void );
void VK_GL_Quad( int kind, const float *a, const float *b, const float *c, const float *st, const float *color );
void VK_GL_QuadFlush( void );
// vk_world.cpp, for DoLine2() of tr_surface.cpp: one segment of a bolt into a batch of quads (shaders/sprite.vert).
// qfalse: the batch is not one, the segment goes into the tess arrays.
qboolean VK_AddBoltSegment( const float *start, const float *end, const float *side, float startWidth, float endWidth );
// vk_model.cpp, for G2_misc.cpp: the data of a saber mark on a model is freed
void VK_GoreFreed( const void *goreData );

// vk_image.cpp: the upload of one texture as tr_image.cpp drives it. GL_Bind() picks the image, the levels
// follow, the parameters pick its sampler.
void VK_ImageLevel( int level, int internalFormat, int width, int height, const byte *rgba );
void VK_ImageParm( int name, int value );
void VK_ImageDelete( unsigned int texnum );
void VK_UpdateImage( image_t *image, int width, int height, const byte *rgba );	// for the glyph pages of rd-common/tr_font_ttf.cpp

// vk_texcache.cpp: the texture of an image file as an earlier run packed it, see there for the order of the calls
qboolean VK_TexCacheFind( const char *name, qboolean mipmap, qboolean allowPicmip, qboolean allowTC, int *width, int *height );
qboolean VK_TexCacheUpload( int *internalFormat, int *width, int *height );
void VK_TexCacheRecord( void );
void VK_TexCacheLuma( float luma );
float VK_TexCacheFoundLuma( void );
void VK_TexCacheEnd( void );

// The few OpenGL calls left in the shared files, by where they are:
// tr_image.cpp Upload32, R_CreateImage, GL_TextureMode, R_Images_DeleteImageContents
#undef qglTexImage2D
#define qglTexImage2D( target, level, internalFormat, width, height, border, format, type, data ) \
	VK_ImageLevel( level, internalFormat, width, height, (const byte *)(data) )
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
// debug polygons (tr_main.cpp), the automap (tr_world.cpp), the particles of the weather (tr_WorldEffects.cpp).
// The calls collect vertices and state, glEnd() draws them like any other geometry.
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
// the sprites that grow on surfaces (tr_quicksprite.cpp) come as arrays of quads, with glFog over them
void VK_GL_Pointer( int which, int stride, const void *data );
void VK_GL_ClientState( int which, int on );
void VK_GL_DrawArrays( int mode, int first, int count );
void VK_GL_Enable( int cap, int on );
void VK_GL_Flush( void );
void VK_GL_Collect( qboolean on );
void VK_GL_StencilFunc( int func, int ref );
void VK_GL_StencilOp( int face, int fail, int zfail, int zpass );
void VK_GL_Fogf( int name, float value );
void VK_GL_Fogfv( int name, const float *value );
static void ( * const vk_glNoLockArrays )( int, int ) = 0;
static void ( * const vk_glNoUnlockArrays )( void ) = 0;

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
#undef qglTexCoord2f
#define qglTexCoord2f( s, t )		VK_GL_TexCoord( s, t )
#undef qglColor3f
#define qglColor3f( r, g, b )		VK_GL_Color( r, g, b, 1.0f )
#undef qglColor4f
#define qglColor4f( r, g, b, a )	VK_GL_Color( r, g, b, a )
#undef qglColor4ub
#define qglColor4ub( r, g, b, a )	VK_GL_Color( ( r ) / 255.0f, ( g ) / 255.0f, ( b ) / 255.0f, ( a ) / 255.0f )
#undef qglColor4ubv
#define qglColor4ubv( c )			VK_GL_Color( ( c )[0] / 255.0f, ( c )[1] / 255.0f, ( c )[2] / 255.0f, ( c )[3] / 255.0f )
#undef qglVertexPointer
#define qglVertexPointer( size, type, stride, data )		VK_GL_Pointer( GL_VERTEX_ARRAY, stride, data )
#undef qglTexCoordPointer
#define qglTexCoordPointer( size, type, stride, data )	VK_GL_Pointer( GL_TEXTURE_COORD_ARRAY, stride, data )
#undef qglColorPointer
#define qglColorPointer( size, type, stride, data )		VK_GL_Pointer( GL_COLOR_ARRAY, stride, data )
#undef qglEnableClientState
#define qglEnableClientState( which )	VK_GL_ClientState( which, 1 )
#undef qglDisableClientState
#define qglDisableClientState( which )	VK_GL_ClientState( which, 0 )
#undef qglDrawArrays
#define qglDrawArrays( mode, first, count )	VK_GL_DrawArrays( mode, first, count )
#undef qglFogf
#define qglFogf( name, value )		VK_GL_Fogf( name, value )
#undef qglFogfv
#define qglFogfv( name, value )		VK_GL_Fogfv( name, value )
#define qglLockArraysEXT			vk_glNoLockArrays
#define qglUnlockArraysEXT			vk_glNoUnlockArrays
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

// the stencil test of the overdraw measurement (tr_cmds.cpp)
#undef qglStencilFunc
#define qglStencilFunc( func, ref, mask )	VK_GL_StencilFunc( func, ref )
#undef qglStencilOp
#define qglStencilOp( fail, zfail, zpass )	VK_GL_StencilOp( GL_FRONT_AND_BACK, fail, zfail, zpass )
#undef qglStencilOpSeparate
#define qglStencilOpSeparate( face, fail, zfail, zpass )	VK_GL_StencilOp( face, fail, zfail, zpass )
#undef qglIsEnabled
#define qglIsEnabled( cap )			0

// What is left has no meaning here (of glEnable the fog and the stencil test have): line width and display lists,
// the stencil mask and clear of the overdraw counter and the error check (tr_cmds.cpp), the level shot (tr_init.cpp)
#undef qglLineWidth
#define qglLineWidth( ... )			((void)0)
#undef qglMatrixMode
#define qglMatrixMode( ... )		((void)0)
#undef qglCallList
#define qglCallList( ... )			((void)0)
#undef qglEnable
#define qglEnable( cap )			VK_GL_Enable( cap, 1 )
#undef qglDisable
#define qglDisable( cap )			VK_GL_Enable( cap, 0 )
#undef qglStencilMask
#define qglStencilMask( ... )		((void)0)
#undef qglClearStencil
#define qglClearStencil( ... )		((void)0)
#undef qglPixelStorei
#define qglPixelStorei( ... )		((void)0)
#undef qglReadPixels
#define qglReadPixels( ... )		((void)0)
#undef qglTexParameteri
#define qglTexParameteri( ... )		((void)0)
#undef qglGetError
#define qglGetError()				GL_NO_ERROR
