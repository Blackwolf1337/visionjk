/*
===========================================================================
Copyright (C) 2013 - 2015, OpenJK contributors
Copyright (C) 2026, VisionJK contributors

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

// wa_actions.c -- /api/action of the web admin panel: what is done to a player (kick, ban, health,
// teleport, the JA+ commands, a gravity of his own) or to the server (a map, a bot, a line of chat), and the
// list of the bans.

#include "game/webadmin/wa_local.h"

extern void G_Kill( gentity_t *ent );

// a gravity of his own for a player, set in the panel. Not in JKAdmin. Gone with the map like the chat lines.
#define WA_MIN_GRAVITY	-2000
#define WA_MAX_GRAVITY	5000

static qboolean	clientGravitySet[MAX_CLIENTS];
static float	clientGravity[MAX_CLIENTS];

void WA_ActionResult( waConnection_t *c, qboolean ok, const char *msg ) {
	waBuffer_t b = { 0 };

	WA_BufPrintf( &b, "{\"ok\":%s,\"message\":", ok ? "true" : "false" );
	WA_BufJsonString( &b, msg );
	WA_BufAppend( &b, "}", 1 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

/*
=================
WA_JpAction

Not in JKAdmin: the JA+ admin commands that work on a player. They run as typed on the server console
(<command> <player number> [text]), so the rules, messages and logs are those of JA+. The command runs in the
next frame, the answer only says that it was sent. Returns qfalse if the action is none of them.
=================
*/
static qboolean WA_JpAction( waConnection_t *c, const char *action, int id, char *value ) {
	static const struct {
		const char	*name;
		qboolean	text;		// the value goes along as text
	} cmds[] = {
		{ "amsleep", qfalse },			{ "amwake", qfalse },			{ "amprotect", qfalse },
		{ "amempower", qfalse },		{ "ammerc", qfalse },			{ "ammindtrick", qfalse },
		{ "amghost", qfalse },			{ "amsilence", qfalse },		{ "amunsilence", qfalse },
		{ "amslap", qfalse },			{ "amForceAltDim", qfalse },	{ "amUnForceAltDim", qfalse },
		{ "amdenyvote", qfalse },		{ "amShowMotd", qfalse },		{ "amrename", qtrue },
		{ "ampsay", qtrue },
	};
	gclient_t	*client = g_entities[id].client;
	size_t		i;

	if ( !strcmp( action, "freeze" ) ) {
		// in JA+ a client command of the rcon admins, there is no console command for it
		client->pers.jp.frozen = (qboolean)!client->pers.jp.frozen;
		WA_ActionResult( c, qtrue, client->pers.jp.frozen ? "frozen" : "not frozen any more" );
		return qtrue;
	}

	if ( !strcmp( action, "admgrant" ) ) {
		if ( strcmp( value, "council" ) && strcmp( value, "knight" ) && strcmp( value, "instructor" ) && strcmp( value, "no" ) ) {
			WA_ActionResult( c, qfalse, "rank must be council, knight, instructor or no" );
			return qtrue;
		}
		trap->SendConsoleCommand( EXEC_APPEND, va( "admGrant %d %s\n", id, value ) );
		WA_ActionResult( c, qtrue, va( "admGrant %s sent", value ) );
		return qtrue;
	}

	for ( i = 0; i < ARRAY_LEN( cmds ); i++ ) {
		if ( strcmp( action, cmds[i].name ) )
			continue;

		if ( cmds[i].text ) {
			WA_Utf8ToLatin1( value );
			Q_strstrip( value, "\n\r\";", NULL );
			if ( !value[0] ) {
				WA_ActionResult( c, qfalse, "no text" );
				return qtrue;
			}
			trap->SendConsoleCommand( EXEC_APPEND, va( "%s %d \"%s\"\n", cmds[i].name, id, value ) );
		} else {
			trap->SendConsoleCommand( EXEC_APPEND, va( "%s %d\n", cmds[i].name, id ) );
		}
		WA_ActionResult( c, qtrue, va( "%s sent", cmds[i].name ) );
		return qtrue;
	}
	return qfalse;
}


