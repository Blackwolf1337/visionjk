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

// cg_lightning.c -- force lightning as forked bolts that light the level, jump to metal, crawl on it, burn lines
// (issue #19)

void CG_LightningInit( void );
void CG_LightningRegister( void );
// a player or NPC casts lightning this frame from hand along dir at level (1 to 3), otherHand: and from that hand
// too (bare hands at level 3, NULL: one hand): qtrue when it is drawn here, the effect of the game is left out then
qboolean CG_LightningCast( centity_t *cent, const vec3_t hand, const vec3_t otherHand, const vec3_t dir, int level );
// once a frame of the main view: the arcs that stay on metal, water that lit up
void CG_LightningFrame( void );

// the same bolts elsewhere: a line of points as a bolt (its points, width and brightness; weapons/cg_wallfx.c)
#include "cgame/force/cg_ribbon.h"
void CG_LightningStrand( const ribbon_t *r, float level, float width );
// arcs over the body of one shocked (es.emplacedOwner, the electrify time of the game), strength 0 to 1
void CG_LightningShocked( centity_t *cent, clientInfo_t *ci, float strength );
// small arcs that crackle round a shot of the DEMP2 (seed: its entity number)
void CG_LightningOrb( const vec3_t origin, int seed, float radius );
// the charged blast of the DEMP2 at origin, as large as radius
void CG_LightningBurst( const vec3_t origin, float radius );
