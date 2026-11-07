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

// com_load.cpp -- what the load of a level and a long frame were spent on
//
// Two reports, both off by default, for who wants to know why a level takes long to load or why the game
// stood still for a moment:
//
//   v_loadReport 1   after the load of a level a table: what kinds of things were loaded, how many of each
//                      and how long they took together, up to the end of the first frame of the game.
//   v_hitchLog <ms>  a frame that took longer than that says what was loaded in it, the longest first.
//
// The code that loads something says so around it: Com_LoadPush( kind, name ) before and Com_LoadPop() after.
// The renderer does that through its import table. They nest (a model loads its images): the time of what is
// inside is taken off what is around it, so every microsecond is counted once and the kinds add up.
//
// Only the main thread calls this.

#include "qcommon/qcommon.h"
#include <chrono>

#define LOAD_MAX_DEPTH		16
#define LOAD_MAX_EVENTS		512		// of a frame: the rest is added up and not named
#define LOAD_MAX_KINDS		48
#define LOAD_NAME			60
#define LOAD_HITCH_LINES	12
#define LOAD_GIVE_UP		180		// seconds after which a load that never got to a frame is reported as it is

typedef struct loadEvent_s {
	const char	*kind;
	char		name[LOAD_NAME];
	int			usec;				// its own time, without what was loaded inside it
} loadEvent_t;

typedef struct loadKind_s {
	const char	*kind;
	int			count;
	int64_t		usec;
} loadKind_t;

static struct {
	struct {
		const char	*kind;
		char		name[LOAD_NAME];
		int64_t		start, inside;
	} stack[LOAD_MAX_DEPTH];
	int			depth;					// may be more than LOAD_MAX_DEPTH: what is deeper is part of what is around it

	loadEvent_t	events[LOAD_MAX_EVENTS];
	int			numEvents, moreEvents;
	int64_t		moreUsec, frameStart;

	qboolean	loading, reportDue;
	char		what[MAX_QPATH];
	int64_t		loadStart;
	loadKind_t	kinds[LOAD_MAX_KINDS];
	int			numKinds;
} load;

static cvar_t	*v_loadReport;
static cvar_t	*v_hitchLog;

static int64_t Com_LoadUsec( void ) {
	return std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now().time_since_epoch() ).count();
}

void Com_LoadInit( void ) {
	v_loadReport = Cvar_Get( "v_loadReport", "0", 0, "1: after the load of a level, a table of what the time went into" );
	v_hitchLog = Cvar_Get( "v_hitchLog", "0", 0, "A frame that takes more milliseconds than this says what was loaded in it. 0 off" );
}

static qboolean Com_LoadWanted( void ) {
	return (qboolean)( v_loadReport && ( v_loadReport->integer || v_hitchLog->integer > 0 ) );
}

static void Com_LoadRecord( const char *kind, const char *name, int64_t usec ) {
	if ( load.loading ) {
		int i;

		// the kinds are literals: the same text is the same pointer nearly always, and the text settles the rest
		for ( i = 0; i < load.numKinds; i++ ) {
			if ( load.kinds[i].kind == kind || !strcmp( load.kinds[i].kind, kind ) ) {
				break;
			}
		}
		if ( i == load.numKinds ) {
			if ( load.numKinds == LOAD_MAX_KINDS ) {
				return;
			}
			load.kinds[load.numKinds].kind = kind;
			load.kinds[load.numKinds].count = 0;
			load.kinds[load.numKinds].usec = 0;
			load.numKinds++;
		}
		load.kinds[i].count++;
		load.kinds[i].usec += usec;
		return;
	}
	if ( load.numEvents == LOAD_MAX_EVENTS ) {
		load.moreEvents++;
		load.moreUsec += usec;
		return;
	}
	load.events[load.numEvents].kind = kind;
	Q_strncpyz( load.events[load.numEvents].name, name ? name : "", sizeof( load.events[0].name ) );
	load.events[load.numEvents].usec = (int)usec;
	load.numEvents++;
}

/*
===============
Com_LoadPush, Com_LoadPop

Around something that is loaded. kind is a literal ("model", "sound"), name what it is or NULL.
===============
*/
void Com_LoadPush( const char *kind, const char *name ) {
	if ( !Com_LoadWanted() && !load.depth ) {
		return;
	}
	if ( load.depth < LOAD_MAX_DEPTH ) {
		load.stack[load.depth].kind = kind;
		Q_strncpyz( load.stack[load.depth].name, name ? name : "", sizeof( load.stack[0].name ) );
		load.stack[load.depth].start = Com_LoadUsec();
		load.stack[load.depth].inside = 0;
	}
	load.depth++;
}

