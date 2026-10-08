/*
===========================================================================
Copyright (C) 2013 - 2015, OpenJK contributors
Copyright (C) 2026, VisionJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
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

// wa_fs.c -- the file manager of the web admin panel and the server config: the real files of the game
// folder in the home path and in the installation, listed, read, written, renamed and deleted within limits.

#include "game/bg_net.h"		// the sockets, in front of everything else
#ifdef _WIN32
	#include <io.h>
	#include <direct.h>
	#include <sys/stat.h>
#else
	#include <sys/stat.h>
	#include <dirent.h>
#endif

#include "game/webadmin/wa_local.h"

/*
=================
WA_ApiConfigGet, WA_ApiConfigSet

Not in JKAdmin: the server config in the panel. It is this one file and no other, the request names none.

The engine looks for a file in the home path first (fs_homepath/<game>/server.cfg) and then in the base path,
and it only ever writes to the home path. So the first save puts the edited text into the home path, and that
is the file "exec server.cfg" takes from then on. The one in the base path, the installation, is not touched.
What was there before a save is kept as server.cfg.bak next to it.

The file has one byte per character like everything in the game (Latin-1), the page gets and sends UTF-8.
The passwords in it are shown as they are: who can log into the panel can do everything on the server anyway.
=================
*/
#define WA_CONFIG_FILE		"server.cfg"
#define WA_CONFIG_BACKUP	"server.cfg.bak"

void WA_ApiConfigGet( waConnection_t *c ) {
	waBuffer_t	b = { 0 };
	int			len = 0;
	char		*text = WA_ReadGameFile( WA_CONFIG_FILE, &len );

	WA_BufLatin1AsUtf8( &b, text, len );
	free( text );
	// no file yet is an empty text
	WA_Respond( c, 200, "OK", "text/plain; charset=utf-8", "Cache-Control: no-store\r\n", b.data, b.len );
	WA_BufFree( &b );
}

// text is the body of the request and is changed in place. run: also execute the file.
void WA_ApiConfigSet( waConnection_t *c, char *text, qboolean run ) {
	char		*old;
	int			oldLen = 0;
	const int	len = WA_TextBody( text );

	if ( !len ) {
		// an empty file is more likely a page that did not load the text than a wish
		WA_ActionResult( c, qfalse, "an empty config is not saved" );
		return;
	}

	old = WA_ReadGameFile( WA_CONFIG_FILE, &oldLen );
	if ( old ) {
		WA_WriteGameFile( WA_CONFIG_BACKUP, old, oldLen );
		free( old );
	}
	if ( !WA_WriteGameFile( WA_CONFIG_FILE, text, len ) ) {
		WA_ActionResult( c, qfalse, "could not write " WA_CONFIG_FILE );
		return;
	}
	// what is in it stays out of the log
	WA_LogUse( c, va( "config saved, %d bytes%s", len, run ? ", executed" : "" ) );
	if ( run )
		trap->SendConsoleCommand( EXEC_APPEND, "exec " WA_CONFIG_FILE "\n" );
	WA_ActionResult( c, qtrue, run ? "saved and executed" : "saved, not executed yet" );
}

/*
==============================================================================

File manager

Not in JKAdmin. Two folders and nothing else: the game folder in the home path of the server
(fs_homepath/<fs_game>: its logs, its stored config, what the panel saves; the engine looks there first), which
can be changed, and the game folder of the installation (fs_basepath/<fs_game>), which can only be read.

It works on the real files. The file system of the engine can not list a folder without what is in the pk3
files, and it can neither delete nor rename. So the limits the engine would set are set here:
- A path is checked letter by letter (WA_FsCleanPath): names of plain characters with / between them and none
  that begins with a dot. So there is no "..", no absolute path, no backslash and no drive letter.
- The system then resolves the path, and what comes out has to lie inside the folder (WA_FsResolve). A symbolic
  link is neither followed nor touched, so a link inside the folder does not lead out of it.
- Nothing is written, renamed or deleted that the engine or the system would run (WA_FsIsCode), the game modules
  above all: a module with another content would be loaded with the next map.
Every change and every file that is read goes to the security log with its path, never with its content.

==============================================================================
*/

