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

// snd_spatial_bake.cpp -- what is worked out once per level and kept in a file: the paths round the corners,
// and the echo at the probes (v_spatialEcho). See snd_spatial.cpp.

#include "snd_spatial_local.h"

#ifdef JP_SPATIAL

spatialBaker_t		spatialBake;
std::atomic<int>	spatialBakeState;
std::atomic<int>	spatialBakeProgress;
std::atomic<int>	spatialBakeCancel;

/*
===============================================================================

THE PATHS OF A LEVEL

===============================================================================
*/

static void IPLCALL S_SpatialBakeProgress( IPLfloat32 progress, void *userData ) {
	spatialBakeProgress.store( (int)( progress * 100.0f ) );
}

// The thread that works the paths of one level out. It touches spatialBake and nothing of the engine.
// A page has no thread for it: there the game calls this while the level loads, see S_SpatialLoadMap().
static int SDLCALL S_SpatialBaker( void *unused ) {
	const int start = S_SpatialUsec();
	IPLProbeGenerationParams generate;
	IPLPathBakeParams bake;
	IPLSerializedObjectSettings serialized;
	IPLProbeArray array = NULL;

	spatialBake.batch = NULL;
	spatialBake.saved = NULL;
	spatialBake.numProbes = 0;

	// Probes over every floor of the box the level is in. Steam Audio spreads them over the cube from -0.5 to 0.5
	// as the matrix places it (generateUniformFloorProbes), so the matrix takes the middle of the box. Until
	// 2026-10-17 it took the lowest corner, and only the quarter of a level next to it had probes.
	memset( &generate, 0, sizeof( generate ) );
	generate.type = IPL_PROBEGENERATIONTYPE_UNIFORMFLOOR;
	generate.spacing = SPATIAL_PROBE_SPACING;
	generate.height = SPATIAL_PROBE_HEIGHT;
	generate.transform.elements[0][0] = spatialBake.maxs.x - spatialBake.mins.x;
	generate.transform.elements[1][1] = spatialBake.maxs.y - spatialBake.mins.y;
	generate.transform.elements[2][2] = spatialBake.maxs.z - spatialBake.mins.z;
	generate.transform.elements[0][3] = ( spatialBake.mins.x + spatialBake.maxs.x ) * 0.5f;
	generate.transform.elements[1][3] = ( spatialBake.mins.y + spatialBake.maxs.y ) * 0.5f;
	generate.transform.elements[2][3] = ( spatialBake.mins.z + spatialBake.maxs.z ) * 0.5f;
	generate.transform.elements[3][3] = 1.0f;

	// a large level gets them further apart, see SPATIAL_MAX_PROBES
	while ( qiplProbeArrayCreate( spatialBake.context, &array ) == IPL_STATUS_SUCCESS ) {
		qiplProbeArrayGenerateProbes( array, spatialBake.scene, &generate );
		spatialBake.numProbes = qiplProbeArrayGetNumProbes( array );
		if ( spatialBake.numProbes <= SPATIAL_MAX_PROBES || spatialBakeCancel.load() ) {
			break;
		}
		qiplProbeArrayRelease( &array );
		array = NULL;
		generate.spacing *= 1.5f;
	}
	spatialBake.spacing = generate.spacing;

	if ( array && qiplProbeBatchCreate( spatialBake.context, &spatialBake.batch ) == IPL_STATUS_SUCCESS ) {
		qiplProbeBatchAddProbeArray( spatialBake.batch, array );
		qiplProbeBatchCommit( spatialBake.batch );

		memset( &bake, 0, sizeof( bake ) );
		bake.scene = spatialBake.scene;
		bake.probeBatch = spatialBake.batch;
		bake.identifier.type = IPL_BAKEDDATATYPE_PATHING;
		bake.identifier.variation = IPL_BAKEDDATAVARIATION_DYNAMIC;
		bake.numSamples = SPATIAL_PATH_SAMPLES;
		bake.radius = SPATIAL_PATH_RADIUS;
		bake.threshold = SPATIAL_PATH_THRESHOLD;
		bake.visRange = SPATIAL_PATH_VIS_RANGE;
		bake.pathRange = SPATIAL_PATH_RANGE;
		bake.numThreads = spatial.threads;
#ifdef __EMSCRIPTEN__
		// too large to do while the game waits, see SPATIAL_PAGE_PROBES: nothing is kept of it
		if ( spatialBake.numProbes > SPATIAL_PAGE_PROBES ) {
			qiplProbeBatchRelease( &spatialBake.batch );
		}
#endif
		if ( spatialBake.batch && spatialBake.numProbes >= SPATIAL_MIN_PROBES && !spatialBakeCancel.load() ) {
			if ( spatialBake.kind == BAKE_ECHO ) {
				// the reverb where a listener stands at each probe, as a pass of the reflections makes it
				IPLReflectionsBakeParams echo;

				memset( &echo, 0, sizeof( echo ) );
				echo.scene = spatialBake.scene;
				echo.probeBatch = spatialBake.batch;
				echo.sceneType = spatialBake.sceneType;
				echo.identifier.type = IPL_BAKEDDATATYPE_REFLECTIONS;
				echo.identifier.variation = IPL_BAKEDDATAVARIATION_REVERB;
				echo.bakeFlags = IPL_REFLECTIONSBAKEFLAGS_BAKECONVOLUTION;
				echo.numRays = SPATIAL_ECHO_BAKE_RAYS;
				echo.numDiffuseSamples = SPATIAL_ECHO_BAKE_DIFFUSE;
				echo.numBounces = spatialBake.bounces;
				echo.simulatedDuration = SPATIAL_REVERB_SECONDS;
				echo.savedDuration = SPATIAL_REVERB_SECONDS;
				echo.order = SPATIAL_REVERB_ORDER;
				echo.numThreads = bake.numThreads;
				echo.irradianceMinDistance = 1.0f;
				echo.bakeBatchSize = 1;
				qiplReflectionsBakerBake( spatialBake.context, &echo, S_SpatialBakeProgress, NULL );
			} else {
				qiplPathBakerBake( spatialBake.context, &bake, S_SpatialBakeProgress, NULL );
			}
		}
		// stopped: see spatialBakeCancel
		if ( spatialBake.batch && spatialBakeCancel.load() ) {
			qiplProbeBatchRelease( &spatialBake.batch );
			spatialBake.batch = NULL;
		}

		// as it goes into the file. The game writes it, the file system of the engine is not for this thread.
		memset( &serialized, 0, sizeof( serialized ) );
		if ( spatialBake.batch && qiplSerializedObjectCreate( spatialBake.context, &serialized, &spatialBake.saved ) == IPL_STATUS_SUCCESS ) {
			qiplProbeBatchSave( spatialBake.batch, spatialBake.saved );
		} else {
			spatialBake.saved = NULL;
		}
	}
	if ( array ) {
		qiplProbeArrayRelease( &array );
	}
	qiplSceneRelease( &spatialBake.scene );
	qiplContextRelease( &spatialBake.context );
	spatialBake.msec = ( S_SpatialUsec() - start ) / 1000;
	spatialBakeState.store( BAKE_DONE );
	return 0;
}

