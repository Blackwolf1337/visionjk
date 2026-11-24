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

void CG_SaberDropInit( void );
void CG_SaberDropRegister( void );
void CG_SaberDropShutdown( void );
// every frame of a player: qtrue when the hilts have fallen out of the hands, so they are not drawn on the body
qboolean CG_SaberDropUpdate( centity_t *cent, clientInfo_t *ci );
// once a frame: the hilts on their way down and on the floor
void CG_SaberDropAdd( void );
// a blade that belongs to no player (cg_players.c)
void CG_SaberLooseBlade( const vec3_t origin, const vec3_t dir, float length, float lengthMax, float radius, int color, float ignite );
