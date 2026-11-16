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

"Hear it from the box" (CG_FeedRoute): the player is then moved to a sink of the game's own, "VisionJK Sync", made
through libpipewire (loaded at run time as well) and heard only from the box. That sink is a node of this
process: when the game ends, or breaks off, PipeWire takes it away and the player goes back to an output that is
there by itself. Muting the player can not do the same: PipeWire mutes inside its stream, before anybody records
it. Without PipeWire (PulseAudio alone) the player is not moved.

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

typedef void ( *paSuccessCb_t )( void *context, int success, void *user );

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
	void		*( *context_move_sink_input_by_name )( void *c, uint32_t input, const char *sink, paSuccessCb_t cb, void *user );
	void		*( *context_move_sink_input_by_index )( void *c, uint32_t input, uint32_t sink, paSuccessCb_t cb, void *user );
	void		*moveOp;		// a move of the player to the sink of the game or back
} pa;

/*
===============================================================================

The sink of the game (libpipewire): "Hear it from the box"

What is used of libpipewire and SPA, declared here so the game builds without their headers. The layouts are the
ones of pipewire/stream.h, spa/buffer/buffer.h and spa/param/audio/raw.h (checked against them on 64 bit; the
pointers keep them right on 32 bit).

===============================================================================
*/

#define PW_SINK_NAME			"visionjk_sync"
#define PW_DIRECTION_INPUT		0
#define PW_ID_ANY				0xffffffffu
#define PW_STREAM_FLAG_MAP_BUFFERS	4
#define FEED_SINK_RING			( VOICE_RATE / 2 )	// stereo samples the sink keeps until a frame takes them

typedef struct spaChunk_s { uint32_t offset, size; int32_t stride, flags; } spaChunk_t;
typedef struct spaData_s {
	uint32_t	type, flags;
	int64_t		fd;
	uint32_t	mapoffset, maxsize;
	void		*data;
	spaChunk_t	*chunk;
} spaData_t;
typedef struct spaBuffer_s { uint32_t n_metas, n_datas; void *metas; spaData_t *datas; } spaBuffer_t;
typedef struct pwBuffer_s { spaBuffer_t *buffer; } pwBuffer_t;		// (more after it, which is not used)
typedef struct pwStreamEvents_s {
	uint32_t	version;
	void		( *destroy )( void *data );
	void		( *state_changed )( void *data, int old, int state, const char *error );
	void		( *control_info )( void *data, uint32_t id, const void *control );
	void		( *io_changed )( void *data, uint32_t id, void *area, uint32_t size );
	void		( *param_changed )( void *data, uint32_t id, const void *param );
	void		( *add_buffer )( void *data, pwBuffer_t *buffer );
	void		( *remove_buffer )( void *data, pwBuffer_t *buffer );
	void		( *process )( void *data );
	void		( *drained )( void *data );
	void		( *command )( void *data, const void *command );
	void		( *trigger_done )( void *data );
} pwStreamEvents_t;

// The format the sink offers: SPA_PARAM_EnumFormat, audio, raw, S16LE, 48000 Hz, two channels FL FR. The bytes
// spa_format_audio_raw_build() makes of it (a SPA POD object, in the byte order of the machine).
static const uint32_t pwSinkFormat[] = {
	0x000000a0, 0x0000000f, 0x00040003, 0x00000003,
	0x00000001, 0x00000000, 0x00000004, 0x00000003, 0x00000001, 0x00000000,		// mediaType audio
	0x00000002, 0x00000000, 0x00000004, 0x00000003, 0x00000001, 0x00000000,		// mediaSubtype raw
	0x00010001, 0x00000000, 0x00000004, 0x00000003, 0x00000103, 0x00000000,		// format S16LE
	0x00010003, 0x00000000, 0x00000004, 0x00000004, 0x0000bb80, 0x00000000,		// rate 48000
	0x00010004, 0x00000000, 0x00000004, 0x00000004, 0x00000002, 0x00000000,		// channels 2
	0x00010005, 0x00000000, 0x00000010, 0x0000000d, 0x00000004, 0x00000003,		// position: an array of ids,
	0x00000003, 0x00000004														//	FL FR
};

