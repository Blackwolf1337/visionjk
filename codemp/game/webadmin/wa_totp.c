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

// wa_totp.c -- the second step of a login to the web admin panel: a code of six digits that an app on the
// phone of the admin (or his password manager) makes from a secret and the time, a new one every 30 seconds.
// RFC 6238 on RFC 4226, with SHA-1, which is what every such app speaks.
//
// The file knows neither the game nor the engine, so it is built and tested by itself
// (japlus_port/tools/authtest).

#include <string.h>

#include "qcommon/q_shared.h"
#include "web/web_http.h"
#include "wa_totp.h"

/*
=================
WA_HmacSha1

RFC 2104. The message is short here (the eight bytes of a counter), a longer one is cut off.
=================
*/
void WA_HmacSha1( const byte *key, int keyLen, const byte *message, int messageLen, byte *digest ) {
	byte	block[64], inner[64 + WA_HMAC_MESSAGE], outer[64 + WEB_SHA1_SIZE];
	int		i;

	memset( block, 0, sizeof( block ) );
	if ( keyLen > (int)sizeof( block ) ) {
		Web_SHA1( key, (size_t)keyLen, block );
	} else {
		memcpy( block, key, keyLen );
	}
	if ( messageLen > WA_HMAC_MESSAGE ) {
		messageLen = WA_HMAC_MESSAGE;
	}
	for ( i = 0; i < 64; i++ ) {
		inner[i] = block[i] ^ 0x36;
		outer[i] = block[i] ^ 0x5c;
	}
	memcpy( inner + 64, message, messageLen );
	Web_SHA1( inner, (size_t)( 64 + messageLen ), outer + 64 );
	Web_SHA1( outer, sizeof( outer ), digest );
	memset( block, 0, sizeof( block ) );
	memset( inner, 0, sizeof( inner ) );
	memset( outer, 0, sizeof( outer ) );
}

/*
=================
WA_TotpCode

The code of a step: the step is the time in seconds divided by 30
=================
*/
int WA_TotpCode( const byte *secret, int secretLen, long long step ) {
	byte			counter[8], digest[WEB_SHA1_SIZE];
	unsigned int	value;
	int				i, offset;

	for ( i = 7; i >= 0; i-- ) {
		counter[i] = (byte)( step & 0xff );
		step >>= 8;
	}
	WA_HmacSha1( secret, secretLen, counter, sizeof( counter ), digest );
	offset = digest[WEB_SHA1_SIZE - 1] & 15;
	value = ( (unsigned int)( digest[offset] & 0x7f ) << 24 ) | ( (unsigned int)digest[offset + 1] << 16 )
		| ( (unsigned int)digest[offset + 2] << 8 ) | digest[offset + 3];
	return (int)( value % 1000000 );
}

/*
=================
WA_TotpMatch

The step a typed code belongs to: the one of now, the one before or the one after (the clock of a phone is a
little off, and typing takes time). -1: it belongs to none of them. Blanks in the code do not count.
=================
*/
long long WA_TotpMatch( const byte *secret, int secretLen, const char *text, long long now ) {
	const long long	step = now / WA_TOTP_SECONDS;
	long long		found = -1;
	int				code = 0, digits = 0, i;

	for ( ; *text; text++ ) {
		if ( *text == ' ' ) {
			continue;
		}
		if ( *text < '0' || *text > '9' || digits == 6 ) {
			return -1;
		}
		code = code * 10 + ( *text - '0' );
		digits++;
	}
	if ( digits != 6 ) {
		return -1;
	}
	// all three are worked out, whichever fits
	for ( i = -1; i <= 1; i++ ) {
		if ( WA_TotpCode( secret, secretLen, step + i ) == code ) {
			found = step + i;
		}
	}
	return found;
}

/*
=================
WA_Base32

RFC 4648 without padding: how an app takes a secret that is typed or scanned
=================
*/
void WA_Base32( const byte *in, int len, char *out, int outSize ) {
	static const char	chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
	unsigned int		bits = 0;
	int					have = 0, n = 0, i;

	for ( i = 0; i < len; i++ ) {
		bits = ( bits << 8 ) | in[i];
		have += 8;
		while ( have >= 5 && n < outSize - 1 ) {
			have -= 5;
			out[n++] = chars[( bits >> have ) & 31];
		}
	}
	if ( have > 0 && n < outSize - 1 ) {
		out[n++] = chars[( bits << ( 5 - have ) ) & 31];
	}
	if ( outSize > 0 ) {
		out[n] = '\0';
	}
}
