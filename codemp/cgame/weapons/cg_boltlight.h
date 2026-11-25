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

void CG_BoltLightsInit( void );
// this missile asks for its light; qfalse: it is no bolt or the switch is off, the caller keeps the stock light
qboolean CG_BoltLight( const centity_t *cent );
// the same for the shot of a vehicle weapon (its own effect, the weapon of the entity says nothing)
qboolean CG_VehicleBoltLight( const centity_t *cent, const vehWeaponInfo_t *weapon );
// the colour of the light of a shot of this weapon (cg_boltlook.c draws its bolt in it); qfalse: it has none
qboolean CG_BoltColor( int weapon, qboolean altFire, vec3_t color );
// the same for a vehicle weapon, qtrue only for its lasers (no torpedo, ion blast or bomb)
qboolean CG_VehicleLaserColor( const vehWeaponInfo_t *weapon, vec3_t color );
// a blade deflected a bolt here
void CG_BoltLightFlash( const vec3_t at );
// a weapon charging a shot: a light at the end of its barrel that grows with the charge, 0 to 1
void CG_ChargeLight( int weapon, const vec3_t at, float charge );
// after the entities of a scene: the nearest bolts go in as lights, the flashes only into the main view
void CG_BoltLightsAdd( qboolean skyPortal );
