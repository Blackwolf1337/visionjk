/*
===========================================================================
Copyright (C) 2013 - 2015, OpenJK contributors

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

// g_webadmin.c -- interactive web admin panel served by the game module.
//
// A tiny non-blocking HTTP server that is polled once per server frame. It
// serves one embedded page (3D view of the map and all players, RTS style),
// the map's .bsp and its textures, the live game state as JSON and a set of
// admin actions (kick, ban, set health, scale, teleport, ...).
//
// It only starts when g_webPassword is set. Every request needs HTTP basic
// auth with that password (any user name).

#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	typedef SOCKET waSocket_t;
	#define WA_INVALID_SOCKET	INVALID_SOCKET
	#define WA_CloseSocket		closesocket
	#define WA_WouldBlock()		( WSAGetLastError() == WSAEWOULDBLOCK )
	#define WA_SEND_FLAGS		0
#else
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <unistd.h>
	#include <fcntl.h>
	#include <errno.h>
	typedef int waSocket_t;
	#define WA_INVALID_SOCKET	( -1 )
	#define WA_CloseSocket		close
	#define WA_WouldBlock()		( errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR )
	#ifdef MSG_NOSIGNAL
		#define WA_SEND_FLAGS	MSG_NOSIGNAL
	#else
		#define WA_SEND_FLAGS	0
	#endif
#endif

#include "g_local.h"
#include "webadmin_html.h"

#define WA_MAX_CONNECTIONS	32
#define WA_MAX_REQUEST		16384
#define WA_TIMEOUT_MS		15000
#define WA_MAX_FILE			( 64 * 1024 * 1024 )

#define WA_BOOL( x )		( ( x ) ? "true" : "false" )

extern void G_Kill( gentity_t *ent );
extern stringID_table_t animTable[MAX_ANIMATIONS + 1];

typedef struct waBuffer_s {
	char	*data;
	int		len;
	int		size;
} waBuffer_t;

typedef struct waConnection_s {
	qboolean	inUse;
	waSocket_t	sock;
	char		request[WA_MAX_REQUEST + 1];
	int			requestLen;
	waBuffer_t	response;
	int			sent;
	int			lastActive;		// trap->Milliseconds()
} waConnection_t;

static waSocket_t		listenSocket = WA_INVALID_SOCKET;
static waConnection_t	connections[WA_MAX_CONNECTIONS];
static int				listenPort;

// key/value pairs each entity was spawned with, "key\0value\0key\0value\0"
static char		*entityVars[MAX_GENTITIES];
static int		entityVarsLen[MAX_GENTITIES];
static int		lastSpawnedEntity = -1;

// the current map's .bsp, loaded on the first request for it
static char		*mapData;
static int		mapLen;
static char		mapDataName[MAX_QPATH];

// the newest chat lines, a ring. Line number n is in chatLines[(n - 1) % WA_MAX_CHAT].
#define WA_MAX_CHAT		128

typedef struct waChatLine_s {
	int		seq;
	char	time[12];				// server clock, HH:MM:SS
	int		clientNum;				// -1: server console or the panel
	char	name[MAX_NETNAME];
	char	team[12];
	char	to[MAX_NETNAME];		// who a tell went to
	char	mode[8];				// all, team, tell, clan, admin, server, panel
	char	text[MAX_SAY_TEXT];
} waChatLine_t;

static waChatLine_t	chatLines[WA_MAX_CHAT];
static int			chatSeq;		// number of the newest line, the first one is 1
static int			chatEpoch;		// start time of this map. The module is loaded again for each one, the numbers start over

// a gravity of his own for a player, set in the panel. Not in JKAdmin. Gone with the map like the chat lines.
#define WA_MIN_GRAVITY	-2000
#define WA_MAX_GRAVITY	5000

static qboolean	clientGravitySet[MAX_CLIENTS];
static float	clientGravity[MAX_CLIENTS];

static const char *WA_TeamName( team_t team );

/*
==============================================================================

Buffers

==============================================================================
*/

static void WA_BufReserve( waBuffer_t *b, int extra ) {
	if ( b->len + extra + 1 <= b->size )
		return;

	int newSize = b->size ? b->size : 4096;
	while ( newSize < b->len + extra + 1 )
		newSize *= 2;
	b->data = (char *)realloc( b->data, newSize );
	b->size = newSize;
}

static void WA_BufAppend( waBuffer_t *b, const void *data, int len ) {
	WA_BufReserve( b, len );
	memcpy( b->data + b->len, data, len );
	b->len += len;
	b->data[b->len] = '\0';
}

static void WA_BufPrintf( waBuffer_t *b, const char *fmt, ... ) {
	va_list ap;
	char	small[1024];
	int		len;

	va_start( ap, fmt );
	len = Q_vsnprintf( small, sizeof( small ), fmt, ap );
	va_end( ap );

	if ( len < (int)sizeof( small ) ) {
		WA_BufAppend( b, small, len );
		return;
	}

	WA_BufReserve( b, len );
	va_start( ap, fmt );
	Q_vsnprintf( b->data + b->len, len + 1, fmt, ap );
	va_end( ap );
	b->len += len;
}

static void WA_BufFree( waBuffer_t *b ) {
	free( b->data );
	memset( b, 0, sizeof( *b ) );
}

// JSON string, color codes stay in, the page renders them
static void WA_BufJsonString( waBuffer_t *b, const char *s ) {
	WA_BufAppend( b, "\"", 1 );
	for ( ; *s; s++ ) {
		const unsigned char c = (unsigned char)*s;
		if ( c == '"' || c == '\\' ) {
			char esc[2] = { '\\', (char)c };
			WA_BufAppend( b, esc, 2 );
		} else if ( c < 0x20 || c >= 0x7f ) {
			// names are not UTF-8, keep them ASCII safe
			WA_BufPrintf( b, "\\u%04x", c );
		} else {
			WA_BufAppend( b, s, 1 );
		}
	}
	WA_BufAppend( b, "\"", 1 );
}

/*
==============================================================================

Responses

==============================================================================
*/

static void WA_Respond( waConnection_t *c, int status, const char *statusText, const char *contentType,
	const char *extraHeaders, const void *body, int bodyLen ) {
	WA_BufPrintf( &c->response,
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n"
		"X-Content-Type-Options: nosniff\r\n"
		"%s"
		"\r\n",
		status, statusText, contentType, bodyLen, extraHeaders ? extraHeaders : "" );
	if ( bodyLen > 0 )
		WA_BufAppend( &c->response, body, bodyLen );
}

static void WA_RespondText( waConnection_t *c, int status, const char *statusText, const char *text ) {
	WA_Respond( c, status, statusText, "text/plain; charset=utf-8", NULL, text, (int)strlen( text ) );
}

static void WA_RespondJson( waConnection_t *c, waBuffer_t *json ) {
	WA_Respond( c, 200, "OK", "application/json", "Cache-Control: no-store\r\n", json->data, json->len );
}

/*
==============================================================================

Request helpers

==============================================================================
*/

static int WA_Base64Value( char c ) {
	if ( c >= 'A' && c <= 'Z' ) return c - 'A';
	if ( c >= 'a' && c <= 'z' ) return c - 'a' + 26;
	if ( c >= '0' && c <= '9' ) return c - '0' + 52;
	if ( c == '+' ) return 62;
	if ( c == '/' ) return 63;
	return -1;
}

static int WA_Base64Decode( const char *in, char *out, int outSize ) {
	int bits = 0, value = 0, len = 0;

	for ( ; *in && *in != '\r' && *in != '\n' && *in != '='; in++ ) {
		const int v = WA_Base64Value( *in );
		if ( v < 0 )
			return -1;
		value = ( value << 6 ) | v;
		bits += 6;
		if ( bits >= 8 ) {
			bits -= 8;
			if ( len >= outSize - 1 )
				return -1;
			out[len++] = (char)( ( value >> bits ) & 0xFF );
		}
	}
	out[len] = '\0';
	return len;
}

