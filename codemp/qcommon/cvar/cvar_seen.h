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

// cvar_seen.h -- the day code last used a cvar, for cvar_cleanup (cvar.cpp). Not from OpenJK. No engine call in
// it, for the unit tests. Read after q_shared.h.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define CVARSEEN_MAX		8192	// names kept, the ones used longest ago go first
#define CVARSEEN_DAYS		30		// a cvar no code used for this long is offered to be removed

void		CvarSeen_Clear( void );
// the day the tracking began, 0 before anything was loaded or touched
int			CvarSeen_Start( void );
// code used the cvar on that day (days since 1970)
void		CvarSeen_Touch( const char *name, int day );
// the day it was last used, 0 for never
int			CvarSeen_Day( const char *name );
// a cvar of this name could go: no code used it for CVARSEEN_DAYS, or never while the tracking ran that long
qboolean	CvarSeen_Unused( const char *name, int today );
// what changed since the last CvarSeen_Write
qboolean	CvarSeen_Dirty( void );

// The text of the file: "start <day>" and then "<name> <day>" a line. What it has is added to what is known,
// the later day wins.
void		CvarSeen_Parse( const char *text, int today );
// The text of the file into out. Returns its length, -1 when out has no room.
int			CvarSeen_Write( char *out, int outSize );

#ifdef __cplusplus
}
#endif
