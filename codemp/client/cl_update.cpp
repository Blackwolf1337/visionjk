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


// cl_update.cpp -- the updater of the client. Not from JA+, see docs/japlus/update.md.
//
// The website has a file list for every system (shared/update/upd_manifest.h). At its start the game asks for
// the one of its own system, in a thread: the game never waits for the website. If the version there is newer
// than this build, the player is told, in the console and on the main menu (codemp/ui/ui_update.c). When he
// says so, the files that changed are fetched, each beside the one it replaces, and checked against the list.
// Only when all of them are there are they put in place, and the game starts itself again.
//
// A program file that runs can not be written to on Windows, but it can be renamed. A file the game has open
// (its pk3 files, as long as it runs) can not even be renamed there. So the game does not put the new files in
// place itself: it writes down which ones are waiting (the journal), ends and starts again, and the first
// thing the program does at its start, before it opens anything, is the swap: the old file is moved aside
// (.vjkold), the new one (.vjknew) takes its name. Then it starts once more, now as the new program, and that
// start deletes the old files. A swap that was cut short is finished the same way by the next start.
//
// Who is trusted: the server at the address this build was given, by its certificate (sys/sys_http.h). There
// is no signature. A player can name another address on the command line and nowhere else, see CL_UpdateAddress().
//
// Never replaced: a build between two releases (somebody's own), a file the list marks "keep" that is there
// (the .cfg files), and anything outside the folder of the program.

#include "client.h"

#if !defined( __EMSCRIPTEN__ )		// a page is the newest version every time it is loaded

#include <SDL.h>
#include <sys/stat.h>

#include "sys/sys_http.h"
#include "sys/sys_public.h"
#include "update/upd_manifest.h"
#include "web/web_http.h"
#include "qcommon/game_version.h"

// Where the file lists and the files are, unless the command line names another address. The build can be
// given another one (CMake: UpdateURL), and for a test another version than the tag it was made from.
#ifndef UPDATE_URL
	#define UPDATE_URL		"https://jka.vision/downloads/update"
#endif
#ifndef UPDATE_VERSION
	#define UPDATE_VERSION	GIT_TAG
#endif

#if defined( _WIN32 )
	#define UPDATE_SYSTEM	"windows-" ARCH_STRING
#elif defined( MACOS_X )
	#define UPDATE_SYSTEM	"macos-" ARCH_STRING
#else
	#define UPDATE_SYSTEM	"linux-" ARCH_STRING
#endif

#define UPDATE_JOURNAL_HEAD		"visionjk-update-journal 1"
#define UPDATE_TEXT_MSEC		250		// how often the line on the menu is written anew while files come in
#define UPDATE_PATH				( MAX_OSPATH + UPD_MAX_PATH + 16 )

// cl_updateState, which the ui module reads
typedef enum {
	UPDATE_NONE,			// nothing to say
	UPDATE_AVAILABLE,		// a newer version is out
	UPDATE_WORKING,			// its files are coming in
	UPDATE_FAILED,			// they did not, the console says why
	UPDATE_RESTARTING		// they are in place, the game is about to end and start again
} updateState_t;

enum { JOB_NONE, JOB_CHECK, JOB_FETCH };

static struct {
	char			address[UPD_MAX_ADDRESS];	// where the lists and the files are asked for
	qboolean		addressGiven;				//	which the command line named,
	qboolean		addressRefused;				//	or tried to: it was no https address
	int				own[3];
	qboolean		release;					// this build is a release, so there can be a newer one of it
	qboolean		elsewhere;					// the game reads its files from another folder than the program's

	updateState_t	state;
	SDL_Thread		*thread;
	int				job;
	volatile int	done;				// the thread is through. It writes this last
	volatile int	cancel;				// the game wants it to end
	qboolean		quiet;				// the question at the start: it says nothing unless there is news
	qboolean		thenFetch;			// "update" before a list was there: go on when it is

	// The thread writes these and then done, the game reads them after it saw done. While a thread runs the
	// game touches none of them but fetched.
	qboolean		ok;
	char			error[512];
	updList_t		list;
	byte			change[UPD_MAX_FILES];		// this file of the list has to be fetched
	int				numChange;
	long long		fetchBytes;					// all of those together
	volatile long long	fetched;				// and how much of it came in so far

	int				textTime;
} upd;

