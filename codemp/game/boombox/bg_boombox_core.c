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
#define BOX_FETCH_MSEC		20000		// a song of which nothing came for this long is skipped,
#define BOX_SEEK_SLOW		10			//	and this many times faster than it plays a song is fetched at least when it is begun
										//	further in (ffmpeg decodes all that is before and drops it)
#define BOX_SEEK_END		2			// a jump goes this many seconds before the end of a song at most
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

// how far into its song a box is, in milliseconds: what was jumped over and what went out
int Box_Msec( const box_t *box ) {
	return (int)( (long long)box->offset * 1000 / BOX_RATE ) + box->frames * VOICE_FRAME_MSEC;
}

// a sound system plays its songs in stereo
int Box_Channels( const box_t *box ) {
	return ( box->kind == BOX_KIND_SYSTEM ) ? 2 : 1;
}

// Where a part of a box stands (BOX_PART_*). qfalse: the box has no such part (a boombox has only its body).
qboolean Box_PartAt( const box_t *box, int part, vec3_t at, float *yaw ) {
	if ( part == BOX_PART_BODY ) {
		VectorCopy( box->origin, at );
		*yaw = box->yaw;
		return qtrue;
	}
	if ( box->kind != BOX_KIND_SYSTEM || part < 0 || part >= BOX_PARTS )
		return qfalse;
	VectorCopy( box->parts[part - 1], at );
	*yaw = box->partYaw[part - 1];
	return qtrue;
}

// Does a part at spot stand too near any part of the boxes of a host, but itself (box self, part selfPart; -1 for a
// new one)? big: it is a part of a sound system, which wants more room round it than a boombox.
qboolean Box_Crowded( const box_t *boxes, int numBoxes, const vec3_t spot, qboolean big, int self, int selfPart ) {
	vec3_t	at;
	float	yaw;
	int		i, part;

	for ( i = 0; i < numBoxes; i++ ) {
		if ( !boxes[i].inuse )
			continue;
		for ( part = 0; part < BOX_PARTS; part++ ) {
			if ( ( i == self && part == selfPart ) || !Box_PartAt( &boxes[i], part, at, &yaw ) )
				continue;
			if ( Distance( at, spot ) < ( ( big || boxes[i].kind == BOX_KIND_SYSTEM ) ? SYS_APART : BOX_APART ) )
				return qtrue;
		}
	}
	return qfalse;
}

// how many songs wait after the one that plays: in the line, and of playlists behind it
int Box_Waiting( const box_t *box ) {
	return Q_max( 0, box->numSongs - 1 ) + box->listNum - box->listAt;
}

// what waits of playlists moves into the line as far as it has room
static void Box_Fill( boxHost_t *host, box_t *box ) {
	while ( box->numSongs < host->queueMax && box->listAt < box->listNum ) {
		box->songs[box->numSongs++] = box->list[box->listAt++];
		box->lineRev++;
	}
	if ( box->listAt == box->listNum )
		box->listAt = box->listNum = 0;
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
		box->lineRev++;
	}
	Box_Fill( host, box );
	if ( !box->numSongs )
		Box_GiveBack( box );
	box->streaming = box->started = box->paused = qfalse;
	box->frames = box->restarts = box->offset = 0;
	Box_Tell( host, b );
}

