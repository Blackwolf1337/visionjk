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

// cl_visit.cpp -- a visit to another server without leaving the one the player is on. Not from OpenJK.
//
// "visit <address>" connects to another server as "connect" does, and the connection the player has is kept: its
// state is put aside, its packets are read into that state while he is away, and the server gets a command of a
// player who does nothing twenty times a second, so it keeps him. His body stands where he left it. When the
// visit ends (disconnect, an error, the other server drops him) the client is back on the first server: its
// state is taken up again and its level is loaded as after a vid_restart. Made for the Vision Temple
// (cgame/focus/cg_focus.c, game/status/g_temple.c), which is a server of its own.
//
// How: the client code reads the state of a connection through cl and clc, and those are two places now
// (client.h). The one the client works on is the server the player sees. While a packet of the first server is
// read or one is written to it, cl and clc are its place for that moment (Visit_Home()), and the parts of the
// reading that would change the game on the screen are left out (CL_VisitParsing()).
//
// What ends a visit from the side of the first server:
// - it changes or restarts its level: the client comes back and loads it, as it would have done there
// - the body is hurt or dies (the damage count or the kind of movement of its player state changes): a server
//   of this tree protects the body of somebody who meditates (game/status/g_focus.c), any other does not
// - it drops the player, or nothing came from it for cl_timeout seconds: then there is nothing to come back to,
//   and the visit ends as that connection would have ended
//
// What the first server sent meanwhile for the screen (chat, prints) is not shown later. Config strings are
// kept up, so names, scores and settings are right when the cgame starts again.
//
// Not with a server of this program (a listen server): its level is the one the collision code holds, and the
// client would load another one over it. cl_visit is 1 while a visit lasts, cl_visitBack is set to 1 when one
// ended at the first server (the cgame reads and clears it), cl_cgameVisit tells that the engine has all this.
//
// The temple of the server the player is on ("visit <address of that server> 1", server/sv_instance.cpp) has no
// address of its own: it is reached through the port of that server, with a mark in front of every packet of
// the visit, and what it sends comes from that port with the same mark (the tunnel, qcommon/net_link.h). So the
// two servers of such a visit have one address, and the mark is what tells their packets apart, where the
// address does it for any other visit. cl_cgameTunnel tells that the engine knows this, cl_visitFailed is 1
// after a visit that got no answer and 0 after one that arrived.

#include "client.h"
#include "qcommon/net_link.h"
#include "qcommon/stringed_ingame.h"
#include "cl_uiapi.h"

extern cvar_t	*cl_timeout;
void CL_StopRecord_f( void );

#define VISIT_KEEP_MSEC		50			// between two commands to the first server
#define VISIT_CONNECT_MSEC	15000		// a visit the other server did not answer after this long is given up

// the two places of a connection (client.h), 0 is the one the client starts with
static clientActive_t		visit_cl[2];
static clientConnection_t	visit_clc[2];

clientActive_t		*clAt = &visit_cl[0];
clientConnection_t	*clcAt = &visit_clc[0];

static struct {
	qboolean	held;					// a first server is kept
	qboolean	home;					// cl and clc are its place right now
	qboolean	back;					// the visit is over: back at the start of the next frame
	qboolean	lost;					// and there is nothing to come back to
	qboolean	gamestateHeld;			// the first server sent a game state: it is read when the client is back
	qboolean	arrived;				// the other server answered
	int			tunnel;					// the visit goes through the port of the first server: which of its instances, 0: none
	int			there;					// the place of the visit, the other one is the first server's
	int			startTime, keepTime;
	int			damageEvent, pmType;	// of the body when the player left
	char		servername[MAX_OSPATH];
	char		address[MAX_OSPATH], ip[MAX_OSPATH];
	char		why[MAX_STRING_CHARS];
	msg_t		gamestate;
	byte		gamestateData[MAX_MSGLEN];
} visit;

static cvar_t	*cl_visit;

