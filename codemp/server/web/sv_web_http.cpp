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

// sv_web_http.cpp -- what the web host answers a request with: the files of the page, the manifest of the
// pk3 files and the pk3 files themselves. Only GET and HEAD, no request has a body. The request for /net is
// the one that makes a WebSocket of its connection (sv_web_ws.cpp).
//
// A file is never held in memory. It is opened when its answer begins and read in pieces as the socket
// takes them (SV_WebBodyMore), with fopen and not with a file of the file system of the engine: every change
// of map restarts that (SV_SpawnServer), which closes all of its files and would end a download halfway.

#include "server/web/sv_web_local.h"

#include <sys/stat.h>

// the headers of every answer that is a file of the page. The files change with every build of the page, so
// a browser asks each time.
#define SV_WEB_PAGE_HEADERS		"Cache-Control: no-cache\r\n"

static const struct {
	const char	*extension;
	const char	*type;
} webTypes[] = {
	{ "html",	"text/html; charset=utf-8" },
	{ "js",		"text/javascript; charset=utf-8" },
	{ "mjs",	"text/javascript; charset=utf-8" },
	{ "wasm",	"application/wasm" },			// a browser only compiles it while it loads under this type
	{ "css",	"text/css; charset=utf-8" },
	{ "json",	"application/json" },
	{ "txt",	"text/plain; charset=utf-8" },
	{ "svg",	"image/svg+xml" },
	{ "png",	"image/png" },
	{ "jpg",	"image/jpeg" },
	{ "ico",	"image/x-icon" },
	{ "pk3",	"application/octet-stream" },
};

// The manifest as it is sent. It is put together when it is asked for and older than SV_WEB_MANIFEST_MSEC:
// the search path changes with the map, and asking for it again and again costs no more than that.
// A pk3 file is at most "{...}," around a path of MAX_QPATH that is all escapes, a size and a checksum.
#define SV_WEB_MANIFEST_SIZE	( 2048 + SV_WEB_MAX_PACKS * ( 2 * MAX_QPATH + 64 ) )

static char			manifestText[SV_WEB_MANIFEST_SIZE];
static int			manifestLen;
static int			manifestGen;			// counts the texts that differed from the one before
static int			manifestTime;			// Sys_Milliseconds() of the last one
static qboolean		manifestBuilt;
static qboolean		manifestCut;			// it did not fit
static fsPakInfo_t	webPaks[SV_WEB_MAX_PACKS];

/*
==============================================================================

What a request is made of

==============================================================================
*/

/*
==================
SV_WebRangeNumber

The digits at *s. qfalse: there are none. A number above every length a file has stays one and does not wrap.
==================
*/
static qboolean SV_WebRangeNumber( const char **s, int64_t *value ) {
	const int64_t	most = (int64_t)1 << 62;
	qboolean		digits = qfalse;
	int64_t			v = 0;

	for ( ; **s >= '0' && **s <= '9'; ( *s )++ ) {
		digits = qtrue;
		v = ( v < most / 10 ) ? v * 10 + ( **s - '0' ) : most;
	}
	*value = v;
	return digits;
}

/*
==================
SV_WebParseRange

The value of a Range header for a body of size bytes: bytes=first-last, bytes=first- or bytes=-count (the
last count bytes).

 1: *first and *last are the part that is asked for, inside of the body
 0: no range this host answers (another unit, several ranges, not well formed): the whole body is the answer
-1: nothing of it is in the body (416)
==================
*/
int SV_WebParseRange( const char *value, int64_t size, int64_t *first, int64_t *last ) {
	const char	*s = value;
	int64_t		from, to;
	qboolean	hasFrom, hasTo;

	if ( Q_stricmpn( s, "bytes", 5 ) ) {
		return 0;
	}
	for ( s += 5; *s == ' '; s++ ) {
	}
	if ( *s != '=' ) {
		return 0;
	}
	for ( s++; *s == ' '; s++ ) {
	}

	hasFrom = SV_WebRangeNumber( &s, &from );
	if ( *s != '-' ) {
		return 0;
	}
	s++;
	hasTo = SV_WebRangeNumber( &s, &to );
	for ( ; *s == ' ' || *s == '\t'; s++ ) {
	}
	// something behind it, a second range for one: the whole body, which is always a right answer
	if ( *s ) {
		return 0;
	}

	if ( !hasFrom ) {
		if ( !hasTo ) {
			return 0;
		}
		if ( to == 0 || size <= 0 ) {
			return -1;
		}
		*first = ( to >= size ) ? 0 : size - to;
		*last = size - 1;
		return 1;
	}

	if ( hasTo && to < from ) {
		return 0;
	}
	if ( from >= size ) {
		return -1;
	}
	*first = from;
	*last = ( hasTo && to < size ) ? to : size - 1;
	return 1;
}

