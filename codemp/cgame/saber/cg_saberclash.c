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

// cg_saberclash.c -- blades meeting (issue #20): a flash that lights the room and glowing drops that fall and cool, on
// top of the sparks of the block event. While two players are locked it keeps going at the point where the blades
// cross, which the server does not send: it is worked out here from the blades as they were drawn this frame.
// No switch of its own: the flash is a moving light (v_dlightScale), the drops and sparks are GPU sparks (v_gpuSparks)
// and the flash of the view belongs to v_fxSaber.

#include "cgame/cg_local.h"
#include "cgame/saber/cg_saberclash.h"
#include "cgame/saber/cg_saberdrawn.h"

#define CLASH_LIGHTS		8
#define CLASH_LIGHT_MSEC	190
#define CLASH_LIGHT_RADIUS	430.0f
#define LOCK_RANGE			150.0f		// of two players locked with each other
#define LOCK_SPARK_MSEC		65
#define LOCK_FLASH_MSEC		115
#define LOCK_CONTACT		40.0f		// blades further apart than this do not touch

typedef struct {
	int		birth;
	vec3_t	origin;
	float	strength;
} clashLight_t;

static clashLight_t	lights[CLASH_LIGHTS];
static int			nextLight;
static int			lockSpark[MAX_CLIENTS], lockFlash[MAX_CLIENTS];

void CG_SaberClashInit( void ) {
	memset( lights, 0, sizeof( lights ) );
	memset( lockSpark, 0, sizeof( lockSpark ) );
	memset( lockFlash, 0, sizeof( lockFlash ) );
	nextLight = 0;
}

static void Clash_Light( const vec3_t origin, float strength ) {
	clashLight_t *l = &lights[nextLight];

	nextLight = ( nextLight + 1 ) % CLASH_LIGHTS;
	l->birth = cg.time;
	l->strength = strength;
	VectorCopy( origin, l->origin );
}

void CG_SaberClash( const vec3_t origin, const vec3_t dir, float strength, qboolean flare ) {
	if ( flare ) {
		Clash_Light( origin, strength );
	}
	CG_GpuSparks( origin, dir, GPUSPARKS_DROPS, strength );
}

static qboolean Lock_Player( int num, vec3_t origin ) {
	const qboolean	own = (qboolean)( cg.snap && num == cg.snap->ps.clientNum );
	const centity_t	*cent = &cg_entities[num];
	int				anim;

	if ( !cgs.clientinfo[num].infoValid || ( !own && !cent->currentValid ) ) {
		return qfalse;
	}
	anim = own ? cg.predictedPlayerState.torsoAnim : cent->currentState.torsoAnim;
	if ( !BG_InSaberLock( anim ) && !BG_InSaberLockOld( anim ) ) {
		return qfalse;
	}
	VectorCopy( own ? cg.predictedPlayerState.origin : cent->lerpOrigin, origin );
	return qtrue;
}

// the closest points of the segments a to a + d1 and b to b + d2 (Ericson, Real-Time Collision Detection 5.1.9);
// out: the middle between them; returns the squared distance
static float Seg_Closest( const vec3_t a, const vec3_t d1, const vec3_t b, const vec3_t d2, vec3_t out ) {
	vec3_t		r, p, q;
	const float	aa = DotProduct( d1, d1 ), e = DotProduct( d2, d2 );
	float		s, t, f, c, bb, denom;

	VectorSubtract( a, b, r );
	f = DotProduct( d2, r );
	c = DotProduct( d1, r );
	bb = DotProduct( d1, d2 );
	denom = aa * e - bb * bb;
	s = denom > 0.0001f ? Com_Clamp( 0.0f, 1.0f, ( bb * f - c * e ) / denom ) : 0.0f;
	t = e > 0.0001f ? ( bb * s + f ) / e : 0.0f;
	if ( t < 0.0f ) {
		t = 0.0f;
		s = aa > 0.0001f ? Com_Clamp( 0.0f, 1.0f, -c / aa ) : 0.0f;
	} else if ( t > 1.0f ) {
		t = 1.0f;
		s = aa > 0.0001f ? Com_Clamp( 0.0f, 1.0f, ( bb - c ) / aa ) : 0.0f;
	}
	VectorMA( a, s, d1, p );
	VectorMA( b, t, d2, q );
	VectorAdd( p, q, out );
	VectorScale( out, 0.5f, out );
	return DistanceSquared( p, q );
}

