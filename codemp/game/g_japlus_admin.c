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
JA+ admin system: login, ranks, command permissions, anti abuse, admin guns.
Rebuilt from jampgamei386.so (JA+ v2.4 Build 7).
*/

#include "g_local.h"
#include "g_japlus.h"

// admin command lists, the bit of a command in jp_*AllowedCMD is its position in A then B, starting at 1
const char *jp_adminCmdsA[JP_NUM_ADMIN_CMDS_A] = {
	"amkick", "amban", "amShowMotd", "amorigin", "amslap", "amsleep", "amwake", "amghost",
	"amdenyvote", "amprotect", "amsilence", "amunsilence", "amUnForceAltDim", "amForceAltDim",
	"amempower", "ammerc", "ammindtrick"
};

const char *jp_adminCmdsB[JP_NUM_ADMIN_CMDS_B] = {
	"amtele", "amstatus", "amwhois", "ammap", "ampsay", "npc", "amrename", "amlockteam",
	"amforceteam", "amkillvote", "amvstr", "amweather", "amlogout", "amtelemark", "ampoll",
	"amseeGhost", "admCmdPrev", "admCmdNext", "playerPrev", "playerNext", "admCmdEXE"
};

// commands every logged in admin may use whatever the allowed mask says
static const char *jp_alwaysAllowedCmds[] = {
	"amorigin", "admCmdPrev", "admCmdNext", "playerPrev", "playerNext", "admCmdEXE",
	"amlogout", "amtelemark", "ampoll", "amseeGhost"
};

#define JP_ADMGUN_RANGE		4096.0f		// admin gun trace length

/*
=================
JP_FormatName

The login messages are cvars holding a text with an optional %s for the player name.
Only the first %s is replaced, nothing else of the cvar is interpreted as format.
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
G_CmdBitAllowed

returns 1 if bit "bit" is set in mask
=================
*/
int G_CmdBitAllowed( int mask, int bit ) {
	if ( bit < 0 || bit > 31 )
		return 0;
	return (mask >> bit) & 1;
}

static int JP_AllowedMaskForLevel( int adminLevel ) {
	switch ( adminLevel ) {
	case JPADM_COUNCIL:		return jp_councilAllowedCMD.integer;
	case JPADM_INSTRUCTOR:	return jp_instructorAllowedCMD.integer;
	default:				return jp_knightAllowedCMD.integer;
	}
}

/*
=================
G_ClientAdminCmdAllowed

true if the admin rank of ent allows the command "cmd"
=================
*/
qboolean G_ClientAdminCmdAllowed( gentity_t *ent, const char *cmd, qboolean notify ) {
	int level_ = ent->client->sess.jpAdminLevel;
	qboolean allowed = qfalse;
	int i, bit = 1;

	if ( level_ == JPADM_RCON ) {
		allowed = qtrue;
	}
	else if ( level_ > JPADM_RCON ) {
		for ( i = 0; i < (int)ARRAY_LEN( jp_alwaysAllowedCmds ); i++ ) {
			if ( !Q_stricmp( cmd, jp_alwaysAllowedCmds[i] ) ) {
				allowed = qtrue;
				break;
			}
		}

		if ( !allowed ) {
			const int mask = JP_AllowedMaskForLevel( level_ );

			for ( i = 0; i < JP_NUM_ADMIN_CMDS_A; i++, bit++ ) {
				if ( !Q_stricmp( cmd, jp_adminCmdsA[i] ) ) {
					allowed = G_CmdBitAllowed( mask, bit ) == 1;
					goto done;
				}
			}
			for ( i = 0; i < JP_NUM_ADMIN_CMDS_B; i++, bit++ ) {
				if ( !Q_stricmp( cmd, jp_adminCmdsB[i] ) ) {
					allowed = G_CmdBitAllowed( mask, bit ) == 1;
					goto done;
				}
			}
		}
	}
done:
	if ( notify && !allowed )
		trap->SendServerCommand( ent-g_entities, "print \"^7 You are not allowed to execute this command! \n\"" );

	return allowed;
}

/*
=================
G_ClientCheckAdminAbuse

true if the admin used too many sensible admin commands in jp_antiAdmAbuseTimer seconds.
After jp_antiAdmAbuseNumberB4ban abuses the admin gets admin-banned.
=================
*/
qboolean G_ClientCheckAdminAbuse( gentity_t *ent, qboolean notify ) {
	jpClient_t *jp;
	qboolean abused = qfalse, countdown = qfalse;

	if ( !ent || !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED )
		return qtrue;

	jp = &ent->client->pers.jp;

	if ( !jp_antiAdmAbuseTimer.integer || ent->client->sess.jpAdminLevel == JPADM_RCON )
		return qfalse;

	if ( jp->abuseTimer <= level.time ) {
		jp->abuseTimer = level.time + jp_antiAdmAbuseTimer.integer * 1000;
		jp->abuseCmdsLeft = jp_antiAdmAbuseCmdsNumber.integer + 1;
	}

	jp->abuseCmdsLeft--;
	if ( jp->abuseCmdsLeft >= 1 )
		return qfalse;

	abused = qtrue;
	if ( jp->abuseCmdsLeft == 0 ) {
		if ( jp_antiAdmAbuseNumberB4ban.integer > 0 ) {
			countdown = qtrue;
			if ( jp->abuseBansLeft < 1 ) {
				const char *ip = ent->client->sess.IP;

				if ( ip[0] && !G_AdminFilterPacket( ip ) ) {
					ent->client->sess.jpAdminLevel = JPADM_NONE;
					G_AdminAddIP( (char *)ip );
					G_LogPrintf( "ADMIN ABUSE DETECTED ==> admBan on Admin : %s \n", ent->client->pers.netname );
					trap->SendServerCommand( ent-g_entities, "print \"You have been ^5admin-banned!\n\"" );
				}
				return qtrue;
			}
			jp->abuseBansLeft--;
		}
		G_LogPrintf( "ADMIN ABUSE DETECTED on Admin: %s \n", ent->client->pers.netname );
	}

	if ( notify ) {
		trap->SendServerCommand( ent-g_entities, va( "print \"^7Detected as ^1Admin Abuse^7 => Wait for ^5%is ^7to have your ^5%i ^7Number of sensible Usable cmds \n\"",
			(jp->abuseTimer - level.time) / 1000, jp_antiAdmAbuseCmdsNumber.integer ) );
		if ( countdown )
			trap->SendServerCommand( ent-g_entities, va( "print \"Again ^5%i Admin abuses ^7and you will be admbanned!\n\"", jp->abuseBansLeft ) );
	}

	return abused;
}

