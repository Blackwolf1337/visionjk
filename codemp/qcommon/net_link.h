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

/*
net_link.h -- the tunnel and the link of a temple instance. Not from OpenJK, see docs/japlus/server.md, "The
temple of a server".

A dedicated server of this tree starts a second one for its players, the Vision Temple (server/sv_instance.cpp).
That one has no port a player could reach. Its players send to the port of the game of the first server, with
a mark in front of every packet, and get their answers from that port with the same mark:

the tunnel		mark[4] instance		the packet, as it would be without the tunnel

mark: the four bytes of NET_TUNNEL_MARK. No packet of the game starts with them: it is no sequence number a
connection reaches, and the one of a connectionless packet is -1. instance: NET_TUNNEL_TEMPLE.

Between the two programs the packets go over the link: one UDP socket each on 127.0.0.1. The first server
makes its end and says its port and a key in the environment of the one it starts (NET_LINK_ENV), which opens
its end, says hello, and from then on has that socket in the place of one on a network (qcommon/net_ip.cpp):

the link		key[8] flags address[4] port[2]		the packet

address and port: the player the packet is from, or is for, as they go over the network. key: what the two
agreed on. Each end takes nothing without it and nothing from another port than the one of the other end.
*/

#pragma once

#define NET_TUNNEL_MARK		( -2 )
#define NET_TUNNEL_HEAD		5
#define NET_TUNNEL_TEMPLE	1

#define NET_LINK_ENV		"VISIONJK_LINK"		// "<port>:<the key as 16 hex digits>"
#define NET_LINK_KEY		8
#define NET_LINK_HEAD		( NET_LINK_KEY + 7 )
#define NET_LINK_PLAIN		1		// from the instance: this leaves without the mark (a datagram of the voice chat)
#define NET_LINK_HELLO		2		// no packet: the sender is there. Said once a second by both ends
#define NET_LINK_QUIT		4		// no packet, to the instance: the server ends
#define NET_LINK_HELLO_MSEC	1000
#define NET_LINK_LOST_MSEC	30000	// nothing from the other end for this long: it is gone. (A server that loads a level says nothing meanwhile.)

// net_ip.cpp: the end of the instance
qboolean	NET_LinkActive( void );				// this program is an instance: its packets go over the link
void		NET_LinkPlain( qboolean plain );	// what is sent from now on leaves the server without the mark
void		NET_LinkFrame( void );
