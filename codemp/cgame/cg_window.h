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

// cg_window.h -- what the windows the cgame draws itself have in common: the window of a box of the boombox
// (boombox/cg_boombox_ui.c) and the tool window of the toolgun (sandbox/cg_tools_ui.c). Their colours, text,
// panels, buttons and bars in the look of the new menus (ui/ui_skin.h), and what of them can be clicked. A
// window keeps its own list of that (winHits_t), made anew each frame as it is drawn. Not from JA+.

#pragma once

#define WIN_LINE			18.0f		// a line of text is this high at a scale of 1
#define WIN_BUTTON_H		17.0f
#define WIN_BAR_H			6.0f		// a bar to drag
#define WIN_GRAB			6.0f		// how far beside a bar it can still be taken
#define WIN_MAX_HITS		64

// what can be pointed at, as a window drew it this frame: its part (what the window calls it) and a number for it
typedef struct winHits_s {
	struct {
		int		part, arg;
		float	x, y, w, h;
	} hit[WIN_MAX_HITS];
	int		num;
} winHits_t;

extern const vec4_t winFill, winEdge, winText, winDim, winAccent, winGold, winRed;

int			Win_Font( void );
float		Win_Width( const char *text, float scale );
void		Win_Text( float x, float y, float scale, const vec4_t color, int align, const char *text, float most );
void		Win_Panel( float x, float y, float w, float h, float radius, const vec4_t fill, const vec4_t edge );
void		Win_Line( float x, float y, float w );
qboolean	Win_Over( float x, float y, float w, float h );
void		Win_Hit( winHits_t *hits, int part, int arg, float x, float y, float w, float h );
int			Win_HitAt( const winHits_t *hits );
void		Win_Button( winHits_t *hits, int part, int arg, float x, float y, float w, const char *text, const vec4_t color, qboolean usable );
void		Win_Bar( winHits_t *hits, int part, float x, float y, float w, float fraction, qboolean usable, qboolean dragged );
float		Win_BarFraction( float x, float w );
