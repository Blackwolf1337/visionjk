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

// cl_discord.cpp -- Discord Rich Presence (issue #14): the profile of the player in Discord shows the server, the map,
// the game type with the score or the opponent, how many play there and the song of a boombox nearby; friends can
// join from Discord. Not from OpenJK, see docs/japlus/client.md, "Discord".
//
// No library: the Discord app that runs on the same machine is asked over its local IPC, a Unix socket
// (discord-ipc-0 to 9 in $XDG_RUNTIME_DIR, its Flatpak and Snap folders, $TMPDIR or /tmp) or the named pipe
// \\.\pipe\discord-ipc-0 to 9 on Windows. Every message there is a frame: opcode and length (4 bytes each, little
// endian), then that many bytes of JSON. The client sends the handshake with the application id, Discord answers with
// the event READY, then SET_ACTIVITY sets what the profile shows; when the connection closes Discord takes it away.
//
// Join: on a public server that has room the activity carries a join secret, "vjk1 <ip>:<port>". A friend who clicks
// Join gets the event ACTIVITY_JOIN with it in his game (which Discord starts first if it does not run, through the
// launch entry DC_SysRegister writes), and that game connects. Discord refuses an activity with a secret and buttons
// together (error 5005), so the button to jka.vision is left out then. The secret comes from another player's
// game: only four numbers and a port are taken from it, and the connect command is written anew from them.
//
// The picture of the map is the address of its level shot in the store of jka.vision (maps/files/<map>.jpg, the
// map name with / as _), which Discord fetches itself. The game reads the list of the store (maps/index.txt) when
// it meets a map it has no picture for; a map the store does not have yet gets the levelshots/<map>.jpg of the
// game's own paks sent once (maps/api/upload), and the first one of a map stays (store/src/store.rs of the site).
// Until then, and on a map without a JPEG level shot, the logo is shown. The address carries the start of the
// SHA-256 of the picture, so a picture the store replaced is not the one Discord keeps.
//
// Nothing waits: the socket is non-blocking, the pipe is read only as far as PeekNamedPipe says bytes are there, and
// without Discord a connect is tried again every DISCORD_RETRY_MSEC. Discord takes 5 updates in 20 seconds, so an
// activity goes out only when it changed and at most every DISCORD_SEND_MSEC.

#include "client.h"
#include "sys/sys_public.h"

#ifndef __EMSCRIPTEN__

#include <SDL.h>
#include "sys/sys_http.h"

#if defined( _WIN32 )
	#include <windows.h>
	#ifdef _MSC_VER
		#pragma comment( lib, "advapi32.lib" )
	#endif
#else
	#include <errno.h>
	#include <fcntl.h>
	#include <stdio.h>
	#include <stdlib.h>
	#include <sys/socket.h>
	#include <sys/stat.h>
	#include <sys/un.h>
	#include <unistd.h>
	#if !defined( __APPLE__ )
		#include <spawn.h>
		#include <sys/wait.h>
		extern char **environ;
	#endif
#endif
#include <time.h>

#define DISCORD_APP_ID			"1572002364397854871"	// the application "VisionJK" in the Discord Developer Portal
#define DISCORD_BUTTON_LABEL	"Get VisionJK"
#define DISCORD_BUTTON_URL		"https://jka.vision"
#define DISCORD_IMAGE			"logo"					// key of the art asset of the application
#define DISCORD_JOIN_PREFIX		"vjk1 "					// of the join secret, before the address

#define DISCORD_RETRY_MSEC		20000	// a connect to a Discord that is not there, again after this long
#define DISCORD_READY_MSEC		10000	// the handshake has this long to be answered
#define DISCORD_LOOK_MSEC		1000	// how often the activity is put together
#define DISCORD_SEND_MSEC		5000	// at most one activity in this time
#define DISCORD_FRAME_HEAD		8
#define DISCORD_IN_BYTES		16384	// READY, the largest frame that comes, has about 1 KB
#define DISCORD_OUT_BYTES		4096
#define DISCORD_TEXT_BYTES		127		// Discord takes 128 characters in a field at most

enum {
	DISCORD_OP_HANDSHAKE,
	DISCORD_OP_FRAME,
	DISCORD_OP_CLOSE,
	DISCORD_OP_PING,
	DISCORD_OP_PONG
};

#define DC_MAP_INDEX_MOST		( 512 * 1024 )		// bytes of maps/index.txt at most (MAPS_MOST lines of the store)
#define DC_MAP_PICTURE_MOST		( 1024 * 1024 )		// bytes of a level shot sent (MAX_MAP_PICTURE of the store)
#define DC_MAP_INDEX_MSEC		( 10 * 60 * 1000 )	// the list is read again after this long, for a map it lacks
#define DC_MAP_AFTER_MSEC		5000				// and this long after a picture was sent
#define DC_MAP_KEY				41					// a map name of the store: 2 to 40 of a-z, 0-9 and _
#define DC_MAP_TRIED			64					// maps a picture was sent for in this run, each once

static cvar_t	*v_discord;
static cvar_t	*v_discordSong;

static qboolean	dcOpen;
static qboolean	dcReady;						// READY came, activities can be sent
static qboolean	dcRegistered;					// the launch entry was written in this run
static int		dcOpened;						// Sys_Milliseconds of the connect
static int		dcNextTry;
static int		dcNextLook;
static int		dcNextSend;
static int		dcNonce;
static byte		dcIn[DISCORD_IN_BYTES];
static int		dcInLen;
static char		dcSent[DISCORD_OUT_BYTES];		// the activity Discord has, "": none yet
static char		dcPlace[MAX_OSPATH];			// where the player is, the time shown counts from when it changed
static long long dcStart;						// that time, Unix seconds

