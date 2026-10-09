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

// cg_botmode.c -- bot mode: this client plays by itself. All of it is in the cgame, no server knows of it and
// the engine is not changed.

#include "cgame/cg_local.h"

/*
=================
Bot mode

Not in JA+. cg_botMode 1 and the client walks the route of the map's bots and fights, on any server. The Bot
Mode page of the setup menus sets the cvars.

The route is the file the bots of a server walk, botroutes/<map>.wnt, as this client has it in its own pk3s
(written by SavePathData of game/ai_wpnav.c). The waypoints are a trail in the order of the file, and a
waypoint names more of them it leads to.

The engine builds the commands of a client from its keys and has no way in for a cgame, but for these:
- the keys themselves. "+forward" and "-forward" as console commands hold a key down and let it go, that is
  how I walk, jump, duck and attack.
- SetClientForceAngle sets the yaw of the view.
- For the pitch there is only the key +lookdown, which turns at cl_pitchspeed, times the pitch override a cgame
  hands over with SetUserCmdValue (for the vehicles). So +lookdown is held all the time and that factor is the
  rate, up or down, set anew every frame from how far off the pitch is.

With cg_botModeKeys a key of mine ends it: one of the move and attack keys that is down, after none of them was.
=================
*/

// of game/ai_main.h, which the cgame can not include
#define WPFLAG_JUMP				0x00000010	// jump while travelling to this point
#define WPFLAG_DUCK				0x00000020	// duck while travelling to this point
#define WPFLAG_ONEWAY_FWD		0x00004000	// only to be walked to along the trail forward
#define WPFLAG_ONEWAY_BACK		0x00008000	//	and backward

#define BOT_NEIGHBORS		16			// links of a waypoint that are kept, of the MAX_NEIGHBOR_SIZE of the file
#define BOT_WP_REACHED		40.0f		// this near, and
#define BOT_WP_REACHED_Z	64.0f		//	this much above or below: I am at the waypoint (twice that in a jump)
#define BOT_WP_FIND			1024.0f		// the route is picked up at a waypoint in view that is this near
#define BOT_WP_RETRY		500			// msec until I look for one again
#define BOT_WP_LINK			800.0f		// two waypoints in a row that are further apart are two trails
#define BOT_WP_TIMEOUT		8000		// msec for one waypoint, then another one
#define BOT_STUCK_TIME		500			// msec between two looks at whether I get on
#define BOT_STUCK_DIST		12.0f		//	and how far I have to have come in that time
#define BOT_STUCK_TRIES		5			// jumps and side steps before the waypoint is given up
#define BOT_ENEMY_RANGE		1536.0f
#define BOT_ENEMY_SCAN		200			// msec between two looks for an enemy
#define BOT_ENEMY_FORGET	1500		// msec an enemy who is out of view is still gone after
#define BOT_AIM_TIME		400			// msec until the aim is off by another bit

enum {
	BK_FORWARD	= 1,
	BK_BACK		= 2,
	BK_LEFT		= 4,
	BK_RIGHT	= 8,
	BK_JUMP		= 16,
	BK_CROUCH	= 32,
	BK_ATTACK	= 64,
	BK_LOOKDOWN	= 128
};

// the key commands of the engine, in the order of the BK_ bits
static const char *const botKeys[] = { "forward", "back", "moveleft", "moveright", "moveup", "movedown", "attack", "lookdown" };

// the keys of mine that take over again
static const char *const botUserKeys[] = { "+forward", "+back", "+moveleft", "+moveright", "+moveup", "+movedown", "+attack", "+altattack" };

typedef struct botSkill_s {
	float	turn;		// degrees a second
	float	aim;		// degrees the aim is off by at most
	int		react;		// msec from seeing an enemy to the first attack
} botSkill_t;

static const botSkill_t botSkills[5] = {
	{ 150.0f, 8.0f, 900 },
	{ 220.0f, 5.5f, 650 },
	{ 300.0f, 3.5f, 450 },
	{ 420.0f, 1.8f, 280 },
	{ 600.0f, 0.6f, 150 }
};

typedef struct botWaypoint_s {
	vec3_t	origin;
	int		flags;
	short	neighbors[BOT_NEIGHBORS];
	byte	numNeighbors;
	byte	inuse;
} botWaypoint_t;

