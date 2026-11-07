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

// cl_sprays.cpp -- the pictures of the sprays (cgame/toys/cg_spray.c, issue #9 of the game). Not from OpenJK, see
// docs/japlus/toys.md.
//
// Every spray is a sheet of PAINT_SHEET x PAINT_SHEET pixels in a slot of the renderer (re.ext.PaintImage): a still
// picture fills it, the frames of an animated one lie in a grid in it (up to 4 at 256, up to PAINT_FRAMES at 128).
// The cgame names a picture and a slot (ext.R_PaintPicture): an emoji of the game or of the chat store
// (gfx/emoji/<name>.png), a file of the player in sprays/ of the home folder (png, jpg, tga or an animated gif), or
// a sheet of another player that cl_models.cpp fetched from the store of the site (sprays/store/<number>.png).
// A sheet for the store is a PNG with the chunk SPRAY_CHUNK, which says how its frames lie and how long each is
// shown: "vjAn" (ancillary, private, safe to copy), a byte 1, the frames, the columns, the pixels of a frame each way
// (two bytes), then two bytes for each frame, its milliseconds, all big endian as in PNG. modelstore.py checks the
// same. Each frame is fitted into its cell with a margin and its sides kept, averaged with its alpha so no dark
// fringe comes in, and the colour of its edge runs out under the transparent part, so the smaller levels of the
// mip map, which the paint of the renderer reads, stay clean at the edge too.

#include "client.h"

#ifdef USE_INTERNAL_ZLIB
#include "zlib/zlib.h"
#else
#include <zlib.h>
#endif

// stb_image (public domain or MIT, lib/stb), only its GIF decoder, frame by frame
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MAX_DIMENSIONS	2048
#define STBI_ASSERT( x )
#if defined( __GNUC__ )
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#include "stb/stb_image.h"
#if defined( __GNUC__ )
#pragma GCC diagnostic pop
#endif

#define SPRAY_CHUNK			"vjAn"
#define SPRAY_GIF_MOST		( 4 * 1024 * 1024 )		// bytes of a gif at most
#define SPRAY_DELAY_GIF		100						// msec of a frame of a gif that says 0, as browsers show it
#define SPRAY_DELAY_LEAST	20
#define SPRAY_DELAY_MOST	10000
#define SPRAY_MARGIN		16						// a frame lies this many sixteenths of its cell in from each side
#define SPRAY_BLEED			16						// pixels the colour of an edge runs out under what is transparent

static byte				spraySheet[PAINT_SHEET * PAINT_SHEET * 4];
static byte				sprayUpload[SPRAY_UPLOAD_MOST];		// the own sheet for the store (CL_SprayUploadMake)
static int				sprayUploadLen;

// the cell of frame n of a picture
static void Spray_Cell( const paintPicture_t *pic, int n, int *x, int *y ) {
	*x = ( n % pic->columns ) * pic->cell;
	*y = ( n / pic->columns ) * pic->cell;
}

// how a picture of frames lies in a sheet of PAINT_SHEET
static void Spray_Layout( paintPicture_t *pic, int frames ) {
	pic->frames = frames;
	pic->sheet = PAINT_SHEET;
	pic->columns = frames == 1 ? 1 : frames <= 4 ? 2 : 4;
	pic->cell = PAINT_SHEET / pic->columns;
}

