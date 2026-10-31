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

// bg_toys.c -- a throw of a toy as the cgame, the game module and the masterserver hand it on. See bg_toys.h.

#include "bg_toys.h"

qboolean BG_ToyValid( const toyThrow_t *toy ) {
	double	speed = 0.0;
	int		i;

	if ( toy->kind <= TOY_HELLO || toy->kind >= TOY_KINDS ) {
		return qfalse;
	}
	for ( i = 0; i < 3; i++ ) {
		if ( toy->origin[i] <= -TOY_MAX_COORD || toy->origin[i] >= TOY_MAX_COORD
			|| toy->velocity[i] < -TOY_MAX_SPEED || toy->velocity[i] > TOY_MAX_SPEED ) {
			return qfalse;
		}
		speed += (double)toy->velocity[i] * toy->velocity[i];
	}
	// a spray carries angles and the number of its picture in place of a speed
	if ( toy->kind == TOY_SPRAY && ( toy->velocity[0] < -90 || toy->velocity[0] > 90 || toy->velocity[1] < -180
		|| toy->velocity[1] > 180 || toy->velocity[2] < 0 || toy->velocity[2] >= TOY_SPRAY_MOST ) ) {
		return qfalse;
	}
	return (qboolean)( speed <= (double)TOY_MAX_SPEED * TOY_MAX_SPEED );
}

int BG_ToyPack( const toyThrow_t *toy, byte *out ) {
	int i, n = 0;

	out[n++] = (byte)toy->kind;
	if ( toy->kind == TOY_HELLO ) {
		return n;
	}
	for ( i = 0; i < 3; i++ ) {
		const unsigned int v = (unsigned int)toy->origin[i];

		out[n++] = (byte)v;
		out[n++] = (byte)( v >> 8 );
		out[n++] = (byte)( v >> 16 );
		out[n++] = (byte)( v >> 24 );
	}
	for ( i = 0; i < 3; i++ ) {
		const unsigned int v = (unsigned int)toy->velocity[i];

		out[n++] = (byte)v;
		out[n++] = (byte)( v >> 8 );
	}
	return n;
}

qboolean BG_ToyUnpack( const byte *data, int len, toyThrow_t *out ) {
	int i, n = 1;

	memset( out, 0, sizeof( *out ) );
	if ( len < 1 ) {
		return qfalse;
	}
	out->kind = data[0];
	if ( out->kind == TOY_HELLO ) {
		return (qboolean)( len == TOY_HELLO_BYTES );
	}
	if ( len != TOY_BYTES ) {
		return qfalse;
	}
	for ( i = 0; i < 3; i++, n += 4 ) {
		out->origin[i] = (int)( (unsigned int)data[n] | (unsigned int)data[n + 1] << 8 | (unsigned int)data[n + 2] << 16
			| (unsigned int)data[n + 3] << 24 );
	}
	for ( i = 0; i < 3; i++, n += 2 ) {
		out->velocity[i] = (short)( data[n] | data[n + 1] << 8 );
	}
	return BG_ToyValid( out );
}
