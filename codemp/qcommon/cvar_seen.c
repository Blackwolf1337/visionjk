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

// cvar_seen.c -- the day code last used a cvar, by name, for cvar_cleanup. Not from OpenJK. The engine tells it
// what was used and keeps the file (cvar.cpp), this is the table and the text of the file.

#include "qcommon/q_shared.h"
#include "qcommon/cvar_seen.h"

#include <stdio.h>
#include <stdlib.h>

#define SEEN_NAME		64				// a longer name is not kept, and never offered either
#define SEEN_HASH		( CVARSEEN_MAX * 2 )

typedef struct cvarSeen_s {
	char	*name;
	int		day;
} cvarSeen_t;

static cvarSeen_t	seen[CVARSEEN_MAX];
static int			numSeen;
static int			seenHash[SEEN_HASH];	// an index into seen + 1, 0 for none
static int			seenStart;
static qboolean		seenDirty;

static unsigned SeenHash( const char *name ) {
	unsigned h = 2166136261u;

	for ( ; *name; name++ )
		h = ( h ^ (unsigned char)tolower( (unsigned char)*name ) ) * 16777619u;
	return h % SEEN_HASH;
}

// the slot of the name in seenHash: its own, or the free one it would go into
static int SeenSlot( const char *name ) {
	int slot = (int)SeenHash( name );

	while ( seenHash[slot] && Q_stricmp( seen[seenHash[slot] - 1].name, name ) )
		slot = ( slot + 1 ) % SEEN_HASH;
	return slot;
}

static void SeenRehash( void ) {
	int i;

	memset( seenHash, 0, sizeof( seenHash ) );
	for ( i = 0; i < numSeen; i++ )
		seenHash[SeenSlot( seen[i].name )] = i + 1;
}

static int SeenLater( const void *a, const void *b ) {
	return ( (const cvarSeen_t *)b )->day - ( (const cvarSeen_t *)a )->day;
}

// the table is full: an eighth of it goes, what was used longest ago
static void SeenDropOldest( void ) {
	const int keep = CVARSEEN_MAX - CVARSEEN_MAX / 8;
	int i;

	qsort( seen, numSeen, sizeof( seen[0] ), SeenLater );
	for ( i = keep; i < numSeen; i++ ) {
		free( seen[i].name );
		seen[i].name = NULL;
	}
	numSeen = keep;
	SeenRehash();
}

void CvarSeen_Clear( void ) {
	int i;

	for ( i = 0; i < numSeen; i++ )
		free( seen[i].name );
	memset( seen, 0, sizeof( seen ) );
	memset( seenHash, 0, sizeof( seenHash ) );
	numSeen = 0;
	seenStart = 0;
	seenDirty = qfalse;
}

int CvarSeen_Start( void ) {
	return seenStart;
}

void CvarSeen_Touch( const char *name, int day ) {
	const size_t	len = strlen( name );
	int				slot;

	if ( day <= 0 || !len || len >= SEEN_NAME )
		return;
	if ( !seenStart ) {
		seenStart = day;
		seenDirty = qtrue;
	}

	slot = SeenSlot( name );
	if ( seenHash[slot] ) {
		cvarSeen_t *s = &seen[seenHash[slot] - 1];

		if ( day > s->day ) {
			s->day = day;
			seenDirty = qtrue;
		}
		return;
	}

	if ( numSeen == CVARSEEN_MAX ) {
		SeenDropOldest();
		slot = SeenSlot( name );
	}
	seen[numSeen].name = (char *)malloc( len + 1 );
	if ( !seen[numSeen].name )
		return;
	memcpy( seen[numSeen].name, name, len + 1 );
	seen[numSeen].day = day;
	seenHash[slot] = ++numSeen;
	seenDirty = qtrue;
}

int CvarSeen_Day( const char *name ) {
	const int slot = SeenSlot( name );

	return seenHash[slot] ? seen[seenHash[slot] - 1].day : 0;
}

qboolean CvarSeen_Unused( const char *name, int today ) {
	const int day = CvarSeen_Day( name );

	if ( strlen( name ) >= SEEN_NAME )
		return qfalse;
	if ( day )
		return (qboolean)( today - day >= CVARSEEN_DAYS );
	return (qboolean)( seenStart && today - seenStart >= CVARSEEN_DAYS );
}

qboolean CvarSeen_Dirty( void ) {
	return seenDirty;
}

void CvarSeen_Parse( const char *text, int today ) {
	const qboolean	wasDirty = seenDirty;
	const char		*line = text;

	while ( *line ) {
		const char	*end = strchr( line, '\n' );
		char		buf[SEEN_NAME + 32], name[SEEN_NAME];
		int			len = end ? (int)( end - line ) : (int)strlen( line ), day;

		if ( len < (int)sizeof( buf ) ) {
			memcpy( buf, line, len );
			buf[len] = '\0';
			if ( sscanf( buf, "%63s %d", name, &day ) == 2 && day > 0 && day <= today + 1 ) {
				if ( !strcmp( name, "start" ) ) {
					// the tracking began with the earliest start that is known
					if ( !seenStart || day < seenStart )
						seenStart = day;
				} else {
					CvarSeen_Touch( name, day );
				}
			}
		}
		if ( !end )
			break;
		line = end + 1;
	}
	// what came from the file is in the file already
	seenDirty = wasDirty;
}

int CvarSeen_Write( char *out, int outSize ) {
	int n, i;

	n = snprintf( out, outSize, "start %d\n", seenStart );
	if ( n < 0 || n >= outSize )
		return -1;
	for ( i = 0; i < numSeen; i++ ) {
		const int add = snprintf( out + n, outSize - n, "%s %d\n", seen[i].name, seen[i].day );

		if ( add < 0 || add >= outSize - n )
			return -1;
		n += add;
	}
	seenDirty = qfalse;
	return n;
}
