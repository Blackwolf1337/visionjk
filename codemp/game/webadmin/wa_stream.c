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

// wa_stream.c -- the WebSocket of a page: the state pushed twenty times a second, and what the page has
// subscribed to besides it when that changes

#include "game/webadmin/wa_local.h"

/*
=================
The socket of a page

Not in JKAdmin. A page that asked ten times a second over HTTP waited for every answer twice: for the server
frame the question was read in, and for the way there and back, and in between its players stood still. The
socket of a page (GET /api/stream of the panel, /live/stream of the live view, RFC 6455 with the frames of
shared/web/web_ws.c) is pushed instead.

What goes out is a text message: a line with what it is and the HTTP status of the answer, then the answer
as GET of the same address has it. "state 200" is /api/state (/live/api/state for the live view), every
WA_STREAM_MS, asked with what the page has after the last one (WA_StateNext): only what changed comes again.
A page subscribes to more with text messages of its own, a page of the live view to the texture swaps alone:

  state <query>               view= and hud= of /api/state, from now on
  sub <name> <path>?<query>   the answer of a GET of the panel under <name>, now and whenever it is another
                              one (asked every WA_CHANNEL_MS). Only what WA_StreamAllowed names (the texture
                              swaps, the chat, the entities, the clips, the log, the console, the intrusion
                              detection), and only as far as the role of the page lets it.
  unsub <name>

A page that does not read is not pushed more than WA_STREAM_BACKLOG: what does not fit is left out, the state
is then asked with what the page has, and a subscription is sent again. The login of the panel is asked again
every second with the Cookie and Authorization of the upgrade, a page that is logged out loses its socket.
=================
*/

#define WA_STREAM_MS		50					// the state, twenty times a second
#define WA_CHANNEL_MS		250					// what a page subscribed to, asked four times a second
#define WA_STREAM_BACKLOG	( 512 * 1024 )		// what may wait unsent before a push is left out
#define WA_STREAM_MAX_IN	4096				// the longest message of a page
#define WA_STREAM_CACHE		4					// answers of the live view kept for one push

static qboolean	stateDue, channelsDue;
static int		lastState, lastChannels;

// What the live view is pushed in this frame. All of its pages have the same after their first push, so one
// answer serves them: kept by the question it answers, with what the pages have after it.
static struct {
	char		query[1100];
	char		next[1024];
	waBuffer_t	body;
	int			status;
	qboolean	used;
} liveCache[WA_STREAM_CACHE];

// a connection that answers a page's subscription the way it would answer the page itself
static waConnection_t capture;

// How often a subscription is asked. The list of the entities changes all the time (what fired, the movers) and
// is long, and the editor builds its table anew with every answer: as often as it asked before the socket.
static int WA_StreamEvery( const char *path ) {
	return !strcmp( path, "/api/entities" ) ? 1000 : WA_CHANNEL_MS;
}

// The GETs a page may subscribe to: what it asked for again and again before the socket. The live view only the
// texture swaps, which every player sees (wa_live.c hands them out too).
static qboolean WA_StreamAllowed( const char *path, qboolean live ) {
	static const char *const paths[] = { "/api/remaps", "/api/chat", "/api/entities", "/api/clips", "/api/log", "/api/console", "/api/ids" };
	size_t i;

	if ( live )
		return (qboolean)!strcmp( path, "/api/remaps" );
	for ( i = 0; i < ARRAY_LEN( paths ); i++ ) {
		if ( !strcmp( path, paths[i] ) )
			return qtrue;
	}
	return qfalse;
}

static unsigned WA_StreamHash( int status, const char *data, int len ) {
	unsigned	h = 2166136261u ^ (unsigned)status;
	int			i;

	for ( i = 0; i < len; i++ )
		h = ( h ^ (unsigned char)data[i] ) * 16777619u;
	return h ? h : 1;
}

/*
==============================================================================

Frames

==============================================================================
*/

