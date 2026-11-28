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

// cl_profstats.h: what the full profiler (v_profiler 3 and 4) reads off the system every half second on a worker
// thread: the threads of the program and the values the driver of the card gives

#pragma once

#include "rd-common/tr_types.h"

#define PROF_THREAD_ROWS	8

typedef struct profThreadRow_s {
	char		name[32];
	float		percent;				// of one processor thread
} profThreadRow_t;

typedef struct profStats_s {
	int				serial;				// readings so far, 0: none yet

	int				numThreads;			// of the program
	int				numRows;			// the busiest, the busiest first
	profThreadRow_t	rows[PROF_THREAD_ROWS];
	int				restThreads;		// the others
	float			restPercent;
	float			totalPercent;		// the whole program, 100: one processor thread

	// the card, from its driver; -1 (0 for the memory): not known
	const char		*cardSource;		// "NVML", "amdgpu", NULL: no values of the driver
	int				loadPercent, clockMHz, memoryClockMHz, temperature;
	float			powerW, powerLimitW;
	float			vramUsedMB, vramTotalMB;	// of the whole card, every program on it
} profStats_t;

void				CL_ProfStats_Frame( const gpuProfile_t *gpu );	// every frame while the full panel is open
const profStats_t	*CL_ProfStats( void );
