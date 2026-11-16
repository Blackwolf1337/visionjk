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

// cl_console_local.h -- what the files of the console (cl_console.cpp, cl_conselect.cpp) share and nothing else needs

#pragma once

#define CON_BLANK_CHAR			' '
#define CON_TIMESTAMP_LEN		11 // "[13:37:00] "

static const conChar_t CON_WRAP = { { ColorIndex(COLOR_GREY), '\\' } };

extern console_t	con;
extern cvar_t		*con_timestamps;
