/*
===========================================================================
Copyright (C) 2026, VisionJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// vk_bake.h -- between the ray tracing of the level (vk_rt.cpp) and the light it bakes into its lightmaps
// (vk_bake.cpp, v_bake). Vulkan device only.

#pragma once

#include <vector>

// vk_rt.cpp: the surfaces of the acceleration structure of the level in the order its triangles have, each split
// into triangles as there (faces by their indexes, triangle meshes by theirs, patches two a quad)
void		VK_RT_LevelSurfaces( std::vector<const msurface_t *> &surfaces );
// the set of this frame with the top level at binding 0 and its layout; qfalse while the level is not built
qboolean	VK_RT_BakeSet( VkDescriptorSet *set, VkDescriptorSetLayout *layout );
VkDeviceAddress	VK_RT_BufferAddress( const vkBuffer_t *buf );

// vk_bake.cpp
void		VK_Bake_Register( void );
void		VK_Bake_Record( VkCommandBuffer cmd );		// in VK_RT_Pass, after the top level of the frame is built
void		VK_Bake_Shutdown( void );
