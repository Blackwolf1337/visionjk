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

// vk_stubs.cpp: what the shared files call and this renderer has no use for
//
// The programs of the glow pass (tr_arb.cpp of rd-vanilla) are OpenGL code. The glow of this renderer is
// vk_post.cpp with shaders of its own, so the call ends here.

#include "vk_local.h"

void ARB_InitGPUShaders( void ) {
}
