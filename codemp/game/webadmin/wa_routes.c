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

// wa_routes.c -- the route of the bots in the web admin panel: /api/waypoints lists it, /api/waypoint
// changes it.

#include "game/webadmin/wa_local.h"

/*
=================
WA_ApiWaypoints, WA_ApiWaypoint

Not in JKAdmin. The waypoints the bots walk along (ai_wpnav.c), for the editor of the page: where they are and
which ones each leads to, and adding, removing and saving them with the functions the waypoint commands of the
game use (bot_wp_add, bot_wp_rem, bot_wp_save). As with those commands the bots stand still from the first
change until the route is saved: what leads where is only worked out again by the save.
=================
*/
extern wpobject_t	*gWPArray[MAX_WPARRAY_SIZE];	// ai_wpnav.c
extern int			gWPNum;
extern float		gDeactivated;
void	CreateNewWP( vec3_t origin, int flags );
int		CreateNewWP_InTrail( vec3_t origin, int flags, int afterindex );
void	RemoveWP_InTrail( int afterindex );
int		SavePathData( const char *filename );

void WA_ApiWaypoints( waConnection_t *c ) {
	waBuffer_t	b = { 0 };
	int			i, n;

	WA_BufPrintf( &b, "{\"max\":%d,\"editing\":%s,\"points\":[", MAX_WPARRAY_SIZE, WA_BOOL( gDeactivated != 0 ) );
	for ( i = 0; i < gWPNum; i++ ) {
		const wpobject_t *wp = gWPArray[i];

		if ( i )
			WA_BufAppend( &b, ",", 1 );
		if ( !wp || !wp->inuse ) {
			WA_BufAppend( &b, "null", 4 );
			continue;
		}
		WA_BufPrintf( &b, "{\"flags\":%d,\"pos\":", wp->flags );
		WA_AppendVec( &b, wp->origin );
		WA_BufAppend( &b, ",\"next\":[", 9 );
		for ( n = 0; n < wp->neighbornum && n < MAX_NEIGHBOR_SIZE; n++ )
			WA_BufPrintf( &b, "%s%d", n ? "," : "", wp->neighbors[n].num );
		WA_BufAppend( &b, "]}", 2 );
	}
	WA_BufAppend( &b, "]}", 2 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

// POST /api/waypoint: op add (x, y, z, flags, and after: the waypoint it comes behind, none for the end),
// remove (index) or save
void WA_ApiWaypoint( waConnection_t *c, const char *body ) {
	char	op[16], value[32], mapname[MAX_QPATH];
	vec3_t	origin;
	int		index = -1;

	WA_FormValue( body, "op", op, sizeof( op ) );
	WA_LogUse( c, va( "waypoint %.15s %.60s", op, strchr( body, '&' ) ? strchr( body, '&' ) + 1 : "" ) );

	if ( !strcmp( op, "add" ) ) {
		static const char *const axes[3] = { "x", "y", "z" };
		int k, flags = 0;

		if ( gWPNum >= MAX_WPARRAY_SIZE ) {
			WA_ActionResult( c, qfalse, va( "the route is full, %d waypoints", MAX_WPARRAY_SIZE ) );
			return;
		}
		for ( k = 0; k < 3; k++ ) {
			if ( !WA_FormValue( body, axes[k], value, sizeof( value ) ) ) {
				WA_ActionResult( c, qfalse, "no place for the waypoint" );
				return;
			}
			origin[k] = Com_Clamp( -MAX_WORLD_COORD, MAX_WORLD_COORD, atof( value ) );
		}
		if ( WA_FormValue( body, "flags", value, sizeof( value ) ) )
			flags = atoi( value );
		if ( WA_FormValue( body, "after", value, sizeof( value ) ) && value[0] )
			index = atoi( value );

		if ( index >= 0 && index < gWPNum - 1 )
			CreateNewWP_InTrail( origin, flags, index );
		else
			CreateNewWP( origin, flags );
		gDeactivated = 1;
		WA_ActionResult( c, qtrue, va( "waypoint added, %d now. The bots wait until the route is saved", gWPNum ) );
	} else if ( !strcmp( op, "remove" ) ) {
		if ( WA_FormValue( body, "index", value, sizeof( value ) ) && value[0] )
			index = atoi( value );
		if ( index < 0 || index >= gWPNum ) {
			WA_ActionResult( c, qfalse, "no such waypoint" );
			return;
		}
		RemoveWP_InTrail( index );
		gDeactivated = 1;
		WA_ActionResult( c, qtrue, va( "waypoint removed, %d left. The bots wait until the route is saved", gWPNum ) );
	} else if ( !strcmp( op, "flags" ) ) {
		// what a bot does at a waypoint that is there (bot_wp_switchflags of the game)
		if ( WA_FormValue( body, "index", value, sizeof( value ) ) && value[0] )
			index = atoi( value );
		if ( index < 0 || index >= gWPNum || !gWPArray[index] || !gWPArray[index]->inuse || !WA_FormValue( body, "flags", value, sizeof( value ) ) ) {
			WA_ActionResult( c, qfalse, "no such waypoint" );
			return;
		}
		gWPArray[index]->flags = atoi( value );
		gDeactivated = 1;
		WA_ActionResult( c, qtrue, va( "waypoint %d changed. The bots wait until the route is saved", index ) );
	} else if ( !strcmp( op, "save" ) ) {
		if ( !gWPNum ) {
			WA_ActionResult( c, qfalse, "there are no waypoints to save" );
			return;
		}
		trap->Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
		if ( SavePathData( mapname ) ) {
			gDeactivated = 0;
			WA_ActionResult( c, qtrue, va( "saved as botroutes/%s.wnt, %d waypoints. The bots walk again", mapname, gWPNum ) );
		} else {
			WA_ActionResult( c, qfalse, "the route could not be saved, the server console says why" );
		}
	} else {
		WA_ActionResult( c, qfalse, "unknown op" );
	}
}

