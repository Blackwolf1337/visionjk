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

// vm_main.c -- the voice master: the relay of the voice chat for game servers without the game module of this
// tree. Not from JA+, see docs/japlus/voice.md and codemp/game/voice/bg_voice.h.
//
// On a server with that module the module relays (g_voice.c): it knows who is connected and where everybody
// stands. This program knows neither. It is one relay for all the other servers, and all it has is what the
// clients tell it in a VOICE_STATE: the room (the same bytes from every client of one game server), the client
// number, whom that client wants to hear and who may hear it. A voice goes from a speaker to a listener of the
// same room if the listener wants to hear him and the speaker lets him. It never looks into the sound.
//
// Since 2026-10-11 it hands on two more things, for clients that say more in their VOICE_STATE: what a speaker
// says for his clan (VOICE_RADIO) to the members of the room that named the same clan, and what stands above
// the head of a player (VOICE_STATUS) to the others of his room. It keeps neither.
//
// A program by itself with nothing of the engine in it, one thread and one socket. It is no game, so it does
// wait: in select(), for the next datagram, a second at most.

#include "game/bg_net.h"		// the sockets, in front of everything else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include "qcommon/q_shared.h"
#include "game/voice/bg_voice.h"
#include "web/web_http.h"		// Web_SHA1

#define VM_MAX_SESSIONS			1024
#define VM_BUCKETS				1024	// slots of the two tables sessions and rooms are found through, a power of two
#define VM_MAX_PER_ADDR			16		// sessions from one address: a household, or a room full of players behind one router
#define VM_SESSION_MSEC			5000	// nothing from a session for this long: it is over. A client says VOICE_STATE once a second
#define VM_SWEEP_MSEC			1000
#define VM_STATS_MSEC			60000
#define VM_MAX_PER_WAKE			256		// datagrams taken before the clock is looked at again
#define VM_DATAGRAM_OVERHEAD	28		// IPv4 and UDP in front of every datagram, counted into -maxkbps
#define VM_DEFAULT_KBPS			4000
#define VM_STATUS_MSEC			1000	// one VOICE_STATUS of a session is handed on in this time at most

typedef struct vmSession_s {
	qboolean			used;
	unsigned int		token;
	struct sockaddr_in	addr;			// where he listens. His router may give him another port, never another address
	int					room;
	int					client;			// the number he has on his game server, as he says
	unsigned int		hear, allow;	// a bit per client number of his room
	qboolean			social;			// he sends the long state: a client that knows VOICE_STATUS,
	byte				clan[VOICE_CLAN_BYTES];	//	and this is his clan in that room, all zero: none
	int64_t				statusTime;		// when his last VOICE_STATUS was handed on
	voiceLimit_t		limit;
	int64_t				lastHeard;
	int					next;			// the next session in the same slot of vmByToken, -1: none
} vmSession_t;

typedef struct vmRoom_s {
	qboolean	used;
	byte		key[VOICE_ROOM_BYTES];
	short		member[MAX_CLIENTS];	// the session that speaks as this client number, -1: nobody
	int			members;
	int			next;					// the next room in the same slot of vmByRoom, -1: none
} vmRoom_t;

static vmSession_t	vmSessions[VM_MAX_SESSIONS];
static vmRoom_t		vmRooms[VM_MAX_SESSIONS];		// a room has a member, so there are never more rooms than sessions
static int			vmByToken[VM_BUCKETS], vmByRoom[VM_BUCKETS];
static int			vmSessionCount, vmRoomCount;
static bgSocket_t	vmSocket = BG_INVALID_SOCKET;
static byte			vmSecret[16];					// what the tokens are made with, new with every start
static int			vmCapBytes;						// what may go out in a second, 0: anything
static volatile sig_atomic_t	vmQuit;

static struct {
	int64_t			second;			// the second that is counted now,
	int				bytes;			//	what went out in it,
	int				peak;			//	and the most that went out in one since the last line
	int64_t			total;
	unsigned int	relayed, refused, limited, capped;
} vmOut;

