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

// cg_mines.c -- trip mines and det packs that were put on a wall (issue #22, item 21): a small light on them that
// blinks, red on a trip mine and green on a det pack, a soft beep with every few blinks, and a click the moment one
// comes to rest. The game sends them as ET_GENERAL with their weapon; resting is TR_STATIONARY (G_SetOrigin when it
// sticks). One that is already lying there when it comes into view clicks no more.

#include "cgame/cg_local.h"
#include "cgame/weapons/cg_mines.h"

#define MINE_LIGHT_MSEC		140			// the light is on that long in each blink
#define MINE_NEAR			1400.0f		// further away: no light and no beep
#define MINE_GONE_MSEC		500			// not seen for this long: another entity in the same slot
#define MINE_BEEP_SOUND		"sound/vehicles/weapons/common/tick.wav"	// (of the game: a short high beep)
#define MINE_CLICK_SOUND	"sound/movers/switches/switch2.wav"			// (of the game)

typedef struct {
	int			seen;		// 0: never
	qboolean	rest;		// it lay still, clicked or not
	int			beep;		// the blink of the last beep, -1: none yet
} mineTrack_t;

static mineTrack_t	mines[MAX_GENTITIES];
static sfxHandle_t	beepSound, clickSound;
static qhandle_t	flareShader;

void CG_MinesInit( void ) {
	memset( mines, 0, sizeof( mines ) );
}

void CG_MinesRegister( void ) {
	beepSound = trap->S_RegisterSound( MINE_BEEP_SOUND );
	clickSound = trap->S_RegisterSound( MINE_CLICK_SOUND );
	flareShader = trap->R_RegisterShader( "gfx/effects/whiteFlare" );	// (of the game, additive)
}

void CG_MineLooks( const centity_t *cent, const refEntity_t *model ) {
	const entityState_t	*s = &cent->currentState;
	const qboolean		trip = (qboolean)( s->weapon == WP_TRIP_MINE );
	const qboolean		still = (qboolean)( s->pos.trType == TR_STATIONARY );
	mineTrack_t			*t;
	int					blink, beepEvery, phase, slot;
	vec3_t				at, color;
	refEntity_t			re;

	if ( !v_weaponImpacts.integer || CG_JPInSeparatedDuel() || ( !trip && s->weapon != WP_DET_PACK )
		|| s->number < 0 || s->number >= MAX_GENTITIES ) {
		return;
	}
	t = &mines[s->number];
	if ( !t->seen || cg.time < t->seen || cg.time - t->seen > MINE_GONE_MSEC ) {
		// (a trip mine that stuck the moment it came into view: laserTrapStick sets trTime to then)
		t->rest = (qboolean)( still && !( trip && cg.time >= s->pos.trTime && cg.time - s->pos.trTime < MINE_GONE_MSEC ) );
		t->beep = -1;
	} else if ( still && !t->rest ) {
		trap->S_StartSound( NULL, s->number, CHAN_AUTO, clickSound );
		t->rest = qtrue;
	}
	t->seen = cg.time;
	if ( !still || DistanceSquared( model->origin, cg.refdef.vieworg ) > MINE_NEAR * MINE_NEAR ) {
		return;
	}

	// (each one blinks in a time of its own, not all of them at once)
	blink = trip ? 1000 : 1500;
	beepEvery = 2;
	phase = cg.time + ( s->number * 137 ) % blink;
	slot = phase / ( blink * beepEvery );
	if ( slot != t->beep ) {
		if ( t->beep >= 0 ) {
			trap->S_StartSound( NULL, s->number, CHAN_AUTO, beepSound );
		}
		t->beep = slot;
	}
	if ( phase % blink >= MINE_LIGHT_MSEC ) {
		return;
	}

	// a little in front of it, on the side away from the wall (the way its laser points)
	VectorMA( model->origin, trip ? 5.0f : 3.0f, model->axis[0], at );
	if ( trip ) {
		VectorSet( color, 1.0f, 0.12f, 0.08f );
	} else {
		VectorSet( color, 0.2f, 1.0f, 0.25f );
	}
	memset( &re, 0, sizeof( re ) );
	re.reType = RT_SPRITE;
	VectorCopy( at, re.origin );
	re.radius = 2.5f;
	re.customShader = flareShader;
	re.shaderRGBA[0] = (byte)( 255.0f * ( 0.5f + 0.5f * color[0] ) );
	re.shaderRGBA[1] = (byte)( 255.0f * ( 0.5f + 0.5f * color[1] ) );
	re.shaderRGBA[2] = (byte)( 255.0f * ( 0.5f + 0.5f * color[2] ) );
	re.shaderRGBA[3] = 255;
	re.renderfx = RF_NOSHADOW;
	trap->R_AddRefEntityToScene( &re );
	re.radius = 7.0f;
	re.shaderRGBA[0] = (byte)( 110.0f * color[0] );
	re.shaderRGBA[1] = (byte)( 110.0f * color[1] );
	re.shaderRGBA[2] = (byte)( 110.0f * color[2] );
	trap->R_AddRefEntityToScene( &re );
	trap->R_AddLightToScene( at, 40.0f, color[0], color[1], color[2] );
}
