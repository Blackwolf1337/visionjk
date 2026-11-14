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

/*
cg_voice_feed.c -- Sync audioplayer: what one music player on this computer plays (Spotify, YouTube Music,
Quince ...), taken from the sound server and coded as the server codes the song of a box, 20 ms at a time. The
sync itself (which box, the title, the commands) is boombox/cg_boombox_sync.c; the frames go out as RELAY_FEED
(game/voice/bg_voice.h). Not from JA+, see docs/japlus/boombox.md.

Linux: the sound server is asked through libpulse (PulseAudio, or PipeWire through pipewire-pulse), loaded when it
is first needed, so the game neither needs it to build nor to start. Only the stream of that one program is
recorded: a record stream that monitors one sink input (pa_stream_set_monitor_stream), so nothing else the
computer plays, the game least of all, gets into it. Which sink input is the player's: the one whose process is
the process that owns the player's name on the desktop bus, or a child of it (Spotify and the browsers play from a
child process; a program of PipeWire itself says its process only on its client), else the one whose program has
the name of the player. The library works in a thread of its own
(threaded mainloop); the thread of the game takes what came, under its lock, once a frame, and never waits.

Windows and the other systems: nothing yet (CG_FeedSupported).
*/

#include "cgame/cg_local.h"
#include "game/voice/bg_voice.h"
#include "cgame/boombox/cg_boombox.h"
#include "ui/ui_music.h"

#if defined( JP_VOICE ) && defined( __linux__ )

#include <opus.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

#define FEED_RING			( VOICE_RATE * 2 )	// stereo samples that wait at most: 2 s
#define FEED_BITRATE		64000				// as BOX_BITRATE of bg_boombox_core.c
#define FEED_ENCODER_BYTES	40960
#define FEED_LOOK_MSEC		1000				// the sink inputs are looked through this often
#define FEED_QUIET_MSEC		200					// no sound for this long: silence goes out instead, by the clock
#define FEED_BURST			8					// frames of silence at once at most
#define FEED_CANDIDATES		32

// What is used of libpulse, declared here so the game builds without its headers. The layouts are the ones of
// pulse/sample.h, channelmap.h, volume.h and introspect.h (checked against them on 32 and 64 bit).
typedef struct paSpec_s { int format; uint32_t rate; uint8_t channels; } paSpec_t;
typedef struct paMap_s { uint8_t channels; int map[32]; } paMap_t;
typedef struct paVolume_s { uint8_t channels; uint32_t values[32]; } paVolume_t;
typedef struct paAttr_s { uint32_t maxlength, tlength, prebuf, minreq, fragsize; } paAttr_t;
typedef struct paSinkInput_s {
	uint32_t	index;
	const char	*name;
	uint32_t	owner_module, client, sink;
	paSpec_t	sample_spec;
	paMap_t		channel_map;
	paVolume_t	volume;
	uint64_t	buffer_usec, sink_usec;
	const char	*resample_method, *driver;
	int			mute;
	void		*proplist;
	int			corked, has_volume, volume_writable;
	void		*format;
} paSinkInput_t;
typedef struct paClient_s {
	uint32_t	index;
	const char	*name;
	uint32_t	owner_module;
	const char	*driver;
	void		*proplist;
} paClient_t;

#define PA_SAMPLE_S16LE			3
#define PA_CONTEXT_NOAUTOSPAWN	1
#define PA_CONTEXT_READY		4
#define PA_CONTEXT_FAILED		5
#define PA_CONTEXT_TERMINATED	6
#define PA_STREAM_READY			2
#define PA_STREAM_FAILED		3
#define PA_STREAM_TERMINATED	4
#define PA_STREAM_DONT_MOVE		512
#define PA_STREAM_ADJUST_LATENCY	8192
#define PA_OPERATION_RUNNING	0

typedef void ( *paSinkInputCb_t )( void *context, const paSinkInput_t *info, int eol, void *user );
typedef void ( *paClientCb_t )( void *context, const paClient_t *info, int eol, void *user );

static struct {
	void		*library;
	qboolean	failed;			// no library, or no sound server: nothing more is tried
	void		*loop, *context, *stream, *op, *clientOp;

