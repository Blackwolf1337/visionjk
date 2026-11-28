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

void CG_SaberBeltInit( void );
// once a frame of a player, before the holster code: follows the saber going on and off
void CG_SaberBeltUpdate( centity_t *cent, const clientInfo_t *ci );
// the switched off sabers of this player hang on the belt now, not in the hands
qboolean CG_SaberBeltHolds( const centity_t *cent );
// how far the hilt on the hip swings with the walk: added to angles, in the space of the hip bolt
void CG_SaberBeltSwing( const centity_t *cent, int saberNum, const vec3_t hip, const matrix3_t boltAxis, vec3_t angles );
