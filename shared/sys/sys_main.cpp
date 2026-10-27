/*
===========================================================================
Copyright (C) 2005 - 2015, ioquake3 contributors
Copyright (C) 2013 - 2015, OpenJK contributors
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

#include <csignal>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <sys/stat.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#ifndef _WIN32
	#include <unistd.h>		// _exit() in Sys_SigHandler
#endif
#include "qcommon/qcommon.h"
#include "sys_local.h"
#include "sys_loadlib.h"
#include "sys_public.h"
#include "con_local.h"
#if !defined(DEDICATED) && !defined(__EMSCRIPTEN__)
	#ifdef _WIN32
		#include <windows.h>
	#else
		#include <unistd.h>
		#include <climits>
	#endif
#endif

static char binaryPath[ MAX_OSPATH ] = { 0 };
static char installPath[ MAX_OSPATH ] = { 0 };

cvar_t *com_minimized;
cvar_t *com_unfocused;
cvar_t *com_maxfps;
cvar_t *com_maxfpsMinimized;
cvar_t *com_maxfpsUnfocused;
cvar_t *com_unpackLibraries;

/*
=================
Sys_SetBinaryPath
=================
*/
void Sys_SetBinaryPath(const char *path)
{
	Q_strncpyz(binaryPath, path, sizeof(binaryPath));
}

/*
=================
Sys_BinaryPath
=================
*/
char *Sys_BinaryPath(void)
{
	return binaryPath;
}

/*
=================
Sys_SetDefaultInstallPath
=================
*/
void Sys_SetDefaultInstallPath(const char *path)
{
	Q_strncpyz(installPath, path, sizeof(installPath));
}

/*
=================
Sys_DefaultInstallPath
=================
*/
char *Sys_DefaultInstallPath(void)
{
	if (*installPath)
		return installPath;
	else
		return Sys_Cwd();
}

/*
=================
Sys_DefaultAppPath
=================
*/
char *Sys_DefaultAppPath(void)
{
	return Sys_BinaryPath();
}

/*
==================
Sys_GetClipboardData
==================
*/
char *Sys_GetClipboardData( void ) {
#ifdef DEDICATED
	return NULL;
#else
	if ( !SDL_HasClipboardText() )
		return NULL;

	char *cbText = SDL_GetClipboardText();
	size_t len = strlen( cbText ) + 1;

	char *buf = (char *)Z_Malloc( len, TAG_CLIPBOARD );
	Q_strncpyz( buf, cbText, len );

	SDL_free( cbText );
	return buf;
#endif
}

/*
==================
Sys_SetClipboardData

Puts a text (UTF-8) on the clipboard of the system
==================
*/
void Sys_SetClipboardData( const char *text ) {
#ifndef DEDICATED
	SDL_SetClipboardText( text );
#endif
}

/*
==================
Sys_OpenURL

Hands a web address to the system, which opens it in the browser of the user. Only http and https, and only
an address without blanks, quotes and control characters: it comes from a chat line, which anybody can write.
The caller asks the player first. qfalse: it was not handed on.
==================
*/
qboolean Sys_OpenURL( const char *url ) {
#ifdef DEDICATED
	return qfalse;
#else
	const size_t len = strlen( url );

	if ( len > 1024 || ( Q_strncmp( url, "https://", 8 ) && Q_strncmp( url, "http://", 7 ) ) ) {
		return qfalse;
	}
	for ( size_t i = 0; i < len; i++ ) {
		const unsigned char c = (unsigned char)url[i];

		if ( c <= ' ' || c == 127 || c == '"' || c == '\'' || c == '`' || c == '\\' || c == '<' || c == '>' ) {
			return qfalse;
		}
	}
	return (qboolean)( SDL_OpenURL( url ) == 0 );
#endif
}

/*
=================
Sys_ConsoleInput

Handle new console input
=================
*/
char *Sys_ConsoleInput(void)
{
	return CON_Input( );
}

void Sys_Print( const char *msg ) {
	// TTimo - prefix for text that shows up in console but not in notify
	// backported from RTCW
	if ( !Q_strncmp( msg, "[skipnotify]", 12 ) ) {
		msg += 12;
	}
	if ( msg[0] == '*' ) {
		msg += 1;
	}
	ConsoleLogAppend( msg );
	CON_Print( msg );
}

