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

// g_status.c -- what stands above the head of a player besides his chat: that he is away from the keyboard, or
// the music he listens to. Not from JA+.
//
// What one player's client knows has to reach the others through the server. A cgame of this tree says in its
// userinfo that it draws statuses (v_caps bit 0), and only such a client is ever sent the server command
//
//   vs_status <client> <away> "<music>"		away: 1 or 0, music: what he listens to, or nothing
//
// so a legacy client never sees a command it does not know. The game module says in the server info that it
// passes statuses on (vs_caps bit 1).
//
// - Away is found out here: a player whose commands did not change for vs_afkTime seconds (no key, no turn) is
//   away until they do. That holds for legacy clients too, so the others see it above them as well.
// - Music is told by the client, which alone knows it: the client command "vs_status music <text>" (the cgame
//   sends it only when its player switched that on), "vs_status none" ends it. At most one every three seconds,
//   64 bytes, without colours and control characters.
//
// Who plays with a client of this tree is told the same way. A cgame of this tree names itself in its userinfo
// (v_vision, its version), and one that draws a mark for the others says so (v_caps bit 1). Only such a
// client is sent
//
//   jpclient <client> <version> [<client> <version> ...]		version: of his VisionJK, "-" for any other client
//
// with one player when his userinfo says something new, and with everybody in one command when it asks for all
// of it: a cgame that has just started knows nothing (a new level, but also a restart of the video, of which
// the server hears nothing), so it sends the client command "vs_status all" once and is told who plays with what
// and what stands above everybody. The game module says in the server info that it does this (vs_caps bit
// 3). The userinfo is what the client says of itself: a mark, nothing to rely on.

#include "game/g_local.h"

#define STATUS_CMD_MSEC		3000
#define STATUS_ALL_MSEC		2000	// "vs_status all" is answered this often at most

static int statusFrameTime;

// Tells one client about one player, if its cgame draws statuses. Both parts always go out: the cgame shows
// away first, and the client the music is from sees by it that its command arrived.
static void G_StatusTell( const gentity_t *ent, const gentity_t *other ) {
	const jpClient_t	*jp = &ent->client->pers.jp;
	char				legacy[sizeof( jp->statusMusic )];

	if ( !other->inuse || !other->client || other->client->pers.connected != CON_CONNECTED
		|| !( other->client->pers.jp.statusCaps & 1 ) ) {
		return;
	}
	trap->SendServerCommand( other - g_entities, va( "vs_status %i %i \"%s\"", (int)( ent - g_entities ), jp->statusAway ? 1 : 0,
		JP_ChatFor( ent, other, jp->statusMusic, legacy, sizeof( legacy ) ) ) );
}

static void G_StatusBroadcast( const gentity_t *ent ) {
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		G_StatusTell( ent, &g_entities[i] );
	}
}

// what a client is, as the command says it
static const char *G_PeerWord( const gentity_t *ent ) {
	return ent->client->pers.jp.vision[0] ? ent->client->pers.jp.vision : "-";
}

// does the cgame of this client draw who plays with what, and is it there to be told
static qboolean G_PeerAsks( const gentity_t *ent ) {
	return (qboolean)( ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED
		&& ( ent->client->pers.jp.statusCaps & 2 ) );
}

// Tells one client about everybody, in one command: a level begins with enough of them.
static void G_PeerTellAll( const gentity_t *to ) {
	char	cmd[MAX_STRING_CHARS - 64];
	int		i;

	if ( !G_PeerAsks( to ) ) {
		return;
	}
	Q_strncpyz( cmd, "jpclient", sizeof( cmd ) );
	for ( i = 0; i < level.maxclients; i++ ) {
		const gentity_t *ent = &g_entities[i];

		if ( ent->inuse && ent->client && ent->client->pers.connected != CON_DISCONNECTED && ent->client->pers.jp.visionSet ) {
			Q_strcat( cmd, sizeof( cmd ), va( " %i %s", i, G_PeerWord( ent ) ) );
		}
	}
	trap->SendServerCommand( to - g_entities, cmd );
}

// The cgame of a client has just started and asks for all of it: what stands above everybody and who plays
// with what, as far as it draws them.
static void G_StatusAll( gentity_t *ent ) {
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		const gentity_t	*other = &g_entities[i];

		if ( other->inuse && other->client && other->client->pers.connected == CON_CONNECTED
			&& ( other->client->pers.jp.statusAway || other->client->pers.jp.statusMusic[0] ) ) {
			G_StatusTell( other, ent );
		}
	}
	G_PeerTellAll( ent );
}

