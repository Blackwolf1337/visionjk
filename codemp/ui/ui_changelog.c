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

// ui_changelog.c -- the window "What's new" (ui/jamp/changelog.menu): the versions at the left (FEEDER_CHANGELOG),
// what the chosen one brought at the right (the owner draw UI_CHANGELOG). Not from JA+, see docs/japlus/update.md.
//
// The text is the changelog of the website (tools/changelog.py there, ui_markdown.h here) in one of two files under
// base: visionjk/changelog.md came with the zip of this version, visionjk/changelog-fetched.md is the one of the
// update channel the engine fetched with the file list (client/cl_update.cpp, v_changelog counts those). The one
// with the newer first entry is shown, so a version that is offered can be read before it is fetched.
//
// The entry is laid out once for a width (word by word, a run of words in one style is one piece to draw) and
// moves by whole lines. A link is opened in the browser of the system by a second click on it.

#include "ui_local.h"
#include "ui_skin.h"
#include "ui_markdown.h"

#define LOG_SHIPPED			"visionjk/changelog.md"
#define LOG_FETCHED			"visionjk/changelog-fetched.md"
#define LOG_MAX_FILE		( 256 * 1024 )
#define LOG_MAX_ENTRIES		128
#define LOG_MAX_LINES		1024
#define LOG_MAX_PIECES		3072
#define LOG_MAX_POOL		( 64 * 1024 )
#define LOG_MAX_LINKS		64
#define LOG_MAX_WORD		256
#define LOG_PAD				10.0f		// inside the window
#define LOG_BAR				4.0f		// the scroll bar at its right
#define LOG_LINE			1.38f		// a line, as a part of the height of the letters
#define LOG_WHEEL			3			// lines for a notch of the wheel

typedef enum {
	LS_TEXT,
	LS_BOLD,
	LS_ITALIC,
	LS_CODE,
	LS_LINK,
	LS_TITLE,			// "Version 0.2.9"
	LS_HEADING,			// "## "
	LS_SUBHEADING,		// "### "
	LS_SUMMARY,			// the line under the title
	LS_DATE,
	LS_TAG,				// a pill: "Installed", "Not installed yet"
	LS_TAG_WARM,		// a pill: "Development build"
	LS_NUM
} logStyle_t;

typedef struct {
	int		font;			// a font of the new look
	float	scale;			// of the scale of the item
	int		color;			// LC_*
	int		classicColor;
} logLook_t;

enum { LC_TEXT, LC_BRIGHT, LC_DIM, LC_ACCENT, LC_WARM, LC_NUM };

static const vec4_t logColors[LC_NUM] = {
	{ 0.86f, 0.92f, 0.97f, 1.0f },		// TEXT of tools/menus/theme.py
	{ 1.0f, 1.0f, 1.0f, 1.0f },
	{ 0.60f, 0.69f, 0.78f, 1.0f },		// DIM
	{ 0.43f, 0.83f, 1.0f, 1.0f },		// the light blue of the look
	{ 1.0f, 0.74f, 0.30f, 1.0f }		// WARM
};
// the menus of the game: white, the gold of their titles, grey, a light blue
static const vec4_t logClassic[LC_NUM] = {
	{ 1.0f, 1.0f, 1.0f, 1.0f },
	{ 1.0f, 0.682f, 0.0f, 1.0f },
	{ 0.7f, 0.7f, 0.7f, 1.0f },
	{ 0.55f, 0.8f, 1.0f, 1.0f },
	{ 1.0f, 0.6f, 0.2f, 1.0f }
};

