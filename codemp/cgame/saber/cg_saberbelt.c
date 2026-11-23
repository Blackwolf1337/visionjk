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

// cg_saberbelt.c -- a saber that is switched off hangs on the belt (issue #20), on the hip bolts the JA+ holster code
// draws on (cg_jpplayer.c), and swings with the walk as a pendulum driven by how the hip moves. The game has no
// animation for putting it there, so the arm is moved by bone overrides: down to the hip while the blade goes in, and
// from the hip when it comes out, which never delays the saber. Not for a staff, which stays as it is.

#include "cgame/cg_local.h"
#include "cgame/saber/cg_saberbelt.h"
#include "ghoul2/G2.h"

#define BELT_SWITCH_MSEC	360			// after switching off: the hand is at the hip and lets go
#define BELT_BACK_MSEC		260			// and is back
#define BELT_TAKE_MSEC		300			// the hand coming up from the hip with the saber
#define BELT_SPRING			95.0f		// of the pendulum, about 1.5 swings a second
#define BELT_DAMP			4.6f
#define BELT_PUSH			0.11f		// degrees a second a second per unit a second a second of the hip
#define BELT_MAX_SWING		34.0f
#define BELT_MAX_ACCEL		2400.0f
#define BELT_STEP_MSEC		8

typedef struct {
	float	swing[2], speed[2];		// degrees, round the two axes of the bolt
	vec3_t	hip, hipSpeed;
	int		hipTime;
} beltHilt_t;

typedef struct {
	qboolean	known, off, holds, armSet;
	int			offTime, onTime;
	beltHilt_t	hilts[MAX_SABERS];
} saberBelt_t;

static saberBelt_t	belts[MAX_CLIENTS];

void CG_SaberBeltInit( void ) {
	memset( belts, 0, sizeof( belts ) );
}

// a saber that can hang on the belt of this player, switched off this moment
static qboolean Belt_Off( const centity_t *cent, const clientInfo_t *ci ) {
	const entityState_t *es = &cent->currentState;

	if ( !v_saberBelt.integer || ( cp_pluginDisable.integer & ( 1 << 9 ) ) || es->weapon != WP_SABER || es->saberHolstered != 2
		|| es->saberInFlight || !es->saberEntityNum || ( es->eFlags & EF_DEAD ) || ci->team == TEAM_SPECTATOR
		|| es->NPC_class == CLASS_VEHICLE || !ci->saber[0].model[0] || ci->bolt_hipR <= 0 ) {
		return qfalse;
	}
	if ( ci->saber[0].type == SABER_STAFF || ( ci->saber[0].saberFlags & SFL_TWO_HANDED )
		|| ( ci->saber[0].saberFlags2 & SFL2_IDLE_IN_BACK ) ) {
		return qfalse;
	}
	if ( ci->saber[1].model[0] && ( ci->bolt_hipL <= 0 || ( ci->saber[1].saberFlags2 & SFL2_IDLE_IN_BACK ) ) ) {
		return qfalse;
	}
	return qtrue;
}

// how far the arm is on its way to the hip, 0 to 1
static float Belt_Arm( const saberBelt_t *b ) {
	float t;

	if ( b->off ) {
		const int age = cg.time - b->offTime;

		if ( age < BELT_SWITCH_MSEC ) {
			t = age / (float)BELT_SWITCH_MSEC;
		} else if ( age < BELT_SWITCH_MSEC + BELT_BACK_MSEC ) {
			t = 1.0f - ( age - BELT_SWITCH_MSEC ) / (float)BELT_BACK_MSEC;
		} else {
			return 0.0f;
		}
	} else {
		const int age = cg.time - b->onTime;

		if ( !b->onTime || age >= BELT_TAKE_MSEC ) {
			return 0.0f;
		}
		t = 1.0f - age / (float)BELT_TAKE_MSEC;
	}
	t = Com_Clamp( 0.0f, 1.0f, t );
	return t * t * ( 3.0f - 2.0f * t );
}

static void Belt_SetArm( centity_t *cent, const char *upper, const char *lower, float side, float arm ) {
	// (the hand of the arm hanging down is at the hip: in a little, the elbow bent up to the hilt; seen on the humanoid)
	const vec3_t upperAngles = { 5.0f * arm, 0.0f, -7.0f * side * arm };
	const vec3_t lowerAngles = { 17.0f * arm, 0.0f, 0.0f };

	trap->G2API_SetBoneAngles( cent->ghoul2, 0, upper, upperAngles, BONE_ANGLES_POSTMULT, POSITIVE_X, NEGATIVE_Y, NEGATIVE_Z,
		cgs.gameModels, 0, cg.time );
	trap->G2API_SetBoneAngles( cent->ghoul2, 0, lower, lowerAngles, BONE_ANGLES_POSTMULT, POSITIVE_X, NEGATIVE_Y, NEGATIVE_Z,
		cgs.gameModels, 0, cg.time );
}

