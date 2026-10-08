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
// through a pointer. Without the library file, or with s_spatial 0, nothing in here runs and the game sounds as
// it always did.

#include "snd_local.h"
#include "snd_spatial.h"

#ifdef JP_SPATIAL

#include "sys/sys_loadlib.h"
#include "qcommon/cm_public.h"
#include <phonon.h>
#include <SDL.h>
#include <atomic>
#include <chrono>

#if defined(_WIN32)
#define SPATIAL_LIBRARY	"phonon.dll"
#elif defined(MACOS_X)
#define SPATIAL_LIBRARY	"libphonon.dylib"
#else
#define SPATIAL_LIBRARY	"libphonon.so"
#endif

/*
===============================================================================

THE FUNCTIONS OF STEAM AUDIO

phonon.h declares them as functions. Nothing here calls those names: each one is a pointer of the type of its
declaration, which carries the calling convention of the library (IPLCALL, __stdcall on Windows), and is filled
from the library when it is loaded.

===============================================================================
*/

#define SPATIAL_FUNCTIONS( X ) \
	X( iplContextCreate ) \
	X( iplContextRelease ) \
	X( iplHRTFCreate ) \
	X( iplHRTFRelease ) \
	X( iplSceneCreate ) \
	X( iplSceneRelease ) \
	X( iplSceneCommit ) \
	X( iplStaticMeshCreate ) \
	X( iplStaticMeshRelease ) \
	X( iplStaticMeshAdd ) \
	X( iplSimulatorCreate ) \
	X( iplSimulatorRelease ) \
	X( iplSimulatorSetScene ) \
	X( iplSimulatorCommit ) \
	X( iplSimulatorSetSharedInputs ) \
	X( iplSimulatorRunDirect ) \
	X( iplSourceCreate ) \
	X( iplSourceRelease ) \
	X( iplSourceAdd ) \
	X( iplSourceSetInputs ) \
	X( iplSourceGetOutputs ) \
	X( iplDirectEffectCreate ) \
	X( iplDirectEffectRelease ) \
	X( iplDirectEffectReset ) \
	X( iplDirectEffectApply ) \
	X( iplBinauralEffectCreate ) \
	X( iplBinauralEffectRelease ) \
	X( iplBinauralEffectReset ) \
	X( iplBinauralEffectApply ) \
	X( iplPanningEffectCreate ) \
	X( iplPanningEffectRelease ) \
	X( iplPanningEffectReset ) \
	X( iplPanningEffectApply ) \
	X( iplSimulatorRunReflections ) \
	X( iplReflectionEffectCreate ) \
	X( iplReflectionEffectRelease ) \
	X( iplReflectionEffectReset ) \
	X( iplReflectionEffectApply ) \
	X( iplAmbisonicsDecodeEffectCreate ) \
	X( iplAmbisonicsDecodeEffectRelease ) \
	X( iplAmbisonicsDecodeEffectReset ) \
	X( iplAmbisonicsDecodeEffectApply ) \
	X( iplSceneRetain ) \
	X( iplProbeArrayCreate ) \
	X( iplProbeArrayRelease ) \
	X( iplProbeArrayGenerateProbes ) \
	X( iplProbeArrayGetNumProbes ) \
	X( iplProbeBatchCreate ) \
	X( iplProbeBatchRelease ) \
	X( iplProbeBatchLoad ) \
	X( iplProbeBatchSave ) \
	X( iplProbeBatchAddProbeArray ) \
	X( iplProbeBatchCommit ) \
	X( iplProbeBatchGetNumProbes ) \
	X( iplPathBakerBake ) \
	X( iplPathBakerCancelBake ) \
	X( iplSerializedObjectCreate ) \
	X( iplSerializedObjectRelease ) \
	X( iplSerializedObjectGetSize ) \
	X( iplSerializedObjectGetData ) \
	X( iplSimulatorAddProbeBatch ) \
	X( iplSimulatorRemoveProbeBatch ) \
	X( iplSimulatorRunPathing ) \
	X( iplPathEffectCreate ) \
	X( iplPathEffectRelease ) \
	X( iplPathEffectReset ) \
	X( iplPathEffectApply )

#define X( name )	static decltype( &name ) q##name;
SPATIAL_FUNCTIONS( X )
#undef X

// Steam Audio counts in metres. A unit of the game is taken as an inch: the player is 64 units tall.
#define SPATIAL_UNIT	0.0254f

// The surfaces of a level say what they are made of (MATERIAL_* in game/surfaceflags.h, for footsteps and
// impacts). How much sound each one swallows, scatters and lets through are the numbers Valve gives for a
// few standard materials (phonon.h, IPLMaterial). Which material of the game goes with which of them is a
// choice made here. The last entry is the sky: what reaches it is gone, open air gives no echo.
//
// A wall of a level is a brush, and a sound that goes through it crosses two of its sides. Valve's numbers for
// what a material lets through are taken for the wall as a whole, so the mesh gets the square root of each for
// a side (S_SpatialLoadMap): the two sides together are the number again.
enum {
	SPATIAL_GENERIC, SPATIAL_BRICK, SPATIAL_CONCRETE, SPATIAL_CERAMIC, SPATIAL_GRAVEL, SPATIAL_CARPET, SPATIAL_GLASS,
	SPATIAL_PLASTER, SPATIAL_WOOD, SPATIAL_METAL, SPATIAL_ROCK, SPATIAL_SKY, SPATIAL_NUM_MATERIALS
};

static const IPLMaterial spatialMaterials[SPATIAL_NUM_MATERIALS] = {
	{ { 0.10f, 0.20f, 0.30f }, 0.05f, { 0.100f, 0.050f, 0.030f } },		// generic
	{ { 0.03f, 0.04f, 0.07f }, 0.05f, { 0.015f, 0.015f, 0.015f } },		// brick
	{ { 0.05f, 0.07f, 0.08f }, 0.05f, { 0.015f, 0.002f, 0.001f } },		// concrete
	{ { 0.01f, 0.02f, 0.02f }, 0.05f, { 0.060f, 0.044f, 0.011f } },		// ceramic
	{ { 0.60f, 0.70f, 0.80f }, 0.05f, { 0.031f, 0.012f, 0.008f } },		// gravel
	{ { 0.24f, 0.69f, 0.73f }, 0.05f, { 0.020f, 0.005f, 0.003f } },		// carpet
	{ { 0.06f, 0.03f, 0.02f }, 0.05f, { 0.060f, 0.044f, 0.011f } },		// glass
	{ { 0.12f, 0.06f, 0.04f }, 0.05f, { 0.056f, 0.056f, 0.004f } },		// plaster
	{ { 0.11f, 0.07f, 0.06f }, 0.05f, { 0.070f, 0.014f, 0.005f } },		// wood
	{ { 0.20f, 0.07f, 0.06f }, 0.05f, { 0.200f, 0.025f, 0.010f } },		// metal
	{ { 0.13f, 0.20f, 0.24f }, 0.05f, { 0.015f, 0.002f, 0.001f } },		// rock
	{ { 1.00f, 1.00f, 1.00f }, 0.00f, { 0.000f, 0.000f, 0.000f } },		// sky
};

static const char *spatialMaterialNames[SPATIAL_NUM_MATERIALS] = {
	"generic", "brick", "concrete", "ceramic", "gravel", "carpet", "glass", "plaster", "wood", "metal", "rock", "sky"
};

static const byte spatialMaterialOf[MATERIAL_LAST] = {
	SPATIAL_GENERIC,	// MATERIAL_NONE
	SPATIAL_WOOD,		// MATERIAL_SOLIDWOOD
	SPATIAL_WOOD,		// MATERIAL_HOLLOWWOOD
	SPATIAL_METAL,		// MATERIAL_SOLIDMETAL
	SPATIAL_METAL,		// MATERIAL_HOLLOWMETAL
	SPATIAL_CARPET,		// MATERIAL_SHORTGRASS
	SPATIAL_CARPET,		// MATERIAL_LONGGRASS
	SPATIAL_GENERIC,	// MATERIAL_DIRT
	SPATIAL_GRAVEL,		// MATERIAL_SAND
	SPATIAL_GRAVEL,		// MATERIAL_GRAVEL
	SPATIAL_GLASS,		// MATERIAL_GLASS
	SPATIAL_CONCRETE,	// MATERIAL_CONCRETE
	SPATIAL_CERAMIC,	// MATERIAL_MARBLE
	SPATIAL_CERAMIC,	// MATERIAL_WATER
	SPATIAL_CARPET,		// MATERIAL_SNOW
	SPATIAL_CERAMIC,	// MATERIAL_ICE
	SPATIAL_GENERIC,	// MATERIAL_FLESH
	SPATIAL_GENERIC,	// MATERIAL_MUD
	SPATIAL_GLASS,		// MATERIAL_BPGLASS
	SPATIAL_CARPET,		// MATERIAL_DRYLEAVES
	SPATIAL_CARPET,		// MATERIAL_GREENLEAVES
	SPATIAL_CARPET,		// MATERIAL_FABRIC
	SPATIAL_CARPET,		// MATERIAL_CANVAS
	SPATIAL_ROCK,		// MATERIAL_ROCK
	SPATIAL_GENERIC,	// MATERIAL_RUBBER
	SPATIAL_GENERIC,	// MATERIAL_PLASTIC
	SPATIAL_CERAMIC,	// MATERIAL_TILES
	SPATIAL_CARPET,		// MATERIAL_CARPET
	SPATIAL_PLASTER,	// MATERIAL_PLASTER
	SPATIAL_GLASS,		// MATERIAL_SHATTERGLASS
	SPATIAL_METAL,		// MATERIAL_ARMOR
	SPATIAL_METAL,		// MATERIAL_COMPUTER
};

#define SPATIAL_VOICES				64		// the mixer has 32 channels and 32 loop sounds, each can be one
#define SPATIAL_RING				16384	// samples of finished sound that are kept, a power of two
#define SPATIAL_OCCLUSION_SAMPLES	16		// rays from the listener to points around a source: how much of it is hidden
#define SPATIAL_OCCLUSION_RADIUS	0.5f	//	and how large a source is taken to be, in metres
#define SPATIAL_TRANSMISSION_RAYS	4		// walls between a source and the listener that are counted
// How loud the new path is against the old one, which the sounds of the own player and of the menus stay on.
// The old one gives a sound in front half of its volume to each ear and one at the side all of it to one.
// Steam Audio gives each ear all of a sound in front for headphones, and 0.7 of it when it pans for speakers.
// Measured with noise from a speaker 45 degrees to the right: the louder ear is within 1.5 dB of the old path.
#define SPATIAL_GAIN_HRTF			0.5f
#define SPATIAL_GAIN_PANNING		0.8f
// The sounds of the own player stay on the old path, in both ears. The room around him gets them like the
// sounds in the level: this much of what the game plays of them goes into the reverb.
#define SPATIAL_OWN_SEND			1.0f

// The thump of an explosion. Below about 200 Hz the direction data for headphones has no direction: both ears
// get the lows of a sound at SPATIAL_GAIN_HRTF wherever it is, where the old path gives the near ear up to all
// of it. An explosion is mostly lows (rocket, thermal, detpack, concussion: half of the energy is under
// 120 Hz), so its lows go to both ears a second time, past HRTF and panning.
#define SPATIAL_SHOCK_HZ			120.0f	// the thump is what the sound has below this
#define SPATIAL_SHOCK_GAIN			1.0f	//	and this much of it for each ear, at s_spatialShock 1