// milliseconds since some moment before the start, never backwards
static int64_t VM_Msec( void ) {
#ifdef _WIN32
	return (int64_t)GetTickCount64();
#else
	struct timespec ts;

	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

/*
=================
VM_Token

The token of the session an address has as this client of this room. It is worked out and not kept: an address
that asks is told its token, and nothing is remembered of it until a datagram comes back from there with it. So
a sender who gives an address that is not his fills no table here, and the address he gave gets one short
answer, shorter than what was sent.
=================
*/
static unsigned int VM_Token( const struct in_addr *ip, const byte *room, int client ) {
	byte			text[sizeof( vmSecret ) + 4 + VOICE_ROOM_BYTES + 1], digest[WEB_SHA1_SIZE];
	unsigned int	token;

	memcpy( text, vmSecret, sizeof( vmSecret ) );
	memcpy( text + sizeof( vmSecret ), &ip->s_addr, 4 );
	memcpy( text + sizeof( vmSecret ) + 4, room, VOICE_ROOM_BYTES );
	text[sizeof( vmSecret ) + 4 + VOICE_ROOM_BYTES] = (byte)client;
	Web_SHA1( text, sizeof( text ), digest );
	token = (unsigned int)digest[0] | ( (unsigned int)digest[1] << 8 ) | ( (unsigned int)digest[2] << 16 ) | ( (unsigned int)digest[3] << 24 );
	return token ? token : 1;
}

static int VM_RoomSlot( const byte *key ) {
	return ( key[0] | ( key[1] << 8 ) ) & ( VM_BUCKETS - 1 );
}

// -1: no session has this token from this address as this client
static int VM_FindSession( unsigned int token, const struct in_addr *ip, int client ) {
	int i;

	for ( i = vmByToken[token & ( VM_BUCKETS - 1 )]; i >= 0; i = vmSessions[i].next ) {
		const vmSession_t *s = &vmSessions[i];

		if ( s->token == token && s->client == client && s->addr.sin_addr.s_addr == ip->s_addr )
			return i;
	}
	return -1;
}

// -1: there is no such room
static int VM_FindRoom( const byte *key ) {
	int i;

	for ( i = vmByRoom[VM_RoomSlot( key )]; i >= 0; i = vmRooms[i].next ) {
		if ( !memcmp( vmRooms[i].key, key, VOICE_ROOM_BYTES ) )
			return i;
	}
	return -1;
}

// takes index out of the chain that starts at *head, the links are at step bytes from each other
static void VM_Unlink( int *head, int index, int *next, size_t step ) {
	int *link = head;

	while ( *link >= 0 && *link != index )
		link = (int *)( (byte *)next + (size_t)*link * step );
	if ( *link == index )
		*link = *(int *)( (byte *)next + (size_t)index * step );
}

// the session is over. A room nobody is left in is over too.
static void VM_Leave( int index ) {
	vmSession_t	*s = &vmSessions[index];
	vmRoom_t	*room = &vmRooms[s->room];

	VM_Unlink( &vmByToken[s->token & ( VM_BUCKETS - 1 )], index, &vmSessions[0].next, sizeof( vmSessions[0] ) );
	room->member[s->client] = -1;
	if ( --room->members <= 0 ) {
		VM_Unlink( &vmByRoom[VM_RoomSlot( room->key )], s->room, &vmRooms[0].next, sizeof( vmRooms[0] ) );
		room->used = qfalse;
		vmRoomCount--;
	}
	s->used = qfalse;
	vmSessionCount--;
}

/*
=================
VM_Join

A new session, for an address that came back with its token. Returns its index, or -1 with the flag for the
answer in *flags: the master is full, that address has too many, or somebody else speaks as this client number
in that room already. The first one to say a number has it, nobody here can tell who is right.
=================
*/
static int VM_Join( const struct sockaddr_in *from, const byte *key, int client, unsigned int token, int *flags ) {
	int			i, slot = -1, sameAddr = 0, roomIndex;
	vmSession_t	*s;
	vmRoom_t	*room;

	for ( i = 0; i < VM_MAX_SESSIONS; i++ ) {
		if ( !vmSessions[i].used ) {
			if ( slot < 0 )
				slot = i;
		} else if ( vmSessions[i].addr.sin_addr.s_addr == from->sin_addr.s_addr ) {
			sameAddr++;
		}
	}
	*flags = VOICE_FLAG_FULL;
	if ( slot < 0 || sameAddr >= VM_MAX_PER_ADDR )
		return -1;

	roomIndex = VM_FindRoom( key );
	if ( roomIndex >= 0 && vmRooms[roomIndex].member[client] >= 0 ) {
		*flags = VOICE_FLAG_TAKEN;
		return -1;
	}
	if ( roomIndex < 0 ) {
		for ( roomIndex = 0; roomIndex < VM_MAX_SESSIONS && vmRooms[roomIndex].used; roomIndex++ )
			;
		if ( roomIndex >= VM_MAX_SESSIONS )
			return -1;
		room = &vmRooms[roomIndex];
		memset( room, 0, sizeof( *room ) );
		room->used = qtrue;
		memcpy( room->key, key, VOICE_ROOM_BYTES );
		for ( i = 0; i < MAX_CLIENTS; i++ )
			room->member[i] = -1;
		room->next = vmByRoom[VM_RoomSlot( key )];
		vmByRoom[VM_RoomSlot( key )] = roomIndex;
		vmRoomCount++;
	}
	room = &vmRooms[roomIndex];
	room->member[client] = (short)slot;
	room->members++;

	s = &vmSessions[slot];
	memset( s, 0, sizeof( *s ) );
	s->used = qtrue;
	s->token = token;
	s->addr = *from;
	s->room = roomIndex;
	s->client = client;
	s->next = vmByToken[token & ( VM_BUCKETS - 1 )];
	vmByToken[token & ( VM_BUCKETS - 1 )] = slot;
	vmSessionCount++;
	*flags = 0;
	return slot;
}

// One datagram out. qfalse: it would be more than may go out in this second (-maxkbps), and it is not sent.
// An answer to a VOICE_STATE always goes out, it is counted all the same.
static qboolean VM_Send( const struct sockaddr_in *to, const voicePacket_t *p, int64_t now, qboolean always ) {
	byte		datagram[VOICE_MAX_DATAGRAM];
	const int	len = BG_VoicePack( datagram, sizeof( datagram ), p, qtrue );

	if ( !len )
		return qfalse;
	if ( now / 1000 != vmOut.second ) {
		vmOut.second = now / 1000;
		vmOut.bytes = 0;
	}
	if ( !always && vmCapBytes > 0 && vmOut.bytes + len + VM_DATAGRAM_OVERHEAD > vmCapBytes )
		return qfalse;
	vmOut.bytes += len + VM_DATAGRAM_OVERHEAD;
	vmOut.total += len + VM_DATAGRAM_OVERHEAD;
	if ( vmOut.bytes > vmOut.peak )
		vmOut.peak = vmOut.bytes;
	sendto( vmSocket, (const char *)datagram, len, BG_SEND_FLAGS, (const struct sockaddr *)to, sizeof( *to ) );
	return qtrue;
}

// Every answer says that this master knows the clan radio and the statuses: a client sends neither, and no
// long state, until it read that.
static void VM_Answer( const struct sockaddr_in *to, int client, unsigned int token, int flags, int64_t now ) {
	voicePacket_t p;

	memset( &p, 0, sizeof( p ) );
	p.type = VOICE_STATE;
	p.client = client;
	p.token = token;
	p.flags = flags | VOICE_FLAG_SOCIAL;
	VM_Send( to, &p, now, qtrue );
}

/*
=================
VM_State

A client says where it is and whom it hears. Without the token of that address, room and number it is told the
token and nothing else happens. With it the session begins or goes on, and the answer carries the same token
again: that is how the client knows that it is in, or by the flag why not.

A client whose game server starts another level is in another room from then on (Voice_MasterRoom of
cg_voice.c): it has another token there and its session here ends by itself.
=================
*/
static void VM_State( const struct sockaddr_in *from, const voicePacket_t *p, int len, int64_t now ) {
	const byte		*key, *clan;
	unsigned int	hear, allow, token;
	int				index, flags = 0;
	vmSession_t		*s;

	BG_VoiceStateUnpack( p, &key, &hear, &allow );
	token = VM_Token( &from->sin_addr, key, p->client );
	if ( p->token != token ) {
		VM_Answer( from, p->client, token, 0, now );
		return;
	}
	index = VM_FindSession( token, &from->sin_addr, p->client );
	if ( index < 0 )
		index = VM_Join( from, key, p->client, token, &flags );
	if ( index < 0 ) {
		VM_Answer( from, p->client, token, flags, now );
		return;
	}
	s = &vmSessions[index];
	if ( !BG_VoiceLimit( &s->limit, (int)( now & 0x7fffffff ), len ) ) {
		vmOut.limited++;
		return;
	}
	s->addr = *from;
	s->hear = hear;
	s->allow = allow;
	s->lastHeard = now;
	// the long state: he knows the statuses, and maybe he has a clan
	s->social = ( p->dataLen == VOICE_STATE_LONG_BYTES ) ? qtrue : qfalse;
	clan = BG_VoiceStateClan( p );
	if ( clan )
		memcpy( s->clan, clan, VOICE_CLAN_BYTES );
	else
		memset( s->clan, 0, VOICE_CLAN_BYTES );
	VM_Answer( from, p->client, token, 0, now );
}

// The session a datagram with sound or a status came from, with what it sent counted. NULL: there is none, or
// it sent more than a speaker may.
static vmSession_t *VM_Speaker( const struct sockaddr_in *from, const voicePacket_t *p, int len, int64_t now ) {
	const int	index = VM_FindSession( p->token, &from->sin_addr, p->client );
	vmSession_t	*s;

	if ( index < 0 ) {
		vmOut.refused++;
		return NULL;
	}
	s = &vmSessions[index];
	if ( !BG_VoiceLimit( &s->limit, (int)( now & 0x7fffffff ), len ) ) {
		vmOut.limited++;
		return NULL;
	}
	s->addr = *from;
	s->lastHeard = now;
	return s;
}

static void VM_Pass( const vmSession_t *listener, voicePacket_t *out, int64_t now ) {
	out->token = listener->token;
	if ( VM_Send( &listener->addr, out, now, qfalse ) )
		vmOut.relayed++;
	else
		vmOut.capped++;
}

/*
=================
VM_Audio

What one speaker said goes to the members of his room who want to hear him and whom he lets hear him. The two
masks are the range of the voice chat: each client says who is near it as its own game shows it, and a voice
passes only where both ends agree. So a listener who asks for everybody gets the speakers who see him near
them and no others.

One datagram in is at most one out per member of the room, and only to addresses that came back with their
token, so this port can not be used to flood somebody else.
=================
*/
static void VM_Audio( const struct sockaddr_in *from, const voicePacket_t *p, int len, int64_t now ) {
	const vmSession_t	*s = VM_Speaker( from, p, len, now );
	const vmRoom_t		*room;
	voicePacket_t		out = *p;
	int					i;

	if ( !s )
		return;
	room = &vmRooms[s->room];
	out.flags = 0;
	// no relay here knows how far away he is. The listener does, from its game, as long as it is shown the
	// speaker: this is for the moment after, and far away.
	out.distance = 255;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const vmSession_t *listener;

		if ( i == p->client || room->member[i] < 0 )
			continue;
		listener = &vmSessions[room->member[i]];
		if ( !( listener->hear & ( 1u << p->client ) ) || !( s->allow & ( 1u << i ) ) )
			continue;
		VM_Pass( listener, &out, now );
	}
}

