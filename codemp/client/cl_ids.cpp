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
// What happened this run is kept in a ring for v_idsstatus, and v_idsSeq/v_idsLast tell the cgame of each event,
// which shows it as a notice (hud/cg_notify.c) and keeps the last it showed in v_idsSeqShown. v_idsLast is made of the address and the name of the event in the
// table below, never of what the server sent.
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
	{ "more data of a download than it said it has",								1 },	// CLIDS_DOWNLOAD_SIZE: with v_ids 0 the engine drops the connection itself
	{ "cvars it may not set",														0 },	// CLIDS_CVAR
	{ "a cvar no server may set",													1 },	// CLIDS_CVAR_PROTECTED: with v_ids 0 the engine drops the connection itself
	{ "a list of servers nobody asked for",											0 },	// CLIDS_MASTER
};

static cvar_t		*v_ids, *v_idsSeq;
static fileHandle_t	clIds_file;
static int			clIds_counts[CLIDS_NUM_EVENTS];		// of this connection
static int			clIds_lineTime[CLIDS_NUM_EVENTS], clIds_skipped[CLIDS_NUM_EVENTS];
static int			clIds_commandSecond, clIds_commands;

// servers this client left for the intrusion detection, until the game ends. The first connect to one again is
// refused with the reason, the next one goes through.
#define CLIDS_LEFT			16
static struct {
	netadr_t	adr;
	int			event;
	qboolean	told;					// the player was told once, connects go through
	qboolean	used;
} clIds_left[CLIDS_LEFT];
static int			clIds_nextLeft;		// the slot taken when the list is full, oldest first

// the events of this run, for v_idsstatus
#define CLIDS_RING			32
static struct {
	char		time[12];				// hh:mm:ss
	char		who[64];
	int			event;
	char		detail[CLIDS_DETAIL + 4];
} clIds_ring[CLIDS_RING];
static int			clIds_ringCount;	// all of this run, the ring keeps the last CLIDS_RING

static int CL_IDS_FindLeft( const netadr_t *adr ) {
	for ( int i = 0; i < CLIDS_LEFT; i++ ) {
		if ( clIds_left[i].used && NET_CompareAdr( adr, &clIds_left[i].adr ) ) {
			return i;
		}
	}
	return -1;
}

static void CL_IDS_RememberLeft( const netadr_t *adr, int event ) {
	int i = CL_IDS_FindLeft( adr );

	if ( i < 0 ) {
		i = clIds_nextLeft;
		clIds_nextLeft = ( clIds_nextLeft + 1 ) % CLIDS_LEFT;
	}
	clIds_left[i].adr = *adr;
	clIds_left[i].event = event;
	clIds_left[i].told = qfalse;
	clIds_left[i].used = qtrue;
}

// for the server browser: a server the client left for the intrusion detection
qboolean CL_IDS_Left( const netadr_t *adr ) {
	return (qboolean)( v_ids && v_ids->integer && CL_IDS_FindLeft( adr ) >= 0 );
}

/*
=================
CL_IDS_CheckConnect

Before a connect. May not return: the first connect to a server the client left for the intrusion detection
ends here, with the reason.
=================
*/
void CL_IDS_CheckConnect( const netadr_t *adr ) {
	const int i = CL_IDS_FindLeft( adr );

	if ( !v_ids || !v_ids->integer || i < 0 || clIds_left[i].told ) {
		return;
	}
	clIds_left[i].told = qtrue;
	Com_Error( ERR_DROP, "You left %s before: it sent %s.\nThe intrusion detection of the client did that (v_ids), ids.log in the game folder has the details. Connect again to join anyway.",
		NET_AdrToString( adr ), clIds_events[clIds_left[i].event].name );
}

/*
=================
CL_IDS_Status_f / CL_IDS_Forget_f
=================
*/
static void CL_IDS_Status_f( void ) {
	int i, shown = 0;

	Com_Printf( "v_ids %i (1 on, 0 off)\n", v_ids->integer );
	if ( !clIds_ringCount ) {
		Com_Printf( "  nothing this run\n" );
	} else if ( clIds_ringCount > CLIDS_RING ) {
		Com_Printf( "  the last %i of %i events this run:\n", CLIDS_RING, clIds_ringCount );
	}
	for ( i = Q_max( 0, clIds_ringCount - CLIDS_RING ); i < clIds_ringCount; i++ ) {
		const int n = i % CLIDS_RING;

		Com_Printf( "  %s  %s sent %s", clIds_ring[n].time, clIds_ring[n].who, clIds_events[clIds_ring[n].event].name );
		Com_Printf( clIds_ring[n].detail[0] ? ": %s\n" : "%s\n", clIds_ring[n].detail );
	}
	for ( i = 0; i < CLIDS_LEFT; i++ ) {
		if ( !clIds_left[i].used ) {
			continue;
		}
		if ( !shown++ ) {
			Com_Printf( "servers left this run:\n" );
		}
		Com_Printf( "  %s, it sent %s%s\n", NET_AdrToString( &clIds_left[i].adr ), clIds_events[clIds_left[i].event].name,
			clIds_left[i].told ? "" : " (the next connect is refused once)" );
	}
	Com_Printf( "v_idsforget <address|all> forgets a server that was left. The events are in " CLIDS_LOG " of the game folder.\n" );
}

