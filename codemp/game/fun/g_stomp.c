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
A giant shakes the ground. A player who is more than three times his size (the JA+ model scale, iModelScale above
STOMP_MIN_SCALE) is felt by who is near him: with every step, where he pushes off for a jump and where he
lands, the view of the others shakes, a heavy step is heard and a ring of dust comes up. g_stomp 0 switches it
off, g_stomp 2 says each one on the console of the server, with whom it was sent to and how strong.

Server side only, with what every client of the game knows: the shake is the screen shake event, one for each
player near enough with what is left of it at his distance, sent to him alone (and to who follows him as a
spectator). The step is a sound of the
rancor and the dust an effect of the game, as events at the spot. So a client without the cgame of this tree
gets all of it.

The server does not know when a foot touches the ground, that is in the animation and only a client plays it.
A step here is where the movement code's cycle of steps turns over (bobCycle, what Quake 3 made its footstep
sounds from), which keeps the pace of the legs but not the very moment.

Not from JA+ and not from JKAdmin.
*/

#include "game/g_local.h"

#define STOMP_MIN_SCALE		300			// percent: only a player above this
#define STOMP_MAX_GROWTH	4.0f		// twice that size is twice as strong and felt twice as far, up to this
#define STOMP_OWN_VIEW		0.6f		// of the shake for the one whose steps they are, or his picture never stands still
#define STOMP_MIN_SHAKE		0.1f		// what is left of it at a distance is not sent below this
#define STOMP_GROUND		64			// below the origin, at normal size: how far the ground is looked for
#define STOMP_STEP_SPEED	60.0f		// units a second over the ground: slower is no step

static const struct stompKind_s {
	float		shake;			// of a view right at the spot, at STOMP_MIN_SCALE. A client takes up to 16
	int			radius;			// how far it is felt, at STOMP_MIN_SCALE
	int			time;			// msec the view shakes
	qboolean	far;			// the sound carries
	const char	*name;
} stompKinds[STOMP_NUM_KINDS] = {
	// What the game itself does for a step of the rancor is 4 for a second (NPC_AI_Rancor.c). A first set of
	// numbers a third of that was not noticed at all, least by the giant himself, whose camera is far out.
	{  4.0f,	 900,	 450,	qfalse,	"step" },	// STOMP_STEP
	{  3.0f,	 800,	 350,	qfalse,	"jump" },	// STOMP_JUMP
	{  7.0f,	1200,	 700,	qtrue,	"landing" },	// STOMP_LAND
	{ 10.0f,	1600,	1000,	qtrue,	"fall" },	// STOMP_FALL
};

static int	stompSounds[2];
static int	stompDust;

/*
=================
G_StompInit

The names go into the config strings while the level loads, where the clients read them
=================
*/
void G_StompInit( void ) {
	stompSounds[0] = G_SoundIndex( "sound/chars/rancor/step1.mp3" );
	stompSounds[1] = G_SoundIndex( "sound/chars/rancor/step2.mp3" );
	stompDust = G_EffectIndex( "env/impact_dustonly" );
}

/*
=================
G_StompGiant
=================
*/
static qboolean G_StompGiant( const gentity_t *ent ) {
	if ( !g_stomp.integer || !ent->client || ent->s.eType != ET_PLAYER ) {
		return qfalse;
	}
	if ( ent->client->ps.iModelScale <= STOMP_MIN_SCALE || ent->health <= 0 ) {
		return qfalse;
	}
	if ( ent->client->sess.sessionTeam == TEAM_SPECTATOR || ent->client->ps.pm_type == PM_SPECTATOR ) {
		return qfalse;
	}
	return qtrue;
}

