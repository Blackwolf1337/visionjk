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

// tr_font_ttf.cpp -- glyphs from the TrueType fonts of this tree.
//
// The engine hands out three font files (ri.FontFile, client/cl_font.cpp). The outline of a character is made
// into a small picture, and the pictures are collected on pages, textures of FONT_PAGE_SIZE. A page that got
// new glyphs is sent to the graphics card once, by R_FontTTF_Flush(), before the frame that uses them is drawn.
//
// TTF_TEXT, for the characters the fonts of the game do not have. A font of the game (CFontInfo in tr_font.cpp)
// is a picture with the 256 characters of Windows-1252. Where text is UTF-8, a character outside of them comes
// from here (R_FontTTF_Glyph), made for the point size of the font of the game it stands in, so it sits on the
// same line and is about as tall. The fonts of the game are pictures made for a screen of 640 x 480; these
// glyphs are made with as many pixels a unit of that screen as the window has (ttf.oversample), so they are
// sharp where the letters of the game are not.
//
// TTF_MONO, with a fixed width, for the console of the engine (RE_Font_DrawMonoChar): a character in a cell
// that is given in pixels of the window, made in exactly that size.
//
// TTF_CJK, Noto Sans CJK, for Chinese, Japanese and Korean, which the other two do not have: every lookup of a
// character goes on to it (R_FontTTF_FindAny). No font of its own for the menus.
//
// TTF_BOLD, Noto Sans Bold, is only a font of the menus ("vision_sans_bold"): the bold words of the changelog.
//
// The first two are also fonts of the menus as a whole, under the names "vision_sans" and "vision_mono" (RE_RegisterFont
// in tr_font.cpp, R_FontTTF_DrawString here): every letter of a text from the TrueType font, made in the size
// the text has in pixels of the window, so it is sharp at every scale. The menus with the new look are set in
// them (ui/ui_skin.c).

#include "tr_local.h"
#include "tr_font_ttf.h"

#include <map>

// stb_truetype allocates while it fills an outline: from the zone of the engine, not from the C library
#define STBTT_malloc( x, u )	( (void)(u), ri.Z_Malloc( (int)(x), TAG_TEMP_WORKSPACE, qfalse, 4 ) )
#define STBTT_free( x, u )		( (void)(u), ri.Z_Free( x ) )
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#define FONT_PAGE_SIZE		1024
#define FONT_MAX_PAGES		8
#define FONT_MAX_GLYPH		256			// pixels a side: an outline that needs more is not made
#define FONT_MAX_OVERSAMPLE	4

typedef struct ttfPage_s {
	image_t		*image;
	qhandle_t	shader;
	qhandle_t	shaderScene;			// for glyphs in the scene: hidden by what is in front of them
	byte		*rgba;					// FONT_PAGE_SIZE x FONT_PAGE_SIZE, white with the glyphs in the alpha
	int			x, y;					// where the next glyph goes
	int			rowHeight;				// of the row that is being filled
	qboolean	dirty;					// has glyphs the graphics card does not have yet
} ttfPage_t;

typedef struct ttfGlyph_s {
	glyphInfo_t	info;
	qhandle_t	shader;
	qhandle_t	shaderScene;
	qboolean	valid;					// qfalse: the font has no such character, or no page had room
	float		ink[4];					// the outline: left, top, right, bottom in units from the pen on the baseline
} ttfGlyph_t;

typedef std::map<uint64_t, ttfGlyph_t> ttfGlyphMap_t;

// the fonts, in the order the engine has them (cl_fonts in client/cl_font.cpp)
enum {
	TTF_TEXT,							// for the characters the fonts of the game do not have
	TTF_MONO,							// fixed width, for the console
	TTF_CJK,							// Chinese, Japanese and Korean, after the other two
	TTF_BOLD,							// Noto Sans Bold, a font of the menus only ("vision_sans_bold")
	TTF_NUM_FONTS
};

typedef struct ttfFont_s {
	qboolean		loaded;
	stbtt_fontinfo	info;
	int				ascent, descent;	// of the font, in its own units
	float			advance;			// of a letter, as a part of the size of the font (TTF_MONO: of every letter)
} ttfFont_t;

static struct {
	ttfFont_t		fonts[TTF_NUM_FONTS];
	int				oversample;			// pixels of a page for one unit of the 640 x 480 screen
	ttfPage_t		pages[FONT_MAX_PAGES];
	int				numPages;
	ttfGlyphMap_t	*glyphs;			// by font, size and code point
} ttf;

