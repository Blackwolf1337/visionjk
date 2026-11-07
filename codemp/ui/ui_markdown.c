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

// ui_markdown.c -- the entries of the changelog and their small Markdown, see ui_markdown.h. The rules are the ones
// of tools/changelog.py of the website, which makes the same text into the changelog page: a change of one is a
// change of the other.

#include "qcommon/q_shared.h"
#include "ui_markdown.h"

#define MD_RELEASE		( 1 << 30 )		// the fourth number of a release: after every development build of it

// where the line from p ends (its '\n', or end)
static const char *MD_LineEnd( const char *p, const char *end ) {
	while ( p < end && *p != '\n' )
		p++;
	return p;
}

// the end of a line without the blanks and the '\r' at its end
static const char *MD_Trim( const char *p, const char *e ) {
	while ( e > p && ( e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' ) )
		e--;
	return e;
}

static const char *MD_SkipBlanks( const char *p, const char *e ) {
	while ( p < e && ( *p == ' ' || *p == '\t' ) )
		p++;
	return p;
}

static qboolean MD_Starts( const char *p, const char *end, const char *with ) {
	const size_t n = strlen( with );

	return ( (size_t)( end - p ) >= n && !strncmp( p, with, n ) ) ? qtrue : qfalse;
}

static void MD_Copy( char *out, int size, const char *in, int len ) {
	if ( len > size - 1 )
		len = size - 1;
	if ( len < 0 )
		len = 0;
	memcpy( out, in, (size_t)len );
	out[len] = '\0';
}

// the line from p is "---", and the next one starts with "version:": the start of an entry
static qboolean MD_EntryStart( const char *p, const char *end ) {
	const char *e = MD_LineEnd( p, end );

	if ( MD_Trim( p, e ) - p != 3 || strncmp( p, "---", 3 ) || e >= end )
		return qfalse;
	return MD_Starts( e + 1, end, "version:" );
}

/*
=================
MD_ReadEntries
=================
*/
int MD_ReadEntries( const char *text, int len, mdEntry_t *entries, int max ) {
	const char	*p = text, *end = text + ( len > 0 ? len : 0 );
	int			count = 0;

	while ( p < end && count < max ) {
		mdEntry_t	*entry = &entries[count];
		const char	*e;
		int			v[4];
		qboolean	closed = qfalse;

		if ( !MD_EntryStart( p, end ) ) {
			e = MD_LineEnd( p, end );
			p = ( e < end ) ? e + 1 : end;
			continue;
		}
		memset( entry, 0, sizeof( *entry ) );
		p = MD_LineEnd( p, end ) + 1;

		// the front matter, up to its second "---"
		while ( p < end ) {
			const char	*colon, *value;

			e = MD_LineEnd( p, end );
			if ( MD_Trim( p, e ) - p == 3 && !strncmp( p, "---", 3 ) ) {
				p = ( e < end ) ? e + 1 : end;
				closed = qtrue;
				break;
			}
			for ( colon = p; colon < e && *colon != ':'; colon++ ) {
			}
			if ( colon < e ) {
				value = MD_SkipBlanks( colon + 1, e );
				if ( colon - p == 7 && !Q_stricmpn( p, "version", 7 ) )
					MD_Copy( entry->version, sizeof( entry->version ), value, (int)( MD_Trim( value, e ) - value ) );
				else if ( colon - p == 4 && !Q_stricmpn( p, "date", 4 ) )
					MD_Copy( entry->date, sizeof( entry->date ), value, (int)( MD_Trim( value, e ) - value ) );
				else if ( colon - p == 7 && !Q_stricmpn( p, "summary", 7 ) )
					MD_Copy( entry->summary, sizeof( entry->summary ), value, (int)( MD_Trim( value, e ) - value ) );
			}
			p = ( e < end ) ? e + 1 : end;
		}
		if ( !closed )
			break;

		// the body, up to the next entry
		entry->body = p;
		while ( p < end && !MD_EntryStart( p, end ) ) {
			e = MD_LineEnd( p, end );
			p = ( e < end ) ? e + 1 : end;
		}
		entry->bodyLen = (int)( p - entry->body );
		if ( MD_ParseVersion( entry->version, v ) )
			count++;
	}
	return count;
}

/*
===============================================================================

Blocks

===============================================================================
*/

void MD_StartBlocks( mdReader_t *reader, const char *body, int len ) {
	reader->p = body;
	reader->end = body + ( len > 0 ? len : 0 );
	reader->last = MD_NONE;
}

// what a line is: its blanks in front, and whether it is a line of a list or a heading
typedef struct {
	const char	*start, *end, *next;	// the line without the blanks at its ends, and the line after it
	int			indent;
	qboolean	blank, item, heading;
} mdLine_t;

static void MD_Line( const char *p, const char *end, mdLine_t *line ) {
	const char *e = MD_LineEnd( p, end );

	line->next = ( e < end ) ? e + 1 : end;
	line->end = MD_Trim( p, e );
	for ( line->start = p; line->start < line->end && *line->start == ' '; line->start++ ) {
	}
	line->indent = (int)( line->start - p );
	line->blank = ( MD_SkipBlanks( line->start, line->end ) >= line->end ) ? qtrue : qfalse;
	line->item = ( !line->blank && line->end - line->start >= 2 && ( line->start[0] == '-' || line->start[0] == '*' )
		&& line->start[1] == ' ' ) ? qtrue : qfalse;
	line->heading = ( !line->blank && !line->indent && ( MD_Starts( line->start, line->end, "## " )
		|| MD_Starts( line->start, line->end, "### " ) ) ) ? qtrue : qfalse;
}

/*
=================
MD_NextBlock

A line of a list goes on over the lines below it that are two spaces in and are no line of a list themselves. A
paragraph goes on over every line below it up to an empty one, a line of a list or a heading. A heading is one
line.
=================
*/
qboolean MD_NextBlock( mdReader_t *reader, mdBlock_t *block ) {
	mdLine_t	line;
	const char	*last;

	for ( ;; ) {
		if ( reader->p >= reader->end )
			return qfalse;
		MD_Line( reader->p, reader->end, &line );
		if ( !line.blank )
			break;
		reader->last = MD_NONE;
		reader->p = line.next;
	}

	if ( line.item ) {
		block->type = ( line.indent >= 2 && ( reader->last == MD_ITEM || reader->last == MD_SUBITEM ) ) ? MD_SUBITEM : MD_ITEM;
		block->text = MD_SkipBlanks( line.start + 2, line.end );
	} else if ( line.heading ) {
		block->type = ( line.start[2] == '#' ) ? MD_SUBHEADING : MD_HEADING;
		block->text = MD_SkipBlanks( line.start + ( block->type == MD_SUBHEADING ? 4 : 3 ), line.end );
		block->len = (int)( line.end - block->text );
		reader->last = block->type;
		reader->p = line.next;
		return qtrue;
	} else {
		block->type = MD_PARAGRAPH;
		block->text = line.start;
	}

	last = line.end;
	reader->p = line.next;
	while ( reader->p < reader->end ) {
		MD_Line( reader->p, reader->end, &line );
		if ( line.blank || line.item || ( block->type == MD_PARAGRAPH ? line.heading : line.indent < 2 ) )
			break;
		last = line.end;
		reader->p = line.next;
	}
	block->len = (int)( last - block->text );
	if ( block->len < 0 )
		block->len = 0;
	reader->last = block->type;
	return qtrue;
}

/*
===============================================================================

Spans

===============================================================================
*/

static const char *MD_Find( const char *p, const char *end, const char *what ) {
	const size_t n = strlen( what );

	for ( ; (size_t)( end - p ) >= n; p++ ) {
		if ( !strncmp( p, what, n ) )
			return p;
	}
	return NULL;
}

static qboolean MD_IsSpace( char c ) {
	return ( c == ' ' || c == '\t' || c == '\n' || c == '\r' ) ? qtrue : qfalse;
}

static qboolean MD_AddressAt( const char *p, const char *end ) {
	return ( MD_Starts( p, end, "https://" ) || MD_Starts( p, end, "http://" ) ) ? qtrue : qfalse;
}

static void MD_Set( mdSpan_t *span, mdStyle_t style, const char *text, int len ) {
	span->style = style;
	span->text = text;
	span->len = len;
	span->url = NULL;
	span->urlLen = 0;
}

// [text](https://...): the text has no ']' and no line break, the address no ')' and no blank
static qboolean MD_Link( const char *s, const char *end, mdSpan_t *span, const char **after ) {
	const char *close, *url, *u;

	for ( close = s + 1; close < end && *close != ']' && *close != '\n'; close++ ) {
	}
	if ( close >= end || *close != ']' || close == s + 1 || close + 1 >= end || close[1] != '(' )
		return qfalse;
	url = close + 2;
	if ( !MD_AddressAt( url, end ) )
		return qfalse;
	u = url + ( url[4] == 's' ? 8 : 7 );
	if ( u >= end || *u == ')' || MD_IsSpace( *u ) )
		return qfalse;
	while ( u < end && *u != ')' && !MD_IsSpace( *u ) )
		u++;
	if ( u >= end || *u != ')' )
		return qfalse;
	MD_Set( span, MD_LINK, s + 1, (int)( close - s - 1 ) );
	span->url = url;
	span->urlLen = (int)( u - url );
	*after = u + 1;
	return qtrue;
}

/*
=================
MD_NextSpan
=================
*/
qboolean MD_NextSpan( const char **p, const char *end, mdSpan_t *span ) {
	const char	*s = *p, *j;

	if ( s >= end )
		return qfalse;

	if ( *s == '`' ) {
		j = MD_Find( s + 1, end, "`" );
		if ( j && j > s + 1 ) {
			MD_Set( span, MD_CODE, s + 1, (int)( j - s - 1 ) );
			*p = j + 1;
			return qtrue;
		}
	} else if ( MD_Starts( s, end, "**" ) ) {
		j = MD_Find( s + 2, end, "**" );
		if ( j && j > s + 2 ) {
			MD_Set( span, MD_BOLD, s + 2, (int)( j - s - 2 ) );
			*p = j + 2;
			return qtrue;
		}
	} else if ( *s == '*' ) {
		j = MD_Find( s + 1, end, "*" );
		if ( j && j > s + 1 && s[1] != ' ' ) {
			MD_Set( span, MD_ITALIC, s + 1, (int)( j - s - 1 ) );
			*p = j + 1;
			return qtrue;
		}
	} else if ( *s == '[' ) {
		if ( MD_Link( s, end, span, p ) )
			return qtrue;
	} else if ( MD_AddressAt( s, end ) ) {
		for ( j = s; j < end && !MD_IsSpace( *j ); j++ ) {
		}
		while ( j > s && strchr( ".,;:!?)", j[-1] ) )
			j--;
		MD_Set( span, MD_LINK, s, (int)( j - s ) );
		span->url = s;
		span->urlLen = span->len;
		*p = j;
		return qtrue;
	}

	// plain text up to the next mark; a mark that opened nothing is one letter of it
	for ( j = s + 1; j < end && *j != '`' && *j != '*' && *j != '[' && !MD_AddressAt( j, end ); j++ ) {
	}
	MD_Set( span, MD_TEXT, s, (int)( j - s ) );
	*p = j;
	return qtrue;
}

/*
===============================================================================

Versions

===============================================================================
*/

qboolean MD_ParseVersion( const char *text, int out[4] ) {
	const char	*p = text;
	int			i;

	for ( i = 0; i < 3; i++ ) {
		if ( *p < '0' || *p > '9' )
			return qfalse;
		out[i] = 0;
		while ( *p >= '0' && *p <= '9' && out[i] < 100000 )
			out[i] = out[i] * 10 + ( *p++ - '0' );
		if ( i < 2 && *p++ != '.' )
			return qfalse;
	}
	out[3] = MD_RELEASE;
	if ( !*p )
		return qtrue;
	if ( strncmp( p, "-dev.", 5 ) )
		return qfalse;
	p += 5;
	if ( *p < '0' || *p > '9' )
		return qfalse;
	out[3] = 0;
	while ( *p >= '0' && *p <= '9' && out[3] < 100000 )
		out[3] = out[3] * 10 + ( *p++ - '0' );
	return ( *p == '\0' ) ? qtrue : qfalse;
}

int MD_CompareVersion( const int a[4], const int b[4] ) {
	int i;

	for ( i = 0; i < 4; i++ ) {
		if ( a[i] != b[i] )
			return ( a[i] < b[i] ) ? -1 : 1;
	}
	return 0;
}

qboolean MD_IsDev( const char *version ) {
	return strstr( version, "-dev." ) ? qtrue : qfalse;
}