/*
=================
G_ClientAdminCmdAllowedOn

true if the admin may use a command on target (see jp_admUsableOn)
=================
*/
qboolean G_ClientAdminCmdAllowedOn( gentity_t *ent, gentity_t *target, qboolean notify, qboolean checkAbuse ) {
	qboolean allowed, notSelf = qtrue;
	int adminLvl, targetLvl;

	if ( !ent || !ent->client || !target || !target->client )
		return qfalse;
	if ( ent->client->pers.connected != CON_CONNECTED || target->client->pers.connected != CON_CONNECTED )
		return qfalse;

	adminLvl = ent->client->sess.jpAdminLevel;
	targetLvl = target->client->sess.jpAdminLevel;

	if ( ent == target ) {
		allowed = qtrue;
		notSelf = qfalse;
	}
	else if ( adminLvl == JPADM_NONE )
		allowed = qfalse;
	else if ( jp_admUsableOn.integer == -1 )
		allowed = qtrue;
	else if ( jp_admUsableOn.integer == -2 )
		allowed = targetLvl < JPADM_COUNCIL;
	else if ( targetLvl < JPADM_COUNCIL )
		allowed = qtrue;
	else if ( jp_admUsableOn.integer >= 0 )
		allowed = targetLvl >= jp_admUsableOn.integer + adminLvl;
	else
		allowed = qfalse;

	if ( notify && !allowed )
		trap->SendServerCommand( ent-g_entities, "print \"^7 You are not allowed to execute this command on this player! \n\"" );

	if ( notSelf && checkAbuse && allowed )
		allowed = !G_ClientCheckAdminAbuse( ent, qtrue );

	return allowed;
}

/*
=================
G_ClientNumberFromPartialName

first connected player whose name contains "name", colors and case ignored
=================
*/
static int G_ClientNumberFromPartialName( const char *name ) {
	char wanted[MAX_NETNAME], cur[MAX_NETNAME];
	int i;

	JP_SanitizeString( wanted, name, sizeof( wanted ), JP_NAME_CHARS );

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *ent = JP_ValidClient( i );

		if ( !ent )
			continue;
		JP_SanitizeString( cur, ent->client->pers.netname, sizeof( cur ), JP_NAME_CHARS );
		if ( cur[0] && strstr( cur, wanted ) )
			return i;
	}
	return -1;
}

/*
=================
G_getClientNumberForString

"-1" means everybody, a number is the client number, else a part of the name.
returns -2 when nobody matches
=================
*/
int G_getClientNumberForString( const char *s ) {
	int num;

	if ( !Q_stricmp( s, "-1" ) )
		return -1;
	if ( *s >= '0' && *s <= '9' )
		return atoi( s );

	num = G_ClientNumberFromPartialName( s );
	return num != -1 ? num : -2;
}

/*
=================
G_ClientUsingJAPLUSClient

version string of the client plugin, NULL if the player does not use it
=================
*/
const char *G_ClientUsingJAPLUSClient( int clientNum ) {
	static char version[MAX_INFO_VALUE];
	gentity_t *ent = JP_ValidClient( clientNum );
	char userinfo[MAX_INFO_STRING];
	const char *v;

	if ( !ent )
		return NULL;

	if ( ent->r.svFlags & SVF_BOT )
		return JP_PLUGIN_VERSION;

	trap->GetUserinfo( clientNum, userinfo, sizeof( userinfo ) );
	v = Info_ValueForKey( userinfo, "cjp_client" );
	if ( v[0] ) {
		Q_strncpyz( version, v, sizeof( version ) );
		return version;
	}
	return NULL;
}

/*
=================
G_seeGhost

lets an admin see (see == qtrue) or not see the ghosted players by treating them like mindtricked ones
=================
*/
void G_seeGhost( gentity_t *ent, qboolean see ) {
	int i;

	if ( !ent || !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED )
		return;

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *other = JP_ValidClient( i );

		if ( !other || !other->client->pers.jp.isGhost )
			continue;
		if ( !see ) {
			if ( i != ent-g_entities )
				WP_AddAsMindtricked( &other->client->ps.fd, ent-g_entities );
		}
		else
			WP_RemoveTrickedEnt( &other->client->ps.fd, ent-g_entities );
	}
}

/*
=================
G_amlogin

checks the password against the three admin passwords, returns the admin level
=================
*/
// not in JA+: slow down guessing the admin passwords. After JP_LOGIN_TRIES wrong tries a player has to wait,
// the wait doubles. Static arrays by client number, reset when the module is loaded again (every map).
#define JP_LOGIN_TRIES		3
#define JP_LOGIN_BASE_MS	30000
#define JP_LOGIN_MAX_MS		1800000

static int jpLoginFails[MAX_CLIENTS];
static int jpLoginUntil[MAX_CLIENTS];

int G_amlogin( gentity_t *ent, const char *password, qboolean notify ) {
	gclient_t *client = ent->client;
	char msg[MAX_STRING_CHARS];
	const char *loginMsg = NULL, *rank = NULL;
	const int clientNum = ent - g_entities;

	if ( G_AdminFilterPacket( client->sess.IP ) ) {
		client->sess.jpAdminLevel = JPADM_NONE;
		if ( notify )
			trap->SendServerCommand( ent-g_entities, "print \"^3 You are admin-banned ! \n\"" );
		return client->sess.jpAdminLevel;
	}

	if ( clientNum >= 0 && clientNum < MAX_CLIENTS && jpLoginUntil[clientNum] > level.time ) {
		if ( notify )
			trap->SendServerCommand( clientNum, "print \"^3 Too many wrong passwords, wait a moment. \n\"" );
		return client->sess.jpAdminLevel;
	}

	if ( jp_councilPass.string[0] && !strcmp( password, jp_councilPass.string ) ) {
		client->sess.jpAdminLevel = JPADM_COUNCIL;
		loginMsg = jp_councilLoginMSG.string;
		rank = "^3admin council";
	}
	else if ( jp_knightPass.string[0] && !strcmp( password, jp_knightPass.string ) ) {
		client->sess.jpAdminLevel = JPADM_KNIGHT;
		loginMsg = jp_knightLoginMSG.string;
		rank = "^5admin knight";
	}
	else if ( jp_instructorPass.string[0] && !strcmp( password, jp_instructorPass.string ) ) {
		client->sess.jpAdminLevel = JPADM_INSTRUCTOR;
		loginMsg = jp_instructorLoginMSG.string;
		rank = "^5admin instructor";
	}
	else {
		client->sess.jpAdminLevel = JPADM_NONE;
		if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
			jpLoginFails[clientNum]++;
			if ( jpLoginFails[clientNum] >= JP_LOGIN_TRIES ) {
				const int steps = jpLoginFails[clientNum] - JP_LOGIN_TRIES;
				int wait = JP_LOGIN_BASE_MS;

				if ( steps < 16 )
					wait <<= steps;
				if ( wait > JP_LOGIN_MAX_MS || wait <= 0 )
					wait = JP_LOGIN_MAX_MS;
				jpLoginUntil[clientNum] = level.time + wait;
			}
			G_SecurityLogPrintf( "amlogin: wrong password from %s [%s]\n", client->pers.netname, client->sess.IP );
		}
		if ( notify )
			trap->SendServerCommand( ent-g_entities, "print \"^3 Bad Password ! \n\"" );
		return client->sess.jpAdminLevel;
	}

	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		jpLoginFails[clientNum] = 0;
		jpLoginUntil[clientNum] = 0;
	}

	if ( notify ) {
		JP_FormatName( msg, sizeof( msg ), loginMsg, client->pers.netname );
		JP_NewLines( msg );
		trap->SendServerCommand( -1, va( "print \"^7%s\n\"", msg ) );
	}
	G_LogPrintf( "%s is logged as an %s.\n", client->pers.netname, rank );

	return client->sess.jpAdminLevel;
}

