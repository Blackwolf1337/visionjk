/*
===========================================================================
Copyright (C) 2026, VisionJK contributors

This file is part of the VisionJK source code.

VisionJK is free software; you can redistribute it and/or modify it
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

// JA+ invisible blocking walls: boxes of a map that block players, kept in a file per map
//
// Not as in JA+: there a wall was one entity no client was told of (SVF_NOCLIENT), so every client walked into
// it in its own prediction and was set back by the server. Here a wall is the entity that says where it is and
// how big, and parts that do the blocking and that clients are told of: each client clips its moves against
// them as it does against any solid entity, the unchanged game and the JA+ plugin too.
//
// Also not as in JA+: the commands that made and walked the walls in the game (BlockWallCreate, Del, Next, Prev,
// Draw, Write, GrantEditor) are gone. The walls are drawn and dragged in the clip editor of the web admin panel,
// which calls what is here. Their file is wallblocks/<map>.cfg, a wall a line as JA+ wrote them (two corners,
// six numbers), and a line "clipsoff" with the clip brushes of the map that the server does not mind (the
// engine command clipbrush). A map without that file takes <map>_holes.cfg of JA+.
#include "game/g_local.h"

#define BLOCKWALL_CLASS	"InvisibleBlockingWall"
#define BLOCKWALL_PART	"InvisibleBlockingWallPart"
#define BLOCKWALL_MAXFILE	0x10000
// What a client is told of a solid box that is no brush model is one number (s.solid, packed by SV_LinkEntity
// and read back by CG_ClipMoveToEntities): half its width, the same along X and Y, how far it reaches down, and
// how far up with 32 added, each below 256. A wall is cut into parts that fit into that.
#define BLOCKWALL_HALF_WIDTH	255
#define BLOCKWALL_HALF_HEIGHT	223
#define BLOCKWALL_MAX_PARTS		64
#define BLOCKWALL_RESERVE		64		// entities that stay for the game: G_Spawn ends the server without one

typedef struct blockPlan_s {
	vec3_t	mins, maxs;		// the box in the world, its sizes whole and even
	int		size[3];
	int		part[3];		// the size of a part, the same along X and Y
	int		count[3];
} blockPlan_t;

static qboolean IsBlockWall( const gentity_t *ent ) {
	return ent->inuse && ent->classname && !Q_stricmp( ent->classname, BLOCKWALL_CLASS );
}

static qboolean JP_BlockWallPart( const gentity_t *ent ) {
	return ent->inuse && ent->classname && !Q_stricmp( ent->classname, BLOCKWALL_PART );
}

// How the box between two corners is cut. A part is as wide as the short side of the box, and wider when that
// makes more than BLOCKWALL_MAX_PARTS: the wall is thicker then. qfalse: too large even so.
static qboolean PlanWall( const vec3_t p1, const vec3_t p2, blockPlan_t *plan ) {
	int i, w;

	for ( i = 0; i < 3; i++ ) {
		int size = (int)ceilf( fabsf( p2[i] - p1[i] ) );

		if ( size < 2 )
			size = 2;
		size += size & 1;
		plan->size[i] = size;
	}
	plan->part[2] = Q_min( plan->size[2], BLOCKWALL_HALF_HEIGHT * 2 );
	plan->count[2] = ( plan->size[2] + plan->part[2] - 1 ) / plan->part[2];

	for ( w = Q_min( Q_min( plan->size[0], plan->size[1] ), BLOCKWALL_HALF_WIDTH * 2 ); ; w += 2 ) {
		for ( i = 0; i < 2; i++ )
			plan->count[i] = ( Q_max( plan->size[i], w ) + w - 1 ) / w;
		if ( plan->count[0] * plan->count[1] * plan->count[2] <= BLOCKWALL_MAX_PARTS )
			break;
		if ( w >= BLOCKWALL_HALF_WIDTH * 2 )
			return qfalse;
	}
	plan->part[0] = plan->part[1] = w;
	for ( i = 0; i < 3; i++ ) {
		const float mid = ( p1[i] + p2[i] ) * 0.5f;

		if ( i < 2 && plan->size[i] < w )
			plan->size[i] = w;
		plan->mins[i] = mid - plan->size[i] / 2;
		plan->maxs[i] = mid + plan->size[i] / 2;
	}
	return qtrue;
}

// how many parts a wall with this box is made of, 0 for one that is too large
int JP_BlockWallParts( const vec3_t mins, const vec3_t maxs ) {
	blockPlan_t plan;

	return PlanWall( mins, maxs, &plan ) ? plan.count[0] * plan.count[1] * plan.count[2] : 0;
}

// a wall or a part of one: nothing the entity editor has to show
qboolean JP_BlockWallEntity( const gentity_t *ent ) {
	return (qboolean)( IsBlockWall( ent ) || JP_BlockWallPart( ent ) );
}

// qtrue: ent is a wall. mins and maxs are its corners in the world then.
qboolean JP_BlockWallBounds( const gentity_t *ent, vec3_t mins, vec3_t maxs ) {
	if ( !IsBlockWall( ent ) )
		return qfalse;
	VectorAdd( ent->r.currentOrigin, ent->r.mins, mins );
	VectorAdd( ent->r.currentOrigin, ent->r.maxs, maxs );
	return qtrue;
}

// the parts of a wall go with it. From G_FreeEntity.
void JP_BlockWallFreed( gentity_t *wall ) {
	int i;

	if ( !IsBlockWall( wall ) )
		return;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		if ( g_entities[i].parent == wall && JP_BlockWallPart( &g_entities[i] ) )
			G_FreeEntity( &g_entities[i] );
	}
}

// One part: the entity that is in the way. Its contents are those of a clip brush, so shots pass. The server
// packs s.solid only for SOLID and BODY and clears it for anything else when it links, so it is packed here,
// as SV_LinkEntity would. A client makes a body of it, which stops its player all the same.
static void SpawnPart( gentity_t *wall, const vec3_t origin, const int *size ) {
	gentity_t	*part = G_Spawn();
	const int	halfWidth = size[0] / 2, halfHeight = size[2] / 2;

	part->classname = BLOCKWALL_PART;
	part->parent = wall;
	G_SetOrigin( part, origin );
	VectorSet( part->r.mins, -halfWidth, -halfWidth, -halfHeight );
	VectorSet( part->r.maxs, halfWidth, halfWidth, halfHeight );
	part->r.contents = CONTENTS_PLAYERCLIP;
	part->s.eType = ET_INVISIBLE;
	trap->LinkEntity( (sharedEntity_t *)part );
	part->s.solid = ( ( halfHeight + 32 ) << 16 ) | ( halfHeight << 8 ) | halfWidth;
}

// The wall between two corners in world space, with its parts, its origin in the middle. NULL: too large, or
// the map has no entities left for it.
gentity_t *JP_BlockWallCreate( const vec3_t p1, const vec3_t p2 ) {
	gentity_t	*wall;
	blockPlan_t	plan;
	vec3_t		origin;
	int			i, x, y, z, at[3];

	if ( !PlanWall( p1, p2, &plan ) || WA_FreeEntities() < plan.count[0] * plan.count[1] * plan.count[2] + 1 + BLOCKWALL_RESERVE )
		return NULL;

	wall = G_Spawn();
	VectorAdd( plan.mins, plan.maxs, origin );
	VectorScale( origin, 0.5f, origin );
	G_SetOrigin( wall, origin );
	wall->classname = BLOCKWALL_CLASS;
	VectorSubtract( plan.mins, origin, wall->r.mins );
	VectorSubtract( plan.maxs, origin, wall->r.maxs );
	// it is what the editor and the file see, it stops nobody and no client hears of it
	wall->r.contents = 0;
	wall->s.eType = ET_INVISIBLE;
	wall->r.svFlags |= SVF_NOCLIENT;
	wall->r.svFlags &= ~SVF_BROADCAST;
	trap->LinkEntity( (sharedEntity_t *)wall );

	// side by side from the low corner, the last one of a row pulled back so that it ends with the box
	for ( z = 0; z < plan.count[2]; z++ ) {
		for ( y = 0; y < plan.count[1]; y++ ) {
			for ( x = 0; x < plan.count[0]; x++ ) {
				at[0] = x; at[1] = y; at[2] = z;
				for ( i = 0; i < 3; i++ ) {
					const int low = Q_min( at[i] * plan.part[i], plan.size[i] - plan.part[i] );

					origin[i] = plan.mins[i] + low + plan.part[i] / 2;
				}
				SpawnPart( wall, origin, plan.part );
			}
		}
	}
	return wall;
}

// every wall of the map goes. Returns how many there were.
int JP_ClearBlockWalls( void ) {
	int i, count = 0;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		if ( IsBlockWall( &g_entities[i] ) ) {
			G_FreeEntity( &g_entities[i] );
			count++;
		}
	}
	return count;
}

/*
=================
JP_ClipBrushes

Clip brushes of the map by their numbers in the map file, off or on: the engine command clipbrush does it in
the next frame. Only numbers out of the text get to the console, in lines it takes (1024 characters).
Returns how many were named.
=================
*/
#define CLIP_LINE	100		// numbers in one line

