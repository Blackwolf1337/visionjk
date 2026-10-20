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

// cg_radial.c -- the quick wheel: while the key of +v_radial is held, a ring of choices stands in the middle of the
// screen, the mouse picks one by its direction, and letting the key go (or a click) does it. Not from JA+, see
// docs/japlus/boombox.md, "The quick wheel".
//
// What it offers depends on what the cross hair was on when it opened (or the pointer, while the mouse is free):
// on a box of the boombox or a part of a sound system, what can be done with that box; anywhere else, putting a
// boombox or a sound system down there, a song, and the player of the box near. "Take it away" wants a second
// click, as on the card of a box, and the wheel stays open for it after the key is let go.
//
// The wheel is a mode of the pointer (select/cg_select.c): the pointer takes the keys and the mouse for it, draws
// it in place of the cards, and hands it every key first.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"
#include "cgame/boombox/cg_boombox.h"

#define RADIAL_MAX			10
#define RADIAL_RADIUS		118.0f		// where the choices stand from the middle, in units of the HUD
#define RADIAL_DEAD			26.0f		// the pointer nearer to the middle than this is on none of them
#define RADIAL_REACH		190.0f		// and is held within this
#define RADIAL_AIM_RANGE	700.0f		// a box further away than this under the cross hair is not the one it is about
#define RADIAL_CONFIRM_MSEC	4000		// the second click for "Take it away" within this
#define RADIAL_SCALE		0.6f		// of the text of a choice
#define RADIAL_GRAB_MSEC	500			// the key let go by the engine when the wheel takes the keys comes this soon

typedef enum {
	RAD_PLACE_BOX,
	RAD_PLACE_SYSTEM,
	RAD_PLAY,
	RAD_WINDOW,
	RAD_PAUSE,
	RAD_SKIP,
	RAD_STOP,
	RAD_LOUDER,
	RAD_QUIETER,
	RAD_MOVE,
	RAD_REMOVE
} radialAct_t;

static struct {
	qboolean	open;
	qboolean	held;				// the key is still down
	int			piece;				// the piece of a box it is about, -1: none
	radialAct_t	acts[RADIAL_MAX];
	qboolean	usable[RADIAL_MAX];
	int			num;
	int			hover;				// the choice the pointer is on, -1: none
	int			confirmTime;		// when "Take it away" was picked first, 0: not yet
	int			openTime;			// trap->Milliseconds() when it opened
} rad;

static const vec4_t radialText = { 0.86f, 0.92f, 0.97f, 1.0f }, radialDim = { 0.50f, 0.56f, 0.62f, 1.0f };
static const vec4_t radialAccent = { 0.43f, 0.83f, 1.0f, 1.0f }, radialGold = { 0.89f, 0.84f, 0.36f, 1.0f };

static void Radial_Add( radialAct_t act, qboolean usable ) {
	if ( rad.num < RADIAL_MAX ) {
		rad.acts[rad.num] = act;
		rad.usable[rad.num] = usable;
		rad.num++;
	}
}

// the choices, made anew every frame: the box may have changed meanwhile, or be gone
static void Radial_Build( void ) {
	const qboolean	on = CG_BoomboxOn();
	boomboxInfo_t	info;

	rad.num = 0;
	if ( rad.piece >= 0 && !CG_BoomboxShown( rad.piece ) )
		rad.piece = -1;
	if ( rad.piece >= 0 && CG_BoomboxInfo( BOX_OF( rad.piece ), &info ) ) {
		const qboolean playing = (qboolean)( ( info.flags & BOX_PLAYING ) != 0 );

		Radial_Add( RAD_PLAY, qtrue );
		Radial_Add( RAD_PAUSE, playing );
		Radial_Add( RAD_SKIP, playing );
		Radial_Add( RAD_STOP, playing );
		Radial_Add( RAD_LOUDER, (qboolean)( info.volume < 100 ) );
		Radial_Add( RAD_MOVE, qtrue );
		Radial_Add( RAD_REMOVE, qtrue );
		Radial_Add( RAD_QUIETER, (qboolean)( info.volume > 0 ) );
		Radial_Add( RAD_WINDOW, qtrue );
		return;
	}
	Radial_Add( RAD_PLACE_BOX, on );
	Radial_Add( RAD_PLACE_SYSTEM, CG_BoomboxSystems() );
	Radial_Add( RAD_PLAY, on );
	Radial_Add( RAD_WINDOW, (qboolean)( on && CG_BoomboxNear() >= 0 ) );
}

