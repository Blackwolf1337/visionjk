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

// g_temple.c -- the Vision Temple as a server. Not from JA+, see docs/japlus/server.md, "The Vision Temple".
//
// A server with g_visionTemple 1 on the map vision_temple is the place the clients of this tree travel to: its
// players walk, jump, talk and use the emotes there, as the models they play with, and nothing else. Nobody has a
// weapon or a force power but the jump, nobody can be hurt, and nobody is in the way of another (the last two
// rules are in the shared movement code, PmoveSingle(), so the client predicts them: the server says the mode in
// its server info). The map has what one stands on and no picture (tools/temple/build.py): the renderer of a
// client of this tree draws the place (rd-vulkan-vision/shaders/temple.frag).

#include "game/g_local.h"

#define TEMPLE_LOST		-512.0f		// nobody gets under the floors: who is there anyway starts again

void G_TempleInit( void ) {
	bg_visionTemple = g_visionTemple.integer;
}

static void Temple_Strip( gentity_t *ent ) {
	gclient_t		*client = ent->client;
	playerState_t	*ps = &client->ps;
	int				i;

	ent->takedamage = qfalse;
	if ( ps->stats[STAT_WEAPONS] != ( 1 << WP_MELEE ) || ps->weapon != WP_MELEE ) {
		ps->stats[STAT_WEAPONS] = 1 << WP_MELEE;
		ps->weapon = WP_MELEE;
		ps->weaponstate = WEAPON_READY;
		client->pers.cmd.weapon = WP_MELEE;
	}
	ps->stats[STAT_HOLDABLE_ITEMS] = 0;
	ps->stats[STAT_HOLDABLE_ITEM] = 0;
	if ( ps->fd.forcePowersKnown != ( 1 << FP_LEVITATION ) ) {
		for ( i = 0; i < NUM_FORCE_POWERS; i++ ) {
			if ( i != FP_LEVITATION ) {
				ps->fd.forcePowerLevel[i] = 0;
			}
		}
		ps->fd.forcePowersKnown = 1 << FP_LEVITATION;
		ps->fd.forcePowersActive &= 1 << FP_LEVITATION;
	}
	if ( ps->fd.forcePowerLevel[FP_LEVITATION] < FORCE_LEVEL_1 ) {
		ps->fd.forcePowerLevel[FP_LEVITATION] = FORCE_LEVEL_1;
	}
}

// at the end of ClientSpawn()
void G_TempleSpawn( gentity_t *ent ) {
	if ( !g_visionTemple.integer || ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
		return;
	}
	Temple_Strip( ent );
}

// Before the movement of a command. qfalse: the player was put back to a start, the command is not his any more.
qboolean G_TempleThink( gentity_t *ent, usercmd_t *ucmd ) {
	if ( !g_visionTemple.integer || ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
		return qtrue;
	}
	ucmd->buttons &= BUTTON_TALK | BUTTON_GESTURE | BUTTON_WALKING | BUTTON_USE | BUTTON_ANY;
	// of what a key asks for at once only the gestures are left
	if ( ucmd->generic_cmd < GENCMD_TAUNT || ucmd->generic_cmd > GENCMD_GLOAT ) {
		ucmd->generic_cmd = 0;
	}
	ucmd->weapon = WP_MELEE;
	Temple_Strip( ent );
	if ( ent->client->ps.origin[2] < TEMPLE_LOST && ent->health > 0 ) {
		ClientSpawn( ent );
		return qfalse;
	}
	return qtrue;
}
