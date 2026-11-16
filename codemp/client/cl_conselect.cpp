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

// cl_conselect.cpp -- marking text of the console with the mouse and copying it

#include "client.h"
#include "cl_console_local.h"

/*
==============================================================================

SELECTION

The mouse belongs to the console while it is down. In a window the system draws its cursor and says where it is
(IN_MousePosition), in fullscreen the game holds the mouse and the console moves a cursor of its own by what the
mouse moved (Con_MouseEvent). Holding the left button and moving marks text, from the place the button went down
to the place the cursor is at; past the upper or lower edge the text scrolls. A double click marks a word, a
third click the line. Shift and a click move the end of what is marked. Ctrl+A marks everything. Ctrl+C or the
right button put the marked text on the clipboard: without the colours, and with lines the console wrapped put
together again.

A place in the scrollback is a line, counted the way con.current counts them, and a column of the text in it:
0 is before the first cell after the timestamp. What is marked runs from one place up to another.

==============================================================================
*/

typedef struct conPos_s {
	int		line;
	int		column;
} conPos_t;

static struct {
	qboolean	active;			// text is marked: anchor and caret differ
	qboolean	dragging;		// the left button went down in the text and is held
	conPos_t	anchor;			// where the marking began
	conPos_t	caret;			// where it ends
	int			mouseX, mouseY;	// the cursor, pixels of the window
	qboolean	ownCursor;		// the game holds the mouse: the console draws the cursor
	int			clickTime;		// of the last press of the button, for double and triple clicks
	int			clickCount;
	conPos_t	clickPos;
	int			scrollTime;		// when marking past an edge scrolls the next step
	// where the text is on the screen, from the last frame that drew the console
	int			bottomY;		// top pixel of the lowest row of text
	int			bottomLine;		// the line in that row
	int			numRows;		// rows of text
	// what is marked, in order, from the last Con_SelectionBegin()
	qboolean	haveRange;
	conPos_t	rangeStart, rangeEnd;
} conSel;

#define CON_DOUBLE_CLICK_MSEC	400
#define CON_DRAG_SCROLL_MSEC	50

// cells of text in a row, without the timestamp and the mark of a wrapped line
static int Con_TextCells( void ) {
	return con.rowwidth - 1 - CON_TIMESTAMP_LEN;
}

static conChar_t *Con_LineText( int line ) {
	return con.text + ( line % con.totallines ) * con.rowwidth + CON_TIMESTAMP_LEN;
}

// whether the scrollback still has that line
static qboolean Con_LineValid( int line ) {
	return (qboolean)( line >= 0 && line <= con.current && con.current - line < con.totallines );
}

// the cells of a line up to its last one that is not blank
static int Con_LineLength( int line ) {
	const conChar_t	*text = Con_LineText( line );
	int				len = Con_TextCells();

	while ( len > 0 && text[len - 1].f.character == CON_BLANK_CHAR ) {
		len--;
	}
	return len;
}

// whether the console broke the line there because it was too long: the next row goes on with it
static qboolean Con_LineWrapped( int line ) {
	return (qboolean)( Con_LineText( line )[Con_TextCells()].compare == CON_WRAP.compare );
}

static int Con_ComparePos( const conPos_t *a, const conPos_t *b ) {
	return ( a->line != b->line ) ? a->line - b->line : a->column - b->column;
}

void Con_ClearSelection( void ) {
	conSel.active = conSel.dragging = qfalse;
}

// what is marked, in order. qfalse when nothing is, or when its lines have left the scrollback.
static qboolean Con_SelectionRange( conPos_t *start, conPos_t *end ) {
	if ( !conSel.active ) {
		return qfalse;
	}
	if ( Con_ComparePos( &conSel.anchor, &conSel.caret ) <= 0 ) {
		*start = conSel.anchor;
		*end = conSel.caret;
	} else {
		*start = conSel.caret;
		*end = conSel.anchor;
	}
	if ( !Con_LineValid( end->line ) ) {
		Con_ClearSelection();
		return qfalse;
	}
	if ( !Con_LineValid( start->line ) ) {
		start->line = Q_max( 0, con.current - con.totallines + 1 );
		start->column = 0;
	}
	return qtrue;
}

// the columns of a line that are marked, qfalse when none are
static qboolean Con_SelectedColumns( const conPos_t *start, const conPos_t *end, int line, int *from, int *to ) {
	if ( line < start->line || line > end->line ) {
		return qfalse;
	}
	*from = ( line == start->line ) ? start->column : 0;
	*to = ( line == end->line ) ? end->column : Con_TextCells();
	return (qboolean)( *to > *from );
}

