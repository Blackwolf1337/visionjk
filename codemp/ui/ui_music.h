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

// ui_music.h -- what music plays on this machine, and which players of music there are (ui_music.c). Built into the
// cgame, which shows what plays (status/cg_status_music.c, v_shareMusic), and into the ui, whose Setup lists the
// players to tick the ones that may be told (v_shareMusicPlayers).

#pragma once

#define MUSIC_KEY			32		// the name of a player as the list of v_shareMusicPlayers has it, with its end
#define MUSIC_MAX_PLAYERS	8

// Once a frame. playing: find out what plays, among the players of allow. players: keep the list of the players
// there are up to date. Neither: nothing runs, and nothing of the desktop is held open.
void		Music_Frame( int now, qboolean playing, qboolean players, const char *allow );
void		Music_Shutdown( void );

// what plays, UTF-8, "" for nothing or nothing that can be found out
const char	*Music_NowPlaying( void );

// the players there are, by their names in the list of v_shareMusicPlayers; how many
int			Music_Players( char keys[MUSIC_MAX_PLAYERS][MUSIC_KEY] );

qboolean	Music_Allowed( const char *allow, const char *key );
void		Music_Toggle( char *allow, int allowSize, const char *key );
void		Music_PlayerName( const char *key, char *out, int outSize );