// text for a string of JSON: " and \ escaped, control characters left out
static void DC_JsonEscape( const char *in, char *out, int outSize ) {
	int len = 0;

	for ( ; *in && len < outSize - 2; in++ ) {
		if ( (unsigned char)*in < 0x20 ) {
			continue;
		}
		if ( *in == '"' || *in == '\\' ) {
			out[len++] = '\\';
		}
		out[len++] = *in;
	}
	out[len] = '\0';
}

/*
===============================================================================

The connection to the Discord app, and the entry it starts the game with

===============================================================================
*/

#if defined( _WIN32 )

static HANDLE dcPipe = INVALID_HANDLE_VALUE;

static qboolean DC_SysOpen( void ) {
	int i;

	for ( i = 0; i < 10; i++ ) {
		// (all instances busy fails at once with ERROR_PIPE_BUSY, nothing waits)
		const HANDLE pipe = CreateFileA( va( "\\\\.\\pipe\\discord-ipc-%d", i ), GENERIC_READ | GENERIC_WRITE, 0, NULL,
			OPEN_EXISTING, 0, NULL );

		if ( pipe != INVALID_HANDLE_VALUE ) {
			dcPipe = pipe;
			return qtrue;
		}
	}
	return qfalse;
}

static void DC_SysClose( void ) {
	if ( dcPipe != INVALID_HANDLE_VALUE ) {
		CloseHandle( dcPipe );
		dcPipe = INVALID_HANDLE_VALUE;
	}
}

// The frames are a few hundred bytes and Discord reads them at once, so the write into the pipe does not wait.
static qboolean DC_SysWrite( const byte *data, int len ) {
	DWORD written;

	return (qboolean)( WriteFile( dcPipe, data, (DWORD)len, &written, NULL ) && written == (DWORD)len );
}

// bytes read, 0: none there, -1: the pipe is broken
static int DC_SysRead( byte *data, int size ) {
	DWORD there, got;

	if ( !PeekNamedPipe( dcPipe, NULL, 0, NULL, &there, NULL ) ) {
		return -1;
	}
	if ( !there ) {
		return 0;
	}
	if ( !ReadFile( dcPipe, data, there < (DWORD)size ? there : (DWORD)size, &got, NULL ) ) {
		return -1;
	}
	return (int)got;
}

static int DC_SysPid( void ) {
	return (int)GetCurrentProcessId();
}

static void DC_SysSetKey( const char *key, const char *name, const char *value ) {
	HKEY k;

	if ( RegCreateKeyExA( HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL ) != ERROR_SUCCESS ) {
		return;
	}
	RegSetValueExA( k, name, 0, REG_SZ, (const BYTE *)value, (DWORD)strlen( value ) + 1 );
	RegCloseKey( k );
}

// the URL scheme discord-<id> of the user, as discord-rpc's Discord_Register writes it: Discord opens it to start
// the game for a join
static void DC_SysRegister( const char *program ) {
	static const char	base[] = "Software\\Classes\\discord-" DISCORD_APP_ID;
	char				command[MAX_OSPATH + 4];

	if ( !program[0] || strchr( program, '"' ) ) {
		return;
	}
	Com_sprintf( command, sizeof( command ), "\"%s\"", program );
	DC_SysSetKey( base, NULL, "URL:Run game " DISCORD_APP_ID " protocol" );
	DC_SysSetKey( base, "URL Protocol", "" );
	DC_SysSetKey( va( "%s\\DefaultIcon", base ), NULL, program );
	DC_SysSetKey( va( "%s\\shell\\open\\command", base ), NULL, command );
}

static void DC_SysFrame( void ) {
}

#else

#ifndef MSG_NOSIGNAL
	#define MSG_NOSIGNAL 0		// macOS: SO_NOSIGPIPE on the socket instead
#endif

static int dcSocket = -1;

static qboolean DC_SysOpenPath( const char *path ) {
	struct sockaddr_un	addr;
	int					s;

	if ( strlen( path ) >= sizeof( addr.sun_path ) ) {
		return qfalse;
	}
	s = socket( AF_UNIX, SOCK_STREAM, 0 );
	if ( s < 0 ) {
		return qfalse;
	}
	fcntl( s, F_SETFL, fcntl( s, F_GETFL, 0 ) | O_NONBLOCK );
	fcntl( s, F_SETFD, FD_CLOEXEC );
#ifdef SO_NOSIGPIPE
	{
		int one = 1;
		setsockopt( s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof( one ) );
	}
#endif
	Com_Memset( &addr, 0, sizeof( addr ) );
	addr.sun_family = AF_UNIX;
	Q_strncpyz( addr.sun_path, path, sizeof( addr.sun_path ) );
	// (a local socket connects at once or not at all; EAGAIN, a full backlog, counts as not there)
	if ( connect( s, (struct sockaddr *)&addr, sizeof( addr ) ) != 0 ) {
		close( s );
		return qfalse;
	}
	dcSocket = s;
	return qtrue;
}

static qboolean DC_SysOpen( void ) {
	static const char *const	vars[] = { "XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP" };
	// Discord itself, the Flatpak of Discord, the Snap of Discord
	static const char *const	subs[] = { "", "app/com.discordapp.Discord/", "snap.discord/" };
	const char					*bases[ARRAY_LEN( vars ) + 1];
	int							numBases = 0;
	int							b, s, i;

	for ( i = 0; i < (int)ARRAY_LEN( vars ); i++ ) {
		const char *dir = getenv( vars[i] );

		if ( dir && dir[0] ) {
			bases[numBases++] = dir;
		}
	}
	bases[numBases++] = "/tmp";

	for ( b = 0; b < numBases; b++ ) {
		const int baseLen = (int)strlen( bases[b] );

		for ( s = 0; s < (int)ARRAY_LEN( subs ); s++ ) {
			for ( i = 0; i < 10; i++ ) {
				char path[MAX_OSPATH];

				Com_sprintf( path, sizeof( path ), "%s%s%sdiscord-ipc-%d", bases[b],
					baseLen && bases[b][baseLen - 1] == '/' ? "" : "/", subs[s], i );
				if ( DC_SysOpenPath( path ) ) {
					return qtrue;
				}
			}
		}
	}
	return qfalse;
}

