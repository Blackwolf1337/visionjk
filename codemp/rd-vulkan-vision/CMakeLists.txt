#============================================================================
# Copyright (C) 2026, OpenJK contributors
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
if(NOT InOpenJK)
	message(FATAL_ERROR "Use the top-level cmake script!")
endif(NOT InOpenJK)

# Only the headers of Vulkan are needed: the library is loaded by the window code at run time (vk_funcs.h).
# -DVulkanHeadersDir=<dir with vulkan/vulkan.h> names where they are if they are not found.
find_path(VulkanHeadersDir vulkan/vulkan.h)
if(NOT VulkanHeadersDir AND CMAKE_CROSSCOMPILING AND EXISTS "/usr/include/vulkan/vulkan.h")
	set(VulkanHeadersDir "/usr/include" CACHE PATH "Vulkan headers" FORCE)
endif()
if(NOT VulkanHeadersDir)
	message(FATAL_ERROR "rd-vulkan-vision needs the Vulkan headers (vulkan/vulkan.h), set VulkanHeadersDir")
endif()
if(CMAKE_CROSSCOMPILING)
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
find_program(GLSLC glslc)
if(NOT GLSLC)
	message(FATAL_ERROR "rd-vulkan-vision needs glslc (package shaderc) to compile its shaders")
endif()

set(MPVulkanVisionIncludeDirectories
	${MPDir}
	${SharedDir}
	"${MPDir}/rd-vulkan-vision"
	"${MPDir}/rd-vanilla"
	"${GSLIncludeDirectory}"
	"${CMAKE_CURRENT_BINARY_DIR}"
	${MPVulkanVisionVulkanHeaders}
	)

# everything that touches the GPU
set(MPVulkanVisionFiles
	"${MPDir}/rd-vulkan-vision/vk_funcs.h"
	"${MPDir}/rd-vulkan-vision/vk_hooks.h"
	"${MPDir}/rd-vulkan-vision/vk_local.h"
	"${MPDir}/rd-vulkan-vision/vk_backend.cpp"
	"${MPDir}/rd-vulkan-vision/vk_bc.cpp"
	"${MPDir}/rd-vulkan-vision/vk_frame.cpp"
	"${MPDir}/rd-vulkan-vision/vk_gl.cpp"
	"${MPDir}/rd-vulkan-vision/vk_image.cpp"
	"${MPDir}/rd-vulkan-vision/vk_main.cpp"
	"${MPDir}/rd-vulkan-vision/vk_memory.cpp"
	"${MPDir}/rd-vulkan-vision/vk_model.cpp"
	"${MPDir}/rd-vulkan-vision/vk_pipeline.cpp"
	"${MPDir}/rd-vulkan-vision/vk_post.cpp"
	"${MPDir}/rd-vulkan-vision/vk_shade.cpp"
	"${MPDir}/rd-vulkan-vision/vk_stubs.cpp"
	"${MPDir}/rd-vulkan-vision/vk_world.cpp"
	)
source_group("renderer" FILES ${MPVulkanVisionFiles})

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
set(MPVulkanVisionFiles ${MPVulkanVisionFiles} ${MPVulkanVisionVanillaFiles})

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

# GLSL to SPIR-V as a C initializer list, one .inc per shader, included by vk_pipeline.cpp. glslc runs on the
# build machine, so this also works when cross compiling.
set(MPVulkanVisionShaders
	generic.vert
	skinned.vert
	generic.frag
	post.vert
	gamma.frag
	blur.frag
	copy.frag
	)
set(MPVulkanVisionShaderOutputs)
foreach(shader ${MPVulkanVisionShaders})
	set(shaderIn "${MPDir}/rd-vulkan-vision/shaders/${shader}")
	string(REPLACE "." "_" shaderName ${shader})
	set(shaderOut "${CMAKE_CURRENT_BINARY_DIR}/shaders/${shaderName}.inc")
	add_custom_command(
		OUTPUT ${shaderOut}
		COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
		COMMAND ${GLSLC} --target-env=vulkan1.3 -O -mfmt=c -I "${MPDir}/rd-vulkan-vision/shaders" -o ${shaderOut} ${shaderIn}
		DEPENDS ${shaderIn} "${MPDir}/rd-vulkan-vision/shaders/common.glsl" "${MPDir}/rd-vulkan-vision/shaders/stage.glsl"
		COMMENT "glslc ${shader}"
		VERBATIM)
	list(APPEND MPVulkanVisionShaderOutputs ${shaderOut})
