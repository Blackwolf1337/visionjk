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

// cg_status_music.c -- what music plays on this machine right now, for cg_status.c. Asked every five seconds,
// and only while cg_shareMusic is 1: nothing of this runs before the player switched it on.
//
// Linux and the other systems with a desktop bus: the players of music say it over D-Bus (MPRIS: Spotify, the
// browsers, VLC and so on). The library of the bus (libdbus-1.so.3) is loaded when it is first needed and is
// not needed to build or start the game. The questions are sent without waiting for the answers, which are
// looked for with every frame (pending calls); only the connection to the bus is made in one go, once.
//
// Windows: the title of the window of Spotify is "<artist> - <title>" while it plays, and a window of a browser
// or of the YouTube Music app is "<title> - YouTube Music". (The media session of Windows would know more, the
// headers of mingw do not have it.)
//
// macOS and the browser client: nothing.
//
// The answer is UTF-8: "Spotify: <artist> - <title>", "YT Music: <title>", or "<artist> - <title>" for any
// other player.

#if defined(_WIN32) && ( !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600 )
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600		// QueryFullProcessImageNameW
#endif

#include "cgame/cg_local.h"

#define MUSIC_ASK_MSEC	5000
#define MUSIC_TEXT		256

static char		musicNow[MUSIC_TEXT];		// what plays, empty: nothing
static int		musicAskTime;				// when it is asked next (trap->Milliseconds)

// puts source and title together. artist and source may be empty.
static void Music_Set( const char *source, const char *artist, const char *title ) {
	if ( !title || !title[0] ) {
		musicNow[0] = '\0';
		return;
	}
	Com_sprintf( musicNow, sizeof( musicNow ), "%s%s%s%s%s", ( source && source[0] ) ? source : "", ( source && source[0] ) ? ": " : "",
		( artist && artist[0] ) ? artist : "", ( artist && artist[0] ) ? " - " : "", title );
}

#if defined(_WIN32)

#include <windows.h>

static void Music_Utf8( const WCHAR *wide, char *out, int outSize ) {
	if ( !WideCharToMultiByte( CP_UTF8, 0, wide, -1, out, outSize, NULL, NULL ) ) {
		out[0] = '\0';
	}
	out[outSize - 1] = '\0';
}

static BOOL CALLBACK Music_EnumWindow( HWND window, LPARAM unused ) {
	WCHAR	title[200], name[64];
	WCHAR	*mark;
	char	text[MUSIC_TEXT];

	(void)unused;
	if ( !IsWindowVisible( window ) || GetWindowTextW( window, title, ARRAY_LEN( title ) ) < 4 ) {
		return TRUE;
	}
	mark = wcsstr( title, L" - YouTube Music" );
	if ( mark && mark != title ) {
		*mark = 0;
		Music_Utf8( title, text, sizeof( text ) );
		Music_Set( "YT Music", "", text );
		return FALSE;
	}
	// Spotify: a window of its kind whose title has the dash, of the program Spotify.exe
	if ( wcsstr( title, L" - " ) && GetClassNameW( window, name, ARRAY_LEN( name ) ) && !wcsncmp( name, L"Chrome_WidgetWin", 16 ) ) {
		DWORD	pid = 0;
		HANDLE	process;

		GetWindowThreadProcessId( window, &pid );
		process = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid );
		if ( process ) {
			WCHAR	path[MAX_PATH];
			DWORD	size = ARRAY_LEN( path );
			BOOL	spotify = FALSE;

			if ( QueryFullProcessImageNameW( process, 0, path, &size ) ) {
				const WCHAR *file = wcsrchr( path, L'\\' );

				spotify = (BOOL)( !_wcsicmp( file ? file + 1 : path, L"Spotify.exe" ) );
			}
			CloseHandle( process );
			if ( spotify ) {
				Music_Utf8( title, text, sizeof( text ) );
				Music_Set( "Spotify", "", text );
				return FALSE;
			}
		}
	}
	return TRUE;
}

void CG_MusicFrame( void ) {
	const int now = trap->Milliseconds();

	if ( !cg_shareMusic.integer || now < musicAskTime ) {
		return;
	}
	musicAskTime = now + MUSIC_ASK_MSEC;
	musicNow[0] = '\0';
	EnumWindows( Music_EnumWindow, 0 );
}

void CG_MusicShutdown( void ) {
}

#elif defined(__APPLE__) || defined(__EMSCRIPTEN__)

void CG_MusicFrame( void ) {
}

void CG_MusicShutdown( void ) {
}

#else

#include <dlfcn.h>

