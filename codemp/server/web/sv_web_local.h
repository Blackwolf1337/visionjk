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
on a TCP port of its own (sv_webPort) and is to a browser what it is to a client over UDP:

  the page         GET /               index.html and the files next to it, from the folder sv_webRoot
  the game files   GET /manifest.json  the pk3 files of the search path of the server, with size and checksum
                   GET /pk3/<path>     one of them, whole or a part of it (Range)
  the packets      GET /net            becomes a WebSocket (RFC 6455). Every binary message on it is one
                                       datagram of the game as UDP carries it, nothing added, both ways.

It is inside the server and no program in front of it, so the server knows the address a page plays from:
the peer of a WebSocket is a netadr_t of the type NA_WEB with the address and the port of its connection, and
its packets go where the ones of the UDP socket go (SV_PacketEvent). A connection that ends is a peer the
server does not hear from any more, and the timeout of its client does the rest.
Nothing of it asks for a login (the files are the ones the server hands to every client anyway) and every
byte that comes in is read as one of an attacker: fixed buffers, a fixed table, every length asked first.

  sv_web.cpp       the listener, the connections, their buffers and timeouts, what NET_Sleep calls
  sv_web_http.cpp  a request, the files, the manifest
  sv_web_ws.cpp    the WebSocket: its handshake, its frames, the packets in and out
*/

#pragma once

#include "server/server.h"
#include "qcommon/net_local.h"
#include "web/web_http.h"

#include <stdio.h>

// Winsock waits on 64 sockets in a set (FD_SETSIZE), and the one NET_Sleep reads has the UDP socket and the
// listener in it too. So the table is a little smaller than 64 on every system.
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

// A WebSocket has no timeout while its peer is there: a client in its menus sends nothing. A peer that was
// not heard for a while is asked (ping), which a browser answers by itself, and one that stays silent is gone.
#define SV_WEB_PING_MSEC		30000
#define SV_WEB_SILENT_MSEC		90000

// The packets a WebSocket peer hands the server: one in a millisecond, and this many more at once. No client
// sends that much (cl_maxpackets is 1000 at most, and one packet in a frame of the client). What is above it
// is lost, as a datagram is on a line that is full.
#define SV_WEB_PACKET_BURST		32
#define SV_WEB_PACKET_MSEC		1

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
	qboolean	pinged;			// and it was asked since
	qboolean	inMessage;		// a message that comes in several frames is begun
	int			messageLen;
	leakyBucket_t	packets;	// how many packets it handed the server of late (SV_WEB_PACKET_BURST)

	int			inLen;
	int			inPos;			// WEB_SOCKET: the frames in front of this are done with
	int			outLen;
	int			outSent;

	// the buffers come last: what is in front of them is cleared for a new connection
	byte		in[SV_WEB_IN_SIZE];
	byte		out[SV_WEB_OUT_SIZE];
	byte		message[SV_WEB_MAX_PACKET];		// the frames of a message so far
} webConnection_t;

// the head of a frame of a WebSocket (RFC 6455, 5.2)
typedef struct webFrame_s {
	qboolean	fin;			// the last frame of its message
	int			opcode;
	int			length;			// of what follows the head
	byte		mask[4];		// what a client scrambles it with
} webFrame_t;

#define WEB_OP_CONTINUATION		0x0
#define WEB_OP_TEXT				0x1
#define WEB_OP_BINARY			0x2
#define WEB_OP_CLOSE			0x8
#define WEB_OP_PING				0x9
#define WEB_OP_PONG				0xA

// what SV_WebFrameHead says of a frame that is not taken
#define WEB_FRAME_BAD			-1			// against the protocol
#define WEB_FRAME_LONG			-2			// more than a datagram

extern cvar_t	*sv_webRoot;

// sv_web.cpp
void SV_WebDrop( webConnection_t *c );
byte *SV_WebOutRoom( webConnection_t *c, int len );
void SV_WebFlush( webConnection_t *c );
int SV_WebTransfers( const netadr_t *adr );
webConnection_t *SV_WebSocketOf( const netadr_t *adr );

// sv_web_http.cpp
int SV_WebParseRange( const char *value, int64_t size, int64_t *first, int64_t *last );
qboolean SV_WebDecodePath( const char *target, char *out, int outSize );
void SV_WebError( webConnection_t *c, int status, const char *statusText, const char *headers );
void SV_WebHttpRequest( webConnection_t *c, int headLen );
int SV_WebBodyMore( webConnection_t *c, int max );
void SV_WebBodyEnd( webConnection_t *c );

// sv_web_ws.cpp
void SV_WebAcceptKey( const char *key, char *out, int outSize );
int SV_WebFrameHead( const byte *data, int len, int maxPayload, webFrame_t *frame );
void SV_WebFrameUnmask( byte *payload, int len, const byte *mask );
int SV_WebFrameBuild( byte *out, int outSize, int opcode, const void *payload, int len );
void SV_WebUpgrade( webConnection_t *c, const char *head, int headLen, int minor );
void SV_WebSocketFrames( webConnection_t *c );
void SV_WebSocketPing( webConnection_t *c );
void SV_WebSocketEnd( webConnection_t *c, int code );
