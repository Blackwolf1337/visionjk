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

#include "qcommon/game_version.h"
#include "sys_local.h"
#include "sys_memwatch.h"
#include "con_local.h"
#include <direct.h>
#include <io.h>
#include <shlobj.h>
#include <windows.h>
#include <tlhelp32.h>

#define MEM_THRESHOLD (128*1024*1024)

// Used to determine where to store user-specific files
static char homePath[ MAX_OSPATH ] = { 0 };

static UINT timerResolution = 0;

/*
==============
Sys_Basename
==============
*/
const char *Sys_Basename( const char *path )
{
	static char base[ MAX_OSPATH ] = { 0 };
	int length;

	length = strlen( path ) - 1;

	// Skip trailing slashes
	while( length > 0 && path[ length ] == '\\' )
		length--;

	while( length > 0 && path[ length - 1 ] != '\\' )
		length--;

	Q_strncpyz( base, &path[ length ], sizeof( base ) );

	length = strlen( base ) - 1;

	// Strip trailing slashes
	while( length > 0 && base[ length ] == '\\' )
    base[ length-- ] = '\0';

	return base;
}

/*
==============
Sys_Dirname
==============
*/
const char *Sys_Dirname( const char *path )
{
	static char dir[ MAX_OSPATH ] = { 0 };
	int length;

	Q_strncpyz( dir, path, sizeof( dir ) );
	length = strlen( dir ) - 1;

	while( length > 0 && dir[ length ] != '\\' )
		length--;

	dir[ length ] = '\0';

	return dir;
}

/*
================
Sys_Milliseconds
================
*/
int Sys_Milliseconds (bool baseTime)
{
	static int sys_timeBase = timeGetTime();
	int			sys_curtime;

	sys_curtime = timeGetTime();
	if(!baseTime)
	{
		sys_curtime -= sys_timeBase;
	}

	return sys_curtime;
}

int Sys_Milliseconds2( void )
{
	return Sys_Milliseconds(false);
}

/*
================
Sys_RandomBytes
================
*/
bool Sys_RandomBytes( byte *string, int len )
{
	HCRYPTPROV  prov;

	if( !CryptAcquireContext( &prov, NULL, NULL,
		PROV_RSA_FULL, CRYPT_VERIFYCONTEXT ) )  {

		return false;
	}

	if( !CryptGenRandom( prov, len, (BYTE *)string ) )  {
		CryptReleaseContext( prov, 0 );
		return false;
	}
	CryptReleaseContext( prov, 0 );
	return true;
}

/*
==================
Sys_GetCurrentUser
==================
*/
char *Sys_GetCurrentUser( void )
{
	static char s_userName[1024];
	DWORD size = sizeof( s_userName );

	if ( !GetUserName( s_userName, &size ) )
		strcpy( s_userName, "player" );

	if ( !s_userName[0] )
	{
		strcpy( s_userName, "player" );
	}

	return s_userName;
}

/*
* Builds the path for the user's game directory
*/
char *Sys_DefaultHomePath( void )
{
#if defined(BUILD_PORTABLE)
	Com_Printf( "Portable install requested, skipping homepath support\n" );
	return NULL;
#else
	if ( !homePath[0] )
	{
		TCHAR homeDirectory[MAX_PATH];

		if( !SUCCEEDED( SHGetFolderPath( NULL, CSIDL_PERSONAL, NULL, 0, homeDirectory ) ) )
		{
			Com_Printf( "Unable to determine your home directory.\n" );
			return NULL;
		}

		Com_sprintf( homePath, sizeof( homePath ), "%s%cMy Games%c", homeDirectory, PATH_SEP, PATH_SEP );
		if ( com_homepath && com_homepath->string[0] )
			Q_strcat( homePath, sizeof( homePath ), com_homepath->string );
		else
			Q_strcat( homePath, sizeof( homePath ), HOMEPATH_NAME_WIN );
	}

	return homePath;
#endif
}

