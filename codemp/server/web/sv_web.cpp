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

// sv_web.cpp -- the web host of the dedicated server: its listener, its connections, and what NET_Sleep calls.
//
// Nothing here waits. The sockets do not block, and they are in the select() the server sleeps in between
// two frames (NET_Sleep): the ones a request is awaited on in its read set, the ones something is to be sent
// on in a write set. A socket is only put into a set when the wake-up it causes does something to it,
// else the server would not sleep any more.
//
// A connection is never closed while the server runs something for it. It is marked (SV_WebDrop) and
// closed where nothing uses it: in front of the next select().

#include "server/web/sv_web_local.h"

#ifndef _WIN32
#include <netinet/tcp.h>		// TCP_NODELAY
#endif

// (on other systems FD_SETSIZE is the highest descriptor a set holds, which is asked for every socket)
#ifdef _WIN32
static_assert( SV_WEB_MAX_CONNECTIONS + 2 <= FD_SETSIZE, "the read set of NET_Sleep has no room for the web host" );
#endif

static cvar_t	*vs_clientWebPort;
static cvar_t	*vs_clientWebBind;
cvar_t			*vs_clientWebRoot;

static SOCKET			listenSocket = INVALID_SOCKET;
static qboolean			listenFailed;			// accept() failed, the listener is left alone for a moment
static int				listenFailTime;
static webConnection_t	connections[SV_WEB_MAX_CONNECTIONS];
static int				firstConnection;		// the one a wake-up begins with
static int				starvedConnection;		// the first one this wake-up had nothing left to read for, or -1
static int				fileBudget;				// what may still be read of files in this wake-up
static int				lastSweep;				// Sys_Milliseconds() when the timeouts were last looked at

/*
==============================================================================

Connections

==============================================================================
*/

/*
==================
SV_WebWouldBlock

qtrue: the call on a socket that just failed would have waited, the connection is as it was
==================
*/
static qboolean SV_WebWouldBlock( void ) {
	const int err = socketError;

#ifndef _WIN32
	// a signal came in between, nothing was done
	if ( err == EINTR ) {
		return qtrue;
	}
#endif
	return (qboolean)( err == EAGAIN );
}

/*
==================
SV_WebSocketFits

qfalse: the sets of select() can not hold the socket. On Windows a set is a list of sockets, which the size of
the table sees to. Elsewhere it has a bit for every descriptor below FD_SETSIZE and no room for one above.
==================
*/
qboolean SV_WebSocketFits( SOCKET s ) {
#ifdef _WIN32
	return qtrue;
#else
	return (qboolean)( s < FD_SETSIZE );
#endif
}

/*
==================
SV_WebNonBlocking

No call on the socket waits from here on. qfalse: it could not be set, and the socket is of no use.
==================
*/
qboolean SV_WebNonBlocking( SOCKET s ) {
#ifdef _WIN32
	u_long	on = 1;
#else
	int		on = 1;
#endif

	return (qboolean)( ioctlsocket( s, FIONBIO, &on ) != SOCKET_ERROR );
}

static void SV_WebSetFD( SOCKET s, fd_set *set, SOCKET *highest ) {
	FD_SET( s, set );
	if ( *highest == INVALID_SOCKET || s > *highest ) {
		*highest = s;
	}
}

/*
==================
SV_WebClose

Not while something still works with the connection, see SV_WebDrop
==================
*/
static void SV_WebClose( webConnection_t *c ) {
	SV_WebBodyEnd( c );
	if ( c->voice ) {
		closesocket( c->voiceSock );
		c->voice = qfalse;
	}
	closesocket( c->sock );
	c->sock = INVALID_SOCKET;
	c->state = WEB_FREE;
}

/*
==================
SV_WebDrop

The connection is over. It is closed in front of the next select(): whoever is in the middle of it, reading
its buffer or answering a packet from it, goes on with a connection that is still there.
==================
*/
void SV_WebDrop( webConnection_t *c ) {
	c->drop = qtrue;
}