/*
=================
Spray_Fit

A picture of w x h RGBA into the square box of side at x, y of the sheet, its sides kept and centred: shrunk, a
pixel of the box is the mean of the pixels it covers, grown, the mix of the four nearest. Both weigh the colour by its
alpha, so the black of a transparent pixel does not darken the edge.
=================
*/
static void Spray_Fit( const byte *src, int w, int h, byte *sheet, int sheetSize, int x, int y, int side ) {
	const float	scale = (float)side / (float)Q_max( w, h );
	const int	dw = Q_max( 1, (int)( w * scale + 0.5f ) ), dh = Q_max( 1, (int)( h * scale + 0.5f ) );
	const int	ox = x + ( side - dw ) / 2, oy = y + ( side - dh ) / 2;

	for ( int j = 0; j < dh; j++ ) {
		for ( int i = 0; i < dw; i++ ) {
			float	sum[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			byte	*out = sheet + ( ( oy + j ) * sheetSize + ox + i ) * 4;

			if ( scale < 1.0f ) {
				const int x0 = (int)( i / scale ), x1 = Q_max( x0 + 1, Q_min( w, (int)( ( i + 1 ) / scale ) ) );
				const int y0 = (int)( j / scale ), y1 = Q_max( y0 + 1, Q_min( h, (int)( ( j + 1 ) / scale ) ) );

				for ( int v = y0; v < y1; v++ ) {
					for ( int u = x0; u < x1; u++ ) {
						const byte *p = src + ( v * w + u ) * 4;

						sum[0] += p[0] * p[3];
						sum[1] += p[1] * p[3];
						sum[2] += p[2] * p[3];
						sum[3] += p[3];
					}
				}
				sum[3] /= (float)( ( x1 - x0 ) * ( y1 - y0 ) );
				for ( int c = 0; c < 3; c++ ) {
					sum[c] /= (float)( ( x1 - x0 ) * ( y1 - y0 ) );
				}
			} else {
				const float	fx = Com_Clamp( 0.0f, (float)( w - 1 ), ( i + 0.5f ) / scale - 0.5f );
				const float	fy = Com_Clamp( 0.0f, (float)( h - 1 ), ( j + 0.5f ) / scale - 0.5f );
				const int	u = Q_min( (int)fx, w - 2 < 0 ? 0 : w - 2 ), v = Q_min( (int)fy, h - 2 < 0 ? 0 : h - 2 );
				const float	a = fx - u, b = fy - v;

				for ( int k = 0; k < 4; k++ ) {
					const int	uu = Q_min( u + ( k & 1 ), w - 1 ), vv = Q_min( v + ( k >> 1 ), h - 1 );
					const byte	*p = src + ( vv * w + uu ) * 4;
					const float	weight = ( ( k & 1 ) ? a : 1.0f - a ) * ( ( k >> 1 ) ? b : 1.0f - b );

					sum[0] += p[0] * p[3] * weight;
					sum[1] += p[1] * p[3] * weight;
					sum[2] += p[2] * p[3] * weight;
					sum[3] += p[3] * weight;
				}
			}
			for ( int c = 0; c < 3; c++ ) {
				out[c] = (byte)Com_Clamp( 0.0f, 255.0f, sum[3] > 0.0f ? sum[c] / sum[3] : 0.0f );
			}
			out[3] = (byte)Com_Clamp( 0.0f, 255.0f, sum[3] );
		}
	}
}

/*
=================
Spray_Bleed

The colour of what is seen runs out under the transparent pixels of each cell, one pixel a pass, so that the
smaller levels of the mip map do not mix the black of nothing into the edge
=================
*/
static void Spray_Bleed( byte *sheet, int size, int cell ) {
	static byte	filled[PAINT_SHEET * PAINT_SHEET], next[PAINT_SHEET * PAINT_SHEET];

	for ( int i = 0; i < size * size; i++ ) {
		filled[i] = (byte)( sheet[i * 4 + 3] > 8 );
	}
	for ( int pass = 0; pass < SPRAY_BLEED; pass++ ) {
		memcpy( next, filled, (size_t)size * size );
		for ( int y = 0; y < size; y++ ) {
			for ( int x = 0; x < size; x++ ) {
				int	sum[3] = { 0, 0, 0 }, count = 0;

				if ( filled[y * size + x] ) {
					continue;
				}
				for ( int k = 0; k < 4; k++ ) {
					const int u = x + ( k == 0 ) - ( k == 1 ), v = y + ( k == 2 ) - ( k == 3 );

					// (not across into the next cell)
					if ( u < 0 || v < 0 || u >= size || v >= size || u / cell != x / cell || v / cell != y / cell || !filled[v * size + u] ) {
						continue;
					}
					for ( int c = 0; c < 3; c++ ) {
						sum[c] += sheet[( v * size + u ) * 4 + c];
					}
					count++;
				}
				if ( count ) {
					for ( int c = 0; c < 3; c++ ) {
						sheet[( y * size + x ) * 4 + c] = (byte)( sum[c] / count );
					}
					next[y * size + x] = 1;
				}
			}
		}
		memcpy( filled, next, (size_t)size * size );
	}
}

// the chunk SPRAY_CHUNK of a PNG in memory, qfalse: it has none or it is broken
static qboolean Spray_ReadChunk( const byte *data, int len, paintPicture_t *pic, int *width, int *height ) {
	static const byte	magic[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
	qboolean			found = qfalse;
	int					at = 8;

	*width = *height = 0;
	if ( len < 8 || memcmp( data, magic, 8 ) ) {
		return qfalse;
	}
	while ( at + 12 <= len ) {
		const unsigned	size = ( (unsigned)data[at] << 24 ) | ( data[at + 1] << 16 ) | ( data[at + 2] << 8 ) | data[at + 3];
		const byte		*body = data + at + 8;

		if ( size > (unsigned)( len - at - 12 ) ) {
			return qfalse;
		}
		if ( !memcmp( data + at + 4, "IHDR", 4 ) && size == 13 ) {
			*width = ( body[0] << 24 ) | ( body[1] << 16 ) | ( body[2] << 8 ) | body[3];
			*height = ( body[4] << 24 ) | ( body[5] << 16 ) | ( body[6] << 8 ) | body[7];
		} else if ( !memcmp( data + at + 4, SPRAY_CHUNK, 4 ) && size >= 5 && body[0] == 1 ) {
			pic->frames = body[1];
			pic->columns = body[2];
			pic->cell = ( body[3] << 8 ) | body[4];
			if ( pic->frames < 1 || pic->frames > PAINT_FRAMES || pic->columns < 1 || pic->cell < 16 || size != 5u + 2u * pic->frames ) {
				return qfalse;
			}
			for ( int i = 0; i < pic->frames; i++ ) {
				pic->delays[i] = Com_Clampi( SPRAY_DELAY_LEAST, SPRAY_DELAY_MOST, ( body[5 + i * 2] << 8 ) | body[6 + i * 2] );
			}
			found = qtrue;
		} else if ( !memcmp( data + at + 4, "IEND", 4 ) ) {
			break;
		}
		at += 12 + (int)size;
	}
	// a square sheet of a power of two, and every frame in it
	pic->sheet = *width;
	return (qboolean)( found && *width == *height && *width >= 64 && *width <= PAINT_SHEET && !( *width & ( *width - 1 ) )
		&& pic->columns * pic->cell <= *width && ( ( pic->frames + pic->columns - 1 ) / pic->columns ) * pic->cell <= *height );
}

// whether a file that came from the store is a sheet of a spray (cl_models.cpp)
qboolean CL_SprayFileGood( const byte *data, int len ) {
	paintPicture_t	pic;
	int				width, height;

	return Spray_ReadChunk( data, len, &pic, &width, &height );
}

/*
=================
Spray_FromGif

The frames of an animated gif: it is gone through twice, frame by frame, first to count them, then to take up to
PAINT_FRAMES of them, spread evenly over it, each shown as long as the frames it stands for.
Only one picture of it is kept at a time (and the two before it, which a frame may go back to).
=================
*/
static qboolean Spray_FromGif( const byte *data, int len, paintPicture_t *pic ) {
	int		total = 0, taken = 0;

	for ( int pass = 0; pass < 2; pass++ ) {
		stbi__context	s;
		stbi__gif		*g = (stbi__gif *)Z_Malloc( sizeof( stbi__gif ), TAG_TEMP_WORKSPACE, qtrue );
		stbi_uc			*two = NULL, *one = NULL, *frame;
		int				comp, n = 0;

		stbi__start_mem( &s, data, len );
		if ( !stbi__gif_test( &s ) ) {
			Z_Free( g );
			return qfalse;
		}
		while ( ( frame = stbi__gif_load_next( &s, g, &comp, 4, two ) ) != NULL && frame != (stbi_uc *)&s ) {
			const int delay = g->delay > 0 ? g->delay : SPRAY_DELAY_GIF;
			const size_t bytes = (size_t)g->w * g->h * 4;

			if ( pass == 0 ) {
				total++;
			} else if ( taken < pic->frames && n == (int)( (long long)taken * total / pic->frames ) ) {
				int x, y;

				Spray_Cell( pic, taken, &x, &y );
				Spray_Fit( frame, g->w, g->h, spraySheet, PAINT_SHEET, x + pic->cell / SPRAY_MARGIN, y + pic->cell / SPRAY_MARGIN,
					pic->cell - 2 * ( pic->cell / SPRAY_MARGIN ) );
				pic->delays[taken] = 0;
				taken++;
			}
			if ( pass == 1 && taken > 0 ) {
				pic->delays[taken - 1] += delay;
			}
			n++;
			// the picture before the one before goes, the frame is copied: the decoder writes the next one into it
			if ( !two ) {
				two = (stbi_uc *)Z_Malloc( (int)bytes, TAG_TEMP_WORKSPACE, qfalse );
				one = (stbi_uc *)Z_Malloc( (int)bytes, TAG_TEMP_WORKSPACE, qfalse );
				memcpy( two, frame, bytes );
			} else {
				memcpy( two, one, bytes );
			}
			memcpy( one, frame, bytes );
		}
		STBI_FREE( g->out );
		STBI_FREE( g->history );
		STBI_FREE( g->background );
		Z_Free( g );
		if ( two ) {
			Z_Free( two );
			Z_Free( one );
		}
		if ( pass == 0 ) {
			if ( !total ) {
				return qfalse;
			}
			memset( spraySheet, 0, sizeof( spraySheet ) );
			Spray_Layout( pic, Q_min( total, PAINT_FRAMES ) );
		}
	}
	for ( int i = 0; i < pic->frames; i++ ) {
		pic->delays[i] = Com_Clampi( SPRAY_DELAY_LEAST, SPRAY_DELAY_MOST, pic->delays[i] );
	}
	return (qboolean)( taken == pic->frames );
}

/*
=================
CL_SprayMake

The sheet of a picture into spraySheet, its frames into pic: a gif by its frames, a sheet of the store by its chunk,
any other picture file of the game still. qfalse: there is no such picture.
=================
*/
static qboolean CL_SprayMake( const char *name, paintPicture_t *pic ) {
	void	*data;
	int		len, width, height;

	memset( pic, 0, sizeof( *pic ) );
	if ( !re || !re->ext.LoadImageFit || !name || !name[0] ) {
		return qfalse;
	}
	len = FS_ReadFile( name, &data );
	if ( len <= 0 ) {
		return qfalse;
	}
	if ( !Q_stricmp( COM_GetExtension( name ), "gif" ) ) {
		const qboolean ok = (qboolean)( len <= SPRAY_GIF_MOST && Spray_FromGif( (const byte *)data, len, pic ) );

		FS_FreeFile( data );
		if ( ok ) {
			Spray_Bleed( spraySheet, PAINT_SHEET, pic->cell );
		}
		return ok;
	}
	if ( Spray_ReadChunk( (const byte *)data, len, pic, &width, &height ) ) {
		FS_FreeFile( data );
		return re->ext.LoadImageFit( name, spraySheet, pic->sheet, pic->sheet );
	}
	FS_FreeFile( data );

	// a still picture: fitted into the sheet with the margin
	{
		static byte	still[PAINT_SHEET * PAINT_SHEET * 4];
		const int	side = PAINT_SHEET - 2 * ( PAINT_SHEET / SPRAY_MARGIN );

		if ( !re->ext.LoadImageFit( name, still, side, side ) ) {
			return qfalse;
		}
		Spray_Layout( pic, 1 );
		pic->delays[0] = SPRAY_DELAY_MOST;
		memset( spraySheet, 0, sizeof( spraySheet ) );
		Spray_Fit( still, side, side, spraySheet, PAINT_SHEET, PAINT_SHEET / SPRAY_MARGIN, PAINT_SHEET / SPRAY_MARGIN, side );
		Spray_Bleed( spraySheet, PAINT_SHEET, pic->cell );
	}
	return qtrue;
}

// ext.R_PaintPicture of the cgame: the sheet of a picture into a slot of the renderer. The shader for marks with it,
// 0 when there is no such picture or the renderer has no slots.
qhandle_t CL_PaintPicture( int slot, const char *name, paintPicture_t *out ) {
	paintPicture_t pic;

	if ( !re || !re->ext.PaintImage || slot < 0 || slot >= PAINT_SLOTS || !CL_SprayMake( name, &pic ) ) {
		return 0;
	}
	if ( out ) {
		*out = pic;
	}
	return re->ext.PaintImage( slot, spraySheet, pic.sheet );
}

/*
=================
Spray_Png

A sheet of side x side RGBA as a PNG with the chunk of its frames into out. Each row takes the filter of PNG that
leaves the least to pack (the sum of the bytes as signed, the rule of libpng). 0: it is larger than outSize.
=================
*/
static void Spray_Chunk( byte *out, int *at, const char *kind, const byte *body, int len ) {
	uLong crc = crc32( 0L, (const Bytef *)kind, 4 );

	out[( *at )++] = (byte)( len >> 24 );
	out[( *at )++] = (byte)( len >> 16 );
	out[( *at )++] = (byte)( len >> 8 );
	out[( *at )++] = (byte)len;
	memcpy( out + *at, kind, 4 );
	*at += 4;
	if ( len ) {
		memcpy( out + *at, body, (size_t)len );
		crc = crc32( crc, body, (uInt)len );
		*at += len;
	}
	out[( *at )++] = (byte)( crc >> 24 );
	out[( *at )++] = (byte)( crc >> 16 );
	out[( *at )++] = (byte)( crc >> 8 );
	out[( *at )++] = (byte)crc;
}

static int Spray_Png( const byte *rgba, int side, const paintPicture_t *pic, byte *out, int outSize ) {
	static byte		rows[PAINT_SHEET * ( PAINT_SHEET * 4 + 1 )];
	static byte		packed[SPRAY_UPLOAD_MOST];
	static const byte magic[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
	const int		stride = side * 4;
	byte			header[13], frames[5 + 2 * PAINT_FRAMES];
	uLongf			packedLen = sizeof( packed );
	int				at = 0;

	for ( int y = 0; y < side; y++ ) {
		const byte	*line = rgba + y * stride, *above = y ? line - stride : NULL;
		byte		*row = rows + y * ( stride + 1 );
		long		best = -1;

		for ( int filter = 0; filter < 5; filter++ ) {
			static byte	trial[PAINT_SHEET * 4];
			long		cost = 0;

			for ( int i = 0; i < stride; i++ ) {
				const int	a = i >= 4 ? line[i - 4] : 0, b = above ? above[i] : 0, c = ( above && i >= 4 ) ? above[i - 4] : 0;
				int			predict = 0;

				switch ( filter ) {
				case 1: predict = a; break;
				case 2: predict = b; break;
				case 3: predict = ( a + b ) / 2; break;
				case 4: {
					const int p = a + b - c, pa = abs( p - a ), pb = abs( p - b ), pc = abs( p - c );
					predict = ( pa <= pb && pa <= pc ) ? a : pb <= pc ? b : c;
					break;
				}
				default: break;
				}
				trial[i] = (byte)( line[i] - predict );
				cost += abs( (signed char)trial[i] );
			}
			if ( best < 0 || cost < best ) {
				best = cost;
				row[0] = (byte)filter;
				memcpy( row + 1, trial, (size_t)stride );
			}
		}
	}
	if ( compress2( packed, &packedLen, rows, (uLong)side * ( stride + 1 ), 9 ) != Z_OK ) {
		return 0;
	}
	if ( 8 + 25 + 12 + 5 + 2 * pic->frames + 12 + (int)packedLen + 12 > outSize ) {
		return 0;
	}
	memcpy( out, magic, 8 );
	at = 8;
	header[0] = (byte)( side >> 24 ); header[1] = (byte)( side >> 16 ); header[2] = (byte)( side >> 8 ); header[3] = (byte)side;
	memcpy( header + 4, header, 4 );
	header[8] = 8;			// bits a channel
	header[9] = 6;			// RGBA
	header[10] = header[11] = header[12] = 0;
	Spray_Chunk( out, &at, "IHDR", header, 13 );
	frames[0] = 1;
	frames[1] = (byte)pic->frames;
	frames[2] = (byte)pic->columns;
	frames[3] = (byte)( pic->cell >> 8 );
	frames[4] = (byte)pic->cell;
	for ( int i = 0; i < pic->frames; i++ ) {
		frames[5 + i * 2] = (byte)( pic->delays[i] >> 8 );
		frames[6 + i * 2] = (byte)pic->delays[i];
	}
	Spray_Chunk( out, &at, SPRAY_CHUNK, frames, 5 + 2 * pic->frames );
	Spray_Chunk( out, &at, "IDAT", packed, (int)packedLen );
	Spray_Chunk( out, &at, "IEND", NULL, 0 );
	return at;
}

/*
=================
CL_SprayUploadMake

v_sprays upload <file>: the sheet of a picture of the player, in the PNG the store takes, waits in sprayUpload for
cl_models.cpp. A sheet that packs to more than the store takes is halved until it fits. qfalse: there is no such
picture.
=================
*/
qboolean CL_SprayUploadMake( const char *name ) {
	static byte		half[( PAINT_SHEET / 2 ) * ( PAINT_SHEET / 2 ) * 4];
	paintPicture_t	pic;

	sprayUploadLen = 0;
	if ( !CL_SprayMake( name, &pic ) ) {
		Com_Printf( S_COLOR_YELLOW "There is no picture %s for a spray\n", name );
		return qfalse;
	}
	while ( !( sprayUploadLen = Spray_Png( spraySheet, pic.sheet, &pic, sprayUpload, sizeof( sprayUpload ) ) ) && pic.sheet > 64 ) {
		// each pixel of the half the mean of four, weighed by their alpha
		const int side = pic.sheet / 2;

		Spray_Fit( spraySheet, pic.sheet, pic.sheet, half, side, 0, 0, side );
		memcpy( spraySheet, half, (size_t)side * side * 4 );
		pic.sheet = side;
		pic.cell /= 2;
	}
	if ( !sprayUploadLen ) {
		Com_Printf( S_COLOR_YELLOW "The picture %s is too large for a spray\n", name );
		return qfalse;
	}
	return qtrue;
}

// the sheet CL_SprayUploadMake made, for the upload of cl_models.cpp. 0: there is none.
int CL_SprayUploadBody( byte *out, int outSize ) {
	if ( sprayUploadLen <= 0 || sprayUploadLen > outSize ) {
		return 0;
	}
	memcpy( out, sprayUpload, (size_t)sprayUploadLen );
	return sprayUploadLen;
}