/*
================
Sys_Init

Called after the common systems (cvars, files, etc)
are initialized
================
*/
void Sys_Init( void ) {
	Cmd_AddCommand ("in_restart", IN_Restart);
	Cvar_Get( "arch", OS_STRING " " ARCH_STRING, CVAR_ROM );
	Cvar_Get( "username", Sys_GetCurrentUser(), CVAR_ROM );

	com_unfocused = Cvar_Get( "com_unfocused", "0", CVAR_ROM );
	com_minimized = Cvar_Get( "com_minimized", "0", CVAR_ROM );
#ifdef _JK2EXE
	com_maxfps = Cvar_Get ("com_maxfps", "125", CVAR_ARCHIVE );
#else
	com_maxfps = Cvar_Get( "com_maxfps", "125", CVAR_ARCHIVE, "Maximum frames per second" );
#endif
	com_maxfpsUnfocused = Cvar_Get( "com_maxfpsUnfocused", "0", CVAR_ARCHIVE_ND );
	com_maxfpsMinimized = Cvar_Get( "com_maxfpsMinimized", "50", CVAR_ARCHIVE_ND );

	com_unpackLibraries = Cvar_Get( "com_unpackLibraries", "0", CVAR_INIT|CVAR_PROTECTED );
}

#if !defined(DEDICATED) && !defined(__EMSCRIPTEN__)
/*
=================
The program starts itself again

For the updater of the client (client/cl_update.cpp): when it has replaced the files of the game, the game
ends and starts again, with the arguments it was started with.
=================
*/
static struct {
	char		program[ MAX_OSPATH ];	// the file this program was started from, under the name it had then
	char		folder[ MAX_OSPATH ];	//	and the folder that file is in
	char		**argv;
	qboolean	wanted;
} sysRestart;

// at the very start, before the updater gives any file another name
static void Sys_RestartInit( char **argv ) {
	sysRestart.argv = argv;
#ifdef _WIN32
	const DWORD len = GetModuleFileNameA( NULL, sysRestart.program, sizeof( sysRestart.program ) );

	if ( !len || len >= sizeof( sysRestart.program ) )
		sysRestart.program[0] = '\0';
#else
	char			resolved[ PATH_MAX ];
	const ssize_t	len = readlink( "/proc/self/exe", resolved, sizeof( resolved ) - 1 );

	if ( len > 0 )
		resolved[len] = '\0';
	else if ( !realpath( argv[0], resolved ) )
		resolved[0] = '\0';
	Q_strncpyz( sysRestart.program, resolved, sizeof( sysRestart.program ) );
#endif
	Q_strncpyz( sysRestart.folder, sysRestart.program, sizeof( sysRestart.folder ) );
	char *slash = strrchr( sysRestart.folder, PATH_SEP );
	if ( slash )
		*slash = '\0';
	else
		sysRestart.folder[0] = '\0';
}

/*
=================
Sys_ProgramFolder

The folder the program file is in, as the system names it, "" if the system does not say. Sys_BinaryPath() is
what the program was called with, which on Windows is nothing at all for a program started by its bare name
from its own folder (a batch file, a console): the updater must not take that for a folder.
=================
*/
const char *Sys_ProgramFolder( void ) {
	return sysRestart.folder;
}

// Are these two the same folder? Each is made a whole path first, so "." and a path with other slashes count.
qboolean Sys_SameFolder( const char *a, const char *b ) {
	char	wholeA[ MAX_OSPATH ], wholeB[ MAX_OSPATH ];

	if ( !a[0] || !b[0] )
		return qfalse;
#ifdef _WIN32
	char	*whole[2] = { wholeA, wholeB };
	const DWORD lenA = GetFullPathNameA( a, sizeof( wholeA ), wholeA, NULL ), lenB = GetFullPathNameA( b, sizeof( wholeB ), wholeB, NULL );

	if ( !lenA || lenA >= sizeof( wholeA ) || !lenB || lenB >= sizeof( wholeB ) )
		return qfalse;
	// (a slash at the end, but not the one of "C:\")
	for ( int i = 0; i < 2; i++ ) {
		const size_t len = strlen( whole[i] );

		if ( len > 3 && whole[i][len - 1] == '\\' )
			whole[i][len - 1] = '\0';
	}
	return Q_stricmp( wholeA, wholeB ) ? qfalse : qtrue;
#else
	char	realA[ PATH_MAX ], realB[ PATH_MAX ];

	(void)wholeA; (void)wholeB;
	if ( !realpath( a, realA ) || !realpath( b, realB ) )
		return qfalse;
	return strcmp( realA, realB ) ? qfalse : qtrue;
#endif
}

void Sys_RestartOnExit( void ) {
	sysRestart.wanted = qtrue;
}

