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

// bg_boombox_fetch.c -- the programs that find and fetch the songs of the boombox: yt-dlp, which looks a song up on
// YouTube and fetches it, and ffmpeg, which decodes it. Not from JA+, see docs/japlus/boombox.md and bg_boombox.h.
//
// They run beside the server and never in its way: what they say comes through pipes that do not block, and
// nothing here waits for them. No program is started through a shell. Of what a player typed only the address
// of a video (made anew here from its eleven characters) or the words to look for reach yt-dlp, behind "--",
// where they can not be an option. An address of anything but YouTube is refused: yt-dlp would fetch any
// address it is given, also one inside the network of the server.
//
// The programs to start are named by the host (boxTools_t), from cvars that can only be set while the server starts
// (CVAR_INIT): whoever can set a cvar later (rcon, an admin of the panel) can not make the server start a program of
// his choice. Built into the game module and into the masterserver.
//
// Not on Windows yet: a server there can not fetch songs and says so.

#ifndef _WIN32
	#include <errno.h>
	#include <fcntl.h>
	#include <signal.h>
	#include <unistd.h>
	#include <sys/types.h>
	#include <sys/wait.h>
#endif
#include "qcommon/q_shared.h"
#include "bg_boombox.h"
#include "bg_boombox_core.h"

#define BOX_LOOKUP_MSEC		45000		// a lookup that takes longer than this has failed
#define BOX_MAX_ARGS		48			// words a program is started with, its name among them
#define BOX_ORPHANS			16			// go-betweens that ended and wait to be reaped

/*
===============================================================================

Programs

A song is fetched by two programs, of which yt-dlp hands what it fetches to ffmpeg. They are started by a
go-between that makes a process group of its own, starts them in it and ends at once. So a song that is stopped
is one kill of that group, and with it whatever the programs started themselves (yt-dlp runs a JavaScript
program for YouTube).

The go-between is reaped only after the group was killed: until then its number, which is the one of the group,
can not be given to another process, and the kill can not reach anything else.

===============================================================================
*/

#ifndef _WIN32

typedef struct boxArgs_s {
	char		*argv[BOX_MAX_ARGS + 1];
	int			argc;
	char		store[2048];
	int			used;
	qboolean	full;			// something did not fit: the program is not started
} boxArgs_t;

static int	boxOrphans[BOX_ORPHANS];

static void Box_ArgsBegin( boxArgs_t *a ) {
	memset( a, 0, sizeof( *a ) );
}

static void Box_Arg( boxArgs_t *a, const char *word ) {
	const int len = (int)strlen( word ) + 1;

	if ( a->argc >= BOX_MAX_ARGS || a->used + len > (int)sizeof( a->store ) ) {
		a->full = qtrue;
		return;
	}
	memcpy( a->store + a->used, word, len );
	a->argv[a->argc++] = a->store + a->used;
	a->argv[a->argc] = NULL;
	a->used += len;
}

// the words of a cvar, as many as it has, split where it has spaces
static void Box_ArgWords( boxArgs_t *a, const char *words ) {
	char	word[256];
	int		len;

	while ( *words ) {
		while ( *words == ' ' || *words == '\t' )
			words++;
		for ( len = 0; *words && *words != ' ' && *words != '\t'; words++ ) {
			if ( len < (int)sizeof( word ) - 1 )
				word[len++] = *words;
		}
		if ( len ) {
			word[len] = '\0';
			Box_Arg( a, word );
		}
	}
}

// a pipe whose ends no other program the server starts gets, and that are not 0, 1 or 2 even where those were closed
static int Box_HighFd( int fd ) {
	int high;

	if ( fd < 0 || fd > 2 ) {
		if ( fd >= 0 )
			fcntl( fd, F_SETFD, FD_CLOEXEC );
		return fd;
	}
	high = fcntl( fd, F_DUPFD_CLOEXEC, 3 );
	close( fd );
	return high;
}

static qboolean Box_Pipe( int ends[2] ) {
	if ( pipe( ends ) != 0 ) {
		ends[0] = ends[1] = -1;
		return qfalse;
	}
	ends[0] = Box_HighFd( ends[0] );
	ends[1] = Box_HighFd( ends[1] );
	return ( ends[0] >= 0 && ends[1] >= 0 ) ? qtrue : qfalse;
}

