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

// g_botmode.c -- a player who lets a bot play for him: the bot code of the game thinks for his slot, and what
// it wants to do is put into the commands his client sends.

#include "game/g_local.h"
#include "botlib/botlib.h"
#include "game/ai_main.h"

/*
=================
Bot mode

Not in JA+. A client asks for it in its userinfo, the Bot Mode page of the setup menus sets the cvars:

cp_botMode	1: a bot plays for me
cp_botSkill	1 to 5, as for addbot
cp_botName	the bot whose personality it has, as the server knows that bot. None: botfiles/default.jkb
cp_botFight	0: nobody is its enemy, it only walks the route
cp_botChat	1: it says the chat lines of its personality

The slot gets a bot state as a bot has one (BotAISetupClient), so the bot code does for him what it does for a
bot: the route of the map (the waypoints of ai_wpnav.c), the fights, the force powers. He stays a client with
a connection, which is what BOTMODE_HUMAN says to the bot code: the reliable commands of his connection are
his client's to read, and the commands are not handed to the server as a bot's are. His client goes on sending
commands, and ClientThink_real asks G_BotModeCommand for each of them: all of it but its time is replaced by
what the bot wants.

His client predicts his own keys, which the server does not run any more. So the player configstring says
skill, as it does for a bot: the cgame of this tree takes its own player from the snapshots while it reads
that, and every client shows him as a bot on the scoreboard. A client that does not know this sees its own
view jitter.

g_botMode 0 switches it off for the server. A server without bots (bot_enable 0) has no bot library set up
and no bot mode.
=================
*/
extern bot_state_t	*botstates[MAX_CLIENTS];		// ai_main.c

#define BOTMODE_DEFAULT_FILE	"botfiles/default.jkb"
#define BOTMODE_DEFAULT_SKILL	3

static qboolean botModeTold[MAX_CLIENTS];	// he was told that he gets no bot, once is enough

// the bot state of a human who has one
static bot_state_t *G_BotModeState( const gentity_t *ent ) {
	bot_state_t *bs = botstates[ent-g_entities];

	if ( !bs || !bs->inuse || !( bs->settings.botMode & BOTMODE_HUMAN ) )
		return NULL;
	return bs;
}

// The personality file of the bot of that name, the default one for no name, for a bot this server does not
// have and for a file that is not there: BotUtilizePersonality leaves a bot without any values then.
static void G_BotModeFile( const char *botName, char *file, int size ) {
	const char		*info = botName[0] ? G_GetBotInfoByName( botName ) : NULL;
	fileHandle_t	f = 0;

	Q_strncpyz( file, info ? Info_ValueForKey( info, "personality" ) : "", size );
	if ( file[0] )
		trap->FS_Open( file, &f, FS_READ );
	if ( f )
		trap->FS_Close( f );
	else
		Q_strncpyz( file, BOTMODE_DEFAULT_FILE, size );
}

// qtrue: this userinfo asks for a bot and the server gives one, settings is the bot it asks for
static qboolean G_BotModeWanted( const gentity_t *ent, const char *userinfo, bot_settings_t *settings ) {
	const int	clientNum = ent-g_entities;
	const char	*s, *reason = NULL;

	if ( !atoi( Info_ValueForKey( userinfo, "cp_botMode" ) ) ) {
		botModeTold[clientNum] = qfalse;
		return qfalse;
	}
	if ( !g_botMode.integer )
		reason = "Bot mode is switched off on this server.";
	else if ( !trap->Cvar_VariableIntegerValue( "bot_enable" ) )
		reason = "This server runs without bots, so it has no bot mode.";
	if ( reason ) {
		// said once he is in the game, and once more after the next connect: a new map, or another player
		// in this slot
		if ( ent->client->pers.connected != CON_CONNECTED ) {
			botModeTold[clientNum] = qfalse;
		} else if ( !botModeTold[clientNum] ) {
			botModeTold[clientNum] = qtrue;
			trap->SendServerCommand( clientNum, va( "print \"%s\n\"", reason ) );
		}
		return qfalse;
	}

	memset( settings, 0, sizeof( *settings ) );
	settings->botMode = BOTMODE_HUMAN;
	s = Info_ValueForKey( userinfo, "cp_botSkill" );
	settings->skill = s[0] ? Com_Clampi( 1, 5, atoi( s ) ) : BOTMODE_DEFAULT_SKILL;
	s = Info_ValueForKey( userinfo, "cp_botFight" );
	if ( s[0] && !atoi( s ) )
		settings->botMode |= BOTMODE_PEACEFUL;
	if ( !atoi( Info_ValueForKey( userinfo, "cp_botChat" ) ) )
		settings->botMode |= BOTMODE_SILENT;
	G_BotModeFile( Info_ValueForKey( userinfo, "cp_botName" ), settings->personalityfile, sizeof( settings->personalityfile ) );
	return qtrue;
}