/*
===============
R_FontTTF_Init
===============
*/
void R_FontTTF_Init( void ) {
	memset( &ttf, 0, sizeof( ttf ) );
	ttf.oversample = Com_Clampi( 1, FONT_MAX_OVERSAMPLE, ( glConfig.vidHeight + SCREEN_HEIGHT - 1 ) / SCREEN_HEIGHT );
	ttf.glyphs = new ttfGlyphMap_t;

	for ( int i = 0; i < TTF_NUM_FONTS; i++ ) {
		ttfFont_t	*font = &ttf.fonts[i];
		int			length = 0, advance, bearing, lineGap;
		const byte	*data = ri.FontFile ? ri.FontFile( i, &length ) : NULL;

		if ( !data || length <= 0 ) {
			continue;
		}
		if ( !stbtt_InitFont( &font->info, data, stbtt_GetFontOffsetForIndex( data, 0 ) ) ) {
			ri.Printf( PRINT_WARNING, "R_FontTTF_Init: font file %i can not be read\n", i );
			continue;
		}
		stbtt_GetFontVMetrics( &font->info, &font->ascent, &font->descent, &lineGap );
		stbtt_GetCodepointHMetrics( &font->info, 'M', &advance, &bearing );
		font->advance = advance * stbtt_ScaleForMappingEmToPixels( &font->info, 1.0f );
		font->loaded = qtrue;
	}
}

/*
===============
R_FontTTF_Shutdown
===============
*/
void R_FontTTF_Shutdown( void ) {
	for ( int i = 0; i < ttf.numPages; i++ ) {
		if ( ttf.pages[i].rgba ) {
			ri.Z_Free( ttf.pages[i].rgba );
		}
	}
	delete ttf.glyphs;
	memset( &ttf, 0, sizeof( ttf ) );
}

/*
===============
R_FontTTF_Flush

Sends the pages that got new glyphs to the graphics card. The quads of a frame are drawn after its last string
was laid out, so one call at the end of a string is early enough.
===============
*/
void R_FontTTF_Flush( void ) {
	for ( int i = 0; i < ttf.numPages; i++ ) {
		ttfPage_t *page = &ttf.pages[i];

		if ( !page->dirty ) {
			continue;
		}
		page->dirty = qfalse;
#ifdef RD_VULKAN_VISION
		VK_UpdateImage( page->image, FONT_PAGE_SIZE, FONT_PAGE_SIZE, page->rgba );
#else
		GL_Bind( page->image );
		qglTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, FONT_PAGE_SIZE, FONT_PAGE_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, page->rgba );
#endif
	}
}

// A page with room for a glyph of width x height pixels, a pixel of nothing around it. The place is in
// page->x and page->y. NULL when all pages are full.
static ttfPage_t *R_FontTTF_Room( int width, int height ) {
	ttfPage_t *page = ttf.numPages ? &ttf.pages[ttf.numPages - 1] : NULL;

	if ( page && page->x + width + 1 > FONT_PAGE_SIZE ) {
		// next row
		page->x = 1;
		page->y += page->rowHeight + 1;
		page->rowHeight = 0;
	}
	if ( !page || page->y + height + 1 > FONT_PAGE_SIZE ) {
		char name[MAX_QPATH];

		if ( ttf.numPages == FONT_MAX_PAGES ) {
			return NULL;
		}
		page = &ttf.pages[ttf.numPages];
		memset( page, 0, sizeof( *page ) );
		page->rgba = (byte *)ri.Z_Malloc( FONT_PAGE_SIZE * FONT_PAGE_SIZE * 4, TAG_GENERAL, qfalse, 4 );
		// white with nothing in the alpha, so a glyph is only its alpha and takes the colour of the text
		for ( int i = 0; i < FONT_PAGE_SIZE * FONT_PAGE_SIZE; i++ ) {
			page->rgba[i * 4 + 0] = page->rgba[i * 4 + 1] = page->rgba[i * 4 + 2] = 255;
			page->rgba[i * 4 + 3] = 0;
		}
		// The star keeps the image over the change of a level (R_Images_Clear and its like leave such names
		// alone). An image of that name from before the last restart of the renderer is found again here and
		// gets the page with the first flush.
		Com_sprintf( name, sizeof( name ), "*fontpage%i", ttf.numPages );
		page->image = R_CreateImage( name, page->rgba, FONT_PAGE_SIZE, FONT_PAGE_SIZE, GL_RGBA, qfalse, qfalse, qfalse, GL_CLAMP );
		if ( !page->image ) {
			ri.Z_Free( page->rgba );
			page->rgba = NULL;
			return NULL;
		}
		page->shader = RE_RegisterShaderFromImage( name, (int *)lightmaps2d, (byte *)stylesDefault, page->image, qfalse );
		// text in the scene is drawn last of all that is blended, over the panel it stands on
		Com_sprintf( name, sizeof( name ), "*fontpage%i_scene", ttf.numPages );
		page->shaderScene = RE_RegisterSceneShader( name, page->image, 0, SS_NEAREST );
		page->x = page->y = 1;
		page->dirty = qtrue;
		ttf.numPages++;
	}
	return page;
}

