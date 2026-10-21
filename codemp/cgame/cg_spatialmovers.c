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

// cg_spatialmovers.c -- where the brush models are (doors, lifts, func_static, glass), for the sound of the level
// with Steam Audio (2026-10-21, client/spatial/snd_spatial.cpp): a closed door blocks the sound, an open one lets it
// through. Once a frame the engine gets every brush model this level has shown so far, where the snapshot has it
// now (ext.S_SpatialMovers). One that is no longer in the snapshot is gone (broken, switched off) when the camera
// would see where it was: the server would have sent it. One out of sight keeps its last place.

#include "cg_local.h"

static struct {
	qboolean	seen;
	qboolean	gone;
	vec3_t		origin, angles;
	vec3_t		middle;				// where it is, for the PVS
} smBrushes[MAX_MODELS];

static int				smModelOf[MAX_GENTITIES];	// the brush model an entity had, 0: none
static spatialMover_t	smList[MAX_MODELS];
static int				smLevel = -1;				// cgs.levelStartTime the above are of

void CG_SpatialMovers( void ) {
	char	value[8];
	byte	inSnap[MAX_MODELS];
	int		i, count = 0;

	if ( !cg.snap ) {
		return;
	}
	trap->Cvar_VariableStringBuffer( "v_cgameSpatialMovers", value, sizeof( value ) );
	if ( atoi( value ) != 1 ) {
		return;
	}
	if ( smLevel != cgs.levelStartTime ) {
		smLevel = cgs.levelStartTime;
		memset( smBrushes, 0, sizeof( smBrushes ) );
		memset( smModelOf, 0, sizeof( smModelOf ) );
	}

	memset( inSnap, 0, sizeof( inSnap ) );
	for ( i = 0; i < cg.snap->numEntities; i++ ) {
		const entityState_t	*es = &cg.snap->entities[i];
		const centity_t		*cent = &cg_entities[es->number];
		const int			model = es->modelindex;

		if ( es->solid == SOLID_BMODEL && model > 0 && model < cgs.numInlineModels && model < MAX_MODELS ) {
			inSnap[model] = 1;
			smBrushes[model].seen = qtrue;
			smBrushes[model].gone = qfalse;
			VectorCopy( cent->lerpOrigin, smBrushes[model].origin );
			VectorCopy( cent->lerpAngles, smBrushes[model].angles );
			VectorAdd( cent->lerpOrigin, cgs.inlineModelMidpoints[model], smBrushes[model].middle );
			smModelOf[es->number] = model;
		} else if ( smModelOf[es->number] ) {
			// it was a brush model and is not solid any more (a func_usable switched off)
			inSnap[smModelOf[es->number]] = 1;
			smBrushes[smModelOf[es->number]].gone = qtrue;
		}
	}

	for ( i = 1; i < cgs.numInlineModels && i < MAX_MODELS; i++ ) {
		spatialMover_t *m;

		if ( !smBrushes[i].seen ) {
			continue;
		}
		if ( !inSnap[i] && !smBrushes[i].gone && trap->R_InPVS( cg.refdef.vieworg, smBrushes[i].middle, cg.snap->areamask ) ) {
			smBrushes[i].gone = qtrue;
		}
		m = &smList[count++];
		m->model = i;
		m->gone = smBrushes[i].gone;
		VectorCopy( smBrushes[i].origin, m->origin );
		VectorCopy( smBrushes[i].angles, m->angles );
	}
	if ( count ) {
		trap->ext.S_SpatialMovers( smList, count );
	}
}
