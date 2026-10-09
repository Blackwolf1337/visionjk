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


// gles_texture.cpp: what a texture and a render target are for OpenGL ES
//
// vk_image.cpp keeps the texture table and decides what a texture looks like. Here a slot of the table is a
// texture of OpenGL ES with its filters and its wrap mode set on it. A draw names the slots of its two textures
// (vkDraw_t), and when it is played the textures of those slots are bound, see VK_SlotTexture().

#include "vk_local.h"

/*
===============================================================================

THE IMAGES OF THE TEXTURE TABLE

===============================================================================
*/

void VK_InitTextures( void ) {
	glActiveTexture( GL_TEXTURE0 );
}

void VK_ShutdownTextures( void ) {
	for ( int i = 0; i < VK_MAX_IMAGES; i++ ) {
		if ( vk.images[i].image ) {
			glDeleteTextures( 1, &vk.images[i].image );
			vk.statTextures--;
		}
	}
	VK_DestroyBuffer( &vk.staging );
}

// the filters and the wrap mode of a slot onto its texture, which is bound
static void VK_TextureParms( const vkImage_t *vi ) {
	GLint minFilter = GL_LINEAR;

	switch ( vi->minFilter ) {
	case GL_NEAREST:
	case GL_NEAREST_MIPMAP_NEAREST:
	case GL_LINEAR_MIPMAP_NEAREST:
	case GL_NEAREST_MIPMAP_LINEAR:
	case GL_LINEAR_MIPMAP_LINEAR:
		minFilter = vi->minFilter;
		break;
	}
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, ( vi->magFilter == GL_NEAREST ) ? GL_NEAREST : GL_LINEAR );
	// GL_CLAMP of OpenGL is not there, and what the game means by it is the edge
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ( vi->wrap == GL_REPEAT ) ? GL_REPEAT : GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ( vi->wrap == GL_REPEAT ) ? GL_REPEAT : GL_CLAMP_TO_EDGE );
	if ( vk.anisotropy ) {
		glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, Com_Clamp( 1.0f, vk.maxAnisotropy, vi->anisotropy ) );
	}
}

// The levels are fixed when the texture is made, as for an image of Vulkan: vk_image.cpp makes another one when
// the size changes.
void VK_CreateImage( vkImage_t *vi, int width, int height, int mipLevels, vkFormat_t format ) {
	glGenTextures( 1, &vi->image );
	vk.statTextures++;
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, vi->image );
	glTexStorage2D( GL_TEXTURE_2D, mipLevels, format, width, height );
	VK_TextureParms( vi );
}

void VK_DestroyImage( vkImage_t *vi ) {
	if ( vi->image ) {
		glDeleteTextures( 1, &vi->image );
		vk.statTextures--;
		vi->image = 0;
	}
}

void VK_BeginImageUpload( vkImage_t *, qboolean ) {
}

// with vk.bcTextures off every image is VKF_RGBA8
void VK_UploadImageLevel( vkImage_t *vi, int level, int width, int height, vkSize_t staged ) {
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, vi->image );
	glTexSubImage2D( GL_TEXTURE_2D, level, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, vk.staging.mapped + staged );
}

void VK_EndImageUpload( vkImage_t * ) {
}

void VK_WriteTexture( int slot ) {
	const vkImage_t *vi = &vk.images[slot];

	vk.shown[slot] = 0;
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, vi->image );
	VK_TextureParms( vi );
}

void VK_ShowTarget( int slot, const vkTarget_t *target ) {
	vk.shown[slot] = target->texture;
}

// the texture a draw gets for a slot it names, 0: none
GLuint VK_SlotTexture( uint32_t slot ) {
	if ( slot >= VK_MAX_IMAGES ) {
		return 0;
	}
	return vk.shown[slot] ? vk.shown[slot] : vk.images[slot].image;
}

/*
===============================================================================

TEXTURES IN BLOCKS

vk_bc.cpp, which packs textures into BC1 and BC3 blocks, is not built with this device: it has worker threads,
and a module in a browser has none. vk.bcTextures stays off, so vk_image.cpp asks for no such texture, and this
is what it still calls.

===============================================================================
*/

void VK_BC_Init( void ) {
}

void VK_BC_Shutdown( void ) {
}

vkSize_t VK_BC_Size( vkFormat_t, int, int ) {
	return 0;
}

void VK_BC_Compress( vkFormat_t, const byte *, int, int, byte * ) {
}

