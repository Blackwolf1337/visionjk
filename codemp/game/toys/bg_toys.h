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
the commands vs_toy), on any other through the masterserver (VOICE_TOY). There it is these
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
#define TOY_RC				3		// a remote controlled car: where it is, its speed over the ground (x, y) and its yaw (z,
									//	degrees from -180 to 180), again and again while it drives
#define TOY_RC_GONE			4		// that car is put away (the point says where, the speed is 0)
#define TOY_SPRAY			5		// a picture sprayed on a wall: the point, and in the speed the pitch and the yaw of the
									//	wall's normal (degrees) and the number of the picture (gfx/vision/sprays.txt)
#define TOY_EMOJI			6		// an emoji sent to a player: the point it leaves (the eyes), and in the speed the client
									//	number of the player it flies to, the number of the picture (gfx/vision/sprays.txt), 0
#define TOY_TABLE			7		// a table of table tennis or (z 1) a tennis court: where it stands (its middle on the floor),
									//	and in the speed its yaw and the client who plays its other end (MAX_CLIENTS: nobody),
									//	again and again. TOY_TABLE_GONE to TOY_PONG_PADDLE are the same for a court.
#define TOY_TABLE_GONE		8		// the table of who sends it is put away
#define TOY_PONG_JOIN		9		// to play at the other end of the table of the client in the speed (x), or (y 1) to stop
#define TOY_PONG_BALL		10		// the ball of table tennis was hit: where it is and how fast
#define TOY_PONG_SCORE		11		// the points of the owner of the table and of the other one (x, y) and who serves (z 0 the
									//	owner, 1 the other one), from who lost the last point
#define TOY_PONG_PADDLE		12		// where the paddle of who sends it is, six times a second while he plays
#define TOY_BOARD			13		// a drawing board is held (z: 0 put away, 1 in front, 2 over the head) with the picture of
									//	the store of the site x * TOY_BOARD_SPLIT + y (0: none yet), again and again
#define TOY_SPRAY_STORE		14		// a picture of the player's own sprayed on a wall: the point, and in the speed the angles of
									//	the wall's normal (x: TOY_SPRAY_ANGLES, pitch and yaw in steps of TOY_SPRAY_STEP
									//	degrees) and the number of the picture in the store of the site y * TOY_BOARD_SPLIT + z
#define TOY_EMOTE_STORE		15		// an interactive emoji of the store of the site sent to a player: the point it leaves (the
									//	eyes), and in the speed the client number of the player it flies to and its
									//	number in the list of the store y * TOY_BOARD_SPLIT + z (cgame/toys/cg_emote.c)
#define TOY_KINDS			16
#define TOY_BOARD_SPLIT		1500	// the number of a picture in two fields of the speed
#define BOARD_W				256		// a picture of a drawing board: pixels across and down, colours of the palette
#define BOARD_H				128		//	(cgame/toys/cg_board.c, drawings of modelstore.py of the site)
#define BOARD_COLORS		32
#define TOY_PONG_MOST		99		// points of one player in a game at most
#define TOY_SPRAY_MOST		1400	// pictures a spray can be (so the "speed" of TOY_SPRAY stays under TOY_MAX_SPEED)
#define TOY_SPRAY_STEP		10		// degrees of a step of the angles of TOY_SPRAY_STORE (the receiver finds the wall itself)
#define TOY_SPRAY_YAWS		( 360 / TOY_SPRAY_STEP + 1 )			// -180 to 180
#define TOY_SPRAY_ANGLES	( ( 180 / TOY_SPRAY_STEP + 1 ) * TOY_SPRAY_YAWS )	// pitch -90 to 90 times the yaws

#define TOY_BYTES			19		// a throw as bytes: 1 + 3 * 4 + 3 * 2
#define TOY_HELLO_BYTES		1
#define TOY_MAX_SPEED		1600	// units a second, more is refused
#define TOY_MAX_COORD		65536	// a point further out than this is in no level
#define TOY_PER_SECOND		10		// throws of one player a second, more are dropped by the relays (a car says where it is
									//	six times a second)

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
