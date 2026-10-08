/*
===========================================================================
Copyright (C) 2013 - 2015, OpenJK contributors
Copyright (C) 2026, VisionJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
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

// cg_voice_audio.c -- the microphone and the speakers of the voice chat. See cg_voice.h.
//
// The engine gives a module no way to record sound and none to play samples it made itself, so the cgame
// opens the sound devices of the system on its own, through miniaudio (lib/miniaudio), which is compiled
// into this one file. The voices do not go through the mixer of the game: s_volume does not change them,
// cg_voiceVolume does.
//
// miniaudio calls Audio_Play() and Audio_Record() from threads of its own. Between them and the game there are
// only ring buffers with one writer and one reader each, and a few numbers that are set as a whole.

#ifdef JP_VOICE

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "qcommon/q_shared.h"
#include "game/voice/bg_voice.h"
#include "cg_voice.h"

#define AUDIO_SPEAKER_SAMPLES	( VOICE_RATE / 2 )		// what waits to be played of one voice at most: half a second
#define AUDIO_CAPTURE_SAMPLES	VOICE_RATE				// what waits of the microphone at most: a second
#define AUDIO_TAP_FRAMES		VOICE_RATE
#define AUDIO_CHUNK				256						// samples of one voice the mixer takes at a time
#define AUDIO_GAIN_SLEW			( 96.0f / VOICE_RATE )	// how fast the loudness of a voice follows what was set, per sample: all the way in about 10 ms

typedef struct audioSpeaker_s {
	ma_pcm_rb		ring;
	ma_atomic_float	left, right, muffle;	// set by the game: how loud on each side, 1 behind a wall
	float			nowLeft, nowRight;		// of the mixer: where the loudness is on its way there
	voiceFilter_t	filter;					// of the mixer
} audioSpeaker_t;

static ma_context		audioContext;
static ma_device		audioPlayback, audioCapture;
static qboolean			audioReady, audioPlaying, audioRecording;
static audioSpeaker_t	audioSpeakers[VOICE_SPEAKERS];
static ma_pcm_rb		audioMic;
static ma_pcm_rb		audioTap;
static ma_atomic_bool32	audioTapOn;

static short			audioSpeakerMem[VOICE_SPEAKERS][AUDIO_SPEAKER_SAMPLES];
static short			audioMicMem[AUDIO_CAPTURE_SAMPLES];
static short			audioTapMem[AUDIO_TAP_FRAMES * 2];

// count samples out of a ring, fewer if it has fewer. A ring hands out its memory in up to two pieces.
static int Audio_RingRead( ma_pcm_rb *ring, short *out, int count ) {
	int done = 0, pass;

	for ( pass = 0; pass < 2 && done < count; pass++ ) {
		ma_uint32	n = (ma_uint32)( count - done );
		void		*from;

		if ( ma_pcm_rb_acquire_read( ring, &n, &from ) != MA_SUCCESS || !n )
			break;
		memcpy( out + done * ring->channels, from, (size_t)n * ring->channels * sizeof( short ) );
		ma_pcm_rb_commit_read( ring, n );
		done += (int)n;
	}
	return done;
}

static int Audio_RingWrite( ma_pcm_rb *ring, const short *in, int count ) {
	int done = 0, pass;

	for ( pass = 0; pass < 2 && done < count; pass++ ) {
		ma_uint32	n = (ma_uint32)( count - done );
		void		*to;

		if ( ma_pcm_rb_acquire_write( ring, &n, &to ) != MA_SUCCESS || !n )
			break;
		memcpy( to, in + done * ring->channels, (size_t)n * ring->channels * sizeof( short ) );
		ma_pcm_rb_commit_write( ring, n );
		done += (int)n;
	}
	return done;
}

/*
=================
Audio_Play

Called by miniaudio when the speakers want sound: two channels of floats, VOICE_RATE a second (miniaudio turns
that into what the device takes). The buffer comes in silent, every voice that has something waiting is added:
through its low-pass, at the loudness the game set for each side.
=================
*/
static void Audio_Play( ma_device *device, void *output, const void *input, ma_uint32 frames ) {
	float	*out = (float *)output;
	int		s;

	(void)device;
	(void)input;

	for ( s = 0; s < VOICE_SPEAKERS; s++ ) {
		audioSpeaker_t	*sp = &audioSpeakers[s];
		const float		left = ma_atomic_float_get( &sp->left ), right = ma_atomic_float_get( &sp->right );
		const float		muffle = ma_atomic_float_get( &sp->muffle );
		ma_uint32		done = 0;

		while ( done < frames ) {
			short		raw[AUDIO_CHUNK];
			float		mono[AUDIO_CHUNK];
			const int	want = ( frames - done < AUDIO_CHUNK ) ? (int)( frames - done ) : AUDIO_CHUNK;
			const int	n = Audio_RingRead( &sp->ring, raw, want );
			int			i;

			if ( !n )
				break;
			for ( i = 0; i < n; i++ )
				mono[i] = raw[i] * ( 1.0f / 32768.0f );
			BG_VoiceMuffle( &sp->filter, mono, n, muffle );
			for ( i = 0; i < n; i++ ) {
				// the loudness follows the game in small steps, a jump would be heard as a click
				sp->nowLeft += ( left - sp->nowLeft ) * AUDIO_GAIN_SLEW;
				sp->nowRight += ( right - sp->nowRight ) * AUDIO_GAIN_SLEW;
				out[( done + i ) * 2] += mono[i] * sp->nowLeft;
				out[( done + i ) * 2 + 1] += mono[i] * sp->nowRight;
			}
			done += (ma_uint32)n;
		}
	}

	// several loud voices at once must not wrap around
	for ( s = 0; s < (int)frames * 2; s++ ) {
		if ( out[s] > 1.0f )
			out[s] = 1.0f;
		else if ( out[s] < -1.0f )
			out[s] = -1.0f;
	}

	// what is played, for the test that writes it to a file
	if ( ma_atomic_bool32_get( &audioTapOn ) ) {
		ma_uint32 done = 0;

		while ( done < frames ) {
			short		stereo[AUDIO_CHUNK * 2];
			const int	n = ( frames - done < AUDIO_CHUNK ) ? (int)( frames - done ) : AUDIO_CHUNK;
			int			i;

			for ( i = 0; i < n * 2; i++ )
				stereo[i] = (short)( out[done * 2 + i] * 32767.0f );
			Audio_RingWrite( &audioTap, stereo, n );
			done += (ma_uint32)n;
		}
	}
}

