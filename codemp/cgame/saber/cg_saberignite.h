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

void CG_SaberIgniteRegister( void );
// the sound of this hilt going on or off: its own one if its .sab names one, else the voice its name picks
sfxHandle_t CG_SaberIgniteSound( const saberInfo_t *saber, qboolean on );
// the same for a sound the server sent as a temp entity at origin, which does not say whose saber it is; 0: play nothing
sfxHandle_t CG_SaberIgniteEvent( const vec3_t origin, sfxHandle_t sound );
// brightness and thickness of a blade, 1 unless it is on its way in or out or just deflected a shot
float CG_SaberIgniteFlicker( const bladeInfo_t *blade, int num, int saberNum, int bladeNum );
// a shot was deflected at this spot: the blade there flickers and throws sparks
void CG_SaberDeflect( const vec3_t at, const vec3_t dir );