static const char *GetErrorString( DWORD error ) {
	static char buf[MAX_STRING_CHARS];
	buf[0] = '\0';

	if ( error ) {
		LPVOID lpMsgBuf;
		DWORD bufLen = FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, error, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ), (LPTSTR)&lpMsgBuf, 0, NULL );
		if ( bufLen ) {
			LPCSTR lpMsgStr = (LPCSTR)lpMsgBuf;
			Q_strncpyz( buf, lpMsgStr, Q_min( (size_t)(lpMsgStr + bufLen), sizeof(buf) ) );
			LocalFree( lpMsgBuf );
		}
	}
	return buf;
}

void Sys_SetProcessorAffinity( void ) {
	DWORD_PTR processMask, processAffinityMask, systemAffinityMask;
	HANDLE handle = GetCurrentProcess();

	if ( !GetProcessAffinityMask( handle, &processAffinityMask, &systemAffinityMask ) )
		return;

	if ( sscanf( com_affinity->string, "%X", &processMask ) != 1 )
		processMask = 1; // set to first core only

	if ( !processMask )
		processMask = systemAffinityMask; // use all the cores available to the system

	if ( processMask == processAffinityMask )
		return; // no change

	if ( !SetProcessAffinityMask( handle, processMask ) )
		Com_DPrintf( "Setting affinity mask failed (%s)\n", GetErrorString( GetLastError() ) );
}

/*
==================
Sys_LowPhysicalMemory()
==================
*/

qboolean Sys_LowPhysicalMemory(void) {
	static MEMORYSTATUSEX stat;
	static qboolean bAsked = qfalse;
	static cvar_t* sys_lowmem = Cvar_Get( "sys_lowmem", "0", 0 );

	if (!bAsked)	// just in case it takes a little time for GlobalMemoryStatusEx() to gather stats on
	{				//	stuff we don't care about such as virtual mem etc.
		bAsked = qtrue;

		stat.dwLength = sizeof (stat);
		GlobalMemoryStatusEx (&stat);
	}
	if (sys_lowmem->integer)
	{
		return qtrue;
	}
	return (stat.ullTotalPhys <= MEM_THRESHOLD) ? qtrue : qfalse;
}

/*
==============
Sys_Mkdir
==============
*/
qboolean Sys_Mkdir( const char *path ) {
	if( !CreateDirectory( path, NULL ) )
	{
		if( GetLastError( ) != ERROR_ALREADY_EXISTS )
			return qfalse;
	}
	return qtrue;
}

/*
==============
Sys_Cwd
==============
*/
char *Sys_Cwd( void ) {
	static char cwd[MAX_OSPATH];

	_getcwd( cwd, sizeof( cwd ) - 1 );
	cwd[MAX_OSPATH-1] = 0;

	return cwd;
}