// floor and ceiling of a / b for a positive b
static int FloorDiv( int a, int b ) {
	return ( a >= 0 ) ? a / b : -( ( -a + b - 1 ) / b );
}

static int CeilDiv( int a, int b ) {
	return -FloorDiv( -a, b );
}

// Makes the picture of a glyph and fills in where it is and how it is placed, qfalse when that can not be done.
// The glyph is `size` units tall and is made with k pixels a unit: what comes back (width, height, offset,
// baseline, advance) is in units.
static qboolean R_FontTTF_Make( const stbtt_fontinfo *font, unsigned int codePoint, int size, int k, ttfGlyph_t *glyph ) {
	static byte	coverage[FONT_MAX_GLYPH * FONT_MAX_GLYPH];
	const int	index = stbtt_FindGlyphIndex( font, (int)codePoint );
	const float	scale = stbtt_ScaleForMappingEmToPixels( font, (float)( size * k ) );
	int			x0, y0, x1, y1, advance, bearing;
	int			cellX, cellY, cellW, cellH;
	ttfPage_t	*page;

	if ( !index || size < 1 ) {
		return qfalse;
	}
	stbtt_GetGlyphHMetrics( font, index, &advance, &bearing );
	stbtt_GetGlyphBitmapBox( font, index, scale, scale, &x0, &y0, &x1, &y1 );

	glyph->info.horizAdvance = (short)Q_max( 1, (int)( advance * scale / k + 0.5f ) );
	if ( x1 <= x0 || y1 <= y0 ) {
		// nothing to draw (a kind of space). A width of 0 would make the font code draw a '.' instead: one
		// unit of the empty corner of the first page.
		page = R_FontTTF_Room( 1, 1 );
		if ( !page ) {
			return qfalse;
		}
		glyph->info.width = glyph->info.height = 1;
		glyph->info.s = glyph->info.t = glyph->info.s2 = glyph->info.t2 = 0.0f;
		glyph->shader = ttf.pages[0].shader;
		glyph->shaderScene = ttf.pages[0].shaderScene;
		return qtrue;
	}

	// the cell of the glyph starts and ends on whole units, so a quad that is drawn in units covers exactly the
	// pixels of the cell
	cellX = FloorDiv( x0, k ) * k;
	cellY = FloorDiv( y0, k ) * k;
	cellW = CeilDiv( x1 - cellX, k ) * k;
	cellH = CeilDiv( y1 - cellY, k ) * k;
	if ( x1 - x0 > FONT_MAX_GLYPH || y1 - y0 > FONT_MAX_GLYPH || cellW + 2 > FONT_PAGE_SIZE || cellH + 2 > FONT_PAGE_SIZE ) {
		return qfalse;
	}
	page = R_FontTTF_Room( cellW, cellH );
	if ( !page ) {
		return qfalse;
	}

	stbtt_MakeGlyphBitmap( font, coverage, x1 - x0, y1 - y0, FONT_MAX_GLYPH, scale, scale, index );
	for ( int y = 0; y < y1 - y0; y++ ) {
		byte *out = page->rgba + ( ( page->y + ( y0 - cellY ) + y ) * FONT_PAGE_SIZE + page->x + ( x0 - cellX ) ) * 4;

		for ( int x = 0; x < x1 - x0; x++ ) {
			out[x * 4 + 3] = coverage[y * FONT_MAX_GLYPH + x];
		}
	}

	// (the cell covers whole units and the bitmap box whole pixels around the outline: R_FontTTF_InkBox wants the
	// letter itself)
	{
		int ix0, iy0, ix1, iy1;

		if ( stbtt_GetGlyphBox( font, index, &ix0, &iy0, &ix1, &iy1 ) ) {
			glyph->ink[0] = ix0 * scale / k;
			glyph->ink[1] = -iy1 * scale / k;
			glyph->ink[2] = ix1 * scale / k;
			glyph->ink[3] = -iy0 * scale / k;
		} else {
			glyph->ink[0] = (float)x0 / k;
			glyph->ink[1] = (float)y0 / k;
			glyph->ink[2] = (float)x1 / k;
			glyph->ink[3] = (float)y1 / k;
		}
	}
	glyph->info.width = (short)( cellW / k );
	glyph->info.height = (short)( cellH / k );
	glyph->info.horizOffset = (short)( cellX / k );
	glyph->info.baseline = -cellY / k;			// from the top of the cell down to the line the text stands on
	glyph->info.s = (float)page->x / FONT_PAGE_SIZE;
	glyph->info.t = (float)page->y / FONT_PAGE_SIZE;
	glyph->info.s2 = (float)( page->x + cellW ) / FONT_PAGE_SIZE;
	glyph->info.t2 = (float)( page->y + cellH ) / FONT_PAGE_SIZE;
	glyph->shader = page->shader;
	glyph->shaderScene = page->shaderScene;

	page->x += cellW + 1;
	if ( cellH > page->rowHeight ) {
		page->rowHeight = cellH;
	}
	page->dirty = qtrue;
	return qtrue;
}