// What the bake that runs makes is not wanted. It is not stopped inside Steam Audio: iplPathBakerCancelBake() of
// 4.8.1 left the threads of the bake waiting for each other for ever (snd_restart hung) or crashed them
// (2026-10-19, a client of the user). It runs to its end and what it made is thrown away. It does not wait for it.
void S_SpatialCancelBake( void ) {
	spatialBakeCancel.store( 1 );
}

// A bake that is done: its thread is at its end, this does not wait. qfalse: there is none, or it still runs.
qboolean S_SpatialReapBake( void ) {
	if ( spatialBakeState.load() != BAKE_DONE ) {
		return qfalse;
	}
	if ( spatialBake.thread ) {
		SDL_WaitThread( spatialBake.thread, NULL );
	}
	spatialBake.thread = NULL;
	spatialBakeState.store( BAKE_NONE );
	// the hold on Steam Audio of a bake the sound system had let go of
	if ( spatialBake.library ) {
		Sys_UnloadLibrary( spatialBake.library );
		spatialBake.library = NULL;
	}
	return qtrue;
}

// For Sys_Exit(): a bake still runs inside Steam Audio. The program then ends without running what is to be
// done when the libraries are unloaded, which would go on under that thread.
qboolean S_SpatialBakeRunning( void ) {
	return (qboolean)( spatialBake.thread && spatialBakeState.load() == BAKE_RUNNING );
}

// What a bake made and nobody took.
void S_SpatialDropBake( void ) {
	if ( spatialBake.batch ) {
		qiplProbeBatchRelease( &spatialBake.batch );
	}
	if ( spatialBake.saved ) {
		qiplSerializedObjectRelease( &spatialBake.saved );
	}
}

// the two kinds of bake
static const struct {
	const char	*suffix;		// of the name of its file, after the level and the checksum of its walls
	const char	*thread;
	const char	*started;		// what the console says when one starts in the background
} spatialBakeKinds[BAKE_NUM] = {
	{ SPATIAL_PATHS_VERSION ".paths", "s_spatialBake", "v_spatial: working out the paths of this level in the background\n" },
	{ SPATIAL_ECHO_VERSION ".echo", "s_spatialEcho", "v_spatial: working out the echo of this level in the background, until then it is worked out as you go\n" },
};

