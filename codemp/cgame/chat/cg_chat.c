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

// cg_chat.c -- the chat box. Not from JA+ and not from the base game, whose chat box (CG_ChatBox_* in cg_draw.c)
// it replaces.
//
// Every chat line is kept (CHAT_HISTORY of them), with who said it and what kind it is. While nobody types the
// newest lines stand at the lower left for cg_chatBox milliseconds, as they always did. With an engine of this
// tree (cvar v_cgameChat) and v_chatInput 1 the message mode keys open the box of this file instead of the
// line of the engine (the cgame says so with v_chatHandler, the engine then runs v_chatOpen): a frame with
// the history, three tabs (all, team, private), a bar that shows where in the history one is, and the line to
// type in. The cgame gets keys, characters and the mouse then (KEYCATCH_CGAME):
//
// - the wheel and Page Up / Page Down scroll, Tab goes to the next tab
// - holding the left button marks text, Ctrl+C or the right button copy it (without colours)
// - an address that starts with http:// or https:// is underlined; a click asks, with the address shown, and
//   Enter then hands it to the system (the engine checks it again and takes nothing else)
// - Up and Down bring back what was sent before, Ctrl+V pastes
//
// In any other engine the box only shows the lines, and typing stays with the line of the engine.
//
// With the HUD of the new look (CG_HudNewLook) the box has that look too: a panel of dark glass, the letters of
// the menus, the tabs as words with a line under the open one, and the lines that stand there while nobody
// types each on a dark strip, so they can be read on a bright floor.
//
// How large it is (2026-10-12): v_chatBoxScale is the size of the box with everything in it, the letters first
// of all; v_chatBoxWidth how wide it is at a size of 1; v_chatBoxRows how many rows of history the open box
// has. All three are in Setup. With the box open they are also set by hand: its upper right corner is dragged
// (width and rows), and Ctrl with the wheel changes the size.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"
#include "cgame/boombox/cg_boombox.h"

extern displayContextDef_t cgDC;

#define CHAT_HISTORY		256
#define CHAT_TEXT			320				// a line as it came: name, colours, text
#define CHAT_MAX_ROWS		8				// rows a line takes at most when it is wrapped
#define CHAT_SCALE			0.65f
#define CHAT_SKIN_SCALE		0.6f			// of the letters of the new look
#define CHAT_ROW_HEIGHT		13.0f			// this and the heights below at a size of 1 (v_chatBoxScale)
#define CHAT_MAX_OPEN_ROWS	24				// rows of history in the open box at most (v_chatBoxRows),
#define CHAT_MIN_OPEN_ROWS	4				//	and at least
#define CHAT_MIN_SCALE		0.6f			// v_chatBoxScale
#define CHAT_MAX_SCALE		1.6f
#define CHAT_GRIP			9.0f			// the corner of the open box that is dragged to make it larger
#define CHAT_CLOSED_ROWS	6				// rows that stand there while nobody types
#define CHAT_X				30.0f
#define CHAT_PAD			4.0f
#define CHAT_TAB_HEIGHT		14.0f
#define CHAT_INPUT_HEIGHT	15.0f
#define CHAT_BAR_WIDTH		4.0f
#define CHAT_FADE_MSEC		1000
#define CHAT_SENT			16				// lines one sent that Up brings back

typedef struct chatMsg_s {
	char		text[CHAT_TEXT];
	int			time;						// cg.time when it came
	chatKind_t	kind;
	int			clientNum;					// who said it, -1 when that is not known
	qboolean	mention;					// it names the own player or a word of tay_notifyWords
	// the rows it takes at wrapWidth: row n is text from rowStart[n] up to rowStart[n + 1], in the colour rowColor[n]
	short		rowStart[CHAT_MAX_ROWS + 1];
	char		rowColor[CHAT_MAX_ROWS];
	int			numRows;
	int			wrapWidth;
} chatMsg_t;

// a place in the history: a line, counted from the first one that ever came, and a byte of its text
typedef struct chatPos_s {
	int			msg;
	int			offset;
} chatPos_t;

typedef struct chatRow_s {
	int			msg;
	int			row;
	float		y;
} chatRow_t;

#define CHAT_SUGGEST		8			// emoji in the list of the word being typed
#define CHAT_EMOJI_WORD		40

static struct {
	chatMsg_t	history[CHAT_HISTORY];
	int			count;						// lines that ever came, line n is history[n % CHAT_HISTORY]

	qboolean	engine;						// the engine has the calls of the box (v_cgameChat)
	qboolean	skin;						// the box is drawn in the new look
	// how large it is this frame
	float		size;						// v_chatBoxScale
	float		rowH;						// the height of a row
	int			openRows;					// rows of history in the open box
	// its upper right corner is dragged with the mouse
	qboolean	resizing;
	float		grabX, grabY, grabWidth;	// where the pointer was when the button went down, and how wide the box
	int			grabRows;					//	and how many rows
	float		gripX, gripY;				// where that corner is, from the last draw
	qboolean	utf8;						// text is UTF-8 (v_utf8Text)
	int			handlerCount;				// v_chatInput as it was when v_chatHandler was last set

	qboolean	open;
	int			mode;						// 0 to all, 1 to the team, 2 to target
	int			target;
	chatKind_t	tab;						// CHAT_ALL shows every line
	int			scroll;						// rows up from the newest

	char		input[MAX_SAY_TEXT];
	int			cursor;
	int			dropBytes;					// bytes of a character that did not fit and are to be left out
	char		sent[CHAT_SENT][MAX_SAY_TEXT];
	int			sentCount;
	int			sentIndex;					// which one Up and Down are at, sentCount: none

	qboolean	selecting;					// the left button is held in the history
	qboolean	selected;
	chatPos_t	anchor, caret;
	char		url[256];					// a link was clicked: the question whether to open it stands there

	// the emoji that fit the word being typed (Chat_Suggest): a list above the line to type in
	int			suggest[CHAT_SUGGEST];		// indexes of chatEmoji
	int			numSuggest;
	int			suggestSel;
	int			suggestFrom;				// the byte where the word begins
	char		suggestWord[CHAT_EMOJI_WORD];	// the word the list was made for
	float		suggestX, suggestY, suggestW;	// where the list is, from the last draw

	// where things are on the screen, from the last draw, for the mouse
	chatRow_t	rows[CHAT_MAX_OPEN_ROWS];
	int			numRows;
	float		x, width;
	float		tabX[CHAT_NUM_KINDS + 1];
	float		tabY;
} chat;

static const char *chatTabNames[CHAT_NUM_KINDS] = { "All", "Team", "Private" };

static const vec4_t chatGlass = { 0.035f, 0.07f, 0.12f, 0.84f }, chatRim = { 0.43f, 0.83f, 1.0f, 0.35f };
static const vec4_t chatAccent = { 0.43f, 0.83f, 1.0f, 1.0f }, chatDim = { 0.60f, 0.69f, 0.78f, 1.0f };

// the letters of the box: the small font of the game, or the font of the new look
static int Chat_Font( void ) {
	return chat.skin ? SKIN_FONT_SANS : FONT_SMALL;
}

static float Chat_Scale( void ) {
	return ( chat.skin ? CHAT_SKIN_SCALE : CHAT_SCALE ) * chat.size;
}

// a row was measured for this: the width of the box, its letters and their size
static int Chat_WrapKey( float width ) {
	return (int)width + ( chat.skin ? 100000 : 0 ) + ( tay_chatEmoji.integer ? 50000 : 0 ) + (int)( chat.size * 100.0f + 0.5f ) * 1000000;
}

// the name of a tab in the language of the game, in capitals in the new look
static const char *Chat_TabName( int tab ) {
	static char name[CHAT_NUM_KINDS][32];

	Q_strncpyz( name[tab], CG_VisionText( chatTabNames[tab] ), sizeof( name[0] ) );
	return chat.skin ? Q_strupr( name[tab] ) : name[tab];
}

/*
===============================================================================

EMOJI

As TaystJK has them (2026-10-17): a code in a chat line is the picture gfx/emoji/<file>.png where there is one, so
a pak can bring more. The file names are TaystJK's: a ` in the name is a colon of the code, a ~ is a >, and ! makes
the letter after it a capital ("`kappa`.png" is :kappa:, "#~`!D.png" is #>:D). A name without those is the code
between colons ("smile.png" is :smile:). Of two files with one code the first one counts. This tree ships Noto
Emoji for the faces and things (assets/japlus_client/gfx/emoji, Apache 2.0) under the names TaystJK uses, and the
rest of TaystJK's set, its emotes of Twitch among them (2026-10-18, with permission). The real emoji characters of
UTF-8 text are pictures too. Only in the chat box (the bubbles are drawn in the level), tay_chatEmoji 0 shows the
text. Clients of other trees read the code.

===============================================================================
*/

#define CHAT_MAX_EMOJI		512
#define CHAT_EMOJI_NAME		32

static struct {
	char		file[CHAT_EMOJI_NAME];		// the name of the file without .png
	char		code[CHAT_EMOJI_NAME + 2];	// what stands in the chat for it
	int			codeLen;
	qhandle_t	shader;						// registered when first drawn, -1 not there
} chatEmoji[CHAT_MAX_EMOJI];
static int		chatNumEmoji = -1;		// -1: gfx/emoji not listed yet

