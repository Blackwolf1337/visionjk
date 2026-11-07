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

// sys_memwatch.h -- what the memory monitor of the client (client/cl_memwatch.cpp) asks of the system, and the
// rules it judges the answers by (sys_memwatch_rules.c, plain C for the unit tests). Not from OpenJK.
//
// Linux: /proc/self/maps, /proc/self/task/<tid>/children and dl_iterate_phdr (sys_unix.cpp). Windows:
// VirtualQuery, a snapshot of the processes and the section headers of a module (sys_win32.cpp). macOS and the
// browser have none of it and answer -1.

#pragma once

#include "qcommon/q_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

// a piece of executable memory
typedef struct sysMemRegion_s {
	uintptr_t	start, end;
	qboolean	write;					// writable as well
	qboolean	image;					// from a file: a library or the program
	qboolean	deleted;				// that file is gone (Linux)
	char		path[MAX_OSPATH];		// the file, "" for anonymous memory, "[name]" for a named one of Linux
} sysMemRegion_t;

typedef struct sysMemChild_s {
	int			pid;
	char		name[64];
} sysMemChild_t;

typedef struct sysMemCode_s {
	uintptr_t	start;
	size_t		size;
} sysMemCode_t;

// the executable regions, at most max. -1: this system can not tell.
int Sys_MemWatchRegions( sysMemRegion_t *regions, int max );
// the processes this one started that still run. -1: this system can not tell.
int Sys_MemWatchChildren( sysMemChild_t *children, int max );
// the executable sections of the module (the program or a library) that has addr in it, at most max
int Sys_MemWatchCode( const void *addr, sysMemCode_t *code, int max );
// counts the libraries that were loaded and unloaded (Sys_LoadDll, Sys_UnloadDll): a code range taken before a
// change of it may be gone
int Sys_LibraryGeneration( void );
// Sys_Milliseconds() of the last Sys_OpenURL (the system starts a program for it), 0 for none
int Sys_OpenURLTime( void );

//
// sys_memwatch_rules.c
//

typedef enum {
	MEMW_NONE,
	MEMW_EXEC_FILE,		// code from a file that is gone, from a memfd or from a temporary folder: strong
	MEMW_RWX,			// writable and executable
	MEMW_ANON,			// executable and from no file
	MEMW_LIBRARY		// a library from outside the program folder and the folders of the system
} memwKind_t;

typedef struct memwPlaces_s {
	const char			*program;		// the program folder
	const char * const	*system;		// folders of the system, NULL at the end
	const char * const	*temporary;		// temporary folders, NULL at the end
	qboolean			foldCase;		// Windows: names in any case, '\' and '/' the same
} memwPlaces_t;

// the folders of this system (sys_unix.cpp, sys_win32.cpp)
void		Sys_MemWatchPlaces( memwPlaces_t *places );

qboolean	MemW_ParseMapsLine( const char *line, sysMemRegion_t *region );
memwKind_t	MemW_Classify( const sysMemRegion_t *region, const memwPlaces_t *places );
qboolean	MemW_Under( const char *path, const char *folder, qboolean foldCase );

#ifdef __cplusplus
}
#endif