// Which sounds are explosions: "explo" in the name of the file (the folder sound/weapons/explosions also has
// glass and debris in it), or one of these in its path.
static const char *spatialShockPaths[] = {
	"rocket/hit_wall", "proton/impact", "conc_missile/impact", "mine/impact"
};

// Reflections: what the room around the listener gives back. Rays from where he stands are followed through
// the level, in a thread of its own, and what comes back when and from where becomes the reverb every sound in
// the level is sent through.
#define SPATIAL_REVERB_SECONDS		1.5f	// how long a room can ring
#define SPATIAL_REVERB_ORDER		1		// directions of what comes back: 4 channels (first order Ambisonics)
#define SPATIAL_REVERB_CHANNELS		( ( SPATIAL_REVERB_ORDER + 1 ) * ( SPATIAL_REVERB_ORDER + 1 ) )
#define SPATIAL_MAX_RAYS			8192
#define SPATIAL_DIFFUSE_SAMPLES		32
#define SPATIAL_REFLECT_MSEC		50		// a new pass of the rays is not started sooner than this after the last one
#define SPATIAL_DRY					8192	// samples of each voice that are kept for the reverb, a power of two

enum {
	WORKER_IDLE,		// waits for work. Only in this state may the simulator be given another level
	WORKER_ASKED,
	WORKER_RUNNING
};

// Paths: how a sound gets round a corner. Steam Audio works that out from points it spreads over the floors of
// the level (probes) and from which of them see each other. That is worked out once per level, which takes
// from seconds to minutes: in a thread of its own, the first time the level is loaded, and kept in a file.
#define SPATIAL_PROBE_SPACING		3.0f	// metres between two probes
#define SPATIAL_PROBE_HEIGHT		1.5f	//	and how high above the floor they are
#define SPATIAL_PATH_SAMPLES		1		// points of a probe that are tried when asking whether two see each other
#define SPATIAL_PATH_RADIUS			0.5f	//	and how far from its middle they are, in metres
#define SPATIAL_PATH_THRESHOLD		0.1f
#define SPATIAL_PATH_VIS_RANGE		30.0f	// two probes further apart than this are not asked, in metres
#define SPATIAL_PATH_RANGE			100.0f	// a way longer than this is no way

enum {
	BAKE_NONE,
	BAKE_RUNNING,
	BAKE_DONE
};

// The voice chat of the cgame can hand the voice of a player to this file (S_SpatialVoice), so that it is in
// the level like any other sound. The cgame decodes it and plays it itself on every other engine.
#define SPATIAL_STREAM_FIFO			16384	// samples of a voice that wait to be rendered, at the rate of the mixer, a power of two
#define SPATIAL_STREAM_MSEC			500		// a voice that got nothing for this long has ended
// How loud a voice is against what the cgame makes of it on its own device, where a voice in front is in both
// ears at full volume. Panning gives each side 0.7 of it, and the HRTF takes 2 dB off noise with the spectrum
// of speech (measured, see docs/japlus/audio.md).
#define SPATIAL_GAIN_VOICE_HRTF		1.25f
#define SPATIAL_GAIN_VOICE_PANNING	1.4f

typedef struct spatialStream_s {
	qboolean	active;
	int			lastWrite;				// Sys_Milliseconds() of the last samples
	vec3_t		origin;
	float		volume;
	float		fifo[SPATIAL_STREAM_FIFO];
	unsigned	read, write;			// counted on and on, the place in fifo is the low bits
	float		last, phase;			// from the rate of the voice to the one of the mixer: where the last call ended
} spatialStream_t;

// One sound in the level. The effects of Steam Audio remember what they did to the block before, so a sound
// keeps its voice for as long as it plays and each voice is rendered once and in order, see "Rendering".
typedef struct spatialVoice_s {
	qboolean	active;
	int			frame;				// spatial.frame of the last S_SpatialAdd*() that named it

	// what it plays
	spatialStream_t	*stream;		// the voice of a player from the voice chat, else NULL
	channel_t	*channel;			// a channel of the mixer, NULL: a loop sound. Only looked into in its frame
	int			entnum;
	sfx_t		*sfx;
	qboolean	loop;
	int			startSample;
	float		step;				// samples of the sound per sample that is heard, above 1: doppler

	// where, and how loud the game wants it at that distance
	vec3_t		origin;
	IPLVector3	direction;			// from the listener, in his axes
	int			entchannel;			// CHAN_*: the kind of sound, which says how it fades with the distance
	float		volume;				// how loud the game plays it, 0 to 1
	float		gain;				//	and what is left of that at its distance in a straight line
	float		renderedGain;		// the gain the last block ended with: the next one moves on from there
	qboolean	voiceVolume;		// s_volumeVoice and not s_volume
	qboolean	sendOnly;			// of the own player: the mixer plays the sound itself, only the reverb gets it from here

	// the thump of an explosion
	qboolean	shock;
	float		shockState[2];		// of its low pass
	float		shockThrough;		// what the walls left of it in the last block

	int			rendered;			// up to which sample of the mixer's time it is in the ring
	qboolean	fresh;				// nothing of it is rendered yet

	// what goes into the reverb of it: the sound as it leaves the walls in its way, by the time of the mixer
	float		dry[SPATIAL_DRY];
	int			dryRead;			// up to which sample the reverb has taken it

	IPLSource				source;
	IPLDirectEffect			direct;
	IPLBinauralEffect		binaural;
	IPLPanningEffect		panning;
	IPLPathEffect			path;
	IPLDirectEffectParams	directParams;
	qboolean				pathing;	// this frame its way round the corners is asked for
} spatialVoice_t;

static cvar_t	*s_spatial;
static cvar_t	*s_spatialHrtf;
static cvar_t	*s_spatialAhead;
static cvar_t	*s_spatialReflections;
static cvar_t	*s_spatialReverb;
static cvar_t	*s_spatialRays;
static cvar_t	*s_spatialBounces;
static cvar_t	*s_spatialPaths;
static cvar_t	*s_spatialPathLoss;
static cvar_t	*s_spatialShock;

static struct {
	void		*library;
	char		libraryPath[MAX_OSPATH];
	qboolean	active;
	float		shockCoeffs[5];		// the low pass of the thump: b0 b1 b2 a1 a2

	IPLContext			context;
	IPLAudioSettings	audio;
	IPLHRTF				hrtf;
	IPLSimulator		simulator;

	// The level. A new one waits in "next" until the thread of the reflections is idle: the simulator must not
	// be given another level while a pass runs.
	IPLScene			scene, nextScene;
	IPLStaticMesh		mesh, nextMesh;
	int					numTriangles, nextTriangles;
	qboolean			sceneWaits;
	int					materialTriangles[SPATIAL_NUM_MATERIALS];
	int					sceneMsec;
	int					sceneNumber;		// counts the levels that were taken over
	IPLVector3			nextMins, nextMaxs;	// the corners of the level, for where to spread probes
	IPLVector3			mins, maxs;
	char				nextPathFile[MAX_QPATH], pathFile[MAX_QPATH];

	// paths
	IPLProbeBatch		probes;				// in the simulator
	IPLProbeBatch		nextProbes;			// waits to be put there, as a level does
	qboolean			probesWait;
	qboolean			bakeWanted;			// this level has no file yet
	int					numProbes;
	int					bakeSeconds;		// what the bake took, 0: the probes came from the file

	// reflections
	IPLSource					reverbSource;		// stands where the listener stands
	IPLReflectionEffect			reverbEffect;
	IPLAmbisonicsDecodeEffect	reverbDecode;
	int							reverbRendered;		// up to which sample of the mixer's time the reverb is in the ring
	SDL_Thread					*worker;
	SDL_sem						*workerWake;
	int							reflectAsked;		// Sys_Milliseconds() of the last pass that was asked for

	// the listener and the sounds of this frame
	int					frame;
	vec3_t				listenerOrigin;
	matrix3_t			listenerAxis;
	spatialVoice_t		voices[SPATIAL_VOICES];

	// finished sound, by the time of the mixer
	float				ring[SPATIAL_RING][2];
	int					ringEnd;			// up to which sample the ring holds what belongs there
	int					lastPaint;

	// for s_spatialInfo
	int					numVoices;
	int					renderUsec, simulateUsec;	// of the last frame
	int					peakRenderUsec;
} spatial;

// The bake of the paths of one level. The thread has the fields to itself while it runs (spatialBakeState is
// BAKE_RUNNING), the game before and after.
static struct {
	SDL_Thread			*thread;
	IPLScene			scene;				// held by the thread for as long as it runs
	IPLVector3			mins, maxs;
	int					sceneNumber;		// the level it is for: by the time it is done there may be another
	IPLProbeBatch		batch;
	IPLSerializedObject	saved;
	int					numProbes;
	int					seconds;
} spatialBake;

static std::atomic<int>	spatialBakeState;		// BAKE_*
static std::atomic<int>	spatialBakeProgress;	// 0 to 100

static spatialStream_t	spatialStreams[MAX_CLIENTS];

// what the game and the thread of the reflections both touch
static std::atomic<int>	spatialWorkerState;		// WORKER_*
static std::atomic<int>	spatialWorkerQuit;
static std::atomic<int>	spatialPassReflections;	// what the pass that is asked for is to work out
static std::atomic<int>	spatialPassPaths;
static std::atomic<int>	spatialReflectUsec;		// what the last pass took
static std::atomic<int>	spatialReflectPasses;

// A point of the game for Steam Audio: metres, x to the right, y up, z to the back. The game has x ahead, y to
// the left and z up. Both are right handed, so a triangle keeps the side it faces.
static void S_SpatialPoint( const vec3_t in, IPLVector3 *out ) {
	out->x = -in[1] * SPATIAL_UNIT;
	out->y = in[2] * SPATIAL_UNIT;
	out->z = -in[0] * SPATIAL_UNIT;
}

// the same for a direction
static void S_SpatialDirection( const vec3_t in, IPLVector3 *out ) {
	out->x = -in[1];
	out->y = in[2];
	out->z = -in[0];
}

static int S_SpatialUsec( void ) {
	return (int)std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now().time_since_epoch() ).count();
}

static void S_SpatialUnload( void ) {
	if ( spatial.library ) {
		Sys_UnloadLibrary( spatial.library );
	}
	spatial.library = NULL;
#define X( name )	q##name = NULL;
	SPATIAL_FUNCTIONS( X )
#undef X
}

// next to the program first: that is where a build puts it. Then wherever the system looks.
static qboolean S_SpatialLoad( void ) {
	const char *missing = NULL;

	Com_sprintf( spatial.libraryPath, sizeof( spatial.libraryPath ), "%s/" SPATIAL_LIBRARY, Sys_BinaryPath() );
	spatial.library = Sys_LoadLibrary( spatial.libraryPath );
	if ( !spatial.library ) {
		Q_strncpyz( spatial.libraryPath, SPATIAL_LIBRARY, sizeof( spatial.libraryPath ) );
		spatial.library = Sys_LoadLibrary( spatial.libraryPath );
	}
	if ( !spatial.library ) {
		Com_Printf( "s_spatial: no " SPATIAL_LIBRARY " next to the program (%s), the sound stays as it is\n", Sys_BinaryPath() );
		return qfalse;
	}
#define X( name )	q##name = (decltype( q##name ))Sys_LoadFunction( spatial.library, #name ); if ( !q##name ) { missing = #name; }
	SPATIAL_FUNCTIONS( X )
#undef X
	if ( missing ) {
		Com_Printf( "s_spatial: %s has no %s, the sound stays as it is\n", spatial.libraryPath, missing );
		S_SpatialUnload();
		return qfalse;
	}
	return qtrue;
}

