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

// cg_drain.h -- force drain as streams of life that flow from the drained into the hand (cg_drain.c, issue #19)

#pragma once

void CG_DrainInit( void );
void CG_DrainRegister( void );
// a player or NPC drains this frame from hand (otherHand: and that one, NULL: one hand) along dir at level (1 to 3):
// qtrue when it is drawn here, the effect of the game is left out then
qboolean CG_DrainCast( centity_t *cent, const vec3_t hand, const vec3_t otherHand, const vec3_t dir, int level );
// once a frame of the main view: the streams of a drain that ended fade out
void CG_DrainFrame( void );
