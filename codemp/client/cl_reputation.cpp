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

// cl_reputation.cpp -- what the intrusion detection of one client learns about a server, for the others. Not from
// OpenJK, see docs/japlus/security.md, which also lists the packets the masterserver answers.
//
// v_idsShare 1: a server the client leaves for the intrusion detection (cl_ids.cpp), or one the game crashed on
// (cl_memwatch.cpp), is reported to the masterserver of v_idsMaster: its address and a word for what happened.
// Only what the client saw over its connection is reported, never a connectionless packet (anybody can send one in
// the name of a server), never what other players or old mod servers cause (an escape character, cvars a server
// may not set). The first report tells the player on the console.
//
// v_idsReputation 1: the list of servers that many networks reported is fetched when the game starts and with the
// server browser, at most every ten minutes. Such a server has an orange IDS in the column of the ping, and the
// first connect to it is refused with the reason; the next one goes through.
//
// The name of the masterserver is looked up on a thread of its own, the rest runs a step a frame: nothing waits.
// With v_ids 0 nothing is sent or fetched.

#include "client.h"
#include "game/voice/bg_voice.h"
#include <SDL.h>

#define REP_LIST			4096
#define REP_QUEUE			8
#define REP_TOLD			32
#define REP_PARTS			128
#define REP_ANSWER_MSEC		4000		// an answer of the masterserver comes within this
#define REP_TRIES			3
#define REP_CHALLENGE_MSEC	50000		// a challenge is good for a minute or more, taken for less
#define REP_LIST_MSEC		600000		// the list is fetched this often at most
#define REP_RESOLVE_MSEC	3600000		// the name is looked up again after this
#define REP_RETRY_MSEC		60000		// after a masterserver that did not answer

typedef enum {
	REP_IDLE,
	REP_CHALLENGE,
	REP_REPORT,
	REP_LISTING
} repState_t;

typedef struct {
	netadr_t	adr;
	int			networks;
} repServer_t;

static cvar_t		*v_idsShare, *v_idsShareTold, *v_idsShareLocal, *v_idsReputation, *v_idsMaster;

static repServer_t	rep_list[REP_LIST], rep_incoming[REP_LIST];
static int			rep_count, rep_incomingCount, rep_parts;
static qboolean		rep_gotPart[REP_PARTS];
static netadr_t		rep_told[REP_TOLD];
static int			rep_toldCount;

static struct {
	netadr_t	server;
	const char	*event;
} rep_queue[REP_QUEUE];
static int			rep_queued;

static repState_t	rep_state;
static netadr_t		rep_master;
static qboolean		rep_masterKnown;
static char			rep_challenge[16];
static int			rep_challengeTime, rep_sentTime, rep_tries, rep_listTime, rep_retryTime, rep_resolveTime;
static qboolean		rep_wantList;

// the lookup of the name of the masterserver
static struct {
	SDL_Thread		*thread;
	volatile int	done;			// the thread is through. It writes this last
	char			name[MAX_OSPATH];
	byte			ip[4];
	qboolean		ok;
	int				port;
} rep_resolve;

// what the masterserver takes, and only these
static const struct {
	clIdsEvent_t	event;
	const char		*name;
} rep_events[] = {
	{ CLIDS_DOWNLOAD, "download" }, { CLIDS_FRAGMENT, "fragment" }, { CLIDS_GAMEDIR, "gamedir" }, { CLIDS_DOWNLOAD_SIZE, "size" },
	{ CLIDS_CVAR_PROTECTED, "protected" }, { CLIDS_MEM_CODE, "code" }, { CLIDS_MEM_EXEC, "exec" }, { CLIDS_MEM_CHILD, "child" },
	{ CLIDS_CRASH, "crash" },
};

static qboolean CL_RepOn( void ) {
	return (qboolean)( v_idsShare && Cvar_VariableIntegerValue( "v_ids" ) );
}

