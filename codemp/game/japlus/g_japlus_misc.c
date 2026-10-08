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
JA+ small per client features: timers, messages for players without the client plugin.
Rebuilt from jampgamei386.so (JA+ v2.4 Build 7).
*/

#include "game/g_local.h"
#include "g_japlus.h"

#define JP_PLUGIN_MESSAGE_VERSION	1.1f	// players with an older plugin get messages as center print

/*
=================
JP_NewLines

a "\n" typed into a cvar or a command becomes a line break. The original does this with G_NewString, which takes
its memory from the level pool for good, so here it is done in place.
=================
*/
void JP_NewLines( char *text ) {
	char *out = text;

	for ( ; *text; text++ ) {
		if ( text[0] == '\\' && text[1] == 'n' ) {
			*out++ = '\n';
			text++;
		}
		else
			*out++ = *text;
	}
	*out = '\0';
}

/*
=================
JP_ShowMessage

shows the text for "seconds" seconds as a center print. "pluginlessOnly": only to the players whose plugin
does not display it itself (the votes), otherwise to everybody (the poll)
=================
*/
void JP_ShowMessage( const char *text, int seconds, qboolean pluginlessOnly ) {
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *ent = &g_entities[i];

		if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED )
			continue;
		if ( pluginlessOnly && ent->client->pers.jp.pluginVersion >= JP_PLUGIN_MESSAGE_VERSION )
			continue;

		Q_strncpyz( ent->client->pers.jp.motd, text, sizeof( ent->client->pers.jp.motd ) );
		JP_NewLines( ent->client->pers.jp.motd );
		ent->client->pers.jp.motdTime = seconds;
	}
}

void JP_ClearPluginlessMessages( void ) {
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *ent = &g_entities[i];

		if ( ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED )
			ent->client->pers.jp.motdTime = 0;
	}
}

/*
=================
jp_autoQUIT "<hours>-<HH>:<MM>": after that many hours the server waits for the time of the day, warns
for a minute and quits (a restart script is expected). "no" or "" turns it off
=================
*/
static int autoQuitTime = -1;	// level.time of the next step, -1 off
static int autoQuitState;

void JP_AutoQuitInit( void ) {
	int hours = 0;

	autoQuitTime = -1;
	autoQuitState = 0;
	if ( !jp_autoQUIT.string[0] || !Q_stricmp( jp_autoQUIT.string, "no" ) )
		return;
	if ( sscanf( jp_autoQUIT.string, "%i-", &hours ) == 1 ) {
		G_LogPrintf( "SERVER: AutoQUIT Feature is activated in %i Hours\n", hours );
		autoQuitTime = level.time + hours * 3600000;
	}
}

void JP_AutoQuitFrame( void ) {
	if ( autoQuitTime == -1 || autoQuitTime >= level.time || !jp_autoQUIT.string[0] || !Q_stricmp( jp_autoQUIT.string, "no" ) )
		return;

	if ( autoQuitState == 0 ) {
		int hours = 0, h = 0, m = 0, target, now;
		qtime_t qt;

		autoQuitState = 1;
		if ( sscanf( jp_autoQUIT.string, "%i-%i:%i", &hours, &h, &m ) == 3 ) {
			target = h * 60 + m;
			trap->RealTime( &qt );
			now = qt.tm_hour * 60 + qt.tm_min;
			if ( now >= target )
				target += 24 * 60;
			// the last minute is the warning
			autoQuitTime = level.time + (target - now - 1) * 60000;
			G_LogPrintf( "SERVER: The autoQUIT numbers of hours is reached. The server will QUIT in %i Minutes\n", target - now );
			autoQuitState = 2;
		}
	}
	else if ( autoQuitState == 2 ) {
		autoQuitTime = level.time + 60000;
		trap->SendServerCommand( -1, "cp \"^7Server will Stop in about \n60 seconds\n just type reconnect into console.\n\"" );
		trap->SendServerCommand( -1, "print \"^1Server will Stop in about \n^160 seconds\n^1 just type reconnect into console\n\"" );
		autoQuitState = 3;
	}
	else if ( autoQuitState == 3 ) {
		G_LogPrintf( "SERVER: The autoQUIT tells to make the server QUIT. Your autoRestart script should restart it. If you don't have an autorestart script you can disallow this=> seta jp_autoQUIT no\n" );
		trap->SendConsoleCommand( EXEC_APPEND, "quit\n" );
		autoQuitTime = -1;
	}
}

