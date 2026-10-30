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


// sys_http_curl.cpp -- Sys_HttpGet() and Sys_HttpWebSocket() with the libcurl of the system, see sys_http.h.
// Linux and macOS.
//
// The engine is not linked against libcurl and is built without its headers: the library is loaded when the
// first question is asked, and the few calls and numbers of it that are used are written down here. They are
// part of its binary interface, which has not changed since libcurl 7.32 (2013), the oldest this needs.
// A system without the library has no update check, and that is all. The WebSocket of the TV needs libcurl 7.86
// (2022) built with WebSockets, which every libcurl since 8.11 is.

#include <pthread.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "sys_loadlib.h"
#include "sys_http.h"

#ifdef MACOS_X
	#define HTTP_LIBRARY	"libcurl.4.dylib"
#else
	#define HTTP_LIBRARY	"libcurl.so.4"
#endif

// curl/curl.h: CURL_GLOBAL_DEFAULT, some of CURLoption, CURLINFO_RESPONSE_CODE, CURLPROTO_HTTP | CURLPROTO_HTTPS
#define HTTP_GLOBAL_DEFAULT		3L
#define HTTP_OPT_WRITEDATA		10001
#define HTTP_OPT_URL			10002
#define HTTP_OPT_USERAGENT		10018
#define HTTP_OPT_XFERINFODATA	10057
#define HTTP_OPT_WRITEFUNCTION	20011
#define HTTP_OPT_XFERINFOFUNCTION 20219
#define HTTP_OPT_LOW_SPEED_LIMIT 19
#define HTTP_OPT_LOW_SPEED_TIME	20
#define HTTP_OPT_NOPROGRESS		43
#define HTTP_OPT_FOLLOWLOCATION	52
#define HTTP_OPT_SSL_VERIFYPEER	64
#define HTTP_OPT_CONNECTTIMEOUT	78
#define HTTP_OPT_SSL_VERIFYHOST	81
#define HTTP_OPT_NOSIGNAL		99
#define HTTP_OPT_PROTOCOLS		181
#define HTTP_OPT_PROTOCOLS_STR	10318
#define HTTP_INFO_RESPONSE_CODE	0x200002
#define HTTP_PROTO_WEB			3L
// curl/websockets.h: the flags of struct curl_ws_frame
#define HTTP_WS_BINARY			( 1 << 1 )

#define HTTP_CONNECT_SECONDS	15L
#define HTTP_SILENT_SECONDS		30L		// a line that brings nothing for this long has failed

typedef int ( *httpGlobalInit_t )( long flags );
typedef void *( *httpEasyInit_t )( void );
typedef int ( *httpEasySetopt_t )( void *handle, int option, ... );
typedef int ( *httpEasyPerform_t )( void *handle );
typedef int ( *httpEasyGetinfo_t )( void *handle, int info, ... );
typedef void ( *httpEasyCleanup_t )( void *handle );
typedef const char *( *httpEasyStrerror_t )( int code );

// curl/websockets.h: struct curl_ws_frame, the frame a piece that libcurl hands over belongs to
typedef struct httpWsFrame_s {
	int			age;
	int			flags;
	long long	offset;
	long long	bytesleft;
	size_t		len;
} httpWsFrame_t;
typedef const httpWsFrame_t *( *httpWsMeta_t )( void *handle );

static struct {
	qboolean			loaded;
	httpEasyInit_t		easyInit;
	httpEasySetopt_t	easySetopt;
	httpEasyPerform_t	easyPerform;
	httpEasyGetinfo_t	easyGetinfo;
	httpEasyCleanup_t	easyCleanup;
	httpEasyStrerror_t	easyStrerror;
	httpWsMeta_t		wsMeta;			// NULL: a libcurl older than 7.86, no WebSocket
} http;

typedef struct httpRequest_s {
	sysHttpSink_t		sink;
	void				*ctx;
	const volatile int	*cancel;
	qboolean			refused;		// the sink did not want more
	void				*handle;
} httpRequest_t;

// The library is loaded and started by the thread that asks first, once: the TV (Sys_HttpWebSocket) may ask
// while the updater or a download does.
static pthread_once_t httpOnce = PTHREAD_ONCE_INIT;