// Finds a header value in the request head, case insensitive name. Returns a
// pointer to the start of the value (ends at \r) or NULL.
static const char *WA_FindHeader( const char *head, const char *name ) {
	const size_t nameLen = strlen( name );
	const char *line = strstr( head, "\r\n" );

	while ( line && line[2] != '\r' ) {
		line += 2;
		if ( !Q_stricmpn( line, name, (int)nameLen ) && line[nameLen] == ':' ) {
			const char *v = line + nameLen + 1;
			while ( *v == ' ' )
				v++;
			return v;
		}
		line = strstr( line, "\r\n" );
	}
	return NULL;
}

static qboolean WA_CheckAuth( const char *head ) {
	const char *auth = WA_FindHeader( head, "Authorization" );
	char		decoded[256];
	const char	*pass;

	if ( !auth || Q_stricmpn( auth, "Basic ", 6 ) )
		return qfalse;
	if ( WA_Base64Decode( auth + 6, decoded, sizeof( decoded ) ) < 0 )
		return qfalse;

	pass = strchr( decoded, ':' );
	return (qboolean)( pass && g_webPassword.string[0] && !strcmp( pass + 1, g_webPassword.string ) );
}

static void WA_UrlDecode( char *s ) {
	char *out = s;

	for ( ; *s; s++ ) {
		if ( *s == '+' ) {
			*out++ = ' ';
		} else if ( *s == '%' && s[1] && s[2] ) {
			char hex[3] = { s[1], s[2], 0 };
			*out++ = (char)strtol( hex, NULL, 16 );
			s += 2;
		} else {
			*out++ = *s;
		}
	}
	*out = '\0';
}

// Looks up key in an "a=1&b=2" string. Returns qfalse if missing.
static qboolean WA_FormValue( const char *form, const char *key, char *out, int outSize ) {
	const size_t keyLen = strlen( key );
	const char *p = form;

	while ( p && *p ) {
		if ( !strncmp( p, key, keyLen ) && p[keyLen] == '=' ) {
			const char *v = p + keyLen + 1;
			const char *end = strchr( v, '&' );
			int len = end ? (int)( end - v ) : (int)strlen( v );
			if ( len >= outSize )
				len = outSize - 1;
			memcpy( out, v, len );
			out[len] = '\0';
			WA_UrlDecode( out );
			return qtrue;
		}
		p = strchr( p, '&' );
		if ( p )
			p++;
	}
	out[0] = '\0';
	return qfalse;
}

// The page sends UTF-8, the game has one byte per character (Latin-1). What the game can not show becomes '?'.
static void WA_Utf8ToLatin1( char *s ) {
	const unsigned char	*in = (const unsigned char *)s;
	char				*out = s;

	while ( *in ) {
		if ( *in < 0x80 ) {
			*out++ = (char)*in++;
		} else if ( ( *in & 0xE0 ) == 0xC0 && ( in[1] & 0xC0 ) == 0x80 ) {
			const int cp = ( ( *in & 0x1F ) << 6 ) | ( in[1] & 0x3F );
			*out++ = ( cp >= 0xA0 && cp <= 0xFF ) ? (char)cp : '?';
			in += 2;
		} else {
			// a longer sequence or a stray byte
			*out++ = '?';
			in++;
			while ( ( *in & 0xC0 ) == 0x80 )
				in++;
		}
	}
	*out = '\0';
}

static char *WA_ReadGameFile( const char *path, int *outLen ) {
	fileHandle_t	f;
	const int		len = trap->FS_Open( path, &f, FS_READ );
	char			*data;

	if ( len <= 0 || !f ) {
		if ( f )
			trap->FS_Close( f );
		return NULL;
	}
	if ( len > WA_MAX_FILE ) {
		trap->FS_Close( f );
		return NULL;
	}

	data = (char *)malloc( len );
	trap->FS_Read( data, len, f );
	trap->FS_Close( f );
	*outLen = len;
	return data;
}

/*
==============================================================================

API

==============================================================================
*/

static const char *WA_TeamName( team_t team ) {
	switch ( team ) {
	case TEAM_RED: return "red";
	case TEAM_BLUE: return "blue";
	case TEAM_SPECTATOR: return "spectator";
	default: return "free";
	}
}

static void WA_AppendVec( waBuffer_t *b, const vec3_t v ) {
	WA_BufPrintf( b, "[%.1f,%.1f,%.1f]", v[0], v[1], v[2] );
}