// The glyph of a character in a font, `size` units tall and made with k pixels a unit. NULL when the font is
// not there, does not have the character, or the pages are full. The pointer stays valid until the fonts are
// shut down.
static const ttfGlyph_t *R_FontTTF_Find( int font, unsigned int codePoint, int size, int k ) {
	if ( !ttf.fonts[font].loaded || size < 1 || size > 0xFFFF ) {
		return NULL;
	}

	const uint64_t			key = ( (uint64_t)font << 56 ) | ( (uint64_t)k << 48 ) | ( (uint64_t)size << 32 ) | codePoint;
	ttfGlyphMap_t::iterator	it = ttf.glyphs->find( key );

	if ( it == ttf.glyphs->end() ) {
		ttfGlyph_t glyph;

		memset( &glyph, 0, sizeof( glyph ) );
		glyph.valid = R_FontTTF_Make( &ttf.fonts[font].info, codePoint, size, k, &glyph );
		it = ttf.glyphs->insert( std::make_pair( key, glyph ) ).first;
	}
	return it->second.valid ? &it->second : NULL;
}

// The glyph of a character from font first, or else from the first of the others that has it (text, fixed
// width, CJK). which: the font it came from. NULL: none has it.
static const ttfGlyph_t *R_FontTTF_FindAny( int first, unsigned int codePoint, int size, int k, int *which ) {
	const ttfGlyph_t	*glyph = R_FontTTF_Find( first, codePoint, size, k );
	int					font = first;

	for ( int other = 0; !glyph && other < TTF_NUM_FONTS; other++ ) {
		if ( other != first ) {
			glyph = R_FontTTF_Find( other, codePoint, size, k );
			font = other;
		}
	}
	if ( which ) {
		*which = font;
	}
	return glyph;
}

/*
===============
R_FontTTF_Glyph

The glyph of a character for a font of the game of that point size. NULL when there is no TrueType font, it
does not have the character, or the pages are full: the caller draws what it draws for an unknown character.
===============
*/
const glyphInfo_t *R_FontTTF_Glyph( unsigned int codePoint, int pointSize, int *shader ) {
	const ttfGlyph_t *glyph = R_FontTTF_FindAny( TTF_TEXT, codePoint, pointSize, ttf.oversample, NULL );

	if ( !glyph ) {
		return NULL;
	}
	if ( shader ) {
		*shader = glyph->shader;
	}
	return &glyph->info;
}

