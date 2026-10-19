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

// g_unlagged.c -- lag compensation (2026-10-19), as jaPRO has it (g_unlagged, its G_StoreTrail and
// G_TimeShiftAllClients in g_active.c), which goes back to the Unlagged mod of Neil "haste" Toronto for Quake 3.
//
// A client draws the others where the snapshots it has put them, interpolated at the time its commands carry
// (usercmd_t serverTime). A shot of that client reaches the server later by its ping, when the others have moved
// on. So the server keeps where every client was at the end of each of its last frames, moves the others back to
// where they were at the shooter's serverTime for the shot, and puts them back afterwards. Nothing of this goes
// over the network: every client gets it, old and stock ones too. Bots are not shifted for (they aim at the
// present), sabers neither (as in jaPRO).
//
// What a trace hits of a player is the box at r.currentOrigin and, for the disruptor and other ghoul2 traces, the
// model turned by s.apos.trBase[YAW] (SV_ClipMoveToEntities). Both are shifted. The pose of the model is the one
// of the present: the server poses it at sv.time for every trace, jaPRO's shift of the animation numbers does not
// reach that.
//
// tay_unlagged (bits of UNLAGGED_*, 7 all), tay_unlaggedMax: at most this many ms back.

#include "game/g_local.h"

static int	unlaggedTime;			// the time the shifted clients are at
static int	unlaggedHits;			// PERS_HITS of the shooter before the shot, for the developer line

static void Unlagged_Read( const gentity_t *ent, unlaggedPoint_t *p ) {
	p->time = level.time;
	VectorCopy( ent->r.currentOrigin, p->origin );
	VectorCopy( ent->r.mins, p->mins );
	VectorCopy( ent->r.maxs, p->maxs );
	p->yaw = ent->s.apos.trBase[YAW];
}

static void Unlagged_Write( gentity_t *ent, const unlaggedPoint_t *p ) {
	VectorCopy( p->origin, ent->r.currentOrigin );
	VectorCopy( p->mins, ent->r.mins );
	VectorCopy( p->maxs, ent->r.maxs );
	ent->s.apos.trBase[YAW] = p->yaw;
	trap->LinkEntity( (sharedEntity_t *)ent );		// absmin and absmax anew
}

/*
=================
G_UnlaggedClear

A client that is not in the level (spectator, joining) has no trail
=================
*/
void G_UnlaggedClear( gentity_t *ent ) {
	if ( ent->client ) {
		ent->client->unlagged.count = 0;
	}
}

/*
=================
G_UnlaggedStore

From ClientEndFrame, once a server frame for every client in the level: where it is now. A teleport (the bit
flips, also at every spawn) starts the trail anew, so no shot finds a client half way between two places.
=================
*/
void G_UnlaggedStore( gentity_t *ent ) {
	unlagged_t	*u = &ent->client->unlagged;
	const int	teleportBit = ent->client->ps.eFlags & EF_TELEPORT_BIT;

	if ( u->count && teleportBit != u->teleportBit ) {
		u->count = 0;
	}
	u->teleportBit = teleportBit;

	// a second call in the same frame takes the place of the first
	if ( !u->count || u->trail[u->head].time != level.time ) {
		u->head = ( u->head + 1 ) % UNLAGGED_TRAIL;
		u->count = Q_min( u->count + 1, UNLAGGED_TRAIL );
	}
	Unlagged_Read( ent, &u->trail[u->head] );
}

/*
=================
G_UnlaggedLag

How many ms behind the server the shooter saw the others: 0 when nothing is to be shifted
=================
*/
int G_UnlaggedLag( const gentity_t *shooter ) {
	int lag;

	if ( !shooter->client || shooter->s.number >= MAX_CLIENTS || shooter->s.eType == ET_NPC
		|| ( shooter->r.svFlags & SVF_BOT ) || level.intermissiontime ) {
		return 0;
	}
	lag = level.time - shooter->client->pers.cmd.serverTime;
	return Com_Clampi( 0, Com_Clampi( 0, 1000, tay_unlaggedMax.integer ), lag );
}

