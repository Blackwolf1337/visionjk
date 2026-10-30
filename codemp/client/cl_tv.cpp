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

// cl_tv.cpp -- the TV of a server, live (issue #7): tv <address:port> opens GET /tv of the web host of the server
// (codemp/server/web/sv_web_tv.cpp) and plays what comes there as a demo that does not end. tv <name> (no port) or
// a ws:// or wss:// address asks a web server that hands /tv on to the server (nginx in front of jka.vision).
// Not from OpenJK, see docs/japlus/server.md, "The TV of the server".
//
// Every binary message on that WebSocket is a record of a demo: its number and its length (4 bytes each, little
// endian) and the message. They go into a queue as they come, and each frame of the client hands every one that
// is whole to the parser (CL_DemoMessage), as packets are taken in network play: the time of the client follows
// them as it follows a server (CL_AdjustTimeDelta), it does not read the file ahead as a demo does.
//
// In a browser the socket is the WebSocket of the page (Emscripten), elsewhere a TCP socket with the handshake of
// RFC 6455 done here. Nothing waits: the socket does not block and is looked at once a frame. Over https the
// engine has no TLS of its own: a thread asks libcurl or WinHTTP (Sys_HttpWebSocket, sys/sys_http.h), and each
// frame takes what that thread put into its inbox.

#include "client/client.h"

#ifdef __EMSCRIPTEN__
	#include <emscripten.h>
	#include <emscripten/websocket.h>
#else
	#include <SDL.h>
	#include "qcommon/net_local.h"
	#include "sys/sys_http.h"
#endif

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

#ifdef __EMSCRIPTEN__
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

#else
/*
===============================================================================

A WebSocket over TCP, as RFC 6455 has it for a client

===============================================================================
*/

typedef enum { TV_CONNECTING, TV_HANDSHAKE, TV_OPEN } tvState_t;

static SOCKET		tvSock = INVALID_SOCKET;
static tvState_t	tvState;
static char			tvRequest[512];
static byte			tvIn[65536];
static int			tvInLen;

/*
The stream through the library (ws:// or wss://), in a thread. The thread writes into inbox (under lock), the game takes it out each frame;
when the stream ends the thread writes why into ended and sets done last. A TV that is closed sets cancel and
leaves the thread, which ends within a second or so by itself (libcurl looks at cancel once a second, WinHTTP with
the next message, and the server sends one each frame of its own).
*/
static struct {
	qboolean		on;					// this TV goes through the library
	qboolean		started;			// a thread was started, and is through when done is set
	SDL_Thread		*thread;			// NULL also for one that was left behind
	SDL_mutex		*lock;
	char			url[MAX_OSPATH];
	volatile int	cancel;
	SDL_atomic_t	done;
	byte			inbox[TV_QUEUE_BYTES];
	int				inboxLen;
	char			ended[128];
} tvLib;

// (the thread)
static qboolean CL_TVLibSink( void *ctx, const void *data, size_t len ) {
	qboolean fits;

	SDL_LockMutex( tvLib.lock );
	fits = (qboolean)( len <= (size_t)( TV_QUEUE_BYTES - tvLib.inboxLen ) );
	if ( fits ) {
		Com_Memcpy( tvLib.inbox + tvLib.inboxLen, data, len );
		tvLib.inboxLen += (int)len;
	}
	SDL_UnlockMutex( tvLib.lock );
	return fits;
}

static int CL_TVLibThread( void *unused ) {
	char	error[128];
	int		status;

	Sys_HttpWebSocket( tvLib.url, CL_TVLibSink, NULL, &tvLib.cancel, error, sizeof( error ), &status );
	SDL_LockMutex( tvLib.lock );
	if ( status == 404 ) {
		Q_strncpyz( tvLib.ended, "the server has no TV", sizeof( tvLib.ended ) );
	} else {
		Q_strncpyz( tvLib.ended, error, sizeof( tvLib.ended ) );
	}
	SDL_UnlockMutex( tvLib.lock );
	SDL_AtomicSet( &tvLib.done, 1 );
	return 0;
}

// ws:// or wss:// with a host and maybe a path, or a name with no port (https then): through the web server in
// front of the game server, with the library. Only address:port is the socket of the web host of the server itself.
static qboolean CL_TVLib( const char *address ) {
	return (qboolean)( strstr( address, "://" ) || !strchr( address, ':' ) );
}

