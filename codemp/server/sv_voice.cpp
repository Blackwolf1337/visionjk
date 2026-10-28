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
//
// The masterserver (the program built with VISION_MASTER) is a dedicated server that carries a relay of its
// own too (codemp/master/ms_relay.c): the one for the servers without the game module
// of this tree, which hears the same kind of datagram. What the module of the masterserver does not take as
// one of its own players goes to it.

#include "server.h"
#include "sv_gameapi.h"
#include "qcommon/net_local.h"
#include "game/voice/bg_voice.h"

#ifdef VISION_MASTER
#include <chrono>
#include "master/ms_relay.h"

static cvar_t	*vs_masterKbps;
static cvar_t	*vs_masterBoxes;
static qboolean	sv_masterUp;
#endif

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

#ifdef VISION_MASTER
// milliseconds of a clock that never goes backwards, for the relay
static int64_t SV_MasterMsec( void ) {
	return std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now().time_since_epoch() ).count();
}

static void SV_MasterSend( const struct sockaddr_in *to, const byte *data, int len ) {
	SV_GameSendDatagram( (const byte *)&to->sin_addr.s_addr, ntohs( to->sin_port ), data, len );
}

static void SV_MasterPrint( const char *line ) {
	Com_Printf( "Masterserver: %s", line );
}

static void SV_MasterDatagram( const netadr_t *from, const byte *data, int length ) {
	struct sockaddr_in addr;

	if ( !sv_masterUp ) {
		return;
	}
	Com_Memset( &addr, 0, sizeof( addr ) );
	addr.sin_family = AF_INET;
	Com_Memcpy( &addr.sin_addr.s_addr, from->ip, 4 );
	addr.sin_port = from->port;		// both as they go over the network
	MS_Datagram( &addr, data, length, SV_MasterMsec() );
}

// what the web panel of the game module shows of the relay (webadmin/wa_settings.c), once a second
static void SV_MasterStatus( void ) {
	int sessions, rooms, kbps;

	MS_Numbers( &sessions, &rooms, &kbps );
	Cvar_Set( "ms_statSessions", va( "%i", sessions ) );
	Cvar_Set( "ms_statRooms", va( "%i", rooms ) );
	Cvar_Set( "ms_statKbps", va( "%i", kbps ) );
}

/*
===============
SV_VoiceFrame

Once a frame of the program: the relay forgets who was not heard of and writes its line of numbers.
===============
*/
void SV_VoiceFrame( void ) {
	static int64_t	boxSetTime;
	const int64_t	now = SV_MasterMsec();

	if ( !sv_masterUp ) {
		return;
	}
	// The boxes of the boombox (ms_boombox.c) play with what the owner set for the game module of this server: the
	// programs (CVAR_INIT, so nobody changes them later), the longest song and the line. Taken again once a second.
	if ( !boxSetTime || now - boxSetTime >= 1000 ) {
		char ytdlp[MAX_OSPATH], ffmpeg[MAX_OSPATH], args[256];

		Cvar_VariableStringBuffer( "vs_boomboxYtdlp", ytdlp, sizeof( ytdlp ) );
		Cvar_VariableStringBuffer( "vs_boomboxFfmpeg", ffmpeg, sizeof( ffmpeg ) );
		Cvar_VariableStringBuffer( "vs_boomboxYtdlpArgs", args, sizeof( args ) );
		MS_BoxSetup( vs_masterBoxes->integer, Cvar_VariableString( "vs_boomboxMaxSeconds" )[0] ? Cvar_VariableIntegerValue( "vs_boomboxMaxSeconds" ) : 600,
			Cvar_VariableString( "vs_boomboxQueue" )[0] ? Cvar_VariableIntegerValue( "vs_boomboxQueue" ) : 10, ytdlp, ffmpeg, args, now );
		boxSetTime = now;
		SV_MasterStatus();
	}
	MS_Frame( now );
}

// the server ends: what the relay did since its last line of numbers
void SV_VoiceShutdown( void ) {
	if ( sv_masterUp ) {
		MS_Shutdown( SV_MasterMsec() );
	}
}
#endif

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
#ifdef VISION_MASTER
	if ( !GVM_VoiceDatagram( from, data, msg->cursize ) ) {
		SV_MasterDatagram( from, data, msg->cursize );
	}
#else
	GVM_VoiceDatagram( from, data, msg->cursize );
#endif
	return qtrue;
}

void SV_VoiceInit( void ) {
#ifdef VISION_MASTER
	vs_voiceShared = Cvar_Get( "vs_voiceShared", "1", CVAR_ROM, "The voice chat comes in on the UDP port of the game" );
	// the game module runs its web panel as the one of the masterserver (webadmin/wa_settings.c)
	Cvar_Get( "vs_master", "1", CVAR_ROM, "This is the masterserver" );
	Cvar_Get( "ms_statSessions", "0", CVAR_ROM, "Sessions of the relay" );
	Cvar_Get( "ms_statRooms", "0", CVAR_ROM, "Rooms of the relay" );
	Cvar_Get( "ms_statKbps", "0", CVAR_ROM, "What the relay sent in the last minute, kbit/s on average" );
	vs_masterKbps = Cvar_Get( "vs_masterKbps", va( "%i", MS_DEFAULT_KBPS ), CVAR_INIT, "What the relay of the masterserver may send, kbit/s. 0: anything" );
	// The programs the boxes start. The game module registers them as CVAR_INIT; registered here too, the
	// masterserver does not depend on which module it runs with to keep a console from setting them later.
	Cvar_Get( "vs_boomboxYtdlp", "yt-dlp", CVAR_INIT, "The yt-dlp program of the boombox" );
	Cvar_Get( "vs_boomboxFfmpeg", "ffmpeg", CVAR_INIT, "The ffmpeg program of the boombox" );
	Cvar_Get( "vs_boomboxYtdlpArgs", "", CVAR_INIT, "Words yt-dlp of the boombox is started with as well" );
	vs_masterBoxes = Cvar_Get( "vs_masterBoxes", "8", CVAR_ARCHIVE, "Songs of the boombox the masterserver plays at once for the servers without the game module of VisionJK, at most 16. 0: no boxes" );
	if ( MS_Init( SV_MasterSend, SV_MasterPrint, Q_max( vs_masterKbps->integer, 0 ) * 125, SV_MasterMsec() ) ) {
		sv_masterUp = qtrue;
		Com_Printf( "Masterserver: its relay listens on the UDP port of the game\n" );
	} else {
		Com_Printf( "Masterserver: no relay, the system gives no random numbers, so nobody could be given a token\n" );
	}
#elif defined( DEDICATED )
	vs_voiceShared = Cvar_Get( "vs_voiceShared", "1", CVAR_INIT, "1: the voice chat comes in on the UDP port of the game, the server needs no second port. 0: on a port of its own (vs_voicePort)" );
#else
	// (a game this program starts for its own player keeps the port of its own)
	vs_voiceShared = Cvar_Get( "vs_voiceShared", "0", CVAR_INIT, "1: the voice chat comes in on the UDP port of the game, the server needs no second port. 0: on a port of its own (vs_voicePort)" );
#endif
	// what the game module answers (g_voice.c): it takes its datagrams from here
	Cvar_Get( "vs_voiceSharedGame", "0", CVAR_ROM, "" );
}
