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

// sv_movecheck.cpp -- notes the timing of the moves of a player that no stock client sends. Not from OpenJK.
//
// It only writes down, the game does the blocking: ClientThink_real cuts a single move to vs_maxCmdMsec, so a
// player can no longer cover half a second in one move and appear somewhere else (g_active.c).
//
// - Warp: the newest move of a packet is far more ahead of the one before than the time that passed between the
//   two packets. A stock client stamps every move with its clock, so both grow alike, after lost packets and after
//   a freeze of the client too. A client that held its moves back (the old h_teleport cheat sends its moves 850 ms
//   in the past and then jumps back to the present, a lag switch holds whole packets) does not.
// - No moves: packets of a player in the game that end before any move (the old h_dontsend cheat). A stock client
//   puts at least one move into every packet once it is in the game.
// - Both are IDS events with a weight of 0: ids.log and the console get a line, nobody is blocked or dropped.
//   The first of them for a player in a minute also writes the last few seconds of his packets to
//   anticheat/<date>_<address>.csv in the game folder, so a cheat leaves numbers to look at.
//
// A clock that runs fast is not looked for: see the note at the top of sv_ids.cpp.

#include "server.h"

#define MOVECHECK_WARP_MSEC		300		// moves this far more ahead than the real time that passed
#define MOVECHECK_REF_MSEC		500		// compared with the packet of that long ago
#define MOVECHECK_NOMOVE		25		// packets in a row without a move
#define MOVECHECK_DUMP_MSEC		60000	// one csv per player in that time

static void SV_MoveCheck_Record( client_t *cl, int now, int newest, int newCmds, int cmdCount ) {
	moveCheck_t			*mc = &cl->moveCheck;
	moveCheckPacket_t	*p = &mc->ring[mc->head];

	p->arrival = now;
	p->svTime = sv.time;
	p->newest = newest;
	p->newCmds = (short)newCmds;
	p->cmdCount = (short)cmdCount;
	mc->head = ( mc->head + 1 ) % MOVECHECK_RING;
	if ( mc->count < MOVECHECK_RING ) {
		mc->count++;
	}
}

/*
=================
SV_MoveCheck_Before

The newest packet with moves that came at or before time, NULL if the ring has none that old. The packet
just recorded is not looked at.
=================
*/
static const moveCheckPacket_t *SV_MoveCheck_Before( const moveCheck_t *mc, int time ) {
	int i;

	for ( i = 2; i <= mc->count; i++ ) {
		const moveCheckPacket_t *p = &mc->ring[( mc->head - i + MOVECHECK_RING ) % MOVECHECK_RING];

		if ( p->arrival <= time && p->newest > 0 ) {
			return p;		// (a packet without moves has no time to compare)
		}
	}
	return NULL;
}

/*
=================
SV_MoveCheck_Dump

The packets in the ring as a table: when each came (msec, the first one is 0), the time of the server then, the
newest move in it, how far that is ahead of the server (negative: behind), how many of its moves were new and how
many it carried. A packet without moves has 0 moves and no newest time.
=================
*/
static void SV_MoveCheck_Dump( client_t *cl, const char *why ) {
	moveCheck_t		*mc = &cl->moveCheck;
	const int		now = Sys_Milliseconds();
	char			path[MAX_QPATH], address[64], line[256];
	qtime_t			t = {};
	fileHandle_t	f;
	int				i, first;

	if ( mc->dumpTime && now - mc->dumpTime < MOVECHECK_DUMP_MSEC ) {
		return;
	}
	mc->dumpTime = now;

	Q_strncpyz( address, NET_AdrToString( &cl->netchan.remoteAddress ), sizeof( address ) );
	for ( i = 0; address[i]; i++ ) {
		if ( address[i] == ':' || address[i] == '[' || address[i] == ']' ) {
			address[i] = '_';		// not in a file name on Windows
		}
	}
	Com_RealTime( &t );
	Com_sprintf( path, sizeof( path ), "anticheat/%04i%02i%02i_%02i%02i%02i_%s.csv", 1900 + t.tm_year, 1 + t.tm_mon,
		t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, address );
	f = FS_FOpenFileWrite( path );
	if ( !f ) {
		return;
	}

	Com_sprintf( line, sizeof( line ), "# %s, client %i, %s\n", Com_IDSDetail( cl->name, 64 ), (int)( cl - svs.clients ), why );
	FS_Write( line, (int)strlen( line ), f );
	Q_strncpyz( line, "arrival,svTime,newest,ahead,newCmds,cmdCount\n", sizeof( line ) );
	FS_Write( line, (int)strlen( line ), f );

	first = ( mc->head - mc->count + MOVECHECK_RING ) % MOVECHECK_RING;
	for ( i = 0; i < mc->count; i++ ) {
		const moveCheckPacket_t *p = &mc->ring[( first + i ) % MOVECHECK_RING];

		if ( p->cmdCount ) {
			Com_sprintf( line, sizeof( line ), "%i,%i,%i,%i,%i,%i\n", p->arrival - mc->ring[first].arrival, p->svTime,
				p->newest, p->newest - p->svTime, p->newCmds, p->cmdCount );
		} else {
			Com_sprintf( line, sizeof( line ), "%i,%i,,,0,0\n", p->arrival - mc->ring[first].arrival, p->svTime );
		}
		FS_Write( line, (int)strlen( line ), f );
	}
	FS_FCloseFile( f );
	Com_Printf( "Move check: wrote %s\n", path );
}