/*
===============================================================================

THE LEVEL

Steam Audio traces its rays through a mesh of its own, in its own threads. It gets the solid surfaces of the
world once per level (qcommon/cm_export.cpp). Doors and lifts are not in it.

===============================================================================
*/

typedef struct spatialMeshBuild_s {
	IPLVector3	*vertices;
	IPLTriangle	*triangles;
	IPLint32	*materials;
	int			count;
} spatialMeshBuild_t;

static void S_SpatialTriangle( const vec3_t a, const vec3_t b, const vec3_t c, int surfaceFlags, void *data ) {
	spatialMeshBuild_t *build = (spatialMeshBuild_t *)data;
	const int n = build->count++;
	const int material = ( surfaceFlags & SURF_SKY ) ? SPATIAL_SKY : spatialMaterialOf[surfaceFlags & MATERIAL_MASK];

	S_SpatialPoint( a, &build->vertices[n * 3] );
	S_SpatialPoint( b, &build->vertices[n * 3 + 1] );
	S_SpatialPoint( c, &build->vertices[n * 3 + 2] );
	for ( int i = 0; i < 3; i++ ) {
		const IPLVector3 *p = &build->vertices[n * 3 + i];

		if ( !n && !i ) {
			spatial.nextMins = spatial.nextMaxs = *p;
		}
		spatial.nextMins.x = Q_min( spatial.nextMins.x, p->x );
		spatial.nextMins.y = Q_min( spatial.nextMins.y, p->y );
		spatial.nextMins.z = Q_min( spatial.nextMins.z, p->z );
		spatial.nextMaxs.x = Q_max( spatial.nextMaxs.x, p->x );
		spatial.nextMaxs.y = Q_max( spatial.nextMaxs.y, p->y );
		spatial.nextMaxs.z = Q_max( spatial.nextMaxs.z, p->z );
	}
	build->triangles[n].indices[0] = n * 3;
	build->triangles[n].indices[1] = n * 3 + 1;
	build->triangles[n].indices[2] = n * 3 + 2;
	build->materials[n] = material;
	spatial.materialTriangles[material]++;
}

// lets go of a level that is not, or no longer, in the simulator
static void S_SpatialReleaseScene( IPLScene *scene, IPLStaticMesh *mesh ) {
	if ( *mesh ) {
		qiplStaticMeshRelease( mesh );
	}
	if ( *scene ) {
		qiplSceneRelease( scene );
	}
	*mesh = NULL;
	*scene = NULL;
}

static void S_SpatialResetVoice( spatialVoice_t *v );

/*
===============================================================================

THE PATHS OF A LEVEL

===============================================================================
*/

static void IPLCALL S_SpatialBakeProgress( IPLfloat32 progress, void *userData ) {
	spatialBakeProgress.store( (int)( progress * 100.0f ) );
}

// The thread that works the paths of one level out. It touches spatialBake and nothing of the engine.
static int SDLCALL S_SpatialBaker( void *unused ) {
	const int start = S_SpatialUsec();
	IPLProbeGenerationParams generate;
	IPLPathBakeParams bake;
	IPLSerializedObjectSettings serialized;
	IPLProbeArray array = NULL;

	spatialBake.batch = NULL;
	spatialBake.saved = NULL;
	spatialBake.numProbes = 0;

	// probes over every floor of the box the level is in. The matrix makes that box of the cube from 0 to 1.
	memset( &generate, 0, sizeof( generate ) );
	generate.type = IPL_PROBEGENERATIONTYPE_UNIFORMFLOOR;
	generate.spacing = SPATIAL_PROBE_SPACING;
	generate.height = SPATIAL_PROBE_HEIGHT;
	generate.transform.elements[0][0] = spatialBake.maxs.x - spatialBake.mins.x;
	generate.transform.elements[1][1] = spatialBake.maxs.y - spatialBake.mins.y;
	generate.transform.elements[2][2] = spatialBake.maxs.z - spatialBake.mins.z;
	generate.transform.elements[0][3] = spatialBake.mins.x;
	generate.transform.elements[1][3] = spatialBake.mins.y;
	generate.transform.elements[2][3] = spatialBake.mins.z;
	generate.transform.elements[3][3] = 1.0f;

	if ( qiplProbeArrayCreate( spatial.context, &array ) == IPL_STATUS_SUCCESS
		&& qiplProbeBatchCreate( spatial.context, &spatialBake.batch ) == IPL_STATUS_SUCCESS ) {
		qiplProbeArrayGenerateProbes( array, spatialBake.scene, &generate );
		spatialBake.numProbes = qiplProbeArrayGetNumProbes( array );
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
		bake.numThreads = 2;
		if ( spatialBake.numProbes > 1 ) {
			qiplPathBakerBake( spatial.context, &bake, S_SpatialBakeProgress, NULL );
		}

		// as it goes into the file. The game writes it, the file system of the engine is not for this thread.
		memset( &serialized, 0, sizeof( serialized ) );
		if ( qiplSerializedObjectCreate( spatial.context, &serialized, &spatialBake.saved ) == IPL_STATUS_SUCCESS ) {
			qiplProbeBatchSave( spatialBake.batch, spatialBake.saved );
		} else {
			spatialBake.saved = NULL;
		}
	}
	if ( array ) {
		qiplProbeArrayRelease( &array );
	}
	qiplSceneRelease( &spatialBake.scene );
	spatialBake.seconds = ( S_SpatialUsec() - start ) / 1000000;
	spatialBakeState.store( BAKE_DONE );
	return 0;
}

// the probes of this level from its file, if it has one
static qboolean S_SpatialLoadPaths( void ) {
	IPLSerializedObjectSettings serialized;
	IPLSerializedObject object = NULL;
	void *data = NULL;
	const int size = FS_ReadFile( spatial.pathFile, &data );
	qboolean loaded = qfalse;

	if ( size <= 0 || !data ) {
		return qfalse;
	}
	memset( &serialized, 0, sizeof( serialized ) );
	serialized.data = (IPLbyte *)data;
	serialized.size = (IPLsize)size;
	if ( qiplSerializedObjectCreate( spatial.context, &serialized, &object ) == IPL_STATUS_SUCCESS ) {
		if ( qiplProbeBatchLoad( spatial.context, object, &spatial.nextProbes ) == IPL_STATUS_SUCCESS ) {
			qiplProbeBatchCommit( spatial.nextProbes );
			spatial.numProbes = qiplProbeBatchGetNumProbes( spatial.nextProbes );
			spatial.bakeSeconds = 0;
			spatial.probesWait = qtrue;
			loaded = qtrue;
		} else {
			spatial.nextProbes = NULL;
		}
		qiplSerializedObjectRelease( &object );
	}
	FS_FreeFile( data );
	return loaded;
}

/*
===============
S_SpatialThinkPaths

Once a frame. Picks up a bake that is done, starts the one this level waits for, and hands probes that are
ready to the simulator when no pass of the reflections runs. Nothing in here waits.
===============
*/
static void S_SpatialThinkPaths( void ) {
	if ( spatialBake.thread && spatialBakeState.load() == BAKE_DONE ) {
		// the thread is at its end: this does not wait
		SDL_WaitThread( spatialBake.thread, NULL );
		spatialBake.thread = NULL;
		spatialBakeState.store( BAKE_NONE );

		if ( spatialBake.sceneNumber == spatial.sceneNumber && spatialBake.batch && spatialBake.saved && !spatial.sceneWaits ) {
			FS_WriteFile( spatial.pathFile, qiplSerializedObjectGetData( spatialBake.saved ), (int)qiplSerializedObjectGetSize( spatialBake.saved ) );
			spatial.nextProbes = spatialBake.batch;
			spatial.numProbes = spatialBake.numProbes;
			spatial.bakeSeconds = Q_max( 1, spatialBake.seconds );
			spatial.probesWait = qtrue;
			spatialBake.batch = NULL;
			Com_Printf( "s_spatial: the paths of this level are worked out, %i probes in %i s, kept in %s\n", spatial.numProbes,
				spatial.bakeSeconds, spatial.pathFile );
		}
		// a bake for a level that is gone
		if ( spatialBake.batch ) {
			qiplProbeBatchRelease( &spatialBake.batch );
		}
		if ( spatialBake.saved ) {
			qiplSerializedObjectRelease( &spatialBake.saved );
		}
	}

	if ( spatial.bakeWanted && !spatialBake.thread && spatial.scene && !spatial.sceneWaits && s_spatialPaths->integer ) {
		spatial.bakeWanted = qfalse;
		spatialBake.scene = qiplSceneRetain( spatial.scene );
		spatialBake.mins = spatial.mins;
		spatialBake.maxs = spatial.maxs;
		spatialBake.sceneNumber = spatial.sceneNumber;
		spatialBakeProgress.store( 0 );
		spatialBakeState.store( BAKE_RUNNING );
		spatialBake.thread = SDL_CreateThread( S_SpatialBaker, "s_spatialBake", NULL );
		if ( !spatialBake.thread ) {
			qiplSceneRelease( &spatialBake.scene );
			spatialBakeState.store( BAKE_NONE );
		} else {
			Com_Printf( "s_spatial: working out the paths of this level in the background\n" );
		}
	}

	if ( spatial.probesWait && !spatial.sceneWaits && spatialWorkerState.load() == WORKER_IDLE ) {
		qiplSimulatorAddProbeBatch( spatial.simulator, spatial.nextProbes );
		qiplSimulatorCommit( spatial.simulator );
		spatial.probes = spatial.nextProbes;
		spatial.nextProbes = NULL;
		spatial.probesWait = qfalse;
	}
}

