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

// g_mapvote.c -- VisionJK: the vote for the next map at the end of a level (vs_mapVote). When the intermission
// begins the server picks up to vs_mapVoteCount maps of its arena files that have the game type (not the one
// just played) and offers them with the next map of its rotation as the last choice. Each player has one vote
// (vs_mapvote <n>, a client of this tree sends it from its keys 1 to 4 and draws the choices); the intermission
// lasts until vs_mapVoteTime is over or every player who is not a bot has voted. The map with the most votes is
// loaded, on a tie the first of them, without votes the rotation goes on as before. The state is the
// configstring CS_VISION_MAPVOTE, which a client of the game does not know and leaves alone; such clients get the
// choices as a line of text with the command. Not in duels, power duels and siege with switching teams, which
// have intermissions of their own. Not from JA+.

#include "g_local.h"

#define MAPVOTE_MAX		4			// the maps, with the rotation as the last one

const char *G_GetArenaInfoByMap( const char *map );	// g_bot.c

static struct {
	qboolean	active;
	int			endTime;
	int			count;					// the choices, the rotation the last of them
	char		maps[MAPVOTE_MAX][MAX_QPATH];
	int			choice[MAX_CLIENTS];	// 1 to count, 0 none
	int			winner;					// -1 the rotation
} mapVote;

// the state as the configstring: the end, the choices (the map and the long name of its arena, "" for the
// rotation) and the votes of each
static void MapVote_Send( void ) {
	char	info[MAX_INFO_STRING];
	int		votes[MAPVOTE_MAX] = { 0 };
	int		i;

	info[0] = '\0';
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( mapVote.choice[i] >= 1 && mapVote.choice[i] <= mapVote.count ) {
			votes[mapVote.choice[i] - 1]++;
		}
	}
	Info_SetValueForKey( info, "t", va( "%i", mapVote.endTime ) );
	Info_SetValueForKey( info, "n", va( "%i", mapVote.count ) );
	for ( i = 0; i < mapVote.count; i++ ) {
		const char	*arena = mapVote.maps[i][0] ? G_GetArenaInfoByMap( mapVote.maps[i] ) : NULL;
		char		longName[64];

		Q_strncpyz( longName, arena ? Info_ValueForKey( arena, "longname" ) : "", sizeof( longName ) );
		Info_SetValueForKey( info, va( "m%i", i ), mapVote.maps[i] );
		Info_SetValueForKey( info, va( "l%i", i ), longName );
		Info_SetValueForKey( info, va( "v%i", i ), va( "%i", votes[i] ) );
	}
	trap->SetConfigstring( CS_VISION_MAPVOTE, info );
}

/*
=================
G_MapVoteStart

From BeginIntermission. Picks the maps; without any to offer there is no vote.
=================
*/
void G_MapVoteStart( void ) {
	char		current[MAX_QPATH];
	int			pool[MAX_ARENAS];
	int			poolCount = 0, n, i;

	memset( &mapVote, 0, sizeof( mapVote ) );
	mapVote.winner = -1;
	if ( !vs_mapVote.integer || level.gametype == GT_DUEL || level.gametype == GT_POWERDUEL
		|| ( level.gametype == GT_SIEGE && g_siegeTeamSwitch.integer ) ) {
		return;
	}
	trap->Cvar_VariableStringBuffer( "mapname", current, sizeof( current ) );
	// (Info_ValueForKey hands back a buffer it uses again: the name copied before anything else asks for one)
	for ( n = 0; n < level.arenas.num && poolCount < (int)ARRAY_LEN( pool ); n++ ) {
		char map[MAX_QPATH];

		Q_strncpyz( map, Info_ValueForKey( level.arenas.infos[n], "map" ), sizeof( map ) );
		if ( map[0] && Q_stricmp( map, current ) && G_DoesMapSupportGametype( map, level.gametype ) ) {
			pool[poolCount++] = n;
		}
	}
	if ( !poolCount ) {
		return;
	}
	// a few of them by chance, then the rotation
	n = Q_max( 1, Q_min( MAPVOTE_MAX - 1, vs_mapVoteCount.integer ) );
	for ( i = 0; i < n && poolCount > 0; i++ ) {
		const int pick = rand() % poolCount;

		Q_strncpyz( mapVote.maps[mapVote.count++], Info_ValueForKey( level.arenas.infos[pool[pick]], "map" ), sizeof( mapVote.maps[0] ) );
		pool[pick] = pool[--poolCount];
	}
	mapVote.maps[mapVote.count++][0] = '\0';
	mapVote.endTime = level.time + 1000 * Q_max( 5, Q_min( 60, vs_mapVoteTime.integer ) );
	mapVote.active = qtrue;
	MapVote_Send();

	// the clients of the game get it as text
	{
		char line[MAX_STRING_CHARS];

		Q_strncpyz( line, "print \"^5Next map:^7", sizeof( line ) );
		for ( i = 0; i < mapVote.count; i++ ) {
			Q_strcat( line, sizeof( line ), va( "  %i %s", i + 1, mapVote.maps[i][0] ? mapVote.maps[i] : "(rotation)" ) );
		}
		Q_strcat( line, sizeof( line ), "  ^5- /vs_mapvote <number>\n\"" );
		trap->SendServerCommand( -1, line );
	}
}

// the vote of a player (vs_mapvote <n>); 0 takes it back
void Cmd_MapVote_f( gentity_t *ent ) {
	char	arg[8];
	int		n;

	if ( !mapVote.active || level.time >= mapVote.endTime || !ent->client ) {
		return;
	}
	trap->Argv( 1, arg, sizeof( arg ) );
	n = atoi( arg );
	if ( n < 0 || n > mapVote.count ) {
		return;
	}
	mapVote.choice[ent - g_entities] = n;
	MapVote_Send();
}

/*
=================
G_MapVoteRunning

From CheckIntermissionExit: qtrue while the vote holds the intermission, which is until its time is over or every
player who is no bot has voted. Then the winner is chosen.
=================
*/
qboolean G_MapVoteRunning( void ) {
	int votes[MAPVOTE_MAX] = { 0 }, best = 0, i;
	qboolean waiting = qfalse;

	if ( !mapVote.active ) {
		return qfalse;
	}
	for ( i = 0; i < level.maxclients; i++ ) {
		const gclient_t *cl = &level.clients[i];

		if ( cl->pers.connected != CON_CONNECTED ) {
			continue;
		}
		if ( mapVote.choice[i] >= 1 && mapVote.choice[i] <= mapVote.count ) {
			votes[mapVote.choice[i] - 1]++;
		} else if ( !( g_entities[i].r.svFlags & SVF_BOT ) ) {
			waiting = qtrue;
		}
	}
	if ( waiting && level.time < mapVote.endTime ) {
		return qtrue;
	}
	// the most votes, the first of them on a tie; none: the rotation
	for ( i = 1; i < mapVote.count; i++ ) {
		if ( votes[i] > votes[best] ) {
			best = i;
		}
	}
	mapVote.winner = ( votes[best] > 0 && mapVote.maps[best][0] ) ? best : -1;
	mapVote.active = qfalse;
	return qfalse;
}

/*
=================
G_MapVoteNextMap

From ExitLevel: the map the vote chose, NULL for the rotation.
=================
*/
const char *G_MapVoteNextMap( void ) {
	if ( mapVote.winner < 0 || mapVote.winner >= mapVote.count || !mapVote.maps[mapVote.winner][0] ) {
		return NULL;
	}
	return mapVote.maps[mapVote.winner];
}