/*
===============
RE_Font_DrawMonoChar

A character of the font with a fixed width in a cell of the console. Place and size of the cell are pixels of the
window: the glyph is made in the size in which a letter is as wide as the cell, and is drawn pixel for pixel. A
character the fixed width font does not have is taken from another font, which may reach into the next cell.
qfalse: nothing was drawn, there is no font or none has the character.
===============
*/
qboolean RE_Font_DrawMonoChar( int x, int y, int cellWidth, int cellHeight, unsigned int codePoint ) {
	const ttfFont_t		*mono = &ttf.fonts[TTF_MONO];
	const ttfFont_t		*font;
	const ttfGlyph_t	*glyph;
	int					size, baseline, which;
	float				scale, xScale, yScale, bias, biasY;

	if ( !mono->loaded || mono->advance <= 0.0f || cellWidth < 1 || cellHeight < 1 ) {
		return qfalse;
	}
	size = (int)( cellWidth / mono->advance + 0.5f );
	glyph = R_FontTTF_FindAny( TTF_MONO, codePoint, size, 1, &which );
	if ( !glyph ) {
		return qfalse;
	}
	font = &ttf.fonts[which];

	// the line the letters stand on: where it is when the height of the font sits in the middle of the cell
	scale = stbtt_ScaleForMappingEmToPixels( &font->info, (float)size );
	baseline = y + (int)( ( cellHeight - ( font->ascent - font->descent ) * scale ) * 0.5f + font->ascent * scale + 0.5f );

	// from pixels of the window to the screen of the 2D, as wide and high as R_2DBias() and R_2DBiasY() make it
	bias = R_2DBias();
	biasY = R_2DBiasY();
	xScale = ( SCREEN_WIDTH + 2.0f * bias ) / glConfig.vidWidth;
	yScale = ( SCREEN_HEIGHT + 2.0f * biasY ) / glConfig.vidHeight;
	RE_StretchPic( ( x + glyph->info.horizOffset ) * xScale - bias, ( baseline - glyph->info.baseline ) * yScale - biasY,
		glyph->info.width * xScale, glyph->info.height * yScale,
		glyph->info.s, glyph->info.t, glyph->info.s2, glyph->info.t2, glyph->shader );
	R_FontTTF_Flush();
	return qtrue;
}

/*
===============================================================================

A WHOLE FONT OF THE MENUS

"vision_sans" and "vision_mono": text of the 2D screen with every letter from a TrueType font. A size is the
height of the font in units of the 480 the screen is high; the letters are made for the pixels that is in the
window and are placed on whole pixels.

===============================================================================
*/

// where the line the letters stand on is, below the top of a line of text, as a part of its size: so that a
// capital letter (0.714 of the size in Noto Sans) stands in the middle of a line that is as high as the size
#define TTF_MENU_BASELINE	0.857f

// TTF_TEXT, TTF_MONO or TTF_BOLD for the names the menus know the fonts by, -1: no such font, or its file is not there
int R_FontTTF_ByName( const char *name ) {
	const int font = !Q_stricmp( name, "vision_sans" ) ? TTF_TEXT : !Q_stricmp( name, "vision_mono" ) ? TTF_MONO
		: !Q_stricmp( name, "vision_sans_bold" ) ? TTF_BOLD : -1;

	return ( font >= 0 && ttf.fonts[font].loaded ) ? font : -1;
}

// the size in pixels of the window a text of that size in units is made in
// The colour of a colour code for the letters of the new look. Wherever they are used they stand on something
// dark (the glass of the menus and of the HUD, a bubble in the level), where the black of ^0 can not be read:
// for them it is a grey that can. (A name in black and red was half a name in the list of servers.)
static const float *R_FontTTF_CodeColor( int index ) {
	static const vec4_t darkGrey = { 0.38f, 0.41f, 0.46f, 1.0f };

	return ( index == 0 ) ? darkGrey : g_color_table[index];
}

static int R_FontTTF_MenuPixels( float size ) {
	return Com_Clampi( 4, FONT_MAX_GLYPH, (int)( size * glConfig.vidHeight / ( SCREEN_HEIGHT + 2.0f * R_2DBiasY() ) + 0.5f ) );
}

// The next character of a text and its glyph. qfalse: the text is over. *glyph is NULL for a character that
// draws nothing here (a colour code, which sets *colour, a line feed, a character no font has).
static qboolean R_FontTTF_MenuChar( int font, const char **text, int pixels, const ttfGlyph_t **glyph, int *colour, qboolean *newLine ) {
	unsigned int	codePoint;
	const byte		*in = (const byte *)*text;

	*glyph = NULL;
	*colour = -1;
	*newLine = qfalse;
	if ( !in[0] ) {
		return qfalse;
	}
	if ( in[0] == '^' && in[1] >= '0' && in[1] <= '9' ) {
		*colour = ColorIndex( in[1] );
		*text += 2;
		return qtrue;
	}
	if ( in[0] >= 0x80 && Font_TextIsUTF8() ) {
		*text += Q_UTF8_Decode( *text, &codePoint );
	} else {
		codePoint = Q_CP1252ToUnicode( in[0] );
		*text += 1;
	}
	if ( codePoint == '\n' ) {
		*newLine = qtrue;
		return qtrue;
	}
	if ( codePoint < ' ' ) {
		return qtrue;
	}
	*glyph = R_FontTTF_FindAny( font, codePoint, pixels, 1, NULL );
	if ( !*glyph ) {
		*glyph = R_FontTTF_Find( font, '?', pixels, 1 );
	}
	return qtrue;
}