void CG_SaberBeltUpdate( centity_t *cent, const clientInfo_t *ci ) {
	const int	num = cent->currentState.number;
	saberBelt_t	*b;
	qboolean	off;
	float		arm;

	if ( num < 0 || num >= MAX_CLIENTS || !cent->ghoul2 ) {
		return;
	}
	b = &belts[num];
	off = Belt_Off( cent, ci );
	if ( !b->known ) {
		// (first seen with it off: it hangs there already)
		b->known = qtrue;
		b->off = off;
		b->offTime = cg.time - BELT_SWITCH_MSEC - BELT_BACK_MSEC;
		b->onTime = 0;
	} else if ( off != b->off ) {
		b->off = off;
		if ( off ) {
			b->offTime = cg.time;
		} else if ( b->holds ) {
			b->onTime = cg.time;	// (only taken from the belt if it was there)
		}
	}
	b->holds = (qboolean)( off && cg.time - b->offTime >= BELT_SWITCH_MSEC );

	// a real draw animation or an attack has the arm
	arm = ( cent->currentState.torsoAnim >= BOTH_STAND1TO2_NEW && cent->currentState.torsoAnim <= BOTH_S1_S6_2ARMS )
		|| BG_SaberInAttack( cent->currentState.saberMove ) ? 0.0f : Belt_Arm( b );
	if ( arm > 0.0f || b->armSet ) {
		Belt_SetArm( cent, "rhumerus", "rradius", 1.0f, arm );
		if ( ci->saber[1].model[0] ) {
			Belt_SetArm( cent, "lhumerus", "lradius", -1.0f, arm );
		}
		b->armSet = (qboolean)( arm > 0.0f );
	}
}

qboolean CG_SaberBeltHolds( const centity_t *cent ) {
	const int num = cent->currentState.number;

	return (qboolean)( num >= 0 && num < MAX_CLIENTS && belts[num].holds );
}

void CG_SaberBeltSwing( const centity_t *cent, int saberNum, const vec3_t hip, const matrix3_t boltAxis, vec3_t angles ) {
	const int	num = cent->currentState.number;
	beltHilt_t	*h;
	int			ms, i;

	if ( !v_saberBelt.integer || num < 0 || num >= MAX_CLIENTS || saberNum < 0 || saberNum >= MAX_SABERS ) {
		return;
	}
	h = &belts[num].hilts[saberNum];
	ms = cg.time - h->hipTime;
	if ( ms < 0 || ms > 200 ) {
		// (it jumped, or was not drawn for a while)
		memset( h, 0, sizeof( *h ) );
		VectorCopy( hip, h->hip );
		h->hipTime = cg.time;
	} else if ( ms > 0 ) {
		const float	dt = ms * 0.001f;
		vec3_t		speed, accel;
		float		push[2];

		VectorSubtract( hip, h->hip, speed );
		VectorScale( speed, 1.0f / dt, speed );
		VectorSubtract( speed, h->hipSpeed, accel );
		VectorScale( accel, 1.0f / dt, accel );
		if ( VectorLength( accel ) > BELT_MAX_ACCEL ) {
			VectorNormalize( accel );
			VectorScale( accel, BELT_MAX_ACCEL, accel );
		}
		VectorCopy( speed, h->hipSpeed );
		VectorCopy( hip, h->hip );
		h->hipTime = cg.time;
		push[0] = DotProduct( accel, boltAxis[0] ) * BELT_PUSH;
		push[1] = DotProduct( accel, boltAxis[1] ) * BELT_PUSH;

		// (in short steps: the spring is too stiff for one step of a slow frame)
		while ( ms > 0 ) {
			const int	step = Q_min( ms, BELT_STEP_MSEC );
			const float	s = step * 0.001f;

			for ( i = 0; i < 2; i++ ) {
				h->speed[i] += ( push[i] * BELT_SPRING * 0.2f - BELT_SPRING * h->swing[i] - BELT_DAMP * h->speed[i] ) * s;
				h->swing[i] = Com_Clamp( -BELT_MAX_SWING, BELT_MAX_SWING, h->swing[i] + h->speed[i] * s );
			}
			ms -= step;
		}
	}
	angles[PITCH] += h->swing[0];
	angles[ROLL] += h->swing[1];
}