/*
===============
S_SpatialTakeScene

Once a frame. A level that waits is given to the simulator when no pass of the reflections runs: that is the
one moment it may be. Until then the level before it stays where it is, and S_SpatialActive() says no.
===============
*/
static void S_SpatialTakeScene( void ) {
	if ( !spatial.sceneWaits || spatialWorkerState.load() != WORKER_IDLE ) {
		return;
	}
	// the probes of the level before go with it, and a bake that still runs for it is told to stop
	if ( spatial.probes ) {
		qiplSimulatorRemoveProbeBatch( spatial.simulator, spatial.probes );
	}
	qiplSimulatorSetScene( spatial.simulator, spatial.nextScene );
	qiplSimulatorCommit( spatial.simulator );
	if ( spatial.probes ) {
		qiplProbeBatchRelease( &spatial.probes );
	}
	if ( spatial.nextProbes ) {
		qiplProbeBatchRelease( &spatial.nextProbes );
	}
	spatial.probesWait = qfalse;
	spatial.numProbes = 0;
	if ( spatialBakeState.load() == BAKE_RUNNING ) {
		qiplPathBakerCancelBake( spatial.context );
	}

	S_SpatialReleaseScene( &spatial.scene, &spatial.mesh );
	spatial.scene = spatial.nextScene;
	spatial.mesh = spatial.nextMesh;
	spatial.numTriangles = spatial.nextTriangles;
	spatial.mins = spatial.nextMins;
	spatial.maxs = spatial.nextMaxs;
	Q_strncpyz( spatial.pathFile, spatial.nextPathFile, sizeof( spatial.pathFile ) );
	spatial.nextScene = NULL;
	spatial.nextMesh = NULL;
	spatial.nextTriangles = 0;
	spatial.sceneWaits = qfalse;
	spatial.sceneNumber++;

	// its paths: from the file of an earlier visit, or worked out now
	spatial.bakeWanted = qfalse;
	if ( spatial.scene && s_spatialPaths->integer && !S_SpatialLoadPaths() ) {
		spatial.bakeWanted = qtrue;
	}

	// nothing of the level before is left to ring on
	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		if ( spatial.voices[i].active ) {
			S_SpatialResetVoice( &spatial.voices[i] );
		}
	}
	memset( spatialStreams, 0, sizeof( spatialStreams ) );
	if ( spatial.reverbEffect ) {
		qiplReflectionEffectReset( spatial.reverbEffect );
		qiplAmbisonicsDecodeEffectReset( spatial.reverbDecode );
	}
}

/*
===============
S_SpatialLoadMap

The collision model of a level was loaded, or the sound system starts while a level is. Runs while the
loading screen is up: building the mesh of a large level takes a moment. The level waits to be taken over,
see S_SpatialTakeScene().
===============
*/
void S_SpatialLoadMap( const char *mapname ) {
	const int start = Sys_Milliseconds();
	spatialMeshBuild_t build;
	IPLSceneSettings sceneSettings;
	IPLStaticMeshSettings meshSettings;
	IPLMaterial materials[SPATIAL_NUM_MATERIALS];
	int count;

	if ( !spatial.active ) {
		return;
	}
	// a level that still waits was never used
	S_SpatialReleaseScene( &spatial.nextScene, &spatial.nextMesh );
	spatial.nextTriangles = 0;
	spatial.sceneWaits = qtrue;
	memset( spatial.materialTriangles, 0, sizeof( spatial.materialTriangles ) );

	count = CM_ExportSolidTriangles( NULL, NULL );
	if ( !count ) {
		return;
	}
	memset( &sceneSettings, 0, sizeof( sceneSettings ) );
	sceneSettings.type = IPL_SCENETYPE_DEFAULT;
	if ( qiplSceneCreate( spatial.context, &sceneSettings, &spatial.nextScene ) != IPL_STATUS_SUCCESS ) {
		Com_Printf( "s_spatial: no scene for the level\n" );
		spatial.nextScene = NULL;
		return;
	}

	memset( &build, 0, sizeof( build ) );
	build.vertices = (IPLVector3 *)Z_Malloc( count * 3 * sizeof( IPLVector3 ), TAG_TEMP_WORKSPACE, qfalse );
	build.triangles = (IPLTriangle *)Z_Malloc( count * sizeof( IPLTriangle ), TAG_TEMP_WORKSPACE, qfalse );
	build.materials = (IPLint32 *)Z_Malloc( count * sizeof( IPLint32 ), TAG_TEMP_WORKSPACE, qfalse );
	CM_ExportSolidTriangles( S_SpatialTriangle, &build );

	// The file of the paths of this level is named after the level and after its walls: another version of a
	// level with the same name has other walls and gets a file of its own.
	{
		char path[MAX_QPATH], name[MAX_QPATH];

		Q_strncpyz( path, mapname, sizeof( path ) );
		COM_StripExtension( COM_SkipPath( path ), name, sizeof( name ) );
		Com_sprintf( spatial.nextPathFile, sizeof( spatial.nextPathFile ), "steamaudio/%s_%08x.paths", name,
			Com_BlockChecksum( build.vertices, build.count * 3 * (int)sizeof( IPLVector3 ) ) );
	}

	// what a side of a wall lets through, see spatialMaterials
	for ( int i = 0; i < SPATIAL_NUM_MATERIALS; i++ ) {
		materials[i] = spatialMaterials[i];
		for ( int k = 0; k < IPL_NUM_BANDS; k++ ) {
			materials[i].transmission[k] = sqrtf( spatialMaterials[i].transmission[k] );
		}
	}

	memset( &meshSettings, 0, sizeof( meshSettings ) );
	meshSettings.numVertices = build.count * 3;
	meshSettings.numTriangles = build.count;
	meshSettings.numMaterials = SPATIAL_NUM_MATERIALS;
	meshSettings.vertices = build.vertices;
	meshSettings.triangles = build.triangles;
	meshSettings.materialIndices = build.materials;
	meshSettings.materials = materials;
	if ( qiplStaticMeshCreate( spatial.nextScene, &meshSettings, &spatial.nextMesh ) == IPL_STATUS_SUCCESS ) {
		qiplStaticMeshAdd( spatial.nextMesh, spatial.nextScene );
		qiplSceneCommit( spatial.nextScene );
		spatial.nextTriangles = build.count;
	} else {
		Com_Printf( "s_spatial: no mesh for the level\n" );
		spatial.nextMesh = NULL;
		S_SpatialReleaseScene( &spatial.nextScene, &spatial.nextMesh );
	}
	// the library keeps a copy
	Z_Free( build.vertices );
	Z_Free( build.triangles );
	Z_Free( build.materials );

	spatial.sceneMsec = Sys_Milliseconds() - start;
	if ( spatial.nextTriangles ) {
		Com_Printf( "s_spatial: %i triangles of the level in %i ms\n", spatial.nextTriangles, spatial.sceneMsec );
	}
}

/*
===============================================================================

THE SOUNDS OF A FRAME

Once a frame snd_dma.cpp names the listener and every sound that has a place in the level: the channels of the
mixer that play one, and the loop sounds. Each finds its voice again or gets a free one.

===============================================================================
*/

static void S_SpatialResetVoice( spatialVoice_t *v ) {
	v->stream = NULL;
	qiplDirectEffectReset( v->direct );
	qiplBinauralEffectReset( v->binaural );
	qiplPanningEffectReset( v->panning );
	qiplPathEffectReset( v->path );
	v->shockState[0] = v->shockState[1] = 0.0f;
	v->active = qfalse;
	v->channel = NULL;
	v->sfx = NULL;
}

