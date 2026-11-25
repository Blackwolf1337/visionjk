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

// cg_hookrope.c -- the rope of the JA+ grappling hook (issue #22, item 22): it sags while it is slack and pulls
// straight when it is tight, and the hook throws sparks with a clink where it lands. The rope is as long as it was
// paid out: a little longer than the way to the hook while the hook flies, as far as the player was from the hook
// at the most since it landed. It is tight while the hook pulls him (PMF_GRAPPLE_PULL, only known of the own
// player), while he hangs on it (EF_GRAPPLE_SUSPEND) or while the way to the hook gets shorter fast. The sag is a
// parabola down across the rope, as deep as the slack asks for, and follows in a short time.

#include "cgame/cg_local.h"
#include "cgame/weapons/cg_hookrope.h"

#define ROPE_SEGMENTS		14
#define ROPE_EASE_MSEC		120.0f		// the sag follows in about that time
#define ROPE_PAID_OUT		1.05f		// the rope while the hook flies, of the way to it
#define ROPE_REEL_SPEED		-120.0f		// units a second: the way to the hook shrinks faster, it pulls
#define ROPE_MAX_SAG		160.0f
#define ROPE_GONE_MSEC		300			// not seen for this long: a new hook
#define HOOK_FLYING			100.0f		// faster than this the hook still flies (CG_JPHookTrail)

typedef struct {
	int			hook;		// entity number of the hook
	int			seen;		// 0: never
	qboolean	landed;
	float		length;		// of the rope paid out
	float		sag;		// units, at the middle
	float		lastDist, rate;
	vec3_t		dir;		// the hook flew, the last frame it did
} hookRope_t;

static hookRope_t ropes[MAX_CLIENTS];

void CG_HookRopesInit( void ) {
	memset( ropes, 0, sizeof( ropes ) );
}

static void Rope_Landed( const hookRope_t *rope, const vec3_t at ) {
	trace_t	tr;
	vec3_t	start, end;

	// on a wall; on a player it follows him (Weapon_HookThink) and throws nothing
	VectorMA( at, -12.0f, rope->dir, start );
	VectorMA( at, 16.0f, rope->dir, end );
	CG_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.fraction >= 1.0f || tr.startsolid || ( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT ) ) ) {
		return;
	}
	CG_GpuSparks( tr.endpos, tr.plane.normal, GPUSPARKS_DRAG_METAL, 1.0f );
	trap->S_StartSound( tr.endpos, ENTITYNUM_WORLD, CHAN_AUTO, cgs.media.metalBounceSound[Q_irand( 0, 1 )] );
}

static void Rope_Segment( const vec3_t from, const vec3_t to ) {
	refEntity_t re;

	// as the stock rope (CG_TestLine with the colour 0x30000): a thin dark line
	memset( &re, 0, sizeof( re ) );
	re.reType = RT_LINE;
	VectorCopy( from, re.origin );
	VectorCopy( to, re.oldorigin );
	re.radius = 0.5f;
	re.customShader = cgs.media.whiteShader;
	re.shaderTexCoord[0] = re.shaderTexCoord[1] = 1.0f;
	re.shaderRGBA[2] = 3;
	re.shaderRGBA[3] = 255;
	re.renderfx = RF_NOSHADOW;
	trap->R_AddRefEntityToScene( &re );
}

void CG_HookRope( const centity_t *hook, const vec3_t hand, const vec3_t end ) {
	const int		owner = hook->currentState.otherEntityNum;
	const qboolean	flying = (qboolean)( VectorLength( hook->currentState.pos.trDelta ) >= HOOK_FLYING );
	hookRope_t		*rope;
	vec3_t			span, hang, from, to;
	float			dist, dt, slack, sag, across;
	int				i;

	if ( !v_weaponImpacts.integer || owner < 0 || owner >= MAX_CLIENTS ) {
		VectorCopy( hand, from );
		VectorCopy( end, to );
		CG_TestLine( from, to, 1, 0x30000, 1 );
		return;
	}
	rope = &ropes[owner];
	VectorSubtract( end, hand, span );
	dist = VectorLength( span );
	if ( !rope->seen || rope->hook != hook->currentState.number || cg.time < rope->seen
		|| cg.time - rope->seen > ROPE_GONE_MSEC ) {
		memset( rope, 0, sizeof( *rope ) );
		rope->hook = hook->currentState.number;
		rope->landed = (qboolean)!flying;	// (came into view already holding: it landed before)
		rope->length = dist * ROPE_PAID_OUT;
		rope->lastDist = dist;
		VectorSet( rope->dir, 0.0f, 0.0f, -1.0f );
		dt = 0.0f;
	} else {
		dt = Com_Clamp( 0.0f, 0.1f, ( cg.time - rope->seen ) * 0.001f );
	}
	rope->seen = cg.time;
	if ( dt > 0.0f ) {
		const float rate = ( dist - rope->lastDist ) / dt;

		rope->rate += ( rate - rope->rate ) * ( 1.0f - expf( -dt * 1000.0f / ROPE_EASE_MSEC ) );
	}
	rope->lastDist = dist;

	if ( flying ) {
		VectorNormalize2( hook->currentState.pos.trDelta, rope->dir );
		rope->landed = qfalse;
		rope->length = dist * ROPE_PAID_OUT;
	} else {
		const centity_t	*oc = &cg_entities[owner];
		qboolean		tight;

		if ( !rope->landed ) {
			rope->landed = qtrue;
			if ( !CG_JPInSeparatedDuel() ) {
				Rope_Landed( rope, hook->lerpOrigin );
			}
		}
		tight = (qboolean)( ( oc->currentState.eFlags & EF_GRAPPLE_SUSPEND ) || rope->rate < ROPE_REEL_SPEED
			|| ( owner == cg.predictedPlayerState.clientNum && ( cg.predictedPlayerState.pm_flags & PMF_GRAPPLE_PULL ) ) );
		rope->length = tight ? dist : Q_max( rope->length, dist );
	}

	// (a parabola of the slack's length: sag = sqrt( 3/8 * span * slack ) for a small sag)
	slack = Q_max( 0.0f, rope->length - dist );
	sag = Q_min( sqrtf( 0.375f * dist * slack ), Q_min( 0.5f * dist, ROPE_MAX_SAG ) );
	rope->sag += ( sag - rope->sag ) * ( dt > 0.0f ? 1.0f - expf( -dt * 1000.0f / ROPE_EASE_MSEC ) : 1.0f );

	// down across the rope: a rope that hangs straight down does not sag
	VectorSet( hang, 0.0f, 0.0f, -1.0f );
	if ( dist > 0.001f ) {
		across = DotProduct( hang, span ) / ( dist * dist );
		VectorMA( hang, -across, span, hang );
	}
	if ( rope->sag < 0.5f || VectorLengthSquared( hang ) < 0.0001f ) {
		Rope_Segment( hand, end );
		return;
	}
	VectorCopy( hand, from );
	for ( i = 1; i <= ROPE_SEGMENTS; i++ ) {
		const float t = i / (float)ROPE_SEGMENTS;

		VectorMA( hand, t, span, to );
		VectorMA( to, 4.0f * t * ( 1.0f - t ) * rope->sag, hang, to );
		Rope_Segment( from, to );
		VectorCopy( to, from );
	}
}