static void CL_IDS_Forget_f( void ) {
	const char		*arg = Cmd_Argv( 1 );
	const qboolean	all = (qboolean)!Q_stricmp( arg, "all" );
	netadr_t		adr;
	int				i, count = 0;

	if ( !arg[0] || ( !all && !NET_StringToAdr( arg, &adr ) ) ) {
		Com_Printf( "usage: v_idsforget <address|all>\n" );
		return;
	}
	for ( i = 0; i < CLIDS_LEFT; i++ ) {
		if ( clIds_left[i].used && ( all || NET_CompareAdr( &adr, &clIds_left[i].adr ) ) ) {
			clIds_left[i].used = qfalse;
			count++;
		}
	}
	Com_Printf( "%i servers forgotten\n", count );
}

void CL_IDS_Init( void ) {
	v_ids = Cvar_Get( "v_ids", "1", CVAR_ARCHIVE, "Intrusion detection of the client: 1 what a server sends that no server of the game does is written to ids.log, and a server that keeps at it is left. 0 off" );
	v_idsSeq = Cvar_Get( "v_idsSeq", "0", CVAR_ROM, "Intrusion detection: counts the events of this run, for the notices of the cgame" );
	Cvar_Get( "v_idsLast", "", CVAR_ROM, "Intrusion detection: the last event, as the notice of the cgame shows it" );
	Cvar_Get( "v_idsSeqShown", "0", 0, "Intrusion detection: the v_idsSeq the cgame has shown a notice for" );
	Cmd_AddCommand( "v_idsstatus", CL_IDS_Status_f, "The events of the intrusion detection this run, and the servers it left" );
	Cmd_AddCommand( "v_idsforget", CL_IDS_Forget_f, "Forgets a server the intrusion detection left, or all of them" );
}

// a new connection: what the last server did is not held against this one
void CL_IDS_Connect( void ) {
	memset( clIds_counts, 0, sizeof( clIds_counts ) );
	memset( clIds_skipped, 0, sizeof( clIds_skipped ) );
	clIds_commands = 0;
}

void CL_IDS_Shutdown( void ) {
	Cmd_RemoveCommand( "v_idsstatus" );
	Cmd_RemoveCommand( "v_idsforget" );
	if ( clIds_file ) {
		FS_FCloseFile( clIds_file );
		clIds_file = 0;
	}
}

// into the ring, and the notice of the cgame
static void CL_IDS_Remember( clIdsEvent_t event, const netadr_t *from, const char *detail ) {
	const int	n = clIds_ringCount++ % CLIDS_RING;
	const char	*adr = NET_AdrToString( from ? from : &clc.serverAddress );
	qtime_t		t;

	Com_RealTime( &t );
	Com_sprintf( clIds_ring[n].time, sizeof( clIds_ring[n].time ), "%02i:%02i:%02i", t.tm_hour, t.tm_min, t.tm_sec );
	Com_sprintf( clIds_ring[n].who, sizeof( clIds_ring[n].who ), from ? "%s" : "the server %s", adr );
	clIds_ring[n].event = event;
	Q_strncpyz( clIds_ring[n].detail, Com_IDSDetail( detail, CLIDS_DETAIL ), sizeof( clIds_ring[n].detail ) );

	Cvar_Set( "v_idsLast", va( "IDS: %s sent %s", adr, clIds_events[event].name ) );
	Cvar_Set( "v_idsSeq", va( "%i", v_idsSeq->integer + 1 ) );
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
	CL_IDS_Remember( event, from, detail );

	// only for what came over the connection: a connectionless packet can name any sender
	if ( !from && clIds_events[event].leaveAt && clIds_counts[event] >= clIds_events[event].leaveAt
		&& cls.state >= CA_CONNECTED && !clc.demoplaying ) {
		clIds_counts[event] = 0;
		CL_IDS_RememberLeft( &clc.serverAddress, event );
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