static void Box_Close( int *fd ) {
	if ( *fd >= 0 ) {
		close( *fd );
		*fd = -1;
	}
}

// what a program started here begins with. Made before the fork: between it and the program only calls a signal
// handler may make are made.
static struct sigaction	boxDefaultAction;
static sigset_t			boxNoSignals;

/*
=================
Box_ChildExec

A child that becomes a program, with in, out and err as its 0, 1 and 2, and no other file or socket of the server:
it would keep the ports of the server bound after its end. The signals the server ignores are heard again (an
ignored signal stays ignored through exec), and none is held back.
=================
*/
static void Box_ChildExec( int in, int out, int err, char *const *argv, int maxFiles ) {
	int fd;

	if ( dup2( in, 0 ) < 0 || dup2( out, 1 ) < 0 || dup2( err, 2 ) < 0 )
		_exit( 127 );
	for ( fd = 3; fd < maxFiles; fd++ )
		close( fd );
	sigaction( SIGPIPE, &boxDefaultAction, NULL );
	sigaction( SIGHUP, &boxDefaultAction, NULL );
	sigaction( SIGINT, &boxDefaultAction, NULL );
	sigaction( SIGTERM, &boxDefaultAction, NULL );
	sigprocmask( SIG_SETMASK, &boxNoSignals, NULL );
	execv( argv[0], argv );
	_exit( 127 );
}

/*
=================
Box_ProcStart

Starts first, and second with what first writes as its input. What the last one writes can be read from p->out,
what both say from p->err. Neither waits for the other or for the server: nothing comes in.
=================
*/
static qboolean Box_ProcStart( boxProc_t *p, char *const *first, char *const *second, int now ) {
	int		out[2] = { -1, -1 }, err[2] = { -1, -1 }, link[2] = { -1, -1 };
	int		null = -1, maxFiles;
	long	most = sysconf( _SC_OPEN_MAX );
	pid_t	pid;

	memset( p, 0, sizeof( *p ) );
	p->out = p->err = -1;
	maxFiles = ( most > 0 && most < 65536 ) ? (int)most : 65536;
	memset( &boxDefaultAction, 0, sizeof( boxDefaultAction ) );
	boxDefaultAction.sa_handler = SIG_DFL;
	sigemptyset( &boxNoSignals );

	null = Box_HighFd( open( "/dev/null", O_RDWR ) );
	if ( null < 0 || !Box_Pipe( out ) || !Box_Pipe( err ) || ( second && !Box_Pipe( link ) ) ) {
		Box_Close( &null );
		Box_Close( &out[0] ); Box_Close( &out[1] );
		Box_Close( &err[0] ); Box_Close( &err[1] );
		Box_Close( &link[0] ); Box_Close( &link[1] );
		return qfalse;
	}

	pid = fork();
	if ( pid == 0 ) {
		// the go-between: a group of its own, the programs in it, and gone
		setpgid( 0, 0 );
		if ( fork() == 0 )
			Box_ChildExec( null, second ? link[1] : out[1], err[1], first, maxFiles );
		if ( second && fork() == 0 )
			Box_ChildExec( link[0], out[1], err[1], second, maxFiles );
		_exit( 0 );
	}
	Box_Close( &null );
	Box_Close( &out[1] );
	Box_Close( &err[1] );
	Box_Close( &link[0] );
	Box_Close( &link[1] );
	if ( pid < 0 ) {
		Box_Close( &out[0] );
		Box_Close( &err[0] );
		return qfalse;
	}
	// (the same from this side: a kill right away reaches the group already)
	setpgid( pid, pid );
	fcntl( out[0], F_SETFL, fcntl( out[0], F_GETFL ) | O_NONBLOCK );
	fcntl( err[0], F_SETFL, fcntl( err[0], F_GETFL ) | O_NONBLOCK );
	p->group = p->middle = (int)pid;
	p->out = out[0];
	p->err = err[0];
	p->started = now;
	return qtrue;
}