#ifdef _WIN32
/*
=================
Sys_RelaunchWithOutput

Starts the program again with the three standard handles of this one and no other handle: what this one writes
to goes on with the next one, a log somebody sent its output to has all of it.

"No other handle" is the point. A process that is started with "inherit handles" gets every handle of its
parent that can be inherited, and every file the C library opens can be. The first version of this did that:
the game had a pk3 open when it started its successor (the video of the main menu), the successor held that
pk3 without knowing it, and the updater in it could never give the file another name. qfalse: not started.
=================
*/
static qboolean Sys_RelaunchWithOutput( char *commandLine ) {
	const DWORD						which[3] = { STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE };
	static byte						listBuffer[ 512 ];
	LPPROC_THREAD_ATTRIBUTE_LIST	list = (LPPROC_THREAD_ATTRIBUTE_LIST)listBuffer;
	STARTUPINFOEXA					startup;
	PROCESS_INFORMATION				process;
	HANDLE							handles[3];
	DWORD							numHandles = 0, flags;
	SIZE_T							size = sizeof( listBuffer );
	BOOL							started;

	memset( &startup, 0, sizeof( startup ) );
	startup.StartupInfo.cb = sizeof( startup );
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	startup.StartupInfo.hStdInput = GetStdHandle( STD_INPUT_HANDLE );
	startup.StartupInfo.hStdOutput = GetStdHandle( STD_OUTPUT_HANDLE );
	startup.StartupInfo.hStdError = GetStdHandle( STD_ERROR_HANDLE );
	// The list takes a handle once, and only one that can be inherited. A game that was started by a double
	// click has none: then there is nothing to hand on.
	for ( int i = 0; i < 3; i++ ) {
		const HANDLE	h = GetStdHandle( which[i] );
		DWORD			n;

		if ( !h || h == INVALID_HANDLE_VALUE || !GetHandleInformation( h, &flags ) || !( flags & HANDLE_FLAG_INHERIT ) )
			continue;
		for ( n = 0; n < numHandles && handles[n] != h; n++ ) {
		}
		if ( n == numHandles )
			handles[numHandles++] = h;
	}
	if ( !numHandles || !InitializeProcThreadAttributeList( list, 1, 0, &size ) )
		return qfalse;
	started = FALSE;
	if ( UpdateProcThreadAttribute( list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, numHandles * sizeof( handles[0] ), NULL, NULL ) ) {
		startup.lpAttributeList = list;
		started = CreateProcessA( sysRestart.program, commandLine, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
			&startup.StartupInfo, &process );
	}
	DeleteProcThreadAttributeList( list );
	if ( !started )
		return qfalse;
	CloseHandle( process.hThread );
	CloseHandle( process.hProcess );
	return qtrue;
}
#endif

// qfalse: it could not be started. On every system but Windows this does not come back if it could.
static qboolean Sys_Relaunch( void ) {
	if ( !sysRestart.program[0] )
		return qfalse;
#ifdef _WIN32
	static char			commandLine[ 8192 ];
	const char			*given = GetCommandLineA();
	STARTUPINFOA		startup;
	PROCESS_INFORMATION	process;

	if ( strlen( given ) >= sizeof( commandLine ) )
		return qfalse;
	Q_strncpyz( commandLine, given, sizeof( commandLine ) );
	if ( Sys_RelaunchWithOutput( commandLine ) )
		return qtrue;
	// without its output, and with no handle of this one at all: that it starts counts for more
	Q_strncpyz( commandLine, given, sizeof( commandLine ) );
	memset( &startup, 0, sizeof( startup ) );
	startup.cb = sizeof( startup );
	if ( !CreateProcessA( sysRestart.program, commandLine, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process ) )
		return qfalse;
	CloseHandle( process.hThread );
	CloseHandle( process.hProcess );
	return qtrue;
#else
	execv( sysRestart.program, sysRestart.argv );
	return qfalse;
#endif
}
#endif

#if !defined(DEDICATED) && !defined(__EMSCRIPTEN__)
qboolean S_SpatialBakeRunning( void );	// client/spatial/snd_spatial.h
#endif

static void NORETURN Sys_Exit( int ex ) {
	IN_Shutdown();
#ifndef DEDICATED
	SDL_Quit();
#endif

	NET_Shutdown();

	Sys_PlatformExit();

	Com_ShutdownHunkMemory();
	Com_ShutdownZoneMemory();

	CON_Shutdown();

#if !defined(DEDICATED) && !defined(__EMSCRIPTEN__)
	// the last thing it does: the new one must not find this one still holding the window and the ports
	if ( sysRestart.wanted )
		Sys_Relaunch();

	// a bake of Steam Audio that was left to run (snd_spatial.cpp) would go on while exit() takes its library down
	if ( S_SpatialBakeRunning() ) {
		fflush( NULL );
		_exit( ex );
	}
#endif

    exit( ex );
}

