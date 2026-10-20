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
web_http.h -- what the two HTTP servers of the tree read a request with: the web admin panel of the game module
(codemp/game/webadmin) and the web host of the dedicated server (codemp/server/web). Plain C that knows neither
the engine nor the game, so both are built from the same files.

  web_http.c     the headers of a request, a length, and whether a request comes from the page itself
  web_base64.c   base64, both ways
  web_sha1.c     SHA-1, which the handshake of a WebSocket asks for
  web_sha256.c   SHA-256, with which the updater of the client checks what it fetched
  web_ws.c       the frames of a WebSocket, as a server reads and writes them
*/

#pragma once

#include <stddef.h>

#include "qcommon/q_platform.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define WEB_SHA1_SIZE	20
#define WEB_SHA256_SIZE	32

// web_http.c
const char *Web_FindHeader( const char *head, const char *name );
qboolean Web_HeaderValue( const char *head, const char *name, char *out, int outSize );
int Web_ParseLength( const char *s, int max );
qboolean Web_SameOrigin( const char *head );

// web_base64.c
int Web_Base64Decode( const char *in, char *out, int outSize );
int Web_Base64Encode( const void *in, int inLen, char *out, int outSize );

// web_sha1.c
void Web_SHA1( const void *data, size_t len, unsigned char digest[WEB_SHA1_SIZE] );

// web_ws.c: the head of a frame of a WebSocket (RFC 6455, 5.2)
typedef struct webFrame_s {
	qboolean		fin;			// the last frame of its message
	int				opcode;
	int				length;			// of what follows the head
	unsigned char	mask[4];		// what a client scrambles it with
} webFrame_t;

#define WEB_OP_CONTINUATION		0x0
#define WEB_OP_TEXT				0x1
#define WEB_OP_BINARY			0x2
#define WEB_OP_CLOSE			0x8
#define WEB_OP_PING				0x9
#define WEB_OP_PONG				0xA

// what Web_WsFrameHead says of a frame that is not taken
#define WEB_FRAME_BAD			-1			// against the protocol
#define WEB_FRAME_LONG			-2			// more than the reader takes

#define WEB_SOCKET_KEY_LEN		24			// the key of the handshake: 16 bytes as base64

// why a WebSocket is ended (RFC 6455, 7.4.1)
#define WEB_CLOSE_NORMAL		1000
#define WEB_CLOSE_PROTOCOL		1002
#define WEB_CLOSE_DATA			1003		// a kind of message this end does not take
#define WEB_CLOSE_TOO_BIG		1009

void Web_WsAcceptKey( const char *key, char *out, int outSize );
int Web_WsFrameHead( const unsigned char *data, int len, int maxPayload, webFrame_t *frame );
void Web_WsFrameUnmask( unsigned char *payload, int len, const unsigned char *mask );
int Web_WsFrameSize( int len );
int Web_WsFrameHeadOut( unsigned char *out, int opcode, int len );
int Web_WsFrameBuild( unsigned char *out, int outSize, int opcode, const void *payload, int len );

// web_sha256.c
typedef struct webSha256_s {
	uint32_t		state[8];
	uint64_t		length;			// bytes so far
	unsigned char	block[64];		// the bytes that do not fill a block yet,
	size_t			used;			//	and how many those are
} webSha256_t;

void Web_SHA256Init( webSha256_t *ctx );
void Web_SHA256Update( webSha256_t *ctx, const void *data, size_t len );
void Web_SHA256Final( webSha256_t *ctx, unsigned char digest[WEB_SHA256_SIZE] );
void Web_SHA256( const void *data, size_t len, unsigned char digest[WEB_SHA256_SIZE] );
void Web_Hex( const unsigned char *in, int len, char *out );	// out has room for len * 2 + 1

#if defined(__cplusplus)
} // extern "C"
#endif
