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
// sees no difference: the one server command there is, vs_voiceLink, only goes to a client that asked with the
// client command of the same name.
//
// A client that runs in a browser sends the same datagrams through the web host of the dedicated server
// (codemp/server/web/sv_web_voice.cpp), which passes them on to this port as they are.
//
// Where the datagrams come in. A dedicated server of this tree that says vs_voiceShared 1 takes them on the UDP
// port of the game and hands them over (ext.VoiceDatagram of the exports, codemp/server/sv_voice.cpp), and what
// goes to such a client leaves from that port again (ext.SendDatagram of the imports): the server needs one
// port. The socket of this file is then one on this machine only, for the web host. With any other engine, or
// with a vs_voicePort, the socket is the relay: the port of the game plus one, or the one that cvar names.

#include "game/bg_net.h"		// the sockets, in front of everything else
#include "game/g_local.h"
#include "bg_voice.h"

#define VOICE_MAX_PER_CALL	64		// datagrams one call of G_VoiceFrame() takes at most

typedef struct voiceClient_s {
	unsigned int		token;		// what the server gave him for this connection, 0: he did not ask for voice
	qboolean			ready;		// a datagram with his token came: addr is where he listens
	qboolean			viaGame;	// it came in on the port of the game: what goes to him leaves from there
	struct sockaddr_in	addr;
	struct in_addr		gameAddr;	// where his game connection comes from
	qboolean			gameAddrKnown;
	voiceLimit_t		limit;
	int					relayed;	// datagrams of his that were handed on, since he asked
	int					dropped;	//	and those over the limit
} voiceClient_t;

static voiceClient_t	voiceClients[MAX_CLIENTS];
static qboolean			voiceOn;			// there is a relay
static bgSocket_t		voiceSocket = BG_INVALID_SOCKET;	// its own socket. The shared way has one only for the web host
static int				voicePort;			// of the socket
static qboolean			voiceShared;		// the engine hands over what comes in on the port of the game (vs_voiceShared)
static int				voiceGamePort;		//	and this is that port
static int				voiceRefused;		// datagrams that were no voice of a client of this server, since the socket was made
static struct in_addr	voiceSelf;			// the address of this machine the web host of the server reaches the relay on

/*
=================
G_VoiceTell

Says in the cvar vs_voiceRelay where the relay listens, "address:port" in numbers, or nothing while there is no
relay. The web host of the dedicated server reads it (codemp/server/web/sv_web_voice.cpp): it carries the
voice chat of the clients that run in a browser, see G_VoiceFromWebHost().
=================
*/
static void G_VoiceTell( qboolean open ) {
	const byte *ip = (const byte *)&voiceSelf.s_addr;

	trap->Cvar_Register( NULL, "vs_voiceRelay", "", CVAR_ROM );
	trap->Cvar_Set( "vs_voiceRelay", open ? va( "%i.%i.%i.%i:%i", ip[0], ip[1], ip[2], ip[3], voicePort ) : "" );
}

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
	G_VoiceTell( qfalse );
	voiceOn = voiceShared = qfalse;
	trap->Cvar_Set( "vs_voiceSharedGame", "0" );
	memset( voiceClients, 0, sizeof( voiceClients ) );
}

