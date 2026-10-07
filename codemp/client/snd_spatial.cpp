/*
===========================================================================
Copyright (C) 2013 - 2015, OpenJK contributors

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
	X( iplPanningEffectApply )

#define X( name )	static decltype( &name ) q##name;
SPATIAL_FUNCTIONS( X )
#undef X

#if defined(_WIN32) && !defined(_WIN64)
// The 32 bit library for Windows exports its functions under their __stdcall names: _name@N, N the bytes its
// arguments take on the stack, each rounded up to 4. N is counted from the type of the declaration.
template<typename T> struct spatialArgBytes_t;
template<typename R> struct spatialArgBytes_t<R (__stdcall *)()> {
	enum { value = 0 };
};
template<typename R, typename A, typename... B> struct spatialArgBytes_t<R (__stdcall *)( A, B... )> {
	enum { value = (int)( ( sizeof( A ) + 3 ) & ~(size_t)3 ) + spatialArgBytes_t<R (__stdcall *)( B... )>::value };
};
#define SPATIAL_EXPORT( name )	va( "_%s@%i", #name, (int)spatialArgBytes_t<decltype( &name )>::value )
#else
#define SPATIAL_EXPORT( name )	#name
#endif

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

// One sound in the level. The effects of Steam Audio remember what they did to the block before, so a sound
// keeps its voice for as long as it plays and each voice is rendered once and in order, see "Rendering".
typedef struct spatialVoice_s {
	qboolean	active;
	int			frame;				// spatial.frame of the last S_SpatialAdd*() that named it

	// what it plays
	channel_t	*channel;			// a channel of the mixer, NULL: a loop sound. Only looked into in its frame
	int			entnum;
	sfx_t		*sfx;
	qboolean	loop;
	int			startSample;
	float		step;				// samples of the sound per sample that is heard, above 1: doppler

	// where, and how loud the game wants it at that distance
	vec3_t		origin;
	IPLVector3	direction;			// from the listener, in his axes
	float		gain;
	float		renderedGain;		// the gain the last block ended with: the next one moves on from there
	qboolean	voiceVolume;		// s_volumeVoice and not s_volume

	int			rendered;			// up to which sample of the mixer's time it is in the ring
	qboolean	fresh;				// nothing of it is rendered yet

	IPLSource				source;
	IPLDirectEffect			direct;
	IPLBinauralEffect		binaural;
	IPLPanningEffect		panning;
	IPLDirectEffectParams	directParams;
} spatialVoice_t;

static cvar_t	*s_spatial;
static cvar_t	*s_spatialHrtf;
static cvar_t	*s_spatialAhead;

static struct {
	void		*library;
	char		libraryPath[MAX_OSPATH];
	qboolean	active;

	IPLContext			context;
	IPLAudioSettings	audio;
	IPLHRTF				hrtf;
	IPLSimulator		simulator;

	// the level
	IPLScene			scene;
	IPLStaticMesh		mesh;
	int					numTriangles;
	int					materialTriangles[SPATIAL_NUM_MATERIALS];
	int					sceneMsec;

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
#define X( name )	q##name = (decltype( q##name ))Sys_LoadFunction( spatial.library, SPATIAL_EXPORT( name ) ); if ( !q##name ) { missing = #name; }
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
	build->triangles[n].indices[0] = n * 3;
	build->triangles[n].indices[1] = n * 3 + 1;
	build->triangles[n].indices[2] = n * 3 + 2;
	build->materials[n] = material;
	spatial.materialTriangles[material]++;
}

static void S_SpatialFreeMap( void ) {
	if ( spatial.simulator && spatial.scene ) {
		// the simulator lets go of the level
		qiplSimulatorSetScene( spatial.simulator, NULL );
		qiplSimulatorCommit( spatial.simulator );
	}
	if ( spatial.mesh ) {
		qiplStaticMeshRelease( &spatial.mesh );
	}
	if ( spatial.scene ) {
		qiplSceneRelease( &spatial.scene );
	}
	spatial.numTriangles = 0;
	memset( spatial.materialTriangles, 0, sizeof( spatial.materialTriangles ) );
}

/*
===============
S_SpatialLoadMap

The collision model of a level was loaded, or the sound system starts while a level is. Runs while the
loading screen is up: building the mesh of a large level takes a moment.
===============
*/
void S_SpatialLoadMap( void ) {
	const int start = Sys_Milliseconds();
	spatialMeshBuild_t build;
	IPLSceneSettings sceneSettings;
	IPLStaticMeshSettings meshSettings;
	IPLMaterial materials[SPATIAL_NUM_MATERIALS];
	int count;

	if ( !spatial.active ) {
		return;
	}
	S_SpatialFreeMap();

	count = CM_ExportSolidTriangles( NULL, NULL );
	if ( !count ) {
		return;
	}
	memset( &sceneSettings, 0, sizeof( sceneSettings ) );
	sceneSettings.type = IPL_SCENETYPE_DEFAULT;
	if ( qiplSceneCreate( spatial.context, &sceneSettings, &spatial.scene ) != IPL_STATUS_SUCCESS ) {
		Com_Printf( "s_spatial: no scene for the level\n" );
		spatial.scene = NULL;
		return;
	}

	memset( &build, 0, sizeof( build ) );
	build.vertices = (IPLVector3 *)Z_Malloc( count * 3 * sizeof( IPLVector3 ), TAG_TEMP_WORKSPACE, qfalse );
	build.triangles = (IPLTriangle *)Z_Malloc( count * sizeof( IPLTriangle ), TAG_TEMP_WORKSPACE, qfalse );
	build.materials = (IPLint32 *)Z_Malloc( count * sizeof( IPLint32 ), TAG_TEMP_WORKSPACE, qfalse );
	CM_ExportSolidTriangles( S_SpatialTriangle, &build );

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
	if ( qiplStaticMeshCreate( spatial.scene, &meshSettings, &spatial.mesh ) == IPL_STATUS_SUCCESS ) {
		qiplStaticMeshAdd( spatial.mesh, spatial.scene );
		qiplSceneCommit( spatial.scene );
		qiplSimulatorSetScene( spatial.simulator, spatial.scene );
		qiplSimulatorCommit( spatial.simulator );
		spatial.numTriangles = build.count;
	} else {
		Com_Printf( "s_spatial: no mesh for the level\n" );
		spatial.mesh = NULL;
		S_SpatialFreeMap();
	}
	// the library keeps a copy
	Z_Free( build.vertices );
	Z_Free( build.triangles );
	Z_Free( build.materials );

	spatial.sceneMsec = Sys_Milliseconds() - start;
	if ( spatial.numTriangles ) {
		Com_Printf( "s_spatial: %i triangles of the level in %i ms\n", spatial.numTriangles, spatial.sceneMsec );
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
	qiplDirectEffectReset( v->direct );
	qiplBinauralEffectReset( v->binaural );
	qiplPanningEffectReset( v->panning );
	v->active = qfalse;
	v->channel = NULL;
	v->sfx = NULL;
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
static spatialVoice_t *S_SpatialFindVoice( channel_t *channel, int entnum, sfx_t *sfx, int startSample ) {
	spatialVoice_t *unused = NULL;

	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		spatialVoice_t *v = &spatial.voices[i];

		if ( !v->active ) {
			if ( !unused ) {
				unused = v;
			}
			continue;
		}
		if ( v->sfx != sfx ) {
			continue;
		}
		if ( channel ? ( !v->loop && v->channel == channel && v->startSample == startSample ) : ( v->loop && v->entnum == entnum ) ) {
			return v;
		}
	}
	if ( unused ) {
		unused->active = qtrue;
		unused->fresh = qtrue;
		unused->loop = channel ? qfalse : qtrue;
		unused->entnum = entnum;
		unused->sfx = sfx;
		unused->startSample = startSample;
		unused->renderedGain = 0.0f;
	}
	return unused;
}

static void S_SpatialPlaceVoice( spatialVoice_t *v, channel_t *channel, const vec3_t origin, float gain, float step, qboolean voiceVolume ) {
	vec3_t d;

	v->frame = spatial.frame;
	v->channel = channel;
	v->gain = gain;
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
qboolean S_SpatialAddChannel( channel_t *ch, const vec3_t origin, float gain, qboolean voiceVolume ) {
	spatialVoice_t *v = S_SpatialFindVoice( ch, ch->entnum, ch->thesfx, (int)ch->startSample );

	if ( !v ) {
		return qfalse;
	}
	S_SpatialPlaceVoice( v, ch, origin, gain, ( ch->doppler && ch->dopplerScale > 1 ) ? ch->dopplerScale : 1.0f, voiceVolume );
	return qtrue;
}

void S_SpatialAddLoop( int entnum, sfx_t *sfx, const vec3_t origin, float gain, float step ) {
	spatialVoice_t *v = S_SpatialFindVoice( NULL, entnum, sfx, 0 );

	if ( v ) {
		S_SpatialPlaceVoice( v, NULL, origin, gain, step, qfalse );
	}
}

/*
===============================================================================

SIMULATION

What the level does to each sound on its straight way to the listener: how much of the source is hidden, and
what the walls in between let through. Cheap, every frame, in the thread of the game.

===============================================================================
*/

static void S_SpatialSimulate( void ) {
	IPLSimulationSharedInputs shared;
	vec3_t right;

	memset( &shared, 0, sizeof( shared ) );
	VectorNegate( spatial.listenerAxis[1], right );
	S_SpatialPoint( spatial.listenerOrigin, &shared.listener.origin );
	S_SpatialDirection( spatial.listenerAxis[0], &shared.listener.ahead );
	S_SpatialDirection( spatial.listenerAxis[2], &shared.listener.up );
	S_SpatialDirection( right, &shared.listener.right );
	qiplSimulatorSetSharedInputs( spatial.simulator, IPL_SIMULATIONFLAGS_DIRECT, &shared );

	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		spatialVoice_t *v = &spatial.voices[i];
		IPLSimulationInputs inputs;

		// a voice that plays nothing is not looked at
		memset( &inputs, 0, sizeof( inputs ) );
		if ( v->active && v->frame == spatial.frame ) {
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
		}
		qiplSourceSetInputs( v->source, IPL_SIMULATIONFLAGS_DIRECT, &inputs );
	}

	qiplSimulatorRunDirect( spatial.simulator );

	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		spatialVoice_t *v = &spatial.voices[i];
		IPLSimulationOutputs outputs;

		if ( !v->active || v->frame != spatial.frame ) {
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
	float *monoData = mono, *directData = direct, *stereoData[2] = { left, right };
	IPLAudioBuffer in = { 1, SPATIAL_FRAME, &monoData };
	IPLAudioBuffer mid = { 1, SPATIAL_FRAME, &directData };
	IPLAudioBuffer out = { 2, SPATIAL_FRAME, stereoData };
	const int c = v->rendered;
	const float from = v->renderedGain, step = ( gain - v->renderedGain ) * ( 1.0f / SPATIAL_FRAME );
	float volume;

	v->rendered += SPATIAL_FRAME;
	v->renderedGain = gain;
	v->fresh = qfalse;
	if ( !S_SpatialSamples( v, c, mono ) ) {
		return;
	}
	for ( int i = 0; i < SPATIAL_FRAME; i++ ) {
		mono[i] *= from + step * i;
	}

	qiplDirectEffectApply( v->direct, &v->directParams, &in, &mid );
	volume = ( v->voiceVolume ? s_volumeVoice->value : s_volume->value ) * ( 32768.0f * 256.0f );
	if ( s_spatialHrtf->integer ) {
		IPLBinauralEffectParams params;

		memset( &params, 0, sizeof( params ) );
		params.direction = v->direction;
		params.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
		params.spatialBlend = 1.0f;
		params.hrtf = spatial.hrtf;
		qiplBinauralEffectApply( v->binaural, &params, &mid, &out );
		volume *= SPATIAL_GAIN_HRTF;
	} else {
		IPLPanningEffectParams params;

		params.direction = v->direction;
		qiplPanningEffectApply( v->panning, &params, &mid, &out );
		volume *= SPATIAL_GAIN_PANNING;
	}

	// into the ring, in what the paint buffer counts in: 16 bit samples times 256
	S_SpatialRingExtend( c + SPATIAL_FRAME );
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

	// the time of the mixer went back (the sound was stopped and started again): everything from the start
	if ( from < spatial.lastPaint ) {
		for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
			if ( spatial.voices[i].active ) {
				S_SpatialResetVoice( &spatial.voices[i] );
			}
		}
		spatial.ringEnd = from;
	}
	spatial.lastPaint = from;
	if ( spatial.ringEnd < from ) {
		spatial.ringEnd = from;
	}

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
			S_SpatialResetVoice( v );
			continue;
		}
		spatial.numVoices++;
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
		}
		while ( v->rendered < target ) {
			S_SpatialRenderBlock( v, v->gain );
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
	Com_Printf( "listener at %.0f %.0f %.0f\n", spatial.listenerOrigin[0], spatial.listenerOrigin[1], spatial.listenerOrigin[2] );
	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		const spatialVoice_t *v = &spatial.voices[i];

		if ( v->active && v->frame == spatial.frame ) {
			Com_Printf( "  %-40s at %.0f %.0f %.0f, %5.0f units away, gain %.2f, %3.0f %% in sight, through what hides it: %.3f %.3f %.3f\n", v->sfx->sSoundName,
				v->origin[0], v->origin[1], v->origin[2], Distance( v->origin, spatial.listenerOrigin ), v->gain, v->directParams.occlusion * 100.0f,
				v->directParams.transmission[0], v->directParams.transmission[1], v->directParams.transmission[2] );
		}
	}
}

