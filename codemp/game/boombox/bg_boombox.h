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

to the server	vs_box hello <protocol> <caps>		this client draws boxes and wants to hear them. caps: BOX_CAP_SYSTEM,
													it draws sound systems too (since 0.2.4, a server before reads no
													further than the protocol)
to the server	vs_box off							it no longer does (v_boomboxes 0)
to a client		vs_box on <protocol> <boxes> <range> <caps>	the answer to the hello: how many boxes a level has at
													most, how far one is heard, and BOX_CAP_* (since 0.1.9)
to a client		vs_box set <box> <rev> <x> <y> <z> <yaw> <volume> <flags> <msec> <length> <waiting> "<title>" "<next>"
					"<owner>" "<by>"
to a client		vs_box list <box> <lineRev> <first> <total> [<seconds> "<title>" "<by>"]...
													the line of a box from song first on, as many as fit (since
													0.1.9), to who watches that box; songs[0] plays
to a client		vs_box gone <box>
to a client		vs_box note "<text>"					a line for the player: what became of the song he asked for,
													why it is not played, why a box can not go where he wanted

set: the whole state of one box. rev counts up with every change. msec: how far into the song it is when the
command leaves (the cgame counts on from there while it plays), length in seconds, waiting: songs in the line
after the one that plays, next: the title of the first of them. owner: who put the box down, by: who asked for
the song that plays (both since 0.1.9; a cgame before reads no further than next).

A sound system (since 0.2.4, BOX_KIND_SYSTEM) is a box of its own kind: a DJ table where the box stands, and two
big speakers that stand where the players put them. Its song is fetched in stereo, the left channel goes out as
speaker BOX_SPEAKER_BASE + the box, the right one as BOX_SPEAKER_BASE + BOX_RIGHT + the box. Its "vs_box set"
has nine more words after by: <kind> <lx> <ly> <lz> <lyaw> <rx> <ry> <rz> <ryaw>, the places of the left and the
right speaker. Only a client that said BOX_CAP_SYSTEM in its hello is told of a sound system or sent its song:
for every other one it is not there.

What a player does with a box is a client command, "vs_boxaction <what> <box> ...", see Cmd_Boombox_f() of
g_boombox.c. Everybody may do everything with every box. Since 0.1.9 (BOX_CAP_MORE): seek <box> <second>, drop
<box> <place in the line>, and watch <box>, after which the client is sent the line of that box whenever it
changes (watch -1: no more). Since 0.2.4 (BOX_CAP_SYSTEM): system <x y z yaw> <lx ly lz lyaw> <rx ry rz ryaw>, a
new sound system, its table and its two speakers, and part <box> <part> <x y z yaw>, one part (BOX_PART_*) of a
box moved.

On a server without the game module of this tree the masterserver keeps the boxes of its room (all clients of one
game server, codemp/master/ms_boombox.c), and everything goes through the datagrams of the voice chat:
VOICE_BOX of bg_voice.h, after the head, every number with its low byte first:

to the master	what box nameLen name[nameLen]		and for BOX_DO_PLACE and BOX_DO_MOVE x[4] y[4] z[4] yaw[2],
													for BOX_DO_SYSTEM three of those (table, left, right), for
													BOX_DO_PART the part[1] and one (both since 0.2.4, to a
													master that said BOX_SAY_HELLO),
													for BOX_DO_PLAY the text (box 255: a new box, its place
													before the text), for BOX_DO_VOLUME volume[1], for
													BOX_DO_SEEK second[2], for BOX_DO_DROP place[1], BOX_DO_WATCH
													nothing (box 255: no more). The last three since 0.1.9, to a
													master that says VOICE_FLAG_BOXLIST.
to a client		BOX_SAY_SET box rev[2] x[4] y[4] z[4] yaw[2] volume flags msec[4] length[2] waiting
					titleLen title nextLen next
to a client		BOX_SAY_GONE box
to a client		BOX_SAY_NOTE box text				(box 255: no box in particular)
to a client		BOX_SAY_INFO box ownerLen owner byLen by	after every BOX_SAY_SET, since 0.1.9 (a client before
													drops it: BOX_SAY_SET itself must never grow)
to a client		BOX_SAY_LIST box lineRev[2] place total seconds[2] titleLen title byLen by
													one song of the line of a box, to who watches it, since 0.1.9
to a client		BOX_SAY_RIG box kind left[14] right[14]	after the BOX_SAY_SET of a sound system: where its
													speakers stand (x[4] y[4] z[4] yaw[2] each), since 0.2.4
to a client		BOX_SAY_HELLO 255 caps				the master knows BOX_CAP_SYSTEM, since 0.2.4
to a client		BOX_SAY_VIDEO box idLen id			after the BOX_SAY_SET of a TV: the video of its song, since 0.2.9

A TV (BOX_KIND_TV, since 0.2.9) is placed as a boombox is, with "vs_boxaction tv <x y z yaw>" or BOX_DO_TV, and is
there only for a client that said BOX_CAP_TV in its hello (or BOX_STATE_TVS in its state to the master). Its "vs_box
set" ends as the one of a sound system, with the kind and eight zeros, and then the video of its song (11
characters, or - for none). Its picture comes from the TV of the masterserver over HTTPS (master/ms_tv.c).

The master has no level: where a box may stand is what the client found (Box_Spot of cg_boombox.c). It does not
know who is near a box either: the client says it in its state (bg_voice.h), and the master sends the song of a
box only to who said he hears it. A yaw is 65536 steps a turn, x, y and z are whole units.

