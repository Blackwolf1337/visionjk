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

// vk_image.cpp: the texture table
//
// All textures of the game sit in one table that every shader sees. image_t::texnum is the index.
// tr_image.cpp of rd-vanilla still decides what a texture looks like (size, picmip, light scale,
// mip levels) and drives the upload: GL_Bind( image ), then VK_ImageLevel() per level and VK_ImageParm() for
// filter and wrap mode, see vk_hooks.h. What a texture is on the device is vk_texture.cpp.

#include "vk_local.h"
#include <chrono>

// R_CreateImage() gives a new image a number of its own, the first upload replaces it by the slot
static vkImage_t *VK_SlotOf( const image_t *image ) {
	if ( image && image->texnum < VK_MAX_IMAGES && vk.images[image->texnum].owner == image ) {
		return &vk.images[image->texnum];
	}
	return NULL;
}

static void VK_FreeSlot( vkImage_t *vi ) {
	VK_DestroyImage( vi );
	memset( vi, 0, sizeof( *vi ) );
}

static vkImage_t *VK_NewSlot( image_t *image ) {
	static int rover = 0;

	for ( int i = 0; i < VK_MAX_IMAGES; i++ ) {
		const int slot = ( rover + i ) % VK_MAX_IMAGES;

		if ( !vk.images[slot].owner ) {
			vkImage_t *vi = &vk.images[slot];

			rover = slot + 1;
			memset( vi, 0, sizeof( *vi ) );
			vi->owner = image;
			vi->minFilter = GL_LINEAR;
			vi->magFilter = GL_LINEAR;
			vi->wrap = GL_REPEAT;
			vi->anisotropy = 1.0f;
			image->texnum = (GLuint)slot;
			return vi;
		}
	}
	Com_Error( ERR_DROP, "VK_NewSlot: more than %i textures", VK_MAX_IMAGES );
	return NULL;
}

static void VK_CreateSlotImage( vkImage_t *vi, int width, int height, int mipLevels, vkFormat_t format ) {
	VK_CreateImage( vi, width, height, mipLevels, format );
	vi->width = width;
	vi->height = height;
	vi->mipLevels = mipLevels;
	vi->format = format;
	vi->uploaded = qfalse;
}

/*
===============
VK_LevelFormat

tr_image.cpp names a compressed format for the images that may be compressed when glConfig.textureCompression
says so (r_ext_compress_textures; lightmaps only with r_ext_compress_lightmaps, shaders with noTC never). It
asks for the one with alpha when an image has none too, unless r_texturebits is 16. The colors are stored the
same way in both, so here an image without alpha takes the format without, which is half the size.
===============
*/
static vkFormat_t VK_LevelFormat( image_t *image, int internalFormat, int width, int height, const byte *rgba ) {
	if ( !vk.bcTextures || ( internalFormat != GL_COMPRESSED_RGB_S3TC_DXT1_EXT && internalFormat != GL_COMPRESSED_RGBA_S3TC_DXT5_EXT ) ) {
		return VKF_RGBA8;
	}
	// what imagelist prints
	if ( width < 4 || height < 4 ) {
		image->internalFormat = GL_RGBA8;
		return VKF_RGBA8;
	}
	for ( int i = 0; i < width * height; i++ ) {
		if ( rgba[i * 4 + 3] != 255 ) {
			image->internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
			return VKF_BC3;
		}
	}
	image->internalFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
	return VKF_BC1;
}

/*
===============
GL_Bind

In this renderer: the image the following VK_ImageLevel() and VK_ImageParm() calls are about
===============
*/
void GL_Bind( image_t *image ) {
	vk.boundImage = image;
}

void GL_SelectTexture( int unit ) {
}

// a level whose bytes are staged goes to the device, and with the last one the texture is ready
static void VK_SendLevel( vkImage_t *vi, int level, int width, int height, vkSize_t offset ) {
	VK_UploadImageLevel( vi, level, width, height, offset );

	if ( level == vi->mipLevels - 1 ) {
		VK_EndImageUpload( vi );
		vi->uploaded = qtrue;
		VK_WriteTexture( (int)( vi - vk.images ) );
	}
}

/*
===============
VK_ImageBlocks

One mip level of the bound image that is in blocks already (vk_texcache.cpp), level 0 first: it makes the
texture with mipLevels levels.
===============
*/
void VK_ImageBlocks( int level, int mipLevels, vkFormat_t format, int width, int height, const byte *blocks, vkSize_t size ) {
	image_t *image = vk.boundImage;
	vkImage_t *vi = VK_SlotOf( image );
	vkSize_t offset;

	if ( !image ) {
		return;
	}
	if ( level == 0 ) {
		if ( vi ) {
			VK_FlushUploads();
			VK_DestroyImage( vi );
		} else {
			vi = VK_NewSlot( image );
		}
		VK_CreateSlotImage( vi, width, height, mipLevels, format );
		VK_BeginImageUpload( vi, qtrue );
	}
	if ( !vi || !vi->image || level >= vi->mipLevels ) {
		return;
	}
	memcpy( VK_Stage( size, &offset ), blocks, (size_t)size );
	VK_SendLevel( vi, level, width, height, offset );
}

