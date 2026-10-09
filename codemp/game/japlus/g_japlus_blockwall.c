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

// JA+ invisible blocking walls: boxes stored per map in <mapname>_holes.cfg that block players
//
// Not as in JA+: there a wall was one entity no client was told of (SVF_NOCLIENT), so every client walked into
// it in its own prediction and was set back by the server. Here a wall is the entity that says where it is and
// how big, and parts that do the blocking and that clients are told of: each client clips its moves against
// them as it does against any solid entity, the unchanged game and the JA+ plugin too.
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

qboolean JP_BlockWallPart( const gentity_t *ent ) {
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

static void DrawBox( const vec3_t mins, const vec3_t maxs, int time ) {
	vec3_t a, b;
	int i, j;

	// the 12 edges: every corner connects to the corners that differ in one axis
	for ( i = 0; i < 8; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			if ( i & (1 << j) )
				continue;
			VectorSet( a, (i & 1) ? maxs[0] : mins[0], (i & 2) ? maxs[1] : mins[1], (i & 4) ? maxs[2] : mins[2] );
			VectorCopy( a, b );
			b[j] = maxs[j];
			G_TestLine( a, b, 0xff, time );
		}
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

// The wall between two corners in world space, with its parts. wall: the entity that is to be it (one spawned
// from keys, which stays where its origin key puts it), NULL for a new one in the middle of the box.
// NULL: too large, or the map has no entities left for it. An entity that was handed in is not freed.
static gentity_t *SpawnWall( gentity_t *wall, const vec3_t p1, const vec3_t p2 ) {
	blockPlan_t	plan;
	vec3_t		origin;
	int			i, x, y, z, at[3];

	if ( !PlanWall( p1, p2, &plan )
		|| WA_FreeEntities() < plan.count[0] * plan.count[1] * plan.count[2] + ( wall ? 0 : 1 ) + BLOCKWALL_RESERVE )
		return NULL;

	if ( !wall ) {
		wall = G_Spawn();
		VectorAdd( plan.mins, plan.maxs, origin );
		VectorScale( origin, 0.5f, origin );
		G_SetOrigin( wall, origin );
	}
	wall->classname = BLOCKWALL_CLASS;
	VectorSubtract( plan.mins, wall->r.currentOrigin, wall->r.mins );
	VectorSubtract( plan.maxs, wall->r.currentOrigin, wall->r.maxs );
	// it is what the editors and the file see, it stops nobody and no client hears of it
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

// p1 and p2 are opposite corners in world space, visible: show the box to everybody for 2 minutes
static gentity_t *CreateBlockingHole( const vec3_t p1, const vec3_t p2, qboolean visible ) {
	gentity_t *ent = SpawnWall( NULL, p1, p2 );

	if ( !ent ) {
		Com_Printf( "^5Blocking Wall: ^7too large or no entities left for it, %s %s\n", vtos( p1 ), vtos( p2 ) );
		return NULL;
	}
	if ( visible )
		DrawBox( p1, p2, 120000 );
	return ent;
}

/*QUAKED InvisibleBlockingWall (0.75 0.3 0.9) ?
A box that stops players and lets shots and sabers through, as the JA+ command BlockWallCreate makes one. Not
in JA+ as a class: this is the wall for the entity editor of the web admin panel, kept by its layout and not by
BlockWallWrite.
"mins" the low corner of the box, from its origin
"maxs" the high corner of the box, from its origin
Each side is rounded up to an even number of units. A long thin wall is made of many boxes: one that would
take more than 64 gets thicker.
*/
void SP_InvisibleBlockingWall( gentity_t *ent ) {
	vec3_t mins, maxs, p1, p2;

	G_SpawnVector( "mins", "0 0 0", mins );
	G_SpawnVector( "maxs", "0 0 0", maxs );
	if ( maxs[0] <= mins[0] || maxs[1] <= mins[1] || maxs[2] <= mins[2] ) {
		Com_Printf( "%s at %s needs mins and maxs, mins below maxs\n", BLOCKWALL_CLASS, vtos( ent->s.origin ) );
		G_FreeEntity( ent );
		return;
	}
	VectorAdd( ent->s.origin, mins, p1 );
	VectorAdd( ent->s.origin, maxs, p2 );
	if ( !SpawnWall( ent, p1, p2 ) ) {
		Com_Printf( "%s at %s is too large or the map has no entities left for it\n", BLOCKWALL_CLASS, vtos( ent->s.origin ) );
		G_FreeEntity( ent );
		return;
	}
	ent->genericValue1 = 1;		// of the editor: BlockWallWrite leaves it out
}

void JP_LoadBlockWalls( void ) {
	static char buf[BLOCKWALL_MAXFILE];
	char mapname[MAX_QPATH], file[MAX_QPATH];
	fileHandle_t f;
	vec3_t p1, p2;
	const char *p, *tok;
	int len, i;

	trap->Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
	for ( p = mapname; *p; p++ ) {
		if ( *p == '/' )
			mapname[p - mapname] = '_';
	}
	Com_sprintf( file, sizeof( file ), "%s_holes.cfg", mapname );
	Com_Printf( "^5Map Holes File Name:^7%s\n", file );

	len = trap->FS_Open( file, &f, FS_READ );
	if ( len < 0 ) {
		Com_Printf( "^5Map Blocking Holes Config File: ^7error reading file %s, or no holes config file for this map!\n", file );
		return;
	}
	if ( len >= BLOCKWALL_MAXFILE ) {
		Com_Printf( "Map Holes Config File %s is too large\n", file );
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
			return;
		p1[0] = atof( tok );
		for ( i = 1; i < 6; i++ ) {
			if ( COM_ParseString( &p, &tok ) )
				return;
			if ( i < 3 )
				p1[i] = atof( tok );
			else
				p2[i - 3] = atof( tok );
		}
		CreateBlockingHole( p1, p2, qfalse );
		SkipRestOfLine( &p );
	}
}

// must be called after the map ents are spawned
void JP_WriteBlockWalls( void ) {
	char mapname[MAX_QPATH], file[MAX_QPATH], line[256];
	fileHandle_t f;
	int i;

	trap->Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
	for ( i = 0; mapname[i]; i++ ) {
		if ( mapname[i] == '/' )
			mapname[i] = '_';
	}
	Com_sprintf( file, sizeof( file ), "%s_holes.cfg", mapname );
	Com_Printf( "^5Writing Map Holes File Name:^7%s\n", file );

	trap->FS_Open( file, &f, FS_WRITE );
	if ( !f )
		return;
	for ( i = 1; i < level.num_entities; i++ ) {
		const gentity_t *ent = &g_entities[i];

		// one of the editor of the web panel comes back with its layout, twice if it was in this file too
		if ( !IsBlockWall( ent ) || ent->genericValue1 )
			continue;
		Com_sprintf( line, sizeof( line ), "%.0f %.0f %.0f %.0f %.0f %.0f\n",
			ent->r.mins[0] + ent->r.currentOrigin[0], ent->r.mins[1] + ent->r.currentOrigin[1], ent->r.mins[2] + ent->r.currentOrigin[2],
			ent->r.maxs[0] + ent->r.currentOrigin[0], ent->r.maxs[1] + ent->r.currentOrigin[1], ent->r.maxs[2] + ent->r.currentOrigin[2] );
		trap->FS_Write( line, strlen( line ), f );
	}
	trap->FS_Close( f );
}

static void DrawAllBlockWalls( void ) {
	vec3_t mins, maxs;
	int i;

	for ( i = 1; i < level.num_entities; i++ ) {
		const gentity_t *ent = &g_entities[i];

		if ( !IsBlockWall( ent ) )
			continue;
		VectorAdd( ent->r.currentOrigin, ent->r.mins, mins );
		VectorAdd( ent->r.currentOrigin, ent->r.maxs, maxs );
		DrawBox( mins, maxs, 120000 );
	}
}

// BlockWallDraw, BlockWallWrite, BlockWallGrantEditor <player>
qboolean JP_BlockWallConsoleCommand( const char *cmd ) {
	char arg[MAX_TOKEN_CHARS];
	gentity_t *ent;
	int num;

	if ( !Q_stricmp( cmd, "BlockWallDraw" ) )	{ DrawAllBlockWalls();	return qtrue; }
	if ( !Q_stricmp( cmd, "BlockWallWrite" ) )	{ JP_WriteBlockWalls();	return qtrue; }
	if ( Q_stricmp( cmd, "BlockWallGrantEditor" ) )
		return qfalse;

	if ( trap->Argc() < 2 ) {
		trap->Print( "Usage:  BlockWallGrantEditor < ID player or partial player name>\n" );
		return qtrue;
	}
	trap->Argv( 1, arg, sizeof( arg ) );
	num = G_getClientNumberForString( arg );
	if ( num < 0 || num >= level.maxclients )
		return qtrue;
	ent = &g_entities[num];
	if ( !ent->client || ent->client->pers.connected != CON_CONNECTED )
		return qtrue;

	ent->client->pers.jp.blockEditor = !ent->client->pers.jp.blockEditor;
	trap->Print( ent->client->pers.jp.blockEditor ? "^5Granted Invisible Blocking Wall Editor access to User ^7%s\n"
		: "^5Removed Invisible Blocking Wall Editor access to User ^7%s\n", ent->client->pers.netname );
	return qtrue;
}

// common checks of the editor commands
static qboolean EditorAllowed( gentity_t *ent, qboolean needSpectator ) {
	if ( !ent->client || ent->client->pers.connected != CON_CONNECTED )
		return qfalse;
	if ( !ent->client->pers.jp.blockEditor ) {
		trap->SendServerCommand( ent - g_entities, "print \"An Rcon User must allow you to access Blocking Wall Edition with ^5BlockWallGrantEditor.\n\"" );
		return qfalse;
	}
	if ( needSpectator && ent->client->sess.sessionTeam != TEAM_SPECTATOR ) {
		trap->SendServerCommand( ent - g_entities, "print \"You need to be a spectator to use the blocking wall editor cmds.\n\"" );
		return qfalse;
	}
	return qtrue;
}

static void MarkCorner( const vec3_t o ) {
	vec3_t end;
	int i;

	for ( i = 0; i < 3; i++ ) {
		VectorCopy( o, end );
		end[i] += 40.0f;
		G_TestLine( (float *)o, end, 0xff, 30000 );
	}
}

void Cmd_BlockWallCreate_f( gentity_t *ent ) {
	jpClient_t *jp;
	gentity_t *wall;

	if ( !EditorAllowed( ent, qtrue ) )
		return;
	jp = &ent->client->pers.jp;

	if ( VectorCompare( jp->blockPoint1, vec3_origin ) ) {
		trap->SendServerCommand( ent - g_entities, va( "print \"^5Blocking Wall Point 1:%s\n\"", vtos( ent->client->ps.origin ) ) );
		VectorCopy( ent->client->ps.origin, jp->blockPoint1 );
		MarkCorner( ent->client->ps.origin );
		return;
	}
	trap->SendServerCommand( ent - g_entities, va( "print \"^5Blocking Wall Point 2:%s\n\"", vtos( ent->client->ps.origin ) ) );
	wall = CreateBlockingHole( jp->blockPoint1, ent->client->ps.origin, qtrue );
	if ( !wall ) {
		trap->SendServerCommand( ent - g_entities, "print \"^1This blocking wall is too large, or the map has no entities left for it.\n\"" );
		VectorClear( jp->blockPoint1 );
		return;
	}
	jp->blockCurrent = wall - g_entities;
	G_LogPrintf( "BLOCKING WALL: %s created a blocking wall %s, %s\n", ent->client->pers.netname, vtos( jp->blockPoint1 ), vtos( ent->client->ps.origin ) );
	VectorClear( jp->blockPoint1 );
}

void Cmd_BlockWallDel_f( gentity_t *ent ) {
	int cur;

	if ( !EditorAllowed( ent, qfalse ) )
		return;
	cur = ent->client->pers.jp.blockCurrent;
	if ( cur < 0 || cur >= MAX_GENTITIES ) {
		trap->SendServerCommand( ent - g_entities, "print \"You cannot delete this current blocking wall or bad entity number\n\"" );
		return;
	}
	if ( IsBlockWall( &g_entities[cur] ) ) {
		G_FreeEntity( &g_entities[cur] );
		trap->SendServerCommand( ent - g_entities, va( "print \"^5You deleted the current blocking wall^7 entity ID:^5%i.\n\"", cur ) );
		G_LogPrintf( "BLOCKING WALL: %s deleted a blocking wall. Entity ID %i\n", ent->client->pers.netname, cur );
	}
}

// dir 1: next entity, -1: previous entity; goes to the wall and teleports the editor next to its mins corner
static void GotoBlockWall( gentity_t *ent, int dir ) {
	int cur = ent->client->pers.jp.blockCurrent, i;
	vec3_t org;

	if ( !EditorAllowed( ent, qtrue ) )
		return;
	if ( cur < (dir > 0 ? 0 : 1) || cur > (dir > 0 ? MAX_GENTITIES - 2 : MAX_GENTITIES ) ) {
		trap->SendServerCommand( ent - g_entities, va( "print \"^1%s blocking wall.\n\"", dir > 0 ? "No Next" : " No Previous" ) );
		return;
	}
	for ( i = cur + dir; i > 0 && i < level.num_entities; i += dir ) {
		if ( !IsBlockWall( &g_entities[i] ) )
			continue;
		ent->client->pers.jp.blockCurrent = i;
		trap->SendServerCommand( ent - g_entities, va( "print \"^5%s blocking wall^7 entity ID:^5%i.\n\"", dir > 0 ? "Next" : "Previous", i ) );
		VectorAdd( g_entities[i].r.currentOrigin, g_entities[i].r.mins, org );
		JP_ManualTeleport( ent, org, ent->client->ps.viewangles );
		return;
	}
	trap->SendServerCommand( ent - g_entities, va( "print \"^1%s blocking wall to the last one^7 entity ID:^5%i.\n\"", dir > 0 ? "No Next" : " No Previous", cur ) );
}

void Cmd_BlockWallNext_f( gentity_t *ent ) { GotoBlockWall( ent, 1 ); }
void Cmd_BlockWallPrev_f( gentity_t *ent ) { GotoBlockWall( ent, -1 ); }
