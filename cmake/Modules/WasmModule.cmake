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

# The client for the browser (Emscripten). The engine is a main module, cgame, ui and the renderer are side
# modules of it, as they are libraries of their own on the desktop. Everything a browser has to fetch ends up
# side by side in one folder, which is what the dedicated server hands out (vs_clientWebRoot).
set(VisionJKWebDir "${CMAKE_BINARY_DIR}/web")

# names of functions in the modules: what a stack trace of the browser prints
set(VisionJKWasmLinkOptions --profiling-funcs)

function(vjk_wasm_main_module target jsLibrary)
	target_compile_options(${target} PRIVATE --use-port=sdl2)
	target_link_options(${target} PRIVATE
		${VisionJKWasmLinkOptions}
		--use-port=sdl2
		# every function of the C library stays in and is exported: the side modules bring none of their own
		-sMAIN_MODULE=1
		-sALLOW_MEMORY_GROWTH
		-sINITIAL_MEMORY=268435456
		-sMAXIMUM_MEMORY=4GB
		# the engine has buffers of tens of kilobytes on the stack (a packet, a config line), the default is 64 KB
		-sSTACK_SIZE=8MB
		-sMIN_WEBGL_VERSION=2
		-sMAX_WEBGL_VERSION=2
		-sFORCE_FILESYSTEM
		-lidbfs.js
		-lwebsocket.js
		# the page calls main() when the game files are in place
		-sINVOKE_RUN=0
		-sENVIRONMENT=web
		# IDBFS for the page: Emscripten 6.0.13 has no FS.filesystems any more
		-sEXPORTED_RUNTIME_METHODS=callMain,FS,IDBFS,addRunDependency,removeRunDependency,ccall,cwrap,UTF8ToString
		-sASSERTIONS=1
		--js-library "${jsLibrary}"
		)
	set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${jsLibrary}")
	set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${VisionJKWebDir}")
endfunction()

function(vjk_wasm_side_module target)
	target_link_options(${target} PRIVATE ${VisionJKWasmLinkOptions} -sSIDE_MODULE=1)
	set_target_properties(${target} PROPERTIES
		PREFIX ""
		SUFFIX ".wasm"
		LIBRARY_OUTPUT_DIRECTORY "${VisionJKWebDir}")
	# what -z defs does for the libraries of the desktop: a function nobody has fails the build, not the game
	if(BuildMPEngine)
		add_dependencies(${target} ${MPEngine})
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND "${CMAKE_CROSSCOMPILING_EMULATOR}" "${CMAKE_SOURCE_DIR}/cmake/Modules/CheckWasmImports.js"
				"$<TARGET_FILE_DIR:${MPEngine}>/$<TARGET_FILE_BASE_NAME:${MPEngine}>.wasm" "$<TARGET_FILE:${target}>"
			VERBATIM)
	endif()
endfunction()

# the files of the page, copied as they are
function(vjk_wasm_page dir)
	file(GLOB pageFiles CONFIGURE_DEPENDS "${dir}/*")
	set(outputs)
	foreach(file ${pageFiles})
		get_filename_component(name "${file}" NAME)
		add_custom_command(
			OUTPUT "${VisionJKWebDir}/${name}"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${file}" "${VisionJKWebDir}/${name}"
			DEPENDS "${file}"
			VERBATIM)
		list(APPEND outputs "${VisionJKWebDir}/${name}")
	endforeach()
	add_custom_target(webpage ALL DEPENDS ${outputs})

	# Installed next to the dedicated server, under the name vs_clientWebRoot has by default: "cmake --install
	# <build> --prefix <folder>" into the folder of a server install adds the client for the browser to it.
	install(DIRECTORY "${VisionJKWebDir}/"
		DESTINATION "${JKAInstallDir}/web"
		COMPONENT ${JKAMPClientComponent})
endfunction()
