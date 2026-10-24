#============================================================================
# Copyright (C) 2026, OpenJK contributors
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

# rd-vulkan-vision has a shared part and two devices (vk_local.h says which file is which), and this file builds it
# up to two times:
# - with the Vulkan device as ${MPVulkanVisionRenderer} (BuildMPRdVulkanVision), the renderer of the client
# - with the OpenGL ES 3 device as ${MPVisionGLESRenderer} (BuildMPRdVisionGLES), a library for the desktop to work
#   on that device with
# For a browser (Emscripten) there is no Vulkan. There BuildMPRdVulkanVision builds the renderer with the OpenGL ES
# device, under the name the engine asks for.
set(MPVulkanVisionVulkanTarget)
set(MPVulkanVisionGLESTarget)
if(EMSCRIPTEN)
	if(BuildMPRdVulkanVision)
		set(MPVulkanVisionGLESTarget ${MPVulkanVisionRenderer})
	endif()
else()
	if(BuildMPRdVulkanVision)
		set(MPVulkanVisionVulkanTarget ${MPVulkanVisionRenderer})
	endif()
	if(BuildMPRdVisionGLES)
		set(MPVulkanVisionGLESTarget ${MPVisionGLESRenderer})
	endif()
endif()

# Only the headers of Vulkan are needed: the library is loaded by the window code at run time (vk_funcs.h).
# -DVulkanHeadersDir=<dir with vulkan/vulkan.h> names where they are if they are not found.
# The renderer is built by default, so a machine without the headers or without glslc is told what is missing
# and builds the rest, as with the other things this tree builds by default.
if(MPVulkanVisionVulkanTarget)
	find_path(VulkanHeadersDir vulkan/vulkan.h)
	if(NOT VulkanHeadersDir AND CMAKE_CROSSCOMPILING AND EXISTS "/usr/include/vulkan/vulkan.h")
		set(VulkanHeadersDir "/usr/include" CACHE PATH "Vulkan headers" FORCE)
	endif()
	find_program(GLSLC glslc)
	if(NOT VulkanHeadersDir)
		message(WARNING "rd-vulkan-vision is not built: it needs the Vulkan headers (vulkan/vulkan.h), set VulkanHeadersDir. -DBuildMPRdVulkanVision=OFF silences this.")
		set(MPVulkanVisionVulkanTarget)
	elseif(NOT GLSLC)
		message(WARNING "rd-vulkan-vision is not built: it needs glslc (package shaderc) to compile its shaders. -DBuildMPRdVulkanVision=OFF silences this.")
		set(MPVulkanVisionVulkanTarget)
	elseif(CMAKE_CROSSCOMPILING)
		# The headers are the same for every platform, so the ones of the host do. But their directory may be the
		# include directory of the host, which a cross compiler must not see: it gets a copy of the two directories.
		file(COPY "${VulkanHeadersDir}/vulkan" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/vulkan-headers")
		if(EXISTS "${VulkanHeadersDir}/vk_video")
			file(COPY "${VulkanHeadersDir}/vk_video" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/vulkan-headers")
		endif()
		set(MPVulkanVisionVulkanHeaders "${CMAKE_CURRENT_BINARY_DIR}/vulkan-headers")
	else()
		set(MPVulkanVisionVulkanHeaders "${VulkanHeadersDir}")
	endif()
endif()
# The library for the desktop links the OpenGL ES of the system. A browser has it in the engine, see below.
if(MPVulkanVisionGLESTarget AND NOT EMSCRIPTEN)
	find_path(GLES3HeadersDir GLES3/gl3.h)
	find_library(GLESv2Library GLESv2)
	if(NOT GLES3HeadersDir OR NOT GLESv2Library)
		message(WARNING "${MPVisionGLESRenderer} is not built: it needs the headers and the library of OpenGL ES 3 (GLES3/gl3.h, libGLESv2). -DBuildMPRdVisionGLES=OFF silences this.")
		set(MPVulkanVisionGLESTarget)
	endif()
endif()
if(NOT MPVulkanVisionVulkanTarget AND NOT MPVulkanVisionGLESTarget)
	return()
endif()

set(MPVulkanVisionIncludeDirectories
	${MPDir}
	${SharedDir}
	"${MPDir}/rd-vulkan-vision"
	"${MPDir}/rd-vanilla"
	"${GSLIncludeDirectory}"
	"${CMAKE_CURRENT_BINARY_DIR}"
	)

# what both devices share: which batch is drawn how and in which order
set(MPVulkanVisionSharedFiles
	"${MPDir}/rd-vulkan-vision/vk_hooks.h"
	"${MPDir}/rd-vulkan-vision/vk_local.h"
	"${MPDir}/rd-vulkan-vision/vk_backend.cpp"
	"${MPDir}/rd-vulkan-vision/vk_frame.cpp"
	"${MPDir}/rd-vulkan-vision/vk_gl.cpp"
	"${MPDir}/rd-vulkan-vision/vk_image.cpp"
	"${MPDir}/rd-vulkan-vision/vk_texcache.cpp"
	"${MPDir}/rd-vulkan-vision/vk_model.cpp"
	"${MPDir}/rd-vulkan-vision/vk_post.cpp"
	"${MPDir}/rd-vulkan-vision/vk_shade.cpp"
	"${MPDir}/rd-vulkan-vision/vk_stubs.cpp"
	"${MPDir}/rd-vulkan-vision/vk_world.cpp"
	)
source_group("renderer" FILES ${MPVulkanVisionSharedFiles})
# The Vulkan device. vk_bc.cpp is with it because the other device does not take textures in blocks yet, and its
# worker threads are nothing a browser has.
set(MPVulkanVisionVulkanFiles
	"${MPDir}/rd-vulkan-vision/vk_device.h"
	"${MPDir}/rd-vulkan-vision/vk_funcs.h"
	"${MPDir}/rd-vulkan-vision/vk_bc.cpp"
	"${MPDir}/rd-vulkan-vision/vk_cmd.cpp"
	"${MPDir}/rd-vulkan-vision/vk_main.cpp"
	"${MPDir}/rd-vulkan-vision/vk_memory.cpp"
	"${MPDir}/rd-vulkan-vision/vk_pipeline.cpp"
	"${MPDir}/rd-vulkan-vision/vk_texture.cpp"
	)
source_group("vulkan" FILES ${MPVulkanVisionVulkanFiles})
# the OpenGL ES device
set(MPVulkanVisionGLESFiles
	"${MPDir}/rd-vulkan-vision/gles/gles_device.h"
	"${MPDir}/rd-vulkan-vision/gles/gles_gl.h"
	"${MPDir}/rd-vulkan-vision/gles/gles_buffer.cpp"
	"${MPDir}/rd-vulkan-vision/gles/gles_cmd.cpp"
	"${MPDir}/rd-vulkan-vision/gles/gles_main.cpp"
	"${MPDir}/rd-vulkan-vision/gles/gles_program.cpp"
	"${MPDir}/rd-vulkan-vision/gles/gles_texture.cpp"
	)
source_group("gles" FILES ${MPVulkanVisionGLESFiles})

# the files of rd-vanilla without graphics code: loaders, shader parser, Ghoul2, scene. Built from where they
# are, with RD_VULKAN_VISION defined, so that there is one copy of them.
set(MPVulkanVisionVanillaFiles
	"${MPDir}/rd-vanilla/G2_API.cpp"
	"${MPDir}/rd-vanilla/G2_bolts.cpp"
	"${MPDir}/rd-vanilla/G2_bones.cpp"
	"${MPDir}/rd-vanilla/G2_misc.cpp"
	"${MPDir}/rd-vanilla/G2_surfaces.cpp"
	"${MPDir}/rd-vanilla/tr_bsp.cpp"
	"${MPDir}/rd-vanilla/tr_cmds.cpp"
	"${MPDir}/rd-vanilla/tr_curve.cpp"
	"${MPDir}/rd-vanilla/tr_decals.cpp"
	"${MPDir}/rd-vanilla/tr_ghoul2.cpp"
	"${MPDir}/rd-vanilla/tr_image.cpp"
	"${MPDir}/rd-vanilla/tr_init.cpp"
	"${MPDir}/rd-vanilla/tr_light.cpp"
	"${MPDir}/rd-vanilla/tr_local.h"
	"${MPDir}/rd-vanilla/tr_main.cpp"
	"${MPDir}/rd-vanilla/tr_marks.cpp"
	"${MPDir}/rd-vanilla/tr_mesh.cpp"
	"${MPDir}/rd-vanilla/tr_model.cpp"
	"${MPDir}/rd-vanilla/tr_scene.cpp"
	"${MPDir}/rd-vanilla/tr_shade_calc.cpp"
	"${MPDir}/rd-vanilla/tr_shader.cpp"
	"${MPDir}/rd-vanilla/tr_skin.cpp"
	"${MPDir}/rd-vanilla/tr_sky.cpp"
	"${MPDir}/rd-vanilla/tr_quicksprite.cpp"
	"${MPDir}/rd-vanilla/tr_quicksprite.h"
	"${MPDir}/rd-vanilla/tr_subs.cpp"
	"${MPDir}/rd-vanilla/tr_surface.cpp"
	"${MPDir}/rd-vanilla/tr_shadows.cpp"
	"${MPDir}/rd-vanilla/tr_surfacesprites.cpp"
	"${MPDir}/rd-vanilla/tr_world.cpp"
	"${MPDir}/rd-vanilla/tr_WorldEffects.cpp"
	"${MPDir}/rd-vanilla/tr_WorldEffects.h"
	)
source_group("rd-vanilla" FILES ${MPVulkanVisionVanillaFiles})
set(MPVulkanVisionFiles ${MPVulkanVisionSharedFiles} ${MPVulkanVisionVanillaFiles})

set(MPVulkanVisionGhoul2Files
	"${MPDir}/ghoul2/g2_local.h"
	"${MPDir}/ghoul2/ghoul2_shared.h"
	"${MPDir}/ghoul2/G2_gore.cpp"
	"${MPDir}/ghoul2/G2_gore.h")
source_group("ghoul2" FILES ${MPVulkanVisionGhoul2Files})
set(MPVulkanVisionFiles ${MPVulkanVisionFiles} ${MPVulkanVisionGhoul2Files})

set(MPVulkanVisionRdCommonFiles
	"${MPDir}/rd-common/mdx_format.h"
	"${MPDir}/rd-common/tr_common.h"
	"${MPDir}/rd-common/tr_font.cpp"
	"${MPDir}/rd-common/tr_font.h"
	"${MPDir}/rd-common/tr_font_ttf.cpp"
	"${MPDir}/rd-common/tr_font_ttf.h"
	"${MPDir}/rd-common/tr_image_load.cpp"
	"${MPDir}/rd-common/tr_image_jpg.cpp"
	"${MPDir}/rd-common/tr_image_tga.cpp"
	"${MPDir}/rd-common/tr_image_png.cpp"
	"${MPDir}/rd-common/tr_noise.cpp"
	"${MPDir}/rd-common/tr_public.h"
	"${MPDir}/rd-common/tr_types.h")
source_group("rd-common" FILES ${MPVulkanVisionRdCommonFiles})
set(MPVulkanVisionFiles ${MPVulkanVisionFiles} ${MPVulkanVisionRdCommonFiles})

set(MPVulkanVisionCommonFiles
	"${MPDir}/qcommon/matcomp.cpp"
	"${MPDir}/qcommon/q_shared.cpp"
	${SharedCommonFiles})
source_group("common" FILES ${MPVulkanVisionCommonFiles})
set(MPVulkanVisionFiles ${MPVulkanVisionFiles} ${MPVulkanVisionCommonFiles} ${SharedCommonSafeFiles})

# The shaders, one set of sources for both devices (shaders/dialect.glsl). Both libraries include what is made of
# them here, so a target of its own makes it and the libraries wait for that: a command whose output two targets
# of one directory list is run for each of them, at the same time.
set(MPVulkanVisionShaders
	generic.vert
	skinned.vert
	generic.frag
	post.vert
	gamma.frag
	blur.frag
	copy.frag
	focus.frag
	scopeprep.frag
	scope.frag
	)
set(MPVulkanVisionShaderIncludes
	"${MPDir}/rd-vulkan-vision/shaders/common.glsl"
	"${MPDir}/rd-vulkan-vision/shaders/dialect.glsl"
	"${MPDir}/rd-vulkan-vision/shaders/post.glsl"
	"${MPDir}/rd-vulkan-vision/shaders/stage.glsl"
	)
set(MPVulkanVisionShaderOutputs)
if(MPVulkanVisionVulkanTarget)
	# GLSL to SPIR-V as a C initializer list, one .inc per shader, included by vk_pipeline.cpp. glslc runs on the
	# build machine, so this also works when cross compiling.
	foreach(shader ${MPVulkanVisionShaders})
		set(shaderIn "${MPDir}/rd-vulkan-vision/shaders/${shader}")
		string(REPLACE "." "_" shaderName ${shader})
		set(shaderOut "${CMAKE_CURRENT_BINARY_DIR}/shaders/${shaderName}.inc")
		add_custom_command(
			OUTPUT ${shaderOut}
			COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
			COMMAND ${GLSLC} --target-env=vulkan1.3 -O -mfmt=c -I "${MPDir}/rd-vulkan-vision/shaders" -o ${shaderOut} ${shaderIn}
			DEPENDS ${shaderIn} ${MPVulkanVisionShaderIncludes}
			COMMENT "glslc ${shader}"
			VERBATIM)
		list(APPEND MPVulkanVisionShaderOutputs ${shaderOut})
	endforeach()
endif()

# The same sources as GLSL ES 3.00 for the OpenGL ES device, which compiles them itself, so they go into the
# library as text: one file a shader with its includes put in (cmake/Modules/BundleShader.cmake), as a C array with
# a zero at its end, and gles_prefix.glsl, the lines the device puts in front of each (gles/gles_program.cpp).
# They are made in every build, also in one of the Vulkan device alone, so that a change to a shader that is not
# GLSL ES fails here and not in a browser: glslangValidator compiles each file with the prefix in front and links
# the pairs that make a program, which is where GLSL ES finds the varyings of a stage by their names.
set(MPVulkanVisionShaderPrefix "${MPDir}/rd-vulkan-vision/shaders/gles_prefix.glsl")
set(MPVulkanVisionShaderCheck)
find_program(GLSLANG_VALIDATOR glslangValidator)
if(GLSLANG_VALIDATOR)
	set(MPVulkanVisionShaderCheck "-DVALIDATOR=${GLSLANG_VALIDATOR}" "-DPREFIX=${MPVulkanVisionShaderPrefix}")
else()
	message(WARNING "rd-vulkan-vision: the shaders are not checked as GLSL ES, that needs glslangValidator (package glslang).")
endif()
foreach(shader ${MPVulkanVisionShaders})
	set(shaderIn "${MPDir}/rd-vulkan-vision/shaders/${shader}")
	string(REPLACE "." "_" shaderName ${shader})
	string(REGEX REPLACE ".*\\." "" shaderStage ${shader})
	set(shaderBundle "${CMAKE_CURRENT_BINARY_DIR}/shaders/${shaderName}_es.glsl")
	set(shaderOut "${CMAKE_CURRENT_BINARY_DIR}/shaders/${shaderName}_es.inc")
	add_custom_command(
		OUTPUT ${shaderBundle}
		COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
		COMMAND ${CMAKE_COMMAND} "-DINPUT=${shaderIn}" "-DOUTPUT=${shaderBundle}" ${MPVulkanVisionShaderCheck} -DSTAGE=${shaderStage}
			-P "${CMAKE_SOURCE_DIR}/cmake/Modules/BundleShader.cmake"
		DEPENDS ${shaderIn} ${MPVulkanVisionShaderIncludes} ${MPVulkanVisionShaderPrefix} "${CMAKE_SOURCE_DIR}/cmake/Modules/BundleShader.cmake"
		COMMENT "GLSL ES ${shader}"
		VERBATIM)
	add_custom_command(
		OUTPUT ${shaderOut}
		COMMAND ${CMAKE_COMMAND} "-DINPUT=${shaderBundle}" "-DOUTPUT=${shaderOut}" -DNAME=vk_es_${shaderName}
			-P "${CMAKE_SOURCE_DIR}/cmake/Modules/EmbedFile.cmake"
		DEPENDS ${shaderBundle} "${CMAKE_SOURCE_DIR}/cmake/Modules/EmbedFile.cmake"
		COMMENT "Embedding GLSL ES ${shader}"
		VERBATIM)
	list(APPEND MPVulkanVisionShaderOutputs ${shaderOut})
endforeach()
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/prefix_es.inc"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
	COMMAND ${CMAKE_COMMAND} "-DINPUT=${MPVulkanVisionShaderPrefix}" "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/shaders/prefix_es.inc" -DNAME=vk_es_prefix
		-P "${CMAKE_SOURCE_DIR}/cmake/Modules/EmbedFile.cmake"
	DEPENDS ${MPVulkanVisionShaderPrefix} "${CMAKE_SOURCE_DIR}/cmake/Modules/EmbedFile.cmake"
	COMMENT "Embedding GLSL ES prefix"
	VERBATIM)
list(APPEND MPVulkanVisionShaderOutputs "${CMAKE_CURRENT_BINARY_DIR}/shaders/prefix_es.inc")
if(GLSLANG_VALIDATOR)
	# vertex and fragment shader of every program, as vk_pipeline.cpp puts them together
	foreach(program generic_vert:generic_frag skinned_vert:generic_frag post_vert:gamma_frag post_vert:blur_frag post_vert:copy_frag post_vert:focus_frag post_vert:scopeprep_frag post_vert:scope_frag)
		string(REPLACE ":" ";" programStages ${program})
		list(GET programStages 0 programVert)
		list(GET programStages 1 programFrag)
		set(programOut "${CMAKE_CURRENT_BINARY_DIR}/shaders/${programVert}_${programFrag}_es.linked")
		add_custom_command(
			OUTPUT ${programOut}
			COMMAND ${GLSLANG_VALIDATOR} --quiet -l --validate-io "${CMAKE_CURRENT_BINARY_DIR}/shaders/${programVert}_es.glsl.vert"
				"${CMAKE_CURRENT_BINARY_DIR}/shaders/${programFrag}_es.glsl.frag"
			COMMAND ${CMAKE_COMMAND} -E touch ${programOut}
			DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/shaders/${programVert}_es.glsl" "${CMAKE_CURRENT_BINARY_DIR}/shaders/${programFrag}_es.glsl"
			COMMENT "GLSL ES ${programVert} with ${programFrag}"
			VERBATIM)
		list(APPEND MPVulkanVisionShaderOutputs ${programOut})
	endforeach()
endif()
add_custom_target(rd-vulkan-vision-shaders DEPENDS ${MPVulkanVisionShaderOutputs})

# Transparently use either bundled or system libjpeg, libpng, zlib and our bundled minizip, as rd-vanilla does.
set(MPVulkanVisionImageIncludeDirectories)
set(MPVulkanVisionLibraries)
list(APPEND MPVulkanVisionImageIncludeDirectories ${JPEG_INCLUDE_DIR})
list(APPEND MPVulkanVisionLibraries               ${JPEG_LIBRARIES})
list(APPEND MPVulkanVisionImageIncludeDirectories ${PNG_INCLUDE_DIRS})
list(APPEND MPVulkanVisionLibraries               ${PNG_LIBRARIES})
list(APPEND MPVulkanVisionImageIncludeDirectories ${ZLIB_INCLUDE_DIR})
list(APPEND MPVulkanVisionLibraries               ${ZLIB_LIBRARIES})
list(APPEND MPVulkanVisionImageIncludeDirectories ${MINIZIP_INCLUDE_DIRS})
list(APPEND MPVulkanVisionLibraries               ${MINIZIP_LIBRARIES})

# One library of the renderer: the shared files with the files of a device (ARGN). defines: what it is compiled
# with beyond the defines of the tree.
function(vision_renderer target label defines)
	add_library(${target} SHARED ${MPVulkanVisionFiles} ${ARGN})
	add_dependencies(${target} rd-vulkan-vision-shaders)

	if(NOT MSVC)
		# remove "lib" prefix for .so/.dylib files
		set_target_properties(${target} PROPERTIES PREFIX "")
	endif()

	if(EMSCRIPTEN)
		# the folder the module is built into is installed as a whole, see WasmModule.cmake
	elseif(WIN32)
		install(TARGETS ${target}
			RUNTIME
			DESTINATION ${JKAInstallDir}
			COMPONENT ${JKAMPClientComponent})
	else()
		if(MakeApplicationBundles AND BuildMPEngine)
			install(TARGETS ${target}
				LIBRARY
				DESTINATION "${JKAInstallDir}/${MPEngine}.app/Contents/MacOS/"
				COMPONENT ${JKAMPClientComponent})
		else()
			install(TARGETS ${target}
				LIBRARY
				DESTINATION ${JKAInstallDir}
				COMPONENT ${JKAMPClientComponent})
		endif()
	endif()

	set_target_properties(${target} PROPERTIES COMPILE_DEFINITIONS "${SharedDefines};${defines}")

	# Hide symbols not explicitly marked public.
	set_property(TARGET ${target} APPEND PROPERTY COMPILE_OPTIONS ${VISIONJK_VISIBILITY_FLAGS})

	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		# an OpenGL call left in a shared file must fail the build, not the game
		set_property(TARGET ${target} APPEND PROPERTY LINK_OPTIONS "-Wl,-z,defs")
	endif()

	set_target_properties(${target} PROPERTIES PROJECT_LABEL "${label}")
endfunction()

if(MPVulkanVisionVulkanTarget)
	vision_renderer(${MPVulkanVisionVulkanTarget} "MP Vulkan Vision Renderer" "RD_VULKAN_VISION" ${MPVulkanVisionVulkanFiles})

	# the worker threads that pack textures into blocks (vk_bc.cpp)
	find_package(Threads REQUIRED)
	# tr_local.h of rd-vanilla pulls in the OpenGL headers for its types. Nothing of OpenGL is linked.
	find_package(OpenGL REQUIRED)
	set_target_properties(${MPVulkanVisionVulkanTarget} PROPERTIES INCLUDE_DIRECTORIES
		"${MPVulkanVisionIncludeDirectories};${MPVulkanVisionVulkanHeaders};${MPVulkanVisionImageIncludeDirectories};${OPENGL_INCLUDE_DIR};${VisionJKLibDir}")
	target_link_libraries(${MPVulkanVisionVulkanTarget} ${MPVulkanVisionLibraries} Threads::Threads)
endif()

if(MPVulkanVisionGLESTarget)
	# RD_VISION_GLES: vk_local.h takes the header of this device, and rd-vanilla/qgl.h the names of OpenGL ES
	vision_renderer(${MPVulkanVisionGLESTarget} "MP Vision Renderer with OpenGL ES" "RD_VULKAN_VISION;RD_VISION_GLES" ${MPVulkanVisionGLESFiles})

	if(EMSCRIPTEN)
		# a module of the engine, which has every function of OpenGL ES 3 that Emscripten knows (WebGL2)
		set_target_properties(${MPVulkanVisionGLESTarget} PROPERTIES INCLUDE_DIRECTORIES
			"${MPVulkanVisionIncludeDirectories};${MPVulkanVisionImageIncludeDirectories};${VisionJKLibDir}")
		target_link_libraries(${MPVulkanVisionGLESTarget} ${MPVulkanVisionLibraries})
		include(WasmModule)
		vjk_wasm_side_module(${MPVulkanVisionGLESTarget})
		# The libraries for JPEG, PNG and zlib are built with their symbols visible, so the module would look for its
		# own tables and functions (jpeg_natural_order, jpeg_idct_islow, ...) by name in the table of symbols of the
		# page. That table keeps the addresses of the first load of a module: after a vid_restart the new renderer
		# read the tables of the old one, whose memory was given back, and every JPEG came out as noise.
		target_link_options(${MPVulkanVisionGLESTarget} PRIVATE -Wl,-Bsymbolic)
	else()
		set_target_properties(${MPVulkanVisionGLESTarget} PROPERTIES INCLUDE_DIRECTORIES
			"${MPVulkanVisionIncludeDirectories};${GLES3HeadersDir};${MPVulkanVisionImageIncludeDirectories};${VisionJKLibDir}")
		target_link_libraries(${MPVulkanVisionGLESTarget} ${MPVulkanVisionLibraries} ${GLESv2Library})
	endif()
endif()
