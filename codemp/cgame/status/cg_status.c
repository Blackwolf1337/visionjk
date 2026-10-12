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
//
// A server without the game module of this tree passes nothing on. There the statuses go through the voice
// master, if this client is linked to it (voice/cg_voice.c, cg_voiceMaster): every client says its own status
// every five seconds, the master hands that to the others of the same server, and a status that was not said
// again for a while is over. Nobody finds out there who is away, so every client says it of itself: no key and
// no turn of the view for a minute, the default of g_afkTime. A legacy player on such a server has no line.

#include "cgame/cg_local.h"

#define STATUS_TEXT			64		// as statusMusic of the game module
#define STATUS_POLL_MSEC	2000
#define STATUS_SEND_MSEC	6000
#define STATUS_MASTER_MSEC	5000	// through the voice master: the own status is said this often, and when it changed
#define STATUS_MASTER_KEEP	15000	//	and one of somebody else that was not said again for this long is over
#define STATUS_AWAY_MSEC	60000	//	and this long without a key or a turn of the view is away: the default of g_afkTime
#define STATUS_ASK_FIRST	1500	// "jpstatus all" goes to the server this long after the cgame has its first frame,
#define STATUS_ASK_MSEC		3500	//	again this often while no answer came,
#define STATUS_MAX_ASKS		6		//	and this many times at most

static struct {
	qboolean	away[MAX_CLIENTS];
	char		music[MAX_CLIENTS][STATUS_TEXT];
	int			pollTime;
	int			sendTime;
	qboolean	utf8;					// text is UTF-8 (cl_utf8Text)
	int			masterTime[MAX_CLIENTS];	// when the voice master last said his status, 0: it is not from there
	qboolean	masterAway;				// what the master was last told of this player,
	char		masterMusic[STATUS_TEXT];
	int			inputTime;				// when this player last pressed a key or turned,
	int			inputAngles[2];			//	and what his commands said then
	int			inputButtons;
	signed char	inputMove[3];
	char		test[STATUS_TEXT];		// status_test: said in place of what the players of music say
	// "jpstatus all": the server is asked for everything once this cgame has started
	int			askStart;				// when its first frame was, by the clock of the system
	int			askTime;				// when it was last asked
	int			asks;					// how often
	qboolean	answered;				// its answer came
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

// the answer to "jpstatus all" came (the server command jpclient with everybody in it, status/cg_peers.c)
void CG_StatusAnswered( void ) {
	status.answered = qtrue;
}

/*
=================
CG_StatusFromMaster

The voice master handed on what stands above the head of somebody. The text is UTF-8 there, whatever the text
of this client is.
=================
*/
void CG_StatusFromMaster( int clientNum, qboolean away, const char *text ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}
	status.away[clientNum] = away;
	if ( status.utf8 ) {
		Q_strncpyz( status.music[clientNum], text, sizeof( status.music[clientNum] ) );
	} else {
		Q_UTF8_ToCP1252( text, status.music[clientNum], sizeof( status.music[clientNum] ) );
	}
	Q_StripColor( status.music[clientNum] );
	status.masterTime[clientNum] = cg.time ? cg.time : 1;
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
	if ( !cgs.clientinfo[clientNum].infoValid
		|| ( status.masterTime[clientNum] && ( cg.time - status.masterTime[clientNum] > STATUS_MASTER_KEEP || cg.time < status.masterTime[clientNum] ) ) ) {
		// he left, the slot may be somebody else's next. Or the voice master did not say his status again:
		// he left its room, or has nothing above his head any more
		status.away[clientNum] = qfalse;
		status.music[clientNum][0] = '\0';
		status.masterTime[clientNum] = 0;
		return qfalse;
	}
	if ( status.away[clientNum] ) {
		Q_strncpyz( out, CG_VisionText( "Away from keyboard" ), outSize );
		return qtrue;
	}
	if ( status.music[clientNum][0] ) {
		// with a note in front (U+266A) where text is UTF-8
		Com_sprintf( out, outSize, "%s%s", status.utf8 ? "\xE2\x99\xAA " : "", CG_VisionTextWith( "Listening to %s", status.music[clientNum] ) );
		return qtrue;
	}
	return qfalse;
}

