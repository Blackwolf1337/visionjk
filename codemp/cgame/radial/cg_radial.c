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

// cg_radial.c -- the quick wheel: while the key of +v_radial is held, a ring of eight slices stands in the middle
// of the screen, the mouse picks one by its direction, and letting the key go (or a click) does it. A short tap of
// the key keeps the wheel open until a click, or until the key is pressed again. Not from JA+, see
// docs/japlus/boombox.md, "The quick wheel".
//
// Every choice has its slice for good, so the hand learns where it is: play, pause, skip, louder, the player,
// quieter, and at the left the two that depend on the cross hair (or the pointer, while the mouse is free) when
// the wheel opened. On a box or a part of a sound system they move that part and take the box away; elsewhere
// they put a boombox or a sound system down there. The others are about that box, or the one near the player.
// "Take it away" wants a second click, as on the card of a box, and the wheel stays open for it.
//
// The wheel is a mode of the pointer (select/cg_select.c): the pointer takes the keys and the mouse for it, draws
// it in place of the cards, and hands it every key first.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"
#include "cgame/boombox/cg_boombox.h"

#define RADIAL_SLICES		8
#define RADIAL_OUTER		124.0f		// the ring, from the middle, in units of the HUD
#define RADIAL_INNER		48.0f		//	and its hole
#define RADIAL_DEAD			20.0f		// the pointer nearer to the middle than this picks nothing new
#define RADIAL_REACH		RADIAL_OUTER	// and is held within this, so it comes back quickly
#define RADIAL_AIM_RANGE	700.0f		// a box further away than this under the cross hair is not the one it is about
#define RADIAL_CONFIRM_MSEC	4000		// the second click for "Take it away" within this
#define RADIAL_TAP_MSEC		250			// the key let go this soon, with nothing picked: the wheel stays open
#define RADIAL_GRAB_MSEC	500			// the key let go by the engine when the wheel takes the keys comes this soon
#define RADIAL_FAN			20			// thin bars that fill a lit slice

typedef enum {
	RAD_PLAY,
	RAD_PAUSE,
	RAD_SKIP,
	RAD_LOUDER,
	RAD_WINDOW,
	RAD_QUIETER,
	RAD_PLACE_BOX,				// the left slice: or RAD_MOVE on a box
	RAD_PLACE_SYSTEM,			// the slice above it: or RAD_REMOVE on a box
	RAD_MOVE,
	RAD_REMOVE
} radialAct_t;

static struct {
	qboolean	open;
	qboolean	held;				// the key is still down
	qboolean	stays;				// open without the key: a tap opened it, or "Take it away" waits for its click
	int			piece;				// the piece of a box under the cross hair when it opened, -1: none
	int			box;				// the box the choices are about: that one, else the one near, -1: none
	radialAct_t	acts[RADIAL_SLICES];
	qboolean	usable[RADIAL_SLICES];
	int			hover;				// the slice picked by the direction of the pointer, -1: none
	int			confirmTime;		// when "Take it away" was picked first, 0: not yet
	int			openTime;			// trap->Milliseconds() when it opened
} rad;

static const vec4_t radialText = { 0.86f, 0.92f, 0.97f, 1.0f }, radialDim = { 0.42f, 0.48f, 0.55f, 1.0f };
static const vec4_t radialAccent = { 0.43f, 0.83f, 1.0f, 1.0f }, radialGold = { 0.89f, 0.84f, 0.36f, 1.0f };

