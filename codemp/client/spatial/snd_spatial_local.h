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

#pragma once

// snd_spatial_local.h -- what the files of the sound with Steam Audio share (snd_spatial*.cpp): the functions
// of the library, the numbers, the state and the functions they call of each other. See snd_spatial.cpp.

#include "client/snd_local.h"
#include "snd_spatial.h"
#include "qcommon/spatial_debug.h"	// also for the stub of S_SpatialDebug when built without Steam Audio

#ifdef JP_SPATIAL

#include "sys/sys_loadlib.h"
#include "qcommon/cm_public.h"
#include <phonon.h>
#include <SDL.h>
#include <atomic>
#include <chrono>
#ifdef __EMSCRIPTEN__
#include <phonon_slices.h>	// lib/steamaudio-wasm: a pass of the reflections that returns when its time is up
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
	X( iplContextRetain ) \
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
	X( iplReflectionEffectGetTail ) \
	X( iplReflectionMixerCreate ) \
	X( iplReflectionMixerRelease ) \
	X( iplReflectionMixerReset ) \
	X( iplReflectionMixerApply ) \
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
	X( iplPathEffectApply ) \
	X( iplInstancedMeshCreate ) \
	X( iplInstancedMeshRelease ) \
	X( iplInstancedMeshAdd ) \
	X( iplInstancedMeshRemove ) \
	X( iplInstancedMeshUpdateTransform ) \
	X( iplSceneSaveOBJ ) \
	X( iplReflectionsBakerBake ) \
	SPATIAL_SLICE_FUNCTIONS( X )

// what the library for a browser has on top of Valve's, see phonon_slices.h
#ifdef __EMSCRIPTEN__
#define SPATIAL_SLICE_FUNCTIONS( X ) \
	X( iplxSetSliceBudget ) \
	X( iplxSlicePending )
#else
#define SPATIAL_SLICE_FUNCTIONS( X )
#endif

#define X( name )	extern decltype( &name ) q##name;
SPATIAL_FUNCTIONS( X )
#undef X

// Intel's ray tracer, of a desktop library: the rays are traced several times as fast as by Steam Audio's own. A
// library without it still serves, with its own (spatial.sceneType).
extern decltype( &iplEmbreeDeviceCreate )	qiplEmbreeDeviceCreate;
extern decltype( &iplEmbreeDeviceRelease )	qiplEmbreeDeviceRelease;

// Steam Audio counts in metres. A unit of the game is taken as an inch: the player is 64 units tall.
#define SPATIAL_UNIT	0.0254f

// The surfaces of a level say what they are made of (MATERIAL_* in game/surfaceflags.h, for footsteps and
// impacts). How much sound each one swallows, scatters and lets through are the numbers Valve gives for a
// few standard materials (phonon.h, IPLMaterial). Which material of the game goes with which of them is a
// choice made here. The last entry is the sky: what reaches it is gone, open air gives no echo.
//
// A wall of a level is a brush, and a sound that goes through it crosses two of its sides. Steam Audio takes the
// square root of what all the sides it crossed let through when there are more than one
// (DirectSimulator::transmission), so Valve's numbers are for a wall and go into the mesh as they are. Until
// 2026-10-17 the mesh had their square root, and a wall let through the square root of the number.
enum {
	SPATIAL_GENERIC, SPATIAL_BRICK, SPATIAL_CONCRETE, SPATIAL_CERAMIC, SPATIAL_GRAVEL, SPATIAL_CARPET, SPATIAL_GLASS,
	SPATIAL_PLASTER, SPATIAL_WOOD, SPATIAL_METAL, SPATIAL_ROCK, SPATIAL_SKY, SPATIAL_NUM_MATERIALS
};

extern const char		*spatialMaterialNames[SPATIAL_NUM_MATERIALS];
extern const IPLMaterial	spatialMaterials[SPATIAL_NUM_MATERIALS];

// Most surfaces of a level do not say what they are made of. For those there is a table in the assets: the
// name of a shader and a material of the game, a line each. It is written from the shader scripts and from the
// pictures of the textures (japlus_port/tools/samat). It is read when the sound starts and kept from then on:
// connected to a pure server that does not have the pk3 the file can not be read again.
#define SPATIAL_TABLE_FILE	"steamaudio/materials.txt"
#define SPATIAL_TABLE_MAX	4096
extern int				spatialTableCount;		// shaders in it

#define SPATIAL_VOICES				128		// the mixer has 32 channels and 32 loop sounds, each can be one, and the voices
											// of the voice chat and the boxes of the boombox are some more. A sound
											// none is free for goes to the mixer, which knows no walls.
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
#define SPATIAL_SHOCK_GAIN			1.0f	//	and this much of it for each ear, at v_spatialShock 1