// What is used of libdbus, declared here so the game builds without its headers. The types are the ones of
// dbus/dbus.h; DBusMessageIter is larger here than there, which is all a caller needs to know of it.
typedef struct DBusConnection DBusConnection;
typedef struct DBusMessage DBusMessage;
typedef struct DBusPendingCall DBusPendingCall;
typedef unsigned int dbus_bool_t;

typedef struct {
	const char		*name;
	const char		*message;
	unsigned int	dummy1 : 1;
	unsigned int	dummy2 : 1;
	unsigned int	dummy3 : 1;
	unsigned int	dummy4 : 1;
	unsigned int	dummy5 : 1;
	void			*padding1;
} DBusError;

typedef struct {
	void	*room[16];
} DBusMessageIter;

#define DBUS_BUS_SESSION				0
#define DBUS_TYPE_INVALID				0
#define DBUS_TYPE_STRING				's'
#define DBUS_TYPE_ARRAY					'a'
#define DBUS_TYPE_VARIANT				'v'
#define DBUS_TYPE_DICT_ENTRY			'e'
#define DBUS_MESSAGE_TYPE_METHOD_RETURN	2
#define DBUS_DISPATCH_DATA_REMAINS		0

#define MUSIC_MAX_PLAYERS	8
#define MUSIC_REPLY_MSEC	1000

enum {
	MUSIC_NONE,			// not tried yet
	MUSIC_FAILED,		// no library or no bus: nothing more is tried
	MUSIC_IDLE,
	MUSIC_NAMES,		// asked the bus who is there
	MUSIC_STATUS,		// asked a player whether it plays
	MUSIC_META			// asked it what
};

static struct {
	int				state;
	void			*library;
	DBusConnection	*connection;
	DBusPendingCall	*pending;
	char			players[MUSIC_MAX_PLAYERS][128];
	int				numPlayers, player;

	void			(*error_init)( DBusError *error );
	void			(*error_free)( DBusError *error );
	DBusConnection	*(*bus_get_private)( int type, DBusError *error );
	void			(*connection_set_exit_on_disconnect)( DBusConnection *connection, dbus_bool_t exitOnDisconnect );
	dbus_bool_t		(*connection_read_write)( DBusConnection *connection, int timeoutMsec );
	int				(*connection_dispatch)( DBusConnection *connection );
	dbus_bool_t		(*connection_send_with_reply)( DBusConnection *connection, DBusMessage *message, DBusPendingCall **pending, int timeoutMsec );
	void			(*connection_close)( DBusConnection *connection );
	void			(*connection_unref)( DBusConnection *connection );
	DBusMessage		*(*message_new_method_call)( const char *destination, const char *path, const char *iface, const char *method );
	dbus_bool_t		(*message_append_args)( DBusMessage *message, int firstType, ... );
	void			(*message_unref)( DBusMessage *message );
	int				(*message_get_type)( DBusMessage *message );
	dbus_bool_t		(*message_iter_init)( DBusMessage *message, DBusMessageIter *iter );
	int				(*message_iter_get_arg_type)( DBusMessageIter *iter );
	void			(*message_iter_recurse)( DBusMessageIter *iter, DBusMessageIter *sub );
	dbus_bool_t		(*message_iter_next)( DBusMessageIter *iter );
	void			(*message_iter_get_basic)( DBusMessageIter *iter, void *value );
	dbus_bool_t		(*pending_call_get_completed)( DBusPendingCall *pending );
	DBusMessage		*(*pending_call_steal_reply)( DBusPendingCall *pending );
	void			(*pending_call_unref)( DBusPendingCall *pending );
} music;

static void Music_Close( int state ) {
	if ( music.pending ) {
		music.pending_call_unref( music.pending );
		music.pending = NULL;
	}
	if ( music.connection ) {
		music.connection_close( music.connection );
		music.connection_unref( music.connection );
		music.connection = NULL;
	}
	if ( music.library ) {
		dlclose( music.library );
		music.library = NULL;
	}
	music.state = state;
	musicNow[0] = '\0';
}

