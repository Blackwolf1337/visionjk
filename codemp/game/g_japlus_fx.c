/*
JA+ admin effects: sleep, ghost, empower, merc, mindtrick, protect, silence, slap,
forced alternate dimension and denyvote.
Rebuilt from jampgamei386.so (JA+ v2.4 Build 7).
*/

#include "g_local.h"
#include "g_japlus.h"

static gentity_t *JP_ValidClient( int num ) {
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
effect functions
=================
*/
void G_GoInSleep( gentity_t *ent, qboolean on ) {
	playerState_t *ps = &ent->client->ps;

	if ( on ) {
		ps->forceHandExtend = HANDEXTEND_KNOCKDOWN;
		ps->forceHandExtendTime = level.time + 1200;
		ps->forceDodgeAnim = 0;
		ent->client->pers.jp.sleeping = qtrue;
		return;
	}
	ps->forceDodgeAnim = 1;
	ent->client->pers.jp.sleeping = qfalse;
}

void G_GoInProtect( gentity_t *ent, qboolean on ) {
	ent->client->pers.jp.protectedByAdmin = on;
	if ( on )
		ent->client->ps.eFlags |= EF_INVULNERABLE;
	else
		ent->client->ps.eFlags &= ~EF_INVULNERABLE;
}

// a protected player who kicks or grabs loses the protection (the other attacks: where the base game ends the
// spawn protection, see dangerTime)
void JP_AttackEndsProtect( gentity_t *ent ) {
	if ( !ent || !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED || !ent->client->pers.jp.protectedByAdmin )
		return;
	ent->client->dangerTime = level.time;
	ent->client->ps.eFlags &= ~EF_INVULNERABLE;
	ent->client->invulnerableTimer = 0;
	ent->client->pers.jp.protectedByAdmin = qfalse;
}

void G_GoInEmpower( gentity_t *ent, qboolean on ) {
	playerState_t *ps = &ent->client->ps;
	int i;

	if ( on ) {
		ent->client->pers.jp.flame = qfalse;
		ent->client->pers.jp.protectedByAdmin = qfalse;
		ent->client->pers.jp.empowered = qtrue;
		ent->client->pers.jp.forcesChanged = qtrue;
		ps->eFlags |= EF_BODYPUSH;	// rww: 0x80000, the glowing "empowered" body effect
		for ( i = 0; i < NUM_FORCE_POWERS; i++ ) {
			ps->fd.forcePowerLevel[i] = FORCE_LEVEL_3;
			ps->fd.forcePowersKnown |= (1 << i);
		}
		return;
	}
	ent->client->pers.jp.empowered = qfalse;
	ent->client->pers.jp.forcesChanged = qtrue;
	ps->eFlags &= ~EF_BODYPUSH;
	WP_InitForcePowers( ent );
}

// flag for the client plugin: the player has no saber
void JP_UpdateWeaponSwitchFlag( gclient_t *client ) {
	if ( client->ps.stats[STAT_WEAPONS] & (1 << WP_SABER) )
		client->ps.eFlags &= ~EF_NOT_USED_1;
	else
		client->ps.eFlags |= EF_NOT_USED_1;
}

#define JP_MERC_ITEMS	((1 << HI_SEEKER) | (1 << HI_SHIELD) | (1 << HI_BINOCULARS) | (1 << HI_SENTRY_GUN) | (1 << HI_EWEB))

// what a merc has: all weapons but the saber, items, ammo, and of the force only jump 2 and lightning 3
// (the lightning is his flamethrower)
void JP_MercLoadout( gentity_t *ent ) {
	playerState_t *ps = &ent->client->ps;
	int i;

	ps->stats[STAT_WEAPONS] = (1 << (LAST_USEABLE_WEAPON + 1)) - 1;	// 0x1ffff
	ps->stats[STAT_WEAPONS] &= ~(1 << WP_SABER);
	ps->weapon = WP_MELEE;
	ps->stats[STAT_HOLDABLE_ITEMS] |= JP_MERC_ITEMS;
	for ( i = 0; i < MAX_AMMO; i++ )
		ps->ammo[i] = 999;
	for ( i = 0; i < NUM_FORCE_POWERS; i++ ) {
		ps->fd.forcePowerLevel[i] = FORCE_LEVEL_0;
		ps->fd.forcePowersKnown &= ~(1 << i);
	}
	ps->fd.forcePowerLevel[FP_LEVITATION] = FORCE_LEVEL_2;
	ps->fd.forcePowersKnown |= (1 << FP_LEVITATION);
	ps->fd.forcePowerLevel[FP_LIGHTNING] = FORCE_LEVEL_3;
	ps->fd.forcePowersKnown |= (1 << FP_LIGHTNING);
}

// merc: no force but force jump and the flamethrower, all weapons and a lot of ammo
void G_GoInMerc( gentity_t *ent, qboolean on ) {
	playerState_t *ps = &ent->client->ps;
	int i;

	if ( on ) {
		ent->client->pers.jp.merc = qtrue;
		ent->client->pers.jp.flame = qtrue;
		JP_MercLoadout( ent );
		JP_UpdateWeaponSwitchFlag( ent->client );
		return;
	}

	ent->client->pers.jp.merc = qfalse;
	ent->client->pers.jp.flame = qfalse;
	for ( i = 0; i <= LAST_USEABLE_WEAPON; i++ ) {
		if ( !g_weaponDisable.integer || !((1 << i) & g_weaponDisable.integer) )
			ps->stats[STAT_WEAPONS] |= (1 << i);
		else
			ps->stats[STAT_WEAPONS] &= ~(1 << i);
	}
	ps->stats[STAT_HOLDABLE_ITEMS] &= ~JP_MERC_ITEMS;
	for ( i = 0; i < MAX_AMMO; i++ )
		ps->ammo[i] = 0;

	if ( !(g_weaponDisable.integer & (1 << WP_SABER)) ) {
		ps->weapon = WP_SABER;
		if ( jp_giveWP_MELEE.integer == 1 )
			ps->stats[STAT_WEAPONS] |= (1 << WP_MELEE);
	}
	else {
		ps->stats[STAT_WEAPONS] |= (1 << WP_MELEE);
		ps->weapon = WP_MELEE;
	}
	WP_InitForcePowers( ent );
	JP_UpdateWeaponSwitchFlag( ent->client );
}

// mindtricked players do not see the player
void G_GoInMindtrick( gentity_t *ent, qboolean on ) {
	int i;

	ent->client->pers.jp.mindtricked = on;
	if ( !on )
		return;

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *other = JP_ValidClient( i );

		if ( other && i != ent-g_entities )
			WP_AddAsMindtricked( &other->client->ps.fd, ent-g_entities );
	}
}