// Reflections: what the room around the listener gives back. Rays from where he stands are followed through
// the level, in a thread of its own, and what comes back when and from where becomes the reverb every sound in
// the level is sent through.
#define SPATIAL_REVERB_SECONDS		1.5f	// how long a room can ring
#define SPATIAL_REVERB_ORDER		1		// directions of what comes back: 4 channels (first order Ambisonics)
#define SPATIAL_REVERB_CHANNELS		( ( SPATIAL_REVERB_ORDER + 1 ) * ( SPATIAL_REVERB_ORDER + 1 ) )
#define SPATIAL_MAX_RAYS			8192
#define SPATIAL_DIFFUSE_SAMPLES		32
#define SPATIAL_REFLECT_MSEC		50		// a new pass of the rays is not started sooner than this after the last one,
#define SPATIAL_REFLECT_DUTY		2		//	nor before this many times what the last one took: the threads rest
#define SPATIAL_DRY					8192	// samples of each voice that are kept for the reverb, a power of two

// Reflections of a sound of its own (Valve's guide, "Reflections"): the rays from the listener also find what
// comes back to him off the walls from where a sound is, so a sound in the next room arrives through the doorway
// and rings in both rooms. Each such sound has a convolution of its own, so only the loudest v_spatialSources
// have one. The others go through the reverb of where the listener stands, as all of them did until 2026-10-17.
// The doppler. Its own: the one of the game is only for loops the cgame gives a speed, and its factor has no
// speed of sound in it (S_AddLoopingSound: above 1 only when the sound would be ten times as far a second later).
#define SPATIAL_SOUND_SPEED			( 343.0f / SPATIAL_UNIT )	// units a second
#define SPATIAL_DOPPLER_SAMPLES		1024	// the distance is measured at least this far apart in the time of the mixer
#define SPATIAL_DOPPLER_SMOOTH		0.5f	//	and each measure moves the speed this much of the way
#define SPATIAL_MAX_TAIL			( 4 * 44100 )	// samples a channel stays at most after its end, see S_SpatialTail()
// v_spatialPhysical: a sound fades as a real one does, by 1/r from where the game stops playing it at full volume,
// and is not heard below this (-50 dB)
#define SPATIAL_PHYSICAL_QUIET		0.00316f

#define SPATIAL_REFLECT_SLOTS		16		// the most v_spatialSources can be. A slot is about 7 MB (its impulse
											//	response, partitioned, and its convolution)
#define SPATIAL_REFLECT_KEEP		4.0f	// a sound that has one keeps it against one up to this much louder,
#define SPATIAL_REFLECT_HOLD		1000	//	and against any for this many ms after it got it

// A page does a pass in the thread of the game, a slice of it in a frame (S_SpatialSlice), so a pass takes as
// many frames as it has rays to trace. Its defaults there are the "Medium" of the menu: such a pass is 20 ms of
// work in a page (mp/ffa3, Ryzen 9 5900X), one with the defaults of the desktop 60 ms. Steam Audio can not
// start threads there, so it is told that it has one.
#ifdef __EMSCRIPTEN__
#define SPATIAL_DEFAULT				"1"
#define SPATIAL_DEFAULT_RAYS		"1024"
#define SPATIAL_DEFAULT_BOUNCES		"8"
#define SPATIAL_THREADS				1		// threads Steam Audio shares a pass between: a page has one
#else
#define SPATIAL_DEFAULT				"0"		// of v_spatial: the library has to be put next to the program first
#define SPATIAL_DEFAULT_RAYS		"2048"
#define SPATIAL_DEFAULT_BOUNCES		"16"
// Threads Steam Audio shares the work between, at v_spatialThreads 0. Valve's plugins give the passes 5 % of the
// cores and a bake half of them. All cores but two (2026-10-21) kept a weaker CPU busy all the time in a level.
#define SPATIAL_PASS_CORES			4		// a pass: one of this many cores (spatial.threads),
#define SPATIAL_PASS_THREADS		4		//	at least one, at most this many
#define SPATIAL_BAKE_CORES			2		// a bake: one of this many cores (spatial.bakeThreads)
#endif

enum {
	WORKER_IDLE,		// waits for work. Only in this state may the simulator be given another level
	WORKER_ASKED,
	WORKER_RUNNING
};

// Paths: how a sound gets round a corner. Steam Audio works that out from points it spreads over the floors of
// the level (probes) and from which of them see each other. That is worked out once per level, which takes
// from seconds to minutes: in a thread of its own, the first time the level is loaded, and kept in a file.
#define SPATIAL_PROBE_SPACING		3.0f	// metres between two probes, or more in a large level:
#define SPATIAL_MAX_PROBES			4000	//	the spacing grows by half until there are no more than this. The work and
											//	the file grow with the square of them (mp/ffa3: 3960, 4.2 s, 21.7 MB)
