/*
===========================================================================
Copyright (C) 2005 - 2015, ioquake3 contributors
Copyright (C) 2013 - 2015, OpenJK contributors

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

#include <dirent.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <libgen.h>
#include <sched.h>
#include <signal.h>
#include <sys/resource.h>

#include "qcommon/q_version.h"
#include "qcommon/qcommon.h"
#include "qcommon/q_shared.h"
#include "sys_local.h"
#include "con_local.h"
#include "qcommon/game_version.h"
#include <pthread.h>
#if defined(__GLIBC__) || defined(__APPLE__)
#include <execinfo.h>
#define SYS_BACKTRACE
#endif

qboolean stdinIsATTY = qfalse;

// Used to determine where to store user-specific files
static char homePath[ MAX_OSPATH ] = { 0 };

// Max open file descriptors. Mostly used by pk3 files with
// MAX_SEARCH_PATHS limit.
#define MAX_OPEN_FILES	4096

/*
=================
Sys_CrashHandler

A crash (a bad address, a bad instruction, abort()): the crash log, as Sys_Error() writes one, with what crashed
and where, then the crash goes on as it would have, so the system still gets its core (coredumpctl). Before
2026-10-19 only Sys_Error() wrote one, and a segfault left nothing.
The names in the stack are the exported ones only: an offset in a file is turned into a function with
addr2line -f -C -e <file> <offset>, on the same build.
=================
*/
static void Sys_CrashHandler( int sig, siginfo_t *info, void *context )
{
	static volatile sig_atomic_t crashed;

	if ( !crashed ) {
		char path[MAX_OSPATH], thread[32] = "";
		FILE *fp;

		crashed = 1;
		Sys_CrashLogPath( path, sizeof( path ) );
		fp = fopen( path, "w" );
		if ( fp ) {
			pthread_getname_np( pthread_self(), thread, sizeof( thread ) );
			fprintf( fp, "%s %s crashed: signal %d (%s), address %p, thread \"%s\"\n\n", JK_VERSION, PLATFORM_STRING, sig,
				strsignal( sig ), info ? info->si_addr : NULL, thread );
#ifdef SYS_BACKTRACE
			void *frames[64];
			const int numFrames = backtrace( frames, ARRAY_LEN( frames ) );

			fprintf( fp, "stack (a function of an offset: addr2line -f -C -e <file> <offset>):\n" );
			fflush( fp );
			backtrace_symbols_fd( frames, numFrames, fileno( fp ) );
#endif
			fprintf( fp, "\nconsole:\n" );
			ConsoleLogWriteOut( fp );
			fclose( fp );
			fprintf( stderr, "\nCrashed (signal %d), the crash log was written to %s\n", sig, path );
		}
	}

	// SA_RESETHAND gave the signal its default back: a fault happens again when this returns and ends the program
	// with a core. One that was sent (kill, raise) is sent again.
	if ( !info || info->si_code <= 0 ) {
		raise( sig );
	}
}

static void Sys_CrashHandlers( void )
{
	static const int signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
	static byte altStack[64 * 1024];	// a stack that ran over still gets its crash log (in the thread of the game)
	stack_t stack;
	struct sigaction action;

#ifdef SYS_BACKTRACE
	// the first backtrace() loads what it needs, which is not to be done in the handler
	void *frame;
	backtrace( &frame, 1 );
#endif

	memset( &stack, 0, sizeof( stack ) );
	stack.ss_sp = altStack;
	stack.ss_size = sizeof( altStack );
	sigaltstack( &stack, NULL );

	memset( &action, 0, sizeof( action ) );
	action.sa_sigaction = Sys_CrashHandler;
	action.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_NODEFER | SA_ONSTACK;
	sigemptyset( &action.sa_mask );
	for ( size_t i = 0; i < ARRAY_LEN( signals ); i++ ) {
		sigaction( signals[i], &action, NULL );
	}
}