// the choices, made anew every frame: the box may have changed meanwhile, or be gone
static void Radial_Build( void ) {
	const qboolean	on = CG_BoomboxOn();
	boomboxInfo_t	info;
	qboolean		have, playing;

	if ( rad.piece >= 0 && !CG_BoomboxShown( rad.piece ) )
		rad.piece = -1;
	rad.box = ( rad.piece >= 0 ) ? BOX_OF( rad.piece ) : on ? CG_BoomboxNear() : -1;
	have = (qboolean)( rad.box >= 0 && CG_BoomboxInfo( rad.box, &info ) );
	if ( !have )
		rad.box = -1;
	playing = (qboolean)( have && ( info.flags & BOX_PLAYING ) );

	rad.acts[0] = RAD_PLAY;			rad.usable[0] = on;
	rad.acts[1] = RAD_PAUSE;		rad.usable[1] = playing;
	rad.acts[2] = RAD_SKIP;			rad.usable[2] = playing;
	rad.acts[3] = RAD_LOUDER;		rad.usable[3] = (qboolean)( have && info.volume < 100 );
	rad.acts[4] = RAD_WINDOW;		rad.usable[4] = have;
	rad.acts[5] = RAD_QUIETER;		rad.usable[5] = (qboolean)( have && info.volume > 0 );
	if ( rad.piece >= 0 ) {
		rad.acts[6] = RAD_MOVE;		rad.usable[6] = qtrue;
		rad.acts[7] = RAD_REMOVE;	rad.usable[7] = qtrue;
	} else {
		rad.acts[6] = RAD_PLACE_BOX;	rad.usable[6] = on;
		rad.acts[7] = RAD_PLACE_SYSTEM;	rad.usable[7] = CG_BoomboxSystems();
	}
}

// the word in the slice
static const char *Radial_Short( radialAct_t act ) {
	boomboxInfo_t info;

	memset( &info, 0, sizeof( info ) );
	if ( rad.box >= 0 )
		CG_BoomboxInfo( rad.box, &info );
	switch ( act ) {
	case RAD_PLAY:			return CG_VisionText( "Play" );
	case RAD_PAUSE:			return CG_VisionText( ( info.flags & BOX_PAUSED ) ? "Resume" : "Pause" );
	case RAD_SKIP:			return CG_VisionText( "Skip" );
	case RAD_LOUDER:		return CG_VisionText( "Louder" );
	case RAD_WINDOW:		return CG_VisionText( "Player" );
	case RAD_QUIETER:		return CG_VisionText( "Quieter" );
	case RAD_PLACE_BOX:		return CG_VisionText( "Boombox" );
	case RAD_PLACE_SYSTEM:	return CG_VisionText( "Sound System" );
	case RAD_MOVE:			return CG_VisionText( "Move it" );
	case RAD_REMOVE:		return CG_VisionText( "Remove" );
	}
	return "";
}

// what it does, under the wheel
static const char *Radial_Long( radialAct_t act ) {
	boomboxInfo_t info;

	memset( &info, 0, sizeof( info ) );
	if ( rad.box >= 0 )
		CG_BoomboxInfo( rad.box, &info );
	switch ( act ) {
	case RAD_PLAY:			return CG_VisionText( ( info.flags & BOX_PLAYING ) ? "Add a song ..." : "Play a song ..." );
	case RAD_PAUSE:			return CG_VisionText( ( info.flags & BOX_PAUSED ) ? "Resume" : "Pause" );
	case RAD_SKIP:			return CG_VisionText( "Skip this song" );
	case RAD_LOUDER:		return CG_VisionTextWith( "Louder for everybody (%i %%)", va( "%i", info.volume ) );
	case RAD_WINDOW:		return CG_VisionText( "Open the player" );
	case RAD_QUIETER:		return CG_VisionTextWith( "Quieter for everybody (%i %%)", va( "%i", info.volume ) );
	case RAD_PLACE_BOX:		return CG_VisionText( "Put a boombox here" );
	case RAD_PLACE_SYSTEM:	return CG_VisionText( "Put a sound system here" );
	case RAD_MOVE:			return CG_VisionText( "Move it" );
	case RAD_REMOVE:		return CG_VisionText( ( rad.confirmTime && cg.time - rad.confirmTime < RADIAL_CONFIRM_MSEC )
								? "Click again to take it away" : "Take it away" );
	}
	return "";
}

// the middle of slice k, as an angle of the screen (y down): the first one at the top, then clockwise
static float Radial_Angle( int k ) {
	return -0.5f * M_PI + 2.0f * M_PI * k / RADIAL_SLICES;
}