/*
=================
JP_JediVsMercSpawn

JA+ g_jediVmerc extras at spawn: jedi are only jedi (no cloak, no flame), mercs get flame thrower, cloak and
jetpack when allowed. Level 2: mercs get all items, level 3: jedi get all forces at level 3 and mercs all weapons
=================
*/
static void JP_JediVsMercSpawn( gentity_t *ent, int wDisable ) {
	gclient_t *client = ent->client;
	playerState_t *ps = &client->ps;
	int i;

	if ( ps->trueJedi && !ps->trueNonJedi ) {
		client->pers.jp.flame = qfalse;
		ps->stats[STAT_HOLDABLE_ITEMS] &= ~(1 << HI_CLOAK);
		ps->stats[STAT_WEAPONS] &= ~(1 << WP_MELEE);
		if ( g_jediVmerc.integer == 3 ) {
			for ( i = 0; i < NUM_FORCE_POWERS; i++ ) {
				ps->fd.forcePowerLevel[i] = FORCE_LEVEL_3;
				ps->fd.forcePowersKnown |= (1 << i);
			}
		}
	}
	else if ( ps->trueNonJedi && !ps->trueJedi ) {
		if ( jp_allowFlameThrower.integer == 1 ) {
			client->pers.jp.flame = qtrue;
			ps->fd.forcePowerLevel[FP_LIGHTNING] = FORCE_LEVEL_3;
			ps->fd.forcePowersKnown |= (1 << FP_LIGHTNING);
		}
		if ( jp_allowCloakItem.integer == 1 )
			ps->stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_CLOAK);
		if ( jp_allowJetpack.integer == 1 )
			ps->stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_JETPACK);

		if ( g_jediVmerc.integer > 1 ) {
			for ( i = 1; i < HI_NUM_HOLDABLE; i++ )
				ps->stats[STAT_HOLDABLE_ITEMS] |= (1 << i);
			client->pers.jp.flame = qtrue;
			ps->fd.forcePowerLevel[FP_LIGHTNING] = FORCE_LEVEL_3;
			ps->fd.forcePowersKnown |= (1 << FP_LIGHTNING);
		}
		if ( g_jediVmerc.integer == 3 ) {
			for ( i = WP_STUN_BATON; i <= LAST_USEABLE_WEAPON; i++ ) {
				if ( !wDisable || !(wDisable & (1 << i)) ) {
					ps->stats[STAT_WEAPONS] |= (1 << i);
					ps->ammo[i] = 999;	// (indexed with the weapon number in the original: all kinds of ammo)
				}
			}
			ps->stats[STAT_WEAPONS] &= ~(1 << WP_SABER);
			ps->weapon = WP_MELEE;
		}
	}
}

/*
=================
JP_TeamBalanceSpawn

g_teamForceBalance 2 and 3: a player who respawns in the bigger team is moved to the other one, if he is the one
picked for it: with 2 the first player of the team, with 3 its best player if the other team is behind in
score, its worst one else. Not while a team is locked.
=================
*/
static void JP_TeamBalanceSpawn( gentity_t *ent ) {
	gclient_t *client = ent->client;
	const int clientNum = client->ps.clientNum;
	int red, blue, from, to, pick = -1, i;

	if ( level.gametype < GT_TEAM || g_jediVmerc.integer || g_teamForceBalance.integer < 2 || client->pers.jp.balanceSpawn
		|| (client->sess.sessionTeam != TEAM_RED && client->sess.sessionTeam != TEAM_BLUE)
		|| G_CmdBitAllowed( jp_teamLock.integer, 4 ) || G_CmdBitAllowed( jp_teamLock.integer, 3 ) )
		return;

	blue = TeamCount( clientNum, TEAM_BLUE );
	red = TeamCount( clientNum, TEAM_RED );
	if ( red < blue && client->sess.sessionTeam == TEAM_BLUE ) {
		from = TEAM_BLUE;
		to = TEAM_RED;
	}
	else if ( red > blue && client->sess.sessionTeam == TEAM_RED ) {
		from = TEAM_RED;
		to = TEAM_BLUE;
	}
	else
		return;

	if ( g_teamForceBalance.integer == 3 ) {
		const qboolean best = level.teamScores[to] < level.teamScores[from];

		for ( i = 0; i < level.maxclients; i++ ) {
			const int n = level.sortedClients[best ? i : level.maxclients - 1 - i];
			const gentity_t *other;

			if ( n < 0 || n >= MAX_CLIENTS )
				continue;
			other = &g_entities[n];
			if ( other->inuse && other->client && other->client->pers.connected == CON_CONNECTED
				&& ((int)other->client->sess.sessionTeam == from || (level.gametype == GT_SIEGE && other->client->sess.siegeDesiredTeam == from)) ) {
				pick = n;
				break;
			}
		}
	}
	else if ( g_teamForceBalance.integer == 2 ) {
		for ( i = 0; i < level.maxclients; i++ ) {
			const gentity_t *other = &g_entities[i];

			if ( other->inuse && other->client && other->client->pers.connected == CON_CONNECTED
				&& ((int)other->client->sess.sessionTeam == from || (level.gametype == GT_SIEGE && other->client->sess.siegeDesiredTeam == from)) ) {
				pick = i;
				break;
			}
		}
	}
	if ( pick != clientNum )
		return;

	if ( to == TEAM_RED ) {
		trap->SendServerCommand( -1, va( "print \"%s ^7has been moved to the ^1Red team ^7to balance the teams. \n\"", client->pers.netname ) );
		SetTeam( ent, "r" );
	}
	else {
		trap->SendServerCommand( -1, va( "print \"%s ^7has been moved to the ^4Blue team ^7to balance the teams. \n\"", client->pers.netname ) );
		SetTeam( ent, "b" );
	}
}