static qboolean S_SpatialIsExplosion( sfx_t *sfx ) {
	if ( Q_stristr( COM_SkipPath( sfx->sSoundName ), "explo" ) ) {
		return qtrue;
	}
	for ( size_t i = 0; i < ARRAY_LEN( spatialShockPaths ); i++ ) {
		if ( Q_stristr( sfx->sSoundName, spatialShockPaths[i] ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

void S_SpatialBeginFrame( const vec3_t origin, matrix3_t axis ) {
	spatial.frame++;
	VectorCopy( origin, spatial.listenerOrigin );
	for ( int i = 0; i < 3; i++ ) {
		VectorCopy( axis[i], spatial.listenerAxis[i] );
	}
}

// A channel is the same sound as last frame if it still plays the sample it started then. A loop sound is
// known by its entity and its sample: the mixer makes its loop channels anew every frame.
static spatialVoice_t *S_SpatialFindVoice( spatialStream_t *stream, channel_t *channel, int entnum, sfx_t *sfx, int startSample ) {
	spatialVoice_t *unused = NULL;

	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		spatialVoice_t *v = &spatial.voices[i];

		if ( !v->active ) {
			if ( !unused ) {
				unused = v;
			}
			continue;
		}
		if ( v->stream != stream || v->sfx != sfx ) {
			continue;
		}
		if ( stream || ( channel ? ( !v->loop && v->channel == channel && v->startSample == startSample ) : ( v->loop && v->entnum == entnum ) ) ) {
			return v;
		}
	}
	if ( unused ) {
		unused->active = qtrue;
		unused->fresh = qtrue;
		unused->stream = stream;
		unused->loop = ( channel || stream ) ? qfalse : qtrue;
		unused->entnum = entnum;
		unused->sfx = sfx;
		unused->startSample = startSample;
		unused->renderedGain = 0.0f;
		unused->sendOnly = qfalse;
		unused->shock = channel ? S_SpatialIsExplosion( sfx ) : qfalse;
	}
	return unused;
}

// samples of the sound of a channel per sample that is heard
static float S_SpatialChannelStep( const channel_t *ch ) {
	return ( ch->doppler && ch->dopplerScale > 1 ) ? ch->dopplerScale : 1.0f;
}

static void S_SpatialPlaceVoice( spatialVoice_t *v, channel_t *channel, int entchannel, const vec3_t origin, float volume, float fade,
	float step, qboolean voiceVolume ) {
	vec3_t d;

	v->frame = spatial.frame;
	v->channel = channel;
	v->entchannel = entchannel;
	v->volume = volume;
	v->gain = volume * fade;
	v->step = step;
	v->voiceVolume = voiceVolume;
	VectorCopy( origin, v->origin );

	// where it is from the listener, in his axes: x right, y up, z back. The game has forward, left, up.
	VectorSubtract( origin, spatial.listenerOrigin, d );
	if ( VectorNormalize( d ) < 1.0f ) {
		VectorCopy( spatial.listenerAxis[0], d );
	}
	v->direction.x = -DotProduct( d, spatial.listenerAxis[1] );
	v->direction.y = DotProduct( d, spatial.listenerAxis[2] );
	v->direction.z = -DotProduct( d, spatial.listenerAxis[0] );
}

// qfalse: no voice is free, the mixer plays the channel the old way
qboolean S_SpatialAddChannel( channel_t *ch, const vec3_t origin, float volume, float fade, qboolean voiceVolume ) {
	spatialVoice_t *v = S_SpatialFindVoice( NULL, ch, ch->entnum, ch->thesfx, (int)ch->startSample );

	if ( !v ) {
		return qfalse;
	}
	S_SpatialPlaceVoice( v, ch, ch->entchannel, origin, volume, fade, S_SpatialChannelStep( ch ), voiceVolume );
	v->sendOnly = qfalse;
	return qtrue;
}

// A sound of the own player. The mixer plays it as it always did, at full volume in both ears: from here only
// the room gets it, see S_SpatialRenderBlock().
void S_SpatialAddSend( channel_t *ch, float volume, qboolean voiceVolume ) {
	spatialVoice_t *v;

	if ( !s_spatialReflections->integer ) {
		return;
	}
	v = S_SpatialFindVoice( NULL, ch, ch->entnum, ch->thesfx, (int)ch->startSample );
	if ( v ) {
		S_SpatialPlaceVoice( v, ch, ch->entchannel, spatial.listenerOrigin, volume, 1.0f, S_SpatialChannelStep( ch ), voiceVolume );
		v->sendOnly = qtrue;
	}
}

void S_SpatialAddLoop( int entnum, sfx_t *sfx, const vec3_t origin, float volume, float fade, float step ) {
	spatialVoice_t *v = S_SpatialFindVoice( NULL, NULL, entnum, sfx, 0 );

	if ( v ) {
		S_SpatialPlaceVoice( v, NULL, CHAN_AUTO, origin, volume, fade, step, qfalse );
	}
}

/*
===============================================================================

VOICES OF THE VOICE CHAT

The cgame hands over what it decoded of a player, a frame of 20 ms at a time as it comes in, and says where his
mouth is and how loud he is to be. Here that is a sound in the level like the others, with one difference: it
is not there ahead of time. The other voices are rendered s_spatialAhead ahead of what is heard. A voice of the
chat is rendered as far as what arrived of it reaches, so it is heard that much later than the cgame would
play it itself.

===============================================================================
*/

static int S_SpatialStreamQueued( const spatialStream_t *stream ) {
	return (int)( stream->write - stream->read );
}

/*
===============
S_SpatialVoice

From the cgame, through its engine interface (ext.S_SpatialVoice). samples: 16 bit, one channel, rate of them a
second. With no samples it only says where the player is and how loud. origin NULL: where he was.
Returns how many samples of him wait here, counted at his rate, or -1: the sound of the level does not run, the
cgame plays him itself.
===============
*/
int S_SpatialVoice( int clientNum, const vec3_t origin, float volume, const short *samples, int numSamples, int rate ) {
	spatialStream_t *stream;
	float ratio;

	if ( !S_SpatialActive() || clientNum < 0 || clientNum >= MAX_CLIENTS || rate < 8000 || rate > 48000 ) {
		return -1;
	}
	stream = &spatialStreams[clientNum];
	if ( !stream->active ) {
		memset( stream, 0, sizeof( *stream ) );
		stream->active = qtrue;
	}
	if ( origin ) {
		VectorCopy( origin, stream->origin );
	}
	stream->volume = volume;

	// to the rate of the mixer: a line from each sample to the next
	ratio = (float)rate / spatial.audio.samplingRate;
	if ( samples && numSamples > 0 ) {
		stream->lastWrite = Sys_Milliseconds();
		for ( int i = 0; i < numSamples; i++ ) {
			const float next = samples[i] * ( 1.0f / 32768.0f );

			while ( stream->phase < 1.0f ) {
				// full: the game stood still, or nothing renders. The newest is dropped.
				if ( S_SpatialStreamQueued( stream ) < SPATIAL_STREAM_FIFO ) {
					stream->fifo[stream->write & ( SPATIAL_STREAM_FIFO - 1 )] = stream->last + ( next - stream->last ) * stream->phase;
					stream->write++;
				}
				stream->phase += ratio;
			}
			stream->phase -= 1.0f;
			stream->last = next;
		}
	}
	return (int)( S_SpatialStreamQueued( stream ) * ratio );
}

// once a frame: every voice of the chat that still has something to say is one of the sounds of the frame
static void S_SpatialGatherStreams( void ) {
	const int now = Sys_Milliseconds();

	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		spatialStream_t *stream = &spatialStreams[i];
		spatialVoice_t *v;

		if ( !stream->active ) {
			continue;
		}
		if ( now - stream->lastWrite > SPATIAL_STREAM_MSEC && S_SpatialStreamQueued( stream ) < SPATIAL_FRAME ) {
			stream->active = qfalse;
			continue;
		}
		v = S_SpatialFindVoice( stream, NULL, i, NULL, 0 );
		if ( v ) {
			// CHAN_VOICE_GLOBAL: how loud he is at his distance is in the volume the cgame gave, also for a way
			// round a corner
			S_SpatialPlaceVoice( v, NULL, CHAN_VOICE_GLOBAL, stream->origin, stream->volume, 1.0f, 1.0f, qfalse );
		}
	}
}

/*
===============================================================================

SIMULATION

What the level does to each sound on its straight way to the listener: how much of the source is hidden, and
what the walls in between let through. Cheap, every frame, in the thread of the game.

===============================================================================
*/

// Called by Steam Audio in the thread of S_SpatialWorker(), with the length of a way in metres. What it calls
// touches nothing but its arguments.
static float IPLCALL S_SpatialPathFade( IPLfloat32 distance, void *userData ) {
	const float fade = S_AttenuateDistance( distance * ( 1.0f / SPATIAL_UNIT ), (int)(intptr_t)userData );

	return fade > 0.0f ? fade : 0.0f;
}

static void S_SpatialSimulate( void ) {
	IPLSimulationSharedInputs shared;
	vec3_t right;

	memset( &shared, 0, sizeof( shared ) );
	VectorNegate( spatial.listenerAxis[1], right );
	S_SpatialPoint( spatial.listenerOrigin, &shared.listener.origin );
	S_SpatialDirection( spatial.listenerAxis[0], &shared.listener.ahead );
	S_SpatialDirection( spatial.listenerAxis[2], &shared.listener.up );
	S_SpatialDirection( right, &shared.listener.right );
	// for the reflections, which the thread of S_SpatialWorker() works out from where the listener is now
	shared.numRays = Com_Clampi( 256, SPATIAL_MAX_RAYS, s_spatialRays->integer );
	shared.numBounces = Com_Clampi( 1, 64, s_spatialBounces->integer );
	shared.duration = SPATIAL_REVERB_SECONDS;
	shared.order = SPATIAL_REVERB_ORDER;
	shared.irradianceMinDistance = 1.0f;
	qiplSimulatorSetSharedInputs( spatial.simulator,
		(IPLSimulationFlags)( IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS | IPL_SIMULATIONFLAGS_PATHING ), &shared );

	{
		IPLSimulationInputs inputs;

		memset( &inputs, 0, sizeof( inputs ) );
		if ( s_spatialReflections->integer ) {
			inputs.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
			inputs.source = shared.listener;
			inputs.reverbScale[0] = inputs.reverbScale[1] = inputs.reverbScale[2] = 1.0f;
		}
		qiplSourceSetInputs( spatial.reverbSource, IPL_SIMULATIONFLAGS_REFLECTIONS, &inputs );
	}

	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		spatialVoice_t *v = &spatial.voices[i];
		IPLSimulationInputs inputs;

		// a voice that plays nothing is not looked at, and neither is one of the own player: nothing is in his way
		memset( &inputs, 0, sizeof( inputs ) );
		if ( v->active && v->frame == spatial.frame && !v->sendOnly ) {
			inputs.flags = IPL_SIMULATIONFLAGS_DIRECT;
			inputs.directFlags = (IPLDirectSimulationFlags)( IPL_DIRECTSIMULATIONFLAGS_OCCLUSION | IPL_DIRECTSIMULATIONFLAGS_TRANSMISSION );
			S_SpatialPoint( v->origin, &inputs.source.origin );
			inputs.source.right.x = 1.0f;
			inputs.source.up.y = 1.0f;
			inputs.source.ahead.z = -1.0f;
			inputs.occlusionType = IPL_OCCLUSIONTYPE_VOLUMETRIC;
			inputs.occlusionRadius = SPATIAL_OCCLUSION_RADIUS;
			inputs.numOcclusionSamples = SPATIAL_OCCLUSION_SAMPLES;
			inputs.numTransmissionRays = SPATIAL_TRANSMISSION_RAYS;

			// Its way round the corners, from the probes of the level. How loud it is at the end of that way is
			// what the game makes of a sound at that distance.
			v->pathing = (qboolean)( spatial.probes && s_spatialPaths->integer );
			if ( v->pathing ) {
				inputs.flags = (IPLSimulationFlags)( inputs.flags | IPL_SIMULATIONFLAGS_PATHING );
				inputs.baked = IPL_TRUE;
				inputs.bakedDataIdentifier.type = IPL_BAKEDDATATYPE_PATHING;
				inputs.bakedDataIdentifier.variation = IPL_BAKEDDATAVARIATION_DYNAMIC;
				inputs.pathingProbes = spatial.probes;
				inputs.visRadius = SPATIAL_PATH_RADIUS;
				inputs.visThreshold = SPATIAL_PATH_THRESHOLD;
				inputs.visRange = SPATIAL_PATH_VIS_RANGE;
				inputs.pathingOrder = SPATIAL_REVERB_ORDER;
				inputs.enableValidation = IPL_TRUE;
				inputs.findAlternatePaths = IPL_TRUE;
				inputs.distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_CALLBACK;
				inputs.distanceAttenuationModel.callback = S_SpatialPathFade;
				inputs.distanceAttenuationModel.userData = (void *)(intptr_t)v->entchannel;
			}
		} else {
			v->pathing = qfalse;
		}
		qiplSourceSetInputs( v->source, (IPLSimulationFlags)( IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_PATHING ), &inputs );
	}

	qiplSimulatorRunDirect( spatial.simulator );

	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		spatialVoice_t *v = &spatial.voices[i];
		IPLSimulationOutputs outputs;

		if ( !v->active || v->frame != spatial.frame || v->sendOnly ) {
			continue;
		}
		memset( &outputs, 0, sizeof( outputs ) );
		qiplSourceGetOutputs( v->source, IPL_SIMULATIONFLAGS_DIRECT, &outputs );
		v->directParams = outputs.direct;
		// how loud a sound is at its distance stays what the game makes of it (v->gain)
		v->directParams.flags = (IPLDirectEffectFlags)( IPL_DIRECTEFFECTFLAGS_APPLYOCCLUSION | IPL_DIRECTEFFECTFLAGS_APPLYTRANSMISSION );
		v->directParams.transmissionType = IPL_TRANSMISSIONTYPE_FREQDEPENDENT;
	}
}

/*
===============================================================================

RENDERING

The mixer of the game paints the next fifth of a second again in every frame, so a sound that starts is heard
after 50 ms and a hitch of the game still finds sound in the buffer. It can do that because it keeps nothing
from one sample to the next. The effects of Steam Audio do: each voice has to be rendered once, block after
block. So every voice is rendered ahead on its own, as far as s_spatialAhead says, and added into a ring of
finished sound by the time of the mixer. The mixer adds what the ring has for the samples it paints, as often
as it paints them. A voice that starts adds itself to what the others already put there, so it is heard as
soon as a sound of the old path would be. What a voice is given later (where it is, what is in the way) only
reaches the blocks that are not rendered yet: that is the delay s_spatialAhead stands for.

===============================================================================
*/

// The ring holds sums. A place in it is emptied before the first voice adds to it.
static void S_SpatialRingExtend( int end ) {
	if ( end - spatial.ringEnd > SPATIAL_RING ) {
		spatial.ringEnd = end - SPATIAL_RING;
	}
	for ( int t = spatial.ringEnd; t < end; t++ ) {
		spatial.ring[t & ( SPATIAL_RING - 1 )][0] = 0.0f;
		spatial.ring[t & ( SPATIAL_RING - 1 )][1] = 0.0f;
	}
	if ( end > spatial.ringEnd ) {
		spatial.ringEnd = end;
	}
}

