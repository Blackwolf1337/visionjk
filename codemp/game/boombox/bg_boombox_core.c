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

// bg_boombox_core.c -- what plays the songs of boxes: the line of songs of a box, its lookups, and the frames that
// are due by the clock, coded with Opus and handed to the host of the box (bg_boombox_core.h). The game module of
// a server of this tree (game/boombox/g_boombox.c) and the masterserver for every other server
// (master/ms_boombox.c) are its hosts. Not from JA+, see docs/japlus/boombox.md.
//
// A song plays for everybody at once: 20 ms of it are coded when they are due and go to whoever the host lets
// hear the box. A song that is fetched too slowly waits, and everybody hears it wait. One whose fetch broke off in
// the middle is fetched again from where it was. Nothing in here waits.

#include "qcommon/q_shared.h"
#include "game/voice/bg_voice.h"
#include "bg_boombox.h"
#include "bg_boombox_core.h"

#ifdef JP_BOOMBOX
	#include <opus.h>
#endif

#define BOX_START_SAMPLES	( BOX_RATE / 2 )	// what has to be there of a song before it starts: half a second
#define BOX_FETCH_MSEC		20000		// a song of which nothing came for this long is skipped
#define BOX_RESTARTS		3			// times a fetch that broke off is begun again further in
#define BOX_RESTART_SLACK	( BOX_RATE * 3 )	// a fetch that ends this close to the length of the song ended
#define BOX_BURST			8			// frames that go out at once at most, after the host stood still
#define BOX_BITRATE			64000		// what a song is coded with: Opus, one channel

/*
=================
Box_Va

va() for these files. They are built into the masterserver too, where va() and Com_sprintf() are functions of C++
(qcommon/q_shared.cpp) that a file of C can not call. Eight buffers in turn.
=================
*/
const char *Box_Va( const char *format, ... ) {
	static char	text[8][1024];
	static int	index;
	char		*out = text[index++ & 7];
	va_list		args;

	va_start( args, format );
	vsnprintf( out, sizeof( text[0] ), format, args );
	va_end( args );
	return out;
}

const char *Box_Length( int seconds ) {
	return Box_Va( "%i:%02i", seconds / 60, seconds % 60 );
}

// what the clients are told a box does (BOX_PLAYING and the others of bg_boombox.h)
int Box_Flags( const box_t *box ) {
	int flags = 0;

	if ( box->numSongs ) {
		flags |= BOX_PLAYING;
		if ( box->paused )
			flags |= BOX_PAUSED;
		if ( !box->started )
			flags |= BOX_LOADING;
	}
	if ( box->looking )
		flags |= BOX_LOOKING;
	return flags;
}

static void Box_Tell( boxHost_t *host, int b ) {
	host->boxes[b].rev++;
	host->tell( host, b );
}

// the player of a box goes back to its host
static void Box_GiveBack( box_t *box ) {
	if ( box->player ) {
		Box_StreamStop( &box->player->stream );
		box->player->inuse = qfalse;
		box->player = NULL;
	}
}

/*
=================
Box_Next

The song that played is through, or was skipped: the next one in the line, if there is one. A box without songs
gives its player back.
=================
*/
void Box_Next( boxHost_t *host, int b ) {
	box_t *box = &host->boxes[b];

	if ( box->player )
		Box_StreamStop( &box->player->stream );
	if ( box->numSongs > 0 ) {
		memmove( &box->songs[0], &box->songs[1], ( box->numSongs - 1 ) * sizeof( box->songs[0] ) );
		box->numSongs--;
	}
	if ( !box->numSongs )
		Box_GiveBack( box );
	box->streaming = box->started = box->paused = qfalse;
	box->frames = box->restarts = 0;
	Box_Tell( host, b );
}

void Box_Stop( boxHost_t *host, int b ) {
	host->boxes[b].numSongs = 0;
	Box_Next( host, b );
}

// The box is gone. What is looked up for it is put into nobody's line (Box_Lookups), and the host tells the clients.
void Box_Remove( boxHost_t *host, int b ) {
	Box_GiveBack( &host->boxes[b] );
	memset( &host->boxes[b], 0, sizeof( host->boxes[b] ) );
	host->tell( host, b );
}

void Box_Pause( boxHost_t *host, int b, qboolean pause, int now ) {
	box_t *box = &host->boxes[b];

	if ( pause == box->paused || ( pause && !box->numSongs ) )
		return;
	box->paused = pause;
	// on from where it was
	if ( !pause )
		box->startMsec = now - box->frames * VOICE_FRAME_MSEC;
	Box_Tell( host, b );
}

