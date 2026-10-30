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

// sv_tv.cpp -- the TV of the server (issue #7): a spectator in no slot of the server and with no connection, as a
// bot that records a demo has none. It follows a player who plays, and what it sees goes into a demo that the
// client of the game plays as it is (demo <name>). Not from OpenJK, see docs/japlus/server.md, "The TV of the
// server".
//
// It is a client_t of its own (tv), not one of svs.clients: no player sees it, it takes no slot, and the game module
// knows nothing of it. Each frame of the server it is sent what a spectator who follows its player is sent
// (SV_SendTVSnapshot of sv_snapshot.cpp): the commands to all clients (SV_SendServerCommand), the configstrings
// (SV_SetConfigstring) and the snapshot, against the one before. The gamestate at the start names a client number
// no player has, so the client of the demo is nobody of the game.
//
//   tvrecord <name>   demos/<name>.dm_26 from now on, until tvstop or the end of the level
//   tvstop
//   sv_tvSwitch       seconds after which it follows the next player who plays (20)

#include "server.h"

static client_t		tv;
static qboolean		tvOn;
static int			tvTarget = -1;		// the player it follows, -1: none yet
static int			tvSwitchTime;		// svs.time when it looks for the next one
static cvar_t		*sv_tvSwitch;

client_t *SV_TVClient( void ) {
	return tvOn ? &tv : NULL;
}

// a player it can follow: in the game, playing, not a spectator himself
static qboolean SV_TVPlays( int i ) {
	const client_t		*cl = &svs.clients[i];
	const playerState_t	*ps;

	if ( cl->state != CS_ACTIVE || !cl->gentity ) {
		return qfalse;
	}
	ps = SV_GameClientNum( i );
	return (qboolean)( ps->pm_type != PM_SPECTATOR && ps->persistant[PERS_TEAM] != TEAM_SPECTATOR && !( ps->pm_flags & PMF_FOLLOW ) );
}

// the one it follows: the same while he plays and his time is not up, else the next one who plays after him
static int SV_TVPick( void ) {
	int i;

	if ( tvTarget >= 0 && SV_TVPlays( tvTarget ) && svs.time < tvSwitchTime ) {
		return tvTarget;
	}
	for ( i = 1; i <= sv_maxclients->integer; i++ ) {
		const int n = ( tvTarget + i + sv_maxclients->integer ) % sv_maxclients->integer;

		if ( SV_TVPlays( n ) ) {
			tvTarget = n;
			tvSwitchTime = svs.time + Q_max( 1, sv_tvSwitch->integer ) * 1000;
			return n;
		}
	}
	tvTarget = -1;
	return -1;
}

// the number the client of the demo has of itself: a slot no player is in, from the top
static int SV_TVOwnNumber( void ) {
	int i;

	for ( i = MAX_CLIENTS - 1; i >= 0; i-- ) {
		if ( i >= sv_maxclients->integer || svs.clients[i].state == CS_FREE ) {
			return i;
		}
	}
	return MAX_CLIENTS - 1;
}

void SV_TVStop( const char *why ) {
	int len = -1;

	if ( !tvOn ) {
		return;
	}
	// the end of a demo: a message number and a length of -1 (as SV_StopRecordDemo)
	FS_Write( &len, 4, tv.demo.demofile );
	FS_Write( &len, 4, tv.demo.demofile );
	FS_FCloseFile( tv.demo.demofile );
	Com_Printf( "TV: demos/%s.dm_%d ends: %s, %i messages\n", tv.demo.demoName, PROTOCOL_VERSION, why,
		tv.netchan.outgoingSequence - 1 );
	tvOn = qfalse;
	tvTarget = -1;
}

static void SV_TVRecord_f( void ) {
	char	name[MAX_OSPATH];
	byte	bufData[MAX_MSGLEN];
	msg_t	msg;
	int		len, swlen;

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "usage: tvrecord <name>\n" );
		return;
	}
	if ( sv.state != SS_GAME ) {
		Com_Printf( "TV: no level runs\n" );
		return;
	}
	SV_TVStop( "a new one starts" );

	Com_Memset( &tv, 0, sizeof( tv ) );
	Q_strncpyz( tv.demo.demoName, Cmd_Argv( 1 ), sizeof( tv.demo.demoName ) );
	Com_sprintf( name, sizeof( name ), "demos/%s.dm_%d", tv.demo.demoName, PROTOCOL_VERSION );
	tv.demo.demofile = FS_FOpenFileWrite( name );
	if ( !tv.demo.demofile ) {
		Com_Printf( "TV: %s can not be written\n", name );
		return;
	}
	Q_strncpyz( tv.name, "TV", sizeof( tv.name ) );
	tv.state = CS_ACTIVE;
	tv.netchan.remoteAddress.type = NA_BOT;
	tv.netchan.outgoingSequence = 1;
	// (a bot that records: the snapshot against the one before, the first one whole, the commands taken as read)
	tv.demo.isBot = qtrue;
	tv.demo.demorecording = qtrue;
	tv.demo.demowaiting = qtrue;
	tvOn = qtrue;
	tvTarget = -1;

	// the gamestate, as the first message of the demo (as SV_RecordDemo)
	MSG_Init( &msg, bufData, sizeof( bufData ) );
	SV_CreateGameStateMessage( &tv, &msg, SV_TVOwnNumber() );
	MSG_WriteByte( &msg, svc_EOF );
	len = tv.netchan.outgoingSequence - 1;
	swlen = LittleLong( len );
	FS_Write( &swlen, 4, tv.demo.demofile );
	swlen = LittleLong( msg.cursize );
	FS_Write( &swlen, 4, tv.demo.demofile );
	FS_Write( msg.data, msg.cursize, tv.demo.demofile );
	Com_Printf( "TV: records %s\n", name );
}

static void SV_TVStop_f( void ) {
	if ( !tvOn ) {
		Com_Printf( "TV: it records nothing\n" );
		return;
	}
	SV_TVStop( "tvstop" );
}

// once a frame of the server, after the snapshots of the clients (SV_SendClientMessages)
void SV_TVFrame( void ) {
	int target;

	if ( !tvOn || sv.state != SS_GAME ) {
		return;
	}
	// (nobody plays: nothing to follow, the demo waits)
	target = SV_TVPick();
	if ( target < 0 ) {
		return;
	}
	SV_SendTVSnapshot( &tv, target );
}

void SV_TVInit( void ) {
	sv_tvSwitch = Cvar_Get( "sv_tvSwitch", "20", CVAR_ARCHIVE, "TV of the server: seconds after which it follows the next player" );
	Cmd_AddCommand( "tvrecord", SV_TVRecord_f, "TV of the server: a demo of a spectator in no slot who follows the players" );
	Cmd_AddCommand( "tvstop", SV_TVStop_f, "TV of the server: ends its demo" );
}