// the emoji characters of the shipped pictures, and TaystJK's smileys
static const struct {
	unsigned int	codePoint;
	const char		*name;
} chatEmojiChars[] = {
	{ 0x1F604, "smile" }, { 0x1F600, "grinning" }, { 0x1F602, "joy" }, { 0x1F606, "laughing" }, { 0x1F923, "rofl" },
	{ 0x1F642, "slightsmile" }, { 0x1F643, "upsidedown" }, { 0x1F609, "wink" }, { 0x1F60A, "blush" },
	{ 0x263A, "relaxed" }, { 0x1F607, "innocent" }, { 0x1F60D, "hearteyes" }, { 0x1F617, "kissing" },
	{ 0x1F618, "kissingheart" }, { 0x1F60B, "yum" }, { 0x1F61B, "tongue" }, { 0x1F61C, "stuckouttongue" },
	{ 0x1F60E, "sunglasses" }, { 0x1F913, "nerd" }, { 0x1F9D0, "monocle" }, { 0x1F914, "thinking" },
	{ 0x1F928, "raisedeyebrow" }, { 0x1F610, "neutral" }, { 0x1F62C, "grimacing" }, { 0x1F633, "flushed" },
	{ 0x1F62E, "openmouth" }, { 0x1F626, "frowning" }, { 0x1F61F, "worried" }, { 0x1F622, "crying" },
	{ 0x1F62D, "sob" }, { 0x1F629, "weary" }, { 0x1F624, "triumph" }, { 0x1F620, "angry" }, { 0x1F608, "imp" },
	{ 0x1F605, "sweatsmile" }, { 0x1F917, "hugging" }, { 0x1F920, "cowboy" }, { 0x1F921, "clown" },
	{ 0x1F480, "skull" }, { 0x1F4A9, "poop" }, { 0x1F440, "eyes" }, { 0x1F9E0, "brain" }, { 0x2764, "heart" },
	{ 0x1F494, "brokenheart" }, { 0x1F525, "fire" }, { 0x1F4AF, "100" }, { 0x1F44D, "thumbsup" },
	{ 0x1F44E, "thumbsdown" }, { 0x1F44B, "wave" }, { 0x1F44F, "clap" }, { 0x1F64F, "pray" }, { 0x1F4AA, "muscle" },
	{ 0x1F451, "crown" }, { 0x2B50, "star" }, { 0x1F3C6, "trophy" }, { 0x1F36A, "cookie" }, { 0x1F34C, "banana" },
	{ 0x1F346, "eggplant" }, { 0x1F351, "peach" }, { 0x1F345, "tomato" }, { 0x1F37F, "popcorn" },
	{ 0x1F37A, "beer" }, { 0x1F40D, "snake" }, { 0x2603, "snowman" }, { 0x1F384, "christmastree" },
	{ 0x1F383, "jackolantern" }, { 0x1F977, "ninja" }, { 0x1F9C2, "salt" }, { 0x1F52B, "pistol" },
	{ 0x1FAA6, "rip" }, { 0x1F47B, "ghost" }, { 0x1F47D, "alien" }, { 0x1F916, "robot" }, { 0x1F680, "rocket" },
	{ 0x1F4A5, "boom" }, { 0x1F4A4, "zzz" }, { 0x1F4A6, "sweat" }, { 0x1F973, "party" },
};

static const struct {
	const char	*text;
	const char	*name;
} chatSmileys[] = {
	// the longer ones first
	{ "#>:(", "angry" }, { "#>:)", "imp" }, { "#^_^", "blush" }, { "#o_o", "flushed" },
	{ "#:)", "slightsmile" }, { "#:(", "frowning" }, { "#:D", "grinning" }, { "#:P", "tongue" }, { "#;)", "wink" },
	{ "#;(", "crying" }, { "#:o", "openmouth" }, { "#8)", "sunglasses" },
	// the names of TaystJK for the same faces
	{ ":+1:", "thumbsup" }, { ":-1:", "thumbsdown" },
};

// the code of a file name, TaystJK's way (see above)
static void Chat_EmojiCode( const char *file, char *code, int codeSize ) {
	int len = 0;

	if ( !strchr( file, '`' ) && file[0] != '#' ) {
		Com_sprintf( code, codeSize, ":%s:", file );
		return;
	}
	for ( ; *file && len < codeSize - 1; file++ ) {
		if ( *file == '!' ) {
			if ( file[1] ) {
				code[len++] = (char)toupper( (unsigned char)file[1] );
				file++;
			}
		} else if ( *file == '`' ) {
			code[len++] = ':';
		} else if ( *file == '~' ) {
			code[len++] = '>';
		} else {
			code[len++] = *file;
		}
	}
	code[len] = '\0';
}

// a code beginning with a colon is a name and matches in any case, a smiley ("#:D") as it is
static int Chat_CodeMatch( const char *s, const char *code, int len ) {
	return code[0] == ':' ? !Q_stricmpn( s, code, len ) : !strncmp( s, code, len );
}

static void Chat_ListEmoji( void ) {
	char		list[16384];
	const char	*name = list;
	int			count, i, j;

	chatNumEmoji = 0;
	count = trap->FS_GetFileList( "gfx/emoji", ".png", list, sizeof( list ) );
	for ( i = 0; i < count && chatNumEmoji < CHAT_MAX_EMOJI; i++, name += strlen( name ) + 1 ) {
		char base[MAX_QPATH], code[CHAT_EMOJI_NAME + 2];

		COM_StripExtension( name, base, sizeof( base ) );
		if ( !base[0] || strlen( base ) >= CHAT_EMOJI_NAME || strchr( base, '/' ) || strchr( base, ' ' ) ) {
			continue;
		}
		Chat_EmojiCode( base, code, sizeof( code ) );
		for ( j = 0; j < chatNumEmoji; j++ ) {
			if ( !Q_stricmp( chatEmoji[j].code, code ) ) {
				break;		// the first file of a code counts
			}
		}
		if ( j < chatNumEmoji || strlen( code ) < 2 ) {
			continue;
		}
		Q_strncpyz( chatEmoji[chatNumEmoji].file, base, sizeof( chatEmoji[0].file ) );
		Q_strncpyz( chatEmoji[chatNumEmoji].code, code, sizeof( chatEmoji[0].code ) );
		chatEmoji[chatNumEmoji].codeLen = strlen( code );
		chatEmoji[chatNumEmoji].shader = 0;
		chatNumEmoji++;
	}
}

// the emoji of a name: ":name:"
static int Chat_FindEmoji( const char *name ) {
	char	code[CHAT_EMOJI_NAME + 2];
	int		i;

	if ( chatNumEmoji < 0 ) {
		Chat_ListEmoji();
	}
	Com_sprintf( code, sizeof( code ), ":%s:", name );
	for ( i = 0; i < chatNumEmoji; i++ ) {
		if ( !Q_stricmp( chatEmoji[i].code, code ) ) {
			return i;
		}
	}
	return -1;
}

// an emoji at s: its index, and *len the bytes of its text. -1 when there is none
static int Chat_EmojiAt( const char *s, int *len ) {
	size_t	i;
	int		n, best = -1;

	if ( !tay_chatEmoji.integer ) {
		return -1;
	}
	if ( s[0] == ':' || s[0] == '#' ) {
		if ( chatNumEmoji < 0 ) {
			Chat_ListEmoji();
		}
		// the longest code that stands here
		for ( n = 0; n < chatNumEmoji; n++ ) {
			if ( chatEmoji[n].code[0] == s[0] && ( best < 0 || chatEmoji[n].codeLen > chatEmoji[best].codeLen )
				&& Chat_CodeMatch( s, chatEmoji[n].code, chatEmoji[n].codeLen ) ) {
				best = n;
			}
		}
		if ( best >= 0 ) {
			*len = chatEmoji[best].codeLen;
			return best;
		}
		// the smileys that have the picture of a face of Noto
		for ( i = 0; i < ARRAY_LEN( chatSmileys ); i++ ) {
			const int l = strlen( chatSmileys[i].text );

			if ( !strncmp( s, chatSmileys[i].text, l ) && ( n = Chat_FindEmoji( chatSmileys[i].name ) ) >= 0 ) {
				*len = l;
				return n;
			}
		}
	}
	if ( chat.utf8 && ( (unsigned char)s[0] & 0x80 ) ) {
		unsigned int codePoint, next;
		int bytes = Q_UTF8_Decode( s, &codePoint );

		for ( i = 0; i < ARRAY_LEN( chatEmojiChars ); i++ ) {
			if ( chatEmojiChars[i].codePoint == codePoint && ( n = Chat_FindEmoji( chatEmojiChars[i].name ) ) >= 0 ) {
				// the variation selector U+FE0F that may follow belongs to it
				if ( Q_UTF8_Decode( s + bytes, &next ) == 3 && next == 0xFE0F ) {
					bytes += 3;
				}
				*len = bytes;
				return n;
			}
		}
	}
	return -1;
}

// the width of an emoji: as high as a row
static float Chat_EmojiWidth( void ) {
	return CHAT_ROW_HEIGHT * chat.size;
}

static qhandle_t Chat_EmojiShader( int index ) {
	if ( !chatEmoji[index].shader ) {
		chatEmoji[index].shader = trap->R_RegisterShaderNoMip( va( "gfx/emoji/%s", chatEmoji[index].file ) );
		if ( !chatEmoji[index].shader ) {
			chatEmoji[index].shader = -1;
		}
	}
	return chatEmoji[index].shader > 0 ? chatEmoji[index].shader : 0;
}