/*
=================
VM_Radio

What one speaker said for his clan goes to the members of his room who named the same clan, wherever their game
has them, and to nobody else: as a VOICE_AUDIO with the flags the relay of a game server gives such a frame
(G_VoiceRadio of g_voice.c), so a client plays it the same from either. The masks of the range do not count
here. A speaker without a clan reaches nobody.
=================
*/
static void VM_Radio( const struct sockaddr_in *from, const voicePacket_t *p, int len, int64_t now ) {
	static const byte	none[VOICE_CLAN_BYTES];
	const vmSession_t	*s = VM_Speaker( from, p, len, now );
	const vmRoom_t		*room;
	voicePacket_t		out = *p;
	int					i;

	if ( !s || !memcmp( s->clan, none, VOICE_CLAN_BYTES ) )
		return;
	room = &vmRooms[s->room];
	out.type = VOICE_AUDIO;
	out.flags = VOICE_FLAG_RADIO | VOICE_FLAG_UNSEEN;
	out.distance = 0;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const vmSession_t *listener;

		if ( i == p->client || room->member[i] < 0 )
			continue;
		listener = &vmSessions[room->member[i]];
		if ( memcmp( listener->clan, s->clan, VOICE_CLAN_BYTES ) )
			continue;
		VM_Pass( listener, &out, now );
	}
}

