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

// cg_peers.c -- who else plays with a client of this tree. Not from JA+. The scoreboard marks them and the card
// of a player says it (hud/cg_scores.c, select/cg_select.c).
//
// A game module of this tree knows it from the userinfo of every client (cjp_vision, which the cgame sets to
// its version) and tells a cgame that asks for it (cjp_caps bit 1): the server command
//
//   jpclient <client> <version> [<client> <version> ...]		version: of his VisionJK, "-" for any other client
//
// It says in the server info that it does (g_visionCaps bit 3), see game/status/g_status.c.
//
// On any other server nobody tells. There a client of this tree is known by what comes from it through the
// voice chat (voice/cg_voice.c): its voice, or what stands above its head, which every client says to the voice
// master every five seconds. So it is known of the players whose voice chat is linked to the master, which is
// how a client starts out, and without a version. That somebody plays with something else is never known
// there: who was not heard may have the voice chat switched off.

#include "cgame/cg_local.h"
#include "qcommon/game_version.h"	// GIT_TAG

#define PEER_VERSION	24

typedef enum {
	PEER_UNKNOWN,
	PEER_OTHER,						// the server says: no client of this tree
	PEER_VISION
} peerKind_t;

static struct {
	peerKind_t	kind[MAX_CLIENTS];
	char		version[MAX_CLIENTS][PEER_VERSION];	// as it is shown, "": not known
} peers;

void CG_PeerInit( void ) {
	memset( &peers, 0, sizeof( peers ) );
}

// A version as a player reads it: "v0.1.6" is 0.1.6, and of a build between two releases
// ("v0.1.6-dev.1-3-gf601c8a3") the commit is left out.
static void Peer_Version( const char *tag, char *out, int outSize ) {
	char *cut;

	Q_strncpyz( out, ( tag[0] == 'v' && tag[1] >= '0' && tag[1] <= '9' ) ? tag + 1 : tag, outSize );
	for ( cut = strstr( out, "-g" ); cut; cut = strstr( cut + 1, "-g" ) ) {
		const char *hex = cut + 2;

		while ( ( *hex >= '0' && *hex <= '9' ) || ( *hex >= 'a' && *hex <= 'f' ) ) {
			hex++;
		}
		if ( hex - cut >= 6 && !*hex ) {
			*cut = '\0';
			break;
		}
	}
}

/*
=================
CG_PeerServerCommand

jpclient <client> <version> [<client> <version> ...]
=================
*/
void CG_PeerServerCommand( void ) {
	char	arg[64];
	int		i;

	// with more than one player in it, or with this one: the answer to "jpstatus all", which is no longer asked
	if ( trap->Cmd_Argc() > 3 ) {
		CG_StatusAnswered();
	}
	for ( i = 1; i + 1 < trap->Cmd_Argc(); i += 2 ) {
		int clientNum;

		trap->Cmd_Argv( i, arg, sizeof( arg ) );
		clientNum = atoi( arg );
		if ( clientNum < 0 || clientNum >= MAX_CLIENTS || arg[0] < '0' || arg[0] > '9' ) {
			continue;
		}
		if ( clientNum == cg.clientNum ) {
			CG_StatusAnswered();
		}
		trap->Cmd_Argv( i + 1, arg, sizeof( arg ) );
		Q_StripColor( arg );
		if ( !strcmp( arg, "-" ) || !arg[0] ) {
			peers.kind[clientNum] = PEER_OTHER;
			peers.version[clientNum][0] = '\0';
		} else {
			peers.kind[clientNum] = PEER_VISION;
			Peer_Version( arg, peers.version[clientNum], sizeof( peers.version[clientNum] ) );
		}
	}
}

/*
=================
CG_PeerHeard

Something of this player came through the voice chat, which only a client of this tree speaks. What a server
said of him stays.
=================
*/
void CG_PeerHeard( int clientNum ) {
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS && peers.kind[clientNum] == PEER_UNKNOWN && cgs.clientinfo[clientNum].infoValid ) {
		peers.kind[clientNum] = PEER_VISION;
		peers.version[clientNum][0] = '\0';
	}
}

// he left: whoever gets his number next is somebody else
void CG_PeerLeft( int clientNum ) {
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		peers.kind[clientNum] = PEER_UNKNOWN;
		peers.version[clientNum][0] = '\0';
	}
}

/*
=================
CG_PeerVision

Does this player play with a client of this tree? 1: yes, and *version is which one, "" when that is not known.
0: no, the server says so. -1: nobody knows.
=================
*/
int CG_PeerVision( int clientNum, const char **version ) {
	static char own[PEER_VERSION];

	if ( version ) {
		*version = "";
	}
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !cgs.clientinfo[clientNum].infoValid ) {
		return -1;
	}
	if ( clientNum == cg.clientNum ) {
		if ( !own[0] ) {
			Peer_Version( GIT_TAG, own, sizeof( own ) );
		}
		if ( version ) {
			*version = own;
		}
		return 1;
	}
	if ( peers.kind[clientNum] == PEER_UNKNOWN ) {
		// (a bot is no client at all, whatever the server it is on)
		return ( cgs.clientinfo[clientNum].botSkill != -1 ) ? 0 : -1;
	}
	if ( version ) {
		*version = peers.version[clientNum];
	}
	return ( peers.kind[clientNum] == PEER_VISION ) ? 1 : 0;
}

/*
=================
CG_PeerList_f

peers: who plays with what, as this client knows it, and from where
=================
*/
void CG_PeerList_f( void ) {
	const qboolean	server = (qboolean)( ( atoi( Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "g_visionCaps" ) ) & 8 ) != 0 );
	int				i, count = 0;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const char	*version;
		const int	vision = CG_PeerVision( i, &version );

		if ( !cgs.clientinfo[i].infoValid ) {
			continue;
		}
		trap->Print( "  %2i %s^7: %s%s%s\n", i, cgs.clientinfo[i].name, vision == 1 ? "VisionJK" : vision == 0 ? "another client" : "not known",
			( vision == 1 && version[0] ) ? " " : "", vision == 1 ? version : "" );
		count += ( vision == 1 );
	}
	if ( server ) {
		trap->Print( "%i with VisionJK, told by the server.\n", count );
	} else if ( CG_VoiceSocial() ) {
		trap->Print( "%i with VisionJK. This server tells nothing: known is who was heard through the voice master.\n", count );
	} else {
		trap->Print( "%i with VisionJK. This server tells nothing, and the voice master is not connected (cg_voice, cg_voiceMaster).\n", count );
	}
}
