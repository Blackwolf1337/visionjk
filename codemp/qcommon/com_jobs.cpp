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

// com_jobs.cpp -- one pool of worker threads for work that can be split, shared by the engine and the renderer
// (through its import table). See docs/japlus/threading.md.
//
// A job is a function and its data, put into a group. The group is asked whether its jobs are done (never waits,
// for the frames of the game) or waited for (for a load, and for work a frame cannot go on without). Who waits runs
// queued jobs of his own group himself, so a job may wait for a group of its own (a texture packed in bands inside
// the job that decodes it), and a wait never picks up a long job of somebody else.
//
// A job must not call anything of the engine that is only for the main thread: the file system, the zone and hunk
// memory, cvars that are set, Com_Printf, Com_Error (it would throw on the wrong thread and end the program).
// Memory from malloc or new is fine.
//
// v_jobThreads sets the number of workers at the start, 0 picks one less than the processor has threads (a dedicated
// server none). Without
// workers (v_jobThreads -1, or one processor thread) every job runs at once on the thread that adds it.

#include "qcommon/qcommon.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#define JOBS_MAX_THREADS	63

struct jobGroup_s {
	std::atomic<int>	pending;	// added and not yet finished
};

typedef struct job_s {
	jobFunc_t			func;
	void				*data;
	jobGroup_t			*group;
} job_t;

// made with new and never freed: the workers are detached and may still sleep on it while the program ends
typedef struct jobPool_s {
	std::mutex				mutex;
	std::condition_variable	wake;		// for the workers: a job was added
	std::condition_variable	done;		// for who waits: a job of some group was finished, or a job was added
	std::deque<job_t>		queue;
	int						numThreads;
	std::atomic<long long>	busyUsec;	// the workers have run jobs, together (the profiler, v_profiler)
	std::atomic<int>		jobsRun;	// by the workers
} jobPool_t;

static jobPool_t	*jobs;
static cvar_t		*v_jobThreads;

static void Jobs_Run( const job_t &job ) {
	job.func( job.data );
	if ( job.group->pending.fetch_sub( 1 ) == 1 ) {
		// (under the lock, so that a waiter between its check and its sleep does not miss it)
		std::lock_guard<std::mutex> lock( jobs->mutex );
		jobs->done.notify_all();
	}
}

static long long Jobs_Usec( void ) {
	return (long long)std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now().time_since_epoch() ).count();
}

static void Jobs_Worker( int number ) {
	char name[16];

	// (not va: its buffers are shared by every thread)
	snprintf( name, sizeof( name ), "vjk job %i", number );
	Sys_SetThreadName( name );
	for ( ;; ) {
		job_t job;
		{
			std::unique_lock<std::mutex> lock( jobs->mutex );

			jobs->wake.wait( lock, [] { return !jobs->queue.empty(); } );
			job = jobs->queue.front();
			jobs->queue.pop_front();
		}
		const long long start = Jobs_Usec();

		Jobs_Run( job );
		jobs->busyUsec += Jobs_Usec() - start;
		jobs->jobsRun++;
	}
}

void Com_JobsInit( void ) {
	int want;

	v_jobThreads = Cvar_Get( "v_jobThreads", "0", CVAR_ARCHIVE_ND | CVAR_LATCH, "Worker threads for loading and other work that can be split, from the next start. 0: one less than the processor has threads, -1: none" );
	if ( jobs ) {
		return;
	}
	jobs = new jobPool_t;
	jobs->numThreads = 0;
	jobs->busyUsec = 0;
	jobs->jobsRun = 0;
	want = v_jobThreads->integer;
	// (a dedicated server has nothing for them yet: none unless asked for)
	if ( want == 0 && !( com_dedicated && com_dedicated->integer ) ) {
		want = (int)std::thread::hardware_concurrency() - 1;
	}
	want = Com_Clampi( 0, JOBS_MAX_THREADS, want );
#ifdef __EMSCRIPTEN__
	want = 0;	// (a module in a browser has no threads of its own)
#endif
	for ( int i = 0; i < want; i++ ) {
		try {
			std::thread( Jobs_Worker, i + 1 ).detach();
		} catch ( ... ) {
			break;
		}
		jobs->numThreads++;
	}
	Com_Printf( "job threads: %i\n", jobs->numThreads );
}

int Com_JobThreads( void ) {
	return jobs ? jobs->numThreads : 0;
}

// what the workers did so far, for the profiler: jobs run and the time they took together
void Com_JobStats( int *jobsRun, double *busyMs ) {
	*jobsRun = jobs ? jobs->jobsRun.load() : 0;
	*busyMs = jobs ? jobs->busyUsec.load() / 1000.0 : 0.0;
}

jobGroup_t *Com_JobGroupNew( void ) {
	jobGroup_t *group = new jobGroup_t;

	group->pending = 0;
	return group;
}

void Com_JobAdd( jobGroup_t *group, jobFunc_t func, void *data ) {
	const job_t job = { func, data, group };

	group->pending++;
	if ( !jobs || !jobs->numThreads ) {
		Jobs_Run( job );
		return;
	}
	{
		std::lock_guard<std::mutex> lock( jobs->mutex );

		jobs->queue.push_back( job );
	}
	jobs->wake.notify_one();
}

qboolean Com_JobGroupDone( jobGroup_t *group ) {
	return (qboolean)( group->pending.load() == 0 );
}

void Com_JobGroupWait( jobGroup_t *group ) {
	while ( group->pending.load() > 0 ) {
		job_t	job;
		bool	found = false;
		{
			std::unique_lock<std::mutex> lock( jobs->mutex );

			for ( auto it = jobs->queue.begin(); it != jobs->queue.end(); ++it ) {
				if ( it->group == group ) {
					job = *it;
					jobs->queue.erase( it );
					found = true;
					break;
				}
			}
			if ( !found ) {
				// all of them are in the hands of workers: until one of those is through
				jobs->done.wait( lock, [group] { return group->pending.load() == 0; } );
				continue;
			}
		}
		Jobs_Run( job );
	}
}

void Com_JobGroupFree( jobGroup_t *group ) {
	if ( group ) {
		Com_JobGroupWait( group );
		delete group;
	}
}

typedef struct jobFor_s {
	void				( *func )( void *data, int index );
	void				*data;
	int					count;
	std::atomic<int>	next;
} jobFor_t;

static void Jobs_ForTake( void *data ) {
	jobFor_t *loop = (jobFor_t *)data;

	for ( int i; ( i = loop->next++ ) < loop->count; ) {
		loop->func( loop->data, i );
	}
}

/*
===============
Com_JobFor

func( data, index ) for every index from 0 to count - 1, spread over the workers and the caller, done when it returns
===============
*/
void Com_JobFor( int count, void ( *func )( void *data, int index ), void *data ) {
	jobFor_t	loop;
	jobGroup_t	group;
	const int	takers = Q_min( count, Com_JobThreads() + 1 );

	loop.func = func;
	loop.data = data;
	loop.count = count;
	loop.next = 0;
	group.pending = 0;
	for ( int i = 1; i < takers; i++ ) {
		Com_JobAdd( &group, Jobs_ForTake, &loop );
	}
	Jobs_ForTake( &loop );
	if ( takers > 1 ) {
		Com_JobGroupWait( &group );
	}
}
