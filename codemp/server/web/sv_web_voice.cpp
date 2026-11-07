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

// sv_web_voice.cpp -- the voice chat of a client that runs in a browser (docs/japlus/voice.md). The cgame in a
// page can not send to the UDP port of the relay in the game module (codemp/game/voice/g_voice.c). Its
// datagrams come here as the messages of a WebSocket of their own (GET /voice) and go on to the relay from a
// UDP socket this host has for that one WebSocket, and what the relay sends to that socket goes back as
// messages. Nothing is added and nothing is changed: to the relay it is one more client of the voice chat.
//
// The relay takes a datagram by the token of the client it names and by the address it comes from, which has
// to be the one of his game connection. What comes from here comes from this machine, so that second check is
// made here, where the address of the page is known: a datagram is handed on only if the client it names
// plays through this host from the address this WebSocket has. The relay takes the address of this machine
// in place of his on a server that runs this host (g_voice.c, G_VoiceFromWebHost).
//
// Where the relay listens is what the game module says in the cvar vs_voiceRelay. The UDP socket is tied to
// that address when it is made: nothing a peer sends changes where a datagram goes, and nothing but the relay
// is heard on it.

#include "server/web/sv_web_local.h"

/*
==================
SV_WebVoiceRelay

Where the relay of the voice chat of this server listens, as its game module says it (G_VoiceInit):
address:port, in numbers. qfalse: there is no relay.
==================
*/
static qboolean SV_WebVoiceRelay( struct sockaddr_in *relay ) {
	char	text[64];
	char	*colon;
	int		port;

	Q_strncpyz( text, Cvar_VariableString( "vs_voiceRelay" ), sizeof( text ) );
	if ( !( colon = strchr( text, ':' ) ) ) {
		return qfalse;
	}
	*colon = '\0';
	port = atoi( colon + 1 );

	memset( relay, 0, sizeof( *relay ) );
	relay->sin_family = AF_INET;
	relay->sin_addr.s_addr = inet_addr( text );
	relay->sin_port = htons( (unsigned short)port );
	return (qboolean)( relay->sin_addr.s_addr != INADDR_NONE && port > 0 && port <= 65535 );
}

/*
==================
SV_WebVoicePlays

qtrue: that client of the game plays through this host from that address. The port is not looked at: the
WebSocket of his voice is another connection than the one of his packets.
==================
*/
static qboolean SV_WebVoicePlays( const client_t *cl, const netadr_t *adr ) {
	return (qboolean)( cl->state >= CS_CONNECTED && cl->netchan.remoteAddress.type == NA_WEB
		&& !memcmp( cl->netchan.remoteAddress.ip, adr->ip, sizeof( adr->ip ) ) );
}