/*
=================
Cmd_amlogin_f
=================
*/
static void Cmd_amlogin_f( gentity_t *ent, qboolean rcon ) {
	char password[MAX_TOKEN_CHARS];
	gclient_t *client = ent->client;
	int i;

	if ( trap->Argc() < 2 )
		return;

	trap->Argv( 1, password, sizeof( password ) );
	if ( !Q_stricmp( password, "NOT SET" ) ) {
		trap->SendServerCommand( ent-g_entities, "print \"^3 Hey! try to hack me? :) ! \n\"" );
		return;
	}

	G_amlogin( ent, password, qtrue );

	if ( client->sess.jpAdminLevel <= JPADM_RCON )
		return;

	// login effect: electricity for the dark side, glow for the light side
	if ( jp_admLoginEffect.integer == 2 || (jp_admLoginEffect.integer > 0 && client->ps.fd.forceSide != FORCE_LIGHTSIDE) ) {
		client->ps.electrifyTime = level.time + 4000;
	}
	else if ( jp_admLoginEffect.integer == 1 ) {
		for ( i = 0; i < NUM_FORCE_POWERS; i++ )
			client->ps.fd.forcePowerBaseLevel[i] = client->ps.fd.forcePowerLevel[i];
		client->ps.powerups[PW_FORCE_ENLIGHTENED_LIGHT] = level.time + 4000;
		client->ps.fd.forceUsingAdded = qtrue;
	}

	if ( client->pers.jp.canSeeGhosts )
		G_seeGhost( ent, qtrue );
}

static void Cmd_amlogout_f( gentity_t *ent, qboolean rcon ) {
	if ( !G_ClientAdminCmdAllowed( ent, "amlogout", qtrue ) )
		return;

	ent->client->sess.jpAdminLevel = JPADM_NONE;
	trap->SendServerCommand( ent-g_entities, "print \"^7 You are not an admin any more! \n\"" );
	G_seeGhost( ent, qfalse );
}

/*
=================
admin command prologue shared by the commands working on a target

The target comes from the admin gun / admCmdEXE or from the first argument.
Returns qfalse if the command must not go on.
=================
*/
typedef struct jpAdminCall_s {
	char	cmd[MAX_TOKEN_CHARS];	// command name used for the permission check and the log
	int		target;					// client number, -1 for all, -2 for nobody
} jpAdminCall_t;

static qboolean JP_AdminCallBegin( gentity_t *ent, qboolean rcon, jpAdminCall_t *call ) {
	char arg[MAX_TOKEN_CHARS];

	trap->Argv( 0, call->cmd, sizeof( call->cmd ) );

	if ( !rcon && (!Q_stricmp( call->cmd, "admCmdEXE" ) || strstr( call->cmd, "gun" )) ) {
		const int idx = ent->client->pers.jp.admCmdIndex;

		if ( idx < 0 || idx >= JP_NUM_ADMIN_CMDS_A )
			return qfalse;
		Q_strncpyz( call->cmd, jp_adminCmdsA[idx], sizeof( call->cmd ) );
		call->target = ent->client->pers.jp.admTargetNum;
	}
	else {
		if ( trap->Argc() < 2 )
			return qfalse;
		trap->Argv( 1, arg, sizeof( arg ) );
		call->target = G_getClientNumberForString( arg );
	}

	if ( !rcon && !G_ClientAdminCmdAllowed( ent, call->cmd, qtrue ) )
		return qfalse;

	return qtrue;
}

static void JP_LogAdminCmd( gentity_t *ent, qboolean rcon, const char *cmd ) {
	if ( !rcon && ent && ent->client->sess.jpAdminLevel != JPADM_RCON )
		G_LogPrintf( "ADMIN CMD EXECUTED : %s executed by %s\n", cmd, ent->client->pers.netname );
}

static void Cmd_amkick_f( gentity_t *ent, qboolean rcon ) {
	jpAdminCall_t call;
	gentity_t *target;

	if ( !JP_AdminCallBegin( ent, rcon, &call ) )
		return;
	if ( !(target = JP_ValidClient( call.target )) )
		return;
	if ( !rcon ) {
		if ( !G_ClientAdminCmdAllowedOn( ent, target, qtrue, qtrue ) )
			return;
		JP_LogAdminCmd( ent, rcon, call.cmd );
	}
	trap->SendConsoleCommand( EXEC_APPEND, va( "clientkick %i\n", call.target ) );
}

static void Cmd_amban_f( gentity_t *ent, qboolean rcon ) {
	jpAdminCall_t call;
	gentity_t *target;
	const char *ip;

	if ( !JP_AdminCallBegin( ent, rcon, &call ) )
		return;
	if ( !(target = JP_ValidClient( call.target )) )
		return;
	if ( !rcon && !G_ClientAdminCmdAllowedOn( ent, target, qtrue, qtrue ) )
		return;

	ip = target->client->sess.IP;
	{
		// the stored IP is already the real one, this only keeps the console line clean
		char clean[NET_ADDRSTRMAXLEN];

		if ( ip[0] && G_CleanIP( ip, clean, sizeof( clean ) ) && !G_FilterPacket( clean ) )
			trap->SendConsoleCommand( EXEC_APPEND, va( "addip %s\n", clean ) );
	}

	JP_LogAdminCmd( ent, rcon, call.cmd );
	trap->SendConsoleCommand( EXEC_APPEND, va( "clientkick %i\n", call.target ) );
}

static void Cmd_amstatus_f( gentity_t *ent, qboolean rcon ) {
	char cmd[MAX_TOKEN_CHARS];
	int i;

	if ( trap->Argc() < 1 )
		return;
	trap->Argv( 0, cmd, sizeof( cmd ) );
	if ( !rcon && !G_ClientAdminCmdAllowed( ent, cmd, qtrue ) )
		return;

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *other = JP_ValidClient( i );
		const char *plugin;

		if ( !other )
			continue;
		plugin = G_ClientUsingJAPLUSClient( i );
		if ( !plugin || !plugin[0] )
			plugin = "^1No Plugin";

		if ( !rcon )
			trap->SendServerCommand( ent-g_entities, va( "print \"^5%i ^7---> %s  ^7Plugin:^2%s\n\"", i, other->client->pers.netname, plugin ) );
		else
			trap->Print( "^5%i ^7---> %s   ^7Plugin:^2%s\n", i, other->client->pers.netname, plugin );
	}
}