static void DC_SysClose( void ) {
	if ( dcSocket >= 0 ) {
		close( dcSocket );
		dcSocket = -1;
	}
}

// A frame is a few hundred bytes and the buffer of a local socket far larger: a write that does not go out whole
// means Discord stopped reading, and the connection is given up.
static qboolean DC_SysWrite( const byte *data, int len ) {
	while ( len > 0 ) {
		const ssize_t n = send( dcSocket, data, (size_t)len, MSG_NOSIGNAL );

		if ( n < 0 && errno == EINTR ) {
			continue;
		}
		if ( n <= 0 ) {
			return qfalse;
		}
		data += n;
		len -= (int)n;
	}
	return qtrue;
}

static int DC_SysRead( byte *data, int size ) {
	for ( ;; ) {
		const ssize_t n = recv( dcSocket, data, (size_t)size, 0 );

		if ( n > 0 ) {
			return (int)n;
		}
		if ( n == 0 ) {
			return -1;		// Discord closed it
		}
		if ( errno == EINTR ) {
			continue;
		}
		return ( errno == EAGAIN || errno == EWOULDBLOCK ) ? 0 : -1;
	}
}

static int DC_SysPid( void ) {
	return (int)getpid();
}

// the file holds text, or gets it; a file that has it already is not written again
static void DC_SysWriteFile( const char *path, const char *text ) {
	char	have[2048];
	size_t	got = 0;
	FILE	*f = fopen( path, "rb" );

	if ( f ) {
		got = fread( have, 1, sizeof( have ) - 1, f );
		fclose( f );
	}
	have[got] = '\0';
	if ( !strcmp( have, text ) ) {
		return;
	}
	f = fopen( path, "wb" );
	if ( !f ) {
		return;
	}
	fwrite( text, 1, strlen( text ), f );
	fclose( f );
}

#if defined( __APPLE__ )

// ~/Library/Application Support/discord/games/<id>.json, as discord-rpc's Discord_Register writes it for a command
static void DC_SysRegister( const char *program ) {
	const char	*home = getenv( "HOME" );
	char		dir[MAX_OSPATH], escaped[MAX_OSPATH * 2], text[MAX_OSPATH * 2 + 32];

	if ( !home || !home[0] || !program[0] ) {
		return;
	}
	Com_sprintf( dir, sizeof( dir ), "%s/Library/Application Support/discord/games", home );
	mkdir( dir, 0755 );		// (the folder discord is there where Discord is installed)
	DC_JsonEscape( program, escaped, sizeof( escaped ) );
	Com_sprintf( text, sizeof( text ), "{\"command\": \"%s\"}", escaped );
	DC_SysWriteFile( va( "%s/" DISCORD_APP_ID ".json", dir ), text );
}

static void DC_SysFrame( void ) {
}

#else

static pid_t dcHelper = -1;		// xdg-mime while it runs

static qboolean DC_SysFileHas( const char *path, const char *text ) {
	char		line[1024];
	qboolean	found = qfalse;
	FILE		*f = fopen( path, "r" );

	if ( !f ) {
		return qfalse;
	}
	while ( !found && fgets( line, sizeof( line ), f ) ) {
		found = (qboolean)( strstr( line, text ) != NULL );
	}
	fclose( f );
	return found;
}

// The desktop entry discord-<id>.desktop and the URL scheme discord-<id> for it, as discord-rpc's Discord_Register
// writes them (Discord opens the scheme to start the game for a join). The scheme is set with xdg-mime only when
// mimeapps.list does not have it yet; that runs on its own, the game does not wait for it (DC_SysFrame reaps it).
// No %u in Exec: the game takes no URL, a bare argument would be run as a command.
static void DC_SysRegister( const char *program ) {
	static const char	entry[] = "discord-" DISCORD_APP_ID ".desktop";
	static const char	scheme[] = "x-scheme-handler/discord-" DISCORD_APP_ID;
	const char			*home = getenv( "HOME" ), *data = getenv( "XDG_DATA_HOME" ), *config = getenv( "XDG_CONFIG_HOME" );
	char				dir[MAX_OSPATH], text[MAX_OSPATH + 256], list[MAX_OSPATH];

	// (characters the Exec key would need escaped for, which no install path has)
	if ( !program[0] || strpbrk( program, "\"`$\\%\n\r" ) ) {
		return;
	}
	if ( data && data[0] ) {
		Com_sprintf( dir, sizeof( dir ), "%s/applications", data );
	} else if ( home && home[0] ) {
		Com_sprintf( dir, sizeof( dir ), "%s/.local/share/applications", home );
	} else {
		return;
	}
	mkdir( dir, 0755 );
	Com_sprintf( text, sizeof( text ), "[Desktop Entry]\nName=VisionJK\nExec=\"%s\"\nType=Application\nNoDisplay=true\n"
		"Categories=Discord;Games;\nMimeType=%s;\n", program, scheme );
	DC_SysWriteFile( va( "%s/%s", dir, entry ), text );

	if ( config && config[0] ) {
		Com_sprintf( list, sizeof( list ), "%s/mimeapps.list", config );
	} else if ( home && home[0] ) {
		Com_sprintf( list, sizeof( list ), "%s/.config/mimeapps.list", home );
	} else {
		return;
	}
	if ( DC_SysFileHas( list, va( "%s=%s", scheme, entry ) ) || dcHelper > 0 ) {
		return;
	}
	{
		char *const					argv[] = { (char *)"xdg-mime", (char *)"default", (char *)entry, (char *)scheme, NULL };
		posix_spawn_file_actions_t	actions;

		posix_spawn_file_actions_init( &actions );
		posix_spawn_file_actions_addopen( &actions, 0, "/dev/null", O_RDONLY, 0 );
		posix_spawn_file_actions_addopen( &actions, 1, "/dev/null", O_WRONLY, 0 );
		posix_spawn_file_actions_addopen( &actions, 2, "/dev/null", O_WRONLY, 0 );
		if ( posix_spawnp( &dcHelper, "xdg-mime", &actions, NULL, argv, environ ) != 0 ) {
			dcHelper = -1;
		}
		posix_spawn_file_actions_destroy( &actions );
	}
}

