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

// snd_spatial.cpp -- sound that knows the level: muffled through walls, reflections that fit the room, sound
// that comes round a corner. Not from JA+ and not in the game's own mixer, see docs/japlus/audio.md.
//
// The work is done by Steam Audio of Valve (lib/steamaudio, a submodule, Apache 2.0). The engine is not linked
// against it: it takes the header from there and loads the library when the sound system starts, every function
// through a pointer. Without the library file, or with v_spatial 0, nothing in here runs and the game sounds as
// it always did.
//
// The client for a browser does the same with phonon.wasm, a module the page takes along when the server has
// it, see "In a browser" in docs/japlus/audio.md. What is different there (__EMSCRIPTEN__): a page has one
// thread, so what the two threads of these files do elsewhere is done by the game itself, in a slice of a frame
// and while a level loads (snd_spatial_page.cpp).
//
// The files of this folder:
//	snd_spatial.cpp			the library, the cvars, start and end of the sound system
//	snd_spatial_level.cpp	the walls of the level and of its brush models, what they are made of
//	snd_spatial_bake.cpp	what is worked out once per level and kept in a file: paths, echo
//	snd_spatial_sim.cpp		what is in the way of each sound, the thread of the reflections and the paths
//	snd_spatial_voice.cpp	which sound of the mixer is which voice, the voice chat and the boombox
//	snd_spatial_render.cpp	the voices and the reverb rendered ahead, into the ring the mixer adds from
//	snd_spatial_debug.cpp	v_spatialInfo, v_spatialCapture, v_spatialDebug
//	snd_spatial_page.cpp	the client in a browser
//	snd_spatial_local.h		what they share. snd_spatial.h: what the rest of the engine calls

#include "snd_spatial_local.h"

#ifdef JP_SPATIAL

#if defined(__EMSCRIPTEN__)
#define SPATIAL_LIBRARY	"phonon.wasm"	// one of the modules the page compiled, built from lib/steamaudio by this tree
#elif defined(_WIN32)
#define SPATIAL_LIBRARY	"phonon.dll"
#elif defined(MACOS_X)
#define SPATIAL_LIBRARY	"libphonon.dylib"
#else
#define SPATIAL_LIBRARY	"libphonon.so"
#endif

#define X( name )	decltype( &name ) q##name;
SPATIAL_FUNCTIONS( X )
#undef X
decltype( &iplEmbreeDeviceCreate )	qiplEmbreeDeviceCreate;
decltype( &iplEmbreeDeviceRelease )	qiplEmbreeDeviceRelease;
cvar_t	*v_spatial;
cvar_t	*v_spatialHrtf;
cvar_t	*v_spatialAhead;
cvar_t	*v_spatialReflections;
cvar_t	*v_spatialReverb;
cvar_t	*v_dreamReverb;
cvar_t	*v_spatialRays;
cvar_t	*v_spatialBounces;
cvar_t	*v_spatialPaths;
cvar_t	*v_spatialPathLoss;
cvar_t	*v_spatialShock;
cvar_t	*v_spatialSources;
cvar_t	*v_spatialThreads;
cvar_t	*v_spatialPhysical;
#ifdef __EMSCRIPTEN__
cvar_t	*v_spatialBudget;
#endif
cvar_t	*v_spatialDebug;
cvar_t	*v_spatialEcho;
cvar_t	*v_spatialGpu;

spatialState_t	spatial;

// Steam Audio stays loaded from its first start to the end of the game, and so do its context and its Embree
// device. Made anew with every start of the sound system, each Embree device left its pool of threads behind (23
// on a CPU of 24 threads, made when the first level was built in it): a player who switched 3D Sound on and off
// had 445 threads after 28 starts (2026-10-28), also before the rays went to the graphics card.
static struct {
	void			*library;
	char			path[MAX_OSPATH];
	IPLContext		context;
	IPLEmbreeDevice	embree;
	qboolean		embreeAsked;		// it was made, or the library has none: asked once
} spatialKept;

static void S_SpatialUnload( void ) {
	// a bake that still runs calls into it: its functions stay until S_SpatialReapBake(). The library stays anyway.
	spatial.library = NULL;
	if ( spatialBake.thread && spatialBakeState.load() == BAKE_RUNNING ) {
		return;
	}
#define X( name )	q##name = NULL;
	SPATIAL_FUNCTIONS( X )
#undef X
	qiplEmbreeDeviceCreate = NULL;
	qiplEmbreeDeviceRelease = NULL;
}