void CG_ChatEmoji_f( void ) {
	int i;

	if ( chatNumEmoji < 0 ) {
		Chat_ListEmoji();
	}
	for ( i = 0; i < chatNumEmoji; i++ ) {
		trap->Print( "%s%s", chatEmoji[i].code, ( i % 6 == 5 || i == chatNumEmoji - 1 ) ? "\n" : "  " );
	}
	trap->Print( "%i emoji in gfx/emoji, written as above in the chat box%s\n", chatNumEmoji,
		tay_chatEmoji.integer ? "" : " (tay_chatEmoji is 0)" );
}

/*
===============================================================================

TEXT

===============================================================================
*/

static float Chat_TextWidth( const char *text ) {
	return trap->ext.R_Font_StrLenPixels( text, chat.skin ? Skin_FontHandle( SKIN_FONT_SANS ) : cgDC.Assets.qhSmallFont, Chat_Scale() );
}

// the width of text from the byte `from` up to the byte `to`, as plain text: the line to type in shows the codes
static float Chat_TextWidthOf( const char *text, int from, int to ) {
	char part[CHAT_TEXT];

	if ( to <= from ) {
		return 0.0f;
	}
	Q_strncpyz( part, text + from, Q_min( (int)sizeof( part ), to - from + 1 ) );
	return Chat_TextWidth( part );
}

// with the emoji as wide as they are drawn
static float Chat_Width( const char *text ) {
	char	rest[CHAT_TEXT];
	int		out = 0, emoji = 0, len;

	if ( !tay_chatEmoji.integer ) {
		return Chat_TextWidth( text );
	}
	while ( *text && out < (int)sizeof( rest ) - 1 ) {
		if ( Chat_EmojiAt( text, &len ) >= 0 ) {
			emoji++;
			text += len;
		} else {
			rest[out++] = *text++;
		}
	}
	rest[out] = '\0';
	return Chat_TextWidth( rest ) + emoji * Chat_EmojiWidth();
}

/*
=================
Chat_Paint

A row of text with its emoji as pictures. The colour of the text goes on after a picture.
=================
*/
static void Chat_Paint( float x, float y, const vec4_t color, const char *text, int style ) {
	char	run[CHAT_TEXT + 4];
	char	colorCode = 0;
	int		out = 0, len, emoji;

	for ( ;; ) {
		emoji = *text ? Chat_EmojiAt( text, &len ) : -1;
		if ( !*text || emoji >= 0 ) {
			if ( out ) {
				run[out] = '\0';
				CG_Text_Paint( x, y + ( chat.skin ? chat.size : 0.0f ), Chat_Scale(), (float *)color, run, 0, 0, style, Chat_Font() );
				x += Chat_TextWidth( run );
				out = 0;
			}
			if ( !*text ) {
				return;
			}
			if ( Chat_EmojiShader( emoji ) ) {
				const vec4_t white = { 1.0f, 1.0f, 1.0f, color[3] };
				const float w = Chat_EmojiWidth();

				trap->R_SetColor( white );
				CG_DrawPic( x, y + ( chat.rowH - w ) * 0.5f, w, w, Chat_EmojiShader( emoji ) );
				trap->R_SetColor( NULL );
			}
			x += Chat_EmojiWidth();
			text += len;
			// the next run starts in the colour that was set
			if ( colorCode ) {
				run[out++] = Q_COLOR_ESCAPE;
				run[out++] = colorCode;
			}
			continue;
		}
		if ( Q_IsColorString( text ) ) {
			colorCode = text[1];
		}
		if ( out < (int)sizeof( run ) - 1 ) {
			run[out++] = *text;
		}
		text++;
	}
}

// the bytes of the character at s
static int Chat_CharLen( const char *s ) {
	unsigned int codePoint;

	if ( !*s ) {
		return 0;
	}
	return chat.utf8 ? Q_UTF8_Decode( s, &codePoint ) : 1;
}

// the width of text from the byte `from` up to the byte `to`
static float Chat_WidthOf( const char *text, int from, int to ) {
	char part[CHAT_TEXT];

	if ( to <= from ) {
		return 0.0f;
	}
	Q_strncpyz( part, text + from, Q_min( (int)sizeof( part ), to - from + 1 ) );
	return Chat_Width( part );
}

static chatMsg_t *Chat_Msg( int index ) {
	return &chat.history[index % CHAT_HISTORY];
}

static int Chat_First( void ) {
	return Q_max( 0, chat.count - CHAT_HISTORY );
}

static qboolean Chat_Shown( const chatMsg_t *m ) {
	return (qboolean)( chat.tab == CHAT_ALL || m->kind == chat.tab );
}

/*
=================
Chat_Wrap

Breaks a line into rows that fit the width, at a blank where there is one. A colour that was set carries on
into the next row.
=================
*/
static void Chat_Wrap( chatMsg_t *m, float width ) {
	int		i = 0, rowStart = 0, lastSpace = -1;
	char	color = COLOR_WHITE, spaceColor = COLOR_WHITE;
	float	w = 0.0f;

	m->wrapWidth = Chat_WrapKey( width );
	m->numRows = 0;
	m->rowStart[0] = 0;
	m->rowColor[0] = COLOR_WHITE;

	while ( m->text[i] ) {
		char		one[8];
		int			len = Chat_CharLen( m->text + i );
		float		cw;

		if ( Q_IsColorString( m->text + i ) ) {
			color = m->text[i + 1];
			i += 2;
			continue;
		}
		if ( Chat_EmojiAt( m->text + i, &len ) >= 0 ) {
			cw = Chat_EmojiWidth();
		} else {
			Q_strncpyz( one, m->text + i, Q_min( (int)sizeof( one ), len + 1 ) );
			cw = Chat_Width( one );
		}

		if ( w + cw > width && i > rowStart ) {
			const int next = ( lastSpace > rowStart ) ? lastSpace + 1 : i;

			if ( m->numRows + 1 >= CHAT_MAX_ROWS ) {
				// more rows than a line may take: the rest is not shown (it is still there for another width)
				m->numRows++;
				m->rowStart[m->numRows] = next;
				return;
			}
			if ( next != i ) {
				color = spaceColor;
			}
			m->numRows++;
			m->rowStart[m->numRows] = next;
			m->rowColor[m->numRows] = color;
			rowStart = i = next;
			lastSpace = -1;
			w = 0.0f;
			continue;
		}
		if ( m->text[i] == ' ' ) {
			lastSpace = i;
			spaceColor = color;
		}
		w += cw;
		i += len;
	}
	m->numRows++;
	m->rowStart[m->numRows] = strlen( m->text );
}

// the rows of a line at the width of the box
static int Chat_Rows( chatMsg_t *m ) {
	if ( chat.width < 1.0f ) {
		return 1;		// nothing was drawn yet
	}
	if ( m->wrapWidth != Chat_WrapKey( chat.width ) || m->numRows < 1 ) {
		Chat_Wrap( m, chat.width );
	}
	return m->numRows;
}

/*
=================
CG_ChatKind, CG_ChatSender

What kind a chat line is and who said it, from the command as the server sent it. The game marks the parts of
a line with the character 0x19: a name in square brackets is a tell, one in round brackets the team (or the
clan or admin chat of JA+). A server built on OpenJK appends the number of the client, any other leaves the
name to look for.
=================
*/
int CG_ChatKind( const char *cmd, const char *raw ) {
	const char *mark = strchr( raw, '\x19' );

	if ( !strcmp( cmd, "tchat" ) || !strcmp( cmd, "ltchat" ) ) {
		return CHAT_TEAM;
	}
	if ( mark && mark[1] == '[' ) {
		return CHAT_TELL;
	}
	if ( mark && mark[1] == '(' ) {
		return CHAT_TEAM;
	}
	return CHAT_ALL;
}

int CG_ChatSender( const char *raw, int clientNum ) {
	const char	*start = raw, *end = strchr( raw, '\x19' );
	char		name[MAX_NETNAME + 8];
	int			i, len;

	if ( clientNum >= 0 && clientNum < MAX_CLIENTS && cgs.clientinfo[clientNum].infoValid ) {
		return clientNum;
	}
	if ( !end ) {
		return -1;
	}
	if ( end[1] == '[' || end[1] == '(' ) {
		start = end + 2;
		end = strchr( start, '\x19' );
		if ( !end ) {
			return -1;
		}
	}
	len = Q_min( (int)sizeof( name ) - 1, (int)( end - start ) );
	Q_strncpyz( name, start, len + 1 );
	// the white the game puts behind a name
	if ( len >= 2 && name[len - 2] == Q_COLOR_ESCAPE && name[len - 1] == COLOR_WHITE ) {
		name[len - 2] = '\0';
	}
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( cgs.clientinfo[i].infoValid && !strcmp( cgs.clientinfo[i].name, name ) ) {
			return i;
		}
	}
	return -1;
}