static void DC_SysFrame( void ) {
	if ( dcHelper > 0 && waitpid( dcHelper, NULL, WNOHANG ) != 0 ) {
		dcHelper = -1;
	}
}

#endif

#endif

static void DC_Close( const char *why ) {
	if ( dcOpen && why ) {
		Com_DPrintf( "Discord: %s\n", why );
	}
	DC_SysClose();
	dcOpen = qfalse;
	dcReady = qfalse;
	dcInLen = 0;
	dcSent[0] = '\0';
}

static qboolean DC_Send( int op, const char *json ) {
	byte		frame[DISCORD_FRAME_HEAD + DISCORD_OUT_BYTES];
	const int	len = (int)strlen( json );
	int			i;

	if ( len > DISCORD_OUT_BYTES ) {
		return qfalse;
	}
	for ( i = 0; i < 4; i++ ) {
		frame[i] = (byte)( (unsigned int)op >> ( i * 8 ) );
		frame[4 + i] = (byte)( (unsigned int)len >> ( i * 8 ) );
	}
	Com_Memcpy( frame + DISCORD_FRAME_HEAD, json, len );
	if ( !DC_SysWrite( frame, DISCORD_FRAME_HEAD + len ) ) {
		DC_Close( "the connection broke" );
		return qfalse;
	}
	return qtrue;
}

static unsigned int DC_GetU32( const byte *b ) {
	return b[0] | ( b[1] << 8 ) | ( b[2] << 16 ) | ( (unsigned int)b[3] << 24 );
}

// the string value of key in the JSON, qfalse: not there, or one with an escape (no value this file reads has one)
static qboolean DC_JsonString( const char *json, const char *key, char *out, int outSize ) {
	char		quoted[64];
	const char	*p;
	int			len = 0;

	Com_sprintf( quoted, sizeof( quoted ), "\"%s\"", key );
	p = strstr( json, quoted );
	if ( !p ) {
		return qfalse;
	}
	p += strlen( quoted );
	while ( *p == ' ' || *p == ':' ) {
		p++;
	}
	if ( *p++ != '"' ) {
		return qfalse;
	}
	for ( ; *p != '"'; p++ ) {
		if ( !*p || *p == '\\' || len >= outSize - 1 ) {
			return qfalse;
		}
		out[len++] = *p;
	}
	out[len] = '\0';
	return qtrue;
}

static qboolean DC_IsEvent( const char *json, const char *evt ) {
	char value[64];

	return (qboolean)( DC_JsonString( json, "evt", value, sizeof( value ) ) && !strcmp( value, evt ) );
}

// A friend's join secret, from his game through Discord: "vjk1 " and an IPv4 address with a port, nothing else. The
// command is written from the numbers, never from the text.
static void DC_Join( const char *secret ) {
	int		ip[4], port, i;
	char	end;

	if ( strncmp( secret, DISCORD_JOIN_PREFIX, strlen( DISCORD_JOIN_PREFIX ) )
		|| sscanf( secret + strlen( DISCORD_JOIN_PREFIX ), "%3d.%3d.%3d.%3d:%5d%c", &ip[0], &ip[1], &ip[2], &ip[3], &port, &end ) != 5
		|| port < 1 || port > 65535 ) {
		Com_Printf( "Discord: a join without a server address, not followed\n" );
		return;
	}
	for ( i = 0; i < 4; i++ ) {
		if ( ip[i] < 0 || ip[i] > 255 ) {
			Com_Printf( "Discord: a join without a server address, not followed\n" );
			return;
		}
	}
	Com_Printf( "Discord: joining your friend on %d.%d.%d.%d:%d\n", ip[0], ip[1], ip[2], ip[3], port );
	Cbuf_AddText( va( "connect %d.%d.%d.%d:%d\n", ip[0], ip[1], ip[2], ip[3], port ) );
}

static void DC_TakeFrame( int op, const byte *data, int len ) {
	char json[DISCORD_IN_BYTES];
	char secret[128];

	Com_Memcpy( json, data, len );
	json[len] = '\0';

	switch ( op ) {
	case DISCORD_OP_FRAME:
		if ( DC_IsEvent( json, "READY" ) ) {
			Com_DPrintf( "Discord: connected\n" );
			dcReady = qtrue;
			dcSent[0] = '\0';
			dcNextSend = Sys_Milliseconds();
			DC_Send( DISCORD_OP_FRAME, va( "{\"cmd\":\"SUBSCRIBE\",\"evt\":\"ACTIVITY_JOIN\",\"nonce\":\"%d\"}", ++dcNonce ) );
			if ( !dcRegistered ) {
				DC_SysRegister( Sys_ProgramFile() );
				dcRegistered = qtrue;
			}
		} else if ( DC_IsEvent( json, "ACTIVITY_JOIN" ) ) {
			if ( DC_JsonString( json, "secret", secret, sizeof( secret ) ) ) {
				DC_Join( secret );
			}
		} else if ( DC_IsEvent( json, "ERROR" ) ) {
			Com_DPrintf( "Discord: %s\n", json );
		}
		break;
	case DISCORD_OP_PING:
		DC_Send( DISCORD_OP_PONG, json );
		break;
	case DISCORD_OP_CLOSE:
		// (a wrong application id ends here too: {"code":4000,"message":"Invalid Client ID"})
		DC_Close( va( "closed by Discord: %s", json ) );
		break;
	default:
		break;
	}
}