// ghosts are invulnerable, invisible for the players and can not be hit
void G_becomeGhost( gentity_t *ent ) {
	int i;

	if ( !ent || !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED )
		return;

	ent->client->pers.jp.isGhost = qtrue;
	ent->r.contents &= ~CONTENTS_BODY;

	// every player except admins who asked to see ghosts is "mindtricked" by the ghost
	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *other = JP_ValidClient( i );

		if ( !other || i == ent-g_entities )
			continue;
		if ( other->client->sess.jpAdminLevel < JPADM_COUNCIL || !other->client->pers.jp.canSeeGhosts )
			WP_AddAsMindtricked( &ent->client->ps.fd, i );
	}

	ent->flags |= FL_GODMODE;
	// a ghost has no fists
	if ( ent->client->ps.weapon == WP_MELEE )
		ent->client->ps.weapon = WP_NONE;
	ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_MELEE);
}

void G_RemoveGhost( gentity_t *ent ) {
	playerState_t *ps = &ent->client->ps;

	ent->client->pers.jp.isGhost = qfalse;
	ent->r.contents |= CONTENTS_BODY;
	ps->fd.forceMindtrickTargetIndex = 0;
	ps->fd.forceMindtrickTargetIndex2 = 0;
	ps->fd.forceMindtrickTargetIndex3 = 0;
	ps->fd.forceMindtrickTargetIndex4 = 0;
	if ( jp_giveWP_MELEE.integer == 1 )
		ps->stats[STAT_WEAPONS] |= (1 << WP_MELEE);
}

// takes away the god mode the admin effects use as long as no effect needs it
void G_ADMRemoveGod( gentity_t *ent ) {
	const jpClient_t *jp = &ent->client->pers.jp;

	if ( !jp->sleeping && !jp->down && !jp->mindtricked && !jp->isGhost && (ent->flags & FL_GODMODE) )
		ent->flags &= ~FL_GODMODE;
}

