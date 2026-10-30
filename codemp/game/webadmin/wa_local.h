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
//   wa_http.c       buffers, answers, and what a request is made of: its values, the wait after wrong logins
//   wa_auth.c       accounts, roles and sessions, the login with a password or a key, the second step
//   wa_totp.c       the codes by time of the second step
//   wa_state.c      /api/state, what the page draws ten times a second
//   wa_actions.c    /api/action, what is done to a player or to the server, and the bans
//   wa_entities.c   the entity editor: the list, placing and changing, what comes back after a reload
//   wa_assets.c     what the page draws with: the map, the files of the game, the lists to pick from
//   wa_fs.c         the file manager and the server config
//   wa_perf.c       /api/perf, how the server keeps up
//   wa_log.c        the chat, the log of the game, and the console: what it prints and a line to run on it
//   wa_routes.c     the route of the bots
//   wa_control.c    a player who is moved from the page
//   wa_clips.c      the clip editor: the clip brushes of the map that are off, and the blocking walls
//   wa_remaps.c     the texture swap: a shader of the map drawn as another one
//   wa_live.c       the live view: /live/, the map view for everybody, read only
//   wa_stream.c     the WebSocket of a page: the state pushed, and what it subscribed to
//   wa_settings.c   the settings form: every cvar of the server with its type, saved into the config and set
//   wa_cfgedit.c    a config as statements: the value of one cvar changed, a statement taken out (no trap)

#pragma once

#include "game/bg/bg_net.h"		// the sockets, in front of everything else
#include "game/g_local.h"
#include "web/web_http.h"		// the headers of a request and base64, shared with the web host of the dedicated server
#include "game/webadmin/wa_totp.h"

// A WebSocket holds its connection for as long as its page is open (wa_stream.c): those of the live view have a
// limit of their own, so that they never take the last connections from the panel.
#define WA_MAX_CONNECTIONS	96
#define WA_MAX_LIVE_STREAMS	64
#define WA_MAX_CHANNELS		6		// what a page asks for besides the state over its socket
#define WA_STREAM_AUTH		1536	// the Cookie and Authorization of the upgrade, asked again every second
#define WA_MAX_REQUEST		65536		// head and body. The largest body is the text of the server config.
#define WA_TIMEOUT_MS		15000
#define WA_MAX_FILE			( 64 * 1024 * 1024 )

#define WA_USER_NAME		24

#define WA_BOOL( x )		( ( x ) ? "true" : "false" )
#define WA_ENTITY_RESERVE	64		// free entities the editor leaves to the game: shots, effects, bodies

typedef struct waBuffer_s {
	char	*data;
	int		len;
	int		size;
} waBuffer_t;

// what an account may do, each role what the one before it may and more (wa_auth.c)
typedef enum waRole_e {
	WA_ROLE_NONE,			// not logged in
	WA_ROLE_MODERATOR,		// the players
	WA_ROLE_ADMIN,			// and the editor, the map, the log
	WA_ROLE_OWNER			// and the console, the config, the files, the accounts
} waRole_t;

typedef struct waConnection_s {
	qboolean		inUse;
	bgSocket_t		sock;
	unsigned int	addr;			// IPv4 of the peer
	// of the request, filled in before it is handled
	unsigned int	peer;			// who asks, for the wait after wrong logins and the log: addr, or behind vs_webProxy who the proxy says it is
	qboolean		proxied;		// it came through vs_webProxy
	qboolean		https;			// and the visitor speaks HTTPS with the proxy
	waRole_t		role;
	char			user[WA_USER_NAME];
	int				session;		// the session it is logged in by, -1: none or a token
	char		request[WA_MAX_REQUEST + 1];
	int			requestLen;
	waBuffer_t	response;
	int			sent;
	int			lastActive;		// trap->Milliseconds()
	fileHandle_t	file;		// a file of the game that is handed out piece by piece, 0 for none
	int			fileLeft;		// what of it is not read yet
	qboolean	loginWait;		// a login whose password is hashed beside the game (wa_auth.c)

	// a WebSocket (wa_stream.c). request holds the frames of the page that are not whole yet, response what
	// waits to be sent.
	qboolean		stream;
	qboolean		live;			// of the live view: no login, the state as everybody may see it, nothing else
	qboolean		closing;		// a close frame is out, the connection ends when it is sent
	char			streamAuth[WA_STREAM_AUTH];
	char			stateHas[1024];	// what the page has of the state (WA_StateNext)
	char			stateWants[32];	// view= and hud= of the page
	int				nextAuth;		// trap->Milliseconds
	struct {
		char		name[16];		// what the answers are sent under
		char		path[24];		// a GET of the panel
		char		query[256];
		unsigned	hash;			// of the last answer that was sent, 0 for none
		int			due;			// trap->Milliseconds of when it is asked again
	} channels[WA_MAX_CHANNELS];
} waConnection_t;

// g_webadmin.c
qboolean WA_Running( void );
waRole_t WA_RouteRole( const char *method, const char *path );
void WA_Route( waConnection_t *c, const char *method, const char *path, const char *query, char *body, int bodyLen );
void WA_CloseConnection( waConnection_t *c );
int WA_LiveStreams( void );
qboolean WA_PageBundle( waBuffer_t *page, const char *name );

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
void WA_RespondJsonText( waConnection_t *c, const char *text );
qboolean WA_GamePathOk( const char *path );
#define WA_NO_ID			( -0x7fffffff - 1 )	// WA_Result() without an id
void WA_Result( waConnection_t *c, qboolean ok, const char *msg, int id, const char *key );
void WA_LogUse( const waConnection_t *c, const char *what );
qboolean WA_AuthThrottle( unsigned int addr );
void WA_AuthResult( unsigned int addr, qboolean ok );
qboolean WA_FormValue( const char *form, const char *key, char *out, int outSize );
void WA_Utf8ToLatin1( char *s );
char *WA_ReadGameFile( const char *path, int *outLen );
void WA_BufLatin1AsUtf8( waBuffer_t *b, const char *text, int len );
int WA_TextBody( char *text );
qboolean WA_WriteGameFile( const char *path, const char *data, int len );

