/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2005 - 2015, ioquake3 contributors
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

// net_local.h -- the sockets of the system as the engine names them, winsock or BSD. This was the head of
// net_ip.cpp and is a file of its own for the second user: the web host of the dedicated server
// (codemp/server/web), whose sockets wait in the select() of NET_Sleep. Include it behind qcommon/qcommon.h.

#pragma once

#ifdef _WIN32
	#include <winsock.h>

	typedef int socklen_t;

	#undef EAGAIN
	#undef EADDRNOTAVAIL
	#undef EAFNOSUPPORT
	#undef ECONNRESET

	#define EAGAIN WSAEWOULDBLOCK
	#define EADDRNOTAVAIL WSAEADDRNOTAVAIL
	#define EAFNOSUPPORT WSAEAFNOSUPPORT
	#define ECONNRESET WSAECONNRESET

	#define socketError WSAGetLastError( )
#else

#if MAC_OS_X_VERSION_MIN_REQUIRED == 1020
        // needed for socklen_t on OSX 10.2
#        define _BSD_SOCKLEN_T_
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef __sun
#include <sys/filio.h>
#endif

typedef int SOCKET;
#define INVALID_SOCKET                -1
#define SOCKET_ERROR                        -1
#define closesocket                                close
#define ioctlsocket                                ioctl
#define socketError                                errno

#endif

char *NET_ErrorString( void );

#ifdef DEDICATED
// sv_web.cpp: what NET_Sleep gives the web host, in front of its select() and behind it
void SV_WebFDSet( fd_set *readSet, fd_set *writeSet, SOCKET *highest );
void SV_WebEvent( fd_set *readSet, fd_set *writeSet );
// and the link to the temple of the dedicated server (codemp/server/sv_instance.cpp)
void SV_InstanceFDSet( fd_set *readSet, SOCKET *highest );
void SV_InstanceEvent( fd_set *readSet );
#endif