// Next to the program first: that is where a build puts it. Then wherever the system looks. A page has its
// modules by name only: there both ask for the same one.
static qboolean S_SpatialLoad( void ) {
	const char *missing = NULL;

	// (the folder of the program as the system names it: with a bare program name Sys_BinaryPath() is "", which made
	// "/" SPATIAL_LIBRARY the root of the drive, and the bare name below is looked for in the current folder too)
#if defined(__EMSCRIPTEN__)
	const char *folder = "";
#else
	const char *folder = Sys_ProgramFolder();
#endif

	if ( spatialKept.library ) {
		spatial.library = spatialKept.library;
		Q_strncpyz( spatial.libraryPath, spatialKept.path, sizeof( spatial.libraryPath ) );
		folder = NULL;
	}
	if ( folder ) {
		Com_sprintf( spatial.libraryPath, sizeof( spatial.libraryPath ), "%s/" SPATIAL_LIBRARY, folder[0] ? folder : Sys_BinaryPath() );
		spatial.library = Sys_LoadLibrary( spatial.libraryPath );
	}
	if ( !spatial.library && folder && !folder[0] ) {
		Q_strncpyz( spatial.libraryPath, SPATIAL_LIBRARY, sizeof( spatial.libraryPath ) );
		spatial.library = Sys_LoadLibrary( spatial.libraryPath );
	}
	if ( !spatial.library ) {
		Com_Printf( "v_spatial: no " SPATIAL_LIBRARY " next to the program (%s), the sound stays as it is\n", Sys_BinaryPath() );
		return qfalse;
	}
#define X( name )	q##name = (decltype( q##name ))Sys_LoadFunction( spatial.library, #name ); if ( !q##name ) { missing = #name; }
	SPATIAL_FUNCTIONS( X )
#undef X
	if ( missing ) {
		Com_Printf( "v_spatial: %s has no %s, the sound stays as it is\n", spatial.libraryPath, missing );
		if ( !spatialKept.library ) {
			Sys_UnloadLibrary( spatial.library );
		}
		S_SpatialUnload();
		return qfalse;
	}
	spatialKept.library = spatial.library;
	Q_strncpyz( spatialKept.path, spatial.libraryPath, sizeof( spatialKept.path ) );
#ifndef __EMSCRIPTEN__
	qiplEmbreeDeviceCreate = (decltype( qiplEmbreeDeviceCreate ))Sys_LoadFunction( spatial.library, "iplEmbreeDeviceCreate" );
	qiplEmbreeDeviceRelease = (decltype( qiplEmbreeDeviceRelease ))Sys_LoadFunction( spatial.library, "iplEmbreeDeviceRelease" );
#endif
	return qtrue;
}

// the simulator of the graphics card, its sources and its scene, and then the device they called into
static void S_SpatialReleaseGpu( void ) {
	for ( int i = 0; i < SPATIAL_REFLECT_SLOTS; i++ ) {
		if ( spatial.reflects[i].gpuSource ) {
			qiplSourceRelease( &spatial.reflects[i].gpuSource );
		}
	}
	if ( spatial.gpuReverbSource ) {
		qiplSourceRelease( &spatial.gpuReverbSource );
	}
	if ( spatial.gpuSimulator ) {
		qiplSimulatorRelease( &spatial.gpuSimulator );
	}
	if ( spatial.gpuScene ) {
		qiplSceneRelease( &spatial.gpuScene );
	}
	spatial.reflectGpu = spatial.heardGpu = qfalse;
	S_SpatialGpuShutdown();
}

