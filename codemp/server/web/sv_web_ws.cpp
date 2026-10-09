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

// sv_web_ws.cpp -- the WebSocket of the web host (RFC 6455), which carries the packets of a client that runs
// in a browser. A binary message is one datagram, in both directions and with nothing added. What comes in
// is handed to the server as NET_Event hands it a datagram of the UDP socket, what Sys_SendPacket has for an
// NA_WEB address goes out as one frame.
//
// It is a stream, but it is used as UDP is: a datagram that does not fit into what waits to be sent is lost,
// and nothing here knows of clients. The netchan above it deals with loss as it always did.

#include "server/web/sv_web_local.h"

// what a server adds to the key of the client before it hashes it (RFC 6455, 1.3)
#define WEB_SOCKET_GUID		"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WEB_SOCKET_KEY_LEN	24			// 16 bytes as base64

// why a WebSocket is ended (RFC 6455, 7.4.1)
#define WEB_CLOSE_NORMAL	1000
#define WEB_CLOSE_PROTOCOL	1002
#define WEB_CLOSE_DATA		1003		// a kind of message this end does not take: text
#define WEB_CLOSE_TOO_BIG	1009

/*
==============================================================================

Frames

==============================================================================
*/

/*
==================
SV_WebAcceptKey

What the handshake answers the Sec-WebSocket-Key of the client with
==================
*/
void SV_WebAcceptKey( const char *key, char *out, int outSize ) {
	char			text[128];
	unsigned char	digest[WEB_SHA1_SIZE];

	Com_sprintf( text, sizeof( text ), "%s" WEB_SOCKET_GUID, key );
	Web_SHA1( text, strlen( text ), digest );
	Web_Base64Encode( digest, sizeof( digest ), out, outSize );
}

/*
==================
SV_WebFrameHead

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
int SV_WebFrameHead( const byte *data, int len, int maxPayload, webFrame_t *frame ) {
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
SV_WebFrameUnmask
==================
*/
void SV_WebFrameUnmask( byte *payload, int len, const byte *mask ) {
	int i;

	for ( i = 0; i < len; i++ ) {
		payload[i] ^= mask[i & 3];
	}
}

/*
==================
SV_WebFrameSize

How long the frame of a payload of len bytes is, as a server sends it: whole, and not masked
==================
*/
static int SV_WebFrameSize( int len ) {
	return len + ( ( len < 126 ) ? 2 : ( len < 65536 ) ? 4 : 10 );
}

/*
==================
SV_WebFrameBuild

Writes that frame. Returns its length, 0 if it does not fit into outSize bytes.
==================
*/
int SV_WebFrameBuild( byte *out, int outSize, int opcode, const void *payload, int len ) {
	int headLen = 2;

	if ( len < 0 || len > outSize || SV_WebFrameSize( len ) > outSize ) {
		return 0;
	}

	out[0] = (byte)( 0x80 | ( opcode & 0x0F ) );
	if ( len < 126 ) {
		out[1] = (byte)len;
	} else if ( len < 65536 ) {
		out[1] = 126;
		out[2] = (byte)( len >> 8 );
		out[3] = (byte)len;
		headLen = 4;
	} else {
		out[1] = 127;
		memset( out + 2, 0, 4 );
		out[6] = (byte)( len >> 24 );
		out[7] = (byte)( len >> 16 );
		out[8] = (byte)( len >> 8 );
		out[9] = (byte)len;
		headLen = 10;
	}

	if ( len ) {
		memcpy( out + headLen, payload, len );
	}
	return headLen + len;
}

/*
==============================================================================

The socket

==============================================================================
*/

/*
==================
SV_WebSocketSend

A frame behind what waits to be sent. qfalse: there is no room for all of it, and none of it is sent.
==================
*/
static qboolean SV_WebSocketSend( webConnection_t *c, int opcode, const void *payload, int len ) {
	byte	*out;
	int		size;

	// (asked before the head is added to it: more than the buffer holds never fits)
	if ( len < 0 || len > SV_WEB_OUT_SIZE ) {
		return qfalse;
	}
	size = SV_WebFrameSize( len );
	if ( !( out = SV_WebOutRoom( c, size ) ) ) {
		return qfalse;
	}
	c->outLen += SV_WebFrameBuild( out, size, opcode, payload, len );
	return qtrue;
}