// What the programs wrote since the last call, up to size bytes. 0: nothing now, -1: they are through.
static int Box_ProcRead( boxProc_t *p, void *buffer, int size ) {
	ssize_t n;

	if ( p->out < 0 )
		return -1;
	n = read( p->out, buffer, (size_t)size );
	if ( n > 0 )
		return (int)n;
	if ( n < 0 && ( errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ) )
		return 0;
	Box_Close( &p->out );
	return -1;
}

// a whole line of what the programs said: the last one is kept, and an error is kept over what comes after it
static void Box_ProcLine( boxProc_t *p ) {
	p->errPart[p->errLen] = '\0';
	p->errLen = 0;
	if ( !p->errPart[0] )
		return;
	if ( !Q_strncmp( p->errPart, "ERROR:", 6 ) || Q_strncmp( p->errLine, "ERROR:", 6 ) )
		Q_strncpyz( p->errLine, p->errPart, sizeof( p->errLine ) );
}

// takes what the programs said since the last call
void Box_ProcErrors( boxProc_t *p ) {
	char	buffer[512];
	ssize_t	n, i;

	while ( p->err >= 0 ) {
		n = read( p->err, buffer, sizeof( buffer ) );
		if ( n < 0 && ( errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ) )
			return;
		if ( n <= 0 ) {
			Box_ProcLine( p );
			Box_Close( &p->err );
			return;
		}
		for ( i = 0; i < n; i++ ) {
			if ( buffer[i] == '\n' || buffer[i] == '\r' )
				Box_ProcLine( p );
			else if ( p->errLen < (int)sizeof( p->errPart ) - 1 )
				p->errPart[p->errLen++] = buffer[i];
		}
	}
}

// the go-betweens that ended by now
void Box_ReapAll( void ) {
	int i;

	for ( i = 0; i < BOX_ORPHANS; i++ ) {
		if ( boxOrphans[i] > 0 && waitpid( (pid_t)boxOrphans[i], NULL, WNOHANG ) != 0 )
			boxOrphans[i] = 0;
	}
}

// the programs end, whatever they were doing
void Box_ProcStop( boxProc_t *p ) {
	int i;

	if ( p->group > 0 )
		kill( -(pid_t)p->group, SIGKILL );
	Box_Close( &p->out );
	Box_Close( &p->err );
	// The go-between ended right after it started them: it waits as a zombie. In the moment after its start it may
	// not be one yet, then it is reaped later.
	if ( p->middle > 0 && waitpid( (pid_t)p->middle, NULL, WNOHANG ) == 0 ) {
		for ( i = 0; i < BOX_ORPHANS && boxOrphans[i] > 0; i++ )
			;
		if ( i < BOX_ORPHANS )
			boxOrphans[i] = p->middle;
	}
	p->group = p->middle = 0;
}

/*
=================
Box_ToolPath

Where the program a cvar names is: the name itself if it has a slash, else the first folder of PATH that has an
executable file of that name.
=================
*/
qboolean Box_ToolPath( const char *name, char *out, int outSize ) {
	const char	*path = getenv( "PATH" ), *end;

	if ( !name[0] )
		return qfalse;
	if ( strchr( name, '/' ) ) {
		Q_strncpyz( out, name, outSize );
		return ( access( out, X_OK ) == 0 ) ? qtrue : qfalse;
	}
	if ( !path )
		path = "/usr/local/bin:/usr/bin:/bin";
	for ( ; *path; path = *end ? end + 1 : end ) {
		end = strchr( path, ':' );
		if ( !end )
			end = path + strlen( path );
		if ( end == path )
			continue;
		snprintf( out, outSize, "%.*s/%s", (int)( end - path ), path, name );
		if ( access( out, X_OK ) == 0 )
			return qtrue;
	}
	return qfalse;
}

#else // _WIN32

qboolean	Box_ToolPath( const char *name, char *out, int outSize ) { (void)name; (void)out; (void)outSize; return qfalse; }
void		Box_ProcStop( boxProc_t *p ) { memset( p, 0, sizeof( *p ) ); p->out = p->err = -1; }
void		Box_ProcErrors( boxProc_t *p ) { (void)p; }
void		Box_ReapAll( void ) { }

#endif // _WIN32

/*
===============================================================================

What a player asks for

===============================================================================
*/

