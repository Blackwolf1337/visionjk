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

// cg_saberdrag.c -- a blade dragged along the floor or a wall by a player on the move (issue #20) throws a trail of
// sparks against the way it goes, and what they are like depends on the material it touches. Sparks the renderer
// simulates only (v_gpuSparks): without them the blade keeps the sparks of the game at the contact.

#include "cgame/cg_local.h"
#include "cgame/saber/cg_saberdrag.h"

#define DRAG_PLAYER_SPEED	50.0f		// units a second of the player: standing still and leaning the blade on a wall is no drag
#define DRAG_BLADE_SPEED	60.0f		// of the point of contact
#define DRAG_GAP_MSEC		200			// longer without a touch: a new contact
#define DRAG_EMIT_MSEC		40

typedef struct {
	vec3_t	last;
	int		lastTime;
	int		next;
} saberDrag_t;

static saberDrag_t	drags[MAX_CLIENTS][MAX_SABERS][MAX_BLADES];

void CG_SaberDragInit( void ) {
	memset( drags, 0, sizeof( drags ) );
}

static gpuSparks_t Drag_Kind( int surfaceFlags ) {
	switch ( surfaceFlags & MATERIAL_MASK ) {
	case MATERIAL_SOLIDMETAL:
	case MATERIAL_HOLLOWMETAL:
	case MATERIAL_ARMOR:
	case MATERIAL_COMPUTER:
		return GPUSPARKS_DRAG_METAL;
	case MATERIAL_SOLIDWOOD:
	case MATERIAL_HOLLOWWOOD:
		return GPUSPARKS_DRAG_WOOD;
	case MATERIAL_GLASS:
	case MATERIAL_BPGLASS:
	case MATERIAL_SHATTERGLASS:
		return GPUSPARKS_DRAG_GLASS;
	case MATERIAL_WATER:
	case MATERIAL_SNOW:
	case MATERIAL_ICE:
		return GPUSPARKS_DRAG_COLD;
	case MATERIAL_SHORTGRASS:
	case MATERIAL_LONGGRASS:
	case MATERIAL_DIRT:
	case MATERIAL_SAND:
	case MATERIAL_MUD:
	case MATERIAL_FLESH:
	case MATERIAL_DRYLEAVES:
	case MATERIAL_GREENLEAVES:
	case MATERIAL_FABRIC:
	case MATERIAL_CANVAS:
	case MATERIAL_RUBBER:
	case MATERIAL_PLASTIC:
	case MATERIAL_CARPET:
		return GPUSPARKS_DRAG_SOFT;
	default:	// stone and what nobody named
		return GPUSPARKS_DRAG_STONE;
	}
}

void CG_SaberDrag( const centity_t *cent, int saberNum, int bladeNum, const vec3_t pos, const vec3_t normal, int surfaceFlags ) {
	const int	num = cent->currentState.number;
	saberDrag_t	*d;
	int			gap;

	if ( !v_saberDrag.integer || !cg.snap || num < 0 || num >= MAX_CLIENTS || saberNum < 0 || saberNum >= MAX_SABERS
		|| bladeNum < 0 || bladeNum >= MAX_BLADES || ( surfaceFlags & SURF_NOIMPACT ) ) {
		return;
	}
	d = &drags[num][saberNum][bladeNum];
	gap = cg.time - d->lastTime;
	if ( !gap ) {
		return;
	}
	if ( gap > 0 && gap <= DRAG_GAP_MSEC && cg.time >= d->next ) {
		const float	playerSpeed = VectorLength( num == cg.snap->ps.clientNum ? cg.predictedPlayerState.velocity : cent->currentState.pos.trDelta );
		vec3_t		moved, dir;
		float		speed;

		VectorSubtract( pos, d->last, moved );
		speed = VectorNormalize( moved ) * 1000.0f / gap;
		if ( playerSpeed >= DRAG_PLAYER_SPEED && speed >= DRAG_BLADE_SPEED ) {
			// (off the surface and back along the way the blade came)
			VectorScale( moved, -1.0f, dir );
			VectorMA( dir, 0.7f, normal, dir );
			VectorNormalize( dir );
			CG_GpuSparks( pos, dir, Drag_Kind( surfaceFlags ), Com_Clamp( 0.5f, 1.4f, speed / 400.0f ) );
			d->next = cg.time + DRAG_EMIT_MSEC;
		}
	}
	VectorCopy( pos, d->last );
	d->lastTime = cg.time;
}
