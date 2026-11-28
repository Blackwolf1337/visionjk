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

// vk_prepare.cpp -- textures made on the worker threads of the engine (qcommon/com_jobs.cpp) while a level loads
//
// A texture is made in steps: the image file is read, decoded, its mip levels are made and packed into blocks, and
// they go to the device. Only the first and the last step need the main thread (the file system, the device). The
// rest, most of the time of a load with an empty texture cache, runs here on the workers, many textures at once.
//
// Two kinds:
//
// The textures of the world are loaded ahead. When the list of shaders of the level is read (tr_bsp.cpp), the images
// their texts name are looked up (R_ShaderImageFiles); every one that is neither loaded nor kept in the texture cache
// is read and goes to a worker. When the shader is made and asks R_FindImageFile() for the image, VK_PrefetchTake()
// hands over what the worker made (it waits for that one if it is not through, running it itself when it was not
// even begun), and the image is made from it at once: the shader parser reads the average colour of its images, so
// an image is whole when it is made. A file the guess misses is loaded as before, what it names wrongly is dropped
// at the end of the load. At most PREFETCH_BYTES of files wait in memory, the rest is read as workers get through.
//
// The maps the renderer makes itself (tr_matgen.cpp, R_MadeImage) are images nobody reads while the level loads:
// they are made at once without contents, their levels on a worker, and they are uploaded before the next frame
// (VK_PrepareFlush, from RE_BeginFrame), or before the image gets other pixels.
//
// What is made is what tr_image.cpp would make on the main thread: the same steps (R_MakeLevels), the same format
// (VK_FormatFor), the same file in the texture cache (VK_TexCacheWrite). Only while a level loads
// (VK_PrepareLevelBegin to VK_PrepareLevelEnd) and only with worker threads; otherwise nothing changes.

#include "vk_local.h"
#include <string>
#include <unordered_map>
#include <vector>

#define PREFETCH_BYTES	( 96 * 1024 * 1024 )	// files read ahead and not yet decoded

typedef struct prepared_s {
	// in
	char				name[MAX_QPATH];		// as asked for, the key of the prefetch
	char				file[MAX_QPATH];		// the image file the bytes are of
	void				*bytes;					// from ri.FS_ReadFile, freed on the main thread; NULL: pixels given
	int					length;
	byte				*pic;					// pixels from malloc, given or decoded
	int					width, height;
	qboolean			mipmap, allowPicmip, allowTC;
	char				cacheKey[256];			// of the texture cache, "" when it is not kept
	qboolean			dead;					// its file is not there or not one a worker takes
	jobGroup_t			*group;					// NULL: not begun yet
	image_t				*image;					// R_MadeImage: the image it is for
	// out, from the worker
	qboolean			ok;
	vec3_t				average;
	int					internalFormat;			// image_t::internalFormat
	int					uploadWidth, uploadHeight, mipLevels;
	vkFormat_t			format;
	std::vector<byte>	levels;					// one after the other
} prepared_t;

static struct {
	qboolean									loading;
	qboolean									inFrame;	// between RE_BeginFrame and RE_EndFrame: what is drawn now is
															// uploaded now
	std::unordered_map<std::string, prepared_t *>	prefetched;
	std::vector<prepared_t *>					toRead;		// prefetched, in the order they were named, file not read yet
	size_t										nextRead;
	std::vector<prepared_t *>					reading;	// begun with the bytes of their file in memory
	size_t										bytesWaiting;	// of those
	std::vector<prepared_t *>					made;		// R_MadeImage, to upload before the next frame
} prep;

/*
===============
The worker
===============
*/

static void Prep_Level( void *ctx, int level, int internalFormat, int width, int height, const byte *rgba ) {
	prepared_t	*p = (prepared_t *)ctx;
	vkSize_t	size;
	size_t		at;

	if ( level == 0 ) {
		p->format = VK_FormatFor( internalFormat, width, height, rgba, &p->internalFormat );
		p->mipLevels = 1;
		if ( p->mipmap ) {
			for ( int w = width, h = height; w > 1 || h > 1; w >>= 1, h >>= 1 ) {
				p->mipLevels++;
			}
		}
	}
	if ( level >= p->mipLevels ) {
		return;
	}
	size = ( p->format == VKF_RGBA8 ) ? (vkSize_t)width * height * 4 : VK_BC_Size( p->format, width, height );
	at = p->levels.size();
	p->levels.resize( at + (size_t)size );
	if ( p->format == VKF_RGBA8 ) {
		memcpy( p->levels.data() + at, rgba, (size_t)size );
	} else {
		VK_BC_Compress( p->format, rgba, width, height, p->levels.data() + at );
	}
}