#define SPATIAL_PROBE_HEIGHT		1.5f	//	and how high above the floor they are
// What a bake of the paths and their check while the game runs take: Valve's numbers (the bake of their Unity
// plugin, Baker.cs). Until 2026-10-21 one point, 0.5 m, 30 m and 100 m, which found fewer of the ways.
#define SPATIAL_PATH_SAMPLES		4		// points of a probe that are tried when asking whether two see each other
#define SPATIAL_PATH_RADIUS			1.0f	//	and how far from its middle they are, in metres
#define SPATIAL_PATH_THRESHOLD		0.1f
#define SPATIAL_PATH_VIS_RANGE		1000.0f	// two probes further apart than this are not asked, in metres
#define SPATIAL_PATH_RANGE			1000.0f	// a way longer than this is no way
// Steam Audio puts a way that was found or lost in at once, from one block to the next: each block moves this much
// of the way to what it found, so a way comes and goes over about 100 ms and does not click in or cut off
#define SPATIAL_PATH_SMOOTH			0.3f
// The EQ of a way round the corners that a source of Steam Audio starts with (SimulationData), and a voice's copy
// of it too (S_SpatialTakePaths): an EQ of 0 is a division by zero in Steam Audio's peaking filter (IIR2::peaking,
// sqrtf( gain ) under alpha), and the NaN stays in the path effect and in the reverb it feeds. Steam Audio never
// hands out 0 (a lookup that finds no way gives 1). Until the fix of 2026-10-21 the copy started at 0, and with
// v_spatialPaths 1 every sound of the level was noise at full scale.
#define SPATIAL_PATH_EQ_START		0.1f
// Fewer probes than this are no paths, and the simulator is never given them: a pass of the reflections asks
// every batch it has which probes are near the listener (SimulationManager::lookupBakedReflections), and one
// without a probe has no tree to ask, which is a read at address 0 in Steam Audio 4.8.1. A level where no floor
// was found for a probe has such a batch (t2_wedge).
#define SPATIAL_MIN_PROBES			2
// In the name of the file of the paths. The files before had probes in a quarter of the level only (2026-10-17).
#define SPATIAL_PATHS_VERSION		"_v3"	// _v2: before Valve's numbers for the bake (2026-10-21)
#define SPATIAL_ECHO_VERSION		"_v2"	// of the files of the echo of a level (v_spatialEcho 1). _v1: the live rays
// The echo is baked with Valve's rays and diffuse samples (their Unity plugin), not with the live ones of
// v_spatialRays: a bake runs once per level, in the background. The bounces are v_spatialBounces. mp/ffa3: 3960
// probes in 21.3 s on 22 threads with Embree (3.0 s with 2048 rays and 32 samples), the file 28.7 MB either way.
#define SPATIAL_ECHO_BAKE_RAYS		16384
#define SPATIAL_ECHO_BAKE_DIFFUSE	1024
// A page has no thread to work the paths out next to the game. It does it while the level loads, when it stands
// still anyway, and only for a level of up to this many probes: the work and the memory it takes grow with their
// square. mp/ffa3 had 740 and took 0.4 s (Ryzen 9 5900X), so this was about 3 s, both with the numbers of the bake
// before 2026-10-21: Valve's take longer, by how much in a page was never measured. A larger level has no paths
// there.
#define SPATIAL_PAGE_PROBES			2000

enum {
	BAKE_NONE,
	BAKE_RUNNING,
	BAKE_DONE
};

// what a bake works out: the paths round the corners, or the echo of the room at the probes (v_spatialEcho)
enum {
	BAKE_PATHS,
	BAKE_ECHO,
	BAKE_NUM
};

// The voice chat of the cgame can hand the voice of a player to this file (S_SpatialVoice), so that it is in
// the level like any other sound. The cgame decodes it and plays it itself on every other engine.
#define SPATIAL_STREAM_FIFO			16384	// samples of a voice that wait to be rendered, at the rate of the mixer, a power of two
#define SPATIAL_BRUSH_EPSILON		0.01f	// units or degrees a brush model moves before its place in the scene is changed
#define SPATIAL_LISTENER_JUMP		64.0f	// units the listener moves in a frame that are a jump, not a speed
#define SPATIAL_WALL_MSEC			50.0f	// how fast what the walls let through follows them, see S_SpatialSimulate()
#define SPATIAL_AHEAD_LEAST			0.02f	// seconds the voices are rendered ahead of what the mixer paints at least,
#define SPATIAL_AHEAD_FRAMES		2.5f	//	see S_SpatialAhead(), and how many frames of the game they are
#define SPATIAL_AHEAD_HITCH			250		//	ms of a frame that is not counted
#define SPATIAL_AHEAD_SETTLE		0.02f	//	how fast a shorter frame takes the measure back down, each frame
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