int JP_ClipBrushes( qboolean on, const char *numbers ) {
	char		line[CLIP_LINE * 8 + 32];
	const char	*p = numbers;
	int			count = 0, inLine = 0;

	line[0] = '\0';
	while ( *p ) {
		char		*end;
		const long	n = strtol( p, &end, 10 );

		if ( end == p ) {
			// what stands between two numbers
			p++;
			continue;
		}
		p = end;
		if ( n < 0 || n > 0xfffff )
			continue;
		Q_strcat( line, sizeof( line ), va( " %ld", n ) );
		count++;
		if ( ++inLine == CLIP_LINE ) {
			trap->SendConsoleCommand( EXEC_APPEND, va( "clipbrush %s%s\n", on ? "on" : "off", line ) );
			line[0] = '\0';
			inLine = 0;
		}
	}
	if ( inLine )
		trap->SendConsoleCommand( EXEC_APPEND, va( "clipbrush %s%s\n", on ? "on" : "off", line ) );
	return count;
}

// the two files a map may have its walls in, the one that is written first. '/' in the name of a map is a
// folder in ours as in botroutes/, and an underscore in the one of JA+.
static void WallFiles( char *file, int fileSize, char *legacy, int legacySize ) {
	char mapname[MAX_QPATH], *p;

	trap->Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
	Com_sprintf( file, fileSize, "wallblocks/%s.cfg", mapname );
	for ( p = mapname; *p; p++ ) {
		if ( *p == '/' )
			*p = '_';
	}
	Com_sprintf( legacy, legacySize, "%s_holes.cfg", mapname );
}

