/*
===========================================================================
Copyright (C) 2026, VisionJK contributors

This file is part of the VisionJK source code.

VisionJK is free software; you can redistribute it and/or modify it
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

// vk_texcache.cpp -- the textures the renderer has packed into blocks, kept in the profile
//
// An image file is read, decoded, scaled, given its mip levels and packed into blocks (vk_bc.cpp) every time a
// level asks for it for the first time since the program started: for mp/ffa3 that is half of the load, and
// for a player who joins in the middle of a game most of the frame his skin is loaded in. What comes out of it
// only depends on the file and on a few settings. So it is kept, a file per texture, in vulkan/tex/ of the
// profile, and the next time the blocks are read and handed to the device as they are.
//
// A file of the cache is named after what it was made from: the path of the image file, its length and the
// checksum of the pak it is in (FS_FileSource() of the engine), and the settings its contents depend on. Another
// pak, another length or another setting is another name, so nothing has to be compared or thrown away, and an
// old file is merely left behind. An image file of a directory is never kept: it can change while the name of
// what was made from it stays the same.
//
// The files end in .dat: of the files of a directory, the engine lets only a few kinds be read while the game
// is on a pure server, and that is one of them.
//
// How tr_image.cpp uses it, in R_FindImageFile() and R_CreateImage():
//   VK_TexCacheFind()    before the file is read. Found: the size of the texture, and R_CreateImage() is called
//                        without pixels.
//   VK_TexCacheUpload()  at the start of Upload32(): the texture that was found goes to the device, and Upload32()
//                        only sets the filters.
//   VK_TexCacheRecord()  before R_CreateImage() when it was not found: the levels vk_image.cpp packs from here
//                        on are kept (VK_TexCacheLevel()).
//   VK_TexCacheEnd()     after R_CreateImage(): what was kept is written.

#include "vk_local.h"
#include <vector>

#define TEXCACHE_MAGIC		0x31585456		// "VTX1"
#define TEXCACHE_VERSION	1				// of the file and of what the packer makes: another one, another name
#define TEXCACHE_MAX_LEVELS	16
#define TEXCACHE_KEY		256

typedef struct texCacheHeader_s {
	int		magic;
	int		width, height;			// of level 0
	int		format;					// VKF_BC1 or VKF_BC3
	int		mipLevels;
	int		keyLength;				// the key follows, then the levels one after the other
} texCacheHeader_t;

static struct {
	char				key[TEXCACHE_KEY];
	qboolean			pending;	// key is of an image that was not found, for VK_TexCacheRecord()

	void				*file;		// found: the file as read, until VK_TexCacheEnd()
	const byte			*levels;
	texCacheHeader_t	header;

	qboolean			recording;
	int					levelsKept;
	texCacheHeader_t	recorded;
	std::vector<byte>	blocks;		// the levels packed so far
} texCache;

static cvar_t *r_vkTexCache;

void VK_TexCacheInit( void ) {
	r_vkTexCache = ri.Cvar_Get( "r_vkTexCache", "1", CVAR_ARCHIVE_ND, "Textures that were packed into blocks are kept in vulkan/tex/ of the profile and not made again. 0: every one is made from its image file" );
}

// FNV-1a, 64 bit
static unsigned long long VK_TexCacheHash( const char *text ) {
	unsigned long long hash = 14695981039346656037ULL;

	for ( ; *text; text++ ) {
		hash = ( hash ^ (byte)*text ) * 1099511628211ULL;
	}
	return hash;
}

static void VK_TexCachePath( char *path, int pathSize ) {
	Com_sprintf( path, pathSize, "vulkan/tex/%016llx.dat", VK_TexCacheHash( texCache.key ) );
}

/*
===============
VK_TexCacheKey

Which file R_LoadImage() would take for the name, as it looks for it: the extension of the name first, then
the kinds it knows in its order. qfalse when that file is of a directory or there is none, else the key of
what is made from it.

The settings in the key are the ones Upload32() and VK_LevelFormat() look at.
===============
*/
static qboolean VK_TexCacheKey( const char *name, qboolean mipmap, qboolean allowPicmip ) {
	static const char *const kinds[] = { "jpg", "png", "tga" };
	char		stripped[MAX_QPATH], file[MAX_QPATH];
	const char	*given = COM_GetExtension( name );
	int			length = 0, checksum = 0, first = -1;

	COM_StripExtension( name, stripped, sizeof( stripped ) );
	for ( int i = 0; i < (int)ARRAY_LEN( kinds ); i++ ) {
		if ( !Q_stricmp( given, kinds[i] ) ) {
			first = i;
		}
	}
	for ( int i = -1; i < (int)ARRAY_LEN( kinds ); i++ ) {
		const int kind = ( i < 0 ) ? first : i;
		int source;

		if ( kind < 0 || ( i >= 0 && kind == first ) ) {
			continue;
		}
		Com_sprintf( file, sizeof( file ), "%s.%s", stripped, kinds[kind] );
		source = ri.FS_FileSource( file, &length, &checksum );
		if ( source == FS_SOURCE_NONE ) {
			continue;
		}
		if ( source != FS_SOURCE_PAK ) {
			return qfalse;
		}
		Q_strlwr( file );
		Com_sprintf( texCache.key, sizeof( texCache.key ), "%s %i %08x v%i m%i p%i s%i c%i t%i x%i o%i i%g g%g", file, length,
			(unsigned)checksum, TEXCACHE_VERSION, mipmap ? 1 : 0, allowPicmip ? r_picmip->integer : 0,
			r_simpleMipMaps->integer, r_colorMipLevels->integer, r_texturebits->integer, glConfig.maxTextureSize, tr.overbrightBits,
			r_intensity->value, glConfig.deviceSupportsGamma ? 0.0f : r_gamma->value );
		return qtrue;
	}
	return qfalse;
}

