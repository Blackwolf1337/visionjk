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

// cg_board.c -- the drawing board, a toy of issue #9 that only players of this tree see: v_board (Controls, Extras)
// opens a window where the player paints a picture of BOARD_W x BOARD_H with the mouse in BOARD_COLORS colours
// (brushes, fill, undo, clear, four presets in drawings/ of the home folder, a picture file of drawings/ brought in
// or one pasted from the clipboard of the system), and holds it up in front of him or over his head for the others
// to see, his arms posed to hold it.
//
// A picture brought in or pasted (Paste, Ctrl+V, v_board paste: the engine writes it to drawings/clipboard,
// client/cl_clipboard.cpp) is placed first, as in a paint program: it lies over the board in its colours and can be
// dragged, a corner scales it (its sides kept, Shift lets them go), the wheel scales it about the pointer, Fit and
// Fill lay it inside or over the whole board, Enter or Done puts it on, Esc or Cancel takes it away. The colours of
// the board are found in OKLab, a space where distances are what the eye sees (Ottosson 2020), with the error of each
// pixel spread to the ones after it in rows that turn at each end (Floyd-Steinberg, serpentine), only over the
// picture, so the rest of the board stays as it was.
//
// The picture is shown through a stream of the engine (ext.Video_Picture, client/cl_video.cpp, "videoMap *tv<n>"):
// the own board in BOARD_STREAM, the boards of the nearest others in the ones after it. The others hear of a board
// as TOY_BOARD (game/toys/bg_toys.h, the way a snowball goes, toys/cg_toys.c): that it is held and how, and the number
// of the picture in the store of the site, from where their engine fetches it (client/cl_models.cpp). Until it has
// a number the board is white for them. Not from JA+, see docs/japlus/toys.md.

#include "cgame/cg_local.h"
#include "cgame/toys/cg_toys.h"
#include "cgame/cg_window.h"
#include "ui/ui_shared.h"
#include "ghoul2/G2.h"

#define BOARD_STREAM		4			// the own board; the others take BOARD_STREAM + 1 on (VIDEO_STREAMS of cg_public.h)
#define BOARD_OTHERS		( VIDEO_STREAMS - BOARD_STREAM - 1 )
#define BOARD_PRESETS		4
#define BOARD_SEND_MSEC		3000		// a held board is said again this often, for who came later
#define BOARD_LOST_MSEC		8000		// a board not heard of for this long is put away
#define BOARD_SHOW_MSEC		50			// the own picture goes to the engine this often at most while it is painted
#define BOARD_UPLOAD_MSEC	8000		// a held picture that changed goes to the store this often at most
#define BOARD_WORLD_W		32.0f		// the board in the level, units
#define BOARD_WORLD_H		16.0f
#define BOARD_FRAME			1.2f
#define BOARD_WIN_W			560.0f
#define BOARD_WIN_H			400.0f
#define BOARD_SCALE			2.0f		// a pixel of the picture is this many of the 2D screen in the window
#define BOARD_IMPORTS		64			// picture files of drawings/ listed to bring in
#define BOARD_IMPORT_ROWS	10
#define BOARD_POSE_BLEND	250			// msec the arms take to their pose

typedef enum {
	BOARD_PART_CLOSE,
	BOARD_PART_CANVAS,
	BOARD_PART_COLOR,
	BOARD_PART_SIZE,
	BOARD_PART_FILL,
	BOARD_PART_UNDO,
	BOARD_PART_CLEAR,
	BOARD_PART_HOLD,			// arg: BOARD_HOLD_*
	BOARD_PART_SAVE,			// arg: the preset
	BOARD_PART_LOAD,
	BOARD_PART_IMPORT,
	BOARD_PART_PICK,			// arg: the file in the list
	BOARD_PART_PAGE,			// arg: -1, 1
	BOARD_PART_PASTE,
	BOARD_PART_PLACE,			// the picture that is placed, or a corner of it (arg: 0 the picture, 1 to 4 the corners)
	BOARD_PART_DONE,
	BOARD_PART_CANCEL,
	BOARD_PART_FIT				// arg: 0 inside the board, 1 over all of it
} boardPart_t;

#define BOARD_PLACE_SIZE	512			// pixels of a picture that is placed, each way at most
#define BOARD_HANDLE		7.0f		// the corners to scale, units of the 2D screen
#define BOARD_NOTE_MSEC		4000

// how a board is held
#define BOARD_HOLD_NONE		0
#define BOARD_HOLD_FRONT	1
#define BOARD_HOLD_HIGH		2

// the pose of the arms for a way to hold: a frame of an animation of the humanoid (_humanoid/animation.cfg), the
// fraction of the way through it, on both upper arms and what hangs on them
static const struct {
	animNumber_t	anim;
	float			at;
} boardPoses[3] = {
	{ BOTH_STAND1, 0.0f },
	{ BOTH_FORCE_2HANDEDLIGHTNING_HOLD, 0.5f },
	{ TORSO_SURRENDER_START, 1.0f },
};
static const char *boardArms[2] = { "lhumerus", "rhumerus" };

static const byte boardPalette[BOARD_COLORS][3] = {
	{ 255, 255, 255 }, { 20, 20, 24 }, { 90, 90, 96 }, { 170, 170, 176 },
	{ 120, 20, 24 }, { 220, 40, 40 }, { 255, 120, 110 }, { 255, 190, 200 },
	{ 230, 110, 20 }, { 255, 170, 40 }, { 255, 225, 60 }, { 255, 245, 160 },
	{ 30, 90, 30 }, { 60, 170, 60 }, { 140, 220, 90 }, { 200, 245, 170 },
	{ 20, 110, 120 }, { 40, 190, 200 }, { 140, 230, 240 }, { 20, 40, 110 },
	{ 40, 90, 220 }, { 110, 160, 255 }, { 70, 30, 120 }, { 140, 70, 210 },
	{ 200, 140, 255 }, { 170, 30, 120 }, { 240, 80, 180 }, { 100, 60, 30 },
	{ 160, 110, 60 }, { 220, 180, 130 }, { 250, 230, 200 }, { 255, 215, 0 },
};

typedef struct {
	int			holding;			// BOARD_HOLD_*
	int			picture;			// the number of the picture in the store, 0: none (white)
	int			heard;
	int			stream;				// the stream it is shown in, -1: none
	int			shown;				// the picture shown in that stream,
	qboolean	uploaded;			//	once it went there
	int			lookTime;			// when its picture was last looked for in the home folder
} boardOther_t;

static struct {
	qboolean	open;
	float		x, y;
	winHits_t	hits;
	byte		pixels[BOARD_H][BOARD_W];		// the palette index of every pixel
	byte		undo[BOARD_H][BOARD_W];
	byte		rgba[BOARD_H * BOARD_W * 4];
	int			color, size;
	qboolean	fill;				// the next click fills
	qboolean	painting;
	float		lastX, lastY;		// where the brush was, in pixels of the picture
	qboolean	dirty;
	int			shownTime;
	qboolean	unpublished;		// changed since it went to the store
	int			publishTime;
	int			holding;
	int			picture;			// the number of the own picture in the store, 0: none yet
	int			sendTime;
	boardOther_t others[MAX_CLIENTS];
	qhandle_t	shaders[VIDEO_STREAMS], frameShader;
	qboolean	importing;			// the list of the files to bring in is shown
	char		imports[BOARD_IMPORTS][MAX_QPATH];
	int			numImports, importPage;
	struct {
		int		frame;				// the frame the arms of the player are frozen in, -1: their own
		void	*ghoul2;			//	on this model
	} poses[MAX_CLIENTS];
	struct {
		qboolean	active;
		byte		src[BOARD_PLACE_SIZE * BOARD_PLACE_SIZE * 4];
		float		sx, sy, sw, sh;			// the part of src the picture covers
		float		x, y, w, h;				// where it lies on the board, its pixels
		int			drag;					// 0 none, 1 the picture, 2 to 5 a corner
		float		grabX, grabY;			// where the pointer took it, pixels of the board
		float		startX, startY, startW, startH;
		qboolean	changed;
		byte		preview[BOARD_H][BOARD_W];
	} place;
	char		note[128];			// a line over the picture for a while, BOARD_NOTE_MSEC
	int			noteTime;
	qboolean	pasting;			// the engine looks at the clipboard
} board;