// lets go of everything of Steam Audio, in the order it depends on each other, and of the library
static void S_SpatialRelease( void ) {
	S_SpatialDebugFree();
	// The thread of the reflections ends first: nothing may be taken from under a pass. This waits for the
	// pass it is in, if any, which is the one place the game waits for it, when the sound system is shut down.
	if ( spatial.worker ) {
		spatialWorkerQuit.store( 1 );
		SDL_SemPost( spatial.workerWake );
		SDL_WaitThread( spatial.worker, NULL );
		spatial.worker = NULL;
	}
	if ( spatial.workerWake ) {
		SDL_DestroySemaphore( spatial.workerWake );
		spatial.workerWake = NULL;
	}
	spatialWorkerQuit.store( 0 );
	spatialWorkerState.store( WORKER_IDLE );

	// A bake is not waited for: what it makes is not wanted, and one that is done is picked up by the next sound
	// system or by S_SpatialBakeRunning(). It holds its own context and scene, and Steam Audio stays loaded for it
	// (S_SpatialUnload()).
	S_SpatialCancelBake();
	if ( S_SpatialReapBake() ) {
		S_SpatialDropBake();
	}

	S_SpatialCaptureEnd( qfalse );
	if ( spatial.library ) {
		if ( spatial.simulator ) {
			S_SpatialRemoveBaked( &spatial.paths );
			S_SpatialRemoveBaked( &spatial.echo );
			qiplSimulatorSetScene( spatial.simulator, NULL );
			qiplSimulatorCommit( spatial.simulator );
		}
		S_SpatialReleaseBaked( &spatial.paths );
		S_SpatialReleaseBaked( &spatial.echo );
		S_SpatialReleaseLevel( &spatial.level );
		S_SpatialReleaseLevel( &spatial.nextLevel );
		S_SpatialReleaseGpu();
		if ( spatial.reverbDecode ) {
			qiplAmbisonicsDecodeEffectRelease( &spatial.reverbDecode );
		}
		if ( spatial.reverbEffect ) {
			qiplReflectionEffectRelease( &spatial.reverbEffect );
		}
		for ( int i = 0; i < 2; i++ ) {
			if ( spatial.dreamEffect[i] ) {
				qiplReflectionEffectRelease( &spatial.dreamEffect[i] );
			}
		}
		if ( spatial.reverbSource ) {
			qiplSourceRelease( &spatial.reverbSource );
		}
		for ( int i = 0; i < spatial.numReflectSlots; i++ ) {
			if ( spatial.reflects[i].effect ) {
				qiplReflectionEffectRelease( &spatial.reflects[i].effect );
			}
			if ( spatial.reflects[i].source ) {
				qiplSourceRelease( &spatial.reflects[i].source );
			}
		}
		if ( spatial.reflectMixer ) {
			qiplReflectionMixerRelease( &spatial.reflectMixer );
		}
		for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
			spatialVoice_t *v = &spatial.voices[i];

			if ( v->path ) {
				qiplPathEffectRelease( &v->path );
			}
			if ( v->panning ) {
				qiplPanningEffectRelease( &v->panning );
			}
			if ( v->binaural ) {
				qiplBinauralEffectRelease( &v->binaural );
			}
			if ( v->direct ) {
				qiplDirectEffectRelease( &v->direct );
			}
			if ( v->source ) {
				qiplSourceRelease( &v->source );
			}
		}
		if ( spatial.simulator ) {
			qiplSimulatorRelease( &spatial.simulator );
		}
	}
	if ( spatial.hrtf ) {
		qiplHRTFRelease( &spatial.hrtf );
	}
	// the context and the Embree device stay (spatialKept)
	S_SpatialUnload();
	memset( &spatial, 0, sizeof( spatial ) );
}

// What a simulator is made with: the one of Embree for all of it, the one of the graphics card for the reflections
static void S_SpatialSimulationSettings( IPLSimulationSettings *simulation, IPLSimulationFlags flags, IPLSceneType sceneType ) {
	memset( simulation, 0, sizeof( *simulation ) );
	simulation->flags = flags;
	simulation->sceneType = sceneType;
	simulation->reflectionType = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
	simulation->maxNumOcclusionSamples = SPATIAL_OCCLUSION_SAMPLES;
	simulation->maxNumRays = spatial.maxRays;
	simulation->numDiffuseSamples = SPATIAL_DIFFUSE_SAMPLES;
	simulation->maxDuration = SPATIAL_REVERB_SECONDS;
	simulation->maxOrder = SPATIAL_REVERB_ORDER;
	simulation->maxNumSources = 1 + SPATIAL_REFLECT_SLOTS;	// sources with reflections: the one at the listener and the slots
	// threads Steam Audio shares a pass of the rays between, inside the one of S_SpatialWorker()
	simulation->numThreads = spatial.threads;
	simulation->numVisSamples = 4;
	simulation->samplingRate = spatial.audio.samplingRate;
	simulation->frameSize = spatial.audio.frameSize;
}

/*
===============
S_SpatialCreateGpu

The simulator of the reflections on the graphics card (v_spatialGpu, snd_spatial_gpu.cpp), with a source of its own
for the reverb and for each slot. Made whenever there is a device for it, so that v_spatialGpu can be switched while
the game runs: the level is put onto the graphics card while it loads. Without one the CPU traces all the rays.
===============
*/
static void S_SpatialCreateGpu( void ) {
	IPLSimulationSettings simulation;
	IPLSceneSettings sceneSettings;
	IPLSourceSettings sourceSettings;
	qboolean ok;

#ifdef __EMSCRIPTEN__
	return;
#endif
	if ( !S_SpatialGpuInit() ) {
		if ( v_spatialGpu->integer ) {
			Com_Printf( "v_spatialGpu: %s, the CPU traces the rays of the echo\n", S_SpatialGpuWhyNot() );
		}
		return;
	}
	S_SpatialGpuSceneSettings( &sceneSettings );
	S_SpatialSimulationSettings( &simulation, IPL_SIMULATIONFLAGS_REFLECTIONS, IPL_SCENETYPE_CUSTOM );
	// each thread of Steam Audio takes its share of the rays to the graphics card as one batch
	simulation.rayBatchSize = ( spatial.maxRays + spatial.threads - 1 ) / spatial.threads;
	memset( &sourceSettings, 0, sizeof( sourceSettings ) );
	sourceSettings.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;

	ok = (qboolean)( qiplSceneCreate( spatial.context, &sceneSettings, &spatial.gpuScene ) == IPL_STATUS_SUCCESS );
	if ( !ok ) {
		spatial.gpuScene = NULL;
	}
	if ( ok && qiplSimulatorCreate( spatial.context, &simulation, &spatial.gpuSimulator ) != IPL_STATUS_SUCCESS ) {
		spatial.gpuSimulator = NULL;
		ok = qfalse;
	}
	if ( ok && qiplSourceCreate( spatial.gpuSimulator, &sourceSettings, &spatial.gpuReverbSource ) != IPL_STATUS_SUCCESS ) {
		spatial.gpuReverbSource = NULL;
		ok = qfalse;
	}
	for ( int i = 0; ok && i < spatial.numReflectSlots; i++ ) {
		if ( qiplSourceCreate( spatial.gpuSimulator, &sourceSettings, &spatial.reflects[i].gpuSource ) != IPL_STATUS_SUCCESS ) {
			spatial.reflects[i].gpuSource = NULL;
			ok = qfalse;
		}
	}
	if ( !ok ) {
		Com_Printf( "v_spatialGpu: Steam Audio gives no simulator for the graphics card, the CPU traces the rays of the echo\n" );
		S_SpatialReleaseGpu();
		return;
	}
	qiplSimulatorSetScene( spatial.gpuSimulator, spatial.gpuScene );
	qiplSourceAdd( spatial.gpuReverbSource, spatial.gpuSimulator );
	for ( int i = 0; i < spatial.numReflectSlots; i++ ) {
		qiplSourceAdd( spatial.reflects[i].gpuSource, spatial.gpuSimulator );
	}
	qiplSimulatorCommit( spatial.gpuSimulator );
}

