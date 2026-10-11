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

// ui_skin.c -- the look of the menus and the HUD of this tree, see ui_skin.h and docs/japlus/ui.md.
//
// Everything here is drawn with pictures this file computes and hands to the renderer (DC->registerShaderRGBA,
// the call the chat bubbles of the cgame use). A shape with a round corner is made for the pixels its radius has
// in the window and is put on whole pixels, so its edge is as sharp at any size of the window. An engine without
// that call has no such look: Skin_Ready() is qfalse there and the callers draw what they always drew.
//
// Nothing here keeps time by itself. What moves (a mark that comes and goes, the stars behind the menus) goes
// by DC->realTime and DC->frameTime.

#ifdef _CGAME
	#include "cgame/cg_local.h"
#else
	#include "ui/ui_local.h"
#endif

#include "ui_shared.h"
#include "ui_skin.h"

extern displayContextDef_t *DC;

#define SKIN_MAX_SHAPES		24		// shapes of different radius that are kept. More than a menu set uses
#define SKIN_MAX_RADIUS		120		// pixels: a corner that is rounder is drawn with this radius
#define SKIN_SOFT_SIZE		64
#define SKIN_STARS			150

typedef enum {
	SKIN_FILL,						// a disc
	SKIN_RING						// its rim
} skinKind_t;

typedef struct skinShape_s {
	skinKind_t	kind;
	int			radius, thick;		// pixels
	int			size;				// of the picture, a power of two
	qhandle_t	shader;
} skinShape_t;

static struct {
	skinShape_t	shapes[SKIN_MAX_SHAPES];
	int			numShapes;
	qhandle_t	soft, softAdd;		// a round light that fades to its rim, blended and added
	qhandle_t	sky;				// the colours behind the menus, top to bottom
	qboolean	tried;
	qhandle_t	fonts[2];			// SKIN_FONT_SANS and SKIN_FONT_MONO
	qboolean	fontsTried;
} skin;

static byte skinPic[256 * 256 * 4];

// the renderer was started anew (or the module was): every handle is gone
void Skin_Reset( void ) {
	memset( &skin, 0, sizeof( skin ) );
}

qboolean Skin_Ready( void ) {
	return (qboolean)( DC && DC->registerShaderRGBA != NULL );
}

// The font of "font 5" or "font 6" of a menu file, 0: this engine does not have it and the caller takes a font
// of the game. An engine that makes the pictures also knows the two names.
qhandle_t Skin_FontHandle( int iMenuFont ) {
	if ( !Skin_Ready() || ( iMenuFont != SKIN_FONT_SANS && iMenuFont != SKIN_FONT_MONO ) ) {
		return 0;
	}
	if ( !skin.fontsTried ) {
		skin.fontsTried = qtrue;
		skin.fonts[0] = DC->RegisterFont( "vision_sans" );
		skin.fonts[1] = DC->RegisterFont( "vision_mono" );
	}
	return skin.fonts[iMenuFont - SKIN_FONT_SANS];
}

// pixels of the window for a unit of the 2D screen
static float Skin_ScaleX( void ) {
	return DC->glconfig.vidWidth / ( SCREEN_WIDTH + 2.0f * DC->bias );
}

static float Skin_ScaleY( void ) {
	return DC->glconfig.vidHeight / (float)SCREEN_HEIGHT;
}

// a place of the 2D screen on the pixel of the window it is nearest to, as units again
static float Skin_SnapX( float x ) {
	const float scale = Skin_ScaleX();

	return floorf( ( x + DC->bias ) * scale + 0.5f ) / scale - DC->bias;
}

static float Skin_SnapY( float y ) {
	const float scale = Skin_ScaleY();

	return floorf( y * scale + 0.5f ) / scale;
}

// toward 1 while on and toward 0 while not, all the way in msec
float Skin_Ease( float value, qboolean on, int msec ) {
	const float step = Com_Clamp( 0.0f, 1.0f, (float)DC->frameTime / (float)msec );

	return on ? Q_min( 1.0f, value + step ) : Q_max( 0.0f, value - step );
}