static const logLook_t logLooks[LS_NUM] = {
	{ SKIN_FONT_SANS, 1.0f, LC_TEXT, LC_TEXT },
	{ SKIN_FONT_BOLD, 1.0f, LC_BRIGHT, LC_BRIGHT },
	{ SKIN_FONT_SANS, 1.0f, LC_DIM, LC_DIM },
	{ SKIN_FONT_MONO, 0.9f, LC_TEXT, LC_ACCENT },
	{ SKIN_FONT_SANS, 1.0f, LC_ACCENT, LC_ACCENT },
	{ SKIN_FONT_SANS, 1.7f, LC_BRIGHT, LC_BRIGHT },
	{ SKIN_FONT_SANS, 1.3f, LC_BRIGHT, LC_BRIGHT },
	{ SKIN_FONT_BOLD, 1.08f, LC_BRIGHT, LC_BRIGHT },
	{ SKIN_FONT_SANS, 1.1f, LC_BRIGHT, LC_TEXT },
	{ SKIN_FONT_SANS, 0.9f, LC_DIM, LC_DIM },
	{ SKIN_FONT_SANS, 0.8f, LC_ACCENT, LC_ACCENT },
	{ SKIN_FONT_SANS, 0.8f, LC_WARM, LC_WARM }
};

typedef struct {
	float		x, w;
	int			start, len;			// in the pool
	logStyle_t	style;
	int			link;				// -1: none
} logPiece_t;

typedef struct {
	float		y, h;				// below the top of the entry
	int			first, count;		// of the pieces
	int			bullet;				// 1: a line of a list starts here, 2: one below one
	float		bulletX;
	qboolean	rule;				// a line under it (a heading)
} logLine_t;

static struct {
	char		files[2][LOG_MAX_FILE + 1];
	int			shown;				// the file the entries are in
	mdEntry_t	entries[LOG_MAX_ENTRIES];
	int			numEntries;
	int			selected;
	qboolean	loaded;
	int			fetchCount;			// v_changelog when the files were read
	int			own[4];				// the version of this build (v_version)
	qboolean	hasOwn;

	// the selected entry, laid out
	int			layoutEntry;
	float		layoutWidth, layoutScale;
	int			layoutFont;			// 0: the fonts of the new look, else the one of the item
	logLine_t	lines[LOG_MAX_LINES];
	int			numLines;
	logPiece_t	pieces[LOG_MAX_PIECES];
	int			numPieces;
	char		pool[LOG_MAX_POOL];
	int			poolLen;
	const char	*links[LOG_MAX_LINKS];
	int			linkLen[LOG_MAX_LINKS];
	int			numLinks;

	// while it is laid out
	float		x0, width, lineX, y;	// y: the top of the open line, or where the next one starts
	qboolean	open;				// the last line can still take words, and grow
	qboolean	space;				// a blank came before the next word
	int			bullet;
	float		bulletX;

	int			top;				// the first line in the window
	int			visible;			// lines that were in it
	rectDef_t	rect;				// where it was drawn last
	int			hoverLink, askLink;	// under the mouse; clicked once, the next click opens it
} logs;

/*
===============================================================================

The files

===============================================================================
*/

static int Log_ReadFile( const char *name, char *buffer ) {
	fileHandle_t	f;
	int				len = trap->FS_Open( name, &f, FS_READ );

	buffer[0] = '\0';
	if ( !f )
		return 0;
	if ( len > LOG_MAX_FILE )
		len = LOG_MAX_FILE;
	if ( len > 0 )
		trap->FS_Read( buffer, len, f );
	trap->FS_Close( f );
	if ( len < 0 )
		len = 0;
	buffer[len] = '\0';
	return len;
}

static qboolean Log_TopVersion( const char *text, int len, int out[4] ) {
	mdEntry_t first;

	return ( MD_ReadEntries( text, len, &first, 1 ) == 1 && MD_ParseVersion( first.version, out ) ) ? qtrue : qfalse;
}

