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

// sv_voice.cpp -- the voice chat on the UDP port of the game. Not from OpenJK, see docs/japlus/voice.md.
//
// The relay of the voice chat is in the game module (codemp/game/voice/g_voice.c) and had a port of its own,
// the one after the port of the game. With vs_voiceShared 1 its datagrams come in on the port of the game: they
// start with "VO" and the version of that protocol (game/voice/bg_voice.h), which no packet of the game does
// but one (see SV_PacketEvent), and are handed to the module, which sends its answers back through here. A
// server then needs one port. Nothing changes for a client: it sends to the port the server names in "vs_voiceLink".

#include "server.h"
#include "sv_gameapi.h"
#include "qcommon/net_local.h"
#include "game/voice/bg_voice.h"

static cvar_t	*vs_voiceShared;

/*
===============
SV_GameSendDatagram

ext.SendDatagram of the game imports: an answer of the voice chat, out of the UDP socket of the game. ip: the
four bytes of an IPv4 address as they go over the network.
===============
*/
void SV_GameSendDatagram( const byte *ip, int port, const byte *data, int length ) {
	netadr_t to;

	if ( length <= 0 || length > VOICE_MAX_DATAGRAM ) {
		return;
	}
	Com_Memset( &to, 0, sizeof( to ) );
	to.type = NA_IP;
	Com_Memcpy( to.ip, ip, sizeof( to.ip ) );
	to.port = BigShort( (short)port );
	NET_SendPacket( NS_SERVER, length, data, &to );
}

/*
===============
SV_VoicePacket

A packet that is not one of a client from this very address and port (SV_PacketEvent). qtrue: it is a datagram
of the voice chat and is done with.
===============
*/
qboolean SV_VoicePacket( const netadr_t *from, msg_t *msg ) {
	const byte *data = msg->data;

	if ( !vs_voiceShared->integer || from->type != NA_IP || msg->cursize < VOICE_HEAD_TO_SERVER
		|| data[0] != 'V' || data[1] != 'O' || data[2] != VOICE_PROTOCOL ) {
		return qfalse;
	}
	// not from an address the intrusion detection has blocked, see sv_ids.cpp
	if ( SV_IDS_Blocked( from ) ) {
		return qtrue;
	}
	GVM_VoiceDatagram( from, data, msg->cursize );
	return qtrue;
}

void SV_VoiceInit( void ) {
#if defined( DEDICATED )
	vs_voiceShared = Cvar_Get( "vs_voiceShared", "1", CVAR_INIT, "1: the voice chat comes in on the UDP port of the game, the server needs no second port. 0: on a port of its own (vs_voicePort)" );
#else
	// (a game this program starts for its own player keeps the port of its own)
	vs_voiceShared = Cvar_Get( "vs_voiceShared", "0", CVAR_INIT, "1: the voice chat comes in on the UDP port of the game, the server needs no second port. 0: on a port of its own (vs_voicePort)" );
#endif
	// what the game module answers (g_voice.c): it takes its datagrams from here
	Cvar_Get( "vs_voiceSharedGame", "0", CVAR_ROM, "" );
}