// the samples of a voice for one block from the time c. qfalse: there are none
static qboolean S_SpatialSamples( const spatialVoice_t *v, int c, float *out ) {
	sfx_t *sfx = v->sfx;
	int have = 0;

	if ( v->stream ) {
		// a whole block or nothing: S_SpatialEndFrame() only asks when there is one
		if ( S_SpatialStreamQueued( v->stream ) < SPATIAL_FRAME ) {
			return qfalse;
		}
		for ( int i = 0; i < SPATIAL_FRAME; i++ ) {
			out[i] = v->stream->fifo[( v->stream->read + i ) & ( SPATIAL_STREAM_FIFO - 1 )];
		}
		v->stream->read += SPATIAL_FRAME;
		return qtrue;
	}
	if ( !sfx || !sfx->bInMemory || sfx->iSoundLengthInSamples <= 0 ) {
		return qfalse;
	}
	if ( v->loop ) {
		// round and round
		while ( have < SPATIAL_FRAME ) {
			const int got = S_SampleRun( sfx, NULL, v->step, SPATIAL_FRAME - have, ( c + have ) % sfx->iSoundLengthInSamples, out + have );

			if ( got <= 0 ) {
				break;
			}
			have += got;
		}
	} else if ( c >= v->startSample ) {
		have = S_SampleRun( sfx, v->channel, v->step, SPATIAL_FRAME, c - v->startSample, out );
	}
	if ( have <= 0 ) {
		return qfalse;
	}
	if ( have < SPATIAL_FRAME ) {
		memset( out + have, 0, ( SPATIAL_FRAME - have ) * sizeof( float ) );
	}
	return qtrue;
}

// the next block of a voice into the ring, getting from the gain of its last block to this one
static void S_SpatialRenderBlock( spatialVoice_t *v, float gain ) {
	static float mono[SPATIAL_FRAME], direct[SPATIAL_FRAME], left[SPATIAL_FRAME], right[SPATIAL_FRAME];
	static float whole[SPATIAL_FRAME], pathLeft[SPATIAL_FRAME], pathRight[SPATIAL_FRAME];
	float *monoData = mono, *directData = direct, *stereoData[2] = { left, right };
	float *wholeData = whole, *pathData[2] = { pathLeft, pathRight };
	IPLAudioBuffer in = { 1, SPATIAL_FRAME, &monoData };
	IPLAudioBuffer mid = { 1, SPATIAL_FRAME, &directData };
	IPLAudioBuffer out = { 2, SPATIAL_FRAME, stereoData };
	IPLAudioBuffer pathIn = { 1, SPATIAL_FRAME, &wholeData };
	IPLAudioBuffer pathOut = { 2, SPATIAL_FRAME, pathData };
	const int c = v->rendered;
	const float from = v->renderedGain, step = ( gain - v->renderedGain ) * ( 1.0f / SPATIAL_FRAME );
	// the sound without its fade by the distance in a straight line: a way round a corner has a length of its own
	const float unfade = ( v->gain > 0.001f ) ? v->volume / v->gain : 0.0f;
	const qboolean fresh = v->fresh;
	qboolean pathed = qfalse;
	float volume, hidden, send;

	if ( v->fresh ) {
		v->dryRead = c;
	}
	v->rendered += SPATIAL_FRAME;
	v->renderedGain = gain;
	v->fresh = qfalse;
	if ( !S_SpatialSamples( v, c, mono ) ) {
		for ( int i = 0; i < SPATIAL_FRAME; i++ ) {
			v->dry[( c + i ) & ( SPATIAL_DRY - 1 )] = 0.0f;
		}
		return;
	}
	for ( int i = 0; i < SPATIAL_FRAME; i++ ) {
		mono[i] *= from + step * i;
		whole[i] = mono[i] * unfade;
	}

	// A voice of the chat is as loud as the cgame said, s_volume is not for it. Against the other sounds it is
	// louder by what its two factors differ, which the reverb has to know: it has one factor for all it gets.
	send = v->voiceVolume ? s_volumeVoice->value : s_volume->value;
	if ( v->stream ) {
		send = s_spatialHrtf->integer ? ( SPATIAL_GAIN_VOICE_HRTF / SPATIAL_GAIN_HRTF ) : ( SPATIAL_GAIN_VOICE_PANNING / SPATIAL_GAIN_PANNING );
	}
	if ( v->sendOnly ) {
		// of the own player: the room gets it as the game plays it, the mixer plays the sound itself
		for ( int i = 0; i < SPATIAL_FRAME; i++ ) {
			v->dry[( c + i ) & ( SPATIAL_DRY - 1 )] = mono[i] * send * SPATIAL_OWN_SEND;
		}
		return;
	}

	qiplDirectEffectApply( v->direct, &v->directParams, &in, &mid );

	// The way round the corners, for as much of the sound as the walls hide: what is in sight needs no way
	// round. Steam Audio gives it the direction it arrives from, which is the last corner.
	hidden = 1.0f - v->directParams.occlusion;
	if ( v->pathing && hidden > 0.01f ) {
		IPLSimulationOutputs outputs;
		IPLPathEffectParams params;
		vec3_t rightAxis;

		memset( &outputs, 0, sizeof( outputs ) );
		qiplSourceGetOutputs( v->source, IPL_SIMULATIONFLAGS_PATHING, &outputs );
		params = outputs.pathing;
		if ( params.shCoeffs ) {
			params.order = SPATIAL_REVERB_ORDER;
			params.binaural = s_spatialHrtf->integer ? IPL_TRUE : IPL_FALSE;
			params.hrtf = spatial.hrtf;
			// what bending round the corners costs: all of it, or only how much duller the sound gets
			params.normalizeEQ = s_spatialPathLoss->integer ? IPL_FALSE : IPL_TRUE;
			VectorNegate( spatial.listenerAxis[1], rightAxis );
			S_SpatialPoint( spatial.listenerOrigin, &params.listener.origin );
			S_SpatialDirection( spatial.listenerAxis[0], &params.listener.ahead );
			S_SpatialDirection( spatial.listenerAxis[2], &params.listener.up );
			S_SpatialDirection( rightAxis, &params.listener.right );
			qiplPathEffectApply( v->path, &params, &pathIn, &pathOut );
			pathed = qtrue;
		}
	}

	// what the walls in the way left of it, and what came round them, is what the room around the listener
	// gets to hear
	for ( int i = 0; i < SPATIAL_FRAME; i++ ) {
		v->dry[( c + i ) & ( SPATIAL_DRY - 1 )] = ( direct[i] + ( pathed ? ( pathLeft[i] + pathRight[i] ) * 0.5f * hidden : 0.0f ) ) * send;
	}
	volume = ( v->stream ? 1.0f : send ) * ( 32768.0f * 256.0f );
	if ( s_spatialHrtf->integer ) {
		IPLBinauralEffectParams params;

		memset( &params, 0, sizeof( params ) );
		params.direction = v->direction;
		params.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
		params.spatialBlend = 1.0f;
		params.hrtf = spatial.hrtf;
		qiplBinauralEffectApply( v->binaural, &params, &mid, &out );
		volume *= v->stream ? SPATIAL_GAIN_VOICE_HRTF : SPATIAL_GAIN_HRTF;
	} else {
		IPLPanningEffectParams params;

		params.direction = v->direction;
		qiplPanningEffectApply( v->panning, &params, &mid, &out );
		volume *= v->stream ? SPATIAL_GAIN_VOICE_PANNING : SPATIAL_GAIN_PANNING;
	}

	// into the ring, in what the paint buffer counts in: 16 bit samples times 256
	S_SpatialRingExtend( c + SPATIAL_FRAME );
	for ( int i = 0; i < SPATIAL_FRAME; i++ ) {
		float *slot = spatial.ring[( c + i ) & ( SPATIAL_RING - 1 )];

		slot[0] += left[i] * volume;
		slot[1] += right[i] * volume;
	}
	if ( pathed ) {
		for ( int i = 0; i < SPATIAL_FRAME; i++ ) {
			float *slot = spatial.ring[( c + i ) & ( SPATIAL_RING - 1 )];

			slot[0] += pathLeft[i] * hidden * volume;
			slot[1] += pathRight[i] * hidden * volume;
		}
	}

	// The thump of an explosion: its lows once more, for both ears alike. A wall takes from them what Steam
	// Audio takes from the lows of any sound behind it, and the block gets there from what the last one had.
	if ( v->shock && s_spatialShock->value > 0.0f ) {
		const float *k = spatial.shockCoeffs;
		const float through = v->directParams.occlusion + ( 1.0f - v->directParams.occlusion ) * v->directParams.transmission[0];
		const float throughFrom = fresh ? through : v->shockThrough;
		const float throughStep = ( through - throughFrom ) * ( 1.0f / SPATIAL_FRAME );
		const float shock = send * s_spatialShock->value * SPATIAL_SHOCK_GAIN * ( 32768.0f * 256.0f );
		float z1 = v->shockState[0], z2 = v->shockState[1];

		for ( int i = 0; i < SPATIAL_FRAME; i++ ) {
			float *slot = spatial.ring[( c + i ) & ( SPATIAL_RING - 1 )];
			const float low = k[0] * mono[i] + z1;
			const float add = low * ( throughFrom + throughStep * i ) * shock;

			z1 = k[1] * mono[i] - k[3] * low + z2;
			z2 = k[2] * mono[i] - k[4] * low;
			slot[0] += add;
			slot[1] += add;
		}
		v->shockState[0] = z1;
		v->shockState[1] = z2;
		v->shockThrough = through;
	}
}

/*
===============================================================================

REFLECTIONS

One source stands where the listener stands. In a thread of its own Steam Audio follows rays from there through
the level (s_spatialRays of them, s_spatialBounces times off a wall) and makes of what comes back the impulse
response of the place: how the room rings. Every sound in the level is sent through that one reverb, as it
arrives after the walls in its way. A pass takes longer than a frame, so the game only asks for one and picks
up what the last one found.

The reverb is rendered block after block behind the voices, as far as all of them are rendered, into the same
ring.

===============================================================================
*/

static int SDLCALL S_SpatialWorker( void *unused ) {
	for ( ;; ) {
		SDL_SemWait( spatial.workerWake );
		if ( spatialWorkerQuit.load() ) {
			break;
		}
		if ( spatialWorkerState.load() == WORKER_ASKED ) {
			const int start = S_SpatialUsec();

			spatialWorkerState.store( WORKER_RUNNING );
			if ( spatialPassReflections.load() ) {
				qiplSimulatorRunReflections( spatial.simulator );
			}
			if ( spatialPassPaths.load() ) {
				qiplSimulatorRunPathing( spatial.simulator );
			}
			spatialReflectUsec.store( S_SpatialUsec() - start );
			spatialReflectPasses.fetch_add( 1 );
			spatialWorkerState.store( WORKER_IDLE );
		}
	}
	return 0;
}

// The next pass of the thread, if the last one is done and not too long ago: the reflections around the
// listener, and the ways of the sounds round the corners. Never while a level waits to be taken over.
static void S_SpatialAskPass( void ) {
	const int now = Sys_Milliseconds();
	const qboolean paths = (qboolean)( spatial.probes && s_spatialPaths->integer );
	int idle = WORKER_IDLE;

	if ( ( !s_spatialReflections->integer && !paths ) || spatial.sceneWaits || now - spatial.reflectAsked < SPATIAL_REFLECT_MSEC ) {
		return;
	}
	// the thread reads these two only after it was woken
	if ( spatialWorkerState.load() == WORKER_IDLE ) {
		spatialPassReflections.store( s_spatialReflections->integer ? 1 : 0 );
		spatialPassPaths.store( paths ? 1 : 0 );
		if ( spatialWorkerState.compare_exchange_strong( idle, WORKER_ASKED ) ) {
			spatial.reflectAsked = now;
			SDL_SemPost( spatial.workerWake );
		}
	}
}

