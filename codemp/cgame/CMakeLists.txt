#============================================================================
# Copyright (C) 2013 - 2018, OpenJK contributors
# Copyright (C) 2026, VisionJK contributors
#
# This file is part of the OpenJK source code.
#
# OpenJK is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
#============================================================================

# Make sure the user is not executing this script directly
if(NOT InVisionJK)
	message(FATAL_ERROR "Use the top-level cmake script!")
endif(NOT InVisionJK)

set(MPCGameIncludeDirectories
	"${MPDir}"
	"${SharedDir}"
	"${GSLIncludeDirectory}"
	"${CMAKE_BINARY_DIR}/shared"
	)
if(WIN32)
	set(MPCGameLibraries "odbc32" "odbccp32") # what are these even?
endif(WIN32)
set(MPCGameDefines ${MPSharedDefines} "_CGAME" )

# The voice chat (cg_voice.c). It records and plays sound and has a codec, none of which the engine offers to
# a module, so the two libraries for that are built into the module: nothing a player has to install.
if(BuildJPVoice)
	# (the game module may have it already, for the boombox)
	if(NOT TARGET bundled_opus)
		add_subdirectory("${VisionJKLibDir}/opus" "${CMAKE_BINARY_DIR}/lib/opus")
	endif()
	set(MPCGameIncludeDirectories ${MPCGameIncludeDirectories} "${VisionJKLibDir}/opus/include" "${VisionJKLibDir}/miniaudio")
	set(MPCGameDefines ${MPCGameDefines} "JP_VOICE")
	set(MPCGameLibraries ${MPCGameLibraries} bundled_opus)
	if(WIN32)
		# the sockets. miniaudio loads the sound libraries of Windows itself when it runs, they are not linked
		set(MPCGameLibraries ${MPCGameLibraries} "ws2_32")
	elseif(EMSCRIPTEN)
		# Nothing to link: the sound devices and the WebSocket are the ones of the page, which the engine
		# (the main module) brings. See docs/japlus/voice.md, "In a browser".
	elseif(APPLE)
		set(MPCGameLibraries ${MPCGameLibraries} "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox" "pthread" "m")
	else()
		# miniaudio opens the sound library of the system (ALSA, PulseAudio) when it runs, it is not linked
		set(MPCGameLibraries ${MPCGameLibraries} "dl" "pthread" "m")
	endif()
endif(BuildJPVoice)
set(MPCGameGameFiles
	"${MPDir}/game/vehicles/AnimalNPC.c"
	"${MPDir}/game/bg/bg_g2_utils.c"
	"${MPDir}/game/bg/bg_misc.c"
	"${MPDir}/game/bg/bg_panimate.c"
	"${MPDir}/game/bg/bg_pmove.c"
	"${MPDir}/game/props/bg_props.c"
	"${MPDir}/game/voice/bg_voice.c"
	"${MPDir}/game/toys/bg_toys.c"
	"${MPDir}/game/toys/bg_toys.h"
	"${MPDir}/game/bg/bg_saber.c"
	"${MPDir}/game/bg/bg_saberLoad.c"
	"${MPDir}/game/bg/bg_saga.c"
	"${MPDir}/game/bg/bg_slidemove.c"
	"${MPDir}/game/bg/bg_vehicleLoad.c"
	"${MPDir}/game/bg/bg_weapons.c"
	"${MPDir}/game/vehicles/FighterNPC.c"
	"${MPDir}/game/vehicles/SpeederNPC.c"
	"${MPDir}/game/vehicles/WalkerNPC.c"
	"${MPDir}/game/anims.h"
	"${MPDir}/game/bg/bg_local.h"
	"${MPDir}/game/props/bg_props.h"
	"${MPDir}/game/props/bg_props_math.h"
	"${MPDir}/game/props/bg_tools.h"
	"${MPDir}/game/voice/bg_voice.h"
	"${MPDir}/game/bg/bg_net.h"
	"${MPDir}/game/bg/bg_public.h"
	"${MPDir}/game/bg/bg_saga.h"
	"${MPDir}/game/bg/bg_weapons.h"
	"${MPDir}/game/surfaceflags.h"
	)
