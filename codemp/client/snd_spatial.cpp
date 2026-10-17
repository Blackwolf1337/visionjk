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
// thread, so what the two threads of this file do elsewhere is done by the game itself, in a slice of a frame
// and while a level loads.

#include "snd_local.h"
#include "snd_spatial.h"

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

#if defined(__EMSCRIPTEN__)
#define SPATIAL_LIBRARY	"phonon.wasm"	// one of the modules the page compiled, built from lib/steamaudio by this tree
#elif defined(_WIN32)
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
	X( iplPathEffectApply ) \
	SPATIAL_SLICE_FUNCTIONS( X )

// what the library for a browser has on top of Valve's, see phonon_slices.h
#ifdef __EMSCRIPTEN__
#define SPATIAL_SLICE_FUNCTIONS( X ) \
	X( iplxSetSliceBudget ) \
	X( iplxSlicePending )
#else
#define SPATIAL_SLICE_FUNCTIONS( X )
#endif

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

// Most surfaces of a level do not say what they are made of. For those there is a table in the assets: the
// name of a shader and a material of the game, a line each. It is written from the shader scripts and from the
// pictures of the textures (japlus_port/tools/samat). It is read when the sound starts and kept from then on:
// connected to a pure server that does not have the pk3 the file can not be read again.
#define SPATIAL_TABLE_FILE	"steamaudio/materials.txt"
#define SPATIAL_TABLE_MAX	4096

typedef struct spatialShader_s {
	char	shader[MAX_QPATH];		// first: an entry is compared as its name
	byte	material;				// MATERIAL_*
} spatialShader_t;

static spatialShader_t	spatialTable[SPATIAL_TABLE_MAX];
static int				spatialTableCount;

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
#define SPATIAL_SHOCK_GAIN			1.0f	//	and this much of it for each ear, at v_spatialShock 1

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

// Reflections of a sound of its own (Valve's guide, "Reflections"): the rays from the listener also find what
// comes back to him off the walls from where a sound is, so a sound in the next room arrives through the doorway
// and rings in both rooms. Each such sound has a convolution of its own, so only the loudest v_spatialSources
// have one. The others go through the reverb of where the listener stands, as all of them did until 2026-10-17.
#define SPATIAL_REFLECT_SLOTS		16		// the most v_spatialSources can be. A slot is about 7 MB (its impulse
											//	response, partitioned, and its convolution)
#define SPATIAL_REFLECT_KEEP		2.0f	// a sound that has one keeps it against one up to this much louder

// A page does a pass in the thread of the game, a slice of it in a frame (S_SpatialSlice), so a pass takes as
// many frames as it has rays to trace. Its defaults there are the "Medium" of the menu: such a pass is 20 ms of
// work in a page (mp/ffa3, Ryzen 9 5900X), one with the defaults of the desktop 60 ms. Steam Audio can not
// start threads there, so it is told that it has one.
#ifdef __EMSCRIPTEN__
#define SPATIAL_DEFAULT				"1"
#define SPATIAL_DEFAULT_RAYS		"1024"
#define SPATIAL_DEFAULT_BOUNCES		"8"
#define SPATIAL_THREADS				1
#else
#define SPATIAL_DEFAULT				"0"		// of v_spatial: the library has to be put next to the program first
#define SPATIAL_DEFAULT_RAYS		"2048"
#define SPATIAL_DEFAULT_BOUNCES		"16"
#define SPATIAL_THREADS				2		// threads Steam Audio shares a pass or a bake between, inside the one of this file
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
											//	the file grow with the square of them (mp/ffa3: 3960, 3.8 s, 16.5 MB)
#define SPATIAL_PROBE_HEIGHT		1.5f	//	and how high above the floor they are
#define SPATIAL_PATH_SAMPLES		1		// points of a probe that are tried when asking whether two see each other
#define SPATIAL_PATH_RADIUS			0.5f	//	and how far from its middle they are, in metres
#define SPATIAL_PATH_THRESHOLD		0.1f
#define SPATIAL_PATH_VIS_RANGE		30.0f	// two probes further apart than this are not asked, in metres
#define SPATIAL_PATH_RANGE			100.0f	// a way longer than this is no way
// Fewer probes than this are no paths, and the simulator is never given them: a pass of the reflections asks
// every batch it has which probes are near the listener (SimulationManager::lookupBakedReflections), and one
// without a probe has no tree to ask, which is a read at address 0 in Steam Audio 4.8.1. A level where no floor
// was found for a probe has such a batch (t2_wedge).
#define SPATIAL_MIN_PROBES			2
// In the name of the file of the paths. The files before had probes in a quarter of the level only (2026-10-17).
#define SPATIAL_PATHS_VERSION		"_v2"
// A page has no thread to work the paths out next to the game. It does it while the level loads, when it stands
// still anyway, and only for a level of up to this many probes: the work and the memory it takes grow with their
// square. mp/ffa3 has 740 and takes 0.4 s (Ryzen 9 5900X), so this is about 3 s. A larger level has no paths
// there.
#define SPATIAL_PAGE_PROBES			2000

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

// The convolution of the reflections of one sound. A sound that lets go of it leaves it ringing out on its own.
typedef struct spatialReflect_s {
	struct spatialVoice_s	*voice;		// whose reflections it renders, NULL: none
	IPLSource				source;		// in the simulator, where the sound is
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
	int			dryRead;			// up to which sample the reverb has taken it, of dry and of own

	// its reflections of its own, NULL: it goes through the reverb of the listener. They get the sound as it
	// leaves it (own), and Valve's impulse response has the walls and the distance in it.
	spatialReflect_t	*reflect;
	float		own[SPATIAL_DRY];
	float		reflectMinDistance;	// metres: as loud as at this distance closer than it, see S_SpatialPlaceVoice()
	float		ownScale;			//	and what the sound is scaled by for them

	IPLSource				source;
	IPLDirectEffect			direct;
	IPLBinauralEffect		binaural;
	IPLPanningEffect		panning;
	IPLPathEffect			path;
	IPLDirectEffectParams	directParams;
	qboolean				pathing;	// this frame its way round the corners is asked for
} spatialVoice_t;

static cvar_t	*v_spatial;
static cvar_t	*v_spatialHrtf;
static cvar_t	*v_spatialAhead;
static cvar_t	*v_spatialReflections;
static cvar_t	*v_spatialReverb;
static cvar_t	*v_spatialRays;
static cvar_t	*v_spatialBounces;
static cvar_t	*v_spatialPaths;
static cvar_t	*v_spatialPathLoss;
static cvar_t	*v_spatialShock;
static cvar_t	*v_spatialSources;
#ifdef __EMSCRIPTEN__
static cvar_t	*v_spatialBudget;
#endif