/*
=================
Sys_CrashLogPath

The file for a crash log: crashlog-<time>.txt in fs_homepath (a profile of its own gets its own), before that is
set in the home folder of the game, or next to the program. The folder is made.
=================
*/
void Sys_CrashLogPath( char *path, int size )
{
	time_t rawtime;
	char timeStr[32] = {}; // should really only reach ~19 chars

	time( &rawtime );
	strftime( timeStr, sizeof( timeStr ), "%Y-%m-%d_%H-%M-%S", localtime( &rawtime ) ); // or gmtime
	const char *homePath = Cvar_VariableString( "fs_homepath" );
	if ( !homePath[0] )
		homePath = Sys_DefaultHomePath();
	const char *logPath = ( homePath && homePath[0] ) ? homePath : Sys_DefaultInstallPath();
	Com_sprintf( path, size, "%s%ccrashlog-%s.txt", logPath, PATH_SEP, timeStr );

	Sys_Mkdir( logPath );
}

#if defined(__EMSCRIPTEN__)
// no dialog and no crash log: the page shows the text, the console is in its log
static void Sys_ErrorDialog( const char *error )
{
	Sys_WebShowError( error );
}
#elif !defined(DEDICATED)
static void Sys_ErrorDialog( const char *error )
{
	char crashLogPath[MAX_OSPATH];

	Sys_CrashLogPath( crashLogPath, sizeof( crashLogPath ) );

	FILE *fp = fopen( crashLogPath, "w" );
	if ( fp )
	{
		ConsoleLogWriteOut( fp );
		fclose( fp );

		const char *errorMessage = va( "%s\n\nThe crash log was written to %s", error, crashLogPath );
		if ( SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, "Error", errorMessage, NULL ) < 0 )
		{
			fprintf( stderr, "%s", errorMessage );
		}
	}
	else
	{
		// Getting pretty desperate now
		ConsoleLogWriteOut( stderr );
		fflush( stderr );

		const char *errorMessage = va( "%s\nCould not write the crash log file, but we printed it to stderr.\n"
										"Try running the game using a command line interface.", error );
		if ( SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, "Error", errorMessage, NULL ) < 0 )
		{
			// We really have hit rock bottom here :(
			fprintf( stderr, "%s", errorMessage );
		}
	}
}
#endif

void NORETURN QDECL Sys_Error( const char *error, ... )
{
	va_list argptr;
	char    string[1024];

	va_start (argptr,error);
	Q_vsnprintf (string, sizeof(string), error, argptr);
	va_end (argptr);

	Sys_Print( string );

	// Only print Sys_ErrorDialog for client binary. The dedicated
	// server binary is meant to be a command line program so you would
	// expect to see the error printed.
#if !defined(DEDICATED)
	Sys_ErrorDialog( string );
#endif

	Sys_Exit( 3 );
}

void NORETURN Sys_Quit (void) {
    Sys_Exit(0);
}

/*
============
Sys_FileTime

returns -1 if not present
============
*/
time_t Sys_FileTime( const char *path )
{
	struct stat buf;

	if ( stat( path, &buf ) == -1 )
		return -1;

	return buf.st_mtime;
}

/*
=================
Sys_UnloadDll
=================
*/
void Sys_UnloadDll( void *dllHandle )
{
	if( !dllHandle )
	{
		Com_Printf("Sys_UnloadDll(NULL)\n");
		return;
	}

	Sys_UnloadLibrary(dllHandle);
}

/*
=================
Sys_LoadDll

First try to load library name from system library path,
from executable path, then fs_basepath.
=================
*/
void *Sys_LoadDll( const char *name, qboolean useSystemLib )
{
	void *dllhandle = NULL;

	// Don't load any DLLs that end with the pk3 extension
	if ( COM_CompareExtension( name, ".pk3" ) )
	{
		Com_Printf( S_COLOR_YELLOW "WARNING: Rejecting DLL named \"%s\"", name );
		return NULL;
	}

	if ( useSystemLib )
	{
		Com_Printf( "Trying to load \"%s\"...\n", name );

		dllhandle = Sys_LoadLibrary( name );
		if ( dllhandle )
			return dllhandle;

		Com_Printf( "%s(%s) failed: \"%s\"\n", __FUNCTION__, name, Sys_LibraryError() );
	}

	const char *binarypath = Sys_BinaryPath();
	const char *basepath = Cvar_VariableString( "fs_basepath" );

	if ( !*binarypath )
		binarypath = ".";

	const char *searchPaths[] = {
		binarypath,
		basepath,
	};
	const size_t numPaths = ARRAY_LEN( searchPaths );

	for ( size_t i = 0; i < numPaths; i++ )
	{
		const char *libDir = searchPaths[i];
		if ( !libDir[0] )
			continue;

		Com_Printf( "Trying to load \"%s\" from \"%s\"...\n", name, libDir );
		char *fn = va( "%s%c%s", libDir, PATH_SEP, name );
		dllhandle = Sys_LoadLibrary( fn );
		if ( dllhandle )
			return dllhandle;

		Com_Printf( "%s(%s) failed: \"%s\"\n", __FUNCTION__, fn, Sys_LibraryError() );
	}
	return NULL;
}

