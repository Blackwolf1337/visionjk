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

// Stands in for core/src/core/thread_pool.cpp of Steam Audio (lib/steamaudio) in the build for the browser,
// see cmake/Modules/SteamAudioWasm.cmake. The class is the one of Valve's thread_pool.h, which is not changed.
//
// Valve's thread pool starts a std::thread for each of its threads when it is made, and hands them the jobs of
// a job graph. A page has one thread: a std::thread can not be started there (the constructor throws), and
// with no threads their process() returns at once with no job done. All the work of Steam Audio that is
// shared out this way (the rays of the reflections, the bake of the paths of a level) goes through this class
// and nowhere else is a thread made, so this is the one file to replace: here the caller of process() does the
// jobs itself, one after the other, as thread 0.
//
// It can also stop before all jobs are done and go on with them in its next call, see phonon_slices.h.

#include "thread_pool.h"
#include "phonon_slices.h"

#include <emscripten.h>

namespace ipl {

static int			sliceBudget;		// microseconds, 0: no limit
static ThreadPool	*slicePool;			// the pool whose last process() left jobs or their follow-up for its next call

ThreadPool::ThreadPool( int numThreads )
	: mCancel( false )
	, mReady( 0 )
	, mCompleted( 0 )
	, mJobsCompleted( 0 )
	, mQuit( false )
	, mJobGraph( nullptr )
{
}

ThreadPool::~ThreadPool()
{
	if ( slicePool == this ) {
		slicePool = nullptr;
	}
}

// Only the simulator of the reflections calls this one with a time set. With a time, a call that did a job says
// that it is not done, also when that was the last job: what its caller does when all jobs are done takes
// time of its own and is left to the call that finds none.
void ThreadPool::process( JobGraph &jobGraph )
{
	const double end = emscripten_get_now() + sliceBudget * 0.001;
	bool worked = false;

	while ( !mCancel && jobGraph.processNextJob( 0, mCancel ) ) {
		worked = true;
		if ( sliceBudget > 0 && emscripten_get_now() >= end ) {
			break;
		}
	}
	slicePool = ( sliceBudget > 0 && worked && !mCancel ) ? this : nullptr;
}

// the bake of the paths of a level: all of it, there is nothing a caller could do with a part
void ThreadPool::process( JobGraph &jobGraph, std::function<void( float )> progressFn )
{
	while ( !mCancel && jobGraph.processNextJob( 0, mCancel ) ) {
		progressFn( ++mJobsCompleted * 1.0f / jobGraph.getNumJobs() );
	}
	mJobsCompleted = 0;
}

void ThreadPool::cancel()
{
	mCancel = true;
}

// never started
void ThreadPool::threadFunc( int threadId )
{
}

}

void iplxSetSliceBudget( int usec )
{
	ipl::sliceBudget = usec;
}

int iplxSlicePending( void )
{
	return ipl::slicePool != nullptr;
}
