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
#define SKIN_ARROW_SIZE		32

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
	qhandle_t	soft;				// a round light that fades to its rim
	qhandle_t	sky;				// the colours behind the menus, top to bottom
	qboolean	tried;
	qhandle_t	fonts[2];			// SKIN_FONT_SANS and SKIN_FONT_MONO
	qboolean	fontsTried;
	qhandle_t	pointer;			// the arrow of the mouse
	qboolean	pointerTried;
	qhandle_t	logo;				// the icon of the game, flat
	qhandle_t	solid;				// a white picture for polygons in a scene
	qboolean	logoTried;
	qhandle_t	arrow;				// a triangle that points to the right
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
	// (Blended, never added to what is behind it: the only way to a picture that is added is RGBA_SHADER_GLOW,
	// and what that marks also goes through the dynamic glow of the renderer (r_DynamicGlow), which smeared a
	// soft light in the 2D into a row of blots beside it.)
	skin.soft = DC->registerShaderRGBA( "skin_soft", skinPic, SKIN_SOFT_SIZE, SKIN_SOFT_SIZE, RGBA_SHADER_OVER );

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

	// a triangle that points to the right: its tip in the middle of the right edge, its base the left edge
	for ( y = 0; y < SKIN_ARROW_SIZE; y++ ) {
		for ( x = 0; x < SKIN_ARROW_SIZE; x++ ) {
			// how far this pixel is inside of the two slanted edges, in pixels along their normal
			const float	px = x + 0.5f, py = fabsf( y + 0.5f - SKIN_ARROW_SIZE * 0.5f );
			const float	inside = ( ( SKIN_ARROW_SIZE - 1.0f - px ) * 0.5f - py ) * 0.894f;
			byte		*out = &skinPic[( y * SKIN_ARROW_SIZE + x ) * 4];

			out[0] = out[1] = out[2] = 255;
			out[3] = (byte)( Com_Clamp( 0.0f, 1.0f, Q_min( inside + 0.5f, px ) ) * 255.0f + 0.5f );
		}
	}
	skin.arrow = DC->registerShaderRGBA( "skin_arrow", skinPic, SKIN_ARROW_SIZE, SKIN_ARROW_SIZE, RGBA_SHADER_OVER );
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