// what v_spatialInfo says the sound of the level took of the thread of the game
enum {
	COST_SIMULATE,		// what is in the way of each sound
	COST_VOICES,		// the effects of the voices
	COST_REVERB,		// the convolution with what the last pass found
	COST_PASS,			// the passes themselves, where the game does them (a browser). Elsewhere a thread does
	COST_NUM
};

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
	int					tableTriangles;		// of them, what the table of shaders said and not the level
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
	float				probeSpacing;		// of a bake, 0: the probes came from the file
	int					bakeMsec;			// what the bake took, 0: the probes came from the file
	int					refusedProbes;		// not 0: the level has this many, too many to work its paths out here

	// reflections
	IPLSource					reverbSource;		// stands where the listener stands
	IPLReflectionEffect			reverbEffect;
	IPLAmbisonicsDecodeEffect	reverbDecode;
	int							reverbRendered;		// up to which sample of the mixer's time the reverb is in the ring
	SDL_Thread					*worker;
	SDL_sem						*workerWake;
	int							reflectAsked;		// Sys_Milliseconds() of the last pass that was asked for
	int							maxRays;			// the rays the simulator was made for
	spatialReflect_t			reflects[SPATIAL_REFLECT_SLOTS];
	int							numReflectSlots;	// made of them: none in a page
	IPLReflectionMixer			reflectMixer;		// adds up what they render, for the one decode of the reverb
	int							numReflects;		// sounds that had reflections of their own in the last frame
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
	float				spacing;			// metres between the probes it spread
	int					msec;
} spatialBake;

static std::atomic<int>	spatialBakeState;		// BAKE_*
static std::atomic<int>	spatialBakeProgress;	// 0 to 100
// The game told the bake that runs to stop (S_SpatialCancelBake). Steam Audio 4.8.1 leaves the paths half made in
// the probes then (PathBaker::bake adds what a stopped BakedPathData has), and writing those out is a crash in
// the thread of the bake: what a stopped bake has is thrown away unread.
static std::atomic<int>	spatialBakeCancel;

// One for every player, one more for what the own player says (S_SpatialOwnVoice), and one for every box of the
// boombox (cgame/boombox): its songs come as the voice of speaker MAX_CLIENTS + the box. BOX_MAX of bg_boombox.h.
#define SPATIAL_OWN_STREAM	MAX_CLIENTS
#define SPATIAL_BOXES		8
static spatialStream_t	spatialStreams[MAX_CLIENTS + 1 + SPATIAL_BOXES];

// What a pass of the reflections starts from. Valve sets the inputs of the reflections in the thread that runs
// them (phonon.h, iplSourceSetInputs). The game writes this only while the thread is idle, the thread reads it
// after it was woken, see S_SpatialAskPass() and S_SpatialPassInputs().
static struct {
	IPLCoordinateSpace3	listener;
	int					rays, bounces;
	qboolean			reverb;							// the source where the listener stands
	qboolean			own[SPATIAL_REFLECT_SLOTS];		// the slots that render a sound
	IPLCoordinateSpace3	source[SPATIAL_REFLECT_SLOTS];
	float				minDistance[SPATIAL_REFLECT_SLOTS];
} spatialPass;

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

