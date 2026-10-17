/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2005 - 2015, ioquake3 contributors
Copyright (C) 2013 - 2015, OpenJK contributors
Copyright (C) 2026, VisionJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
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

// cg_consolecmds.c -- text commands typed in at the local console, or
// executed by a key binding

#include "cg_local.h"
#include "game/bg_saga.h"
#include "ui/ui_shared.h"

/*
=================
CG_TargetCommand_f

=================
*/
void CG_TargetCommand_f( void ) {
	int		targetNum;
	char	test[4];

	targetNum = CG_CrosshairPlayer();
	if ( targetNum == -1 ) {
		return;
	}

	trap->Cmd_Argv( 1, test, 4 );
	trap->SendClientCommand( va( "gc %i %i", targetNum, atoi( test ) ) );
}

/*
=================
CG_SizeUp_f

Keybinding command
=================
*/
static void CG_SizeUp_f (void) {
	trap->Cvar_Set( "cg_viewsize", va( "%i", Q_min( cg_viewsize.integer + 10, 100 ) ) );
}

/*
=================
CG_SizeDown_f

Keybinding command
=================
*/
static void CG_SizeDown_f (void) {
	trap->Cvar_Set( "cg_viewsize", va( "%i", Q_max( cg_viewsize.integer - 10, 30 ) ) );
}

/*
=============
CG_Viewpos_f

Debugging command to print the current position
=============
*/
static void CG_Viewpos_f (void) {
	trap->Print ("%s (%i %i %i) : %i\n", cgs.mapname, (int)cg.refdef.vieworg[0],
		(int)cg.refdef.vieworg[1], (int)cg.refdef.vieworg[2],
		(int)cg.refdef.viewangles[YAW]);
}

/*
=================
CG_ScoresDown_f

=================
*/
static void CG_ScoresDown_f( void ) {

	// twice quickly keeps the scoreboard open, with the mouse free for its rows (hud/cg_scores.c)
	if ( !CG_ScoresPress() ) {
		return;
	}
	CG_BuildSpectatorString();
	if ( cg.scoresRequestTime + 2000 < cg.time ) {
		// the scores are more than two seconds out of data,
		// so request new ones
		cg.scoresRequestTime = cg.time;
		trap->SendClientCommand( "score" );

		// leave the current scores up if they were already
		// displayed, but if this is the first hit, clear them out
		if ( !cg.showScores ) {
			cg.showScores = qtrue;
			cg.numScores = 0;
		}
	} else {
		// show the cached contents even if they just pressed if it
		// is within two seconds
		cg.showScores = qtrue;
	}
}

/*
=================
CG_ScoresUp_f

=================
*/
static void CG_ScoresUp_f( void ) {
	CG_ScoresRelease();
	if ( CG_ScoresKept() ) {
		return;
	}
	if ( cg.showScores ) {
		cg.showScores = qfalse;
		cg.scoreFadeTime = cg.time;
	}
}

/*
=================
CG_Key_f

v_cgKey <key> [up|char]: a key or a button of the mouse for the cgame, or with char a letter by its code, as the engine hands one over while the cgame
has the keys (the chat box, the card of a player, the scoreboard that is kept). For a script in place of a
hand, as v_uiKey is for the menus. By name (esc, tab, enter, mouse1, mouse2, wheelup, wheeldown, pgup, pgdn) or
by its number.
=================
*/
static void CG_Key_f( void ) {
	static const struct {
		const char	*name;
		int			key;
	} names[] = {
		{ "esc", A_ESCAPE }, { "tab", A_TAB }, { "enter", A_ENTER }, { "mouse1", A_MOUSE1 }, { "mouse2", A_MOUSE2 },
		{ "wheelup", A_MWHEELUP }, { "wheeldown", A_MWHEELDOWN }, { "pgup", A_PAGE_UP }, { "pgdn", A_PAGE_DOWN },
	};
	const char	*arg = CG_Argv( 1 );
	int			key = atoi( arg );
	size_t		i;

	for ( i = 0; i < ARRAY_LEN( names ); i++ ) {
		if ( !Q_stricmp( arg, names[i].name ) ) {
			key = names[i].key;
		}
	}
	if ( key <= 0 || key >= MAX_KEYS || !( trap->Key_GetCatcher() & KEYCATCH_CGAME ) ) {
		return;
	}
	if ( !Q_stricmp( CG_Argv( 2 ), "char" ) ) {
		CG_KeyEvent( key | K_CHAR_FLAG, qtrue );		// a letter that was typed, by its code
		return;
	}
	CG_KeyEvent( key, (qboolean)( Q_stricmp( CG_Argv( 2 ), "up" ) != 0 ) );
}

