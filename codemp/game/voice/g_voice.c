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

// g_voice.c -- the relay of the voice chat. Not from JA+, see docs/japlus/voice.md and bg_voice.h.
//
// The cgame of this port records the microphone and sends it to a UDP port of its own here. This file hands
// every datagram on to the clients who are to hear it. It never looks into the sound.
//
// Nothing of it is in the game protocol, so a client without that cgame (jamp, the JA+ plugin, another fork)
// sees no difference: the one server command there is, jpvoice, only goes to a client that asked with the
// client command of the same name.

#include "game/bg_net.h"		// the sockets, in front of everything else
#include "game/g_local.h"
#include "bg_voice.h"

#define VOICE_MAX_PER_CALL	64		// datagrams one call of G_VoiceFrame() takes at most

typedef struct voiceClient_s {
	unsigned int		token;		// what the server gave him for this connection, 0: he did not ask for voice
	qboolean			ready;		// a datagram with his token came: addr is where he listens
	struct sockaddr_in	addr;
	struct in_addr		gameAddr;	// where his game connection comes from
	qboolean			gameAddrKnown;
	voiceLimit_t		limit;
	int					relayed;	// datagrams of his that were handed on, since he asked
	int					dropped;	//	and those over the limit
} voiceClient_t;

static voiceClient_t	voiceClients[MAX_CLIENTS];
static bgSocket_t		voiceSocket = BG_INVALID_SOCKET;
static int				voicePort;
static int				voiceRefused;		// datagrams that were no voice of a client of this server, since the socket was made

/*
=================
G_VoiceShutdown

restart: the level starts again and the module stays, so do the socket and what the clients were given.
=================
*/
void G_VoiceShutdown( qboolean restart ) {
	if ( restart )
		return;
	if ( voiceSocket != BG_INVALID_SOCKET ) {
		BG_CloseSocket( voiceSocket );
		voiceSocket = BG_INVALID_SOCKET;
	}
	memset( voiceClients, 0, sizeof( voiceClients ) );
}

/*
=================
G_VoiceInit

Opens the port if g_voice is set. Whatever goes wrong here ends in one line in the console and a server that
runs as it would without voice.
=================
*/
void G_VoiceInit( qboolean restart ) {
	struct sockaddr_in	addr;
	char				text[MAX_INFO_STRING];

	if ( restart && voiceSocket != BG_INVALID_SOCKET )
		return;
	G_VoiceShutdown( qfalse );
	voiceRefused = 0;
	if ( !g_voice.integer )
		return;

	voicePort = g_voicePort.integer;
	if ( voicePort <= 0 )
		voicePort = trap->Cvar_VariableIntegerValue( "net_port" ) + 1;
	if ( voicePort <= 0 || voicePort > 65535 ) {
		trap->Print( "Voice chat is off: %i is no port\n", voicePort );
		return;
	}

	BG_NetStart();
	voiceSocket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( voiceSocket == BG_INVALID_SOCKET ) {
		trap->Print( "Voice chat is off: no socket\n" );
		return;
	}
	memset( &addr, 0, sizeof( addr ) );
	addr.sin_family = AF_INET;
	addr.sin_port = htons( (unsigned short)voicePort );
	// on the address the game itself listens on, if it was given one
	trap->Cvar_VariableStringBuffer( "net_ip", text, sizeof( text ) );
	if ( inet_pton( AF_INET, text, &addr.sin_addr ) != 1 )
		addr.sin_addr.s_addr = htonl( INADDR_ANY );
	if ( bind( voiceSocket, (struct sockaddr *)&addr, sizeof( addr ) ) != 0 ) {
		trap->Print( "Voice chat is off: UDP port %i can not be opened (g_voicePort)\n", voicePort );
		BG_CloseSocket( voiceSocket );
		voiceSocket = BG_INVALID_SOCKET;
		return;
	}
	BG_SocketNonBlocking( voiceSocket );
	trap->Print( "Voice chat on UDP port %i\n", voicePort );

	// the cgame only asks for voice on a server that says in its server info that it has it
	trap->GetServerinfo( text, sizeof( text ) );
	if ( !Info_ValueForKey( text, "g_voice" )[0] )
		trap->Print( "Voice chat: the server info is full and does not say g_voice, so no client will ask for it\n" );
}