void Sys_PlatformInit( int argc, char *argv[] )
{
	const char* term = getenv( "TERM" );

#ifdef DEDICATED
	signal( SIGHUP, Sys_SigHandler );
#else
	// the client has its window: it plays on when the terminal it was started from closes (CON_Input() lets go of it)
	signal( SIGHUP, SIG_IGN );
#endif
	signal( SIGQUIT, Sys_SigHandler );
	signal( SIGTRAP, Sys_SigHandler );
#ifndef __EMSCRIPTEN__
	Sys_CrashHandlers();
#endif

	if (isatty( STDIN_FILENO ) && !( term && ( !strcmp( term, "raw" ) || !strcmp( term, "dumb" ) ) ))
		stdinIsATTY = qtrue;
	else
		stdinIsATTY = qfalse;

#ifndef __EMSCRIPTEN__	// a page has no such limit to raise
	// raise open file limit to allow more pk3 files
	int retval;
	struct rlimit rlim;
	rlim_t maxfds = MAX_OPEN_FILES;

	for (int i = 1; i + 1 < argc; i++) {
		if (!Q_stricmp(argv[i], "-maxfds")) {
			maxfds = atoi(argv[i + 1]);
		}
	}

	getrlimit(RLIMIT_NOFILE, &rlim);
	rlim.rlim_cur = Q_min(maxfds, rlim.rlim_max);
	retval = setrlimit(RLIMIT_NOFILE, &rlim);

	if (retval == -1) {
		Com_Printf("Warning: Failed to raise open file limit. %s\n", strerror(errno));
	}
#endif
}

void Sys_PlatformExit( void )
{
}

/*
================
Sys_Milliseconds
================
*/
/* base time in seconds, that's our origin
   timeval:tv_sec is an int:
   assuming this wraps every 0x7fffffff - ~68 years since the Epoch (1970) - we're safe till 2038 */
unsigned long sys_timeBase = 0;
/* current time in ms, using sys_timeBase as origin
   NOTE: sys_timeBase*1000 + curtime -> ms since the Epoch
     0x7fffffff ms - ~24 days
   although timeval:tv_usec is an int, I'm not sure wether it is actually used as an unsigned int
     (which would affect the wrap period) */
int curtime;
int Sys_Milliseconds (bool baseTime)
{
	struct timeval tp;

	gettimeofday(&tp, NULL);

	if (!sys_timeBase)
	{
		sys_timeBase = tp.tv_sec;
		return tp.tv_usec/1000;
	}

	curtime = (tp.tv_sec - sys_timeBase)*1000 + tp.tv_usec/1000;

    static int sys_timeBase = curtime;
	if (!baseTime)
	{
		curtime -= sys_timeBase;
	}

	return curtime;
}

int Sys_Milliseconds2( void )
{
    return Sys_Milliseconds(false);
}

/*
==================
Sys_RandomBytes
==================
*/
bool Sys_RandomBytes( byte *string, int len )
{
	FILE *fp;

	fp = fopen( "/dev/urandom", "r" );
	if( !fp )
		return false;

	setvbuf( fp, NULL, _IONBF, 0 ); // don't buffer reads from /dev/urandom

	if( !fread( string, sizeof( byte ), len, fp ) )
	{
		fclose( fp );
		return false;
	}

	fclose( fp );
	return true;
}

/*
 ==================
 Sys_GetCurrentUser
 ==================
 */
char *Sys_GetCurrentUser( void )
{
	struct passwd *p;

	if ( (p = getpwuid( getuid() )) == NULL ) {
		return "player";
	}
	return p->pw_name;
}

#define MEM_THRESHOLD 96*1024*1024

/*
==================
Sys_LowPhysicalMemory

TODO
==================
*/
qboolean Sys_LowPhysicalMemory( void )
{
	return qfalse;
}

/*
==================
Sys_Basename
==================
*/
const char *Sys_Basename( const char *path )
{
	static char buf[ MAX_OSPATH ];
	Q_strncpyz( buf, path, sizeof(buf) );
	return basename( buf );
}

