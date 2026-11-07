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

// sys_memwatch_rules.c -- how the memory monitor of the client judges a piece of executable memory (see
// sys_memwatch.h). Plain C without the engine, for the unit tests (tests/vjk/memwatch.cpp). Not from OpenJK.

#include "sys_memwatch.h"

// the regions the kernel of Linux names and that are no code of anybody
static const char * const memwKernel[] = { "[vdso]", "[vsyscall]", "[vectors]", "[uprobes]", NULL };

static int MemW_Lower( int c, qboolean foldCase ) {
	if ( !foldCase ) {
		return c;
	}
	if ( c == '\\' ) {
		return '/';
	}
	return ( c >= 'A' && c <= 'Z' ) ? c - 'A' + 'a' : c;
}

/*
=================
MemW_Under

Is path inside folder (or folder itself)? folder may end in a slash or not.
=================
*/
qboolean MemW_Under( const char *path, const char *folder, qboolean foldCase ) {
	size_t i, len;

	if ( !path || !folder || !folder[0] ) {
		return qfalse;
	}
	len = strlen( folder );
	while ( len > 1 && MemW_Lower( folder[len - 1], foldCase ) == '/' ) {
		len--;
	}
	for ( i = 0; i < len; i++ ) {
		if ( !path[i] || MemW_Lower( (unsigned char)path[i], foldCase ) != MemW_Lower( (unsigned char)folder[i], foldCase ) ) {
			return qfalse;
		}
	}
	return (qboolean)( !path[len] || MemW_Lower( (unsigned char)path[len], foldCase ) == '/' );
}

static qboolean MemW_UnderAny( const char *path, const char * const *folders, qboolean foldCase ) {
	for ( ; folders && *folders; folders++ ) {
		if ( MemW_Under( path, *folders, foldCase ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
=================
MemW_ParseMapsLine

A line of /proc/self/maps: "<start>-<end> <perms> <offset> <dev> <inode>   <path>". qtrue for an executable
region, which is then filled in.
=================
*/
qboolean MemW_ParseMapsLine( const char *line, sysMemRegion_t *region ) {
	unsigned long long	start = 0, end = 0;
	const char			*s = line;
	char				perms[5];
	int					field, n;
	size_t				len;

	memset( region, 0, sizeof( *region ) );
	for ( n = 0; ( *s >= '0' && *s <= '9' ) || ( *s >= 'a' && *s <= 'f' ); s++, n++ ) {
		start = start * 16 + ( *s <= '9' ? *s - '0' : *s - 'a' + 10 );
	}
	if ( !n || n > 16 || *s++ != '-' ) {
		return qfalse;
	}
	for ( n = 0; ( *s >= '0' && *s <= '9' ) || ( *s >= 'a' && *s <= 'f' ); s++, n++ ) {
		end = end * 16 + ( *s <= '9' ? *s - '0' : *s - 'a' + 10 );
	}
	if ( !n || n > 16 || *s++ != ' ' || end <= start ) {
		return qfalse;
	}
	for ( n = 0; n < 4 && *s && *s != ' '; n++ ) {
		perms[n] = *s++;
	}
	perms[n] = '\0';
	if ( n != 4 || perms[2] != 'x' ) {
		return qfalse;
	}

	// offset, device and inode, then the blanks before the path
	for ( field = 0; field < 3; field++ ) {
		while ( *s == ' ' ) {
			s++;
		}
		if ( !*s ) {
			return qfalse;
		}
		while ( *s && *s != ' ' ) {
			s++;
		}
	}
	while ( *s == ' ' ) {
		s++;
	}

	region->start = (uintptr_t)start;
	region->end = (uintptr_t)end;
	region->write = (qboolean)( perms[1] == 'w' );
	Q_strncpyz( region->path, s, sizeof( region->path ) );
	len = strlen( region->path );
	while ( len && ( region->path[len - 1] == '\n' || region->path[len - 1] == '\r' ) ) {
		region->path[--len] = '\0';
	}
	if ( len > 10 && !strcmp( region->path + len - 10, " (deleted)" ) ) {
		region->path[len - 10] = '\0';
		region->deleted = qtrue;
	}
	region->image = (qboolean)( region->path[0] == '/' );
	return qtrue;
}

/*
=================
MemW_Classify

What a piece of executable memory is to the monitor. Whether it is new is for the caller.
=================
*/
memwKind_t MemW_Classify( const sysMemRegion_t *region, const memwPlaces_t *places ) {
	const char * const *k;

	if ( region->image ) {
		if ( region->deleted || !Q_strncmp( region->path, "/memfd:", 7 )
			|| MemW_UnderAny( region->path, places->temporary, places->foldCase ) ) {
			return MEMW_EXEC_FILE;
		}
		if ( region->write ) {
			return MEMW_RWX;
		}
		if ( MemW_Under( region->path, places->program, places->foldCase )
			|| MemW_UnderAny( region->path, places->system, places->foldCase ) ) {
			return MEMW_NONE;
		}
		return MEMW_LIBRARY;
	}

	for ( k = memwKernel; *k; k++ ) {
		if ( !strcmp( region->path, *k ) ) {
			return MEMW_NONE;
		}
	}
	return region->write ? MEMW_RWX : MEMW_ANON;
}