// tay_chatLog (as TaystJK's cg_logChat): every line that is shown, without colours, appended to
// chatlogs/<date>.log in the home folder of the game, opened and closed for each line
static void Chat_Log( int kind, const char *text ) {
	static const char	*kinds[CHAT_NUM_KINDS] = { "", "team ", "private " };
	char				line[MAX_SAY_TEXT * 2];
	qtime_t				now;
	fileHandle_t		f;
	int					i;

	if ( !tay_chatLog.integer ) {
		return;
	}
	trap->RealTime( &now );
	Com_sprintf( line, sizeof( line ), "[%02i:%02i:%02i] %s%s", now.tm_hour, now.tm_min, now.tm_sec,
		kinds[Com_Clampi( 0, CHAT_NUM_KINDS - 1, kind )], text );
	Q_StripColor( line );
	// one line in the file for one line of the chat: what others send may hold line breaks
	for ( i = 0; line[i]; i++ ) {
		if ( (unsigned char)line[i] < ' ' ) {
			line[i] = ' ';
		}
	}
	Q_strcat( line, sizeof( line ), "\n" );
	if ( trap->FS_Open( va( "chatlogs/%04i-%02i-%02i.log", now.tm_year + 1900, now.tm_mon + 1, now.tm_mday ), &f, FS_APPEND ) >= 0
		&& f ) {
		trap->FS_Write( line, strlen( line ), f );
		trap->FS_Close( f );
	}
}

