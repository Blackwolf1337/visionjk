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

// cl_memwatch.cpp -- the memory monitor of the client (v_memwatch). Not from OpenJK.
//
// The intrusion detection (cl_ids.cpp) sees what a server sends. This file looks at the process itself for what
// code of an attacker that got in would leave behind:
//
// - the code of the game changed: the executable sections of the program, the renderer, the cgame and the ui are
//   hashed when they are loaded and again, 64 KB a frame, while they run. Nothing of the game writes there.
// - code from a file that was deleted, from a memfd or from a temporary folder.
// - a process the game started (a shell, a downloader) that is no browser for Sys_OpenURL.
// - writable and executable memory, new executable memory from no file, and libraries from outside the program
//   folder and the system. Drivers, overlays (Steam, Discord, RTSS) and injected libraries make these too, so they
//   are written down and nothing more.
//
// The first three are strong: while on a server the client leaves it, and it is remembered like the other servers
// the intrusion detection left. The rest is a line in ids.log and a notice. What was there before the first look
// counts as the start of the game and is not written down, except the strong ones.
//
// A crash while on a server: the address of the server is in ids_session of the home folder from the connect to
// the disconnect. Is it still there at the next start, that start writes the crash down and the next connect to
// that server is refused once.
//
// Code of an attacker that runs in the process can turn all of this off. What the monitor catches is what does not
// know of it, and the line in ids.log is written before anything else could be done about it.
//
// Linux and Windows. macOS: only the crash. Off by default in the builds of the hotpatch, which writes code, and
// of the sanitizers, which make executable memory.

#include "client.h"
#include "sys/sys_memwatch.h"
#include "web/web_http.h"

#define MEMW_PASS_MSEC		2000		// regions and processes are looked at this often
#define MEMW_URL_MSEC		5000		// a process started this long after Sys_OpenURL is the browser
#define MEMW_REGIONS		1024
#define MEMW_SEEN			4096		// regions and processes seen, a power of two
#define MEMW_CHILDREN		32
#define MEMW_MODULES		4			// the program, the renderer, the cgame, the ui
#define MEMW_RANGES			32
#define MEMW_CHUNK			( 64 * 1024 )
#define MEMW_CHUNKS			4096		// 256 MB of code at most
#define MEMW_WEAK_LINES		8			// lines of a weak kind a run
#define MEMW_SESSION		"ids_session"

#if defined(VISIONJK_HOTPATCH) || defined(__SANITIZE_ADDRESS__)
#define MEMW_DEFAULT		"0"
#else
#define MEMW_DEFAULT		"1"
#endif

static cvar_t			*v_memwatch;
static int				memw_nextPass;
static qboolean			memw_started;		// the first look is done
static int				memw_weakLines[CLIDS_NUM_EVENTS];

static sysMemRegion_t	memw_regions[MEMW_REGIONS];
static uint64_t			memw_seen[MEMW_SEEN];	// 0: free
static int				memw_seenCount;

// the code of the game: executable sections, hashed in chunks
static const void		*memw_modules[MEMW_MODULES];
static sysMemCode_t		memw_ranges[MEMW_RANGES];
static int				memw_rangeFirst[MEMW_RANGES];	// its first chunk in memw_hashes
static int				memw_rangeCount, memw_chunkCount, memw_cursor;
static uint64_t			memw_hashes[MEMW_CHUNKS];
static qboolean			memw_hashed[MEMW_CHUNKS];
static int				memw_generation;
static qboolean			memw_sessionRead;	// ids_session of the last run was read: it may be written now

static uint64_t MemW_Key( uintptr_t start, const char *text ) {
	uint64_t key = 1469598103934665603ULL ^ (uint64_t)start;

	for ( ; text && *text; text++ ) {
		key = ( key ^ (unsigned char)*text ) * 1099511628211ULL;
	}
	return key ? key : 1;
}

// qtrue the first time the key is seen. A full table forgets nothing and takes no more.
static qboolean MemW_FirstSeen( uint64_t key ) {
	unsigned int i = (unsigned int)( key ^ ( key >> 32 ) ) & ( MEMW_SEEN - 1 );

	for ( int n = 0; n < MEMW_SEEN; n++, i = ( i + 1 ) & ( MEMW_SEEN - 1 ) ) {
		if ( memw_seen[i] == key ) {
			return qfalse;
		}
		if ( !memw_seen[i] ) {
			if ( memw_seenCount >= MEMW_SEEN * 3 / 4 ) {
				return qfalse;
			}
			memw_seen[i] = key;
			memw_seenCount++;
			return qtrue;
		}
	}
	return qfalse;
}

