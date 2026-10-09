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

// wa_clips.c -- the clip brushes of the map in the web admin panel: which of them are off, and switching them

#include "game/webadmin/wa_local.h"

/*
=================
The clip editor

Not in JKAdmin. A clip brush is part of the map file, which the server and every client load alike: it can not be
moved or taken away for everybody while the map runs. What the server can do is stop minding one (the command
clipbrush of the engine, CM_SetClipBrush): players are let through there, and a client that still has the brush
predicts a stop first and is then set to where the server has him. The panel turns brushes off and on by their
number in the map file, which the page knows from the file itself. To move a clip the page turns it off and
places a blocking wall of its size, which clients do predict.

The engine keeps the numbers that are off in sv_clipOff. In a layout of the map they are a block of their own
("#{" and a line off=), so that saving, forgetting and starting the map again carry them as they carry the
entities.
=================
*/
#define WA_CLIP_LIST	4096		// the numbers that are off, as text
#define WA_CLIP_LINE	100			// numbers in one line for the console, which takes 1024 characters

// Brush numbers out of what a request or a file says, to the console: digits, and nothing else gets there.
// Returns how many.
static int WA_ClipCommand( qboolean on, const char *numbers ) {
	char		line[WA_CLIP_LINE * 8 + 32];
	const char	*p = numbers;
	int			count = 0, inLine = 0;

	line[0] = '\0';
	while ( *p ) {
		char		*end;
		const long	n = strtol( p, &end, 10 );

		if ( end == p ) {
			// what stands between two numbers
			p++;
			continue;
		}
		p = end;
		if ( n < 0 || n > 0xfffff )
			continue;
		Q_strcat( line, sizeof( line ), va( " %ld", n ) );
		count++;
		if ( ++inLine == WA_CLIP_LINE ) {
			trap->SendConsoleCommand( EXEC_APPEND, va( "clipbrush %s%s\n", on ? "on" : "off", line ) );
			line[0] = '\0';
			inLine = 0;
		}
	}
	if ( inLine )
		trap->SendConsoleCommand( EXEC_APPEND, va( "clipbrush %s%s\n", on ? "on" : "off", line ) );
	return count;
}

// POST /api/clip: brush (one number or several), on (1 or 0). The engine does it in the next frame and says on
// its console which numbers were no clip brush.
void WA_ApiClip( waConnection_t *c, const char *body ) {
	char		list[2048], value[8];
	qboolean	on;
	int			count;

	WA_FormValue( body, "brush", list, sizeof( list ) );
	WA_FormValue( body, "on", value, sizeof( value ) );
	on = (qboolean)( value[0] == '1' );
	WA_LogUse( c, va( "clip %s %.200s", on ? "on" : "off", list ) );

	count = WA_ClipCommand( on, list );
	if ( !count ) {
		WA_ActionResult( c, qfalse, "no clip brush named" );
		return;
	}
	WA_LayoutTouched();
	WA_ActionResult( c, qtrue, va( "%d clip brush%s %s", count, count == 1 ? "" : "es", on ? "on again" : "off: players are let through there" ) );
}

// "clipOff":[1,2,3], for the list of the editor
void WA_ClipsJson( waBuffer_t *b ) {
	char		off[WA_CLIP_LIST];
	const char	*p;
	qboolean	first = qtrue;

	trap->Cvar_VariableStringBuffer( "sv_clipOff", off, sizeof( off ) );
	WA_BufAppend( b, "\"clipOff\":[", 11 );
	for ( p = off; *p; ) {
		char		*end;
		const long	n = strtol( p, &end, 10 );

		if ( end == p )
			break;
		WA_BufPrintf( b, "%s%ld", first ? "" : ",", n );
		first = qfalse;
		p = end;
	}
	WA_BufAppend( b, "],", 2 );
}

// The block of a layout that names the brushes that are off, nothing when none is. b may be NULL, to count
// only. Returns how many there are.
int WA_ClipsLayout( waBuffer_t *b ) {
	char		off[WA_CLIP_LIST];
	const char	*p;
	int			count = 0;

	trap->Cvar_VariableStringBuffer( "sv_clipOff", off, sizeof( off ) );
	for ( p = off; *p; p++ ) {
		if ( *p != ' ' && ( p == off || p[-1] == ' ' ) )
			count++;
	}
	if ( count && b )
		WA_BufPrintf( b, "#{\noff=%s\n}\n", off );
	return count;
}

// the lines of such a block, when the map has started: its brushes are off again. Returns how many.
int WA_ClipsApply( const char *lines ) {
	return strncmp( lines, "off=", 4 ) ? 0 : WA_ClipCommand( qfalse, lines + 4 );
}
