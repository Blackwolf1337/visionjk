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

// tr_font_ttf.cpp -- glyphs from a TrueType font for the characters the fonts of the game do not have.
//
// A font of the game (CFontInfo in tr_font.cpp) is a picture with the 256 characters of Windows-1252. Where
// text is UTF-8, a character outside of them is drawn from the TrueType font the engine hands out (ri.FontFile,
// client/cl_font.cpp): its outline is made into a small picture, and the pictures are collected on pages,
// textures of FONT_PAGE_SIZE. A page that got new glyphs is sent to the graphics card once, by
// R_FontTTF_Flush(), before the frame that uses them is drawn.
//
// A glyph is made for the point size of the font of the game it stands in, so it sits on the same line and
// is about as tall. The fonts of the game are pictures made for a screen of 640 x 480; these glyphs are made
// with as many pixels a unit of that screen as the window has (ttf.oversample), so they are sharp where the
// letters of the game are not.

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
	byte		*rgba;					// FONT_PAGE_SIZE x FONT_PAGE_SIZE, white with the glyphs in the alpha
	int			x, y;					// where the next glyph goes
	int			rowHeight;				// of the row that is being filled
	qboolean	dirty;					// has glyphs the graphics card does not have yet
} ttfPage_t;

typedef struct ttfGlyph_s {
	glyphInfo_t	info;
	qhandle_t	shader;
	qboolean	valid;					// qfalse: the font has no such character, or no page had room
} ttfGlyph_t;

typedef std::map<uint64_t, ttfGlyph_t> ttfGlyphMap_t;

static struct {
	qboolean		loaded;
	stbtt_fontinfo	font;
	int				oversample;			// pixels of a page for one unit of the 640 x 480 screen
	ttfPage_t		pages[FONT_MAX_PAGES];
	int				numPages;
	ttfGlyphMap_t	*glyphs;			// by point size and code point
} ttf;

/*
===============
R_FontTTF_Init
===============
*/
void R_FontTTF_Init( void ) {
	int			length = 0;
	const byte	*data = ri.FontFile ? ri.FontFile( &length ) : NULL;

	memset( &ttf, 0, sizeof( ttf ) );
	if ( !data || length <= 0 ) {
		return;
	}
	if ( !stbtt_InitFont( &ttf.font, data, stbtt_GetFontOffsetForIndex( data, 0 ) ) ) {
		ri.Printf( PRINT_WARNING, "R_FontTTF_Init: the font file can not be read\n" );
		return;
	}
	ttf.oversample = Com_Clampi( 1, FONT_MAX_OVERSAMPLE, ( glConfig.vidHeight + SCREEN_HEIGHT - 1 ) / SCREEN_HEIGHT );
	ttf.glyphs = new ttfGlyphMap_t;
	ttf.loaded = qtrue;
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

// makes the picture of a glyph and fills in where it is and how it is placed, qfalse when that can not be done
static qboolean R_FontTTF_Make( unsigned int codePoint, int pointSize, ttfGlyph_t *glyph ) {
	static byte	coverage[FONT_MAX_GLYPH * FONT_MAX_GLYPH];
	const int	k = ttf.oversample;
	const int	index = stbtt_FindGlyphIndex( &ttf.font, (int)codePoint );
	const float	scale = stbtt_ScaleForMappingEmToPixels( &ttf.font, (float)( pointSize * k ) );
	int			x0, y0, x1, y1, advance, bearing;
	int			cellX, cellY, cellW, cellH;
	ttfPage_t	*page;

	if ( !index || pointSize < 1 ) {
		return qfalse;
	}
	stbtt_GetGlyphHMetrics( &ttf.font, index, &advance, &bearing );
	stbtt_GetGlyphBitmapBox( &ttf.font, index, scale, scale, &x0, &y0, &x1, &y1 );

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
		return qtrue;
	}

	// the cell of the glyph starts and ends on whole units of the 640 x 480 screen, so the quad the font code
	// draws (in such units) covers exactly the pixels of the cell
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

	stbtt_MakeGlyphBitmap( &ttf.font, coverage, x1 - x0, y1 - y0, FONT_MAX_GLYPH, scale, scale, index );
	for ( int y = 0; y < y1 - y0; y++ ) {
		byte *out = page->rgba + ( ( page->y + ( y0 - cellY ) + y ) * FONT_PAGE_SIZE + page->x + ( x0 - cellX ) ) * 4;

		for ( int x = 0; x < x1 - x0; x++ ) {
			out[x * 4 + 3] = coverage[y * FONT_MAX_GLYPH + x];
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

	page->x += cellW + 1;
	if ( cellH > page->rowHeight ) {
		page->rowHeight = cellH;
	}
	page->dirty = qtrue;
	return qtrue;
}

/*
===============
R_FontTTF_Glyph

The glyph of a character for a font of the game of that point size. NULL when there is no TrueType font, it
does not have the character, or the pages are full: the caller draws what it draws for an unknown character.
The pointer stays valid until the fonts are shut down.
===============
*/
const glyphInfo_t *R_FontTTF_Glyph( unsigned int codePoint, int pointSize, int *shader ) {
	if ( !ttf.loaded ) {
		return NULL;
	}

	const uint64_t			key = ( (uint64_t)(unsigned int)pointSize << 32 ) | codePoint;
	ttfGlyphMap_t::iterator	it = ttf.glyphs->find( key );

	if ( it == ttf.glyphs->end() ) {
		ttfGlyph_t glyph;

		memset( &glyph, 0, sizeof( glyph ) );
		glyph.valid = R_FontTTF_Make( codePoint, pointSize, &glyph );
		it = ttf.glyphs->insert( std::make_pair( key, glyph ) ).first;
	}
	if ( !it->second.valid ) {
		return NULL;
	}
	if ( shader ) {
		*shader = it->second.shader;
	}
	return &it->second.info;
}

// for fontlist
void R_FontTTF_Info( void ) {
	if ( !ttf.loaded ) {
		ri.Printf( PRINT_ALL, "No TrueType font: characters the fonts of the game do not have are not drawn\n" );
		return;
	}
	ri.Printf( PRINT_ALL, "TrueType font: %i glyphs made on %i pages of %i x %i, %i pixels a unit of the 2D screen\n",
		(int)ttf.glyphs->size(), ttf.numPages, FONT_PAGE_SIZE, FONT_PAGE_SIZE, ttf.oversample );
}
