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

void CG_SaberClashInit( void );
// blades met at origin: glowing drops, and with flare a flash of light on the room
void CG_SaberClash( const vec3_t origin, const vec3_t dir, float strength, qboolean flare );
// once a frame after the entities: the flashes, and the players whose blades are locked
void CG_SaberClashAdd( void );