// The convolution of the reflections of one sound. A sound that lets go of it leaves it ringing out on its own.
typedef struct spatialReflect_s {
	struct spatialVoice_s	*voice;		// whose reflections it renders, NULL: none
	IPLSource				source;		// in the simulator, where the sound is
	IPLSource				gpuSource;	//	and in the one of the graphics card, NULL: none (S_SpatialReflectSource)
	IPLReflectionEffect		effect;
	qboolean				ringing;	// the tail of a sound that let go of it
	int						askedPass;	// spatialReflectPasses when the first pass with the sound in it was asked, -1: none yet
} spatialReflect_t;

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
	double		readPos;			// where in the sound the next block begins, in its samples, with fractions
	float		rate;				// samples of the sound per sample heard in the last block: 1, or the doppler
	float		distance;			// units from the listener, this frame
	float		lastDistance;		//	and when it was last measured for the doppler,
	int			lastTime;			//	at this time of the mixer, -1: not yet
	float		approach;			// units a second it comes closer to the listener, smoothed
	int			delay;				// samples it begins later than the game started it: the time it takes to get here

	// where, and how loud the game wants it at that distance
	vec3_t		origin;
	vec3_t		simOrigin;			// where Steam Audio has it: origin, or out of the brush model it is in
	qboolean	outOfBrush;			//	(S_SpatialOutOfBrushes)
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
	int			dryRead;			// up to which sample the reverb has taken it, of dry and of own

	// its reflections of its own, NULL: it goes through the reverb of the listener. They get the sound as the
	// game plays it at its distance without the walls in the way (own): Valve's impulse response has the walls.
	spatialReflect_t	*reflect;
	int			reflectSince;		// Sys_Milliseconds() it got them
	float		own[SPATIAL_DRY];
	float		ownMix;				// how much of the last block went to them and not to the reverb of the listener

	IPLSource				source;
	IPLDirectEffect			direct;
	IPLBinauralEffect		binaural;
	IPLPanningEffect		panning;
	IPLPathEffect			path;
	IPLDirectEffectParams	directParams;
	qboolean				pathing;	// this frame its way round the corners is asked for
	float					pathSh[SPATIAL_REVERB_CHANNELS];	// of that way, smoothed from block to block
	float					pathTarget[SPATIAL_REVERB_CHANNELS];	//	what the last pass found (S_SpatialTakePaths)
	float					pathEq[3];
	float					lastHidden;	// how much of it the walls hid in the last block
	float					occlusion, transmission[3];	// what the walls let through, smoothed (S_SpatialSimulate)
} spatialVoice_t;

/*
A brush model of the level (a door, a lift, a func_static, a pane of glass): its walls in a scene of their own,
which the scene of the simulator holds where the cgame says the entity is now (S_SpatialMovers). One the cgame
never named stays where the map has it, which for a door is shut.
*/
typedef struct {
	int					model;			// its number in the collision model
	IPLScene			scene;
	IPLStaticMesh		mesh;
	IPLInstancedMesh	instance;		// in spatialLevel_t.top
	int					triangles;
	qboolean			added;			// in the top scene now
	qboolean			gone;			// the cgame says it is gone (broken, switched off)
	qboolean			changed;		// a place or a state the top scene does not have yet
	vec3_t				origin, angles;
	vec3_t				mins, maxs;		// of its walls, where the map has them (CM_ModelBounds)
} spatialBrush_t;

// What a bake of one kind made of a level, or its file (BAKE_PATHS, BAKE_ECHO): its probes with the data at them
typedef struct {
	IPLProbeBatch		batch;				// in the simulator
	IPLProbeBatch		next;				// waits to be put there, as a level does
	qboolean			wait;
	qboolean			tried;				// the file of this level was looked for (the echo, once v_spatialEcho is on)
	qboolean			wanted;				//	and there was none: a bake is wanted
	int					numProbes;
	float				spacing;			// metres between the probes of a bake, 0: they came from the file
	int					bakeMsec;			// what the bake took, 0: from the file
	char				file[MAX_QPATH], nextFile[MAX_QPATH];
} spatialBaked_t;