static botWaypoint_t	botWaypoints[MAX_WPARRAY_SIZE];
static int				botNumWaypoints;

static struct {
	qboolean	active;
	qboolean	routeTried;		// the route of this map was looked for
	qboolean	idle;			// none of my keys was down since the bot began: one that is down now was pressed
	int			held;			// BK_ bits: the keys the engine was told are down
	float		yaw;			// the view the engine was given

	int			wp;				// the waypoint I walk to, -1: none
	int			wpDir;			// 1 or -1: along the trail or against it
	int			wpTime;			// when wp became it, or when I last looked for one
	int			wpAvoid;		// not this one again: it was not to be reached
	vec3_t		spot;			// where I was at spotTime
	int			spotTime, stuck;
	int			jumpUntil, sideUntil, side;

	int			enemy;			// -1: none
	int			enemyScan, enemySeen;
	int			attackFrom;		// the time from which I attack him
	float		aimError[2];	// yaw and pitch
	int			aimTime;
	int			strafeUntil, strafe;
} bot;

/*
===============================================================================

The keys

===============================================================================
*/

// The engine takes "+forward" without the number of a key as typed at the console: down until "-forward".
static void CG_BotKeys( int want ) {
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( botKeys ); i++ ) {
		if ( ( want ^ bot.held ) & ( 1 << i ) )
			trap->SendConsoleCommand( va( "%c%s\n", ( want & ( 1 << i ) ) ? '+' : '-', botKeys[i] ) );
	}
	bot.held = want;
}

// the keys that move me along dir, seen from where I look
static int CG_BotMoveKeys( const vec3_t dir ) {
	const float	a = AngleSubtract( vectoyaw( dir ), bot.yaw );	// to the left of the view is positive
	int			keys = 0;

	if ( fabsf( a ) < 67.5f )
		keys |= BK_FORWARD;
	else if ( fabsf( a ) > 112.5f )
		keys |= BK_BACK;
	if ( a > 22.5f && a < 157.5f )
		keys |= BK_LEFT;
	else if ( a < -22.5f && a > -157.5f )
		keys |= BK_RIGHT;
	return keys;
}

// one of my own move or attack keys is down, after none was: not while the console or a menu has the keys
static qboolean CG_BotTakenOver( void ) {
	int i;

	if ( !cg_botModeKeys.integer || trap->Key_GetCatcher() )
		return qfalse;
	for ( i = 0; i < (int)ARRAY_LEN( botUserKeys ); i++ ) {
		const int key = trap->Key_GetKey( botUserKeys[i] );

		if ( key >= 0 && trap->Key_IsDown( key ) )
			return bot.idle;
	}
	bot.idle = qtrue;
	return qfalse;
}

/*
===============================================================================

The route

===============================================================================
*/

// nothing of the level is between the two points
static qboolean CG_BotVisible( const vec3_t from, const vec3_t to ) {
	trace_t tr;

	CG_Trace( &tr, from, NULL, NULL, to, cg.predictedPlayerState.clientNum, MASK_SOLID );
	return tr.fraction == 1.0f;
}

// A line of the file: "index flags weight (x y z) { neighbor neighbor-jump ... } distance". A neighbor with a
// jump behind it needs a force jump to be reached: not kept. The first line of some files, "levelflags n",
// has no number at its start and falls out here.
static void CG_BotParseWaypoint( char *line ) {
	botWaypoint_t	*wp;
	char			*s = line, *end;
	int				index, flags, i;
	vec3_t			origin;

	index = strtol( s, &end, 10 );
	if ( end == s || index < 0 || index >= MAX_WPARRAY_SIZE )
		return;
	flags = strtol( end, &s, 10 );
	strtod( s, &s );		// weight
	if ( !( s = strchr( s, '(' ) ) )
		return;
	s++;
	for ( i = 0; i < 3; i++ )
		origin[i] = strtod( s, &s );
	if ( !( s = strchr( s, '{' ) ) )
		return;
	s++;

	wp = &botWaypoints[index];
	memset( wp, 0, sizeof( *wp ) );
	VectorCopy( origin, wp->origin );
	wp->flags = flags;
	wp->inuse = 1;
	for ( ;; ) {
		const int n = strtol( s, &end, 10 );

		if ( end == s )
			break;		// the closing brace
		s = end;
		if ( *s == '-' ) {
			strtol( s + 1, &s, 10 );
			continue;
		}
		if ( n >= 0 && n < MAX_WPARRAY_SIZE && wp->numNeighbors < BOT_NEIGHBORS )
			wp->neighbors[wp->numNeighbors++] = n;
	}
	if ( index >= botNumWaypoints )
		botNumWaypoints = index + 1;
}

