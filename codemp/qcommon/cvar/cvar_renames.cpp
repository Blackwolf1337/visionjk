/*
===========================================================================
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

// cvar_renames.cpp -- the old names of the cvars and commands of this tree (cvar_renames.h): what is stored or
// set under one goes to the new name. cvar.cpp asks for the cvars, cl_keys.cpp has the binds rewritten.
// Nothing here but the two tables and text: a test builds it with q_string.c alone.

#include "qcommon/qcommon.h"

typedef struct {
	const char	*from, *to;
} rename_t;

static const rename_t cvarRenames[] = {
#define CVAR_RENAME( from, to )	{ from, to },
#include "cvar_renames.h"
#undef CVAR_RENAME
};

static const rename_t cmdRenames[] = {
#define CMD_RENAME( from, to )	{ from, to },
#include "cvar_renames.h"
#undef CMD_RENAME
};

static const char *Rename_Find( const rename_t *table, size_t count, const char *name, qboolean toOld ) {
	for ( size_t i = 0; i < count; i++ ) {
		if ( !Q_stricmp( toOld ? table[i].to : table[i].from, name ) ) {
			return toOld ? table[i].from : table[i].to;
		}
	}
	return NULL;
}

// the name a cvar had before, NULL: it had no other
const char *Cvar_RenamedFrom( const char *name ) {
	return Rename_Find( cvarRenames, ARRAY_LEN( cvarRenames ), name, qtrue );
}

// the name a cvar has now, NULL: this is no old name
const char *Cvar_RenamedTo( const char *name ) {
	return Rename_Find( cvarRenames, ARRAY_LEN( cvarRenames ), name, qfalse );
}

/*
============
Cvar_RenamedText

The text of a bind with the new names: the first word of each command (after a ';') as a command or a cvar,
"+name" and "-name" included, and any later word as a cvar ("toggle cg_hud"). What stands in quotes is left
alone, so a "say" keeps its words. qfalse: nothing to change, or it would not fit, and out is not to be used.
============
*/
qboolean Cvar_RenamedText( const char *in, char *out, int outSize ) {
	qboolean	changed = qfalse, first = qtrue;
	int			len = 0;

	while ( *in ) {
		const char	*end = in, *to = NULL;
		char		word[MAX_CVAR_VALUE_STRING];
		int			n;

		if ( *in == ';' || *in == ' ' || *in == '\t' || *in == '"' ) {
			if ( *in == ';' ) {
				first = qtrue;
			}
			// a quoted text goes as it is, with its quotes
			if ( *in == '"' ) {
				end = strchr( in + 1, '"' );
				end = end ? end + 1 : in + strlen( in );
				first = qfalse;
			} else {
				end = in + 1;
			}
			n = (int)( end - in );
			if ( len + n >= outSize ) {
				return qfalse;
			}
			memcpy( out + len, in, n );
			len += n;
			in = end;
			continue;
		}

		while ( *end && *end != ';' && *end != ' ' && *end != '\t' && *end != '"' ) {
			end++;
		}
		n = (int)( end - in );
		if ( n < (int)sizeof( word ) ) {
			memcpy( word, in, n );
			word[n] = '\0';
			if ( first ) {
				to = Rename_Find( cmdRenames, ARRAY_LEN( cmdRenames ), word, qfalse );
			}
			if ( !to ) {
				to = Cvar_RenamedTo( word );
			}
		}
		if ( to ) {
			changed = qtrue;
			n = (int)strlen( to );
		}
		if ( len + n >= outSize ) {
			return qfalse;
		}
		memcpy( out + len, to ? to : in, n );
		len += n;
		in = end;
		first = qfalse;
	}
	out[len] = '\0';
	return changed;
}
