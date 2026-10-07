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

// bg_voice.c -- the voice chat, the parts game and cgame share. See bg_voice.h.
//
// Nothing in here calls into the engine or the modules, so the file can be built and tested by itself
// (japlus_port/tools/voicetest).

#include <string.h>
#include <math.h>

#include "qcommon/q_shared.h"
#include "bg_voice.h"

/*
===============================================================================

The datagram

Every length is checked here and nowhere else: what BG_VoiceUnpack() lets through fits the buffers of both
sides.

===============================================================================
*/

/*
=================
BG_VoicePack

Returns the length of the datagram, 0 if it does not fit or a field is out of range.
=================
*/
int BG_VoicePack( byte *out, int outSize, const voicePacket_t *p, qboolean toClient ) {
	const int head = toClient ? VOICE_HEAD_TO_CLIENT : VOICE_HEAD_TO_SERVER;

	if ( p->dataLen < 0 || p->dataLen > VOICE_MAX_FRAME || head + p->dataLen > outSize )
		return 0;
	if ( p->client < 0 || p->client > 255 || ( p->type != VOICE_HELLO && p->type != VOICE_AUDIO ) )
		return 0;
	// a hello carries no sound, sound is never empty
	if ( ( p->type == VOICE_AUDIO ) != ( p->dataLen > 0 ) )
		return 0;

	out[0] = 'V';
	out[1] = 'O';
	out[2] = VOICE_PROTOCOL;
	out[3] = (byte)p->type;
	out[4] = (byte)p->client;
	out[5] = (byte)( p->token & 0xff );
	out[6] = (byte)( ( p->token >> 8 ) & 0xff );
	out[7] = (byte)( ( p->token >> 16 ) & 0xff );
	out[8] = (byte)( ( p->token >> 24 ) & 0xff );
	out[9] = (byte)( p->seq & 0xff );
	out[10] = (byte)( ( p->seq >> 8 ) & 0xff );
	if ( toClient ) {
		out[11] = (byte)p->flags;
		out[12] = (byte)( p->distance < 0 ? 0 : ( p->distance > 255 ? 255 : p->distance ) );
	}
	if ( p->dataLen )
		memcpy( out + head, p->data, (size_t)p->dataLen );
	return head + p->dataLen;
}

/*
=================
BG_VoiceUnpack

qfalse: not a datagram of this protocol, or one that is cut off or too long. p->data points into in.
=================
*/
qboolean BG_VoiceUnpack( const byte *in, int len, qboolean toClient, voicePacket_t *p ) {
	const int head = toClient ? VOICE_HEAD_TO_CLIENT : VOICE_HEAD_TO_SERVER;

	if ( len < head || len > head + VOICE_MAX_FRAME )
		return qfalse;
	if ( in[0] != 'V' || in[1] != 'O' || in[2] != VOICE_PROTOCOL )
		return qfalse;
	if ( in[3] != VOICE_HELLO && in[3] != VOICE_AUDIO )
		return qfalse;
	if ( ( in[3] == VOICE_AUDIO ) != ( len > head ) )
		return qfalse;

	p->type = in[3];
	p->client = in[4];
	p->token = (unsigned int)in[5] | ( (unsigned int)in[6] << 8 ) | ( (unsigned int)in[7] << 16 ) | ( (unsigned int)in[8] << 24 );
	p->seq = in[9] | ( in[10] << 8 );
	p->flags = toClient ? in[11] : 0;
	p->distance = toClient ? in[12] : 0;
	p->data = in + head;
	p->dataLen = len - head;
	return qtrue;
}

// how far sequence number a is ahead of b. They wrap at 65536, so this holds while the two are less than
// half of that apart.
int BG_VoiceSeqDelta( int a, int b ) {
	return (short)( ( a - b ) & 0xffff );
}

/*
===============================================================================

The queue of a listener for one speaker

Datagrams come late, twice, in the wrong order or not at all. The frames wait here by their sequence number
and are taken out in order, one every 20 ms of what is played. A frame that is not there when its turn
comes is skipped: the caller lets the codec fill the gap.

===============================================================================
*/

void BG_VoiceQueueReset( voiceQueue_t *q ) {
	memset( q, 0, sizeof( *q ) );
}

/*
=================
BG_VoiceQueuePut

qfalse: the frame is not kept, it is too old, already there or of no use.

A frame that is further ahead than the queue is long means the speaker went on somewhere else (he paused,
or a long gap in the connection): what waited is dropped and the queue starts again with that frame.
=================
*/
qboolean BG_VoiceQueuePut( voiceQueue_t *q, int seq, const byte *data, int len ) {
	int ahead, slot;

	if ( len <= 0 || len > VOICE_MAX_FRAME )
		return qfalse;
	seq &= 0xffff;

	if ( !q->started ) {
		BG_VoiceQueueReset( q );
		q->started = qtrue;
		q->next = seq;
	}
	ahead = BG_VoiceSeqDelta( seq, q->next );
	if ( ahead < 0 )
		return qfalse;
	if ( ahead >= VOICE_QUEUE ) {
		BG_VoiceQueueReset( q );
		q->started = qtrue;
		q->next = seq;
	}

	slot = seq % VOICE_QUEUE;
	if ( q->len[slot] )
		return qfalse;
	q->len[slot] = (short)len;
	q->seq[slot] = (unsigned short)seq;
	memcpy( q->data[slot], data, (size_t)len );
	return qtrue;
}

