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

void CG_SaberWoundsInit( void );
void CG_SaberWoundsRegister( void );
// a blade burned victim between start and end. qfalse: not handled here, leave the mark of the game
qboolean CG_SaberWound( centity_t *victim, const vec3_t start, const vec3_t end );
// the same from a hit event, which only says where: the ray goes level from there towards the axis of the body
qboolean CG_SaberWoundAt( centity_t *victim, const vec3_t at );
// the player respawned, the game cleared the marks on the model
void CG_SaberWoundsClear( int num );
