/*
===========================================================================
Copyright (C) 2013 - 2015, OpenJK contributors
Copyright (C) 2026, VisionJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
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
bg_voice.h -- the voice chat: what the cgame (cg_voice.c) and the game module (g_voice.c) have to agree on, and
the parts of both that are plain arithmetic. Not from JA+, see docs/japlus/voice.md.

Voice does not go through the game protocol. The cgame has a UDP socket of its own, the game module relays
on a second port. A datagram, every number with its low byte first:

to the server	"VO" version type client token[4] sequence[2]							frame
to a client		"VO" version type speaker token[4] sequence[2] flags distance			frame

The token is the one the server gave that client over the game connection, in both directions: it is what
tells the server who sends and tells the client that it is the server that sends.

On a server without this game module the cgame goes through the masterserver (codemp/master), one relay
for all such servers. There nobody hands out a token over a game connection and nobody knows where a player
stands, so the client says both in a datagram of its own, VOICE_STATE:

to the master	"VO" version type client token[4] sequence[2]							room[16] hear[4] allow[4]
to a client		"VO" version type client token[4] sequence[2] flags distance

room: the same bytes from every client of one game server (Voice_MasterRoom of cg_voice.c). hear and allow: a
bit for every client number, whom this client wants to hear and who may hear it. The first one goes out with
token 0, the answer carries the token of the session.

The clan radio (2026-10-11): a frame a client sends as VOICE_RADIO in place of VOICE_AUDIO goes to the players
who have the clan password of the speaker (cp_clanPwd, the group of the clan chat), wherever they are and
whatever team they are on, and to nobody else. They get it as a VOICE_AUDIO with VOICE_FLAG_RADIO: a voice
without a place, which the cgame plays through BG_VoiceRadio(). A relay says that it knows the radio in its
answer to "vs_voiceLink hello" (VOICE_CAP_RADIO), a client sends no VOICE_RADIO to one that does not. A cgame from
before the radio does not know the flag: for it the frame has VOICE_FLAG_UNSEEN and a distance of 0 as well,
which it plays as a voice close by behind a wall.

The masterserver knows the radio too, and what stands above the head of a player (cg_status.c), since
2026-10-11. It says so in every answer (VOICE_FLAG_SOCIAL), and only a client that saw that flag sends it
anything of the following, so a master from before never gets a datagram it would not read:

to the master	"VO" version 2 client token[4] sequence[2]							room[16] hear[4] allow[4] clan[8]
to the master	"VO" version 3 client token[4] sequence[2]							frame
to the master	"VO" version 4 client token[4] sequence[2]							flags text
to a client		"VO" version 4 client token[4] sequence[2] flags distance			flags text

clan: who has the same eight bytes as the speaker in the same room gets his VOICE_RADIO, as a relay sends it.
They are worked out from the room and the clan password (Voice_MasterClan of cg_voice.c), all zero: no clan.
The master never has the password. A VOICE_STATUS goes to the others of the room who sent the long state, as it
came: one byte of flags (VOICE_STATUS_AWAY) and a text of up to VOICE_STATUS_TEXT - 1 bytes, UTF-8, which the
master does not keep.

Type 5 was the Vision Temple of versions 0.1.6 and 0.1.7 (removed 2026-10-17): such a client may still send it, nothing
answers it, and the number is not given to anything else.

The boombox (2026-10-15, game/boombox/bg_boombox.h): on a server without the game module of this tree the
masterserver keeps the boxes of a room and plays their songs (master/ms_boombox.c). A master that does says so in
every answer (VOICE_FLAG_BOXES), and only a client that saw that flag sends it the state with one byte more, the
boxes it hears (a bit for every box, as near as its game shows them), and what its player does with a box:

to the master	"VO" version 2 client token[4] sequence[2]							room[16] hear[4] allow[4] clan[8] boxes[1]
to the master	"VO" version 6 client token[4] sequence[2]							what box name what is done
to a client		"VO" version 6 client token[4] sequence[2] flags distance			what box what it is now, or a line

The songs come as VOICE_AUDIO of speaker MAX_CLIENTS + the box, to the clients that hear the box. What the bytes
of type 6 are is in bg_boombox.h.
*/

