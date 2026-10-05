/*
JA+ teleporting: amtele, amtelemark, amorigin, refuseTele and the star trek teleport effect.
Rebuilt from jampgamei386.so (JA+ v2.4 Build 7).
*/

#include "g_local.h"
#include "g_japlus.h"

extern void Jedi_Cloak( gentity_t *self );
extern void Jedi_Decloak( gentity_t *self );
extern void hurt_touch( gentity_t *self, gentity_t *other, trace_t *trace );

#define JP_STARTREK_DELAY	3		// seconds the cloaked player waits before he is beamed

static void JP_Reply( gentity_t *ent, qboolean rcon, const char *text ) {
	if ( rcon )
		trap->Print( "%s", text );
	else
		trap->SendServerCommand( ent-g_entities, va( "print \"%s\"", text ) );
}

static gentity_t *JP_ValidPlayer( int num ) {
	gentity_t *ent;

	if ( num < 0 || num >= level.maxclients )
		return NULL;
	ent = &g_entities[num];
	if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED )
		return NULL;
	return ent;
}

/*
=================
JP_TeleportNow: the plain teleport without the star trek delay
=================
*/
static void JP_TeleportNow( gentity_t *ent, const vec3_t origin, const vec3_t angles, qboolean effects ) {
	gclient_t *client = ent->client;
	const int oldType = ent->s.eType;
	gentity_t *tent;

	if ( effects && client->sess.sessionTeam != TEAM_SPECTATOR ) {
		tent = G_TempEntity( client->ps.origin, EV_PLAYER_TELEPORT_OUT );
		tent->s.clientNum = ent->s.clientNum;
		tent = G_TempEntity( (float *)origin, EV_PLAYER_TELEPORT_IN );
		tent->s.clientNum = ent->s.clientNum;
	}

	JP_HookFree( ent );
	trap->UnlinkEntity( (sharedEntity_t *)ent );

	VectorCopy( origin, client->ps.origin );
	client->ps.origin[2] += 1.0f;
	VectorClear( client->ps.velocity );
	client->ps.pm_time = 160;
	client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
	client->ps.eFlags ^= EF_TELEPORT_BIT;

	SetClientViewAngle( ent, (float *)angles );
	if ( client->sess.sessionTeam != TEAM_SPECTATOR )
		G_KillBox( ent );

	BG_PlayerStateToEntityState( &client->ps, &ent->s, qtrue );
	if ( oldType == ET_NPC )
		ent->s.eType = ET_NPC;

	VectorCopy( client->ps.origin, ent->r.currentOrigin );
	if ( client->sess.sessionTeam != TEAM_SPECTATOR )
		trap->LinkEntity( (sharedEntity_t *)ent );
}

/*
=================
JP_ManualTeleport

jp_starTrekTeleport 1: the player is cloaked for a few seconds first, JP_StarTrekSecond beams him
=================
*/
void JP_ManualTeleport( gentity_t *ent, const vec3_t origin, const vec3_t angles ) {
	jpClient_t *jp = &ent->client->pers.jp;

	if ( jp_starTrekTeleport.integer == 1 && ent->client->sess.sessionTeam != TEAM_SPECTATOR ) {
		if ( !jp->stActive && !jp->stDone ) {
			jp->stCounter = JP_STARTREK_DELAY;
			jp->stActive = qtrue;
			jp->stDone = qfalse;
			Jedi_Cloak( ent );
			VectorCopy( origin, jp->stOrigin );
			VectorCopy( angles, jp->stAngles );
		}
		return;
	}

	JP_TeleportNow( ent, origin, angles, qtrue );
}

// once per second from JP_ClientSecondActions
void JP_StarTrekSecond( gentity_t *ent ) {
	jpClient_t *jp = &ent->client->pers.jp;

	if ( jp_starTrekTeleport.integer != 1 || !jp->stActive )
		return;

	jp->stCounter--;
	if ( jp->stCounter >= 2 )
		return;

	if ( !jp->stDone && jp->stCounter >= 0 ) {
		jp->stDone = qtrue;
		JP_TeleportNow( ent, jp->stOrigin, jp->stAngles, qfalse );
	}
	else if ( jp->stCounter < 0 ) {
		jp->stActive = qfalse;
		jp->stDone = qfalse;
		Jedi_Decloak( ent );
	}
}