/*
==================
SV_WebDecodePath

The path of the address in a request: without what follows a ?, and %xx as the character it stands for.
qfalse: it is none this host has. That is every address that does not begin with a slash, that is too long,
or that has anything in it but the visible characters of ASCII. No file of the page and no pk3 file of the
manifest is named with another one, and so no zero and no control character ever gets into a file name.
==================
*/
qboolean SV_WebDecodePath( const char *target, char *out, int outSize ) {
	int len = 0;

	if ( target[0] != '/' ) {
		return qfalse;
	}

	for ( ; *target && *target != '?' && *target != '#'; target++ ) {
		int ch = (unsigned char)*target;

		if ( ch == '%' ) {
			int i;

			ch = 0;
			for ( i = 1; i <= 2; i++ ) {
				const int digit = target[i];

				if ( digit >= '0' && digit <= '9' ) {
					ch = ch * 16 + ( digit - '0' );
				} else if ( digit >= 'a' && digit <= 'f' ) {
					ch = ch * 16 + ( digit - 'a' + 10 );
				} else if ( digit >= 'A' && digit <= 'F' ) {
					ch = ch * 16 + ( digit - 'A' + 10 );
				} else {
					return qfalse;		// also the end of the text
				}
			}
			target += 2;
		}

		if ( ch < 0x20 || ch > 0x7E || len >= outSize - 1 ) {
			return qfalse;
		}
		out[len++] = (char)ch;
	}
	out[len] = '\0';
	return qtrue;
}

/*
==================
SV_WebRequestLine

"GET /path HTTP/1.1": its three parts. Returns 0, or the status the request is refused with.
==================
*/
static int SV_WebRequestLine( const char *head, char *method, int methodSize, char *target, int targetSize, int *minor ) {
	const char	*end = strstr( head, "\r\n" );
	const char	*space, *version;

	if ( !end ) {
		end = head + strlen( head );
	}

	space = (const char *)memchr( head, ' ', end - head );
	if ( !space || space == head || space - head >= methodSize ) {
		return 400;
	}
	Q_strncpyz( method, head, (int)( space - head ) + 1 );

	head = space + 1;
	space = (const char *)memchr( head, ' ', end - head );
	if ( !space || space == head ) {
		return 400;
	}
	if ( space - head >= targetSize ) {
		return 414;
	}
	Q_strncpyz( target, head, (int)( space - head ) + 1 );

	// HTTP/1.0 or HTTP/1.1, and the line ends with it
	version = space + 1;
	if ( end - version != 8 || strncmp( version, "HTTP/1.", 7 ) || ( version[7] != '0' && version[7] != '1' ) ) {
		return 400;
	}
	*minor = version[7] - '0';
	return 0;
}

/*
==============================================================================

Answers

==============================================================================
*/