/* Resolves path names and determines if they are the same */
/* For use with full OS paths not quake paths */
/* Returns true if resulting paths are valid and the same, otherwise false */
bool Sys_PathCmp( const char *path1, const char *path2 ) {
	char *r1, *r2;

	r1 = _fullpath(NULL, path1, MAX_OSPATH);
	r2 = _fullpath(NULL, path2, MAX_OSPATH);

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
==============================================================

DIRECTORY SCANNING

==============================================================
*/

#define	MAX_FOUND_FILES	0x1000

void Sys_ListFilteredFiles( const char *basedir, char *subdirs, char *filter, char **psList, int *numfiles ) {
	char		search[MAX_OSPATH], newsubdirs[MAX_OSPATH];
	char		filename[MAX_OSPATH];
	intptr_t	findhandle;
	struct _finddata_t findinfo;

	if ( *numfiles >= MAX_FOUND_FILES - 1 ) {
		return;
	}

	if (strlen(subdirs)) {
		Com_sprintf( search, sizeof(search), "%s\\%s\\*", basedir, subdirs );
	}
	else {
		Com_sprintf( search, sizeof(search), "%s\\*", basedir );
	}

	findhandle = _findfirst (search, &findinfo);
	if (findhandle == -1) {
		return;
	}

	do {
		if (findinfo.attrib & _A_SUBDIR) {
			if (Q_stricmp(findinfo.name, ".") && Q_stricmp(findinfo.name, "..")) {
				if (strlen(subdirs)) {
					Com_sprintf( newsubdirs, sizeof(newsubdirs), "%s\\%s", subdirs, findinfo.name);
				}
				else {
					Com_sprintf( newsubdirs, sizeof(newsubdirs), "%s", findinfo.name);
				}
				Sys_ListFilteredFiles( basedir, newsubdirs, filter, psList, numfiles );
			}
		}
		if ( *numfiles >= MAX_FOUND_FILES - 1 ) {
			break;
		}
		Com_sprintf( filename, sizeof(filename), "%s\\%s", subdirs, findinfo.name );
		if (!Com_FilterPath( filter, filename, qfalse ))
			continue;
		psList[ *numfiles ] = CopyString( filename );
		(*numfiles)++;
	} while ( _findnext (findhandle, &findinfo) != -1 );

	_findclose (findhandle);
}

static qboolean strgtr(const char *s0, const char *s1) {
	int l0, l1, i;

	l0 = strlen(s0);
	l1 = strlen(s1);

	if (l1<l0) {
		l0 = l1;
	}

	for(i=0;i<l0;i++) {
		if (s1[i] > s0[i]) {
			return qtrue;
		}
		if (s1[i] < s0[i]) {
			return qfalse;
		}
	}
	return qfalse;
}

char **Sys_ListFiles( const char *directory, const char *extension, char *filter, int *numfiles, qboolean wantsubs ) {
	char		search[MAX_OSPATH];
	int			nfiles;
	char		**listCopy;
	char		*list[MAX_FOUND_FILES];
	struct _finddata_t findinfo;
	intptr_t	findhandle;
	int			flag;
	int			i;
	int			extLen;

	if (filter) {

		nfiles = 0;
		Sys_ListFilteredFiles( directory, "", filter, list, &nfiles );

		list[ nfiles ] = 0;
		*numfiles = nfiles;

		if (!nfiles)
			return NULL;

		listCopy = (char **)Z_Malloc( ( nfiles + 1 ) * sizeof( *listCopy ), TAG_LISTFILES );
		for ( i = 0 ; i < nfiles ; i++ ) {
			listCopy[i] = list[i];
		}
		listCopy[i] = NULL;

		return listCopy;
	}

	if ( !extension) {
		extension = "";
	}

	// passing a slash as extension will find directories
	if ( extension[0] == '/' && extension[1] == 0 ) {
		extension = "";
		flag = 0;
	} else {
		flag = _A_SUBDIR;
	}

	extLen = strlen( extension );

	Com_sprintf( search, sizeof(search), "%s\\*%s", directory, extension );

	// search
	nfiles = 0;

	findhandle = _findfirst (search, &findinfo);
	if (findhandle == -1) {
		*numfiles = 0;
		return NULL;
	}

	do {
		if ( (!wantsubs && flag ^ ( findinfo.attrib & _A_SUBDIR )) || (wantsubs && findinfo.attrib & _A_SUBDIR) ) {
			if (*extension) {
				if ( strlen( findinfo.name ) < extLen ||
					Q_stricmp(
						findinfo.name + strlen( findinfo.name ) - extLen,
						extension ) ) {
					continue; // didn't match
				}
			}
			if ( nfiles == MAX_FOUND_FILES - 1 ) {
				break;
			}
			list[ nfiles ] = CopyString( findinfo.name );
			nfiles++;
		}
	} while ( _findnext (findhandle, &findinfo) != -1 );

	list[ nfiles ] = 0;

	_findclose (findhandle);

	// return a copy of the list
	*numfiles = nfiles;

	if ( !nfiles ) {
		return NULL;
	}

	listCopy = (char **)Z_Malloc( ( nfiles + 1 ) * sizeof( *listCopy ), TAG_LISTFILES );
	for ( i = 0 ; i < nfiles ; i++ ) {
		listCopy[i] = list[i];
	}
	listCopy[i] = NULL;

	do {
		flag = 0;
		for(i=1; i<nfiles; i++) {
			if (strgtr(listCopy[i-1], listCopy[i])) {
				char *temp = listCopy[i];
				listCopy[i] = listCopy[i-1];
				listCopy[i-1] = temp;
				flag = 1;
			}
		}
	} while(flag);

	return listCopy;
}

void	Sys_FreeFileList( char **psList ) {
	int		i;

	if ( !psList ) {
		return;
	}

	for ( i = 0 ; psList[i] ; i++ ) {
		Z_Free( psList[i] );
	}

	Z_Free( psList );
}

/*
========================================================================

LOAD/UNLOAD DLL

========================================================================
*/
//make sure the dll can be opened by the file system, then write the
//file back out again so it can be loaded is a library. If the read
//fails then the dll is probably not in the pk3 and we are running
//a pure server -rww
UnpackDLLResult Sys_UnpackDLL(const char *name)
{
	UnpackDLLResult result = {};
	void *data;
	long len = FS_ReadFile(name, &data);

	if (len >= 1)
	{
		if (FS_FileIsInPAK(name, NULL) == 1)
		{
			char *tempFileName;
			if ( FS_WriteToTemporaryFile(data, len, &tempFileName) )
			{
				result.tempDLLPath = tempFileName;
				result.succeeded = true;
			}
		}
	}

	FS_FreeFile(data);

	return result;
}

bool Sys_DLLNeedsUnpacking()
{
#if defined(_JK2EXE)
	return false;
#else
	return Cvar_VariableIntegerValue("sv_pure") != 0;
#endif
}

/*
================
Sys_PlatformInit

Platform-specific initialization
================
*/

// Max open file descriptors. Mostly used by pk3 files with
// MAX_SEARCH_PATHS limit.
#define MAX_OPEN_FILES	4096

/*
================
Sys_CrashFilter

An exception nothing caught: the crash log, as Sys_CrashHandler of sys_unix.cpp writes one, then Windows ends the
program as it would have. Before 2026-10-28 such a crash left nothing. Not async safe, like the one of Linux: a
crash in the allocator may end in here without a log.
================
*/
static LONG WINAPI Sys_CrashFilter( EXCEPTION_POINTERS *info )
{
	static volatile LONG crashed;

	if ( !InterlockedExchange( &crashed, 1 ) ) {
		char path[MAX_OSPATH];
		FILE *fp;

		Sys_CrashLogPath( path, sizeof( path ) );
		fp = fopen( path, "w" );
		if ( fp ) {
			const EXCEPTION_RECORD *rec = info ? info->ExceptionRecord : NULL;

			fprintf( fp, "%s %s crashed: exception 0x%08lx, address %p", JK_VERSION, PLATFORM_STRING,
				rec ? (unsigned long)rec->ExceptionCode : 0ul, rec ? rec->ExceptionAddress : NULL );
			if ( rec && rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2 ) {
				fprintf( fp, ", %s %p", rec->ExceptionInformation[0] == 8 ? "executing" : rec->ExceptionInformation[0] ? "writing" : "reading",
					(void *)rec->ExceptionInformation[1] );
			}
			if ( rec ) {
				HMODULE mod = NULL;
				char name[MAX_OSPATH] = "";

				if ( GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
						(LPCSTR)rec->ExceptionAddress, &mod ) && GetModuleFileNameA( mod, name, sizeof( name ) ) ) {
					fprintf( fp, " (%s + 0x%llx)", name, (unsigned long long)( (uintptr_t)rec->ExceptionAddress - (uintptr_t)mod ) );
				}
			}
			fprintf( fp, "\n\nconsole:\n" );
			ConsoleLogWriteOut( fp );
			fclose( fp );
		}
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

void Sys_PlatformInit( int argc, char *argv[] ) {
	TIMECAPS ptc;

	SetUnhandledExceptionFilter( Sys_CrashFilter );
	if ( timeGetDevCaps( &ptc, sizeof( ptc ) ) == MMSYSERR_NOERROR )
	{
		timerResolution = ptc.wPeriodMin;

		if ( timerResolution > 1 )
		{
			Com_Printf( "Warning: Minimum supported timer resolution is %ums "
				"on this system, recommended resolution 1ms\n", timerResolution );
		}

		timeBeginPeriod( timerResolution );
	}
	else
		timerResolution = 0;

	// raise open file limit to allow more pk3 files
	int maxfds = MAX_OPEN_FILES;

	for (int i = 1; i + 1 < argc; i++) {
		if (!Q_stricmp(argv[i], "-maxfds")) {
			maxfds = atoi(argv[i + 1]);
		}
	}

	maxfds = _setmaxstdio(maxfds);

	if (maxfds == -1) {
		Com_Printf("Warning: Failed to increase open file limit. %s\n", strerror(errno));
	}
}

/*
================
Sys_PlatformExit

Platform-specific exit code
================
*/
void Sys_PlatformExit( void )
{
	if ( timerResolution )
		timeEndPeriod( timerResolution );
}

void Sys_Sleep( int msec )
{
	if ( msec == 0 )
		return;

#ifdef DEDICATED
	if ( msec < 0 )
		WaitForSingleObject( GetStdHandle( STD_INPUT_HANDLE ), INFINITE );
	else
		WaitForSingleObject( GetStdHandle( STD_INPUT_HANDLE ), msec );
#else
	// Client Sys_Sleep doesn't support waiting on stdin
	if ( msec < 0 )
		return;

	Sleep( msec );
#endif
}

/*
==============================================================

The memory monitor of the client (client/cl_memwatch.cpp, sys_memwatch.h)

==============================================================
*/

#ifndef DEDICATED

static void Sys_MemWatchPath( HMODULE mod, char *path, int size )
{
	wchar_t wide[MAX_OSPATH];
	const DWORD len = GetModuleFileNameW( mod, wide, ARRAY_LEN( wide ) );

	path[0] = '\0';
	if ( len && len < ARRAY_LEN( wide ) && !WideCharToMultiByte( CP_UTF8, 0, wide, -1, path, size, NULL, NULL ) )
		path[0] = '\0';
}

// the Windows folder (System32, SysWOW64 and WinSxS are in it) and the temporary folder of the user
void Sys_MemWatchPlaces( memwPlaces_t *places )
{
	static char windows[MAX_OSPATH], temp[MAX_OSPATH];
	static const char * const system[] = { windows, NULL };
	static const char * const temporary[] = { temp, NULL };
	wchar_t wide[MAX_OSPATH];
	DWORD len;

	windows[0] = temp[0] = '\0';
	len = GetWindowsDirectoryW( wide, ARRAY_LEN( wide ) );
	if ( len && len < ARRAY_LEN( wide ) && !WideCharToMultiByte( CP_UTF8, 0, wide, -1, windows, sizeof( windows ), NULL, NULL ) )
		windows[0] = '\0';
	len = GetTempPathW( ARRAY_LEN( wide ), wide );
	if ( len && len < ARRAY_LEN( wide ) && !WideCharToMultiByte( CP_UTF8, 0, wide, -1, temp, sizeof( temp ), NULL, NULL ) )
		temp[0] = '\0';
	places->program = Sys_ProgramFolder();
	places->system = system;
	places->temporary = temporary;
	places->foldCase = qtrue;
}

// the committed executable memory, a module once (its sections are regions of their own)
int Sys_MemWatchRegions( sysMemRegion_t *regions, int max )
{
	const DWORD exec = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
	SYSTEM_INFO si;
	MEMORY_BASIC_INFORMATION mbi;
	uintptr_t addr, top;
	void *lastImage = NULL;
	int count = 0;

	GetSystemInfo( &si );
	addr = (uintptr_t)si.lpMinimumApplicationAddress;
	top = (uintptr_t)si.lpMaximumApplicationAddress;
	while ( addr < top && count < max && VirtualQuery( (LPCVOID)addr, &mbi, sizeof( mbi ) ) == sizeof( mbi ) )
	{
		const uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;

		if ( mbi.State == MEM_COMMIT && ( mbi.Protect & exec ) && !( mbi.Protect & PAGE_GUARD )
			&& !( mbi.Type == MEM_IMAGE && mbi.AllocationBase == lastImage ) )
		{
			sysMemRegion_t *r = &regions[count++];

			memset( r, 0, sizeof( *r ) );
			r->start = (uintptr_t)mbi.BaseAddress;
			r->end = next;
			// (PAGE_EXECUTE_WRITECOPY is how a section of a module may be mapped, and is no writable code)
			r->write = (qboolean)( ( mbi.Protect & PAGE_EXECUTE_READWRITE ) != 0 );
			if ( mbi.Type == MEM_IMAGE )
			{
				lastImage = mbi.AllocationBase;
				r->image = qtrue;
				Sys_MemWatchPath( (HMODULE)mbi.AllocationBase, r->path, sizeof( r->path ) );
				if ( !r->path[0] )
					Q_strncpyz( r->path, "(a module without a name)", sizeof( r->path ) );
			}
		}
		if ( next <= addr )
			break;
		addr = next;
	}
	return count;
}

// the processes whose parent is this one and that started after it (a number of a process is used again)
int Sys_MemWatchChildren( sysMemChild_t *children, int max )
{
	const DWORD self = GetCurrentProcessId();
	HANDLE snap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
	PROCESSENTRY32W entry;
	FILETIME selfStart, unused;
	int count = 0;

	if ( snap == INVALID_HANDLE_VALUE )
		return -1;
	GetProcessTimes( GetCurrentProcess(), &selfStart, &unused, &unused, &unused );
	entry.dwSize = sizeof( entry );
	for ( BOOL more = Process32FirstW( snap, &entry ); more && count < max; more = Process32NextW( snap, &entry ) )
	{
		HANDLE proc;
		FILETIME start;
		qboolean after = qfalse;

		if ( entry.th32ParentProcessID != self || entry.th32ProcessID == self )
			continue;
		proc = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID );
		if ( proc )
		{
			if ( GetProcessTimes( proc, &start, &unused, &unused, &unused ) )
				after = (qboolean)( CompareFileTime( &start, &selfStart ) >= 0 );
			CloseHandle( proc );
		}
		if ( !after )
			continue;
		children[count].pid = (int)entry.th32ProcessID;
		if ( !WideCharToMultiByte( CP_UTF8, 0, entry.szExeFile, -1, children[count].name, sizeof( children[count].name ), NULL, NULL ) )
			children[count].name[0] = '\0';
		count++;
	}
	CloseHandle( snap );
	return count;
}

// the sections of the module with addr in it that hold code
int Sys_MemWatchCode( const void *addr, sysMemCode_t *code, int max )
{
	HMODULE mod = NULL;
	const IMAGE_DOS_HEADER *dos;
	const IMAGE_NT_HEADERS *nt;
	const IMAGE_SECTION_HEADER *sec;
	int count = 0;

	if ( !addr || !GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCWSTR)addr, &mod ) )
		return 0;
	dos = (const IMAGE_DOS_HEADER *)mod;
	if ( dos->e_magic != IMAGE_DOS_SIGNATURE )
		return 0;
	nt = (const IMAGE_NT_HEADERS *)( (const byte *)mod + dos->e_lfanew );
	if ( nt->Signature != IMAGE_NT_SIGNATURE )
		return 0;
	sec = IMAGE_FIRST_SECTION( nt );
	for ( int i = 0; i < nt->FileHeader.NumberOfSections && count < max; i++, sec++ )
	{
		if ( ( sec->Characteristics & IMAGE_SCN_MEM_EXECUTE ) && sec->Misc.VirtualSize )
		{
			code[count].start = (uintptr_t)mod + sec->VirtualAddress;
			code[count].size = sec->Misc.VirtualSize;
			count++;
		}
	}
	return count;
}
#endif // !DEDICATED
