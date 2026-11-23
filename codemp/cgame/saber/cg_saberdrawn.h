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

// where the blades of the players were drawn. The cgame keeps this nowhere else: bladeInfo_t.muzzlePoint is filled by
// the game module only.
typedef struct {
	vec3_t	start, span;		// from the hilt to the tip
	int		time;
	vec3_t	oldStart, oldSpan;	// the frame before
	int		oldTime;
	float	lengthMax, radius;
	int		color;
} saberDrawn_t;

void CG_SaberDrawnInit( void );
void CG_SaberDrawnAdd( int num, int saberNum, int bladeNum, const vec3_t start, const vec3_t dir, float length,
	float lengthMax, float radius, int color );
// the blade if it was drawn this frame, else NULL
const saberDrawn_t *CG_SaberDrawnBlade( int num, int saberNum, int bladeNum );
// the same whenever it was last drawn, NULL if never
const saberDrawn_t *CG_SaberDrawnLast( int num, int saberNum, int bladeNum );
