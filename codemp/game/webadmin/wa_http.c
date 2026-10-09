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

// wa_http.c -- what every answer of the web admin panel is made with: a buffer that grows, the head
// of an answer, and what a request is made of (its headers, the login and its throttle, the values of a form,
// text between UTF-8 and the one byte a character of the game), and a file of the game read or written whole.

#include "game/webadmin/wa_local.h"

// login throttle: after WA_AUTH_TRIES wrong passwords an address waits, the wait doubles from WA_AUTH_BASE_MS
#define WA_AUTH_TRIES		5
#define WA_AUTH_BASE_MS		30000
#define WA_AUTH_MAX_MS		1800000
#define WA_AUTH_SLOTS		64

typedef struct waAuthFail_s {
	unsigned int	addr;
	int				fails;
	int				until;		// blocked while trap->Milliseconds() < until
	int				seen;		// last time this slot was touched, for reuse
} waAuthFail_t;

static waAuthFail_t	authFails[WA_AUTH_SLOTS];

/*
==============================================================================

Buffers

==============================================================================
*/

// grows the buffer to hold extra more bytes and a zero. qfalse if it is out of memory or the size would wrap;
// the buffer is left as it was, so a caller that checks the result leaves a short response instead of crashing.
qboolean WA_BufReserve( waBuffer_t *b, int extra ) {
	int		newSize;
	char	*grown;

	if ( extra < 0 || b->len > WA_MAX_FILE || extra > WA_MAX_FILE )
		return qfalse;
	if ( b->len + extra + 1 <= b->size )
		return qtrue;

	newSize = b->size ? b->size : 4096;
	while ( newSize < b->len + extra + 1 )
		newSize *= 2;
	grown = (char *)realloc( b->data, newSize );
	if ( !grown )
		return qfalse;
	b->data = grown;
	b->size = newSize;
	return qtrue;
}

void WA_BufAppend( waBuffer_t *b, const void *data, int len ) {
	if ( len <= 0 || !WA_BufReserve( b, len ) )
		return;
	memcpy( b->data + b->len, data, len );
	b->len += len;
	b->data[b->len] = '\0';
}

void WA_BufPrintf( waBuffer_t *b, const char *fmt, ... ) {
	va_list ap;
	char	small[1024];
	int		len;

	va_start( ap, fmt );
	len = Q_vsnprintf( small, sizeof( small ), fmt, ap );
	va_end( ap );

	if ( len < 0 )
		return;
	if ( len < (int)sizeof( small ) ) {
		WA_BufAppend( b, small, len );
		return;
	}

	if ( !WA_BufReserve( b, len ) )
		return;
	va_start( ap, fmt );
	Q_vsnprintf( b->data + b->len, len + 1, fmt, ap );
	va_end( ap );
	b->len += len;
}

void WA_BufFree( waBuffer_t *b ) {
	free( b->data );
	memset( b, 0, sizeof( *b ) );
}

// JSON string, color codes stay in, the page renders them
void WA_BufJsonString( waBuffer_t *b, const char *s ) {
	WA_BufAppend( b, "\"", 1 );
	for ( ; *s; s++ ) {
		const unsigned char c = (unsigned char)*s;
		if ( c == '"' || c == '\\' ) {
			char esc[2] = { '\\', (char)c };
			WA_BufAppend( b, esc, 2 );
		} else if ( c < 0x20 || c >= 0x7f ) {
			// names are not UTF-8, keep them ASCII safe
			WA_BufPrintf( b, "\\u%04x", c );
		} else {
			WA_BufAppend( b, s, 1 );
		}
	}
	WA_BufAppend( b, "\"", 1 );
}

/*
==============================================================================

Responses

==============================================================================
*/

// the head of an answer whose body has bodyLen bytes
void WA_RespondHead( waConnection_t *c, int status, const char *statusText, const char *contentType,
	const char *extraHeaders, int bodyLen ) {
	WA_BufPrintf( &c->response,
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n"
		"X-Content-Type-Options: nosniff\r\n"
		// nothing of the panel inside a frame or as a resource of another site
		"X-Frame-Options: DENY\r\n"
		"Content-Security-Policy: frame-ancestors 'none'\r\n"
		"Cross-Origin-Resource-Policy: same-origin\r\n"
		"%s"
		"\r\n",
		status, statusText, contentType, bodyLen, extraHeaders ? extraHeaders : "" );
}

