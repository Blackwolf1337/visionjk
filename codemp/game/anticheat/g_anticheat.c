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
// Three things, all server side, so they hold for every client, stock and legacy ones included:
//
// - The time of one move (G_AC_MoveTime, vs_antiSpeed). A stock client sends a move for every frame it draws,
//   8 to 33 ms. The game ran whatever time a move claimed, up to a second (Pmove), so a client that held its moves
//   back and then sent one far ahead covered that distance in one step and appeared somewhere else: the old
//   h_teleport cheat (moves 850 ms in the past, then back to the present), a lag switch, h_dontsend (packets
//   without moves, then the burst). With vs_antiSpeed 2 a move is cut to vs_maxCmdMsec: the rest of its time is
//   skipped, the player stays where the server had him. The engine writes down the same moves (sv_movecheck.cpp).
//   A clock that runs fast gains nothing here, see the note at the top of server/sv_ids.cpp.
//
// - The input of one move (G_AC_MoveInput, vs_antiSpeed as well). Two ways to run faster than the game allows:
//   A rolled view (the old h_speedhack cheat sends a roll angle): the sideways direction of a move comes from the
//   view, and rolled far enough it points forwards, so a strafe key adds to the run, up to 1.41 times the speed.
//   The engine drops the roll of every move with sv_legacyFixes 1; this is the same in the game module, so it holds
//   with that cvar off and on another engine. No stock client sends a roll on foot.
//   The zigzag: the strafe key flipped with every move while running. The acceleration of the game (Quake 3) adds
//   speed along the wish direction until the part of the velocity along it reaches the limit, so two wish
//   directions that take turns 45 degrees left and right of the run carry it past the limit: measured 285 in
//   place of 250 forwards, 212 in place of 188 backwards, with a change every move and just the same with one
//   every 40 ms; with one every 160 ms nothing is gained. So counting the changes alone does not tell it from a
//   hand that taps left and right: what is looked at is the changes together with what they gained. At least
//   AC_FLIP_MIN changes in AC_FLIP_WINDOW while the player ran on the ground faster than his speed allows for
//   AC_FLIP_FAST_MSEC of it: with vs_antiSpeed 2 the strafe of his moves does not count for AC_FLIP_BLOCK, so the
//   run goes straight at the speed of the game. A slower zigzag (under ten changes a second) still gains a few
//   percent.
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
// - The aimbot (G_AC_Aim, vs_antiAim), for the saber, force powers and guns alike. An aimbot turns the view onto
//   an enemy where the client sees him, which is behind the server by the lag: the enemy is taken from his trail
//   at the time of the move (G_UnlaggedOrigin), so the check sees what the client saw. Two signs, yaw only (an
//   aimbot may aim at the head or the chest, that is a pitch the check cannot know):
//   Lock: while the enemy crosses the view faster than AC_AIM_FAST degrees a second, the view stays within
//   AC_AIM_EXACT of him for most of that time, in two two-second windows in a row. A hand lags behind a target
//   that fast and overshoots it.
//   Snap: a turn of AC_AIM_SNAP_TURN degrees or more within AC_AIM_SNAP_MSEC that stops dead on an enemy, three
//   times in AC_AIM_SNAP_SPAN, or twice with an attack or an aimed force power (grip, lightning, drain, push,
//   pull, mind trick) within AC_AIM_ACT_MSEC of it (a trigger bot fires with the turn).
//   vs_antiAim 1 writes it down, 2 (default) also tells the player that his aim was flagged and logged. Nothing is
//   blocked: good players track closely too, this needs numbers from real cheats first.
//
// Each flag is a line in the security log and on the console. The first in a minute also writes the last moves to
// anticheat/<date>_<address>_<what>.csv in the game folder, so the limits can be set from what a cheat really does.

#include "game/g_local.h"

#define AC_WARP_LOG_MSEC		250		// a cut move this much longer than the time that passed is written down
#define AC_WARP_KICK_CUTS		5		// vs_antiSpeed 3: cut moves of that length in AC_WARP_KICK_MSEC
#define AC_WARP_KICK_MSEC		60000

#define AC_FLIP_WINDOW			1000	// msec
#define AC_FLIP_MIN				10		// changes of direction in that time
#define AC_FLIP_FAST_MSEC		300		// of it on the ground faster than AC_FLIP_FAST times his speed
#define AC_FLIP_FAST			1.06f
#define AC_FLIP_BLOCK			3000	// msec the strafe does not count after that
#define AC_INPUT_LOG_MSEC		5000	// at most one line a player in this time