// where the blades of the two cross: the closest point of any blade of one drawn this frame to any of the other
static qboolean Lock_Contact( int a, int b, vec3_t contact ) {
	float	best = LOCK_CONTACT * LOCK_CONTACT;
	int		sa, ba, sb, bb;
	qboolean found = qfalse;

	for ( sa = 0; sa < MAX_SABERS; sa++ ) {
		for ( ba = 0; ba < MAX_BLADES; ba++ ) {
			const saberDrawn_t *one = CG_SaberDrawnBlade( a, sa, ba );

			if ( !one ) {
				continue;
			}
			for ( sb = 0; sb < MAX_SABERS; sb++ ) {
				for ( bb = 0; bb < MAX_BLADES; bb++ ) {
					const saberDrawn_t	*other = CG_SaberDrawnBlade( b, sb, bb );
					vec3_t				at;
					float				d;

					if ( !other ) {
						continue;
					}
					d = Seg_Closest( one->start, one->span, other->start, other->span, at );
					if ( d < best ) {
						best = d;
						found = qtrue;
						VectorCopy( at, contact );
					}
				}
			}
		}
	}
	return found;
}

static qboolean Lock_NoFlare( const clientInfo_t *ci ) {
	return (qboolean)( ( ci->saber[0].saberFlags2 & ( SFL2_NO_CLASH_FLARE | SFL2_NO_CLASH_FLARE2 ) ) != 0 );
}

static void Lock_Frame( int a, int b, const vec3_t originA, const vec3_t originB ) {
	static const vec3_t	up = { 0.0f, 0.0f, 1.0f };
	const clientInfo_t	*ca = &cgs.clientinfo[a], *cb = &cgs.clientinfo[b];
	vec3_t				contact;

	if ( cg.time >= lockSpark[a] || lockSpark[a] - cg.time > LOCK_SPARK_MSEC ) {
		if ( !Lock_Contact( a, b, contact ) ) {
			// (a blade not drawn this frame: between the two, at the height of the hands)
			VectorAdd( originA, originB, contact );
			VectorScale( contact, 0.5f, contact );
			contact[2] += 22.0f;
		}
		lockSpark[a] = cg.time + LOCK_SPARK_MSEC;
		CG_GpuSparks( contact, up, GPUSPARKS_CLASH, 0.22f );
		CG_SaberClash( contact, up, 0.45f, qfalse );

		if ( !Lock_NoFlare( ca ) && !Lock_NoFlare( cb )
			&& ( cg.time >= lockFlash[a] || lockFlash[a] - cg.time > LOCK_FLASH_MSEC ) ) {
			lockFlash[a] = cg.time + LOCK_FLASH_MSEC;
			Clash_Light( contact, 0.65f + 0.35f * Q_flrand( 0.0f, 1.0f ) );
			CG_ScreenFxFlash( contact, 52.0f, 0.6f );
		}
	}
}

void CG_SaberClashAdd( void ) {
	vec3_t	origins[MAX_CLIENTS];
	int		i, j;

	for ( i = 0; i < CLASH_LIGHTS; i++ ) {
		const clashLight_t	*l = &lights[i];
		const int			age = cg.time - l->birth;

		if ( l->birth && age >= 0 && age < CLASH_LIGHT_MSEC ) {
			const float t = 1.0f - age / (float)CLASH_LIGHT_MSEC;
			const float level = t * t * l->strength;

			trap->R_AddLightToScene( l->origin, CLASH_LIGHT_RADIUS * sqrtf( l->strength ) * ( 0.4f + 0.6f * t ),
				level, level * 0.93f, level * 0.8f );
		}
	}
	if ( !cg.snap ) {
		return;
	}
	for ( i = 0; i < cgs.maxclients; i++ ) {
		if ( !Lock_Player( i, origins[i] ) ) {
			continue;
		}
		for ( j = i + 1; j < cgs.maxclients; j++ ) {
			if ( Lock_Player( j, origins[j] ) && DistanceSquared( origins[i], origins[j] ) < LOCK_RANGE * LOCK_RANGE ) {
				Lock_Frame( i, j, origins[i], origins[j] );
				break;
			}
		}
	}
}
