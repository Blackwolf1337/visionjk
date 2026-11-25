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


// cg_duelannounce.c -- at the end of the own duel the announcer says whether it was won or lost (issue #23): a
// private duel of JA+ (EV_PRIVATE_DUEL 2 when the fight begins, 0 when it ends) and a duel of the duel game type
// (its two duelists, while both live). Only when a death decided it, not when the opponent left.

#include "cgame/cg_local.h"
#include "cgame/duel/cg_duelannounce.h"

#define ANNOUNCE_AT		700		// msec after the end

static struct {
	qboolean	fighting;
	int			opponent;
	int			endTime;		// 0: no announcement waits
	qboolean	won;
} da;

void CG_DuelAnnounceInit( void ) {
	memset( &da, 0, sizeof( da ) );
}

// the one the own player duels, -1: none (also while he follows somebody: that is not his duel)
static int DA_Opponent( void ) {
	const playerState_t *ps = &cg.snap->ps;

	if ( ps->pm_flags & PMF_FOLLOW ) {
		return -1;
	}
	if ( ps->duelInProgress ) {
		return ps->duelIndex;
	}
	if ( cgs.gametype == GT_DUEL && cgs.clientinfo[ps->clientNum].team != TEAM_SPECTATOR ) {
		return ps->clientNum == cgs.duelist1 ? cgs.duelist2 : ps->clientNum == cgs.duelist2 ? cgs.duelist1 : -1;
	}
	return -1;
}

static void DA_Begin( int opponent ) {
	da.fighting = qtrue;
	da.opponent = opponent;
	da.endTime = 0;
}

// the duel is over: decided by a death, the announcement follows
static void DA_End( void ) {
	const qboolean meDead = (qboolean)( cg.snap->ps.stats[STAT_HEALTH] <= 0 || cg.predictedPlayerState.pm_type == PM_DEAD );
	const qboolean himDead = (qboolean)( da.opponent >= 0 && da.opponent < MAX_CLIENTS
		&& ( cg_entities[da.opponent].currentState.eFlags & EF_DEAD ) );

	da.fighting = qfalse;
	if ( meDead || himDead ) {
		da.won = (qboolean)( !meDead && himDead );
		da.endTime = cg.time;
	}
}

// EV_PRIVATE_DUEL of the own player (cg_event.c)
void CG_DuelAnnouncePrivate( int eventParm ) {
	if ( !cg.snap ) {
		return;
	}
	if ( eventParm == 2 ) {
		DA_Begin( cg.snap->ps.duelIndex );
	} else if ( !eventParm && da.fighting ) {
		DA_End();
	}
}

// every frame (cg_view.c)
void CG_DuelAnnounceFrame( void ) {
	int opponent;

	if ( !cg.snap ) {
		return;
	}
	opponent = DA_Opponent();
	// the duel game type has no events for this: the fight runs while both duelists live and the match is on
	if ( cgs.gametype == GT_DUEL && !cg.snap->ps.duelInProgress ) {
		const qboolean fighting = (qboolean)( opponent >= 0 && opponent < MAX_CLIENTS && cgs.clientinfo[opponent].infoValid
			&& cg_entities[opponent].currentValid && !( cg_entities[opponent].currentState.eFlags & EF_DEAD )
			&& cg.snap->ps.stats[STAT_HEALTH] > 0 && !cg.warmup );

		if ( !da.fighting && fighting ) {
			DA_Begin( opponent );
		} else if ( da.fighting && !fighting ) {
			DA_End();
		}
	} else if ( da.fighting && opponent != da.opponent ) {
		da.fighting = qfalse;		// gone without an end of the duel (the opponent left)
	}
	if ( da.endTime && cg.time - da.endTime >= ANNOUNCE_AT ) {
		trap->S_StartLocalSound( da.won ? cgs.media.winnerSound : cgs.media.loserSound, CHAN_ANNOUNCER );
		da.endTime = 0;
	}
}