// A level as the simulator has it: the walls of the world (scene, also what the bake of the paths reads), and the
// scene the simulator traces in, which holds that one and the brush models
typedef struct {
	IPLScene			scene;
	IPLStaticMesh		mesh;
	IPLScene			top;
	IPLInstancedMesh	world;			// the scene of the world in the top one
	spatialBrush_t		*brushes;		// Z_Malloc, numBrushes of them
	int					numBrushes;
	int					brushTriangles;
} spatialLevel_t;

extern cvar_t	*v_spatial;
extern cvar_t	*v_spatialHrtf;
extern cvar_t	*v_spatialAhead;
extern cvar_t	*v_spatialReflections;
extern cvar_t	*v_spatialReverb;
extern cvar_t	*v_dreamReverb;
extern cvar_t	*v_spatialRays;
extern cvar_t	*v_spatialBounces;
extern cvar_t	*v_spatialPaths;
extern cvar_t	*v_spatialPathLoss;
extern cvar_t	*v_spatialShock;
extern cvar_t	*v_spatialSources;
extern cvar_t	*v_spatialThreads;
extern cvar_t	*v_spatialPhysical;
#ifdef __EMSCRIPTEN__
extern cvar_t	*v_spatialBudget;
#endif
extern cvar_t	*v_spatialDebug;
extern cvar_t	*v_spatialEcho;
extern cvar_t	*v_spatialGpu;

// what v_spatialInfo says the sound of the level took of the thread of the game
enum {
	COST_SIMULATE,		// what is in the way of each sound
	COST_VOICES,		// the effects of the voices
	COST_REVERB,		// the convolution with what the last pass found
	COST_PASS,			// the passes themselves, where the game does them (a browser). Elsewhere a thread does
	COST_NUM
};

typedef struct {
	void		*library;
	char		libraryPath[MAX_OSPATH];
	qboolean	active;
	float		shockCoeffs[5];		// the low pass of the thump: b0 b1 b2 a1 a2

	IPLContext			context;
	IPLAudioSettings	audio;
	IPLHRTF				hrtf;
	IPLSimulator		simulator;
	IPLEmbreeDevice		embree;				// NULL: Steam Audio traces the rays itself
	IPLSceneType		sceneType;			//	of the levels and the simulator: the two must be alike
	// The reflections traced on the graphics card (v_spatialGpu, snd_spatial_gpu.cpp): a simulator of their own, with a
	// scene that asks the callbacks there, and sources of its own for the reverb and the slots. NULL: none here.
	IPLSimulator		gpuSimulator;
	IPLScene			gpuScene;
	int					threads;			// Steam Audio shares a pass between this many
	int					bakeThreads;		//	and a bake between this many

	// The level. A new one waits in "next" until the thread of the reflections is idle: the simulator must not
	// be given another level while a pass runs.
	spatialLevel_t		level, nextLevel;
	int					numTriangles, nextTriangles;
	int					brushesMoved;		// brush models whose place changed in the scene this frame
	qboolean			sceneWaits;
	int					materialTriangles[SPATIAL_NUM_MATERIALS];
	int					tableTriangles;		// of them, what the table of shaders said and not the level
	int					sceneMsec;
	int					sceneNumber;		// counts the levels that were taken over
	IPLVector3			nextMins, nextMaxs;	// the corners of the level, for where to spread probes
	IPLVector3			mins, maxs;

	// what is worked out once per level and kept in a file: the paths, and the echo of the room at the probes
	// (v_spatialEcho 1), with which the reverb where the listener stands is looked up there and no ray is traced
	spatialBaked_t		paths, echo;
	int					refusedProbes;		// not 0: the level has this many, too many to work its paths out here

	// reflections
	IPLSource					reverbSource;		// stands where the listener stands
	IPLSource					gpuReverbSource;	//	and its twin in the simulator of the graphics card
	qboolean					reflectGpu;			// the passes are traced on the graphics card now,
	int							reflectGpuPass;		//	since the pass that spatialReflectPasses counted this at
	qboolean					heardGpu;			//	and the reverb is heard from what that simulator found
	IPLReflectionEffect			reverbEffect;
	IPLAmbisonicsDecodeEffect	reverbDecode;
	IPLReflectionEffect			dreamEffect[2];		// the reverb of a dream for each ear (v_dreamReverb, S_SpatialRenderDream)
	float						dreamMix;			// how much goes into it, 0 to 1, at the end of the last block
	int							dreamTail;			// samples it still rings after nothing more went in
	int							reverbRendered;		// up to which sample of the mixer's time the reverb is in the ring
	SDL_Thread					*worker;
	SDL_sem						*workerWake;
	int							reflectAsked;		// Sys_Milliseconds() of the last pass that was asked for
	int							maxRays;			// the rays the simulator was made for
	spatialReflect_t			reflects[SPATIAL_REFLECT_SLOTS];
	int							numReflectSlots;	// made of them: none in a page
	IPLReflectionMixer			reflectMixer;		// adds up what they render, for the one decode of the reverb
	int							numReflects;		// sounds that had reflections of their own in the last frame
	int							pathsTaken;			// spatialReflectPasses when the ways round the corners were last taken
#ifdef __EMSCRIPTEN__
	int							pathsAsked;			// Sys_Milliseconds() of the last look for the ways round the corners
	int							passCredit;			// microseconds the passes may take, see S_SpatialUpdate()
	// of the pass that is being done: the time of its slices, how many they are, the longest, when it began
	int							passUsec, passSlices, passPeakUsec, passBegun;
	int							lastPassSlices, lastPassPeakUsec, lastPassMsec;
#endif

	// the listener and the sounds of this frame
	int					frame;
	vec3_t				listenerOrigin;
	matrix3_t			listenerAxis;
	int					listenerJumped;		// the frame he last jumped to another place, see S_SpatialBeginFrame()
	int					lastUpdate;			// Sys_Milliseconds() of the last S_SpatialUpdate()
	float				frameMsec;			//	and how long a frame takes, see S_SpatialAhead()
	float				lastFrameMsec;		//	and how long the last one took

	// for v_spatialDebug (S_SpatialDebug)
	int					numFallbacks;		// sounds of this frame no voice was free for
	spatialDebugFallback_t	fallbacks[SPATIAL_DEBUG_FALLBACKS];
	double				directEnergy, reverbEnergy;	// what the voices and the reverb put into the ring since it was
	int					energySamples;		//	last asked, squared, and over how many samples of the mixer
	float				directLevel, reverbLevel;	// dB of full scale, smoothed
	IPLVector3			settledOrigin;		// where the listener was at the last pass,
	int					settledPasses;		//	and how many passes he has not moved since: Steam Audio adds them up
	int					numSegments;		// the ways round the corners of the last pass with paths
	spatialDebugSegment_t	segments[SPATIAL_DEBUG_SEGMENTS];
	spatialVoice_t		voices[SPATIAL_VOICES];

	// finished sound, by the time of the mixer
	float				ring[SPATIAL_RING][2];
	int					ringEnd;			// up to which sample the ring holds what belongs there
	int					lastPaint;

	// for v_spatialInfo
	int					numVoices;
	int					renderUsec, simulateUsec;	// of the last frame
	int					peakRenderUsec;
	// since the command last asked: microseconds by what they went into, the frame that took most, the voices
	int					costFrames;
	int					costUsec[COST_NUM];
	int					costFrameUsec, costPeakUsec;
	int					costVoices, costPeakVoices;

	// v_spatialCapture
	short				*capture;			// a WAV file in the making: its header, then two channels of 16 bit
	int					captureSamples, captureWritten;
	int					captureFrom;		// up to which sample of the mixer's time it has what was heard, -1: not begun
} spatialState_t;

