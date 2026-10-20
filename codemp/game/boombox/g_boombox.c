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

// g_boombox.c -- the boombox of the server: the boxes of a level, what the players do with them, and the songs
// they play, which go out over the relay of the voice chat. Not from JA+, see docs/japlus/boombox.md and
// bg_boombox.h.
//
// A box is no entity. Only clients whose cgame said "vs_box hello" are told of it and hear it, a client of any
// other kind gets nothing, and its players do not see the box and walk through it as everybody does. Every
// player whose cgame draws boxes may do everything with every box: put one down, ask for songs, pause, skip,
// stop, move it, make it louder or quieter for everybody, take it away.
//
// A song plays for everybody at once: the server fetches it, codes 20 ms of it at a time with Opus as they are due
// by the clock (bg_boombox_core.c, bg_boombox_fetch.c, which the masterserver plays the boxes of other servers
// with), and this file sends each frame to every player in range. Nothing in here waits.

#include "game/g_local.h"
#include "game/voice/bg_voice.h"
#include "bg_boombox.h"
#include "bg_boombox_core.h"

char *ConcatArgs( int start );		// g_cmds.c

#define BOX_LOOKUPS			4			// lookups that run at once, for all players together
#define BOX_ASK_MSEC		3000		// a player asks for a song this often at most
#define BOX_FRAME_MSEC		10			// G_BoomboxFrame() works at most this often: it is called with every move of a player
#define BOX_VOLUME			80			// how loud a new box is, of 100
#define BOX_NOTE_RANGE		1.25f		// what went wrong with a box is told to the players this much further away than it is heard

typedef struct boxClient_s {
	qboolean	hears;			// his cgame said "vs_box hello": it draws boxes, and he wants to hear them
	int			askTime;		// when he last asked for a song
	int			watch;			// the box whose line he is sent whenever it changes, plus one. 0: none
	int			watchRev;		//	the lineRev of it he was sent last
} boxClient_t;

static qboolean		boxOn;
static int			boxFrameTime;
static box_t		boxes[BOX_MAX];
static boxPlayer_t	boxPlayers[BOX_MAX];	// one for every box: here no box ever waits for one
static boxLookup_t	boxLookups[BOX_LOOKUPS];
static boxClient_t	boxClients[MAX_CLIENTS];
static boxHost_t	boxHost;

static int Box_Max( void ) {
	return Com_Clampi( 0, BOX_MAX, vs_boomboxMax.integer );
}

static float Box_Range( void ) {
	return (float)( vs_boomboxRange.integer > 0 ? vs_boomboxRange.integer : BOX_RANGE );
}

static int Box_QueueMax( void ) {
	return Com_Clampi( 1, BOX_QUEUE, vs_boomboxQueue.integer );
}

// where the sound of a box comes from
static void Box_Mouth( const box_t *box, vec3_t mouth ) {
	VectorCopy( box->origin, mouth );
	mouth[2] += BOX_SPEAKER_HEIGHT;
}

// is this player one the boxes are for, and is he there
static qboolean Box_Hears( int clientNum ) {
	const gentity_t *ent = &g_entities[clientNum];

	return ( boxClients[clientNum].hears && ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED ) ? qtrue : qfalse;
}

/*
===============================================================================

What the clients are told

===============================================================================
*/

// a text for one client: as it is, or without what his client can not show (v_utf8Text)
static const char *Box_TextFor( int clientNum, const char *text, char *buffer, int bufferSize ) {
	const gclient_t *client = g_entities[clientNum].client;

	if ( !client || client->pers.jp.utf8 ) {
		Q_strncpyz( buffer, text, bufferSize );
	} else {
		Q_UTF8_ToCP1252( text, buffer, bufferSize );
	}
	// (nothing that would end the quotes it is sent in)
	Q_strstrip( buffer, "\"\n\r", "'  " );
	return buffer;
}

