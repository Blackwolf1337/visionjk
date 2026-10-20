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

// cg_status_music.c -- what music plays on this machine right now, for cg_status.c: asked of the players of music
// that are in v_shareMusicPlayers (ticked in Setup), every five seconds, and only while v_shareMusic is 1. How it
// is found out is in ui/ui_music.c, which the ui builds in as well for its list of the players.

#include "cgame/cg_local.h"
#include "ui/ui_music.h"

void CG_MusicFrame( void ) {
	Music_Frame( trap->Milliseconds(), (qboolean)( v_shareMusic.integer != 0 ), qfalse, v_shareMusicPlayers.string );
}

void CG_MusicShutdown( void ) {
	Music_Shutdown();
}

/*
=================
CG_MusicNowPlaying

What plays, as UTF-8. qfalse: nothing, or nothing that can be found out.
=================
*/
qboolean CG_MusicNowPlaying( char *out, int outSize ) {
	Q_strncpyz( out, Music_NowPlaying(), outSize );
	return (qboolean)( out[0] != '\0' );
}