/*
=================
Status_OwnAway

On a server that does not find out who is away: is this player? The commands he sends are looked at, as the
server does it (G_StatusUserCmd of g_status.c).
=================
*/
static qboolean Status_OwnAway( void ) {
	usercmd_t cmd;

	if ( !trap->GetUserCmd( trap->GetCurrentCmdNumber(), &cmd ) ) {
		return qfalse;
	}
	if ( !status.inputTime || cg.time < status.inputTime || cmd.buttons != status.inputButtons
		|| cmd.angles[0] != status.inputAngles[0] || cmd.angles[1] != status.inputAngles[1]
		|| cmd.forwardmove != status.inputMove[0] || cmd.rightmove != status.inputMove[1] || cmd.upmove != status.inputMove[2] ) {
		status.inputTime = cg.time ? cg.time : 1;
		status.inputButtons = cmd.buttons;
		status.inputAngles[0] = cmd.angles[0];
		status.inputAngles[1] = cmd.angles[1];
		status.inputMove[0] = cmd.forwardmove;
		status.inputMove[1] = cmd.rightmove;
		status.inputMove[2] = cmd.upmove;
		return qfalse;
	}
	return (qboolean)( cg.time - status.inputTime > STATUS_AWAY_MSEC );
}

/*
=================
Status_Music

What this player listens to as it may be told: UTF-8, without colours, control characters and what would end
the quotes of a command, and no longer than a status is. Empty: nothing, or he does not share it.
=================
*/
static void Status_Music( char *out, int outSize ) {
	char	wanted[STATUS_TEXT * 4];
	char	*in, *to;

	wanted[0] = '\0';
	if ( cg_shareMusic.integer ) {
		if ( status.test[0] ) {
			Q_strncpyz( wanted, status.test, sizeof( wanted ) );
		} else {
			CG_MusicNowPlaying( wanted, sizeof( wanted ) );
		}
	}
	Q_StripColor( wanted );
	for ( in = to = wanted; *in; in++ ) {
		if ( (unsigned char)*in >= ' ' && *in != 127 && *in != '"' && *in != ';' && *in != '%' ) {
			*to++ = *in;
		}
	}
	*to = '\0';
	Q_UTF8_Truncate( wanted, STATUS_TEXT - 1 );
	Q_strncpyz( out, wanted, outSize );
}

/*
=================
Status_MasterFrame

A server that passes no statuses on, and a voice master that does: it is told what stands above this player
=================
*/
static void Status_MasterFrame( qboolean away, const char *music ) {
	const qboolean changed = (qboolean)( away != status.masterAway || strcmp( music, status.masterMusic ) );

	// (called every STATUS_POLL_MSEC, and the master takes one a second)
	if ( !changed && cg.time >= status.sendTime && cg.time - status.sendTime < STATUS_MASTER_MSEC ) {
		return;
	}
	status.sendTime = cg.time;
	status.masterAway = away;
	Q_strncpyz( status.masterMusic, music, sizeof( status.masterMusic ) );
	CG_VoiceSendStatus( away, music );
	// nobody says it back to this client, and it stands above the own head in third person too
	CG_StatusFromMaster( cg.clientNum, away, music );
}