// The place in the text the cursor is at: the line of the row it is in (the top or bottom row when it is above or
// below the text) and the gap between two cells it is nearest to. cell: the cell it is in, may be outside the row.
static void Con_MousePos( conPos_t *pos, int *cell ) {
	const int	shown = con_timestamps->integer ? CON_TIMESTAMP_LEN : 0;	// cells of the timestamp left of the text
	const int	fromBottom = conSel.bottomY + con.charHeight - 1 - conSel.mouseY;
	int			row = ( fromBottom > 0 ) ? fromBottom / con.charHeight : 0;

	if ( row > conSel.numRows - 1 ) {
		row = conSel.numRows - 1;
	}
	pos->line = Com_Clampi( Q_max( 0, con.current - con.totallines + 1 ), con.current, conSel.bottomLine - Q_max( 0, row ) );
	pos->column = Com_Clampi( 0, Con_TextCells(), ( conSel.mouseX + con.charWidth / 2 ) / con.charWidth - 1 - shown );
	if ( cell ) {
		*cell = conSel.mouseX / con.charWidth - 1 - shown;
	}
}

/*
================
Con_MouseEvent

The mouse moved while the console is down and the game holds the mouse (fullscreen): the console has a cursor of
its own then. In a window the place comes from the system, see Con_SelectionFrame().
================
*/
void Con_MouseEvent( int dx, int dy ) {
	conSel.mouseX = Com_Clampi( 0, cls.glconfig.vidWidth - 1, conSel.mouseX + dx );
	conSel.mouseY = Com_Clampi( 0, cls.glconfig.vidHeight - 1, conSel.mouseY + dy );
}

/*
================
Con_MouseDown

The left button went down
================
*/
void Con_MouseDown( void ) {
	conPos_t	pos;
	int			cell;

	// the rows of an Asian language are drawn as strings of another height, a place on screen is no cell there
	if ( conSel.numRows < 1 || re->Language_IsAsian() ) {
		return;
	}
	if ( conSel.mouseY >= conSel.bottomY + con.charHeight ) {
		// below the text, at the input line
		Con_ClearSelection();
		return;
	}
	Con_MousePos( &pos, &cell );

	if ( cls.realtime - conSel.clickTime < CON_DOUBLE_CLICK_MSEC && pos.line == conSel.clickPos.line
		&& abs( pos.column - conSel.clickPos.column ) <= 1 ) {
		conSel.clickCount++;
	} else {
		conSel.clickCount = 1;
	}
	conSel.clickTime = cls.realtime;
	conSel.clickPos = pos;
	conSel.dragging = qfalse;

	if ( conSel.clickCount == 2 ) {
		// the word under the cursor
		const conChar_t *text = Con_LineText( pos.line );

		if ( cell < 0 || cell >= Con_TextCells() || text[cell].f.character == CON_BLANK_CHAR ) {
			return;
		}
		conSel.anchor.line = conSel.caret.line = pos.line;
		for ( conSel.anchor.column = cell; conSel.anchor.column > 0 && text[conSel.anchor.column - 1].f.character != CON_BLANK_CHAR; conSel.anchor.column-- ) {
		}
		for ( conSel.caret.column = cell + 1; conSel.caret.column < Con_TextCells() && text[conSel.caret.column].f.character != CON_BLANK_CHAR; conSel.caret.column++ ) {
		}
		conSel.active = qtrue;
	} else if ( conSel.clickCount >= 3 ) {
		// the line, with the rows before and after it when the console wrapped it
		conSel.anchor.line = conSel.caret.line = pos.line;
		while ( Con_LineValid( conSel.anchor.line - 1 ) && Con_LineWrapped( conSel.anchor.line - 1 ) ) {
			conSel.anchor.line--;
		}
		while ( Con_LineWrapped( conSel.caret.line ) && Con_LineValid( conSel.caret.line + 1 ) ) {
			conSel.caret.line++;
		}
		conSel.anchor.column = 0;
		conSel.caret.column = Con_TextCells();
		conSel.active = qtrue;
	} else if ( kg.keys[A_SHIFT].down && conSel.active ) {
		conSel.caret = pos;
		conSel.dragging = qtrue;
	} else {
		conSel.anchor = conSel.caret = pos;
		conSel.active = qfalse;
		conSel.dragging = qtrue;
	}
}

/*
================
Con_SelectAll
================
*/
void Con_SelectAll( void ) {
	conSel.anchor.line = Q_max( 0, con.current - con.totallines + 1 );
	conSel.anchor.column = 0;
	// not the rows nothing was printed to yet
	while ( conSel.anchor.line < con.current && !Con_LineLength( conSel.anchor.line ) ) {
		conSel.anchor.line++;
	}
	conSel.caret.line = con.current;
	conSel.caret.column = Con_LineLength( con.current );
	conSel.dragging = qfalse;
	conSel.active = (qboolean)( Con_ComparePos( &conSel.anchor, &conSel.caret ) != 0 );
}

