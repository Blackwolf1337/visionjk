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

// cl_cache.cpp -- the one folder of everything the client makes again when it is gone: <fs_homepath>/cache/, a folder
// a kind (cacheKinds), shared by all mod folders (the keys of the files carry the CRC of the file they were made
// from). Until 2026-11-14 each kind wrote into the mod folder (vulkan/tex, vulkan/gen, vulkan/pipelines*.dat,
// soundcache, steamaudio), without a limit: Cache_Init() moves those over once. v_cacheMaxMB keeps the folder under
// a size, the files that were not used for the longest time go first. The window "Clear Caches" of Setup
// (ui/ui_cacheclean.c) and the commands v_cacheList and v_cacheClear empty a kind. Not from JA+.
//
// Everything here runs on the main thread. The files are read and written with the C library directly: the file
// system of the game only knows the mod folders.

#include "client.h"
#include "cl_cache.h"

#include <sys/stat.h>
#include <time.h>
#include <algorithm>
#include <vector>
#ifdef _WIN32
#include <sys/utime.h>
#include <direct.h>
#define cache_utime( path )	_utime( path, NULL )
#define cache_rmdir( path )	_rmdir( path )
#else
#include <utime.h>
#include <unistd.h>
#define cache_utime( path )	utime( path, NULL )
#define cache_rmdir( path )	rmdir( path )
#endif

// files.cpp: makes the folders a file path goes through
qboolean FS_CreatePath( char *OSPath );

#define CACHE_FOLDER		FS_DATAFOLDER "/cache"
#define CACHE_KEYS_FILE		"keys.txt"
// how the writers name their files: when it changes, what was written under the names of before is never read again
// (until 2026-11-28 the keys had the checksum of the whole pak instead of the CRC of the file)
#define CACHE_KEYS			"file-crc"
#define CACHE_TOUCH_SECONDS	( 24 * 60 * 60 )	// a file that is read gets the time of now when its time is older than this

typedef struct cacheKind_s {
	const char	*id;		// the folder under cache/ and the word of v_cacheClear
	const char	*label;		// for the window, in English (the ui translates it)
	qboolean	limited;	// v_cacheMaxMB removes its files
} cacheKind_t;

static const cacheKind_t cacheKinds[] = {
	{ "pipelines",	"Graphics Pipelines",		qfalse },	// two small files, and without them every pipeline is compiled again
	{ "textures",	"Textures",					qtrue },
	{ "materials",	"Automatic Materials",		qtrue },
	{ "sound",		"Sounds",					qtrue },
	{ "steamaudio",	"Sound Paths of Levels",	qtrue },
};
#define CACHE_KINDS		( (int)ARRAY_LEN( cacheKinds ) )

typedef struct cacheFile_s {
	char		path[MAX_OSPATH];
	time_t		time;
	long long	bytes;
} cacheFile_t;

static cvar_t	*v_cacheMaxMB;
// a kind that was cleared is not written again until the program ends, where the writer would only put back what
// it has in memory (the pipelines at the end of the renderer)
static qboolean	cacheBlocked[CACHE_KINDS];

// the folder of a kind, or with name a file in it
static void Cache_OSPath( const char *name, char *path, int pathSize ) {
	Q_strncpyz( path, FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), CACHE_FOLDER, name ), pathSize );
}

// "textures/0123.dat": which kind, -1 for a name that is none of ours
static int Cache_KindOf( const char *name ) {
	if ( strstr( name, ".." ) || strchr( name, '\\' ) || strchr( name, ':' ) || name[0] == '/' ) {
		return -1;
	}
	for ( int k = 0; k < CACHE_KINDS; k++ ) {
		const size_t length = strlen( cacheKinds[k].id );

		if ( !Q_stricmpn( name, cacheKinds[k].id, (int)length ) && name[length] == '/' && name[length + 1] ) {
			return k;
		}
	}
	return -1;
}

static int Cache_KindNamed( const char *id ) {
	for ( int k = 0; k < CACHE_KINDS; k++ ) {
		if ( !Q_stricmp( id, cacheKinds[k].id ) ) {
			return k;
		}
	}
	return -1;
}

