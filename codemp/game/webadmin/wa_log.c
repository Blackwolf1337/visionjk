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

// wa_log.c -- what was said and what was written: the chat lines and the log of the game, each
// kept as a ring that the page of the web admin panel reads on from where it was.

#include "game/webadmin/wa_local.h"

// the newest chat lines, a ring. Line number n is in chatLines[(n - 1) % WA_MAX_CHAT].
#define WA_MAX_CHAT		128

typedef struct waChatLine_s {
	int		seq;
	char	time[12];				// server clock, HH:MM:SS
	int		clientNum;				// -1: server console or the panel
	char	name[MAX_NETNAME];
	char	team[12];
	char	to[MAX_NETNAME];		// who a tell went to
	char	mode[8];				// all, team, tell, clan, admin, server, panel
	char	text[MAX_SAY_TEXT];
} waChatLine_t;

static waChatLine_t	chatLines[WA_MAX_CHAT];
static int			chatSeq;		// number of the newest line, the first one is 1
static int			chatEpoch;		// start time of this map. The module is loaded again for each one, the numbers start over

/*
==============================================================================

Chat

Not in JKAdmin. The game hands every chat line that went out to WebAdmin_Chat,
the page asks for the lines it does not have yet.

==============================================================================
*/

// The engine loads the module again for every map and map_restart, the chat lines of the last one are gone.
// The lines of this map have its start time with them, by which a page tells them from the ones it has.
void WA_ChatInit( void ) {
	qtime_t now;

	chatEpoch = trap->RealTime( &now );
}

void WebAdmin_Chat( const char *mode, const gentity_t *from, const gentity_t *to, const char *text ) {
	waChatLine_t	*line;
	qtime_t			now;

	if ( !WA_Running() )
		return;

	line = &chatLines[chatSeq % WA_MAX_CHAT];
	memset( line, 0, sizeof( *line ) );
	line->seq = ++chatSeq;

	trap->RealTime( &now );
	Com_sprintf( line->time, sizeof( line->time ), "%02i:%02i:%02i", now.tm_hour, now.tm_min, now.tm_sec );

	Q_strncpyz( line->mode, mode, sizeof( line->mode ) );
	Q_strncpyz( line->text, text, sizeof( line->text ) );
	if ( from && from->client ) {
		line->clientNum = (int)( from-g_entities );
		Q_strncpyz( line->name, from->client->pers.netname, sizeof( line->name ) );
		Q_strncpyz( line->team, WA_TeamName( from->client->sess.sessionTeam ), sizeof( line->team ) );
	} else {
		line->clientNum = -1;
		Q_strncpyz( line->name, !strcmp( mode, "panel" ) ? S_COLOR_RED "Admin" : "server", sizeof( line->name ) );
	}
	if ( to && to->client )
		Q_strncpyz( line->to, to->client->pers.netname, sizeof( line->to ) );
}

