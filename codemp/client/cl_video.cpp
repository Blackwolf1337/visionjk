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

// cl_video.cpp -- videos the cgame shows on surfaces of the level: the screens of the TV of the boombox (issue #9,
// cgame/boombox). The cgame opens one of VIDEO_STREAMS streams on a video of YouTube from a second on
// (ext.Video_Open): a thread of its own fetches its picture from the TV of the masterserver (v_tvService,
// master/ms_tv.c) as MPEG-1 (an elementary stream, as ffmpeg writes it with -f mpeg1video), and the cgame says once
// a frame how far it is to play (ext.Video_Advance). It is decoded here with pl_mpeg (lib/pl_mpeg), and a shader
// stage "videoMap *tv<n>" shows stream n: for that name CIN_PlayCinematic (cl_cin.cpp) hands out a handle whose
// frames come from here, so both renderers show it as they show any cinematic. The sound is the song of the box.
//
// What comes is not trusted: a picture larger than VIDEO_MAX_WIDTH x VIDEO_MAX_HEIGHT is refused before pl_mpeg
// sees it, so is more data waiting than VIDEO_MAX_BUFFER, and one call decodes VIDEO_MAX_FRAMES frames at most, so
// the client never stalls on it. The thread calls nothing of the engine but Sys_HttpGet, and the game never waits
// for it: a stream opened anew lets the old thread go, which ends by itself (as the fetches of cl_models.cpp).

#include "client.h"

// Every block of pl_mpeg gets VIDEO_PAD zeroed bytes more than it asks for: with a broken motion vector at the bottom
// of a picture plm_video_process_macroblock() reads up to a line and a byte past the end of the reference frame
// (its check of the address leaves out the half pixel), which a test with damaged streams under AddressSanitizer
// found. Writes stay inside, so the reads only see these zeros.
#define VIDEO_PAD			4096
#define PLM_MALLOC( sz )		calloc( 1, (sz) + VIDEO_PAD )
#define PLM_REALLOC( p, sz )	realloc( (p), (sz) + VIDEO_PAD )
#define PLM_FREE( p )			free( p )
#define PLM_NO_STDIO
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg/pl_mpeg.h"

#define VIDEO_MAX_WIDTH		640
#define VIDEO_MAX_HEIGHT	360
#define VIDEO_MAX_BUFFER	( 4 * 1024 * 1024 )		// bytes fed and not decoded yet
#define VIDEO_MAX_FRAMES	12						// decoded in one call at most, it catches up over the next ones
#define VIDEO_JOBS			( VIDEO_STREAMS * 2 )	// threads that fetch, those that were let go and still end among them
#define VIDEO_JOB_BYTES		( 2 * 1024 * 1024 )		// what came and the game has not taken yet, at most
#define VIDEO_URL			256
#define VIDEO_SERVICE		"https://jka.vision/tv/"

typedef struct {
	plm_buffer_t	*buffer;
	plm_video_t		*video;
	byte			*rgba;					// the last frame decoded, width x height
	int				width, height;
	int				frameMsec;				// of that frame, -1: none yet
	qboolean		dirty;					// not uploaded since
	qboolean		broken;					// refused: everything is dropped until the next reset
	vec3_t			color;					// the mean colour of that frame, 0 to 1
	int				job;					// the fetch that feeds it, -1: none
	qboolean		fetched;				// that fetch is through and all of it was fed
	qboolean		failed;					// it failed before anything came
} videoStream_t;

static videoStream_t videoStreams[VIDEO_STREAMS];

static videoStream_t *CL_VideoStream( int stream ) {
	return stream >= 0 && stream < VIDEO_STREAMS ? &videoStreams[stream] : NULL;
}

/*
===============================================================================

The fetch

===============================================================================
*/

#if !defined( __EMSCRIPTEN__ )

#include <SDL.h>
#include "sys/sys_http.h"

enum {
	VIDEOJOB_FREE,
	VIDEOJOB_RUNNING,
	VIDEOJOB_DONE,			// through: the game reaps the thread
	VIDEOJOB_LETGO			// nobody wants it any more: the thread frees the slot itself at its end
};

