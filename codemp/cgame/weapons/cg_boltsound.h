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

// cg_boltsound.h -- the own shots rise as the ammunition runs out, a bolt flying past the head whizzes
// (cg_boltsound.c, issue #22)

#pragma once

void CG_BoltSoundInit( void );
void CG_BoltSoundRegister( void );
// the sound of a shot of entityNum on CHAN_WEAPON, in place of S_StartSound: higher for the own player low on ammunition
void CG_FireSound( int entityNum, int weapon, qboolean altFire, sfxHandle_t sfx );
// once a frame of a missile: hears it fly past the head of the own player
void CG_BoltWhiz( const centity_t *cent );