void Skin_Color( vec4_t out, const vec4_t in, float alphaScale ) {
	VectorCopy( in, out );
	out[3] = in[3] * alphaScale;
}

/*
=================
Skin_Shape

A disc or its rim for a radius in pixels, white with the shape in the alpha, in the middle of a picture. The
rim of a ring is `thick` pixels wide. A shape that was made before is found again.
=================
*/
static const skinShape_t *Skin_Shape( skinKind_t kind, int radius, int thick ) {
	skinShape_t	*shape;
	int			i, x, y, size;

	radius = Com_Clampi( 1, SKIN_MAX_RADIUS, radius );
	for ( i = 0; i < skin.numShapes; i++ ) {
		shape = &skin.shapes[i];
		if ( shape->kind == kind && shape->radius == radius && shape->thick == thick ) {
			return shape;
		}
	}
	if ( skin.numShapes == SKIN_MAX_SHAPES ) {
		return NULL;
	}
	for ( size = 8; size < 2 * radius; size *= 2 )
		;
	for ( y = 0; y < size; y++ ) {
		for ( x = 0; x < size; x++ ) {
			// from the middle of the picture to the middle of this pixel
			const float	dx = x + 0.5f - size * 0.5f, dy = y + 0.5f - size * 0.5f;
			const float	d = sqrtf( dx * dx + dy * dy );
			float		alpha = Com_Clamp( 0.0f, 1.0f, radius - d );
			byte		*out = &skinPic[( y * size + x ) * 4];

			if ( kind == SKIN_RING ) {
				alpha -= Com_Clamp( 0.0f, 1.0f, radius - thick - d );
			}
			out[0] = out[1] = out[2] = 255;
			out[3] = (byte)( alpha * 255.0f + 0.5f );
		}
	}
	shape = &skin.shapes[skin.numShapes];
	shape->kind = kind;
	shape->radius = radius;
	shape->thick = thick;
	shape->size = size;
	shape->shader = DC->registerShaderRGBA( va( "skin_%s_%i_%i", kind == SKIN_RING ? "ring" : "fill", radius, thick ),
		skinPic, size, size, RGBA_SHADER_OVER );
	if ( !shape->shader ) {
		return NULL;
	}
	skin.numShapes++;
	return shape;
}

static void Skin_Pictures( void ) {
	int x, y;

	if ( skin.tried ) {
		return;
	}
	skin.tried = qtrue;

	// a light that is full in the middle and gone at the rim
	for ( y = 0; y < SKIN_SOFT_SIZE; y++ ) {
		for ( x = 0; x < SKIN_SOFT_SIZE; x++ ) {
			const float	dx = x + 0.5f - SKIN_SOFT_SIZE * 0.5f, dy = y + 0.5f - SKIN_SOFT_SIZE * 0.5f;
			const float	a = Com_Clamp( 0.0f, 1.0f, 1.0f - sqrtf( dx * dx + dy * dy ) / ( SKIN_SOFT_SIZE * 0.5f - 1.0f ) );
			byte		*out = &skinPic[( y * SKIN_SOFT_SIZE + x ) * 4];

			out[0] = out[1] = out[2] = 255;
			out[3] = (byte)( a * a * ( 3.0f - 2.0f * a ) * 255.0f + 0.5f );
		}
	}
	skin.soft = DC->registerShaderRGBA( "skin_soft", skinPic, SKIN_SOFT_SIZE, SKIN_SOFT_SIZE, RGBA_SHADER_OVER );
	skin.softAdd = DC->registerShaderRGBA( "skin_soft_add", skinPic, SKIN_SOFT_SIZE, SKIN_SOFT_SIZE, RGBA_SHADER_OVER | RGBA_SHADER_GLOW );

	// the sky behind the menus: nearly black at the top, a deep blue at the bottom
	for ( y = 0; y < 64; y++ ) {
		const float t = y / 63.0f;

		for ( x = 0; x < 2; x++ ) {
			byte *out = &skinPic[( y * 2 + x ) * 4];

			out[0] = (byte)( 4.0f + 6.0f * t * t );
			out[1] = (byte)( 6.0f + 16.0f * t * t );
			out[2] = (byte)( 12.0f + 34.0f * t );
			out[3] = 255;
		}
	}
	skin.sky = DC->registerShaderRGBA( "skin_sky", skinPic, 2, 64, RGBA_SHADER_OVER );
}

