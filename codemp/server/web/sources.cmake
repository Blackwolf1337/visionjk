#============================================================================
# Copyright (C) 2026, VisionJK contributors
#
# This file is part of the VisionJK source code.
#
# VisionJK is free software; you can redistribute it and/or modify it
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

# The sources of the web host of the dedicated server (sv_webPort). codemp/CMakeLists.txt includes this in
# front of the add_executable of the server. The folders the files are found in (codemp and shared) are
# among the include directories of the server already.

set(MPDedWebFiles
	"${MPDir}/server/web/sv_web_local.h"
	"${MPDir}/server/web/sv_web.cpp"
	"${MPDir}/server/web/sv_web_http.cpp"
	"${MPDir}/server/web/sv_web_ws.cpp"
	"${MPDir}/server/web/sv_web_voice.cpp"
	# the sockets of the system, which net_ip.cpp and the web host name alike
	"${MPDir}/qcommon/net_local.h"
	)
source_group("server/web" FILES ${MPDedWebFiles})
set(MPDedFiles ${MPDedFiles} ${MPDedWebFiles})

# The datagram of the voice chat, which sv_web_voice.cpp reads before it hands one on: the file the game
# modules are built with (codemp/game/CMakeLists.txt, codemp/cgame/CMakeLists.txt). It calls nothing.
set(MPDedVoiceFiles
	"${MPDir}/game/voice/bg_voice.h"
	"${MPDir}/game/voice/bg_voice.c"
	)
source_group("game/voice" FILES ${MPDedVoiceFiles})
set(MPDedFiles ${MPDedFiles} ${MPDedVoiceFiles})

# what it reads a request with, the web admin panel of the game module is built with the same files
# (codemp/game/CMakeLists.txt)
set(MPDedSharedWebFiles
	"${SharedDir}/web/web_http.h"
	"${SharedDir}/web/web_http.c"
	"${SharedDir}/web/web_base64.c"
	"${SharedDir}/web/web_sha1.c"
	)
source_group("shared/web" FILES ${MPDedSharedWebFiles})
set(MPDedFiles ${MPDedFiles} ${MPDedSharedWebFiles})
