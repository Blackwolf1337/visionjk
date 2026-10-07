/*
===========================================================================
Copyright (C) 2013 - 2015, OpenJK contributors

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

// cg_voice.c -- the voice chat of the client. Not from JA+, see docs/japlus/voice.md.
//
// The engine knows nothing of it and is not changed for it. This module records the microphone
// (cg_voice_audio.c), codes it with Opus and sends it to a UDP port of the server over a socket of its own,
// and plays what comes back from there: the voices of the players the server lets this one hear.
//
// Nothing in here waits. The socket does not block, the sound devices run in threads of the audio library,
// and CG_VoiceFrame() moves a little of everything along once a frame.

#include "game/bg_net.h"		// the sockets, in front of everything else
#include "cg_local.h"
#include "game/bg_voice.h"
#include "cg_voice.h"

#ifdef JP_VOICE

#include <opus.h>
#ifndef _WIN32
	#include <pthread.h>
	#include <netdb.h>
#endif

#define VOICE_ENCODER_BYTES		40960	// room for the state of the Opus encoder. What it needs is asked and checked
#define VOICE_DECODER_BYTES		24576	//	and for the decoder of one speaker
#define VOICE_BITRATE			20000
#define VOICE_ASK_MSEC			3000	// the server is asked for voice this often until it answers,
#define VOICE_MAX_ASKS			10		//	and this many times at most
#define VOICE_HELLO_MSEC		500		// a hello datagram this often until the server answers one,
#define VOICE_KEEPALIVE_MSEC	5000	//	and this often after that: the router of the player keeps the way open
#define VOICE_LEAD_FRAMES		3		// frames of a speaker that wait before he is played: 60 ms for late datagrams
#define VOICE_MAX_LEAD_FRAMES	8		//	what that grows to when his voice keeps running dry
#define VOICE_SILENT_MSEC		300		// nothing of a speaker for this long: he stopped talking
#define VOICE_MIC_HOLD_MSEC		30000	// push to talk: the microphone is closed this long after the key was let go
#define VOICE_VAD_HOLD_MSEC		400		// voice activation: it goes on sending this long after the last loud frame
#define VOICE_MAX_PER_FRAME		64		// datagrams taken and frames sent in one frame of the game at most
#define VOICE_TRACE_MSEC		100		// how often the line from the listener to a speaker is looked at
#define VOICE_FAR_GAIN			0.1f	// how loud a voice still is at the end of its range
#define VOICE_MAX_PAN			0.8f	// how far a voice goes to one side: it is never gone from the other ear
#define VOICE_HEAD				24.0f	// the mouth of a player above his origin

typedef struct voiceSpeaker_s {
	voiceQueue_t	queue;
	qboolean		decoderOk;
	qboolean		playing;
	int				lead;			// frames that have to wait before he is played, VOICE_LEAD_FRAMES and up
	int				waitSince;		// when the first frame came that still waits
	int				lastHeard;		// when his last datagram came
	int				concealed;		// frames in a row that were made up
	int				flags, distance;	// of his last datagram: is he in what the server shows, and how far away
	qboolean		muted;			// by this player (voice_mute)
	qboolean		muffled;		// something is between him and the listener
	int				traceTime;		//	and when that was looked at last
} voiceSpeaker_t;

// a block of memory for a state of Opus, as well aligned as anything can ask for
typedef union voiceMem_u {
	double	align;
	void	*alignToo;
	byte	bytes[VOICE_DECODER_BYTES];
} voiceMem_t;

static struct {
	qboolean			serverHas;		// the server info says g_voice
	qboolean			asked;			// "jpvoice hello" went to the server,
	int					asks;			//	this many times
	qboolean			answered;		// the server answered: port, token, range
	qboolean			failed;			// something is missing for good, said once
	int					port, range;
	unsigned int		token;

	bgSocket_t			sock;
	qboolean			sockOpen;
	struct sockaddr_in	server;
	qboolean			serverKnown;
	qboolean			linked;			// the server answered a hello datagram: the way is open both ways
	int					helloTime, askTime;
	qboolean			toldNoLink;

	qboolean			audio;			// the speakers are open
	qboolean			encoderOk;
	int					seq;
	qboolean			keyDown;		// +voice
	qboolean			sending;
	int					talkUntil;		// voice activation
	int					micUntil;		// push to talk: when the microphone is closed again

	int					test;			// voice_test: 1 a tone goes out in place of the microphone
	int					testNext;
	int					testSample;
	fileHandle_t		record;			// voice_test record: what is played goes into this file
} voice;

static voiceSpeaker_t	voiceSpeakers[MAX_CLIENTS];
static voiceMem_t		voiceDecoders[MAX_CLIENTS];
static union {
	double	align;
	void	*alignToo;
	byte	bytes[VOICE_ENCODER_BYTES];
} voiceEncoder;

/*
===============================================================================

The address of the server

The server said on which port it relays. Its address is the one the game is connected to, which the engine
keeps in a cvar: OpenJK as numbers (cl_currentServerIP), jamp only as the player typed it
(cl_currentServerAddress). A name has to be looked up, and a lookup can take seconds, so it runs in a thread
that does nothing else. The game only asks whether it is done.

===============================================================================
*/