#if defined(MACOS_X) && !defined(_JK2EXE)
void *Sys_LoadMachOBundle( const char *name )
{
	if ( !FS_LoadMachOBundle(name) )
		return NULL;

	char *homepath = Cvar_VariableString( "fs_homepath" );
	char *gamedir = Cvar_VariableString( "fs_game" );
	char dllName[MAX_QPATH];

	Com_sprintf( dllName, sizeof(dllName), "%s_pk3" DLL_EXT, name );

	//load the unzipped library
	char *fn = FS_BuildOSPath( homepath, gamedir, dllName );

	void    *libHandle = Sys_LoadLibrary( fn );

	if ( libHandle != NULL ) {
		Com_Printf( "Loaded pk3 bundle %s.\n", name );
	}

	return libHandle;
}
#endif

enum SearchPathFlag
{
	SEARCH_PATH_MOD		= 1 << 0,
	SEARCH_PATH_BASE	= 1 << 1,
	SEARCH_PATH_VISIONJK	= 1 << 2,
	SEARCH_PATH_ROOT	= 1 << 3
};

// One of the folders a module is looked for in did not give it. A folder that has no such file is the usual
// case (the folder of the player comes first and has none) and no failure: the next one is tried.
static void Sys_DllFailed( const char *callerName, const char *fn )
{
	const char *why = Sys_LibraryError();
	FILE *f = fopen( fn, "rb" );

	if ( !f )
	{
		Com_DPrintf( "%s: no %s\n", callerName, fn );
		return;
	}
	fclose( f );
	Com_Printf( "%s(%s) failed: \"%s\"\n", callerName, fn, why );
}

static void *Sys_LoadDllFromPaths( const char *filename, const char *gamedir, const char **searchPaths,
									size_t numPaths, uint32_t searchFlags, const char *callerName )
{
	char *fn;
	void *libHandle;

	if ( searchFlags & SEARCH_PATH_MOD )
	{
		for ( size_t i = 0; i < numPaths; i++ )
		{
			const char *libDir = searchPaths[i];
			if ( !libDir[0] )
				continue;

			fn = FS_BuildOSPath( libDir, gamedir, filename );
			libHandle = Sys_LoadLibrary( fn );
			if ( libHandle )
				return libHandle;

			Sys_DllFailed( callerName, fn );
		}
	}

	if ( searchFlags & SEARCH_PATH_BASE )
	{
		for ( size_t i = 0; i < numPaths; i++ )
		{
			const char *libDir = searchPaths[i];
			if ( !libDir[0] )
				continue;

			fn = FS_BuildOSPath( libDir, BASEGAME, filename );
			libHandle = Sys_LoadLibrary( fn );
			if ( libHandle )
				return libHandle;

			Sys_DllFailed( callerName, fn );
		}
	}

	if ( searchFlags & SEARCH_PATH_VISIONJK )
	{
		for ( size_t i = 0; i < numPaths; i++ )
		{
			const char *libDir = searchPaths[i];
			if ( !libDir[0] )
				continue;

			fn = FS_BuildOSPath( libDir, VISIONJKGAME, filename );
			libHandle = Sys_LoadLibrary( fn );
			if ( libHandle )
				return libHandle;

			Sys_DllFailed( callerName, fn );
		}
	}

	if ( searchFlags & SEARCH_PATH_ROOT )
	{
		for ( size_t i = 0; i < numPaths; i++ )
		{
			const char *libDir = searchPaths[i];
			if ( !libDir[0] )
				continue;

			fn = va( "%s%c%s", libDir, PATH_SEP, filename );
			libHandle = Sys_LoadLibrary( fn );
			if ( libHandle )
				return libHandle;

			Sys_DllFailed( callerName, fn );
		}
	}

	return NULL;
}