static void Log_Load( void ) {
	int		lens[2], tops[2][4];
	char	own[MD_VERSION_LEN];

	logs.loaded = qtrue;
	logs.fetchCount = (int)trap->Cvar_VariableValue( "v_changelog" );
	trap->Cvar_VariableStringBuffer( "v_version", own, sizeof( own ) );
	logs.hasOwn = MD_ParseVersion( own, logs.own );

	lens[0] = Log_ReadFile( LOG_FETCHED, logs.files[0] );
	lens[1] = Log_ReadFile( LOG_SHIPPED, logs.files[1] );
	// the fetched one, unless the one of the zip goes further (no line to the website since an update)
	logs.shown = 0;
	if ( !Log_TopVersion( logs.files[0], lens[0], tops[0] )
		|| ( Log_TopVersion( logs.files[1], lens[1], tops[1] ) && MD_CompareVersion( tops[1], tops[0] ) > 0 ) )
		logs.shown = 1;
	logs.numEntries = MD_ReadEntries( logs.files[logs.shown], lens[logs.shown], logs.entries, LOG_MAX_ENTRIES );
	if ( logs.selected >= logs.numEntries )
		logs.selected = 0;
	logs.layoutEntry = -1;
}

// the files are read again when the engine fetched a newer one
static void Log_Fresh( void ) {
	if ( !logs.loaded || (int)trap->Cvar_VariableValue( "v_changelog" ) != logs.fetchCount )
		Log_Load();
}

int UI_ChangelogCount( void ) {
	Log_Fresh();
	return logs.numEntries;
}

// the newest version of the changelog, "" without one
const char *UI_ChangelogNewest( void ) {
	Log_Fresh();
	return logs.numEntries ? logs.entries[0].version : "";
}

static const char *Log_ShortDate( const char *date, qboolean year ) {
	static const char	*months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	int					y, m, d;

	if ( sscanf( date, "%d-%d-%d", &y, &m, &d ) != 3 || m < 1 || m > 12 )
		return date;
	return year ? va( "%s %d, %d", months[m - 1], d, y ) : va( "%s %d", months[m - 1], d );
}

// FEEDER_CHANGELOG: a row of the list of versions
const char *UI_ChangelogText( int index ) {
	static char	row[64];

	if ( index < 0 || index >= logs.numEntries )
		return "";
	Com_sprintf( row, sizeof( row ), "%s  ^9%s", logs.entries[index].version, Log_ShortDate( logs.entries[index].date, qfalse ) );
	return row;
}

void UI_ChangelogSelect( int index ) {
	if ( index < 0 || index >= logs.numEntries || index == logs.selected )
		return;
	logs.selected = index;
	logs.top = 0;
	logs.askLink = -1;
}

/*
=================
UI_ChangelogOpen

uiScript Changelog: the window opens. The files are read again, and the entry that is shown is the one of the
version the game was just updated to (v_updated), else the newest. Returns the row to select.
=================
*/
int UI_ChangelogOpen( void ) {
	char	updated[MD_VERSION_LEN];
	int		i;

	Log_Load();
	logs.selected = 0;
	trap->Cvar_VariableStringBuffer( "v_updated", updated, sizeof( updated ) );
	for ( i = 0; updated[0] && i < logs.numEntries; i++ ) {
		if ( !Q_stricmp( logs.entries[i].version, updated ) ) {
			logs.selected = i;
			break;
		}
	}
	trap->Cvar_Set( "v_updated", "" );
	logs.top = 0;
	logs.askLink = -1;
	logs.layoutEntry = -1;
	return logs.selected;
}

/*
===============================================================================

The layout

===============================================================================
*/

static int Log_Font( logStyle_t style ) {
	return logs.layoutFont ? logs.layoutFont : logLooks[style].font;
}

static float Log_Scale( logStyle_t style ) {
	// (the fonts of the game have no smaller code, and a bigger title is all they can do)
	if ( logs.layoutFont && style != LS_TITLE && style != LS_HEADING )
		return logs.layoutScale;
	return logs.layoutScale * logLooks[style].scale;
}

static float Log_Width( const char *text, logStyle_t style ) {
	const int font = MenuFontToHandle( Log_Font( style ) );

	if ( uiInfo.uiDC.ext.Font_StrLenPixels )
		return uiInfo.uiDC.ext.Font_StrLenPixels( text, font, Log_Scale( style ) );
	return (float)trap->R_Font_StrLenPixels( text, font, Log_Scale( style ) );
}

static float Log_Height( logStyle_t style ) {
	return 18.0f * Log_Scale( style );
}

