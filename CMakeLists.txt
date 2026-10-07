#============================================================================
# Copyright (C) 2013 - 2018, OpenJK contributors
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

# The <min>...<policy_max> syntax is needed to remain compatible with 3.1, but also newer versions that dropped 3.1 compatibility
# More info here: https://cmake.org/cmake/help/latest/command/cmake_minimum_required.html
cmake_minimum_required(VERSION 3.1...3.31)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.9" CACHE STRING "Minimum OS X deployment version")

set(InOpenJK ON)

set(ProjectName "OpenJK" CACHE STRING "Project Name")
project(${ProjectName})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

#=============================================================================
#
# Customizable options
#
#=============================================================================
option(BuildPortableVersion "Build portable version (does not read or write files from your user/home directory" OFF)

option(BuildMPEngine "Whether to create projects for the MP client (openjk.exe)" ON)
option(BuildMPRdVanilla "Whether to create projects for the MP default renderer (rd-vanilla_x86_64.dll)" ON)
option(BuildMPDed "Whether to create projects for the MP dedicated server (openjkded.exe)" ON)
option(BuildMPGame "Whether to create projects for the MP server-side gamecode (jampgamex86_64.dll)" ON)
option(BuildMPCGame "Whether to create projects for the MP clientside gamecode (cgamex86_64.dll)" ON)
option(BuildMPUI "Whether to create projects for the MP UI code (uix86_64.dll)" ON)
option(BuildJPVoice "Whether the MP clientside gamecode gets the voice chat (Opus and miniaudio from lib/ are built into it)" ON)
option(BuildJPAssets "Whether to pack the directories of assets/ into pk3 files (needs Python 3)" ON)
option(BuildJPSpatialAudio "Whether the MP client can use Steam Audio for its sound (needs the submodule lib/steamaudio, the library is loaded at run time)" ON)
option(BuildMPRdVulkanVision "Whether to create projects for the MP Vulkan renderer (rd-vulkan-vision_x86_64.dll, needs the Vulkan headers and glslc)" ON)

option(BuildSPEngine "Whether to create projects for the SP engine (openjk_sp.exe)" OFF)
option(BuildSPGame "Whether to create projects for the SP gamecode (jagamex86_64.dll)" OFF)
option(BuildSPRdVanilla "Whether to create projects for the SP default renderer (rdsp-vanilla_x86_64.dll)" OFF)

option(BuildJK2SPEngine "Whether to create projects for the jk2 SP engine (openjo_sp.exe)" OFF)
option(BuildJK2SPGame "Whether to create projects for the jk2 sp gamecode mod (jk2gamex86_64.dll)" OFF)
option(BuildJK2SPRdVanilla "Whether to create projects for the jk2 sp renderer (rdjosp-vanilla_x86_64.dll)" OFF)

option(BuildTests "Whether to build automatic unit tests (requires Boost)" OFF)
option(UseAddressSanitizer "Whether to enable runtime address sanitizer" OFF)
option(UseUndefinedSanitizer "Whether to enable runtime Undefined Behavior sanitizer" OFF)

include(CMakeDependentOption)
cmake_dependent_option(BuildSymbolServer "Build WIP Windows Symbol Server (experimental and unused)" OFF "NOT WIN32 OR NOT MSVC" OFF)

# Configure the use of bundled libraries.  By default, we assume the user is on
# a platform that does not require any bundling.
#
# Note that we always use the bundled copy of minizip, since it is modified to
# use Z_Malloc.

set(UseInternalZlibDefault   OFF)
set(UseInternalPNGDefault    OFF)
set(UseInternalJPEGDefault   OFF)
set(UseInternalSDL2Default   OFF)

if(UseInternalLibs OR WIN32)
  set(UseInternalZlibDefault   ON)
  set(UseInternalPNGDefault    ON)
  set(UseInternalJPEGDefault   ON)
  set(UseInternalSDL2Default   ON)
endif()

if(APPLE)
  set(UseInternalJPEGDefault ON)
  set(UseInternalPNGDefault  ON)
endif()

