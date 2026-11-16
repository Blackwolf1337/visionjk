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

// cl_confeed.cpp -- the feed of the notify lines for the cgame of this tree

#include "client.h"

/*
================
The feed

VisionJK: every line that would stand at the top of the game window (the notify lines) is kept here whole, with
its colours and a level: 0 a line, 1 a warning, 2 an error, as Com_PrintWarning and Com_PrintError said it
(com_printLevel; the colour of a line says nothing). A cgame of this tree draws them itself, in one feed
with its kills (cgame/hud/cg_killfeed.c): it reads them with ext.Con_FeedRead and says so by setting
v_feedHandler, which the engine clears whenever a cgame is loaded. Con_DrawNotify leaves the lines out then.
================
*/
#define CON_FEED_LINES	32
#define CON_FEED_TEXT	256

static struct {
	int		seq;							// of the newest line, 0: none yet
	struct {
		int		level;
		char	text[CON_FEED_TEXT];
	} lines[CON_FEED_LINES];
	char	pending[CON_FEED_TEXT];			// the line that has no end yet
	int		pendingLen;
	qboolean pendingSkip;					//	and something of it is not for the notify lines
	int		pendingLevel;					//	the highest level of what it was printed with
} conFeed;

void Con_FeedAdd( const char *txt, qboolean skipnotify ) {
	for ( ; *txt; txt++ ) {
		if ( *txt == '\n' ) {
			conFeed.pending[conFeed.pendingLen] = '\0';
			if ( !conFeed.pendingSkip && conFeed.pendingLen > 0 ) {
				const int slot = ++conFeed.seq % CON_FEED_LINES;

				conFeed.lines[slot].level = conFeed.pendingLevel;
				Q_strncpyz( conFeed.lines[slot].text, conFeed.pending, sizeof( conFeed.lines[0].text ) );
			}
			conFeed.pendingLen = 0;
			conFeed.pendingSkip = qfalse;
			conFeed.pendingLevel = 0;
			continue;
		}
		if ( skipnotify ) {
			conFeed.pendingSkip = qtrue;
		}
		if ( com_printLevel > conFeed.pendingLevel ) {
			conFeed.pendingLevel = com_printLevel;
		}
		if ( *txt != '\r' && conFeed.pendingLen < CON_FEED_TEXT - 1 ) {
			conFeed.pending[conFeed.pendingLen++] = *txt;
		}
	}
}

// ext.Con_FeedRead: the line after number `after` (0: from the oldest kept), its number returned, 0: none
int Con_FeedRead( int after, int *level, char *out, int outSize ) {
	int next = after + 1;

	if ( after < 0 || after >= conFeed.seq ) {
		return 0;
	}
	if ( next <= conFeed.seq - CON_FEED_LINES ) {
		next = conFeed.seq - CON_FEED_LINES + 1;
	}
	if ( level ) {
		*level = conFeed.lines[next % CON_FEED_LINES].level;
	}
	Q_strncpyz( out, conFeed.lines[next % CON_FEED_LINES].text, outSize );
	return next;
}

// the number of the newest line: a cgame that starts reads from here on
int Con_FeedNewest( void ) {
	return conFeed.seq;
}

// a cgame of this tree draws the notify lines itself: Con_DrawNotify leaves them out
qboolean Con_FeedByCgame( void ) {
	return (qboolean)( cls.cgameStarted && cls.state == CA_ACTIVE && Cvar_VariableIntegerValue( "v_feedHandler" ) );
}