/*
=================
Skin_NineSlice

A shape with round corners over a rectangle: the four quarters of the disc at the corners, the row and the
column through its middle along the sides, its middle over the rest. corner: the size of a corner in units.
=================
*/
static void Skin_NineSlice( float x, float y, float w, float h, float corner, const skinShape_t *shape, qboolean middle ) {
	const float	half = 0.5f, c = (float)shape->radius / shape->size;
	const float	s0 = half - c, s1 = half, s2 = half + c;
	const float	xs[4] = { x, x + corner, x + w - corner, x + w };
	const float	ys[4] = { y, y + corner, y + h - corner, y + h };
	const float	ss[4] = { s0, s1, s1, s2 };
	int			i, j;

	for ( j = 0; j < 3; j++ ) {
		for ( i = 0; i < 3; i++ ) {
			if ( ( i == 1 && j == 1 && !middle ) || xs[i + 1] <= xs[i] || ys[j + 1] <= ys[j] ) {
				continue;
			}
			DC->drawStretchPic( xs[i], ys[j], xs[i + 1] - xs[i], ys[j + 1] - ys[j], ss[i], ss[j], ss[i + 1], ss[j + 1], shape->shader );
		}
	}
}

/*
=================
Skin_Panel

A rectangle with round corners: filled, and with a rim of one pixel if edge is given and can be seen. Its
corners are whole pixels and the rectangle starts and ends on a pixel.
=================
*/
void Skin_Panel( float x, float y, float w, float h, float radius, const vec4_t fill, const vec4_t edge ) {
	const float			sx = Skin_ScaleX(), sy = Skin_ScaleY();
	const float			x0 = Skin_SnapX( x ), y0 = Skin_SnapY( y ), x1 = Skin_SnapX( x + w ), y1 = Skin_SnapY( y + h );
	const skinShape_t	*shape;
	int					pixels;
	float				corner;

	if ( !Skin_Ready() || x1 <= x0 || y1 <= y0 ) {
		return;
	}
	// no rounder than the rectangle is small
	pixels = (int)( Q_min( radius, Q_min( x1 - x0, y1 - y0 ) * 0.5f ) * sy + 0.5f );
	if ( pixels < 1 ) {
		if ( fill && fill[3] > 0.0f ) {
			DC->fillRect( x0, y0, x1 - x0, y1 - y0, fill );
		}
		if ( edge && edge[3] > 0.0f ) {
			DC->drawRect( x0, y0, x1 - x0, y1 - y0, 1.0f / sy, edge );
		}
		return;
	}
	corner = pixels / sy;
	// (a pixel is as wide as it is high wherever the 2D keeps its shape, which is where this look is used)
	(void)sx;
	if ( fill && fill[3] > 0.0f ) {
		shape = Skin_Shape( SKIN_FILL, pixels, 0 );
		if ( shape ) {
			DC->setColor( fill );
			Skin_NineSlice( x0, y0, x1 - x0, y1 - y0, corner, shape, qtrue );
		}
	}
	if ( edge && edge[3] > 0.0f ) {
		shape = Skin_Shape( SKIN_RING, pixels, Q_max( 1, (int)( sy * 0.7f ) ) );
		if ( shape ) {
			DC->setColor( edge );
			Skin_NineSlice( x0, y0, x1 - x0, y1 - y0, corner, shape, qfalse );
		}
	}
	DC->setColor( NULL );
}

