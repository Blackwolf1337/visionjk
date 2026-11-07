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

// sv_ids.cpp -- the intrusion detection of the server. Not from OpenJK.
//
// The bounds checks of the engine and of the game keep a bad packet from doing harm: it is thrown away. This file
// is told about each of them (SV_IDS_Event), so that somebody who keeps sending them is noticed, written down
// and after a while no longer listened to. It finds what looks like the known attacks and what no client of the
// game sends. It does not find a hole nobody knows: that stays the job of the checks themselves.
//
// - Every event has a weight. The weights add up for the address they come from and fall off again, two a
//   second. At 100 the address is blocked: its connectionless packets (getinfo, getstatus, connect, rcon) are
//   dropped unread for a minute, and for twice as long each time it happens again, up to an hour. An event that
//   comes from a player in the game drops that player as well.
// - Every event is a line in ids.log of the game folder and on the console, at most one in two seconds for an
//   address and an event (the rest is counted and named with the next line), and at most thirty a second
//   overall: a flood must not fill the disk either.
// - vs_ids 0: nothing of it. 1: events are written down, nobody is blocked or dropped. 2 (default): all of it.
//
// What the address of a packet says can be made up, a connectionless packet needs no answer to be taken. So an
// attacker can make it look as if a player sent junk. That is why a block only ever stops connectionless
// packets and never the game of somebody who is on the server, and why connectionless events do not count at
// all for an address that has a player on the server: at worst the attacker keeps a stranger from connecting
// for a while, which he could do with a flood anyway.
//
// No speed check is here, though one was (2026-10-11): it counted the moves of a player that are ahead of the time
// of the server, which is what a clock that runs fast would give. A client whose clock runs three times too fast
// was tried against it and never got there: the client keeps the time of its moves at the time the server tells
// it, a few frames ahead at most, and the game cuts a move back to 200 msec ahead anyway (ClientThink_real). So
// the hack gains nothing in this game and there is nothing to catch. What such a count does catch is every
// player after the server stood still for a moment: their moves of that moment are all ahead of its time.
// vs_antiWallhack is in sv_snapshot.cpp.

#include "server.h"

#define IDS_LOG				"ids.log"
#define IDS_ENTRIES			256			// a power of two
#define IDS_PROBE			8
#define IDS_BLOCK_SCORE		100
#define IDS_DECAY_MSEC		500			// a point falls off in that time
#define IDS_BLOCK_MSEC		60000
#define IDS_BLOCK_MAX_MSEC	3600000
#define IDS_FORGET_MSEC		3600000		// an address that did nothing for that long is forgotten, its blocks too
#define IDS_LINE_MSEC		2000
#define IDS_LINES_SECOND	30
#define IDS_DETAIL			96

typedef struct idsEntry_s {
	byte		ip[4];
	qboolean	inUse;
	int			score;
	int			scoreTime;			// when the score was last brought up to date
	int			seenTime;
	int			blockedUntil;
	int			blocks;				// how often it was blocked
	int			dropped;			// packets that were not read while it was
	int			lineTime;			// the last line that was written for it,
	int			lineEvent;			//	for which event,
	int			lineSkipped;		//	and how many of that event were not written since
} idsEntry_t;

static const struct {
	const char	*name;
	int			score;
} ids_events[IDS_NUM_EVENTS] = {
	{ "an unknown connectionless packet",				5 },	// IDS_OOB_UNKNOWN
	{ "an oversized connectionless packet",				5 },	// IDS_OOB_SIZE
	{ "rcon, which this server does not take",			10 },	// IDS_RCON
	{ "connectionless packets past the rate limit",		1 },	// IDS_OOB_FLOOD
	{ "a connect with a wrong challenge",				3 },	// IDS_BAD_CHALLENGE
	{ "an illegible message",							25 },	// IDS_BAD_MESSAGE
	{ "a number of moves that is out of range",			25 },	// IDS_BAD_COUNT
	{ "an acknowledgement that is out of range",		25 },	// IDS_BAD_ACK
	{ "reliable commands that are missing",				10 },	// IDS_LOST_COMMANDS
	{ "a fragment that does not fit its message",		25 },	// IDS_FRAGMENT
	{ "a selection in a move that is out of range",		5 },	// IDS_USERCMD
	{ "commands past the flood protection",				1 },	// IDS_CMD_FLOOD
	{ "a download it may not have",						10 },	// IDS_DOWNLOAD
	{ "a download path that leaves the game",			50 },	// IDS_DOWNLOAD_PATH
	{ "a line break inside a command",					30 },	// IDS_TEXT_BREAK
	{ "a path that goes up, in a command",				20 },	// IDS_TEXT_PATH
	{ "a run of bytes that is no text",					35 },	// IDS_TEXT_BINARY
	{ "a userinfo that is too long",					25 },	// IDS_USERINFO
};

