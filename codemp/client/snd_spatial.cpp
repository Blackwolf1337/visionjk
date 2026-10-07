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
	X( iplStaticMeshAdd )

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
enum {
	SPATIAL_GENERIC, SPATIAL_BRICK, SPATIAL_CONCRETE, SPATIAL_CERAMIC, SPATIAL_GRAVEL, SPATIAL_CARPET, SPATIAL_GLASS,
	SPATIAL_PLASTER, SPATIAL_WOOD, SPATIAL_METAL, SPATIAL_ROCK, SPATIAL_SKY, SPATIAL_NUM_MATERIALS
};

static IPLMaterial spatialMaterials[SPATIAL_NUM_MATERIALS] = {
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

static cvar_t	*s_spatial;
static cvar_t	*s_spatialHrtf;

static struct {
	void		*library;
	char		libraryPath[MAX_OSPATH];
	qboolean	active;

	IPLContext			context;
	IPLAudioSettings	audio;
	IPLHRTF				hrtf;

	// the level
	IPLScene			scene;
	IPLStaticMesh		mesh;
	int					numTriangles;
	int					materialTriangles[SPATIAL_NUM_MATERIALS];
	int					sceneMsec;
} spatial;

// A point of the game for Steam Audio: metres, x to the right, y up, z to the back. The game has x ahead, y to
// the left and z up. Both are right handed, so a triangle keeps the side it faces.
static void S_SpatialPoint( const vec3_t in, IPLVector3 *out ) {
	out->x = -in[1] * SPATIAL_UNIT;
	out->y = in[2] * SPATIAL_UNIT;
	out->z = -in[0] * SPATIAL_UNIT;
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

	memset( &meshSettings, 0, sizeof( meshSettings ) );
	meshSettings.numVertices = build.count * 3;
	meshSettings.numTriangles = build.count;
	meshSettings.numMaterials = SPATIAL_NUM_MATERIALS;
	meshSettings.vertices = build.vertices;
	meshSettings.triangles = build.triangles;
	meshSettings.materialIndices = build.materials;
	meshSettings.materials = spatialMaterials;
	if ( qiplStaticMeshCreate( spatial.scene, &meshSettings, &spatial.mesh ) == IPL_STATUS_SUCCESS ) {
		qiplStaticMeshAdd( spatial.mesh, spatial.scene );
		qiplSceneCommit( spatial.scene );
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

	spatial.active = qtrue;
	// the sound system was started again in the middle of a level
	S_SpatialLoadMap();
	S_SpatialInfo_f();
}

void S_SpatialShutdown( void ) {
	Cmd_RemoveCommand( "s_spatialInfo" );
	if ( spatial.active ) {
		S_SpatialFreeMap();
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

qboolean S_SpatialActive( void ) {
	return spatial.active;
}

#else // JP_SPATIAL

// built without it (BuildJPSpatialAudio off, or the submodule lib/steamaudio is not checked out)
void		S_SpatialInit( void ) {}
void		S_SpatialShutdown( void ) {}
qboolean	S_SpatialActive( void ) { return qfalse; }
void		S_SpatialLoadMap( void ) {}

#endif // JP_SPATIAL
