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

// cg_portal_view.c -- the view through a portal of the portal gun (cg_portal_draw.h). The player himself goes through
// once his origin is past the face (bg_pmove.c); the eye can be through before that: in first person his head goes
// first into a floor portal under him or a ceiling portal over him, in third person the camera behind him backs into
// the portal he just came out of. Then the eye is where it would be had the way to it gone through the opening, and
// it looks the way it looked, turned with it.

#include "cgame/cg_local.h"
#include "cg_portal_draw.h"
#include "cg_portals.h"

// the open portal the way from from to to goes into, with nothing in the way up to its opening: where it crosses the
// face, and how far the box reaches along the normal (its traces stop that far from a face)
static qboolean CG_PortalWay( const vec3_t from, const vec3_t to, const vec3_t mins, const vec3_t maxs, int mask,
	const bgPortal_t **in, const bgPortal_t **out, vec3_t at, float *reach ) {
	const bgPortal_t	*list;
	int					count;
	const vec3_t		point = { 0.0f, 0.0f, 0.0f };

	list = CG_PortalList( &count );
	for ( int i = 0; i < count; i++ ) {
		const bgPortal_t	*p = &list[i];
		const bgPortal_t	*partner = BG_PortalPartner( list, count, p );
		const float			*boxMins = mins ? mins : point, *boxMaxs = maxs ? maxs : point;
		vec3_t				front;
		trace_t				tr;

		if ( !partner || !BG_PortalCrossed( p, from, to, boxMins, boxMaxs ) ) {
			continue;
		}
		const float before = BG_PortalSide( p, from ), after = BG_PortalSide( p, to );

		VectorSubtract( to, from, at );
		VectorMA( from, before / ( before - after ), at, at );
		// (right at the face the box stood in the wall round the opening, so the way counted as blocked and the camera
		// stayed against the wall behind the player, their question 2026-11-27)
		*reach = 1.0f;
		if ( mins ) {
			*reach += fabsf( p->axis[0][0] ) * ( boxMaxs[0] - boxMins[0] ) * 0.5f + fabsf( p->axis[0][1] ) * ( boxMaxs[1] - boxMins[1] ) * 0.5f
				+ fabsf( p->axis[0][2] ) * ( boxMaxs[2] - boxMins[2] ) * 0.5f;
			// (a wall in front of the opening keeps the camera on this side)
			VectorMA( at, *reach, p->axis[0], front );
			CG_PortalViewTrace( &tr, from, mins, maxs, front, cg.predictedPlayerState.clientNum, mask );
			if ( tr.fraction < 1.0f || tr.startsolid ) {
				continue;
			}
		}
		*in = p;
		*out = partner;
		return qtrue;
	}
	return qfalse;
}

qboolean CG_PortalCameraThrough( const vec3_t from, const vec3_t to, const vec3_t mins, const vec3_t maxs, int mask ) {
	const bgPortal_t	*in, *out;
	vec3_t				at;
	float				reach;

	return CG_PortalWay( from, to, mins, maxs, mask, &in, &out, at, &reach );
}

void CG_PortalView( const vec3_t from, const vec3_t to, const vec3_t mins, const vec3_t maxs, int mask ) {
	const bgPortal_t	*p, *partner;
	vec3_t				at, start, end;
	float				reach;
	trace_t				tr;

	if ( !CG_PortalWay( from, to, mins, maxs, mask, &p, &partner, at, &reach ) ) {
		return;
	}
	// the rest of the way on the other side, from as far in front of the face of the partner
	BG_PortalPoint( p, partner, at, start );
	VectorMA( start, reach, partner->axis[0], start );
	BG_PortalPoint( p, partner, to, end );
	if ( mins ) {
		CG_PortalViewTrace( &tr, start, mins, maxs, end, cg.predictedPlayerState.clientNum, mask );
		VectorCopy( tr.endpos, end );
	}
	VectorCopy( end, cg.refdef.vieworg );
	BG_PortalViewAngles( p, partner, cg.refdef.viewangles, cg.refdef.viewangles );
}