// the eleven characters of a video
static qboolean Box_VideoId( const char *s, int len ) {
	int i;

	if ( len != 11 )
		return qfalse;
	for ( i = 0; i < len; i++ ) {
		if ( !isalnum( (unsigned char)s[i] ) && s[i] != '-' && s[i] != '_' )
			return qfalse;
	}
	return qtrue;
}

// the length of the part of s up to the end or the first of stop
static int Box_Span( const char *s, const char *stop ) {
	return (int)strcspn( s, stop );
}

/*
=================
Box_Target

What yt-dlp is asked for, from what a player typed: a link of YouTube or YouTube Music (youtube.com/watch?v=,
youtu.be/, /shorts/, /live/, /embed/, with or without www., m. or music.) becomes the address of that video, and
anything else that looks like no address the words to look for on YouTube. qfalse: an address of something else.
=================
*/
qboolean Box_Target( const char *asked, char *target, int targetSize, char *why, int whySize ) {
	static const char *const subs[] = { "www.", "m.", "music." };
	static const char *const paths[] = { "/shorts/", "/live/", "/embed/", "/v/" };
	char		text[BOX_ASK], host[64];
	const char	*s, *rest, *arg, *id = NULL;
	int			hostLen, idLen = 0, i, len;
	qboolean	scheme;

	// without the colours, the characters that are no text, and the spaces at both ends
	Q_strncpyz( text, asked, sizeof( text ) );
	Q_StripColor( text );
	for ( i = len = 0; text[i]; i++ ) {
		if ( (unsigned char)text[i] >= ' ' && text[i] != '"' && text[i] != '\\' )
			text[len++] = text[i];
	}
	text[len] = '\0';
	while ( len > 0 && text[len - 1] == ' ' )
		text[--len] = '\0';
	for ( s = text; *s == ' '; s++ )
		;
	if ( !*s ) {
		Q_strncpyz( why, "say what to play: a link of YouTube or YouTube Music, or what to look for", whySize );
		return qfalse;
	}

	// An address has a scheme, or its first word has a dot and goes on with a path or begins with www.
	// "lofi beats" and "song.mp3" are words to look for.
	rest = s;
	scheme = qtrue;
	if ( !Q_stricmpn( rest, "https://", 8 ) )
		rest += 8;
	else if ( !Q_stricmpn( rest, "http://", 7 ) )
		rest += 7;
	else
		scheme = qfalse;
	hostLen = Box_Span( rest, "/?# " );
	Q_strncpyz( host, rest, Q_min( hostLen + 1, (int)sizeof( host ) ) );
	Q_strlwr( host );
	if ( !scheme && !( strchr( host, '.' ) && ( rest[hostLen] == '/' || rest[hostLen] == '?' || !Q_strncmp( host, "www.", 4 ) ) ) ) {
		snprintf( target, targetSize, "ytsearch1:%s", s );
		return qtrue;
	}

	for ( i = 0; i < (int)ARRAY_LEN( subs ); i++ ) {
		const int subLen = (int)strlen( subs[i] );

		if ( !Q_strncmp( host, subs[i], subLen ) )
			memmove( host, host + subLen, strlen( host + subLen ) + 1 );
	}
	rest += hostLen;
	if ( !strcmp( host, "youtu.be" ) ) {
		if ( *rest == '/' ) {
			id = rest + 1;
			idLen = Box_Span( id, "/?#&" );
		}
	} else if ( !strcmp( host, "youtube.com" ) ) {
		if ( !Q_stricmpn( rest, "/watch?", 7 ) || !Q_stricmpn( rest, "/watch/?", 8 ) ) {
			// v= among the arguments
			for ( arg = strchr( rest, '?' ) + 1; *arg; ) {
				const int argLen = Box_Span( arg, "&#" );

				if ( !Q_strncmp( arg, "v=", 2 ) ) {
					id = arg + 2;
					idLen = argLen - 2;
					break;
				}
				arg += argLen;
				if ( *arg )
					arg++;
			}
		} else {
			for ( i = 0; i < (int)ARRAY_LEN( paths ); i++ ) {
				if ( !Q_stricmpn( rest, paths[i], (int)strlen( paths[i] ) ) ) {
					id = rest + strlen( paths[i] );
					idLen = Box_Span( id, "/?#&" );
					break;
				}
			}
		}
	} else {
		Q_strncpyz( why, "only links of YouTube and YouTube Music are played", whySize );
		return qfalse;
	}
	if ( !id || !Box_VideoId( id, idLen ) ) {
		Q_strncpyz( why, "that link names no video", whySize );
		return qfalse;
	}
	snprintf( target, targetSize, "https://www.youtube.com/watch?v=%.11s", id );
	return qtrue;
}