#define WA_FS_MAX_PATH		160							// of a path inside a root, as the page names it
#define WA_FS_MAX_ENTRIES	4000						// of one folder
#define WA_FS_MAX_UPLOAD	( 512 * 1024 * 1024 )
#define WA_FS_PART			".part"						// a file that is still coming in

#ifdef _WIN32
	#define WA_FS_SEP( ch )	( ( ch ) == '/' || ( ch ) == '\\' )
#else
	#define WA_FS_SEP( ch )	( ( ch ) == '/' )
#endif

// A path as the page may name it, see above. "" is the root itself. out has room for WA_FS_MAX_PATH + 1.
static qboolean WA_FsCleanPath( const char *in, char *out, int outSize ) {
	qboolean	start = qtrue;		// at the first character of a name
	int			len = 0;

	if ( outSize <= WA_FS_MAX_PATH || strlen( in ) > WA_FS_MAX_PATH )
		return qfalse;
	for ( ; *in; in++ ) {
		const unsigned char ch = (unsigned char)*in;

		if ( ch == '/' ) {
			// no empty name, and none that ends with a dot or a blank (Windows drops those)
			if ( start || in[-1] == '.' || in[-1] == ' ' )
				return qfalse;
			start = qtrue;
		} else {
			if ( ch >= 0x80 || !( isalnum( ch ) || strchr( " _-.()+,", ch ) ) )
				return qfalse;
			if ( start && ( ch == '.' || ch == ' ' ) )
				return qfalse;
			start = qfalse;
		}
		out[len++] = (char)ch;
	}
	if ( len && ( start || out[len - 1] == '.' || out[len - 1] == ' ' ) )
		return qfalse;
	out[len] = '\0';
	return qtrue;
}

// What the engine or the system would run. An extension anywhere in the name counts (libx.so.1).
static qboolean WA_FsIsCode( const char *path ) {
	static const char *const code[] = { "so", "dll", "dylib", "exe", "qvm", "sh", "bat", "cmd", "com", "scr", "msi",
		"ps1", "vbs", "jar", "py", "pl", "php", "bin", "run", "appimage", "desktop" };
	const char *name = strrchr( path, '/' ), *dot;

	name = name ? name + 1 : path;
	for ( dot = strchr( name, '.' ); dot; dot = strchr( dot + 1, '.' ) ) {
		char	ext[16];
		int		n = 0;
		size_t	i;

		while ( dot[1 + n] && dot[1 + n] != '.' && n < (int)sizeof( ext ) - 1 ) {
			ext[n] = dot[1 + n];
			n++;
		}
		ext[n] = '\0';
		for ( i = 0; i < ARRAY_LEN( code ); i++ ) {
			if ( !Q_stricmp( ext, code[i] ) )
				return qtrue;
		}
	}
	return qfalse;
}

// the path as the system has it, without links and dots. qfalse: there is no such file.
static qboolean WA_FsRealPath( const char *path, char *out, int outSize ) {
	char real[MAX_OSPATH];
#ifdef _WIN32
	struct _stat st;

	if ( !_fullpath( real, path, sizeof( real ) ) || _stat( real, &st ) )
		return qfalse;
#else
	if ( !realpath( path, real ) )
		return qfalse;
#endif
	if ( (int)strlen( real ) >= outSize )
		return qfalse;
	Q_strncpyz( out, real, outSize );
	return qtrue;
}

static qboolean WA_FsIsLink( const char *path ) {
#ifdef _WIN32
	(void)path;
	return qfalse;		// junctions are not looked for
#else
	struct stat st;

	return (qboolean)( !lstat( path, &st ) && S_ISLNK( st.st_mode ) );
#endif
}