/*
==================
SV_WebTransfers

How many connections hand out a file: all of them, or the ones of that address
==================
*/
int SV_WebTransfers( const netadr_t *adr ) {
	int i, count = 0;

	for ( i = 0; i < SV_WEB_MAX_CONNECTIONS; i++ ) {
		if ( connections[i].state != WEB_FREE && connections[i].body == WEB_BODY_FILE
			&& ( !adr || !memcmp( connections[i].adr.ip, adr->ip, sizeof( adr->ip ) ) ) ) {
			count++;
		}
	}
	return count;
}

/*
==================
SV_WebVoices

How many connections are WebSockets of the voice chat: all of them, or the ones of that address
==================
*/
int SV_WebVoices( const netadr_t *adr ) {
	int i, count = 0;

	for ( i = 0; i < SV_WEB_MAX_CONNECTIONS; i++ ) {
		if ( connections[i].state != WEB_FREE && connections[i].voice
			&& ( !adr || !memcmp( connections[i].adr.ip, adr->ip, sizeof( adr->ip ) ) ) ) {
			count++;
		}
	}
	return count;
}

/*
==================
SV_WebPlaces

How many of the SV_WEB_MAX_CONNECTIONS places are taken. A connection takes one, a WebSocket of the voice chat
two: its UDP socket to the relay waits in the read set of NET_Sleep as well, which has no more room than that
on Windows.
==================
*/
int SV_WebPlaces( void ) {
	int i, count = 0;

	for ( i = 0; i < SV_WEB_MAX_CONNECTIONS; i++ ) {
		if ( connections[i].state != WEB_FREE ) {
			count += connections[i].voice ? 2 : 1;
		}
	}
	return count;
}

/*
==================
SV_WebOutRoom

Room for len more bytes behind what waits to be sent, or NULL if there is not that much. The caller writes
them and adds len to outLen.
==================
*/
byte *SV_WebOutRoom( webConnection_t *c, int len ) {
	if ( len < 0 || len > SV_WEB_OUT_SIZE - ( c->outLen - c->outSent ) ) {
		return NULL;
	}
	if ( len > SV_WEB_OUT_SIZE - c->outLen ) {
		// what was sent makes room
		memmove( c->out, c->out + c->outSent, c->outLen - c->outSent );
		c->outLen -= c->outSent;
		c->outSent = 0;
	}
	return c->out + c->outLen;
}

/*
==================
SV_WebSocketOf

The WebSocket a packet to that address goes out on, NULL if it is gone. Never one of the voice chat.
==================
*/
webConnection_t *SV_WebSocketOf( const netadr_t *adr ) {
	int i;

	for ( i = 0; i < SV_WEB_MAX_CONNECTIONS; i++ ) {
		webConnection_t *c = &connections[i];

		if ( c->state == WEB_SOCKET && !c->drop && !c->voice && c->adr.port == adr->port
			&& !memcmp( c->adr.ip, adr->ip, sizeof( c->adr.ip ) ) ) {
			return c;
		}
	}
	return NULL;
}

/*
==================
SV_WebPeerOpen

qtrue: the WebSocket of that peer, the address of a client of the game, is open. The server asks when a
packet for the channel of that client came from another socket of its address (SV_PacketEvent), and the
channel stays where it is while the answer is qtrue.

A socket can be gone without a word (the network of the player changed), and the client that comes back on
its next socket is not to wait until the silence of the old one is noticed by itself. So a peer that was not
heard for a moment is asked at once whether it is there (a ping, which a browser answers by itself), and
one that sent nothing at all SV_WEB_PROBE_MSEC later is gone.
==================
*/
qboolean SV_WebPeerOpen( const netadr_t *adr ) {
	webConnection_t	*c = SV_WebSocketOf( adr );
	char			waiting;
	int				now;

	if ( !c ) {
		return qfalse;
	}

	now = Sys_Milliseconds();
	if ( !c->probed ) {
		if ( now - c->lastHeard > SV_WEB_PROBE_QUIET_MSEC ) {
			c->probed = qtrue;
			c->probeTime = now;
			SV_WebSocketPing( c );
		}
		return (qboolean)!c->drop;
	}

	// Its time is over, unless its answer is there and only was not read yet: this wake-up came to the other
	// socket first, or the server did not look at its sockets for a while (a map loaded).
	if ( now - c->probeTime > SV_WEB_PROBE_MSEC && recv( c->sock, &waiting, 1, MSG_PEEK ) <= 0 ) {
		SV_WebDrop( c );
		return qfalse;
	}
	return qtrue;
}