option(UseInternalZlib   "If set, use bundled zlib."    ${UseInternalZlibDefault})
option(UseInternalPNG    "If set, use bundled libpng."  ${UseInternalPNGDefault})
option(UseInternalJPEG   "If set, use bundled libjpeg." ${UseInternalJPEGDefault})
option(UseInternalSDL2   "If set, use bundled SDL2."    ${UseInternalSDL2Default})

# This option won't appear on non-Apple platforms.
if(APPLE)
  option(MakeApplicationBundles "Whether to build .app application bundles for engines built" ON)
endif()

#=============================================================================
#
# Custom CMake Modules needed
#
#=============================================================================
list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_SOURCE_DIR}/cmake/Modules")

if(CMAKE_EXPORT_COMPILE_COMMANDS)
	add_custom_target(
		copy-compile-commands ALL
		${CMAKE_COMMAND} -E copy_if_different
		${CMAKE_BINARY_DIR}/compile_commands.json
		${CMAKE_CURRENT_LIST_DIR})
endif()

#=============================================================================
#
# Architecture/OS defines
#
#=============================================================================
# ${Architecture} must match ARCH_STRING in q_platform.h,
# and is used in DLL names (jagamex86_64.dll, jagamex86_64.dylib, jagamex86_64.so).

# This tree is built for 64 bit only. A toolchain file that forces the compiler leaves the size unset.
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
	message(FATAL_ERROR "32 bit builds are not supported, use a 64 bit compiler")
endif()

if(WIN32)
	set(X86 ON)
	set(Architecture "x86_64")
	set(WIN64 TRUE)
else()
	set(X86 OFF)
	if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
		set(Architecture "arm64")
		add_definitions(-DPNG_ARM_NEON_OPT=0)
	elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
		set(Architecture "arm")
	elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^x86.64$")
		set(X86 ON)
		set(Architecture "x86_64")
	elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "powerpc")
		set(Architecture "ppc")
	elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "powerpc64")
		set(Architecture "ppc64")
	else()
		set(Architecture "${CMAKE_SYSTEM_PROCESSOR}")
	endif()
endif()

message(STATUS "Architecture is ${Architecture}")

if(WIN32 AND CMAKE_VERSION VERSION_LESS "3.4")
	message(WARNING "Building on Windows platform with CMake version less than 3.4 is deprecated. Manifest file will fail to be included.")
endif()

#=============================================================================
#
# Binary names
#
#=============================================================================
set(SPEngine "openjk_sp.${Architecture}")
set(SPGame "jagame${Architecture}")
set(SPRDVanillaRenderer "rdsp-vanilla_${Architecture}")
set(MPEngine "openjk.${Architecture}")
set(MPVanillaRenderer "rd-vanilla_${Architecture}")
set(MPDed "openjkded.${Architecture}")
set(MPGame "jampgame${Architecture}")
set(MPCGame "cgame${Architecture}")
set(MPUI "ui${Architecture}")
set(MPVulkanVisionRenderer "rd-vulkan-vision_${Architecture}")
set(JK2SPEngine "openjo_sp.${Architecture}")
set(JK2SPGame "jospgame${Architecture}")
set(JK2SPVanillaRenderer "rdjosp-vanilla_${Architecture}")
set(AssetsPk3 "openjk-${Architecture}.pk3")
# Library names
set(MPBotLib "botlib")
set(SharedLib "shared")

#=============================================================================
#
# Paths
#
#=============================================================================
set(SPDir "${CMAKE_SOURCE_DIR}/code")
set(MPDir "${CMAKE_SOURCE_DIR}/codemp")
set(JK2SPDir "${CMAKE_SOURCE_DIR}/codeJK2")
set(SharedDir ${CMAKE_SOURCE_DIR}/shared)
set(OpenJKLibDir "${CMAKE_SOURCE_DIR}/lib")
set(GSLIncludeDirectory "${OpenJKLibDir}/gsl-lite/include")

include(InstallConfig)

#=============================================================================
#
# Compiler definitions/flags
#
#=============================================================================

# Operating system settings
if(WIN64)
	set(SharedDefines ${SharedDefines} "WIN64")
endif()