// mode: 0 primary dimension, 1 alternate dimension, 2 forced to stay in the alternate dimension
/*
=================
amAltDim: player command, toggles the alternate dimension (FFA only)
=================
*/
void Cmd_amAltDim_f( gentity_t *ent ) {
	gclient_t *client = ent->client;
	jpClient_t *jp = &client->pers.jp;
	playerState_t *ps = &client->ps;
	const int num = ent-g_entities;

	if ( !client || client->pers.connected != CON_CONNECTED )
		return;
	if ( g_gametype.integer != GT_FFA || g_jediVmerc.integer ) {
		trap->SendServerCommand( num, "print \"this cmd is not allowed in this gametype!.\n\"" );
		return;
	}
	if ( jp_altDim.integer == 0 ) {
		trap->SendServerCommand( num, "print \"Alternate dimension is not allowed!.\n\"" );
		return;
	}
	if ( jp->dimension == 2 ) {
		trap->SendServerCommand( num, "print \"^1You are forced to stay in the Alternate Dimension!.\n\"" );
		return;
	}

	if ( ps->duelInProgress || ps->pm_type == PM_FLOAT || ps->fd.forcePowersActive
		|| BG_InGrappleMove( ps->legsAnim ) || BG_InGrappleMove( ps->torsoAnim )
		|| BG_SaberInAttack( ps->saberMove ) || BG_SaberInTransitionAny( ps->saberMove )
		|| ps->pm_type == PM_DEAD || ent->health <= 0 || ps->stats[STAT_HEALTH] <= 0 || (ps->eFlags & EF_DEAD) )
		return;

	if ( client->sess.jpAdminLevel != JPADM_RCON && client->sess.sessionTeam != TEAM_SPECTATOR ) {
		int idle;

		if ( level.time < jp->altDimTimer ) {
			trap->SendServerCommand( num, va( "print \"^1Alternate Dimension Timer: you can't switch dimensions until ^5%is!\n\"", (jp->altDimTimer - level.time) / 1000 ) );
			return;
		}
		idle = level.time - client->inactivityTime;
		if ( idle < jp_altDimInactivityTimer.integer * 1000 ) {
			trap->SendServerCommand( num, va( "print \"^2Alternate Dimension Inactivity Timer: to change dimension, Stay inactive during: ^5%is!\n\"", jp_altDimInactivityTimer.integer - idle / 1000 ) );
			return;
		}
	}
	if ( G_TouchOtherPlayer( ent ) ) {
		trap->SendServerCommand( num, "print \"^1You can't change your dimension beeing inside another player!.\n\"" );
		return;
	}

	jp->altDimTimer = level.time + jp_altDimTimer.integer * 1000;
	if ( jp->dimension != 1 ) {
		trap->SendServerCommand( -1, va( "print \"%s^1 is going to the ^5%s!.\n\"", client->pers.netname, jp_altDimName.string ) );
		G_GoInAltDim( ent, 1 );
	} else {
		trap->SendServerCommand( -1, va( "print \"%s^2 is going to the ^5%s!.\n\"", client->pers.netname, jp_primDimName.string ) );
		G_GoInAltDim( ent, 0 );
	}
}

void G_GoInAltDim( gentity_t *ent, int mode ) {
	jpClient_t *jp = &ent->client->pers.jp;

	if ( mode != 1 && mode != 2 ) {
		ent->client->ps.eFlags &= ~EF_ALT_DIM;
		jp->dimension = 0;
		ent->flags &= ~FL_GODMODE;
		G_Sound( ent, CHAN_ITEM, G_SoundIndex( "sound/chars/shadowtrooper/decloak.wav" ) );
		return;
	}

	ent->client->ps.eFlags |= EF_ALT_DIM;
	jp->dimension = mode;

	// every admin effect is removed in the alternate dimension
	if ( jp->sleeping || jp->isGhost || jp->down || jp->mindtricked ) {
		jp->dimension = 2;
	}
	if ( jp->sleeping )		G_GoInSleep( ent, qfalse );
	if ( jp->isGhost )		G_RemoveGhost( ent );
	if ( jp->protectedByAdmin )	G_GoInProtect( ent, qfalse );
	if ( jp->empowered )	G_GoInEmpower( ent, qfalse );
	if ( jp->merc )			G_GoInMerc( ent, qfalse );
	if ( jp->mindtricked )	G_GoInMindtrick( ent, qfalse );
	ent->flags &= ~FL_GODMODE;

	if ( ent->client->jetPackOn ) {
		Jetpack_Off( ent );
		ent->client->jetPackToggleTime = level.time + Q_irand( 3000, 10000 );
	}
	ent->client->ps.stats[STAT_HOLDABLE_ITEMS] &= ~(1 << HI_JETPACK);	// jetpack item is taken away
	JP_HookFree( ent );
	G_Sound( ent, CHAN_ITEM, G_SoundIndex( "sound/chars/shadowtrooper/cloak.wav" ) );
}

/*
=================
G_TouchOtherPlayer

true if the player is inside the bounding box of another living player
=================
*/
qboolean G_TouchOtherPlayer( gentity_t *ent ) {
	vec3_t mins, maxs;
	int touch[MAX_GENTITIES], num, i;

	if ( !ent || !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED )
		return qfalse;
	if ( ent->client->ps.stats[STAT_HEALTH] < 1 )
		return qfalse;

	VectorSet( mins, ent->client->ps.origin[0] - 40.0f, ent->client->ps.origin[1] - 40.0f, ent->client->ps.origin[2] - 52.0f );
	VectorSet( maxs, ent->client->ps.origin[0] + 40.0f, ent->client->ps.origin[1] + 40.0f, ent->client->ps.origin[2] + 52.0f );
	num = trap->EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );

	VectorAdd( ent->client->ps.origin, ent->r.mins, mins );
	VectorAdd( ent->client->ps.origin, ent->r.maxs, maxs );

	for ( i = 0; i < num; i++ ) {
		gentity_t *hit = &g_entities[touch[i]];

		if ( hit == ent || !hit->inuse || hit->s.eType != ET_PLAYER || hit->s.number >= MAX_CLIENTS )
			continue;
		if ( !hit->client || hit->client->pers.connected != CON_CONNECTED || hit->client->ps.stats[STAT_HEALTH] < 1 )
			continue;
		if ( trap->EntityContact( mins, maxs, (sharedEntity_t *)hit, qfalse ) )
			return qtrue;
	}
	return qfalse;
}

