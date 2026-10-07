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
JA+ Highlander FFA modification (jp_highlander 1): every player has lives, kills take the force
powers of the victim, at the end two players fight a final duel.
Rebuilt from jampgamei386.so (JA+ v2.4 Build 7).
*/

#include "g_local.h"
#include "g_japlus.h"

extern void LogExit( const char *string );

static int jpFinalDuelStart;	// level.time the last two players started, 0 if not running

qboolean JP_HighlanderOn( void ) {
	return level.gametype == GT_FFA && jp_highlander.integer == 1;
}

static qboolean JP_HLPlayer( const gentity_t *ent ) {
	return ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED;
}

// the killer gets the force powers of the victim, up to level 3
void G_GetEnemyForces( gentity_t *killer, gentity_t *victim ) {
	int i;

	if ( !JP_HLPlayer( killer ) || !JP_HLPlayer( victim ) )
		return;

	for ( i = 0; i < NUM_FORCE_POWERS; i++ ) {
		const int victimLevel = victim->client->ps.fd.forcePowerLevel[i];

		if ( !(victim->client->ps.fd.forcePowersKnown & (1 << i)) || victimLevel <= 0 )
			continue;

		if ( !(killer->client->ps.fd.forcePowersKnown & (1 << i)) || killer->client->ps.fd.forcePowerLevel[i] < 1 ) {
			killer->client->ps.fd.forcePowersKnown |= (1 << i);
			killer->client->ps.fd.forcePowerLevel[i] = 0;
		}
		killer->client->ps.fd.forcePowerLevel[i] = Q_min( 3, killer->client->ps.fd.forcePowerLevel[i] + victimLevel );
	}
}

/*
=================
CheckEndHighlanderGame

returns -2 if highlander is not running, 0 everybody is out, 1 a winner exists, 2 the final duel
runs, else -1. 0 and 1 end the level. afterDeath is set when called after a player died.
=================
*/
int CheckEndHighlanderGame( qboolean afterDeath ) {
	// the original compares with level.numConnectedClients: spectators and connecting players count too
	const int numClients = level.numConnectedClients;
	int withLives = 0, inGame = 0, ret = -1, i;

	if ( !JP_HighlanderOn() || level.intermissiontime )
		return -2;

	for ( i = 0; i < level.maxclients; i++ ) {
		const gentity_t *ent = &g_entities[i];

		if ( !JP_HLPlayer( ent ) )
			continue;
		if ( ent->client->pers.jp.hlLives > 0 ) {
			withLives++;
			if ( ent->client->sess.sessionTeam != TEAM_SPECTATOR )
				inGame++;
		}
	}

	if ( afterDeath && withLives == 0 && numClients > 0 )
		ret = 0;
	else if ( withLives == 1 && numClients == 1 )
		return -1;
	else if ( inGame == 1 && numClients != withLives && numClients - withLives >= 0 )
		ret = 1;
	else if ( withLives == 1 && numClients > 1 )
		ret = 1;
	else if ( (withLives == 2 && numClients >= 3) || (inGame == 2 && numClients >= 2) ) {
		ret = 2;
		if ( jp_highlanderFinalDualTimeLimit.integer > 0 ) {
			if ( !jpFinalDuelStart )
				jpFinalDuelStart = level.time;
			trap->SendServerCommand( -1, va( "cp \"^5%i s^7 \nto kill one of the final dueler\n before the Time Limit\n\"",
				(jpFinalDuelStart - level.time) / 1000 + jp_highlanderFinalDualTimeLimit.integer * 60 ) );
			return 2;
		}
	}

	if ( withLives > 2 && jp_highlanderFinalDualTimeLimit.integer > 0 )
		jpFinalDuelStart = 0;

	if ( ret == 0 || ret == 1 ) {
		LogExit( "Highlander End Game!" );
		return ret;
	}
	return ret;
}

// level exit when the final duelers did not finish in time (called from CheckExitRules)
qboolean JP_HighlanderTimelimitHit( void ) {
	if ( JP_HighlanderOn() && jp_highlanderFinalDualTimeLimit.integer > 0 && jpFinalDuelStart > 0
		&& jp_highlanderFinalDualTimeLimit.integer * 60000 <= level.time - jpFinalDuelStart ) {
		trap->SendServerCommand( -1, "print \"^5Highlander End Game Timelimit hit.\n\"" );
		LogExit( "Highlander End Game Timelimit hit." );
		return qtrue;
	}
	return qfalse;
}

void JP_HighlanderMapStart( void ) {
	jpFinalDuelStart = 0;
}

