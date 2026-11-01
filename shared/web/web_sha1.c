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

// web_sha1.c -- SHA-1 (RFC 3174). The handshake of a WebSocket (RFC 6455) answers the key of the client with
// the hash of it: nothing is kept safe with it there.
//
// The voice chat uses it too, on servers without the game module of this tree: the cgame makes the name of its
// room with it (Voice_MasterRoom of cg_voice.c), and the masterserver its tokens, 32 bits of the hash of a
// secret and an address. Nobody has to find two texts with one hash for either.

#include "web_http.h"

#include <string.h>

#define WEB_SHA1_ROL( v, n )	( ( (v) << (n) ) | ( (v) >> ( 32 - (n) ) ) )

// one block of 64 bytes, its words are big endian
static void Web_SHA1Block( uint32_t state[5], const unsigned char *block ) {
	uint32_t	w[80], a, b, c, d, e;
	int			i;

	for ( i = 0; i < 16; i++ ) {
		w[i] = ( (uint32_t)block[i * 4] << 24 ) | ( (uint32_t)block[i * 4 + 1] << 16 )
			| ( (uint32_t)block[i * 4 + 2] << 8 ) | (uint32_t)block[i * 4 + 3];
	}
	for ( ; i < 80; i++ )
		w[i] = WEB_SHA1_ROL( w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1 );

	a = state[0];
	b = state[1];
	c = state[2];
	d = state[3];
	e = state[4];

	for ( i = 0; i < 80; i++ ) {
		uint32_t f, k, t;

		if ( i < 20 ) {
			f = ( b & c ) | ( ~b & d );
			k = 0x5A827999;
		} else if ( i < 40 ) {
			f = b ^ c ^ d;
			k = 0x6ED9EBA1;
		} else if ( i < 60 ) {
			f = ( b & c ) | ( b & d ) | ( c & d );
			k = 0x8F1BBCDC;
		} else {
			f = b ^ c ^ d;
			k = 0xCA62C1D6;
		}

		t = WEB_SHA1_ROL( a, 5 ) + f + e + k + w[i];
		e = d;
		d = c;
		c = WEB_SHA1_ROL( b, 30 );
		b = a;
		a = t;
	}

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;
}

/*
=================
Web_SHA1

The hash of len bytes, all at once.
=================
*/
void Web_SHA1( const void *data, size_t len, unsigned char digest[WEB_SHA1_SIZE] ) {
	uint32_t			state[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
	const unsigned char	*p = (const unsigned char *)data;
	const uint64_t		bits = (uint64_t)len * 8;
	unsigned char		tail[128];
	size_t				tailLen;
	int					i;

	for ( ; len >= 64; p += 64, len -= 64 )
		Web_SHA1Block( state, p );

	// what is left of the message, a 1 bit, zeros, and at the end of a block the length of the message in bits
	// as 64 bits. That is one block, or two if the length does not fit behind what is left.
	tailLen = ( len < 56 ) ? 64 : 128;
	memset( tail, 0, sizeof( tail ) );
	memcpy( tail, p, len );
	tail[len] = 0x80;
	for ( i = 0; i < 8; i++ )
		tail[tailLen - 1 - i] = (unsigned char)( bits >> ( i * 8 ) );

	Web_SHA1Block( state, tail );
	if ( tailLen == 128 )
		Web_SHA1Block( state, tail + 64 );

	for ( i = 0; i < WEB_SHA1_SIZE; i++ )
		digest[i] = (unsigned char)( state[i >> 2] >> ( ( 3 - ( i & 3 ) ) * 8 ) );
}