/*
=================
CG_StatusFrame

Once a frame: keeps the server told what this player listens to, or the voice master
=================
*/
void CG_StatusFrame( void ) {
	char		music[STATUS_TEXT], wanted[STATUS_TEXT], value[8];
	qboolean	server, away = qfalse;

	CG_MusicFrame();

	if ( !cg.snap || cg.demoPlayback ) {
		return;
	}
	// A cgame that has just started knows nothing of the others: a new level, but also a restart of the video,
	// of which the server hears nothing. A game module that answers it (g_visionCaps bit 3) is asked for what
	// stands above everybody and for who plays with what (status/cg_peers.c). Not in the first moment, when the
	// voice chat says its hello, and again while no answer came: the flood protection of a server
	// (sv_floodProtect, on by default) drops a command that comes within a second of another one without a word.
	if ( !status.answered && status.asks < STATUS_MAX_ASKS
		&& ( atoi( Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "g_visionCaps" ) ) & 8 ) ) {
		const int now = trap->Milliseconds();

		if ( !status.askStart ) {
			status.askStart = now ? now : 1;
		}
		if ( now - status.askStart >= STATUS_ASK_FIRST && ( !status.asks || now - status.askTime >= STATUS_ASK_MSEC ) ) {
			trap->SendClientCommand( "jpstatus all" );
			status.askTime = now;
			status.asks++;
		}
	}
	// only a game module of this tree passes statuses on, and says so. Anywhere else the voice master may.
	server = (qboolean)( ( atoi( Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "g_visionCaps" ) ) & 2 ) != 0 );
	if ( !server ) {
		if ( !CG_VoiceSocial() ) {
			return;
		}
		// every frame: a key that was pressed and let go between two looks would be missed
		away = Status_OwnAway();
	}
	if ( cg.time - status.pollTime < STATUS_POLL_MSEC && cg.time >= status.pollTime ) {
		return;
	}
	status.pollTime = cg.time;
	trap->Cvar_VariableStringBuffer( "cl_utf8Text", value, sizeof( value ) );
	status.utf8 = (qboolean)( atoi( value ) == 1 );

	Status_Music( music, sizeof( music ) );
	if ( !server ) {
		Status_MasterFrame( away, music );
		return;
	}

	// what the players of music say is UTF-8. As the server will have it: in the text the client has.
	if ( status.utf8 ) {
		Q_strncpyz( wanted, music, sizeof( wanted ) );
	} else {
		Q_UTF8_ToCP1252( music, wanted, sizeof( wanted ) );
	}

	if ( !strcmp( wanted, status.music[cg.clientNum] ) || ( cg.time - status.sendTime < STATUS_SEND_MSEC && cg.time >= status.sendTime ) ) {
		return;
	}
	status.sendTime = cg.time;
	trap->SendClientCommand( wanted[0] ? va( "jpstatus music \"%s\"", wanted ) : "jpstatus none" );
}

/*
=================
CG_StatusList_f

statuses: what stands above whom, as this client knows it
=================
*/
void CG_StatusList_f( void ) {
	char	line[STATUS_TEXT + 32];
	int		i, count = 0;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( CG_StatusLine( i, line, sizeof( line ) ) ) {
			trap->Print( "  %2i %s^7: %s\n", i, cgs.clientinfo[i].name, line );
			count++;
		}
	}
	// what this machine plays, as it would be told: the answer to "why does nobody see my music"
	if ( !cg_shareMusic.integer ) {
		trap->Print( "Your music is not told to anybody (cg_shareMusic 0).\n" );
	} else {
		char music[STATUS_TEXT];

		Status_Music( music, sizeof( music ) );
		trap->Print( "Your music: %s\n", music[0] ? music : "nothing is found playing" );
	}
	if ( !cg_playerStatuses.integer ) {
		trap->Print( "The statuses of the others are off (cg_playerStatuses 0).\n" );
	} else if ( atoi( Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "g_visionCaps" ) ) & 2 ) {
		trap->Print( "%i statuses, told by the server.\n", count );
	} else if ( CG_VoiceSocial() ) {
		trap->Print( "%i statuses, handed on by the voice master: this server passes none on.\n", count );
	} else {
		trap->Print( "No statuses here: this server passes none on, and the voice master is not connected (cg_voice, cg_voiceMaster).\n" );
	}
}

/*
=================
CG_StatusTest_f

status_test <text> / status_test stop: said as the music of this player in place of what plays on this machine,
for checks without a player of music. cg_shareMusic decides as always whether anything is said.
=================
*/
void CG_StatusTest_f( void ) {
	char text[STATUS_TEXT * 2];

	trap->Cmd_Args( text, sizeof( text ) );
	if ( !text[0] ) {
		trap->Print( "usage: status_test <text> | stop\n" );
		return;
	}
	if ( !Q_stricmp( text, "stop" ) ) {
		status.test[0] = '\0';
		return;
	}
	Q_strncpyz( status.test, text, sizeof( status.test ) );
}
