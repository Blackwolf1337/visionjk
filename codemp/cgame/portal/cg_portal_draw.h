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

// How this client shows the portals of the portal gun (game/portal/bg_portal.h): the openings drawn by the renderer,
// what stands in an opening on both sides, the view that goes on through one, the shots. Not from JA+.

#pragma once

#include "cg_portals.h"

// cg_portal_draw.c
// whether the renderer draws the portals (the ROM cvar v_cgamePortals and ext.R_AddPortal); without it the stock
// effect the server plays at each portal is all there is of them
qboolean			CG_PortalsDrawn( void );
// the portals of the snapshot the client is in, kept until the next one
const bgPortal_t	*CG_PortalList( int *count );
// each portal to the scene of this frame (ext.R_AddPortal)
void				CG_PortalsScene( void );
// Around the drawing of an entity (cg_ents.c CG_AddCEntity): while it stands in an opening, all it adds to the scene
// is added twice, at its place cut at the face of the portal and moved through to the partner cut at its face.
void				CG_PortalEntityBegin( centity_t *cent );
void				CG_PortalEntityEnd( void );
// whether an effect played at origin is the mark the server plays at a portal for the legacy clients, which this client
// does not need (cg_event.c EV_PLAY_EFFECT_ID)
qboolean			CG_PortalMarker( const vec3_t origin );
// a shot of the portal gun as the server sends it (cg_event.c EV_DISRUPTOR_MAIN_SHOT): a beam of the colour of its portal
void				CG_PortalShotBeam( vec3_t start, vec3_t end, int color );

// cg_portal_view.c
// The view goes on through a portal (cg_view.c CG_CalcViewValues): when the way from from (the head in third person,
// the origin in first person) to the eye at to goes through an open opening, the rest of the way is taken on the other
// side, turned with it. mins, maxs and mask: the box of the camera and what stops it there; NULL: the eye is not traced.
void				CG_PortalView( const vec3_t from, const vec3_t to, const vec3_t mins, const vec3_t maxs, int mask );
// whether that way goes through an open opening with nothing in the way up to it: the damped third person camera is not
// held at the face then (cg_view.c CG_UpdateThirdPersonCameraDamp), CG_PortalView takes it on on the other side
qboolean			CG_PortalCameraThrough( const vec3_t from, const vec3_t to, const vec3_t mins, const vec3_t maxs, int mask );
// after the eye is placed: one behind the face of an open portal, in its tunnel, goes through to the partner
void				CG_PortalNearGuard( void );
// whether a box at point (where a trace without holes stopped) sits on the face of an open portal, inside its
// opening: what lies there is no wall (the view weapon is not lowered in front of it, cg_viewweapon.c)
qboolean			CG_PortalOpeningAt( const vec3_t point, const vec3_t mins, const vec3_t maxs );
// whether the eye is less than range in front of an open opening, inside it with range to spare around it: the view
// weapon is drawn after the openings then (the portal pass takes what lies within PORTAL_FACE_RELIEF of the face for
// behind it, and the weapon in the hand reaches there)
qboolean			CG_PortalEyeNear( float range );
// once a frame before the view is worked out: if the player went through a portal since the last frame, the third
// person camera (where it is and what it looks at, both followed damped) goes through with him
void				CG_PortalCamera( vec3_t loc, vec3_t target );