#ifndef _WIN32

// the words of yt-dlp for a lookup and for a fetch alike, after its name
static void Box_ArgsYtdlp( boxArgs_t *a, const char *path, const boxTools_t *tools ) {
	Box_Arg( a, path );
	Box_Arg( a, "--ignore-config" );
	Box_Arg( a, "--no-playlist" );
	Box_Arg( a, "--no-warnings" );
	Box_Arg( a, "--encoding" );
	Box_Arg( a, "utf-8" );
	Box_Arg( a, "--socket-timeout" );
	Box_Arg( a, "15" );
	Box_Arg( a, "-f" );
	Box_Arg( a, "bestaudio" );
	// what the owner of the server adds (a JavaScript program other than deno, cookies, a proxy)
	Box_ArgWords( a, tools->ytdlpArgs );
}

#endif

// What yt-dlp said on its way out, as a reason for a player: without "ERROR: " and the name of the site and the
// video in front of it.
static void Box_Why( const boxProc_t *p, const char *otherwise, char *why, int whySize ) {
	const char *s = p->errLine, *colon;

	if ( !*s ) {
		Q_strncpyz( why, otherwise, whySize );
		return;
	}
	if ( !Q_strncmp( s, "ERROR:", 6 ) )
		s += 6;
	while ( *s == ' ' )
		s++;
	if ( *s == '[' && ( colon = strstr( s, ": " ) ) != NULL )
		s = colon + 2;
	Q_strncpyz( why, s, whySize );
}

/*
=================
Box_LookupStart

yt-dlp looks up what a player asked for (a target of Box_Target()) and prints four lines: the video, its length in
seconds, whether it is live, its title. Nothing is fetched yet.
=================
*/
qboolean Box_LookupStart( boxLookup_t *l, const boxTools_t *tools, const char *target, int now, char *why, int whySize ) {
#ifndef _WIN32
	boxArgs_t	a;
	char		path[MAX_OSPATH];

	if ( !Box_ToolPath( tools->ytdlp, path, sizeof( path ) ) ) {
		Q_strncpyz( why, "this server can not fetch songs: it has no yt-dlp", whySize );
		return qfalse;
	}
	Box_ArgsBegin( &a );
	Box_ArgsYtdlp( &a, path, tools );
	Box_Arg( &a, "--print" );
	Box_Arg( &a, "id" );
	Box_Arg( &a, "--print" );
	Box_Arg( &a, "duration" );
	Box_Arg( &a, "--print" );
	Box_Arg( &a, "live_status" );
	Box_Arg( &a, "--print" );
	Box_Arg( &a, "title" );
	Box_Arg( &a, "--" );
	Box_Arg( &a, target );
	if ( a.full ) {
		Q_strncpyz( why, "the words for yt-dlp are too many (g_boomboxYtdlpArgs)", whySize );
		return qfalse;
	}
	l->len = 0;
	l->text[0] = '\0';
	if ( !Box_ProcStart( &l->proc, a.argv, NULL, now ) ) {
		Q_strncpyz( why, "this server could not start yt-dlp", whySize );
		return qfalse;
	}
	l->busy = qtrue;
	return qtrue;
#else
	(void)l; (void)tools; (void)target; (void)now;
	Q_strncpyz( why, "a server on Windows can not fetch songs yet", whySize );
	return qfalse;
#endif
}

// the title as it is sent to the clients: no quotes, no colours, nothing that is no text
static void Box_CleanTitle( const char *in, char *out, int outSize ) {
	int len = 0;

	for ( ; *in && len < outSize - 1; in++ ) {
		if ( *in == '"' )
			out[len++] = '\'';
		else if ( (unsigned char)*in >= ' ' && *in != '\\' && *in != '^' )
			out[len++] = *in;
	}
	out[len] = '\0';
}

