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
#include "game/g_local.h"

#define BLOCKWALL_CLASS	"InvisibleBlockingWall"
#define BLOCKWALL_MAXFILE	0x10000

static qboolean IsBlockWall( const gentity_t *ent ) {
	return ent->inuse && ent->classname && !Q_stricmp( ent->classname, BLOCKWALL_CLASS );
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

// p1 and p2 are opposite corners in world space, visible: show the box to everybody for 2 minutes
static gentity_t *CreateBlockingHole( const vec3_t p1, const vec3_t p2, qboolean visible ) {
	gentity_t *ent = G_Spawn();
	vec3_t mid;
	int i;

	for ( i = 0; i < 3; i++ ) {
		mid[i] = (p1[i] + p2[i]) * 0.5f;
		ent->r.mins[i] = Q_min( p1[i], p2[i] ) - mid[i];
		ent->r.maxs[i] = Q_max( p1[i], p2[i] ) - mid[i];
	}
	G_SetOrigin( ent, mid );
	if ( visible )
		DrawBox( p1, p2, 120000 );

	ent->classname = BLOCKWALL_CLASS;
	ent->r.contents = CONTENTS_PLAYERCLIP;
	ent->s.eType = ET_SPECIAL;
	ent->r.svFlags |= SVF_NOCLIENT;
	ent->r.svFlags &= ~SVF_BROADCAST;
	trap->LinkEntity( (sharedEntity_t *)ent );
	return ent;
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

		if ( !IsBlockWall( ent ) )
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
