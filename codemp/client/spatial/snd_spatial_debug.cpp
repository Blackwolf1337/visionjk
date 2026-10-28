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

// snd_spatial_debug.cpp -- what v_spatialInfo, v_spatialCapture and v_spatialDebug show of the sound of the
// level. See snd_spatial.cpp.

#include "snd_spatial_local.h"

#ifdef JP_SPATIAL

/*
===============
S_SpatialCapture

v_spatialCapture <seconds>: what the voices and the reverb give the mixer, as it is heard, into the file
steamaudio/capture.wav of the home path. For measuring without ears where SDL has no sound driver that writes
a file (a page) and with the same code where it has one, see docs/japlus/audio.md. What the mixer plays itself
is not in it: the sounds of the own player, the menus, the music.

What is behind the first sample the mixer paints in a frame is not painted again and no voice adds to it any
more: that is what was heard, and it is still in the ring unless the game stood still for a third of a second.
===============
*/
#define SPATIAL_WAV_HEADER	44		// bytes

static void S_SpatialCapturePut( byte *out, unsigned value, int bytes ) {
	for ( int i = 0; i < bytes; i++ ) {
		out[i] = ( value >> ( i * 8 ) ) & 0xff;		// little endian, as the format wants it
	}
}

void S_SpatialCaptureEnd( qboolean write ) {
	if ( spatial.capture && write ) {
		byte *header = (byte *)spatial.capture;
		const int dataBytes = spatial.captureWritten * 2 * (int)sizeof( short );

		memcpy( header, "RIFF", 4 );
		S_SpatialCapturePut( header + 4, SPATIAL_WAV_HEADER - 8 + dataBytes, 4 );
		memcpy( header + 8, "WAVEfmt ", 8 );
		S_SpatialCapturePut( header + 16, 16, 4 );
		S_SpatialCapturePut( header + 20, 1, 2 );		// PCM
		S_SpatialCapturePut( header + 22, 2, 2 );		// channels
		S_SpatialCapturePut( header + 24, spatial.audio.samplingRate, 4 );
		S_SpatialCapturePut( header + 28, spatial.audio.samplingRate * 2 * (int)sizeof( short ), 4 );
		S_SpatialCapturePut( header + 32, 2 * (int)sizeof( short ), 2 );
		S_SpatialCapturePut( header + 34, 16, 2 );
		memcpy( header + 36, "data", 4 );
		S_SpatialCapturePut( header + 40, dataBytes, 4 );
		FS_WriteFile( "steamaudio/capture.wav", spatial.capture, SPATIAL_WAV_HEADER + dataBytes );
		Com_Printf( "v_spatialCapture: %.1f s in steamaudio/capture.wav\n", (float)spatial.captureWritten / spatial.audio.samplingRate );
	}
	if ( spatial.capture ) {
		Z_Free( spatial.capture );
	}
	spatial.capture = NULL;
}

// from: the first sample the mixer paints in this frame
void S_SpatialCapture( int from ) {
	short *out;

	if ( !spatial.capture ) {
		return;
	}
	// the first frame, or the time of the mixer went back
	if ( spatial.captureFrom < 0 || from < spatial.captureFrom ) {
		spatial.captureFrom = from;
	}
	out = spatial.capture + SPATIAL_WAV_HEADER / (int)sizeof( short ) + spatial.captureWritten * 2;
	for ( int t = spatial.captureFrom; t < from && spatial.captureWritten < spatial.captureSamples; t++, spatial.captureWritten++, out += 2 ) {
		const float *slot = spatial.ring[t & ( SPATIAL_RING - 1 )];

		// The ring counts in 16 bit samples times 256, as the paint buffer does. What it no longer has, or
		// never had, is silence.
		if ( t >= spatial.ringEnd || t < spatial.ringEnd - SPATIAL_RING ) {
			out[0] = out[1] = 0;
		} else {
			out[0] = (short)Com_Clampi( -32768, 32767, (int)( slot[0] * ( 1.0f / 256.0f ) ) );
			out[1] = (short)Com_Clampi( -32768, 32767, (int)( slot[1] * ( 1.0f / 256.0f ) ) );
		}
	}
	spatial.captureFrom = from;
	if ( spatial.captureWritten >= spatial.captureSamples ) {
		S_SpatialCaptureEnd( qtrue );
	}
}

void S_SpatialCapture_f( void ) {
	const float seconds = Com_Clamp( 1.0f, 120.0f, atof( Cmd_Argv( 1 ) ) );

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "usage: v_spatialCapture <seconds>, 1 to 120. Writes what the sounds of the level give the mixer to steamaudio/capture.wav\n" );
		return;
	}
	if ( !spatial.active ) {
		Com_Printf( "v_spatialCapture: v_spatial does not run\n" );
		return;
	}
	S_SpatialCaptureEnd( qfalse );
	spatial.captureSamples = (int)( seconds * spatial.audio.samplingRate );
	spatial.captureWritten = 0;
	spatial.captureFrom = -1;
	// as long as it was asked for, so the size is not known before
	spatial.capture = (short *)Z_Malloc( SPATIAL_WAV_HEADER + spatial.captureSamples * 2 * (int)sizeof( short ), TAG_TEMP_WORKSPACE, qfalse );
}