void CG_ClientList_f( void )
{
	clientInfo_t *ci;
	int i;
	int count = 0;

	for( i = 0; i < MAX_CLIENTS; i++ )
	{
		ci = &cgs.clientinfo[ i ];
		if( !ci->infoValid )
			continue;

		switch( ci->team )
		{
		case TEAM_FREE:
			Com_Printf( "%2d " S_COLOR_YELLOW "F   " S_COLOR_WHITE "%s" S_COLOR_WHITE "%s\n", i, ci->name, (ci->botSkill != -1) ? " (bot)" : "" );
			break;

		case TEAM_RED:
			Com_Printf( "%2d " S_COLOR_RED "R   " S_COLOR_WHITE "%s" S_COLOR_WHITE "%s\n", i,
				ci->name, (ci->botSkill != -1) ? " (bot)" : "" );
			break;

		case TEAM_BLUE:
			Com_Printf( "%2d " S_COLOR_BLUE "B   " S_COLOR_WHITE "%s" S_COLOR_WHITE "%s\n", i,
				ci->name, (ci->botSkill != -1) ? " (bot)" : "" );
			break;

		default:
		case TEAM_SPECTATOR:
			Com_Printf( "%2d " S_COLOR_YELLOW "S   " S_COLOR_WHITE "%s" S_COLOR_WHITE "%s\n", i, ci->name, (ci->botSkill != -1) ? " (bot)" : "" );
			break;
		}

		count++;
	}

	Com_Printf( "Listed %2d clients\n", count );
}


static void CG_TellTarget_f( void ) {
	int		clientNum;
	char	command[MAX_SAY_TEXT+10];
	char	message[MAX_SAY_TEXT];

	clientNum = CG_CrosshairPlayer();
	if ( clientNum == -1 ) {
		return;
	}

	trap->Cmd_Args( message, sizeof(message) );
	Com_sprintf( command, sizeof(command), "tell %i %s", clientNum, message );
	trap->SendClientCommand( command );
}

static void CG_TellAttacker_f( void ) {
	int		clientNum;
	char	command[MAX_SAY_TEXT + 10];
	char	message[MAX_SAY_TEXT];

	clientNum = CG_LastAttacker();
	if ( clientNum == -1 ) {
		return;
	}

	trap->Cmd_Args( message, sizeof(message) );
	Com_sprintf( command, sizeof(command), "tell %i %s", clientNum, message );
	trap->SendClientCommand( command );
}

/*
==================
CG_StartOrbit_f
==================
*/

static void CG_StartOrbit_f( void ) {
	char var[MAX_TOKEN_CHARS];

	trap->Cvar_VariableStringBuffer( "developer", var, sizeof( var ) );
	if ( !atoi(var) ) {
		return;
	}
	if (cg_cameraOrbit.value != 0) {
		trap->Cvar_Set ("cg_cameraOrbit", "0");
		trap->Cvar_Set("cg_thirdPerson", "0");
	} else {
		trap->Cvar_Set("cg_cameraOrbit", "5");
		trap->Cvar_Set("cg_thirdPerson", "1");
		trap->Cvar_Set("cg_thirdPersonAngle", "0");
		trap->Cvar_Set("cg_thirdPersonRange", "100");
	}
}

void CG_SiegeBriefingDisplay(int team, int dontshow);
static void CG_SiegeBriefing_f(void)
{
	int team;

	if (cgs.gametype != GT_SIEGE)
	{ //Cannot be displayed unless in this gametype
		return;
	}

	team = cg.predictedPlayerState.persistant[PERS_TEAM];

	if (team != SIEGETEAM_TEAM1 &&
		team != SIEGETEAM_TEAM2)
	{ //cannot be displayed if not on a valid team
		return;
	}

	CG_SiegeBriefingDisplay(team, 0);
}