// a job of the engine: no file system, no zone, no printing
static void Prep_Run( void *data ) {
	prepared_t *p = (prepared_t *)data;

	if ( p->bytes ) {
		int width, height;

		if ( !R_DecodeImageFile( p->file, p->bytes, p->length, &p->pic, &width, &height ) ) {
			return;
		}
		// (the header said this size, R_FindImageFile() refuses others and says so)
		if ( width != p->width || height != p->height ) {
			free( p->pic );
			p->pic = NULL;
			return;
		}
	}
	R_AverageColor( p->pic, p->width, p->height, p->average );
	R_MakeLevels( (unsigned *)p->pic, p->width, p->height, p->mipmap, p->allowPicmip, qfalse, p->allowTC, &p->internalFormat,
		&p->uploadWidth, &p->uploadHeight, Prep_Level, p );
	free( p->pic );
	p->pic = NULL;
	p->ok = (qboolean)( p->mipLevels > 0 );
}

/*
===============
The main thread
===============
*/

static void Prep_Begin( prepared_t *p ) {
	p->group = ri.JobGroupNew();
	ri.JobAdd( p->group, Prep_Run, p );
}

// the file of a prefetched image, read by the main thread, may go once the worker is through with it
static void Prep_DropBytes( prepared_t *p ) {
	if ( !p->bytes ) {
		return;
	}
	for ( size_t i = 0; i < prep.reading.size(); i++ ) {
		if ( prep.reading[i] == p ) {
			prep.reading.erase( prep.reading.begin() + i );
			break;
		}
	}
	ri.FS_FreeFile( p->bytes );
	p->bytes = NULL;
	prep.bytesWaiting -= (size_t)p->length;
}

static void Prep_Free( prepared_t *p ) {
	if ( p->group ) {
		ri.JobGroupFree( p->group );
	}
	Prep_DropBytes( p );
	free( p->pic );
	delete p;
}

// what the worker made to the device, into image
static void Prep_Upload( image_t *image, const prepared_t *p ) {
	const byte *level = p->levels.data();

	GL_Bind( image );
	for ( int i = 0; i < p->mipLevels; i++ ) {
		const int		width = Q_max( p->uploadWidth >> i, 1 ), height = Q_max( p->uploadHeight >> i, 1 );
		const vkSize_t	size = ( p->format == VKF_RGBA8 ) ? (vkSize_t)width * height * 4 : VK_BC_Size( p->format, width, height );

		VK_ImageBlocks( i, p->mipLevels, p->format, width, height, level, size );
		level += size;
	}
	image->internalFormat = p->internalFormat;
	image->width = (word)p->uploadWidth;
	image->height = (word)p->uploadHeight;
	VectorCopy( p->average, image->averageColor );
	image->averageLuma = image->averageColor[0] * 0.3f + image->averageColor[1] * 0.59f + image->averageColor[2] * 0.11f;
	R_ImageFilters( (qboolean)image->mipmap, GL_TEXTURE_2D );
	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, image->wrapClampMode );
	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, image->wrapClampMode );
	qglBindTexture( GL_TEXTURE_2D, 0 );
	glState.currenttextures[glState.currenttmu] = 0;
}

static qboolean Prep_FileSize( const char *file, const byte *b, int length, int *width, int *height );

// the file of a prefetched image read and the image to a worker; dead when it is not one a worker takes
static void Prep_Load( prepared_t *p ) {
	int width = 0, height = 0;

	if ( !VK_ReadImageFile( p->name, p->file, sizeof( p->file ), &p->bytes, &p->length )
		|| !Prep_FileSize( p->file, (const byte *)p->bytes, p->length, &width, &height )
		|| width < 1 || height < 1 || width > IMAGE_MAX_SIDE || height > IMAGE_MAX_SIDE
		|| ( width & ( width - 1 ) ) || ( height & ( height - 1 ) ) ) {
		// (R_FindImageFile() reads it again and says what is wrong with it)
		if ( p->bytes ) {
			ri.FS_FreeFile( p->bytes );
			p->bytes = NULL;
		}
		p->dead = qtrue;
		return;
	}
	p->width = width;
	p->height = height;
	prep.bytesWaiting += (size_t)p->length;
	prep.reading.push_back( p );
	Prep_Begin( p );
}