/*
==================
Sys_Dirname
==================
*/
const char *Sys_Dirname( const char *path )
{
	static char buf[ MAX_OSPATH ];
	Q_strncpyz( buf, path, sizeof(buf) );
	return dirname( buf );
}

/*
==============================================================

DIRECTORY SCANNING

==============================================================
*/

#define MAX_FOUND_FILES 0x1000

/*
==================
Sys_ListFiles
==================
*/
void Sys_ListFilteredFiles( const char *basedir, char *subdirs, char *filter, char **list, int *numfiles ) {
	char		search[MAX_OSPATH], newsubdirs[MAX_OSPATH];
	char		filename[MAX_OSPATH];
	DIR			*fdir;
	struct dirent *d;
	struct stat st;

	if ( *numfiles >= MAX_FOUND_FILES - 1 ) {
		return;
	}

	if (strlen(subdirs)) {
		Com_sprintf( search, sizeof(search), "%s/%s", basedir, subdirs );
	}
	else {
		Com_sprintf( search, sizeof(search), "%s", basedir );
	}

	if ((fdir = opendir(search)) == NULL) {
		return;
	}

	while ((d = readdir(fdir)) != NULL) {
		Com_sprintf(filename, sizeof(filename), "%s/%s", search, d->d_name);
		if (stat(filename, &st) == -1)
			continue;

		if (st.st_mode & S_IFDIR) {
			if (Q_stricmp(d->d_name, ".") && Q_stricmp(d->d_name, "..")) {
				if (strlen(subdirs)) {
					Com_sprintf( newsubdirs, sizeof(newsubdirs), "%s/%s", subdirs, d->d_name);
				}
				else {
					Com_sprintf( newsubdirs, sizeof(newsubdirs), "%s", d->d_name);
				}
				Sys_ListFilteredFiles( basedir, newsubdirs, filter, list, numfiles );
			}
		}
		if ( *numfiles >= MAX_FOUND_FILES - 1 ) {
			break;
		}
		Com_sprintf( filename, sizeof(filename), "%s/%s", subdirs, d->d_name );
		if (!Com_FilterPath( filter, filename, qfalse ))
			continue;
		list[ *numfiles ] = CopyString( filename );
		(*numfiles)++;
	}

	closedir(fdir);
}

char **Sys_ListFiles( const char *directory, const char *extension, char *filter, int *numfiles, qboolean wantsubs )
{
	struct dirent *d;
	DIR		*fdir;
	qboolean dironly = wantsubs;
	char		search[MAX_OSPATH];
	int			nfiles;
	char		**listCopy;
	char		*list[MAX_FOUND_FILES];
	int			i;
	struct stat st;

	if (filter) {

		nfiles = 0;
		Sys_ListFilteredFiles( directory, "", filter, list, &nfiles );

		list[ nfiles ] = 0;
		*numfiles = nfiles;

		if (!nfiles)
			return NULL;

		listCopy = (char **)Z_Malloc( ( nfiles + 1 ) * sizeof( *listCopy ), TAG_LISTFILES, qfalse );
		for ( i = 0 ; i < nfiles ; i++ ) {
			listCopy[i] = list[i];
		}
		listCopy[i] = NULL;

		return listCopy;
	}

	if ( !extension)
		extension = "";

	if ( extension[0] == '/' && extension[1] == 0 ) {
		extension = "";
		dironly = qtrue;
	}

	size_t extLen = strlen( extension );

	// search
	nfiles = 0;

	if ((fdir = opendir(directory)) == NULL) {
		*numfiles = 0;
		return NULL;
	}

	while ((d = readdir(fdir)) != NULL) {
		Com_sprintf(search, sizeof(search), "%s/%s", directory, d->d_name);
		if (stat(search, &st) == -1)
			continue;
		if ((dironly && !(st.st_mode & S_IFDIR)) ||
			(!dironly && (st.st_mode & S_IFDIR)))
			continue;

		if (*extension) {
			if ( strlen( d->d_name ) < extLen ||
				Q_stricmp(
					d->d_name + strlen( d->d_name ) - extLen,
					extension ) ) {
				continue; // didn't match
			}
		}

		if ( nfiles == MAX_FOUND_FILES - 1 )
			break;
		list[ nfiles ] = CopyString( d->d_name );
		nfiles++;
	}

	list[ nfiles ] = 0;

	closedir(fdir);

	// return a copy of the list
	*numfiles = nfiles;

	if ( !nfiles ) {
		return NULL;
	}

	listCopy = (char **)Z_Malloc( ( nfiles + 1 ) * sizeof( *listCopy ), TAG_LISTFILES, qfalse );
	for ( i = 0 ; i < nfiles ; i++ ) {
		listCopy[i] = list[i];
	}
	listCopy[i] = NULL;

	return listCopy;
}