/*
===============
Cache_Read

name: "<kind>/<file>". The whole file in a buffer of the zone, to free with FS_FreeFile() (ri.FS_FreeFile), and its
length; -1 and NULL when it is not there. A file that is read counts as used (v_cacheMaxMB).
===============
*/
long Cache_Read( const char *name, void **buffer ) {
	char		path[MAX_OSPATH];
	struct stat	st;
	FILE		*f;
	byte		*data;

	*buffer = NULL;
	if ( Cache_KindOf( name ) < 0 ) {
		return -1;
	}
	Cache_OSPath( name, path, sizeof( path ) );
	f = fopen( path, "rb" );
	if ( !f ) {
		return -1;
	}
	// (Z_Malloc takes an int)
	if ( stat( path, &st ) || st.st_size <= 0 || st.st_size >= 0x7fffffff ) {
		fclose( f );
		return -1;
	}
	data = (byte *)Z_Malloc( (int)st.st_size + 1, TAG_FILESYS, qfalse );
	if ( fread( data, 1, (size_t)st.st_size, f ) != (size_t)st.st_size ) {
		fclose( f );
		Z_Free( data );
		return -1;
	}
	fclose( f );
	data[st.st_size] = 0;
	if ( time( NULL ) - st.st_mtime > CACHE_TOUCH_SECONDS ) {
		cache_utime( path );
	}
	*buffer = data;
	return (long)st.st_size;
}

// name: "<kind>/<file>". Whether it is there, without reading it or counting it as used.
qboolean Cache_Exists( const char *name ) {
	char		path[MAX_OSPATH];
	struct stat	st;

	if ( Cache_KindOf( name ) < 0 ) {
		return qfalse;
	}
	Cache_OSPath( name, path, sizeof( path ) );
	return (qboolean)( !stat( path, &st ) && ( st.st_mode & S_IFREG ) );
}

/*
===============
Cache_Write

name: "<kind>/<file>". Written next to its place and renamed, so a file is whole or not there.
===============
*/
qboolean Cache_Write( const char *name, const void *data, int size ) {
	char		path[MAX_OSPATH], temp[MAX_OSPATH];
	const int	kind = Cache_KindOf( name );
	FILE		*f;

	if ( kind < 0 || cacheBlocked[kind] || !data || size <= 0 ) {
		return qfalse;
	}
	Cache_OSPath( name, path, sizeof( path ) );
	Com_sprintf( temp, sizeof( temp ), "%s.tmp", path );
	FS_CreatePath( temp );
	f = fopen( temp, "wb" );
	if ( !f ) {
		return qfalse;
	}
	if ( fwrite( data, 1, (size_t)size, f ) != (size_t)size ) {
		fclose( f );
		remove( temp );
		return qfalse;
	}
	fclose( f );
	// (rename of Windows does not replace a file)
	remove( path );
	if ( rename( temp, path ) ) {
		remove( temp );
		return qfalse;
	}
	return qtrue;
}

// The files of a kind, into files if given. Files a write left half done (.tmp, the program ended in it) go.
static void Cache_Scan( int kind, std::vector<cacheFile_t> *files, long long *bytes, int *count ) {
	char	folder[MAX_OSPATH];
	char	**names;
	int		numNames = 0;

	*bytes = 0;
	*count = 0;
	Cache_OSPath( cacheKinds[kind].id, folder, sizeof( folder ) );
	names = Sys_ListFiles( folder, "", NULL, &numNames, qfalse );
	for ( int i = 0; i < numNames; i++ ) {
		cacheFile_t	file;
		struct stat	st;
		const char	*ext = strrchr( names[i], '.' );

		Com_sprintf( file.path, sizeof( file.path ), "%s/%s", folder, names[i] );
		if ( stat( file.path, &st ) || !( st.st_mode & S_IFREG ) ) {
			continue;
		}
		if ( ext && !Q_stricmp( ext, ".tmp" ) ) {
			remove( file.path );
			continue;
		}
		file.time = st.st_mtime;
		file.bytes = (long long)st.st_size;
		*bytes += file.bytes;
		( *count )++;
		if ( files ) {
			files->push_back( file );
		}
	}
	Sys_FreeFileList( names );
}

/*
===============
Cache_List

For the window of the ui (ext.Cache_List) and v_cacheList: one "id\tlabel\tbytes\tfiles\n" a kind. Returns the
length, cut at a whole line where buf has no more room.
===============
*/
int Cache_List( char *buf, int size ) {
	int length = 0;

	if ( size > 0 ) {
		buf[0] = '\0';
	}
	for ( int k = 0; k < CACHE_KINDS; k++ ) {
		char		line[256];
		long long	bytes;
		int			count, lineLength;

		Cache_Scan( k, NULL, &bytes, &count );
		Com_sprintf( line, sizeof( line ), "%s\t%s\t%lld\t%i\n", cacheKinds[k].id, cacheKinds[k].label, bytes, count );
		lineLength = (int)strlen( line );
		if ( length + lineLength >= size ) {
			break;
		}
		memcpy( buf + length, line, lineLength + 1 );
		length += lineLength;
	}
	return length;
}