/*
=================
CL_RepQueue

A server the client left for the intrusion detection, or crashed on (cl_ids.cpp)
=================
*/
void CL_RepQueue( const netadr_t *server, clIdsEvent_t event ) {
	const char *name = NULL;

	if ( !CL_RepOn() || !v_idsShare->integer || server->type != NA_IP ) {
		return;
	}
	for ( size_t i = 0; i < ARRAY_LEN( rep_events ); i++ ) {
		if ( rep_events[i].event == event ) {
			name = rep_events[i].name;
		}
	}
	// (a server on the network at home is of no use to anybody else)
	if ( !name || rep_queued >= REP_QUEUE || ( !v_idsShareLocal->integer && Sys_IsLANAddress( server ) ) ) {
		return;
	}
	rep_queue[rep_queued].server = *server;
	rep_queue[rep_queued].event = name;
	rep_queued++;

	if ( !v_idsShareTold->integer ) {
		Cvar_Set( "v_idsShareTold", "1" );
		Com_Printf( S_COLOR_YELLOW "The intrusion detection reports this server to the VisionJK masterserver (%s), so other players are warned:"
			" its address and what happened, nothing about you. The masterserver keeps the first three numbers of your IP address with the report."
			" v_idsShare 0 turns it off.\n", v_idsMaster->string );
	}
}

// how many networks reported a server, 0 for none (the server browser, cl_lan.cpp)
int CL_RepNetworks( const netadr_t *adr ) {
	if ( !v_idsReputation || !v_idsReputation->integer || !Cvar_VariableIntegerValue( "v_ids" ) ) {
		return 0;
	}
	for ( int i = 0; i < rep_count; i++ ) {
		if ( NET_CompareAdr( adr, &rep_list[i].adr ) ) {
			return rep_list[i].networks;
		}
	}
	return 0;
}

/*
=================
CL_RepCheckConnect

Before a connect. May not return: the first connect to a server the masterserver has in its list ends here, with
the reason.
=================
*/
void CL_RepCheckConnect( const netadr_t *adr ) {
	const int networks = CL_RepNetworks( adr );

	if ( !networks ) {
		return;
	}
	for ( int i = 0; i < rep_toldCount; i++ ) {
		if ( NET_CompareAdr( adr, &rep_told[i] ) ) {
			return;
		}
	}
	rep_told[rep_toldCount++ % REP_TOLD] = *adr;
	rep_toldCount = Q_min( rep_toldCount, REP_TOLD );
	Com_Error( ERR_DROP, "Players from %i networks reported %s in the last 7 days: their clients left it for an attack or crashed on it.\nThe VisionJK masterserver has the reports (v_idsReputation). Connect again to join anyway.",
		networks, NET_AdrToString( adr ) );
}

// the server browser asks for a new list (CL_GlobalServers_f)
void CL_RepRefresh( void ) {
	if ( v_idsReputation && v_idsReputation->integer && ( !rep_listTime || Sys_Milliseconds() - rep_listTime >= REP_LIST_MSEC ) ) {
		rep_wantList = qtrue;
	}
}

static int SDLCALL CL_RepResolveThread( void *data ) {
	rep_resolve.ok = Sys_ResolveIPv4( rep_resolve.name, rep_resolve.ip );
	rep_resolve.done = 1;
	return 0;
}

// the name of v_idsMaster is looked up on a thread. qtrue: the address is known.
static qboolean CL_RepMaster( void ) {
	const int now = Sys_Milliseconds();

	if ( rep_resolve.thread ) {
		if ( !rep_resolve.done ) {
			return qfalse;
		}
		SDL_WaitThread( rep_resolve.thread, NULL );		// (it is through, this does not wait)
		rep_resolve.thread = NULL;
		if ( rep_resolve.ok ) {
			Com_Memset( &rep_master, 0, sizeof( rep_master ) );
			rep_master.type = NA_IP;
			Com_Memcpy( rep_master.ip, rep_resolve.ip, 4 );
			rep_master.port = BigShort( (short)rep_resolve.port );
			rep_masterKnown = qtrue;
		} else {
			rep_retryTime = now + REP_RETRY_MSEC;
		}
		rep_resolveTime = now;
	}
	if ( v_idsMaster->modified ) {
		v_idsMaster->modified = qfalse;
		rep_masterKnown = qfalse;
		rep_resolveTime = 0;
		rep_state = REP_IDLE;
		rep_challengeTime = 0;
	}
	if ( rep_masterKnown && now - rep_resolveTime < REP_RESOLVE_MSEC ) {
		return qtrue;
	}
	if ( rep_retryTime && now - rep_retryTime < 0 ) {
		return rep_masterKnown;
	}

	// "name" or "name:port"
	char *colon;

	Q_strncpyz( rep_resolve.name, v_idsMaster->string, sizeof( rep_resolve.name ) );
	colon = strrchr( rep_resolve.name, ':' );
	rep_resolve.port = VOICE_MASTER_PORT;
	if ( colon ) {
		*colon = '\0';
		rep_resolve.port = atoi( colon + 1 );
	}
	if ( !rep_resolve.name[0] || rep_resolve.port <= 0 || rep_resolve.port > 65535 ) {
		rep_retryTime = now + REP_RETRY_MSEC;
		return qfalse;
	}
	rep_resolve.done = 0;
	rep_resolve.thread = SDL_CreateThread( CL_RepResolveThread, "idsmaster", NULL );
	if ( !rep_resolve.thread ) {
		rep_retryTime = now + REP_RETRY_MSEC;
	}
	return rep_masterKnown;
}

