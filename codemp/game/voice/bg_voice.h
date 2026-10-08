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

#define VOICE_FLAG_UNSEEN		1		// to a client: the speaker is not in what the server shows him of the level
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

int			BG_VoicePack( byte *out, int outSize, const voicePacket_t *p, qboolean toClient );
qboolean	BG_VoiceUnpack( const byte *in, int len, qboolean toClient, voicePacket_t *p );
int			BG_VoiceSeqDelta( int a, int b );

void		BG_VoiceQueueReset( voiceQueue_t *q );
qboolean	BG_VoiceQueuePut( voiceQueue_t *q, int seq, const byte *data, int len );
int			BG_VoiceQueueSpan( const voiceQueue_t *q );
int			BG_VoiceQueueGet( voiceQueue_t *q, const byte **data, const byte **after, int *afterLen );

qboolean	BG_VoiceLimit( voiceLimit_t *l, int msec, int bytes );
qboolean	BG_VoiceHears( const voiceEar_t *speaker, const voiceEar_t *listener, float range, qboolean teamOnly, float *distance );
void		BG_VoiceMuffle( voiceFilter_t *f, float *samples, int count, float target );
