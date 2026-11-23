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

// cg_saberwound.c -- burn wounds (issue #20): where a blade hit a player the model gets three marks of the skin gore at
// once. A white core that is gone in a moment and an orange glow that lasts longer fade on their own, so the wound goes
// from white over orange to red, and under them a dark char with no end of life, which the game clears at the respawn.
// The fades are additive: the sorting trouble of fading gore (CG_AddGhoul2Mark) is only with blended marks.

#include "cgame/cg_local.h"
#include "cgame/saber/cg_saberwound.h"

#define WOUND_GAP_MSEC		300			// per player: a blade dragged through a body hits every frame
#define WOUND_MAX_CHARS		6			// dark marks a player keeps
#define WOUND_MAX_TAGS		72			// of the skin gore on a model: one mark is a tag per surface it crosses, about four, so
										//	the 16 of cg_ghoul2Marks would be full after a single wound
#define WOUND_CORE_MSEC		900
#define WOUND_GLOW_MSEC		5000

static qhandle_t	coreShader, glowShader, charShader;
static int			woundTime[MAX_CLIENTS];
static int			chars[MAX_CLIENTS];

void CG_SaberWoundsInit( void ) {
	memset( woundTime, 0, sizeof( woundTime ) );
	memset( chars, 0, sizeof( chars ) );
}

void CG_SaberWoundsRegister( void ) {
	coreShader = trap->R_RegisterShader( "gfx/vision/burn_core" );
	glowShader = trap->R_RegisterShader( "gfx/vision/burn_glow" );
	charShader = trap->R_RegisterShader( "gfx/vision/burn_char" );
}

void CG_SaberWoundsClear( int num ) {
	if ( num >= 0 && num < MAX_CLIENTS ) {
		chars[num] = 0;
		woundTime[num] = 0;
	}
}

static void Wound_Mark( centity_t *victim, qhandle_t shader, float size, float theta, const vec3_t start, const vec3_t dir, int life ) {
	SSkinGoreData gore;

	memset( &gore, 0, sizeof( gore ) );
	gore.growDuration = -1;
	gore.goreScaleStartFraction = 1.0f;
	gore.frontFaces = qtrue;
	gore.backFaces = qtrue;
	gore.lifeTime = life;
	gore.fadeOutTime = life;		// (from the first moment on; 0 for the char, which never ends)
	gore.fadeRGB = (qboolean)( life != 0 );
	gore.currentTime = cg.time;
	gore.entNum = victim->currentState.number;
	gore.SSize = size;
	gore.TSize = size;
	gore.theta = theta;
	gore.shader = shader;
	if ( victim->modelScale[0] || victim->modelScale[1] || victim->modelScale[2] ) {
		VectorCopy( victim->modelScale, gore.scale );
	} else {
		VectorSet( gore.scale, 1.0f, 1.0f, 1.0f );
	}
	VectorCopy( start, gore.hitLocation );
	VectorCopy( dir, gore.rayDirection );
	VectorCopy( victim->lerpOrigin, gore.position );
	gore.angles[YAW] = victim->lerpAngles[YAW];
	trap->G2API_AddSkinGore( victim->ghoul2, &gore );
}

qboolean CG_SaberWound( centity_t *victim, const vec3_t start, const vec3_t end ) {
	const int	num = victim->currentState.number;
	vec3_t		dir;
	float		size, theta;

	// (a shader of 0 would be drawn as the red gore of the game)
	if ( !v_saberBurns.integer || !cg_ghoul2Marks.integer || num < 0 || num >= MAX_CLIENTS || !victim->ghoul2
		|| !coreShader || !glowShader || !charShader ) {
		return qfalse;
	}
	if ( woundTime[num] && cg.time - woundTime[num] >= 0 && cg.time - woundTime[num] < WOUND_GAP_MSEC ) {
		return qtrue;
	}
	VectorSubtract( end, start, dir );
	if ( VectorNormalize( dir ) < 0.1f ) {
		return qtrue;
	}
	if ( trap->G2API_GetNumGoreMarks( victim->ghoul2, 0 ) >= WOUND_MAX_TAGS ) {
		return qtrue;
	}
	woundTime[num] = cg.time;
	size = flrand( 3.2f, 4.4f );
	theta = flrand( 0.0f, 6.28f );
	if ( chars[num] < WOUND_MAX_CHARS ) {
		chars[num]++;
		Wound_Mark( victim, charShader, size, theta, start, dir, 0 );
	}
	Wound_Mark( victim, glowShader, size, theta, start, dir, WOUND_GLOW_MSEC );
	Wound_Mark( victim, coreShader, size * 0.85f, theta, start, dir, WOUND_CORE_MSEC );
	return qtrue;
}

qboolean CG_SaberWoundAt( centity_t *victim, const vec3_t at ) {
	vec3_t	middle, dir, start, end;

	// (level: a ray that slants would land the mark far below the hit on a standing body)
	VectorCopy( victim->lerpOrigin, middle );
	middle[2] = at[2];
	VectorSubtract( middle, at, dir );
	if ( VectorNormalize( dir ) < 0.1f ) {
		VectorSet( dir, 1.0f, 0.0f, 0.0f );
	}
	VectorMA( at, -6.0f, dir, start );
	VectorMA( at, 6.0f, dir, end );
	return CG_SaberWound( victim, start, end );
}
