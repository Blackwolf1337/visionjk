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

// cg_voice.c -- the voice chat of the client. Not from JA+, see docs/japlus/voice.md.
//
// The engine knows nothing of it and is not changed for it. This module records the microphone
// (cg_voice_audio.c), codes it with Opus and sends it to a UDP port of the server over a socket of its own,
// and plays what comes back from there: the voices of the players the server lets this one hear.
//
// A server without the game module of this tree has no such port. There the same sound goes through the voice
// master, one relay for all those servers (v_voiceMaster, see "The masterserver" below).
//
// Nothing in here waits. The socket does not block, the sound devices run in threads of the audio library,
// and CG_VoiceFrame() moves a little of everything along once a frame.
//
// The client that runs in a browser can not send UDP and has no threads. There the same datagrams go through
// a WebSocket to the web host of the server, which hands them to that port (cg_voice_web.c), and nothing is
// looked up. Those are the places marked __EMSCRIPTEN__, everything else is the same code.

#include "game/bg_net.h"		// the sockets, in front of everything else
#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"
#include "game/voice/bg_voice.h"
#include "cg_voice.h"
#include "cgame/boombox/cg_boombox.h"

#ifdef JP_VOICE

#include <opus.h>
#if !defined( _WIN32 ) && !defined( __EMSCRIPTEN__ )
	#include <pthread.h>
	#include <netdb.h>
#endif
#ifndef __EMSCRIPTEN__
	#include "web/web_http.h"	// Web_SHA1, for the name of the room at the masterserver
#endif

#define VOICE_ENCODER_BYTES		40960	// room for the state of the Opus encoder. What it needs is asked and checked
#define VOICE_DECODER_BYTES		24576	//	and for the decoder of one speaker
// With the copy of the frame before in every frame (OPUS_SET_INBAND_FEC, 10 % loss) Opus codes the whole width
// of VOICE_RATE only from about 26500 bits a second on. Below that it gives up width to pay for the copy: at
// 20000, which this was until 2026-10-08, nothing above 8 kHz was left of a voice.
#define VOICE_BITRATE			32000
#define VOICE_ASK_MSEC			3000	// the server is asked for voice this often until it answers,
#define VOICE_MAX_ASKS			10		//	and this many times at most
#define VOICE_HELLO_MSEC		500		// a hello datagram this often until the server answers one,
#define VOICE_KEEPALIVE_MSEC	5000	//	and this often after that: the router of the player keeps the way open
#define VOICE_NO_LINK_MSEC		10000	// no answer from the relay for this long: it is said once, and a masterserver is asked less often
#define VOICE_STATE_MSEC		1000	// the masterserver is told this often where this client is and whom it hears,
#define VOICE_STATE_MIN_MSEC	100		//	and when that changes at once, but not more often than this
#define VOICE_HEAR_MSEC			100		// how often it is looked at who is near, for the masterserver
#define VOICE_ROOM_MSEC			1000	//	and whether the game server started another level
#define VOICE_HEAR_MARGIN		1.1f	// who is near reaches this much further than a voice, so that nobody is cut off at its end
#define VOICE_REFUSED_MSEC		10000	// the masterserver says no for this long before it is said: a player who just left keeps his number there for a moment
#define VOICE_GAME_PORT			29070	// the port of a game server whose address names none
#define VOICE_LEAD_FRAMES		3		// frames of a speaker that wait before he is played: 60 ms for late datagrams
#define VOICE_MAX_LEAD_FRAMES	8		//	what that grows to when his voice keeps running dry
#define VOICE_SILENT_MSEC		300		// nothing of a speaker for this long: he stopped talking
#define VOICE_MIC_HOLD_MSEC		30000	// push to talk: the microphone is closed this long after the key was let go
#define VOICE_VAD_HOLD_MSEC		400		// voice activation: it goes on sending this long after the last loud frame
#define VOICE_SENT_MSEC			100		// the HUD shows the own voice this long after a frame of it went out
#define VOICE_MAX_PER_FRAME		64		// datagrams taken and frames sent in one frame of the game at most
#define VOICE_TRACE_MSEC		100		// how often the line from the listener to a speaker is looked at
#define VOICE_FAR_GAIN			0.1f	// how loud a voice still is at the end of its range
#define VOICE_MAX_PAN			0.8f	// how far a voice goes to one side: it is never gone from the other ear
#define VOICE_HEAD				24.0f	// the mouth of a player above his origin
// What the room gets of the own voice at v_voiceOwnReverb 1, against a voice next to the listener. It was 0.3,
// which the first one to play with it found too loud (2026-10-11): half of that now, the menu has a slider.
#define VOICE_OWN_REVERB_GAIN	0.15f
#define VOICE_OWN_REVERB_CAP	0.1f	//	a frame of it louder than this (Voice_Level) goes in this loud,
#define VOICE_OWN_REVERB_RISE	0.1f	//	and after a loud one the gain comes back by this much a frame at most
#define VOICE_LIP_FLOOR			0.004f	// a frame quieter than this (Voice_Level) leaves the mouth shut
#define VOICE_LIP_DECAY			0.995f	// what the loudest frame of a player lately loses with every frame after it
#define VOICE_LIP_HOLD_MSEC		120		// the mouth keeps what a frame gave it this long
#define VOICE_FILE_FRAME		( VOICE_FRAME_SAMPLES * 4 )	// v_voice_test: the bytes of one frame in a file, two channels

// The songs of the boxes of the boombox (boombox/cg_boombox.c) come over the relay as the voices of the speakers
// behind the players: speaker MAX_CLIENTS + the box. They are played like a voice, with more of them waiting
// before they start: a song has no pauses to catch up in.
#define VOICE_ALL				( MAX_CLIENTS + BOX_MAX )
#define VOICE_BOX_LEAD_FRAMES	6		// 120 ms
#define VOICE_BOX_MAX_LEAD_FRAMES	12
#define VOICE_BOX_BEATS			16		// how loud the last frames of a song were, and when they are heard

typedef struct voiceSpeaker_s {
	voiceQueue_t	queue;
	qboolean		decoderOk;
	qboolean		playing;
	int				lead;			// frames that have to wait before he is played, VOICE_LEAD_FRAMES and up
	int				waitSince;		// when the first frame came that still waits
	int				lastHeard;		// when his last datagram came
	int				concealed;		// frames in a row that were made up
	int				flags, distance;	// of his last datagram: is he in what the server shows, and how far away
	int				heard;			// frames of him that were played, for v_voice_status
	qboolean		muted;			// by this player (v_voice_mute)
	int				gainSteps;		// how much louder or quieter this player wants him, in quarters: 0 is as he is
	qboolean		muffled;		// something is between him and the listener
	int				traceTime;		//	and when that was looked at last
	qboolean		inLevel;		// what he says now is played by the engine as a sound in the level, see Voice_InLevel()
	vec3_t			mouth;			//	from here,
	float			volume;			//	this loud
	float			lipPeak;		// Voice_Lip: the loudest frame of him lately,
	int				lipLevel;		//	how far that opens his mouth now, 0 to 4,
	int				lipTime;		//	and since when
	qboolean		radio;			// what is played of him now came over the clan radio (VOICE_FLAG_RADIO): it has no place
	voiceRadio_t	radioFx;		//	and this is how it sounds (v_voiceRadioFX)
	float			beat[VOICE_BOX_BEATS];		// a box: how loud a frame of its song is (Voice_Beat()),
	int				beatAt[VOICE_BOX_BEATS];	//	and when it is heard
	int				beatNext;
} voiceSpeaker_t;

// a block of memory for a state of Opus, as well aligned as anything can ask for
typedef union voiceMem_u {
	double	align;
	void	*alignToo;
	byte	bytes[VOICE_DECODER_BYTES];
} voiceMem_t;

static struct {
	qboolean			serverHas;		// the server info says vs_voice
	qboolean			serverSays;		//	or at least says something about it: vs_voice 0 is a no, also to the masterserver
	qboolean			master;			// this server has no relay, the voice goes through the masterserver
	qboolean			local;			//	or would, but the game runs on this machine
	struct in_addr		game;			// masterserver: the address of the game server,
	int					gamePort;
	qboolean			gameKnown;
	byte				room[VOICE_ROOM_BYTES];		//	the name of its room there,
	unsigned int		hear, allow;	//	whom this client wants to hear and who may hear it, a bit per client number,
	int					roomTime, hearTime;			//	when those were looked at last,
	int					stateTime;		//	when the master was told last,
	qboolean			stateDue;		//	whether it has to be told again,
	int					refusedSince;	//	and since when it says no, 0: it does not
	qboolean			toldRefused;
	qboolean			social;			// the master knows the clan radio and the statuses (VOICE_FLAG_SOCIAL)
	qboolean			looks;			// the master hands on hats and capes (VOICE_FLAG_LOOKS, look/cg_look.c)
	byte				clan[VOICE_CLAN_BYTES];	//	and this is what it is told as the clan of this client, all zero: none
	qboolean			boxes;			// the master plays boxes of the boombox (VOICE_FLAG_BOXES),
	qboolean			boxList;		//	it knows what came for them after 0.1.8 (VOICE_FLAG_BOXLIST),
	int					boxHear;		//	and this is what it is told of the boxes this client hears, a bit each
	qboolean			asked;			// "vs_voiceLink hello" went to the server,
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
	qboolean			keyDown;		// +v_voice
	qboolean			clanDown;		// +v_voiceclan: what is said goes to the clan, over the radio
	qboolean			radio;			// the relay of the server knows the clan radio (VOICE_CAP_RADIO)
	qboolean			sendingRadio;	// the frames that go out now go over it
	qboolean			toldNoRadio;
	voiceRadio_t		ownRadio;		// the clicks of the own key
	qhandle_t			radioShader;	// the mark of a voice from the radio in the list of who talks, 0: none
	qboolean			radioShaderTried;
	qboolean			sending;
	float				ownGain;		// Voice_OwnReverb: what the cap left of the last frame
	int					sentTime;		// when the last frame of the microphone went out
	int					talkUntil;		// voice activation
	int					micUntil;		// push to talk: when the microphone is closed again

	int					test;			// v_voice_test: 1 a tone, 2 a file goes out in place of the microphone
	int					testNext;
	int					testSample;
	fileHandle_t		play;			// v_voice_test play: the file,
	int					playLeft;		//	how many bytes of it are not sent yet,
	char				playName[MAX_QPATH];	//	and its name: it is opened again when it is through
	fileHandle_t		record;			// v_voice_test record: what is played goes into this file
} voice;

static voiceSpeaker_t	voiceSpeakers[VOICE_ALL];
static voiceMem_t		voiceDecoders[VOICE_ALL];
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

A server without a relay of its own is not asked anything: there the voice goes through the masterserver
(v_voiceMasterHost), whose name is looked up the same way.

===============================================================================
*/

