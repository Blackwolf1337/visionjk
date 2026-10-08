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

# Collects the entity definitions of the game code: every /*QUAKED ... */ comment of the .c files below a
# folder, one after the other, each closed by a line with */. That is what a map editor reads the classes
# from, and what the editor of the web admin panel shows.
# cmake -DDIR=folder -DOUTPUT=file -P ExtractQuaked.cmake
file(GLOB_RECURSE sources "${DIR}/*.c")
list(SORT sources)
set(out "")
foreach(source ${sources})
	file(READ "${source}" text)
	# by position and not with a regular expression: the text has ; and [ ] in it, which a list would split at
	while(TRUE)
		string(FIND "${text}" "/*QUAKED" begin)
		if(begin EQUAL -1)
			break()
		endif()
		string(SUBSTRING "${text}" ${begin} -1 text)
		string(FIND "${text}" "*/" end)
		if(end EQUAL -1)
			break()
		endif()
		string(SUBSTRING "${text}" 0 ${end} block)
		math(EXPR end "${end} + 2")
		string(SUBSTRING "${text}" ${end} -1 text)
		string(APPEND out "${block}\n*/\n")
	endwhile()
endforeach()
file(WRITE "${OUTPUT}" "${out}")
