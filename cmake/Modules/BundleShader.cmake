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

# Makes one file of a shader of rd-vulkan-vision for a device that compiles GLSL itself (GLSL ES on WebGL2), which
# knows no #include. Every line of the form
#   #include "name"
# is replaced by the file of that name next to the shader, with what that file includes. The #version line is
# left out: it names the GLSL of Vulkan, the device puts its own in front. No #line is written, GLSL ES takes no
# file names in it.
# cmake -DINPUT=shader -DOUTPUT=file -P BundleShader.cmake
# With -DVALIDATOR=glslangValidator -DPREFIX=file -DSTAGE=vert or frag the result is compiled first, as that
# stage and with the lines of the prefix file in front, and is only written if it has no error.
# A script run with -P has no CMakeLists.txt that sets the policies. Without them a cmake before 4.0 takes the
# TRUE of while(TRUE) for a variable nobody set and never enters the loop (seen with 3.25 of Debian 12).
cmake_minimum_required(VERSION 3.1...3.31)
get_filename_component(dir "${INPUT}" DIRECTORY)

# by position and not with a regular expression or as a list of lines: the files have ; and [ ] in them
function(bundle file result)
	if(NOT EXISTS "${file}")
		message(FATAL_ERROR "${INPUT}: ${file} is not there")
	endif()
	file(READ "${file}" text)
	# with a line end in front an include in the first line is found like every other one
	set(text "\n${text}")
	set(out "")
	while(TRUE)
		string(FIND "${text}" "\n#include \"" begin)
		if(begin EQUAL -1)
			break()
		endif()
		math(EXPR begin "${begin} + 1")
		string(SUBSTRING "${text}" 0 ${begin} head)
		string(APPEND out "${head}")
		math(EXPR begin "${begin} + 10")
		string(SUBSTRING "${text}" ${begin} -1 text)
		string(FIND "${text}" "\"" end)
		if(end EQUAL -1)
			message(FATAL_ERROR "${file}: an include that is not closed")
		endif()
		string(SUBSTRING "${text}" 0 ${end} name)
		# the rest of the line of the include goes with it, its end stays
		string(FIND "${text}" "\n" end)
		if(end EQUAL -1)
			set(text "")
		else()
			string(SUBSTRING "${text}" ${end} -1 text)
		endif()
		bundle("${dir}/${name}" part)
		string(APPEND out "${part}")
	endwhile()
	string(APPEND out "${text}")
	string(SUBSTRING "${out}" 1 -1 out)
	set(${result} "${out}" PARENT_SCOPE)
endfunction()

bundle("${INPUT}" out)

set(out "\n${out}")
string(FIND "${out}" "\n#version " begin)
if(NOT begin EQUAL -1)
	string(SUBSTRING "${out}" 0 ${begin} head)
	math(EXPR begin "${begin} + 1")
	string(SUBSTRING "${out}" ${begin} -1 out)
	string(FIND "${out}" "\n" end)
	if(end EQUAL -1)
		set(out "${head}")
	else()
		string(SUBSTRING "${out}" ${end} -1 out)
		set(out "${head}${out}")
	endif()
endif()
string(SUBSTRING "${out}" 1 -1 out)

if(VALIDATOR)
	# the validator tells the stage by the end of the name
	set(checked "${OUTPUT}.${STAGE}")
	file(READ "${PREFIX}" prefix)
	file(WRITE "${checked}" "${prefix}${out}")
	execute_process(COMMAND "${VALIDATOR}" "${checked}" RESULT_VARIABLE failed OUTPUT_VARIABLE messages ERROR_VARIABLE messages)
	if(failed)
		string(STRIP "${messages}" messages)
		message(FATAL_ERROR "${INPUT} is not GLSL ES 3.00. The lines count in ${checked}:\n${messages}")
	endif()
endif()
file(WRITE "${OUTPUT}" "${out}")
