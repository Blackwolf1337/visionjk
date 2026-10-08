/*
===========================================================================
Copyright (C) 2013 - 2015, OpenJK contributors
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

// g_webadmin.c -- interactive web admin panel served by the game module.
//
// A tiny non-blocking HTTP server that is polled once per server frame. It
// serves one embedded page (3D view of the map and all players, RTS style),
// the map's .bsp and its textures, the live game state as JSON and a set of
// admin actions (kick, ban, set health, scale, teleport, ...).
//
// It only starts when g_webPassword is set. Every request needs HTTP basic
// auth with that password (any user name).
//
// This file is the server itself: the connections, which request goes to which file, and what the game calls.
// What the files are is in wa_local.h.

#include "game/webadmin/wa_local.h"
#include "webadmin_html.h"

// A file of the game is read as it is sent, so much in a frame for one connection and so much for all of them.
// Read in one go, a big one (the animations of the players, 8 MB out of a pk3) took a frame of 30 ms.
#define WA_FILE_CHUNK		( 256 * 1024 )
#define WA_FILE_FRAME		( 512 * 1024 )
static int	fileBudget;		// what may still be read in this frame

static bgSocket_t		listenSocket = BG_INVALID_SOCKET;
static waConnection_t	connections[WA_MAX_CONNECTIONS];
static int				firstConnection;	// the one WebAdmin_Frame starts with
static int				listenPort;

/*
==============================================================================

Connections

==============================================================================
*/

// qtrue: the panel listens. It does not without a password, or when its port could not be had.
qboolean WA_Running( void ) {
	return (qboolean)( listenSocket != BG_INVALID_SOCKET );
}

static void WA_CloseConnection( waConnection_t *c ) {
	if ( c->file )
		trap->FS_Close( c->file );
	if ( c->sock != BG_INVALID_SOCKET )
		BG_CloseSocket( c->sock );
	WA_BufFree( &c->response );
	memset( c, 0, sizeof( *c ) );
	c->sock = BG_INVALID_SOCKET;
}

// Handles a complete request. The response goes into c->response.
// A file of the folder g_webRoot names, added to a buffer. qfalse: it can not be read.
static qboolean WA_PageFile( waBuffer_t *b, const char *name ) {
	FILE	*f = fopen( va( "%s/%s", g_webRoot.string, name ), "rb" );
	char	chunk[8192];
	size_t	n;

	if ( !f )
		return qfalse;
	while ( ( n = fread( chunk, 1, sizeof( chunk ), f ) ) > 0 )
		WA_BufAppend( b, chunk, (int)n );
	fclose( f );
	return qtrue;
}

// The page as the build puts it together (cmake/Modules/BundlePage.cmake), from the files of the folder
// g_webRoot names: index.html, and for every line "<!--#include name -->" in it the file of that name in its
// place. For working on the page: a change to one of its files is there with the next load of the page.
static qboolean WA_PageFromDisk( waBuffer_t *page ) {
	static const char	mark[] = "<!--#include ";
	waBuffer_t			text = { 0 };
	const char			*at, *include;
	qboolean			whole = qtrue;

	if ( !WA_PageFile( &text, "index.html" ) || !text.len ) {
		WA_BufFree( &text );
		return qfalse;
	}

	for ( at = text.data; ( include = strstr( at, mark ) ) != NULL; ) {
		const char	*name = include + sizeof( mark ) - 1, *end = strstr( name, " -->" ), *line;
		char		file[MAX_QPATH];

		if ( !end || end - name >= (int)sizeof( file ) )
			break;
		WA_BufAppend( page, at, (int)( include - at ) );
		Q_strncpyz( file, name, (int)( end - name ) + 1 );
		// only what is in the folder
		if ( strstr( file, ".." ) || file[0] == '/' || strchr( file, '\\' ) || strchr( file, ':' ) || !WA_PageFile( page, file ) ) {
			trap->Print( S_COLOR_YELLOW "Web admin: %s/index.html includes %s, which can not be read\n", g_webRoot.string, file );
			whole = qfalse;
		}
		// the line of the include goes with it
		line = strchr( end, '\n' );
		at = line ? line + 1 : end + strlen( end );
	}
	WA_BufAppend( page, at, (int)strlen( at ) );
	WA_BufFree( &text );
	return whole;
}

