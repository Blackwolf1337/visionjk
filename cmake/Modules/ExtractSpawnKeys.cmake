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

# Collects the keys the spawn function of every entity class reads itself: the calls G_SpawnString, G_SpawnInt,
# G_SpawnFloat, G_SpawnVector and G_SpawnBoolean with the name of a key, in the function the table of the
# classes names for a class and in the functions that one calls (three deep). The definitions of the classes
# (their QUAKED comments) leave many of these keys out, the editor of the web admin panel offers them.
# One line for a class: its name, then for every key a tab and key|kind|default (kind: String, Int, Float,
# Vector, Boolean; the default is the one the call has, empty when it has none that is written out).
# The line *item is for every class that is an item (G_SpawnItem).
# cmake -DDIR=folder -DOUTPUT=file -P ExtractSpawnKeys.cmake
file(GLOB_RECURSE sources "${DIR}/*.c")
list(SORT sources)

set(readers "")		# the functions that read a key themselves
set(classes "")		# class=function, from the tables of the classes
foreach(source ${sources})
	file(READ "${source}" text)
	string(FIND "${text}" "G_Spawn" reads)
	string(FIND "${text}" "SP_" spawns)
	if(reads EQUAL -1 AND spawns EQUAL -1)
		continue()
	endif()

	# { "classname", SP_function }: no ; and no brackets in such an entry, so a list holds them
	string(REGEX MATCHALL "{[ \t]*\"[A-Za-z0-9_]+\"[ \t]*,[ \t]*SP_[A-Za-z0-9_]+[ \t]*}" entries "${text}")
	foreach(entry ${entries})
		string(REGEX REPLACE "^{[ \t]*\"([A-Za-z0-9_]+)\"[ \t]*,[ \t]*(SP_[A-Za-z0-9_]+)[ \t]*}$" "\\1=\\2" pair "${entry}")
		list(APPEND classes "${pair}")
	endforeach()

	# One function after the other: each ends with a } at the start of a line. By position and not as a list:
	# the text has ; and [ ] in it.
	while(TRUE)
		string(FIND "${text}" "\n}" end)
		if(end EQUAL -1)
			break()
		endif()
		string(SUBSTRING "${text}" 0 ${end} chunk)
		math(EXPR end "${end} + 2")
		string(SUBSTRING "${text}" ${end} -1 text)

		string(FIND "${chunk}" "G_Spawn" reads)
		string(FIND "${chunk}" "SP_" spawns)
		if(reads EQUAL -1 AND spawns EQUAL -1)
			continue()
		endif()
		# its head: a line that starts with a type, then the name, the arguments and the {
		if(NOT chunk MATCHES "\n[A-Za-z_][A-Za-z0-9_ \t*]*[ \t*]([A-Za-z_][A-Za-z0-9_]*)[ \t]*\\([^;{}()]*\\)[ \t\r\n]*{")
			continue()
		endif()
		set(func "${CMAKE_MATCH_1}")

		set(keys "")
		string(REGEX MATCHALL "G_Spawn(String|Int|Float|Vector|Boolean)[ \t]*\\([ \t]*\"[A-Za-z0-9_]+\"([ \t]*,[ \t]*\"[^\"|;]*\")?" calls "${chunk}")
		foreach(call ${calls})
			string(REGEX REPLACE "^G_Spawn([A-Za-z]+)[ \t]*\\([ \t]*\"([A-Za-z0-9_]+)\".*$" "\\2|\\1|" key "${call}")
			if(call MATCHES ",[ \t]*\"([^\"]*)\"$")
				string(APPEND key "${CMAKE_MATCH_1}")
			endif()
			list(APPEND keys "${key}")
		endforeach()

		if(keys OR func MATCHES "^SP_")
			# a name that two files have: what both read
			set(keys_${func} ${keys_${func}} ${keys})
			set(body_${func} "${body_${func}}${chunk}")
			if(keys)
				list(APPEND readers ${func})
			endif()
		endif()
	endwhile()
endforeach()
list(REMOVE_DUPLICATES readers)

# what a function reads, with what the readers it calls read
function(keys_of func depth result)
	set(keys ${keys_${func}})
	if(depth LESS 3 AND DEFINED body_${func})
		math(EXPR deeper "${depth} + 1")
		foreach(other ${readers})
			if(other STREQUAL func)
				continue()
			endif()
			string(FIND "${body_${func}}" "${other}" at)
			if(NOT at EQUAL -1 AND body_${func} MATCHES "[^A-Za-z0-9_]${other}[ \t]*\\(")
				keys_of(${other} ${deeper} more)
				list(APPEND keys ${more})
			endif()
		endforeach()
	endif()
	set(${result} "${keys}" PARENT_SCOPE)
endfunction()

# a key once, as the first call that reads it has it
function(line_of name keys result)
	set(seen "")
	set(line "${name}")
	foreach(key ${keys})
		string(REGEX REPLACE "\\|.*$" "" bare "${key}")
		string(TOLOWER "${bare}" bare)
		if(NOT bare IN_LIST seen)
			list(APPEND seen "${bare}")
			string(APPEND line "\t${key}")
		endif()
	endforeach()
	set(${result} "${line}" PARENT_SCOPE)
endfunction()

list(REMOVE_DUPLICATES classes)
list(SORT classes)
set(out "")
foreach(pair ${classes})
	string(REGEX REPLACE "=.*$" "" name "${pair}")
	string(REGEX REPLACE "^.*=" "" func "${pair}")
	keys_of(${func} 0 keys)
	if(keys)
		line_of("${name}" "${keys}" line)
		string(APPEND out "${line}\n")
	endif()
endforeach()
keys_of(G_SpawnItem 0 keys)
if(keys)
	line_of("*item" "${keys}" line)
	string(APPEND out "${line}\n")
endif()
file(WRITE "${OUTPUT}" "${out}")
