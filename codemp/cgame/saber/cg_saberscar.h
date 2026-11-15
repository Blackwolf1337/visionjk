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

// cg_saberscar.h -- the scars a saber melts into what it cuts (cg_saberscar.c)

#pragma once

void	CG_SaberScarsInit( void );
void	CG_SaberScarsRegister( void );
// a blade (key: its trail, index: which of its two traces) touches the level at pos this frame
void	CG_SaberScarContact( const void *key, int index, const vec3_t pos, const vec3_t normal, float bladeRadius );
// it touches nothing this frame
void	CG_SaberScarRelease( const void *key, int index );
// the scars into the scene, once a frame
void	CG_SaberScarsAdd( void );
