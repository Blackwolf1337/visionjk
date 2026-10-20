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
sv_web_local.h -- what the files of the web host of the dedicated server share. Not from OpenJK and not from JA+.

The client also runs in a browser (WebAssembly), and a page can not send UDP. So the dedicated server listens
on a TCP port of its own (vs_clientWebPort) and is to a browser what it is to a client over UDP:

  the page         GET /               index.html and the files next to it, from the folder vs_clientWebRoot
  the game files   GET /manifest.json  the pk3 files of the search path of the server, with size and checksum
                   GET /pk3/<path>     one of them, whole or a part of it (Range)
  the packets      GET /net            becomes a WebSocket (RFC 6455). Every binary message on it is one
                                       datagram of the game as UDP carries it, nothing added, both ways.
  the voice chat   GET /voice          becomes a WebSocket too. Every binary message on it is one datagram of
                                       the voice chat, which goes to the relay of the game module and back.

It is inside the server and no program in front of it, so the server knows the address a page plays from:
the peer of a WebSocket is a netadr_t of the type NA_WEB with the address and the port of its connection, and
its packets go where the ones of the UDP socket go (SV_PacketEvent). A connection that ends is a peer the
server does not hear from any more, and the timeout of its client does the rest.
Nothing of it asks for a login (the files are the ones the server hands to every client anyway) and every
byte that comes in is read as one of an attacker: fixed buffers, a fixed table, every length asked first.

  sv_web.cpp       the listener, the connections, their buffers and timeouts, what NET_Sleep calls
  sv_web_http.cpp  a request, the files, the manifest
  sv_web_ws.cpp    the WebSocket: its handshake, its frames, the packets in and out
  sv_web_voice.cpp the voice chat of a client in a page, between its WebSocket and the relay
*/

#pragma once

#include "server/server.h"
#include "qcommon/net_local.h"
#include "web/web_http.h"

// the datagram of the voice chat (plain C of the game modules, built into the server for sv_web_voice.cpp)
extern "C" {
#include "game/voice/bg_voice.h"
}

#include <stdio.h>

// Winsock waits on 64 sockets in a set (FD_SETSIZE), and the one NET_Sleep reads has the UDP socket and the
// listener in it too. So the table is a little smaller than 64 on every system. A WebSocket of the voice chat
// has a second socket in that set (to the relay) and takes two of these places (SV_WebPlaces).
#define SV_WEB_MAX_CONNECTIONS	60
#define SV_WEB_MAX_TRANSFERS	32			// of them hand out a file at once, the rest stays free for requests
#define SV_WEB_MAX_ADDRESS_TRANSFERS	6	// and of those for one address: what a browser opens to one host
#define SV_WEB_MAX_PER_ADDRESS	16			// a browser opens 6 to a host and a socket for each of its pages
#define SV_WEB_MAX_ACCEPTS		16			// new connections taken in one wake-up
#define SV_WEB_IN_SIZE			16384		// the head of a request, or the frames of a WebSocket that came in
#define SV_WEB_MAX_PACKET		4096		// the longest message a WebSocket peer may send, which is one datagram
#define SV_WEB_OUT_SIZE			65536		// what waits to be sent, and the piece a file is read in
#define SV_WEB_MAX_TARGET		1024		// the address in a request

// What is read of files in one wake-up of NET_Sleep, for all connections. A wake-up comes as soon as a socket
// takes more, so this does not slow a transfer down: it is the longest the game waits for the disk at a time.
#define SV_WEB_WAKE_BYTES		( 4 * SV_WEB_OUT_SIZE )

#define SV_WEB_IDLE_MSEC		15000		// a connection has this long to send a request
#define SV_WEB_STALL_MSEC		30000		// and an answer is given up when the other side takes nothing for this long
#define SV_WEB_SWEEP_MSEC		1000		// how often the timeouts are looked at
#define SV_WEB_AWAY_MSEC		5000		// the server itself did not look for longer (a map loaded): nobody timed out
#define SV_WEB_LISTEN_WAIT_MSEC	250			// the listener is left alone for this long after it failed

// The WebSocket of a client of the game has no timeout while its peer is there. A peer that was not heard
// for a while is asked (ping), which a browser answers by itself, and one that stays silent is gone.
#define SV_WEB_PING_MSEC		30000
#define SV_WEB_SILENT_MSEC		90000

// A WebSocket whose peer is no client of the game is closed this long after it was opened, or after its peer
// stopped being one. A page in its menus loses nothing by that: it opens the next socket with the next packet
// it sends. And a peer that only asks what the server runs (getinfo, getstatus) does not keep a place.
#define SV_WEB_NO_CLIENT_MSEC	60000

// The channel of a client of the game stays with its WebSocket while that is open (SV_WebPeerOpen). A socket
// that is gone without a word must not hold it for the 90 seconds above: when another socket of the address
// sends for the channel and the peer was not heard for the first of these, it is asked at once (a ping), and
// if nothing at all came from it by the second it is gone.
#define SV_WEB_PROBE_QUIET_MSEC	1000
#define SV_WEB_PROBE_MSEC		5000

// The packets a WebSocket peer hands the server: one in a millisecond, and this many more at once. No client
// sends that much (cl_maxpackets is 1000 at most, and one packet in a frame of the client). What is above it
// is lost, as a datagram is on a line that is full.
#define SV_WEB_PACKET_BURST		32
#define SV_WEB_PACKET_MSEC		1