static void Cmd_amwhois_f( gentity_t *ent, qboolean rcon ) {
	static const struct { const char *title; } ranks[] = {
		{ NULL }, { NULL }, { "^3Admin Council" }, { "^5Admin Knight" }, { "^2Admin Instructor" }
	};
	char cmd[MAX_TOKEN_CHARS];
	int i;

	if ( trap->Argc() < 1 )
		return;
	trap->Argv( 0, cmd, sizeof( cmd ) );
	if ( !rcon && !G_ClientAdminCmdAllowed( ent, cmd, qtrue ) )
		return;

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *other = JP_ValidClient( i );
		const int lvl = other ? other->client->sess.jpAdminLevel : 0;
		const char *msg;

		if ( !other || lvl < JPADM_COUNCIL || lvl > JPADM_INSTRUCTOR )
			continue;

		msg = va( "print \"%s ^7: %s  \n\"", ranks[lvl].title, other->client->pers.netname );
		// a rcon level admin asking sees it alone, else everybody sees who the admins are
		if ( !rcon && ent->client->sess.jpAdminLevel == JPADM_RCON )
			trap->SendServerCommand( ent-g_entities, msg );
		else
			trap->SendServerCommand( -1, msg );
	}
}

static void JP_SetMotd( gclient_t *client ) {
	Q_strncpyz( client->pers.jp.motd, va( "^5%s\n\n^7%s\n", JP_VERSION, jp_motd.string ), sizeof( client->pers.jp.motd ) );
	JP_NewLines( client->pers.jp.motd );
	client->pers.jp.motdTime = jp_motdtime.integer;
}

static void Cmd_ammotd_f( gentity_t *ent, qboolean rcon ) {
	JP_SetMotd( ent->client );
}

static void Cmd_amShowMotd_f( gentity_t *ent, qboolean rcon ) {
	jpAdminCall_t call;
	int first, last, i;

	if ( !JP_AdminCallBegin( ent, rcon, &call ) )
		return;
	if ( call.target < -1 || call.target >= level.maxclients )
		return;

	first = call.target == -1 ? 0 : call.target;
	last = call.target == -1 ? level.maxclients - 1 : call.target;

	for ( i = first; i <= last; i++ ) {
		gentity_t *target = JP_ValidClient( i );

		if ( !target )
			continue;
		if ( rcon || G_ClientAdminCmdAllowedOn( ent, target, qtrue, qtrue ) )
			JP_SetMotd( target->client );
	}

	JP_LogAdminCmd( ent, rcon, call.cmd );
}

static void Cmd_amsay_f( gentity_t *ent, qboolean rcon ) {
	char text[150], name[64];
	int i;

	if ( ent->client->pers.jp.silenced )
		return;
	if ( trap->Argc() < 2 )
		return;

	Q_strncpyz( text, ConcatArgs( 1 ), sizeof( text ) );
	G_LogPrintf( "say_admin: %s: %s\n", ent->client->pers.netname, text );
	Com_sprintf( name, sizeof( name ), EC"(%s%c%c"EC")"EC": ", ent->client->pers.netname, Q_COLOR_ESCAPE, COLOR_WHITE );

	if ( jp_wordFilter.integer > 0 && (jp_wordFilter.integer & 0x20) && G_WordFilter( ent, text ) )
		return;
	WebAdmin_Chat( "admin", ent, NULL, text );

	// only admins and the sender see it
	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *other = JP_ValidClient( i );

		if ( !other || (i != ent-g_entities && other->client->sess.jpAdminLevel <= JPADM_NONE) )
			continue;
		trap->SendServerCommand( i, va( "chat \"^3<Admin>^7%s^3%s\"", name, text ) );
	}
}

static void Cmd_ampsay_f( gentity_t *ent, qboolean rcon ) {
	char cmd[MAX_TOKEN_CHARS], arg[MAX_TOKEN_CHARS], text[150];
	int target;

	if ( trap->Argc() < 2 )
		return;

	trap->Argv( 0, cmd, sizeof( cmd ) );
	if ( trap->Argc() == 2 ) {
		// "say_team <msg>" typed by an admin with the say_team override: send to everybody
		if ( Q_stricmp( cmd, "say_team" ) )
			return;
		Q_strncpyz( cmd, "ampsay", sizeof( cmd ) );
		target = -1;
		Q_strncpyz( text, ConcatArgs( 1 ), sizeof( text ) );
	}
	else {
		trap->Argv( 1, arg, sizeof( arg ) );
		target = atoi( arg );
		Q_strncpyz( text, ConcatArgs( 2 ), sizeof( text ) );
	}

	if ( !rcon && !G_ClientAdminCmdAllowed( ent, cmd, qtrue ) )
		return;
	if ( target < -1 || target >= level.maxclients )
		return;

	if ( target != -1 && !JP_ValidClient( target ) )
		return;

	JP_NewLines( text );
	trap->SendServerCommand( target, va( "cp \"^7%s\n\"", text ) );
	trap->SendServerCommand( target, va( "print \"^7%s\n\"", text ) );
}

/*
=================
admin gun and cycling through the admin commands / players
=================
*/
qboolean JP_AdminGun( gentity_t *ent, const char *cmd ) {
	jpClient_t *jp;
	trace_t tr;
	vec3_t start, end, fwd;
	int i;

	if ( !ent || !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED || ent->client->sess.jpAdminLevel == JPADM_NONE )
		return qfalse;

	for ( i = 0; i < JP_NUM_ADMIN_CMDS_A; i++ ) {
		if ( !Q_stricmp( cmd, jp_adminCmdsA[i] ) )
			break;
	}
	if ( i == JP_NUM_ADMIN_CMDS_A )
		return qfalse;

	jp = &ent->client->pers.jp;
	jp->admCmdIndex = i;

	VectorCopy( ent->client->ps.origin, start );
	start[2] += ent->client->ps.viewheight;
	AngleVectors( ent->client->ps.viewangles, fwd, NULL, NULL );
	VectorMA( start, JP_ADMGUN_RANGE, fwd, end );

	trap->Trace( &tr, start, NULL, NULL, end, ent-g_entities, 0x1111, qfalse, 0, 0 );
	if ( tr.fraction != 1.0f && tr.entityNum < MAX_CLIENTS && JP_ValidClient( tr.entityNum ) ) {
		jp->admTargetNum = tr.entityNum;
		return qtrue;
	}
	return qfalse;
}

static void JP_AdminShowSelection( gentity_t *ent ) {
	const int cmdIdx = ent->client->pers.jp.admCmdIndex, target = ent->client->pers.jp.admTargetNum;
	gentity_t *t = JP_ValidClient( target );

	trap->SendServerCommand( ent-g_entities, va( "print \"^5%s ^7-> ID ^5%i ^7: %s \n\"",
		jp_adminCmdsA[cmdIdx], target, t ? t->client->pers.netname : "^1No Player" ) );
}