// the files of the workers that are through go, and more files are read in their order while there is room
static void Prep_Read( void ) {
	for ( size_t i = 0; i < prep.reading.size(); ) {
		prepared_t *p = prep.reading[i];

		if ( ri.JobGroupDone( p->group ) ) {
			Prep_DropBytes( p );
		} else {
			i++;
		}
	}
	while ( prep.nextRead < prep.toRead.size() && prep.bytesWaiting < PREFETCH_BYTES ) {
		prepared_t *p = prep.toRead[prep.nextRead++];

		if ( p && !p->group && !p->dead ) {
			Prep_Load( p );
		}
	}
}

qboolean VK_PrepareLoading( void ) {
	return (qboolean)( prep.loading && !prep.inFrame && ri.JobThreads() > 0 );
}

void VK_PrepareInFrame( qboolean inFrame ) {
	prep.inFrame = inFrame;
}

// RE_RegisterMedia_LevelLoadBegin: also when the renderer does not run, so nothing of it is touched here
void VK_PrepareLevelBegin( void ) {
	prep.loading = qtrue;
}

// the renderer goes (also in the middle of a load, which goes on after it started again): nothing that waits is kept
void VK_PrepareDrop( void ) {
	VK_PrepareFlush();
	for ( auto &entry : prep.prefetched ) {
		Prep_Free( entry.second );
	}
	prep.prefetched.clear();
	prep.toRead.clear();
	prep.reading.clear();
	prep.nextRead = 0;
}

// the load is over: what was made ahead and not asked for goes, the made maps go to the device
void VK_PrepareLevelEnd( void ) {
	VK_PrepareDrop();
	prep.loading = qfalse;
}

/*
===============
VK_PrefetchWorld

The images the shaders of the world name, ahead on the workers. The files are read here, as many as fit into
PREFETCH_BYTES; the rest when workers are through with some (Prep_Read).
===============
*/
typedef struct prefetchList_s {
	int count;
} prefetchList_t;

static std::string Prep_Key( const char *name, qboolean mipmap, qboolean allowPicmip, qboolean allowTC ) {
	char stripped[MAX_QPATH];

	COM_StripExtension( name, stripped, sizeof( stripped ) );
	Q_strlwr( stripped );
	return std::string( stripped ) + ( mipmap ? "+m" : "-m" ) + ( allowPicmip ? "+p" : "-p" ) + ( allowTC ? "+c" : "-c" );
}

// the width and height a header of an image file says, qfalse when it says none
static qboolean Prep_FileSize( const char *file, const byte *b, int length, int *width, int *height ) {
	const char *ext = COM_GetExtension( file );

	if ( !Q_stricmp( ext, "tga" ) && length >= 18 ) {
		*width = b[12] | ( b[13] << 8 );
		*height = b[14] | ( b[15] << 8 );
		return qtrue;
	}
	if ( !Q_stricmp( ext, "png" ) && length >= 24 && !memcmp( b + 12, "IHDR", 4 ) ) {
		*width = ( b[16] << 24 ) | ( b[17] << 16 ) | ( b[18] << 8 ) | b[19];
		*height = ( b[20] << 24 ) | ( b[21] << 16 ) | ( b[22] << 8 ) | b[23];
		return qtrue;
	}
	if ( !Q_stricmp( ext, "jpg" ) && length >= 4 && b[0] == 0xff && b[1] == 0xd8 ) {
		// the markers up to the first frame header: SOF0 to SOF15, but C4 (DHT), C8 and CC (DAC) are others
		for ( int at = 2; at + 9 <= length; ) {
			int marker, segment;

			if ( b[at] != 0xff ) {
				return qfalse;
			}
			marker = b[at + 1];
			if ( marker == 0xff ) {
				at++;
				continue;
			}
			if ( marker == 0x01 || ( marker >= 0xd0 && marker <= 0xd7 ) ) {
				at += 2;
				continue;
			}
			segment = ( b[at + 2] << 8 ) | b[at + 3];
			if ( marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc ) {
				*height = ( b[at + 5] << 8 ) | b[at + 6];
				*width = ( b[at + 7] << 8 ) | b[at + 8];
				return qtrue;
			}
			if ( segment < 2 ) {
				return qfalse;
			}
			at += 2 + segment;
		}
	}
	return qfalse;
}