// Next to the program first: that is where a build puts it. Then wherever the system looks. A page has its
// modules by name only: there both ask for the same one.
static qboolean S_SpatialLoad( void ) {
	const char *missing = NULL;

	Com_sprintf( spatial.libraryPath, sizeof( spatial.libraryPath ), "%s/" SPATIAL_LIBRARY, Sys_BinaryPath() );
	spatial.library = Sys_LoadLibrary( spatial.libraryPath );
	if ( !spatial.library ) {
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
	const char	*shader;			// of the triangle before: the triangles of a side come one after the other
	int			tableMaterial;		//	and what the table says it is made of
} spatialMeshBuild_t;

// for qsort and bsearch: both sides are a name, or an entry of the table, which begins with its name
static int QDECL S_SpatialCompareShader( const void *a, const void *b ) {
	return Q_stricmp( (const char *)a, (const char *)b );
}

static void S_SpatialLoadMaterials( void ) {
	static const char *names[MATERIAL_LAST] = { MATERIALS };
	void *data = NULL;
	const char *text;
	int count = 0, unknown = 0;

	if ( FS_ReadFile( SPATIAL_TABLE_FILE, &data ) <= 0 || !data ) {
		return;
	}
	text = (const char *)data;
	COM_BeginParseSession( SPATIAL_TABLE_FILE );
	while ( 1 ) {
		const char *token = COM_ParseExt( &text, qtrue );
		int material;

		if ( !token[0] ) {
			break;
		}
		if ( count == SPATIAL_TABLE_MAX ) {
			Com_Printf( S_COLOR_YELLOW "v_spatial: " SPATIAL_TABLE_FILE " has more than %i shaders, the rest is left out\n", SPATIAL_TABLE_MAX );
			break;
		}
		Q_strncpyz( spatialTable[count].shader, token, sizeof( spatialTable[count].shader ) );
		token = COM_ParseExt( &text, qfalse );
		for ( material = MATERIAL_NONE + 1; material < MATERIAL_LAST; material++ ) {
			if ( !Q_stricmp( token, names[material] ) ) {
				break;
			}
		}
		SkipRestOfLine( &text );
		if ( material == MATERIAL_LAST ) {
			unknown++;
			continue;
		}
		spatialTable[count++].material = material;
	}
	FS_FreeFile( data );
	if ( unknown ) {
		Com_Printf( S_COLOR_YELLOW "v_spatial: %i lines of " SPATIAL_TABLE_FILE " name no material of the game\n", unknown );
	}
	qsort( spatialTable, count, sizeof( spatialTable[0] ), S_SpatialCompareShader );
	spatialTableCount = count;
}

// what the table says a shader is made of, MATERIAL_NONE if it is not in it
static int S_SpatialTableMaterial( const char *shader ) {
	const spatialShader_t *entry = (const spatialShader_t *)bsearch( shader, spatialTable, spatialTableCount,
		sizeof( spatialTable[0] ), S_SpatialCompareShader );

	return entry ? entry->material : MATERIAL_NONE;
}

// The sky first. Then the table, before what the level says: a line of the table can put a level right.
static void S_SpatialTriangle( const vec3_t a, const vec3_t b, const vec3_t c, const char *shader, int surfaceFlags, void *data ) {
	spatialMeshBuild_t *build = (spatialMeshBuild_t *)data;
	const int n = build->count++;
	int material;

	if ( shader != build->shader ) {
		build->shader = shader;
		build->tableMaterial = S_SpatialTableMaterial( shader );
	}
	if ( surfaceFlags & SURF_SKY ) {
		material = SPATIAL_SKY;
	} else if ( build->tableMaterial != MATERIAL_NONE ) {
		material = spatialMaterialOf[build->tableMaterial];
		spatial.tableTriangles++;
	} else {
		material = spatialMaterialOf[surfaceFlags & MATERIAL_MASK];
	}

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

// nothing of the reflections rings on: the level changed, or the time of the mixer went back
static void S_SpatialResetReverb( void ) {
	if ( spatial.reverbEffect ) {
		qiplReflectionEffectReset( spatial.reverbEffect );
		qiplAmbisonicsDecodeEffectReset( spatial.reverbDecode );
	}
	for ( int i = 0; i < spatial.numReflectSlots; i++ ) {
		qiplReflectionEffectReset( spatial.reflects[i].effect );
		spatial.reflects[i].ringing = qfalse;
	}
	if ( spatial.reflectMixer ) {
		qiplReflectionMixerReset( spatial.reflectMixer );
	}
}

#ifdef __EMSCRIPTEN__
static void S_SpatialSlice( int limit );

// spatial.refusedProbes says how many it has
static void S_SpatialSayTooLarge( void ) {
	Com_Printf( "v_spatial: this level has %i probes, a page works the paths out for up to %i: no sound round corners here\n",
		spatial.refusedProbes, SPATIAL_PAGE_PROBES );
}
#endif

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
	while ( qiplProbeArrayCreate( spatial.context, &array ) == IPL_STATUS_SUCCESS ) {
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

	if ( array && qiplProbeBatchCreate( spatial.context, &spatialBake.batch ) == IPL_STATUS_SUCCESS ) {
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
		bake.numThreads = SPATIAL_THREADS;
#ifdef __EMSCRIPTEN__
		// too large to do while the game waits, see SPATIAL_PAGE_PROBES: nothing is kept of it
		if ( spatialBake.numProbes > SPATIAL_PAGE_PROBES ) {
			qiplProbeBatchRelease( &spatialBake.batch );
		}
#endif
		if ( spatialBake.batch && spatialBake.numProbes >= SPATIAL_MIN_PROBES && !spatialBakeCancel.load() ) {
			qiplPathBakerBake( spatial.context, &bake, S_SpatialBakeProgress, NULL );
		}
		// stopped: see spatialBakeCancel
		if ( spatialBake.batch && spatialBakeCancel.load() ) {
			qiplProbeBatchRelease( &spatialBake.batch );
			spatialBake.batch = NULL;
		}

		// as it goes into the file. The game writes it, the file system of the engine is not for this thread.
		memset( &serialized, 0, sizeof( serialized ) );
		if ( spatialBake.batch && qiplSerializedObjectCreate( spatial.context, &serialized, &spatialBake.saved ) == IPL_STATUS_SUCCESS ) {
			qiplProbeBatchSave( spatialBake.batch, spatialBake.saved );
		} else {
			spatialBake.saved = NULL;
		}
	}
	if ( array ) {
		qiplProbeArrayRelease( &array );
	}
	qiplSceneRelease( &spatialBake.scene );
	spatialBake.msec = ( S_SpatialUsec() - start ) / 1000;
	spatialBakeState.store( BAKE_DONE );
	return 0;
}

// tells the bake that runs to stop. It does not wait for it.
static void S_SpatialCancelBake( void ) {
	spatialBakeCancel.store( 1 );
	qiplPathBakerCancelBake( spatial.context );
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
			spatial.bakeMsec = 0;
			spatial.probeSpacing = 0.0f;
			loaded = qtrue;
			if ( qiplProbeBatchGetNumProbes( spatial.nextProbes ) >= SPATIAL_MIN_PROBES ) {
				qiplProbeBatchCommit( spatial.nextProbes );
				spatial.numProbes = qiplProbeBatchGetNumProbes( spatial.nextProbes );
				spatial.probesWait = qtrue;
			} else {
				// the file of a level without paths: it is not worked out again, and has none
				qiplProbeBatchRelease( &spatial.nextProbes );
				spatial.nextProbes = NULL;
			}
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
	if ( spatialBakeState.load() == BAKE_DONE ) {
		// the thread is at its end: this does not wait. A page had none.
		if ( spatialBake.thread ) {
			SDL_WaitThread( spatialBake.thread, NULL );
		}
		spatialBake.thread = NULL;
		spatialBakeState.store( BAKE_NONE );

		if ( spatialBake.sceneNumber == spatial.sceneNumber && spatialBake.batch && spatialBake.saved && !spatial.sceneWaits ) {
			// (also the file of a level without paths, so that its next load does not look for them again)
			FS_WriteFile( spatial.pathFile, qiplSerializedObjectGetData( spatialBake.saved ), (int)qiplSerializedObjectGetSize( spatialBake.saved ) );
			if ( spatialBake.numProbes >= SPATIAL_MIN_PROBES ) {
				spatial.nextProbes = spatialBake.batch;
				spatial.numProbes = spatialBake.numProbes;
				spatial.bakeMsec = Q_max( 1, spatialBake.msec );
				spatial.probeSpacing = spatialBake.spacing;
				spatial.probesWait = qtrue;
				spatialBake.batch = NULL;
				Com_Printf( "v_spatial: the paths of this level are worked out, %i probes %.1f m apart in %.1f s, kept in %s\n",
					spatial.numProbes, spatial.probeSpacing, spatial.bakeMsec * 0.001f, spatial.pathFile );
			} else {
				Com_Printf( "v_spatial: no place for probes was found in this level: no sound round corners here\n" );
			}
		}
#ifdef __EMSCRIPTEN__
		else if ( spatialBake.sceneNumber == spatial.sceneNumber && spatialBake.numProbes > SPATIAL_PAGE_PROBES ) {
			spatial.refusedProbes = spatialBake.numProbes;
			S_SpatialSayTooLarge();
		}
#endif
		// a bake for a level that is gone
		if ( spatialBake.batch ) {
			qiplProbeBatchRelease( &spatialBake.batch );
		}
		if ( spatialBake.saved ) {
			qiplSerializedObjectRelease( &spatialBake.saved );
		}
	}

	if ( spatial.bakeWanted && spatialBakeState.load() == BAKE_NONE && spatial.scene && !spatial.sceneWaits && v_spatialPaths->integer ) {
		spatial.bakeWanted = qfalse;
		spatialBake.scene = qiplSceneRetain( spatial.scene );
		spatialBake.mins = spatial.mins;
		spatialBake.maxs = spatial.maxs;
		spatialBake.sceneNumber = spatial.sceneNumber;
		spatialBakeProgress.store( 0 );
		spatialBakeCancel.store( 0 );
		spatialBakeState.store( BAKE_RUNNING );
#ifdef __EMSCRIPTEN__
		// done when this returns, and picked up by the next call of this function
		S_SpatialBaker( NULL );
#else
		spatialBake.thread = SDL_CreateThread( S_SpatialBaker, "s_spatialBake", NULL );
		if ( !spatialBake.thread ) {
			qiplSceneRelease( &spatialBake.scene );
			spatialBakeState.store( BAKE_NONE );
		} else {
			Com_Printf( "v_spatial: working out the paths of this level in the background\n" );
		}
#endif
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
		S_SpatialCancelBake();
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
	spatial.refusedProbes = 0;
	if ( spatial.scene && v_spatialPaths->integer && !S_SpatialLoadPaths() ) {
		spatial.bakeWanted = qtrue;
	}

	// nothing of the level before is left to ring on
	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		if ( spatial.voices[i].active ) {
			S_SpatialResetVoice( &spatial.voices[i] );
		}
	}
	memset( spatialStreams, 0, sizeof( spatialStreams ) );
	S_SpatialResetReverb();
}

/*
===============
S_SpatialLoadMap

The collision model of a level was loaded, or the sound system starts while a level is. Runs while the
loading screen is up: building the mesh of a large level takes a moment. The level waits to be taken over,
see S_SpatialTakeScene().

A page takes it over here and now, and works its paths out: it has no thread that could do that later next
to the game, and it stands still while a level loads anyway.
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
	spatial.tableTriangles = 0;

	count = CM_ExportSolidTriangles( NULL, NULL );
	if ( !count ) {
		return;
	}
	memset( &sceneSettings, 0, sizeof( sceneSettings ) );
	sceneSettings.type = IPL_SCENETYPE_DEFAULT;
	if ( qiplSceneCreate( spatial.context, &sceneSettings, &spatial.nextScene ) != IPL_STATUS_SUCCESS ) {
		Com_Printf( "v_spatial: no scene for the level\n" );
		spatial.nextScene = NULL;
		return;
	}

	memset( &build, 0, sizeof( build ) );
	build.vertices = (IPLVector3 *)Z_Malloc( count * 3 * sizeof( IPLVector3 ), TAG_TEMP_WORKSPACE, qfalse );
	build.triangles = (IPLTriangle *)Z_Malloc( count * sizeof( IPLTriangle ), TAG_TEMP_WORKSPACE, qfalse );
	build.materials = (IPLint32 *)Z_Malloc( count * sizeof( IPLint32 ), TAG_TEMP_WORKSPACE, qfalse );
	CM_ExportSolidTriangles( S_SpatialTriangle, &build );

	// The file of the paths of this level is named after the level and after its walls: another version of a
	// level with the same name has other walls and gets a file of its own. SPATIAL_PATHS_VERSION is for files
	// that were worked out another way.
	{
		char path[MAX_QPATH], name[MAX_QPATH];

		Q_strncpyz( path, mapname, sizeof( path ) );
		COM_StripExtension( COM_SkipPath( path ), name, sizeof( name ) );
		Com_sprintf( spatial.nextPathFile, sizeof( spatial.nextPathFile ), "steamaudio/%s_%08x" SPATIAL_PATHS_VERSION ".paths", name,
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
		Com_Printf( "v_spatial: no mesh for the level\n" );
		spatial.nextMesh = NULL;
		S_SpatialReleaseScene( &spatial.nextScene, &spatial.nextMesh );
	}
	// the library keeps a copy
	Z_Free( build.vertices );
	Z_Free( build.triangles );
	Z_Free( build.materials );

	spatial.sceneMsec = Sys_Milliseconds() - start;
	if ( spatial.nextTriangles ) {
		Com_Printf( "v_spatial: %i triangles of the level in %i ms\n", spatial.nextTriangles, spatial.sceneMsec );
	}
#ifdef __EMSCRIPTEN__
	// the pass that may be in its slices is brought to its end, so that the simulator can be given the level
	S_SpatialSlice( 0 );
	S_SpatialTakeScene();
	S_SpatialThinkPaths();		// the bake, if this level has no file
	S_SpatialThinkPaths();		//	and what it made
#endif
}

/*
===============================================================================

THE SOUNDS OF A FRAME

Once a frame snd_dma.cpp names the listener and every sound that has a place in the level: the channels of the
mixer that play one, and the loop sounds. Each finds its voice again or gets a free one.

===============================================================================
*/

static void S_SpatialLetGoReflect( spatialVoice_t *v );

static void S_SpatialResetVoice( spatialVoice_t *v ) {
	S_SpatialLetGoReflect( v );
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
	float dist_mult, full, dist;
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
	dist = VectorNormalize( d );
	if ( dist < 1.0f ) {
		VectorCopy( spatial.listenerAxis[0], d );
	}

	// For its own reflections. Valve's impulse response is that of a source as loud at 1 m as it is, falling with
	// 1/r, and the model of the source starts that where the game stops playing it at full volume
	// (S_FullVolumeDistance, 1 m for a voice of the chat, whose fade is the cgame's). The sound goes in as loud
	// as the game plays it, times its distance: what comes back is to it as it would be to a real source there.
	full = S_FullVolumeDistance( entchannel, &dist_mult ) * SPATIAL_UNIT;
	v->reflectMinDistance = Q_max( 1.0f, full );
	v->ownScale = fade * Q_max( v->reflectMinDistance, dist * SPATIAL_UNIT );
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

	if ( !v_spatialReflections->integer ) {
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
is not there ahead of time. The other voices are rendered v_spatialAhead ahead of what is heard. A voice of the
chat is rendered as far as what arrived of it reaches, so it is heard that much later than the cgame would
play it itself.

===============================================================================
*/

static int S_SpatialStreamQueued( const spatialStream_t *stream ) {
	return (int)( stream->write - stream->read );
}

// What the cgame hands over of a voice, into the queue of it here. samples: 16 bit, one channel, rate of them a
// second. With no samples it only says where the voice is and how loud. origin NULL: where it was.
// Returns how many samples of it wait here, counted at its rate.
static int S_SpatialStreamWrite( spatialStream_t *stream, const vec3_t origin, float volume, const short *samples, int numSamples, int rate ) {
	float ratio;

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

/*
===============
S_SpatialVoice

From the cgame, through its engine interface (ext.S_SpatialVoice): the voice of another player, see
S_SpatialStreamWrite(). Returns how many samples of him wait here, counted at his rate, or -1: the sound of the
level does not run, the cgame plays him itself.
===============
*/
int S_SpatialVoice( int clientNum, const vec3_t origin, float volume, const short *samples, int numSamples, int rate ) {
	if ( !S_SpatialActive() || clientNum < 0 || clientNum >= MAX_CLIENTS + SPATIAL_BOXES || rate < 8000 || rate > 48000 ) {
		return -1;
	}
	// a box of the boombox: behind the stream of the own voice
	if ( clientNum >= MAX_CLIENTS ) {
		return S_SpatialStreamWrite( &spatialStreams[clientNum + 1], origin, volume, samples, numSamples, rate );
	}
	return S_SpatialStreamWrite( &spatialStreams[clientNum], origin, volume, samples, numSamples, rate );
}

/*
===============
S_SpatialOwnVoice

From the cgame (ext.S_SpatialOwnVoice): what the own player says into the voice chat, as it goes out. Nobody
hears himself from a place in the level, so the voice is not played. The room around the listener gets it, like
the other sounds of the own player, and he hears it ring.
===============
*/
void S_SpatialOwnVoice( float volume, const short *samples, int numSamples, int rate ) {
	if ( !S_SpatialActive() || !v_spatialReflections->integer || rate < 8000 || rate > 48000 ) {
		return;
	}
	S_SpatialStreamWrite( &spatialStreams[SPATIAL_OWN_STREAM], NULL, volume, samples, numSamples, rate );
}

// once a frame: every voice of the chat that still has something to say is one of the sounds of the frame
static void S_SpatialGatherStreams( void ) {
	const int now = Sys_Milliseconds();

	for ( int i = 0; i < (int)ARRAY_LEN( spatialStreams ); i++ ) {
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
			// the own voice has no place of its own: it is where the listener is, and only for the reverb
			v->sendOnly = (qboolean)( i == SPATIAL_OWN_STREAM );
			// CHAN_VOICE_GLOBAL: how loud he is at his distance is in the volume the cgame gave, also for a way
			// round a corner
			S_SpatialPlaceVoice( v, NULL, CHAN_VOICE_GLOBAL, v->sendOnly ? spatial.listenerOrigin : stream->origin, stream->volume, 1.0f,
				1.0f, qfalse );
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

// The rays of a pass. Steam Audio spreads the rays the simulator was made for (spatial.maxRays) round the
// listener in the order of their direction, and a pass of fewer takes the first of them. So the simulator is
// made for the rays it traces, see S_SpatialCreateVoices(). Until 2026-10-17 the desktop made it for
// SPATIAL_MAX_RAYS, and the 2048 of the default went into a quarter of the circle.
static int S_SpatialRays( void ) {
	return spatial.maxRays;
}

// where the listener is and how he stands, as Steam Audio has it
static void S_SpatialListener( IPLCoordinateSpace3 *listener ) {
	vec3_t right;

	VectorNegate( spatial.listenerAxis[1], right );
	S_SpatialPoint( spatial.listenerOrigin, &listener->origin );
	S_SpatialDirection( spatial.listenerAxis[0], &listener->ahead );
	S_SpatialDirection( spatial.listenerAxis[2], &listener->up );
	S_SpatialDirection( right, &listener->right );
}

// a sound lets go of its reflections of its own: they ring out on their own, and it goes on through the reverb
// of the listener
static void S_SpatialLetGoReflect( spatialVoice_t *v ) {
	if ( v->reflect ) {
		v->reflect->voice = NULL;
		v->reflect->ringing = qtrue;
		v->reflect = NULL;
	}
}

// a pass with the sound in it is done: its impulse response is its own
static qboolean S_SpatialReflectReady( const spatialReflect_t *r ) {
	return (qboolean)( r->askedPass >= 0 && spatialReflectPasses.load() > r->askedPass );
}

/*
===============
S_SpatialChooseReflects

Once a frame: the loudest v_spatialSources sounds have reflections of their own. One that has them counts
SPATIAL_REFLECT_KEEP times as loud, so that two of about the same loudness do not take them from each other
frame after frame. A sound that gets them goes on through the reverb of the listener until a pass with it in it
is done (S_SpatialReflectReady).
===============
*/
static void S_SpatialChooseReflects( void ) {
	const int want = v_spatialReflections->integer ? Com_Clampi( 0, spatial.numReflectSlots, v_spatialSources->integer ) : 0;
	qboolean chosen[SPATIAL_VOICES];

	memset( chosen, 0, sizeof( chosen ) );
	for ( int n = 0; n < want; n++ ) {
		float bestScore = 0.0f;
		int best = -1;

		for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
			const spatialVoice_t *v = &spatial.voices[i];
			float score;

			if ( chosen[i] || !v->active || v->frame != spatial.frame || v->sendOnly ) {
				continue;
			}
			score = v->gain * ( v->reflect ? SPATIAL_REFLECT_KEEP : 1.0f );
			if ( score > bestScore ) {
				bestScore = score;
				best = i;
			}
		}
		if ( best < 0 ) {
			break;
		}
		chosen[best] = qtrue;
	}

	// those that lose them first, so that the others find one free
	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		if ( !chosen[i] ) {
			S_SpatialLetGoReflect( &spatial.voices[i] );
		}
	}
	spatial.numReflects = 0;
	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		spatialVoice_t *v = &spatial.voices[i];

		if ( !chosen[i] ) {
			continue;
		}
		for ( int k = 0; !v->reflect && k < spatial.numReflectSlots; k++ ) {
			spatialReflect_t *r = &spatial.reflects[k];

			if ( !r->voice && !r->ringing ) {
				r->voice = v;
				r->askedPass = -1;
				v->reflect = r;
			}
		}
		if ( v->reflect ) {
			spatial.numReflects++;
		}
	}
}

static void S_SpatialSimulate( void ) {
	IPLSimulationSharedInputs shared;

	// the reflections get theirs in their own thread, see S_SpatialPassInputs()
	memset( &shared, 0, sizeof( shared ) );
	S_SpatialListener( &shared.listener );
	qiplSimulatorSetSharedInputs( spatial.simulator, (IPLSimulationFlags)( IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_PATHING ), &shared );
	S_SpatialChooseReflects();

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
			v->pathing = (qboolean)( spatial.probes && v_spatialPaths->integer );
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
block. So every voice is rendered ahead on its own, as far as v_spatialAhead says, and added into a ring of
finished sound by the time of the mixer. The mixer adds what the ring has for the samples it paints, as often
as it paints them. A voice that starts adds itself to what the others already put there, so it is heard as
soon as a sound of the old path would be. What a voice is given later (where it is, what is in the way) only
reaches the blocks that are not rendered yet: that is the delay v_spatialAhead stands for.

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
			v->own[( c + i ) & ( SPATIAL_DRY - 1 )] = 0.0f;
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
		send = v_spatialHrtf->integer ? ( SPATIAL_GAIN_VOICE_HRTF / SPATIAL_GAIN_HRTF ) : ( SPATIAL_GAIN_VOICE_PANNING / SPATIAL_GAIN_PANNING );
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
			params.binaural = v_spatialHrtf->integer ? IPL_TRUE : IPL_FALSE;
			params.hrtf = spatial.hrtf;
			// what bending round the corners costs: all of it, or only how much duller the sound gets
			params.normalizeEQ = v_spatialPathLoss->integer ? IPL_FALSE : IPL_TRUE;
			VectorNegate( spatial.listenerAxis[1], rightAxis );
			S_SpatialPoint( spatial.listenerOrigin, &params.listener.origin );
			S_SpatialDirection( spatial.listenerAxis[0], &params.listener.ahead );
			S_SpatialDirection( spatial.listenerAxis[2], &params.listener.up );
			S_SpatialDirection( rightAxis, &params.listener.right );
			qiplPathEffectApply( v->path, &params, &pathIn, &pathOut );
			pathed = qtrue;
		}
	}

	// What the walls in the way left of it, and what came round them, is what the room around the listener
	// gets to hear. Its own reflections get the sound as it leaves it: their impulse response has the walls.
	for ( int i = 0; i < SPATIAL_FRAME; i++ ) {
		v->dry[( c + i ) & ( SPATIAL_DRY - 1 )] = ( direct[i] + ( pathed ? ( pathLeft[i] + pathRight[i] ) * 0.5f * hidden : 0.0f ) ) * send;
		v->own[( c + i ) & ( SPATIAL_DRY - 1 )] = whole[i] * send * v->ownScale;
	}
	volume = ( v->stream ? 1.0f : send ) * ( 32768.0f * 256.0f );
	if ( v_spatialHrtf->integer ) {
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
	if ( v->shock && v_spatialShock->value > 0.0f ) {
		const float *k = spatial.shockCoeffs;
		const float through = v->directParams.occlusion + ( 1.0f - v->directParams.occlusion ) * v->directParams.transmission[0];
		const float throughFrom = fresh ? through : v->shockThrough;
		const float throughStep = ( through - throughFrom ) * ( 1.0f / SPATIAL_FRAME );
		const float shock = send * v_spatialShock->value * SPATIAL_SHOCK_GAIN * ( 32768.0f * 256.0f );
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
the level (v_spatialRays of them, v_spatialBounces times off a wall) and makes of what comes back the impulse
response of the place: how the room rings. Every sound in the level is sent through that one reverb, as it
arrives after the walls in its way. A pass takes longer than a frame, so the game only asks for one and picks
up what the last one found.

The reverb is rendered block after block behind the voices, as far as all of them are rendered, into the same
ring.

===============================================================================
*/

// In the thread of the reflections, before a pass: what spatialPass says it is to start from.
static void S_SpatialPassInputs( void ) {
	IPLSimulationSharedInputs shared;
	IPLSimulationInputs inputs;

	memset( &shared, 0, sizeof( shared ) );
	shared.listener = spatialPass.listener;
	shared.numRays = spatialPass.rays;
	shared.numBounces = spatialPass.bounces;
	shared.duration = SPATIAL_REVERB_SECONDS;
	shared.order = SPATIAL_REVERB_ORDER;
	shared.irradianceMinDistance = 1.0f;
	qiplSimulatorSetSharedInputs( spatial.simulator, IPL_SIMULATIONFLAGS_REFLECTIONS, &shared );

	memset( &inputs, 0, sizeof( inputs ) );
	if ( spatialPass.reverb ) {
		inputs.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
		inputs.source = spatialPass.listener;
		inputs.reverbScale[0] = inputs.reverbScale[1] = inputs.reverbScale[2] = 1.0f;
	}
	qiplSourceSetInputs( spatial.reverbSource, IPL_SIMULATIONFLAGS_REFLECTIONS, &inputs );

	for ( int i = 0; i < spatial.numReflectSlots; i++ ) {
		memset( &inputs, 0, sizeof( inputs ) );
		if ( spatialPass.own[i] ) {
			inputs.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
			inputs.source = spatialPass.source[i];
			inputs.reverbScale[0] = inputs.reverbScale[1] = inputs.reverbScale[2] = 1.0f;
			inputs.distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_INVERSEDISTANCE;
			inputs.distanceAttenuationModel.minDistance = spatialPass.minDistance[i];
		}
		qiplSourceSetInputs( spatial.reflects[i].source, IPL_SIMULATIONFLAGS_REFLECTIONS, &inputs );
	}
}

// In the thread of the game, while the thread of the reflections is idle: what the next pass is to start from.
static void S_SpatialFillPass( void ) {
	const int passes = spatialReflectPasses.load();

	S_SpatialListener( &spatialPass.listener );
	spatialPass.rays = S_SpatialRays();
	spatialPass.bounces = Com_Clampi( 1, 64, v_spatialBounces->integer );
	spatialPass.reverb = v_spatialReflections->integer ? qtrue : qfalse;
	for ( int i = 0; i < spatial.numReflectSlots; i++ ) {
		spatialReflect_t *r = &spatial.reflects[i];
		const spatialVoice_t *v = r->voice;

		spatialPass.own[i] = (qboolean)( v != NULL );
		if ( !v ) {
			continue;
		}
		memset( &spatialPass.source[i], 0, sizeof( spatialPass.source[i] ) );
		S_SpatialPoint( v->origin, &spatialPass.source[i].origin );
		spatialPass.source[i].right.x = 1.0f;
		spatialPass.source[i].up.y = 1.0f;
		spatialPass.source[i].ahead.z = -1.0f;
		spatialPass.minDistance[i] = v->reflectMinDistance;
		// the pass that is asked now is the first with it in it
		if ( r->askedPass < 0 ) {
			r->askedPass = passes;
		}
	}
}

#ifdef __EMSCRIPTEN__

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

#else

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
				S_SpatialPassInputs();
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

#endif

// The next pass of the thread, if the last one is done and not too long ago: the reflections around the
// listener, and the ways of the sounds round the corners. Never while a level waits to be taken over.
static void S_SpatialAskPass( void ) {
	const int now = Sys_Milliseconds();
	qboolean paths = (qboolean)( spatial.probes && v_spatialPaths->integer );
	int idle = WORKER_IDLE;

#ifdef __EMSCRIPTEN__
	// A page: the ways round the corners are looked up in what was worked out for the level, which is little
	// work (20 microseconds for two sounds behind a wall). Done here and as often as the desktop does it, and
	// not as a part of a pass, which takes a page many frames.
	if ( paths && !spatial.sceneWaits && now - spatial.pathsAsked >= SPATIAL_REFLECT_MSEC ) {
		const int start = S_SpatialUsec();

		spatial.pathsAsked = now;
		qiplSimulatorRunPathing( spatial.simulator );
		spatial.costUsec[COST_PASS] += S_SpatialUsec() - start;
	}
	paths = qfalse;
#endif
	if ( ( !v_spatialReflections->integer && !paths ) || spatial.sceneWaits || now - spatial.reflectAsked < SPATIAL_REFLECT_MSEC ) {
		return;
	}
	// the thread reads these only after it was woken
	if ( spatialWorkerState.load() == WORKER_IDLE ) {
		spatialPassReflections.store( v_spatialReflections->integer ? 1 : 0 );
		spatialPassPaths.store( paths ? 1 : 0 );
		S_SpatialFillPass();
		if ( spatialWorkerState.compare_exchange_strong( idle, WORKER_ASKED ) ) {
			spatial.reflectAsked = now;
#ifndef __EMSCRIPTEN__
			SDL_SemPost( spatial.workerWake );
#endif
		}
	}
}

// one block of the reverb from the time c into the ring, with the reflections of the sounds that have their own
static void S_SpatialRenderReverb( int c ) {
	static float in[SPATIAL_FRAME], ambisonics[SPATIAL_REVERB_CHANNELS][SPATIAL_FRAME], left[SPATIAL_FRAME], right[SPATIAL_FRAME];
	static float own[SPATIAL_FRAME], scratch[SPATIAL_REVERB_CHANNELS][SPATIAL_FRAME], mixed[SPATIAL_REVERB_CHANNELS][SPATIAL_FRAME];
	float *inData = in, *ownData = own, *ambisonicsData[SPATIAL_REVERB_CHANNELS], *stereoData[2] = { left, right };
	float *scratchData[SPATIAL_REVERB_CHANNELS], *mixedData[SPATIAL_REVERB_CHANNELS];
	IPLAudioBuffer inBuffer = { 1, SPATIAL_FRAME, &inData };
	IPLAudioBuffer ownBuffer = { 1, SPATIAL_FRAME, &ownData };
	IPLAudioBuffer ambisonicsBuffer = { SPATIAL_REVERB_CHANNELS, SPATIAL_FRAME, ambisonicsData };
	IPLAudioBuffer scratchBuffer = { SPATIAL_REVERB_CHANNELS, SPATIAL_FRAME, scratchData };
	IPLAudioBuffer mixedBuffer = { SPATIAL_REVERB_CHANNELS, SPATIAL_FRAME, mixedData };
	IPLAudioBuffer out = { 2, SPATIAL_FRAME, stereoData };
	IPLSimulationOutputs outputs;
	IPLReflectionEffectParams params;
	IPLAmbisonicsDecodeEffectParams decode;
	vec3_t rightAxis;
	float volume;

	for ( int i = 0; i < SPATIAL_REVERB_CHANNELS; i++ ) {
		ambisonicsData[i] = ambisonics[i];
		scratchData[i] = scratch[i];
		mixedData[i] = mixed[i];
	}

	// Every voice gives the next block of what it has rendered, from its first sample on. A sound that starts is
	// put into the ring where the mixer paints, which is behind where the reverb is by then: its start would
	// never get here if the reverb took the voices by the time of the mixer. So the reverb of a sound comes a few
	// blocks late (up to v_spatialAhead), which a room does to a sound anyway. A voice with reflections of its
	// own gives its block to their convolution, which adds it into the mixer of them.
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
		memset( &outputs, 0, sizeof( outputs ) );
		if ( v->reflect && S_SpatialReflectReady( v->reflect ) ) {
			qiplSourceGetOutputs( v->reflect->source, IPL_SIMULATIONFLAGS_REFLECTIONS, &outputs );
		}
		if ( outputs.reflections.ir ) {
			memset( own, 0, sizeof( own ) );
			for ( int k = 0; k < count; k++ ) {
				own[k] = v->own[( v->dryRead + k ) & ( SPATIAL_DRY - 1 )];
			}
			params = outputs.reflections;
			params.type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
			params.numChannels = SPATIAL_REVERB_CHANNELS;
			params.irSize = (int)( SPATIAL_REVERB_SECONDS * spatial.audio.samplingRate );
			qiplReflectionEffectApply( v->reflect->effect, &params, &ownBuffer, &scratchBuffer, spatial.reflectMixer );
		} else {
			for ( int k = 0; k < count; k++ ) {
				in[k] += v->dry[( v->dryRead + k ) & ( SPATIAL_DRY - 1 )];
			}
		}
		v->dryRead += count;
	}
	// what rings on of the sounds that let go of theirs
	for ( int i = 0; i < spatial.numReflectSlots; i++ ) {
		spatialReflect_t *r = &spatial.reflects[i];

		if ( r->ringing && qiplReflectionEffectGetTail( r->effect, &scratchBuffer, spatial.reflectMixer ) == IPL_AUDIOEFFECTSTATE_TAILCOMPLETE ) {
			qiplReflectionEffectReset( r->effect );
			r->ringing = qfalse;
		}
	}

	// the room where the listener stands, of what the last finished pass found. Before the first one there is
	// nothing to ring.
	memset( &outputs, 0, sizeof( outputs ) );
	qiplSourceGetOutputs( spatial.reverbSource, IPL_SIMULATIONFLAGS_REFLECTIONS, &outputs );
	params = outputs.reflections;
	if ( params.ir ) {
		params.type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
		params.numChannels = SPATIAL_REVERB_CHANNELS;
		params.irSize = (int)( SPATIAL_REVERB_SECONDS * spatial.audio.samplingRate );
		qiplReflectionEffectApply( spatial.reverbEffect, &params, &inBuffer, &ambisonicsBuffer, NULL );
	} else {
		memset( ambisonics, 0, sizeof( ambisonics ) );
	}
	if ( spatial.reflectMixer ) {
		memset( &params, 0, sizeof( params ) );
		params.type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
		params.numChannels = SPATIAL_REVERB_CHANNELS;
		params.irSize = (int)( SPATIAL_REVERB_SECONDS * spatial.audio.samplingRate );
		qiplReflectionMixerApply( spatial.reflectMixer, &params, &mixedBuffer );
		for ( int i = 0; i < SPATIAL_REVERB_CHANNELS; i++ ) {
			for ( int k = 0; k < SPATIAL_FRAME; k++ ) {
				ambisonics[i][k] += mixed[i][k];
			}
		}
	}

	// what comes back comes from all sides of the room: turned to how the listener stands in it
	memset( &decode, 0, sizeof( decode ) );
	decode.order = SPATIAL_REVERB_ORDER;
	decode.hrtf = spatial.hrtf;
	decode.binaural = v_spatialHrtf->integer ? IPL_TRUE : IPL_FALSE;
	VectorNegate( spatial.listenerAxis[1], rightAxis );
	S_SpatialDirection( spatial.listenerAxis[0], &decode.orientation.ahead );
	S_SpatialDirection( spatial.listenerAxis[2], &decode.orientation.up );
	S_SpatialDirection( rightAxis, &decode.orientation.right );
	qiplAmbisonicsDecodeEffectApply( spatial.reverbDecode, &decode, &ambisonicsBuffer, &out );

	S_SpatialRingExtend( c + SPATIAL_FRAME );
	// how loud each voice is went in with what it gave
	volume = v_spatialReverb->value * ( v_spatialHrtf->integer ? SPATIAL_GAIN_HRTF : SPATIAL_GAIN_PANNING ) * ( 32768.0f * 256.0f );
	for ( int i = 0; i < SPATIAL_FRAME; i++ ) {
		float *slot = spatial.ring[( c + i ) & ( SPATIAL_RING - 1 )];

		slot[0] += left[i] * volume;
		slot[1] += right[i] * volume;
	}
}

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

static void S_SpatialCaptureEnd( qboolean write ) {
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
static void S_SpatialCapture( int from ) {
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

static void S_SpatialCapture_f( void ) {
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

/*
===============
S_SpatialEndFrame

All sounds of the frame are named. from: the first sample the mixer paints now, target: up to which sample the
voices have to be in the ring.
===============
*/
void S_SpatialEndFrame( int from, int target ) {
	const int start = S_SpatialUsec();
	int simulated, voicesDone;

	int limit = target;

	// the time of the mixer went back (the sound was stopped and started again): everything from the start
	if ( from < spatial.lastPaint ) {
		for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
			if ( spatial.voices[i].active ) {
				S_SpatialResetVoice( &spatial.voices[i] );
			}
		}
		S_SpatialResetReverb();
		spatial.reverbRendered = from;
		spatial.ringEnd = from;
	}
	spatial.lastPaint = from;
	S_SpatialCapture( from );
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
	voicesDone = S_SpatialUsec();
	if ( v_spatialReflections->integer ) {
		if ( spatial.reverbRendered < from ) {
			spatial.reverbRendered = from;
		}
		while ( spatial.reverbRendered + SPATIAL_FRAME <= limit ) {
			S_SpatialRenderReverb( spatial.reverbRendered );
			spatial.reverbRendered += SPATIAL_FRAME;
		}
		spatial.costUsec[COST_REVERB] += S_SpatialUsec() - voicesDone;
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

	// what this frame took, with what S_SpatialUpdate() did of a pass before it (a page)
	spatial.costFrames++;
	spatial.costUsec[COST_SIMULATE] += spatial.simulateUsec;
	spatial.costUsec[COST_VOICES] += voicesDone - simulated;
	spatial.costFrameUsec += S_SpatialUsec() - start;
	spatial.costPeakUsec = Q_max( spatial.costPeakUsec, spatial.costFrameUsec );
	spatial.costFrameUsec = 0;
	spatial.costVoices += spatial.numVoices;
	spatial.costPeakVoices = Q_max( spatial.costPeakVoices, spatial.numVoices );
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
	return Com_Clamp( 0.08f, 0.5f, v_spatialAhead->value );
}

#ifdef __EMSCRIPTEN__
// microseconds of a frame for the passes
static int S_SpatialBudget( void ) {
	return (int)( Com_Clamp( 0.1f, 8.0f, v_spatialBudget->value ) * 1000.0f );
}
#endif

static void S_SpatialInfo_f( void ) {
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
			S_SpatialRays(), Com_Clampi( 1, 64, v_spatialBounces->integer ), spatialReflectUsec.load() / 1000, spatialReflectPasses.load(),
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
	} else if ( spatialBakeState.load() == BAKE_RUNNING && spatialBake.sceneNumber == spatial.sceneNumber ) {
		Com_Printf( "paths: being worked out for this level, %i %%\n", spatialBakeProgress.load() );
	} else if ( spatial.probes ) {
		if ( spatial.bakeMsec ) {
			Com_Printf( "paths: %i probes %.1f m apart, worked out in %.1f s, kept in %s\n", spatial.numProbes, spatial.probeSpacing,
				spatial.bakeMsec * 0.001f, spatial.pathFile );
		} else {
			Com_Printf( "paths: %i probes from %s\n", spatial.numProbes, spatial.pathFile );
		}
	} else if ( spatial.refusedProbes ) {
		Com_Printf( "paths: none, the level has %i probes and a page works them out for up to %i\n", spatial.refusedProbes, SPATIAL_PAGE_PROBES );
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
		Com_Printf( "  %-40s at %.0f %.0f %.0f, %5.0f units away, gain %.2f, %3.0f %% in sight, through what hides it: %.3f %.3f %.3f%s%s\n",
			v->stream ? va( "voice of player %i", (int)( v->stream - spatialStreams ) ) : v->sfx->sSoundName,
			v->origin[0], v->origin[1], v->origin[2], Distance( v->origin, spatial.listenerOrigin ), v->gain, v->directParams.occlusion * 100.0f,
			v->directParams.transmission[0], v->directParams.transmission[1], v->directParams.transmission[2],
			( v->shock && v_spatialShock->value > 0.0f ) ? ", thump" : "",
			v->reflect ? ( S_SpatialReflectReady( v->reflect ) ? ", own reflections" : ", own reflections soon" ) : "" );
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
		S_SpatialCancelBake();
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

	S_SpatialCaptureEnd( qfalse );
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
	// A pass traces the first rays of those the simulator has, which are not spread round the listener unless
	// it traces them all (S_SpatialRays): the simulator is made for what it traces, and v_spatialRays is read here.
	spatial.maxRays = Com_Clampi( 256, SPATIAL_MAX_RAYS, v_spatialRays->integer );
	v_spatialRays->modified = qfalse;
	simulation.maxNumRays = spatial.maxRays;
	simulation.numDiffuseSamples = SPATIAL_DIFFUSE_SAMPLES;
	simulation.maxDuration = SPATIAL_REVERB_SECONDS;
	simulation.maxOrder = SPATIAL_REVERB_ORDER;
	simulation.maxNumSources = 1 + SPATIAL_REFLECT_SLOTS;	// sources with reflections: the one at the listener and the slots
	// threads Steam Audio shares a pass of the rays between, inside the one of S_SpatialWorker()
	simulation.numThreads = SPATIAL_THREADS;
	simulation.numVisSamples = 4;
	simulation.samplingRate = spatial.audio.samplingRate;
	simulation.frameSize = spatial.audio.frameSize;
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
	v_spatialAhead = Cvar_Get( "v_spatialAhead", "0.1", CVAR_ARCHIVE, "v_spatial: seconds of sound rendered ahead. Less follows a turn of the head sooner, more survives a longer hitch" );
	v_spatialReflections = Cvar_Get( "v_spatialReflections", "1", CVAR_ARCHIVE, "v_spatial: 1 the room around the listener rings, worked out with rays. 0 no reverb" );
	v_spatialReverb = Cvar_Get( "v_spatialReverb", "1", CVAR_ARCHIVE, "v_spatial: how loud the reverb is" );
	v_spatialRays = Cvar_Get( "v_spatialRays", SPATIAL_DEFAULT_RAYS, CVAR_ARCHIVE, "v_spatial: rays of a pass of the reflections, 256 to 8192. From the next snd_restart on" );
#ifdef __EMSCRIPTEN__
	v_spatialPaths = Cvar_Get( "v_spatialPaths", "1", CVAR_ARCHIVE, "v_spatial: 1 a sound comes round corners. Worked out once per level while it loads, in a browser not for a large level" );
	v_spatialBudget = Cvar_Get( "v_spatialBudget", "0.75", CVAR_ARCHIVE, "v_spatial in a browser: milliseconds of a frame the passes of the reflections take on average, 0.1 to 8. More: the reverb follows the listener sooner" );
#else
	v_spatialPaths = Cvar_Get( "v_spatialPaths", "1", CVAR_ARCHIVE, "v_spatial: 1 a sound comes round corners. Worked out once per level in the background" );
#endif
	v_spatialBounces = Cvar_Get( "v_spatialBounces", SPATIAL_DEFAULT_BOUNCES, CVAR_ARCHIVE, "v_spatial: how often a ray is followed off a wall" );
	v_spatialPathLoss = Cvar_Get( "v_spatialPathLoss", "1", CVAR_ARCHIVE, "v_spatial: 1 a sound loses on its way round a corner what Steam Audio works out. 0 it only gets duller, and is as loud as the game makes a sound that far away" );
	v_spatialSources = Cvar_Get( "v_spatialSources", "8", CVAR_ARCHIVE, "v_spatial: how many of the loudest sounds have reflections of their own, 0 to 16. The others ring with the room of the listener" );
	v_spatialShock = Cvar_Get( "v_spatialShock", "1", CVAR_ARCHIVE, "v_spatial: how strong the thump of an explosion is, its lows in both ears. 0 none" );
	Cmd_AddCommand( "v_spatialInfo", S_SpatialInfo_f, "What v_spatial is doing" );
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
	if ( qiplContextCreate( &contextSettings, &spatial.context ) != IPL_STATUS_SUCCESS ) {
		Com_Printf( "v_spatial: %s does not go with version %i.%i.%i of the header, the sound stays as it is\n", spatial.libraryPath,
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
		Com_Printf( "v_spatial: no direction data (HRTF) for %i samples a second, the sound stays as it is\n", dma.speed );
		qiplContextRelease( &spatial.context );
		S_SpatialUnload();
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
		if ( v_spatialRays->modified ) {
			v_spatialRays->modified = qfalse;
			if ( Com_Clampi( 256, SPATIAL_MAX_RAYS, v_spatialRays->integer ) != spatial.maxRays ) {
				Com_Printf( "v_spatialRays: taken when the sound system starts (snd_restart), the passes go on with %i rays\n", spatial.maxRays );
			}
		}
#ifdef __EMSCRIPTEN__
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
			if ( v_spatialPaths->integer && spatial.scene && !spatial.probes && !spatial.probesWait ) {
				if ( spatial.refusedProbes ) {
					S_SpatialSayTooLarge();
				} else {
					Com_Printf( "v_spatialPaths: a page works the paths of a level out while it loads, this one has them from its next load on\n" );
				}
			}
		}
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
void		S_SpatialOwnVoice( float volume, const short *samples, int numSamples, int rate ) {}

#endif // JP_SPATIAL