static logLine_t *Log_Line( void ) {
	return logs.numLines ? &logs.lines[logs.numLines - 1] : NULL;
}

// the open line is done: the next one starts under it, as high as it grew
static void Log_Close( void ) {
	if ( logs.open && Log_Line() )
		logs.y = Log_Line()->y + Log_Line()->h;
	logs.open = qfalse;
}

static void Log_NewLine( logStyle_t style ) {
	logLine_t *line;

	Log_Close();
	if ( logs.numLines >= LOG_MAX_LINES )
		return;
	line = &logs.lines[logs.numLines++];
	memset( line, 0, sizeof( *line ) );
	line->y = logs.y;
	line->h = Log_Height( style ) * LOG_LINE;
	line->first = logs.numPieces;
	line->bullet = logs.bullet;
	line->bulletX = logs.bulletX;
	logs.bullet = 0;
	logs.lineX = logs.x0;
	logs.space = qfalse;
	logs.open = qtrue;
}

// a block starts: at x0 in from the left, over width
static void Log_Begin( float x0, float width, logStyle_t style ) {
	logs.x0 = x0;
	logs.width = width;
	Log_NewLine( style );
}

static void Log_Gap( float h ) {
	Log_Close();
	logs.y += h;
}

// One word, or a part of one that has another style than the part before (no blank between them). It goes on the
// line if it fits, else it starts the next one; a word wider than a line has one of its own.
static void Log_Word( const char *word, int len, logStyle_t style, int link ) {
	char		text[LOG_MAX_WORD];
	logLine_t	*line = Log_Line();
	logPiece_t	*piece;
	float		w, blank;
	qboolean	join;

	if ( !line || !logs.open || len <= 0 )
		return;
	Q_strncpyz( text, word, Q_min( len + 1, (int)sizeof( text ) ) );
	if ( style == LS_HEADING )
		Q_strupr( text );
	w = Log_Width( text, style );
	// (a pill has room around its word, see Log_DrawPiece)
	if ( style == LS_TAG || style == LS_TAG_WARM )
		w += 12.0f;
	blank = ( logs.space && line->count ) ? Log_Width( " ", style ) : 0.0f;
	if ( line->count && logs.lineX + blank + w > logs.x0 + logs.width ) {
		const float height = line->h;

		Log_NewLine( style );
		line = Log_Line();
		if ( !logs.open || line->first != logs.numPieces )
			return;
		line->h = Q_max( line->h, height );
		blank = 0.0f;
	}
	line->h = Q_max( line->h, Log_Height( style ) * LOG_LINE );

	// the same style as the piece before on this line: the word goes into it
	piece = line->count ? &logs.pieces[line->first + line->count - 1] : NULL;
	join = ( piece && piece->style == style && piece->link == link ) ? qtrue : qfalse;
	if ( logs.poolLen + len + 2 > LOG_MAX_POOL || ( !join && logs.numPieces >= LOG_MAX_PIECES ) )
		return;
	if ( join ) {
		if ( blank > 0.0f )
			logs.pool[logs.poolLen++] = ' ';
		piece->len += ( blank > 0.0f ? 1 : 0 ) + (int)strlen( text );
		piece->w += blank + w;
	} else {
		piece = &logs.pieces[logs.numPieces++];
		line->count++;
		piece->x = logs.lineX + blank;
		piece->w = w;
		piece->start = logs.poolLen;
		piece->len = (int)strlen( text );
		piece->style = style;
		piece->link = link;
	}
	memcpy( logs.pool + logs.poolLen, text, strlen( text ) );
	logs.poolLen += (int)strlen( text );
	logs.pool[logs.poolLen] = '\0';
	logs.lineX += blank + w;
	logs.space = qfalse;
}

// a run of text in one style: its words, the blanks and line breaks between them count as one blank
static void Log_Words( const char *text, int len, logStyle_t style, int link ) {
	const char *p = text, *end = text + len;

	while ( p < end ) {
		const char *w;

		if ( *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ) {
			logs.space = qtrue;
			p++;
			continue;
		}
		for ( w = p; w < end && *w != ' ' && *w != '\t' && *w != '\n' && *w != '\r'; w++ ) {
		}
		Log_Word( p, (int)( w - p ), style, link );
		p = w;
	}
}