void CG_BoardInit( void ) {
	int i;

	memset( &board, 0, sizeof( board ) );
	board.color = 1;
	board.size = 2;
	board.dirty = qtrue;
	board.unpublished = qtrue;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		board.others[i].stream = -1;
		board.poses[i].frame = -1;
	}
}

static void Board_Register( void ) {
	int i;

	if ( board.frameShader )
		return;
	for ( i = BOARD_STREAM; i < VIDEO_STREAMS; i++ )
		board.shaders[i] = trap->R_RegisterShader( va( "gfx/vision/board/%i", i ) );
	board.frameShader = trap->R_RegisterShader( "gfx/vision/board/frame" );
}

static qboolean Board_HasVideo( void ) {
	char value[8];

	trap->Cvar_VariableStringBuffer( "v_cgameVideo", value, sizeof( value ) );
	return (qboolean)( atoi( value ) == 1 );
}

// a picture as palette indices into RGBA
static void Board_ToRGBA( const byte pixels[BOARD_H][BOARD_W], byte *out ) {
	int x, y;

	for ( y = 0; y < BOARD_H; y++ ) {
		for ( x = 0; x < BOARD_W; x++, out += 4 ) {
			const byte *c = boardPalette[pixels[y][x] % BOARD_COLORS];

			out[0] = c[0];
			out[1] = c[1];
			out[2] = c[2];
			out[3] = 255;
		}
	}
}

// the own picture to the engine, now or soon
static void Board_Show( qboolean now ) {
	if ( !now && cg.time - board.shownTime < BOARD_SHOW_MSEC && cg.time >= board.shownTime )
		return;
	if ( !Board_HasVideo() )
		return;
	Board_ToRGBA( (const byte ( * )[BOARD_W])board.pixels, board.rgba );
	trap->ext.Video_Picture( BOARD_STREAM, board.rgba, BOARD_W, BOARD_H );
	board.shownTime = cg.time;
	board.dirty = qfalse;
}

static void Board_Send( void ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	toyThrow_t			toy;
	int					i;

	toy.kind = TOY_BOARD;
	for ( i = 0; i < 3; i++ )
		toy.origin[i] = (int)ps->origin[i];
	toy.velocity[0] = board.picture / TOY_BOARD_SPLIT;
	toy.velocity[1] = board.picture % TOY_BOARD_SPLIT;
	toy.velocity[2] = board.holding;
	if ( BG_ToyValid( &toy ) )
		CG_ToySend( &toy );
	board.sendTime = cg.time ? cg.time : 1;
}

// a brush of the size at a pixel
static void Board_Dab( int cx, int cy ) {
	const int r = board.size - 1;
	int x, y;

	for ( y = cy - r; y <= cy + r; y++ ) {
		for ( x = cx - r; x <= cx + r; x++ ) {
			if ( x < 0 || y < 0 || x >= BOARD_W || y >= BOARD_H || ( x - cx ) * ( x - cx ) + ( y - cy ) * ( y - cy ) > r * r + r )
				continue;
			board.pixels[y][x] = (byte)board.color;
		}
	}
	board.dirty = qtrue;
}

// the brush along a line of the picture
static void Board_Stroke( float x0, float y0, float x1, float y1 ) {
	const float	length = Q_max( fabsf( x1 - x0 ), fabsf( y1 - y0 ) );
	const int	steps = Q_max( 1, (int)ceilf( length ) );
	int			i;

	for ( i = 0; i <= steps; i++ ) {
		const float t = (float)i / steps;

		Board_Dab( (int)( x0 + ( x1 - x0 ) * t ), (int)( y0 + ( y1 - y0 ) * t ) );
	}
}

// what has the colour of the pixel and touches it gets the colour of the brush (a stack of spans, no recursion)
static void Board_Fill( int sx, int sy ) {
	static short	stack[BOARD_W * BOARD_H][2];
	const byte		from = board.pixels[sy][sx], to = (byte)board.color;
	int				top = 0;

	if ( from == to )
		return;
	stack[top][0] = (short)sx;
	stack[top++][1] = (short)sy;
	while ( top > 0 ) {
		int x, y, left;

		top--;
		x = stack[top][0];
		y = stack[top][1];
		if ( board.pixels[y][x] != from )
			continue;
		for ( left = x; left > 0 && board.pixels[y][left - 1] == from; left-- )
			;
		for ( x = left; x < BOARD_W && board.pixels[y][x] == from; x++ ) {
			board.pixels[y][x] = to;
			if ( y > 0 && board.pixels[y - 1][x] == from && top < BOARD_W * BOARD_H ) {
				stack[top][0] = (short)x;
				stack[top++][1] = (short)( y - 1 );
			}
			if ( y < BOARD_H - 1 && board.pixels[y + 1][x] == from && top < BOARD_W * BOARD_H ) {
				stack[top][0] = (short)x;
				stack[top++][1] = (short)( y + 1 );
			}
		}
	}
	board.dirty = qtrue;
}

static void Board_KeepUndo( void ) {
	memcpy( board.undo, board.pixels, sizeof( board.undo ) );
}

// a colour of 0 to 255 in sRGB as OKLab (Ottosson 2020): L, a, b
static void Board_OkLab( float r, float g, float b, float *out ) {
	float	c[3] = { r / 255.0f, g / 255.0f, b / 255.0f }, l, m, s;
	int		k;

	for ( k = 0; k < 3; k++ )
		c[k] = c[k] <= 0.04045f ? c[k] / 12.92f : powf( ( c[k] + 0.055f ) / 1.055f, 2.4f );
	l = cbrtf( 0.4122214708f * c[0] + 0.5363325363f * c[1] + 0.0514459929f * c[2] );
	m = cbrtf( 0.2119034982f * c[0] + 0.6806995451f * c[1] + 0.1073969566f * c[2] );
	s = cbrtf( 0.0883024619f * c[0] + 0.2817188376f * c[1] + 0.6299787005f * c[2] );
	out[0] = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
	out[1] = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
	out[2] = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
}

// the colour of the palette nearest to one in OKLab
static int Board_Nearest( const float *lab ) {
	static float	palette[BOARD_COLORS][3];
	static qboolean	made;
	float			bestDist = 1e9f;
	int				i, best = 0;

	if ( !made ) {
		for ( i = 0; i < BOARD_COLORS; i++ )
			Board_OkLab( boardPalette[i][0], boardPalette[i][1], boardPalette[i][2], palette[i] );
		made = qtrue;
	}
	for ( i = 0; i < BOARD_COLORS; i++ ) {
		const float	dl = lab[0] - palette[i][0], da = lab[1] - palette[i][1], db = lab[2] - palette[i][2];
		const float	dist = dl * dl + da * da + db * db;

		if ( dist < bestDist ) {
			bestDist = dist;
			best = i;
		}
	}
	return best;
}