/*
===============
Cache_Clear

id: a kind or "all". What is in memory stays until it is loaded again; the kind is not written again before the
program ends when its writer would only put back what it holds (the pipelines). qfalse: no such kind.
===============
*/
qboolean Cache_Clear( const char *id ) {
	const qboolean	all = (qboolean)!Q_stricmp( id, "all" );
	const int		only = Cache_KindNamed( id );

	if ( !all && only < 0 ) {
		return qfalse;
	}
	for ( int k = 0; k < CACHE_KINDS; k++ ) {
		std::vector<cacheFile_t>	files;
		long long					bytes;
		int							count;

		if ( !all && k != only ) {
			continue;
		}
		Cache_Scan( k, &files, &bytes, &count );
		for ( const cacheFile_t &file : files ) {
			remove( file.path );
		}
		if ( !cacheKinds[k].limited ) {
			cacheBlocked[k] = qtrue;
		}
		Com_Printf( "%s: %i files, %.1f MB removed\n", cacheKinds[k].id, count, bytes / ( 1024.0 * 1024.0 ) );
	}
	return qtrue;
}

// Once after a change of CACHE_KEYS: the kinds named after their source file are emptied, nothing would find them
// again and v_cacheMaxMB would only take them at 4 GB.
static void Cache_Rekey( void ) {
	static const char *const keyed[] = { "textures", "materials", "sound" };
	char	path[MAX_OSPATH], have[32] = "";
	FILE	*f;

	Cache_OSPath( CACHE_KEYS_FILE, path, sizeof( path ) );
	f = fopen( path, "rb" );
	if ( f ) {
		const size_t got = fread( have, 1, sizeof( have ) - 1, f );

		have[got] = '\0';
		fclose( f );
	}
	if ( !strcmp( have, CACHE_KEYS ) ) {
		return;
	}
	for ( int i = 0; i < (int)ARRAY_LEN( keyed ); i++ ) {
		long long	bytes;
		int			count;

		Cache_Scan( Cache_KindNamed( keyed[i] ), NULL, &bytes, &count );
		if ( count ) {
			Com_Printf( "cache: the files are named another way now, " );
			Cache_Clear( keyed[i] );
		}
	}
	FS_CreatePath( path );
	f = fopen( path, "wb" );
	if ( f ) {
		fputs( CACHE_KEYS, f );
		fclose( f );
	}
}

// v_cacheMaxMB: the files that were not used for the longest time go until the folder is under it
static void Cache_Limit( void ) {
	std::vector<cacheFile_t>	files;
	const long long				most = (long long)v_cacheMaxMB->integer * 1024 * 1024;
	long long					total = 0, removed = 0;
	int							numRemoved = 0;

	if ( v_cacheMaxMB->integer <= 0 ) {
		return;
	}
	for ( int k = 0; k < CACHE_KINDS; k++ ) {
		long long	bytes;
		int			count;

		Cache_Scan( k, cacheKinds[k].limited ? &files : NULL, &bytes, &count );
		total += bytes;
	}
	if ( total <= most ) {
		return;
	}
	std::sort( files.begin(), files.end(), []( const cacheFile_t &a, const cacheFile_t &b ) { return a.time < b.time; } );
	for ( const cacheFile_t &file : files ) {
		if ( total <= most ) {
			break;
		}
		if ( !remove( file.path ) ) {
			total -= file.bytes;
			removed += file.bytes;
			numRemoved++;
		}
	}
	Com_Printf( "cache: over v_cacheMaxMB %i, the %i files unused for the longest time removed (%.1f MB)\n", v_cacheMaxMB->integer,
		numRemoved, removed / ( 1024.0 * 1024.0 ) );
}

// The files of folder (of a mod folder, as the versions before wrote them) with this extension, and prefix if given,
// into a kind. Where both have the file the newer one stays. Returns how many moved.
static int Cache_MoveFolder( const char *folder, const char *extension, const char *prefix, int kind ) {
	char	**names;
	int		numNames = 0, moved = 0;

	names = Sys_ListFiles( folder, extension, NULL, &numNames, qfalse );
	for ( int i = 0; i < numNames; i++ ) {
		char		from[MAX_OSPATH], to[MAX_OSPATH];
		struct stat	stFrom, stTo;

		if ( prefix && Q_stricmpn( names[i], prefix, (int)strlen( prefix ) ) ) {
			continue;
		}
		Com_sprintf( from, sizeof( from ), "%s/%s", folder, names[i] );
		Cache_OSPath( va( "%s/%s", cacheKinds[kind].id, names[i] ), to, sizeof( to ) );
		if ( stat( from, &stFrom ) || !( stFrom.st_mode & S_IFREG ) ) {
			continue;
		}
		if ( !stat( to, &stTo ) ) {
			if ( stTo.st_mtime >= stFrom.st_mtime ) {
				remove( from );
				continue;
			}
			remove( to );
		}
		FS_CreatePath( to );
		if ( !rename( from, to ) ) {
			moved++;
		}
	}
	Sys_FreeFileList( names );
	return moved;
}