static void Cmd_admCmdCycle_f( gentity_t *ent, qboolean next ) {
	jpClient_t *jp = &ent->client->pers.jp;
	const int step = next ? 1 : -1;
	int idx = jp->admCmdIndex, tries;

	if ( ent->client->sess.jpAdminLevel == JPADM_NONE )
		return;

	// next allowed command in the direction, wrapping around the list
	for ( tries = 0; tries < JP_NUM_ADMIN_CMDS_A; tries++ ) {
		idx += step;
		if ( idx < 0 )
			idx = JP_NUM_ADMIN_CMDS_A - 1;
		else if ( idx >= JP_NUM_ADMIN_CMDS_A )
			idx = 0;
		if ( G_ClientAdminCmdAllowed( ent, jp_adminCmdsA[idx], qfalse ) ) {
			jp->admCmdIndex = idx;
			JP_AdminShowSelection( ent );
			return;
		}
	}
}

static void Cmd_playerCycle_f( gentity_t *ent, qboolean next ) {
	jpClient_t *jp = &ent->client->pers.jp;
	const int step = next ? 1 : -1;
	int i, num;

	if ( ent->client->sess.jpAdminLevel == JPADM_NONE )
		return;

	num = jp->admTargetNum;
	for ( i = 0; i <= level.maxclients; i++ ) {
		num += step;
		if ( num < 0 )
			num = level.maxclients - 1;
		else if ( num >= level.maxclients )
			num = 0;
		if ( JP_ValidClient( num ) ) {
			jp->admTargetNum = num;
			if ( jp->admCmdIndex >= 0 && jp->admCmdIndex < JP_NUM_ADMIN_CMDS_A )
				JP_AdminShowSelection( ent );
			return;
		}
	}
}

static void Cmd_aminfo_f( gentity_t *ent, qboolean rcon ) {
	const int clientNum = ent-g_entities;
	const int adm = ent->client->sess.jpAdminLevel;
	int i;

	switch ( adm ) {
	case JPADM_RCON:
	case JPADM_COUNCIL:		trap->SendServerCommand( clientNum, "print \"^5Admin COUNCIL Commands \n\"" ); break;
	case JPADM_KNIGHT:		trap->SendServerCommand( clientNum, "print \"^5Admin KNIGHT Commands \n\"" ); break;
	case JPADM_INSTRUCTOR:	trap->SendServerCommand( clientNum, "print \"^5Admin INSTRUCTOR Commands \n\"" ); break;
	default: break;
	}

	if ( adm > JPADM_NONE ) {
		for ( i = 0; i < JP_NUM_ADMIN_CMDS_A; i++ ) {
			if ( G_ClientAdminCmdAllowed( ent, jp_adminCmdsA[i], qfalse ) )
				trap->SendServerCommand( clientNum, va( "print \"%s, \"", jp_adminCmdsA[i] ) );
		}
		for ( i = 0; i < JP_NUM_ADMIN_CMDS_B; i++ ) {
			if ( G_ClientAdminCmdAllowed( ent, jp_adminCmdsB[i], qfalse ) )
				trap->SendServerCommand( clientNum, va( "print \"%s, \"", jp_adminCmdsB[i] ) );
		}
	}

	trap->SendServerCommand( clientNum, "print \"\n\n^5Player Commands \n\"" );
	trap->SendServerCommand( clientNum, "print \"\t^3Total New MOVES LIST Display: ^7ammove\"" );
	trap->SendServerCommand( clientNum, "print \"\n\t^3Damages list: ^7amDMGs\"" );
	trap->SendServerCommand( clientNum, "print \"\n\t^3Speak Mod Cmd: ^7say_team_mod, clanpass, clanwhois, amsay, clansay, ignore, tell\"" );
	trap->SendServerCommand( clientNum, "print \"\n\t^3Vote Cmds: ^7callvote\"" );
	trap->SendServerCommand( clientNum, "print \"\n\t^3Action Cmds: ^7engage_FullForceDuel, saber, jetpack, amknockmedown, amdropsaber, toggleFlame, +button12 ^2(grapple hook)^7\"" );
	trap->SendServerCommand( clientNum, "print \"\n\t^3Emot Cmds: ^7taunt ,meditate, bow, gloat, flourish, amkneel, amdie, amdie2, amfinishinghim, ampower, amflip, amnod, amshake, amhiltThrow1, amhiltThrow2, amcomeon, amwon, amhello, amhug, sleep, amatease, amsurrender, amsit, amsit2, amsit3, amsit4, ambeg, amwait, amhips, amvictory, amneo, ambreakdance, ambreakdance2, amnoisy, amkiss, ampoop\"" );
	trap->SendServerCommand( clientNum, "print \"\n\t^3Other Cmds: ^7refuseTele, amAltDim, ammotd, amcolor, sabercolor, immortal, duelWhois\n\"" );
	// (no closing quote in the original either)
	trap->SendServerCommand( clientNum, "print \"\t^3Client PLugin Cmds: ^7pluginDisable, reloadReplier, showPlayerID\n" );
	trap->SendServerCommand( clientNum, va( "print \"\n^5---------------------\n^5%s by Slider\n---------------------\n\"", JP_VERSION ) );

	JP_DisplayLatestPluginVersion( ent );

	if ( d_saberSPStyleDamage.integer == 1 && jp_alterDMG.integer == 0 )
		trap->SendServerCommand( clientNum, "print \"^2Single Player Saber Damages (default in baseJKA) are ^5Activated.\n \"" );
	else
		trap->SendServerCommand( clientNum, "print \"^2Multiplayer Saber Damages are ^5Activated. (the new JA+ damage system looking like the JK2 one.)\n \"" );

	if ( jp_alterDMG.integer == 1 )
		trap->SendServerCommand( clientNum, "print \"^3In this server the saber damages are altered!\n\"" );

	if ( jp_altDim.integer && g_gametype.integer == GT_FFA && !g_jediVmerc.integer ) {
		if ( ent->client->ps.eFlags & EF_ALT_DIM )
			trap->SendServerCommand( clientNum, va( "print \"^1You are in the %s.\n\"", jp_altDimName.string ) );
		else
			trap->SendServerCommand( clientNum, va( "print \"^2You are in the %s.\n\"", jp_primDimName.string ) );
	}
}

