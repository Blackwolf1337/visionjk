/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

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

// cl_cgame.c  -- client system interaction with client game
#include "client.h"
#include "cl_cgameapi.h"
#include "botlib/botlib.h"
#include "FXExport.h"
#include "FxUtil.h"
#include "qcommon/RoffSystem.h"
#include "qcommon/stringed_ingame.h"
#include "ghoul2/G2_gore.h"

extern IHeapAllocator *G2VertSpaceClient;

#include "snd_ambient.h"
#include "qcommon/timing.h"

/*
Ghoul2 Insert End
*/

extern	botlib_export_t	*botlib_export;

extern qboolean loadCamera(const char *name);
extern void startCamera(int time);
extern qboolean getCameraInfo(int time, vec3_t *origin, vec3_t *angles);

/*
====================
CL_GetUserCmd
====================
*/
qboolean CL_GetUserCmd( int cmdNumber, usercmd_t *ucmd ) {
	// cmds[cmdNumber] is the last properly generated command

	// can't return anything that we haven't created yet
	if ( cmdNumber > cl.cmdNumber ) {
		Com_Error( ERR_DROP, "CL_GetUserCmd: %i >= %i", cmdNumber, cl.cmdNumber );
	}

	// the usercmd has been overwritten in the wrapping
	// buffer because it is too far out of date
	if ( cmdNumber <= cl.cmdNumber - CMD_BACKUP ) {
		return qfalse;
	}

	*ucmd = cl.cmds[ cmdNumber & CMD_MASK ];

	return qtrue;
}

/*
====================
CL_GetParseEntityState
====================
*/
qboolean	CL_GetParseEntityState( int parseEntityNumber, entityState_t *state ) {
	// can't return anything that hasn't been parsed yet
	if ( parseEntityNumber >= cl.parseEntitiesNum ) {
		Com_Error( ERR_DROP, "CL_GetParseEntityState: %i >= %i",
			parseEntityNumber, cl.parseEntitiesNum );
	}

	// can't return anything that has been overwritten in the circular buffer
	if ( parseEntityNumber <= cl.parseEntitiesNum - MAX_PARSE_ENTITIES ) {
		return qfalse;
	}

	*state = cl.parseEntities[ parseEntityNumber & ( MAX_PARSE_ENTITIES - 1 ) ];
	return qtrue;
}

/*
====================
CL_GetSnapshot
====================
*/
qboolean CL_GetSnapshot( int snapshotNumber, snapshot_t *snapshot ) {
	clSnapshot_t	*clSnap;
	int				i, count;

	if ( snapshotNumber > cl.snap.messageNum ) {
		Com_Error( ERR_DROP, "CL_GetSnapshot: snapshotNumber > cl.snapshot.messageNum" );
	}

	// if the frame has fallen out of the circular buffer, we can't return it
	if ( cl.snap.messageNum - snapshotNumber >= PACKET_BACKUP ) {
		return qfalse;
	}

	// if the frame is not valid, we can't return it
	clSnap = &cl.snapshots[snapshotNumber & PACKET_MASK];
	if ( !clSnap->valid ) {
		return qfalse;
	}

	// if the entities in the frame have fallen out of their
	// circular buffer, we can't return it
	if ( cl.parseEntitiesNum - clSnap->parseEntitiesNum >= MAX_PARSE_ENTITIES ) {
		return qfalse;
	}

	// write the snapshot
	snapshot->snapFlags = clSnap->snapFlags;
	snapshot->serverCommandSequence = clSnap->serverCommandNum;
	snapshot->ping = clSnap->ping;
	snapshot->serverTime = clSnap->serverTime;
	Com_Memcpy( snapshot->areamask, clSnap->areamask, sizeof( snapshot->areamask ) );
	snapshot->ps = clSnap->ps;
	snapshot->vps = clSnap->vps; //get the vehicle ps
	count = clSnap->numEntities;
	if ( count > MAX_ENTITIES_IN_SNAPSHOT ) {
		Com_DPrintf( "CL_GetSnapshot: truncated %i entities to %i\n", count, MAX_ENTITIES_IN_SNAPSHOT );
		count = MAX_ENTITIES_IN_SNAPSHOT;
	}
	snapshot->numEntities = count;

 	for ( i = 0 ; i < count ; i++ ) {

		int entNum =  ( clSnap->parseEntitiesNum + i ) & (MAX_PARSE_ENTITIES-1) ;

		// copy everything but the ghoul2 pointer
		memcpy(&snapshot->entities[i], &cl.parseEntities[ entNum ], sizeof(entityState_t));
	}

	// FIXME: configstring changes and server commands!!!

	return qtrue;
}