static struct {
	void		*library;
	qboolean	failed;			// no PipeWire: the player is never moved
	qboolean	inited;
	void		*loop, *stream;
	int			state;			// of the stream, PW_STREAM_STATE_* (2 and up: the sound server has the node)
	short		ring[FEED_SINK_RING * 2];
	unsigned	read, write;	// stereo samples, written in the thread of the loop, under its lock

	void		( *init )( int *argc, char ***argv );
	void		*( *thread_loop_new )( const char *name, const void *props );
	void		*( *thread_loop_get_loop )( void *loop );
	int			( *thread_loop_start )( void *loop );
	void		( *thread_loop_stop )( void *loop );
	void		( *thread_loop_destroy )( void *loop );
	void		( *thread_loop_lock )( void *loop );
	void		( *thread_loop_unlock )( void *loop );
	void		*( *properties_new )( const char *key, ... );
	void		*( *stream_new_simple )( void *loop, const char *name, void *props, const pwStreamEvents_t *events, void *data );
	int			( *stream_connect )( void *stream, int direction, uint32_t target, int flags, const void **params, uint32_t numParams );
	pwBuffer_t	*( *stream_dequeue_buffer )( void *stream );
	int			( *stream_queue_buffer )( void *stream, pwBuffer_t *buffer );
	void		( *stream_destroy )( void *stream );
} pw;

static void Feed_SinkState( void *data, int old, int state, const char *error ) {
	(void)data;
	(void)old;
	(void)error;
	pw.state = state;
}

// the thread of the loop: what the player played into the sink, into the ring
static void Feed_SinkProcess( void *data ) {
	pwBuffer_t	*b;
	(void)data;

	while ( ( b = pw.stream_dequeue_buffer( pw.stream ) ) != NULL ) {
		const spaData_t *d = b->buffer->n_datas ? &b->buffer->datas[0] : NULL;

		if ( d && d->data && d->chunk ) {
			const uint32_t	offset = d->chunk->offset < d->maxsize ? d->chunk->offset : d->maxsize;
			const uint32_t	size = ( d->chunk->size < d->maxsize - offset ) ? d->chunk->size : d->maxsize - offset;
			const short		*in = (const short *)( (const byte *)d->data + offset );
			const int		count = (int)( size / ( 2 * sizeof( short ) ) );
			int				i;

			for ( i = 0; i < count; i++ ) {
				if ( pw.write - pw.read >= FEED_SINK_RING )
					pw.read++;
				pw.ring[( pw.write % FEED_SINK_RING ) * 2] = in[i * 2];
				pw.ring[( pw.write % FEED_SINK_RING ) * 2 + 1] = in[i * 2 + 1];
				pw.write++;
			}
		}
		pw.stream_queue_buffer( pw.stream, b );
	}
}

static const pwStreamEvents_t pwSinkEvents = { 2, NULL, Feed_SinkState, NULL, NULL, NULL, NULL, NULL, Feed_SinkProcess, NULL, NULL, NULL };

