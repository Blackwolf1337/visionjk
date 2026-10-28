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

set(TestFiles
	"main.cpp"
	"safe/string.cpp"
	"safe/limited_vector.cpp"
	"${SharedDir}/qcommon/safe/string.cpp"
	"vjk/c_api.h"
	"vjk/stubs.cpp"
	"vjk/update.cpp"
	"vjk/utf8.cpp"
	"vjk/info.cpp"
	"vjk/voice.cpp"
	"vjk/boombox.cpp"
	"vjk/sha256.cpp"
	"vjk/auth.cpp"
	"vjk/missing.cpp"
	"vjk/stringed.cpp"
	"vjk/memwatch.cpp"
	)
# The C code of the game the tests in vjk/ look at. The stub of Com_Error throws through it, so it is built with
# the tables for that.
set(TestCFiles
	"${SharedDir}/qcommon/q_string.c"
	"${SharedDir}/qcommon/q_math.c"
	"${SharedDir}/qcommon/q_color.c"
	"${SharedDir}/update/upd_manifest.c"
	"${SharedDir}/web/web_sha256.c"
	"${SharedDir}/web/web_base64.c"
	"${SharedDir}/web/web_missing.c"
	"${SharedDir}/sys/sys_memwatch_rules.c"
	"${MPDir}/qcommon/q_shared.c"
	"${MPDir}/game/voice/bg_voice.c"
	"${MPDir}/game/auth/bg_auth.c"
	"${MPDir}/game/boombox/bg_boombox_core.c"
	"${MPDir}/game/boombox/bg_boombox_fetch.c"
	)
if(NOT MSVC)
	set_source_files_properties(${TestCFiles} PROPERTIES COMPILE_OPTIONS "-fexceptions")
endif()
set(TestFiles ${TestFiles} ${TestCFiles})
if(MSVC)
	set(TestFiles
		${TestFiles}
		"${SharedDir}/visionjk.natvis"
		)
endif()
source_group( "tests" REGULAR_EXPRESSION ".*")
source_group( "tests\\safe" REGULAR_EXPRESSION "safe/.*" )
source_group( "tests\\vjk" REGULAR_EXPRESSION "vjk/.*" )
source_group( "qcommon\\safe" REGULAR_EXPRESSION "${SharedDir}/qcommon/safe/.*" )

if(MSVC)
	set( Boost_USE_STATIC_LIBS ON )
endif()
find_package( Boost COMPONENTS unit_test_framework REQUIRED )

set(TestTarget "UnitTests")
set(TestLibraries "${Boost_UNIT_TEST_FRAMEWORK_LIBRARY}")
set(TestIncludeDirectories
	"${Boost_INCLUDE_DIRS}"
	"${SharedDir}"
	"${MPDir}"
	"${GSLIncludeDirectory}"
	)
set(TestDefines "${SharedDefines}")

add_executable(${TestTarget} ${TestFiles})
set_target_properties(${TestTarget} PROPERTIES COMPILE_DEFINITIONS "${TestDefines}")
set_target_properties(${TestTarget} PROPERTIES INCLUDE_DIRECTORIES "${TestIncludeDirectories}")
set_target_properties(${TestTarget} PROPERTIES PROJECT_LABEL "Unit Tests")
target_link_libraries(${TestTarget} ${TestLibraries})
install(TARGETS ${TestTarget} DESTINATION ".")

if(NOT MSVC)
	#Generate a main function
	target_compile_definitions(${TestTarget} PRIVATE BOOST_TEST_DYN_LINK)
endif()

add_test(NAME unittests COMMAND ${TestTarget})
