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

// bg_boombox.c -- the bytes of a RELAY_BOX: what a client does with a box through the masterserver, and what the
// masterserver says of a box (bg_boombox.h). Built into the cgame and into the masterserver. Not from JA+, see
// docs/japlus/boombox.md.
//
// Every length is checked here and nowhere else. What comes out of an unpack is text without anything below a
// space in it, ends inside its buffer, and has numbers in their range.

#include "qcommon/q_shared.h"
#include "game/voice/bg_voice.h"
#include "bg_boombox.h"

#define BOX_COORD	65536		// a box stands within this of the middle of a level on every axis

static void Box_Put4( byte *out, int v ) {
	const unsigned int u = (unsigned int)v;

	out[0] = (byte)( u & 0xff );
	out[1] = (byte)( ( u >> 8 ) & 0xff );
	out[2] = (byte)( ( u >> 16 ) & 0xff );
	out[3] = (byte)( ( u >> 24 ) & 0xff );
}

static int Box_Get4( const byte *in ) {
	return (int)( (unsigned int)in[0] | ( (unsigned int)in[1] << 8 ) | ( (unsigned int)in[2] << 16 ) | ( (unsigned int)in[3] << 24 ) );
}

static void Box_Put2( byte *out, int v ) {
	out[0] = (byte)( v & 0xff );
	out[1] = (byte)( ( v >> 8 ) & 0xff );
}

static int Box_Get2( const byte *in ) {
	return in[0] | ( in[1] << 8 );
}

static int Box_Yaw16( float yaw ) {
	return (int)( AngleNormalize360( yaw ) * 65536.0f / 360.0f ) & 0xffff;
}

// a text from a datagram: len bytes, with nothing below a space, into a buffer of size
static void Box_GetText( const byte *in, int len, char *out, int size ) {
	int i, n = 0;

	for ( i = 0; i < len && n < size - 1; i++ ) {
		if ( in[i] >= ' ' && in[i] != '"' )
			out[n++] = (char)in[i];
	}
	out[n] = '\0';
}

// a place: three whole numbers and a yaw
static void Box_PutPlace( byte *out, const int *origin, float yaw ) {
	Box_Put4( out, origin[0] );
	Box_Put4( out + 4, origin[1] );
	Box_Put4( out + 8, origin[2] );
	Box_Put2( out + 12, Box_Yaw16( yaw ) );
}

static qboolean Box_GetPlace( const byte *in, int *origin, float *yaw ) {
	int k;

	for ( k = 0; k < 3; k++ ) {
		origin[k] = Box_Get4( in + 4 * k );
		if ( origin[k] < -BOX_COORD || origin[k] > BOX_COORD )
			return qfalse;
	}
	*yaw = Box_Get2( in + 12 ) * 360.0f / 65536.0f;
	return qtrue;
}