#define AC_WIGGLE_REACH			128.0f	// an enemy closer than this counts as in reach of the saber
#define AC_WIGGLE_TURN			150.0f	// degrees per second: slower turns are not counted as wiggling
#define AC_WIGGLE_JUMP			90.0f	// a change of the yaw this big in one move is a teleport or a respawn
#define AC_WIGGLE_WINDOW		1000	// msec of moves per window
#define AC_WIGGLE_MIN_ENGAGED	400		// msec of a window that have to be engaged for it to count
#define AC_WIGGLE_MIN_REVS		6
#define AC_WIGGLE_EVEN			0.6f	// intervals that vary less than this (relative) are even, as a bot's
#define AC_WIGGLE_KICK_FLAGS	3
#define AC_DUMP_MSEC			60000

#define AC_AIM_RANGE			4096.0f	// enemies farther away are not looked at
#define AC_AIM_CONE				30.0f	// degrees of yaw: an enemy this far off the view is no aim target
#define AC_AIM_EXACT			1.0f	// degrees of yaw: on the enemy, as an aimbot puts the view
#define AC_AIM_FAST				90.0f	// degrees a second: an enemy crossing the view at least this fast
#define AC_AIM_WINDOW			2000	// msec of moves per window of the lock check
#define AC_AIM_MIN_FAST			500		// msec of a window the enemy has to cross fast for it to count
#define AC_AIM_LOCK_RATIO		0.8f	// of that time on him
#define AC_AIM_TURN				300.0f	// degrees a second: a turn this fast may become a snap
#define AC_AIM_SNAP_TURN		25.0f	// degrees a snap turns
#define AC_AIM_SNAP_MSEC		40		// within that time
#define AC_AIM_SNAP_SPAN		10000	// snaps counted in this time
#define AC_AIM_SNAPS			3
#define AC_AIM_SNAP_ACTS		2		// snaps with an attack or force power within AC_AIM_ACT_MSEC of them
#define AC_AIM_ACT_MSEC			150		// before or after the snap
#define AC_AIM_LOG_MSEC			5000	// at most one aim line a player in this time
#define AC_AIM_WARN_MSEC		60000	// at most one warning a player in this time

