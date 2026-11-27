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

// bg_portal.c -- the portals of the portal gun, as the game and the cgame both need them (bg_portal.h)

#include "qcommon/q_shared.h"
#include "game/bg/bg_public.h"
#include "bg_portal.h"

qboolean BG_IsPortal( const entityState_t *s ) {
	return (qboolean)( s->eType == ET_INVISIBLE && s->weapon == WP_BRYAR_OLD && s->modelindex == PORTAL_MARK );
}

void BG_PortalFromState( const entityState_t *s, bgPortal_t *out ) {
	VectorCopy( s->origin, out->origin );
	AnglesToAxis( s->angles, out->axis );
	out->entityNum = s->number;
	out->partner = s->otherEntityNum2;
	out->owner = s->otherEntityNum;
	out->color = s->eventParm;
}

void BG_PortalAxis( const vec3_t normal, const vec3_t look, vec3_t axis[3] ) {
	vec3_t up;

	VectorNormalize2( normal, axis[0] );
	if ( fabsf( axis[0][2] ) < 0.7f ) {
		// a wall: the top of the opening as near to straight up as the wall lets it be
		VectorSet( up, 0.0f, 0.0f, 1.0f );
	} else {
		// a floor or a ceiling: the top points the way he looked
		VectorCopy( look, up );
	}
	VectorMA( up, -DotProduct( up, axis[0] ), axis[0], up );
	if ( VectorNormalize( up ) < 0.01f ) {
		VectorSet( up, 1.0f, 0.0f, 0.0f );
		VectorMA( up, -DotProduct( up, axis[0] ), axis[0], up );
		VectorNormalize( up );
	}
	VectorCopy( up, axis[2] );
	// (forward, left, up as AnglesToAxis makes them: up x forward = left)
	CrossProduct( axis[2], axis[0], axis[1] );
}

// the angles whose axis (AnglesToAxis) is this one: pitch and yaw from the forward axis, the roll that puts up where
// it is (of the two turns that might, the one that does)
void BG_PortalAngles( const vec3_t axis[3], vec3_t angles ) {
	matrix3_t	flat;
	float		roll;
	vec3_t		test;
	matrix3_t	turned;

	vectoangles( axis[0], angles );
	angles[ROLL] = 0.0f;
	AnglesToAxis( angles, flat );
	roll = RAD2DEG( atan2f( DotProduct( axis[2], flat[1] ), DotProduct( axis[2], flat[2] ) ) );
	VectorSet( test, angles[PITCH], angles[YAW], roll );
	AnglesToAxis( test, turned );
	if ( DotProduct( turned[2], axis[2] ) < 0.999f ) {
		roll = -roll;
	}
	angles[ROLL] = roll;
}

void BG_PortalVector( const bgPortal_t *in, const bgPortal_t *out, const vec3_t vec, vec3_t result ) {
	const float	f = DotProduct( vec, in->axis[0] ), l = DotProduct( vec, in->axis[1] ), u = DotProduct( vec, in->axis[2] );
	vec3_t		v;

	// half round about the up of the opening: forward and left change sign
	VectorScale( out->axis[0], -f, v );
	VectorMA( v, -l, out->axis[1], v );
	VectorMA( v, u, out->axis[2], v );
	VectorCopy( v, result );
}

void BG_PortalPoint( const bgPortal_t *in, const bgPortal_t *out, const vec3_t point, vec3_t result ) {
	vec3_t	local;

	VectorSubtract( point, in->origin, local );
	BG_PortalVector( in, out, local, local );
	VectorAdd( out->origin, local, result );
}

void BG_PortalViewAngles( const bgPortal_t *in, const bgPortal_t *out, const vec3_t angles, vec3_t result ) {
	vec3_t	forward, through;

	AngleVectors( angles, forward, NULL, NULL );
	BG_PortalVector( in, out, forward, through );
	vectoangles( through, result );
	result[ROLL] = 0.0f;
}

float BG_PortalSide( const bgPortal_t *p, const vec3_t point ) {
	vec3_t	d;

	VectorSubtract( point, p->origin, d );
	return DotProduct( d, p->axis[0] );
}

void BG_PortalBoxAlong( const bgPortal_t *p, const vec3_t origin, const vec3_t mins, const vec3_t maxs, int axis,
	float *middle, float *reach ) {
	vec3_t	center, half;

	for ( int i = 0; i < 3; i++ ) {
		center[i] = origin[i] + ( mins[i] + maxs[i] ) * 0.5f - p->origin[i];
		half[i] = ( maxs[i] - mins[i] ) * 0.5f;
	}
	*middle = DotProduct( center, p->axis[axis] );
	*reach = fabsf( p->axis[axis][0] ) * half[0] + fabsf( p->axis[axis][1] ) * half[1] + fabsf( p->axis[axis][2] ) * half[2];
}

