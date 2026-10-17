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

// cg_boombox_ui.c -- the window of a box of the boombox. Not from JA+, see docs/japlus/boombox.md.
//
// "Open the player" on the card of a box (select/cg_select.c) opens it in the middle of the screen: who put the
// box down, the song that plays and who asked for it, a bar to jump in it, pause, skip, stop, mute for this
// player, the volume for everybody, the line of songs with a cross to take one out, and a song to add. While it
// is open the mouse is free (select/cg_select.c draws it and hands it the keys), and it is watched: the server
// or the master sends the line of the box whenever it changes. Esc, the right button or its cross closes it.
//
// A server or a master of 0.1.7 or 0.1.8 can not jump nor take a song out nor send the line (CG_BoomboxMore()):
// there the window shows what the card shows, the next song, and has no bar to jump with.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"
#include "cg_boombox.h"

#define WIN_W				300.0f
#define WIN_TOP				60.0f		// of the window
#define WIN_PAD				10.0f
#define WIN_LINE			18.0f		// a line of text is this high at a scale of 1
#define WIN_ROWS			6			// songs of the line that are shown at once
#define WIN_ROW_H			14.0f
#define WIN_BUTTON_H		17.0f
#define WIN_BAR_H			6.0f		// the bar of the song and the one of the volume
#define WIN_GRAB			6.0f		// how far beside a bar it can still be taken
#define WIN_CONFIRM_MSEC	4000		// "Take it away" has to be clicked twice within this time
#define WIN_MAX_PARTS		32

typedef enum {
	PART_CLOSE,
	PART_SEEK,
	PART_PAUSE,
	PART_SKIP,
	PART_STOP,
	PART_MUTE,
	PART_VOLUME,
	PART_DROP,			// with the place of the song in the line
	PART_ADD,
	PART_MOVE,
	PART_REMOVE,
	PART_LINE			// the list, for the wheel
} winPart_t;

static struct {
	int			box;				// -1: closed
	int			scroll;				// the first song of the line that is shown after the one that plays
	int			confirmTime;		// when "Take it away" was clicked first, 0: not yet
	// a bar that is pressed and dragged: the song or the volume
	winPart_t	drag;
	qboolean	dragging;
	float		dragFraction;
	// what was drawn this frame, for the pointer
	float		x, y, h;
	struct {
		winPart_t	part;
		int			arg;
		float		x, y, w, h;
	} parts[WIN_MAX_PARTS];
	int			numParts;
	float		seekX, seekW, volumeX, volumeW;
} win = { -1 };

static const vec4_t winFill = { 0.035f, 0.07f, 0.12f, 0.9f }, winEdge = { 0.43f, 0.83f, 1.0f, 0.35f };
static const vec4_t winText = { 0.86f, 0.92f, 0.97f, 1.0f }, winDim = { 0.60f, 0.69f, 0.78f, 1.0f };
static const vec4_t winAccent = { 0.43f, 0.83f, 1.0f, 1.0f }, winGold = { 0.89f, 0.84f, 0.36f, 1.0f };
static const vec4_t winRed = { 1.0f, 0.42f, 0.36f, 1.0f };

/*
===============================================================================

Open and closed

===============================================================================
*/

void CG_BoomboxWindowOpen( int box ) {
	boomboxInfo_t info;

	if ( !CG_BoomboxInfo( box, &info ) ) {
		return;
	}
	memset( &win, 0, sizeof( win ) );
	win.box = box;
	CG_BoomboxWatch( box );
}

void CG_BoomboxWindowClose( void ) {
	if ( win.box < 0 ) {
		return;
	}
	win.box = -1;
	win.dragging = qfalse;
	CG_BoomboxWatch( -1 );
}

// is it open: the box is still there
qboolean CG_BoomboxWindowShown( void ) {
	boomboxInfo_t info;

	if ( win.box >= 0 && !CG_BoomboxInfo( win.box, &info ) ) {
		CG_BoomboxWindowClose();
	}
	return ( win.box >= 0 ) ? qtrue : qfalse;
}