typedef struct {
	SDL_atomic_t	state;
	SDL_Thread		*thread;
	SDL_mutex		*lock;					// over data, len and overflow
	volatile int	cancel;
	char			url[VIDEO_URL];
	byte			*data;					// VIDEO_JOB_BYTES, made the first time the slot is used
	int				len;
	qboolean		overflow;
	qboolean		ok;
	int				status;
	char			error[128];
} videoJob_t;

static videoJob_t videoJobs[VIDEO_JOBS];

// what came: behind what the game has not taken yet. More than fits ends the fetch.
static qboolean CL_VideoSink( void *ctx, const void *data, size_t len ) {
	videoJob_t	*job = (videoJob_t *)ctx;
	qboolean	room;

	if ( job->cancel )
		return qfalse;
	SDL_LockMutex( job->lock );
	room = (qboolean)( (size_t)job->len + len <= VIDEO_JOB_BYTES );
	if ( room ) {
		memcpy( job->data + job->len, data, len );
		job->len += (int)len;
	} else {
		job->overflow = qtrue;
	}
	SDL_UnlockMutex( job->lock );
	return room;
}

static int CL_VideoThread( void *data ) {
	videoJob_t *job = (videoJob_t *)data;

	job->ok = Sys_HttpGet( job->url, CL_VideoSink, job, &job->cancel, job->error, sizeof( job->error ), &job->status );
	if ( !SDL_AtomicCAS( &job->state, VIDEOJOB_RUNNING, VIDEOJOB_DONE ) )
		SDL_AtomicSet( &job->state, VIDEOJOB_FREE );
	return 0;
}

static void CL_VideoLetGo( int j ) {
	videoJob_t *job = &videoJobs[j];

	job->cancel = 1;
	if ( SDL_AtomicCAS( &job->state, VIDEOJOB_RUNNING, VIDEOJOB_LETGO ) ) {
		SDL_DetachThread( job->thread );
	} else if ( SDL_AtomicGet( &job->state ) == VIDEOJOB_DONE ) {
		SDL_WaitThread( job->thread, NULL );		// (it is through, this does not wait)
		SDL_AtomicSet( &job->state, VIDEOJOB_FREE );
	}
	job->thread = NULL;
}

// the stream gets what its fetch brought since the last frame
static void CL_VideoTake( videoStream_t *v, int stream ) {
	videoJob_t	*job;
	qboolean	done;

	if ( v->job < 0 )
		return;
	job = &videoJobs[v->job];
	done = (qboolean)( SDL_AtomicGet( &job->state ) == VIDEOJOB_DONE );
	SDL_LockMutex( job->lock );
	if ( job->len > 0 ) {
		CL_VideoFeed( stream, job->data, job->len );
		job->len = 0;
	}
	if ( job->overflow )
		v->broken = qtrue;
	SDL_UnlockMutex( job->lock );
	if ( done ) {
		if ( !job->ok && !v->video ) {
			v->failed = qtrue;
			Com_DPrintf( "TV: %s: %s\n", job->url, job->error );
		}
		v->fetched = qtrue;
		SDL_WaitThread( job->thread, NULL );
		job->thread = NULL;
		SDL_AtomicSet( &job->state, VIDEOJOB_FREE );
		v->job = -1;
		if ( v->buffer )
			plm_buffer_signal_end( v->buffer );
	}
}