static spatialBaked_t *S_SpatialBakedOf( int kind ) {
	return kind == BAKE_ECHO ? &spatial.echo : &spatial.paths;
}

// The files of a level that waits to be taken over are named after the level and after its walls: another version
// of a level with the same name has other walls and gets files of its own. The versions in the names are for files
// that were worked out another way.
void S_SpatialNameBakes( const char *mapname, unsigned checksum ) {
	char path[MAX_QPATH], name[MAX_QPATH];

	Q_strncpyz( path, mapname, sizeof( path ) );
	COM_StripExtension( COM_SkipPath( path ), name, sizeof( name ) );
	for ( int kind = 0; kind < BAKE_NUM; kind++ ) {
		spatialBaked_t *baked = S_SpatialBakedOf( kind );

		Com_sprintf( baked->nextFile, sizeof( baked->nextFile ), "steamaudio/%s_%08x%s", name, checksum, spatialBakeKinds[kind].suffix );
	}
}

// The probes of this level from its file, if it has one. qtrue also for the file of a level without probes: it is
// not worked out again, and has none.
qboolean S_SpatialLoadBaked( spatialBaked_t *baked ) {
	IPLSerializedObjectSettings serialized;
	IPLSerializedObject object = NULL;
	void *data = NULL;
	const int size = FS_ReadFile( baked->file, &data );
	qboolean loaded = qfalse;

	if ( size <= 0 || !data ) {
		return qfalse;
	}
	memset( &serialized, 0, sizeof( serialized ) );
	serialized.data = (IPLbyte *)data;
	serialized.size = (IPLsize)size;
	if ( qiplSerializedObjectCreate( spatial.context, &serialized, &object ) == IPL_STATUS_SUCCESS ) {
		if ( qiplProbeBatchLoad( spatial.context, object, &baked->next ) == IPL_STATUS_SUCCESS ) {
			baked->bakeMsec = 0;
			baked->spacing = 0.0f;
			loaded = qtrue;
			if ( qiplProbeBatchGetNumProbes( baked->next ) >= SPATIAL_MIN_PROBES ) {
				qiplProbeBatchCommit( baked->next );
				baked->numProbes = qiplProbeBatchGetNumProbes( baked->next );
				baked->wait = qtrue;
			} else {
				qiplProbeBatchRelease( &baked->next );
				baked->next = NULL;
			}
		} else {
			baked->next = NULL;
		}
		qiplSerializedObjectRelease( &object );
	}
	FS_FreeFile( data );
	return loaded;
}

// Out of the simulator. Only while no pass runs, and a commit of the simulator has to follow.
void S_SpatialRemoveBaked( spatialBaked_t *baked ) {
	if ( baked->batch ) {
		qiplSimulatorRemoveProbeBatch( spatial.simulator, baked->batch );
	}
}

// lets go of the probes of a level that is gone, which are no longer in the simulator
void S_SpatialReleaseBaked( spatialBaked_t *baked ) {
	if ( baked->batch ) {
		qiplProbeBatchRelease( &baked->batch );
	}
	if ( baked->next ) {
		qiplProbeBatchRelease( &baked->next );
	}
	baked->batch = baked->next = NULL;
	baked->wait = baked->tried = baked->wanted = qfalse;
	baked->numProbes = 0;
}

// the echo of the level is looked up at the probes, and no ray is traced for it (v_spatialEcho 1)
qboolean S_SpatialEchoBaked( void ) {
	return (qboolean)( v_spatialEcho->integer && spatial.echo.batch );
}

