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

// sv_web_tv.cpp -- the viewers of the TV of the server (issue #7): GET /tv becomes a WebSocket that the server sends
// on, and every binary message on it is one record of the demo of the TV (sv_tv.cpp): the gamestate first, then a
// snapshot each frame of the server. A viewer gets the gamestate when it comes and when the level changes, and the
// snapshots from the first whole one on; one that could not take a snapshot (its buffer was full) waits for the
// next whole one, which the TV then makes. Not from OpenJK, see docs/japlus/server.md, "The TV of the server".
//
// No login: what the TV shows is what any spectator of the server sees. vs_tv 0 turns it off. Behind a reverse
// proxy (nginx in front of jka.vision, vs_clientWebProxy) every viewer comes from the proxy, which names the real
// one in X-Real-IP: the limit per address counts that one.

#include "server/web/sv_web_local.h"

#define SV_WEB_MAX_TV			32			// viewers at once
#define SV_WEB_MAX_ADDRESS_TV	4			// and of them from one address

static cvar_t *vs_tv;

static qboolean SV_WebTVViewer( const webConnection_t *c ) {
	return (qboolean)( c->tv && c->state == WEB_SOCKET && !c->drop );
}

// viewers, of one address or of all (ip NULL)
static int SV_WebTVCount( const byte *ip ) {
	int i, n = 0;

	for ( i = 0; i < SV_WEB_MAX_CONNECTIONS; i++ ) {
		const webConnection_t *c = SV_WebConnection( i );

		if ( SV_WebTVViewer( c ) && ( !ip || !memcmp( c->tvIp, ip, sizeof( c->tvIp ) ) ) ) {
			n++;
		}
	}
	return n;
}

int SV_WebTVViewers( void ) {
	return SV_WebTVCount( NULL );
}

// a viewer waits for a whole snapshot (sv_tv.cpp makes the next one whole then)
qboolean SV_WebTVWaiting( void ) {
	int i;

	for ( i = 0; i < SV_WEB_MAX_CONNECTIONS; i++ ) {
		const webConnection_t *c = SV_WebConnection( i );

		if ( SV_WebTVViewer( c ) && !c->tvSynced ) {
			return qtrue;
		}
	}
	return qfalse;
}

// the level changed or the TV began anew: every viewer gets the gamestate again
void SV_WebTVResync( void ) {
	int i;

	for ( i = 0; i < SV_WEB_MAX_CONNECTIONS; i++ ) {
		webConnection_t *c = SV_WebConnection( i );

		if ( SV_WebTVViewer( c ) ) {
			c->tvGreet = qtrue;
			c->tvSynced = qfalse;
		}
	}
}

// the gamestate for the viewers that are to get it, before the snapshot of this frame
void SV_WebTVGreet( void ) {
	static byte	record[MAX_MSGLEN + 8];
	int			len = 0, i;

	for ( i = 0; i < SV_WEB_MAX_CONNECTIONS; i++ ) {
		webConnection_t *c = SV_WebConnection( i );

		if ( !SV_WebTVViewer( c ) || !c->tvGreet ) {
			continue;
		}
		if ( !len && !( len = SV_TVGamestate( record, sizeof( record ) ) ) ) {
			return;
		}
		c->tvSynced = qfalse;
		if ( !SV_WebSocketSend( c, WEB_OP_BINARY, record, len ) ) {
			// what went before is still on its way: again in the next frame; with nothing on its way the gamestate
			// is more than its buffer takes, and it can not watch
			if ( c->outLen == c->outSent ) {
				SV_WebSocketEnd( c, WEB_CLOSE_TOO_BIG );
			}
			continue;
		}
		c->tvGreet = qfalse;
		SV_WebFlush( c );
	}
}

// a record of a snapshot for every viewer that is in step, or that waits and this one is whole
void SV_WebTVSend( const byte *record, int len, qboolean full ) {
	int i;

	for ( i = 0; i < SV_WEB_MAX_CONNECTIONS; i++ ) {
		webConnection_t *c = SV_WebConnection( i );

		if ( !SV_WebTVViewer( c ) || c->tvGreet || ( !c->tvSynced && !full ) ) {
			continue;
		}
		// (its buffer is full: it gets nothing until the next whole snapshot, which follows at once)
		c->tvSynced = SV_WebSocketSend( c, WEB_OP_BINARY, record, len );
		SV_WebFlush( c );
	}
}

// is the peer one of the proxies of vs_clientWebProxy (addresses split by blanks or commas)?
static qboolean SV_WebTVProxy( const netadr_t *adr ) {
	const char	*s = vs_clientWebProxy->string;
	char		word[16];
	byte		ip[4];

	while ( *s ) {
		const size_t len = strcspn( s, " ,\t" );

		if ( len && len < sizeof( word ) ) {
			Q_strncpyz( word, s, (int)len + 1 );
			if ( Q_ParseIPv4( word, ip ) && !memcmp( ip, adr->ip, sizeof( ip ) ) ) {
				return qtrue;
			}
		}
		s += len;
		s += strspn( s, " ,\t" );
	}
	return qfalse;
}

// who watches: the peer, or the viewer a proxy names. A proxy that names none (or none that is an IPv4 address)
// counts as the viewer itself.
static void SV_WebTVFrom( webConnection_t *c, const char *head ) {
	char value[64];

	memcpy( c->tvIp, c->adr.ip, sizeof( c->tvIp ) );
	if ( SV_WebTVProxy( &c->adr ) && Web_HeaderValue( head, "X-Real-IP", value, sizeof( value ) ) ) {
		Q_ParseIPv4( value, c->tvIp );
	}
}

/*
==================
SV_WebTVUpgrade

GET /tv: a viewer of the TV, while a level runs and vs_tv is on
==================
*/
void SV_WebTVUpgrade( webConnection_t *c, const char *head, int headLen, int minor ) {
	if ( !vs_tv ) {
		vs_tv = Cvar_Get( "vs_tv", "1", CVAR_ARCHIVE, "1: the TV of the server can be watched in a browser (GET /tv of the web host)" );
	}
	c->keepAlive = qfalse;
	if ( !vs_tv->integer || sv.state != SS_GAME ) {
		SV_WebError( c, 404, "Not Found", "" );
		return;
	}
	SV_WebTVFrom( c, head );
	if ( SV_WebTVCount( NULL ) >= SV_WEB_MAX_TV || SV_WebTVCount( c->tvIp ) >= SV_WEB_MAX_ADDRESS_TV ) {
		SV_WebError( c, 503, "Service Unavailable", "Retry-After: 5\r\n" );
		return;
	}
	SV_WebUpgrade( c, head, headLen, minor, qtrue );
	if ( c->state != WEB_SOCKET ) {
		return;		// the request was none for a WebSocket, and is answered with why
	}
	c->tv = qtrue;
	c->tvGreet = qtrue;
	c->tvSynced = qfalse;
	Com_Printf( "TV: a viewer from %i.%i.%i.%i%s\n", c->tvIp[0], c->tvIp[1], c->tvIp[2], c->tvIp[3],
		memcmp( c->tvIp, c->adr.ip, sizeof( c->tvIp ) ) ? " (through a proxy)" : "" );
}