static void WA_HandleRequest( waConnection_t *c, char *head, char *body, int bodyLen ) {
	char	method[8], path[4096];
	char	*query;

	if ( sscanf( head, "%7s %4095s", method, path ) != 2 ) {
		WA_RespondText( c, 400, "Bad Request", "bad request" );
		return;
	}
	// an address that fills the buffer was cut, and half a file name is another file name
	if ( strlen( path ) >= sizeof( path ) - 1 ) {
		WA_RespondText( c, 414, "URI Too Long", "address too long" );
		return;
	}

	if ( WA_AuthThrottle( c->addr ) ) {
		WA_Respond( c, 429, "Too Many Requests", "text/plain", "Retry-After: 30\r\n", "too many wrong passwords, wait", 30 );
		return;
	}
	if ( !WA_CheckAuth( head ) ) {
		const qboolean tried = ( WA_FindHeader( head, "Authorization" ) != NULL );

		// only count a wrong password, not the first request a browser sends without one
		if ( tried ) {
			WA_AuthResult( c->addr, qfalse );
			WA_LogUse( c, "wrong password" );
		}
		WA_Respond( c, 401, "Unauthorized", "text/plain", "WWW-Authenticate: Basic realm=\"VisionJK web admin\"\r\n",
			"login required", 14 );
		return;
	}
	WA_AuthResult( c->addr, qtrue );

	if ( !strcmp( method, "POST" ) && !WA_SameOrigin( head ) ) {
		WA_RespondText( c, 403, "Forbidden", "request from another site refused" );
		return;
	}

	query = strchr( path, '?' );
	if ( query )
		*query++ = '\0';
	else
		query = (char *)"";

	if ( !strcmp( method, "GET" ) && ( !strcmp( path, "/" ) || !strcmp( path, "/index.html" ) ) ) {
		// g_webRoot serves the page from disk instead, for working on it without a restart
		waBuffer_t page = { 0 };

		if ( g_webRoot.string[0] && WA_PageFromDisk( &page ) ) {
			WA_Respond( c, 200, "OK", "text/html; charset=utf-8", "Cache-Control: no-store\r\n", page.data, page.len );
			WA_BufFree( &page );
		} else {
			WA_BufFree( &page );
			WA_Respond( c, 200, "OK", "text/html; charset=utf-8", "Cache-Control: no-store\r\n", webadmin_html, (int)webadmin_html_len );
		}
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/anims" ) ) {
		WA_ApiAnims( c );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/state" ) ) {
		WA_ApiState( c, query );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/map" ) ) {
		WA_ApiMap( c );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/file" ) ) {
		WA_ApiFile( c, query );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/action" ) ) {
		WA_ApiAction( c, body );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/entities" ) ) {
		WA_ApiEntities( c );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/classes" ) ) {
		WA_ApiClasses( c );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/export" ) ) {
		WA_ApiExport( c, query );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/chat" ) ) {
		WA_ApiChat( c, query );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/entity" ) ) {
		WA_ApiEntity( c, body );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/layout" ) ) {
		WA_ApiLayout( c, body );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/config" ) ) {
		WA_ApiConfigGet( c );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/config" ) ) {
		char run[4];

		WA_FormValue( query, "exec", run, sizeof( run ) );
		WA_ApiConfigSet( c, body, (qboolean)( run[0] == '1' ) );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/models" ) ) {
		WA_ApiModels( c );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/shaders" ) ) {
		WA_ApiShaders( c );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/classdoc" ) ) {
		WA_ApiClassDoc( c );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/list" ) ) {
		WA_ApiList( c, query );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/bans" ) ) {
		WA_ApiBans( c );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/waypoints" ) ) {
		WA_ApiWaypoints( c );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/waypoint" ) ) {
		WA_ApiWaypoint( c, body );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/fs/list" ) ) {
		WA_ApiFsList( c, query );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/log" ) ) {
		WA_ApiLog( c, query );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/perf" ) ) {
		WA_ApiPerf( c, query );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/fs/get" ) ) {
		WA_ApiFsGet( c, query );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/fs/put" ) ) {
		WA_ApiFsPut( c, query, body, bodyLen );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/fs/delete" ) ) {
		WA_ApiFsDelete( c, query );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/fs/rename" ) ) {
		WA_ApiFsRename( c, query );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/fs/mkdir" ) ) {
		WA_ApiFsMkdir( c, query );
	} else {
		WA_RespondText( c, 404, "Not Found", "not found" );
	}
}