static void FreeUnpackDLLResult(UnpackDLLResult *result)
{
	if ( result->tempDLLPath )
		Z_Free((void *)result->tempDLLPath);
}

void *Sys_LoadLegacyGameDll( const char *name, VMMainProc **vmMain, SystemCallProc *systemcalls )
{
	void	*libHandle = NULL;
	char	filename[MAX_OSPATH];

	Com_sprintf (filename, sizeof(filename), "%s" ARCH_STRING DLL_EXT, name);

#if defined(_DEBUG)
	libHandle = Sys_LoadLibrary( filename );
	if ( !libHandle )
#endif
	{
		if ( com_unpackLibraries->integer ) {
			UnpackDLLResult unpackResult = Sys_UnpackDLL(filename);
			if ( !unpackResult.succeeded )
			{
				if ( Sys_DLLNeedsUnpacking() )
				{
					FreeUnpackDLLResult(&unpackResult);
					Com_DPrintf( "Sys_LoadLegacyGameDll: Failed to unpack %s from PK3.\n", filename );
					return NULL;
				}
			}
			else
			{
				libHandle = Sys_LoadLibrary(unpackResult.tempDLLPath);
			}

			FreeUnpackDLLResult(&unpackResult);
		}

		if ( !libHandle )
		{
#if defined(MACOS_X) && !defined(_JK2EXE)
			//First, look for the old-style mac .bundle that's inside a pk3
			//It's actually zipped, and the zipfile has the same name as 'name'
			libHandle = Sys_LoadMachOBundle( name );
#endif

			if (!libHandle) {
				char *basepath = Cvar_VariableString( "fs_basepath" );
				char *homepath = Cvar_VariableString( "fs_homepath" );
				char *cdpath = Cvar_VariableString( "fs_cdpath" );
				char *gamedir = Cvar_VariableString( "fs_game" );
		#ifdef MACOS_X
				char *apppath = Cvar_VariableString( "fs_apppath" );
		#endif

				const char *searchPaths[] = {
					homepath,
		#ifdef MACOS_X
					apppath,
		#endif
					basepath,
					cdpath,
				};
				size_t numPaths = ARRAY_LEN( searchPaths );

				libHandle = Sys_LoadDllFromPaths( filename, gamedir, searchPaths, numPaths, SEARCH_PATH_BASE | SEARCH_PATH_MOD, __FUNCTION__ );
				if ( !libHandle )
					return NULL;
			}
		}
	}

	typedef void QDECL DllEntryProc( SystemCallProc *syscallptr );

	DllEntryProc *dllEntry = (DllEntryProc *)Sys_LoadFunction( libHandle, "dllEntry" );
	*vmMain = (VMMainProc *)Sys_LoadFunction( libHandle, "vmMain" );

	if ( !*vmMain || !dllEntry ) {
		Com_DPrintf ( "Sys_LoadLegacyGameDll(%s) failed to find vmMain function:\n...%s!\n", name, Sys_LibraryError() );
		Sys_UnloadLibrary( libHandle );
		return NULL;
	}

	Com_DPrintf ( "Sys_LoadLegacyGameDll(%s) found vmMain function at 0x%" PRIxPTR "\n", name, *vmMain );
	dllEntry( systemcalls );

	return libHandle;
}

void *Sys_LoadSPGameDll( const char *name, GetGameAPIProc **GetGameAPI )
{
	void	*libHandle = NULL;
	char	filename[MAX_OSPATH];

	assert( GetGameAPI );

	Com_sprintf (filename, sizeof(filename), "%s" ARCH_STRING DLL_EXT, name);

#if defined(MACOS_X) && !defined(_JK2EXE)
    //First, look for the old-style mac .bundle that's inside a pk3
    //It's actually zipped, and the zipfile has the same name as 'name'
    libHandle = Sys_LoadMachOBundle( filename );
#endif

	if (!libHandle) {
		char *basepath = Cvar_VariableString( "fs_basepath" );
		char *homepath = Cvar_VariableString( "fs_homepath" );
		char *cdpath = Cvar_VariableString( "fs_cdpath" );
		char *gamedir = Cvar_VariableString( "fs_game" );
#ifdef MACOS_X
        char *apppath = Cvar_VariableString( "fs_apppath" );
#endif

		const char *searchPaths[] = {
			homepath,
#ifdef MACOS_X
			apppath,
#endif
			basepath,
			cdpath,
		};
		size_t numPaths = ARRAY_LEN( searchPaths );

		libHandle = Sys_LoadDllFromPaths( filename, gamedir, searchPaths, numPaths,
											SEARCH_PATH_BASE | SEARCH_PATH_MOD | SEARCH_PATH_VISIONJK | SEARCH_PATH_ROOT,
											__FUNCTION__ );
		if ( !libHandle )
			return NULL;
	}

	*GetGameAPI = (GetGameAPIProc *)Sys_LoadFunction( libHandle, "GetGameAPI" );
	if ( !*GetGameAPI ) {
		Com_DPrintf ( "%s(%s) failed to find GetGameAPI function:\n...%s!\n", __FUNCTION__, name, Sys_LibraryError() );
		Sys_UnloadLibrary( libHandle );
		return NULL;
	}

	return libHandle;
}

