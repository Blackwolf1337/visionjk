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

// ui_ids.c -- the list of the window "Intrusions" of the server browser (FEEDER_IDSLOG, ui/jamp/ids.menu): the last
// lines of ids.log, which the intrusion detection of the engine writes (client/cl_ids.cpp), the newest first. Read
// when the window opens (uiScript IDSLog). Not from JA+.
//
// The engine cleans what it writes there (Com_IDSDetail), so a line has no colour codes or control characters
// a server could have put in. They are taken out here again all the same.

#include "ui_local.h"

#define IDS_LOG			"ids.log"
#define IDS_LINES		64
#define IDS_LINE		192

static char	idsLines[IDS_LINES][IDS_LINE];
static int	idsCount;		// lines of the file, the list keeps the last IDS_LINES

static void UI_IDSKeep( const char *line, int len ) {
	char	*out = idsLines[idsCount++ % IDS_LINES];
	int		i, n = 0;

	for ( i = 0; i < len && n < IDS_LINE - 1; i++ ) {
		const unsigned char c = (unsigned char)line[i];

		if ( c == Q_COLOR_ESCAPE ) {
			out[n++] = '.';
		} else {
			out[n++] = ( c < ' ' || c == 127 ) ? '.' : (char)c;
		}
	}
	out[n] = '\0';
}

// uiScript IDSLog. The file is read through once (the ui has no seek), in pieces.
void UI_IDSLoad( void ) {
	char			buf[4096], line[IDS_LINE];
	fileHandle_t	f;
	int				left, lineLen = 0;

	idsCount = 0;
	left = trap->FS_Open( IDS_LOG, &f, FS_READ );
	if ( !f ) {
		return;
	}
	while ( left > 0 ) {
		const int	n = Q_min( left, (int)sizeof( buf ) );
		int			i;

		trap->FS_Read( buf, n, f );
		left -= n;
		for ( i = 0; i < n; i++ ) {
			if ( buf[i] == '\n' ) {
				if ( lineLen ) {
					UI_IDSKeep( line, lineLen );
				}
				lineLen = 0;
			} else if ( lineLen < IDS_LINE - 1 ) {
				line[lineLen++] = buf[i];
			}
		}
	}
	if ( lineLen ) {
		UI_IDSKeep( line, lineLen );
	}
	trap->FS_Close( f );
}

int UI_IDSCount( void ) {
	return Q_min( idsCount, IDS_LINES );
}

// index 0 is the newest line
const char *UI_IDSText( int index ) {
	if ( index < 0 || index >= UI_IDSCount() ) {
		return "";
	}
	return idsLines[( idsCount - 1 - index ) % IDS_LINES];
}