/*
=================
G_AdminCSDisplay

center print for everybody: the target gets "targetMsg", all others "broadcastMsg" with the
name of the target in it
=================
*/
static void JP_FormatName( char *out, int outSize, const char *fmt, const char *name ) {
	const char *p = strstr( fmt, "%s" );

	if ( !p ) {
		Q_strncpyz( out, fmt, outSize );
		return;
	}
	Com_sprintf( out, outSize, "%.*s%s%s", (int)(p - fmt), fmt, name, p + 2 );
}

void G_AdminCSDisplay( int target, const char *targetMsg, const char *broadcastMsg ) {
	char msg[MAX_STRING_CHARS];
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		if ( !JP_ValidClient( i ) )
			continue;

		if ( i == target )
			Q_strncpyz( msg, targetMsg, sizeof( msg ) );
		else
			JP_FormatName( msg, sizeof( msg ), broadcastMsg, level.clients[target].pers.netname );
		JP_NewLines( msg );
		trap->SendServerCommand( i, va( "cp \"^7%s\n\"", msg ) );
	}
}

/*
=================
shared parts of the effect commands
=================
*/
typedef struct jpFxCall_s {
	char		cmd[MAX_TOKEN_CHARS];
	int			target;			// -1 for everybody
	int			first, last;	// range of the clients to work on
	qboolean	single;			// one player addressed
	qboolean	silent;			// rcon level admin: no broadcast message
} jpFxCall_t;

static void JP_LogAdminCmd( gentity_t *ent, qboolean rcon, const char *cmd ) {
	if ( !rcon && ent && ent->client->sess.jpAdminLevel != JPADM_RCON )
		G_LogPrintf( "ADMIN CMD EXECUTED : %s executed by %s\n", cmd, ent->client->pers.netname );
}

static void JP_Reply( gentity_t *ent, qboolean rcon, const char *text ) {
	if ( rcon )
		trap->Print( "%s", text );
	else
		trap->SendServerCommand( ent-g_entities, va( "print \"%s\"", text ) );
}

// qfalse when the command would end while it reads its arguments: no player given and not run by the admin script
static qboolean JP_FxHasTarget( qboolean rcon ) {
	char name[MAX_TOKEN_CHARS];

	trap->Argv( 0, name, sizeof( name ) );
	return (!rcon && (!Q_stricmp( name, "admCmdEXE" ) || strstr( name, "gun" ))) || trap->Argc() >= 2;
}

// tells the admin the command does not work in this gametype. The original reads the arguments first: a command
// that needs a player and has none ends without a message (the callers go on to JP_FxBegin, which ends it).
static qboolean JP_FxDisallowedInGametype( gentity_t *ent, qboolean rcon, const char *cmd, qboolean alsoHighlander, qboolean selfByDefault ) {
	char name[MAX_TOKEN_CHARS];

	trap->Argv( 0, name, sizeof( name ) );
	if ( !(!rcon && (!Q_stricmp( name, "admCmdEXE" ) || strstr( name, "gun" ))) && trap->Argc() < 2 && (!selfByDefault || rcon) )
		return qfalse;

	if ( g_gametype.integer == GT_FFA && !g_jediVmerc.integer && !(alsoHighlander && jp_highlander.integer == 1) )
		return qfalse;

	JP_Reply( ent, rcon, va( "^5%s ^7is disallowed in this gametype!\n", cmd ) );
	return qtrue;
}

// reads command name and target like the admin prologue, but a missing argument is "myself" if allowed
static qboolean JP_FxBegin( gentity_t *ent, qboolean rcon, jpFxCall_t *c, qboolean selfByDefault ) {
	char arg[MAX_TOKEN_CHARS];

	trap->Argv( 0, c->cmd, sizeof( c->cmd ) );

	if ( !rcon && (!Q_stricmp( c->cmd, "admCmdEXE" ) || strstr( c->cmd, "gun" )) ) {
		const int idx = ent->client->pers.jp.admCmdIndex;

		if ( idx < 0 || idx >= JP_NUM_ADMIN_CMDS_A )
			return qfalse;
		Q_strncpyz( c->cmd, jp_adminCmdsA[idx], sizeof( c->cmd ) );
		c->target = ent->client->pers.jp.admTargetNum;
	}
	else if ( trap->Argc() < 2 ) {
		if ( !selfByDefault || rcon )
			return qfalse;
		c->target = ent-g_entities;
	}
	else {
		trap->Argv( 1, arg, sizeof( arg ) );
		c->target = G_getClientNumberForString( arg );
	}

	c->single = c->target != -1;
	c->first = c->single ? c->target : 0;
	c->last = c->single ? c->target : level.maxclients - 1;
	c->silent = !rcon && ent->client->sess.jpAdminLevel == JPADM_RCON;
	return qtrue;
}