static void CG_SiegeCvarUpdate_f(void)
{
	int team;

	if (cgs.gametype != GT_SIEGE)
	{ //Cannot be displayed unless in this gametype
		return;
	}

	team = cg.predictedPlayerState.persistant[PERS_TEAM];

	if (team != SIEGETEAM_TEAM1 &&
		team != SIEGETEAM_TEAM2)
	{ //cannot be displayed if not on a valid team
		return;
	}

	CG_SiegeBriefingDisplay(team, 1);
}

static void CG_SiegeCompleteCvarUpdate_f(void)
{
	if (cgs.gametype != GT_SIEGE)
	{ //Cannot be displayed unless in this gametype
		return;
	}

	// Set up cvars for both teams
	CG_SiegeBriefingDisplay(SIEGETEAM_TEAM1, 1);
	CG_SiegeBriefingDisplay(SIEGETEAM_TEAM2, 1);
}

static void CG_LoadHud_f( void ) {
	const char *hudSet = cg_hudFiles.string;
	if ( hudSet[0] == '\0' ) {
		hudSet = "ui/jahud.txt";
	}

	String_Init();
	Menu_Reset();
	CG_LoadMenus( hudSet );
}

typedef struct consoleCommand_s {
	const char	*cmd;
	void		(*func)(void);
} consoleCommand_t;

int cmdcmp( const void *a, const void *b ) {
	return Q_stricmp( (const char *)a, ((consoleCommand_t*)b)->cmd );
}

static consoleCommand_t	commands[] = {
	{ "+v_cursor",					CG_SelectCursorDown_f },
	{ "+scores",					CG_ScoresDown_f },
	{ "+v_voice",						CG_VoiceDown_f },
	{ "+v_voiceclan",					CG_VoiceClanDown_f },
	{ "-v_cursor",					CG_SelectCursorUp_f },
	{ "-scores",					CG_ScoresUp_f },
	{ "-v_voice",						CG_VoiceUp_f },
	{ "-v_voiceclan",					CG_VoiceClanUp_f },
	{ "admCmdEXE",					CG_JPAdmCmdExe_f },
	{ "admCmdNext",					CG_JPAdmCycleCmd_f },
	{ "admCmdPrev",					CG_JPAdmCycleCmd_f },
	{ "v_amkey",						CG_AuthCommand_f },
	{ "v_boombox",					CG_Boombox_f },
	{ "briefing",					CG_SiegeBriefing_f },
	{ "v_chatDrag",				CG_ChatDrag_f },
	{ "tay_emoji",				CG_ChatEmoji_f },
	{ "tay_cosmetics",			CG_Cosmetics_f },
	{ "v_chatOpen",				CG_ChatOpen_f },
	{ "v_cgKey",						CG_Key_f },
	{ "clientlist",					CG_ClientList_f },
	{ "cursor",						CG_SelectCursor_f },
	{ "forcenext",					CG_NextForcePower_f },
	{ "forceprev",					CG_PrevForcePower_f },
	{ "invnext",					CG_NextInventory_f },
	{ "invprev",					CG_PrevInventory_f },
	{ "loaddeferred",				CG_LoadDeferredPlayers },
	{ "loadhud",					CG_LoadHud_f },
	{ "nextframe",					CG_TestModelNextFrame_f },
	{ "nextskin",					CG_TestModelNextSkin_f },
	{ "v_peers",						CG_PeerList_f },
	{ "playerNext",					CG_JPAdmCyclePlayer_f },
	{ "playerPrev",					CG_JPAdmCyclePlayer_f },
	{ "pluginDisable",				CG_JPPluginDisable_f },
	{ "prevframe",					CG_TestModelPrevFrame_f },
	{ "prevskin",					CG_TestModelPrevSkin_f },
	{ "reloadReplier",				CG_JPReloadReplier_f },
	{ "v_scores_pick",				CG_ScoresPick_f },
	{ "showPlayerID",				CG_JPShowPlayerID_f },
	{ "siegeCompleteCvarUpdate",	CG_SiegeCompleteCvarUpdate_f },
	{ "siegeCvarUpdate",			CG_SiegeCvarUpdate_f },
	{ "sizedown",					CG_SizeDown_f },
	{ "sizeup",						CG_SizeUp_f },
	{ "startOrbit",					CG_StartOrbit_f },
	{ "v_status_test",				CG_StatusTest_f },
	{ "v_statuses",					CG_StatusList_f },
	{ "tcmd",						CG_TargetCommand_f },
	{ "tell_attacker",				CG_TellAttacker_f },
	{ "tell_target",				CG_TellTarget_f },
	{ "testgun",					CG_TestGun_f },
	{ "testmodel",					CG_TestModel_f },
	{ "viewpos",					CG_Viewpos_f },
	{ "v_voice_devices",				CG_VoiceDevices_f },
	{ "v_voice_mute",					CG_VoiceMute_f },
	{ "v_voice_status",				CG_VoiceStatus_f },
	{ "v_voice_test",					CG_VoiceTest_f },
	{ "v_voice_unmute",				CG_VoiceUnmute_f },
	{ "weapnext",					CG_NextWeapon_f },
	{ "weapon",						CG_Weapon_f },
	{ "weaponclean",				CG_WeaponClean_f },
	{ "weapprev",					CG_PrevWeapon_f },
};

