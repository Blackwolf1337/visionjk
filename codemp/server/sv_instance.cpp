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

// sv_instance.cpp -- the temple a dedicated server starts for its players. Not from OpenJK, see
// docs/japlus/server.md, "The temple of a server", and qcommon/net_link.h for the packets.
//
// The Vision Temple is a level of its own, and a server runs one level. So a server whose player wants to go to
// the temple starts this program a second time, as a temple server (game/status/g_temple.c), and its players
// visit that one while they stay on this one (client/cl_visit.cpp). The second one has no port: its players
// send to the port of the game of this server with a mark in front of their packets (the tunnel), this file
// passes those on over a socket on this machine (the link) and sends what comes back out of the port of the
// game. A server needs one UDP port for all of it.
//
// The tunnel is open to an address a player of this server plays from, and to nobody else: who is not let in
// here is not let into its temple.
//
// Both ends are this file. Which one a program is, the network layer knows (NET_LinkActive()).

#include "server.h"
#include "qcommon/net_local.h"
#include "qcommon/net_link.h"

#define INSTANCE_ARGS		48
#define INSTANCE_PER_EVENT	256			// datagrams of the link taken at once
#define INSTANCE_AGAIN_MSEC	10000		// between two starts: one that ends at once is not started in every frame

static cvar_t	*sv_templeInstance;
static cvar_t	*sv_templeIdle;
static cvar_t	*sv_instanceIdle;		// in the instance: sv_templeIdle of the server that started it

static struct {
	SOCKET				sock;			// the end of the link of this server
	byte				key[NET_LINK_KEY];
	int					process;		// the instance, 0: none
	qboolean			up;				// it said hello: addr is its end
	struct sockaddr_in	addr;
	int					started;		// when the last one was started
	int					said;			// when this end last said hello
	int					empty;			// in the instance: no player since then, 0: there is one
} inst = { INVALID_SOCKET };

static void SV_InstanceSend( int flags, const netadr_t *adr, const byte *data, int length ) {
	static byte buffer[NET_LINK_HEAD + MAX_MSGLEN];

	if ( inst.sock == INVALID_SOCKET || !inst.up || length < 0 || length > MAX_MSGLEN ) {
		return;
	}
	memcpy( buffer, inst.key, NET_LINK_KEY );
	buffer[NET_LINK_KEY] = (byte)flags;
	memset( buffer + NET_LINK_KEY + 1, 0, 6 );
	if ( adr ) {
		memcpy( buffer + NET_LINK_KEY + 1, adr->ip, 4 );
		memcpy( buffer + NET_LINK_KEY + 5, &adr->port, 2 );
	}
	if ( length ) {
		memcpy( buffer + NET_LINK_HEAD, data, length );
	}
	sendto( inst.sock, (const char *)buffer, NET_LINK_HEAD + length, 0, (const struct sockaddr *)&inst.addr, sizeof( inst.addr ) );
}

