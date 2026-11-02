/*
===========================================================================
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

// cvar_renames.h -- the cvars and commands VisionJK added, with the names they had until 2026-10-17. Since then a
// name of this tree begins with v_ (the client: cgame, ui, engine client, sound, renderer, its console commands) or
// vs_ (the server: game module, dedicated server, masterserver, its console commands, and the commands client and
// server send each other). Stock OpenJK and JA+ names keep theirs.
//
// CVAR_RENAME( old, new ): a value stored or set under the old name goes to the new one (qcommon/cvar/cvar.cpp).
// CMD_RENAME( old, new ): a bind with the old command gets the new one (client/cl_keys.cpp). The commands between
// client and server (WIRE_RENAME) have nothing to take over: an old version does not understand a new one.
//
// Included with the macros it needs defined; one that is not defined stands for nothing.

#ifndef CVAR_RENAME
#define CVAR_RENAME( from, to )
#define CVAR_RENAME_LOCAL
#endif
#ifndef CMD_RENAME
#define CMD_RENAME( from, to )
#define CMD_RENAME_LOCAL
#endif
#ifndef WIRE_RENAME
#define WIRE_RENAME( from, to )
#define WIRE_RENAME_LOCAL
#endif

// cvars of the client
CVAR_RENAME( "cg_authAgent", "v_authAgent" )
CVAR_RENAME( "cg_authAgentKey", "v_authAgentKey" )
CVAR_RENAME( "cg_authAgentPath", "v_authAgentPath" )
CVAR_RENAME( "cg_authKeyFile", "v_authKeyFile" )
CVAR_RENAME( "cg_authServers", "v_authServers" )
CVAR_RENAME( "cg_boombox", "v_boomboxes" )
CVAR_RENAME( "cg_boomboxVolume", "v_boomboxVolume" )
CVAR_RENAME( "cg_bubbleRange", "v_bubbleRange" )
CVAR_RENAME( "cg_catchQuiet", "v_catchQuiet" )
CVAR_RENAME( "cg_chatBoxRows", "v_chatBoxRows" )
CVAR_RENAME( "cg_chatBoxScale", "v_chatBoxScale" )
CVAR_RENAME( "cg_chatBoxWidth", "v_chatBoxWidth" )
CVAR_RENAME( "cg_chatBubbles", "v_chatBubbles" )
CVAR_RENAME( "cg_chatHandler", "v_chatHandler" )
CVAR_RENAME( "cg_chatInput", "v_chatInput" )
CVAR_RENAME( "cg_hud", "v_hud" )
CVAR_RENAME( "cg_immersiveMenu", "v_immersiveMenu" )
CVAR_RENAME( "cg_playerStatuses", "v_playerStatuses" )
CVAR_RENAME( "cg_scoresPick", "v_scoresPick" )
CVAR_RENAME( "cg_selectOutline", "v_selectOutline" )
CVAR_RENAME( "cg_selectPlayers", "v_selectPlayers" )
CVAR_RENAME( "cg_shareMusic", "v_shareMusic" )
CVAR_RENAME( "cg_showLoading", "v_showLoading" )
CVAR_RENAME( "cg_voice", "v_voice" )
CVAR_RENAME( "cg_voiceBackend", "v_voiceBackend" )
CVAR_RENAME( "cg_voiceCapture", "v_voiceCapture" )
CVAR_RENAME( "cg_voiceDrawTalkers", "v_voiceDrawTalkers" )
CVAR_RENAME( "cg_voiceMaster", "v_voiceMaster" )
CVAR_RENAME( "cg_voiceMasterHost", "v_master" )
CVAR_RENAME( "cg_voiceMicGain", "v_voiceMicGain" )
CVAR_RENAME( "cg_voiceMode", "v_voiceMode" )
CVAR_RENAME( "cg_voiceMuffle", "v_voiceMuffle" )
CVAR_RENAME( "cg_voiceOwnReverb", "v_voiceOwnReverb" )
CVAR_RENAME( "cg_voicePlayback", "v_voicePlayback" )
CVAR_RENAME( "cg_voiceRadioFX", "v_voiceRadioFX" )
CVAR_RENAME( "cg_voiceThreshold", "v_voiceThreshold" )
CVAR_RENAME( "cg_voiceVolume", "v_voiceVolume" )
CVAR_RENAME( "cjp_caps", "v_caps" )
CVAR_RENAME( "cjp_vision", "v_vision" )
CVAR_RENAME( "cl_cgameChat", "v_cgameChat" )
CVAR_RENAME( "cl_cgameDoF", "v_cgameDoF" )
CVAR_RENAME( "cl_cgameScene", "v_cgameScene" )
CVAR_RENAME( "cl_cgameUIPlane", "v_cgameUIPlane" )
CVAR_RENAME( "cl_ids", "v_ids" )
CVAR_RENAME( "cl_uiPlane", "v_uiPlane" )
CVAR_RENAME( "cl_uiSkin", "v_uiSkin" )
CVAR_RENAME( "cl_updateCheck", "v_updateCheck" )
CVAR_RENAME( "cl_updateState", "v_updateState" )
CVAR_RENAME( "cl_updateText", "v_updateText" )
CVAR_RENAME( "cl_updateURL", "v_updateURL" )
CVAR_RENAME( "cl_utf8", "v_utf8" )
CVAR_RENAME( "cl_utf8Text", "v_utf8Text" )
CVAR_RENAME( "com_hitchLog", "v_hitchLog" )
CVAR_RENAME( "com_loadReport", "v_loadReport" )
CVAR_RENAME( "net_wsHost", "v_wsHost" )
CVAR_RENAME( "r_2dAspect", "v_2dAspect" )
CVAR_RENAME( "r_2dBias", "v_2dBias" )
CVAR_RENAME( "r_glesDebug", "v_glesDebug" )
CVAR_RENAME( "r_menuBlur", "v_menuBlur" )
CVAR_RENAME( "r_vkDebug", "v_vkDebug" )
CVAR_RENAME( "r_vkMerge", "v_vkMerge" )
CVAR_RENAME( "r_vkStatic", "v_vkStatic" )
CVAR_RENAME( "r_vkTexCache", "v_vkTexCache" )
CVAR_RENAME( "s_muffle", "v_muffle" )
CVAR_RENAME( "s_spatial", "v_spatial" )
CVAR_RENAME( "s_spatialAhead", "v_spatialAhead" )
CVAR_RENAME( "s_spatialBounces", "v_spatialBounces" )
CVAR_RENAME( "s_spatialBoxes", "v_spatialBoxes" )
CVAR_RENAME( "s_spatialBudget", "v_spatialBudget" )
CVAR_RENAME( "s_spatialHrtf", "v_spatialHrtf" )
CVAR_RENAME( "s_spatialOwnVoice", "v_spatialOwnVoice" )
CVAR_RENAME( "s_spatialPathLoss", "v_spatialPathLoss" )
CVAR_RENAME( "s_spatialPaths", "v_spatialPaths" )
CVAR_RENAME( "s_spatialRays", "v_spatialRays" )
CVAR_RENAME( "s_spatialReflections", "v_spatialReflections" )
CVAR_RENAME( "s_spatialReverb", "v_spatialReverb" )
CVAR_RENAME( "s_spatialShock", "v_spatialShock" )
CVAR_RENAME( "s_spatialVoice", "v_spatialVoice" )
CVAR_RENAME( "ui_browserShowBotOnly", "v_browserShowBotOnly" )
CVAR_RENAME( "ui_classic", "v_classic" )
CVAR_RENAME( "ui_cursorKeyOffered", "v_cursorKeyOffered" )
CVAR_RENAME( "ui_openBox", "v_openBox" )
CVAR_RENAME( "ui_s_spatialQuality", "v_spatialQuality" )

// cvars of the server
CVAR_RENAME( "cm_clipOff", "vs_clipRanges" )
CVAR_RENAME( "g_afkTime", "vs_afkTime" )
CVAR_RENAME( "g_boombox", "vs_boombox" )
CVAR_RENAME( "g_boomboxFfmpeg", "vs_boomboxFfmpeg" )
CVAR_RENAME( "g_boomboxMax", "vs_boomboxMax" )
CVAR_RENAME( "g_boomboxMaxSeconds", "vs_boomboxMaxSeconds" )
CVAR_RENAME( "g_boomboxQueue", "vs_boomboxQueue" )
CVAR_RENAME( "g_boomboxRange", "vs_boomboxRange" )
CVAR_RENAME( "g_boomboxYtdlp", "vs_boomboxYtdlp" )
CVAR_RENAME( "g_boomboxYtdlpArgs", "vs_boomboxYtdlpArgs" )
CVAR_RENAME( "g_grabHugDist", "vs_grabHugDist" )
CVAR_RENAME( "g_logClientCmds", "vs_logClientCmds" )
CVAR_RENAME( "g_visionCaps", "vs_caps" )
CVAR_RENAME( "g_voice", "vs_voice" )
CVAR_RENAME( "g_voicePort", "vs_voicePort" )
CVAR_RENAME( "g_voiceRadio", "vs_voiceRadio" )
CVAR_RENAME( "g_voiceRange", "vs_voiceRange" )
CVAR_RENAME( "g_voiceRelay", "vs_voiceRelay" )
CVAR_RENAME( "g_voiceShared", "vs_voiceSharedGame" )
CVAR_RENAME( "g_voiceTeamOnly", "vs_voiceTeamOnly" )
CVAR_RENAME( "g_webBasicAuth", "vs_webBasicAuth" )
CVAR_RENAME( "g_webBind", "vs_webBind" )
CVAR_RENAME( "g_webPassword", "vs_webPassword" )
CVAR_RENAME( "g_webPort", "vs_webPort" )
CVAR_RENAME( "g_webProxy", "vs_webProxy" )
CVAR_RENAME( "g_webRoot", "vs_webRoot" )
CVAR_RENAME( "jp_adminPasswords", "vs_adminPasswords" )
CVAR_RENAME( "jp_authHosts", "vs_authHosts" )
CVAR_RENAME( "sv_antiWallhack", "vs_antiWallhack" )
CVAR_RENAME( "sv_clipOff", "vs_clipOff" )
CVAR_RENAME( "sv_ids", "vs_ids" )
CVAR_RENAME( "sv_masterBoxes", "vs_masterBoxes" )
CVAR_RENAME( "sv_masterKbps", "vs_masterKbps" )
CVAR_RENAME( "sv_voiceShared", "vs_voiceShared" )
CVAR_RENAME( "sv_webBind", "vs_clientWebBind" )
CVAR_RENAME( "sv_webPort", "vs_clientWebPort" )
CVAR_RENAME( "sv_webRoot", "vs_clientWebRoot" )

// console commands of the client
CMD_RENAME( "+cursor", "+v_cursor" )
CMD_RENAME( "+voice", "+v_voice" )
CMD_RENAME( "+voiceclan", "+v_voiceclan" )
CMD_RENAME( "-cursor", "-v_cursor" )
CMD_RENAME( "-voice", "-v_voice" )
CMD_RENAME( "-voiceclan", "-v_voiceclan" )
CMD_RENAME( "amkey", "v_amkey" )
CMD_RENAME( "boombox", "v_boombox" )
CMD_RENAME( "cg_chatdrag", "v_chatDrag" )
CMD_RENAME( "cg_chatopen", "v_chatOpen" )
CMD_RENAME( "cg_key", "v_cgKey" )
CMD_RENAME( "peers", "v_peers" )
CMD_RENAME( "s_spatialCapture", "v_spatialCapture" )
CMD_RENAME( "s_spatialInfo", "v_spatialInfo" )
CMD_RENAME( "scores_pick", "v_scores_pick" )
CMD_RENAME( "status_test", "v_status_test" )
CMD_RENAME( "statuses", "v_statuses" )
CMD_RENAME( "ui_key", "v_uiKey" )
CMD_RENAME( "ui_mouse", "v_uiMouse" )
CMD_RENAME( "ui_page", "v_uiPage" )
CMD_RENAME( "update_cancel", "v_update_cancel" )
CMD_RENAME( "update_check", "v_update_check" )
CMD_RENAME( "voice_devices", "v_voice_devices" )
CMD_RENAME( "voice_mute", "v_voice_mute" )
CMD_RENAME( "voice_status", "v_voice_status" )
CMD_RENAME( "voice_test", "v_voice_test" )
CMD_RENAME( "voice_unmute", "v_voice_unmute" )

// console commands of the server
CMD_RENAME( "boombox", "vs_boxes" )
CMD_RENAME( "disorient", "vs_disorient" )
CMD_RENAME( "flatulence", "vs_flatulence" )
CMD_RENAME( "idsstatus", "vs_idsstatus" )
CMD_RENAME( "idsunblock", "vs_idsunblock" )
CMD_RENAME( "props", "vs_props" )
CMD_RENAME( "schizo", "vs_schizo" )
CMD_RENAME( "voicestatus", "vs_voicestatus" )

// What came from TaystJK (feature/taystjk-integration): v_ for one evening of 2026-10-17, tay_ since. The
// console script commands had TaystJK's own names (delay, waitf, ifCvar, strSub ...) until then.
CVAR_RENAME( "v_cape", "tay_cape" )
CVAR_RENAME( "v_chatEmoji", "tay_chatEmoji" )
CVAR_RENAME( "v_chatLog", "tay_chatLog" )
CVAR_RENAME( "v_consoleClock", "tay_consoleClock" )
CVAR_RENAME( "v_crosshairColor", "tay_crosshairColor" )
CVAR_RENAME( "v_crosshairNamesOpacity", "tay_crosshairNamesOpacity" )
CVAR_RENAME( "v_disruptorBeam", "tay_disruptorBeam" )
CVAR_RENAME( "v_disruptorColor", "tay_disruptorColor" )
CVAR_RENAME( "v_forceAllyModel", "tay_forceAllyModel" )
CVAR_RENAME( "v_forceEnemyModel", "tay_forceEnemyModel" )
CVAR_RENAME( "v_hat", "tay_hat" )
CVAR_RENAME( "v_jk2Effects", "tay_jk2Effects" )
CVAR_RENAME( "v_killfeed", "tay_killfeed" )
CVAR_RENAME( "v_killfeedSize", "tay_killfeedSize" )
CVAR_RENAME( "v_killfeedTime", "tay_killfeedTime" )
CVAR_RENAME( "v_killfeedX", "tay_killfeedX" )
CVAR_RENAME( "v_killfeedY", "tay_killfeedY" )
CVAR_RENAME( "v_pitchHud", "tay_pitchHud" )
CVAR_RENAME( "v_pitchHudAngle", "tay_pitchHudAngle" )
CVAR_RENAME( "v_pitchHudColor", "tay_pitchHudColor" )
CVAR_RENAME( "v_saberStaffColors", "tay_saberStaffColors" )
CVAR_RENAME( "v_saberTeamColors", "tay_saberTeamColors" )
CVAR_RENAME( "v_seasonalCosmetics", "tay_seasonalCosmetics" )
CVAR_RENAME( "v_showCosmetics", "tay_showCosmetics" )
CVAR_RENAME( "v_snapHud", "tay_snapHud" )
CVAR_RENAME( "v_snapHudColor1", "tay_snapHudColor1" )
CVAR_RENAME( "v_snapHudColor2", "tay_snapHudColor2" )
CVAR_RENAME( "v_snapHudY", "tay_snapHudY" )
CVAR_RENAME( "v_speedometer", "tay_speedometer" )
CVAR_RENAME( "v_speedometerJumps", "tay_speedometerJumps" )
CVAR_RENAME( "v_speedometerSize", "tay_speedometerSize" )
CVAR_RENAME( "v_speedometerX", "tay_speedometerX" )
CVAR_RENAME( "v_speedometerY", "tay_speedometerY" )
CVAR_RENAME( "v_strafeHelper", "tay_strafeHelper" )
CVAR_RENAME( "v_strafeHelperColor", "tay_strafeHelperColor" )
CVAR_RENAME( "v_strafeHelperY", "tay_strafeHelperY" )
CVAR_RENAME( "v_strafeTrail", "tay_strafeTrail" )
CVAR_RENAME( "v_strafeTrailLife", "tay_strafeTrailLife" )
CVAR_RENAME( "v_teamOverlayBar", "tay_teamOverlayBar" )
CVAR_RENAME( "v_teamOverlayMaxHP", "tay_teamOverlayMaxHP" )
CVAR_RENAME( "v_teamOverlayScale", "tay_teamOverlayScale" )
CVAR_RENAME( "v_teamOverlayWeapons", "tay_teamOverlayWeapons" )
// the address of the masterserver, one cvar for all since 2026-11-02 (client/cl_master.cpp)
CVAR_RENAME( "v_voiceMasterHost", "v_master" )
CVAR_RENAME( "v_idsMaster", "v_master" )
CMD_RENAME( "delay", "tay_delay" )
CMD_RENAME( "delaycancel", "tay_delaycancel" )
CMD_RENAME( "ifCvar", "tay_ifCvar" )
CMD_RENAME( "strSub", "tay_strSub" )
CMD_RENAME( "v_cosmetics", "tay_cosmetics" )
CMD_RENAME( "v_emoji", "tay_emoji" )
CMD_RENAME( "waitf", "tay_waitf" )
CMD_RENAME( "waitfcancel", "tay_waitfcancel" )


// commands between client and server
WIRE_RENAME( "boombox", "vs_boxaction" )
WIRE_RENAME( "jpauth", "vs_auth" )
WIRE_RENAME( "jpbox", "vs_box" )
WIRE_RENAME( "jpstatus", "vs_status" )
WIRE_RENAME( "jpvoice", "vs_voiceLink" )

#ifdef CVAR_RENAME_LOCAL
#undef CVAR_RENAME
#undef CVAR_RENAME_LOCAL
#endif
#ifdef CMD_RENAME_LOCAL
#undef CMD_RENAME
#undef CMD_RENAME_LOCAL
#endif
#ifdef WIRE_RENAME_LOCAL
#undef WIRE_RENAME
#undef WIRE_RENAME_LOCAL
#endif