/*
=================
G_VoiceInit

Opens the port if vs_voice is set. Whatever goes wrong here ends in one line in the console and a server that
runs as it would without voice.
=================
*/
void G_VoiceInit( qboolean restart ) {
	struct sockaddr_in	addr;
	socklen_t			addrLen = sizeof( addr );
	char				text[MAX_INFO_STRING];
	qboolean			shared;

	if ( restart && voiceOn )
		return;
	trap->Cvar_Register( NULL, "vs_voiceSharedGame", "0", CVAR_ROM );
	G_VoiceShutdown( qfalse );
	voiceRefused = 0;
	if ( !vs_voice.integer )
		return;

	voiceGamePort = trap->Cvar_VariableIntegerValue( "net_port" );
	shared = ( trap->Cvar_VariableIntegerValue( "vs_voiceShared" ) && vs_voicePort.integer <= 0 ) ? qtrue : qfalse;
	voicePort = vs_voicePort.integer;
	if ( shared )
		voicePort = 0;		// any free one: nobody outside of this machine sends to it
	else if ( voicePort <= 0 )
		voicePort = voiceGamePort + 1;
	if ( voicePort < 0 || voicePort > 65535 || ( !shared && !voicePort ) ) {
		trap->Print( "Voice chat is off: %i is no port\n", voicePort );
		return;
	}

	// the shared way needs a socket of its own only for the web host of the server (vs_clientWebPort)
	if ( !shared || trap->Cvar_VariableIntegerValue( "vs_clientWebPort" ) > 0 ) {
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
		if ( shared )
			addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
		else if ( inet_pton( AF_INET, text, &addr.sin_addr ) != 1 )
			addr.sin_addr.s_addr = htonl( INADDR_ANY );
		if ( bind( voiceSocket, (struct sockaddr *)&addr, sizeof( addr ) ) != 0
			|| ( shared && getsockname( voiceSocket, (struct sockaddr *)&addr, &addrLen ) != 0 ) ) {
			trap->Print( shared ? "Voice chat is off: no UDP port on this machine can be opened\n"
				: "Voice chat is off: UDP port %i can not be opened (vs_voicePort)\n", voicePort );
			BG_CloseSocket( voiceSocket );
			voiceSocket = BG_INVALID_SOCKET;
			return;
		}
		BG_SocketNonBlocking( voiceSocket );
		voicePort = ntohs( addr.sin_port );

		// on every address: the web host of this server reaches the relay on the one no network has
		voiceSelf = addr.sin_addr;
		if ( voiceSelf.s_addr == htonl( INADDR_ANY ) )
			voiceSelf.s_addr = htonl( INADDR_LOOPBACK );
		G_VoiceTell( qtrue );
	}
	voiceOn = qtrue;
	if ( shared ) {
		voiceShared = qtrue;
		trap->Cvar_Set( "vs_voiceSharedGame", "1" );
		trap->Print( "Voice chat on the UDP port of the game, %i\n", voiceGamePort );
	} else {
		trap->Print( "Voice chat on UDP port %i\n", voicePort );
	}

	// the cgame only asks for voice on a server that says in its server info that it has it
	trap->GetServerinfo( text, sizeof( text ) );
	if ( !Info_ValueForKey( text, "vs_voice" )[0] )
		trap->Print( "Voice chat: the server info is full and does not say vs_voice, so no client will ask for it\n" );
}

// he is gone, or somebody new has his number
void G_VoiceClientGone( int clientNum ) {
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS )
		memset( &voiceClients[clientNum], 0, sizeof( voiceClients[clientNum] ) );
}

/*
=================
Cmd_JPVoice_f

vs_voiceLink hello <protocol>: the cgame of a client asks for voice. The answer is the server command
vs_voiceLink <protocol> <port> <token> <range> <caps>, and it is the only thing of the voice chat that ever goes
through the game connection. A client is told the protocol of this server even if his is another one, he then
stays silent by himself. caps: what this relay knows beyond the protocol (VOICE_CAP_RADIO), a cgame from before
that does not look at it.
=================
*/
void Cmd_JPVoice_f( gentity_t *ent ) {
	voiceClient_t	*vc = &voiceClients[ent - g_entities];
	char			arg[16], ip[NET_ADDRSTRMAXLEN];
	unsigned int	token = 0;

	trap->Argv( 1, arg, sizeof( arg ) );
	if ( !voiceOn || Q_stricmp( arg, "hello" ) || ( ent->r.svFlags & SVF_BOT ) )
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
	JP_IPOnly( ent->client->sess.IP, ip, sizeof( ip ) );
	if ( inet_pton( AF_INET, ip, &vc->gameAddr ) == 1 ) {
		vc->gameAddrKnown = qtrue;
	} else if ( !Q_stricmp( ip, "localhost" ) || !Q_stricmp( ip, "loopback" ) ) {
		vc->gameAddr.s_addr = htonl( INADDR_LOOPBACK );
		vc->gameAddrKnown = qtrue;
	}
	trap->SendServerCommand( ent - g_entities, va( "vs_voiceLink %i %i %u %i %i", VOICE_PROTOCOL, voiceShared ? voiceGamePort : voicePort, token,
		vs_voiceRange.integer > 0 ? vs_voiceRange.integer : 0, vs_voiceRadio.integer ? VOICE_CAP_RADIO : 0 ) );
}

