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

#pragma once

// The engine in a browser, built with Emscripten. The page (page/boot.js) puts the game files into the file
// system, compiles the modules and calls main().

#ifdef __cplusplus
extern "C" {
#endif

// sys_web.js: the modules cgame, ui and renderer. A handle and a function are numbers the size of a pointer
// (4 bytes), 0 is "none".
void	*Sys_WebLoadLibrary( const char *name );
void	*Sys_WebLoadFunction( void *handle, const char *name );
void	Sys_WebUnloadLibrary( void *handle );
void	Sys_WebShowError( const char *error );

// for the page
int		Web_ClientState( void );
void	Web_Command( const char *text );

#ifdef __cplusplus
}
#endif

// sys_web.cpp
void	Sys_WebMainLoop( void );