// every byte that came, and every frame that is whole
static void DC_Pump( void ) {
	while ( dcOpen ) {
		const int n = DC_SysRead( dcIn + dcInLen, (int)sizeof( dcIn ) - dcInLen );

		if ( n < 0 ) {
			DC_Close( "Discord closed the connection" );
			return;
		}
		dcInLen += n;
		while ( dcOpen && dcInLen >= DISCORD_FRAME_HEAD ) {
			const int			op = (int)DC_GetU32( dcIn );
			const unsigned int	len = DC_GetU32( dcIn + 4 );

			if ( len > sizeof( dcIn ) - DISCORD_FRAME_HEAD - 1 ) {
				DC_Close( va( "a frame of %u bytes", len ) );
				return;
			}
			if ( dcInLen < DISCORD_FRAME_HEAD + (int)len ) {
				break;
			}
			DC_TakeFrame( op, dcIn + DISCORD_FRAME_HEAD, (int)len );
			if ( !dcOpen ) {
				return;
			}
			dcInLen -= DISCORD_FRAME_HEAD + (int)len;
			memmove( dcIn, dcIn + DISCORD_FRAME_HEAD + len, dcInLen );
		}
		if ( !n ) {
			return;
		}
	}
}

static void DC_Connect( void ) {
	if ( !DC_SysOpen() ) {
		return;
	}
	dcOpen = qtrue;
	dcReady = qfalse;
	dcInLen = 0;
	dcSent[0] = '\0';
	dcOpened = Sys_Milliseconds();
	DC_Send( DISCORD_OP_HANDSHAKE, "{\"v\":1,\"client_id\":\"" DISCORD_APP_ID "\"}" );
}

/*
===============================================================================

The activity

===============================================================================
*/

// "key":"text" at the end of out, the text without colors, in UTF-8 (names of legacy servers are Windows-1252,
// Q_UTF8_Decode reads both), cut to what Discord takes and escaped for JSON. qfalse: shorter than the two characters
// Discord asks for, nothing written.
static qboolean DC_AddText( char *out, int outSize, const char *key, const char *text ) {
	char		clean[MAX_STRING_CHARS];
	char		utf8[MAX_STRING_CHARS];
	char		json[MAX_STRING_CHARS];
	const char	*p;
	int			len = 0, start, end;

	Q_strncpyz( clean, text, sizeof( clean ) );
	Q_StripColor( clean );
	for ( p = clean; *p; ) {
		unsigned int	codePoint;
		char			bytes[4];
		const int		used = Q_UTF8_Decode( p, &codePoint );
		int				count;

		if ( !used ) {
			break;
		}
		p += used;
		if ( codePoint < 0x20 ) {
			continue;
		}
		count = Q_UTF8_Encode( codePoint, bytes );
		if ( len + count >= (int)sizeof( utf8 ) ) {
			break;
		}
		Com_Memcpy( utf8 + len, bytes, count );
		len += count;
	}
	utf8[len] = '\0';

	for ( start = 0; utf8[start] == ' '; start++ ) {
	}
	Q_UTF8_Truncate( utf8 + start, DISCORD_TEXT_BYTES );
	for ( end = (int)strlen( utf8 ); end > start && utf8[end - 1] == ' '; end-- ) {
	}
	utf8[end] = '\0';
	if ( end - start < 2 ) {
		return qfalse;
	}

	DC_JsonEscape( utf8 + start, json, sizeof( json ) );
	Q_strcat( out, outSize, va( "%s\"%s\":\"%s\"", out[0] && out[strlen( out ) - 1] != '{' ? "," : "", key, json ) );
	return qtrue;
}

static const char *DC_GametypeName( int gametype ) {
	static const char *const names[] = {
		"FFA", "Holocron", "Jedi Master", "Duel", "Power Duel", "Single Player",
		"Team FFA", "Siege", "CTF", "CTY"
	};

	return ( gametype >= 0 && gametype < (int)ARRAY_LEN( names ) ) ? names[gametype] : "";
}

static const char *DC_ConfigString( int index ) {
	return cl.gameState.stringData + cl.gameState.stringOffsets[index];
}

// what the player does in the game: spectating, the opponent of a duel, the score of the teams or his own
static void DC_Status( int gametype, char *out, int outSize ) {
	const char			*name = DC_GametypeName( gametype );
	const playerState_t	*ps = &cl.snap.ps;

	if ( !cl.snap.valid ) {
		Q_strncpyz( out, name, outSize );
		return;
	}
	// (following somebody, the snapshot carries that player's state with PMF_FOLLOW)
	if ( ps->persistant[PERS_TEAM] == TEAM_SPECTATOR || ( ps->pm_flags & PMF_FOLLOW ) ) {
		Com_sprintf( out, outSize, "%s, spectating", name );
		return;
	}
	if ( gametype == GT_DUEL ) {
		const char	*duelists = DC_ConfigString( CS_CLIENT_DUELISTS );
		const char	*bar = strchr( duelists, '|' );
		const int	a = atoi( duelists ), b = bar ? atoi( bar + 1 ) : -1;
		const int	other = ( clc.clientNum == a ) ? b : ( clc.clientNum == b ) ? a : -1;
		const char	*info = ( other >= 0 && other < MAX_CLIENTS ) ? DC_ConfigString( CS_PLAYERS + other ) : "";

		if ( info[0] ) {
			Com_sprintf( out, outSize, "Duel vs %s", Info_ValueForKey( info, "n" ) );
		} else {
			Q_strncpyz( out, "Duel, waiting", outSize );
		}
		return;
	}
	if ( gametype >= GT_TEAM && gametype != GT_SIEGE ) {
		const int red = atoi( DC_ConfigString( CS_SCORES1 ) ), blue = atoi( DC_ConfigString( CS_SCORES2 ) );

		if ( red != SCORE_NOT_PRESENT && blue != SCORE_NOT_PRESENT ) {
			Com_sprintf( out, outSize, "%s, Red %d : %d Blue", name, red, blue );
			return;
		}
	} else if ( gametype < GT_DUEL ) {
		Com_sprintf( out, outSize, "%s, score %d", name, ps->persistant[PERS_SCORE] );
		return;
	}
	Q_strncpyz( out, name, outSize );
}