// qtrue: somebody plays on this server from this address
static qboolean SV_InstanceKnows( const netadr_t *from ) {
	int			i;
	client_t	*cl;

	for ( i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++ ) {
		if ( cl->state >= CS_CONNECTED && NET_CompareBaseAdr( from, &cl->netchan.remoteAddress ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

static qboolean SV_InstanceOpenLink( void ) {
	struct sockaddr_in	address;
	u_long				_true = 1;

	if ( inst.sock != INVALID_SOCKET ) {
		return qtrue;
	}
	if ( ( inst.sock = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP ) ) == INVALID_SOCKET ) {
		return qfalse;
	}
	memset( &address, 0, sizeof( address ) );
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
	if ( ioctlsocket( inst.sock, FIONBIO, &_true ) == SOCKET_ERROR
		|| bind( inst.sock, (const struct sockaddr *)&address, sizeof( address ) ) == SOCKET_ERROR ) {
		closesocket( inst.sock );
		inst.sock = INVALID_SOCKET;
		return qfalse;
	}
	return qtrue;
}

/*
===============
SV_InstanceStart

The temple of this server, unless it runs.

It reads what this server reads, in the same order: the folder this server writes to is its game folder, and
the game folder of this server stands behind that one (as fs_cdpath, so a server that has one of its own
loses it there). So the game module, the level, temple.cfg, the settings this server stored and the lists of
its admins and bans are the same for both. What the temple writes goes into a folder of its own inside the one
this server writes to, and it stores no settings (Com_WriteConfiguration). temple.log there has what it says.
===============
*/
static void SV_InstanceStart( void ) {
	char				basepath[MAX_OSPATH], homepath[MAX_OSPATH], home[MAX_OSPATH], game[MAX_QPATH], log[MAX_OSPATH];
	char				port[16], idle[16], link[32 + NET_LINK_KEY * 2];
	char				*argv[INSTANCE_ARGS];
	struct sockaddr_in	address;
	socklen_t			addressLen = sizeof( address );
	const int			now = Sys_Milliseconds();
	int					n = 1, i;

#ifdef VISION_MASTER
	return;		// the masterserver is the temple
#endif
	if ( NET_LinkActive() || !sv_templeInstance->integer || inst.process || !com_sv_running->integer
		|| ( inst.started && now - inst.started < INSTANCE_AGAIN_MSEC ) ) {
		return;
	}
	inst.started = now ? now : 1;
	inst.up = qfalse;
	if ( !SV_InstanceOpenLink() || getsockname( inst.sock, (struct sockaddr *)&address, &addressLen ) != 0
		|| !Sys_RandomBytes( inst.key, sizeof( inst.key ) ) ) {
		Com_Printf( "The temple of this server can not be started: no link on this machine\n" );
		return;
	}
	Com_sprintf( link, sizeof( link ), "%i:", ntohs( address.sin_port ) );
	for ( i = 0; i < NET_LINK_KEY; i++ ) {
		Q_strcat( link, sizeof( link ), va( "%02x", inst.key[i] ) );
	}

	Q_strncpyz( basepath, Cvar_VariableString( "fs_basepath" ), sizeof( basepath ) );
	Q_strncpyz( homepath, Cvar_VariableString( "fs_homepath" ), sizeof( homepath ) );
	Q_strncpyz( game, Cvar_VariableString( "fs_game" ), sizeof( game ) );
	Com_sprintf( home, sizeof( home ), "%s/temple", homepath );
	Com_sprintf( log, sizeof( log ), "%s/temple.log", home );
	Com_sprintf( port, sizeof( port ), "%i", Cvar_VariableIntegerValue( "net_port" ) );
	// (one that was started with the server stays)
	Com_sprintf( idle, sizeof( idle ), "%i", sv_templeInstance->integer > 1 ? 0 : Q_max( sv_templeIdle->integer, 0 ) );
	Sys_Mkdir( home );

#define ARG( text )		argv[n++] = (char *)( text )
#define SET( name, value )	ARG( "+set" ); ARG( name ); ARG( value )
	SET( "dedicated", "1" );
	SET( "fs_basepath", homepath );
	if ( !Sys_PathCmp( homepath, basepath ) ) {
		SET( "fs_cdpath", basepath );
	}
	SET( "fs_homepath", home );
	if ( game[0] ) {
		SET( "fs_game", game );
	}
	// the port of this server: what the instance tells its players to send to. It opens none.
	SET( "net_port", port );
	SET( "sv_instanceIdle", idle );
	// (These also where temple.cfg is missing, and whatever this server stored: no fight, no second web panel
	// on the port of the first, and no port for the voice chat, which comes over the link.)
	SET( "g_visionTemple", "1" );
	SET( "g_webPort", "0" );
	SET( "sv_webPort", "0" );
	SET( "g_voicePort", "0" );
	ARG( "+exec" ); ARG( "temple.cfg" );
	ARG( "+map" ); ARG( "vision_temple" );
	argv[n] = NULL;
#undef SET
#undef ARG

	inst.process = Sys_StartSelf( argv, NET_LINK_ENV, link, log );
	if ( inst.process ) {
		Com_Printf( "The temple of this server is started (what it says: %s)\n", log );
	} else {
		Com_Printf( "The temple of this server can not be started on this system\n" );
	}
}

// "templeinstance": the temple of this server is started unless it runs. "templeinstance stop": it is told to end.
static void SV_Instance_f( void ) {
	if ( NET_LinkActive() ) {
		Com_Printf( "This server is a temple itself\n" );
		return;
	}
	if ( !Q_stricmp( Cmd_Argv( 1 ), "stop" ) ) {
		SV_InstanceSend( NET_LINK_QUIT, NULL, NULL, 0 );
		return;
	}
	if ( !sv_templeInstance->integer ) {
		Com_Printf( "This server starts no temple (sv_templeInstance 0)\n" );
		return;
	}
	if ( inst.process ) {
		if ( Cmd_Argc() > 1 ) {
			Com_Printf( "The temple of this server %s\n", inst.up ? "runs" : "is started and has not said hello yet" );
		}
		return;
	}
	inst.started = 0;
	SV_InstanceStart();
}

/*
===============
SV_InstancePacket

A packet of the tunnel came in on the port of the game (SV_PacketEvent): from a player of this server it goes to
the temple, from anybody else nowhere.
===============
*/
void SV_InstancePacket( const netadr_t *from, msg_t *msg ) {
	if ( !inst.up || from->type != NA_IP || msg->cursize < NET_TUNNEL_HEAD || msg->data[4] != NET_TUNNEL_TEMPLE
		|| SV_IDS_Blocked( from ) || !SV_InstanceKnows( from ) ) {
		return;
	}
	SV_InstanceSend( 0, from, msg->data + NET_TUNNEL_HEAD, msg->cursize - NET_TUNNEL_HEAD );
}

// A datagram of the voice chat that is none of a player in this level (sv_voice.cpp): he may be in the temple.
// qfalse: there is no temple, or nobody plays here from that address.
qboolean SV_InstanceVoice( const netadr_t *from, const byte *data, int length ) {
	if ( !inst.up || !SV_InstanceKnows( from ) ) {
		return qfalse;
	}
	SV_InstanceSend( 0, from, data, length );
	return qtrue;
}

// the socket of the link waits in the select() of NET_Sleep, as the ones of the web host do
void SV_InstanceFDSet( fd_set *readSet, SOCKET *highest ) {
	if ( inst.sock == INVALID_SOCKET ) {
		return;
	}
	FD_SET( inst.sock, readSet );
	if ( *highest == INVALID_SOCKET || inst.sock > *highest ) {
		*highest = inst.sock;
	}
}

/*
===============
SV_InstanceEvent

What the temple sent over the link goes out of the port of the game: with the mark of the tunnel in front, or
as it is where the temple says so (a datagram of the voice chat, whose socket in the client knows no tunnel).
===============
*/
void SV_InstanceEvent( fd_set *readSet ) {
	static byte	buffer[NET_LINK_HEAD + MAX_MSGLEN];
	const int	mark = NET_TUNNEL_MARK;
	int			count;

	if ( inst.sock == INVALID_SOCKET || !FD_ISSET( inst.sock, readSet ) ) {
		return;
	}
	for ( count = 0; count < INSTANCE_PER_EVENT; count++ ) {
		struct sockaddr_in	from;
		socklen_t			fromLen = sizeof( from );
		const int			length = recvfrom( inst.sock, (char *)buffer, sizeof( buffer ), 0, (struct sockaddr *)&from, &fromLen );
		netadr_t			to;
		byte				*packet = buffer + NET_LINK_HEAD;
		int					flags;

		if ( length == SOCKET_ERROR ) {
			break;
		}
		if ( length < NET_LINK_HEAD || from.sin_addr.s_addr != htonl( INADDR_LOOPBACK ) || memcmp( buffer, inst.key, NET_LINK_KEY ) ) {
			continue;
		}
		flags = buffer[NET_LINK_KEY];
		if ( !inst.up ) {
			if ( !( flags & NET_LINK_HELLO ) ) {
				continue;
			}
			inst.addr = from;
			inst.up = qtrue;
			Com_Printf( "The temple of this server is up\n" );
		} else if ( from.sin_port != inst.addr.sin_port ) {
			continue;
		}
		if ( flags & NET_LINK_HELLO ) {
			continue;
		}
		Com_Memset( &to, 0, sizeof( to ) );
		to.type = NA_IP;
		memcpy( to.ip, buffer + NET_LINK_KEY + 1, 4 );
		memcpy( &to.port, buffer + NET_LINK_KEY + 5, 2 );
		if ( flags & NET_LINK_PLAIN ) {
			Sys_SendPacket( length - NET_LINK_HEAD, packet, &to );
		} else {
			// (the mark goes where the end of the head of the link was)
			packet -= NET_TUNNEL_HEAD;
			memcpy( packet, &mark, 4 );
			packet[4] = NET_TUNNEL_TEMPLE;
			Sys_SendPacket( length - NET_LINK_HEAD + NET_TUNNEL_HEAD, packet, &to );
		}
	}
}

/*
===============
SV_InstanceFrame

Once a frame of a server that runs. The server: is its temple still there, does it have to be started
(sv_templeInstance 2), and it says hello to it. The temple: it says hello, and ends when its server is gone or
when nobody was in it for the time its server allows.
===============
*/
void SV_InstanceFrame( void ) {
	const int now = Sys_Milliseconds();

	if ( NET_LinkActive() ) {
		const int	minutes = sv_instanceIdle->integer;
		int			i, players = 0;

		NET_LinkFrame();
		for ( i = 0; i < sv_maxclients->integer; i++ ) {
			if ( svs.clients[i].state >= CS_CONNECTED ) {
				players++;
			}
		}
		if ( players ) {
			inst.empty = 0;
		} else if ( !inst.empty ) {
			inst.empty = now ? now : 1;
		} else if ( minutes > 0 && now - inst.empty >= minutes * 60000 ) {
			inst.empty = now ? now : 1;
			Com_Printf( "Nobody was in the temple for %i minutes: it ends until somebody asks for it\n", minutes );
			Cbuf_AddText( "quit\n" );
		}
		return;
	}

	if ( inst.process && Sys_ProcessEnded( inst.process ) ) {
		Com_Printf( "The temple of this server ended\n" );
		inst.process = 0;
		inst.up = qfalse;
	}
	if ( !inst.process && sv_templeInstance->integer > 1 && sv.state == SS_GAME ) {
		SV_InstanceStart();
	}
	if ( inst.up && now - inst.said >= NET_LINK_HELLO_MSEC ) {
		inst.said = now;
		SV_InstanceSend( NET_LINK_HELLO, NULL, NULL, 0 );
	}
}

// the server ends: its temple with it
void SV_InstanceShutdown( void ) {
	SV_InstanceSend( NET_LINK_QUIT, NULL, NULL, 0 );
	inst.up = qfalse;
}

void SV_InstanceInit( void ) {
	if ( NET_LinkActive() ) {
		// a temple starts none of its own, and says so to its game module
		sv_templeInstance = Cvar_Get( "sv_templeInstance", "0", CVAR_ROM, "" );
		Cvar_Set( "sv_templeInstance", "0" );
	} else {
#ifdef VISION_MASTER
		sv_templeInstance = Cvar_Get( "sv_templeInstance", "0", CVAR_ROM, "" );
#else
		sv_templeInstance = Cvar_Get( "sv_templeInstance", "1", CVAR_ARCHIVE_ND, "The Vision Temple of this server, a second server it starts for its players on the port of the game. 0: none, they go to the masterserver. 1: started when the first player asks for it. 2: started with the server" );
#endif
	}
	sv_templeIdle = Cvar_Get( "sv_templeIdle", "5", CVAR_ARCHIVE_ND, "Minutes without a player after which the temple of this server ends, until it is asked for again. 0: never. (Not with sv_templeInstance 2)" );
	sv_instanceIdle = Cvar_Get( "sv_instanceIdle", "0", CVAR_INIT, "" );
	Cmd_AddCommand( "templeinstance", SV_Instance_f, "Start the Vision Temple of this server unless it runs. With stop: end it" );
}