/*
===============================================================================

RENDER TARGETS

A target is a texture. What is drawn into is a framebuffer object with the texture of a target as its color,
alone or with vk.depth, made the first time it is asked for.

Anti aliasing (r_ext_multisample): a texture of WebGL has one sample a pixel. The scene and its glow are drawn
into renderbuffers with several, color and depth, and the texture of a target gets their average when something
is about to read it (GLES_Resolve() in gles_cmd.cpp): the gamma pass, the copy for refraction, the blur of the
glow. The depth of several samples is never brought down: the flares ask the device, they do not read it.

===============================================================================
*/

// samples above 1: the target also gets a renderbuffer with that many samples a pixel
void VK_CreateTarget( vkTarget_t *target, int width, int height, GLenum format, GLenum filter, int samples ) {
	memset( target, 0, sizeof( *target ) );
	if ( samples > 1 ) {
		glGenRenderbuffers( 1, &target->renderbuffer );
		vk.statRenderbuffers++;
		glBindRenderbuffer( GL_RENDERBUFFER, target->renderbuffer );
		glRenderbufferStorageMultisample( GL_RENDERBUFFER, samples, format, width, height );
	}
	glGenTextures( 1, &target->texture );
	vk.statTextures++;
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, target->texture );
	glTexStorage2D( GL_TEXTURE_2D, 1, format, width, height );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	target->width = width;
	target->height = height;
}

void VK_DestroyTarget( vkTarget_t *target ) {
	GLuint *framebuffers[3] = { &target->framebuffer, &target->framebufferDepth, &target->framebufferSamples };

	for ( int i = 0; i < 3; i++ ) {
		if ( *framebuffers[i] ) {
			glDeleteFramebuffers( 1, framebuffers[i] );
			vk.statFramebuffers--;
		}
	}
	if ( target->renderbuffer ) {
		glDeleteRenderbuffers( 1, &target->renderbuffer );
		vk.statRenderbuffers--;
	}
	if ( target->texture ) {
		glDeleteTextures( 1, &target->texture );
		vk.statTextures--;
	}
	memset( target, 0, sizeof( *target ) );
}

/*
===============
VK_TargetFramebuffer

What is bound to draw into a target, with depth or alone, or to read it (alone). With depth and several samples
a pixel that is the framebuffer of the two renderbuffers. Leaves it bound as GL_FRAMEBUFFER when it had to be
made.
===============
*/
GLuint VK_TargetFramebuffer( vkTarget_t *color, vkTarget_t *depth ) {
	const qboolean samples = (qboolean)( depth && color->renderbuffer && depth->renderbuffer );
	GLuint *framebuffer = samples ? &color->framebufferSamples : ( depth ? &color->framebufferDepth : &color->framebuffer );

	if ( !*framebuffer ) {
		glGenFramebuffers( 1, framebuffer );
		vk.statFramebuffers++;
		glBindFramebuffer( GL_FRAMEBUFFER, *framebuffer );
		if ( samples ) {
			glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color->renderbuffer );
			glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth->renderbuffer );
		} else {
			glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color->texture, 0 );
			if ( depth ) {
				glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depth->texture, 0 );
			}
		}
		// asked once for each: the answer makes a browser wait for its graphics process
		if ( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE ) {
			ri.Printf( PRINT_ALL, S_COLOR_RED "OpenGL ES: a framebuffer of %i x %i%s is not complete\n", color->width, color->height,
				depth ? " with depth" : "" );
		}
	}
	return *framebuffer;
}

/*
===============
VK_CreatePostTargets

OpenGL copies a frame buffer that has no alpha into the texture of a refraction surface, and such a texture reads
as 1 there. The frame here has an alpha channel with what the blends left in it, so its copy is a texture without
one.
===============
*/
void VK_CreatePostTargets( int blurWidth, int blurHeight ) {
	// the glowing stages are drawn over the depth of the scene, so with as many samples
	VK_CreateTarget( &vk.glow, vk.width, vk.height, GL_RGBA8, GL_LINEAR, vk.samples );
	VK_CreateTarget( &vk.screen, vk.width, vk.height, GL_RGB8, GL_LINEAR, 1 );
	VK_CreateTarget( &vk.blur[0], blurWidth, blurHeight, GL_RGBA8, GL_LINEAR, 1 );
	VK_CreateTarget( &vk.blur[1], blurWidth, blurHeight, GL_RGBA8, GL_LINEAR, 1 );
}

void VK_DestroyPostTargets( void ) {
	VK_DestroyTarget( &vk.glow );
	VK_DestroyTarget( &vk.screen );
	VK_DestroyTarget( &vk.blur[0] );
	VK_DestroyTarget( &vk.blur[1] );
}
