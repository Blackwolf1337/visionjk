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
JA+ dimensions and duel interference: players in the alternate dimension and players in a
private duel do not interfere with the others.
Rebuilt from jampgamei386.so (JA+ v2.4 Build 7).
*/

#include "game/g_local.h"
#include "g_japlus.h"

qboolean JP_IsConnectedClient( const gentity_t *ent ) {
	return ent && ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED;
}

/*
=================
G_PlInSameDim

true if both players are in the same dimension (both primary or both alternate)
=================
*/
qboolean G_PlInSameDim( const gentity_t *a, const gentity_t *b ) {
	if ( !JP_IsConnectedClient( a ) || !JP_IsConnectedClient( b ) )
		return qfalse;

	return ((a->client->ps.eFlags & EF_ALT_DIM) != 0) == ((b->client->ps.eFlags & EF_ALT_DIM) != 0);
}

/*
=================
G_canInterferWith

true if the player "a" and the player "b" may interact (hit, push, hook each other)
=================
*/
qboolean G_canInterferWith( int a, int b ) {
	const gentity_t *entA, *entB;

	if ( a < 0 || a >= MAX_CLIENTS || b < 0 || b >= MAX_CLIENTS || a == b )
		return qtrue;

	entA = &g_entities[a];
	entB = &g_entities[b];
	if ( !JP_IsConnectedClient( entA ) || !JP_IsConnectedClient( entB ) )
		return qtrue;

	// only FFA and TDM have these rules
	if ( g_gametype.integer != GT_FFA && g_gametype.integer != GT_TEAM )
		return qtrue;

	if ( jp_altDim.integer && g_gametype.integer == GT_FFA && !G_PlInSameDim( entA, entB ) )
		return qfalse;

	if ( jp_DuelAlpha.integer >= 0 ) {
		const playerState_t *psA = &entA->client->ps, *psB = &entB->client->ps;
		const qboolean duelingEachOther = psA->duelInProgress && psB->duelInProgress
			&& psA->duelIndex == b && psB->duelIndex == a;

		if ( !duelingEachOther && (psA->duelInProgress || psB->duelInProgress) )
			return qfalse;
	}

	return qtrue;
}

/*
=================
JP_DamageBlocked

true if the damage must be ignored: players that are ghosts, mindtricked or asleep can neither
hurt nor be hurt, and players in different dimensions do not interact
=================
*/
qboolean JP_DamageBlocked( gentity_t *targ, gentity_t *attacker ) {
	if ( !attacker || !attacker->client || attacker->client->NPC_class == CLASS_VEHICLE )
		return qfalse;

	if ( JP_IsConnectedClient( targ ) ) {
		const jpClient_t *jp = &targ->client->pers.jp;

		if ( jp->isGhost || jp->mindtricked || jp->sleeping )
			return qtrue;
	}

	if ( JP_IsConnectedClient( attacker ) && (attacker->client->pers.jp.isGhost || attacker->client->pers.jp.mindtricked) )
		return qtrue;

	if ( jp_altDim.integer && JP_IsConnectedClient( targ ) && JP_IsConnectedClient( attacker ) && !G_PlInSameDim( targ, attacker ) )
		return qtrue;

	return qfalse;
}

// players that are ghosts, mindtricked or asleep are no enemies of anybody
qboolean JP_Untargetable( const gentity_t *ent ) {
	return JP_IsConnectedClient( ent ) && (ent->client->pers.jp.isGhost || ent->client->pers.jp.mindtricked || ent->client->pers.jp.sleeping);
}

/*
=================
non interference traces

Players in the other dimension and players in a duel with somebody else do not stop traces
and movement of a player. G_TempUnlink makes the list of those players for a client, JP_Trace
clears their contents for the time of the trace.
=================
*/
static int jpUnlinked[MAX_CLIENTS][MAX_CLIENTS];
static int jpNumUnlinked[MAX_CLIENTS];

static qboolean JP_NonInterferenceRules( void ) {
	return ((g_gametype.integer == GT_TEAM || g_gametype.integer == GT_FFA) && jp_DuelAlpha.integer >= 0)
		|| (g_gametype.integer == GT_FFA && jp_altDim.integer);
}

void G_TempUnlink( int clientNum ) {
	const gentity_t *me;
	int i;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS )
		return;

	jpNumUnlinked[clientNum] = 0;
	me = &g_entities[clientNum];
	if ( !JP_IsConnectedClient( me ) )
		return;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const gentity_t *other = &g_entities[i];
		qboolean skip;

		if ( i == clientNum || !other->inuse || !other->client || other->health < 1 || other->client->ps.stats[STAT_HEALTH] < 1
			|| other->client->sess.sessionTeam == TEAM_SPECTATOR )
			continue;

		if ( !me->client->ps.duelInProgress ) {
			skip = (other->client->ps.duelInProgress && jp_DuelAlpha.integer >= 0)
				|| (jp_altDim.integer && !G_PlInSameDim( me, other ));
		}
		else {
			skip = (jp_DuelAlpha.integer >= 0 && (!other->client->ps.duelInProgress || other->client->ps.duelIndex != clientNum))
				|| (jp_altDim.integer && !G_PlInSameDim( me, other ));
		}

		if ( skip )
			jpUnlinked[clientNum][jpNumUnlinked[clientNum]++] = i;
	}
}

void JP_Trace( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentMask,
	int capsule, int traceFlags, int useLod ) {
	int saved[MAX_CLIENTS], count = 0, i;
	const qboolean active = passEntityNum >= 0 && passEntityNum < MAX_CLIENTS && JP_NonInterferenceRules();

	if ( active ) {
		count = jpNumUnlinked[passEntityNum];
		for ( i = 0; i < count; i++ ) {
			gentity_t *ent = &g_entities[jpUnlinked[passEntityNum][i]];

			saved[i] = ent->r.contents;
			ent->r.contents = 0;
		}
	}

	trap->Trace( results, start, mins, maxs, end, passEntityNum, contentMask, capsule, traceFlags, useLod );

	for ( i = 0; i < count; i++ )
		g_entities[jpUnlinked[passEntityNum][i]].r.contents = saved[i];
}