// qfalse: neither a file nor a folder. A link is looked at itself, not at what it points to.
qboolean WA_FsStat( const char *path, qboolean *isDir, double *size, double *mtime ) {
#ifdef _WIN32
	struct _stat64 st;

	if ( _stat64( path, &st ) || !( st.st_mode & ( _S_IFDIR | _S_IFREG ) ) )
		return qfalse;
	*isDir = (qboolean)( ( st.st_mode & _S_IFDIR ) != 0 );
#else
	struct stat st;

	if ( lstat( path, &st ) || !( S_ISDIR( st.st_mode ) || S_ISREG( st.st_mode ) ) )
		return qfalse;
	*isDir = (qboolean)S_ISDIR( st.st_mode );
#endif
	if ( size )
		*size = (double)st.st_size;
	if ( mtime )
		*mtime = (double)st.st_mtime;
	return qtrue;
}

static qboolean WA_FsInside( const char *path, const char *root ) {
	const size_t n = strlen( root );

#ifdef _WIN32
	if ( Q_stricmpn( path, root, (int)n ) )
#else
	if ( strncmp( path, root, n ) )
#endif
		return qfalse;
	return (qboolean)( path[n] == '\0' || WA_FS_SEP( path[n] ) );
}

// the folder of a root as the system has it
static qboolean WA_FsRoot( qboolean install, char *out, int outSize ) {
	char base[MAX_OSPATH], game[MAX_QPATH], name[WA_FS_MAX_PATH + 1], path[MAX_OSPATH];

	trap->Cvar_VariableStringBuffer( install ? "fs_basepath" : "fs_homepath", base, sizeof( base ) );
	trap->Cvar_VariableStringBuffer( "fs_game", game, sizeof( game ) );
	if ( !game[0] )
		Q_strncpyz( game, BASEGAME, sizeof( game ) );
	// the game folder is one name
	if ( !base[0] || strchr( game, '/' ) || !WA_FsCleanPath( game, name, sizeof( name ) )
		|| strlen( base ) + strlen( name ) + 2 > sizeof( path ) )
		return qfalse;
	Com_sprintf( path, sizeof( path ), "%s/%s", base, name );
	return WA_FsRealPath( path, out, outSize );
}

/*
=================
WA_FsResolve

The real file of a path inside a root. rel has been through WA_FsCleanPath(). With mayCreate the file itself
need not be there, the folder it is to be in has to. exists says which it is.
qfalse: not there, a symbolic link, or not inside the root once the system has resolved it.
=================
*/
qboolean WA_FsResolve( qboolean install, const char *rel, qboolean mayCreate, char *out, int outSize, qboolean *exists ) {
	char		root[MAX_OSPATH], full[MAX_OSPATH], real[MAX_OSPATH];
	char		*name;
	qboolean	isDir;

	*exists = qfalse;
	if ( !WA_FsRoot( install, root, sizeof( root ) ) || strlen( root ) + strlen( rel ) + 2 > sizeof( full ) )
		return qfalse;
	if ( !rel[0] ) {
		if ( (int)strlen( root ) >= outSize )
			return qfalse;
		*exists = qtrue;
		Q_strncpyz( out, root, outSize );
		return qtrue;
	}
	Com_sprintf( full, sizeof( full ), "%s/%s", root, rel );
	if ( WA_FsIsLink( full ) )
		return qfalse;
	if ( WA_FsRealPath( full, real, sizeof( real ) ) ) {
		if ( !WA_FsInside( real, root ) || (int)strlen( real ) >= outSize )
			return qfalse;
		*exists = qtrue;
		Q_strncpyz( out, real, outSize );
		return qtrue;
	}
	if ( !mayCreate )
		return qfalse;

	// not there yet: the folder it is to be in decides. The last / of full is the one in front of its name.
	name = strrchr( full, '/' );
	*name++ = '\0';
	if ( !WA_FsRealPath( full, real, sizeof( real ) ) || !WA_FsInside( real, root )
		|| !WA_FsStat( real, &isDir, NULL, NULL ) || !isDir
		|| (int)( strlen( real ) + strlen( name ) + 2 ) > outSize )
		return qfalse;
	Com_sprintf( out, outSize, "%s/%s", real, name );
	return qtrue;
}

