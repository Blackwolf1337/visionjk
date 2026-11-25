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

// cg_doors.c -- a heavy door that opens (issue #23, "Doors"). A large door of metal lets out a puff of steam along its
// edges with a hiss, a large one of stone lets dust trickle from its top. Only a door that stood shut for some
// seconds: one that people walk through all the time would hiss every second. Only large ones: a sliding door the
// size of a person does nothing. The level does not say which doors are heavy (139 of the 169 doors of the
// multiplayer levels have no sound set of their own), so the size of the leaf and what it is made of decide
// (steamaudio/materials.txt, as the impacts of shots). Wood and everything else: nothing.
//
// A door: a brush model that moves and stands upright (its thinnest side lies flat: no lift, no hatch). Where it
// stood when it was first seen at rest is taken for shut. The light of the room behind is not touched: the light of a
// level is baked.
//
// On v_levelFx. Only a look and a sound here: the game knows nothing of it.

#include "cgame/cg_local.h"
#include "cgame/weapons/cg_impact.h"
#include "cgame/level/cg_doors.h"

#define DOOR_AREA			20000.0f	// square units of its leaf from which a door is heavy (128 x 160)
#define DOOR_FULL			70000.0f	// and from which it lets out all it has
#define DOOR_SHUT_MSEC		4000		// it must have stood shut that long
#define DOOR_NEAR			1400.0f		// further from the eye: nothing
#define DOOR_PUFF_MSEC		450			// the steam comes over that time

typedef struct {
	int			model;					// the brush model it was seen with, 0: not seen
	qboolean	moving;
	vec3_t		home;					// where it stands shut
	int			restSince;				// since when it rests there, 0: it does not
	int			puffUntil;				// steam or dust comes until then
	int			nextPuff;
	int			kind;					// impactKind_t of the door that opens now
	float		strength;
} door_t;

static door_t		doors[MAX_GENTITIES];
static qhandle_t	steamShader;
static sfxHandle_t	hissSound;
static int			doorsLevel = -1;	// cgs.levelStartTime the above are of

void CG_DoorsInit( void ) {
	memset( doors, 0, sizeof( doors ) );
}

void CG_DoorsRegister( void ) {
	steamShader = CG_ImpactDustShader();
	hissSound = trap->S_RegisterSound( "sound/effects/air_burst.mp3" );		// (of the game)
}

// the leaf of a door where it is now: its middle, the way through it, its sides across and up, and half its sizes.
// qfalse: no door (it lies flat, or it is as thick as it is wide)
static qboolean Door_Leaf( const centity_t *cent, vec3_t middle, vec3_t through, vec3_t across, float *halfWide, float *halfHigh, float *halfThick ) {
	const int	model = cent->currentState.modelindex;
	vec3_t		mins, maxs, size;
	int			thin;

	trap->R_ModelBounds( cgs.inlineDrawModel[model], mins, maxs );
	VectorSubtract( maxs, mins, size );
	thin = size[0] < size[1] ? 0 : 1;
	if ( size[2] <= size[thin] || size[thin] > 0.6f * size[1 - thin] ) {
		return qfalse;
	}
	VectorAdd( cent->lerpOrigin, cgs.inlineModelMidpoints[model], middle );
	VectorClear( through );
	through[thin] = 1.0f;
	VectorClear( across );
	across[1 - thin] = 1.0f;
	*halfWide = size[1 - thin] * 0.5f;
	*halfHigh = size[2] * 0.5f;
	*halfThick = size[thin] * 0.5f;
	return qtrue;
}

// what the leaf is made of: by the table of the textures where the renderer finds its surface (the door stands where
// the level has it while it is shut), else by the flags of its brush; a door nobody named a material for is metal
static int Door_Kind( const centity_t *cent, const vec3_t middle, const vec3_t through, float halfThick ) {
	vec3_t	from, normal;
	int		material = MATERIAL_NONE;

	VectorMA( middle, halfThick + 1.0f, through, from );
	VectorCopy( through, normal );
	if ( !CG_ImpactMaterial( from, normal, qtrue, &material, NULL ) ) {
		return IMPACT_METAL;
	}
	return CG_ImpactKindOf( material );
}

// a heavy door begins to open
static void Door_Opens( door_t *door, const centity_t *cent ) {
	vec3_t	middle, through, across;
	float	halfWide, halfHigh, halfThick, area;

	if ( !Door_Leaf( cent, middle, through, across, &halfWide, &halfHigh, &halfThick ) ) {
		return;
	}
	area = 4.0f * halfWide * halfHigh;
	if ( area < DOOR_AREA || DistanceSquared( middle, cg.refdef.vieworg ) > DOOR_NEAR * DOOR_NEAR ) {
		return;
	}
	door->kind = Door_Kind( cent, middle, through, halfThick );
	if ( door->kind != IMPACT_METAL && door->kind != IMPACT_STONE ) {
		return;
	}
	door->strength = Com_Clamp( 0.35f, 1.0f, ( area - DOOR_AREA ) / ( DOOR_FULL - DOOR_AREA ) + 0.35f );
	door->puffUntil = cg.time + DOOR_PUFF_MSEC + ( door->kind == IMPACT_STONE ? 900 : 0 );
	door->nextPuff = cg.time;
	if ( door->kind == IMPACT_METAL && hissSound ) {
		trap->S_StartSound( middle, ENTITYNUM_WORLD, CHAN_AUTO, hissSound );
	}
}

