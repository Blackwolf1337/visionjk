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

// g_webadmin.c -- interactive web admin panel served by the game module.
//
// A tiny non-blocking HTTP server that is polled once per server frame. It
// serves one embedded page (3D view of the map and all players, RTS style),
// the map's .bsp and its textures, the live game state as JSON and a set of
// admin actions (kick, ban, set health, scale, teleport, ...).
//
// It only starts when g_webPassword is set. Every request needs HTTP basic
// auth with that password (any user name).

#include "game/bg_net.h"		// the sockets, in front of everything else
#ifdef _WIN32
	#include <io.h>
	#include <direct.h>
	#include <sys/stat.h>
#else
	#include <sys/stat.h>
	#include <dirent.h>
#endif

#include "game/g_local.h"
#include "qcommon/qfiles.h"		// the header of a .bsp
#include "webadmin_html.h"
#include "classdoc_txt.h"

#define WA_MAX_CONNECTIONS	32
#define WA_MAX_REQUEST		65536		// head and body. The largest body is the text of the server config.
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
	qboolean		inUse;
	bgSocket_t		sock;
	unsigned int	addr;			// IPv4 of the peer, for the login throttle
	char		request[WA_MAX_REQUEST + 1];
	int			requestLen;
	waBuffer_t	response;
	int			sent;
	int			lastActive;		// trap->Milliseconds()
} waConnection_t;

// login throttle: after WA_AUTH_TRIES wrong passwords an address waits, the wait doubles from WA_AUTH_BASE_MS
#define WA_AUTH_TRIES		5
#define WA_AUTH_BASE_MS		30000
#define WA_AUTH_MAX_MS		1800000
#define WA_AUTH_SLOTS		64

typedef struct waAuthFail_s {
	unsigned int	addr;
	int				fails;
	int				until;		// blocked while trap->Milliseconds() < until
	int				seen;		// last time this slot was touched, for reuse
} waAuthFail_t;

static waAuthFail_t	authFails[WA_AUTH_SLOTS];

static bgSocket_t		listenSocket = BG_INVALID_SOCKET;
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

// entities that were spawned through the panel: WA_ReloadMap() puts these back after the map has started again
static qboolean	entityPlaced[MAX_GENTITIES];
// what WA_ReloadMap() left for this start of the map, the entities one after the other. NULL: nothing.
static char		*reloadText;

static const char *WA_TeamName( team_t team );
static void WA_ReloadMap( waConnection_t *c );

/*
==============================================================================

Buffers

==============================================================================
*/

// grows the buffer to hold extra more bytes and a zero. qfalse if it is out of memory or the size would wrap;
// the buffer is left as it was, so a caller that checks the result leaves a short response instead of crashing.
static qboolean WA_BufReserve( waBuffer_t *b, int extra ) {
	int		newSize;
	char	*grown;

	if ( extra < 0 || b->len > WA_MAX_FILE || extra > WA_MAX_FILE )
		return qfalse;
	if ( b->len + extra + 1 <= b->size )
		return qtrue;

	newSize = b->size ? b->size : 4096;
	while ( newSize < b->len + extra + 1 )
		newSize *= 2;
	grown = (char *)realloc( b->data, newSize );
	if ( !grown )
		return qfalse;
	b->data = grown;
	b->size = newSize;
	return qtrue;
}