/*
=================
JP_RemovePlayerExplosives

JA+: a player who leaves the server or changes his team takes his explosives with him. The det packs, trip
mines and thermal detonators go off without damage, his missiles in the air explode (RemovePlantedDetpacks,
RemoveLaserTrap, RemoveThermalDetonator and RemoveAllMissileFromPlayer of the original).
=================
*/
void JP_RemovePlayerExplosives( gentity_t *ent ) {
	gentity_t *e;
	vec3_t v, origin;
	int i;

	if ( ent->client->ps.hasDetPackPlanted ) {
		for ( e = G_Find( NULL, FOFS( classname ), "detpack" ); e; e = G_Find( e, FOFS( classname ), "detpack" ) ) {
			if ( e->parent != ent )
				continue;
			e->pain = NULL;
			e->die = NULL;
			e->takedamage = qfalse;
			VectorSet( v, 0, 0, 1 );
			if ( e->count == -1 )
				VectorCopy( e->pos2, v );
			G_PlayEffect( EFFECT_EXPLOSION_DETPACK, e->r.currentOrigin, v );
			e->think = G_FreeEntity;
			e->nextthink = level.time;
		}
		ent->client->ps.hasDetPackPlanted = qfalse;
	}

	for ( e = G_Find( NULL, FOFS( classname ), "laserTrap" ); e; e = G_Find( e, FOFS( classname ), "laserTrap" ) ) {
		if ( e->parent != ent )
			continue;
		if ( e->s.weapon != WP_FLECHETTE )
			G_AddEvent( e, EV_MISSILE_MISS, 0 );
		VectorCopy( e->s.pos.trDelta, v );
		if ( e->s.time == -2 )
			VectorClear( v );
		G_PlayEffect( e->s.weapon == WP_FLECHETTE ? EFFECT_EXPLOSION_FLECHETTE : EFFECT_EXPLOSION_TRIPMINE, e->r.currentOrigin, v );
		e->think = G_FreeEntity;
		e->nextthink = level.time;
	}

	for ( e = G_Find( NULL, FOFS( classname ), "thermal_detonator" ); e; e = G_Find( e, FOFS( classname ), "thermal_detonator" ) ) {
		if ( e->parent != ent )
			continue;
		VectorSet( v, 0, 0, 1 );
		e->r.svFlags |= SVF_BROADCAST;
		e->count = 1;
		BG_EvaluateTrajectory( &e->s.pos, level.time, origin );
		origin[2] += 8;
		SnapVector( origin );
		G_SetOrigin( e, origin );
		e->s.eType = ET_GENERAL;
		G_AddEvent( e, EV_MISSILE_MISS, DirToByte( v ) );
		e->freeAfterEvent = qtrue;
		trap->LinkEntity( (sharedEntity_t *)e );
	}

	// everything he fired that is still in the air, except his grappling hook
	for ( i = 0; i < level.num_entities; i++ ) {
		e = &g_entities[i];
		if ( !e->inuse || e->s.eType != ET_MISSILE || e->parent != ent || e->s.weapon <= WP_SABER || e->s.weapon >= WP_NUM_WEAPONS )
			continue;
		if ( !Q_stricmp( e->classname, "hook" ) || !Q_stricmp( e->classname, "hook2" ) )
			continue;
		e->die = NULL;
		e->r.contents = 0;
		G_ExplodeMissile( e );
		e->think = G_FreeEntity;
		e->nextthink = level.time;
	}
}