// the simulator, and for every voice its source in it and its effects: all there is of them, made once
static qboolean S_SpatialCreateVoices( void ) {
	IPLSimulationSettings simulation;
	IPLSourceSettings sourceSettings;
	IPLDirectEffectSettings directSettings;
	IPLBinauralEffectSettings binauralSettings;
	IPLPanningEffectSettings panningSettings;
	IPLReflectionEffectSettings reflectionSettings, dreamSettings;
	IPLAmbisonicsDecodeEffectSettings decodeSettings;
	IPLPathEffectSettings pathSettings;

	// A pass traces the first rays of those the simulator has, which are not spread round the listener unless
	// it traces them all (S_SpatialRays): the simulator is made for what it traces, and v_spatialRays is read here.
	spatial.maxRays = S_SpatialRayCvar();
	v_spatialRays->modified = qfalse;
	S_SpatialSimulationSettings( &simulation, (IPLSimulationFlags)( IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS | IPL_SIMULATIONFLAGS_PATHING ),
		spatial.sceneType );
	if ( qiplSimulatorCreate( spatial.context, &simulation, &spatial.simulator ) != IPL_STATUS_SUCCESS ) {
		spatial.simulator = NULL;
		return qfalse;
	}

	// A page has no slots for reflections of a sound of its own: a pass there is spread over many frames already.
#ifdef __EMSCRIPTEN__
	spatial.numReflectSlots = 0;
#else
	spatial.numReflectSlots = SPATIAL_REFLECT_SLOTS;
#endif
	memset( &sourceSettings, 0, sizeof( sourceSettings ) );
	sourceSettings.flags = (IPLSimulationFlags)( IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_PATHING );
	memset( &pathSettings, 0, sizeof( pathSettings ) );
	pathSettings.maxOrder = SPATIAL_REVERB_ORDER;
	pathSettings.spatialize = IPL_TRUE;
	pathSettings.speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_STEREO;
	pathSettings.hrtf = spatial.hrtf;
	memset( &directSettings, 0, sizeof( directSettings ) );
	directSettings.numChannels = 1;
	memset( &binauralSettings, 0, sizeof( binauralSettings ) );
	binauralSettings.hrtf = spatial.hrtf;
	memset( &panningSettings, 0, sizeof( panningSettings ) );
	panningSettings.speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_STEREO;

	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		spatialVoice_t *v = &spatial.voices[i];

		if ( qiplSourceCreate( spatial.simulator, &sourceSettings, &v->source ) != IPL_STATUS_SUCCESS
			|| qiplDirectEffectCreate( spatial.context, &spatial.audio, &directSettings, &v->direct ) != IPL_STATUS_SUCCESS
			|| qiplBinauralEffectCreate( spatial.context, &spatial.audio, &binauralSettings, &v->binaural ) != IPL_STATUS_SUCCESS
			|| qiplPanningEffectCreate( spatial.context, &spatial.audio, &panningSettings, &v->panning ) != IPL_STATUS_SUCCESS
			|| qiplPathEffectCreate( spatial.context, &spatial.audio, &pathSettings, &v->path ) != IPL_STATUS_SUCCESS ) {
			return qfalse;
		}
		qiplSourceAdd( v->source, spatial.simulator );
		for ( int k = 0; k < 3; k++ ) {
			v->pathEq[k] = SPATIAL_PATH_EQ_START;
		}
	}

	// the reverb of where the listener stands: its source, the effect that rings and the one that turns what
	// comes back from all sides into two ears or two speakers
	sourceSettings.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
	memset( &reflectionSettings, 0, sizeof( reflectionSettings ) );
	reflectionSettings.type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
	reflectionSettings.irSize = (int)( SPATIAL_REVERB_SECONDS * spatial.audio.samplingRate );
	reflectionSettings.numChannels = SPATIAL_REVERB_CHANNELS;
	memset( &decodeSettings, 0, sizeof( decodeSettings ) );
	decodeSettings.speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_STEREO;
	decodeSettings.hrtf = spatial.hrtf;
	decodeSettings.maxOrder = SPATIAL_REVERB_ORDER;
	if ( qiplSourceCreate( spatial.simulator, &sourceSettings, &spatial.reverbSource ) != IPL_STATUS_SUCCESS
		|| qiplReflectionEffectCreate( spatial.context, &spatial.audio, &reflectionSettings, &spatial.reverbEffect ) != IPL_STATUS_SUCCESS
		|| qiplAmbisonicsDecodeEffectCreate( spatial.context, &spatial.audio, &decodeSettings, &spatial.reverbDecode ) != IPL_STATUS_SUCCESS ) {
		return qfalse;
	}
	qiplSourceAdd( spatial.reverbSource, spatial.simulator );

	// the reverb of a dream: an artificial one (feedback delay networks), one for each ear. Settings of its own:
	// the effects of the sounds below take the ones of the room.
	memset( &dreamSettings, 0, sizeof( dreamSettings ) );
	dreamSettings.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
	dreamSettings.irSize = reflectionSettings.irSize;
	dreamSettings.numChannels = 1;
	for ( int i = 0; i < 2; i++ ) {
		if ( qiplReflectionEffectCreate( spatial.context, &spatial.audio, &dreamSettings, &spatial.dreamEffect[i] ) != IPL_STATUS_SUCCESS ) {
			spatial.dreamEffect[i] = NULL;
			return qfalse;
		}
	}

	// the sounds with reflections of their own: their sources, their convolutions, and what adds them up
	for ( int i = 0; i < spatial.numReflectSlots; i++ ) {
		spatialReflect_t *r = &spatial.reflects[i];

		if ( qiplSourceCreate( spatial.simulator, &sourceSettings, &r->source ) != IPL_STATUS_SUCCESS ) {
			r->source = NULL;
			return qfalse;
		}
		qiplSourceAdd( r->source, spatial.simulator );
		if ( qiplReflectionEffectCreate( spatial.context, &spatial.audio, &reflectionSettings, &r->effect ) != IPL_STATUS_SUCCESS ) {
			r->effect = NULL;
			return qfalse;
		}
	}
	if ( spatial.numReflectSlots && qiplReflectionMixerCreate( spatial.context, &spatial.audio, &reflectionSettings, &spatial.reflectMixer ) != IPL_STATUS_SUCCESS ) {
		spatial.reflectMixer = NULL;
		return qfalse;
	}
	qiplSimulatorCommit( spatial.simulator );
	S_SpatialCreateGpu();

