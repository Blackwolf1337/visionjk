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

// web_missing.c -- the paks a web address did not have. See web_missing.h.

#include "web_missing.h"

#include <stdio.h>
#include <string.h>

typedef struct {
	long long	time;
	const char	*base;			// in the text, baseLen long
	size_t		baseLen;
	const char	*remote;		// the rest of the line: a name may have blanks
	size_t		remoteLen;
} webMissingLine_t;

// One line from line to its end (\n or \r\n, or the end of the text). qfalse: it is not one of the file.
static qboolean WEB_MissingParse( const char *line, size_t len, webMissingLine_t *out ) {
	size_t i = 0;

	if ( len && line[len - 1] == '\r' ) {
		len--;
	}
	out->time = 0;
	while ( i < len && line[i] >= '0' && line[i] <= '9' && i < 18 ) {
		out->time = out->time * 10 + ( line[i] - '0' );
		i++;
	}
	if ( !i || i >= len || line[i] != ' ' ) {
		return qfalse;
	}
	out->base = line + ++i;
	while ( i < len && line[i] != ' ' ) {
		i++;
	}
	out->baseLen = (size_t)( line + i - out->base );
	if ( !out->baseLen || i >= len ) {
		return qfalse;
	}
	out->remote = line + ++i;
	out->remoteLen = len - i;
	return out->remoteLen ? qtrue : qfalse;
}

// young enough: from now back six days. A time after now (a clock that was set back) counts as old.
static qboolean WEB_MissingYoung( const webMissingLine_t *line, long long now ) {
	return ( line->time <= now && now - line->time < WEB_MISSING_SECONDS ) ? qtrue : qfalse;
}

static qboolean WEB_MissingIs( const webMissingLine_t *line, const char *base, const char *remote ) {
	return ( line->baseLen == strlen( base ) && !strncmp( line->base, base, line->baseLen )
		&& line->remoteLen == strlen( remote ) && !strncmp( line->remote, remote, line->remoteLen ) ) ? qtrue : qfalse;
}

qboolean WEB_MissingListed( const char *text, const char *base, const char *remote, long long now ) {
	const char *line = text;

	while ( *line ) {
		const char			*end = strchr( line, '\n' );
		const size_t		len = end ? (size_t)( end - line ) : strlen( line );
		webMissingLine_t	parsed;

		if ( WEB_MissingParse( line, len, &parsed ) && WEB_MissingYoung( &parsed, now ) && WEB_MissingIs( &parsed, base, remote ) ) {
			return qtrue;
		}
		line += len + ( end ? 1 : 0 );
	}
	return qfalse;
}

size_t WEB_MissingAdd( const char *text, const char *base, const char *remote, long long now, char *out, size_t outSize ) {
	char		added[512];
	const char	*line = text;
	size_t		used = 0, addedLen;
	int			n;

	if ( !outSize ) {
		return 0;
	}
	out[0] = '\0';
	// (a line of its own each, with no blank in the address)
	if ( !base[0] || !remote[0] || strpbrk( base, " \r\n" ) || strpbrk( remote, "\r\n" ) ) {
		return 0;
	}
	n = snprintf( added, sizeof( added ), "%lld %s %s\n", now, base, remote );
	if ( n < 0 || (size_t)n >= sizeof( added ) || (size_t)n >= outSize ) {
		return 0;
	}
	addedLen = (size_t)n;
	while ( *line ) {
		const char			*end = strchr( line, '\n' );
		const size_t		len = end ? (size_t)( end - line ) : strlen( line );
		webMissingLine_t	parsed;

		if ( WEB_MissingParse( line, len, &parsed ) && WEB_MissingYoung( &parsed, now ) && !WEB_MissingIs( &parsed, base, remote ) ) {
			// (the file is in the order of time: what does not fit any more are the oldest lines, at the top)
			while ( used + len + 1 + addedLen >= outSize && used ) {
				const char	*next = memchr( out, '\n', used );
				size_t		drop = next ? (size_t)( next - out ) + 1 : used;

				memmove( out, out + drop, used - drop );
				used -= drop;
			}
			if ( used + len + 1 + addedLen < outSize ) {
				memcpy( out + used, line, len );
				used += len;
				out[used++] = '\n';
			}
		}
		line += len + ( end ? 1 : 0 );
	}
	memcpy( out + used, added, addedLen + 1 );
	return used + addedLen;
}