void Com_LoadPop( void ) {
	int64_t took;

	// a push that was left out because nobody wanted to know then
	if ( load.depth <= 0 ) {
		return;
	}
	load.depth--;
	if ( load.depth >= LOAD_MAX_DEPTH ) {
		return;
	}
	took = Com_LoadUsec() - load.stack[load.depth].start;
	if ( load.depth > 0 ) {
		load.stack[load.depth - 1].inside += took;
	}
	Com_LoadRecord( load.stack[load.depth].kind, load.stack[load.depth].name, took - load.stack[load.depth].inside );
}

static int QDECL Com_LoadCompareKinds( const void *a, const void *b ) {
	const int64_t d = ( (const loadKind_t *)b )->usec - ( (const loadKind_t *)a )->usec;

	return d > 0 ? 1 : d < 0 ? -1 : 0;
}

static int QDECL Com_LoadCompareEvents( const void *a, const void *b ) {
	return ( (const loadEvent_t *)b )->usec - ( (const loadEvent_t *)a )->usec;
}

static void Com_LoadPrintReport( const char *how ) {
	const int64_t total = Com_LoadUsec() - load.loadStart;
	int64_t named = 0;

	qsort( load.kinds, load.numKinds, sizeof( load.kinds[0] ), Com_LoadCompareKinds );
	Com_Printf( "load of %s%s: %.0f ms\n", load.what, how, total / 1000.0 );
	for ( int i = 0; i < load.numKinds; i++ ) {
		Com_Printf( "  %9.1f ms  %-26s %i\n", load.kinds[i].usec / 1000.0, load.kinds[i].kind, load.kinds[i].count );
		named += load.kinds[i].usec;
	}
	Com_Printf( "  %9.1f ms  not named\n", ( total - named ) / 1000.0 );
	load.loading = load.reportDue = qfalse;
	load.numKinds = 0;
}

/*
===============
Com_LoadBegin

A level begins to load. The server of a level and the client that plays on it in the same program are one
load: the second call goes on with the first.
===============
*/
void Com_LoadBegin( const char *what ) {
	if ( !v_loadReport || !v_loadReport->integer ) {
		return;
	}
	if ( load.loading ) {
		return;
	}
	load.loading = qtrue;
	load.reportDue = qfalse;
	load.numKinds = 0;
	load.numEvents = load.moreEvents = 0;
	Q_strncpyz( load.what, what && what[0] ? what : "a level", sizeof( load.what ) );
	load.loadStart = Com_LoadUsec();
}

// the load is over with the frame that is running: the first one of the game for a client, the one a level was
// started in for a server without one
void Com_LoadDone( void ) {
	if ( load.loading ) {
		load.reportDue = qtrue;
	}
}

// once a frame, before the work of the frame begins and after the wait for its time
void Com_LoadFrameBegin( void ) {
	// an error that ended a frame may have left something open
	load.depth = 0;
	load.numEvents = load.moreEvents = 0;
	load.moreUsec = 0;
	load.frameStart = Com_LoadUsec();
}

// once a frame, when its work is done
void Com_LoadFrameEnd( void ) {
	int64_t took, named = 0;

	if ( load.loading ) {
		if ( load.reportDue ) {
			Com_LoadPrintReport( "" );
		} else if ( Com_LoadUsec() - load.loadStart > (int64_t)LOAD_GIVE_UP * 1000000 ) {
			Com_LoadPrintReport( ", which did not get to a frame of the game" );
		}
		return;
	}
	if ( !v_hitchLog || v_hitchLog->integer <= 0 ) {
		return;
	}
	took = Com_LoadUsec() - load.frameStart;
	if ( took < (int64_t)v_hitchLog->integer * 1000 ) {
		return;
	}
	// what did not fit into the list is already added up
	named = load.moreUsec;
	qsort( load.events, load.numEvents, sizeof( load.events[0] ), Com_LoadCompareEvents );
	Com_Printf( "hitch: a frame of %.1f ms\n", took / 1000.0 );
	for ( int i = 0; i < load.numEvents; i++ ) {
		if ( i < LOAD_HITCH_LINES ) {
			Com_Printf( "  %7.2f ms  %-16s %s\n", load.events[i].usec / 1000.0, load.events[i].kind, load.events[i].name );
		} else {
			load.moreEvents++;
			load.moreUsec += load.events[i].usec;
		}
		named += load.events[i].usec;
	}
	if ( load.moreEvents ) {
		Com_Printf( "  %7.2f ms  %i more\n", load.moreUsec / 1000.0, load.moreEvents );
	}
	Com_Printf( "  %7.2f ms  not named\n", ( took - named ) / 1000.0 );
}
