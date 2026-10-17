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

// bg_auth.c -- admin login with a key, the parts game and cgame share. See bg_auth.h.
//
// Nothing in here calls into the engine or the modules, so the file can be built and tested by itself
// (japlus_port/tools/authtest).

#include <string.h>
#include <stdio.h>

#if defined(_WIN32)
	#include <windows.h>
	#include <wincrypt.h>
#endif

#include "qcommon/q_shared.h"
#include "web/web_http.h"
#include "bg_auth.h"

#define AUTH_KEY_TYPE		"ssh-ed25519"
#define AUTH_KEY_TYPE_LEN	11
#define AUTH_BLOB_SIZE		( 4 + AUTH_KEY_TYPE_LEN + 4 + AUTH_PUBLIC_SIZE )		// a public key as ssh passes it around
#define AUTH_FIRST_LINE		"VisionJK admin login v1\n"
#define AUTH_FILE_MAGIC		"openssh-key-v1"
#define AUTH_FILE_BEGIN		"-----BEGIN OPENSSH PRIVATE KEY-----"
#define AUTH_FILE_END		"-----END OPENSSH PRIVATE KEY-----"

/*
=================
BG_AuthRandom
=================
*/
qboolean BG_AuthRandom( byte *out, int size ) {
#if defined(_WIN32)
	HCRYPTPROV	prov;
	qboolean	ok;

	if ( !CryptAcquireContext( &prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT ) ) {
		return qfalse;
	}
	ok = (qboolean)( CryptGenRandom( prov, (DWORD)size, out ) != 0 );
	CryptReleaseContext( prov, 0 );
	return ok;
#else
	// also what a browser build has: there the bytes come from crypto.getRandomValues()
	FILE		*f = fopen( "/dev/urandom", "rb" );
	qboolean	ok;

	if ( !f ) {
		return qfalse;
	}
	ok = (qboolean)( fread( out, 1, (size_t)size, f ) == (size_t)size );
	fclose( f );
	return ok;
#endif
}

/*
=================
BG_AuthMessage
=================
*/
int BG_AuthMessage( byte *out, int outSize, const byte *nonce, const char *address, int clientNum ) {
	const int	first = (int)strlen( AUTH_FIRST_LINE );
	char		tail[AUTH_ADDRESS_SIZE + 16];
	int			tailLen;

	if ( strlen( address ) >= AUTH_ADDRESS_SIZE ) {
		return 0;
	}
	Com_sprintf( tail, sizeof( tail ), "%s\n%i", address, clientNum );
	tailLen = (int)strlen( tail );
	if ( first + AUTH_NONCE_SIZE + tailLen > outSize ) {
		return 0;
	}
	memcpy( out, AUTH_FIRST_LINE, first );
	memcpy( out + first, nonce, AUTH_NONCE_SIZE );
	memcpy( out + first + AUTH_NONCE_SIZE, tail, tailLen );
	return first + AUTH_NONCE_SIZE + tailLen;
}

/*
=================
BG_AuthDecode / BG_AuthEncode
=================
*/
qboolean BG_AuthDecode( const char *text, byte *out, int size ) {
	char raw[AUTH_BASE64_SIZE];

	if ( size >= (int)sizeof( raw ) || strlen( text ) > ( (size_t)size / 3 + 1 ) * 4 ) {
		return qfalse;
	}
	if ( Web_Base64Decode( text, raw, sizeof( raw ) ) != size ) {
		return qfalse;
	}
	memcpy( out, raw, size );
	return qtrue;
}

void BG_AuthEncode( const byte *in, int size, char *out, int outSize ) {
	if ( Web_Base64Encode( in, size, out, outSize ) < 0 && outSize > 0 ) {
		out[0] = '\0';
	}
}

// four bytes, the high one first, as everything of ssh has its lengths
static void Auth_PutLong( byte *out, unsigned int v ) {
	out[0] = (byte)( v >> 24 );
	out[1] = (byte)( v >> 16 );
	out[2] = (byte)( v >> 8 );
	out[3] = (byte)v;
}

static unsigned int Auth_GetLong( const byte *in ) {
	return ( (unsigned int)in[0] << 24 ) | ( (unsigned int)in[1] << 16 ) | ( (unsigned int)in[2] << 8 ) | in[3];
}