// the line of a box to a client who watches it, in as few commands as it fits into
static void Box_LineTo( int b, int clientNum ) {
	const box_t	*box = &boxes[b];
	char		cmd[MAX_STRING_CHARS - 64], title[BOX_TITLE], by[BOX_NAME];
	int			k = 0;

	boxClients[clientNum].watchRev = box->lineRev;
	do {
		Com_sprintf( cmd, sizeof( cmd ), "vs_box list %i %i %i %i", b, box->lineRev, k, box->numSongs );
		for ( ; k < box->numSongs; k++ ) {
			const char *entry;

			Box_TextFor( clientNum, box->songs[k].title, title, sizeof( title ) );
			Box_TextFor( clientNum, box->songs[k].by, by, sizeof( by ) );
			entry = va( " %i \"%s\" \"%s\"", box->songs[k].seconds, title, by );
			if ( strlen( cmd ) + strlen( entry ) >= sizeof( cmd ) )
				break;
			Q_strcat( cmd, sizeof( cmd ), entry );
		}
		trap->SendServerCommand( clientNum, cmd );
	} while ( k < box->numSongs );
}

static void Box_TellTo( int b, int clientNum ) {
	const box_t	*box = &boxes[b];
	char		title[BOX_TITLE], next[BOX_TITLE], owner[BOX_NAME], by[BOX_NAME];
	const int	flags = Box_Flags( box );

	if ( !box->inuse ) {
		trap->SendServerCommand( clientNum, va( "vs_box gone %i", b ) );
		if ( boxClients[clientNum].watch == b + 1 )
			boxClients[clientNum].watch = 0;
		return;
	}
	Box_TextFor( clientNum, box->numSongs ? box->songs[0].title : "", title, sizeof( title ) );
	Box_TextFor( clientNum, box->numSongs > 1 ? box->songs[1].title : "", next, sizeof( next ) );
	Box_TextFor( clientNum, box->owner, owner, sizeof( owner ) );
	Box_TextFor( clientNum, box->numSongs ? box->songs[0].by : "", by, sizeof( by ) );
	// (who put it down and who asked for the song at the end: a cgame of 0.1.7 or 0.1.8 reads no further than next)
	trap->SendServerCommand( clientNum, va( "vs_box set %i %i %i %i %i %i %i %i %i %i %i \"%s\" \"%s\" \"%s\" \"%s\"", b, box->rev,
		(int)box->origin[0], (int)box->origin[1], (int)box->origin[2], (int)box->yaw, box->volume, flags,
		Box_Msec( box ), box->numSongs ? box->songs[0].seconds : 0, Box_Waiting( box ),
		title, next, owner, by ) );
	if ( boxClients[clientNum].watch == b + 1 && boxClients[clientNum].watchRev != box->lineRev )
		Box_LineTo( b, clientNum );
}

// what a box is now, to everybody the boxes are for
static void Box_TellAll( int b ) {
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		if ( Box_Hears( i ) )
			Box_TellTo( b, i );
	}
}

// a box changed here (put down, moved, louder): one up and told
static void Box_Changed( int b ) {
	boxes[b].rev++;
	Box_TellAll( b );
}

// a line in the chat box of one player
static void Box_Note( int clientNum, const char *text ) {
	char line[MAX_STRING_CHARS - 64];

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !g_entities[clientNum].inuse || !g_entities[clientNum].client )
		return;
	if ( Box_Hears( clientNum ) )
		trap->SendServerCommand( clientNum, va( "vs_box note \"%s\"", Box_TextFor( clientNum, text, line, sizeof( line ) ) ) );
	else
		trap->SendServerCommand( clientNum, va( "print \"%s\n\"", Box_TextFor( clientNum, text, line, sizeof( line ) ) ) );
}

// a line for everybody near a box, and for one more who may be anywhere (-1: nobody)
static void Box_NoteNear( int b, int also, const char *text ) {
	vec3_t	mouth;
	int		i;

	Box_Mouth( &boxes[b], mouth );
	for ( i = 0; i < level.maxclients; i++ ) {
		if ( Box_Hears( i ) && ( i == also || Distance( g_entities[i].client->ps.origin, mouth ) <= Box_Range() * BOX_NOTE_RANGE ) )
			Box_Note( i, text );
	}
}