/*
==================
SV_WebFlush

Sends what the socket takes of what waits
==================
*/
void SV_WebFlush( webConnection_t *c ) {
	while ( c->outSent < c->outLen ) {
		const int sent = send( c->sock, (const char *)c->out + c->outSent, c->outLen - c->outSent, SV_WEB_SEND_FLAGS );

		if ( sent == SOCKET_ERROR ) {
			if ( !SV_WebWouldBlock() ) {
				SV_WebDrop( c );
			}
			return;
		}
		if ( sent <= 0 ) {
			return;
		}
		c->outSent += sent;
		c->lastActive = Sys_Milliseconds();
	}
	c->outLen = c->outSent = 0;
}

/*
==================
SV_WebWrite

Sends what waits and, while the socket takes it, the body of the answer piece by piece. The pieces are read
here, and no more of them in one wake-up than SV_WEB_WAKE_BYTES for all connections: a connection that finds
nothing left is the first one of the next wake-up, which comes at once because its socket is still ready.
==================
*/
static void SV_WebWrite( webConnection_t *c ) {
	qboolean served = qfalse;		// it got a piece in this wake-up

	for ( ;; ) {
		int max, got;

		SV_WebFlush( c );
		if ( c->drop || c->outSent < c->outLen ) {
			return;		// over, or the socket takes no more for now
		}
		if ( c->state != WEB_RESPONSE || c->bodyLeft <= 0 ) {
			break;
		}

		if ( fileBudget <= 0 ) {
			// (not the one that took the last of it: it would be the first again in every wake-up, and
			// the others would wait until its file is through)
			if ( !served && starvedConnection < 0 ) {
				starvedConnection = (int)( c - connections );
			}
			return;
		}
		max = ( fileBudget < SV_WEB_OUT_SIZE ) ? fileBudget : SV_WEB_OUT_SIZE;
		got = SV_WebBodyMore( c, max );
		if ( got <= 0 ) {
			// the length of the body is on its way, and the rest of it is not to be had
			SV_WebDrop( c );
			return;
		}
		fileBudget -= got;
		served = qtrue;
	}

	// all of it is out
	if ( c->state == WEB_RESPONSE && c->keepAlive ) {
		SV_WebBodyEnd( c );
		c->state = WEB_REQUEST;
		c->keepAlive = c->headOnly = qfalse;
		c->inLen = 0;
		c->lastActive = Sys_Milliseconds();
	} else if ( c->state == WEB_RESPONSE || c->state == WEB_CLOSING ) {
		SV_WebDrop( c );
	}
}