// the text of a block, its spans in their styles; plain text in the style of the block
static void Log_Spans( const char *text, int len, logStyle_t plain ) {
	const char	*p = text, *end = text + len;
	mdSpan_t	span;

	while ( MD_NextSpan( &p, end, &span ) ) {
		int link = -1;

		if ( span.style == MD_LINK && logs.numLinks < LOG_MAX_LINKS ) {
			link = logs.numLinks++;
			logs.links[link] = span.url;
			logs.linkLen[link] = span.urlLen;
		}
		switch ( span.style ) {
		case MD_BOLD:	Log_Words( span.text, span.len, plain == LS_TEXT ? LS_BOLD : plain, -1 ); break;
		case MD_ITALIC:	Log_Words( span.text, span.len, plain == LS_TEXT ? LS_ITALIC : plain, -1 ); break;
		case MD_CODE:	Log_Words( span.text, span.len, LS_CODE, -1 ); break;
		case MD_LINK:	Log_Words( span.text, span.len, link >= 0 ? LS_LINK : plain, link ); break;
		default:		Log_Words( span.text, span.len, plain, -1 ); break;
		}
	}
}

static void Log_Layout( const mdEntry_t *entry, float width ) {
	const float	unit = Log_Height( LS_TEXT );
	const float	indent = unit * 1.1f;
	char		title[96], date[32], dev[64];
	int			v[4];
	mdReader_t	reader;
	mdBlock_t	block;

	// (va() and Vision_Text() hand out buffers that the next calls take again). The word in capitals as the titles
	// of the menus, the version as it is written.
	Q_strncpyz( title, Vision_Text( "Version" ), sizeof( title ) );
	Q_strupr( title );
	Q_strcat( title, sizeof( title ), va( " %s", entry->version ) );
	Q_strncpyz( date, Log_ShortDate( entry->date, qtrue ), sizeof( date ) );
	Q_strncpyz( dev, Vision_Text( "Development build" ), sizeof( dev ) );

	logs.numLines = logs.numPieces = logs.poolLen = logs.numLinks = 0;
	logs.y = 0.0f;
	logs.open = qfalse;
	logs.bullet = 0;

	Log_Begin( 0.0f, width, LS_TITLE );
	Log_Words( title, (int)strlen( title ), LS_TITLE, -1 );

	// the date and what this version is to the one that runs
	Log_Gap( unit * 0.2f );
	Log_Begin( 0.0f, width, LS_DATE );
	Log_Words( date, (int)strlen( date ), LS_DATE, -1 );
	if ( MD_IsDev( entry->version ) ) {
		logs.space = qtrue;
		Log_Word( dev, (int)strlen( dev ), LS_TAG_WARM, -1 );
	}
	if ( logs.hasOwn && MD_ParseVersion( entry->version, v ) ) {
		const int	newer = MD_CompareVersion( v, logs.own );
		const char	*tag = !newer ? Vision_Text( "Installed" ) : ( newer > 0 ? Vision_Text( "Not installed yet" ) : NULL );

		if ( tag ) {
			logs.space = qtrue;
			Log_Word( tag, (int)strlen( tag ), LS_TAG, -1 );
		}
	}

	Log_Gap( unit * 0.5f );
	Log_Begin( 0.0f, width, LS_SUMMARY );
	Log_Spans( entry->summary, (int)strlen( entry->summary ), LS_SUMMARY );
	Log_Gap( unit * 0.6f );

	MD_StartBlocks( &reader, entry->body, entry->bodyLen );
	while ( MD_NextBlock( &reader, &block ) ) {
		switch ( block.type ) {
		case MD_HEADING:
		case MD_SUBHEADING:
			Log_Gap( unit * 0.7f );
			Log_Begin( 0.0f, width, block.type == MD_HEADING ? LS_HEADING : LS_SUBHEADING );
			Log_Spans( block.text, block.len, block.type == MD_HEADING ? LS_HEADING : LS_SUBHEADING );
			if ( block.type == MD_HEADING && Log_Line() )
				Log_Line()->rule = qtrue;
			Log_Gap( unit * 0.3f );
			break;
		case MD_ITEM:
		case MD_SUBITEM: {
			const float x0 = ( block.type == MD_ITEM ) ? indent : indent * 2.0f;

			logs.bullet = ( block.type == MD_ITEM ) ? 1 : 2;
			logs.bulletX = x0 - indent * 0.6f;
			Log_Begin( x0, width - x0, LS_TEXT );
			Log_Spans( block.text, block.len, LS_TEXT );
			Log_Gap( unit * 0.3f );
			break;
		}
		default:
			Log_Gap( unit * 0.2f );
			Log_Begin( 0.0f, width, LS_TEXT );
			Log_Spans( block.text, block.len, LS_TEXT );
			Log_Gap( unit * 0.4f );
			break;
		}
	}
	Log_Close();
}

