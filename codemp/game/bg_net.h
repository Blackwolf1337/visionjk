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

/*
bg_net.h -- sockets as the modules open them next to the ones of the engine: the web admin panel
(g_webadmin.c), the voice relay (g_voice.c) and the voice chat of the client (cg_voice.c). Not from JA+.

Include it in front of the other headers of a file: on Windows winsock2.h has to come before windows.h.
*/

#pragma once

#ifdef _WIN32
	#ifndef _CRT_RAND_S
		#define _CRT_RAND_S		// rand_s(), the random source of BG_Random()
	#endif
	#include <stdlib.h>
	#include <winsock2.h>
	#include <ws2tcpip.h>
	typedef SOCKET bgSocket_t;
	#define BG_INVALID_SOCKET		INVALID_SOCKET
	#define BG_CloseSocket			closesocket
	#define BG_SocketWouldBlock()	( WSAGetLastError() == WSAEWOULDBLOCK )
	// what recvfrom() of a UDP socket says after a datagram it sent was refused: not an error of this socket
	#define BG_SocketReset()		( WSAGetLastError() == WSAECONNRESET )
	#define BG_SEND_FLAGS			0
#else
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <unistd.h>
	#include <fcntl.h>
	#include <errno.h>
	typedef int bgSocket_t;
	#define BG_INVALID_SOCKET		( -1 )
	#define BG_CloseSocket			close
	#define BG_SocketWouldBlock()	( errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR )
	#define BG_SocketReset()		( errno == ECONNREFUSED )
	#ifdef MSG_NOSIGNAL
		#define BG_SEND_FLAGS		MSG_NOSIGNAL
	#else
		#define BG_SEND_FLAGS		0
	#endif
#endif

// Winsock has to be started in a process before a socket is made. The engine did that for its own, this adds
// one to its count.
static inline void BG_NetStart( void ) {
#ifdef _WIN32
	WSADATA wsa;

	WSAStartup( MAKEWORD( 2, 2 ), &wsa );
#endif
}

// no call on this socket waits from here on
static inline void BG_SocketNonBlocking( bgSocket_t s ) {
#ifdef _WIN32
	u_long on = 1;

	ioctlsocket( s, FIONBIO, &on );
#else
	fcntl( s, F_SETFL, fcntl( s, F_GETFL, 0 ) | O_NONBLOCK );
#endif
}

// len bytes from the random source of the system. 0: there is none, and the caller must not go on with a guess.
static inline int BG_Random( void *out, int len ) {
#ifdef _WIN32
	unsigned char	*p = (unsigned char *)out;
	unsigned int	v = 0;
	int				i;

	for ( i = 0; i < len; i++ ) {
		if ( !( i & 3 ) && rand_s( &v ) != 0 )
			return 0;
		p[i] = (unsigned char)( v >> ( ( i & 3 ) * 8 ) );
	}
	return 1;
#else
	const int	fd = open( "/dev/urandom", O_RDONLY );
	int			got = 0;

	if ( fd < 0 )
		return 0;
	while ( got < len ) {
		const int n = (int)read( fd, (char *)out + got, (size_t)( len - got ) );

		if ( n <= 0 )
			break;
		got += n;
	}
	close( fd );
	return ( got == len ) ? 1 : 0;
#endif
}