/*
=================
G_StompFollowed

Is a spectator looking through this player's eyes
=================
*/
static qboolean G_StompFollowed( int clientNum ) {
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		const gclient_t *cl = &level.clients[i];

		if ( cl->pers.connected == CON_CONNECTED && cl->sess.spectatorState == SPECTATOR_FOLLOW
				&& cl->sess.spectatorClient == clientNum ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
=================
G_Stomp
=================
*/
void G_Stomp( gentity_t *ent, stompKind_t kind ) {
	const struct stompKind_s *k = &stompKinds[kind];
	vec3_t spot, end, up = { -90.0f, 0.0f, 0.0f };	// angles: the effect goes upward
	trace_t tr;
	float growth, radius;
	int i, sent = 0;
	float strongest = 0.0f;

	if ( !G_StompGiant( ent ) ) {
		return;
	}

	// the ground below him. The origin of a player is at his middle, and a giant's is that much higher.
	VectorCopy( ent->client->ps.origin, end );
	end[2] -= STOMP_GROUND * ( ent->client->ps.iModelScale / 100.0f );
	trap->Trace( &tr, ent->client->ps.origin, NULL, NULL, end, ent->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
	if ( tr.fraction == 1.0f || tr.startsolid ) {
		return;
	}
	VectorCopy( tr.endpos, spot );

	growth = ent->client->ps.iModelScale / (float)STOMP_MIN_SCALE;
	if ( growth > STOMP_MAX_GROWTH ) {
		growth = STOMP_MAX_GROWTH;
	}
	radius = k->radius * growth;

	G_SoundAtLoc( spot, k->far ? CHAN_LESS_ATTEN : CHAN_AUTO, stompSounds[Q_irand( 0, 1 )] );
	G_PlayEffectID( stompDust, spot, up );

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *other = &g_entities[i];
		gentity_t *te;
		float dist, shake;

		if ( !other->inuse || !other->client || other->client->pers.connected != CON_CONNECTED ) {
			continue;
		}
		// A spectator who follows somebody is sent what is for that player (the server goes by the client
		// number of the player state, which is the followed one's), and it is that player's view he has.
		if ( other->client->sess.spectatorState == SPECTATOR_FOLLOW ) {
			continue;
		}
		// a bot has no screen, but who follows one has
		if ( ( other->r.svFlags & SVF_BOT ) && !G_StompFollowed( i ) ) {
			continue;
		}
		dist = Distance( other->client->ps.origin, spot );
		if ( dist >= radius ) {
			continue;
		}
		shake = k->shake * growth * ( 1.0f - dist / radius );
		if ( other == ent ) {
			shake *= STOMP_OWN_VIEW;
		}
		if ( shake < STOMP_MIN_SHAKE ) {
			continue;
		}
		// For nobody by number: a client compares that with the player it shows, and it is sent to him alone
		// anyway. Broadcast: the ground shakes behind a wall too.
		te = G_ScreenShake( spot, NULL, shake, k->time, qtrue );
		te->r.svFlags |= SVF_SINGLECLIENT;
		te->r.singleClient = i;
		// for the cgame of this tree, which can leave these out (cg_stomp). Other clients do not read it.
		te->s.angles[1] = SHAKE_STOMP;
		sent++;
		if ( shake > strongest ) {
			strongest = shake;
		}
	}
	if ( g_stomp.integer >= 2 ) {
		trap->Print( "stomp: %s of %s^7 (scale %i) at %s, shake sent to %i, the strongest %.1f for %i msec\n", k->name,
			ent->client->pers.netname, ent->client->ps.iModelScale, vtos( spot ), sent, strongest, k->time );
	}
}

/*
=================
G_StompStep

After the movement of a player: bobCycle before it. The cycle has two steps, one each time it passes a half.
=================
*/
void G_StompStep( gentity_t *ent, int oldBobCycle ) {
	const playerState_t *ps = &ent->client->ps;

	if ( !( ( ( oldBobCycle + 64 ) ^ ( ps->bobCycle + 64 ) ) & 128 ) ) {
		return;
	}
	if ( ps->groundEntityNum == ENTITYNUM_NONE || ent->waterlevel > 1 ) {
		return;
	}
	if ( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] < STOMP_STEP_SPEED * STOMP_STEP_SPEED ) {
		return;
	}
	G_Stomp( ent, STOMP_STEP );
}