/*
===============================================================================

Drawing and keys

===============================================================================
*/

static const float *Log_Color( logStyle_t style ) {
	return logs.layoutFont ? logClassic[logLooks[style].classicColor] : logColors[logLooks[style].color];
}

// where a line ends, below the top of the entry (the gaps between blocks are in the tops of the lines)
static float Log_Bottom( int line ) {
	return logs.lines[line].y + logs.lines[line].h;
}

// the lines from the first one in the window, as many as fit into its height
static int Log_Fits( int top, float height ) {
	int n = 0;

	while ( top + n < logs.numLines && Log_Bottom( top + n ) - logs.lines[top].y <= height )
		n++;
	return Q_max( n, 1 );
}

// the first line that shows the end of the entry
static int Log_MaxTop( float height ) {
	int top = logs.numLines;

	while ( top > 0 && logs.numLines && Log_Bottom( logs.numLines - 1 ) - logs.lines[top - 1].y <= height )
		top--;
	return top;
}

static void Log_ScrollTo( int top ) {
	logs.top = Com_Clampi( 0, Log_MaxTop( logs.rect.h - 2.0f * LOG_PAD ), top );
}

static void Log_DrawPiece( const logPiece_t *piece, float x, float y, float h, qboolean newLook ) {
	const float	textH = Log_Height( piece->style );
	const float	top = y + ( h - textH * LOG_LINE ) + textH * ( LOG_LINE - 1.0f ) * 0.5f;
	vec4_t		color;
	char		text[1024];

	Q_strncpyz( text, logs.pool + piece->start, Q_min( piece->len + 1, (int)sizeof( text ) ) );
	VectorCopy4( Log_Color( piece->style ), color );
	if ( piece->style == LS_LINK && piece->link == logs.hoverLink )
		VectorCopy4( newLook ? logColors[LC_BRIGHT] : logClassic[LC_TEXT], color );

	if ( newLook && piece->style == LS_CODE ) {
		const vec4_t fill = { 0.0f, 0.0f, 0.0f, 0.35f }, edge = { 0.43f, 0.83f, 1.0f, 0.16f };

		Skin_Panel( x + piece->x - 2.0f, top - 1.0f, piece->w + 4.0f, textH + 2.0f, 2.5f, fill, edge );
	} else if ( newLook && ( piece->style == LS_TAG || piece->style == LS_TAG_WARM ) ) {
		vec4_t fill = { 0.035f, 0.07f, 0.12f, 0.86f }, edge;

		VectorCopy4( color, edge );
		edge[3] = 0.6f;
		Skin_Panel( x + piece->x, top - 1.5f, piece->w, textH + 3.0f, ( textH + 3.0f ) * 0.5f, fill, edge );
	}
	if ( piece->style == LS_TAG || piece->style == LS_TAG_WARM )
		x += 6.0f;
	uiInfo.uiDC.drawText( x + piece->x, top, Log_Scale( piece->style ), color, text, 0, 0, ITEM_TEXTSTYLE_NORMAL, Log_Font( piece->style ) );
	if ( piece->style == LS_LINK ) {
		color[3] *= 0.7f;
		uiInfo.uiDC.fillRect( x + piece->x, top + textH * 0.98f, piece->w, 0.6f, color );
	}
}

