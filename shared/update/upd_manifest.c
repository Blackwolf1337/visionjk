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


// upd_manifest.c -- the updater of the client, the part that is plain text work. See upd_manifest.h.
//
// Whatever this file lets through is what the client then writes to and asks a server for, and all of it comes
// from outside: the file list from the website, the address maybe from a player. So every rule about what a
// path or an address may look like is here and nowhere else.

#include "upd_manifest.h"

#include <string.h>

static qboolean UPD_IsDigit( char c ) {
	return ( c >= '0' && c <= '9' ) ? qtrue : qfalse;
}

static qboolean UPD_IsAlnum( char c ) {
	return ( UPD_IsDigit( c ) || ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) ) ? qtrue : qfalse;
}

// A number of digits and nothing else, up to max. Returns the first character behind it, NULL: no such number.
static const char *UPD_Number( const char *text, const char *end, long long max, long long *value ) {
	long long v = 0;

	if ( text >= end || !UPD_IsDigit( *text ) )
		return NULL;
	for ( ; text < end && UPD_IsDigit( *text ); text++ ) {
		v = v * 10 + ( *text - '0' );
		if ( v > max )
			return NULL;
	}
	*value = v;
	return text;
}

/*
=================
UPD_ParseVersion

"0.1.1": three numbers with a dot between them and nothing before, between or after.
=================
*/
qboolean UPD_ParseVersion( const char *text, int version[3] ) {
	const char	*end = text + strlen( text );
	int			i;

	for ( i = 0; i < 3; i++ ) {
		long long v;

		text = UPD_Number( text, end, 9999, &v );
		if ( !text )
			return qfalse;
		version[i] = (int)v;
		if ( i < 2 && *text++ != '.' )
			return qfalse;
	}
	return ( text == end ) ? qtrue : qfalse;
}

/*
=================
UPD_ParseRelease

The version a build of the game was made from, as git describes it (GIT_TAG). Only the tag of a release is one,
"v0.1.1". A build between two releases ("v0.1.1-3-gabc1234") is somebody's own and is never replaced.
=================
*/
qboolean UPD_ParseRelease( const char *tag, int version[3] ) {
	return ( tag[0] == 'v' && UPD_ParseVersion( tag + 1, version ) ) ? qtrue : qfalse;
}

// less than 0: a is the older one. 0: the same. More: a is the newer one.
int UPD_CompareVersion( const int a[3], const int b[3] ) {
	int i;

	for ( i = 0; i < 3; i++ ) {
		if ( a[i] != b[i] )
			return a[i] - b[i];
	}
	return 0;
}

static qboolean UPD_EndsWith( const char *text, const char *ending ) {
	const size_t len = strlen( text ), endingLen = strlen( ending );

	return ( len >= endingLen && !strcmp( text + len - endingLen, ending ) ) ? qtrue : qfalse;
}

/*
=================
UPD_PathAllowed

A path of a file list: is it a file below the folder of the game, and nothing else? Letters, digits, dot, dash
and underscore, folders with forward slashes. No part of it starts with a dot (which also rules out ".."), it
does not start or end with a slash, has no drive and no backslash, and is none of the names the updater uses
for its own files.
=================
*/
qboolean UPD_PathAllowed( const char *path ) {
	const size_t	len = strlen( path );
	size_t			i;

	if ( !len || len >= UPD_MAX_PATH )
		return qfalse;
	for ( i = 0; i < len; i++ ) {
		const char		c = path[i];
		const qboolean	first = ( i == 0 || path[i - 1] == '/' ) ? qtrue : qfalse;

		if ( c == '/' ) {
			// an empty part: a slash at the start, at the end, or two in a row
			if ( first || i == len - 1 )
				return qfalse;
		} else if ( c == '.' ) {
			if ( first )
				return qfalse;
		} else if ( !UPD_IsAlnum( c ) && c != '-' && c != '_' ) {
			return qfalse;
		}
	}
	if ( UPD_EndsWith( path, UPD_NEW_ENDING ) || UPD_EndsWith( path, UPD_OLD_ENDING ) || !strcmp( path, UPD_JOURNAL ) )
		return qfalse;
	return qtrue;
}

/*
=================
UPD_AddressAllowed

Where the file lists and the files are asked for: "https://host/folder", with a port if there is one, and
nothing a web address can have beyond that (no name and password, no question mark, no fragment, no "..").
plainToo: "http://" is taken as well. That is for the address a build was given when it was made, by whoever
made it. An address a player gives is always https.
=================
*/
qboolean UPD_AddressAllowed( const char *address, qboolean plainToo ) {
	const size_t	len = strlen( address );
	const char		*host, *p;

	if ( len >= UPD_MAX_ADDRESS )
		return qfalse;
	if ( !strncmp( address, "https://", 8 ) )
		host = address + 8;
	else if ( plainToo && !strncmp( address, "http://", 7 ) )
		host = address + 7;
	else
		return qfalse;

	for ( p = host; UPD_IsAlnum( *p ) || *p == '.' || *p == '-'; p++ )
		;
	if ( p == host || *host == '.' || *host == '-' )
		return qfalse;
	if ( *p == ':' ) {
		const char *port = ++p;

		for ( ; UPD_IsDigit( *p ); p++ )
			;
		if ( p == port || p - port > 5 )
			return qfalse;
	}
	if ( *p && *p != '/' )
		return qfalse;
	// the folder, if there is one. The names of the files are added behind it with a slash.
	for ( ; *p; p++ ) {
		if ( *p == '/' ) {
			if ( p[1] == '/' || p[1] == '.' )
				return qfalse;
		} else if ( !UPD_IsAlnum( *p ) && *p != '.' && *p != '-' && *p != '_' ) {
			return qfalse;
		}
	}
	return ( address[len - 1] != '/' ) ? qtrue : qfalse;
}