void	Sys_FreeFileList( char **fileList ) {
	int		i;

	if ( !fileList ) {
		return;
	}

	for ( i = 0 ; fileList[i] ; i++ ) {
		Z_Free( fileList[i] );
	}

	Z_Free( fileList );
}

/*
==================
Sys_Sleep

Block execution for msec or until input is recieved.
==================
*/
void Sys_Sleep( int msec )
{
	if( msec == 0 )
		return;

#ifdef __EMSCRIPTEN__
	// a page has one thread, a wait would stop the page with it
	return;
#endif

	if( stdinIsATTY )
	{
		fd_set fdset;

		FD_ZERO(&fdset);
		FD_SET(STDIN_FILENO, &fdset);
		if( msec < 0 )
		{
			select(STDIN_FILENO + 1, &fdset, NULL, NULL, NULL);
		}
		else
		{
			struct timeval timeout;

			timeout.tv_sec = msec/1000;
			timeout.tv_usec = (msec%1000)*1000;
			select(STDIN_FILENO + 1, &fdset, NULL, NULL, &timeout);
		}
	}
	else
	{
		// With nothing to select() on, we can't wait indefinitely
		if( msec < 0 )
			msec = 10;

		usleep( msec * 1000 );
	}
}

/*
==================
Sys_Mkdir
==================
*/
qboolean Sys_Mkdir( const char *path )
{
	int result = mkdir( path, 0750 );

	if( result != 0 )
		return (qboolean)(errno == EEXIST);

	return qtrue;
}

char *Sys_Cwd( void )
{
	static char cwd[MAX_OSPATH];

	if ( getcwd( cwd, sizeof( cwd ) - 1 ) == NULL )
		cwd[0] = '\0';
	else
		cwd[MAX_OSPATH-1] = '\0';

	return cwd;
}

/* Resolves path names and determines if they are the same */
/* For use with full OS paths not quake paths */
/* Returns true if resulting paths are valid and the same, otherwise false */
bool Sys_PathCmp( const char *path1, const char *path2 )
{
	char *r1, *r2;

	r1 = realpath(path1, NULL);
	r2 = realpath(path2, NULL);

	if(r1 && r2 && !Q_stricmp(r1, r2))
	{
		free(r1);
		free(r2);
		return true;
	}

	free(r1);
	free(r2);
	return false;
}