if (APPLE)
	set(SharedDefines ${SharedDefines} "MACOS_X")
endif()

if (NOT WIN32 AND NOT APPLE)
	set(SharedDefines ${SharedDefines} "ARCH_STRING=\"${Architecture}\"")
endif()

if(CMAKE_SYSTEM_NAME MATCHES "BSD")
  add_definitions(-DIOAPI_NO_64)
endif()

# Compiler settings
if(MSVC)
	set(SharedDefines ${SharedDefines} "NOMINMAX")
	set(SharedDefines ${SharedDefines} "_CRT_SECURE_NO_WARNINGS")
	set(SharedDefines ${SharedDefines} "_SCL_SECURE_NO_WARNINGS")
	set(SharedDefines ${SharedDefines} "_CRT_NONSTDC_NO_DEPRECATE")

	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP")

	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")

	# We don't try to control symbol visibility under MSVC.
	set(OPENJK_VISIBILITY_FLAGS "")
elseif (("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU") OR ("${CMAKE_C_COMPILER_ID}" MATCHES "Clang"))
	# I hope this doesn't come back to bite me in the butt later on.
	# Realistically though, can the C and CXX compilers be different?

	# Visibility can't be set project-wide -- it needs to be specified on a
	# per-target basis.  This is primarily due to the bundled copy of ZLib.
	# ZLib explicitly declares symbols hidden, rather than defaulting to hidden.
	#
	# Note that -fvisibility=hidden is stronger than -fvisibility-inlines-hidden.
	set(OPENJK_VISIBILITY_FLAGS "-fvisibility=hidden")

	# removes the -rdynamic flag at linking (which causes crashes for some reason)
	set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")
	set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")

	if(UseAddressSanitizer)
		# also raise stack size drastically (to 64MiB), since the sanitizer adds overhead to stack frames
		add_compile_options(-fsanitize=address)
		add_link_options(-fsanitize=address -z stack-size=4000000)
	endif()
	if(UseUndefinedSanitizer)
		add_compile_options(-fsanitize=undefined)
		add_link_options(-fsanitize=undefined)
	endif()

	# additional flags for debug configuration
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb")

	if (X86)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse2")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2")
	endif()

	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

	# enable somewhat modern C++
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

	if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-comment")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsigned-char")
		if (X86)
			# "x86 vm will crash without -mstackrealign since MMX
			# instructions will be used no matter what and they
			# corrupt the frame pointer in VM calls"
			# -ioquake3 Makefile
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mstackrealign")
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mfpmath=sse")
		endif()

		if(WIN32)
			# Link libgcc and libstdc++ statically
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static-libgcc")
			set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc")
			set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++")
		endif()
	elseif("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-comment")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsigned-char")
	endif()

	if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-invalid-offsetof")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-write-strings")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-comment")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsigned-char")
		if (X86)
			set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mstackrealign")
			set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mfpmath=sse")
		endif()
	elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-write-strings")
		#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-writable-strings")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-comment")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-invalid-offsetof")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsigned-char")
	endif()

	# Security hardening. Not with the sanitizers, which bring their own checks and clash with _FORTIFY_SOURCE.
	if(NOT UseAddressSanitizer AND NOT UseUndefinedSanitizer)
		# warn about format strings that are not constant, on every platform (compile only, no new dependency)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wformat -Wformat-security")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wformat -Wformat-security")
		if(WIN32)
			# the PE flags: data execution prevention and address space layout randomisation. No stack protector
			# here: on mingw it would pull in libssp-0.dll, a new runtime file the modules must not depend on.
			add_link_options(-Wl,--nxcompat -Wl,--dynamicbase)
		else()
			# a canary on functions with buffers (the runtime is in libc, no new dependency), checked libc calls
			# (needs optimisation, so only in the optimised configs), and a read-only GOT bound at start
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-strong")
			set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fstack-protector-strong")
			set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -D_FORTIFY_SOURCE=2")
			set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -D_FORTIFY_SOURCE=2")
			set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -D_FORTIFY_SOURCE=2")
			set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -D_FORTIFY_SOURCE=2")
			add_link_options(-Wl,-z,relro -Wl,-z,now)
		endif()
	endif()