extern spatialState_t	spatial;

// The bake of the paths of one level. The thread has the fields to itself while it runs (spatialBakeState is
// BAKE_RUNNING), the game before and after. It outlives the sound system: a bake that still runs when the level
// changes or the sound is restarted is never stopped inside Steam Audio, see S_SpatialCancelBake().
typedef struct {
	SDL_Thread			*thread;
	IPLContext			context;			// held by the thread for as long as it runs
	void				*library;			// Steam Audio, kept loaded for a bake the sound system let go of
	IPLScene			scene;				// held by the thread for as long as it runs
	IPLSceneType		sceneType;			//	and its type
	int					kind;				// BAKE_PATHS or BAKE_ECHO
	int					bounces;			// of the echo
	IPLVector3			mins, maxs;
	int					sceneNumber;		// the level it is for: by the time it is done there may be another
	IPLProbeBatch		batch;
	IPLSerializedObject	saved;
	int					numProbes;
	float				spacing;			// metres between the probes it spread
	int					msec;
} spatialBaker_t;

extern spatialBaker_t	spatialBake;

extern std::atomic<int>	spatialBakeState;		// BAKE_*
extern std::atomic<int>	spatialBakeProgress;	// 0 to 100
// The game does not want what the bake that runs makes (S_SpatialCancelBake): the thread does not start what it
// has not started yet, and what it made is thrown away unread.
extern std::atomic<int>	spatialBakeCancel;