void S_SpatialInfo_f( void ) {
	if ( !v_spatial->integer ) {
		Com_Printf( "v_spatial is 0 (set it to 1, then snd_restart)\n" );
		return;
	}
	if ( !spatial.active ) {
		Com_Printf( "v_spatial is 1, but Steam Audio did not start, see the lines of the sound initialization\n" );
		return;
	}
	Com_Printf( "Steam Audio %i.%i.%i from %s\n", STEAMAUDIO_VERSION_MAJOR, STEAMAUDIO_VERSION_MINOR, STEAMAUDIO_VERSION_PATCH,
		spatial.libraryPath );
	Com_Printf( "occlusion, paths and bakes traced by %s, %i threads for a pass, %i for a bake\n", spatial.embree ? "Embree" : "Steam Audio", spatial.threads,
		spatial.bakeThreads );
	if ( spatial.gpuSimulator && S_SpatialGpuName() ) {
		Com_Printf( "the rays of the echo %s by %s (v_spatialGpu %i)\n", spatial.reflectGpu ? "are traced" : "can be traced",
			S_SpatialGpuName(), v_spatialGpu->integer );
		if ( spatial.reflectGpu ) {
			int batches, usec, answered;

			S_SpatialGpuBatchInfo( &batches, &usec, &answered );
			Com_Printf( "  the last pass made %i trips to the card, %i us each, and %i of the sources were answered without one\n", batches, usec, answered );
		}
	} else {
		Com_Printf( "the rays of the echo are traced by the CPU: %s\n", S_SpatialGpuWhyNot() );
	}
	Com_Printf( "%i samples a second, blocks of %i, direction for %s\n", spatial.audio.samplingRate, spatial.audio.frameSize,
		v_spatialHrtf->integer ? "headphones (HRTF)" : "speakers (panning)" );
	if ( spatial.sceneWaits ) {
		Com_Printf( "a level waits to be taken over\n" );
		return;
	}
	if ( !spatial.numTriangles ) {
		Com_Printf( "no level\n" );
		return;
	}
	Com_Printf( "level: %i triangles, built in %i ms\n", spatial.numTriangles, spatial.sceneMsec );
	{
		int brushes, added;

		S_SpatialCountBrushes( &brushes, &added );
		Com_Printf( "brush models (doors, lifts ...): %i with %i triangles, %i of them in the scene now\n", brushes,
			spatial.level.brushTriangles, added );
	}
	for ( int i = 0; i < SPATIAL_NUM_MATERIALS; i++ ) {
		if ( spatial.materialTriangles[i] ) {
			Com_Printf( "  %-9s %i\n", spatialMaterialNames[i], spatial.materialTriangles[i] );
		}
	}
	Com_Printf( "  %i of them by " SPATIAL_TABLE_FILE " (%i shaders), the others by what the level says\n", spatial.tableTriangles,
		spatialTableCount );
	Com_Printf( "%i sounds in the level, rendered %i ms ahead. Last frame: %i us for what is in the way, %i us to render (most so far: %i us)\n",
		spatial.numVoices, (int)( S_SpatialAhead() * 1000.0f ), spatial.simulateUsec, spatial.renderUsec, spatial.peakRenderUsec );
	spatial.peakRenderUsec = 0;
	Com_Printf( "fade with the distance: %s, doppler %s (s_doppler)\n",
		v_spatialPhysical->integer ? "of a real source, with the air and the speed of sound (v_spatialPhysical 1)" : "the game's (v_spatialPhysical 0)",
		s_doppler->integer ? "on" : "off" );
	// What it took of the thread of the game since this was last asked. A browser counts time in steps of 0.1 ms
	// or more: the averages are of many frames and hold, the one frame that took most is that coarse.
	if ( spatial.costFrames ) {
		const float perFrame = 1.0f / spatial.costFrames;

		Com_Printf( "%i frames since this was last asked, %.1f sounds in each (most: %i). Of the thread of the game a frame took %.0f us (most: %i us):"
			" %.0f us what is in the way, %.0f us the sounds, %.0f us the reverb",
			spatial.costFrames, spatial.costVoices * perFrame, spatial.costPeakVoices,
			( spatial.costUsec[COST_SIMULATE] + spatial.costUsec[COST_VOICES] + spatial.costUsec[COST_REVERB] + spatial.costUsec[COST_PASS] ) * perFrame,
			spatial.costPeakUsec, spatial.costUsec[COST_SIMULATE] * perFrame, spatial.costUsec[COST_VOICES] * perFrame,
			spatial.costUsec[COST_REVERB] * perFrame );
#ifdef __EMSCRIPTEN__
		Com_Printf( ", %.0f us the passes", spatial.costUsec[COST_PASS] * perFrame );
#endif
		Com_Printf( "\n" );
	}
	spatial.costFrames = spatial.costPeakUsec = spatial.costVoices = spatial.costPeakVoices = 0;
	memset( spatial.costUsec, 0, sizeof( spatial.costUsec ) );
	if ( v_spatialReflections->integer ) {
		Com_Printf( "reflections: %i rays, %i bounces, the last pass took %i ms, %i passes so far, %i sounds with their own (v_spatialSources %i)\n",
			S_SpatialRays(), S_SpatialBounces(), spatialReflectUsec.load() / 1000, spatialReflectPasses.load(),
			spatial.numReflects, Com_Clampi( 0, spatial.numReflectSlots, v_spatialSources->integer ) );
#ifdef __EMSCRIPTEN__
		Com_Printf( "  done by the game itself, with %.2f ms of each frame (v_spatialBudget): the last pass in %i slices over %i ms, the longest %.1f ms\n",
			S_SpatialBudget() * 0.001f, spatial.lastPassSlices, spatial.lastPassMsec, spatial.lastPassPeakUsec * 0.001f );
#endif
	} else {
		Com_Printf( "reflections: off (v_spatialReflections 0)\n" );
	}
	if ( !v_spatialPaths->integer ) {
		Com_Printf( "paths: off (v_spatialPaths 0)\n" );
	} else if ( spatialBakeState.load() == BAKE_RUNNING && spatialBake.kind == BAKE_PATHS && spatialBake.sceneNumber == spatial.sceneNumber ) {
		Com_Printf( "paths: being worked out for this level, %i %%\n", spatialBakeProgress.load() );
	} else if ( spatial.paths.batch ) {
		if ( spatial.paths.bakeMsec ) {
			Com_Printf( "paths: %i probes %.1f m apart, worked out in %.1f s, kept in %s\n", spatial.paths.numProbes, spatial.paths.spacing,
				spatial.paths.bakeMsec * 0.001f, spatial.paths.file );
		} else {
			Com_Printf( "paths: %i probes from %s\n", spatial.paths.numProbes, spatial.paths.file );
		}
	} else if ( spatial.refusedProbes ) {
		Com_Printf( "paths: none, the level has %i probes and a page works them out for up to %i\n", spatial.refusedProbes, SPATIAL_PAGE_PROBES );
	} else {
		Com_Printf( "paths: none yet\n" );
	}
	if ( !v_spatialEcho->integer ) {
		Com_Printf( "echo: worked out as you go (v_spatialEcho 0)\n" );
	} else if ( S_SpatialEchoBaked() ) {
		if ( spatial.echo.bakeMsec ) {
			Com_Printf( "echo: looked up at %i probes, worked out in %.1f s, kept in %s\n", spatial.echo.numProbes, spatial.echo.bakeMsec * 0.001f, spatial.echo.file );
		} else {
			Com_Printf( "echo: looked up at %i probes from %s\n", spatial.echo.numProbes, spatial.echo.file );
		}
	} else if ( spatialBakeState.load() == BAKE_RUNNING && spatialBake.kind == BAKE_ECHO && spatialBake.sceneNumber == spatial.sceneNumber ) {
		Com_Printf( "echo: being worked out for this level, %i %%, until then as you go\n", spatialBakeProgress.load() );
	} else {
		Com_Printf( "echo: none for this level yet, worked out as you go\n" );
	}
	Com_Printf( "listener at %.0f %.0f %.0f\n", spatial.listenerOrigin[0], spatial.listenerOrigin[1], spatial.listenerOrigin[2] );
	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		const spatialVoice_t *v = &spatial.voices[i];

		if ( !v->active || v->frame != spatial.frame ) {
			continue;
		}
		if ( v->sendOnly ) {
			Com_Printf( "  %-40s own, reverb only, gain %.2f\n", v->stream ? "voice chat" : v->sfx->sSoundName, v->gain );
			continue;
		}
		Com_Printf( "  %-40s at %.0f %.0f %.0f, %5.0f units away, gain %.2f, doppler %.3f, %.0f ms late, %3.0f %% in sight, through what hides it: %.3f %.3f %.3f%s%s%s\n",
			v->stream ? va( "voice of player %i", (int)( v->stream - spatialStreams ) ) : v->sfx->sSoundName,
			v->origin[0], v->origin[1], v->origin[2], v->distance, v->gain, v->rate, v->delay * 1000.0f / spatial.audio.samplingRate,
			v->directParams.occlusion * 100.0f,
			v->directParams.transmission[0], v->directParams.transmission[1], v->directParams.transmission[2],
			( v->shock && v_spatialShock->value > 0.0f ) ? ", thump" : "",
			v->reflect ? ( S_SpatialReflectReady( v->reflect ) ? ", own reflections" : ", own reflections soon" ) : "",
			v->outOfBrush ? va( ", from the side of its brush model at %.0f %.0f %.0f", v->simOrigin[0], v->simOrigin[1], v->simOrigin[2] ) : "" );
	}
}