/*
==================
Sys_DefaultHomePath
==================
*/
#if defined(BUILD_PORTABLE)
char *Sys_DefaultHomePath(void)
{
	return NULL;
}
#elif defined(MACOS_X)
char *Sys_DefaultHomePath(void)
{
	char *p;

	if ( !homePath[0] )
	{
		if ( (p = getenv( "HOME" )) != NULL )
		{
			Com_sprintf( homePath, sizeof( homePath ), "%s%c", p, PATH_SEP );
			Q_strcat( homePath, sizeof( homePath ), "Library/Application Support/" );

			if ( com_homepath && com_homepath->string[0] )
				Q_strcat( homePath, sizeof( homePath ), com_homepath->string );
			else
				Q_strcat( homePath, sizeof( homePath ), HOMEPATH_NAME_MACOSX );
		}
	}

	return homePath;
}
#else
char *Sys_DefaultHomePath(void)
{
	char *p;

	if ( !homePath[0] )
	{
		if ( (p = getenv( "XDG_DATA_HOME" )) != NULL )
		{
			Com_sprintf( homePath, sizeof( homePath ), "%s%c", p, PATH_SEP );
			if ( com_homepath && com_homepath->string[0] )
				Q_strcat( homePath, sizeof( homePath ), com_homepath->string );
			else
				Q_strcat( homePath, sizeof( homePath ), HOMEPATH_NAME_UNIX );

			return homePath;
		}

		if ( (p = getenv( "HOME" )) != NULL )
		{
			Com_sprintf( homePath, sizeof( homePath ), "%s%c.local%cshare%c", p, PATH_SEP, PATH_SEP, PATH_SEP );
			if ( com_homepath && com_homepath->string[0] )
				Q_strcat( homePath, sizeof( homePath ), com_homepath->string );
			else
				Q_strcat( homePath, sizeof( homePath ), HOMEPATH_NAME_UNIX );

			return homePath;
		}
	}

	return homePath;
}
#endif

void Sys_SetProcessorAffinity( void ) {
#if defined(__linux__)
	uint32_t cores;

	if ( sscanf( com_affinity->string, "%X", &cores ) != 1 )
		cores = 1; // set to first core only

	const long numCores = sysconf( _SC_NPROCESSORS_ONLN );
	if ( !cores )
		cores = (1 << numCores) - 1; // use all cores

	cpu_set_t set;
	CPU_ZERO( &set );
	for ( int i = 0; i < numCores; i++ ) {
		if ( cores & (1<<i) ) {
			CPU_SET( i, &set );
		}
	}

	sched_setaffinity( 0, sizeof( set ), &set );
#elif defined(MACOS_X)
	//TODO: Apple's APIs for this are weird but exist on a per-thread level. Good enough for us.
#endif
}

UnpackDLLResult Sys_UnpackDLL(const char *name)
{
	return UnpackDLLResult();
}

bool Sys_DLLNeedsUnpacking()
{
	return false;
}


/*
=================
Sys_AnsiColorPrint

Transform Q3 colour codes to ANSI escape sequences
=================
*/
void Sys_AnsiColorPrint( const char *msg )
{
	static char buffer[MAXPRINTMSG];
	int         length = 0;
	static int  q3ToAnsi[Q_COLOR_BITS+1] =
	{
		30, // COLOR_BLACK
		31, // COLOR_RED
		32, // COLOR_GREEN
		33, // COLOR_YELLOW
		34, // COLOR_BLUE
		36, // COLOR_CYAN
		35, // COLOR_MAGENTA
		0,  // COLOR_WHITE
		33, // COLOR_ORANGE // FIXME
		30, // COLOR_GREY
	};

	while ( *msg )
	{
		if ( Q_IsColorStringExt( msg ) || *msg == '\n' )
		{
			// First empty the buffer
			if ( length > 0 )
			{
				buffer[length] = '\0';
				fputs( buffer, stderr );
				length = 0;
			}

			if ( *msg == '\n' )
			{
				// Issue a reset and then the newline
				fputs( "\033[0m\n", stderr );
				msg++;
			}
			else
			{
				// Print the color code
				Com_sprintf( buffer, sizeof( buffer ), "\033[%dm",
					q3ToAnsi[ColorIndex( *(msg + 1) )] );
				fputs( buffer, stderr );
				msg += 2;
			}
		}
		else
		{
			if ( length >= MAXPRINTMSG - 1 )
				break;

			buffer[length] = *msg;
			length++;
			msg++;
		}
	}

	// Empty anything still left in the buffer
	if ( length > 0 )
	{
		buffer[length] = '\0';
		fputs( buffer, stderr );
	}
}