/*
=================
SV_MoveCheck_Moves

A packet with moves, before they are run. cmds are the moves as read, the old ones that cl_packetdup repeats
included.
=================
*/
void SV_MoveCheck_Moves( client_t *cl, const usercmd_t *cmds, int cmdCount ) {
	moveCheck_t	*mc = &cl->moveCheck;
	const int	now = Sys_Milliseconds();
	const int	newest = cmds[cmdCount - 1].serverTime;
	int			i, newCmds = 0;

	if ( cl->state != CS_ACTIVE || cl->netchan.remoteAddress.type == NA_BOT ) {
		return;
	}
	for ( i = 0; i < cmdCount; i++ ) {
		if ( cmds[i].serverTime > cl->lastUsercmd.serverTime ) {
			newCmds++;
		}
	}
	SV_MoveCheck_Record( cl, now, newest, newCmds, cmdCount );
	mc->noMove = 0;

	// Until its first snapshot a client stamps its moves with 0, then with the time of the server: that first step
	// is no warp. After a map change the times start over, so the newest time is followed down as well.
	if ( newest <= 0 ) {
		return;
	}
	// Compared with a packet of some time ago, not with the one before: after the client stood still for a moment
	// (a hitch, a debugger, the window dragged) its first packet still carries the old time and the next one the new
	// time a millisecond later. Over the half second both grew alike.
	if ( mc->lastNewest > 0 && newest > mc->lastNewest ) {
		const moveCheckPacket_t *ref = SV_MoveCheck_Before( mc, now - MOVECHECK_REF_MSEC );

		if ( ref ) {
			const int ahead = ( newest - ref->newest ) - ( now - ref->arrival );

			if ( ahead > MOVECHECK_WARP_MSEC ) {
				const char *detail = va( "%i ms of moves in %i ms", newest - ref->newest, now - ref->arrival );

				SV_IDS_Event( &cl->netchan.remoteAddress, cl, IDS_CMD_WARP, detail );
				SV_MoveCheck_Dump( cl, detail );
			}
		}
	}
	mc->lastNewest = newest;
}

/*
=================
SV_MoveCheck_NoMove

A packet of a player that ended before any move.
=================
*/
void SV_MoveCheck_NoMove( client_t *cl ) {
	moveCheck_t *mc = &cl->moveCheck;

	if ( cl->state != CS_ACTIVE || cl->netchan.remoteAddress.type == NA_BOT ) {
		return;
	}
	SV_MoveCheck_Record( cl, Sys_Milliseconds(), 0, 0, 0 );
	if ( ++mc->noMove == MOVECHECK_NOMOVE ) {
		const char *detail = va( "%i packets in a row without a move", MOVECHECK_NOMOVE );

		SV_IDS_Event( &cl->netchan.remoteAddress, cl, IDS_CMD_NOMOVE, detail );
		SV_MoveCheck_Dump( cl, detail );
	}
}
