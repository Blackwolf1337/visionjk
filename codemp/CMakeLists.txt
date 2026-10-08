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

set(MPSharedDefines ${SharedDefines})

#    Add Game Project
if(BuildMPGame)
	add_subdirectory("${MPDir}/game")
endif(BuildMPGame)

#    Add CGame Project
if(BuildMPCGame)
	add_subdirectory("${MPDir}/cgame")
endif(BuildMPCGame)

#    Add UI Project
if(BuildMPUI)
	add_subdirectory("${MPDir}/ui")
endif(BuildMPUI)

#	 Add Vanilla JKA Renderer Project
if(BuildMPRdVanilla)
	add_subdirectory("${MPDir}/rd-vanilla")
endif(BuildMPRdVanilla)

if(BuildMPRdVulkanVision)
	add_subdirectory("${MPDir}/rd-vulkan-vision")
endif(BuildMPRdVulkanVision)

#    Common things between Engine and Dedicated Server
if(BuildMPEngine OR BuildMPDed)

	#    Botlib
	# the files could arguably just be put into the engine and dedicated projects without having a library for it.
	set(MPBotlibDefines ${SharedDefines} "BOTLIB")
	set(MPBotlibIncludeDirectories
		${SharedDir}
		${MPDir}
		"${GSLIncludeDirectory}"
		)
	set(MPBotlibBotlibFiles
		"${MPDir}/botlib/aasfile.h"
		"${MPDir}/botlib/be_aas.h"
		"${MPDir}/botlib/be_aas_bsp.h"
		"${MPDir}/botlib/be_aas_cluster.h"
		"${MPDir}/botlib/be_aas_debug.h"
		"${MPDir}/botlib/be_aas_def.h"
		"${MPDir}/botlib/be_aas_entity.h"
		"${MPDir}/botlib/be_aas_file.h"
		"${MPDir}/botlib/be_aas_funcs.h"
		"${MPDir}/botlib/be_aas_main.h"
		"${MPDir}/botlib/be_aas_move.h"
		"${MPDir}/botlib/be_aas_optimize.h"
		"${MPDir}/botlib/be_aas_reach.h"
		"${MPDir}/botlib/be_aas_route.h"
		"${MPDir}/botlib/be_aas_routealt.h"
		"${MPDir}/botlib/be_aas_sample.h"
		"${MPDir}/botlib/be_ai_char.h"
		"${MPDir}/botlib/be_ai_chat.h"
		"${MPDir}/botlib/be_ai_gen.h"
		"${MPDir}/botlib/be_ai_goal.h"
		"${MPDir}/botlib/be_ai_move.h"
		"${MPDir}/botlib/be_ai_weap.h"
		"${MPDir}/botlib/be_ai_weight.h"
		"${MPDir}/botlib/be_ea.h"
		"${MPDir}/botlib/be_interface.h"
		"${MPDir}/botlib/botlib.h"
		"${MPDir}/botlib/l_crc.h"
		"${MPDir}/botlib/l_libvar.h"
		"${MPDir}/botlib/l_log.h"
		"${MPDir}/botlib/l_memory.h"
		"${MPDir}/botlib/l_precomp.h"
		"${MPDir}/botlib/l_script.h"
		"${MPDir}/botlib/l_struct.h"
		"${MPDir}/botlib/l_utils.h"

		"${MPDir}/botlib/be_aas_bspq3.cpp"
		"${MPDir}/botlib/be_aas_cluster.cpp"
		"${MPDir}/botlib/be_aas_debug.cpp"
		"${MPDir}/botlib/be_aas_entity.cpp"
		"${MPDir}/botlib/be_aas_file.cpp"
		"${MPDir}/botlib/be_aas_main.cpp"
		"${MPDir}/botlib/be_aas_move.cpp"
		"${MPDir}/botlib/be_aas_optimize.cpp"
		"${MPDir}/botlib/be_aas_reach.cpp"
		"${MPDir}/botlib/be_aas_route.cpp"
		"${MPDir}/botlib/be_aas_routealt.cpp"
		"${MPDir}/botlib/be_aas_sample.cpp"
		"${MPDir}/botlib/be_ai_char.cpp"
		"${MPDir}/botlib/be_ai_chat.cpp"
		"${MPDir}/botlib/be_ai_gen.cpp"
		"${MPDir}/botlib/be_ai_goal.cpp"
		"${MPDir}/botlib/be_ai_move.cpp"
		"${MPDir}/botlib/be_ai_weap.cpp"
		"${MPDir}/botlib/be_ai_weight.cpp"
		"${MPDir}/botlib/be_ea.cpp"
		"${MPDir}/botlib/be_interface.cpp"
		"${MPDir}/botlib/l_crc.cpp"
		"${MPDir}/botlib/l_libvar.cpp"
		"${MPDir}/botlib/l_log.cpp"
		"${MPDir}/botlib/l_memory.cpp"
		"${MPDir}/botlib/l_precomp.cpp"
		"${MPDir}/botlib/l_script.cpp"
		"${MPDir}/botlib/l_struct.cpp"
		)
	source_group("botlib" FILES ${MPBotlibBotlibFiles})
	set(MPBotlibFiles ${MPBotlibFiles} ${MPBotlibBotlibFiles})

	set(MPBotlibGameFiles
		"${MPDir}/game/surfaceflags.h"
		)
	source_group("game" FILES ${MPBotlibGameFiles})
	set(MPBotlibFiles ${MPBotlibFiles} ${MPBotlibGameFiles})

	set(MPBotlibCommonFiles
		"${MPDir}/qcommon/cm_public.h"
		"${MPDir}/qcommon/disablewarnings.h"
		"${MPDir}/qcommon/q_shared.h"
		"${MPDir}/qcommon/qcommon.h"
		"${MPDir}/qcommon/qfiles.h"
		"${MPDir}/qcommon/tags.h"

		${SharedCommonFiles}
		)
	source_group("common" FILES ${MPBotlibCommonFiles})
	set(MPBotlibFiles ${MPBotlibFiles} ${MPBotlibCommonFiles})

	set(MPBotlibCommonSafeFiles
		"${SharedDir}/qcommon/safe/gsl.h"
		"${SharedDir}/qcommon/safe/string.cpp"
		"${SharedDir}/qcommon/safe/string.h"
		"${SharedDir}/qcommon/safe/sscanf.h"
		)
	source_group("common/safe" FILES ${MPBotlibCommonSafeFiles})
	set(MPBotlibFiles ${MPBotlibFiles} ${MPBotlibCommonSafeFiles})

	set(MPBotlibServerFiles
		"${MPDir}/server/server.h"
		)
	source_group("server" FILES ${MPBotlibServerFiles})
	set(MPBotlibFiles ${MPBotlibFiles} ${MPBotlibServerFiles})

	add_library(${MPBotLib} STATIC ${MPBotlibFiles})

	# Hide symbols not explicitly marked public.
	set_property(TARGET ${MPBotLib} APPEND PROPERTY COMPILE_OPTIONS ${VISIONJK_VISIBILITY_FLAGS})

	set_target_properties(${MPBotLib} PROPERTIES COMPILE_DEFINITIONS "${MPBotlibDefines}")
	set_target_properties(${MPBotLib} PROPERTIES INCLUDE_DIRECTORIES "${MPBotlibIncludeDirectories}")
	set_target_properties(${MPBotLib} PROPERTIES PROJECT_LABEL "Bot Library")

	#    Common files/libraries/defines of both Engine and Dedicated Server

	# libraries: Botlib
	set(MPEngineAndDedLibraries ${MPBotLib})
	# Platform-specific libraries
	if(WIN32)
		set(MPEngineAndDedLibraries ${MPEngineAndDedLibraries} "winmm" "wsock32")
	endif(WIN32)

	# Include directories
	set(MPEngineAndDedIncludeDirectories ${MPDir} ${SharedDir} ${GSLIncludeDirectory} ${CMAKE_BINARY_DIR}/shared)

	# Transparently use our bundled minizip.
	list(APPEND MPEngineAndDedIncludeDirectories ${MINIZIP_INCLUDE_DIRS})
	list(APPEND MPEngineAndDedLibraries          ${MINIZIP_LIBRARIES})

	# Transparently use either bundled or system zlib.
	list(APPEND MPEngineAndDedIncludeDirectories ${ZLIB_INCLUDE_DIR})
	list(APPEND MPEngineAndDedLibraries          ${ZLIB_LIBRARIES})

	set(MPEngineAndDedCgameFiles
		"${MPDir}/cgame/cg_public.h"
		)
	source_group("cgame" FILES ${MPEngineAndDedCgameFiles})
	set(MPEngineAndDedFiles ${MPEngineAndDedFiles} ${MPEngineAndDedCgameFiles})

	set(MPEngineAndDedClientFiles
		"${MPDir}/client/client.h"
		"${MPDir}/client/keys.h"
		"${MPDir}/client/snd_public.h"
		)
	source_group("client" FILES ${MPEngineAndDedClientFiles})
	set(MPEngineAndDedFiles ${MPEngineAndDedFiles} ${MPEngineAndDedClientFiles})

	set(MPEngineAndDedGameFiles
		"${MPDir}/game/anims.h"
		"${MPDir}/game/bg_public.h"
		"${MPDir}/game/bg_weapons.h"
		"${MPDir}/botlib/botlib.h"
		"${MPDir}/game/g_public.h"
		"${MPDir}/game/surfaceflags.h"
		)
	source_group("game" FILES ${MPEngineAndDedGameFiles})
	set(MPEngineAndDedFiles ${MPEngineAndDedFiles} ${MPEngineAndDedGameFiles})

	set(MPEngineAndDedCommonFiles
		"${MPDir}/qcommon/q_shared.h"
		"${SharedDir}/qcommon/q_platform.h"
		"${MPDir}/qcommon/cm_export.cpp"
		"${MPDir}/qcommon/cm_load.cpp"
		"${MPDir}/qcommon/cm_local.h"
		"${MPDir}/qcommon/cm_patch.cpp"
		"${MPDir}/qcommon/cm_patch.h"
		"${MPDir}/qcommon/cm_polylib.cpp"
		"${MPDir}/qcommon/cm_polylib.h"
		"${MPDir}/qcommon/cm_public.h"
		"${MPDir}/qcommon/cm_test.cpp"
		"${MPDir}/qcommon/cm_trace.cpp"
		"${MPDir}/qcommon/cmd.cpp"
		"${MPDir}/qcommon/common.cpp"
		"${MPDir}/qcommon/cvar.cpp"
		"${MPDir}/qcommon/disablewarnings.h"
		"${MPDir}/qcommon/files.cpp"
		"${MPDir}/qcommon/game_version.h"
		"${MPDir}/qcommon/GenericParser2.cpp"
		"${MPDir}/qcommon/GenericParser2.h"
		"${MPDir}/qcommon/huffman.cpp"
		"${MPDir}/qcommon/md4.cpp"
		"${MPDir}/qcommon/md5.cpp"
		"${MPDir}/qcommon/md5.h"
		"${MPDir}/qcommon/MiniHeap.h"
		"${MPDir}/qcommon/msg.cpp"
		"${MPDir}/qcommon/matcomp.h"
		"${MPDir}/qcommon/matcomp.cpp"
		"${MPDir}/qcommon/net_chan.cpp"
		"${MPDir}/qcommon/net_ip.cpp"
		"${MPDir}/qcommon/persistence.cpp"
		"${MPDir}/qcommon/q_shared.cpp"
		"${MPDir}/qcommon/qcommon.h"
		"${MPDir}/qcommon/qfiles.h"
		"${MPDir}/qcommon/RoffSystem.cpp"
		"${MPDir}/qcommon/RoffSystem.h"
		"${MPDir}/qcommon/sstring.h"
		"${MPDir}/qcommon/stringed_ingame.cpp"
		"${MPDir}/qcommon/stringed_ingame.h"
		"${MPDir}/qcommon/stringed_interface.cpp"
		"${MPDir}/qcommon/stringed_interface.h"
		"${MPDir}/qcommon/tags.h"
		"${MPDir}/qcommon/timing.h"
		"${MPDir}/qcommon/vm.cpp"
		"${MPDir}/qcommon/z_memman_pc.cpp"

		${SharedCommonFiles}
		)
	if(WIN32)
		set(MPEngineAndDedCommonFiles ${MPEngineAndDedCommonFiles})
	endif(WIN32)
	source_group("common" FILES ${MPEngineAndDedCommonFiles})
	set(MPEngineAndDedFiles ${MPEngineAndDedFiles} ${MPEngineAndDedCommonFiles})

	set(MPEngineAndDedCommonSafeFiles
		"${SharedDir}/qcommon/safe/files.cpp"
		"${SharedDir}/qcommon/safe/files.h"
		"${SharedDir}/qcommon/safe/gsl.h"
		)
	source_group("common/safe" FILES ${MPEngineAndDedCommonSafeFiles})
	set(MPEngineAndDedFiles ${MPEngineAndDedFiles} ${MPEngineAndDedCommonSafeFiles})

	set(MPEngineAndDedG2Files
		"${MPDir}/ghoul2/G2.h"
		"${MPDir}/ghoul2/G2_gore.h"
		"${MPDir}/ghoul2/ghoul2_shared.h"
		"${MPDir}/ghoul2/g2_local.h"
		)
	source_group("ghoul2" FILES ${MPEngineAndDedG2Files})
	set(MPEngineAndDedFiles ${MPEngineAndDedFiles} ${MPEngineAndDedG2Files})

	set(MPEngineAndDedIcarusFiles
		"${MPDir}/icarus/BlockStream.cpp"
		"${MPDir}/icarus/blockstream.h"
		"${MPDir}/icarus/GameInterface.cpp"
		"${MPDir}/icarus/GameInterface.h"
		"${MPDir}/icarus/icarus.h"
		"${MPDir}/icarus/Instance.cpp"
		"${MPDir}/icarus/instance.h"
		"${MPDir}/icarus/Interface.cpp"
		"${MPDir}/icarus/interface.h"
		"${MPDir}/icarus/interpreter.h"
		"${MPDir}/icarus/Memory.cpp"
		"${MPDir}/icarus/Q3_Interface.cpp"
		"${MPDir}/icarus/Q3_Interface.h"
		"${MPDir}/icarus/Q3_Registers.cpp"
		"${MPDir}/icarus/Q3_Registers.h"
		"${MPDir}/icarus/Sequence.cpp"
		"${MPDir}/icarus/sequence.h"
		"${MPDir}/icarus/Sequencer.cpp"
		"${MPDir}/icarus/sequencer.h"
		"${MPDir}/icarus/TaskManager.cpp"
		"${MPDir}/icarus/taskmanager.h"
		"${MPDir}/icarus/tokenizer.h"
		)
	source_group("icarus" FILES ${MPEngineAndDedIcarusFiles})
	set(MPEngineAndDedFiles ${MPEngineAndDedFiles} ${MPEngineAndDedIcarusFiles})

	set(MPEngineAndDedServerFiles
		"${MPDir}/server/NPCNav/navigator.cpp"
		"${MPDir}/server/NPCNav/navigator.h"
		"${MPDir}/server/server.h"
		"${MPDir}/server/sv_bot.cpp"
		"${MPDir}/server/sv_ccmds.cpp"
		"${MPDir}/server/sv_challenge.cpp"
		"${MPDir}/server/sv_client.cpp"
		"${MPDir}/server/sv_game.cpp"
		"${MPDir}/server/sv_init.cpp"
		"${MPDir}/server/sv_main.cpp"
		"${MPDir}/server/sv_net_chan.cpp"
		"${MPDir}/server/sv_snapshot.cpp"
		"${MPDir}/server/sv_world.cpp"
		"${MPDir}/server/sv_gameapi.cpp"
		"${MPDir}/server/sv_gameapi.h"
		)
	source_group("server" FILES ${MPEngineAndDedServerFiles})
	set(MPEngineAndDedFiles ${MPEngineAndDedFiles} ${MPEngineAndDedServerFiles})

	set(MPEngineAndDedUiFiles
		"${MPDir}/ui/keycodes.h"
		"${MPDir}/ui/ui_public.h"
		)
	source_group("ui" FILES ${MPEngineAndDedUiFiles})
	set(MPEngineAndDedFiles ${MPEngineAndDedFiles} ${MPEngineAndDedUiFiles})

	set(MPEngineAndDedSysFiles
		"${SharedDir}/sys/snapvector.cpp"
		)
	set(MPEngineAndDedFiles ${MPEngineAndDedFiles} ${MPEngineAndDedSysFiles})
	source_group("sys" FILES ${MPEngineAndDedSysFiles})
