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

void CG_WallFxInit( void );
void CG_WallFxRegister( void );
// a shot of this weapon hit a wall (CG_MissileHitWall, the blast of the DEMP2 alt fire): arcs of the DEMP2, a shard
// of the flechette. normal: of the wall, a zero one is no wall
void CG_WallFxHit( int weapon, qboolean altFire, const vec3_t at, const vec3_t normal );
// once a frame of a missile: where a shard of the flechette flies, for the one it leaves in the wall
void CG_WallFxTrack( const centity_t *cent );
// once a frame, with the entities of the main view
void CG_WallFxScene( void );