// one datagram to a client, the way his come in
static void G_VoiceSend( const voiceClient_t *vc, const byte *datagram, int len ) {
	if ( vc->viaGame )
		trap->ext.SendDatagram( (const byte *)&vc->addr.sin_addr.s_addr, ntohs( vc->addr.sin_port ), datagram, len );
	else
		sendto( voiceSocket, (const char *)datagram, len, BG_SEND_FLAGS, (const struct sockaddr *)&vc->addr, sizeof( vc->addr ) );
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
	const qboolean	teamOnly = ( vs_voiceTeamOnly.integer && level.gametype >= GT_TEAM ) ? qtrue : qfalse;
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
		if ( !BG_VoiceHears( &mouth, &ear, (float)vs_voiceRange.integer, teamOnly, &distance ) )
			continue;

		out.token = vc->token;
		// The server does not show him the speaker: he hears him, but is told no more than about how far away.
		// A voice must not say where somebody behind a wall stands.
		out.flags = trap->InPVS( ear.origin, mouth.origin ) ? 0 : VOICE_FLAG_UNSEEN;
		out.distance = (int)( distance / VOICE_DISTANCE_STEP );
		len = BG_VoicePack( datagram, sizeof( datagram ), &out, qtrue );
		if ( len )
			G_VoiceSend( vc, datagram, len );
	}
	voiceClients[speaker].relayed++;
}

/*
=================
G_VoiceRadio

What one speaker said into the clan radio goes to the players of his clan: the ones with his clan password
(cp_clanPwd of the client, or /clanpass), who are the ones his clan chat reaches. Where they are and which
team they are on does not matter, nobody else gets it, and it says nothing about where he is.
=================
*/
static void G_VoiceRadio( int speaker, const voicePacket_t *in ) {
	const gclient_t	*client = g_entities[speaker].client;
	voicePacket_t	out = *in;
	byte			datagram[VOICE_MAX_DATAGRAM];
	int				i, len;

	if ( !vs_voiceRadio.integer || !voiceClients[speaker].ready || client->pers.connected != CON_CONNECTED
		|| client->pers.jp.silenced || !client->pers.jp.clanPass[0] )
		return;
	out.type = VOICE_AUDIO;
	out.client = speaker;
	// a cgame from before the radio takes it for a voice close by that it is not shown, see bg_voice.h
	out.flags = VOICE_FLAG_RADIO | VOICE_FLAG_UNSEEN;
	out.distance = 0;

	for ( i = 0; i < level.maxclients; i++ ) {
		const voiceClient_t	*vc = &voiceClients[i];
		const gentity_t		*other = &g_entities[i];

		if ( i == speaker || !vc->ready || !other->inuse || !other->client || other->client->pers.connected != CON_CONNECTED
			|| Q_stricmp( client->pers.jp.clanPass, other->client->pers.jp.clanPass ) )
			continue;
		out.token = vc->token;
		len = BG_VoicePack( datagram, sizeof( datagram ), &out, qtrue );
		if ( len )
			G_VoiceSend( vc, datagram, len );
	}
	voiceClients[speaker].relayed++;
}

/*
=================
G_VoiceFromWebHost

A client that runs in a browser can not send to this port. His datagrams come through the web host of the
dedicated server of this tree (vs_clientWebPort), from a socket that host has on this machine, and are handed on
there only if the client they name plays through that host from the address they came from: the check of the
address is made where the address is known. So on a server that runs that host a datagram from this machine
is taken with the token alone.

Nothing that comes in from a network has the address of this machine, the system does not take such a packet
from a line. On a server without that host nothing changes.
=================
*/
static qboolean G_VoiceFromWebHost( const struct sockaddr_in *from ) {
	return ( from->sin_addr.s_addr == voiceSelf.s_addr && trap->Cvar_VariableIntegerValue( "vs_clientWebPort" ) > 0 ) ? qtrue : qfalse;
}

/*
=================
G_VoiceDatagram

One datagram of the voice chat that came in: on the socket of this file, or on the port of the game (viaGame,
G_VoiceFromGame()). qfalse: it is none of a client of this server.

A datagram counts only if it is a whole one of this protocol, names a client who is connected, carries the
token that client was given, and comes from the address his game connection comes from (or from the web host
of this server, which looked at that address itself). Everything else is dropped without an answer. One
datagram in is at most one out per listener, and only to addresses that proved themselves with a token, so the
port can not be used to flood somebody else.
=================
*/
static qboolean G_VoiceDatagram( const struct sockaddr_in *from, const byte *datagram, int len, qboolean viaGame ) {
	voicePacket_t	p;
	voiceClient_t	*vc;

	// (a VOICE_STATE and a VOICE_STATUS are for the masterserver: this relay knows from the game where
	// everybody is, and the statuses of a server with it go through the game connection, status/g_status.c)
	if ( from->sin_family != AF_INET || !BG_VoiceUnpack( datagram, len, qfalse, &p ) || p.type == VOICE_STATE
		|| p.type == VOICE_STATUS || p.client >= level.maxclients ) {
		voiceRefused++;
		return qfalse;
	}
	vc = &voiceClients[p.client];
	if ( !vc->token || p.token != vc->token || !g_entities[p.client].inuse || !g_entities[p.client].client
		|| g_entities[p.client].client->pers.connected == CON_DISCONNECTED
		|| ( vc->gameAddrKnown && from->sin_addr.s_addr != vc->gameAddr.s_addr && ( viaGame || !G_VoiceFromWebHost( from ) ) ) ) {
		voiceRefused++;
		return qfalse;
	}
	if ( !BG_VoiceLimit( &vc->limit, trap->Milliseconds(), len ) ) {
		vc->dropped++;
		return qtrue;
	}

	// this is where he listens. His router may give him another port after a while, so it is taken anew
	// every time.
	vc->addr = *from;
	vc->viaGame = viaGame;
	vc->ready = qtrue;

	if ( p.type == VOICE_HELLO ) {
		// the answer tells him that the way is open in both directions
		voicePacket_t	ack = p;
		byte			out[VOICE_MAX_DATAGRAM];
		int				outLen;

		ack.flags = ack.distance = 0;
		outLen = BG_VoicePack( out, sizeof( out ), &ack, qtrue );
		if ( outLen )
			G_VoiceSend( vc, out, outLen );
	} else if ( p.type == VOICE_RADIO ) {
		G_VoiceRadio( p.client, &p );
	} else {
		G_VoiceRelay( p.client, &p );
	}
	return qtrue;
}