/*
===============
VK_ImageLevel

One mip level of the bound image, 32 bit RGBA. Level 0 comes first and makes the texture, with all levels if
the image is mip mapped: Upload32() then sends every level down to 1 x 1. internalFormat is what OpenGL was to
store it as.
===============
*/
void VK_ImageLevel( int level, int internalFormat, int width, int height, const byte *rgba ) {
	image_t *image = vk.boundImage;
	vkImage_t *vi = VK_SlotOf( image );
	vkSize_t offset, size;

	if ( !image ) {
		return;
	}
	if ( level == 0 ) {
		const vkFormat_t format = VK_LevelFormat( image, internalFormat, width, height, rgba );
		int mipLevels = 1;

		if ( image->mipmap ) {
			for ( int w = width, h = height; w > 1 || h > 1; w >>= 1, h >>= 1 ) {
				mipLevels++;
			}
		}
		if ( vi && ( vi->width != width || vi->height != height || vi->mipLevels != mipLevels || vi->format != format ) ) {
			// new contents of another size, keep the slot and the sampler parameters
			VK_FlushUploads();
			VK_DestroyImage( vi );
		}
		if ( !vi ) {
			vi = VK_NewSlot( image );
		}
		if ( !vi->image ) {
			VK_CreateSlotImage( vi, width, height, mipLevels, format );
			VK_BeginImageUpload( vi, qtrue );
		} else {
			VK_BeginImageUpload( vi, qfalse );
		}
		vi->uploaded = qfalse;
	}
	if ( !vi || !vi->image || level >= vi->mipLevels ) {
		return;
	}

	if ( vi->format == VKF_RGBA8 ) {
		size = (vkSize_t)width * height * 4;
		memcpy( VK_Stage( size, &offset ), rgba, (size_t)size );
	} else {
		const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		byte *kept;

		size = VK_BC_Size( vi->format, width, height );
		// a texture that is kept for the next run is packed into its place there, the staging memory is not
		// for reading
		kept = VK_TexCacheLevel( level, vi->mipLevels, vi->format, width, height, size );
		ri.LoadPush( "texture packed", NULL );
		if ( kept ) {
			VK_BC_Compress( vi->format, rgba, width, height, kept );
			memcpy( VK_Stage( size, &offset ), kept, (size_t)size );
		} else {
			VK_BC_Compress( vi->format, rgba, width, height, VK_Stage( size, &offset ) );
		}
		ri.LoadPop();
		vk.statBcPixels += (double)width * height;
		vk.statBcSeconds += std::chrono::duration<double>( std::chrono::steady_clock::now() - start ).count();
	}
	VK_SendLevel( vi, level, width, height, offset );
}

/*
===============
VK_ImageParm

glTexParameter of the bound image: filters, wrap mode, anisotropy
===============
*/
void VK_ImageParm( int name, int value ) {
	vkImage_t *vi = VK_SlotOf( vk.boundImage );

	if ( !vi ) {
		return;
	}
	switch ( name ) {
	case GL_TEXTURE_MIN_FILTER:			vi->minFilter = value;			break;
	case GL_TEXTURE_MAG_FILTER:			vi->magFilter = value;			break;
	case GL_TEXTURE_WRAP_S:
	case GL_TEXTURE_WRAP_T:				vi->wrap = value;				break;
	case GL_TEXTURE_MAX_ANISOTROPY_EXT:	vi->anisotropy = (float)value;	break;
	default:							return;
	}
	if ( vi->uploaded ) {
		VK_WriteTexture( (int)( vi - vk.images ) );
	}
}

void VK_ImageDelete( unsigned int texnum ) {
	if ( texnum < VK_MAX_IMAGES && vk.images[texnum].owner ) {
		if ( vk.boundImage == vk.images[texnum].owner ) {
			vk.boundImage = NULL;
		}
		VK_FlushUploads();
		VK_FreeSlot( &vk.images[texnum] );
	}
}

/*
===============
VK_UpdateImage

New contents for an image that changes while the game runs (cinematics, scratch images)
===============
*/
void VK_UpdateImage( image_t *image, int width, int height, const byte *rgba ) {
	image_t *bound = vk.boundImage;

	vk.boundImage = image;
	image->width = width;
	image->height = height;
	VK_ImageLevel( 0, GL_RGBA8, width, height, rgba );
	vk.boundImage = bound;
}

// for gfxinfo: how many slots of the table are taken, and how many of those hold a texture in blocks
void VK_CountImages( int *numImages, int *numBlockImages ) {
	*numImages = *numBlockImages = 0;
	for ( int i = 0; i < VK_MAX_IMAGES; i++ ) {
		if ( vk.images[i].owner ) {
			( *numImages )++;
			if ( vk.images[i].format == VKF_BC1 || vk.images[i].format == VKF_BC3 ) {
				( *numBlockImages )++;
			}
		}
	}
}

void VK_InitImages( void ) {
	VK_TexCacheInit();
	VK_InitTextures();
	if ( vk.bcTextures ) {
		VK_BC_Init();
	}
}

void VK_ShutdownImages( void ) {
	VK_FinishUploads();
	VK_BC_Shutdown();
	VK_ShutdownTextures();
	memset( vk.images, 0, sizeof( vk.images ) );
}

/*
===============
RB_ShowImages

RE_EndRegistration() calls this in rd-vanilla to touch every texture, so that OpenGL has them all in memory.
Here they are on the device once they are uploaded. The tiles of r_showImages are VK_ShowImages().
===============
*/
void RB_ShowImages( void ) {
}