static char			updListText[UPD_MAX_LIST];
static int			updListLen;
static const char	*updStartNote;				// what CL_UpdateResume() has to say, once there is a console
static char			updStartVersion[UPD_MAX_PATH];	//	and the version or the file it is about

static cvar_t	*cl_updateCheck;

/*
===============================================================================

Files in the folder of the game

Plain C and nothing of the engine, for two reasons: the thread uses them, and CL_UpdateResume() runs before
the engine is started.

===============================================================================
*/

// The path of a file of a release on this machine, with one of the endings of the updater or "". qfalse: too
// long, or the system does not say where the program is: then there is no folder to put anything into.
static qboolean Update_Path( const char *path, const char *ending, char *out, size_t outSize ) {
	const char	*folder = Sys_ProgramFolder();
	const int	len = snprintf( out, outSize, "%s/%s%s", folder, path, ending );

	return ( folder[0] && len > 0 && (size_t)len < outSize ) ? qtrue : qfalse;
}

static qboolean Update_Exists( const char *osPath ) {
	struct stat st;

	return ( stat( osPath, &st ) == 0 ) ? qtrue : qfalse;
}

// from gets the name to. A file that has that name is gone after it.
static qboolean Update_Move( const char *from, const char *to ) {
	// (rename() of Windows does not replace a file, the one of every other system does)
	remove( to );
	return ( rename( from, to ) == 0 ) ? qtrue : qfalse;
}

// The folders a file is in, made if they are not there. A release may bring a folder an older one did not have.
static void Update_MakeFolders( const char *path ) {
	char		part[UPD_MAX_PATH], osPath[UPDATE_PATH];
	const char	*slash = path;

	while ( ( slash = strchr( slash, '/' ) ) != NULL ) {
		Q_strncpyz( part, path, (int)( slash - path ) + 1 );
		if ( Update_Path( part, "", osPath, sizeof( osPath ) ) )
			Sys_Mkdir( osPath );
		slash++;
	}
}

// Size and SHA-256 of a file. qfalse: there is no such file, or it could not be read to its end.
static qboolean Update_HashFile( const char *osPath, long long *bytes, byte digest[WEB_SHA256_SIZE], const volatile int *cancel ) {
	static byte	buffer[65536];		// (the thread, or the start before there is one)
	webSha256_t	sha;
	FILE		*f = fopen( osPath, "rb" );
	size_t		got;
	qboolean	whole;

	if ( !f )
		return qfalse;
	Web_SHA256Init( &sha );
	*bytes = 0;
	while ( ( got = fread( buffer, 1, sizeof( buffer ), f ) ) > 0 ) {
		Web_SHA256Update( &sha, buffer, got );
		*bytes += (long long)got;
		if ( cancel && *cancel )
			break;
	}
	whole = ( feof( f ) && !ferror( f ) ) ? qtrue : qfalse;
	fclose( f );
	Web_SHA256Final( &sha, digest );
	return whole;
}

/*
===============================================================================

The journal

visionjk-update.txt in the folder of the game, while a swap is under way and until the old files are gone:

	visionjk-update-journal 1
	swap 0.1.1
	<path>
	<path>

The second line: "swap", the files below are being moved. "done", every one of them is in place. "old", the
same and the player was told, only old files are left to delete.

===============================================================================
*/

typedef struct updateJournal_s {
	qboolean	done;
	qboolean	told;
	char		version[32];
	int			numPaths;
	char		paths[UPD_MAX_FILES][UPD_MAX_PATH];
} updateJournal_t;

static updateJournal_t updJournal;

static void Update_Chomp( char *line ) {
	size_t len = strlen( line );

	while ( len && ( line[len - 1] == '\n' || line[len - 1] == '\r' ) )
		line[--len] = '\0';
}