qboolean CL_Visiting( void ) {
	return visit.held;
}

// the state of the first server is being read: nothing of it reaches the game on the screen
qboolean CL_VisitParsing( void ) {
	return visit.home;
}

/*
===============
CL_VisitTunnel

A packet is about to leave for this address (NET_SendPacket()). Not 0: it is one of a visit that goes through
the tunnel, and this is the instance it is for. A packet for the first server goes out while cl and clc are its
place, and has no mark.
===============
*/
int CL_VisitTunnel( const netadr_t *to ) {
	if ( !visit.held || !visit.tunnel || visit.home || !NET_CompareAdr( to, &visit_clc[visit.there].serverAddress ) ) {
		return 0;
	}
	return visit.tunnel;
}

/*
===============
CL_VisitUntunnel

A packet with the mark of the tunnel arrived (CL_PacketEvent()). qtrue: it is one of the visit, and the mark is
off it. qfalse: there is no such visit, and nobody wants the packet.
===============
*/
qboolean CL_VisitUntunnel( const netadr_t *from, msg_t *msg ) {
	if ( !visit.held || !visit.tunnel || visit.back || msg->cursize < NET_TUNNEL_HEAD || msg->data[4] != visit.tunnel
		|| !NET_CompareAdr( from, &visit_clc[visit.there].serverAddress ) ) {
		return qfalse;
	}
	msg->cursize -= NET_TUNNEL_HEAD;
	memmove( msg->data, msg->data + NET_TUNNEL_HEAD, msg->cursize );
	return qtrue;
}

static void Visit_Home( qboolean home ) {
	const int place = home ? !visit.there : visit.there;

	clAt = &visit_cl[place];
	clcAt = &visit_clc[place];
	visit.home = home;
}

static void Visit_Back( const char *why, qboolean lost ) {
	if ( visit.back ) {
		visit.lost = (qboolean)( visit.lost || lost );
		return;
	}
	visit.back = qtrue;
	visit.lost = lost;
	Q_strncpyz( visit.why, why, sizeof( visit.why ) );
}

/*
===============
CL_VisitGamestate

The first server changed its level. Reading its game state loads that level, so it waits until the client is
back, which is now. (CL_ParseServerMessage())
===============
*/
void CL_VisitGamestate( const msg_t *msg ) {
	if ( visit.gamestateHeld || msg->cursize > (int)sizeof( visit.gamestateData ) ) {
		return;
	}
	visit.gamestate = *msg;
	visit.gamestate.data = visit.gamestateData;
	Com_Memcpy( visit.gamestateData, msg->data, msg->cursize );
	visit.gamestateHeld = qtrue;
	Visit_Back( "the server changed its level", qfalse );
}

// The reliable commands of the first server that came with a packet. The config strings are kept up, a level
// that starts again or a player who is dropped ends the visit, the rest was for the screen.
static void Visit_Commands( void ) {
	while ( clc.lastExecutedServerCommand < clc.serverCommandSequence && !visit.lost ) {
		const int	number = clc.lastExecutedServerCommand + 1;
		const char	*s = clc.serverCommands[number & ( MAX_RELIABLE_COMMANDS - 1 )];

		if ( !strncmp( s, "disconnect", 10 ) ) {
			Visit_Back( s[10] ? s + 11 : "", qtrue );
		} else if ( !strncmp( s, "cs ", 3 ) || !strncmp( s, "bcs", 3 ) ) {
			CL_GetServerCommand( number );
		} else if ( !strncmp( s, "map_restart", 11 ) ) {
			Visit_Back( "the server started its level again", qfalse );
		}
		clc.lastExecutedServerCommand = number;
	}
}

