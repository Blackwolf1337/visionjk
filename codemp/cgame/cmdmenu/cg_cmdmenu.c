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

// cg_cmdmenu.c -- the command menu (v_commandmenu): lists of what can be done at the left of the screen, as the
// command menu of Half-Life has them (issue #2). A list stands beside the entry of the one before that opened it.
// A number key picks the entry with that number in the last list, the mouse opens a list by pointing at its entry
// and does an entry by a click. Backspace goes back one list, Escape and the right button close it all. Not from
// JA+, see docs/japlus/ui.md, "The command menu".
//
// What is in the lists is a file, ui/vision/commandmenu.txt, read each time the menu opens (so a change to it
// counts at once). The pk3 of VisionJK has one; the same path in the japlus folder of the home folder comes first.
// A line is an entry:
//   "<key>" "<name>" "<command>"     the key picks it, the command is run as if typed into the console
//   "<key>" "<name>" { ... }         a list of its own, the entries between the braces
// A name is looked up in the texts of VisionJK by its English (CG_VisionText), so it shows in the language of the
// game where tools/menus/texts.py has it.
//
// The menu is a mode of the pointer (select/cg_select.c), as the quick wheel is: the pointer takes the keys and
// the mouse for it, draws it, and hands it every key first.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"

#define CMD_FILE			"ui/vision/commandmenu.txt"
#define CMD_FILE_MAX		( 32 * 1024 )
#define CMD_ENTRIES			512
#define CMD_DEPTH			6			// lists that can stand beside each other
#define CMD_NAME			64
#define CMD_COMMAND			256
#define CMD_LEFT			8.0f		// where the first list stands, in the 2D of the pointer
#define CMD_TOP				110.0f
#define CMD_ROW_H			24.0f		// an entry
#define CMD_LIST_W			185.0f		// a list
#define CMD_GAP				2.0f		// between two lists

typedef struct {
	char	key;					// '0' to '9', or another letter of the file
	char	name[CMD_NAME];
	char	command[CMD_COMMAND];	// "": the entry opens a list
	int		first;					// the first entry of its list, -1: none
	int		next;					// the next entry of the same list, -1: none
} cmdEntry_t;

static struct {
	qboolean	open;
	cmdEntry_t	entries[CMD_ENTRIES + 1];	// 0 is the top that holds the first list
	int			count;
	int			path[CMD_DEPTH];			// the entries whose lists stand open, one after the other
	int			depth;						// how many of those there are
	int			hover;						// the entry under the pointer, -1: none
	int			pointerX, pointerY;			// where the pointer was in the frame before
} cmd;

static const vec4_t cmdRow = { 0.03f, 0.06f, 0.10f, 0.82f }, cmdEdge = { 0.43f, 0.83f, 1.0f, 0.22f };
static const vec4_t cmdLit = { 0.43f, 0.83f, 1.0f, 0.30f }, cmdText = { 0.86f, 0.92f, 0.97f, 1.0f };
static const vec4_t cmdKey = { 0.43f, 0.83f, 1.0f, 1.0f };

/*
===============================================================================

The file

===============================================================================
*/

static int Cmd_NewEntry( int parent, int *last ) {
	cmdEntry_t *e;

	if ( cmd.count >= CMD_ENTRIES ) {
		return -1;
	}
	e = &cmd.entries[++cmd.count];
	memset( e, 0, sizeof( *e ) );
	e->first = e->next = -1;
	if ( *last >= 0 ) {
		cmd.entries[*last].next = cmd.count;
	} else {
		cmd.entries[parent].first = cmd.count;
	}
	*last = cmd.count;
	return cmd.count;
}

// the entries of one list, up to its closing brace or the end of the file. qfalse: the file is wrong there.
static qboolean Cmd_ParseList( const char **p, int parent, int depth ) {
	int last = -1;

	for ( ;; ) {
		const char	*token = COM_ParseExt( p, qtrue );
		int			n;

		if ( !token[0] ) {
			return (qboolean)( depth == 0 );
		}
		if ( !strcmp( token, "}" ) ) {
			return (qboolean)( depth > 0 );
		}
		n = Cmd_NewEntry( parent, &last );
		if ( n < 0 ) {
			trap->Print( S_COLOR_YELLOW "Command menu: more than %i entries, the rest is left out\n", CMD_ENTRIES );
			return qtrue;
		}
		cmd.entries[n].key = token[0];
		Q_strncpyz( cmd.entries[n].name, COM_ParseExt( p, qfalse ), sizeof( cmd.entries[n].name ) );
		token = COM_ParseExt( p, qtrue );
		if ( !strcmp( token, "{" ) ) {
			if ( depth + 1 >= CMD_DEPTH || !Cmd_ParseList( p, n, depth + 1 ) ) {
				return qfalse;
			}
		} else if ( token[0] ) {
			Q_strncpyz( cmd.entries[n].command, token, sizeof( cmd.entries[n].command ) );
		} else {
			return qfalse;
		}
	}
}