// the lines after ?since=<number of the newest line the page has>&epoch=<the epoch it got them with>
void WA_ApiChat( waConnection_t *c, const char *query ) {
	waBuffer_t	b = { 0 };
	char		arg[16];
	int			since = 0, seq;

	if ( WA_FormValue( query, "since", arg, sizeof( arg ) ) )
		since = atoi( arg );
	// the numbers started again since the page asked last, it gets everything
	if ( !WA_FormValue( query, "epoch", arg, sizeof( arg ) ) || atoi( arg ) != chatEpoch || since < 0 || since > chatSeq )
		since = 0;
	if ( since < chatSeq - WA_MAX_CHAT )
		since = chatSeq - WA_MAX_CHAT;

	WA_BufPrintf( &b, "{\"epoch\":%d,\"seq\":%d,\"messages\":[", chatEpoch, chatSeq );
	for ( seq = since + 1; seq <= chatSeq; seq++ ) {
		const waChatLine_t *line = &chatLines[( seq - 1 ) % WA_MAX_CHAT];

		WA_BufPrintf( &b, "%s{\"seq\":%d,\"time\":\"%s\",\"id\":%d,\"mode\":\"%s\",\"team\":\"%s\",\"name\":",
			seq > since + 1 ? "," : "", line->seq, line->time, line->clientNum, line->mode, line->team );
		WA_BufJsonString( &b, line->name );
		WA_BufAppend( &b, ",\"to\":", 6 );
		WA_BufJsonString( &b, line->to );
		WA_BufAppend( &b, ",\"text\":", 8 );
		WA_BufJsonString( &b, line->text );
		WA_BufAppend( &b, "}", 1 );
	}
	WA_BufAppend( &b, "]}", 2 );

	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

/*
=================
WebAdmin_Log, WA_ApiLog

Not in JKAdmin. The log of the game for the view of the page that follows it. The engine writes the file of the
log in blocks unless g_logSync is set, so its end is not on the disk yet when it is asked for: the panel keeps
the lines itself, as the game hands them over where it writes them (G_LogPrintf). The module is loaded anew
with every map, so the view of a new map starts with the end of the file, which the map before left complete.

GET /api/log?from=: the log from a place on. Without from, or with a place the panel does not have (any more):
its last part. next is where to ask from the next time.
=================
*/
#define WA_LOG_RING		65536		// bytes of the log the panel keeps
#define WA_LOG_CHUNK	32768		//	and gives in one answer
#define WA_LOG_TAIL		8192		// what a view starts with, and what is taken from the file when a map starts

static char		logRing[WA_LOG_RING];
static int		logLen;			// bytes in it
static double	logTotal;		// bytes that ever went into it: the place of what comes next
static qboolean	logPreloaded;

static void WA_LogAppend( const char *text, int len ) {
	if ( len <= 0 || len >= WA_LOG_RING / 2 )
		return;
	if ( logLen + len > WA_LOG_RING ) {
		// full: the older half goes
		memmove( logRing, logRing + logLen - WA_LOG_RING / 2, WA_LOG_RING / 2 );
		logLen = WA_LOG_RING / 2;
	}
	memcpy( logRing + logLen, text, len );
	logLen += len;
	logTotal += len;
}

// the end of the file of the log as the maps before left it, from the first whole line on
static void WA_LogPreload( void ) {
	static char	tail[WA_LOG_TAIL + 1];
	char		path[MAX_OSPATH], *line;
	qboolean	exists, isDir;
	double		size = 0;
	int			len = 0;
	FILE		*f;

	if ( logPreloaded )
		return;
	logPreloaded = qtrue;
	if ( !g_log.string[0] || !WA_FsResolve( qfalse, g_log.string, qfalse, path, sizeof( path ), &exists )
		|| !WA_FsStat( path, &isDir, &size, NULL ) || isDir )
		return;
	f = fopen( path, "rb" );
	if ( !f )
		return;
	if ( !fseek( f, size > WA_LOG_TAIL ? (long)size - WA_LOG_TAIL : 0, SEEK_SET ) )
		len = (int)fread( tail, 1, WA_LOG_TAIL, f );
	fclose( f );
	tail[len] = '\0';
	line = ( size > WA_LOG_TAIL ) ? strchr( tail, '\n' ) : tail;
	if ( line && line != tail )
		line++;
	if ( line )
		WA_LogAppend( line, (int)strlen( line ) );
}

void WebAdmin_Log( const char *line ) {
	// the panel is only there with a password, and the first lines of a map come before it listens
	if ( !vs_webPassword.string[0] )
		return;
	WA_LogPreload();
	WA_LogAppend( line, (int)strlen( line ) );
}

void WA_ApiLog( waConnection_t *c, const char *query ) {
	static char	data[WA_LOG_CHUNK + 1];
	char		value[32];
	double		start, from = -1;
	int			offset, len;
	waBuffer_t	b = { 0 };

	WA_LogPreload();
	start = logTotal - logLen;
	if ( WA_FormValue( query, "from", value, sizeof( value ) ) && value[0] )
		from = atof( value );
	if ( from < start || from > logTotal )
		from = ( logLen > WA_LOG_TAIL ) ? logTotal - WA_LOG_TAIL : start;

	offset = (int)( from - start );
	len = logLen - offset;
	if ( len > WA_LOG_CHUNK )
		len = WA_LOG_CHUNK;
	memcpy( data, logRing + offset, len );
	data[len] = '\0';

	WA_BufPrintf( &b, "{\"from\":%.0f,\"next\":%.0f,\"size\":%.0f,\"text\":", from, from + len, logTotal );
	WA_BufJsonString( &b, data );
	WA_BufAppend( &b, "}", 1 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

/*
=================
WA_ApiConsole, WA_ApiCommand

Not in JKAdmin. The console of the server for the page: what it prints, and a line to run on it. The module
only sees its own prints, so what the engine and its commands print is read from the file the engine keeps of
its console (qconsole.log in the server folder, written while logfile is set). The first request sets
logfile 2 when it is off: every print is in the file at once then.

GET /api/console?from=: the file from a place on, as /api/log gives the log. Without from, or with a place
behind its end (the server was started again, the file is a new one): its last part.

POST /api/command (cmd=): one line for the console, run in the next frame as if it was typed there. Not more
than "Save and execute" of the config could do already. What follows a word with "pass" in it is left out of
the security log and of the echo on the console, which is in a file too.
=================
*/
#define WA_CONSOLE_FILE	"qconsole.log"
#define WA_COMMAND_MAX	480

// the end of a text file of the game folder that grows, from a place on: the console, and what the intrusion
// detection of the engine wrote down
static void WA_TailFile( waConnection_t *c, const char *query, const char *file ) {
	static char	data[WA_LOG_CHUNK + 1];
	char		path[MAX_OSPATH], value[32];
	qboolean	exists, isDir;
	double		size = 0, from = -1;
	int			len = 0;
	waBuffer_t	b = { 0 };
	FILE		*f;

	if ( !WA_FsResolve( qfalse, file, qfalse, path, sizeof( path ), &exists ) || !WA_FsStat( path, &isDir, &size, NULL ) || isDir )
		size = 0;
	if ( WA_FormValue( query, "from", value, sizeof( value ) ) && value[0] )
		from = atof( value );
	if ( from < 0 || from > size )
		from = ( size > WA_LOG_TAIL ) ? size - WA_LOG_TAIL : 0;

	if ( size > from && ( f = fopen( path, "rb" ) ) != NULL ) {
		if ( !fseek( f, (long)from, SEEK_SET ) )
			len = (int)fread( data, 1, WA_LOG_CHUNK, f );
		fclose( f );
	}
	data[len] = '\0';

	WA_BufPrintf( &b, "{\"from\":%.0f,\"next\":%.0f,\"size\":%.0f,\"text\":", from, from + len, size );
	WA_BufJsonString( &b, data );
	WA_BufAppend( &b, "}", 1 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

void WA_ApiConsole( waConnection_t *c, const char *query ) {
	if ( !trap->Cvar_VariableIntegerValue( "logfile" ) )
		trap->Cvar_Set( "logfile", "2" );
	WA_TailFile( c, query, WA_CONSOLE_FILE );
}

// GET /api/ids: ids.log, what the intrusion detection of the server noticed (server/sv_ids.cpp)
void WA_ApiIds( waConnection_t *c, const char *query ) {
	WA_TailFile( c, query, "ids.log" );
}

void WA_ApiCommand( waConnection_t *c, const char *body ) {
	char	cmd[WA_COMMAND_MAX + 2], shown[WA_COMMAND_MAX + 2];
	char	*in, *out, *secret;

	WA_FormValue( body, "cmd", cmd, sizeof( cmd ) );
	WA_Utf8ToLatin1( cmd );
	// one line of the console: nothing that ends it
	for ( in = out = cmd; *in; in++ ) {
		if ( (unsigned char)*in >= ' ' && ( out != cmd || *in != ' ' ) )
			*out++ = *in;
	}
	*out = '\0';
	if ( !cmd[0] ) {
		WA_ActionResult( c, qfalse, "no command" );
		return;
	}
	if ( strlen( cmd ) > WA_COMMAND_MAX ) {
		WA_ActionResult( c, qfalse, va( "a command is %d characters at most", WA_COMMAND_MAX ) );
		return;
	}

	Q_strncpyz( shown, cmd, sizeof( shown ) );
	secret = (char *)Q_stristr( shown, "pass" );
	if ( secret && ( secret = strchr( secret, ' ' ) ) != NULL )
		Q_strncpyz( secret, " ...", (int)( sizeof( shown ) - ( secret - shown ) ) );
	WA_LogUse( c, va( "command %s", shown ) );
	trap->Print( "]%s\n", shown );
	trap->SendConsoleCommand( EXEC_APPEND, va( "%s\n", cmd ) );
	WA_ActionResult( c, qtrue, "sent to the console" );
}