// The slice in the direction of the pointer. Back in the middle the one that was picked stays picked: the hand
// comes back a little before it lets the key go.
static void Radial_Point( void ) {
	const float	dx = cgs.cursorX - SCREEN_WIDTH * 0.5f, dy = cgs.cursorY - SCREEN_HEIGHT * 0.5f;
	float		angle;

	if ( dx * dx + dy * dy < RADIAL_DEAD * RADIAL_DEAD )
		return;
	// (0 at the top, growing clockwise as the slices go)
	angle = atan2f( dy, dx ) + 0.5f * M_PI;
	if ( angle < 0.0f )
		angle += 2.0f * M_PI;
	rad.hover = (int)floorf( angle / ( 2.0f * M_PI ) * RADIAL_SLICES + 0.5f ) % RADIAL_SLICES;
}

void CG_RadialClose( void ) {
	rad.open = rad.held = rad.stays = qfalse;
	rad.hover = -1;
	rad.confirmTime = 0;
}

qboolean CG_RadialOpen( void ) {
	return rad.open;
}

// does the choice; qfalse: the wheel stays open (the first click of "Take it away")
static qboolean Radial_Run( radialAct_t act ) {
	const int		box = rad.box;
	boomboxInfo_t	info;

	switch ( act ) {
	case RAD_PLACE_BOX:
		CG_BoomboxPlace();
		break;
	case RAD_PLACE_SYSTEM:
		CG_BoomboxPlaceSystem();
		break;
	case RAD_PLAY:
		CG_BoomboxPrompt( box );
		break;
	case RAD_WINDOW:
		CG_BoomboxWindowOpen( box );
		break;
	case RAD_PAUSE:
		if ( CG_BoomboxInfo( box, &info ) )
			CG_BoomboxDo( box, ( info.flags & BOX_PAUSED ) ? "resume" : "pause" );
		break;
	case RAD_SKIP:
		CG_BoomboxDo( box, "skip" );
		break;
	case RAD_LOUDER:
		CG_BoomboxLouder( box, 1 );
		break;
	case RAD_QUIETER:
		CG_BoomboxLouder( box, -1 );
		break;
	case RAD_MOVE:
		// (the piece it was opened on: a speaker of a sound system moves by itself)
		CG_RadialClose();
		CG_SelectCarry( rad.piece );
		return qtrue;
	case RAD_REMOVE:
		if ( !rad.confirmTime || cg.time - rad.confirmTime >= RADIAL_CONFIRM_MSEC ) {
			rad.confirmTime = cg.time ? cg.time : 1;
			rad.stays = qtrue;
			return qfalse;
		}
		CG_BoomboxDo( box, "remove" );
		break;
	}
	return qtrue;
}

// the picked slice, if it can be had. qfalse: the wheel stays open.
static qboolean Radial_Pick( void ) {
	if ( rad.hover < 0 || !rad.usable[rad.hover] )
		return qtrue;
	return Radial_Run( rad.acts[rad.hover] );
}

/*
=================
CG_RadialDown_f

+v_radial: the wheel opens, about the box under the pointer (while the mouse is free) or the cross hair. Pressed
while a tap keeps it open: what is picked is done, and it closes.
=================
*/
void CG_RadialDown_f( void ) {
	if ( rad.open && !rad.held ) {
		if ( Radial_Pick() )
			CG_RadialClose();
		return;
	}
	if ( !cg.snap || cg.demoPlayback || CG_ChatIsOpen() || rad.open )
		return;
	memset( &rad, 0, sizeof( rad ) );
	rad.open = rad.held = qtrue;
	rad.hover = -1;
	rad.openTime = trap->Milliseconds();
	rad.piece = CG_SelectHoveredPiece();
	if ( rad.piece < 0 )
		rad.piece = CG_BoomboxAimed( RADIAL_AIM_RANGE );
	cgs.cursorX = SCREEN_WIDTH / 2;
	cgs.cursorY = SCREEN_HEIGHT / 2;
	Radial_Build();
}

