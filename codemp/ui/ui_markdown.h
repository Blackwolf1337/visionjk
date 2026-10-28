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

/*
ui_markdown.h -- the changelog of the game as the website writes it (tools/changelog.py of the website): entries
of a front matter and a body, the body in a small part of Markdown. Nothing of the ui is used here, only q_shared,
so the tests (tests/vjk/markdown.cpp) link it as it is. Not from JA+.

	---
	version: 0.2.7
	date: 2026-10-24
	summary: One line that says what the version is about.
	---

	- a line of a list, **bold**, *italic*, `code`, [a link](https://jka.vision/), https://jka.vision/
	  - one level below it (two spaces in)
	## a heading
	### a smaller one

Everything else is plain text. Nothing is copied: entries, blocks and spans point into the text they came from,
which has to stay where it is while they are used. No part of a text can make a reader leave it: every length is
counted against its end.
*/

#pragma once

#define MD_VERSION_LEN		32
#define MD_DATE_LEN			16
#define MD_SUMMARY_LEN		320

typedef struct mdEntry_s {
	char		version[MD_VERSION_LEN];
	char		date[MD_DATE_LEN];			// YYYY-MM-DD
	char		summary[MD_SUMMARY_LEN];
	const char	*body;						// after the front matter, up to the next entry
	int			bodyLen;
} mdEntry_t;

// The entries of a text, in its order, at most max. A part that is no entry (no front matter, or one without
// a version) is skipped.
int MD_ReadEntries( const char *text, int len, mdEntry_t *entries, int max );

typedef enum {
	MD_NONE,
	MD_PARAGRAPH,
	MD_HEADING,			// "## "
	MD_SUBHEADING,		// "### "
	MD_ITEM,			// "- " or "* "
	MD_SUBITEM			// the same two spaces or more in, under an item
} mdBlockType_t;

// A block: its text runs over the lines it has, from the first word after its mark to the end of its last line.
// The line breaks and the spaces at the start of a line in it count as one space each (MD_NextSpan).
typedef struct mdBlock_s {
	mdBlockType_t	type;
	const char		*text;
	int				len;
} mdBlock_t;

typedef struct mdReader_s {
	const char		*p, *end;
	mdBlockType_t	last;			// the block before, MD_NONE after an empty line
} mdReader_t;

void		MD_StartBlocks( mdReader_t *reader, const char *body, int len );
qboolean	MD_NextBlock( mdReader_t *reader, mdBlock_t *block );

typedef enum {
	MD_TEXT,
	MD_BOLD,
	MD_ITALIC,
	MD_CODE,
	MD_LINK			// text and url: [text](url), or an address alone, which is both
} mdStyle_t;

typedef struct mdSpan_s {
	mdStyle_t	style;
	const char	*text;
	int			len;
	const char	*url;
	int			urlLen;
} mdSpan_t;

// The next span of the text of a block from *p on, qfalse at end. Marks that are not closed are plain text.
qboolean MD_NextSpan( const char **p, const char *end, mdSpan_t *span );

// A version as numbers, for the order: 0.2.9-dev.3 comes before 0.2.9. qfalse: no version of the website.
qboolean MD_ParseVersion( const char *text, int out[4] );
int MD_CompareVersion( const int a[4], const int b[4] );
qboolean MD_IsDev( const char *version );
