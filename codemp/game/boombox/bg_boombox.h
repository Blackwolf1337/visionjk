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
bg_boombox.h -- the boombox: what the cgame (cgame/boombox/cg_boombox.c) and the game module
(game/boombox/g_boombox.c) have to agree on. Not from JA+, see docs/japlus/boombox.md.

A boombox is a box in the level that plays songs of YouTube and YouTube Music. Only clients of this tree see it
and hear it: it is no entity, nothing of it is in the game protocol, and a client that did not ask with
"vs_box hello" is told nothing of it.

The server fetches a song itself (yt-dlp and ffmpeg, game/boombox/g_boombox_fetch.c), codes it with Opus and
sends it out over the relay of the voice chat (game/voice/g_voice.c), as the voice of a speaker with the number
BOX_SPEAKER_BASE + the number of the box: a VOICE_AUDIO datagram as bg_voice.h has it. A cgame from before the
boombox drops a speaker number that is no client number, and it is never sent one anyway.

Over the game connection:

to the server	vs_box hello <protocol>				this client draws boxes and wants to hear them
to the server	vs_box off							it no longer does (v_boomboxes 0)
to a client		vs_box on <protocol> <boxes> <range>	the answer to the hello: how many boxes a level has at most,
													and how far one is heard
to a client		vs_box set <box> <rev> <x> <y> <z> <yaw> <volume> <flags> <msec> <length> <waiting> "<title>" "<next>"
					"<owner>" "<by>"
to a client		vs_box gone <box>
to a client		vs_box note "<text>"					a line for the player: what became of the song he asked for,
													why it is not played, why a box can not go where he wanted

set: the whole state of one box. rev counts up with every change. msec: how far into the song it is when the
command leaves (the cgame counts on from there while it plays), length in seconds, waiting: songs in the line
after the one that plays, next: the title of the first of them. owner: who put the box down, by: who asked for
the song that plays (both since 0.1.9; a cgame before reads no further than next).

What a player does with a box is a client command, "vs_boxaction <what> <box> ...", see Cmd_Boombox_f() of
g_boombox.c. Everybody may do everything with every box.

On a server without the game module of this tree the masterserver keeps the boxes of its room (all clients of one
game server, codemp/master/ms_boombox.c), and everything goes through the datagrams of the voice chat:
VOICE_BOX of bg_voice.h, after the head, every number with its low byte first:

to the master	what box nameLen name[nameLen]		and for BOX_DO_PLACE and BOX_DO_MOVE x[4] y[4] z[4] yaw[2],
													for BOX_DO_PLAY the text (box 255: a new box, its place
													before the text), for BOX_DO_VOLUME volume[1]
to a client		BOX_SAY_SET box rev[2] x[4] y[4] z[4] yaw[2] volume flags msec[4] length[2] waiting
					titleLen title nextLen next
to a client		BOX_SAY_GONE box
to a client		BOX_SAY_NOTE box text				(box 255: no box in particular)
to a client		BOX_SAY_INFO box ownerLen owner byLen by	after every BOX_SAY_SET, since 0.1.9 (a client before
													drops it: BOX_SAY_SET itself must never grow)

The master has no level: where a box may stand is what the client found (Box_Spot of cg_boombox.c). It does not
know who is near a box either: the client says it in its state (bg_voice.h), and the master sends the song of a
box only to who said he hears it. A yaw is 65536 steps a turn, x, y and z are whole units.
*/

#pragma once

#define BOX_PROTOCOL		1
#define BOX_MAX				8			// boxes a level has at most (vs_boomboxMax is up to this)
#define BOX_SPEAKER_BASE	MAX_CLIENTS	// the speaker number of box n in a datagram of the voice relay is this + n
#define BOX_RATE			48000		// what a song is decoded to: one channel, 16 bit. VOICE_RATE of bg_voice.h
#define BOX_TITLE			100			// a title with its end
#define BOX_QUEUE			16			// songs one box has in its line at most, the one that plays among them
#define BOX_ASK				200			// what a player can ask for: a link or the words to look for, with the end

#define BOX_RANGE			1536		// the default of vs_boomboxRange: how far a box is heard, in units
#define BOX_REACH			320			// how far from the eye of a player a box can be put
#define BOX_HALF_WIDTH		16.0f		// the box: from its middle to its left and right end,
#define BOX_HALF_DEPTH		6.0f		//	to its front and back,
#define BOX_HEIGHT			26.0f		//	and from the floor to the top of its handle. Its origin is on the floor.
#define BOX_SPEAKER_HEIGHT	9.0f		// where its speakers are, above its origin: where the sound comes from
#define BOX_APART			24.0f		// two boxes stand at least this far apart
#define BOX_FOOT			12.0f		// where a box is put: the box from its middle to its sides that must fit on the floor,
#define BOX_FOOT_HEIGHT		20.0f		//	how high that box is,
#define BOX_DROP			96.0f		//	and how far below the place the floor is looked for

#define BOX_MASTER_MAX		4			// boxes a room of the masterserver has at most
#define BOX_NAME_MAX		35			// bytes of the name in a VOICE_BOX to the master at most

// what a client does with a box through the masterserver
#define BOX_DO_PLAY			1
#define BOX_DO_PLACE		2
#define BOX_DO_MOVE			3
#define BOX_DO_PAUSE		4
#define BOX_DO_RESUME		5
#define BOX_DO_SKIP			6
#define BOX_DO_STOP			7
#define BOX_DO_REMOVE		8
#define BOX_DO_VOLUME		9

// what the masterserver says to a client
#define BOX_SAY_SET			1
#define BOX_SAY_GONE		2
#define BOX_SAY_NOTE		3
#define BOX_SAY_INFO		4

// the flags of "vs_box set"
#define BOX_PLAYING			1			// a song plays, or is about to
#define BOX_PAUSED			2
#define BOX_LOADING			4			// the song is being fetched: nothing is heard yet
#define BOX_LOOKING			8			// a song that was asked for is being looked up

// a VOICE_BOX to the master, unpacked
typedef struct boxDo_s {
	int		what, box;
	char	name[BOX_NAME_MAX + 1];
	int		origin[3];
	float	yaw;
	int		volume;
	char	text[BOX_ASK];
} boxDo_t;

// a VOICE_BOX to a client, unpacked
typedef struct boxSay_s {
	int		what, box;
	int		rev;
	int		origin[3];
	float	yaw;
	int		volume, flags, msec, length, waiting;
	char	title[BOX_TITLE], next[BOX_TITLE];
	char	text[256];			// of a note
	char	owner[BOX_NAME_MAX + 1], by[BOX_NAME_MAX + 1];	// of an info
} boxSay_t;

int			BG_BoxDoPack( byte *out, int outSize, const boxDo_t *d );
qboolean	BG_BoxDoUnpack( const byte *in, int len, boxDo_t *d );
int			BG_BoxSayPack( byte *out, int outSize, const boxSay_t *s );
qboolean	BG_BoxSayUnpack( const byte *in, int len, boxSay_t *s );