// -v_radial: what is picked is done and the wheel closes. Let go at once with nothing picked: it stays open.
void CG_RadialUp_f( void ) {
	const int sinceOpen = trap->Milliseconds() - rad.openTime;

	if ( !rad.open || !rad.held )
		return;
	// When the pointer takes the keys for the wheel (Key_SetCatcher) the engine lets go of every key that is held,
	// this one too, and sends "-v_radial <key> 0": without a time. That one is not the player letting go. A key
	// let go without a time later on is the window losing the focus.
	if ( !atoi( CG_Argv( 2 ) ) && sinceOpen < RADIAL_GRAB_MSEC )
		return;
	rad.held = qfalse;
	if ( rad.hover < 0 && sinceOpen < RADIAL_TAP_MSEC ) {
		rad.stays = qtrue;
		return;
	}
	if ( Radial_Pick() && !rad.stays )
		CG_RadialClose();
}

// a key while the wheel is open: a click picks, Escape and the right button close it, the rest is swallowed
qboolean CG_RadialKey( int key, qboolean down ) {
	if ( !rad.open )
		return qfalse;
	if ( !down )
		return qtrue;
	if ( key == A_MOUSE1 ) {
		if ( Radial_Pick() )
			CG_RadialClose();
	} else if ( key == A_MOUSE2 || key == A_ESCAPE ) {
		CG_RadialClose();
	}
	return qtrue;
}

/*
===============================================================================

Drawing

===============================================================================
*/

// a thin bar from radius r0 to r1 at an angle of the screen, thick units across
static void Radial_Bar( float cx, float cy, float angle, float r0, float r1, float thick ) {
	const float mid = 0.5f * ( r0 + r1 );

	CG_DrawRotatePic2( cx + mid * cosf( angle ), cy + mid * sinf( angle ), r1 - r0, thick, RAD2DEG( angle ), cgs.media.whiteShader );
}

// a slice filled with a fan of bars, as thick as it is at its outer edge, so they meet there and overlap inside
static void Radial_Slice( float cx, float cy, int k, float r0, float r1, const vec4_t color ) {
	const float	half = M_PI / RADIAL_SLICES, step = 2.0f * half / RADIAL_FAN;
	int			i;

	trap->R_SetColor( color );
	for ( i = 0; i < RADIAL_FAN; i++ ) {
		Radial_Bar( cx, cy, Radial_Angle( k ) - half + ( i + 0.5f ) * step, r0, r1, r1 * step + 0.6f );
	}
	trap->R_SetColor( NULL );
}

// a text in the middle of a place, as large as scale allows within width
static void Radial_Text( float x, float y, float width, float scale, const vec4_t color, const char *text, int font ) {
	float w = (float)CG_Text_Width( text, scale, font );

	if ( w > width && w > 0.0f ) {
		scale *= width / w;
		w = width;
	}
	CG_Text_Paint( x - w * 0.5f, y - 9.0f * scale, scale, (float *)color, text, 0, 0, 0, font );
}