/*
=================
VM_Status

What stands above the head of one player (away, the music he listens to) goes to the other members of his room
that know such a datagram, as it came. Nothing of it is kept or written down: a client says its status again
every few seconds, which is also how somebody who joins later learns it. One a second from a session at most,
and none with a control character in its text.
=================
*/
static void VM_Status( const struct sockaddr_in *from, const voicePacket_t *p, int len, int64_t now ) {
	vmSession_t		*s = VM_Speaker( from, p, len, now );
	const vmRoom_t	*room;
	voicePacket_t	out = *p;
	char			text[VOICE_STATUS_TEXT];
	qboolean		away;
	int				i;

	if ( !s )
		return;
	if ( !s->social || !BG_VoiceStatusUnpack( p, &away, text, sizeof( text ) ) ) {
		vmOut.refused++;
		return;
	}
	if ( s->statusTime && now - s->statusTime < VM_STATUS_MSEC ) {
		vmOut.limited++;
		return;
	}
	s->statusTime = now;
	room = &vmRooms[s->room];
	out.flags = out.distance = 0;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( i == p->client || room->member[i] < 0 || !vmSessions[room->member[i]].social )
			continue;
		VM_Pass( &vmSessions[room->member[i]], &out, now );
	}
}

static void VM_Receive( int64_t now ) {
	int count;

	for ( count = 0; count < VM_MAX_PER_WAKE; count++ ) {
		byte				datagram[VOICE_MAX_DATAGRAM + 1];	// one more: a longer one shows as too long
		struct sockaddr_in	from;
		socklen_t			fromLen = sizeof( from );
		voicePacket_t		p;
		const int			len = (int)recvfrom( vmSocket, (char *)datagram, sizeof( datagram ), 0, (struct sockaddr *)&from, &fromLen );

		if ( len < 0 ) {
			if ( BG_SocketReset() )
				continue;
			break;
		}
		if ( fromLen < (socklen_t)sizeof( from ) || from.sin_family != AF_INET
			|| !BG_VoiceUnpack( datagram, len, qfalse, &p ) || p.client >= MAX_CLIENTS ) {
			vmOut.refused++;
			continue;
		}
		if ( p.type == VOICE_STATE )
			VM_State( &from, &p, len, now );
		else if ( p.type == VOICE_AUDIO )
			VM_Audio( &from, &p, len, now );
		else if ( p.type == VOICE_RADIO )
			VM_Radio( &from, &p, len, now );
		else if ( p.type == VOICE_STATUS )
			VM_Status( &from, &p, len, now );
		else
			vmOut.refused++;
	}
}