endif(BuildMPEngine OR BuildMPDed)

#        Engine/Executable (jamp.exe)

if(BuildMPEngine)
	set(MPEngineLibraries ${MPEngineAndDedLibraries})
	set(MPEngineIncludeDirectories ${MPEngineAndDedIncludeDirectories})
	set(MPEngineFiles ${MPEngineAndDedFiles})
	set(MPEngineDefines ${MPSharedDefines})

	# Steam Audio: sound that knows the level (client/snd_spatial.cpp). The engine is not linked against it, it
	# loads the library when it runs. It needs the header of Valve's repository, the submodule lib/steamaudio:
	# only that one file is put on the include path (in the build folder), the folder it comes from holds the
	# whole of Steam Audio, with header names of its own that the engine must not pick up.
	if(BuildJPSpatialAudio)
		set(SteamAudioCore "${CMAKE_SOURCE_DIR}/lib/steamaudio/core")
		if(EXISTS "${SteamAudioCore}/src/core/phonon.h")
			# phonon.h includes phonon_version.h. Valve's build writes that from a template with the version of
			# its project line, so this build does the same.
			function(jp_steamaudio_headers)
				file(STRINGS "${SteamAudioCore}/CMakeLists.txt" line REGEX "^project\\(Phonon VERSION")
				string(REGEX MATCH "([0-9]+)\\.([0-9]+)\\.([0-9]+)" version "${line}")
				if(NOT version)
					message(FATAL_ERROR "No version in ${SteamAudioCore}/CMakeLists.txt")
				endif()
				set(PROJECT_VERSION_MAJOR ${CMAKE_MATCH_1})
				set(PROJECT_VERSION_MINOR ${CMAKE_MATCH_2})
				set(PROJECT_VERSION_PATCH ${CMAKE_MATCH_3})
				configure_file("${SteamAudioCore}/src/core/phonon_version.h.in" "${CMAKE_BINARY_DIR}/steamaudio/phonon_version.h" @ONLY)
				configure_file("${SteamAudioCore}/src/core/phonon.h" "${CMAKE_BINARY_DIR}/steamaudio/phonon.h" COPYONLY)
				message(STATUS "Steam Audio ${version}: the header of lib/steamaudio, the library is loaded at run time")
				set(SteamAudioVersion "${version}" PARENT_SCOPE)
			endfunction()
			jp_steamaudio_headers()
			set(MPEngineDefines ${MPEngineDefines} "JP_SPATIAL")
			set(MPEngineIncludeDirectories ${MPEngineIncludeDirectories} "${CMAKE_BINARY_DIR}/steamaudio")

			# The library is no part of this tree and is not built here: Valve gives it out ready made, in the zip
			# of a release. SteamAudioSDK is a folder with the content of that zip. The library of this system is
			# copied from there next to the client, when the client is built and when it is installed. If the
			# folder does not have it and FetchSteamAudio is on, the zip is fetched once and that one file is
			# taken out of it.
			set(SteamAudioSDK "${CMAKE_SOURCE_DIR}/lib/steamaudio-sdk" CACHE PATH "The unpacked release zip of Steam Audio (the folder with lib/ in it): where the library is taken from that the client loads at run time")
			option(FetchSteamAudio "Download the release zip of Steam Audio when SteamAudioSDK does not have the library of this system (181 MB, once)" ON)
			set(SteamAudioURL "" CACHE STRING "Where FetchSteamAudio gets the zip. Empty: the release of Valve on GitHub. file:// for a zip that is on the disk already")
			# the zips this tree knows: nothing else is unpacked under the name of a release
			set(SteamAudioSHA256_4.8.1 "4a0aa5ec1176f38f0b0993a37c2259d9e86f27e22d5e24f83ec4c3cb9a1d5449")
			if(WIN32)
				set(SteamAudioLibrary "lib/windows-x64/phonon.dll")
			elseif(APPLE)
				set(SteamAudioLibrary "lib/osx/libphonon.dylib")
			elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
				set(SteamAudioLibrary "lib/linux-x64/libphonon.so")
			else()
				set(SteamAudioLibrary "")
			endif()

			function(jp_steamaudio_fetch)
				set(url "${SteamAudioURL}")
				if(NOT url)
					set(url "https://github.com/ValveSoftware/steam-audio/releases/download/v${SteamAudioVersion}/steamaudio_${SteamAudioVersion}.zip")
				endif()
				if(NOT DEFINED SteamAudioSHA256_${SteamAudioVersion})
					message(STATUS "Steam Audio: no checksum of the zip of ${SteamAudioVersion} in codemp/CMakeLists.txt, it is not fetched")
					return()
				endif()
				if(CMAKE_VERSION VERSION_LESS 3.18)
					message(STATUS "Steam Audio: this cmake can not unpack a zip (3.18 and up can)")
					return()
				endif()
				set(zip "${CMAKE_BINARY_DIR}/steamaudio/release.zip")
				message(STATUS "Steam Audio: fetching ${url} for ${SteamAudioLibrary} (181 MB, once)")
				file(DOWNLOAD "${url}" "${zip}" STATUS status TLS_VERIFY ON)
				list(GET status 0 code)
				if(code)
					list(GET status 1 text)
					message(WARNING "Steam Audio: the download failed (${text})")
					file(REMOVE "${zip}")
					return()
				endif()
				file(SHA256 "${zip}" sum)
				if(NOT sum STREQUAL "${SteamAudioSHA256_${SteamAudioVersion}}")
					message(WARNING "Steam Audio: ${url} is not the zip of ${SteamAudioVersion} this tree knows (its SHA256 is ${sum}), it is not used")
					file(REMOVE "${zip}")
					return()
				endif()
				file(ARCHIVE_EXTRACT INPUT "${zip}" DESTINATION "${CMAKE_BINARY_DIR}/steamaudio/unpacked" PATTERNS "steamaudio/${SteamAudioLibrary}")
				get_filename_component(dir "${SteamAudioSDK}/${SteamAudioLibrary}" DIRECTORY)
				file(COPY "${CMAKE_BINARY_DIR}/steamaudio/unpacked/steamaudio/${SteamAudioLibrary}" DESTINATION "${dir}")
				file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/steamaudio/unpacked")
				file(REMOVE "${zip}")
			endfunction()

			if(SteamAudioLibrary AND FetchSteamAudio AND NOT EXISTS "${SteamAudioSDK}/${SteamAudioLibrary}")
				jp_steamaudio_fetch()
			endif()
			if(SteamAudioLibrary AND EXISTS "${SteamAudioSDK}/${SteamAudioLibrary}")
				set(SteamAudioLibraryFile "${SteamAudioSDK}/${SteamAudioLibrary}")
				message(STATUS "Steam Audio: ${SteamAudioLibraryFile} goes next to the client")
			else()
				message(STATUS "Steam Audio: no library for this system in ${SteamAudioSDK}. The client is built with s_spatial, the library has to be put next to it by hand (docs/japlus/audio.md)")
			endif()
		else()
			message(STATUS "Steam Audio: lib/steamaudio is not checked out (git submodule update --init), the client is built without s_spatial")
		endif()
	endif()

	#    Dependencies

	if (NOT WIN32)
		# OpenGL
		find_package(OpenGL REQUIRED)
		set(MPEngineIncludeDirectories ${MPEngineIncludeDirectories} ${OPENGL_INCLUDE_DIR})
		set(MPEngineLibraries ${MPEngineLibraries} ${OPENGL_LIBRARIES})
	endif()

	if(UseInternalSDL2)
		set(MPEngineLibraries
			${MPEngineLibraries}
			${VisionJKLibDir}/SDL2/lib/x64/SDL2main.lib
			${VisionJKLibDir}/SDL2/lib/x64/SDL2.lib
			)

		set(MPEngineIncludeDirectories
			${MPEngineIncludeDirectories}
			${VisionJKLibDir}/SDL2/include
			)
	else()
		find_package(SDL2 REQUIRED CONFIG)
		if (TARGET SDL2::SDL2)
			set(SDL2_INCLUDE_DIRS "")
			set(SDL2_LIBRARIES SDL2::SDL2)
		endif()
		string(STRIP ${SDL2_LIBRARIES} SDL2_LIBRARIES)
		set(MPEngineIncludeDirectories ${MPEngineIncludeDirectories} ${SDL2_INCLUDE_DIRS})
		set(MPEngineLibraries ${MPEngineLibraries} ${SDL2_LIBRARIES})
	endif()

	#    Source Files

	set(MPEngineClientFiles
		"${MPDir}/client/cl_avi.cpp"
		"${MPDir}/client/cl_cgame.cpp"
		"${MPDir}/client/cl_cgameapi.cpp"
		"${MPDir}/client/cl_cgameapi.h"
		"${MPDir}/client/cl_cin.cpp"
		"${MPDir}/client/cl_console.cpp"
		"${MPDir}/client/cl_input.cpp"
		"${MPDir}/client/cl_keys.cpp"
		"${MPDir}/client/cl_lan.cpp"
		"${MPDir}/client/cl_lan.h"
		"${MPDir}/client/cl_main.cpp"
		"${MPDir}/client/cl_net_chan.cpp"
		"${MPDir}/client/cl_parse.cpp"
		"${MPDir}/client/cl_scrn.cpp"
		"${MPDir}/client/cl_ui.cpp"
		"${MPDir}/client/cl_uiapi.cpp"
		"${MPDir}/client/cl_uiapi.h"
		"${MPDir}/client/FXExport.cpp"
		"${MPDir}/client/FXExport.h"
		"${MPDir}/client/FxPrimitives.cpp"
		"${MPDir}/client/FxPrimitives.h"
		"${MPDir}/client/FxScheduler.cpp"
		"${MPDir}/client/FxScheduler.h"
		"${MPDir}/client/FxSystem.cpp"
		"${MPDir}/client/FxSystem.h"
		"${MPDir}/client/FxTemplate.cpp"
		"${MPDir}/client/FxUtil.cpp"
		"${MPDir}/client/FxUtil.h"
		"${MPDir}/client/snd_ambient.cpp"
		"${MPDir}/client/snd_ambient.h"
		"${MPDir}/client/snd_dma.cpp"
		"${MPDir}/client/snd_local.h"
		"${MPDir}/client/snd_mem.cpp"
		"${MPDir}/client/snd_mix.cpp"
		"${MPDir}/client/snd_mp3.cpp"
		"${MPDir}/client/snd_mp3.h"
		"${MPDir}/client/snd_music.cpp"
		"${MPDir}/client/snd_music.h"
		"${MPDir}/client/snd_spatial.cpp"
		"${MPDir}/client/snd_spatial.h"
		)
	source_group("client" FILES ${MPEngineClientFiles})
	set(MPEngineFiles ${MPEngineFiles} ${MPEngineClientFiles})

	set(MPEngineRatlFiles
		"${MPDir}/Ratl/bits_vs.h"
		"${MPDir}/Ratl/ratl_common.h"
		"${MPDir}/Ratl/vector_vs.h"
		)
	source_group("ratl" FILES ${MPEngineRatlFiles})
	set(MPEngineFiles ${MPEngineFiles} ${MPEngineRatlFiles})

	set(MPEngineRavlFiles
		"${MPDir}/Ravl/CVec.h"
		)
	source_group("ravl" FILES ${MPEngineRavlFiles})
	set(MPEngineFiles ${MPEngineFiles} ${MPEngineRavlFiles})

	set(MPEngineMp3Files
		"${MPDir}/mp3code/cdct.c"
		"${MPDir}/mp3code/config.h"
		"${MPDir}/mp3code/csbt.c"
		"${MPDir}/mp3code/csbtb.c"
		"${MPDir}/mp3code/csbtl3.c"
		"${MPDir}/mp3code/cup.c"
		"${MPDir}/mp3code/cupini.c"
		"${MPDir}/mp3code/cupl1.c"
		"${MPDir}/mp3code/cupl3.c"
		"${MPDir}/mp3code/cwin.c"
		"${MPDir}/mp3code/cwinb.c"
		"${MPDir}/mp3code/cwinm.c"
		"${MPDir}/mp3code/htable.h"
		"${MPDir}/mp3code/hwin.c"
		"${MPDir}/mp3code/jdw.h"
		"${MPDir}/mp3code/l3.h"
		"${MPDir}/mp3code/l3dq.c"
		"${MPDir}/mp3code/l3init.c"
		"${MPDir}/mp3code/mdct.c"
		"${MPDir}/mp3code/mhead.c"
		"${MPDir}/mp3code/mhead.h"
		"${MPDir}/mp3code/mp3struct.h"
		"${MPDir}/mp3code/msis.c"
		"${MPDir}/mp3code/port.h"
		"${MPDir}/mp3code/small_header.h"
		"${MPDir}/mp3code/tableawd.h"
		"${MPDir}/mp3code/towave.c"
		"${MPDir}/mp3code/uph.c"
		"${MPDir}/mp3code/upsf.c"
		"${MPDir}/mp3code/wavep.c"
		)
	source_group("mp3code" FILES ${MPEngineMp3Files})
	set(MPEngineFiles ${MPEngineFiles} ${MPEngineMp3Files})

	set(MPEngineSysFiles
		"${SharedDir}/sdl/sdl_window.cpp"
		"${SharedDir}/sdl/sdl_icon.h"
		"${SharedDir}/sdl/sdl_input.cpp"
		"${SharedDir}/sdl/sdl_sound.cpp"
		"${SharedDir}/sys/sys_loadlib.h"
		"${SharedDir}/sys/sys_local.h"
		"${SharedDir}/sys/sys_main.cpp"
		"${SharedDir}/sys/sys_event.cpp"
		"${SharedDir}/sys/sys_public.h"
		"${SharedDir}/sys/con_local.h"
		"${SharedDir}/sys/con_log.cpp"
		)

	if(WIN32)
		set(MPEngineWin32Files
			"${MPDir}/win32/resource.h"
			"${MPDir}/win32/visionjk.rc"
			"${SharedDir}/sys/win_manifest.manifest"
			)

		set(MPEngineFiles ${MPEngineFiles} ${MPEngineWin32Files})
		source_group("win32" FILES ${MPEngineWin32Files})

		set(MPEngineSysFiles
			${MPEngineSysFiles}
			"${SharedDir}/sys/sys_win32.cpp"
			"${SharedDir}/sys/con_passive.cpp"
			)
	else(WIN32)
		set(MPEngineSysFiles
			${MPEngineSysFiles}
			"${SharedDir}/sys/sys_unix.cpp"
			"${SharedDir}/sys/con_tty.cpp"
			)
	endif(WIN32)

	set(MPEngineFiles ${MPEngineFiles} ${MPEngineSysFiles})
	source_group("sys" FILES ${MPEngineSysFiles})

	if(MakeApplicationBundles)
		set(MACOSX_BUNDLE_BUNDLE_NAME ${MPEngine})
		set(MACOSX_BUNDLE_DISPLAY_NAME ${MPEngine})
		set(MACOSX_BUNDLE_ICON_FILE VisionJK.icns)
		set_source_files_properties(${MPDir}/macosx/VisionJK.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
		set(MPEngineFiles ${MPEngineFiles} ${MPDir}/macosx/VisionJK.icns)
		set_source_files_properties(${MPDir}/macosx/English.lproj/InfoPlist.strings PROPERTIES MACOSX_PACKAGE_LOCATION Resources/English.lproj)
		set(MPEngineFiles ${MPEngineFiles} ${MPDir}/macosx/English.lproj/InfoPlist.strings)
	endif(MakeApplicationBundles)

	if(WIN32)
		add_executable(${MPEngine} WIN32 ${MPEngineFiles})
		if(UseInternalSDL2)
			set(MPEngineExtraInstallFiles
				${VisionJKLibDir}/SDL2/bin/x64/SDL2.dll
				)
		endif()
	else(WIN32)
		if(MakeApplicationBundles)
			add_executable(${MPEngine} MACOSX_BUNDLE ${MPEngineFiles})
			set_target_properties(${MPEngine} PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${MPDir}/macosx/VisionJKInfo.plist")
		else(MakeApplicationBundles)
			add_executable(${MPEngine} ${MPEngineFiles})
		endif(MakeApplicationBundles)
	endif(WIN32)

	if(MakeApplicationBundles)
		install(CODE "
			include(BundleUtilities)
			set(BU_CHMOD_BUNDLE_ITEMS ON)
			fixup_bundle(\"${CMAKE_BINARY_DIR}/${MPEngine}.app\" \"\" \"\")
			"
			COMPONENT Runtime)
		install(TARGETS ${MPEngine}
			BUNDLE
			DESTINATION ${JKAInstallDir}
			COMPONENT ${JKAMPClientComponent})
	else(MakeApplicationBundles)
		install(TARGETS ${MPEngine}
			RUNTIME
			DESTINATION ${JKAInstallDir}
			COMPONENT ${JKAMPClientComponent})
		if(MPEngineExtraInstallFiles)
			install(FILES ${MPEngineExtraInstallFiles}
				DESTINATION ${JKAInstallDir}
				COMPONENT ${JKAMPClientComponent})
		endif()
		if(SteamAudioLibraryFile)
			install(FILES "${SteamAudioLibraryFile}"
				DESTINATION ${JKAInstallDir}
				COMPONENT ${JKAMPClientComponent})
		endif()
	endif(MakeApplicationBundles)

	# The client looks for the library of Steam Audio in the folder it is in itself. In an application bundle
	# that is inside the bundle, which is installed as a whole.
	if(SteamAudioLibraryFile)
		add_custom_command(TARGET ${MPEngine} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SteamAudioLibraryFile}" "$<TARGET_FILE_DIR:${MPEngine}>"
			VERBATIM)
	endif()

	set_target_properties(${MPEngine} PROPERTIES COMPILE_DEFINITIONS "${MPEngineDefines}")

	# Hide symbols not explicitly marked public.
	set_property(TARGET ${MPEngine} APPEND PROPERTY COMPILE_OPTIONS ${VISIONJK_VISIBILITY_FLAGS})

	set_target_properties(${MPEngine} PROPERTIES INCLUDE_DIRECTORIES "${MPEngineIncludeDirectories}")
	set_target_properties(${MPEngine} PROPERTIES PROJECT_LABEL "MP Client")
	target_link_libraries(${MPEngine} ${MPEngineLibraries})
endif(BuildMPEngine)

#        Dedicated Server (Engine) (jampded.exe)

if(BuildMPDed)
	set(MPDedLibraries ${MPEngineAndDedLibraries})
	set(MPDedIncludeDirectories ${MPEngineAndDedIncludeDirectories})
	set(MPDedFiles ${MPEngineAndDedFiles})
	set(MPDedDefines ${MPSharedDefines} "_CONSOLE" "DEDICATED")

	# Dedicated renderer is compiled with the server.
	set(MPDedicatedRendererFiles
		"${MPDir}/ghoul2/G2_gore.cpp"
		"${MPDir}/rd-common/mdx_format.h"
		"${MPDir}/rd-common/tr_public.h"
		"${MPDir}/rd-dedicated/tr_local.h"
		"${MPDir}/rd-dedicated/G2_API.cpp"
		"${MPDir}/rd-dedicated/G2_bolts.cpp"
		"${MPDir}/rd-dedicated/G2_bones.cpp"
		"${MPDir}/rd-dedicated/G2_misc.cpp"
		"${MPDir}/rd-dedicated/G2_surfaces.cpp"
		"${MPDir}/rd-dedicated/tr_backend.cpp"
		"${MPDir}/rd-dedicated/tr_ghoul2.cpp"
		"${MPDir}/rd-dedicated/tr_init.cpp"
		"${MPDir}/rd-dedicated/tr_main.cpp"
		"${MPDir}/rd-dedicated/tr_mesh.cpp"
		"${MPDir}/rd-dedicated/tr_model.cpp"
		"${MPDir}/rd-dedicated/tr_shader.cpp"
		"${MPDir}/rd-dedicated/tr_skin.cpp")
	source_group("renderer" FILES ${MPDedicatedRendererFiles})
	set(MPDedFiles ${MPDedFiles} ${MPDedicatedRendererFiles})

	set(MPDedNullFiles
		"${MPDir}/null/null_client.cpp"
		"${MPDir}/null/null_input.cpp"
		"${MPDir}/null/null_renderer.cpp"
		"${MPDir}/null/null_snddma.cpp"
		)
	source_group("null" FILES ${MPDedNullFiles})
	set(MPDedFiles ${MPDedFiles} ${MPDedNullFiles})
	if(WIN32)
		set(MPDedWin32Files
			"${MPDir}/win32/visionjkded.rc"
			)
		source_group("win32" FILES ${MPDedWin32Files})
		set(MPDedFiles ${MPDedFiles} ${MPDedWin32Files})

		set(MPDedSysFiles
			"${SharedDir}/sys/sys_loadlib.h"
			"${SharedDir}/sys/sys_local.h"
			"${SharedDir}/sys/sys_main.cpp"
			"${SharedDir}/sys/sys_event.cpp"
			"${SharedDir}/sys/sys_public.h"
			"${SharedDir}/sys/sys_win32.cpp"
			"${SharedDir}/sys/con_local.h"
			"${SharedDir}/sys/con_log.cpp"
			"${SharedDir}/sys/con_win32.cpp"
			)
		set(MPDedLibraries ${MPDedLibraries} ${CMAKE_DL_LIBS})
		source_group("sys" FILES ${MPDedSysFiles})
		set(MPDedFiles ${MPDedFiles} ${MPDedSysFiles})
	else(WIN32)
		set(MPDedSysFiles
			"${SharedDir}/sys/sys_loadlib.h"
			"${SharedDir}/sys/sys_local.h"
			"${SharedDir}/sys/sys_main.cpp"
			"${SharedDir}/sys/sys_event.cpp"
			"${SharedDir}/sys/sys_public.h"
			"${SharedDir}/sys/sys_unix.cpp"
			"${SharedDir}/sys/con_local.h"
			"${SharedDir}/sys/con_log.cpp"
			"${SharedDir}/sys/con_tty.cpp"
			)
		set(MPDedLibraries ${MPDedLibraries} ${CMAKE_DL_LIBS})
		source_group("sys" FILES ${MPDedSysFiles})
		set(MPDedFiles ${MPDedFiles} ${MPDedSysFiles})
	endif(WIN32)

	add_executable(${MPDed} ${MPDedFiles})
	install(TARGETS ${MPDed}
		RUNTIME
		DESTINATION ${JKAInstallDir}
		COMPONENT ${JKAMPServerComponent})

	set_target_properties(${MPDed} PROPERTIES COMPILE_DEFINITIONS "${MPDedDefines}")

	# Hide symbols not explicitly marked public.
	set_property(TARGET ${MPDed} APPEND PROPERTY COMPILE_OPTIONS ${VISIONJK_VISIBILITY_FLAGS})

	set_target_properties(${MPDed} PROPERTIES INCLUDE_DIRECTORIES "${MPDedIncludeDirectories}")
	set_target_properties(${MPDed} PROPERTIES PROJECT_LABEL "MP Dedicated Server")
	target_link_libraries(${MPDed} ${MPDedLibraries})
endif(BuildMPDed)
