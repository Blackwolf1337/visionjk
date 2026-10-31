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

// cg_toys.h -- what the toys of issue #9 share inside cgame/toys: cg_toys.c (the snowball and the way to the
// others) and cg_water.c (the water gun).

#pragma once

#include "game/toys/bg_toys.h"

// cg_toys.c
qboolean	CG_ToyCanUse( void );
void		CG_ToySend( const toyThrow_t *toy );
void		CG_ToyLight( const vec3_t point, vec3_t out );

// cg_water.c
void		CG_WaterInit( void );
void		CG_WaterFrame( void );
void		CG_WaterScene( void );
void		CG_WaterHeard( int client, const vec3_t origin, const vec3_t velocity );
