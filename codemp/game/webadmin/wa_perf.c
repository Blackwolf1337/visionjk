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

// wa_perf.c -- /api/perf of the web admin panel: how long a frame of the game takes and how many
// it gets.

#include "game/webadmin/wa_local.h"

/*
==============================================================================

Performance

Not in JKAdmin. How long the game takes for a frame, how many frames it gets in
a second and how long the longest wait from one to the next was. One sample a
second, kept for five minutes. Only the game is measured (G_RunFrame), not the
engine around it: what the engine adds shows in the wait between two frames.
The numbers start anew with every map, the module is loaded again then.

==============================================================================
*/

#define WA_PERF_SAMPLES		300

typedef struct waPerfSample_s {
	int		frames;				// frames of the game in this second
	int		avgUs, maxUs;		// what a frame took, in microseconds
	int		gapMs;				// the longest time from the start of a frame to the start of the next
	int		entities, clients;
	int		panelAvgUs, panelMaxUs;	// what of a frame was this panel (WebAdmin_Frame)
} waPerfSample_t;

static waPerfSample_t	perfSamples[WA_PERF_SAMPLES];
static int				perfSeq;			// how many samples were written
static double			perfFrameStart, perfSecondStart, perfSum, perfMax, perfGap, perfPanelSum, perfPanelMax;
static int				perfFrames;
static qboolean			perfInFrame;

// A clock finer than trap->Milliseconds(): a frame of the game is done in less than a millisecond most of the
// time. The precision timer of the engine is of no use here, it only counts on Windows.
static double WA_Microseconds( void ) {
#ifdef _WIN32
	static LARGE_INTEGER	frequency;
	LARGE_INTEGER			now;

	if ( !frequency.QuadPart )
		QueryPerformanceFrequency( &frequency );
	QueryPerformanceCounter( &now );
	return (double)now.QuadPart * 1000000.0 / (double)frequency.QuadPart;
#else
	struct timespec now;

	clock_gettime( CLOCK_MONOTONIC, &now );
	return (double)now.tv_sec * 1000000.0 + (double)now.tv_nsec / 1000.0;
#endif
}

void WA_PerfFrameBegin( void ) {
	const double now = WA_Microseconds();

	if ( perfFrameStart > 0 && now - perfFrameStart > perfGap )
		perfGap = now - perfFrameStart;
	perfFrameStart = now;
	perfInFrame = qtrue;

	if ( perfSecondStart <= 0 ) {
		perfSecondStart = now;
	} else if ( now - perfSecondStart >= 1000000.0 ) {
		waPerfSample_t *sample = &perfSamples[perfSeq % WA_PERF_SAMPLES];

		sample->frames = perfFrames;
		sample->avgUs = perfFrames ? (int)( perfSum / perfFrames ) : 0;
		sample->maxUs = (int)perfMax;
		sample->gapMs = (int)( perfGap / 1000.0 + 0.5 );
		sample->entities = ENTITYNUM_MAX_NORMAL - MAX_CLIENTS - WA_FreeEntities();
		sample->clients = level.numConnectedClients;
		sample->panelAvgUs = perfFrames ? (int)( perfPanelSum / perfFrames ) : 0;
		sample->panelMaxUs = (int)perfPanelMax;
		perfSeq++;

		perfFrames = 0;
		perfSum = perfMax = perfGap = perfPanelSum = perfPanelMax = 0;
		perfSecondStart = now;
	}
}

// the part of the panel in a frame is over: the rest of it is the game
void WA_PerfPanelDone( void ) {
	const double took = WA_Microseconds() - perfFrameStart;

	perfPanelSum += took;
	if ( took > perfPanelMax )
		perfPanelMax = took;
}

void WebAdmin_FrameEnd( void ) {
	double took;

	if ( !perfInFrame )
		return;
	perfInFrame = qfalse;

	took = WA_Microseconds() - perfFrameStart;
	perfSum += took;
	if ( took > perfMax )
		perfMax = took;
	perfFrames++;
}

// since=<n>: only the samples from number n on, for a page that has the ones before. from is the number of the
// first one that is sent: another one than asked for when those are gone, or when the map is a new one.
void WA_ApiPerf( waConnection_t *c, const char *query ) {
	waBuffer_t	b = { 0 };
	char		value[32];
	int			i, since = 0;

	if ( WA_FormValue( query, "since", value, sizeof( value ) ) )
		since = atoi( value );
	if ( since < perfSeq - WA_PERF_SAMPLES || since > perfSeq )
		since = perfSeq - WA_PERF_SAMPLES;
	if ( since < 0 )
		since = 0;

	WA_BufPrintf( &b, "{\"from\":%d,\"seq\":%d,\"keep\":%d,\"svFps\":%d,\"entityMax\":%d,\"maxClients\":%d,\"samples\":[",
		since, perfSeq, WA_PERF_SAMPLES, trap->Cvar_VariableIntegerValue( "sv_fps" ),
		ENTITYNUM_MAX_NORMAL - MAX_CLIENTS - WA_ENTITY_RESERVE, level.maxclients );
	for ( i = since; i < perfSeq; i++ ) {
		const waPerfSample_t *sample = &perfSamples[i % WA_PERF_SAMPLES];

		WA_BufPrintf( &b, "%s[%d,%d,%d,%d,%d,%d,%d,%d]", i > since ? "," : "", sample->frames, sample->avgUs, sample->maxUs,
			sample->gapMs, sample->entities, sample->clients, sample->panelAvgUs, sample->panelMaxUs );
	}
	WA_BufAppend( &b, "]}", 2 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

