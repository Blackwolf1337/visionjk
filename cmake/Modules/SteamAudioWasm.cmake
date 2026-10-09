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

# Steam Audio for the client in a browser. There is no library of Valve for WebAssembly to load, so the core
# of lib/steamaudio is built here, as a static library the engine is linked against (client/snd_spatial.cpp
# takes its functions from there, where the desktop takes them from the library it loads).
#
# Valve's own cmake files are not used: they find their dependencies in a folder their Python script fills,
# and they copy the result into the folders of the Unity, Unreal, FMOD and Wwise plugins, which would write
# into the submodule. What is built is what they build for WebAssembly (the platform IPL_OS_WASM of
# core/src/core/platform.h): the sources their CMakeLists names for the object library "core", the vector code
# for NEON through the header of Emscripten that maps it to the SIMD of WebAssembly, PFFFT for the FFT.
#
# Left out: Embree, Radeon Rays, TrueAudio Next, IPP, MKL and FFTS (none of them is there for WebAssembly,
# Valve's options for them are off on this platform too), and MySOFA with its zlib: it reads HRTF data from
# SOFA files, the engine only uses the data that is built into the library. Valve's switch for that is
# IPL_DISABLE_SOFA, and the header their precompiled header still asks for is an empty one of this tree.
#
# Two things are not in the submodule and are fetched, at the commits Valve's core/build/dependencies.json
# names and held against a checksum of this file: FlatBuffers (headers, and the compiler flatc for the schemas
# of the files Steam Audio writes) and PFFFT. flatc is built with Emscripten like everything else and run with
# the node of the SDK, so no compiler for the build machine is needed.
#
# The submodule stays as it is. lib/steamaudio-wasm has what is different here: thread_pool.cpp, which is
# built in place of Valve's file of that name (a page has no threads), and two patches, each laid over a copy of
# Valve's file in the build folder: simulation_manager.cpp.patch (a pass of the reflections that can be done in
# slices, see phonon_slices.h there) and hrtf_database.cpp.patch (an error of Valve's code for NEON that left
# the direction for headphones without its timing, see the patch). Patches for those two, since each changes a
# few lines of a long file that should follow the submodule, a file of its own for the thread pool, since
# nothing of Valve's file is left in it.

set(SteamAudioDeps "${CMAKE_BINARY_DIR}/steamaudio/deps" CACHE PATH "The sources Steam Audio needs to be built for WebAssembly, in the folders flatbuffers and pffft. They are fetched into it when they are not there")
set(SteamAudioFlatBuffersURL "" CACHE STRING "Where FetchSteamAudio gets the sources of FlatBuffers. Empty: the archive of the commit on GitHub. file:// for one that is on the disk already")
set(SteamAudioPFFFTURL "" CACHE STRING "Where FetchSteamAudio gets the sources of PFFFT. Empty: the archive of the commit on GitHub. file:// for one that is on the disk already")

# the archives this tree knows, by the commit GitHub made them of
set(SteamAudioDepSHA256_6df40a2471737b27271bdd9b900ab5f3aec746c7 "4aa372a30703e1801514c95281966bbd7c8fb4029ab73af0d911d43ef1d3ab11")	# flatbuffers
set(SteamAudioDepSHA256_e0bf595c98ded55cc457a371c1b29c8cab552628 "d80286fbbe128fcad14140d7c77e27d063c2388a898c130f7c2d63c25b55cdcd")	# pffft

# Puts the sources of one dependency into ${SteamAudioDeps}/<name>, if the file <has> is not there yet.
# Sets <found> in the scope of the caller.
function(jp_steamaudio_dep name repository urlOverride has found)
	set(dir "${SteamAudioDeps}/${name}")
	set(${found} FALSE PARENT_SCOPE)
	if(EXISTS "${dir}/${has}")
		set(${found} TRUE PARENT_SCOPE)
		return()
	endif()
	if(NOT FetchSteamAudio)
		message(STATUS "Steam Audio: no ${dir}/${has}, and FetchSteamAudio is off")
		return()
	endif()
	if(CMAKE_VERSION VERSION_LESS 3.19)
		message(STATUS "Steam Audio: this cmake can not read Valve's list of dependencies (3.19 and up can)")
		return()
	endif()
	file(READ "${SteamAudioCore}/build/dependencies.json" json)
	string(JSON commit ERROR_VARIABLE error GET "${json}" ${name} fetch tag)
	if(error OR NOT DEFINED SteamAudioDepSHA256_${commit})
		message(STATUS "Steam Audio: no checksum of ${name} at the commit lib/steamaudio wants (${commit}) in cmake/Modules/SteamAudioWasm.cmake, it is not fetched")
		return()
	endif()
	set(url "${urlOverride}")
	if(NOT url)
		set(url "https://github.com/${repository}/archive/${commit}.tar.gz")
	endif()
	set(archive "${CMAKE_BINARY_DIR}/steamaudio/${name}.tar.gz")
	message(STATUS "Steam Audio: fetching ${url} (once)")
	file(DOWNLOAD "${url}" "${archive}" STATUS status TLS_VERIFY ON)
	list(GET status 0 code)
	if(code)
		list(GET status 1 text)
		message(WARNING "Steam Audio: the download failed (${text})")
		file(REMOVE "${archive}")
		return()
	endif()
	file(SHA256 "${archive}" sum)
	if(NOT sum STREQUAL "${SteamAudioDepSHA256_${commit}}")
		message(WARNING "Steam Audio: ${url} is not the archive of ${name} this tree knows (its SHA256 is ${sum}), it is not used")
		file(REMOVE "${archive}")
		return()
	endif()
	# the archive has one folder, named after the repository and the commit
	set(unpacked "${CMAKE_BINARY_DIR}/steamaudio/unpacked-${name}")
	file(REMOVE_RECURSE "${unpacked}" "${dir}")
	file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${unpacked}")
	file(GLOB top "${unpacked}/*")
	file(MAKE_DIRECTORY "${SteamAudioDeps}")
	file(RENAME "${top}" "${dir}")
	file(REMOVE_RECURSE "${unpacked}")
	file(REMOVE "${archive}")
	if(EXISTS "${dir}/${has}")
		set(${found} TRUE PARENT_SCOPE)
	endif()
