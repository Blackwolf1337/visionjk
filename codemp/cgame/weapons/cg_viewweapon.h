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

// cg_viewweapon.h -- the weapon in first person kicks back, sways, is lowered at a wall, put away and drawn
// (cg_viewweapon.c, issue #22)

#pragma once

void CG_ViewWeaponInit( void );
// the own player fired (EV_FIRE_WEAPON or EV_ALT_FIRE of the player the view is of)
void CG_ViewWeaponFired( int weapon, qboolean altFire );
// once a frame, on the place and angles of the weapon in first person (CG_CalculateWeaponPosition)
void CG_ViewWeaponMove( vec3_t origin, vec3_t angles );
// whether the weapon is put away and drawn by its own move here (the frames of the hands then keep still)
qboolean CG_ViewWeaponOwnSwitch( void );