qboolean CL_GetDefaultState(int index, entityState_t *state)
{
	if (index < 0 || index >= MAX_GENTITIES)
	{
		return qfalse;
	}

	if (!(cl.entityBaselines[index].eFlags & EF_PERMANENT))
	{
		return qfalse;
	}

	*state = cl.entityBaselines[index];

	return qtrue;
}

/*
=====================
CL_SetUserCmdValue
=====================
*/
extern float cl_mPitchOverride;
extern float cl_mYawOverride;
extern float cl_mSensitivityOverride;
void CL_SetUserCmdValue( int userCmdValue, float sensitivityScale, float mPitchOverride, float mYawOverride, float mSensitivityOverride, int fpSel, int invenSel ) {
	cl.cgameUserCmdValue = userCmdValue;
	cl.cgameSensitivity = sensitivityScale;
	cl_mPitchOverride = mPitchOverride;
	cl_mYawOverride = mYawOverride;
	cl_mSensitivityOverride = mSensitivityOverride;
	cl.cgameForceSelection = fpSel;
	cl.cgameInvenSelection = invenSel;
}

int gCLTotalClientNum = 0;
//keep track of the total number of clients
extern cvar_t	*cl_autolodscale;
//if we want to do autolodscaling

void CL_DoAutoLODScale(void)
{
	float finalLODScaleFactor = 0;

	if ( gCLTotalClientNum >= 8 )
	{
		finalLODScaleFactor = (gCLTotalClientNum/-8.0f);
	}


	Cvar_Set( "r_autolodscalevalue", va("%f", finalLODScaleFactor) );
}

/*
=====================
CL_ConfigstringModified
=====================
*/
void CL_ConfigstringModified( void ) {
	char		*old, *s;
	int			i, index;
	char		*dup;
	gameState_t	oldGs;
	int			len;

	index = atoi( Cmd_Argv(1) );
	if ( index < 0 || index >= MAX_CONFIGSTRINGS ) {
		Com_Error( ERR_DROP, "CL_ConfigstringModified: bad index %i", index );
	}
	// get everything after "cs <num>"
	s = Cmd_ArgsFrom(2);

	old = cl.gameState.stringData + cl.gameState.stringOffsets[ index ];
	if ( !strcmp( old, s ) ) {
		return;		// unchanged
	}

	// build the new gameState_t
	oldGs = cl.gameState;

	Com_Memset( &cl.gameState, 0, sizeof( cl.gameState ) );

	// leave the first 0 for uninitialized strings
	cl.gameState.dataCount = 1;

	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		if ( i == index ) {
			dup = s;
		} else {
			dup = oldGs.stringData + oldGs.stringOffsets[ i ];
		}
		if ( !dup[0] ) {
			continue;		// leave with the default empty string
		}

		len = strlen( dup );

		if ( len + 1 + cl.gameState.dataCount > MAX_GAMESTATE_CHARS ) {
			Com_Error( ERR_DROP, "MAX_GAMESTATE_CHARS exceeded" );
		}

		// append it to the gameState string buffer
		cl.gameState.stringOffsets[ i ] = cl.gameState.dataCount;
		Com_Memcpy( cl.gameState.stringData + cl.gameState.dataCount, dup, len + 1 );
		cl.gameState.dataCount += len + 1;
	}

	if (cl_autolodscale && cl_autolodscale->integer)
	{
		if (index >= CS_PLAYERS &&
			index < CS_G2BONES)
		{ //this means that a client was updated in some way. Go through and count the clients.
			int clientCount = 0;
			i = CS_PLAYERS;

			while (i < CS_G2BONES)
			{
				s = cl.gameState.stringData + cl.gameState.stringOffsets[ i ];

				if (s && s[0])
				{
					clientCount++;
				}

				i++;
			}

			gCLTotalClientNum = clientCount;

#ifdef _DEBUG
			Com_DPrintf("%i clients\n", gCLTotalClientNum);
#endif

			CL_DoAutoLODScale();
		}
	}

	if ( index == CS_SYSTEMINFO ) {
		// parse serverId and other cvars
		CL_SystemInfoChanged();
	}

}
#ifndef MAX_STRINGED_SV_STRING
	#define MAX_STRINGED_SV_STRING 1024