/*
=================
JP_LoadBlockWalls

The walls of the map from its file, and the clip brushes it has off. At the start of the map, and when the clip
editor goes back to what is saved (which has cleared both before). Returns how many walls were made.
=================
*/
int JP_LoadBlockWalls( void ) {
	static char buf[BLOCKWALL_MAXFILE];
	char file[MAX_QPATH + 16], legacy[MAX_QPATH + 16];
	fileHandle_t f;
	vec3_t p1, p2;
	const char *p, *tok;
	int len, i, count = 0;

	WallFiles( file, sizeof( file ), legacy, sizeof( legacy ) );
	len = trap->FS_Open( file, &f, FS_READ );
	if ( len < 0 || !f )
		len = trap->FS_Open( legacy, &f, FS_READ );
	if ( len < 0 || !f )
		return 0;
	if ( len >= BLOCKWALL_MAXFILE ) {
		Com_Printf( "The file of the blocking walls of this map is too large, its end is not read\n" );
		len = BLOCKWALL_MAXFILE - 1;
	}
	trap->FS_Read( buf, len, f );
	buf[len] = '\0';
	trap->FS_Close( f );

	COM_Compress( buf );
	p = buf;
	COM_BeginParseSession( "mapHolesInfo" );
	// one wall per line, numbers missing at the end of a line are 0
	while ( p && *p ) {
		tok = COM_ParseExt( &p, qtrue );
		if ( !tok[0] )
			break;
		if ( !Q_stricmp( tok, "clipsoff" ) ) {
			char numbers[4096];

			numbers[0] = '\0';
			while ( ( tok = COM_ParseExt( &p, qfalse ) )[0] )
				Q_strcat( numbers, sizeof( numbers ), va( " %s", tok ) );
			JP_ClipBrushes( qfalse, numbers );
			continue;
		}
		p1[0] = atof( tok );
		for ( i = 1; i < 6; i++ ) {
			if ( COM_ParseString( &p, &tok ) )
				break;
			if ( i < 3 )
				p1[i] = atof( tok );
			else
				p2[i - 3] = atof( tok );
		}
		if ( i < 6 )
			break;
		if ( JP_BlockWallCreate( p1, p2 ) )
			count++;
		SkipRestOfLine( &p );
	}
	Com_Printf( "Blocking walls of this map: %d\n", count );
	return count;
}

// The walls that stand and the clip brushes that are off, into wallblocks/<map>.cfg. qfalse: not written.
qboolean JP_SaveBlockWalls( void ) {
	char file[MAX_QPATH + 16], legacy[MAX_QPATH + 16], line[256], off[4096];
	fileHandle_t f = 0;
	int i;

	WallFiles( file, sizeof( file ), legacy, sizeof( legacy ) );
	trap->FS_Open( file, &f, FS_WRITE );
	if ( !f )
		return qfalse;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		vec3_t mins, maxs;

		if ( !JP_BlockWallBounds( &g_entities[i], mins, maxs ) )
			continue;
		Com_sprintf( line, sizeof( line ), "%.1f %.1f %.1f %.1f %.1f %.1f\n", mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2] );
		trap->FS_Write( line, strlen( line ), f );
	}
	trap->Cvar_VariableStringBuffer( "sv_clipOff", off, sizeof( off ) );
	if ( off[0] ) {
		trap->FS_Write( "clipsoff ", 9, f );
		trap->FS_Write( off, strlen( off ), f );
		trap->FS_Write( "\n", 1, f );
	}
	trap->FS_Close( f );
	return qtrue;
}