// the blocks of a level of a texture of this size
static vkSize_t VK_TexCacheLevelSize( const texCacheHeader_t *header, int level ) {
	return VK_BC_Size( (vkFormat_t)header->format, Q_max( header->width >> level, 1 ), Q_max( header->height >> level, 1 ) );
}

/*
===============
VK_TexCacheFind

qtrue: the texture of this image file is kept. width and height are its size, and VK_TexCacheUpload() hands it to
the device.
===============
*/
qboolean VK_TexCacheFind( const char *name, qboolean mipmap, qboolean allowPicmip, qboolean allowTC, int *width, int *height ) {
	char		path[MAX_QPATH];
	long		length;
	vkSize_t	need;

	texCache.pending = qfalse;
	if ( !vk.bcTextures || !allowTC || !r_vkTexCache || !r_vkTexCache->integer || !VK_TexCacheKey( name, mipmap, allowPicmip ) ) {
		return qfalse;
	}
	texCache.pending = qtrue;

	VK_TexCachePath( path, sizeof( path ) );
	ri.LoadPush( "texture kept", name );
	length = ri.FS_ReadFile( path, &texCache.file );
	ri.LoadPop();
	if ( length <= 0 || !texCache.file ) {
		texCache.file = NULL;
		return qfalse;
	}

	// a file that is cut off, of another version or of another key with the same hash is not this texture
	need = sizeof( texCache.header );
	if ( (vkSize_t)length >= need ) {
		const int keyLength = (int)strlen( texCache.key );

		memcpy( &texCache.header, texCache.file, sizeof( texCache.header ) );
		if ( texCache.header.magic == TEXCACHE_MAGIC && texCache.header.keyLength == keyLength
			&& ( texCache.header.format == VKF_BC1 || texCache.header.format == VKF_BC3 )
			&& texCache.header.mipLevels > 0 && texCache.header.mipLevels <= TEXCACHE_MAX_LEVELS
			&& texCache.header.width >= 4 && texCache.header.height >= 4
			&& (vkSize_t)length >= need + keyLength
			&& !memcmp( (const byte *)texCache.file + need, texCache.key, keyLength ) ) {
			need += keyLength;
			texCache.levels = (const byte *)texCache.file + need;
			for ( int i = 0; i < texCache.header.mipLevels; i++ ) {
				need += VK_TexCacheLevelSize( &texCache.header, i );
			}
			if ( (vkSize_t)length == need ) {
				texCache.pending = qfalse;
				*width = texCache.header.width;
				*height = texCache.header.height;
				return qtrue;
			}
		}
	}
	ri.FS_FreeFile( texCache.file );
	texCache.file = NULL;
	return qfalse;
}

