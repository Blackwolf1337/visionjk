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

// web_ws.c -- the frames of a WebSocket (RFC 6455) as a server reads and writes them: the answer to the key of
// the handshake, the head of a frame of a client, its payload unscrambled, and a frame of the server. The web
// host of the dedicated server (codemp/server/web/sv_web_ws.cpp) and the web admin panel of the game module
// (codemp/game/webadmin) keep their sockets with it.

#include "web_http.h"

#include <stdio.h>
#include <string.h>

// what a server adds to the key of the client before it hashes it (RFC 6455, 1.3)
#define WEB_SOCKET_GUID		"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/*
==================
Web_WsAcceptKey

What the handshake answers the Sec-WebSocket-Key of the client with
==================
*/
void Web_WsAcceptKey( const char *key, char *out, int outSize ) {
	char			text[128];
	unsigned char	digest[WEB_SHA1_SIZE];

	snprintf( text, sizeof( text ), "%s" WEB_SOCKET_GUID, key );
	Web_SHA1( text, strlen( text ), digest );
	Web_Base64Encode( digest, sizeof( digest ), out, outSize );
}

/*
==================
Web_WsFrameHead

The head of the frame of a client that begins at data, of which len bytes are there. Returns the length of
the head (the payload follows it), 0 if not all of the head is there yet, WEB_FRAME_BAD for a frame that is
against the protocol and WEB_FRAME_LONG for one with more than maxPayload bytes.

  byte 0   FIN, three bits of extensions (none is agreed on, so they are 0), four bits opcode
  byte 1   the mask bit, which every frame of a client has, and seven bits of length:
           up to 125 the length itself, 126: it follows as 16 bits, 127: as 64 bits, big endian
  then     the four bytes the payload is scrambled with

The length is judged before anything is done with it, the 64 bits as they are.
==================
*/
int Web_WsFrameHead( const unsigned char *data, int len, int maxPayload, webFrame_t *frame ) {
	uint64_t	length;
	int			headLen = 2, i;

	if ( len < 2 ) {
		return 0;
	}
	if ( ( data[0] & 0x70 ) || !( data[1] & 0x80 ) ) {
		return WEB_FRAME_BAD;
	}

	frame->fin = (qboolean)( ( data[0] & 0x80 ) != 0 );
	frame->opcode = data[0] & 0x0F;

	length = data[1] & 0x7F;
	if ( length == 126 ) {
		if ( len < 4 ) {
			return 0;
		}
		length = ( (uint64_t)data[2] << 8 ) | data[3];
		headLen = 4;
		// (the shortest way to write a length is the only one)
		if ( length < 126 ) {
			return WEB_FRAME_BAD;
		}
	} else if ( length == 127 ) {
		if ( len < 10 ) {
			return 0;
		}
		length = 0;
		for ( i = 0; i < 8; i++ ) {
			length = ( length << 8 ) | data[2 + i];
		}
		headLen = 10;
		if ( length < 65536 ) {
			return WEB_FRAME_BAD;
		}
	}

	switch ( frame->opcode ) {
	case WEB_OP_CONTINUATION:
	case WEB_OP_TEXT:
	case WEB_OP_BINARY:
		break;
	case WEB_OP_CLOSE:
	case WEB_OP_PING:
	case WEB_OP_PONG:
		// these are short and never in pieces
		if ( !frame->fin || length > 125 ) {
			return WEB_FRAME_BAD;
		}
		break;
	default:
		return WEB_FRAME_BAD;
	}

	if ( maxPayload < 0 || length > (uint64_t)maxPayload ) {
		return WEB_FRAME_LONG;
	}
	if ( len < headLen + 4 ) {
		return 0;
	}

	memcpy( frame->mask, data + headLen, sizeof( frame->mask ) );
	frame->length = (int)length;
	return headLen + 4;
}

/*
==================
Web_WsFrameUnmask
==================
*/
void Web_WsFrameUnmask( unsigned char *payload, int len, const unsigned char *mask ) {
	int i;

	for ( i = 0; i < len; i++ ) {
		payload[i] ^= mask[i & 3];
	}
}

/*
==================
Web_WsFrameSize

How long the frame of a payload of len bytes is, as a server sends it: whole, and not masked
==================
*/
int Web_WsFrameSize( int len ) {
	return len + ( ( len < 126 ) ? 2 : ( len < 65536 ) ? 4 : 10 );
}

/*
==================
Web_WsFrameBuild

Writes that frame. Returns its length, 0 if it does not fit into outSize bytes.
==================
*/
int Web_WsFrameBuild( unsigned char *out, int outSize, int opcode, const void *payload, int len ) {
	int headLen = 2;

	if ( len < 0 || len > outSize || Web_WsFrameSize( len ) > outSize ) {
		return 0;
	}

	out[0] = (unsigned char)( 0x80 | ( opcode & 0x0F ) );
	if ( len < 126 ) {
		out[1] = (unsigned char)len;
	} else if ( len < 65536 ) {
		out[1] = 126;
		out[2] = (unsigned char)( len >> 8 );
		out[3] = (unsigned char)len;
		headLen = 4;
	} else {
		out[1] = 127;
		memset( out + 2, 0, 4 );
		out[6] = (unsigned char)( len >> 24 );
		out[7] = (unsigned char)( len >> 16 );
		out[8] = (unsigned char)( len >> 8 );
		out[9] = (unsigned char)len;
		headLen = 10;
	}

	if ( len ) {
		memcpy( out + headLen, payload, len );
	}
	return headLen + len;
}