/*
=================
Skin_Soft

A light without an edge around a rectangle: full inside it, gone `spread` units outside of it. Blended it is a
shadow, added it is a glow.
=================
*/
void Skin_Soft( float x, float y, float w, float h, float spread, const vec4_t color, qboolean add ) {
	const qhandle_t	shader = add ? skin.softAdd : skin.soft;
	const float		in = Q_min( spread, Q_min( w, h ) * 0.5f );
	const float		xs[4] = { x - spread, x + in, x + w - in, x + w + spread };
	const float		ys[4] = { y - spread, y + in, y + h - in, y + h + spread };
	const float		ss[4] = { 0.0f, 0.5f, 0.5f, 1.0f };
	int				i, j;

	if ( !Skin_Ready() ) {
		return;
	}
	Skin_Pictures();
	if ( !shader || color[3] <= 0.0f ) {
		return;
	}
	DC->setColor( color );
	for ( j = 0; j < 3; j++ ) {
		for ( i = 0; i < 3; i++ ) {
			if ( xs[i + 1] > xs[i] && ys[j + 1] > ys[j] ) {
				DC->drawStretchPic( xs[i], ys[j], xs[i + 1] - xs[i], ys[j + 1] - ys[j], ss[i], ss[j], ss[i + 1], ss[j + 1], shader );
			}
		}
	}
	DC->setColor( NULL );
}

void Skin_Disc( float cx, float cy, float radius, const vec4_t color ) {
	const int			pixels = Q_max( 1, (int)( radius * Skin_ScaleY() + 0.5f ) );
	const skinShape_t	*shape = Skin_Ready() ? Skin_Shape( SKIN_FILL, pixels, 0 ) : NULL;
	const float			r = pixels / Skin_ScaleY();
	float				c;

	if ( !shape ) {
		return;
	}
	c = (float)shape->radius / shape->size;
	DC->setColor( color );
	DC->drawStretchPic( Skin_SnapX( cx - r ), Skin_SnapY( cy - r ), 2.0f * r, 2.0f * r, 0.5f - c, 0.5f - c, 0.5f + c, 0.5f + c, shape->shader );
	DC->setColor( NULL );
}

/*
=================
Skin_Backdrop

What is behind the menus outside of a game: a dark sky, two wide lights that wander slowly, and stars that
drift past at three depths and flicker a little. Over all of a wide window.
=================
*/
void Skin_Backdrop( void ) {
	const float		left = -DC->wide, width = SCREEN_WIDTH + 2.0f * DC->wide;
	const float		t = DC->realTime * 0.001f;
	const vec4_t	blue = { 0.10f, 0.34f, 0.75f, 0.16f }, teal = { 0.05f, 0.55f, 0.62f, 0.10f };
	unsigned int	seed = 0x9e3779b9u;
	int				i;

	if ( !Skin_Ready() ) {
		return;
	}
	Skin_Pictures();
	if ( skin.sky ) {
		// (half a pixel of the picture in from its ends: its rim would be mixed with nothing)
		DC->setColor( NULL );
		DC->drawStretchPic( left, 0.0f, width, SCREEN_HEIGHT, 0.5f, 0.5f / 64.0f, 0.5f, 63.5f / 64.0f, skin.sky );
	}
	if ( !skin.soft ) {
		return;
	}
	DC->setColor( blue );
	DC->drawHandlePic( left + width * 0.55f + sinf( t * 0.05f ) * 40.0f - 300.0f, 150.0f + cosf( t * 0.04f ) * 20.0f, 600.0f, 420.0f, skin.soft );
	DC->setColor( teal );
	DC->drawHandlePic( left + width * 0.2f + cosf( t * 0.03f ) * 50.0f - 220.0f, -120.0f + sinf( t * 0.045f ) * 25.0f, 440.0f, 360.0f, skin.soft );

	for ( i = 0; i < SKIN_STARS; i++ ) {
		float	fx, fy, depth, size, twinkle;
		vec4_t	color;

		// the same stars every frame: a row of numbers that starts anew
		seed = seed * 1664525u + 1013904223u;
		fx = ( seed >> 8 ) * ( 1.0f / 16777216.0f );
		seed = seed * 1664525u + 1013904223u;
		fy = ( seed >> 8 ) * ( 1.0f / 16777216.0f );
		seed = seed * 1664525u + 1013904223u;
		depth = 0.25f + 0.75f * ( ( seed >> 8 ) * ( 1.0f / 16777216.0f ) );

		// the near ones are larger, brighter and faster
		fx = fmodf( fx + t * 0.004f * depth, 1.0f );
		size = 0.7f + 1.5f * depth * depth;
		twinkle = 0.75f + 0.25f * sinf( t * ( 0.6f + 1.7f * fy ) + fx * 40.0f + i );
		VectorSet( color, 0.75f + 0.25f * fy, 0.86f, 1.0f );
		color[3] = ( 0.18f + 0.6f * depth * depth ) * twinkle;
		DC->setColor( color );
		DC->drawHandlePic( left + ( 1.0f - fx ) * ( width + 8.0f ) - 4.0f, fy * SCREEN_HEIGHT, size, size, skin.soft );
	}
	DC->setColor( NULL );
}