/*
================
Con_CopySelection

Puts what is marked on the clipboard. qfalse: nothing is marked.
================
*/
qboolean Con_CopySelection( void ) {
	conPos_t	start, end;
	char		*text;
	int			len = 0;

	if ( !Con_SelectionRange( &start, &end ) ) {
		return qfalse;
	}
	// a cell is a character of four bytes at most, a row ends with two at most
	text = (char *)Z_Malloc( ( end.line - start.line + 1 ) * ( Con_TextCells() * 4 + 2 ) + 1, TAG_TEMP_WORKSPACE, qfalse );

	for ( int line = start.line; line <= end.line; line++ ) {
		const conChar_t	*cell = Con_LineText( line );
		const qboolean	goesOn = (qboolean)( line != end.line && Con_LineWrapped( line ) );
		int				from, to;

		if ( !Con_SelectedColumns( &start, &end, line, &from, &to ) ) {
			from = to = 0;
		}
		// the blanks a row is filled up with are no text. A row that goes on in the next one is full.
		if ( !goesOn ) {
			while ( to > from && cell[to - 1].f.character == CON_BLANK_CHAR ) {
				to--;
			}
		}
		for ( int i = from; i < to; i++ ) {
			// the clipboard is UTF-8: a byte of a code page goes there as the character it is in Windows-1252
			len += Q_UTF8_Encode( CL_UTF8() ? cell[i].f.character : Q_CP1252ToUnicode( (int)cell[i].f.character ), text + len );
		}
		if ( line != end.line && !goesOn ) {
#ifdef _WIN32
			text[len++] = '\r';
#endif
			text[len++] = '\n';
		}
	}
	text[len] = '\0';
	Sys_SetClipboardData( text );
	Z_Free( text );
	return qtrue;
}

// once a frame while the console is drawn: where the cursor is, and what a held button marks
static void Con_SelectionFrame( void ) {
	static qboolean	placed = qfalse;
	int				x, y;

	conSel.ownCursor = (qboolean)!IN_MousePosition( &x, &y );
	if ( !conSel.ownCursor ) {
		conSel.mouseX = x;
		conSel.mouseY = y;
	} else if ( !placed ) {
		// the cursor of the console starts in the middle of its text
		conSel.mouseX = cls.glconfig.vidWidth / 2;
		conSel.mouseY = Q_max( 0, conSel.bottomY / 2 );
	}
	placed = qtrue;
	if ( !conSel.dragging ) {
		return;
	}
	if ( !kg.keys[A_MOUSE1].down || conSel.numRows < 1 ) {
		conSel.dragging = qfalse;
		return;
	}
	// past the upper or lower edge of the text it scrolls
	if ( cls.realtime >= conSel.scrollTime ) {
		if ( conSel.mouseY < conSel.bottomY - ( conSel.numRows - 1 ) * con.charHeight ) {
			Con_PageUp();
			conSel.scrollTime = cls.realtime + CON_DRAG_SCROLL_MSEC;
		} else if ( conSel.mouseY >= conSel.bottomY + con.charHeight ) {
			Con_PageDown();
			conSel.scrollTime = cls.realtime + CON_DRAG_SCROLL_MSEC;
		}
	}
	Con_MousePos( &conSel.caret, NULL );
	conSel.active = (qboolean)( Con_ComparePos( &conSel.anchor, &conSel.caret ) != 0 );
}

/*
================
Con_SelectionBegin

For the drawing of the console, before its rows: where the lowest row of text is (its top pixel and its line) and
how many rows there are, for the mouse. Then what the mouse does this frame. qtrue: something is marked.
================
*/
qboolean Con_SelectionBegin( int bottomY, int bottomLine, int numRows ) {
	conSel.bottomY = bottomY;
	conSel.bottomLine = bottomLine;
	conSel.numRows = numRows;
	Con_SelectionFrame();
	conSel.haveRange = Con_SelectionRange( &conSel.rangeStart, &conSel.rangeEnd );
	return conSel.haveRange;
}

// the columns of a row that are drawn as marked: up to the end of its text, and a cell at least so an empty line
// shows that it is marked (not in the last row of what is marked). qfalse: none.
qboolean Con_SelectionRow( int row, int *from, int *to ) {
	if ( !Con_SelectedColumns( &conSel.rangeStart, &conSel.rangeEnd, row, from, to ) ) {
		return qfalse;
	}
	if ( row != conSel.rangeEnd.line ) {
		*to = Q_max( Q_min( *to, Con_LineLength( row ) ), *from + 1 );
	}
	return qtrue;
}

// the cursor of the console in pixels of the window, qtrue where the system does not draw one
qboolean Con_SelectionCursor( int *x, int *y ) {
	*x = conSel.mouseX;
	*y = conSel.mouseY;
	return conSel.ownCursor;
}