/*
=================
JP_ClientSpawnReset

The original keeps these states outside of client->pers, so every spawn clears them (ClientSpawn zeroes the
whole client and puts back pers, sess, the force data and the sabers)
=================
*/
void JP_ClientSpawnReset( gclient_t *client ) {
	jpClient_t *jp = &client->pers.jp;

	jp->clanTagWarnState = 0;
	jp->testAnim = 0;
	jp->burnTime = 0;
	jp->burnDebounce = 0;
	jp->saberSwitchTime = 0;
	jp->godTalkDenied = qfalse;
	jp->duelEndTime = 0;
	VectorClear( jp->blockPoint1 );
	jp->blockCurrent = 0;
	jp->quickening = qfalse;
	jp->quickeningTimer = 0;
	jp->teleTime = 0;
	jp->inactProtectTime = 0;
	jp->inactProtectState = 0;
	jp->inactProtectCount = 0;
	jp->fullForceDuel = qfalse;
	jp->stActive = jp->stDone = qfalse;
	jp->stCounter = 0;
	VectorClear( jp->stOrigin );
	VectorClear( jp->stAngles );
	jp->kickHit = qfalse;
	jp->motdTime = 0;
	jp->motd[0] = '\0';
	jp->hooked = qfalse;
	jp->hookTime = 0;
	jp->hookUseTime = 0;
}

/*
=================
JP_ClientSpawnEnd

what JA+ does at the end of ClientSpawn, in its order: the states of the player that outlive his death are put
back (empower, merc, ghost, alternate dimension), the extra items and the team balance
=================
*/
void JP_ClientSpawnEnd( gentity_t *ent, int wDisable ) {
	gclient_t *client = ent->client;
	jpClient_t *jp = &client->pers.jp;
	playerState_t *ps = &client->ps;

	if ( jp->empowered )
		G_GoInEmpower( ent, qtrue );
	JP_WelcomeSpawn( ent );
	JP_HighlanderSpawn( ent );
	if ( jp->merc )
		JP_MercLoadout( ent );
	jp->cheatGod = qfalse;

	if ( !g_jediVmerc.integer ) {
		if ( jp_giveWP_MELEE.integer == 1 )
			ps->stats[STAT_WEAPONS] |= (1 << WP_MELEE);
		if ( jp_allowCloakItem.integer == 1 && level.gametype == GT_FFA )
			ps->stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_CLOAK);
	}
	else
		JP_JediVsMercSpawn( ent, wDisable );

	if ( jp->isGhost )
		G_becomeGhost( ent );
	JP_ApplyModelScale( ent );
	jp->pushGrabber = ENTITYNUM_NONE;
	JP_TeamBalanceSpawn( ent );

	JP_UpdateWeaponSwitchFlag( client );
	if ( jp->sleeping || jp->down || jp->mindtricked || jp->isGhost )
		ent->flags |= FL_GODMODE;
	if ( jp->dimension )
		ps->eFlags |= EF_ALT_DIM;
	else
		ps->eFlags &= ~EF_ALT_DIM;

	if ( jp->balanceSpawn && level.gametype >= GT_TEAM && (client->sess.sessionTeam == TEAM_RED || client->sess.sessionTeam == TEAM_BLUE) ) {
		jp->balanceSpawn = qfalse;
		if ( !(ent->r.svFlags & SVF_BOT) )
			G_LogPrintf( "BalanceTeam spawning client %i in team %i\n", ps->clientNum, client->sess.sessionTeam );
	}

	ent->r.svFlags &= ~SVF_NOCLIENT;
	ent->s.eFlags &= ~EF_NODRAW;
	ps->eFlags &= ~EF_NODRAW;
}

/*
=================
JP_InactivityProtect

jp_inactivityProtectTimer (seconds, FFA without highlander): players without input get protected, the
protection ends with the next input
=================
*/
void JP_InactivityProtect( gentity_t *ent ) {
	gclient_t *client = ent->client;
	jpClient_t *jp = &client->pers.jp;
	const usercmd_t *cmd = &client->pers.cmd;

	if ( (g_gametype.integer == GT_FFA && jp_highlander.integer == 1) || g_gametype.integer != GT_FFA || !jp_inactivityProtectTimer.integer ) {
		if ( level.time + 5000 > jp->inactProtectTime ) {
			jp->inactProtectTime = level.time + 60000;
			jp->inactProtectState = 0;
			jp->inactProtectCount = 0;
		}
		return;
	}

	if ( !cmd->forwardmove && !cmd->rightmove && !cmd->upmove
		&& !(client->ps.eFlags & EF_ALT_DIM) && !(cmd->buttons & (BUTTON_ATTACK|BUTTON_ALT_ATTACK))
		&& !jp->sleeping && !(client->ps.eFlags2 & EF2_HOOK) )
	{
		if ( jp->inactProtectState == 0 || jp->protectedByAdmin ) {
			if ( client->pers.connected != CON_CONNECTED || level.time <= jp->inactProtectTime || jp->inactProtectState != 0 )
				return;
			jp->inactProtectCount = 10;
			jp->protectedByAdmin = qtrue;
			jp->inactProtectState = 1;
			client->ps.eFlags |= EF_INVULNERABLE;
			if ( !client->ps.saberHolstered ) {
				client->ps.weaponTime = 400;
				client->ps.saberHolstered = 2;
			}
			return;
		}
	}

	jp->inactProtectTime = level.time + jp_inactivityProtectTimer.integer * 1000;
	if ( jp->inactProtectState )
		jp->protectedByAdmin = qfalse;
	jp->inactProtectState = 0;
	jp->inactProtectCount = 0;
}

