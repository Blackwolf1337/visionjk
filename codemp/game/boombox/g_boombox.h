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

// g_boombox.h -- what g_boombox.c asks of g_boombox_fetch.c: the programs that find and fetch a song. Not from
// JA+, see docs/japlus/boombox.md.

#pragma once

#define BOX_RING			( BOX_RATE * 4 )	// samples of a song that are fetched ahead of what goes out: 4 s
#define BOX_ERROR			160					// a line of what a program said on its way out, with the end

// programs that run for a song: one, or two of which the first hands its output to the second
typedef struct boxProc_s {
	int		group;				// their process group, 0: none run. Its number is the one of the go-between,
	int		middle;				//	which started them and is left unreaped until they are stopped (Box_ProcStop)
	int		out;				// what is read: the output of the last of them, -1: none (any more)
	int		err;				// what they say, -1: nothing (any more)
	char	errLine[BOX_ERROR];	// the last line of it, or the last error among those lines
	char	errPart[BOX_ERROR];	// what came of the line after it so far
	int		errLen;
	int		started;			// trap->Milliseconds()
} boxProc_t;

// a song as yt-dlp found it
typedef struct boxSong_s {
	char	id[12];				// the video: eleven characters
	char	title[BOX_TITLE];
	int		seconds;			// how long it is
	char	by[MAX_NETNAME];	// who asked for it
} boxSong_t;

// a song that was asked for and is looked up
typedef struct boxLookup_s {
	qboolean	busy;
	int			box;			// it goes into the line of this box,
	int			client;			//	this player asked for it,
	char		by[MAX_NETNAME];	//	who was called that
	boxProc_t	proc;
	char		text[1024];		// what yt-dlp printed so far
	int			len;
} boxLookup_t;

// the sound of the song that plays
typedef struct boxStream_s {
	boxProc_t	proc;
	short		ring[BOX_RING];
	unsigned	read, write;	// samples, counted on and on: the place in ring is the rest of a division
	int			fetched;		// samples that came since the beginning of the song, with what a start further in skipped
	int			odd;			// a byte of a sample whose other byte has not come yet, -1: none
	qboolean	ended;			// the programs are through: what is in the ring is the rest
} boxStream_t;

qboolean	Box_ToolPath( const char *name, char *out, int outSize );
void		Box_ProcStop( boxProc_t *p );
void		Box_ProcErrors( boxProc_t *p );
void		Box_ReapAll( void );

qboolean	Box_Target( const char *asked, char *target, int targetSize, char *why, int whySize );
qboolean	Box_LookupStart( boxLookup_t *l, const char *target, char *why, int whySize );
int			Box_LookupFrame( boxLookup_t *l, boxSong_t *song, char *why, int whySize );

qboolean	Box_StreamStart( boxStream_t *s, const char *id, int fromSample, qboolean keepRing, char *why, int whySize );
void		Box_StreamFill( boxStream_t *s );
int			Box_StreamQueued( const boxStream_t *s );
int			Box_StreamTake( boxStream_t *s, short *out, int count );
void		Box_StreamStop( boxStream_t *s );
void		Box_StreamWhy( const boxStream_t *s, const char *otherwise, char *why, int whySize );
