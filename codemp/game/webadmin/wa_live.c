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

// wa_live.c -- the live view: what the map view of the panel draws, for everybody and without a login

#include "game/webadmin/wa_local.h"

#include "webadmin_live.h"

/*
=================
The live view

Not in JKAdmin. vs_liveView 1 opens /live/ of the panel's port to every visitor: the script that draws the map
as the panel does (page/live.js, the drawing parts of the page and js/live-boot.js) and the few questions it
asks, read only, and its WebSocket (/live/stream, wa_stream.c). A website puts it in front of its visitors
through a reverse proxy (jka.vision/live/).

What a visitor gets is what a spectator in the game sees: the map, the players with their names, what moves.
Not the addresses of the players, not who is an admin, not the HUD of a player, not what the server prints.
Of the files of the game only what the map is drawn with, no sounds. The map is the one that runs: a proxy
that keeps the answer by its address keeps the right file under the name of each map.
=================
*/

// GET /live/api/map?name=<map>: only the map that runs, under its own name
static void WA_LiveMap( waConnection_t *c, const char *query ) {
	char name[MAX_QPATH], mapname[MAX_QPATH];

	trap->Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
	if ( !WA_FormValue( query, "name", name, sizeof( name ) ) || Q_stricmp( name, mapname ) ) {
		WA_RespondText( c, 404, "Not Found", "not the map that runs" );
		return;
	}
	WA_ApiMap( c );
}

// GET /live/api/file?name=: what WA_ApiFile hands out, without the sounds and the music
static void WA_LiveFile( waConnection_t *c, const char *query ) {
	char name[MAX_QPATH];

	if ( WA_FormValue( query, "name", name, sizeof( name ) ) && ( !Q_stricmpn( name, "sound/", 6 ) || !Q_stricmpn( name, "music/", 6 ) ) ) {
		WA_RespondText( c, 404, "Not Found", "not found" );
		return;
	}
	WA_ApiFile( c, query );
}

// GET /live/api/list?kind=: the lists the drawing asks for, not the ones of the editor
static void WA_LiveList( waConnection_t *c, const char *query ) {
	char kind[16];

	if ( !WA_FormValue( query, "kind", kind, sizeof( kind ) )
		|| ( strcmp( kind, "fxindex" ) && strcmp( kind, "events" ) && strcmp( kind, "mods" ) ) ) {
		WA_RespondText( c, 404, "Not Found", "not found" );
		return;
	}
	WA_ApiList( c, query );
}

// The script of the live view: built into the module, or put together from the files of vs_webRoot
static void WA_LiveScript( waConnection_t *c ) {
	static const char	type[] = "application/javascript; charset=utf-8";
	waBuffer_t			script = { 0 };

	if ( vs_webRoot.string[0] && WA_PageBundle( &script, "live.js" ) ) {
		WA_Respond( c, 200, "OK", type, "Cache-Control: no-cache\r\n", script.data, script.len );
	} else {
		WA_Respond( c, 200, "OK", type, "Cache-Control: no-cache\r\n", webadmin_live, (int)webadmin_live_len );
	}
	WA_BufFree( &script );
}

/*
=================
WA_LiveRequest

A request below /live/. qfalse: it is not one, the panel goes on with it. Anything below /live/ that is not
named here is not there, also while vs_liveView is 0.
=================
*/
qboolean WA_LiveRequest( waConnection_t *c, const char *method, const char *path, const char *query, const char *head ) {
	if ( Q_strncmp( path, "/live/", 6 ) )
		return qfalse;

	if ( !vs_liveView.integer || strcmp( method, "GET" ) ) {
		WA_RespondText( c, 404, "Not Found", "not found" );
		return qtrue;
	}

	path += 5;
	if ( !strcmp( path, "/live.js" ) ) {
		WA_LiveScript( c );
	} else if ( !strcmp( path, "/stream" ) ) {
		// the state pushed twenty times a second, as /api/state has it (wa_stream.c)
		WA_StreamUpgrade( c, head, qtrue );
	} else if ( !strcmp( path, "/api/state" ) ) {
		WA_ApiLiveState( c, query );
	} else if ( !strcmp( path, "/api/map" ) ) {
		WA_LiveMap( c, query );
	} else if ( !strcmp( path, "/api/file" ) ) {
		WA_LiveFile( c, query );
	} else if ( !strcmp( path, "/api/anims" ) ) {
		WA_ApiAnims( c );
	} else if ( !strcmp( path, "/api/shaders" ) ) {
		WA_ApiShaders( c );
	} else if ( !strcmp( path, "/api/remaps" ) ) {
		WA_ApiRemaps( c );
	} else if ( !strcmp( path, "/api/list" ) ) {
		WA_LiveList( c, query );
	} else {
		WA_RespondText( c, 404, "Not Found", "not found" );
	}
	return qtrue;
}