// the welcome text and the saber damage mode, once per connection at the first spawn as a player
void JP_WelcomeSpawn( gentity_t *ent ) {
	jpClient_t *jp = &ent->client->pers.jp;
	const int num = ent-g_entities;
	char welcome[MAX_CVAR_VALUE_STRING];

	if ( jp->welcomed || ent->client->sess.sessionTeam == TEAM_SPECTATOR )
		return;
	jp->welcomed = qtrue;

	Q_strncpyz( welcome, jp_welcomeMSG.string, sizeof( welcome ) );
	JP_NewLines( welcome );
	trap->SendServerCommand( num, va( "print \"\n^3***********************************************\n^5%s\n^3Author^7: Slider\nType ^5/aminfo ^7in your console to display the Command list.\nType ^5/ammove ^7in your console to display the list of NEW MOVES.\nType ^5/amAltDim ^7to switch from 1st dimension to the alternate dimension(No Rules Dimension).\n^3***********************************************\n\n\n%s\n\n\"", "JA+ Mod v2.4 Build 7", welcome ) );
	if ( d_saberSPStyleDamage.integer == 1 && jp_alterDMG.integer == 0 )
		trap->SendServerCommand( num, "print \"^3Single Player Saber Damages (default in baseJKA) are activated!\n\"" );
	else
		trap->SendServerCommand( num, "print \"^3MultiPlayer Saber Damages are activated! (the new JA+ damage system looking like the JK2 one.)\n\"" );
	if ( jp_alterDMG.integer == 1 )
		trap->SendServerCommand( num, "print \"^3In this server the saber damages are altered!\n\"" );
}

/*
=================
JP_GodTalk

jp_allowGodTalk: god mode while the chat bubble is shown, FFA only and not while fighting. Called from
PmoveSingle like in the original, talking: the talk button of the command.
=================
*/
void JP_GodTalk( playerState_t *ps, qboolean talking ) {
	const qboolean ffa = level.gametype == GT_FFA && jp_highlander.integer != 1;
	gentity_t *ent;
	gclient_t *client;
	jpClient_t *jp;

	if ( jp_allowGodTalk.integer != 1 || ps->clientNum < 0 || ps->clientNum >= level.maxclients )
		return;
	ent = &g_entities[ps->clientNum];
	client = ent->client;
	if ( !ent->inuse || !client || client->pers.connected != CON_CONNECTED )
		return;
	jp = &client->pers.jp;

	if ( !talking ) {
		// the bubble is gone: no god mode, unless it comes from somewhere else
		if ( ffa && !jp->sleeping && !jp->isGhost && !jp->down && !jp->mindtricked && !jp->cheatGod )
			ent->flags &= ~FL_GODMODE;
		jp->godTalkDenied = qfalse;
		return;
	}

	if ( ffa && !g_jediVmerc.integer && !client->ps.duelInProgress && !(ent->flags & FL_GODMODE) && !(client->ps.eFlags & EF_ALT_DIM)
		&& !jp->godTalkDenied && !BG_SaberInAttack( client->ps.saberMove ) && !BG_SaberInTransitionAny( client->ps.saberMove )
		&& ps->pm_type != PM_FLOAT && !BG_InGrappleMove( client->ps.legsAnim ) && !BG_InGrappleMove( client->ps.torsoAnim )
		&& !ps->fd.forcePowersActive )
	{
		ent->flags |= FL_GODMODE;
	}

	// a force power used under the chat bubble ends the protection until the bubble is gone
	if ( ps->fd.forcePowersActive > 0 && (ent->flags & FL_GODMODE) ) {
		jp->godTalkDenied = qtrue;
		ent->flags &= ~FL_GODMODE;
	}
}