static void CL_RepSend( const char *text ) {
	NET_OutOfBandPrint( NS_CLIENT, &rep_master, "%s", text );
	rep_sentTime = Sys_Milliseconds();
}

// the next thing to send, with a challenge that is still good
static void CL_RepNext( void ) {
	if ( rep_queued ) {
		CL_RepSend( va( "idsReport %s %s %s", rep_challenge, NET_AdrToString( &rep_queue[0].server ), rep_queue[0].event ) );
		rep_state = REP_REPORT;
	} else if ( rep_wantList ) {
		rep_incomingCount = rep_parts = 0;
		Com_Memset( rep_gotPart, 0, sizeof( rep_gotPart ) );
		CL_RepSend( va( "idsList %s", rep_challenge ) );
		rep_state = REP_LISTING;
	} else {
		rep_state = REP_IDLE;
	}
	rep_tries = 0;
}

static void CL_RepDrop( void ) {
	if ( rep_queued ) {
		memmove( rep_queue, rep_queue + 1, sizeof( rep_queue[0] ) * --rep_queued );
	}
}

/*
=================
CL_RepFrame

A step a frame: nothing here waits for the masterserver.
=================
*/
void CL_RepFrame( void ) {
	const int now = Sys_Milliseconds();

	if ( !CL_RepOn() ) {
		rep_queued = 0;
		rep_wantList = qfalse;
		return;
	}
	if ( !rep_listTime && v_idsReputation->integer ) {
		rep_wantList = qtrue;		// once at the start of the game
		rep_listTime = 1;
	}
	if ( !rep_queued && !rep_wantList ) {
		return;
	}
	if ( !CL_RepMaster() ) {
		return;
	}

	switch ( rep_state ) {
	case REP_IDLE:
		if ( rep_challengeTime && now - rep_challengeTime < REP_CHALLENGE_MSEC ) {
			CL_RepNext();
		} else if ( !rep_retryTime || now - rep_retryTime >= 0 ) {
			CL_RepSend( "idsChallenge" );
			rep_state = REP_CHALLENGE;
		}
		break;
	case REP_CHALLENGE:
	case REP_REPORT:
	case REP_LISTING:
		if ( now - rep_sentTime < REP_ANSWER_MSEC ) {
			break;
		}
		if ( rep_state == REP_LISTING && rep_parts ) {
			break;		// (a list cut short is not taken, CL_RepListPart)
		}
		if ( ++rep_tries < REP_TRIES ) {
			CL_RepSend( rep_state == REP_CHALLENGE ? "idsChallenge" : rep_state == REP_REPORT
				? va( "idsReport %s %s %s", rep_challenge, NET_AdrToString( &rep_queue[0].server ), rep_queue[0].event )
				: va( "idsList %s", rep_challenge ) );
			break;
		}
		// the masterserver does not answer: what was asked is kept for later, the list waits for the next time
		Com_DPrintf( "The VisionJK masterserver did not answer the intrusion detection\n" );
		rep_wantList = qfalse;
		rep_listTime = now;
		rep_retryTime = now + REP_RETRY_MSEC;
		rep_state = REP_IDLE;
		break;
	}
	if ( rep_state == REP_LISTING && rep_parts && now - rep_sentTime >= REP_ANSWER_MSEC ) {
		rep_wantList = qfalse;
		rep_listTime = now;
		rep_state = REP_IDLE;
	}
}