/*
==================
SV_WebRead

More of the head of a request, which is answered with the empty line that ends it. Or more frames of a
WebSocket, which are run as far as they are whole.
==================
*/
static void SV_WebRead( webConnection_t *c ) {
	int received, i, headLen = 0;

	if ( c->state != WEB_REQUEST && c->state != WEB_SOCKET ) {
		return;
	}

	// the frames that are done with make room for the next ones
	if ( c->inPos ) {
		memmove( c->in, c->in + c->inPos, c->inLen - c->inPos );
		c->inLen -= c->inPos;
		c->inPos = 0;
	}

	// (there is room: a head that fills the buffer was refused when it did, and a frame that is longer than
	// the quarter of it when its length came)
	received = recv( c->sock, (char *)c->in + c->inLen, SV_WEB_IN_SIZE - 1 - c->inLen, 0 );
	if ( received == SOCKET_ERROR ) {
		if ( !SV_WebWouldBlock() ) {
			SV_WebDrop( c );
		}
		return;
	}
	if ( received <= 0 ) {
		SV_WebDrop( c );		// the other side closed
		return;
	}

	if ( c->state == WEB_SOCKET ) {
		c->inLen += received;
		c->lastHeard = Sys_Milliseconds();
		c->pinged = c->probed = qfalse;
		SV_WebSocketFrames( c );
		SV_WebWrite( c );
		return;
	}

	// the end of the head may have begun in what was there before
	i = ( c->inLen > 3 ) ? c->inLen - 3 : 0;
	c->inLen += received;
	for ( ; i + 3 < c->inLen; i++ ) {
		if ( c->in[i] == '\r' && c->in[i + 1] == '\n' && c->in[i + 2] == '\r' && c->in[i + 3] == '\n' ) {
			headLen = i + 4;
			break;
		}
	}

	if ( headLen ) {
		SV_WebHttpRequest( c, headLen );
	} else if ( c->inLen >= SV_WEB_IN_SIZE - 1 ) {
		SV_WebError( c, 431, "Request Header Fields Too Large", "" );
	} else {
		return;		// more of it is to come
	}

	// a WebSocket now, and frames came right behind the request
	if ( c->state == WEB_SOCKET ) {
		SV_WebSocketFrames( c );
	}

	// what it is answered with goes out now if the socket takes it, the rest when it does
	SV_WebWrite( c );
}

/*
==================
SV_WebAccept
==================
*/
static void SV_WebAccept( void ) {
	const int	now = Sys_Milliseconds();
	int			accepted;

	// (some at a time: the ones left keep the listener readable and are taken with the next wake-up)
	for ( accepted = 0; accepted < SV_WEB_MAX_ACCEPTS; accepted++ ) {
		struct sockaddr_in	from;
		socklen_t			fromLen = sizeof( from );
		webConnection_t		*c = NULL;
		int					i, on = 1, sameAddress = 0;
		SOCKET				s;

		memset( &from, 0, sizeof( from ) );
		s = accept( listenSocket, (struct sockaddr *)&from, &fromLen );
		if ( s == INVALID_SOCKET ) {
			// one that was ended by the other side while it waited is gone from the listener: the next
			if ( socketError == ECONNRESET || socketError == ECONNABORTED ) {
				continue;
			}
			// Nothing more waits, or it can not be taken: no descriptor is free, for one. That connection
			// stays with the listener and keeps it readable, so the listener is not waited on for a moment.
			if ( !SV_WebWouldBlock() ) {
				listenFailed = qtrue;
				listenFailTime = now;
			}
			return;
		}

		for ( i = 0; i < SV_WEB_MAX_CONNECTIONS; i++ ) {
			if ( connections[i].state == WEB_FREE ) {
				if ( !c ) {
					c = &connections[i];
				}
			} else if ( !memcmp( connections[i].adr.ip, &from.sin_addr, sizeof( connections[i].adr.ip ) ) ) {
				sameAddress++;
			}
		}

		// It is always taken from the listener, also when there is no place for it: one that stays there
		// keeps the listener readable, and the server would not sleep any more.
		if ( !c || SV_WebPlaces() >= SV_WEB_MAX_CONNECTIONS || sameAddress >= SV_WEB_MAX_PER_ADDRESS
			|| from.sin_family != AF_INET || !SV_WebSocketFits( s )
			|| !SV_WebNonBlocking( s ) ) {
			closesocket( s );
			continue;
		}

		// what is written goes out at once: a packet of the game must not wait for the one behind it (Nagle)
		setsockopt( s, IPPROTO_TCP, TCP_NODELAY, (char *)&on, sizeof( on ) );
#ifdef SO_NOSIGPIPE
		setsockopt( s, SOL_SOCKET, SO_NOSIGPIPE, (char *)&on, sizeof( on ) );
#endif

		memset( c, 0, offsetof( webConnection_t, in ) );
		c->state = WEB_REQUEST;
		c->sock = s;
		c->adr.type = NA_WEB;
		memcpy( c->adr.ip, &from.sin_addr, sizeof( c->adr.ip ) );
		c->adr.port = from.sin_port;		// in network order, as the port of a UDP peer is kept
		c->lastActive = now;
	}
}