// a line of the intrusion detection; a weak kind only so many times a run. May not return (a strong one).
static void MemW_Report( clIdsEvent_t event, const char *detail ) {
	const qboolean strong = (qboolean)( event == CLIDS_MEM_CODE || event == CLIDS_MEM_EXEC || event == CLIDS_MEM_CHILD );

	if ( !strong && memw_weakLines[event]++ >= MEMW_WEAK_LINES ) {
		return;
	}
	CL_IDS_Event( event, NULL, detail );
}

/*
=================
MemW_Modules

The modules of the game whose code is hashed, by an address inside each. The ranges are taken again when one of
them is another than before or a library was unloaded since: a range of a module that is gone may not be read.
=================
*/
static void MemW_Modules( void ) {
	const void		*now[MEMW_MODULES] = { (const void *)CL_MemWatchFrame, re ? (const void *)re->Shutdown : NULL,
		VM_ModuleAddress( VM_CGAME ), VM_ModuleAddress( VM_UI ) };
	sysMemCode_t	code[MEMW_RANGES];

	if ( !memcmp( now, memw_modules, sizeof( now ) ) && memw_generation == Sys_LibraryGeneration() ) {
		return;
	}
	memcpy( memw_modules, now, sizeof( now ) );
	memw_generation = Sys_LibraryGeneration();
	memw_rangeCount = memw_chunkCount = memw_cursor = 0;
	memset( memw_hashed, 0, sizeof( memw_hashed ) );

	for ( int i = 0; i < MEMW_MODULES; i++ ) {
		const int n = now[i] ? Sys_MemWatchCode( now[i], code, MEMW_RANGES ) : 0;

		for ( int k = 0; k < n && memw_rangeCount < MEMW_RANGES; k++ ) {
			const int chunks = (int)( ( code[k].size + MEMW_CHUNK - 1 ) / MEMW_CHUNK );

			if ( memw_chunkCount + chunks > MEMW_CHUNKS ) {
				break;
			}
			memw_ranges[memw_rangeCount] = code[k];
			memw_rangeFirst[memw_rangeCount++] = memw_chunkCount;
			memw_chunkCount += chunks;
		}
	}
}

// one chunk a frame: the first time it is kept, after that compared
static void MemW_HashStep( void ) {
	unsigned char	digest[WEB_SHA256_SIZE];
	uint64_t		hash;
	int				r, chunk;
	uintptr_t		start;
	size_t			size;

	if ( !memw_chunkCount || memw_generation != Sys_LibraryGeneration() ) {
		return;
	}
	chunk = memw_cursor;
	memw_cursor = ( memw_cursor + 1 ) % memw_chunkCount;
	for ( r = memw_rangeCount - 1; r > 0 && memw_rangeFirst[r] > chunk; r-- ) {
	}
	start = memw_ranges[r].start + (size_t)( chunk - memw_rangeFirst[r] ) * MEMW_CHUNK;
	if ( start >= memw_ranges[r].start + memw_ranges[r].size ) {
		return;
	}
	size = Q_min( (size_t)MEMW_CHUNK, memw_ranges[r].start + memw_ranges[r].size - start );

	Web_SHA256( (const void *)start, size, digest );
	memcpy( &hash, digest, sizeof( hash ) );
	if ( !memw_hashed[chunk] ) {
		memw_hashes[chunk] = hash;
		memw_hashed[chunk] = qtrue;
	} else if ( memw_hashes[chunk] != hash ) {
		memw_hashes[chunk] = hash;		// written down once
		MemW_Report( CLIDS_MEM_CODE, va( "0x%llx, %u bytes from there", (unsigned long long)start, (unsigned int)size ) );
	}
}

// is the region one of the modules of the game (a module unpacked from a pak lies in a temporary folder)
static qboolean MemW_Ours( const sysMemRegion_t *region ) {
	for ( int i = 0; i < memw_rangeCount; i++ ) {
		if ( memw_ranges[i].start >= region->start && memw_ranges[i].start < region->end ) {
			return qtrue;
		}
	}
	return qfalse;
}

static void MemW_Regions( void ) {
	memwPlaces_t	places;
	const int		count = Sys_MemWatchRegions( memw_regions, MEMW_REGIONS );

	Sys_MemWatchPlaces( &places );
	for ( int i = 0; i < count; i++ ) {
		const sysMemRegion_t	*r = &memw_regions[i];
		const memwKind_t		kind = MemW_Classify( r, &places );
		static const clIdsEvent_t events[] = { CLIDS_NUM_EVENTS, CLIDS_MEM_EXEC, CLIDS_MEM_RWX, CLIDS_MEM_ANON, CLIDS_MEM_LIBRARY };

		if ( kind == MEMW_NONE || !MemW_FirstSeen( MemW_Key( r->image ? 0 : r->start, r->image ? r->path : "" ) ) ) {
			continue;
		}
		if ( MemW_Ours( r ) || ( !memw_started && kind != MEMW_EXEC_FILE ) ) {
			continue;
		}
		if ( r->image ) {
			MemW_Report( events[kind], va( "%s%s", r->path, r->deleted ? " (deleted)" : "" ) );
		} else {
			MemW_Report( events[kind], va( "0x%llx, %llu KB%s%s", (unsigned long long)r->start,
				(unsigned long long)( ( r->end - r->start ) / 1024 ), r->path[0] ? " " : "", r->path ) );
		}
	}
}