static struct {
	volatile int		state;		// 0: none, 1: running, 2: found, 3: not found
	char				name[256];
	struct in_addr		result;
#ifdef _WIN32
	HANDLE				thread;
#else
	pthread_t			thread;
#endif
} voiceLookup;

#ifdef _WIN32
static DWORD WINAPI Voice_LookupThread( LPVOID unused )
#else
static void *Voice_LookupThread( void *unused )
#endif
{
	struct addrinfo	hints, *list = NULL;

	(void)unused;
	memset( &hints, 0, sizeof( hints ) );
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	if ( getaddrinfo( voiceLookup.name, NULL, &hints, &list ) == 0 && list && list->ai_addrlen >= sizeof( struct sockaddr_in ) ) {
		voiceLookup.result = ( (struct sockaddr_in *)list->ai_addr )->sin_addr;
		voiceLookup.state = 2;
	} else {
		voiceLookup.state = 3;
	}
	if ( list )
		freeaddrinfo( list );
	return 0;
}

// The thread is over, or is waited for. Only at the end of the module does that ever wait, and only if a
// lookup is still on its way then: the code of the thread must not be unloaded under it.
static void Voice_LookupJoin( void ) {
	if ( !voiceLookup.state )
		return;
#ifdef _WIN32
	WaitForSingleObject( voiceLookup.thread, INFINITE );
	CloseHandle( voiceLookup.thread );
#else
	pthread_join( voiceLookup.thread, NULL );
#endif
	voiceLookup.state = 0;
}

// Finds out where the server is. qtrue: voice.server is set. qfalse: not yet, or never (voice.failed).
static qboolean Voice_FindServer( void ) {
	char	host[256], *colon;

	if ( voiceLookup.state == 1 )
		return qfalse;
	if ( voiceLookup.state == 2 || voiceLookup.state == 3 ) {
		const qboolean found = ( voiceLookup.state == 2 ) ? qtrue : qfalse;

		Voice_LookupJoin();
		if ( !found ) {
			trap->Print( "Voice chat: the address of the server could not be looked up\n" );
			voice.failed = qtrue;
			return qfalse;
		}
		voice.server.sin_addr = voiceLookup.result;
		return qtrue;
	}

	trap->Cvar_VariableStringBuffer( "cl_currentServerIP", host, sizeof( host ) );
	if ( !host[0] )
		trap->Cvar_VariableStringBuffer( "cl_currentServerAddress", host, sizeof( host ) );
	if ( host[0] == '[' || !host[0] ) {
		// an address of the newer kind, or none at all: the relay only has the older kind
		trap->Print( "Voice chat: no address of the server that it could use\n" );
		voice.failed = qtrue;
		return qfalse;
	}
	colon = strrchr( host, ':' );
	if ( colon )
		*colon = '\0';
	if ( !Q_stricmp( host, "localhost" ) || !Q_stricmp( host, "loopback" ) )
		Q_strncpyz( host, "127.0.0.1", sizeof( host ) );
	if ( inet_pton( AF_INET, host, &voice.server.sin_addr ) == 1 )
		return qtrue;

	// a name: looked up beside the game
	Q_strncpyz( voiceLookup.name, host, sizeof( voiceLookup.name ) );
	voiceLookup.state = 1;
#ifdef _WIN32
	voiceLookup.thread = CreateThread( NULL, 0, Voice_LookupThread, NULL, 0, NULL );
	if ( !voiceLookup.thread )
#else
	if ( pthread_create( &voiceLookup.thread, NULL, Voice_LookupThread, NULL ) != 0 )
#endif
	{
		voiceLookup.state = 0;
		voice.failed = qtrue;
	}
	return qfalse;
}

/*
===============================================================================

Start and end

===============================================================================
*/

static void Voice_Close( void ) {
	const qboolean	serverHas = voice.serverHas;
	int				i;

	if ( voice.record ) {
		trap->FS_Close( voice.record );
		voice.record = 0;
	}
	CG_VoiceAudioShutdown();
	Voice_LookupJoin();
	if ( voice.sockOpen )
		BG_CloseSocket( voice.sock );
	// who was muted stays muted while the module does
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const qboolean muted = voiceSpeakers[i].muted;

		memset( &voiceSpeakers[i], 0, sizeof( voiceSpeakers[i] ) );
		voiceSpeakers[i].muted = muted;
	}
	memset( &voice, 0, sizeof( voice ) );
	voice.sock = BG_INVALID_SOCKET;
	// the server is still the same one: switched on again, the chat asks it anew
	voice.serverHas = serverHas;
}

