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

# The headers of Vulkan, for what loads the library at run time and needs nothing else of it at build time: the
# renderer rd-vulkan-vision and the sound of the client (client/sound/spatial/snd_spatial_gpu.cpp).
# -DVulkanHeadersDir=<dir with vulkan/vulkan.h> names where they are if they are not found.
#
# jp_vulkan_headers(<out> <copyDir>) sets <out> to the directory to put on the include path, or to "" when there
# are none. The headers are the same for every platform, so the ones of the host do when cross compiling. But their
# directory may be the include directory of the host, which a cross compiler must not see: then it gets a copy of
# the two directories in <copyDir>. On macOS too: there they are in the include directory of Homebrew, with the
# jpeglib.h of jpeg-turbo, which came before the one of lib/jpeg-9a the renderer is linked with. Every JPEG then
# failed to load ("Wrong JPEG library version: library is 90, caller expects 80"), and the levels were white.
function(jp_vulkan_headers out copyDir)
	find_path(VulkanHeadersDir vulkan/vulkan.h)
	if(NOT VulkanHeadersDir AND CMAKE_CROSSCOMPILING AND EXISTS "/usr/include/vulkan/vulkan.h")
		set(VulkanHeadersDir "/usr/include" CACHE PATH "Vulkan headers" FORCE)
	endif()
	if(NOT VulkanHeadersDir)
		set(${out} "" PARENT_SCOPE)
	elseif(CMAKE_CROSSCOMPILING OR APPLE)
		foreach(dir vulkan vk_video)
			if(EXISTS "${VulkanHeadersDir}/${dir}")
				# the directory itself and not a link to it: Homebrew links them in from its Cellar, by a relative
				# path that leads nowhere from the copy
				get_filename_component(real "${VulkanHeadersDir}/${dir}" REALPATH)
				file(REMOVE_RECURSE "${copyDir}/${dir}")
				file(COPY "${real}/" DESTINATION "${copyDir}/${dir}")
			endif()
		endforeach()
		set(${out} "${copyDir}" PARENT_SCOPE)
	else()
		set(${out} "${VulkanHeadersDir}" PARENT_SCOPE)
	endif()
endfunction()