else()
	message(ERROR "Unsupported compiler")
endif()

set(SharedDefines ${SharedDefines} "$<$<CONFIG:Debug>:_DEBUG>")
set(SharedDefines ${SharedDefines} "$<$<NOT:$<CONFIG:Debug>>:FINAL_BUILD>")

if(BuildPortableVersion)
	set(BUILD_PORTABLE ON)
endif()

#=============================================================================
#
# Generate version file
#
#=============================================================================

# Reproducible builds
# https://reproducible-builds.org/specs/source-date-epoch/
if ("$ENV{SOURCE_DATE_EPOCH}" STREQUAL "")
	message(STATUS "SOURCE_DATE_EPOCH is not set: SOURCE_DATE will be set to the compile-time date")
else()
	execute_process(
		COMMAND "date" "--date=@$ENV{SOURCE_DATE_EPOCH}" "+%b %_d %Y"
		OUTPUT_VARIABLE SOURCE_DATE
		ERROR_QUIET
		OUTPUT_STRIP_TRAILING_WHITESPACE)
	message(STATUS "SOURCE_DATE_EPOCH is set ($ENV{SOURCE_DATE_EPOCH}): SOURCE_DATE set to \"${SOURCE_DATE}\"")
endif()

# Current Git tag/version
# ideally we would separate the hash suffix into its own variable, but sed is not available everywhere
execute_process(
	COMMAND "git" "describe" "--tag"
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	OUTPUT_VARIABLE GIT_TAG
	RESULT_VARIABLE GIT_TAG_ERROR
	OUTPUT_STRIP_TRAILING_WHITESPACE)
if(GIT_TAG_ERROR AND NOT GIT_TAG_ERROR EQUAL 0)
	message(WARNING "Unable to determine Git tag")
	set(GIT_TAG vUNKNOWN)
endif()
message(STATUS "Git tag is ${GIT_TAG}")

configure_file(shared/qcommon/q_version.h.in shared/qcommon/q_version.h @ONLY)

#=============================================================================

# Files shared across all projects
#
#=============================================================================
set(SharedCommonFiles
	"${SharedDir}/qcommon/q_color.h"
	"${SharedDir}/qcommon/q_color.c"
	"${SharedDir}/qcommon/q_math.h"
	"${SharedDir}/qcommon/q_math.c"
	"${SharedDir}/qcommon/q_string.h"
	"${SharedDir}/qcommon/q_string.c"
	"${SharedDir}/qcommon/q_platform.h"
	"${CMAKE_BINARY_DIR}/shared/qcommon/q_version.h"
	)
set(SharedCommonSafeFiles
	"${SharedDir}/qcommon/safe/gsl.h"
	"${SharedDir}/qcommon/safe/string.cpp"
	"${SharedDir}/qcommon/safe/string.h"
	"${SharedDir}/qcommon/safe/sscanf.h"
	"${SharedDir}/qcommon/safe/limited_vector.h"
	)

#=============================================================================
#
# Adding all of the projects
#
#=============================================================================
if(UseInternalJPEG)
  add_subdirectory(lib/jpeg-9a)
else()
  find_package(JPEG REQUIRED)
endif()

if(UseInternalZlib)
  add_subdirectory(lib/zlib)
else()
  find_package(ZLIB REQUIRED)
endif()

if(UseInternalPNG)
  add_subdirectory(lib/libpng)
else()
  find_package(PNG REQUIRED)
endif()

# Always use bundled minizip (sets MINIZIP_{LIBRARIES,INCLUDE_DIR})
add_subdirectory(lib/minizip)

# Add projects
add_subdirectory(${SPDir})
if(BuildJK2SPGame)
	add_subdirectory("${JK2SPDir}/game")
endif()

add_subdirectory(${MPDir})

if(BuildJPAssets)
	add_subdirectory(assets)
endif()

if(BuildSymbolServer)
	add_subdirectory("tools/WinSymbol")
endif()

if(BuildTests)
	enable_testing()
	add_subdirectory("tests")
endif()