/*
==================
SV_WebHead

The head of an answer whose body has length bytes, into the out buffer. Nothing waits in there: a connection
reads no request while it answers one.
==================
*/
static void SV_WebHead( webConnection_t *c, int status, const char *statusText, const char *type, int64_t length,
	const char *headers ) {
	char	head[1024];
	byte	*out;
	int		len;

	Com_sprintf( head, sizeof( head ),
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %lld\r\n"
		"Connection: %s\r\n"
		"X-Content-Type-Options: nosniff\r\n"
		// nothing of this host is a resource of the page of another site
		"Cross-Origin-Resource-Policy: same-origin\r\n"
		"%s"
		"\r\n",
		status, statusText, type, (long long)length, c->keepAlive ? "keep-alive" : "close", headers );

	len = (int)strlen( head );
	c->state = WEB_RESPONSE;
	c->lastActive = Sys_Milliseconds();
	if ( !( out = SV_WebOutRoom( c, len ) ) ) {
		SV_WebDrop( c );
		return;
	}
	memcpy( out, head, len );
	c->outLen += len;
}

/*
==================
SV_WebError

An answer that is nothing but its status
==================
*/
void SV_WebError( webConnection_t *c, int status, const char *statusText, const char *headers ) {
	char	text[128], all[512];
	byte	*out;
	int		len;

	// A request that was not understood may not have ended where this host thinks it did. Nothing more is read
	// from that connection. The same goes for one this host has no time for.
	if ( status == 400 || status == 405 || status == 414 || status == 431 || status == 500 || status == 503 ) {
		c->keepAlive = qfalse;
	}

	Com_sprintf( text, sizeof( text ), "%d %s\n", status, statusText );
	Com_sprintf( all, sizeof( all ), "Cache-Control: no-store\r\n%s", headers );
	len = (int)strlen( text );
	SV_WebHead( c, status, statusText, "text/plain; charset=utf-8", len, all );

	if ( !c->headOnly && ( out = SV_WebOutRoom( c, len ) ) != NULL ) {
		memcpy( out, text, len );
		c->outLen += len;
	}
}

/*
==================
SV_WebFileSize

The length of a file, -1 if there is none. Only of what is a file: a folder is none, and on Windows neither is
the name of a device (CON, NUL), which is asked about here so that it is never opened.
==================
*/
static int64_t SV_WebFileSize( const char *osPath ) {
	struct stat info;

	if ( stat( osPath, &info ) != 0 || !S_ISREG( info.st_mode ) || info.st_size < 0 ) {
		return -1;
	}
	return info.st_size;
}

/*
==================
SV_WebOpen

A file to hand out, and its length. Should the file get shorter behind this, the answer ends where the file
does and its connection with it (SV_WebBodyMore).
==================
*/
static FILE *SV_WebOpen( const char *osPath, int64_t *size ) {
	*size = SV_WebFileSize( osPath );
	if ( *size < 0 ) {
		return NULL;
	}
	return fopen( osPath, "rb" );
}

/*
==================
SV_WebSendFile

Answers with a file that is open, or with the part of it a Range header asks for (range is its value, NULL
if the file is only handed out whole)
==================
*/
static void SV_WebSendFile( webConnection_t *c, FILE *f, int64_t size, const char *type, const char *headers,
	const char *range ) {
	int64_t	first = 0, last = size - 1;
	char	all[512];
	int		part = range ? SV_WebParseRange( range, size, &first, &last ) : 0;

	if ( part < 0 ) {
		fclose( f );
		Com_sprintf( all, sizeof( all ), "Content-Range: bytes */%lld\r\n", (long long)size );
		SV_WebError( c, 416, "Range Not Satisfiable", all );
		return;
	}

	if ( part > 0 ) {
		// (a file whose length fseek can not count to was not opened on that system in the first place)
		if ( (long)first != first || fseek( f, (long)first, SEEK_SET ) != 0 ) {
			fclose( f );
			SV_WebError( c, 500, "Internal Server Error", "" );
			return;
		}
		Com_sprintf( all, sizeof( all ), "%sContent-Range: bytes %lld-%lld/%lld\r\n", headers,
			(long long)first, (long long)last, (long long)size );
		SV_WebHead( c, 206, "Partial Content", type, last - first + 1, all );
	} else {
		SV_WebHead( c, 200, "OK", type, size, headers );
	}

	if ( c->headOnly || last < first ) {
		fclose( f );
		return;
	}
	c->body = WEB_BODY_FILE;
	c->file = f;
	c->bodyLeft = last - first + 1;
}