// a length and that many bytes
static int Auth_PutString( byte *out, const void *data, int len ) {
	Auth_PutLong( out, (unsigned int)len );
	memcpy( out + 4, data, len );
	return 4 + len;
}

// The next string of a buffer of ssh: qfalse if it does not lie inside of it. *pos is behind it afterwards.
static qboolean Auth_GetString( const byte *buf, int size, int *pos, const byte **data, int *len ) {
	unsigned int n;

	if ( *pos < 0 || size - *pos < 4 ) {
		return qfalse;
	}
	n = Auth_GetLong( buf + *pos );
	if ( n > (unsigned int)( size - *pos - 4 ) ) {
		return qfalse;
	}
	*data = buf + *pos + 4;
	*len = (int)n;
	*pos += 4 + (int)n;
	return qtrue;
}

static void Auth_PublicBlob( const byte *pub, byte *blob ) {
	int n = Auth_PutString( blob, AUTH_KEY_TYPE, AUTH_KEY_TYPE_LEN );

	Auth_PutString( blob + n, pub, AUTH_PUBLIC_SIZE );
}

// the 32 bytes out of a public key as ssh passes it around, qfalse: it is no Ed25519 key
static qboolean Auth_BlobPublic( const byte *blob, int size, byte *pub ) {
	const byte	*data;
	int			pos = 0, len;

	if ( !Auth_GetString( blob, size, &pos, &data, &len ) || len != AUTH_KEY_TYPE_LEN || memcmp( data, AUTH_KEY_TYPE, len ) ) {
		return qfalse;
	}
	if ( !Auth_GetString( blob, size, &pos, &data, &len ) || len != AUTH_PUBLIC_SIZE || pos != size ) {
		return qfalse;
	}
	memcpy( pub, data, AUTH_PUBLIC_SIZE );
	return qtrue;
}

/*
=================
BG_AuthParsePublicLine
=================
*/
qboolean BG_AuthParsePublicLine( const char *line, byte *pub, char *comment, int commentSize ) {
	const char	*p = strstr( line, AUTH_KEY_TYPE " " );
	char		text[AUTH_BASE64_SIZE], raw[AUTH_BASE64_SIZE];
	int			n = 0, len;

	if ( comment && commentSize > 0 ) {
		comment[0] = '\0';
	}
	if ( !p ) {
		return qfalse;
	}
	for ( p += AUTH_KEY_TYPE_LEN + 1; *p == ' ' || *p == '\t'; p++ ) {
	}
	for ( ; *p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n'; p++ ) {
		if ( n >= (int)sizeof( text ) - 1 ) {
			return qfalse;
		}
		text[n++] = *p;
	}
	text[n] = '\0';
	len = Web_Base64Decode( text, raw, sizeof( raw ) );
	if ( len != AUTH_BLOB_SIZE || !Auth_BlobPublic( (const byte *)raw, len, pub ) ) {
		return qfalse;
	}
	if ( comment && commentSize > 0 ) {
		for ( ; *p == ' ' || *p == '\t'; p++ ) {
		}
		for ( n = 0; *p && *p != '\r' && *p != '\n' && n < commentSize - 1; p++ ) {
			// it is printed on consoles and in the panel
			comment[n++] = Q_isLogChar( (unsigned char)*p ) ? *p : '_';
		}
		comment[n] = '\0';
	}
	return qtrue;
}

void BG_AuthPublicLine( const byte *pub, const char *comment, char *out, int outSize ) {
	byte blob[AUTH_BLOB_SIZE];
	char text[AUTH_BASE64_SIZE];

	Auth_PublicBlob( pub, blob );
	BG_AuthEncode( blob, sizeof( blob ), text, sizeof( text ) );
	if ( comment && comment[0] ) {
		Com_sprintf( out, outSize, AUTH_KEY_TYPE " %s %s", text, comment );
	} else {
		Com_sprintf( out, outSize, AUTH_KEY_TYPE " %s", text );
	}
}