/*
=================
amtele

amtele <player> <x> <y> <z> <yaw>, amtele <x> <y> <z> <yaw>, amtele <player> <to player>,
amtele <to player>, amtele (to the tele marker)
=================
*/
void Cmd_amtele_f( gentity_t *ent, qboolean rcon ) {
	char cmd[MAX_TOKEN_CHARS], a1[MAX_TOKEN_CHARS], a2[MAX_TOKEN_CHARS], a3[MAX_TOKEN_CHARS], a4[MAX_TOKEN_CHARS], a5[MAX_TOKEN_CHARS];
	const int argc = trap->Argc();
	vec3_t dest = { 0, 0, 0 }, angles = { 0, 0, 0 }, below, start;
	gentity_t *target, *to;
	trace_t tr;
	int targetNum, toNum = -1;

	if ( g_gametype.integer != GT_FFA || g_jediVmerc.integer ) {
		JP_Reply( ent, rcon, "^5amtele ^7is disallowed in this gametype!\n" );
		return;
	}

	trap->Argv( 0, cmd, sizeof( cmd ) );
	if ( argc == 6 ) {
		trap->Argv( 1, a1, sizeof( a1 ) );
		trap->Argv( 2, a2, sizeof( a2 ) ); trap->Argv( 3, a3, sizeof( a3 ) );
		trap->Argv( 4, a4, sizeof( a4 ) ); trap->Argv( 5, a5, sizeof( a5 ) );
		targetNum = G_getClientNumberForString( a1 );
	}
	else if ( !rcon && argc == 5 ) {
		targetNum = ent-g_entities;
		trap->Argv( 1, a2, sizeof( a2 ) ); trap->Argv( 2, a3, sizeof( a3 ) );
		trap->Argv( 3, a4, sizeof( a4 ) ); trap->Argv( 4, a5, sizeof( a5 ) );
	}
	else if ( argc == 3 ) {
		trap->Argv( 1, a1, sizeof( a1 ) );
		trap->Argv( 2, a2, sizeof( a2 ) );
		targetNum = G_getClientNumberForString( a1 );
		toNum = G_getClientNumberForString( a2 );
	}
	else if ( rcon )
		return;
	else if ( argc == 2 ) {
		targetNum = ent-g_entities;
		trap->Argv( 1, a1, sizeof( a1 ) );
		toNum = G_getClientNumberForString( a1 );
	}
	else if ( argc == 1 && ent->client->pers.jp.teleMarkSet )
		targetNum = ent-g_entities;
	else
		return;

	if ( !rcon && !G_ClientAdminCmdAllowed( ent, cmd, qtrue ) )
		return;

	if ( !(target = JP_ValidPlayer( targetNum )) )
		return;
	if ( !rcon && !G_ClientAdminCmdAllowedOn( ent, target, qtrue, qtrue ) )
		return;

	if ( target->client->ps.eFlags & EF_ALT_DIM ) {
		JP_Reply( ent, rcon, "^1You cannot use this cmd on a player inside the Alternate dimension.!\n" );
		return;
	}
	if ( BG_InGrappleMove( target->client->ps.legsAnim ) || BG_InGrappleMove( target->client->ps.torsoAnim ) )
		return;

	target->client->pers.jp.teleTime = level.time + 5000;
	if ( target->client->pers.jp.refuseTele ) {
		JP_Reply( ent, rcon, "^1this player refuses to be teleported!\n" );
		return;
	}

	if ( rcon || (ent != target) ) {
		trap->SendServerCommand( targetNum, "print \"^5You can refuse to be teleported typing ^1/refuseTele ^5into the console!\n^5retype it to accept again\n\"" );
	}

	if ( !rcon && ent == target && argc == 1 ) {
		const jpClient_t *jp = &ent->client->pers.jp;

		VectorCopy( jp->teleMarkOrigin, dest );
		angles[YAW] = (float)jp->teleMarkYaw;
	}
	else if ( argc == 3 || (!rcon && argc == 2) ) {
		if ( !(to = JP_ValidPlayer( toNum )) )
			return;
		VectorSet( dest, to->client->ps.origin[0] + 60.0f, to->client->ps.origin[1], to->client->ps.origin[2] + 50.0f );
		VectorSet( below, to->client->ps.origin[0] - dest[0], 0.0f, to->client->ps.origin[2] - dest[2] );
		angles[YAW] = vectoyaw( below );
	}
	else {
		VectorSet( dest, (float)atoi( a2 ), (float)atoi( a3 ), (float)atoi( a4 ) );
		angles[YAW] = (float)atoi( a5 );
	}

	// refuse destinations over lava, slime or a trigger_hurt
	VectorCopy( dest, start );
	VectorSet( below, dest[0], dest[1], dest[2] - 5000.0f );
	trap->Trace( &tr, start, NULL, NULL, below, target-g_entities, 0x21007, qfalse, 0, 0 );
	if ( tr.fraction < 1.0f && (tr.contents & (CONTENTS_LAVA|CONTENTS_SLIME)) ) {
		JP_Reply( ent, rcon, rcon ? "^5It seems there is lava under the teleport Point\n" : "^5It seems there is lava or Slime under the teleport Point\n" );
		return;
	}
	trap->Trace( &tr, start, NULL, NULL, below, target-g_entities, 0x1401, qfalse, 0, 0 );
	if ( tr.fraction < 1.0f && (tr.contents & CONTENTS_TRIGGER) ) {
		vec3_t mins, maxs;
		int touch[MAX_GENTITIES], num, i;

		VectorSet( mins, tr.endpos[0] - 40.0f, tr.endpos[1] - 40.0f, tr.endpos[2] - 52.0f );
		VectorSet( maxs, tr.endpos[0] + 40.0f, tr.endpos[1] + 40.0f, tr.endpos[2] + 52.0f );
		num = trap->EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );
		for ( i = 0; i < num; i++ ) {
			const gentity_t *hit = &g_entities[touch[i]];

			if ( hit->touch == hurt_touch && (hit->r.contents & CONTENTS_TRIGGER) ) {
				JP_Reply( ent, rcon, "^5It seems there is a trigger_hurt under the teleport Point\n" );
				return;
			}
		}
	}

	JP_ManualTeleport( target, dest, angles );
	if ( !rcon && ent->client->sess.jpAdminLevel == JPADM_RCON )
		G_AdminCSDisplay( targetNum, jp_msgTargetTeleport.string, "" );
	else
		G_AdminCSDisplay( targetNum, jp_msgTargetTeleport.string, jp_msgBroadcastTeleport.string );

	if ( !rcon && ent->client->sess.jpAdminLevel != JPADM_RCON )
		G_LogPrintf( "ADMIN CMD EXECUTED : %s executed by %s\n", cmd, ent->client->pers.netname );
}

