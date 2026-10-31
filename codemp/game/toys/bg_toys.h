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
bg_toys.h -- the toys of issue #9 that only players of this tree see (cgame/toys/cg_toys.c): a throw is where a
toy leaves the hand and how fast, and every cgame that hears of it lets it fly on its own. Nothing of it is in
the game state of the server, so legacy clients see nothing and no server has to know the toys.

A throw goes to the others the way hats do: on a server of this tree through the game module (game/toys/g_toys.c,
the commands vs_toy), on any other through the masterserver (VOICE_TOY, master/ms_relay.c). There it is these
bytes after the head of the datagram: the kind, the point (three int32, units), the speed (three int16, units a
second), all little endian. TOY_HELLO alone, without the rest, tells the masterserver that this cgame draws toys.
*/

#pragma once

#include "qcommon/q_shared.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define TOY_HELLO			0		// to the masterserver only: this cgame wants the throws of the others
#define TOY_SNOWBALL		1
#define TOY_WATER			2		// the jet of a water gun: where it leaves the gun and how fast, again and again while it sprays
#define TOY_KINDS			3

#define TOY_BYTES			19		// a throw as bytes: 1 + 3 * 4 + 3 * 2
#define TOY_HELLO_BYTES		1
#define TOY_MAX_SPEED		1600	// units a second, more is refused
#define TOY_MAX_COORD		65536	// a point further out than this is in no level
#define TOY_PER_SECOND		4		// throws of one player a second, more are dropped by the relays

typedef struct {
	int		kind;					// TOY_*
	int		origin[3];				// where it leaves the hand
	int		velocity[3];			// units a second
} toyThrow_t;

// a throw that can be: a known toy, inside a level, not faster than TOY_MAX_SPEED
qboolean	BG_ToyValid( const toyThrow_t *toy );
// the bytes of a throw (TOY_BYTES), or of a hello (TOY_HELLO_BYTES). Returns how many.
int			BG_ToyPack( const toyThrow_t *toy, byte *out );
// qtrue: a hello (len TOY_HELLO_BYTES, kind TOY_HELLO) or a throw that BG_ToyValid takes
qboolean	BG_ToyUnpack( const byte *data, int len, toyThrow_t *out );

#if defined(__cplusplus)
} // extern "C"
#endif