/*
===============================================================================

The host of the core: how the songs of the boxes of this level reach the players

===============================================================================
*/

// the programs, as the owner of the server named them (CVAR_INIT)
static void G_BoxTools( boxTools_t *out ) {
	Q_strncpyz( out->ytdlp, vs_boomboxYtdlp.string, sizeof( out->ytdlp ) );
	Q_strncpyz( out->ffmpeg, vs_boomboxFfmpeg.string, sizeof( out->ffmpeg ) );
	Q_strncpyz( out->ytdlpArgs, vs_boomboxYtdlpArgs.string, sizeof( out->ytdlpArgs ) );
}

static boxPlayer_t *G_BoxBorrow( boxHost_t *host ) {
	int i;

	(void)host;
	for ( i = 0; i < BOX_MAX; i++ ) {
		if ( !boxPlayers[i].inuse )
			return &boxPlayers[i];
	}
	return NULL;
}

static void G_BoxTell( boxHost_t *host, int b ) {
	(void)host;
	Box_TellAll( b );
}

static void G_BoxNote( boxHost_t *host, int b, int client, const char *text ) {
	(void)host;
	if ( b >= 0 )
		Box_NoteNear( b, client, text );
	else
		Box_Note( client, text );
}

// one frame of a song to everybody in range
static void G_BoxSend( boxHost_t *host, int b, const byte *frame, int len ) {
	const box_t	*box = &boxes[b];
	vec3_t		mouth;
	int			i;

	(void)host;
	Box_Mouth( box, mouth );
	for ( i = 0; i < level.maxclients; i++ ) {
		float distance;

		if ( !Box_Hears( i ) || !G_VoiceReady( i ) )
			continue;
		distance = Distance( g_entities[i].client->ps.origin, mouth );
		if ( distance <= Box_Range() )
			G_VoiceSendBox( i, BOX_SPEAKER_BASE + b, box->seq, (int)( distance / VOICE_DISTANCE_STEP ), frame, len );
	}
}

/*
===============================================================================

Once a frame, start and end

===============================================================================
*/

void G_BoomboxFrame( void ) {
	const int	now = trap->Milliseconds();
	int			i;

	if ( !boxOn || ( boxFrameTime && now - boxFrameTime < BOX_FRAME_MSEC ) )
		return;
	boxFrameTime = now;
	boxHost.queueMax = Box_QueueMax();
	boxHost.maxSeconds = vs_boomboxMaxSeconds.integer;
	Box_ReapAll();
	Box_Lookups( boxLookups, BOX_LOOKUPS, now );
	for ( i = 0; i < BOX_MAX; i++ )
		Box_Run( &boxHost, i, now );
}

/*
=================
G_BoomboxShutdown

restart: the level starts again and the module stays, so do the boxes and their songs.
=================
*/
void G_BoomboxShutdown( qboolean restart ) {
	int i;

	if ( restart )
		return;
	for ( i = 0; i < BOX_MAX; i++ )
		Box_StreamStop( &boxPlayers[i].stream );
	for ( i = 0; i < BOX_LOOKUPS; i++ )
		Box_ProcStop( &boxLookups[i].proc );
	Box_ReapAll();
	memset( boxes, 0, sizeof( boxes ) );
	for ( i = 0; i < BOX_MAX; i++ )
		boxPlayers[i].inuse = qfalse;
	memset( boxLookups, 0, sizeof( boxLookups ) );
	memset( boxClients, 0, sizeof( boxClients ) );
	boxOn = qfalse;
	boxFrameTime = 0;
}

// after G_VoiceInit(): the songs go out over the relay of the voice chat, without it there are no boxes
void G_BoomboxInit( qboolean restart ) {
	int i;

	if ( restart && boxOn )
		return;
	G_BoomboxShutdown( qfalse );
	for ( i = 0; i < BOX_MAX; i++ )
		boxPlayers[i].stream.odd = -1;
	memset( &boxHost, 0, sizeof( boxHost ) );
	boxHost.boxes = boxes;
	boxHost.tools = G_BoxTools;
	boxHost.borrow = G_BoxBorrow;
	boxHost.tell = G_BoxTell;
	boxHost.note = G_BoxNote;
	boxHost.send = G_BoxSend;
	if ( !vs_boombox.integer )
		return;
#ifndef JP_BOOMBOX
	trap->Print( "Boombox: off, this game module was built without Opus\n" );
#else
	if ( !G_VoiceOn() ) {
		trap->Print( "Boombox: off, it needs the voice chat (vs_voice 1)\n" );
		return;
	}
	boxOn = qtrue;
#endif
}