// qfalse: there is none, or it is not one
static qboolean Update_ReadJournal( updateJournal_t *journal ) {
	char	osPath[UPDATE_PATH], line[UPD_MAX_PATH + 16];
	FILE	*f;

	memset( journal, 0, sizeof( *journal ) );
	if ( !Update_Path( UPD_JOURNAL, "", osPath, sizeof( osPath ) ) || ( f = fopen( osPath, "r" ) ) == NULL )
		return qfalse;
	if ( !fgets( line, sizeof( line ), f ) || ( Update_Chomp( line ), strcmp( line, UPDATE_JOURNAL_HEAD ) )
		|| !fgets( line, sizeof( line ), f ) ) {
		fclose( f );
		return qfalse;
	}
	Update_Chomp( line );
	if ( !strncmp( line, "done ", 5 ) ) {
		journal->done = qtrue;
	} else if ( !strncmp( line, "old ", 4 ) ) {
		journal->done = journal->told = qtrue;
	} else if ( strncmp( line, "swap ", 5 ) ) {
		fclose( f );
		return qfalse;
	}
	Q_strncpyz( journal->version, strchr( line, ' ' ) + 1, sizeof( journal->version ) );
	while ( journal->numPaths < UPD_MAX_FILES && fgets( line, sizeof( line ), f ) ) {
		Update_Chomp( line );
		// (the same rule as for a file list: the updater touches nothing else, whoever wrote this file)
		if ( UPD_PathAllowed( line ) )
			Q_strncpyz( journal->paths[journal->numPaths++], line, sizeof( journal->paths[0] ) );
	}
	fclose( f );
	return qtrue;
}

// Written beside the journal and then given its name, so there is always a whole one.
static qboolean Update_WriteJournal( const updateJournal_t *journal ) {
	char	osPath[UPDATE_PATH], osNew[UPDATE_PATH];
	FILE	*f;
	int		i;

	if ( !Update_Path( UPD_JOURNAL, "", osPath, sizeof( osPath ) ) || !Update_Path( UPD_JOURNAL, UPD_NEW_ENDING, osNew, sizeof( osNew ) )
		|| ( f = fopen( osNew, "w" ) ) == NULL )
		return qfalse;
	fprintf( f, "%s\n%s %s\n", UPDATE_JOURNAL_HEAD, journal->told ? "old" : ( journal->done ? "done" : "swap" ), journal->version );
	for ( i = 0; i < journal->numPaths; i++ )
		fprintf( f, "%s\n", journal->paths[i] );
	if ( fclose( f ) != 0 ) {
		remove( osNew );
		return qfalse;
	}
	return Update_Move( osNew, osPath );
}

static void Update_RemoveJournal( void ) {
	char osPath[UPDATE_PATH];

	if ( Update_Path( UPD_JOURNAL, "", osPath, sizeof( osPath ) ) )
		remove( osPath );
}

// A file that can be run keeps that. A new one is given it by its name: the programs of a release end in the
// name of the processor (visionjk.x86_64). Windows has no such thing.
static void Update_Mode( const char *path, const char *osPath, const char *osOld, qboolean hadOld ) {
#ifndef _WIN32
	struct stat	st;
	const char	*dot = strrchr( path, '.' );

	if ( hadOld && stat( osOld, &st ) == 0 )
		chmod( osPath, st.st_mode & 07777 );
	else
		chmod( osPath, ( dot && !strcmp( dot + 1, ARCH_STRING ) ) ? 0755 : 0644 );
#else
	(void)path; (void)osPath; (void)osOld; (void)hadOld;
#endif
}

// One file of a swap: the old one aside, the new one in its place. qfalse: neither happened.
static qboolean Update_SwapOne( const char *path, qboolean *hadOld ) {
	char osPath[UPDATE_PATH], osOld[UPDATE_PATH], osNew[UPDATE_PATH];

	if ( !Update_Path( path, "", osPath, sizeof( osPath ) ) || !Update_Path( path, UPD_OLD_ENDING, osOld, sizeof( osOld ) )
		|| !Update_Path( path, UPD_NEW_ENDING, osNew, sizeof( osNew ) ) )
		return qfalse;
	*hadOld = Update_Exists( osPath );
	if ( *hadOld && !Update_Move( osPath, osOld ) )
		return qfalse;
	if ( !Update_Move( osNew, osPath ) ) {
		if ( *hadOld )
			Update_Move( osOld, osPath );
		return qfalse;
	}
	Update_Mode( path, osPath, osOld, *hadOld );
	return qtrue;
}

// and back, for a swap that did not get through
static void Update_UnswapOne( const char *path, qboolean hadOld ) {
	char osPath[UPDATE_PATH], osOld[UPDATE_PATH], osNew[UPDATE_PATH];

	if ( !Update_Path( path, "", osPath, sizeof( osPath ) ) || !Update_Path( path, UPD_OLD_ENDING, osOld, sizeof( osOld ) )
		|| !Update_Path( path, UPD_NEW_ENDING, osNew, sizeof( osNew ) ) )
		return;
	Update_Move( osPath, osNew );
	if ( hadOld )
		Update_Move( osOld, osPath );
}