static const char *Radial_Label( radialAct_t act ) {
	boomboxInfo_t info;

	memset( &info, 0, sizeof( info ) );
	if ( rad.piece >= 0 )
		CG_BoomboxInfo( BOX_OF( rad.piece ), &info );
	switch ( act ) {
	case RAD_PLACE_BOX:		return CG_VisionText( "Put a boombox here" );
	case RAD_PLACE_SYSTEM:	return CG_VisionText( "Put a sound system here" );
	case RAD_PLAY:			return CG_VisionText( ( info.flags & BOX_PLAYING ) ? "Add a song ..." : "Play a song ..." );
	case RAD_WINDOW:		return CG_VisionText( "Open the player" );
	case RAD_PAUSE:			return CG_VisionText( ( info.flags & BOX_PAUSED ) ? "Resume" : "Pause" );
	case RAD_SKIP:			return CG_VisionText( "Skip" );
	case RAD_STOP:			return CG_VisionText( "Stop" );
	case RAD_LOUDER:		return CG_VisionTextWith( "Louder (%i %%)", va( "%i", info.volume ) );
	case RAD_QUIETER:		return CG_VisionTextWith( "Quieter (%i %%)", va( "%i", info.volume ) );
	case RAD_MOVE:			return CG_VisionText( "Move it" );
	case RAD_REMOVE:		return CG_VisionText( ( rad.confirmTime && cg.time - rad.confirmTime < RADIAL_CONFIRM_MSEC )
								? "Click again to take it away" : "Take it away" );
	}
	return "";
}

// where choice k stands round the middle: the first one at the top, then clockwise
static void Radial_Spot( int k, float *x, float *y ) {
	const float angle = -0.5f * M_PI + 2.0f * M_PI * k / Q_max( rad.num, 1 );

	*x = SCREEN_WIDTH * 0.5f + RADIAL_RADIUS * cosf( angle );
	*y = SCREEN_HEIGHT * 0.5f + RADIAL_RADIUS * sinf( angle );
}

// the choice in the direction of the pointer, -1: it is in the middle
static int Radial_Pointed( void ) {
	const float	dx = cgs.cursorX - SCREEN_WIDTH * 0.5f, dy = cgs.cursorY - SCREEN_HEIGHT * 0.5f;
	float		angle;
	int			k;

	if ( !rad.num || dx * dx + dy * dy < RADIAL_DEAD * RADIAL_DEAD )
		return -1;
	// (0 at the top, growing clockwise as the choices go)
	angle = atan2f( dy, dx ) + 0.5f * M_PI;
	if ( angle < 0.0f )
		angle += 2.0f * M_PI;
	k = (int)floorf( angle / ( 2.0f * M_PI ) * rad.num + 0.5f ) % rad.num;
	return k;
}

void CG_RadialClose( void ) {
	rad.open = rad.held = qfalse;
	rad.hover = -1;
	rad.confirmTime = 0;
}

qboolean CG_RadialOpen( void ) {
	return rad.open;
}

// does the choice; qfalse: the wheel stays open (the first click of "Take it away")
static qboolean Radial_Run( radialAct_t act ) {
	const int		box = ( rad.piece >= 0 ) ? BOX_OF( rad.piece ) : -1;
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
		CG_BoomboxWindowOpen( ( box >= 0 ) ? box : CG_BoomboxNear() );
		break;
	case RAD_PAUSE:
		if ( CG_BoomboxInfo( box, &info ) )
			CG_BoomboxDo( box, ( info.flags & BOX_PAUSED ) ? "resume" : "pause" );
		break;
	case RAD_SKIP:
		CG_BoomboxDo( box, "skip" );
		break;
	case RAD_STOP:
		CG_BoomboxDo( box, "stop" );
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
			return qfalse;
		}
		CG_BoomboxDo( box, "remove" );
		break;
	}
	return qtrue;
}

// the choice the pointer is on, if it can be had. qfalse: the wheel stays open.
static qboolean Radial_Pick( void ) {
	const int k = Radial_Pointed();

	if ( k < 0 || !rad.usable[k] )
		return qtrue;
	return Radial_Run( rad.acts[k] );
}