// ext.VoiceDatagram of the exports (g_public.h): what came in on the UDP port of the game
qboolean G_VoiceFromGame( const byte *ip, int port, const byte *data, int length ) {
	struct sockaddr_in from;

	if ( !voiceShared )
		return qfalse;
	memset( &from, 0, sizeof( from ) );
	from.sin_family = AF_INET;
	memcpy( &from.sin_addr.s_addr, ip, 4 );
	from.sin_port = htons( (unsigned short)port );
	return G_VoiceDatagram( &from, data, length, qtrue );
}

/*
=================
G_VoiceFrame

Takes what came in on the socket of this file and hands it on. Called with every server frame and with every
move a client sends (ClientThink), so a voice does not wait for the next server frame. No call waits, and one
call takes a limited number of datagrams.
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
		const int			len = (int)recvfrom( voiceSocket, (char *)datagram, sizeof( datagram ), 0, (struct sockaddr *)&from, &fromLen );

		if ( len < 0 ) {
			if ( BG_SocketReset() )
				continue;
			break;
		}
		if ( fromLen < (socklen_t)sizeof( from ) ) {
			voiceRefused++;
			continue;
		}
		G_VoiceDatagram( &from, datagram, len, qfalse );
	}
}

/*
===============================================================================

For the boombox (boombox/g_boombox.c), whose songs go out over this relay

===============================================================================
*/

qboolean G_VoiceOn( void ) {
	return voiceOn;
}

// does this client listen on the relay: his cgame asked for voice and its datagrams came
qboolean G_VoiceReady( int clientNum ) {
	return ( voiceOn && clientNum >= 0 && clientNum < MAX_CLIENTS && voiceClients[clientNum].ready ) ? qtrue : qfalse;
}

// one frame of a song to a client, as the voice of the speaker number the box has (BOX_SPEAKER_BASE of bg_boombox.h)
void G_VoiceSendBox( int clientNum, int speaker, int seq, int distance, const byte *frame, int len ) {
	const voiceClient_t	*vc = &voiceClients[clientNum];
	voicePacket_t		out;
	byte				datagram[VOICE_MAX_DATAGRAM];
	int					outLen;

	if ( !G_VoiceReady( clientNum ) )
		return;
	memset( &out, 0, sizeof( out ) );
	out.type = VOICE_AUDIO;
	out.client = speaker;
	out.token = vc->token;
	out.seq = seq;
	out.distance = distance;
	out.data = frame;
	out.dataLen = len;
	outLen = BG_VoicePack( datagram, sizeof( datagram ), &out, qtrue );
	if ( outLen )
		G_VoiceSend( vc, datagram, outLen );
}

// the server command vs_voicestatus: who has voice and what the relay did
void Svcmd_VoiceStatus_f( void ) {
	int i, count = 0;

	if ( !voiceOn ) {
		trap->Print( "Voice chat is off (vs_voice %i)\n", vs_voice.integer );
		return;
	}
	trap->Print( "Voice chat on UDP port %i%s, range %i, %s, clan radio %s. %i datagrams refused.\n",
		voiceShared ? voiceGamePort : voicePort, voiceShared ? " (the port of the game)" : "", vs_voiceRange.integer,
		( vs_voiceTeamOnly.integer && level.gametype >= GT_TEAM ) ? "team only" : "everybody in range", vs_voiceRadio.integer ? "on" : "off", voiceRefused );
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