// Once for each mod folder of the profile that still has the folders of before 2026-11-14. Only those folders and
// only the files the caches wrote there; the folders go when they are empty (rmdir leaves one with anything in it).
static void Cache_MoveOld( void ) {
	const char	*home = Cvar_VariableString( "fs_homepath" );
	char		**mods;
	int			numMods = 0, moved = 0;

	mods = Sys_ListFiles( home, "/", NULL, &numMods, qfalse );
	for ( int i = 0; i < numMods; i++ ) {
		char folder[MAX_OSPATH];

		if ( !Q_stricmp( mods[i], "." ) || !Q_stricmp( mods[i], ".." ) || !Q_stricmp( mods[i], FS_DATAFOLDER ) ) {
			continue;
		}
		Com_sprintf( folder, sizeof( folder ), "%s/%s/vulkan/tex", home, mods[i] );
		moved += Cache_MoveFolder( folder, ".dat", NULL, Cache_KindNamed( "textures" ) );
		cache_rmdir( folder );
		Com_sprintf( folder, sizeof( folder ), "%s/%s/vulkan/gen", home, mods[i] );
		moved += Cache_MoveFolder( folder, ".dat", NULL, Cache_KindNamed( "materials" ) );
		cache_rmdir( folder );
		Com_sprintf( folder, sizeof( folder ), "%s/%s/vulkan", home, mods[i] );
		moved += Cache_MoveFolder( folder, ".dat", "pipelines", Cache_KindNamed( "pipelines" ) );
		cache_rmdir( folder );
		Com_sprintf( folder, sizeof( folder ), "%s/%s/soundcache", home, mods[i] );
		moved += Cache_MoveFolder( folder, ".dat", NULL, Cache_KindNamed( "sound" ) );
		cache_rmdir( folder );
		// (materials.txt and what the debug commands write stay there)
		Com_sprintf( folder, sizeof( folder ), "%s/%s/steamaudio", home, mods[i] );
		moved += Cache_MoveFolder( folder, ".paths", NULL, Cache_KindNamed( "steamaudio" ) );
		moved += Cache_MoveFolder( folder, ".echo", NULL, Cache_KindNamed( "steamaudio" ) );
		cache_rmdir( folder );
	}
	Sys_FreeFileList( mods );
	if ( moved ) {
		Com_Printf( "cache: %i files of the mod folders moved into %s/%s\n", moved, home, CACHE_FOLDER );
	}
}

static void Cache_List_f( void ) {
	long long total = 0;

	for ( int k = 0; k < CACHE_KINDS; k++ ) {
		long long	bytes;
		int			count;

		Cache_Scan( k, NULL, &bytes, &count );
		total += bytes;
		Com_Printf( "%-12s %8.1f MB  %5i files  %s\n", cacheKinds[k].id, bytes / ( 1024.0 * 1024.0 ), count, cacheKinds[k].label );
	}
	Com_Printf( "%.1f MB in %s/%s, v_cacheMaxMB %i\n", total / ( 1024.0 * 1024.0 ), Cvar_VariableString( "fs_homepath" ), CACHE_FOLDER,
		v_cacheMaxMB->integer );
}

static void Cache_Clear_f( void ) {
	if ( Cmd_Argc() != 2 || !Cache_Clear( Cmd_Argv( 1 ) ) ) {
		Com_Printf( "usage: v_cacheClear <kind|all>, the kinds:" );
		for ( int k = 0; k < CACHE_KINDS; k++ ) {
			Com_Printf( " %s", cacheKinds[k].id );
		}
		Com_Printf( "\n" );
	}
}

// before the renderer and the sound start: they read from the folder
void Cache_Init( void ) {
	v_cacheMaxMB = Cvar_Get( "v_cacheMaxMB", "4096", CVAR_ARCHIVE_ND, "The folder cache of the profile (textures, materials, sounds, sound paths) is kept under this many megabytes: at the start the files unused for the longest time go. 0: no limit" );
	Cmd_AddCommand( "v_cacheList", Cache_List_f, "How large each kind of the cache folder is" );
	Cmd_AddCommand( "v_cacheClear", Cache_Clear_f, "Empties a kind of the cache folder, or all" );
	Cache_MoveOld();
	Cache_Rekey();
	Cache_Limit();
}