static void CG_BotLoadRoute( void ) {
	const char		*map = cgs.rawmapname;
	fileHandle_t	f = 0;
	char			chunk[4096], line[1024];
	int				len, got, i, n = 0;

	bot.routeTried = qtrue;
	botNumWaypoints = 0;
	memset( botWaypoints, 0, sizeof( botWaypoints ) );

	if ( !Q_strncmp( map, "maps/", 5 ) )
		map += 5;
	len = trap->FS_Open( va( "botroutes/%s.wnt", map ), &f, FS_READ );
	if ( !f ) {
		trap->Print( "Bot mode: this map has no route (botroutes/%s.wnt), I stay where I am\n", map );
		return;
	}
	while ( len > 0 ) {
		got = len < (int)sizeof( chunk ) ? len : (int)sizeof( chunk );
		trap->FS_Read( chunk, got, f );
		len -= got;
		for ( i = 0; i < got; i++ ) {
			if ( chunk[i] != '\n' ) {
				if ( n < (int)sizeof( line ) - 1 )
					line[n++] = chunk[i];
				continue;
			}
			line[n] = '\0';
			n = 0;
			CG_BotParseWaypoint( line );
		}
	}
	if ( n ) {
		line[n] = '\0';
		CG_BotParseWaypoint( line );
	}
	trap->FS_Close( f );
	trap->Print( "Bot mode: route of %d waypoints\n", botNumWaypoints );
}

// the nearest waypoint in view
static int CG_BotNearestWaypoint( const vec3_t eye ) {
	float	bestDist = BOT_WP_FIND * BOT_WP_FIND;
	int		i, best = -1;

	for ( i = 0; i < botNumWaypoints; i++ ) {
		const botWaypoint_t	*wp = &botWaypoints[i];
		float				dist;

		if ( !wp->inuse || i == bot.wpAvoid )
			continue;
		dist = DistanceSquared( eye, wp->origin );
		if ( dist >= bestDist || !CG_BotVisible( eye, wp->origin ) )
			continue;
		bestDist = dist;
		best = i;
	}
	return best;
}

// the trail leads from this waypoint to the next one in that direction
static qboolean CG_BotLinked( int from, int dir ) {
	const int to = from + dir;

	if ( to < 0 || to >= botNumWaypoints || !botWaypoints[to].inuse )
		return qfalse;
	if ( botWaypoints[to].flags & ( dir > 0 ? WPFLAG_ONEWAY_BACK : WPFLAG_ONEWAY_FWD ) )
		return qfalse;
	return Distance( botWaypoints[from].origin, botWaypoints[to].origin ) < BOT_WP_LINK;
}

// I am at my waypoint: the next one
static void CG_BotNextWaypoint( void ) {
	const botWaypoint_t *wp = &botWaypoints[bot.wp];

	bot.wpTime = cg.time;
	bot.wpAvoid = -1;
	// now and then a side trail, or the route is one line that is walked up and down
	if ( wp->numNeighbors && !Q_irand( 0, 3 ) ) {
		const int n = wp->neighbors[Q_irand( 0, wp->numNeighbors - 1 )];

		if ( n < botNumWaypoints && botWaypoints[n].inuse ) {
			bot.wp = n;
			bot.wpDir = Q_irand( 0, 1 ) ? 1 : -1;
			return;
		}
	}
	if ( !CG_BotLinked( bot.wp, bot.wpDir ) ) {
		// the end of the trail: back
		bot.wpDir = -bot.wpDir;
		if ( !CG_BotLinked( bot.wp, bot.wpDir ) ) {
			bot.wp = -1;
			return;
		}
	}
	bot.wp += bot.wpDir;
}