// reads the file anew; qfalse: there is none, or nothing in it can be used
static qboolean Cmd_Load( void ) {
	static char	text[CMD_FILE_MAX];
	fileHandle_t f;
	const char	*p = text;
	const int	len = trap->FS_Open( CMD_FILE, &f, FS_READ );

	memset( &cmd.entries[0], 0, sizeof( cmd.entries[0] ) );
	cmd.entries[0].first = cmd.entries[0].next = -1;
	cmd.count = 0;
	if ( !f ) {
		trap->Print( S_COLOR_YELLOW "Command menu: there is no " CMD_FILE "\n" );
		return qfalse;
	}
	if ( len <= 0 || len >= CMD_FILE_MAX ) {
		trap->FS_Close( f );
		trap->Print( S_COLOR_YELLOW "Command menu: " CMD_FILE " is empty or larger than %i bytes\n", CMD_FILE_MAX - 1 );
		return qfalse;
	}
	trap->FS_Read( text, len, f );
	trap->FS_Close( f );
	text[len] = '\0';
	COM_BeginParseSession( CMD_FILE );
	if ( !Cmd_ParseList( &p, 0, 0 ) ) {
		trap->Print( S_COLOR_YELLOW "Command menu: " CMD_FILE " is wrong near line %i, what came before it is used\n",
			COM_GetCurrentParseLine() );
	}
	return (qboolean)( cmd.entries[0].first >= 0 );
}

/*
===============================================================================

Open, close, keys

===============================================================================
*/

qboolean CG_CmdMenuOpen( void ) {
	return cmd.open;
}

void CG_CmdMenuClose( void ) {
	cmd.open = qfalse;
	cmd.depth = 0;
	cmd.hover = -1;
}

// v_commandmenu: opens it, or closes it when it is open
void CG_CmdMenu_f( void ) {
	if ( cmd.open ) {
		CG_CmdMenuClose();
		return;
	}
	if ( !cg.snap || cg.demoPlayback || CG_ChatIsOpen() || !Cmd_Load() ) {
		return;
	}
	CG_RadialClose();
	cmd.open = qtrue;
	cmd.depth = 0;
	cmd.hover = -1;
	cmd.pointerX = cgs.cursorX;
	cmd.pointerY = cgs.cursorY;
}

// the list that stands at column n: the first one, or the one the entry before it opened
static int Cmd_ListOf( int column ) {
	return column == 0 ? 0 : cmd.path[column - 1];
}

// An entry is picked: its list opens beside it, or its command runs and the menu closes. column: where it stands.
static void Cmd_Pick( int column, int n ) {
	const cmdEntry_t *e = &cmd.entries[n];

	if ( !e->command[0] ) {
		if ( e->first >= 0 && column < CMD_DEPTH ) {
			cmd.path[column] = n;
			cmd.depth = column + 1;
		}
		return;
	}
	trap->SendConsoleCommand( va( "%s\n", e->command ) );
	CG_CmdMenuClose();
}

// the entry with this key in the last list that stands open, -1: none
static int Cmd_ByKey( char key ) {
	int n;

	for ( n = cmd.entries[Cmd_ListOf( cmd.depth )].first; n >= 0; n = cmd.entries[n].next ) {
		if ( cmd.entries[n].key == key ) {
			return n;
		}
	}
	return -1;
}

// the column of the entry under the pointer, as it was drawn (Cmd_Draw sets cmd.hover)
static int Cmd_ColumnOf( int n ) {
	int column;

	for ( column = 0; column <= cmd.depth; column++ ) {
		int i;

		for ( i = cmd.entries[Cmd_ListOf( column )].first; i >= 0; i = cmd.entries[i].next ) {
			if ( i == n ) {
				return column;
			}
		}
	}
	return -1;
}