// tay_notify, as TaystJK's con_notifywords: whether a line of somebody else holds the own name or one of the words
// of tay_notifyWords, colours and case aside. The sender's name in front of the line does not count.
static qboolean Chat_Mentions( int clientNum, const char *text ) {
	char		line[CHAT_TEXT], name[MAX_NETNAME], words[MAX_CVAR_VALUE_STRING];
	const char	*said = line;
	char		*word, *rest;

	if ( !tay_notify.integer || !cg.snap || clientNum == cg.snap->ps.clientNum ) {
		return qfalse;
	}
	Q_strncpyz( line, text, sizeof( line ) );
	Q_StripColor( line );
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		Q_strncpyz( name, cgs.clientinfo[clientNum].name, sizeof( name ) );
		Q_StripColor( name );
		if ( name[0] && !Q_strncmp( line, name, strlen( name ) ) ) {
			said += strlen( name );
		}
	}

	Q_strncpyz( name, cgs.clientinfo[cg.snap->ps.clientNum].name, sizeof( name ) );
	Q_StripColor( name );
	if ( strlen( name ) >= 3 && Q_stristr( said, name ) ) {
		return qtrue;
	}
	Q_strncpyz( words, tay_notifyWords.string, sizeof( words ) );
	for ( word = words; *word; word = rest ) {
		rest = word + strcspn( word, " ," );
		if ( *rest ) {
			*rest++ = '\0';
		}
		if ( strlen( word ) >= 2 && Q_stristr( said, word ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
=================
CG_ChatAdd

A chat line came, text without the 0x19 marks
=================
*/
void CG_ChatAdd( int kind, int clientNum, const char *text ) {
	chatMsg_t *m;

	// somebody this player does not want to read (the card of select/cg_select.c)
	if ( CG_ChatHidden( clientNum ) ) {
		return;
	}
	m = Chat_Msg( chat.count );

	memset( m, 0, sizeof( *m ) );
	Q_strncpyz( m->text, text, sizeof( m->text ) );
	if ( chat.utf8 ) {
		Q_UTF8_Truncate( m->text, sizeof( m->text ) - 1 );
	}
	m->time = cg.time;
	m->kind = (chatKind_t)Com_Clampi( 0, CHAT_NUM_KINDS - 1, kind );
	m->clientNum = clientNum;
	m->mention = Chat_Mentions( clientNum, text );
	if ( m->mention && tay_notify.integer >= 2 ) {
		trap->S_StartLocalSound( cgs.media.tayNotifySound, CHAN_LOCAL_SOUND );
	}
	// v_notify (hud/cg_notify.c): a tell to this player, a line with his name
	if ( m->kind == CHAT_TELL && ( !cg.snap || clientNum != cg.snap->ps.clientNum ) ) {
		CG_NotifyAdd( NOTIFY_TELL, text );
	} else if ( m->mention ) {
		CG_NotifyAdd( NOTIFY_MENTION, text );
	}
	chat.count++;
	Chat_Log( kind, text );

	// what is marked and the place one scrolled to stay where they are
	if ( chat.open && chat.scroll > 0 && Chat_Shown( m ) ) {
		chat.scroll += Chat_Rows( m );
	}
	if ( chat.selected && chat.count - Q_min( chat.anchor.msg, chat.caret.msg ) > CHAT_HISTORY ) {
		chat.selected = chat.selecting = qfalse;
	}
}

// what the base game calls for a line whose kind and sender it does not tell (voice chat texts)
void CG_ChatBox_AddString( char *chatStr ) {
	CG_ChatAdd( CHAT_ALL, -1, chatStr );
}

/*
===============================================================================

SELECTION AND LINKS

===============================================================================
*/

static int Chat_ComparePos( const chatPos_t *a, const chatPos_t *b ) {
	return ( a->msg != b->msg ) ? a->msg - b->msg : a->offset - b->offset;
}

// what is marked, in order
static qboolean Chat_Selection( chatPos_t *start, chatPos_t *end ) {
	if ( !chat.selected ) {
		return qfalse;
	}
	if ( Chat_ComparePos( &chat.anchor, &chat.caret ) <= 0 ) {
		*start = chat.anchor;
		*end = chat.caret;
	} else {
		*start = chat.caret;
		*end = chat.anchor;
	}
	return (qboolean)( start->msg >= Chat_First() );
}

// The place in the history the cursor is at: the row it is in, the top or bottom one when it is above or below
// them, and the gap between two characters it is nearest to
static qboolean Chat_PosAt( float mx, float my, chatPos_t *pos ) {
	const chatRow_t	*row;
	const chatMsg_t	*m;
	int				i, n = 0, end;
	float			w = 0.0f;

	if ( chat.numRows < 1 ) {
		return qfalse;
	}
	// rows[0] is the lowest
	for ( n = 0; n < chat.numRows - 1; n++ ) {
		if ( my >= chat.rows[n].y ) {
			break;
		}
	}
	row = &chat.rows[n];
	m = Chat_Msg( row->msg );
	end = m->rowStart[row->row + 1];

	pos->msg = row->msg;
	for ( i = m->rowStart[row->row]; i < end; ) {
		char		one[8];
		const int	len = Chat_CharLen( m->text + i );
		float		cw;

		if ( Q_IsColorString( m->text + i ) ) {
			i += 2;
			continue;
		}
		Q_strncpyz( one, m->text + i, Q_min( (int)sizeof( one ), len + 1 ) );
		cw = Chat_Width( one );
		if ( mx - chat.x < w + cw * 0.5f ) {
			break;
		}
		w += cw;
		i += len;
	}
	pos->offset = Q_min( i, end );
	return qtrue;
}

// The next web address in a text from the byte `from` on: where it starts and ends. It ends at a blank, a quote,
// a colour code or the end of the text.
static qboolean Chat_FindURL( const char *text, int from, int *start, int *end ) {
	const char *s;

	for ( s = text + from; *s; s++ ) {
		if ( !Q_strncmp( s, "https://", 8 ) || !Q_strncmp( s, "http://", 7 ) ) {
			const char *e = s;

			while ( *e && (unsigned char)*e > ' ' && *e != '"' && *e != '\'' && *e != '<' && *e != '>' && !Q_IsColorString( e ) ) {
				e++;
			}
			*start = (int)( s - text );
			*end = (int)( e - text );
			// more than the scheme
			if ( *end - *start > ( s[4] == 's' ? 8 : 7 ) ) {
				return qtrue;
			}
			s = e;
			if ( !*s ) {
				break;
			}
		}
	}
	return qfalse;
}

// the web address at a place, qfalse when there is none
static qboolean Chat_URLAt( const chatPos_t *pos, char *url, int urlSize ) {
	const chatMsg_t	*m = Chat_Msg( pos->msg );
	int				from = 0, start, end;

	while ( Chat_FindURL( m->text, from, &start, &end ) ) {
		if ( pos->offset >= start && pos->offset < end ) {
			Q_strncpyz( url, m->text + start, Q_min( urlSize, end - start + 1 ) );
			return qtrue;
		}
		from = end;
	}
	return qfalse;
}

// puts what is marked on the clipboard, a line of the history a line, without the colours
static void Chat_Copy( void ) {
	static char	text[CHAT_MAX_OPEN_ROWS * 8 * CHAT_TEXT];
	chatPos_t	start, end;
	int			i, len = 0;

	if ( !chat.engine || !Chat_Selection( &start, &end ) ) {
		return;
	}
	for ( i = start.msg; i <= end.msg && len < (int)sizeof( text ) - CHAT_TEXT - 2; i++ ) {
		const chatMsg_t	*m = Chat_Msg( i );
		char			part[CHAT_TEXT];
		const int		from = ( i == start.msg ) ? start.offset : 0;
		const int		to = ( i == end.msg ) ? end.offset : (int)strlen( m->text );

		if ( !Chat_Shown( m ) ) {
			continue;
		}
		Q_strncpyz( part, m->text + from, Q_min( (int)sizeof( part ), to - from + 1 ) );
		Q_StripColor( part );
		if ( len ) {
			text[len++] = '\n';
		}
		Q_strncpyz( text + len, part, sizeof( text ) - len );
		len += strlen( text + len );
	}
	text[len] = '\0';
	trap->ext.SetClipboard( text );
}

/*
===============================================================================

THE LINE TO TYPE IN

===============================================================================
*/

static void Chat_Insert( int ch ) {
	const int len = strlen( chat.input );

	if ( chat.utf8 ) {
		// a character that does not fit as a whole is left out as a whole
		if ( ( ch & 0xC0 ) == 0x80 && chat.dropBytes > 0 ) {
			chat.dropBytes--;
			return;
		}
		chat.dropBytes = 0;
		if ( ch >= 0xC0 ) {
			const int need = ( ch >= 0xF0 ) ? 4 : ( ch >= 0xE0 ) ? 3 : 2;

			if ( len + need > (int)sizeof( chat.input ) - 1 ) {
				chat.dropBytes = need - 1;
				return;
			}
		}
	}
	if ( len >= (int)sizeof( chat.input ) - 1 ) {
		return;
	}
	memmove( chat.input + chat.cursor + 1, chat.input + chat.cursor, len + 1 - chat.cursor );
	chat.input[chat.cursor++] = (char)ch;
}

static int Chat_PrevChar( int pos ) {
	if ( pos <= 0 ) {
		return 0;
	}
	return chat.utf8 ? Q_UTF8_PrevChar( chat.input, pos ) : pos - 1;
}

static void Chat_SetInput( const char *text ) {
	Q_strncpyz( chat.input, text, sizeof( chat.input ) );
	chat.cursor = strlen( chat.input );
}

void CG_ChatClose( void ) {
	if ( !chat.open ) {
		return;
	}
	chat.open = qfalse;
	chat.selecting = chat.selected = chat.resizing = qfalse;
	chat.url[0] = '\0';
	chat.scroll = 0;
	trap->Key_SetCatcher( trap->Key_GetCatcher() & ~KEYCATCH_CGAME );
}

static void Chat_Send( void ) {
	char	text[MAX_SAY_TEXT];
	char	*c;

	Q_strncpyz( text, chat.input, sizeof( text ) );
	// the command is read as words in quotes: a quote in the text would end it early
	for ( c = text; *c; c++ ) {
		if ( *c == '"' ) {
			*c = '\'';
		}
	}
	if ( text[0] ) {
		if ( chat.mode == 3 ) {
			// a song for a box of the boombox (boombox/cg_boombox.c)
			CG_BoomboxAsk( chat.target, text );
		} else if ( chat.mode == 2 ) {
			trap->SendClientCommand( va( "tell %i \"%s\"", chat.target, text ) );
		} else if ( chat.mode == 1 ) {
			trap->SendClientCommand( va( "say_team \"%s\"", text ) );
		} else {
			trap->SendClientCommand( va( "say \"%s\"", text ) );
		}
		Q_strncpyz( chat.sent[chat.sentCount % CHAT_SENT], chat.input, sizeof( chat.sent[0] ) );
		chat.sentCount++;
	}
	CG_ChatClose();
}

/*
=================
CG_ChatOpenFor

The box opens to type in. mode 0: say, 1: team, 2: a whisper to client target, 3: a song for box target of the
boombox (boombox/cg_boombox.c). qfalse: this engine leaves typing to its own line (or v_chatInput 0).
=================
*/
qboolean CG_ChatOpenFor( int mode, int target ) {
	if ( !chat.engine || !v_chatInput.integer || cg.demoPlayback || !cg.snap ) {
		return qfalse;
	}
	chat.mode = Com_Clampi( 0, 3, mode );
	chat.target = target;
	if ( chat.mode == 2 && ( chat.target < 0 || chat.target >= MAX_CLIENTS || !cgs.clientinfo[chat.target].infoValid ) ) {
		return qfalse;
	}

	chat.open = qtrue;
	chat.input[0] = '\0';
	chat.cursor = 0;
	chat.dropBytes = 0;
	chat.sentIndex = chat.sentCount;
	chat.scroll = 0;
	chat.selecting = chat.selected = qfalse;
	chat.url[0] = '\0';
	// the cursor starts in the box
	cgs.cursorX = (int)( CHAT_X - cgs.bias2D + 60.0f );
	cgs.cursorY = Q_max( 40, cg_chatBoxHeight.integer - 40 );
	trap->Key_SetCatcher( trap->Key_GetCatcher() | KEYCATCH_CGAME );
	return qtrue;
}

// v_chatOpen <mode> <target>: run by the engine for its message mode commands while v_chatHandler is 1
void CG_ChatOpen_f( void ) {
	char mode[16], target[16];

	trap->Cmd_Argv( 1, mode, sizeof( mode ) );
	trap->Cmd_Argv( 2, target, sizeof( target ) );
	CG_ChatOpenFor( atoi( mode ), atoi( target ) );
}

// a character was typed (also Ctrl with a letter, which comes as its place in the alphabet)
// Ctrl with left or right, Backspace or Delete goes or deletes by word, as in the console
static int Chat_WordLeft( int pos ) {
	while ( pos > 0 && chat.input[pos - 1] == ' ' ) {
		pos--;
	}
	while ( pos > 0 && chat.input[pos - 1] != ' ' ) {
		pos = Chat_PrevChar( pos );
	}
	return pos;
}

static int Chat_WordRight( int pos ) {
	while ( chat.input[pos] && chat.input[pos] != ' ' ) {
		pos += Chat_CharLen( chat.input + pos );
	}
	while ( chat.input[pos] == ' ' ) {
		pos++;
	}
	return pos;
}

static void Chat_DeleteRange( int from, int to ) {
	if ( from < to ) {
		memmove( chat.input + from, chat.input + to, strlen( chat.input + to ) + 1 );
		chat.cursor = from;
	}
}

static void Chat_CharEvent( int ch ) {
	if ( chat.url[0] ) {
		chat.url[0] = '\0';		// anything but Enter is a no
		return;
	}
	if ( ch == 'c' - 'a' + 1 ) {
		Chat_Copy();
		return;
	}
	if ( ch == 'v' - 'a' + 1 ) {
		char	paste[MAX_SAY_TEXT];
		char	*c;

		paste[0] = '\0';
		trap->ext.GetClipboard( paste, sizeof( paste ) );
		for ( c = paste; *c; c++ ) {
			if ( (unsigned char)*c >= ' ' && *c != 127 ) {
				Chat_Insert( (unsigned char)*c );
			}
		}
		return;
	}
	if ( ch == 'h' - 'a' + 1 ) {
		// backspace
		if ( trap->Key_IsDown( A_CTRL ) ) {
			Chat_DeleteRange( Chat_WordLeft( chat.cursor ), chat.cursor );
		} else if ( chat.cursor > 0 ) {
			Chat_DeleteRange( Chat_PrevChar( chat.cursor ), chat.cursor );
		}
		return;
	}
	if ( ch < ' ' || ch == 127 ) {
		return;
	}
	Chat_Insert( ch );
}

/*
=================
Chat_Suggest

The emoji whose codes begin with the word before the cursor, when that word begins with a colon and has a letter
more ("hey :ka" lists :kappa:, :kappaclaus: ...). Up and Down choose, Tab, Enter or a click puts the code in
=================
*/
static void Chat_Suggest( void ) {
	char	word[CHAT_EMOJI_WORD];
	int		start = chat.cursor, len, i;

	while ( start > 0 && chat.input[start - 1] != ' ' ) {
		start--;
	}
	len = chat.cursor - start;
	if ( !tay_chatEmoji.integer || chat.url[0] || chat.input[start] != ':' || len < 2 || len >= (int)sizeof( word )
		|| memchr( chat.input + start + 1, ':', len - 1 ) ) {
		chat.numSuggest = 0;
		chat.suggestWord[0] = '\0';
		return;
	}
	Q_strncpyz( word, chat.input + start, len + 1 );
	if ( chatNumEmoji < 0 ) {
		Chat_ListEmoji();
	}
	if ( strcmp( word, chat.suggestWord ) ) {
		Q_strncpyz( chat.suggestWord, word, sizeof( chat.suggestWord ) );
		chat.suggestSel = 0;
	}
	chat.suggestFrom = start;
	chat.numSuggest = 0;
	for ( i = 0; i < chatNumEmoji && chat.numSuggest < CHAT_SUGGEST; i++ ) {
		if ( chatEmoji[i].code[0] == ':' && !Q_stricmpn( chatEmoji[i].code, word, len ) ) {
			chat.suggest[chat.numSuggest++] = i;
		}
	}
	if ( chat.suggestSel >= chat.numSuggest ) {
		chat.suggestSel = 0;
	}
}

// the chosen code in place of the word, and a blank after it
static void Chat_SuggestTake( void ) {
	const char	*code = chatEmoji[chat.suggest[chat.suggestSel]].code;
	const int	codeLen = strlen( code ), rest = strlen( chat.input + chat.cursor );

	if ( chat.suggestFrom + codeLen + 1 + rest >= (int)sizeof( chat.input ) ) {
		return;
	}
	memmove( chat.input + chat.suggestFrom + codeLen + 1, chat.input + chat.cursor, rest + 1 );
	memcpy( chat.input + chat.suggestFrom, code, codeLen );
	chat.input[chat.suggestFrom + codeLen] = ' ';
	chat.cursor = chat.suggestFrom + codeLen + 1;
	chat.numSuggest = 0;
}

/*
=================
CG_ChatKeyEvent

qtrue: the chat box is open and took the key
=================
*/
qboolean CG_ChatKeyEvent( int key, qboolean down ) {
	chatPos_t	pos;
	int			i;

	if ( !chat.open ) {
		return qfalse;
	}
	if ( key & K_CHAR_FLAG ) {
		Chat_CharEvent( key & ~K_CHAR_FLAG );
		return qtrue;
	}
	if ( !down ) {
		if ( key == A_MOUSE1 ) {
			chat.resizing = qfalse;
		}
		if ( key == A_MOUSE1 && chat.selecting ) {
			chat.selecting = qfalse;
			// a click that marked nothing: on a link it asks whether to open it
			if ( !chat.selected && chat.engine && Chat_PosAt( cgs.cursorX, cgs.cursorY, &pos ) ) {
				Chat_URLAt( &pos, chat.url, sizeof( chat.url ) );
			}
		}
		return qtrue;
	}

	if ( chat.url[0] ) {
		if ( key == A_ENTER || key == A_KP_ENTER ) {
			if ( !trap->ext.OpenURL( chat.url ) ) {
				trap->Print( "This address can not be opened: %s\n", chat.url );
			}
		}
		chat.url[0] = '\0';
		return qtrue;
	}

	// the list of emoji for the word being typed takes the keys that choose from it
	Chat_Suggest();
	if ( chat.numSuggest ) {
		switch ( key ) {
		case A_CURSOR_UP:
			chat.suggestSel = ( chat.suggestSel + chat.numSuggest - 1 ) % chat.numSuggest;
			return qtrue;
		case A_CURSOR_DOWN:
			chat.suggestSel = ( chat.suggestSel + 1 ) % chat.numSuggest;
			return qtrue;
		case A_TAB:
		case A_ENTER:
		case A_KP_ENTER:
			Chat_SuggestTake();
			return qtrue;
		case A_MOUSE1:
			if ( cgs.cursorX >= chat.suggestX && cgs.cursorX < chat.suggestX + chat.suggestW
				&& cgs.cursorY >= chat.suggestY && cgs.cursorY < chat.suggestY + chat.numSuggest * chat.rowH ) {
				chat.suggestSel = Com_Clampi( 0, chat.numSuggest - 1, (int)( ( cgs.cursorY - chat.suggestY ) / chat.rowH ) );
				Chat_SuggestTake();
				return qtrue;
			}
			break;
		default:
			break;
		}
	}

	switch ( key ) {
	case A_ENTER:
	case A_KP_ENTER:
		Chat_Send();
		break;
	case A_DELETE:
		if ( trap->Key_IsDown( A_CTRL ) ) {
			Chat_DeleteRange( chat.cursor, Chat_WordRight( chat.cursor ) );
		} else if ( chat.input[chat.cursor] ) {
			const int len = Chat_CharLen( chat.input + chat.cursor );

			memmove( chat.input + chat.cursor, chat.input + chat.cursor + len, strlen( chat.input + chat.cursor + len ) + 1 );
		}
		break;
	case A_CURSOR_LEFT:
		chat.cursor = trap->Key_IsDown( A_CTRL ) ? Chat_WordLeft( chat.cursor ) : Chat_PrevChar( chat.cursor );
		break;
	case A_CURSOR_RIGHT:
		chat.cursor = trap->Key_IsDown( A_CTRL ) ? Chat_WordRight( chat.cursor ) : chat.cursor + Chat_CharLen( chat.input + chat.cursor );
		break;
	case A_HOME:
		chat.cursor = 0;
		break;
	case A_END:
		chat.cursor = strlen( chat.input );
		break;
	case A_CURSOR_UP:
		// what was sent before
		if ( chat.sentIndex > 0 && chat.sentIndex > chat.sentCount - CHAT_SENT ) {
			chat.sentIndex--;
			Chat_SetInput( chat.sent[chat.sentIndex % CHAT_SENT] );
		}
		break;
	case A_CURSOR_DOWN:
		if ( chat.sentIndex < chat.sentCount ) {
			chat.sentIndex++;
			Chat_SetInput( chat.sentIndex < chat.sentCount ? chat.sent[chat.sentIndex % CHAT_SENT] : "" );
		}
		break;
	case A_PAGE_UP:
		chat.scroll += chat.openRows - 1;		// the draw keeps it inside the history
		break;
	case A_PAGE_DOWN:
		chat.scroll = Q_max( 0, chat.scroll - ( chat.openRows - 1 ) );
		break;
	case A_MWHEELUP:
	case A_MWHEELDOWN:
		// with Ctrl: the box and its letters larger or smaller
		if ( trap->Key_IsDown( A_CTRL ) ) {
			const float size = Com_Clamp( CHAT_MIN_SCALE, CHAT_MAX_SCALE, chat.size + ( key == A_MWHEELUP ? 0.05f : -0.05f ) );

			trap->Cvar_Set( "v_chatBoxScale", va( "%.2f", size ) );
		} else if ( key == A_MWHEELUP ) {
			chat.scroll += 2;
		} else {
			chat.scroll = Q_max( 0, chat.scroll - 2 );
		}
		break;
	case A_TAB:
		chat.tab = (chatKind_t)( ( chat.tab + 1 ) % CHAT_NUM_KINDS );
		chat.scroll = 0;
		chat.selecting = chat.selected = qfalse;
		break;
	case A_MOUSE1:
		// the corner of the box: it is dragged until the button is let go (Chat_DrawOpen)
		if ( cgs.cursorX >= chat.gripX - 3.0f && cgs.cursorX < chat.gripX + CHAT_GRIP + 3.0f
			&& cgs.cursorY >= chat.gripY - 3.0f && cgs.cursorY < chat.gripY + CHAT_GRIP + 3.0f ) {
			chat.resizing = qtrue;
			chat.grabX = cgs.cursorX;
			chat.grabY = cgs.cursorY;
			chat.grabWidth = chat.width;
			chat.grabRows = chat.openRows;
			chat.selecting = chat.selected = qfalse;
			break;
		}
		// a tab
		if ( cgs.cursorY >= chat.tabY && cgs.cursorY < chat.tabY + CHAT_TAB_HEIGHT * chat.size ) {
			for ( i = 0; i < CHAT_NUM_KINDS; i++ ) {
				if ( cgs.cursorX >= chat.tabX[i] && cgs.cursorX < chat.tabX[i + 1] ) {
					chat.tab = (chatKind_t)i;
					chat.scroll = 0;
					chat.selecting = chat.selected = qfalse;
				}
			}
			break;
		}
		// the history: marking begins
		chat.selected = qfalse;
		if ( Chat_PosAt( cgs.cursorX, cgs.cursorY, &pos ) ) {
			chat.anchor = chat.caret = pos;
			chat.selecting = qtrue;
		}
		break;
	case A_MOUSE2:
		Chat_Copy();
		break;
	default:
		break;
	}
	return qtrue;
}

/*
===============================================================================

DRAWING

===============================================================================
*/

/*
=================
Chat_Resize

The upper right corner of the open box was moved by dx and dy from where the box was `width` wide and had
`rows` rows: to the right it gets wider, upwards it gets more rows. What it is set to is what the settings say
from the next frame on.
=================
*/
static void Chat_Resize( float width, int rows, float dx, float dy ) {
	const int newWidth = (int)Com_Clamp( 200.0f, 600.0f, ( width + dx ) / chat.size );
	const int newRows = Com_Clampi( CHAT_MIN_OPEN_ROWS, CHAT_MAX_OPEN_ROWS, rows + (int)floorf( -dy / chat.rowH + 0.5f ) );

	if ( newWidth != (int)v_chatBoxWidth.value ) {
		trap->Cvar_Set( "v_chatBoxWidth", va( "%i", newWidth ) );
	}
	if ( newRows != v_chatBoxRows.integer ) {
		trap->Cvar_Set( "v_chatBoxRows", va( "%i", newRows ) );
	}
}

// v_chatDrag <dx> <dy>: the corner of the open box moved by that much, for a script in place of a mouse
void CG_ChatDrag_f( void ) {
	if ( chat.open ) {
		Chat_Resize( chat.width, chat.openRows, atof( CG_Argv( 1 ) ), atof( CG_Argv( 2 ) ) );
	}
}

// one row of a line, with what is marked behind it and its links underlined
static void Chat_DrawRow( int index, int row, float x, float y, float alpha, qboolean open ) {
	const chatMsg_t	*m = Chat_Msg( index );
	const int		from = m->rowStart[row], to = m->rowStart[row + 1];
	char			text[CHAT_TEXT + 4];
	vec4_t			color = { 1.0f, 1.0f, 1.0f, 1.0f };
	chatPos_t		start, end;
	int				urlFrom = 0, urlStart, urlEnd;

	color[3] = alpha;

	// tay_notify: a line that names the own player stands on a strip of gold
	if ( m->mention ) {
		const vec4_t mention = { 1.0f, 0.78f, 0.25f, 0.22f * alpha };

		CG_FillRect( x - 2.0f, y, Chat_WidthOf( m->text, from, to ) + 4.0f, chat.rowH, mention );
	}

	if ( open && Chat_Selection( &start, &end ) && index >= start.msg && index <= end.msg ) {
		static vec4_t	selColor = { 0.25f, 0.45f, 0.85f, 0.65f }, selSkin = { 0.43f, 0.83f, 1.0f, 0.32f };
		const int		a = Q_max( from, ( index == start.msg ) ? start.offset : 0 );
		const int		b = Q_min( to, ( index == end.msg ) ? end.offset : to );

		if ( b > a ) {
			CG_FillRect( x + Chat_WidthOf( m->text, from, a ), y, Chat_WidthOf( m->text, a, b ), chat.rowH, chat.skin ? selSkin : selColor );
		}
	}
	// over the game, with nobody typing: a dark strip under the row, as wide as its words
	if ( !open && chat.skin ) {
		const vec4_t	strip = { 0.02f, 0.04f, 0.08f, 0.46f * alpha };
		const float		w = Chat_WidthOf( m->text, from, to );

		if ( w > 0.0f ) {
			Skin_Panel( x - 4.0f, y, w + 8.0f, chat.rowH, 3.0f, strip, NULL );
		}
	}

	text[0] = Q_COLOR_ESCAPE;
	text[1] = m->rowColor[row];
	Q_strncpyz( text + 2, m->text + from, Q_min( (int)sizeof( text ) - 2, to - from + 1 ) );
	// (the letters of the new look are lower than their row, and need no shadow on the glass of the open box)
	Chat_Paint( x, y, color, text, ( chat.skin && open ) ? ITEM_TEXTSTYLE_NORMAL : ITEM_TEXTSTYLE_OUTLINED );

	if ( !open || !chat.engine ) {
		return;
	}
	while ( Chat_FindURL( m->text, urlFrom, &urlStart, &urlEnd ) ) {
		static vec4_t	linkColor = { 0.45f, 0.7f, 1.0f, 0.9f };
		const int		a = Q_max( from, urlStart ), b = Q_min( to, urlEnd );

		if ( b > a ) {
			CG_FillRect( x + Chat_WidthOf( m->text, from, a ), y + chat.rowH - 1.5f, Chat_WidthOf( m->text, a, b ), 1.0f, linkColor );
		}
		urlFrom = urlEnd;
	}
}

// the newest lines, for cg_chatBox milliseconds each, while nobody types
static void Chat_DrawClosed( float x, float bottom ) {
	const int	first = Chat_First();
	float		y = bottom;
	int			i, rows = 0;

	for ( i = chat.count - 1; i >= first && rows < CHAT_CLOSED_ROWS; i-- ) {
		chatMsg_t	*m = Chat_Msg( i );
		const int	left = m->time + cg_chatBox.integer - cg.time;
		float		alpha;
		int			row;

		if ( left <= 0 || m->time > cg.time ) {
			break;
		}
		alpha = ( left < CHAT_FADE_MSEC ) ? (float)left / CHAT_FADE_MSEC : 1.0f;
		for ( row = Chat_Rows( m ) - 1; row >= 0 && rows < CHAT_CLOSED_ROWS; row--, rows++ ) {
			y -= chat.rowH;
			Chat_DrawRow( i, row, x, y, alpha, qfalse );
		}
	}
}

static void Chat_DrawOpen( float x, float bottom ) {
	static vec4_t	backColor = { 0.0f, 0.0f, 0.0f, 0.6f };
	static vec4_t	frameColor = { 0.5f, 0.6f, 0.85f, 0.8f };
	static vec4_t	tabColor = { 0.25f, 0.35f, 0.6f, 0.8f };
	static vec4_t	dimColor = { 0.7f, 0.7f, 0.7f, 1.0f };
	const int		first = Chat_First();
	const float		tabH = CHAT_TAB_HEIGHT * chat.size, inputH = CHAT_INPUT_HEIGHT * chat.size;
	const float		top = bottom - chat.openRows * chat.rowH;
	const float		boxX = x - CHAT_PAD, boxY = top - tabH - CHAT_PAD;
	const float		boxW = chat.width + CHAT_BAR_WIDTH + 3.0f * CHAT_PAD;
	const float		boxH = tabH + chat.openRows * chat.rowH + inputH + 3.0f * CHAT_PAD;
	const float		inputY = bottom + CHAT_PAD;
	int				i, total = 0, skip, rows = 0;
	float			y = bottom, tx;
	char			prompt[MAX_NETNAME + 48];

	// the corner is dragged, as far as the pointer went since the button was pressed on it
	if ( chat.resizing ) {
		if ( !trap->Key_IsDown( A_MOUSE1 ) ) {
			chat.resizing = qfalse;
		} else {
			Chat_Resize( chat.grabWidth, chat.grabRows, cgs.cursorX - chat.grabX, cgs.cursorY - chat.grabY );
		}
	}

	if ( chat.skin ) {
		const vec4_t shade = { 0.0f, 0.0f, 0.0f, 0.4f };

		Skin_Soft( boxX, boxY + 3.0f, boxW, boxH, 10.0f, shade );
		Skin_Panel( boxX, boxY, boxW, boxH, 6.0f, chatGlass, chatRim );
	} else {
		CG_FillRect( boxX, boxY, boxW, boxH, backColor );
		CG_DrawRect( boxX, boxY, boxW, boxH, 1.0f, frameColor );
	}

	// the tabs: a filled one for the open tab, or in the new look its word bright with a line under it
	chat.tabY = boxY + 1.0f;
	tx = x;
	for ( i = 0; i < CHAT_NUM_KINDS; i++ ) {
		const char		*name = Chat_TabName( i );
		const float		w = Chat_Width( name ) + 2.0f * CHAT_PAD;
		const qboolean	open = (qboolean)( chat.tab == (chatKind_t)i );

		chat.tabX[i] = tx;
		if ( chat.skin ) {
			if ( open ) {
				Skin_Panel( tx + CHAT_PAD, chat.tabY + tabH - 1.5f, w - 2.0f * CHAT_PAD, 1.3f, 0.65f, chatAccent, NULL );
			}
			CG_Text_Paint( tx + CHAT_PAD, chat.tabY + chat.size, Chat_Scale(), (float *)( open ? colorWhite : chatDim ), name, 0, 0, 0, Chat_Font() );
		} else {
			if ( open ) {
				CG_FillRect( tx, chat.tabY, w, tabH, tabColor );
			}
			CG_Text_Paint( tx + CHAT_PAD, chat.tabY, Chat_Scale(), open ? colorWhite : dimColor, name, 0, 0, 0, FONT_SMALL );
		}
		tx += w + 2.0f;
	}
	chat.tabX[CHAT_NUM_KINDS] = tx;

	// the corner that is dragged: two short lines at the upper right, bright under the pointer and while it is held
	chat.gripX = boxX + boxW - CHAT_GRIP - 3.0f;
	chat.gripY = boxY + 3.0f;
	{
		const qboolean	over = (qboolean)( chat.resizing || ( cgs.cursorX >= chat.gripX - 3.0f && cgs.cursorX < chat.gripX + CHAT_GRIP + 3.0f
			&& cgs.cursorY >= chat.gripY - 3.0f && cgs.cursorY < chat.gripY + CHAT_GRIP + 3.0f ) );
		const float		*color = chat.skin ? ( over ? chatAccent : chatDim ) : ( over ? colorWhite : dimColor );

		CG_FillRect( chat.gripX, chat.gripY, CHAT_GRIP, 1.2f, (float *)color );
		CG_FillRect( chat.gripX + CHAT_GRIP - 1.2f, chat.gripY, 1.2f, CHAT_GRIP, (float *)color );
	}

	// the history, from the lowest row up: `scroll` rows of the newest lines are below the box
	for ( i = first; i < chat.count; i++ ) {
		if ( Chat_Shown( Chat_Msg( i ) ) ) {
			total += Chat_Rows( Chat_Msg( i ) );
		}
	}
	chat.scroll = Com_Clampi( 0, Q_max( 0, total - chat.openRows ), chat.scroll );
	skip = chat.scroll;
	chat.numRows = 0;
	for ( i = chat.count - 1; i >= first && rows < chat.openRows; i-- ) {
		chatMsg_t	*m = Chat_Msg( i );
		int			row;

		if ( !Chat_Shown( m ) ) {
			continue;
		}
		for ( row = Chat_Rows( m ) - 1; row >= 0 && rows < chat.openRows; row-- ) {
			if ( skip > 0 ) {
				skip--;
				continue;
			}
			y -= chat.rowH;
			chat.rows[rows].msg = i;
			chat.rows[rows].row = row;
			chat.rows[rows].y = y;
			rows++;
		}
	}
	chat.numRows = rows;

	// what the held button marks, by the rows as they stand now
	if ( chat.selecting ) {
		if ( !trap->Key_IsDown( A_MOUSE1 ) ) {
			chat.selecting = qfalse;
		} else if ( Chat_PosAt( cgs.cursorX, cgs.cursorY, &chat.caret ) ) {
			chat.selected = (qboolean)( Chat_ComparePos( &chat.anchor, &chat.caret ) != 0 );
		}
	}
	for ( i = 0; i < rows; i++ ) {
		Chat_DrawRow( chat.rows[i].msg, chat.rows[i].row, x, chat.rows[i].y, 1.0f, qtrue );
	}

	// where in the history the box is
	if ( total > chat.openRows ) {
		const float barH = chat.openRows * chat.rowH;
		const float thumbH = Q_max( 6.0f, barH * chat.openRows / total );
		const float thumbY = top + ( barH - thumbH ) * ( 1.0f - (float)chat.scroll / ( total - chat.openRows ) );

		if ( chat.skin ) {
			Skin_ScrollBar( x + chat.width + CHAT_PAD, top, CHAT_BAR_WIDTH, barH, ( thumbY - top ) / barH, thumbH / barH, qfalse, chatAccent );
		} else {
			CG_FillRect( x + chat.width + CHAT_PAD, top, CHAT_BAR_WIDTH, barH, tabColor );
			CG_FillRect( x + chat.width + CHAT_PAD, thumbY, CHAT_BAR_WIDTH, thumbH, frameColor );
		}
	}

	// the line to type in, or the question about a link
	if ( chat.skin ) {
		Skin_Line( boxX + 4.0f, inputY - 1.0f, boxW - 8.0f, chatRim );
	} else {
		CG_FillRect( boxX + 1.0f, inputY - 1.0f, boxW - 2.0f, 1.0f, frameColor );
	}
	if ( chat.url[0] ) {
		char	question[sizeof( chat.url ) + 64];
		int		len;

		Q_strncpyz( question, CG_VisionTextWith( "Open %s ? Enter: yes, any other key: no", chat.url ), sizeof( question ) );
		// what does not fit is cut, the start of the address is what matters
		for ( len = strlen( question ); len > 0 && Chat_Width( question ) > chat.width; len-- ) {
			question[len - 1] = '\0';
		}
		CG_Text_Paint( x, inputY + ( chat.skin ? chat.size : 0.0f ), Chat_Scale(), colorYellow, question, 0, 0, 0, Chat_Font() );
	} else {
		const float	down = chat.skin ? chat.size : 0.0f;
		float		promptW, textW;
		int			start = 0;
		char		shown[MAX_SAY_TEXT];

		if ( chat.mode == 3 ) {
			Com_sprintf( prompt, sizeof( prompt ), "%s: ", CG_VisionText( "Boombox, link or song" ) );
		} else if ( chat.mode == 2 ) {
			Com_sprintf( prompt, sizeof( prompt ), "%s^7: ", CG_VisionTextWith( "To %s", cgs.clientinfo[chat.target].name ) );
		} else {
			Com_sprintf( prompt, sizeof( prompt ), "%s: ", CG_VisionText( chat.mode == 1 ? "Team" : "Say" ) );
		}
		promptW = Chat_TextWidth( prompt );
		CG_Text_Paint( x, inputY + down, Chat_Scale(), (float *)( chat.skin ? chatDim : dimColor ), prompt, 0, 0, 0, Chat_Font() );

		// a text that is wider than the line is shown from where the cursor still fits (the codes of emoji as text)
		while ( start < chat.cursor && Chat_TextWidthOf( chat.input, start, chat.cursor ) > chat.width - promptW - 6.0f ) {
			start += Chat_CharLen( chat.input + start );
		}
		Q_strncpyz( shown, chat.input + start, sizeof( shown ) );
		for ( i = strlen( shown ); i > 0 && Chat_TextWidth( shown ) > chat.width - promptW; i = strlen( shown ) ) {
			shown[Chat_PrevChar( start + i ) - start] = '\0';
		}
		// a ^ with a digit colours what follows while it is typed, as the line will look when it is sent
		textW = Chat_TextWidthOf( chat.input, start, chat.cursor );
		CG_Text_Paint( x + promptW, inputY + down, Chat_Scale(), colorWhite, shown, 0, 0, 0, Chat_Font() );
		if ( ( trap->Milliseconds() >> 8 ) & 1 ) {
			CG_FillRect( x + promptW + textW, inputY + 1.0f, 1.0f, chat.rowH - 2.0f, (float *)( chat.skin ? chatAccent : colorWhite ) );
		}

		// the emoji for the word being typed, above the line, from where the word begins
		Chat_Suggest();
		if ( chat.numSuggest ) {
			const float	emojiW = Chat_EmojiWidth();
			const float	wordX = x + promptW + ( chat.suggestFrom > start ? Chat_TextWidthOf( chat.input, start, chat.suggestFrom ) : 0.0f );
			float		w = 0.0f;
			int			n;

			for ( n = 0; n < chat.numSuggest; n++ ) {
				w = Q_max( w, Chat_TextWidth( chatEmoji[chat.suggest[n]].code ) );
			}
			chat.suggestW = emojiW + w + 3.0f * CHAT_PAD;
			chat.suggestX = Q_min( wordX, boxX + boxW - chat.suggestW );
			chat.suggestY = inputY - 2.0f - chat.numSuggest * chat.rowH;
			if ( chat.skin ) {
				Skin_Panel( chat.suggestX, chat.suggestY - 2.0f, chat.suggestW, chat.numSuggest * chat.rowH + 4.0f, 4.0f, chatGlass, chatRim );
			} else {
				CG_FillRect( chat.suggestX, chat.suggestY - 2.0f, chat.suggestW, chat.numSuggest * chat.rowH + 4.0f, frameColor );
			}
			for ( n = 0; n < chat.numSuggest; n++ ) {
				const float	rowY = chat.suggestY + n * chat.rowH;
				const int	e = chat.suggest[n];

				if ( n == chat.suggestSel ) {
					static vec4_t sel = { 0.43f, 0.83f, 1.0f, 0.32f }, selOld = { 0.25f, 0.45f, 0.85f, 0.65f };

					CG_FillRect( chat.suggestX + 2.0f, rowY, chat.suggestW - 4.0f, chat.rowH, chat.skin ? sel : selOld );
				}
				if ( Chat_EmojiShader( e ) ) {
					trap->R_SetColor( NULL );
					CG_DrawPic( chat.suggestX + CHAT_PAD, rowY + ( chat.rowH - emojiW ) * 0.5f, emojiW, emojiW, Chat_EmojiShader( e ) );
				}
				CG_Text_Paint( chat.suggestX + 2.0f * CHAT_PAD + emojiW, rowY + down, Chat_Scale(),
					(float *)( n == chat.suggestSel ? colorWhite : ( chat.skin ? chatDim : dimColor ) ), chatEmoji[e].code, 0, 0, 0, Chat_Font() );
			}
		}
	}
	// (the pointer is drawn by CG_SelectDraw(), after the card of a player: it lies on everything)
}

// The lines of one player are left out of the chat box from now on, or let in again. Until the level changes.
static qboolean chatHidden[MAX_CLIENTS];

qboolean CG_ChatHidden( int clientNum ) {
	return (qboolean)( clientNum >= 0 && clientNum < MAX_CLIENTS && chatHidden[clientNum] );
}

void CG_ChatHide( int clientNum, qboolean hide ) {
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		chatHidden[clientNum] = hide;
	}
}

qboolean CG_ChatIsOpen( void ) {
	return chat.open;
}

/*
=================
CG_ChatDraw

Once a frame, in the 2D of the HUD
=================
*/
void CG_ChatDraw( void ) {
	float		bottom;
	char		value[8];

	// which engine this is, and whether its message mode commands are to open the box
	trap->Cvar_VariableStringBuffer( "v_cgameChat", value, sizeof( value ) );
	chat.engine = (qboolean)( atoi( value ) == 1 );
	trap->Cvar_VariableStringBuffer( "v_utf8Text", value, sizeof( value ) );
	chat.utf8 = (qboolean)( atoi( value ) == 1 );
	if ( chat.handlerCount != v_chatInput.modificationCount + 1 ) {
		chat.handlerCount = v_chatInput.modificationCount + 1;
		trap->Cvar_Set( "v_chatHandler", ( chat.engine && v_chatInput.integer ) ? "1" : "0" );
	}

	chat.skin = CG_HudNewLook();
	chat.x = CHAT_X - cgs.bias2D;
	// how large it is: everything by v_chatBoxScale, and as many rows as fit under the top of the screen
	chat.size = Com_Clamp( CHAT_MIN_SCALE, CHAT_MAX_SCALE, v_chatBoxScale.value );
	chat.rowH = CHAT_ROW_HEIGHT * chat.size;
	// Where the last row stands. The lines of the closed box move down while the scoreboard shows, the HUD is gone
	// then. The open box stays where it always is, also over a scoreboard that is kept open, and it keeps room for
	// its input line: at 475 that line was under the edge of the screen and nobody saw what he typed.
	bottom = ( cg.scoreBoardShowing && !chat.open ) ? 475.0f : cg_chatBoxHeight.value;
	if ( chat.open ) {
		bottom = Q_min( bottom, SCREEN_HEIGHT - 4.0f - CHAT_INPUT_HEIGHT * chat.size - 2.0f * CHAT_PAD );
	}
	// (no wider than the screen has room, whatever width and size say together)
	chat.width = Q_min( Com_Clamp( 200.0f, 600.0f, v_chatBoxWidth.value ) * chat.size,
		SCREEN_WIDTH + 2.0f * cgs.bias2D - 2.0f * CHAT_X - CHAT_BAR_WIDTH - 3.0f * CHAT_PAD );
	chat.openRows = Com_Clampi( CHAT_MIN_OPEN_ROWS, Q_max( CHAT_MIN_OPEN_ROWS,
		Q_min( CHAT_MAX_OPEN_ROWS, (int)( ( bottom - CHAT_TAB_HEIGHT * chat.size - 2.0f * CHAT_PAD - 4.0f ) / chat.rowH ) ) ), v_chatBoxRows.integer );

	CG_Place2D( PLACE_CENTER );
	if ( chat.open ) {
		if ( !( trap->Key_GetCatcher() & KEYCATCH_CGAME ) ) {
			// something else took the keys away (a menu)
			chat.open = qfalse;
		} else {
			Chat_DrawOpen( chat.x, bottom );
			return;
		}
	}
	if ( cg_chatBox.integer > 0 ) {
		Chat_DrawClosed( chat.x, bottom );
	}
}

// the cgame was loaded anew: the engine cleared v_chatHandler, it is set again with the first frame
void CG_ChatInit( void ) {
	memset( &chat, 0, sizeof( chat ) );
	chatNumEmoji = -1;		// listed again: a pak of the server may bring more
}