// lets go of everything of Steam Audio, in the order it depends on each other, and of the library
static void S_SpatialRelease( void ) {
	if ( spatial.library ) {
		S_SpatialFreeMap();
		for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
			spatialVoice_t *v = &spatial.voices[i];

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

	memset( &simulation, 0, sizeof( simulation ) );
	simulation.flags = IPL_SIMULATIONFLAGS_DIRECT;
	simulation.sceneType = IPL_SCENETYPE_DEFAULT;
	simulation.reflectionType = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
	simulation.maxNumOcclusionSamples = SPATIAL_OCCLUSION_SAMPLES;
	simulation.maxNumSources = SPATIAL_VOICES;
	simulation.numThreads = 1;
	simulation.samplingRate = spatial.audio.samplingRate;
	simulation.frameSize = spatial.audio.frameSize;
	if ( qiplSimulatorCreate( spatial.context, &simulation, &spatial.simulator ) != IPL_STATUS_SUCCESS ) {
		spatial.simulator = NULL;
		return qfalse;
	}

	memset( &sourceSettings, 0, sizeof( sourceSettings ) );
	sourceSettings.flags = IPL_SIMULATIONFLAGS_DIRECT;
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
			|| qiplPanningEffectCreate( spatial.context, &spatial.audio, &panningSettings, &v->panning ) != IPL_STATUS_SUCCESS ) {
			return qfalse;
		}
		qiplSourceAdd( v->source, spatial.simulator );
	}
	qiplSimulatorCommit( spatial.simulator );
	return qtrue;
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
	Cmd_AddCommand( "s_spatialInfo", S_SpatialInfo_f, "What s_spatial is doing" );

	memset( &spatial, 0, sizeof( spatial ) );
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
	// the sound system was started again in the middle of a level
	S_SpatialLoadMap();
	S_SpatialInfo_f();
}