// the command works on a single player: checks target and permission, returns the target
static gentity_t *JP_FxSingleTarget( gentity_t *ent, qboolean rcon, jpFxCall_t *c, qboolean notify, qboolean checkAbuse ) {
	gentity_t *target;

	if ( !rcon && !G_ClientAdminCmdAllowed( ent, c->cmd, qtrue ) )
		return NULL;
	if ( !(target = JP_ValidClient( c->target )) )
		return NULL;
	if ( !rcon && !G_ClientAdminCmdAllowedOn( ent, target, notify, checkAbuse ) )
		return NULL;
	return target;
}

static void JP_FxAnnounce( const jpFxCall_t *c, const vmCvar_t *targetMsg, const vmCvar_t *broadcastMsg ) {
	G_AdminCSDisplay( c->target, targetMsg->string, c->silent ? "" : broadcastMsg->string );
}

static void JP_FxHintAltDim( gentity_t *target ) {
	if ( jp_altDim.integer )
		trap->SendServerCommand( target-g_entities, "print \"^2You can use the ^5/amAltDim ^2cmd to go in the alternate dimension^5 (no Rules Dimension)!\n\"" );
}

/*
=================
toggle commands working on one player: ghost, empower, merc, protect, mindtrick
=================
*/
typedef struct jpToggleFx_s {
	const char		*name;
	qboolean		*(*flag)( gclient_t *client );
	void			(*set)( gentity_t *ent, qboolean on );
	vmCvar_t		*msgTargetOn, *msgBroadcastOn, *msgTargetOff, *msgBroadcastOff;
	qboolean		refuseInAltDim;
	qboolean		selfByDefault;	// without a player name the command is for the admin himself
} jpToggleFx_t;

static qboolean *Flag_Empowered( gclient_t *c )		{ return &c->pers.jp.empowered; }
static qboolean *Flag_Merc( gclient_t *c )			{ return &c->pers.jp.merc; }
static qboolean *Flag_Protected( gclient_t *c )		{ return &c->pers.jp.protectedByAdmin; }
static qboolean *Flag_Mindtricked( gclient_t *c )	{ return &c->pers.jp.mindtricked; }

static void Set_Empower( gentity_t *ent, qboolean on )	{
	// empower, merc and protect exclude each other
	if ( on && ent->client->pers.jp.merc )
		G_GoInMerc( ent, qfalse );
	G_GoInEmpower( ent, on );
}
static void Set_Merc( gentity_t *ent, qboolean on )		{
	if ( on && ent->client->pers.jp.empowered )
		G_GoInEmpower( ent, qfalse );
	G_GoInMerc( ent, on );
}
static void Set_Protect( gentity_t *ent, qboolean on )	{
	if ( on && ent->client->pers.jp.empowered )
		G_GoInEmpower( ent, qfalse );
	G_GoInProtect( ent, on );
}
static void Set_Mindtrick( gentity_t *ent, qboolean on )	{
	G_GoInMindtrick( ent, on );
	// a mindtricked player can not be hurt
	if ( on )
		ent->flags |= FL_GODMODE;
	else
		G_ADMRemoveGod( ent );
}

static void JP_ToggleFxCmd( gentity_t *ent, qboolean rcon, const jpToggleFx_t *fx ) {
	jpFxCall_t c;
	gentity_t *target;
	qboolean on;

	if ( JP_FxDisallowedInGametype( ent, rcon, fx->name, qfalse, fx->selfByDefault ) )
		return;
	if ( !JP_FxBegin( ent, rcon, &c, fx->selfByDefault ) )
		return;
	if ( !(target = JP_FxSingleTarget( ent, rcon, &c, qtrue, qfalse )) )
		return;
	if ( !rcon && G_ClientCheckAdminAbuse( ent, qtrue ) )
		return;

	on = !*fx->flag( target->client );
	if ( on ) {
		if ( target->client->ps.eFlags & EF_ALT_DIM ) {
			JP_Reply( ent, rcon, "^1You cannot use this cmd on a player inside the Alternate dimension.!\n" );
			return;
		}
		if ( fx->refuseInAltDim )
			JP_FxHintAltDim( target );
	}

	fx->set( target, on );
	if ( on )
		JP_FxAnnounce( &c, fx->msgTargetOn, fx->msgBroadcastOn );
	else
		JP_FxAnnounce( &c, fx->msgTargetOff, fx->msgBroadcastOff );

	JP_LogAdminCmd( ent, rcon, c.cmd );
}