#pragma once

#define VOICE_PROTOCOL			1
// Samples a second, one channel, 16 bit: what the microphone is opened with and what a voice is decoded to.
// It is no part of the protocol. A frame of Opus is 20 ms at any rate, so a client that has another number
// here (24000 until 2026-10-08) is understood and understands.
#define VOICE_RATE				48000
#define VOICE_FRAME_MSEC		20
#define VOICE_FRAME_SAMPLES		( VOICE_RATE * VOICE_FRAME_MSEC / 1000 )
#define VOICE_MAX_FRAME			240		// bytes of one coded frame. A longer one is not sent and not taken
#define VOICE_HEAD_TO_SERVER	11
#define VOICE_HEAD_TO_CLIENT	13
#define VOICE_MAX_DATAGRAM		( VOICE_HEAD_TO_CLIENT + VOICE_MAX_FRAME )

#define VOICE_HELLO				0		// to the server: this is my address. To a client: the server has it
#define VOICE_AUDIO				1
#define VOICE_STATE				2		// the masterserver only: where this client is and whom it hears, and the answer
#define VOICE_RADIO				3		// to the server only: sound for the clan of the speaker
#define VOICE_STATUS			4		// through the masterserver only: what stands above the head of a player
// 5: the Vision Temple of 0.1.6 and 0.1.7, removed. Not to be used again.
#define VOICE_BOX				6		// through the masterserver only: a box of the boombox (bg_boombox.h)

#define VOICE_ROOM_BYTES		16
#define VOICE_STATE_BYTES		( VOICE_ROOM_BYTES + 8 )	// what a VOICE_STATE to the master carries after its head
#define VOICE_CLAN_BYTES		8		//	and what the long one carries in addition: the clan of the client
#define VOICE_STATE_LONG_BYTES	( VOICE_STATE_BYTES + VOICE_CLAN_BYTES )
#define VOICE_STATE_BOX_BYTES	( VOICE_STATE_LONG_BYTES + 1 )	//	and one byte more: the boxes it hears (VOICE_FLAG_BOXES)
#define VOICE_STATUS_TEXT		64		// the text of a status with its end, as STATUS_TEXT of cg_status.c
#define VOICE_STATUS_AWAY		1		// in the first byte of a VOICE_STATUS: he is away from the keyboard
#define VOICE_MASTER_PORT		29170	// UDP, where the masterserver listens unless it is told another one
#define VOICE_MASTER_RANGE		1024	// how far a voice carries through the master: the default of vs_voiceRange

#define VOICE_FLAG_UNSEEN		1		// to a client: the speaker is not in what the server shows him of the level
#define VOICE_FLAG_TAKEN		2		// the answer of the master: another session speaks as this client number
#define VOICE_FLAG_FULL			4		//	or it has no room for one more
#define VOICE_FLAG_RADIO		8		// to a client: this came over the clan radio and has no place
#define VOICE_FLAG_SOCIAL		16		// the answer of the master: it knows the long state, VOICE_RADIO and VOICE_STATUS
#define VOICE_FLAG_BOXES		32		// the answer of the master: it plays boxes, it knows VOICE_BOX and the state with the boxes
#define VOICE_FLAG_BOXLIST		64		//	and what came for them after 0.1.8: BOX_DO_SEEK, BOX_DO_DROP, BOX_DO_WATCH (bg_boombox.h)
#define VOICE_CAP_RADIO			1		// in the answer to "vs_voiceLink hello": the relay knows VOICE_RADIO
#define VOICE_DISTANCE_STEP		16		// the distance byte counts in steps of this many units

#define VOICE_MAX_PPS			60		// what the relay takes from one speaker in a second: datagrams
#define VOICE_MAX_BPS			14000	//	and bytes. 50 frames a second at 20 kbit are 50 and about 3500
#define VOICE_QUEUE				16		// frames a listener keeps of one speaker until they are played