// units of the 2D screen for a pixel of the window, as wide and high as R_2DBias() and R_2DBiasY() make it
static void R_FontTTF_MenuScale( float *xScale, float *yScale, float *bias, float *biasY ) {
	*bias = R_2DBias();
	*biasY = R_2DBiasY();
	*xScale = ( SCREEN_WIDTH + 2.0f * *bias ) / glConfig.vidWidth;
	*yScale = ( SCREEN_HEIGHT + 2.0f * *biasY ) / glConfig.vidHeight;
}

// how wide a text of that size is, in units: its widest line
float R_FontTTF_StringWidth( int font, const char *text, float size ) {
	const int			pixels = R_FontTTF_MenuPixels( size );
	const ttfGlyph_t	*glyph;
	float				xScale, yScale, bias, biasY;
	int					colour, width = 0, widest = 0;
	qboolean			newLine;

	while ( R_FontTTF_MenuChar( font, &text, pixels, &glyph, &colour, &newLine ) ) {
		if ( newLine ) {
			width = 0;
		} else if ( glyph ) {
			width += glyph->info.horizAdvance;
			if ( width > widest ) {
				widest = width;
			}
		}
	}
	R_FontTTF_MenuScale( &xScale, &yScale, &bias, &biasY );
	return widest * xScale;
}

/*
===============
R_FontTTF_InkBox

Where the letters of a text of that size cover pixels, as R_FontTTF_DrawString draws them at 0,0: box is left,
top, right, bottom in units. A text of spaces only has an empty box at 0,0. A line feed starts a line below.
===============
*/
void R_FontTTF_InkBox( int font, const char *text, float size, float *box ) {
	const int			pixels = R_FontTTF_MenuPixels( size );
	const ttfGlyph_t	*glyph;
	float				xScale, yScale, bias, biasY;
	int					colour, penX = 0, lineY = 0;
	float				left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
	qboolean			newLine, ink = qfalse;

	while ( R_FontTTF_MenuChar( font, &text, pixels, &glyph, &colour, &newLine ) ) {
		if ( newLine ) {
			penX = 0;
			lineY += pixels;
			continue;
		}
		if ( !glyph ) {
			continue;
		}
		// (the 1 x 1 corner R_FontTTF_Make gives a space covers nothing)
		if ( glyph->info.s2 > glyph->info.s ) {
			const float x0 = penX + glyph->ink[0], y0 = lineY + glyph->ink[1];
			const float x1 = penX + glyph->ink[2], y1 = lineY + glyph->ink[3];

			if ( !ink ) {
				left = x0; top = y0; right = x1; bottom = y1;
				ink = qtrue;
			} else {
				left = Q_min( left, x0 ); top = Q_min( top, y0 );
				right = Q_max( right, x1 ); bottom = Q_max( bottom, y1 );
			}
		}
		penX += glyph->info.horizAdvance;
	}
	R_FontTTF_MenuScale( &xScale, &yScale, &bias, &biasY );
	box[0] = left * xScale;
	box[2] = right * xScale;
	box[1] = ink ? size * TTF_MENU_BASELINE + top * yScale : 0.0f;
	box[3] = ink ? size * TTF_MENU_BASELINE + bottom * yScale : 0.0f;
}

/*
===============
R_FontTTF_DrawString

A text of the menus, its first line with its top at y. maxWidth: units after which no further letter is drawn,
-1 for all of it. With shadow the text is drawn in one colour and a ^ with a digit changes nothing.
===============
*/
void R_FontTTF_DrawString( int font, float x, float y, const char *text, const float *rgba, float size, int maxWidth, qboolean shadow ) {
	const int			pixels = R_FontTTF_MenuPixels( size );
	const ttfGlyph_t	*glyph;
	float				xScale, yScale, bias, biasY;
	int					colour, penX, startX, baseline, limit;
	qboolean			newLine;

	R_FontTTF_MenuScale( &xScale, &yScale, &bias, &biasY );
	startX = penX = (int)floorf( ( x + bias ) / xScale + 0.5f );
	baseline = (int)floorf( ( y + biasY + size * TTF_MENU_BASELINE ) / yScale + 0.5f );
	limit = ( maxWidth < 0 ) ? -1 : (int)( maxWidth / xScale );

	RE_SetColor( rgba );
	while ( R_FontTTF_MenuChar( font, &text, pixels, &glyph, &colour, &newLine ) ) {
		if ( colour >= 0 ) {
			if ( !shadow ) {
				vec4_t tint;

				Com_Memcpy( tint, R_FontTTF_CodeColor( colour ), sizeof( tint ) );
				tint[3] = rgba ? rgba[3] : 1.0f;
				RE_SetColor( tint );
			}
			continue;
		}
		if ( newLine ) {
			penX = startX;
			baseline += pixels;
			continue;
		}
		if ( !glyph ) {
			continue;
		}
		if ( limit >= 0 && penX + glyph->info.horizAdvance - startX > limit ) {
			break;
		}
		RE_StretchPic( ( penX + glyph->info.horizOffset ) * xScale - bias, ( baseline - glyph->info.baseline ) * yScale - biasY,
			glyph->info.width * xScale, glyph->info.height * yScale,
			glyph->info.s, glyph->info.t, glyph->info.s2, glyph->info.t2, glyph->shader );
		penX += glyph->info.horizAdvance;
	}
	R_FontTTF_Flush();
}