static void Unlagged_Lerp( const vec3_t from, float frac, const vec3_t to, vec3_t out ) {
	vec3_t delta;

	VectorSubtract( to, from, delta );
	VectorMA( from, frac, delta, out );
}

// where the client was at time, between the two points of its trail around it
static qboolean Unlagged_At( const unlagged_t *u, int time, unlaggedPoint_t *out ) {
	const unlaggedPoint_t	*newer = &u->trail[u->head];
	int						i;

	if ( !u->count || time >= newer->time ) {
		return qfalse;
	}
	for ( i = 1; i < u->count; i++ ) {
		const unlaggedPoint_t *older = &u->trail[( u->head - i + UNLAGGED_TRAIL ) % UNLAGGED_TRAIL];

		if ( older->time <= time ) {
			const float frac = (float)( time - older->time ) / (float)( newer->time - older->time );

			out->time = time;
			Unlagged_Lerp( older->origin, frac, newer->origin, out->origin );
			Unlagged_Lerp( older->mins, frac, newer->mins, out->mins );
			Unlagged_Lerp( older->maxs, frac, newer->maxs, out->maxs );
			out->yaw = LerpAngle( older->yaw, newer->yaw, frac );
			return qtrue;
		}
		newer = older;
	}
	// older than the trail: the oldest point
	*out = *newer;
	return qtrue;
}

/*
=================
G_UnlaggedShift

Moves every other client in the level back to where the shooter saw it. what is one of UNLAGGED_*: nothing is
shifted when tay_unlagged lacks it. qtrue when something was shifted, then G_UnlaggedRestore() must follow in the
same frame.
=================
*/
qboolean G_UnlaggedShift( gentity_t *shooter, int what ) {
	const int	lag = ( tay_unlagged.integer & what ) ? G_UnlaggedLag( shooter ) : 0;
	qboolean	shifted = qfalse;
	int			i;

	if ( lag <= 0 ) {
		return qfalse;
	}
	unlaggedTime = level.time - lag;
	unlaggedHits = shooter->client->ps.persistant[PERS_HITS];

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t	*ent = &g_entities[i];
		unlagged_t	*u;

		if ( ent == shooter || !ent->inuse || !ent->client || ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
			continue;
		}
		u = &ent->client->unlagged;
		if ( u->isShifted || !Unlagged_At( u, unlaggedTime, &u->shifted ) ) {
			continue;
		}
		Unlagged_Read( ent, &u->saved );
		Unlagged_Write( ent, &u->shifted );
		u->isShifted = qtrue;
		shifted = qtrue;
	}
	return shifted;
}

/*
=================
G_UnlaggedRestore

Puts the shifted clients back. A client the shot itself moved (a teleport, a respawn) keeps where it went.
=================
*/
void G_UnlaggedRestore( gentity_t *shooter ) {
	float	farthest = 0.0f;
	int		count = 0;
	int		i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t	*ent = &g_entities[i];
		unlagged_t	*u;

		if ( !ent->client ) {
			continue;
		}
		u = &ent->client->unlagged;
		if ( !u->isShifted ) {
			continue;
		}
		u->isShifted = qfalse;
		if ( !ent->inuse || !VectorCompare( ent->r.currentOrigin, u->shifted.origin ) ) {
			continue;
		}
		farthest = Q_max( farthest, Distance( u->saved.origin, u->shifted.origin ) );
		count++;
		Unlagged_Write( ent, &u->saved );
	}

	if ( developer.integer && shooter->client ) {
		Com_Printf( "unlagged: %s^7 %d ms back, %d players shifted, the farthest by %.1f, %s\n",
			shooter->client->pers.netname, level.time - unlaggedTime, count, farthest,
			shooter->client->ps.persistant[PERS_HITS] != unlaggedHits ? "hit" : "no hit" );
	}
}