#ifdef __EMSCRIPTEN__
	// no thread: S_SpatialSlice() does the passes
	return qtrue;
#else
	spatial.workerWake = SDL_CreateSemaphore( 0 );
	spatial.worker = spatial.workerWake ? SDL_CreateThread( S_SpatialWorker, "v_spatial", NULL ) : NULL;
	return (qboolean)( spatial.worker != NULL );
#endif
}

/*
===============
S_SpatialInit

v_spatial is latched: what it is when the sound system starts holds until the next snd_restart
===============
*/
void S_SpatialInit( void ) {
	IPLContextSettings contextSettings;
	IPLHRTFSettings hrtfSettings;

	v_spatial = Cvar_Get( "v_spatial", SPATIAL_DEFAULT, CVAR_ARCHIVE | CVAR_LATCH, "Sound that knows the level, with Steam Audio (snd_restart)" );
	v_spatialHrtf = Cvar_Get( "v_spatialHrtf", "1", CVAR_ARCHIVE, "v_spatial: 1 direction for headphones (HRTF), 0 for speakers" );
	v_spatialAhead = Cvar_Get( "v_spatialAhead", "0.1", CVAR_ARCHIVE, "v_spatial: most seconds of sound rendered ahead, up to 0.5. Below that it follows the frame time: less follows a turn of the head sooner, more survives a longer hitch" );
	v_spatialReflections = Cvar_Get( "v_spatialReflections", "1", CVAR_ARCHIVE, "v_spatial: 1 the room around the listener rings, worked out with rays. 0 no reverb" );
	v_spatialReverb = Cvar_Get( "v_spatialReverb", "1", CVAR_ARCHIVE, "v_spatial: how loud the reverb is" );
	v_dreamReverb = Cvar_Get( "v_dreamReverb", "0", CVAR_TEMP, "v_spatial: 0 to 1, a long reverb as in a dream, set by the cgame while Force Sight is on" );
	v_spatialRays = Cvar_Get( "v_spatialRays", SPATIAL_DEFAULT_RAYS, CVAR_ARCHIVE, "v_spatial: rays of a pass of the reflections, 256 to 8192. From the next snd_restart on" );
#ifdef __EMSCRIPTEN__
	v_spatialPaths = Cvar_Get( "v_spatialPaths", "1", CVAR_ARCHIVE, "v_spatial: 1 a sound comes round corners. Worked out once per level while it loads, in a browser not for a large level" );
	v_spatialBudget = Cvar_Get( "v_spatialBudget", "0.75", CVAR_ARCHIVE, "v_spatial in a browser: milliseconds of a frame the passes of the reflections take on average, 0.1 to 8. More: the reverb follows the listener sooner" );
#else
	v_spatialPaths = Cvar_Get( "v_spatialPaths", "1", CVAR_ARCHIVE, "v_spatial: 1 a sound comes round corners. Worked out once per level in the background" );
	v_spatialThreads = Cvar_Get( "v_spatialThreads", "0", CVAR_ARCHIVE, "v_spatial: threads Steam Audio works the sound out with, for the passes and the bakes of a level. 0 a quarter of the cores (at most 4) for the passes and half of them for a bake. From the next snd_restart on" );
#endif
	v_spatialBounces = Cvar_Get( "v_spatialBounces", SPATIAL_DEFAULT_BOUNCES, CVAR_ARCHIVE, "v_spatial: how often a ray is followed off a wall" );
	v_spatialPathLoss = Cvar_Get( "v_spatialPathLoss", "1", CVAR_ARCHIVE, "v_spatial: 1 a sound loses on its way round a corner what Steam Audio works out. 0 it only gets duller, and is as loud as the game makes a sound that far away" );
	v_spatialSources = Cvar_Get( "v_spatialSources", "8", CVAR_ARCHIVE, "v_spatial: how many of the loudest sounds have reflections of their own, 0 to 16. The others ring with the room of the listener" );
	v_spatialPhysical = Cvar_Get( "v_spatialPhysical", "0", CVAR_ARCHIVE, "v_spatial: 1 sounds fade with the distance as real ones do and not as the game fades them, lose their highs in the air, and arrive at the speed of sound" );
	v_spatialShock = Cvar_Get( "v_spatialShock", "1", CVAR_ARCHIVE, "v_spatial: how strong the thump of an explosion is, its lows in both ears. 0 none" );
	v_spatialEcho = Cvar_Get( "v_spatialEcho", "0", CVAR_ARCHIVE, "v_spatial: 0 the echo is worked out as you go, with the own echoes of the loudest sounds. 1 it is worked out once per level and kept in a file, and looked up where you stand: it does not flutter while you move, all sounds ring with the room you are in" );
	v_spatialGpu = Cvar_Get( "v_spatialGpu", "1", CVAR_ARCHIVE, "v_spatial: 1 the rays of the echo are traced on the graphics card (Vulkan ray queries) where it has them, 0 on the CPU (Embree). From the next pass on" );
	v_spatialDebug = Cvar_Get( "v_spatialDebug", "0", CVAR_NONE, "v_spatial: shows in the level what becomes of the sounds. 1 all, or the sum of 2 the sounds, 4 the ways round corners, 8 rays off the walls, 16 the numbers and the wall under the crosshair" );
	Cmd_AddCommand( "v_spatialInfo", S_SpatialInfo_f, "What v_spatial is doing" );
	Cmd_AddCommand( "v_spatialSaveScene", S_SpatialSaveScene_f, "v_spatial: the walls Steam Audio has, with the brush models where they are, into steamaudio/scene.obj" );
	Cmd_AddCommand( "v_spatialCapture", S_SpatialCapture_f, "v_spatial: what the sounds of the level give the mixer for so many seconds, into steamaudio/capture.wav" );

	memset( &spatial, 0, sizeof( spatial ) );
	memset( spatialStreams, 0, sizeof( spatialStreams ) );
	Cvar_Get( "v_spatialVoice", "0", CVAR_ROM, "1: this engine plays the voices of the voice chat as sounds in the level (v_spatial)" );
	Cvar_Set( "v_spatialVoice", "0" );
	Cvar_Get( "v_spatialOwnVoice", "0", CVAR_ROM, "1: this engine lets the room ring from what the own player says into the voice chat (v_spatial)" );
	Cvar_Set( "v_spatialOwnVoice", "0" );
	Cvar_Get( "v_spatialBoxes", "0", CVAR_ROM, "How many boxes of the boombox this engine plays as sounds in the level (v_spatial), as speakers MAX_CLIENTS and up" );
	Cvar_Set( "v_spatialBoxes", "0" );
	// also with v_spatial 0: it may be turned on later, at a server where the file can not be read
	S_SpatialLoadMaterials();
	if ( !v_spatial->integer ) {
		return;
	}
	// Two channels, and not the 22 kHz the game can also run at: the direction of a sound for headphones is
	// in what is above 11 kHz to a good part, and at that rate there is nothing up there
	if ( dma.channels != 2 || dma.speed < 44100 ) {
		Com_Printf( "v_spatial: needs stereo at 44 kHz or more (s_khz 44), the sound stays as it is\n" );
		return;
	}
	if ( !S_SpatialLoad() ) {
		return;
	}

	// the version of the header: the library refuses one it can not serve
	memset( &contextSettings, 0, sizeof( contextSettings ) );
	contextSettings.version = STEAMAUDIO_VERSION;
	contextSettings.simdLevel = IPL_SIMDLEVEL_AVX2;
	if ( !spatialKept.context && qiplContextCreate( &contextSettings, &spatialKept.context ) != IPL_STATUS_SUCCESS ) {
		spatialKept.context = NULL;
	}
	spatial.context = spatialKept.context;
	if ( !spatial.context ) {
		Com_Printf( "v_spatial: %s does not go with version %i.%i.%i of the header, the sound stays as it is\n", spatial.libraryPath,
			STEAMAUDIO_VERSION_MAJOR, STEAMAUDIO_VERSION_MINOR, STEAMAUDIO_VERSION_PATCH );
		spatial.context = NULL;
		S_SpatialRelease();
		return;
	}

	// the rays by Embree where the library has it, and threads for them on the cores the game does not keep
	if ( !spatialKept.embreeAsked && qiplEmbreeDeviceCreate && qiplEmbreeDeviceRelease ) {
		IPLEmbreeDeviceSettings embreeSettings;

		spatialKept.embreeAsked = qtrue;
		memset( &embreeSettings, 0, sizeof( embreeSettings ) );
		if ( qiplEmbreeDeviceCreate( spatial.context, &embreeSettings, &spatialKept.embree ) != IPL_STATUS_SUCCESS ) {
			spatialKept.embree = NULL;
		}
	}
	spatial.embree = spatialKept.embree;
	spatial.sceneType = spatial.embree ? IPL_SCENETYPE_EMBREE : IPL_SCENETYPE_DEFAULT;
	if ( !spatial.embree ) {
		Com_Printf( "v_spatial: no Embree, Steam Audio traces the rays itself\n" );
	}
#ifdef __EMSCRIPTEN__
	spatial.threads = spatial.bakeThreads = SPATIAL_THREADS;
#else
	if ( v_spatialThreads->integer > 0 ) {
		spatial.threads = spatial.bakeThreads = Q_min( v_spatialThreads->integer, SDL_GetCPUCount() );
	} else {
		spatial.threads = Com_Clampi( 1, SPATIAL_PASS_THREADS, SDL_GetCPUCount() / SPATIAL_PASS_CORES );
		spatial.bakeThreads = Q_max( 1, SDL_GetCPUCount() / SPATIAL_BAKE_CORES );
	}
#endif

	spatial.audio.samplingRate = dma.speed;
	spatial.audio.frameSize = SPATIAL_FRAME;

	// the low pass of the thump of an explosion: two poles, flat up to SPATIAL_SHOCK_HZ (Butterworth)
	{
		const double w = 2.0 * M_PI * SPATIAL_SHOCK_HZ / dma.speed;
		const double alpha = sin( w ) * 0.7071067811865476, a0 = 1.0 + alpha;	// sin( w ) / ( 2 Q ), Q = 1 / sqrt( 2 )

		spatial.shockCoeffs[0] = spatial.shockCoeffs[2] = (float)( ( 1.0 - cos( w ) ) * 0.5 / a0 );
		spatial.shockCoeffs[1] = (float)( ( 1.0 - cos( w ) ) / a0 );
		spatial.shockCoeffs[3] = (float)( -2.0 * cos( w ) / a0 );
		spatial.shockCoeffs[4] = (float)( ( 1.0 - alpha ) / a0 );
	}

	// the direction data the library brings along
	memset( &hrtfSettings, 0, sizeof( hrtfSettings ) );
	hrtfSettings.type = IPL_HRTFTYPE_DEFAULT;
	hrtfSettings.volume = 1.0f;
	hrtfSettings.normType = IPL_HRTFNORMTYPE_NONE;
	if ( qiplHRTFCreate( spatial.context, &spatial.audio, &hrtfSettings, &spatial.hrtf ) != IPL_STATUS_SUCCESS ) {
		Com_Printf( "v_spatial: no direction data (HRTF) for %i samples a second, the sound stays as it is\n", dma.speed );
		spatial.hrtf = NULL;
		S_SpatialRelease();		// (until 2026-10-21 this let go of the context and the library, and not of Embree)
		return;
	}

	if ( !S_SpatialCreateVoices() ) {
		Com_Printf( "v_spatial: Steam Audio gives no simulator or no effects, the sound stays as it is\n" );
		S_SpatialRelease();
		return;
	}

	spatial.active = qtrue;
	// tells the cgame that this engine takes the voices of its voice chat (ext.S_SpatialVoice), and the own one
	// for the reverb (ext.S_SpatialOwnVoice)
	Cvar_Set( "v_spatialVoice", "1" );
	Cvar_Set( "v_spatialOwnVoice", "1" );
	// and the songs of the boxes of the boombox, through the same call
	Cvar_Set( "v_spatialBoxes", va( "%i", SPATIAL_BOXES ) );
	// the sound system was started again in the middle of a level
	S_SpatialLoadMap( Cvar_VariableString( "mapname" ) );
	S_SpatialInfo_f();
}