source_group("game" FILES ${MPCGameGameFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameGameFiles})

set(MPCGameCgameFiles
	"${MPDir}/cgame/cg_consolecmds.c"
	"${MPDir}/cgame/cg_cvar.c"
	"${MPDir}/cgame/cg_draw.c"
	"${MPDir}/cgame/cg_drawtools.c"
	"${MPDir}/cgame/cg_effects.c"
	"${MPDir}/cgame/cg_ents.c"
	"${MPDir}/cgame/cg_event.c"
	"${MPDir}/cgame/cg_info.c"
	"${MPDir}/cgame/cg_light.c"
	"${MPDir}/cgame/cg_localents.c"
	"${MPDir}/cgame/cg_main.c"
	"${MPDir}/cgame/cg_japlus.c"
	"${MPDir}/cgame/cg_marks.c"
	"${MPDir}/cgame/cg_newDraw.c"
	"${MPDir}/cgame/cg_players.c"
	"${MPDir}/cgame/cg_playerstate.c"
	"${MPDir}/cgame/cg_predict.c"
	"${MPDir}/cgame/cg_saga.c"
	"${MPDir}/cgame/cg_scoreboard.c"
	"${MPDir}/cgame/cg_servercmds.c"
	"${MPDir}/cgame/cg_snapshot.c"
	"${MPDir}/cgame/cg_strafetrail.c"
	"${MPDir}/cgame/cg_spawn.c"
	"${MPDir}/cgame/cg_spatialdebug.c"
	"${MPDir}/cgame/cg_spatialmovers.c"
	"${MPDir}/cgame/cg_syscalls.c"
	"${MPDir}/cgame/cg_turret.c"
	"${MPDir}/cgame/cg_view.c"
	"${MPDir}/cgame/cg_weaponinit.c"
	"${MPDir}/cgame/cg_weapons.c"
	"${MPDir}/cgame/cg_window.c"
	"${MPDir}/cgame/cg_window.h"
	"${MPDir}/cgame/fx_blaster.c"
	"${MPDir}/cgame/fx_bowcaster.c"
	"${MPDir}/cgame/fx_bryarpistol.c"
	"${MPDir}/cgame/fx_demp2.c"
	"${MPDir}/cgame/fx_disruptor.c"
	"${MPDir}/cgame/fx_flechette.c"
	"${MPDir}/cgame/fx_force.c"
	"${MPDir}/cgame/fx_heavyrepeater.c"
	"${MPDir}/cgame/fx_rocketlauncher.c"
	"${MPDir}/cgame/animtable.h"
	"${MPDir}/cgame/cg_local.h"
	"${MPDir}/cgame/cg_public.h"
	"${MPDir}/cgame/cg_xcvar.h"
	"${MPDir}/cgame/fx_local.h"
	)
source_group("cgame" FILES ${MPCGameCgameFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameCgameFiles})

# the voice chat, its part in the game module is game/voice
set(MPCGameVoiceFiles
	"${MPDir}/cgame/voice/cg_voice.c"
	"${MPDir}/cgame/voice/cg_voice_audio.c"
	"${MPDir}/cgame/voice/cg_voice.h"
	)
if(BuildJPVoice AND NOT EMSCRIPTEN)
	# SHA-1: the name of the room at the masterserver, for servers without the game module of this tree
	set(MPCGameVoiceFiles ${MPCGameVoiceFiles}
		"${SharedDir}/web/web_sha1.c"
		"${SharedDir}/web/web_http.h"
		)
endif()
if(EMSCRIPTEN AND BuildJPVoice)
	# in a browser the datagrams go through a WebSocket
	set(MPCGameVoiceFiles ${MPCGameVoiceFiles} "${MPDir}/cgame/voice/cg_voice_web.c")
