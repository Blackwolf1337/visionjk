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
#include "botlib/botlib.h"
#include "game/bots/ai_main.h"

/*
=================
WA_ApiWaypoints, WA_ApiWaypoint

Not in JKAdmin. The waypoints the bots walk along (ai_wpnav.c), for the editor of the page: where they are and
which ones each leads to, and adding, removing and saving them with the functions the waypoint commands of the
game use (bot_wp_add, bot_wp_rem, bot_wp_save). As with those commands the bots stand still from the first
change until the route is saved: what leads where is only worked out again by the save. Remove, flags and move
take several waypoints at once, revert loads the route again as the map start did, default puts the saved
route of the server folder aside first, so that the one of the map's own files is found.
=================
*/
extern wpobject_t	*gWPArray[MAX_WPARRAY_SIZE];	// ai_wpnav.c
extern int			gWPNum;
extern float		gDeactivated;
extern bot_state_t	*botstates[MAX_CLIENTS];		// ai_main.c
void	CreateNewWP( vec3_t origin, int flags );
int		CreateNewWP_InTrail( vec3_t origin, int flags, int afterindex );
void	RemoveWP_InTrail( int afterindex );
void	RemoveAllWP( void );
void	LoadPath_ThisLevel( void );
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

// The waypoints a request names: indexes as 3,5,9 or one index. Each of them is there. They come out without
// doubles and the highest first, so that removing one does not move the ones still to come. Returns how many,
// 0 for a list that names one that is not there.
static int WA_WaypointList( const char *body, int *out ) {
	static char		list[MAX_WPARRAY_SIZE * 5 + 16];
	static qboolean	named[MAX_WPARRAY_SIZE];
	const char		*p;
	int				i, count = 0;

	if ( !WA_FormValue( body, "indexes", list, sizeof( list ) ) || !list[0] )
		WA_FormValue( body, "index", list, sizeof( list ) );
	memset( named, 0, sizeof( named ) );
	for ( p = list; *p; ) {
		char		*end;
		const long	v = strtol( p, &end, 10 );

		if ( end == p || ( *end && *end != ',' ) || v < 0 || v >= gWPNum || !gWPArray[v] || !gWPArray[v]->inuse )
			return 0;
		named[v] = qtrue;
		p = *end ? end + 1 : end;
	}
	for ( i = gWPNum - 1; i >= 0; i-- ) {
		if ( named[i] )
			out[count++] = i;
	}
	return count;
}

// The route as the start of the map loaded it: what the engine finds as botroutes/<map>.wnt. A bot holds
// waypoints of the route that was there, it looks for its way anew.
static void WA_RouteLoad( void ) {
	int i;

	RemoveAllWP();
	LoadPath_ThisLevel();
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		bot_state_t *bs = botstates[i];

		if ( bs )
			bs->wpCurrent = bs->wpDestination = bs->wpStoreDest = bs->wpCamping = bs->wpCampingTo = NULL;
	}
	gDeactivated = 0;
}

// POST /api/waypoint: op add (x, y, z, flags, and after: the waypoint it comes behind, none for the end),
// remove (indexes), flags (indexes, flags), move (indexes, dx, dy, dz), save, revert or default
void WA_ApiWaypoint( waConnection_t *c, const char *body ) {
	static int	list[MAX_WPARRAY_SIZE];
	char		op[16], value[32], mapname[MAX_QPATH];
	vec3_t		origin;
	int			index = -1, count, k;

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
		count = WA_WaypointList( body, list );
		if ( !count ) {
			WA_ActionResult( c, qfalse, "no such waypoint" );
			return;
		}
		for ( k = 0; k < count; k++ )
			RemoveWP_InTrail( list[k] );
		gDeactivated = 1;
		WA_ActionResult( c, qtrue, va( "%d removed, %d left. The bots wait until the route is saved", count, gWPNum ) );
	} else if ( !strcmp( op, "flags" ) ) {
		// what a bot does at a waypoint that is there (bot_wp_switchflags of the game)
		count = WA_WaypointList( body, list );
		if ( !count || !WA_FormValue( body, "flags", value, sizeof( value ) ) ) {
			WA_ActionResult( c, qfalse, "no such waypoint" );
			return;
		}
		for ( k = 0; k < count; k++ )
			gWPArray[list[k]]->flags = atoi( value );
		gDeactivated = 1;
		WA_ActionResult( c, qtrue, va( "%d changed. The bots wait until the route is saved", count ) );
	} else if ( !strcmp( op, "move" ) ) {
		static const char *const axes[3] = { "dx", "dy", "dz" };
		vec3_t by;

		count = WA_WaypointList( body, list );
		if ( !count ) {
			WA_ActionResult( c, qfalse, "no such waypoint" );
			return;
		}
		for ( k = 0; k < 3; k++ )
			by[k] = WA_FormValue( body, axes[k], value, sizeof( value ) ) ? atof( value ) : 0.0f;
		for ( k = 0; k < count; k++ ) {
			wpobject_t *wp = gWPArray[list[k]];
			int axis;

			for ( axis = 0; axis < 3; axis++ )
				wp->origin[axis] = Com_Clamp( -MAX_WORLD_COORD, MAX_WORLD_COORD, wp->origin[axis] + by[axis] );
		}
		gDeactivated = 1;
		WA_ActionResult( c, qtrue, va( "%d moved. The bots wait until the route is saved", count ) );
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
	} else if ( !strcmp( op, "revert" ) ) {
		WA_RouteLoad();
		WA_ActionResult( c, qtrue, gWPNum ? va( "the route was loaded again, %d waypoints. The bots walk again", gWPNum )
			: "no route is saved for this map, it has none now" );
	} else if ( !strcmp( op, "default" ) ) {
		char		rel[MAX_QPATH + 32], path[MAX_OSPATH], bak[MAX_OSPATH + 8];
		qboolean	exists, aside = qfalse;

		// The route that was saved lies in the server folder, where the engine looks first. Out of the way, the
		// one of the map's own files is what a load finds.
		trap->Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
		Com_sprintf( rel, sizeof( rel ), "botroutes/%s.wnt", mapname );
		if ( !strstr( rel, ".." ) && WA_FsResolve( qfalse, rel, qfalse, path, sizeof( path ), &exists ) && exists ) {
			Com_sprintf( bak, sizeof( bak ), "%s.bak", path );
			remove( bak );		// rename() does not replace a file there
			if ( rename( path, bak ) ) {
				WA_ActionResult( c, qfalse, "the saved route could not be put aside" );
				return;
			}
			aside = qtrue;
		}
		WA_RouteLoad();
		if ( aside && !gWPNum ) {
			// the saved one was all there is (a server whose folder is the installation): it stays
			rename( bak, path );
			WA_RouteLoad();
			WA_ActionResult( c, qfalse, "this map has no route besides the saved one, it stays" );
			return;
		}
		WA_ActionResult( c, qtrue, !gWPNum ? "this map comes without a route" : aside
			? va( "the route of the map is back, %d waypoints. The saved one is kept as botroutes/%s.wnt.bak", gWPNum, mapname )
			: va( "no route was saved, the one of the map was loaded again, %d waypoints", gWPNum ) );
	} else {
		WA_ActionResult( c, qfalse, "unknown op" );
	}
}

