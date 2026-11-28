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

// cg_forceaura.c -- the Force on the bodies (issue #19), part of Force Looks (v_fxForce):
//
// - Auras: Heal, Protect, Absorb and Rage light the body of the player and the room around him, each in its colour
//   and with its own beat. Every player sees them on everybody; the own view had only its screen looks before.
// - Grip: at the throat of who is held by a grip, a dark mist curls round the neck and a ring of air tightens. What
//   the held player sees and hears is in screenfx/cg_screenfx.c (the edges dark, the sound dull).
//
// Which powers run comes with the entity (forcePowersActive). Absorb is shown by the game only when it took
// something in; its light shows while it runs.

#include "cgame/cg_local.h"
#include "cgame/force/cg_forceaura.h"

#define AURA_RADIUS			120.0f
#define AURA_STRENGTH		0.5f
#define CHOKE_PUFF_MSEC		60		// a curl of mist at the throat that often

static qhandle_t	ringShader;
static qhandle_t	mistShader;
static int			nextPuff[MAX_GENTITIES];

void CG_ForceAuraRegister( void ) {
	ringShader = trap->R_RegisterShader( "gfx/effects/forcePush" );		// (of the game: the wave of push, bends the air)
	mistShader = trap->R_RegisterShader( "gfx/misc/black_smoke" );		// (of the game)
	memset( nextPuff, 0, sizeof( nextPuff ) );
}

// held by a grip: the move code plays the choke on him (HANDEXTEND_CHOKE, BOTH_CHOKE3)
static qboolean Aura_Choked( const centity_t *cent ) {
	const entityState_t *es = &cent->currentState;

	if ( es->number == cg.predictedPlayerState.clientNum && !( cg.predictedPlayerState.pm_flags & PMF_FOLLOW ) ) {
		return (qboolean)( cg.predictedPlayerState.forceHandExtend == HANDEXTEND_CHOKE );
	}
	return (qboolean)( es->torsoAnim == BOTH_CHOKE3 || es->legsAnim == BOTH_CHOKE3 || es->torsoAnim == BOTH_CHOKE1
		|| es->torsoAnim == BOTH_CHOKE2 );
}

static qboolean Aura_ShadowLight( void ) {
	static int has = -1;

	if ( has < 0 ) {
		char value[8];

		trap->Cvar_VariableStringBuffer( "v_cgameShadowLight", value, sizeof( value ) );
		has = ( atoi( value ) && trap->ext.R_AddShadowLight ) ? 1 : 0;
	}
	return (qboolean)( has > 0 );
}

static void Aura_Light( const centity_t *cent ) {
	const int	active = cent->currentState.forcePowersActive;
	vec3_t		color, center;
	float		beat;

	VectorClear( color );
	// (each with its beat: protect steady, absorb breathing, rage throbbing, heal a soft swell)
	if ( active & ( 1 << FP_PROTECT ) ) {
		beat = 0.85f + 0.15f * sinf( cg.time * 0.004f );
		VectorMA( color, beat, ( vec3_t ){ 0.25f, 0.9f, 0.35f }, color );
	}
	if ( active & ( 1 << FP_ABSORB ) ) {
		beat = 0.7f + 0.3f * sinf( cg.time * 0.003f );
		VectorMA( color, beat, ( vec3_t ){ 0.25f, 0.45f, 1.0f }, color );
	}
	if ( active & ( 1 << FP_RAGE ) ) {
		beat = 0.5f + 0.5f * sinf( cg.time * 0.0105f );
		beat = 0.6f + 0.4f * beat * beat * beat;
		VectorMA( color, beat, ( vec3_t ){ 1.0f, 0.12f, 0.08f }, color );
	}
	if ( active & ( 1 << FP_HEAL ) ) {
		VectorMA( color, 0.9f, ( vec3_t ){ 0.55f, 1.0f, 0.65f }, color );
	}
	if ( color[0] + color[1] + color[2] <= 0.0f ) {
		return;
	}
	VectorCopy( cent->lerpOrigin, center );
	center[2] += 8.0f;
	VectorScale( color, AURA_STRENGTH, color );
	// (it sits in the body: the level and the other players cast their shadows in it, the own body does not, it would
	// block it)
	if ( Aura_ShadowLight() ) {
		trap->ext.R_AddShadowLight( center, AURA_RADIUS, color[0], color[1], color[2], 0, cent->ghoul2 );
	} else {
		trap->R_AddLightToScene( center, AURA_RADIUS, color[0], color[1], color[2] );
	}
}

// the throat of a held player: the bolt of the head, a little down
static qboolean Aura_Throat( centity_t *cent, clientInfo_t *ci, vec3_t throat, vec3_t up ) {
	mdxaBone_t m;

	if ( !cent->ghoul2 || ci->bolt_head < 0 ) {
		return qfalse;
	}
	trap->G2API_GetBoltMatrix( cent->ghoul2, 0, ci->bolt_head, &m, cent->turAngles, cent->lerpOrigin, cg.time, cgs.gameModels, cent->modelScale );
	throat[0] = m.matrix[0][3];
	throat[1] = m.matrix[1][3];
	throat[2] = m.matrix[2][3] - 9.0f;
	VectorSet( up, 0.0f, 0.0f, 1.0f );
	return qtrue;
}

static void Aura_Choke( centity_t *cent, clientInfo_t *ci ) {
	const int	n = cent->currentState.number;
	vec3_t		throat, up, side, fwd, at, vel;
	refEntity_t	ring;
	float		a;

	// (the own throat is not seen in first person)
	if ( n == cg.snap->ps.clientNum && !cg.renderingThirdPerson ) {
		return;
	}
	if ( !Aura_Throat( cent, ci, throat, up ) ) {
		return;
	}
	// a ring of bent air that tightens and lets go
	if ( ringShader ) {
		memset( &ring, 0, sizeof( ring ) );
		ring.reType = RT_SPRITE;
		VectorCopy( throat, ring.origin );
		ring.radius = 7.0f + 2.5f * sinf( cg.time * 0.012f );
		ring.rotation = (float)( ( cg.time / 4 ) % 360 );
		ring.customShader = ringShader;
		ring.shaderRGBA[0] = 90;
		ring.shaderRGBA[1] = 40;
		ring.shaderRGBA[2] = 60;
		ring.shaderRGBA[3] = 255;
		trap->R_AddRefEntityToScene( &ring );
	}
	// dark mist that curls round the neck
	if ( mistShader && ( cg.time >= nextPuff[n] || nextPuff[n] - cg.time > CHOKE_PUFF_MSEC ) && n >= 0 && n < MAX_GENTITIES ) {
		nextPuff[n] = cg.time + CHOKE_PUFF_MSEC;
		a = cg.time * 0.009f;
		VectorSet( fwd, cosf( a ), sinf( a ), 0.0f );
		CrossProduct( up, fwd, side );
		VectorMA( throat, 6.0f, fwd, at );
		VectorScale( side, 40.0f, vel );
		vel[2] += 6.0f;
		CG_SmokePuff( at, vel, flrand( 4.0f, 7.0f ), 0.15f, 0.1f, 0.15f, 0.55f, Q_irand( 350, 550 ), cg.time, 0, 0, mistShader );
	}
}

void CG_ForceAuraPlayer( centity_t *cent, clientInfo_t *ci ) {
	if ( !v_fxForce.integer || !cg.snap || ( cent->currentState.eFlags & EF_DEAD ) ) {
		return;
	}
	Aura_Light( cent );
	if ( ci && Aura_Choked( cent ) ) {
		Aura_Choke( cent, ci );
	}
}
