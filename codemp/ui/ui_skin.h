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

/*
ui_skin.h -- the look of the menus and the HUD of this tree: panels with round corners, soft light, switches and
sliders, all of it drawn from pictures this file computes, so no file of a pk3 is involved. Not from JA+ and
not from the game, see docs/japlus/ui.md.

A menu asks for it with "skin 1", a window with "style WINDOW_STYLE_PANEL". A menu file without those words is
drawn as it always was. The file is built into the ui module and into the cgame, like ui_shared.c.
*/

#pragma once

#define SKIN_FONT_SANS		5		// "font 5" of a menu file: Noto Sans, every letter from the TrueType font
#define SKIN_FONT_MONO		6		// "font 6": DejaVu Sans Mono
#define SKIN_EASE_MSEC		130		// how long a mark takes to come and to go

qboolean	Skin_Ready( void );
void		Skin_Reset( void );
qhandle_t	Skin_FontHandle( int iMenuFont );
float		Skin_Ease( float value, qboolean on, int msec );
void		Skin_Color( vec4_t out, const vec4_t in, float alphaScale );

void		Skin_Panel( float x, float y, float w, float h, float radius, const vec4_t fill, const vec4_t edge );
void		Skin_Soft( float x, float y, float w, float h, float spread, const vec4_t color, qboolean add );
void		Skin_Disc( float cx, float cy, float radius, const vec4_t color );
void		Skin_Backdrop( void );
void		Skin_Cursor( float x, float y );
void		Skin_Logo( float x, float y, float w, float h, float alpha );
void		Skin_Cube( float x, float y, float w, float h );

// the widgets of a menu with "skin 1"
void		Skin_ItemBack( itemDef_t *item );
void		Skin_TextColor( itemDef_t *item, vec4_t out );
void		Skin_Switch( itemDef_t *item, float x, float y, float height, qboolean on );
void		Skin_Slider( itemDef_t *item, float x, float y, float width, float height, float fraction );
void		Skin_Field( itemDef_t *item, float x, float y, float width, float height, qboolean editing );
void		Skin_Bar( float x, float y, float w, float h, float fraction, const vec4_t fill, const vec4_t back );
void		Skin_ScrollBar( float x, float y, float w, float h, float start, float part, qboolean horizontal, const vec4_t accent );
void		Skin_Row( float x, float y, float w, float h, const vec4_t accent, qboolean selected, float hover );