qboolean CG_BoomboxWindowPointerOver( void ) {
	return ( CG_BoomboxWindowShown() && cgs.cursorX >= win.x && cgs.cursorX < win.x + WIN_W && cgs.cursorY >= win.y
		&& cgs.cursorY < win.y + win.h ) ? qtrue : qfalse;
}

/*
===============================================================================

Drawing

===============================================================================
*/

static int Win_Font( void ) {
	return Skin_Ready() ? SKIN_FONT_SANS : FONT_SMALL2;
}

static float Win_Width( const char *text, float scale ) {
	return (float)CG_Text_Width( text, scale, Win_Font() );
}

static float Win_FitWidth( const char *text, void *arg ) {
	return Win_Width( text, *(const float *)arg );
}

// a line of text with its top at y. align: 0 from x on, 1 ending at x, 2 with its middle at x. Cut with three dots
// where it is wider than most (0: as wide as it is).
static void Win_Text( float x, float y, float scale, const vec4_t color, int align, const char *text, float most ) {
	char line[160];

	if ( most > 0.0f && Win_Width( text, scale ) > most ) {
		scale = Q_max( scale * 0.85f, scale * most / Win_Width( text, scale ) * 0.98f );
		CG_TextCut( text, most, Win_FitWidth, &scale, line, sizeof( line ) );
		text = line;
	}
	if ( align ) {
		x -= Win_Width( text, scale ) * ( align == 2 ? 0.5f : 1.0f );
	}
	CG_Text_Paint( x, y, scale, (float *)color, text, 0, 0, 0, Win_Font() );
}

static void Win_Panel( float x, float y, float w, float h, float radius, const vec4_t fill, const vec4_t edge ) {
	if ( Skin_Ready() ) {
		Skin_Panel( x, y, w, h, radius, fill, edge );
		return;
	}
	if ( fill ) {
		CG_FillRect( x, y, w, h, (float *)fill );
	}
	if ( edge ) {
		CG_DrawRect( x, y, w, h, 1.0f, (float *)edge );
	}
}

static void Win_Line( float x, float y, float w ) {
	if ( Skin_Ready() ) {
		Skin_Line( x, y, w, winEdge );
	} else {
		CG_FillRect( x, y, w, 1.0f, (float *)winEdge );
	}
}

// something that can be pointed at, as it was drawn this frame
static void Win_Part( winPart_t part, int arg, float x, float y, float w, float h ) {
	if ( win.numParts < WIN_MAX_PARTS ) {
		win.parts[win.numParts].part = part;
		win.parts[win.numParts].arg = arg;
		win.parts[win.numParts].x = x;
		win.parts[win.numParts].y = y;
		win.parts[win.numParts].w = w;
		win.parts[win.numParts].h = h;
		win.numParts++;
	}
}

// the part the pointer is on, -1: none
static int Win_PartAt( void ) {
	int i;

	for ( i = win.numParts - 1; i >= 0; i-- ) {
		if ( cgs.cursorX >= win.parts[i].x && cgs.cursorX < win.parts[i].x + win.parts[i].w
			&& cgs.cursorY >= win.parts[i].y && cgs.cursorY < win.parts[i].y + win.parts[i].h ) {
			return i;
		}
	}
	return -1;
}

static qboolean Win_Over( float x, float y, float w, float h ) {
	return ( cgs.cursorX >= x && cgs.cursorX < x + w && cgs.cursorY >= y && cgs.cursorY < y + h ) ? qtrue : qfalse;
}

// a button: a pill with its text in the middle, lit under the pointer. Not usable: dim and nothing to click.
static void Win_Button( winPart_t part, int arg, float x, float y, float w, const char *text, const vec4_t color, qboolean usable ) {
	const vec4_t	back = { 1.0f, 1.0f, 1.0f, 0.06f }, lit = { 0.43f, 0.83f, 1.0f, 0.22f };
	const qboolean	over = (qboolean)( usable && Win_Over( x, y, w, WIN_BUTTON_H ) );

	Win_Panel( x, y, w, WIN_BUTTON_H, WIN_BUTTON_H * 0.5f, over ? lit : back, over ? winEdge : NULL );
	Win_Text( x + w * 0.5f, y + ( WIN_BUTTON_H - WIN_LINE * 0.52f ) * 0.5f, 0.52f, usable ? color : winDim, 2, text, w - 8.0f );
	if ( usable ) {
		Win_Part( part, arg, x, y, w, WIN_BUTTON_H );
	}
}