void CG_VoiceInit( void ) {
	memset( voiceSpeakers, 0, sizeof( voiceSpeakers ) );
	memset( &voice, 0, sizeof( voice ) );
	voice.sock = BG_INVALID_SOCKET;
}

// the module is about to be unloaded (the level changes, vid_restart, the game ends): nothing of it may run after
void CG_VoiceShutdown( void ) {
	Voice_Close();
}

// from the server info: does this server have a voice chat? Only then is it asked for one.
void CG_VoiceServerInfo( qboolean has ) {
	voice.serverHas = has;
}

/*
=================
CG_VoiceServerCommand

jpvoice <protocol> <port> <token> <range>: the answer of the server to "jpvoice hello". From here on there is
a socket, and the speakers are open.
=================
*/
void CG_VoiceServerCommand( void ) {
	const int	protocol = atoi( CG_Argv( 1 ) ), port = atoi( CG_Argv( 2 ) );
	const unsigned int token = (unsigned int)strtoul( CG_Argv( 3 ), NULL, 10 );
	int			i;

	if ( !voice.asked || voice.failed )
		return;
	if ( voice.answered ) {
		// two questions crossed on the way: the server knows the token of its last answer
		if ( protocol == VOICE_PROTOCOL && token )
			voice.token = token;
		return;
	}
	if ( protocol != VOICE_PROTOCOL ) {
		trap->Print( "Voice chat: the server speaks version %i of it, this client version %i\n", protocol, VOICE_PROTOCOL );
		voice.failed = qtrue;
		return;
	}
	if ( port <= 0 || port > 65535 || !token ) {
		voice.failed = qtrue;
		return;
	}
	voice.port = port;
	voice.token = token;
	voice.range = atoi( CG_Argv( 4 ) );

	// the codec. Its states live in memory of this file, so what they need has to fit.
	if ( opus_encoder_get_size( 1 ) > (int)sizeof( voiceEncoder ) || opus_decoder_get_size( 1 ) > (int)sizeof( voiceDecoders[0] )
		|| opus_encoder_init( (OpusEncoder *)&voiceEncoder, VOICE_RATE, 1, OPUS_APPLICATION_VOIP ) != OPUS_OK ) {
		trap->Print( "Voice chat: the codec does not start\n" );
		voice.failed = qtrue;
		return;
	}
	opus_encoder_ctl( (OpusEncoder *)&voiceEncoder, OPUS_SET_BITRATE( VOICE_BITRATE ) );
	opus_encoder_ctl( (OpusEncoder *)&voiceEncoder, OPUS_SET_SIGNAL( OPUS_SIGNAL_VOICE ) );
	// every frame carries a small copy of the one before it, so one lost datagram can be made good
	opus_encoder_ctl( (OpusEncoder *)&voiceEncoder, OPUS_SET_INBAND_FEC( 1 ) );
	opus_encoder_ctl( (OpusEncoder *)&voiceEncoder, OPUS_SET_PACKET_LOSS_PERC( 10 ) );
	voice.encoderOk = qtrue;
	for ( i = 0; i < MAX_CLIENTS; i++ )
		voiceSpeakers[i].decoderOk = ( opus_decoder_init( (OpusDecoder *)&voiceDecoders[i], VOICE_RATE, 1 ) == OPUS_OK ) ? qtrue : qfalse;

	BG_NetStart();
	voice.sock = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( voice.sock == BG_INVALID_SOCKET ) {
		trap->Print( "Voice chat: no socket\n" );
		voice.failed = qtrue;
		return;
	}
	voice.sockOpen = qtrue;
	BG_SocketNonBlocking( voice.sock );
	memset( &voice.server, 0, sizeof( voice.server ) );
	voice.server.sin_family = AF_INET;
	voice.server.sin_port = htons( (unsigned short)port );

	if ( !CG_VoiceAudioInit( cg_voiceBackend.string ) || !CG_VoiceAudioOpenPlayback( cg_voicePlayback.string ) ) {
		trap->Print( "Voice chat: no sound device to play on, you will not hear the others\n" );
	} else {
		voice.audio = qtrue;
		for ( i = 0; i < MAX_CLIENTS; i++ )
			CG_VoiceAudioMix( i, cg_voiceVolume.value, cg_voiceVolume.value, 0.0f );
	}
	voice.answered = qtrue;
	trap->Print( "Voice chat: on. bind a key to +voice to talk (cg_voiceMode 2 sends whenever you speak)\n" );
}

