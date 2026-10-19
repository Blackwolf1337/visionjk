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

// g_unlagged.h -- lag compensation (g_unlagged.c), included by g_local.h before gclient_s

#pragma once

// the bits of tay_unlagged
#define UNLAGGED_HITSCAN		1		// disruptor, the alt fire of the concussion rifle, stun baton, melee
#define UNLAGGED_PROJECTILES	2		// a missile starts as far along as the shooter's lag
#define UNLAGGED_PUSH			4		// force push and pull

// 32 server frames: 800 ms at sv_fps 40, 400 ms at 80, more than tay_unlaggedMax may ask for
#define UNLAGGED_TRAIL			32

typedef struct unlaggedPoint_s {
	int		time;					// level.time of the frame
	vec3_t	origin;					// r.currentOrigin
	vec3_t	mins, maxs;
	float	yaw;					// s.apos.trBase[YAW], what the ghoul2 trace of a player turns the model by
} unlaggedPoint_t;

typedef struct unlagged_s {
	unlaggedPoint_t	trail[UNLAGGED_TRAIL];
	int				head;			// the newest point
	int				count;
	int				teleportBit;	// EF_TELEPORT_BIT at the last point, a change starts the trail anew

	unlaggedPoint_t	saved;			// where the client really is while it is shifted
	unlaggedPoint_t	shifted;		// where the shift put it
	qboolean		isShifted;
} unlagged_t;

void		G_UnlaggedStore( gentity_t *ent );
void		G_UnlaggedClear( gentity_t *ent );
int			G_UnlaggedLag( const gentity_t *shooter );
qboolean	G_UnlaggedShift( gentity_t *shooter, int what );
void		G_UnlaggedRestore( gentity_t *shooter );