/*
=================
BG_BoxDoPack

What a client does with a box, as the body of a RELAY_BOX to the master. Returns its length, 0 if it does not fit.
=================
*/
int BG_BoxDoPack( byte *out, int outSize, const boxDo_t *d ) {
	const int	nameLen = Q_min( (int)strlen( d->name ), BOX_NAME_MAX );
	int			len = 3 + nameLen;

	if ( outSize < len || d->box < 0 || d->box > 255 )
		return 0;
	out[0] = (byte)d->what;
	out[1] = (byte)d->box;
	out[2] = (byte)nameLen;
	memcpy( out + 3, d->name, nameLen );
	switch ( d->what ) {
	case BOX_DO_PLACE:
	case BOX_DO_MOVE:
	case BOX_DO_TV:
		if ( len + 14 > outSize )
			return 0;
		Box_PutPlace( out + len, d->origin, d->yaw );
		len += 14;
		break;
	case BOX_DO_SYSTEM:
		if ( len + 3 * 14 > outSize )
			return 0;
		Box_PutPlace( out + len, d->origin, d->yaw );
		Box_PutPlace( out + len + 14, d->parts[0], d->partYaw[0] );
		Box_PutPlace( out + len + 28, d->parts[1], d->partYaw[1] );
		len += 3 * 14;
		break;
	case BOX_DO_PART:
		if ( len + 15 > outSize )
			return 0;
		out[len] = (byte)Com_Clampi( 0, BOX_PARTS - 1, d->value );
		Box_PutPlace( out + len + 1, d->origin, d->yaw );
		len += 15;
		break;
	case BOX_DO_PLAY: {
		const int textLen = (int)strlen( d->text );

		// (a new box: where it goes)
		if ( d->box == 255 ) {
			if ( len + 14 > outSize )
				return 0;
			Box_PutPlace( out + len, d->origin, d->yaw );
			len += 14;
		}
		if ( !textLen || len + textLen > outSize )
			return 0;
		memcpy( out + len, d->text, textLen );
		len += textLen;
		break;
	}
	case BOX_DO_VOLUME:
		if ( len + 1 > outSize )
			return 0;
		out[len++] = (byte)Com_Clampi( 0, 100, d->volume );
		break;
	case BOX_DO_SEEK:
		if ( len + 2 > outSize )
			return 0;
		Box_Put2( out + len, Com_Clampi( 0, 65535, d->value ) );
		len += 2;
		break;
	case BOX_DO_DROP:
		if ( len + 1 > outSize )
			return 0;
		out[len++] = (byte)Com_Clampi( 0, 255, d->value );
		break;
	}
	return ( len <= VOICE_MAX_FRAME ) ? len : 0;
}

// qfalse: no command of this kind, or not whole
qboolean BG_BoxDoUnpack( const byte *in, int len, boxDo_t *d ) {
	int at;

	memset( d, 0, sizeof( *d ) );
	if ( len < 3 || in[2] > BOX_NAME_MAX || 3 + in[2] > len )
		return qfalse;
	d->what = in[0];
	d->box = in[1];
	Box_GetText( in + 3, in[2], d->name, sizeof( d->name ) );
	at = 3 + in[2];
	switch ( d->what ) {
	case BOX_DO_PLACE:
	case BOX_DO_MOVE:
	case BOX_DO_TV:
		return ( len == at + 14 && Box_GetPlace( in + at, d->origin, &d->yaw ) ) ? qtrue : qfalse;
	case BOX_DO_SYSTEM:
		return ( len == at + 3 * 14 && Box_GetPlace( in + at, d->origin, &d->yaw ) && Box_GetPlace( in + at + 14, d->parts[0], &d->partYaw[0] )
			&& Box_GetPlace( in + at + 28, d->parts[1], &d->partYaw[1] ) ) ? qtrue : qfalse;
	case BOX_DO_PART:
		if ( len != at + 15 || in[at] >= BOX_PARTS )
			return qfalse;
		d->value = in[at];
		return Box_GetPlace( in + at + 1, d->origin, &d->yaw );
	case BOX_DO_PLAY:
		if ( d->box == 255 ) {
			if ( len < at + 14 || !Box_GetPlace( in + at, d->origin, &d->yaw ) )
				return qfalse;
			at += 14;
		}
		if ( len <= at || len - at >= BOX_ASK )
			return qfalse;
		Box_GetText( in + at, len - at, d->text, sizeof( d->text ) );
		return d->text[0] ? qtrue : qfalse;
	case BOX_DO_VOLUME:
		if ( len != at + 1 )
			return qfalse;
		d->volume = Com_Clampi( 0, 100, in[at] );
		return qtrue;
	case BOX_DO_SEEK:
		if ( len != at + 2 )
			return qfalse;
		d->value = Box_Get2( in + at );
		return qtrue;
	case BOX_DO_DROP:
		if ( len != at + 1 )
			return qfalse;
		d->value = in[at];
		return qtrue;
	case BOX_DO_PAUSE:
	case BOX_DO_RESUME:
	case BOX_DO_SKIP:
	case BOX_DO_STOP:
	case BOX_DO_REMOVE:
	case BOX_DO_WATCH:
		return ( len == at ) ? qtrue : qfalse;
	default:
		return qfalse;
	}
}