static cvar_t		*vs_ids;
static idsEntry_t	ids_entries[IDS_ENTRIES];
static fileHandle_t	ids_file;
static int			ids_lineSecond, ids_lines, ids_linesLost;

/*
=================
SV_IDS_Find

The entry of an address, a new one if create says so. Only addresses of the internet protocol have one: the
machine itself, bots and the players of the web host are never blocked.
=================
*/
static idsEntry_t *SV_IDS_Find( const netadr_t *from, qboolean create, int now ) {
	const unsigned int	hash = ( ( from->ip[0] * 31u + from->ip[1] ) * 31u + from->ip[2] ) * 31u + from->ip[3];
	idsEntry_t			*unused = NULL, *stale = NULL, *e;
	int					i;

	if ( from->type != NA_IP ) {
		return NULL;
	}
	for ( i = 0; i < IDS_PROBE; i++ ) {
		e = &ids_entries[( hash + i ) & ( IDS_ENTRIES - 1 )];
		if ( e->inUse && !memcmp( e->ip, from->ip, 4 ) ) {
			return e;
		}
		if ( e->inUse && now - e->seenTime > IDS_FORGET_MSEC && now >= e->blockedUntil ) {
			e->inUse = qfalse;
		}
		if ( !e->inUse ) {
			if ( !unused ) {
				unused = e;
			}
		} else if ( now >= e->blockedUntil && ( !stale || e->seenTime < stale->seenTime ) ) {
			stale = e;		// nothing was heard of it for the longest, and it is not blocked
		}
	}
	e = unused ? unused : stale;
	if ( !create || !e ) {
		return NULL;
	}
	memset( e, 0, sizeof( *e ) );
	memcpy( e->ip, from->ip, 4 );
	e->inUse = qtrue;
	e->scoreTime = e->seenTime = now;
	e->lineEvent = -1;
	return e;
}

// a line into ids.log and onto the console
static void SV_IDS_Write( const char *line ) {
	Com_IDSLine( &ids_file, IDS_LOG, line );
}

// qtrue: there is room for another line in this second
static qboolean SV_IDS_LineRoom( int now ) {
	if ( now / 1000 != ids_lineSecond ) {
		if ( ids_linesLost ) {
			const int lost = ids_linesLost;

			ids_linesLost = 0;
			ids_lineSecond = now / 1000;
			ids_lines = 1;
			SV_IDS_Write( va( "%i more events were not written down, there were too many in a second", lost ) );
		}
		ids_lineSecond = now / 1000;
		ids_lines = 0;
	}
	if ( ids_lines >= IDS_LINES_SECOND ) {
		ids_linesLost++;
		return qfalse;
	}
	ids_lines++;
	return qtrue;
}

