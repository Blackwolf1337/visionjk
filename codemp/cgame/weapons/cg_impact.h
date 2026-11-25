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

typedef enum {
	IMPACT_STONE,		// and what nobody named
	IMPACT_METAL,
	IMPACT_WOOD,
	IMPACT_GLASS,
	IMPACT_COLD,		// water, snow, ice
	IMPACT_SOFT,		// earth, plants, cloth
	IMPACT_KINDS
} impactKind_t;

void CG_ImpactInit( void );
void CG_ImpactRegister( void );
// what a MATERIAL_* of game/surfaceflags.h counts as
impactKind_t CG_ImpactKindOf( int material );
// the MATERIAL_* of the surface at this spot, normal pointing out of it, looked up in steamaudio/materials.txt first;
// MATERIAL_NONE where nobody named one, metal: take it for metal then. qfalse: no surface there, or the sky or one
// that takes no impacts. surfaceFlags (may be NULL): the SURF_* of it
qboolean CG_ImpactMaterial( const vec3_t at, const vec3_t normal, qboolean metal, int *material, int *surfaceFlags );
// the picture of a puff of dust, tinted by CG_ImpactDustColor
qhandle_t CG_ImpactDustShader( void );
// the colour of the dust a kind throws up; qfalse: it throws none (metal, glass)
qboolean CG_ImpactDustColor( impactKind_t kind, vec3_t color );

typedef struct {
	int		firstChunk, lastChunk;	// CHUNK_* of cg_local.h, one of them picked for each bit
	float	sizeMin, sizeMax;		// scale of the chunk model
	float	speedMin, speedMax;		// units a second, about along the normal
	int		lifeMin, lifeMax;		// msec
	float	bounce;
	int		bounceSound;			// LEBS_*
} impactChips_t;
// bits knocked off a surface, which tumble and bounce (LE_FRAGMENT)
void CG_ImpactChips( const vec3_t at, const vec3_t normal, const impactChips_t *how, int count );
// a shot of this weapon hit a wall at this spot: what it throws off and how it sounds, by the material there
void CG_BoltImpact( int weapon, qboolean altFire, const vec3_t at, const vec3_t normal, qboolean metal );
// once a frame of a missile: sees it bounce off a wall, which the game sends no event for
void CG_BoltTrack( const centity_t *cent );