// the file R_LoadImage() would read for name (its extension first, then jpg, png, tga) and its bytes, for FS_FreeFile
qboolean VK_ReadImageFile( const char *name, char *file, int fileSize, void **bytes, int *length ) {
	static const char *const kinds[] = { "jpg", "png", "tga" };
	char		stripped[MAX_QPATH];
	const char	*given = COM_GetExtension( name );

	COM_StripExtension( name, stripped, sizeof( stripped ) );
	for ( int i = -1; i < (int)ARRAY_LEN( kinds ); i++ ) {
		if ( i < 0 ) {
			if ( !given[0] ) {
				continue;
			}
			Q_strncpyz( file, name, fileSize );
		} else {
			if ( !Q_stricmp( given, kinds[i] ) ) {
				continue;
			}
			Com_sprintf( file, fileSize, "%s.%s", stripped, kinds[i] );
		}
		*length = (int)ri.FS_ReadFile( file, bytes );
		if ( *bytes && *length > 0 ) {
			return qtrue;
		}
		if ( *bytes ) {
			ri.FS_FreeFile( *bytes );
			*bytes = NULL;
		}
	}
	return qfalse;
}

static void Prep_Found( const char *name, qboolean mipmap, qboolean allowPicmip, qboolean allowTC, void *ctx ) {
	const std::string	key = Prep_Key( name, mipmap, allowPicmip, allowTC );
	prepared_t			*p;
	char				cacheKey[256] = "";

	if ( prep.prefetched.count( key ) || R_ImageLoaded( name ) ) {
		return;
	}
	// a texture the cache has is read from there when it is asked for, faster than any worker makes it
	if ( VK_TexCacheKeyFor( name, mipmap, allowPicmip, allowTC, cacheKey, sizeof( cacheKey ) ) && VK_TexCacheHas( cacheKey ) ) {
		return;
	}
	p = new prepared_t();
	Q_strncpyz( p->name, name, sizeof( p->name ) );
	p->mipmap = mipmap;
	p->allowPicmip = allowPicmip;
	p->allowTC = allowTC;
	Q_strncpyz( p->cacheKey, cacheKey, sizeof( p->cacheKey ) );
	prep.prefetched[key] = p;
	prep.toRead.push_back( p );
	( (prefetchList_t *)ctx )->count++;
}

void VK_PrefetchWorld( const dshader_t *shaders, int numShaders ) {
	prefetchList_t list = { 0 };

	if ( !VK_PrepareLoading() ) {
		return;
	}
	ri.LoadPush( "world: images read ahead", NULL );
	for ( int i = 0; i < numShaders; i++ ) {
		R_ShaderImageFiles( shaders[i].shader, qtrue, Prep_Found, &list );
	}
	Prep_Read();
	ri.LoadPop();
}

/*
===============
VK_PrefetchTake

R_FindImageFile(), after the texture cache: the image made from what a worker made for it, NULL when nothing was
made ahead for it with these settings (or the worker could not decode it: the caller loads it as before).
===============
*/
image_t *VK_PrefetchTake( const char *name, qboolean mipmap, qboolean allowPicmip, qboolean allowTC, int glWrapClampMode ) {
	image_t		*image;
	prepared_t	*p;

	if ( prep.prefetched.empty() ) {
		return NULL;
	}
	auto found = prep.prefetched.find( Prep_Key( name, mipmap, allowPicmip, allowTC ) );
	if ( found == prep.prefetched.end() ) {
		Prep_Read();
		return NULL;
	}
	p = found->second;
	prep.prefetched.erase( found );
	for ( size_t i = prep.nextRead; i < prep.toRead.size(); i++ ) {
		if ( prep.toRead[i] == p ) {
			prep.toRead[i] = NULL;
			break;
		}
	}
	if ( !p->group && !p->dead ) {
		ri.LoadPush( "image file", name );
		Prep_Load( p );
		ri.LoadPop();
	}
	if ( p->dead ) {
		Prep_Free( p );
		return NULL;
	}
	ri.LoadPush( "image made ahead", name );
	ri.JobGroupWait( p->group );
	ri.LoadPop();
	Prep_Read();
	if ( !p->ok ) {
		Prep_Free( p );
		return NULL;
	}
	image = R_NewImage( name, p->width, p->height, mipmap, allowPicmip, glWrapClampMode );
	Prep_Upload( image, p );
	if ( p->cacheKey[0] ) {
		VK_TexCacheWrite( p->cacheKey, p->format, p->uploadWidth, p->uploadHeight, p->mipLevels, p->average, p->levels.data(),
			p->levels.size() );
	}
	Prep_Free( p );
	return image;
}