/*
===============================================================================

Out: the microphone

===============================================================================
*/

static void Voice_Send( int type, const byte *frame, int frameLen ) {
	voicePacket_t	p;
	byte			datagram[VOICE_MAX_DATAGRAM];
	int				len;

	memset( &p, 0, sizeof( p ) );
	p.type = type;
	p.client = cg.clientNum;
	p.token = voice.token;
	p.seq = voice.seq;
	p.data = frame;
	p.dataLen = frameLen;
	len = BG_VoicePack( datagram, sizeof( datagram ), &p, qfalse );
	if ( len )
		sendto( voice.sock, (const char *)datagram, len, BG_SEND_FLAGS, (const struct sockaddr *)&voice.server, sizeof( voice.server ) );
}

// one frame of sound goes out
static void Voice_SendFrame( const short *samples ) {
	byte		frame[VOICE_MAX_FRAME];
	const int	len = opus_encode( (OpusEncoder *)&voiceEncoder, samples, VOICE_FRAME_SAMPLES, frame, sizeof( frame ) );

	// two bytes and less is a frame the codec found nothing in
	if ( len > 2 ) {
		Voice_Send( VOICE_AUDIO, frame, len );
		voice.seq = ( voice.seq + 1 ) & 0xffff;
	}
}

// how loud a frame is, 0 to 1
static float Voice_Level( const short *samples ) {
	float	sum = 0.0f;
	int		i;

	for ( i = 0; i < VOICE_FRAME_SAMPLES; i++ )
		sum += (float)samples[i] * samples[i];
	return sqrtf( sum / VOICE_FRAME_SAMPLES ) * ( 1.0f / 32768.0f );
}

/*
=================
Voice_Capture

Push to talk (cg_voiceMode 1): the microphone is opened when the key goes down, which takes a moment the
first time, and is closed again when it has not been used for a while. A player who never presses the key
never has his microphone open.

Voice activation (cg_voiceMode 2): the microphone is open all the time, and what is louder than
cg_voiceThreshold is sent.
=================
*/
static void Voice_Capture( int now ) {
	const qboolean	activation = ( cg_voiceMode.integer == 2 ) ? qtrue : qfalse;
	const qboolean	wanted = ( activation || voice.keyDown || now < voice.micUntil ) ? qtrue : qfalse;
	short			samples[VOICE_FRAME_SAMPLES];
	int				count, i;

	if ( voice.test == 1 ) {
		// a tone of two pitches in place of the microphone, a frame for every 20 ms that went by
		if ( !voice.testNext || now - voice.testNext > 500 )
			voice.testNext = now;
		for ( count = 0; count < VOICE_MAX_PER_FRAME && now >= voice.testNext; count++ ) {
			for ( i = 0; i < VOICE_FRAME_SAMPLES; i++, voice.testSample++ ) {
				samples[i] = (short)( 6000.0f * sinf( voice.testSample * ( 2.0f * M_PI * 440.0f / VOICE_RATE ) )
					+ 6000.0f * sinf( voice.testSample * ( 2.0f * M_PI * 3000.0f / VOICE_RATE ) ) );
			}
			Voice_SendFrame( samples );
			voice.testNext += VOICE_FRAME_MSEC;
		}
		voice.sending = qtrue;
		return;
	}

	if ( !wanted ) {
		CG_VoiceAudioCloseCapture();
		voice.sending = qfalse;
		return;
	}
	if ( !CG_VoiceAudioCapturing() ) {
		if ( !CG_VoiceAudioInit( cg_voiceBackend.string ) || !CG_VoiceAudioOpenCapture( cg_voiceCapture.string ) ) {
			if ( voice.keyDown || activation )
				trap->Print( "Voice chat: no microphone (voice_devices lists what there is, cg_voiceCapture chooses)\n" );
			voice.keyDown = qfalse;
			voice.micUntil = 0;
			if ( activation )
				trap->Cvar_Set( "cg_voiceMode", "1" );
			return;
		}
	}
	if ( voice.keyDown )
		voice.micUntil = now + VOICE_MIC_HOLD_MSEC;

	voice.sending = qfalse;
	for ( count = 0; count < VOICE_MAX_PER_FRAME && CG_VoiceAudioRead( samples, VOICE_FRAME_SAMPLES ); count++ ) {
		if ( cg_voiceMicGain.value != 1.0f ) {
			for ( i = 0; i < VOICE_FRAME_SAMPLES; i++ )
				samples[i] = (short)Com_Clamp( -32767.0f, 32767.0f, samples[i] * cg_voiceMicGain.value );
		}
		if ( activation && Voice_Level( samples ) > cg_voiceThreshold.value )
			voice.talkUntil = now + VOICE_VAD_HOLD_MSEC;
		if ( activation ? ( now < voice.talkUntil ) : voice.keyDown ) {
			Voice_SendFrame( samples );
			voice.sending = qtrue;
		}
	}
	// the game stood still for a while and more piled up than is worth sending late
	if ( count == VOICE_MAX_PER_FRAME )
		CG_VoiceAudioDropCapture();
}

