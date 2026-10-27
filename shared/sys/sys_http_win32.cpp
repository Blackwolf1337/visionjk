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


// sys_http_win32.cpp -- Sys_HttpGet() with WinHTTP, which is part of Windows, see sys_http.h.
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

qboolean Sys_HttpGet( const char *url, sysHttpSink_t sink, void *ctx, const volatile int *cancel, char *error, int errorSize, int *status ) {
	wchar_t			wide[HTTP_MAX_URL], host[256], path[HTTP_MAX_URL];
	URL_COMPONENTS	parts;
	HINTERNET		session = NULL, connection = NULL, request = NULL;
	DWORD			answer = 0, size = sizeof( answer ), off = WINHTTP_DISABLE_REDIRECTS;
	qboolean		ok = qfalse;
	size_t			i;

	error[0] = '\0';
	if ( status ) {
		*status = 0;
	}
	// an address is plain letters here (UPD_AddressAllowed, UPD_PathAllowed), so each is one wide one
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

	do {
		session = WinHttpOpen( L"VisionJK", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
		if ( !session ) {
			Sys_HttpError( "WinHttpOpen", error, errorSize );
			break;
		}
		WinHttpSetTimeouts( session, HTTP_CONNECT_MSEC, HTTP_CONNECT_MSEC, HTTP_SILENT_MSEC, HTTP_SILENT_MSEC );
		connection = WinHttpConnect( session, host, parts.nPort, 0 );
		if ( !connection ) {
			Sys_HttpError( "WinHttpConnect", error, errorSize );
			break;
		}
		request = WinHttpOpenRequest( connection, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
			parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0 );
		if ( !request ) {
			Sys_HttpError( "WinHttpOpenRequest", error, errorSize );
			break;
		}
		// a redirect is not followed: its answer is not 200 and so counts as failed
		WinHttpSetOption( request, WINHTTP_OPTION_DISABLE_FEATURE, &off, sizeof( off ) );
		if ( !WinHttpSendRequest( request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0 )
			|| !WinHttpReceiveResponse( request, NULL ) ) {
			Sys_HttpError( "the request", error, errorSize );
			break;
		}
		if ( !WinHttpQueryHeaders( request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
			&answer, &size, WINHTTP_NO_HEADER_INDEX ) ) {
			Sys_HttpError( "the answer", error, errorSize );
			break;
		}
		if ( status ) {
			*status = (int)answer;
		}
		if ( answer != 200 ) {
			Com_sprintf( error, errorSize, "the server answered %lu", (unsigned long)answer );
			break;
		}
		for ( ;; ) {
			static char	buffer[65536];		// (one request at a time in the whole client: cl_update.cpp, cl_download.cpp)
			DWORD		got = 0;

			if ( *cancel ) {
				Q_strncpyz( error, "stopped", errorSize );
				break;
			}
			if ( !WinHttpReadData( request, buffer, sizeof( buffer ), &got ) ) {
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
	} while ( 0 );

	if ( request )
		WinHttpCloseHandle( request );
	if ( connection )
		WinHttpCloseHandle( connection );
	if ( session )
		WinHttpCloseHandle( session );
	return ok;
}