// a bar that can be taken: a track, what is filled of it, and a knob at its end
static void Win_Bar( winPart_t part, float x, float y, float w, float fraction, qboolean usable ) {
	const vec4_t back = { 1.0f, 1.0f, 1.0f, 0.1f };

	fraction = Com_Clamp( 0.0f, 1.0f, fraction );
	if ( Skin_Ready() ) {
		Skin_Bar( x, y, w, WIN_BAR_H, fraction, usable ? winAccent : winDim, back );
		if ( usable ) {
			Skin_Disc( x + w * fraction, y + WIN_BAR_H * 0.5f, ( win.dragging && win.drag == part ) ? 5.0f : 4.0f, colorWhite );
		}
	} else {
		CG_FillRect( x, y, w, WIN_BAR_H, (float *)back );
		CG_FillRect( x, y, w * fraction, WIN_BAR_H, (float *)( usable ? winAccent : winDim ) );
	}
	if ( usable ) {
		Win_Part( part, 0, x - WIN_GRAB, y - WIN_GRAB, w + 2.0f * WIN_GRAB, WIN_BAR_H + 2.0f * WIN_GRAB );
	}
}

static const char *Win_Time( int seconds ) {
	return va( "%i:%02i", seconds / 60, seconds % 60 );
}

// the place of the pointer on a bar, 0 to 1
static float Win_BarFraction( float x, float w ) {
	return Com_Clamp( 0.0f, 1.0f, ( cgs.cursorX - x ) / Q_max( 1.0f, w ) );
}