endif()
source_group("cgame/voice" FILES ${MPCGameVoiceFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameVoiceFiles})

# Admin login with a key: the command v_amkey, the key file, the ssh-agent. bg_auth.c is the part the game module
# shares, Monocypher signs. A browser has neither a key file nor an agent, there the command only says so.
set(MPCGameAuthFiles
	"${MPDir}/cgame/auth/cg_auth.c"
	"${MPDir}/cgame/auth/cg_auth_agent.c"
	"${MPDir}/cgame/auth/cg_auth_agent.h"
	"${MPDir}/game/auth/bg_auth.h"
	)
if(NOT EMSCRIPTEN)
	set(MPCGameAuthFiles ${MPCGameAuthFiles}
		"${MPDir}/game/auth/bg_auth.c"
		"${VisionJKLibDir}/monocypher/monocypher.c"
		"${VisionJKLibDir}/monocypher/monocypher.h"
		"${VisionJKLibDir}/monocypher/monocypher-ed25519.c"
		"${VisionJKLibDir}/monocypher/monocypher-ed25519.h"
		"${SharedDir}/web/web_base64.c"
		"${SharedDir}/web/web_sha256.c"
		"${SharedDir}/web/web_http.h"
		)
	set(MPCGameIncludeDirectories ${MPCGameIncludeDirectories} "${VisionJKLibDir}/monocypher")
endif()
# gcc warns of reads behind an array in the SHA-512 of Monocypher where it works out a call with 32 bytes: the
# branch it means is never taken with that length. The file is left as its authors ship it.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
	set_source_files_properties("${VisionJKLibDir}/monocypher/monocypher-ed25519.c" PROPERTIES COMPILE_OPTIONS "-Wno-array-bounds")
endif()
source_group("cgame/auth" FILES ${MPCGameAuthFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameAuthFiles})

# the chat box: history, tabs, marking and copying, links, and its own line to type in
set(MPCGameChatFiles
	"${MPDir}/cgame/chat/cg_chat.c"
	)
source_group("cgame/chat" FILES ${MPCGameChatFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameChatFiles})

# chat bubbles above the heads, in the 3D scene
set(MPCGameBubbleFiles
	"${MPDir}/cgame/bubble/cg_bubble.c"
	)
source_group("cgame/bubble" FILES ${MPCGameBubbleFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameBubbleFiles})

# away and music above the heads, the server part is game/status
set(MPCGameStatusFiles
	"${MPDir}/cgame/status/cg_peers.c"
	"${MPDir}/cgame/status/cg_status.c"
	"${MPDir}/cgame/status/cg_status_music.c"
	)
source_group("cgame/status" FILES ${MPCGameStatusFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameStatusFiles})

# players that can be clicked while the mouse is free: the outline, the card
set(MPCGameSelectFiles
	"${MPDir}/cgame/select/cg_select.c"
	)
source_group("cgame/select" FILES ${MPCGameSelectFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameSelectFiles})

# the quick wheel (+v_radial), a mode of the pointer
set(MPCGameRadialFiles
	"${MPDir}/cgame/radial/cg_radial.c"
	)
source_group("cgame/radial" FILES ${MPCGameRadialFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameRadialFiles})

# the command menu (v_commandmenu), a mode of the pointer too
set(MPCGameCmdMenuFiles
	"${MPDir}/cgame/cmdmenu/cg_cmdmenu.c"
	)
source_group("cgame/cmdmenu" FILES ${MPCGameCmdMenuFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameCmdMenuFiles})

# the toolgun of the sandbox (amsandbox): its tool as cvars for the server, the line under the crosshair
set(MPCGameSandboxFiles
	"${MPDir}/cgame/sandbox/cg_tools.c"
	"${MPDir}/cgame/sandbox/cg_tools_ui.c"
	)