/*
=================
Board_Quantize

A picture in RGBA into the palette where mask is set (all of it with mask NULL), out keeps what it has elsewhere.
What is clear is laid over white. The error of each pixel in OKLab goes to the ones after it (7/16 on, 3/16 behind
below, 5/16 below, 1/16 on below), the rows run back and forth so it does not drift to one side.
=================
*/
static void Board_Quantize( const byte *rgba, const byte mask[BOARD_H][BOARD_W], byte out[BOARD_H][BOARD_W] ) {
	static float	err[2][BOARD_W + 2][3];		// a pixel at x is at x + 1
	static float	palette[BOARD_COLORS][3];
	static qboolean	made;
	int				x, y, k, i;

	if ( !made ) {
		for ( i = 0; i < BOARD_COLORS; i++ )
			Board_OkLab( boardPalette[i][0], boardPalette[i][1], boardPalette[i][2], palette[i] );
		made = qtrue;
	}
	memset( err, 0, sizeof( err ) );
	for ( y = 0; y < BOARD_H; y++ ) {
		float		( *cur )[3] = err[y & 1], ( *next )[3] = err[( y + 1 ) & 1];
		const int	step = ( y & 1 ) ? -1 : 1;

		memset( next, 0, sizeof( err[0] ) );
		for ( x = ( y & 1 ) ? BOARD_W - 1 : 0; x >= 0 && x < BOARD_W; x += step ) {
			const byte	*in = rgba + ( y * BOARD_W + x ) * 4;
			float		lab[3];

			if ( mask && !mask[y][x] )
				continue;
			Board_OkLab( ( in[0] * in[3] + 255.0f * ( 255 - in[3] ) ) / 255.0f, ( in[1] * in[3] + 255.0f * ( 255 - in[3] ) ) / 255.0f,
				( in[2] * in[3] + 255.0f * ( 255 - in[3] ) ) / 255.0f, lab );
			for ( k = 0; k < 3; k++ )
				lab[k] += cur[x + 1][k];
			i = Board_Nearest( lab );
			out[y][x] = (byte)i;
			for ( k = 0; k < 3; k++ ) {
				// (a large error, of a colour far from all of the palette, is not spread whole, it would streak)
				const float e = Com_Clamp( -0.12f, 0.12f, lab[k] - palette[i][k] );

				cur[x + 1 + step][k] += e * ( 7.0f / 16.0f );
				next[x + 1 - step][k] += e * ( 3.0f / 16.0f );
				next[x + 1][k] += e * ( 5.0f / 16.0f );
				next[x + 1 + step][k] += e * ( 1.0f / 16.0f );
			}
		}
	}
}

// the picture files of drawings/ (in the home folder or the game folder) that can be brought in
static void Board_ListImports( void ) {
	static const char	*types[] = { ".png", ".jpg", ".jpeg", ".tga" };
	char				list[8192];
	int					t, i, count;

	board.numImports = 0;
	board.importPage = 0;
	for ( t = 0; t < (int)ARRAY_LEN( types ); t++ ) {
		const char *name = list;

		count = trap->FS_GetFileList( "drawings", types[t], list, sizeof( list ) );
		for ( i = 0; i < count && board.numImports < BOARD_IMPORTS; i++, name += strlen( name ) + 1 ) {
			if ( name[0] && strlen( name ) < MAX_QPATH && !strchr( name, '/' ) && !strchr( name, '\\' ) )
				Q_strncpyz( board.imports[board.numImports++], name, MAX_QPATH );
		}
	}
}

static void Board_Note( const char *text ) {
	Q_strncpyz( board.note, text, sizeof( board.note ) );
	board.noteTime = cg.time;
}

// the picture that is placed laid inside the board (fill 0) or over all of it (fill 1), its sides kept, in the middle
static void Board_PlaceFit( int fill ) {
	const float	board_ = (float)BOARD_W / BOARD_H, pic = board.place.sw / board.place.sh;

	if ( ( pic > board_ ) != ( fill != 0 ) ) {
		board.place.w = BOARD_W;
		board.place.h = BOARD_W / pic;
	} else {
		board.place.h = BOARD_H;
		board.place.w = BOARD_H * pic;
	}
	board.place.x = ( BOARD_W - board.place.w ) * 0.5f;
	board.place.y = ( BOARD_H - board.place.h ) * 0.5f;
	board.place.changed = qtrue;
}

