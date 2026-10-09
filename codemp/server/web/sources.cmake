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