/*
===============================================================================

DEBUG

v_spatialDebug: what becomes of the sounds, for the cgame to draw (ext.S_SpatialDebug, cgame/cg_spatialdebug.c).
Steam Audio does not show the rays it traces: the rays here are traced through a copy of the mesh it was given,
and bounce off the walls as off a mirror, where Steam Audio scatters some of them. They show which walls a sound
can reach and what they were made of.

===============================================================================
*/

#define SPATIAL_DEBUG_CELLS			64		// cells of the grid of the copy along its longest side
#define SPATIAL_DEBUG_LEAST_CELL	64.0f	//	but none smaller than this, units
#define SPATIAL_DEBUG_MOST_ENTRIES	( 8 * 1024 * 1024 )	// triangles in cells, more and the copy is not made
#define SPATIAL_DEBUG_RAY_COUNT		48		// rays from the listener (SPATIAL_DEBUG_REFLECTIONS)
#define SPATIAL_DEBUG_RAY_BOUNCES	4		//	and how often each bounces at most
#define SPATIAL_DEBUG_RAY_LENGTH	8192.0f
#define SPATIAL_DEBUG_RAY_MSEC		250		// they are traced again this often, or when the listener moved
#define SPATIAL_DEBUG_LEVEL_SMOOTH	0.25f	// how fast the levels follow, each time they are asked

