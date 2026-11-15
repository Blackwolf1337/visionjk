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

// g_anticheat.c -- moves that no hand makes. Not in JA+ and not in OpenJK.
//
// Two things, both server side, so they hold for every client, stock and legacy ones included:
//
// - The time of one move (G_AC_MoveTime, vs_antiSpeed). A stock client sends a move for every frame it draws,
//   8 to 33 ms. The game ran whatever time a move claimed, up to a second (Pmove), so a client that held its moves
//   back and then sent one far ahead covered that distance in one step and appeared somewhere else: the old
//   h_teleport cheat (moves 850 ms in the past, then back to the present), a lag switch, h_dontsend (packets
//   without moves, then the burst). With vs_antiSpeed 2 a move is cut to vs_maxCmdMsec: the rest of its time is
//   skipped, the player stays where the server had him. The engine writes down the same moves (sv_movecheck.cpp).
//   A clock that runs fast gains nothing here, see the note at the top of server/sv_ids.cpp.
//
// - The wiggle bot (G_AC_Think, vs_antiWiggle). With the single player saber damage (d_saberSPStyleDamage 1)
//   a swing hurts on every server frame its blade crosses a body, more for a longer sweep, so turning the view
//   back and forth fast during a swing deals more. Players do it by hand, a bot does it faster and evenly. The
//   yaw of every move is looked at while the player swings at an enemy in reach: the turns that reverse at more
//   than vs_wiggleRate a second, in two one-second windows in a row, flag the player. A hand reaches 6 to 10
//   reversals a second. vs_antiWiggle 2: his saber then hurts no player for vs_wiggleBlockTime seconds (the
//   swing goes on, so a false alarm costs a few seconds of damage). 3: kicked after the third flag.
//   It is a guess from the moves: a bot that wiggles slowly and unevenly looks like a hand.
//
// Each flag is a line in the security log and on the console. The first in a minute also writes the last moves to
// anticheat/<date>_<address>_<what>.csv in the game folder, so the limits can be set from what a cheat really does.

#include "game/g_local.h"

#define AC_WARP_LOG_MSEC		250		// a cut move this much longer than the time that passed is written down
#define AC_WARP_KICK_CUTS		5		// vs_antiSpeed 3: cut moves of that length in AC_WARP_KICK_MSEC
#define AC_WARP_KICK_MSEC		60000

#define AC_WIGGLE_REACH			128.0f	// an enemy closer than this counts as in reach of the saber
#define AC_WIGGLE_TURN			150.0f	// degrees per second: slower turns are not counted as wiggling
#define AC_WIGGLE_JUMP			90.0f	// a change of the yaw this big in one move is a teleport or a respawn
#define AC_WIGGLE_WINDOW		1000	// msec of moves per window
#define AC_WIGGLE_MIN_ENGAGED	400		// msec of a window that have to be engaged for it to count
#define AC_WIGGLE_MIN_REVS		6
#define AC_WIGGLE_EVEN			0.6f	// intervals that vary less than this (relative) are even, as a bot's
#define AC_WIGGLE_KICK_FLAGS	3
#define AC_DUMP_MSEC			60000

