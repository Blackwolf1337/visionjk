/*
===========================================================================
Copyright (C) 2026, VisionJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
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

#pragma once

// What Steam Audio as it is built for the browser (cmake/Modules/SteamAudioWasm.cmake) has on top of phonon.h.
// Not from Valve: thread_pool.cpp of this folder has the two functions, simulation_manager.cpp.patch makes
// iplSimulatorRunReflections() heed them.
//
// A pass of the reflections traces its rays in jobs of 32 and takes tens of milliseconds as a whole. On the
// desktop that is the work of a thread. In a page there is one thread, and it has a frame to draw: so there a
// call of iplSimulatorRunReflections() may be given a time. When that is up after a job and rays are left, the
// call returns, and the next call goes on where it stopped. The call that finds no ray left does the rest of
// the pass (the impulse response and what the convolution needs of it, about 2 ms at 1.5 s of reverb), so no
// call does both. Between the first and the last call of a pass the scene of the simulator must stay as it is,
// and the inputs of the reflections (iplSimulatorSetSharedInputs() with IPL_SIMULATIONFLAGS_REFLECTIONS)
// should: the rays that are left start from where the listener is by then.

#ifdef __cplusplus
extern "C" {
#endif

// usec: how long a call of iplSimulatorRunReflections() traces rays, in microseconds. 0: until all are traced,
// which is what the library does without this.
void	iplxSetSliceBudget( int usec );

// not 0: the last call of iplSimulatorRunReflections() left its pass unfinished
int		iplxSlicePending( void );

#ifdef __cplusplus
}
#endif