typedef struct {
	vec3_t		v0, e1, e2, normal;
	byte		material, byTable;
	const char	*shader;			// the name the collision model has, there as long as the level
} spatialDebugTri_t;

static struct {
	IPLScene			scene;		// of which level the copy is, NULL: none
	qboolean			failed;		//	it could not be made for it
	spatialDebugTri_t	*tris;
	int					numTris;
	spatialShaderCache_t	cache;	// while it is made
	vec3_t				mins;
	float				cell;
	int					dims[3];
	int					*cellStart;	// where the triangles of a cell begin in cellTris, one more than cells
	int					*cellTris;
	int					worldTris;	// the first of tris are the world's, in the grid
	int					numModels;	// then those of the brush models, in their own place:
	int					*brushFirst, *brushCount;	//	where they begin in tris and how many, by brush model
	int					raysTime;	// Sys_Milliseconds() the rays were traced
	vec3_t				raysFrom;
	int					numRays;
	spatialDebugSegment_t	rays[SPATIAL_DEBUG_RAYS];
} spatialDebugMesh;

void S_SpatialDebugFree( void ) {
	if ( spatialDebugMesh.tris ) {
		Z_Free( spatialDebugMesh.tris );
	}
	if ( spatialDebugMesh.cellStart ) {
		Z_Free( spatialDebugMesh.cellStart );
	}
	if ( spatialDebugMesh.cellTris ) {
		Z_Free( spatialDebugMesh.cellTris );
	}
	if ( spatialDebugMesh.brushFirst ) {
		Z_Free( spatialDebugMesh.brushFirst );
	}
	if ( spatialDebugMesh.brushCount ) {
		Z_Free( spatialDebugMesh.brushCount );
	}
	memset( &spatialDebugMesh, 0, sizeof( spatialDebugMesh ) );
}

static void S_SpatialDebugTriangle( const vec3_t a, const vec3_t b, const vec3_t c, const char *shader, int surfaceFlags, void *data ) {
	spatialDebugTri_t *tri = &spatialDebugMesh.tris[spatialDebugMesh.numTris++];
	qboolean byTable;

	tri->material = (byte)S_SpatialTriangleMaterial( &spatialDebugMesh.cache, shader, surfaceFlags, &byTable );
	tri->byTable = byTable ? 1 : 0;
	VectorCopy( a, tri->v0 );
	VectorSubtract( b, a, tri->e1 );
	VectorSubtract( c, a, tri->e2 );
	CrossProduct( tri->e1, tri->e2, tri->normal );
	VectorNormalize( tri->normal );
	tri->shader = shader;
}

// the cells a box of the level touches
static void S_SpatialDebugCells( const vec3_t mins, const vec3_t maxs, int lo[3], int hi[3] ) {
	for ( int k = 0; k < 3; k++ ) {
		lo[k] = Com_Clampi( 0, spatialDebugMesh.dims[k] - 1, (int)floorf( ( mins[k] - spatialDebugMesh.mins[k] ) / spatialDebugMesh.cell ) );
		hi[k] = Com_Clampi( 0, spatialDebugMesh.dims[k] - 1, (int)floorf( ( maxs[k] - spatialDebugMesh.mins[k] ) / spatialDebugMesh.cell ) );
	}
}

static void S_SpatialDebugBounds( const spatialDebugTri_t *tri, vec3_t mins, vec3_t maxs ) {
	for ( int k = 0; k < 3; k++ ) {
		const float b = tri->v0[k] + tri->e1[k], c = tri->v0[k] + tri->e2[k];

		mins[k] = Q_min( tri->v0[k], Q_min( b, c ) );
		maxs[k] = Q_max( tri->v0[k], Q_max( b, c ) );
	}
}