// A frame behind what waits to be sent. qfalse: the page has more than WA_STREAM_BACKLOG waiting, and none of it
// is sent. The parts are put one behind the other into its payload.
static qboolean WA_StreamSend( waConnection_t *c, int opcode, const char *a, int aLen, const char *b, int bLen ) {
	const int	len = aLen + bLen, size = Web_WsFrameSize( len );
	char		*out;
	int			headLen;

	if ( c->closing || c->response.len - c->sent + size > WA_STREAM_BACKLOG )
		return qfalse;
	if ( !WA_BufReserve( &c->response, size ) ) {
		WA_CloseConnection( c );
		return qfalse;
	}
	out = c->response.data + c->response.len;
	// the head, then the parts behind it: they are not copied together first
	headLen = Web_WsFrameHeadOut( (unsigned char *)out, opcode, len );
	if ( aLen )
		memcpy( out + headLen, a, aLen );
	if ( bLen )
		memcpy( out + headLen + aLen, b, bLen );
	c->response.len += size;
	return qtrue;
}

// a message "<name> <status>\n<body>"
static qboolean WA_StreamMessage( waConnection_t *c, const char *name, int status, const char *body, int bodyLen ) {
	char line[48];

	Com_sprintf( line, sizeof( line ), "%s %d\n", name, status );
	return WA_StreamSend( c, WEB_OP_TEXT, line, (int)strlen( line ), body, bodyLen );
}

// tells the page why (a close frame) and reads nothing more, the connection ends when that is sent
static void WA_StreamEnd( waConnection_t *c, int code ) {
	const char status[2] = { (char)( code >> 8 ), (char)code };

	WA_StreamSend( c, WEB_OP_CLOSE, status, sizeof( status ), NULL, 0 );
	c->closing = qtrue;
}

/*
==============================================================================

The upgrade

==============================================================================
*/

/*
=================
WA_StreamUpgrade

GET /api/stream or /live/stream with the headers of a WebSocket (RFC 6455, 4.2). Only for a page of this
address: a page of another site that opened the socket of the panel would read it with the login of whoever
looks at it, and the live view is for the website it is on. The browser names the site in Origin, which no
page can leave out or change.
=================
*/
void WA_StreamUpgrade( waConnection_t *c, const char *head, qboolean live ) {
	char	key[64], raw[32], accept[32], value[1024];
	int		i;

	if ( !Web_HeaderValue( head, "Upgrade", value, sizeof( value ) ) || !Q_stristr( value, "websocket" )
		|| !Web_HeaderValue( head, "Connection", value, sizeof( value ) ) || !Q_stristr( value, "upgrade" )
		|| !Web_HeaderValue( head, "Sec-WebSocket-Version", value, sizeof( value ) ) || strcmp( value, "13" ) ) {
		WA_Respond( c, 426, "Upgrade Required", "text/plain", "Upgrade: websocket\r\nSec-WebSocket-Version: 13\r\n", "websocket", 9 );
		return;
	}
	if ( !Web_HeaderValue( head, "Sec-WebSocket-Key", key, sizeof( key ) ) || strlen( key ) != WEB_SOCKET_KEY_LEN
		|| Web_Base64Decode( key, raw, sizeof( raw ) ) != 16 ) {
		WA_RespondText( c, 400, "Bad Request", "bad key" );
		return;
	}
	if ( !Web_FindHeader( head, "Origin" ) || !Web_SameOrigin( head ) ) {
		WA_RespondText( c, 403, "Forbidden", "request from another site refused" );
		return;
	}
	if ( live && WA_LiveStreams() >= WA_MAX_LIVE_STREAMS ) {
		WA_Respond( c, 503, "Service Unavailable", "text/plain", "Retry-After: 30\r\n", "full", 4 );
		return;
	}

	Web_WsAcceptKey( key, accept, sizeof( accept ) );
	WA_BufPrintf( &c->response,
		"HTTP/1.1 101 Switching Protocols\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Accept: %s\r\n"
		"\r\n", accept );

	c->stream = qtrue;
	c->live = live;
	c->closing = qfalse;
	c->requestLen = 0;
	c->sent = 0;
	c->lastActive = trap->Milliseconds();
	c->nextAuth = c->lastActive + 1000;
	// the first push: the items and the effects as they are, the events and the prints from now on
	Q_strncpyz( c->stateHas, live ? "items=0&fx=0&fxs=-1" : "items=0&fx=0&fxs=-1&prs=-1", sizeof( c->stateHas ) );
	c->stateWants[0] = '\0';
	for ( i = 0; i < WA_MAX_CHANNELS; i++ )
		c->channels[i].name[0] = '\0';

	// what the login is asked with again: a head as WA_AuthRequest reads one, the line of the request first
	Q_strncpyz( c->streamAuth, "GET /api/stream HTTP/1.1\r\n", sizeof( c->streamAuth ) );
	if ( !live ) {
		if ( Web_HeaderValue( head, "Cookie", value, sizeof( value ) ) )
			Q_strcat( c->streamAuth, sizeof( c->streamAuth ), va( "Cookie: %s\r\n", value ) );
		if ( Web_HeaderValue( head, "Authorization", value, sizeof( value ) ) )
			Q_strcat( c->streamAuth, sizeof( c->streamAuth ), va( "Authorization: %s\r\n", value ) );
	}
}

