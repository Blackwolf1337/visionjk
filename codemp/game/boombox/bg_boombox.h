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
"jpbox hello" is told nothing of it.

The server fetches a song itself (yt-dlp and ffmpeg, game/boombox/g_boombox_fetch.c), codes it with Opus and
sends it out over the relay of the voice chat (game/voice/g_voice.c), as the voice of a speaker with the number
BOX_SPEAKER_BASE + the number of the box: a VOICE_AUDIO datagram as bg_voice.h has it. A cgame from before the
boombox drops a speaker number that is no client number, and it is never sent one anyway.

Over the game connection:

to the server	jpbox hello <protocol>				this client draws boxes and wants to hear them
to the server	jpbox off							it no longer does (cg_boombox 0)
to a client		jpbox on <protocol> <boxes> <range>	the answer to the hello: how many boxes a level has at most,
													and how far one is heard
to a client		jpbox set <box> <rev> <x> <y> <z> <yaw> <volume> <flags> <msec> <length> <waiting> "<title>" "<next>"
to a client		jpbox gone <box>
to a client		jpbox note "<text>"					a line for the chat box: what was put on, why a song is not played

set: the whole state of one box. rev counts up with every change. msec: how far into the song it is when the
command leaves (the cgame counts on from there while it plays), length in seconds, waiting: songs in the line
after the one that plays, next: the title of the first of them.

What a player does with a box is a client command, "boombox <what> <box> ...", see Cmd_Boombox_f() of
g_boombox.c. Everybody may do everything with every box.
*/

#pragma once

#define BOX_PROTOCOL		1
#define BOX_MAX				8			// boxes a level has at most (g_boomboxMax is up to this)
#define BOX_SPEAKER_BASE	MAX_CLIENTS	// the speaker number of box n in a datagram of the voice relay is this + n
#define BOX_RATE			48000		// what a song is decoded to: one channel, 16 bit. VOICE_RATE of bg_voice.h
#define BOX_TITLE			100			// a title with its end
#define BOX_QUEUE			16			// songs one box has in its line at most, the one that plays among them
#define BOX_ASK				200			// what a player can ask for: a link or the words to look for, with the end

#define BOX_RANGE			1536		// the default of g_boomboxRange: how far a box is heard, in units
#define BOX_REACH			320			// how far from the eye of a player a box can be put
#define BOX_HALF_WIDTH		16.0f		// the box: from its middle to its left and right end,
#define BOX_HALF_DEPTH		6.0f		//	to its front and back,
#define BOX_HEIGHT			26.0f		//	and from the floor to the top of its handle. Its origin is on the floor.
#define BOX_SPEAKER_HEIGHT	9.0f		// where its speakers are, above its origin: where the sound comes from

// the flags of "jpbox set"
#define BOX_PLAYING			1			// a song plays, or is about to
#define BOX_PAUSED			2
#define BOX_LOADING			4			// the song is being fetched: nothing is heard yet
#define BOX_LOOKING			8			// a song that was asked for is being looked up