// What is done to the server and not to one player: a line of chat, a bot, a map, the gravity of all, a ban or
// its end by address, an effect or a sound at a place. qfalse: the action is none of these.
static qboolean WA_ServerAction( waConnection_t *c, const char *action, char *value, const char *body ) {
	char xs[32], ys[32], zs[32];

	if ( !strcmp( action, "say" ) ) {
		WA_Utf8ToLatin1( value );
		Q_strstrip( value, "\n\r\";", NULL );
		trap->SendServerCommand( -1, va( "chat \"" S_COLOR_RED "Admin" S_COLOR_WHITE ": %s\"", value ) );
		WebAdmin_Chat( "panel", NULL, NULL, value );
		WA_ActionResult( c, qtrue, "message sent" );
		return qtrue;
	}
	if ( !strcmp( action, "addbot" ) ) {
		Q_strstrip( value, "\n\r\";", NULL );
		trap->SendConsoleCommand( EXEC_APPEND, va( "addbot \"%s\" 3\n", value[0] ? value : "Kyle Katarn" ) );	// JKAdmin says "Kyle", no bot has that name
		WA_ActionResult( c, qtrue, "bot added" );
		return qtrue;
	}
	if ( !strcmp( action, "map" ) ) {
		Q_strstrip( value, "\n\r\";", NULL );
		if ( !value[0] || strstr( value, ".." ) ) {
			WA_ActionResult( c, qfalse, "no map name" );
			return qtrue;
		}
		trap->SendConsoleCommand( EXEC_APPEND, va( "map \"%s\"\n", value ) );
		WA_ActionResult( c, qtrue, "changing map" );
		return qtrue;
	}

	if ( !strcmp( action, "reload" ) ) {
		// not in JKAdmin: the running map again, so the engine finds a pk3 that was uploaded
		WA_ReloadMap( c );
		return qtrue;
	}

	if ( !strcmp( action, "gravity" ) ) {
		// not in JKAdmin: g_gravity for everybody. The map sets it again at its next start (worldspawn key "gravity").
		if ( !value[0] ) {
			WA_ActionResult( c, qfalse, "no gravity value" );
			return qtrue;
		}
		{
			const int v = Com_Clampi( WA_MIN_GRAVITY, WA_MAX_GRAVITY, atoi( value ) );

			trap->Cvar_Set( "g_gravity", va( "%d", v ) );
			trap->Cvar_Update( &g_gravity );
			WA_ActionResult( c, qtrue, va( "gravity %d", v ) );
		}
		return qtrue;
	}

	if ( !strcmp( action, "banip" ) || !strcmp( action, "unban" ) ) {
		// Not in JKAdmin: a ban by its address, which is how one is lifted. addip and removeip of the game do it,
		// and write the file of the bans. Only what such an address is made of goes into the line.
		if ( !value[0] || strlen( value ) > 15 || strspn( value, "0123456789.*" ) != strlen( value ) || !strpbrk( value, "0123456789" ) ) {
			WA_ActionResult( c, qfalse, "not an address: four numbers with dots, a * for any" );
			return qtrue;
		}
		trap->SendConsoleCommand( EXEC_APPEND, va( "%s %s\n", action[0] == 'b' ? "addip" : "removeip", value ) );
		WA_ActionResult( c, qtrue, va( "%s %s", action[0] == 'b' ? "banned" : "ban lifted for", value ) );
		return qtrue;
	}

	if ( !strcmp( action, "effect" ) || !strcmp( action, "sound" ) ) {
		// Not in JKAdmin: an effect or a sound of the game once at a place, so that who is in the game sees and
		// hears what a key of an entity would bring. Its name takes a place among the config strings if it has
		// none yet, and the server would end without one left.
		const qboolean	fx = (qboolean)( action[0] == 'e' );
		vec3_t			at, up = { 0, 0, 1 };

		Q_strstrip( value, "\n\r\";", NULL );
		if ( !value[0] || strstr( value, ".." ) || !WA_FormValue( body, "x", xs, sizeof( xs ) ) || !WA_FormValue( body, "y", ys, sizeof( ys ) )
			|| !WA_FormValue( body, "z", zs, sizeof( zs ) ) ) {
			WA_ActionResult( c, qfalse, "no name or no place" );
			return qtrue;
		}
		if ( !WA_ConfigstringRoom( fx ? CS_EFFECTS : CS_SOUNDS, fx ? MAX_FX : MAX_SOUNDS, value ) ) {
			WA_ActionResult( c, qfalse, va( "the map has no room for another one of its %s", fx ? "effects" : "sounds" ) );
			return qtrue;
		}
		VectorSet( at, Com_Clamp( -MAX_WORLD_COORD, MAX_WORLD_COORD, atof( xs ) ), Com_Clamp( -MAX_WORLD_COORD, MAX_WORLD_COORD, atof( ys ) ),
			Com_Clamp( -MAX_WORLD_COORD, MAX_WORLD_COORD, atof( zs ) ) );
		if ( fx )
			G_PlayEffectID( G_EffectIndex( value ), at, up );
		else
			G_SoundAtLoc( at, CHAN_AUTO, G_SoundIndex( value ) );
		WA_ActionResult( c, qtrue, va( "%s played in the game", value ) );
		return qtrue;
	}

	return qfalse;
}

