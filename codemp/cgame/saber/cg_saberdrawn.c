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

// cg_saberdrawn.c -- where each blade of each player was drawn this frame and the frame before (issue #20).

#include "cgame/cg_local.h"
#include "cgame/saber/cg_saberdrawn.h"

static saberDrawn_t	drawn[MAX_CLIENTS][MAX_SABERS][MAX_BLADES];

void CG_SaberDrawnInit( void ) {
	memset( drawn, 0, sizeof( drawn ) );
}

void CG_SaberDrawnAdd( int num, int saberNum, int bladeNum, const vec3_t start, const vec3_t dir, float length,
	float lengthMax, float radius, int color ) {
	saberDrawn_t *b;

	if ( num < 0 || num >= MAX_CLIENTS || saberNum < 0 || saberNum >= MAX_SABERS || bladeNum < 0 || bladeNum >= MAX_BLADES
		|| length < 1.0f ) {
		return;
	}
	b = &drawn[num][saberNum][bladeNum];
	if ( b->time != cg.time ) {
		VectorCopy( b->start, b->oldStart );
		VectorCopy( b->span, b->oldSpan );
		b->oldTime = b->time;
	}
	VectorCopy( start, b->start );
	VectorScale( dir, length, b->span );
	b->time = cg.time;
	b->lengthMax = lengthMax;
	b->radius = radius;
	b->color = color;
}

const saberDrawn_t *CG_SaberDrawnLast( int num, int saberNum, int bladeNum ) {
	if ( num < 0 || num >= MAX_CLIENTS || saberNum < 0 || saberNum >= MAX_SABERS || bladeNum < 0 || bladeNum >= MAX_BLADES
		|| !drawn[num][saberNum][bladeNum].time ) {
		return NULL;
	}
	return &drawn[num][saberNum][bladeNum];
}

const saberDrawn_t *CG_SaberDrawnBlade( int num, int saberNum, int bladeNum ) {
	if ( num < 0 || num >= MAX_CLIENTS || saberNum < 0 || saberNum >= MAX_SABERS || bladeNum < 0 || bladeNum >= MAX_BLADES
		|| drawn[num][saberNum][bladeNum].time != cg.time ) {
		return NULL;
	}
	return &drawn[num][saberNum][bladeNum];
}