/*
==============================================================================

What a page says

==============================================================================
*/

static void WA_StreamText( waConnection_t *c, char *text ) {
	char	*arg = strchr( text, ' ' ), *path, *query;
	char	value[16];
	int		i, free = -1;

	if ( arg )
		*arg++ = '\0';
	else
		arg = text + strlen( text );

	if ( !strcmp( text, "state" ) ) {
		// the live view has the state as everybody sees it, no view and no HUD of a player
		if ( c->live )
			return;
		c->stateWants[0] = '\0';
		if ( WA_FormValue( arg, "view", value, sizeof( value ) ) )
			Q_strcat( c->stateWants, sizeof( c->stateWants ), va( "&view=%d", atoi( value ) ) );
		if ( WA_FormValue( arg, "hud", value, sizeof( value ) ) )
			Q_strcat( c->stateWants, sizeof( c->stateWants ), va( "&hud=%d", atoi( value ) ) );
		return;
	}

	if ( !strcmp( text, "sub" ) || !strcmp( text, "unsub" ) ) {
		const qboolean	sub = (qboolean)!strcmp( text, "sub" );
		char			*name = arg;

		path = strchr( name, ' ' );
		if ( path )
			*path++ = '\0';
		if ( !name[0] || strlen( name ) >= sizeof( c->channels[0].name ) )
			return;
		for ( i = 0; i < WA_MAX_CHANNELS; i++ ) {
			if ( !strcmp( c->channels[i].name, name ) )
				break;
			if ( free < 0 && !c->channels[i].name[0] )
				free = i;
		}
		if ( !sub ) {
			if ( i < WA_MAX_CHANNELS )
				c->channels[i].name[0] = '\0';
			return;
		}
		if ( !path )
			return;
		query = strchr( path, '?' );
		if ( query )
			*query++ = '\0';
		else
			query = (char *)"";
		if ( !WA_StreamAllowed( path, c->live ) || ( !c->live && c->role < WA_RouteRole( "GET", path ) )
			|| strlen( query ) >= sizeof( c->channels[0].query ) ) {
			// the page is told, as an answer of that name
			WA_StreamMessage( c, name, 403, "", 0 );
			return;
		}
		if ( i == WA_MAX_CHANNELS )
			i = free;
		if ( i < 0 )
			return;
		Q_strncpyz( c->channels[i].name, name, sizeof( c->channels[i].name ) );
		Q_strncpyz( c->channels[i].path, path, sizeof( c->channels[i].path ) );
		Q_strncpyz( c->channels[i].query, query, sizeof( c->channels[i].query ) );
		// asked at the next round, and sent whatever it was before
		c->channels[i].hash = 0;
		c->channels[i].due = 0;
	}
}