/*
===============================================================================

The map pictures in the store of jka.vision

===============================================================================
*/

enum {
	DC_JOB_FREE,
	DC_JOB_RUNNING,
	DC_JOB_DONE			// the thread is through, the game takes what it did
};

// one question to the store at a time, in a thread of its own (Sys_HttpGet waits)
typedef struct {
	SDL_atomic_t	state;
	volatile int	cancel;
	char			url[MAX_OSPATH + 128];
	const byte		*body;				// what a POST sends, NULL: a GET
	int				bodyLen;
	// what the thread writes, the game reads it once the state is DC_JOB_DONE
	char			*out;
	int				outMost, outLen;
	qboolean		ok;
	int				status;
	char			error[256];
} dcJob_t;

static dcJob_t	dcJob;
static qboolean	dcJobUpload;					// the job sends a picture, else it reads the list
static char		dcMapIn[DC_MAP_INDEX_MOST + 1];	// the list as it comes in
static char		dcMapIndex[DC_MAP_INDEX_MOST + 1];
static qboolean	dcMapHave;						// a list came
static qboolean	dcMapAsked;						// it was asked for once
static int		dcMapDue;						// Sys_Milliseconds when the list may be read again
static byte		dcMapBody[DC_MAP_PICTURE_MOST];
static char		dcMapAnswer[512];
static char		dcMapTried[DC_MAP_TRIED][DC_MAP_KEY];
static int		dcMapNumTried;

static qboolean DC_JobSink( void *ctx, const void *data, size_t len ) {
	dcJob_t *job = (dcJob_t *)ctx;

	if ( job->outLen + (int)len > job->outMost ) {
		return qfalse;
	}
	memcpy( job->out + job->outLen, data, len );
	job->outLen += (int)len;
	return qtrue;
}

// the thread: nothing of the engine but Sys_Http*
static int DC_JobThread( void *data ) {
	dcJob_t *job = (dcJob_t *)data;

	job->ok = job->body ? Sys_HttpPost( job->url, job->body, job->bodyLen, DC_JobSink, job, &job->cancel, job->error, sizeof( job->error ), &job->status )
		: Sys_HttpGet( job->url, DC_JobSink, job, &job->cancel, job->error, sizeof( job->error ), &job->status );
	SDL_AtomicSet( &job->state, DC_JOB_DONE );
	return 0;
}

static qboolean DC_JobStart( const char *path, const byte *body, int bodyLen, char *out, int outMost ) {
	dcJob_t		*job = &dcJob;
	SDL_Thread	*thread;

	memset( job, 0, sizeof( *job ) );
	if ( !CL_ModelsStoreURL( path, job->url, sizeof( job->url ) ) ) {
		return qfalse;
	}
	job->body = body;
	job->bodyLen = bodyLen;
	job->out = out;
	job->outMost = outMost;
	SDL_AtomicSet( &job->state, DC_JOB_RUNNING );
	thread = SDL_CreateThread( DC_JobThread, "discord", job );
	if ( !thread ) {
		SDL_AtomicSet( &job->state, DC_JOB_FREE );
		return qfalse;
	}
	SDL_DetachThread( thread );
	return qtrue;
}

// what a job that is through did
static void DC_MapPump( void ) {
	const int now = Sys_Milliseconds();

	if ( SDL_AtomicGet( &dcJob.state ) != DC_JOB_DONE ) {
		return;
	}
	if ( dcJobUpload ) {
		dcMapAnswer[dcJob.outLen] = '\0';
		// (409: another game sent it first; either way the list has it now)
		if ( dcJob.ok || dcJob.status == 409 ) {
			dcMapDue = now + DC_MAP_AFTER_MSEC;
		} else {
			Com_DPrintf( "Discord: the map picture was not taken (%d %s) %s\n", dcJob.status, dcJob.error, dcMapAnswer );
		}
	} else {
		if ( dcJob.ok ) {
			dcMapIn[dcJob.outLen] = '\0';
			Com_Memcpy( dcMapIndex, dcMapIn, dcJob.outLen + 1 );
			dcMapHave = qtrue;
		} else {
			Com_DPrintf( "Discord: the list of the map pictures did not come (%d %s)\n", dcJob.status, dcJob.error );
		}
		dcMapDue = now + DC_MAP_INDEX_MSEC;
	}
	SDL_AtomicSet( &dcJob.state, DC_JOB_FREE );
}

// the name of map in the store: lower case, / as _; qfalse: a name the store does not take
static qboolean DC_MapKey( const char *map, char *key ) {
	int len = 0;

	for ( ; *map; map++ ) {
		const char c = (char)tolower( (unsigned char)*map );

		if ( len >= DC_MAP_KEY - 1 ) {
			return qfalse;
		}
		if ( c == '/' ) {
			key[len++] = '_';
		} else if ( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' ) {
			key[len++] = c;
		} else {
			return qfalse;
		}
	}
	key[len] = '\0';
	return (qboolean)( len >= 2 );
}