/*
=================
CL_UpdateResume

Called by main() before anything of the engine is started, so with no console, no cvars and no memory of the
engine, and before the game has a file open. Looks for the journal of the updater.

"swap": the files that wait as .vjknew are put in place. Every one of them was fetched whole and checked
before the journal was written. qtrue: done, and the program that runs is the old one, so main() starts the
game again. If one of them can not be moved, the ones that were are moved back, and if that leaves the game
as it was the journal is dropped: the player is told, and "update" tries again with the files that are there.
If it was a swap that had been cut short before, there is no way back to one version, the journal stays and
the next start tries again.

"done": the old files are deleted. One that can not be deleted yet (on Windows the program that just ended
may still have it) stays until the next start, and the journal with it.
=================
*/
qboolean CL_UpdateResume( void ) {
	static qboolean	hadOld[UPD_MAX_FILES];
	static byte		waits[UPD_MAX_FILES];
	char			osPath[UPDATE_PATH];
	int				i, waiting = 0, left = 0;

	if ( !Update_ReadJournal( &updJournal ) )
		return qfalse;
	Q_strncpyz( updStartVersion, updJournal.version, sizeof( updStartVersion ) );

	if ( !updJournal.done ) {
		// which ones still wait. In a swap that starts here that is all of them.
		for ( i = 0; i < updJournal.numPaths; i++ ) {
			waits[i] = ( Update_Path( updJournal.paths[i], UPD_NEW_ENDING, osPath, sizeof( osPath ) ) && Update_Exists( osPath ) ) ? 1 : 0;
			waiting += waits[i];
		}
		for ( i = 0; i < updJournal.numPaths; i++ ) {
			if ( !waits[i] || Update_SwapOne( updJournal.paths[i], &hadOld[i] ) )
				continue;
			Q_strncpyz( updStartVersion, updJournal.paths[i], sizeof( updStartVersion ) );
			while ( --i >= 0 ) {
				if ( waits[i] )
					Update_UnswapOne( updJournal.paths[i], hadOld[i] );
			}
			if ( waiting == updJournal.numPaths ) {
				Update_RemoveJournal();
				updStartNote = S_COLOR_YELLOW "The update could not be put in place, nothing was changed: %s can not be replaced.\n";
			} else {
				updStartNote = S_COLOR_YELLOW "An update of the game was cut short and could not be finished: %s can not be replaced. "
					"If the game does not work, put the files of the newest version from the website into its folder.\n";
			}
			return qfalse;
		}
		updJournal.done = qtrue;
		// (if this fails the next start finds a swap with nothing left to move, and goes on from here)
		Update_WriteJournal( &updJournal );
		if ( waiting )
			return qtrue;
	}

	for ( i = 0; i < updJournal.numPaths; i++ ) {
		if ( !Update_Path( updJournal.paths[i], UPD_OLD_ENDING, osPath, sizeof( osPath ) ) )
			continue;
		remove( osPath );
		if ( Update_Exists( osPath ) )
			left++;
	}
	if ( !updJournal.told )
		updStartNote = "The game was updated to version %s.\n";
	if ( !left ) {
		Update_RemoveJournal();
	} else if ( !updJournal.told ) {
		updJournal.told = qtrue;
		Update_WriteJournal( &updJournal );
	}
	return qfalse;
}

/*
=================
Update_Journal

In the thread of the game, when every file that changed is there as .vjknew and was checked: writes down
which ones wait. The swap itself is the first thing the next start does, see CL_UpdateResume().
=================
*/
static qboolean Update_Journal( void ) {
	int i;

	memset( &updJournal, 0, sizeof( updJournal ) );
	Com_sprintf( updJournal.version, sizeof( updJournal.version ), "%i.%i.%i", upd.list.version[0], upd.list.version[1], upd.list.version[2] );
	for ( i = 0; i < upd.list.numFiles; i++ ) {
		if ( upd.change[i] )
			Q_strncpyz( updJournal.paths[updJournal.numPaths++], upd.list.files[i].path, sizeof( updJournal.paths[0] ) );
	}
	return Update_WriteJournal( &updJournal );
}

