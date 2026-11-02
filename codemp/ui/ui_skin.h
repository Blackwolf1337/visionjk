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
#define SKIN_FONT_BOLD		7		// "font 7": Noto Sans Bold
#define SKIN_EASE_MSEC		130		// how long a mark takes to come and to go
#define SKIN_RADIUS			3.2f	// the corners of a field, a choice, a row: 6 pixels of a window 900 high
#define SKIN_RADIUS_INNER	2.1f	// the corners of the part of a choice that is chosen

// The colours of the widgets (docs/japlus/ui.md, "The look"). The accent is not among them: it is the
// focusColor of the menu, so a menu file can change it.
extern const vec4_t skinField;			// inside of a field, a choice, a switch that is off
extern const vec4_t skinEdge;			// their outline
extern const vec4_t skinKeyFill;		// a key a command is bound to
extern const vec4_t skinKeyEdge;
extern const vec4_t skinOnAccent;		// words on the accent: the setting that is chosen
extern const vec4_t skinMuted;			// words of a setting that is not chosen
extern const vec4_t skinFaint;			// a key that is not bound, the knob of a switch that is off
extern const vec4_t skinRule;			// the line under a row
extern const vec4_t skinTrack;			// the part of a slider above what it stands at
extern const vec4_t skinThumb;			// the knob of a slider
extern const vec4_t skinScrollThumb;	// the bar beside a list

qboolean	Skin_Ready( void );
void		Skin_Reset( void );
qhandle_t	Skin_FontHandle( int iMenuFont );
float		Skin_Ease( float value, qboolean on, int msec );
void		Skin_Color( vec4_t out, const vec4_t in, float alphaScale );

void		Skin_Panel( float x, float y, float w, float h, float radius, const vec4_t fill, const vec4_t edge );
void		Skin_Soft( float x, float y, float w, float h, float spread, const vec4_t color );
void		Skin_Disc( float cx, float cy, float radius, const vec4_t color );
void		Skin_CardHeadBand( float top, float line, float *y, float *h );
void		Skin_Arrow( float x, float y, float w, float h, qboolean left, const vec4_t color );
void		Skin_ArrowDown( float x, float y, float w, float h, const vec4_t color );
void		Skin_Line( float x, float y, float w, const vec4_t color );
void		Skin_Backdrop( void );
void		Skin_Cursor( float x, float y );
void		Skin_Logo( float x, float y, float w, float h, float alpha );
void		Skin_Cube( float x, float y, float w, float h );
qhandle_t	Skin_NoPicture( void );
void		Skin_LoadPanel( const char *title, const char *about, const char *now, float fraction );

// the widgets of a menu with "skin 1"
void		Skin_ItemBack( itemDef_t *item );
void		Skin_CardHeadBack( itemDef_t *item, float x, float y, float w, float h, float radius );
void		Skin_TextColor( itemDef_t *item, vec4_t out );
void		Skin_Switch( itemDef_t *item, float x, float y, float height, qboolean on );
void		Skin_Slider( itemDef_t *item, float x, float y, float width, float height, float fraction );
void		Skin_SliderTrack( float x, float mid, float width, float fraction, const vec4_t accent, float focus, float alpha, float unit );
void		Skin_Field( itemDef_t *item, float x, float y, float width, float height, qboolean editing );
void		Skin_Bar( float x, float y, float w, float h, float fraction, const vec4_t fill, const vec4_t back );
void		Skin_ScrollBar( float x, float y, float w, float h, float start, float part, qboolean horizontal, const vec4_t accent );
void		Skin_Row( float x, float y, float w, float h, const vec4_t accent, qboolean selected, float hover );
void		Skin_Tile( float x, float y, float w, float h, const vec4_t accent, qboolean selected, qboolean hover );
