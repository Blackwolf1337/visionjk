/*
===========================================================================
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

// The network of the client in a browser, in place of net_ip.cpp. A page can not send UDP. Its packets go
// through one WebSocket to the server the page came from (the web host of visionjkded, codemp/server/web),
// one binary message a datagram, nothing added. There is only that one server at the other end: a packet to
// another address is dropped.

#include <arpa/inet.h>
#include <emscripten.h>
#include <emscripten/websocket.h>

#include "qcommon/qcommon.h"

// what came in since the last frame, each datagram behind its length. The callbacks of the socket run between
// two frames and only copy, NET_Sleep() hands the datagrams to the client.
#define WS_QUEUE_BYTES		( 1 << 20 )
#define WS_REOPEN_MSEC		1000

static cvar_t	*net_enabled;
static cvar_t	*net_wsHost;

static qboolean	ws_enabled;
static EMSCRIPTEN_WEBSOCKET_T ws_socket;	// 0: none
static qboolean	ws_open;
static int		ws_openTime;				// Sys_Milliseconds() when the socket was asked for
static netadr_t	ws_server;					// the address the client knows the server by
static byte		ws_queue[WS_QUEUE_BYTES];
static int		ws_queued;

/*
====================
NET_WS_OnOpen
====================
*/
static EM_BOOL NET_WS_OnOpen( int eventType, const EmscriptenWebSocketOpenEvent *event, void *userData ) {
	if ( event->socket == ws_socket ) {
		ws_open = qtrue;
	}
	return EM_TRUE;
}

/*
====================
NET_WS_OnClose

Also when the socket could not be opened. The next packet asks for a new one.
====================
*/
static EM_BOOL NET_WS_OnClose( int eventType, const EmscriptenWebSocketCloseEvent *event, void *userData ) {
	if ( event->socket == ws_socket ) {
		emscripten_websocket_delete( ws_socket );
		ws_socket = 0;
		ws_open = qfalse;
	}
	return EM_TRUE;
}

/*
====================
NET_WS_OnMessage
====================
*/
static EM_BOOL NET_WS_OnMessage( int eventType, const EmscriptenWebSocketMessageEvent *event, void *userData ) {
	int length = (int)event->numBytes;

	if ( event->socket != ws_socket || event->isText || length <= 0 || length > MAX_MSGLEN ) {
		return EM_TRUE;
	}
	// no room: lost, as a datagram is when nobody reads the socket for too long
	if ( ws_queued + (int)sizeof( length ) + length > WS_QUEUE_BYTES ) {
		return EM_TRUE;
	}
	memcpy( ws_queue + ws_queued, &length, sizeof( length ) );
	memcpy( ws_queue + ws_queued + sizeof( length ), event->data, length );
	ws_queued += sizeof( length ) + length;
	return EM_TRUE;
}

/*
====================
NET_WS_Close
====================
*/
static void NET_WS_Close( void ) {
	if ( ws_socket ) {
		// the callbacks first: the close of a socket that is let go must not end the one that follows it
		emscripten_websocket_set_onopen_callback( ws_socket, NULL, NULL );
		emscripten_websocket_set_onclose_callback( ws_socket, NULL, NULL );
		emscripten_websocket_set_onmessage_callback( ws_socket, NULL, NULL );
		emscripten_websocket_close( ws_socket, 1000, "" );
		emscripten_websocket_delete( ws_socket );
		ws_socket = 0;
	}
	ws_open = qfalse;
	ws_queued = 0;
}

/*
====================
NET_WS_Open

Asks for the socket, the answer comes with NET_WS_OnOpen() or NET_WS_OnClose(). At most once in WS_REOPEN_MSEC.
====================
*/
static void NET_WS_Open( void ) {
	EmscriptenWebSocketCreateAttributes attr;
	char url[MAX_STRING_CHARS];
	int now = Sys_Milliseconds();

	if ( !ws_enabled || ws_socket || !net_wsHost->string[0] ) {
		return;
	}
	if ( ws_openTime && now - ws_openTime < WS_REOPEN_MSEC ) {
		return;
	}
	ws_openTime = now;

	// a page that came over https may only open a socket that is encrypted too
	Com_sprintf( url, sizeof( url ), "%s://%s/net",
		EM_ASM_INT( { return location.protocol == 'https:'; } ) ? "wss" : "ws", net_wsHost->string );

	emscripten_websocket_init_create_attributes( &attr );
	attr.url = url;
	ws_socket = emscripten_websocket_new( &attr );
	if ( ws_socket <= 0 ) {
		Com_Printf( "NET_WS_Open: no socket to %s\n", url );
		ws_socket = 0;
		return;
	}
	emscripten_websocket_set_onopen_callback( ws_socket, NULL, NET_WS_OnOpen );
	emscripten_websocket_set_onclose_callback( ws_socket, NULL, NET_WS_OnClose );
	emscripten_websocket_set_onmessage_callback( ws_socket, NULL, NET_WS_OnMessage );
}