/*
=================
UI_ChangelogDraw

The owner draw UI_CHANGELOG: the entry that is selected, in the rectangle of the item.
=================
*/
void UI_ChangelogDraw( rectDef_t *rect, float scale, int iMenuFont ) {
	const qboolean	newLook = UI_NewLook();
	const float		width = rect->w - 2.0f * LOG_PAD - LOG_BAR - 4.0f;
	const float		height = rect->h - 2.0f * LOG_PAD;
	float			x, y;
	int				i, j, n;

	Log_Fresh();
	logs.rect = *rect;
	logs.hoverLink = -1;
	if ( !logs.numEntries ) {
		const char *none = Vision_Text( "The changelog is not there. It comes with the next update." );

		uiInfo.uiDC.drawText( rect->x + LOG_PAD, rect->y + LOG_PAD, scale, newLook ? (float *)logColors[LC_DIM] : (float *)logClassic[LC_DIM],
			none, 0, 0, ITEM_TEXTSTYLE_NORMAL, newLook ? SKIN_FONT_SANS : iMenuFont );
		return;
	}
	if ( logs.layoutEntry != logs.selected || logs.layoutWidth != width || logs.layoutScale != scale
		|| logs.layoutFont != ( newLook ? 0 : iMenuFont ) ) {
		logs.layoutEntry = logs.selected;
		logs.layoutWidth = width;
		logs.layoutScale = scale;
		logs.layoutFont = newLook ? 0 : iMenuFont;
		Log_Layout( &logs.entries[logs.selected], width );
		Log_ScrollTo( logs.top );
	}

	x = rect->x + LOG_PAD;
	y = rect->y + LOG_PAD;
	n = Log_Fits( logs.top, height );
	logs.visible = n;
	for ( i = logs.top; i < logs.top + n && i < logs.numLines; i++ ) {
		const logLine_t	*line = &logs.lines[i];
		const float		ly = y + line->y - logs.lines[logs.top].y;

		if ( line->bullet ) {
			const float		cy = ly + line->h * 0.5f;
			const float		r = Log_Height( LS_TEXT ) * ( line->bullet == 1 ? 0.13f : 0.1f );
			const float		*c = newLook ? logColors[line->bullet == 1 ? LC_ACCENT : LC_DIM] : logClassic[LC_BRIGHT];

			if ( newLook ) {
				Skin_Disc( x + line->bulletX, cy, r, c );
			} else {
				uiInfo.uiDC.fillRect( x + line->bulletX - r, cy - r, r * 2.0f, r * 2.0f, (float *)c );
			}
		}
		for ( j = 0; j < line->count; j++ ) {
			const logPiece_t *piece = &logs.pieces[line->first + j];

			if ( piece->link >= 0 && uiInfo.uiDC.cursorx >= x + piece->x && uiInfo.uiDC.cursorx <= x + piece->x + piece->w
				&& uiInfo.uiDC.cursory >= ly && uiInfo.uiDC.cursory <= ly + line->h )
				logs.hoverLink = piece->link;
		}
		for ( j = 0; j < line->count; j++ )
			Log_DrawPiece( &logs.pieces[line->first + j], x, ly, line->h, newLook );
		if ( line->rule ) {
			vec4_t accent;

			VectorCopy4( newLook ? logColors[LC_ACCENT] : logClassic[LC_BRIGHT], accent );
			accent[3] = 0.5f;
			uiInfo.uiDC.fillRect( x, ly + line->h - 1.0f, Q_min( width, 40.0f ), 1.0f, accent );
		}
	}

	// the bar at the right, when the entry is longer than the window
	if ( n < logs.numLines ) {
		const float	total = logs.lines[logs.numLines - 1].y + logs.lines[logs.numLines - 1].h;
		const float	start = logs.lines[logs.top].y / total;
		const float	part = Q_min( 1.0f, height / total );
		vec4_t		accent;

		VectorCopy4( newLook ? logColors[LC_ACCENT] : logClassic[LC_BRIGHT], accent );
		accent[3] = 0.65f;
		if ( newLook ) {
			Skin_ScrollBar( rect->x + rect->w - LOG_PAD - LOG_BAR, y, LOG_BAR, height, start, part, qfalse, accent );
		} else {
			uiInfo.uiDC.fillRect( rect->x + rect->w - LOG_PAD - LOG_BAR, y + height * start, LOG_BAR, height * part, accent );
		}
	}

	// a link that was clicked once: where it leads, and that a second click opens it
	if ( logs.askLink >= 0 && logs.askLink < logs.numLinks ) {
		char	url[160], line[256];
		vec4_t	back = { 0.02f, 0.04f, 0.08f, 0.92f };

		Q_strncpyz( url, logs.links[logs.askLink], Q_min( logs.linkLen[logs.askLink] + 1, (int)sizeof( url ) ) );
		Com_sprintf( line, sizeof( line ), "%s %s", Vision_Text( "Click the link again to open it in your browser:" ), url );
		uiInfo.uiDC.fillRect( rect->x + 1.0f, rect->y + rect->h - 20.0f, rect->w - 2.0f, 19.0f, back );
		uiInfo.uiDC.drawText( rect->x + LOG_PAD, rect->y + rect->h - 16.0f, scale * 0.85f, newLook ? (float *)logColors[LC_ACCENT] : (float *)logClassic[LC_ACCENT],
			line, 0, 0, ITEM_TEXTSTYLE_NORMAL, newLook ? SKIN_FONT_SANS : iMenuFont );
	}
}