// A Content-Length: only digits, and nothing a request can not be. JKAdmin read it with atoi and then checked
// headLen + bodyLen, which wraps for a length near INT_MAX: the check passed and the terminating zero of the
// body was written about 2 GB in front of the request buffer, before any login. Returns -1 for a bad value.
static int WA_ParseLength( const char *s ) {
	int len = 0, digits = 0;

	for ( ; *s >= '0' && *s <= '9'; s++, digits++ ) {
		len = len * 10 + ( *s - '0' );
		if ( len > WA_MAX_REQUEST )
			return -1;
	}
	while ( *s == ' ' || *s == '\t' )
		s++;
	if ( !digits || ( *s && *s != '\r' && *s != '\n' ) )
		return -1;
	return len;
}

static void WA_ReadRequest( waConnection_t *c ) {
	char		*headEnd;
	const char	*lenHeader;
	int			received, bodyLen = 0, headLen;

	received = recv( c->sock, c->request + c->requestLen, WA_MAX_REQUEST - c->requestLen, 0 );
	if ( received == 0 || ( received < 0 && !BG_SocketWouldBlock() ) ) {
		WA_CloseConnection( c );
		return;
	}
	if ( received < 0 )
		return;

	c->requestLen += received;
	c->request[c->requestLen] = '\0';
	c->lastActive = trap->Milliseconds();

	headEnd = strstr( c->request, "\r\n\r\n" );
	if ( !headEnd ) {
		if ( c->requestLen >= WA_MAX_REQUEST )
			WA_CloseConnection( c );
		return;
	}

	headLen = (int)( headEnd - c->request ) + 4;
	*headEnd = '\0';
	lenHeader = WA_FindHeader( c->request, "Content-Length" );
	if ( lenHeader )
		bodyLen = WA_ParseLength( lenHeader );
	*headEnd = '\r';

	// (headLen is at most WA_MAX_REQUEST, the subtraction can not wrap)
	if ( bodyLen < 0 || bodyLen > WA_MAX_REQUEST - headLen ) {
		WA_CloseConnection( c );
		return;
	}
	if ( c->requestLen < headLen + bodyLen )
		return;		// body still coming

	*headEnd = '\0';
	c->request[headLen + bodyLen] = '\0';
	WA_HandleRequest( c, c->request, c->request + headLen, bodyLen );
}

// The next piece of the file a connection hands out, in the place of what was sent. qfalse: there is none, or
// nothing may be read any more in this frame.
static qboolean WA_FileMore( waConnection_t *c ) {
	int n = c->fileLeft;

	if ( n > WA_FILE_CHUNK )
		n = WA_FILE_CHUNK;
	if ( n > fileBudget )
		n = fileBudget;
	if ( n <= 0 )
		return qfalse;

	c->response.len = 0;
	c->sent = 0;
	if ( !WA_BufReserve( &c->response, n ) ) {
		WA_CloseConnection( c );
		return qfalse;
	}
	trap->FS_Read( c->response.data, n, c->file );
	c->response.len = n;
	c->fileLeft -= n;
	fileBudget -= n;
	if ( c->fileLeft <= 0 ) {
		trap->FS_Close( c->file );
		c->file = 0;
	}
	return qtrue;
}

static void WA_WriteResponse( waConnection_t *c ) {
	do {
		while ( c->sent < c->response.len ) {
			const int n = send( c->sock, c->response.data + c->sent, c->response.len - c->sent, BG_SEND_FLAGS );
			if ( n < 0 ) {
				if ( !BG_SocketWouldBlock() )
					WA_CloseConnection( c );
				return;
			}
			c->sent += n;
			c->lastActive = trap->Milliseconds();
		}
	} while ( WA_FileMore( c ) );

	if ( !c->inUse )
		return;
	if ( c->fileLeft > 0 ) {
		// it waits for the next frame, not for the other side
		c->lastActive = trap->Milliseconds();
		return;
	}
	WA_CloseConnection( c );
}

static void WA_Accept( void ) {
	for ( ;; ) {
		struct sockaddr_in	from;
		socklen_t			fromLen = sizeof( from );
		bgSocket_t			s = accept( listenSocket, (struct sockaddr *)&from, &fromLen );
		int					i;

		if ( s == BG_INVALID_SOCKET )
			return;

		BG_SocketNonBlocking( s );
#ifdef SO_NOSIGPIPE
		{
			int on = 1;
			setsockopt( s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof( on ) );
		}
#endif

		for ( i = 0; i < WA_MAX_CONNECTIONS; i++ ) {
			if ( !connections[i].inUse )
				break;
		}
		if ( i == WA_MAX_CONNECTIONS ) {
			BG_CloseSocket( s );
			continue;
		}

		memset( &connections[i], 0, sizeof( connections[i] ) );
		connections[i].inUse = qtrue;
		connections[i].sock = s;
		connections[i].addr = (unsigned int)from.sin_addr.s_addr;	// network order, only compared and printed
		connections[i].lastActive = trap->Milliseconds();
	}
}

