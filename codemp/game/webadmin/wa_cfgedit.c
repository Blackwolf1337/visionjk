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

// wa_cfgedit.c -- a config of the game as statements, for the settings form and the cleanup of the web panel
// (wa_settings.c). Not from JA+. See wa_cfgedit.h.

#include "qcommon/q_shared.h"
#include "game/webadmin/wa_cfgedit.h"

#define WA_CFG_BLOCK		"// set by the web panel"

static qboolean CfgLineEnd( char ch ) {
	return (qboolean)( ch == '\n' || ch == '\r' );
}

static qboolean CfgComment( const char *text, int len, int p ) {
	return (qboolean)( text[p] == '/' && p + 1 < len && text[p + 1] == '/' );
}

// The next word of a statement from *p on, as Cmd_TokenizeString takes it. qfalse at the end of the statement,
// *p is then on what ends it: the ';', the line break, the comment or the end of the text.
static qboolean CfgWord( const char *text, int len, int *p, int *ws, int *we ) {
	int i = *p;

	while ( i < len && !CfgLineEnd( text[i] ) && (unsigned char)text[i] <= ' ' )
		i++;
	*p = i;
	if ( i >= len || CfgLineEnd( text[i] ) || text[i] == ';' || CfgComment( text, len, i ) )
		return qfalse;

	*ws = i;
	if ( text[i] == '"' ) {
		// a line break ends a quote that was not closed, as it ends the statement
		i++;
		while ( i < len && text[i] != '"' && !CfgLineEnd( text[i] ) )
			i++;
		if ( i < len && text[i] == '"' )
			i++;
	} else {
		while ( i < len && (unsigned char)text[i] > ' ' && text[i] != '"' && text[i] != ';' && !CfgComment( text, len, i ) )
			i++;
	}
	*we = i;
	*p = i;
	return qtrue;
}

// a word without its quotes
static void CfgCopyWord( const char *text, int ws, int we, char *out, int outSize ) {
	int n;

	if ( text[ws] == '"' ) {
		ws++;
		if ( we > ws && text[we - 1] == '"' )
			we--;
	}
	n = we - ws;
	if ( n > outSize - 1 )
		n = outSize - 1;
	if ( n < 0 )
		n = 0;
	memcpy( out, text + ws, n );
	out[n] = '\0';
}

qboolean WA_CfgNext( const char *text, int len, int *pos, int *line, waCfgStatement_t *st ) {
	int p = *pos, ws, we;

	// blanks, empty statements, comments and line breaks before it
	for ( ;; ) {
		if ( p >= len ) {
			*pos = p;
			return qfalse;
		}
		if ( text[p] == '\n' ) {
			( *line )++;
			p++;
		} else if ( CfgComment( text, len, p ) ) {
			while ( p < len && text[p] != '\n' )
				p++;
		} else if ( text[p] == ';' || (unsigned char)text[p] <= ' ' ) {
			p++;
		} else {
			break;
		}
	}

	memset( st, 0, sizeof( *st ) );
	st->start = st->end = p;
	st->line = *line;
	st->lineStart = p;
	while ( st->lineStart > 0 && text[st->lineStart - 1] != '\n' )
		st->lineStart--;

	while ( CfgWord( text, len, &p, &ws, &we ) ) {
		if ( st->words < WA_CFG_WORDS ) {
			st->wordStart[st->words] = ws;
			st->wordEnd[st->words] = we;
		}
		st->words++;
		st->end = we;
	}
	st->next = ( p < len && text[p] == ';' ) ? p + 1 : p;

	*pos = st->next;
	return qtrue;
}

qboolean WA_CfgSetter( const char *text, const waCfgStatement_t *st, char *name, int nameSize ) {
	static const char *const setters[] = { "set", "seta", "sets", "setu" };
	char	cmd[8];
	int		i;

	if ( st->words < 2 )
		return qfalse;
	CfgCopyWord( text, st->wordStart[0], st->wordEnd[0], cmd, sizeof( cmd ) );
	for ( i = 0; i < (int)ARRAY_LEN( setters ); i++ ) {
		if ( !Q_stricmp( cmd, setters[i] ) ) {
			CfgCopyWord( text, st->wordStart[1], st->wordEnd[1], name, nameSize );
			return qtrue;
		}
	}
	return qfalse;
}

void WA_CfgValue( const char *text, const waCfgStatement_t *st, int first, char *out, int outSize ) {
	int p, ws, we, n = 0;

	out[0] = '\0';
	if ( first >= st->words || first >= WA_CFG_WORDS || outSize < 1 )
		return;
	p = st->wordStart[first];
	while ( p < st->end && CfgWord( text, st->end, &p, &ws, &we ) ) {
		if ( n && n < outSize - 1 )
			out[n++] = ' ';
		CfgCopyWord( text, ws, we, out + n, outSize - n );
		n += (int)strlen( out + n );
	}
}