#define VOICE_MUFFLE_FADE		0.15f	// seconds from clear to muffled and back
#define VOICE_MUFFLE_GAIN		0.6f	// how loud a muffled voice is next to a clear one

typedef struct voicePacket_s {
	int				type;
	int				client;			// to the server: who sends. To a client: who speaks
	unsigned int	token;
	int				seq;			// 0 to 65535, one up with every frame of a speaker
	int				flags;			// to a client only
	int				distance;		// to a client only: VOICE_DISTANCE_STEP units each, 255 and more is 255
	const byte		*data;			// the coded frame, inside the datagram it was read from
	int				dataLen;
} voicePacket_t;

// the frames of one speaker that came in and wait for their turn, by sequence number
typedef struct voiceQueue_s {
	qboolean	started;
	int			next;						// the sequence number that is played next
	short		len[VOICE_QUEUE];			// of the frame in a slot, 0: none
	unsigned short	seq[VOICE_QUEUE];
	byte		data[VOICE_QUEUE][VOICE_MAX_FRAME];
} voiceQueue_t;

typedef struct voiceLimit_s {
	int		second;
	int		packets;
	int		bytes;
} voiceLimit_t;

// a player as far as the question who hears whom goes
typedef struct voiceEar_s {
	qboolean	ready;			// he has voice and the server knows his address
	qboolean	spectator;
	qboolean	silenced;
	int			team;
	vec3_t		origin;
} voiceEar_t;

// the low-pass of one voice and how much of it is in what is heard, 0 (clear) to 1 (muffled)
typedef struct voiceFilter_s {
	float	low[2];
	float	mix;
} voiceFilter_t;

// a voice that comes over the radio: the state of its four filters, and where its noise is
typedef struct voiceRadio_s {
	float			state[4][2];
	unsigned int	noise;
} voiceRadio_t;

#define VOICE_RADIO_CLICK		( VOICE_RATE * 90 / 1000 )		// samples of the longer click, the one at the end

int			BG_VoicePack( byte *out, int outSize, const voicePacket_t *p, qboolean toClient );
qboolean	BG_VoiceUnpack( const byte *in, int len, qboolean toClient, voicePacket_t *p );
int			BG_VoiceSeqDelta( int a, int b );
void		BG_VoiceStatePack( byte out[VOICE_STATE_BYTES], const byte room[VOICE_ROOM_BYTES], unsigned int hear, unsigned int allow );
void		BG_VoiceStateUnpack( const voicePacket_t *p, const byte **room, unsigned int *hear, unsigned int *allow );
const byte	*BG_VoiceStateClan( const voicePacket_t *p );
qboolean	BG_VoiceStateBoxes( const voicePacket_t *p, int *boxes );
int			BG_VoiceStatusPack( byte out[VOICE_STATUS_TEXT], qboolean away, const char *text );
qboolean	BG_VoiceStatusUnpack( const voicePacket_t *p, qboolean *away, char *text, int textSize );

void		BG_VoiceQueueReset( voiceQueue_t *q );
qboolean	BG_VoiceQueuePut( voiceQueue_t *q, int seq, const byte *data, int len );
int			BG_VoiceQueueSpan( const voiceQueue_t *q );
int			BG_VoiceQueueGet( voiceQueue_t *q, const byte **data, const byte **after, int *afterLen );

qboolean	BG_VoiceLimit( voiceLimit_t *l, int msec, int bytes );
qboolean	BG_VoiceHears( const voiceEar_t *speaker, const voiceEar_t *listener, float range, qboolean teamOnly, float *distance );
void		BG_VoiceMuffle( voiceFilter_t *f, float *samples, int count, float target );
void		BG_VoiceRadio( voiceRadio_t *r, short *samples, int count );
int			BG_VoiceRadioClick( voiceRadio_t *r, short *out, int max, qboolean end );