// "root" and "path" of a request. qfalse: the path is not one the panel takes.
static qboolean WA_FsRequestPath( const char *query, const char *key, qboolean *install, char *rel, int relSize ) {
	char rootName[16], raw[1024];

	WA_FormValue( query, "root", rootName, sizeof( rootName ) );
	if ( install )
		*install = (qboolean)!strcmp( rootName, "install" );
	// WA_FormValue cuts a value that is longer than the buffer, and a path that lost its end is another path. What
	// is left of a value that long is still over 340 characters when decoded, so WA_FsCleanPath refuses it.
	WA_FormValue( query, key, raw, sizeof( raw ) );
	return WA_FsCleanPath( raw, rel, relSize );
}

static void WA_FsListEntry( waBuffer_t *b, const char *dir, const char *name, int *count ) {
	char		path[MAX_OSPATH], clean[WA_FS_MAX_PATH + 1];
	qboolean	isDir = qfalse, link;
	double		size = 0, mtime = 0;

	if ( !strcmp( name, "." ) || !strcmp( name, ".." ) || strlen( dir ) + strlen( name ) + 2 > sizeof( path ) )
		return;
	Com_sprintf( path, sizeof( path ), "%s/%s", dir, name );
	link = WA_FsIsLink( path );
	if ( !link && !WA_FsStat( path, &isDir, &size, &mtime ) )
		return;		// a device, a socket

	WA_BufAppend( b, *count ? ",{\"name\":" : "{\"name\":", *count ? 9 : 8 );
	WA_BufJsonString( b, name );
	// odd: a name the panel does not take in a request, it is only shown
	WA_BufPrintf( b, ",\"dir\":%s,\"size\":%.0f,\"time\":%.0f,\"link\":%s,\"code\":%s,\"odd\":%s}",
		WA_BOOL( isDir ), size, mtime, WA_BOOL( link ), WA_BOOL( !isDir && WA_FsIsCode( name ) ),
		WA_BOOL( strchr( name, '/' ) || !WA_FsCleanPath( name, clean, sizeof( clean ) ) ) );
	( *count )++;
}

