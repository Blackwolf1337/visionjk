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


// sys_http_win32.cpp -- Sys_HttpGet() and Sys_HttpWebSocket() with WinHTTP, which is part of Windows, see
// sys_http.h.
// WinHTTP checks the certificate of a server against what the system trusts unless it is told not to, and
// nothing here tells it.

#include <windows.h>
#include <winhttp.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "sys_http.h"

#define HTTP_CONNECT_MSEC	15000
#define HTTP_SILENT_MSEC	30000		// a line that brings nothing for this long has failed
#define HTTP_MAX_URL		512

// what WinHTTP says went wrong, as far as a player can do something with it
static void Sys_HttpError( const char *step, char *error, int errorSize ) {
	const DWORD code = GetLastError();

	switch ( code ) {
	case ERROR_WINHTTP_NAME_NOT_RESOLVED:
		Q_strncpyz( error, "the name of the server could not be looked up", errorSize );
		break;
	case ERROR_WINHTTP_CANNOT_CONNECT:
	case ERROR_WINHTTP_CONNECTION_ERROR:
		Q_strncpyz( error, "no connection to the server", errorSize );
		break;
	case ERROR_WINHTTP_TIMEOUT:
		Q_strncpyz( error, "the server did not answer in time", errorSize );
		break;
	case ERROR_WINHTTP_SECURE_FAILURE:
		Q_strncpyz( error, "the certificate of the server is not one this system trusts", errorSize );
		break;
	default:
		Com_sprintf( error, errorSize, "%s failed (WinHTTP %lu)", step, (unsigned long)code );
		break;
	}
}

typedef struct httpHandles_s {
	HINTERNET	session, connection, request;
} httpHandles_t;

static void Sys_HttpClose( httpHandles_t *h ) {
	if ( h->request )
		WinHttpCloseHandle( h->request );
	if ( h->connection )
		WinHttpCloseHandle( h->connection );
	if ( h->session )
		WinHttpCloseHandle( h->session );
	memset( h, 0, sizeof( *h ) );
}