static void MemW_Children( void ) {
	static const char * const allowed[] = { "pulseaudio", NULL };
	sysMemChild_t	children[MEMW_CHILDREN];
	const int		count = Sys_MemWatchChildren( children, MEMW_CHILDREN );
	const int		url = Sys_OpenURLTime();

	for ( int i = 0; i < count; i++ ) {
		qboolean known = qfalse;

		if ( !MemW_FirstSeen( MemW_Key( (uintptr_t)children[i].pid, "\001pid" ) ) ) {
			continue;
		}
		for ( const char * const *a = allowed; *a; a++ ) {
			known = (qboolean)( known || !Q_stricmp( children[i].name, *a ) );
		}
		if ( known || ( url && Sys_Milliseconds() - url < MEMW_URL_MSEC ) ) {
			continue;
		}
		MemW_Report( CLIDS_MEM_CHILD, va( "%d %s", children[i].pid, children[i].name ) );
	}
}

/*
=================
CL_MemWatchFrame

Every frame of the client: a chunk of code, and every two seconds the regions and the processes. May not return:
a strong finding while on a server leaves it.
=================
*/
void CL_MemWatchFrame( void ) {
	const int now = Sys_Milliseconds();

	if ( !v_memwatch || !v_memwatch->integer || !Cvar_VariableIntegerValue( "v_ids" ) ) {
		return;
	}
	if ( !memw_nextPass || now - memw_nextPass >= 0 ) {
		memw_nextPass = now + MEMW_PASS_MSEC;
		MemW_Modules();
		MemW_Regions();
		MemW_Children();
		memw_started = qtrue;
	}
	MemW_HashStep();
}

/*
=================
ids_session

The address of the server the client is on, in the home folder (not the folder of the game: a pak can not hold
it there). Empty when it is on none.
=================
*/
static void MemW_Session( const char *text ) {
	fileHandle_t f;

	if ( !memw_sessionRead ) {
		return;
	}
	f = FS_SV_FOpenFileWrite( MEMW_SESSION );
	if ( f ) {
		FS_Write( text, (int)strlen( text ), f );
		FS_FCloseFile( f );
	}
}

void CL_MemWatchConnect( void ) {
	qtime_t t;

	Com_RealTime( &t );
	MemW_Session( va( "%s %04i-%02i-%02i %02i:%02i\n", NET_AdrToString( &clc.serverAddress ), 1900 + t.tm_year, 1 + t.tm_mon,
		t.tm_mday, t.tm_hour, t.tm_min ) );
}

void CL_MemWatchDisconnect( void ) {
	MemW_Session( "" );
}

// a session the last run did not end: it crashed on that server
static void MemW_LastSession( void ) {
	fileHandle_t	f;
	char			text[128], *time;
	netadr_t		adr;
	const int		len = FS_SV_FOpenFileRead( MEMW_SESSION, &f );

	memw_sessionRead = qtrue;
	if ( !f ) {
		return;
	}
	text[0] = '\0';
	if ( len > 0 && len < (int)sizeof( text ) ) {
		FS_Read( text, len, f );
		text[len] = '\0';
	}
	FS_FCloseFile( f );
	text[strcspn( text, "\r\n" )] = '\0';
	time = strchr( text, ' ' );
	if ( !time ) {
		return;
	}
	*time++ = '\0';
	// numbers only: the file names no host to look up
	if ( !text[0] || text[strspn( text, "0123456789.:" )] || !NET_StringToAdr( text, &adr ) || adr.type != NA_IP ) {
		return;
	}
	CL_IDS_Crashed( &adr, va( "%s, %s", NET_AdrToString( &adr ), time ) );
	MemW_Session( "" );
}

void CL_MemWatchInit( void ) {


	v_memwatch = Cvar_Get( "v_memwatch", MEMW_DEFAULT, CVAR_ARCHIVE, "Memory monitor of the client (with v_ids): 1 the process is looked at for signs of an attacker (changed code, code from deleted or temporary files, programs it did not start) and a server is left for them. 0 off" );
	MemW_LastSession();
}

void CL_MemWatchShutdown( void ) {
	CL_MemWatchDisconnect();
}
