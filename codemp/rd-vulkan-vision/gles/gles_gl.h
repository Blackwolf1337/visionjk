/*
===========================================================================
Copyright (C) 2026, OpenJK contributors
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


#pragma once

// gles_gl.h: what rd-vanilla/qgl.h includes in the place of the headers of OpenGL when rd-vulkan-vision is built
// with its device for OpenGL ES (RD_VISION_GLES)
//
// The types and names of OpenGL ES 3, and the names of OpenGL that the files shared with rd-vanilla still say and
// OpenGL ES does not have. Those are numbers the shared code hands around: a primitive of glBegin() for vk_gl.cpp,
// a wrap mode or a format tr_image.cpp asks for, the way two textures of a stage are combined. None of them
// reaches OpenGL ES as it is, the device takes what it is handed apart (GL_CLAMP is clamp to edge for it, as for
// the Vulkan device).

#include <GLES3/gl3.h>

// rd-vanilla/glext.h is about OpenGL and its typedefs do not compile without it. tr_image.cpp and
// tr_WorldEffects.cpp include it by name, this is the name it guards itself with.
#define __gl_glext_h_ 1

#define GL_QUADS							0x0007
#define GL_POLYGON							0x0009
#define GL_ADD								0x0104
#define GL_BACK_LEFT						0x0402
#define GL_BACK_RIGHT						0x0403
#define GL_FOG								0x0B60
#define GL_FOG_DENSITY						0x0B62
#define GL_FOG_COLOR						0x0B66
#define GL_MODULATE							0x2100
#define GL_DECAL							0x2101
#define GL_CLAMP							0x2900
#define GL_CLIP_PLANE0						0x3000
#define GL_RGB5								0x8050
#define GL_VERTEX_ARRAY						0x8074
#define GL_COLOR_ARRAY						0x8076
#define GL_TEXTURE_COORD_ARRAY				0x8078
#define GL_RGB4_S3TC						0x83A1
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT		0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT	0x83F3
#define GL_TEXTURE_RECTANGLE_ARB			0x84F5
#define GL_TEXTURE_MAX_ANISOTROPY_EXT		0x84FE