/*
=================
G_AC_Dump

Writes the moves of the wiggle check of a player as a table.
=================
*/
static void G_AC_Dump( gentity_t *ent, const char *what, const char *why ) {
	anticheat_t		*ac = &ent->client->pers.anticheat;
	char			path[MAX_QPATH], address[64], line[256];
	qtime_t			t;
	fileHandle_t	f;
	int				i, first;

	if ( ac->dumpTime && level.time - ac->dumpTime < AC_DUMP_MSEC && level.time >= ac->dumpTime ) {
		return;
	}
	ac->dumpTime = level.time;

	Q_strncpyz( address, ent->client->sess.IP, sizeof( address ) );
	for ( i = 0; address[i]; i++ ) {
		if ( address[i] == ':' || address[i] == '[' || address[i] == ']' ) {
			address[i] = '_';		// not in a file name on Windows
		}
	}
	memset( &t, 0, sizeof( t ) );
	trap->RealTime( &t );
	Com_sprintf( path, sizeof( path ), "anticheat/%04i%02i%02i_%02i%02i%02i_%s_%s.csv", 1900 + t.tm_year, 1 + t.tm_mon,
		t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, address, what );
	trap->FS_Open( path, &f, FS_WRITE );
	if ( !f ) {
		return;
	}

	Com_sprintf( line, sizeof( line ), "# %s^7, client %i, %s\n", ent->client->pers.netname, ent->s.number, why );
	trap->FS_Write( line, (int)strlen( line ), f );
	Q_strncpyz( line, "time,yaw,speed,enemyErr,engaged\n", sizeof( line ) );
	trap->FS_Write( line, (int)strlen( line ), f );

	first = ( ac->head - ac->count + AC_WIGGLE_SAMPLES ) % AC_WIGGLE_SAMPLES;
	for ( i = 0; i < ac->count; i++ ) {
		const acSample_t *s = &ac->samples[( first + i ) % AC_WIGGLE_SAMPLES];

		Com_sprintf( line, sizeof( line ), "%i,%.1f,%.0f,%.1f,%i\n", s->time - ac->samples[first].time, s->yaw, s->speed,
			s->enemyErr, s->engaged );
		trap->FS_Write( line, (int)strlen( line ), f );
	}
	trap->FS_Close( f );
}

// security.log, and the console of a dedicated server
static void G_AC_Log( const gentity_t *ent, const char *text ) {
	G_SecurityLogPrintf( "AntiCheat: %i [%s] \"%s^7\" %s\n", ent->s.number, ent->client->sess.IP, ent->client->pers.netname, text );
}

/*
=================
G_AC_MoveTime

ClientThink_real, for a move a client sent (not the null move of G_RunClient, not bots or NPCs), after msec, the
time the move claims, is known. Returns the msec that is run.
=================
*/
int G_AC_MoveTime( gentity_t *ent, usercmd_t *ucmd, int msec ) {
	gclient_t	*client = ent->client;
	anticheat_t	*ac = &client->pers.anticheat;
	const int	cap = Com_Clampi( 16, 1000, vs_maxCmdMsec.integer );
	const int	mode = vs_antiSpeed.integer;
	// how much more time the move claims than passed on the server since the move before: a client that froze
	// for a moment claims about the time that passed, one that held its moves back claims far more
	const int	heldBack = ac->lastMoveLevelTime && level.time >= ac->lastMoveLevelTime ? msec - ( level.time - ac->lastMoveLevelTime ) : 0;

	ac->lastMoveLevelTime = level.time;
	if ( mode <= 0 || msec <= cap || client->sess.sessionTeam == TEAM_SPECTATOR ) {
		return msec;
	}

	if ( heldBack >= AC_WARP_LOG_MSEC ) {
		if ( !ac->warpCutsStart || level.time - ac->warpCutsStart > AC_WARP_KICK_MSEC || level.time < ac->warpCutsStart ) {
			ac->warpCutsStart = level.time;
			ac->warpCuts = 0;
		}
		ac->warpCuts++;
		if ( !ac->warpLogTime || level.time - ac->warpLogTime >= 2000 || level.time < ac->warpLogTime ) {
			ac->warpLogTime = level.time;
			G_AC_Log( ent, va( "sent one move of %i ms, %i ms more than passed (%i such in the last minute)%s", msec, heldBack, ac->warpCuts,
				mode >= 2 ? va( ", run as %i ms", cap ) : "" ) );
		}
		if ( mode >= 3 && ac->warpCuts >= AC_WARP_KICK_CUTS ) {
			G_AC_Log( ent, "kicked: moves held back and sent at once" );
			trap->DropClient( ent->s.number, "was kicked: moves held back (lag or teleport cheat)" );
			return 0;
		}
	}

	if ( mode < 2 ) {
		return msec;
	}
	client->ps.commandTime = ucmd->serverTime - cap;
	return cap;
}