// a made image to the device once its worker is through, with the file of the texture cache; p goes
static void Prep_Finish( prepared_t *p ) {
	ri.JobGroupWait( p->group );
	// (without a device, as when the renderer goes, it is dropped)
	if ( p->ok && tr.registered ) {
		Prep_Upload( p->image, p );
		if ( p->cacheKey[0] ) {
			VK_TexCacheWrite( p->cacheKey, p->format, p->uploadWidth, p->uploadHeight, p->mipLevels, p->average, p->levels.data(),
				p->levels.size() );
		}
	}
	Prep_Free( p );
}

/*
===============
VK_PrepareFile

R_FindImageFile() for the image of a shader nothing reads the pixels of while it is parsed (R_ImagesMayWait), while a
level loads: the image now with the size of its file, its texture from a worker before the next frame. NULL when it
is not one a worker takes (the caller loads it as before).
===============
*/
image_t *VK_PrepareFile( const char *name, qboolean mipmap, qboolean allowPicmip, qboolean allowTC, int glWrapClampMode ) {
	prepared_t *p;

	if ( !VK_PrepareLoading() ) {
		return NULL;
	}
	Prep_Read();
	if ( prep.bytesWaiting >= PREFETCH_BYTES ) {
		return NULL;
	}
	p = new prepared_t();
	Q_strncpyz( p->name, name, sizeof( p->name ) );
	p->mipmap = mipmap;
	p->allowPicmip = allowPicmip;
	p->allowTC = allowTC;
	if ( !VK_TexCacheKeyFor( name, mipmap, allowPicmip, allowTC, p->cacheKey, sizeof( p->cacheKey ) ) ) {
		p->cacheKey[0] = '\0';
	}
	ri.LoadPush( "image file", name );
	Prep_Load( p );
	ri.LoadPop();
	if ( p->dead ) {
		Prep_Free( p );
		return NULL;
	}
	p->image = R_NewImage( name, p->width, p->height, mipmap, allowPicmip, glWrapClampMode );
	prep.made.push_back( p );
	return p->image;
}

/*
===============
VK_PreparePixels

R_MadeImage while a level loads: new pixels (from malloc, taken) for image, uploaded before the next frame
===============
*/
void VK_PreparePixels( image_t *image, byte *pic, int width, int height ) {
	prepared_t *p = new prepared_t();

	// (newer pixels for an image that waits replace the older ones)
	VK_PrepareFinish( image );
	p->pic = pic;
	p->width = width;
	p->height = height;
	p->mipmap = (qboolean)image->mipmap;
	p->allowPicmip = (qboolean)image->allowPicmip;
	p->allowTC = qfalse;
	p->image = image;
	Prep_Begin( p );
	prep.made.push_back( p );
}

// the pixels a worker makes for image, to the device now
void VK_PrepareFinish( image_t *image ) {
	for ( size_t i = 0; i < prep.made.size(); i++ ) {
		prepared_t *p = prep.made[i];

		if ( p->image == image ) {
			prep.made.erase( prep.made.begin() + i );
			Prep_Finish( p );
			return;
		}
	}
}

// before a frame, before the end of a load and before the renderer goes: every made map to the device
void VK_PrepareFlush( void ) {
	if ( prep.made.empty() ) {
		return;
	}
	ri.LoadPush( "made images", NULL );
	// (in their order: the same image may come twice)
	for ( prepared_t *p : prep.made ) {
		Prep_Finish( p );
	}
	prep.made.clear();
	ri.LoadPop();
}