/*
=================
G_BotModeUpdate

From ClientUserinfoChanged, in front of the player configstring it writes: the bot state of a human is set
up, changed or taken away to match what his userinfo asks for. That is also what brings it back after a new
map and a map_restart, both of which connect every client again.
=================
*/
void G_BotModeUpdate( gentity_t *ent, const char *userinfo ) {
	const int		clientNum = ent-g_entities;
	bot_state_t		*bs = G_BotModeState( ent );
	bot_settings_t	settings;

	if ( ent->r.svFlags & SVF_BOT )
		return;

	if ( !G_BotModeWanted( ent, userinfo, &settings ) ) {
		if ( bs )
			BotAIShutdownClient( clientNum, qfalse );
		return;
	}
	if ( bs && !strcmp( bs->settings.personalityfile, settings.personalityfile ) ) {
		// the same bot with another skill or other manners: it goes on where it is
		bs->settings.skill = settings.skill;
		bs->settings.botMode = settings.botMode;
		return;
	}
	if ( bs )
		BotAIShutdownClient( clientNum, qfalse );
	BotAISetupClient( clientNum, &settings, qfalse );
}

/*
=================
G_BotModeSkill

0, or the skill of the bot that plays for this human
=================
*/
int G_BotModeSkill( const gentity_t *ent ) {
	const bot_state_t *bs = G_BotModeState( ent );

	return bs ? (int)bs->settings.skill : 0;
}

/*
=================
G_BotModeCommand

From ClientThink_real, for every command of a client. The time of the command stays his client's, his moves
are run in the steps his client sends them in.

A spectator and a player in the intermission keep their own commands: the bot code does nothing for the one,
and the other one says with his own key that he is ready.
=================
*/
void G_BotModeCommand( gentity_t *ent, usercmd_t *ucmd ) {
	const bot_state_t	*bs = G_BotModeState( ent );
	const int			serverTime = ucmd->serverTime;
	int					i;

	// lastucmd has a time once the bot has thought
	if ( !bs || !bs->lastucmd.serverTime )
		return;
	if ( ent->client->sess.sessionTeam == TEAM_SPECTATOR || level.intermissiontime )
		return;

	*ucmd = bs->lastucmd;
	ucmd->serverTime = serverTime;
	// The view of a command is what it says plus delta_angles (PM_UpdateViewAngles). The bot took the ones
	// of when it thought (cur_ps), a respawn or a teleporter since then has set others.
	for ( i = 0; i < 3; i++ )
		ucmd->angles[i] = bs->lastucmd.angles[i] + bs->cur_ps.delta_angles[i] - ent->client->ps.delta_angles[i];
}

/*
=================
G_BotModeCvarChanged

g_botMode was changed: every human is asked again what he wants, and his configstring says what he got
=================
*/
void G_BotModeCvarChanged( void ) {
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		const gentity_t *ent = &g_entities[i];

		if ( ent->inuse && ent->client && ent->client->pers.connected != CON_DISCONNECTED && !( ent->r.svFlags & SVF_BOT ) )
			ClientUserinfoChanged( i );
	}
}