static qboolean CL_TVLibOpen( const char *address ) {
	const char *scheme = "wss", *host = address;

	if ( !Q_stricmpn( address, "wss://", 6 ) ) {
		host = address + 6;
	} else if ( !Q_stricmpn( address, "ws://", 5 ) ) {
		scheme = "ws";
		host = address + 5;
	} else if ( strstr( address, "://" ) ) {
		Com_Printf( "TV: %s is no ws:// or wss:// address\n", address );
		return qfalse;
	}

	if ( tvLib.started && !SDL_AtomicGet( &tvLib.done ) ) {
		Com_Printf( "TV: the one before still closes, ask again in a moment\n" );
		return qfalse;
	}
	if ( !tvLib.lock && !( tvLib.lock = SDL_CreateMutex() ) ) {
		return qfalse;
	}
	// (an address goes to WinHTTP as plain letters, one wide letter each: nothing else is let through)
	for ( const char *c = host; *c; c++ ) {
		if ( *c <= ' ' || *c >= 127 || *c == '"' || *c == '\\' ) {
			Com_Printf( "TV: %s is no address\n", address );
			return qfalse;
		}
	}
	Com_sprintf( tvLib.url, sizeof( tvLib.url ), "%s://%s%s", scheme, host, strchr( host, '/' ) ? "" : "/tv" );
	tvLib.cancel = 0;
	tvLib.inboxLen = 0;
	tvLib.ended[0] = '\0';
	SDL_AtomicSet( &tvLib.done, 0 );
	tvLib.thread = SDL_CreateThread( CL_TVLibThread, "tv", NULL );
	tvLib.started = tvLib.on = (qboolean)( tvLib.thread != NULL );
	return tvLib.on;
}

// what the thread brought, into the queue; a thread that is through is left behind
static void CL_TVLibPump( void ) {
	const qboolean done = (qboolean)( SDL_AtomicGet( &tvLib.done ) != 0 );

	SDL_LockMutex( tvLib.lock );
	CL_TVQueue( tvLib.inbox, tvLib.inboxLen );
	tvLib.inboxLen = 0;
	if ( done && !tvEnded[0] ) {
		Q_strncpyz( tvEnded, tvLib.ended, sizeof( tvEnded ) );
	}
	SDL_UnlockMutex( tvLib.lock );
	if ( done && tvLib.thread ) {
		SDL_WaitThread( tvLib.thread, NULL );		// (it is through, this does not wait)
		tvLib.thread = NULL;
	}
}

static void CL_TVLibShut( void ) {
	tvLib.on = qfalse;
	if ( !tvLib.thread ) {
		return;
	}
	if ( SDL_AtomicGet( &tvLib.done ) ) {
		SDL_WaitThread( tvLib.thread, NULL );		// (it is through, this does not wait)
	} else {
		// it ends by itself and touches nothing after done; the next tv through the library waits for that (CL_TVLibOpen)
		tvLib.cancel = 1;
		SDL_DetachThread( tvLib.thread );
	}
	tvLib.thread = NULL;
}