void Box_Stop( boxHost_t *host, int b ) {
	host->boxes[b].numSongs = 0;
	host->boxes[b].listAt = host->boxes[b].listNum = 0;
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

/*
=================
Box_Touch

A skip or a seek of a player restarts the programs of a song (Box_StreamStart): a fork and a fresh lookup at
YouTube each time. qfalse: this box had one less than BOX_TOUCH_MSEC ago, the next is not taken, whoever asks.
=================
*/
#define BOX_TOUCH_MSEC		1000

qboolean Box_Touch( box_t *box, int now ) {
	if ( box->touched && now - box->touched < BOX_TOUCH_MSEC && now >= box->touched )
		return qfalse;
	box->touched = now ? now : 1;
	return qtrue;
}

/*
=================
Box_Seek

The song that plays goes on from that second: its programs begin again there, and it waits for enough of it as
a song that begins does. Paused it stays paused.
=================
*/
void Box_Seek( boxHost_t *host, int b, int seconds, int now ) {
	box_t		*box = &host->boxes[b];
	boxTools_t	tools;
	char		why[128];

	if ( !box->numSongs )
		return;
	seconds = Com_Clampi( 0, Q_max( 0, box->songs[0].seconds - BOX_SEEK_END ), seconds );
	box->offset = seconds * BOX_RATE;
	box->frames = box->restarts = 0;
	box->started = qfalse;
	box->waitSince = now;
	// (not begun yet: it begins there)
	if ( box->streaming ) {
		host->tools( &tools );
		if ( !Box_StreamStart( &box->player->stream, &tools, box->songs[0].id, box->offset, qfalse, Box_Channels( box ), now, why,
				sizeof( why ) ) ) {
			host->note( host, b, -1, Box_Va( "^5Boombox^7: %s is not played, %s", box->songs[0].title, why ) );
			Box_Next( host, b );
			return;
		}
	}
	Box_Tell( host, b );
}

// A song that waits in the line is taken out of it, index 1 the next one. The one that plays is skipped with Box_Next.
void Box_Drop( boxHost_t *host, int b, int index ) {
	box_t *box = &host->boxes[b];

	if ( index < 1 || index >= box->numSongs )
		return;
	memmove( &box->songs[index], &box->songs[index + 1], ( box->numSongs - index - 1 ) * sizeof( box->songs[0] ) );
	box->numSongs--;
	box->lineRev++;
	Box_Fill( host, box );
	Box_Tell( host, b );
}

// The encoder of a channel of a player, the first time it is used. A player that comes to another box starts its
// encoders anew. Each channel is a voice of one channel of its own: the left and the right speaker of a sound system
// are two speakers of the relay.
static qboolean Box_Encoder( boxPlayer_t *player, int channel ) {
#ifdef JP_BOOMBOX
	OpusEncoder *enc = (OpusEncoder *)&player->encoder[channel];

	if ( player->encoderOk[channel] ) {
		opus_encoder_ctl( enc, OPUS_RESET_STATE );
		return qtrue;
	}
	if ( opus_encoder_get_size( 1 ) > (int)sizeof( player->encoder[0] ) || opus_encoder_init( enc, BOX_RATE, 1, OPUS_APPLICATION_AUDIO ) != OPUS_OK )
		return qfalse;
	opus_encoder_ctl( enc, OPUS_SET_BITRATE( BOX_BITRATE ) );
	opus_encoder_ctl( enc, OPUS_SET_SIGNAL( OPUS_SIGNAL_MUSIC ) );
	// every frame carries a small copy of the one before it, so one lost datagram can be made good
	opus_encoder_ctl( enc, OPUS_SET_INBAND_FEC( 1 ) );
	opus_encoder_ctl( enc, OPUS_SET_PACKET_LOSS_PERC( 5 ) );
	player->encoderOk[channel] = qtrue;
	return qtrue;
#else
	(void)player;
	(void)channel;
	return qfalse;
#endif
}

// the programs of the first song in the line start, with a player of the host. Without a free one it waits.
static void Box_Begin( boxHost_t *host, int b, int now ) {
	box_t		*box = &host->boxes[b];
	boxTools_t	tools;
	char		why[128];

	if ( !box->player ) {
		int channel;

		// (without a free one it waits: the clients see it is loading)
		box->player = host->borrow( host );
		if ( !box->player )
			return;
		box->player->inuse = qtrue;
		box->player->stream.partLen = 0;
		for ( channel = 0; channel < Box_Channels( box ); channel++ ) {
			if ( !Box_Encoder( box->player, channel ) ) {
				host->note( host, b, -1, "^5Boombox^7: the server can not code sound" );
				Box_Stop( host, b );
				return;
			}
		}
	}
	host->tools( &tools );
	if ( !Box_StreamStart( &box->player->stream, &tools, box->songs[0].id, box->offset, qfalse, Box_Channels( box ), now, why,
			sizeof( why ) ) ) {
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

// One frame of a song to the host, which hands it to whoever hears the box: every channel coded by itself, with
// the same sequence number.
static void Box_Send( boxHost_t *host, int b, const short *samples ) {
#ifdef JP_BOOMBOX
	box_t		*box = &host->boxes[b];
	const int	channels = box->player->stream.channels;
	byte		frame[VOICE_MAX_FRAME];
	short		one[VOICE_FRAME_SAMPLES];
	int			channel, i, len;

	box->seq = ( box->seq + 1 ) & 0xffff;
	box->frames++;
	for ( channel = 0; channel < channels; channel++ ) {
		const short *in = samples;

		if ( channels > 1 ) {
			for ( i = 0; i < VOICE_FRAME_SAMPLES; i++ )
				one[i] = samples[i * channels + channel];
			in = one;
		}
		len = opus_encode( (OpusEncoder *)&box->player->encoder[channel], in, VOICE_FRAME_SAMPLES, frame, sizeof( frame ) );
		if ( len > 0 )
			host->send( host, b, channel, frame, len );
	}
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
	short		samples[VOICE_FRAME_SAMPLES * 2];
	char		why[128];
	int			due;

	if ( !box->inuse || !box->numSongs )
		return;
	if ( !box->streaming ) {
		Box_Begin( host, b, now );
		return;
	}
	s = &box->player->stream;
	Box_StreamFill( s, now );
	if ( s->ended && box->restarts < BOX_RESTARTS && s->fetched < box->songs[0].seconds * BOX_RATE - BOX_RESTART_SLACK
		&& ( box->started || s->fetched > 0 ) ) {
		box->restarts++;
		host->tools( &tools );
		Box_StreamStart( s, &tools, box->songs[0].id, s->fetched, qtrue, s->channels, now, why, sizeof( why ) );
	}

	if ( !box->started ) {
		if ( Box_StreamQueued( s ) >= BOX_START_SAMPLES || ( s->ended && Box_StreamQueued( s ) > 0 ) ) {
			box->started = qtrue;
			box->startMsec = now;
			box->frames = 0;
			Box_Tell( host, b );
		} else if ( s->ended || now - box->waitSince > BOX_FETCH_MSEC + box->offset / ( BOX_RATE / 1000 ) / BOX_SEEK_SLOW ) {
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
	// (a playlist goes behind the line, as far as there is room)
	if ( strstr( target, "list=" ) ? box->listNum - box->listAt >= BOX_LIST : box->numSongs + box->looking >= host->queueMax ) {
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

// The songs of a playlist that was looked up: into the line as far as it has room, the rest behind it. The
// player who asked is told how many, and how many were left out.
static void Box_ListDone( boxLookup_t *l, boxSong_t *songs, int found ) {
	boxHost_t	*host = l->host;
	box_t		*box = &host->boxes[l->box];
	int			i, first = 0, added = 0, tooLong = 0, noRoom = 0;

	// (the video of the link is where it begins, if it is among them)
	for ( i = 0; l->start[0] && i < found; i++ ) {
		if ( !Q_strncmp( songs[i].id, l->start, sizeof( songs[i].id ) ) ) {
			first = i;
			break;
		}
	}
	if ( box->listAt > 0 ) {
		memmove( &box->list[0], &box->list[box->listAt], ( box->listNum - box->listAt ) * sizeof( box->list[0] ) );
		box->listNum -= box->listAt;
		box->listAt = 0;
	}
	for ( i = first; i < found; i++ ) {
		if ( host->maxSeconds > 0 && songs[i].seconds > host->maxSeconds ) {
			tooLong++;
			continue;
		}
		if ( box->listNum >= BOX_LIST ) {
			noRoom++;
			continue;
		}
		Q_strncpyz( songs[i].by, l->by, sizeof( songs[i].by ) );
		box->list[box->listNum++] = songs[i];
		added++;
	}
	Box_Fill( host, box );
	if ( !added ) {
		host->note( host, -1, l->client, tooLong ? Box_Va( "^5Boombox^7: the songs of that playlist are too long (at most %s)",
			Box_Length( host->maxSeconds ) ) : "^5Boombox^7: the line of that boombox is full" );
		return;
	}
	host->note( host, -1, l->client, Box_Va( "^5Boombox^7: %i %s of the playlist in the line%s%s", added, ( added == 1 ) ? "song" : "songs",
		tooLong ? Box_Va( ", %i too long", tooLong ) : "", noRoom ? Box_Va( ", %i did not fit", noRoom ) : "" ) );
}

// A lookup ended: the song goes into the line of its box, or the player who asked is told why not.
static void Box_LookupDone( boxLookup_t *l, int now ) {
	static boxSong_t	songs[BOX_LIST];	// (large for a stack, and only one lookup ends at a time)
	boxHost_t			*host = l->host;
	box_t				*box = &host->boxes[l->box];
	boxSong_t			song;
	char				why[128];
	const int			result = Box_LookupFrame( l, songs, l->list ? BOX_LIST : 1, now, why, sizeof( why ) );

	if ( result == 0 )
		return;
	if ( box->looking > 0 )
		box->looking--;
	song = songs[0];
	if ( result < 0 ) {
		host->note( host, -1, l->client, Box_Va( "^5Boombox^7: not played, %s", why ) );
	} else if ( !box->inuse ) {
		host->note( host, -1, l->client, Box_Va( "^5Boombox^7: %s is not played, the boombox is gone",
			l->list ? "the playlist" : song.title ) );
	} else if ( l->list ) {
		Box_ListDone( l, songs, result );
	} else if ( host->maxSeconds > 0 && song.seconds > host->maxSeconds ) {
		host->note( host, -1, l->client, Box_Va( "^5Boombox^7: %s is too long (%s, at most %s)", song.title, Box_Length( song.seconds ),
			Box_Length( host->maxSeconds ) ) );
	} else if ( box->numSongs >= host->queueMax ) {
		host->note( host, -1, l->client, Box_Va( "^5Boombox^7: %s is not played, the line is full", song.title ) );
	} else {
		Q_strncpyz( song.by, l->by, sizeof( song.by ) );
		box->songs[box->numSongs++] = song;
		box->lineRev++;
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