/*
=================
server console commands: admGrant, admToggle, admAddIP, admRemoveIP, admListIP, admBan
=================
*/
static void Svcmd_admGrant_f( void ) {
	char arg[MAX_TOKEN_CHARS], levelName[MAX_TOKEN_CHARS];
	gentity_t *target;
	int num;

	if ( trap->Argc() < 3 )
		return;

	trap->Argv( 1, arg, sizeof( arg ) );
	trap->Argv( 2, levelName, sizeof( levelName ) );

	num = G_getClientNumberForString( arg );
	if ( !(target = JP_ValidClient( num )) )
		return;

	if ( !Q_stricmp( levelName, "council" ) ) {
		target->client->sess.jpAdminLevel = JPADM_COUNCIL;
		trap->Print( "^5Granted Council access to User ^7%s\n", target->client->pers.netname );
	}
	else if ( !Q_stricmp( levelName, "knight" ) ) {
		target->client->sess.jpAdminLevel = JPADM_KNIGHT;
		trap->Print( "^5Granted Knight access to User ^7%s\n", target->client->pers.netname );
	}
	else if ( !Q_stricmp( levelName, "instructor" ) ) {
		target->client->sess.jpAdminLevel = JPADM_INSTRUCTOR;
		trap->Print( "^5Granted Instructor access to User ^7%s\n", target->client->pers.netname );
	}
	else if ( !Q_stricmp( levelName, "no" ) ) {
		target->client->sess.jpAdminLevel = JPADM_NONE;
		trap->Print( "^5Remove Admin access to User ^7%s\n", target->client->pers.netname );
	}
	else
		trap->Print( "^5Usage:  admGrant <ID player> <level> where <level> = council, knight, instructor or no\n" );
}

static const char *JP_AdminCmdName( int bit ) {
	return bit <= JP_NUM_ADMIN_CMDS_A ? jp_adminCmdsA[bit - 1] : jp_adminCmdsB[bit - 1 - JP_NUM_ADMIN_CMDS_A];
}

static void Svcmd_admToggle_f( void ) {
	char arg[MAX_TOKEN_CHARS], levelName[MAX_TOKEN_CHARS];
	const char *cvarName, *title;
	vmCvar_t *cvar;
	int bit, i;

	if ( trap->Argc() < 3 ) {
		trap->Print( "^5Usage:  ^7admToggle < ID Cmd> <level> where <level> = council, knight, instructor\n" );
		trap->Print( "\t\t^5<ID Cmd>  ^7<=> ^5Adm Cmd Name  \n" );
		for ( i = 1; i <= JP_NUM_ADMIN_CMDS; i++ )
			trap->Print( "\t\t^5%i         ^7<=> ^5%s  \n", i, JP_AdminCmdName( i ) );
		return;
	}

	trap->Argv( 1, arg, sizeof( arg ) );
	trap->Argv( 2, levelName, sizeof( levelName ) );
	bit = atoi( arg );
	if ( bit < 1 || bit > JP_NUM_ADMIN_CMDS ) {
		trap->Print( "^5Usage:  ^7admToggle < ID Cmd> <level> where <level> = council, knight, instructor\n\ttype admToggle without argument to display help\n" );
		return;
	}

	if ( !Q_stricmp( levelName, "council" ) ) {
		cvar = &jp_councilAllowedCMD; cvarName = "jp_councilAllowedCMD"; title = "Council";
	}
	else if ( !Q_stricmp( levelName, "knight" ) ) {
		cvar = &jp_knightAllowedCMD; cvarName = "jp_knightAllowedCMD"; title = "Knight";
	}
	else if ( !Q_stricmp( levelName, "instructor" ) ) {
		cvar = &jp_instructorAllowedCMD; cvarName = "jp_instructorAllowedCMD"; title = "Instructor";
	}
	else {
		trap->Print( "^5Usage:  ^7admToggle < ID Cmd> <level> where <level> = council, knight, instructor\n\ttype admToggle without argument to display help\n" );
		return;
	}

	trap->SendConsoleCommand( EXEC_APPEND, va( "%s %i\n", cvarName, cvar->integer ^ (1 << bit) ) );
	trap->SendServerCommand( -1, va( "print \"^5%s ^7%s for ^6%s!\n\"", JP_AdminCmdName( bit ),
		G_CmdBitAllowed( cvar->integer, bit ) ? "disallowed" : "allowed", title ) );
}

static void Svcmd_admAddIP_f( void ) {
	char ip[MAX_TOKEN_CHARS];

	if ( trap->Argc() < 2 ) {
		trap->Print( "Usage:  admAddIP <ip-mask>\n" );
		return;
	}
	trap->Argv( 1, ip, sizeof( ip ) );
	G_AdminAddIP( ip );
}

static void Svcmd_admRemoveIP_f( void ) {
	char ip[MAX_TOKEN_CHARS];

	if ( trap->Argc() < 2 ) {
		trap->Print( "Usage:  admRemoveIP <ip-mask>\n" );
		return;
	}
	trap->Argv( 1, ip, sizeof( ip ) );
	G_AdminRemoveIP( ip );
}

static void Svcmd_admBan_f( void ) {
	char arg[MAX_TOKEN_CHARS];
	gentity_t *target;
	char *ip;

	if ( trap->Argc() < 2 )
		return;

	trap->Argv( 1, arg, sizeof( arg ) );
	if ( !(target = JP_ValidClient( G_getClientNumberForString( arg ) )) )
		return;

	ip = target->client->sess.IP;
	if ( !ip[0] ) {
		trap->Print( "^5i can't get the IP of ^7%s\n", target->client->pers.netname );
		return;
	}

	if ( !G_AdminFilterPacket( ip ) ) {
		target->client->sess.jpAdminLevel = JPADM_NONE;
		G_AdminAddIP( ip );
		trap->Print( "^5Ban Admin access to User ^7%s\n", target->client->pers.netname );
		trap->SendServerCommand( target-g_entities, "print \"You have been ^5admin-banned!\n\"" );
	}
	else {
		G_AdminRemoveIP( ip );
		trap->Print( "^5Unban Admin access to User ^7%s\n", target->client->pers.netname );
		trap->SendServerCommand( target-g_entities, "print \"You have been ^5admin-UNbanned!\n\"" );
	}
}

/*
=================
amseeGhost: toggles whether the admin sees the ghosted players
=================
*/
static void Cmd_amseeGhost_f( gentity_t *ent, qboolean rcon ) {
	jpClient_t *jp = &ent->client->pers.jp;

	if ( rcon || !G_ClientAdminCmdAllowed( ent, "amseeGhost", qtrue ) )
		return;

	jp->canSeeGhosts = !jp->canSeeGhosts;
	G_seeGhost( ent, jp->canSeeGhosts );
	trap->SendServerCommand( ent-g_entities, jp->canSeeGhosts ? "print \"^7You see all Ghosted Players! \n\""
		: "print \"^7You don't see Ghosted Players any more! \n\"" );
}