static qboolean SV_IDS_HasClient( const netadr_t *from ) {
	client_t	*cl;
	int			i;

	if ( !svs.clients ) {
		return qfalse;
	}
	for ( i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++ ) {
		if ( cl->state >= CS_CONNECTED && NET_CompareBaseAdr( from, &cl->netchan.remoteAddress ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
=================
SV_IDS_Event

Something arrived that no client of the game sends. cl: the player it came from, NULL for a connectionless
packet. detail: what it was, NULL for nothing to add.
=================
*/
void SV_IDS_Event( const netadr_t *from, client_t *cl, idsEvent_t event, const char *detail ) {
	const int	now = Sys_Milliseconds();
	idsEntry_t	*e;
	qboolean	counts = qtrue;

	if ( !vs_ids || !vs_ids->integer || event < 0 || event >= IDS_NUM_EVENTS ) {
		return;
	}
	e = SV_IDS_Find( from, qtrue, now );
	if ( !e ) {
		return;
	}
	e->seenTime = now;

	// the score as it is now, then what this adds. A connectionless packet may carry an address that is not its
	// sender's: it does not count against somebody who is in the game.
	if ( now - e->scoreTime >= IDS_DECAY_MSEC ) {
		e->score = Q_max( 0, e->score - ( now - e->scoreTime ) / IDS_DECAY_MSEC );
		e->scoreTime = now - ( now - e->scoreTime ) % IDS_DECAY_MSEC;
	}
	if ( !cl && SV_IDS_HasClient( from ) ) {
		counts = qfalse;
	} else {
		e->score += ids_events[event].score;
	}

	if ( e->lineEvent == event && now - e->lineTime < IDS_LINE_MSEC && now >= e->lineTime ) {
		e->lineSkipped++;
	} else if ( SV_IDS_LineRoom( now ) ) {
		char line[512];

		Com_sprintf( line, sizeof( line ), "%s%s%s sent %s", NET_AdrToString( from ), cl ? " " : "", cl ? Com_IDSDetail( cl->name, IDS_DETAIL ) : "",
			ids_events[event].name );
		if ( detail && detail[0] ) {
			Q_strcat( line, sizeof( line ), va( ": %s", Com_IDSDetail( detail, IDS_DETAIL ) ) );
		}
		if ( e->lineSkipped && e->lineEvent == event ) {
			Q_strcat( line, sizeof( line ), va( " (and %i more of it)", e->lineSkipped ) );
		}
		if ( !counts ) {
			Q_strcat( line, sizeof( line ), " (does not count: the address has a player on the server, and such a packet can name any address)" );
		}
		SV_IDS_Write( line );
		e->lineEvent = event;
		e->lineTime = now;
		e->lineSkipped = 0;
	}

	if ( e->score < IDS_BLOCK_SCORE || vs_ids->integer < 2 ) {
		return;
	}

	// enough: the address is not listened to for a while, and a player it came from leaves
	{
		int msec = IDS_BLOCK_MSEC;

		if ( e->blocks < 8 ) {
			msec <<= e->blocks;
		}
		if ( e->blocks >= 8 || msec > IDS_BLOCK_MAX_MSEC ) {
			msec = IDS_BLOCK_MAX_MSEC;
		}
		e->blocks++;
		e->score = 0;
		e->scoreTime = now;
		e->blockedUntil = now + msec;
		e->dropped = 0;
		SV_IDS_Write( va( "%s is blocked for %i seconds (%i. time): its connectionless packets are not read%s", NET_AdrToString( from ), msec / 1000,
			e->blocks, ( cl && cl->state >= CS_CONNECTED ) ? ", and the player is dropped" : "" ) );
	}
	if ( cl && cl->state >= CS_CONNECTED ) {
		SV_DropClient( cl, "was dropped by the intrusion detection of the server" );
	}
}

/*
=================
SV_IDS_Blocked

At the top of the connectionless packets: qtrue, this one is not read
=================
*/
qboolean SV_IDS_Blocked( const netadr_t *from ) {
	const int	now = Sys_Milliseconds();
	idsEntry_t	*e;

	if ( !vs_ids || vs_ids->integer < 2 ) {
		return qfalse;
	}
	e = SV_IDS_Find( from, qfalse, now );
	if ( !e || now >= e->blockedUntil ) {
		return qfalse;
	}
	e->dropped++;
	e->seenTime = now;
	return qtrue;
}

/*
=================
SV_IDS_Text

A command of a player, or his userinfo: looks for what the known attacks on games of this engine look like.
None of it does anything here (the game takes line breaks out of a command and checks every path), but nobody
types it by accident. Format marks (%s, %n) are not among them: the engine turns every % into a dot when it
reads a string from a message (MSG_ReadString), so none ever gets this far.
=================
*/
// Bytes above 127 are letters with accents from an old client and UTF-8 from one of this tree. What is neither:
// 48 of them in a row of which most begin no character of UTF-8. That is the padding in front of code, or code.
static qboolean SV_IDS_BinaryRun( const unsigned char *run, int length ) {
	int at = 0, bad = 0;

	if ( length < 48 ) {
		return qfalse;
	}
	while ( at < length ) {
		unsigned int	codePoint;
		// the lenient reader takes a byte that begins nothing as one character of Windows-1252
		const int		used = Q_UTF8_Decode( (const char *)run + at, &codePoint );

		if ( used <= 1 || at + used > length ) {
			bad++;
			at++;
		} else {
			at += used;
		}
	}
	return (qboolean)( bad > length / 2 );
}

void SV_IDS_Text( client_t *cl, const char *text, qboolean userinfo ) {
	const unsigned char	*s = (const unsigned char *)text;
	const netadr_t		*from = &cl->netchan.remoteAddress;
	int					binary = 0, i;
	qboolean			chat, lineBreak = qfalse, pathUp = qfalse, binaryRun = qfalse;

	if ( !vs_ids || !vs_ids->integer || from->type != NA_IP ) {
		return;
	}
	// what people write to each other may have dots in it
	chat = (qboolean)( !userinfo && ( !Q_stricmpn( text, "say", 3 ) || !Q_stricmpn( text, "tell", 4 ) || !Q_stricmpn( text, "amsay", 5 )
		|| !Q_stricmpn( text, "ampsay", 6 ) || !Q_stricmpn( text, "clansay", 7 ) || !Q_stricmpn( text, "vsay", 4 ) || !Q_stricmpn( text, "vs_status", 8 ) ) );

	for ( i = 0; s[i]; i++ ) {
		if ( s[i] == '\n' || s[i] == '\r' ) {
			lineBreak = qtrue;
		}
		if ( !chat && s[i] == '.' && s[i + 1] == '.' && ( s[i + 2] == '/' || s[i + 2] == '\\' ) ) {
			pathUp = qtrue;
		}
		if ( s[i] >= 0x80 ) {
			binary++;
		} else {
			if ( SV_IDS_BinaryRun( s + i - binary, binary ) ) {
				binaryRun = qtrue;
			}
			binary = 0;
		}
	}
	if ( SV_IDS_BinaryRun( s + i - binary, binary ) ) {
		binaryRun = qtrue;
	}

	if ( lineBreak ) {
		SV_IDS_Event( from, cl, IDS_TEXT_BREAK, text );
	}
	if ( pathUp && cl->state >= CS_CONNECTED ) {
		SV_IDS_Event( from, cl, IDS_TEXT_PATH, text );
	}
	if ( binaryRun && cl->state >= CS_CONNECTED ) {
		SV_IDS_Event( from, cl, IDS_TEXT_BINARY, text );
	}
}

/*
=================
SV_IDS_Status_f / SV_IDS_Unblock_f
=================
*/
static void SV_IDS_Status_f( void ) {
	const int	now = Sys_Milliseconds();
	int			i, shown = 0;

	Com_Printf( "vs_ids %i (0 off, 1 events are written down, 2 addresses are blocked and players dropped as well)\n", vs_ids->integer );
	for ( i = 0; i < IDS_ENTRIES; i++ ) {
		const idsEntry_t *e = &ids_entries[i];

		if ( !e->inUse || ( !e->blocks && now - e->seenTime > 600000 ) ) {
			continue;
		}
		Com_Printf( "  %3i.%3i.%3i.%3i  score %3i  blocked %i times", e->ip[0], e->ip[1], e->ip[2], e->ip[3],
			Q_max( 0, e->score - ( now - e->scoreTime ) / IDS_DECAY_MSEC ), e->blocks );
		if ( now < e->blockedUntil ) {
			Com_Printf( ", for another %i seconds, %i packets not read", ( e->blockedUntil - now ) / 1000, e->dropped );
		}
		Com_Printf( "\n" );
		shown++;
	}
	if ( !shown ) {
		Com_Printf( "  nothing of note in the last ten minutes\n" );
	}
	Com_Printf( "vs_idsunblock <address|all> lifts a block. The events are in " IDS_LOG " of the game folder.\n" );
}

static void SV_IDS_Unblock_f( void ) {
	const char	*arg = Cmd_Argv( 1 );
	const qboolean all = (qboolean)!Q_stricmp( arg, "all" );
	netadr_t	adr;
	int			i, count = 0;

	if ( !arg[0] || ( !all && ( !NET_StringToAdr( arg, &adr ) || adr.type != NA_IP ) ) ) {
		Com_Printf( "usage: vs_idsunblock <address|all>\n" );
		return;
	}
	for ( i = 0; i < IDS_ENTRIES; i++ ) {
		idsEntry_t *e = &ids_entries[i];

		if ( e->inUse && ( all || !memcmp( e->ip, adr.ip, 4 ) ) && ( e->blockedUntil || e->blocks || e->score ) ) {
			e->blockedUntil = e->blocks = e->score = 0;
			count++;
		}
	}
	Com_Printf( "%i addresses start from nothing again\n", count );
}

void SV_IDS_Init( void ) {
	vs_ids = Cvar_Get( "vs_ids", "2", CVAR_ARCHIVE, "Intrusion detection: 0 off, 1 what looks like an attack is written to ids.log, 2 its address is blocked for a while as well" );
	Cmd_AddCommand( "vs_idsstatus", SV_IDS_Status_f, "The addresses the intrusion detection has noted" );
	Cmd_AddCommand( "vs_idsunblock", SV_IDS_Unblock_f, "Lifts the block of an address, or of all" );
}

void SV_IDS_Shutdown( void ) {
	if ( ids_file ) {
		FS_FCloseFile( ids_file );
		ids_file = 0;
	}
}
