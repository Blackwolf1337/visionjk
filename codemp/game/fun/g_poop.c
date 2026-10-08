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
The emote "ampoop": the player sits down in the air, strains with three moans and leaves a small rock behind
on the floor, with a splat and a sigh of relief. The rock lies there for a while and then goes away.

The command and its rules are with the other emotes in g_japlus_cmds.c (Cmd_ampoop_f), this file is what
happens after the emote was let through.

Server side only, the client code is untouched: the moans are the pain sounds of his own model, the rock is a
plain model entity (a chunk of the rock debris of the game), the splat a stock sound.

Not from JA+ and not from JKAdmin.
*/

#include "game/g_local.h"

#define POOP_TIME		3200		// msec the emote takes
#define POOP_DROP		2500		// msec until the rock comes
#define POOP_MOAN_MSEC	800			// between two moans
#define POOP_MOANS		3
#define POOP_STAY		90000		// msec a rock lies there
#define POOP_MAX_ROCKS	24			// on the map at a time, then the oldest goes
#define POOP_MODEL		"models/chunks/rock/rock1_1.md3"	// 7 units wide

static struct {
	int			start;		// level.time the emote began, 0: none runs
	int			moans;		// done
} poop[MAX_CLIENTS];

// the rocks that lie around, oldest first in a ring
static const char	poopClassname[] = "poop_rock";
static int			poopRocks[POOP_MAX_ROCKS];
static int			poopRockNext;

// configstring numbers, looked up when first needed. 0: not yet, or the table of the map is full
static const char *const poopMoanNames[POOP_MOANS] = { "*pain25.wav", "*pain50.wav", "*pain75.wav" };
static int poopMoans[POOP_MOANS], poopRelief, poopSplat, poopModel;

static qboolean Poop_IsRock( const gentity_t *ent ) {
	return (qboolean)( ent->inuse && ent->classname == poopClassname );
}

// a small rock on the floor under his behind
static void Poop_Drop( gentity_t *ent ) {
	vec3_t		angles, fwd, top, bottom;
	trace_t		tr;
	gentity_t	*rock;

	if ( !poopModel )
		poopModel = G_TryConfigstringIndex( POOP_MODEL, CS_MODELS, MAX_MODELS );
	// G_Spawn raises an error when the entities run out, and an error ends a dedicated server
	if ( !poopModel || level.num_entities > ENTITYNUM_MAX_NORMAL - 64 )
		return;

	VectorSet( angles, 0, ent->client->ps.viewangles[YAW], 0 );
	AngleVectors( angles, fwd, NULL, NULL );
	VectorMA( ent->client->ps.origin, -10.0f, fwd, top );
	VectorCopy( top, bottom );
	bottom[2] -= 96.0f;
	trap->Trace( &tr, top, NULL, NULL, bottom, ent-g_entities, MASK_SOLID, qfalse, 0, 0 );
	if ( tr.startsolid || tr.allsolid || tr.fraction == 1.0f )
		return;		// no floor there

	// room in the ring: the oldest one goes
	if ( poopRocks[poopRockNext] >= MAX_CLIENTS && poopRocks[poopRockNext] < ENTITYNUM_MAX_NORMAL
		&& Poop_IsRock( &g_entities[poopRocks[poopRockNext]] ) )
		G_FreeEntity( &g_entities[poopRocks[poopRockNext]] );

	rock = G_Spawn();
	rock->classname = (char *)poopClassname;
	rock->s.eType = ET_GENERAL;
	rock->s.modelindex = poopModel;
	rock->s.iModelScale = Q_irand( 90, 150 );
	rock->r.contents = 0;
	VectorSet( rock->r.mins, -4, -4, -4 );
	VectorSet( rock->r.maxs, 4, 4, 4 );

	tr.endpos[2] += 3.0f;		// the model reaches 3 units below its origin
	G_SetOrigin( rock, tr.endpos );
	VectorSet( angles, 0, Q_irand( 0, 359 ), 0 );
	VectorCopy( angles, rock->s.angles );
	VectorCopy( angles, rock->s.apos.trBase );
	VectorCopy( angles, rock->r.currentAngles );

	rock->think = G_FreeEntity;
	rock->nextthink = level.time + POOP_STAY;
	trap->LinkEntity( (sharedEntity_t *)rock );

	poopRocks[poopRockNext] = rock-g_entities;
	poopRockNext = ( poopRockNext + 1 ) % POOP_MAX_ROCKS;

	if ( !poopSplat )
		poopSplat = G_TryConfigstringIndex( "sound/player/fallsplat.wav", CS_SOUNDS, MAX_SOUNDS );
	if ( poopSplat )
		G_Sound( rock, CHAN_AUTO, poopSplat );
}

static void Poop_Voice( gentity_t *ent, int *index, const char *name ) {
	if ( !*index )
		*index = G_TryConfigstringIndex( name, CS_SOUNDS, MAX_SOUNDS );
	if ( *index )
		G_EntitySound( ent, CHAN_VOICE, *index );
}

/*
=================
G_PoopStart

The emote was let through for this player. Returns how long it takes, in msec.
=================
*/
int G_PoopStart( gentity_t *ent ) {
	const int num = ent-g_entities;

	if ( num < 0 || num >= MAX_CLIENTS )
		return 0;

	poop[num].start = level.time;
	poop[num].moans = 0;
	return POOP_TIME;
}

/*
=================
G_PoopFrame

Once per server frame
=================
*/
void G_PoopFrame( void ) {
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t	*ent = &g_entities[i];
		gclient_t	*client = ent->client;
		int			elapsed;

		if ( !poop[i].start )
			continue;

		// gone, dead, or something knocked him out of the emote: nothing comes
		if ( !ent->inuse || !client || client->pers.connected != CON_CONNECTED || ent->health <= 0
			|| client->ps.forceHandExtend != HANDEXTEND_TAUNT ) {
			poop[i].start = 0;
			continue;
		}

		elapsed = level.time - poop[i].start;

		if ( poop[i].moans < POOP_MOANS && elapsed >= poop[i].moans * POOP_MOAN_MSEC ) {
			Poop_Voice( ent, &poopMoans[poop[i].moans], poopMoanNames[poop[i].moans] );
			poop[i].moans++;
		}

		if ( elapsed >= POOP_DROP ) {
			Poop_Drop( ent );
			Poop_Voice( ent, &poopRelief, "*gasp.wav" );
			poop[i].start = 0;
		}
	}
}