static void VM_Sweep( int64_t now ) {
	int i;

	for ( i = 0; i < VM_MAX_SESSIONS; i++ ) {
		if ( vmSessions[i].used && now - vmSessions[i].lastHeard >= VM_SESSION_MSEC )
			VM_Leave( i );
	}
}

// One line of what happened since the last one, if anything did. It names no address and no room.
static void VM_Stats( int64_t msec ) {
	if ( vmSessionCount || vmOut.relayed || vmOut.refused || vmOut.limited || vmOut.capped ) {
		printf( "%i sessions in %i rooms. %u datagrams handed on, %i kbit/s (at most %i in a second). "
			"Not taken: %u that were no voice of a session, %u over the limit of a speaker, %u over -maxkbps\n",
			vmSessionCount, vmRoomCount, vmOut.relayed, (int)( vmOut.total * 8 / ( msec > 0 ? msec : 1 ) ),
			vmOut.peak * 8 / 1000, vmOut.refused, vmOut.limited, vmOut.capped );
	}
	vmOut.total = 0;
	vmOut.peak = 0;
	vmOut.relayed = vmOut.refused = vmOut.limited = vmOut.capped = 0;
}

static void VM_Signal( int sig ) {
	(void)sig;
	vmQuit = 1;
}

static void VM_Usage( const char *name ) {
	fprintf( stderr, "usage: %s [-port <UDP port, %i>] [-ip <address to listen on, all>] [-maxkbps <what may go out, %i, 0: anything>]\n",
		name, VOICE_MASTER_PORT, VM_DEFAULT_KBPS );
}

