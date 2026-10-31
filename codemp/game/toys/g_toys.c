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

// g_toys.c -- the toys of issue #9 on a server of this tree (game/toys/bg_toys.h): the cgame of a player says it
// threw one with the client command
//
//   vs_toy <kind> <x> <y> <z> <vx> <vy> <vz>
//
// and every other client whose cgame draws toys (v_caps bit 3) is sent
//
//   vs_toy <client> <kind> <x> <y> <z> <vx> <vy> <vz>
//
// Nothing of it is in the game state: a legacy client never sees a command it does not know, and a toy hurts
// nobody. The game module says in the server info that it does this (vs_caps bit 9). A throw is taken only from
// a player who plays and is alive, from near his eyes, and TOY_PER_SECOND a second at most. Not from JA+, see
// docs/japlus/toys.md.

#include "game/g_local.h"
#include "game/toys/bg_toys.h"

#define TOY_REACH		160		// how far from the eyes of the thrower a toy may leave the hand

/*
=================
Cmd_Toy_f

vs_toy <kind> <x> <y> <z> <vx> <vy> <vz>: a player threw a toy
=================
*/
void Cmd_Toy_f( gentity_t *ent ) {
	gclient_t	*client = ent->client;
	jpClient_t	*jp = &client->pers.jp;
	toyThrow_t	toy;
	vec3_t		eyes, from;
	char		arg[16];
	int			i;

	if ( trap->Argc() != 8 || client->sess.sessionTeam == TEAM_SPECTATOR || client->ps.pm_type == PM_SPECTATOR
		|| ent->health <= 0 || level.intermissiontime ) {
		return;
	}
	if ( level.time - jp->toySecond >= 1000 || level.time < jp->toySecond ) {
		jp->toySecond = level.time;
		jp->toyCount = 0;
	}
	if ( ++jp->toyCount > TOY_PER_SECOND ) {
		return;
	}
	trap->Argv( 1, arg, sizeof( arg ) );
	toy.kind = atoi( arg );
	for ( i = 0; i < 3; i++ ) {
		trap->Argv( 2 + i, arg, sizeof( arg ) );
		toy.origin[i] = atoi( arg );
		trap->Argv( 5 + i, arg, sizeof( arg ) );
		toy.velocity[i] = atoi( arg );
	}
	if ( !BG_ToyValid( &toy ) ) {
		return;
	}
	VectorCopy( client->ps.origin, eyes );
	eyes[2] += client->ps.viewheight;
	VectorSet( from, (float)toy.origin[0], (float)toy.origin[1], (float)toy.origin[2] );
	if ( Distance( eyes, from ) > TOY_REACH ) {
		return;
	}
	for ( i = 0; i < level.maxclients; i++ ) {
		const gentity_t *other = &g_entities[i];

		if ( other == ent || !other->inuse || !other->client || other->client->pers.connected != CON_CONNECTED
			|| !( other->client->pers.jp.statusCaps & 8 ) ) {
			continue;
		}
		trap->SendServerCommand( i, va( "vs_toy %i %i %i %i %i %i %i %i", (int)( ent - g_entities ), toy.kind,
			toy.origin[0], toy.origin[1], toy.origin[2], toy.velocity[0], toy.velocity[1], toy.velocity[2] ) );
	}
}