// The voice chat of a client in a page (GET /voice, sv_web_voice.cpp). A message is a datagram of that chat
// as a client sends it, and a longer one ends the socket. What is handed on to the relay is as many as the
// relay itself takes from a speaker in a second (VOICE_MAX_PPS) at once and that many in every second after:
// a page that stood still for a moment sends what its microphone recorded since in one go.
#define SV_WEB_MAX_VOICE_PACKET	( VOICE_HEAD_TO_SERVER + VOICE_MAX_FRAME )
#define SV_WEB_MAX_ADDRESS_VOICE	4		// WebSockets of the voice chat for one address
#define SV_WEB_VOICE_READS		32			// datagrams of the relay that are taken for one of them in a wake-up
// It is only opened for an address a client of the game plays from through this host, and closed this long
// after the last such client is gone: nothing is handed on for it by then anyway.
#define SV_WEB_VOICE_NO_CLIENT_MSEC	10000

#define SV_WEB_MAX_PACKS		512			// pk3 files in the manifest
#define SV_WEB_MANIFEST_MSEC	2000		// the manifest is not put together more often than this

// a connection that was closed by the other side must not end the process with SIGPIPE
#ifdef MSG_NOSIGNAL
	#define SV_WEB_SEND_FLAGS	MSG_NOSIGNAL
#else
	#define SV_WEB_SEND_FLAGS	0
#endif

typedef enum {
	WEB_FREE,
	WEB_REQUEST,				// waits for the head of a request
	WEB_RESPONSE,				// sends an answer, the body of it is read while it goes out
	WEB_SOCKET,					// a WebSocket, the peer of the server for packets
	WEB_CLOSING					// sends what is left and ends
} webState_t;

typedef enum {
	WEB_BODY_NONE,
	WEB_BODY_FILE,
	WEB_BODY_MANIFEST
} webBody_t;

typedef struct webConnection_s {
	webState_t	state;
	SOCKET		sock;
	netadr_t	adr;			// NA_WEB: the address and the port of the other side
	qboolean	drop;			// closed at the next safe place, nothing is read or sent any more
	int			lastActive;		// Sys_Milliseconds() when it last got on

	// the answer that is being sent
	qboolean	keepAlive;		// another request may follow it
	qboolean	headOnly;		// HEAD: the head of the answer without its body
	webBody_t	body;
	FILE		*file;
	int64_t		bodyLeft;		// what of the body is not in the out buffer yet
	int			bodyAt;			// WEB_BODY_MANIFEST: how far into the text
	int			bodyGen;		// and which text it was

	// the WebSocket
	int			lastHeard;		// Sys_Milliseconds() when something came from the peer
	int			clientTime;		// and when it was last the peer of a client of the game, or became a WebSocket
	qboolean	pinged;			// and it was asked since
	qboolean	probed;			// asked because another socket sent for its channel, and not heard since
	int			probeTime;
	qboolean	inMessage;		// a message that comes in several frames is begun
	int			messageLen;
	leakyBucket_t	packets;	// how many packets it handed the server of late (SV_WEB_PACKET_BURST)

	// the WebSocket of the voice chat (GET /voice)
	qboolean	voice;			// its messages are datagrams for the relay of the voice chat, none is a packet of the game
	SOCKET		voiceSock;		// while voice is set: the UDP socket they go to the relay from, and what it answers comes on
	unsigned int	voiceToken;	// the token of the last datagram that went out there, 0: none yet

	int			inLen;
	int			inPos;			// WEB_SOCKET: the frames in front of this are done with
	int			outLen;
	int			outSent;

	// the buffers come last: what is in front of them is cleared for a new connection
	byte		in[SV_WEB_IN_SIZE];
	byte		out[SV_WEB_OUT_SIZE];
	byte		message[SV_WEB_MAX_PACKET];		// the frames of a message so far
} webConnection_t;

// the frames of a WebSocket (webFrame_t, WEB_OP_ and the rest) are in shared/web/web_http.h

extern cvar_t	*vs_clientWebRoot;

// sv_web.cpp
qboolean SV_WebSocketFits( SOCKET s );
qboolean SV_WebNonBlocking( SOCKET s );
void SV_WebDrop( webConnection_t *c );
byte *SV_WebOutRoom( webConnection_t *c, int len );
void SV_WebFlush( webConnection_t *c );
int SV_WebTransfers( const netadr_t *adr );
int SV_WebVoices( const netadr_t *adr );
int SV_WebPlaces( void );
webConnection_t *SV_WebSocketOf( const netadr_t *adr );

// sv_web_http.cpp
int SV_WebParseRange( const char *value, int64_t size, int64_t *first, int64_t *last );
qboolean SV_WebDecodePath( const char *target, char *out, int outSize );
void SV_WebError( webConnection_t *c, int status, const char *statusText, const char *headers );
void SV_WebHttpRequest( webConnection_t *c, int headLen );
int SV_WebBodyMore( webConnection_t *c, int max );
void SV_WebBodyEnd( webConnection_t *c );

// sv_web_ws.cpp
void SV_WebUpgrade( webConnection_t *c, const char *head, int headLen, int minor );
qboolean SV_WebSocketSend( webConnection_t *c, int opcode, const void *payload, int len );
void SV_WebSocketFrames( webConnection_t *c );
void SV_WebSocketPing( webConnection_t *c );
void SV_WebSocketEnd( webConnection_t *c, int code );

// sv_web_voice.cpp
qboolean SV_WebVoiceIsClient( const netadr_t *adr );
void SV_WebVoiceUpgrade( webConnection_t *c, const char *head, int headLen, int minor );
void SV_WebVoiceDatagram( webConnection_t *c, const byte *data, int length );
void SV_WebVoiceRead( webConnection_t *c );