endfunction()

# Makes the static library of Steam Audio and names its targets in <libraries> of the caller's scope, or
# leaves that empty when something it needs is not there. Wants SteamAudioCore (lib/steamaudio/core) and
# ${CMAKE_BINARY_DIR}/steamaudio/phonon_version.h, both from codemp/CMakeLists.txt.
function(jp_steamaudio_wasm libraries)
	set(${libraries} "" PARENT_SCOPE)
	set(source "${SteamAudioCore}/src/core")
	set(own "${CMAKE_SOURCE_DIR}/lib/steamaudio-wasm")
	set(generated "${CMAKE_BINARY_DIR}/steamaudio/generated")

	find_package(Patch)
	if(NOT Patch_FOUND)
		message(STATUS "Steam Audio: no program \"patch\" for ${own}/*.patch")
		return()
	endif()
	jp_steamaudio_dep(flatbuffers "google/flatbuffers" "${SteamAudioFlatBuffersURL}" "include/flatbuffers/flatbuffers.h" haveFlatBuffers)
	jp_steamaudio_dep(pffft "marton78/pffft" "${SteamAudioPFFFTURL}" "pffft.c" havePFFFT)
	if(NOT haveFlatBuffers OR NOT havePFFFT)
		return()
	endif()
	set(flatbuffers "${SteamAudioDeps}/flatbuffers")
	set(pffft "${SteamAudioDeps}/pffft")

	# Nothing of this is the tree's own code: its warnings are not shown, with the one exception the build for
	# the browser has for every file (a call through a function pointer of another type traps there).
	set(quiet -Wno-everything -Wcast-function-type)
	# what Valve's CMakeLists and their dependencies.json give the compiler on this platform
	set(simd -msimd128 -mfpu=neon)
	# FlatBuffers at the commit Valve builds with has a function in a template of flatbuffers.h that no
	# compiler could ever build and nothing calls (TableKeyComparator::operator=). The clang of Emscripten 6
	# refuses the header for it. With this switch it reads the functions of a template when they are used,
	# which is what the compiler Valve builds with on Windows does, and the header stays as it was fetched.
	set(templates -fdelayed-template-parsing)

	# flatc, the compiler of FlatBuffers: the sources its own CMakeLists names (FlatBuffers_Compiler_SRCS). A
	# program for node that works on the real file system.
	file(READ "${flatbuffers}/CMakeLists.txt" text)
	string(REGEX MATCH "set\\(FlatBuffers_Library_SRCS[^)]*\\)" librarySources "${text}")
	string(REGEX MATCH "set\\(FlatBuffers_Compiler_SRCS[^)]*\\)" compilerSources "${text}")
	string(REGEX MATCHALL "[A-Za-z0-9_/]+\\.(cpp|cc)" flatcFiles "${librarySources} ${compilerSources}")
	if(NOT flatcFiles)
		message(STATUS "Steam Audio: ${flatbuffers}/CMakeLists.txt does not name the sources of flatc")
		return()
	endif()
	list(TRANSFORM flatcFiles PREPEND "${flatbuffers}/")
	add_executable(steamaudio_flatc EXCLUDE_FROM_ALL ${flatcFiles})
	target_include_directories(steamaudio_flatc PRIVATE "${flatbuffers}/include" "${flatbuffers}/grpc")
	target_compile_options(steamaudio_flatc PRIVATE ${quiet} ${templates} -O1)
	target_link_options(steamaudio_flatc PRIVATE -sNODERAWFS=1 -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH -sEXIT_RUNTIME=1)

	# the schemas of what Steam Audio keeps in files (the mesh of a scene, probes, the paths of a level)
	file(GLOB schemas CONFIGURE_DEPENDS "${source}/*.fbs")
	set(schemaHeaders)
	foreach(schema ${schemas})
		get_filename_component(name "${schema}" NAME)
		add_custom_command(
			OUTPUT "${generated}/${name}.h"
			COMMAND "${CMAKE_CROSSCOMPILING_EMULATOR}" "$<TARGET_FILE:steamaudio_flatc>" --cpp --scoped-enums --filename-suffix .fbs
				-o "${generated}/" "${schema}"
			DEPENDS steamaudio_flatc "${schema}"
			COMMENT "Steam Audio: ${name}"
			VERBATIM)
		list(APPEND schemaHeaders "${generated}/${name}.h")
	endforeach()
	add_custom_target(steamaudio_schemas DEPENDS ${schemaHeaders})

	# PFFFT, the FFT of Steam Audio where it has no other
	add_library(steamaudio_pffft STATIC EXCLUDE_FROM_ALL "${pffft}/pffft.c" "${pffft}/pffft_common.c")
	target_include_directories(steamaudio_pffft PUBLIC "${pffft}")
	target_compile_options(steamaudio_pffft PRIVATE ${quiet} ${simd})
	target_compile_definitions(steamaudio_pffft PRIVATE PFFFT_ENABLE_NEON=1 __arm__=1)

	# The core: what Valve's CMakeLists puts into its object library "core", and what its options add on a
	# platform without IPP, Embree and a profiler. Read from their file, so that another version of the
	# submodule brings its own list.
	file(READ "${source}/CMakeLists.txt" text)
	string(REGEX MATCH "add_library\\(core OBJECT[^)]*\\)" coreSources "${text}")
	string(REGEX MATCHALL "[A-Za-z0-9_/]+\\.(cpp|cc)" coreFiles "${coreSources}")
	if(NOT coreFiles)
		message(STATUS "Steam Audio: ${source}/CMakeLists.txt does not name the sources of the core")
		return()
	endif()
	list(APPEND coreFiles null_profiler.cpp array_math.cpp pffft_fft.cpp hrtf.cpp)
	# its threads are replaced, see lib/steamaudio-wasm/thread_pool.cpp, and SOFA files are not read
	list(REMOVE_ITEM coreFiles thread_pool.cpp sofa_hrtf_map.cpp)
	list(TRANSFORM coreFiles PREPEND "${source}/")
	file(GLOB ownFiles CONFIGURE_DEPENDS "${own}/*.cpp")

	# <name>.patch of this tree's folder: <name> of Valve's sources is built from a copy with the patch applied.
	# The copy is in another folder than the headers it includes by name, which the include path finds.
	file(GLOB patches CONFIGURE_DEPENDS "${own}/*.patch")
	foreach(patch ${patches})
		get_filename_component(name "${patch}" NAME_WLE)
		if(NOT "${source}/${name}" IN_LIST coreFiles)
			message(STATUS "Steam Audio: ${patch} is for a file the core is not built from")
			return()
		endif()
		# under its name only when the patch went in whole, each line where it expects it: a file that is half
		# patched would be built as if it were done
		add_custom_command(
			OUTPUT "${generated}/${name}"
			COMMAND ${CMAKE_COMMAND} -E copy "${source}/${name}" "${generated}/${name}.unpatched"
			COMMAND "${Patch_EXECUTABLE}" -s -F 0 "${generated}/${name}.unpatched" "${patch}"
			COMMAND ${CMAKE_COMMAND} -E rename "${generated}/${name}.unpatched" "${generated}/${name}"
			DEPENDS "${source}/${name}" "${patch}"
			COMMENT "Steam Audio: ${name} with ${name}.patch"
			VERBATIM)
		list(REMOVE_ITEM coreFiles "${source}/${name}")
		list(APPEND ownFiles "${generated}/${name}")
	endforeach()

	add_library(steamaudio STATIC EXCLUDE_FROM_ALL ${coreFiles} ${ownFiles})
	add_dependencies(steamaudio steamaudio_schemas)
	# ${own} holds the empty mysofa.h, and the folder of the version header also has the copy of phonon.h
	# the engine is built with: the same text as the one next to the sources
	target_include_directories(steamaudio PRIVATE "${source}" "${generated}" "${CMAKE_BINARY_DIR}/steamaudio" "${flatbuffers}/include" "${own}")
	target_compile_definitions(steamaudio PRIVATE STEAMAUDIO_BUILDING_CORE IPL_DISABLE_SOFA)
	# after the -std of the tree's flags, and so the one that counts: Valve builds it as C++14
	target_compile_options(steamaudio PRIVATE -std=c++14 ${quiet} ${simd} ${templates})
	# the sources count on it for the headers of the standard library
	target_precompile_headers(steamaudio PRIVATE "${source}/pch.h")
	target_link_libraries(steamaudio PUBLIC steamaudio_pffft)

	set(${libraries} steamaudio PARENT_SCOPE)
endfunction()