/*
=================
JP_KissSlap

amkiss: the kissed player (BOTH_KISSEE) slaps the kisser with the force push button instead of pushing
=================
*/
void JP_KissSlap( gentity_t *ent, const usercmd_t *ucmd ) {
	gclient_t *client = ent->client;
	vec3_t fwd, end;
	trace_t tr;

	if ( client->ps.legsAnim != BOTH_KISSEE && client->ps.torsoAnim != BOTH_KISSEE )
		return;
	if ( ent-g_entities >= level.maxclients || client->pers.connected != CON_CONNECTED )
		return;

	client->lastGenCmd = GENCMD_FORCE_THROW;	// with lastGenCmdTime below the push command is ignored
	if ( ucmd->generic_cmd != GENCMD_FORCE_THROW )
		return;

	AngleVectors( client->ps.viewangles, fwd, NULL, NULL );
	VectorMA( client->ps.origin, 40.0f, fwd, end );
	trap->Trace( &tr, client->ps.origin, NULL, NULL, end, ent-g_entities, ent->clipmask, qfalse, 0, 0 );
	if ( tr.entityNum < MAX_CLIENTS && tr.entityNum != ent-g_entities ) {
		gentity_t *other = &g_entities[tr.entityNum];

		if ( other->inuse && other->client && other->client->pers.connected == CON_CONNECTED
			&& (other->client->ps.legsAnim == BOTH_KISSER || other->client->ps.torsoAnim == BOTH_KISSER) )
		{
			JP_EmoteExec( other, BOTH_KISSER1STOP );
			G_Damage( other, ent, ent, end, tr.endpos, 5, DAMAGE_NO_ARMOR, MOD_MELEE );
		}
	}
	client->lastGenCmdTime = level.time + 1500;
	JP_EmoteExec( ent, BOTH_FORCEGRIP3THROW );
	G_Sound( ent, CHAN_AUTO, G_SoundIndex( va( "sound/weapons/melee/punch%d", Q_irand( 1, 4 ) ) ) );
}

/*
=================
JP_UserinfoMayChange

jp_userInfoAntiSpam: more than that many userinfo changes in a short time are ignored
=================
*/
qboolean JP_UserinfoMayChange( int clientNum ) {
	gentity_t *ent = &g_entities[clientNum];
	jpClient_t *jp;
	int limit = jp_userInfoAntiSpam.integer;

	if ( limit == 0 || clientNum < 0 || clientNum >= level.maxclients || !ent->inuse || !ent->client
		|| ent->client->pers.connected != CON_CONNECTED )
		return qtrue;

	jp = &ent->client->pers.jp;
	if ( jp->userinfoCount > limit )
		return qfalse;
	jp->userinfoCount++;
	if ( jp->userinfoCount > limit )
		jp->userinfoCount = limit + 3;
	return qtrue;
}

/*
=================
JP_ClientSecondActions

called once per second from ClientTimerActions
=================
*/
void JP_ClientSecondActions( gentity_t *ent ) {
	gclient_t *client = ent->client;
	jpClient_t *jp = &client->pers.jp;

	if ( jp->inactProtectState && jp->inactProtectCount > 0 )
		jp->inactProtectCount--;
	else if ( jp->inactProtectState && client->sess.sessionTeam != TEAM_SPECTATOR ) {
		// idle and protected: a sitting animation now and then
		client->ps.forceDodgeAnim = BOTH_CONSOLE2;
		client->ps.forceHandExtend = HANDEXTEND_TAUNT;
		client->ps.forceHandExtendTime = level.time + 3000;
		jp->inactProtectCount = 10;
	}

	if ( jp->userinfoCount > 0 && jp->userinfoDecayTime < level.time ) {
		jp->userinfoDecayTime = level.time + 4000;
		jp->userinfoCount--;
	}

	JP_StarTrekSecond( ent );
	JP_HighlanderSecond( ent );

	if ( jp->motdTime ) {
		trap->SendServerCommand( ent-g_entities, va( "cp \"^7%s\nTimeLeft : ^2%i s\"", jp->motd, jp->motdTime ) );
		jp->motdTime--;
	}

	// keep the sleep and the knocked down animation going
	if ( jp->sleeping && client->ps.forceHandExtendTime < level.time + 2000 ) {
		client->ps.forceHandExtend = HANDEXTEND_KNOCKDOWN;
		client->ps.forceHandExtendTime = level.time + 5000;
	}

	// silenced players can not rename themselves either
	if ( jp->silenced )
		client->pers.netnameTime = level.time + 1500;

	if ( jp->down && client->ps.forceHandExtendTime < level.time + 2000 ) {
		client->ps.forceGripChangeMovetype = PM_FLOAT;
		client->ps.forceHandExtend = HANDEXTEND_CHOKE;
		client->ps.forceHandExtendTime = level.time + 5000;
	}
}