static const jpToggleFx_t jpFxEmpower = { "amempower", Flag_Empowered, Set_Empower, &jp_msgTargetEmpower, &jp_msgBroadcastEmpower, &jp_msgTargetUnempower, &jp_msgBroadcastUnempower, qfalse, qtrue };
static const jpToggleFx_t jpFxMerc = { "ammerc", Flag_Merc, Set_Merc, &jp_msgTargetMerc, &jp_msgBroadcastMerc, &jp_msgTargetUnmerc, &jp_msgBroadcastUnmerc, qfalse, qtrue };
static const jpToggleFx_t jpFxProtect = { "amprotect", Flag_Protected, Set_Protect, &jp_msgTargetProtect, &jp_msgBroadcastProtect, &jp_msgTargetUnprotect, &jp_msgBroadcastUnprotect, qfalse, qtrue };
static const jpToggleFx_t jpFxMindtrick = { "ammindtrick", Flag_Mindtricked, Set_Mindtrick, &jp_msgTargetMindtrick, &jp_msgBroadcastMindtrick, &jp_msgTargetUnmindtrick, &jp_msgBroadcastUnmindtrick, qtrue, qfalse };

void Cmd_amempower_f( gentity_t *ent, qboolean rcon )	{ JP_ToggleFxCmd( ent, rcon, &jpFxEmpower ); }
void Cmd_ammerc_f( gentity_t *ent, qboolean rcon )		{ JP_ToggleFxCmd( ent, rcon, &jpFxMerc ); }
void Cmd_amprotect_f( gentity_t *ent, qboolean rcon )	{ JP_ToggleFxCmd( ent, rcon, &jpFxProtect ); }
void Cmd_ammindtrick_f( gentity_t *ent, qboolean rcon )	{ JP_ToggleFxCmd( ent, rcon, &jpFxMindtrick ); }

/*
=================
amghost
=================
*/
void Cmd_amghost_f( gentity_t *ent, qboolean rcon ) {
	jpFxCall_t c;
	gentity_t *target;

	if ( JP_FxDisallowedInGametype( ent, rcon, "amghost", qtrue, qtrue ) )
		return;
	if ( !JP_FxBegin( ent, rcon, &c, qtrue ) )
		return;
	if ( !rcon && !G_ClientAdminCmdAllowed( ent, c.cmd, qtrue ) )
		return;
	if ( !(target = JP_ValidClient( c.target )) )
		return;
	if ( !rcon && !G_ClientAdminCmdAllowedOn( ent, target, qtrue, qtrue ) )
		return;

	if ( G_TouchOtherPlayer( target ) ) {
		JP_Reply( ent, rcon, "^1You can't ghost or unghost a player beeing inside another player!\n" );
		return;
	}

	if ( !target->client->pers.jp.isGhost ) {
		if ( target->client->ps.eFlags & EF_ALT_DIM ) {
			JP_Reply( ent, rcon, "^1You cannot use this cmd on a player inside the Alternate dimension.!\n" );
			return;
		}
		JP_FxHintAltDim( target );
		G_becomeGhost( target );
		JP_FxAnnounce( &c, &jp_msgTargetGhost, &jp_msgBroadcastGhost );
	}
	else {
		G_RemoveGhost( target );
		G_ADMRemoveGod( target );
		JP_FxAnnounce( &c, &jp_msgTargetUnghost, &jp_msgBroadcastUnghost );
	}

	JP_LogAdminCmd( ent, rcon, c.cmd );
}

/*
=================
amsleep, amwake, amsilence, amunsilence, amForceAltDim, amUnForceAltDim: one player or everybody
=================
*/
// apply() returns FX_SKIPPED, FX_APPLIED or FX_ABORT (stop the whole command)
enum { FX_ABORT = -1, FX_SKIPPED, FX_APPLIED };