A light without an edge around a rectangle: full inside it, gone `spread` units outside of it. A dark one is a
shadow, a bright one a glow. Blended in both cases.
=================
*/
void Skin_Soft( float x, float y, float w, float h, float spread, const vec4_t color ) {
	const float		in = Q_min( spread, Q_min( w, h ) * 0.5f );
	const float		xs[4] = { x - spread, x + in, x + w - in, x + w + spread };
	const float		ys[4] = { y - spread, y + in, y + h - in, y + h + spread };
	const float		ss[4] = { 0.0f, 0.5f, 0.5f, 1.0f };
	int				i, j;

	if ( !Skin_Ready() ) {
		return;
	}
	Skin_Pictures();
	if ( !skin.soft || color[3] <= 0.0f ) {
		return;
	}
	DC->setColor( color );
	for ( j = 0; j < 3; j++ ) {
		for ( i = 0; i < 3; i++ ) {
			if ( xs[i + 1] > xs[i] && ys[j + 1] > ys[j] ) {
				DC->drawStretchPic( xs[i], ys[j], xs[i + 1] - xs[i], ys[j + 1] - ys[j], ss[i], ss[j], ss[i + 1], ss[j + 1], skin.soft );
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

// A line from left to right that is one pixel high, whatever the size of the window. While the menus stand on
// a plane in the level (cl_uiPlane) a pixel of the 2D is less than one of the screen, and a line that thin
// breaks into dashes: there it is a strip with soft edges, which the renderer can thin out evenly.
void Skin_Line( float x, float y, float w, const vec4_t color ) {
	static int		askedTime = -1;
	static qboolean	plane;

	if ( !Skin_Ready() || color[3] <= 0.0f ) {
		return;
	}
	if ( askedTime != DC->realTime ) {
		askedTime = DC->realTime;
		plane = (qboolean)( DC->getCVarValue( "cl_uiPlane" ) != 0.0f );
	}
	if ( !plane ) {
		DC->fillRect( Skin_SnapX( x ), Skin_SnapY( y ), w, 1.0f / Skin_ScaleY(), color );
		return;
	}
	Skin_Pictures();
	if ( skin.soft ) {
		vec4_t strong;

		// (the column through the middle of the round light: nothing, all, nothing)
		Skin_Color( strong, color, 1.6f );
		DC->setColor( strong );
		DC->drawStretchPic( x, y - 1.0f, w, 2.6f, 0.5f, 0.0f, 0.5f, 1.0f, skin.soft );
		DC->setColor( NULL );
	}
}

// A triangle in a rectangle, pointing to its right edge or to its left
void Skin_Arrow( float x, float y, float w, float h, qboolean left, const vec4_t color ) {
	if ( !Skin_Ready() ) {
		return;
	}
	Skin_Pictures();
	if ( !skin.arrow || color[3] <= 0.0f ) {
		return;
	}
	DC->setColor( color );
	DC->drawStretchPic( Skin_SnapX( x ), Skin_SnapY( y ), w, h, left ? 1.0f : 0.0f, 0.0f, left ? 0.0f : 1.0f, 1.0f, skin.arrow );
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

// Is a point inside the arrow of the mouse? Its outline in a square of 32, the tip at the top left.
static qboolean Skin_InArrow( float x, float y ) {
	static const float	arrow[7][2] = { { 3, 2 }, { 3, 24 }, { 9, 18.5f }, { 13.5f, 28.5f }, { 17.5f, 26.8f }, { 13, 17 }, { 21, 17 } };
	qboolean			inside = qfalse;
	int					i, j;

	for ( i = 0, j = 6; i < 7; j = i++ ) {
		if ( ( arrow[i][1] > y ) != ( arrow[j][1] > y )
			&& x < ( arrow[j][0] - arrow[i][0] ) * ( y - arrow[i][1] ) / ( arrow[j][1] - arrow[i][1] ) + arrow[i][0] ) {
			inside = !inside;
		}
	}
	return inside;
}

/*
=================
Skin_Cursor

The pointer of the mouse in the new look: a light arrow with a dark rim, its tip at x and y
=================
*/
void Skin_Cursor( float x, float y ) {
	if ( !Skin_Ready() ) {
		return;
	}
	if ( !skin.pointerTried ) {
		int px, py, sx, sy, k;

		skin.pointerTried = qtrue;
		for ( py = 0; py < 32; py++ ) {
			for ( px = 0; px < 32; px++ ) {
				// how much of the pixel the arrow covers, and how much the arrow made 1.3 pixels fatter does
				static const float	ring[8][2] = { { 1.3f, 0 }, { -1.3f, 0 }, { 0, 1.3f }, { 0, -1.3f }, { 0.9f, 0.9f }, { -0.9f, 0.9f }, { 0.9f, -0.9f }, { -0.9f, -0.9f } };
				int					fill = 0, rim = 0;
				byte				*out = &skinPic[( py * 32 + px ) * 4];

				for ( sy = 0; sy < 4; sy++ ) {
					for ( sx = 0; sx < 4; sx++ ) {
						const float	fx = px + ( sx + 0.5f ) * 0.25f, fy = py + ( sy + 0.5f ) * 0.25f;
						qboolean	near = Skin_InArrow( fx, fy );

						fill += near;
						for ( k = 0; k < 8 && !near; k++ ) {
							near = Skin_InArrow( fx + ring[k][0], fy + ring[k][1] );
						}
						rim += near;
					}
				}
				out[0] = (byte)( 16 + fill * 232 / 16 );
				out[1] = (byte)( 26 + fill * 224 / 16 );
				out[2] = (byte)( 40 + fill * 215 / 16 );
				out[3] = (byte)( rim * 255 / 16 );
			}
		}
		skin.pointer = DC->registerShaderRGBA( "skin_pointer", skinPic, 32, 32, RGBA_SHADER_OVER );
	}
	if ( skin.pointer ) {
		DC->setColor( NULL );
		DC->drawHandlePic( x - 2.0f, y - 1.5f, 21.0f, 21.0f, skin.pointer );
	}
}

/*
===============================================================================

The icon of the game: a box seen from above, its lid white, its sides yellow and blue. The same three shapes
the website has (img/logo.svg), flat as a picture, and as a body in a scene of its own that turns.

===============================================================================
*/

#define LOGO_SIZE		128
// the corners of the three faces in the drawing of the icon, which is 209 wide and 216 high from 100, 96
static const float	logoFaces[3][4][2] = {
	{ { 183, 102 }, { 279, 149 }, { 204, 209 }, { 107, 163 } },
	{ { 107, 163 }, { 204, 209 }, { 204, 306 }, { 107, 261 } },
	{ { 204, 209 }, { 302, 163 }, { 302, 260 }, { 204, 306 } },
};
static const byte	logoColors[3][3] = { { 255, 255, 255 }, { 227, 215, 92 }, { 0, 168, 255 } };

static qboolean Skin_InQuad( const float quad[4][2], float x, float y ) {
	qboolean	inside = qfalse;
	int			i, j;

	for ( i = 0, j = 3; i < 4; j = i++ ) {
		if ( ( quad[i][1] > y ) != ( quad[j][1] > y )
			&& x < ( quad[j][0] - quad[i][0] ) * ( y - quad[i][1] ) / ( quad[j][1] - quad[i][1] ) + quad[i][0] ) {
			inside = !inside;
		}
	}
	return inside;
}

static void Skin_LogoPictures( void ) {
	int px, py, sx, sy, f;

	if ( skin.logoTried ) {
		return;
	}
	skin.logoTried = qtrue;
	for ( py = 0; py < LOGO_SIZE; py++ ) {
		for ( px = 0; px < LOGO_SIZE; px++ ) {
			int		sum[4] = { 0, 0, 0, 0 };
			byte	*out = &skinPic[( py * LOGO_SIZE + px ) * 4];

			// sixteen places in the pixel, each in the drawing of the icon, which stands in the middle of the picture
			for ( sy = 0; sy < 4; sy++ ) {
				for ( sx = 0; sx < 4; sx++ ) {
					const float	x = 96.5f + ( px + ( sx + 0.5f ) * 0.25f ) * ( 216.0f / LOGO_SIZE );
					const float	y = 96.0f + ( py + ( sy + 0.5f ) * 0.25f ) * ( 216.0f / LOGO_SIZE );

					for ( f = 0; f < 3; f++ ) {
						if ( Skin_InQuad( logoFaces[f], x, y ) ) {
							sum[0] += logoColors[f][0];
							sum[1] += logoColors[f][1];
							sum[2] += logoColors[f][2];
							sum[3]++;
							break;
						}
					}
				}
			}
			out[0] = out[1] = out[2] = 255;
			if ( sum[3] ) {
				out[0] = (byte)( sum[0] / sum[3] );
				out[1] = (byte)( sum[1] / sum[3] );
				out[2] = (byte)( sum[2] / sum[3] );
			}
			out[3] = (byte)( sum[3] * 255 / 16 );
		}
	}
	skin.logo = DC->registerShaderRGBA( "skin_logo", skinPic, LOGO_SIZE, LOGO_SIZE, RGBA_SHADER_OVER );
	memset( skinPic, 255, 8 * 8 * 4 );
	skin.solid = DC->registerShaderRGBA( "skin_solid", skinPic, 8, 8, 0 );
}

// the icon as a picture, as large as fits into the rectangle and in its middle
void Skin_Logo( float x, float y, float w, float h, float alpha ) {
	const float		size = Q_min( w, h );
	const vec4_t	color = { 1.0f, 1.0f, 1.0f, alpha };

	if ( !Skin_Ready() ) {
		return;
	}
	Skin_LogoPictures();
	if ( skin.logo ) {
		DC->setColor( color );
		DC->drawHandlePic( x + ( w - size ) * 0.5f, y + ( h - size ) * 0.5f, size, size, skin.logo );
		DC->setColor( NULL );
	}
}

/*
=================
Skin_Cube

The box of the icon as a body, in the pose the icon has: seen from above over one edge, the yellow side at the
left, the blue one at the right, the white lid hinged on the yellow side and a little open, the dark of its
inside in the gap. It is alive without leaving that pose: it sways a few degrees and the lid opens and closes
slowly. A scene of its own in the rectangle, seen from far away through a narrow lens, so its edges
run side by side as in the drawing. Where the engine gives no way to add a polygon to such a scene the flat
icon stands there.
=================
*/
#define CUBE_HALF		20.0f		// half the width of the box, in units of its scene
#define CUBE_HEIGHT		0.8f		//	its height against its width: the box of the icon is a little flat
#define CUBE_LID		0.9f		//	the lid against the top
#define CUBE_FOV		9.0f		// the lens,
#define CUBE_DISTANCE	520.0f		//	and how far the eye is: what is seen is about 80 units high
#define CUBE_PITCH		28.0f		// how far above the box the eye is, as in the drawing
#define CUBE_YAW		135.0f		// the turn in which the yellow side is at the left

// a corner of the box, turned and set down in front of the eye
static void Skin_CubePoint( float px, float py, float pz, float cy, float sy, float cp, float sp, vec3_t out ) {
	const float x = px * cy - py * sy, y = px * sy + py * cy;

	out[0] = x * cp - pz * sp + CUBE_DISTANCE;
	out[1] = y;
	out[2] = x * sp + pz * cp;
}

static void Skin_CubeVert( polyVert_t *v, const vec3_t at, const byte *rgb, float shade ) {
	VectorCopy( at, v->xyz );
	v->modulate[0] = (byte)( rgb[0] * shade );
	v->modulate[1] = (byte)( rgb[1] * shade );
	v->modulate[2] = (byte)( rgb[2] * shade );
	v->modulate[3] = 255;
}

void Skin_Cube( float x, float y, float w, float h ) {
	// the four sides: where they stand, and their two colours by turns
	static const float	sideAt[4][2][2] = { { { 1, -1 }, { 1, 1 } }, { { 1, 1 }, { -1, 1 } }, { { -1, 1 }, { -1, -1 } }, { { -1, -1 }, { 1, -1 } } };
	static const float	sideNormal[4][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } };
	static const byte	inside[3] = { 26, 60, 104 };
	const float			t = DC->realTime * 0.001f;
	const float			yaw = DEG2RAD( CUBE_YAW + 9.0f * sinf( t * 0.55f ) ), pitch = DEG2RAD( CUBE_PITCH );
	const float			cy = cosf( yaw ), sy = sinf( yaw ), cp = cosf( pitch ), sp = sinf( pitch );
	const float			open = 0.5f + 0.5f * sinf( t * 0.8f );					// 0 nearly shut, 1 as open as it gets
	const float			lid = DEG2RAD( 7.0f + 17.0f * open );
	const float			top = CUBE_HALF * CUBE_HEIGHT, bob = sinf( t * 0.7f ) * 0.8f;
	const float			scaleX = Skin_ScaleX(), scaleY = Skin_ScaleY();
	const float			size = Q_min( w, h ), cx = x + w * 0.5f, cyy = y + h * 0.5f;
	vec4_t				glow;
	refdef_t			refdef;
	polyVert_t			verts[4];
	vec3_t				at;
	int					f;

	if ( !Skin_Ready() ) {
		return;
	}
	Skin_LogoPictures();
	if ( !skin.solid || !DC->addPolyToScene || !DC->clearScene || !DC->renderScene ) {
		Skin_Logo( x, y, w, h, 1.0f );
		return;
	}

	// a light behind the box, brighter while the lid is open, and its shadow under it
	VectorSet( glow, 0.10f, 0.55f, 1.0f );
	glow[3] = 0.10f + 0.10f * open;
	Skin_Soft( cx - size * 0.16f, cyy - size * 0.20f, size * 0.32f, size * 0.32f, size * 0.34f, glow );
	VectorSet( glow, 0.0f, 0.0f, 0.0f );
	glow[3] = 0.40f;
	Skin_Soft( cx - size * 0.20f, cyy + size * 0.36f, size * 0.40f, size * 0.01f, size * 0.06f, glow );

	memset( &refdef, 0, sizeof( refdef ) );
	refdef.rdflags = RDF_NOWORLDMODEL;
	AxisClear( refdef.viewaxis );
	// a square in the middle of the rectangle, on whole pixels
	refdef.width = refdef.height = (int)( size * scaleY );
	refdef.x = (int)( ( cx + DC->bias ) * scaleX ) - refdef.width / 2;
	refdef.y = (int)( cyy * scaleY ) - refdef.height / 2;
	if ( refdef.width < 8 ) {
		return;
	}
	refdef.fov_x = refdef.fov_y = CUBE_FOV;
	refdef.time = DC->realTime;

	// The polygons of a scene are drawn in the order they come in and do not hide each other by their depth.
	// So: the sides that look at the eye (of a box no two of those overlap), its opening, the lid.
	DC->clearScene();
	memset( verts, 0, sizeof( verts ) );
	for ( f = 0; f < 4; f++ ) {
		const float	nx = sideNormal[f][0] * cy - sideNormal[f][1] * sy;
		const byte	*rgb = logoColors[1 + ( f & 1 )];

		// (the eye is in front of the box and above it: a side looks at it when it looks back along the view)
		if ( nx * cp >= 0.0f ) {
			continue;
		}
		Skin_CubePoint( sideAt[f][0][0] * CUBE_HALF, sideAt[f][0][1] * CUBE_HALF, -top + bob, cy, sy, cp, sp, at );
		Skin_CubeVert( &verts[0], at, rgb, 0.86f );
		Skin_CubePoint( sideAt[f][1][0] * CUBE_HALF, sideAt[f][1][1] * CUBE_HALF, -top + bob, cy, sy, cp, sp, at );
		Skin_CubeVert( &verts[1], at, rgb, 0.86f );
		Skin_CubePoint( sideAt[f][1][0] * CUBE_HALF, sideAt[f][1][1] * CUBE_HALF, top + bob, cy, sy, cp, sp, at );
		Skin_CubeVert( &verts[2], at, rgb, 1.0f );
		Skin_CubePoint( sideAt[f][0][0] * CUBE_HALF, sideAt[f][0][1] * CUBE_HALF, top + bob, cy, sy, cp, sp, at );
		Skin_CubeVert( &verts[3], at, rgb, 1.0f );
		DC->addPolyToScene( skin.solid, 4, verts );
	}
	// the opening: the dark inside the box, darker still toward the side the lid is hinged on
	for ( f = 0; f < 4; f++ ) {
		Skin_CubePoint( sideAt[f][0][0] * CUBE_HALF, sideAt[f][0][1] * CUBE_HALF, top + bob, cy, sy, cp, sp, at );
		Skin_CubeVert( &verts[f], at, inside, ( sideAt[f][0][0] > 0.0f ? 0.50f : 0.85f ) + 0.15f * open );
	}
	DC->addPolyToScene( skin.solid, 4, verts );
	// the lid: hinged on the top edge of the first side, the yellow one of the pose, and swung up from there
	for ( f = 0; f < 4; f++ ) {
		const float	away = ( f == 1 || f == 2 ) ? 2.0f * CUBE_HALF * CUBE_LID : 0.0f;		// how far this corner is from the hinge
		const float	along = ( f < 2 ) ? CUBE_HALF : -CUBE_HALF;

		Skin_CubePoint( CUBE_HALF - away * cosf( lid ), along, top + bob + away * sinf( lid ), cy, sy, cp, sp, at );
		Skin_CubeVert( &verts[f], at, logoColors[0], ( away > 0.0f ) ? 1.0f : 0.93f );
	}
	DC->addPolyToScene( skin.solid, 4, verts );
	DC->renderScene( &refdef );
}

/*
=================
Skin_LoadPanel

The panel at the bottom of the screens that wait for a level, the one of the ui while it connects and the one of
the cgame while it loads, so the second follows the first without a jump: the box of the icon, a title, a line
about it, a line that says what happens now, and a bar. fraction is how far it is. Below 0 nobody knows: then a
piece of the bar runs to and fro.
=================
*/
#define LOAD_TEXT_X		130.0f
#define LOAD_TEXT_W		470.0f

// a line of the panel: one that is too long (the name of a server, what a server says) is set smaller and stays in it
static void Skin_LoadLine( float y, float scale, const vec4_t color, const char *text ) {
	const float	width = DC->textWidth( text, scale, SKIN_FONT_SANS );
	const float	fit = ( width > LOAD_TEXT_W ) ? LOAD_TEXT_W / width : 1.0f;

	DC->drawText( LOAD_TEXT_X, y + ( 1.0f - fit ) * scale * 9.0f, scale * fit, (float *)color, text, 0, 0, 0, SKIN_FONT_SANS );
}

void Skin_LoadPanel( const char *title, const char *about, const char *now, float fraction ) {
	static const vec4_t	fill = { 0.035f, 0.07f, 0.12f, 0.80f }, edge = { 0.43f, 0.83f, 1.0f, 0.28f }, dark = { 0.0f, 0.0f, 0.0f, 0.45f };
	static const vec4_t	bar = { 0.43f, 0.83f, 1.0f, 1.0f }, track = { 1.0f, 1.0f, 1.0f, 0.10f };
	static const vec4_t	bright = { 1.0f, 1.0f, 1.0f, 1.0f }, text = { 0.86f, 0.92f, 0.97f, 1.0f }, dim = { 0.60f, 0.69f, 0.78f, 1.0f };

	if ( !Skin_Ready() ) {
		return;
	}
	Skin_Soft( 24.0f, 362.0f, 592.0f, 96.0f, 14.0f, dark );
	Skin_Panel( 24.0f, 358.0f, 592.0f, 96.0f, 9.0f, fill, edge );
	Skin_Cube( 30.0f, 360.0f, 92.0f, 92.0f );

	Skin_LoadLine( 366.0f, 1.15f, bright, title );
	Skin_LoadLine( 392.0f, 0.62f, dim, about );
	Skin_LoadLine( 410.0f, 0.62f, text, now );
	if ( fraction >= 0.0f ) {
		Skin_Bar( LOAD_TEXT_X, 432.0f, LOAD_TEXT_W, 5.0f, fraction, bar, track );
	} else {
		const float	piece = LOAD_TEXT_W * 0.2f, at = 0.5f - 0.5f * cosf( DC->realTime * 0.0025f );

		Skin_Bar( LOAD_TEXT_X, 432.0f, LOAD_TEXT_W, 5.0f, 0.0f, bar, track );
		Skin_Bar( LOAD_TEXT_X + at * ( LOAD_TEXT_W - piece ), 432.0f, piece, 5.0f, 1.0f, bar, NULL );
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
		Skin_Soft( x + width * fraction - 1.0f, mid - 1.0f, 2.0f, 2.0f, 7.0f, glow );
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