static qboolean CL_TVOpen( const char *address ) {
	struct sockaddr_in	sa;
	netadr_t			adr;
	char				host[MAX_OSPATH];
#ifdef _WIN32
	u_long				on = 1;
#else
	int					on = 1;
#endif

	if ( CL_TVLib( address ) ) {
		return CL_TVLibOpen( address );
	}
	Q_strncpyz( host, address, sizeof( host ) );
	if ( !NET_StringToAdr( host, &adr ) || adr.type != NA_IP || !adr.port ) {
		Com_Printf( "TV: %s is no address with a port\n", host );
		return qfalse;
	}
	memset( &sa, 0, sizeof( sa ) );
	sa.sin_family = AF_INET;
	memcpy( &sa.sin_addr, adr.ip, 4 );
	sa.sin_port = adr.port;
	tvSock = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	if ( tvSock == INVALID_SOCKET ) {
		return qfalse;
	}
	ioctlsocket( tvSock, FIONBIO, &on );
	if ( connect( tvSock, (struct sockaddr *)&sa, sizeof( sa ) ) == SOCKET_ERROR ) {
#ifdef _WIN32
		if ( WSAGetLastError() != WSAEWOULDBLOCK ) {
#else
		if ( errno != EINPROGRESS ) {
#endif
			closesocket( tvSock );
			tvSock = INVALID_SOCKET;
			return qfalse;
		}
	}
	// (the key need not be secret: it only shows the answer is to this request; 16 bytes in base64, the example of RFC 6455)
	Com_sprintf( tvRequest, sizeof( tvRequest ), "GET /tv HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n", host );
	tvState = TV_CONNECTING;
	tvInLen = 0;
	return qtrue;
}

// a frame of the client: masked (here with a mask of zeros, which RFC 6455 allows)
static void CL_TVSendFrame( int opcode, const byte *payload, int len ) {
	byte frame[2 + 4 + 125];

	if ( len > 125 ) {
		return;
	}
	frame[0] = (byte)( 0x80 | opcode );
	frame[1] = (byte)( 0x80 | len );
	memset( frame + 2, 0, 4 );
	memcpy( frame + 6, payload, len );
	send( tvSock, (const char *)frame, 6 + len, 0 );
}

// the frames that are whole in tvIn: a binary message into the queue, a ping answered, a close ends it
static void CL_TVFrames( void ) {
	int at = 0;

	while ( tvInLen - at >= 2 ) {
		const byte	*p = tvIn + at;
		const int	opcode = p[0] & 0x0f;
		int			head = 2;
		int64_t		len = p[1] & 0x7f;

		if ( p[1] & 0x80 ) {
			Q_strncpyz( tvEnded, "the server sent a masked frame", sizeof( tvEnded ) );
			return;
		}
		if ( len == 126 ) {
			if ( tvInLen - at < 4 ) {
				break;
			}
			len = ( p[2] << 8 ) | p[3];
			head = 4;
		} else if ( len == 127 ) {
			if ( tvInLen - at < 10 ) {
				break;
			}
			len = 0;
			for ( int i = 0; i < 8; i++ ) {
				len = ( len << 8 ) | p[2 + i];
			}
			head = 10;
		}
		if ( len > (int64_t)sizeof( tvIn ) - 10 ) {
			Q_strncpyz( tvEnded, "a message of the stream is too long", sizeof( tvEnded ) );
			return;
		}
		if ( tvInLen - at < head + len ) {
			break;
		}
		if ( opcode == 2 ) {
			CL_TVQueue( p + head, (int)len );
		} else if ( opcode == 9 ) {
			CL_TVSendFrame( 10, p + head, (int)len );
		} else if ( opcode == 8 ) {
			Q_strncpyz( tvEnded, "the server ended the stream", sizeof( tvEnded ) );
			return;
		}
		at += head + (int)len;
	}
	memmove( tvIn, tvIn + at, tvInLen - at );
	tvInLen -= at;
}

static void CL_TVPump( void ) {
	if ( tvLib.on ) {
		CL_TVLibPump();
		return;
	}
	if ( tvSock == INVALID_SOCKET || tvEnded[0] ) {
		return;
	}
	if ( tvState == TV_CONNECTING ) {
		fd_set			set;
		struct timeval	tv = { 0, 0 };
		int				err = 0;
		socklen_t		errLen = sizeof( err );

		FD_ZERO( &set );
		FD_SET( tvSock, &set );
		if ( select( (int)tvSock + 1, NULL, &set, NULL, &tv ) <= 0 ) {
			return;
		}
		getsockopt( tvSock, SOL_SOCKET, SO_ERROR, (char *)&err, &errLen );
		if ( err ) {
			Q_strncpyz( tvEnded, "the server can not be reached", sizeof( tvEnded ) );
			return;
		}
		send( tvSock, tvRequest, (int)strlen( tvRequest ), 0 );
		tvState = TV_HANDSHAKE;
	}
	for ( ;; ) {
		const int n = recv( tvSock, (char *)tvIn + tvInLen, (int)sizeof( tvIn ) - tvInLen, 0 );

		if ( n == 0 ) {
			Q_strncpyz( tvEnded, "the server closed the connection", sizeof( tvEnded ) );
			return;
		}
		if ( n < 0 ) {
#ifdef _WIN32
			if ( WSAGetLastError() != WSAEWOULDBLOCK ) {
#else
			if ( errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR ) {
#endif
				Q_strncpyz( tvEnded, "the connection to the server broke", sizeof( tvEnded ) );
			}
			return;
		}
		tvInLen += n;
		if ( tvState == TV_HANDSHAKE ) {
			char		*end;
			const int	keep = tvInLen;

			tvIn[Q_min( tvInLen, (int)sizeof( tvIn ) - 1 )] = '\0';
			end = strstr( (char *)tvIn, "\r\n\r\n" );
			if ( !end ) {
				if ( tvInLen >= (int)sizeof( tvIn ) - 1 ) {
					Q_strncpyz( tvEnded, "the server sent no answer", sizeof( tvEnded ) );
					return;
				}
				continue;
			}
			if ( strncmp( (char *)tvIn, "HTTP/1.1 101", 12 ) ) {
				( (char *)tvIn )[strcspn( (char *)tvIn, "\r\n" )] = '\0';
				Com_sprintf( tvEnded, sizeof( tvEnded ), "the server has no TV (%s)", (char *)tvIn );
				return;
			}
			tvInLen = keep - (int)( end + 4 - (char *)tvIn );
			memmove( tvIn, end + 4, tvInLen );
			tvState = TV_OPEN;
		}
		CL_TVFrames();
		if ( tvEnded[0] ) {
			return;
		}
	}
}

static void CL_TVShut( void ) {
	CL_TVLibShut();
	if ( tvSock != INVALID_SOCKET ) {
		closesocket( tvSock );
		tvSock = INVALID_SOCKET;
	}
}
#endif

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

tv <address:port>: the TV of that server, at the port of its web host (vs_clientWebPort)
tv <name>, tv wss://<name>[/path] or ws://...: through the web server in front of it (/tv when no path is given)
====================
*/
static void CL_TV_f( void ) {
	char address[MAX_OSPATH];

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "usage: tv <address:port of the web host of the server>, or tv <name> for one behind https\n" );
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
	Cmd_AddCommand( "tv", CL_TV_f, "Watch the TV of a server live: tv <address:port of its web host>, or tv <name> over https" );
}