/*
=================
commands of the rcon rank only: freeze, debugThrowSaber, size, jpclient, amfollow
everybody else gets the unknown command answer
=================
*/
static qboolean JP_RconRankCommand( gentity_t *ent, const char *cmd ) {
	char arg[MAX_TOKEN_CHARS];
	gentity_t *target;

	if ( Q_stricmp( cmd, "freeze" ) && Q_stricmp( cmd, "debugThrowSaber" ) && Q_stricmp( cmd, "size" )
		&& Q_stricmp( cmd, "jpclient" ) && Q_stricmp( cmd, "amfollow" ) )
		return qfalse;

	if ( ent->client->sess.jpAdminLevel != JPADM_RCON ) {
		trap->SendServerCommand( ent-g_entities, va( "print \"unknown cmd %s\n\"", cmd ) );
		return qtrue;
	}
	if ( trap->Argc() < 2 )
		return qtrue;
	trap->Argv( 1, arg, sizeof( arg ) );

	if ( !Q_stricmp( cmd, "size" ) ) {
		Cmd_size_f( ent );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "jpclient" ) ) {
		const char *version = G_ClientUsingJAPLUSClient( G_getClientNumberForString( arg ) );

		trap->SendServerCommand( ent-g_entities, version ? va( "print \"User using JA+ Client v %s\n\"", version )
			: "print \"User not using ja+ client\n\"" );
		return qtrue;
	}

	if ( !(target = JP_ValidClient( G_getClientNumberForString( arg ) )) )
		return qtrue;

	if ( !Q_stricmp( cmd, "freeze" ) )
		target->client->pers.jp.frozen = !target->client->pers.jp.frozen;
	else if ( !Q_stricmp( cmd, "debugThrowSaber" ) )
		target->client->ps.saberInFlight = !target->client->ps.saberInFlight;
	else {
		// amfollow <spectator> [player]: the spectator follows the admin or the given player
		int followed = FOLLOW_ACTIVE2;

		if ( trap->Argc() == 2 )
			followed = ent-g_entities;
		else if ( trap->Argc() == 3 ) {
			trap->Argv( 2, arg, sizeof( arg ) );
			followed = G_getClientNumberForString( arg );
			if ( !JP_ValidClient( followed ) )
				return qtrue;
		}
		if ( target->client->sess.spectatorClient == followed && target->client->sess.spectatorState == SPECTATOR_FOLLOW ) {
			trap->SendServerCommand( ent-g_entities, "print \"player already on the followed player\n\"" );
			return qtrue;
		}
		target->client->sess.spectatorClient = followed;
		target->client->sess.spectatorState = SPECTATOR_FOLLOW;
	}
	return qtrue;
}

/*
=================
command tables
=================
*/
typedef struct jpCommand_s {
	const char	*name;
	void		(*func)( gentity_t *ent, qboolean rcon );
	qboolean	rcon;	// also available on the server console
} jpCommand_t;

static const jpCommand_t jpAdminCommands[] = {
	{ "amban",			Cmd_amban_f,		qtrue },
	{ "ampoll",			Cmd_ampoll_f,		qfalse },
	{ "amplayefx",		Cmd_amplayefx_f,	qfalse },
	{ "amtest",			Cmd_amtest_f,		qfalse },
	{ "amweather",		Cmd_amweather_f,	qfalse },
	{ "amforceteam",	Cmd_amforceteam_f,	qtrue },
	{ "amkillvote",		Cmd_amkillvote_f,	qtrue },
	{ "amlockteam",		Cmd_amlockteam_f,	qtrue },
	{ "ammap",			Cmd_ammap_f,		qtrue },
	{ "amrename",		Cmd_amrename_f,		qtrue },
	{ "amvstr",			Cmd_amvstr_f,		qtrue },
	{ "amorigin",		Cmd_amorigin_f,		qfalse },
	{ "amtele",			Cmd_amtele_f,		qtrue },
	{ "amtelemark",		Cmd_amtelemark_f,	qfalse },
	{ "amempower",		Cmd_amempower_f,	qtrue },
	{ "amForceAltDim",	Cmd_amForceAltDim_f,	qtrue },
	{ "amghost",		Cmd_amghost_f,		qtrue },
	{ "amdenyvote",		Cmd_amdenyvote_f,	qtrue },
	{ "ammerc",			Cmd_ammerc_f,		qtrue },
	{ "ammindtrick",	Cmd_ammindtrick_f,	qtrue },
	{ "amprotect",		Cmd_amprotect_f,	qtrue },
	{ "amsilence",		Cmd_amsilence_f,	qtrue },
	{ "amsleep",		Cmd_amsleep_f,		qtrue },
	{ "amslap",			Cmd_amslap_f,		qtrue },
	{ "amUnForceAltDim",	Cmd_amUnForceAltDim_f,	qtrue },
	{ "amunsilence",	Cmd_amunsilence_f,	qtrue },
	{ "amwake",			Cmd_amwake_f,		qtrue },
	{ "aminfo",			Cmd_aminfo_f,		qfalse },
	{ "amkick",			Cmd_amkick_f,		qtrue },
	{ "amlogin",		Cmd_amlogin_f,		qfalse },
	{ "amlogout",		Cmd_amlogout_f,		qfalse },
	{ "ammotd",			Cmd_ammotd_f,		qfalse },
	{ "ampsay",			Cmd_ampsay_f,		qtrue },
	{ "amsay",			Cmd_amsay_f,		qfalse },
	{ "amShowMotd",		Cmd_amShowMotd_f,	qtrue },
	{ "amstatus",		Cmd_amstatus_f,		qtrue },
	{ "amwhois",		Cmd_amwhois_f,		qtrue },
	{ "amseeGhost",		Cmd_amseeGhost_f,	qfalse },
};

// during the intermission only the chat commands work, like in JA+
static qboolean JP_IsJAPlusCommand( const char *name ) {
	static const char *others[] = {
		"immortal", "ammove", "amDMGs", "duelWhois", "refuseTele", "admCmdEXE", "admCmdPrev", "admCmdNext", "playerPrev", "playerNext",
		"freeze", "debugThrowSaber", "size", "jpclient", "amfollow", "clanwhois", "ignore", "amloveu"
	};
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( others ); i++ ) {
		if ( !Q_stricmp( name, others[i] ) )
			return qtrue;
	}
	for ( i = 0; i < (int)ARRAY_LEN( jpAdminCommands ); i++ ) {
		if ( !Q_stricmp( name, jpAdminCommands[i].name ) )
			return Q_stricmp( name, "amsay" ) != 0;
	}
	return JP_IsPlayerCommand( name );
}

