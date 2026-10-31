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

// cg_window.c -- the parts of the windows the cgame draws itself, see cg_window.h. Not from JA+.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"
#include "cg_window.h"

const vec4_t winFill = { 0.035f, 0.07f, 0.12f, 0.9f }, winEdge = { 0.43f, 0.83f, 1.0f, 0.35f };
const vec4_t winText = { 0.86f, 0.92f, 0.97f, 1.0f }, winDim = { 0.60f, 0.69f, 0.78f, 1.0f };
const vec4_t winAccent = { 0.43f, 0.83f, 1.0f, 1.0f }, winGold = { 0.89f, 0.84f, 0.36f, 1.0f };
const vec4_t winRed = { 1.0f, 0.42f, 0.36f, 1.0f };

int Win_Font( void ) {
	return Skin_Ready() ? SKIN_FONT_SANS : FONT_SMALL2;
}

float Win_Width( const char *text, float scale ) {
	return (float)CG_Text_Width( text, scale, Win_Font() );
}

static float Win_FitWidth( const char *text, void *arg ) {
	return Win_Width( text, *(const float *)arg );
}

// a line of text with its top at y. align: 0 from x on, 1 ending at x, 2 with its middle at x. Cut with three dots
// where it is wider than most (0: as wide as it is).
void Win_Text( float x, float y, float scale, const vec4_t color, int align, const char *text, float most ) {
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

void Win_Panel( float x, float y, float w, float h, float radius, const vec4_t fill, const vec4_t edge ) {
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

void Win_Line( float x, float y, float w ) {
	if ( Skin_Ready() ) {
		Skin_Line( x, y, w, winEdge );
	} else {
		CG_FillRect( x, y, w, 1.0f, (float *)winEdge );
	}
}

qboolean Win_Over( float x, float y, float w, float h ) {
	return ( cgs.cursorX >= x && cgs.cursorX < x + w && cgs.cursorY >= y && cgs.cursorY < y + h ) ? qtrue : qfalse;
}

// something that can be pointed at, as it was drawn this frame
void Win_Hit( winHits_t *hits, int part, int arg, float x, float y, float w, float h ) {
	if ( hits->num < WIN_MAX_HITS ) {
		hits->hit[hits->num].part = part;
		hits->hit[hits->num].arg = arg;
		hits->hit[hits->num].x = x;
		hits->hit[hits->num].y = y;
		hits->hit[hits->num].w = w;
		hits->hit[hits->num].h = h;
		hits->num++;
	}
}

// the one the pointer is on, the last one drawn there, -1: none
int Win_HitAt( const winHits_t *hits ) {
	int i;

	for ( i = hits->num - 1; i >= 0; i-- ) {
		if ( Win_Over( hits->hit[i].x, hits->hit[i].y, hits->hit[i].w, hits->hit[i].h ) ) {
			return i;
		}
	}
	return -1;
}

// a button: a pill with its text in the middle, lit under the pointer. Not usable: dim and nothing to click.
void Win_Button( winHits_t *hits, int part, int arg, float x, float y, float w, const char *text, const vec4_t color, qboolean usable ) {
	const vec4_t	back = { 1.0f, 1.0f, 1.0f, 0.06f }, lit = { 0.43f, 0.83f, 1.0f, 0.22f };
	const qboolean	over = (qboolean)( usable && Win_Over( x, y, w, WIN_BUTTON_H ) );

	Win_Panel( x, y, w, WIN_BUTTON_H, WIN_BUTTON_H * 0.5f, over ? lit : back, over ? winEdge : NULL );
	Win_Text( x + w * 0.5f, y + ( WIN_BUTTON_H - WIN_LINE * 0.52f ) * 0.5f, 0.52f, usable ? color : winDim, 2, text, w - 8.0f );
	if ( usable ) {
		Win_Hit( hits, part, arg, x, y, w, WIN_BUTTON_H );
	}
}

// a bar that can be taken: a track, what is filled of it, and a knob at its end, larger while it is dragged
void Win_Bar( winHits_t *hits, int part, float x, float y, float w, float fraction, qboolean usable, qboolean dragged ) {
	const vec4_t back = { 1.0f, 1.0f, 1.0f, 0.1f };

	fraction = Com_Clamp( 0.0f, 1.0f, fraction );
	if ( Skin_Ready() ) {
		Skin_Bar( x, y, w, WIN_BAR_H, fraction, usable ? winAccent : winDim, back );
		if ( usable ) {
			Skin_Disc( x + w * fraction, y + WIN_BAR_H * 0.5f, dragged ? 5.0f : 4.0f, colorWhite );
		}
	} else {
		CG_FillRect( x, y, w, WIN_BAR_H, (float *)back );
		CG_FillRect( x, y, w * fraction, WIN_BAR_H, (float *)( usable ? winAccent : winDim ) );
	}
	if ( usable ) {
		Win_Hit( hits, part, 0, x - WIN_GRAB, y - WIN_GRAB, w + 2.0f * WIN_GRAB, WIN_BAR_H + 2.0f * WIN_GRAB );
	}
}

// the place of the pointer on a bar, 0 to 1
float Win_BarFraction( float x, float w ) {
	return Com_Clamp( 0.0f, 1.0f, ( cgs.cursorX - x ) / Q_max( 1.0f, w ) );
}
