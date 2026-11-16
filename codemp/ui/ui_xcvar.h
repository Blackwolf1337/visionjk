/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
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

#ifdef XCVAR_PROTO
	#define XCVAR_DEF( name, defVal, update, flags ) extern vmCvar_t name;
#endif

#ifdef XCVAR_DECL
	#define XCVAR_DEF( name, defVal, update, flags ) vmCvar_t name;
#endif

#ifdef XCVAR_LIST
	#define XCVAR_DEF( name, defVal, update, flags ) { & name , #name , defVal , update , flags },
#endif

XCVAR_DEF( capturelimit,					"0",					NULL,				CVAR_ARCHIVE|CVAR_NORESTART|CVAR_SERVERINFO ) // fixme init'd to 8 in game module
XCVAR_DEF( cg_drawCrosshair,				"1",					NULL,				CVAR_ARCHIVE )
XCVAR_DEF( cg_drawCrosshairNames,			"1",					NULL,				CVAR_ARCHIVE )
XCVAR_DEF( cg_marks,						"1",					NULL,				CVAR_ARCHIVE )
// Setup and Controls: the headings that are folded ("foldCvar" of the menu), and the words of its search ("searchCvar")
XCVAR_DEF( v_setupFolds,					"",						NULL,				CVAR_ARCHIVE )
XCVAR_DEF( v_controlsFolds,				"",						NULL,				CVAR_ARCHIVE )
XCVAR_DEF( v_setupSearch,					"",						NULL,				CVAR_INTERNAL )
XCVAR_DEF( v_quality,						"4",					NULL,				CVAR_ARCHIVE )		// Setup, Video: the preset the settings are (UI_QualityFrame), 4 none
XCVAR_DEF( cg_selectedPlayer,				"0",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( cg_selectedPlayerName,			"",						NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
// cvars of the cgame that the setup menus offer before a level was loaded. Most come from cg_xcvar.h (XCVAR_UI, at
// the end of this file); here the ones that are stock in the cgame table, and those whose default or update differs.
XCVAR_DEF( cg_drawFPS,						"0",					NULL,				CVAR_ARCHIVE )
XCVAR_DEF( cg_fov,							"80",					NULL,				CVAR_ARCHIVE )
XCVAR_DEF( cg_drawTimer,					"0",					NULL,				CVAR_ARCHIVE )
XCVAR_DEF( cg_lagometer,					"0",					NULL,				CVAR_ARCHIVE )
XCVAR_DEF( cg_crosshairSize,				"24",					NULL,				CVAR_ARCHIVE )
XCVAR_DEF( cg_thirdPersonRange,				"80",					NULL,				CVAR_ARCHIVE )
XCVAR_DEF( cg_chatBeep,						"1",					NULL,				CVAR_ARCHIVE )
XCVAR_DEF( tay_forceEnemyModel,			"",						NULL,				CVAR_ARCHIVE )
XCVAR_DEF( tay_forceAllyModel,			"",						NULL,				CVAR_ARCHIVE )
XCVAR_DEF( cg_smoothCamera,				"0",						NULL,				CVAR_ARCHIVE )
XCVAR_DEF( tay_specHud,					"0",						NULL,				CVAR_ARCHIVE )
XCVAR_DEF( g_botsFile,						"",						NULL,				CVAR_INIT|CVAR_ROM )
XCVAR_DEF( g_spSkill,						"2",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( g_warmup,						"20",					NULL,				CVAR_ARCHIVE )
XCVAR_DEF( se_language,						"english",				NULL,				CVAR_ARCHIVE|CVAR_NORESTART )
XCVAR_DEF( ui_PrecacheModels,				"0",					NULL,				CVAR_ARCHIVE )
XCVAR_DEF( ui_actualNetGametype,			"3",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_blueteam,						DEFAULT_BLUETEAM_NAME,	NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_blueteam1,					"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_blueteam2,					"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_blueteam3,					"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_blueteam4,					"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_blueteam5,					"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_blueteam6,					"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_blueteam7,					"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_blueteam8,					"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_browserFilterInvalidInfo,		"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( v_browserShowBotOnly,			"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_browserShowEmpty,				"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_browserShowFull,				"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_browserShowPasswordProtected,	"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_bypassMainMenuLoad,			"0",					NULL,				CVAR_INTERNAL )
XCVAR_DEF( ui_char_anim,					"BOTH_WALK1",			NULL,				CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_char_color_blue,				"255",					NULL,				CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_char_color_green,				"255",					NULL,				CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_char_color_red,				"255",					NULL,				CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_char_model,					"jedi_tf",				NULL,				CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_char_skin_head,				"head_a1",				NULL,				CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_char_skin_legs,				"lower_a1",				NULL,				CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_char_skin_torso,				"torso_a1",				NULL,				CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_currentMap,					"0",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_modsay,						"0",					NULL,				CVAR_NONE )
XCVAR_DEF( ui_modsayAllPlayer,				"0",					NULL,				CVAR_NONE )
XCVAR_DEF( ui_modSayText,					"",						NULL,				CVAR_NONE )
XCVAR_DEF( ui_currentNetMap,				"0",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_dedicated,					"0",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_findPlayer,					"Kyle",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_forcePowerDisable,			"0",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_freeSaber,					"0",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_gametype,						"0",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_joinGametype,					"0",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_lastServerRefresh_0,			"",						NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_lastServerRefresh_1,			"",						NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_lastServerRefresh_2,			"",						NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_lastServerRefresh_3,			"",						NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_lastServerRefresh_4,			"",						NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_lastServerRefresh_5,			"",						NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_lastServerRefresh_6,			"",						NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_mapIndex,						"0",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_menuFilesMP,					"ui/jampmenus.txt",		NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( v_classic,						"0",					NULL,				CVAR_ARCHIVE )
XCVAR_DEF( v_cursorKeyOffered,				"0",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_netGametype,					"0",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_netSource,					"0",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_opponentName,					DEFAULT_BLUETEAM_NAME,	NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_rankChange,					"0",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_recordSPDemo,					"0",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_redteam,						DEFAULT_REDTEAM_NAME,	NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_redteam1,						"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL ) //rww - these used to all default to 0 (closed).. I changed them to 1 (human)
XCVAR_DEF( ui_redteam2,						"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_redteam3,						"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_redteam4,						"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_redteam5,						"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_redteam6,						"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_redteam7,						"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_redteam8,						"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_saber,						DEFAULT_SABER,			NULL,				CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_saber2,						"none",					NULL,				CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_saber2_color,					"yellow",				NULL,				CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_saber_color,					"yellow",				NULL,				CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_saber_type,					"single",				NULL,				CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_sbBlue1,						"0",					NULL,				CVAR_ARCHIVE )
XCVAR_DEF( ui_sbBlue2,						"0",					NULL,				CVAR_ARCHIVE )
XCVAR_DEF( ui_sbGreen1,						"0",					NULL,				CVAR_ARCHIVE )
XCVAR_DEF( ui_sbGreen2,						"0",					NULL,				CVAR_ARCHIVE )
XCVAR_DEF( ui_sbRed1,						"0",					NULL,				CVAR_ARCHIVE )
XCVAR_DEF( ui_sbRed2,						"0",					NULL,				CVAR_ARCHIVE )
XCVAR_DEF( ui_selectedHolsterIndex,			"1",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_selectedModelIndex,			"16",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_serverFilterType,				"0",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_serverStatusTimeOut,			"7000",					NULL,				CVAR_ARCHIVE|CVAR_INTERNAL )
XCVAR_DEF( ui_screenshotType,				"jpg",					UI_UpdateScreenshot,	CVAR_ARCHIVE )
XCVAR_DEF( ui_singlePlayerActive,			"0",					NULL,				CVAR_INTERNAL )
// VisionJK: how many the window "Clean Up Settings" lists, for its line when there are none (ui_cvarclean.c)
XCVAR_DEF( ui_cvarCleanCount,				"0",					NULL,				CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_modelPickTarget,				"",						NULL,				CVAR_INTERNAL )
XCVAR_DEF( ui_modelPickName,				"",						NULL,				CVAR_INTERNAL )

#undef XCVAR_DEF

// the rest of the cvars of the cgame that the menus offer (XCVAR_UI of cg_xcvar.h), with its defaults and flags
#define XCVAR_UI_ONLY
#include "cgame/cg_xcvar.h"
#undef XCVAR_UI_ONLY