// called by miniaudio with what the microphone recorded: 16 bit, one channel, VOICE_RATE a second
static void Audio_Record( ma_device *device, void *output, const void *input, ma_uint32 frames ) {
	(void)device;
	(void)output;
	// a full ring means the game does not take the sound right now, the newest is dropped
	Audio_RingWrite( &audioMic, (const short *)input, (int)frames );
}

/*
=================
CG_VoiceAudioInit

backend: "" for the sound system of the machine, "null" for one that plays into nothing and records silence
(for tests on a machine without sound). Returns 0 if there is no sound system.
=================
*/
int CG_VoiceAudioInit( const char *backend ) {
	const ma_backend	none[] = { ma_backend_null };
	int					s;

	if ( audioReady )
		return 1;
	if ( backend && !strcmp( backend, "null" ) ) {
		if ( ma_context_init( none, 1, NULL, &audioContext ) != MA_SUCCESS )
			return 0;
	} else if ( ma_context_init( NULL, 0, NULL, &audioContext ) != MA_SUCCESS ) {
		return 0;
	}

	for ( s = 0; s < VOICE_SPEAKERS; s++ ) {
		memset( &audioSpeakers[s], 0, sizeof( audioSpeakers[s] ) );
		ma_pcm_rb_init( ma_format_s16, 1, AUDIO_SPEAKER_SAMPLES, audioSpeakerMem[s], NULL, &audioSpeakers[s].ring );
	}
	ma_pcm_rb_init( ma_format_s16, 1, AUDIO_CAPTURE_SAMPLES, audioMicMem, NULL, &audioMic );
	ma_pcm_rb_init( ma_format_s16, 2, AUDIO_TAP_FRAMES, audioTapMem, NULL, &audioTap );
	ma_atomic_bool32_set( &audioTapOn, MA_FALSE );
	audioReady = qtrue;
	return 1;
}

// stops the devices and with them the threads of miniaudio. After this nothing of this file runs any more,
// which is what has to hold before the module is unloaded.
void CG_VoiceAudioShutdown( void ) {
	if ( !audioReady )
		return;
	CG_VoiceAudioCloseCapture();
	if ( audioPlaying ) {
		ma_device_uninit( &audioPlayback );
		audioPlaying = qfalse;
	}
	ma_context_uninit( &audioContext );
	audioReady = qfalse;
}

// the names of the devices there are, for the player to choose from. Returns how many.
int CG_VoiceAudioDevices( int capture, char names[][VOICE_DEVICE_NAME], int max ) {
	ma_device_info	*play, *record;
	ma_uint32		numPlay, numRecord, i, count;

	if ( !audioReady || ma_context_get_devices( &audioContext, &play, &numPlay, &record, &numRecord ) != MA_SUCCESS )
		return 0;
	count = capture ? numRecord : numPlay;
	if ( count > (ma_uint32)max )
		count = (ma_uint32)max;
	for ( i = 0; i < count; i++ ) {
		strncpy( names[i], capture ? record[i].name : play[i].name, VOICE_DEVICE_NAME - 1 );
		names[i][VOICE_DEVICE_NAME - 1] = '\0';
	}
	return (int)count;
}