/*
=================
CG_BoomboxWindowDraw

Once a frame while it is open, in the 2D of the HUD, before the pointer
=================
*/
void CG_BoomboxWindowDraw( void ) {
	const float		x = ( SCREEN_WIDTH - WIN_W ) * 0.5f, inner = WIN_W - 2.0f * WIN_PAD;
	const qboolean	more = CG_BoomboxMore();
	boomboxInfo_t	info;
	const char		*status;
	float			y, h, fraction;
	int				songs, i, shown;
	qboolean		playing;

	win.numParts = 0;
	if ( !CG_BoomboxWindowShown() || !CG_BoomboxInfo( win.box, &info ) ) {
		return;
	}
	playing = (qboolean)( ( info.flags & BOX_PLAYING ) != 0 );
	songs = more ? CG_BoomboxSongs( win.box ) : 0;
	shown = more ? Q_min( WIN_ROWS, Q_max( 0, songs - 1 ) ) : ( info.waiting > 0 ? 1 : 0 );
	win.scroll = Com_Clampi( 0, Q_max( 0, songs - 1 - WIN_ROWS ), win.scroll );

	// how high it is: head, song, bar, buttons, volume, line, the buttons at the end, the last line of the server
	h = WIN_PAD + 34.0f + 40.0f + 26.0f + WIN_BUTTON_H + 8.0f + 18.0f + 6.0f + 16.0f + Q_max( 1, shown ) * WIN_ROW_H + 8.0f
		+ WIN_BUTTON_H + WIN_PAD + ( info.note[0] ? 14.0f : 0.0f );
	win.x = x;
	win.y = y = WIN_TOP;
	win.h = h;

	if ( Skin_Ready() ) {
		const vec4_t dark = { 0.0f, 0.0f, 0.0f, 0.45f };

		Skin_Soft( x, y + 4.0f, WIN_W, h, 14.0f, dark );
	}
	Win_Panel( x, y, WIN_W, h, 8.0f, winFill, winEdge );
	Win_Part( PART_LINE, -1, x, y, WIN_W, h );

	// the head: what it is, who put it down, and the cross
	y += WIN_PAD;
	Win_Text( x + WIN_PAD, y, 0.78f, colorWhite, 0, CG_VisionText( "Boombox" ), inner - 30.0f );
	Win_Button( PART_CLOSE, 0, x + WIN_W - WIN_PAD - 18.0f, y, 18.0f, "x", winText, qtrue );
	if ( info.owner[0] ) {
		Win_Text( x + WIN_PAD, y + 17.0f, 0.5f, winDim, 0, CG_VisionTextWith( "Put down by %s", info.owner ), inner );
	}
	y += 34.0f;
	Win_Line( x + WIN_PAD, y - 4.0f, inner );

	// the song, who asked for it, and what the box does
	if ( info.flags & BOX_LOOKING ) {
		status = CG_VisionText( "Looking a song up ..." );
	} else if ( !playing ) {
		status = CG_VisionText( "Silent" );
	} else if ( info.flags & BOX_LOADING ) {
		status = CG_VisionText( "Loading ..." );
	} else if ( info.flags & BOX_PAUSED ) {
		status = CG_VisionText( "Paused" );
	} else {
		status = CG_VisionText( "Plays" );
	}
	Win_Text( x + WIN_W - WIN_PAD, y + 2.0f, 0.5f, winGold, 1, status, 110.0f );
	Win_Text( x + WIN_PAD, y + 16.0f, 0.68f, playing ? winAccent : winDim, 0, playing ? info.title : CG_VisionText( "Nothing plays" ), inner );
	if ( playing && info.by[0] ) {
		Win_Text( x + WIN_PAD, y + 30.0f, 0.5f, winDim, 0, CG_VisionTextWith( "Asked for by %s", info.by ), inner );
	}
	y += 40.0f;

	// the bar of the song, and its times
	win.seekX = x + WIN_PAD;
	win.seekW = inner;
	fraction = ( info.length > 0 ) ? info.msec / ( info.length * 1000.0f ) : 0.0f;
	if ( win.dragging && win.drag == PART_SEEK ) {
		fraction = win.dragFraction = Win_BarFraction( win.seekX, win.seekW );
	}
	Win_Bar( PART_SEEK, win.seekX, y + 4.0f, win.seekW, fraction, (qboolean)( more && playing && info.length > 0 ) );
	if ( playing ) {
		Win_Text( x + WIN_PAD, y + 13.0f, 0.46f, winDim, 0, Win_Time( (int)( fraction * info.length ) ), 0.0f );
		Win_Text( x + WIN_W - WIN_PAD, y + 13.0f, 0.46f, winDim, 1, Win_Time( info.length ), 0.0f );
	}
	y += 26.0f;

	// what can be done with the song
	{
		const float gap = 5.0f, w = ( inner - 3.0f * gap ) / 4.0f;
		float		bx = x + WIN_PAD;

		Win_Button( PART_PAUSE, 0, bx, y, w, CG_VisionText( ( info.flags & BOX_PAUSED ) ? "Resume" : "Pause" ), winText, playing );
		bx += w + gap;
		Win_Button( PART_SKIP, 0, bx, y, w, CG_VisionText( "Skip" ), winText, playing );
		bx += w + gap;
		Win_Button( PART_STOP, 0, bx, y, w, CG_VisionText( "Stop" ), winText, playing );
		bx += w + gap;
		Win_Button( PART_MUTE, 0, bx, y, w, CG_VisionText( info.muted ? "Hear it again" : "Mute it for me" ), info.muted ? winGold : winText, qtrue );
	}
	y += WIN_BUTTON_H + 8.0f;

	// the volume for everybody
	{
		const float	label = 70.0f, number = 34.0f;
		int			volume = info.volume;

		win.volumeX = x + WIN_PAD + label;
		win.volumeW = inner - label - number;
		if ( win.dragging && win.drag == PART_VOLUME ) {
			win.dragFraction = Win_BarFraction( win.volumeX, win.volumeW );
			volume = (int)( win.dragFraction * 100.0f + 0.5f );
		}
		Win_Text( x + WIN_PAD, y, 0.5f, winDim, 0, CG_VisionText( "Volume" ), label - 6.0f );
		Win_Bar( PART_VOLUME, win.volumeX, y + 3.0f, win.volumeW, volume / 100.0f, qtrue );
		Win_Text( x + WIN_W - WIN_PAD, y, 0.5f, winText, 1, va( "%i %%", volume ), 0.0f );
	}
	y += 18.0f;
	Win_Line( x + WIN_PAD, y, inner );
	y += 6.0f;

	// the line of songs after the one that plays
	Win_Text( x + WIN_PAD, y, 0.55f, winText, 0, more ? CG_VisionTextWith( "In the line: %s", va( "%i", Q_max( 0, songs - 1 ) ) )
		: CG_VisionText( "In the line" ), inner );
	y += 16.0f;
	if ( !shown ) {
		Win_Text( x + WIN_PAD + 4.0f, y + 1.0f, 0.5f, winDim, 0, CG_VisionText( "Nothing waits" ), inner );
	} else if ( !more ) {
		// (a server of 0.1.7 or 0.1.8 says only the next one and how many there are)
		Win_Text( x + WIN_PAD + 4.0f, y + 1.0f, 0.5f, winText, 0, info.next, inner - 30.0f );
		if ( info.waiting > 1 ) {
			Win_Text( x + WIN_W - WIN_PAD, y + 1.0f, 0.5f, winDim, 1, va( "+%i", info.waiting - 1 ), 0.0f );
		}
	} else {
		for ( i = 0; i < shown; i++ ) {
			const int	place = 1 + win.scroll + i;
			const float	rowY = y + i * WIN_ROW_H, cross = 14.0f;
			const char	*title, *by;
			int			seconds;

			if ( Skin_Ready() ) {
				Skin_Row( x + WIN_PAD, rowY, inner, WIN_ROW_H, winAccent, qfalse, Win_Over( x + WIN_PAD, rowY, inner, WIN_ROW_H ) ? 1.0f : 0.0f );
			}
			Win_Text( x + WIN_PAD + 3.0f, rowY + 1.5f, 0.46f, winDim, 0, va( "%i", place ), 0.0f );
			if ( !CG_BoomboxSong( win.box, place, &title, &by, &seconds ) ) {
				Win_Text( x + WIN_PAD + 20.0f, rowY + 1.5f, 0.5f, winDim, 0, "...", 0.0f );
				continue;
			}
			Win_Text( x + WIN_PAD + 20.0f, rowY + 1.5f, 0.5f, winText, 0, title, inner - 20.0f - 110.0f );
			Win_Text( x + WIN_W - WIN_PAD - cross - 40.0f, rowY + 1.5f, 0.46f, winDim, 1, by, 64.0f );
			Win_Text( x + WIN_W - WIN_PAD - cross - 4.0f, rowY + 1.5f, 0.46f, winDim, 1, Win_Time( seconds ), 0.0f );
			{
				const float		cx = x + WIN_W - WIN_PAD - cross;
				const qboolean	over = Win_Over( cx, rowY, cross, WIN_ROW_H );

				Win_Text( cx + cross * 0.5f, rowY + 1.0f, 0.5f, over ? winRed : winDim, 2, "x", 0.0f );
				Win_Part( PART_DROP, place, cx, rowY, cross, WIN_ROW_H );
			}
		}
		if ( songs - 1 > WIN_ROWS && Skin_Ready() ) {
			Skin_ScrollBar( x + WIN_W - 6.0f, y, 4.0f, shown * WIN_ROW_H, (float)win.scroll / ( songs - 1 ), (float)WIN_ROWS / ( songs - 1 ),
				qfalse, winAccent );
		}
	}
	y += Q_max( 1, shown ) * WIN_ROW_H + 8.0f;

	// a song to add, move the box, take it away
	{
		const float		gap = 5.0f, w = ( inner - 2.0f * gap ) / 3.0f;
		const qboolean	confirm = (qboolean)( win.confirmTime && cg.time - win.confirmTime < WIN_CONFIRM_MSEC );
		float			bx = x + WIN_PAD;

		Win_Button( PART_ADD, 0, bx, y, w, CG_VisionText( playing ? "Add a song ..." : "Play a song ..." ), winAccent, qtrue );
		bx += w + gap;
		Win_Button( PART_MOVE, 0, bx, y, w, CG_VisionText( "Move it" ), winText, qtrue );
		bx += w + gap;
		Win_Button( PART_REMOVE, 0, bx, y, w, CG_VisionText( confirm ? "Click again to take it away" : "Take it away" ), confirm ? winRed : winText, qtrue );
	}
	y += WIN_BUTTON_H;

	// the last line the server had for this player
	if ( info.note[0] ) {
		Win_Text( x + WIN_PAD, y + 4.0f, 0.48f, winGold, 0, info.note, inner );
	}
}