static void Sys_HttpLoadOnce( void ) {
	void				*library;
	httpGlobalInit_t	globalInit;

	library = Sys_LoadLibrary( HTTP_LIBRARY );
	if ( !library )
		return;
	globalInit = (httpGlobalInit_t)Sys_LoadFunction( library, "curl_global_init" );
	http.easyInit = (httpEasyInit_t)Sys_LoadFunction( library, "curl_easy_init" );
	http.easySetopt = (httpEasySetopt_t)Sys_LoadFunction( library, "curl_easy_setopt" );
	http.easyPerform = (httpEasyPerform_t)Sys_LoadFunction( library, "curl_easy_perform" );
	http.easyGetinfo = (httpEasyGetinfo_t)Sys_LoadFunction( library, "curl_easy_getinfo" );
	http.easyCleanup = (httpEasyCleanup_t)Sys_LoadFunction( library, "curl_easy_cleanup" );
	http.easyStrerror = (httpEasyStrerror_t)Sys_LoadFunction( library, "curl_easy_strerror" );
	http.wsMeta = (httpWsMeta_t)Sys_LoadFunction( library, "curl_ws_meta" );
	if ( !globalInit || !http.easyInit || !http.easySetopt || !http.easyPerform || !http.easyGetinfo || !http.easyCleanup
		|| !http.easyStrerror || globalInit( HTTP_GLOBAL_DEFAULT ) != 0 ) {
		Sys_UnloadLibrary( library );
		return;
	}
	http.loaded = qtrue;
}

static qboolean Sys_HttpLoad( void ) {
	pthread_once( &httpOnce, Sys_HttpLoadOnce );
	return http.loaded;
}

static size_t Sys_HttpWrite( char *data, size_t size, size_t count, void *user ) {
	httpRequest_t *request = (httpRequest_t *)user;

	if ( *request->cancel || !request->sink( request->ctx, data, size * count ) ) {
		request->refused = qtrue;
		return 0;		// less than it was given: libcurl ends the transfer
	}
	return size * count;
}

// called about once a second also while nothing comes in. Anything but 0 ends the transfer.
static int Sys_HttpProgress( void *user, long long dlTotal, long long dlNow, long long ulTotal, long long ulNow ) {
	(void)dlTotal; (void)dlNow; (void)ulTotal; (void)ulNow;
	return *( (httpRequest_t *)user )->cancel ? 1 : 0;
}

qboolean Sys_HttpGet( const char *url, sysHttpSink_t sink, void *ctx, const volatile int *cancel, char *error, int errorSize, int *status ) {
	httpRequest_t	request;
	void			*handle;
	long			answer = 0;
	int				code;

	error[0] = '\0';
	if ( status ) {
		*status = 0;
	}
	if ( !Sys_HttpLoad() ) {
		Com_sprintf( error, errorSize, "this system has no %s", HTTP_LIBRARY );
		return qfalse;
	}
	handle = http.easyInit();
	if ( !handle ) {
		Q_strncpyz( error, "libcurl does not start", errorSize );
		return qfalse;
	}
	request.sink = sink;
	request.ctx = ctx;
	request.cancel = cancel;
	request.refused = qfalse;

	http.easySetopt( handle, HTTP_OPT_URL, url );
	http.easySetopt( handle, HTTP_OPT_USERAGENT, "VisionJK" );
	http.easySetopt( handle, HTTP_OPT_PROTOCOLS, HTTP_PROTO_WEB );
	http.easySetopt( handle, HTTP_OPT_FOLLOWLOCATION, 0L );
	// what libcurl does by itself, said here so that it is said: the certificate has to be one the system
	// trusts and has to be the one of the host that was asked
	http.easySetopt( handle, HTTP_OPT_SSL_VERIFYPEER, 1L );
	http.easySetopt( handle, HTTP_OPT_SSL_VERIFYHOST, 2L );
	// (libcurl must not raise a signal for a timeout in a program with threads)
	http.easySetopt( handle, HTTP_OPT_NOSIGNAL, 1L );
	http.easySetopt( handle, HTTP_OPT_CONNECTTIMEOUT, HTTP_CONNECT_SECONDS );
	http.easySetopt( handle, HTTP_OPT_LOW_SPEED_LIMIT, 1L );
	http.easySetopt( handle, HTTP_OPT_LOW_SPEED_TIME, HTTP_SILENT_SECONDS );
	http.easySetopt( handle, HTTP_OPT_WRITEFUNCTION, Sys_HttpWrite );
	http.easySetopt( handle, HTTP_OPT_WRITEDATA, &request );
	http.easySetopt( handle, HTTP_OPT_NOPROGRESS, 0L );
	http.easySetopt( handle, HTTP_OPT_XFERINFOFUNCTION, Sys_HttpProgress );
	http.easySetopt( handle, HTTP_OPT_XFERINFODATA, &request );

	code = http.easyPerform( handle );
	http.easyGetinfo( handle, HTTP_INFO_RESPONSE_CODE, &answer );
	http.easyCleanup( handle );
	if ( status ) {
		*status = (int)answer;
	}

	if ( *cancel ) {
		Q_strncpyz( error, "stopped", errorSize );
		return qfalse;
	}
	if ( request.refused ) {
		Q_strncpyz( error, "it was ended on this side", errorSize );
		return qfalse;
	}
	if ( code != 0 ) {
		Q_strncpyz( error, http.easyStrerror( code ), errorSize );
		return qfalse;
	}
	if ( answer != 200 ) {
		Com_sprintf( error, errorSize, "the server answered %li", answer );
		return qfalse;
	}
	return qtrue;
}