/*
===============================================================================

TEXT IN THE SCENE

For what a module writes into the level (the chat bubbles of the cgame): glyphs of TTF_TEXT as polygons, made
about as large as they are on the screen at a usual distance and scaled from there by the graphics card.

===============================================================================
*/

// the pixels a letter of text in the scene is made with
static int R_FontTTF_SceneSize( void ) {
	return Com_Clampi( 16, 48, glConfig.vidHeight / 36 );
}

// The next character of a text for the scene. qfalse: it was a colour code, which is in color then (the alpha
// stays). codePoint 0 ends the text.
static qboolean R_FontTTF_SceneChar( const char **text, unsigned int *codePoint, float *color ) {
	int advance;

	if ( Q_IsColorString( *text ) ) {
		if ( color ) {
			VectorCopy( R_FontTTF_CodeColor( ColorIndex( (*text)[1] ) ), color );
		}
		*text += 2;
		return qfalse;
	}
	*codePoint = AnyLanguage_ReadCharFromString( *text, &advance, NULL );
	if ( !Font_TextIsUTF8() ) {
		*codePoint = Q_CP1252ToUnicode( (int)*codePoint );
	}
	*text += advance;
	return qtrue;
}

static const ttfGlyph_t *R_FontTTF_SceneGlyph( unsigned int codePoint, int size ) {
	const ttfGlyph_t *glyph = R_FontTTF_FindAny( TTF_TEXT, codePoint, size, 1, NULL );

	return glyph ? glyph : R_FontTTF_Find( TTF_TEXT, '?', size, 1 );
}

/*
===============
RE_Font_Text3DWidth

How wide a text is in the scene with letters of that height. 0 when there is no font.
===============
*/
float RE_Font_Text3DWidth( const char *text, float height ) {
	const int	size = R_FontTTF_SceneSize();
	float		width = 0.0f;

	if ( !ttf.fonts[TTF_TEXT].loaded || !text ) {
		return 0.0f;
	}
	while ( *text ) {
		unsigned int		codePoint;
		const ttfGlyph_t	*glyph;

		if ( !R_FontTTF_SceneChar( &text, &codePoint, NULL ) ) {
			continue;
		}
		glyph = R_FontTTF_SceneGlyph( codePoint, size );
		if ( glyph ) {
			width += glyph->info.horizAdvance;
		}
	}
	return width * height / size;
}

/*
===============
RE_Font_Text3D

Adds a text to the scene: a polygon a glyph. The first letter stands at origin on the line of the text, which
runs along right, up is up for the letters, both of length 1. qfalse: there is no font.
===============
*/
qboolean RE_Font_Text3D( const vec3_t origin, const vec3_t right, const vec3_t up, float height, const char *text, const float *rgba ) {
	const int	size = R_FontTTF_SceneSize();
	const float	unit = height / size;		// units of the level a pixel of a glyph
	vec4_t		color;
	float		pen = 0.0f;

	if ( !ttf.fonts[TTF_TEXT].loaded || !text ) {
		return qfalse;
	}
	if ( rgba ) {
		VectorCopy4( rgba, color );
	} else {
		VectorSet4( color, 1.0f, 1.0f, 1.0f, 1.0f );
	}

	while ( *text ) {
		unsigned int		codePoint;
		const ttfGlyph_t	*glyph;
		polyVert_t			verts[4];
		float				x[2], y[2];

		if ( !R_FontTTF_SceneChar( &text, &codePoint, color ) ) {
			continue;
		}
		glyph = R_FontTTF_SceneGlyph( codePoint, size );
		if ( !glyph ) {
			continue;
		}
		if ( glyph->info.s != glyph->info.s2 ) {
			// left and right, top and bottom of the glyph, from the place of the pen on the line
			x[0] = pen + glyph->info.horizOffset * unit;
			x[1] = x[0] + glyph->info.width * unit;
			y[0] = glyph->info.baseline * unit;
			y[1] = y[0] - glyph->info.height * unit;

			for ( int i = 0; i < 4; i++ ) {
				const int	side = ( i == 1 || i == 2 ) ? 1 : 0;	// 0 1 2 3: top left, top right, bottom right, bottom left
				const int	low = ( i >= 2 ) ? 1 : 0;

				VectorMA( origin, x[side], right, verts[i].xyz );
				VectorMA( verts[i].xyz, y[low], up, verts[i].xyz );
				verts[i].st[0] = side ? glyph->info.s2 : glyph->info.s;
				verts[i].st[1] = low ? glyph->info.t2 : glyph->info.t;
				for ( int c = 0; c < 4; c++ ) {
					verts[i].modulate[c] = (byte)( Com_Clamp( 0.0f, 1.0f, color[c] ) * 255.0f );
				}
			}
			RE_AddPolyToScene( glyph->shaderScene, 4, verts, 1 );
		}
		pen += glyph->info.horizAdvance * unit;
	}
	R_FontTTF_Flush();
	return qtrue;
}

