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

# Puts the page of the web admin panel together. The page is kept as a template (index.html), a style sheet and
# one script file for each part of it, and handed out as a single file, so that a browser asks once and the
# scripts run as the one script they are parts of. Every line of the template of the form
#   <!--#include name -->
# is replaced by the file of that name next to it, as it is.
# cmake -DINPUT=template -DOUTPUT=file -P BundlePage.cmake
get_filename_component(dir "${INPUT}" DIRECTORY)
file(READ "${INPUT}" text)
set(out "")
# by position and not with a regular expression or as a list of lines: the files have ; and [ ] in them
while(TRUE)
	string(FIND "${text}" "<!--#include " begin)
	if(begin EQUAL -1)
		break()
	endif()
	string(SUBSTRING "${text}" 0 ${begin} head)
	string(APPEND out "${head}")
	math(EXPR begin "${begin} + 13")
	string(SUBSTRING "${text}" ${begin} -1 text)
	string(FIND "${text}" " -->" end)
	if(end EQUAL -1)
		message(FATAL_ERROR "${INPUT}: an include that is not closed")
	endif()
	string(SUBSTRING "${text}" 0 ${end} name)
	# the line of the include goes with it, its end included
	string(FIND "${text}" "\n" end)
	math(EXPR end "${end} + 1")
	string(SUBSTRING "${text}" ${end} -1 text)
	if(NOT EXISTS "${dir}/${name}")
		message(FATAL_ERROR "${INPUT}: ${name} is not there")
	endif()
	file(READ "${dir}/${name}" part)
	string(APPEND out "${part}")
endwhile()
string(APPEND out "${text}")
file(WRITE "${OUTPUT}" "${out}")