/*
=================
G_AC_Dump

Writes the last moves of a player as a table, with what the wiggle and the aim check saw in each.
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
	Q_strncpyz( line, "time,yaw,speed,enemyErr,engaged,aimTarget,aimErr,aimRate,aimVisible\n", sizeof( line ) );
	trap->FS_Write( line, (int)strlen( line ), f );

	first = ( ac->head - ac->count + AC_WIGGLE_SAMPLES ) % AC_WIGGLE_SAMPLES;
	for ( i = 0; i < ac->count; i++ ) {
		const acSample_t *s = &ac->samples[( first + i ) % AC_WIGGLE_SAMPLES];

		Com_sprintf( line, sizeof( line ), "%i,%.1f,%.0f,%.1f,%i,%i,%.2f,%.0f,%i\n", s->time - ac->samples[first].time, s->yaw,
			s->speed, s->enemyErr, s->engaged, s->aimTarget, s->aimErr, s->aimRate, s->aimVisible );
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
G_AC_MoveInput

ClientThink_real, for a move a client sent (not bots or NPCs), before it is run: the roll of the view and the
zigzag, see the top of the file. Changes the move.
=================
*/
void G_AC_MoveInput( gentity_t *ent, usercmd_t *ucmd, int msec ) {
	gclient_t	*client = ent->client;
	anticheat_t	*ac = &client->pers.anticheat;
	const int	mode = vs_antiSpeed.integer;

	if ( mode <= 0 || ( ent->r.svFlags & SVF_BOT ) || client->sess.sessionTeam == TEAM_SPECTATOR ) {
		return;
	}

	if ( ucmd->angles[ROLL] && !client->ps.m_iVehicleNum ) {
		if ( !ac->rollLogTime || level.time - ac->rollLogTime >= AC_INPUT_LOG_MSEC || level.time < ac->rollLogTime ) {
			ac->rollLogTime = level.time;
			G_AC_Log( ent, va( "sent a view rolled by %i degrees on foot%s", (int)SHORT2ANGLE( ucmd->angles[ROLL] ),
				mode >= 2 ? ", ignored" : "" ) );
		}
		if ( mode >= 2 ) {
			ucmd->angles[ROLL] = 0;
		}
	}

	// What the move before left: on the ground, in control, and faster than the speed the game gave him (which
	// has the slower run backwards, rage and the rest in it). A landing or a push is faster for a moment too, that
	// is why it has to last.
	if ( client->ps.groundEntityNum != ENTITYNUM_NONE && client->ps.pm_type == PM_NORMAL && !( client->ps.pm_flags & PMF_TIME_KNOCKBACK )
		&& !client->ps.m_iVehicleNum && client->ps.speed > 0
		&& VectorLength2( client->ps.velocity ) > client->ps.speed * AC_FLIP_FAST + 4.0f ) {
		ac->flipFastMsec += msec;
	}

	// a change of direction: the strafe or the run goes the other way than it last went (a move of rest between
	// the two does not hide it)
	if ( ( ucmd->rightmove > 0 && ac->lastRight < 0 ) || ( ucmd->rightmove < 0 && ac->lastRight > 0 )
		|| ( ucmd->forwardmove > 0 && ac->lastForward < 0 ) || ( ucmd->forwardmove < 0 && ac->lastForward > 0 ) ) {
		if ( !ac->flipsStart || level.time - ac->flipsStart > AC_FLIP_WINDOW || level.time < ac->flipsStart ) {
			ac->flipsStart = level.time;
			ac->flips = 0;
			ac->flipFastMsec = 0;
		}
		if ( ++ac->flips >= AC_FLIP_MIN && ac->flipFastMsec >= AC_FLIP_FAST_MSEC ) {
			if ( !ac->flipLogTime || level.time - ac->flipLogTime >= AC_INPUT_LOG_MSEC || level.time < ac->flipLogTime ) {
				ac->flipLogTime = level.time;
				G_AC_Log( ent, va( "changed the direction of his moves %i times in %i ms and ran faster than his speed allows for %i ms of it (zigzag)%s",
					ac->flips, level.time - ac->flipsStart, ac->flipFastMsec, mode >= 2 ? ", his strafe does not count for a moment" : "" ) );
			}
			ac->flipBlockUntil = level.time + AC_FLIP_BLOCK;
		}
	}
	if ( ucmd->forwardmove ) {
		ac->lastForward = ucmd->forwardmove;
	}
	if ( ucmd->rightmove ) {
		ac->lastRight = ucmd->rightmove;
	}

	if ( mode >= 2 && ac->flipBlockUntil > level.time && level.time + AC_FLIP_BLOCK >= ac->flipBlockUntil ) {
		// the axis he is not running along: for a zigzag of the strafe the strafe, for one of the run the run
		if ( ucmd->forwardmove ) {
			ucmd->rightmove = 0;
		} else {
			ucmd->forwardmove = 0;
		}
	}
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
G_AC_AimTarget

The enemy closest to the view in yaw, where the client saw him at time: his client number and the yaw from the eye
to him, -1 for none within AC_AIM_CONE and AC_AIM_RANGE.
=================
*/
static int G_AC_AimTarget( const gentity_t *ent, const vec3_t eye, float viewYaw, int time, float *bearing, vec3_t where ) {
	float	best = AC_AIM_CONE;
	int		i, found = -1;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const gentity_t	*other = &g_entities[i];
		vec3_t			origin, d;
		float			yaw, err;

		if ( other == ent || !other->inuse || !other->client || other->client->pers.connected != CON_CONNECTED
			|| other->health <= 0 || other->client->sess.sessionTeam == TEAM_SPECTATOR || other->client->tempSpectate >= level.time ) {
			continue;
		}
		if ( level.gametype >= GT_TEAM && other->client->sess.sessionTeam == ent->client->sess.sessionTeam ) {
			continue;
		}
		G_UnlaggedOrigin( other, time, origin );
		VectorSubtract( origin, eye, d );
		if ( VectorLengthSquared( d ) > AC_AIM_RANGE * AC_AIM_RANGE ) {
			continue;
		}
		yaw = vectoyaw( d );
		err = fabsf( AngleDelta( viewYaw, yaw ) );
		if ( err < best ) {
			best = err;
			found = i;
			*bearing = yaw;
			VectorCopy( origin, where );
		}
	}
	return found;
}