/*
===============================================================================

The thread

One at a time, for one job: ask for the file list, or fetch the files. It calls nothing of the engine that is
not plain arithmetic: no console, no cvars, no file system of the game. What it found is in upd when it sets
upd.done, and the game says it.

===============================================================================
*/

static void Update_Error( const char *format, const char *a, const char *b ) {
	snprintf( upd.error, sizeof( upd.error ), format, a, b );
}

static qboolean Update_ListSink( void *ctx, const void *data, size_t len ) {
	(void)ctx;
	if ( (size_t)updListLen + len > sizeof( updListText ) )
		return qfalse;
	memcpy( updListText + updListLen, data, len );
	updListLen += (int)len;
	return qtrue;
}

/*
=================
Update_CheckThread

Fetches the file list of this system. If its version is newer than this build, every file of it is compared
with the one that is here: by its size, and if that is the same by its SHA-256. What differs or is missing
has to be fetched. A "keep" file that is here is left alone whatever is in it.
=================
*/
static int Update_CheckThread( void *unused ) {
	char		url[UPD_MAX_ADDRESS + 64], why[160], osPath[UPDATE_PATH];
	const char	*wrong;
	int			i;

	(void)unused;
	upd.ok = qfalse;
	upd.numChange = 0;
	upd.fetchBytes = 0;
	memset( upd.change, 0, sizeof( upd.change ) );
	updListLen = 0;

	snprintf( url, sizeof( url ), "%s/%s.txt", upd.address, UPDATE_SYSTEM );
	if ( !Sys_HttpGet( url, Update_ListSink, NULL, &upd.cancel, why, sizeof( why ) ) ) {
		Update_Error( "%s did not say whether there is a newer version: %s", upd.address, why );
	} else if ( ( wrong = UPD_ParseList( updListText, (size_t)updListLen, &upd.list ) ) != NULL ) {
		Update_Error( "The file list of %s can not be used: %s", upd.address, wrong );
	} else {
		upd.ok = qtrue;
		if ( UPD_CompareVersion( upd.list.version, upd.own ) > 0 ) {
			for ( i = 0; i < upd.list.numFiles && !upd.cancel; i++ ) {
				const updFile_t	*file = &upd.list.files[i];
				byte			digest[WEB_SHA256_SIZE];
				long long		bytes;
				struct stat		st;

				if ( !Update_Path( file->path, "", osPath, sizeof( osPath ) ) )
					continue;
				if ( stat( osPath, &st ) == 0 ) {
					if ( file->keep )
						continue;
					if ( (long long)st.st_size == file->bytes && Update_HashFile( osPath, &bytes, digest, &upd.cancel )
						&& bytes == file->bytes && !memcmp( digest, file->sha256, sizeof( digest ) ) )
						continue;
				}
				upd.change[i] = 1;
				upd.numChange++;
				upd.fetchBytes += file->bytes;
			}
		}
	}
	upd.done = 1;
	return 0;
}

typedef struct updateSink_s {
	FILE			*f;
	webSha256_t		sha;
	long long		bytes, most;
	qboolean		disk;			// the file could not be written
} updateSink_t;

static qboolean Update_FileSink( void *ctx, const void *data, size_t len ) {
	updateSink_t *sink = (updateSink_t *)ctx;

	// more than the list says the file has: not this file
	if ( sink->bytes + (long long)len > sink->most )
		return qfalse;
	if ( fwrite( data, 1, len, sink->f ) != len ) {
		sink->disk = qtrue;
		return qfalse;
	}
	Web_SHA256Update( &sink->sha, data, len );
	sink->bytes += (long long)len;
	upd.fetched += (long long)len;
	return qtrue;
}