/*
==================
SV_WebListen

Opens the listener as vs_clientWebPort and vs_clientWebBind say, after it closed the one there was. The connections stay,
unless the port says the web host is off.
==================
*/
static void SV_WebListen( void ) {
	struct sockaddr_in	address;
	const char			*bindTo = "0.0.0.0";
	const int			port = vs_clientWebPort->integer;
	int					i;

	vs_clientWebPort->modified = qfalse;
	vs_clientWebBind->modified = qfalse;

	if ( listenSocket != INVALID_SOCKET ) {
		closesocket( listenSocket );
		listenSocket = INVALID_SOCKET;
	}
	listenFailed = qfalse;

	if ( port <= 0 || port > 65535 ) {
		if ( port ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: web host: vs_clientWebPort %d is no port\n", port );
		}
		for ( i = 0; i < SV_WEB_MAX_CONNECTIONS; i++ ) {
			if ( connections[i].state != WEB_FREE ) {
				SV_WebClose( &connections[i] );
			}
		}
		return;
	}

	// Every interface unless vs_clientWebBind names one address, as vs_webBind of the web admin panel has it: an
	// empty one is every interface too, and a value that is no address opens nothing to the outside.
	// (inet_addr and not Sys_StringToAdr, which asks the name service about what is no number and waits for it)
	memset( &address, 0, sizeof( address ) );
	address.sin_family = AF_INET;
	address.sin_port = htons( (unsigned short)port );
	address.sin_addr.s_addr = INADDR_ANY;
	if ( vs_clientWebBind->string[0] ) {
		address.sin_addr.s_addr = inet_addr( vs_clientWebBind->string );
		if ( address.sin_addr.s_addr != INADDR_NONE ) {
			bindTo = vs_clientWebBind->string;
		} else {
			bindTo = "127.0.0.1";
			address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
			Com_Printf( S_COLOR_YELLOW "WARNING: web host: vs_clientWebBind \"%s\" is no IPv4 address, taking %s\n",
				vs_clientWebBind->string, bindTo );
		}
	}

	if ( ( listenSocket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP ) ) == INVALID_SOCKET ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: web host: socket: %s\n", NET_ErrorString() );
		return;
	}

#ifndef _WIN32
	// the port is free again as soon as the server that had it is gone. Not on Windows, where this lets a
	// second program take a port that is in use.
	i = 1;
	setsockopt( listenSocket, SOL_SOCKET, SO_REUSEADDR, (char *)&i, sizeof( i ) );
#endif

	if ( !SV_WebSocketFits( listenSocket ) ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: web host: too many files are open, select() can not wait on another socket\n" );
		closesocket( listenSocket );
		listenSocket = INVALID_SOCKET;
		return;
	}
	if ( !SV_WebNonBlocking( listenSocket )
		|| bind( listenSocket, (const struct sockaddr *)&address, sizeof( address ) ) == SOCKET_ERROR
		|| listen( listenSocket, 32 ) == SOCKET_ERROR ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: web host: could not listen on %s:%d: %s\n", bindTo, port, NET_ErrorString() );
		closesocket( listenSocket );
		listenSocket = INVALID_SOCKET;
		return;
	}

	Com_Printf( "Web host listening on http://%s:%d/\n", bindTo, port );
}

/*
==============================================================================

What the engine calls

==============================================================================
*/