/*
=================
G_StatusUserinfo

The userinfo of a client came. A cgame that says for the first time that it draws statuses is told what stands
above everybody.
=================
*/
void G_StatusUserinfo( gentity_t *ent, const char *userinfo ) {
	jpClient_t	*jp = &ent->client->pers.jp;
	const int	caps = atoi( Info_ValueForKey( userinfo, "v_caps" ) );
	char		vision[sizeof( jp->vision )];
	const char	*in;
	int			i, len = 0;

	// Which VisionJK he says he has: letters, digits and the marks of a version, as one word of a command. A
	// new one is told to everybody who asks. (Who enters the level later is told all of them then.)
	for ( in = Info_ValueForKey( userinfo, "v_vision" ); *in && len < (int)sizeof( vision ) - 1; in++ ) {
		if ( ( *in >= '0' && *in <= '9' ) || ( *in >= 'a' && *in <= 'z' ) || ( *in >= 'A' && *in <= 'Z' ) || *in == '.' || *in == '-' || *in == '_' ) {
			vision[len++] = *in;
		}
	}
	vision[len] = '\0';
	if ( !jp->visionSet || strcmp( vision, jp->vision ) ) {
		Q_strncpyz( jp->vision, vision, sizeof( jp->vision ) );
		jp->visionSet = qtrue;
		for ( i = 0; i < level.maxclients; i++ ) {
			if ( G_PeerAsks( &g_entities[i] ) ) {
				trap->SendServerCommand( i, va( "jpclient %i %s", (int)( ent - g_entities ), G_PeerWord( ent ) ) );
			}
		}
	}

	if ( ( caps & 1 ) && !( jp->statusCaps & 1 ) ) {
		jp->statusCaps = caps;
		for ( i = 0; i < level.maxclients; i++ ) {
			const gentity_t	*other = &g_entities[i];

			if ( other->inuse && other->client && other->client->pers.connected == CON_CONNECTED
				&& ( other->client->pers.jp.statusAway || other->client->pers.jp.statusMusic[0] ) ) {
				G_StatusTell( other, ent );
			}
		}
	}
	jp->statusCaps = caps;
}

/*
=================
G_StatusInput

The command of a client for this frame. One that differs from the last is a sign of life.
=================
*/
void G_StatusInput( gentity_t *ent, const usercmd_t *ucmd ) {
	jpClient_t *jp = &ent->client->pers.jp;

	if ( ent->r.svFlags & SVF_BOT ) {
		return;
	}
	if ( jp->statusInputTime && ucmd->buttons == jp->statusButtons && ucmd->forwardmove == jp->statusMove[0]
		&& ucmd->rightmove == jp->statusMove[1] && ucmd->upmove == jp->statusMove[2]
		&& ucmd->angles[0] == jp->statusAngles[0] && ucmd->angles[1] == jp->statusAngles[1] ) {
		return;
	}
	jp->statusButtons = ucmd->buttons;
	jp->statusMove[0] = ucmd->forwardmove;
	jp->statusMove[1] = ucmd->rightmove;
	jp->statusMove[2] = ucmd->upmove;
	jp->statusAngles[0] = ucmd->angles[0];
	jp->statusAngles[1] = ucmd->angles[1];
	jp->statusInputTime = level.time;
	if ( jp->statusAway ) {
		jp->statusAway = qfalse;
		G_StatusBroadcast( ent );
	}
}

/*
=================
G_StatusFrame

Once a second: who gave no sign of life for vs_afkTime seconds is away
=================
*/
void G_StatusFrame( void ) {
	int i;

	if ( level.time - statusFrameTime < 1000 && level.time >= statusFrameTime ) {
		return;
	}
	statusFrameTime = level.time;
	if ( vs_afkTime.integer <= 0 ) {
		return;
	}
	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t	*ent = &g_entities[i];
		jpClient_t	*jp;

		if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED || ( ent->r.svFlags & SVF_BOT ) ) {
			continue;
		}
		jp = &ent->client->pers.jp;
		if ( !jp->statusAway && jp->statusInputTime && level.time - jp->statusInputTime > vs_afkTime.integer * 1000 ) {
			jp->statusAway = qtrue;
			G_StatusBroadcast( ent );
		}
	}
}

/*
=================
Cmd_JPStatus_f

vs_status music <text> / vs_status none: the cgame of a client tells what its player listens to.
vs_status all: a cgame that has just started asks for what stands above everybody and who plays with what
=================
*/
void Cmd_JPStatus_f( gentity_t *ent ) {
	jpClient_t	*jp = &ent->client->pers.jp;
	char		kind[16], text[sizeof( jp->statusMusic )];

	trap->Argv( 1, kind, sizeof( kind ) );
	if ( !Q_stricmp( kind, "all" ) ) {
		// (a clock of its own: it must not keep the music of the same moment from being told)
		if ( !jp->statusAllTime || level.time - jp->statusAllTime >= STATUS_ALL_MSEC || level.time < jp->statusAllTime ) {
			jp->statusAllTime = level.time;
			G_StatusAll( ent );
		}
		return;
	}
	if ( jp->statusCmdTime && level.time - jp->statusCmdTime < STATUS_CMD_MSEC && level.time >= jp->statusCmdTime ) {
		return;		// the cgame asks again when it does not hear its own status back
	}
	jp->statusCmdTime = level.time;

	text[0] = '\0';
	if ( !Q_stricmp( kind, "music" ) ) {
		JP_ChatCopy( ent, ConcatArgs( 2 ), text, sizeof( text ) );
		BG_StatusClean( text );
	}
	if ( !strcmp( text, jp->statusMusic ) ) {
		// nothing new. The client still hears what the others are told, so it knows its command arrived.
		G_StatusTell( ent, ent );
		return;
	}
	Q_strncpyz( jp->statusMusic, text, sizeof( jp->statusMusic ) );
	G_StatusBroadcast( ent );
}