#ifdef __EMSCRIPTEN__

// A page has no socket to give an address to and no thread to look one up in. Its way to the relay is a
// WebSocket to the web host it came from, and the server is found when that is open.
// qtrue: it is. qfalse: not yet, or never (voice.failed).
static qboolean Voice_FindServer( void ) {
	const int open = CG_VoiceWebOpen();

	if ( open < 0 ) {
		trap->Print( "Voice chat: the web host of the server does not take it (/voice, where this page came from)\n" );
		voice.failed = qtrue;
	}
	return ( open > 0 ) ? qtrue : qfalse;
}

#else

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

/*
=================
Voice_Lookup

Where a host is. 1: *out is its address. 0: not yet, asked again with the next frame. -1: never.
=================
*/
static int Voice_Lookup( const char *host, struct in_addr *out ) {
	if ( voiceLookup.state == 1 )
		return 0;
	if ( voiceLookup.state == 2 || voiceLookup.state == 3 ) {
		const qboolean found = ( voiceLookup.state == 2 ) ? qtrue : qfalse;

		Voice_LookupJoin();
		// (what was looked up is another name: this one is asked for below)
		if ( !Q_stricmp( voiceLookup.name, host ) ) {
			if ( !found )
				return -1;
			*out = voiceLookup.result;
			return 1;
		}
	}
	if ( inet_pton( AF_INET, host, out ) == 1 )
		return 1;

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
		return -1;
	}
	return 0;
}

// "host:port" in its two parts, *port stays what it was if there is none. qfalse: an address of the newer kind,
// or none at all: the relays only have the older kind.
static qboolean Voice_SplitAddress( char *host, int hostSize, int *port ) {
	char *colon;

	if ( host[0] == '[' || !host[0] )
		return qfalse;
	colon = strrchr( host, ':' );
	if ( colon ) {
		const int named = atoi( colon + 1 );

		*colon = '\0';
		if ( named > 0 && named <= 65535 )
			*port = named;
	}
	if ( !Q_stricmp( host, "localhost" ) || !Q_stricmp( host, "loopback" ) )
		Q_strncpyz( host, "127.0.0.1", hostSize );
	return host[0] ? qtrue : qfalse;
}

// The address the game is connected to, and its port if the engine says one. 1, 0 and -1 as Voice_Lookup(),
// and what went wrong is said here.
static int Voice_GameServer( struct in_addr *out, int *port ) {
	char	host[256];
	int		found;

	trap->Cvar_VariableStringBuffer( "cl_currentServerIP", host, sizeof( host ) );
	if ( !host[0] )
		trap->Cvar_VariableStringBuffer( "cl_currentServerAddress", host, sizeof( host ) );
	if ( !Voice_SplitAddress( host, sizeof( host ), port ) ) {
		trap->Print( "Voice chat: no address of the server that it could use\n" );
		return -1;
	}
	found = Voice_Lookup( host, out );
	if ( found < 0 )
		trap->Print( "Voice chat: the address of the server could not be looked up\n" );
	return found;
}

// Finds out where the relay is. qtrue: voice.server is set. qfalse: not yet, or never (voice.failed).
static qboolean Voice_FindServer( void ) {
	char	host[256];
	int		port = 0, found;

	if ( !voice.master ) {
		// the game server relays itself, on the port it named
		found = Voice_GameServer( &voice.server.sin_addr, &port );
		if ( found < 0 )
			voice.failed = qtrue;
		return ( found > 0 ) ? qtrue : qfalse;
	}

	// The masterserver. The room there is named after the game server (Voice_MasterRoom), so that one first.
	if ( !voice.gameKnown ) {
		voice.gamePort = VOICE_GAME_PORT;
		found = Voice_GameServer( &voice.game, &voice.gamePort );
		if ( found < 0 )
			voice.failed = qtrue;
		if ( found <= 0 )
			return qfalse;
		if ( ( ntohl( voice.game.s_addr ) >> 24 ) == 127 ) {
			// a game on this machine that is not of this tree: nobody else is in it
			voice.failed = voice.local = qtrue;
			return qfalse;
		}
		voice.gameKnown = qtrue;
	}
	Q_strncpyz( host, v_voiceMasterHost.string, sizeof( host ) );
	port = VOICE_MASTER_PORT;
	found = Voice_SplitAddress( host, sizeof( host ), &port ) ? Voice_Lookup( host, &voice.server.sin_addr ) : -1;
	if ( found < 0 ) {
		trap->Print( "Voice chat: the masterserver \"%s\" could not be looked up (v_voiceMasterHost)\n", v_voiceMasterHost.string );
		voice.failed = qtrue;
	}
	if ( found <= 0 )
		return qfalse;
	voice.port = port;
	return qtrue;
}

#endif // __EMSCRIPTEN__

/*
===============================================================================

Start and end

===============================================================================
*/

static void Voice_Close( void ) {
	const qboolean	serverHas = voice.serverHas, serverSays = voice.serverSays;
	int				i;

	if ( voice.record ) {
		trap->FS_Close( voice.record );
		voice.record = 0;
	}
	if ( voice.play ) {
		trap->FS_Close( voice.play );
		voice.play = 0;
	}
	CG_VoiceAudioShutdown();
#ifdef __EMSCRIPTEN__
	CG_VoiceWebClose();
#else
	Voice_LookupJoin();
	if ( voice.sockOpen )
		BG_CloseSocket( voice.sock );
#endif
	// the boxes the masterserver played are gone with the way to it
	if ( voice.boxes )
		CG_BoomboxMasterReady( qfalse );
	// who was muted stays muted while the module does
	for ( i = 0; i < VOICE_ALL; i++ ) {
		const qboolean	muted = voiceSpeakers[i].muted;
		const int		gainSteps = voiceSpeakers[i].gainSteps;

		memset( &voiceSpeakers[i], 0, sizeof( voiceSpeakers[i] ) );
		voiceSpeakers[i].muted = muted;
		voiceSpeakers[i].gainSteps = gainSteps;
	}
	memset( &voice, 0, sizeof( voice ) );
	voice.sock = BG_INVALID_SOCKET;
	// the server is still the same one: switched on again, the chat asks it anew
	voice.serverHas = serverHas;
	voice.serverSays = serverSays;
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

// From the server info, the value of vs_voice: does this server have a voice chat? Only then is it asked for one.
// A server that does not say the key at all has no relay, and the voice goes through the masterserver. One that
// says 0 wants no voice chat, and that holds for the master too.
void CG_VoiceServerInfo( const char *value ) {
	voice.serverHas = atoi( value ) ? qtrue : qfalse;
	voice.serverSays = value[0] ? qtrue : qfalse;
}

/*
=================
Voice_Open

The relay is known: its port, and with it the token and the range, or that it is the masterserver. From here
on there is a socket, and the speakers are open. qfalse: something is missing for good (voice.failed).
=================
*/
static qboolean Voice_Open( int port ) {
	int i;

	// the codec. Its states live in memory of this file, so what they need has to fit.
	if ( opus_encoder_get_size( 1 ) > (int)sizeof( voiceEncoder ) || opus_decoder_get_size( 1 ) > (int)sizeof( voiceDecoders[0] )
		|| opus_encoder_init( (OpusEncoder *)&voiceEncoder, VOICE_RATE, 1, OPUS_APPLICATION_VOIP ) != OPUS_OK ) {
		trap->Print( "Voice chat: the codec does not start\n" );
		voice.failed = qtrue;
		return qfalse;
	}
	opus_encoder_ctl( (OpusEncoder *)&voiceEncoder, OPUS_SET_BITRATE( VOICE_BITRATE ) );
	opus_encoder_ctl( (OpusEncoder *)&voiceEncoder, OPUS_SET_SIGNAL( OPUS_SIGNAL_VOICE ) );
	// every frame carries a small copy of the one before it, so one lost datagram can be made good
	opus_encoder_ctl( (OpusEncoder *)&voiceEncoder, OPUS_SET_INBAND_FEC( 1 ) );
	opus_encoder_ctl( (OpusEncoder *)&voiceEncoder, OPUS_SET_PACKET_LOSS_PERC( 10 ) );
	voice.encoderOk = qtrue;
	for ( i = 0; i < VOICE_ALL; i++ )
		voiceSpeakers[i].decoderOk = ( opus_decoder_init( (OpusDecoder *)&voiceDecoders[i], VOICE_RATE, 1 ) == OPUS_OK ) ? qtrue : qfalse;

#ifndef __EMSCRIPTEN__		// (the WebSocket of a page is asked for by Voice_FindServer)
	BG_NetStart();
	voice.sock = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( voice.sock == BG_INVALID_SOCKET ) {
		trap->Print( "Voice chat: no socket\n" );
		voice.failed = qtrue;
		return qfalse;
	}
	voice.sockOpen = qtrue;
	BG_SocketNonBlocking( voice.sock );
	// (the address of the masterserver is in there by now, the one of a game server follows: Voice_FindServer)
	voice.server.sin_family = AF_INET;
	voice.server.sin_port = htons( (unsigned short)port );
#else
	(void)port;
#endif

	if ( !CG_VoiceAudioInit( v_voiceBackend.string ) || !CG_VoiceAudioOpenPlayback( v_voicePlayback.string ) ) {
		trap->Print( "Voice chat: no sound device to play on, you will not hear the others\n" );
	} else {
		voice.audio = qtrue;
		for ( i = 0; i < VOICE_ALL; i++ )
			CG_VoiceAudioMix( i, v_voiceVolume.value, v_voiceVolume.value, 0.0f );
	}
	voice.answered = qtrue;
	// (switched off, the way is open for the songs of the boombox alone)
	if ( v_voice.integer )
		trap->Print( "Voice chat: on. bind a key to +v_voice to talk (v_voiceMode 2 sends whenever you speak)\n" );
	return qtrue;
}

/*
=================
CG_VoiceServerCommand

vs_voiceLink <protocol> <port> <token> <range>: the answer of the server to "vs_voiceLink hello".
=================
*/
void CG_VoiceServerCommand( void ) {
	const int	protocol = atoi( CG_Argv( 1 ) ), port = atoi( CG_Argv( 2 ) );
	const unsigned int token = (unsigned int)strtoul( CG_Argv( 3 ), NULL, 10 );

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
	// what the relay knows beyond the protocol, nothing from a server of before
	voice.radio = ( atoi( CG_Argv( 5 ) ) & VOICE_CAP_RADIO ) ? qtrue : qfalse;
	Voice_Open( port );
}

// Does the voice go through the masterserver on this server? Only where the server says nothing about a voice
// chat: one of this tree relays itself or says no with vs_voice 0, and so can any other (sets vs_voice 0).
static qboolean Voice_WantsMaster( void ) {
#ifdef __EMSCRIPTEN__
	// a page can not send UDP, and the servers it can play on are of this tree
	return qfalse;
#else
	// (also for the boxes of the boombox alone: the master plays them)
	return ( !voice.serverSays && ( v_voice.integer || v_boomboxes.integer ) && v_voiceMaster.integer && !cg.demoPlayback ) ? qtrue : qfalse;
#endif
}

#ifndef __EMSCRIPTEN__
/*
=================
Voice_MasterStart

Nobody to ask on this server: the voice goes through the masterserver, once it is known where that is. The
player is told, since the master is another machine than the one he chose to play on. qtrue: it is open.
=================
*/
static qboolean Voice_MasterStart( int now ) {
	voice.master = qtrue;
	if ( !voice.serverKnown ) {
		voice.serverKnown = Voice_FindServer();
		if ( !voice.serverKnown )
			return qfalse;
	}
	voice.range = VOICE_MASTER_RANGE;
	voice.askTime = now;
	trap->Print( "Voice chat: this server has none of its own, so it goes through the masterserver %s (v_voiceMaster 0: not at all)\n",
		v_voiceMasterHost.string );
	return Voice_Open( voice.port );
}
#endif

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
#ifdef __EMSCRIPTEN__
		CG_VoiceWebSend( datagram, len );
#else
		sendto( voice.sock, (const char *)datagram, len, BG_SEND_FLAGS, (const struct sockaddr *)&voice.server, sizeof( voice.server ) );
#endif
}

