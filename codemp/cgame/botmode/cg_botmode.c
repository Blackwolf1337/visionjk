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

// cg_botmode.c -- bot mode on the side of the client: a bot of the server plays for me (game/botmode/g_botmode.c)

#include "cgame/cg_local.h"

/*
=================
Bot mode

Not in JA+. The cp_bot cvars are in the userinfo and ask the server for it, the Bot Mode page of the setup
menus sets them. The server says that it plays for me with a skill in my player configstring, as a bot has
one. From then on it does not run what my keys say, so I am not predicted from them: CG_PredictPlayerState
takes me from the snapshots, as it does for a player I follow, and I see what the bot sees.

Once a frame what I hold is put in step with the bot, so that nothing jumps when it lets go: the weapon I
have selected is its weapon, and the engine is given its view (it takes the yaw). With cg_botModeKeys a key
ends the bot mode: a move or a button that was not down the frame before.
=================
*/
#define BOTMODE_KEYS	( BUTTON_ATTACK | BUTTON_ALT_ATTACK | BUTTON_USE | BUTTON_USE_HOLDABLE | BUTTON_FORCEPOWER \
						| BUTTON_FORCEGRIP | BUTTON_FORCE_LIGHTNING | BUTTON_FORCE_DRAIN )

qboolean CG_BotMode( void ) {
	const clientInfo_t *ci = &cgs.clientinfo[cg.clientNum];

	if ( !cg.snap || cg.demoPlayback || !ci->infoValid || ci->botSkill == -1 )
		return qfalse;
	// the server leaves a spectator his own commands
	return cg.snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR;
}

void CG_BotModeFrame( void ) {
	static qboolean	idle;	// no key was down since the bot took over: one that is down now was pressed anew
	usercmd_t		cmd;

	if ( !CG_BotMode() ) {
		idle = qfalse;
		return;
	}
	cg.weaponSelect = cg.snap->ps.weapon;
	trap->SetClientForceAngle( cg.time + 1000, cg.snap->ps.viewangles );

	if ( !cg_botModeKeys.integer || !trap->GetUserCmd( trap->GetCurrentCmdNumber(), &cmd ) )
		return;
	if ( !cmd.forwardmove && !cmd.rightmove && !cmd.upmove && !( cmd.buttons & BOTMODE_KEYS ) ) {
		idle = qtrue;
	} else if ( idle ) {
		idle = qfalse;
		trap->Cvar_Set( "cp_botMode", "0" );
	}
}

void CG_BotModeDraw( void ) {
	if ( !CG_BotMode() || cg.scoreBoardShowing )
		return;
	CG_DrawProportionalString( 320, 64, cg_botModeKeys.integer ? "BOT MODE (press a key to take over)" : "BOT MODE",
		UI_CENTER | UI_SMALLFONT, colorTable[CT_ICON_BLUE] );
}