// The copy of the walls of the level Steam Audio has, in a grid, made when it is first wanted for this level
static qboolean S_SpatialDebugMesh( void ) {
	vec3_t mins, maxs, size;
	int count, cells, entries;

	if ( spatialDebugMesh.scene == spatial.level.scene && ( spatialDebugMesh.tris || spatialDebugMesh.failed ) ) {
		return (qboolean)!spatialDebugMesh.failed;
	}
	S_SpatialDebugFree();
	spatialDebugMesh.scene = spatial.level.scene;
	count = CM_ExportSolidTriangles( NULL, NULL );
	if ( !count ) {
		spatialDebugMesh.failed = qtrue;
		return qfalse;
	}
	spatialDebugMesh.numModels = CM_ExportModelCount();
	for ( int m = 1; m < spatialDebugMesh.numModels; m++ ) {
		count += CM_ExportModelTriangles( m, NULL, NULL );
	}
	spatialDebugMesh.tris = (spatialDebugTri_t *)Z_Malloc( count * sizeof( spatialDebugTri_t ), TAG_GENERAL, qfalse );
	spatialDebugMesh.brushFirst = (int *)Z_Malloc( Q_max( 1, spatialDebugMesh.numModels ) * sizeof( int ), TAG_GENERAL, qtrue );
	spatialDebugMesh.brushCount = (int *)Z_Malloc( Q_max( 1, spatialDebugMesh.numModels ) * sizeof( int ), TAG_GENERAL, qtrue );
	CM_ExportSolidTriangles( S_SpatialDebugTriangle, NULL );
	spatialDebugMesh.worldTris = spatialDebugMesh.numTris;
	for ( int m = 1; m < spatialDebugMesh.numModels; m++ ) {
		spatialDebugMesh.brushFirst[m] = spatialDebugMesh.numTris;
		CM_ExportModelTriangles( m, S_SpatialDebugTriangle, NULL );
		spatialDebugMesh.brushCount[m] = spatialDebugMesh.numTris - spatialDebugMesh.brushFirst[m];
	}

	ClearBounds( mins, maxs );
	for ( int i = 0; i < spatialDebugMesh.worldTris; i++ ) {
		vec3_t a, b;

		S_SpatialDebugBounds( &spatialDebugMesh.tris[i], a, b );
		AddPointToBounds( a, mins, maxs );
		AddPointToBounds( b, mins, maxs );
	}
	VectorSubtract( maxs, mins, size );
	spatialDebugMesh.cell = Q_max( SPATIAL_DEBUG_LEAST_CELL, Q_max( size[0], Q_max( size[1], size[2] ) ) / SPATIAL_DEBUG_CELLS );
	VectorCopy( mins, spatialDebugMesh.mins );
	cells = 1;
	for ( int k = 0; k < 3; k++ ) {
		spatialDebugMesh.dims[k] = (int)( size[k] / spatialDebugMesh.cell ) + 1;
		cells *= spatialDebugMesh.dims[k];
	}

	// how many triangles each cell has, where they begin, then the triangles
	spatialDebugMesh.cellStart = (int *)Z_Malloc( ( cells + 1 ) * sizeof( int ), TAG_GENERAL, qtrue );
	entries = 0;
	for ( int pass = 0; pass < 2; pass++ ) {
		if ( pass ) {
			if ( entries > SPATIAL_DEBUG_MOST_ENTRIES ) {
				Com_Printf( "v_spatialDebug: the level is too large for the copy of its walls\n" );
				S_SpatialDebugFree();
				spatialDebugMesh.scene = spatial.level.scene;
				spatialDebugMesh.failed = qtrue;
				return qfalse;
			}
			for ( int c = 0, at = 0; c <= cells; c++ ) {
				const int n = spatialDebugMesh.cellStart[c];

				spatialDebugMesh.cellStart[c] = at;
				at += n;
			}
			spatialDebugMesh.cellTris = (int *)Z_Malloc( Q_max( 1, entries ) * sizeof( int ), TAG_GENERAL, qfalse );
		}
		for ( int i = 0; i < spatialDebugMesh.worldTris; i++ ) {
			vec3_t a, b;
			int lo[3], hi[3];

			S_SpatialDebugBounds( &spatialDebugMesh.tris[i], a, b );
			S_SpatialDebugCells( a, b, lo, hi );
			for ( int z = lo[2]; z <= hi[2]; z++ ) {
				for ( int y = lo[1]; y <= hi[1]; y++ ) {
					for ( int x = lo[0]; x <= hi[0]; x++ ) {
						const int c = ( z * spatialDebugMesh.dims[1] + y ) * spatialDebugMesh.dims[0] + x;

						if ( pass ) {
							spatialDebugMesh.cellTris[spatialDebugMesh.cellStart[c]++] = i;
						} else {
							spatialDebugMesh.cellStart[c]++;
							entries++;
						}
					}
				}
			}
		}
	}
	// the second pass moved the start of each cell on to its end, which is where the next one begins
	for ( int c = cells; c > 0; c-- ) {
		spatialDebugMesh.cellStart[c] = spatialDebugMesh.cellStart[c - 1];
	}
	spatialDebugMesh.cellStart[0] = 0;
	return qtrue;
}

// how far along dir (of length 1) a triangle is, -1 if the ray misses it
static float S_SpatialDebugHit( const spatialDebugTri_t *tri, const vec3_t start, const vec3_t dir ) {
	vec3_t p, t, q;
	float det, inv, u, v;

	CrossProduct( dir, tri->e2, p );
	det = DotProduct( tri->e1, p );
	if ( fabsf( det ) < 1e-8f ) {
		return -1.0f;
	}
	inv = 1.0f / det;
	VectorSubtract( start, tri->v0, t );
	u = DotProduct( t, p ) * inv;
	if ( u < 0.0f || u > 1.0f ) {
		return -1.0f;
	}
	CrossProduct( t, tri->e1, q );
	v = DotProduct( dir, q ) * inv;
	if ( v < 0.0f || u + v > 1.0f ) {
		return -1.0f;
	}
	return DotProduct( tri->e2, q ) * inv;
}