static void JP_AllPlayersFx( gentity_t *ent, qboolean rcon, int (*apply)( gentity_t *target, const jpFxCall_t *c, gentity_t *admin, qboolean rconCall ),
	const vmCvar_t *msgTarget, const vmCvar_t *msgBroadcast, const vmCvar_t *msgAll, qboolean checkAbuseFirst ) {
	jpFxCall_t c;
	int i, result;

	if ( !JP_FxBegin( ent, rcon, &c, qfalse ) )
		return;
	if ( !rcon && !G_ClientAdminCmdAllowed( ent, c.cmd, qtrue ) )
		return;
	if ( c.target < -1 || c.target >= level.maxclients )
		return;
	if ( checkAbuseFirst && !rcon && G_ClientCheckAdminAbuse( ent, qtrue ) )
		return;

	for ( i = c.first; i <= c.last; i++ ) {
		gentity_t *target = JP_ValidClient( i );

		if ( !target )
			continue;
		if ( !rcon && !G_ClientAdminCmdAllowedOn( ent, target, qfalse, qfalse ) )
			continue;
		result = apply( target, &c, ent, rcon );
		if ( result == FX_ABORT )
			return;
		if ( result == FX_APPLIED && c.single )
			G_AdminCSDisplay( i, msgTarget->string, c.silent ? "" : msgBroadcast->string );
	}

	if ( !c.single ) {
		char msg[MAX_CVAR_VALUE_STRING];

		Q_strncpyz( msg, msgAll->string, sizeof( msg ) );
		JP_NewLines( msg );
		trap->SendServerCommand( -1, va( "cp \"^7%s\n\"", msg ) );
	}

	JP_LogAdminCmd( ent, rcon, c.cmd );
}

static int Apply_Sleep( gentity_t *t, const jpFxCall_t *c, gentity_t *admin, qboolean rcon ) {
	if ( t->client->ps.eFlags & EF_ALT_DIM ) {
		JP_Reply( admin, rcon, "^1You cannot use this cmd on a player inside the Alternate dimension.!\n" );
		return FX_ABORT;
	}
	JP_FxHintAltDim( t );
	G_GoInSleep( t, qtrue );
	t->flags |= FL_GODMODE;
	if ( t->client->pers.jp.down ) {
		t->client->pers.jp.down = qfalse;
		t->client->ps.forceGripChangeMovetype = 0;
		t->client->pers.jp.silenced = qfalse;
	}
	return FX_APPLIED;
}

static int Apply_Wake( gentity_t *t, const jpFxCall_t *c, gentity_t *admin, qboolean rcon ) {
	if ( !t->client->pers.jp.sleeping )
		return FX_SKIPPED;
	G_GoInSleep( t, qfalse );
	G_ADMRemoveGod( t );
	return FX_APPLIED;
}

static int Apply_Silence( gentity_t *t, const jpFxCall_t *c, gentity_t *admin, qboolean rcon ) {
	t->client->pers.jp.silenced = qtrue;
	return FX_APPLIED;
}

static int Apply_Unsilence( gentity_t *t, const jpFxCall_t *c, gentity_t *admin, qboolean rcon ) {
	t->client->pers.jp.silenced = qfalse;
	return FX_APPLIED;
}

static int Apply_ForceAltDim( gentity_t *t, const jpFxCall_t *c, gentity_t *admin, qboolean rcon ) {
	const playerState_t *ps = &t->client->ps;

	if ( t->client->pers.jp.dimension == 2 || ps->duelInProgress || ps->pm_type == PM_FLOAT
		|| BG_InGrappleMove( ps->legsAnim ) || BG_InGrappleMove( ps->torsoAnim ) )
		return FX_SKIPPED;
	G_GoInAltDim( t, 2 );
	return FX_APPLIED;
}

static int Apply_UnForceAltDim( gentity_t *t, const jpFxCall_t *c, gentity_t *admin, qboolean rcon ) {
	if ( t->client->pers.jp.dimension != 2 )
		return FX_SKIPPED;
	G_GoInAltDim( t, 1 );
	return FX_APPLIED;
}

void Cmd_amsleep_f( gentity_t *ent, qboolean rcon ) {
	if ( JP_FxDisallowedInGametype( ent, rcon, "amsleep", qfalse, qfalse ) )
		return;
	JP_AllPlayersFx( ent, rcon, Apply_Sleep, &jp_msgTargetSleep, &jp_msgBroadcastSleep, &jp_msgAllSleep, qtrue );
}

void Cmd_amwake_f( gentity_t *ent, qboolean rcon ) {
	// (no gametype rule in the original: a sleeping player can be woken up everywhere)
	JP_AllPlayersFx( ent, rcon, Apply_Wake, &jp_msgTargetWake, &jp_msgBroadcastWake, &jp_msgAllWake, qfalse );
}

void Cmd_amsilence_f( gentity_t *ent, qboolean rcon ) {
	JP_AllPlayersFx( ent, rcon, Apply_Silence, &jp_msgTargetSilence, &jp_msgBroadcastSilence, &jp_msgAllSilence, qfalse );
}

void Cmd_amunsilence_f( gentity_t *ent, qboolean rcon ) {
	JP_AllPlayersFx( ent, rcon, Apply_Unsilence, &jp_msgTargetUnsilence, &jp_msgBroadcastUnsilence, &jp_msgAllUnsilence, qfalse );
}

