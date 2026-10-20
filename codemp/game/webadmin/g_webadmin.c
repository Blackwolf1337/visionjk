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
// It only starts when it has an account (wa_auth.c). A visitor who is not logged in gets the page of the
// login and nothing else.
//
// This file is the server itself: the connections, which request goes to which file, and what the game calls.
// What the files are is in wa_local.h.

#include "game/webadmin/wa_local.h"
#include "webadmin_html.h"
#include "webadmin_login.h"

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

/*
==============================================================================

What the game tells all of its clients at once

A line for their console ("print") and a centre print ("cp") go straight to the engine from some four hundred
places of the game and are in no state the panel could read. So the game calls the engine through a copy of the
engine's table in which SendServerCommand is the one below: it shows the panel what is sent to everybody and
hands it on unchanged. Every other call of the table is the engine's own.

==============================================================================
*/

static gameImport_t	waImport;
static void			(*waSendServerCommand)( int clientNum, const char *text );

static void WA_SendServerCommand( int clientNum, const char *text ) {
	if ( clientNum == -1 && text )
		WA_PrintsTake( text );
	waSendServerCommand( clientNum, text );
}

gameImport_t *WebAdmin_WrapImport( gameImport_t *import ) {
	// (the table of an engine that does not say vs_voiceShared ends before ext, g_public.h)
	memset( &waImport, 0, sizeof( waImport ) );
	memcpy( &waImport, import, import->Cvar_VariableIntegerValue( "vs_voiceShared" ) ? sizeof( waImport ) : offsetof( gameImport_t, ext ) );
	waSendServerCommand = import->SendServerCommand;
	waImport.SendServerCommand = WA_SendServerCommand;
	return &waImport;
}

void WA_CloseConnection( waConnection_t *c ) {
	if ( c->loginWait )
		WA_AuthLoginGone( c );
	if ( c->file )
		trap->FS_Close( c->file );
	if ( c->sock != BG_INVALID_SOCKET )
		BG_CloseSocket( c->sock );
	WA_BufFree( &c->response );
	memset( c, 0, sizeof( *c ) );
	c->sock = BG_INVALID_SOCKET;
}

// Handles a complete request. The response goes into c->response.
// A file of the folder vs_webRoot names, added to a buffer. qfalse: it can not be read.
static qboolean WA_PageFile( waBuffer_t *b, const char *name ) {
	FILE	*f = fopen( va( "%s/%s", vs_webRoot.string, name ), "rb" );
	char	chunk[8192];
	size_t	n;

	if ( !f )
		return qfalse;
	while ( ( n = fread( chunk, 1, sizeof( chunk ), f ) ) > 0 )
		WA_BufAppend( b, chunk, (int)n );
	fclose( f );
	return qtrue;
}