qboolean BG_PortalBoxFits( const bgPortal_t *p, const vec3_t origin, const vec3_t mins, const vec3_t maxs, float margin ) {
	float	middle, reach;

	BG_PortalBoxAlong( p, origin, mins, maxs, 1, &middle, &reach );
	if ( fabsf( middle ) + reach > PORTAL_HALF_WIDTH - margin ) {
		return qfalse;
	}
	BG_PortalBoxAlong( p, origin, mins, maxs, 2, &middle, &reach );
	return (qboolean)( fabsf( middle ) + reach <= PORTAL_HALF_HEIGHT - margin );
}

qboolean BG_PortalCrossed( const bgPortal_t *p, const vec3_t from, const vec3_t to, const vec3_t mins, const vec3_t maxs ) {
	const float	before = BG_PortalSide( p, from ), after = BG_PortalSide( p, to );
	vec3_t		at;

	if ( before < 0.0f || after >= 0.0f ) {
		return qfalse;
	}
	// where the origin went through the face, the box there inside the opening (a little slack: the hole held it)
	VectorSubtract( to, from, at );
	VectorMA( from, before / ( before - after ), at, at );
	return BG_PortalBoxFits( p, at, mins, maxs, -1.0f );
}

void BG_PortalHole( const bgPortal_t *p, traceHole_t *hole ) {
	VectorCopy( p->origin, hole->origin );
	VectorCopy( p->axis[0], hole->axis[0] );
	VectorCopy( p->axis[1], hole->axis[1] );
	VectorCopy( p->axis[2], hole->axis[2] );
	VectorSet( hole->half, PORTAL_HOLE_DEPTH, PORTAL_HALF_WIDTH, PORTAL_HALF_HEIGHT );
}

void BG_PortalExit( const bgPortal_t *in, const bgPortal_t *out, const vec3_t origin, const vec3_t mins, const vec3_t maxs,
	float ahead, vec3_t result ) {
	const float	half[3] = { 0.0f, PORTAL_HALF_WIDTH, PORTAL_HALF_HEIGHT };
	float		middle, reach;
	vec3_t		at;

	BG_PortalPoint( in, out, origin, at );
	for ( int axis = 1; axis < 3; axis++ ) {
		float limit;

		BG_PortalBoxAlong( out, at, mins, maxs, axis, &middle, &reach );
		// a quarter of a unit to spare, the traces with holes want it inside, not on the rim
		limit = half[axis] - reach - 0.25f;
		if ( limit < 0.0f ) {
			limit = 0.0f;
		}
		if ( middle > limit ) {
			VectorMA( at, limit - middle, out->axis[axis], at );
		} else if ( middle < -limit ) {
			VectorMA( at, -limit - middle, out->axis[axis], at );
		}
	}
	if ( ahead > 0.0f ) {
		BG_PortalBoxAlong( out, at, mins, maxs, 0, &middle, &reach );
		if ( middle - reach < ahead ) {
			VectorMA( at, ahead - ( middle - reach ), out->axis[0], at );
		}
	}
	VectorCopy( at, result );
}

const bgPortal_t *BG_PortalPartner( const bgPortal_t *portals, int numPortals, const bgPortal_t *p ) {
	if ( p->partner == ENTITYNUM_NONE ) {
		return NULL;
	}
	for ( int i = 0; i < numPortals; i++ ) {
		if ( portals[i].entityNum == p->partner ) {
			return &portals[i];
		}
	}
	return NULL;
}

int BG_PortalHoles( const bgPortal_t *portals, int numPortals, traceHole_t *holes, int max ) {
	int count = 0;

	for ( int i = 0; i < numPortals && count < max; i++ ) {
		// (a closed portal is a wall like any other)
		if ( BG_PortalPartner( portals, numPortals, &portals[i] ) ) {
			BG_PortalHole( &portals[i], &holes[count++] );
		}
	}
	return count;
}

int BG_PortalHolesNear( const bgPortal_t *portals, int numPortals, const vec3_t point, traceHole_t *holes, int max ) {
	const bgPortal_t	*open[MAX_PORTALS];
	float				dist[MAX_PORTALS];
	int					numOpen = 0, count;

	for ( int i = 0; i < numPortals && numOpen < MAX_PORTALS; i++ ) {
		if ( BG_PortalPartner( portals, numPortals, &portals[i] ) ) {
			open[numOpen] = &portals[i];
			dist[numOpen++] = DistanceSquared( point, portals[i].origin );
		}
	}
	// the nearest first, picked one by one (there are only a few)
	for ( count = 0; count < max && count < numOpen; count++ ) {
		const bgPortal_t	*p;
		float				d;
		int					best = count;

		for ( int j = count + 1; j < numOpen; j++ ) {
			if ( dist[j] < dist[best] ) {
				best = j;
			}
		}
		p = open[best];
		d = dist[best];
		open[best] = open[count];
		dist[best] = dist[count];
		open[count] = p;
		dist[count] = d;
		BG_PortalHole( p, &holes[count] );
	}
	return count;
}