void Cmd_amForceAltDim_f( gentity_t *ent, qboolean rcon ) {
	if ( JP_FxDisallowedInGametype( ent, rcon, "amForceAltDim", qfalse, qfalse ) )
		return;
	if ( !jp_altDim.integer ) {
		if ( JP_FxHasTarget( rcon ) )	// (the texts of the original, the console one with its typo)
			JP_Reply( ent, rcon, rcon ? "^5Alternate dimension are is not allowed!\n" : "^5Alternate dimension is not allowed!\n" );
		return;
	}
	JP_AllPlayersFx( ent, rcon, Apply_ForceAltDim, &jp_msgTargetForceAltDim, &jp_msgBroadcastForceAltDim, &jp_msgAllForceAltDim, qfalse );
}

void Cmd_amUnForceAltDim_f( gentity_t *ent, qboolean rcon ) {
	if ( JP_FxDisallowedInGametype( ent, rcon, "amUnForceAltDim", qfalse, qfalse ) )
		return;
	if ( !jp_altDim.integer ) {
		if ( JP_FxHasTarget( rcon ) )
			JP_Reply( ent, rcon, "^5Alternate dimension is not allowed!\n" );
		return;
	}
	JP_AllPlayersFx( ent, rcon, Apply_UnForceAltDim, &jp_msgTargetUnForceAltDim, &jp_msgBroadcastUnForceAltDim, &jp_msgAllUnForceAltDim, qfalse );
}

/*
=================
amdenyvote: toggles the right to callvote
=================
*/
void Cmd_amdenyvote_f( gentity_t *ent, qboolean rcon ) {
	jpFxCall_t c;
	gentity_t *target;

	if ( !JP_FxBegin( ent, rcon, &c, qfalse ) )
		return;
	if ( !(target = JP_FxSingleTarget( ent, rcon, &c, qtrue, qfalse )) )
		return;

	target->client->pers.jp.denyVote = !target->client->pers.jp.denyVote;
	if ( target->client->pers.jp.denyVote )
		JP_FxAnnounce( &c, &jp_msgTargetDenyvote, &jp_msgBroadcastDenyvote );
	else
		JP_FxAnnounce( &c, &jp_msgTargetUndenyvote, &jp_msgBroadcastUndenyvote );

	JP_LogAdminCmd( ent, rcon, c.cmd );
}

/*
=================
amslap: throws the player in a random direction
=================
*/
void Cmd_amslap_f( gentity_t *ent, qboolean rcon ) {
	jpFxCall_t c;
	gentity_t *target;
	vec3_t dir, flat;
	float angle;
	double len;

	if ( JP_FxDisallowedInGametype( ent, rcon, "amslap", qfalse, qfalse ) )
		return;
	if ( !JP_FxBegin( ent, rcon, &c, qfalse ) )
		return;
	if ( !(target = JP_FxSingleTarget( ent, rcon, &c, qtrue, qtrue )) )
		return;

	if ( target->client->ps.eFlags & EF_ALT_DIM ) {
		JP_Reply( ent, rcon, "^1You cannot use this cmd on a player inside the Alternate dimension.!\n" );
		return;
	}

	// flood protection per target for everybody except the rcon level admin
	if ( (rcon || ent->client->sess.jpAdminLevel > JPADM_RCON) && level.time < target->client->pers.jp.slapTime ) {
		JP_Reply( ent, rcon, va( "^1Admin Flood Protect: you can't execute this cmd on this player during ^5%is!\n", (target->client->pers.jp.slapTime - level.time) / 1000 ) );
		return;
	}
	target->client->pers.jp.slapTime = level.time + 15000;
	JP_FxHintAltDim( target );

	angle = flrand( 0.0f, 2.0f * M_PI );
	flat[0] = cos( angle ) * 10.0f;
	flat[1] = sin( angle ) * 10.0f;
	flat[2] = 0.0f;
	VectorNormalize2( flat, dir );
	VectorNegate( dir, dir );
	// the original cuts the decimals off the length it gets from the x87 (a hair below or above 10): the
	// strength of the slap is 175 or 200
	len = sqrt( (double)flat[0] * flat[0] + (double)flat[1] * flat[1] );

	if ( target->health > 0 && target->client->ps.stats[STAT_HEALTH] > 0 ) {
		const int strength = (int)((int)len * 0.8) * 25;

		target->client->ps.forceHandExtend = HANDEXTEND_KNOCKDOWN;
		target->client->ps.forceHandExtendTime = level.time + 1000;
		target->client->ps.forceDodgeAnim = 0;
		target->client->ps.velocity[0] = dir[0] * strength;
		target->client->ps.velocity[1] = dir[1] * strength;
		target->client->ps.velocity[2] = 170.0f;
		G_Sound( target, CHAN_AUTO, G_SoundIndex( va( "sound/weapons/melee/punch%d", Q_irand( 1, 4 ) ) ) );
		JP_FxAnnounce( &c, &jp_msgTargetSlap, &jp_msgBroadcastSlap );
	}

	JP_LogAdminCmd( ent, rcon, c.cmd );
}
