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


/*
sys_http.h -- one question to a web server, for the updater of the client (client/cl_update.cpp) and the paks a
server has on a web address (client/cl_download.cpp). The engine
has no code for that of its own and brings none: on Windows it asks WinHTTP, which is part of the system
(sys_http_win32.cpp), anywhere else the libcurl the system has, loaded when it is first needed
(sys_http_curl.cpp). Either one knows the certificates the system trusts, and checks them.

Sys_HttpGet() waits until the answer is there or has failed. It is only ever called from a thread of its
own, never from the thread of the game, and one at a time: the two callers never ask at once.

Sys_HttpWebSocket() is the stream of the TV of a server (client/cl_tv.cpp) behind a web server that speaks https:
it waits as long as the stream runs, in a thread of its own too, and may run while Sys_HttpGet() does.
*/

#pragma once

#include <stddef.h>

#include "qcommon/q_platform.h"

// What came in, piece by piece and in order. qfalse: no more of it, the request ends as failed.
typedef qboolean ( *sysHttpSink_t )( void *ctx, const void *data, size_t len );

/*
Asks for url and hands the answer to sink. qtrue: the server said 200 and all of it went to the sink.
A redirect is not followed and counts as failed, as does any other answer.
cancel: looked at while it runs, anything but 0 there ends it (within some seconds if the line is silent).
error: what went wrong, in words for the console.
status: the answer of the server (404: it does not have it), 0 when there was none. NULL: not wanted.
*/
qboolean Sys_HttpGet( const char *url, sysHttpSink_t sink, void *ctx, const volatile int *cancel, char *error, int errorSize, int *status );

/*
Opens a WebSocket at url (ws:// or wss://) and hands what each binary message carries to sink, in order, until
the server ends it, the sink does not want more or cancel is set. A ping of the server is answered here. It always
ends with a reason in error. status: the answer of a server that did not take the socket (404), else 0.
*/
qboolean Sys_HttpWebSocket( const char *url, sysHttpSink_t sink, void *ctx, const volatile int *cancel, char *error, int errorSize, int *status );
