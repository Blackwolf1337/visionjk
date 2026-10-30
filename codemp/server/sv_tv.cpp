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
// bot that records a demo has none. It follows a player who plays, and what it sees is a demo: into a file
// (tvrecord), and live to the viewers of the web host (GET /tv, server/web/sv_web_tv.cpp), whose client plays it as
// a demo that does not end. Not from OpenJK, see docs/japlus/server.md, "The TV of the server".
//
// It is a client_t of its own (tv), not one of svs.clients: no player sees it, it takes no slot, and the game module
// knows nothing of it. Each frame of the server it is sent what a spectator who follows its player is sent
// (SV_SendTVSnapshot of sv_snapshot.cpp): the commands to all clients (SV_SendServerCommand), the configstrings
// (SV_SetConfigstring) and the snapshot, against the one before. The gamestate at the start names a client number
// no player has, so the client of the demo is nobody of the game.
//
// A message of the TV is a record of a demo: its number and its length (4 bytes each, little endian) and the
// message, which ends with svc_EOF. The first one is the gamestate (SV_TVGamestate), made anew for each viewer
// that comes and for a file that begins; it names the commands so far as received. A snapshot is sent against
// the one before, and whole when a viewer waits for that (one that came, or fell behind) or a file begins.
// It only runs while somebody watches or records: else its commands are taken as read and nothing is built.
//
//   tvrecord <name>   demos/<name>.dm_26 from now on, until tvstop or the end of the level
//   tvstop
//   sv_tvSwitch       seconds after which it follows the next player who plays (20)

#include "server.h"

#define TV_RECORD_HEAD	8			// the number and the length in front of a message

static client_t		tv;
static qboolean		tvLevel;			// tv is set up for the level that runs
static qboolean		tvIdle = qtrue;		// nobody watched or recorded in the frame before
static qboolean		tvFull;				// the next snapshot is sent whole
static int			tvOwnNumber;		// the number the client of the demo has of itself
static int			tvSequence = 1;		// the number of the next message, also over a change of the level: the
										//	client of a viewer refuses numbers that go back
static fileHandle_t	tvFile;
static char			tvName[MAX_QPATH];
static int			tvTarget = -1;		// the player it follows, -1: none yet
static int			tvSwitchTime;		// svs.time when it looks for the next one
static playerState_t	tvStand;		// where it stands while nobody plays
static qboolean		tvStandSet;			// tvStand is of this level
static cvar_t		*sv_tvSwitch;

// the TV while a level runs, for the commands to all clients and the configstrings
client_t *SV_TVClient( void ) {
	return tvLevel ? &tv : NULL;
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

// it stands as a spectator of its own where the view of from is (the client of the demo needs a snapshot to begin)
static void SV_TVStandAt( const playerState_t *from ) {
	Com_Memset( &tvStand, 0, sizeof( tvStand ) );
	tvStand.pm_type = PM_SPECTATOR;
	tvStand.clientNum = tvOwnNumber;
	tvStand.persistant[PERS_TEAM] = TEAM_SPECTATOR;
	if ( from ) {
		VectorCopy( from->origin, tvStand.origin );
		VectorCopy( from->viewangles, tvStand.viewangles );
		tvStand.viewheight = from->viewheight;
		tvStandSet = qtrue;
	}
}

// nobody plays: where the last player it followed was, else where a spectator is (at the intermission point when he
// just came), else in the middle of the level until one comes
static const playerState_t *SV_TVStand( void ) {
	int i;

	if ( !tvStandSet ) {
		for ( i = 0; i < sv_maxclients->integer && !tvStandSet; i++ ) {
			if ( svs.clients[i].state == CS_ACTIVE && svs.clients[i].gentity ) {
				SV_TVStandAt( SV_GameClientNum( i ) );
			}
		}
		if ( !tvStandSet ) {
			SV_TVStandAt( NULL );
		}
	}
	tvStand.commandTime = sv.time;
	return &tvStand;
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

// the TV for the level that runs (once a level, when it is first wanted)
static void SV_TVSetUp( void ) {
	if ( tvLevel ) {
		return;
	}
	Com_Memset( &tv, 0, sizeof( tv ) );
	Q_strncpyz( tv.name, "TV", sizeof( tv.name ) );
	tv.state = CS_ACTIVE;
	tv.netchan.remoteAddress.type = NA_BOT;
	tv.netchan.outgoingSequence = tvSequence;
	// (a bot that records: the snapshot against the one before, the commands taken as read)
	tv.demo.isBot = qtrue;
	tv.demo.demorecording = qtrue;
	tvOwnNumber = SV_TVOwnNumber();
	tvTarget = -1;
	tvStandSet = qfalse;
	tvFull = qtrue;
	tvLevel = qtrue;
}

static int SV_TVRecordHead( byte *out, int sequence, int len ) {
	const int seq = LittleLong( sequence ), length = LittleLong( len );

	Com_Memcpy( out, &seq, 4 );
	Com_Memcpy( out + 4, &length, 4 );
	return TV_RECORD_HEAD;
}

/*
=================
SV_TVGamestate

The first record for a viewer or a file: the gamestate as it is now. Made from a copy of the TV, so that the
commands it names as received are not taken from the others. 0: no level runs.
=================
*/
int SV_TVGamestate( byte *out, int outSize ) {
	static client_t	copy;
	msg_t			msg;

	if ( sv.state != SS_GAME ) {
		return 0;
	}
	SV_TVSetUp();
	copy = tv;
	MSG_Init( &msg, out + TV_RECORD_HEAD, outSize - TV_RECORD_HEAD );
	SV_CreateGameStateMessage( &copy, &msg, tvOwnNumber );
	MSG_WriteByte( &msg, svc_EOF );
	if ( msg.overflowed ) {
		return 0;
	}
	// a snapshot after it is whole: what came before it is not there for the newcomer
	tvFull = qtrue;
	return SV_TVRecordHead( out, tv.netchan.outgoingSequence - 1, msg.cursize ) + msg.cursize;
}

// the end of the file: a message number and a length of -1 (as SV_StopRecordDemo)
static void SV_TVCloseFile( const char *why ) {
	int len = -1;

	if ( !tvFile ) {
		return;
	}
	FS_Write( &len, 4, tvFile );
	FS_Write( &len, 4, tvFile );
	FS_FCloseFile( tvFile );
	tvFile = 0;
	Com_Printf( "TV: demos/%s.dm_%d ends: %s\n", tvName, PROTOCOL_VERSION, why );
}

// a command overflow (SV_AddServerCommand): the viewers start again from a gamestate, the file ends
void SV_TVStop( const char *why ) {
	SV_TVCloseFile( why );
	if ( tvLevel ) {
		tvSequence = tv.netchan.outgoingSequence + 1;
	}
	tvLevel = qfalse;
	SV_WebTVResync();
}

// the level ends (SV_SpawnServer, SV_Shutdown): a file can not go on into the next one, the viewers can
void SV_TVLevelEnd( void ) {
	SV_TVStop( "the level ends" );
}

static void SV_TVRecord_f( void ) {
	static byte	record[MAX_MSGLEN + TV_RECORD_HEAD];
	char		name[MAX_OSPATH];
	int			len;

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "usage: tvrecord <name>\n" );
		return;
	}
	if ( sv.state != SS_GAME ) {
		Com_Printf( "TV: no level runs\n" );
		return;
	}
	SV_TVCloseFile( "a new one starts" );
	Q_strncpyz( tvName, Cmd_Argv( 1 ), sizeof( tvName ) );
	Com_sprintf( name, sizeof( name ), "demos/%s.dm_%d", tvName, PROTOCOL_VERSION );
	tvFile = FS_FOpenFileWrite( name );
	if ( !tvFile ) {
		Com_Printf( "TV: %s can not be written\n", name );
		return;
	}
	len = SV_TVGamestate( record, sizeof( record ) );
	FS_Write( record, len, tvFile );
	Com_Printf( "TV: records %s\n", name );
}

