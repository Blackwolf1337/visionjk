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

// g_forcemoves.h -- what the Force does to bodies (g_forcemoves.c, vs_forceMoves)

#pragma once

// vs_forceMoves is on
qboolean G_ForceMovesOn( void );
// a new level
void G_ForceMovesInit( void );
// Force Grip level 3 holds gripEnt in front of self this frame (in the place of the stock move every 300 ms)
void G_ForceMovesGrip( gentity_t *self, gentity_t *gripEnt );
// a push or pull of self while his Force Grip holds a player: throws him, or pulls him down at the feet; qtrue: done
qboolean G_ForceMovesGripThrow( gentity_t *self, qboolean pull );
// victim was sent flying by thrower: the next wall he hits within msec hurts him by strength (1: a throw)
void G_ForceMovesFlying( gentity_t *victim, gentity_t *thrower, float strength, int msec );
// after the move of a player or NPC: did he hit a wall flying (velocity: his before the move)
void G_ForceMovesAfterMove( gentity_t *ent, const vec3_t velocity );
// the push of self with bare hands is a two-handed one: a stronger push that knocks down
qboolean G_ForceMovesTwoHandPush( const gentity_t *self );
// lightning of self along forward that strikes water shocks everybody in that water
void G_ForceMovesLightningWater( gentity_t *self, const vec3_t forward );
