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

// wa_local.h -- what the files of the web admin panel share: the sizes, a buffer that grows, a connection,
// and the functions one file has for the others. The panel is not from JA+.
//
//   g_webadmin.c    the connections, which request goes where, and what the game calls (WebAdmin_*)
//   wa_http.c       buffers, answers, and what a request is made of: headers, the login, its values
//   wa_state.c      /api/state, what the page draws ten times a second
//   wa_actions.c    /api/action, what is done to a player or to the server, and the bans
//   wa_entities.c   the entity editor: the list, placing and changing, what comes back after a reload
//   wa_assets.c     what the page draws with: the map, the files of the game, the lists to pick from
//   wa_fs.c         the file manager and the server config
//   wa_perf.c       /api/perf, how the server keeps up
//   wa_log.c        the chat and the log of the game
//   wa_routes.c     the route of the bots

#pragma once

#include "game/bg_net.h"		// the sockets, in front of everything else
#include "game/g_local.h"

#define WA_MAX_CONNECTIONS	32
#define WA_MAX_REQUEST		65536		// head and body. The largest body is the text of the server config.
#define WA_TIMEOUT_MS		15000
#define WA_MAX_FILE			( 64 * 1024 * 1024 )

#define WA_BOOL( x )		( ( x ) ? "true" : "false" )
#define WA_ENTITY_RESERVE	64		// free entities the editor leaves to the game: shots, effects, bodies

typedef struct waBuffer_s {
	char	*data;
	int		len;
	int		size;
} waBuffer_t;

typedef struct waConnection_s {
	qboolean		inUse;
	bgSocket_t		sock;
	unsigned int	addr;			// IPv4 of the peer, for the login throttle
	char		request[WA_MAX_REQUEST + 1];
	int			requestLen;
	waBuffer_t	response;
	int			sent;
	int			lastActive;		// trap->Milliseconds()
	fileHandle_t	file;		// a file of the game that is handed out piece by piece, 0 for none
	int			fileLeft;		// what of it is not read yet
} waConnection_t;

// g_webadmin.c
qboolean WA_Running( void );

// wa_http.c
qboolean WA_BufReserve( waBuffer_t *b, int extra );
void WA_BufAppend( waBuffer_t *b, const void *data, int len );
void WA_BufPrintf( waBuffer_t *b, const char *fmt, ... );
void WA_BufFree( waBuffer_t *b );
void WA_BufJsonString( waBuffer_t *b, const char *s );
void WA_RespondHead( waConnection_t *c, int status, const char *statusText, const char *contentType,
	const char *extraHeaders, int bodyLen );
void WA_Respond( waConnection_t *c, int status, const char *statusText, const char *contentType,
	const char *extraHeaders, const void *body, int bodyLen );
void WA_RespondText( waConnection_t *c, int status, const char *statusText, const char *text );
void WA_RespondJson( waConnection_t *c, waBuffer_t *json );
void WA_LogUse( const waConnection_t *c, const char *what );
const char *WA_FindHeader( const char *head, const char *name );
qboolean WA_SameOrigin( const char *head );
qboolean WA_CheckAuth( const char *head );
qboolean WA_AuthThrottle( unsigned int addr );
void WA_AuthResult( unsigned int addr, qboolean ok );
qboolean WA_FormValue( const char *form, const char *key, char *out, int outSize );
void WA_Utf8ToLatin1( char *s );
char *WA_ReadGameFile( const char *path, int *outLen );
void WA_BufLatin1AsUtf8( waBuffer_t *b, const char *text, int len );
int WA_TextBody( char *text );
qboolean WA_WriteGameFile( const char *path, const char *data, int len );

// wa_state.c
const char *WA_TeamName( team_t team );
void WA_AppendVec( waBuffer_t *b, const vec3_t v );
void WA_ApiState( waConnection_t *c, const char *query );

// wa_actions.c
void WA_ActionResult( waConnection_t *c, qboolean ok, const char *msg );
void WA_ApiAction( waConnection_t *c, const char *body );
void WA_ApiBans( waConnection_t *c );
qboolean WA_GravityIsSet( int clientNum );
void WA_GravityFrame( void );

// wa_entities.c
int WA_FreeEntities( void );
qboolean WA_ConfigstringRoom( int start, int max, const char *name );
void WA_ApiEntities( waConnection_t *c );
void WA_ApiClasses( waConnection_t *c );
void WA_ApiExport( waConnection_t *c, const char *query );
void WA_ApiEntity( waConnection_t *c, const char *body );
void WA_ReloadMap( waConnection_t *c );
void WA_ApiLayout( waConnection_t *c, const char *body );
void WA_ReloadRead( void );
void WA_EntitiesFrame( void );
void WA_EntitiesShutdown( void );
void WA_ApiClassDoc( waConnection_t *c );

// wa_assets.c
int WA_MapBrushModels( void );
void WA_ApiMap( waConnection_t *c );
void WA_ApiFile( waConnection_t *c, const char *query );
void WA_ApiAnims( waConnection_t *c );
void WA_ApiList( waConnection_t *c, const char *query );
void WA_ApiShaders( waConnection_t *c );
void WA_ApiModels( waConnection_t *c );
void WA_AssetsFrame( void );
void WA_AssetsShutdown( void );

// wa_fs.c
void WA_ApiConfigGet( waConnection_t *c );
void WA_ApiConfigSet( waConnection_t *c, char *text, qboolean run );
qboolean WA_FsStat( const char *path, qboolean *isDir, double *size, double *mtime );
qboolean WA_FsResolve( qboolean install, const char *rel, qboolean mayCreate, char *out, int outSize, qboolean *exists );
void WA_ApiFsList( waConnection_t *c, const char *query );
void WA_ApiFsGet( waConnection_t *c, const char *query );
void WA_ApiFsPut( waConnection_t *c, const char *query, char *body, int bodyLen );
void WA_ApiFsDelete( waConnection_t *c, const char *query );
void WA_ApiFsRename( waConnection_t *c, const char *query );
void WA_ApiFsMkdir( waConnection_t *c, const char *query );

// wa_perf.c
void WA_PerfFrameBegin( void );
void WA_PerfPanelDone( void );
void WA_ApiPerf( waConnection_t *c, const char *query );

// wa_log.c
void WA_ChatInit( void );
void WA_ApiChat( waConnection_t *c, const char *query );
void WA_ApiLog( waConnection_t *c, const char *query );

// wa_routes.c
void WA_ApiWaypoints( waConnection_t *c );
void WA_ApiWaypoint( waConnection_t *c, const char *body );