/*
=================
Box_LookupFrame

Once a frame for a lookup that runs. 0: it still does. 1: song is what was found. -1: nothing that can be played
was, why says what.
=================
*/
int Box_LookupFrame( boxLookup_t *l, boxSong_t *song, int now, char *why, int whySize ) {
#ifndef _WIN32
	char	*line[4], *s;
	int		n, lines;

	Box_ProcErrors( &l->proc );
	for ( ;; ) {
		n = Box_ProcRead( &l->proc, l->text + l->len, (int)sizeof( l->text ) - 1 - l->len );
		if ( n > 0 ) {
			l->len += n;
			l->text[l->len] = '\0';
			if ( l->len < (int)sizeof( l->text ) - 1 )
				continue;
			n = -1;		// more than four lines say: the rest does not matter
		}
		if ( n == 0 ) {
			if ( now - l->proc.started < BOX_LOOKUP_MSEC )
				return 0;
			Box_ProcStop( &l->proc );
			l->busy = qfalse;
			Q_strncpyz( why, "YouTube did not answer in time", whySize );
			return -1;
		}
		break;
	}
	Box_ProcErrors( &l->proc );
	Box_ProcStop( &l->proc );
	l->busy = qfalse;

	// the four lines
	for ( lines = 0, s = l->text; lines < 4 && *s; lines++ ) {
		line[lines] = s;
		s += strcspn( s, "\r\n" );
		if ( *s ) {
			*s++ = '\0';
			while ( *s == '\r' || *s == '\n' )
				s++;
		}
	}
	if ( lines < 4 || !Box_VideoId( line[0], (int)strlen( line[0] ) ) ) {
		Box_Why( &l->proc, "nothing was found", why, whySize );
		return -1;
	}
	if ( !Q_stricmp( line[2], "is_live" ) || !Q_stricmp( line[2], "is_upcoming" ) || !Q_stricmp( line[2], "post_live" ) ) {
		Q_strncpyz( why, "live streams are not played", whySize );
		return -1;
	}
	if ( !Q_stricmp( line[1], "NA" ) || atof( line[1] ) <= 0.0 ) {
		Q_strncpyz( why, "it has no length", whySize );
		return -1;
	}
	memset( song, 0, sizeof( *song ) );
	Q_strncpyz( song->id, line[0], sizeof( song->id ) );
	song->seconds = (int)( atof( line[1] ) + 0.5 );
	Box_CleanTitle( line[3], song->title, sizeof( song->title ) );
	if ( !song->title[0] )
		Q_strncpyz( song->title, song->id, sizeof( song->title ) );
	return 1;
#else
	(void)l; (void)song; (void)now; (void)why; (void)whySize;
	return -1;
#endif
}

/*
===============================================================================

The sound of a song

===============================================================================
*/

int Box_StreamQueued( const boxStream_t *s ) {
	return (int)( s->write - s->read );
}

