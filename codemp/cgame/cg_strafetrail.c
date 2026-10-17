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

// cg_strafetrail.c -- strafe trails (2026-10-17), as jaPRO and TaystJK have them (cg_strafeTrail): a line on the
// ground along where a player ran, fading out over tay_strafeTrailLife seconds. tay_strafeTrail 1 oneself, 2
// everybody. From the places the client has anyway, nothing comes from the server for it.

#include "cg_local.h"

#define TRAIL_POINTS		128
#define TRAIL_STEP			8.0f		// a new point when the player moved this far
#define TRAIL_FEET			20.0f		// below the origin, near the ground

typedef struct {
	vec3_t	pos[TRAIL_POINTS];
	int		time[TRAIL_POINTS];
	int		head;						// the newest point
	int		count;
} strafeTrail_t;

static strafeTrail_t	strafeTrails[MAX_CLIENTS];
static int				strafeTrailLastTime;

static void Trail_Add( strafeTrail_t *t, const vec3_t origin ) {
	vec3_t feet;

	VectorCopy( origin, feet );
	feet[2] -= TRAIL_FEET;
	if ( t->count && DistanceSquared( t->pos[t->head], feet ) < TRAIL_STEP * TRAIL_STEP ) {
		return;
	}
	// a jump across the level (a teleporter, a respawn) starts a new trail
	if ( t->count && DistanceSquared( t->pos[t->head], feet ) > 512.0f * 512.0f ) {
		t->count = 0;
	}
	t->head = ( t->head + 1 ) % TRAIL_POINTS;
	VectorCopy( feet, t->pos[t->head] );
	t->time[t->head] = cg.time;
	t->count = Q_min( t->count + 1, TRAIL_POINTS );
}

static void Trail_Draw( const strafeTrail_t *t, const vec3_t color, int life ) {
	refEntity_t	re;
	int			i;

	memset( &re, 0, sizeof( re ) );
	re.reType = RT_LINE;
	re.radius = 1.5f;
	re.customShader = trap->R_RegisterShader( "gfx/effects/whiteGlow" );
	AxisClear( re.axis );

	for ( i = 0; i + 1 < t->count; i++ ) {
		const int	a = ( t->head - i + TRAIL_POINTS ) % TRAIL_POINTS;
		const int	b = ( a - 1 + TRAIL_POINTS ) % TRAIL_POINTS;
		const int	age = cg.time - t->time[b];
		float		fade;

		if ( age >= life || age < 0 ) {
			break;
		}
		// the shader adds its light, so darker is fainter
		fade = 1.0f - (float)age / life;
		re.shaderRGBA[0] = (byte)( color[0] * fade * 255 );
		re.shaderRGBA[1] = (byte)( color[1] * fade * 255 );
		re.shaderRGBA[2] = (byte)( color[2] * fade * 255 );
		re.shaderRGBA[3] = 255;
		VectorCopy( t->pos[a], re.origin );
		VectorCopy( t->pos[b], re.oldorigin );
		trap->R_AddRefEntityToScene( &re );
	}
}

/*
=================
CG_StrafeTrailsAdd

From CG_DrawActiveFrame, with the entities of the scene
=================
*/
void CG_StrafeTrailsAdd( void ) {
	const int	life = (int)( Com_Clamp( 0.5f, 30.0f, tay_strafeTrailLife.value ) * 1000.0f );
	int			i;

	if ( !tay_strafeTrail.integer || !cg.snap ) {
		return;
	}
	if ( cg.time < strafeTrailLastTime ) {
		memset( strafeTrails, 0, sizeof( strafeTrails ) );		// a new level
	}
	strafeTrailLastTime = cg.time;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const centity_t	*cent = &cg_entities[i];
		const qboolean	self = (qboolean)( i == cg.snap->ps.clientNum );
		vec3_t			color;

		if ( !self && ( tay_strafeTrail.integer < 2 || !cent->currentValid || cent->currentState.eType != ET_PLAYER ) ) {
			strafeTrails[i].count = 0;
			continue;
		}
		if ( self && ( cg.snap->ps.pm_type == PM_SPECTATOR || cg.snap->ps.stats[STAT_HEALTH] <= 0 ) ) {
			strafeTrails[i].count = 0;
			continue;
		}
		Trail_Add( &strafeTrails[i], self ? cg.predictedPlayerState.origin : cent->lerpOrigin );

		// oneself green, the teams in their colours, the others white
		if ( self ) {
			VectorSet( color, 0.2f, 1.0f, 0.4f );
		} else if ( cgs.gametype >= GT_TEAM && cgs.clientinfo[i].team == TEAM_RED ) {
			VectorSet( color, 1.0f, 0.25f, 0.2f );
		} else if ( cgs.gametype >= GT_TEAM && cgs.clientinfo[i].team == TEAM_BLUE ) {
			VectorSet( color, 0.25f, 0.45f, 1.0f );
		} else {
			VectorSet( color, 0.8f, 0.8f, 0.8f );
		}
		Trail_Draw( &strafeTrails[i], color, life );
	}
}