/*
===============
RE_RegisterShaderRGBA

A shader for a picture a module made itself, for polygons in the scene. The picture needs no file, so it is there
on a pure server that does not have the pk3 of this tree. Width and height are powers of two up to 512. An
image of that name from before the last restart of the renderer is found again and keeps its picture: a module
hands in the same picture under the same name. flags: RGBA_SHADER_GLOW, RGBA_SHADER_OVER, RGBA_SHADER_BEHIND,
and RGBA_SHADER_HULL for a shader a model is drawn with a second time, as an outline around it.
===============
*/
qhandle_t RE_RegisterShaderRGBA( const char *name, const byte *rgba, int width, int height, int flags ) {
	const qboolean first = (qboolean)( ( flags & ( RGBA_SHADER_GLOW | RGBA_SHADER_BEHIND ) ) != 0 );
	char	imageName[MAX_QPATH];
	image_t	*image;

	if ( !name || !rgba || width < 1 || height < 1 || width > 512 || height > 512
		|| ( width & ( width - 1 ) ) || ( height & ( height - 1 ) ) ) {
		return 0;
	}
	// the star keeps the image over the change of a level, see R_FontTTF_Room()
	Com_sprintf( imageName, sizeof( imageName ), "*rgba_%s", name );
	if ( strchr( imageName, '/' ) ) {
		return 0;
	}
	image = R_CreateImage( imageName, rgba, width, height, GL_RGBA, qtrue, qfalse, qfalse, GL_CLAMP );
	if ( !image ) {
		return 0;
	}
	// What glows is drawn before the rest a module draws, a plain picture before the text that stands on it.
	// What stands over the scene comes a step later each, after everything that could be drawn across it.
	if ( flags & RGBA_SHADER_OVER ) {
		return RE_RegisterSceneShader( imageName, image, flags, first ? SS_ALMOST_NEAREST : SS_NEAREST );
	}
	// what only the buffer of the glow gets comes before everything else that glows and is blended
	if ( flags & RGBA_SHADER_BLUR ) {
		return RE_RegisterSceneShader( imageName, image, flags, SS_SEE_THROUGH );
	}
	return RE_RegisterSceneShader( imageName, image, flags, first ? SS_BLEND6 : SS_ALMOST_NEAREST );
}

// for fontlist
void R_FontTTF_Info( void ) {
	if ( !ttf.fonts[TTF_TEXT].loaded ) {
		ri.Printf( PRINT_ALL, "No TrueType font: characters the fonts of the game do not have are not drawn\n" );
	}
	if ( !ttf.fonts[TTF_MONO].loaded ) {
		ri.Printf( PRINT_ALL, "No TrueType font with a fixed width: the console uses the letters of the game\n" );
	}
	if ( !ttf.fonts[TTF_CJK].loaded ) {
		ri.Printf( PRINT_ALL, "No TrueType font for Chinese, Japanese and Korean: their characters are drawn as ?\n" );
	}
	if ( ttf.glyphs ) {
		ri.Printf( PRINT_ALL, "TrueType: %i glyphs made on %i pages of %i x %i, %i pixels a unit of the 2D screen\n",
			(int)ttf.glyphs->size(), ttf.numPages, FONT_PAGE_SIZE, FONT_PAGE_SIZE, ttf.oversample );
	}
}