// The engine of this tree says in a cvar what its sound (v_spatial) takes from the voice chat. Only when the
// cvar is 1 is the call that goes with it there at all: any other engine ends trap->ext before it.
static qboolean Voice_EngineSays( const char *cvar ) {
	char value[8];

	trap->Cvar_VariableStringBuffer( cvar, value, sizeof( value ) );
	return ( atoi( value ) == 1 ) ? qtrue : qfalse;
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
Voice_Lip

The game opens the mouth of a character by how loud the line is that he says, in four steps, which the engine
works out for the sounds on its voice channels (S_GetVoiceVolume). A voice of the chat is no sound of the
engine, so the same is worked out here for every frame that is played, and for the own player for every frame
that goes out: how loud it is against the loudest frame of that player lately, since no two microphones are
alike. CG_G2PlayerHeadAnims() asks CG_VoiceLip().

A frame is heard a little after it is decoded, so the mouth is ahead of the voice by what is kept ahead of the
sound device.
=================
*/
static void Voice_Lip( int client, const short *samples ) {
	voiceSpeaker_t	*sp = &voiceSpeakers[client];
	const float		level = Voice_Level( samples );
	float			part;

	sp->lipPeak *= VOICE_LIP_DECAY;
	if ( level > sp->lipPeak )
		sp->lipPeak = level;
	sp->lipTime = trap->Milliseconds();
	if ( level < VOICE_LIP_FLOOR ) {
		sp->lipLevel = 0;
		return;
	}
	part = level / sp->lipPeak;
	sp->lipLevel = ( part > 0.75f ) ? 4 : ( part > 0.5f ) ? 3 : ( part > 0.25f ) ? 2 : 1;
}

int CG_VoiceLip( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS )
		return 0;
	return ( trap->Milliseconds() - voiceSpeakers[clientNum].lipTime < VOICE_LIP_HOLD_MSEC ) ? voiceSpeakers[clientNum].lipLevel : 0;
}

// What this player says, for an engine that lets the room he stands in ring from it. He does not hear his
// voice, only the room. Not for a spectator and not while he follows somebody (the listener is not where his
// mouth is then), and not in a chat without a range, where no voice is in the level.
//
// The room is in his ears a moment after the word, while he talks on, so it gets little of him
// (VOICE_OWN_REVERB_GAIN) and has a cap: a frame that is louder than VOICE_OWN_REVERB_CAP is turned down to it
// within that frame, and what is turned down comes back slowly, so that the room does not pump with every word.
static void Voice_OwnReverb( const short *samples ) {
	short	capped[VOICE_FRAME_SAMPLES];
	float	level, gain, step;
	int		i;

	if ( v_voiceOwnReverb.value <= 0.0f || voice.range <= 0 || !cg.snap || cg.snap->ps.clientNum != cg.clientNum
		|| cgs.clientinfo[cg.clientNum].team == TEAM_SPECTATOR )
		return;
	if ( !Voice_EngineSays( "v_spatialOwnVoice" ) || !trap->ext.S_SpatialOwnVoice )
		return;

	level = Voice_Level( samples );
	gain = ( level > VOICE_OWN_REVERB_CAP ) ? VOICE_OWN_REVERB_CAP / level : 1.0f;
	if ( gain > voice.ownGain + VOICE_OWN_REVERB_RISE )
		gain = voice.ownGain + VOICE_OWN_REVERB_RISE;
	step = ( gain - voice.ownGain ) * ( 1.0f / VOICE_FRAME_SAMPLES );
	for ( i = 0; i < VOICE_FRAME_SAMPLES; i++ )
		capped[i] = (short)( samples[i] * ( voice.ownGain + step * ( i + 1 ) ) );
	voice.ownGain = gain;
	trap->ext.S_SpatialOwnVoice( v_voiceVolume.value * v_voiceOwnReverb.value * VOICE_OWN_REVERB_GAIN, capped, VOICE_FRAME_SAMPLES,
		VOICE_RATE );
}

// one frame of sound goes out
// type: VOICE_AUDIO for everybody in range, VOICE_RADIO for the clan
static void Voice_SendFrame( const short *samples, int type ) {
	byte		frame[VOICE_MAX_FRAME];
	const int	len = opus_encode( (OpusEncoder *)&voiceEncoder, samples, VOICE_FRAME_SAMPLES, frame, sizeof( frame ) );

	// he talks in the room he stands in either way
	Voice_OwnReverb( samples );
	Voice_Lip( cg.clientNum, samples );

	// two bytes and less is a frame the codec found nothing in
	if ( len > 2 ) {
		Voice_Send( type, frame, len );
		voice.seq = ( voice.seq + 1 ) & 0xffff;
	}
}

/*
=================
Voice_RadioKey

+voiceclan is held, or not: whether what is said now goes over the clan radio. It does where the relay knows the
radio (the one of the server, or the masterserver), for a player who has a clan password: the server decides
who that reaches, the master goes by what Voice_MasterClan() made of the password. The key cracks when it is
pressed and when it is let go, as the key of a radio does, so the player knows he is on it.
=================
*/
static qboolean Voice_RadioKey( void ) {
	char		pass[64];
	qboolean	on = voice.clanDown;

	if ( on && !voice.radio ) {
		if ( !voice.toldNoRadio ) {
			voice.toldNoRadio = qtrue;
			trap->Print( "Voice chat: %s has no clan radio.\n", voice.master ? "the masterserver" : "this server" );
		}
		on = qfalse;
	}
	if ( on ) {
		// "none" is what the cvar has until the player sets it, and what the server takes for no password
		trap->Cvar_VariableStringBuffer( "cp_clanPwd", pass, sizeof( pass ) );
		if ( !pass[0] || !Q_stricmp( pass, "none" ) ) {
			if ( !voice.toldNoRadio ) {
				voice.toldNoRadio = qtrue;
				trap->Print( "Voice chat: the clan radio reaches the players with your clan password, and you have none (cp_clanPwd).\n" );
			}
			on = qfalse;
		}
	}
	if ( on != voice.sendingRadio ) {
		voice.sendingRadio = on;
		if ( v_voiceRadioFX.integer && voice.audio ) {
			short	click[VOICE_RADIO_CLICK];
			const int count = BG_VoiceRadioClick( &voice.ownRadio, click, VOICE_RADIO_CLICK, on ? qfalse : qtrue );

			// the place of this client among the voices is free: nobody hears himself
			CG_VoiceAudioMix( cg.clientNum, v_voiceVolume.value * 0.5f, v_voiceVolume.value * 0.5f, 0.0f );
			CG_VoiceAudioWrite( cg.clientNum, click, count );
		}
	}
	return on;
}

// v_voice_test play: the file from its start. qfalse: there is not one frame in it.
static qboolean Voice_TestOpen( void ) {
	if ( voice.play )
		trap->FS_Close( voice.play );
	voice.play = 0;
	voice.playLeft = trap->FS_Open( voice.playName, &voice.play, FS_READ );
	if ( voice.play && voice.playLeft >= VOICE_FILE_FRAME )
		return qtrue;
	if ( voice.play )
		trap->FS_Close( voice.play );
	voice.play = 0;
	return qfalse;
}

// v_voice_test: the next frame that goes out in place of the microphone. qfalse: the file is gone, the test ended.
static qboolean Voice_TestFrame( short *samples ) {
	byte	raw[VOICE_FILE_FRAME];
	int		i;

	if ( voice.test == 1 ) {
		for ( i = 0; i < VOICE_FRAME_SAMPLES; i++, voice.testSample++ ) {
			samples[i] = (short)( 6000.0f * sinf( voice.testSample * ( 2.0f * M_PI * 440.0f / VOICE_RATE ) )
				+ 6000.0f * sinf( voice.testSample * ( 2.0f * M_PI * 3000.0f / VOICE_RATE ) ) );
		}
		return qtrue;
	}

	// The file again from its start when it is through. What is left of it is counted here: jamp does not say
	// how much a read gave.
	if ( voice.playLeft < VOICE_FILE_FRAME && !Voice_TestOpen() ) {
		voice.test = 0;
		return qfalse;
	}
	trap->FS_Read( raw, sizeof( raw ), voice.play );
	voice.playLeft -= (int)sizeof( raw );
	for ( i = 0; i < VOICE_FRAME_SAMPLES; i++ ) {
		const int	left = (short)( raw[i * 4] | ( raw[i * 4 + 1] << 8 ) );
		const int	right = (short)( raw[i * 4 + 2] | ( raw[i * 4 + 3] << 8 ) );

		samples[i] = (short)( ( left + right ) / 2 );
	}
	return qtrue;
}

