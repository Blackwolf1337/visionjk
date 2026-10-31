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

// web_models.c -- the player models of the model store of the site. See web_models.h.

#include "web_models.h"

#include <stdio.h>
#include <string.h>

#define WEB_MODEL_PATH		160			// longest file name the store takes (MAX_PATH of modelstore.py)

static qboolean WEB_ModelNameChar( int c ) {
	return (qboolean)( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' );
}

static int WEB_ModelLower( int c ) {
	return ( c >= 'A' && c <= 'Z' ) ? c + ( 'a' - 'A' ) : c;
}

qboolean WEB_ModelName( const char *name ) {
	size_t i;

	for ( i = 0; name[i]; i++ ) {
		if ( i >= WEB_MODEL_NAME - 1 || !WEB_ModelNameChar( name[i] ) ) {
			return qfalse;
		}
	}
	return (qboolean)( i >= 2 );
}

qboolean WEB_ModelFromInfo( const char *value, char *out, size_t outSize ) {
	size_t i;

	if ( outSize < WEB_MODEL_NAME ) {
		return qfalse;
	}
	for ( i = 0; value[i] && value[i] != '/'; i++ ) {
		if ( i >= WEB_MODEL_NAME - 1 ) {
			return qfalse;
		}
		out[i] = (char)WEB_ModelLower( (unsigned char)value[i] );
	}
	out[i] = '\0';
	return WEB_ModelName( out );
}

// the part of a lower-case path after prefix, NULL: it does not begin with it
static const char *WEB_ModelAfter( const char *path, const char *prefix ) {
	const size_t len = strlen( prefix );

	return strncmp( path, prefix, len ) ? NULL : path + len;
}

static qboolean WEB_ModelEndsWith( const char *path, const char *end ) {
	const size_t len = strlen( path ), endLen = strlen( end );

	return (qboolean)( len >= endLen && !strcmp( path + len - endLen, end ) );
}

qboolean WEB_ModelEntryAllowed( const char *entry, const char *model ) {
	static const char *const modelFiles[] = { ".glm", ".gla", ".skin", ".jpg", ".jpeg", ".png", ".tga", NULL };
	static const char *const configs[] = { "sounds.cfg", "animevents.cfg", "animation.cfg", NULL };
	char		path[WEB_MODEL_PATH + 1], prefix[WEB_MODEL_NAME + 32];
	const char	*rest, *part;
	size_t		i;

	if ( !WEB_ModelName( model ) ) {
		return qfalse;
	}
	for ( i = 0; entry[i]; i++ ) {
		const unsigned char c = (unsigned char)entry[i];

		if ( i >= WEB_MODEL_PATH || c < 0x20 || c == 0x7f || c == '"' || c == '\'' || c == ';' || c == ':' || c == '\\' ) {
			return qfalse;
		}
		path[i] = (char)WEB_ModelLower( c );
	}
	path[i] = '\0';
	if ( !path[0] || path[0] == '/' || path[i - 1] == '/' ) {
		return qfalse;
	}
	// every part of the path a name: no empty one, no . or ..
	for ( part = path; part; part = strchr( part, '/' ) ? strchr( part, '/' ) + 1 : NULL ) {
		const size_t len = strchr( part, '/' ) ? (size_t)( strchr( part, '/' ) - part ) : strlen( part );

		if ( !len || ( len == 1 && part[0] == '.' ) || ( len == 2 && part[0] == '.' && part[1] == '.' ) ) {
			return qfalse;
		}
	}

	// a readme at the top
	if ( !strchr( path, '/' ) ) {
		return WEB_ModelEndsWith( path, ".txt" );
	}
	snprintf( prefix, sizeof( prefix ), "shaders/%s.shader", model );
	if ( !strcmp( path, prefix ) ) {
		return qtrue;
	}
	snprintf( prefix, sizeof( prefix ), "models/players/%s/", model );
	if ( ( rest = WEB_ModelAfter( path, prefix ) ) != NULL ) {
		for ( i = 0; modelFiles[i]; i++ ) {
			if ( WEB_ModelEndsWith( rest, modelFiles[i] ) ) {
				return qtrue;
			}
		}
		for ( i = 0; configs[i]; i++ ) {
			if ( !strcmp( rest, configs[i] ) ) {
				return qtrue;
			}
		}
		return qfalse;
	}
	snprintf( prefix, sizeof( prefix ), "sound/chars/%s/", model );
	if ( ( rest = WEB_ModelAfter( path, prefix ) ) != NULL ) {
		return (qboolean)( WEB_ModelEndsWith( rest, ".wav" ) || WEB_ModelEndsWith( rest, ".mp3" ) );
	}
	return qfalse;
}

static int WEB_ModelHex( int c ) {
	if ( c >= '0' && c <= '9' ) return c - '0';
	if ( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
	if ( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
	return -1;
}

const char *WEB_ModelIndexNext( const char *text, char *name, char *icon, long long *bytes ) {
	unsigned char sha[WEB_SHA256_SIZE];

	while ( *text ) {
		const char	*end = strchr( text, '\n' ), *p = text;
		const char	*next = end ? end + 1 : text + strlen( text );
		size_t		len = 0;

		while ( WEB_ModelNameChar( (unsigned char)*p ) && len < WEB_MODEL_NAME - 1 ) {
			name[len++] = *p++;
		}
		name[len] = '\0';
		if ( *p == ' ' && WEB_ModelIndexFind( text, name, bytes, sha ) ) {
			// the fourth field: <name> <bytes> <64 hex> <icon>
			for ( len = 0; *p && *p != '\n' && len < 3; p++ ) {
				len += ( *p == ' ' );
			}
			icon[0] = '\0';
			if ( len == 3 && ( !strncmp( p, "jpg", 3 ) || !strncmp( p, "png", 3 ) ) ) {
				memcpy( icon, p, 3 );
				icon[3] = '\0';
			}
			return next;
		}
		text = next;
	}
	return NULL;
}

qboolean WEB_ModelIndexFind( const char *text, const char *model, long long *bytes, unsigned char sha256[WEB_SHA256_SIZE] ) {
	const size_t	nameLen = strlen( model );
	const char		*line;

	if ( !WEB_ModelName( model ) ) {
		return qfalse;
	}
	for ( line = text; *line; line = strchr( line, '\n' ) ? strchr( line, '\n' ) + 1 : line + strlen( line ) ) {
		const char	*p = line + nameLen;
		long long	size = 0;
		int			i, digits = 0;

		if ( strncmp( line, model, nameLen ) || *p != ' ' ) {
			continue;
		}
		for ( p++; *p >= '0' && *p <= '9' && digits < 10; p++, digits++ ) {
			size = size * 10 + ( *p - '0' );
		}
		if ( !digits || *p != ' ' || size <= 0 || size > WEB_MODEL_MOST ) {
			return qfalse;
		}
		for ( p++, i = 0; i < WEB_SHA256_SIZE; i++, p += 2 ) {
			const int hi = WEB_ModelHex( (unsigned char)p[0] ), lo = hi < 0 ? -1 : WEB_ModelHex( (unsigned char)p[1] );

			if ( lo < 0 ) {
				return qfalse;
			}
			sha256[i] = (unsigned char)( hi << 4 | lo );
		}
		// (after it the type of the icon, which the cgame reads, cgame/modelstore/cg_modelstore.c)
		if ( *p && *p != '\n' && *p != '\r' && *p != ' ' ) {
			return qfalse;
		}
		*bytes = size;
		return qtrue;
	}
	return qfalse;
}
