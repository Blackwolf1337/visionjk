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

// web_http.c -- the head of an HTTP request: its headers, a length in it, and where the request comes from.
// The head is the request line and the header lines as they came, ended by a zero in place of the empty line.

#include "web_http.h"

#include <string.h>

#include "qcommon/q_string.h"

/*
=================
Web_FindHeader

Finds a header value in the request head, case insensitive name. Returns a
pointer to the start of the value (ends at \r) or NULL.
=================
*/
const char *Web_FindHeader( const char *head, const char *name ) {
	const size_t nameLen = strlen( name );
	const char *line = strstr( head, "\r\n" );

	while ( line && line[2] != '\r' ) {
		line += 2;
		if ( !Q_stricmpn( line, name, (int)nameLen ) && line[nameLen] == ':' ) {
			const char *v = line + nameLen + 1;
			while ( *v == ' ' )
				v++;
			return v;
		}
		line = strstr( line, "\r\n" );
	}
	return NULL;
}

/*
=================
Web_HeaderValue

Copies the value of a header up to the end of its line, without the spaces at its end. qfalse: not there.
=================
*/
qboolean Web_HeaderValue( const char *head, const char *name, char *out, int outSize ) {
	const char	*v = Web_FindHeader( head, name );
	int			len = 0;

	out[0] = '\0';
	if ( !v )
		return qfalse;
	while ( v[len] && v[len] != '\r' && v[len] != '\n' && len < outSize - 1 )
		len++;
	while ( len > 0 && ( v[len - 1] == ' ' || v[len - 1] == '\t' ) )
		len--;
	memcpy( out, v, len );
	out[len] = '\0';
	return qtrue;
}

/*
=================
Web_ParseLength

A Content-Length: only digits, and nothing above max. JKAdmin read it with atoi and then checked
headLen + bodyLen, which wraps for a length near INT_MAX: the check passed and the terminating zero of the
body was written about 2 GB in front of the request buffer, before any login. Returns -1 for a bad value.
=================
*/
int Web_ParseLength( const char *s, int max ) {
	int len = 0, digits = 0;

	for ( ; *s >= '0' && *s <= '9'; s++, digits++ ) {
		const int digit = *s - '0';

		// (asked before the digit is added, so len does not wrap whatever max is)
		if ( digit > max || len > ( max - digit ) / 10 )
			return -1;
		len = len * 10 + digit;
	}
	while ( *s == ' ' || *s == '\t' )
		s++;
	if ( !digits || ( *s && *s != '\r' && *s != '\n' ) )
		return -1;
	return len;
}

/*
=================
Web_SameOrigin

Not in JKAdmin. A request that changes something, or that opens a WebSocket, has to come from the page itself.
The browser sends what it holds for a server (the login of the panel) along with every request to it, also with
one that another web site makes it send (CSRF). That site would get at the player actions and at the entity
editor without knowing the password, or play through the address of whoever looks at it.

Browsers say where such a request comes from: Sec-Fetch-Site, Origin, Referer. A page can not leave out or forge
the first two. A request with none of the three is not from a browser (curl, scripts) and goes through.
The address in Origin has to be the one the request was sent to: Host, or X-Forwarded-Host behind a reverse
proxy that sends another Host to the server (a page can not set that header on a request to another site).
=================
*/
qboolean Web_SameOrigin( const char *head ) {
	static const char *const sentTo[] = { "Host", "X-Forwarded-Host" };
	char		from[512], host[256];
	const char	*address;
	size_t		i;

	if ( Web_HeaderValue( head, "Sec-Fetch-Site", from, sizeof( from ) ) && Q_stricmp( from, "same-origin" ) && Q_stricmp( from, "none" ) )
		return qfalse;

	if ( !Web_HeaderValue( head, "Origin", from, sizeof( from ) ) && !Web_HeaderValue( head, "Referer", from, sizeof( from ) ) )
		return qtrue;

	// scheme://host[:port][/path]. "null" (a sandboxed frame, a data: page) is no address.
	address = strstr( from, "://" );
	if ( !address )
		return qfalse;
	address += 3;

	for ( i = 0; i < sizeof( sentTo ) / sizeof( sentTo[0] ); i++ ) {
		if ( Web_HeaderValue( head, sentTo[i], host, sizeof( host ) ) && host[0] ) {
			const size_t len = strlen( host );

			if ( !Q_stricmpn( address, host, (int)len ) && ( address[len] == '\0' || address[len] == '/' ) )
				return qtrue;
		}
	}
	return qfalse;
}