/*
===============================================================================

The widgets of a menu with "skin 1"

The colours come from the menu file: the text of an item has its forecolor, what is switched on, filled or in
focus has the focusColor of its menu.

===============================================================================
*/

static const float *Skin_Accent( const itemDef_t *item ) {
	return ( (const menuDef_t *)item->parent )->focusColor;
}

static qboolean Skin_HasFocus( const itemDef_t *item ) {
	return (qboolean)( ( item->window.flags & WINDOW_HASFOCUS ) && !item->disabled );
}

// The mark behind an item the mouse is on or the keys are at: a light fill and a bar at its left. It comes and
// goes in SKIN_EASE_MSEC. Called once a frame for every item that can have the focus.
void Skin_ItemBack( itemDef_t *item ) {
	const float	*accent = Skin_Accent( item );
	vec4_t		color;

	item->skinFocus = Skin_Ease( item->skinFocus, Skin_HasFocus( item ), SKIN_EASE_MSEC );
	if ( item->skinFocus <= 0.0f || ( item->window.flags & WINDOW_DECORATION ) ) {
		return;
	}
	Skin_Color( color, accent, 0.13f * item->skinFocus );
	Skin_Panel( item->window.rect.x, item->window.rect.y, item->window.rect.w, item->window.rect.h, 3.0f, color, NULL );
	Skin_Color( color, accent, 0.9f * item->skinFocus );
	Skin_Panel( item->window.rect.x, item->window.rect.y + 2.0f, 1.5f, item->window.rect.h - 4.0f, 0.75f, color, NULL );
}

// the colour of the text of an item: its own, brighter the more it is in focus, and that of its menu when off
void Skin_TextColor( itemDef_t *item, vec4_t out ) {
	const menuDef_t	*parent = (const menuDef_t *)item->parent;
	const vec4_t	bright = { 1.0f, 1.0f, 1.0f, item->window.foreColor[3] };

	if ( item->disabled ) {
		VectorCopy4( parent->disableColor, out );
		return;
	}
	out[0] = item->window.foreColor[0] + ( bright[0] - item->window.foreColor[0] ) * item->skinFocus;
	out[1] = item->window.foreColor[1] + ( bright[1] - item->window.foreColor[1] ) * item->skinFocus;
	out[2] = item->window.foreColor[2] + ( bright[2] - item->window.foreColor[2] ) * item->skinFocus;
	out[3] = item->window.foreColor[3];
}

// A switch in place of the words yes and no: a track with a knob that slides to the right when it is on
void Skin_Switch( itemDef_t *item, float x, float y, float height, qboolean on ) {
	const float		*accent = Skin_Accent( item );
	const float		h = Q_min( height, 10.0f ), w = h * 2.0f, top = y + ( height - h ) * 0.5f;
	const vec4_t	off = { 0.30f, 0.37f, 0.46f, 0.9f }, knob = { 0.94f, 0.97f, 1.0f, 1.0f };
	vec4_t			track;
	float			at;

	item->skinValue = Skin_Ease( item->skinValue, on, SKIN_EASE_MSEC );
	at = item->skinValue;
	track[0] = off[0] + ( accent[0] - off[0] ) * at;
	track[1] = off[1] + ( accent[1] - off[1] ) * at;
	track[2] = off[2] + ( accent[2] - off[2] ) * at;
	track[3] = item->disabled ? 0.4f : 0.9f;
	Skin_Panel( x, top, w, h, h * 0.5f, track, NULL );
	Skin_Disc( x + h * 0.5f + ( w - h ) * at, top + h * 0.5f, h * 0.5f - 1.0f, knob );
}

