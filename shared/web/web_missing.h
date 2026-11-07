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
web_missing.h -- the paks a web address did not have, as the client plugin of JA+ kept them in
japlus/urlRedirect_cache.txt: one line each, "<unix time> <web address> <game folder>/<name>.pk3". A pak that is
listed for the web address of a server is not asked there again for six days (client/cl_download.cpp). Only an
answer 404 puts it there, not a line that broke off. Plain text work, the file itself is read and written by the
caller.
*/

#pragma once

#include <stddef.h>

#include "qcommon/q_platform.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define WEB_MISSING_SECONDS		( 6 * 24 * 60 * 60 )

// remote is listed for base in text and the line is younger than six days at now
qboolean	WEB_MissingListed( const char *text, const char *base, const char *remote, long long now );
// text without its old and broken lines and without remote for base, then remote for base at now, into out (cut
// at outSize, the oldest lines first). The length of out.
size_t		WEB_MissingAdd( const char *text, const char *base, const char *remote, long long now, char *out, size_t outSize );

#if defined(__cplusplus)
} // extern "C"
#endif
