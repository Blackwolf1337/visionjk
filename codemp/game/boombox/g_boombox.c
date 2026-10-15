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
// A box is no entity. Only clients whose cgame said "jpbox hello" are told of it and hear it, a client of any
// other kind gets nothing, and its players do not see the box and walk through it as everybody does. Every
// player whose cgame draws boxes may do everything with every box: put one down, ask for songs, pause, skip,
// stop, move it, make it louder or quieter for everybody, take it away.
//
// A song plays for everybody at once: the server fetches it (g_boombox_fetch.c), codes 20 ms of it at a time
// with Opus as they are due by the clock, and sends each frame to every player in range. Nothing in here waits.

#include "game/g_local.h"
#include "game/voice/bg_voice.h"
#include "bg_boombox.h"
#include "g_boombox.h"

#ifdef JP_BOOMBOX
	#include <opus.h>
#endif

char *ConcatArgs( int start );		// g_cmds.c

#define BOX_LOOKUPS			4			// lookups that run at once, for all players together
#define BOX_ASK_MSEC		3000		// a player asks for a song this often at most
#define BOX_START_SAMPLES	( BOX_RATE / 2 )	// what has to be there of a song before it starts: half a second
#define BOX_FETCH_MSEC		20000		// a song of which nothing came for this long is skipped
#define BOX_RESTARTS		3			// times a fetch that broke off is begun again further in
#define BOX_RESTART_SLACK	( BOX_RATE * 3 )	// a fetch that ends this close to the length of the song ended
#define BOX_BURST			8			// frames that go out at once at most, after the server stood still
#define BOX_FRAME_MSEC		10			// G_BoomboxFrame() works at most this often: it is called with every move of a player
#define BOX_BITRATE			64000		// what a song is coded with: Opus, one channel
#define BOX_ENCODER_BYTES	40960		// room for the state of the encoder of one box. What it needs is asked and checked
#define BOX_VOLUME			80			// how loud a new box is, of 100
#define BOX_APART			24.0f		// two boxes stand at least this far apart
#define BOX_NOTE_RANGE		1.25f		// what happens to a box is told to the players this much further away than it is heard

typedef struct box_s {
	qboolean	inuse;
	int			rev;			// one up with every change the clients are told
	vec3_t		origin;			// on the floor
	float		yaw;
	int			volume;			// for everybody, 0 to 100
	boxSong_t	songs[BOX_QUEUE];	// the line: songs[0] plays
	int			numSongs;
	int			looking;		// songs for it that are looked up
	qboolean	paused;
	// the song that plays
	qboolean	streaming;		// its programs were started
	qboolean	started;		// enough of it came: its frames go out
	int			startMsec;		// trap->Milliseconds() when its first frame would have gone out, if it had never waited
	int			frames;			// frames of it that went out
	int			waitSince;		// when its fetch began
	int			restarts;		// times its fetch broke off and was begun again further in
	int			seq;			// of the datagrams of this box, one up with every frame
	qboolean	encoderOk;
} box_t;

typedef struct boxClient_s {
	qboolean	hears;			// his cgame said "jpbox hello": it draws boxes, and he wants to hear them
	int			askTime;		// when he last asked for a song
} boxClient_t;

// a block of memory for the state of an encoder, as well aligned as anything can ask for
typedef union boxEncoder_u {
	double	align;
	void	*alignToo;
	byte	bytes[BOX_ENCODER_BYTES];
} boxEncoder_t;

static qboolean		boxOn;
static int			boxFrameTime;
static box_t		boxes[BOX_MAX];
static boxStream_t	boxStreams[BOX_MAX];
static boxEncoder_t	boxEncoders[BOX_MAX];
static boxLookup_t	boxLookups[BOX_LOOKUPS];
static boxClient_t	boxClients[MAX_CLIENTS];

static int Box_Max( void ) {
	return Com_Clampi( 0, BOX_MAX, g_boomboxMax.integer );
}

static float Box_Range( void ) {
	return (float)( g_boomboxRange.integer > 0 ? g_boomboxRange.integer : BOX_RANGE );
}