// a whole number from min to max, or -1
static long VM_Number( const char *text, long min, long max ) {
	char		*end;
	const long	value = strtol( text, &end, 10 );

	return ( end != text && !*end && value >= min && value <= max ) ? value : -1;
}

int main( int argc, char **argv ) {
	struct sockaddr_in	addr;
	long				port = VOICE_MASTER_PORT, kbps = VM_DEFAULT_KBPS;
	int64_t				lastSweep, lastStats;
	int					i;

	memset( &addr, 0, sizeof( addr ) );
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl( INADDR_ANY );
	for ( i = 1; i < argc; i++ ) {
		const char *value = ( i + 1 < argc ) ? argv[i + 1] : "";

		if ( !strcmp( argv[i], "-port" ) && ( port = VM_Number( value, 1, 65535 ) ) > 0 ) {
			i++;
		} else if ( !strcmp( argv[i], "-maxkbps" ) && ( kbps = VM_Number( value, 0, 1000000 ) ) >= 0 ) {
			i++;
		} else if ( !strcmp( argv[i], "-ip" ) && inet_pton( AF_INET, value, &addr.sin_addr ) == 1 ) {
			i++;
		} else {
			VM_Usage( argv[0] );
			return 1;
		}
	}
	addr.sin_port = htons( (unsigned short)port );
	vmCapBytes = (int)( kbps * 125 );

	if ( !BG_Random( vmSecret, sizeof( vmSecret ) ) ) {
		fprintf( stderr, "The system gives no random numbers, so nobody could be given a token\n" );
		return 1;
	}
	for ( i = 0; i < VM_BUCKETS; i++ )
		vmByToken[i] = vmByRoom[i] = -1;

	BG_NetStart();
	vmSocket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( vmSocket == BG_INVALID_SOCKET || bind( vmSocket, (struct sockaddr *)&addr, sizeof( addr ) ) != 0 ) {
		fprintf( stderr, "UDP port %li can not be opened\n", port );
		return 1;
	}
	BG_SocketNonBlocking( vmSocket );
	signal( SIGINT, VM_Signal );
	signal( SIGTERM, VM_Signal );
	setvbuf( stdout, NULL, _IONBF, 0 );		// a line is there when it was said, also in a log that is no terminal
	if ( kbps > 0 )
		printf( "Voice master on UDP port %li, at most %li kbit/s go out\n", port, kbps );
	else
		printf( "Voice master on UDP port %li\n", port );

	lastSweep = lastStats = VM_Msec();
	while ( !vmQuit ) {
		fd_set			readable;
		struct timeval	wait;
		int64_t			now;

		FD_ZERO( &readable );
		FD_SET( vmSocket, &readable );
		wait.tv_sec = 1;
		wait.tv_usec = 0;
		// all the waiting there is: for the next datagram, and no longer than until the next look at the clock
		if ( select( (int)vmSocket + 1, &readable, NULL, NULL, &wait ) > 0 )
			VM_Receive( VM_Msec() );

		now = VM_Msec();
		if ( now - lastSweep >= VM_SWEEP_MSEC ) {
			VM_Sweep( now );
			lastSweep = now;
		}
		if ( now - lastStats >= VM_STATS_MSEC ) {
			VM_Stats( now - lastStats );
			lastStats = now;
		}
	}

	// what happened since the last line, for whoever stopped it
	VM_Stats( VM_Msec() - lastStats );
	BG_CloseSocket( vmSocket );
	return 0;
}