/*
=================
Update_FetchThread

Fetches every file that changed to <path>.vjknew and checks it: as many bytes as the list says, with the
SHA-256 the list says. One that is there already from an earlier try and is right is not fetched again.
A file that came in wrong is deleted, and nothing was touched: the swap is the business of the game, after.
=================
*/
static int Update_FetchThread( void *unused ) {
	char	url[UPD_MAX_ADDRESS + UPD_MAX_PATH + 96], why[160], osNew[UPDATE_PATH];
	int		i;

	(void)unused;
	upd.ok = qtrue;
	upd.fetched = 0;
	for ( i = 0; i < upd.list.numFiles && upd.ok; i++ ) {
		const updFile_t	*file = &upd.list.files[i];
		updateSink_t	sink;
		byte			digest[WEB_SHA256_SIZE];
		long long		bytes;
		qboolean		got;

		if ( !upd.change[i] )
			continue;
		upd.ok = qfalse;
		if ( !Update_Path( file->path, UPD_NEW_ENDING, osNew, sizeof( osNew ) ) ) {
			Update_Error( "The path of %s is too long in this folder%s", file->path, "" );
			break;
		}
		if ( Update_HashFile( osNew, &bytes, digest, &upd.cancel ) && bytes == file->bytes && !memcmp( digest, file->sha256, sizeof( digest ) ) ) {
			upd.fetched += bytes;
			upd.ok = qtrue;
			continue;
		}
		if ( upd.cancel )
			break;

		Update_MakeFolders( file->path );
		memset( &sink, 0, sizeof( sink ) );
		sink.most = file->bytes;
		sink.f = fopen( osNew, "wb" );
		if ( !sink.f ) {
			Update_Error( "The folder of the game can not be written to (%s). Get the new version from the website%s", file->path, "" );
			break;
		}
		Web_SHA256Init( &sink.sha );
		snprintf( url, sizeof( url ), "%s/%i.%i.%i/%s/%s", upd.address, upd.list.version[0], upd.list.version[1], upd.list.version[2],
			UPDATE_SYSTEM, file->path );
		got = Sys_HttpGet( url, Update_FileSink, &sink, &upd.cancel, why, sizeof( why ) );
		if ( fclose( sink.f ) != 0 )
			sink.disk = qtrue;
		Web_SHA256Final( &sink.sha, digest );

		if ( sink.disk )
			Update_Error( "%s could not be written, is the disk full?%s", file->path, "" );
		else if ( !got )
			Update_Error( "%s did not come in: %s", file->path, why );
		else if ( sink.bytes != file->bytes || memcmp( digest, file->sha256, sizeof( digest ) ) )
			Update_Error( "%s came in damaged%s", file->path, "" );
		else
			upd.ok = qtrue;
		if ( !upd.ok )
			remove( osNew );
	}
	if ( upd.cancel )
		upd.ok = qfalse;
	upd.done = 1;
	return 0;
}

/*
===============================================================================

The game

===============================================================================
*/

static void Update_Say( updateState_t state, const char *text ) {
	upd.state = state;
	Cvar_Set( "cl_updateState", va( "%i", (int)state ) );
	Cvar_Set( "cl_updateText", text );
}

static const char *Update_VersionText( const int version[3] ) {
	return va( "%i.%i.%i", version[0], version[1], version[2] );
}

static void Update_Start( int job, qboolean quiet ) {
	upd.job = job;
	upd.quiet = quiet;
	upd.done = 0;
	upd.cancel = 0;
	upd.thread = SDL_CreateThread( job == JOB_CHECK ? Update_CheckThread : Update_FetchThread, "update", NULL );
	if ( !upd.thread ) {
		upd.job = JOB_NONE;
		if ( !quiet )
			Com_Printf( "The update check could not be started\n" );
		return;
	}
	if ( job == JOB_FETCH ) {
		upd.textTime = 0;
		Update_Say( UPDATE_WORKING, va( "Updating to version %s", Update_VersionText( upd.list.version ) ) );
	}
}

static void Update_Available( void ) {
	const char *version = Update_VersionText( upd.list.version );

	Update_Say( UPDATE_AVAILABLE, va( "Version %s is out. Click here to update (%.1f MB)", version, upd.fetchBytes / 1048576.0 ) );
}

// the file list is there, or is not
static void Update_Checked( void ) {
	if ( !upd.ok ) {
		// The website is not there, or this machine has no line: not worth a word at every start
		if ( !upd.quiet )
			Com_Printf( "%s\n", upd.error );
		else
			Com_DPrintf( "%s\n", upd.error );
		return;
	}
	if ( !upd.numChange ) {
		if ( !upd.quiet )
			Com_Printf( "This is version %s, and the newest one is %s.\n", Update_VersionText( upd.own ), Update_VersionText( upd.list.version ) );
		Update_Say( UPDATE_NONE, "" );
		return;
	}
	Update_Available();
	Com_Printf( S_COLOR_CYAN "Version %s of the game is out, this is %s. To get it, type update here or click the line on the main menu "
		"(%.1f MB in %i files).\n", Update_VersionText( upd.list.version ), Update_VersionText( upd.own ), upd.fetchBytes / 1048576.0, upd.numChange );
	if ( upd.thenFetch && cls.state == CA_DISCONNECTED )
		Update_Start( JOB_FETCH, qfalse );
}

