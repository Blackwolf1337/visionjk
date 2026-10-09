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

// wa_clips.c -- the clip editor of the web admin panel: the clip brushes of the map that are off, and the
// blocking walls

#include "game/webadmin/wa_local.h"

/*
=================
The clip editor

Not in JKAdmin. Two things stop a player without being seen, and the editor has both.

A clip brush is part of the map file, which the server and every client load alike: it can not be moved or taken
away for everybody while the map runs. What the server can do is stop minding one (the command clipbrush of the
engine, CM_SetClipBrush), by its number in the map file, which the page knows from the file itself. The engine
keeps the numbers that are off in sv_clipOff and tells the clients of them (cm_clipOff): a client of this tree
turns the same brushes off and predicts as the server moves, any other one still has the brush, predicts a
stop first and is then set to where the server has the player.

A blocking wall is a box the server puts up while the map runs (japlus/g_japlus_blockwall.c), made of entities
that every client is told of and clips against. It is no entity of the entity editor: it is drawn, dragged and
shaped here, and is known by the number of its entity until the map ends.

Both are kept in one file per map, wallblocks/<map>.cfg, by Save, as the route of the bots is kept by its
save: without it a change is gone with the map.

GET /api/clips: what is off, the walls with their corners, and whether something changed since the last save.
POST /api/clip: op off or on (brush: numbers), wall (x1 y1 z1 x2 y2 z2, and id to put it in the place of
that wall), delete (id), save, revert (what the file has).
=================
*/
static qboolean clipsDirty;

static void WA_ClipResult( waConnection_t *c, qboolean ok, const char *msg, int id ) {
	waBuffer_t b = { 0 };

	WA_BufPrintf( &b, "{\"ok\":%s,\"id\":%d,\"message\":", WA_BOOL( ok ), id );
	WA_BufJsonString( &b, msg );
	WA_BufAppend( &b, "}", 1 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

void WA_ApiClips( waConnection_t *c ) {
	static char	off[4096];
	waBuffer_t	b = { 0 };
	const char	*p;
	qboolean	first = qtrue;
	int			i;

	trap->Cvar_VariableStringBuffer( "sv_clipOff", off, sizeof( off ) );
	WA_BufPrintf( &b, "{\"dirty\":%s,\"off\":[", WA_BOOL( clipsDirty ) );
	for ( p = off; *p; ) {
		char		*end;
		const long	n = strtol( p, &end, 10 );

		if ( end == p )
			break;
		WA_BufPrintf( &b, "%s%ld", first ? "" : ",", n );
		first = qfalse;
		p = end;
	}
	WA_BufAppend( &b, "],\"walls\":[", 11 );
	first = qtrue;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		vec3_t mins, maxs;

		if ( !JP_BlockWallBounds( &g_entities[i], mins, maxs ) )
			continue;
		WA_BufPrintf( &b, "%s{\"id\":%d,\"mins\":", first ? "" : ",", i );
		WA_AppendVec( &b, mins );
		WA_BufAppend( &b, ",\"maxs\":", 8 );
		WA_AppendVec( &b, maxs );
		WA_BufAppend( &b, "}", 1 );
		first = qfalse;
	}
	WA_BufAppend( &b, "]}", 2 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

// the wall a request names by the number of its entity, NULL when that is none
static gentity_t *WA_ClipWall( const char *body ) {
	char	value[16];
	vec3_t	mins, maxs;
	int		id;

	if ( !WA_FormValue( body, "id", value, sizeof( value ) ) || !value[0] )
		return NULL;
	id = atoi( value );
	return ( id >= MAX_CLIENTS && id < level.num_entities && JP_BlockWallBounds( &g_entities[id], mins, maxs ) ) ? &g_entities[id] : NULL;
}

void WA_ApiClip( waConnection_t *c, const char *body ) {
	static const char *const corners[6] = { "x1", "y1", "z1", "x2", "y2", "z2" };
	char		op[16], list[2048], value[32];
	gentity_t	*wall, *old;
	vec3_t		p[2];
	int			count, k;

	WA_FormValue( body, "op", op, sizeof( op ) );
	WA_LogUse( c, va( "clip %.15s %.200s", op, strchr( body, '&' ) ? strchr( body, '&' ) + 1 : "" ) );

	if ( !strcmp( op, "off" ) || !strcmp( op, "on" ) ) {
		const qboolean on = (qboolean)!strcmp( op, "on" );

		// the engine does it in its next frame and says on its console which numbers were no clip brush
		WA_FormValue( body, "brush", list, sizeof( list ) );
		count = JP_ClipBrushes( on, list );
		if ( !count ) {
			WA_ClipResult( c, qfalse, "no clip brush named", -1 );
			return;
		}
		clipsDirty = qtrue;
		WA_ClipResult( c, qtrue, va( "%d clip brush%s %s", count, count == 1 ? "" : "es", on ? "on again" : "off: players are let through there" ), -1 );
	} else if ( !strcmp( op, "wall" ) ) {
		for ( k = 0; k < 6; k++ ) {
			if ( !WA_FormValue( body, corners[k], value, sizeof( value ) ) || !value[0] ) {
				WA_ClipResult( c, qfalse, "a wall needs two corners", -1 );
				return;
			}
			p[k / 3][k % 3] = Com_Clamp( -MAX_WORLD_COORD, MAX_WORLD_COORD, atof( value ) );
		}
		old = WA_ClipWall( body );
		if ( !JP_BlockWallParts( p[0], p[1] ) ) {
			WA_ClipResult( c, qfalse, "this wall is too large: it is cut into boxes of 510 by 510 by 446 units at most, and 64 of them are the limit", old ? (int)( old-g_entities ) : -1 );
			return;
		}
		// the new one first: when the map has no room for it, the one it was to take the place of stays
		wall = JP_BlockWallCreate( p[0], p[1] );
		if ( !wall ) {
			WA_ClipResult( c, qfalse, "the map has no entities left for this wall", old ? (int)( old-g_entities ) : -1 );
			return;
		}
		if ( old )
			G_FreeEntity( old );
		clipsDirty = qtrue;
		WA_ClipResult( c, qtrue, old ? "wall changed" : "wall placed", (int)( wall-g_entities ) );
	} else if ( !strcmp( op, "delete" ) ) {
		old = WA_ClipWall( body );
		if ( !old ) {
			WA_ClipResult( c, qfalse, "no such wall", -1 );
			return;
		}
		G_FreeEntity( old );
		clipsDirty = qtrue;
		WA_ClipResult( c, qtrue, "wall removed", -1 );
	} else if ( !strcmp( op, "save" ) ) {
		if ( !JP_SaveBlockWalls() ) {
			WA_ClipResult( c, qfalse, "the file of the walls could not be written", -1 );
			return;
		}
		clipsDirty = qfalse;
		WA_ClipResult( c, qtrue, "Saved. The walls and the clips that are off come back with the map.", -1 );
	} else if ( !strcmp( op, "revert" ) ) {
		// as the start of the map has it: every clip on, no wall, then what the file says
		JP_ClearBlockWalls();
		trap->SendConsoleCommand( EXEC_APPEND, "clipbrush on all\n" );
		count = JP_LoadBlockWalls();
		clipsDirty = qfalse;
		WA_ClipResult( c, qtrue, va( "Back to what is saved: %d wall%s", count, count == 1 ? "" : "s" ), -1 );
	} else {
		WA_ClipResult( c, qfalse, "unknown op", -1 );
	}
}