	void		*( *threaded_mainloop_new )( void );
	int			( *threaded_mainloop_start )( void *m );
	void		( *threaded_mainloop_stop )( void *m );
	void		( *threaded_mainloop_free )( void *m );
	void		( *threaded_mainloop_lock )( void *m );
	void		( *threaded_mainloop_unlock )( void *m );
	void		*( *threaded_mainloop_get_api )( void *m );
	void		*( *context_new )( void *api, const char *name );
	int			( *context_connect )( void *c, const char *server, int flags, const void *spawn );
	int			( *context_get_state )( void *c );
	void		( *context_disconnect )( void *c );
	void		( *context_unref )( void *c );
	void		*( *context_get_sink_input_info_list )( void *c, paSinkInputCb_t cb, void *user );
	void		*( *context_get_client_info_list )( void *c, paClientCb_t cb, void *user );
	int			( *operation_get_state )( void *op );
	void		( *operation_unref )( void *op );
	void		( *operation_cancel )( void *op );
	const char	*( *proplist_gets )( const void *p, const char *key );
	void		*( *stream_new )( void *c, const char *name, const paSpec_t *spec, const void *map );
	int			( *stream_set_monitor_stream )( void *s, uint32_t sinkInput );
	int			( *stream_connect_record )( void *s, const char *dev, const paAttr_t *attr, int flags );
	int			( *stream_get_state )( void *s );
	int			( *stream_peek )( void *s, const void **data, size_t *bytes );
	int			( *stream_drop )( void *s );
	int			( *stream_disconnect )( void *s );
	void		( *stream_unref )( void *s );
} pa;

// what the last look through the sink inputs found (written in the thread of the library, under its lock)
typedef struct feedCandidate_s {
	uint32_t	index;
	uint32_t	client;			// of the sound server: a program of PipeWire itself says its process only there
	int			pid;
	int			corked;
	char		name[64];		// application.name
	char		binary[64];		// application.process.binary
} feedCandidate_t;

static struct {
	qboolean		on;					// a sync wants the sound of a player
	int				pid;				// its process, 0: not known
	char			key[MUSIC_KEY];	// its name on the bus ("spotify", "quince")
	int				channels;			// 1: a boombox, mixed down; 2: a sound system, left and right
	uint32_t		input;				// the sink input the stream records, -1: none
	int				lookTime;
	feedCandidate_t	found[FEED_CANDIDATES];
	int				numFound;
	qboolean		looked;				// the look that ran is through
	struct {
		uint32_t	index;
		int			pid;
	} clients[FEED_CANDIDATES * 2];		// the clients of the sound server and their processes,
	int				numClients;
	qboolean		clientsLooked;		//	that look through

	short			ring[FEED_RING * 2];
	unsigned		read, write;		// stereo samples, counted on and on
	int				lastSound;			// when sound came last
	int				quietSince;			// silence goes out by the clock from here,
	int				quietSent;			//	this many frames of it so far
	int				seq;
	int				soundFrames, quietFrames;	// what went out of this sync: frames of sound and of silence
	union {
		double	align;
		void	*alignToo;
		byte	bytes[FEED_ENCODER_BYTES];
	} encoder[2];
	qboolean		encoderOk[2];
} feed;

static void Feed_Close( qboolean failed ) {
	if ( pa.loop ) {
		pa.threaded_mainloop_lock( pa.loop );
		if ( pa.op ) {
			pa.operation_cancel( pa.op );
			pa.operation_unref( pa.op );
			pa.op = NULL;
		}
		if ( pa.clientOp ) {
			pa.operation_cancel( pa.clientOp );
			pa.operation_unref( pa.clientOp );
			pa.clientOp = NULL;
		}
		if ( pa.stream ) {
			pa.stream_disconnect( pa.stream );
			pa.stream_unref( pa.stream );
			pa.stream = NULL;
		}
		if ( pa.context ) {
			pa.context_disconnect( pa.context );
			pa.context_unref( pa.context );
			pa.context = NULL;
		}
		pa.threaded_mainloop_unlock( pa.loop );
		pa.threaded_mainloop_stop( pa.loop );
		pa.threaded_mainloop_free( pa.loop );
		pa.loop = NULL;
	}
	if ( pa.library ) {
		dlclose( pa.library );
		pa.library = NULL;
	}
	pa.failed = failed;
	feed.input = (uint32_t)-1;
}