void WA_Respond( waConnection_t *c, int status, const char *statusText, const char *contentType,
	const char *extraHeaders, const void *body, int bodyLen ) {
	WA_RespondHead( c, status, statusText, contentType, extraHeaders, bodyLen );
	if ( bodyLen > 0 )
		WA_BufAppend( &c->response, body, bodyLen );
}

void WA_RespondText( waConnection_t *c, int status, const char *statusText, const char *text ) {
	WA_Respond( c, status, statusText, "text/plain; charset=utf-8", NULL, text, (int)strlen( text ) );
}

void WA_RespondJson( waConnection_t *c, waBuffer_t *json ) {
	WA_Respond( c, 200, "OK", "application/json", "Cache-Control: no-store\r\n", json->data, json->len );
}

/*
==============================================================================

Request helpers

==============================================================================
*/

// the address of a peer as text, for the security log
static const char *WA_AddrText( const waConnection_t *c ) {
	const unsigned char *b = (const unsigned char *)&c->addr;		// network order: the bytes as they are written

	return va( "%u.%u.%u.%u", b[0], b[1], b[2], b[3] );
}

// Not in JKAdmin: what was done through the panel and from where, one line in the security log. The panel
// changes the game with the rights of an admin, and without this nothing on the server showed that it was used.
void WA_LogUse( const waConnection_t *c, const char *what ) {
	char line[320];

	Com_sprintf( line, sizeof( line ), "Web admin: %s: %.280s", WA_AddrText( c ), what );
	Q_strstrip( line, "\n\r", "  " );
	G_SecurityLogPrintf( "%s\n", line );
}

static int WA_Base64Value( char c ) {
	if ( c >= 'A' && c <= 'Z' ) return c - 'A';
	if ( c >= 'a' && c <= 'z' ) return c - 'a' + 26;
	if ( c >= '0' && c <= '9' ) return c - '0' + 52;
	if ( c == '+' ) return 62;
	if ( c == '/' ) return 63;
	return -1;
}

static int WA_Base64Decode( const char *in, char *out, int outSize ) {
	int bits = 0, len = 0;
	unsigned int value = 0;		// unsigned: the running bits shift left without signed overflow

	for ( ; *in && *in != '\r' && *in != '\n' && *in != '='; in++ ) {
		const int v = WA_Base64Value( *in );
		if ( v < 0 )
			return -1;
		value = ( value << 6 ) | (unsigned int)v;
		bits += 6;
		if ( bits >= 8 ) {
			bits -= 8;
			if ( len >= outSize - 1 )
				return -1;
			out[len++] = (char)( ( value >> bits ) & 0xFF );
		}
	}
	out[len] = '\0';
	return len;
}

