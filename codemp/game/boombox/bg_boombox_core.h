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
bg_boombox_core.h -- what plays the songs of boxes, for whoever keeps the boxes: the game module of a server of this
tree (game/boombox/g_boombox.c) and the masterserver for every other server (master/ms_boombox.c). Not from JA+,
see docs/japlus/boombox.md.

bg_boombox_fetch.c starts the programs that find and fetch a song (yt-dlp, ffmpeg), bg_boombox_core.c keeps the
line of songs of a box, codes what is due by the clock with Opus and hands every frame to its host. The host
says how the clients are told (boxHost_t) and who hears a frame. Read it after q_shared.h and bg_boombox.h, and
in a module that has Opus (JP_BOOMBOX): without it nothing is coded.
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define BOX_RING			( BOX_RATE * 4 )	// samples of a song that are fetched ahead of what goes out: 4 s
#define BOX_ERROR			160					// a line of what a program said on its way out, with the end
#define BOX_ENCODER_BYTES	40960				// room for the state of the encoder of one player. What it needs is asked and checked
#define BOX_NAME			36					// who asked for a song, with the end (MAX_NETNAME)

// the programs a host names, as the owner of the server set them
typedef struct boxTools_s {
	char	ytdlp[MAX_OSPATH];		// a name in PATH, or a path
	char	ffmpeg[MAX_OSPATH];
	char	ytdlpArgs[256];			// more words for yt-dlp, split at spaces
} boxTools_t;

// programs that run for a song: one, or two of which the first hands its output to the second
typedef struct boxProc_s {
	int		group;				// their process group, 0: none run. Its number is the one of the go-between,
	int		middle;				//	which started them and is left unreaped until they are stopped (Box_ProcStop)
	int		out;				// what is read: the output of the last of them, -1: none (any more)
	int		err;				// what they say, -1: nothing (any more)
	char	errLine[BOX_ERROR];	// the last line of it, or the last error among those lines
	char	errPart[BOX_ERROR];	// what came of the line after it so far
	int		errLen;
	int		started;			// the clock of the host when they were started
} boxProc_t;

// a song as yt-dlp found it
typedef struct boxSong_s {
	char	id[12];				// the video: eleven characters
	char	title[BOX_TITLE];
	int		seconds;			// how long it is
	char	by[BOX_NAME];		// who asked for it
} boxSong_t;

// the sound of the song that plays
typedef struct boxStream_s {
	boxProc_t	proc;
	short		ring[BOX_RING];
	unsigned	read, write;	// samples, counted on and on: the place in ring is the rest of a division
	int			fetched;		// samples that came since the beginning of the song, with what a start further in skipped
	int			odd;			// a byte of a sample whose other byte has not come yet, -1: none
	qboolean	ended;			// the programs are through: what is in the ring is the rest
} boxStream_t;

// a block of memory for the state of an encoder, as well aligned as anything can ask for
typedef union boxEncoder_u {
	double	align;
	void	*alignToo;
	byte	bytes[BOX_ENCODER_BYTES];
} boxEncoder_t;

// What a box plays with: the programs of its song, what they fetched, and the encoder. Large (BOX_RING), so a
// host has a few of them and lends one to a box for as long as it has songs.
typedef struct boxPlayer_s {
	qboolean		inuse;
	boxStream_t		stream;
	boxEncoder_t	encoder;
	qboolean		encoderOk;
} boxPlayer_t;

typedef struct box_s {
	qboolean	inuse;
	int			rev;			// one up with every change the clients are told
	vec3_t		origin;			// on the floor
	float		yaw;
	int			volume;			// for everybody, 0 to 100
	char		owner[BOX_NAME];	// who put it down, as he was called then
	boxSong_t	songs[BOX_QUEUE];	// the line: songs[0] plays
	int			numSongs;
	int			looking;		// songs for it that are looked up
	qboolean	paused;
	// the song that plays
	boxPlayer_t	*player;		// lent by the host while it has songs, NULL: none (yet)
	qboolean	streaming;		// the programs of its song were started
	qboolean	started;		// enough of it came: its frames go out
	int			startMsec;		// the clock when its first frame would have gone out, if it had never waited
	int			frames;			// frames of it that went out
	int			waitSince;		// when its fetch began
	int			restarts;		// times its fetch broke off and was begun again further in
	int			seq;			// of the datagrams of this box, one up with every frame
} box_t;

typedef struct boxHost_s boxHost_t;

// Who keeps a set of boxes, and how the core reaches the clients. note: a line for client (-1: nobody in
// particular) and, when box is not -1, for everybody near that box. Only what went wrong is said to everybody:
// what a box does now is in what the clients are told of it.
struct boxHost_s {
	box_t		*boxes;			// BOX_MAX of them
	int			queueMax;		// songs in the line of one box at most
	int			maxSeconds;		// the longest song, 0: any length
	void		(*tools)( boxTools_t *out );
	boxPlayer_t	*(*borrow)( boxHost_t *host );
	void		(*tell)( boxHost_t *host, int box );
	void		(*note)( boxHost_t *host, int box, int client, const char *text );
	void		(*send)( boxHost_t *host, int box, const byte *frame, int len );
	void		*owner;			// for the host: a room of the masterserver
};

// a song that was asked for and is looked up
typedef struct boxLookup_s {
	qboolean	busy;
	boxHost_t	*host;			// it goes into the line of a box of this host,
	int			box;			//	this one,
	int			client;			//	this player asked for it,
	char		by[BOX_NAME];	//	who was called that
	boxProc_t	proc;
	char		text[1024];		// what yt-dlp printed so far
	int			len;
} boxLookup_t;

// bg_boombox_fetch.c
qboolean	Box_ToolPath( const char *name, char *out, int outSize );
void		Box_ProcStop( boxProc_t *p );
void		Box_ProcErrors( boxProc_t *p );
void		Box_ReapAll( void );
qboolean	Box_Target( const char *asked, char *target, int targetSize, char *why, int whySize );
qboolean	Box_LookupStart( boxLookup_t *l, const boxTools_t *tools, const char *target, int now, char *why, int whySize );
int			Box_LookupFrame( boxLookup_t *l, boxSong_t *song, int now, char *why, int whySize );
qboolean	Box_StreamStart( boxStream_t *s, const boxTools_t *tools, const char *id, int fromSample, qboolean keepRing, int now,
				char *why, int whySize );
void		Box_StreamFill( boxStream_t *s );
int			Box_StreamQueued( const boxStream_t *s );
int			Box_StreamTake( boxStream_t *s, short *out, int count );
void		Box_StreamStop( boxStream_t *s );
void		Box_StreamWhy( const boxStream_t *s, const char *otherwise, char *why, int whySize );

// bg_boombox_core.c
const char	*Box_Va( const char *format, ... );
void		Box_Run( boxHost_t *host, int box, int now );
void		Box_Next( boxHost_t *host, int box );
void		Box_Stop( boxHost_t *host, int box );
void		Box_Remove( boxHost_t *host, int box );
void		Box_Pause( boxHost_t *host, int box, qboolean pause, int now );
int			Box_Flags( const box_t *box );
qboolean	Box_Ask( boxHost_t *host, boxLookup_t *lookups, int numLookups, int box, int client, const char *by, const char *asked,
				int now, char *why, int whySize );
void		Box_Lookups( boxLookup_t *lookups, int numLookups, int now );
void		Box_DropLookups( boxLookup_t *lookups, int numLookups, const boxHost_t *host );
const char	*Box_Length( int seconds );

#ifdef __cplusplus
}
#endif