/*
=================
WA_StreamRead

What the page sent: its frames, as far as they are whole
=================
*/
void WA_StreamRead( waConnection_t *c ) {
	int	received, pos = 0;

	if ( c->closing )
		return;
	received = recv( c->sock, c->request + c->requestLen, WA_MAX_REQUEST - c->requestLen, 0 );
	if ( received == 0 || ( received < 0 && !BG_SocketWouldBlock() ) ) {
		WA_CloseConnection( c );
		return;
	}
	if ( received < 0 )
		return;
	c->requestLen += received;
	c->lastActive = trap->Milliseconds();

	while ( !c->closing ) {
		webFrame_t		frame;
		unsigned char	*data = (unsigned char *)c->request + pos;
		const int		len = c->requestLen - pos;
		const int		headLen = Web_WsFrameHead( data, len, WA_STREAM_MAX_IN, &frame );

		if ( headLen < 0 ) {
			WA_StreamEnd( c, headLen == WEB_FRAME_LONG ? WEB_CLOSE_TOO_BIG : WEB_CLOSE_PROTOCOL );
			break;
		}
		if ( !headLen || len - headLen < frame.length )
			break;		// the rest of it is to come
		Web_WsFrameUnmask( data + headLen, frame.length, frame.mask );
		pos += headLen + frame.length;

		switch ( frame.opcode ) {
		case WEB_OP_TEXT:
			if ( !frame.fin ) {
				// a page sends a few words, never a message in pieces
				WA_StreamEnd( c, WEB_CLOSE_TOO_BIG );
			} else {
				char text[WA_STREAM_MAX_IN + 1];

				memcpy( text, data + headLen, frame.length );
				text[frame.length] = '\0';
				WA_StreamText( c, text );
			}
			break;
		case WEB_OP_PING:
			WA_StreamSend( c, WEB_OP_PONG, (const char *)data + headLen, frame.length, NULL, 0 );
			break;
		case WEB_OP_PONG:
			break;
		case WEB_OP_CLOSE:
			WA_StreamEnd( c, WEB_CLOSE_NORMAL );
			break;
		default:
			WA_StreamEnd( c, WEB_CLOSE_DATA );
			break;
		}
	}
	if ( !c->inUse )
		return;
	memmove( c->request, c->request + pos, c->requestLen - pos );
	c->requestLen -= pos;
	if ( c->requestLen >= WA_MAX_REQUEST )
		WA_StreamEnd( c, WEB_CLOSE_TOO_BIG );
}

/*
==============================================================================

What a page is pushed

==============================================================================
*/

// The answer of the panel to a GET of the page, as status and body: through a connection that is the page's in
// all that the answer may depend on, and whose answer goes nowhere.
static const char *WA_StreamCapture( const waConnection_t *c, const char *path, const char *query, int *status, int *len ) {
	const char *body;

	capture.response.len = 0;
	capture.sent = 0;
	capture.file = 0;
	capture.role = c->role;
	capture.session = c->session;
	capture.peer = c->peer;
	capture.addr = c->addr;
	capture.proxied = c->proxied;
	capture.https = c->https;
	Q_strncpyz( capture.user, c->user, sizeof( capture.user ) );

	if ( !strcmp( path, "/api/state" ) ) {
		if ( c->live )
			WA_ApiLiveState( &capture, query );
		else
			WA_ApiState( &capture, query );
	} else {
		WA_Route( &capture, "GET", path, query, (char *)"", 0 );
	}
	// (none of what may be asked for is a file that is handed out piece by piece)
	if ( capture.file ) {
		trap->FS_Close( capture.file );
		capture.file = 0;
	}

	*status = 500;
	*len = 0;
	if ( !capture.response.len )
		return "";
	capture.response.data[capture.response.len] = '\0';
	*status = atoi( capture.response.data + 9 );
	body = strstr( capture.response.data, "\r\n\r\n" );
	if ( !body )
		return "";
	body += 4;
	*len = (int)( capture.response.data + capture.response.len - body );
	return body;
}