/*
==================
SV_WebVoiceIsClient

qtrue: a client of the game plays through this host from that address
==================
*/
qboolean SV_WebVoiceIsClient( const netadr_t *adr ) {
	int i;

	// (no server was started yet, or it is gone)
	if ( !svs.clients ) {
		return qfalse;
	}
	for ( i = 0; i < sv_maxclients->integer; i++ ) {
		if ( SV_WebVoicePlays( &svs.clients[i], adr ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
==================
SV_WebVoiceUpgrade

The request for /voice, which asks to go on as a WebSocket. It is answered as the one for /net is
(SV_WebUpgrade, with its rule for Origin), but only for an address a client of the game plays from through
this host: the cgame asks when the server gave it the token of the voice chat, which is when it is in the
game. So whoever just reaches the port gets no socket to hold on to.
==================
*/
void SV_WebVoiceUpgrade( webConnection_t *c, const char *head, int headLen, int minor ) {
	struct sockaddr_in	relay;
	SOCKET				s;

	c->keepAlive = qfalse;

	// (vs_voice is 0, or the port of the relay could not be opened: this server has no voice chat)
	if ( !SV_WebVoiceRelay( &relay ) ) {
		SV_WebError( c, 404, "Not Found", "" );
		return;
	}
	if ( !SV_WebVoiceIsClient( &c->adr ) ) {
		SV_WebError( c, 403, "Forbidden", "" );
		return;
	}
	// the UDP socket needs a place of its own (SV_WebPlaces), and an address has so many of these
	if ( SV_WebPlaces() >= SV_WEB_MAX_CONNECTIONS || SV_WebVoices( &c->adr ) >= SV_WEB_MAX_ADDRESS_VOICE ) {
		SV_WebError( c, 503, "Service Unavailable", "Retry-After: 2\r\n" );
		return;
	}

	if ( ( s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP ) ) == INVALID_SOCKET ) {
		SV_WebError( c, 500, "Internal Server Error", "" );
		return;
	}
	// Tied to the relay: what is sent on it goes there, and the system hands it nothing that comes from
	// anywhere else.
	if ( !SV_WebSocketFits( s ) || !SV_WebNonBlocking( s )
		|| connect( s, (const struct sockaddr *)&relay, sizeof( relay ) ) == SOCKET_ERROR ) {
		closesocket( s );
		SV_WebError( c, 500, "Internal Server Error", "" );
		return;
	}

	SV_WebUpgrade( c, head, headLen, minor, qfalse );
	if ( c->state != WEB_SOCKET ) {
		closesocket( s );		// the request was none for a WebSocket, and is answered with why
		return;
	}
	c->voice = qtrue;
	c->voiceSock = s;
	c->voiceToken = 0;
}

/*
==================
SV_WebVoiceDatagram

A message of the peer: a datagram of the voice chat on its way to the relay
==================
*/
void SV_WebVoiceDatagram( webConnection_t *c, const byte *data, int length ) {
	voicePacket_t p;

	// More than a speaker sends is lost here: as many at once as the relay takes from him in a second, and
	// that many in every second after. The relay has its own limit behind this one.
	// (lastHeard is the time of the read these frames came with)
	if ( SVC_RateLimit( &c->packets, VOICE_MAX_PPS, 1000 / VOICE_MAX_PPS, c->lastHeard ) ) {
		return;
	}

	// Only a whole datagram of the voice chat, and only for a client who plays through this host from the
	// address of this peer. With that the relay may take the address of this machine for his.
	if ( !com_sv_running->integer || !svs.clients || !BG_VoiceUnpack( data, length, qfalse, &p )
		|| p.client >= sv_maxclients->integer || !SV_WebVoicePlays( &svs.clients[p.client], &c->adr ) ) {
		return;
	}

	c->voiceToken = p.token;
	// (not taken, or refused because the relay is gone for a moment: lost, as any datagram may be)
	send( c->voiceSock, (const char *)data, length, SV_WEB_SEND_FLAGS );
}

/*
==================
SV_WebVoiceRead

The UDP socket of this peer has something from the relay: each datagram goes out as a message, at once if
the socket of the peer takes it. Is there no room for it behind what that socket did not take yet, it is lost.

A datagram is handed on only with the token the peer itself last spoke with. The relay keeps the port of a
socket that is gone until the client it was for speaks from its next one, and the system may give that port
to the socket of another peer in between.
==================
*/
void SV_WebVoiceRead( webConnection_t *c ) {
	byte	datagram[VOICE_MAX_DATAGRAM + 1];		// one more: a longer one shows as too long
	int		count;

	for ( count = 0; count < SV_WEB_VOICE_READS; count++ ) {
		voicePacket_t	p;
		const int		len = recv( c->voiceSock, (char *)datagram, sizeof( datagram ), 0 );

		// Nothing more. Or the system tells that a datagram to the relay was refused (there is none right
		// now): the socket is readable again if something waits behind that.
		if ( len == SOCKET_ERROR ) {
			break;
		}
		if ( !c->voiceToken || !BG_VoiceUnpack( datagram, len, qtrue, &p ) || p.token != c->voiceToken ) {
			continue;
		}
		SV_WebSocketSend( c, WEB_OP_BINARY, datagram, len );
	}
	SV_WebFlush( c );
}