#endif
// just copied it from CG_CheckSVStringEdRef(
void CL_CheckSVStringEdRef(char *buf, const char *str)
{ //I don't really like doing this. But it utilizes the system that was already in place.
	int i = 0;
	int b = 0;
	int strLen = 0;
	qboolean gotStrip = qfalse;

	if (!str || !str[0])
	{
		if (str)
		{
			strcpy(buf, str);
		}
		return;
	}

	strcpy(buf, str);

	strLen = strlen(str);

	if (strLen >= MAX_STRINGED_SV_STRING)
	{
		return;
	}

	while (i < strLen && str[i])
	{
		gotStrip = qfalse;

		if (str[i] == '@' && (i+1) < strLen)
		{
			if (str[i+1] == '@' && (i+2) < strLen)
			{
				if (str[i+2] == '@' && (i+3) < strLen)
				{ //@@@ should mean to insert a StringEd reference here, so insert it into buf at the current place
					char stringRef[MAX_STRINGED_SV_STRING];
					int r = 0;

					while (i < strLen && str[i] == '@')
					{
						i++;
					}

					while (i < strLen && str[i] && str[i] != ' ' && str[i] != ':' && str[i] != '.' && str[i] != '\n')
					{
						stringRef[r] = str[i];
						r++;
						i++;
					}
					stringRef[r] = 0;

					buf[b] = 0;
					Q_strcat(buf, MAX_STRINGED_SV_STRING, SE_GetString("MP_SVGAME", stringRef));
					b = strlen(buf);
				}
			}
		}

		if (!gotStrip)
		{
			buf[b] = str[i];
			b++;
		}
		i++;
	}

	buf[b] = 0;
}
/*
===================
CL_GetServerCommand

Set up argc/argv for the given command
===================
*/
qboolean CL_GetServerCommand( int serverCommandNumber ) {
	char	*s;
	char	*cmd;
	static char bigConfigString[BIG_INFO_STRING];

	// if we have irretrievably lost a reliable command, drop the connection
	if ( serverCommandNumber <= clc.serverCommandSequence - MAX_RELIABLE_COMMANDS )
	{
		int i = 0;

		// when a demo record was started after the client got a whole bunch of
		// reliable commands then the client never got those first reliable commands
		if ( clc.demoplaying )
			return qfalse;

		while (i < MAX_RELIABLE_COMMANDS)
		{ //spew out the reliable command buffer
			if (clc.reliableCommands[i][0])
			{
				Com_Printf("%i: %s\n", i, clc.reliableCommands[i]);
			}
			i++;
		}
		Com_Error( ERR_DROP, "CL_GetServerCommand: a reliable command was cycled out" );
		return qfalse;
	}

	if ( serverCommandNumber > clc.serverCommandSequence ) {
		Com_Error( ERR_DROP, "CL_GetServerCommand: requested a command not received" );
		return qfalse;
	}

	s = clc.serverCommands[ serverCommandNumber & ( MAX_RELIABLE_COMMANDS - 1 ) ];
	clc.lastExecutedServerCommand = serverCommandNumber;

	Com_DPrintf( "serverCommand: %i : %s\n", serverCommandNumber, s );

rescan:
	Cmd_TokenizeString( s );
	cmd = Cmd_Argv(0);

	if ( !strcmp( cmd, "disconnect" ) ) {
		char strEd[MAX_STRINGED_SV_STRING];
		CL_CheckSVStringEdRef(strEd, Cmd_Argv(1));
		Com_Error (ERR_SERVERDISCONNECT, "%s: %s\n", SE_GetString("MP_SVGAME_SERVER_DISCONNECTED"), strEd );
	}

	if ( !strcmp( cmd, "bcs0" ) ) {
		Com_sprintf( bigConfigString, BIG_INFO_STRING, "cs %s \"%s", Cmd_Argv(1), Cmd_Argv(2) );
		return qfalse;
	}

	if ( !strcmp( cmd, "bcs1" ) ) {
		s = Cmd_Argv(2);
		if( strlen(bigConfigString) + strlen(s) >= BIG_INFO_STRING ) {
			Com_Error( ERR_DROP, "bcs exceeded BIG_INFO_STRING" );
		}
		strcat( bigConfigString, s );
		return qfalse;
	}

	if ( !strcmp( cmd, "bcs2" ) ) {
		s = Cmd_Argv(2);
		if( strlen(bigConfigString) + strlen(s) + 1 >= BIG_INFO_STRING ) {
			Com_Error( ERR_DROP, "bcs exceeded BIG_INFO_STRING" );
		}
		strcat( bigConfigString, s );
		strcat( bigConfigString, "\"" );
		s = bigConfigString;
		goto rescan;
	}

	if ( !strcmp( cmd, "cs" ) ) {
		CL_ConfigstringModified();
		// reparse the string, because CL_ConfigstringModified may have done another Cmd_TokenizeString()
		Cmd_TokenizeString( s );
		return qtrue;
	}

	if ( !strcmp( cmd, "map_restart" ) ) {
		// clear notify lines and outgoing commands before passing
		// the restart to the cgame
		Con_ClearNotify();
		// reparse the string, because Con_ClearNotify() may have done another Cmd_TokenizeString()
		Cmd_TokenizeString( s );
		Com_Memset( cl.cmds, 0, sizeof( cl.cmds ) );
		return qtrue;
	}

	// the clientLevelShot command is used during development
	// to generate 128*128 screenshots from the intermission
	// point of levels for the menu system to use
	// we pass it along to the cgame to make appropriate adjustments,
	// but we also clear the console and notify lines here
	if ( !strcmp( cmd, "clientLevelShot" ) ) {
		// don't do it if we aren't running the server locally,
		// otherwise malicious remote servers could overwrite
		// the existing thumbnails
		if ( !com_sv_running->integer ) {
			return qfalse;
		}
		// close the console
		Con_Close();
		// take a special screenshot next frame
		Cbuf_AddText( "wait ; wait ; wait ; wait ; screenshot levelshot\n" );
		return qtrue;
	}

	// we may want to put a "connect to other server" command here

	// cgame can now act on the command
	return qtrue;
}