/*
===============================================================================

In: the voices of the others

===============================================================================
*/

static void Voice_Receive( int now ) {
	int count;

	for ( count = 0; count < VOICE_MAX_PER_FRAME; count++ ) {
		byte				datagram[VOICE_MAX_DATAGRAM + 1];	// one more: a longer one shows as too long
		struct sockaddr_in	from;
		socklen_t			fromLen = sizeof( from );
		voicePacket_t		p;
		voiceSpeaker_t		*sp;
		const int			len = (int)recvfrom( voice.sock, (char *)datagram, sizeof( datagram ), 0, (struct sockaddr *)&from, &fromLen );

		if ( len < 0 ) {
			if ( BG_SocketReset() )
				continue;
			break;
		}
		// only the server speaks here: its address, its port, and the token it gave this client
		if ( fromLen < (socklen_t)sizeof( from ) || from.sin_addr.s_addr != voice.server.sin_addr.s_addr
			|| from.sin_port != voice.server.sin_port || !BG_VoiceUnpack( datagram, len, qtrue, &p ) || p.token != voice.token )
			continue;
		if ( p.type == VOICE_HELLO ) {
			voice.linked = qtrue;
			continue;
		}
		if ( p.client >= MAX_CLIENTS || p.client == cg.clientNum )
			continue;
		sp = &voiceSpeakers[p.client];
		if ( sp->muted || !sp->decoderOk )
			continue;
		if ( !BG_VoiceQueueSpan( &sp->queue ) )
			sp->waitSince = now;
		if ( BG_VoiceQueuePut( &sp->queue, p.seq, p.data, p.dataLen ) ) {
			sp->lastHeard = now;
			sp->flags = p.flags;
			sp->distance = p.distance;
		}
	}
}

// the next 20 ms of a speaker into what is played: the frame whose turn it is, or what the codec makes up for it
static void Voice_PlayFrame( int speaker, qboolean makeUp ) {
	voiceSpeaker_t	*sp = &voiceSpeakers[speaker];
	OpusDecoder		*decoder = (OpusDecoder *)&voiceDecoders[speaker];
	short			samples[VOICE_FRAME_SAMPLES];
	const byte		*data = NULL, *after = NULL;
	int				afterLen = 0, got;
	const int		len = makeUp ? 0 : BG_VoiceQueueGet( &sp->queue, &data, &after, &afterLen );

	if ( len ) {
		got = opus_decode( decoder, data, len, samples, VOICE_FRAME_SAMPLES, 0 );
		sp->concealed = 0;
	} else {
		// lost: from the copy the frame after it carries, or guessed from what came before
		got = after ? opus_decode( decoder, after, afterLen, samples, VOICE_FRAME_SAMPLES, 1 )
			: opus_decode( decoder, NULL, 0, samples, VOICE_FRAME_SAMPLES, 0 );
		sp->concealed++;
	}
	// a frame of another length than this chat uses, or one the codec refuses, is silence
	if ( got != VOICE_FRAME_SAMPLES )
		memset( samples, 0, sizeof( samples ) );
	CG_VoiceAudioWrite( speaker, samples, VOICE_FRAME_SAMPLES );
}