// what it lets out while it opens: steam along the edges of a door of metal, out of both its sides; dust off the top
// of a door of stone
static void Door_Puffs( door_t *door, const centity_t *cent ) {
	vec3_t	middle, through, across, at, vel, color;
	float	halfWide, halfHigh, halfThick;
	int		i, count;

	if ( cg.time < door->nextPuff || !steamShader || !Door_Leaf( cent, middle, through, across, &halfWide, &halfHigh, &halfThick ) ) {
		return;
	}
	door->nextPuff = cg.time + 50;
	count = (int)( 2.0f + 3.0f * door->strength );
	for ( i = 0; i < count; i++ ) {
		const float side = Q_irand( 0, 1 ) ? 1.0f : -1.0f;

		if ( door->kind == IMPACT_METAL ) {
			// (out of the seam at one of its upright edges or along its foot, where the seals let go)
			const qboolean foot = (qboolean)( Q_irand( 0, 2 ) == 0 );

			VectorMA( middle, foot ? flrand( -halfWide, halfWide ) : ( Q_irand( 0, 1 ) ? halfWide : -halfWide ), across, at );
			at[2] += foot ? -halfHigh + 6.0f : flrand( -halfHigh, halfHigh * 0.6f );
			VectorMA( at, side * ( halfThick + 12.0f ), through, at );
			VectorScale( through, side * flrand( 40.0f, 110.0f ) * door->strength, vel );
			vel[2] += flrand( 8.0f, 30.0f );
			CG_SmokePuff( at, vel, flrand( 18.0f, 34.0f ) * ( 0.7f + 0.5f * door->strength ), 0.86f, 0.88f, 0.9f, 0.32f,
				Q_irand( 900, 1700 ), cg.time, 0, 0, steamShader );
		} else {
			if ( !CG_ImpactDustColor( IMPACT_STONE, color ) ) {
				return;
			}
			VectorMA( middle, flrand( -halfWide, halfWide ), across, at );
			at[2] += halfHigh - 4.0f;
			VectorMA( at, side * ( halfThick + 12.0f ), through, at );
			VectorSet( vel, flrand( -6.0f, 6.0f ), flrand( -6.0f, 6.0f ), -flrand( 30.0f, 70.0f ) );
			CG_SmokePuff( at, vel, flrand( 10.0f, 20.0f ), color[0], color[1], color[2], 0.4f, Q_irand( 900, 1500 ), cg.time, 0, 0,
				steamShader );
		}
	}
}

void CG_DoorsFrame( void ) {
	int i;

	if ( doorsLevel != cgs.levelStartTime ) {
		doorsLevel = cgs.levelStartTime;
		CG_DoorsInit();
	}
	if ( !v_levelFx.integer || !cg.snap ) {
		return;
	}
	for ( i = 0; i < cg.snap->numEntities; i++ ) {
		const entityState_t	*es = &cg.snap->entities[i];
		const centity_t		*cent = &cg_entities[es->number];
		door_t				*door = &doors[es->number];
		qboolean			moving;

		if ( es->eType != ET_MOVER || es->solid != SOLID_BMODEL || es->modelindex <= 0 || es->modelindex >= cgs.numInlineModels
			|| es->modelindex >= MAX_MODELS ) {
			continue;
		}
		moving = (qboolean)( es->pos.trType != TR_STATIONARY && cg.time < es->pos.trTime + es->pos.trDuration );
		if ( door->model != es->modelindex ) {
			// first seen: where it rests is where it is shut (one seen on its way is looked at again when it has stopped)
			if ( moving ) {
				continue;
			}
			memset( door, 0, sizeof( *door ) );
			door->model = es->modelindex;
			VectorCopy( cent->lerpOrigin, door->home );
			// (as if it had stood there long: its first opening counts)
			door->restSince = cg.time - DOOR_SHUT_MSEC;
		}
		if ( moving && !door->moving ) {
			if ( door->restSince && cg.time - door->restSince >= DOOR_SHUT_MSEC ) {
				Door_Opens( door, cent );
			}
			door->restSince = 0;
		} else if ( !moving && door->moving ) {
			door->restSince = Distance( cent->lerpOrigin, door->home ) < 2.0f ? cg.time : 0;
		}
		door->moving = moving;
		if ( cg.time < door->puffUntil ) {
			Door_Puffs( door, cent );
		}
	}
}