static int Box_QueueMax( void ) {
	return Com_Clampi( 1, BOX_QUEUE, g_boomboxQueue.integer );
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

// a text for one client: as it is, or without what his client can not show (cl_utf8Text)
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

static void Box_TellTo( int b, int clientNum ) {
	const box_t	*box = &boxes[b];
	char		title[BOX_TITLE], next[BOX_TITLE];
	int			flags = 0;

	if ( !box->inuse ) {
		trap->SendServerCommand( clientNum, va( "jpbox gone %i", b ) );
		return;
	}
	if ( box->numSongs ) {
		flags |= BOX_PLAYING;
		if ( box->paused )
			flags |= BOX_PAUSED;
		if ( !box->started )
			flags |= BOX_LOADING;
	}
	if ( box->looking )
		flags |= BOX_LOOKING;
	Box_TextFor( clientNum, box->numSongs ? box->songs[0].title : "", title, sizeof( title ) );
	Box_TextFor( clientNum, box->numSongs > 1 ? box->songs[1].title : "", next, sizeof( next ) );
	trap->SendServerCommand( clientNum, va( "jpbox set %i %i %i %i %i %i %i %i %i %i %i \"%s\" \"%s\"", b, box->rev,
		(int)box->origin[0], (int)box->origin[1], (int)box->origin[2], (int)box->yaw, box->volume, flags,
		box->frames * VOICE_FRAME_MSEC, box->numSongs ? box->songs[0].seconds : 0, box->numSongs > 1 ? box->numSongs - 1 : 0,
		title, next ) );
}

// what a box is now, to everybody the boxes are for
static void Box_Tell( int b ) {
	int i;

	boxes[b].rev++;
	for ( i = 0; i < level.maxclients; i++ ) {
		if ( Box_Hears( i ) )
			Box_TellTo( b, i );
	}
}

// a line in the chat box of one player
static void Box_Note( int clientNum, const char *text ) {
	char line[MAX_STRING_CHARS - 64];

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !g_entities[clientNum].inuse || !g_entities[clientNum].client )
		return;
	if ( Box_Hears( clientNum ) )
		trap->SendServerCommand( clientNum, va( "jpbox note \"%s\"", Box_TextFor( clientNum, text, line, sizeof( line ) ) ) );
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

static const char *Box_Length( int seconds ) {
	return va( "%i:%02i", seconds / 60, seconds % 60 );
}

/*
===============================================================================

The songs of a box

===============================================================================
*/

// the song that played is through, or was skipped: the next one in the line, if there is one
static void Box_Next( int b ) {
	box_t *box = &boxes[b];

	Box_StreamStop( &boxStreams[b] );
	if ( box->numSongs > 0 ) {
		memmove( &box->songs[0], &box->songs[1], ( box->numSongs - 1 ) * sizeof( box->songs[0] ) );
		box->numSongs--;
	}
	box->streaming = box->started = box->paused = qfalse;
	box->frames = box->restarts = 0;
	Box_Tell( b );
}

static void Box_Stop( int b ) {
	boxes[b].numSongs = 0;
	Box_Next( b );
}

// the encoder of a box, the first time it is needed
static qboolean Box_Encoder( int b ) {
#ifdef JP_BOOMBOX
	OpusEncoder *enc = (OpusEncoder *)&boxEncoders[b];

	if ( boxes[b].encoderOk )
		return qtrue;
	if ( opus_encoder_get_size( 1 ) > (int)sizeof( boxEncoders[b] ) || opus_encoder_init( enc, BOX_RATE, 1, OPUS_APPLICATION_AUDIO ) != OPUS_OK )
		return qfalse;
	opus_encoder_ctl( enc, OPUS_SET_BITRATE( BOX_BITRATE ) );
	opus_encoder_ctl( enc, OPUS_SET_SIGNAL( OPUS_SIGNAL_MUSIC ) );
	// every frame carries a small copy of the one before it, so one lost datagram can be made good
	opus_encoder_ctl( enc, OPUS_SET_INBAND_FEC( 1 ) );
	opus_encoder_ctl( enc, OPUS_SET_PACKET_LOSS_PERC( 5 ) );
	boxes[b].encoderOk = qtrue;
	return qtrue;
#else
	(void)b;
	return qfalse;
#endif
}

// the programs of the first song in the line start
static void Box_Begin( int b, int now ) {
	box_t	*box = &boxes[b];
	char	why[128];

	if ( !Box_Encoder( b ) ) {
		Box_NoteNear( b, -1, "^5Boombox^7: the server can not code sound" );
		Box_Stop( b );
		return;
	}
	if ( !Box_StreamStart( &boxStreams[b], box->songs[0].id, 0, qfalse, why, sizeof( why ) ) ) {
		Box_NoteNear( b, -1, va( "^5Boombox^7: %s is not played, %s", box->songs[0].title, why ) );
		Box_Next( b );
		return;
	}
	box->streaming = qtrue;
	box->started = qfalse;
	box->frames = box->restarts = 0;
	box->waitSince = now;
	Box_Tell( b );
}

// one frame of a song to everybody in range
static void Box_Send( int b, const short *samples ) {
#ifdef JP_BOOMBOX
	box_t		*box = &boxes[b];
	byte		frame[VOICE_MAX_FRAME];
	const int	len = opus_encode( (OpusEncoder *)&boxEncoders[b], samples, VOICE_FRAME_SAMPLES, frame, sizeof( frame ) );
	vec3_t		mouth;
	int			i;

	box->seq = ( box->seq + 1 ) & 0xffff;
	box->frames++;
	if ( len <= 0 )
		return;
	Box_Mouth( box, mouth );
	for ( i = 0; i < level.maxclients; i++ ) {
		float distance;

		if ( !Box_Hears( i ) || !G_VoiceReady( i ) )
			continue;
		distance = Distance( g_entities[i].client->ps.origin, mouth );
		if ( distance <= Box_Range() )
			G_VoiceSendBox( i, BOX_SPEAKER_BASE + b, box->seq, (int)( distance / VOICE_DISTANCE_STEP ), frame, len );
	}
#else
	(void)b;
	(void)samples;
#endif
}

/*
=================
Box_Run

Once a frame for a box with songs: its programs are started, what they made is taken, and the frames that are
due by the clock go out. A song that is fetched too slowly waits, and everybody hears it wait. One whose fetch
broke off in the middle is fetched again from where it was.
=================
*/
static void Box_Run( int b, int now ) {
	box_t		*box = &boxes[b];
	boxStream_t	*s = &boxStreams[b];
	short		samples[VOICE_FRAME_SAMPLES];
	char		why[128];
	int			due;

	if ( !box->streaming ) {
		Box_Begin( b, now );
		return;
	}
	Box_StreamFill( s );
	if ( s->ended && box->restarts < BOX_RESTARTS && s->fetched < box->songs[0].seconds * BOX_RATE - BOX_RESTART_SLACK
		&& ( box->started || s->fetched > 0 ) ) {
		box->restarts++;
		Box_StreamStart( s, box->songs[0].id, s->fetched, qtrue, why, sizeof( why ) );
	}

	if ( !box->started ) {
		if ( Box_StreamQueued( s ) >= BOX_START_SAMPLES || ( s->ended && Box_StreamQueued( s ) > 0 ) ) {
			box->started = qtrue;
			box->startMsec = now;
			box->frames = 0;
			Box_Tell( b );
		} else if ( s->ended || now - box->waitSince > BOX_FETCH_MSEC ) {
			Box_StreamWhy( s, "nothing of it came", why, sizeof( why ) );
			Box_NoteNear( b, -1, va( "^5Boombox^7: %s is not played, %s", box->songs[0].title, why ) );
			Box_Next( b );
		}
		return;
	}
	if ( box->paused )
		return;

	due = ( now - box->startMsec ) / VOICE_FRAME_MSEC - box->frames;
	if ( due > BOX_BURST ) {
		box->startMsec += ( due - BOX_BURST ) * VOICE_FRAME_MSEC;
		due = BOX_BURST;
	}
	for ( ; due > 0; due-- ) {
		const int queued = Box_StreamQueued( s );

		if ( queued < VOICE_FRAME_SAMPLES && !s->ended ) {
			// not fetched yet: the song waits for it
			box->startMsec += due * VOICE_FRAME_MSEC;
			return;
		}
		if ( !queued ) {
			Box_Next( b );
			return;
		}
		memset( samples, 0, sizeof( samples ) );
		Box_StreamTake( s, samples, VOICE_FRAME_SAMPLES );
		Box_Send( b, samples );
	}
}

/*
=================
Box_LookupDone

A lookup ended: the song goes into the line of its box, or the player who asked is told why not.
=================
*/
static void Box_LookupDone( boxLookup_t *l ) {
	box_t		*box = &boxes[l->box];
	boxSong_t	song;
	char		why[128];
	const int	result = Box_LookupFrame( l, &song, why, sizeof( why ) );
	const int	most = g_boomboxMaxSeconds.integer;

	if ( result == 0 )
		return;
	if ( box->looking > 0 )
		box->looking--;
	if ( result < 0 ) {
		Box_Note( l->client, va( "^5Boombox^7: not played, %s", why ) );
	} else if ( !box->inuse ) {
		Box_Note( l->client, va( "^5Boombox^7: %s is not played, the boombox is gone", song.title ) );
	} else if ( most > 0 && song.seconds > most ) {
		Box_Note( l->client, va( "^5Boombox^7: %s is too long (%s, at most %s)", song.title, Box_Length( song.seconds ), Box_Length( most ) ) );
	} else if ( box->numSongs >= Box_QueueMax() ) {
		Box_Note( l->client, va( "^5Boombox^7: %s is not played, the line is full", song.title ) );
	} else {
		Q_strncpyz( song.by, l->by, sizeof( song.by ) );
		box->songs[box->numSongs++] = song;
		if ( box->numSongs == 1 )
			Box_NoteNear( l->box, l->client, va( "^5Boombox^7: %s^7 put on %s (%s)", song.by, song.title, Box_Length( song.seconds ) ) );
		else
			Box_NoteNear( l->box, l->client, va( "^5Boombox^7: %s^7 added %s (%s), number %i in the line", song.by, song.title,
				Box_Length( song.seconds ), box->numSongs - 1 ) );
	}
	if ( box->inuse )
		Box_Tell( l->box );
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
	Box_ReapAll();
	for ( i = 0; i < BOX_LOOKUPS; i++ ) {
		if ( boxLookups[i].busy )
			Box_LookupDone( &boxLookups[i] );
	}
	for ( i = 0; i < BOX_MAX; i++ ) {
		if ( boxes[i].inuse && boxes[i].numSongs )
			Box_Run( i, now );
	}
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
		Box_StreamStop( &boxStreams[i] );
	for ( i = 0; i < BOX_LOOKUPS; i++ )
		Box_ProcStop( &boxLookups[i].proc );
	Box_ReapAll();
	memset( boxes, 0, sizeof( boxes ) );
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
		boxStreams[i].odd = -1;
	if ( !g_boombox.integer )
		return;
#ifndef JP_BOOMBOX
	trap->Print( "Boombox: off, this game module was built without Opus\n" );
#else
	if ( !G_VoiceOn() ) {
		trap->Print( "Boombox: off, it needs the voice chat (g_voice 1)\n" );
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

jpbox hello <protocol>: the cgame of a client draws boxes. The answer is "jpbox on" and the state of every box,
and from then on he is told every change and hears the boxes near him. jpbox off: no more of it.
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
	trap->SendServerCommand( clientNum, va( "jpbox on %i %i %i", BOX_PROTOCOL, Box_Max(), (int)Box_Range() ) );
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
	const vec3_t	mins = { -12.0f, -12.0f, 0.0f }, maxs = { 12.0f, 12.0f, 20.0f };
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
	end[2] -= 96.0f;
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
	Box_StreamStop( &boxStreams[i] );
	memset( &boxes[i], 0, sizeof( boxes[i] ) );
	boxes[i].inuse = qtrue;
	VectorCopy( spot, boxes[i].origin );
	boxes[i].yaw = AngleNormalize360( yaw );
	boxes[i].volume = BOX_VOLUME;
	Box_Tell( i );
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
static void Box_Ask( gentity_t *ent, int b, const char *asked ) {
	const int		clientNum = ent - g_entities;
	const int		now = trap->Milliseconds();
	boxClient_t		*bc = &boxClients[clientNum];
	char			target[BOX_ASK + 32], why[128];
	int				i;

	if ( bc->askTime && now - bc->askTime < BOX_ASK_MSEC ) {
		Box_Note( clientNum, "^5Boombox^7: wait a moment before you ask for the next song" );
		return;
	}
	if ( !Box_Target( asked, target, sizeof( target ), why, sizeof( why ) ) ) {
		Box_Note( clientNum, va( "^5Boombox^7: %s", why ) );
		return;
	}
	for ( i = 0; i < BOX_LOOKUPS && boxLookups[i].busy; i++ )
		;
	if ( i == BOX_LOOKUPS ) {
		Box_Note( clientNum, "^5Boombox^7: the server looks up too many songs right now, try again in a moment" );
		return;
	}
	if ( b < 0 )
		b = Box_Nearest( ent );
	if ( b < 0 )
		b = Box_PlaceInFront( ent );
	if ( b < 0 )
		return;
	if ( boxes[b].numSongs + boxes[b].looking >= Box_QueueMax() ) {
		Box_Note( clientNum, "^5Boombox^7: the line of that boombox is full" );
		return;
	}
	if ( !Box_LookupStart( &boxLookups[i], target, why, sizeof( why ) ) ) {
		Box_Note( clientNum, va( "^5Boombox^7: %s", why ) );
		return;
	}
	boxLookups[i].box = b;
	boxLookups[i].client = clientNum;
	Q_strncpyz( boxLookups[i].by, ent->client->pers.netname, sizeof( boxLookups[i].by ) );
	boxes[b].looking++;
	bc->askTime = now;
	Box_Note( clientNum, "^5Boombox^7: looking it up..." );
	Box_Tell( b );
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
		trap->SendServerCommand( clientNum, "print \"The boombox needs VisionJK: only its players see and hear it (or cg_boombox is 0)\n\"" );
		return;
	}
	if ( !Q_stricmp( what, "list" ) ) {
		Box_List( clientNum );
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
		Box_Ask( ent, b, ConcatArgs( 3 ) );
		return;
	}
	if ( b < 0 || b >= BOX_MAX || !boxes[b].inuse ) {
		Box_Note( clientNum, "^5Boombox^7: there is no such boombox" );
		return;
	}
	box = &boxes[b];
	if ( !Q_stricmp( what, "pause" ) ) {
		if ( box->numSongs && !box->paused ) {
			box->paused = qtrue;
			Box_Tell( b );
		}
	} else if ( !Q_stricmp( what, "resume" ) ) {
		if ( box->paused ) {
			box->paused = qfalse;
			box->startMsec = trap->Milliseconds() - box->frames * VOICE_FRAME_MSEC;
			Box_Tell( b );
		}
	} else if ( !Q_stricmp( what, "skip" ) ) {
		if ( box->numSongs ) {
			Box_NoteNear( b, clientNum, va( "^5Boombox^7: %s^7 skipped %s", ent->client->pers.netname, box->songs[0].title ) );
			Box_Next( b );
		}
	} else if ( !Q_stricmp( what, "stop" ) ) {
		if ( box->numSongs ) {
			Box_NoteNear( b, clientNum, va( "^5Boombox^7: %s^7 stopped the music", ent->client->pers.netname ) );
			Box_Stop( b );
		}
	} else if ( !Q_stricmp( what, "remove" ) ) {
		Box_StreamStop( &boxStreams[b] );
		memset( box, 0, sizeof( *box ) );
		Box_Tell( b );
	} else if ( !Q_stricmp( what, "volume" ) ) {
		box->volume = Com_Clampi( 0, 100, (int)Box_ArgFloat( 3 ) );
		Box_Tell( b );
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
		Box_Tell( b );
	} else {
		Box_Usage( clientNum );
	}
}

// the server command boombox: the boxes and their lines, "boombox clear" takes them all away
void Svcmd_Boombox_f( void ) {
	char	arg[16];
	int		i, k;

	trap->Argv( 1, arg, sizeof( arg ) );
	if ( !boxOn ) {
		trap->Print( "The boombox is off (g_boombox %i, g_voice %i)\n", g_boombox.integer, g_voice.integer );
		return;
	}
	if ( !Q_stricmp( arg, "clear" ) ) {
		for ( i = 0; i < BOX_MAX; i++ ) {
			if ( boxes[i].inuse ) {
				Box_StreamStop( &boxStreams[i] );
				memset( &boxes[i], 0, sizeof( boxes[i] ) );
				Box_Tell( i );
			}
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
		if ( box->streaming && boxStreams[i].proc.errLine[0] )
			trap->Print( "  last word of yt-dlp or ffmpeg: %s\n", boxStreams[i].proc.errLine );
	}
	for ( i = k = 0; i < MAX_CLIENTS; i++ ) {
		if ( Box_Hears( i ) )
			k++;
	}
	trap->Print( "%i players see the boomboxes\n", k );
}
