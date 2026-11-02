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

// cl_master.cpp -- the VisionJK masterserver as the engine knows it: its address (v_master, which the voice chat of
// the cgame reads too), looked up on a thread of its own, for the intrusion detection (cl_reputation.cpp) and the
// stores of the site (cl_models.cpp). Not the classic master servers of the server browser (sv_master1 and on).
//
// The stores: the game tells the masterserver "storeWatch" every CLM_WATCH_MSEC, which also keeps the way through
// the router of the player open, and the masterserver sends "storeChanged <store> ..." to every game that did when
// somebody uploaded to a store of the site or something was taken down. The engine then fetches that list again.
// Nothing here waits.

#include "client.h"
#include "game/voice/bg_voice.h"
#include <SDL.h>

#define CLM_RESOLVE_MSEC	3600000		// the name is looked up again after this
#define CLM_RETRY_MSEC		60000		// after a name that could not be looked up
#define CLM_WATCH_MSEC		25000		// "storeWatch" this often: a router forgets a way for UDP after 30 s or more

static cvar_t		*v_master;
static netadr_t		clm_adr;
static qboolean		clm_known;
static int			clm_serial;			// counts up with every new address
static int			clm_resolveTime, clm_retryTime, clm_watchTime;

// the lookup of the name
static struct {
	SDL_Thread		*thread;
	volatile int	done;			// the thread is through. It writes this last
	char			name[MAX_OSPATH];
	byte			ip[4];
	qboolean		ok;
	int				port;
} clm_resolve;

static int SDLCALL CL_MasterResolveThread( void *data ) {
	clm_resolve.ok = Sys_ResolveIPv4( clm_resolve.name, clm_resolve.ip );
	clm_resolve.done = 1;
	return 0;
}

// the lookup that came back is taken, or a new one is started when it is due. Once a frame.
static void CL_MasterResolve( void ) {
	const int	now = Sys_Milliseconds();
	char		*colon;

	if ( clm_resolve.thread ) {
		if ( !clm_resolve.done ) {
			return;
		}
		SDL_WaitThread( clm_resolve.thread, NULL );		// (it is through, this does not wait)
		clm_resolve.thread = NULL;
		if ( clm_resolve.ok ) {
			netadr_t adr;

			Com_Memset( &adr, 0, sizeof( adr ) );
			adr.type = NA_IP;
			Com_Memcpy( adr.ip, clm_resolve.ip, 4 );
			adr.port = BigShort( (short)clm_resolve.port );
			if ( !clm_known || !NET_CompareAdr( &adr, &clm_adr ) ) {
				clm_adr = adr;
				clm_serial++;
				clm_watchTime = 0;
			}
			clm_known = qtrue;
		} else {
			clm_retryTime = now + CLM_RETRY_MSEC;
		}
		clm_resolveTime = now;
	}
	if ( v_master->modified ) {
		v_master->modified = qfalse;
		clm_known = qfalse;
		clm_serial++;
		clm_resolveTime = clm_retryTime = 0;
	}
	if ( clm_known && now - clm_resolveTime < CLM_RESOLVE_MSEC ) {
		return;
	}
	if ( clm_retryTime && now - clm_retryTime < 0 ) {
		return;
	}

	// "name" or "name:port"
	Q_strncpyz( clm_resolve.name, v_master->string, sizeof( clm_resolve.name ) );
	colon = strrchr( clm_resolve.name, ':' );
	clm_resolve.port = VOICE_MASTER_PORT;
	if ( colon ) {
		*colon = '\0';
		clm_resolve.port = atoi( colon + 1 );
	}
	if ( !clm_resolve.name[0] || clm_resolve.port <= 0 || clm_resolve.port > 65535 ) {
		clm_known = qfalse;
		clm_retryTime = now + CLM_RETRY_MSEC;
		return;
	}
	clm_resolve.done = 0;
	clm_resolve.thread = SDL_CreateThread( CL_MasterResolveThread, "master", NULL );
	if ( !clm_resolve.thread ) {
		clm_retryTime = now + CLM_RETRY_MSEC;
	}
}

/*
=================
CL_MasterAddress

The address of the masterserver. qfalse: not known yet, or v_master is empty. serial (may be NULL) counts up with
every new address, so a caller knows to forget what it had going with the old one.
=================
*/
qboolean CL_MasterAddress( netadr_t *adr, int *serial ) {
	if ( serial ) {
		*serial = clm_serial;
	}
	if ( !clm_known ) {
		return qfalse;
	}
	*adr = clm_adr;
	return qtrue;
}

// the address as v_master says it, for the lines of the console
const char *CL_MasterName( void ) {
	return v_master->string;
}

/*
=================
CL_MasterFrame

The address is kept looked up, and the masterserver told that this game wants to know of changes to the stores
while one of them is on. The address is looked up only while something wants it.
=================
*/
void CL_MasterFrame( void ) {
#ifndef __EMSCRIPTEN__
	const qboolean watch = CL_ModelsWatching();
#else
	const qboolean watch = qfalse;
#endif

	if ( !watch && !CL_RepWantsMaster() ) {
		return;
	}
	CL_MasterResolve();
	if ( watch && clm_known && ( !clm_watchTime || Sys_Milliseconds() - clm_watchTime >= CLM_WATCH_MSEC ) ) {
		NET_OutOfBandPrint( NS_CLIENT, &clm_adr, "storeWatch" );
		clm_watchTime = Sys_Milliseconds();
	}
}

/*
=================
CL_MasterPacket

A connectionless packet whose command begins with "store". Only from the masterserver:
storeChanged <store> ...   (models, emoji, emotes)
=================
*/
void CL_MasterPacket( const netadr_t *from, const char *command ) {
	if ( !clm_known || !NET_CompareAdr( from, &clm_adr ) || Q_stricmp( command, "storeChanged" ) ) {
		return;
	}
#ifndef __EMSCRIPTEN__
	for ( int i = 1; i < Cmd_Argc(); i++ ) {
		CL_ModelsStoreChanged( Cmd_Argv( i ) );
	}
#endif
}

void CL_MasterInit( void ) {
	v_master = Cvar_Get( "v_master", "master.jka.vision", CVAR_ARCHIVE, "The VisionJK masterserver, a name or an address, with :port if it is not 29170: the voice chat on servers without it, the intrusion detection, the changes of the stores of the site. Empty: none" );
	v_master->modified = qfalse;
}

void CL_MasterShutdown( void ) {
	if ( clm_resolve.thread ) {
		// (a name server that does not answer: the thread ends by itself, nothing of it is used any more)
		SDL_DetachThread( clm_resolve.thread );
		clm_resolve.thread = NULL;
	}
}