// sends the request for url (http:// or https://), with socket as one that asks to become a WebSocket, and reads
// the head of the answer. qfalse: no answer, error says why.
static qboolean Sys_HttpRequest( const char *url, qboolean socket, httpHandles_t *h, DWORD *answer, char *error, int errorSize ) {
	wchar_t			wide[HTTP_MAX_URL], host[256], path[HTTP_MAX_URL];
	URL_COMPONENTS	parts;
	DWORD			size = sizeof( *answer ), off = WINHTTP_DISABLE_REDIRECTS;
	size_t			i;

	memset( h, 0, sizeof( *h ) );
	*answer = 0;
	// an address is plain letters here (UPD_AddressAllowed, UPD_PathAllowed, CL_TV_f), so each is one wide one
	for ( i = 0; url[i] && i < HTTP_MAX_URL - 1; i++ )
		wide[i] = (wchar_t)(unsigned char)url[i];
	wide[i] = L'\0';
	if ( url[i] ) {
		Q_strncpyz( error, "the address is too long", errorSize );
		return qfalse;
	}

	memset( &parts, 0, sizeof( parts ) );
	parts.dwStructSize = sizeof( parts );
	parts.lpszHostName = host;
	parts.dwHostNameLength = ARRAY_LEN( host );
	parts.lpszUrlPath = path;
	parts.dwUrlPathLength = ARRAY_LEN( path );
	if ( !WinHttpCrackUrl( wide, 0, 0, &parts ) || ( parts.nScheme != INTERNET_SCHEME_HTTPS && parts.nScheme != INTERNET_SCHEME_HTTP ) ) {
		Q_strncpyz( error, "the address is not one", errorSize );
		return qfalse;
	}

	h->session = WinHttpOpen( L"VisionJK", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
	if ( !h->session ) {
		Sys_HttpError( "WinHttpOpen", error, errorSize );
		return qfalse;
	}
	WinHttpSetTimeouts( h->session, HTTP_CONNECT_MSEC, HTTP_CONNECT_MSEC, HTTP_SILENT_MSEC, HTTP_SILENT_MSEC );
	h->connection = WinHttpConnect( h->session, host, parts.nPort, 0 );
	if ( !h->connection ) {
		Sys_HttpError( "WinHttpConnect", error, errorSize );
		return qfalse;
	}
	h->request = WinHttpOpenRequest( h->connection, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
		parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0 );
	if ( !h->request ) {
		Sys_HttpError( "WinHttpOpenRequest", error, errorSize );
		return qfalse;
	}
	// a redirect is not followed: its answer is not the one asked for and so counts as failed
	WinHttpSetOption( h->request, WINHTTP_OPTION_DISABLE_FEATURE, &off, sizeof( off ) );
	if ( socket && !WinHttpSetOption( h->request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0 ) ) {
		Sys_HttpError( "the WebSocket", error, errorSize );
		return qfalse;
	}
	if ( !WinHttpSendRequest( h->request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0 )
		|| !WinHttpReceiveResponse( h->request, NULL ) ) {
		Sys_HttpError( "the request", error, errorSize );
		return qfalse;
	}
	if ( !WinHttpQueryHeaders( h->request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
		answer, &size, WINHTTP_NO_HEADER_INDEX ) ) {
		Sys_HttpError( "the answer", error, errorSize );
		return qfalse;
	}
	return qtrue;
}

qboolean Sys_HttpGet( const char *url, sysHttpSink_t sink, void *ctx, const volatile int *cancel, char *error, int errorSize, int *status ) {
	httpHandles_t	h;
	DWORD			answer;
	qboolean		ok = qfalse;

	error[0] = '\0';
	if ( status ) {
		*status = 0;
	}
	if ( !Sys_HttpRequest( url, qfalse, &h, &answer, error, errorSize ) ) {
		Sys_HttpClose( &h );
		return qfalse;
	}
	if ( status ) {
		*status = (int)answer;
	}
	if ( answer != 200 ) {
		Com_sprintf( error, errorSize, "the server answered %lu", (unsigned long)answer );
		Sys_HttpClose( &h );
		return qfalse;
	}
	for ( ;; ) {
		static char	buffer[65536];		// (one request at a time in the whole client: cl_update.cpp, cl_download.cpp)
		DWORD		got = 0;

		if ( *cancel ) {
			Q_strncpyz( error, "stopped", errorSize );
			break;
		}
		if ( !WinHttpReadData( h.request, buffer, sizeof( buffer ), &got ) ) {
			Sys_HttpError( "reading", error, errorSize );
			break;
		}
		if ( !got ) {
			ok = qtrue;
			break;
		}
		if ( !sink( ctx, buffer, got ) ) {
			Q_strncpyz( error, "it was ended on this side", errorSize );
			break;
		}
	}
	Sys_HttpClose( &h );
	return ok;
}

// The WebSocket calls of WinHTTP came with Windows 8. They are looked up when the TV asks, so that the game still
// starts on a system without them.
typedef HINTERNET ( WINAPI *httpWsUpgrade_t )( HINTERNET, DWORD_PTR );
typedef DWORD ( WINAPI *httpWsReceive_t )( HINTERNET, void *, DWORD, DWORD *, WINHTTP_WEB_SOCKET_BUFFER_TYPE * );

qboolean Sys_HttpWebSocket( const char *url, sysHttpSink_t sink, void *ctx, const volatile int *cancel, char *error, int errorSize, int *status ) {
	static char		buffer[65536];		// (one TV at a time)
	const HMODULE	module = GetModuleHandleA( "winhttp.dll" );
	httpWsUpgrade_t	upgrade = module ? (httpWsUpgrade_t)(void *)GetProcAddress( module, "WinHttpWebSocketCompleteUpgrade" ) : NULL;
	httpWsReceive_t	receive = module ? (httpWsReceive_t)(void *)GetProcAddress( module, "WinHttpWebSocketReceive" ) : NULL;
	char			address[HTTP_MAX_URL];
	httpHandles_t	h;
	HINTERNET		socket;
	DWORD			answer;

	error[0] = '\0';
	if ( status ) {
		*status = 0;
	}
	if ( !upgrade || !receive ) {
		Q_strncpyz( error, "WinHTTP of this Windows has no WebSocket (Windows 8 or newer)", errorSize );
		return qfalse;
	}
	// (WinHttpCrackUrl knows http and https only: the request that becomes a WebSocket is one of those)
	if ( !Q_stricmpn( url, "wss://", 6 ) ) {
		Com_sprintf( address, sizeof( address ), "https://%s", url + 6 );
	} else if ( !Q_stricmpn( url, "ws://", 5 ) ) {
		Com_sprintf( address, sizeof( address ), "http://%s", url + 5 );
	} else {
		Q_strncpyz( error, "the address is not one", errorSize );
		return qfalse;
	}
	if ( !Sys_HttpRequest( address, qtrue, &h, &answer, error, errorSize ) ) {
		Sys_HttpClose( &h );
		return qfalse;
	}
	if ( answer != 101 ) {
		if ( status ) {
			*status = (int)answer;
		}
		Com_sprintf( error, errorSize, "the server answered %lu", (unsigned long)answer );
		Sys_HttpClose( &h );
		return qfalse;
	}
	socket = upgrade( h.request, 0 );
	if ( !socket ) {
		Sys_HttpError( "the WebSocket", error, errorSize );
		Sys_HttpClose( &h );
		return qfalse;
	}
	WinHttpCloseHandle( h.request );
	h.request = socket;

	for ( ;; ) {
		WINHTTP_WEB_SOCKET_BUFFER_TYPE	type;
		DWORD							got = 0, code;

		if ( *cancel ) {
			Q_strncpyz( error, "stopped", errorSize );
			break;
		}
		code = receive( socket, buffer, sizeof( buffer ), &got, &type );
		if ( code != NO_ERROR ) {
			SetLastError( code );
			Sys_HttpError( "reading", error, errorSize );
			break;
		}
		if ( type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE ) {
			Q_strncpyz( error, "the server ended the stream", errorSize );
			break;
		}
		if ( ( type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE )
			&& got && !sink( ctx, buffer, got ) ) {
			Q_strncpyz( error, "it was ended on this side", errorSize );
			break;
		}
	}
	Sys_HttpClose( &h );
	return qfalse;
}
