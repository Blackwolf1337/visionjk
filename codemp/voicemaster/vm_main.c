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

// vm_main.c -- the voice master as a program by itself: the socket, the clock and the options around
// vm_core.c, which is the master. Not from JA+, see docs/japlus/voice.md.
//
// What runs for the players is the masterserver, which carries the same vm_core.c inside a dedicated server
// (codemp/server/sv_voice.cpp). This program is what the checks of the voice chat start, and a master for
// somebody who wants one without a game server.
//
// One thread and one socket, nothing of the engine. It is no game, so it does wait: in select(), for the next
// datagram, a second at most.

#include "game/bg_net.h"		// the sockets, in front of everything else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include "qcommon/q_shared.h"
#include "game/voice/bg_voice.h"
#include "voicemaster/vm_core.h"

#define VM_MAX_PER_WAKE			256		// datagrams taken before the clock is looked at again

static bgSocket_t				vmSocket = BG_INVALID_SOCKET;
static volatile sig_atomic_t	vmQuit;

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

static void VM_SendOut( const struct sockaddr_in *to, const byte *data, int len ) {
	sendto( vmSocket, (const char *)data, len, BG_SEND_FLAGS, (const struct sockaddr *)to, sizeof( *to ) );
}

static void VM_PrintOut( const char *line ) {
	fputs( line, stdout );
}

static void VM_Receive( int64_t now ) {
	int count;

	for ( count = 0; count < VM_MAX_PER_WAKE; count++ ) {
		byte				datagram[VOICE_MAX_DATAGRAM + 1];	// one more: a longer one shows as too long
		struct sockaddr_in	from;
		socklen_t			fromLen = sizeof( from );
		const int			len = (int)recvfrom( vmSocket, (char *)datagram, sizeof( datagram ), 0, (struct sockaddr *)&from, &fromLen );

		if ( len < 0 ) {
			if ( BG_SocketReset() )
				continue;
			break;
		}
		if ( fromLen < (socklen_t)sizeof( from ) )
			continue;
		VM_Datagram( &from, datagram, len, now );
	}
}

static void VM_Signal( int sig ) {
	(void)sig;
	vmQuit = 1;
}

static void VM_Usage( const char *name ) {
	fprintf( stderr, "usage: %s [-port <UDP port, %i>] [-ip <address to listen on, all>] [-maxkbps <what may go out, %i, 0: anything>]\n"
		"	[-temple <[address:]port of the game server that is the Vision Temple, on this host without an address>]\n",
		name, VOICE_MASTER_PORT, VM_DEFAULT_KBPS );
}

// a whole number from min to max, or -1
static long VM_Number( const char *text, long min, long max ) {
	char		*end;
	const long	value = strtol( text, &end, 10 );

	return ( end != text && !*end && value >= min && value <= max ) ? value : -1;
}

// "<address>:<port>" or "<port>" of the game server that is the Vision Temple, as the bytes of an answer
static qboolean VM_TempleServer( const char *text ) {
	char			host[64];
	const char		*colon = strrchr( text, ':' );
	struct in_addr	address;
	byte			server[VOICE_TEMPLE_SERVER];
	long			port;

	memset( &address, 0, sizeof( address ) );
	if ( colon ) {
		if ( colon == text || (size_t)( colon - text ) >= sizeof( host ) )
			return qfalse;
		memcpy( host, text, colon - text );
		host[colon - text] = 0;
		if ( inet_pton( AF_INET, host, &address ) != 1 )
			return qfalse;
	}
	port = VM_Number( colon ? colon + 1 : text, 1, 65535 );
	if ( port < 0 )
		return qfalse;
	memcpy( server, &address.s_addr, 4 );
	server[4] = (byte)( port >> 8 );
	server[5] = (byte)( port & 0xff );
	VM_SetTemple( server );
	return qtrue;
}

int main( int argc, char **argv ) {
	struct sockaddr_in	addr;
	long				port = VOICE_MASTER_PORT, kbps = VM_DEFAULT_KBPS;
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
		} else if ( !strcmp( argv[i], "-temple" ) && VM_TempleServer( value ) ) {
			i++;
		} else {
			VM_Usage( argv[0] );
			return 1;
		}
	}
	addr.sin_port = htons( (unsigned short)port );

	if ( !VM_Init( VM_SendOut, VM_PrintOut, (int)( kbps * 125 ), VM_Msec() ) ) {
		fprintf( stderr, "The system gives no random numbers, so nobody could be given a token\n" );
		return 1;
	}

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

	while ( !vmQuit ) {
		fd_set			readable;
		struct timeval	wait;

		FD_ZERO( &readable );
		FD_SET( vmSocket, &readable );
		wait.tv_sec = 1;
		wait.tv_usec = 0;
		// all the waiting there is: for the next datagram, and no longer than until the next look at the clock
		if ( select( (int)vmSocket + 1, &readable, NULL, NULL, &wait ) > 0 )
			VM_Receive( VM_Msec() );
		VM_Frame( VM_Msec() );
	}

	VM_Shutdown( VM_Msec() );
	BG_CloseSocket( vmSocket );
	return 0;
}