/*
==================
SV_WebInit

Once, when the server program starts. The listener is opened by the first NET_Sleep and again whenever
vs_clientWebPort or vs_clientWebBind change, so they are set like any other cvar: on the command line, in a config that
runs later, or in the console while the server runs.
==================
*/
void SV_WebInit( void ) {
	vs_clientWebPort = Cvar_Get( "vs_clientWebPort", "0", 0, "TCP port of the web host: the page of the client that runs in a browser, its files and its packets. 0: off" );
	vs_clientWebBind = Cvar_Get( "vs_clientWebBind", "0.0.0.0", 0, "Address the web host listens on" );
	vs_clientWebRoot = Cvar_Get( "vs_clientWebRoot", "web", 0, "Folder of the page of the web host, under fs_basepath unless it is a full path" );

	vs_clientWebPort->modified = qtrue;
}

/*
==================
SV_WebShutdown

When the program ends (NET_Shutdown), not when the server does
==================
*/
void SV_WebShutdown( void ) {
	int i, reads;

	for ( i = 0; i < SV_WEB_MAX_CONNECTIONS; i++ ) {
		webConnection_t *c = &connections[i];

		if ( c->state == WEB_FREE ) {
			continue;
		}
		if ( c->state == WEB_SOCKET && !c->drop ) {
			// the page is told that the server went away (1001), if its socket takes that right now
			SV_WebSocketEnd( c, 1001 );
			SV_WebFlush( c );

			// A socket that is closed with something in it nobody read is cut off, and what was just sent on
			// it (the disconnect of the server too) may never arrive. So what came in is taken out first.
			for ( reads = 0; reads < 8 && recv( c->sock, (char *)c->in, sizeof( c->in ), 0 ) > 0; reads++ ) {
			}
		}
		SV_WebClose( c );
	}
	if ( listenSocket != INVALID_SOCKET ) {
		closesocket( listenSocket );
		listenSocket = INVALID_SOCKET;
	}
}

/*
==================
SV_WebFDSet

In front of the select() of NET_Sleep: the sockets it is to wake up for
==================
*/
void SV_WebFDSet( fd_set *readSet, fd_set *writeSet, SOCKET *highest ) {
	int i;

	if ( !vs_clientWebPort ) {
		return;
	}
	if ( vs_clientWebPort->modified || vs_clientWebBind->modified ) {
		SV_WebListen();
	}

	if ( listenFailed ) {
		const int since = Sys_Milliseconds() - listenFailTime;

		if ( since < 0 || since >= SV_WEB_LISTEN_WAIT_MSEC ) {
			listenFailed = qfalse;
		}
	}
	if ( listenSocket != INVALID_SOCKET && !listenFailed ) {
		SV_WebSetFD( listenSocket, readSet, highest );
	}

	for ( i = 0; i < SV_WEB_MAX_CONNECTIONS; i++ ) {
		webConnection_t *c = &connections[i];

		if ( c->state == WEB_FREE ) {
			continue;
		}
		if ( c->drop ) {
			SV_WebClose( c );
			continue;
		}

		// Read only where something is awaited. What comes in while an answer goes out stays in the socket:
		// in the set it would wake the server up again and again and not be read.
		if ( c->state == WEB_REQUEST || c->state == WEB_SOCKET ) {
			SV_WebSetFD( c->sock, readSet, highest );
		}
		// what the relay of the voice chat has for this peer (not for one that is being closed: nobody would
		// read it, and the socket would wake the server up again and again)
		if ( c->state == WEB_SOCKET && c->voice ) {
			SV_WebSetFD( c->voiceSock, readSet, highest );
		}
		if ( c->state == WEB_RESPONSE || c->state == WEB_CLOSING || c->outSent < c->outLen ) {
			SV_WebSetFD( c->sock, writeSet, highest );
		}
	}
}

