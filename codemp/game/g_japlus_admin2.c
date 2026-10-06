/*
JA+ server management admin commands: ammap, amvstr, amrename, amlockteam, amforceteam, amkillvote.
Rebuilt from jampgamei386.so (JA+ v2.4 Build 7).
*/

#include "g_local.h"
#include "g_japlus.h"

extern stringID_table_t animTable[MAX_ANIMATIONS+1];

static void JP_Reply( gentity_t *ent, qboolean rcon, const char *text ) {
	if ( rcon )
		trap->Print( "%s", text );
	else
		trap->SendServerCommand( ent-g_entities, va( "print \"%s\"", text ) );
}

static void JP_LogCmd( gentity_t *ent, qboolean rcon, const char *cmd, const char *extra ) {
	if ( !rcon && ent && ent->client->sess.jpAdminLevel != JPADM_RCON )
		G_LogPrintf( "ADMIN CMD EXECUTED : %s%s%s executed by %s\n", cmd, extra[0] ? " " : "", extra, ent->client->pers.netname );
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
ammap <gametype> <map>: HL is the highlander mode of FFA
=================
*/
#define JP_AMMAP_GAMETYPES_CLIENT	"[^5HL = ^7HighLander] [^50 = ^7FFA] [^53 = ^7DUEL one on one tournament] [^54 = ^7POWER DUEL] [^56 = ^7TEAM DEATHMATCH] [^57 = ^7SIEGE] [^58 = ^7CTF]\n"
#define JP_AMMAP_GAMETYPES_CONSOLE	"[HL = HighLander] [0 = FFA] [3 = DUEL one on one tournament] [4 = POWER DUEL] [6 = TEAM DEATHMATCH] [7 = SIEGE] [8 = CTF]\n"

void Cmd_ammap_f( gentity_t *ent, qboolean rcon ) {
	char cmd[MAX_TOKEN_CHARS], gt[MAX_TOKEN_CHARS], map[MAX_TOKEN_CHARS], nextmap[MAX_CVAR_VALUE_STRING];
	qboolean highlander = qfalse;
	int gametype;

	if ( trap->Argc() < 3 ) {
		JP_Reply( ent, rcon, va( "Usage : ammap <gametype> <map_name>\n\t <gametype> \t%s", rcon ? JP_AMMAP_GAMETYPES_CONSOLE : JP_AMMAP_GAMETYPES_CLIENT ) );
		return;
	}

	trap->Argv( 0, cmd, sizeof( cmd ) );
	trap->Argv( 1, gt, sizeof( gt ) );
	trap->Argv( 2, map, sizeof( map ) );
	if ( !rcon && !G_ClientAdminCmdAllowed( ent, cmd, qtrue ) )
		return;

	if ( Q_strchrs( cmd, ";\r\n" ) || Q_strchrs( gt, ";\r\n" ) || Q_strchrs( map, ";\r\n" ) ) {
		JP_Reply( ent, rcon, "Invalid string.\n" );
		return;
	}

	if ( !Q_stricmp( gt, "HL" ) ) {
		gametype = GT_FFA;
		highlander = qtrue;
	}
	else {
		gametype = atoi( gt );
		if ( gametype < 0 || gametype > 8 || gametype == GT_HOLOCRON || gametype == GT_JEDIMASTER || gametype == GT_SINGLE_PLAYER ) {
			JP_Reply( ent, rcon, va( "^5Invalid GAMEType ^7==> valid are \n\t\t%s", rcon ? JP_AMMAP_GAMETYPES_CONSOLE : JP_AMMAP_GAMETYPES_CLIENT ) );
			return;
		}
	}

	JP_LogCmd( ent, rcon, cmd, "" );

	trap->SendConsoleCommand( EXEC_APPEND, va( "jp_highlander %i\n", highlander ? 1 : 0 ) );
	trap->SendConsoleCommand( EXEC_APPEND, va( "g_gametype %i\n", gametype ) );
	trap->Cvar_VariableStringBuffer( "nextmap", nextmap, sizeof( nextmap ) );
	if ( !nextmap[0] )
		trap->SendConsoleCommand( EXEC_APPEND, va( "map %s\n", map ) );
	else
		trap->SendConsoleCommand( EXEC_APPEND, va( "map %s ; set nextmap \"%s\"\n", map, nextmap ) );
}

/*
=================
amvstr <cvar>: executes the vstr of a cvar, not the passwords
=================
*/
void Cmd_amvstr_f( gentity_t *ent, qboolean rcon ) {
	char cmd[MAX_TOKEN_CHARS], name[MAX_TOKEN_CHARS];
	// cvars an admin may not vstr: passwords, and the session cvars, which hold the IP and admin level of every
	// player. g_webPassword and jp_clanPass were missing, session<n> is matched by its prefix.
	static const char *const secret[] = { "rconpassword", "sv_privatePassword", "jp_councilPass", "jp_knightPass",
		"jp_instructorPass", "jp_clanPass", "g_password", "g_webPassword" };
	int i;

	if ( trap->Argc() < 2 )
		return;
	trap->Argv( 0, cmd, sizeof( cmd ) );
	trap->Argv( 1, name, sizeof( name ) );
	if ( !rcon && !G_ClientAdminCmdAllowed( ent, cmd, qtrue ) )
		return;

	if ( Q_strchrs( cmd, ";\r\n" ) || Q_strchrs( name, ";\r\n" ) ) {
		JP_Reply( ent, rcon, "Invalid string.\n" );
		return;
	}
	if ( !Q_stricmpn( name, "session", 7 ) )
		return;
	for ( i = 0; i < (int)ARRAY_LEN( secret ); i++ ) {
		if ( !Q_stricmp( name, secret[i] ) )
			return;
	}

	JP_LogCmd( ent, rcon, cmd, name );
	trap->SendConsoleCommand( EXEC_APPEND, va( "vstr %s\n", name ) );
}

/*
=================
amrename <player> <new name>
=================
*/
void Cmd_amrename_f( gentity_t *ent, qboolean rcon ) {
	char cmd[MAX_TOKEN_CHARS], who[MAX_TOKEN_CHARS], newName[MAX_NETNAME], userinfo[MAX_INFO_STRING];
	gentity_t *target;

	if ( trap->Argc() < 3 )
		return;
	trap->Argv( 0, cmd, sizeof( cmd ) );
	trap->Argv( 1, who, sizeof( who ) );
	Q_strncpyz( newName, ConcatArgs( 2 ), sizeof( newName ) );

	if ( !rcon && !G_ClientAdminCmdAllowed( ent, cmd, qtrue ) )
		return;
	if ( !(target = JP_ValidPlayer( G_getClientNumberForString( who ) )) )
		return;
	if ( !rcon && !G_ClientAdminCmdAllowedOn( ent, target, qtrue, qfalse ) )
		return;

	trap->GetUserinfo( target-g_entities, userinfo, sizeof( userinfo ) );
	Info_SetValueForKey( userinfo, "name", newName );
	trap->SetUserinfo( target-g_entities, userinfo );
	ClientUserinfoChanged( target-g_entities );
	target->client->pers.netnameTime = level.time + 30000;

	JP_LogCmd( ent, rcon, cmd, "" );
}

/*
=================
team lock: amlockteam toggles a team in jp_teamLock, amforceteam moves players ignoring the locks
=================
*/
static const char *const jpTeamNames[] = { "spectator", "free", "blue", "red" };	// bit n+1 of jp_teamLock

static qboolean jpForcingTeam;

#define JP_TEAMLOCK_USAGE	"usage ^7==> amlockteam <team>\n\t<team> = s (spectator), f (free), b (blue), r (red)\n"

void Cmd_amlockteam_f( gentity_t *ent, qboolean rcon ) {
	char cmd[MAX_TOKEN_CHARS], team[MAX_TOKEN_CHARS];
	int i;

	if ( trap->Argc() < 2 ) {
		JP_Reply( ent, rcon, JP_TEAMLOCK_USAGE );
		return;
	}
	trap->Argv( 0, cmd, sizeof( cmd ) );
	trap->Argv( 1, team, sizeof( team ) );
	if ( !rcon && !G_ClientAdminCmdAllowed( ent, cmd, qtrue ) )
		return;

	for ( i = 0; i < (int)ARRAY_LEN( jpTeamNames ); i++ ) {
		const int bit = i + 1;

		if ( team[0] != jpTeamNames[i][0] )
			continue;

		if ( G_CmdBitAllowed( jp_teamLock.integer, bit ) ) {
			trap->SendConsoleCommand( EXEC_APPEND, va( "jp_teamLock %i\n", jp_teamLock.integer & ~(1 << bit) ) );
			trap->SendServerCommand( -1, va( "print \"^5%s ^7team ^6Unlocked!\n\"", jpTeamNames[i] ) );
		}
		else {
			trap->SendConsoleCommand( EXEC_APPEND, va( "jp_teamLock %i\n", jp_teamLock.integer | (1 << bit) ) );
			trap->SendServerCommand( -1, va( "print \"^5%s ^7team ^6locked!\n\"", jpTeamNames[i] ) );
		}
		JP_LogCmd( ent, rcon, cmd, "" );
		return;
	}

	JP_Reply( ent, rcon, JP_TEAMLOCK_USAGE );
}

// SetTeam past the team locks and the team balance, for amforceteam and the web admin
void JP_ForceTeam( gentity_t *ent, char *team ) {
	jpForcingTeam = qtrue;
	SetTeam( ent, team );
	jpForcingTeam = qfalse;
}

void Cmd_amforceteam_f( gentity_t *ent, qboolean rcon ) {
	char cmd[MAX_TOKEN_CHARS], who[MAX_TOKEN_CHARS], team[MAX_TOKEN_CHARS];
	int num, first, last, i;

	if ( trap->Argc() < 3 ) {
		JP_Reply( ent, rcon, "usage ^7==> amforceteam <ClientNUM> <team>\n\t<ClientNUM> can be -1 for all players\n\t<team> = s (spectator), f (free), b (blue), r (red)\n" );
		return;
	}
	trap->Argv( 0, cmd, sizeof( cmd ) );
	trap->Argv( 1, who, sizeof( who ) );
	trap->Argv( 2, team, sizeof( team ) );
	num = G_getClientNumberForString( who );

	if ( !rcon && !G_ClientAdminCmdAllowed( ent, cmd, qtrue ) )
		return;
	if ( num < -1 || num >= level.maxclients ) {
		JP_Reply( ent, rcon, "usage ^7==> amforceteam <ClientNUM> <team>\n\t<ClientNUM> can be -1 for all players\n\t<team> = s (spectator), f (free), b (blue), r (red)\n" );
		return;
	}

	first = num == -1 ? 0 : num;
	last = num == -1 ? level.maxclients - 1 : num;

	for ( i = first; i <= last; i++ ) {
		gentity_t *target = JP_ValidPlayer( i );

		if ( target && (rcon || G_ClientAdminCmdAllowedOn( ent, target, qfalse, qfalse )) )
			JP_ForceTeam( target, team );
	}

	JP_LogCmd( ent, rcon, cmd, "" );
}

/*
JP_TeamLockRefused

called from SetTeam with the team the player asked for. May change *team (auto join respects the
locks). Returns qtrue if the player was told he can not join.
*/
qboolean JP_TeamLockRefused( gentity_t *ent, team_t *team, qboolean autoPick ) {
	const int clientNum = ent-g_entities;
	const qboolean blueLocked = G_CmdBitAllowed( jp_teamLock.integer, 3 ), redLocked = G_CmdBitAllowed( jp_teamLock.integer, 4 );

	if ( jpForcingTeam )
		return qfalse;

	if ( *team == TEAM_SPECTATOR ) {
		if ( level.gametype != GT_DUEL && level.gametype != GT_POWERDUEL && G_CmdBitAllowed( jp_teamLock.integer, 1 ) ) {
			trap->SendServerCommand( clientNum, "print \"^7Sorry, ^5Spectator team^7 is locked!\n\"" );
			return qtrue;
		}
		return qfalse;
	}

	if ( level.gametype < GT_TEAM ) {
		if ( level.gametype != GT_DUEL && level.gametype != GT_POWERDUEL && G_CmdBitAllowed( jp_teamLock.integer, 2 ) ) {
			trap->SendServerCommand( clientNum, "print \"^7Sorry, ^5You can't join^7 the game!\n\"" );
			return qtrue;
		}
		return qfalse;
	}

	if ( autoPick ) {
		if ( blueLocked && redLocked ) {
			trap->SendServerCommand( clientNum, "print \"^7Sorry, ^5All teams^7 are locked!\n\"" );
			return qtrue;
		}
		if ( blueLocked )		*team = TEAM_RED;
		else if ( redLocked )	*team = TEAM_BLUE;
	}

	if ( redLocked && *team == TEAM_RED ) {
		trap->SendServerCommand( clientNum, "print \"^7Sorry, the ^1Red team^7 is locked!\n\"" );
		return qtrue;
	}
	if ( blueLocked && *team == TEAM_BLUE ) {
		trap->SendServerCommand( clientNum, "print \"^7Sorry, the ^4Blue team^7 is locked!\n\"" );
		return qtrue;
	}
	return qfalse;
}

// the team balance check does not apply if a team is locked or the admin forces the team
qboolean JP_TeamBalanceApplies( void ) {
	return !jpForcingTeam && !G_CmdBitAllowed( jp_teamLock.integer, 3 ) && !G_CmdBitAllowed( jp_teamLock.integer, 4 );
}

/*
=================
amkillvote
=================
*/
void Cmd_amkillvote_f( gentity_t *ent, qboolean rcon ) {
	char cmd[MAX_TOKEN_CHARS];

	trap->Argv( 0, cmd, sizeof( cmd ) );
	if ( !rcon && !G_ClientAdminCmdAllowed( ent, cmd, qtrue ) )
		return;
	if ( !level.voteTime && level.voteExecuteTime < level.time )
		return;

	// the vote runs out in the next frame (CheckVote), without a vote string nothing is executed
	level.voteString[0] = '\0';
	level.voteStringClean[0] = '\0';
	level.voteTime = level.time - 32000;
	trap->SendServerCommand( -1, "print \"^1Vote has been killed!\n\"" );

	JP_LogCmd( ent, rcon, cmd, "" );
}

/*
=================
ampoll <text>: a vote without effect, the players answer yes or no
=================
*/
void Cmd_ampoll_f( gentity_t *ent, qboolean rcon ) {
	char cmd[MAX_TOKEN_CHARS], text[150];
	const int clientNum = ent-g_entities;
	int i;

	if ( trap->Argc() < 2 )
		return;
	trap->Argv( 0, cmd, sizeof( cmd ) );
	Q_strncpyz( text, ConcatArgs( 1 ), sizeof( text ) );

	if ( !g_allowVote.integer ) {
		trap->SendServerCommand( clientNum, va( "print \"%s\n\"", G_GetStringEdString( "MP_SVGAME", "NOVOTE" ) ) );
		return;
	}
	if ( level.voteTime ) {
		trap->SendServerCommand( clientNum, va( "print \"%s\n\"", G_GetStringEdString( "MP_SVGAME", "VOTEINPROGRESS" ) ) );
		return;
	}
	if ( level.gametype != GT_DUEL && level.gametype != GT_POWERDUEL && ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
		trap->SendServerCommand( clientNum, va( "print \"%s\n\"", G_GetStringEdString( "MP_SVGAME", "NOSPECVOTE" ) ) );
		return;
	}
	if ( strchr( cmd, ';' ) || strchr( text, ';' ) ) {
		trap->SendServerCommand( clientNum, "print \"Invalid vote string.\n\"" );
		return;
	}
	if ( G_CmdBitAllowed( jp_votesDisable.integer, 10 ) ) {
		trap->SendServerCommand( clientNum, va( "print \"^1Vote %s ^7: This vote type is not allowed.\n\"", cmd ) );
		return;
	}
	if ( !G_ClientAdminCmdAllowed( ent, cmd, qtrue ) )
		return;

	level.voteString[0] = '\0';
	Com_sprintf( level.voteStringClean, sizeof( level.voteStringClean ), "poll %s", ent->client->pers.netname );
	Q_strncpyz( level.voteDisplayString, level.voteStringClean, sizeof( level.voteDisplayString ) );

	JP_NewLines( text );
	trap->SendServerCommand( -1, va( "print \"^3-------------------------------------------\n%s made a ^5POLL :\n %s\n^3-------------------------------------------\n\"", ent->client->pers.netname, text ) );
	trap->SendServerCommand( -1, va( "print \"%s^7 %s\n\"", ent->client->pers.netname, G_GetStringEdString( "MP_SVGAME", "PLCALLEDVOTE" ) ) );

	level.voteTime = level.time;
	level.voteYes = 0;
	level.voteNo = 0;
	for ( i = 0; i < level.maxclients; i++ ) {
		level.clients[i].mGameFlags &= ~PSG_VOTED;
		level.clients[i].pers.vote = 0;
	}
	JP_ShowMessage( va( "%s made a ^5POLL:\n %s", ent->client->pers.netname, text ), 30, qfalse );

	trap->SetConfigstring( CS_VOTE_TIME,	va( "%i", level.voteTime ) );
	trap->SetConfigstring( CS_VOTE_STRING,	level.voteDisplayString );
	trap->SetConfigstring( CS_VOTE_YES,		va( "%i", level.voteYes ) );
	trap->SetConfigstring( CS_VOTE_NO,		va( "%i", level.voteNo ) );
}

/*
=================
amweather rain|snow|none
=================
*/
void Cmd_amweather_f( gentity_t *ent, qboolean rcon ) {
	static qboolean weatherSet;
	char cmd[MAX_TOKEN_CHARS], arg[MAX_TOKEN_CHARS];

	trap->Argv( 0, cmd, sizeof( cmd ) );
	if ( !G_ClientAdminCmdAllowed( ent, cmd, qfalse ) ) {
		trap->SendServerCommand( ent-g_entities, va( "print \"unknown cmd %s\n\"", cmd ) );
		return;
	}

	if ( trap->Argc() < 2 ) {
		trap->SendServerCommand( ent-g_entities, "print \"Usage: ^2amweather ^7[rain or snow or none].\n\"" );
		return;
	}
	trap->Argv( 1, arg, sizeof( arg ) );

	if ( !weatherSet && !Q_stricmp( arg, "snow" ) ) {
		G_EffectIndex( "*snow" );
		G_EffectIndex( "*fog" );
		G_EffectIndex( "*constantwind (100 100 -100)" );
		weatherSet = qtrue;
	}
	else if ( !weatherSet && !Q_stricmp( arg, "rain" ) ) {
		G_EffectIndex( "*rain init 500" );
		weatherSet = qtrue;
	}
	else if ( weatherSet && !Q_stricmp( arg, "none" ) )
		G_EffectIndex( "*die" );
	else if ( weatherSet && (!Q_stricmp( arg, "snow" ) || !Q_stricmp( arg, "rain" )) )
		trap->SendServerCommand( ent-g_entities, "print \"^1Weather has already been set !!\n\"" );
	else
		trap->SendServerCommand( ent-g_entities, "print \"Usage: ^2amweather ^7[rain or snow or none].\n\"" );
}

/*
=================
amplayefx <effect> and amtest [anim]: rcon level admins only, for testing
=================
*/
void Cmd_amplayefx_f( gentity_t *ent, qboolean rcon ) {
	char arg[MAX_TOKEN_CHARS];
	mdxaBone_t matrix;
	vec3_t angles, origin;

	if ( ent->client->sess.jpAdminLevel != JPADM_RCON ) {
		trap->Argv( 0, arg, sizeof( arg ) );
		trap->SendServerCommand( ent-g_entities, va( "print \"unknown cmd %s\n\"", arg ) );
		return;
	}
	if ( trap->Argc() < 2 )
		return;
	trap->Argv( 1, arg, sizeof( arg ) );

	VectorCopy( ent->client->ps.viewangles, angles );
	trap->G2API_GetBoltMatrix( ent->ghoul2, 0, ent->client->renderInfo.handLBolt, &matrix, ent->client->ps.viewangles, ent->client->ps.origin, level.time, NULL, ent->modelScale );
	BG_GiveMeVectorFromMatrix( &matrix, ORIGIN, origin );
	G_PlayEffectID( G_EffectIndex( arg ), origin, angles );
}

void Cmd_amtest_f( gentity_t *ent, qboolean rcon ) {
	char arg[MAX_TOKEN_CHARS];
	int *anim = &ent->client->pers.jp.testAnim;

	if ( ent->client->sess.jpAdminLevel != JPADM_RCON )
		return;

	if ( trap->Argc() == 2 ) {
		trap->Argv( 1, arg, sizeof( arg ) );
		*anim = atoi( arg );
	}
	else if ( trap->Argc() == 3 )
		*anim = BOTH_KISSEE - 0x5d5 + 0x558;

	if ( *anim < 0 || *anim >= MAX_ANIMATIONS )
		*anim = 0;

	JP_EmoteExec( ent, *anim );
	trap->SendServerCommand( ent-g_entities, va( "print \"anim %i => %s\n\"", *anim, animTable[*anim].name ) );

	(*anim)++;
	if ( *anim > 0x624 )
		*anim = 0;
}