/*
====================
NET_WS_ServerAddress

The server has no address a page could send to, but the client keeps one for it and compares where a packet
comes from with it. That is the host of the page if it is written as numbers, else 127.0.0.1. The port is the
one the client last sent to.
====================
*/
static void NET_WS_ServerAddress( void ) {
	char host[MAX_STRING_CHARS];
	char *colon;

	Q_strncpyz( host, net_wsHost->string, sizeof( host ) );
	colon = strchr( host, ':' );
	if ( colon ) {
		*colon = '\0';
	}
	memset( &ws_server, 0, sizeof( ws_server ) );
	ws_server.type = NA_IP;
	if ( inet_pton( AF_INET, host, ws_server.ip ) != 1 ) {
		ws_server.ip[0] = 127;
		ws_server.ip[3] = 1;
	}
	ws_server.port = BigShort( PORT_SERVER );
}

/*
====================
Sys_StringToAdr

Numbers, or the name of the host of the page. Nothing can be looked up from a page.
====================
*/
qboolean Sys_StringToAdr( const char *s, netadr_t *a ) {
	size_t length = strlen( s );

	memset( a, 0, sizeof( *a ) );
	a->type = NA_IP;
	if ( inet_pton( AF_INET, s, a->ip ) == 1 ) {
		return qtrue;
	}
	if ( !Q_stricmp( s, "localhost" ) || ( !Q_stricmpn( s, net_wsHost->string, length )
			&& ( net_wsHost->string[length] == '\0' || net_wsHost->string[length] == ':' ) ) ) {
		memcpy( a->ip, ws_server.ip, sizeof( a->ip ) );
		return qtrue;
	}
	a->type = NA_BAD;
	return qfalse;
}

/*
====================
Sys_SendPacket
====================
*/
void Sys_SendPacket( int length, const void *data, const netadr_t *to ) {
	if ( to->type != NA_BROADCAST && to->type != NA_IP ) {
		Com_Error( ERR_FATAL, "Sys_SendPacket: bad address type" );
	}
	if ( to->type == NA_IP ) {
		if ( memcmp( to->ip, ws_server.ip, sizeof( to->ip ) ) ) {
			return;
		}
		ws_server.port = to->port;
	}
	if ( !ws_open ) {
		// lost, as a datagram may be. The client asks again by itself.
		NET_WS_Open();
		return;
	}
	if ( emscripten_websocket_send_binary( ws_socket, (void *)data, (uint32_t)length ) < 0 ) {
		Com_DPrintf( "Sys_SendPacket: the socket did not take %d bytes\n", length );
	}
}

/*
====================
Sys_IsLANAddress

A LAN client has its rate ignored and sends without care. This link is one TCP connection whatever the
address looks like, so it is never treated that way.
====================
*/
qboolean Sys_IsLANAddress( const netadr_t *adr ) {
	return (qboolean)( adr->type == NA_LOOPBACK );
}

/*
====================
Sys_ShowIP
====================
*/
void Sys_ShowIP( void ) {
	Com_Printf( "WebSocket to %s: %s\n", net_wsHost->string, ws_open ? "open" : "not open" );
}

/*
====================
NET_Sleep

Nothing sleeps on a page. The datagrams that came in since the last call go to the client.
====================
*/
void NET_Sleep( int msec ) {
	byte bufData[MAX_MSGLEN + 1];
	msg_t netmsg;
	netadr_t from;
	int pos = 0;

	while ( pos < ws_queued ) {
		int length;

		memcpy( &length, ws_queue + pos, sizeof( length ) );
		pos += sizeof( length );
		MSG_Init( &netmsg, bufData, sizeof( bufData ) );
		memcpy( bufData, ws_queue + pos, length );
		pos += length;
		netmsg.cursize = length;
		netmsg.readcount = 0;
		from = ws_server;

		if ( com_sv_running->integer ) {
			Com_RunAndTimeServerPacket( &from, &netmsg );
		} else {
			CL_PacketEvent( &from, &netmsg );
		}
	}
	ws_queued = 0;
}

/*
====================
NET_Config
====================
*/
void NET_Config( qboolean enableNetworking ) {
	qboolean modified = (qboolean)( net_enabled->modified || net_wsHost->modified );

	net_enabled->modified = qfalse;
	net_wsHost->modified = qfalse;
	if ( !net_enabled->integer ) {
		enableNetworking = qfalse;
	}
	if ( enableNetworking == ws_enabled && !modified ) {
		return;
	}
	NET_WS_Close();
	ws_enabled = enableNetworking;
	ws_openTime = 0;
	NET_WS_ServerAddress();
	// now and not with the first packet: until the socket is open, packets are lost
	NET_WS_Open();
}

/*
====================
NET_Restart_f
====================
*/
void NET_Restart_f( void ) {
	NET_Config( qtrue );
}

/*
====================
NET_Init
====================
*/
void NET_Init( void ) {
	net_enabled = Cvar_Get( "net_enabled", "1", CVAR_LATCH | CVAR_ARCHIVE_ND );
	// host and port of the page, the page sets it
	net_wsHost = Cvar_Get( "net_wsHost", "", CVAR_INIT, "Where the WebSocket to the server goes: host:port" );
	net_enabled->modified = qtrue;
	NET_Config( qtrue );
	Cmd_AddCommand( "net_restart", NET_Restart_f, "Restart the networking sub-system" );
}

/*
====================
NET_Shutdown
====================
*/
void NET_Shutdown( void ) {
	if ( !ws_enabled ) {
		return;
	}
	NET_Config( qfalse );
}