/*
====================
CL_ShutdonwCGame

====================
*/
void CL_ShutdownCGame( void ) {
	Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_CGAME );
	Cvar_Set( "v_chatHandler", "0" );		// see Con_ChatByCgame()
	Cvar_Set( "v_catchQuiet", "0" );		// see CL_CmdButtons()
	Cvar_Set( "v_muffle", "0" );			// see S_MufflePaint(): nobody is left who would take it back
	Cvar_Set( "v_tinnitus", "0" );			// see S_TinnitusPaint(), the same
	Cvar_Set( "v_dreamReverb", "0" );		// see S_SpatialRenderDream(), the same
	Cvar_Set( "v_cgameResize", "0" );		// see CL_WindowSize()
	Cvar_Set( "v_scopeWheel", "0" );		// see CL_ParseBinding()

	if ( !cls.cgameStarted )
		return;

	cls.cgameStarted = qfalse;

	CL_UnbindCGame();
}

/*
====================
CL_InitCGame

Should only be called by CL_StartHunkUsers
====================
*/

void CL_InitCGame( void ) {
	const char			*info;
	const char			*mapname;
	int					t1, t2;

	t1 = Sys_Milliseconds();

	// a cgame of this tree sets it again when it takes the chat line (Con_ChatByCgame())
	Cvar_Set( "v_chatHandler", "0" );
	// and this one while it holds the keys without its player typing (CL_CmdButtons())
	Cvar_Set( "v_catchQuiet", "0" );
	// and this one when it follows a new size of the window by itself (CL_WindowSize())
	Cvar_Set( "v_cgameResize", "0" );

	// put away the console
	Con_Close();

	// find the current mapname
	info = cl.gameState.stringData + cl.gameState.stringOffsets[ CS_SERVERINFO ];
	mapname = Info_ValueForKey( info, "mapname" );
	Com_sprintf( cl.mapname, sizeof( cl.mapname ), "maps/%s.bsp", mapname );

	// load the dll
	CL_BindCGame();

	cls.state = CA_LOADING;

	// init for this gamestate
	// use the lastExecutedServerCommand instead of the serverCommandSequence
	// otherwise server commands sent just before a gamestate are dropped
	// what the cgame asks the engine to load in there has kinds of its own, this is the rest of it
	Com_LoadPush( "cgame start", NULL );
	CGVM_Init( clc.serverMessageSequence, clc.lastExecutedServerCommand, clc.clientNum );
	Com_LoadPop();

	int clRate = Cvar_VariableIntegerValue( "rate" );
	if ( clRate == 4000 ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: Old default /rate value detected (4000). Suggest typing /rate 25000 into console for a smoother connection!\n" );
	}

	// reset any CVAR_CHEAT cvars registered by cgame
	if ( !clc.demoplaying && !cl_connectedToCheatServer )
		Cvar_SetCheatState();

	// we will send a usercmd this frame, which
	// will cause the server to send us the first snapshot
	cls.state = CA_PRIMED;

	t2 = Sys_Milliseconds();

	Com_Printf( "CL_InitCGame: %5.2f seconds\n", (t2-t1)/1000.0 );

	// have the renderer touch all its images, so they are present
	// on the card even if the driver does deferred loading
	Com_LoadPush( "renderer end of registration", NULL );
	re->EndRegistration();
	Com_LoadPop();

	// make sure everything is paged in
//	if (!Sys_LowPhysicalMemory())
	{
		Com_TouchMemory();
	}

	// clear anything that got printed
	Con_ClearNotify ();
}