// The first wall of the copy along dir (of length 1) from start, through the grid cell by cell. -1: none
static int S_SpatialDebugTrace( const vec3_t start, const vec3_t dir, float length, float *hitT ) {
	const float cell = spatialDebugMesh.cell;
	float enter = 0.0f, leave = length, tMax[3], tDelta[3];
	int idx[3], step[3];

	// into the box of the grid
	for ( int k = 0; k < 3; k++ ) {
		const float lo = spatialDebugMesh.mins[k], hi = lo + spatialDebugMesh.dims[k] * cell;

		if ( fabsf( dir[k] ) < 1e-8f ) {
			if ( start[k] < lo || start[k] > hi ) {
				return -1;
			}
			continue;
		}
		float t0 = ( lo - start[k] ) / dir[k], t1 = ( hi - start[k] ) / dir[k];

		if ( t0 > t1 ) {
			const float swap = t0;

			t0 = t1;
			t1 = swap;
		}
		enter = Q_max( enter, t0 );
		leave = Q_min( leave, t1 );
	}
	if ( enter > leave ) {
		return -1;
	}
	for ( int k = 0; k < 3; k++ ) {
		const float at = start[k] + dir[k] * enter;

		idx[k] = Com_Clampi( 0, spatialDebugMesh.dims[k] - 1, (int)floorf( ( at - spatialDebugMesh.mins[k] ) / cell ) );
		if ( dir[k] > 1e-8f ) {
			step[k] = 1;
			tMax[k] = ( spatialDebugMesh.mins[k] + ( idx[k] + 1 ) * cell - start[k] ) / dir[k];
			tDelta[k] = cell / dir[k];
		} else if ( dir[k] < -1e-8f ) {
			step[k] = -1;
			tMax[k] = ( spatialDebugMesh.mins[k] + idx[k] * cell - start[k] ) / dir[k];
			tDelta[k] = -cell / dir[k];
		} else {
			step[k] = 0;
			tMax[k] = tDelta[k] = 1e30f;
		}
	}
	for ( ;; ) {
		const int c = ( idx[2] * spatialDebugMesh.dims[1] + idx[1] ) * spatialDebugMesh.dims[0] + idx[0];
		const float cellLeave = Q_min( tMax[0], Q_min( tMax[1], tMax[2] ) );
		float best = 1e30f;
		int bestTri = -1;

		for ( int n = spatialDebugMesh.cellStart[c]; n < spatialDebugMesh.cellStart[c + 1]; n++ ) {
			const int i = spatialDebugMesh.cellTris[n];
			const float t = S_SpatialDebugHit( &spatialDebugMesh.tris[i], start, dir );

			if ( t > 0.05f && t < best ) {
				best = t;
				bestTri = i;
			}
		}
		// a triangle of this cell may be hit beyond it, in a cell that comes later
		if ( bestTri >= 0 && best <= cellLeave + 0.01f && best <= length ) {
			*hitT = best;
			return bestTri;
		}
		if ( cellLeave > leave ) {
			return -1;
		}
		const int k = ( tMax[0] < tMax[1] ) ? ( tMax[0] < tMax[2] ? 0 : 2 ) : ( tMax[1] < tMax[2] ? 1 : 2 );

		idx[k] += step[k];
		if ( idx[k] < 0 || idx[k] >= spatialDebugMesh.dims[k] ) {
			return -1;
		}
		tMax[k] += tDelta[k];
	}
}

// The first wall along dir from start, of the world or of a brush model where the scene has it now. normal: its
// normal in the level. NULL: none.
static const spatialDebugTri_t *S_SpatialDebugTraceAll( const vec3_t start, const vec3_t dir, float length, float *hitT, vec3_t normal ) {
	const spatialLevel_t *level = &spatial.level;
	const spatialDebugTri_t *hit = NULL;
	float best = length, t;
	const int tri = S_SpatialDebugTrace( start, dir, length, &t );

	if ( tri >= 0 ) {
		best = t;
		hit = &spatialDebugMesh.tris[tri];
		VectorCopy( hit->normal, normal );
	}
	for ( int m = 1; m < spatialDebugMesh.numModels && m < level->numBrushes; m++ ) {
		const spatialBrush_t *b = &level->brushes[m];
		matrix3_t axis;
		vec3_t d, localStart, localDir;

		if ( !b->added || !spatialDebugMesh.brushCount[m] ) {
			continue;
		}
		// into the place the map has it in: local = R^T ( world - origin )
		AnglesToAxis( b->angles, axis );
		VectorSubtract( start, b->origin, d );
		for ( int k = 0; k < 3; k++ ) {
			localStart[k] = DotProduct( d, axis[k] );
			localDir[k] = DotProduct( dir, axis[k] );
		}
		for ( int i = 0; i < spatialDebugMesh.brushCount[m]; i++ ) {
			const spatialDebugTri_t *candidate = &spatialDebugMesh.tris[spatialDebugMesh.brushFirst[m] + i];

			t = S_SpatialDebugHit( candidate, localStart, localDir );
			if ( t > 0.05f && t < best ) {
				best = t;
				hit = candidate;
				VectorScale( axis[0], candidate->normal[0], normal );
				VectorMA( normal, candidate->normal[1], axis[1], normal );
				VectorMA( normal, candidate->normal[2], axis[2], normal );
			}
		}
	}
	*hitT = best;
	return hit;
}