// A page as the build puts it together (cmake/Modules/BundlePage.cmake), from the files of the folder
// vs_webRoot names: the template (index.html, live.js), and for every line "<!--#include name -->" in it the file
// of that name in its place. For working on the page: a change to one of its files is there with the next load.
qboolean WA_PageBundle( waBuffer_t *page, const char *name ) {
	static const char	mark[] = "<!--#include ";
	waBuffer_t			text = { 0 };
	const char			*at, *include;
	qboolean			whole = qtrue;

	if ( !WA_PageFile( &text, name ) || !text.len ) {
		WA_BufFree( &text );
		return qfalse;
	}

	for ( at = text.data; ( include = strstr( at, mark ) ) != NULL; ) {
		const char	*part = include + sizeof( mark ) - 1, *end = strstr( part, " -->" ), *line;
		char		file[MAX_QPATH];

		if ( !end || end - part >= (int)sizeof( file ) )
			break;
		WA_BufAppend( page, at, (int)( include - at ) );
		Q_strncpyz( file, part, (int)( end - part ) + 1 );
		// only what is in the folder
		if ( !WA_GamePathOk( file ) || !WA_PageFile( page, file ) ) {
			trap->Print( S_COLOR_YELLOW "Web admin: %s/%s includes %s, which can not be read\n", vs_webRoot.string, name, file );
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

/*
=================
WA_IsProxy

vs_webProxy names the reverse proxies in front of the panel, IPv4 addresses separated by blanks or commas: one in
front of the whole panel, and another one on the machine itself for the live view, for example. qtrue: addr is
one of them. *valid and *bad: how many words are addresses and how many are not (may be NULL).
=================
*/
static qboolean WA_IsProxy( unsigned int addr, int *valid, int *bad ) {
	const char		*s = vs_webProxy.string;
	char			word[32];
	struct in_addr	proxy;
	qboolean		found = qfalse;
	int				n, good = 0, wrong = 0;

	while ( *s ) {
		while ( *s == ' ' || *s == ',' )
			s++;
		for ( n = 0; *s && *s != ' ' && *s != ','; s++ ) {
			if ( n < (int)sizeof( word ) - 1 )
				word[n++] = *s;
		}
		word[n] = '\0';
		if ( !n )
			continue;
		if ( inet_pton( AF_INET, word, &proxy ) == 1 ) {
			good++;
			if ( (unsigned int)proxy.s_addr == addr )
				found = qtrue;
		} else {
			wrong++;
		}
	}
	if ( valid )
		*valid = good;
	if ( bad )
		*bad = wrong;
	return found;
}

/*
=================
WA_Visitor

Who asks. Behind a reverse proxy (vs_webProxy) every connection is the proxy's, and the proxy says in a header
whom it serves: X-Real-IP, which nginx writes itself, or the last entry of X-Forwarded-For, which is the proxy's
too (what stands in front of it came from the visitor and can be anything). That address gets the wait after
wrong logins and stands in the log. An address that is not IPv4 gets a number made from its text.
=================
*/
static void WA_Visitor( waConnection_t *c, const char *head ) {
	char			value[256];
	const char		*text = value, *comma;
	struct in_addr	address;

	c->peer = c->addr;
	c->proxied = c->https = qfalse;
	if ( !vs_webProxy.string[0] ) {
		return;
	}
	c->proxied = qtrue;
	if ( Web_HeaderValue( head, "X-Forwarded-Proto", value, sizeof( value ) ) && !Q_stricmp( value, "https" ) ) {
		c->https = qtrue;
	}
	if ( !Web_HeaderValue( head, "X-Real-IP", value, sizeof( value ) ) ) {
		if ( !Web_HeaderValue( head, "X-Forwarded-For", value, sizeof( value ) ) ) {
			return;
		}
		comma = strrchr( value, ',' );
		if ( comma ) {
			text = comma + 1;
		}
	}
	while ( *text == ' ' ) {
		text++;
	}
	if ( inet_pton( AF_INET, text, &address ) == 1 ) {
		c->peer = (unsigned int)address.s_addr;
	} else if ( text[0] ) {
		unsigned int hash = 2166136261u;

		for ( ; *text; text++ ) {
			hash = ( hash ^ (unsigned char)tolower( (unsigned char)*text ) ) * 16777619u;
		}
		c->peer = hash | 0xf0u;		// its first byte as it is printed is above 239: no address of a machine
	}
}

/*
=================
WA_RouteRole

What a request needs at least. Everybody who is logged in may look at the game. Who may change what, and what
is nobody's business but the owner's, is said here and for the actions on players in WA_ApiAction().
=================
*/
waRole_t WA_RouteRole( const char *method, const char *path ) {
	// the console runs anything, the config and the files hold the passwords of the server
	if ( !strcmp( path, "/api/command" ) || !strcmp( path, "/api/config" ) || !strncmp( path, "/api/fs/", 8 ) ) {
		return WA_ROLE_OWNER;
	}
	// what the console and the log print: addresses, and what the admins did
	if ( !strcmp( path, "/api/console" ) || !strcmp( path, "/api/log" ) || !strcmp( path, "/api/ids" ) ) {
		return WA_ROLE_ADMIN;
	}
	// every other change but the actions: the editor, the clips, the textures, the routes, a controlled player
	if ( !strcmp( method, "POST" ) && strcmp( path, "/api/action" ) ) {
		return WA_ROLE_ADMIN;
	}
	return WA_ROLE_MODERATOR;
}

// A page: from disk where vs_webRoot names a folder and it has the file (for working on the page without a
// restart), else the one built into the module.
/*
=================
WA_ScriptPolicy

The headers of a page of the panel: its scripts are written into it (<script> ... </script>, put together from
the files of page/js when the module is built), and only those may run. A name or a line that slipped into the
page past escapeHtml() would be no script then. The hashes are made of what is sent, so a page from vs_webRoot
gets its own.
=================
*/
static void WA_ScriptPolicy( const char *page, int len, char *out, int outSize ) {
	static const char	open[] = "<script>", close[] = "</script>";
	const int			openLen = (int)strlen( open ), closeLen = (int)strlen( close );
	unsigned char		digest[WEB_SHA256_SIZE];
	char				hash[64];
	int					at = 0, start, end;

	Q_strncpyz( out, "Cache-Control: no-store\r\nContent-Security-Policy: object-src 'none'; base-uri 'none'; script-src", outSize );
	while ( at <= len - openLen ) {
		if ( memcmp( page + at, open, openLen ) ) {
			at++;
			continue;
		}
		start = at + openLen;
		for ( end = start; end <= len - closeLen && memcmp( page + end, close, closeLen ); end++ )
			;
		if ( end > len - closeLen )
			break;
		Web_SHA256( page + start, (size_t)( end - start ), digest );
		if ( Web_Base64Encode( digest, sizeof( digest ), hash, sizeof( hash ) ) > 0 )
			Q_strcat( out, outSize, va( " 'sha256-%s'", hash ) );
		at = end + closeLen;
	}
	Q_strcat( out, outSize, "\r\n" );
}

static void WA_Page( waConnection_t *c, const char *file, const char *embedded, int embeddedLen ) {
	waBuffer_t	page = { 0 };
	char		headers[512];

	if ( vs_webRoot.string[0] && WA_PageFile( &page, file ) && page.len ) {
		WA_ScriptPolicy( page.data, page.len, headers, sizeof( headers ) );
		WA_Respond( c, 200, "OK", "text/html; charset=utf-8", headers, page.data, page.len );
	} else {
		WA_ScriptPolicy( embedded, embeddedLen, headers, sizeof( headers ) );
		WA_Respond( c, 200, "OK", "text/html; charset=utf-8", headers, embedded, embeddedLen );
	}
	WA_BufFree( &page );
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

	WA_Visitor( c, head );
	query = strchr( path, '?' );
	if ( query )
		*query++ = '\0';
	else
		query = (char *)"";

	// The live view asks without a login, and nothing there needs one (wa_live.c). It comes before the wait after
	// wrong logins: behind a proxy that does not say who the visitor is, every visitor has one address, and five
	// wrong tokens of anybody would close the live view for everybody.
	if ( WA_LiveRequest( c, method, path, query, head ) ) {
		return;
	}
	if ( WA_AuthThrottle( c->peer ) ) {
		WA_Respond( c, 429, "Too Many Requests", "text/plain", "Retry-After: 30\r\n", "too many wrong logins, wait", 27 );
		return;
	}
	// before anything else, the login included: another site must not log a visitor in either
	if ( !strcmp( method, "POST" ) && !Web_SameOrigin( head ) ) {
		WA_RespondText( c, 403, "Forbidden", "request from another site refused" );
		return;
	}
	WA_AuthRequest( c, head );
	if ( WA_AuthRoute( c, method, path, head, body ) ) {
		return;
	}
	if ( c->role == WA_ROLE_NONE ) {
		// the page of the login for a browser that asks for a page, a plain no for everything else
		if ( !strcmp( method, "GET" ) && strncmp( path, "/api/", 5 ) ) {
			WA_Page( c, "login.html", (const char *)webadmin_login, (int)webadmin_login_len );
		} else {
			WA_AuthAnswer( c, 401, "Unauthorized", "login" );
		}
		return;
	}
	if ( c->role < WA_RouteRole( method, path ) ) {
		WA_AuthAnswer( c, 403, "Forbidden", "role" );
		return;
	}
	// the socket of the page: the state and what it asks for besides it, pushed (wa_stream.c)
	if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/stream" ) ) {
		WA_StreamUpgrade( c, head, qfalse );
		return;
	}
	WA_Route( c, method, path, query, body, bodyLen );
}

/*
=================
WA_Route

A request of somebody who may make it, to what answers it. The WebSocket of a page asks the same way for what it
has subscribed to (wa_stream.c).
=================
*/
void WA_Route( waConnection_t *c, const char *method, const char *path, const char *query, char *body, int bodyLen ) {
	if ( !strcmp( method, "GET" ) && ( !strcmp( path, "/" ) || !strcmp( path, "/index.html" ) ) ) {
		// vs_webRoot serves the page from disk instead, for working on it without a restart
		waBuffer_t page = { 0 };

		if ( vs_webRoot.string[0] && WA_PageBundle( &page, "index.html" ) ) {
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
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/classkeys" ) ) {
		WA_ApiClassKeys( c );
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
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/console" ) ) {
		WA_ApiConsole( c, query );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/ids" ) ) {
		WA_ApiIds( c, query );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/command" ) ) {
		WA_ApiCommand( c, body );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/clips" ) ) {
		WA_ApiClips( c );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/clip" ) ) {
		WA_ApiClip( c, body );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/remaps" ) ) {
		WA_ApiRemaps( c );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/remap" ) ) {
		WA_ApiRemap( c, body );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/control" ) ) {
		WA_ApiControl( c, body );
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
	lenHeader = Web_FindHeader( c->request, "Content-Length" );
	if ( lenHeader )
		bodyLen = Web_ParseLength( lenHeader, WA_MAX_REQUEST );
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
	if ( c->stream ) {
		// a socket stays: what was sent goes, the rest waits for the next frame
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
		c->response.len = c->sent = 0;
		if ( c->closing )
			WA_CloseConnection( c );
		return;
	}
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

// the WebSockets of the live view that are open
int WA_LiveStreams( void ) {
	int i, n = 0;

	for ( i = 0; i < WA_MAX_CONNECTIONS; i++ ) {
		if ( connections[i].inUse && connections[i].stream && connections[i].live )
			n++;
	}
	return n;
}

static void WA_Accept( void ) {
	for ( ;; ) {
		struct sockaddr_in	from;
		socklen_t			fromLen = sizeof( from );
		bgSocket_t			s = accept( listenSocket, (struct sockaddr *)&from, &fromLen );
		int					i;

		if ( s == BG_INVALID_SOCKET )
			return;

		// With vs_webProxy only the proxies are let in: they alone say who the visitor is (WA_Visitor), and anybody
		// else who reached this port could say the same.
		if ( vs_webProxy.string[0] && !WA_IsProxy( (unsigned int)from.sin_addr.s_addr, NULL, NULL ) ) {
			BG_CloseSocket( s );
			continue;
		}

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

// Opens the port. With the first account, which may be made while a map runs ("webuser add" on the console).
static void WA_Listen( void ) {
	struct sockaddr_in	addr;
	int					on = 1;
	const char			*bindTo = "0.0.0.0";

	if ( WA_Running() || vs_webPort.integer <= 0 || vs_webPort.integer > 65535 )
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
	addr.sin_port = htons( (unsigned short)vs_webPort.integer );
	// Every interface unless vs_webBind names one address, an empty one included (a config that was written while
	// empty was the default has it that way). The security pass had made that 127.0.0.1; it is back so that the
	// panel is reached from the network without a setting. It is plain HTTP: where the network is not trusted,
	// vs_webBind 127.0.0.1 and a tunnel or a reverse proxy. A value that is no address does not open anything.
	if ( !vs_webBind.string[0] ) {
		inet_pton( AF_INET, bindTo, &addr.sin_addr );
	} else if ( inet_pton( AF_INET, vs_webBind.string, &addr.sin_addr ) == 1 ) {
		bindTo = vs_webBind.string;
	} else {
		bindTo = "127.0.0.1";
		inet_pton( AF_INET, bindTo, &addr.sin_addr );
		trap->Print( S_COLOR_YELLOW "Web admin: vs_webBind \"%s\" is no IPv4 address, taking %s\n", vs_webBind.string, bindTo );
	}

	if ( bind( listenSocket, (struct sockaddr *)&addr, sizeof( addr ) ) != 0 || listen( listenSocket, 16 ) != 0 ) {
		trap->Print( S_COLOR_YELLOW "Web admin: could not listen on %s:%d (port in use, or below 1024 without root?)\n",
			bindTo, vs_webPort.integer );
		BG_CloseSocket( listenSocket );
		listenSocket = BG_INVALID_SOCKET;
		return;
	}

	BG_SocketNonBlocking( listenSocket );
	WA_ChatInit();
	listenPort = vs_webPort.integer;
	trap->Print( "Web admin listening on http://%s:%d/\n", bindTo, listenPort );
	if ( vs_webProxy.string[0] ) {
		int valid, bad;

		WA_IsProxy( 0, &valid, &bad );
		if ( bad ) {
			trap->Print( S_COLOR_YELLOW "Web admin: %d words of vs_webProxy \"%s\" are no IPv4 address, they let nobody in\n", bad, vs_webProxy.string );
		}
		if ( valid ) {
			trap->Print( "Web admin: only the reverse proxies %s are let in (vs_webProxy)\n", vs_webProxy.string );
		} else {
			trap->Print( S_COLOR_YELLOW "Web admin: vs_webProxy names no proxy, so nobody is let in\n" );
		}
	}
}

void WebAdmin_Init( void ) {
	int i;

	for ( i = 0; i < WA_MAX_CONNECTIONS; i++ )
		connections[i].sock = BG_INVALID_SOCKET;

	// before anything can keep the panel off: the file of a reload must not wait for a later start of this map
	WA_ReloadRead();

	WA_AuthInit();
	if ( !WA_AuthHasUsers() ) {
		if ( vs_webPort.integer > 0 )
			trap->Print( "Web admin is off: it has no account. \"webuser add <name> owner\" on this console makes the first one\n" );
		// the live view needs no account (wa_live.c), and nobody gets past the login without one
		if ( !vs_liveView.integer )
			return;
	}
	WA_Listen();
	if ( vs_liveView.integer && WA_Running() )
		trap->Print( "Web admin: the live view is open to everybody at /live/ (vs_liveView)\n" );
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

	WA_RemapsFrame();

	WA_Accept();

	// what is read of files in this frame. Another connection is the first each frame, so none waits for long
	// behind the ones in front of it.
	fileBudget = WA_FILE_FRAME;
	firstConnection = ( firstConnection + 1 ) % WA_MAX_CONNECTIONS;

	// what the sockets are pushed in this frame is put together once for all that have the same (wa_stream.c)
	WA_StreamTick( now );

	for ( i = 0; i < WA_MAX_CONNECTIONS; i++ ) {
		waConnection_t *c = &connections[( firstConnection + i ) % WA_MAX_CONNECTIONS];

		if ( !c->inUse )
			continue;

		if ( now - c->lastActive > WA_TIMEOUT_MS ) {
			WA_CloseConnection( c );
			continue;
		}

		if ( c->stream ) {
			WA_StreamRead( c );
			if ( c->inUse )
				WA_StreamFrame( c, now );
			// (a socket that is closing and has nothing left to send ends there)
			if ( c->inUse && ( c->response.len || c->closing ) )
				WA_WriteResponse( c );
			continue;
		}
		// a login whose password is hashed beside the game: answered when the hash is there
		if ( c->loginWait ) {
			c->lastActive = now;
			WA_AuthLoginFrame( c );
			if ( c->inUse && c->response.len )
				WA_WriteResponse( c );
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

	WA_AuthShutdown();
	WA_EntitiesShutdown();
	WA_AssetsShutdown();
}

// "webuser" on the console of the server. The first account starts the panel.
qboolean WebAdmin_ConsoleCommand( const char *cmd ) {
	if ( !WA_AuthConsoleCommand( cmd ) )
		return qfalse;
	if ( WA_AuthHasUsers() )
		WA_Listen();
	return qtrue;
}

