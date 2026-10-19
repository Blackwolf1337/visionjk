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

// snd_cache.cpp -- the samples of MP3 files that were unpacked, kept in the profile
//
// An MP3 outside sound/chars is unpacked to samples when it is registered (snd_mem.cpp): weapons, effects, the
// sounds of the players. That was most of the sounds of a level, 182 of 256 ms when a server with 29 players
// was joined, and it comes out the same every time. So the samples are kept, a file per sound, in soundcache/
// of the profile, and the next time they are read instead of unpacked.
//
// As the textures of rd-vulkan-vision (vk_texcache.cpp): a file is named after what it was made from, the path
// of the MP3, its length and the checksum of the pak it is in (FS_FileSource()). Another pak or another length is
// another name, an old file is merely left behind. An MP3 of a directory is never kept. The files end in .dat,
// which the engine lets be read from a directory on a pure server. The samples are the ones of the unpacker,
// before they are brought to the rate of the mixer, so s_khz is not in the name.

#include "snd_local.h"

#define SNDCACHE_MAGIC		0x31444E53		// "SND1"
#define SNDCACHE_VERSION	1				// of the file: another one, another name
#define SNDCACHE_KEY		256

typedef struct sndCacheHeader_s {
	int		magic;
	int		rate, width, samples;
	int		keyLength;				// the key follows, then samples * width bytes
} sndCacheHeader_t;

static struct {
	char		key[SNDCACHE_KEY];
	qboolean	pending;			// key is of a sound that was not found, for S_SoundCacheKeep()
} sndCache;

static cvar_t *v_soundCache;

void S_SoundCacheInit( void ) {
	v_soundCache = Cvar_Get( "v_soundCache", "1", CVAR_ARCHIVE_ND, "Sounds that were unpacked from MP3 files are kept in soundcache/ of the profile and not unpacked again. 0: every one is unpacked" );
}

// FNV-1a, 64 bit
static unsigned long long S_SoundCacheHash( const char *text ) {
	unsigned long long hash = 14695981039346656037ULL;

	for ( ; *text; text++ ) {
		hash = ( hash ^ (byte)*text ) * 1099511628211ULL;
	}
	return hash;
}

static void S_SoundCachePath( char *path, int pathSize ) {
	Com_sprintf( path, pathSize, "soundcache/%016llx.dat", S_SoundCacheHash( sndCache.key ) );
}

/*
===============
S_SoundCacheFind

file is the MP3 that was read. Found: the samples, info as MP3_FakeUpWAVInfo() fills it, and *kept the file to
free with FS_FreeFile() after them. NULL: unpack it, and hand the samples to S_SoundCacheKeep().
===============
*/
const byte *S_SoundCacheFind( const char *file, wavinfo_t *info, void **kept ) {
	sndCacheHeader_t	header;
	char				path[MAX_QPATH];
	int					length, checksum;
	long				size;
	void				*buffer;

	*kept = NULL;
	sndCache.pending = qfalse;
	if ( !v_soundCache || !v_soundCache->integer || FS_FileSource( file, &length, &checksum ) != FS_SOURCE_PAK ) {
		return NULL;
	}
	Com_sprintf( sndCache.key, sizeof( sndCache.key ), "%s %i %08x v%i", file, length, checksum, SNDCACHE_VERSION );
	sndCache.pending = qtrue;

	S_SoundCachePath( path, sizeof( path ) );
	Com_LoadPush( "sound kept", file );
	size = FS_ReadFile( path, &buffer );
	Com_LoadPop();
	if ( size <= 0 || !buffer ) {
		return NULL;
	}

	// a file that is cut off, of another version or of another key with the same hash is not this sound
	const int keyLength = (int)strlen( sndCache.key );
	if ( size >= (long)sizeof( header ) ) {
		memcpy( &header, buffer, sizeof( header ) );
		if ( header.magic == SNDCACHE_MAGIC && header.keyLength == keyLength && ( header.width == 1 || header.width == 2 )
			&& header.rate > 0 && header.samples > 0
			&& size == (long)sizeof( header ) + keyLength + (long)header.samples * header.width
			&& !memcmp( (const byte *)buffer + sizeof( header ), sndCache.key, keyLength ) ) {
			Com_Memset( info, 0, sizeof( *info ) );
			info->format = WAV_FORMAT_MP3;
			info->rate = header.rate;
			info->width = header.width;
			info->channels = 1;
			info->samples = header.samples;
			info->dataofs = 0;
			sndCache.pending = qfalse;
			*kept = buffer;
			return (const byte *)buffer + sizeof( header ) + keyLength;
		}
	}
	FS_FreeFile( buffer );
	return NULL;
}

/*
===============
S_SoundCacheKeep

After S_SoundCacheFind() found nothing: the samples the unpacker made, bytes of them in data.
===============
*/
void S_SoundCacheKeep( const wavinfo_t *info, const byte *data, int bytes ) {
	sndCacheHeader_t	header;
	char				path[MAX_QPATH];
	fileHandle_t		f;

	if ( !sndCache.pending ) {
		return;
	}
	sndCache.pending = qfalse;
	if ( ( info->width != 1 && info->width != 2 ) || info->rate <= 0 || info->samples <= 0
		|| (long)info->samples * info->width > (long)bytes - info->dataofs ) {
		return;
	}

	header.magic = SNDCACHE_MAGIC;
	header.rate = info->rate;
	header.width = info->width;
	header.samples = info->samples;
	header.keyLength = (int)strlen( sndCache.key );

	S_SoundCachePath( path, sizeof( path ) );
	Com_LoadPush( "sound kept: written", NULL );
	f = FS_FOpenFileWrite( path );
	if ( f ) {
		FS_Write( &header, sizeof( header ), f );
		FS_Write( sndCache.key, header.keyLength, f );
		FS_Write( data + info->dataofs, info->samples * info->width, f );
		FS_FCloseFile( f );
	}
	Com_LoadPop();
}