// Rays from the listener, spread evenly round him, off the walls as off a mirror
static void S_SpatialDebugRays( void ) {
	const int now = Sys_Milliseconds();

	if ( now - spatialDebugMesh.raysTime < SPATIAL_DEBUG_RAY_MSEC && spatialDebugMesh.raysTime
		&& DistanceSquared( spatialDebugMesh.raysFrom, spatial.listenerOrigin ) < 1.0f ) {
		return;
	}
	spatialDebugMesh.raysTime = now;
	VectorCopy( spatial.listenerOrigin, spatialDebugMesh.raysFrom );
	spatialDebugMesh.numRays = 0;
	const int bounces = Com_Clampi( 1, SPATIAL_DEBUG_RAY_BOUNCES, v_spatialBounces->integer );

	for ( int r = 0; r < SPATIAL_DEBUG_RAY_COUNT; r++ ) {
		// a spiral over the sphere (Fibonacci): even, and the same every time
		const float z = 1.0f - 2.0f * ( r + 0.5f ) / SPATIAL_DEBUG_RAY_COUNT, ring = sqrtf( 1.0f - z * z );
		const float a = r * 2.39996323f;
		vec3_t from, dir;

		VectorCopy( spatial.listenerOrigin, from );
		VectorSet( dir, ring * cosf( a ), ring * sinf( a ), z );
		for ( int b = 0; b < bounces && spatialDebugMesh.numRays < SPATIAL_DEBUG_RAYS; b++ ) {
			spatialDebugSegment_t *seg = &spatialDebugMesh.rays[spatialDebugMesh.numRays++];
			float t;
			vec3_t n;
			const spatialDebugTri_t *tri = S_SpatialDebugTraceAll( from, dir, SPATIAL_DEBUG_RAY_LENGTH, &t, n );

			VectorCopy( from, seg->from );
			seg->bounce = b;
			if ( !tri ) {
				VectorMA( from, SPATIAL_DEBUG_RAY_LENGTH, dir, seg->to );
				seg->kind = -1;		// out of the level
				break;
			}
			VectorMA( from, t, dir, seg->to );
			seg->kind = tri->material;
			if ( seg->kind == SPATIAL_SKY ) {
				break;
			}
			// off the wall as off a mirror, a little away from it
			const float d = DotProduct( dir, n );

			VectorMA( dir, -2.0f * d, n, dir );
			VectorNormalize( dir );
			VectorMA( seg->to, 0.5f, dir, from );
		}
	}
}

static float S_SpatialDebugLevel( double energy, int samples, float last ) {
	const double full = 32768.0 * 256.0;
	float db;

	if ( samples <= 0 ) {
		return last;
	}
	db = ( energy > 0.0 ) ? (float)( 10.0 * log10( energy / ( 2.0 * samples ) / ( full * full ) ) ) : -96.0f;
	db = Q_max( -96.0f, db );
	return last + ( db - last ) * SPATIAL_DEBUG_LEVEL_SMOOTH;
}

// A sound no voice was free for, which the mixer plays itself (snd_dma.cpp)
void S_SpatialDebugFallback( const vec3_t origin, const sfx_t *sfx ) {
	spatialDebugFallback_t *f;

	if ( !v_spatialDebug || !v_spatialDebug->integer || spatial.numFallbacks >= SPATIAL_DEBUG_FALLBACKS ) {
		return;
	}
	f = &spatial.fallbacks[spatial.numFallbacks++];
	VectorCopy( origin, f->origin );
	Q_strncpyz( f->name, sfx ? sfx->sSoundName : "?", sizeof( f->name ) );
}