// what is done to one player, who is there
static void WA_PlayerAction( waConnection_t *c, const char *action, int id, char *value, const char *body ) {
	char		xs[32], ys[32], zs[32];
	gentity_t	*ent = &g_entities[id];

	if ( !strcmp( action, "kick" ) ) {
		trap->SendConsoleCommand( EXEC_APPEND, va( "clientkick %d\n", id ) );
		WA_ActionResult( c, qtrue, "kicked" );
	} else if ( !strcmp( action, "ban" ) ) {
		char userinfo[MAX_INFO_STRING], ip[64], *port;

		char raw[64];

		// the stored IP is already the real one the engine set, this only keeps a console line clean
		trap->GetUserinfo( id, userinfo, sizeof( userinfo ) );
		Q_strncpyz( raw, Info_ValueForKey( userinfo, "ip" ), sizeof( raw ) );
		port = strchr( raw, ':' );
		if ( port )
			*port = '\0';
		if ( !raw[0] || !Q_stricmp( raw, "localhost" ) || !Q_stricmp( raw, "bot" ) ) {
			WA_ActionResult( c, qfalse, "this player has no IP to ban" );
			return;
		}
		if ( !G_CleanIP( raw, ip, sizeof( ip ) ) ) {
			WA_ActionResult( c, qfalse, "this player's IP is not a plain address" );
			return;
		}
		trap->SendConsoleCommand( EXEC_APPEND, va( "addip %s\nclientkick %d\n", ip, id ) );
		WA_ActionResult( c, qtrue, va( "banned %s", ip ) );
	} else if ( !strcmp( action, "health" ) ) {
		const int v = Com_Clampi( 1, 999, atoi( value ) );
		if ( ent->health <= 0 ) {
			WA_ActionResult( c, qfalse, "player is dead" );
			return;
		}
		ent->health = ent->client->ps.stats[STAT_HEALTH] = v;
		WA_ActionResult( c, qtrue, va( "health %d", v ) );
	} else if ( !strcmp( action, "armor" ) ) {
		const int v = Com_Clampi( 0, 999, atoi( value ) );
		ent->client->ps.stats[STAT_ARMOR] = v;
		WA_ActionResult( c, qtrue, va( "armor %d", v ) );
	} else if ( !strcmp( action, "scale" ) ) {
		const int v = Com_Clampi( 10, 500, atoi( value ) );
		char userinfo[MAX_INFO_STRING];

		// JA+ keeps the scale in pers.jp.modelScale and applies it again at every spawn
		trap->GetUserinfo( id, userinfo, sizeof( userinfo ) );
		JP_SetModelScale( ent, v / 100.0f, userinfo );
		WA_ActionResult( c, qtrue, va( "scale %d%%", v ) );
	} else if ( !strcmp( action, "kill" ) ) {
		const jpClient_t *jp = &ent->client->pers.jp;

		if ( ent->health <= 0 || ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
			WA_ActionResult( c, qfalse, "player is not alive" );
			return;
		}
		// JA+: the states the kill command refuses too
		if ( jp->sleeping || jp->down || jp->isGhost ) {
			WA_ActionResult( c, qfalse, "player is sleeping, knocked down or a ghost" );
			return;
		}
		ent->flags &= ~FL_GODMODE;
		G_Kill( ent );
		WA_ActionResult( c, qtrue, "killed" );
	} else if ( !strcmp( action, "god" ) ) {
		ent->flags ^= FL_GODMODE;
		// JA+: like the god cheat, so jp_allowGodTalk does not take it away with the chat bubble
		ent->client->pers.jp.cheatGod = (qboolean)( ( ent->flags & FL_GODMODE ) != 0 );
		WA_ActionResult( c, qtrue, ( ent->flags & FL_GODMODE ) ? "god mode on" : "god mode off" );
	} else if ( !strcmp( action, "noclip" ) ) {
		// not in JKAdmin: the noclip cheat for the selected player
		if ( ent->health <= 0 || ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
			WA_ActionResult( c, qfalse, "player is not alive" );
			return;
		}
		ent->client->noclip = (qboolean)!ent->client->noclip;
		WA_ActionResult( c, qtrue, ent->client->noclip ? "noclip on" : "noclip off" );
	} else if ( !strcmp( action, "team" ) ) {
		team_t		now;
		qboolean	ok;

		if ( strcmp( value, "red" ) && strcmp( value, "blue" ) && strcmp( value, "free" ) && strcmp( value, "spectator" ) ) {
			WA_ActionResult( c, qfalse, "team must be red, blue, free or spectator" );
			return;
		}
		// JA+: past the team locks and the team balance, like amforceteam
		JP_ForceTeam( ent, value );

		now = ent->client->sess.sessionTeam;
		if ( !strcmp( value, "spectator" ) )
			ok = (qboolean)( now == TEAM_SPECTATOR );
		else if ( level.gametype < GT_TEAM )
			ok = (qboolean)( now == TEAM_FREE );
		else if ( !strcmp( value, "free" ) )
			ok = (qboolean)( now == TEAM_RED || now == TEAM_BLUE );		// the game picks a team
		else
			ok = (qboolean)!strcmp( WA_TeamName( now ), value );
		// JA+: with g_teamForceBalance 2 or 3 the spawn in the bigger team can move the player straight back
		WA_ActionResult( c, ok, ok ? va( "team %s", WA_TeamName( now ) ) :
			( g_teamForceBalance.integer >= 2 && level.gametype >= GT_TEAM ) ?
			"the game did not keep the player there (g_teamForceBalance moves players to balance the teams)" :
			"the game refused the team change" );
	} else if ( !strcmp( action, "teleport" ) ) {
		vec3_t origin;

		if ( ent->health <= 0 || ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
			WA_ActionResult( c, qfalse, "player is not alive" );
			return;
		}
		// JA+: the target checks of rcon amtele
		if ( ent->client->ps.eFlags & EF_ALT_DIM ) {
			WA_ActionResult( c, qfalse, "player is in the alternate dimension" );
			return;
		}
		if ( BG_InGrappleMove( ent->client->ps.legsAnim ) || BG_InGrappleMove( ent->client->ps.torsoAnim ) ) {
			WA_ActionResult( c, qfalse, "player is in a grapple move" );
			return;
		}
		if ( ent->client->pers.jp.refuseTele ) {
			WA_ActionResult( c, qfalse, "this player refuses to be teleported" );
			return;
		}
		if ( !WA_FormValue( body, "x", xs, sizeof( xs ) ) || !WA_FormValue( body, "y", ys, sizeof( ys ) ) ||
			 !WA_FormValue( body, "z", zs, sizeof( zs ) ) ) {
			WA_ActionResult( c, qfalse, "no position" );
			return;
		}
		// the page sends the floor point, stand on it (JP_ManualTeleport lifts it by one unit)
		VectorSet( origin, atof( xs ), atof( ys ), atof( zs ) - ent->r.mins[2] );
		// JA+: frees the hook and honours jp_starTrekTeleport, the path of amtele
		JP_ManualTeleport( ent, origin, ent->client->ps.viewangles );
		WA_ActionResult( c, qtrue, jp_starTrekTeleport.integer == 1 ? "teleport started" : "teleported" );
	} else if ( !strcmp( action, "schizo" ) ) {
		// not in JKAdmin: hallucinations only this player gets, see g_schizo.c
		WA_ActionResult( c, qtrue, G_SchizoToggle( ent ) ? "schizophrenia on" : "schizophrenia off" );
	} else if ( !strcmp( action, "poop" ) ) {
		// not in JKAdmin: the emote ampoop for him, see g_poop.c. The rules of the emote still decide.
		JP_EmotePoop( ent );
		WA_ActionResult( c, qtrue, "ampoop sent" );
	} else if ( !strcmp( action, "flatulence" ) ) {
		// not in JKAdmin: he breaks wind all the time, see g_flatulence.c
		WA_ActionResult( c, qtrue, G_FlatulenceToggle( ent ) ? "flatulence on" : "flatulence off" );
	} else if ( !strcmp( action, "disorient" ) ) {
		// not in JKAdmin: the view sways and is thrown around, see g_disorient.c
		WA_ActionResult( c, qtrue, G_DisorientToggle( ent ) ? "disorientation on" : "disorientation off" );
	} else if ( !strcmp( action, "playergravity" ) ) {
		// not in JKAdmin: a gravity of his own. An empty value gives him the gravity of the server again.
		if ( !value[0] ) {
			clientGravitySet[id] = qfalse;
			WA_ActionResult( c, qtrue, "gravity of the server" );
		} else {
			const int v = Com_Clampi( WA_MIN_GRAVITY, WA_MAX_GRAVITY, atoi( value ) );

			clientGravity[id] = (float)v;
			clientGravitySet[id] = qtrue;
			WA_ActionResult( c, qtrue, va( "own gravity %d", v ) );
		}
	} else if ( !WA_JpAction( c, action, id, value ) ) {
		WA_ActionResult( c, qfalse, "unknown action" );
	}
}