/*
=================
BG_AuthFingerprint

The SHA-256 of the public key as ssh passes it around, as base64 without its padding
=================
*/
void BG_AuthFingerprint( const byte *pub, char *out, int outSize ) {
	byte			blob[AUTH_BLOB_SIZE];
	unsigned char	digest[WEB_SHA256_SIZE];
	char			text[AUTH_BASE64_SIZE], *end;

	Auth_PublicBlob( pub, blob );
	Web_SHA256( blob, sizeof( blob ), digest );
	BG_AuthEncode( digest, sizeof( digest ), text, sizeof( text ) );
	end = strchr( text, '=' );
	if ( end ) {
		*end = '\0';
	}
	Com_sprintf( out, outSize, "SHA256:%s", text );
}

/*
=================
BG_AuthReadKeyFile

The file, once the base64 between its two lines is taken apart:

  "openssh-key-v1" and a zero
  string   cipher, "none" without a passphrase
  string   how the passphrase becomes a key, "none"
  string   the options of that, empty
  uint32   number of keys, 1
  string   the public key
  string   the secret part, encrypted if there is a cipher:
             uint32 uint32   the same number twice, to tell a wrong passphrase
             string          "ssh-ed25519"
             string          the public key, 32 bytes
             string          the seed and the public key, 64 bytes
             string          the comment
             1, 2, 3, ...    up to a multiple of the block of the cipher, 8 without one
=================
*/
static qboolean Auth_Why( char *why, int whySize, const char *text ) {
	if ( why && whySize > 0 ) {
		Q_strncpyz( why, text, whySize );
	}
	return qfalse;
}

qboolean BG_AuthReadKeyFile( const char *text, byte *secret, char *comment, int commentSize, char *why, int whySize ) {
	const char	*begin = strstr( text, AUTH_FILE_BEGIN ), *end;
	static char	packed[AUTH_FILE_SIZE], raw[AUTH_FILE_SIZE];
	const byte	*buf = (const byte *)raw, *data, *inner;
	const int	magic = (int)sizeof( AUTH_FILE_MAGIC );		// with its zero
	int			n = 0, size, pos, len, innerSize;

	if ( comment && commentSize > 0 ) {
		comment[0] = '\0';
	}
	if ( !begin || !( end = strstr( begin, AUTH_FILE_END ) ) ) {
		if ( strstr( text, "-----BEGIN " ) ) {
			return Auth_Why( why, whySize, "it is a key file, but not one in the format of OpenSSH (ssh-keygen -t ed25519 writes that)" );
		}
		return Auth_Why( why, whySize, "it is no key file of OpenSSH" );
	}
	for ( begin += strlen( AUTH_FILE_BEGIN ); begin < end; begin++ ) {
		if ( *begin == '\r' || *begin == '\n' || *begin == ' ' || *begin == '\t' ) {
			continue;
		}
		if ( n >= (int)sizeof( packed ) - 1 ) {
			return Auth_Why( why, whySize, "the file is too long for an Ed25519 key" );
		}
		packed[n++] = *begin;
	}
	packed[n] = '\0';
	size = Web_Base64Decode( packed, raw, sizeof( raw ) );
	if ( size < magic || memcmp( raw, AUTH_FILE_MAGIC, magic ) ) {
		return Auth_Why( why, whySize, "the file is damaged" );
	}

	pos = magic;
	if ( !Auth_GetString( buf, size, &pos, &data, &len ) ) {
		return Auth_Why( why, whySize, "the file is damaged" );
	}
	if ( len != 4 || memcmp( data, "none", 4 ) ) {
		return Auth_Why( why, whySize, "the key has a passphrase. Load it into an ssh-agent and set cg_authAgent 1" );
	}
	if ( !Auth_GetString( buf, size, &pos, &data, &len ) || !Auth_GetString( buf, size, &pos, &data, &len )
		|| size - pos < 4 || Auth_GetLong( buf + pos ) != 1 ) {
		return Auth_Why( why, whySize, "the file is damaged, or it holds more than one key" );
	}
	pos += 4;
	if ( !Auth_GetString( buf, size, &pos, &data, &len ) || !Auth_GetString( buf, size, &pos, &inner, &innerSize ) ) {
		return Auth_Why( why, whySize, "the file is damaged" );
	}

	pos = 8;
	if ( innerSize < 8 || Auth_GetLong( inner ) != Auth_GetLong( inner + 4 ) ) {
		return Auth_Why( why, whySize, "the file is damaged" );
	}
	if ( !Auth_GetString( inner, innerSize, &pos, &data, &len ) || len != AUTH_KEY_TYPE_LEN || memcmp( data, AUTH_KEY_TYPE, len ) ) {
		return Auth_Why( why, whySize, "it is no Ed25519 key (ssh-keygen -t ed25519 makes one)" );
	}
	if ( !Auth_GetString( inner, innerSize, &pos, &data, &len ) || len != AUTH_PUBLIC_SIZE
		|| !Auth_GetString( inner, innerSize, &pos, &data, &len ) || len != AUTH_SECRET_SIZE ) {
		return Auth_Why( why, whySize, "the file is damaged" );
	}
	memcpy( secret, data, AUTH_SECRET_SIZE );
	if ( comment && commentSize > 0 && Auth_GetString( inner, innerSize, &pos, &data, &len ) ) {
		for ( n = 0; n < len && n < commentSize - 1; n++ ) {
			comment[n] = Q_isLogChar( data[n] ) ? (char)data[n] : '_';
		}
		comment[n] = '\0';
	}
	// what was read lies in buffers that stay
	memset( packed, 0, sizeof( packed ) );
	memset( raw, 0, sizeof( raw ) );
	return qtrue;
}