// The keys that take me along the route, and where I look while I walk it. 0: there is no way for me now.
static int CG_BotRoute( const playerState_t *ps, const vec3_t eye, float *wishYaw ) {
	const botWaypoint_t	*wp;
	vec3_t				to;
	int					keys;

	if ( !botNumWaypoints )
		return 0;
	if ( bot.wp >= 0 && cg.time - bot.wpTime > BOT_WP_TIMEOUT ) {
		// not to be reached from where I am: another one, and the other way
		bot.wpAvoid = bot.wp;
		bot.wp = -1;
		bot.wpDir = -bot.wpDir;
		bot.wpTime = 0;
	}
	if ( bot.wp < 0 ) {
		if ( cg.time - bot.wpTime < BOT_WP_RETRY )
			return 0;
		bot.wpTime = cg.time;
		bot.wp = CG_BotNearestWaypoint( eye );
		if ( bot.wp < 0 ) {
			bot.wpAvoid = -1;
			return 0;
		}
	}

	wp = &botWaypoints[bot.wp];
	VectorSubtract( wp->origin, ps->origin, to );
	if ( fabsf( to[2] ) < ( ( wp->flags & WPFLAG_JUMP ) ? 2.0f : 1.0f ) * BOT_WP_REACHED_Z
		&& to[0] * to[0] + to[1] * to[1] < BOT_WP_REACHED * BOT_WP_REACHED ) {
		CG_BotNextWaypoint();
		if ( bot.wp < 0 )
			return 0;
		wp = &botWaypoints[bot.wp];
		VectorSubtract( wp->origin, ps->origin, to );
	}
	to[2] = 0.0f;
	*wishYaw = vectoyaw( to );
	keys = CG_BotMoveKeys( to );
	if ( wp->flags & WPFLAG_JUMP )
		keys |= BK_JUMP;
	if ( wp->flags & WPFLAG_DUCK )
		keys |= BK_CROUCH;
	return keys;
}

// I hold move keys and do not get on: a jump and a step to the side, and after some of those another waypoint
static int CG_BotUnstick( const playerState_t *ps, int keys ) {
	if ( !( keys & ( BK_FORWARD | BK_BACK | BK_LEFT | BK_RIGHT ) ) ) {
		bot.spotTime = cg.time;
		bot.stuck = 0;
		VectorCopy( ps->origin, bot.spot );
		return keys;
	}
	if ( cg.time - bot.spotTime >= BOT_STUCK_TIME ) {
		if ( Distance( ps->origin, bot.spot ) >= BOT_STUCK_DIST ) {
			bot.stuck = 0;
		} else {
			bot.jumpUntil = cg.time + 250;
			bot.sideUntil = cg.time + 400;
			bot.side = Q_irand( 0, 1 ) ? BK_LEFT : BK_RIGHT;
			if ( ++bot.stuck >= BOT_STUCK_TRIES ) {
				bot.stuck = 0;
				bot.wpAvoid = bot.wp;
				bot.wp = -1;
				bot.wpDir = -bot.wpDir;
				bot.wpTime = 0;
			}
		}
		bot.spotTime = cg.time;
		VectorCopy( ps->origin, bot.spot );
	}
	if ( cg.time < bot.jumpUntil )
		keys |= BK_JUMP;
	if ( cg.time < bot.sideUntil )
		keys = ( keys & ~( BK_LEFT | BK_RIGHT ) ) | bot.side;
	return keys;
}

/*
===============================================================================

The fight

===============================================================================
*/

// cg_botModeFight: 0 nobody, 1 only the one I am in a duel with, 2 every enemy
static qboolean CG_BotIsEnemy( const playerState_t *ps, int num ) {
	const centity_t *cent = &cg_entities[num];

	if ( num == ps->clientNum || !cent->currentValid || cent->currentState.eType != ET_PLAYER
		|| ( cent->currentState.eFlags & EF_DEAD ) || !cgs.clientinfo[num].infoValid )
		return qfalse;
	if ( cg_botModeFight.integer <= 0 )
		return qfalse;
	if ( ps->duelInProgress )
		return num == ps->duelIndex;
	if ( cg_botModeFight.integer < 2 )
		return qfalse;
	// bolt1: he is in a duel, which I am not
	if ( cent->currentState.bolt1 )
		return qfalse;
	return cgs.gametype < GT_TEAM || cgs.clientinfo[num].team != ps->persistant[PERS_TEAM];
}