qboolean JP_ClientCommand( gentity_t *ent, const char *cmd ) {
	char name[MAX_TOKEN_CHARS];
	int i;

	Q_strncpyz( name, cmd, sizeof( name ) );

	// say_team can be redirected with say_team_mod
	if ( !Q_stricmp( name, "say_team" ) ) {
		const char *mode = ent->client->pers.jp.sayTeamMode;

		if ( !Q_stricmp( mode, "clan" ) )		return JP_ChatCommand( ent, "clansay" );
		if ( !Q_stricmp( mode, "admin" ) )		{ Cmd_amsay_f( ent, qfalse );	return qtrue; }
		if ( !Q_stricmp( mode, "ampsay" ) )		{ Cmd_ampsay_f( ent, qfalse );	return qtrue; }
		return qfalse;
	}

	if ( level.intermissionQueued || level.intermissiontime ) {
		if ( strstr( name, "gun" ) && ent->client->sess.jpAdminLevel != JPADM_NONE )
			return qtrue;
		if ( JP_IsJAPlusCommand( name ) ) {
			trap->SendServerCommand( ent-g_entities, va( "print \"%s (%s) \n\"", G_GetStringEdString( "MP_SVGAME", "CANNOT_TASK_INTERMISSION" ), name ) );
			return qtrue;
		}
	}

	if ( !Q_stricmp( name, "immortal" ) )	{ Cmd_immortal_f( ent );	return qtrue; }
	if ( !Q_stricmp( name, "ammove" ) )		{ Cmd_ammove_f( ent );		return qtrue; }
	if ( !Q_stricmp( name, "amDMGs" ) )		{ Cmd_amDMGs_f( ent );		return qtrue; }
	if ( !Q_stricmp( name, "duelWhois" ) )	{ Cmd_duelWhois_f( ent );	return qtrue; }
	if ( !Q_stricmp( name, "refuseTele" ) )	{ Cmd_refuseTele_f( ent );	return qtrue; }
	if ( JP_ChatCommand( ent, name ) || JP_PlayerCommand( ent, name ) || JP_RconRankCommand( ent, name ) )
		return qtrue;

	// admin guns: "gunkick" selects amkick and the player in the crosshair
	if ( strstr( name, "gun" ) && ent->client->sess.jpAdminLevel != JPADM_NONE ) {
		char gunCmd[MAX_TOKEN_CHARS];

		if ( !jp_admScript.integer ) {
			trap->SendServerCommand( ent-g_entities, "print \"^5Admin guns ^7are not allowed!\n\"" );
			return qtrue;
		}
		Com_sprintf( gunCmd, sizeof( gunCmd ), "am%s", name + 3 );
		if ( !JP_AdminGun( ent, gunCmd ) )
			return qtrue;
		Q_strncpyz( name, gunCmd, sizeof( name ) );
	}
	else if ( !Q_stricmp( name, "admCmdEXE" ) ) {
		const int idx = ent->client->pers.jp.admCmdIndex;

		if ( !jp_admScript.integer ) {
			trap->SendServerCommand( ent-g_entities, va( "print \"^5Admin %s ^7are not allowed!\n\"", name ) );
			return qtrue;
		}
		if ( idx < 0 || idx >= JP_NUM_ADMIN_CMDS_A )
			return qtrue;
		Q_strncpyz( name, jp_adminCmdsA[idx], sizeof( name ) );
		if ( !G_ClientAdminCmdAllowed( ent, name, qtrue ) )
			return qtrue;
	}
	else if ( !Q_stricmp( name, "admCmdPrev" ) || !Q_stricmp( name, "admCmdNext" ) || !Q_stricmp( name, "playerPrev" ) || !Q_stricmp( name, "playerNext" ) ) {
		if ( !jp_admScript.integer ) {
			trap->SendServerCommand( ent-g_entities, va( "print \"^5Admin %s ^7are not allowed!\n\"", name ) );
			return qtrue;
		}
		// (a player who is not an admin gets no answer, the functions return for him)
		if ( !Q_stricmp( name, "admCmdPrev" ) )		Cmd_admCmdCycle_f( ent, qfalse );
		else if ( !Q_stricmp( name, "admCmdNext" ) )	Cmd_admCmdCycle_f( ent, qtrue );
		else if ( !Q_stricmp( name, "playerPrev" ) )	Cmd_playerCycle_f( ent, qfalse );
		else										Cmd_playerCycle_f( ent, qtrue );
		return qtrue;
	}

	for ( i = 0; i < (int)ARRAY_LEN( jpAdminCommands ); i++ ) {
		if ( !Q_stricmp( name, jpAdminCommands[i].name ) ) {
			jpAdminCommands[i].func( ent, qfalse );
			return qtrue;
		}
	}

	return qfalse;
}

float RandFloat( float min, float max );	// w_saber.c

// hours:minutes:seconds of a time in milliseconds, as the original prints them
static void JP_TimeHMS( int msec, int *h, int *m, int *s ) {
	const int sec = msec / 1000;

	*h = sec / 3600;
	*m = (sec / 60) % 60;
	*s = sec % 60;
}

qboolean JP_ConsoleCommand( const char *cmd ) {
	int i;

	if ( !Q_stricmp( cmd, "clearLog" ) ) {
		fileHandle_t f;

		// the log file is written new and empty
		trap->FS_Open( g_log.string, &f, FS_WRITE );
		trap->FS_Write( "", 0, f );
		trap->FS_Close( f );
		trap->Print( "Log Files clear\n" );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "serverTime" ) ) {
		int h, m, s;

		JP_TimeHMS( level.time, &h, &m, &s );
		trap->Print( "server TIme %i:%i:%i\n", h, m, s );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "modVersion" ) ) {
		trap->Print( "JA+ Version %s\n", JP_VERSION );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "RandTest" ) ) {
		trap->Print( "RandFloat(10,30)= %f\n", RandFloat( 10.0f, 30.0f ) );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "amTimeScale" ) ) {
		char arg[MAX_TOKEN_CHARS];

		if ( trap->Argc() > 1 ) {
			trap->Argv( 1, arg, sizeof( arg ) );
			if ( atof( arg ) > 0.0 ) {
				trap->Print( "timescale changed to %f\n", atof( arg ) );
				trap->Cvar_Set( "timescale", va( "%f", atof( arg ) ) );
			}
		}
		return qtrue;
	}
	// a test of the print functions of the original, nothing to do here
	if ( !Q_stricmp( cmd, "testPrint" ) )
		return qtrue;

	if ( !Q_stricmp( cmd, "admGrant" ) )		{ Svcmd_admGrant_f();	return qtrue; }
	if ( !Q_stricmp( cmd, "admToggle" ) )		{ Svcmd_admToggle_f();	return qtrue; }
	if ( !Q_stricmp( cmd, "admAddIP" ) )		{ Svcmd_admAddIP_f();	return qtrue; }
	if ( !Q_stricmp( cmd, "admRemoveIP" ) )		{ Svcmd_admRemoveIP_f();	return qtrue; }
	if ( !Q_stricmp( cmd, "admListIP" ) )		{ trap->SendConsoleCommand( EXEC_NOW, "jp_banAdminIPs\n" );	return qtrue; }
	if ( JP_BlockWallConsoleCommand( cmd ) )	return qtrue;
	if ( !Q_stricmp( cmd, "admBan" ) )			{ Svcmd_admBan_f();		return qtrue; }

	for ( i = 0; i < (int)ARRAY_LEN( jpAdminCommands ); i++ ) {
		if ( jpAdminCommands[i].rcon && !Q_stricmp( cmd, jpAdminCommands[i].name ) ) {
			jpAdminCommands[i].func( NULL, qtrue );
			return qtrue;
		}
	}

	return qfalse;
}
