/*
===========================================================================
Copyright (C) 2013 - 2015, OpenJK contributors
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

// snd_spatial_page.cpp -- the client in a browser (__EMSCRIPTEN__): a page has one thread, so the game does
// the passes of the reflections itself, a slice of one in a frame. See snd_spatial.cpp.

#include "snd_spatial_local.h"

#if defined( JP_SPATIAL ) && defined( __EMSCRIPTEN__ )

// spatial.refusedProbes says how many it has
void S_SpatialSayTooLarge( void ) {
	Com_Printf( "v_spatial: this level has %i probes, a page works the paths out for up to %i: no sound round corners here\n",
		spatial.refusedProbes, SPATIAL_PAGE_PROBES );
}

/*
===============
S_SpatialSlice

A page: what the thread of S_SpatialWorker() does elsewhere. A pass that was asked for is begun, one that is
begun gets its next slice: Steam Audio traces rays for limit microseconds and returns
(lib/steamaudio-wasm/phonon_slices.h), and the call that finds no ray left makes the impulse response of them.
A slice is one job of 32 rays at least, whatever limit says. limit 0: what is left of a pass that is begun,
all at once, which is for when the game stands still anyway. While a pass is in its slices the state is
WORKER_RUNNING, and what must wait for the thread elsewhere waits here too.
===============
*/
static void S_SpatialSlice( int limit ) {
	const int start = S_SpatialUsec();
	int asked = WORKER_ASKED;
	int took;

	// a pass that was asked for begins, unless all that is wanted is the end of one that is begun
	if ( spatialWorkerState.compare_exchange_strong( asked, limit ? WORKER_RUNNING : WORKER_IDLE ) ) {
		spatial.passUsec = spatial.passSlices = spatial.passPeakUsec = 0;
		spatial.passBegun = Sys_Milliseconds();
		if ( limit ) {
			S_SpatialPassInputs();
		}
	}
	if ( spatialWorkerState.load() != WORKER_RUNNING ) {
		return;
	}
	qiplxSetSliceBudget( limit );
	qiplSimulatorRunReflections( spatial.simulator );
	qiplxSetSliceBudget( 0 );

	took = S_SpatialUsec() - start;
	// what is done while the game stands still anyway takes nothing off the frames after it
	if ( limit ) {
		spatial.passCredit -= took;
	}
	spatial.passUsec += took;
	spatial.passSlices++;
	spatial.passPeakUsec = Q_max( spatial.passPeakUsec, took );
	spatial.costUsec[COST_PASS] += took;
	spatial.costFrameUsec += took;
	if ( !qiplxSlicePending() ) {
		spatialReflectUsec.store( spatial.passUsec );
		spatialReflectPasses.fetch_add( 1 );
		spatial.lastPassSlices = spatial.passSlices;
		spatial.lastPassPeakUsec = spatial.passPeakUsec;
		spatial.lastPassMsec = Sys_Milliseconds() - spatial.passBegun;
		spatialWorkerState.store( WORKER_IDLE );
	}
}

// microseconds of a frame for the passes
int S_SpatialBudget( void ) {
	return (int)( Com_Clamp( 0.1f, 8.0f, v_spatialBudget->value ) * 1000.0f );
}

// The level was built while it loads (S_SpatialLoadMap): taken over now, and its paths worked out.
void S_SpatialPageLoadMap( void ) {
	// the pass that may be in its slices is brought to its end, so that the simulator can be given the level
	S_SpatialSlice( 0 );
	S_SpatialTakeScene();
	S_SpatialThinkPaths();		// the bake, if this level has no file
	S_SpatialThinkPaths();		//	and what it made
}

// A page: the ways round the corners are looked up in what was worked out for the level, which is little
// work (20 microseconds for two sounds behind a wall). Done here and as often as the desktop does it, and
// not as a part of a pass, which takes a page many frames.
void S_SpatialPagePaths( qboolean paths, int now ) {
	if ( paths && !spatial.sceneWaits && now - spatial.pathsAsked >= SPATIAL_REFLECT_MSEC ) {
		const int start = S_SpatialUsec();

		spatial.pathsAsked = now;
		S_SpatialFillPaths();
		S_SpatialPathInputs();
		qiplSimulatorRunPathing( spatial.simulator );
		S_SpatialCopyPaths();
		spatial.costUsec[COST_PASS] += S_SpatialUsec() - start;
	}
}

// once a frame from S_SpatialUpdate(), while the sound runs
void S_SpatialPageUpdate( void ) {
	// Every frame gives the passes v_spatialBudget of time, and a slice takes off that what it took: the
	// least a slice can be is 32 rays with all their bounces, which is more than a frame gives, and what makes
	// the impulse response at the end of a pass is 2 ms in one piece. So some frames go without a slice.
	spatial.costFrameUsec = 0;
	spatial.passCredit = Q_min( spatial.passCredit, 0 ) + S_SpatialBudget();
	if ( spatial.passCredit > 0 ) {
		S_SpatialSlice( spatial.passCredit );
	}
	// switched on in a level that has no paths: a page has no thread that could work them out now
	if ( v_spatialPaths->modified ) {
		v_spatialPaths->modified = qfalse;
		if ( v_spatialPaths->integer && spatial.level.scene && !spatial.paths.batch && !spatial.paths.wait ) {
			if ( spatial.refusedProbes ) {
				S_SpatialSayTooLarge();
			} else {
				Com_Printf( "v_spatialPaths: a page works the paths of a level out while it loads, this one has them from its next load on\n" );
			}
		}
	}
}

#endif // JP_SPATIAL && __EMSCRIPTEN__