/*
The third person camera follows the player damped (cg_view.c CG_UpdateThirdPersonCameraDamp): it moves only part of
the way to where it should be each frame. It jumps there only when a snapshot says he was teleported, and the move
through a portal is predicted frames before that one comes. So the camera stayed on the side he left, against the wall
round the portal, and looked at him through it (their picture 2026-11-27). When his teleport bit flipped and his origin
of the last frame comes out of a portal close to where he is now, he went through that one: the camera goes through
it as well and follows on from there.
*/
#define PORTAL_CAMERA_NEAR	64.0f	// how close to his origin the one before has to come out of a portal

void CG_PortalCamera( vec3_t loc, vec3_t target ) {
	static vec3_t		lastOrigin;
	static int			lastBit = -1;
	const playerState_t	*ps = &cg.predictedPlayerState;
	const int			bit = ps->eFlags & EF_TELEPORT_BIT;

	if ( lastBit >= 0 && bit != lastBit ) {
		const bgPortal_t	*list;
		int					count;

		list = CG_PortalList( &count );
		for ( int i = 0; i < count; i++ ) {
			const bgPortal_t	*p = &list[i];
			const bgPortal_t	*partner = BG_PortalPartner( list, count, p );
			vec3_t				through;

			if ( !partner ) {
				continue;
			}
			BG_PortalPoint( p, partner, lastOrigin, through );
			if ( DistanceSquared( through, ps->origin ) < PORTAL_CAMERA_NEAR * PORTAL_CAMERA_NEAR ) {
				BG_PortalPoint( p, partner, loc, loc );
				BG_PortalPoint( p, partner, target, target );
				break;
			}
		}
	}
	lastBit = bit;
	VectorCopy( ps->origin, lastOrigin );
}

/*
An eye behind the face of an open portal, inside the opening (in the tunnel: the head or the third person camera got
there before the origin went through), goes through to the partner. A point as far behind one face is as far in front
of the other, so the view goes on without a jump (an eye held a few units off the face jumped by twice that when it
went through, their word 2026-11-27).
*/
void CG_PortalNearGuard( void ) {
	const bgPortal_t	*list;
	int					count;
	const vec3_t		point = { 0.0f, 0.0f, 0.0f };

	list = CG_PortalList( &count );
	for ( int i = 0; i < count; i++ ) {
		const bgPortal_t	*p = &list[i];
		const bgPortal_t	*partner = BG_PortalPartner( list, count, p );
		const float			side = BG_PortalSide( p, cg.refdef.vieworg );

		if ( !partner || side >= 0.0f || side <= -PORTAL_HOLE_DEPTH
			|| !BG_PortalBoxFits( p, cg.refdef.vieworg, point, point, 0.0f ) ) {
			continue;
		}
		BG_PortalPoint( p, partner, cg.refdef.vieworg, cg.refdef.vieworg );
		BG_PortalViewAngles( p, partner, cg.refdef.viewangles, cg.refdef.viewangles );
		return;
	}
}

qboolean CG_PortalOpeningAt( const vec3_t point, const vec3_t mins, const vec3_t maxs ) {
	const bgPortal_t	*list;
	int					count;

	if ( !CG_PortalsDrawn() ) {
		return qfalse;
	}
	list = CG_PortalList( &count );
	for ( int i = 0; i < count; i++ ) {
		const bgPortal_t	*p = &list[i];
		float				middle, reach;

		if ( !BG_PortalPartner( list, count, p ) ) {
			continue;
		}
		// the box touches the face (a unit of slack either way), and lies inside the opening
		BG_PortalBoxAlong( p, point, mins, maxs, 0, &middle, &reach );
		if ( fabsf( middle - reach ) <= 1.0f && BG_PortalBoxFits( p, point, mins, maxs, 0.0f ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

qboolean CG_PortalEyeNear( float range ) {
	const bgPortal_t	*list;
	int					count;
	const vec3_t		point = { 0.0f, 0.0f, 0.0f };

	if ( !CG_PortalsDrawn() ) {
		return qfalse;
	}
	list = CG_PortalList( &count );
	for ( int i = 0; i < count; i++ ) {
		const bgPortal_t	*p = &list[i];
		const float			side = BG_PortalSide( p, cg.refdef.vieworg );

		if ( side >= 0.0f && side < range && BG_PortalPartner( list, count, p )
			&& BG_PortalBoxFits( p, cg.refdef.vieworg, point, point, -range ) ) {
			return qtrue;
		}
	}
	return qfalse;
}