/*
=================
BG_AuthWriteKeyFile
=================
*/
int BG_AuthWriteKeyFile( const byte *secret, const char *comment, char *out, int outSize ) {
	static byte	raw[AUTH_FILE_SIZE / 2], inner[AUTH_FILE_SIZE / 2];
	static char	packed[AUTH_FILE_SIZE];
	byte		blob[AUTH_BLOB_SIZE], check[4];
	const int	commentLen = (int)strlen( comment );
	int			n = 0, innerSize = 0, len, i, pad;

	if ( commentLen > 64 || !BG_AuthRandom( check, sizeof( check ) ) ) {
		return 0;
	}
	Auth_PublicBlob( secret + AUTH_PUBLIC_SIZE, blob );

	memcpy( inner, check, 4 );
	memcpy( inner + 4, check, 4 );
	innerSize = 8;
	innerSize += Auth_PutString( inner + innerSize, AUTH_KEY_TYPE, AUTH_KEY_TYPE_LEN );
	innerSize += Auth_PutString( inner + innerSize, secret + AUTH_PUBLIC_SIZE, AUTH_PUBLIC_SIZE );
	innerSize += Auth_PutString( inner + innerSize, secret, AUTH_SECRET_SIZE );
	innerSize += Auth_PutString( inner + innerSize, comment, commentLen );
	for ( pad = 1; innerSize & 7; pad++ ) {
		inner[innerSize++] = (byte)pad;
	}

	memcpy( raw, AUTH_FILE_MAGIC, sizeof( AUTH_FILE_MAGIC ) );
	n = (int)sizeof( AUTH_FILE_MAGIC );
	n += Auth_PutString( raw + n, "none", 4 );
	n += Auth_PutString( raw + n, "none", 4 );
	n += Auth_PutString( raw + n, "", 0 );
	Auth_PutLong( raw + n, 1 );
	n += 4;
	n += Auth_PutString( raw + n, blob, sizeof( blob ) );
	n += Auth_PutString( raw + n, inner, innerSize );

	len = Web_Base64Encode( raw, n, packed, sizeof( packed ) );
	memset( raw, 0, sizeof( raw ) );
	memset( inner, 0, sizeof( inner ) );
	if ( len < 0 || outSize < 1 ) {
		return 0;
	}

	// lines of 70 characters between the first and the last one
	out[0] = '\0';
	Q_strcat( out, outSize, AUTH_FILE_BEGIN "\n" );
	for ( i = 0; i < len; i += 70 ) {
		char line[72];

		Q_strncpyz( line, packed + i, sizeof( line ) - 1 );
		Q_strcat( out, outSize, line );
		Q_strcat( out, outSize, "\n" );
	}
	Q_strcat( out, outSize, AUTH_FILE_END "\n" );
	memset( packed, 0, sizeof( packed ) );
	// Q_strcat() stops at the end of the buffer: a text that was cut is of no use
	if ( (int)strlen( out ) != len + ( len + 69 ) / 70 + (int)strlen( AUTH_FILE_BEGIN AUTH_FILE_END ) + 2 ) {
		return 0;
	}
	return (int)strlen( out );
}