/*
=================
CG_RadialDown_f

+v_radial: the wheel opens, about the box under the pointer (while the mouse is free) or the cross hair
=================
*/
void CG_RadialDown_f( void ) {
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

// -v_radial: what the pointer is on is done, the wheel closes (but for the first click of "Take it away")
void CG_RadialUp_f( void ) {
	if ( !rad.open || !rad.held )
		return;
	// When the pointer takes the keys for the wheel (Key_SetCatcher) the engine lets go of every key that is held,
	// this one too, and sends "-v_radial <key> 0": without a time. That one is not the player letting go. (Taken
	// for one, the wheel closed, the key repeat opened it again and the pointer went back to the middle: it
	// flickered and the mouse did nothing.) A key let go without a time later on is the window losing the focus.
	if ( !atoi( CG_Argv( 2 ) ) && trap->Milliseconds() - rad.openTime < RADIAL_GRAB_MSEC )
		return;
	rad.held = qfalse;
	if ( Radial_Pick() )
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
=================
CG_RadialDraw

The wheel in the middle of the screen, the choice in the direction of the pointer lit, and in the middle what it is
about. In the 2D of the pointer (PLACE_CENTER).
=================
*/
void CG_RadialDraw( void ) {
	const vec4_t	back = { 0.02f, 0.05f, 0.09f, 0.55f }, fill = { 0.06f, 0.11f, 0.17f, 0.88f }, edge = { 0.43f, 0.83f, 1.0f, 0.35f };
	const vec4_t	lit = { 0.12f, 0.30f, 0.42f, 0.95f };
	const int		font = Skin_Ready() ? SKIN_FONT_SANS : FONT_SMALL2;
	const float		cx = SCREEN_WIDTH * 0.5f, cy = SCREEN_HEIGHT * 0.5f;
	const char		*title;
	float			dx, dy, length;
	int				k;

	if ( !rad.open )
		return;
	Radial_Build();
	// the pointer stays within the reach of the wheel
	dx = cgs.cursorX - cx;
	dy = cgs.cursorY - cy;
	length = sqrtf( dx * dx + dy * dy );
	if ( length > RADIAL_REACH ) {
		cgs.cursorX = (int)( cx + dx * RADIAL_REACH / length );
		cgs.cursorY = (int)( cy + dy * RADIAL_REACH / length );
	}
	rad.hover = Radial_Pointed();

	if ( Skin_Ready() ) {
		Skin_Soft( cx - RADIAL_RADIUS - 70.0f, cy - RADIAL_RADIUS - 30.0f, 2.0f * RADIAL_RADIUS + 140.0f, 2.0f * RADIAL_RADIUS + 60.0f, 30.0f, back );
		Skin_Disc( cx, cy, 44.0f, fill );
	}
	for ( k = 0; k < rad.num; k++ ) {
		const char	*label = Radial_Label( rad.acts[k] );
		const float	w = (float)CG_Text_Width( label, RADIAL_SCALE, font ) + 16.0f, h = 20.0f;
		const float	*color = !rad.usable[k] ? radialDim : ( rad.acts[k] == RAD_REMOVE && rad.confirmTime ) ? radialGold
						: ( k == rad.hover ) ? colorWhite : radialText;
		float		x, y;

		Radial_Spot( k, &x, &y );
		if ( Skin_Ready() ) {
			Skin_Panel( x - w * 0.5f, y - h * 0.5f, w, h, 8.0f, ( k == rad.hover && rad.usable[k] ) ? lit : fill, ( k == rad.hover ) ? radialAccent : edge );
		} else {
			CG_FillRect( x - w * 0.5f, y - h * 0.5f, w, h, ( k == rad.hover && rad.usable[k] ) ? lit : fill );
		}
		CG_Text_Paint( x - w * 0.5f + 8.0f, y - h * 0.5f + 2.0f, RADIAL_SCALE, (float *)color, label, 0, 0, 0, font );
	}
	// what it is about, in the middle, and a mark toward the pointer
	title = ( rad.piece >= 0 ) ? CG_BoomboxName( BOX_OF( rad.piece ) ) : CG_BoomboxOn() ? CG_VisionText( "Boombox" )
		: CG_VisionText( "No boombox here" );
	CG_Text_Paint( cx - CG_Text_Width( title, 0.5f, font ) * 0.5f, cy - 7.0f, 0.5f, (float *)radialAccent, title, 0, 0, 0, font );
	if ( length >= RADIAL_DEAD && Skin_Ready() ) {
		Skin_Disc( cx + dx / Q_max( length, 1.0f ) * 38.0f, cy + dy / Q_max( length, 1.0f ) * 38.0f, 4.0f, radialAccent );
	}
}