// What a bake that is done made, for the level it was for: into its file, and the probes wait to be handed to the
// simulator. Also the file of a level without probes, so that its next load does not work it out again.
static void S_SpatialTakeBake( void ) {
	spatialBaked_t *baked = S_SpatialBakedOf( spatialBake.kind );

	// (a bake of a sound system before this one has a number of its own: spatialBakeCancel)
	if ( !spatialBakeCancel.load() && spatialBake.sceneNumber == spatial.sceneNumber && spatialBake.batch && spatialBake.saved && !spatial.sceneWaits ) {
		FS_WriteFile( baked->file, qiplSerializedObjectGetData( spatialBake.saved ), (int)qiplSerializedObjectGetSize( spatialBake.saved ) );
		if ( spatialBake.numProbes >= SPATIAL_MIN_PROBES ) {
			baked->next = spatialBake.batch;
			baked->numProbes = spatialBake.numProbes;
			baked->bakeMsec = Q_max( 1, spatialBake.msec );
			baked->spacing = spatialBake.spacing;
			baked->wait = qtrue;
			spatialBake.batch = NULL;
			if ( spatialBake.kind == BAKE_ECHO ) {
				Com_Printf( "v_spatial: the echo of this level is worked out, %i probes in %.1f s, kept in %s\n",
					baked->numProbes, baked->bakeMsec * 0.001f, baked->file );
			} else {
				Com_Printf( "v_spatial: the paths of this level are worked out, %i probes %.1f m apart in %.1f s, kept in %s\n",
					baked->numProbes, baked->spacing, baked->bakeMsec * 0.001f, baked->file );
			}
		} else if ( spatialBake.kind == BAKE_PATHS ) {
			Com_Printf( "v_spatial: no place for probes was found in this level: no sound round corners here\n" );
		}
	}
#ifdef __EMSCRIPTEN__
	else if ( spatialBake.kind == BAKE_PATHS && spatialBake.sceneNumber == spatial.sceneNumber && spatialBake.numProbes > SPATIAL_PAGE_PROBES ) {
		spatial.refusedProbes = spatialBake.numProbes;
		S_SpatialSayTooLarge();
	}
#endif
	// a bake for a level that is gone
	S_SpatialDropBake();
}

// The bake a level waits for, in the background
static void S_SpatialStartBake( int kind ) {
	S_SpatialBakedOf( kind )->wanted = qfalse;
	spatialBake.kind = kind;
	spatialBake.scene = qiplSceneRetain( spatial.level.scene );
	spatialBake.sceneType = spatial.sceneType;
	spatialBake.bounces = S_SpatialBounces();
	spatialBake.context = qiplContextRetain( spatial.context );
	spatialBake.mins = spatial.mins;
	spatialBake.maxs = spatial.maxs;
	spatialBake.sceneNumber = spatial.sceneNumber;
	spatialBakeProgress.store( 0 );
	spatialBakeCancel.store( 0 );
	spatialBakeState.store( BAKE_RUNNING );
#ifdef __EMSCRIPTEN__
	// done when this returns, and picked up by the next call of S_SpatialThinkPaths()
	S_SpatialBaker( NULL );
#else
	spatialBake.thread = SDL_CreateThread( S_SpatialBaker, spatialBakeKinds[kind].thread, NULL );
	if ( !spatialBake.thread ) {
		qiplSceneRelease( &spatialBake.scene );
		qiplContextRelease( &spatialBake.context );
		spatialBakeState.store( BAKE_NONE );
	} else {
		Com_Printf( "%s", spatialBakeKinds[kind].started );
	}
#endif
}

// no bake runs, and the level the simulator has can be worked out
static qboolean S_SpatialCanBake( void ) {
	return (qboolean)( spatialBakeState.load() == BAKE_NONE && spatial.level.scene && !spatial.sceneWaits );
}

// probes that are ready go to the simulator, when no pass of the reflections runs
static void S_SpatialHandOver( spatialBaked_t *baked ) {
	if ( baked->wait && !spatial.sceneWaits && spatialWorkerState.load() == WORKER_IDLE ) {
		qiplSimulatorAddProbeBatch( spatial.simulator, baked->next );
		qiplSimulatorCommit( spatial.simulator );
		baked->batch = baked->next;
		baked->next = NULL;
		baked->wait = qfalse;
	}
}

/*
===============
S_SpatialThinkPaths

Once a frame. Picks up a bake that is done, starts the one this level waits for, and hands probes that are
ready to the simulator when no pass of the reflections runs. Nothing in here waits.
===============
*/
void S_SpatialThinkPaths( void ) {
	if ( S_SpatialReapBake() ) {
		S_SpatialTakeBake();
	}

	// the paths first: their file was looked for when the level was taken over (S_SpatialTakeScene)
	if ( spatial.paths.wanted && S_SpatialCanBake() && v_spatialPaths->integer ) {
		S_SpatialStartBake( BAKE_PATHS );
	}

#ifndef __EMSCRIPTEN__
	// The echo of the level, when it is wanted: from its file, or worked out in the background after the paths
	if ( v_spatialEcho->integer && !spatial.echo.tried && spatial.level.scene && !spatial.sceneWaits ) {
		spatial.echo.tried = qtrue;
		if ( !S_SpatialLoadBaked( &spatial.echo ) ) {
			spatial.echo.wanted = qtrue;
		}
	}
	if ( spatial.echo.wanted && v_spatialEcho->integer && !spatial.paths.wanted && S_SpatialCanBake() ) {
		S_SpatialStartBake( BAKE_ECHO );
	}
#endif

	S_SpatialHandOver( &spatial.paths );
	S_SpatialHandOver( &spatial.echo );
}

#endif // JP_SPATIAL