// he is gone, or somebody new has his number
void G_VoiceClientGone( int clientNum ) {
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS )
		memset( &voiceClients[clientNum], 0, sizeof( voiceClients[clientNum] ) );
}

/*
=================
Cmd_JPVoice_f

jpvoice hello <protocol>: the cgame of a client asks for voice. The answer is the server command
jpvoice <protocol> <port> <token> <range>, and it is the only thing of the voice chat that ever goes through
the game connection. A client is told the protocol of this server even if his is another one, he then stays
silent by himself.
=================
*/
void Cmd_JPVoice_f( gentity_t *ent ) {
	voiceClient_t	*vc = &voiceClients[ent - g_entities];
	char			arg[16], ip[NET_ADDRSTRMAXLEN], *colon;
	unsigned int	token = 0;

	trap->Argv( 1, arg, sizeof( arg ) );
	if ( voiceSocket == BG_INVALID_SOCKET || Q_stricmp( arg, "hello" ) || ( ent->r.svFlags & SVF_BOT ) )
		return;

	// a number nobody can guess: with it a datagram counts as his
	if ( !BG_Random( &token, sizeof( token ) ) ) {
		trap->Print( "Voice chat: the system gives no random numbers, client %i gets no voice\n", (int)( ent - g_entities ) );
		return;
	}
	if ( !token )
		token = 1;

	memset( vc, 0, sizeof( *vc ) );
	vc->token = token;
	// his datagrams have to come from where his game connection comes from. The port is another one.
	Q_strncpyz( ip, ent->client->sess.IP, sizeof( ip ) );
	colon = strchr( ip, ':' );
	if ( colon )
		*colon = '\0';
	if ( inet_pton( AF_INET, ip, &vc->gameAddr ) == 1 ) {
		vc->gameAddrKnown = qtrue;
	} else if ( !Q_stricmp( ip, "localhost" ) || !Q_stricmp( ip, "loopback" ) ) {
		vc->gameAddr.s_addr = htonl( INADDR_LOOPBACK );
		vc->gameAddrKnown = qtrue;
	}
	trap->SendServerCommand( ent - g_entities, va( "jpvoice %i %i %u %i", VOICE_PROTOCOL, voicePort, token,
		g_voiceRange.integer > 0 ? g_voiceRange.integer : 0 ) );
}

// a player as BG_VoiceHears() wants him
static void G_VoiceEar( int clientNum, voiceEar_t *ear ) {
	const gentity_t	*ent = &g_entities[clientNum];
	const gclient_t	*client = ent->client;

	memset( ear, 0, sizeof( *ear ) );
	if ( !ent->inuse || !client || client->pers.connected != CON_CONNECTED )
		return;
	ear->ready = voiceClients[clientNum].ready;
	ear->spectator = ( client->sess.sessionTeam == TEAM_SPECTATOR || client->tempSpectate >= level.time ) ? qtrue : qfalse;
	ear->silenced = client->pers.jp.silenced ? qtrue : qfalse;
	ear->team = client->sess.sessionTeam;
	VectorCopy( client->ps.origin, ear->origin );
}

// what one speaker said goes to everybody who is to hear it
static void G_VoiceRelay( int speaker, const voicePacket_t *in ) {
	const qboolean	teamOnly = ( g_voiceTeamOnly.integer && level.gametype >= GT_TEAM ) ? qtrue : qfalse;
	voiceEar_t		mouth, ear;
	voicePacket_t	out = *in;
	byte			datagram[VOICE_MAX_DATAGRAM];
	int				i, len;

	G_VoiceEar( speaker, &mouth );
	if ( !mouth.ready )
		return;
	out.client = speaker;

	for ( i = 0; i < level.maxclients; i++ ) {
		const voiceClient_t	*vc = &voiceClients[i];
		float				distance;

		if ( i == speaker || !vc->ready )
			continue;
		G_VoiceEar( i, &ear );
		if ( !BG_VoiceHears( &mouth, &ear, (float)g_voiceRange.integer, teamOnly, &distance ) )
			continue;

		out.token = vc->token;
		// The server does not show him the speaker: he hears him, but is told no more than about how far away.
		// A voice must not say where somebody behind a wall stands.
		out.flags = trap->InPVS( ear.origin, mouth.origin ) ? 0 : VOICE_FLAG_UNSEEN;
		out.distance = (int)( distance / VOICE_DISTANCE_STEP );
		len = BG_VoicePack( datagram, sizeof( datagram ), &out, qtrue );
		if ( len )
			sendto( voiceSocket, (const char *)datagram, len, BG_SEND_FLAGS, (const struct sockaddr *)&vc->addr, sizeof( vc->addr ) );
	}
	voiceClients[speaker].relayed++;
}