// One for every player, one more for what the own player says (S_SpatialOwnVoice), and one for every channel of a
// box of the boombox (cgame/boombox): its songs come as the voice of speaker MAX_CLIENTS + the box, the right
// channel of a sound system as MAX_CLIENTS + BOX_MAX + the box. BOX_CHANNELS of bg_boombox.h (8 before 0.2.4).
#define SPATIAL_OWN_STREAM	MAX_CLIENTS
#define SPATIAL_BOXES		16
extern spatialStream_t	spatialStreams[MAX_CLIENTS + 1 + SPATIAL_BOXES];

// A box of the boombox far away is heard as music across a hall: its straight way fades with the distance as the
// cgame makes it (in the volume it gives), but the room keeps ringing from it. So what of it the reverb and its
// reflections get falls off slower: from SPATIAL_BOX_WET_NEAR on it is that much more for every such distance, up
// to SPATIAL_BOX_WET_MAX times. And it loses its highs in the air, also without v_spatialPhysical.
#define SPATIAL_BOX_WET_NEAR	192.0f
#define SPATIAL_BOX_WET_MAX		4.0f

// What a pass of the reflections starts from. Valve sets the inputs of the reflections in the thread that runs
// them (phonon.h, iplSourceSetInputs). The game writes this only while the thread is idle, the thread reads it
// after it was woken, see S_SpatialAskPass() and S_SpatialPassInputs().
typedef struct {
	IPLCoordinateSpace3	listener;
	int					rays, bounces;
	qboolean			gpu;							// traced on the graphics card (spatial.gpuSimulator)
	qboolean			reverb;							// the source where the listener stands
	qboolean			echoBaked;						//	looked up at the probes (v_spatialEcho)
	qboolean			own[SPATIAL_REFLECT_SLOTS];		// the slots that render a sound
	IPLCoordinateSpace3	source[SPATIAL_REFLECT_SLOTS];
	// the ways round the corners, the same: their inputs are set where iplSimulatorRunPathing() runs. The thread
	// of the game set them every frame while the thread ran the paths, and both crashed in Steam Audio (2026-10-19)
	qboolean			path[SPATIAL_VOICES];			// the voices that look for a way
	IPLVector3			pathOrigin[SPATIAL_VOICES];
	int					pathChannel[SPATIAL_VOICES];	// CHAN_*, for S_SpatialPathFade()
} spatialPassInputs_t;

extern spatialPassInputs_t	spatialPass;

// what the game and the thread of the reflections both touch
extern std::atomic<int>	spatialWorkerState;		// WORKER_*
extern std::atomic<int>	spatialWorkerQuit;
extern std::atomic<int>	spatialPassReflections;	// what the pass that is asked for is to work out
extern std::atomic<int>	spatialPassPaths;
extern std::atomic<int>	spatialReflectUsec;		// what the last pass took
extern std::atomic<int>	spatialReflectPasses;
extern std::atomic<int>	spatialPassDebug;		// the pass hands the ways it found to S_SpatialPathVis()
extern std::atomic<int>	spatialVisCount;		//	how many of them, as Steam Audio has them (spatialVis)
typedef struct {
	IPLVector3	from, to;
	IPLbool		occluded;
} spatialVisSegment_t;

extern spatialVisSegment_t	spatialVis[SPATIAL_DEBUG_SEGMENTS];

// A point of the game for Steam Audio: metres, x to the right, y up, z to the back. The game has x ahead, y to
// the left and z up. Both are right handed, so a triangle keeps the side it faces.
static inline void S_SpatialPoint( const vec3_t in, IPLVector3 *out ) {
	out->x = -in[1] * SPATIAL_UNIT;
	out->y = in[2] * SPATIAL_UNIT;
	out->z = -in[0] * SPATIAL_UNIT;
}

// the same for a direction
static inline void S_SpatialDirection( const vec3_t in, IPLVector3 *out ) {
	out->x = -in[1];
	out->y = in[2];
	out->z = -in[0];
}

// and back, from Steam Audio's metres and axes to the game's
static inline void S_SpatialGamePoint( const IPLVector3 *in, vec3_t out ) {
	VectorSet( out, -in->z / SPATIAL_UNIT, -in->x / SPATIAL_UNIT, in->y / SPATIAL_UNIT );
}

// A source has a place and no direction: its axes are Steam Audio's own. origin in Steam Audio's metres.
static inline void S_SpatialSourceSpace( const IPLVector3 *origin, IPLCoordinateSpace3 *out ) {
	memset( out, 0, sizeof( *out ) );
	out->origin = *origin;
	out->right.x = 1.0f;
	out->up.y = 1.0f;
	out->ahead.z = -1.0f;
}

// a value that follows what it is given, step of the way each time, and is it at once while fresh
static inline void S_SpatialFollow( float *value, float target, float step, qboolean fresh ) {
	*value = fresh ? target : *value + ( target - *value ) * step;
}