// a key while the menu is open: everything is taken
qboolean CG_CmdMenuKey( int key, qboolean down ) {
	char digit = 0;

	if ( !cmd.open ) {
		return qfalse;
	}
	if ( !down ) {
		return qtrue;
	}
	if ( key >= A_0 && key <= A_9 ) {
		digit = (char)( '0' + key - A_0 );
	} else if ( key >= A_KP_0 && key <= A_KP_9 ) {
		digit = (char)( '0' + key - A_KP_0 );
	}
	if ( digit ) {
		const int n = Cmd_ByKey( digit );

		if ( n >= 0 ) {
			Cmd_Pick( cmd.depth, n );
		}
	} else if ( key == A_MOUSE1 ) {
		if ( cmd.hover >= 0 ) {
			Cmd_Pick( Cmd_ColumnOf( cmd.hover ), cmd.hover );
		}
	} else if ( key == A_BACKSPACE ) {
		if ( cmd.depth > 0 ) {
			cmd.depth--;
		} else {
			CG_CmdMenuClose();
		}
	} else if ( key == A_MOUSE2 || key == A_ESCAPE ) {
		CG_CmdMenuClose();
	}
	return qtrue;
}

/*
===============================================================================

Drawing

===============================================================================
*/

// One list at x, its first entry at y: the number at the left, the name, and an arrow at the right where an entry
// opens a list. The entry under the pointer is lit and, when the pointer moved onto it, opens its list. (A pointer
// that stands still leaves the lists the number keys opened as they are.)
static void Cmd_DrawList( int column, float x, float y, qboolean moved, float *openY ) {
	const int	font = Skin_Ready() ? SKIN_FONT_SANS : FONT_SMALL2;
	const float	scale = 0.62f;
	int			n, row = 0;

	for ( n = cmd.entries[Cmd_ListOf( column )].first; n >= 0; n = cmd.entries[n].next, row++ ) {
		const cmdEntry_t	*e = &cmd.entries[n];
		const float			top = y + row * CMD_ROW_H;
		const qboolean		under = (qboolean)( cgs.cursorX >= x && cgs.cursorX < x + CMD_LIST_W
								&& cgs.cursorY >= top && cgs.cursorY < top + CMD_ROW_H );
		const qboolean		opened = (qboolean)( column < cmd.depth && cmd.path[column] == n );
		char				key[2] = { e->key, '\0' };

		if ( under ) {
			cmd.hover = n;
		}
		if ( under && moved ) {
			// pointing at an entry opens its list, or closes the lists after this one
			if ( !e->command[0] ) {
				Cmd_Pick( column, n );
			} else if ( cmd.depth > column ) {
				cmd.depth = column;
			}
		}
		if ( Skin_Ready() ) {
			Skin_Panel( x, top, CMD_LIST_W, CMD_ROW_H - 1.0f, 2.0f, ( under || opened ) ? cmdLit : cmdRow, cmdEdge );
		} else {
			CG_FillRect( x, top, CMD_LIST_W, CMD_ROW_H - 1.0f, ( under || opened ) ? cmdLit : cmdRow );
		}
		CG_Text_Paint( x + 8.0f, top + 4.0f, scale, (float *)cmdKey, key, 0, 0, 0, font );
		CG_Text_Paint( x + 24.0f, top + 4.0f, scale, (float *)cmdText, CG_VisionText( e->name ), 0, 0, 0, font );
		if ( !e->command[0] && Skin_Ready() ) {
			Skin_Arrow( x + CMD_LIST_W - 12.0f, top + CMD_ROW_H * 0.5f - 4.5f, 5.0f, 8.0f, qfalse, cmdText );
		}
		if ( opened ) {
			*openY = top;
		}
	}
}

/*
=================
CG_CmdMenuDraw

The lists that stand open, from the left of the screen, each beside the entry that opened it. In the 2D of the
pointer (PLACE_CENTER).
=================
*/
void CG_CmdMenuDraw( void ) {
	const qboolean	moved = (qboolean)( cgs.cursorX != cmd.pointerX || cgs.cursorY != cmd.pointerY );
	float			x = CMD_LEFT, y = CMD_TOP;
	int				column;

	if ( !cmd.open ) {
		return;
	}
	cmd.pointerX = cgs.cursorX;
	cmd.pointerY = cgs.cursorY;
	cmd.hover = -1;
	for ( column = 0; column <= cmd.depth && column <= CMD_DEPTH; column++ ) {
		float		openY = y;
		int			rows = 0, n;

		// (a list does not run off the bottom of the screen)
		for ( n = cmd.entries[Cmd_ListOf( column )].first; n >= 0; n = cmd.entries[n].next ) {
			rows++;
		}
		y = Q_min( y, SCREEN_HEIGHT - 8.0f - rows * CMD_ROW_H );
		y = Q_max( y, 8.0f );
		Cmd_DrawList( column, x, y, moved, &openY );
		x += CMD_LIST_W + CMD_GAP;
		y = openY;
	}
	CG_SelectDrawCursor();
}
