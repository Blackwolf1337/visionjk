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
//
// The WebSocket of the voice chat (GET /voice) has the same frames. Its messages are datagrams of that chat
// and go to sv_web_voice.cpp.

#include "server/web/sv_web_local.h"

// The frames themselves (the key of the handshake, the head of a frame, a frame of the server) are read and
// written by shared/web/web_ws.c, which the web admin panel of the game module has its sockets with too.

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
qboolean SV_WebSocketSend( webConnection_t *c, int opcode, const void *payload, int len ) {
	byte	*out;
	int		size;

	// (asked before the head is added to it: more than the buffer holds never fits)
	if ( len < 0 || len > SV_WEB_OUT_SIZE ) {
		return qfalse;
	}
	size = Web_WsFrameSize( len );
	if ( !( out = SV_WebOutRoom( c, size ) ) ) {
		return qfalse;
	}
	c->outLen += Web_WsFrameBuild( out, size, opcode, payload, len );
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
static void SV_WebDatagram( webConnection_t *c, const byte *data, int length ) {
	byte		bufData[MAX_MSGLEN + 1];
	msg_t		netmsg;
	netadr_t	from = c->adr;

	// (no server runs: a dedicated server has nobody else to give it to)
	if ( !com_sv_running->integer ) {
		return;
	}

	// More than a client sends is lost. An empty message is six bytes, 2700 of them fit into what is read
	// from a socket at once, and the server answers every packet of an address it has no client for with
	// one of its own. A peer that sent nothing else kept the server from the frames of its game.
	// (lastHeard is the time of the read these frames came with)
	if ( SVC_RateLimit( &c->packets, SV_WEB_PACKET_BURST, SV_WEB_PACKET_MSEC, c->lastHeard ) ) {
		return;
	}

	MSG_Init( &netmsg, bufData, sizeof( bufData ) );
	memcpy( bufData, data, length );
	netmsg.cursize = length;

	Com_RunAndTimeServerPacket( &from, &netmsg );
}

/*
==================
SV_WebMaxMessage

The longest message this peer may send: a packet of the game, or a datagram of the voice chat
==================
*/
static int SV_WebMaxMessage( const webConnection_t *c ) {
	return c->voice ? SV_WEB_MAX_VOICE_PACKET : SV_WEB_MAX_PACKET;
}

/*
==================
SV_WebMessage

A message of a peer that is whole
==================
*/
static void SV_WebMessage( webConnection_t *c, const byte *data, int length ) {
	if ( c->voice ) {
		SV_WebVoiceDatagram( c, data, length );
	} else {
		SV_WebDatagram( c, data, length );
	}
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
			SV_WebMessage( c, payload, frame->length );
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
		} else if ( frame->length > SV_WebMaxMessage( c ) - c->messageLen ) {
			SV_WebSocketEnd( c, WEB_CLOSE_TOO_BIG );
		} else {
			memcpy( c->message + c->messageLen, payload, frame->length );
			c->messageLen += frame->length;
			if ( frame->fin ) {
				c->inMessage = qfalse;
				SV_WebMessage( c, c->message, c->messageLen );
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
		const int	headLen = Web_WsFrameHead( data, len, SV_WebMaxMessage( c ), &frame );

		if ( headLen < 0 ) {
			SV_WebSocketEnd( c, ( headLen == WEB_FRAME_LONG ) ? WEB_CLOSE_TOO_BIG : WEB_CLOSE_PROTOCOL );
			return;
		}
		if ( !headLen || len - headLen < frame.length ) {
			return;		// the rest of it is to come
		}

		Web_WsFrameUnmask( data + headLen, frame.length, frame.mask );
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

	Web_WsAcceptKey( key, accept, sizeof( accept ) );
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
	c->lastHeard = c->clientTime = Sys_Milliseconds();
	c->pinged = c->probed = qfalse;
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