void *Sys_LoadGameDll( const char *name, GetModuleAPIProc **moduleAPI )
{
	void	*libHandle = NULL;
	char	filename[MAX_OSPATH];

	Com_sprintf (filename, sizeof(filename), "%s" ARCH_STRING DLL_EXT, name);

#if defined(_DEBUG)
	libHandle = Sys_LoadLibrary( filename );
	if ( !libHandle )
#endif
	{
		if ( com_unpackLibraries->integer ) {
			UnpackDLLResult unpackResult = Sys_UnpackDLL(filename);
			if ( !unpackResult.succeeded )
			{
				if ( Sys_DLLNeedsUnpacking() )
				{
					FreeUnpackDLLResult(&unpackResult);
					Com_DPrintf( "Sys_LoadLegacyGameDll: Failed to unpack %s from PK3.\n", filename );
					return NULL;
				}
			}
			else
			{
				libHandle = Sys_LoadLibrary(unpackResult.tempDLLPath);
			}

			FreeUnpackDLLResult(&unpackResult);
		}

		if ( !libHandle )
		{
#if defined(MACOS_X) && !defined(_JK2EXE)
			//First, look for the old-style mac .bundle that's inside a pk3
			//It's actually zipped, and the zipfile has the same name as 'name'
			libHandle = Sys_LoadMachOBundle( name );
#endif

			if (!libHandle) {
				char *basepath = Cvar_VariableString( "fs_basepath" );
				char *homepath = Cvar_VariableString( "fs_homepath" );
				char *cdpath = Cvar_VariableString( "fs_cdpath" );
				char *gamedir = Cvar_VariableString( "fs_game" );
#ifdef MACOS_X
				char *apppath = Cvar_VariableString( "fs_apppath" );
#endif

				const char *searchPaths[] = {
					homepath,
#ifdef MACOS_X
					apppath,
#endif
					basepath,
					cdpath,
				};
				size_t numPaths = ARRAY_LEN( searchPaths );

				libHandle = Sys_LoadDllFromPaths( filename, gamedir, searchPaths, numPaths, SEARCH_PATH_BASE | SEARCH_PATH_MOD, __FUNCTION__ );
				if ( !libHandle )
					return NULL;
			}
		}
	}

	*moduleAPI = (GetModuleAPIProc *)Sys_LoadFunction( libHandle, "GetModuleAPI" );
	if ( !*moduleAPI ) {
		Com_DPrintf ( "Sys_LoadGameDll(%s) failed to find GetModuleAPI function:\n...%s!\n", name, Sys_LibraryError() );
		Sys_UnloadLibrary( libHandle );
		return NULL;
	}

	return libHandle;
}

// A signal that asks the program to end, for the main loop: it quits there at the start of the next frame. The
// handler runs in whichever thread got the signal, and a shutdown in there pulled the zone from under a frame of
// the main thread (2026-10-14: "Z_Free(): Corrupt zone header!", then the process hung).
static volatile sig_atomic_t sys_quitSignal;

/*
=================
Sys_SigHandler
=================
*/
void Sys_SigHandler( int signal )
{
	static qboolean signalcaught = qfalse;

#ifndef _WIN32
	if ( signal == SIGHUP || signal == SIGQUIT || signal == SIGTERM || signal == SIGINT ) {
		if ( sys_quitSignal ) {
			// a second one: the frame that was to quit does not come, so nothing is shut down
			_exit( 1 );
		}
		sys_quitSignal = signal;
		return;
	}
#endif

	if( signalcaught )
	{
		fprintf( stderr, "DOUBLE SIGNAL FAULT: Received signal %d, exiting...\n",
			signal );
	}
	else
	{
		signalcaught = qtrue;
		//VM_Forced_Unload_Start();
#ifndef DEDICATED
		CL_Shutdown();
		//CL_Shutdown(va("Received signal %d", signal), qtrue, qtrue);
#endif
		SV_Shutdown(va("Received signal %d", signal) );
		//VM_Forced_Unload_Done();
	}

	if( signal == SIGTERM || signal == SIGINT )
		Sys_Exit( 1 );
	else
		Sys_Exit( 2 );
}