void S_SpatialShutdown( void ) {
	Cmd_RemoveCommand( "v_spatialInfo" );
	Cmd_RemoveCommand( "v_spatialSaveScene" );
	Cmd_RemoveCommand( "v_spatialCapture" );
	Cvar_Set( "v_spatialVoice", "0" );
	Cvar_Set( "v_spatialOwnVoice", "0" );
	Cvar_Set( "v_spatialBoxes", "0" );
	S_SpatialRelease();
}

// The new path is taken for the sounds of a level. Without one (the menus) there is nothing to place, and
// while a new one waits to be taken over the simulator still has the one before.
qboolean S_SpatialActive( void ) {
	return (qboolean)( spatial.active && spatial.numTriangles > 0 && !spatial.sceneWaits );
}

// once a frame, whether a level is loaded or not
void S_SpatialUpdate( void ) {
	if ( spatial.active ) {
		// How long a frame takes, for S_SpatialAhead(): a longer one counts at once, then it goes back slowly. A
		// stop longer than SPATIAL_AHEAD_HITCH (a level that loads) says nothing about the frames after it.
		const int now = Sys_Milliseconds();
		const float took = (float)( now - spatial.lastUpdate );

		spatial.lastFrameMsec = Com_Clamp( 0.0f, (float)SPATIAL_AHEAD_HITCH, took );
		if ( spatial.lastUpdate && took >= 0.0f && took < SPATIAL_AHEAD_HITCH ) {
			spatial.frameMsec = took > spatial.frameMsec ? took : spatial.frameMsec + ( took - spatial.frameMsec ) * SPATIAL_AHEAD_SETTLE;
		}
		spatial.lastUpdate = now;

		if ( v_spatialRays->modified ) {
			v_spatialRays->modified = qfalse;
			if ( S_SpatialRayCvar() != spatial.maxRays ) {
				Com_Printf( "v_spatialRays: taken when the sound system starts (snd_restart), the passes go on with %i rays\n", spatial.maxRays );
			}
		}
#ifdef __EMSCRIPTEN__
		S_SpatialPageUpdate();
#endif
		S_SpatialTakeScene();
		S_SpatialThinkPaths();
	}
}