/*
===============
CL_VisitPacket

A packet arrived (CL_PacketEvent()). qtrue: it was of the first server and is done with.
===============
*/
qboolean CL_VisitPacket( const netadr_t *from, msg_t *msg ) {
	if ( !visit.held || !NET_CompareAdr( from, &visit_clc[!visit.there].netchan.remoteAddress ) ) {
		return qfalse;
	}
	// (a connectionless one can be from anybody, and nothing the first server would say that way matters now)
	if ( visit.back || msg->cursize < 4 || *(int *)msg->data == -1 ) {
		return qtrue;
	}
	Visit_Home( qtrue );
	if ( CL_Netchan_Process( &clc.netchan, msg ) ) {
		clc.serverMessageSequence = LittleLong( *(int *)msg->data );
		clc.lastPacketTime = cls.realtime;
		CL_ParseServerMessage( msg );
		Visit_Commands();
		if ( cl.snap.valid && ( cl.snap.ps.damageEvent != visit.damageEvent || cl.snap.ps.pm_type != visit.pmType ) ) {
			Visit_Back( "something happened to the body", qfalse );
		}
	}
	Visit_Home( qfalse );
	return qtrue;
}

// the first server hears from the player: he stands and does nothing
static void Visit_Keep( void ) {
	usercmd_t	*cmd, last;

	Visit_Home( qtrue );
	if ( cls.realtime - clc.lastPacketTime > cl_timeout->value * 1000 ) {
		Visit_Back( SE_GetString( "MP_SVGAME_SERVER_CONNECTION_TIMED_OUT" ), qtrue );
	} else {
		last = cl.cmds[cl.cmdNumber & CMD_MASK];
		cl.cmdNumber++;
		cmd = &cl.cmds[cl.cmdNumber & CMD_MASK];
		*cmd = last;
		cmd->forwardmove = cmd->rightmove = cmd->upmove = 0;
		cmd->buttons = 0;
		cmd->generic_cmd = 0;
		// the time of the server as CL_SetCGameTime() works it out
		cmd->serverTime = cls.realtime + cl.serverTimeDelta;
		CL_WritePacket();
	}
	Visit_Home( qfalse );
}

// the first server is told that the player is gone for good, and its state is thrown away
static void Visit_Forget( void ) {
	Visit_Home( qtrue );
	if ( !visit.lost ) {
		CL_AddReliableCommand( "disconnect", qtrue );
		CL_WritePacket();
		CL_WritePacket();
		CL_WritePacket();
	}
	Com_Memset( &cl, 0, sizeof( cl ) );
	Com_Memset( &clc, 0, sizeof( clc ) );
	Visit_Home( qfalse );
	// from here on the client has one connection, and it is in the place of the visit
	Com_Memset( &visit, 0, sizeof( visit ) );
	visit.there = ( clAt == &visit_cl[1] );
	Cvar_Set( "cl_visit", "0" );
}

/*
===============
CL_VisitDrop

The client goes somewhere else altogether (connect, a local game, a demo, quit): it leaves the first server too.
Before CL_Disconnect().
===============
*/
void CL_VisitDrop( void ) {
	if ( visit.held ) {
		Visit_Forget();
	}
}

/*
===============
CL_VisitBroken

At the start of CL_Disconnect(): an error came up while a packet of the first server was read (Com_Error()
leads here). What that server sends can not be read any more, so it is left, and the disconnect is the one of
the visit.
===============
*/
void CL_VisitBroken( void ) {
	if ( visit.held && visit.home ) {
		Visit_Home( qfalse );
		visit.lost = qtrue;
		Visit_Forget();
	}
}

/*
===============
CL_VisitDisconnected

At the end of CL_Disconnect(): the client left the server it visited, or was dropped by it, and the first one is
still there.
===============
*/
void CL_VisitDisconnected( void ) {
	if ( visit.held ) {
		Visit_Back( "", qfalse );
	}
}