#ifdef MACOS_X
/*
 =================
 Sys_StripAppBundle

 Discovers if passed dir is suffixed with the directory structure of a Mac OS X
 .app bundle. If it is, the .app directory structure is stripped off the end and
 the result is returned. If not, dir is returned untouched.
 =================
 */
char *Sys_StripAppBundle( char *dir )
{
	static char cwd[MAX_OSPATH];

	Q_strncpyz(cwd, dir, sizeof(cwd));
	if(strcmp(Sys_Basename(cwd), "MacOS"))
		return dir;
	Q_strncpyz(cwd, Sys_Dirname(cwd), sizeof(cwd));
	if(strcmp(Sys_Basename(cwd), "Contents"))
		return dir;
	Q_strncpyz(cwd, Sys_Dirname(cwd), sizeof(cwd));
	if(!strstr(Sys_Basename(cwd), ".app"))
		return dir;
	Q_strncpyz(cwd, Sys_Dirname(cwd), sizeof(cwd));
	return cwd;
}
#endif

#ifndef DEFAULT_BASEDIR
#	ifdef MACOS_X
#		define DEFAULT_BASEDIR Sys_StripAppBundle(Sys_BinaryPath())
#	else
#		define DEFAULT_BASEDIR Sys_BinaryPath()
#	endif
#endif

int main ( int argc, char* argv[] )
{
	int		i;
	char	commandLine[ MAX_STRING_CHARS ] = { 0 };

	Sys_PlatformInit( argc, argv );
	CON_Init();

	// get the initial time base
	Sys_Milliseconds();

#ifdef MACOS_X
	// This is passed if we are launched by double-clicking
	if ( argc >= 2 && Q_strncmp ( argv[1], "-psn", 4 ) == 0 )
		argc = 1;
#endif

	Sys_SetBinaryPath( Sys_Dirname( argv[ 0 ] ) );
	Sys_SetDefaultInstallPath( DEFAULT_BASEDIR );

#if !defined(DEDICATED) && !defined(__EMSCRIPTEN__)
	Sys_RestartInit( argv );
	// A swap of files by the updater that was cut short is finished here, before a renderer or a module is
	// loaded. The program that runs may be the old one then, so it starts the new one in its place.
	if ( CL_UpdateResume() && Sys_Relaunch() )
		return 0;
#endif

	// Concatenate the command line for passing to Com_Init
	for( i = 1; i < argc; i++ )
	{
		// An argument with a space is one word, and so is one with two slashes in it: the console takes those
		// as the start of a comment and would cut a web address off behind "https:" (+set v_updateURL).
		const bool containsSpaces = (strchr(argv[i], ' ') != NULL || strstr(argv[i], "//") != NULL || strstr(argv[i], "/*") != NULL);
		if (containsSpaces)
			Q_strcat( commandLine, sizeof( commandLine ), "\"" );

		Q_strcat( commandLine, sizeof( commandLine ), argv[ i ] );

		if (containsSpaces)
			Q_strcat( commandLine, sizeof( commandLine ), "\"" );

		Q_strcat( commandLine, sizeof( commandLine ), " " );
	}

	Com_Init (commandLine);

#ifndef DEDICATED
	SDL_version compiled;
	SDL_version linked;

	SDL_VERSION( &compiled );
	SDL_GetVersion( &linked );

	Com_Printf( "SDL Version Compiled: %d.%d.%d\n", compiled.major, compiled.minor, compiled.patch );
	Com_Printf( "SDL Version Linked: %d.%d.%d\n", linked.major, linked.minor, linked.patch );
#endif

#ifdef __EMSCRIPTEN__
	// a page has one thread and no loop of its own: the frames come when it is about to paint
	Sys_WebMainLoop();
#else
	// main game loop
	while (1)
	{
		if ( com_busyWait->integer )
		{
			bool shouldSleep = false;

#if !defined(_JK2EXE)
			if ( com_dedicated->integer )
			{
				shouldSleep = true;
			}
#endif

			if ( com_minimized->integer )
			{
				shouldSleep = true;
			}

			if ( shouldSleep )
			{
				Sys_Sleep( 5 );
			}
		}

		if ( sys_quitSignal ) {
			Com_Printf( "Received signal %d, quitting\n", (int)sys_quitSignal );
			Cbuf_ExecuteText( EXEC_NOW, "quit\n" );
		}

		// run the game
		Com_Frame();
	}
#endif

	// never gets here
	return 0;
}