void Cmd_amtelemark_f( gentity_t *ent, qboolean rcon ) {
	jpClient_t *jp = &ent->client->pers.jp;
	char cmd[MAX_TOKEN_CHARS];

	trap->Argv( 0, cmd, sizeof( cmd ) );
	if ( !G_ClientAdminCmdAllowed( ent, cmd, qtrue ) )
		return;

	jp->teleMarkSet = qtrue;
	VectorCopy( ent->client->ps.origin, jp->teleMarkOrigin );
	jp->teleMarkYaw = (int)ent->client->ps.viewangles[YAW];
	trap->SendServerCommand( ent-g_entities, va( "print \"TELEPORT MARKER :^3 %s\n\"", vtos( ent->client->ps.origin ) ) );
}

void Cmd_amorigin_f( gentity_t *ent, qboolean rcon ) {
	char cmd[MAX_TOKEN_CHARS], arg[MAX_TOKEN_CHARS];
	gentity_t *target;
	int num;

	trap->Argv( 0, cmd, sizeof( cmd ) );
	if ( !Q_stricmp( cmd, "admCmdEXE" ) || strstr( cmd, "gun" ) ) {
		const int idx = ent->client->pers.jp.admCmdIndex;

		if ( idx < 0 || idx >= JP_NUM_ADMIN_CMDS_A )
			return;
		Q_strncpyz( cmd, jp_adminCmdsA[idx], sizeof( cmd ) );
		num = ent->client->pers.jp.admTargetNum;
	}
	else if ( trap->Argc() == 1 )
		num = ent-g_entities;
	else {
		trap->Argv( 1, arg, sizeof( arg ) );
		num = G_getClientNumberForString( arg );
	}

	if ( !G_ClientAdminCmdAllowed( ent, cmd, qtrue ) )
		return;
	if ( !(target = JP_ValidPlayer( num )) )
		return;
	if ( !G_ClientAdminCmdAllowedOn( ent, target, qtrue, qfalse ) )
		return;

	trap->SendServerCommand( ent-g_entities, va( "print \"%s : %s\n\"", target->client->pers.netname, vtos( target->client->ps.origin ) ) );
}

void Cmd_refuseTele_f( gentity_t *ent ) {
	jpClient_t *jp = &ent->client->pers.jp;

	jp->refuseTele = !jp->refuseTele;
	trap->SendServerCommand( ent-g_entities, jp->refuseTele ? "print \"You refuse to be teleported by admins.\n\"" : "print \"You accept to be teleported by admins.\n\"" );
}