// one block of the reverb from the time c into the ring
static void S_SpatialRenderReverb( int c ) {
	static float in[SPATIAL_FRAME], ambisonics[SPATIAL_REVERB_CHANNELS][SPATIAL_FRAME], left[SPATIAL_FRAME], right[SPATIAL_FRAME];
	float *inData = in, *ambisonicsData[SPATIAL_REVERB_CHANNELS], *stereoData[2] = { left, right };
	IPLAudioBuffer inBuffer = { 1, SPATIAL_FRAME, &inData };
	IPLAudioBuffer ambisonicsBuffer = { SPATIAL_REVERB_CHANNELS, SPATIAL_FRAME, ambisonicsData };
	IPLAudioBuffer out = { 2, SPATIAL_FRAME, stereoData };
	IPLSimulationOutputs outputs;
	IPLReflectionEffectParams params;
	IPLAmbisonicsDecodeEffectParams decode;
	vec3_t rightAxis;
	float volume;

	// what the last finished pass found. Before the first one there is nothing to ring.
	memset( &outputs, 0, sizeof( outputs ) );
	qiplSourceGetOutputs( spatial.reverbSource, IPL_SIMULATIONFLAGS_REFLECTIONS, &outputs );
	params = outputs.reflections;
	if ( !params.ir ) {
		return;
	}
	params.type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
	params.numChannels = SPATIAL_REVERB_CHANNELS;
	params.irSize = (int)( SPATIAL_REVERB_SECONDS * spatial.audio.samplingRate );

	// Every voice gives the next block of what it has rendered, from its first sample on. A sound that starts is
	// put into the ring where the mixer paints, which is behind where the reverb is by then: its start would
	// never get here if the reverb took the voices by the time of the mixer. So the reverb of a sound comes a few
	// blocks late (up to s_spatialAhead), which a room does to a sound anyway.
	memset( in, 0, sizeof( in ) );
	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		spatialVoice_t *v = &spatial.voices[i];
		int count;

		if ( !v->active || v->fresh ) {
			continue;
		}
		if ( v->rendered - v->dryRead > SPATIAL_DRY ) {
			v->dryRead = v->rendered - SPATIAL_DRY;
		}
		count = Q_min( SPATIAL_FRAME, v->rendered - v->dryRead );
		for ( int k = 0; k < count; k++ ) {
			in[k] += v->dry[( v->dryRead + k ) & ( SPATIAL_DRY - 1 )];
		}
		v->dryRead += count;
	}

	for ( int i = 0; i < SPATIAL_REVERB_CHANNELS; i++ ) {
		ambisonicsData[i] = ambisonics[i];
	}
	qiplReflectionEffectApply( spatial.reverbEffect, &params, &inBuffer, &ambisonicsBuffer, NULL );

	// what comes back comes from all sides of the room: turned to how the listener stands in it
	memset( &decode, 0, sizeof( decode ) );
	decode.order = SPATIAL_REVERB_ORDER;
	decode.hrtf = spatial.hrtf;
	decode.binaural = s_spatialHrtf->integer ? IPL_TRUE : IPL_FALSE;
	VectorNegate( spatial.listenerAxis[1], rightAxis );
	S_SpatialDirection( spatial.listenerAxis[0], &decode.orientation.ahead );
	S_SpatialDirection( spatial.listenerAxis[2], &decode.orientation.up );
	S_SpatialDirection( rightAxis, &decode.orientation.right );
	qiplAmbisonicsDecodeEffectApply( spatial.reverbDecode, &decode, &ambisonicsBuffer, &out );

	S_SpatialRingExtend( c + SPATIAL_FRAME );
	// how loud each voice is went in with what it gave
	volume = s_spatialReverb->value * ( s_spatialHrtf->integer ? SPATIAL_GAIN_HRTF : SPATIAL_GAIN_PANNING ) * ( 32768.0f * 256.0f );
	for ( int i = 0; i < SPATIAL_FRAME; i++ ) {
		float *slot = spatial.ring[( c + i ) & ( SPATIAL_RING - 1 )];

		slot[0] += left[i] * volume;
		slot[1] += right[i] * volume;
	}
}

/*
===============
S_SpatialEndFrame

All sounds of the frame are named. from: the first sample the mixer paints now, target: up to which sample the
voices have to be in the ring.
===============
*/
void S_SpatialEndFrame( int from, int target ) {
	const int start = S_SpatialUsec();
	int simulated;

	int limit = target;

	// the time of the mixer went back (the sound was stopped and started again): everything from the start
	if ( from < spatial.lastPaint ) {
		for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
			if ( spatial.voices[i].active ) {
				S_SpatialResetVoice( &spatial.voices[i] );
			}
		}
		qiplReflectionEffectReset( spatial.reverbEffect );
		qiplAmbisonicsDecodeEffectReset( spatial.reverbDecode );
		spatial.reverbRendered = from;
		spatial.ringEnd = from;
	}
	spatial.lastPaint = from;
	if ( spatial.ringEnd < from ) {
		spatial.ringEnd = from;
	}

	S_SpatialGatherStreams();
	S_SpatialSimulate();
	simulated = S_SpatialUsec();

	spatial.numVoices = 0;
	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		spatialVoice_t *v = &spatial.voices[i];

		if ( !v->active ) {
			continue;
		}
		if ( v->frame != spatial.frame ) {
			// The game no longer names it. What is in the ring of it plays on, one more block lets it fade, so
			// that a sound that was cut off does not end in a click. Its channel may be another sound by now.
			v->channel = NULL;
			if ( !v->fresh && v->rendered >= from ) {
				S_SpatialRenderBlock( v, 0.0f );
			}
			continue;
		}
		spatial.numVoices++;
		if ( v->stream ) {
			// A voice of the chat: not there ahead of time. It starts a block short of where the others are
			// rendered to, and goes on as whole blocks of it arrive. If it runs dry it starts there again.
			if ( v->fresh || v->rendered < from ) {
				v->rendered = Q_max( from, target - SPATIAL_FRAME );
				v->dryRead = v->rendered;
				if ( v->fresh ) {
					v->renderedGain = v->gain;
				}
			}
			while ( v->rendered < target + SPATIAL_FRAME && S_SpatialStreamQueued( v->stream ) >= SPATIAL_FRAME ) {
				S_SpatialRenderBlock( v, v->gain );
			}
			continue;
		}
		if ( v->fresh ) {
			// A sound that starts now starts at full volume with its first sample. One that is found in the
			// middle (a loop sound that comes into reach) fades in.
			if ( !v->loop && v->startSample >= from ) {
				v->rendered = v->startSample;
				v->renderedGain = v->gain;
			} else {
				v->rendered = from;
			}
		} else if ( v->rendered < from ) {
			// the game stood still for longer than was rendered ahead
			v->rendered = from;
			v->dryRead = from;
		}
		while ( v->rendered < target ) {
			S_SpatialRenderBlock( v, v->gain );
		}
		if ( v->rendered < limit ) {
			limit = v->rendered;
		}
	}

	// the reverb, up to where every voice that plays is rendered, and the next pass of the rays
	if ( s_spatialReflections->integer ) {
		if ( spatial.reverbRendered < from ) {
			spatial.reverbRendered = from;
		}
		while ( spatial.reverbRendered + SPATIAL_FRAME <= limit ) {
			S_SpatialRenderReverb( spatial.reverbRendered );
			spatial.reverbRendered += SPATIAL_FRAME;
		}
	}
	S_SpatialAskPass();

	// the voices that ended have given the reverb what they had
	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		spatialVoice_t *v = &spatial.voices[i];

		if ( v->active && v->frame != spatial.frame ) {
			S_SpatialResetVoice( v );
		}
	}

	spatial.simulateUsec = simulated - start;
	spatial.renderUsec = S_SpatialUsec() - simulated;
	if ( spatial.renderUsec > spatial.peakRenderUsec ) {
		spatial.peakRenderUsec = spatial.renderUsec;
	}
}

// the mixer paints the samples from to to: what the voices have for them is added
void S_SpatialPaint( int from, int to, portable_samplepair_t *paint ) {
	if ( to > spatial.ringEnd ) {
		to = spatial.ringEnd;
	}
	for ( int t = from; t < to; t++ ) {
		const float *slot = spatial.ring[t & ( SPATIAL_RING - 1 )];

		paint[t - from].left += (int)slot[0];
		paint[t - from].right += (int)slot[1];
	}
}

// how far ahead of what is heard the voices are rendered, in seconds
float S_SpatialAhead( void ) {
	return Com_Clamp( 0.08f, 0.5f, s_spatialAhead->value );
}

static void S_SpatialInfo_f( void ) {
	if ( !s_spatial->integer ) {
		Com_Printf( "s_spatial is 0 (set it to 1, then snd_restart)\n" );
		return;
	}
	if ( !spatial.active ) {
		Com_Printf( "s_spatial is 1, but Steam Audio did not start, see the lines of the sound initialization\n" );
		return;
	}
	Com_Printf( "Steam Audio %i.%i.%i from %s\n", STEAMAUDIO_VERSION_MAJOR, STEAMAUDIO_VERSION_MINOR, STEAMAUDIO_VERSION_PATCH,
		spatial.libraryPath );
	Com_Printf( "%i samples a second, blocks of %i, direction for %s\n", spatial.audio.samplingRate, spatial.audio.frameSize,
		s_spatialHrtf->integer ? "headphones (HRTF)" : "speakers (panning)" );
	if ( spatial.sceneWaits ) {
		Com_Printf( "a level waits to be taken over\n" );
		return;
	}
	if ( !spatial.numTriangles ) {
		Com_Printf( "no level\n" );
		return;
	}
	Com_Printf( "level: %i triangles, built in %i ms\n", spatial.numTriangles, spatial.sceneMsec );
	for ( int i = 0; i < SPATIAL_NUM_MATERIALS; i++ ) {
		if ( spatial.materialTriangles[i] ) {
			Com_Printf( "  %-9s %i\n", spatialMaterialNames[i], spatial.materialTriangles[i] );
		}
	}
	Com_Printf( "%i sounds in the level, rendered %i ms ahead. Last frame: %i us for what is in the way, %i us to render (most so far: %i us)\n",
		spatial.numVoices, (int)( S_SpatialAhead() * 1000.0f ), spatial.simulateUsec, spatial.renderUsec, spatial.peakRenderUsec );
	spatial.peakRenderUsec = 0;
	if ( s_spatialReflections->integer ) {
		Com_Printf( "reflections: %i rays, %i bounces, the last pass took %i ms, %i passes so far\n", Com_Clampi( 256, SPATIAL_MAX_RAYS, s_spatialRays->integer ),
			Com_Clampi( 1, 64, s_spatialBounces->integer ), spatialReflectUsec.load() / 1000, spatialReflectPasses.load() );
	} else {
		Com_Printf( "reflections: off (s_spatialReflections 0)\n" );
	}
	if ( !s_spatialPaths->integer ) {
		Com_Printf( "paths: off (s_spatialPaths 0)\n" );
	} else if ( spatialBakeState.load() == BAKE_RUNNING && spatialBake.sceneNumber == spatial.sceneNumber ) {
		Com_Printf( "paths: being worked out for this level, %i %%\n", spatialBakeProgress.load() );
	} else if ( spatial.probes ) {
		if ( spatial.bakeSeconds ) {
			Com_Printf( "paths: %i probes, worked out in %i s, kept in %s\n", spatial.numProbes, spatial.bakeSeconds, spatial.pathFile );
		} else {
			Com_Printf( "paths: %i probes from %s\n", spatial.numProbes, spatial.pathFile );
		}
	} else {
		Com_Printf( "paths: none yet\n" );
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
		Com_Printf( "  %-40s at %.0f %.0f %.0f, %5.0f units away, gain %.2f, %3.0f %% in sight, through what hides it: %.3f %.3f %.3f%s\n",
			v->stream ? va( "voice of player %i", (int)( v->stream - spatialStreams ) ) : v->sfx->sSoundName,
			v->origin[0], v->origin[1], v->origin[2], Distance( v->origin, spatial.listenerOrigin ), v->gain, v->directParams.occlusion * 100.0f,
			v->directParams.transmission[0], v->directParams.transmission[1], v->directParams.transmission[2],
			( v->shock && s_spatialShock->value > 0.0f ) ? ", thump" : "" );
	}
}

