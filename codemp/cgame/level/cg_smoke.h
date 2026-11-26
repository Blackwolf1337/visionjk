/*
===========================================================================
Copyright (C) 2026, VisionJK contributors

This file is part of the VisionJK source code.

VisionJK is free software; you can redistribute it and/or modify it
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

// cg_smoke.c -- smoke that stays in a room and moves with what happens in it (issue #23)

void CG_SmokeInit( void );
void CG_SmokeRegister( void );
// once a frame of the main view, after CG_GustFrame (it asks for the pushes of the frame)
void CG_SmokeFrame( void );
// the sheet of smoke (gfx/vision/fb/smoke) is drawn tile by tile: by rd-vulkan-vision alone, RF_FLIPBOOK
qboolean CG_SmokeSheet( void );
// an explosion of this size (1: a rocket) went off here, on a surface that faces dir (or in the air: no length): it
// clears the smoke around for a moment and leaves its own. qtrue: smoke with depth, the caller leaves its cloud of
// sprites away.
qboolean CG_SmokeBlast( const vec3_t origin, const vec3_t dir, float size );
// a shot hit a wall that faces normal: count puffs of its dust in this colour, about radius units, as smoke of the
// volume. qfalse: the renderer has no volume, the caller makes sprites.
qboolean CG_SmokeImpact( const vec3_t at, const vec3_t normal, float radius, const vec3_t color, int count );