/*
===============
S_SpatialDebug

From the cgame (ext.S_SpatialDebug), once a frame while v_spatialDebug is on: what to draw. eye and forward: where
the camera is and looks, for the wall under the crosshair.
===============
*/
void S_SpatialDebug( spatialDebug_t *out, const vec3_t eye, const vec3_t forward ) {
	const int mask = ( v_spatialDebug && v_spatialDebug->integer > 1 ) ? v_spatialDebug->integer : -1;

	memset( out, 0, sizeof( *out ) );
	if ( !S_SpatialActive() ) {
		return;
	}
	out->active = qtrue;
	VectorCopy( spatial.listenerOrigin, out->listener );
	out->tracers = ( spatial.embree ? SPATIAL_DEBUG_TRACE_EMBREE : 0 ) | ( spatial.reflectGpu ? SPATIAL_DEBUG_TRACE_GPU : 0 );
	out->threads = spatial.threads;
	out->passRays = spatial.maxRays;
	out->passBounces = S_SpatialBounces();
	out->passMsec = spatialReflectUsec.load() * 0.001f;
	out->passes = spatialReflectPasses.load();
	out->settledPasses = spatial.settledPasses;
	out->maxVoices = SPATIAL_VOICES;
	out->reflects = spatial.numReflects;
	out->reflectSlots = Com_Clampi( 0, spatial.numReflectSlots, v_spatialSources->integer );
	out->reverbVolume = v_spatialReflections->integer ? v_spatialReverb->value : 0.0f;
	S_SpatialCountBrushes( &out->brushes, &out->brushesIn );
	out->brushesMoved = spatial.brushesMoved;
	if ( !v_spatialEcho->integer ) {
		out->echo = SPATIAL_DEBUG_ECHO_LIVE;
	} else if ( S_SpatialEchoBaked() ) {
		out->echo = SPATIAL_DEBUG_ECHO_STORED;
		out->echoProbes = spatial.echo.numProbes;
	} else if ( spatialBakeState.load() == BAKE_RUNNING && spatialBake.kind == BAKE_ECHO ) {
		out->echo = SPATIAL_DEBUG_ECHO_BAKING;
		out->echoProgress = spatialBakeProgress.load();
	} else {
		out->echo = SPATIAL_DEBUG_ECHO_NONE;
	}

	spatial.directLevel = S_SpatialDebugLevel( spatial.directEnergy, spatial.energySamples, spatial.directLevel );
	spatial.reverbLevel = S_SpatialDebugLevel( spatial.reverbEnergy, spatial.energySamples, spatial.reverbLevel );
	spatial.directEnergy = spatial.reverbEnergy = 0.0;
	spatial.energySamples = 0;
	out->direct = spatial.directLevel;
	out->reverb = spatial.reverbLevel;

	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		const spatialVoice_t *v = &spatial.voices[i];
		spatialDebugSound_t *d;

		if ( !v->active || v->frame != spatial.frame ) {
			continue;
		}
		out->voices++;
		if ( out->numSounds >= SPATIAL_DEBUG_SOUNDS ) {
			continue;
		}
		d = &out->sounds[out->numSounds++];
		VectorCopy( v->origin, d->origin );
		if ( v->stream ) {
			const int k = (int)( v->stream - spatialStreams );

			if ( k < MAX_CLIENTS ) {
				Com_sprintf( d->name, sizeof( d->name ), "voice chat, player %i", k );
			} else if ( k == SPATIAL_OWN_STREAM ) {
				Q_strncpyz( d->name, "own voice", sizeof( d->name ) );
			} else {
				Com_sprintf( d->name, sizeof( d->name ), "boombox speaker %i", k - SPATIAL_OWN_STREAM - 1 );
			}
			d->flags |= SPATIAL_DEBUG_STREAM;
		} else {
			const char *name = v->sfx ? v->sfx->sSoundName : "?";

			Q_strncpyz( d->name, Q_stristr( name, "sound/" ) == name ? name + 6 : name, sizeof( d->name ) );
		}
		d->distance = v->distance;
		d->gain = v->gain;
		d->occlusion = v->sendOnly ? 1.0f : v->occlusion;
		for ( int k = 0; k < 3; k++ ) {
			d->transmission[k] = v->sendOnly ? 1.0f : v->transmission[k];
		}
		if ( v->reflect && S_SpatialReflectReady( v->reflect ) ) {
			d->flags |= SPATIAL_DEBUG_OWN;
		}
		if ( v->loop ) {
			d->flags |= SPATIAL_DEBUG_LOOP;
		}
		if ( v->pathing ) {
			d->flags |= SPATIAL_DEBUG_PATHING;
		}
		if ( v->sendOnly ) {
			d->flags |= SPATIAL_DEBUG_SEND;
		}
	}

	out->numFallbacks = spatial.numFallbacks;
	memcpy( out->fallbacks, spatial.fallbacks, spatial.numFallbacks * sizeof( out->fallbacks[0] ) );
	out->numSegments = spatial.numSegments;
	memcpy( out->segments, spatial.segments, spatial.numSegments * sizeof( out->segments[0] ) );

	if ( !( mask & ( SPATIAL_DEBUG_REFLECTIONS | SPATIAL_DEBUG_PANEL ) ) || !S_SpatialDebugMesh() ) {
		return;
	}
	if ( mask & SPATIAL_DEBUG_REFLECTIONS ) {
		S_SpatialDebugRays();
		out->numRays = spatialDebugMesh.numRays;
		memcpy( out->rays, spatialDebugMesh.rays, spatialDebugMesh.numRays * sizeof( out->rays[0] ) );
	}
	if ( mask & SPATIAL_DEBUG_PANEL ) {
		vec3_t dir, n;
		float t;
		const spatialDebugTri_t *tri;

		VectorNormalize2( forward, dir );
		tri = S_SpatialDebugTraceAll( eye, dir, 16384.0f, &t, n );
		if ( tri ) {
			out->aimHit = qtrue;
			VectorMA( eye, t, dir, out->aimPoint );
			out->aimDistance = t;
			out->aimMaterial = tri->material;
			out->aimByTable = tri->byTable ? qtrue : qfalse;
			out->aimBrush = ( tri - spatialDebugMesh.tris ) >= spatialDebugMesh.worldTris ? qtrue : qfalse;
			Q_strncpyz( out->aimShader, tri->shader ? tri->shader : "?", sizeof( out->aimShader ) );
		}
	}
}

#endif // JP_SPATIAL