// lets go of everything of Steam Audio, in the order it depends on each other, and of the library
static void S_SpatialRelease( void ) {
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

	// a bake is told to stop and waited for the same way. What it had is thrown away.
	if ( spatialBake.thread ) {
		qiplPathBakerCancelBake( spatial.context );
		SDL_WaitThread( spatialBake.thread, NULL );
		spatialBake.thread = NULL;
		if ( spatialBake.batch ) {
			qiplProbeBatchRelease( &spatialBake.batch );
		}
		if ( spatialBake.saved ) {
			qiplSerializedObjectRelease( &spatialBake.saved );
		}
	}
	spatialBakeState.store( BAKE_NONE );

	if ( spatial.library ) {
		if ( spatial.simulator ) {
			if ( spatial.probes ) {
				qiplSimulatorRemoveProbeBatch( spatial.simulator, spatial.probes );
			}
			qiplSimulatorSetScene( spatial.simulator, NULL );
			qiplSimulatorCommit( spatial.simulator );
		}
		if ( spatial.probes ) {
			qiplProbeBatchRelease( &spatial.probes );
		}
		if ( spatial.nextProbes ) {
			qiplProbeBatchRelease( &spatial.nextProbes );
		}
		S_SpatialReleaseScene( &spatial.scene, &spatial.mesh );
		S_SpatialReleaseScene( &spatial.nextScene, &spatial.nextMesh );
		if ( spatial.reverbDecode ) {
			qiplAmbisonicsDecodeEffectRelease( &spatial.reverbDecode );
		}
		if ( spatial.reverbEffect ) {
			qiplReflectionEffectRelease( &spatial.reverbEffect );
		}
		if ( spatial.reverbSource ) {
			qiplSourceRelease( &spatial.reverbSource );
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
	if ( spatial.context ) {
		qiplContextRelease( &spatial.context );
	}
	S_SpatialUnload();
	memset( &spatial, 0, sizeof( spatial ) );
}

// the simulator, and for every voice its source in it and its effects: all there is of them, made once
static qboolean S_SpatialCreateVoices( void ) {
	IPLSimulationSettings simulation;
	IPLSourceSettings sourceSettings;
	IPLDirectEffectSettings directSettings;
	IPLBinauralEffectSettings binauralSettings;
	IPLPanningEffectSettings panningSettings;
	IPLReflectionEffectSettings reflectionSettings;
	IPLAmbisonicsDecodeEffectSettings decodeSettings;
	IPLPathEffectSettings pathSettings;

	memset( &simulation, 0, sizeof( simulation ) );
	simulation.flags = (IPLSimulationFlags)( IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS | IPL_SIMULATIONFLAGS_PATHING );
	simulation.sceneType = IPL_SCENETYPE_DEFAULT;
	simulation.reflectionType = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
	simulation.maxNumOcclusionSamples = SPATIAL_OCCLUSION_SAMPLES;
	simulation.maxNumRays = SPATIAL_MAX_RAYS;
	simulation.numDiffuseSamples = SPATIAL_DIFFUSE_SAMPLES;
	simulation.maxDuration = SPATIAL_REVERB_SECONDS;
	simulation.maxOrder = SPATIAL_REVERB_ORDER;
	simulation.maxNumSources = SPATIAL_VOICES + 1;
	// threads Steam Audio shares a pass of the rays between, inside the one of S_SpatialWorker()
	simulation.numThreads = 2;
	simulation.numVisSamples = 4;
	simulation.samplingRate = spatial.audio.samplingRate;
	simulation.frameSize = spatial.audio.frameSize;
	if ( qiplSimulatorCreate( spatial.context, &simulation, &spatial.simulator ) != IPL_STATUS_SUCCESS ) {
		spatial.simulator = NULL;
		return qfalse;
	}

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
	qiplSimulatorCommit( spatial.simulator );

	spatial.workerWake = SDL_CreateSemaphore( 0 );
	spatial.worker = spatial.workerWake ? SDL_CreateThread( S_SpatialWorker, "s_spatial", NULL ) : NULL;
	return (qboolean)( spatial.worker != NULL );
}

/*
===============
S_SpatialInit

s_spatial is latched: what it is when the sound system starts holds until the next snd_restart
===============
*/
void S_SpatialInit( void ) {
	IPLContextSettings contextSettings;
	IPLHRTFSettings hrtfSettings;

	s_spatial = Cvar_Get( "s_spatial", "0", CVAR_ARCHIVE | CVAR_LATCH, "Sound that knows the level, with Steam Audio (snd_restart)" );
	s_spatialHrtf = Cvar_Get( "s_spatialHrtf", "1", CVAR_ARCHIVE, "s_spatial: 1 direction for headphones (HRTF), 0 for speakers" );
	s_spatialAhead = Cvar_Get( "s_spatialAhead", "0.1", CVAR_ARCHIVE, "s_spatial: seconds of sound rendered ahead. Less follows a turn of the head sooner, more survives a longer hitch" );
	s_spatialReflections = Cvar_Get( "s_spatialReflections", "1", CVAR_ARCHIVE, "s_spatial: 1 the room around the listener rings, worked out with rays. 0 no reverb" );
	s_spatialReverb = Cvar_Get( "s_spatialReverb", "1", CVAR_ARCHIVE, "s_spatial: how loud the reverb is" );
	s_spatialRays = Cvar_Get( "s_spatialRays", "2048", CVAR_ARCHIVE, "s_spatial: rays of a pass of the reflections, 256 to 8192" );
	s_spatialBounces = Cvar_Get( "s_spatialBounces", "16", CVAR_ARCHIVE, "s_spatial: how often a ray is followed off a wall" );
	s_spatialPaths = Cvar_Get( "s_spatialPaths", "1", CVAR_ARCHIVE, "s_spatial: 1 a sound comes round corners. Worked out once per level in the background" );
	s_spatialPathLoss = Cvar_Get( "s_spatialPathLoss", "1", CVAR_ARCHIVE, "s_spatial: 1 a sound loses on its way round a corner what Steam Audio works out. 0 it only gets duller, and is as loud as the game makes a sound that far away" );
	s_spatialShock = Cvar_Get( "s_spatialShock", "1", CVAR_ARCHIVE, "s_spatial: how strong the thump of an explosion is, its lows in both ears. 0 none" );
	Cmd_AddCommand( "s_spatialInfo", S_SpatialInfo_f, "What s_spatial is doing" );

	memset( &spatial, 0, sizeof( spatial ) );
	memset( spatialStreams, 0, sizeof( spatialStreams ) );
	Cvar_Get( "s_spatialVoice", "0", CVAR_ROM, "1: this engine plays the voices of the voice chat as sounds in the level (s_spatial)" );
	Cvar_Set( "s_spatialVoice", "0" );
	if ( !s_spatial->integer ) {
		return;
	}
	// Two channels, and not the 22 kHz the game can also run at: the direction of a sound for headphones is
	// in what is above 11 kHz to a good part, and at that rate there is nothing up there
	if ( dma.channels != 2 || dma.speed < 44100 ) {
		Com_Printf( "s_spatial: needs stereo at 44 kHz or more (s_khz 44), the sound stays as it is\n" );
		return;
	}
	if ( !S_SpatialLoad() ) {
		return;
	}

	// the version of the header: the library refuses one it can not serve
	memset( &contextSettings, 0, sizeof( contextSettings ) );
	contextSettings.version = STEAMAUDIO_VERSION;
	contextSettings.simdLevel = IPL_SIMDLEVEL_AVX2;
	if ( qiplContextCreate( &contextSettings, &spatial.context ) != IPL_STATUS_SUCCESS ) {
		Com_Printf( "s_spatial: %s does not go with version %i.%i.%i of the header, the sound stays as it is\n", spatial.libraryPath,
			STEAMAUDIO_VERSION_MAJOR, STEAMAUDIO_VERSION_MINOR, STEAMAUDIO_VERSION_PATCH );
		S_SpatialUnload();
		return;
	}

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
		Com_Printf( "s_spatial: no direction data (HRTF) for %i samples a second, the sound stays as it is\n", dma.speed );
		qiplContextRelease( &spatial.context );
		S_SpatialUnload();
		return;
	}

	if ( !S_SpatialCreateVoices() ) {
		Com_Printf( "s_spatial: Steam Audio gives no simulator or no effects, the sound stays as it is\n" );
		S_SpatialRelease();
		return;
	}

	spatial.active = qtrue;
	// tells the cgame that this engine takes the voices of its voice chat (ext.S_SpatialVoice)
	Cvar_Set( "s_spatialVoice", "1" );
	// the sound system was started again in the middle of a level
	S_SpatialLoadMap( Cvar_VariableString( "mapname" ) );
	S_SpatialInfo_f();
}

void S_SpatialShutdown( void ) {
	Cmd_RemoveCommand( "s_spatialInfo" );
	Cvar_Set( "s_spatialVoice", "0" );
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
		S_SpatialTakeScene();
		S_SpatialThinkPaths();
	}
}

#else // JP_SPATIAL

// built without it (BuildJPSpatialAudio off, or the submodule lib/steamaudio is not checked out)
void		S_SpatialInit( void ) {}
void		S_SpatialShutdown( void ) {}
qboolean	S_SpatialActive( void ) { return qfalse; }
void		S_SpatialLoadMap( const char *mapname ) {}
void		S_SpatialUpdate( void ) {}
void		S_SpatialBeginFrame( const vec3_t origin, matrix3_t axis ) {}
qboolean	S_SpatialAddChannel( channel_t *ch, const vec3_t origin, float volume, float fade, qboolean voiceVolume ) { return qfalse; }
void		S_SpatialAddSend( channel_t *ch, float volume, qboolean voiceVolume ) {}
void		S_SpatialAddLoop( int entnum, sfx_t *sfx, const vec3_t origin, float volume, float fade, float step ) {}
void		S_SpatialEndFrame( int from, int target ) {}
void		S_SpatialPaint( int from, int to, portable_samplepair_t *paint ) {}
float		S_SpatialAhead( void ) { return 0.0f; }
int			S_SpatialVoice( int clientNum, const vec3_t origin, float volume, const short *samples, int numSamples, int rate ) { return -1; }

#endif // JP_SPATIAL