// the encoder of a player, the first time it is used. A player that comes to another box starts its encoder anew.
static qboolean Box_Encoder( boxPlayer_t *player ) {
#ifdef JP_BOOMBOX
	OpusEncoder *enc = (OpusEncoder *)&player->encoder;

	if ( player->encoderOk ) {
		opus_encoder_ctl( enc, OPUS_RESET_STATE );
		return qtrue;
	}
	if ( opus_encoder_get_size( 1 ) > (int)sizeof( player->encoder ) || opus_encoder_init( enc, BOX_RATE, 1, OPUS_APPLICATION_AUDIO ) != OPUS_OK )
		return qfalse;
	opus_encoder_ctl( enc, OPUS_SET_BITRATE( BOX_BITRATE ) );
	opus_encoder_ctl( enc, OPUS_SET_SIGNAL( OPUS_SIGNAL_MUSIC ) );
	// every frame carries a small copy of the one before it, so one lost datagram can be made good
	opus_encoder_ctl( enc, OPUS_SET_INBAND_FEC( 1 ) );
	opus_encoder_ctl( enc, OPUS_SET_PACKET_LOSS_PERC( 5 ) );
	player->encoderOk = qtrue;
	return qtrue;
#else
	(void)player;
	return qfalse;
#endif
}

// the programs of the first song in the line start, with a player of the host. Without a free one it waits.
static void Box_Begin( boxHost_t *host, int b, int now ) {
	box_t		*box = &host->boxes[b];
	boxTools_t	tools;
	char		why[128];

	if ( !box->player ) {
		// (without a free one it waits: the clients see it is loading)
		box->player = host->borrow( host );
		if ( !box->player )
			return;
		box->player->inuse = qtrue;
		box->player->stream.odd = -1;
		if ( !Box_Encoder( box->player ) ) {
			host->note( host, b, -1, "^5Boombox^7: the server can not code sound" );
			Box_Stop( host, b );
			return;
		}
	}
	host->tools( &tools );
	if ( !Box_StreamStart( &box->player->stream, &tools, box->songs[0].id, 0, qfalse, now, why, sizeof( why ) ) ) {
		host->note( host, b, -1, Box_Va( "^5Boombox^7: %s is not played, %s", box->songs[0].title, why ) );
		Box_Next( host, b );
		return;
	}
	box->streaming = qtrue;
	box->started = qfalse;
	box->frames = box->restarts = 0;
	box->waitSince = now;
	Box_Tell( host, b );
}

// one frame of a song to the host, which hands it to whoever hears the box
static void Box_Send( boxHost_t *host, int b, const short *samples ) {
#ifdef JP_BOOMBOX
	box_t		*box = &host->boxes[b];
	byte		frame[VOICE_MAX_FRAME];
	const int	len = opus_encode( (OpusEncoder *)&box->player->encoder, samples, VOICE_FRAME_SAMPLES, frame, sizeof( frame ) );

	box->seq = ( box->seq + 1 ) & 0xffff;
	box->frames++;
	if ( len > 0 )
		host->send( host, b, frame, len );
#else
	(void)host;
	(void)b;
	(void)samples;
#endif
}

/*
=================
Box_Run

Once a frame for a box with songs: its programs are started, what they made is taken, and the frames that are
due by the clock go out.
=================
*/
void Box_Run( boxHost_t *host, int b, int now ) {
	box_t		*box = &host->boxes[b];
	boxStream_t	*s;
	boxTools_t	tools;
	short		samples[VOICE_FRAME_SAMPLES];
	char		why[128];
	int			due;

	if ( !box->inuse || !box->numSongs )
		return;
	if ( !box->streaming ) {
		Box_Begin( host, b, now );
		return;
	}
	s = &box->player->stream;
	Box_StreamFill( s );
	if ( s->ended && box->restarts < BOX_RESTARTS && s->fetched < box->songs[0].seconds * BOX_RATE - BOX_RESTART_SLACK
		&& ( box->started || s->fetched > 0 ) ) {
		box->restarts++;
		host->tools( &tools );
		Box_StreamStart( s, &tools, box->songs[0].id, s->fetched, qtrue, now, why, sizeof( why ) );
	}

	if ( !box->started ) {
		if ( Box_StreamQueued( s ) >= BOX_START_SAMPLES || ( s->ended && Box_StreamQueued( s ) > 0 ) ) {
			box->started = qtrue;
			box->startMsec = now;
			box->frames = 0;
			Box_Tell( host, b );
		} else if ( s->ended || now - box->waitSince > BOX_FETCH_MSEC ) {
			Box_StreamWhy( s, "nothing of it came", why, sizeof( why ) );
			host->note( host, b, -1, Box_Va( "^5Boombox^7: %s is not played, %s", box->songs[0].title, why ) );
			Box_Next( host, b );
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
			Box_Next( host, b );
			return;
		}
		memset( samples, 0, sizeof( samples ) );
		Box_StreamTake( s, samples, VOICE_FRAME_SAMPLES );
		Box_Send( host, b, samples );
	}
}

