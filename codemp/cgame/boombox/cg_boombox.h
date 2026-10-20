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

// What the pointer picks and moves of a box: a piece, the box and its part (BOX_PART_*). Piece n of part 0 is box n.
#define BOX_PIECES			( BOX_MAX * BOX_PARTS )
#define BOX_PIECE( box, part )	( ( box ) + BOX_MAX * ( part ) )
#define BOX_OF( piece )		( ( piece ) % BOX_MAX )
#define BOX_PART_OF( piece )	( ( piece ) / BOX_MAX )

// what the card of a box shows (select/cg_select.c)
typedef struct boomboxInfo_s {
	int			kind;			// BOX_KIND_*
	int			flags;			// BOX_PLAYING and the others of bg_boombox.h
	int			volume;			// for everybody, 0 to 100
	int			msec, length;	// how far into the song it is now, and how long it is in seconds
	int			waiting;		// songs in the line after it
	const char	*title, *next;
	const char	*owner;			// who put it down, "": not known
	const char	*by;			// who asked for the song that plays, "": not known
	const char	*note;			// the last line of the server for this player while it is new, "": none
	qboolean	muted;			// for this player (CG_VoiceBoxMute)
} boomboxInfo_t;

void		CG_BoomboxInit( void );
void		CG_BoomboxServerInfo( const char *value );		// vs_boombox of the server info
void		CG_BoomboxServerCommand( void );				// vs_box
void		CG_BoomboxFrame( void );
void		CG_BoomboxScene( void );						// the boxes into the scene
void		CG_BoomboxDraw( void );							// the titles over them, in the 2D
void		CG_Boombox_f( void );							// the console command boombox
void		CG_BoomboxAsk( int box, const char *text );		// a song for that box (-1: the one near), from the chat box

void		CG_BoomboxPlace( void );						// a boombox where the cross hair points
void		CG_BoomboxPlaceSystem( void );					// a sound system there, its speakers beside it
qboolean	CG_BoomboxOn( void );							// the server or the master has boxes
qboolean	CG_BoomboxSystems( void );						// and sound systems
int			CG_BoomboxNear( void );							// the box near the player, -1: none
int			CG_BoomboxAimed( float range );					// the piece under the cross hair, -1: none

// For the voice chat (voice/cg_voice.c), which plays the songs and is the way to the masterserver. A channel is
// the speaker number of the relay less MAX_CLIENTS: box n is channel n, the right speaker of a sound system n +
// BOX_RIGHT.
qboolean	CG_BoomboxWantsLink( void );
void		CG_BoomboxMasterReady( qboolean ready );
void		CG_BoomboxFromMaster( const byte *data, int len );
int			CG_BoomboxHearMask( void );
qboolean	CG_BoomboxHears( int channel );
qboolean	CG_BoomboxMouth( int channel, vec3_t mouth, float *gain );
float		CG_BoomboxRange( void );

// for the pointer (select/cg_select.c): pieces, but for what is done with the box itself
qboolean	CG_BoomboxShown( int piece );
int			CG_BoomboxPick( const vec3_t start, const vec3_t dir, float *bestT );
void		CG_BoomboxMiddle( int piece, vec3_t at );
float		CG_BoomboxHeight( int piece );
const char	*CG_BoomboxName( int box );
qboolean	CG_BoomboxInfo( int box, boomboxInfo_t *info );
void		CG_BoomboxDo( int box, const char *what );
void		CG_BoomboxLouder( int box, int steps );
void		CG_BoomboxSetVolume( int box, int volume );
qboolean	CG_BoomboxMore( void );
void		CG_BoomboxSeek( int box, int seconds );
void		CG_BoomboxDropSong( int box, int place );
void		CG_BoomboxWatch( int box );
int			CG_BoomboxSongs( int box );
qboolean	CG_BoomboxSong( int box, int place, const char **title, const char **by, int *seconds );

// the window of a box (cg_boombox_ui.c), which the pointer (select/cg_select.c) draws and hands keys
void		CG_BoomboxWindowOpen( int box );
void		CG_BoomboxWindowClose( void );
qboolean	CG_BoomboxWindowShown( void );
qboolean	CG_BoomboxWindowPointerOver( void );
void		CG_BoomboxWindowDraw( void );
qboolean	CG_BoomboxWindowKey( int key, qboolean down );
void		CG_BoomboxPrompt( int box );
void		CG_BoomboxGrab( int piece );
void		CG_BoomboxTurnHeld( int notches );
void		CG_BoomboxCarry( const vec3_t start, const vec3_t dir );
void		CG_BoomboxDrop( qboolean place );
int			CG_BoomboxHeld( void );
qboolean	CG_BoomboxHeldBad( void );

// voice/cg_voice.c: the songs as they are played, and the way to the masterserver
float		CG_VoiceBoxBeat( int channel );
qboolean	CG_VoiceBoxSend( const boxDo_t *d );
qboolean	CG_VoiceMasterBoxes( void );
qboolean	CG_VoiceMasterBoxList( void );
qboolean	CG_VoiceBoxMuted( int box );
void		CG_VoiceBoxMute( int box, qboolean mute );
void		CG_VoiceBoxReset( int box );