// GET /api/fs/list?root=home|install&path=<folder>
void WA_ApiFsList( waConnection_t *c, const char *query ) {
	char		rel[WA_FS_MAX_PATH + 1], dir[MAX_OSPATH];
	qboolean	install, exists, isDir;
	waBuffer_t	b = { 0 };
	int			count = 0;

	if ( !WA_FsRequestPath( query, "path", &install, rel, sizeof( rel ) )
		|| !WA_FsResolve( install, rel, qfalse, dir, sizeof( dir ), &exists )
		|| !WA_FsStat( dir, &isDir, NULL, NULL ) || !isDir ) {
		WA_ActionResult( c, qfalse, "no such folder" );
		return;
	}

	WA_BufPrintf( &b, "{\"ok\":true,\"root\":\"%s\",\"writable\":%s,\"path\":", install ? "install" : "home", WA_BOOL( !install ) );
	WA_BufJsonString( &b, rel );
	WA_BufAppend( &b, ",\"entries\":[", 12 );
	{
#ifdef _WIN32
		struct _finddata_t	fd;
		char				pattern[MAX_OSPATH];
		intptr_t			h;

		Com_sprintf( pattern, sizeof( pattern ), "%s/*", dir );
		h = _findfirst( pattern, &fd );
		if ( h != -1 ) {
			do {
				WA_FsListEntry( &b, dir, fd.name, &count );
			} while ( count < WA_FS_MAX_ENTRIES && !_findnext( h, &fd ) );
			_findclose( h );
		}
#else
		DIR				*d = opendir( dir );
		struct dirent	*e;

		while ( d && count < WA_FS_MAX_ENTRIES && ( e = readdir( d ) ) != NULL )
			WA_FsListEntry( &b, dir, e->d_name, &count );
		if ( d )
			closedir( d );
#endif
	}
	WA_BufAppend( &b, "]}", 2 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

// GET /api/fs/get?root=&path=<file>[&text=1]: the file as it is for a download, or as UTF-8 text for the editor
void WA_ApiFsGet( waConnection_t *c, const char *query ) {
	char		rel[WA_FS_MAX_PATH + 1], path[MAX_OSPATH], flag[4], header[WA_FS_MAX_PATH + 128];
	qboolean	install, exists, isDir;
	double		size;
	FILE		*f;
	char		*data;
	int			len;

	if ( !WA_FsRequestPath( query, "path", &install, rel, sizeof( rel ) ) || !rel[0]
		|| !WA_FsResolve( install, rel, qfalse, path, sizeof( path ), &exists )
		|| !WA_FsStat( path, &isDir, &size, NULL ) || isDir ) {
		WA_RespondText( c, 404, "Not Found", "no such file" );
		return;
	}
	if ( size > WA_MAX_FILE ) {
		WA_RespondText( c, 413, "Payload Too Large", "the file is too large for the panel" );
		return;
	}
	len = (int)size;
	f = fopen( path, "rb" );
	data = f ? (char *)malloc( len + 1 ) : NULL;
	if ( !data || (int)fread( data, 1, len, f ) != len ) {
		if ( f )
			fclose( f );
		free( data );
		WA_RespondText( c, 500, "Internal Server Error", "could not read the file" );
		return;
	}
	fclose( f );
	WA_LogUse( c, va( "file read %s:%s", install ? "install" : "home", rel ) );

	WA_FormValue( query, "text", flag, sizeof( flag ) );
	if ( flag[0] == '1' ) {
		waBuffer_t b = { 0 };

		WA_BufLatin1AsUtf8( &b, data, len );
		WA_Respond( c, 200, "OK", "text/plain; charset=utf-8", "Cache-Control: no-store\r\n", b.data, b.len );
		WA_BufFree( &b );
	} else {
		const char *name = strrchr( rel, '/' );

		// the name has been through WA_FsCleanPath(): nothing in it ends the header or the quotes
		Com_sprintf( header, sizeof( header ), "Cache-Control: no-store\r\nContent-Disposition: attachment; filename=\"%s\"\r\n",
			name ? name + 1 : rel );
		WA_Respond( c, 200, "OK", "application/octet-stream", header, data, len );
	}
	free( data );
}

/*
=================
WA_ApiFsPut

POST /api/fs/put?path=<file>&offset=<n>[&last=1][&text=1], the body is the next piece of the file.

A request is 64 KB at most, so a file comes in pieces: each one names where it begins, and that has to be where
the one before ended. They collect in <file>.part, and the last one puts that in the place of the file, so a
file that is there is replaced in one go or not at all. text: the body is UTF-8 text of the editor.
Only into the home path.
=================
*/
void WA_ApiFsPut( waConnection_t *c, const char *query, char *body, int bodyLen ) {
	char		rel[WA_FS_MAX_PATH + 1], partRel[WA_FS_MAX_PATH + 1], num[16], flag[4];
	char		path[MAX_OSPATH], part[MAX_OSPATH];
	qboolean	exists, partExists, isDir = qfalse, last;
	double		have = 0;
	int			offset;
	const char	*digit;
	FILE		*f;

	if ( !WA_FsRequestPath( query, "path", NULL, rel, sizeof( rel ) ) || !rel[0] ) {
		WA_ActionResult( c, qfalse, "not a file name the panel takes (letters, digits, blank and _-.()+, no name that begins with a dot)" );
		return;
	}
	if ( WA_FsIsCode( rel ) ) {
		WA_ActionResult( c, qfalse, "programs and game modules are not written through the panel" );
		return;
	}
	if ( strlen( rel ) + strlen( WA_FS_PART ) > WA_FS_MAX_PATH ) {
		WA_ActionResult( c, qfalse, "the name is too long" );
		return;
	}
	Com_sprintf( partRel, sizeof( partRel ), "%s" WA_FS_PART, rel );
	if ( !WA_FsResolve( qfalse, rel, qtrue, path, sizeof( path ), &exists )
		|| !WA_FsResolve( qfalse, partRel, qtrue, part, sizeof( part ), &partExists ) ) {
		WA_ActionResult( c, qfalse, "no such folder in the server folder" );
		return;
	}
	if ( ( exists && ( !WA_FsStat( path, &isDir, NULL, NULL ) || isDir ) )
		|| ( partExists && ( !WA_FsStat( part, &isDir, &have, NULL ) || isDir ) ) ) {
		WA_ActionResult( c, qfalse, "a folder has that name" );
		return;
	}

	// digits and nothing else, and no number an int can not hold
	WA_FormValue( query, "offset", num, sizeof( num ) );
	for ( offset = 0, digit = num; *digit; digit++ ) {
		if ( *digit < '0' || *digit > '9' || offset > WA_FS_MAX_UPLOAD / 10 ) {
			offset = -1;
			break;
		}
		offset = offset * 10 + ( *digit - '0' );
	}
	WA_FormValue( query, "last", flag, sizeof( flag ) );
	last = (qboolean)( flag[0] == '1' );
	WA_FormValue( query, "text", flag, sizeof( flag ) );
	if ( flag[0] == '1' )
		bodyLen = WA_TextBody( body );

	// a piece at 0 begins the file anew, any other one goes on where the pieces so far end
	if ( offset < 0 || ( offset > 0 && (double)offset != have ) ) {
		WA_ActionResult( c, qfalse, "this piece does not go on where the file stopped, begin again" );
		return;
	}
	if ( (double)offset + bodyLen > WA_FS_MAX_UPLOAD ) {
		WA_ActionResult( c, qfalse, "the file is too large for the panel" );
		return;
	}

	f = fopen( part, offset ? "ab" : "wb" );
	if ( !f || ( bodyLen > 0 && (int)fwrite( body, 1, bodyLen, f ) != bodyLen ) ) {
		if ( f )
			fclose( f );
		WA_ActionResult( c, qfalse, "could not write the file" );
		return;
	}
	if ( fclose( f ) ) {
		WA_ActionResult( c, qfalse, "could not write the file" );
		return;
	}
	if ( !last ) {
		WA_ActionResult( c, qtrue, "piece taken" );
		return;
	}

#ifdef _WIN32
	remove( path );		// rename() does not replace a file there
#endif
	if ( rename( part, path ) ) {
		WA_ActionResult( c, qfalse, "could not put the file in its place" );
		return;
	}
	WA_LogUse( c, va( "file written home:%s, %d bytes", rel, offset + bodyLen ) );
	WA_ActionResult( c, qtrue, "saved" );
}

// POST /api/fs/delete?path=<file or empty folder>, only in the home path
void WA_ApiFsDelete( waConnection_t *c, const char *query ) {
	char		rel[WA_FS_MAX_PATH + 1], path[MAX_OSPATH];
	qboolean	exists, isDir;
	int			failed;

	if ( !WA_FsRequestPath( query, "path", NULL, rel, sizeof( rel ) ) || !rel[0]
		|| !WA_FsResolve( qfalse, rel, qfalse, path, sizeof( path ), &exists )
		|| !WA_FsStat( path, &isDir, NULL, NULL ) ) {
		WA_ActionResult( c, qfalse, "no such file in the server folder" );
		return;
	}
	if ( !isDir && WA_FsIsCode( rel ) ) {
		WA_ActionResult( c, qfalse, "programs and game modules are not deleted through the panel" );
		return;
	}
#ifdef _WIN32
	failed = isDir ? _rmdir( path ) : remove( path );
#else
	failed = isDir ? rmdir( path ) : remove( path );
#endif
	if ( failed ) {
		WA_ActionResult( c, qfalse, isDir ? "the folder is not empty" : "could not delete the file" );
		return;
	}
	WA_LogUse( c, va( "file deleted home:%s", rel ) );
	WA_ActionResult( c, qtrue, "deleted" );
}

// POST /api/fs/rename?path=<file or folder>&to=<new path>, only in the home path, never onto something that is there
void WA_ApiFsRename( waConnection_t *c, const char *query ) {
	char		rel[WA_FS_MAX_PATH + 1], toRel[WA_FS_MAX_PATH + 1], path[MAX_OSPATH], to[MAX_OSPATH];
	qboolean	exists, toExists, isDir;

	if ( !WA_FsRequestPath( query, "path", NULL, rel, sizeof( rel ) ) || !rel[0]
		|| !WA_FsResolve( qfalse, rel, qfalse, path, sizeof( path ), &exists )
		|| !WA_FsStat( path, &isDir, NULL, NULL ) ) {
		WA_ActionResult( c, qfalse, "no such file in the server folder" );
		return;
	}
	if ( !WA_FsRequestPath( query, "to", NULL, toRel, sizeof( toRel ) ) || !toRel[0] ) {
		WA_ActionResult( c, qfalse, "not a name the panel takes (letters, digits, blank and _-.()+, no name that begins with a dot)" );
		return;
	}
	if ( WA_FsIsCode( rel ) || WA_FsIsCode( toRel ) ) {
		WA_ActionResult( c, qfalse, "programs and game modules are not renamed through the panel, and nothing is renamed into one" );
		return;
	}
	if ( !WA_FsResolve( qfalse, toRel, qtrue, to, sizeof( to ), &toExists ) ) {
		WA_ActionResult( c, qfalse, "no such folder in the server folder" );
		return;
	}
	if ( toExists ) {
		WA_ActionResult( c, qfalse, "there is something with that name already" );
		return;
	}
	if ( rename( path, to ) ) {
		WA_ActionResult( c, qfalse, "could not rename" );
		return;
	}
	WA_LogUse( c, va( "file renamed home:%s to %s", rel, toRel ) );
	WA_ActionResult( c, qtrue, "renamed" );
}

// POST /api/fs/mkdir?path=<new folder>, only in the home path
void WA_ApiFsMkdir( waConnection_t *c, const char *query ) {
	char		rel[WA_FS_MAX_PATH + 1], path[MAX_OSPATH];
	qboolean	exists;

	if ( !WA_FsRequestPath( query, "path", NULL, rel, sizeof( rel ) ) || !rel[0] ) {
		WA_ActionResult( c, qfalse, "not a name the panel takes (letters, digits, blank and _-.()+, no name that begins with a dot)" );
		return;
	}
	if ( !WA_FsResolve( qfalse, rel, qtrue, path, sizeof( path ), &exists ) ) {
		WA_ActionResult( c, qfalse, "no such folder in the server folder" );
		return;
	}
	if ( exists ) {
		WA_ActionResult( c, qfalse, "there is something with that name already" );
		return;
	}
#ifdef _WIN32
	if ( _mkdir( path ) ) {
#else
	if ( mkdir( path, 0755 ) ) {
#endif
		WA_ActionResult( c, qfalse, "could not make the folder" );
		return;
	}
	WA_LogUse( c, va( "folder made home:%s", rel ) );
	WA_ActionResult( c, qtrue, "folder made" );
}