#else // JP_SPATIAL

// built without it (BuildJPSpatialAudio off, or the submodule lib/steamaudio is not checked out)
void		S_SpatialInit( void ) {}
void		S_SpatialShutdown( void ) {}
qboolean	S_SpatialActive( void ) { return qfalse; }
qboolean	S_SpatialBakeRunning( void ) { return qfalse; }
void		S_SpatialLoadMap( const char *mapname ) {}
void		S_SpatialUpdate( void ) {}
void		S_SpatialBeginFrame( const vec3_t origin, matrix3_t axis ) {}
float		S_SpatialFade( float dist, int channel ) { return S_AttenuateDistance( dist, channel ); }
qboolean	S_SpatialAddChannel( channel_t *ch, const vec3_t origin, float volume, float fade, qboolean voiceVolume ) { return qfalse; }
void		S_SpatialAddSend( channel_t *ch, float volume, qboolean voiceVolume ) {}
qboolean	S_SpatialAddLoop( int entnum, sfx_t *sfx, const vec3_t origin, float volume, float fade ) { return qfalse; }
void		S_SpatialEndFrame( int from, int target ) {}
void		S_SpatialPaint( int from, int to, portable_samplepair_t *paint ) {}
float		S_SpatialAhead( void ) { return 0.0f; }
int			S_SpatialVoice( int clientNum, const vec3_t origin, float volume, const short *samples, int numSamples, int rate ) { return -1; }
void		S_SpatialOwnVoice( float volume, const short *samples, int numSamples, int rate ) {}
void		S_SpatialDebugFallback( const vec3_t origin, const sfx_t *sfx ) {}
void		S_SpatialMovers( const spatialMover_t *list, int count ) {}
void		S_SpatialDebug( spatialDebug_t *out, const vec3_t eye, const vec3_t forward ) { memset( out, 0, sizeof( *out ) ); }

#endif // JP_SPATIAL