static qboolean Feed_PwOpen( void ) {
	if ( pw.failed )
		return qfalse;
	if ( pw.inited )
		return qtrue;

#define PW_SYMBOL( name ) \
	*(void **)&pw.name = dlsym( pw.library, "pw_" #name ); \
	if ( !pw.name ) { pw.failed = qtrue; return qfalse; }

	// (it stays loaded: the library keeps threads and state of its own)
	pw.library = dlopen( "libpipewire-0.3.so.0", RTLD_NOW | RTLD_LOCAL );
	if ( !pw.library ) {
		pw.failed = qtrue;
		return qfalse;
	}
	PW_SYMBOL( init )
	PW_SYMBOL( thread_loop_new )
	PW_SYMBOL( thread_loop_get_loop )
	PW_SYMBOL( thread_loop_start )
	PW_SYMBOL( thread_loop_stop )
	PW_SYMBOL( thread_loop_destroy )
	PW_SYMBOL( thread_loop_lock )
	PW_SYMBOL( thread_loop_unlock )
	PW_SYMBOL( properties_new )
	PW_SYMBOL( stream_new_simple )
	PW_SYMBOL( stream_connect )
	PW_SYMBOL( stream_dequeue_buffer )
	PW_SYMBOL( stream_queue_buffer )
	PW_SYMBOL( stream_destroy )
#undef PW_SYMBOL
	pw.init( NULL, NULL );
	pw.inited = qtrue;
	return qtrue;
}

// The sink of the game, if there is none yet. It never becomes the output of anything by itself: the lowest
// priority, and nothing is linked to it but the player this file moves there. qfalse: no PipeWire.
static qboolean Feed_SinkStart( void ) {
	const void	*params[1] = { pwSinkFormat };
	void		*props;

	if ( pw.stream )
		return qtrue;
	if ( !Feed_PwOpen() )
		return qfalse;
	pw.loop = pw.thread_loop_new( "visionjk-sync", NULL );
	if ( !pw.loop )
		return qfalse;
	props = pw.properties_new( "media.type", "Audio", "media.category", "Capture", "media.class", "Audio/Sink",
		"node.name", PW_SINK_NAME, "node.description", "VisionJK Sync", "node.virtual", "true", "node.want-driver", "true",
		"priority.session", "0", "priority.driver", "0", "audio.position", "FL,FR", (const char *)NULL );
	pw.read = pw.write = 0;
	pw.state = 0;
	pw.thread_loop_lock( pw.loop );
	pw.stream = props ? pw.stream_new_simple( pw.thread_loop_get_loop( pw.loop ), "VisionJK Sync", props, &pwSinkEvents, NULL ) : NULL;
	if ( pw.stream && pw.stream_connect( pw.stream, PW_DIRECTION_INPUT, PW_ID_ANY, PW_STREAM_FLAG_MAP_BUFFERS, params, 1 ) < 0 ) {
		pw.stream_destroy( pw.stream );
		pw.stream = NULL;
	}
	pw.thread_loop_unlock( pw.loop );
	if ( !pw.stream || pw.thread_loop_start( pw.loop ) < 0 ) {
		if ( pw.stream ) {
			pw.stream_destroy( pw.stream );
			pw.stream = NULL;
		}
		pw.thread_loop_destroy( pw.loop );
		pw.loop = NULL;
		return qfalse;
	}
	return qtrue;
}

// The sink goes. A player still on it is moved by PipeWire to an output that is there by itself.
static void Feed_SinkStop( void ) {
	if ( !pw.loop )
		return;
	pw.thread_loop_lock( pw.loop );
	if ( pw.stream ) {
		pw.stream_destroy( pw.stream );
		pw.stream = NULL;
	}
	pw.thread_loop_unlock( pw.loop );
	pw.thread_loop_stop( pw.loop );
	pw.thread_loop_destroy( pw.loop );
	pw.loop = NULL;
}

// what the last look through the sink inputs found (written in the thread of the library, under its lock)
typedef struct feedCandidate_s {
	uint32_t	index;
	uint32_t	client;			// of the sound server: a program of PipeWire itself says its process only there
	uint32_t	sink;			// where it plays now
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
	qboolean		route;				// "Hear it from the box": the player plays into the sink of the game,
	uint32_t		routed;				//	this sink input of it does, -1: none,
	uint32_t		routedFrom;			//	and this is where it played before, -1: not known (the default then)
	uint32_t		ownSink;			// the sink of the game as the sound server numbers it, -1: not known yet
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
	int				peak;				// the loudest sample of the last second of sound, 0 to 32767
	int				peakTime;
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
		if ( pa.moveOp ) {
			pa.operation_unref( pa.moveOp );
			pa.moveOp = NULL;
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
	Feed_SinkStop();
	feed.routed = feed.ownSink = (uint32_t)-1;
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
	FEED_SYMBOL( context_move_sink_input_by_name )
	FEED_SYMBOL( context_move_sink_input_by_index )
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
	feed.input = feed.routed = feed.ownSink = (uint32_t)-1;
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
	c->sink = info->sink;
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

// Records the stream of that sink input from now on (the one before ends). monitor: with a record stream of the
// sound server; without one, its sound comes from the sink of the game, where it plays. Under the lock.
static void Feed_Record( uint32_t input, qboolean monitor ) {
	const paSpec_t	spec = { PA_SAMPLE_S16LE, VOICE_RATE, 2 };
	paAttr_t		attr;

	if ( pa.stream ) {
		pa.stream_disconnect( pa.stream );
		pa.stream_unref( pa.stream );
		pa.stream = NULL;
	}
	feed.input = input;
	if ( input == (uint32_t)-1 || !monitor )
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

// the thread of the library: a move ended (success 0: that sink input or sink is gone, tried again with a look)
static void Feed_Moved( void *context, int success, void *user ) {
	(void)context;
	(void)success;
	(void)user;
}

static const feedCandidate_t *Feed_Found( uint32_t input ) {
	int i;

	for ( i = 0; i < feed.numFound; i++ ) {
		if ( feed.found[i].index == input )
			return &feed.found[i];
	}
	return NULL;
}

// The player goes back to where it played before it was moved to the sink of the game, or to the default output.
// Under the lock. The move is not waited for.
static void Feed_Unroute( void ) {
	if ( feed.routed == (uint32_t)-1 )
		return;
	if ( pa.moveOp )
		pa.operation_unref( pa.moveOp );
	pa.moveOp = ( feed.routedFrom != (uint32_t)-1 )
		? pa.context_move_sink_input_by_index( pa.context, feed.routed, feed.routedFrom, Feed_Moved, NULL )
		: pa.context_move_sink_input_by_name( pa.context, feed.routed, "@DEFAULT_SINK@", Feed_Moved, NULL );
	feed.routed = (uint32_t)-1;
}

/*
=================
Feed_Route

After a look through the sink inputs, under the lock: where the player should play. "Hear it from the box": in
the sink of the game, which is made for it, and its sound is taken from there; else where it was, recorded with a
monitor stream. A player whose stream comes anew (the next song of some players) is moved again.
=================
*/
static void Feed_Route( uint32_t input ) {
	const feedCandidate_t	*c = ( input != (uint32_t)-1 ) ? Feed_Found( input ) : NULL;
	qboolean				onOwn;

	if ( !feed.route || input == (uint32_t)-1 || !Feed_SinkStart() ) {
		Feed_Unroute();
		// (the sink goes once the player is back where it was: else PipeWire would move it first, to the default)
		if ( pw.stream && !feed.route && ( !pa.moveOp || pa.operation_get_state( pa.moveOp ) != PA_OPERATION_RUNNING ) )
			Feed_SinkStop();
		if ( input != feed.input || !pa.stream || pa.stream_get_state( pa.stream ) >= PA_STREAM_FAILED )
			Feed_Record( input, qtrue );
		return;
	}
	// (the number of the sink of the game: where the player plays that was moved there)
	if ( feed.routed == input && c && ( !pa.moveOp || pa.operation_get_state( pa.moveOp ) != PA_OPERATION_RUNNING ) )
		feed.ownSink = c->sink;
	onOwn = ( c && feed.ownSink != (uint32_t)-1 && c->sink == feed.ownSink ) ? qtrue : qfalse;
	if ( feed.routed != input || !onOwn ) {
		// a move on its way is waited for, one that failed is tried again with the next look
		if ( pa.moveOp && pa.operation_get_state( pa.moveOp ) == PA_OPERATION_RUNNING )
			return;
		if ( pa.moveOp ) {
			pa.operation_unref( pa.moveOp );
			pa.moveOp = NULL;
		}
		if ( pw.state < 2 )
			return;		// (the sink is not there for the sound server yet: PW_STREAM_STATE_PAUSED or STREAMING)
		if ( feed.routed != input )
			feed.routedFrom = ( c && !onOwn ) ? c->sink : (uint32_t)-1;
		feed.routed = input;
		pa.moveOp = pa.context_move_sink_input_by_name( pa.context, input, PW_SINK_NAME, Feed_Moved, NULL );
	}
	// its sound comes from the sink now, the monitor stream ends
	if ( feed.input != input || pa.stream )
		Feed_Record( input, qfalse );
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
	Feed_Record( (uint32_t)-1, qtrue );
	pa.threaded_mainloop_unlock( pa.loop );
	return qtrue;
}

// no more: the player goes back where it played, the stream ends, the connection to the sound server stays
void CG_FeedStop( void ) {
	feed.on = qfalse;
	feed.route = qfalse;
	if ( pa.loop ) {
		pa.threaded_mainloop_lock( pa.loop );
		Feed_Unroute();
		Feed_Record( (uint32_t)-1, qtrue );
		pa.threaded_mainloop_unlock( pa.loop );
	}
	Feed_SinkStop();
}

// the player was started again: another process (the next look finds its stream by it)
void CG_FeedPid( int pid ) {
	if ( pid > 0 )
		feed.pid = pid;
}

// "Hear it from the box": the player plays into the sink of the game (with the next look), or back where it was
void CG_FeedRoute( qboolean onlyInGame ) {
	feed.route = onlyInGame;
}

// the player plays into the sink of the game now
qboolean CG_FeedRouted( void ) {
	return ( feed.on && feed.routed != (uint32_t)-1 && feed.routed == feed.input && pw.stream ) ? qtrue : qfalse;
}

// the sound of the player is found and recorded
qboolean CG_FeedFound( void ) {
	return ( feed.on && feed.input != (uint32_t)-1 ) ? qtrue : qfalse;
}

// The module goes. A player in the sink of the game is sent back (not waited for); if that does not reach the
// sound server before the connection ends, PipeWire moves it when the sink goes.
void CG_FeedShutdown( void ) {
	feed.on = qfalse;
	if ( pa.loop ) {
		pa.threaded_mainloop_lock( pa.loop );
		if ( pa.context && pa.context_get_state( pa.context ) == PA_CONTEXT_READY )
			Feed_Unroute();
		pa.threaded_mainloop_unlock( pa.loop );
	}
	Feed_Close( qfalse );
}

// for v_voice_status: frames of sound and of silence that went out of this sync
void CG_FeedCounts( int *sound, int *quiet, int *peak ) {
	*sound = feed.soundFrames;
	*quiet = feed.quietFrames;
	*peak = ( trap->Milliseconds() - feed.peakTime < 2000 ) ? feed.peak : 0;
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
			if ( feed.looked )
				Feed_Route( Feed_Pick() );
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

	// what the player played into the sink of the game
	if ( pw.loop ) {
		pw.thread_loop_lock( pw.loop );
		while ( pw.read != pw.write ) {
			if ( feed.write - feed.read >= FEED_RING )
				feed.read++;
			feed.ring[( feed.write % FEED_RING ) * 2] = pw.ring[( pw.read % FEED_SINK_RING ) * 2];
			feed.ring[( feed.write % FEED_RING ) * 2 + 1] = pw.ring[( pw.read % FEED_SINK_RING ) * 2 + 1];
			feed.write++;
			pw.read++;
		}
		pw.thread_loop_unlock( pw.loop );
	}

	// the sound that came, a frame at a time
	while ( feed.write - feed.read >= VOICE_FRAME_SAMPLES ) {
		for ( i = 0; i < VOICE_FRAME_SAMPLES; i++ ) {
			frame[i * 2] = feed.ring[( feed.read % FEED_RING ) * 2];
			frame[i * 2 + 1] = feed.ring[( feed.read % FEED_RING ) * 2 + 1];
			feed.read++;
		}
		for ( i = 0; i < VOICE_FRAME_SAMPLES * 2; i++ ) {
			const int v = abs( frame[i] );

			if ( v > feed.peak || now - feed.peakTime > 1000 ) {
				feed.peak = v;
				feed.peakTime = now;
			}
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
void CG_FeedCounts( int *sound, int *quiet, int *peak ) { *sound = *quiet = *peak = 0; }
void CG_FeedRoute( qboolean onlyInGame ) { (void)onlyInGame; }
void CG_FeedPid( int pid ) { (void)pid; }
qboolean CG_FeedRouted( void ) { return qfalse; }

#endif
