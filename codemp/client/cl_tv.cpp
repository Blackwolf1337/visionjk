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

// cl_tv.cpp -- the TV of a server, live, in the browser client (issue #7): the page opened with ?tv runs tv <host>,
// which opens GET /tv of the web host of the server (codemp/server/web/sv_web_tv.cpp) or of the web server in front
// of it, and plays what comes there as a demo that does not end. The desktop client has no TV (client.h).
// Not from OpenJK, see docs/japlus/server.md, "The TV of the server".
//
// Every binary message on that WebSocket is a record of a demo: its number and its length (4 bytes each, little
// endian) and the message. They go into a queue as they come, and each frame of the client hands every one that
// is whole to the parser (CL_DemoMessage), as packets are taken in network play: the time of the client follows
// them as it follows a server (CL_AdjustTimeDelta), it does not read the file ahead as a demo does.
//
// The socket is the WebSocket of the page (Emscripten): nothing waits, its callbacks only fill the queue.

#ifdef __EMSCRIPTEN__

#include "client/client.h"
#include <emscripten.h>
#include <emscripten/websocket.h>

#define TV_QUEUE_BYTES		( 1 << 20 )			// the records that came and wait for the next frame
#define TV_RECORD_HEAD		8

static byte		tvQueue[TV_QUEUE_BYTES];
static int		tvQueued;
static qboolean	tvOn;
static char		tvEnded[128];					// why the stream ended, "": it did not

static void CL_TVQueue( const byte *data, int len ) {
	if ( tvEnded[0] ) {
		return;
	}
	if ( len > TV_QUEUE_BYTES - tvQueued ) {
		Q_strncpyz( tvEnded, "the client could not keep up with the stream", sizeof( tvEnded ) );
		return;
	}
	Com_Memcpy( tvQueue + tvQueued, data, len );
	tvQueued += len;
}

/*
===============================================================================

The WebSocket of the page

===============================================================================
*/

static EMSCRIPTEN_WEBSOCKET_T tvSocket;		// 0: none

static EM_BOOL CL_TVOnMessage( int eventType, const EmscriptenWebSocketMessageEvent *event, void *userData ) {
	if ( event->socket == tvSocket && !event->isText ) {
		CL_TVQueue( event->data, (int)event->numBytes );
	}
	return EM_TRUE;
}

static EM_BOOL CL_TVOnClose( int eventType, const EmscriptenWebSocketCloseEvent *event, void *userData ) {
	if ( event->socket == tvSocket && !tvEnded[0] ) {
		Com_sprintf( tvEnded, sizeof( tvEnded ), "the server ended the stream (%d)", event->code );
	}
	return EM_TRUE;
}

static qboolean CL_TVOpen( const char *address ) {
	EmscriptenWebSocketCreateAttributes	attr;
	char								url[MAX_OSPATH];

	if ( !Q_stricmpn( address, "ws://", 5 ) || !Q_stricmpn( address, "wss://", 6 ) ) {
		Q_strncpyz( url, address, sizeof( url ) );
	} else {
		// (a page that came over https may only open a socket that is encrypted too)
		Com_sprintf( url, sizeof( url ), "%s://%s/tv",
			EM_ASM_INT( { return location.protocol == 'https:'; } ) ? "wss" : "ws", address );
	}
	emscripten_websocket_init_create_attributes( &attr );
	attr.url = url;
	tvSocket = emscripten_websocket_new( &attr );
	if ( tvSocket <= 0 ) {
		tvSocket = 0;
		return qfalse;
	}
	emscripten_websocket_set_onmessage_callback( tvSocket, NULL, CL_TVOnMessage );
	emscripten_websocket_set_onclose_callback( tvSocket, NULL, CL_TVOnClose );
	return qtrue;
}

static void CL_TVPump( void ) {
}

static void CL_TVShut( void ) {
	if ( tvSocket ) {
		emscripten_websocket_set_onmessage_callback( tvSocket, NULL, NULL );
		emscripten_websocket_set_onclose_callback( tvSocket, NULL, NULL );
		emscripten_websocket_close( tvSocket, 1000, "" );
		emscripten_websocket_delete( tvSocket );
		tvSocket = 0;
	}
}

/*
===============================================================================

The records as messages of a demo

===============================================================================
*/

void CL_TVClose( void ) {
	CL_TVShut();
	tvOn = qfalse;
	tvQueued = 0;
	tvEnded[0] = '\0';
}

// once a frame: what came, and every record that is whole, to the parser
void CL_TVFrame( void ) {
	static byte	data[MAX_MSGLEN];

	if ( !tvOn ) {
		return;
	}
	CL_TVPump();
	while ( tvOn && tvQueued >= TV_RECORD_HEAD ) {
		msg_t	buf;
		int		sequence, len;

		Com_Memcpy( &sequence, tvQueue, 4 );
		Com_Memcpy( &len, tvQueue + 4, 4 );
		sequence = LittleLong( sequence );
		len = LittleLong( len );
		if ( len < 0 || len > MAX_MSGLEN ) {
			Com_Error( ERR_DROP, "TV: a message of %d bytes", len );
		}
		if ( tvQueued < TV_RECORD_HEAD + len ) {
			break;
		}
		Com_Memcpy( data, tvQueue + TV_RECORD_HEAD, len );
		tvQueued -= TV_RECORD_HEAD + len;
		memmove( tvQueue, tvQueue + TV_RECORD_HEAD + len, tvQueued );
		MSG_Init( &buf, data, sizeof( data ) );
		buf.cursize = len;
		CL_DemoMessage( sequence, &buf );
	}
	if ( tvOn && tvEnded[0] ) {
		Com_Error( ERR_DROP, "TV: %s", tvEnded );
	}
}

/*
====================
CL_TV_f

tv <host[:port]>: the TV of that server, at its web host (vs_clientWebPort) or at the web server in front of it
tv ws://... or wss://...: that address
====================
*/
static void CL_TV_f( void ) {
	char address[MAX_OSPATH];

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "usage: tv <host[:port]> or tv <ws:// or wss:// address>\n" );
		return;
	}
	Q_strncpyz( address, Cmd_Argv( 1 ), sizeof( address ) );

	// as a demo begins (CL_PlayDemo_f): nothing of a game or a server of this client is left
	Cvar_Set( "sv_killserver", "2" );
	CL_Disconnect( qtrue );
	if ( com_sv_running->integer ) {
		SV_Shutdown( "Server was killed.\n" );
		Cvar_Set( "sv_killserver", "0" );
	}
	if ( !CL_TVOpen( address ) ) {
		Com_Printf( "TV: %s can not be opened\n", address );
		return;
	}
	tvOn = qtrue;
	tvQueued = 0;
	tvEnded[0] = '\0';
	Con_Close();
	Q_strncpyz( clc.demoName, "tv", sizeof( clc.demoName ) );
	Q_strncpyz( cls.servername, address, sizeof( cls.servername ) );
	cls.state = CA_CONNECTED;
	clc.demoplaying = qtrue;
	clc.tvLive = qtrue;
	Com_Printf( "TV: %s\n", address );
}

void CL_TVInit( void ) {
	Cmd_AddCommand( "tv", CL_TV_f, "Watch the TV of a server live (the page with ?tv runs it)" );
}

#endif
