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

// cl_join.cpp -- the join report: how long each step of joining a server took

#include "client.h"

/*
===============================================================================

THE JOIN REPORT

v_joinReport 1: one line when the game starts after a connect, how long each step of joining took. The load
itself is told apart by v_loadReport (qcommon/com_load.cpp).

===============================================================================
*/

static struct {
	qboolean	active;
	int			at[JOIN_NUM_STEPS];			// Sys_Milliseconds(), 0 for a step that did not come
	int			gamestatePackets, gamestateBytes;
	int			snapshotPackets, snapshotBytes;
} join;

static int CL_JoinSpan( joinStep_t from, joinStep_t to ) {
	return join.at[from] && join.at[to] ? join.at[to] - join.at[from] : -1;
}

void CL_JoinStep( joinStep_t step ) {
	if ( step == JOIN_CONNECT ) {
		Com_Memset( &join, 0, sizeof( join ) );
		join.active = (qboolean)( v_joinReport && v_joinReport->integer );
	}
	if ( !join.active || join.at[step] ) {
		return;		// (a second gamestate after a download: the first one counts)
	}
	join.at[step] = Sys_Milliseconds();
	if ( !join.at[step] ) {
		join.at[step] = 1;
	}
	if ( step != JOIN_ACTIVE ) {
		return;
	}

	join.active = qfalse;
	// the challenge is skipped for an address of the LAN
	const joinStep_t before = join.at[JOIN_CHALLENGE] ? JOIN_CHALLENGE : JOIN_CONNECT;
	Com_Printf( "join of %s: challenge %d ms, connect %d, first packet %d, gamestate %d (%d packets, %d bytes), "
		"downloads %d, load %d, first snapshot %d (%d packets, %d bytes), total %d ms\n",
		NET_AdrToString( &clc.serverAddress ), CL_JoinSpan( JOIN_CONNECT, JOIN_CHALLENGE ),
		CL_JoinSpan( before, JOIN_CONNECTED ), CL_JoinSpan( JOIN_CONNECTED, JOIN_FIRST_PACKET ),
		CL_JoinSpan( JOIN_FIRST_PACKET, JOIN_GAMESTATE ), join.gamestatePackets, join.gamestateBytes,
		CL_JoinSpan( JOIN_GAMESTATE, JOIN_LOAD ), CL_JoinSpan( JOIN_LOAD, JOIN_PRIMED ),
		CL_JoinSpan( JOIN_PRIMED, JOIN_ACTIVE ), join.snapshotPackets, join.snapshotBytes,
		CL_JoinSpan( JOIN_CONNECT, JOIN_ACTIVE ) );
}

// v_joinReport 2: every packet between connectResponse and the first snapshot, by the time since connectResponse
static void CL_JoinTrace( const char *what, int bytes ) {
	if ( join.active && v_joinReport->integer >= 2 && join.at[JOIN_CONNECTED] ) {
		Com_Printf( "join: +%d ms %s %d bytes (%s)\n", Sys_Milliseconds() - join.at[JOIN_CONNECTED], what, bytes,
			cls.state == CA_CONNECTED ? "connected" : cls.state == CA_PRIMED ? "primed" : "loading" );
	}
}

void CL_JoinSent( int bytes ) {
	CL_JoinTrace( "sent", bytes );
}

// a packet of the server, before the netchan puts its fragments together
void CL_JoinPacket( int bytes ) {
	if ( !join.active ) {
		return;
	}
	CL_JoinTrace( "got", bytes );
	if ( cls.state == CA_CONNECTED && !join.at[JOIN_GAMESTATE] ) {
		CL_JoinStep( JOIN_FIRST_PACKET );
		join.gamestatePackets++;
	} else if ( cls.state == CA_PRIMED ) {
		join.snapshotPackets++;
		join.snapshotBytes += bytes;
	}
}

void CL_JoinGamestate( int bytes ) {
	if ( join.active && !join.at[JOIN_GAMESTATE] ) {
		join.gamestateBytes = bytes;
	}
	CL_JoinStep( JOIN_GAMESTATE );
}