// the rays the simulator is made for, from v_spatialRays (taken when the sound system starts)
static inline int S_SpatialRayCvar( void ) {
	return Com_Clampi( 256, SPATIAL_MAX_RAYS, v_spatialRays->integer );
}

// how often a ray is followed off a wall
static inline int S_SpatialBounces( void ) {
	return Com_Clampi( 1, 64, v_spatialBounces->integer );
}

static inline int S_SpatialUsec( void ) {
	return (int)std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now().time_since_epoch() ).count();
}

/*
===============================================================================

WHAT THE FILES CALL OF EACH OTHER

===============================================================================
*/

// snd_spatial_level.cpp
// The shader of the triangle before, and what the table says it is made of: the triangles of a side come one
// after the other
typedef struct {
	const char	*shader;
	int			tableMaterial;		// MATERIAL_*
} spatialShaderCache_t;

void S_SpatialLoadMaterials( void );
int S_SpatialTriangleMaterial( spatialShaderCache_t *cache, const char *shader, int surfaceFlags, qboolean *byTable );
void S_SpatialCountBrushes( int *withMesh, int *inScene );
qboolean S_SpatialOutOfBrushes( const vec3_t origin, vec3_t out );
void S_SpatialReleaseLevel( spatialLevel_t *level );
void S_SpatialTakeScene( void );
void S_SpatialApplyBrushes( void );
void S_SpatialSaveScene_f( void );

// snd_spatial_bake.cpp
void S_SpatialCancelBake( void );
qboolean S_SpatialReapBake( void );
void S_SpatialDropBake( void );
qboolean S_SpatialLoadBaked( spatialBaked_t *baked );
void S_SpatialRemoveBaked( spatialBaked_t *baked );
void S_SpatialReleaseBaked( spatialBaked_t *baked );
void S_SpatialNameBakes( const char *mapname, unsigned checksum );
qboolean S_SpatialEchoBaked( void );
void S_SpatialThinkPaths( void );

// snd_spatial_gpu.cpp
qboolean S_SpatialGpuInit( void );
void S_SpatialGpuShutdown( void );
const char *S_SpatialGpuName( void );
const char *S_SpatialGpuWhyNot( void );
void S_SpatialGpuSceneSettings( IPLSceneSettings *settings );
void S_SpatialGpuBeginLevel( void );
void S_SpatialGpuMesh( int model, const IPLVector3 *vertices, const IPLint32 *materials, int count );
void S_SpatialGpuPlace( qboolean next, int model, const IPLMatrix4x4 *transform );
qboolean S_SpatialGpuIdle( void );
void S_SpatialGpuTakeLevel( void );
qboolean S_SpatialGpuReady( void );
qboolean S_SpatialGpuBeginPass( void );
void S_SpatialGpuBatchInfo( int *batches, int *usec, int *answered );

// snd_spatial_sim.cpp
int S_SpatialRays( void );
void S_SpatialListener( IPLCoordinateSpace3 *listener );
void S_SpatialLetGoReflect( spatialVoice_t *v );
qboolean S_SpatialReflectReady( const spatialReflect_t *r );
IPLSource S_SpatialHeardReverb( void );
IPLSource S_SpatialReflectSource( const spatialReflect_t *r, qboolean gpu );
void S_SpatialSimulate( void );
void S_SpatialPassInputs( void );
void S_SpatialPathInputs( void );
void S_SpatialFillPaths( void );
void S_SpatialCopyPaths( void );
void S_SpatialTakePaths( void );
int SDLCALL S_SpatialWorker( void *unused );
void S_SpatialAskPass( void );

// snd_spatial_voice.cpp
void S_SpatialResetVoice( spatialVoice_t *v );
qboolean S_SpatialBoxVoice( const spatialVoice_t *v );
float S_SpatialBoxWet( const spatialVoice_t *v );
float S_SpatialRate( const spatialVoice_t *v );
int S_SpatialStreamQueued( const spatialStream_t *stream );
void S_SpatialGatherStreams( void );

// snd_spatial_render.cpp
void S_SpatialResetReverb( void );

// snd_spatial_debug.cpp
void S_SpatialCaptureEnd( qboolean write );
void S_SpatialCapture( int from );
void S_SpatialCapture_f( void );
void S_SpatialInfo_f( void );
void S_SpatialDebugFree( void );

#ifdef __EMSCRIPTEN__
// snd_spatial_page.cpp
void S_SpatialSayTooLarge( void );
int S_SpatialBudget( void );
void S_SpatialPageLoadMap( void );
void S_SpatialPagePaths( qboolean paths, int now );
void S_SpatialPageUpdate( void );
#endif

#endif // JP_SPATIAL