/*
=================
BG_BoxSayPack

What the master says of a box, as the body of a RELAY_BOX to a client. A title is cut where it does not fit,
between two characters of UTF-8. Returns the length, 0 if it does not fit.
=================
*/
static int Box_PutText( byte *out, int room, const char *text ) {
	int len = Q_min( (int)strlen( text ), room );

	// (not inside a character: a byte 10xxxxxx continues one)
	while ( len > 0 && len < (int)strlen( text ) && ( (byte)text[len] & 0xc0 ) == 0x80 )
		len--;
	memcpy( out, text, len );
	return len;
}

int BG_BoxSayPack( byte *out, int outSize, const boxSay_t *s ) {
	int len, n;

	if ( outSize < 2 || outSize > VOICE_MAX_FRAME )
		outSize = VOICE_MAX_FRAME;
	out[0] = (byte)s->what;
	out[1] = (byte)s->box;
	switch ( s->what ) {
	case BOX_SAY_SET:
		if ( outSize < 29 )
			return 0;
		Box_Put2( out + 2, s->rev & 0xffff );
		Box_PutPlace( out + 4, s->origin, s->yaw );
		out[18] = (byte)Com_Clampi( 0, 100, s->volume );
		out[19] = (byte)s->flags;
		Box_Put4( out + 20, s->msec );
		Box_Put2( out + 24, Com_Clampi( 0, 65535, s->length ) );
		out[26] = (byte)Com_Clampi( 0, 255, s->waiting );
		len = 27;
		n = Box_PutText( out + len + 1, Q_min( BOX_TITLE - 1, ( outSize - len - 2 ) / 2 ), s->title );
		out[len] = (byte)n;
		len += 1 + n;
		n = Box_PutText( out + len + 1, Q_min( BOX_TITLE - 1, outSize - len - 1 ), s->next );
		out[len] = (byte)n;
		return len + 1 + n;
	case BOX_SAY_GONE:
		return 2;
	case BOX_SAY_NOTE:
		return 2 + Box_PutText( out + 2, outSize - 2, s->text );
	case BOX_SAY_INFO:
		len = 2;
		n = Box_PutText( out + len + 1, BOX_NAME_MAX, s->owner );
		out[len] = (byte)n;
		len += 1 + n;
		n = Box_PutText( out + len + 1, BOX_NAME_MAX, s->by );
		out[len] = (byte)n;
		return len + 1 + n;
	case BOX_SAY_LIST:
		if ( outSize < 11 + BOX_NAME_MAX )
			return 0;
		Box_Put2( out + 2, s->rev & 0xffff );
		out[4] = (byte)Com_Clampi( 0, 255, s->place );
		out[5] = (byte)Com_Clampi( 0, 255, s->total );
		Box_Put2( out + 6, Com_Clampi( 0, 65535, s->length ) );
		len = 8;
		n = Box_PutText( out + len + 1, Q_min( BOX_TITLE - 1, outSize - len - 2 - BOX_NAME_MAX ), s->title );
		out[len] = (byte)n;
		len += 1 + n;
		n = Box_PutText( out + len + 1, BOX_NAME_MAX, s->by );
		out[len] = (byte)n;
		return len + 1 + n;
	case BOX_SAY_RIG:
		if ( outSize < 31 )
			return 0;
		out[2] = (byte)s->kind;
		Box_PutPlace( out + 3, s->parts[0], s->partYaw[0] );
		Box_PutPlace( out + 17, s->parts[1], s->partYaw[1] );
		return 31;
	case BOX_SAY_HELLO:
		out[2] = (byte)s->caps;
		return 3;
	case BOX_SAY_VIDEO:
		n = (int)strlen( s->video );
		if ( outSize < 3 + 11 || ( n != 0 && n != 11 ) )
			return 0;
		out[2] = (byte)n;
		memcpy( out + 3, s->video, n );
		return 3 + n;
	}
	return 0;
}