static void G_AC_AimFlag( gentity_t *ent, const char *why ) {
	anticheat_t *ac = &ent->client->pers.anticheat;

	ac->aimFlags++;
	if ( !ac->aimLogTime || level.time - ac->aimLogTime >= AC_AIM_LOG_MSEC || level.time < ac->aimLogTime ) {
		ac->aimLogTime = level.time;
		G_AC_Log( ent, va( "aims like an aimbot: %s, flag %i", why, ac->aimFlags ) );
		G_AC_Dump( ent, "aim", why );
	}
	if ( vs_antiAim.integer >= 2
		&& ( !ac->aimWarnTime || level.time - ac->aimWarnTime >= AC_AIM_WARN_MSEC || level.time < ac->aimWarnTime ) ) {
		ac->aimWarnTime = level.time;
		trap->SendServerCommand( ent->s.number, "cp \"^3Your aim was flagged as automated.\nThis was logged.\"" );
		trap->SendServerCommand( ent->s.number, "print \"^3Your aim was flagged as automated by the server. This was logged.\n\"" );
	}
}

// keeps the times of the last snaps, returns how many lie within AC_AIM_SNAP_SPAN
static int G_AC_AddSnap( int *times, int count, int now ) {
	int i, n = 0;

	for ( i = count - 1; i > 0; i-- ) {
		times[i] = times[i - 1];
	}
	times[0] = now;
	for ( i = 0; i < count; i++ ) {
		if ( times[i] && now - times[i] <= AC_AIM_SNAP_SPAN && now >= times[i] ) {
			n++;
		}
	}
	return n;
}

// a snap with a shot or an aimed force power within AC_AIM_ACT_MSEC of it
static void G_AC_SnapAct( gentity_t *ent, int now ) {
	anticheat_t	*ac = &ent->client->pers.anticheat;
	const int	acts = G_AC_AddSnap( ac->snapActTimes, ARRAY_LEN( ac->snapActTimes ), now );

	ac->lastSnapTime = 0;
	ac->lastActTime = 0;
	if ( acts >= AC_AIM_SNAP_ACTS ) {
		memset( ac->snapActTimes, 0, sizeof( ac->snapActTimes ) );
		G_AC_AimFlag( ent, va( "%i snaps onto an enemy with an attack or force power within %i ms of them, in %i s", acts,
			AC_AIM_ACT_MSEC, AC_AIM_SNAP_SPAN / 1000 ) );
	}
}