/*
==================
SV_WebBusy

qtrue, and the answer that says so, if as many files are on their way as this host hands out at once
==================
*/
static qboolean SV_WebBusy( webConnection_t *c ) {
	if ( c->headOnly || SV_WebTransfers() < SV_WEB_MAX_TRANSFERS ) {
		return qfalse;
	}
	SV_WebError( c, 503, "Service Unavailable", "Retry-After: 2\r\n" );
	return qtrue;
}

/*
==================
SV_WebBodyMore

The next piece of the body of an answer, at most max bytes, into the out buffer, which is empty. Returns its
length, 0 if there is none to be had.
==================
*/
int SV_WebBodyMore( webConnection_t *c, int max ) {
	int len = SV_WEB_OUT_SIZE;

	if ( len > max ) {
		len = max;
	}
	if ( len > c->bodyLeft ) {
		len = (int)c->bodyLeft;
	}
	if ( len <= 0 || c->outLen ) {
		return 0;
	}

	if ( c->body == WEB_BODY_FILE ) {
		// (shorter than it was when its length was sent: there is no right end for that answer)
		if ( (int)fread( c->out, 1, len, c->file ) != len ) {
			return 0;
		}
	} else if ( c->body == WEB_BODY_MANIFEST ) {
		// another text by now, of which this is not the rest
		if ( c->bodyGen != manifestGen || c->bodyAt < 0 || len > manifestLen - c->bodyAt ) {
			return 0;
		}
		memcpy( c->out, manifestText + c->bodyAt, len );
		c->bodyAt += len;
	} else {
		return 0;
	}

	c->outLen = len;
	c->outSent = 0;
	c->bodyLeft -= len;
	return len;
}

/*
==================
SV_WebBodyEnd
==================
*/
void SV_WebBodyEnd( webConnection_t *c ) {
	if ( c->file ) {
		fclose( c->file );
		c->file = NULL;
	}
	c->body = WEB_BODY_NONE;
	c->bodyLeft = 0;
}

/*
==============================================================================

The page

==============================================================================
*/