/*
=================
Box_StreamStart

yt-dlp fetches the video and hands it to ffmpeg, which makes one channel of 16 bit at BOX_RATE of it. fromSample:
where in the song to begin (ffmpeg decodes what is before and drops it), for a song whose fetch broke off.
keepRing: what was fetched before stays, and what comes is put behind it.
=================
*/
qboolean Box_StreamStart( boxStream_t *s, const boxTools_t *tools, const char *id, int fromSample, qboolean keepRing, int now,
	char *why, int whySize ) {
#ifndef _WIN32
	boxArgs_t	fetch, decode;
	char		ytdlp[MAX_OSPATH], ffmpeg[MAX_OSPATH];

	Box_ProcStop( &s->proc );
	if ( !keepRing )
		s->read = s->write = 0;
	s->fetched = fromSample;
	s->odd = -1;
	s->ended = qtrue;
	if ( !Box_ToolPath( tools->ytdlp, ytdlp, sizeof( ytdlp ) ) ) {
		Q_strncpyz( why, "this server can not fetch songs: it has no yt-dlp", whySize );
		return qfalse;
	}
	if ( !Box_ToolPath( tools->ffmpeg, ffmpeg, sizeof( ffmpeg ) ) ) {
		Q_strncpyz( why, "this server can not play songs: it has no ffmpeg", whySize );
		return qfalse;
	}

	Box_ArgsBegin( &fetch );
	Box_ArgsYtdlp( &fetch, ytdlp, tools );
	Box_Arg( &fetch, "--quiet" );
	Box_Arg( &fetch, "--no-progress" );
	Box_Arg( &fetch, "-o" );
	Box_Arg( &fetch, "-" );
	Box_Arg( &fetch, "--" );
	Box_Arg( &fetch, Box_Va( "https://www.youtube.com/watch?v=%s", id ) );

	Box_ArgsBegin( &decode );
	Box_Arg( &decode, ffmpeg );
	Box_Arg( &decode, "-nostdin" );
	Box_Arg( &decode, "-hide_banner" );
	Box_Arg( &decode, "-loglevel" );
	Box_Arg( &decode, "error" );
	Box_Arg( &decode, "-i" );
	Box_Arg( &decode, "pipe:0" );
	if ( fromSample > 0 ) {
		// (behind -i: decoded and dropped, which a pipe allows, and to the sample)
		Box_Arg( &decode, "-ss" );
		Box_Arg( &decode, Box_Va( "%.3f", (double)fromSample / BOX_RATE ) );
	}
	Box_Arg( &decode, "-vn" );
	Box_Arg( &decode, "-ac" );
	Box_Arg( &decode, "1" );
	Box_Arg( &decode, "-ar" );
	Box_Arg( &decode, Box_Va( "%i", BOX_RATE ) );
	Box_Arg( &decode, "-f" );
	Box_Arg( &decode, "s16le" );
	Box_Arg( &decode, "pipe:1" );

	if ( fetch.full || decode.full ) {
		Q_strncpyz( why, "the words for yt-dlp are too many (g_boomboxYtdlpArgs)", whySize );
		return qfalse;
	}
	if ( !Box_ProcStart( &s->proc, fetch.argv, decode.argv, now ) ) {
		Q_strncpyz( why, "this server could not start yt-dlp and ffmpeg", whySize );
		return qfalse;
	}
	s->ended = qfalse;
	return qtrue;
#else
	(void)tools; (void)id; (void)keepRing; (void)now;
	Q_strncpyz( why, "a server on Windows can not fetch songs yet", whySize );
	return qfalse;
#endif
}

// takes what ffmpeg made since the last call, as far as the ring has room. When it is full the programs wait.
void Box_StreamFill( boxStream_t *s ) {
#ifndef _WIN32
	byte	buffer[16384];
	int		want, n, i;

	Box_ProcErrors( &s->proc );
	while ( !s->ended ) {
		want = ( BOX_RING - Box_StreamQueued( s ) ) * 2 - ( s->odd >= 0 ? 1 : 0 );
		if ( want <= 0 )
			break;
		if ( want > (int)sizeof( buffer ) )
			want = (int)sizeof( buffer );
		n = Box_ProcRead( &s->proc, buffer, want );
		if ( n == 0 )
			break;
		if ( n < 0 ) {
			s->ended = qtrue;
			Box_ProcErrors( &s->proc );
			break;
		}
		// the low byte of a sample first
		for ( i = 0; i < n; i++ ) {
			if ( s->odd < 0 ) {
				s->odd = buffer[i];
				continue;
			}
			s->ring[s->write % BOX_RING] = (short)( s->odd | ( buffer[i] << 8 ) );
			s->write++;
			s->fetched++;
			s->odd = -1;
		}
	}
#endif
}

// up to count samples of the song, as many as are there
int Box_StreamTake( boxStream_t *s, short *out, int count ) {
	int i;

	if ( count > Box_StreamQueued( s ) )
		count = Box_StreamQueued( s );
	for ( i = 0; i < count; i++ )
		out[i] = s->ring[( s->read + i ) % BOX_RING];
	s->read += count;
	return count;
}

void Box_StreamStop( boxStream_t *s ) {
	Box_ProcStop( &s->proc );
	s->read = s->write = 0;
	s->fetched = 0;
	s->odd = -1;
	s->ended = qtrue;
}

// (for Box_Why() of the programs of a song that broke off)
void Box_StreamWhy( const boxStream_t *s, const char *otherwise, char *why, int whySize ) {
	Box_Why( &s->proc, otherwise, why, whySize );
}