// he is gone, or somebody new has his number. What he asked for is still put on.
void G_BoomboxClientGone( int clientNum ) {
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS )
		memset( &boxClients[clientNum], 0, sizeof( boxClients[clientNum] ) );
}

/*
===============================================================================

Commands

===============================================================================
*/

/*
=================
Cmd_JPBox_f

vs_box hello <protocol>: the cgame of a client draws boxes. The answer is "vs_box on" and the state of every box,
and from then on he is told every change and hears the boxes near him. vs_box off: no more of it.
=================
*/
void Cmd_JPBox_f( gentity_t *ent ) {
	const int	clientNum = ent - g_entities;
	char		arg[16];
	int			i;

	trap->Argv( 1, arg, sizeof( arg ) );
	if ( !Q_stricmp( arg, "off" ) ) {
		boxClients[clientNum].hears = qfalse;
		return;
	}
	if ( !boxOn || Q_stricmp( arg, "hello" ) || ( ent->r.svFlags & SVF_BOT ) )
		return;
	boxClients[clientNum].hears = qtrue;
	trap->SendServerCommand( clientNum, va( "vs_box on %i %i %i %i", BOX_PROTOCOL, Box_Max(), (int)Box_Range(), BOX_CAP_MORE ) );
	for ( i = 0; i < BOX_MAX; i++ ) {
		if ( boxes[i].inuse )
			Box_TellTo( i, clientNum );
	}
}

