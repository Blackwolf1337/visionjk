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

// cg_hudkit.h -- the pieces of the new HUD (cg_hudkit.c). Include after ui/ui_shared.h.

#pragma once

#define HUD_LEFT		0
#define HUD_RIGHT		1
#define HUD_CENTER		2

// the sizes of text, scales of the font of the new look (a line of scale 1 is 18 units high)
#define HUD_CAPS		0.30f		// captions in capitals
#define HUD_SMALL		0.36f
#define HUD_BODY		0.42f
#define HUD_LARGE		0.62f
#define HUD_HUGE		1.0f

#define HUD_EDGE		10.0f		// from the edge of the screen
#define HUD_GAP			4.0f		// between two parts

extern const vec4_t hudFill, hudSoftFill, hudText, hudBright, hudDim, hudAccent, hudWarm, hudRed, hudBlue, hudGreen;
extern const vec4_t hudAmmo, hudForce;

void		Hud_Color( vec4_t out, const float *rgb, float alpha );
void		Hud_Glass( float x, float y, float w, float h, float radius, float alpha, const float *edge, qboolean soft );
void		Hud_Rim( float x, float y, float w, float h, float radius, const float *rgb, float alpha );
void		Hud_Glow( float x, float y, float w, float h, float spread, const float *rgb, float alpha );
float		Hud_TextWidth( const char *text, float scale );
float		Hud_TextHeight( float scale );
float		Hud_Text( float x, float y, float scale, const float *rgb, float alpha, int align, const char *text, float most );
float		Hud_TextMid( float x, float midY, float scale, const float *rgb, float alpha, int align, const char *text, float most );
float		Hud_Caps( float x, float midY, const float *rgb, float alpha, int align, const char *text );
float		Hud_Key( float x, float midY, const char *name, float alpha, int align );
const char	*Hud_KeyFor( const char *command, const char *fallback );
float		Hud_KeyHint( float x, float midY, const char *key, const char *what, float alpha );
float		Hud_KeyHintWidth( const char *key, const char *what );
void		Hud_Bar( float x, float y, float w, float h, float fraction, const float *rgb, float alpha, qboolean fromRight );
float		Hud_ChipWidth( float h, qhandle_t icon, const char *text, float bar );
float		Hud_Chip( float x, float y, float h, qhandle_t icon, const char *text, const float *rgb, float alpha, int align, float bar );
qboolean	Hud_ItemRect( const char *menuName, const char *itemName, rectDef_t *out );
void		Hud_PanelRect( qboolean right, rectDef_t *out );
const float	*Hud_TeamColor( int team );
const char	*Hud_Clock( int msec );
