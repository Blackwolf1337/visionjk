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

// cg_hudkit.c -- VisionJK: the pieces the parts of the new HUD are drawn with that its menus (ui/vision/hud.menu)
// do not draw: glass, a chip, the cap of a key, a caption, a bar, text, and the colours of the design (the canvas
// "VisionJK HUD", the tokens of the design system VisionJK). Units of the 640 x 480 screen; the caller sets the
// place (CG_Place2D). Only for the new look: Skin_Ready() is true where these are called. Not from JA+.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"
#include "cg_hudkit.h"

const vec4_t hudFill = { 0.035f, 0.07f, 0.12f, 0.80f };
const vec4_t hudSoftFill = { 0.035f, 0.07f, 0.12f, 0.62f };
const vec4_t hudText = { 0.86f, 0.92f, 0.97f, 1.0f };
const vec4_t hudBright = { 1.0f, 1.0f, 1.0f, 1.0f };
const vec4_t hudDim = { 0.60f, 0.69f, 0.78f, 1.0f };
const vec4_t hudAccent = { 0.43f, 0.83f, 1.0f, 1.0f };
const vec4_t hudWarm = { 1.0f, 0.74f, 0.30f, 1.0f };
const vec4_t hudRed = { 1.0f, 0.62f, 0.59f, 1.0f };
const vec4_t hudBlue = { 0.66f, 0.78f, 1.0f, 1.0f };
const vec4_t hudGreen = { 0.38f, 0.88f, 0.56f, 1.0f };
const vec4_t hudAmmo = { 1.0f, 0.72f, 0.29f, 1.0f };
const vec4_t hudForce = { 0.66f, 0.71f, 1.0f, 1.0f };

void Hud_Color( vec4_t out, const float *rgb, float alpha ) {
	VectorCopy( rgb, out );
	out[3] = alpha;
}

/*
=================
Hud_Glass

A panel of the HUD: frosted glass behind it (once it is nearly there, glass can not fade), the fill thinner over
it, a rim. edge: the rgb of the rim, NULL the rim of the HUD. alpha: how much it shows. soft: the lighter panel
of something that is not a main part (a line of the chat, a notice).
=================
*/
void Hud_Glass( float x, float y, float w, float h, float radius, float alpha, const float *edge, qboolean soft ) {
	vec4_t fill, rim;

	if ( alpha <= 0.0f || w <= 0.0f || h <= 0.0f ) {
		return;
	}
	memcpy( fill, soft ? hudSoftFill : hudFill, sizeof( fill ) );
	if ( alpha > 0.6f && CG_DrawBackdrop( x, y, w, h, radius ) ) {
		fill[3] *= soft ? 0.7f : 0.6f;
	}
	fill[3] *= alpha;
	if ( edge ) {
		Hud_Color( rim, edge, 0.7f * alpha );
	} else {
		Hud_Color( rim, hudAccent, ( soft ? 0.16f : 0.28f ) * alpha );
	}
	Skin_Panel( x, y, w, h, radius, fill, rim );
}

// a rim of that colour over a panel that is drawn already (the colour of the team round the panels of the HUD)
void Hud_Rim( float x, float y, float w, float h, float radius, const float *rgb, float alpha ) {
	vec4_t rim;

	Hud_Color( rim, rgb, alpha );
	Skin_Panel( x, y, w, h, radius, NULL, rim );
}

void Hud_Glow( float x, float y, float w, float h, float spread, const float *rgb, float alpha ) {
	vec4_t glow;

	Hud_Color( glow, rgb, alpha );
	Skin_Soft( x, y, w, h, spread, glow );
}

float Hud_TextWidth( const char *text, float scale ) {
	return (float)CG_Text_Width( text, scale, SKIN_FONT_SANS );
}

float Hud_TextHeight( float scale ) {
	return (float)CG_Text_Height( "A", scale, SKIN_FONT_SANS );
}

/*
=================
Hud_Text

A line with its top at y. align: HUD_LEFT from x on, HUD_RIGHT ending at x, HUD_CENTER its middle at x. most > 0:
cut with three dots where it is wider. Returns its width.
=================
*/
static float Hud_FitWidth( const char *text, void *arg ) {
	return Hud_TextWidth( text, *(const float *)arg );
}