/*
=================
G_AC_Aim

G_AC_Think, for every move: the lock and the snap of an aimbot. s is the sample of the move, its aim fields are
filled in here.
=================
*/
static void G_AC_Aim( gentity_t *ent, const usercmd_t *ucmd, acSample_t *s, int dt, float yaw, float dyaw ) {
	gclient_t	*client = ent->client;
	anticheat_t	*ac = &client->pers.anticheat;
	const float	turn = dyaw * 1000.0f / dt;
	const int	pressed = ucmd->buttons & ~ac->lastButtons;
	// a shot or an aimed force power started with this move
	const qboolean acted = ( pressed & ( BUTTON_ATTACK | BUTTON_ALT_ATTACK | BUTTON_FORCEPOWER | BUTTON_FORCEGRIP
		| BUTTON_FORCE_LIGHTNING | BUTTON_FORCE_DRAIN ) ) || ucmd->generic_cmd == GENCMD_FORCE_THROW
		|| ucmd->generic_cmd == GENCMD_FORCE_PULL || ucmd->generic_cmd == GENCMD_FORCE_DISTRACT;
	vec3_t		eye, where;
	float		bearing = 0.0f, err = 999.0f, rate = 0.0f;
	qboolean	visible = qfalse;
	int			target;

	s->aimTarget = -1;
	s->aimErr = 999.0f;
	s->aimRate = 0.0f;
	s->aimVisible = 0;
	ac->lastButtons = ucmd->buttons;

	if ( ent->health <= 0 || client->sess.sessionTeam == TEAM_SPECTATOR || client->ps.pm_type != PM_NORMAL ) {
		ac->aimTarget = -1;
		ac->turnStart = 0;
		return;
	}

	VectorCopy( client->ps.origin, eye );
	eye[2] += client->ps.viewheight;
	target = G_AC_AimTarget( ent, eye, yaw, ucmd->serverTime, &bearing, where );
	if ( target >= 0 ) {
		err = fabsf( AngleDelta( yaw, bearing ) );
		if ( target == ac->aimTarget ) {
			rate = fabsf( AngleDelta( bearing, ac->aimLastBearing ) ) * 1000.0f / dt;
		}
		if ( err < AC_AIM_EXACT ) {
			trace_t tr;

			where[2] += 16.0f;		// the chest rather than the waist
			trap->Trace( &tr, eye, NULL, NULL, where, ent->s.number, CONTENTS_SOLID, qfalse, 0, 0 );
			visible = (qboolean)( tr.fraction >= 1.0f );
		}
		s->aimTarget = (short)target;
		s->aimErr = err;
		s->aimRate = rate;
		s->aimVisible = (byte)visible;
	}
	ac->aimTarget = target;
	ac->aimLastBearing = bearing;

	// the lock: the view on an enemy who crosses it fast
	if ( target >= 0 && rate >= AC_AIM_FAST ) {
		ac->aimFastMs += dt;
		if ( err < AC_AIM_EXACT && visible ) {
			ac->aimLockMs += dt;
		}
	}
	if ( ucmd->serverTime - ac->aimWindowStart >= AC_AIM_WINDOW ) {
		if ( ac->aimFastMs >= AC_AIM_MIN_FAST ) {
			const float ratio = (float)ac->aimLockMs / ac->aimFastMs;

			ac->aimWindowsBad = ratio >= AC_AIM_LOCK_RATIO ? ac->aimWindowsBad + 1 : 0;
			if ( ac->aimWindowsBad >= 2 ) {
				ac->aimWindowsBad = 0;
				G_AC_AimFlag( ent, va( "the view sat within %.1f degrees on an enemy crossing it fast for %i of %i ms",
					AC_AIM_EXACT, ac->aimLockMs, ac->aimFastMs ) );
			}
		}
		ac->aimWindowStart = ucmd->serverTime;
		ac->aimFastMs = ac->aimLockMs = 0;
	}

	// the snap: a fast turn that stops dead on an enemy
	// (no limit on the size of one step: a teleport or a respawn turns delta_angles, not the angles of the move, and
	// an aimbot may turn half round in one move)
	if ( fabsf( turn ) >= AC_AIM_TURN
		&& ( !ac->turnStart || ( turn > 0.0f ? 1 : -1 ) == ac->turnDir ) ) {
		if ( !ac->turnStart ) {
			ac->turnStart = ucmd->serverTime - dt;
			ac->turnAccum = 0.0f;
			ac->turnDir = turn > 0.0f ? 1 : -1;
		}
		ac->turnAccum += fabsf( dyaw );
	} else if ( ac->turnStart ) {
		// the turn is over (it ended with the move before this one)
		if ( ac->turnAccum >= AC_AIM_SNAP_TURN && s->time - dt - ac->turnStart <= AC_AIM_SNAP_MSEC
			&& fabsf( turn ) < AC_AIM_TURN * 0.25f && target >= 0 && err < AC_AIM_EXACT && visible ) {
			const int snaps = G_AC_AddSnap( ac->snapTimes, ARRAY_LEN( ac->snapTimes ), ucmd->serverTime );

			ac->lastSnapTime = ucmd->serverTime;
			if ( ac->lastActTime && ucmd->serverTime - ac->lastActTime <= AC_AIM_ACT_MSEC && ucmd->serverTime >= ac->lastActTime ) {
				G_AC_SnapAct( ent, ucmd->serverTime );		// fired with the turn or just before it ended
			}
			if ( snaps >= AC_AIM_SNAPS ) {
				memset( ac->snapTimes, 0, sizeof( ac->snapTimes ) );
				G_AC_AimFlag( ent, va( "%i snaps of %.0f degrees or more onto an enemy in %i s", snaps, AC_AIM_SNAP_TURN,
					AC_AIM_SNAP_SPAN / 1000 ) );
			}
		}
		ac->turnStart = 0;
	}

	// a shot or an aimed force power right after a snap
	if ( acted ) {
		ac->lastActTime = ucmd->serverTime;
		if ( ac->lastSnapTime && ucmd->serverTime - ac->lastSnapTime <= AC_AIM_ACT_MSEC && ucmd->serverTime >= ac->lastSnapTime ) {
			G_AC_SnapAct( ent, ucmd->serverTime );
		}
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

	if ( vs_antiWiggle.integer <= 0 && vs_antiAim.integer <= 0 ) {
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
		ac->aimTarget = -1;
		ac->turnStart = 0;
		ac->aimWindowStart = ucmd->serverTime;
		ac->aimFastMs = ac->aimLockMs = 0;
		ac->lastButtons = ucmd->buttons;
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

	if ( vs_antiAim.integer > 0 ) {
		G_AC_Aim( ent, ucmd, s, dt, yaw, dyaw );
	} else {
		s->aimTarget = -1;
		s->aimErr = 999.0f;
		s->aimRate = 0.0f;
		s->aimVisible = 0;
	}
	if ( vs_antiWiggle.integer <= 0 ) {
		return;
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