/*
=================
G_AC_Enemy

The nearest enemy of a player in reach of his saber: the yaw from him to it, qfalse for none.
=================
*/
static qboolean G_AC_Enemy( const gentity_t *ent, float *bearing ) {
	float	best = AC_WIGGLE_REACH * AC_WIGGLE_REACH;
	int		i;
	qboolean found = qfalse;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const gentity_t	*other = &g_entities[i];
		vec3_t			d;
		float			dist;

		if ( other == ent || !other->inuse || !other->client || other->client->pers.connected != CON_CONNECTED
			|| other->health <= 0 || other->client->sess.sessionTeam == TEAM_SPECTATOR || other->client->tempSpectate >= level.time ) {
			continue;
		}
		if ( level.gametype >= GT_TEAM && other->client->sess.sessionTeam == ent->client->sess.sessionTeam ) {
			continue;
		}
		VectorSubtract( other->r.currentOrigin, ent->r.currentOrigin, d );
		dist = VectorLengthSquared( d );
		if ( dist < best ) {
			best = dist;
			*bearing = vectoyaw( d );
			found = qtrue;
		}
	}
	return found;
}

/*
=================
G_AC_WindowEnd

A window of the wiggle check is over: is it one of a bot?
=================
*/
static void G_AC_WindowEnd( gentity_t *ent ) {
	anticheat_t	*ac = &ent->client->pers.anticheat;
	const int	mode = vs_antiWiggle.integer;
	float		rate, mean = 0.0f, spread = 0.0f;
	qboolean	bad;

	if ( ac->windowEngaged < AC_WIGGLE_MIN_ENGAGED ) {
		return;		// not fighting enough for a verdict, the count in a row stays
	}
	rate = ac->windowRevs * 1000.0f / ac->windowEngaged;
	if ( ac->windowIntervals >= 2 ) {
		mean = ac->windowIntervalSum / ac->windowIntervals;
		spread = ac->windowIntervalSq / ac->windowIntervals - mean * mean;
		spread = spread > 0.0f ? sqrtf( spread ) / Q_max( mean, 1.0f ) : 0.0f;
	}
	// fast enough, and either even like a machine or far past any hand
	bad = ac->windowRevs >= AC_WIGGLE_MIN_REVS && rate >= vs_wiggleRate.value
		&& ( ( ac->windowIntervals >= 2 && spread < AC_WIGGLE_EVEN ) || rate >= vs_wiggleRate.value * 1.5f );
	ac->windowsBad = bad ? ac->windowsBad + 1 : 0;
	if ( ac->windowsBad < 2 ) {
		return;
	}
	ac->windowsBad = 0;
	ac->flags++;

	{
		const char *why = va( "turns like a wiggle bot: %.1f reversals a second (limit %.1f), intervals %.0f ms +-%.0f%%, flag %i",
			rate, vs_wiggleRate.value, mean, spread * 100.0f, ac->flags );

		G_AC_Log( ent, mode >= 2 ? va( "%s, saber hurts no player for %i s", why, vs_wiggleBlockTime.integer ) : why );
		G_AC_Dump( ent, "wiggle", why );
	}
	if ( mode >= 2 ) {
		ac->blockUntil = level.time + Com_Clampi( 0, 60, vs_wiggleBlockTime.integer ) * 1000;
	}
	if ( mode >= 3 && ac->flags >= AC_WIGGLE_KICK_FLAGS ) {
		G_AC_Log( ent, "kicked: wiggle bot" );
		trap->DropClient( ent->s.number, "was kicked: wiggle bot" );
	}
}