/*
===============
VK_TexCacheUpload

At the start of Upload32(), with the image bound. qfalse: the texture was not found, Upload32() has to make it
from the pixels. qtrue: it is on its way to the device, with what Upload32() and VK_LevelFormat() say of an image:
the format and the size of what was uploaded. The filters are left to Upload32().
===============
*/
qboolean VK_TexCacheUpload( int *internalFormat, int *width, int *height ) {
	const byte *level = texCache.levels;

	if ( !texCache.file ) {
		return qfalse;
	}
	*width = texCache.header.width;
	*height = texCache.header.height;
	*internalFormat = ( texCache.header.format == VKF_BC1 ) ? GL_COMPRESSED_RGB_S3TC_DXT1_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
	for ( int i = 0; i < texCache.header.mipLevels; i++ ) {
		const vkSize_t size = VK_TexCacheLevelSize( &texCache.header, i );

		VK_ImageBlocks( i, texCache.header.mipLevels, (vkFormat_t)texCache.header.format, Q_max( texCache.header.width >> i, 1 ),
			Q_max( texCache.header.height >> i, 1 ), level, size );
		level += size;
	}
	return qtrue;
}

// the levels of the image that R_CreateImage() makes next are kept, if it is one VK_TexCacheFind() did not find
void VK_TexCacheRecord( void ) {
	if ( !texCache.pending ) {
		return;
	}
	texCache.recording = qtrue;
	texCache.levelsKept = 0;
	texCache.blocks.clear();
}

/*
===============
VK_TexCacheLevel

For vk_image.cpp: room for the blocks of a level it is about to pack, NULL when nothing is kept. Level 0 comes
first. A texture that is not in blocks (too small, or the device takes none) is not kept.
===============
*/
byte *VK_TexCacheLevel( int level, int mipLevels, vkFormat_t format, int width, int height, vkSize_t size ) {
	size_t at;

	if ( !texCache.recording ) {
		return NULL;
	}
	if ( ( format != VKF_BC1 && format != VKF_BC3 ) || level != texCache.levelsKept || mipLevels > TEXCACHE_MAX_LEVELS
		|| ( level == 0 && ( width < 4 || height < 4 ) ) ) {
		texCache.recording = qfalse;
		return NULL;
	}
	if ( level == 0 ) {
		texCache.recorded.magic = TEXCACHE_MAGIC;
		texCache.recorded.width = width;
		texCache.recorded.height = height;
		texCache.recorded.format = format;
		texCache.recorded.mipLevels = mipLevels;
		texCache.recorded.keyLength = (int)strlen( texCache.key );
	} else if ( format != texCache.recorded.format || size != VK_TexCacheLevelSize( &texCache.recorded, level ) ) {
		texCache.recording = qfalse;
		return NULL;
	}
	at = texCache.blocks.size();
	texCache.blocks.resize( at + (size_t)size );
	texCache.levelsKept++;
	return texCache.blocks.data() + at;
}

void VK_TexCacheEnd( void ) {
	if ( texCache.file ) {
		ri.FS_FreeFile( texCache.file );
		texCache.file = NULL;
	}
	if ( texCache.recording && texCache.levelsKept == texCache.recorded.mipLevels && texCache.levelsKept > 0 ) {
		char path[MAX_QPATH];
		std::vector<byte> out( sizeof( texCache.recorded ) + texCache.recorded.keyLength + texCache.blocks.size() );

		memcpy( out.data(), &texCache.recorded, sizeof( texCache.recorded ) );
		memcpy( out.data() + sizeof( texCache.recorded ), texCache.key, texCache.recorded.keyLength );
		memcpy( out.data() + sizeof( texCache.recorded ) + texCache.recorded.keyLength, texCache.blocks.data(), texCache.blocks.size() );
		VK_TexCachePath( path, sizeof( path ) );
		ri.LoadPush( "texture kept: written", NULL );
		ri.FS_WriteFile( path, out.data(), (int)out.size() );
		ri.LoadPop();
	}
	texCache.recording = texCache.pending = qfalse;
}