/*
===============================================================================

Keys

===============================================================================
*/

// one part of the window was clicked
static void Win_Run( winPart_t part, int arg ) {
	boomboxInfo_t info;

	if ( !CG_BoomboxInfo( win.box, &info ) ) {
		CG_BoomboxWindowClose();
		return;
	}
	switch ( part ) {
	case PART_CLOSE:
		CG_BoomboxWindowClose();
		break;
	case PART_SEEK:
	case PART_VOLUME:
		// (taken: it follows the pointer until the button is let go)
		win.drag = part;
		win.dragging = qtrue;
		break;
	case PART_PAUSE:
		CG_BoomboxDo( win.box, ( info.flags & BOX_PAUSED ) ? "resume" : "pause" );
		break;
	case PART_SKIP:
		CG_BoomboxDo( win.box, "skip" );
		break;
	case PART_STOP:
		CG_BoomboxDo( win.box, "stop" );
		break;
	case PART_MUTE:
		CG_VoiceBoxMute( win.box, (qboolean)!info.muted );
		break;
	case PART_DROP:
		CG_BoomboxDropSong( win.box, arg );
		break;
	case PART_ADD:
		// (the chat box asks for it, the window stays)
		CG_BoomboxPrompt( win.box );
		break;
	case PART_MOVE: {
		const int box = win.box;

		CG_BoomboxWindowClose();
		CG_SelectCarry( box );
		break;
	}
	case PART_REMOVE:
		if ( !win.confirmTime || cg.time - win.confirmTime >= WIN_CONFIRM_MSEC ) {
			win.confirmTime = cg.time ? cg.time : 1;
			break;
		}
		CG_BoomboxDo( win.box, "remove" );
		CG_BoomboxWindowClose();
		break;
	case PART_LINE:
		break;
	}
}