static void SV_TVStop_f( void ) {
	if ( !tvFile ) {
		Com_Printf( "TV: it records nothing\n" );
		return;
	}
	SV_TVCloseFile( "tvstop" );
}

// once a frame of the server, after the snapshots of the clients (SV_SendClientMessages)
void SV_TVFrame( void ) {
	static byte			record[MAX_MSGLEN + TV_RECORD_HEAD];
	const playerState_t	*ps;
	int					target, len, sequence;
	qboolean			full;

	if ( sv.state != SS_GAME ) {
		return;
	}
	// nobody watches or records: nothing is built, and the commands are taken as read so none piles up
	if ( !tvFile && !SV_WebTVViewers() ) {
		if ( tvLevel ) {
			tv.reliableAcknowledge = tv.demo.botReliableAcknowledge = tv.reliableSent = tv.reliableSequence;
		}
		tvIdle = qtrue;
		return;
	}
	SV_TVSetUp();
	if ( tvIdle ) {
		tvIdle = qfalse;
		tvFull = qtrue;
	}
	// viewers that came, or whose level changed: the gamestate first (it makes the snapshot after it whole)
	SV_WebTVGreet();
	target = SV_TVPick();
	if ( target >= 0 ) {
		ps = SV_GameClientNum( target );
		SV_TVStandAt( ps );
	} else {
		ps = SV_TVStand();
	}
	full = (qboolean)( tvFull || SV_WebTVWaiting() );
	len = SV_SendTVSnapshot( &tv, ps, (qboolean)( target >= 0 ), full, record + TV_RECORD_HEAD, sizeof( record ) - TV_RECORD_HEAD, &sequence );
	if ( len <= 0 ) {
		return;
	}
	tvFull = qfalse;
	len += SV_TVRecordHead( record, sequence, len );
	if ( tvFile ) {
		FS_Write( record, len, tvFile );
	}
	SV_WebTVSend( record, len, full );
}

#ifndef DEDICATED
// the server of a client has no web host (codemp/server/web): nobody watches there but a file
int SV_WebTVViewers( void ) {
	return 0;
}

qboolean SV_WebTVWaiting( void ) {
	return qfalse;
}

void SV_WebTVSend( const byte *record, int len, qboolean full ) {
}

void SV_WebTVResync( void ) {
}

void SV_WebTVGreet( void ) {
}
#endif

void SV_TVInit( void ) {
	sv_tvSwitch = Cvar_Get( "sv_tvSwitch", "20", CVAR_ARCHIVE, "TV of the server: seconds after which it follows the next player" );
	Cmd_AddCommand( "tvrecord", SV_TVRecord_f, "TV of the server: a demo of a spectator in no slot who follows the players" );
	Cmd_AddCommand( "tvstop", SV_TVStop_f, "TV of the server: ends its demo" );
}
