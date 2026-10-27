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


// sys_http_curl.cpp -- Sys_HttpGet() with the libcurl of the system, see sys_http.h. Linux and macOS.
//
// The engine is not linked against libcurl and is built without its headers: the library is loaded when the
// first question is asked, and the few calls and numbers of it that are used are written down here. They are
// part of its binary interface, which has not changed since libcurl 7.32 (2013), the oldest this needs.
// A system without the library has no update check, and that is all.

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
#define HTTP_INFO_RESPONSE_CODE	0x200002
#define HTTP_PROTO_WEB			3L

#define HTTP_CONNECT_SECONDS	15L
#define HTTP_SILENT_SECONDS		30L		// a line that brings nothing for this long has failed

typedef int ( *httpGlobalInit_t )( long flags );
typedef void *( *httpEasyInit_t )( void );
typedef int ( *httpEasySetopt_t )( void *handle, int option, ... );
typedef int ( *httpEasyPerform_t )( void *handle );
typedef int ( *httpEasyGetinfo_t )( void *handle, int info, ... );
typedef void ( *httpEasyCleanup_t )( void *handle );
typedef const char *( *httpEasyStrerror_t )( int code );

static struct {
	qboolean			tried, loaded;
	httpEasyInit_t		easyInit;
	httpEasySetopt_t	easySetopt;
	httpEasyPerform_t	easyPerform;
	httpEasyGetinfo_t	easyGetinfo;
	httpEasyCleanup_t	easyCleanup;
	httpEasyStrerror_t	easyStrerror;
} http;

typedef struct httpRequest_s {
	sysHttpSink_t		sink;
	void				*ctx;
	const volatile int	*cancel;
	qboolean			refused;		// the sink did not want more
} httpRequest_t;

// One question at a time in the whole client (cl_update.cpp and cl_download.cpp never ask at once), so the
// library is loaded and started by the thread of the first one, once.
static qboolean Sys_HttpLoad( void ) {
	void				*library;
	httpGlobalInit_t	globalInit;

	if ( http.tried )
		return http.loaded;
	http.tried = qtrue;

	library = Sys_LoadLibrary( HTTP_LIBRARY );
	if ( !library )
		return qfalse;
	globalInit = (httpGlobalInit_t)Sys_LoadFunction( library, "curl_global_init" );
	http.easyInit = (httpEasyInit_t)Sys_LoadFunction( library, "curl_easy_init" );
	http.easySetopt = (httpEasySetopt_t)Sys_LoadFunction( library, "curl_easy_setopt" );
	http.easyPerform = (httpEasyPerform_t)Sys_LoadFunction( library, "curl_easy_perform" );
	http.easyGetinfo = (httpEasyGetinfo_t)Sys_LoadFunction( library, "curl_easy_getinfo" );
	http.easyCleanup = (httpEasyCleanup_t)Sys_LoadFunction( library, "curl_easy_cleanup" );
	http.easyStrerror = (httpEasyStrerror_t)Sys_LoadFunction( library, "curl_easy_strerror" );
	if ( !globalInit || !http.easyInit || !http.easySetopt || !http.easyPerform || !http.easyGetinfo || !http.easyCleanup
		|| !http.easyStrerror || globalInit( HTTP_GLOBAL_DEFAULT ) != 0 ) {
		Sys_UnloadLibrary( library );
		return qfalse;
	}
	http.loaded = qtrue;
	return qtrue;
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
