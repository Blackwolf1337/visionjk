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

/*
The portal gun (WP_BRYAR_OLD while vs_portalGun is on, g_portal.c): two portals a player, blue and orange, each an
opening in a wall, a floor or a ceiling. What goes into one comes out of the other, turned the way the other faces,
at the speed it went in. Shared by the game (the portals, the moves, what flies through them) and the cgame (the
prediction of the moves, the drawing). Not from JA+.

A portal is an entity of the type ET_INVISIBLE, which a stock cgame does not draw: s.weapon WP_BRYAR_OLD and
s.modelindex PORTAL_MARK tell it from others, s.eventParm is its colour, s.otherEntityNum the player it belongs to,
s.otherEntityNum2 its partner (ENTITYNUM_NONE while there is none), s.origin the middle of the opening on the face of
the wall, s.angles its turn (forward out of the wall, up along the opening). It has SVF_PORTAL with r.s.origin2 at its
partner, so the server sends a client what can be seen through it (sv_snapshot.cpp; s.generic1 is a radius there
and stays 0).

A legacy client sees a stock effect at each portal (g_portal.c) and is put through by the server when he touches an
opening. A client of this tree on a server that says so (VS_CAPS_PORTAL, V_CAPS_PORTAL in bg_public.h) walks
through: the traces of his moves have holes where the portals are (traceHole_t, q_shared.h) and bg_pmove.c takes
him to the other side once his origin is past the face of the wall.
*/

#pragma once

#define PORTAL_MARK			2017	// s.modelindex of a portal (with s.weapon WP_BRYAR_OLD, s.eType ET_INVISIBLE)
#define PORTAL_BLUE			0		// s.eventParm: the colour, the primary fire sets the blue one
#define PORTAL_ORANGE		1		//	and the alt fire the orange one

// the opening: 64 across, 104 high; a player (30 x 30 x 64) goes through upright with room to spare, so he does not
// stick on its rim (48 x 80 held him at the corners, their word 2026-11-27)
#define PORTAL_HALF_WIDTH	32.0f
#define PORTAL_HALF_HEIGHT	52.0f
#define PORTAL_HOLE_DEPTH	64.0f	// how far the hole of a portal goes into the wall behind it
#define PORTAL_IN_FRONT		1.0f	// the hole starts that far in front of the face, so the face itself is no contact
#define PORTAL_TOUCH		8.0f	// a legacy player who comes this close to an opening is put through

#define MAX_PORTALS			64		// of all players at once (one pair each)

typedef struct bgPortal_s {
	vec3_t		origin;				// the middle of the opening, on the face of the wall
	vec3_t		axis[3];			// 0 out of the wall, 1 to the left, 2 up along the opening
	int			entityNum;
	int			partner;			// entity number of the other portal, ENTITYNUM_NONE: none, it is closed
	int			owner;				// the client it belongs to
	int			color;				// PORTAL_BLUE, PORTAL_ORANGE
} bgPortal_t;

// whether an entity is a portal, and what it says about itself
qboolean	BG_IsPortal( const entityState_t *s );
void		BG_PortalFromState( const entityState_t *s, bgPortal_t *out );
// the turn of a portal from the normal of the wall and the way the player looked when he set it: on a wall up stays
// up, on a floor or a ceiling the top of the opening points the way he looked
void		BG_PortalAxis( const vec3_t normal, const vec3_t look, vec3_t axis[3] );
void		BG_PortalAngles( const vec3_t axis[3], vec3_t angles );

// From portal in to portal out, turned half round about the up of the opening: a point behind the face of in (in its
// tunnel, where a body that goes in is) comes out as far in front of out, and what moved into the wall of in moves
// out of the wall of out. A point in front of in lands behind out: the eye of the view through in looks out of the
// wall of out from there (rd-vulkan-vision R_PortalViews, the third person camera).
void		BG_PortalPoint( const bgPortal_t *in, const bgPortal_t *out, const vec3_t point, vec3_t result );
void		BG_PortalVector( const bgPortal_t *in, const bgPortal_t *out, const vec3_t vec, vec3_t result );
// view angles: the way they look goes through as a vector, and they come out without roll (a player does not roll)
void		BG_PortalViewAngles( const bgPortal_t *in, const bgPortal_t *out, const vec3_t angles, vec3_t result );

// how far a point lies in front of the face of the wall (below 0: behind it, in the wall)
float		BG_PortalSide( const bgPortal_t *p, const vec3_t point );
// a box at origin seen along an axis of the portal (0 out of the wall, 1 across, 2 up): where its middle is from the
// middle of the opening, and how far it reaches from its middle
void		BG_PortalBoxAlong( const bgPortal_t *p, const vec3_t origin, const vec3_t mins, const vec3_t maxs, int axis,
	float *middle, float *reach );
// whether a box at origin, seen along the normal of the portal, lies inside the opening, with margin to spare
qboolean	BG_PortalBoxFits( const bgPortal_t *p, const vec3_t origin, const vec3_t mins, const vec3_t maxs, float margin );
// whether a box went through the opening on its way from to to: from in front of the face, to behind it, and where
// it crossed the face, inside the opening
qboolean	BG_PortalCrossed( const bgPortal_t *p, const vec3_t from, const vec3_t to, const vec3_t mins, const vec3_t maxs );

// Where a box at origin that goes into portal in comes out of portal out: BG_PortalPoint, then moved along
// the opening as far as it takes to fit into it (a box that went through off the middle of a floor is taller than a
// wall portal is high from there), and with ahead above 0 at least that far in front of the face of out (a legacy
// player has no hole to stand in).
void		BG_PortalExit( const bgPortal_t *in, const bgPortal_t *out, const vec3_t origin, const vec3_t mins,
	const vec3_t maxs, float ahead, vec3_t result );

// the hole in the solid world behind an open portal, for the traces with holes
void		BG_PortalHole( const bgPortal_t *p, traceHole_t *hole );
// the holes of the open portals of a list, up to max
int			BG_PortalHoles( const bgPortal_t *portals, int numPortals, traceHole_t *holes, int max );
// the partner of a portal in a list, NULL while it is closed
const bgPortal_t *BG_PortalPartner( const bgPortal_t *portals, int numPortals, const bgPortal_t *p );
// the holes of the open portals of a list nearest to a point first, up to max (MAX_TRACE_HOLES is less than
// MAX_PORTALS; the far ones do not touch the traces of a move anyway)
int			BG_PortalHolesNear( const bgPortal_t *portals, int numPortals, const vec3_t point, traceHole_t *holes, int max );