qboolean BG_BoxSayUnpack( const byte *in, int len, boxSay_t *s ) {
	int at;

	memset( s, 0, sizeof( *s ) );
	if ( len < 2 )
		return qfalse;
	s->what = in[0];
	s->box = in[1];
	switch ( s->what ) {
	case BOX_SAY_SET:
		if ( len < 29 || !Box_GetPlace( in + 4, s->origin, &s->yaw ) )
			return qfalse;
		s->rev = Box_Get2( in + 2 );
		s->volume = Com_Clampi( 0, 100, in[18] );
		s->flags = in[19];
		s->msec = Box_Get4( in + 20 );
		s->length = Box_Get2( in + 24 );
		s->waiting = in[26];
		at = 27;
		if ( at + 1 + in[at] > len )
			return qfalse;
		Box_GetText( in + at + 1, in[at], s->title, sizeof( s->title ) );
		at += 1 + in[at];
		if ( at >= len || at + 1 + in[at] != len )
			return qfalse;
		Box_GetText( in + at + 1, in[at], s->next, sizeof( s->next ) );
		return qtrue;
	case BOX_SAY_GONE:
		return ( len == 2 ) ? qtrue : qfalse;
	case BOX_SAY_NOTE:
		Box_GetText( in + 2, len - 2, s->text, sizeof( s->text ) );
		return s->text[0] ? qtrue : qfalse;
	case BOX_SAY_INFO:
		at = 2;
		if ( at >= len || in[at] > BOX_NAME_MAX || at + 1 + in[at] > len )
			return qfalse;
		Box_GetText( in + at + 1, in[at], s->owner, sizeof( s->owner ) );
		at += 1 + in[at];
		if ( at >= len || in[at] > BOX_NAME_MAX || at + 1 + in[at] != len )
			return qfalse;
		Box_GetText( in + at + 1, in[at], s->by, sizeof( s->by ) );
		return qtrue;
	case BOX_SAY_LIST:
		if ( len < 10 )
			return qfalse;
		s->rev = Box_Get2( in + 2 );
		s->place = in[4];
		s->total = in[5];
		s->length = Box_Get2( in + 6 );
		at = 8;
		if ( at + 1 + in[at] > len )
			return qfalse;
		Box_GetText( in + at + 1, in[at], s->title, sizeof( s->title ) );
		at += 1 + in[at];
		if ( at >= len || in[at] > BOX_NAME_MAX || at + 1 + in[at] != len )
			return qfalse;
		Box_GetText( in + at + 1, in[at], s->by, sizeof( s->by ) );
		return ( s->place < s->total && s->total <= BOX_QUEUE ) ? qtrue : qfalse;
	case BOX_SAY_RIG:
		if ( len != 31 )
			return qfalse;
		s->kind = in[2];
		return ( Box_GetPlace( in + 3, s->parts[0], &s->partYaw[0] ) && Box_GetPlace( in + 17, s->parts[1], &s->partYaw[1] ) ) ? qtrue : qfalse;
	case BOX_SAY_HELLO:
		if ( len != 3 )
			return qfalse;
		s->caps = in[2];
		return qtrue;
	case BOX_SAY_VIDEO:
		if ( len < 3 || ( in[2] != 0 && in[2] != 11 ) || len != 3 + in[2] )
			return qfalse;
		memcpy( s->video, in + 3, in[2] );
		s->video[in[2]] = '\0';
		return ( !in[2] || strspn( s->video, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_" ) == 11 ) ? qtrue : qfalse;
	}
	return qfalse;
}
