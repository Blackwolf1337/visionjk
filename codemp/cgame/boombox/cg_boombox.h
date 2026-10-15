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

// cg_boombox.h -- the boombox of the client (cg_boombox.c): what the rest of the cgame asks of it. Not from JA+,
// see docs/japlus/boombox.md and game/boombox/bg_boombox.h.

#pragma once

#include "game/boombox/bg_boombox.h"

// what the card of a box shows (select/cg_select.c)
typedef struct boomboxInfo_s {
	int			flags;			// BOX_PLAYING and the others of bg_boombox.h
	int			volume;			// for everybody, 0 to 100
	int			msec, length;	// how far into the song it is now, and how long it is in seconds
	int			waiting;		// songs in the line after it
	const char	*title, *next;
	qboolean	muted;			// for this player (CG_VoiceBoxMute)
} boomboxInfo_t;

void		CG_BoomboxInit( void );
void		CG_BoomboxServerInfo( const char *value );		// g_boombox of the server info
void		CG_BoomboxServerCommand( void );				// jpbox
void		CG_BoomboxFrame( void );
void		CG_BoomboxScene( void );						// the boxes into the scene
void		CG_BoomboxDraw( void );							// the titles over them, in the 2D
void		CG_Boombox_f( void );							// the console command boombox
void		CG_BoomboxAsk( int box, const char *text );		// a song for that box (-1: the one near), from the chat box

// for the voice chat (voice/cg_voice.c), which plays the songs
qboolean	CG_BoomboxWantsLink( void );
qboolean	CG_BoomboxHears( int box );
qboolean	CG_BoomboxMouth( int box, vec3_t mouth, float *gain );
float		CG_BoomboxRange( void );

// for the pointer (select/cg_select.c)
qboolean	CG_BoomboxShown( int box );
int			CG_BoomboxPick( const vec3_t start, const vec3_t dir, float *bestT );
void		CG_BoomboxMiddle( int box, vec3_t at );
qboolean	CG_BoomboxInfo( int box, boomboxInfo_t *info );
void		CG_BoomboxDo( int box, const char *what );
void		CG_BoomboxLouder( int box, int steps );
void		CG_BoomboxPrompt( int box );
void		CG_BoomboxGrab( int box );
void		CG_BoomboxCarry( const vec3_t start, const vec3_t dir );
void		CG_BoomboxDrop( qboolean place );
int			CG_BoomboxHeld( void );
qboolean	CG_BoomboxHeldBad( void );

// voice/cg_voice.c: the songs as they are played
float		CG_VoiceBoxBeat( int box );
qboolean	CG_VoiceBoxMuted( int box );
void		CG_VoiceBoxMute( int box, qboolean mute );
void		CG_VoiceBoxReset( int box );
