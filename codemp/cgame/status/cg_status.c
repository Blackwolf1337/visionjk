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

// cg_status.c -- what stands above the head of a player besides his chat: that he is away from the keyboard,
// or the music he listens to. Not from JA+. The server part is game/status/g_status.c, which has the protocol;
// the line itself is drawn with the chat bubbles (bubble/cg_bubble.c).
//
// The server tells the statuses of all players to a cgame that asks for them (cjp_caps bit 0 in the userinfo).
// Away is found out by the server. The music is told by this file, and only when the player switched it on
// (cg_shareMusic 1, off by default: it tells everybody on the server what one listens to): every two seconds it
// asks cg_status_music.c what plays, and tells the server when that differs from what the server last said
// about this player, at most every six seconds. That also sends it again when the command was lost.

#include "cgame/cg_local.h"

#define STATUS_TEXT			64		// as statusMusic of the game module
#define STATUS_POLL_MSEC	2000
#define STATUS_SEND_MSEC	6000

static struct {
	qboolean	away[MAX_CLIENTS];
	char		music[MAX_CLIENTS][STATUS_TEXT];
	int			pollTime;
	int			sendTime;
	qboolean	utf8;					// text is UTF-8 (cl_utf8Text)
} status;

void CG_StatusInit( void ) {
	memset( &status, 0, sizeof( status ) );
}

/*
=================
CG_StatusServerCommand

jpstatus <client> <away> "<music>"
=================
*/
void CG_StatusServerCommand( void ) {
	char	arg[16];
	int		clientNum;

	if ( trap->Cmd_Argc() < 4 ) {
		return;
	}
	trap->Cmd_Argv( 1, arg, sizeof( arg ) );
	clientNum = atoi( arg );
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}
	trap->Cmd_Argv( 2, arg, sizeof( arg ) );
	status.away[clientNum] = (qboolean)( atoi( arg ) != 0 );
	trap->Cmd_Argv( 3, status.music[clientNum], sizeof( status.music[clientNum] ) );
	Q_StripColor( status.music[clientNum] );
}

/*
=================
CG_StatusLine

The line that stands above a player, qfalse when there is none. Away comes first: who is not there does not
listen.
=================
*/
qboolean CG_StatusLine( int clientNum, char *out, int outSize ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !cg_playerStatuses.integer ) {
		return qfalse;
	}
	if ( !cgs.clientinfo[clientNum].infoValid ) {
		// he left, the slot may be somebody else's next
		status.away[clientNum] = qfalse;
		status.music[clientNum][0] = '\0';
		return qfalse;
	}
	if ( status.away[clientNum] ) {
		Q_strncpyz( out, "Away from keyboard", outSize );
		return qtrue;
	}
	if ( status.music[clientNum][0] ) {
		// with a note in front (U+266A) where text is UTF-8
		Com_sprintf( out, outSize, "%sListening to %s", status.utf8 ? "\xE2\x99\xAA " : "", status.music[clientNum] );
		return qtrue;
	}
	return qfalse;
}

/*
=================
CG_StatusFrame

Once a frame: keeps the server told what this player listens to
=================
*/
void CG_StatusFrame( void ) {
	char	wanted[STATUS_TEXT * 4], value[8];
	char	*in, *out;

	CG_MusicFrame();

	if ( !cg.snap || cg.demoPlayback || ( cg.time - status.pollTime < STATUS_POLL_MSEC && cg.time >= status.pollTime ) ) {
		return;
	}
	status.pollTime = cg.time;
	trap->Cvar_VariableStringBuffer( "cl_utf8Text", value, sizeof( value ) );
	status.utf8 = (qboolean)( atoi( value ) == 1 );

	// only a game module of this tree passes it on, and says so
	if ( !( atoi( Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "g_visionCaps" ) ) & 2 ) ) {
		return;
	}

	wanted[0] = '\0';
	if ( cg_shareMusic.integer ) {
		CG_MusicNowPlaying( wanted, sizeof( wanted ) );
	}
	// what the players of music say is UTF-8. As the server will have it: in the text the client has, without
	// what the server takes out, and no longer than it keeps.
	if ( status.utf8 ) {
		Q_UTF8_Truncate( wanted, STATUS_TEXT - 1 );
	} else {
		char utf8[sizeof( wanted )];

		Q_strncpyz( utf8, wanted, sizeof( utf8 ) );
		Q_UTF8_ToCP1252( utf8, wanted, STATUS_TEXT );
	}
	Q_StripColor( wanted );
	for ( in = out = wanted; *in; in++ ) {
		if ( (unsigned char)*in >= ' ' && *in != 127 && *in != '"' && *in != ';' && *in != '%' ) {
			*out++ = *in;
		}
	}
	*out = '\0';

	if ( !strcmp( wanted, status.music[cg.clientNum] ) || ( cg.time - status.sendTime < STATUS_SEND_MSEC && cg.time >= status.sendTime ) ) {
		return;
	}
	status.sendTime = cg.time;
	trap->SendClientCommand( wanted[0] ? va( "jpstatus music \"%s\"", wanted ) : "jpstatus none" );
}