// The last statement that sets name: with set and the others, or as "name value". *first: the word its value
// begins with.
static qboolean CfgFind( const char *text, int len, const char *name, waCfgStatement_t *found, int *first ) {
	waCfgStatement_t	st;
	char				word[MAX_CVAR_VALUE_STRING];
	int					pos = 0, line = 1;
	qboolean			any = qfalse;

	while ( WA_CfgNext( text, len, &pos, &line, &st ) ) {
		if ( WA_CfgSetter( text, &st, word, sizeof( word ) ) ) {
			if ( !Q_stricmp( word, name ) ) {
				*found = st;
				*first = 2;
				any = qtrue;
			}
			continue;
		}
		CfgCopyWord( text, st.wordStart[0], st.wordEnd[0], word, sizeof( word ) );
		if ( st.words >= 2 && !Q_stricmp( word, name ) ) {
			*found = st;
			*first = 1;
			any = qtrue;
		}
	}
	return any;
}

qboolean WA_CfgGetValue( const char *text, int len, const char *name, char *out, int outSize ) {
	waCfgStatement_t	st;
	int					first;

	if ( !CfgFind( text, len, name, &st, &first ) ) {
		out[0] = '\0';
		return qfalse;
	}
	WA_CfgValue( text, &st, first, out, outSize );
	return qtrue;
}

// out of room is remembered in *n as -1
static void CfgPut( char *out, int outSize, int *n, const char *data, int len ) {
	if ( *n < 0 )
		return;
	if ( *n + len > outSize ) {
		*n = -1;
		return;
	}
	memcpy( out + *n, data, len );
	*n += len;
}

static void CfgPuts( char *out, int outSize, int *n, const char *s ) {
	CfgPut( out, outSize, n, s, (int)strlen( s ) );
}

// the text of a file has no 0 at its end
static qboolean CfgContains( const char *text, int len, const char *what ) {
	const int	n = (int)strlen( what );
	int			i;

	for ( i = 0; i + n <= len; i++ ) {
		if ( !memcmp( text + i, what, n ) )
			return qtrue;
	}
	return qfalse;
}

int WA_CfgSetValue( const char *text, int len, char *out, int outSize, const char *name, const char *value ) {
	waCfgStatement_t	st;
	int					first, n = 0;

	if ( CfgFind( text, len, name, &st, &first ) ) {
		CfgPut( out, outSize, &n, text, st.words > first ? st.wordStart[first] : st.end );
		CfgPuts( out, outSize, &n, st.words > first ? "\"" : " \"" );
		CfgPuts( out, outSize, &n, value );
		CfgPuts( out, outSize, &n, "\"" );
		CfgPut( out, outSize, &n, text + st.end, len - st.end );
		return n;
	}

	CfgPut( out, outSize, &n, text, len );
	if ( len && text[len - 1] != '\n' )
		CfgPuts( out, outSize, &n, "\n" );
	if ( !CfgContains( text, len, WA_CFG_BLOCK ) )
		CfgPuts( out, outSize, &n, len ? "\n" WA_CFG_BLOCK "\n" : WA_CFG_BLOCK "\n" );
	CfgPuts( out, outSize, &n, "seta " );
	CfgPuts( out, outSize, &n, name );
	CfgPuts( out, outSize, &n, " \"" );
	CfgPuts( out, outSize, &n, value );
	CfgPuts( out, outSize, &n, "\"\n" );
	return n;
}

int WA_CfgRemove( const char *text, int len, char *out, int outSize, waCfgPick_t pick, void *context ) {
	waCfgStatement_t	st;
	int					pos = 0, line = 1, copied = 0, n = 0;
	int					wholeLine = -1;		// the line that goes as a whole, -1 for none

	while ( WA_CfgNext( text, len, &pos, &line, &st ) ) {
		int from, to;

		if ( !pick( text, &st, context ) )
			continue;

		if ( st.line == wholeLine ) {
			continue;
		}
		if ( st.lineStart >= copied ) {
			// the first statement of its line that goes: do all the others of the line go as well?
			waCfgStatement_t	other;
			int					p = st.lineStart, l = st.line;
			qboolean			all = qtrue;

			while ( WA_CfgNext( text, len, &p, &l, &other ) && other.line == st.line ) {
				if ( !pick( text, &other, context ) ) {
					all = qfalse;
					break;
				}
			}
			if ( all ) {
				from = st.lineStart;
				for ( to = st.end; to < len && text[to] != '\n'; to++ )
					;
				if ( to < len )
					to++;
				CfgPut( out, outSize, &n, text + copied, from - copied );
				copied = to;
				wholeLine = st.line;
				continue;
			}
		}

		from = st.start;
		to = st.next;
		if ( to > st.end && text[to - 1] == ';' ) {
			// "set a 1; set b 2" without a: the blanks after the ';' go too
			while ( to < len && ( text[to] == ' ' || text[to] == '\t' ) )
				to++;
		} else {
			// without b: the ';' before it and the blanks around it, what follows it stays as it is
			to = st.end;
			while ( from > copied && ( text[from - 1] == ' ' || text[from - 1] == '\t' ) )
				from--;
			if ( from > copied && text[from - 1] == ';' )
				from--;
			while ( from > copied && ( text[from - 1] == ' ' || text[from - 1] == '\t' ) )
				from--;
		}
		if ( from < copied )
			from = copied;
		CfgPut( out, outSize, &n, text + copied, from - copied );
		copied = to;
	}
	CfgPut( out, outSize, &n, text + copied, len - copied );
	return n;
}