// the files are there, or are not
static void Update_Fetched( void ) {
	if ( !upd.ok ) {
		if ( upd.cancel ) {
			Com_Printf( "The update was stopped. What came in is kept for the next time.\n" );
			Update_Available();
		} else {
			Com_Printf( S_COLOR_YELLOW "The update failed, nothing was changed. %s\n", upd.error );
			Update_Say( UPDATE_FAILED, "The update failed, the console says why. Click here to try again" );
		}
		return;
	}
	if ( cls.state != CA_DISCONNECTED ) {
		// he joined a server while the files came in. They stay where they are.
		Com_Printf( "The update is fetched. Leave the server and type update to put it in place.\n" );
		Update_Available();
		return;
	}
	if ( !Update_Journal() ) {
		Com_Printf( S_COLOR_YELLOW "The update could not be put in place, nothing was changed: the folder of the game can not be written to.\n" );
		Update_Say( UPDATE_FAILED, "The update failed, the console says why. Click here to try again" );
		return;
	}
	Com_Printf( "Version %s is fetched. The game starts again and puts it in place.\n", Update_VersionText( upd.list.version ) );
	Update_Say( UPDATE_RESTARTING, va( "Version %s is fetched, the game starts again", Update_VersionText( upd.list.version ) ) );
	Sys_RestartOnExit();
	Cbuf_AddText( "quit\n" );
}

/*
=================
CL_UpdateFrame

Once a frame: is the thread through, and how far are the files.
=================
*/
void CL_UpdateFrame( void ) {
	if ( !upd.thread )
		return;
	if ( upd.done ) {
		const int job = upd.job;

		SDL_WaitThread( upd.thread, NULL );		// (it is through, this does not wait)
		upd.thread = NULL;
		upd.job = JOB_NONE;
		if ( job == JOB_CHECK )
			Update_Checked();
		else
			Update_Fetched();
		upd.thenFetch = qfalse;
		return;
	}
	if ( upd.job == JOB_FETCH && cls.realtime - upd.textTime >= UPDATE_TEXT_MSEC ) {
		upd.textTime = cls.realtime;
		Cvar_Set( "cl_updateText", va( "Updating to version %s: %i %%", Update_VersionText( upd.list.version ),
			upd.fetchBytes > 0 ? (int)( upd.fetched * 100 / upd.fetchBytes ) : 0 ) );
	}
}

static qboolean Update_Possible( void ) {
	if ( !upd.release ) {
		Com_Printf( "This is a build between two releases (%s), there is no update for those.\n", UPDATE_VERSION );
		return qfalse;
	}
	if ( upd.elsewhere ) {
		Com_Printf( "This game reads its files from another folder than the one its program is in (fs_basepath), it does not update itself.\n" );
		return qfalse;
	}
	if ( !upd.address[0] ) {
		Com_Printf( "This build has no address to ask for updates.\n" );
		return qfalse;
	}
	if ( upd.thread ) {
		if ( upd.job == JOB_FETCH )
			Com_Printf( "The update is on its way: %i %% so far. update_cancel stops it.\n",
				upd.fetchBytes > 0 ? (int)( upd.fetched * 100 / upd.fetchBytes ) : 0 );
		else
			Com_Printf( "The website is being asked.\n" );
		return qfalse;
	}
	return qtrue;
}

// update_check: asks the website now, whatever cl_updateCheck says
static void CL_UpdateCheck_f( void ) {
	if ( Update_Possible() )
		Update_Start( JOB_CHECK, qfalse );
}

// update: fetches the newer version, puts it in place and starts the game again
static void CL_Update_f( void ) {
	if ( !Update_Possible() )
		return;
	if ( cls.state != CA_DISCONNECTED ) {
		Com_Printf( "Leave the server first: the update ends the game and starts it again.\n" );
		return;
	}
	if ( upd.numChange && ( upd.state == UPDATE_AVAILABLE || upd.state == UPDATE_FAILED ) ) {
		Update_Start( JOB_FETCH, qfalse );
	} else {
		upd.thenFetch = qtrue;
		Update_Start( JOB_CHECK, qfalse );
	}
}

