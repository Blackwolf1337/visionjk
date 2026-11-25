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

// cg_explosion.c
void CG_ExplosionInit( void );
void CG_ExplosionRegister( void );
// a missile of this weapon blew up at origin (EV_MISSILE_MISS and _HIT), dir: the normal it came with. The bombs that
// come as an effect are left to CG_ExplosionEffect.
void CG_Explosion( int weapon, qboolean altFire, const vec3_t origin, const vec3_t dir );
// EV_PLAY_EFFECT: the det pack, the trip mine and the bombs of the flechette (EFFECT_* of bg_public.h)
void CG_ExplosionEffect( int effect, const vec3_t origin, const vec3_t dir );
// once a frame of the main view: the fires explosions left
void CG_ExplosionsAdd( void );
// a player fired (CG_FireWeapon): the backblast of a rocket launcher
void CG_Backblast( centity_t *cent );

// cg_thermal.c
void CG_ThermalInit( void );
void CG_ThermalRegister( void );
// once a frame of a missile: a thermal detonator beeps faster as its fuse runs down
void CG_ThermalFuse( const centity_t *cent );
// how it is drawn: smooth rolls and bounces in place of the stock spin and the jumps of its trajectory (CG_Missile)
void CG_ThermalPose( const centity_t *cent, refEntity_t *ent );
// and the stripe of light of its ring, once its model is turned (CG_Missile)
void CG_ThermalRing( const centity_t *cent, const refEntity_t *ent );
