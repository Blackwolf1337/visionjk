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

// cg_sabersound.h -- SmoothSwing: the sound of a blade follows how it moves (cg_sabersound.c, issue #20)

#pragma once

void		CG_SaberSoundInit( void );
void		CG_SaberSoundRegister( void );
// where a blade of cent points this frame (not of a thrown saber)
void		CG_SaberSoundBlade( const centity_t *cent, int saberNum, int bladeNum, const vec3_t dir );
// the hum of a saber of cent this frame, in place of S_AddLoopingSound, with the swing loops over it
void		CG_SaberSoundHum( const centity_t *cent, const vec3_t spot, sfxHandle_t hum );
// the blade of another player passing the head, once a frame
void		CG_SaberSoundNearMiss( void );
// the swing of a stance (0 fast, 1 medium, 2 strong), 0: SmoothSwing is off, play the sound of the game
sfxHandle_t	CG_SaberSwingSound( int kind );
// the same for the stance of a saber style (SS_*)
sfxHandle_t	CG_SaberStyleSwingSound( int style );
