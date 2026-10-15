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

XCVAR_DEF( bg_fighterAltControl,				"0",					NULL,					CVAR_SYSTEMINFO )
// what this cgame draws that a server only sends to who asks for it, bit 0: statuses above the heads (status/cg_status.c),
// bit 1: who plays with a client of this tree (status/cg_peers.c)
XCVAR_DEF( cjp_caps,							"3",					NULL,					CVAR_USERINFO|CVAR_ROM )
XCVAR_DEF( cjp_client,							"1.4B4",				NULL,					CVAR_USERINFO|CVAR_ROM )
XCVAR_DEF( cjp_vision,							GIT_TAG,				NULL,					CVAR_USERINFO|CVAR_ROM )
XCVAR_DEF( cl_yawspeed,							"140",					NULL,					CVAR_ARCHIVE )	// (engine cvar, read by the JA+ yawspeed check)
XCVAR_DEF( cp_pluginDisable,						"0",					NULL,					CVAR_USERINFO|CVAR_ARCHIVE )
XCVAR_DEF( cp_sbRGB1,							"0",					NULL,					CVAR_USERINFO|CVAR_ARCHIVE )
XCVAR_DEF( cp_sbRGB2,							"0",					NULL,					CVAR_USERINFO|CVAR_ARCHIVE )
XCVAR_DEF( cp_holster,							"1",					NULL,					CVAR_USERINFO )
XCVAR_DEF( cp_login,							"NOT SET",				NULL,					CVAR_USERINFO|CVAR_ARCHIVE )
XCVAR_DEF( cp_clanPwd,							"none",					NULL,					CVAR_USERINFO|CVAR_ARCHIVE )
XCVAR_DEF( cp_autoReplierFile,					"autoreplier.cfg",		NULL,					CVAR_INTERNAL )
XCVAR_DEF( cp_hookModel,						"0",					NULL,					CVAR_INTERNAL )
XCVAR_DEF( cp_altDimAlpha,						"40",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( broadsword,							"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_animBlend,						"1",					NULL,					CVAR_NONE )
XCVAR_DEF( cg_animSpeed,						"1",					NULL,					CVAR_CHEAT )
XCVAR_DEF( cg_auraShell,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_autoSwitch,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_bobPitch,							"0.002",				NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_bobRoll,							"0.002",				NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_bobUp,							"0.005",				NULL,					CVAR_ARCHIVE )
// bot mode (cg_botmode.c): the client plays by itself. They are in ui_xcvar.h too. cg_botMode is not saved.
XCVAR_DEF( cg_botMode,							"0",					NULL,					CVAR_NONE )
XCVAR_DEF( cg_botModeFight,						"2",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_botModeKeys,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_botModeRoam,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_botModeSkill,						"3",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_cameraOrbit,						"0",					NULL,					CVAR_CHEAT )
XCVAR_DEF( cg_cameraOrbitDelay,					"50",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_centerTime,						"3",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_chatBeep,							"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_chatBox,							"10000",				NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_chatBoxHeight,					"350",					NULL,					CVAR_ARCHIVE )
// the chat box of this tree (chat/cg_chat.c): its width, and whether the message mode keys open its line to type in
XCVAR_DEF( cg_chatBoxRows,						"10",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_chatBoxScale,						"0.85",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_chatBoxWidth,						"380",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_chatInput,						"1",					NULL,					CVAR_ARCHIVE )
// the menu in the level while in a game (menu3d/cg_menu3d.c), 0: flat on the screen as in the base game
XCVAR_DEF( cg_immersiveMenu,					"1",					NULL,					CVAR_ARCHIVE )
// chat bubbles above the heads (bubble/cg_bubble.c), and how far away one still sees them
XCVAR_DEF( cg_chatBubbles,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_bubbleRange,						"1200",					NULL,					CVAR_ARCHIVE )
// statuses above the heads (status/cg_status.c): show the ones of the others, and tell them what music plays here
XCVAR_DEF( cg_playerStatuses,					"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_shareMusic,						"0",					NULL,					CVAR_ARCHIVE )
// Admin login with a key (auth/cg_auth.c, the command amkey). The key file, by default admin_ed25519 in the home
// folder of the game. cg_authAgent 1: the keys of the ssh-agent are used too, at cg_authAgentPath if that is
// not where the system says, and only the one cg_authAgentKey names (a part of its comment or of its line).
// cg_authServers: where the player logs in by himself, kept by "amkey login" and "amkey forget".
XCVAR_DEF( cg_authAgent,						"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_authAgentKey,						"",						NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_authAgentPath,					"",						NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_authKeyFile,						"",						NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_authServers,						"",						NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_crosshairHealth,					"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_crosshairSize,					"24",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_crosshairX,						"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_crosshairY,						"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_currentSelectedPlayer,			"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_draw2D,							"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_draw3DIcons,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_drawAmmoWarning,					"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_drawCrosshair,					"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_drawCrosshairNames,				"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_drawEnemyInfo,					"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_drawFPS,							"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_drawFriend,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_drawGun,							"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_drawIcons,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_drawRadar,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_drawRewards,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_drawScores,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_drawSnapshot,						"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_drawStatus,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_drawTeamOverlay,					"0",					CG_TeamOverlayChange,	CVAR_ARCHIVE )
XCVAR_DEF( cg_drawTimer,						"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_drawVehLeadIndicator,				"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_dynamicCrosshair,					"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_dynamicCrosshairPrecision,		"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_debugAnim,						"0",					NULL,					CVAR_CHEAT )
XCVAR_DEF( cg_debugGun,							"0",					NULL,					CVAR_CHEAT )
XCVAR_DEF( cg_debugSaber,						"0",					NULL,					CVAR_CHEAT )
XCVAR_DEF( cg_debugPosition,					"0",					NULL,					CVAR_CHEAT )
XCVAR_DEF( cg_debugEvents,						"0",					NULL,					CVAR_CHEAT )
XCVAR_DEF( cg_duelHeadAngles,					"0",					NULL,					CVAR_NONE )
XCVAR_DEF( cg_dismember,						"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_deferPlayers,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_errorDecay,						"100",					NULL,					CVAR_NONE )
XCVAR_DEF( cg_fallingBob,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_footsteps,						"3",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_forceModel,						"0",					CG_ForceModelChange,	CVAR_ARCHIVE )
XCVAR_DEF( cg_fov,								"80",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_fovAspectAdjust,					"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_fovViewmodel,						"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_fovViewmodelAdjust,				"1",					NULL,					CVAR_ARCHIVE )
#if 0
XCVAR_DEF( cg_fpls,								"0",					NULL,					CVAR_NONE )
#endif
XCVAR_DEF( cg_g2TraceLod,						"2",					NULL,					CVAR_NONE )
XCVAR_DEF( cg_ghoul2Marks,						"16",					NULL,					CVAR_NONE )
XCVAR_DEF( cg_gunX,								"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_gunY,								"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_gunZ,								"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_hudFiles,							"ui/jahud.txt",			NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_hud,								"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_showLoading,						"0",					NULL,					CVAR_CHEAT )
XCVAR_DEF( cg_selectPlayers,					"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_selectOutline,					"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_jumpSounds,						"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_lagometer,						"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_marks,							"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_noPlayerAnims,					"0",					NULL,					CVAR_CHEAT )
XCVAR_DEF( cg_noPredict,						"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_noProjectileTrail,				"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_noTaunt,							"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_oldPainSounds,					"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_predictItems,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_renderToTextureFX,				"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_repeaterOrb,						"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_runPitch,							"0.002",				NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_runRoll,							"0.005",				NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_saberClientVisualCompensation,	"1",					NULL,					CVAR_NONE )
XCVAR_DEF( cg_saberContact,						"1",					NULL,					CVAR_NONE )
XCVAR_DEF( cg_saberDynamicMarks,				"0",					NULL,					CVAR_NONE )
XCVAR_DEF( cg_saberDynamicMarkTime,				"60000",				NULL,					CVAR_NONE )
XCVAR_DEF( cg_saberModelTraceEffect,			"0",					NULL,					CVAR_NONE )
XCVAR_DEF( cg_saberTrail,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_scoreboardBots,					"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_scoresPick,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_scorePlums,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_stereoSeparation,					"0.4",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_shadows,							"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_simpleItems,						"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_showMiss,							"0",					NULL,					CVAR_NONE )
XCVAR_DEF( cg_showVehBounds,					"0",					NULL,					CVAR_NONE )
XCVAR_DEF( cg_showVehMiss,						"0",					NULL,					CVAR_NONE )
XCVAR_DEF( cg_smoothCamera,						"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_smoothClients,					"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_snapshotTimeout,					"10",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_speedTrail,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_stats,							"0",					NULL,					CVAR_NONE )
XCVAR_DEF( cg_teamChatBeep,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_teamChatsOnly,					"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_thirdPerson,						"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_thirdPersonAlpha,					"1.0",					NULL,					CVAR_CHEAT )
XCVAR_DEF( cg_thirdPersonAngle,					"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_thirdPersonCameraDamp,			"0.3",					NULL,					CVAR_NONE )
XCVAR_DEF( cg_thirdPersonHorzOffset,			"0",					NULL,					CVAR_CHEAT )
XCVAR_DEF( cg_thirdPersonPitchOffset,			"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_thirdPersonRange,					"80",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_thirdPersonSpecialCam,			"0",					NULL,					CVAR_NONE )
XCVAR_DEF( cg_thirdPersonTargetDamp,			"0.5",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_thirdPersonVertOffset,			"16",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_timescaleFadeEnd,					"1",					NULL,					CVAR_NONE )
XCVAR_DEF( cg_timescaleFadeSpeed,				"0",					NULL,					CVAR_NONE )
XCVAR_DEF( cg_viewsize,							"100",					NULL,					CVAR_ARCHIVE )
// the boombox (boombox/cg_boombox.c): its boxes are seen and heard, and how loud their songs are
XCVAR_DEF( cg_boombox,							"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_boomboxVolume,					"0.6",					NULL,					CVAR_ARCHIVE )
// the voice chat (cg_voice.c)
XCVAR_DEF( cg_voice,							"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_voiceBackend,						"",						NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_voiceCapture,						"",						NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_voiceDrawTalkers,					"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_voiceMaster,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_voiceMasterHost,					"master.jka.vision",		NULL,					CVAR_ARCHIVE )
// The Vision Temple (focus/cg_focus.c): whom one meets there, through the masterserver. 1: everybody who is there.
// Any other text: those who have the same text. 0: nobody, and the masterserver is not asked.
XCVAR_DEF( cg_templeLink,						"1",					NULL,					CVAR_ARCHIVE )
// set by the entry Vision Temple of the main menu before it starts a game: that game is only there to stand behind
// the temple, the cgame goes there with its first frame and leaves the game when the temple is left
XCVAR_DEF( cg_templeStart,						"0",					NULL,					CVAR_TEMP )
XCVAR_DEF( cg_voiceMicGain,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_voiceMode,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_voiceMuffle,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_voiceOwnReverb,					"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_voicePlayback,					"",						NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_voiceRadioFX,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_voiceThreshold,					"0.02",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_voiceVolume,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cg_weaponBob,						"1",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( cl_paused,							"0",					NULL,					CVAR_ROM )
XCVAR_DEF( com_buildScript,						"0",					NULL,					CVAR_NONE )
XCVAR_DEF( com_cameraMode,						"0",					NULL,					CVAR_CHEAT )
XCVAR_DEF( com_optvehtrace,						"0",					NULL,					CVAR_NONE )
XCVAR_DEF( debugBB,								"0",					NULL,					CVAR_NONE )
XCVAR_DEF( forcepowers,							DEFAULT_FORCEPOWERS,	NULL,					CVAR_USERINFO|CVAR_ARCHIVE )
XCVAR_DEF( g_synchronousClients,				"0",					NULL,					CVAR_SYSTEMINFO )
XCVAR_DEF( model,								DEFAULT_MODEL,			NULL,					CVAR_USERINFO|CVAR_ARCHIVE )
XCVAR_DEF( pmove_fixed,							"0",					NULL,					CVAR_SYSTEMINFO )
XCVAR_DEF( pmove_float,							"0",					NULL,					CVAR_SYSTEMINFO )
XCVAR_DEF( pmove_msec,							"8",					NULL,					CVAR_SYSTEMINFO )
XCVAR_DEF( r_autoMap,							"0",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( r_autoMapX,							"496",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( r_autoMapY,							"32",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( r_autoMapW,							"128",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( r_autoMapH,							"128",					NULL,					CVAR_ARCHIVE )
XCVAR_DEF( sv_running,							"0",					CG_SVRunningChange,		CVAR_ROM )
XCVAR_DEF( teamoverlay,							"0",					NULL,					CVAR_ROM|CVAR_USERINFO )
XCVAR_DEF( timescale,							"1",					NULL,					CVAR_CHEAT )
XCVAR_DEF( ui_about_gametype,					"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_about_fraglimit,					"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_about_capturelimit,				"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_about_duellimit,					"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_about_timelimit,					"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_about_maxclients,					"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_about_dmflags,					"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_about_mapname,					"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_about_hostname,					"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_about_needpass,					"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_about_botminplayers,				"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_myteam,							"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_tm1_c0_cnt,						"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_tm1_c1_cnt,						"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_tm1_c2_cnt,						"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_tm1_c3_cnt,						"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_tm1_c4_cnt,						"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_tm1_c5_cnt,						"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_tm1_cnt,							"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_tm2_c0_cnt,						"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_tm2_c1_cnt,						"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_tm2_c2_cnt,						"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_tm2_c3_cnt,						"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_tm2_c4_cnt,						"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_tm2_c5_cnt,						"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_tm2_cnt,							"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
XCVAR_DEF( ui_tm3_cnt,							"0",					NULL,					CVAR_ROM|CVAR_INTERNAL )
#undef XCVAR_DEF
