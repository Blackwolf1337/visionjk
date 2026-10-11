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
// userinfo that it draws statuses (cjp_caps bit 0), and only such a client is ever sent the server command
//
//   jpstatus <client> <away> "<music>"		away: 1 or 0, music: what he listens to, or nothing
//
// so a legacy client never sees a command it does not know. The game module says in the server info that it
// passes statuses on (g_visionCaps bit 1).
//
// - Away is found out here: a player whose commands did not change for g_afkTime seconds (no key, no turn) is
//   away until they do. That holds for legacy clients too, so the others see it above them as well.
// - Music is told by the client, which alone knows it: the client command "jpstatus music <text>" (the cgame
//   sends it only when its player switched that on), "jpstatus none" ends it. At most one every three seconds,
//   64 bytes, without colours and control characters.

#include "game/g_local.h"

#define STATUS_CMD_MSEC		3000

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
	trap->SendServerCommand( other - g_entities, va( "jpstatus %i %i \"%s\"", (int)( ent - g_entities ), jp->statusAway ? 1 : 0,
		JP_ChatFor( ent, other, jp->statusMusic, legacy, sizeof( legacy ) ) ) );
}

static void G_StatusBroadcast( const gentity_t *ent ) {
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		G_StatusTell( ent, &g_entities[i] );
	}
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
	const int	caps = atoi( Info_ValueForKey( userinfo, "cjp_caps" ) );
	int			i;

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

Once a second: who gave no sign of life for g_afkTime seconds is away
=================
*/
void G_StatusFrame( void ) {
	int i;

	if ( level.time - statusFrameTime < 1000 && level.time >= statusFrameTime ) {
		return;
	}
	statusFrameTime = level.time;
	if ( g_afkTime.integer <= 0 ) {
		return;
	}
	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t	*ent = &g_entities[i];
		jpClient_t	*jp;

		if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED || ( ent->r.svFlags & SVF_BOT ) ) {
			continue;
		}
		jp = &ent->client->pers.jp;
		if ( !jp->statusAway && jp->statusInputTime && level.time - jp->statusInputTime > g_afkTime.integer * 1000 ) {
			jp->statusAway = qtrue;
			G_StatusBroadcast( ent );
		}
	}
}

/*
=================
Cmd_JPStatus_f

jpstatus music <text> / jpstatus none: the cgame of a client tells what its player listens to
=================
*/
void Cmd_JPStatus_f( gentity_t *ent ) {
	jpClient_t	*jp = &ent->client->pers.jp;
	char		kind[16], text[sizeof( jp->statusMusic )];
	char		*in, *out;

	if ( jp->statusCmdTime && level.time - jp->statusCmdTime < STATUS_CMD_MSEC && level.time >= jp->statusCmdTime ) {
		return;		// the cgame asks again when it does not hear its own status back
	}
	jp->statusCmdTime = level.time;

	trap->Argv( 1, kind, sizeof( kind ) );
	text[0] = '\0';
	if ( !Q_stricmp( kind, "music" ) ) {
		JP_ChatCopy( ent, ConcatArgs( 2 ), text, sizeof( text ) );
		Q_StripColor( text );
		// no control characters, and nothing that ends the quotes of the command it is passed on in
		for ( in = out = text; *in; in++ ) {
			if ( (unsigned char)*in >= ' ' && *in != 127 && *in != '"' && *in != ';' && *in != '%' ) {
				*out++ = *in;
			}
		}
		*out = '\0';
	}
	if ( !strcmp( text, jp->statusMusic ) ) {
		// nothing new. The client still hears what the others are told, so it knows its command arrived.
		G_StatusTell( ent, ent );
		return;
	}
	Q_strncpyz( jp->statusMusic, text, sizeof( jp->statusMusic ) );
	G_StatusBroadcast( ent );
}
