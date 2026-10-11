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
// tree (cvar cl_cgameChat) and cg_chatInput 1 the message mode keys open the box of this file instead of the
// line of the engine (the cgame says so with cg_chatHandler, the engine then runs cg_chatopen): a frame with
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

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"

extern displayContextDef_t cgDC;

#define CHAT_HISTORY		256
#define CHAT_TEXT			320				// a line as it came: name, colours, text
#define CHAT_MAX_ROWS		8				// rows a line takes at most when it is wrapped
#define CHAT_SCALE			0.65f
#define CHAT_ROW_HEIGHT		13.0f
#define CHAT_OPEN_ROWS		10				// rows of history in the open box
#define CHAT_CLOSED_ROWS	6				// rows that stand there while nobody types
#define CHAT_X				30.0f
#define CHAT_PAD			4.0f
#define CHAT_TAB_HEIGHT		14.0f
#define CHAT_INPUT_HEIGHT	15.0f
#define CHAT_BAR_WIDTH		4.0f
#define CHAT_FADE_MSEC		1000
#define CHAT_SENT			16				// lines one sent that Up brings back
#define CHAT_CURSOR_SIZE	40.0f			// as the ui draws its cursor

typedef struct chatMsg_s {
	char		text[CHAT_TEXT];
	int			time;						// cg.time when it came
	chatKind_t	kind;
	int			clientNum;					// who said it, -1 when that is not known
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

static struct {
	chatMsg_t	history[CHAT_HISTORY];
	int			count;						// lines that ever came, line n is history[n % CHAT_HISTORY]

	qboolean	engine;						// the engine has the calls of the box (cl_cgameChat)
	qboolean	utf8;						// text is UTF-8 (cl_utf8Text)
	int			handlerCount;				// cg_chatInput as it was when cg_chatHandler was last set

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

	// where things are on the screen, from the last draw, for the mouse
	chatRow_t	rows[CHAT_OPEN_ROWS];
	int			numRows;
	float		x, width;
	float		tabX[CHAT_NUM_KINDS + 1];
	float		tabY;
	qhandle_t	cursor;						// the picture of the mouse cursor
} chat;

static const char *chatTabNames[CHAT_NUM_KINDS] = { "All", "Team", "Private" };

/*
===============================================================================

TEXT

===============================================================================
*/

static float Chat_Width( const char *text ) {
	return trap->ext.R_Font_StrLenPixels( text, cgDC.Assets.qhSmallFont, CHAT_SCALE );
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

	m->wrapWidth = (int)width;
	m->numRows = 0;
	m->rowStart[0] = 0;
	m->rowColor[0] = COLOR_WHITE;

	while ( m->text[i] ) {
		char		one[8];
		const int	len = Chat_CharLen( m->text + i );
		float		cw;

		if ( Q_IsColorString( m->text + i ) ) {
			color = m->text[i + 1];
			i += 2;
			continue;
		}
		Q_strncpyz( one, m->text + i, Q_min( (int)sizeof( one ), len + 1 ) );
		cw = Chat_Width( one );

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
	if ( m->wrapWidth != (int)chat.width || m->numRows < 1 ) {
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

/*
=================
CG_ChatAdd

A chat line came, text without the 0x19 marks
=================
*/
void CG_ChatAdd( int kind, int clientNum, const char *text ) {
	chatMsg_t *m = Chat_Msg( chat.count );

	memset( m, 0, sizeof( *m ) );
	Q_strncpyz( m->text, text, sizeof( m->text ) );
	if ( chat.utf8 ) {
		Q_UTF8_Truncate( m->text, sizeof( m->text ) - 1 );
	}
	m->time = cg.time;
	m->kind = (chatKind_t)Com_Clampi( 0, CHAT_NUM_KINDS - 1, kind );
	m->clientNum = clientNum;
	chat.count++;

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
	static char	text[CHAT_OPEN_ROWS * 8 * CHAT_TEXT];
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
	chat.selecting = chat.selected = qfalse;
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
		if ( chat.mode == 2 ) {
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
CG_ChatOpen_f

cg_chatopen <mode> <client>: run by the engine for its message mode commands while cg_chatHandler is 1
=================
*/
void CG_ChatOpen_f( void ) {
	char arg[16];

	if ( !chat.engine || !cg_chatInput.integer || cg.demoPlayback || !cg.snap ) {
		return;
	}
	trap->Cmd_Argv( 1, arg, sizeof( arg ) );
	chat.mode = Com_Clampi( 0, 2, atoi( arg ) );
	trap->Cmd_Argv( 2, arg, sizeof( arg ) );
	chat.target = atoi( arg );
	if ( chat.mode == 2 && ( chat.target < 0 || chat.target >= MAX_CLIENTS || !cgs.clientinfo[chat.target].infoValid ) ) {
		return;
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
}

// a character was typed (also Ctrl with a letter, which comes as its place in the alphabet)
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
		if ( chat.cursor > 0 ) {
			const int prev = Chat_PrevChar( chat.cursor );

			memmove( chat.input + prev, chat.input + chat.cursor, strlen( chat.input + chat.cursor ) + 1 );
			chat.cursor = prev;
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

	switch ( key ) {
	case A_ENTER:
	case A_KP_ENTER:
		Chat_Send();
		break;
	case A_DELETE:
		if ( chat.input[chat.cursor] ) {
			const int len = Chat_CharLen( chat.input + chat.cursor );

			memmove( chat.input + chat.cursor, chat.input + chat.cursor + len, strlen( chat.input + chat.cursor + len ) + 1 );
		}
		break;
	case A_CURSOR_LEFT:
		chat.cursor = Chat_PrevChar( chat.cursor );
		break;
	case A_CURSOR_RIGHT:
		chat.cursor += Chat_CharLen( chat.input + chat.cursor );
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
		chat.scroll += CHAT_OPEN_ROWS - 1;		// the draw keeps it inside the history
		break;
	case A_PAGE_DOWN:
		chat.scroll = Q_max( 0, chat.scroll - ( CHAT_OPEN_ROWS - 1 ) );
		break;
	case A_MWHEELUP:
		chat.scroll += 2;
		break;
	case A_MWHEELDOWN:
		chat.scroll = Q_max( 0, chat.scroll - 2 );
		break;
	case A_TAB:
		chat.tab = (chatKind_t)( ( chat.tab + 1 ) % CHAT_NUM_KINDS );
		chat.scroll = 0;
		chat.selecting = chat.selected = qfalse;
		break;
	case A_MOUSE1:
		// a tab
		if ( cgs.cursorY >= chat.tabY && cgs.cursorY < chat.tabY + CHAT_TAB_HEIGHT ) {
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

// one row of a line, with what is marked behind it and its links underlined
static void Chat_DrawRow( int index, int row, float x, float y, float alpha, qboolean open ) {
	const chatMsg_t	*m = Chat_Msg( index );
	const int		from = m->rowStart[row], to = m->rowStart[row + 1];
	char			text[CHAT_TEXT + 4];
	vec4_t			color = { 1.0f, 1.0f, 1.0f, 1.0f };
	chatPos_t		start, end;
	int				urlFrom = 0, urlStart, urlEnd;

	color[3] = alpha;

	if ( open && Chat_Selection( &start, &end ) && index >= start.msg && index <= end.msg ) {
		static vec4_t	selColor = { 0.25f, 0.45f, 0.85f, 0.65f };
		const int		a = Q_max( from, ( index == start.msg ) ? start.offset : 0 );
		const int		b = Q_min( to, ( index == end.msg ) ? end.offset : to );

		if ( b > a ) {
			CG_FillRect( x + Chat_WidthOf( m->text, from, a ), y, Chat_WidthOf( m->text, a, b ), CHAT_ROW_HEIGHT, selColor );
		}
	}

	text[0] = Q_COLOR_ESCAPE;
	text[1] = m->rowColor[row];
	Q_strncpyz( text + 2, m->text + from, Q_min( (int)sizeof( text ) - 2, to - from + 1 ) );
	CG_Text_Paint( x, y, CHAT_SCALE, color, text, 0, 0, ITEM_TEXTSTYLE_OUTLINED, FONT_SMALL );

	if ( !open || !chat.engine ) {
		return;
	}
	while ( Chat_FindURL( m->text, urlFrom, &urlStart, &urlEnd ) ) {
		static vec4_t	linkColor = { 0.45f, 0.7f, 1.0f, 0.9f };
		const int		a = Q_max( from, urlStart ), b = Q_min( to, urlEnd );

		if ( b > a ) {
			CG_FillRect( x + Chat_WidthOf( m->text, from, a ), y + CHAT_ROW_HEIGHT - 1.5f, Chat_WidthOf( m->text, a, b ), 1.0f, linkColor );
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
			y -= CHAT_ROW_HEIGHT;
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
	const float		top = bottom - CHAT_OPEN_ROWS * CHAT_ROW_HEIGHT;
	const float		boxX = x - CHAT_PAD, boxY = top - CHAT_TAB_HEIGHT - CHAT_PAD;
	const float		boxW = chat.width + CHAT_BAR_WIDTH + 3.0f * CHAT_PAD;
	const float		boxH = CHAT_TAB_HEIGHT + CHAT_OPEN_ROWS * CHAT_ROW_HEIGHT + CHAT_INPUT_HEIGHT + 3.0f * CHAT_PAD;
	const float		inputY = bottom + CHAT_PAD;
	int				i, total = 0, skip, rows = 0;
	float			y = bottom, tx;
	char			prompt[MAX_NETNAME + 16];

	CG_FillRect( boxX, boxY, boxW, boxH, backColor );
	CG_DrawRect( boxX, boxY, boxW, boxH, 1.0f, frameColor );

	// the tabs
	chat.tabY = boxY + 1.0f;
	tx = x;
	for ( i = 0; i < CHAT_NUM_KINDS; i++ ) {
		const float w = Chat_Width( chatTabNames[i] ) + 2.0f * CHAT_PAD;

		chat.tabX[i] = tx;
		if ( chat.tab == (chatKind_t)i ) {
			CG_FillRect( tx, chat.tabY, w, CHAT_TAB_HEIGHT, tabColor );
		}
		CG_Text_Paint( tx + CHAT_PAD, chat.tabY, CHAT_SCALE, ( chat.tab == (chatKind_t)i ) ? colorWhite : dimColor, chatTabNames[i], 0, 0, 0, FONT_SMALL );
		tx += w + 2.0f;
	}
	chat.tabX[CHAT_NUM_KINDS] = tx;

	// the history, from the lowest row up: `scroll` rows of the newest lines are below the box
	for ( i = first; i < chat.count; i++ ) {
		if ( Chat_Shown( Chat_Msg( i ) ) ) {
			total += Chat_Rows( Chat_Msg( i ) );
		}
	}
	chat.scroll = Com_Clampi( 0, Q_max( 0, total - CHAT_OPEN_ROWS ), chat.scroll );
	skip = chat.scroll;
	chat.numRows = 0;
	for ( i = chat.count - 1; i >= first && rows < CHAT_OPEN_ROWS; i-- ) {
		chatMsg_t	*m = Chat_Msg( i );
		int			row;

		if ( !Chat_Shown( m ) ) {
			continue;
		}
		for ( row = Chat_Rows( m ) - 1; row >= 0 && rows < CHAT_OPEN_ROWS; row-- ) {
			if ( skip > 0 ) {
				skip--;
				continue;
			}
			y -= CHAT_ROW_HEIGHT;
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
	if ( total > CHAT_OPEN_ROWS ) {
		const float barH = CHAT_OPEN_ROWS * CHAT_ROW_HEIGHT;
		const float thumbH = Q_max( 6.0f, barH * CHAT_OPEN_ROWS / total );
		const float thumbY = top + ( barH - thumbH ) * ( 1.0f - (float)chat.scroll / ( total - CHAT_OPEN_ROWS ) );

		CG_FillRect( x + chat.width + CHAT_PAD, top, CHAT_BAR_WIDTH, barH, tabColor );
		CG_FillRect( x + chat.width + CHAT_PAD, thumbY, CHAT_BAR_WIDTH, thumbH, frameColor );
	}

	// the line to type in, or the question about a link
	CG_FillRect( boxX + 1.0f, inputY - 1.0f, boxW - 2.0f, 1.0f, frameColor );
	if ( chat.url[0] ) {
		char	question[sizeof( chat.url ) + 64];
		int		len;

		Com_sprintf( question, sizeof( question ), "Open %s ? Enter: yes, any other key: no", chat.url );
		// what does not fit is cut, the start of the address is what matters
		for ( len = strlen( question ); len > 0 && Chat_Width( question ) > chat.width; len-- ) {
			question[len - 1] = '\0';
		}
		CG_Text_Paint( x, inputY, CHAT_SCALE, colorYellow, question, 0, 0, 0, FONT_SMALL );
	} else {
		float	promptW, textW;
		int		start = 0;
		char	shown[MAX_SAY_TEXT];

		if ( chat.mode == 2 ) {
			Com_sprintf( prompt, sizeof( prompt ), "To %s^7: ", cgs.clientinfo[chat.target].name );
		} else {
			Q_strncpyz( prompt, chat.mode == 1 ? "Team: " : "Say: ", sizeof( prompt ) );
		}
		promptW = Chat_Width( prompt );
		CG_Text_Paint( x, inputY, CHAT_SCALE, dimColor, prompt, 0, 0, 0, FONT_SMALL );

		// a text that is wider than the line is shown from where the cursor still fits
		while ( start < chat.cursor && Chat_WidthOf( chat.input, start, chat.cursor ) > chat.width - promptW - 6.0f ) {
			start += Chat_CharLen( chat.input + start );
		}
		Q_strncpyz( shown, chat.input + start, sizeof( shown ) );
		for ( i = strlen( shown ); i > 0 && Chat_Width( shown ) > chat.width - promptW; i = strlen( shown ) ) {
			shown[Chat_PrevChar( start + i ) - start] = '\0';
		}
		// a ^ with a digit colours what follows while it is typed, as the line will look when it is sent
		textW = Chat_WidthOf( chat.input, start, chat.cursor );
		CG_Text_Paint( x + promptW, inputY, CHAT_SCALE, colorWhite, shown, 0, 0, 0, FONT_SMALL );
		if ( ( trap->Milliseconds() >> 8 ) & 1 ) {
			CG_FillRect( x + promptW + textW, inputY + 1.0f, 1.0f, CHAT_ROW_HEIGHT - 2.0f, colorWhite );
		}
	}

	// The cursor of the menus. (cgs.media.cursor is no use: the base game registers a picture of Quake 3 Team
	// Arena for it that this game does not have, so nothing was drawn.)
	if ( !chat.cursor ) {
		chat.cursor = trap->R_RegisterShaderNoMip( "cursor" );
	}
	trap->R_SetColor( NULL );
	CG_DrawPic( cgs.cursorX, cgs.cursorY, CHAT_CURSOR_SIZE, CHAT_CURSOR_SIZE, chat.cursor );
}

/*
=================
CG_ChatDraw

Once a frame, in the 2D of the HUD
=================
*/
void CG_ChatDraw( void ) {
	const float	bottom = cg.scoreBoardShowing ? 475.0f : cg_chatBoxHeight.value;
	char		value[8];

	// which engine this is, and whether its message mode commands are to open the box
	trap->Cvar_VariableStringBuffer( "cl_cgameChat", value, sizeof( value ) );
	chat.engine = (qboolean)( atoi( value ) == 1 );
	trap->Cvar_VariableStringBuffer( "cl_utf8Text", value, sizeof( value ) );
	chat.utf8 = (qboolean)( atoi( value ) == 1 );
	if ( chat.handlerCount != cg_chatInput.modificationCount + 1 ) {
		chat.handlerCount = cg_chatInput.modificationCount + 1;
		trap->Cvar_Set( "cg_chatHandler", ( chat.engine && cg_chatInput.integer ) ? "1" : "0" );
	}

	chat.x = CHAT_X - cgs.bias2D;
	chat.width = Com_Clamp( 200.0f, 600.0f, cg_chatBoxWidth.value );

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

// the cgame was loaded anew: the engine cleared cg_chatHandler, it is set again with the first frame
void CG_ChatInit( void ) {
	memset( &chat, 0, sizeof( chat ) );
}
