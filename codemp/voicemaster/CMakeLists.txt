#============================================================================
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

# The voice master: the relay of the voice chat for game servers without the game module of this tree
# (docs/japlus/voice.md). A program by itself: the datagram of the voice chat, SHA-1 and nothing of the engine.
set(VoiceMasterFiles
	"${MPDir}/voicemaster/vm_main.c"
	"${MPDir}/voicemaster/vm_core.c"
	"${MPDir}/voicemaster/vm_core.h"
	"${MPDir}/game/voice/bg_voice.c"
	"${MPDir}/game/voice/bg_voice.h"
	"${MPDir}/game/bg_net.h"
	"${SharedDir}/web/web_sha1.c"
	"${SharedDir}/web/web_http.h"
	)
source_group("voicemaster" FILES ${VoiceMasterFiles})

set(VoiceMasterIncludeDirectories
	"${MPDir}"
	"${SharedDir}"
	"${CMAKE_BINARY_DIR}/shared"
	)
# _GAME: q_shared.h wants to know which module it is read for, and bg_voice.c is the one of the game module
set(VoiceMasterDefines ${MPSharedDefines} "_GAME")
if(WIN32)
	set(VoiceMasterLibraries "ws2_32")
else()
	set(VoiceMasterLibraries "m")
endif()

add_executable(${VoiceMaster} ${VoiceMasterFiles})
install(TARGETS ${VoiceMaster}
	RUNTIME
	DESTINATION ${JKAInstallDir}
	COMPONENT ${JKAMPServerComponent})

set_target_properties(${VoiceMaster} PROPERTIES COMPILE_DEFINITIONS "${VoiceMasterDefines}")
set_target_properties(${VoiceMaster} PROPERTIES INCLUDE_DIRECTORIES "${VoiceMasterIncludeDirectories}")
set_target_properties(${VoiceMaster} PROPERTIES PROJECT_LABEL "Voice Master")
target_link_libraries(${VoiceMaster} ${VoiceMasterLibraries})