static const size_t numCommands = ARRAY_LEN( commands );

/*
=================
CG_ConsoleCommand

The string has been tokenized and can be retrieved with
Cmd_Argc() / Cmd_Argv()
=================
*/
qboolean CG_ConsoleCommand( void ) {
	consoleCommand_t	*command = NULL;

	// JA+: admin gun
	if ( CG_JPGunCommand( CG_Argv( 0 ) ) )
		return qtrue;

	command = (consoleCommand_t *)Q_LinearSearch( CG_Argv( 0 ), commands, numCommands, sizeof( commands[0] ), cmdcmp );

	if ( !command || !command->func )
		return qfalse;

	command->func();
	return qtrue;
}

static const char *gcmds[] = {
	"addbot",
	"callteamvote",
	"callvote",
	"duelteam",
	"follow",
	"follownext",
	"followprev",
	"forcechanged",
	"give",
	"god",
	"kill",
	"levelshot",
	"loaddefered",
	"noclip",
	"notarget",
	"NPC",
	"say",
	"say_team",
	"setviewpos",
	"siegeclass",
	"stats",
	//"stopfollow",
	"team",
	"teamtask",
	"teamvote",
	"tell",
	"voice_cmd",
	"vote",
	"where",
	"zoom"
};
static const size_t numgcmds = ARRAY_LEN( gcmds );

// JA+: the commands of the server module, the client plugin adds them for tab completion
static const char *jpgcmds[] = {
	"amAltDim", "amban", "amcolor", "amdenyvote", "amDMGs", "amdropsaber", "amempower", "amForceAltDim", "amforceteam",
	"amghost", "aminfo", "amkick", "amkillvote", "amknockmedown", "amlockteam", "amlogin", "amlogout", "ammap", "ammerc",
	"ammindtrick", "ammotd", "ammove", "amorigin", "ampoll", "amprotect", "ampsay", "amrename", "amsay", "amseeGhost",
	"amShowMotd", "amsilence", "amslap", "amsleep", "amstatus", "amtele", "amtelemark", "amUnForceAltDim", "amunsilence",
	"amvstr", "amwake", "amwhois", "clanpass", "clansay", "clanwhois", "duelWhois",
	"engage_FullForceDuel", "ignore", "immortal", "jetpack", "refuseTele", "sabercolor", "say_team_mod", "toggleFlame",
	"ui_macroenable", "ui_macroscan",
};
static const size_t numjpgcmds = ARRAY_LEN( jpgcmds );

/*
=================
CG_InitConsoleCommands

Let the client system know about all of our commands
so it can perform tab completion
=================
*/
void CG_InitConsoleCommands( void ) {
	size_t i;

	for ( i = 0; i < numCommands; i++ )
		trap->AddCommand( commands[i].cmd );

	//
	// the game server will interpret these commands, which will be automatically
	// forwarded to the server after they are not recognized locally
	//
	for( i = 0; i < numgcmds; i++ )
		trap->AddCommand( gcmds[i] );

	for ( i = 0; i < numjpgcmds; i++ )
		trap->AddCommand( jpgcmds[i] );
}