void WA_ApiAction( waConnection_t *c, const char *body ) {
	char	action[32], idStr[16], value[256];
	int		id;

	WA_FormValue( body, "action", action, sizeof( action ) );
	WA_FormValue( body, "value", value, sizeof( value ) );
	WA_FormValue( body, "id", idStr, sizeof( idStr ) );
	WA_LogUse( c, va( "action %s id %s value \"%.100s\"", action, idStr[0] ? idStr : "-", value ) );

	if ( WA_ServerAction( c, action, value, body ) )
		return;

	// the rest is done to a player
	if ( !WA_FormValue( body, "id", idStr, sizeof( idStr ) ) ) {
		WA_ActionResult( c, qfalse, "no player id" );
		return;
	}
	id = atoi( idStr );
	if ( id < 0 || id >= level.maxclients || !g_entities[id].inuse || !g_entities[id].client ||
		 g_entities[id].client->pers.connected == CON_DISCONNECTED ) {
		WA_ActionResult( c, qfalse, "no such player" );
		return;
	}
	WA_PlayerAction( c, action, id, value, body );
}


// GET /api/bans: the addresses that are banned, as addip and removeip take them
void WA_ApiBans( waConnection_t *c ) {
	waBuffer_t	b = { 0 };
	char		address[32];
	qboolean	first = qtrue;
	int			i;

	WA_BufAppend( &b, "{\"bans\":[", 9 );
	for ( i = 0; i < G_BanListSize(); i++ ) {
		if ( !G_BanListEntry( i, address, sizeof( address ) ) )
			continue;
		if ( !first )
			WA_BufAppend( &b, ",", 1 );
		first = qfalse;
		WA_BufJsonString( &b, address );
	}
	WA_BufAppend( &b, "]}", 2 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

// qtrue: this player has a gravity of his own
qboolean WA_GravityIsSet( int clientNum ) {
	return clientGravitySet[clientNum];
}

// the own gravity goes with the player, the next one in his slot starts without
void WA_GravityFrame( void ) {
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		if ( clientGravitySet[i] && level.clients[i].pers.connected == CON_DISCONNECTED )
			clientGravitySet[i] = qfalse;
	}
}

/*
=================
WebAdmin_ClientGravity

Not in JKAdmin. ClientThink_real asks here for the gravity of a player: his own one if the panel gave him
one, else the gravity it passes in.
=================
*/
float WebAdmin_ClientGravity( const gentity_t *ent, float gravity ) {
	const int num = (int)( ent-g_entities );

	return ( num >= 0 && num < MAX_CLIENTS && clientGravitySet[num] ) ? clientGravity[num] : gravity;
}

