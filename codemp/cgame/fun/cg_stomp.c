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

// cg_stomp.c -- a giant shakes the ground. All of it is in the cgame: the server knows nothing of it, and a
// client without this cgame just does not get it.

#include "cgame/cg_local.h"

/*
=================
Stomp

Not in JA+. A player the server made more than three times his size (iModelScale above STOMP_MIN_SCALE, the JA+
model scale) is felt by who is near him: where a foot of his comes down, where he pushes off for a jump and
where he lands, the view shakes, a heavy step is heard and dust comes up. cg_stomp 0 switches it off.

The steps are the footstep events of his animation, as the footstep sounds are (CG_PlayerFootsteps), so with
cg_footsteps 0 there are none of those. Jumps and landings are the events of the player movement.
=================
*/

#define STOMP_MIN_SCALE		300			// percent: only a player above this
#define STOMP_MAX_GROWTH	4.0f		// twice that size is twice as strong and felt twice as far, up to this
#define STOMP_OWN_VIEW		0.35f		// of the shake for the one whose steps they are, or his picture never stands still
#define STOMP_GROUND		64			// below the feet, at normal size: how far the ground is looked for

static const struct stompKind_s {
	float		shake;			// of a view right at the spot, at STOMP_MIN_SCALE. CGCam_Shake() takes up to 16
	int			radius;			// how far it is felt, at STOMP_MIN_SCALE
	int			time;			// msec the view shakes
	qboolean	far;			// the sound carries
} stompKinds[STOMP_NUM_KINDS] = {
	{ 1.5f,		 700,	250,	qfalse },	// STOMP_STEP
	{ 1.0f,		 600,	200,	qfalse },	// STOMP_JUMP
	{ 3.0f,		1000,	400,	qtrue },	// STOMP_LAND
	{ 5.0f,		1400,	600,	qtrue },	// STOMP_FALL
};

static sfxHandle_t	stompSounds[2];
static fxHandle_t	stompDust;

/*
=================
CG_StompRegister
=================
*/
void CG_StompRegister( void ) {
	// the steps of the rancor and the ring of dust of something that hit the ground, both of the game itself
	stompSounds[0] = trap->S_RegisterSound( "sound/chars/rancor/step1.mp3" );
	stompSounds[1] = trap->S_RegisterSound( "sound/chars/rancor/step2.mp3" );
	stompDust = trap->FX_RegisterEffect( "env/impact_dustonly" );
}

/*
=================
CG_Stomp

origin: where on the ground. Does nothing for a player who is no giant.
=================
*/
void CG_Stomp( const centity_t *cent, const vec3_t origin, stompKind_t kind ) {
	const struct stompKind_s *k = &stompKinds[kind];
	vec3_t spot, up = { 0.0f, 0.0f, 1.0f };
	float growth, shake;

	if ( !cg_stomp.integer || !cg.snap ) {
		return;
	}
	if ( cent->currentState.number >= MAX_CLIENTS || cent->currentState.iModelScale <= STOMP_MIN_SCALE ) {
		return;
	}
	if ( cent->currentState.eFlags & EF_DEAD ) {
		return;
	}

	growth = cent->currentState.iModelScale / (float)STOMP_MIN_SCALE;
	if ( growth > STOMP_MAX_GROWTH ) {
		growth = STOMP_MAX_GROWTH;
	}
	shake = k->shake * growth;
	// also for a spectator who follows him: it is his view
	if ( cent->currentState.number == cg.snap->ps.clientNum ) {
		shake *= STOMP_OWN_VIEW;
	}

	VectorCopy( origin, spot );
	CG_DoCameraShake( spot, shake, (int)( k->radius * growth ), k->time );
	trap->S_StartSound( spot, ENTITYNUM_WORLD, k->far ? CHAN_LESS_ATTEN : CHAN_AUTO, stompSounds[Q_irand( 0, 1 )] );
	trap->FX_PlayEffectID( stompDust, spot, up, -1, -1, qfalse );
}

/*
=================
CG_StompBelow

For a jump or a landing, which have no foot to ask: the ground below the player
=================
*/
void CG_StompBelow( const centity_t *cent, stompKind_t kind ) {
	trace_t tr;
	vec3_t end;

	if ( !cg_stomp.integer || cent->currentState.iModelScale <= STOMP_MIN_SCALE ) {
		return;
	}
	VectorCopy( cent->lerpOrigin, end );
	// the origin of a player is at his middle, and a giant's is that much higher above his feet
	end[2] -= STOMP_GROUND * ( cent->currentState.iModelScale / 100.0f );
	CG_Trace( &tr, cent->lerpOrigin, NULL, NULL, end, cent->currentState.number, MASK_PLAYERSOLID );
	if ( tr.fraction == 1.0f || tr.startsolid ) {
		return;
	}
	CG_Stomp( cent, tr.endpos, kind );
}