/*
====================
CL_GameCommand

See if the current console command is claimed by the cgame
====================
*/
qboolean CL_GameCommand( void ) {
	if ( !cls.cgameStarted )
		return qfalse;

	return CGVM_ConsoleCommand();
}



/*
=====================
CL_CGameRendering
=====================
*/
void CL_CGameRendering( stereoFrame_t stereo ) {
	//rww - RAGDOLL_BEGIN
	if (!com_sv_running->integer)
	{ //set the server time to match the client time, if we don't have a server going.
		re->G2API_SetTime(cl.serverTime, 0);
	}
	re->G2API_SetTime(cl.serverTime, 1);
	//rww - RAGDOLL_END

	Com_LoadPush( "frame: cgame", NULL );
	CGVM_DrawActiveFrame( cl.serverTime, stereo, clc.demoplaying );
	Com_LoadPop();
}


/*
=================
CL_AdjustTimeDelta

Adjust the clients view of server time.

We attempt to have cl.serverTime exactly equal the server's view
of time plus the timeNudge, but with variable latencies over
the internet it will often need to drift a bit to match conditions.

Our ideal time would be to have the adjusted time approach, but not pass,
the very latest snapshot.

Adjustments are only made when a new snapshot arrives with a rational
latency, which keeps the adjustment process framerate independent and
prevents massive overadjustment during times of significant packet loss
or bursted delayed packets.
=================
*/

#define	RESET_TIME	500
#define	SNAP_MARGIN	5		// how close a frame may come to the newest snapshot before the clock goes back, at least

/*
=================
CL_SnapshotJitter

How evenly the snapshots come, as RFC 3550 measures it for the packets of a stream: how much the time between
the server making one and the client taking it in changes from one to the next, smoothed over about 16. The
clock of the game (CL_AdjustTimeDelta) goes forward a little with every snapshot and back a little when a frame
came closer than cl.snapMargin to the newest one. With the 5 ms of the original as that margin, a line whose
snapshots come unevenly has frames that run past the newest snapshot, which the cgame has to extrapolate: what
moves stutters. Twice the jitter as the margin keeps the clock as far behind as the line needs, and no further:
on an even line it stays the 5 ms (v_netSmooth 0: always). Not from OpenJK, 2026-10-19.
=================
*/
static void CL_SnapshotJitter( void ) {
	const int offset = cls.realtime - cl.snap.serverTime;

	if ( cl.snapOffsetValid ) {
		const int change = abs( offset - cl.snapOffset );

		// (a jump of the clock or a long gap is no jitter)
		if ( change < RESET_TIME ) {
			cl.snapJitter += ( change - cl.snapJitter ) / 16.0f;
		}
	}
	cl.snapOffset = offset;
	cl.snapOffsetValid = qtrue;
	if ( v_netSmooth->integer > SNAP_MARGIN ) {
		cl.snapMargin = Com_Clampi( SNAP_MARGIN, v_netSmooth->integer, (int)( 2.0f * cl.snapJitter + 0.5f ) );
	} else {
		cl.snapMargin = SNAP_MARGIN;
	}
}