/*
=================
JP_ClientConnectInit

called from ClientConnect after the client was cleared
=================
*/
void JP_ClientConnectInit( gclient_t *client, qboolean isBot ) {
	jpClient_t *jp = &client->pers.jp;

	jp->abuseBansLeft = jp_antiAdmAbuseNumberB4ban.integer;
	jp->canSeeGhosts = qtrue;
	jp->modelScale = 1.0f;
	JP_HighlanderConnect( client );

	// everybody starts in the alternate dimension with jp_altDim 2
	jp->dimension = (jp_altDim.integer == 2 && g_gametype.integer == GT_FFA && !g_jediVmerc.integer) ? 1 : 0;

	// bots count as players with the newest plugin
	if ( isBot && sscanf( JP_PLUGIN_VERSION, "%f", &jp->pluginVersion ) != 1 )
		jp->pluginVersion = 0.0f;
}

/*
=================
JP_DisplayLatestPluginVersion

tells the player which client plugin version he runs and which one is the latest the server knows
=================
*/
void JP_DisplayLatestPluginVersion( gentity_t *ent ) {
	const int clientNum = ent-g_entities;
	const float have = ent->client->pers.jp.pluginVersion;
	const char *plugin = G_ClientUsingJAPLUSClient( clientNum );
	float latest = 0.0f;

	if ( sscanf( JP_PLUGIN_VERSION, "%f", &latest ) != 1 || latest <= 0.0f )
		return;
	if ( !plugin || !plugin[0] )
		plugin = "undefined";

	trap->SendServerCommand( clientNum, "print \"^3---------------------\n\"" );
	if ( have < 1.0f ) {
		trap->SendServerCommand( clientNum, va( "print \"^5Your plugin version is ^5%s^7. You haven't the JA+ client plugin installed. Check the JA+ website to get it!!\n       Or check your client plugin installation (do not forget to remove all previous versions of the plugin and never put it in base folder but only JAPLUS folder).\n\"", plugin ) );
	}
	else if ( have < latest ) {
		trap->SendServerCommand( clientNum, va( "print \"^5A new JA+ client plugin version is available. \n              Your client Plugin version is ^5%s ^7and the lastest version known by the server is ^5%s\n\"", plugin, JP_PLUGIN_VERSION ) );
	}
	else if ( have == latest && Q_stricmp( plugin, JP_PLUGIN_VERSION ) ) {
		trap->SendServerCommand( clientNum, va( "print \"Your client Plugin version is ^5%s ^7and the lastest version known by the server is ^5%s\n\"", plugin, JP_PLUGIN_VERSION ) );
	}
	else {
		trap->SendServerCommand( clientNum, va( "print \"Your client Plugin version is ^5%s \n\"", plugin ) );
	}
	trap->SendServerCommand( clientNum, "print \"^3---------------------\n\"" );
}

/*
=================
JP_ClientBeginInit

called at the end of ClientBegin: detects the client plugin, shows the welcome message and the motd
=================
*/
void JP_ClientBeginInit( gentity_t *ent ) {
	gclient_t *client = ent->client;
	jpClient_t *jp = &client->pers.jp;
	const int clientNum = ent-g_entities;

	// the plugin can log in the admin and the clan pass by itself
	{
		char userinfo[MAX_INFO_STRING];
		const char *v;

		trap->GetUserinfo( clientNum, userinfo, sizeof( userinfo ) );
		if ( client->sess.jpAdminLevel == JPADM_NONE ) {
			v = Info_ValueForKey( userinfo, "cp_login" );
			if ( v[0] && Q_stricmp( v, "NOT SET" ) )
				G_amlogin( ent, v, qtrue );
		}
		if ( !jp->clanPass[0] ) {
			v = Info_ValueForKey( userinfo, "cp_clanPwd" );
			if ( v[0] && Q_stricmp( v, "none" ) )
				Q_strncpyz( jp->clanPass, v, sizeof( jp->clanPass ) );
		}
	}

	if ( jp->pluginVersion < 1.0f ) {
		const char *plugin = G_ClientUsingJAPLUSClient( clientNum );
		float version = 0.0f;

		jp->pluginVersion = 0.0f;
		if ( plugin && plugin[0] >= '0' && plugin[0] <= '9' && sscanf( plugin, "%f", &version ) == 1 )
			jp->pluginVersion = version;

		if ( jp->pluginVersion >= 1.0f )
			G_LogPrintf( "%s USES JA+ PLUGIN v%s. => will be known as a %f version.\n", client->pers.netname, plugin, jp->pluginVersion );
	}

	if ( !(ent->r.svFlags & SVF_BOT) )
		JP_DisplayLatestPluginVersion( ent );

	if ( client->sess.sessionTeam != TEAM_SPECTATOR ) {
		trap->SendServerCommand( clientNum, va( "cp \"^5%s\n\n\"", JP_VERSION ) );
		if ( JP_HighlanderOn() )
			Q_strncpyz( jp->motd, va( "^5%s\n ^3\xac^1Highlander ^7FFA Modification^3\xac\n\n^7Kill all immortal\nGet their Forces Powers\n\n^5At the end, there can be only one\n", JP_VERSION ), sizeof( jp->motd ) );
		else
			Q_strncpyz( jp->motd, va( "^5%s\n\n^7%s\n", JP_VERSION, jp_motd.string ), sizeof( jp->motd ) );
		JP_NewLines( jp->motd );
		jp->motdTime = jp_motdtime.integer;
	}

	// the admin script commands start at the first command and the first player
	jp->admCmdIndex = 0;
	jp->admTargetNum = 0;

	// players only see the ghosts if they are admins who asked for it
	if ( client->sess.jpAdminLevel < JPADM_COUNCIL || !jp->canSeeGhosts )
		G_seeGhost( ent, qfalse );
}