/*
=================
CG_BoomboxWindowKey

A key or a button while the window is open. qtrue: it was for the window. A click beside it is not: it goes to
the level, where another player or box can be clicked.
=================
*/
qboolean CG_BoomboxWindowKey( int key, qboolean down ) {
	int at;

	if ( !CG_BoomboxWindowShown() || CG_ChatIsOpen() ) {
		return qfalse;
	}
	// a bar that was taken is let go: the song jumps there, the volume is set
	if ( !down && key == A_MOUSE1 && win.dragging ) {
		boomboxInfo_t info;

		win.dragging = qfalse;
		if ( CG_BoomboxInfo( win.box, &info ) ) {
			if ( win.drag == PART_SEEK ) {
				CG_BoomboxSeek( win.box, (int)( Win_BarFraction( win.seekX, win.seekW ) * info.length ) );
			} else {
				CG_BoomboxSetVolume( win.box, (int)( Win_BarFraction( win.volumeX, win.volumeW ) * 100.0f + 0.5f ) );
			}
		}
		return qtrue;
	}
	if ( !down ) {
		return CG_BoomboxWindowPointerOver();
	}
	if ( key == A_ESCAPE || key == A_MOUSE2 ) {
		CG_BoomboxWindowClose();
		return qtrue;
	}
	if ( ( key == A_MWHEELUP || key == A_MWHEELDOWN ) && CG_BoomboxWindowPointerOver() ) {
		win.scroll += ( key == A_MWHEELUP ) ? -1 : 1;
		return qtrue;
	}
	if ( key != A_MOUSE1 ) {
		// (every other key is swallowed while the mouse is free, as without the window)
		return qfalse;
	}
	at = Win_PartAt();
	if ( at < 0 ) {
		return qfalse;
	}
	Win_Run( win.parts[at].part, win.parts[at].arg );
	return qtrue;
}