/*
=================
CL_VideoOpen

The stream shows the picture of video (eleven characters of YouTube) from second on, fetched from v_tvService.
What it showed before is dropped. qfalse: nothing is fetched (no free thread now: the cgame asks again later).
=================
*/
qboolean CL_VideoOpen( int stream, const char *video, int second ) {
	videoStream_t	*v = CL_VideoStream( stream );
	videoJob_t		*job = NULL;
	char			service[VIDEO_URL];
	int				i;

	if ( !v || strlen( video ) != 11 || strspn( video, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_" ) != 11
		|| second < 0 || second > 36000 ) {
		return qfalse;
	}
	CL_VideoReset( stream );
	for ( i = 0; i < VIDEO_JOBS && !job; i++ ) {
		if ( SDL_AtomicGet( &videoJobs[i].state ) == VIDEOJOB_FREE ) {
			job = &videoJobs[i];
			v->job = i;
		}
	}
	if ( !job )
		return qfalse;
	if ( !job->lock && !( job->lock = SDL_CreateMutex() ) )
		return qfalse;
	if ( !job->data )
		job->data = (byte *)Z_Malloc( VIDEO_JOB_BYTES, TAG_CLIENTS, qfalse );
	Q_strncpyz( service, Cvar_VariableString( "v_tvService" ), sizeof( service ) );
	if ( Q_strncmp( service, "https://", 8 ) && Q_strncmp( service, "http://127.0.0.1", 16 ) )
		Q_strncpyz( service, VIDEO_SERVICE, sizeof( service ) );
	Com_sprintf( job->url, sizeof( job->url ), "%s%s%s?t=%i", service, service[strlen( service ) - 1] == '/' ? "" : "/",
		video, second );
	job->cancel = 0;
	job->len = 0;
	job->overflow = qfalse;
	job->ok = qfalse;
	job->status = 0;
	job->error[0] = '\0';
	SDL_AtomicSet( &job->state, VIDEOJOB_RUNNING );
	job->thread = SDL_CreateThread( CL_VideoThread, "video", job );
	if ( !job->thread ) {
		SDL_AtomicSet( &job->state, VIDEOJOB_FREE );
		v->job = -1;
		return qfalse;
	}
	return qtrue;
}

#else // __EMSCRIPTEN__: the TV of the site shows no TV pictures

static void CL_VideoLetGo( int j ) { (void)j; }
static void CL_VideoTake( videoStream_t *v, int stream ) { (void)v; (void)stream; }
qboolean CL_VideoOpen( int stream, const char *video, int second ) { (void)stream; (void)video; (void)second; return qfalse; }

#endif

/*
===============================================================================

The decoder

===============================================================================
*/

void CL_VideoReset( int stream ) {
	videoStream_t *v = CL_VideoStream( stream );

	if ( !v )
		return;
	if ( v->job >= 0 )
		CL_VideoLetGo( v->job );
	if ( v->video )
		plm_video_destroy( v->video );		// with its buffer
	if ( v->rgba )
		Z_Free( v->rgba );
	Com_Memset( v, 0, sizeof( *v ) );
	v->frameMsec = -1;
	v->job = -1;
}

void CL_VideoShutdown( void ) {
	int i;

	for ( i = 0; i < VIDEO_STREAMS; i++ )
		CL_VideoReset( i );
}

// qfalse: a sequence header in data names a picture this file does not take
static qboolean CL_VideoSizesFit( const byte *data, int len ) {
	int i;

	for ( i = 0; i + 7 <= len; i++ ) {
		if ( data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1 && data[i + 3] == 0xB3 ) {
			const int width = ( data[i + 4] << 4 ) | ( data[i + 5] >> 4 );
			const int height = ( ( data[i + 5] & 15 ) << 8 ) | data[i + 6];

			if ( width < 16 || height < 16 || width > VIDEO_MAX_WIDTH || height > VIDEO_MAX_HEIGHT )
				return qfalse;
		}
	}
	return qtrue;
}

// More of the video of a stream, as it came. qfalse: the stream refused it and drops everything until it is reset.
qboolean CL_VideoFeed( int stream, const byte *data, int len ) {
	videoStream_t *v = CL_VideoStream( stream );

	if ( !v || v->broken || len <= 0 )
		return qfalse;
	// (a header cut in two by the feeds is checked again below, when pl_mpeg has read it)
	if ( !CL_VideoSizesFit( data, len ) ) {
		v->broken = qtrue;
		return qfalse;
	}
	if ( !v->video ) {
		v->buffer = plm_buffer_create_with_capacity( 256 * 1024 );
		v->video = plm_video_create_with_buffer( v->buffer, TRUE );
		plm_video_set_no_delay( v->video, TRUE );
		v->frameMsec = -1;
	}
	if ( plm_buffer_get_remaining( v->buffer ) + (size_t)len > VIDEO_MAX_BUFFER ) {
		v->broken = qtrue;
		return qfalse;
	}
	plm_buffer_write( v->buffer, (uint8_t *)data, (size_t)len );
	return qtrue;
}

static void CL_VideoMeanColor( videoStream_t *v ) {
	const int	step = 7;		// every seventh pixel of every seventh line is enough for a light
	float		sum[3] = { 0.0f, 0.0f, 0.0f };
	int			x, y, count = 0;

	for ( y = 0; y < v->height; y += step ) {
		const byte *row = v->rgba + y * v->width * 4;

		for ( x = 0; x < v->width; x += step, count++ ) {
			sum[0] += row[x * 4];
			sum[1] += row[x * 4 + 1];
			sum[2] += row[x * 4 + 2];
		}
	}
	if ( count )
		VectorScale( sum, 1.0f / ( 255.0f * count ), v->color );
}

/*
=================
CL_VideoAdvance

The stream plays on to msec (from the second it was opened at): what came is fed, the frames up to there are
decoded, the last one is shown. Returns the time of the frame shown, -1 while there is none.
=================
*/
int CL_VideoAdvance( int stream, int msec ) {
	videoStream_t	*v = CL_VideoStream( stream );
	plm_frame_t		*frame = NULL;
	int				i;

	if ( !v )
		return -1;
	CL_VideoTake( v, stream );
	if ( !v->video || v->broken )
		return -1;
	for ( i = 0; i < VIDEO_MAX_FRAMES; i++ ) {
		plm_frame_t *next;

		if ( !plm_video_has_header( v->video ) )
			break;
		if ( plm_video_get_width( v->video ) > VIDEO_MAX_WIDTH || plm_video_get_height( v->video ) > VIDEO_MAX_HEIGHT ) {
			v->broken = qtrue;
			return -1;
		}
		if ( plm_video_get_time( v->video ) * 1000.0 > (double)msec )
			break;
		next = plm_video_decode( v->video );
		if ( !next )
			break;
		frame = next;
	}
	if ( !frame )
		return v->frameMsec;
	if ( !v->rgba || v->width != (int)frame->width || v->height != (int)frame->height ) {
		if ( v->rgba )
			Z_Free( v->rgba );
		v->width = frame->width;
		v->height = frame->height;
		v->rgba = (byte *)Z_Malloc( v->width * v->height * 4, TAG_CLIENTS, qfalse );
		for ( i = 3; i < v->width * v->height * 4; i += 4 )
			v->rgba[i] = 255;
	}
	plm_frame_to_rgba( frame, v->rgba, v->width * 4 );
	v->frameMsec = (int)( frame->time * 1000.0 );
	v->dirty = qtrue;
	CL_VideoMeanColor( v );
	return v->frameMsec;
}

/*
=================
CL_VideoState

What a stream does: VIDEO_NONE, VIDEO_FETCHING (nothing to show yet), VIDEO_PLAYING, VIDEO_ENDED (all of it
came and was shown), VIDEO_FAILED (nothing came, or it was refused).
=================
*/
int CL_VideoState( int stream ) {
	const videoStream_t *v = CL_VideoStream( stream );

	if ( !v || ( v->job < 0 && !v->video && !v->failed ) )
		return VIDEO_NONE;
	if ( v->failed || v->broken )
		return VIDEO_FAILED;
	if ( v->fetched && v->video && plm_video_has_ended( v->video ) )
		return VIDEO_ENDED;
	return v->rgba ? VIDEO_PLAYING : VIDEO_FETCHING;
}

// qfalse: no frame yet
qboolean CL_VideoColor( int stream, vec3_t out ) {
	const videoStream_t *v = CL_VideoStream( stream );

	if ( !v || !v->rgba || v->broken ) {
		VectorClear( out );
		return qfalse;
	}
	VectorCopy( v->color, out );
	return qtrue;
}

// for a "videoMap *tv<n>" stage, from CIN_UploadCinematic: black until the first frame
void CL_VideoUpload( int stream, int handle ) {
	static const byte	black[4 * 4 * 4] = { 0 };
	videoStream_t		*v = CL_VideoStream( stream );

	if ( !v )
		return;
	if ( !v->rgba || v->broken ) {
		re->UploadCinematic( 4, 4, black, handle, qtrue );
		return;
	}
	re->UploadCinematic( v->width, v->height, v->rgba, handle, v->dirty );
	v->dirty = qfalse;
}