// called from player_die for the dying player and the one who killed him
void JP_HighlanderDeath( gentity_t *self, gentity_t *attacker ) {
	if ( !JP_HighlanderOn() || !JP_HLPlayer( self ) )
		return;

	if ( self->client->pers.jp.hlLives > 0 )
		self->client->pers.jp.hlLives--;

	if ( attacker && attacker != self && JP_HLPlayer( attacker ) ) {
		if ( jp_highlanderFinalDualTimeLimit.integer > 0 && jpFinalDuelStart > 0 )
			jpFinalDuelStart = level.time;

		if ( self->client->pers.jp.hlLives == 0 ) {
			// the victim is out: ghost, the killer gets the quickening
			self->client->pers.jp.isGhost = qtrue;
			attacker->client->pers.jp.quickeningTimer = 3;
			attacker->client->pers.jp.quickening = qtrue;
			attacker->client->pers.jp.hlLives++;
			G_GetEnemyForces( attacker, self );
		}
	}
}

// at the spawn: no lives left means ghost, the others are told how many lives they have
void JP_HighlanderSpawn( gentity_t *ent ) {
	if ( JP_HighlanderOn() ) {
		if ( ent->client->pers.jp.hlLives == 0 )
			G_becomeGhost( ent );
		trap->SendServerCommand( ent-g_entities, va( "print \"^5You have %i lives remaining!\n\"", ent->client->pers.jp.hlLives ) );
	}
	CheckEndHighlanderGame( qtrue );
}

void JP_HighlanderConnect( gclient_t *client ) {
	if ( !JP_HighlanderOn() )
		return;
	client->pers.jp.hlLives = jp_highlanderStartingLives.integer;
	client->pers.jp.quickening = qfalse;
	client->pers.jp.quickeningTimer = 0;
	client->pers.jp.canSeeGhosts = qfalse;	// admins do not see the ghosts in this mode by default
}

// once per second: the quickening effect after a kill
void JP_HighlanderSecond( gentity_t *ent ) {
	jpClient_t *jp = &ent->client->pers.jp;

	if ( !JP_HighlanderOn() || !jp->quickening || ent->health < 1 || ent->client->ps.stats[STAT_HEALTH] < 1 )
		return;

	if ( jp->quickeningTimer == 3 ) {
		ent->client->ps.electrifyTime = level.time + 3000;
		trap->SendServerCommand( -1, va( "print \"^5%s got a Quickening : %i lives remaining!\n\"", ent->client->pers.netname, jp->hlLives ) );
		ent->flags |= FL_GODMODE;
	}

	jp->quickeningTimer--;
	if ( jp->quickeningTimer == 1 ) {
		ent->client->ps.forceHandExtend = HANDEXTEND_TAUNT;
		ent->client->ps.forceDodgeAnim = BOTH_KISSER;
		ent->client->ps.forceHandExtendTime = level.time + BG_AnimLength( ent->localAnimIndex, BOTH_KISSER );
	}
	if ( jp->quickeningTimer == 0 ) {
		gentity_t *te = G_TempEntity( ent->client->ps.origin, EV_PLAYER_TELEPORT_IN );

		te->s.clientNum = ent->s.clientNum;
	}
	if ( jp->quickeningTimer < 1 ) {
		ent->flags &= ~FL_GODMODE;
		jp->quickening = qfalse;
		jp->quickeningTimer = 0;
		CheckEndHighlanderGame( qfalse );
	}
}

void Cmd_immortal_f( gentity_t *ent ) {
	int i;

	if ( !JP_HighlanderOn() ) {
		trap->SendServerCommand( ent-g_entities, "print \"This cmd gives the list of all alive player in ^5Highlander FFA Modification \n\"" );
		return;
	}
	for ( i = 0; i < level.maxclients; i++ ) {
		const gentity_t *other = &g_entities[i];

		if ( JP_HLPlayer( other ) && other->client->pers.jp.hlLives > 0 )
			trap->SendServerCommand( ent-g_entities, va( "print \"^3Immortal Player ^7: %s  ^7Lives: ^5%i\n\"", other->client->pers.netname, other->client->pers.jp.hlLives ) );
	}
}

// highlander has its own maximum force rank
int JP_MaxForceRank( void ) {
	if ( JP_HighlanderOn() ) {
		if ( jp_highlanderMaxForceRank.integer == 0 )
			return NUM_FORCE_MASTERY_LEVELS - 1;
		return Q_min( jp_highlanderMaxForceRank.integer, NUM_FORCE_MASTERY_LEVELS - 1 );
	}
	return g_maxForceRank.integer;
}