/*
==============================================================================

Public

==============================================================================
*/

void WebAdmin_Init( void ) {
	struct sockaddr_in	addr;
	int					on = 1, i;

	for ( i = 0; i < WA_MAX_CONNECTIONS; i++ )
		connections[i].sock = BG_INVALID_SOCKET;

	// before anything can keep the panel off: the file of a reload must not wait for a later start of this map
	WA_ReloadRead();

	if ( !g_webPassword.string[0] ) {
		if ( g_webPort.integer > 0 )
			trap->Print( "Web admin is off, set g_webPassword to start it\n" );
		return;
	}
	if ( g_webPort.integer <= 0 || g_webPort.integer > 65535 )
		return;

	BG_NetStart();

	listenSocket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	if ( listenSocket == BG_INVALID_SOCKET ) {
		trap->Print( S_COLOR_YELLOW "Web admin: could not create a socket\n" );
		return;
	}
	setsockopt( listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof( on ) );

	memset( &addr, 0, sizeof( addr ) );
	addr.sin_family = AF_INET;
	addr.sin_port = htons( (unsigned short)g_webPort.integer );
	// default to localhost, not every interface: the panel is plain HTTP, reaching it from outside should be a
	// choice (g_webBind 0.0.0.0, or a reverse proxy). An unset or unparseable g_webBind binds to 127.0.0.1.
	if ( !g_webBind.string[0] || inet_pton( AF_INET, g_webBind.string, &addr.sin_addr ) != 1 )
		inet_pton( AF_INET, "127.0.0.1", &addr.sin_addr );

	if ( bind( listenSocket, (struct sockaddr *)&addr, sizeof( addr ) ) != 0 || listen( listenSocket, 16 ) != 0 ) {
		trap->Print( S_COLOR_YELLOW "Web admin: could not listen on %s:%d (port in use, or below 1024 without root?)\n",
			g_webBind.string[0] ? g_webBind.string : "0.0.0.0", g_webPort.integer );
		BG_CloseSocket( listenSocket );
		listenSocket = BG_INVALID_SOCKET;
		return;
	}

	BG_SocketNonBlocking( listenSocket );
	WA_ChatInit();
	listenPort = g_webPort.integer;
	trap->Print( "Web admin listening on http://%s:%d/\n", g_webBind.string[0] ? g_webBind.string : "0.0.0.0", listenPort );
}

void WebAdmin_Frame( void ) {
	const int	now = trap->Milliseconds();
	int			i;

	WA_EntitiesFrame();

	if ( !WA_Running() )
		return;

	WA_PerfFrameBegin();

	WA_AssetsFrame();

	WA_GravityFrame();

	WA_Accept();

	// what is read of files in this frame. Another connection is the first each frame, so none waits for long
	// behind the ones in front of it.
	fileBudget = WA_FILE_FRAME;
	firstConnection = ( firstConnection + 1 ) % WA_MAX_CONNECTIONS;

	for ( i = 0; i < WA_MAX_CONNECTIONS; i++ ) {
		waConnection_t *c = &connections[( firstConnection + i ) % WA_MAX_CONNECTIONS];

		if ( !c->inUse )
			continue;

		if ( now - c->lastActive > WA_TIMEOUT_MS ) {
			WA_CloseConnection( c );
			continue;
		}

		if ( !c->response.len )
			WA_ReadRequest( c );
		if ( c->inUse && c->response.len )
			WA_WriteResponse( c );
	}

	WA_PerfPanelDone();
}

void WebAdmin_Shutdown( void ) {
	int i;

	for ( i = 0; i < WA_MAX_CONNECTIONS; i++ ) {
		if ( connections[i].inUse )
			WA_CloseConnection( &connections[i] );
	}
	if ( listenSocket != BG_INVALID_SOCKET ) {
		BG_CloseSocket( listenSocket );
		listenSocket = BG_INVALID_SOCKET;
	}

	WA_EntitiesShutdown();
	WA_AssetsShutdown();
}