static void WA_BufAppend( waBuffer_t *b, const void *data, int len ) {
	if ( len <= 0 || !WA_BufReserve( b, len ) )
		return;
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

	if ( len < 0 )
		return;
	if ( len < (int)sizeof( small ) ) {
		WA_BufAppend( b, small, len );
		return;
	}

	if ( !WA_BufReserve( b, len ) )
		return;
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
		// nothing of the panel inside a frame or as a resource of another site
		"X-Frame-Options: DENY\r\n"
		"Content-Security-Policy: frame-ancestors 'none'\r\n"
		"Cross-Origin-Resource-Policy: same-origin\r\n"
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

// the address of a peer as text, for the security log
static const char *WA_AddrText( const waConnection_t *c ) {
	const unsigned char *b = (const unsigned char *)&c->addr;		// network order: the bytes as they are written

	return va( "%u.%u.%u.%u", b[0], b[1], b[2], b[3] );
}

// Not in JKAdmin: what was done through the panel and from where, one line in the security log. The panel
// changes the game with the rights of an admin, and without this nothing on the server showed that it was used.
static void WA_LogUse( const waConnection_t *c, const char *what ) {
	char line[320];

	Com_sprintf( line, sizeof( line ), "Web admin: %s: %.280s", WA_AddrText( c ), what );
	Q_strstrip( line, "\n\r", "  " );
	G_SecurityLogPrintf( "%s\n", line );
}

static int WA_Base64Value( char c ) {
	if ( c >= 'A' && c <= 'Z' ) return c - 'A';
	if ( c >= 'a' && c <= 'z' ) return c - 'a' + 26;
	if ( c >= '0' && c <= '9' ) return c - '0' + 52;
	if ( c == '+' ) return 62;
	if ( c == '/' ) return 63;
	return -1;
}

static int WA_Base64Decode( const char *in, char *out, int outSize ) {
	int bits = 0, len = 0;
	unsigned int value = 0;		// unsigned: the running bits shift left without signed overflow

	for ( ; *in && *in != '\r' && *in != '\n' && *in != '='; in++ ) {
		const int v = WA_Base64Value( *in );
		if ( v < 0 )
			return -1;
		value = ( value << 6 ) | (unsigned int)v;
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

// Copies the value of a header up to the end of its line, without the spaces at its end. qfalse: not there.
static qboolean WA_HeaderValue( const char *head, const char *name, char *out, int outSize ) {
	const char	*v = WA_FindHeader( head, name );
	int			len = 0;

	out[0] = '\0';
	if ( !v )
		return qfalse;
	while ( v[len] && v[len] != '\r' && v[len] != '\n' && len < outSize - 1 )
		len++;
	while ( len > 0 && ( v[len - 1] == ' ' || v[len - 1] == '\t' ) )
		len--;
	memcpy( out, v, len );
	out[len] = '\0';
	return qtrue;
}

/*
=================
WA_SameOrigin

Not in JKAdmin. A request that changes something has to come from the page itself. The browser sends the login
of the panel along with every request to it, also with one that another web site makes it send (CSRF). That
site would get at the player actions and at the entity editor without knowing the password.

Browsers say where a POST comes from: Sec-Fetch-Site, Origin, Referer. A page can not leave out or forge the
first two. A request with none of the three is not from a browser (curl, scripts) and goes through.
The address in Origin has to be the one the request was sent to: Host, or X-Forwarded-Host behind a reverse
proxy that sends another Host to the server (a page can not set that header on a request to another site).
=================
*/
static qboolean WA_SameOrigin( const char *head ) {
	static const char *const sentTo[] = { "Host", "X-Forwarded-Host" };
	char		from[512], host[256];
	const char	*address;
	size_t		i;

	if ( WA_HeaderValue( head, "Sec-Fetch-Site", from, sizeof( from ) ) && Q_stricmp( from, "same-origin" ) && Q_stricmp( from, "none" ) )
		return qfalse;

	if ( !WA_HeaderValue( head, "Origin", from, sizeof( from ) ) && !WA_HeaderValue( head, "Referer", from, sizeof( from ) ) )
		return qtrue;

	// scheme://host[:port][/path]. "null" (a sandboxed frame, a data: page) is no address.
	address = strstr( from, "://" );
	if ( !address )
		return qfalse;
	address += 3;

	for ( i = 0; i < ARRAY_LEN( sentTo ); i++ ) {
		if ( WA_HeaderValue( head, sentTo[i], host, sizeof( host ) ) && host[0] ) {
			const size_t len = strlen( host );

			if ( !Q_stricmpn( address, host, (int)len ) && ( address[len] == '\0' || address[len] == '/' ) )
				return qtrue;
		}
	}
	return qfalse;
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
	if ( !pass || !g_webPassword.string[0] )
		return qfalse;

	// constant time: the time it takes does not tell how much of the password is right
	{
		const char	*a = pass + 1, *b = g_webPassword.string;
		const int	la = (int)strlen( a ), lb = (int)strlen( b );
		int			diff = la ^ lb, i;

		for ( i = 0; i < la; i++ )
			diff |= a[i] ^ b[i % lb];
		return (qboolean)( diff == 0 );
	}
}

/*
=================
WA_AuthThrottle

Not in JKAdmin. Slows down guessing the panel password: after WA_AUTH_TRIES wrong tries an address has to wait,
and the wait doubles each time. WA_AuthThrottle tells if an address is waiting right now, WA_AuthResult records
how a try went. A small table of addresses, the slot not seen for the longest is reused when it is full.
=================
*/
static waAuthFail_t *WA_AuthSlot( unsigned int addr, qboolean create ) {
	const int	now = trap->Milliseconds();
	int			i, oldest = 0;

	for ( i = 0; i < WA_AUTH_SLOTS; i++ ) {
		if ( authFails[i].fails && authFails[i].addr == addr )
			return &authFails[i];
	}
	if ( !create )
		return NULL;
	for ( i = 0; i < WA_AUTH_SLOTS; i++ ) {
		if ( !authFails[i].fails )
			return &authFails[i];		// a free slot
		if ( authFails[i].seen < authFails[oldest].seen )
			oldest = i;
	}
	// full: take the slot not used for the longest, but not one still blocking
	if ( authFails[oldest].until > now )
		return NULL;
	memset( &authFails[oldest], 0, sizeof( authFails[oldest] ) );
	return &authFails[oldest];
}

static qboolean WA_AuthThrottle( unsigned int addr ) {
	waAuthFail_t	*slot = WA_AuthSlot( addr, qfalse );
	const int		now = trap->Milliseconds();

	return (qboolean)( slot && slot->until > now );
}

static void WA_AuthResult( unsigned int addr, qboolean ok ) {
	const int		now = trap->Milliseconds();
	waAuthFail_t	*slot;

	if ( ok ) {
		if ( (slot = WA_AuthSlot( addr, qfalse )) )
			memset( slot, 0, sizeof( *slot ) );
		return;
	}
	if ( !(slot = WA_AuthSlot( addr, qtrue )) )
		return;

	slot->addr = addr;
	slot->seen = now;
	slot->fails++;
	if ( slot->fails >= WA_AUTH_TRIES ) {
		const int steps = slot->fails - WA_AUTH_TRIES;
		int wait = WA_AUTH_BASE_MS;

		// double the wait for each try past the limit, capped (steps stays small, no overflow)
		if ( steps < 16 )
			wait <<= steps;
		if ( wait > WA_AUTH_MAX_MS || wait <= 0 )
			wait = WA_AUTH_MAX_MS;
		slot->until = now + wait;
	}
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
	if ( !data ) {
		trap->FS_Close( f );
		return NULL;
	}
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
		// Not in JKAdmin: who that is for an admin. Where he connects from (without the port, nothing for a bot),
		// how long he has been in the game and which client plugin of JA+ he has, 0 for none.
		{
			char address[NET_ADDRSTRMAXLEN], *port;

			Q_strncpyz( address, ( ent->r.svFlags & SVF_BOT ) ? "" : cl->sess.IP, sizeof( address ) );
			port = strchr( address, ':' );
			if ( port )
				*port = '\0';
			WA_BufAppend( &b, ",\"address\":", 11 );
			WA_BufJsonString( &b, address );
			WA_BufPrintf( &b, ",\"minutes\":%d,\"plugin\":%.2f", ( level.time - cl->pers.enterTime ) / 60000, cl->pers.jp.pluginVersion );
		}
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
		WA_BufPrintf( &b, ",\"flatulence\":%s", WA_BOOL( G_FlatulenceActive( i ) ) );	// not in JKAdmin, g_flatulence.c
		WA_BufPrintf( &b, ",\"disorient\":%s", WA_BOOL( G_DisorientActive( i ) ) );	// not in JKAdmin, g_disorient.c
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

	// Not in JKAdmin: what else the clients draw as a model, where they draw it: the props, the models of the
	// map that are entities. Not what is made of brushes, and not what only one client is sent.
	WA_BufAppend( &b, "],\"models\":[", 12 );
	first = qtrue;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t	*ent = &g_entities[i];
		char		model[MAX_QPATH];
		vec3_t		pos, angles;

		if ( !ent->inuse || ent->s.eType != ET_GENERAL || !ent->s.modelindex || ent->r.bmodel || ( ent->s.eFlags & EF_NODRAW )
			|| ( ent->r.svFlags & ( SVF_NOCLIENT | SVF_SINGLECLIENT ) ) )
			continue;
		trap->GetConfigstring( CS_MODELS + ent->s.modelindex, model, sizeof( model ) );
		if ( !model[0] || model[0] == '*' )
			continue;

		if ( !first )
			WA_BufAppend( &b, ",", 1 );
		first = qfalse;

		BG_EvaluateTrajectory( &ent->s.pos, level.time, pos );
		BG_EvaluateTrajectory( &ent->s.apos, level.time, angles );
		WA_BufPrintf( &b, "{\"id\":%d,\"scale\":%d,\"model\":", i, ent->s.iModelScale ? ent->s.iModelScale : 100 );
		WA_BufJsonString( &b, model );
		WA_BufAppend( &b, ",\"pos\":", 7 );
		WA_AppendVec( &b, pos );
		WA_BufAppend( &b, ",\"angles\":", 10 );
		WA_AppendVec( &b, angles );
		WA_BufAppend( &b, "}", 1 );
	}

	// Not in JKAdmin: what is made of brushes and is an entity (doors, lifts, what turns, what can be switched
	// off or broken), as the clients draw it (CG_Mover). Each is the number of its brush model, or that number
	// with its place and its angles when it is not where the map file has it. One that is not in the list is
	// not drawn by a client either.
	WA_BufAppend( &b, "],\"movers\":[", 12 );
	first = qtrue;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t	*ent = &g_entities[i];
		vec3_t		pos, angles;

		if ( !ent->inuse || !ent->r.linked || !ent->r.bmodel || ent->s.eType != ET_MOVER || ent->s.modelindex <= 0
			|| ( ent->s.eFlags & EF_NODRAW ) || ( ent->s.eFlags2 & EF2_HYPERSPACE )
			|| ( ent->r.svFlags & ( SVF_NOCLIENT | SVF_SINGLECLIENT ) ) )
			continue;

		if ( !first )
			WA_BufAppend( &b, ",", 1 );
		first = qfalse;

		BG_EvaluateTrajectory( &ent->s.pos, level.time, pos );
		BG_EvaluateTrajectory( &ent->s.apos, level.time, angles );
		if ( VectorCompare( pos, vec3_origin ) && VectorCompare( angles, vec3_origin ) )
			WA_BufPrintf( &b, "%d", ent->s.modelindex );
		else
			WA_BufPrintf( &b, "[%d,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f]", ent->s.modelindex, pos[0], pos[1], pos[2],
				angles[0], angles[1], angles[2] );
	}
	WA_BufAppend( &b, "]}", 2 );

	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

// the .bsp of the map that runs, read once and kept. qfalse: it can not be read
static qboolean WA_LoadMap( void ) {
	char mapname[MAX_QPATH];

	trap->Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );

	if ( !mapData || Q_stricmp( mapDataName, mapname ) ) {
		free( mapData );
		mapData = WA_ReadGameFile( va( "maps/%s.bsp", mapname ), &mapLen );
		Q_strncpyz( mapDataName, mapname, sizeof( mapDataName ) );
	}
	return (qboolean)( mapData != NULL );
}

// how many brush models the map has, the world being the first: *1 to one less than this can be named. -1: not known
static int WA_MapBrushModels( void ) {
	dheader_t header;

	if ( !WA_LoadMap() || mapLen < (int)sizeof( header ) )
		return -1;
	memcpy( &header, mapData, sizeof( header ) );
	return LittleLong( header.lumps[LUMP_MODELS].filelen ) / (int)sizeof( dmodel_t );
}

static void WA_ApiMap( waConnection_t *c ) {
	if ( !WA_LoadMap() ) {
		WA_RespondText( c, 404, "Not Found", "map not found" );
		return;
	}
	WA_Respond( c, 200, "OK", "application/octet-stream", "Cache-Control: no-store\r\n", mapData, mapLen );
}

// Textures, models and skins for the 3D views, and the shader scripts the view of the administration draws the
// map with. Only below textures/, models/ and gfx/, and of shaders/ the scripts.
static void WA_ApiFile( waConnection_t *c, const char *query ) {
	char		name[MAX_QPATH];
	const char	*ext, *type;
	char		*data;
	int			len;
	qboolean	script, audio;

	if ( !WA_FormValue( query, "name", name, sizeof( name ) ) || strstr( name, ".." ) || strchr( name, '\\' ) || strchr( name, ':' ) ) {
		WA_RespondText( c, 400, "Bad Request", "bad file name" );
		return;
	}
	ext = COM_GetExtension( name );
	script = (qboolean)( !Q_stricmpn( name, "shaders/", 8 ) && !Q_stricmp( ext, "shader" ) );
	// a sound or a piece of music, which the editor plays in the browser to hear what a key names
	audio = (qboolean)( ( !Q_stricmpn( name, "sound/", 6 ) || !Q_stricmpn( name, "music/", 6 ) ) && ( !Q_stricmp( ext, "wav" ) || !Q_stricmp( ext, "mp3" ) ) );
	if ( !script && !audio && Q_stricmpn( name, "textures/", 9 ) && Q_stricmpn( name, "models/", 7 ) && Q_stricmpn( name, "gfx/", 4 ) ) {
		WA_RespondText( c, 400, "Bad Request", "bad file name" );
		return;
	}

	if ( script ) type = "text/plain; charset=utf-8";
	else if ( audio ) type = Q_stricmp( ext, "mp3" ) ? "audio/wav" : "audio/mpeg";
	else if ( !Q_stricmp( ext, "jpg" ) ) type = "image/jpeg";
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
WA_ApiConfigGet, WA_ApiConfigSet

Not in JKAdmin: the server config in the panel. It is this one file and no other, the request names none.

The engine looks for a file in the home path first (fs_homepath/<game>/server.cfg) and then in the base path,
and it only ever writes to the home path. So the first save puts the edited text into the home path, and that
is the file "exec server.cfg" takes from then on. The one in the base path, the installation, is not touched.
What was there before a save is kept as server.cfg.bak next to it.

The file has one byte per character like everything in the game (Latin-1), the page gets and sends UTF-8.
The passwords in it are shown as they are: who can log into the panel can do everything on the server anyway.
=================
*/
#define WA_CONFIG_FILE		"server.cfg"
#define WA_CONFIG_BACKUP	"server.cfg.bak"

// text of the game, one byte per character, as the UTF-8 the page reads
static void WA_BufLatin1AsUtf8( waBuffer_t *b, const char *text, int len ) {
	int i;

	for ( i = 0; i < len; i++ ) {
		const unsigned char ch = (unsigned char)text[i];

		if ( ch < 0x80 ) {
			WA_BufAppend( b, &text[i], 1 );
		} else {
			const char utf8[2] = { (char)( 0xC0 | ( ch >> 6 ) ), (char)( 0x80 | ( ch & 0x3F ) ) };

			WA_BufAppend( b, utf8, 2 );
		}
	}
}

// Text the page sends as the body of a request, changed in place into what goes into a file: one byte per
// character and LF at the end of a line (a browser sends the lines of a text field with CR LF). Returns its length.
static int WA_TextBody( char *text ) {
	char *in, *out;

	WA_Utf8ToLatin1( text );
	for ( in = out = text; *in; in++ ) {
		if ( *in != '\r' )
			*out++ = *in;
	}
	*out = '\0';
	return (int)( out - text );
}

static void WA_ApiConfigGet( waConnection_t *c ) {
	waBuffer_t	b = { 0 };
	int			len = 0;
	char		*text = WA_ReadGameFile( WA_CONFIG_FILE, &len );

	WA_BufLatin1AsUtf8( &b, text, len );
	free( text );
	// no file yet is an empty text
	WA_Respond( c, 200, "OK", "text/plain; charset=utf-8", "Cache-Control: no-store\r\n", b.data, b.len );
	WA_BufFree( &b );
}

static qboolean WA_WriteGameFile( const char *path, const char *data, int len ) {
	fileHandle_t f = 0;

	trap->FS_Open( path, &f, FS_WRITE );
	if ( !f )
		return qfalse;
	trap->FS_Write( data, len, f );
	trap->FS_Close( f );
	return qtrue;
}

// text is the body of the request and is changed in place. run: also execute the file.
static void WA_ApiConfigSet( waConnection_t *c, char *text, qboolean run ) {
	char		*old;
	int			oldLen = 0;
	const int	len = WA_TextBody( text );

	if ( !len ) {
		// an empty file is more likely a page that did not load the text than a wish
		WA_ActionResult( c, qfalse, "an empty config is not saved" );
		return;
	}

	old = WA_ReadGameFile( WA_CONFIG_FILE, &oldLen );
	if ( old ) {
		WA_WriteGameFile( WA_CONFIG_BACKUP, old, oldLen );
		free( old );
	}
	if ( !WA_WriteGameFile( WA_CONFIG_FILE, text, len ) ) {
		WA_ActionResult( c, qfalse, "could not write " WA_CONFIG_FILE );
		return;
	}
	// what is in it stays out of the log
	WA_LogUse( c, va( "config saved, %d bytes%s", len, run ? ", executed" : "" ) );
	if ( run )
		trap->SendConsoleCommand( EXEC_APPEND, "exec " WA_CONFIG_FILE "\n" );
	WA_ActionResult( c, qtrue, run ? "saved and executed" : "saved, not executed yet" );
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

static qboolean WA_ConfigstringRoom( int start, int max, const char *name );

static void WA_ApiAction( waConnection_t *c, const char *body ) {
	char		action[32], idStr[16], value[256], xs[32], ys[32], zs[32];
	gentity_t	*ent = NULL;
	int			id;

	WA_FormValue( body, "action", action, sizeof( action ) );
	WA_FormValue( body, "value", value, sizeof( value ) );
	WA_FormValue( body, "id", idStr, sizeof( idStr ) );
	WA_LogUse( c, va( "action %s id %s value \"%.100s\"", action, idStr[0] ? idStr : "-", value ) );

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

	if ( !strcmp( action, "reload" ) ) {
		// not in JKAdmin: the running map again, so the engine finds a pk3 that was uploaded
		WA_ReloadMap( c );
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

	if ( !strcmp( action, "banip" ) || !strcmp( action, "unban" ) ) {
		// Not in JKAdmin: a ban by its address, which is how one is lifted. addip and removeip of the game do it,
		// and write the file of the bans. Only what such an address is made of goes into the line.
		if ( !value[0] || strlen( value ) > 15 || strspn( value, "0123456789.*" ) != strlen( value ) || !strpbrk( value, "0123456789" ) ) {
			WA_ActionResult( c, qfalse, "not an address: four numbers with dots, a * for any" );
			return;
		}
		trap->SendConsoleCommand( EXEC_APPEND, va( "%s %s\n", action[0] == 'b' ? "addip" : "removeip", value ) );
		WA_ActionResult( c, qtrue, va( "%s %s", action[0] == 'b' ? "banned" : "ban lifted for", value ) );
		return;
	}

	if ( !strcmp( action, "effect" ) || !strcmp( action, "sound" ) ) {
		// Not in JKAdmin: an effect or a sound of the game once at a place, so that who is in the game sees and
		// hears what a key of an entity would bring. Its name takes a place among the config strings if it has
		// none yet, and the server would end without one left.
		const qboolean	fx = (qboolean)( action[0] == 'e' );
		vec3_t			at, up = { 0, 0, 1 };

		Q_strstrip( value, "\n\r\";", NULL );
		if ( !value[0] || strstr( value, ".." ) || !WA_FormValue( body, "x", xs, sizeof( xs ) ) || !WA_FormValue( body, "y", ys, sizeof( ys ) )
			|| !WA_FormValue( body, "z", zs, sizeof( zs ) ) ) {
			WA_ActionResult( c, qfalse, "no name or no place" );
			return;
		}
		if ( !WA_ConfigstringRoom( fx ? CS_EFFECTS : CS_SOUNDS, fx ? MAX_FX : MAX_SOUNDS, value ) ) {
			WA_ActionResult( c, qfalse, va( "the map has no room for another one of its %s", fx ? "effects" : "sounds" ) );
			return;
		}
		VectorSet( at, Com_Clamp( -MAX_WORLD_COORD, MAX_WORLD_COORD, atof( xs ) ), Com_Clamp( -MAX_WORLD_COORD, MAX_WORLD_COORD, atof( ys ) ),
			Com_Clamp( -MAX_WORLD_COORD, MAX_WORLD_COORD, atof( zs ) ) );
		if ( fx )
			G_PlayEffectID( G_EffectIndex( value ), at, up );
		else
			G_SoundAtLoc( at, CHAN_AUTO, G_SoundIndex( value ) );
		WA_ActionResult( c, qtrue, va( "%s played in the game", value ) );
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

		char raw[64];

		// the stored IP is already the real one the engine set, this only keeps a console line clean
		trap->GetUserinfo( id, userinfo, sizeof( userinfo ) );
		Q_strncpyz( raw, Info_ValueForKey( userinfo, "ip" ), sizeof( raw ) );
		port = strchr( raw, ':' );
		if ( port )
			*port = '\0';
		if ( !raw[0] || !Q_stricmp( raw, "localhost" ) || !Q_stricmp( raw, "bot" ) ) {
			WA_ActionResult( c, qfalse, "this player has no IP to ban" );
			return;
		}
		if ( !G_CleanIP( raw, ip, sizeof( ip ) ) ) {
			WA_ActionResult( c, qfalse, "this player's IP is not a plain address" );
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
	} else if ( !strcmp( action, "poop" ) ) {
		// not in JKAdmin: the emote ampoop for him, see g_poop.c. The rules of the emote still decide.
		JP_EmotePoop( ent );
		WA_ActionResult( c, qtrue, "ampoop sent" );
	} else if ( !strcmp( action, "flatulence" ) ) {
		// not in JKAdmin: he breaks wind all the time, see g_flatulence.c
		WA_ActionResult( c, qtrue, G_FlatulenceToggle( ent ) ? "flatulence on" : "flatulence off" );
	} else if ( !strcmp( action, "disorient" ) ) {
		// not in JKAdmin: the view sways and is thrown around, see g_disorient.c
		WA_ActionResult( c, qtrue, G_DisorientToggle( ent ) ? "disorientation on" : "disorientation off" );
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
	entityPlaced[entityNum] = qfalse;
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
	entityPlaced[entityNum] = qfalse;
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

#define WA_ENTITY_RESERVE	64		// free entities the editor leaves to the game: shots, effects, bodies

// entities the game can still spawn: the slots it never used and the ones that are free for long enough
static int WA_FreeEntities( void ) {
	int i, count = ENTITYNUM_MAX_NORMAL - level.num_entities;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		const gentity_t *ent = &g_entities[i];

		// G_Spawn() leaves a slot alone for a second after it was freed
		if ( !ent->inuse && !( ent->freetime > level.startTime + 2000 && level.time - ent->freetime < 1000 ) )
			count++;
	}
	return count;
}

// how many names of a kind (models, sounds, effects) are taken. The first place of each kind is never used.
static int WA_ConfigstringsUsed( int start, int max ) {
	char	name[MAX_STRING_CHARS];
	int		i, count = 0;

	for ( i = 1; i < max; i++ ) {
		trap->GetConfigstring( start + i, name, sizeof( name ) );
		if ( name[0] )
			count++;
	}
	return count;
}

// qtrue: the name has its place among the config strings already, or there is one left for it. The compare is
// the one of G_FindConfigstringIndex(), which ends the server when it finds no place.
static qboolean WA_ConfigstringRoom( int start, int max, const char *name ) {
	char	have[MAX_STRING_CHARS];
	int		i;

	for ( i = 1; i < max; i++ ) {
		trap->GetConfigstring( start + i, have, sizeof( have ) );
		if ( !have[0] || !strcmp( have, name ) )
			return qtrue;
	}
	return qfalse;
}

// the value of a key of the entity that is about to be spawned, NULL without it
static const char *WA_SpawnVar( const char *key ) {
	int i;

	for ( i = 0; i < level.numSpawnVars; i++ ) {
		if ( !Q_stricmp( key, level.spawnVars[i][0] ) )
			return level.spawnVars[i][1];
	}
	return NULL;
}

/*
=================
WA_SpawnRefused

Not in JKAdmin. The spawn code of the game ends the server when an entity is not what it has to be: an error of
the game is fatal on a dedicated server. So does running out of entities, and out of places for the names of
models, sounds and effects. What can be told from the keys is looked at here before the spawn code runs.

qtrue: it is not spawned, err says why. The keys are in level.spawnVars.
=================
*/
static qboolean WA_SpawnRefused( char *err, int errSize ) {
	// trap->Error in the spawn function of the class without the key. unless: the other key that will do
	static const struct { const char *classname, *key, *unless; } needs[] = {
		{ "target_speaker", "noise", "soundSet" },
		{ "target_play_music", "music", NULL },
		{ "target_level_change", "mapname", NULL },
		{ "trigger_lightningstrike", "lightningfx", NULL },
		{ "misc_siege_item", "model", NULL },
		{ "misc_model_breakable", "model", NULL },
		{ "trigger_shipboundary", "target", NULL },
		{ "trigger_shipboundary", "traveltime", NULL },
		{ "trigger_hyperspace", "target", NULL },
		{ "trigger_hyperspace", "target2", NULL },
	};
	// keys whose value gets a place among the config strings
	static const struct { const char *key; int start, max; const char *what; } names[] = {
		{ "model", CS_MODELS, MAX_MODELS, "models" },
		{ "model2", CS_MODELS, MAX_MODELS, "models" },
		{ "noise", CS_SOUNDS, MAX_SOUNDS, "sounds" },
		{ "fxFile", CS_EFFECTS, MAX_FX, "effects" },
		{ "lightningfx", CS_EFFECTS, MAX_FX, "effects" },
	};
	const char	*classname = WA_SpawnVar( "classname" ), *value;
	const int	room = WA_FreeEntities();
	size_t		i;

	if ( room < WA_ENTITY_RESERVE ) {
		Com_sprintf( err, errSize, "the map is full: %d entities are left, the game needs %d for itself", room, WA_ENTITY_RESERVE );
		return qtrue;
	}

	for ( i = 0; i < ARRAY_LEN( needs ); i++ ) {
		if ( Q_stricmp( classname, needs[i].classname ) )
			continue;
		value = WA_SpawnVar( needs[i].key );
		if ( !VALIDSTRING( value ) ) {
			value = needs[i].unless ? WA_SpawnVar( needs[i].unless ) : NULL;
			if ( VALIDSTRING( value ) )
				continue;
			Com_sprintf( err, errSize, "a %s needs the key %s", classname, needs[i].key );
			return qtrue;
		}
		// these two look for what they name while they spawn
		if ( !Q_stricmpn( needs[i].key, "target", 6 ) && !G_Find( NULL, FOFS( targetname ), value ) ) {
			Com_sprintf( err, errSize, "a %s needs an entity with the targetname %s, there is none", classname, value );
			return qtrue;
		}
	}

	// the classes that are made of brushes of the map take them from the model key
	if ( ( !Q_stricmpn( classname, "trigger_", 8 ) && Q_stricmp( classname, "trigger_always" ) )
		|| ( !Q_stricmpn( classname, "func_", 5 ) && Q_stricmp( classname, "func_timer" ) && Q_stricmp( classname, "func_group" ) ) ) {
		const int count = WA_MapBrushModels();

		value = WA_SpawnVar( "model" );
		if ( !value || value[0] != '*' || atoi( value + 1 ) < 1 ) {
			Com_sprintf( err, errSize, "a %s is made of brushes of the map: it needs a model key like *1", classname );
			return qtrue;
		}
		if ( count >= 0 && atoi( value + 1 ) >= count ) {
			Com_sprintf( err, errSize, "the map has no brush model %s, the last one is *%d", value, count - 1 );
			return qtrue;
		}
	}

	for ( i = 0; i < ARRAY_LEN( names ); i++ ) {
		value = WA_SpawnVar( names[i].key );
		if ( VALIDSTRING( value ) && value[0] != '*' && !WA_ConfigstringRoom( names[i].start, names[i].max, value ) ) {
			Com_sprintf( err, errSize, "the map has no room for another one of its %s (%d at most)", names[i].what, names[i].max - 1 );
			return qtrue;
		}
	}
	return qfalse;
}

static void WA_ApiEntities( waConnection_t *c ) {
	waBuffer_t	b = { 0 };
	qboolean	first = qtrue;
	int			i;

	// how much of what the engine has a limit for is in use, and how many brush models the map has
	WA_BufPrintf( &b, "{\"budget\":{\"entities\":[%d,%d],\"models\":[%d,%d],\"sounds\":[%d,%d],\"effects\":[%d,%d]},\"brushModels\":%d,",
		ENTITYNUM_MAX_NORMAL - MAX_CLIENTS - WA_FreeEntities(), ENTITYNUM_MAX_NORMAL - MAX_CLIENTS - WA_ENTITY_RESERVE,
		WA_ConfigstringsUsed( CS_MODELS, MAX_MODELS ), MAX_MODELS - 1, WA_ConfigstringsUsed( CS_SOUNDS, MAX_SOUNDS ), MAX_SOUNDS - 1,
		WA_ConfigstringsUsed( CS_EFFECTS, MAX_FX ), MAX_FX - 1, WA_MapBrushModels() );
	WA_BufAppend( &b, "\"entities\":[", 12 );
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
		WA_BufPrintf( &b, ",\"type\":%d,\"bmodel\":%s,\"linked\":%s,\"placed\":%s,\"pos\":", ent->s.eType, ent->r.bmodel ? "true" : "false",
			ent->r.linked ? "true" : "false", WA_BOOL( entityPlaced[i] ) );
		WA_AppendVec( &b, pos );
		// one that is made of brushes: how far they were moved and turned from where the map file has them
		if ( ent->r.bmodel ) {
			WA_BufAppend( &b, ",\"moved\":", 9 );
			WA_AppendVec( &b, ent->r.currentOrigin );
			WA_BufAppend( &b, ",\"turned\":", 10 );
			WA_AppendVec( &b, ent->r.currentAngles );
		}

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
// placed=1: only what was placed through the panel, without the worldspawn. That is what an import takes on
// another map or another server.
static void WA_ApiExport( waConnection_t *c, const char *query ) {
	waBuffer_t	b = { 0 };
	char		mapname[MAX_QPATH], headers[256], flag[4];
	qboolean	placedOnly;
	int			i;

	WA_FormValue( query, "placed", flag, sizeof( flag ) );
	placedOnly = (qboolean)( flag[0] == '1' );

	for ( i = MAX_CLIENTS; i <= ENTITYNUM_WORLD; i++ ) {
		const int num = i == MAX_CLIENTS ? ENTITYNUM_WORLD : ( i == ENTITYNUM_WORLD ? MAX_CLIENTS : i );	// worldspawn first
		const char *p, *end;

		if ( !entityVars[num] || ( num != ENTITYNUM_WORLD && !g_entities[num].inuse ) )
			continue;
		if ( placedOnly && ( num == ENTITYNUM_WORLD || !entityPlaced[num] ) )
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
	Com_sprintf( headers, sizeof( headers ), "Content-Disposition: attachment; filename=\"%s%s.ent\"\r\nCache-Control: no-store\r\n", COM_SkipPath( mapname ),
		placedOnly ? "_placed" : "" );
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
	if ( WA_SpawnRefused( err, errSize ) )
		return -1;

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

// Movers with the same team key move together (G_FindTeams): they are a chain, the first one leads, the game
// runs only that one and it moves the others, who name it as their master. The game never frees a mover, so
// nothing takes one out of its team either. Left in, the team goes on moving and linking whatever entity gets
// its number, and without its first part the others never move again.
// Returns the part that leads the team from now on when ent led it, NULL otherwise.
static gentity_t *WA_LeaveTeam( gentity_t *ent ) {
	gentity_t	*master = ent->teammaster, *next = NULL, *part;

	if ( !master )
		return NULL;

	if ( master != ent ) {
		for ( part = master; part; part = part->teamchain ) {
			if ( part->teamchain == ent ) {
				part->teamchain = ent->teamchain;
				break;
			}
		}
	} else if ( ( next = ent->teamchain ) != NULL ) {
		next->flags &= ~FL_TEAMSLAVE;
		for ( part = next; part; part = part->teamchain )
			part->teammaster = next;
		// what fires the team fires its first part: G_FindTeams gave that one the name
		if ( !next->targetname )
			next->targetname = ent->targetname;
		// what the team was about to do (come back, start to move) it still does
		if ( !next->think ) {
			next->think = ent->think;
			next->nextthink = ent->nextthink;
		}
	}
	ent->teammaster = ent->teamchain = NULL;
	return next;
}

// An edit spawns a new entity and frees the old one. The new one takes the place of the old one in its team,
// unless the edit gave it another team key.
static void WA_TakeTeamPlace( gentity_t *old, gentity_t *ent ) {
	gentity_t	*master = old->teammaster, *part;

	if ( !master || !old->team || !ent->team || strcmp( old->team, ent->team ) )
		return;

	if ( master == old ) {
		ent->teamchain = old->teamchain;
		ent->flags &= ~FL_TEAMSLAVE;
		for ( part = ent; part; part = part->teamchain )
			part->teammaster = ent;
		if ( !ent->targetname )
			ent->targetname = old->targetname;
	} else {
		for ( part = master; part && part->teamchain != old; part = part->teamchain )
			;
		if ( !part )
			return;
		part->teamchain = ent;
		ent->teamchain = old->teamchain;
		ent->teammaster = master;
		ent->flags |= FL_TEAMSLAVE;
		// as G_FindTeams: targets only point at the first part
		if ( ent->targetname ) {
			master->targetname = ent->targetname;
			ent->targetname = NULL;
		}
	}
	old->teammaster = old->teamchain = NULL;
}

// A func_plat and a func_door make a trigger of their own when they spawn (SpawnPlatTrigger,
// Think_SpawnNewDoorTrigger) and are its parent. The game never frees a mover, so nothing frees that trigger
// either: left behind it would use whatever entity gets the number of the mover next. The trigger of a team
// goes to the part that leads the team now.
static void WA_FreeEntity( gentity_t *ent ) {
	gentity_t	*heir = WA_LeaveTeam( ent );
	int			i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *other = &g_entities[i];

		if ( !other->inuse || other == ent || other->parent != ent || entityVars[i] || other->r.contents != CONTENTS_TRIGGER )
			continue;
		if ( heir )
			other->parent = heir;
		else
			G_FreeEntity( other );
	}
	G_FreeEntity( ent );
}

static void WA_ApiEntity( waConnection_t *c, const char *body ) {
	char		op[16], idStr[16], ofMap[4], err[128];
	static char	kv[WA_MAX_REQUEST];
	int			id = -1, newId;

	WA_FormValue( body, "op", op, sizeof( op ) );
	WA_FormValue( body, "kv", kv, sizeof( kv ) );
	if ( WA_FormValue( body, "id", idStr, sizeof( idStr ) ) )
		id = atoi( idStr );
	WA_LogUse( c, va( "entity %s id %i keys %.200s", op, id, kv ) );

	if ( !strcmp( op, "spawn" ) ) {
		newId = WA_SpawnFromText( kv, err, sizeof( err ) );
		// map=1: an entity of the map that was deleted and is taken back (undo). It is not kept over a start of
		// the map as a placed one is, the map brings it itself.
		if ( newId >= 0 )
			entityPlaced[newId] = (qboolean)!( WA_FormValue( body, "map", ofMap, sizeof( ofMap ) ) && ofMap[0] == '1' );
		WA_EntityResult( c, newId >= 0, newId >= 0 ? "spawned" : err, newId );
		return;
	}

	if ( !strcmp( op, "clear" ) ) {
		// the keys are "classname=<class>": every entity of that class that was placed through the panel goes,
		// the ones of the map stay
		const char	*classname = Q_stricmpn( kv, "classname=", 10 ) ? "" : kv + 10;
		int			i, count = 0;

		for ( i = MAX_CLIENTS; classname[0] && i < level.num_entities; i++ ) {
			gentity_t *ent = &g_entities[i];

			if ( entityPlaced[i] && WA_ListedEntity( ent ) && !Q_stricmp( ent->classname, classname ) ) {
				WA_FreeEntity( ent );
				count++;
			}
		}
		WA_EntityResult( c, qtrue, va( "%i deleted", count ), -1 );
		return;
	}

	if ( id < 0 || id >= MAX_GENTITIES || !WA_ListedEntity( &g_entities[id] ) ) {
		WA_EntityResult( c, qfalse, "no such entity", id );
		return;
	}

	if ( !strcmp( op, "delete" ) ) {
		WA_FreeEntity( &g_entities[id] );
		WA_EntityResult( c, qtrue, "deleted", id );
	} else if ( !strcmp( op, "use" ) ) {
		// Fires the entity as a trigger_always would that has it as its target, with the world in the place of
		// the trigger: a door opens, a trigger fires its targets, a target does what it is there for. What only
		// works on the player who fired it (a teleporter, a give) does nothing then, the use functions of the game
		// all ask for a client before they reach into one. An NPC is left alone: a vehicle takes whoever uses it
		// on board without asking.
		gentity_t *ent = &g_entities[id];

		if ( ent->client || ent->s.eType == ET_NPC ) {
			WA_EntityResult( c, qfalse, "an NPC is not fired from here", id );
		} else if ( !ent->use ) {
			WA_EntityResult( c, qfalse, "this entity does nothing when it is fired", id );
		} else if ( ent->flags & FL_INACTIVE ) {
			WA_EntityResult( c, qfalse, "this entity is inactive, a target_activate has to wake it first", id );
		} else {
			GlobalUse( ent, &g_entities[ENTITYNUM_WORLD], &g_entities[ENTITYNUM_WORLD] );
			// it may be gone now (something that breaks, a trigger that fires once)
			WA_EntityResult( c, qtrue, "fired", ent->inuse ? id : -1 );
		}
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
		// an entity of the map that was edited is still one of the map: the next start of the map has it as it was
		entityPlaced[newId] = entityPlaced[id];
		WA_TakeTeamPlace( &g_entities[id], &g_entities[newId] );
		WA_FreeEntity( &g_entities[id] );
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

	if ( listenSocket == BG_INVALID_SOCKET )
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

File manager

Not in JKAdmin. Two folders and nothing else: the game folder in the home path of the server
(fs_homepath/<fs_game>: its logs, its stored config, what the panel saves; the engine looks there first), which
can be changed, and the game folder of the installation (fs_basepath/<fs_game>), which can only be read.

It works on the real files. The file system of the engine can not list a folder without what is in the pk3
files, and it can neither delete nor rename. So the limits the engine would set are set here:
- A path is checked letter by letter (WA_FsCleanPath): names of plain characters with / between them and none
  that begins with a dot. So there is no "..", no absolute path, no backslash and no drive letter.
- The system then resolves the path, and what comes out has to lie inside the folder (WA_FsResolve). A symbolic
  link is neither followed nor touched, so a link inside the folder does not lead out of it.
- Nothing is written, renamed or deleted that the engine or the system would run (WA_FsIsCode), the game modules
  above all: a module with another content would be loaded with the next map.
Every change and every file that is read goes to the security log with its path, never with its content.

==============================================================================
*/

#define WA_FS_MAX_PATH		160							// of a path inside a root, as the page names it
#define WA_FS_MAX_ENTRIES	4000						// of one folder
#define WA_FS_MAX_UPLOAD	( 512 * 1024 * 1024 )
#define WA_FS_PART			".part"						// a file that is still coming in

#ifdef _WIN32
	#define WA_FS_SEP( ch )	( ( ch ) == '/' || ( ch ) == '\\' )
#else
	#define WA_FS_SEP( ch )	( ( ch ) == '/' )
#endif

// A path as the page may name it, see above. "" is the root itself. out has room for WA_FS_MAX_PATH + 1.
static qboolean WA_FsCleanPath( const char *in, char *out, int outSize ) {
	qboolean	start = qtrue;		// at the first character of a name
	int			len = 0;

	if ( outSize <= WA_FS_MAX_PATH || strlen( in ) > WA_FS_MAX_PATH )
		return qfalse;
	for ( ; *in; in++ ) {
		const unsigned char ch = (unsigned char)*in;

		if ( ch == '/' ) {
			// no empty name, and none that ends with a dot or a blank (Windows drops those)
			if ( start || in[-1] == '.' || in[-1] == ' ' )
				return qfalse;
			start = qtrue;
		} else {
			if ( ch >= 0x80 || !( isalnum( ch ) || strchr( " _-.()+,", ch ) ) )
				return qfalse;
			if ( start && ( ch == '.' || ch == ' ' ) )
				return qfalse;
			start = qfalse;
		}
		out[len++] = (char)ch;
	}
	if ( len && ( start || out[len - 1] == '.' || out[len - 1] == ' ' ) )
		return qfalse;
	out[len] = '\0';
	return qtrue;
}

// What the engine or the system would run. An extension anywhere in the name counts (libx.so.1).
static qboolean WA_FsIsCode( const char *path ) {
	static const char *const code[] = { "so", "dll", "dylib", "exe", "qvm", "sh", "bat", "cmd", "com", "scr", "msi",
		"ps1", "vbs", "jar", "py", "pl", "php", "bin", "run", "appimage", "desktop" };
	const char *name = strrchr( path, '/' ), *dot;

	name = name ? name + 1 : path;
	for ( dot = strchr( name, '.' ); dot; dot = strchr( dot + 1, '.' ) ) {
		char	ext[16];
		int		n = 0;
		size_t	i;

		while ( dot[1 + n] && dot[1 + n] != '.' && n < (int)sizeof( ext ) - 1 ) {
			ext[n] = dot[1 + n];
			n++;
		}
		ext[n] = '\0';
		for ( i = 0; i < ARRAY_LEN( code ); i++ ) {
			if ( !Q_stricmp( ext, code[i] ) )
				return qtrue;
		}
	}
	return qfalse;
}

// the path as the system has it, without links and dots. qfalse: there is no such file.
static qboolean WA_FsRealPath( const char *path, char *out, int outSize ) {
	char real[MAX_OSPATH];
#ifdef _WIN32
	struct _stat st;

	if ( !_fullpath( real, path, sizeof( real ) ) || _stat( real, &st ) )
		return qfalse;
#else
	if ( !realpath( path, real ) )
		return qfalse;
#endif
	if ( (int)strlen( real ) >= outSize )
		return qfalse;
	Q_strncpyz( out, real, outSize );
	return qtrue;
}

static qboolean WA_FsIsLink( const char *path ) {
#ifdef _WIN32
	(void)path;
	return qfalse;		// junctions are not looked for
#else
	struct stat st;

	return (qboolean)( !lstat( path, &st ) && S_ISLNK( st.st_mode ) );
#endif
}

// qfalse: neither a file nor a folder. A link is looked at itself, not at what it points to.
static qboolean WA_FsStat( const char *path, qboolean *isDir, double *size, double *mtime ) {
#ifdef _WIN32
	struct _stat64 st;

	if ( _stat64( path, &st ) || !( st.st_mode & ( _S_IFDIR | _S_IFREG ) ) )
		return qfalse;
	*isDir = (qboolean)( ( st.st_mode & _S_IFDIR ) != 0 );
#else
	struct stat st;

	if ( lstat( path, &st ) || !( S_ISDIR( st.st_mode ) || S_ISREG( st.st_mode ) ) )
		return qfalse;
	*isDir = (qboolean)S_ISDIR( st.st_mode );
#endif
	if ( size )
		*size = (double)st.st_size;
	if ( mtime )
		*mtime = (double)st.st_mtime;
	return qtrue;
}

static qboolean WA_FsInside( const char *path, const char *root ) {
	const size_t n = strlen( root );

#ifdef _WIN32
	if ( Q_stricmpn( path, root, (int)n ) )
#else
	if ( strncmp( path, root, n ) )
#endif
		return qfalse;
	return (qboolean)( path[n] == '\0' || WA_FS_SEP( path[n] ) );
}

// the folder of a root as the system has it
static qboolean WA_FsRoot( qboolean install, char *out, int outSize ) {
	char base[MAX_OSPATH], game[MAX_QPATH], name[WA_FS_MAX_PATH + 1], path[MAX_OSPATH];

	trap->Cvar_VariableStringBuffer( install ? "fs_basepath" : "fs_homepath", base, sizeof( base ) );
	trap->Cvar_VariableStringBuffer( "fs_game", game, sizeof( game ) );
	if ( !game[0] )
		Q_strncpyz( game, BASEGAME, sizeof( game ) );
	// the game folder is one name
	if ( !base[0] || strchr( game, '/' ) || !WA_FsCleanPath( game, name, sizeof( name ) )
		|| strlen( base ) + strlen( name ) + 2 > sizeof( path ) )
		return qfalse;
	Com_sprintf( path, sizeof( path ), "%s/%s", base, name );
	return WA_FsRealPath( path, out, outSize );
}

/*
=================
WA_FsResolve

The real file of a path inside a root. rel has been through WA_FsCleanPath(). With mayCreate the file itself
need not be there, the folder it is to be in has to. exists says which it is.
qfalse: not there, a symbolic link, or not inside the root once the system has resolved it.
=================
*/
static qboolean WA_FsResolve( qboolean install, const char *rel, qboolean mayCreate, char *out, int outSize, qboolean *exists ) {
	char		root[MAX_OSPATH], full[MAX_OSPATH], real[MAX_OSPATH];
	char		*name;
	qboolean	isDir;

	*exists = qfalse;
	if ( !WA_FsRoot( install, root, sizeof( root ) ) || strlen( root ) + strlen( rel ) + 2 > sizeof( full ) )
		return qfalse;
	if ( !rel[0] ) {
		if ( (int)strlen( root ) >= outSize )
			return qfalse;
		*exists = qtrue;
		Q_strncpyz( out, root, outSize );
		return qtrue;
	}
	Com_sprintf( full, sizeof( full ), "%s/%s", root, rel );
	if ( WA_FsIsLink( full ) )
		return qfalse;
	if ( WA_FsRealPath( full, real, sizeof( real ) ) ) {
		if ( !WA_FsInside( real, root ) || (int)strlen( real ) >= outSize )
			return qfalse;
		*exists = qtrue;
		Q_strncpyz( out, real, outSize );
		return qtrue;
	}
	if ( !mayCreate )
		return qfalse;

	// not there yet: the folder it is to be in decides. The last / of full is the one in front of its name.
	name = strrchr( full, '/' );
	*name++ = '\0';
	if ( !WA_FsRealPath( full, real, sizeof( real ) ) || !WA_FsInside( real, root )
		|| !WA_FsStat( real, &isDir, NULL, NULL ) || !isDir
		|| (int)( strlen( real ) + strlen( name ) + 2 ) > outSize )
		return qfalse;
	Com_sprintf( out, outSize, "%s/%s", real, name );
	return qtrue;
}

// "root" and "path" of a request. qfalse: the path is not one the panel takes.
static qboolean WA_FsRequestPath( const char *query, const char *key, qboolean *install, char *rel, int relSize ) {
	char rootName[16], raw[1024];

	WA_FormValue( query, "root", rootName, sizeof( rootName ) );
	if ( install )
		*install = (qboolean)!strcmp( rootName, "install" );
	// WA_FormValue cuts a value that is longer than the buffer, and a path that lost its end is another path. What
	// is left of a value that long is still over 340 characters when decoded, so WA_FsCleanPath refuses it.
	WA_FormValue( query, key, raw, sizeof( raw ) );
	return WA_FsCleanPath( raw, rel, relSize );
}

static void WA_FsListEntry( waBuffer_t *b, const char *dir, const char *name, int *count ) {
	char		path[MAX_OSPATH], clean[WA_FS_MAX_PATH + 1];
	qboolean	isDir = qfalse, link;
	double		size = 0, mtime = 0;

	if ( !strcmp( name, "." ) || !strcmp( name, ".." ) || strlen( dir ) + strlen( name ) + 2 > sizeof( path ) )
		return;
	Com_sprintf( path, sizeof( path ), "%s/%s", dir, name );
	link = WA_FsIsLink( path );
	if ( !link && !WA_FsStat( path, &isDir, &size, &mtime ) )
		return;		// a device, a socket

	WA_BufAppend( b, *count ? ",{\"name\":" : "{\"name\":", *count ? 9 : 8 );
	WA_BufJsonString( b, name );
	// odd: a name the panel does not take in a request, it is only shown
	WA_BufPrintf( b, ",\"dir\":%s,\"size\":%.0f,\"time\":%.0f,\"link\":%s,\"code\":%s,\"odd\":%s}",
		WA_BOOL( isDir ), size, mtime, WA_BOOL( link ), WA_BOOL( !isDir && WA_FsIsCode( name ) ),
		WA_BOOL( strchr( name, '/' ) || !WA_FsCleanPath( name, clean, sizeof( clean ) ) ) );
	( *count )++;
}

// GET /api/fs/list?root=home|install&path=<folder>
static void WA_ApiFsList( waConnection_t *c, const char *query ) {
	char		rel[WA_FS_MAX_PATH + 1], dir[MAX_OSPATH];
	qboolean	install, exists, isDir;
	waBuffer_t	b = { 0 };
	int			count = 0;

	if ( !WA_FsRequestPath( query, "path", &install, rel, sizeof( rel ) )
		|| !WA_FsResolve( install, rel, qfalse, dir, sizeof( dir ), &exists )
		|| !WA_FsStat( dir, &isDir, NULL, NULL ) || !isDir ) {
		WA_ActionResult( c, qfalse, "no such folder" );
		return;
	}

	WA_BufPrintf( &b, "{\"ok\":true,\"root\":\"%s\",\"writable\":%s,\"path\":", install ? "install" : "home", WA_BOOL( !install ) );
	WA_BufJsonString( &b, rel );
	WA_BufAppend( &b, ",\"entries\":[", 12 );
	{
#ifdef _WIN32
		struct _finddata_t	fd;
		char				pattern[MAX_OSPATH];
		intptr_t			h;

		Com_sprintf( pattern, sizeof( pattern ), "%s/*", dir );
		h = _findfirst( pattern, &fd );
		if ( h != -1 ) {
			do {
				WA_FsListEntry( &b, dir, fd.name, &count );
			} while ( count < WA_FS_MAX_ENTRIES && !_findnext( h, &fd ) );
			_findclose( h );
		}
#else
		DIR				*d = opendir( dir );
		struct dirent	*e;

		while ( d && count < WA_FS_MAX_ENTRIES && ( e = readdir( d ) ) != NULL )
			WA_FsListEntry( &b, dir, e->d_name, &count );
		if ( d )
			closedir( d );
#endif
	}
	WA_BufAppend( &b, "]}", 2 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

// GET /api/fs/get?root=&path=<file>[&text=1]: the file as it is for a download, or as UTF-8 text for the editor
/*
=================
WebAdmin_Log, WA_ApiLog

Not in JKAdmin. The log of the game for the view of the page that follows it. The engine writes the file of the
log in blocks unless g_logSync is set, so its end is not on the disk yet when it is asked for: the panel keeps
the lines itself, as the game hands them over where it writes them (G_LogPrintf). The module is loaded anew
with every map, so the view of a new map starts with the end of the file, which the map before left complete.

GET /api/log?from=: the log from a place on. Without from, or with a place the panel does not have (any more):
its last part. next is where to ask from the next time.
=================
*/
#define WA_LOG_RING		65536		// bytes of the log the panel keeps
#define WA_LOG_CHUNK	32768		//	and gives in one answer
#define WA_LOG_TAIL		8192		// what a view starts with, and what is taken from the file when a map starts

static char		logRing[WA_LOG_RING];
static int		logLen;			// bytes in it
static double	logTotal;		// bytes that ever went into it: the place of what comes next
static qboolean	logPreloaded;

static void WA_LogAppend( const char *text, int len ) {
	if ( len <= 0 || len >= WA_LOG_RING / 2 )
		return;
	if ( logLen + len > WA_LOG_RING ) {
		// full: the older half goes
		memmove( logRing, logRing + logLen - WA_LOG_RING / 2, WA_LOG_RING / 2 );
		logLen = WA_LOG_RING / 2;
	}
	memcpy( logRing + logLen, text, len );
	logLen += len;
	logTotal += len;
}

// the end of the file of the log as the maps before left it, from the first whole line on
static void WA_LogPreload( void ) {
	static char	tail[WA_LOG_TAIL + 1];
	char		path[MAX_OSPATH], *line;
	qboolean	exists, isDir;
	double		size = 0;
	int			len = 0;
	FILE		*f;

	if ( logPreloaded )
		return;
	logPreloaded = qtrue;
	if ( !g_log.string[0] || !WA_FsResolve( qfalse, g_log.string, qfalse, path, sizeof( path ), &exists )
		|| !WA_FsStat( path, &isDir, &size, NULL ) || isDir )
		return;
	f = fopen( path, "rb" );
	if ( !f )
		return;
	if ( !fseek( f, size > WA_LOG_TAIL ? (long)size - WA_LOG_TAIL : 0, SEEK_SET ) )
		len = (int)fread( tail, 1, WA_LOG_TAIL, f );
	fclose( f );
	tail[len] = '\0';
	line = ( size > WA_LOG_TAIL ) ? strchr( tail, '\n' ) : tail;
	if ( line && line != tail )
		line++;
	if ( line )
		WA_LogAppend( line, (int)strlen( line ) );
}

void WebAdmin_Log( const char *line ) {
	// the panel is only there with a password, and the first lines of a map come before it listens
	if ( !g_webPassword.string[0] )
		return;
	WA_LogPreload();
	WA_LogAppend( line, (int)strlen( line ) );
}

static void WA_ApiLog( waConnection_t *c, const char *query ) {
	static char	data[WA_LOG_CHUNK + 1];
	char		value[32];
	double		start, from = -1;
	int			offset, len;
	waBuffer_t	b = { 0 };

	WA_LogPreload();
	start = logTotal - logLen;
	if ( WA_FormValue( query, "from", value, sizeof( value ) ) && value[0] )
		from = atof( value );
	if ( from < start || from > logTotal )
		from = ( logLen > WA_LOG_TAIL ) ? logTotal - WA_LOG_TAIL : start;

	offset = (int)( from - start );
	len = logLen - offset;
	if ( len > WA_LOG_CHUNK )
		len = WA_LOG_CHUNK;
	memcpy( data, logRing + offset, len );
	data[len] = '\0';

	WA_BufPrintf( &b, "{\"from\":%.0f,\"next\":%.0f,\"size\":%.0f,\"text\":", from, from + len, logTotal );
	WA_BufJsonString( &b, data );
	WA_BufAppend( &b, "}", 1 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

static void WA_ApiFsGet( waConnection_t *c, const char *query ) {
	char		rel[WA_FS_MAX_PATH + 1], path[MAX_OSPATH], flag[4], header[WA_FS_MAX_PATH + 128];
	qboolean	install, exists, isDir;
	double		size;
	FILE		*f;
	char		*data;
	int			len;

	if ( !WA_FsRequestPath( query, "path", &install, rel, sizeof( rel ) ) || !rel[0]
		|| !WA_FsResolve( install, rel, qfalse, path, sizeof( path ), &exists )
		|| !WA_FsStat( path, &isDir, &size, NULL ) || isDir ) {
		WA_RespondText( c, 404, "Not Found", "no such file" );
		return;
	}
	if ( size > WA_MAX_FILE ) {
		WA_RespondText( c, 413, "Payload Too Large", "the file is too large for the panel" );
		return;
	}
	len = (int)size;
	f = fopen( path, "rb" );
	data = f ? (char *)malloc( len + 1 ) : NULL;
	if ( !data || (int)fread( data, 1, len, f ) != len ) {
		if ( f )
			fclose( f );
		free( data );
		WA_RespondText( c, 500, "Internal Server Error", "could not read the file" );
		return;
	}
	fclose( f );
	WA_LogUse( c, va( "file read %s:%s", install ? "install" : "home", rel ) );

	WA_FormValue( query, "text", flag, sizeof( flag ) );
	if ( flag[0] == '1' ) {
		waBuffer_t b = { 0 };

		WA_BufLatin1AsUtf8( &b, data, len );
		WA_Respond( c, 200, "OK", "text/plain; charset=utf-8", "Cache-Control: no-store\r\n", b.data, b.len );
		WA_BufFree( &b );
	} else {
		const char *name = strrchr( rel, '/' );

		// the name has been through WA_FsCleanPath(): nothing in it ends the header or the quotes
		Com_sprintf( header, sizeof( header ), "Cache-Control: no-store\r\nContent-Disposition: attachment; filename=\"%s\"\r\n",
			name ? name + 1 : rel );
		WA_Respond( c, 200, "OK", "application/octet-stream", header, data, len );
	}
	free( data );
}

/*
=================
WA_ApiFsPut

POST /api/fs/put?path=<file>&offset=<n>[&last=1][&text=1], the body is the next piece of the file.

A request is 64 KB at most, so a file comes in pieces: each one names where it begins, and that has to be where
the one before ended. They collect in <file>.part, and the last one puts that in the place of the file, so a
file that is there is replaced in one go or not at all. text: the body is UTF-8 text of the editor.
Only into the home path.
=================
*/
static void WA_ApiFsPut( waConnection_t *c, const char *query, char *body, int bodyLen ) {
	char		rel[WA_FS_MAX_PATH + 1], partRel[WA_FS_MAX_PATH + 1], num[16], flag[4];
	char		path[MAX_OSPATH], part[MAX_OSPATH];
	qboolean	exists, partExists, isDir = qfalse, last;
	double		have = 0;
	int			offset;
	const char	*digit;
	FILE		*f;

	if ( !WA_FsRequestPath( query, "path", NULL, rel, sizeof( rel ) ) || !rel[0] ) {
		WA_ActionResult( c, qfalse, "not a file name the panel takes (letters, digits, blank and _-.()+, no name that begins with a dot)" );
		return;
	}
	if ( WA_FsIsCode( rel ) ) {
		WA_ActionResult( c, qfalse, "programs and game modules are not written through the panel" );
		return;
	}
	if ( strlen( rel ) + strlen( WA_FS_PART ) > WA_FS_MAX_PATH ) {
		WA_ActionResult( c, qfalse, "the name is too long" );
		return;
	}
	Com_sprintf( partRel, sizeof( partRel ), "%s" WA_FS_PART, rel );
	if ( !WA_FsResolve( qfalse, rel, qtrue, path, sizeof( path ), &exists )
		|| !WA_FsResolve( qfalse, partRel, qtrue, part, sizeof( part ), &partExists ) ) {
		WA_ActionResult( c, qfalse, "no such folder in the server folder" );
		return;
	}
	if ( ( exists && ( !WA_FsStat( path, &isDir, NULL, NULL ) || isDir ) )
		|| ( partExists && ( !WA_FsStat( part, &isDir, &have, NULL ) || isDir ) ) ) {
		WA_ActionResult( c, qfalse, "a folder has that name" );
		return;
	}

	// digits and nothing else, and no number an int can not hold
	WA_FormValue( query, "offset", num, sizeof( num ) );
	for ( offset = 0, digit = num; *digit; digit++ ) {
		if ( *digit < '0' || *digit > '9' || offset > WA_FS_MAX_UPLOAD / 10 ) {
			offset = -1;
			break;
		}
		offset = offset * 10 + ( *digit - '0' );
	}
	WA_FormValue( query, "last", flag, sizeof( flag ) );
	last = (qboolean)( flag[0] == '1' );
	WA_FormValue( query, "text", flag, sizeof( flag ) );
	if ( flag[0] == '1' )
		bodyLen = WA_TextBody( body );

	// a piece at 0 begins the file anew, any other one goes on where the pieces so far end
	if ( offset < 0 || ( offset > 0 && (double)offset != have ) ) {
		WA_ActionResult( c, qfalse, "this piece does not go on where the file stopped, begin again" );
		return;
	}
	if ( (double)offset + bodyLen > WA_FS_MAX_UPLOAD ) {
		WA_ActionResult( c, qfalse, "the file is too large for the panel" );
		return;
	}

	f = fopen( part, offset ? "ab" : "wb" );
	if ( !f || ( bodyLen > 0 && (int)fwrite( body, 1, bodyLen, f ) != bodyLen ) ) {
		if ( f )
			fclose( f );
		WA_ActionResult( c, qfalse, "could not write the file" );
		return;
	}
	if ( fclose( f ) ) {
		WA_ActionResult( c, qfalse, "could not write the file" );
		return;
	}
	if ( !last ) {
		WA_ActionResult( c, qtrue, "piece taken" );
		return;
	}

#ifdef _WIN32
	remove( path );		// rename() does not replace a file there
#endif
	if ( rename( part, path ) ) {
		WA_ActionResult( c, qfalse, "could not put the file in its place" );
		return;
	}
	WA_LogUse( c, va( "file written home:%s, %d bytes", rel, offset + bodyLen ) );
	WA_ActionResult( c, qtrue, "saved" );
}

// POST /api/fs/delete?path=<file or empty folder>, only in the home path
static void WA_ApiFsDelete( waConnection_t *c, const char *query ) {
	char		rel[WA_FS_MAX_PATH + 1], path[MAX_OSPATH];
	qboolean	exists, isDir;
	int			failed;

	if ( !WA_FsRequestPath( query, "path", NULL, rel, sizeof( rel ) ) || !rel[0]
		|| !WA_FsResolve( qfalse, rel, qfalse, path, sizeof( path ), &exists )
		|| !WA_FsStat( path, &isDir, NULL, NULL ) ) {
		WA_ActionResult( c, qfalse, "no such file in the server folder" );
		return;
	}
	if ( !isDir && WA_FsIsCode( rel ) ) {
		WA_ActionResult( c, qfalse, "programs and game modules are not deleted through the panel" );
		return;
	}
#ifdef _WIN32
	failed = isDir ? _rmdir( path ) : remove( path );
#else
	failed = isDir ? rmdir( path ) : remove( path );
#endif
	if ( failed ) {
		WA_ActionResult( c, qfalse, isDir ? "the folder is not empty" : "could not delete the file" );
		return;
	}
	WA_LogUse( c, va( "file deleted home:%s", rel ) );
	WA_ActionResult( c, qtrue, "deleted" );
}

// POST /api/fs/rename?path=<file or folder>&to=<new path>, only in the home path, never onto something that is there
static void WA_ApiFsRename( waConnection_t *c, const char *query ) {
	char		rel[WA_FS_MAX_PATH + 1], toRel[WA_FS_MAX_PATH + 1], path[MAX_OSPATH], to[MAX_OSPATH];
	qboolean	exists, toExists, isDir;

	if ( !WA_FsRequestPath( query, "path", NULL, rel, sizeof( rel ) ) || !rel[0]
		|| !WA_FsResolve( qfalse, rel, qfalse, path, sizeof( path ), &exists )
		|| !WA_FsStat( path, &isDir, NULL, NULL ) ) {
		WA_ActionResult( c, qfalse, "no such file in the server folder" );
		return;
	}
	if ( !WA_FsRequestPath( query, "to", NULL, toRel, sizeof( toRel ) ) || !toRel[0] ) {
		WA_ActionResult( c, qfalse, "not a name the panel takes (letters, digits, blank and _-.()+, no name that begins with a dot)" );
		return;
	}
	if ( WA_FsIsCode( rel ) || WA_FsIsCode( toRel ) ) {
		WA_ActionResult( c, qfalse, "programs and game modules are not renamed through the panel, and nothing is renamed into one" );
		return;
	}
	if ( !WA_FsResolve( qfalse, toRel, qtrue, to, sizeof( to ), &toExists ) ) {
		WA_ActionResult( c, qfalse, "no such folder in the server folder" );
		return;
	}
	if ( toExists ) {
		WA_ActionResult( c, qfalse, "there is something with that name already" );
		return;
	}
	if ( rename( path, to ) ) {
		WA_ActionResult( c, qfalse, "could not rename" );
		return;
	}
	WA_LogUse( c, va( "file renamed home:%s to %s", rel, toRel ) );
	WA_ActionResult( c, qtrue, "renamed" );
}

// POST /api/fs/mkdir?path=<new folder>, only in the home path
static void WA_ApiFsMkdir( waConnection_t *c, const char *query ) {
	char		rel[WA_FS_MAX_PATH + 1], path[MAX_OSPATH];
	qboolean	exists;

	if ( !WA_FsRequestPath( query, "path", NULL, rel, sizeof( rel ) ) || !rel[0] ) {
		WA_ActionResult( c, qfalse, "not a name the panel takes (letters, digits, blank and _-.()+, no name that begins with a dot)" );
		return;
	}
	if ( !WA_FsResolve( qfalse, rel, qtrue, path, sizeof( path ), &exists ) ) {
		WA_ActionResult( c, qfalse, "no such folder in the server folder" );
		return;
	}
	if ( exists ) {
		WA_ActionResult( c, qfalse, "there is something with that name already" );
		return;
	}
#ifdef _WIN32
	if ( _mkdir( path ) ) {
#else
	if ( mkdir( path, 0755 ) ) {
#endif
		WA_ActionResult( c, qfalse, "could not make the folder" );
		return;
	}
	WA_LogUse( c, va( "folder made home:%s", rel ) );
	WA_ActionResult( c, qtrue, "folder made" );
}

/*
==============================================================================

Map reload

Not in JKAdmin. The engine looks through its folders for pk3 files whenever a map starts (FS_Restart in
SV_SpawnServer) and at no other time, so starting the running map again is what makes it find a pk3 that was
uploaded. The players stay connected.

A start of the map also removes what was spawned through the panel, and the module is loaded anew with
nothing it remembered. So the placed entities are written to a file first, with the keys they were spawned
with and the place they are at now, and the new map spawns them again and removes the file.

==============================================================================
*/

#define WA_RELOAD_FILE		"webadmin_reload.dat"
#define WA_RELOAD_DELAY		1500		// msec after the start of the map

static void WA_ReloadMap( waConnection_t *c ) {
	waBuffer_t	b = { 0 };
	char		mapname[MAX_QPATH];
	int			i, count = 0;

	trap->Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
	Q_strstrip( mapname, "\n\r\";", NULL );
	if ( !mapname[0] ) {
		WA_ActionResult( c, qfalse, "no map is running" );
		return;
	}

	// the first line names the map, then a block of key=value lines for each entity
	WA_BufPrintf( &b, "map=%s\n", mapname );
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		const gentity_t	*ent = &g_entities[i];
		const char		*key, *end;

		if ( !entityPlaced[i] || !entityVars[i] || !WA_ListedEntity( ent ) )
			continue;
		WA_BufAppend( &b, "{\n", 2 );
		for ( key = entityVars[i], end = key + entityVarsLen[i]; key < end; ) {
			const char *value = key + strlen( key ) + 1;

			if ( !Q_stricmp( key, "origin" ) && !ent->r.bmodel ) {
				// where it is now, not where it was put: a ball rolls
				WA_BufPrintf( &b, "origin=%.1f %.1f %.1f\n", ent->r.currentOrigin[0], ent->r.currentOrigin[1], ent->r.currentOrigin[2] );
			} else {
				WA_BufPrintf( &b, "%s=%s\n", key, value );
			}
			key = value + strlen( value ) + 1;
		}
		WA_BufAppend( &b, "}\n", 2 );
		count++;
	}
	if ( !WA_WriteGameFile( WA_RELOAD_FILE, b.data, b.len ) ) {
		WA_BufFree( &b );
		WA_ActionResult( c, qfalse, "could not write " WA_RELOAD_FILE ", the map is not started again" );
		return;
	}
	WA_BufFree( &b );

	// devmap keeps the cheats on if they are
	trap->SendConsoleCommand( EXEC_APPEND, va( "%s \"%s\"\n", trap->Cvar_VariableIntegerValue( "sv_cheats" ) ? "devmap" : "map", mapname ) );
	WA_ActionResult( c, qtrue, va( "starting %s again, %d placed objects come back", mapname, count ) );
}

// At the start of a map: takes what WA_ReloadMap() left. The file is for the start that follows it and for no
// other, so it is removed whatever is in it.
static void WA_ReloadRead( void ) {
	char		path[MAX_OSPATH], mapname[MAX_QPATH];
	qboolean	exists, isDir;
	double		size;
	FILE		*f;
	char		*text, *body;
	int			len;

	free( reloadText );
	reloadText = NULL;
	if ( !WA_FsResolve( qfalse, WA_RELOAD_FILE, qfalse, path, sizeof( path ), &exists )
		|| !WA_FsStat( path, &isDir, &size, NULL ) || isDir )
		return;
	len = ( size < 4 * 1024 * 1024 ) ? (int)size : 0;
	f = fopen( path, "rb" );
	text = f ? (char *)malloc( len + 1 ) : NULL;
	if ( text )
		len = (int)fread( text, 1, len, f );
	if ( f )
		fclose( f );
	remove( path );
	if ( !text )
		return;
	text[len] = '\0';

	trap->Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
	body = strchr( text, '\n' );
	if ( !body || strncmp( text, "map=", 4 ) || (int)( body - text ) - 4 != (int)strlen( mapname )
		|| strncmp( text + 4, mapname, strlen( mapname ) ) ) {
		free( text );		// it was left for another map
		return;
	}
	memmove( text, body + 1, strlen( body + 1 ) + 1 );
	reloadText = text;
}

// spawns what WA_ReloadRead() took, when the map has run for a moment
static void WA_ReloadSpawn( void ) {
	char	*block = reloadText, err[128];
	int		count = 0, lost = 0;

	while ( ( block = strstr( block, "{\n" ) ) != NULL ) {
		char *end = strstr( block, "\n}\n" );

		if ( !end )
			break;
		end[1] = '\0';
		{
			const int id = WA_SpawnFromText( block + 2, err, sizeof( err ) );

			if ( id >= 0 ) {
				entityPlaced[id] = qtrue;
				count++;
			} else {
				lost++;
			}
		}
		block = end + 3;
	}
	free( reloadText );
	reloadText = NULL;
	trap->Print( "Web admin: %d placed objects are back after the map reload%s\n", count, lost ? va( ", %d did not spawn", lost ) : "" );
}

/*
==============================================================================

Model list

Not in JKAdmin: every md3 and glm under models/, for the tab that places them as misc_prop.

The file system of the engine lists one folder at a call and goes through all pk3 files for it. The models are
in a few hundred folders, so that is some hundred calls, and they are not made in one frame: WebAdmin_Frame()
takes folders for a few milliseconds a frame until all are done, and the page asks again until the list is
complete. The list stays until the module is loaded anew, which is when the pk3 files may have changed.

==============================================================================
*/

#define WA_MODEL_MSEC		3		// of a server frame for the search
#define WA_MODEL_MAX		8000	// files
#define WA_MODEL_DEPTH		6		// folders below models/

static waBuffer_t	modelQueue;		// the folders that were found, one after the other with a 0 after each
static int			modelQueueNext;	// where the next one to look into begins
static waBuffer_t	modelNames;		// the files so far: JSON strings with commas between them
static int			modelCount;
static int			modelScan;		// 0: not begun, 1: running, 2: complete

// The engine names a file once for every place it finds it in, and a folder with a / at its end from a pk3 and
// without one from the disk. qtrue: name stands in the list before this place already.
static qboolean WA_ModelListedBefore( const char *list, const char *name ) {
	const char *p;

	for ( p = list; p < name; p += strlen( p ) + 1 ) {
		if ( !Q_stricmp( p, name ) )
			return qtrue;
	}
	return qfalse;
}

// qtrue: the folder is in the queue, looked into or not
static qboolean WA_ModelQueued( const char *path ) {
	const char *p, *end = modelQueue.data + modelQueue.len;

	for ( p = modelQueue.data; p && p < end; p += strlen( p ) + 1 ) {
		if ( !Q_stricmp( p, path ) )
			return qtrue;
	}
	return qfalse;
}

static void WA_ModelScanFolder( const char *dir ) {
	static const char *const	extensions[] = { ".md3", ".glm" };
	static char					list[32768];
	char						path[MAX_QPATH];
	const char					*name;
	int							i, n, depth = 0;
	size_t						e;

	for ( name = dir; *name; name++ ) {
		if ( *name == '/' )
			depth++;
	}
	if ( depth < WA_MODEL_DEPTH ) {
		n = trap->FS_GetFileList( dir, "/", list, sizeof( list ) );
		for ( i = 0, name = list; i < n; i++, name += strlen( name ) + 1 ) {
			int len = (int)strlen( name );

			// the engine names a folder with or without a / at its end, and itself and the one above among them
			if ( len && name[len - 1] == '/' )
				len--;
			if ( !len || name[0] == '.' || strlen( dir ) + len + 2 > sizeof( path ) )
				continue;
			Com_sprintf( path, sizeof( path ), "%s/%.*s", dir, len, name );
			if ( !WA_ModelQueued( path ) )
				WA_BufAppend( &modelQueue, path, (int)strlen( path ) + 1 );
		}
	}
	for ( e = 0; e < ARRAY_LEN( extensions ); e++ ) {
		n = trap->FS_GetFileList( dir, extensions[e], list, sizeof( list ) );
		for ( i = 0, name = list; i < n && modelCount < WA_MODEL_MAX; i++, name += strlen( name ) + 1 ) {
			// The engine also names the files of the folders one and two levels further down, with the folder in
			// front of the name. Those are taken when their own folder is looked into, or they would be here thrice.
			if ( !name[0] || strchr( name, '/' ) || strlen( dir ) + strlen( name ) + 2 > sizeof( path )
				|| WA_ModelListedBefore( list, name ) )
				continue;
			Com_sprintf( path, sizeof( path ), "%s/%s", dir, name );
			if ( modelCount )
				WA_BufAppend( &modelNames, ",", 1 );
			WA_BufJsonString( &modelNames, path );
			modelCount++;
		}
	}
}

// once a frame while the search runs
static void WA_ModelScanFrame( void ) {
	const int start = trap->Milliseconds();

	while ( modelScan == 1 ) {
		char dir[MAX_QPATH];

		// the queue is kept to the end, a folder that was looked into must not come in again
		if ( modelQueueNext >= modelQueue.len ) {
			modelScan = 2;
			WA_BufFree( &modelQueue );
			modelQueueNext = 0;
			break;
		}
		// a copy: what is found is put at the end of the queue, which may move it
		Q_strncpyz( dir, modelQueue.data + modelQueueNext, sizeof( dir ) );
		modelQueueNext += (int)strlen( modelQueue.data + modelQueueNext ) + 1;
		WA_ModelScanFolder( dir );
		if ( trap->Milliseconds() - start >= WA_MODEL_MSEC )
			break;
	}
}

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

static void WA_ApiWaypoints( waConnection_t *c ) {
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
static void WA_ApiWaypoint( waConnection_t *c, const char *body ) {
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

// GET /api/bans: the addresses that are banned, as addip and removeip take them
static void WA_ApiBans( waConnection_t *c ) {
	waBuffer_t	b = { 0 };
	char		address[32];
	qboolean	first = qtrue;
	int			i;

	WA_BufAppend( &b, "{\"bans\":[", 9 );
	for ( i = 0; i < G_BanListSize(); i++ ) {
		if ( !G_BanListEntry( i, address, sizeof( address ) ) )
			continue;
		if ( !first )
			WA_BufAppend( &b, ",", 1 );
		first = qfalse;
		WA_BufJsonString( &b, address );
	}
	WA_BufAppend( &b, "]}", 2 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

extern char NPCParms[];		// NPC_stats.c: the text of every .npc file

/*
=================
WA_ApiList

GET /api/list?kind=: names for the fields of the page to offer, so that a value does not have to be known by
heart. maps, effects and music are the files of the game, npcs the types of the .npc files, bots the bots the
server knows.
=================
*/
static void WA_ApiList( waConnection_t *c, const char *query ) {
	// strip: the name goes without its ending. prefix: what the key of an entity wants in front of it
	static const struct { const char *kind, *dir, *ext, *prefix; qboolean strip; } folders[] = {
		{ "maps", "maps", ".bsp", "", qtrue },
		{ "effects", "effects", ".efx", "", qtrue },
		{ "music", "music", ".mp3", "music/", qfalse },
	};
	static char	list[65536];
	char		kind[16], name[MAX_QPATH];
	waBuffer_t	b = { 0 };
	qboolean	first = qtrue;
	size_t		f;
	int			i, n;

	WA_FormValue( query, "kind", kind, sizeof( kind ) );
	WA_BufAppend( &b, "{\"names\":[", 10 );

	for ( f = 0; f < ARRAY_LEN( folders ); f++ ) {
		const char *file;

		if ( strcmp( kind, folders[f].kind ) )
			continue;
		// the engine names the files of the folders below too, with the folder in front
		n = trap->FS_GetFileList( folders[f].dir, folders[f].ext, list, sizeof( list ) );
		for ( i = 0, file = list; i < n; i++, file += strlen( file ) + 1 ) {
			if ( !file[0] || WA_ModelListedBefore( list, file ) )
				continue;
			Com_sprintf( name, sizeof( name ), "%s%s", folders[f].prefix, file );
			if ( folders[f].strip )
				COM_StripExtension( name, name, sizeof( name ) );
			if ( !first )
				WA_BufAppend( &b, ",", 1 );
			first = qfalse;
			WA_BufJsonString( &b, name );
		}
	}

	// The files and folders of one folder of the game, for a field that takes a path: dir is the folder, ext
	// the endings that are wanted (".wav,.mp3"). Only below the folders the editor has a use for.
	if ( !strcmp( kind, "dir" ) ) {
		static const char *const roots[] = { "sound", "models", "music", "effects", "maps" };
		char		dir[MAX_QPATH], exts[64], *ext, *next;
		const char	*file;
		qboolean	allowed = qfalse;

		WA_FormValue( query, "dir", dir, sizeof( dir ) );
		WA_FormValue( query, "ext", exts, sizeof( exts ) );
		i = (int)strlen( dir );
		while ( i > 0 && dir[i - 1] == '/' )
			dir[--i] = '\0';
		for ( f = 0; f < ARRAY_LEN( roots ); f++ ) {
			const size_t len = strlen( roots[f] );

			if ( !Q_stricmpn( dir, roots[f], (int)len ) && ( dir[len] == '\0' || dir[len] == '/' ) )
				allowed = qtrue;
		}
		if ( allowed && !strstr( dir, ".." ) && !strchr( dir, '\\' ) && !strchr( dir, ':' ) ) {
			// the folders in it, named with a / at their end
			n = trap->FS_GetFileList( dir, "/", list, sizeof( list ) );
			for ( i = 0, file = list; i < n; i++, file += strlen( file ) + 1 ) {
				int len = (int)strlen( file );

				if ( len && file[len - 1] == '/' )
					len--;
				if ( !len || file[0] == '.' || WA_ModelListedBefore( list, file ) )
					continue;
				Com_sprintf( name, sizeof( name ), "%s/%.*s/", dir, len, file );
				if ( !first )
					WA_BufAppend( &b, ",", 1 );
				first = qfalse;
				WA_BufJsonString( &b, name );
			}
			// and its files: the engine names the ones of the folders below too, those are left out
			for ( ext = exts; ext && ext[0]; ext = next ) {
				next = strchr( ext, ',' );
				if ( next )
					*next++ = '\0';
				n = trap->FS_GetFileList( dir, ext, list, sizeof( list ) );
				for ( i = 0, file = list; i < n; i++, file += strlen( file ) + 1 ) {
					if ( !file[0] || strchr( file, '/' ) || WA_ModelListedBefore( list, file ) )
						continue;
					Com_sprintf( name, sizeof( name ), "%s/%s", dir, file );
					if ( !first )
						WA_BufAppend( &b, ",", 1 );
					first = qfalse;
					WA_BufJsonString( &b, name );
				}
			}
		}
	}

	if ( !strcmp( kind, "npcs" ) ) {
		// a .npc file is a name and its block, one after the other
		const char *p = NPCParms;

		while ( p ) {
			const char *token = COM_ParseExt( &p, qtrue );

			if ( !token[0] )
				break;
			Q_strncpyz( name, token, sizeof( name ) );
			if ( SkipBracedSection( &p, 0 ) && name[0] != '{' ) {
				if ( !first )
					WA_BufAppend( &b, ",", 1 );
				first = qfalse;
				WA_BufJsonString( &b, name );
			}
		}
	} else if ( !strcmp( kind, "bots" ) ) {
		for ( i = 0; i < level.bots.num; i++ ) {
			if ( !first )
				WA_BufAppend( &b, ",", 1 );
			first = qfalse;
			WA_BufJsonString( &b, Info_ValueForKey( G_GetBotInfoByNumber( i ), "name" ) );
		}
	}

	WA_BufAppend( &b, "]}", 2 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

// GET /api/classdoc: the entity definitions of the game code, every QUAKED comment of its source files as it
// stands there (cmake/Modules/ExtractQuaked.cmake). The editor of the page reads the classes from it: what one
// does, its keys, the names of its spawnflags, its box and its colour.
static void WA_ApiClassDoc( waConnection_t *c ) {
	WA_Respond( c, 200, "OK", "text/plain; charset=utf-8", "Cache-Control: max-age=3600\r\n", (const char *)classdoc_txt, (int)classdoc_txt_len );
}

// GET /api/shaders: the names of the shader scripts of the game, in the order the engine reads them in. The page
// reads the files themselves through /api/file.
static void WA_ApiShaders( waConnection_t *c ) {
	static char	list[32768];
	waBuffer_t	b = { 0 };
	const char	*name;
	qboolean	first = qtrue;
	int			i, n;

	n = trap->FS_GetFileList( "shaders", ".shader", list, sizeof( list ) );
	WA_BufAppend( &b, "{\"files\":[", 10 );
	for ( i = 0, name = list; i < n; i++, name += strlen( name ) + 1 ) {
		// the engine reads the ones of this folder only, and each name once
		if ( !name[0] || strchr( name, '/' ) || WA_ModelListedBefore( list, name ) )
			continue;
		if ( !first )
			WA_BufAppend( &b, ",", 1 );
		first = qfalse;
		WA_BufJsonString( &b, name );
	}
	WA_BufAppend( &b, "]}", 2 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

// GET /api/models: the list as far as it is, done says whether that is all
static void WA_ApiModels( waConnection_t *c ) {
	waBuffer_t b = { 0 };

	if ( !modelScan ) {
		modelScan = 1;
		WA_BufAppend( &modelQueue, "models", 7 );
	}
	WA_BufPrintf( &b, "{\"done\":%s,\"count\":%d,\"models\":[", WA_BOOL( modelScan == 2 ), modelCount );
	if ( modelNames.len )
		WA_BufAppend( &b, modelNames.data, modelNames.len );
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
	if ( c->sock != BG_INVALID_SOCKET )
		BG_CloseSocket( c->sock );
	WA_BufFree( &c->response );
	memset( c, 0, sizeof( *c ) );
	c->sock = BG_INVALID_SOCKET;
}

// Handles a complete request. The response goes into c->response.
static void WA_HandleRequest( waConnection_t *c, char *head, char *body, int bodyLen ) {
	char	method[8], path[4096];
	char	*query;

	if ( sscanf( head, "%7s %4095s", method, path ) != 2 ) {
		WA_RespondText( c, 400, "Bad Request", "bad request" );
		return;
	}
	// an address that fills the buffer was cut, and half a file name is another file name
	if ( strlen( path ) >= sizeof( path ) - 1 ) {
		WA_RespondText( c, 414, "URI Too Long", "address too long" );
		return;
	}

	if ( WA_AuthThrottle( c->addr ) ) {
		WA_Respond( c, 429, "Too Many Requests", "text/plain", "Retry-After: 30\r\n", "too many wrong passwords, wait", 30 );
		return;
	}
	if ( !WA_CheckAuth( head ) ) {
		const qboolean tried = ( WA_FindHeader( head, "Authorization" ) != NULL );

		// only count a wrong password, not the first request a browser sends without one
		if ( tried ) {
			WA_AuthResult( c->addr, qfalse );
			WA_LogUse( c, "wrong password" );
		}
		WA_Respond( c, 401, "Unauthorized", "text/plain", "WWW-Authenticate: Basic realm=\"VisionJK web admin\"\r\n",
			"login required", 14 );
		return;
	}
	WA_AuthResult( c->addr, qtrue );

	if ( !strcmp( method, "POST" ) && !WA_SameOrigin( head ) ) {
		WA_RespondText( c, 403, "Forbidden", "request from another site refused" );
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
		WA_ApiExport( c, query );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/chat" ) ) {
		WA_ApiChat( c, query );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/entity" ) ) {
		WA_ApiEntity( c, body );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/config" ) ) {
		WA_ApiConfigGet( c );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/config" ) ) {
		char run[4];

		WA_FormValue( query, "exec", run, sizeof( run ) );
		WA_ApiConfigSet( c, body, (qboolean)( run[0] == '1' ) );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/models" ) ) {
		WA_ApiModels( c );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/shaders" ) ) {
		WA_ApiShaders( c );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/classdoc" ) ) {
		WA_ApiClassDoc( c );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/list" ) ) {
		WA_ApiList( c, query );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/bans" ) ) {
		WA_ApiBans( c );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/waypoints" ) ) {
		WA_ApiWaypoints( c );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/waypoint" ) ) {
		WA_ApiWaypoint( c, body );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/fs/list" ) ) {
		WA_ApiFsList( c, query );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/log" ) ) {
		WA_ApiLog( c, query );
	} else if ( !strcmp( method, "GET" ) && !strcmp( path, "/api/fs/get" ) ) {
		WA_ApiFsGet( c, query );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/fs/put" ) ) {
		WA_ApiFsPut( c, query, body, bodyLen );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/fs/delete" ) ) {
		WA_ApiFsDelete( c, query );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/fs/rename" ) ) {
		WA_ApiFsRename( c, query );
	} else if ( !strcmp( method, "POST" ) && !strcmp( path, "/api/fs/mkdir" ) ) {
		WA_ApiFsMkdir( c, query );
	} else {
		WA_RespondText( c, 404, "Not Found", "not found" );
	}
}

// A Content-Length: only digits, and nothing a request can not be. JKAdmin read it with atoi and then checked
// headLen + bodyLen, which wraps for a length near INT_MAX: the check passed and the terminating zero of the
// body was written about 2 GB in front of the request buffer, before any login. Returns -1 for a bad value.
static int WA_ParseLength( const char *s ) {
	int len = 0, digits = 0;

	for ( ; *s >= '0' && *s <= '9'; s++, digits++ ) {
		len = len * 10 + ( *s - '0' );
		if ( len > WA_MAX_REQUEST )
			return -1;
	}
	while ( *s == ' ' || *s == '\t' )
		s++;
	if ( !digits || ( *s && *s != '\r' && *s != '\n' ) )
		return -1;
	return len;
}

static void WA_ReadRequest( waConnection_t *c ) {
	char		*headEnd;
	const char	*lenHeader;
	int			received, bodyLen = 0, headLen;

	received = recv( c->sock, c->request + c->requestLen, WA_MAX_REQUEST - c->requestLen, 0 );
	if ( received == 0 || ( received < 0 && !BG_SocketWouldBlock() ) ) {
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
		bodyLen = WA_ParseLength( lenHeader );
	*headEnd = '\r';

	// (headLen is at most WA_MAX_REQUEST, the subtraction can not wrap)
	if ( bodyLen < 0 || bodyLen > WA_MAX_REQUEST - headLen ) {
		WA_CloseConnection( c );
		return;
	}
	if ( c->requestLen < headLen + bodyLen )
		return;		// body still coming

	*headEnd = '\0';
	c->request[headLen + bodyLen] = '\0';
	WA_HandleRequest( c, c->request, c->request + headLen, bodyLen );
}

static void WA_WriteResponse( waConnection_t *c ) {
	while ( c->sent < c->response.len ) {
		const int n = send( c->sock, c->response.data + c->sent, c->response.len - c->sent, BG_SEND_FLAGS );
		if ( n < 0 ) {
			if ( !BG_SocketWouldBlock() )
				WA_CloseConnection( c );
			return;
		}
		c->sent += n;
		c->lastActive = trap->Milliseconds();
	}
	WA_CloseConnection( c );
}

static void WA_Accept( void ) {
	for ( ;; ) {
		struct sockaddr_in	from;
		socklen_t			fromLen = sizeof( from );
		bgSocket_t			s = accept( listenSocket, (struct sockaddr *)&from, &fromLen );
		int					i;

		if ( s == BG_INVALID_SOCKET )
			return;

		BG_SocketNonBlocking( s );
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
			BG_CloseSocket( s );
			continue;
		}

		memset( &connections[i], 0, sizeof( connections[i] ) );
		connections[i].inUse = qtrue;
		connections[i].sock = s;
		connections[i].addr = (unsigned int)from.sin_addr.s_addr;	// network order, only compared and printed
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
		connections[i].sock = BG_INVALID_SOCKET;

	// before anything can keep the panel off: the file of a reload must not wait for a later start of this map
	WA_ReloadRead();

	if ( !g_webPassword.string[0] ) {
		if ( g_webPort.integer > 0 )
			trap->Print( "Web admin is off, set g_webPassword to start it\n" );
		return;
	}
	if ( g_webPort.integer <= 0 || g_webPort.integer > 65535 )
		return;

	BG_NetStart();

	listenSocket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	if ( listenSocket == BG_INVALID_SOCKET ) {
		trap->Print( S_COLOR_YELLOW "Web admin: could not create a socket\n" );
		return;
	}
	setsockopt( listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof( on ) );

	memset( &addr, 0, sizeof( addr ) );
	addr.sin_family = AF_INET;
	addr.sin_port = htons( (unsigned short)g_webPort.integer );
	// default to localhost, not every interface: the panel is plain HTTP, reaching it from outside should be a
	// choice (g_webBind 0.0.0.0, or a reverse proxy). An unset or unparseable g_webBind binds to 127.0.0.1.
	if ( !g_webBind.string[0] || inet_pton( AF_INET, g_webBind.string, &addr.sin_addr ) != 1 )
		inet_pton( AF_INET, "127.0.0.1", &addr.sin_addr );

	if ( bind( listenSocket, (struct sockaddr *)&addr, sizeof( addr ) ) != 0 || listen( listenSocket, 16 ) != 0 ) {
		trap->Print( S_COLOR_YELLOW "Web admin: could not listen on %s:%d (port in use, or below 1024 without root?)\n",
			g_webBind.string[0] ? g_webBind.string : "0.0.0.0", g_webPort.integer );
		BG_CloseSocket( listenSocket );
		listenSocket = BG_INVALID_SOCKET;
		return;
	}

	BG_SocketNonBlocking( listenSocket );
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

	if ( reloadText && level.time - level.startTime >= WA_RELOAD_DELAY )
		WA_ReloadSpawn();

	if ( listenSocket == BG_INVALID_SOCKET )
		return;

	if ( modelScan == 1 )
		WA_ModelScanFrame();

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
	if ( listenSocket != BG_INVALID_SOCKET ) {
		BG_CloseSocket( listenSocket );
		listenSocket = BG_INVALID_SOCKET;
	}

	for ( i = 0; i < MAX_GENTITIES; i++ )
		WebAdmin_EntityFreed( i );

	free( mapData );
	mapData = NULL;
	mapLen = 0;
	mapDataName[0] = '\0';
	free( reloadText );
	reloadText = NULL;
	WA_BufFree( &modelQueue );
	WA_BufFree( &modelNames );
	modelQueueNext = modelCount = modelScan = 0;
}