// the board with the placed picture over it, in its colours, into place.preview
static void Board_PlaceCompose( void ) {
	static byte	rgba[BOARD_H * BOARD_W * 4];
	static byte	mask[BOARD_H][BOARD_W];
	int			x, y, k;

	memcpy( board.place.preview, board.pixels, sizeof( board.place.preview ) );
	memset( mask, 0, sizeof( mask ) );
	for ( y = 0; y < BOARD_H; y++ ) {
		for ( x = 0; x < BOARD_W; x++ ) {
			const byte	*base = boardPalette[board.pixels[y][x] % BOARD_COLORS];
			byte		*out = rgba + ( y * BOARD_W + x ) * 4;
			const float	u = ( x + 0.5f - board.place.x ) / board.place.w, v = ( y + 0.5f - board.place.y ) / board.place.h;
			float		fx, fy, a, b, c[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			int			ix, iy;

			out[3] = 255;
			if ( u < 0.0f || v < 0.0f || u >= 1.0f || v >= 1.0f )
				continue;
			// the picture there, the four nearest of its pixels mixed by their alpha
			fx = Com_Clamp( 0.0f, BOARD_PLACE_SIZE - 1.001f, board.place.sx + u * board.place.sw - 0.5f );
			fy = Com_Clamp( 0.0f, BOARD_PLACE_SIZE - 1.001f, board.place.sy + v * board.place.sh - 0.5f );
			ix = (int)fx;
			iy = (int)fy;
			a = fx - ix;
			b = fy - iy;
			for ( k = 0; k < 4; k++ ) {
				const byte	*p = board.place.src + ( ( iy + ( k >> 1 ) ) * BOARD_PLACE_SIZE + ix + ( k & 1 ) ) * 4;
				const float	w = ( ( k & 1 ) ? a : 1.0f - a ) * ( ( k >> 1 ) ? b : 1.0f - b ) * p[3] / 255.0f;

				c[0] += p[0] * w;
				c[1] += p[1] * w;
				c[2] += p[2] * w;
				c[3] += w;
			}
			if ( c[3] < 0.03f )
				continue;
			// over what the board has there
			for ( k = 0; k < 3; k++ )
				out[k] = (byte)Com_Clamp( 0.0f, 255.0f, c[k] / c[3] * c[3] + base[k] * ( 1.0f - c[3] ) );
			mask[y][x] = 1;
		}
	}
	Board_Quantize( rgba, (const byte ( * )[BOARD_W])mask, board.place.preview );
	board.place.changed = qfalse;
	if ( Board_HasVideo() ) {
		Board_ToRGBA( (const byte ( * )[BOARD_W])board.place.preview, board.rgba );
		trap->ext.Video_Picture( BOARD_STREAM, board.rgba, BOARD_W, BOARD_H );
	}
}

// the placed picture goes onto the board (Undo takes it back), or is taken away
static void Board_PlaceEnd( qboolean keep ) {
	if ( !board.place.active )
		return;
	if ( keep ) {
		if ( board.place.changed )
			Board_PlaceCompose();
		Board_KeepUndo();
		memcpy( board.pixels, board.place.preview, sizeof( board.pixels ) );
	}
	board.place.active = qfalse;
	board.place.drag = 0;
	Board_Show( qtrue );
	if ( keep )
		CG_BoardPublish();
}

/*
=================
Board_Import

A picture file onto the board (ext.R_LoadImageFit, the engine loads it): of drawings/, or the one the engine made
of the clipboard. With the window open it is placed first, without (a bind) it goes on inside the board at once.
=================
*/
static void Board_Import( const char *file ) {
	char	value[8];
	int		x, y, x0 = BOARD_PLACE_SIZE, y0 = BOARD_PLACE_SIZE, x1 = -1, y1 = -1;

	trap->Cvar_VariableStringBuffer( "v_cgameImage", value, sizeof( value ) );
	if ( atoi( value ) != 1 )
		return;
	if ( !trap->ext.R_LoadImageFit( file, board.place.src, BOARD_PLACE_SIZE, BOARD_PLACE_SIZE ) ) {
		Com_Printf( "No picture %s\n", file );
		return;
	}
	// the part of it that is not clear
	for ( y = 0; y < BOARD_PLACE_SIZE; y++ ) {
		for ( x = 0; x < BOARD_PLACE_SIZE; x++ ) {
			if ( board.place.src[( y * BOARD_PLACE_SIZE + x ) * 4 + 3] ) {
				x0 = Q_min( x0, x );
				y0 = Q_min( y0, y );
				x1 = Q_max( x1, x );
				y1 = Q_max( y1, y );
			}
		}
	}
	if ( x1 < 0 ) {
		Board_Note( "That picture is empty" );
		return;
	}
	board.place.sx = (float)x0;
	board.place.sy = (float)y0;
	board.place.sw = (float)( x1 - x0 + 1 );
	board.place.sh = (float)( y1 - y0 + 1 );
	board.place.active = qtrue;
	board.place.drag = 0;
	board.importing = qfalse;
	Board_PlaceFit( 0 );
	if ( !board.open )
		Board_PlaceEnd( qtrue );
}

// a picture file of drawings/
static void Board_ImportFile( const char *name ) {
	if ( !name[0] || strchr( name, '/' ) || strchr( name, '\\' ) || strstr( name, ".." ) )
		return;
	Board_Import( va( "drawings/%s", name ) );
}

// the engine is asked for the picture on the clipboard, it answers with v_boardPasted
static void Board_Paste( void ) {
	if ( board.pasting )
		return;
	board.pasting = qtrue;
	Board_Note( "Looking at the clipboard" );
	trap->SendConsoleCommand( "v_clipboardImage drawings/clipboard v_boardPasted\n" );
}

// v_boardPasted <file>: the engine wrote the picture of the clipboard there, or found none ("")
void CG_BoardPasted_f( void ) {
	const char *file = CG_Argv( 1 );

	board.pasting = qfalse;
	if ( !file[0] ) {
		Board_Note( "The clipboard has no picture" );
		return;
	}
	board.note[0] = '\0';
	Board_Import( file );
}

// drawings/preset<n>.vjd of the home folder: "VJD1" and a byte a pixel
static void Board_Save( int preset ) {
	fileHandle_t f;

	trap->FS_Open( va( "drawings/preset%i.vjd", preset + 1 ), &f, FS_WRITE );
	if ( !f )
		return;
	trap->FS_Write( "VJD1", 4, f );
	trap->FS_Write( board.pixels, sizeof( board.pixels ), f );
	trap->FS_Close( f );
	Com_Printf( "The drawing is kept as preset %i\n", preset + 1 );
}

static void Board_Load( int preset ) {
	static byte		data[4 + BOARD_W * BOARD_H];
	fileHandle_t	f;
	int				len, x, y;

	len = trap->FS_Open( va( "drawings/preset%i.vjd", preset + 1 ), &f, FS_READ );
	if ( !f )
		return;
	if ( len != (int)sizeof( data ) ) {
		trap->FS_Close( f );
		return;
	}
	trap->FS_Read( data, sizeof( data ), f );
	trap->FS_Close( f );
	if ( memcmp( data, "VJD1", 4 ) )
		return;
	Board_KeepUndo();
	for ( y = 0; y < BOARD_H; y++ ) {
		for ( x = 0; x < BOARD_W; x++ )
			board.pixels[y][x] = data[4 + y * BOARD_W + x] % BOARD_COLORS;
	}
	board.dirty = qtrue;
	Board_Show( qtrue );
	CG_BoardPublish();
}

// the picture changed: it goes to the store when it is held (Board_Upload)
void CG_BoardPublish( void ) {
	board.unpublished = qtrue;
}

/*
=================
Board_Upload

A held picture that changed goes to the store of the site (client/cl_models.cpp, v_drawings upload), at most every
BOARD_UPLOAD_MSEC, whose number the board then says to the others. Until it comes they see the one before.
=================
*/
static void Board_Upload( void ) {
	static byte		data[4 + BOARD_W * BOARD_H];
	fileHandle_t	f;

	board.unpublished = qfalse;
	board.publishTime = cg.time ? cg.time : 1;
	memcpy( data, "VJD1", 4 );
	memcpy( data + 4, board.pixels, BOARD_W * BOARD_H );
	trap->FS_Open( "drawings/upload.vjd", &f, FS_WRITE );
	if ( !f )
		return;
	trap->FS_Write( data, sizeof( data ), f );
	trap->FS_Close( f );
	trap->SendConsoleCommand( "v_drawings upload\n" );
}

// v_boardPicture <number>: the engine says the number the store gave the own picture
void CG_BoardPicture_f( void ) {
	board.picture = Com_Clampi( 0, TOY_BOARD_SPLIT * TOY_BOARD_SPLIT - 1, atoi( CG_Argv( 1 ) ) );
	if ( board.holding )
		Board_Send();
}

static void Board_Hold( int how ) {
	if ( how == board.holding )
		return;
	board.holding = how;
	if ( how != BOARD_HOLD_NONE )
		Board_Show( qtrue );
	Board_Send();
}

void CG_BoardOpen( void ) {
	board.open = qtrue;
	board.x = ( SCREEN_WIDTH - BOARD_WIN_W ) * 0.5f;
	board.y = ( SCREEN_HEIGHT - BOARD_WIN_H ) * 0.5f;
	board.painting = qfalse;
	board.importing = qfalse;
	Board_Show( qtrue );
}

void CG_BoardClose( void ) {
	Board_PlaceEnd( qfalse );
	if ( board.open && board.painting ) {
		board.painting = qfalse;
		CG_BoardPublish();
	}
	board.open = qfalse;
}

// v_board: the window opens and closes. v_board hold <0 away, 1 in front, 2 over the head>, v_board save <1-4>,
// v_board load <1-4>, v_board import <file of drawings/>, v_board paste: the same without the window, for a bind
void CG_Board_f( void ) {
	char		what[16], arg[MAX_QPATH];
	int			n;

	// (CG_Argv has one buffer)
	Q_strncpyz( what, CG_Argv( 1 ), sizeof( what ) );
	Q_strncpyz( arg, CG_Argv( 2 ), sizeof( arg ) );
	n = atoi( arg );

	if ( !Q_stricmp( what, "hold" ) ) {
		if ( CG_ToyCanUse() || n == BOARD_HOLD_NONE )
			Board_Hold( Com_Clampi( BOARD_HOLD_NONE, BOARD_HOLD_HIGH, n ) );
	} else if ( !Q_stricmp( what, "save" ) && n >= 1 && n <= BOARD_PRESETS ) {
		Board_Save( n - 1 );
	} else if ( !Q_stricmp( what, "load" ) && n >= 1 && n <= BOARD_PRESETS ) {
		Board_Load( n - 1 );
	} else if ( !Q_stricmp( what, "import" ) && arg[0] ) {
		Board_ImportFile( arg );
	} else if ( !Q_stricmp( what, "paste" ) ) {
		Board_Paste();
	} else if ( what[0] ) {
		Com_Printf( "usage: v_board [hold <0-2> | save <1-%i> | load <1-%i> | import <file> | paste]\n", BOARD_PRESETS, BOARD_PRESETS );
	} else if ( board.open ) {
		CG_BoardClose();
	} else if ( CG_ToyCanUse() || !cg.snap ) {
		CG_BoardOpen();
	}
}

qboolean CG_BoardShown( void ) {
	return board.open;
}

qboolean CG_BoardPointerOver( void ) {
	return ( board.open && Win_Over( board.x, board.y, BOARD_WIN_W, BOARD_WIN_H ) ) ? qtrue : qfalse;
}

// where the pointer is on the picture, in its pixels
static void Board_PointerPixel( float *px, float *py ) {
	*px = ( cgs.cursorX - ( board.x + 12.0f ) ) / BOARD_SCALE;
	*py = ( cgs.cursorY - ( board.y + 34.0f ) ) / BOARD_SCALE;
}

// over the picture: the picture files of drawings/ to bring in, a page of them
static void Board_DrawImports( float cx, float cy ) {
	const float	w = BOARD_W * BOARD_SCALE, h = BOARD_H * BOARD_SCALE;
	const int	pages = ( board.numImports + BOARD_IMPORT_ROWS - 1 ) / BOARD_IMPORT_ROWS;
	int			i;

	Win_Panel( cx + 8.0f, cy + 8.0f, w - 16.0f, h - 16.0f, 4.0f, winFill, winEdge );
	Win_Hit( &board.hits, BOARD_PART_PICK, -1, cx, cy, w, h );
	if ( !board.numImports ) {
		char home[MAX_OSPATH], game[MAX_QPATH];

		trap->Cvar_VariableStringBuffer( "fs_homepath", home, sizeof( home ) );
		trap->Cvar_VariableStringBuffer( "fs_game", game, sizeof( game ) );
		Win_Text( cx + 20.0f, cy + 20.0f, 0.5f, winText, ITEM_ALIGN_LEFT, "Put a picture (png, jpg or tga) into this folder:", w - 40.0f );
		Win_Text( cx + 20.0f, cy + 42.0f, 0.5f, winDim, ITEM_ALIGN_LEFT, va( "%s/%s/drawings", home, game[0] ? game : "base" ), w - 40.0f );
		Win_Text( cx + 20.0f, cy + 64.0f, 0.5f, winText, ITEM_ALIGN_LEFT, "Then click Picture again.", w - 40.0f );
		return;
	}
	Win_Text( cx + 20.0f, cy + 14.0f, 0.5f, winDim, ITEM_ALIGN_LEFT, "Click a picture to put it on the board (Undo takes it back):", w - 40.0f );
	for ( i = 0; i < BOARD_IMPORT_ROWS; i++ ) {
		const int n = board.importPage * BOARD_IMPORT_ROWS + i;

		if ( n >= board.numImports )
			break;
		Win_Button( &board.hits, BOARD_PART_PICK, n, cx + 20.0f, cy + 36.0f + i * 20.0f, w - 40.0f, board.imports[n], winText, qtrue );
	}
	if ( pages > 1 ) {
		Win_Button( &board.hits, BOARD_PART_PAGE, -1, cx + w - 112.0f, cy + h - 34.0f, 40.0f, "<", winText, board.importPage > 0 );
		Win_Button( &board.hits, BOARD_PART_PAGE, 1, cx + w - 64.0f, cy + h - 34.0f, 40.0f, ">", winText, board.importPage < pages - 1 );
	}
}

/*
=================
Board_PlaceDrag

While the placed picture or a corner of it is dragged, once a frame: moved with the pointer, or scaled from the
corner across, its sides kept unless Shift is down
=================
*/
static void Board_PlaceDrag( void ) {
	float px, py;

	if ( !board.place.drag )
		return;
	Board_PointerPixel( &px, &py );
	if ( board.place.drag == 1 ) {
		board.place.x = board.place.startX + px - board.place.grabX;
		board.place.y = board.place.startY + py - board.place.grabY;
	} else {
		const int	corner = board.place.drag - 2;		// 0 upper left, 1 upper right, 2 lower right, 3 lower left
		const float	ax = ( corner == 1 || corner == 2 ) ? board.place.startX : board.place.startX + board.place.startW;
		const float	ay = ( corner >= 2 ) ? board.place.startY : board.place.startY + board.place.startH;
		float		w = Q_max( 4.0f, fabsf( px - ax ) ), h = Q_max( 4.0f, fabsf( py - ay ) );

		if ( !trap->Key_IsDown( A_SHIFT ) ) {
			const float scale = Q_max( w / board.place.startW, h / board.place.startH );

			w = board.place.startW * scale;
			h = board.place.startH * scale;
		}
		board.place.w = w;
		board.place.h = h;
		board.place.x = ( corner == 1 || corner == 2 ) ? ax : ax - w;
		board.place.y = ( corner >= 2 ) ? ay : ay - h;
	}
	board.place.changed = qtrue;
}

// the frame of the placed picture with its corners, and the buttons to put it on or away, over the board
static void Board_DrawPlace( float cx, float cy ) {
	const float	x = cx + board.place.x * BOARD_SCALE, y = cy + board.place.y * BOARD_SCALE;
	const float	w = board.place.w * BOARD_SCALE, h = board.place.h * BOARD_SCALE;
	const float	bw = BOARD_W * BOARD_SCALE, bh = BOARD_H * BOARD_SCALE;
	int			i;

	CG_DrawRect( x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f, 1.0f, winAccent );
	Win_Hit( &board.hits, BOARD_PART_PLACE, 0, cx, cy, bw, bh );
	for ( i = 0; i < 4; i++ ) {
		const float hx = ( i == 1 || i == 2 ) ? x + w : x, hy = ( i >= 2 ) ? y + h : y;

		CG_FillRect( hx - BOARD_HANDLE * 0.5f, hy - BOARD_HANDLE * 0.5f, BOARD_HANDLE, BOARD_HANDLE, winAccent );
		Win_Hit( &board.hits, BOARD_PART_PLACE, i + 1, hx - BOARD_HANDLE, hy - BOARD_HANDLE, BOARD_HANDLE * 2.0f, BOARD_HANDLE * 2.0f );
	}
	// under the board, where the colours are while it is painted
	Win_Text( cx, cy + bh + 12.0f, 0.5f, winText, ITEM_ALIGN_LEFT, "Drag to move, drag a corner to scale (Shift: free), wheel zooms", bw );
	Win_Text( cx, cy + bh + 33.0f, 0.45f, winDim, ITEM_ALIGN_LEFT, "Enter puts it on, Esc drops it", bw - 250.0f );
	Win_Button( &board.hits, BOARD_PART_FIT, 0, cx + bw - 246.0f, cy + bh + 31.0f, 52.0f, "Fit", winText, qtrue );
	Win_Button( &board.hits, BOARD_PART_FIT, 1, cx + bw - 190.0f, cy + bh + 31.0f, 52.0f, "Fill", winText, qtrue );
	Win_Button( &board.hits, BOARD_PART_CANCEL, 0, cx + bw - 134.0f, cy + bh + 31.0f, 60.0f, "Cancel", winText, qtrue );
	Win_Button( &board.hits, BOARD_PART_DONE, 0, cx + bw - 70.0f, cy + bh + 31.0f, 60.0f, "Done", winAccent, qtrue );
}

/*
=================
CG_BoardDraw

Once a frame while it is open, in the 2D of the HUD, before the pointer: the picture, the colours, the tools
=================
*/
void CG_BoardDraw( void ) {
	static const char *holdNames[3] = { "Put away", "Hold up", "Over the head" };
	const float			cx = board.x + 12.0f, cy = board.y + 34.0f;
	vec4_t				swatch;
	float				px, py, x, y;
	int					i;

	if ( !board.open )
		return;
	Board_Register();
	if ( board.painting ) {
		Board_PointerPixel( &px, &py );
		Board_Stroke( board.lastX, board.lastY, px, py );
		board.lastX = px;
		board.lastY = py;
	}
	if ( board.place.active ) {
		Board_PlaceDrag();
		if ( board.place.changed )
			Board_PlaceCompose();
	} else if ( board.dirty ) {
		Board_Show( qfalse );
	}
	board.hits.num = 0;
	Win_Panel( board.x, board.y, BOARD_WIN_W, BOARD_WIN_H, 6.0f, winFill, winEdge );
	Win_Text( board.x + 12.0f, board.y + 8.0f, 0.6f, winText, ITEM_ALIGN_LEFT, "Drawing board", 300.0f );
	Win_Button( &board.hits, BOARD_PART_CLOSE, 0, board.x + BOARD_WIN_W - 64.0f, board.y + 7.0f, 52.0f, "Close", winText, qtrue );
	// the picture itself, as the stream of the engine shows it
	CG_FillRect( cx - 1.0f, cy - 1.0f, BOARD_W * BOARD_SCALE + 2.0f, BOARD_H * BOARD_SCALE + 2.0f, winEdge );
	CG_DrawPic( cx, cy, BOARD_W * BOARD_SCALE, BOARD_H * BOARD_SCALE, board.shaders[BOARD_STREAM] );
	if ( board.place.active )
		Board_DrawPlace( cx, cy );
	else if ( board.importing )
		Board_DrawImports( cx, cy );
	else
		Win_Hit( &board.hits, BOARD_PART_CANVAS, 0, cx, cy, BOARD_W * BOARD_SCALE, BOARD_H * BOARD_SCALE );
	if ( board.note[0] && ( board.pasting || cg.time - board.noteTime < BOARD_NOTE_MSEC ) ) {
		Win_Panel( cx + 6.0f, cy + 6.0f, Win_Width( board.note, 0.5f ) + 20.0f, 24.0f, 4.0f, winFill, winEdge );
		Win_Text( cx + 16.0f, cy + 10.0f, 0.5f, winText, ITEM_ALIGN_LEFT, board.note, BOARD_W * BOARD_SCALE - 30.0f );
	}
	// the colours, two rows, and the tools at the right of them (while a picture is placed its buttons are there)
	y = cy + BOARD_H * BOARD_SCALE + 10.0f;
	for ( i = 0; i < BOARD_COLORS && !board.place.active; i++ ) {
		x = cx + ( i % 16 ) * 21.0f;
		VectorSet( swatch, boardPalette[i][0] / 255.0f, boardPalette[i][1] / 255.0f, boardPalette[i][2] / 255.0f );
		swatch[3] = 1.0f;
		if ( i == board.color )
			CG_FillRect( x - 2.0f, y + ( i / 16 ) * 21.0f - 2.0f, 22.0f, 22.0f, winAccent );
		CG_FillRect( x, y + ( i / 16 ) * 21.0f, 18.0f, 18.0f, swatch );
		Win_Hit( &board.hits, BOARD_PART_COLOR, i, x, y + ( i / 16 ) * 21.0f, 18.0f, 18.0f );
	}
	x = cx + 16 * 21.0f + 8.0f;
	if ( !board.place.active ) {
		for ( i = 1; i <= 4; i++ )
			Win_Button( &board.hits, BOARD_PART_SIZE, i, x + ( i - 1 ) * 31.0f, y, 28.0f, va( "%i", i ), ( i == board.size && !board.fill ) ? winAccent : winText, qtrue );
		Win_Button( &board.hits, BOARD_PART_IMPORT, 0, x + 126.0f, y, 52.0f, "Picture", board.importing ? winAccent : winText, qtrue );
		Win_Button( &board.hits, BOARD_PART_FILL, 0, x, y + 21.0f, 40.0f, "Fill", board.fill ? winAccent : winText, qtrue );
		Win_Button( &board.hits, BOARD_PART_UNDO, 0, x + 43.0f, y + 21.0f, 40.0f, "Undo", winText, qtrue );
		Win_Button( &board.hits, BOARD_PART_CLEAR, 0, x + 86.0f, y + 21.0f, 40.0f, "Clear", winRed, qtrue );
		Win_Button( &board.hits, BOARD_PART_PASTE, 0, x + 129.0f, y + 21.0f, 49.0f, "Paste", board.pasting ? winAccent : winText, qtrue );
	}
	// holding, the presets, clear
	y = board.y + BOARD_WIN_H - 26.0f;
	for ( i = 0; i < 3; i++ )
		Win_Button( &board.hits, BOARD_PART_HOLD, i, cx + i * 104.0f, y, 100.0f, holdNames[i], board.holding == i ? winAccent : winText, qtrue );
	for ( i = 0; i < BOARD_PRESETS; i++ ) {
		Win_Button( &board.hits, BOARD_PART_SAVE, i, cx + 318.0f + i * 44.0f, y - 21.0f, 41.0f, va( "Save %i", i + 1 ), winDim, qtrue );
		Win_Button( &board.hits, BOARD_PART_LOAD, i, cx + 318.0f + i * 44.0f, y, 41.0f, va( "Load %i", i + 1 ), winText, qtrue );
	}
}

qboolean CG_BoardKey( int key, qboolean down ) {
	int at;

	if ( !board.open )
		return qfalse;
	if ( key == A_ESCAPE ) {
		if ( down && board.place.active )
			Board_PlaceEnd( qfalse );
		else if ( down )
			CG_BoardClose();
		return qtrue;
	}
	if ( down && ( key == 'v' || key == 'V' ) && trap->Key_IsDown( A_CTRL ) ) {
		Board_Paste();
		return qtrue;
	}
	if ( board.place.active ) {
		if ( down && ( key == A_ENTER || key == A_KP_ENTER ) ) {
			Board_PlaceEnd( qtrue );
			return qtrue;
		}
		// the wheel scales it about the pointer
		if ( down && ( key == A_MWHEELUP || key == A_MWHEELDOWN ) ) {
			const float	scale = key == A_MWHEELUP ? 1.1f : 1.0f / 1.1f;
			float		px, py;

			Board_PointerPixel( &px, &py );
			if ( board.place.w * scale >= 4.0f && board.place.h * scale >= 4.0f ) {
				board.place.x = px - ( px - board.place.x ) * scale;
				board.place.y = py - ( py - board.place.y ) * scale;
				board.place.w *= scale;
				board.place.h *= scale;
				board.place.changed = qtrue;
			}
			return qtrue;
		}
		if ( key == A_MOUSE1 && !down ) {
			board.place.drag = 0;
			return qtrue;
		}
	}
	if ( key != A_MOUSE1 )
		return CG_BoardPointerOver();
	if ( !down ) {
		if ( board.painting ) {
			board.painting = qfalse;
			Board_Show( qtrue );
			CG_BoardPublish();
		}
		return qtrue;
	}
	if ( ( at = Win_HitAt( &board.hits ) ) < 0 )
		return CG_BoardPointerOver();
	switch ( board.hits.hit[at].part ) {
	case BOARD_PART_CLOSE:
		CG_BoardClose();
		break;
	case BOARD_PART_CANVAS:
		Board_KeepUndo();
		Board_PointerPixel( &board.lastX, &board.lastY );
		if ( board.fill ) {
			const int fx = Com_Clampi( 0, BOARD_W - 1, (int)board.lastX ), fy = Com_Clampi( 0, BOARD_H - 1, (int)board.lastY );

			Board_Fill( fx, fy );
			Board_Show( qtrue );
			CG_BoardPublish();
		} else {
			board.painting = qtrue;
			Board_Dab( (int)board.lastX, (int)board.lastY );
		}
		break;
	case BOARD_PART_COLOR:
		board.color = board.hits.hit[at].arg;
		break;
	case BOARD_PART_SIZE:
		board.size = board.hits.hit[at].arg;
		board.fill = qfalse;
		break;
	case BOARD_PART_FILL:
		board.fill = !board.fill;
		break;
	case BOARD_PART_UNDO: {
			static byte swap[BOARD_H][BOARD_W];

			memcpy( swap, board.pixels, sizeof( swap ) );
			memcpy( board.pixels, board.undo, sizeof( swap ) );
			memcpy( board.undo, swap, sizeof( swap ) );
			Board_Show( qtrue );
			CG_BoardPublish();
		}
		break;
	case BOARD_PART_CLEAR:
		Board_KeepUndo();
		memset( board.pixels, 0, sizeof( board.pixels ) );
		Board_Show( qtrue );
		CG_BoardPublish();
		break;
	case BOARD_PART_HOLD:
		Board_Hold( board.hits.hit[at].arg );
		break;
	case BOARD_PART_SAVE:
		Board_Save( board.hits.hit[at].arg );
		break;
	case BOARD_PART_LOAD:
		Board_Load( board.hits.hit[at].arg );
		break;
	case BOARD_PART_IMPORT:
		board.importing = !board.importing;
		if ( board.importing )
			Board_ListImports();
		break;
	case BOARD_PART_PICK:
		if ( board.hits.hit[at].arg >= 0 && board.hits.hit[at].arg < board.numImports ) {
			board.importing = qfalse;
			Board_ImportFile( board.imports[board.hits.hit[at].arg] );
		}
		break;
	case BOARD_PART_PAGE:
		board.importPage = Com_Clampi( 0, Q_max( 0, ( board.numImports - 1 ) / BOARD_IMPORT_ROWS ), board.importPage + board.hits.hit[at].arg );
		break;
	case BOARD_PART_PASTE:
		Board_Paste();
		break;
	case BOARD_PART_PLACE: {
			float px, py;

			Board_PointerPixel( &px, &py );
			// a corner, or inside the picture, takes it; a click beside it does nothing
			if ( !board.hits.hit[at].arg && ( px < board.place.x || py < board.place.y || px >= board.place.x + board.place.w
				|| py >= board.place.y + board.place.h ) ) {
				break;
			}
			board.place.drag = board.hits.hit[at].arg + 1;
			board.place.grabX = px;
			board.place.grabY = py;
			board.place.startX = board.place.x;
			board.place.startY = board.place.y;
			board.place.startW = board.place.w;
			board.place.startH = board.place.h;
		}
		break;
	case BOARD_PART_FIT:
		Board_PlaceFit( board.hits.hit[at].arg );
		break;
	case BOARD_PART_DONE:
		Board_PlaceEnd( qtrue );
		break;
	case BOARD_PART_CANCEL:
		Board_PlaceEnd( qfalse );
		break;
	}
	return qtrue;
}

// a board of somebody else, from the game module or the masterserver
void CG_BoardHeard( int client, const vec3_t velocity ) {
	boardOther_t *o = &board.others[client];

	o->holding = Com_Clampi( BOARD_HOLD_NONE, BOARD_HOLD_HIGH, (int)velocity[2] );
	o->picture = (int)velocity[0] * TOY_BOARD_SPLIT + (int)velocity[1];
	o->heard = cg.time;
}

/*
=================
Board_Pose

The arms of a player who holds a board up: a frame of an animation (boardPoses) frozen on both upper arms, until the
board is put away and they follow the body again. Not for a model that is no humanoid, nor for one who is dead.
=================
*/
static void Board_Pose( centity_t *cent, int holding ) {
	const int	num = cent->currentState.number;
	int			frame = -1, i;

	if ( holding != BOARD_HOLD_NONE && cent->ghoul2 && cent->localAnimIndex <= 1 && !( cent->currentState.eFlags & EF_DEAD ) ) {
		const animation_t *anim = &bgAllAnims[cent->localAnimIndex].anims[boardPoses[holding].anim];

		frame = anim->firstFrame + (int)( ( anim->numFrames - 1 ) * boardPoses[holding].at );
	}
	if ( board.poses[num].frame == frame && board.poses[num].ghoul2 == cent->ghoul2 )
		return;
	for ( i = 0; i < 2 && cent->ghoul2; i++ ) {
		if ( frame >= 0 ) {
			trap->G2API_SetBoneAnim( cent->ghoul2, 0, boardArms[i], frame, frame + 1, BONE_ANIM_OVERRIDE_FREEZE | BONE_ANIM_BLEND,
				1.0f, cg.time, (float)frame, BOARD_POSE_BLEND );
		} else if ( board.poses[num].frame >= 0 && board.poses[num].ghoul2 == cent->ghoul2 ) {
			// (as cg_players.c lets a broken arm go: its flags to 0 first, else the bone stays)
			trap->G2API_SetBoneAnim( cent->ghoul2, 0, boardArms[i], 0, 1, 0, 0, cg.time, -1, 0 );
			trap->G2API_RemoveBone( cent->ghoul2, boardArms[i], 0 );
		}
	}
	board.poses[num].frame = frame;
	board.poses[num].ghoul2 = cent->ghoul2;
}

// where the middle of a board is held: between the hands of a posed player, else in front of the chest or over
// the head
static void Board_Middle( centity_t *cent, int holding, const vec3_t forward, vec3_t mid ) {
	const clientInfo_t	*ci = &cgs.clientinfo[cent->currentState.number];
	mdxaBone_t			matrix;
	vec3_t				left, right;

	if ( board.poses[cent->currentState.number].frame >= 0 && ci->bolt_lhand >= 0 && ci->bolt_rhand >= 0
		&& trap->G2API_GetBoltMatrix( cent->ghoul2, 0, ci->bolt_lhand, &matrix, cent->turAngles, cent->lerpOrigin, cg.time,
			cgs.gameModels, cent->modelScale ) ) {
		BG_GiveMeVectorFromMatrix( &matrix, ORIGIN, left );
		if ( trap->G2API_GetBoltMatrix( cent->ghoul2, 0, ci->bolt_rhand, &matrix, cent->turAngles, cent->lerpOrigin, cg.time,
			cgs.gameModels, cent->modelScale ) ) {
			BG_GiveMeVectorFromMatrix( &matrix, ORIGIN, right );
			VectorAdd( left, right, mid );
			VectorScale( mid, 0.5f, mid );
			// (over the head the hands hold its lower edge, in front its upper edge, the board a little before them
			// so what they hold does not show through)
			VectorMA( mid, 4.0f, forward, mid );
			mid[2] += holding == BOARD_HOLD_HIGH ? BOARD_WORLD_H * 0.5f - 1.0f : -( BOARD_WORLD_H * 0.5f - 2.0f );
			return;
		}
	}
	VectorMA( cent->lerpOrigin, holding == BOARD_HOLD_HIGH ? 4.0f : 16.0f, forward, mid );
	mid[2] += holding == BOARD_HOLD_HIGH ? 58.0f : 26.0f;
}

// the board of a player where he holds it, its face to where he looks
static void Board_Draw( centity_t *cent, int holding, qhandle_t shader ) {
	vec3_t		forward, right, mid, angles;
	polyVert_t	front[4], back[4];
	const float	hw = BOARD_WORLD_W * 0.5f, hh = BOARD_WORLD_H * 0.5f;
	int			i;

	VectorSet( angles, 0.0f, cent->lerpAngles[YAW], 0.0f );
	AngleVectors( angles, forward, right, NULL );
	Board_Middle( cent, holding, forward, mid );
	for ( i = 0; i < 4; i++ ) {
		const float	sx = ( i == 0 || i == 3 ) ? -1.0f : 1.0f, sy = ( i < 2 ) ? 1.0f : -1.0f;
		vec3_t		p;

		// (seen from in front the picture reads from left to right: its left is the holder's right)
		VectorMA( mid, -sx * hw, right, p );
		p[2] += sy * hh;
		VectorCopy( p, front[i].xyz );
		front[i].st[0] = ( sx + 1.0f ) * 0.5f;
		front[i].st[1] = ( 1.0f - sy ) * 0.5f;
		front[i].modulate[0] = front[i].modulate[1] = front[i].modulate[2] = front[i].modulate[3] = 255;
		// the back and the frame, a little behind it and larger
		VectorMA( mid, -sx * ( hw + BOARD_FRAME ), right, p );
		p[2] += sy * ( hh + BOARD_FRAME );
		VectorMA( p, -0.4f, forward, p );
		VectorCopy( p, back[3 - i].xyz );
		back[3 - i].st[0] = back[3 - i].st[1] = 0.5f;
		back[3 - i].modulate[0] = back[3 - i].modulate[1] = back[3 - i].modulate[2] = back[3 - i].modulate[3] = 255;
	}
	trap->R_AddPolysToScene( shader, 4, front, 1 );
	trap->R_AddPolysToScene( board.frameShader, 4, back, 1 );
	// (the frame seen from in front too, round the picture)
	for ( i = 0; i < 4; i++ ) {
		VectorMA( back[i].xyz, -0.2f, forward, back[i].xyz );
	}
	{
		polyVert_t edge[4];

		for ( i = 0; i < 4; i++ )
			edge[i] = back[3 - i];
		trap->R_AddPolysToScene( board.frameShader, 4, edge, 1 );
	}
}

/*
=================
CG_BoardScene

While the scene is built: the own board where it is held, and the boards of the others near, each in a stream of
its own with its picture from the store
=================
*/
void CG_BoardScene( void ) {
	int i, k, used = 0;

	Board_Register();
	if ( cg.snap )
		Board_Pose( &cg_entities[cg.clientNum], board.holding );
	if ( board.holding && cg.snap && cg.renderingThirdPerson )
		Board_Draw( &cg_entities[cg.clientNum], board.holding, board.shaders[BOARD_STREAM] );
	if ( board.holding && board.unpublished && !board.painting
		&& ( !board.publishTime || cg.time - board.publishTime >= BOARD_UPLOAD_MSEC || cg.time < board.publishTime ) )
		Board_Upload();
	if ( board.holding && ( !board.sendTime || cg.time - board.sendTime >= BOARD_SEND_MSEC || cg.time < board.sendTime ) )
		Board_Send();
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		boardOther_t		*o = &board.others[i];
		centity_t			*cent = &cg_entities[i];

		if ( i == cg.clientNum )
			continue;
		if ( o->holding && ( cg.time - o->heard > BOARD_LOST_MSEC || !cgs.clientinfo[i].infoValid ) )
			o->holding = BOARD_HOLD_NONE;
		if ( !o->holding || !v_toys.integer || !cent->currentValid || used >= BOARD_OTHERS ) {
			Board_Pose( cent, BOARD_HOLD_NONE );
			o->stream = -1;
			o->uploaded = qfalse;
			continue;
		}
		k = BOARD_STREAM + 1 + used++;
		if ( o->stream != k ) {
			o->stream = k;
			o->uploaded = qfalse;
		}
		if ( ( !o->uploaded || o->shown != o->picture ) && ( cg.time - o->lookTime >= 500 || cg.time < o->lookTime ) ) {
			o->lookTime = cg.time;
			static byte rgba[BOARD_W * BOARD_H * 4];
			static byte pixels[BOARD_H][BOARD_W];

			if ( !o->picture || CG_BoardFetch( o->picture, (byte *)pixels ) ) {
				if ( !o->picture )
					memset( pixels, 0, sizeof( pixels ) );
				Board_ToRGBA( (const byte ( * )[BOARD_W])pixels, rgba );
				if ( Board_HasVideo() )
					trap->ext.Video_Picture( o->stream, rgba, BOARD_W, BOARD_H );
				o->shown = o->picture;
				o->uploaded = qtrue;
			}
		}
		Board_Pose( cent, o->holding );
		Board_Draw( cent, o->holding, board.shaders[o->stream] );
	}
}