/*
=================
Box_Ask

What a player asked for is looked up, and put into the line of the box when it is found. by: his name. qfalse:
nothing is looked up, why says why.
=================
*/
qboolean Box_Ask( boxHost_t *host, boxLookup_t *lookups, int numLookups, int b, int client, const char *by, const char *asked,
	int now, char *why, int whySize ) {
	box_t		*box = &host->boxes[b];
	boxTools_t	tools;
	char		target[BOX_ASK + 32];
	int			i;

	if ( !Box_Target( asked, target, sizeof( target ), why, whySize ) )
		return qfalse;
	for ( i = 0; i < numLookups && lookups[i].busy; i++ )
		;
	if ( i == numLookups ) {
		Q_strncpyz( why, "the server looks up too many songs right now, try again in a moment", whySize );
		return qfalse;
	}
	if ( box->numSongs + box->looking >= host->queueMax ) {
		Q_strncpyz( why, "the line of that boombox is full", whySize );
		return qfalse;
	}
	host->tools( &tools );
	if ( !Box_LookupStart( &lookups[i], &tools, target, now, why, whySize ) )
		return qfalse;
	lookups[i].host = host;
	lookups[i].box = b;
	lookups[i].client = client;
	Q_strncpyz( lookups[i].by, by, sizeof( lookups[i].by ) );
	box->looking++;
	Box_Tell( host, b );
	return qtrue;
}

// A lookup ended: the song goes into the line of its box, or the player who asked is told why not.
static void Box_LookupDone( boxLookup_t *l, int now ) {
	boxHost_t	*host = l->host;
	box_t		*box = &host->boxes[l->box];
	boxSong_t	song;
	char		why[128];
	const int	result = Box_LookupFrame( l, &song, now, why, sizeof( why ) );

	if ( result == 0 )
		return;
	if ( box->looking > 0 )
		box->looking--;
	if ( result < 0 ) {
		host->note( host, -1, l->client, Box_Va( "^5Boombox^7: not played, %s", why ) );
	} else if ( !box->inuse ) {
		host->note( host, -1, l->client, Box_Va( "^5Boombox^7: %s is not played, the boombox is gone", song.title ) );
	} else if ( host->maxSeconds > 0 && song.seconds > host->maxSeconds ) {
		host->note( host, -1, l->client, Box_Va( "^5Boombox^7: %s is too long (%s, at most %s)", song.title, Box_Length( song.seconds ),
			Box_Length( host->maxSeconds ) ) );
	} else if ( box->numSongs >= host->queueMax ) {
		host->note( host, -1, l->client, Box_Va( "^5Boombox^7: %s is not played, the line is full", song.title ) );
	} else {
		Q_strncpyz( song.by, l->by, sizeof( song.by ) );
		box->songs[box->numSongs++] = song;
		// (to the one who asked only: the others see it in the line of the box)
		if ( box->numSongs == 1 )
			host->note( host, -1, l->client, Box_Va( "^5Boombox^7: %s (%s) plays", song.title, Box_Length( song.seconds ) ) );
		else
			host->note( host, -1, l->client, Box_Va( "^5Boombox^7: %s (%s) is number %i in the line", song.title,
				Box_Length( song.seconds ), box->numSongs - 1 ) );
	}
	if ( box->inuse )
		Box_Tell( host, l->box );
}

// once a frame: the lookups that run
void Box_Lookups( boxLookup_t *lookups, int numLookups, int now ) {
	int i;

	for ( i = 0; i < numLookups; i++ ) {
		if ( lookups[i].busy )
			Box_LookupDone( &lookups[i], now );
	}
}

// the host is gone (a room of the masterserver ended): its lookups stop and put nothing anywhere
void Box_DropLookups( boxLookup_t *lookups, int numLookups, const boxHost_t *host ) {
	int i;

	for ( i = 0; i < numLookups; i++ ) {
		if ( lookups[i].busy && lookups[i].host == host ) {
			Box_ProcStop( &lookups[i].proc );
			lookups[i].busy = qfalse;
		}
	}
}
