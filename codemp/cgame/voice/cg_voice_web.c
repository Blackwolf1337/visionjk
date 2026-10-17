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

// cg_voice_web.c -- the way of the voice chat to its relay for the client that runs in a browser. See
// cg_voice.h and docs/japlus/voice.md.
//
// A page can not send UDP. The datagrams of cg_voice.c go through a WebSocket to the web host of the server
// the page came from (the address /voice, codemp/server/web/sv_web_voice.cpp), one binary message a datagram
// with nothing added, and that host hands them to the UDP port of the relay and back. It is a socket of its
// own next to the one of the game (codemp/qcommon/net_ws.cpp), which the engine does not hand to a module.
//
// Nothing here waits. The socket is asked for and is open some frames later, or never. Its callbacks run
// between two frames of the game and only copy into a queue, which CG_VoiceWebReceive() empties.
// CG_VoiceWebClose() has to run before the module is unloaded: a callback that came after it would run code
// that is gone.

#include "cgame/cg_local.h"
#include "game/voice/bg_voice.h"
#include "cg_voice.h"

#if defined( JP_VOICE ) && defined( __EMSCRIPTEN__ )

#include <emscripten.h>
#include <emscripten/websocket.h>

#define WEB_QUEUE			64		// datagrams that wait for the next frame of the game, which takes that many (VOICE_MAX_PER_FRAME)
#define WEB_REOPEN_MSEC		1000	// a socket that ended is asked for again after this long, twice as long after
#define WEB_MAX_TRIES		6		//	each one that never opened, and this many of those in a row are the end: a minute

static struct {
	EMSCRIPTEN_WEBSOCKET_T	socket;		// 0: none
	qboolean				open;
	int						askTime;	// trap->Milliseconds() when the socket was asked for
	int						tries;		// sockets in a row that ended without ever being open
	short					len[WEB_QUEUE];
	int						first, count;
	byte					data[WEB_QUEUE][VOICE_MAX_DATAGRAM];
} web;

static EM_BOOL Web_OnOpen( int eventType, const EmscriptenWebSocketOpenEvent *event, void *userData ) {
	(void)eventType;
	(void)userData;
	if ( event->socket == web.socket ) {
		web.open = qtrue;
		web.tries = 0;
	}
	return EM_TRUE;
}

// also when the socket could not be opened: the web host refused it, or is not there
static EM_BOOL Web_OnClose( int eventType, const EmscriptenWebSocketCloseEvent *event, void *userData ) {
	(void)eventType;
	(void)userData;
	if ( event->socket == web.socket ) {
		emscripten_websocket_delete( web.socket );
		web.socket = 0;
		if ( !web.open )
			web.tries++;
		web.open = qfalse;
	}
	return EM_TRUE;
}

static EM_BOOL Web_OnMessage( int eventType, const EmscriptenWebSocketMessageEvent *event, void *userData ) {
	const int	length = (int)event->numBytes;
	int			slot;

	(void)eventType;
	(void)userData;
	if ( event->socket != web.socket || event->isText || length <= 0 || length > VOICE_MAX_DATAGRAM )
		return EM_TRUE;
	// no room: lost, as a datagram is when nobody reads its socket for too long (a page that is not shown)
	if ( web.count == WEB_QUEUE )
		return EM_TRUE;
	slot = ( web.first + web.count ) % WEB_QUEUE;
	memcpy( web.data[slot], event->data, (size_t)length );
	web.len[slot] = (short)length;
	web.count++;
	return EM_TRUE;
}

/*
=================
CG_VoiceWebOpen

1: the socket is open. 0: not yet, it was asked for now or a moment ago. -1: never, the web host did not take
it WEB_MAX_TRIES times in a row, or the page does not say where it came from.

Where it came from is in the cvar v_wsHost of the engine (host:port), as for the socket of the game. A page
that came over https may only open a socket that is encrypted too.
=================
*/
int CG_VoiceWebOpen( void ) {
	EmscriptenWebSocketCreateAttributes	attr;
	char		host[256], url[320];
	const int	now = trap->Milliseconds();

	if ( web.open )
		return 1;
	if ( web.tries >= WEB_MAX_TRIES )
		return -1;
	if ( web.socket || ( web.askTime && now - web.askTime < ( WEB_REOPEN_MSEC << web.tries ) ) )
		return 0;

	trap->Cvar_VariableStringBuffer( "v_wsHost", host, sizeof( host ) );
	if ( !host[0] )
		return -1;
	Com_sprintf( url, sizeof( url ), "%s://%s/voice", EM_ASM_INT( { return location.protocol == 'https:'; }, 0 ) ? "wss" : "ws", host );

	web.askTime = now ? now : 1;
	emscripten_websocket_init_create_attributes( &attr );
	attr.url = url;
	web.socket = emscripten_websocket_new( &attr );
	if ( web.socket <= 0 ) {
		web.socket = 0;
		web.tries++;
		return 0;
	}
	emscripten_websocket_set_onopen_callback( web.socket, NULL, Web_OnOpen );
	emscripten_websocket_set_onclose_callback( web.socket, NULL, Web_OnClose );
	emscripten_websocket_set_onmessage_callback( web.socket, NULL, Web_OnMessage );
	return 0;
}

// the socket ends, and with it everything the page could still call here
void CG_VoiceWebClose( void ) {
	if ( web.socket ) {
		emscripten_websocket_close( web.socket, 1000, "" );
		// (takes the callbacks from the socket)
		emscripten_websocket_delete( web.socket );
	}
	memset( &web, 0, sizeof( web ) );
}

// a datagram to the relay. Lost if the socket is not open, as a datagram may be.
void CG_VoiceWebSend( const byte *datagram, int len ) {
	if ( web.open )
		emscripten_websocket_send_binary( web.socket, (void *)datagram, (uint32_t)len );
}

// The next datagram that came in since the last frame, at most size bytes of it. Returns its length, -1 if
// there is none.
int CG_VoiceWebReceive( byte *datagram, int size ) {
	int len;

	if ( !web.count )
		return -1;
	len = web.len[web.first];
	if ( len > size )
		len = size;
	memcpy( datagram, web.data[web.first], (size_t)len );
	web.first = ( web.first + 1 ) % WEB_QUEUE;
	web.count--;
	return len;
}

#endif // JP_VOICE && __EMSCRIPTEN__