static void CL_UpdateCancel_f( void ) {
	if ( upd.thread && upd.job == JOB_FETCH )
		upd.cancel = 1;
	else
		Com_Printf( "No update is on its way.\n" );
}

/*
=================
CL_UpdateAddress

Called by Com_Init() right after the command line was read and before any config file is run. At that
moment a cvar can only have come from the command line, and that is the one place a player can name another
address than the one of this build: "+set cl_updateURL https://...". It is copied here and the copy is what
the updater asks, never the cvar. So no config, no module, no server and nobody at the console can send a
client somewhere else for its program files. The cvar is only there to be looked at.

An address a player gives has to be https. The one of the build is whatever its maker gave it.
=================
*/
void CL_UpdateAddress( void ) {
	char given[MAX_CVAR_VALUE_STRING];

	Cvar_VariableStringBuffer( "cl_updateURL", given, sizeof( given ) );
	if ( given[0] && UPD_AddressAllowed( given, qfalse ) ) {
		Q_strncpyz( upd.address, given, sizeof( upd.address ) );
		upd.addressGiven = qtrue;
	} else {
		upd.addressRefused = given[0] ? qtrue : qfalse;
		if ( UPD_AddressAllowed( UPDATE_URL, qtrue ) )
			Q_strncpyz( upd.address, UPDATE_URL, sizeof( upd.address ) );
	}
	Cvar_Get( "cl_updateURL", upd.address, CVAR_INIT | CVAR_ROM | CVAR_PROTECTED, "Where the game asks for a newer version of itself. Only the command line can name another address" );
	Cvar_Set( "cl_updateURL", upd.address );
}

void CL_UpdateInit( void ) {
	cl_updateCheck = Cvar_Get( "cl_updateCheck", "1", CVAR_ARCHIVE, "Ask the website at every start whether there is a newer version (0: never)" );
	Cvar_Get( "cl_updateState", "0", CVAR_ROM, "What the updater is doing: 0 nothing, 1 a newer version is out, 2 fetching it, 3 failed, 4 starting again" );
	Cvar_Get( "cl_updateText", "", CVAR_ROM, "What the updater has to say, as the main menu shows it" );
	Update_Say( UPDATE_NONE, "" );
	Cmd_AddCommand( "update", CL_Update_f, "Fetches the newer version of the game, puts it in place and starts the game again" );
	Cmd_AddCommand( "update_check", CL_UpdateCheck_f, "Asks the website whether there is a newer version" );
	Cmd_AddCommand( "update_cancel", CL_UpdateCancel_f, "Stops an update that is being fetched" );

	if ( updStartNote ) {
		Com_Printf( updStartNote, updStartVersion );
		updStartNote = NULL;
	}
	if ( upd.addressRefused )
		Com_Printf( S_COLOR_YELLOW "cl_updateURL on the command line is no https address. The game asks %s as it would without it.\n", upd.address );
	else if ( upd.addressGiven )
		Com_Printf( "Updates are asked for at %s, as the command line says.\n", upd.address );

	upd.release = UPD_ParseRelease( UPDATE_VERSION, upd.own );
	// The files of a release are replaced where the program is. A game that reads its files from somewhere
	// else (fs_basepath) is set up by hand, and is left to that hand.
	upd.elsewhere = Sys_SameFolder( Cvar_VariableString( "fs_basepath" ), Sys_ProgramFolder() ) ? qfalse : qtrue;
	if ( upd.release && !upd.elsewhere && upd.address[0] && cl_updateCheck->integer )
		Update_Start( JOB_CHECK, qtrue );
}

// The game ends. A thread that is still asking is told to stop and left to itself: the program does not wait
// for a web server to end.
void CL_UpdateShutdown( void ) {
	if ( upd.thread ) {
		upd.cancel = 1;
		SDL_DetachThread( upd.thread );
		upd.thread = NULL;
	}
	Cmd_RemoveCommand( "update" );
	Cmd_RemoveCommand( "update_check" );
	Cmd_RemoveCommand( "update_cancel" );
}

#else // __EMSCRIPTEN__

qboolean CL_UpdateResume( void ) { return qfalse; }
void CL_UpdateAddress( void ) {}
void CL_UpdateInit( void ) {}
void CL_UpdateFrame( void ) {}
void CL_UpdateShutdown( void ) {}

#endif