/*
=================
JP_AllPlayersHavePlugin

true if every real player has a plugin that knows the JA+ animations
=================
*/
qboolean JP_AllPlayersHavePlugin( void ) {
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		const gentity_t *ent = &g_entities[i];

		if ( ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED && !(ent->r.svFlags & SVF_BOT)
			&& ent->client->pers.jp.pluginVersion < 1.1f )
			return qfalse;
	}
	return qtrue;
}

/*
=================
JP_UpdateServerInfo

builds jp_cinfo, the settings the client plugin needs for its prediction. Called every frame.
=================
*/
void JP_UpdateServerInfo( void ) {
	int f = 0;

	if ( jp_allowflipkick.integer == 1 )	f |= JPF_FLIPKICK;
	if ( jp_fixRoll.integer > 0 )			f |= JPF_FIXROLL1;
	if ( jp_fixRoll.integer > 1 )			f |= JPF_FIXROLL2;
	if ( jp_fixRoll.integer > 2 )			f |= JPF_FIXROLL3;
	if ( jp_improveYellowDFA.integer == 1 )	f |= JPF_IMPROVEYELLOWDFA;
	if ( jp_slideOnPlayer.integer == 1 )	f |= JPF_SLIDEONPLAYER;
	if ( jp_allowSPattacks.integer == 1 )	f |= JPF_SPATTACKS;
	if ( jp_allowNewDFA.integer == 1 )		f |= JPF_NEWDFA;
	if ( jp_allowModelScale.integer == 1 )	f |= JPF_MODELSCALE;
	if ( jp_allowDmgSpeedScale.integer == 1 )	f |= JPF_DMGSPEEDSCALE;
	if ( jp_macroScan.integer == 1 )		f |= JPF_MACROSCAN;
	if ( jp_macroScan.integer != 0 && (jp_macroScan.integer == 1 || JP_AllPlayersHavePlugin()) )	f |= JPF_MACROSCAN_ALL;
	if ( jp_allowSPForces.integer > 0 )		f |= JPF_SPFORCES;
	if ( jp_jk2RedDFA.integer == 1 )		f |= JPF_JK2REDDFA;
	if ( jp_noKATA.integer == 1 )			f |= JPF_NOKATA;
	if ( jp_noAutoReplier.integer == 1 )	f |= JPF_NOAUTOREPLIER;
	if ( jp_newGLAAnims.integer != 0 && (jp_newGLAAnims.integer != 2 || JP_AllPlayersHavePlugin()) )	f |= JPF_NEWGLAANIMS;
	if ( jp_ledgeGrab.integer != 0 )		f |= JPF_LEDGEGRAB;
	if ( jp_altDim.integer != 0 )			f |= JPF_ALTDIM;

	bg_jpFeatures = f;
	bg_jpGripSpeedScale = jp_gripSpeedScale.value;
	bg_jpForceCombo = jp_forceCombo.integer;
	if ( f != jp_cinfo.integer )
		trap->Cvar_Set( "jp_cinfo", va( "%i", f ) );
}

// shared with bg_*.c (pmove): version of the client plugin of a player, 0 if none
float BG_pluginVersion( int clientNum ) {
	const gentity_t *ent;

	if ( clientNum < 0 || clientNum >= level.maxclients )
		return 0.0f;
	ent = &g_entities[clientNum];
	if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED )
		return 0.0f;
	return ent->client->pers.jp.pluginVersion;
}

qboolean BG_pluginDisable( int clientNum, int feature ) {
	const gentity_t *ent;

	if ( feature < 0 || feature >= 15 || clientNum < 0 || clientNum >= level.maxclients )
		return qfalse;
	ent = &g_entities[clientNum];
	if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED )
		return qfalse;
	return (ent->client->pers.jp.pluginDisable & (1 << feature)) != 0;
}

