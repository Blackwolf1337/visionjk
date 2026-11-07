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

// snd_spatial_level.cpp -- the level as Steam Audio has it: the walls of the world and of the brush models,
// what they are made of, and a level that waits to be taken over. See snd_spatial.cpp.

#include "snd_spatial_local.h"

#ifdef JP_SPATIAL

const IPLMaterial spatialMaterials[SPATIAL_NUM_MATERIALS] = {
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

const char *spatialMaterialNames[SPATIAL_NUM_MATERIALS] = {
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


typedef struct spatialShader_s {
	char	shader[MAX_QPATH];		// first: an entry is compared as its name
	byte	material;				// MATERIAL_*
} spatialShader_t;

static spatialShader_t	spatialTable[SPATIAL_TABLE_MAX];
int				spatialTableCount;

/*
===============================================================================

THE LEVEL

Steam Audio traces its rays through a mesh of its own, in its own threads. It gets the solid surfaces of the
world once per level (qcommon/cm/cm_export.cpp), and each brush model (doors, lifts) as a mesh of its own that is
placed where the cgame says it is.

===============================================================================
*/

typedef struct spatialMeshBuild_s {
	IPLVector3	*vertices;
	IPLTriangle	*triangles;
	IPLint32	*materials;
	int			count;
	spatialShaderCache_t	cache;
	qboolean	world;				// the walls of the world, not a brush model: counted for v_spatialInfo, and the corners
} spatialMeshBuild_t;

// for qsort and bsearch: both sides are a name, or an entry of the table, which begins with its name
static int QDECL S_SpatialCompareShader( const void *a, const void *b ) {
	return Q_stricmp( (const char *)a, (const char *)b );
}

void S_SpatialLoadMaterials( void ) {
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

// The material (SPATIAL_*) of a triangle of a shader. The sky first. Then the table, before what the level says: a
// line of the table can put a level right. byTable: the table named it.
int S_SpatialTriangleMaterial( spatialShaderCache_t *cache, const char *shader, int surfaceFlags, qboolean *byTable ) {
	if ( shader != cache->shader ) {
		cache->shader = shader;
		cache->tableMaterial = S_SpatialTableMaterial( shader );
	}
	*byTable = (qboolean)( !( surfaceFlags & SURF_SKY ) && cache->tableMaterial != MATERIAL_NONE );
	if ( surfaceFlags & SURF_SKY ) {
		return SPATIAL_SKY;
	}
	if ( cache->tableMaterial != MATERIAL_NONE ) {
		return spatialMaterialOf[cache->tableMaterial];
	}
	return spatialMaterialOf[surfaceFlags & MATERIAL_MASK];
}

static void S_SpatialTriangle( const vec3_t a, const vec3_t b, const vec3_t c, const char *shader, int surfaceFlags, void *data ) {
	spatialMeshBuild_t *build = (spatialMeshBuild_t *)data;
	const int n = build->count++;
	qboolean byTable;
	const int material = S_SpatialTriangleMaterial( &build->cache, shader, surfaceFlags, &byTable );

	if ( build->world && byTable ) {
		spatial.tableTriangles++;
	}

	S_SpatialPoint( a, &build->vertices[n * 3] );
	S_SpatialPoint( b, &build->vertices[n * 3 + 1] );
	S_SpatialPoint( c, &build->vertices[n * 3 + 2] );
	for ( int i = 0; build->world && i < 3; i++ ) {
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
	if ( build->world ) {
		spatial.materialTriangles[material]++;
	}
}

// lets go of a scene and its mesh
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

// lets go of a level that is not, or no longer, in the simulator. A bake that still runs holds the scene of the
// world itself (S_SpatialBaker).
void S_SpatialReleaseLevel( spatialLevel_t *level ) {
	for ( int i = 0; i < level->numBrushes; i++ ) {
		spatialBrush_t *b = &level->brushes[i];

		if ( b->instance ) {
			qiplInstancedMeshRelease( &b->instance );
		}
		S_SpatialReleaseScene( &b->scene, &b->mesh );
	}
	if ( level->brushes ) {
		Z_Free( level->brushes );
	}
	if ( level->world ) {
		qiplInstancedMeshRelease( &level->world );
	}
	if ( level->top ) {
		qiplSceneRelease( &level->top );
	}
	S_SpatialReleaseScene( &level->scene, &level->mesh );
	memset( level, 0, sizeof( *level ) );
}

/*
===============
S_SpatialTakeScene

Once a frame. A level that waits is given to the simulator when no pass of the reflections runs: that is the
one moment it may be. Until then the level before it stays where it is, and S_SpatialActive() says no. The
graphics card may still be building the one before (v_spatialGpu): then it waits for that too.
===============
*/
void S_SpatialTakeScene( void ) {
	if ( !spatial.sceneWaits || spatialWorkerState.load() != WORKER_IDLE || !S_SpatialGpuIdle() ) {
		return;
	}
	// the probes of the level before go with it, and a bake that still runs for it is told to stop
	S_SpatialRemoveBaked( &spatial.paths );
	S_SpatialRemoveBaked( &spatial.echo );
	qiplSimulatorSetScene( spatial.simulator, spatial.nextLevel.top );
	qiplSimulatorCommit( spatial.simulator );
	S_SpatialReleaseBaked( &spatial.paths );
	S_SpatialReleaseBaked( &spatial.echo );
	if ( spatialBakeState.load() == BAKE_RUNNING ) {
		S_SpatialCancelBake();
	}

	S_SpatialReleaseLevel( &spatial.level );
	spatial.level = spatial.nextLevel;
	memset( &spatial.nextLevel, 0, sizeof( spatial.nextLevel ) );
	S_SpatialGpuTakeLevel();
	spatial.numTriangles = spatial.nextTriangles;
	spatial.mins = spatial.nextMins;
	spatial.maxs = spatial.nextMaxs;
	Q_strncpyz( spatial.paths.file, spatial.paths.nextFile, sizeof( spatial.paths.file ) );
	Q_strncpyz( spatial.echo.file, spatial.echo.nextFile, sizeof( spatial.echo.file ) );
	spatial.nextTriangles = 0;
	spatial.sceneWaits = qfalse;
	spatial.sceneNumber++;

	// its paths: from the file of an earlier visit, or worked out now
	spatial.refusedProbes = 0;
	if ( spatial.level.scene && v_spatialPaths->integer && !S_SpatialLoadBaked( &spatial.paths ) ) {
		spatial.paths.wanted = qtrue;
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

// A scene with the walls of one brush model of the collision model in it, 0: the world. Returns how many triangles
// it has, 0: none, and no scene.
static int S_SpatialBuildMesh( int model, IPLScene *scene, IPLStaticMesh *mesh, unsigned *checksum ) {
	spatialMeshBuild_t build;
	IPLSceneSettings sceneSettings;
	IPLStaticMeshSettings meshSettings;
	IPLMaterial materials[SPATIAL_NUM_MATERIALS];
	const int count = CM_ExportModelTriangles( model, NULL, NULL );

	*scene = NULL;
	*mesh = NULL;
	if ( !count ) {
		return 0;
	}
	memset( &sceneSettings, 0, sizeof( sceneSettings ) );
	sceneSettings.type = spatial.sceneType;
	sceneSettings.embreeDevice = spatial.embree;
	if ( qiplSceneCreate( spatial.context, &sceneSettings, scene ) != IPL_STATUS_SUCCESS ) {
		*scene = NULL;
		return 0;
	}

	memset( &build, 0, sizeof( build ) );
	build.world = (qboolean)( model == 0 );
	build.vertices = (IPLVector3 *)Z_Malloc( count * 3 * sizeof( IPLVector3 ), TAG_TEMP_WORKSPACE, qfalse );
	build.triangles = (IPLTriangle *)Z_Malloc( count * sizeof( IPLTriangle ), TAG_TEMP_WORKSPACE, qfalse );
	build.materials = (IPLint32 *)Z_Malloc( count * sizeof( IPLint32 ), TAG_TEMP_WORKSPACE, qfalse );
	CM_ExportModelTriangles( model, S_SpatialTriangle, &build );
	if ( checksum ) {
		*checksum = Com_BlockChecksum( build.vertices, build.count * 3 * (int)sizeof( IPLVector3 ) );
	}
	S_SpatialGpuMesh( model, build.vertices, build.materials, build.count );

	memcpy( materials, spatialMaterials, sizeof( materials ) );
	memset( &meshSettings, 0, sizeof( meshSettings ) );
	meshSettings.numVertices = build.count * 3;
	meshSettings.numTriangles = build.count;
	meshSettings.numMaterials = SPATIAL_NUM_MATERIALS;
	meshSettings.vertices = build.vertices;
	meshSettings.triangles = build.triangles;
	meshSettings.materialIndices = build.materials;
	meshSettings.materials = materials;
	if ( qiplStaticMeshCreate( *scene, &meshSettings, mesh ) == IPL_STATUS_SUCCESS ) {
		qiplStaticMeshAdd( *mesh, *scene );
		qiplSceneCommit( *scene );
	} else {
		*mesh = NULL;
		S_SpatialReleaseScene( scene, mesh );
		build.count = 0;
	}
	// the library keeps a copy
	Z_Free( build.vertices );
	Z_Free( build.triangles );
	Z_Free( build.materials );
	return build.count;
}

/*
=================
S_SpatialBrushTransform

Where the walls of a brush model are, for Steam Audio: the entity turns them by its angles and moves them to its
origin, as the renderer and CM_TransformedBoxTrace() do. Its scene has them in Steam Audio's axes and metres
(S_SpatialPoint, P times SPATIAL_UNIT): world = P R P^T local + P origin SPATIAL_UNIT, the scale cancels in the
turn. R has the axes of the angles as its columns. Row major, the move in the last column (IPLMatrix4x4).
=================
*/
static void S_SpatialBrushTransform( const vec3_t origin, const vec3_t angles, IPLMatrix4x4 *m ) {
	// the rows of P: Steam Audio x is -y of the game, y is z, z is -x
	static const float P[3][3] = { { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f } };
	matrix3_t axis;
	float R[3][3];
	IPLVector3 move;

	AnglesToAxis( angles, axis );
	for ( int i = 0; i < 3; i++ ) {
		for ( int j = 0; j < 3; j++ ) {
			R[i][j] = axis[j][i];
		}
	}
	memset( m, 0, sizeof( *m ) );
	for ( int i = 0; i < 3; i++ ) {
		for ( int j = 0; j < 3; j++ ) {
			float sum = 0.0f;

			for ( int k = 0; k < 3; k++ ) {
				for ( int l = 0; l < 3; l++ ) {
					sum += P[i][k] * R[k][l] * P[j][l];
				}
			}
			m->elements[i][j] = sum;
		}
	}
	S_SpatialPoint( origin, &move );
	m->elements[0][3] = move.x;
	m->elements[1][3] = move.y;
	m->elements[2][3] = move.z;
	m->elements[3][3] = 1.0f;
}

/*
===============
S_SpatialLoadMap

The collision model of a level was loaded, or the sound system starts while a level is. Runs while the
loading screen is up: building the mesh of a large level takes a moment. The level waits to be taken over,
see S_SpatialTakeScene().

The walls of the world are a scene of their own, which the bake of the paths reads. The simulator traces in a
scene that holds that one, and each brush model (doors, lifts, func_static, glass) as a scene of its own where
the map has it, until the cgame says where it is (S_SpatialMovers).

A page takes it over here and now, and works its paths out: it has no thread that could do that later next
to the game, and it stands still while a level loads anyway.
===============
*/
void S_SpatialLoadMap( const char *mapname ) {
	const int start = Sys_Milliseconds();
	spatialLevel_t *level = &spatial.nextLevel;
	IPLSceneSettings sceneSettings;
	IPLInstancedMeshSettings instanceSettings;
	unsigned checksum = 0;
	int numModels;

	if ( !spatial.active ) {
		return;
	}
	// a level that still waits was never used
	S_SpatialReleaseLevel( level );
	S_SpatialGpuBeginLevel();
	spatial.nextTriangles = 0;
	spatial.sceneWaits = qtrue;
	memset( spatial.materialTriangles, 0, sizeof( spatial.materialTriangles ) );
	spatial.tableTriangles = 0;

	spatial.nextTriangles = S_SpatialBuildMesh( 0, &level->scene, &level->mesh, &checksum );
	if ( !spatial.nextTriangles ) {
		if ( CM_ExportSolidTriangles( NULL, NULL ) ) {
			Com_Printf( "v_spatial: no mesh for the level\n" );
		}
		return;
	}

	// the files of its paths and its echo, by its name and its walls
	S_SpatialNameBakes( mapname, checksum );

	// the scene of the simulator: the world, and the brush models
	memset( &sceneSettings, 0, sizeof( sceneSettings ) );
	sceneSettings.type = spatial.sceneType;
	sceneSettings.embreeDevice = spatial.embree;
	memset( &instanceSettings, 0, sizeof( instanceSettings ) );
	if ( qiplSceneCreate( spatial.context, &sceneSettings, &level->top ) != IPL_STATUS_SUCCESS ) {
		Com_Printf( "v_spatial: no scene for the level\n" );
		level->top = NULL;
		S_SpatialReleaseLevel( level );
		spatial.nextTriangles = 0;
		return;
	}
	instanceSettings.subScene = level->scene;
	S_SpatialBrushTransform( vec3_origin, vec3_origin, &instanceSettings.transform );
	if ( qiplInstancedMeshCreate( level->top, &instanceSettings, &level->world ) != IPL_STATUS_SUCCESS ) {
		Com_Printf( "v_spatial: no scene for the level\n" );
		level->world = NULL;
		S_SpatialReleaseLevel( level );
		spatial.nextTriangles = 0;
		return;
	}
	qiplInstancedMeshAdd( level->world, level->top );
	S_SpatialGpuPlace( qtrue, 0, &instanceSettings.transform );

	numModels = CM_ExportModelCount();
	if ( numModels > 1 ) {
		level->brushes = (spatialBrush_t *)Z_Malloc( numModels * sizeof( spatialBrush_t ), TAG_GENERAL, qtrue );
		level->numBrushes = numModels;
	}
	for ( int i = 1; i < level->numBrushes; i++ ) {
		spatialBrush_t *b = &level->brushes[i];

		b->model = i;
		CM_ModelBounds( CM_InlineModel( i ), b->mins, b->maxs );
		b->triangles = S_SpatialBuildMesh( i, &b->scene, &b->mesh, NULL );
		if ( !b->triangles ) {
			continue;
		}
		instanceSettings.subScene = b->scene;
		if ( qiplInstancedMeshCreate( level->top, &instanceSettings, &b->instance ) != IPL_STATUS_SUCCESS ) {
			b->instance = NULL;
			S_SpatialReleaseScene( &b->scene, &b->mesh );
			b->triangles = 0;
			continue;
		}
		qiplInstancedMeshAdd( b->instance, level->top );
		S_SpatialGpuPlace( qtrue, i, &instanceSettings.transform );
		b->added = qtrue;
		level->brushTriangles += b->triangles;
	}
	qiplSceneCommit( level->top );

	spatial.sceneMsec = Sys_Milliseconds() - start;
	Com_Printf( "v_spatial: %i triangles of the level and %i of its brush models in %i ms\n", spatial.nextTriangles,
		level->brushTriangles, spatial.sceneMsec );
#ifdef __EMSCRIPTEN__
	S_SpatialPageLoadMap();
#endif
}

// how many brush models have walls in the scene of the simulator, and how many of them are in it now
void S_SpatialCountBrushes( int *withMesh, int *inScene ) {
	*withMesh = *inScene = 0;
	for ( int i = 1; i < spatial.level.numBrushes; i++ ) {
		*withMesh += spatial.level.brushes[i].instance ? 1 : 0;
		*inScene += spatial.level.brushes[i].added ? 1 : 0;
	}
}

/*
===============
S_SpatialOutOfBrushes

The sounds of a brush model come from the middle of it (CG_SetEntitySoundPosition, and its loop sound in
CG_EntityEffects): inside its own walls, which are in the scene of the simulator since 2026-10-21 (5200ad81). There
they hid it: the lift of mp/ffa3 that carried the listener was 0 % in sight and let through 0.04 of its lows. A place
inside the bounds of a brush model of the scene goes out of them along the line to the listener, to where the line
leaves them and SPATIAL_OCCLUSION_RADIUS on, no further than halfway to him: the side of a door or a lift he hears it
from. On that line its direction from him stays what it was. Not when the listener is in the bounds as well.
qtrue: it was moved.
===============
*/
static qboolean S_SpatialInside( const vec3_t p, const vec3_t mins, const vec3_t maxs ) {
	return (qboolean)( p[0] >= mins[0] && p[0] <= maxs[0] && p[1] >= mins[1] && p[1] <= maxs[1] && p[2] >= mins[2] && p[2] <= maxs[2] );
}

qboolean S_SpatialOutOfBrushes( const vec3_t origin, vec3_t out ) {
	const spatialLevel_t *level = &spatial.level;
	qboolean moved = qfalse;

	VectorCopy( origin, out );
	for ( int i = 1; i < level->numBrushes; i++ ) {
		const spatialBrush_t *b = &level->brushes[i];
		matrix3_t axis;
		vec3_t d, local, localListener, dir, localDir;
		float length, exit = 1e30f;

		if ( !b->added ) {
			continue;
		}
		// into the place the map has it in, as its walls are: local = R^T ( world - origin )
		AnglesToAxis( b->angles, axis );
		VectorSubtract( out, b->origin, d );
		for ( int k = 0; k < 3; k++ ) {
			local[k] = DotProduct( d, axis[k] );
		}
		if ( !S_SpatialInside( local, b->mins, b->maxs ) ) {
			continue;
		}
		VectorSubtract( spatial.listenerOrigin, b->origin, d );
		for ( int k = 0; k < 3; k++ ) {
			localListener[k] = DotProduct( d, axis[k] );
		}
		if ( S_SpatialInside( localListener, b->mins, b->maxs ) ) {
			continue;
		}
		VectorSubtract( spatial.listenerOrigin, out, dir );
		length = VectorNormalize( dir );
		for ( int k = 0; k < 3; k++ ) {
			localDir[k] = DotProduct( dir, axis[k] );
		}
		// where the line leaves the bounds: the nearest side it goes out through
		for ( int k = 0; k < 3; k++ ) {
			if ( localDir[k] > 1e-6f ) {
				exit = Q_min( exit, ( b->maxs[k] - local[k] ) / localDir[k] );
			} else if ( localDir[k] < -1e-6f ) {
				exit = Q_min( exit, ( b->mins[k] - local[k] ) / localDir[k] );
			}
		}
		if ( exit >= length ) {
			continue;
		}
		VectorMA( out, exit + Q_min( SPATIAL_OCCLUSION_RADIUS / SPATIAL_UNIT, ( length - exit ) * 0.5f ), dir, out );
		moved = qtrue;
	}
	return moved;
}

/*
===============
S_SpatialMovers

From the cgame (ext.S_SpatialMovers), once a frame: where the brush models of its snapshot are, and which are gone.
A level that waits to be taken over gets them, it is the one the cgame sees.
===============
*/
static qboolean S_SpatialNear( const vec3_t a, const vec3_t b ) {
	return (qboolean)( fabsf( a[0] - b[0] ) < SPATIAL_BRUSH_EPSILON && fabsf( a[1] - b[1] ) < SPATIAL_BRUSH_EPSILON
		&& fabsf( a[2] - b[2] ) < SPATIAL_BRUSH_EPSILON );
}

void S_SpatialMovers( const spatialMover_t *list, int count ) {
	spatialLevel_t *level = spatial.sceneWaits ? &spatial.nextLevel : &spatial.level;

	if ( !spatial.active || !list ) {
		return;
	}
	for ( int i = 0; i < count; i++ ) {
		const spatialMover_t *m = &list[i];
		spatialBrush_t *b;

		if ( m->model <= 0 || m->model >= level->numBrushes ) {
			continue;
		}
		b = &level->brushes[m->model];
		if ( !b->instance ) {
			continue;
		}
		if ( !m->gone != !b->gone ) {
			b->gone = m->gone ? qtrue : qfalse;
			b->changed = qtrue;
		}
		if ( !m->gone && ( !S_SpatialNear( m->origin, b->origin ) || !S_SpatialNear( m->angles, b->angles ) ) ) {
			VectorCopy( m->origin, b->origin );
			VectorCopy( m->angles, b->angles );
			b->changed = qtrue;
		}
	}
}

// What S_SpatialMovers() changed, into the scene of the simulator. Not while a pass runs in the thread of the
// reflections: a scene must not be committed while anything simulates in it. Then it waits a frame or two.
void S_SpatialApplyBrushes( void ) {
	spatialLevel_t *level = &spatial.level;
	int moved = 0;

	spatial.brushesMoved = 0;
	if ( !level->top || spatialWorkerState.load() != WORKER_IDLE ) {
		return;
	}
	for ( int i = 1; i < level->numBrushes; i++ ) {
		spatialBrush_t *b = &level->brushes[i];

		if ( !b->changed || !b->instance ) {
			continue;
		}
		b->changed = qfalse;
		moved++;
		if ( b->gone ) {
			if ( b->added ) {
				qiplInstancedMeshRemove( b->instance, level->top );
				S_SpatialGpuPlace( qfalse, i, NULL );
				b->added = qfalse;
			}
			continue;
		}
		if ( !b->added ) {
			qiplInstancedMeshAdd( b->instance, level->top );
			b->added = qtrue;
		}
		IPLMatrix4x4 transform;

		S_SpatialBrushTransform( b->origin, b->angles, &transform );
		qiplInstancedMeshUpdateTransform( b->instance, level->top, transform );
		S_SpatialGpuPlace( qfalse, i, &transform );
	}
	if ( moved ) {
		qiplSceneCommit( level->top );
	}
	spatial.brushesMoved = moved;
}

// v_spatialSaveScene: the scene of the simulator as Steam Audio has it, into steamaudio/scene.obj of the home path
void S_SpatialSaveScene_f( void ) {
	const char *path;

	if ( !spatial.level.top || spatialWorkerState.load() != WORKER_IDLE ) {
		Com_Printf( "v_spatialSaveScene: no level, or a pass runs, try again\n" );
		return;
	}
	// steamaudio/ is there: the paths of the levels are kept in it
	path = FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), FS_GetCurrentGameDir(), "steamaudio/scene.obj" );
	qiplSceneSaveOBJ( spatial.level.top, path );
	Com_Printf( "v_spatialSaveScene: %s\n", path );
}

static_assert( (int)SPATIAL_DEBUG_MATERIALS == (int)SPATIAL_NUM_MATERIALS, "spatial_debug.h has the materials of this file" );

#endif // JP_SPATIAL