/*
=================
Voice_Play

A speaker starts to play when a few of his frames wait (sp->lead), so a datagram that comes a little late
still finds its place. While he plays, decoded sound is kept ahead of the speakers by a little more than a
frame of the game lasts. When his frames stop coming for a moment the codec fills in. When they stop for
longer he has stopped talking, and the next thing from him starts anew.
=================
*/
static void Voice_Play( int now ) {
	// how much sound is kept ahead of the sound device: 40 ms, more when the game runs slowly
	int ahead = cg.frametime * 5 / 2;
	int i, count;

	if ( ahead < 2 * VOICE_FRAME_MSEC )
		ahead = 2 * VOICE_FRAME_MSEC;
	if ( ahead > 250 )
		ahead = 250;
	ahead = ahead * ( VOICE_RATE / 1000 );

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		voiceSpeaker_t	*sp = &voiceSpeakers[i];
		const int		span = BG_VoiceQueueSpan( &sp->queue );

		if ( !sp->playing ) {
			if ( !span )
				continue;
			if ( sp->lead < VOICE_LEAD_FRAMES )
				sp->lead = VOICE_LEAD_FRAMES;
			if ( span < sp->lead && now - sp->waitSince < sp->lead * VOICE_FRAME_MSEC )
				continue;
			sp->playing = qtrue;
			sp->concealed = 0;
		}

		for ( count = 0; count < VOICE_QUEUE && CG_VoiceAudioQueued( i ) < ahead; count++ ) {
			if ( BG_VoiceQueueSpan( &sp->queue ) ) {
				Voice_PlayFrame( i, qfalse );
			} else if ( now - sp->lastHeard < 3 * VOICE_FRAME_MSEC && sp->concealed < 3 ) {
				// his next datagram is a little late: bridged
				Voice_PlayFrame( i, qtrue );
			} else {
				break;
			}
		}

		if ( !BG_VoiceQueueSpan( &sp->queue ) && !CG_VoiceAudioQueued( i ) ) {
			if ( now - sp->lastHeard > VOICE_SILENT_MSEC ) {
				// he stopped talking
				sp->playing = qfalse;
				BG_VoiceQueueReset( &sp->queue );
				if ( sp->lead > VOICE_LEAD_FRAMES )
					sp->lead--;
			} else if ( sp->concealed >= 3 ) {
				// his voice ran dry while he talks: the way from him is uneven, more of it waits next time
				sp->playing = qfalse;
				if ( sp->lead < VOICE_MAX_LEAD_FRAMES )
					sp->lead++;
			}
		}
	}
}

/*
===============================================================================

Where a voice comes from

===============================================================================
*/

// Is something between the listener and this speaker: a wall, a closed door, or the surface of water?
static qboolean Voice_Blocked( const centity_t *cent ) {
	vec3_t	mouth;
	trace_t	tr;

	VectorCopy( cent->lerpOrigin, mouth );
	mouth[2] += VOICE_HEAD;
	if ( ( CG_PointContents( cg.refdef.vieworg, -1 ) ^ CG_PointContents( mouth, -1 ) ) & MASK_WATER )
		return qtrue;
	// players are not in this mask, so nobody stands in the way of a voice
	CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, mouth, cg.snap->ps.clientNum, MASK_SOLID );
	return ( tr.fraction < 1.0f && tr.entityNum != cent->currentState.number ) ? qtrue : qfalse;
}

/*
=================
Voice_Place

How every voice that plays is heard in this frame: how loud on each side, and whether muffled.

On a server with a range (g_voiceRange) a voice gets quieter with the distance and comes from the side its
speaker is on. A speaker behind a wall, behind a closed door or on the other side of a water surface is
muffled: one line from the eye of the listener to his head is looked at ten times a second. A speaker the
server does not show at all is muffled too and comes from no direction, at the distance the server named:
no more is known of him, on purpose.

Without a range, and among spectators, a voice is the same everywhere, like a radio.
=================
*/
static void Voice_Place( int now ) {
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		voiceSpeaker_t	*sp = &voiceSpeakers[i];
		const centity_t	*cent = &cg_entities[i];
		float			gain = cg_voiceVolume.value, pan = 0.0f, distance;
		qboolean		muffled = qfalse;

		if ( !sp->playing )
			continue;
		// the mark over his head, as long as his voice comes in (CG_Player)
		cg_entities[i].vChatTime = cg.time + 250;

		if ( voice.range > 0 && cgs.clientinfo[i].team != TEAM_SPECTATOR ) {
			if ( cent->currentValid && !( sp->flags & VOICE_FLAG_UNSEEN ) ) {
				vec3_t to;

				VectorSubtract( cent->lerpOrigin, cg.refdef.vieworg, to );
				to[2] += VOICE_HEAD;
				distance = VectorNormalize( to );
				// viewaxis[1] points to the left of the listener
				pan = Com_Clamp( -VOICE_MAX_PAN, VOICE_MAX_PAN, DotProduct( to, cg.refdef.viewaxis[1] ) );
				if ( now - sp->traceTime >= VOICE_TRACE_MSEC ) {
					sp->traceTime = now;
					sp->muffled = Voice_Blocked( cent );
				}
				muffled = sp->muffled;
			} else {
				distance = (float)sp->distance * VOICE_DISTANCE_STEP;
				muffled = qtrue;
			}
			distance = Com_Clamp( 0.0f, 1.0f, 1.0f - distance / voice.range );
			gain *= VOICE_FAR_GAIN + ( 1.0f - VOICE_FAR_GAIN ) * distance * distance;
		}
		if ( !cg_voiceMuffle.integer )
			muffled = qfalse;
		// the same loudness from the middle as from a side: the two ears share it
		CG_VoiceAudioMix( i, gain * sqrtf( 1.0f + pan ), gain * sqrtf( 1.0f - pan ), muffled ? 1.0f : 0.0f );
	}
}

