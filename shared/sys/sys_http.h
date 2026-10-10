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
sys_http.h -- one question to a web server, for the updater of the client (client/cl_update.cpp). The engine
has no code for that of its own and brings none: on Windows it asks WinHTTP, which is part of the system
(sys_http_win32.cpp), anywhere else the libcurl the system has, loaded when it is first needed
(sys_http_curl.cpp). Either one knows the certificates the system trusts, and checks them.

Sys_HttpGet() waits until the answer is there or has failed. It is only ever called from a thread of its
own, never from the thread of the game.
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
*/
qboolean Sys_HttpGet( const char *url, sysHttpSink_t sink, void *ctx, const volatile int *cancel, char *error, int errorSize );