/*
=================
CG_RadialDraw

The wheel in the middle of the screen, the picked slice lit, what it is about in its middle and what the picked
slice does under it. In the 2D of the pointer (PLACE_CENTER).
=================
*/
void CG_RadialDraw( void ) {
	const vec4_t	ring = { 0.03f, 0.06f, 0.10f, 0.82f }, hole = { 0.01f, 0.02f, 0.04f, 0.90f }, edge = { 0.43f, 0.83f, 1.0f, 0.22f };
	const vec4_t	lit = { 0.43f, 0.83f, 1.0f, 0.30f }, litEdge = { 0.43f, 0.83f, 1.0f, 0.95f };
	const int		font = Skin_Ready() ? SKIN_FONT_SANS : FONT_SMALL2;
	const float		cx = SCREEN_WIDTH * 0.5f, cy = SCREEN_HEIGHT * 0.5f, mid = 0.5f * ( RADIAL_INNER + RADIAL_OUTER );
	const char		*title;
	float			dx, dy, length;
	int				k;

	if ( !rad.open )
		return;
	Radial_Build();
	// the pointer stays on the ring, so the way back to another slice is short
	dx = cgs.cursorX - cx;
	dy = cgs.cursorY - cy;
	length = sqrtf( dx * dx + dy * dy );
	if ( length > RADIAL_REACH ) {
		cgs.cursorX = (int)( cx + dx * RADIAL_REACH / length );
		cgs.cursorY = (int)( cy + dy * RADIAL_REACH / length );
	}
	Radial_Point();

	// the ring and its hole
	if ( Skin_Ready() ) {
		Skin_Soft( cx - RADIAL_OUTER - 24.0f, cy - RADIAL_OUTER - 24.0f, 2.0f * RADIAL_OUTER + 48.0f, 2.0f * RADIAL_OUTER + 48.0f, 24.0f, hole );
		Skin_Disc( cx, cy, RADIAL_OUTER, ring );
		Skin_Disc( cx, cy, RADIAL_INNER, hole );
	} else {
		for ( k = 0; k < RADIAL_SLICES; k++ ) {
			Radial_Slice( cx, cy, k, RADIAL_INNER, RADIAL_OUTER, ring );
		}
	}
	// the picked slice, lit, with a bright edge outside
	if ( rad.hover >= 0 ) {
		Radial_Slice( cx, cy, rad.hover, RADIAL_INNER + 2.0f, RADIAL_OUTER - 2.0f, rad.usable[rad.hover] ? lit : edge );
		if ( rad.usable[rad.hover] ) {
			Radial_Slice( cx, cy, rad.hover, RADIAL_OUTER - 4.0f, RADIAL_OUTER, litEdge );
		}
	}
	// the lines between the slices
	trap->R_SetColor( edge );
	for ( k = 0; k < RADIAL_SLICES; k++ ) {
		Radial_Bar( cx, cy, Radial_Angle( k ) + M_PI / RADIAL_SLICES, RADIAL_INNER + 4.0f, RADIAL_OUTER - 4.0f, 1.0f );
	}
	trap->R_SetColor( NULL );

	// the word of every slice
	for ( k = 0; k < RADIAL_SLICES; k++ ) {
		const float	a = Radial_Angle( k );
		const float	*color = !rad.usable[k] ? radialDim : ( rad.acts[k] == RAD_REMOVE && rad.confirmTime ) ? radialGold
						: ( k == rad.hover ) ? colorWhite : radialText;

		Radial_Text( cx + mid * cosf( a ), cy + mid * sinf( a ), 2.0f * mid * sinf( M_PI / RADIAL_SLICES ) - 8.0f, 0.5f, color,
			Radial_Short( rad.acts[k] ), font );
	}

	// in the middle what it is about, and a mark toward the pointer on the edge of the hole
	title = ( rad.box >= 0 ) ? CG_BoomboxName( rad.box ) : CG_BoomboxOn() ? CG_VisionText( "Boombox" ) : CG_VisionText( "No boombox here" );
	Radial_Text( cx, cy, 2.0f * RADIAL_INNER - 10.0f, 0.46f, radialAccent, title, font );
	if ( rad.hover >= 0 && Skin_Ready() ) {
		const float a = Radial_Angle( rad.hover );

		Skin_Disc( cx + ( RADIAL_INNER - 6.0f ) * cosf( a ), cy + ( RADIAL_INNER - 6.0f ) * sinf( a ), 3.0f, radialAccent );
	}

	// under the wheel what the picked slice does
	if ( rad.hover >= 0 ) {
		Radial_Text( cx, cy + RADIAL_OUTER + 18.0f, 300.0f, 0.6f, rad.usable[rad.hover] ? colorWhite : radialDim,
			Radial_Long( rad.acts[rad.hover] ), font );
	} else if ( rad.stays ) {
		Radial_Text( cx, cy + RADIAL_OUTER + 18.0f, 300.0f, 0.5f, radialDim, CG_VisionText( "Click a choice, Esc closes" ), font );
	}
}
