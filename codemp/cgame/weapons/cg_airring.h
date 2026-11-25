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

void CG_AirRingsInit( void );
void CG_AirRingsRegister( void );
// EV_FIRE_WEAPON of this player: a ring at his barrel if it was a fully charged shot of the pistol or the bowcaster
void CG_ChargeRingFired( const centity_t *cent, qboolean altFire );
// the charged shot of the disruptor from muzzle to end (EV_DISRUPTOR_SNIPER_SHOT), fullCharge as the game sends it
void CG_ChargeRingDisruptor( const vec3_t muzzle, const vec3_t end, qboolean fullCharge );
// the beam of the concussion rifle from start, dir (normalized) and length long: rings run along it
void CG_AirRingBeam( const vec3_t start, const vec3_t dir, float length );
// once a frame of the missile of the concussion rifle: a ring of bent air behind it
void CG_AirRingTrail( const centity_t *cent );
// once a frame, with the entities of the main view
void CG_AirRingsScene( void );