static void WA_ApiState( waConnection_t *c ) {
	waBuffer_t	b = { 0 };
	char		mapname[MAX_QPATH], hostname[MAX_NETNAME * 2];
	int			i;
	qboolean	first = qtrue;

	trap->Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
	trap->Cvar_VariableStringBuffer( "sv_hostname", hostname, sizeof( hostname ) );

	WA_BufAppend( &b, "{\"map\":", 7 );
	WA_BufJsonString( &b, mapname );
	WA_BufAppend( &b, ",\"host\":", 8 );
	WA_BufJsonString( &b, hostname );
	WA_BufPrintf( &b, ",\"gravity\":%d", (int)g_gravity.value );		// not in JKAdmin
	WA_BufPrintf( &b, ",\"gametype\":%d,\"gametypeName\":\"%s\",\"time\":%d,\"timelimit\":%d,\"fraglimit\":%d,\"capturelimit\":%d,"
		"\"scores\":{\"red\":%d,\"blue\":%d},\"maxClients\":%d,\"players\":[",
		level.gametype, BG_GetGametypeString( level.gametype ), level.time - level.startTime, timelimit.integer,
		fraglimit.integer, capturelimit.integer, level.teamScores[TEAM_RED], level.teamScores[TEAM_BLUE], level.maxclients );

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t		*ent = &g_entities[i];
		gclient_t		*cl = ent->client;
		playerState_t	*ps;

		if ( !ent->inuse || !cl || cl->pers.connected == CON_DISCONNECTED )
			continue;
		ps = &cl->ps;

		if ( !first )
			WA_BufAppend( &b, ",", 1 );
		first = qfalse;

		WA_BufPrintf( &b, "{\"id\":%d,\"name\":", i );
		WA_BufJsonString( &b, cl->pers.netname );
		WA_BufPrintf( &b, ",\"team\":\"%s\",\"bot\":%s,\"connected\":%s,\"health\":%d,\"maxHealth\":%d,\"armor\":%d,"
			"\"score\":%d,\"ping\":%d,\"weapon\":%d,\"forcePower\":%d,\"scale\":%d,\"god\":%s,\"dead\":%s,\"saberOn\":%s,\"pos\":",
			WA_TeamName( cl->sess.sessionTeam ), ( ent->r.svFlags & SVF_BOT ) ? "true" : "false",
			cl->pers.connected == CON_CONNECTED ? "true" : "false",
			ps->stats[STAT_HEALTH], ps->stats[STAT_MAX_HEALTH], ps->stats[STAT_ARMOR],
			ps->persistant[PERS_SCORE], ps->ping, ps->weapon, ps->fd.forcePower,
			ps->iModelScale ? ps->iModelScale : 100, ( ent->flags & FL_GODMODE ) ? "true" : "false",
			( ps->pm_type == PM_DEAD || ps->stats[STAT_HEALTH] <= 0 ) ? "true" : "false",
			( ps->weapon == WP_SABER && !ps->saberHolstered ) ? "true" : "false" );
		WA_AppendVec( &b, ps->origin );
		WA_BufAppend( &b, ",\"vel\":", 7 );
		WA_AppendVec( &b, ps->velocity );
		WA_BufPrintf( &b, ",\"noclip\":%s", cl->noclip ? "true" : "false" );		// not in JKAdmin
		WA_BufPrintf( &b, ",\"gravity\":%d,\"ownGravity\":%s", ps->gravity, WA_BOOL( clientGravitySet[i] ) );	// not in JKAdmin
		WA_BufPrintf( &b, ",\"schizo\":%s", WA_BOOL( G_SchizoActive( i ) ) );		// not in JKAdmin, g_schizo.c
		{
			// not in JKAdmin: what the JA+ buttons of the page show. admin is jpAdminLevel_t, altDim 2 is forced.
			const jpClient_t *jp = &cl->pers.jp;

			WA_BufPrintf( &b, ",\"jp\":{\"admin\":%d,\"sleep\":%s,\"protect\":%s,\"empower\":%s,\"merc\":%s,\"mindtrick\":%s,"
				"\"ghost\":%s,\"silence\":%s,\"altDim\":%d,\"denyVote\":%s,\"frozen\":%s}",
				(int)cl->sess.jpAdminLevel, WA_BOOL( jp->sleeping ), WA_BOOL( jp->protectedByAdmin ), WA_BOOL( jp->empowered ),
				WA_BOOL( jp->merc ), WA_BOOL( jp->mindtricked ), WA_BOOL( jp->isGhost ), WA_BOOL( jp->silenced ), jp->dimension,
				WA_BOOL( jp->denyVote ), WA_BOOL( jp->frozen ) );
		}
		WA_BufPrintf( &b, ",\"yaw\":%.1f,\"pitch\":%.1f,\"legsAnim\":%d,\"torsoAnim\":%d,\"viewheight\":%d,"
			"\"saberHolstered\":%d,\"saberInFlight\":%s,",
			ps->viewangles[YAW], ps->viewangles[PITCH], ps->legsAnim, ps->torsoAnim, ps->viewheight,
			ps->saberHolstered, ps->saberInFlight ? "true" : "false" );
		if ( ps->weapon > WP_NONE && ps->weapon < WP_NUM_WEAPONS ) {
			const int ammoIndex = weaponData[ps->weapon].ammoIndex;
			WA_BufPrintf( &b, "\"ammo\":%d,\"maxAmmo\":%d,\"ammoInfinite\":%s,", ammoIndex > AMMO_NONE && ammoIndex < AMMO_MAX ? ps->ammo[ammoIndex] : -1,
				ammoIndex > AMMO_NONE && ammoIndex < AMMO_MAX ? ammoData[ammoIndex].max : 0,
				( !weaponData[ps->weapon].energyPerShot && !weaponData[ps->weapon].altEnergyPerShot ) ? "true" : "false" );
		}
		WA_BufPrintf( &b, "\"saberStyle\":%d,\"forceMax\":%d,\"sabers\":[", ps->fd.saberDrawAnimLevel, ps->fd.forcePowerMax ? ps->fd.forcePowerMax : 100 );
		{
			int s, bl;
			qboolean firstSaber = qtrue;
			for ( s = 0; s < MAX_SABERS; s++ ) {
				const saberInfo_t *saber = &cl->saber[s];
				if ( !saber->model[0] )
					continue;
				if ( !firstSaber )
					WA_BufAppend( &b, ",", 1 );
				firstSaber = qfalse;
				WA_BufAppend( &b, "{\"model\":", 9 );
				WA_BufJsonString( &b, saber->model );
				WA_BufAppend( &b, ",\"blades\":[", 11 );
				for ( bl = 0; bl < saber->numBlades && bl < MAX_BLADES; bl++ ) {
					const bladeInfo_t *blade = &saber->blade[bl];
					WA_BufPrintf( &b, "%s{\"length\":%.1f,\"current\":%.1f,\"radius\":%.1f,\"color\":%d,"
						"\"org\":[%.1f,%.1f,%.1f],\"dir\":[%.3f,%.3f,%.3f]}", bl ? "," : "",
						blade->lengthMax, blade->length, blade->radius, blade->color,
						blade->muzzlePoint[0], blade->muzzlePoint[1], blade->muzzlePoint[2],
						blade->muzzleDir[0], blade->muzzleDir[1], blade->muzzleDir[2] );
				}
				WA_BufAppend( &b, "]}", 2 );
			}
		}
		WA_BufAppend( &b, "],\"model\":", 10 );
		{
			char userinfo[MAX_INFO_STRING];
			trap->GetUserinfo( i, userinfo, sizeof( userinfo ) );
			WA_BufJsonString( &b, Info_ValueForKey( userinfo, "model" ) );
		}
		WA_BufAppend( &b, "}", 1 );
	}

	// NPCs, drawn with their models too
	WA_BufAppend( &b, "],\"npcs\":[", 10 );
	first = qtrue;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *ent = &g_entities[i];
		char model[MAX_QPATH];

		if ( !ent->inuse || ent->s.eType != ET_NPC || !ent->client )
			continue;

		if ( !first )
			WA_BufAppend( &b, ",", 1 );
		first = qfalse;

		trap->GetConfigstring( CS_MODELS + ent->s.modelindex, model, sizeof( model ) );
		WA_BufPrintf( &b, "{\"id\":%d,\"name\":", i );
		WA_BufJsonString( &b, ent->NPC_type ? ent->NPC_type : "NPC" );
		WA_BufAppend( &b, ",\"model\":", 9 );
		WA_BufJsonString( &b, model );
		WA_BufPrintf( &b, ",\"health\":%d,\"dead\":%s,\"scale\":%d,\"yaw\":%.1f,\"legsAnim\":%d,\"torsoAnim\":%d,\"pos\":",
			ent->health, ( ent->health <= 0 ) ? "true" : "false", ent->client->ps.iModelScale ? ent->client->ps.iModelScale : 100,
			ent->client->ps.viewangles[YAW], ent->client->ps.legsAnim, ent->client->ps.torsoAnim );
		WA_AppendVec( &b, ent->client->ps.origin );
		WA_BufAppend( &b, "}", 1 );
	}

	// items, flags and other pickups, so the map looks alive
	WA_BufAppend( &b, "],\"items\":[", 11 );
	first = qtrue;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *ent = &g_entities[i];

		if ( !ent->inuse || ent->s.eType != ET_ITEM || !ent->item || ( ent->s.eFlags & EF_NODRAW ) || ( ent->r.svFlags & SVF_NOCLIENT ) )
			continue;

		if ( !first )
			WA_BufAppend( &b, ",", 1 );
		first = qfalse;

		WA_BufPrintf( &b, "{\"id\":%d,\"type\":%d,\"name\":", i, ent->item->giType );
		WA_BufJsonString( &b, ent->item->classname );
		WA_BufAppend( &b, ",\"pos\":", 7 );
		WA_AppendVec( &b, ent->r.currentOrigin );
		WA_BufAppend( &b, "}", 1 );
	}
	WA_BufAppend( &b, "]}", 2 );

	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

static void WA_ApiMap( waConnection_t *c ) {
	char mapname[MAX_QPATH];

	trap->Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );

	if ( !mapData || Q_stricmp( mapDataName, mapname ) ) {
		free( mapData );
		mapData = WA_ReadGameFile( va( "maps/%s.bsp", mapname ), &mapLen );
		Q_strncpyz( mapDataName, mapname, sizeof( mapDataName ) );
	}

	if ( !mapData ) {
		WA_RespondText( c, 404, "Not Found", "map not found" );
		return;
	}
	WA_Respond( c, 200, "OK", "application/octet-stream", "Cache-Control: no-store\r\n", mapData, mapLen );
}

// Textures, models and skins for the 3D views. Only below textures/, models/ and gfx/.
static void WA_ApiFile( waConnection_t *c, const char *query ) {
	char		name[MAX_QPATH];
	const char	*ext, *type;
	char		*data;
	int			len;

	if ( !WA_FormValue( query, "name", name, sizeof( name ) ) || strstr( name, ".." ) || strchr( name, '\\' ) || strchr( name, ':' ) ||
		 ( Q_stricmpn( name, "textures/", 9 ) && Q_stricmpn( name, "models/", 7 ) && Q_stricmpn( name, "gfx/", 4 ) ) ) {
		WA_RespondText( c, 400, "Bad Request", "bad file name" );
		return;
	}

	ext = COM_GetExtension( name );
	if ( !Q_stricmp( ext, "jpg" ) ) type = "image/jpeg";
	else if ( !Q_stricmp( ext, "gla" ) || !Q_stricmp( ext, "cfg" ) ) type = "application/octet-stream";
	else if ( !Q_stricmp( ext, "png" ) ) type = "image/png";
	else if ( !Q_stricmp( ext, "tga" ) || !Q_stricmp( ext, "md3" ) || !Q_stricmp( ext, "glm" ) ) type = "application/octet-stream";
	else if ( !Q_stricmp( ext, "skin" ) ) type = "text/plain; charset=utf-8";
	else {
		WA_RespondText( c, 400, "Bad Request", "only images, md3, glm and skin files" );
		return;
	}

	data = WA_ReadGameFile( name, &len );
	if ( !data ) {
		WA_RespondText( c, 404, "Not Found", "not found" );
		return;
	}
	WA_Respond( c, 200, "OK", type, "Cache-Control: max-age=3600\r\n", data, len );
	free( data );
}