float Hud_Text( float x, float y, float scale, const float *rgb, float alpha, int align, const char *text, float most ) {
	char	line[256];
	vec4_t	color;
	float	w;

	if ( !text || !text[0] || alpha <= 0.0f ) {
		return 0.0f;
	}
	if ( most > 0.0f && Hud_TextWidth( text, scale ) > most ) {
		CG_TextCut( text, most, Hud_FitWidth, &scale, line, sizeof( line ) );
		text = line;
	}
	w = Hud_TextWidth( text, scale );
	if ( align == HUD_RIGHT ) {
		x -= w;
	} else if ( align == HUD_CENTER ) {
		x -= w * 0.5f;
	}
	Hud_Color( color, rgb, alpha );
	CG_Text_Paint( x, y, scale, color, text, 0, 0, 0, SKIN_FONT_SANS );
	return w;
}

// a line with its middle at midY
float Hud_TextMid( float x, float midY, float scale, const float *rgb, float alpha, int align, const char *text, float most ) {
	return Hud_Text( x, midY - Hud_TextHeight( scale ) * 0.5f, scale, rgb, alpha, align, text, most );
}

// a caption: the text in capitals, small, spread a little (the headings of cards, the names of numbers)
float Hud_Caps( float x, float midY, const float *rgb, float alpha, int align, const char *text ) {
	char upper[256];

	Q_strncpyz( upper, text ? text : "", sizeof( upper ) );
	Q_strupr( upper );
	return Hud_TextMid( x, midY, HUD_CAPS, rgb ? rgb : hudDim, alpha, align, upper, 0.0f );
}

/*
=================
Hud_Key

The cap of a key with its name in it, its middle at midY. align as Hud_Text. Returns its width.
=================
*/
float Hud_Key( float x, float midY, const char *text, float alpha, int align ) {
	const float	h = 8.0f, pad = 3.0f;
	char		name[64];
	float		w;
	vec4_t		edge;

	Q_strncpyz( name, text, sizeof( name ) );
	Q_strupr( name );
	w = Q_max( h, Hud_TextWidth( name, HUD_CAPS ) + 2.0f * pad );

	if ( align == HUD_RIGHT ) {
		x -= w;
	} else if ( align == HUD_CENTER ) {
		x -= w * 0.5f;
	}
	Hud_Color( edge, hudDim, 0.6f * alpha );
	Skin_Panel( x, midY - h * 0.5f, w, h, 1.8f, NULL, edge );
	Hud_TextMid( x + w * 0.5f, midY, HUD_CAPS, hudText, alpha, HUD_CENTER, name, 0.0f );
	return w;
}

// the key bound to a command, as a hint names it, or the fallback when there is none the cgame can name
const char *Hud_KeyFor( const char *command, const char *fallback ) {
	const char *name = CG_KeyLabel( trap->Key_GetKey( command ) );

	return name ? name : fallback;
}

/*
=================
Hud_KeyHint

A key and what it does after it, from x on: "[KEY] does this". Returns the width.
=================
*/
float Hud_KeyHint( float x, float midY, const char *key, const char *what, float alpha ) {
	const float kw = Hud_Key( x, midY, key, alpha, HUD_LEFT );

	return kw + 3.0f + Hud_TextMid( x + kw + 3.0f, midY, HUD_SMALL, hudDim, alpha, HUD_LEFT, what, 0.0f );
}

// how wide Hud_KeyHint draws that
float Hud_KeyHintWidth( const char *key, const char *what ) {
	char name[64];

	Q_strncpyz( name, key, sizeof( name ) );
	Q_strupr( name );
	return Q_max( 8.0f, Hud_TextWidth( name, HUD_CAPS ) + 6.0f ) + 3.0f + Hud_TextWidth( what, HUD_SMALL );
}