// A slider: a thin track, filled up to the knob
void Skin_Slider( itemDef_t *item, float x, float y, float width, float height, float fraction ) {
	const float		*accent = Skin_Accent( item );
	const float		mid = y + height * 0.5f, knobR = 3.0f + 1.0f * item->skinFocus;
	const vec4_t	back = { 0.30f, 0.37f, 0.46f, 0.8f }, knob = { 0.94f, 0.97f, 1.0f, 1.0f };
	vec4_t			glow;

	fraction = Com_Clamp( 0.0f, 1.0f, fraction );
	Skin_Panel( x, mid - 1.0f, width, 2.0f, 1.0f, back, NULL );
	Skin_Panel( x, mid - 1.0f, width * fraction, 2.0f, 1.0f, accent, NULL );
	if ( item->skinFocus > 0.0f ) {
		Skin_Color( glow, accent, 0.35f * item->skinFocus );
		Skin_Soft( x + width * fraction - 1.0f, mid - 1.0f, 2.0f, 2.0f, 7.0f, glow, qtrue );
	}
	Skin_Disc( x + width * fraction, mid, knobR, knob );
}

// The box of a line that can be typed in, or of the key a command is bound to
void Skin_Field( itemDef_t *item, float x, float y, float width, float height, qboolean editing ) {
	const float		*accent = Skin_Accent( item );
	const vec4_t	fill = { 0.0f, 0.0f, 0.0f, 0.35f };
	vec4_t			edge;

	Skin_Color( edge, accent, editing ? 0.95f : 0.22f + 0.4f * item->skinFocus );
	Skin_Panel( x, y, width, height, 2.5f, fill, edge );
}

// A bar that is filled to a part of its length, for the HUD: health, shield, force
void Skin_Bar( float x, float y, float w, float h, float fraction, const vec4_t fill, const vec4_t back ) {
	const float radius = h * 0.5f;

	fraction = Com_Clamp( 0.0f, 1.0f, fraction );
	if ( back && back[3] > 0.0f ) {
		Skin_Panel( x, y, w, h, radius, back, NULL );
	}
	if ( fraction > 0.0f && fill[3] > 0.0f ) {
		Skin_Panel( x, y, Q_max( h, w * fraction ), h, radius, fill, NULL );
	}
}

// The bar at the side of a list: a faint track, and a thumb that is `part` of it long and starts at `start` (0 to 1)
void Skin_ScrollBar( float x, float y, float w, float h, float start, float part, qboolean horizontal, const vec4_t accent ) {
	const vec4_t	track = { 1.0f, 1.0f, 1.0f, 0.06f };
	vec4_t			thumb;

	Skin_Color( thumb, accent, 0.65f );
	part = Com_Clamp( 0.08f, 1.0f, part );
	start = Com_Clamp( 0.0f, 1.0f - part, start );
	if ( horizontal ) {
		Skin_Panel( x, y + h * 0.5f - 1.5f, w, 3.0f, 1.5f, track, NULL );
		Skin_Panel( x + w * start, y + h * 0.5f - 1.5f, w * part, 3.0f, 1.5f, thumb, NULL );
	} else {
		Skin_Panel( x + w * 0.5f - 1.5f, y, 3.0f, h, 1.5f, track, NULL );
		Skin_Panel( x + w * 0.5f - 1.5f, y + h * start, 3.0f, h * part, 1.5f, thumb, NULL );
	}
}

// A row of a list: the chosen one is filled and has a bar at its left, the one under the mouse is lit
void Skin_Row( float x, float y, float w, float h, const vec4_t accent, qboolean selected, float hover ) {
	vec4_t color;

	if ( selected ) {
		Skin_Color( color, accent, 0.22f );
		Skin_Panel( x, y, w, h, 2.0f, color, NULL );
		Skin_Color( color, accent, 0.9f );
		Skin_Panel( x, y + 1.0f, 1.5f, h - 2.0f, 0.75f, color, NULL );
	} else if ( hover > 0.0f ) {
		Skin_Color( color, accent, 0.09f * hover );
		Skin_Panel( x, y, w, h, 2.0f, color, NULL );
	}
}