static void WA_ActionResult( waConnection_t *c, qboolean ok, const char *msg ) {
	waBuffer_t b = { 0 };

	WA_BufPrintf( &b, "{\"ok\":%s,\"message\":", ok ? "true" : "false" );
	WA_BufJsonString( &b, msg );
	WA_BufAppend( &b, "}", 1 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

/*
=================
WA_JpAction

Not in JKAdmin: the JA+ admin commands that work on a player. They run as typed on the server console
(<command> <player number> [text]), so the rules, messages and logs are those of JA+. The command runs in the
next frame, the answer only says that it was sent. Returns qfalse if the action is none of them.
=================
*/
static qboolean WA_JpAction( waConnection_t *c, const char *action, int id, char *value ) {
	static const struct {
		const char	*name;
		qboolean	text;		// the value goes along as text
	} cmds[] = {
		{ "amsleep", qfalse },			{ "amwake", qfalse },			{ "amprotect", qfalse },
		{ "amempower", qfalse },		{ "ammerc", qfalse },			{ "ammindtrick", qfalse },
		{ "amghost", qfalse },			{ "amsilence", qfalse },		{ "amunsilence", qfalse },
		{ "amslap", qfalse },			{ "amForceAltDim", qfalse },	{ "amUnForceAltDim", qfalse },
		{ "amdenyvote", qfalse },		{ "amShowMotd", qfalse },		{ "amrename", qtrue },
		{ "ampsay", qtrue },
	};
	gclient_t	*client = g_entities[id].client;
	size_t		i;

	if ( !strcmp( action, "freeze" ) ) {
		// in JA+ a client command of the rcon admins, there is no console command for it
		client->pers.jp.frozen = (qboolean)!client->pers.jp.frozen;
		WA_ActionResult( c, qtrue, client->pers.jp.frozen ? "frozen" : "not frozen any more" );
		return qtrue;
	}

	if ( !strcmp( action, "admgrant" ) ) {
		if ( strcmp( value, "council" ) && strcmp( value, "knight" ) && strcmp( value, "instructor" ) && strcmp( value, "no" ) ) {
			WA_ActionResult( c, qfalse, "rank must be council, knight, instructor or no" );
			return qtrue;
		}
		trap->SendConsoleCommand( EXEC_APPEND, va( "admGrant %d %s\n", id, value ) );
		WA_ActionResult( c, qtrue, va( "admGrant %s sent", value ) );
		return qtrue;
	}

	for ( i = 0; i < ARRAY_LEN( cmds ); i++ ) {
		if ( strcmp( action, cmds[i].name ) )
			continue;

		if ( cmds[i].text ) {
			WA_Utf8ToLatin1( value );
			Q_strstrip( value, "\n\r\";", NULL );
			if ( !value[0] ) {
				WA_ActionResult( c, qfalse, "no text" );
				return qtrue;
			}
			trap->SendConsoleCommand( EXEC_APPEND, va( "%s %d \"%s\"\n", cmds[i].name, id, value ) );
		} else {
			trap->SendConsoleCommand( EXEC_APPEND, va( "%s %d\n", cmds[i].name, id ) );
		}
		WA_ActionResult( c, qtrue, va( "%s sent", cmds[i].name ) );
		return qtrue;
	}
	return qfalse;
}

static void WA_ApiAction( waConnection_t *c, const char *body ) {
	char		action[32], idStr[16], value[256], xs[32], ys[32], zs[32];
	gentity_t	*ent = NULL;
	int			id;

	WA_FormValue( body, "action", action, sizeof( action ) );
	WA_FormValue( body, "value", value, sizeof( value ) );

	// actions on the server
	if ( !strcmp( action, "say" ) ) {
		WA_Utf8ToLatin1( value );
		Q_strstrip( value, "\n\r\";", NULL );
		trap->SendServerCommand( -1, va( "chat \"" S_COLOR_RED "Admin" S_COLOR_WHITE ": %s\"", value ) );
		WebAdmin_Chat( "panel", NULL, NULL, value );
		WA_ActionResult( c, qtrue, "message sent" );
		return;
	}
	if ( !strcmp( action, "addbot" ) ) {
		Q_strstrip( value, "\n\r\";", NULL );
		trap->SendConsoleCommand( EXEC_APPEND, va( "addbot \"%s\" 3\n", value[0] ? value : "Kyle Katarn" ) );	// JKAdmin says "Kyle", no bot has that name
		WA_ActionResult( c, qtrue, "bot added" );
		return;
	}
	if ( !strcmp( action, "map" ) ) {
		Q_strstrip( value, "\n\r\";", NULL );
		if ( !value[0] || strstr( value, ".." ) ) {
			WA_ActionResult( c, qfalse, "no map name" );
			return;
		}
		trap->SendConsoleCommand( EXEC_APPEND, va( "map \"%s\"\n", value ) );
		WA_ActionResult( c, qtrue, "changing map" );
		return;
	}

	if ( !strcmp( action, "gravity" ) ) {
		// not in JKAdmin: g_gravity for everybody. The map sets it again at its next start (worldspawn key "gravity").
		if ( !value[0] ) {
			WA_ActionResult( c, qfalse, "no gravity value" );
			return;
		}
		{
			const int v = Com_Clampi( WA_MIN_GRAVITY, WA_MAX_GRAVITY, atoi( value ) );

			trap->Cvar_Set( "g_gravity", va( "%d", v ) );
			trap->Cvar_Update( &g_gravity );
			WA_ActionResult( c, qtrue, va( "gravity %d", v ) );
		}
		return;
	}

	// actions on a player
	if ( !WA_FormValue( body, "id", idStr, sizeof( idStr ) ) ) {
		WA_ActionResult( c, qfalse, "no player id" );
		return;
	}
	id = atoi( idStr );
	if ( id < 0 || id >= level.maxclients || !g_entities[id].inuse || !g_entities[id].client ||
		 g_entities[id].client->pers.connected == CON_DISCONNECTED ) {
		WA_ActionResult( c, qfalse, "no such player" );
		return;
	}
	ent = &g_entities[id];

	if ( !strcmp( action, "kick" ) ) {
		trap->SendConsoleCommand( EXEC_APPEND, va( "clientkick %d\n", id ) );
		WA_ActionResult( c, qtrue, "kicked" );
	} else if ( !strcmp( action, "ban" ) ) {
		char userinfo[MAX_INFO_STRING], ip[64], *port;

		trap->GetUserinfo( id, userinfo, sizeof( userinfo ) );
		Q_strncpyz( ip, Info_ValueForKey( userinfo, "ip" ), sizeof( ip ) );
		port = strchr( ip, ':' );
		if ( port )
			*port = '\0';
		if ( !ip[0] || !Q_stricmp( ip, "localhost" ) || !Q_stricmp( ip, "bot" ) ) {
			WA_ActionResult( c, qfalse, "this player has no IP to ban" );
			return;
		}
		trap->SendConsoleCommand( EXEC_APPEND, va( "addip %s\nclientkick %d\n", ip, id ) );
		WA_ActionResult( c, qtrue, va( "banned %s", ip ) );
	} else if ( !strcmp( action, "health" ) ) {
		const int v = Com_Clampi( 1, 999, atoi( value ) );
		if ( ent->health <= 0 ) {
			WA_ActionResult( c, qfalse, "player is dead" );
			return;
		}
		ent->health = ent->client->ps.stats[STAT_HEALTH] = v;
		WA_ActionResult( c, qtrue, va( "health %d", v ) );
	} else if ( !strcmp( action, "armor" ) ) {
		const int v = Com_Clampi( 0, 999, atoi( value ) );
		ent->client->ps.stats[STAT_ARMOR] = v;
		WA_ActionResult( c, qtrue, va( "armor %d", v ) );
	} else if ( !strcmp( action, "scale" ) ) {
		const int v = Com_Clampi( 10, 500, atoi( value ) );
		char userinfo[MAX_INFO_STRING];

		// JA+ keeps the scale in pers.jp.modelScale and applies it again at every spawn
		trap->GetUserinfo( id, userinfo, sizeof( userinfo ) );
		JP_SetModelScale( ent, v / 100.0f, userinfo );
		WA_ActionResult( c, qtrue, va( "scale %d%%", v ) );
	} else if ( !strcmp( action, "kill" ) ) {
		const jpClient_t *jp = &ent->client->pers.jp;

		if ( ent->health <= 0 || ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
			WA_ActionResult( c, qfalse, "player is not alive" );
			return;
		}
		// JA+: the states the kill command refuses too
		if ( jp->sleeping || jp->down || jp->isGhost ) {
			WA_ActionResult( c, qfalse, "player is sleeping, knocked down or a ghost" );
			return;
		}
		ent->flags &= ~FL_GODMODE;
		G_Kill( ent );
		WA_ActionResult( c, qtrue, "killed" );
	} else if ( !strcmp( action, "god" ) ) {
		ent->flags ^= FL_GODMODE;
		// JA+: like the god cheat, so jp_allowGodTalk does not take it away with the chat bubble
		ent->client->pers.jp.cheatGod = (qboolean)( ( ent->flags & FL_GODMODE ) != 0 );
		WA_ActionResult( c, qtrue, ( ent->flags & FL_GODMODE ) ? "god mode on" : "god mode off" );
	} else if ( !strcmp( action, "noclip" ) ) {
		// not in JKAdmin: the noclip cheat for the selected player
		if ( ent->health <= 0 || ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
			WA_ActionResult( c, qfalse, "player is not alive" );
			return;
		}
		ent->client->noclip = (qboolean)!ent->client->noclip;
		WA_ActionResult( c, qtrue, ent->client->noclip ? "noclip on" : "noclip off" );
	} else if ( !strcmp( action, "team" ) ) {
		team_t		now;
		qboolean	ok;

		if ( strcmp( value, "red" ) && strcmp( value, "blue" ) && strcmp( value, "free" ) && strcmp( value, "spectator" ) ) {
			WA_ActionResult( c, qfalse, "team must be red, blue, free or spectator" );
			return;
		}
		// JA+: past the team locks and the team balance, like amforceteam
		JP_ForceTeam( ent, value );

		now = ent->client->sess.sessionTeam;
		if ( !strcmp( value, "spectator" ) )
			ok = (qboolean)( now == TEAM_SPECTATOR );
		else if ( level.gametype < GT_TEAM )
			ok = (qboolean)( now == TEAM_FREE );
		else if ( !strcmp( value, "free" ) )
			ok = (qboolean)( now == TEAM_RED || now == TEAM_BLUE );		// the game picks a team
		else
			ok = (qboolean)!strcmp( WA_TeamName( now ), value );
		// JA+: with g_teamForceBalance 2 or 3 the spawn in the bigger team can move the player straight back
		WA_ActionResult( c, ok, ok ? va( "team %s", WA_TeamName( now ) ) :
			( g_teamForceBalance.integer >= 2 && level.gametype >= GT_TEAM ) ?
			"the game did not keep the player there (g_teamForceBalance moves players to balance the teams)" :
			"the game refused the team change" );
	} else if ( !strcmp( action, "teleport" ) ) {
		vec3_t origin;

		if ( ent->health <= 0 || ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
			WA_ActionResult( c, qfalse, "player is not alive" );
			return;
		}
		// JA+: the target checks of rcon amtele
		if ( ent->client->ps.eFlags & EF_ALT_DIM ) {
			WA_ActionResult( c, qfalse, "player is in the alternate dimension" );
			return;
		}
		if ( BG_InGrappleMove( ent->client->ps.legsAnim ) || BG_InGrappleMove( ent->client->ps.torsoAnim ) ) {
			WA_ActionResult( c, qfalse, "player is in a grapple move" );
			return;
		}
		if ( ent->client->pers.jp.refuseTele ) {
			WA_ActionResult( c, qfalse, "this player refuses to be teleported" );
			return;
		}
		if ( !WA_FormValue( body, "x", xs, sizeof( xs ) ) || !WA_FormValue( body, "y", ys, sizeof( ys ) ) ||
			 !WA_FormValue( body, "z", zs, sizeof( zs ) ) ) {
			WA_ActionResult( c, qfalse, "no position" );
			return;
		}
		// the page sends the floor point, stand on it (JP_ManualTeleport lifts it by one unit)
		VectorSet( origin, atof( xs ), atof( ys ), atof( zs ) - ent->r.mins[2] );
		// JA+: frees the hook and honours jp_starTrekTeleport, the path of amtele
		JP_ManualTeleport( ent, origin, ent->client->ps.viewangles );
		WA_ActionResult( c, qtrue, jp_starTrekTeleport.integer == 1 ? "teleport started" : "teleported" );
	} else if ( !strcmp( action, "schizo" ) ) {
		// not in JKAdmin: hallucinations only this player gets, see g_schizo.c
		WA_ActionResult( c, qtrue, G_SchizoToggle( ent ) ? "schizophrenia on" : "schizophrenia off" );
	} else if ( !strcmp( action, "playergravity" ) ) {
		// not in JKAdmin: a gravity of his own. An empty value gives him the gravity of the server again.
		if ( !value[0] ) {
			clientGravitySet[id] = qfalse;
			WA_ActionResult( c, qtrue, "gravity of the server" );
		} else {
			const int v = Com_Clampi( WA_MIN_GRAVITY, WA_MAX_GRAVITY, atoi( value ) );

			clientGravity[id] = (float)v;
			clientGravitySet[id] = qtrue;
			WA_ActionResult( c, qtrue, va( "own gravity %d", v ) );
		}
	} else if ( !WA_JpAction( c, action, id, value ) ) {
		WA_ActionResult( c, qfalse, "unknown action" );
	}
}


/*
==============================================================================

Entity editor

==============================================================================
*/

void WebAdmin_EntitySpawned( int entityNum ) {
	int		i, len = 0;
	char	*p;

	if ( entityNum < 0 || entityNum >= MAX_GENTITIES )
		return;

	free( entityVars[entityNum] );
	for ( i = 0; i < level.numSpawnVars; i++ )
		len += (int)strlen( level.spawnVars[i][0] ) + (int)strlen( level.spawnVars[i][1] ) + 2;

	entityVars[entityNum] = p = (char *)malloc( len + 1 );
	entityVarsLen[entityNum] = len;
	for ( i = 0; i < level.numSpawnVars; i++ ) {
		const int k = (int)strlen( level.spawnVars[i][0] ) + 1, v = (int)strlen( level.spawnVars[i][1] ) + 1;
		memcpy( p, level.spawnVars[i][0], k );
		memcpy( p + k, level.spawnVars[i][1], v );
		p += k + v;
	}
	lastSpawnedEntity = entityNum;
}

void WebAdmin_EntityFreed( int entityNum ) {
	if ( entityNum < 0 || entityNum >= MAX_GENTITIES )
		return;
	free( entityVars[entityNum] );
	entityVars[entityNum] = NULL;
	entityVarsLen[entityNum] = 0;
}

// entities the editor shows: no players, temp entities or saber helpers
static qboolean WA_ListedEntity( const gentity_t *ent ) {
	return (qboolean)( ent->inuse && ent->s.number >= MAX_CLIENTS && ent->s.number < ENTITYNUM_WORLD && ent->classname &&
		ent->classname[0] && ent->s.eType < ET_EVENTS && !ent->isSaberEntity && Q_stricmp( ent->classname, "tempEntity" ) &&
		Q_stricmp( ent->classname, "bodyque" ) );
}

static void WA_EntityPosition( const gentity_t *ent, vec3_t out ) {
	if ( ent->r.bmodel ) {
		VectorAdd( ent->r.absmin, ent->r.absmax, out );
		VectorScale( out, 0.5f, out );
	} else {
		VectorCopy( ent->r.currentOrigin, out );
	}
}

static void WA_ApiEntities( waConnection_t *c ) {
	waBuffer_t	b = { 0 };
	qboolean	first = qtrue;
	int			i;

	WA_BufAppend( &b, "{\"entities\":[", 13 );
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t	*ent = &g_entities[i];
		vec3_t		pos;

		if ( !WA_ListedEntity( ent ) )
			continue;

		if ( !first )
			WA_BufAppend( &b, ",", 1 );
		first = qfalse;

		WA_EntityPosition( ent, pos );
		WA_BufPrintf( &b, "{\"id\":%d,\"classname\":", i );
		WA_BufJsonString( &b, ent->classname );
		WA_BufPrintf( &b, ",\"type\":%d,\"bmodel\":%s,\"linked\":%s,\"pos\":", ent->s.eType, ent->r.bmodel ? "true" : "false",
			ent->r.linked ? "true" : "false" );
		WA_AppendVec( &b, pos );

		if ( entityVars[i] ) {
			const char *p = entityVars[i], *end = entityVars[i] + entityVarsLen[i];
			qboolean firstKey = qtrue;

			WA_BufAppend( &b, ",\"keys\":[", 9 );
			while ( p < end ) {
				const char *value = p + strlen( p ) + 1;
				if ( !firstKey )
					WA_BufAppend( &b, ",", 1 );
				firstKey = qfalse;
				WA_BufAppend( &b, "[", 1 );
				WA_BufJsonString( &b, p );
				WA_BufAppend( &b, ",", 1 );
				WA_BufJsonString( &b, value );
				WA_BufAppend( &b, "]", 1 );
				p = value + strlen( value ) + 1;
			}
			WA_BufAppend( &b, "]", 1 );
		}
		WA_BufAppend( &b, "}", 1 );
	}
	WA_BufAppend( &b, "]}", 2 );

	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

// animation names in animNumber_t order, to look up the frames in animation.cfg
static void WA_ApiAnims( waConnection_t *c ) {
	waBuffer_t	b = { 0 };
	int			i;

	WA_BufAppend( &b, "{\"anims\":[", 10 );
	for ( i = 0; i < MAX_ANIMATIONS; i++ ) {
		if ( i )
			WA_BufAppend( &b, ",", 1 );
		WA_BufJsonString( &b, animTable[i].name ? animTable[i].name : "" );
	}
	WA_BufAppend( &b, "]}", 2 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

// everything that can be spawned: the spawn table and all items
static void WA_ApiClasses( waConnection_t *c ) {
	waBuffer_t	b = { 0 };
	int			i, n = G_NumSpawnClassnames();
	qboolean	first;

	WA_BufAppend( &b, "{\"classes\":[", 12 );
	for ( i = 0; i < n; i++ ) {
		if ( i )
			WA_BufAppend( &b, ",", 1 );
		WA_BufJsonString( &b, G_SpawnClassname( i ) );
	}
	for ( i = 1; i < bg_numItems; i++ ) {
		if ( !bg_itemlist[i].classname || !bg_itemlist[i].classname[0] )
			continue;
		WA_BufAppend( &b, ",", 1 );
		WA_BufJsonString( &b, bg_itemlist[i].classname );
	}

	// models and icons of the items, for the previews
	WA_BufAppend( &b, "],\"items\":[", 11 );
	for ( i = 1, first = qtrue; i < bg_numItems; i++ ) {
		const gitem_t *item = &bg_itemlist[i];

		if ( !item->classname || !item->classname[0] )
			continue;
		if ( !first )
			WA_BufAppend( &b, ",", 1 );
		first = qfalse;
		WA_BufAppend( &b, "{\"classname\":", 13 );
		WA_BufJsonString( &b, item->classname );
		WA_BufAppend( &b, ",\"model\":", 9 );
		WA_BufJsonString( &b, item->world_model[0] ? item->world_model[0] : "" );
		WA_BufAppend( &b, ",\"view\":", 8 );
		WA_BufJsonString( &b, item->view_model ? item->view_model : "" );
		WA_BufAppend( &b, ",\"icon\":", 8 );
		WA_BufJsonString( &b, item->icon ? item->icon : "" );
		WA_BufAppend( &b, ",\"description\":", 15 );
		WA_BufJsonString( &b, item->description ? item->description : "" );
		WA_BufPrintf( &b, ",\"type\":%d,\"tag\":%d,\"quantity\":%d}", item->giType, item->giTag, item->quantity );
	}
	WA_BufAppend( &b, "]}", 2 );

	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

// The current entities in .ent format, for keeping an edited layout.
static void WA_ApiExport( waConnection_t *c ) {
	waBuffer_t	b = { 0 };
	char		mapname[MAX_QPATH], headers[256];
	int			i;

	for ( i = MAX_CLIENTS; i <= ENTITYNUM_WORLD; i++ ) {
		const int num = i == MAX_CLIENTS ? ENTITYNUM_WORLD : ( i == ENTITYNUM_WORLD ? MAX_CLIENTS : i );	// worldspawn first
		const char *p, *end;

		if ( !entityVars[num] || ( num != ENTITYNUM_WORLD && !g_entities[num].inuse ) )
			continue;

		WA_BufAppend( &b, "{\n", 2 );
		for ( p = entityVars[num], end = p + entityVarsLen[num]; p < end; ) {
			const char *value = p + strlen( p ) + 1;
			WA_BufPrintf( &b, "\"%s\" \"%s\"\n", p, value );
			p = value + strlen( value ) + 1;
		}
		WA_BufAppend( &b, "}\n", 2 );
	}

	trap->Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
	Com_sprintf( headers, sizeof( headers ), "Content-Disposition: attachment; filename=\"%s.ent\"\r\nCache-Control: no-store\r\n", COM_SkipPath( mapname ) );
	WA_Respond( c, 200, "OK", "text/plain; charset=utf-8", headers, b.data ? b.data : "", b.len );
	WA_BufFree( &b );
}

// Spawns an entity from "key=value" lines through the normal spawn code.
// Returns the entity number, or -1 with a reason in err.
static int WA_SpawnFromText( const char *text, char *err, int errSize ) {
	char		line[MAX_SPAWN_VARS_CHARS];
	const char	*p = text;
	qboolean	hasClass = qfalse, wasSpawning;

	level.numSpawnVars = 0;
	level.numSpawnVarChars = 0;

	while ( *p ) {
		const char	*end = strchr( p, '\n' );
		const int	len = end ? (int)( end - p ) : (int)strlen( p );
		char		*key, *value, *eq;

		if ( len >= (int)sizeof( line ) ) {
			Q_strncpyz( err, "a line is too long", errSize );
			return -1;
		}
		memcpy( line, p, len );
		line[len] = '\0';
		p = end ? end + 1 : p + len;

		Q_strstrip( line, "\r\"", NULL );
		eq = strchr( line, '=' );
		if ( !eq )
			continue;
		*eq = '\0';
		key = line;
		value = eq + 1;
		while ( *key == ' ' || *key == '\t' )
			key++;
		while ( eq > key && ( eq[-1] == ' ' || eq[-1] == '\t' ) )
			*--eq = '\0';
		if ( !key[0] )
			continue;

		if ( level.numSpawnVars >= MAX_SPAWN_VARS ||
			 level.numSpawnVarChars + (int)strlen( key ) + (int)strlen( value ) + 2 >= MAX_SPAWN_VARS_CHARS ) {
			Q_strncpyz( err, "too many keys", errSize );
			return -1;
		}
		if ( !Q_stricmp( key, "classname" ) ) {
			if ( !value[0] || !Q_stricmp( value, "worldspawn" ) ) {
				Q_strncpyz( err, "this classname can not be spawned", errSize );
				return -1;
			}
			hasClass = qtrue;
		}

		level.spawnVars[level.numSpawnVars][0] = G_AddSpawnVarToken( key );
		level.spawnVars[level.numSpawnVars][1] = G_AddSpawnVarToken( value );
		level.numSpawnVars++;
	}

	if ( !hasClass ) {
		Q_strncpyz( err, "no classname", errSize );
		return -1;
	}

	wasSpawning = level.spawning;
	level.spawning = qtrue;
	lastSpawnedEntity = -1;
	G_SpawnGEntityFromSpawnVars( qfalse );
	level.spawning = wasSpawning;

	// new item types have to reach the clients
	SaveRegisteredItems();

	if ( lastSpawnedEntity < 0 || !g_entities[lastSpawnedEntity].inuse ) {
		Q_strncpyz( err, "the game did not spawn it (unknown classname, or not for this gametype)", errSize );
		return -1;
	}
	return lastSpawnedEntity;
}

static void WA_EntityResult( waConnection_t *c, qboolean ok, const char *msg, int id ) {
	waBuffer_t b = { 0 };

	WA_BufPrintf( &b, "{\"ok\":%s,\"id\":%d,\"message\":", ok ? "true" : "false", id );
	WA_BufJsonString( &b, msg );
	WA_BufAppend( &b, "}", 1 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

static void WA_ApiEntity( waConnection_t *c, const char *body ) {
	char		op[16], idStr[16], err[128];
	static char	kv[WA_MAX_REQUEST];
	int			id = -1, newId;

	WA_FormValue( body, "op", op, sizeof( op ) );
	WA_FormValue( body, "kv", kv, sizeof( kv ) );
	if ( WA_FormValue( body, "id", idStr, sizeof( idStr ) ) )
		id = atoi( idStr );

	if ( !strcmp( op, "spawn" ) ) {
		newId = WA_SpawnFromText( kv, err, sizeof( err ) );
		WA_EntityResult( c, newId >= 0, newId >= 0 ? "spawned" : err, newId );
		return;
	}

	if ( id < 0 || id >= MAX_GENTITIES || !WA_ListedEntity( &g_entities[id] ) ) {
		WA_EntityResult( c, qfalse, "no such entity", id );
		return;
	}

	if ( !strcmp( op, "delete" ) ) {
		G_FreeEntity( &g_entities[id] );
		WA_EntityResult( c, qtrue, "deleted", id );
	} else if ( !strcmp( op, "update" ) ) {
		if ( !entityVars[id] ) {
			WA_EntityResult( c, qfalse, "this entity was made by the game code, it has no keys to edit", id );
			return;
		}
		// spawn the new one first, so a bad edit keeps the old entity
		newId = WA_SpawnFromText( kv, err, sizeof( err ) );
		if ( newId < 0 ) {
			WA_EntityResult( c, qfalse, err, id );
			return;
		}
		G_FreeEntity( &g_entities[id] );
		WA_EntityResult( c, qtrue, "updated", newId );
	} else {
		WA_EntityResult( c, qfalse, "unknown op", id );
	}
}

/*
==============================================================================

Chat

Not in JKAdmin. The game hands every chat line that went out to WebAdmin_Chat,
the page asks for the lines it does not have yet.

==============================================================================
*/

void WebAdmin_Chat( const char *mode, const gentity_t *from, const gentity_t *to, const char *text ) {
	waChatLine_t	*line;
	qtime_t			now;

	if ( listenSocket == WA_INVALID_SOCKET )
		return;

	line = &chatLines[chatSeq % WA_MAX_CHAT];
	memset( line, 0, sizeof( *line ) );
	line->seq = ++chatSeq;

	trap->RealTime( &now );
	Com_sprintf( line->time, sizeof( line->time ), "%02i:%02i:%02i", now.tm_hour, now.tm_min, now.tm_sec );

	Q_strncpyz( line->mode, mode, sizeof( line->mode ) );
	Q_strncpyz( line->text, text, sizeof( line->text ) );
	if ( from && from->client ) {
		line->clientNum = (int)( from-g_entities );
		Q_strncpyz( line->name, from->client->pers.netname, sizeof( line->name ) );
		Q_strncpyz( line->team, WA_TeamName( from->client->sess.sessionTeam ), sizeof( line->team ) );
	} else {
		line->clientNum = -1;
		Q_strncpyz( line->name, !strcmp( mode, "panel" ) ? S_COLOR_RED "Admin" : "server", sizeof( line->name ) );
	}
	if ( to && to->client )
		Q_strncpyz( line->to, to->client->pers.netname, sizeof( line->to ) );
}

// the lines after ?since=<number of the newest line the page has>&epoch=<the epoch it got them with>
static void WA_ApiChat( waConnection_t *c, const char *query ) {
	waBuffer_t	b = { 0 };
	char		arg[16];
	int			since = 0, seq;

	if ( WA_FormValue( query, "since", arg, sizeof( arg ) ) )
		since = atoi( arg );
	// the numbers started again since the page asked last, it gets everything
	if ( !WA_FormValue( query, "epoch", arg, sizeof( arg ) ) || atoi( arg ) != chatEpoch || since < 0 || since > chatSeq )
		since = 0;
	if ( since < chatSeq - WA_MAX_CHAT )
		since = chatSeq - WA_MAX_CHAT;

	WA_BufPrintf( &b, "{\"epoch\":%d,\"seq\":%d,\"messages\":[", chatEpoch, chatSeq );
	for ( seq = since + 1; seq <= chatSeq; seq++ ) {
		const waChatLine_t *line = &chatLines[( seq - 1 ) % WA_MAX_CHAT];

		WA_BufPrintf( &b, "%s{\"seq\":%d,\"time\":\"%s\",\"id\":%d,\"mode\":\"%s\",\"team\":\"%s\",\"name\":",
			seq > since + 1 ? "," : "", line->seq, line->time, line->clientNum, line->mode, line->team );
		WA_BufJsonString( &b, line->name );
		WA_BufAppend( &b, ",\"to\":", 6 );
		WA_BufJsonString( &b, line->to );
		WA_BufAppend( &b, ",\"text\":", 8 );
		WA_BufJsonString( &b, line->text );
		WA_BufAppend( &b, "}", 1 );
	}
	WA_BufAppend( &b, "]}", 2 );

	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

/*
==============================================================================

Connections

==============================================================================
*/

static void WA_CloseConnection( waConnection_t *c ) {
	if ( c->sock != WA_INVALID_SOCKET )
		WA_CloseSocket( c->sock );
	WA_BufFree( &c->response );
	memset( c, 0, sizeof( *c ) );
	c->sock = WA_INVALID_SOCKET;
}

// Handles a complete request. The response goes into c->response.
static void WA_HandleRequest( waConnection_t *c, char *head, const char *body ) {
	char	method[8], path[512];
	char	*query;

	if ( sscanf( head, "%7s %511s", method, path ) != 2 ) {
		WA_RespondText( c, 400, "Bad Request", "bad request" );
		return;
	}

	if ( !WA_CheckAuth( head ) ) {
		WA_Respond( c, 401, "Unauthorized", "text/plain", "WWW-Authenticate: Basic realm=\"OpenJK web admin\"\r\n",
			"login required", 14 );
		return;
	}

	query = strchr( path, '?' );
	if ( query )
		*query++ = '\0';
	else
		query = (char *)"";

	if ( !strcmp( method, "GET" ) && ( !strcmp( path, "/" ) || !strcmp( path, "/index.html" ) ) ) {
		// g_webRoot serves the page from disk instead, for working on it without a restart
		FILE *f = g_webRoot.string[0] ? fopen( va( "%s/index.html", g_webRoot.string ), "rb" ) : NULL;
		if ( f ) {
			waBuffer_t page = { 0 };
			char chunk[8192];
			size_t n;
			while ( ( n = fread( chunk, 1, sizeof( chunk ), f ) ) > 0 )
				WA_BufAppend( &page, chunk, (int)n );
			fclose( f );
			WA_Respond( c, 200, "OK", "text/html; charset=utf-8", "Cache-Control: no-store\r\n", page.data, page.len );
			WA_BufFree( &page );
		} else {
			WA_Respond( c, 200, "OK", "text/html; charset=utf-8", "Cache-Control: no-store\r\n", webadmin_html, (int)webadmin_html_len );
		}
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/anims" ) ) {
		WA_ApiAnims( c );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/state" ) ) {
		WA_ApiState( c );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/map" ) ) {
		WA_ApiMap( c );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/file" ) ) {
		WA_ApiFile( c, query );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/action" ) ) {
		WA_ApiAction( c, body );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/entities" ) ) {
		WA_ApiEntities( c );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/classes" ) ) {
		WA_ApiClasses( c );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/export" ) ) {
		WA_ApiExport( c );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/chat" ) ) {
		WA_ApiChat( c, query );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/entity" ) ) {
		WA_ApiEntity( c, body );
	} else {
		WA_RespondText( c, 404, "Not Found", "not found" );
	}
}

static void WA_ReadRequest( waConnection_t *c ) {
	char		*headEnd;
	const char	*lenHeader;
	int			received, bodyLen = 0, headLen;

	received = recv( c->sock, c->request + c->requestLen, WA_MAX_REQUEST - c->requestLen, 0 );
	if ( received == 0 || ( received < 0 && !WA_WouldBlock() ) ) {
		WA_CloseConnection( c );
		return;
	}
	if ( received < 0 )
		return;

	c->requestLen += received;
	c->request[c->requestLen] = '\0';
	c->lastActive = trap->Milliseconds();

	headEnd = strstr( c->request, "\r\n\r\n" );
	if ( !headEnd ) {
		if ( c->requestLen >= WA_MAX_REQUEST )
			WA_CloseConnection( c );
		return;
	}

	headLen = (int)( headEnd - c->request ) + 4;
	*headEnd = '\0';
	lenHeader = WA_FindHeader( c->request, "Content-Length" );
	if ( lenHeader )
		bodyLen = atoi( lenHeader );
	*headEnd = '\r';

	if ( bodyLen < 0 || headLen + bodyLen > WA_MAX_REQUEST ) {
		WA_CloseConnection( c );
		return;
	}
	if ( c->requestLen < headLen + bodyLen )
		return;		// body still coming

	*headEnd = '\0';
	c->request[headLen + bodyLen] = '\0';
	WA_HandleRequest( c, c->request, c->request + headLen );
}

static void WA_WriteResponse( waConnection_t *c ) {
	while ( c->sent < c->response.len ) {
		const int n = send( c->sock, c->response.data + c->sent, c->response.len - c->sent, WA_SEND_FLAGS );
		if ( n < 0 ) {
			if ( !WA_WouldBlock() )
				WA_CloseConnection( c );
			return;
		}
		c->sent += n;
		c->lastActive = trap->Milliseconds();
	}
	WA_CloseConnection( c );
}

static void WA_SetNonBlocking( waSocket_t s ) {
#ifdef _WIN32
	u_long on = 1;
	ioctlsocket( s, FIONBIO, &on );
#else
	fcntl( s, F_SETFL, fcntl( s, F_GETFL, 0 ) | O_NONBLOCK );
#endif
}

static void WA_Accept( void ) {
	for ( ;; ) {
		struct sockaddr_in	from;
		socklen_t			fromLen = sizeof( from );
		waSocket_t			s = accept( listenSocket, (struct sockaddr *)&from, &fromLen );
		int					i;

		if ( s == WA_INVALID_SOCKET )
			return;

		WA_SetNonBlocking( s );
#ifdef SO_NOSIGPIPE
		{
			int on = 1;
			setsockopt( s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof( on ) );
		}
#endif

		for ( i = 0; i < WA_MAX_CONNECTIONS; i++ ) {
			if ( !connections[i].inUse )
				break;
		}
		if ( i == WA_MAX_CONNECTIONS ) {
			WA_CloseSocket( s );
			continue;
		}

		memset( &connections[i], 0, sizeof( connections[i] ) );
		connections[i].inUse = qtrue;
		connections[i].sock = s;
		connections[i].lastActive = trap->Milliseconds();
	}
}

/*
==============================================================================

Public

==============================================================================
*/

void WebAdmin_Init( void ) {
	struct sockaddr_in	addr;
	int					on = 1, i;

	for ( i = 0; i < WA_MAX_CONNECTIONS; i++ )
		connections[i].sock = WA_INVALID_SOCKET;

	if ( !g_webPassword.string[0] ) {
		if ( g_webPort.integer > 0 )
			trap->Print( "Web admin is off, set g_webPassword to start it\n" );
		return;
	}
	if ( g_webPort.integer <= 0 || g_webPort.integer > 65535 )
		return;

#ifdef _WIN32
	{
		WSADATA wsa;
		WSAStartup( MAKEWORD( 2, 2 ), &wsa );
	}
#endif

	listenSocket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	if ( listenSocket == WA_INVALID_SOCKET ) {
		trap->Print( S_COLOR_YELLOW "Web admin: could not create a socket\n" );
		return;
	}
	setsockopt( listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof( on ) );

	memset( &addr, 0, sizeof( addr ) );
	addr.sin_family = AF_INET;
	addr.sin_port = htons( (unsigned short)g_webPort.integer );
	if ( !g_webBind.string[0] || inet_pton( AF_INET, g_webBind.string, &addr.sin_addr ) != 1 )
		addr.sin_addr.s_addr = htonl( INADDR_ANY );

	if ( bind( listenSocket, (struct sockaddr *)&addr, sizeof( addr ) ) != 0 || listen( listenSocket, 16 ) != 0 ) {
		trap->Print( S_COLOR_YELLOW "Web admin: could not listen on %s:%d (port in use, or below 1024 without root?)\n",
			g_webBind.string[0] ? g_webBind.string : "0.0.0.0", g_webPort.integer );
		WA_CloseSocket( listenSocket );
		listenSocket = WA_INVALID_SOCKET;
		return;
	}

	WA_SetNonBlocking( listenSocket );
	// the engine loads the module again for every map and map_restart, the chat lines of the last one are gone
	{
		qtime_t now;
		chatEpoch = trap->RealTime( &now );
	}
	listenPort = g_webPort.integer;
	trap->Print( "Web admin listening on http://%s:%d/\n", g_webBind.string[0] ? g_webBind.string : "0.0.0.0", listenPort );
}

/*
=================
WebAdmin_ClientGravity

Not in JKAdmin. ClientThink_real asks here for the gravity of a player: his own one if the panel gave him
one, else the gravity it passes in.
=================
*/
float WebAdmin_ClientGravity( const gentity_t *ent, float gravity ) {
	const int num = (int)( ent-g_entities );

	return ( num >= 0 && num < MAX_CLIENTS && clientGravitySet[num] ) ? clientGravity[num] : gravity;
}

void WebAdmin_Frame( void ) {
	const int	now = trap->Milliseconds();
	int			i;

	if ( listenSocket == WA_INVALID_SOCKET )
		return;

	// the own gravity goes with the player, the next one in his slot starts without
	for ( i = 0; i < level.maxclients; i++ ) {
		if ( clientGravitySet[i] && level.clients[i].pers.connected == CON_DISCONNECTED )
			clientGravitySet[i] = qfalse;
	}

	WA_Accept();

	for ( i = 0; i < WA_MAX_CONNECTIONS; i++ ) {
		waConnection_t *c = &connections[i];

		if ( !c->inUse )
			continue;

		if ( now - c->lastActive > WA_TIMEOUT_MS ) {
			WA_CloseConnection( c );
			continue;
		}

		if ( !c->response.len )
			WA_ReadRequest( c );
		if ( c->inUse && c->response.len )
			WA_WriteResponse( c );
	}
}

void WebAdmin_Shutdown( void ) {
	int i;

	for ( i = 0; i < WA_MAX_CONNECTIONS; i++ ) {
		if ( connections[i].inUse )
			WA_CloseConnection( &connections[i] );
	}
	if ( listenSocket != WA_INVALID_SOCKET ) {
		WA_CloseSocket( listenSocket );
		listenSocket = WA_INVALID_SOCKET;
	}

	for ( i = 0; i < MAX_GENTITIES; i++ )
		WebAdmin_EntityFreed( i );

	free( mapData );
	mapData = NULL;
	mapLen = 0;
	mapDataName[0] = '\0';
}