// one hex digit as a number, -1: it is none
static int UPD_Hex( char c ) {
	if ( UPD_IsDigit( c ) )
		return c - '0';
	if ( c >= 'a' && c <= 'f' )
		return c - 'a' + 10;
	return -1;
}

// UPD_ParseList without the emptying of the list it refuses
static const char *UPD_ParseLines( const char *text, size_t length, updList_t *list ) {
	static const char	head[] = "visionjk-update ";
	const char			*end = text + length, *line = text;
	qboolean			first = qtrue, versionSeen = qfalse;

	memset( list, 0, sizeof( *list ) );
	if ( length > UPD_MAX_LIST )
		return "it is too long";

	while ( line < end ) {
		const char	*next = (const char *)memchr( line, '\n', (size_t)( end - line ) );
		const char	*stop = next ? next : end;
		qboolean	keep;

		next = next ? next + 1 : end;
		if ( stop > line && stop[-1] == '\r' )
			stop--;

		if ( first ) {
			long long format;

			first = qfalse;
			if ( (size_t)( stop - line ) <= sizeof( head ) - 1 || memcmp( line, head, sizeof( head ) - 1 )
				|| UPD_Number( line + sizeof( head ) - 1, stop, 9999, &format ) != stop )
				return "it is no file list of VisionJK";
			if ( format != UPD_FORMAT )
				return "it is of a newer kind than this version of the game reads";
		} else if ( stop - line > 8 && !memcmp( line, "version ", 8 ) ) {
			char number[32];

			if ( versionSeen || (size_t)( stop - line - 8 ) >= sizeof( number ) )
				return "its version line is not one";
			memcpy( number, line + 8, (size_t)( stop - line - 8 ) );
			number[stop - line - 8] = '\0';
			if ( !UPD_ParseVersion( number, list->version ) )
				return "its version line is not one";
			versionSeen = qtrue;
		} else if ( stop - line > 5 && ( ( keep = (qboolean)!memcmp( line, "keep ", 5 ) ) || !memcmp( line, "file ", 5 ) ) ) {
			updFile_t	*file;
			const char	*p;
			int			i;

			if ( list->numFiles >= UPD_MAX_FILES )
				return "it names more files than a release has";
			file = &list->files[list->numFiles];
			file->keep = keep;

			p = UPD_Number( line + 5, stop, UPD_MAX_FILE_BYTES, &file->bytes );
			if ( !p || stop - p < 1 + WEB_SHA256_SIZE * 2 + 1 + 1 || *p++ != ' ' )
				return "a line of it has no size";
			for ( i = 0; i < WEB_SHA256_SIZE; i++, p += 2 ) {
				const int high = UPD_Hex( p[0] ), low = UPD_Hex( p[1] );

				if ( high < 0 || low < 0 )
					return "a line of it has no checksum";
				file->sha256[i] = (unsigned char)( high * 16 + low );
			}
			if ( *p++ != ' ' || (size_t)( stop - p ) >= sizeof( file->path ) )
				return "a line of it has no name of a file";
			memcpy( file->path, p, (size_t)( stop - p ) );
			file->path[stop - p] = '\0';
			// (a zero byte inside the name makes it shorter than the line, which is no name either)
			if ( strlen( file->path ) != (size_t)( stop - p ) || !UPD_PathAllowed( file->path ) )
				return "it names a file this game does not touch";
			for ( i = 0; i < list->numFiles; i++ ) {
				if ( !strcmp( list->files[i].path, file->path ) )
					return "it names a file twice";
			}
			list->numFiles++;
		}
		// (any other line: a later release says more than this client knows)
		line = next;
	}
	if ( first )
		return "it is empty";
	if ( !versionSeen )
		return "it has no version";
	if ( !list->numFiles )
		return "it names no files";
	return NULL;
}

/*
=================
UPD_ParseList

A file list of the website. Returns NULL and the list, or what is wrong with the text, in words for a
console. A list is taken as a whole or not at all: one line that is not as it has to be and nothing of it is
used, the list is left empty.
=================
*/
const char *UPD_ParseList( const char *text, size_t length, updList_t *list ) {
	const char *wrong = UPD_ParseLines( text, length, list );

	if ( wrong )
		memset( list, 0, sizeof( *list ) );
	return wrong;
}