/*
===============
Visit_Return

Back on the first server: its state is the one the client works on again, and its level is loaded as after a
vid_restart, or as its new game state says.
===============
*/
static void Visit_Return( void ) {
	const qboolean	lost = visit.lost, gamestateHeld = visit.gamestateHeld;
	char			why[MAX_STRING_CHARS];

	Q_strncpyz( why, visit.why, sizeof( why ) );
	// leave the server of the visit, unless CL_Disconnect() did
	if ( cls.state >= CA_CONNECTED ) {
		CL_AddReliableCommand( "disconnect", qtrue );
		CL_WritePacket();
		CL_WritePacket();
		CL_WritePacket();
	}
	if ( clc.demorecording ) {
		CL_StopRecord_f();
	}
	S_StopAllSounds();
	CL_ShutdownCGame();
	cls.cgameStarted = qfalse;
	Key_SetCatcher( 0 );
	if ( cls.uiStarted ) {
		UIVM_SetActiveMenu( UIMENU_NONE );
	}

	Com_Memset( &cl, 0, sizeof( cl ) );
	Com_Memset( &clc, 0, sizeof( clc ) );
	Visit_Home( qtrue );
	visit.there = !visit.there;			// the place of the first server is the one the client works on from now
	visit.home = qfalse;
	visit.held = qfalse;
	visit.back = qfalse;
	visit.lost = qfalse;
	visit.gamestateHeld = qfalse;
	Q_strncpyz( cls.servername, visit.servername, sizeof( cls.servername ) );
	Cvar_Set( "cl_currentServerAddress", visit.address );
	Cvar_Set( "cl_currentServerIP", visit.ip );
	Cvar_Set( "cl_visit", "0" );
	// what ended the visit is no error of this connection
	Cvar_Set( "com_errorMessage", "" );
	cls.state = CA_CONNECTED;
	if ( why[0] ) {
		Com_Printf( "Back from the visit: %s\n", why );
	}
	if ( lost ) {
		Com_Error( ERR_SERVERDISCONNECT, "%s: %s\n", SE_GetString( "MP_SVGAME_SERVER_DISCONNECTED" ), why );
	}

	Cvar_Set( "cl_visitBack", "1" );
	// the server gets what the player changed meanwhile (his name, his model)
	cvar_modifiedFlags |= CVAR_USERINFO;
	clc.lastPacketTime = cls.realtime;
	if ( gamestateHeld ) {
		CL_ParseGamestate( &visit.gamestate );
		return;
	}
	// the first snapshot that comes after the level is loaded starts the game, not one from before
	cl.newSnapshots = qfalse;
	// the settings and the pure list of this server, which the other one replaced
	CL_SystemInfoChanged();
	FS_ConditionalRestart( clc.checksumFeed );
	CL_DownloadsComplete();
}

/*
===============
CL_VisitFrame

At the start of a frame of the client.
===============
*/
void CL_VisitFrame( void ) {
	if ( !visit.held ) {
		return;
	}
	if ( !visit.arrived && cls.state >= CA_CONNECTED ) {
		visit.arrived = qtrue;
		Cvar_Set( "cl_visitFailed", "0" );
	}
	if ( !visit.back && cls.state < CA_CONNECTED && cls.realtime - visit.startTime > VISIT_CONNECT_MSEC ) {
		Cvar_Set( "cl_visitFailed", "1" );
		Visit_Back( "no answer", qfalse );
	}
	if ( visit.back ) {
		Visit_Return();
		return;
	}
	if ( cls.realtime - visit.keepTime >= VISIT_KEEP_MSEC ) {
		visit.keepTime = cls.realtime;
		Visit_Keep();
	}
}