/*
==================
SV_WebDeviceName

qtrue: the name is one Windows has for a device, in every folder and with any extension behind it: CON, PRN,
AUX, NUL, COM0 to COM9, LPT0 to LPT9. No file of the page has such a name on any system.
(SV_WebFileSize does not keep them out for sure: the older C library of Windows says they are files.)
==================
*/
static qboolean SV_WebDeviceName( const char *name ) {
	static const char *const devices[] = { "CON", "PRN", "AUX", "NUL", "COM", "LPT" };
	const char	*dot = strchr( name, '.' );
	const int	len = dot ? (int)( dot - name ) : (int)strlen( name );
	size_t		i;

	for ( i = 0; i < ARRAY_LEN( devices ); i++ ) {
		const qboolean numbered = (qboolean)( i >= 4 );

		if ( len == ( numbered ? 4 : 3 ) && !Q_stricmpn( name, devices[i], 3 )
			&& ( !numbered || ( name[3] >= '0' && name[3] <= '9' ) ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
==================
SV_WebSendPage

A file of the folder sv_webRoot. Only one that is in the folder itself: its name is made of letters, digits,
dot, dash and underscore, which leaves no way to name a folder, another drive or the folder above, and it
does not begin with a dot. There is no list of the folder.
==================
*/
static void SV_WebSendPage( webConnection_t *c, const char *name ) {
	const char	*root = sv_webRoot->string;
	const char	*base = Cvar_VariableString( "fs_basepath" );
	const char	*extension = COM_GetExtension( name );
	const char	*type = "application/octet-stream";
	const char	*s;
	char		osPath[MAX_OSPATH];
	int64_t		size;
	size_t		i;
	FILE		*f;

	for ( s = name; *s; s++ ) {
		if ( !( ( *s >= 'a' && *s <= 'z' ) || ( *s >= 'A' && *s <= 'Z' ) || ( *s >= '0' && *s <= '9' )
			|| *s == '.' || *s == '-' || *s == '_' ) ) {
			break;
		}
	}
	// (a path that does not fit would be cut, and what is left of it is the name of another file)
	if ( *s || !name[0] || name[0] == '.' || strstr( name, ".." ) || s - name >= MAX_QPATH || !root[0]
		|| SV_WebDeviceName( name ) || strlen( base ) + strlen( root ) + strlen( name ) + 3 > sizeof( osPath ) ) {
		SV_WebError( c, 404, "Not Found", "" );
		return;
	}
	if ( SV_WebBusy( c ) ) {
		return;
	}

	// a full path is taken as it is, anything else is a folder of the installation
	if ( root[0] == '/' || root[0] == '\\' || ( root[0] && root[1] == ':' ) ) {
		Com_sprintf( osPath, sizeof( osPath ), "%s%c%s", root, PATH_SEP, name );
	} else {
		Q_strncpyz( osPath, FS_BuildOSPath( base, root, name ), sizeof( osPath ) );
	}

	if ( !( f = SV_WebOpen( osPath, &size ) ) ) {
		SV_WebError( c, 404, "Not Found", "" );
		return;
	}

	for ( i = 0; i < ARRAY_LEN( webTypes ); i++ ) {
		if ( !Q_stricmp( extension, webTypes[i].extension ) ) {
			type = webTypes[i].type;
			break;
		}
	}
	SV_WebSendFile( c, f, size, type, SV_WEB_PAGE_HEADERS, NULL );
}

/*
==============================================================================

The pk3 files

==============================================================================
*/

/*
==================
SV_WebPakPath

The path a pk3 file of the search path has in the manifest and in /pk3/: its game folder and its name.
qfalse: it has none and is not handed out. That is a path longer than the game has them (as much as the
download over UDP takes), and one with a character that is not a visible one of ASCII: nothing says which
character a byte above that is, and the address of a request could not name it for sure. A backslash is
none either: the page makes a file of the path and takes it for the end of a folder.
==================
*/
static qboolean SV_WebPakPath( const fsPakInfo_t *pak, char *path, int size ) {
	const char *s;

	// (asked first: Com_sprintf would cut it, and half a name is another name)
	if ( strlen( pak->gameDir ) + strlen( pak->baseName ) + sizeof( "/.pk3" ) > (size_t)size ) {
		return qfalse;
	}
	Com_sprintf( path, size, "%s/%s.pk3", pak->gameDir, pak->baseName );

	for ( s = path; *s; s++ ) {
		if ( *s < 0x20 || *s > 0x7E || *s == '\\' ) {
			return qfalse;
		}
	}
	return qtrue;
}

/*
==================
SV_WebManifestAdd
==================
*/
static void QDECL SV_WebManifestAdd( const char *fmt, ... ) {
	const int	room = (int)sizeof( manifestText ) - manifestLen;
	va_list		argptr;
	int			len;

	if ( manifestCut ) {
		return;
	}

	va_start( argptr, fmt );
	len = Q_vsnprintf( manifestText + manifestLen, room, fmt, argptr );
	va_end( argptr );

	if ( len < 0 || len >= room ) {
		manifestText[manifestLen] = '\0';
		manifestCut = qtrue;
		return;
	}
	manifestLen += len;
}

// a text as JSON has it
static void SV_WebManifestString( const char *s ) {
	SV_WebManifestAdd( "\"" );
	for ( ; *s; s++ ) {
		const int ch = (unsigned char)*s;

		if ( ch == '"' || ch == '\\' ) {
			SV_WebManifestAdd( "\\%c", ch );
		} else if ( ch < 0x20 || ch > 0x7E ) {
			SV_WebManifestAdd( "\\u%04x", ch );
		} else {
			SV_WebManifestAdd( "%c", ch );
		}
	}
	SV_WebManifestAdd( "\"" );
}

/*
==================
SV_WebManifestBuild

{"game":"<fs_game>","packs":[{"path":"base/assets0.pk3","size":561806019,"checksum":123456789},...]}

One entry for every pk3 file of the search path, in its order, which is the order the server looks things up
in. The checksum is the one of the engine (what sv_paks has), as a number without a sign. A file that is
found a second time under the same name is not listed again: the first one is the one the server reads.
==================
*/
static void SV_WebManifestBuild( void ) {
	const uint32_t	oldSum = Com_BlockChecksum( manifestText, manifestLen );
	const int		oldLen = manifestLen;
	const int		count = FS_PakInfo( webPaks, SV_WEB_MAX_PACKS );
	int				i, j, listed = 0, unnamed = 0;

	manifestLen = 0;
	manifestCut = qfalse;
	manifestTime = Sys_Milliseconds();

	SV_WebManifestAdd( "{\"game\":" );
	SV_WebManifestString( Cvar_VariableString( "fs_game" ) );
	SV_WebManifestAdd( ",\"packs\":[" );

	for ( i = 0; i < count; i++ ) {
		char	path[MAX_QPATH];
		int64_t	size;

		if ( !SV_WebPakPath( &webPaks[i], path, sizeof( path ) ) ) {
			Com_DPrintf( "web host: %s is not in the manifest\n", webPaks[i].fileName );
			unnamed++;
			continue;
		}
		for ( j = 0; j < i; j++ ) {
			if ( !Q_stricmp( webPaks[j].gameDir, webPaks[i].gameDir ) && !Q_stricmp( webPaks[j].baseName, webPaks[i].baseName ) ) {
				break;
			}
		}
		if ( j < i || ( size = SV_WebFileSize( webPaks[i].fileName ) ) < 0 ) {
			continue;
		}

		SV_WebManifestAdd( listed ? ",{\"path\":" : "{\"path\":" );
		SV_WebManifestString( path );
		SV_WebManifestAdd( ",\"size\":%lld,\"checksum\":%u}", (long long)size, (unsigned int)webPaks[i].checksum );
		listed++;
	}
	SV_WebManifestAdd( "]}" );

	if ( manifestCut ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: web host: the manifest has no room for %d pk3 files\n", count );
	}
	if ( !manifestBuilt || manifestLen != oldLen || Com_BlockChecksum( manifestText, manifestLen ) != oldSum ) {
		manifestGen++;
		// (said once for a list, not every time it is put together)
		if ( unnamed ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: web host: %d pk3 files are not handed out, their names are too long or have characters "
				"a page can not ask for (developer 1 names them)\n", unnamed );
		}
	}
	manifestBuilt = qtrue;
}

/*
==================
SV_WebSendManifest
==================
*/
static void SV_WebSendManifest( webConnection_t *c ) {
	const int age = Sys_Milliseconds() - manifestTime;

	if ( !manifestBuilt || age < 0 || age > SV_WEB_MANIFEST_MSEC ) {
		SV_WebManifestBuild();
	}
	if ( manifestCut ) {
		SV_WebError( c, 500, "Internal Server Error", "" );
		return;
	}

	SV_WebHead( c, 200, "OK", "application/json", manifestLen, "Cache-Control: no-cache\r\n" );
	if ( !c->headOnly ) {
		c->body = WEB_BODY_MANIFEST;
		c->bodyLeft = manifestLen;
		c->bodyAt = 0;
		c->bodyGen = manifestGen;
	}
}

/*
==================
SV_WebSendPak

The pk3 file with that path in the manifest, and no other file: the name in the request is only compared
with the search path, the file that is opened is the one the search path has. The first file of that name
is the one the manifest lists, and it is handed out under the name as it is written there.
==================
*/
static void SV_WebSendPak( webConnection_t *c, const char *head, const char *name ) {
	const int	count = FS_PakInfo( webPaks, SV_WEB_MAX_PACKS );
	char		path[MAX_QPATH], range[128];
	int64_t		size;
	FILE		*f;
	int			i;

	for ( i = 0; i < count; i++ ) {
		if ( SV_WebPakPath( &webPaks[i], path, sizeof( path ) ) && !Q_stricmp( path, name ) ) {
			break;
		}
	}
	if ( i == count || strcmp( path, name ) ) {
		SV_WebError( c, 404, "Not Found", "" );
		return;
	}
	if ( SV_WebBusy( c ) ) {
		return;
	}
	if ( !( f = SV_WebOpen( webPaks[i].fileName, &size ) ) ) {
		SV_WebError( c, 404, "Not Found", "" );
		return;
	}

	// A part of the file only if nothing is tied to it: If-Range asks for the part of one version of the
	// file, and this host tells no versions apart, so the whole file is the answer then.
	// (and a value that fills the buffer was cut, which makes another range of it)
	if ( !Web_HeaderValue( head, "Range", range, sizeof( range ) ) || strlen( range ) >= sizeof( range ) - 1
		|| Web_FindHeader( head, "If-Range" ) ) {
		range[0] = '\0';
	}

	// (no-store: the page keeps the files itself and tells them by their checksums, the cache of the browser
	// is not to hold a second copy of 650 MB)
	SV_WebSendFile( c, f, size, "application/octet-stream", "Cache-Control: no-store\r\nAccept-Ranges: bytes\r\n", range );
}

/*
==============================================================================

A request

==============================================================================
*/

/*
==================
SV_WebHttpRequest

The head of a request is in the in buffer, headLen bytes with its empty line. Whatever it says, this leaves
the connection with an answer to send.
==================
*/
void SV_WebHttpRequest( webConnection_t *c, int headLen ) {
	char		*head = (char *)c->in;
	char		method[8], target[SV_WEB_MAX_TARGET], path[SV_WEB_MAX_TARGET], value[64];
	const char	*length;
	const int	behind = c->inLen - headLen;
	int			status, minor = 0;

	// a zero in place of the empty line: the head is a text from here on, which the header functions read
	head[headLen - 4] = '\0';
	c->keepAlive = qfalse;
	c->headOnly = qfalse;

	// (a zero inside of it would hide the lines behind it from every check that follows)
	if ( (int)strlen( head ) != headLen - 4 ) {
		SV_WebError( c, 400, "Bad Request", "" );
		return;
	}
	if ( ( status = SV_WebRequestLine( head, method, sizeof( method ), target, sizeof( target ), &minor ) ) != 0 ) {
		SV_WebError( c, status, ( status == 414 ) ? "URI Too Long" : "Bad Request", "" );
		return;
	}

	c->headOnly = (qboolean)!strcmp( method, "HEAD" );
	if ( !c->headOnly && strcmp( method, "GET" ) ) {
		SV_WebError( c, 405, "Method Not Allowed", "Allow: GET, HEAD\r\n" );
		return;
	}

	// No request to this host has a body. One that says it has is refused and its connection closed: the
	// body would be read as the next request.
	length = Web_FindHeader( head, "Content-Length" );
	if ( Web_FindHeader( head, "Transfer-Encoding" ) || ( length && Web_ParseLength( length, 0 ) != 0 ) ) {
		SV_WebError( c, 400, "Bad Request", "" );
		return;
	}

	if ( !SV_WebDecodePath( target, path, sizeof( path ) ) ) {
		SV_WebError( c, 404, "Not Found", "" );
		return;
	}

	if ( !strcmp( path, "/net" ) ) {
		SV_WebUpgrade( c, head, headLen, minor );
		return;
	}

	// HTTP/1.1 goes on with the next request unless it says it does not. Bytes behind the head are a second
	// request that did not wait for the answer to the first, which no browser sends: it is not read, the
	// connection ends with this answer.
	c->keepAlive = (qboolean)( minor >= 1 && !behind
		&& !( Web_HeaderValue( head, "Connection", value, sizeof( value ) ) && Q_stristr( value, "close" ) ) );

	if ( !strcmp( path, "/manifest.json" ) ) {
		SV_WebSendManifest( c );
	} else if ( !strncmp( path, "/pk3/", 5 ) ) {
		SV_WebSendPak( c, head, path + 5 );
	} else if ( !strcmp( path, "/" ) ) {
		SV_WebSendPage( c, "index.html" );
	} else {
		SV_WebSendPage( c, path + 1 );
	}
}
