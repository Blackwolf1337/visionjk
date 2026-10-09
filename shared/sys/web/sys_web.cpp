/*
===========================================================================
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

#include <emscripten.h>

#include "qcommon/qcommon.h"
#include "client/client.h"
#include "sys/sys_local.h"
#include "sys_web.h"

/*
=================
Sys_WebFrame

The page calls this when it is about to paint. A browser paints as often as the display refreshes, which may be
more often than com_maxfps asks for: Com_Frame() would then wait for the rest of its frame, and on the one
thread of a page nothing may wait. So a call that comes too early is left out.
=================
*/
static void Sys_WebFrame( void ) {
	if ( Sys_Milliseconds() - com_frameTime < Com_MinFrameMsec() ) {
		return;
	}
	Com_Frame();
}

/*
=================
Sys_WebMainLoop

Instead of the endless loop of main(): the frames come from the page, main() returns.
=================
*/
void Sys_WebMainLoop( void ) {
	// 0: with every paint of the page. A swap interval of 0 would make SDL turn this into a timer.
	emscripten_set_main_loop( Sys_WebFrame, 0, 0 );
}

/*
=================
Web_ClientState

For the page: connstate_t, CA_ACTIVE is "in the game"
=================
*/
EMSCRIPTEN_KEEPALIVE int Web_ClientState( void ) {
	return (int)cls.state;
}

/*
=================
Web_Command

For the page: a line for the console, run with the next frame
=================
*/
EMSCRIPTEN_KEEPALIVE void Web_Command( const char *text ) {
	Cbuf_AddText( text );
	Cbuf_AddText( "\n" );
}