static void Log_OpenLink( int link ) {
	char url[512];

	Q_strncpyz( url, logs.links[link], Q_min( logs.linkLen[link] + 1, (int)sizeof( url ) ) );
	// (only an engine that says so has the call, see ui_public.h)
	if ( trap->Cvar_VariableValue( "v_uiURL" ) == 1.0f && trap->ext.OpenURL && trap->ext.OpenURL( url ) )
		return;
	Com_Printf( "%s\n", url );
}

// the owner draw UI_CHANGELOG has the focus and a key went down
qboolean UI_ChangelogKey( int key ) {
	const int page = Q_max( 1, logs.visible - 1 );

	switch ( key ) {
	case A_MWHEELUP:	Log_ScrollTo( logs.top - LOG_WHEEL ); return qtrue;
	case A_MWHEELDOWN:	Log_ScrollTo( logs.top + LOG_WHEEL ); return qtrue;
	case A_PAGE_UP:		Log_ScrollTo( logs.top - page ); return qtrue;
	case A_PAGE_DOWN:	Log_ScrollTo( logs.top + page ); return qtrue;
	case A_HOME:		Log_ScrollTo( 0 ); return qtrue;
	case A_END:			Log_ScrollTo( logs.numLines ); return qtrue;
	case A_MOUSE1:
		if ( logs.hoverLink >= 0 ) {
			trap->S_StartLocalSound( uiInfo.uiDC.Assets.menuEnterSound, CHAN_LOCAL_SOUND );
			if ( logs.askLink == logs.hoverLink ) {
				Log_OpenLink( logs.hoverLink );
				logs.askLink = -1;
			} else {
				logs.askLink = logs.hoverLink;
			}
			return qtrue;
		}
		logs.askLink = -1;
		// a click on the bar at the right: there in the entry
		if ( uiInfo.uiDC.cursorx >= logs.rect.x + logs.rect.w - LOG_PAD - LOG_BAR - 3.0f && logs.numLines ) {
			const float f = ( uiInfo.uiDC.cursory - logs.rect.y - LOG_PAD ) / ( logs.rect.h - 2.0f * LOG_PAD );

			Log_ScrollTo( (int)( f * logs.numLines ) - logs.visible / 2 );
			return qtrue;
		}
		return qfalse;
	default:
		return qfalse;
	}
}