/*
===============
CL_Visit_f

visit <address> [instance]
===============
*/
static void CL_Visit_f( void ) {
	const char	*server = Cmd_Argv( 1 );
	const int	tunnel = atoi( Cmd_Argv( 2 ) );
	const char	*serverString;
	netadr_t	to;

	if ( Cmd_Argc() < 2 || Cmd_Argc() > 3 || ( tunnel && tunnel != NET_TUNNEL_TEMPLE ) ) {
		Com_Printf( "usage: visit <server> [%i: the temple of the server one is on]\n", NET_TUNNEL_TEMPLE );
		return;
	}
	if ( visit.held ) {
		Com_Printf( "visit: this is a visit already\n" );
		return;
	}
	if ( cls.state != CA_ACTIVE || clc.demoplaying || com_sv_running->integer ) {
		Com_Printf( "visit: only from a server one plays on, and not from a game of this program\n" );
		return;
	}
	// (a name is looked up here, and the client waits for that: the cgame passes numbers)
	if ( !NET_StringToAdr( server, &to ) ) {
		Com_Printf( "Bad server address\n" );
		return;
	}
	if ( to.port == 0 ) {
		to.port = BigShort( PORT_SERVER );
	}
	// (the temple of a server is reached through that server and no other)
	if ( NET_CompareAdr( &to, &clc.netchan.remoteAddress ) != ( tunnel ? qtrue : qfalse ) ) {
		Com_Printf( tunnel ? "visit: the temple of a server is visited from that server\n" : "visit: that is the server one is on\n" );
		return;
	}

	if ( clc.demorecording ) {
		CL_StopRecord_f();
	}
	// the game of the first server is over on this screen: its cgame holds nothing the client needs later
	S_StopAllSounds();
	CL_ShutdownCGame();
	cls.cgameStarted = qfalse;

	Com_Memset( &visit, 0, sizeof( visit ) );
	visit.held = qtrue;
	visit.tunnel = tunnel;
	visit.there = ( clAt == &visit_cl[0] );
	visit.startTime = visit.keepTime = cls.realtime;
	visit.damageEvent = cl.snap.ps.damageEvent;
	visit.pmType = cl.snap.ps.pm_type;
	Q_strncpyz( visit.servername, cls.servername, sizeof( visit.servername ) );
	Cvar_VariableStringBuffer( "cl_currentServerAddress", visit.address, sizeof( visit.address ) );
	Cvar_VariableStringBuffer( "cl_currentServerIP", visit.ip, sizeof( visit.ip ) );
	Visit_Home( qfalse );
	Com_Memset( &cl, 0, sizeof( cl ) );
	Com_Memset( &clc, 0, sizeof( clc ) );
	Cvar_Set( "cl_visit", "1" );

	// and the rest is CL_Connect_f()
	CL_IDS_Connect();
	Q_strncpyz( cls.servername, server, sizeof( cls.servername ) );
	clc.serverAddress = to;
	serverString = NET_AdrToString( &clc.serverAddress );
	Com_Printf( tunnel ? "Visiting the temple of %s\n" : "Visiting %s\n", serverString );
	if ( NET_IsLocalAddress( &clc.serverAddress ) ) {
		cls.state = CA_CHALLENGING;
	} else {
		cls.state = CA_CONNECTING;
		clc.challenge = ( ( rand() << 16 ) ^ rand() ) ^ Com_Milliseconds();
	}
	Key_SetCatcher( 0 );
	clc.connectTime = -99999;
	clc.connectPacketCount = 0;
	Cvar_Set( "cl_currentServerAddress", server );
	Cvar_Set( "cl_currentServerIP", serverString );
}

void CL_VisitInit( void ) {
	cl_visit = Cvar_Get( "cl_visit", "0", CVAR_ROM, "1 while the server one is on is a visit and the one before it is kept" );
	Cvar_Get( "cl_visitBack", "0", CVAR_TEMP, "set to 1 when a visit ended at the server one came from" );
	Cvar_Get( "cl_visitFailed", "0", CVAR_TEMP, "1 after a visit the other server did not answer, 0 after one that arrived" );
	Cvar_Get( "cl_cgameVisit", "1", CVAR_ROM, "" );
	Cvar_Get( "cl_cgameTunnel", "1", CVAR_ROM, "" );
	Cmd_AddCommand( "visit", CL_Visit_f, "Connect to a server and keep the one the player is on, to come back to" );
}

void CL_VisitShutdown( void ) {
	Cmd_RemoveCommand( "visit" );
}