/*
==================
SV_WebIsClient

qtrue: a client of the game has that address, which is the one of a WebSocket peer with its port
==================
*/
static qboolean SV_WebIsClient( const netadr_t *adr ) {
	client_t	*cl;
	int			i;

	// (no server was started yet, or it is gone)
	if ( !svs.clients ) {
		return qfalse;
	}
	for ( i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++ ) {
		if ( cl->state != CS_FREE && NET_CompareAdr( adr, &cl->netchan.remoteAddress ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
==================
SV_WebTimeouts
==================
*/
static void SV_WebTimeouts( void ) {
	const int	now = Sys_Milliseconds();
	const int	since = now - lastSweep;
	int			i;

	if ( since >= 0 && since < SV_WEB_SWEEP_MSEC ) {
		return;
	}
	lastSweep = now;

	for ( i = 0; i < SV_WEB_MAX_CONNECTIONS; i++ ) {
		webConnection_t *c = &connections[i];

		if ( c->state == WEB_FREE || c->drop ) {
			continue;
		}
		// the server did not look at its sockets for a while, which is not the silence of the other side
		if ( since < 0 || since > SV_WEB_AWAY_MSEC ) {
			c->lastActive = c->lastHeard = c->clientTime = now;
			continue;
		}

		if ( c->state == WEB_SOCKET ) {
			if ( c->voice ? SV_WebVoiceIsClient( &c->adr ) : SV_WebIsClient( &c->adr ) ) {
				c->clientTime = now;
			}
			if ( now - c->lastHeard > SV_WEB_SILENT_MSEC ) {
				SV_WebDrop( c );
			} else if ( now - c->clientTime > ( c->voice ? SV_WEB_VOICE_NO_CLIENT_MSEC : SV_WEB_NO_CLIENT_MSEC ) ) {
				// no client for too long: it is told that nothing went wrong (1000), and its place is free
				SV_WebSocketEnd( c, 1000 );
			} else if ( now - c->lastHeard > SV_WEB_PING_MSEC && !c->pinged ) {
				c->pinged = qtrue;
				SV_WebSocketPing( c );
			}
		} else if ( now - c->lastActive > ( ( c->state == WEB_REQUEST ) ? SV_WEB_IDLE_MSEC : SV_WEB_STALL_MSEC ) ) {
			SV_WebDrop( c );
		}
	}
}

/*
==================
SV_WebEvent

Behind the select() of NET_Sleep, with what it found ready. Also called when nothing was.
==================
*/
void SV_WebEvent( fd_set *readSet, fd_set *writeSet ) {
	int i;

	if ( !vs_clientWebPort ) {
		return;
	}

	if ( listenSocket != INVALID_SOCKET && FD_ISSET( listenSocket, readSet ) ) {
		SV_WebAccept();
	}

	fileBudget = SV_WEB_WAKE_BYTES;
	starvedConnection = -1;

	// (no connection is closed in here, so no socket of the sets is another one by now)
	for ( i = 0; i < SV_WEB_MAX_CONNECTIONS; i++ ) {
		webConnection_t *c = &connections[( firstConnection + i ) % SV_WEB_MAX_CONNECTIONS];

		if ( c->state == WEB_FREE || c->drop ) {
			continue;
		}
		if ( FD_ISSET( c->sock, readSet ) ) {
			SV_WebRead( c );
		} else if ( c->state == WEB_SOCKET && c->inPos < c->inLen ) {
			// Frames that were read and not run: the server left the packet in front of them with an error,
			// which ended that wake-up. Nothing more may come from the peer that would get them run.
			// (The rest may also be a part of a frame, which goes on waiting for its end. What the frames
			// are answered with is sent by the next wake-up, which the socket causes if it takes it.)
			SV_WebSocketFrames( c );
		}
		// (a socket to the relay that was made in this wake-up is in no set yet)
		if ( c->voice && c->state == WEB_SOCKET && !c->drop && FD_ISSET( c->voiceSock, readSet ) ) {
			SV_WebVoiceRead( c );
		}
		if ( !c->drop && FD_ISSET( c->sock, writeSet ) ) {
			SV_WebWrite( c );
		}
	}

	// the one that found nothing left to read is served first next time, so each gets its turn
	if ( starvedConnection >= 0 ) {
		firstConnection = starvedConnection;
	}

	SV_WebTimeouts();
}