/*
=================
CL_NetStats

v_netStats 1: a line every five seconds with how the snapshots come and how the game runs behind them. Counted
whatever the cvar says, it is a few additions.
=================
*/
static struct {
	int			start;				// cls.realtime the counts began
	int			snapshots, bytes;
	int			frames, past;		// frames of the game, and those past the newest snapshot (the cgame extrapolates)
	long long	behind;				// cl.snap.serverTime - cl.serverTime of every frame, added up
} netStats;

void CL_NetStatsPacket( int bytes ) {
	netStats.bytes += bytes;
}

void CL_NetStatsSnapshot( void ) {
	netStats.snapshots++;
}

static void CL_NetStatsFrame( void ) {
	int msec;

	netStats.frames++;
	netStats.behind += cl.snap.serverTime - cl.serverTime;
	if ( cl.serverTime > cl.snap.serverTime ) {
		netStats.past++;
	}
	if ( !netStats.start ) {
		netStats.start = cls.realtime;
	}
	msec = cls.realtime - netStats.start;
	if ( msec < 5000 ) {
		return;
	}
	if ( v_netStats->integer ) {
		Com_Printf( "net: %i snapshots/s, %i bytes/s, %i of %i frames past the newest snapshot, jitter %.1f ms, margin %i ms, "
			"%.1f ms behind the newest snapshot\n", netStats.snapshots * 1000 / msec, (int)( netStats.bytes * 1000LL / msec ),
			netStats.past, netStats.frames, cl.snapJitter, cl.snapMargin ? cl.snapMargin : SNAP_MARGIN,
			netStats.frames ? (double)netStats.behind / netStats.frames : 0.0 );
	}
	memset( &netStats, 0, sizeof( netStats ) );
	netStats.start = cls.realtime;
}

void CL_AdjustTimeDelta( void ) {
	int		newDelta;
	int		deltaDelta;

	cl.newSnapshots = qfalse;

	// the delta never drifts when replaying a demo
	if ( clc.demoplaying ) {
		return;
	}

	CL_SnapshotJitter();

	newDelta = cl.snap.serverTime - cls.realtime;
	deltaDelta = abs( newDelta - cl.serverTimeDelta );

	if ( deltaDelta > RESET_TIME ) {
		cl.serverTimeDelta = newDelta;
		cl.oldServerTime = cl.snap.serverTime;	// FIXME: is this a problem for cgame?
		cl.serverTime = cl.snap.serverTime;
		if ( cl_showTimeDelta->integer ) {
			Com_Printf( "<RESET> " );
		}
	} else if ( deltaDelta > 100 ) {
		// fast adjust, cut the difference in half
		if ( cl_showTimeDelta->integer ) {
			Com_Printf( "<FAST> " );
		}
		cl.serverTimeDelta = ( cl.serverTimeDelta + newDelta ) >> 1;
	} else {
		// slow drift adjust, only move 1 or 2 msec

		// if any of the frames between this and the previous snapshot
		// had to be extrapolated, nudge our sense of time back a little
		// the granularity of +1 / -2 is too high for timescale modified frametimes
		if ( com_timescale->value == 0 || com_timescale->value == 1 ) {
			if ( cl.extrapolatedSnapshot ) {
				cl.extrapolatedSnapshot = qfalse;
				cl.serverTimeDelta -= 2;
			} else {
				// otherwise, move our sense of time forward to minimize total latency
				cl.serverTimeDelta++;
			}
		}
	}

	if ( cl_showTimeDelta->integer ) {
		Com_Printf( "%i ", cl.serverTimeDelta );
	}
}


/*
==================
CL_FirstSnapshot
==================
*/
void CL_FirstSnapshot( void ) {
	// ignore snapshots that don't have entities
	if ( cl.snap.snapFlags & SNAPFLAG_NOT_ACTIVE ) {
		return;
	}

	Com_LoadPush( "renderer end of load", NULL );
	re->RegisterMedia_LevelLoadEnd();
	Com_LoadPop();

	cls.state = CA_ACTIVE;
	// the frame that runs draws the game for the first time: with it the load is over
	Com_LoadDone();
	CL_JoinStep( JOIN_ACTIVE );

	// set the timedelta so we are exactly on this first frame
	cl.serverTimeDelta = cl.snap.serverTime - cls.realtime;
	cl.oldServerTime = cl.snap.serverTime;

	clc.timeDemoBaseTime = cl.snap.serverTime;

	// if this is the first frame of active play,
	// execute the contents of activeAction now
	// this is to allow scripting a timedemo to start right
	// after loading
	if ( cl_activeAction->string[0] ) {
		Cbuf_AddText( cl_activeAction->string );
		Cvar_Set( "activeAction", "" );
	}
}

