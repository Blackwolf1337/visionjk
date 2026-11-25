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

// cg_gust.c -- air that moves in the level (issue #23)

typedef struct {
	int			clientNum;
	qboolean	pull;
	vec3_t		from, forward;		// from: the middle of the player
} forcePush_t;

void CG_GustInit( void );
void CG_GustRegister( void );
// once a frame of the main view, before whoever asks for the pushes: sees the players that begin a Force Push or
// Pull, moves and draws the leaves, tells the renderer who stands in the grass
void CG_GustFrame( void );
// the pushes and pulls that began this frame; returns how many
int CG_ForcePushes( const forcePush_t **list );
// an explosion went off on a surface of this MATERIAL_* at this spot: the leaves of a floor of plants fly up, and the
// grass around lies down for a moment
void CG_GustBlast( const vec3_t at, const vec3_t normal, int material, float size );
