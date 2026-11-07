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

// web_base64.c -- base64 (RFC 4648) as HTTP headers carry it: the login of a request, the key of a WebSocket

#include "web_http.h"

static const char web_base64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int Web_Base64Value( char c ) {
	if ( c >= 'A' && c <= 'Z' ) return c - 'A';
	if ( c >= 'a' && c <= 'z' ) return c - 'a' + 26;
	if ( c >= '0' && c <= '9' ) return c - '0' + 52;
	if ( c == '+' ) return 62;
	if ( c == '/' ) return 63;
	return -1;
}

/*
=================
Web_Base64Decode

Reads up to the end of the text, of its line or to the padding. The result gets a zero behind it, which is not
counted. Returns its length, or -1 for a character that is none of base64 and for a result that does not fit.
=================
*/
int Web_Base64Decode( const char *in, char *out, int outSize ) {
	int bits = 0, len = 0;
	unsigned int value = 0;		// unsigned: the running bits shift left without signed overflow

	for ( ; *in && *in != '\r' && *in != '\n' && *in != '='; in++ ) {
		const int v = Web_Base64Value( *in );
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

/*
=================
Web_Base64Encode

With padding, and a zero behind the text that is not counted. Returns the length of the text, or -1 if out is
too small for all of it (four characters for every three bytes begun).
=================
*/
int Web_Base64Encode( const void *in, int inLen, char *out, int outSize ) {
	const unsigned char	*p = (const unsigned char *)in;
	int					len = 0, i;

	if ( outSize < 1 )
		return -1;
	out[0] = '\0';
	if ( inLen < 0 || inLen / 3 + ( inLen % 3 != 0 ) > ( outSize - 1 ) / 4 )
		return -1;

	for ( i = 0; i < inLen; i += 3 ) {
		const int		n = inLen - i;		// 1 or 2: the last group is not full
		unsigned int	v = (unsigned int)p[i] << 16;

		if ( n > 1 )
			v |= (unsigned int)p[i + 1] << 8;
		if ( n > 2 )
			v |= p[i + 2];

		out[len++] = web_base64Chars[( v >> 18 ) & 63];
		out[len++] = web_base64Chars[( v >> 12 ) & 63];
		out[len++] = ( n > 1 ) ? web_base64Chars[( v >> 6 ) & 63] : '=';
		out[len++] = ( n > 2 ) ? web_base64Chars[v & 63] : '=';
	}
	out[len] = '\0';
	return len;
}