/*
==================
CL_SetCGameTime
==================
*/
void CL_SetCGameTime( void ) {
	// getting a valid frame message ends the connection process
	if ( cls.state != CA_ACTIVE ) {
		if ( cls.state != CA_PRIMED ) {
			return;
		}
		if ( clc.demoplaying ) {
			// we shouldn't get the first snapshot on the same frame
			// as the gamestate, because it causes a bad time skip
			if ( !clc.firstDemoFrameSkipped ) {
				clc.firstDemoFrameSkipped = qtrue;
				return;
			}
			CL_ReadDemoMessage();
		}
		if ( cl.newSnapshots ) {
			cl.newSnapshots = qfalse;
			CL_FirstSnapshot();
		}
		if ( cls.state != CA_ACTIVE ) {
			return;
		}
	}

	// if we have gotten to this point, cl.snap is guaranteed to be valid
	if ( !cl.snap.valid ) {
		Com_Error( ERR_DROP, "CL_SetCGameTime: !cl.snap.valid" );
	}

	// allow pause in single player
	if ( sv_paused->integer && CL_CheckPaused() && com_sv_running->integer ) {
		// paused
		return;
	}

	if ( cl.snap.serverTime < cl.oldFrameServerTime ) {
		Com_Error( ERR_DROP, "cl.snap.serverTime < cl.oldFrameServerTime" );
	}
	cl.oldFrameServerTime = cl.snap.serverTime;


	// get our current view of time

	if ( clc.demoplaying && cl_freezeDemo->integer ) {
		// cl_freezeDemo is used to lock a demo in place for single frame advances
	} else
	{
		// cl_timeNudge is a user adjustable cvar that allows more
		// or less latency to be added in the interest of better
		// smoothness or better responsiveness.
		int tn;

		tn = cl_timeNudge->integer;
#ifdef _DEBUG
		if (tn<-900) {
			tn = -900;
		} else if (tn>900) {
			tn = 900;
		}
#else
		if (tn<-30) {
			tn = -30;
		} else if (tn>30) {
			tn = 30;
		}
#endif

		cl.serverTime = cls.realtime + cl.serverTimeDelta - tn;

		// guarantee that time will never flow backwards, even if
		// serverTimeDelta made an adjustment or cl_timeNudge was changed
		if ( cl.serverTime < cl.oldServerTime ) {
			cl.serverTime = cl.oldServerTime;
		}
		cl.oldServerTime = cl.serverTime;

		// note if we are almost past the latest frame (without timeNudge),
		// so we will try and adjust back a bit when the next snapshot arrives
		if ( cls.realtime + cl.serverTimeDelta >= cl.snap.serverTime - ( cl.snapMargin ? cl.snapMargin : SNAP_MARGIN ) ) {
			cl.extrapolatedSnapshot = qtrue;
		}
		if ( !clc.demoplaying ) {
			CL_NetStatsFrame();
		}
	}

	// if we have gotten new snapshots, drift serverTimeDelta
	// don't do this every frame, or a period of packet loss would
	// make a huge adjustment
	if ( cl.newSnapshots ) {
		CL_AdjustTimeDelta();
	}

	if ( !clc.demoplaying ) {
		return;
	}

	// if we are playing a demo back, we can just keep reading
	// messages from the demo file until the cgame definately
	// has valid snapshots to interpolate between

	// a timedemo will always use a deterministic set of time samples
	// no matter what speed machine it is run on,
	// while a normal demo may have different time samples
	// each time it is played back
	if ( cl_timedemo->integer ) {
		if (!clc.timeDemoStart) {
			clc.timeDemoStart = Sys_Milliseconds();
		}
		clc.timeDemoFrames++;
		cl.serverTime = clc.timeDemoBaseTime + clc.timeDemoFrames * 50;
	}

	while ( cl.serverTime >= cl.snap.serverTime ) {
		// feed another messag, which should change
		// the contents of cl.snap
		CL_ReadDemoMessage();
		if ( cls.state != CA_ACTIVE ) {
			return;		// end of demo
		}
	}
}