endforeach()
set(MPVulkanVisionFiles ${MPVulkanVisionFiles} ${MPVulkanVisionShaderOutputs})

# Transparently use either bundled or system libjpeg, libpng, zlib and our bundled minizip, as rd-vanilla does.
list(APPEND MPVulkanVisionIncludeDirectories ${JPEG_INCLUDE_DIR})
list(APPEND MPVulkanVisionLibraries          ${JPEG_LIBRARIES})
list(APPEND MPVulkanVisionIncludeDirectories ${PNG_INCLUDE_DIRS})
list(APPEND MPVulkanVisionLibraries          ${PNG_LIBRARIES})
list(APPEND MPVulkanVisionIncludeDirectories ${ZLIB_INCLUDE_DIR})
list(APPEND MPVulkanVisionLibraries          ${ZLIB_LIBRARIES})
list(APPEND MPVulkanVisionIncludeDirectories ${MINIZIP_INCLUDE_DIRS})
list(APPEND MPVulkanVisionLibraries          ${MINIZIP_LIBRARIES})

# the worker threads that pack textures into blocks (vk_bc.cpp)
find_package(Threads REQUIRED)
list(APPEND MPVulkanVisionLibraries Threads::Threads)

# tr_local.h of rd-vanilla pulls in the OpenGL headers for its types. Nothing of OpenGL is linked.
find_package(OpenGL REQUIRED)
list(APPEND MPVulkanVisionIncludeDirectories ${OPENGL_INCLUDE_DIR} ${OpenJKLibDir})

add_library(${MPVulkanVisionRenderer} SHARED ${MPVulkanVisionFiles})

if(NOT MSVC)
	# remove "lib" prefix for .so/.dylib files
	set_target_properties(${MPVulkanVisionRenderer} PROPERTIES PREFIX "")
endif()

if(WIN32)
	install(TARGETS ${MPVulkanVisionRenderer}
		RUNTIME
		DESTINATION ${JKAInstallDir}
		COMPONENT ${JKAMPClientComponent})
else(WIN32)
	if(MakeApplicationBundles AND BuildMPEngine)
		install(TARGETS ${MPVulkanVisionRenderer}
			LIBRARY
			DESTINATION "${JKAInstallDir}/${MPEngine}.app/Contents/MacOS/"
			COMPONENT ${JKAMPClientComponent})
	else()
		install(TARGETS ${MPVulkanVisionRenderer}
			LIBRARY
			DESTINATION ${JKAInstallDir}
			COMPONENT ${JKAMPClientComponent})
	endif()
endif()

set_target_properties(${MPVulkanVisionRenderer} PROPERTIES COMPILE_DEFINITIONS "${SharedDefines};RD_VULKAN_VISION")

# Hide symbols not explicitly marked public.
set_property(TARGET ${MPVulkanVisionRenderer} APPEND PROPERTY COMPILE_OPTIONS ${OPENJK_VISIBILITY_FLAGS})

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	# an OpenGL call left in a shared file must fail the build, not the game
	set_property(TARGET ${MPVulkanVisionRenderer} APPEND PROPERTY LINK_OPTIONS "-Wl,-z,defs")
endif()

set_target_properties(${MPVulkanVisionRenderer} PROPERTIES INCLUDE_DIRECTORIES "${MPVulkanVisionIncludeDirectories}")
set_target_properties(${MPVulkanVisionRenderer} PROPERTIES PROJECT_LABEL "MP Vulkan Vision Renderer")
target_link_libraries(${MPVulkanVisionRenderer} ${MPVulkanVisionLibraries})