/*
==================
SV_WebSocketEnd

Tells the peer why (a close frame) and reads nothing more from it. The connection ends when that is sent.
==================
*/
void SV_WebSocketEnd( webConnection_t *c, int code ) {
	const byte status[2] = { (byte)( code >> 8 ), (byte)code };

	SV_WebSocketSend( c, WEB_OP_CLOSE, status, sizeof( status ) );
	c->state = WEB_CLOSING;
	c->lastActive = Sys_Milliseconds();
}

/*
==================
SV_WebSocketPing

Asks a peer that was not heard for a while. Whatever it sends then shows that it is there.
==================
*/
void SV_WebSocketPing( webConnection_t *c ) {
	if ( SV_WebSocketSend( c, WEB_OP_PING, NULL, 0 ) ) {
		SV_WebFlush( c );
	}
}

/*
==================
SV_WebDatagram

A datagram of a peer, handed to the server as NET_Event hands it one of the UDP socket
==================
*/
static void SV_WebDatagram( const webConnection_t *c, const byte *data, int length ) {
	byte		bufData[MAX_MSGLEN + 1];
	msg_t		netmsg;
	netadr_t	from = c->adr;

	// (no server runs: a dedicated server has nobody else to give it to)
	if ( !com_sv_running->integer ) {
		return;
	}

	MSG_Init( &netmsg, bufData, sizeof( bufData ) );
	memcpy( bufData, data, length );
	netmsg.cursize = length;

	Com_RunAndTimeServerPacket( &from, &netmsg );
}

/*
==================
SV_WebSocketFrame

One frame of a peer that is whole, its payload is unscrambled
==================
*/
static void SV_WebSocketFrame( webConnection_t *c, const webFrame_t *frame, const byte *payload ) {
	switch ( frame->opcode ) {
	case WEB_OP_BINARY:
		if ( c->inMessage ) {
			SV_WebSocketEnd( c, WEB_CLOSE_PROTOCOL );
		} else if ( frame->fin ) {
			SV_WebDatagram( c, payload, frame->length );
		} else {
			// the first frame of a message that comes in several
			memcpy( c->message, payload, frame->length );
			c->messageLen = frame->length;
			c->inMessage = qtrue;
		}
		break;

	case WEB_OP_CONTINUATION:
		if ( !c->inMessage ) {
			SV_WebSocketEnd( c, WEB_CLOSE_PROTOCOL );
		} else if ( frame->length > SV_WEB_MAX_PACKET - c->messageLen ) {
			SV_WebSocketEnd( c, WEB_CLOSE_TOO_BIG );
		} else {
			memcpy( c->message + c->messageLen, payload, frame->length );
			c->messageLen += frame->length;
			if ( frame->fin ) {
				c->inMessage = qfalse;
				SV_WebDatagram( c, c->message, c->messageLen );
			}
		}
		break;

	case WEB_OP_TEXT:
		SV_WebSocketEnd( c, WEB_CLOSE_DATA );
		break;

	case WEB_OP_PING:
		// (if there is no room for the answer the peer is not reading anyway)
		SV_WebSocketSend( c, WEB_OP_PONG, payload, frame->length );
		break;

	case WEB_OP_PONG:
		break;

	case WEB_OP_CLOSE:
		SV_WebSocketEnd( c, WEB_CLOSE_NORMAL );
		break;
	}
}