/*
=================
G_AC_Think

ClientThink_real, after Pmove ran a move a client sent.
=================
*/
void G_AC_Think( gentity_t *ent, const usercmd_t *ucmd ) {
	gclient_t	*client = ent->client;
	anticheat_t	*ac = &client->pers.anticheat;
	const int	dt = ucmd->serverTime - ac->lastTime;
	acSample_t	*s;
	float		yaw, dyaw, speed, bearing = 0.0f;
	qboolean	engaged = qfalse, enemy;
	int			dir;

	if ( vs_antiWiggle.integer <= 0 ) {
		return;
	}
	if ( !ac->lastTime || dt <= 0 || dt > 250 ) {
		// the first move, or one from long ago: start anew
		ac->lastTime = ucmd->serverTime;
		ac->lastYaw = ucmd->angles[YAW];
		ac->lastDir = 0;
		ac->windowStart = ucmd->serverTime;
		ac->windowEngaged = ac->windowRevs = ac->windowIntervals = 0;
		ac->windowIntervalSum = ac->windowIntervalSq = 0.0f;
		return;
	}

	yaw = SHORT2ANGLE( ucmd->angles[YAW] + client->ps.delta_angles[YAW] );
	dyaw = AngleNormalize180( SHORT2ANGLE( (short)( ucmd->angles[YAW] - ac->lastYaw ) ) );
	speed = dyaw * 1000.0f / dt;
	ac->lastYaw = ucmd->angles[YAW];
	ac->lastTime = ucmd->serverTime;

	enemy = G_AC_Enemy( ent, &bearing );
	if ( enemy && client->ps.weapon == WP_SABER && !client->ps.saberHolstered && client->sess.sessionTeam != TEAM_SPECTATOR
		&& ent->health > 0 && ( BG_SaberInAttack( client->ps.saberMove ) || BG_SaberInSpecial( client->ps.saberMove )
			|| ( ucmd->buttons & BUTTON_ATTACK ) ) ) {
		engaged = qtrue;
	}

	s = &ac->samples[ac->head];
	s->time = ucmd->serverTime;
	s->yaw = yaw;
	s->speed = speed;
	s->enemyErr = enemy ? AngleDelta( yaw, bearing ) : 999.0f;
	s->engaged = (byte)engaged;
	ac->head = ( ac->head + 1 ) % AC_WIGGLE_SAMPLES;
	if ( ac->count < AC_WIGGLE_SAMPLES ) {
		ac->count++;
	}

	if ( fabsf( dyaw ) > AC_WIGGLE_JUMP ) {
		ac->lastDir = 0;		// a teleport or a respawn set the view
	} else {
		dir = speed > AC_WIGGLE_TURN ? 1 : ( speed < -AC_WIGGLE_TURN ? -1 : 0 );
		if ( dir ) {
			if ( ac->lastDir && dir != ac->lastDir && engaged ) {
				const int interval = ucmd->serverTime - ac->lastRevTime;

				ac->windowRevs++;
				if ( ac->lastRevTime && interval > 0 && interval < 500 ) {
					ac->windowIntervals++;
					ac->windowIntervalSum += interval;
					ac->windowIntervalSq += (float)interval * interval;
				}
				ac->lastRevTime = ucmd->serverTime;
			}
			ac->lastDir = dir;
		}
	}
	if ( engaged ) {
		ac->windowEngaged += dt;
	}

	if ( ucmd->serverTime - ac->windowStart >= AC_WIGGLE_WINDOW ) {
		G_AC_WindowEnd( ent );
		ac->windowStart = ucmd->serverTime;
		ac->windowEngaged = ac->windowRevs = ac->windowIntervals = 0;
		ac->windowIntervalSum = ac->windowIntervalSq = 0.0f;
	}
}

/*
=================
G_AC_SaberBlocked

WP_SaberApplyDamage: qtrue when the saber of attacker must not hurt victim now.
=================
*/
qboolean G_AC_SaberBlocked( const gentity_t *attacker, const gentity_t *victim ) {
	if ( !attacker->client || !victim->client || victim->s.number >= MAX_CLIENTS || vs_antiWiggle.integer < 2 ) {
		return qfalse;
	}
	return attacker->client->pers.anticheat.blockUntil > level.time;
}