// The line of key in the list ("<name> <bytes> <sha256> jpg"), the first 8 of its SHA-256 into version.
static qboolean DC_MapFind( const char *key, char *version ) {
	const int	len = (int)strlen( key );
	const char	*line;

	if ( !dcMapHave ) {
		return qfalse;
	}
	for ( line = dcMapIndex; *line; ) {
		const char *end = strchr( line, '\n' );

		if ( !strncmp( line, key, len ) && line[len] == ' ' ) {
			int			i;
			const char	*sha = strchr( line + len + 1, ' ' );

			if ( !sha ) {
				return qfalse;
			}
			for ( sha++, i = 0; i < 8 && isxdigit( (unsigned char)sha[i] ); i++ ) {
				version[i] = sha[i];
			}
			version[i] = '\0';
			return (qboolean)( i == 8 );
		}
		if ( !end ) {
			break;
		}
		line = end + 1;
	}
	return qfalse;
}

static qboolean DC_MapWasTried( const char *key ) {
	int i;

	for ( i = 0; i < dcMapNumTried; i++ ) {
		if ( !strcmp( dcMapTried[i], key ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

// The level shot of map out of the paks to the store, once a run. qfalse: there is none to send.
static qboolean DC_MapSend( const char *map, const char *key ) {
	void	*data;
	int		len;

	Q_strncpyz( dcMapTried[dcMapNumTried % DC_MAP_TRIED], key, DC_MAP_KEY );
	if ( dcMapNumTried < DC_MAP_TRIED ) {
		dcMapNumTried++;
	}
	len = FS_ReadFile( va( "levelshots/%s.jpg", map ), &data );
	if ( len <= 0 ) {
		return qfalse;
	}
	if ( len > DC_MAP_PICTURE_MOST || len < 4 || ( (byte *)data )[0] != 0xff || ( (byte *)data )[1] != 0xd8 ) {
		FS_FreeFile( data );
		return qfalse;
	}
	Com_Memcpy( dcMapBody, data, len );
	FS_FreeFile( data );
	dcJobUpload = qtrue;
	return DC_JobStart( va( "maps/api/upload?name=%s", key ), dcMapBody, len, dcMapAnswer, sizeof( dcMapAnswer ) - 1 );
}

// The address of the picture of map, qfalse: none (yet), the logo then. Reads the list or sends the level shot
// when the store lacks it.
static qboolean DC_MapPicture( const char *map, char *url, int urlSize ) {
	char key[DC_MAP_KEY], version[9];

	if ( !DC_MapKey( map, key ) ) {
		return qfalse;
	}
	if ( DC_MapFind( key, version ) ) {
		return CL_ModelsStoreURL( va( "maps/files/%s.jpg?v=%s", key, version ), url, urlSize );
	}
	if ( SDL_AtomicGet( &dcJob.state ) != DC_JOB_FREE ) {
		return qfalse;
	}
	if ( !dcMapAsked || Sys_Milliseconds() - dcMapDue >= 0 ) {
		dcJobUpload = qfalse;
		dcMapAsked = qtrue;
		dcMapDue = Sys_Milliseconds() + DC_MAP_INDEX_MSEC;		// (also when it can not start)
		DC_JobStart( "maps/index.txt", NULL, 0, dcMapIn, DC_MAP_INDEX_MOST );
	} else if ( !DC_MapWasTried( key ) ) {
		DC_MapSend( map, key );
	}
	return qfalse;
}

// the activity as Discord wants it in SET_ACTIVITY, and where the player is (for the time shown)
static void DC_Activity( char *out, int outSize, char *place, int placeSize ) {
	char		details[MAX_STRING_CHARS], state[MAX_STRING_CHARS], status[MAX_STRING_CHARS];
	char		map[MAX_QPATH], mapUrl[MAX_OSPATH + 128], mapKey[MAX_OSPATH + 160], song[MAX_STRING_CHARS], party[32], secret[64];
	const char	*address = NET_AdrToString( &clc.serverAddress );
	int			players = 0, maxPlayers = 0;

	details[0] = state[0] = map[0] = mapKey[0] = song[0] = party[0] = secret[0] = '\0';

	if ( clc.demoplaying ) {
		Q_strncpyz( details, "Watching a demo", sizeof( details ) );
		Q_strncpyz( place, "demo", placeSize );
	} else if ( cls.state == CA_ACTIVE ) {
		const char	*info = DC_ConfigString( CS_SERVERINFO );
		const int	gametype = atoi( Info_ValueForKey( info, "g_gametype" ) );
		const int	local = com_sv_running->integer, locked = atoi( Info_ValueForKey( info, "g_needpass" ) );
		int			i;

		Com_sprintf( place, placeSize, "server %s", cls.servername );
		if ( v_discord->integer == 2 ) {
			Q_strncpyz( details, "In a game", sizeof( details ) );
		} else {
			if ( local ) {
				Q_strncpyz( details, "Local game", sizeof( details ) );
			} else if ( locked ) {
				Q_strncpyz( details, "Private server", sizeof( details ) );
			} else {
				Q_strncpyz( details, Info_ValueForKey( info, "sv_hostname" ), sizeof( details ) );
			}
			Q_strncpyz( map, Info_ValueForKey( info, "mapname" ), sizeof( map ) );
			if ( DC_MapPicture( map, mapUrl, sizeof( mapUrl ) ) ) {
				DC_JsonEscape( mapUrl, mapKey, sizeof( mapKey ) );
			}
			DC_Status( gametype, status, sizeof( status ) );
			Com_sprintf( state, sizeof( state ), "%s%s%s", map, status[0] ? " | " : "", status );
			Q_strncpyz( song, v_discordSong->string, sizeof( song ) );

			maxPlayers = atoi( Info_ValueForKey( info, "sv_maxclients" ) );
			for ( i = 0; i < MAX_CLIENTS && i < maxPlayers; i++ ) {
				if ( DC_ConfigString( CS_PLAYERS + i )[0] ) {
					players++;
				}
			}
			if ( maxPlayers > 1 ) {
				// (groups the friends on the same server; a hash, so the address is not in it)
				Com_sprintf( party, sizeof( party ), "vjk-%08x", (unsigned int)Com_BlockChecksum( address, (int)strlen( address ) ) );
			}
			// Join only where a friend can follow: a public server on the internet with room. A server with a password,
			// one in the own network and a game of this client are left out.
			if ( !local && !locked && clc.serverAddress.type == NA_IP && !Sys_IsLANAddress( &clc.serverAddress )
				&& players < maxPlayers ) {
				Com_sprintf( secret, sizeof( secret ), DISCORD_JOIN_PREFIX "%s", address );
			}
		}
	} else if ( cls.state >= CA_CONNECTING && cls.state < CA_ACTIVE ) {
		Q_strncpyz( details, "Joining a server", sizeof( details ) );
		Com_sprintf( place, placeSize, "server %s", cls.servername );
	} else {
		Q_strncpyz( details, "In the menus", sizeof( details ) );
		Q_strncpyz( place, "menu", placeSize );
	}

	if ( strcmp( place, dcPlace ) ) {
		Q_strncpyz( dcPlace, place, sizeof( dcPlace ) );
		dcStart = (long long)time( NULL );
	}

	Q_strncpyz( out, "{", outSize );
	if ( !DC_AddText( out, outSize, "details", details ) ) {
		DC_AddText( out, outSize, "details", "VisionJK" );
	}
	DC_AddText( out, outSize, "state", state );
	Q_strcat( out, outSize, va( ",\"timestamps\":{\"start\":%lld}", dcStart ) );

	// the map large with its name, the logo small; on a map without a picture the logo large
	if ( mapKey[0] ) {
		Q_strcat( out, outSize, va( ",\"assets\":{\"large_image\":\"%s\"", mapKey ) );
		DC_AddText( out, outSize, "large_text", map );
		Q_strcat( out, outSize, ",\"small_image\":\"" DISCORD_IMAGE "\"" );
		DC_AddText( out, outSize, "small_text", song[0] ? va( "Listening to %s", song ) : "VisionJK" );
	} else {
		Q_strcat( out, outSize, ",\"assets\":{\"large_image\":\"" DISCORD_IMAGE "\"" );
		DC_AddText( out, outSize, "large_text", song[0] ? va( "Listening to %s", song ) : "VisionJK" );
	}
	Q_strcat( out, outSize, "}" );

	if ( party[0] ) {
		Q_strcat( out, outSize, va( ",\"party\":{\"id\":\"%s\",\"size\":[%d,%d]}", party, players, maxPlayers ) );
	}
	if ( secret[0] ) {
		Q_strcat( out, outSize, va( ",\"secrets\":{\"join\":\"%s\"}", secret ) );
	} else {
		Q_strcat( out, outSize, ",\"buttons\":[{\"label\":\"" DISCORD_BUTTON_LABEL "\",\"url\":\"" DISCORD_BUTTON_URL "\"}]" );
	}
	Q_strcat( out, outSize, "}" );
}

/*
===============================================================================

Called by the client

===============================================================================
*/

void CL_DiscordFrame( void ) {
	const int	now = Sys_Milliseconds();
	char		activity[DISCORD_OUT_BYTES - 128];
	char		place[MAX_OSPATH];

	DC_SysFrame();
	DC_MapPump();
	if ( !v_discord->integer ) {
		DC_Close( "switched off" );		// (Discord takes the activity away when the connection closes)
		return;
	}
	if ( !dcOpen ) {
		if ( now - dcNextTry < 0 ) {
			return;
		}
		dcNextTry = now + DISCORD_RETRY_MSEC;
		DC_Connect();
		if ( !dcOpen ) {
			return;
		}
	}

	DC_Pump();
	if ( !dcOpen ) {
		return;
	}
	if ( !dcReady ) {
		if ( now - dcOpened > DISCORD_READY_MSEC ) {
			DC_Close( "no answer to the handshake" );
		}
		return;
	}

	if ( now - dcNextLook < 0 || now - dcNextSend < 0 ) {
		return;
	}
	dcNextLook = now + DISCORD_LOOK_MSEC;
	DC_Activity( activity, sizeof( activity ), place, sizeof( place ) );
	if ( !strcmp( activity, dcSent ) ) {
		return;
	}
	if ( DC_Send( DISCORD_OP_FRAME, va( "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%d,\"activity\":%s},\"nonce\":\"%d\"}",
		DC_SysPid(), activity, ++dcNonce ) ) ) {
		Q_strncpyz( dcSent, activity, sizeof( dcSent ) );
		dcNextSend = now + DISCORD_SEND_MSEC;
	}
}

void CL_DiscordInit( void ) {
	v_discord = Cvar_Get( "v_discord", "1", CVAR_ARCHIVE_ND, "Discord profile while the Discord app runs. 1: server, map, score, players, song nearby, friends can join. 2: only \"In a game\". 0: off" );
	// set by the cgame (boombox/cg_boombox.c), a server can not set a v_ cvar
	v_discordSong = Cvar_Get( "v_discordSong", "", CVAR_TEMP, "The song of the boombox this player hears, for the Discord profile" );
	dcNextTry = Sys_Milliseconds();
}

void CL_DiscordShutdown( void ) {
	dcJob.cancel = 1;		// (a job that runs ends on its own, nobody waits for it)
	DC_Close( NULL );
}

#endif
