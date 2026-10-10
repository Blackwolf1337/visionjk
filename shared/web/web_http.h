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

/*
web_http.h -- what the two HTTP servers of the tree read a request with: the web admin panel of the game module
(codemp/game/webadmin) and the web host of the dedicated server (codemp/server/web). Plain C that knows neither
the engine nor the game, so both are built from the same files.

  web_http.c     the headers of a request, a length, and whether a request comes from the page itself
  web_base64.c   base64, both ways
  web_sha1.c     SHA-1, which the handshake of a WebSocket asks for
  web_sha256.c   SHA-256, with which the updater of the client checks what it fetched
*/

#pragma once

#include <stddef.h>

#include "qcommon/q_platform.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define WEB_SHA1_SIZE	20
#define WEB_SHA256_SIZE	32

// web_http.c
const char *Web_FindHeader( const char *head, const char *name );
qboolean Web_HeaderValue( const char *head, const char *name, char *out, int outSize );
int Web_ParseLength( const char *s, int max );
qboolean Web_SameOrigin( const char *head );

// web_base64.c
int Web_Base64Decode( const char *in, char *out, int outSize );
int Web_Base64Encode( const void *in, int inLen, char *out, int outSize );

// web_sha1.c
void Web_SHA1( const void *data, size_t len, unsigned char digest[WEB_SHA1_SIZE] );

// web_sha256.c
typedef struct webSha256_s {
	uint32_t		state[8];
	uint64_t		length;			// bytes so far
	unsigned char	block[64];		// the bytes that do not fill a block yet,
	size_t			used;			//	and how many those are
} webSha256_t;

void Web_SHA256Init( webSha256_t *ctx );
void Web_SHA256Update( webSha256_t *ctx, const void *data, size_t len );
void Web_SHA256Final( webSha256_t *ctx, unsigned char digest[WEB_SHA256_SIZE] );

#if defined(__cplusplus)
} // extern "C"
#endif
