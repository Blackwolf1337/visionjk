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

// cl_font.cpp -- the font files of this tree.
//
// The fonts of the game are pictures with 256 glyphs. The renderer draws any other character from a TrueType
// font, and the console from one with a fixed width (rd-common/tr_font_ttf.cpp). Both are files in the pk3 of
// this tree. The engine reads them, not the renderer, and reads each once:
//
// - A renderer starts anew with every level. On a pure server that does not have the pk3 of this tree the file
//   system no longer hands out its files, so the font is read at the first start of the renderer, before any
//   server was joined, and kept.
// - A server can send a pk3 with a file of the same name, and the code that reads a font is not made for a
//   hostile one. Only the file this build shipped is taken: its SHA-256 is compiled in (FONT_SHA256 and
//   FONT_MONO_SHA256, from codemp/CMakeLists.txt). Any other file is refused with a line in the console.

#include "client.h"
#include "web/web_http.h"

typedef struct clFont_s {
	const char	*file;
	const char	*sha256;
	byte		*data;
	int			length;
	qboolean	tried;
} clFont_t;

// in the order of the fonts of rd-common/tr_font_ttf.h
static clFont_t cl_fonts[] = {
	{ "fonts/visionjk/NotoSans-Regular.ttf", FONT_SHA256, NULL, 0, qfalse },	// for what the fonts of the game lack
	{ "fonts/visionjk/DejaVuSansMono.ttf", FONT_MONO_SHA256, NULL, 0, qfalse },	// fixed width, for the console
};

/*
====================
CL_FontFile

The bytes of a font and their count, NULL when there is no such font. Stays valid until the program ends.
====================
*/
const byte *CL_FontFile( int font, int *length ) {
	clFont_t *f;

	*length = 0;
	if ( font < 0 || font >= (int)ARRAY_LEN( cl_fonts ) ) {
		return NULL;
	}
	f = &cl_fonts[font];
	if ( !f->tried ) {
		void		*buffer = NULL;
		const int	len = FS_ReadFile( f->file, &buffer );

		f->tried = qtrue;
		if ( len <= 0 || !buffer ) {
			Com_Printf( S_COLOR_YELLOW "No %s: text is drawn without it\n", f->file );
		} else {
			webSha256_t		sha;
			unsigned char	digest[WEB_SHA256_SIZE];
			char			hex[WEB_SHA256_SIZE * 2 + 1];

			Web_SHA256Init( &sha );
			Web_SHA256Update( &sha, buffer, (size_t)len );
			Web_SHA256Final( &sha, digest );
			for ( int i = 0; i < WEB_SHA256_SIZE; i++ ) {
				Com_sprintf( hex + i * 2, 3, "%02x", digest[i] );
			}
			if ( Q_stricmp( hex, f->sha256 ) ) {
				Com_Printf( S_COLOR_YELLOW "%s is not the file of this build, it is not used\n", f->file );
			} else {
				f->data = (byte *)Z_Malloc( len, TAG_STATIC, qfalse );
				Com_Memcpy( f->data, buffer, len );
				f->length = len;
			}
			FS_FreeFile( buffer );
		}
	}
	*length = f->length;
	return f->data;
}
