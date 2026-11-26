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

// cg_ripples.c -- rings on the water of the level where somebody wades, a shot goes in or something blows up (issue #23)

void CG_RipplesInit( void );
// a player or creature that is drawn; qtrue: the rings stand in for the mark of the game round him
qboolean CG_RipplePlayer( centity_t *cent );
// a missile that is drawn
void CG_RippleMissile( centity_t *cent );
// a shot that is there at once, from start to where it hit
void CG_RippleShot( const vec3_t start, const vec3_t end );
// something blew up, size as weapons/cg_explosion.c has it
void CG_RippleBlast( const vec3_t origin, float size );
// once a frame of the main view
void CG_RipplesFrame( void );