// idsListResponse <part> <parts> <a.b.c.d:port:networks> ...
static void CL_RepListPart( void ) {
	const int part = atoi( Cmd_Argv( 1 ) ), parts = atoi( Cmd_Argv( 2 ) );

	if ( rep_state != REP_LISTING || parts < 1 || parts > REP_PARTS || part < 1 || part > parts || ( rep_parts && parts != rep_parts )
		|| rep_gotPart[part - 1] ) {
		return;
	}
	rep_parts = parts;
	rep_gotPart[part - 1] = qtrue;
	rep_sentTime = Sys_Milliseconds();
	for ( int i = 3; i < Cmd_Argc() && rep_incomingCount < REP_LIST; i++ ) {
		char		text[64], *last;
		netadr_t	adr;
		int			networks;

		Q_strncpyz( text, Cmd_Argv( i ), sizeof( text ) );
		last = strrchr( text, ':' );
		if ( !last ) {
			continue;
		}
		*last = '\0';
		networks = atoi( last + 1 );
		if ( networks <= 0 || !text[0] || text[strspn( text, "0123456789.:" )] || !NET_StringToAdr( text, &adr ) || adr.type != NA_IP ) {
			continue;
		}
		rep_incoming[rep_incomingCount].adr = adr;
		rep_incoming[rep_incomingCount].networks = networks;
		rep_incomingCount++;
	}
	for ( int i = 0; i < rep_parts; i++ ) {
		if ( !rep_gotPart[i] ) {
			return;
		}
	}
	// the whole list
	Com_Memcpy( rep_list, rep_incoming, sizeof( rep_incoming[0] ) * rep_incomingCount );
	rep_count = rep_incomingCount;
	rep_wantList = qfalse;
	rep_listTime = Sys_Milliseconds();
	Com_DPrintf( "The VisionJK masterserver has %i servers in its list of the intrusion detection\n", rep_count );
	CL_RepNext();
}

/*
=================
CL_RepPacket

A connectionless packet whose command begins with "ids". Only from the masterserver.
=================
*/
void CL_RepPacket( const netadr_t *from, const char *command ) {
	if ( !CL_RepOn() || !rep_masterKnown || !NET_CompareAdr( from, &rep_master ) ) {
		return;
	}
	if ( !Q_stricmp( command, "idsChallengeResponse" ) && rep_state == REP_CHALLENGE ) {
		const char *c = Cmd_Argv( 1 );

		if ( !c[0] || strlen( c ) >= sizeof( rep_challenge ) || c[strspn( c, "0123456789abcdef" )] ) {
			return;
		}
		Q_strncpyz( rep_challenge, c, sizeof( rep_challenge ) );
		rep_challengeTime = Sys_Milliseconds();
		rep_retryTime = 0;
		CL_RepNext();
	} else if ( !Q_stricmp( command, "idsReportResponse" ) && rep_state == REP_REPORT ) {
		if ( !Q_stricmp( Cmd_Argv( 1 ), "ok" ) ) {
			Com_DPrintf( "The VisionJK masterserver took the report of %s\n", NET_AdrToString( &rep_queue[0].server ) );
		}
		CL_RepDrop();		// (also one it would not take: sending it again changes nothing)
		CL_RepNext();
	} else if ( !Q_stricmp( command, "idsListResponse" ) ) {
		CL_RepListPart();
	}
}

void CL_RepInit( void ) {
	v_idsShare = Cvar_Get( "v_idsShare", "1", CVAR_ARCHIVE, "Intrusion detection: 1 a server the client leaves for an attack, or crashes on, is reported to the VisionJK masterserver (its address and what happened), so other players are warned. 0 off" );
	v_idsShareTold = Cvar_Get( "v_idsShareTold", "0", CVAR_ARCHIVE, "Intrusion detection: the player was told once that reports are sent (v_idsShare)" );
	v_idsShareLocal = Cvar_Get( "v_idsShareLocal", "0", 0, "Intrusion detection: 1 servers on the local network are reported too (for a test)" );
	v_idsReputation = Cvar_Get( "v_idsReputation", "1", CVAR_ARCHIVE, "Intrusion detection: 1 the servers many players reported are fetched from the VisionJK masterserver, marked in the server browser and the first connect to one is refused. 0 off" );
	v_idsMaster = Cvar_Get( "v_idsMaster", "master.jka.vision:29170", CVAR_ARCHIVE, "Intrusion detection: the VisionJK masterserver for the reports and the list of servers (v_idsShare, v_idsReputation)" );
	v_idsMaster->modified = qfalse;
}

void CL_RepShutdown( void ) {
	if ( rep_resolve.thread ) {
		// (a name server that does not answer: the thread ends by itself, nothing of it is used any more)
		SDL_DetachThread( rep_resolve.thread );
		rep_resolve.thread = NULL;
	}
}