/*
=================
Voice_Capture

Push to talk (v_voiceMode 1): the microphone is opened when the key goes down, which takes a moment the
first time, and is closed again when it has not been used for a while. A player who never presses the key
never has his microphone open.

Voice activation (v_voiceMode 2): the microphone is open all the time, and what is louder than
v_voiceThreshold is sent.
=================
*/
static void Voice_Capture( int now ) {
	const qboolean	activation = ( v_voiceMode.integer == 2 ) ? qtrue : qfalse;
	// the radio is always by its key, also for a player whose voice is sent as soon as he talks
	const qboolean	radio = Voice_RadioKey();
	const qboolean	wanted = ( activation || voice.keyDown || radio || now < voice.micUntil ) ? qtrue : qfalse;
	short			samples[VOICE_FRAME_SAMPLES];
	int				count, i;

	if ( voice.test ) {
		// a tone or a file in place of the microphone, a frame for every 20 ms that went by
		if ( !voice.testNext || now - voice.testNext > 500 )
			voice.testNext = now;
		for ( count = 0; count < VOICE_MAX_PER_FRAME && now >= voice.testNext && Voice_TestFrame( samples ); count++ ) {
			Voice_SendFrame( samples, radio ? VOICE_RADIO : VOICE_AUDIO );
			voice.testNext += VOICE_FRAME_MSEC;
		}
		voice.sending = voice.test ? qtrue : qfalse;
		return;
	}

	if ( !wanted ) {
		CG_VoiceAudioCloseCapture();
		voice.sending = qfalse;
		return;
	}
	if ( !CG_VoiceAudioCapturing() ) {
		if ( !CG_VoiceAudioInit( v_voiceBackend.string ) || !CG_VoiceAudioOpenCapture( v_voiceCapture.string ) ) {
#ifdef __EMSCRIPTEN__
			const char *why;

			// the browser is asking the player whether this page may have the microphone: his answer is not
			// waited for, the next frame looks again
			if ( CG_VoiceAudioAsking() )
				return;
			why = CG_VoiceAudioNoMicrophone();
			if ( voice.keyDown || voice.clanDown || activation )
				trap->Print( "Voice chat: this page has no microphone: %s\n", why ? why : "it does not open" );
#else
			if ( voice.keyDown || voice.clanDown || activation )
				trap->Print( "Voice chat: no microphone (v_voice_devices lists what there is, v_voiceCapture chooses)\n" );
#endif
			voice.keyDown = voice.clanDown = qfalse;
			voice.micUntil = 0;
			if ( activation )
				trap->Cvar_Set( "v_voiceMode", "1" );
			return;
		}
	}
	if ( voice.keyDown || radio )
		voice.micUntil = now + VOICE_MIC_HOLD_MSEC;

	for ( count = 0; count < VOICE_MAX_PER_FRAME && CG_VoiceAudioRead( samples, VOICE_FRAME_SAMPLES ); count++ ) {
		if ( v_voiceMicGain.value != 1.0f ) {
			for ( i = 0; i < VOICE_FRAME_SAMPLES; i++ )
				samples[i] = (short)Com_Clamp( -32767.0f, 32767.0f, samples[i] * v_voiceMicGain.value );
		}
		if ( activation && Voice_Level( samples ) > v_voiceThreshold.value )
			voice.talkUntil = now + VOICE_VAD_HOLD_MSEC;
		if ( radio || ( activation ? ( now < voice.talkUntil ) : voice.keyDown ) ) {
			Voice_SendFrame( samples, radio ? VOICE_RADIO : VOICE_AUDIO );
			voice.sending = qtrue;
			voice.sentTime = now;
		}
	}
	// the microphone gives a frame every 20 ms and the game draws more often than that: the mark on the HUD
	// must not go out in the frames between
	if ( now - voice.sentTime > VOICE_SENT_MSEC )
		voice.sending = qfalse;
	// the game stood still for a while and more piled up than is worth sending late
	if ( count == VOICE_MAX_PER_FRAME )
		CG_VoiceAudioDropCapture();
}

/*
===============================================================================

In: the voices of the others

===============================================================================
*/

/*
=================
Voice_MasterAnswer

What the masterserver says to a VOICE_STATE. With another token than the one it was sent with: that is the
token this address has there for its room and number, and the master keeps nothing until it is told again
with it. With the same token: this client is in, or the flags say why not.
=================
*/
static void Voice_MasterAnswer( const voicePacket_t *p, int now ) {
	const qboolean social = ( p->flags & VOICE_FLAG_SOCIAL ) ? qtrue : qfalse;
	const qboolean boxes = ( social && ( p->flags & VOICE_FLAG_BOXES ) ) ? qtrue : qfalse;

	if ( p->client != cg.clientNum )
		return;
	voice.boxList = ( boxes && ( p->flags & VOICE_FLAG_BOXLIST ) ) ? qtrue : qfalse;
	voice.looks = ( social && ( p->flags & VOICE_FLAG_LOOKS ) ) ? qtrue : qfalse;
	// A master that knows the radio is told the clan of this client from now on, in the long state
	if ( social != voice.social ) {
		voice.social = voice.radio = social;
		voice.toldNoRadio = qfalse;
		voice.stateDue = qtrue;
	}
	// A master that plays boxes is told the boxes this client hears, in the state with the boxes, and gets what
	// its player does with them (boombox/cg_boombox.c)
	if ( boxes != voice.boxes ) {
		voice.boxes = boxes;
		voice.boxHear = 0;
		voice.stateDue = qtrue;
		CG_BoomboxMasterReady( boxes );
	}
	if ( p->token != voice.token ) {
		voice.token = p->token;
		voice.linked = qfalse;
		voice.stateDue = qtrue;
		return;
	}
	if ( p->flags & ( VOICE_FLAG_TAKEN | VOICE_FLAG_FULL ) ) {
		voice.linked = qfalse;
		if ( !voice.refusedSince )
			voice.refusedSince = now ? now : 1;
		if ( !voice.toldRefused && now - voice.refusedSince > VOICE_REFUSED_MSEC ) {
			trap->Print( "Voice chat: the masterserver does not let you in, %s\n", ( p->flags & VOICE_FLAG_TAKEN )
				? "another player of this server speaks there under your client number" : "it is full" );
			voice.toldRefused = qtrue;
		}
		return;
	}
	voice.linked = qtrue;
	voice.refusedSince = 0;
	voice.toldRefused = qfalse;
}

