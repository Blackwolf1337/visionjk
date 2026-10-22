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

// g_clone.h -- the clone (g_clone.c): a bot that plays like the player it was trained on (tools/trainer)

#pragma once

#define CLONE_HEADS		6		// move, jump, hook, attack, aim yaw, aim pitch (trainerlib.HEADS)

// what the clone does in one frame
typedef struct {
	int		forwardmove, rightmove, upmove;
	int		buttons;
	float	yawOff;		// the player this far to the left of the view
	float	pitch;		// the view this far down
} cloneCmd_t;

// what the clone keeps from frame to frame
typedef struct {
	int		prev[CLONE_HEADS];	// the decisions of the frame before (features pMove to pYaw)
	int		since[CLONE_HEADS];	// level.time each decision last changed
	float	yawOff, pitch;		// the aim, smoothed
} cloneState_t;

qboolean	Clone_Ready( void );
void		Clone_Reset( cloneState_t *s );
void		Clone_Decide( const gentity_t *self, const gentity_t *enemy, cloneState_t *s, int elapsed, cloneCmd_t *out );