// The library, its thread and the connection to the sound server, which comes up by itself. qfalse: none of it.
static qboolean Feed_Open( void ) {
	if ( pa.failed )
		return qfalse;
	if ( pa.loop )
		return qtrue;

#define FEED_SYMBOL( name ) \
	*(void **)&pa.name = dlsym( pa.library, "pa_" #name ); \
	if ( !pa.name ) { Feed_Close( qtrue ); return qfalse; }

	pa.library = dlopen( "libpulse.so.0", RTLD_NOW | RTLD_LOCAL );
	if ( !pa.library ) {
		pa.failed = qtrue;
		return qfalse;
	}
	FEED_SYMBOL( threaded_mainloop_new )
	FEED_SYMBOL( threaded_mainloop_start )
	FEED_SYMBOL( threaded_mainloop_stop )
	FEED_SYMBOL( threaded_mainloop_free )
	FEED_SYMBOL( threaded_mainloop_lock )
	FEED_SYMBOL( threaded_mainloop_unlock )
	FEED_SYMBOL( threaded_mainloop_get_api )
	FEED_SYMBOL( context_new )
	FEED_SYMBOL( context_connect )
	FEED_SYMBOL( context_get_state )
	FEED_SYMBOL( context_disconnect )
	FEED_SYMBOL( context_unref )
	FEED_SYMBOL( context_get_sink_input_info_list )
	FEED_SYMBOL( context_get_client_info_list )
	FEED_SYMBOL( operation_get_state )
	FEED_SYMBOL( operation_unref )
	FEED_SYMBOL( operation_cancel )
	FEED_SYMBOL( proplist_gets )
	FEED_SYMBOL( stream_new )
	FEED_SYMBOL( stream_set_monitor_stream )
	FEED_SYMBOL( stream_connect_record )
	FEED_SYMBOL( stream_get_state )
	FEED_SYMBOL( stream_peek )
	FEED_SYMBOL( stream_drop )
	FEED_SYMBOL( stream_disconnect )
	FEED_SYMBOL( stream_unref )
#undef FEED_SYMBOL

	pa.loop = pa.threaded_mainloop_new();
	if ( !pa.loop ) {
		Feed_Close( qtrue );
		return qfalse;
	}
	pa.context = pa.context_new( pa.threaded_mainloop_get_api( pa.loop ), "VisionJK" );
	// (connecting is not waited for: the state is looked at with the frames that follow)
	if ( !pa.context || pa.context_connect( pa.context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL ) < 0 || pa.threaded_mainloop_start( pa.loop ) < 0 ) {
		if ( pa.context ) {
			pa.context_unref( pa.context );
			pa.context = NULL;
		}
		pa.threaded_mainloop_free( pa.loop );
		pa.loop = NULL;
		Feed_Close( qtrue );
		return qfalse;
	}
	feed.input = (uint32_t)-1;
	return qtrue;
}

/*
===============================================================================

Which sink input is the player's

===============================================================================
*/

// the parent of a process, 0: none or not known (/proc/<pid>/stat: "pid (name) state ppid ...", the name may
// have spaces and brackets of its own)
static int Feed_Parent( int pid ) {
	char		text[512];
	const char	*end;
	int			fd, len;

	fd = open( va( "/proc/%i/stat", pid ), O_RDONLY | O_CLOEXEC );
	if ( fd < 0 )
		return 0;
	len = (int)read( fd, text, sizeof( text ) - 1 );
	close( fd );
	if ( len <= 0 )
		return 0;
	text[len] = '\0';
	end = strrchr( text, ')' );
	if ( !end || strlen( end ) < 5 )
		return 0;
	return atoi( end + 4 );
}

// is process pid the player's (ancestor) or one of its children's children
static qboolean Feed_Below( int pid, int ancestor ) {
	int i;

	for ( i = 0; i < 16 && pid > 1; i++ ) {
		if ( pid == ancestor )
			return qtrue;
		pid = Feed_Parent( pid );
	}
	return qfalse;
}

// the thread of the library: one sink input after the other, and an end
static void Feed_SinkInput( void *context, const paSinkInput_t *info, int eol, void *user ) {
	feedCandidate_t	*c;
	const char		*value;

	(void)context;
	(void)user;
	if ( eol ) {
		feed.looked = qtrue;
		return;
	}
	if ( !info || feed.numFound >= FEED_CANDIDATES )
		return;
	c = &feed.found[feed.numFound++];
	memset( c, 0, sizeof( *c ) );
	c->index = info->index;
	c->client = info->client;
	c->corked = info->corked;
	if ( info->proplist ) {
		value = pa.proplist_gets( info->proplist, "application.process.id" );
		c->pid = value ? atoi( value ) : 0;
		value = pa.proplist_gets( info->proplist, "application.name" );
		Q_strncpyz( c->name, value ? value : "", sizeof( c->name ) );
		value = pa.proplist_gets( info->proplist, "application.process.binary" );
		Q_strncpyz( c->binary, value ? value : "", sizeof( c->binary ) );
	}
}

// the thread of the library: one client after the other, and an end
static void Feed_Client( void *context, const paClient_t *info, int eol, void *user ) {
	const char *value;

	(void)context;
	(void)user;
	if ( eol ) {
		feed.clientsLooked = qtrue;
		return;
	}
	if ( !info || !info->proplist || feed.numClients >= (int)ARRAY_LEN( feed.clients ) )
		return;
	value = pa.proplist_gets( info->proplist, "application.process.id" );
	if ( value && atoi( value ) > 0 ) {
		feed.clients[feed.numClients].index = info->index;
		feed.clients[feed.numClients].pid = atoi( value );
		feed.numClients++;
	}
}

// does a name of a program go with the name of the player on the bus ("spotify" and "Spotify", "quince" and "Quince")
static qboolean Feed_NameIs( const char *name, const char *key ) {
	return ( name[0] && key[0] && ( Q_stristr( name, key ) || Q_stristr( key, name ) ) ) ? qtrue : qfalse;
}

// The sink input of the player among what the look found, -1: none. Its own process first, then a child of it,
// then one that has its name; one that plays before one that is corked.
static uint32_t Feed_Pick( void ) {
	uint32_t	best = (uint32_t)-1;
	int			bestScore = 0, i;

	for ( i = 0; i < feed.numFound; i++ ) {
		feedCandidate_t	*c = &feed.found[i];
		int				score = 0, k;

		// (no process on the stream: the one of its client)
		for ( k = 0; !c->pid && k < feed.numClients; k++ ) {
			if ( feed.clients[k].index == c->client )
				c->pid = feed.clients[k].pid;
		}

		if ( feed.pid > 0 && c->pid == feed.pid )
			score = 6;
		else if ( feed.pid > 0 && c->pid > 0 && Feed_Below( c->pid, feed.pid ) )
			score = 4;
		else if ( Feed_NameIs( c->name, feed.key ) || Feed_NameIs( c->binary, feed.key ) )
			score = 2;
		if ( score && !c->corked )
			score++;
		if ( score > bestScore ) {
			bestScore = score;
			best = c->index;
		}
	}
	return best;
}

// records the stream of that sink input from now on (the one before ends). Under the lock.
static void Feed_Record( uint32_t input ) {
	const paSpec_t	spec = { PA_SAMPLE_S16LE, VOICE_RATE, 2 };
	paAttr_t		attr;

	if ( pa.stream ) {
		pa.stream_disconnect( pa.stream );
		pa.stream_unref( pa.stream );
		pa.stream = NULL;
	}
	feed.input = input;
	if ( input == (uint32_t)-1 )
		return;
	pa.stream = pa.stream_new( pa.context, "Sync audioplayer", &spec, NULL );
	if ( !pa.stream )
		return;
	// (one frame of the game at a time comes from the server, no more waits there)
	attr.maxlength = (uint32_t)-1;
	attr.tlength = attr.prebuf = attr.minreq = (uint32_t)-1;
	attr.fragsize = VOICE_FRAME_SAMPLES * 2 * sizeof( short );
	if ( pa.stream_set_monitor_stream( pa.stream, input ) < 0
		|| pa.stream_connect_record( pa.stream, NULL, &attr, PA_STREAM_ADJUST_LATENCY | PA_STREAM_DONT_MOVE ) < 0 ) {
		pa.stream_unref( pa.stream );
		pa.stream = NULL;
	}
}

/*
===============================================================================

What it plays, coded

===============================================================================
*/

static qboolean Feed_Encoder( int channel ) {
	OpusEncoder *enc = (OpusEncoder *)&feed.encoder[channel];

	if ( feed.encoderOk[channel] ) {
		opus_encoder_ctl( enc, OPUS_RESET_STATE );
		return qtrue;
	}
	if ( opus_encoder_get_size( 1 ) > (int)sizeof( feed.encoder[0] ) || opus_encoder_init( enc, VOICE_RATE, 1, OPUS_APPLICATION_AUDIO ) != OPUS_OK )
		return qfalse;
	opus_encoder_ctl( enc, OPUS_SET_BITRATE( FEED_BITRATE ) );
	opus_encoder_ctl( enc, OPUS_SET_SIGNAL( OPUS_SIGNAL_MUSIC ) );
	opus_encoder_ctl( enc, OPUS_SET_INBAND_FEC( 1 ) );
	opus_encoder_ctl( enc, OPUS_SET_PACKET_LOSS_PERC( 5 ) );
	feed.encoderOk[channel] = qtrue;
	return qtrue;
}

// one frame of stereo samples (NULL: silence) coded and handed out, a boombox mixed down to one channel
static void Feed_Send( const short *stereo, feedOut_t out ) {
	static const short	silence[VOICE_FRAME_SAMPLES * 2];
	short				one[VOICE_FRAME_SAMPLES];
	byte				frame[VOICE_MAX_FRAME];
	int					channel, i, len;

	if ( !stereo )
		stereo = silence;
	feed.seq = ( feed.seq + 1 ) & 0xffff;
	for ( channel = 0; channel < feed.channels; channel++ ) {
		for ( i = 0; i < VOICE_FRAME_SAMPLES; i++ )
			one[i] = ( feed.channels == 1 ) ? (short)( ( stereo[i * 2] + stereo[i * 2 + 1] ) / 2 ) : stereo[i * 2 + channel];
		// (one byte less than a frame may be: the box and the channel go in front of it, RELAY_FEED)
		len = opus_encode( (OpusEncoder *)&feed.encoder[channel], one, VOICE_FRAME_SAMPLES, frame, sizeof( frame ) - 1 );
		if ( len > 0 )
			out( channel, feed.seq, frame, len );
	}
}

/*
===============================================================================

For the sync (boombox/cg_boombox_sync.c)

===============================================================================
*/

qboolean CG_FeedSupported( void ) {
	return Feed_Open();
}

/*
=================
CG_FeedStart

The sound of the player with that process (0: not known) and that name on the bus is taken from now on, for a
box of channels channels (2: a sound system). The stream itself is found with the frames that follow.
=================
*/
qboolean CG_FeedStart( int pid, const char *key, int channels ) {
	int channel;

	if ( !Feed_Open() )
		return qfalse;
	feed.channels = Com_Clampi( 1, 2, channels );
	for ( channel = 0; channel < feed.channels; channel++ ) {
		if ( !Feed_Encoder( channel ) )
			return qfalse;
	}
	feed.on = qtrue;
	feed.pid = pid;
	Q_strncpyz( feed.key, key, sizeof( feed.key ) );
	feed.read = feed.write = 0;
	feed.lastSound = feed.quietSince = trap->Milliseconds();
	feed.quietSent = 0;
	feed.lookTime = 0;
	feed.soundFrames = feed.quietFrames = 0;
	pa.threaded_mainloop_lock( pa.loop );
	Feed_Record( (uint32_t)-1 );
	pa.threaded_mainloop_unlock( pa.loop );
	return qtrue;
}

// no more: the stream ends, the connection to the sound server stays for the next sync
void CG_FeedStop( void ) {
	feed.on = qfalse;
	if ( pa.loop ) {
		pa.threaded_mainloop_lock( pa.loop );
		Feed_Record( (uint32_t)-1 );
		pa.threaded_mainloop_unlock( pa.loop );
	}
}

// the sound of the player is found and recorded
qboolean CG_FeedFound( void ) {
	return ( feed.on && feed.input != (uint32_t)-1 ) ? qtrue : qfalse;
}

void CG_FeedShutdown( void ) {
	feed.on = qfalse;
	Feed_Close( qfalse );
}

// for v_voice_status: frames of sound and of silence that went out of this sync
void CG_FeedCounts( int *sound, int *quiet ) {
	*sound = feed.soundFrames;
	*quiet = feed.quietFrames;
}

/*
=================
CG_FeedFrame

Once a frame while a sync runs: the sink input of the player is looked for, what came of its stream is taken,
and every whole frame of it goes to out, coded. While nothing comes (the player is paused, or its stream is not
found yet) silence goes out by the clock, a few bytes a frame, so the box does not end its sync.
=================
*/
void CG_FeedFrame( int now, feedOut_t out ) {
	short	frame[VOICE_FRAME_SAMPLES * 2];
	int		state, due, i;

	if ( !feed.on || !pa.loop )
		return;
	pa.threaded_mainloop_lock( pa.loop );
	state = pa.context_get_state( pa.context );
	if ( state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED ) {
		pa.threaded_mainloop_unlock( pa.loop );
		Feed_Close( qtrue );
		feed.on = qfalse;
		return;
	}
	if ( state == PA_CONTEXT_READY ) {
		// what the last look found, and the next look
		if ( pa.op && pa.operation_get_state( pa.op ) != PA_OPERATION_RUNNING
			&& ( !pa.clientOp || pa.operation_get_state( pa.clientOp ) != PA_OPERATION_RUNNING ) ) {
			pa.operation_unref( pa.op );
			pa.op = NULL;
			if ( pa.clientOp ) {
				pa.operation_unref( pa.clientOp );
				pa.clientOp = NULL;
			}
			if ( feed.looked ) {
				const uint32_t input = Feed_Pick();

				if ( input != feed.input || ( pa.stream && pa.stream_get_state( pa.stream ) >= PA_STREAM_FAILED ) )
					Feed_Record( input );
			}
		}
		if ( !pa.op && now - feed.lookTime >= FEED_LOOK_MSEC ) {
			feed.lookTime = now;
			feed.numFound = feed.numClients = 0;
			feed.looked = feed.clientsLooked = qfalse;
			pa.op = pa.context_get_sink_input_info_list( pa.context, Feed_SinkInput, NULL );
			pa.clientOp = pa.op ? pa.context_get_client_info_list( pa.context, Feed_Client, NULL ) : NULL;
		}
		// what came of the stream
		if ( pa.stream && pa.stream_get_state( pa.stream ) == PA_STREAM_READY ) {
			const void	*data;
			size_t		bytes;

			while ( pa.stream_peek( pa.stream, &data, &bytes ) == 0 && bytes > 0 ) {
				// (no data with a length: a hole in the stream, which is dropped)
				if ( data ) {
					const short	*in = (const short *)data;
					const int	count = (int)( bytes / ( 2 * sizeof( short ) ) );

					for ( i = 0; i < count; i++ ) {
						// (what does not fit is the oldest, which goes)
						if ( feed.write - feed.read >= FEED_RING )
							feed.read++;
						feed.ring[( feed.write % FEED_RING ) * 2] = in[i * 2];
						feed.ring[( feed.write % FEED_RING ) * 2 + 1] = in[i * 2 + 1];
						feed.write++;
					}
				}
				pa.stream_drop( pa.stream );
			}
		}
	}
	pa.threaded_mainloop_unlock( pa.loop );

	// the sound that came, a frame at a time
	while ( feed.write - feed.read >= VOICE_FRAME_SAMPLES ) {
		for ( i = 0; i < VOICE_FRAME_SAMPLES; i++ ) {
			frame[i * 2] = feed.ring[( feed.read % FEED_RING ) * 2];
			frame[i * 2 + 1] = feed.ring[( feed.read % FEED_RING ) * 2 + 1];
			feed.read++;
		}
		Feed_Send( frame, out );
		feed.soundFrames++;
		feed.lastSound = now;
	}
	// nothing came for a while: silence by the clock
	if ( now - feed.lastSound < FEED_QUIET_MSEC ) {
		feed.quietSince = now;
		feed.quietSent = 0;
		return;
	}
	due = ( now - feed.quietSince ) / VOICE_FRAME_MSEC - feed.quietSent;
	if ( due > FEED_BURST ) {
		feed.quietSent += due - FEED_BURST;
		due = FEED_BURST;
	}
	for ( ; due > 0; due-- ) {
		Feed_Send( NULL, out );
		feed.quietSent++;
		feed.quietFrames++;
	}
}

#else

qboolean CG_FeedSupported( void ) { return qfalse; }
qboolean CG_FeedStart( int pid, const char *key, int channels ) { (void)pid; (void)key; (void)channels; return qfalse; }
void CG_FeedStop( void ) {}
qboolean CG_FeedFound( void ) { return qfalse; }
void CG_FeedShutdown( void ) {}
void CG_FeedFrame( int now, feedOut_t out ) { (void)now; (void)out; }
void CG_FeedCounts( int *sound, int *quiet ) { *sound = *quiet = 0; }

#endif
