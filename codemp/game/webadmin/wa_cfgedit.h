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

// wa_cfgedit.h -- a config of the game as statements, read the way the console reads it (Cbuf_Execute and
// Cmd_TokenizeString): a statement ends at a ';' or a line break, "..." is one word, // begins a comment, a ';'
// in quotes or in a comment ends nothing. No trap and no allocation, for the unit tests. Read after q_shared.h.

#pragma once

#define WA_CFG_WORDS		3

typedef struct waCfgStatement_s {
	int			start, end;		// the statement, without the ';' or the comment after it, no blanks at its end
	int			next;			// where the one after it may begin
	int			line;			// 1 for the first
	int			lineStart;		// where its line begins
	int			words;			// how many words it has
	int			wordStart[WA_CFG_WORDS], wordEnd[WA_CFG_WORDS];	// of the first ones, a quoted word with its quotes
} waCfgStatement_t;

// The next statement from *pos on, *pos and *line (1 for the first line) are moved past it. qfalse at the end.
qboolean WA_CfgNext( const char *text, int len, int *pos, int *line, waCfgStatement_t *st );

// The cvar a set, seta, sets or setu statement sets, into name. qfalse for any other statement.
qboolean WA_CfgSetter( const char *text, const waCfgStatement_t *st, char *name, int nameSize );

// What a statement gives a cvar: its words from the one after the name on, without quotes, a blank between them,
// as Cmd_ArgsFrom puts them together. first: the word the value begins with (2 for set, 1 for "name value").
void WA_CfgValue( const char *text, const waCfgStatement_t *st, int first, char *out, int outSize );

// The value the last statement of the text that sets name gives it. qfalse when none does.
qboolean WA_CfgGetValue( const char *text, int len, const char *name, char *out, int outSize );

// The text with the last statement that sets name changed to the value, or a "seta" line for it added in the
// block of the web panel at the end. The rest of the text stays as it is, byte for byte. Returns the length of
// out, -1 when it has no room.
int WA_CfgSetValue( const char *text, int len, char *out, int outSize, const char *name, const char *value );

// The text without the statements pick says yes to. A line all of whose statements go goes with them, comment
// and all, else only the statement and its ';' go. Returns the length of out, -1 when it has no room.
typedef qboolean ( *waCfgPick_t )( const char *text, const waCfgStatement_t *st, void *context );
int WA_CfgRemove( const char *text, int len, char *out, int outSize, waCfgPick_t pick, void *context );