// wa_auth.c
const char *WA_RoleName( waRole_t role );
void WA_AuthAnswer( waConnection_t *c, int status, const char *statusText, const char *why );
void WA_AuthInit( void );
void WA_AuthShutdown( void );
qboolean WA_AuthHasUsers( void );
void WA_AuthRequest( waConnection_t *c, const char *head );
qboolean WA_AuthRoute( waConnection_t *c, const char *method, const char *path, const char *head, const char *body );
void WA_AuthLoginFrame( waConnection_t *c );
void WA_AuthLoginGone( waConnection_t *c );
qboolean WA_AuthConsoleCommand( const char *cmd );

// wa_state.c
const char *WA_TeamName( team_t team );
void WA_AppendVec( waBuffer_t *b, const vec3_t v );
void WA_ApiState( waConnection_t *c, const char *query );
void WA_ApiLiveState( waConnection_t *c, const char *query );
void WA_StateNext( char *out, int outSize );
void WA_EventsFrame( void );
void WA_AnimsFrame( void );
void WA_PrintsTake( const char *command );

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
void WA_ApiClassKeys( waConnection_t *c );

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
void WA_ApiConsole( waConnection_t *c, const char *query );
void WA_ApiIds( waConnection_t *c, const char *query );
void WA_ApiCommand( waConnection_t *c, const char *body );

// wa_routes.c
void WA_ApiWaypoints( waConnection_t *c );
void WA_ApiWaypoint( waConnection_t *c, const char *body );

// wa_control.c
void WA_ApiControl( waConnection_t *c, const char *body );

// wa_clips.c
void WA_ApiClips( waConnection_t *c );
void WA_ApiClip( waConnection_t *c, const char *body );

// wa_remaps.c
void WA_RemapsFrame( void );
void WA_ApiRemaps( waConnection_t *c );

// wa_live.c
qboolean WA_LiveRequest( waConnection_t *c, const char *method, const char *path, const char *query, const char *head );

// wa_settings.c
typedef enum waSetType_e {
	WA_SET_TEXT,
	WA_SET_INT,			// min and max, both 0: any
	WA_SET_FLOAT,
	WA_SET_BOOL,		// 0 or 1
	WA_SET_ENUM,		// options "0=Free for all|1=Holocron|..."
	WA_SET_BITS,		// options "0=Heal|1=Jump|...", the number is that of the bit
	WA_SET_SECRET		// a password: a text field that does not show what it holds
} waSetType_t;

// the groups of the form, in the order the page lists them
typedef enum waSetCat_e {
	WA_CAT_SERVER,		// name, message of the day, slots, passwords
	WA_CAT_NETWORK,		// rates, snapshots, the masters, downloads, flood protection
	WA_CAT_GAMETYPE,	// the game type and its limits
	WA_CAT_VOTES,		// votes and the map cycle
	WA_CAT_FORCE,		// force powers, ranks, weapons and items
	WA_CAT_SABER,
	WA_CAT_GAMEPLAY,	// gravity, speed, knockback, damage, dismemberment
	WA_CAT_DUEL,
	WA_CAT_BOTS,
	WA_CAT_JPADMIN,		// the admin system of JA+
	WA_CAT_JPFEATURES,	// the rest of JA+
	WA_CAT_PROTECTION,	// anti-cheat, names, spam, bans
	WA_CAT_VISIONJK,	// voice, boombox, statuses, lag compensation and other things of this tree
	WA_CAT_WEB,			// the web panel
	WA_CAT_LOGGING,
	WA_CAT_MASTER,		// the masterserver
	WA_CAT_OTHER,		// a cvar of the game the table does not know
	WA_CAT_COUNT
} waSetCat_t;

#define WA_SET_MASTER		1		// the masterserver shows it as well
#define WA_SET_MASTERONLY	2		// only the masterserver shows it
#define WA_SET_RESTART		4		// takes effect after a restart of the server (when the engine does not say so itself)
#define WA_SET_LATCH		8		// takes effect with the next map (when the engine does not say so itself)

typedef struct waSetting_s {
	const char	*name;
	waSetCat_t	cat;
	waSetType_t	type;
	float		min, max;
	const char	*options;		// ENUM and BITS, else NULL
	int			flags;			// WA_SET_*
	const char	*desc;
} waSetting_t;

qboolean WA_MasterMode( void );
const char *WA_ConfigFile( void );
qboolean WA_SaveConfig( const char *text, int len );
void WA_ApiSettingsGet( waConnection_t *c );
void WA_ApiSettingsSet( waConnection_t *c, char *body );
void WA_ApiCleanupGet( waConnection_t *c );
void WA_ApiCleanupSet( waConnection_t *c, char *body );
void WA_ApiMaster( waConnection_t *c );

// wa_stream.c
void WA_StreamUpgrade( waConnection_t *c, const char *head, qboolean live );
void WA_StreamRead( waConnection_t *c );
void WA_StreamFrame( waConnection_t *c, int now );
void WA_StreamTick( int now );
void WA_ApiRemap( waConnection_t *c, const char *body );