/*
=================
Hud_Bar

A bar with round ends; fromRight: it is filled from the right (the bars of the right panel).
=================
*/
void Hud_Bar( float x, float y, float w, float h, float fraction, const float *rgb, float alpha, qboolean fromRight ) {
	vec4_t	fill, back;
	float	fw;

	fraction = Com_Clamp( 0.0f, 1.0f, fraction );
	Hud_Color( back, colorWhite, 0.10f * alpha );
	Skin_Panel( x, y, w, h, h * 0.5f, back, NULL );
	if ( fraction <= 0.0f ) {
		return;
	}
	fw = Q_max( h, w * fraction );
	Hud_Color( fill, rgb, alpha );
	Skin_Panel( fromRight ? x + w - fw : x, y, fw, h, h * 0.5f, fill, NULL );
}

/*
=================
Hud_Chip

A chip of a strip: glass of height h with a picture (0: none) and a caption (NULL: none), the rim in rgb (NULL:
the rim of the HUD). align HUD_LEFT: from x on, HUD_RIGHT: ending at x. bar >= 0: a bar under the caption that
runs down (a fuel, a time). Returns its width.
=================
*/
float Hud_ChipWidth( float h, qhandle_t icon, const char *text, float bar ) {
	float w = 0.0f;

	if ( icon ) {
		w += h;
	}
	if ( text && text[0] ) {
		char upper[256];

		Q_strncpyz( upper, text, sizeof( upper ) );
		Q_strupr( upper );
		w += ( icon ? 0.0f : h * 0.35f ) + Hud_TextWidth( upper, HUD_CAPS ) + h * 0.35f;
	}
	if ( bar >= 0.0f ) {
		w += 30.0f + h * 0.3f;
	}
	return Q_max( w, h );
}

float Hud_Chip( float x, float y, float h, qhandle_t icon, const char *text, const float *rgb, float alpha, int align, float bar ) {
	const float	w = Hud_ChipWidth( h, icon, text, bar );
	float		cx;

	if ( align == HUD_RIGHT ) {
		x -= w;
	}
	Hud_Glass( x, y, w, h, Q_min( 4.0f, h * 0.22f ), alpha, rgb, qfalse );
	cx = x;
	if ( icon ) {
		const float	inset = h * 0.17f;
		vec4_t		white;

		Hud_Color( white, colorWhite, alpha );
		trap->R_SetColor( white );
		CG_DrawPic( x + inset, y + inset, h - 2.0f * inset, h - 2.0f * inset, icon );
		trap->R_SetColor( NULL );
		cx += h;
	} else {
		cx += h * 0.35f;
	}
	if ( text && text[0] ) {
		cx += Hud_Caps( cx, y + h * 0.5f, rgb ? rgb : hudText, alpha, HUD_LEFT, text ) + h * 0.35f;
	}
	if ( bar >= 0.0f ) {
		Hud_Bar( cx, y + h * 0.5f - 1.0f, 30.0f, 2.0f, bar, rgb ? rgb : hudAccent, alpha, qfalse );
	}
	return w;
}

/*
=================
Hud_ItemRect

The rect of an item of a menu of the HUD (ui/vision/hud.menu), so what C draws stands where the menu file puts it.
qfalse: no such menu or item.
=================
*/
qboolean Hud_ItemRect( const char *menuName, const char *itemName, rectDef_t *out ) {
	menuDef_t	*menu = Menus_FindByName( menuName );
	itemDef_t	*item = menu ? Menu_FindItemByName( menu, itemName ) : NULL;

	if ( !item ) {
		return qfalse;
	}
	*out = item->window.rect;
	return qtrue;
}

// the left and right panel of the HUD, with what they are in the menu file of 2026-11 when it has no such item
// the panel of a side; qfalse: the HUD file has none there, out is where the panel of the old layout was (a
// vehicle draws there)
qboolean Hud_PanelRect( qboolean right, rectDef_t *out ) {
	if ( Hud_ItemRect( right ? "visionhud_right" : "visionhud_left", "panel", out ) ) {
		return qtrue;
	}
	out->x = right ? 452.0f : 10.0f;
	out->y = 426.0f;
	out->w = 178.0f;
	out->h = 44.0f;
	return qfalse;
}

// the colour of a team, NULL for none
const float *Hud_TeamColor( int team ) {
	return team == TEAM_RED ? hudRed : team == TEAM_BLUE ? hudBlue : NULL;
}

// the time as m:ss
const char *Hud_Clock( int msec ) {
	const int s = Q_max( 0, msec ) / 1000;

	return va( "%i:%02i", s / 60, s % 60 );
}