A client of 0.2.4 or later sets BOX_STATE_SYSTEMS in the byte of the boxes it hears: it draws sound systems.
Only such a client is sent BOX_SAY_HELLO, BOX_SAY_RIG, a sound system, and its right channel. A master before
looks at the bits of its four boxes only.
*/

#pragma once

#define BOX_PROTOCOL		1
#define BOX_MAX				8			// boxes a level has at most (vs_boomboxMax is up to this)
#define BOX_SPEAKER_BASE	MAX_CLIENTS	// the speaker number of box n in a datagram of the voice relay is this + n,
#define BOX_RIGHT			BOX_MAX		//	of the right channel of a sound system this + BOX_RIGHT
#define BOX_CHANNELS		( BOX_MAX * 2 )	// speaker numbers of boxes from BOX_SPEAKER_BASE on: a channel number
#define BOX_RATE			48000		// what a song is decoded to: 16 bit, one channel a speaker. VOICE_RATE of bg_voice.h
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

// the sound system (BOX_KIND_SYSTEM): a DJ table where the box stands, and two big speakers. Origins on the floor.
#define BOX_KIND_BOOMBOX	0
#define BOX_KIND_SYSTEM		1
#define BOX_KIND_TV			2			// the TV (since 0.2.9): a box that shows the video of its song (master/ms_tv.c)
#define BOX_PARTS			3			// what of a box stands somewhere: the box or the table, and the two speakers
#define BOX_PART_BODY		0
#define BOX_PART_LEFT		1
#define BOX_PART_RIGHT		2
#define SYS_DESK_HALF_WIDTH	30.0f		// the table: from its middle to its left and right end,
#define SYS_DESK_HALF_DEPTH	13.0f		//	to its front and back,
#define SYS_DESK_HEIGHT		40.0f		//	and its top
#define SYS_SPK_HALF_WIDTH	13.0f		// a speaker, the same
#define SYS_SPK_HALF_DEPTH	11.0f
#define SYS_SPK_HEIGHT		62.0f
#define SYS_SPK_MOUTH		36.0f		// where its sound comes from: the middle of its big cone, above its origin
#define SYS_SPK_SIDE		76.0f		// a new speaker stands this far to the side of the middle of the table
#define SYS_FOOT_DESK		24.0f		// what has to fit on the floor where a table goes, from its middle (BOX_FOOT of a box)
#define SYS_FOOT_SPK		10.0f		//	and where a speaker goes
#define SYS_APART			40.0f		// a part of a sound system stands at least this far from any other part
#define SYS_RANGE_SCALE		2.0f		// a sound system is heard this many times as far as a boombox (vs_boomboxRange)
#define TV_FOOT				24.0f		// the TV: what has to fit on the floor where it goes, from its middle,
#define TV_MOUTH			40.0f		//	where its sound comes from, above its origin,
#define TV_APART			48.0f		//	and how far it stands from any other part at least

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
#define BOX_DO_SEEK			10
#define BOX_DO_DROP			11
#define BOX_DO_WATCH		12
#define BOX_DO_SYSTEM		13			// since 0.2.4
#define BOX_DO_PART			14
#define BOX_DO_TV			15			// since 0.2.9, to a master that said BOX_CAP_TV: a new TV, as BOX_DO_PLACE

// what the masterserver says to a client
#define BOX_SAY_SET			1
#define BOX_SAY_GONE		2
#define BOX_SAY_NOTE		3
#define BOX_SAY_INFO		4
#define BOX_SAY_LIST		5
#define BOX_SAY_RIG			6			// since 0.2.4
#define BOX_SAY_HELLO		7
#define BOX_SAY_VIDEO		8			// since 0.2.9: after the BOX_SAY_SET of a TV, the video of its song

#define BOX_STATE_SYSTEMS	0x80		// in the byte of the boxes a client hears: it draws sound systems
#define BOX_STATE_TVS		0x40		//	and TVs

// what the server can do of what came after 0.1.8, in "vs_box on"
#define BOX_CAP_MORE		1			// who put a box down, its line, seek, drop, watch
#define BOX_CAP_SYSTEM		2			// the sound system (since 0.2.4). Also what a client says in its hello
#define BOX_CAP_TV			4			// the TV (since 0.2.9), the same

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
	int		parts[2][3];		// BOX_DO_SYSTEM: where the left and the right speaker go
	float	partYaw[2];
	int		volume;
	int		value;				// BOX_DO_SEEK: the second, BOX_DO_DROP: the place in the line, BOX_DO_PART: the part
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
	char	video[12];			// of a BOX_SAY_VIDEO: the eleven characters, "" none
	char	owner[BOX_NAME_MAX + 1], by[BOX_NAME_MAX + 1];	// of an info, and by of a song of the line
	int		place, total;		// of a song of the line: its place (0 plays) and how many there are; its title and
								//	seconds are title and length
	int		kind;				// of a rig: BOX_KIND_*,
	int		parts[2][3];		//	where its left and right speaker stand
	float	partYaw[2];
	int		caps;				// of a hello: BOX_CAP_*
} boxSay_t;

int			BG_BoxDoPack( byte *out, int outSize, const boxDo_t *d );
qboolean	BG_BoxDoUnpack( const byte *in, int len, boxDo_t *d );
int			BG_BoxSayPack( byte *out, int outSize, const boxSay_t *s );
qboolean	BG_BoxSayUnpack( const byte *in, int len, boxSay_t *s );