/*
=================
G_VoiceFrame

Takes what came in on the voice port and hands it on. Called with every server frame and with every move a
client sends (ClientThink), so a voice does not wait for the next server frame. No call waits, and one call
takes a limited number of datagrams.

A datagram counts only if it is a whole one of this protocol, names a client who is connected, carries the
token that client was given, and comes from the address his game connection comes from. Everything else is
dropped without an answer. One datagram in is at most one out per listener, and only to addresses that proved
themselves with a token, so the port can not be used to flood somebody else.
=================
*/
void G_VoiceFrame( void ) {
	int count;

	if ( voiceSocket == BG_INVALID_SOCKET )
		return;

	for ( count = 0; count < VOICE_MAX_PER_CALL; count++ ) {
		byte				datagram[VOICE_MAX_DATAGRAM + 1];	// one more: a longer one shows as too long
		struct sockaddr_in	from;
		socklen_t			fromLen = sizeof( from );
		voicePacket_t		p;
		voiceClient_t		*vc;
		const int			len = (int)recvfrom( voiceSocket, (char *)datagram, sizeof( datagram ), 0, (struct sockaddr *)&from, &fromLen );

		if ( len < 0 ) {
			if ( BG_SocketReset() )
				continue;
			break;
		}
		if ( fromLen < (socklen_t)sizeof( from ) || from.sin_family != AF_INET
			|| !BG_VoiceUnpack( datagram, len, qfalse, &p ) || p.client >= level.maxclients ) {
			voiceRefused++;
			continue;
		}
		vc = &voiceClients[p.client];
		if ( !vc->token || p.token != vc->token || !g_entities[p.client].inuse || !g_entities[p.client].client
			|| g_entities[p.client].client->pers.connected == CON_DISCONNECTED
			|| ( vc->gameAddrKnown && from.sin_addr.s_addr != vc->gameAddr.s_addr ) ) {
			voiceRefused++;
			continue;
		}
		if ( !BG_VoiceLimit( &vc->limit, trap->Milliseconds(), len ) ) {
			vc->dropped++;
			continue;
		}

		// this is where he listens. His router may give him another port after a while, so it is taken anew
		// every time.
		vc->addr = from;
		vc->ready = qtrue;

		if ( p.type == VOICE_HELLO ) {
			// the answer tells him that the way is open in both directions
			voicePacket_t	ack = p;
			byte			out[VOICE_MAX_DATAGRAM];
			int				outLen;

			ack.flags = ack.distance = 0;
			outLen = BG_VoicePack( out, sizeof( out ), &ack, qtrue );
			if ( outLen )
				sendto( voiceSocket, (const char *)out, outLen, BG_SEND_FLAGS, (const struct sockaddr *)&from, sizeof( from ) );
			continue;
		}
		G_VoiceRelay( p.client, &p );
	}
}

// the server command voicestatus: who has voice and what the relay did
void Svcmd_VoiceStatus_f( void ) {
	int i, count = 0;

	if ( voiceSocket == BG_INVALID_SOCKET ) {
		trap->Print( "Voice chat is off (g_voice %i)\n", g_voice.integer );
		return;
	}
	trap->Print( "Voice chat on UDP port %i, range %i, %s. %i datagrams refused.\n", voicePort, g_voiceRange.integer,
		( g_voiceTeamOnly.integer && level.gametype >= GT_TEAM ) ? "team only" : "everybody in range", voiceRefused );
	for ( i = 0; i < level.maxclients; i++ ) {
		const voiceClient_t *vc = &voiceClients[i];

		if ( !vc->token || !g_entities[i].inuse || !g_entities[i].client )
			continue;
		trap->Print( "%2i %-24s %s, %i handed on, %i over the limit\n", i, g_entities[i].client->pers.netname_nocolor,
			vc->ready ? "ready" : "asked, no datagram yet", vc->relayed, vc->dropped );
		count++;
	}
	trap->Print( "%i clients with voice\n", count );
}