static void Voice_Receive( int now ) {
	int count;

	for ( count = 0; count < VOICE_MAX_PER_FRAME; count++ ) {
		byte				datagram[VOICE_MAX_DATAGRAM + 1];	// one more: a longer one shows as too long
		voicePacket_t		p;
		voiceSpeaker_t		*sp;
#ifdef __EMSCRIPTEN__
		// what the WebSocket brought since the last frame. Only the web host of the server is at its other end.
		const int			len = CG_VoiceWebReceive( datagram, sizeof( datagram ) );

		if ( len < 0 )
			break;
#else
		struct sockaddr_in	from;
		socklen_t			fromLen = sizeof( from );
		const int			len = (int)recvfrom( voice.sock, (char *)datagram, sizeof( datagram ), 0, (struct sockaddr *)&from, &fromLen );

		if ( len < 0 ) {
			if ( BG_SocketReset() )
				continue;
			break;
		}
		// only the server speaks here: its address, its port, and the token it gave this client
		if ( fromLen < (socklen_t)sizeof( from ) || from.sin_addr.s_addr != voice.server.sin_addr.s_addr
			|| from.sin_port != voice.server.sin_port )
			continue;
#endif
		if ( !BG_VoiceUnpack( datagram, len, qtrue, &p ) )
			continue;
		if ( p.type == VOICE_STATE ) {
			// only the masterserver says these, and its first one names the token
			if ( voice.master )
				Voice_MasterAnswer( &p, now );
			continue;
		}
		if ( p.token != voice.token )
			continue;
		if ( p.type == VOICE_HELLO ) {
			if ( !voice.master )
				voice.linked = qtrue;
			continue;
		}
		if ( p.type == VOICE_BOX ) {
			// what a box of the masterserver is now (boombox/cg_boombox.c)
			if ( voice.master && voice.boxes )
				CG_BoomboxFromMaster( p.data, p.dataLen );
			continue;
		}
		if ( p.client >= VOICE_ALL || p.client == cg.clientNum )
			continue;
		if ( p.client >= MAX_CLIENTS ) {
			// the song of a box of the boombox: from the relay of a game server, or from a master that plays boxes
			if ( ( voice.master && !voice.boxes ) || p.type != VOICE_AUDIO || !CG_BoomboxHears( p.client - MAX_CLIENTS ) )
				continue;
		} else {
			// (the voice chat is off, the way is open for the songs alone)
			if ( !v_voice.integer )
				continue;
			// whoever speaks here plays with a client of this tree (status/cg_peers.c)
			CG_PeerHeard( p.client );
			if ( p.type == VOICE_STATUS ) {
				// what stands above the head of somebody, handed on by the masterserver (status/cg_status.c)
				char		text[VOICE_STATUS_TEXT];
				qboolean	away;

				if ( voice.master && BG_VoiceStatusUnpack( &p, &away, text, sizeof( text ) ) )
					CG_StatusFromMaster( p.client, away, text );
				continue;
			}
			if ( p.type == VOICE_LOOK ) {
				// the hat and the cape of somebody, handed on by the masterserver (look/cg_look.c)
				char text[VOICE_STATUS_TEXT];

				if ( voice.master && BG_VoiceLookUnpack( &p, text, sizeof( text ) ) )
					CG_LookFromMaster( p.client, text );
				continue;
			}
		}
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

/*
=================
Voice_InLevel

This engine with v_spatial on plays every sound in a level through walls, with the ring of the room and round
corners. It takes the voice of a player too, if it is told where he is: then what is decoded of him goes to the
engine and not to the sound device of this file, and the direction, the fall with the distance to nothing and
the muffling of Voice_Place() are left to it. Any other engine has no such call, a speaker the server does not
show has no place, and a chat without a range has none on purpose: those are played here as before. So is every
voice for a player who does not want voices muffled (v_voiceMuffle 0): a wall of the engine can not be put off.

The engine says that it takes voices in the cvar v_spatialVoice. Only then is the call there at all.
=================
*/
static qboolean Voice_EngineTakesVoices( void ) {
	return ( Voice_EngineSays( "v_spatialVoice" ) && trap->ext.S_SpatialVoice ) ? qtrue : qfalse;
}

static qboolean Voice_InLevel( int speaker ) {
	const voiceSpeaker_t	*sp = &voiceSpeakers[speaker];
	const centity_t			*cent = &cg_entities[speaker];
	char					boxes[8];

	// a box of the boombox, in an engine that takes that many (v_spatialBoxes)
	if ( speaker >= MAX_CLIENTS ) {
		trap->Cvar_VariableStringBuffer( "v_spatialBoxes", boxes, sizeof( boxes ) );
		if ( atoi( boxes ) <= speaker - MAX_CLIENTS || !Voice_EngineTakesVoices() )
			return qfalse;
		return ( trap->ext.S_SpatialVoice( speaker, NULL, 0.0f, NULL, 0, VOICE_RATE ) >= 0 ) ? qtrue : qfalse;
	}
	if ( voice.range <= 0 || cgs.clientinfo[speaker].team == TEAM_SPECTATOR || !cent->currentValid
		|| ( sp->flags & ( VOICE_FLAG_UNSEEN | VOICE_FLAG_RADIO ) ) )
		return qfalse;
	if ( !v_voiceMuffle.integer || !Voice_EngineTakesVoices() )
		return qfalse;
	return ( trap->ext.S_SpatialVoice( speaker, NULL, 0.0f, NULL, 0, VOICE_RATE ) >= 0 ) ? qtrue : qfalse;
}

// how much of a speaker is decoded and waits to be heard, in samples
static int Voice_Queued( int speaker ) {
	if ( voiceSpeakers[speaker].inLevel ) {
		const int queued = trap->ext.S_SpatialVoice( speaker, NULL, voiceSpeakers[speaker].volume, NULL, 0, VOICE_RATE );

		// the engine stopped taking voices (the level changes): what he says goes on here
		if ( queued >= 0 )
			return queued;
		voiceSpeakers[speaker].inLevel = qfalse;
	}
	return CG_VoiceAudioQueued( speaker );
}

// For the speakers of a box that pulse with its song (boombox/cg_boombox.c): how loud a frame of it is that was
// just decoded, and when it is heard, which is after what waits of it already.
static void Voice_Beat( int speaker, const short *samples ) {
	voiceSpeaker_t	*sp = &voiceSpeakers[speaker];
	const int		slot = sp->beatNext++ % VOICE_BOX_BEATS;

	sp->beat[slot] = Voice_Level( samples );
	sp->beatAt[slot] = trap->Milliseconds() + Voice_Queued( speaker ) * 1000 / VOICE_RATE;
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
		sp->heard++;
	} else {
		// lost: from the copy the frame after it carries, or guessed from what came before
		got = after ? opus_decode( decoder, after, afterLen, samples, VOICE_FRAME_SAMPLES, 1 )
			: opus_decode( decoder, NULL, 0, samples, VOICE_FRAME_SAMPLES, 0 );
		sp->concealed++;
	}
	// a frame of another length than this chat uses, or one the codec refuses, is silence
	if ( got != VOICE_FRAME_SAMPLES )
		memset( samples, 0, sizeof( samples ) );
	if ( speaker < MAX_CLIENTS )
		Voice_Lip( speaker, samples );
	else
		Voice_Beat( speaker, samples );
	if ( sp->inLevel && trap->ext.S_SpatialVoice( speaker, sp->mouth, sp->volume, samples, VOICE_FRAME_SAMPLES, VOICE_RATE ) >= 0 )
		return;
	if ( sp->radio && v_voiceRadioFX.integer )
		BG_VoiceRadio( &sp->radioFx, samples, VOICE_FRAME_SAMPLES );
	CG_VoiceAudioWrite( speaker, samples, VOICE_FRAME_SAMPLES );
}

static void Voice_Mouth( int speaker );

// The crack of a radio in front of what somebody says over it, and the rush that is cut off behind it
static void Voice_RadioClick( int speaker, qboolean end ) {
	short click[VOICE_RADIO_CLICK];

	if ( !v_voiceRadioFX.integer )
		return;
	CG_VoiceAudioWrite( speaker, click, BG_VoiceRadioClick( &voiceSpeakers[speaker].radioFx, click, VOICE_RADIO_CLICK, end ) );
}

// Does what he says now come over the radio? A speaker can change from one to the other in the middle of a word
// (he has two keys), so this is looked at with every frame of the game and not only when he starts to talk.
static void Voice_RadioState( int speaker ) {
	voiceSpeaker_t	*sp = &voiceSpeakers[speaker];
	const qboolean	radio = ( sp->playing && ( sp->flags & VOICE_FLAG_RADIO ) ) ? qtrue : qfalse;

	if ( radio == sp->radio )
		return;
	sp->radio = radio;
	if ( radio ) {
		// from here on it has no place: not through the engine, which would put it where he stands
		sp->inLevel = qfalse;
		Voice_RadioClick( speaker, qfalse );
	} else {
		Voice_RadioClick( speaker, qtrue );
		if ( sp->playing ) {
			sp->inLevel = Voice_InLevel( speaker );
			if ( sp->inLevel )
				Voice_Mouth( speaker );
		}
	}
}

// Where the mouth of a speaker is and how loud he is, for a voice the engine plays in the level. How loud at
// his distance is the fall of this chat (vs_voiceRange), the engine does not let it fall again.
static void Voice_Mouth( int speaker ) {
	voiceSpeaker_t	*sp = &voiceSpeakers[speaker];
	const centity_t	*cent = &cg_entities[speaker];
	float			distance, gain;

	// A box of the boombox: where its speakers are, as loud as the player wants songs and the box is set, and
	// down to nothing at the end of its range, where the server stops sending it.
	if ( speaker >= MAX_CLIENTS ) {
		if ( !CG_BoomboxMouth( speaker - MAX_CLIENTS, sp->mouth, &gain ) )
			gain = 0.0f;
		distance = Com_Clamp( 0.0f, 1.0f, 1.0f - Distance( sp->mouth, cg.refdef.vieworg ) / CG_BoomboxRange() );
		sp->volume = gain * distance * distance;
		return;
	}
	// a speaker the server stopped showing in the middle of a word stays where he was
	if ( cent->currentValid ) {
		VectorCopy( cent->lerpOrigin, sp->mouth );
		sp->mouth[2] += VOICE_HEAD;
	}
	distance = Com_Clamp( 0.0f, 1.0f, 1.0f - Distance( sp->mouth, cg.refdef.vieworg ) / voice.range );
	sp->volume = v_voiceVolume.value * ( 1.0f + 0.25f * sp->gainSteps ) * ( VOICE_FAR_GAIN + ( 1.0f - VOICE_FAR_GAIN ) * distance * distance );
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

	for ( i = 0; i < VOICE_ALL; i++ ) {
		voiceSpeaker_t	*sp = &voiceSpeakers[i];
		const int		span = BG_VoiceQueueSpan( &sp->queue );
		const int		leastLead = ( i >= MAX_CLIENTS ) ? VOICE_BOX_LEAD_FRAMES : VOICE_LEAD_FRAMES;
		const int		mostLead = ( i >= MAX_CLIENTS ) ? VOICE_BOX_MAX_LEAD_FRAMES : VOICE_MAX_LEAD_FRAMES;

		if ( !sp->playing ) {
			if ( !span ) {
				// his voice ran dry in a word over the radio and did not come back: the radio is let go
				if ( sp->radio && now - sp->lastHeard > VOICE_SILENT_MSEC )
					Voice_RadioState( i );
				continue;
			}
			if ( sp->lead < leastLead )
				sp->lead = leastLead;
			if ( span < sp->lead && now - sp->waitSince < sp->lead * VOICE_FRAME_MSEC )
				continue;
			sp->playing = qtrue;
			sp->concealed = 0;
			// decided when he starts to talk and kept until he stops: a voice does not change over in a word
			sp->inLevel = Voice_InLevel( i );
			if ( sp->inLevel )
				Voice_Mouth( i );
		}
		Voice_RadioState( i );

#ifdef __EMSCRIPTEN__
		// (the sound device of a page takes VOICE_PAGE_PERIOD samples at a time, which have to wait there too)
		for ( count = 0; count < VOICE_QUEUE && Voice_Queued( i ) < ahead + ( sp->inLevel ? 0 : VOICE_PAGE_PERIOD ); count++ ) {
#else
		for ( count = 0; count < VOICE_QUEUE && Voice_Queued( i ) < ahead; count++ ) {
#endif
			if ( BG_VoiceQueueSpan( &sp->queue ) ) {
				Voice_PlayFrame( i, qfalse );
			} else if ( now - sp->lastHeard < 3 * VOICE_FRAME_MSEC && sp->concealed < 3 ) {
				// his next datagram is a little late: bridged
				Voice_PlayFrame( i, qtrue );
			} else {
				break;
			}
		}

		if ( !BG_VoiceQueueSpan( &sp->queue ) && !Voice_Queued( i ) ) {
			if ( now - sp->lastHeard > VOICE_SILENT_MSEC ) {
				// he stopped talking
				sp->playing = qfalse;
				Voice_RadioState( i );
				BG_VoiceQueueReset( &sp->queue );
				if ( sp->lead > leastLead )
					sp->lead--;
			} else if ( sp->concealed >= 3 ) {
				// his voice ran dry while he talks: the way from him is uneven, more of it waits next time
				sp->playing = qfalse;
				if ( sp->lead < mostLead )
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

// Where a player is, as far as the game shows him to this client. qfalse: it does not.
static qboolean Voice_Where( int client, vec3_t origin ) {
	if ( cg.snap && client == cg.snap->ps.clientNum ) {
		// this player himself, or the one he follows as a spectator: that one is no entity of what the server sends
		VectorCopy( cg.predictedPlayerState.origin, origin );
		return qtrue;
	}
	if ( !cg_entities[client].currentValid )
		return qfalse;
	VectorCopy( cg_entities[client].lerpOrigin, origin );
	return qtrue;
}

// Is something between the listener and this speaker, who stands at origin: a wall, a closed door, or the
// surface of water?
static qboolean Voice_Blocked( int speaker, const vec3_t origin ) {
	vec3_t	mouth;
	trace_t	tr;

	VectorCopy( origin, mouth );
	mouth[2] += VOICE_HEAD;
	if ( ( CG_PointContents( cg.refdef.vieworg, -1 ) ^ CG_PointContents( mouth, -1 ) ) & MASK_WATER )
		return qtrue;
	// players are not in this mask, so nobody stands in the way of a voice
	CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, mouth, cg.snap->ps.clientNum, MASK_SOLID );
	return ( tr.fraction < 1.0f && tr.entityNum != speaker ) ? qtrue : qfalse;
}

/*
=================
Voice_PlaceBox

How the song of a box of the boombox is heard in this frame. It has a place everybody knows: the engine plays it
there (Voice_InLevel()), or it comes from the side the box is on here, muffled behind a wall like a voice.
=================
*/
static void Voice_PlaceBox( int speaker, int now ) {
	voiceSpeaker_t	*sp = &voiceSpeakers[speaker];
	vec3_t			to;
	float			pan;
	trace_t			tr;

	Voice_Mouth( speaker );
	if ( sp->inLevel ) {
		trap->ext.S_SpatialVoice( speaker, sp->mouth, sp->volume, NULL, 0, VOICE_RATE );
		return;
	}
	VectorSubtract( sp->mouth, cg.refdef.vieworg, to );
	pan = ( VectorNormalize( to ) > 1.0f ) ? Com_Clamp( -VOICE_MAX_PAN, VOICE_MAX_PAN, DotProduct( to, cg.refdef.viewaxis[1] ) ) : 0.0f;
	if ( now - sp->traceTime >= VOICE_TRACE_MSEC ) {
		sp->traceTime = now;
		CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, sp->mouth, cg.snap->ps.clientNum, MASK_SOLID );
		sp->muffled = ( tr.fraction < 1.0f || ( ( CG_PointContents( cg.refdef.vieworg, -1 ) ^ CG_PointContents( sp->mouth, -1 ) ) & MASK_WATER ) )
			? qtrue : qfalse;
	}
	CG_VoiceAudioMix( speaker, sp->volume * sqrtf( 1.0f + pan ), sp->volume * sqrtf( 1.0f - pan ),
		( ( sp->muffled && v_voiceMuffle.integer ) || CG_Menu3DActive() ) ? 1.0f : 0.0f );
}

/*
=================
Voice_Place

How every voice that plays is heard in this frame: how loud on each side, and whether muffled.

On a server with a range (vs_voiceRange) a voice gets quieter with the distance and comes from the side its
speaker is on. A speaker behind a wall, behind a closed door or on the other side of a water surface is
muffled: one line from the eye of the listener to his head is looked at ten times a second. A speaker the
server does not show at all is muffled too and comes from no direction, at the distance the server named:
no more is known of him, on purpose. (The masterserver names the end of the range for everybody. Through it
only a speaker this client is shown is asked for, so that is for the moment after he went out of sight.)

Without a range, and among spectators, a voice is the same everywhere, like a radio.
=================
*/
static void Voice_Place( int now ) {
	int i;

	for ( i = 0; i < VOICE_ALL; i++ ) {
		voiceSpeaker_t	*sp = &voiceSpeakers[i];
		float			gain = v_voiceVolume.value, pan = 0.0f, distance;
		qboolean		muffled = qfalse;
		vec3_t			at;

		if ( !sp->playing )
			continue;
		if ( i >= MAX_CLIENTS ) {
			Voice_PlaceBox( i, now );
			continue;
		}
		// over the radio: in both ears, as loud wherever he is, and nothing over his head that would say where
		if ( sp->radio ) {
			gain *= 1.0f + 0.25f * sp->gainSteps;
			CG_VoiceAudioMix( i, gain, gain, CG_Menu3DActive() ? 1.0f : 0.0f );
			continue;
		}
		// the mark over his head, as long as his voice comes in (CG_Player)
		cg_entities[i].vChatTime = cg.time + 250;

		// the engine places him, see Voice_InLevel()
		if ( sp->inLevel ) {
			Voice_Mouth( i );
			trap->ext.S_SpatialVoice( i, sp->mouth, sp->volume, NULL, 0, VOICE_RATE );
			continue;
		}

		if ( voice.range > 0 && cgs.clientinfo[i].team != TEAM_SPECTATOR ) {
			if ( !( sp->flags & VOICE_FLAG_UNSEEN ) && Voice_Where( i, at ) ) {
				vec3_t to;

				VectorSubtract( at, cg.refdef.vieworg, to );
				to[2] += VOICE_HEAD;
				distance = VectorNormalize( to );
				// viewaxis[1] points to the left of the listener
				pan = Com_Clamp( -VOICE_MAX_PAN, VOICE_MAX_PAN, DotProduct( to, cg.refdef.viewaxis[1] ) );
				if ( now - sp->traceTime >= VOICE_TRACE_MSEC ) {
					sp->traceTime = now;
					sp->muffled = Voice_Blocked( i, at );
				}
				muffled = sp->muffled;
			} else {
				distance = (float)sp->distance * VOICE_DISTANCE_STEP;
				muffled = qtrue;
			}
			distance = Com_Clamp( 0.0f, 1.0f, 1.0f - distance / voice.range );
			gain *= VOICE_FAR_GAIN + ( 1.0f - VOICE_FAR_GAIN ) * distance * distance;
		}
		if ( !v_voiceMuffle.integer )
			muffled = qfalse;
		// the same loudness from the middle as from a side: the two ears share it
		// also while the menu stands in the level (menu3d/cg_menu3d.c), as the sound of the game is then
		gain *= 1.0f + 0.25f * sp->gainSteps;
		CG_VoiceAudioMix( i, gain * sqrtf( 1.0f + pan ), gain * sqrtf( 1.0f - pan ), ( muffled || CG_Menu3DActive() ) ? 1.0f : 0.0f );
	}
}

/*
=================
CG_VoiceDraw

On the HUD: who is talking, and a line for this player while his own voice goes out.
=================
*/
// The mark of a voice from the radio: a dot with three waves that leave it. Drawn here and handed to the
// renderer, where an engine of this tree takes a picture a module made (v_cgameScene). 0: there is none.
static qhandle_t Voice_RadioShader( void ) {
	enum { SIZE = 32 };
	static byte	pic[SIZE * SIZE * 4];
	char		value[8];
	int			x, y;

	if ( voice.radioShaderTried )
		return voice.radioShader;
	voice.radioShaderTried = qtrue;
	trap->Cvar_VariableStringBuffer( "v_cgameScene", value, sizeof( value ) );
	if ( atoi( value ) != 1 )
		return 0;
	for ( y = 0; y < SIZE; y++ ) {
		for ( x = 0; x < SIZE; x++ ) {
			// from the lower left corner, up and to the right
			const float	dx = x + 0.5f - 5.0f, dy = ( SIZE - 5.0f ) - ( y + 0.5f );
			const float	d = sqrtf( dx * dx + dy * dy );
			float		alpha = Com_Clamp( 0.0f, 1.0f, 3.5f - d );
			int			ring;

			if ( dx > -1.0f && dy > -1.0f ) {
				for ( ring = 0; ring < 3; ring++ ) {
					alpha = Q_max( alpha, Com_Clamp( 0.0f, 1.0f, 1.8f - fabsf( d - ( 10.0f + ring * 7.0f ) ) ) );
				}
			}
			pic[( y * SIZE + x ) * 4 + 0] = pic[( y * SIZE + x ) * 4 + 1] = pic[( y * SIZE + x ) * 4 + 2] = 255;
			pic[( y * SIZE + x ) * 4 + 3] = (byte)( alpha * 255.0f );
		}
	}
	voice.radioShader = trap->ext.R_RegisterShaderRGBA( "voice_radio", pic, SIZE, SIZE, RGBA_SHADER_OVER );
	return voice.radioShader;
}

// one line of the list of who talks: the picture of the voice chat or of the radio, and the name. With the HUD of
// the new look it lies on a dark strip, as the lines of the chat do.
static void Voice_DrawTalker( int x, int y, qhandle_t picture, const char *text ) {
	if ( CG_HudNewLook() ) {
		const vec4_t strip = { 0.02f, 0.04f, 0.08f, 0.46f };

		Skin_Panel( x - 3.0f, y - 1.0f, SMALLCHAR_HEIGHT + 4.0f + CG_StringWidth( text, SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT ) + 8.0f, SMALLCHAR_HEIGHT + 1.0f, 3.0f, strip, NULL );
	}
	CG_DrawPic( x, y, SMALLCHAR_HEIGHT, SMALLCHAR_HEIGHT, picture );
	CG_DrawSmallString( x + SMALLCHAR_HEIGHT + 4, y, text, 1.0f );
}

void CG_VoiceDraw( void ) {
	const int	x = 8, height = SMALLCHAR_HEIGHT + 2;
	int			y = 232, i;

	if ( !voice.answered || !v_voiceDrawTalkers.integer )
		return;
	if ( voice.sending && cgs.clientinfo[cg.clientNum].infoValid ) {
		const qhandle_t radio = voice.sendingRadio ? Voice_RadioShader() : 0;

		Voice_DrawTalker( x, y, radio ? radio : cgs.media.vchatShader, va( "%s ^7(%s)", cgs.clientinfo[cg.clientNum].name,
			CG_VisionText( voice.sendingRadio ? "You, clan radio" : "You" ) ) );
		y += height;
	}
	for ( i = 0; i < MAX_CLIENTS && y < 400; i++ ) {
		const qhandle_t radio = voiceSpeakers[i].radio ? Voice_RadioShader() : 0;

		if ( !voiceSpeakers[i].playing || !cgs.clientinfo[i].infoValid )
			continue;
		// without the picture the line says it
		Voice_DrawTalker( x, y, radio ? radio : cgs.media.vchatShader, ( voiceSpeakers[i].radio && !radio )
			? va( "%s ^7(%s)", cgs.clientinfo[i].name, CG_VisionText( "radio" ) ) : cgs.clientinfo[i].name );
		y += height;
	}
}

#ifndef __EMSCRIPTEN__
/*
===============================================================================

The masterserver

A game server without the game module of this tree has no relay, hands out no token and tells nobody who is
near whom. There the voice goes through one relay for all such servers (codemp/master), which knows no
game server. So this client tells it what a relay of a game server knows by itself, in a VOICE_STATE: which
room it is in, and who is near it.

===============================================================================
*/

/*
=================
Voice_MasterRoom

The name of the room of this game server at the masterserver: the same bytes on every client of that server,
and nothing somebody could work out who is not connected to it. sv_serverid is in the system info, which a
server only sends to its clients, and is another number with every level it starts. The master gets the hash
and never the address.

Players who reach the server under another address than the rest (its own network) are in a room of their own.
=================
*/
static void Voice_MasterRoom( int now ) {
	const byte	*ip = (const byte *)&voice.game.s_addr;
	char		text[64];
	byte		digest[WEB_SHA1_SIZE];

	Com_sprintf( text, sizeof( text ), "%i.%i.%i.%i:%i %s", ip[0], ip[1], ip[2], ip[3], voice.gamePort,
		Info_ValueForKey( CG_ConfigString( CS_SYSTEMINFO ), "sv_serverid" ) );
	Web_SHA1( text, strlen( text ), digest );
	if ( !memcmp( voice.room, digest, VOICE_ROOM_BYTES ) )
		return;
	memcpy( voice.room, digest, VOICE_ROOM_BYTES );
	// another room: this client has another token there, which the master names in its next answer
	voice.linked = qfalse;
	voice.stateDue = qtrue;
	voice.askTime = now;
}

/*
=================
Voice_MasterClan

What the masterserver is told as the clan of this client: eight bytes that are the same for everybody in this
room with this clan password (cp_clanPwd, the group of the clan chat) and that say nothing about the password
to somebody who does not have it. The room is part of them, so they are others with every level. All zero: this
player has no clan password, and what he says over the radio reaches nobody.
=================
*/
static void Voice_MasterClan( void ) {
	char	pass[64], text[VOICE_ROOM_BYTES + sizeof( pass ) + 8];
	byte	digest[WEB_SHA1_SIZE], clan[VOICE_CLAN_BYTES];
	int		len;

	memset( clan, 0, sizeof( clan ) );
	trap->Cvar_VariableStringBuffer( "cp_clanPwd", pass, sizeof( pass ) );
	// "none" is what the cvar has until the player sets it
	if ( pass[0] && Q_stricmp( pass, "none" ) ) {
		memcpy( text, "clan", 4 );
		memcpy( text + 4, voice.room, VOICE_ROOM_BYTES );
		len = (int)strlen( pass );
		memcpy( text + 4 + VOICE_ROOM_BYTES, pass, (size_t)len );
		Web_SHA1( text, (size_t)( 4 + VOICE_ROOM_BYTES + len ), digest );
		memcpy( clan, digest, VOICE_CLAN_BYTES );
		clan[0] |= 1;		// never all zero
	}
	if ( memcmp( clan, voice.clan, VOICE_CLAN_BYTES ) ) {
		memcpy( voice.clan, clan, VOICE_CLAN_BYTES );
		voice.stateDue = qtrue;
	}
}

/*
=================
Voice_Hear

Whom this client wants to hear and who may hear it, a bit per client number: the range of the voice chat, as
the relay of a game server works it out for everybody (BG_VoiceHears), here by every client for itself from
what its game shows it. The master hands a voice on only where both ends agree, so a client that asks for
everybody still gets only the speakers who see it near them.

A player this client is not shown is neither heard nor let in, however near he is: behind enough walls a
server stops sending him. Nobody is shown where a spectator watches from, so every spectator is let in, and
he asks for the players near his spot himself.
=================
*/
static void Voice_Hear( void ) {
	const float		range = VOICE_MASTER_RANGE * VOICE_HEAR_MARGIN;
	voiceEar_t		mouth, ear, other;
	unsigned int	hear = 0, allow = 0;
	float			distance;
	int				i;

	memset( &mouth, 0, sizeof( mouth ) );
	mouth.ready = qtrue;
	mouth.spectator = ( cgs.clientinfo[cg.clientNum].team == TEAM_SPECTATOR ) ? qtrue : qfalse;
	mouth.team = cgs.clientinfo[cg.clientNum].team;
	VectorCopy( cg.predictedPlayerState.origin, mouth.origin );
	// he listens where he watches from: a spectator at the player he follows
	ear = mouth;
	VectorCopy( cg.refdef.vieworg, ear.origin );

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		qboolean shown;

		if ( i == cg.clientNum || !cgs.clientinfo[i].infoValid )
			continue;
		memset( &other, 0, sizeof( other ) );
		other.ready = qtrue;
		other.spectator = ( cgs.clientinfo[i].team == TEAM_SPECTATOR ) ? qtrue : qfalse;
		other.team = cgs.clientinfo[i].team;
		shown = Voice_Where( i, other.origin );
		if ( !shown && !other.spectator )
			continue;

		// he speaks
		if ( !voiceSpeakers[i].muted && BG_VoiceHears( &other, &ear, range, qfalse, &distance ) )
			hear |= 1u << i;
		// this client speaks
		if ( other.spectator )
			VectorCopy( mouth.origin, other.origin );
		if ( BG_VoiceHears( &mouth, &other, range, qfalse, &distance ) )
			allow |= 1u << i;
	}
	// with the voice chat off the way is open for the boxes alone: nobody is heard, nobody hears this client
	if ( !v_voice.integer )
		hear = allow = 0;
	if ( hear != voice.hear || allow != voice.allow ) {
		voice.hear = hear;
		voice.allow = allow;
		voice.stateDue = qtrue;
	}
	// the boxes of the masterserver near enough to be heard (boombox/cg_boombox.c)
	if ( voice.boxes && CG_BoomboxHearMask() != voice.boxHear ) {
		voice.boxHear = CG_BoomboxHearMask();
		voice.stateDue = qtrue;
	}
}

/*
=================
Voice_MasterFrame

Keeps the master told: at once when the room or one of the masks changed, and once a second anyway, which is
also what keeps the session there and the way through the router of the player open. Until the master
answered, twice a second, and after ten seconds without an answer only every five. The clan password is looked
at with the room: it is part of what the master is told.
=================
*/
static void Voice_MasterFrame( int now ) {
	int every;

	if ( now - voice.roomTime >= VOICE_ROOM_MSEC || !voice.roomTime ) {
		voice.roomTime = now;
		Voice_MasterRoom( now );
		Voice_MasterClan();
	}
	if ( now - voice.hearTime >= VOICE_HEAR_MSEC ) {
		voice.hearTime = now;
		Voice_Hear();
	}

	if ( voice.linked )
		every = VOICE_STATE_MSEC;
	else
		every = ( now - voice.askTime > VOICE_NO_LINK_MSEC ) ? VOICE_KEEPALIVE_MSEC : VOICE_HELLO_MSEC;
	if ( !voice.stateTime || now - voice.stateTime >= every || ( voice.stateDue && now - voice.stateTime >= VOICE_STATE_MIN_MSEC ) ) {
		byte state[VOICE_STATE_BOX_BYTES];

		// the long one, with the clan, only to a master that said it reads it, and the boxes to one that plays them
		BG_VoiceStatePack( state, voice.room, voice.hear, voice.allow );
		memcpy( state + VOICE_STATE_BYTES, voice.clan, VOICE_CLAN_BYTES );
		state[VOICE_STATE_LONG_BYTES] = (byte)voice.boxHear;
		Voice_Send( VOICE_STATE, state, ( voice.boxes && v_boomboxes.integer ) ? VOICE_STATE_BOX_BYTES
			: voice.social ? VOICE_STATE_LONG_BYTES : VOICE_STATE_BYTES );
		voice.stateTime = now;
		voice.stateDue = qfalse;
	}
	if ( !voice.linked && !voice.refusedSince && !voice.toldNoLink && now - voice.askTime > VOICE_NO_LINK_MSEC ) {
		trap->Print( "Voice chat: no answer from the masterserver %s (UDP port %i)\n", v_voiceMasterHost.string, voice.port );
		voice.toldNoLink = qtrue;
	}
}
#endif // !__EMSCRIPTEN__

/*
===============================================================================

Once a frame

===============================================================================
*/

// the relay of a game server learns from these where this client listens, and its router keeps the way open
static void Voice_Hello( int now ) {
	if ( now - voice.helloTime < ( voice.linked ? VOICE_KEEPALIVE_MSEC : VOICE_HELLO_MSEC ) && voice.helloTime )
		return;
	Voice_Send( VOICE_HELLO, NULL, 0 );
	voice.helloTime = now;
	if ( !voice.linked && !voice.toldNoLink && now - voice.askTime > VOICE_NO_LINK_MSEC ) {
#ifdef __EMSCRIPTEN__
		trap->Print( "Voice chat: no answer from the server through its web host\n" );
#else
		trap->Print( "Voice chat: no answer from UDP port %i of the server. Is that port open there?\n", voice.port );
#endif
		voice.toldNoLink = qtrue;
	}
}

// The way to the relay is wanted for the voice chat, or with the chat switched off for the songs of the boombox alone
static qboolean Voice_Wanted( void ) {
	return ( v_voice.integer || CG_BoomboxWantsLink() ) ? qtrue : qfalse;
}

void CG_VoiceFrame( void ) {
	const int		now = trap->Milliseconds();
	const qboolean	master = Voice_WantsMaster();

	// the player does not want the masterserver any more, or the server says something about voice now
	if ( voice.master && !master ) {
		if ( voice.answered )
			Voice_Close();
		voice.master = qfalse;
		return;
	}
	if ( !Voice_Wanted() || ( !voice.serverHas && !master ) || voice.failed || !cg.snap ) {
		// switched off while it ran
		if ( voice.answered && !Voice_Wanted() )
			Voice_Close();
		return;
	}
#ifndef __EMSCRIPTEN__
	if ( !voice.answered && master && !Voice_MasterStart( now ) )
		return;
#endif
	if ( !voice.answered ) {
		// Asked again while there is no answer: the flood protection of the server (sv_floodProtect) drops a
		// command that comes within a second of another one without a word, and this one goes out just when the
		// client entered the game and has more to say.
		if ( voice.asks < VOICE_MAX_ASKS && ( !voice.asked || now - voice.askTime >= VOICE_ASK_MSEC ) ) {
			trap->SendClientCommand( va( "vs_voiceLink hello %i", VOICE_PROTOCOL ) );
			voice.asked = qtrue;
			voice.askTime = now;
			voice.asks++;
		}
		return;
	}
#ifdef __EMSCRIPTEN__
	// A WebSocket ends when the server or the line says so. The next one is asked for, and the relay has to
	// be greeted from it anew.
	if ( voice.serverKnown && CG_VoiceWebOpen() <= 0 ) {
		voice.serverKnown = voice.linked = qfalse;
		voice.askTime = now;
	}
#endif
	if ( !voice.serverKnown ) {
		voice.serverKnown = Voice_FindServer();
		if ( !voice.serverKnown )
			return;
	}

#ifndef __EMSCRIPTEN__
	if ( voice.master )
		Voice_MasterFrame( now );
	else
#endif
		Voice_Hello( now );

	Voice_Receive( now );
	if ( voice.audio ) {
		Voice_Play( now );
		Voice_Place( now );
	}
	if ( voice.linked && voice.encoderOk && v_voice.integer ) {
		Voice_Capture( now );
	} else if ( CG_VoiceAudioCapturing() ) {
		// the chat was switched off while the way stays open for the songs
		CG_VoiceAudioCloseCapture();
		voice.keyDown = voice.clanDown = voice.sending = qfalse;
		voice.micUntil = 0;
	}

	// v_voice_test record: what the speakers got since the last frame, as it is
	if ( voice.record ) {
		short	stereo[2048];
		int		frames;

		while ( ( frames = CG_VoiceAudioTapRead( stereo, 1024 ) ) > 0 )
			trap->FS_Write( stereo, frames * 2 * (int)sizeof( short ), voice.record );
	}
}

/*
===============================================================================

For the boombox (boombox/cg_boombox.c)

===============================================================================
*/

// how loud the song of a box is in what is heard now, 0 to 1: the newest frame whose time has come
float CG_VoiceBoxBeat( int box ) {
	const voiceSpeaker_t	*sp;
	const int				now = trap->Milliseconds();
	int						i, best = -1;

	if ( box < 0 || box >= BOX_MAX )
		return 0.0f;
	sp = &voiceSpeakers[MAX_CLIENTS + box];
	if ( !sp->playing )
		return 0.0f;
	for ( i = 0; i < VOICE_BOX_BEATS; i++ ) {
		if ( sp->beatAt[i] <= now && ( best < 0 || sp->beatAt[i] > sp->beatAt[best] ) )
			best = i;
	}
	return ( best >= 0 ) ? sp->beat[best] : 0.0f;
}

// does the masterserver the voice chat reaches play boxes
qboolean CG_VoiceMasterBoxes( void ) {
	return ( voice.master && voice.linked && voice.boxes ) ? qtrue : qfalse;
}

// and it can jump in a song, take one out of the line and send the line (VOICE_FLAG_BOXLIST)
qboolean CG_VoiceMasterBoxList( void ) {
	return ( voice.boxes && voice.boxList ) ? qtrue : qfalse;
}

// What the player does with a box of the masterserver, to the master. qfalse: there is no master that plays boxes.
qboolean CG_VoiceBoxSend( const boxDo_t *d ) {
	byte		body[VOICE_MAX_FRAME];
	const int	len = BG_BoxDoPack( body, sizeof( body ), d );

	if ( !voice.master || !voice.linked || !voice.boxes || !len )
		return qfalse;
#ifndef __EMSCRIPTEN__
	Voice_Send( VOICE_BOX, body, len );
	return qtrue;
#else
	return qfalse;
#endif
}

qboolean CG_VoiceBoxMuted( int box ) {
	return ( box >= 0 && box < BOX_MAX && voiceSpeakers[MAX_CLIENTS + box].muted ) ? qtrue : qfalse;
}

// this player does not want to hear that box, or wants to again
void CG_VoiceBoxMute( int box, qboolean mute ) {
	voiceSpeaker_t *sp;

	if ( box < 0 || box >= BOX_MAX )
		return;
	sp = &voiceSpeakers[MAX_CLIENTS + box];
	sp->muted = mute;
	if ( mute ) {
		sp->playing = qfalse;
		BG_VoiceQueueReset( &sp->queue );
	}
}

// the box is gone: what waits of it is dropped, and the next box of that number is heard
void CG_VoiceBoxReset( int box ) {
	voiceSpeaker_t *sp;

	if ( box < 0 || box >= BOX_MAX )
		return;
	sp = &voiceSpeakers[MAX_CLIENTS + box];
	sp->playing = sp->muted = qfalse;
	sp->lead = 0;
	BG_VoiceQueueReset( &sp->queue );
}

/*
===============================================================================

For the statuses above the heads (status/cg_status.c), on a server whose game module does not pass them on

===============================================================================
*/

// Is this player muted (v_voice_mute)?
qboolean CG_VoiceMuted( int clientNum ) {
	return ( clientNum >= 0 && clientNum < MAX_CLIENTS && voiceSpeakers[clientNum].muted ) ? qtrue : qfalse;
}

// One player louder or quieter than the others by a quarter, from silent to twice as loud, for this player
// alone and until the level changes. Returns how loud he is now, in percent.
int CG_VoiceLouder( int clientNum, int steps ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS )
		return 100;
	voiceSpeakers[clientNum].gainSteps = Com_Clampi( -4, 4, voiceSpeakers[clientNum].gainSteps + steps );
	return 100 + 25 * voiceSpeakers[clientNum].gainSteps;
}

// Does the masterserver hand statuses on for this client right now?
qboolean CG_VoiceSocial( void ) {
#ifdef __EMSCRIPTEN__
	return qfalse;
#else
	return ( voice.master && voice.linked && voice.social ) ? qtrue : qfalse;
#endif
}

// What stands above the head of this player goes to the others of the room. text is UTF-8. The master takes one
// a second.
void CG_VoiceSendStatus( qboolean away, const char *text ) {
	byte status[VOICE_STATUS_TEXT];

	if ( CG_VoiceSocial() )
		Voice_Send( VOICE_STATUS, status, BG_VoiceStatusPack( status, away, text ) );
}

// Does the masterserver hand on hats and capes for this client right now?
qboolean CG_VoiceLooks( void ) {
#ifdef __EMSCRIPTEN__
	return qfalse;
#else
	return ( voice.master && voice.linked && voice.looks ) ? qtrue : qfalse;
#endif
}

// What this player wears goes to the others of the room: "<hat> <cape>". The master takes one a second.
void CG_VoiceSendLook( const char *text ) {
	const int len = Q_min( (int)strlen( text ), VOICE_STATUS_TEXT - 1 );

	if ( CG_VoiceLooks() && len > 0 )
		Voice_Send( VOICE_LOOK, (const byte *)text, len );
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

// +v_voiceclan: to the clan, over the radio
void CG_VoiceClanDown_f( void ) {
	voice.clanDown = qtrue;
}

void CG_VoiceClanUp_f( void ) {
	voice.clanDown = qfalse;
	voice.toldNoRadio = qfalse;		// said again at the next try
}

// v_voice_devices: the microphones and speakers there are. v_voiceCapture and v_voicePlayback take a name.
void CG_VoiceDevices_f( void ) {
	static char	names[VOICE_MAX_DEVICES][VOICE_DEVICE_NAME];
	int			capture, count, i;

	if ( !CG_VoiceAudioInit( v_voiceBackend.string ) ) {
		trap->Print( "No sound system\n" );
		return;
	}
	for ( capture = 1; capture >= 0; capture-- ) {
		count = CG_VoiceAudioDevices( capture, names, VOICE_MAX_DEVICES );
		trap->Print( "%s (%s \"name\", empty for the one of the system):\n", capture ? "Microphones" : "Speakers",
			capture ? "v_voiceCapture" : "v_voicePlayback" );
		for ( i = 0; i < count; i++ )
			trap->Print( "  \"%s\"\n", names[i] );
		if ( !count )
			trap->Print( "  none\n" );
	}
}

void CG_VoiceStatus_f( void ) {
	const qboolean	master = Voice_WantsMaster();
	int				i;

	if ( !v_voice.integer ) {
		trap->Print( "Voice chat is off (v_voice 0)\n" );
	} else if ( !voice.serverHas && !master ) {
		trap->Print( "This server has no voice chat%s\n", voice.serverSays ? " and wants none (vs_voice 0)"
			: ( cg.demoPlayback ? "" : ", and the masterserver is switched off (v_voiceMaster 0)" ) );
	} else if ( voice.local ) {
		trap->Print( "This game runs on this machine and has no voice chat\n" );
	} else if ( voice.failed ) {
		trap->Print( "Voice chat could not start, see the lines above in the console\n" );
	} else if ( !voice.answered ) {
		trap->Print( master ? "Voice chat: this server has none, looking for the masterserver\n" : "Voice chat: asked the server, no answer yet\n" );
	} else {
		char		way[128];
		const char	*silent = "no answer from that port yet";

#ifdef __EMSCRIPTEN__
		Q_strncpyz( way, "through the web host of the server", sizeof( way ) );
		silent = "no answer from it yet";
#else
		if ( voice.master ) {
			Com_sprintf( way, sizeof( way ), "through the masterserver %s, UDP port %i", v_voiceMasterHost.string, voice.port );
			if ( voice.refusedSince )
				silent = "it does not let this client in";
		} else {
			Com_sprintf( way, sizeof( way ), "UDP port %i", voice.port );
		}
#endif

		trap->Print( "Voice chat: %s, %s, range %i, %s, microphone %s%s\n", way,
			voice.linked ? "connected" : silent, voice.range,
			v_voiceMode.integer == 2 ? "voice activation" : "push to talk (+v_voice)",
			CG_VoiceAudioCapturing() ? "open" : "closed", voice.sending ? ", sending" : "" );
		// do the speakers run: the frames they took so far, in seconds
		if ( !voice.audio || CG_VoiceAudioPlayed() < 0 )
			trap->Print( "Speakers: none open, you do not hear the others\n" );
		else
			trap->Print( "Speakers: open, %.1f seconds played so far\n", CG_VoiceAudioPlayed() / (float)VOICE_RATE );
		trap->Print( "Clan radio (+v_voiceclan): %s\n", !voice.radio ? ( voice.master ? "the masterserver does not have it" : "not on this server" )
			: voice.master ? "the masterserver has it. It reaches the players of this server with your clan password (cp_clanPwd)"
			: "the server has it. It reaches the players with your clan password (cp_clanPwd)" );
	}
#ifdef __EMSCRIPTEN__
	if ( v_voice.integer && voice.serverHas && CG_VoiceAudioNoMicrophone() )
		trap->Print( "This page has no microphone: %s. The others are heard all the same.\n", CG_VoiceAudioNoMicrophone() );
#endif
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( cgs.clientinfo[i].infoValid && ( voiceSpeakers[i].playing || voiceSpeakers[i].muted || voiceSpeakers[i].heard ) )
			trap->Print( "  %2i %s^7%s%s%s, %i frames heard\n", i, cgs.clientinfo[i].name, voiceSpeakers[i].playing ? " talks" : "",
				( voiceSpeakers[i].playing && voiceSpeakers[i].radio ) ? " over the radio" : "",
				voiceSpeakers[i].muted ? " (muted by you)" : "", voiceSpeakers[i].heard );
	}
}

// v_voice_mute <player>, v_voice_unmute <player or all>: this player does not want to hear that one
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

v_voice_test tone: a tone of 440 and 3000 Hz goes out in place of the microphone
v_voice_test play <name>: voice/<name>.raw goes out in place of the microphone, again and again
v_voice_test record <name>: what the speakers get is also written to voice/<name>.raw
v_voice_test stop: all of them end

The files are 16 bit, two channels, VOICE_RATE a second, low byte first: what was recorded can be played.
=================
*/
void CG_VoiceTest_f( void ) {
	const char *what = CG_Argv( 1 );

	if ( !Q_stricmp( what, "tone" ) ) {
		voice.test = 1;
		voice.testNext = 0;
	} else if ( !Q_stricmp( what, "play" ) && CG_Argv( 2 )[0] && !strpbrk( CG_Argv( 2 ), "/\\.:" ) ) {
		Com_sprintf( voice.playName, sizeof( voice.playName ), "voice/%s.raw", CG_Argv( 2 ) );
		voice.testNext = 0;
		voice.test = Voice_TestOpen() ? 2 : 0;
		if ( !voice.test )
			trap->Print( "v_voice_test: no sound in %s\n", voice.playName );
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
		if ( voice.play )
			trap->FS_Close( voice.play );
		voice.play = 0;
	} else {
		trap->Print( "usage: v_voice_test tone | play <name> | record <name> | stop\n" );
	}
}

#else // JP_VOICE

// built without the voice chat (BuildJPVoice off)
void CG_VoiceInit( void ) {}
void CG_VoiceShutdown( void ) {}
void CG_VoiceFrame( void ) {}
void CG_VoiceServerInfo( const char *value ) { (void)value; }
void CG_VoiceServerCommand( void ) {}
void CG_VoiceDown_f( void ) {}
void CG_VoiceUp_f( void ) {}
void CG_VoiceClanDown_f( void ) {}
void CG_VoiceClanUp_f( void ) {}
void CG_VoiceDevices_f( void ) {}
void CG_VoiceStatus_f( void ) {}
void CG_VoiceTest_f( void ) {}
void CG_VoiceMute_f( void ) {}
void CG_VoiceUnmute_f( void ) {}
void CG_VoiceDraw( void ) {}
int CG_VoiceLip( int clientNum ) { (void)clientNum; return 0; }
qboolean CG_VoiceSocial( void ) { return qfalse; }
qboolean CG_VoiceMuted( int clientNum ) { (void)clientNum; return qfalse; }
int CG_VoiceLouder( int clientNum, int steps ) { (void)clientNum; (void)steps; return 100; }
void CG_VoiceSendStatus( qboolean away, const char *text ) { (void)away; (void)text; }
qboolean CG_VoiceLooks( void ) { return qfalse; }
void CG_VoiceSendLook( const char *text ) { (void)text; }

#endif // JP_VOICE