// the nearest enemy in view becomes bot.enemy, the one I had stays it for a while after he is out of view
static void CG_BotFindEnemy( const playerState_t *ps, const vec3_t eye, const botSkill_t *skill ) {
	float	bestDist = BOT_ENEMY_RANGE * BOT_ENEMY_RANGE;
	int		i, best = -1;

	if ( cg.time - bot.enemyScan < BOT_ENEMY_SCAN )
		return;
	bot.enemyScan = cg.time;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		float dist;

		if ( !CG_BotIsEnemy( ps, i ) )
			continue;
		dist = DistanceSquared( ps->origin, cg_entities[i].lerpOrigin );
		if ( dist >= bestDist || !CG_BotVisible( eye, cg_entities[i].lerpOrigin ) )
			continue;
		bestDist = dist;
		best = i;
	}
	if ( best >= 0 ) {
		if ( best != bot.enemy )
			bot.attackFrom = cg.time + skill->react;
		bot.enemy = best;
		bot.enemySeen = cg.time;
	} else if ( bot.enemy >= 0 && ( cg.time - bot.enemySeen > BOT_ENEMY_FORGET || !CG_BotIsEnemy( ps, bot.enemy ) ) ) {
		bot.enemy = -1;
	}
}

// qtrue: there is an enemy, and the keys and the view are what the fight wants
static qboolean CG_BotFight( const playerState_t *ps, const vec3_t eye, const botSkill_t *skill, int *keys, float *wishYaw, float *wishPitch ) {
	const qboolean	melee = ps->weapon == WP_SABER || ps->weapon == WP_MELEE || ps->weapon == WP_STUN_BATON;
	vec3_t			to, angles;
	float			dist;
	int				move = 0;

	CG_BotFindEnemy( ps, eye, skill );
	if ( bot.enemy < 0 )
		return qfalse;

	VectorSubtract( cg_entities[bot.enemy].lerpOrigin, eye, to );
	dist = VectorLength( to );
	vectoangles( to, angles );
	// a hand that is not steady
	if ( cg.time - bot.aimTime > BOT_AIM_TIME ) {
		bot.aimTime = cg.time;
		bot.aimError[0] = Q_flrand( -skill->aim, skill->aim );
		bot.aimError[1] = Q_flrand( -skill->aim, skill->aim );
	}
	*wishYaw = angles[YAW] + bot.aimError[0];
	*wishPitch = angles[PITCH] + bot.aimError[1];

	// to where my weapon reaches, and not into him
	to[2] = 0.0f;
	if ( dist > ( melee ? 72.0f : 512.0f ) ) {
		move = CG_BotMoveKeys( to );
	} else if ( dist < ( melee ? 40.0f : 160.0f ) ) {
		VectorNegate( to, to );
		move = CG_BotMoveKeys( to );
	}
	// and never still: to one side, to the other, or to none for a moment
	if ( cg.time > bot.strafeUntil ) {
		bot.strafeUntil = cg.time + Q_irand( 500, 1500 );
		bot.strafe = Q_irand( -1, 1 );
	}
	if ( dist < 600.0f && bot.strafe && !( move & ( BK_LEFT | BK_RIGHT ) ) )
		move |= bot.strafe < 0 ? BK_LEFT : BK_RIGHT;

	// he is in view, I have had the time to see him, and he is where I look
	if ( cg.time - bot.enemySeen <= 2 * BOT_ENEMY_SCAN && cg.time >= bot.attackFrom
		&& fabsf( AngleSubtract( *wishYaw, bot.yaw ) ) < ( melee ? 20.0f : 6.0f )
		&& ( melee ? dist < 120.0f : fabsf( AngleSubtract( *wishPitch, ps->viewangles[PITCH] ) ) < 6.0f ) )
		move |= BK_ATTACK;

	*keys |= move;
	return qtrue;
}

/*
===============================================================================

Once a frame

===============================================================================
*/

// the bot may play now: it is switched on and I am a player in the game, on foot
static qboolean CG_BotModeOn( const playerState_t *ps ) {
	if ( !cg_botMode.integer || !cg.snap || cg.demoPlayback || ( cg.snap->ps.pm_flags & PMF_FOLLOW ) )
		return qfalse;
	if ( ps->persistant[PERS_TEAM] == TEAM_SPECTATOR || ps->pm_type == PM_SPECTATOR || ps->pm_type == PM_INTERMISSION )
		return qfalse;
	return !ps->m_iVehicleNum && ps->weapon != WP_EMPLACED_GUN;
}