source_group("cgame/sandbox" FILES ${MPCGameSandboxFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameSandboxFiles})

# the window of the model store of the site (v_modelbrowser), the list as the engine reads it (client/cl_models.cpp)
set(MPCGameModelStoreFiles
	"${MPDir}/cgame/modelstore/cg_modelstore.c"
	"${SharedDir}/web/web_models.c"
	"${SharedDir}/web/web_models.h"
	"${SharedDir}/web/web_http.h"
	)
source_group("cgame/modelstore" FILES ${MPCGameModelStoreFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameModelStoreFiles})

# the toys of issue #9 that only players of this tree see: the snowball, the water gun
set(MPCGameToysFiles
	"${MPDir}/cgame/toys/cg_toys.c"
	"${MPDir}/cgame/toys/cg_toys.h"
	"${MPDir}/cgame/toys/cg_water.c"
	"${MPDir}/cgame/toys/cg_rccar.c"
	"${MPDir}/cgame/toys/cg_emote.c"
	"${MPDir}/cgame/toys/cg_spray.c"
	)
source_group("cgame/toys" FILES ${MPCGameToysFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameToysFiles})

# the boombox: boxes that play songs, which the server sends over the relay of the voice chat
set(MPCGameBoomboxFiles
	"${MPDir}/cgame/boombox/cg_boombox.c"
	"${MPDir}/cgame/boombox/cg_boombox_ui.c"
	"${MPDir}/cgame/boombox/cg_boombox.h"
	"${MPDir}/game/boombox/bg_boombox.c"
	"${MPDir}/game/boombox/bg_boombox.h"
	)
source_group("cgame/boombox" FILES ${MPCGameBoomboxFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameBoomboxFiles})

# the numbers of the game for the items of a menu file that show one: what the HUD of this tree is made of
set(MPCGameHudFiles
	"${MPDir}/cgame/hud/cg_hud.c"
	"${MPDir}/cgame/hud/cg_killfeed.c"
	"${MPDir}/cgame/hud/cg_notify.c"
	"${MPDir}/cgame/hud/cg_movehud.c"
	"${MPDir}/cgame/hud/cg_lead.c"
	"${MPDir}/cgame/hud/cg_spechud.c"
	"${MPDir}/cgame/look/cg_look.c"
	"${MPDir}/cgame/hud/cg_scores.c"
	)
source_group("cgame/hud" FILES ${MPCGameHudFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameHudFiles})

# the scope of the disruptor: the lens of the renderer, the zoom of the wheel, the motion view
set(MPCGameScopeFiles
	"${MPDir}/cgame/scope/cg_scope.c"
	)
source_group("cgame/scope" FILES ${MPCGameScopeFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameScopeFiles})

# the screen effects: the renderer bends and colours the view of the level
set(MPCGameScreenFxFiles
	"${MPDir}/cgame/screenfx/cg_screenfx.c"
	)
source_group("cgame/screenfx" FILES ${MPCGameScreenFxFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameScreenFxFiles})

# the photo mode
set(MPCGamePhotoFiles
	"${MPDir}/cgame/photo/cg_photo.c"
	)
source_group("cgame/photo" FILES ${MPCGamePhotoFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGamePhotoFiles})

# the menu in the level: shoulder camera, panel, blur and muffled sound while a menu is open
set(MPCGameMenu3DFiles
	"${MPDir}/cgame/menu3d/cg_menu3d.c"
	)
source_group("cgame/menu3d" FILES ${MPCGameMenu3DFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameMenu3DFiles})

set(MPCGameCommonFiles
	"${MPDir}/qcommon/q_shared.c"
	"${MPDir}/qcommon/disablewarnings.h"
	"${MPDir}/qcommon/q_shared.h"
	"${MPDir}/qcommon/qfiles.h"
	"${MPDir}/qcommon/tags.h"

	${SharedCommonFiles}
	)