// the state for a page, with what it has after it in next
static const char *WA_StreamState( waConnection_t *c, int *status, int *len, char *next, int nextSize ) {
	char		query[1100];
	const char	*body;
	int			i;

	Com_sprintf( query, sizeof( query ), "%s%s", c->stateHas, c->stateWants );
	if ( c->live ) {
		for ( i = 0; i < WA_STREAM_CACHE; i++ ) {
			if ( liveCache[i].used && !strcmp( liveCache[i].query, query ) ) {
				Q_strncpyz( next, liveCache[i].next, nextSize );
				*status = liveCache[i].status;
				*len = liveCache[i].body.len;
				return liveCache[i].body.data ? liveCache[i].body.data : "";
			}
		}
	}

	body = WA_StreamCapture( c, "/api/state", query, status, len );
	WA_StateNext( next, nextSize );

	if ( c->live ) {
		for ( i = 0; i < WA_STREAM_CACHE && liveCache[i].used; i++ )
			;
		if ( i < WA_STREAM_CACHE ) {
			liveCache[i].used = qtrue;
			Q_strncpyz( liveCache[i].query, query, sizeof( liveCache[i].query ) );
			Q_strncpyz( liveCache[i].next, next, sizeof( liveCache[i].next ) );
			liveCache[i].status = *status;
			liveCache[i].body.len = 0;
			WA_BufAppend( &liveCache[i].body, body, *len );
			return liveCache[i].body.data;
		}
	}
	return body;
}

/*
=================
WA_StreamTick

At the start of every frame of the panel: whether the state and the subscriptions are due in it. The answers
of the live view of the last push are let go.
=================
*/
void WA_StreamTick( int now ) {
	int i;

	stateDue = (qboolean)( now - lastState >= WA_STREAM_MS );
	if ( stateDue ) {
		// (on the beat, unless the frames fell far behind it)
		lastState = ( now - lastState < 2 * WA_STREAM_MS ) ? lastState + WA_STREAM_MS : now;
		for ( i = 0; i < WA_STREAM_CACHE; i++ )
			liveCache[i].used = qfalse;
	}
	channelsDue = (qboolean)( now - lastChannels >= WA_CHANNEL_MS );
	if ( channelsDue )
		lastChannels = now;
}

/*
=================
WA_StreamFrame

What a socket is pushed in this frame, and whether its page is still logged in
=================
*/
void WA_StreamFrame( waConnection_t *c, int now ) {
	int i;

	if ( c->closing || c->response.len - c->sent > WA_STREAM_BACKLOG / 2 )
		return;

	if ( !c->live && now - c->nextAuth >= 0 ) {
		c->nextAuth = now + 1000;
		WA_AuthRequest( c, c->streamAuth );
		if ( c->role == WA_ROLE_NONE ) {
			// 1008: a policy of this end, here the login that is gone
			WA_StreamEnd( c, 1008 );
			return;
		}
		// a subscription the role of the page no longer lets it have
		for ( i = 0; i < WA_MAX_CHANNELS; i++ ) {
			if ( c->channels[i].name[0] && c->role < WA_RouteRole( "GET", c->channels[i].path ) ) {
				WA_StreamMessage( c, c->channels[i].name, 403, "", 0 );
				c->channels[i].name[0] = '\0';
			}
		}
	}

	if ( stateDue ) {
		char		next[1024];
		const char	*body;
		int			status, len;

		body = WA_StreamState( c, &status, &len, next, sizeof( next ) );
		if ( WA_StreamMessage( c, "state", status, body, len ) && status == 200 )
			Q_strncpyz( c->stateHas, next, sizeof( c->stateHas ) );
	}

	if ( channelsDue ) {
		for ( i = 0; i < WA_MAX_CHANNELS && c->inUse; i++ ) {
			const char	*body;
			unsigned	hash;
			int			status, len;

			if ( !c->channels[i].name[0] || now - c->channels[i].due < 0 )
				continue;
			c->channels[i].due = now + WA_StreamEvery( c->channels[i].path ) - WA_CHANNEL_MS / 2;
			body = WA_StreamCapture( c, c->channels[i].path, c->channels[i].query, &status, &len );
			hash = WA_StreamHash( status, body, len );
			if ( hash != c->channels[i].hash && WA_StreamMessage( c, c->channels[i].name, status, body, len ) )
				c->channels[i].hash = hash;
		}
	}
}
