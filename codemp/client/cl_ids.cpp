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

// cl_ids.cpp -- the intrusion detection of the client. Not from OpenJK.
//
// A client believes a lot of what a server sends, and whoever runs a server can send anything. The checks of the
// engine throw away what does not fit (a message that is cut off, an index past its table, a download nobody
// asked for). This file is told about each of them, writes them into ids.log of the game folder and onto the
// console, and with v_ids 1 leaves a server that keeps doing what no server of the game does. The server side
// of it is server/sv_ids.cpp.
//
// Two kinds of sender are told apart. What comes over the connection is from the server the player is on: a
// server that sends a folder as its game, data of a download that was never asked for or fragments that do not
// fit is left. A connectionless packet can carry any address, so one that claims to be from the server and is
// not is written down and never acted on: leaving for it would let anybody throw a player off any server.
//
// v_ids 0: nothing is written down and nobody is left for it (the checks of the engine stay).

#include "client.h"

#define CLIDS_LOG			"ids.log"
#define CLIDS_LINE_MSEC		2000
#define CLIDS_DETAIL		96
#define CLIDS_COMMANDS		400			// server commands in a second that are a flood

static const struct {
	const char	*name;
	int			leaveAt;				// the client leaves the server at that many of them, 0: never
} clIds_events[CLIDS_NUM_EVENTS] = {
	{ "a connectionless packet that claims to be from the server and is not",	0 },	// CLIDS_OOB_SOURCE
	{ "an echo request from somebody who is not the server",						0 },	// CLIDS_ECHO
	{ "a message that can not be read",												0 },	// CLIDS_MESSAGE: the engine drops the connection itself
	{ "data of a download that was not asked for",									3 },	// CLIDS_DOWNLOAD
	{ "a fragment that does not fit its message",									3 },	// CLIDS_FRAGMENT
	{ "a path in the place of its game folder",										1 },	// CLIDS_GAMEDIR
	{ "more commands in a second than a game sends",								0 },	// CLIDS_COMMAND_FLOOD
	{ "an escape character in a command, which is for a terminal and not for a game",	0 },	// CLIDS_COMMAND_TEXT
	{ "more data of a download than it said it has",								0 },	// CLIDS_DOWNLOAD_SIZE: the engine drops the connection itself
	{ "cvars it may not set",														0 },	// CLIDS_CVAR: a protected one ends the connection itself
	{ "a list of servers nobody asked for",											0 },	// CLIDS_MASTER
};

static cvar_t		*v_ids;
static fileHandle_t	clIds_file;
static int			clIds_counts[CLIDS_NUM_EVENTS];		// of this connection
static int			clIds_lineTime[CLIDS_NUM_EVENTS], clIds_skipped[CLIDS_NUM_EVENTS];
static int			clIds_commandSecond, clIds_commands;

void CL_IDS_Init( void ) {
	v_ids = Cvar_Get( "v_ids", "1", CVAR_ARCHIVE, "Intrusion detection of the client: 1 what a server sends that no server of the game does is written to ids.log, and a server that keeps at it is left. 0 off" );
}

// a new connection: what the last server did is not held against this one
void CL_IDS_Connect( void ) {
	memset( clIds_counts, 0, sizeof( clIds_counts ) );
	memset( clIds_skipped, 0, sizeof( clIds_skipped ) );
	clIds_commands = 0;
}

void CL_IDS_Shutdown( void ) {
	if ( clIds_file ) {
		FS_FCloseFile( clIds_file );
		clIds_file = 0;
	}
}

/*
=================
CL_IDS_Event

from: who sent it, NULL for the server the client is connected to. detail: what it was, NULL for nothing to add.
May not return: with v_ids 1 the connection ends here when the server did it once too often.
=================
*/
void CL_IDS_Event( clIdsEvent_t event, const netadr_t *from, const char *detail ) {
	const int	now = Sys_Milliseconds();
	char		line[512];

	if ( !v_ids || !v_ids->integer || event < 0 || event >= CLIDS_NUM_EVENTS ) {
		return;
	}
	clIds_counts[event]++;

	// at most a line in two seconds for an event, the rest is counted
	if ( clIds_lineTime[event] && now - clIds_lineTime[event] < CLIDS_LINE_MSEC && now >= clIds_lineTime[event]
		&& !( clIds_events[event].leaveAt && clIds_counts[event] >= clIds_events[event].leaveAt ) ) {
		clIds_skipped[event]++;
		return;
	}

	Com_sprintf( line, sizeof( line ), "%s sent %s", from ? NET_AdrToString( from ) : va( "the server %s", NET_AdrToString( &clc.serverAddress ) ),
		clIds_events[event].name );
	if ( detail && *detail ) {
		Q_strcat( line, sizeof( line ), va( ": %s", Com_IDSDetail( detail, CLIDS_DETAIL ) ) );
	}
	if ( clIds_skipped[event] ) {
		Q_strcat( line, sizeof( line ), va( " (and %i more of it)", clIds_skipped[event] ) );
	}
	clIds_lineTime[event] = now ? now : 1;
	clIds_skipped[event] = 0;

	Com_IDSLine( &clIds_file, CLIDS_LOG, line );

	// only for what came over the connection: a connectionless packet can name any sender
	if ( !from && clIds_events[event].leaveAt && clIds_counts[event] >= clIds_events[event].leaveAt
		&& cls.state >= CA_CONNECTED && !clc.demoplaying ) {
		clIds_counts[event] = 0;
		Com_Error( ERR_DROP, "Left the server: it sent %s.\nThe intrusion detection of the client did that (v_ids), ids.log in the game folder has the details.",
			clIds_events[event].name );
	}
}

/*
=================
CL_IDS_ServerCommand

A command of the server before it is kept for the game. An escape character means nothing to the game: it is
there for the terminal the console of the client may be printed to, where it can move the cursor, rewrite what
stands there and on some terminals do more. It is taken out. Counts the commands of a second as well.
=================
*/
void CL_IDS_ServerCommand( char *text ) {
	const int	second = Sys_Milliseconds() / 1000;
	qboolean	escape = qfalse;
	char		*s;

	for ( s = text; *s; s++ ) {
		if ( *s == 0x1b ) {
			*s = '.';
			escape = qtrue;
		}
	}
	if ( escape ) {
		CL_IDS_Event( CLIDS_COMMAND_TEXT, NULL, text );
	}

	if ( second != clIds_commandSecond ) {
		clIds_commandSecond = second;
		clIds_commands = 0;
	}
	if ( ++clIds_commands == CLIDS_COMMANDS ) {
		CL_IDS_Event( CLIDS_COMMAND_FLOOD, NULL, NULL );
	}
}