static void CG_BotStart( const playerState_t *ps ) {
	if ( !bot.routeTried )
		CG_BotLoadRoute();
	bot.active = qtrue;
	bot.idle = qfalse;
	bot.yaw = ps->viewangles[YAW];
	bot.wp = bot.wpAvoid = bot.enemy = -1;
	bot.wpDir = Q_irand( 0, 1 ) ? 1 : -1;
	bot.wpTime = bot.stuck = 0;
	bot.spotTime = cg.time;
	VectorCopy( ps->origin, bot.spot );
}

static void CG_BotStop( void ) {
	CG_BotKeys( 0 );
	bot.active = qfalse;
}

/*
=================
CG_BotModeFrame

From CG_DrawActiveFrame, behind the SetUserCmdValue of the frame: while the bot plays it says that again, with
the pitch rate in it. sensitivity is what the frame worked out for the mouse.
=================
*/
void CG_BotModeFrame( float sensitivity ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	const botSkill_t	*skill = &botSkills[Com_Clampi( 1, 5, cg_botModeSkill.integer ) - 1];
	const float			dt = Com_Clamp( 1.0f, 100.0f, cg.frametime ) * 0.001f;
	vec3_t				eye, angles;
	float				wishYaw, wishPitch = 0.0f, step, pitchSpeed, rate = 0.0f;
	char				value[32];
	int					keys = BK_LOOKDOWN;

	if ( !CG_BotModeOn( ps ) ) {
		if ( bot.active )
			CG_BotStop();
		return;
	}
	if ( !bot.active )
		CG_BotStart( ps );
	if ( CG_BotTakenOver() ) {
		trap->Cvar_Set( "cg_botMode", "0" );
		CG_BotStop();
		return;
	}

	wishYaw = bot.yaw;
	VectorCopy( ps->origin, eye );
	eye[2] += ps->viewheight;
	if ( ps->pm_type == PM_DEAD || ps->stats[STAT_HEALTH] <= 0 ) {
		// the attack key brings me back, pressed anew now and then
		if ( ( cg.time / 400 ) & 1 )
			keys |= BK_ATTACK;
		bot.wp = bot.enemy = -1;
		bot.wpTime = 0;
	} else if ( CG_BotFight( ps, eye, skill, &keys, &wishYaw, &wishPitch ) ) {
		// the route is picked up again where the fight ends
		bot.wp = -1;
		bot.wpTime = 0;
	} else if ( cg_botModeRoam.integer ) {
		keys |= CG_BotRoute( ps, eye, &wishYaw );
	}
	CG_BotKeys( CG_BotUnstick( ps, keys ) );

	// the view turns to where it is wanted at the speed of the skill
	step = skill->turn * dt;
	bot.yaw = AngleNormalize180( bot.yaw + Com_Clamp( -step, step, AngleSubtract( wishYaw, bot.yaw ) ) );
	VectorSet( angles, 0.0f, bot.yaw, 0.0f );
	trap->SetClientForceAngle( cg.time + 1000, angles );

	// The pitch moves by rate * cl_pitchspeed * the time of the engine's next frame, which is about the time of
	// this one. Half of what is missing a frame, so that it does not swing: I see the answer a frame late.
	trap->Cvar_VariableStringBuffer( "cl_pitchspeed", value, sizeof( value ) );
	pitchSpeed = atof( value );
	if ( pitchSpeed > 0.0f )
		rate = Com_Clamp( -step, step, 0.5f * AngleSubtract( wishPitch, ps->viewangles[PITCH] ) ) / ( dt * pitchSpeed );
	// never 0: that is the engine's own rate, with +lookdown held a turn to the floor
	if ( fabsf( rate ) < 0.0001f )
		rate = 0.0001f;
	trap->SetUserCmdValue( cg.weaponSelect, sensitivity, rate, 0.0f, 1.0f, cg.forceSelect, cg.itemSelect, qfalse );
}

void CG_BotModeDraw( void ) {
	if ( !bot.active || cg.scoreBoardShowing )
		return;
	CG_DrawProportionalString( 320, 64, ( botNumWaypoints || !cg_botModeRoam.integer ) ? "BOT MODE" : "BOT MODE (no route for this map)",
		UI_CENTER | UI_SMALLFONT, colorTable[CT_ICON_BLUE] );
}

// the cgame goes: the engine would go on holding my keys
void CG_BotModeShutdown( void ) {
	if ( bot.active )
		CG_BotStop();
}