/*
=================
Box_Spot

Where a box that a player wants at `wanted` stands: on the floor under that place, within his reach, and where his
eye sees it. qfalse: nowhere there, why says why.
=================
*/
static qboolean Box_Spot( const gentity_t *ent, const vec3_t wanted, int self, vec3_t spot, char *why, int whySize ) {
	const vec3_t	mins = { -BOX_FOOT, -BOX_FOOT, 0.0f }, maxs = { BOX_FOOT, BOX_FOOT, BOX_FOOT_HEIGHT };
	vec3_t			eye, start, end;
	trace_t			tr;
	int				i;

	VectorCopy( ent->client->ps.origin, eye );
	eye[2] += ent->client->ps.viewheight;
	if ( Distance( eye, wanted ) > BOX_REACH + 32.0f ) {
		Q_strncpyz( why, "that is too far away", whySize );
		return qfalse;
	}
	VectorCopy( wanted, end );
	end[2] += 4.0f;
	trap->Trace( &tr, eye, NULL, NULL, end, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
	if ( tr.fraction < 1.0f && Distance( tr.endpos, end ) > 12.0f ) {
		Q_strncpyz( why, "you can not see that place", whySize );
		return qfalse;
	}
	VectorCopy( wanted, start );
	start[2] += 12.0f;
	VectorCopy( wanted, end );
	end[2] -= BOX_DROP;
	trap->Trace( &tr, start, mins, maxs, end, ENTITYNUM_NONE, MASK_SOLID, qfalse, 0, 0 );
	if ( tr.startsolid || tr.allsolid || tr.fraction >= 1.0f || tr.plane.normal[2] < 0.7f ) {
		Q_strncpyz( why, "there is no floor for it there", whySize );
		return qfalse;
	}
	// on whole units, as the clients are told it, and not into the floor
	spot[0] = floorf( tr.endpos[0] + 0.5f );
	spot[1] = floorf( tr.endpos[1] + 0.5f );
	spot[2] = ceilf( tr.endpos[2] );
	for ( i = 0; i < BOX_MAX; i++ ) {
		if ( i != self && boxes[i].inuse && Distance( boxes[i].origin, spot ) < BOX_APART ) {
			Q_strncpyz( why, "another boombox stands there", whySize );
			return qfalse;
		}
	}
	return qtrue;
}

// a new box, -1: none, the player is told why
static int Box_Place( gentity_t *ent, const vec3_t wanted, float yaw ) {
	const int	clientNum = ent - g_entities;
	vec3_t		spot;
	char		why[64];
	int			i, count = 0;

	for ( i = 0; i < BOX_MAX; i++ ) {
		if ( boxes[i].inuse )
			count++;
	}
	if ( count >= Box_Max() ) {
		Box_Note( clientNum, va( "^5Boombox^7: there are %i boomboxes in this level already, the most there may be", count ) );
		return -1;
	}
	if ( !Box_Spot( ent, wanted, -1, spot, why, sizeof( why ) ) ) {
		Box_Note( clientNum, va( "^5Boombox^7: not put there, %s", why ) );
		return -1;
	}
	for ( i = 0; i < BOX_MAX && boxes[i].inuse; i++ )
		;
	memset( &boxes[i], 0, sizeof( boxes[i] ) );
	boxes[i].inuse = qtrue;
	VectorCopy( spot, boxes[i].origin );
	boxes[i].yaw = AngleNormalize360( yaw );
	boxes[i].volume = BOX_VOLUME;
	Q_strncpyz( boxes[i].owner, ent->client->pers.netname, sizeof( boxes[i].owner ) );
	Box_Changed( i );
	return i;
}

// a box in front of the player, turned to him: for a song he asks for with no box near
static int Box_PlaceInFront( gentity_t *ent ) {
	vec3_t forward, wanted;

	AngleVectors( tv( 0.0f, ent->client->ps.viewangles[YAW], 0.0f ), forward, NULL, NULL );
	VectorMA( ent->client->ps.origin, 48.0f, forward, wanted );
	wanted[2] += 8.0f;
	return Box_Place( ent, wanted, ent->client->ps.viewangles[YAW] + 180.0f );
}

// the box nearest to a player within twice his reach, -1: none
static int Box_Nearest( const gentity_t *ent ) {
	float	best = BOX_REACH * 2.0f;
	int		i, found = -1;

	for ( i = 0; i < BOX_MAX; i++ ) {
		if ( boxes[i].inuse && Distance( boxes[i].origin, ent->client->ps.origin ) < best ) {
			best = Distance( boxes[i].origin, ent->client->ps.origin );
			found = i;
		}
	}
	return found;
}

// what a player asked for is looked up, and put into the line of box b when it is found (-1: the box near him)
static void Box_AskFor( gentity_t *ent, int b, const char *asked ) {
	const int		clientNum = ent - g_entities;
	const int		now = trap->Milliseconds();
	boxClient_t		*bc = &boxClients[clientNum];
	char			why[128], target[BOX_ASK + 32];

	if ( bc->askTime && now - bc->askTime < BOX_ASK_MSEC ) {
		Box_Note( clientNum, "^5Boombox^7: wait a moment before you ask for the next song" );
		return;
	}
	// (said before a box is put down for it)
	if ( !Box_Target( asked, target, sizeof( target ), why, sizeof( why ) ) ) {
		Box_Note( clientNum, va( "^5Boombox^7: %s", why ) );
		return;
	}
	if ( b < 0 )
		b = Box_Nearest( ent );
	if ( b < 0 )
		b = Box_PlaceInFront( ent );
	if ( b < 0 )
		return;
	boxHost.queueMax = Box_QueueMax();
	boxHost.maxSeconds = vs_boomboxMaxSeconds.integer;
	if ( !Box_Ask( &boxHost, boxLookups, BOX_LOOKUPS, b, clientNum, ent->client->pers.netname, asked, now, why, sizeof( why ) ) ) {
		Box_Note( clientNum, va( "^5Boombox^7: %s", why ) );
		return;
	}
	// (that it is looked up is in what the clients are told of the box)
	bc->askTime = now;
}

static float Box_ArgFloat( int n ) {
	char arg[32];

	trap->Argv( n, arg, sizeof( arg ) );
	return (float)atof( arg );
}

static void Box_Usage( int clientNum ) {
	Box_Note( clientNum, "^5Boombox^7: boombox play <link of YouTube or YouTube Music, or what to look for>. "
		"Hold the key of the cursor and click a boombox for the rest, or drag it." );
}

// the console of a player: the boxes and their lines
static void Box_List( int clientNum ) {
	char	line[MAX_STRING_CHARS - 64];
	int		i, k, count = 0;

	for ( i = 0; i < BOX_MAX; i++ ) {
		const box_t *box = &boxes[i];

		if ( !box->inuse )
			continue;
		count++;
		trap->SendServerCommand( clientNum, va( "print \"Boombox %i, volume %i%s%s\n\"", i, box->volume,
			box->paused ? ", paused" : "", box->looking ? ", looking a song up" : "" ) );
		for ( k = 0; k < box->numSongs; k++ ) {
			Box_TextFor( clientNum, box->songs[k].title, line, sizeof( line ) );
			trap->SendServerCommand( clientNum, va( "print \"  %s %s (%s), from %s\n\"", k ? va( "%2i.", k ) : "now", line,
				Box_Length( box->songs[k].seconds ), box->songs[k].by ) );
		}
	}
	if ( !count )
		trap->SendServerCommand( clientNum, "print \"No boombox in this level\n\"" );
}

/*
=================
Cmd_Boombox_f

What a player does with a box, sent by his cgame or typed:

boombox play <box> <link or words>		into the line of that box, -1: the one near him, or a new one in front of him
boombox place <x> <y> <z> <yaw>			a new box at that place
boombox move <box> <x> <y> <z> <yaw>
boombox pause|resume|skip|stop|remove <box>
boombox volume <box> <0 to 100>
boombox seek <box> <second>				the song that plays goes on from there
boombox drop <box> <place>				a song that waits is taken out of the line, 1: the next one
boombox watch <box>						his cgame is sent the line of that box now and whenever it changes, -1: no more
boombox list
=================
*/
void Cmd_Boombox_f( gentity_t *ent ) {
	const int	clientNum = ent - g_entities;
	char		what[16];
	vec3_t		at, spot;
	char		why[64];
	int			b;
	box_t		*box;

	trap->Argv( 1, what, sizeof( what ) );
	if ( !boxOn ) {
		trap->SendServerCommand( clientNum, "print \"This server has no boombox\n\"" );
		return;
	}
	if ( !boxClients[clientNum].hears ) {
		trap->SendServerCommand( clientNum, "print \"The boombox needs VisionJK: only its players see and hear it (or v_boomboxes is 0)\n\"" );
		return;
	}
	if ( !Q_stricmp( what, "list" ) ) {
		Box_List( clientNum );
		return;
	}
	if ( !Q_stricmp( what, "watch" ) ) {
		b = (int)Box_ArgFloat( 2 );
		boxClients[clientNum].watch = ( b >= 0 && b < BOX_MAX && boxes[b].inuse ) ? b + 1 : 0;
		if ( boxClients[clientNum].watch )
			Box_LineTo( b, clientNum );
		return;
	}
	if ( ent->client->pers.jp.silenced ) {
		Box_Note( clientNum, "^5Boombox^7: not while you are silenced" );
		return;
	}
	if ( trap->Argc() < 3 ) {
		Box_Usage( clientNum );
		return;
	}
	if ( !Q_stricmp( what, "place" ) ) {
		Box_Place( ent, tv( Box_ArgFloat( 2 ), Box_ArgFloat( 3 ), Box_ArgFloat( 4 ) ), Box_ArgFloat( 5 ) );
		return;
	}
	b = (int)Box_ArgFloat( 2 );
	if ( !Q_stricmp( what, "play" ) ) {
		if ( b >= BOX_MAX || ( b >= 0 && !boxes[b].inuse ) )
			b = -1;
		Box_AskFor( ent, b, ConcatArgs( 3 ) );
		return;
	}
	if ( b < 0 || b >= BOX_MAX || !boxes[b].inuse ) {
		Box_Note( clientNum, "^5Boombox^7: there is no such boombox" );
		return;
	}
	box = &boxes[b];
	if ( !Q_stricmp( what, "pause" ) || !Q_stricmp( what, "resume" ) ) {
		Box_Pause( &boxHost, b, (qboolean)!Q_stricmp( what, "pause" ), trap->Milliseconds() );
	} else if ( !Q_stricmp( what, "skip" ) ) {
		if ( box->numSongs && Box_Touch( box, trap->Milliseconds() ) )
			Box_Next( &boxHost, b );
	} else if ( !Q_stricmp( what, "stop" ) ) {
		if ( box->numSongs )
			Box_Stop( &boxHost, b );
	} else if ( !Q_stricmp( what, "remove" ) ) {
		Box_Remove( &boxHost, b );
	} else if ( !Q_stricmp( what, "volume" ) ) {
		box->volume = Com_Clampi( 0, 100, (int)Box_ArgFloat( 3 ) );
		Box_Changed( b );
	} else if ( !Q_stricmp( what, "seek" ) ) {
		if ( Box_Touch( box, trap->Milliseconds() ) )
			Box_Seek( &boxHost, b, (int)Box_ArgFloat( 3 ), trap->Milliseconds() );
	} else if ( !Q_stricmp( what, "drop" ) ) {
		Box_Drop( &boxHost, b, (int)Box_ArgFloat( 3 ) );
	} else if ( !Q_stricmp( what, "move" ) ) {
		VectorSet( at, Box_ArgFloat( 3 ), Box_ArgFloat( 4 ), Box_ArgFloat( 5 ) );
		if ( !Box_Spot( ent, at, b, spot, why, sizeof( why ) ) ) {
			Box_Note( clientNum, va( "^5Boombox^7: not moved there, %s", why ) );
			// (his cgame shows it where it was again)
			Box_TellTo( b, clientNum );
			return;
		}
		VectorCopy( spot, box->origin );
		box->yaw = AngleNormalize360( Box_ArgFloat( 6 ) );
		Box_Changed( b );
	} else {
		Box_Usage( clientNum );
	}
}

// the server command vs_boxes: the boxes and their lines, "vs_boxes clear" takes them all away
void Svcmd_Boombox_f( void ) {
	char	arg[16];
	int		i, k;

	trap->Argv( 1, arg, sizeof( arg ) );
	if ( !boxOn ) {
		trap->Print( "The boombox is off (vs_boombox %i, vs_voice %i)\n", vs_boombox.integer, vs_voice.integer );
		return;
	}
	if ( !Q_stricmp( arg, "clear" ) ) {
		for ( i = 0; i < BOX_MAX; i++ ) {
			if ( boxes[i].inuse )
				Box_Remove( &boxHost, i );
		}
		trap->Print( "All boomboxes are gone\n" );
		return;
	}
	for ( i = 0; i < BOX_MAX; i++ ) {
		const box_t *box = &boxes[i];

		if ( !box->inuse )
			continue;
		trap->Print( "Boombox %i at %i %i %i, volume %i%s%s%s\n", i, (int)box->origin[0], (int)box->origin[1], (int)box->origin[2],
			box->volume, box->paused ? ", paused" : "", box->streaming && !box->started ? ", fetching" : "",
			box->looking ? ", looking a song up" : "" );
		for ( k = 0; k < box->numSongs; k++ ) {
			trap->Print( "  %s %s %s (%s), from %s\n", k ? va( "%2i.", k ) : "now", box->songs[k].id, box->songs[k].title,
				Box_Length( box->songs[k].seconds ), box->songs[k].by );
		}
		if ( box->player && box->player->stream.proc.errLine[0] )
			trap->Print( "  last word of yt-dlp or ffmpeg: %s\n", box->player->stream.proc.errLine );
	}
	for ( i = k = 0; i < MAX_CLIENTS; i++ ) {
		if ( Box_Hears( i ) )
			k++;
	}
	trap->Print( "%i players see the boomboxes\n", k );
}