/*
=================
CG_VoiceDraw

On the HUD: who is talking, and a line for this player while his own voice goes out.
=================
*/
void CG_VoiceDraw( void ) {
	const int	x = 8, height = SMALLCHAR_HEIGHT + 2;
	int			y = 232, i;

	if ( !voice.answered || !cg_voiceDrawTalkers.integer )
		return;
	if ( voice.sending && cgs.clientinfo[cg.clientNum].infoValid ) {
		CG_DrawPic( x, y, SMALLCHAR_HEIGHT, SMALLCHAR_HEIGHT, cgs.media.vchatShader );
		CG_DrawSmallString( x + SMALLCHAR_HEIGHT + 4, y, va( "%s ^7(you)", cgs.clientinfo[cg.clientNum].name ), 1.0f );
		y += height;
	}
	for ( i = 0; i < MAX_CLIENTS && y < 400; i++ ) {
		if ( !voiceSpeakers[i].playing || !cgs.clientinfo[i].infoValid )
			continue;
		CG_DrawPic( x, y, SMALLCHAR_HEIGHT, SMALLCHAR_HEIGHT, cgs.media.vchatShader );
		CG_DrawSmallString( x + SMALLCHAR_HEIGHT + 4, y, cgs.clientinfo[i].name, 1.0f );
		y += height;
	}
}

/*
===============================================================================

Once a frame

===============================================================================
*/

void CG_VoiceFrame( void ) {
	const int now = trap->Milliseconds();

	if ( !cg_voice.integer || !voice.serverHas || voice.failed || !cg.snap ) {
		// switched off while it ran
		if ( voice.answered && !cg_voice.integer )
			Voice_Close();
		return;
	}
	if ( !voice.answered ) {
		// Asked again while there is no answer: the flood protection of the server (sv_floodProtect) drops a
		// command that comes within a second of another one without a word, and this one goes out just when the
		// client entered the game and has more to say.
		if ( voice.asks < VOICE_MAX_ASKS && ( !voice.asked || now - voice.askTime >= VOICE_ASK_MSEC ) ) {
			trap->SendClientCommand( va( "jpvoice hello %i", VOICE_PROTOCOL ) );
			voice.asked = qtrue;
			voice.askTime = now;
			voice.asks++;
		}
		return;
	}
	if ( !voice.serverKnown ) {
		voice.serverKnown = Voice_FindServer();
		if ( !voice.serverKnown )
			return;
	}

	// the server learns from these where this client listens, and its router keeps the way open
	if ( now - voice.helloTime >= ( voice.linked ? VOICE_KEEPALIVE_MSEC : VOICE_HELLO_MSEC ) || !voice.helloTime ) {
		Voice_Send( VOICE_HELLO, NULL, 0 );
		voice.helloTime = now;
		if ( !voice.linked && !voice.toldNoLink && now - voice.askTime > 10000 ) {
			trap->Print( "Voice chat: no answer from UDP port %i of the server. Is that port open there?\n", voice.port );
			voice.toldNoLink = qtrue;
		}
	}

	Voice_Receive( now );
	if ( voice.audio ) {
		Voice_Play( now );
		Voice_Place( now );
	}
	if ( voice.linked && voice.encoderOk )
		Voice_Capture( now );

	// voice_test record: what the speakers got since the last frame, as it is
	if ( voice.record ) {
		short	stereo[2048];
		int		frames;

		while ( ( frames = CG_VoiceAudioTapRead( stereo, 1024 ) ) > 0 )
			trap->FS_Write( stereo, frames * 2 * (int)sizeof( short ), voice.record );
	}
}

/*
===============================================================================

Commands

===============================================================================
*/

void CG_VoiceDown_f( void ) {
	voice.keyDown = qtrue;
}

void CG_VoiceUp_f( void ) {
	voice.keyDown = qfalse;
}

// voice_devices: the microphones and speakers there are. cg_voiceCapture and cg_voicePlayback take a name.
void CG_VoiceDevices_f( void ) {
	static char	names[VOICE_MAX_DEVICES][VOICE_DEVICE_NAME];
	int			capture, count, i;

	if ( !CG_VoiceAudioInit( cg_voiceBackend.string ) ) {
		trap->Print( "No sound system\n" );
		return;
	}
	for ( capture = 1; capture >= 0; capture-- ) {
		count = CG_VoiceAudioDevices( capture, names, VOICE_MAX_DEVICES );
		trap->Print( "%s (%s \"name\", empty for the one of the system):\n", capture ? "Microphones" : "Speakers",
			capture ? "cg_voiceCapture" : "cg_voicePlayback" );
		for ( i = 0; i < count; i++ )
			trap->Print( "  \"%s\"\n", names[i] );
		if ( !count )
			trap->Print( "  none\n" );
	}
}