/*
=================
CG_BoardFetch

The pixels of a picture of the store, as the engine fetched them into drawings/<number>.vjd of the home folder
(v_drawings get). qfalse: not here yet, it is asked for.
=================
*/
qboolean CG_BoardFetch( int picture, byte *pixels ) {
	static byte		data[4 + BOARD_W * BOARD_H];
	static int		asked[8], numAsked;
	fileHandle_t	f;
	int				len, i;

	len = trap->FS_Open( va( "drawings/%i.vjd", picture ), &f, FS_READ );
	if ( f ) {
		if ( len == (int)sizeof( data ) ) {
			trap->FS_Read( data, sizeof( data ), f );
			trap->FS_Close( f );
			if ( !memcmp( data, "VJD1", 4 ) ) {
				for ( i = 0; i < BOARD_W * BOARD_H; i++ )
					pixels[i] = data[4 + i] % BOARD_COLORS;
				return qtrue;
			}
			return qfalse;
		}
		trap->FS_Close( f );
	}
	for ( i = 0; i < numAsked; i++ ) {
		if ( asked[i] == picture )
			return qfalse;
	}
	asked[numAsked++ % ARRAY_LEN( asked )] = picture;
	numAsked = Q_min( numAsked, (int)ARRAY_LEN( asked ) );
	trap->SendConsoleCommand( va( "v_drawings get %i\n", picture ) );
	return qfalse;
}