// Finds a header value in the request head, case insensitive name. Returns a
// pointer to the start of the value (ends at \r) or NULL.
const char *WA_FindHeader( const char *head, const char *name ) {
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

// Copies the value of a header up to the end of its line, without the spaces at its end. qfalse: not there.
static qboolean WA_HeaderValue( const char *head, const char *name, char *out, int outSize ) {
	const char	*v = WA_FindHeader( head, name );
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
WA_SameOrigin

Not in JKAdmin. A request that changes something has to come from the page itself. The browser sends the login
of the panel along with every request to it, also with one that another web site makes it send (CSRF). That
site would get at the player actions and at the entity editor without knowing the password.

Browsers say where a POST comes from: Sec-Fetch-Site, Origin, Referer. A page can not leave out or forge the
first two. A request with none of the three is not from a browser (curl, scripts) and goes through.
The address in Origin has to be the one the request was sent to: Host, or X-Forwarded-Host behind a reverse
proxy that sends another Host to the server (a page can not set that header on a request to another site).
=================
*/
qboolean WA_SameOrigin( const char *head ) {
	static const char *const sentTo[] = { "Host", "X-Forwarded-Host" };
	char		from[512], host[256];
	const char	*address;
	size_t		i;

	if ( WA_HeaderValue( head, "Sec-Fetch-Site", from, sizeof( from ) ) && Q_stricmp( from, "same-origin" ) && Q_stricmp( from, "none" ) )
		return qfalse;

	if ( !WA_HeaderValue( head, "Origin", from, sizeof( from ) ) && !WA_HeaderValue( head, "Referer", from, sizeof( from ) ) )
		return qtrue;

	// scheme://host[:port][/path]. "null" (a sandboxed frame, a data: page) is no address.
	address = strstr( from, "://" );
	if ( !address )
		return qfalse;
	address += 3;

	for ( i = 0; i < ARRAY_LEN( sentTo ); i++ ) {
		if ( WA_HeaderValue( head, sentTo[i], host, sizeof( host ) ) && host[0] ) {
			const size_t len = strlen( host );

			if ( !Q_stricmpn( address, host, (int)len ) && ( address[len] == '\0' || address[len] == '/' ) )
				return qtrue;
		}
	}
	return qfalse;
}

qboolean WA_CheckAuth( const char *head ) {
	const char *auth = WA_FindHeader( head, "Authorization" );
	char		decoded[256];
	const char	*pass;

	if ( !auth || Q_stricmpn( auth, "Basic ", 6 ) )
		return qfalse;
	if ( WA_Base64Decode( auth + 6, decoded, sizeof( decoded ) ) < 0 )
		return qfalse;

	pass = strchr( decoded, ':' );
	if ( !pass || !g_webPassword.string[0] )
		return qfalse;

	// constant time: the time it takes does not tell how much of the password is right
	{
		const char	*a = pass + 1, *b = g_webPassword.string;
		const int	la = (int)strlen( a ), lb = (int)strlen( b );
		int			diff = la ^ lb, i;

		for ( i = 0; i < la; i++ )
			diff |= a[i] ^ b[i % lb];
		return (qboolean)( diff == 0 );
	}
}

/*
=================
WA_AuthThrottle

Not in JKAdmin. Slows down guessing the panel password: after WA_AUTH_TRIES wrong tries an address has to wait,
and the wait doubles each time. WA_AuthThrottle tells if an address is waiting right now, WA_AuthResult records
how a try went. A small table of addresses, the slot not seen for the longest is reused when it is full.
=================
*/
static waAuthFail_t *WA_AuthSlot( unsigned int addr, qboolean create ) {
	const int	now = trap->Milliseconds();
	int			i, oldest = 0;

	for ( i = 0; i < WA_AUTH_SLOTS; i++ ) {
		if ( authFails[i].fails && authFails[i].addr == addr )
			return &authFails[i];
	}
	if ( !create )
		return NULL;
	for ( i = 0; i < WA_AUTH_SLOTS; i++ ) {
		if ( !authFails[i].fails )
			return &authFails[i];		// a free slot
		if ( authFails[i].seen < authFails[oldest].seen )
			oldest = i;
	}
	// full: take the slot not used for the longest, but not one still blocking
	if ( authFails[oldest].until > now )
		return NULL;
	memset( &authFails[oldest], 0, sizeof( authFails[oldest] ) );
	return &authFails[oldest];
}

qboolean WA_AuthThrottle( unsigned int addr ) {
	waAuthFail_t	*slot = WA_AuthSlot( addr, qfalse );
	const int		now = trap->Milliseconds();

	return (qboolean)( slot && slot->until > now );
}

void WA_AuthResult( unsigned int addr, qboolean ok ) {
	const int		now = trap->Milliseconds();
	waAuthFail_t	*slot;

	if ( ok ) {
		if ( (slot = WA_AuthSlot( addr, qfalse )) )
			memset( slot, 0, sizeof( *slot ) );
		return;
	}
	if ( !(slot = WA_AuthSlot( addr, qtrue )) )
		return;

	slot->addr = addr;
	slot->seen = now;
	slot->fails++;
	if ( slot->fails >= WA_AUTH_TRIES ) {
		const int steps = slot->fails - WA_AUTH_TRIES;
		int wait = WA_AUTH_BASE_MS;

		// double the wait for each try past the limit, capped (steps stays small, no overflow)
		if ( steps < 16 )
			wait <<= steps;
		if ( wait > WA_AUTH_MAX_MS || wait <= 0 )
			wait = WA_AUTH_MAX_MS;
		slot->until = now + wait;
	}
}

// Looks up key in an "a=1&b=2" string. Returns qfalse if missing. The value is decoded as it is copied: the room
// of out is for what the value says, not for how it was written. A file name with a %2F for every slash is longer
// on the wire than MAX_QPATH while the name itself fits, and it was cut before it was decoded.
qboolean WA_FormValue( const char *form, const char *key, char *out, int outSize ) {
	const size_t keyLen = strlen( key );
	const char *p = form;

	while ( p && *p ) {
		if ( !strncmp( p, key, keyLen ) && p[keyLen] == '=' ) {
			const char	*v = p + keyLen + 1;
			int			n = 0;

			for ( ; *v && *v != '&' && n < outSize - 1; v++ ) {
				if ( *v == '+' ) {
					out[n++] = ' ';
				} else if ( *v == '%' && v[1] && v[1] != '&' && v[2] && v[2] != '&' ) {
					char hex[3] = { v[1], v[2], 0 };

					out[n++] = (char)strtol( hex, NULL, 16 );
					v += 2;
				} else {
					out[n++] = *v;
				}
			}
			out[n] = '\0';
			return qtrue;
		}
		p = strchr( p, '&' );
		if ( p )
			p++;
	}
	out[0] = '\0';
	return qfalse;
}

// The page sends UTF-8, the game has one byte per character (Latin-1). What the game can not show becomes '?'.
void WA_Utf8ToLatin1( char *s ) {
	const unsigned char	*in = (const unsigned char *)s;
	char				*out = s;

	while ( *in ) {
		if ( *in < 0x80 ) {
			*out++ = (char)*in++;
		} else if ( ( *in & 0xE0 ) == 0xC0 && ( in[1] & 0xC0 ) == 0x80 ) {
			const int cp = ( ( *in & 0x1F ) << 6 ) | ( in[1] & 0x3F );
			*out++ = ( cp >= 0xA0 && cp <= 0xFF ) ? (char)cp : '?';
			in += 2;
		} else {
			// a longer sequence or a stray byte
			*out++ = '?';
			in++;
			while ( ( *in & 0xC0 ) == 0x80 )
				in++;
		}
	}
	*out = '\0';
}

char *WA_ReadGameFile( const char *path, int *outLen ) {
	fileHandle_t	f;
	const int		len = trap->FS_Open( path, &f, FS_READ );
	char			*data;

	if ( len <= 0 || !f ) {
		if ( f )
			trap->FS_Close( f );
		return NULL;
	}
	if ( len > WA_MAX_FILE ) {
		trap->FS_Close( f );
		return NULL;
	}

	data = (char *)malloc( len );
	if ( !data ) {
		trap->FS_Close( f );
		return NULL;
	}
	trap->FS_Read( data, len, f );
	trap->FS_Close( f );
	*outLen = len;
	return data;
}

// text of the game, one byte per character, as the UTF-8 the page reads
void WA_BufLatin1AsUtf8( waBuffer_t *b, const char *text, int len ) {
	int i;

	for ( i = 0; i < len; i++ ) {
		const unsigned char ch = (unsigned char)text[i];

		if ( ch < 0x80 ) {
			WA_BufAppend( b, &text[i], 1 );
		} else {
			const char utf8[2] = { (char)( 0xC0 | ( ch >> 6 ) ), (char)( 0x80 | ( ch & 0x3F ) ) };

			WA_BufAppend( b, utf8, 2 );
		}
	}
}

// Text the page sends as the body of a request, changed in place into what goes into a file: one byte per
// character and LF at the end of a line (a browser sends the lines of a text field with CR LF). Returns its length.
int WA_TextBody( char *text ) {
	char *in, *out;

	WA_Utf8ToLatin1( text );
	for ( in = out = text; *in; in++ ) {
		if ( *in != '\r' )
			*out++ = *in;
	}
	*out = '\0';
	return (int)( out - text );
}

qboolean WA_WriteGameFile( const char *path, const char *data, int len ) {
	fileHandle_t f = 0;

	trap->FS_Open( path, &f, FS_WRITE );
	if ( !f )
		return qfalse;
	trap->FS_Write( data, len, f );
	trap->FS_Close( f );
	return qtrue;
}