// loads the library and joins the bus of the desktop. Once: when it fails nothing is tried again.
static void Music_Open( void ) {
	DBusError error;

#define MUSIC_SYMBOL( name ) \
	*(void **)&music.name = dlsym( music.library, "dbus_" #name ); \
	if ( !music.name ) { Music_Close( MUSIC_FAILED ); return; }

	music.library = dlopen( "libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL );
	if ( !music.library ) {
		music.state = MUSIC_FAILED;
		return;
	}
	MUSIC_SYMBOL( error_init )
	MUSIC_SYMBOL( error_free )
	MUSIC_SYMBOL( bus_get_private )
	MUSIC_SYMBOL( connection_set_exit_on_disconnect )
	MUSIC_SYMBOL( connection_read_write )
	MUSIC_SYMBOL( connection_dispatch )
	MUSIC_SYMBOL( connection_send_with_reply )
	MUSIC_SYMBOL( connection_close )
	MUSIC_SYMBOL( connection_unref )
	MUSIC_SYMBOL( message_new_method_call )
	MUSIC_SYMBOL( message_append_args )
	MUSIC_SYMBOL( message_unref )
	MUSIC_SYMBOL( message_get_type )
	MUSIC_SYMBOL( message_iter_init )
	MUSIC_SYMBOL( message_iter_get_arg_type )
	MUSIC_SYMBOL( message_iter_recurse )
	MUSIC_SYMBOL( message_iter_next )
	MUSIC_SYMBOL( message_iter_get_basic )
	MUSIC_SYMBOL( pending_call_get_completed )
	MUSIC_SYMBOL( pending_call_steal_reply )
	MUSIC_SYMBOL( pending_call_unref )
#undef MUSIC_SYMBOL

	music.error_init( &error );
	music.connection = music.bus_get_private( DBUS_BUS_SESSION, &error );
	music.error_free( &error );
	if ( !music.connection ) {
		Music_Close( MUSIC_FAILED );
		return;
	}
	// the library ends the program when the bus goes away, unless it is told not to
	music.connection_set_exit_on_disconnect( music.connection, 0 );
	music.state = MUSIC_IDLE;
}

// sends a question with up to two words. The answer is looked for with the frames that follow.
static qboolean Music_Ask( const char *destination, const char *path, const char *iface, const char *method, const char *arg1, const char *arg2 ) {
	DBusMessage	*message = music.message_new_method_call( destination, path, iface, method );
	dbus_bool_t	sent;

	if ( !message ) {
		return qfalse;
	}
	if ( arg1 && arg2 ) {
		music.message_append_args( message, DBUS_TYPE_STRING, &arg1, DBUS_TYPE_STRING, &arg2, DBUS_TYPE_INVALID );
	}
	sent = music.connection_send_with_reply( music.connection, message, &music.pending, MUSIC_REPLY_MSEC );
	music.message_unref( message );
	return (qboolean)( sent && music.pending );
}

static qboolean Music_AskPlayer( const char *property, int state ) {
	if ( !Music_Ask( music.players[music.player], "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "Get",
			"org.mpris.MediaPlayer2.Player", property ) ) {
		return qfalse;
	}
	music.state = state;
	return qtrue;
}

// the next player is asked whether it plays. When there is none left, nothing plays.
static void Music_NextPlayer( void ) {
	for ( music.player++; music.player < music.numPlayers; music.player++ ) {
		if ( Music_AskPlayer( "PlaybackStatus", MUSIC_STATUS ) ) {
			return;
		}
	}
	musicNow[0] = '\0';
	music.state = MUSIC_IDLE;
}

// a text out of an answer, where the iterator stands on a text or on a variant that holds one
static qboolean Music_String( DBusMessageIter *iter, char *out, int outSize ) {
	DBusMessageIter	sub;
	const char		*text = NULL;

	if ( music.message_iter_get_arg_type( iter ) == DBUS_TYPE_VARIANT ) {
		music.message_iter_recurse( iter, &sub );
		iter = &sub;
	}
	if ( music.message_iter_get_arg_type( iter ) == DBUS_TYPE_ARRAY ) {
		// a list of texts (the artists): the first one
		DBusMessageIter list;

		music.message_iter_recurse( iter, &list );
		if ( music.message_iter_get_arg_type( &list ) != DBUS_TYPE_STRING ) {
			return qfalse;
		}
		music.message_iter_get_basic( &list, &text );
	} else if ( music.message_iter_get_arg_type( iter ) == DBUS_TYPE_STRING ) {
		music.message_iter_get_basic( iter, &text );
	}
	if ( !text ) {
		return qfalse;
	}
	Q_strncpyz( out, text, outSize );
	return qtrue;
}

// the answer to the question that is on its way has come
static void Music_Answer( DBusMessage *reply ) {
	DBusMessageIter	iter, list, entry, value;
	const qboolean	good = (qboolean)( music.message_get_type( reply ) == DBUS_MESSAGE_TYPE_METHOD_RETURN && music.message_iter_init( reply, &iter ) );
	char			text[128];

	switch ( music.state ) {
	case MUSIC_NAMES:
		// who is on the bus: the players of music have names that begin alike
		music.numPlayers = 0;
		if ( good && music.message_iter_get_arg_type( &iter ) == DBUS_TYPE_ARRAY ) {
			music.message_iter_recurse( &iter, &list );
			while ( music.message_iter_get_arg_type( &list ) == DBUS_TYPE_STRING && music.numPlayers < MUSIC_MAX_PLAYERS ) {
				const char *name = NULL;

				music.message_iter_get_basic( &list, &name );
				if ( name && !Q_strncmp( name, "org.mpris.MediaPlayer2.", 23 ) ) {
					Q_strncpyz( music.players[music.numPlayers++], name, sizeof( music.players[0] ) );
				}
				if ( !music.message_iter_next( &list ) ) {
					break;
				}
			}
		}
		music.player = -1;
		Music_NextPlayer();
		break;

	case MUSIC_STATUS:
		if ( good && Music_String( &iter, text, sizeof( text ) ) && !strcmp( text, "Playing" ) && Music_AskPlayer( "Metadata", MUSIC_META ) ) {
			break;
		}
		Music_NextPlayer();
		break;

	case MUSIC_META: {
		char title[128] = "", artist[128] = "", url[256] = "";
		const char *source = "";

		// a variant that holds a list of pairs: a name, and a variant with its value
		if ( good && music.message_iter_get_arg_type( &iter ) == DBUS_TYPE_VARIANT ) {
			music.message_iter_recurse( &iter, &list );
			if ( music.message_iter_get_arg_type( &list ) == DBUS_TYPE_ARRAY ) {
				music.message_iter_recurse( &list, &entry );
				while ( music.message_iter_get_arg_type( &entry ) == DBUS_TYPE_DICT_ENTRY ) {
					const char *key = NULL;

					music.message_iter_recurse( &entry, &value );
					if ( music.message_iter_get_arg_type( &value ) == DBUS_TYPE_STRING ) {
						music.message_iter_get_basic( &value, &key );
						if ( key && music.message_iter_next( &value ) ) {
							if ( !strcmp( key, "xesam:title" ) ) {
								Music_String( &value, title, sizeof( title ) );
							} else if ( !strcmp( key, "xesam:artist" ) ) {
								Music_String( &value, artist, sizeof( artist ) );
							} else if ( !strcmp( key, "xesam:url" ) ) {
								Music_String( &value, url, sizeof( url ) );
							}
						}
					}
					if ( !music.message_iter_next( &entry ) ) {
						break;
					}
				}
			}
		}
		if ( !title[0] ) {
			Music_NextPlayer();
			break;
		}
		if ( Q_stristr( music.players[music.player], "spotify" ) ) {
			source = "Spotify";
		} else if ( strstr( url, "music.youtube.com" ) ) {
			source = "YT Music";
		}
		Music_Set( source, artist, title );
		music.state = MUSIC_IDLE;
		break;
	}

	default:
		break;
	}
}

void CG_MusicFrame( void ) {
	const int	now = trap->Milliseconds();
	int			i;

	if ( !cg_shareMusic.integer ) {
		if ( music.state > MUSIC_FAILED ) {
			Music_Close( MUSIC_NONE );
		}
		return;
	}
	if ( music.state == MUSIC_NONE ) {
		Music_Open();
	}
	if ( music.state == MUSIC_FAILED ) {
		return;
	}

	// what came from the bus since the last frame, without waiting for more
	if ( !music.connection_read_write( music.connection, 0 ) ) {
		Music_Close( MUSIC_FAILED );
		return;
	}
	for ( i = 0; i < 16 && music.connection_dispatch( music.connection ) == DBUS_DISPATCH_DATA_REMAINS; i++ ) {
	}

	if ( music.pending ) {
		DBusMessage *reply;

		if ( !music.pending_call_get_completed( music.pending ) ) {
			return;
		}
		reply = music.pending_call_steal_reply( music.pending );
		music.pending_call_unref( music.pending );
		music.pending = NULL;
		if ( reply ) {
			Music_Answer( reply );
			music.message_unref( reply );
		} else {
			music.state = MUSIC_IDLE;
		}
		return;
	}

	if ( music.state != MUSIC_IDLE ) {
		music.state = MUSIC_IDLE;		// a question that could not be sent
	}
	if ( now < musicAskTime ) {
		return;
	}
	musicAskTime = now + MUSIC_ASK_MSEC;
	if ( Music_Ask( "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames", NULL, NULL ) ) {
		music.state = MUSIC_NAMES;
	}
}

void CG_MusicShutdown( void ) {
	if ( music.state > MUSIC_FAILED ) {
		Music_Close( MUSIC_NONE );
	}
}

#endif

/*
=================
CG_MusicNowPlaying

What plays, as UTF-8. qfalse: nothing, or nothing that can be found out.
=================
*/
qboolean CG_MusicNowPlaying( char *out, int outSize ) {
	Q_strncpyz( out, musicNow, outSize );
	return (qboolean)( musicNow[0] != '\0' );
}