// the device of that name, NULL (the one the system uses by default) if the name is empty or not there
static const ma_device_id *Audio_FindDevice( int capture, const char *name ) {
	ma_device_info	*play, *record, *list;
	ma_uint32		numPlay, numRecord, i, count;

	if ( !name || !name[0] || ma_context_get_devices( &audioContext, &play, &numPlay, &record, &numRecord ) != MA_SUCCESS )
		return NULL;
	list = capture ? record : play;
	count = capture ? numRecord : numPlay;
	for ( i = 0; i < count; i++ ) {
		if ( !strcmp( list[i].name, name ) )
			return &list[i].id;
	}
	return NULL;
}

int CG_VoiceAudioOpenPlayback( const char *device ) {
	ma_device_config config;

	if ( !audioReady )
		return 0;
	if ( audioPlaying )
		return 1;
	config = ma_device_config_init( ma_device_type_playback );
	config.playback.pDeviceID = Audio_FindDevice( 0, device );
	config.playback.format = ma_format_f32;
	config.playback.channels = 2;
	config.sampleRate = VOICE_RATE;
	config.periodSizeInMilliseconds = 10;
	config.dataCallback = Audio_Play;
	if ( ma_device_init( &audioContext, &config, &audioPlayback ) != MA_SUCCESS )
		return 0;
	if ( ma_device_start( &audioPlayback ) != MA_SUCCESS ) {
		ma_device_uninit( &audioPlayback );
		return 0;
	}
	audioPlaying = qtrue;
	return 1;
}

int CG_VoiceAudioOpenCapture( const char *device ) {
	ma_device_config config;

	if ( !audioReady )
		return 0;
	if ( audioRecording )
		return 1;
	ma_pcm_rb_reset( &audioMic );
	config = ma_device_config_init( ma_device_type_capture );
	config.capture.pDeviceID = Audio_FindDevice( 1, device );
	config.capture.format = ma_format_s16;
	config.capture.channels = 1;
	config.sampleRate = VOICE_RATE;
	config.periodSizeInMilliseconds = 10;
	config.dataCallback = Audio_Record;
	if ( ma_device_init( &audioContext, &config, &audioCapture ) != MA_SUCCESS )
		return 0;
	if ( ma_device_start( &audioCapture ) != MA_SUCCESS ) {
		ma_device_uninit( &audioCapture );
		return 0;
	}
	audioRecording = qtrue;
	return 1;
}

void CG_VoiceAudioCloseCapture( void ) {
	if ( audioRecording ) {
		ma_device_uninit( &audioCapture );
		audioRecording = qfalse;
	}
}

int CG_VoiceAudioCapturing( void ) {
	return audioRecording ? 1 : 0;
}

// count samples of the microphone, or none: a frame is taken whole or not at all. Returns count or 0.
int CG_VoiceAudioRead( short *samples, int count ) {
	if ( !audioRecording || (int)ma_pcm_rb_available_read( &audioMic ) < count )
		return 0;
	return Audio_RingRead( &audioMic, samples, count );
}

// what the microphone recorded while nobody wanted it
void CG_VoiceAudioDropCapture( void ) {
	short	scrap[AUDIO_CHUNK];

	while ( audioRecording && Audio_RingRead( &audioMic, scrap, AUDIO_CHUNK ) == AUDIO_CHUNK ) {
	}
}

// samples of that voice that wait to be played
int CG_VoiceAudioQueued( int speaker ) {
	if ( !audioReady || speaker < 0 || speaker >= VOICE_SPEAKERS )
		return 0;
	return (int)ma_pcm_rb_available_read( &audioSpeakers[speaker].ring );
}

// more of that voice. Returns how many samples were taken, fewer if its half second is full.
int CG_VoiceAudioWrite( int speaker, const short *samples, int count ) {
	if ( !audioReady || speaker < 0 || speaker >= VOICE_SPEAKERS )
		return 0;
	return Audio_RingWrite( &audioSpeakers[speaker].ring, samples, count );
}

// how that voice is heard from now on: loudness on each side, and 1 for behind a wall
void CG_VoiceAudioMix( int speaker, float left, float right, float muffle ) {
	if ( speaker < 0 || speaker >= VOICE_SPEAKERS )
		return;
	ma_atomic_float_set( &audioSpeakers[speaker].left, left );
	ma_atomic_float_set( &audioSpeakers[speaker].right, right );
	ma_atomic_float_set( &audioSpeakers[speaker].muffle, muffle );
}

// from now on what is played is also kept for CG_VoiceAudioTapRead()
void CG_VoiceAudioTap( int on ) {
	if ( !audioReady )
		return;
	if ( on )
		ma_pcm_rb_reset( &audioTap );
	ma_atomic_bool32_set( &audioTapOn, on ? MA_TRUE : MA_FALSE );
}

// what was played since the last call: up to maxFrames pairs of 16 bit samples. Returns how many.
int CG_VoiceAudioTapRead( short *stereo, int maxFrames ) {
	if ( !audioReady )
		return 0;
	return Audio_RingRead( &audioTap, stereo, maxFrames );
}

#endif // JP_VOICE
