/*
===========================================================================
Copyright (C) 2026, OpenJK contributors

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

// vk_stubs.cpp: what the shared files call and this renderer does not do yet
//
// Weather (tr_WorldEffects.cpp of rd-vanilla) and the programs of the glow pass (tr_arb.cpp) are OpenGL code.
// Until this renderer has its own, the calls end here and nothing is drawn for them.

#include "vk_local.h"

void R_InitWorldEffects( void ) {
}

void R_ShutdownWorldEffects( void ) {
}

void RE_WorldEffectCommand( const char *command ) {
}

void R_WorldEffect_f( void ) {
}

void RE_AddWeatherZone( vec3_t mins, vec3_t maxs ) {
}

void ARB_InitGPUShaders( void ) {
}