void S_SpatialShutdown( void ) {
	Cmd_RemoveCommand( "s_spatialInfo" );
	S_SpatialRelease();
}

// The new path is taken for the sounds of a level. Without one (the menus) there is nothing to place.
qboolean S_SpatialActive( void ) {
	return (qboolean)( spatial.active && spatial.numTriangles > 0 );
}

#else // JP_SPATIAL

// built without it (BuildJPSpatialAudio off, or the submodule lib/steamaudio is not checked out)
void		S_SpatialInit( void ) {}
void		S_SpatialShutdown( void ) {}
qboolean	S_SpatialActive( void ) { return qfalse; }
void		S_SpatialLoadMap( void ) {}
void		S_SpatialBeginFrame( const vec3_t origin, matrix3_t axis ) {}
qboolean	S_SpatialAddChannel( channel_t *ch, const vec3_t origin, float gain, qboolean voiceVolume ) { return qfalse; }
void		S_SpatialAddLoop( int entnum, sfx_t *sfx, const vec3_t origin, float gain, float step ) {}
void		S_SpatialEndFrame( int from, int target ) {}
void		S_SpatialPaint( int from, int to, portable_samplepair_t *paint ) {}
float		S_SpatialAhead( void ) { return 0.0f; }

#endif // JP_SPATIAL