void CG_VoiceStatus_f( void ) {
	int i;

	if ( !cg_voice.integer ) {
		trap->Print( "Voice chat is off (cg_voice 0)\n" );
	} else if ( !voice.serverHas ) {
		trap->Print( "This server has no voice chat\n" );
	} else if ( voice.failed ) {
		trap->Print( "Voice chat could not start, see the lines above in the console\n" );
	} else if ( !voice.answered ) {
		trap->Print( "Voice chat: asked the server, no answer yet\n" );
	} else {
		trap->Print( "Voice chat: UDP port %i, %s, range %i, %s, microphone %s%s\n", voice.port,
			voice.linked ? "connected" : "no answer from that port yet", voice.range,
			cg_voiceMode.integer == 2 ? "voice activation" : "push to talk (+voice)",
			CG_VoiceAudioCapturing() ? "open" : "closed", voice.sending ? ", sending" : "" );
	}
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( cgs.clientinfo[i].infoValid && ( voiceSpeakers[i].playing || voiceSpeakers[i].muted ) )
			trap->Print( "  %2i %s^7%s%s\n", i, cgs.clientinfo[i].name, voiceSpeakers[i].playing ? " talks" : "",
				voiceSpeakers[i].muted ? " (muted by you)" : "" );
	}
}

// voice_mute <player>, voice_unmute <player or all>: this player does not want to hear that one
static void Voice_Mute( qboolean mute ) {
	const char	*who = CG_Argv( 1 );
	int			n = -1, i;

	if ( !mute && !Q_stricmp( who, "all" ) ) {
		for ( i = 0; i < MAX_CLIENTS; i++ )
			voiceSpeakers[i].muted = qfalse;
		trap->Print( "Nobody is muted\n" );
		return;
	}
	if ( who[0] >= '0' && who[0] <= '9' && strlen( who ) <= 2 )
		n = atoi( who );
	else if ( who[0] )
		n = CG_ClientNumFromName( who );
	if ( n < 0 || n >= MAX_CLIENTS || !cgs.clientinfo[n].infoValid ) {
		trap->Print( "usage: %s <name or number of a player>%s\n", CG_Argv( 0 ), mute ? "" : " | all" );
		return;
	}
	voiceSpeakers[n].muted = mute;
	if ( mute ) {
		voiceSpeakers[n].playing = qfalse;
		BG_VoiceQueueReset( &voiceSpeakers[n].queue );
	}
	trap->Print( "%s^7 is %s\n", cgs.clientinfo[n].name, mute ? "muted until the level changes" : "heard again" );
}

void CG_VoiceMute_f( void ) {
	Voice_Mute( qtrue );
}

void CG_VoiceUnmute_f( void ) {
	Voice_Mute( qfalse );
}

/*
=================
CG_VoiceTest_f

voice_test tone: a tone of 440 and 3000 Hz goes out in place of the microphone
voice_test record <name>: what the speakers get is also written to voice/<name>.raw (16 bit, two channels,
	VOICE_RATE a second, low byte first)
voice_test stop: both end
=================
*/
void CG_VoiceTest_f( void ) {
	const char *what = CG_Argv( 1 );

	if ( !Q_stricmp( what, "tone" ) ) {
		voice.test = 1;
		voice.testNext = 0;
	} else if ( !Q_stricmp( what, "record" ) && CG_Argv( 2 )[0] && !strpbrk( CG_Argv( 2 ), "/\\.:" ) ) {
		if ( voice.record )
			trap->FS_Close( voice.record );
		voice.record = 0;
		trap->FS_Open( va( "voice/%s.raw", CG_Argv( 2 ) ), &voice.record, FS_WRITE );
		CG_VoiceAudioTap( voice.record ? 1 : 0 );
	} else if ( !Q_stricmp( what, "stop" ) ) {
		voice.test = 0;
		CG_VoiceAudioTap( 0 );
		if ( voice.record )
			trap->FS_Close( voice.record );
		voice.record = 0;
	} else {
		trap->Print( "usage: voice_test tone | record <name> | stop\n" );
	}
}

#else // JP_VOICE

// built without the voice chat (BuildJPVoice off)
void CG_VoiceInit( void ) {}
void CG_VoiceShutdown( void ) {}
void CG_VoiceFrame( void ) {}
void CG_VoiceServerInfo( qboolean has ) { (void)has; }
void CG_VoiceServerCommand( void ) {}
void CG_VoiceDown_f( void ) {}
void CG_VoiceUp_f( void ) {}
void CG_VoiceDevices_f( void ) {}
void CG_VoiceStatus_f( void ) {}
void CG_VoiceTest_f( void ) {}
void CG_VoiceMute_f( void ) {}
void CG_VoiceUnmute_f( void ) {}
void CG_VoiceDraw( void ) {}

#endif // JP_VOICE