// a piece of a frame of the WebSocket: only what binary messages carry goes on, libcurl answers pings itself
static size_t Sys_HttpSocketWrite( char *data, size_t size, size_t count, void *user ) {
	httpRequest_t		*request = (httpRequest_t *)user;
	const httpWsFrame_t	*frame = http.wsMeta( request->handle );

	if ( *request->cancel ) {
		request->refused = qtrue;
		return 0;
	}
	if ( frame && ( frame->flags & HTTP_WS_BINARY ) && !request->sink( request->ctx, data, size * count ) ) {
		request->refused = qtrue;
		return 0;
	}
	return size * count;
}

qboolean Sys_HttpWebSocket( const char *url, sysHttpSink_t sink, void *ctx, const volatile int *cancel, char *error, int errorSize, int *status ) {
	httpRequest_t	request;
	void			*handle;
	long			answer = 0;
	int				code;

	error[0] = '\0';
	if ( status ) {
		*status = 0;
	}
	if ( !Sys_HttpLoad() ) {
		Com_sprintf( error, errorSize, "this system has no %s", HTTP_LIBRARY );
		return qfalse;
	}
	if ( !http.wsMeta ) {
		Q_strncpyz( error, "the libcurl of this system is older than 7.86 and has no WebSocket", errorSize );
		return qfalse;
	}
	handle = http.easyInit();
	if ( !handle ) {
		Q_strncpyz( error, "libcurl does not start", errorSize );
		return qfalse;
	}
	request.sink = sink;
	request.ctx = ctx;
	request.cancel = cancel;
	request.refused = qfalse;
	request.handle = handle;

	http.easySetopt( handle, HTTP_OPT_URL, url );
	http.easySetopt( handle, HTTP_OPT_USERAGENT, "VisionJK" );
	http.easySetopt( handle, HTTP_OPT_PROTOCOLS_STR, "ws,wss" );
	http.easySetopt( handle, HTTP_OPT_FOLLOWLOCATION, 0L );
	http.easySetopt( handle, HTTP_OPT_SSL_VERIFYPEER, 1L );
	http.easySetopt( handle, HTTP_OPT_SSL_VERIFYHOST, 2L );
	http.easySetopt( handle, HTTP_OPT_NOSIGNAL, 1L );
	http.easySetopt( handle, HTTP_OPT_CONNECTTIMEOUT, HTTP_CONNECT_SECONDS );
	http.easySetopt( handle, HTTP_OPT_LOW_SPEED_LIMIT, 1L );
	http.easySetopt( handle, HTTP_OPT_LOW_SPEED_TIME, HTTP_SILENT_SECONDS );
	http.easySetopt( handle, HTTP_OPT_WRITEFUNCTION, Sys_HttpSocketWrite );
	http.easySetopt( handle, HTTP_OPT_WRITEDATA, &request );
	http.easySetopt( handle, HTTP_OPT_NOPROGRESS, 0L );
	http.easySetopt( handle, HTTP_OPT_XFERINFOFUNCTION, Sys_HttpProgress );
	http.easySetopt( handle, HTTP_OPT_XFERINFODATA, &request );

	code = http.easyPerform( handle );
	http.easyGetinfo( handle, HTTP_INFO_RESPONSE_CODE, &answer );
	http.easyCleanup( handle );

	if ( *cancel ) {
		Q_strncpyz( error, "stopped", errorSize );
	} else if ( request.refused ) {
		Q_strncpyz( error, "it was ended on this side", errorSize );
	} else if ( answer && answer != 101 ) {
		if ( status ) {
			*status = (int)answer;
		}
		Com_sprintf( error, errorSize, "the server answered %li", answer );
	} else if ( code != 0 ) {
		Q_strncpyz( error, http.easyStrerror( code ), errorSize );
	} else {
		Q_strncpyz( error, "the server ended the stream", errorSize );
	}
	return qfalse;
}
