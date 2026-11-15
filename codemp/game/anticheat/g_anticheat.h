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

// g_anticheat.h -- moves that no hand makes (g_anticheat.c), included by g_local.h before clientPersistant_t

#pragma once

#define AC_WIGGLE_SAMPLES	256		// the moves kept for the csv, two seconds at 125 a second

typedef struct acSample_s {
	int		time;					// serverTime of the move
	float	yaw;					// the yaw of the move, degrees
	float	speed;					// degrees per second since the move before
	float	enemyErr;				// yaw minus the bearing to the nearest enemy in reach, 999 for none
	byte	engaged;				// attacking with an enemy in reach
	float	aimErr;					// yaw minus the bearing to the aim target where the client saw it, 999 for none
	float	aimRate;				// degrees per second the bearing to that target turned
	short	aimTarget;				// its client number, -1 for none
	byte	aimVisible;				// no wall between the eye and it
} acSample_t;

typedef struct anticheat_s {
	// the cap of a single move (G_AC_MoveTime)
	int			warpLogTime;		// level.time of the last line about a cut move
	int			warpCuts;			// cuts of a long move since warpCutsStart
	int			warpCutsStart;
	int			lastMoveLevelTime;	// level.time of the last move the client sent

	// the wiggle (G_AC_Think)
	int			lastTime;			// serverTime of the last move looked at
	int			lastYaw;			// its cmd.angles[YAW], 16 bit
	int			lastDir;			// -1, 0, 1: the way the view last turned fast
	int			lastRevTime;		// serverTime of the last reversal

	int			windowStart;		// serverTime the window began
	int			windowEngaged;		// msec of it spent attacking with an enemy in reach
	int			windowRevs;			// reversals while engaged
	int			windowIntervals;	// how many reversal intervals are summed below
	float		windowIntervalSum, windowIntervalSq;
	int			windowsBad;			// windows in a row over the limit

	// the aimbot (G_AC_Aim)
	int			aimTarget;			// client number of the last aim target, -1 for none
	float		aimLastBearing;		// the bearing to it at the last move
	int			aimWindowStart;		// serverTime the window began
	int			aimFastMs;			// msec of it the target crossed the view fast
	int			aimLockMs;			// msec of those the view sat on it
	int			aimWindowsBad;
	int			turnStart;			// serverTime a fast turn began, 0 while none
	float		turnAccum;			// degrees it turned so far
	int			turnDir;
	int			snapTimes[4];		// serverTime of the last snaps onto a target
	int			snapActTimes[4];	// serverTime of the last snaps followed by a shot or a force power
	int			lastSnapTime;		// serverTime of the last snap, for the shot after it
	int			lastActTime;		// serverTime of the last shot or aimed force power, for the snap after it
	int			lastButtons;		// buttons of the move before
	int			aimFlags;
	int			aimLogTime;			// level.time of the last aim line
	int			aimWarnTime;		// level.time the player was last warned

	int			blockUntil;			// level.time until which the saber hurts no player
	int			flags;				// how often this player was flagged
	int			dumpTime;			// level.time of the last csv

	acSample_t	samples[AC_WIGGLE_SAMPLES];
	int			head, count;
} anticheat_t;

int			G_AC_MoveTime( gentity_t *ent, usercmd_t *ucmd, int msec );
void		G_AC_Think( gentity_t *ent, const usercmd_t *ucmd );
qboolean	G_AC_SaberBlocked( const gentity_t *attacker, const gentity_t *victim );