source_group("common" FILES ${MPCGameCommonFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameCommonFiles})

set(MPCGameCommonSafeFiles
	${SharedCommonSafeFiles}
	)
source_group("common/safe" FILES ${MPCGameCommonSafeFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameCommonSafeFiles})

set(MPCGameUiFiles
	"${MPDir}/ui/ui_shared.c"
	"${MPDir}/ui/ui_skin.c"
	"${MPDir}/ui/ui_saberfx.c"
	"${MPDir}/ui/ui_music.c"
	"${MPDir}/ui/keycodes.h"
	"${MPDir}/ui/menudef.h"
	"${MPDir}/ui/ui_shared.h"
	"${MPDir}/ui/ui_skin.h"
	"${MPDir}/ui/ui_saberfx.h"
	"${MPDir}/ui/ui_music.h"
	)
source_group("ui" FILES ${MPCGameUiFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameUiFiles})

set(MPCGameRendererFiles
	"${MPDir}/rd-common/tr_types.h"
	)
source_group("rd-common" FILES ${MPCGameRendererFiles})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameRendererFiles})

set(MPCGameGFiles
	"${MPDir}/ghoul2/G2.h"
	)
source_group("ghoul2" FILES ${MPCGameG2Files})
set(MPCGameFiles ${MPCGameFiles} ${MPCGameG2Files})

add_library(${MPCGame} SHARED ${MPCGameFiles})

if(NOT MSVC)
	# remove "lib" prefix for .so/.dylib files
	set_target_properties(${MPCGame} PROPERTIES PREFIX "")
endif()

if(EMSCRIPTEN)
	include(WasmModule)
	vjk_wasm_side_module(${MPCGame})
endif()

if(MakeApplicationBundles AND BuildMPEngine)
	install(TARGETS ${MPCGame}
		LIBRARY
		DESTINATION "${JKAInstallDir}/${MPEngine}.app/Contents/MacOS/VisionJK"
		COMPONENT ${JKAMPCoreComponent})
	install(TARGETS ${MPCGame}
		LIBRARY
		DESTINATION "${JKAInstallDir}/${MPEngine}.app/Contents/MacOS/base"
		COMPONENT ${JKAMPCoreComponent})
elseif(WIN32)
	# into the mod directory, next to the asset archives and the configs
	install(TARGETS ${MPCGame}
		RUNTIME
		DESTINATION "${JPModInstallDir}"
		COMPONENT ${JKAMPCoreComponent})

	# and into base: the engine looks there when fs_game is not set or the mod directory has no module
	install(TARGETS ${MPCGame}
		RUNTIME
		DESTINATION "${JKAInstallDir}/base"
		COMPONENT ${JKAMPCoreComponent})
elseif(NOT EMSCRIPTEN)
	install(TARGETS ${MPCGame}
		LIBRARY
		DESTINATION "${JPModInstallDir}"
		COMPONENT ${JKAMPCoreComponent})
	install(TARGETS ${MPCGame}
		LIBRARY
		DESTINATION "${JKAInstallDir}/base"
		COMPONENT ${JKAMPCoreComponent})
endif()

set_target_properties(${MPCGame} PROPERTIES COMPILE_DEFINITIONS "${MPCGameDefines}")

# Hide symbols not explicitly marked public (not for BuildDevHotpatch, see the main CMakeLists.txt).
set_property(TARGET ${MPCGame} APPEND PROPERTY COMPILE_OPTIONS ${VISIONJK_MODULE_FLAGS})

set_target_properties(${MPCGame} PROPERTIES INCLUDE_DIRECTORIES "${MPCGameIncludeDirectories}")
set_target_properties(${MPCGame} PROPERTIES PROJECT_LABEL "MP Client Game Library")
# no libraries used
if(MPCGameLibraries)
	target_link_libraries(${MPCGame} ${MPCGameLibraries})
endif(MPCGameLibraries)