// from the frame that is played next to the newest one that waits, both counted. 0: nothing waits.
int BG_VoiceQueueSpan( const voiceQueue_t *q ) {
	int i, span = 0;

	if ( !q->started )
		return 0;
	for ( i = 0; i < VOICE_QUEUE; i++ ) {
		if ( q->len[i] ) {
			const int ahead = BG_VoiceSeqDelta( q->seq[i], q->next ) + 1;

			if ( ahead > span )
				span = ahead;
		}
	}
	return span;
}

/*
=================
BG_VoiceQueueGet

The frame whose turn it is. Returns its length and sets *data, or 0 if it never came. Then *after and
*afterLen are the frame that follows it, if that one is there: the codec can rebuild the missing one from it.
Either way the queue moves on by one frame. The pointers hold until the next call for this queue.
=================
*/
int BG_VoiceQueueGet( voiceQueue_t *q, const byte **data, const byte **after, int *afterLen ) {
	const int	slot = q->next % VOICE_QUEUE;
	const int	len = q->len[slot];

	*data = NULL;
	*after = NULL;
	*afterLen = 0;
	if ( !q->started )
		return 0;

	q->next = ( q->next + 1 ) & 0xffff;
	if ( len ) {
		q->len[slot] = 0;
		*data = q->data[slot];
		return len;
	}
	if ( q->len[q->next % VOICE_QUEUE] ) {
		*after = q->data[q->next % VOICE_QUEUE];
		*afterLen = q->len[q->next % VOICE_QUEUE];
	}
	return 0;
}

/*
===============================================================================

The relay: how much it takes from one speaker, and who hears him

===============================================================================
*/

// A datagram of this size at this time (milliseconds): is it within what a speaker may send in a second?
// A sound card sends 50 a second. What is over the limit is somebody who floods the relay.
qboolean BG_VoiceLimit( voiceLimit_t *l, int msec, int bytes ) {
	const int second = msec / 1000;

	if ( second != l->second ) {
		l->second = second;
		l->packets = 0;
		l->bytes = 0;
	}
	if ( l->packets >= VOICE_MAX_PPS || l->bytes + bytes > VOICE_MAX_BPS )
		return qfalse;
	l->packets++;
	l->bytes += bytes;
	return qtrue;
}

/*
=================
BG_VoiceHears

Does the listener get what the speaker says? range: how far a voice carries, 0: all over the level.
*distance is set when it returns qtrue.

A spectator is heard by spectators only, wherever they are. A spectator hears the players near the spot he
watches from.
=================
*/
qboolean BG_VoiceHears( const voiceEar_t *speaker, const voiceEar_t *listener, float range, qboolean teamOnly, float *distance ) {
	const float	dx = speaker->origin[0] - listener->origin[0];
	const float	dy = speaker->origin[1] - listener->origin[1];
	const float	dz = speaker->origin[2] - listener->origin[2];

	if ( !speaker->ready || !listener->ready || speaker->silenced )
		return qfalse;
	*distance = sqrtf( dx * dx + dy * dy + dz * dz );

	if ( speaker->spectator ) {
		if ( !listener->spectator )
			return qfalse;
		*distance = 0.0f;
		return qtrue;
	}
	if ( teamOnly && !listener->spectator && speaker->team != listener->team )
		return qfalse;
	if ( range > 0.0f && *distance > range )
		return qfalse;
	return qtrue;
}

/*
===============================================================================

A voice behind a wall

===============================================================================
*/

/*
=================
BG_VoiceMuffle

The samples of one voice (VOICE_RATE a second) on their way into the mix. target: 1 behind a wall, 0 in the
open. The voice does not jump from one to the other, f->mix moves there over VOICE_MUFFLE_FADE seconds.

Muffled is the voice through two low-passes of one pole each in a row, at about 700 Hz: 3 kHz comes out at
a twentieth, 300 Hz nearly whole, and all of it a little quieter.
=================
*/
void BG_VoiceMuffle( voiceFilter_t *f, float *samples, int count, float target ) {
	// 1 - exp( -2 pi 700 / VOICE_RATE )
	const float	pole = 0.1675f;
	const float	step = 1.0f / ( VOICE_MUFFLE_FADE * VOICE_RATE );
	int			i;

	for ( i = 0; i < count; i++ ) {
		const float in = samples[i];

		// the filter runs all the time, so it has the right state when the voice fades into it
		f->low[0] += pole * ( in - f->low[0] );
		f->low[1] += pole * ( f->low[0] - f->low[1] );

		if ( f->mix < target ) {
			f->mix += step;
			if ( f->mix > target )
				f->mix = target;
		} else if ( f->mix > target ) {
			f->mix -= step;
			if ( f->mix < target )
				f->mix = target;
		}
		samples[i] = in + f->mix * ( f->low[1] * VOICE_MUFFLE_GAIN - in );
	}
}