/*
==================
SV_WebSocketFrames

Runs the frames in the in buffer that are whole.

A frame counts as done with (inPos) before the server sees its packet. The server may throw out of that with
an error that ends the map, and may send to this very peer while it runs: either way the buffer is in order
behind it, and no frame is ever run twice.
==================
*/
void SV_WebSocketFrames( webConnection_t *c ) {
	while ( c->state == WEB_SOCKET && !c->drop ) {
		webFrame_t	frame;
		byte		*data = c->in + c->inPos;
		const int	len = c->inLen - c->inPos;
		const int	headLen = SV_WebFrameHead( data, len, SV_WEB_MAX_PACKET, &frame );

		if ( headLen < 0 ) {
			SV_WebSocketEnd( c, ( headLen == WEB_FRAME_LONG ) ? WEB_CLOSE_TOO_BIG : WEB_CLOSE_PROTOCOL );
			return;
		}
		if ( !headLen || len - headLen < frame.length ) {
			return;		// the rest of it is to come
		}

		SV_WebFrameUnmask( data + headLen, frame.length, frame.mask );
		c->inPos += headLen + frame.length;
		SV_WebSocketFrame( c, &frame, data + headLen );
	}
}

/*
==================
SV_WebUpgrade

The request for /net, which asks to go on as a WebSocket (RFC 6455, 4.2). Only for the page this host handed
out: a page of another site that opens a socket to here would play from the address of whoever looks at it.
The browser names the site in Origin, which no page can leave out or change, and it has to be this host.
==================
*/
void SV_WebUpgrade( webConnection_t *c, const char *head, int headLen, int minor ) {
	char	key[64], raw[32], accept[32], value[128], answer[256];
	byte	*out;
	int		len;

	c->keepAlive = qfalse;

	if ( c->headOnly || minor < 1 ) {
		SV_WebError( c, 400, "Bad Request", "" );
		return;
	}
	if ( !Web_HeaderValue( head, "Upgrade", value, sizeof( value ) ) || !Q_stristr( value, "websocket" )
		|| !Web_HeaderValue( head, "Connection", value, sizeof( value ) ) || !Q_stristr( value, "upgrade" ) ) {
		SV_WebError( c, 426, "Upgrade Required", "Upgrade: websocket\r\n" );
		return;
	}
	if ( !Web_HeaderValue( head, "Sec-WebSocket-Version", value, sizeof( value ) ) || strcmp( value, "13" ) ) {
		SV_WebError( c, 426, "Upgrade Required", "Sec-WebSocket-Version: 13\r\n" );
		return;
	}
	if ( !Web_HeaderValue( head, "Sec-WebSocket-Key", key, sizeof( key ) ) || strlen( key ) != WEB_SOCKET_KEY_LEN
		|| Web_Base64Decode( key, raw, sizeof( raw ) ) != 16 ) {
		SV_WebError( c, 400, "Bad Request", "" );
		return;
	}
	if ( !Web_FindHeader( head, "Origin" ) || !Web_SameOrigin( head ) ) {
		SV_WebError( c, 403, "Forbidden", "" );
		return;
	}

	SV_WebAcceptKey( key, accept, sizeof( accept ) );
	Com_sprintf( answer, sizeof( answer ),
		"HTTP/1.1 101 Switching Protocols\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Accept: %s\r\n"
		"\r\n",
		accept );
	len = (int)strlen( answer );
	if ( !( out = SV_WebOutRoom( c, len ) ) ) {
		SV_WebDrop( c );
		return;
	}
	memcpy( out, answer, len );
	c->outLen += len;

	// what came behind the request is the first of its frames
	memmove( c->in, c->in + headLen, c->inLen - headLen );
	c->inLen -= headLen;
	c->inPos = 0;

	c->state = WEB_SOCKET;
	c->lastHeard = Sys_Milliseconds();
	c->pinged = qfalse;
	c->inMessage = qfalse;
	c->messageLen = 0;
}

/*
==================
SV_WebSendPacket

Sys_SendPacket for a peer of the web host. The datagram is one frame, and it goes out at once if the socket
takes it. Is there no room left for all of it behind what the socket did not take yet, it is lost, as it is
on a line that is full: old packets are worth nothing to the game. And so is one for a peer that is gone.
==================
*/
void SV_WebSendPacket( int length, const void *data, const netadr_t *to ) {
	webConnection_t *c = SV_WebSocketOf( to );

	if ( !c || !SV_WebSocketSend( c, WEB_OP_BINARY, data, length ) ) {
		return;
	}
	SV_WebFlush( c );
}
