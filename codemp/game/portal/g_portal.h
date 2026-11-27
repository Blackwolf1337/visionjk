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

// The portal gun in the game module (g_portal.c, game/portal/bg_portal.h). Not from JA+.

#pragma once

#include "game/portal/bg_portal.h"

// at the start of a level: no portals, nobody has the gun, vs_caps says whether the engine cuts the holes
void		G_PortalInit( void );
// every server frame: portals of players who left, changed team or lost the gun go, the partners are sent along,
// the stock effect for the legacy clients
void		G_PortalFrame( void );
void		G_PortalClientGone( int clientNum );
// the portals there are now, up to max; returns how many
int			G_Portals( bgPortal_t *out, int max );
// this player walks through by himself, the traces of his moves have holes (VS_CAPS_PORTAL and V_CAPS_PORTAL)
qboolean	G_PortalMoves( const gentity_t *ent );
// anybody else: put through when he touches an opening, after his Pmove (mins and maxs of that move)
void		G_PortalTouch( gentity_t *ent, const vec3_t mins, const vec3_t maxs );

// the old bryar pistol fires portals for him (vs_portalGun and amportalgun)
qboolean	G_PortalGunReady( const gentity_t *ent );
void		G_PortalFire( gentity_t *ent, qboolean altFire, const vec3_t muzzle, const vec3_t forward );
void		G_PortalRefund( gentity_t *ent, qboolean altFire );
// the orange portal on the press of the alt fire, both portals away on the press of use (ClientThink_real, after Pmove,
// with the buttons of the command)
void		G_PortalButtons( gentity_t *ent, int buttons );
// the gun comes back with every spawn of a player an admin gave it
void		G_PortalGunSpawn( gentity_t *ent );
void		Cmd_amportalgun_f( gentity_t *ent, qboolean rcon );

// open portals a missile or a ray goes through in a frame (no more where two face each other)
#define		PORTAL_RAY_THROUGH	4

// What flies through. A move of an entity on its trajectory from start to origin whose trace tr went into an open
// portal comes out of the partner: trajectory and place turned, the rest of the move traced again from there (tr
// and origin then say how that went). Returns whether it went through.
qboolean	G_PortalTrajectory( gentity_t *ent, const vec3_t start, vec3_t origin, trace_t *tr, int passEntityNum,
	int contentMask, int traceFlags, int useLod );
// A ray of a hitscan weapon from start to end whose trace tr met an open portal: where it went in (at), and the ray
// out of the partner for the rest of its length (start and end of it).
qboolean	G_PortalRay( const vec3_t start, const vec3_t end, const trace_t *tr, vec3_t at, vec3_t outStart, vec3_t outEnd );
// A body that is not moved by traces of its own (a prop): the open portal its box touches on its way into it.
qboolean	G_PortalTouched( const vec3_t origin, const vec3_t mins, const vec3_t maxs, const vec3_t velocity,
	bgPortal_t *in, bgPortal_t *out );
