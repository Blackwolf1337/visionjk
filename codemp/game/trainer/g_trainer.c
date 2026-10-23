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

/*
Training bots ("vs_trainer [1-5|off]"): a player calls a bot that fights only that player, the way flip kick
players fight. It pulls itself in with the JA+ grappling hook, grabs with melee, flip kicks, or swings from
above in the red style. It watches the player first: while the player attacks it backs off and blocks, and a
hook, flip kick or grab of the player it answers. Level 1 reacts late and misses much, level 5 is quick and
sees nearly everything.

The bot is a normal bot (bot_enable 1) of the profile "Jedi Trainer" with a single saber instead of its staff
and its own force powers. Its usercmd is made here instead of by the stock bot AI (ai_main.c asks
G_TrainerCmd), so it goes through the pmove of every player and can do nothing a player can not.

vs_allowTrainer is the number of training bots there may be at once, 0 turns the command off. Free for all
only: the hook works only there, and in a team game the bot could end up on the team of its player.

How it hooks and kicks follows a movement log of a flip kick player (vs_movelog, g_movelog.c, 15 kicks in
83 seconds on 2026-10-22). The hook is a short pull, fired in the air 130 to 170 units off the player and let go
with use after about 60 ms at about 100 units. Down on the ground at 35 to 105 units comes the jump, held about
120 ms, let go about 55 ms and pressed again: that press is the kick, 40 to 70 units off the player and 15 to 50
units above him. During the flip back the hook pulls it in again for the next kick. The side kick is a jump on
the ground while strafing into a player who stands 30 to 40 units to the side, at about 90 degrees.

A recorded fight of that player against it (2.6 minutes, 1044 damage dealt to the bot, 132 taken) showed the rest.
The bot ran straight at him between its attacks and was kicked running in, backing off from his swings it was
followed and hit again and again, and its swing against a coming kick did not keep the kick off. So between attacks
it keeps tp.RING_NEAR to tp.RING_FAR off and circles, against a swing it steps to a side and swings into
the return of it, out of a coming kick it steps aside, and a player on the ground gets its swing at once.

A second recorded fight (after those changes: 198 damage a minute to the bot instead of 394, 54 from it) was a race
of kicks. The player kicked it on its own short pull and running in, it kicked him when he came at it. So it may
wait in the ring for him to come (an approach it learns, below), after stepping out of his hook or kick it kicks
back at once, and the kick jump starts the earlier the faster it runs.

A third recorded fight (5 minutes, 94 attacks of the bot) showed how the player counters: when the bot comes at him
through the air, by a jump or on the hook, he jumps to meet it and kicks it there, as it can not turn in the air
(35 times; he took off 120 to 135 units off when it was 45 to 55 above him near the top of its jump, 30 to 100 when
it came in low, the kick 100 to 200 ms later). Its jumps at him without the hook lost 24 to 5, its hook attacks
won 13 to 11. So it counters the same way, and it hooks more: the short pull from further off and lower above him,
the long pull from nearer.

Prediction and learning. Where the player will be is his speed carried on, with gravity in the air, where the rope
pulls him straight: nobody turns in the air. The bot aims ahead of him (by level), jumps for its kick when he will be
in reach at the press 150 ms later, meets him in the air where he will arrive, and steps out of his kick to the side
he is not turning to. Which of its approaches (hook kick, run kick, side kick, red swing, waiting for him) works
against this player it learns during the fight: an approach scores the damage it dealt minus the damage it took in
the tp.TRY_MSEC after it began, the scores are kept as a moving average, and the next approach is the best one
with a bonus for the ones tried less and some chance by level. "vs_trainer info" shows the scores. They are gone
with the map.

A fourth recorded fight (7.5 minutes, the bot dealt 131 a minute and took 160, it won 9 of 20 kills) showed where
the player puts his hook: 279 of 295 times into the floor, median 76 units short of the bot at its feet and a little
to a side, looking 42 degrees down, the bot hooked into his body and was pulled into his saber. After 79 of 117
missed kicks he hooked the floor at once (median 100 ms later), 186 units off, behind him or to a side as often as
past the bot, held it 200 ms and was 115 units off again for the next kick. The bot does both now.

A fifth recorded fight (6.5 minutes, 255 damage a minute to the bot, 81 from it) showed two holes. His red swings
from 48 units hit it while its saber stood ready and faced him: the server takes saber hits SP style
(d_saberSPStyleDamage), only a blade that is in the way blocks, so the bot now keeps out of the reach of the swing,
diagonally back and to a side, and jumps back when his swing starts that close. And he kicked it out of the air
while it still held its kick jump (416 of 610 damage), so it lets go early when he will be in reach.

Owner and level are a userinfo key of the bot ("vs_trainer" "<owner> <level>"), so a bot that stays over a map
change is a trainer again. It leaves when its player leaves or sends it away.

Not from JA+.
*/

#include "game/g_local.h"
#include "game/ai_main.h"
#include "g_policy.h"
#include <stddef.h>

qboolean PM_SaberInStart( int move );					// bg_panimate.c
qboolean PM_SaberInReturn( int move );
qboolean PM_InKnockDown( playerState_t *ps );
qboolean BG_InKnockDownOnGround( playerState_t *ps );

#define TRAINER_LEVELS			5
#define TRAINER_CLONE			( TRAINER_LEVELS + 1 )	// the "level" of the clone (g_clone.c)
#define TRAINER_POLICY			( TRAINER_LEVELS + 2 )	// the learned policy, "ai" (g_policy.c)
#define TRAINER_POLICY_OLD		( TRAINER_LEVELS + 3 )	// an older one it fights in a match, "ai2"
#define TRAINER_LAST			TRAINER_POLICY_OLD
#define TRAINER_HOP				0x100	// a level 1-5 with "h" ("5h"): it hops between its attacks, as players do
#define TRAINER_SEARCH_FAR		800.0f	// the clone and the policy search the player farther than this
#define TRAINER_SEARCH_NEAR		600.0f	// and fight him again nearer than this, when they see him
#define TRAINER_SEARCH_UNSEEN	1500	// or when they have not seen him for this long (msec)
#define TRAINER_ROPE_MSEC		1500	// the policy lets go of a hook it held this long while he is not near
#define TRAINER_ROPE_NEAR		400.0f
#define TRAINER_CLONE_TURN		1500.0f	// degrees a second the clone turns, like a player with a mouse
#define TRAINER_BOT				"Jedi Trainer"	// botfiles/bots.txt of the game
#define TRAINER_NAME			"Trainer"
#define TRAINER_USERINFO		"\\saber1\\single_1\\saber2\\none"	// the staff of the profile has no red style
// levitation 3 for the flip kick, saber offense and defense 3 for the red style (rank 7, light side)
#define TRAINER_FORCEPOWERS		"7-1-030000000000000330"
#define TRAINER_PARAMS_SIZE		16384	// largest params file

// the values a file can change (vs_trainerParams), see trainer_params.h
typedef struct {
#define TRAINER_PARAM( name, def )	float name;
#include "trainer_params.h"
#undef TRAINER_PARAM
} trainerParams_t;

static const trainerParams_t trainerDefaults = {
#define TRAINER_PARAM( name, def )	def,
#include "trainer_params.h"
#undef TRAINER_PARAM
};

static const struct {
	const char	*name;
	size_t		offset;
} trainerParamNames[] = {
#define TRAINER_PARAM( name, def )	{ #name, offsetof( trainerParams_t, name ) },
#include "trainer_params.h"
#undef TRAINER_PARAM
};

static trainerParams_t	tp;					// what the bot uses
static int				tpModified = -1;	// vs_trainerParams.modificationCount they were read for


typedef struct {
	int		reactMs;		// from the first sign of an attack until the trainer reacts, half of it for a counter
	float	turnSpeed;		// degrees a second the view turns
	int		readChance;		// percent: an attack of the player is seen at all
	int		counterChance;	// percent: a hook, flip kick or grab of the player is answered
	int		pauseMs;		// between two attacks of the trainer
	float	leadSec;		// aims where the player will be this much later
	float	chance;			// random part (in health) when it picks an approach, less is more of what works
} trainerLevel_t;

static const trainerLevel_t trainerLevelDefaults[TRAINER_LEVELS] = {
	{ 450, 220.0f, 45, 20, 2600, 0.00f, 24.0f },
	{ 330, 340.0f, 60, 40, 1900, 0.03f, 18.0f },
	{ 230, 480.0f, 75, 60, 1300, 0.06f, 12.0f },
	{ 150, 650.0f, 88, 80,  850, 0.09f,  7.0f },
	{  80, 900.0f, 97, 95,  500, 0.12f,  3.0f },
};

// what it can begin an attack with, learned against the player (Trainer_Choose)
typedef enum {
	TA_HOOKKICK,	// up, the short pull, the kick
	TA_RUNKICK,		// the kick jump from a run, no hook
	TA_SIDEKICK,
	TA_SWING,		// red style from above
	TA_WAIT,		// in the ring for him to come, then the counter
	TA_NUM
} trainerApproach_t;

static trainerLevel_t trainerLevels[TRAINER_LEVELS];

static const char *trainerApproachNames[TA_NUM] = { "hook kick", "run kick", "side kick", "red swing", "wait" };

typedef enum {
	TS_CHASE,		// runs to the player, nothing started
	TS_GUARD,		// the player attacks: back off and block
	TS_COUNTER,		// answers a hook, flip kick or grab of the player
	TS_HOOK,		// on the rope to the player
	TS_KICK,		// jumps and flip kicks off the player, with short pulls of the hook (the kick steps)
	TS_SIDEKICK,	// strafes into the player at its side and jumps
	TS_GRAB,		// melee grab with a punch, then a flip kick
	TS_SWING		// red style, from above
} trainerState_t;

// the steps of TS_KICK
typedef enum {
	KS_RUN,			// to the player, or down to the ground
	KS_HOP,			// up into the air to hook from there
	KS_YANK,		// the short pull, let go with use
	KS_HOLD,		// the kick jump, held
	KS_GAP,			// let go
	KS_PRESS,		// pressed again: the kick
	KS_FLIP,		// kicked, flips back
	KS_ESCAPE		// missed: a hook into the floor away from him, to come again from there
} trainerKickStep_t;

typedef enum {
	TT_NONE,
	TT_HOOK,		// the hook of the player sits in the trainer
	TT_KICK,		// the player jumps at it
	TT_GRAB			// the hand of the player is out
} trainerThreat_t;

// a sign of the player, seen or not, and since when
typedef struct {
	int			sign;
	int			since;
	qboolean	seen;
} trainerSign_t;

typedef struct {
	qboolean		checked;	// the userinfo of the slot was looked at
	qboolean		active;
	qboolean		drop;		// leaves at the next frame
	int				owner;
	int				level;		// 1 to TRAINER_LEVELS
	trainerState_t	state;
	int				stateTime;	// level.time the state began
	int				step;		// within the state
	int				stepTime;
	int				hookedTime;	// level.time the hook of KS_YANK got hold (KS_ESCAPE: went out), 0 before
	float			hookSide;	// how far to a side of the line to him its hook goes into the floor
	vec3_t			escapePoint;	// KS_ESCAPE: where that hook goes
	int				chain;		// kicks one after another in this TS_KICK
	int				side;		// TS_SIDEKICK: 1 the player is to the left, -1 to the right
	int				nextAttack;
	int				styleTime;	// no saber key before this
	int				cmdTime;	// level.time cmd was made for
	usercmd_t		cmd;
	trainerSign_t	attack;		// the player attacks
	trainerSign_t	threat;		// a trainerThreat_t of the player
	trainerSign_t	open;		// the player is down or his swing returns
	qboolean		noHop;		// TA_RUNKICK: the kick without the hook
	int				approach;	// the trainerApproach_t that is scored, -1 none
	int				approachTime;
	int				dealt, taken;	// health since approachTime
	float			value[TA_NUM];	// moving average of dealt - taken
	int				tries[TA_NUM];
	qboolean		learned;	// value has its priors
	qboolean		match;		// fights another bot in a vs_trainermatch, needs no vs_allowTrainer
	qboolean		clone;		// plays like the player of the clone model, not by the states here (g_clone.c)
	cloneState_t	cloneState;	// its decisions of the frame before and its aim
	int				policy;		// the slot of its learned policy (g_policy.c), -1: none
	qboolean		hop;		// TRAINER_HOP
	int				hopUntil;	// level.time it holds its jump until
	qboolean		search;		// it runs to the player along the waypoints (Trainer_SearchThink)
	int				seenTime;	// level.time it last saw the player
	int				ropeTime;	// level.time its hook got hold, 0 not holding
	int				pathTime;	// level.time the way to the player is looked for again
	int				pathWP;		// the next waypoint on it, -1 none (straight to him)
	vec3_t			stuckAt;	// where it was at stuckTime
	int				stuckTime;
	int				avoidWP;	// a waypoint it got stuck going to, left out of the way until avoidTime
	int				avoidTime;
	int				dodge;		// 1 or -1: it steps to that side to get loose
	int				cloneUp;	// its upmove of the frame before
	int				circle;		// 1 circles to the right, -1 to the left
	int				circleTime;	// level.time it may turn round
} trainer_t;

static trainer_t trainers[MAX_CLIENTS];

// vs_trainermatch: two bots fight each other for some rounds, the result goes into a file (tools/trainer)
#define TRAINER_MATCH_ROUND		60000	// msec a round lasts at most
#define TRAINER_MATCH_NEAR		150.0f	// a round starts with the bots this far apart at least
#define TRAINER_MATCH_FAR		600.0f	// and at most
#define TRAINER_MATCH_TRIES		64		// frames a round tries to place them before it starts where they are

static struct {
	qboolean	on;
	qboolean	quit;			// quit the server when it is over
	int			bot[2];
	int			level[2];
	int			rounds, round;
	int			start, roundStart;
	int			dealt[2], kills[2];
	int			kicks[2];		// landed flip kicks (MOD_MELEE, g_active.c)
	qboolean	alive[2];
	int			place;			// frames left to place the bots for the round, 0 placed
	char		file[MAX_QPATH];
} match;

// one value of the params file: a name of trainer_params.h, or level<1-5>.<field> of the level table
static qboolean Trainer_SetParam( const char *key, float value ) {
	static const char *fields[] = { "reactMs", "turnSpeed", "readChance", "counterChance", "pauseMs", "leadSec", "chance" };
	size_t i;

	for ( i = 0; i < ARRAY_LEN( trainerParamNames ); i++ ) {
		if ( !Q_stricmp( key, trainerParamNames[i].name ) ) {
			*(float *)( (byte *)&tp + trainerParamNames[i].offset ) = value;
			return qtrue;
		}
	}
	if ( !Q_stricmpn( key, "level", 5 ) && key[5] >= '1' && key[5] <= '0' + TRAINER_LEVELS && key[6] == '.' ) {
		trainerLevel_t *lvl = &trainerLevels[key[5] - '1'];

		for ( i = 0; i < ARRAY_LEN( fields ); i++ ) {
			if ( Q_stricmp( key + 7, fields[i] ) )
				continue;
			switch ( i ) {
			case 0:	lvl->reactMs = (int)value;			break;
			case 1:	lvl->turnSpeed = value;				break;
			case 2:	lvl->readChance = (int)value;		break;
			case 3:	lvl->counterChance = (int)value;	break;
			case 4:	lvl->pauseMs = (int)value;			break;
			case 5:	lvl->leadSec = value;				break;
			default:	lvl->chance = value;			break;
			}
			return qtrue;
		}
	}
	return qfalse;
}

/*
=================
Trainer_LoadParams

the defaults, then what the file of vs_trainerParams changes, "<name> <value>" on each line (// comments). Read
again when the cvar changes, so a simulation can try other values without a new build.
=================
*/
static void Trainer_LoadParams( void ) {
	static char		buf[TRAINER_PARAMS_SIZE];
	char			key[MAX_TOKEN_CHARS];
	const char		*p, *tok;
	fileHandle_t	f;
	int				len, count = 0;

	if ( tpModified == vs_trainerParams.modificationCount )
		return;
	tpModified = vs_trainerParams.modificationCount;
	tp = trainerDefaults;
	memcpy( trainerLevels, trainerLevelDefaults, sizeof( trainerLevels ) );
	if ( !vs_trainerParams.string[0] )
		return;

	len = trap->FS_Open( vs_trainerParams.string, &f, FS_READ );
	if ( !f ) {
		trap->Print( "vs_trainerParams: no file %s, the defaults are used\n", vs_trainerParams.string );
		return;
	}
	if ( len <= 0 || len >= (int)sizeof( buf ) ) {
		trap->FS_Close( f );
		trap->Print( "vs_trainerParams: %s is empty or larger than %d bytes\n", vs_trainerParams.string, (int)sizeof( buf ) - 1 );
		return;
	}
	trap->FS_Read( buf, len, f );
	buf[len] = '\0';
	trap->FS_Close( f );

	p = buf;
	while ( 1 ) {
		tok = COM_ParseExt( &p, qtrue );
		if ( !tok[0] )
			break;
		Q_strncpyz( key, tok, sizeof( key ) );
		tok = COM_ParseExt( &p, qfalse );
		if ( !tok[0] ) {
			trap->Print( "vs_trainerParams: %s has no value\n", key );
			break;
		}
		if ( Trainer_SetParam( key, atof( tok ) ) )
			count++;
		else
			trap->Print( "vs_trainerParams: unknown value %s\n", key );
	}
	trap->Print( "vs_trainerParams: %d values from %s\n", count, vs_trainerParams.string );
}

// level 1 to 5, TRAINER_CLONE or a learned policy
static void Trainer_SetLevel( trainer_t *t, int lvl ) {
	t->hop = (qboolean)( (lvl & TRAINER_HOP) && (lvl & ~TRAINER_HOP) <= TRAINER_LEVELS );
	lvl &= ~TRAINER_HOP;
	t->level = Com_Clampi( 1, TRAINER_LAST, lvl );
	t->clone = (qboolean)( t->level == TRAINER_CLONE );
	t->policy = ( t->level == TRAINER_POLICY ) ? 0 : ( t->level == TRAINER_POLICY_OLD ) ? 1 : -1;
	Clone_Reset( &t->cloneState );
	Policy_Reset( t - trainers, t->policy );
}

// "clone", "ai", "ai2" or a number, with "h" a level that hops (TRAINER_HOP); 0 if none of them
static int Trainer_ParseLevel( const char *arg ) {
	const size_t len = strlen( arg );

	if ( len >= 2 && (arg[len - 1] == 'h' || arg[len - 1] == 'H') && atoi( arg ) >= 1 && atoi( arg ) <= TRAINER_LEVELS )
		return atoi( arg ) | TRAINER_HOP;
	if ( !Q_stricmp( arg, "clone" ) )
		return TRAINER_CLONE;
	if ( !Q_stricmp( arg, "ai" ) )
		return TRAINER_POLICY;
	if ( !Q_stricmp( arg, "ai2" ) )
		return TRAINER_POLICY_OLD;
	return atoi( arg );
}

// the model a level needs is there
static qboolean Trainer_LevelReady( int lvl ) {
	lvl &= ~TRAINER_HOP;
	if ( lvl == TRAINER_CLONE )
		return Clone_Ready();
	if ( lvl == TRAINER_POLICY || lvl == TRAINER_POLICY_OLD )
		return Policy_Ready( lvl - TRAINER_POLICY );
	return (qboolean)( lvl >= 1 && lvl <= TRAINER_LEVELS );
}

/*
=================
Trainer_Get

the trainer in a slot, NULL if it holds none. After a map change the table is empty and the userinfo key says
it again.
=================
*/
static trainer_t *Trainer_Get( int clientNum ) {
	trainer_t *t;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS )
		return NULL;

	t = &trainers[clientNum];
	if ( !t->checked && g_entities[clientNum].client && (g_entities[clientNum].r.svFlags & SVF_BOT) ) {
		char userinfo[MAX_INFO_STRING];
		int owner, lvl;

		t->checked = qtrue;
		trap->GetUserinfo( clientNum, userinfo, sizeof( userinfo ) );
		if ( sscanf( Info_ValueForKey( userinfo, "vs_trainer" ), "%d %d", &owner, &lvl ) == 2
			&& owner >= 0 && owner < MAX_CLIENTS && owner != clientNum ) {
			t->active = qtrue;
			t->approach = -1;
			t->owner = owner;
			Trainer_SetLevel( t, lvl );
		}
	}
	return t->active ? t : NULL;
}

static void Trainer_SetKey( int clientNum, int owner, int lvl ) {
	char userinfo[MAX_INFO_STRING];

	trap->GetUserinfo( clientNum, userinfo, sizeof( userinfo ) );
	Info_SetValueForKey( userinfo, "vs_trainer", va( "%d %d", owner, lvl ) );
	trap->SetUserinfo( clientNum, userinfo );
}

static gentity_t *Trainer_Owner( const trainer_t *t ) {
	gentity_t *ent = &g_entities[t->owner];

	if ( !ent->inuse || !ent->client || ent->client->pers.connected == CON_DISCONNECTED )
		return NULL;
	return ent;
}

// the owner, when there is something to fight
static gentity_t *Trainer_Enemy( const trainer_t *t ) {
	gentity_t *ent = Trainer_Owner( t );

	if ( !ent || ent->client->pers.connected != CON_CONNECTED || ent->client->sess.sessionTeam == TEAM_SPECTATOR
		|| ent->client->tempSpectate >= level.time || ent->health <= 0 || (ent->client->ps.eFlags & EF_DEAD) )
		return NULL;
	return ent;
}

static void Trainer_SetState( trainer_t *t, trainerState_t state ) {
	if ( t->state != state ) {
		t->state = state;
		t->stateTime = level.time;
		t->step = 0;
		t->stepTime = level.time;
		t->chain = 0;
		t->hookSide = Q_flrand( -tp.HOOK_SIDE, tp.HOOK_SIDE );
	}
}

static void Trainer_Step( trainer_t *t, int step ) {
	t->step = step;
	t->stepTime = level.time;
	t->hookedTime = 0;
}

// an attack is over: back to running after the player, the next one after a pause of its level
static void Trainer_Done( trainer_t *t ) {
	const int pause = trainerLevels[t->level - 1].pauseMs;

	Trainer_SetState( t, TS_CHASE );
	t->nextAttack = level.time + Q_irand( pause * 7 / 10, pause * 13 / 10 );
}

// the player swings, kicks, shoots or is about to (the attack button is down near enough to strike)
static qboolean Trainer_Attacking( const gentity_t *enemy, float dist ) {
	const playerState_t *ps = &enemy->client->ps;
	const int buttons = enemy->client->pers.cmd.buttons;

	if ( BG_KickingAnim( ps->legsAnim ) || BG_KickingAnim( ps->torsoAnim ) )
		return qtrue;
	if ( ps->weapon == WP_SABER && !ps->saberHolstered
		&& (BG_SaberInAttack( ps->saberMove ) || PM_SaberInStart( ps->saberMove ) || BG_SaberInSpecial( ps->saberMove )) )
		return qtrue;
	// both buttons with melee is a grab, that is a threat of its own
	if ( ps->weapon == WP_MELEE && (buttons & BUTTON_ATTACK) && (buttons & BUTTON_ALT_ATTACK) )
		return qfalse;
	return (qboolean)( dist < tp.STRIKE_RANGE && (buttons & (BUTTON_ATTACK | BUTTON_ALT_ATTACK)) != 0 );
}

// the player can not defend himself right now: down, getting up, or his swing returns
static qboolean Trainer_Open( const gentity_t *enemy ) {
	playerState_t *ps = &enemy->client->ps;

	return (qboolean)( ps->forceHandExtend == HANDEXTEND_KNOCKDOWN || BG_InKnockDownOnGround( ps ) || PM_InKnockDown( ps )
		|| (ps->weapon == WP_SABER && PM_SaberInReturn( ps->saberMove )) );
}

// where a player will be in sec: his speed carried on, gravity in the air, the rope pulls straight
static void Trainer_Predict( const playerState_t *ps, float sec, vec3_t out ) {
	VectorMA( ps->origin, sec, ps->velocity, out );
	if ( ps->groundEntityNum == ENTITYNUM_NONE && !(ps->pm_flags & PMF_GRAPPLE_PULL) )
		out[2] -= 0.5f * ps->gravity * sec * sec;
}

// flat distance from a to b, and how far b is above a
static float Trainer_FlatTo( const vec3_t a, const vec3_t b, float *above ) {
	if ( above )
		*above = b[2] - a[2];
	return sqrtf( (b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) );
}

// how far both will be apart (flat) in sec, and how far the player will be above it
static float Trainer_FlatIn( const gentity_t *self, const gentity_t *enemy, float sec, float *above ) {
	vec3_t a, b;

	Trainer_Predict( &self->client->ps, sec, a );
	Trainer_Predict( &enemy->client->ps, sec, b );
	return Trainer_FlatTo( a, b, above );
}

// the floor under a point, at most 256 units down; qfalse over a pit
static qboolean Trainer_Floor( const vec3_t from, int skip, vec3_t out ) {
	vec3_t	down;
	trace_t	tr;

	VectorCopy( from, down );
	down[2] -= 256.0f;
	trap->Trace( &tr, from, NULL, NULL, down, skip, MASK_SOLID, qfalse, 0, 0 );
	if ( tr.startsolid || tr.fraction >= 1.0f )
		return qfalse;
	VectorCopy( tr.endpos, out );
	return qtrue;
}

// where its hook goes: the floor short of the player and a little to a side, as the recorded player hooked. A hook
// into his body pulls it into his saber.
static void Trainer_HookPoint( const trainer_t *t, const gentity_t *self, const gentity_t *enemy, vec3_t out ) {
	const float	*e = enemy->client->ps.origin;
	vec3_t		dir, side, from;
	float		flat;

	VectorSubtract( self->client->ps.origin, e, dir );
	dir[2] = 0.0f;
	flat = VectorNormalize( dir );
	VectorSet( side, -dir[1], dir[0], 0.0f );
	VectorMA( e, Q_min( tp.HOOK_SHORT, flat * 0.5f ), dir, from );
	VectorMA( from, t->hookSide, side, from );
	if ( !Trainer_Floor( from, self->s.number, out ) ) {
		VectorCopy( from, out );
		out[2] = e[2] + enemy->r.mins[2];
	}
}

// after a missed kick: a floor 186 units off, 60 to 150 degrees off the line to him, to a side or behind
static qboolean Trainer_EscapePoint( trainer_t *t, const gentity_t *self, const gentity_t *enemy ) {
	vec3_t	toHim, angles, dir, from;
	int		i;

	VectorSubtract( enemy->client->ps.origin, self->client->ps.origin, toHim );
	vectoangles( toHim, angles );
	for ( i = 0; i < 4; i++ ) {
		const vec3_t a = { 0.0f, angles[YAW] + ( Q_irand( 0, 1 ) ? 1.0f : -1.0f ) * Q_flrand( 60.0f, 150.0f ), 0.0f };

		AngleVectors( a, dir, NULL, NULL );
		VectorMA( self->client->ps.origin, tp.ESCAPE_LEN, dir, from );
		if ( Trainer_Floor( from, self->s.number, t->escapePoint ) )
			return qtrue;
	}
	return qfalse;
}

// it looks at the floor point of its hook: the hook goes out where it looks, so already on the way to it
static qboolean Trainer_HookAim( const trainer_t *t, const playerState_t *ps ) {
	if ( t->state == TS_HOOK )
		return qtrue;
	if ( t->state != TS_KICK || t->noHop )
		return qfalse;
	return (qboolean)( t->step == KS_HOP || t->step == KS_YANK || t->step == KS_FLIP
		|| (t->step == KS_RUN && ps->groundEntityNum == ENTITYNUM_NONE) );
}

static trainerThreat_t Trainer_Threat( const gentity_t *self, const gentity_t *enemy, float dist ) {
	const playerState_t *ps = &enemy->client->ps;
	vec3_t toSelf;

	if ( enemy->client->hook && enemy->client->hook->enemy == self )
		return TT_HOOK;
	if ( ps->torsoAnim == BOTH_KYLE_GRAB && dist < tp.COUNTER_GRAB )
		return TT_GRAB;
	if ( ps->groundEntityNum == ENTITYNUM_NONE && dist < tp.COUNTER_KICK ) {
		VectorSubtract( self->client->ps.origin, ps->origin, toSelf );
		toSelf[2] = 0.0f;
		VectorNormalize( toSelf );
		if ( toSelf[0] * ps->velocity[0] + toSelf[1] * ps->velocity[1] > 100.0f )
			return TT_KICK;
	}
	return TT_NONE;
}

// qtrue once a sign has lasted the reaction time, if the trainer saw it at all (one roll for each new sign)
static qboolean Trainer_Notice( trainerSign_t *s, int sign, int chance, int delay ) {
	if ( sign != s->sign ) {
		s->sign = sign;
		s->since = level.time;
		s->seen = (qboolean)( Q_irand( 1, 100 ) <= chance );
	}
	return (qboolean)( sign && s->seen && level.time - s->since >= delay );
}

// turns the view at most maxTurn degrees toward target, turned yawOffset degrees further (NULL: keeps it), and
// puts it into the command. Returns how many degrees the yaw is still off.
static float Trainer_Aim( bot_state_t *bs, const vec3_t target, float yawOffset, float maxTurn, float *off, usercmd_t *cmd ) {
	vec3_t	dir, want;
	float	left = 0.0f;
	int		i;

	if ( off )
		*off = 0.0f;

	if ( target ) {
		VectorSubtract( target, bs->eye, dir );
		vectoangles( dir, want );
		want[YAW] = AngleMod( want[YAW] + yawOffset );
	}
	for ( i = 0; i < 3; i++ ) {
		const float delta = SHORT2ANGLE( bs->cur_ps.delta_angles[i] );
		float view = AngleMod( bs->viewangles[i] + delta );

		if ( target ) {
			view = AngleMod( view + Com_Clamp( -maxTurn, maxTurn, AngleSubtract( want[i], view ) ) );
			if ( i == YAW )
				left = AngleSubtract( want[i], view );
			if ( off && i != ROLL )
				*off = Q_max( *off, fabsf( AngleSubtract( want[i], view ) ) );
		}
		bs->viewangles[i] = AngleMod( view - delta );
		cmd->angles[i] = ANGLE2SHORT( view ) - bs->cur_ps.delta_angles[i];
	}
	return left;
}

// degrees the player is to the left of where the trainer looks, negative to the right
static float Trainer_YawTo( const bot_state_t *bs, const gentity_t *enemy ) {
	vec3_t dir, want;

	VectorSubtract( enemy->client->ps.origin, bs->eye, dir );
	vectoangles( dir, want );
	return AngleSubtract( want[YAW], AngleMod( bs->viewangles[YAW] + SHORT2ANGLE( bs->cur_ps.delta_angles[YAW] ) ) );
}

// the hook can go out now (JP_HookCanFire, the parts a bot can miss)
static qboolean Trainer_CanHook( const gentity_t *self ) {
	const gclient_t *client = self->client;

	return (qboolean)( jp_allowHook.integer == 1 && !(client->ps.eFlags & EF_ALT_DIM) && !client->hook && !(client->ps.eFlags2 & EF2_HOOK)
		&& client->ps.jetpackFuel >= tp.HOOK_FUEL && client->pers.jp.hookTime + jp_hookFloodProtect.integer < level.time );
}

// the flip off the player of a kick, forward or to a side
static qboolean Trainer_Kicked( const playerState_t *ps ) {
	return (qboolean)( ps->legsAnim == BOTH_WALL_FLIP_BACK1 || ps->legsAnim == BOTH_WALL_FLIP_LEFT
		|| ps->legsAnim == BOTH_WALL_FLIP_RIGHT );
}

static qboolean Trainer_Visible( const bot_state_t *bs, const gentity_t *enemy ) {
	trace_t tr;

	trap->Trace( &tr, bs->eye, NULL, NULL, enemy->client->ps.origin, bs->client, MASK_SOLID, qfalse, 0, 0 );
	return (qboolean)( tr.fraction >= 1.0f || tr.entityNum == enemy->s.number );
}

// the saber keys: one at a time, the way a player presses them
static qboolean Trainer_SaberKey( const gentity_t *self, const trainer_t *t ) {
	const playerState_t *ps = &self->client->ps;

	return (qboolean)( ps->weapon == WP_SABER && ps->weaponTime <= 0 && t->styleTime <= level.time );
}

static void Trainer_ToggleSaber( gentity_t *self, trainer_t *t ) {
	if ( Trainer_SaberKey( self, t ) ) {
		t->styleTime = level.time + 500;
		Cmd_ToggleSaber_f( self );
	}
}

// the red style, as far as the saber has one
static void Trainer_Style( gentity_t *self, trainer_t *t ) {
	if ( Trainer_SaberKey( self, t ) && !self->client->ps.saberHolstered && self->client->ps.fd.saberAnimLevel != SS_STRONG ) {
		t->styleTime = level.time + 400;
		Cmd_SaberAttackCycle_f( self );
	}
}

// the saber on and in the red style
static void Trainer_Saber( gentity_t *self, trainer_t *t ) {
	if ( self->client->ps.saberHolstered )
		Trainer_ToggleSaber( self, t );
	else
		Trainer_Style( self, t );
}

static qboolean Trainer_SaberReady( const playerState_t *ps ) {
	return (qboolean)( ps->weapon == WP_SABER && !ps->saberHolstered && ps->weaponTime <= 0 );
}

static void Trainer_Learn( trainer_t *t ) {
	int i;

	if ( !t->learned ) {
		const float prior[TA_NUM] = { tp.PRIOR_HOOKKICK, tp.PRIOR_RUNKICK, tp.PRIOR_SIDEKICK, tp.PRIOR_SWING, tp.PRIOR_WAIT };

		for ( i = 0; i < TA_NUM; i++ )
			t->value[i] = prior[i];
		t->learned = qtrue;
	}
}

// the score of the approach that ran: what it dealt minus what it took, after tp.TRY_MSEC or when the next
// one begins (now)
static void Trainer_Score( trainer_t *t, qboolean now ) {
	if ( t->approach < 0 || (!now && level.time - t->approachTime < tp.TRY_MSEC) )
		return;
	Trainer_Learn( t );
	t->value[t->approach] += tp.LEARN_RATE * ( (float)( t->dealt - t->taken ) - t->value[t->approach] );
	t->tries[t->approach]++;
	t->approach = -1;
}

/*
=================
Trainer_Choose

the next attack, from the approaches that fit now: the one that worked best against this player, with a bonus for
the ones tried less (so a player who changes is noticed) and a random part that is the larger the lower the level
=================
*/
static void Trainer_Choose( trainer_t *t, const bot_state_t *bs, const gentity_t *self, const gentity_t *enemy, float dist ) {
	const playerState_t	*ps = &self->client->ps;
	const qboolean		onGround = ps->groundEntityNum != ENTITYNUM_NONE && enemy->client->ps.groundEntityNum != ENTITYNUM_NONE;
	const qboolean		canKick = JP_FEATURE( JPF_FLIPKICK ) && ps->fd.forcePowerLevel[FP_LEVITATION] > FORCE_LEVEL_1;
	qboolean			fits[TA_NUM];
	float				best = -1.0e9f;
	int					i, total = 0, pick = -1;

	if ( dist >= tp.HOOK_MIN && dist <= tp.HOOK_MAX && Trainer_CanHook( self ) && Trainer_Visible( bs, enemy ) ) {
		Trainer_SetState( t, TS_HOOK );		// far off: the long pull, not scored
		return;
	}
	if ( dist <= tp.GRAB_RANGE && onGround && g_debugMelee.integer && (ps->stats[STAT_WEAPONS] & (1 << WP_MELEE))
		&& !Q_irand( 0, 4 ) ) {
		Trainer_SetState( t, TS_GRAB );
		return;
	}
	if ( dist > tp.ATTACK_RANGE )
		return;

	fits[TA_HOOKKICK] = canKick && dist <= tp.HOP_RANGE && Trainer_CanHook( self );
	fits[TA_RUNKICK] = canKick;
	fits[TA_SIDEKICK] = canKick && onGround && dist <= tp.SIDE_RANGE
		&& fabsf( enemy->client->ps.origin[2] - ps->origin[2] ) < tp.SIDE_LEVEL;
	fits[TA_SWING] = qtrue;
	fits[TA_WAIT] = t->level >= 3;

	Trainer_Score( t, qtrue );
	Trainer_Learn( t );
	for ( i = 0; i < TA_NUM; i++ )
		total += t->tries[i];
	for ( i = 0; i < TA_NUM; i++ ) {
		float score;

		if ( !fits[i] )
			continue;
		score = t->value[i] + tp.EXPLORE * sqrtf( logf( (float)total + 1.0f ) / ( (float)t->tries[i] + 1.0f ) )
			+ Q_flrand( 0.0f, trainerLevels[t->level - 1].chance );
		if ( score > best ) {
			best = score;
			pick = i;
		}
	}
	if ( pick < 0 )
		return;

	t->approach = pick;
	t->approachTime = level.time;
	t->dealt = t->taken = 0;
	switch ( pick ) {
	case TA_HOOKKICK:
	case TA_RUNKICK:
		Trainer_SetState( t, TS_KICK );
		t->noHop = (qboolean)( pick == TA_RUNKICK );
		break;
	case TA_SIDEKICK:
		Trainer_SetState( t, TS_SIDEKICK );
		t->side = ( Trainer_YawTo( bs, enemy ) >= 0.0f ) ? 1 : -1;	// keeps the player on the side it is nearer
		break;
	case TA_SWING:
		Trainer_SetState( t, TS_SWING );
		break;
	default:
		t->nextAttack = level.time + (int)tp.WAIT_MSEC;	// stays in the ring, his attack is countered
		break;
	}
}

// in the middle of something that can not be stopped: in the air for a kick or holding the player
static qboolean Trainer_Busy( const trainer_t *t ) {
	return (qboolean)( (t->state == TS_KICK && t->step != KS_RUN) || (t->state == TS_SIDEKICK && t->step > 0)
		|| (t->state == TS_GRAB && t->step > 0) );
}

// strafes round the player, now and then the other way
static void Trainer_Circle( trainer_t *t, usercmd_t *cmd ) {
	if ( !t->circle || level.time >= t->circleTime ) {
		t->circle = ( !t->circle || !Q_irand( 0, 2 ) ) ? ( Q_irand( 0, 1 ) ? 1 : -1 ) : -t->circle;
		t->circleTime = level.time + Q_irand( (int)tp.CIRCLE_MIN, (int)tp.CIRCLE_MAX );
	}
	cmd->rightmove = t->circle * 127;
}

static void Trainer_Counter( gentity_t *self, const gentity_t *enemy, trainer_t *t, trainerThreat_t threat, float dist,
	usercmd_t *cmd ) {
	const playerState_t	*ps = &self->client->ps;
	const float			dz = enemy->client->ps.origin[2] - ps->origin[2];
	const float			flat = sqrtf( Q_max( 0.0f, dist * dist - dz * dz ) );

	Trainer_Saber( self, t );
	switch ( threat ) {
	case TT_HOOK:
	case TT_KICK:
		// He comes through the air and can not turn: jump to meet him where he will be at the press and kick, the
		// counter of the recorded player. Coming but not yet near enough: it waits for him facing him. Else out of
		// the line of the rope and of the kick, which only hits straight ahead of him, to the side he is not turning to.
		{
			const qboolean	canMeet = ps->groundEntityNum != ENTITYNUM_NONE && JP_FEATURE( JPF_FLIPKICK )
				&& ps->fd.forcePowerLevel[FP_LEVITATION] > FORCE_LEVEL_1 && flat >= tp.TOO_CLOSE && flat <= tp.MEET_RANGE;
			float			above;
			const float		atPress = Trainer_FlatIn( self, enemy, tp.PRESS_SEC, &above );

			if ( canMeet && atPress >= tp.REACH_NEAR && atPress <= tp.REACH_FAR && above >= -tp.MEET_BELOW ) {
				Trainer_SetState( t, TS_KICK );
				Trainer_Step( t, KS_HOLD );
				cmd->forwardmove = 127;
				cmd->upmove = 127;
			}
			else if ( canMeet && atPress > tp.REACH_FAR
				&& Trainer_FlatIn( self, enemy, tp.ARRIVE_SEC, NULL ) <= tp.REACH_FAR ) {
				// he arrives in a moment: stay in front of him
			}
			else {
				vec3_t dir, angles;

				VectorSubtract( ps->origin, enemy->client->ps.origin, dir );
				vectoangles( dir, angles );
				// it is to the left of where he looks: further to his left, which is its right
				t->circle = ( AngleSubtract( angles[YAW], enemy->client->ps.viewangles[YAW] ) >= 0.0f ) ? 1 : -1;
				t->circleTime = level.time + (int)tp.CIRCLE_MIN;
				cmd->rightmove = t->circle * 127;
			}
		}
		break;
	case TT_GRAB:
		// out of reach of the hand, the grab breaks off past 64 units
		cmd->forwardmove = -127;
		if ( dist < tp.GRAB_RANGE && Trainer_SaberReady( ps ) )
			cmd->buttons |= BUTTON_ATTACK;
		break;
	default:
		break;
	}
}

// the long pull from afar, let go near the player for the kick (Trainer_LetGo)
static void Trainer_Hook( trainer_t *t, const gentity_t *self, float dist, usercmd_t *cmd ) {
	if ( dist <= tp.HOOK_NEAR ) {
		Trainer_SetState( t, TS_KICK );
		return;
	}
	if ( level.time - t->stateTime > tp.HOOK_TIME || (level.time - t->stateTime > 400 && !self->client->hook) ) {
		Trainer_Done( t );
		return;
	}
	cmd->forwardmove = 127;
	cmd->buttons |= BUTTON_HOOK;
}

static void Trainer_Yank( trainer_t *t, usercmd_t *cmd ) {
	Trainer_Step( t, KS_YANK );
	cmd->buttons |= BUTTON_HOOK;
}

/*
=================
Trainer_Kick

The JA+ flip kick (PM_CheckJump): in the air, still going up fast and near the ground, a new press of jump while
moving forward with the player right in front. The steps and their times are the ones of the movement log in the
head of this file. The bot sends one command a server frame (50 ms at sv_fps 20), so the times are in msec.
=================
*/
// in the air above the player near the top of the jump, at a distance for the short pull
static qboolean Trainer_YankNow( const gentity_t *self, float dist, float dz, float from ) {
	const playerState_t *ps = &self->client->ps;

	return (qboolean)( ps->groundEntityNum == ENTITYNUM_NONE && dist <= from && dist > tp.YANK_TO
		&& dz <= -tp.YANK_ABOVE && ps->velocity[2] < tp.YANK_RISE && Trainer_CanHook( self ) );
}

// The kick jump now: the player will be in reach at the press (both carried on for tp.PRESS_SEC), or, while
// he stands still, the flat distance by its own speed (the player took off at 75 to 106 at 250 to 466).
static qboolean Trainer_TakeOff( const gentity_t *self, const gentity_t *enemy, float flat, float speed ) {
	const float atPress = Trainer_FlatIn( self, enemy, tp.PRESS_SEC, NULL );

	if ( atPress >= tp.REACH_NEAR && atPress <= tp.REACH_FAR )
		return qtrue;
	return (qboolean)( atPress > tp.REACH_FAR && flat <= Q_min( tp.TAKEOFF + speed * tp.TAKEOFF_SPEED, tp.TAKEOFF_MAX )
		&& VectorLengthSquared( enemy->client->ps.velocity ) < 100.0f * 100.0f );
}

static void Trainer_Kick( trainer_t *t, const gentity_t *self, const gentity_t *enemy, float dist, float aimOff, usercmd_t *cmd ) {
	const playerState_t *ps = &self->client->ps;
	const qboolean onGround = ps->groundEntityNum != ENTITYNUM_NONE;
	const int msec = level.time - t->stepTime;
	const float speed = sqrtf( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] );
	const float dz = enemy->client->ps.origin[2] - ps->origin[2];	// negative: it is above him
	const float flat = sqrtf( Q_max( 0.0f, dist * dist - dz * dz ) );

	cmd->forwardmove = 127;
	if ( level.time - t->stateTime > tp.KICK_TIME ) {
		Trainer_Done( t );
		return;
	}

	switch ( t->step ) {
	case KS_RUN:
		if ( !onGround ) {
			if ( !t->noHop && Trainer_YankNow( self, dist, dz, tp.YANK_FROM ) )
				Trainer_Yank( t, cmd );
		}
		else if ( flat < tp.TOO_CLOSE || dz < -tp.LEVEL_DZ )
			cmd->forwardmove = -127;		// right at him or on top of him: no room for the kick
		else if ( dz < tp.LEVEL_DZ && Trainer_TakeOff( self, enemy, flat, speed ) ) {
			cmd->upmove = 127;
			Trainer_Step( t, KS_HOLD );
		}
		else if ( !t->noHop && dist <= tp.HOP_RANGE && Trainer_CanHook( self ) ) {
			cmd->upmove = 127;
			Trainer_Step( t, KS_HOP );
		}
		else if ( dist > tp.ATTACK_RANGE * 1.5f )
			Trainer_Done( t );
		break;
	case KS_HOP:
		if ( Trainer_YankNow( self, dist, dz, tp.YANK_FROM ) )
			Trainer_Yank( t, cmd );
		else if ( onGround && msec > 150 )
			Trainer_Step( t, KS_RUN );
		break;
	case KS_YANK:
		if ( self->client->pers.jp.hooked && !t->hookedTime )
			t->hookedTime = level.time;
		// let go: without the hook button Trainer_LetGo presses use
		if ( onGround || dist - speed * tp.YANK_LEAD <= tp.YANK_TO
			|| (t->hookedTime && level.time - t->hookedTime >= tp.YANK_MSEC)
			|| (!t->hookedTime && msec > tp.YANK_MISS) )
			Trainer_Step( t, KS_RUN );
		else
			cmd->buttons |= BUTTON_HOOK;
		break;
	case KS_HOLD:
		// this command lets go; early when he will be in reach, he kicked it out of the air while it still held
		if ( msec >= tp.JUMP_HOLD
			|| (msec >= tp.JUMP_GAP && Trainer_FlatIn( self, enemy, tp.JUMP_GAP / 1000.0f, NULL ) <= tp.REACH_FAR) )
			Trainer_Step( t, KS_GAP );
		else
			cmd->upmove = 127;
		break;
	case KS_GAP:
		if ( onGround && msec > 100 )
			Trainer_Step( t, KS_RUN );		// down again without a kick
		else if ( msec >= tp.JUMP_GAP ) {
			cmd->upmove = 127;
			Trainer_Step( t, KS_PRESS );
		}
		break;
	case KS_PRESS:
		if ( Trainer_Kicked( ps ) ) {
			t->chain++;
			Trainer_Step( t, KS_FLIP );
		}
		else if ( onGround || ps->velocity[2] <= 200.0f ) {
			// missed: the recorded player hooked the floor away from him at once
			if ( !onGround && Trainer_CanHook( self ) && Trainer_EscapePoint( t, self, enemy ) )
				Trainer_Step( t, KS_ESCAPE );
			else
				Trainer_Step( t, KS_RUN );
		}
		else
			Trainer_Step( t, KS_GAP );		// out of reach yet: let go and press again
		break;
	case KS_FLIP:
		// a player on the ground gets the red swing on top, else the hook pulls it in for the next kick
		if ( enemy->client->ps.forceHandExtend == HANDEXTEND_KNOCKDOWN || BG_InKnockDownOnGround( &enemy->client->ps ) )
			Trainer_SetState( t, TS_SWING );
		else if ( msec >= tp.CHAIN_WAIT && t->chain <= t->level / 2 && Trainer_YankNow( self, dist, dz, tp.YANK_CHAIN ) ) {
			t->stateTime = level.time;
			Trainer_Yank( t, cmd );
		}
		else if ( onGround )
			Trainer_Done( t );
		break;
	case KS_ESCAPE:
		// the hook goes out once it looks at the escape point (Trainer_Think aims there), is held, and let go
		// (Trainer_LetGo); from where it lands it comes again
		cmd->forwardmove = 0;
		if ( !t->hookedTime ) {
			if ( aimOff <= tp.ESCAPE_AIM || msec >= tp.ESCAPE_WAIT ) {
				if ( Trainer_CanHook( self ) ) {
					t->hookedTime = level.time;
					cmd->buttons |= BUTTON_HOOK;
				}
				else
					Trainer_Step( t, KS_RUN );
			}
			else if ( onGround )
				Trainer_Step( t, KS_RUN );
		}
		else if ( level.time - t->hookedTime < tp.ESCAPE_HOLD && self->client->hook )
			cmd->buttons |= BUTTON_HOOK;
		else {
			t->stateTime = level.time;
			Trainer_Step( t, KS_RUN );
		}
		break;
	default:
		Trainer_Step( t, KS_RUN );
		break;
	}
}

// strafes into the player at its side (Trainer_Aim keeps it there) and jumps: PM_CheckJump flips off him and kicks
static void Trainer_SideKick( trainer_t *t, const gentity_t *self, const gentity_t *enemy, float dist, float yawLeft,
	usercmd_t *cmd ) {
	const playerState_t *ps = &self->client->ps;

	if ( t->step == 0 ) {
		cmd->rightmove = ( t->side > 0 ) ? -127 : 127;
		if ( ps->groundEntityNum != ENTITYNUM_NONE && dist <= tp.SIDE_JUMP && fabsf( yawLeft ) < tp.SIDE_AIM ) {
			cmd->upmove = 127;
			t->step = 1;
			t->stepTime = level.time;
		}
		else if ( level.time - t->stateTime > tp.SIDE_TIME || dist > tp.SIDE_RANGE * 1.3f )
			Trainer_Done( t );
		return;
	}
	if ( Trainer_Kicked( ps ) && ( enemy->client->ps.forceHandExtend == HANDEXTEND_KNOCKDOWN
		|| BG_InKnockDownOnGround( &enemy->client->ps ) ) )
		Trainer_SetState( t, TS_SWING );
	else if ( ps->groundEntityNum != ENTITYNUM_NONE && level.time - t->stepTime > 200 )
		Trainer_Done( t );
}

static void Trainer_Grab( trainer_t *t, const gentity_t *self, float dist, usercmd_t *cmd ) {
	const playerState_t *ps = &self->client->ps;

	if ( t->step == 0 ) {
		// in with the melee weapon, both attack buttons are the grab (TryGrapple)
		cmd->forwardmove = (dist > tp.GRAB_REACH) ? 127 : 0;
		if ( ps->weapon == WP_MELEE && ps->weaponTime <= 0 && dist <= tp.GRAB_REACH && ps->groundEntityNum != ENTITYNUM_NONE ) {
			cmd->buttons |= BUTTON_ATTACK | BUTTON_ALT_ATTACK;
			t->step = 1;
			t->stepTime = level.time;
		}
		else if ( level.time - t->stateTime > tp.GRAB_TIME / 2 || dist > tp.ATTACK_RANGE )
			Trainer_Done( t );
		return;
	}
	// forward when the hand gets the player is the punch (G_GrabSomeMofos), out of the grab comes the flip kick
	cmd->forwardmove = 127;
	if ( !BG_InGrappleMove( ps->torsoAnim ) && level.time - t->stepTime > 300 )
		Trainer_SetState( t, TS_KICK );
	else if ( level.time - t->stepTime > tp.GRAB_TIME )
		Trainer_Done( t );
}

// red style, forward and attack: the swing from above (LS_A_T2B)
static void Trainer_Swing( trainer_t *t, gentity_t *self, float dist, usercmd_t *cmd ) {
	const playerState_t *ps = &self->client->ps;

	Trainer_Saber( self, t );
	cmd->forwardmove = 127;
	if ( t->step == 0 ) {
		if ( dist <= tp.SWING_RANGE && Trainer_SaberReady( ps )
			&& (ps->fd.saberAnimLevel == SS_STRONG || level.time - t->stateTime > tp.RED_WAIT) ) {
			cmd->buttons |= BUTTON_ATTACK;
			t->step = 1;
			t->stepTime = level.time;
		}
		else if ( level.time - t->stateTime > tp.SWING_TIME )
			Trainer_Done( t );
		return;
	}
	if ( level.time - t->stepTime < 100 )
		cmd->buttons |= BUTTON_ATTACK;		// held for a few commands so the swing starts
	else if ( !BG_SaberInAttack( ps->saberMove ) && !PM_SaberInStart( ps->saberMove ) )
		Trainer_Done( t );
	else
		cmd->forwardmove = 0;
}

// A bot counts as a client with the JA+ plugin: without the hook button it hangs on the rope instead of letting
// go (JP_HookClientThink), only use drops the hook.
static void Trainer_LetGo( const gentity_t *self, usercmd_t *cmd ) {
	if ( self->client->hook && !(cmd->buttons & BUTTON_HOOK) )
		cmd->buttons |= BUTTON_USE;
}

static void Trainer_CloneCmd( trainer_t *t, bot_state_t *bs, gentity_t *self, const gentity_t *enemy, const cloneCmd_t *c,
	int elapsed, usercmd_t *cmd );

// the clone (g_clone.c): what the player it learned from would do, aimed where he would look
static void Trainer_CloneThink( trainer_t *t, bot_state_t *bs, gentity_t *self, const gentity_t *enemy, int elapsed,
	usercmd_t *cmd ) {
	cloneCmd_t c;

	Clone_Decide( self, enemy, &t->cloneState, elapsed, &c );

	// The kick itself is a reflex: the player presses in the one frame it reaches, the forests at 50 ms frames
	// mostly miss it (1.5 kicks a minute against his 10.7 with the same moves and hooks). On the ground, going for
	// him: the jump when he will be in reach at its press (as the trainer, Trainer_TakeOff). In the air, still rising
	// fast and with him in reach at the next frame: let go, then press, looking straight at him (the smoothed aim
	// goes on from there).
	if ( self->client->ps.groundEntityNum != ENTITYNUM_NONE && c.forwardmove > 0 && t->cloneUp <= 0
		&& JP_FEATURE( JPF_FLIPKICK ) ) {
		float above;
		const float atPress = Trainer_FlatIn( self, enemy, trainerDefaults.PRESS_SEC, &above );

		if ( atPress >= trainerDefaults.REACH_NEAR && atPress <= trainerDefaults.REACH_FAR
			&& fabsf( above ) < trainerDefaults.LEVEL_DZ ) {
			c.upmove = 127;
			c.yawOff = t->cloneState.yawOff = 0.0f;
		}
	}
	else if ( self->client->ps.groundEntityNum == ENTITYNUM_NONE && self->client->ps.velocity[2] > 200.0f
		&& JP_FEATURE( JPF_FLIPKICK ) ) {
		// the defaults, not tp: the clone stays the same opponent while tools/trainer tries other values
		const float atNext = Trainer_FlatIn( self, enemy, trainerDefaults.JUMP_GAP / 1000.0f, NULL );

		if ( atNext >= trainerDefaults.REACH_NEAR && atNext <= trainerDefaults.REACH_FAR ) {
			c.forwardmove = 127;
			c.rightmove = 0;
			c.yawOff = t->cloneState.yawOff = 0.0f;
			c.upmove = ( t->cloneUp > 0 ) ? 0 : 127;
		}
	}
	t->cloneUp = c.upmove;
	Trainer_CloneCmd( t, bs, self, enemy, &c, elapsed, cmd );
}

// the policy (g_policy.c): what the network picks. It learned in rounds that start near the player, a hook it holds
// with him not near is let go: it hung on the rope instead of searching him.
static void Trainer_PolicyThink( trainer_t *t, bot_state_t *bs, gentity_t *self, const gentity_t *enemy, int elapsed, usercmd_t *cmd ) {
	cloneCmd_t c;

	Policy_Decide( self, enemy, elapsed, &c );
	if ( self->client->hook && self->client->pers.jp.hooked ) {
		if ( !t->ropeTime )
			t->ropeTime = level.time;
		if ( level.time - t->ropeTime > TRAINER_ROPE_MSEC
			&& (Distance( self->client->ps.origin, enemy->client->ps.origin ) > TRAINER_ROPE_NEAR || level.time - t->seenTime > 500) ) {
			c.buttons = (c.buttons & ~BUTTON_HOOK) | BUTTON_USE;
			t->ropeTime = 0;
		}
	}
	else
		t->ropeTime = 0;
	Trainer_CloneCmd( t, bs, self, enemy, &c, elapsed, cmd );
}

// the moves, buttons and saber key of the clone or the policy into the usercmd, the view turned toward its aim as
// fast as a player with a mouse
static void Trainer_CloneCmd( trainer_t *t, bot_state_t *bs, gentity_t *self, const gentity_t *enemy, const cloneCmd_t *c,
	int elapsed, usercmd_t *cmd ) {
	vec3_t eye, dir, angles, point;

	if ( c->saber )
		Trainer_ToggleSaber( self, t );
	else
		Trainer_Style( self, t );		// the player fights in red (94 percent of his logs)

	cmd->forwardmove = c->forwardmove;
	cmd->rightmove = c->rightmove;
	cmd->upmove = c->upmove;
	cmd->buttons |= c->buttons;

	VectorCopy( self->client->ps.origin, eye );
	eye[2] += self->client->ps.viewheight;
	VectorSubtract( enemy->client->ps.origin, eye, dir );
	vectoangles( dir, angles );
	angles[YAW] -= c->yawOff;
	angles[PITCH] = c->pitch;
	AngleVectors( angles, dir, NULL, NULL );
	VectorMA( eye, 256.0f, dir, point );
	Trainer_Aim( bs, point, 0.0f, TRAINER_CLONE_TURN * elapsed / 1000.0f, NULL, cmd );
}

// a level with "h": a jump whenever it lands between its attacks, held 50 to 1000 ms so it goes up to different
// heights. The learned policy met no opponent who hops and did little against a player high in the air.
static void Trainer_Hop( trainer_t *t, const playerState_t *ps, usercmd_t *cmd ) {
	if ( !t->hop || cmd->upmove || (t->state != TS_CHASE && t->state != TS_GUARD) )
		return;
	if ( level.time < t->hopUntil )
		cmd->upmove = 127;
	else if ( ps->groundEntityNum != ENTITYNUM_NONE && !(ps->pm_flags & PMF_JUMP_HELD) ) {
		t->hopUntil = level.time + Q_irand( 50, 1000 );
		cmd->upmove = 127;
	}
}

/*
=================
Trainer_NextWP

the next waypoint from one to another on the shortest way (Dijkstra over the trail, each waypoint to the one
before and after it, and the neighbours), without the waypoint avoid; -1 if there is none
=================
*/
static int Trainer_NextWP( int from, int to, int avoid ) {
	static float	dist[MAX_WPARRAY_SIZE];
	static int		prev[MAX_WPARRAY_SIZE];
	static qboolean	done[MAX_WPARRAY_SIZE];
	const int		num = Q_min( gWPNum, MAX_WPARRAY_SIZE );
	int				i, u;

	if ( from < 0 || to < 0 || from >= num || to >= num )
		return -1;
	for ( i = 0; i < num; i++ ) {
		dist[i] = -1.0f;
		prev[i] = -1;
		done[i] = qfalse;
	}
	dist[from] = 0.0f;
	if ( avoid >= 0 && avoid < num && avoid != from && avoid != to )
		done[avoid] = qtrue;
	for ( ;; ) {
		const wpobject_t	*wp;
		int					next[MAX_NEIGHBOR_SIZE + 2], count = 0, k;

		u = -1;
		for ( i = 0; i < num; i++ ) {
			if ( !done[i] && dist[i] >= 0.0f && (u < 0 || dist[i] < dist[u]) )
				u = i;
		}
		if ( u < 0 || u == to )
			break;
		done[u] = qtrue;
		wp = gWPArray[u];
		next[count++] = u - 1;
		next[count++] = u + 1;
		for ( k = 0; k < wp->neighbornum && k < MAX_NEIGHBOR_SIZE; k++ )
			next[count++] = wp->neighbors[k].num;
		for ( k = 0; k < count; k++ ) {
			const int v = next[k];
			float d;

			if ( v < 0 || v >= num || done[v] || !gWPArray[v] || !gWPArray[v]->inuse )
				continue;
			d = dist[u] + Distance( wp->origin, gWPArray[v]->origin );
			if ( dist[v] < 0.0f || d < dist[v] ) {
				dist[v] = d;
				prev[v] = u;
			}
		}
	}
	if ( dist[to] < 0.0f )
		return -1;
	for ( u = to; prev[u] >= 0 && prev[u] != from; u = prev[u] )
		;
	return u;
}

// the clone and the policy search the player when he is far or out of sight: they only learned to fight near him.
// A map without waypoints: straight to him, as the trainer levels go.
static qboolean Trainer_Searching( trainer_t *t, const bot_state_t *bs, const gentity_t *self, const gentity_t *enemy ) {
	const float		dist = Distance( self->client->ps.origin, enemy->client->ps.origin );
	const qboolean	seen = Trainer_Visible( bs, enemy );

	if ( seen )
		t->seenTime = level.time;
	if ( !t->search && (dist > TRAINER_SEARCH_FAR || level.time - t->seenTime > TRAINER_SEARCH_UNSEEN) ) {
		t->search = qtrue;
		t->pathTime = 0;
		VectorCopy( self->client->ps.origin, t->stuckAt );
		t->stuckTime = level.time;
	}
	else if ( t->search && dist < TRAINER_SEARCH_NEAR && seen )
		t->search = qfalse;
	return t->search;
}

/*
=================
Trainer_SearchThink

runs to the player along the waypoints: the way is looked for again every second, a waypoint reached goes on to
the next. A waypoint higher up gets a jump, and so does being stuck (half a second without getting 32 units on).
Stuck for 1.5 s, it leaves that waypoint out for 5 s and steps to a side: one on a ledge held it for a minute.
=================
*/
static void Trainer_SearchThink( trainer_t *t, bot_state_t *bs, gentity_t *self, const gentity_t *enemy, int elapsed,
	usercmd_t *cmd ) {
	const playerState_t	*ps = &self->client->ps;
	vec3_t				target;

	if ( t->pathWP >= 0 && t->pathWP < gWPNum && gWPArray[t->pathWP]
		&& Distance( ps->origin, gWPArray[t->pathWP]->origin ) < 48.0f )
		t->pathTime = 0;		// reached: on to the next
	if ( level.time - t->stuckTime > 1500 ) {
		t->avoidWP = t->pathWP;
		t->avoidTime = level.time + 5000;
		t->dodge = Q_irand( 0, 1 ) ? 1 : -1;
		t->pathTime = 0;
		VectorCopy( ps->origin, t->stuckAt );
		t->stuckTime = level.time;
	}
	if ( level.time >= t->pathTime ) {
		t->pathTime = level.time + 1000;
		t->pathWP = Trainer_NextWP( GetNearestVisibleWP( (float *)ps->origin, self->s.number ),
			GetNearestVisibleWP( (float *)enemy->client->ps.origin, enemy->s.number ),
			level.time < t->avoidTime ? t->avoidWP : -1 );
	}
	if ( t->pathWP >= 0 && t->pathWP < gWPNum && gWPArray[t->pathWP] )
		VectorCopy( gWPArray[t->pathWP]->origin, target );
	else
		VectorCopy( enemy->client->ps.origin, target );

	Trainer_Aim( bs, target, 0.0f, TRAINER_CLONE_TURN * elapsed / 1000.0f, NULL, cmd );
	cmd->forwardmove = 127;
	if ( level.time < t->avoidTime - 4500 )
		cmd->rightmove = 127 * t->dodge;
	if ( ps->groundEntityNum != ENTITYNUM_NONE && !(ps->pm_flags & PMF_JUMP_HELD)
		&& (target[2] - ps->origin[2] > 40.0f || level.time - t->stuckTime > 500) )
		cmd->upmove = 127;
	if ( Distance( ps->origin, t->stuckAt ) > 32.0f ) {
		VectorCopy( ps->origin, t->stuckAt );
		t->stuckTime = level.time;
	}
	Trainer_Saber( self, t );
	Trainer_LetGo( self, cmd );
}

/*
=================
Trainer_Think

the usercmd of a training bot for this game frame
=================
*/
static void Trainer_Think( trainer_t *t, bot_state_t *bs, int time, int elapsed ) {
	const trainerLevel_t	*lvl;
	gentity_t				*self, *enemy;
	playerState_t			*ps;
	usercmd_t				*cmd = &t->cmd;
	trainerThreat_t			threat;
	float					dist, yawLeft, aimOff;
	vec3_t					aimAt;
	qboolean				attacked, open;

	self = &g_entities[bs->client];
	ps = &self->client->ps;
	lvl = &trainerLevels[Q_min( t->level, TRAINER_LEVELS ) - 1];

	memset( cmd, 0, sizeof( *cmd ) );
	cmd->serverTime = time;
	cmd->weapon = ps->weapon;
	if ( t->state == TS_GRAB )
		cmd->weapon = WP_MELEE;
	else if ( ps->stats[STAT_WEAPONS] & (1 << WP_SABER) )
		cmd->weapon = WP_SABER;

	if ( self->health <= 0 || ps->pm_type == PM_DEAD ) {
		Trainer_Aim( bs, NULL, 0.0f, 0.0f, NULL, cmd );
		if ( level.framenum & 1 )
			cmd->buttons = BUTTON_ATTACK;		// respawn
		Trainer_SetState( t, TS_CHASE );
		return;
	}

	enemy = Trainer_Enemy( t );
	if ( !enemy ) {
		Trainer_Aim( bs, NULL, 0.0f, 0.0f, NULL, cmd );
		Trainer_SetState( t, TS_CHASE );
		Trainer_LetGo( self, cmd );
		return;
	}

	if ( (t->clone || t->policy >= 0) && Trainer_Searching( t, bs, self, enemy ) ) {
		Trainer_SearchThink( t, bs, self, enemy, elapsed, cmd );
		return;
	}
	if ( t->clone ) {
		Trainer_CloneThink( t, bs, self, enemy, elapsed, cmd );
		return;
	}
	if ( t->policy >= 0 ) {
		Trainer_PolicyThink( t, bs, self, enemy, elapsed, cmd );
		return;
	}

	dist = Distance( ps->origin, enemy->client->ps.origin );
	if ( t->state == TS_KICK && t->step == KS_ESCAPE )
		VectorCopy( t->escapePoint, aimAt );
	else if ( Trainer_HookAim( t, ps ) )
		Trainer_HookPoint( t, self, enemy, aimAt );
	else
		Trainer_Predict( &enemy->client->ps, lvl->leadSec, aimAt );
	yawLeft = Trainer_Aim( bs, aimAt, ( t->state == TS_SIDEKICK ) ? -90.0f * t->side : 0.0f,
		lvl->turnSpeed * elapsed / 1000.0f, &aimOff, cmd );
	Trainer_Score( t, qfalse );

	attacked = Trainer_Notice( &t->attack, Trainer_Attacking( enemy, dist ), lvl->readChance, lvl->reactMs );
	open = Trainer_Notice( &t->open, Trainer_Open( enemy ), lvl->readChance, lvl->reactMs );
	threat = (trainerThreat_t)Trainer_Threat( self, enemy, dist );
	if ( !Trainer_Notice( &t->threat, threat, lvl->counterChance, lvl->reactMs / 2 ) )
		threat = TT_NONE;

	if ( !Trainer_Busy( t ) ) {
		if ( threat != TT_NONE )
			Trainer_SetState( t, TS_COUNTER );
		else if ( attacked )
			Trainer_SetState( t, TS_GUARD );		// no hook, grab or kick into an attack
		else if ( t->state == TS_COUNTER && dist <= tp.KICK_BACK && JP_FEATURE( JPF_FLIPKICK ) )
			Trainer_SetState( t, TS_KICK );		// out of his hook or kick: kicks back at once
		else if ( t->state == TS_COUNTER || t->state == TS_GUARD )
			Trainer_Done( t );
		if ( t->state == TS_CHASE && open && dist <= tp.PUNISH_RANGE && threat == TT_NONE )
			Trainer_SetState( t, TS_SWING );		// no pause for a player who can not defend himself
		else if ( t->state == TS_CHASE && level.time >= t->nextAttack )
			Trainer_Choose( t, bs, self, enemy, dist );
	}

	switch ( t->state ) {
	case TS_GUARD:
		// To a side out of the line of the swing, backing off straight was followed and hit. Within the reach of a
		// swing also back (the blade only blocks when it is in the way, d_saberSPStyleDamage), and a jump back when
		// his swing starts that close. When the swing returns he is open (punish above).
		Trainer_Saber( self, t );
		Trainer_Circle( t, cmd );
		if ( dist < tp.SWING_REACH ) {
			cmd->forwardmove = -127;
			if ( ps->groundEntityNum != ENTITYNUM_NONE && !(ps->pm_flags & PMF_JUMP_HELD)
				&& PM_SaberInStart( enemy->client->ps.saberMove ) )
				cmd->upmove = 127;
		}
		break;
	case TS_COUNTER:
		Trainer_Counter( self, enemy, t, (trainerThreat_t)t->threat.sign, dist, cmd );
		break;
	case TS_HOOK:
		Trainer_Hook( t, self, dist, cmd );
		break;
	case TS_KICK:
		Trainer_Kick( t, self, enemy, dist, aimOff, cmd );
		break;
	case TS_SIDEKICK:
		Trainer_SideKick( t, self, enemy, dist, yawLeft, cmd );
		break;
	case TS_GRAB:
		Trainer_Grab( t, self, dist, cmd );
		break;
	case TS_SWING:
		Trainer_Swing( t, self, dist, cmd );
		break;
	default:
		// between attacks: in the ring round the player, not right in front of him where his kick lands
		Trainer_Saber( self, t );
		if ( dist > tp.RING_FAR )
			cmd->forwardmove = 127;
		else {
			if ( dist < tp.RING_NEAR )
				cmd->forwardmove = -127;
			Trainer_Circle( t, cmd );
		}
		break;
	}
	Trainer_Hop( t, ps, cmd );
	Trainer_LetGo( self, cmd );
}

/*
=================
G_TrainerCmd

from BotUpdateInput: the usercmd of a training bot, qfalse for any other bot. The bot frame runs more often than
the game frame (every client frame of a listen server, every frame of a dedicated one between two game frames),
but a bot moves once a game frame with the last command it sent (G_RunClient). So the command is made once a game
frame and sent again as it is: a press made in the first call would be gone with the second.
=================
*/
qboolean G_TrainerCmd( bot_state_t *bs, int time, int elapsed ) {
	trainer_t *t = Trainer_Get( bs->client );

	if ( !t )
		return qfalse;
	Trainer_LoadParams();
	if ( t->cmdTime != level.time ) {
		t->cmdTime = level.time;
		Trainer_Think( t, bs, time, elapsed );
	}
	bs->lastucmd = t->cmd;
	bs->lastucmd.serverTime = time;
	return qtrue;
}

// a training bot thinks in G_TrainerCmd, not in the stock bot AI
qboolean G_TrainerActive( int clientNum ) {
	return (qboolean)( Trainer_Get( clientNum ) != NULL );
}

// for the movement log: state, kick step and approach of a training bot, qfalse for anybody else
qboolean G_TrainerState( int clientNum, int out[3] ) {
	const trainer_t *t = Trainer_Get( clientNum );

	if ( !t )
		return qfalse;
	out[0] = t->state;
	out[1] = t->step;
	out[2] = t->approach;
	return qtrue;
}

// from WP_InitForcePowers: the powers of a training bot, whatever its profile has
void G_TrainerForcePowers( int clientNum, char *forcePowers, size_t size ) {
	if ( Trainer_Get( clientNum ) )
		Q_strncpyz( forcePowers, TRAINER_FORCEPOWERS, size );
}

// from G_Damage, after the damage is taken (at most the health there was): what the approach that runs dealt to the
// player and took from him, the reward of a learned policy
void G_TrainerDamage( const gentity_t *targ, const gentity_t *attacker, int take, int mod ) {
	int i;

	if ( take <= 0 )
		return;
	Policy_Damage( targ, attacker, take, mod );
	if ( !attacker )
		return;
	if ( match.on ) {
		for ( i = 0; i < 2; i++ ) {
			if ( attacker->s.number == match.bot[i] && targ->s.number == match.bot[1 - i] ) {
				match.dealt[i] += take;
				if ( mod == MOD_MELEE )
					match.kicks[i]++;
			}
		}
	}
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		trainer_t *t = &trainers[i];

		if ( !t->active || t->approach < 0 )
			continue;
		if ( attacker->s.number == i && targ->s.number == t->owner )
			t->dealt += take;
		else if ( targ->s.number == i && attacker->s.number == t->owner )
			t->taken += take;
	}
}

// from ClientDisconnect: the slot is free, and the trainers of a player who leaves go too
void G_TrainerDisconnect( int clientNum ) {
	int i;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS )
		return;
	Policy_Reset( clientNum, -1 );
	memset( &trainers[clientNum], 0, sizeof( trainers[0] ) );
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( trainers[i].active && trainers[i].owner == clientNum )
			trainers[i].drop = qtrue;
	}
}

// the result line of a match into its file, both bots leave
static void Trainer_MatchEnd( const char *why ) {
	char			line[1024];
	fileHandle_t	f;
	int				i, j;

	match.on = qfalse;
	Com_sprintf( line, sizeof( line ), "levelA=%d levelB=%d rounds=%d sec=%.1f dealtA=%d dealtB=%d killsA=%d killsB=%d "
		"kicksA=%d kicksB=%d end=%s", match.level[0], match.level[1], match.round, ( level.time - match.start ) / 1000.0f,
		match.dealt[0], match.dealt[1], match.kills[0], match.kills[1], match.kicks[0], match.kicks[1], why );
	for ( i = 0; i < 2; i++ ) {
		const trainer_t *t = &trainers[match.bot[i]];

		for ( j = 0; j < TA_NUM; j++ ) {
			char name[32];
			int k;

			Q_strncpyz( name, trainerApproachNames[j], sizeof( name ) );
			for ( k = 0; name[k]; k++ ) {
				if ( name[k] == ' ' )
					name[k] = '_';		// one key=value without spaces
			}
			Q_strcat( line, sizeof( line ), va( " %c.%s=%.1f/%d", 'A' + i, name, t->value[j], t->tries[j] ) );
		}
	}
	Q_strcat( line, sizeof( line ), "\n" );
	trap->FS_Open( match.file, &f, FS_APPEND );
	if ( f ) {
		trap->FS_Write( line, strlen( line ), f );
		trap->FS_Close( f );
	}
	trap->Print( "vs_trainermatch: %s", line );
	for ( i = 0; i < 2; i++ ) {
		Policy_MatchEnd( match.bot[i] );
		if ( g_entities[match.bot[i]].inuse )
			trap->DropClient( match.bot[i], "left the match" );
	}
	if ( match.quit )
		trap->SendConsoleCommand( EXEC_APPEND, "quit\n" );
}

/*
=================
Trainer_MatchPlace

B near A for a new round, facing each other, on a floor and with nothing between them. Spawned far apart, bots
that learned from a player who was always close (the clone, the policy) stand and wait.
=================
*/
static qboolean Trainer_MatchPlace( void ) {
	gentity_t	*a = &g_entities[match.bot[0]], *b = &g_entities[match.bot[1]];
	vec3_t		dir, end, down, angles;
	trace_t		tr;
	int			i;

	for ( i = 0; i < 8; i++ ) {
		const float yaw = Q_flrand( 0.0f, 360.0f );

		VectorSet( dir, cosf( DEG2RAD( yaw ) ), sinf( DEG2RAD( yaw ) ), 0.0f );
		VectorMA( a->client->ps.origin, Q_flrand( TRAINER_MATCH_NEAR, TRAINER_MATCH_FAR ), dir, end );
		trap->Trace( &tr, a->client->ps.origin, b->r.mins, b->r.maxs, end, a->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
		if ( tr.startsolid || tr.allsolid || tr.fraction < 1.0f )
			continue;
		VectorCopy( end, down );
		down[2] -= 128.0f;
		trap->Trace( &tr, end, b->r.mins, b->r.maxs, down, b->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
		if ( tr.startsolid || tr.fraction >= 1.0f )
			continue;		// no floor near below: a pit or a ledge

		VectorSet( angles, 0.0f, yaw + 180.0f, 0.0f );
		TeleportPlayer( b, tr.endpos, angles );
		VectorClear( b->client->ps.velocity );		// TeleportPlayer throws the player forward
		b->client->ps.pm_time = 0;
		b->client->ps.pm_flags &= ~PMF_TIME_KNOCKBACK;
		VectorSet( angles, 0.0f, yaw, 0.0f );
		SetClientViewAngle( a, angles );
		return qtrue;
	}
	return qfalse;
}

// every frame of a match: a kill or the time ends a round, the last round the match; a round starts with the bots
// placed near each other
static void Trainer_MatchFrame( void ) {
	qboolean	ended = qfalse;
	int			i;

	if ( !match.on )
		return;
	for ( i = 0; i < 2; i++ ) {
		const gentity_t *ent = &g_entities[match.bot[i]];
		qboolean alive;

		if ( !ent->inuse || !ent->client || !trainers[match.bot[i]].active ) {
			Trainer_MatchEnd( "left" );
			return;
		}
		alive = (qboolean)( ent->health > 0 && !(ent->client->ps.eFlags & EF_DEAD) );
		if ( match.alive[i] && !alive ) {
			match.kills[1 - i]++;
			Policy_Reward( match.bot[i], 0.0f, 1.0f );
			Policy_Reward( match.bot[1 - i], 1.0f, 0.0f );
			ended = qtrue;
		}
		match.alive[i] = alive;
	}
	if ( match.place > 0 && match.alive[0] && match.alive[1] ) {
		if ( Trainer_MatchPlace() || --match.place <= 0 ) {
			match.place = 0;
			match.roundStart = level.time;
		}
		return;
	}
	if ( !ended && level.time - match.roundStart < TRAINER_MATCH_ROUND )
		return;
	for ( i = 0; i < 2; i++ )
		Policy_EndEpisode( match.bot[i], ended ? POLICY_DONE_END : POLICY_DONE_TIME );
	match.round++;
	match.roundStart = level.time;
	match.place = TRAINER_MATCH_TRIES;
	if ( match.round >= match.rounds )
		Trainer_MatchEnd( "done" );
}

// every frame: trainers without their player, or which the server does not allow any more, leave
void G_TrainerFrame( void ) {
	int i;

	Trainer_LoadParams();
	for ( i = 0; i < level.maxclients; i++ ) {
		const gentity_t *ent = &g_entities[i];
		trainer_t *t;

		if ( !ent->inuse || !ent->client || ent->client->pers.connected == CON_DISCONNECTED || !(t = Trainer_Get( i )) )
			continue;
		if ( t->drop || !Trainer_Owner( t ) || (!t->match && vs_allowTrainer.integer <= 0) || level.gametype != GT_FFA )
			trap->DropClient( i, "left the training" );
	}
	Trainer_MatchFrame();
}

// a trainer for this owner, and spawned again so its force powers are the ones of a trainer (WP_InitForcePowers)
static void Trainer_MatchBot( int bot, int owner, int lvl ) {
	trainer_t *t = &trainers[bot];

	memset( t, 0, sizeof( *t ) );
	t->checked = qtrue;
	t->active = qtrue;
	t->match = qtrue;
	t->owner = owner;
	t->approach = -1;
	Trainer_SetLevel( t, lvl );
	Trainer_SetKey( bot, owner, lvl );
	ClientSpawn( &g_entities[bot] );
}

/*
=================
Svcmd_TrainerMatch_f

vs_trainermatch <levelA> <levelB> <rounds> <file> [quit]: two training bots fight each other, a round ends with a
kill or after a minute. The result (damage, kills, the scores of the approaches) is added to the file as one line,
"quit" ends the server then. For tools/trainer, with vs_simFast and fixedtime on a dedicated server.
=================
*/
void Svcmd_TrainerMatch_f( void ) {
	char	arg[MAX_TOKEN_CHARS], overrides[MAX_INFO_STRING];
	int		i, lvl[2], bot[2];

	if ( trap->Argc() < 5 ) {
		trap->Print( "Usage: vs_trainermatch <levelA> <levelB> <rounds> <file> [quit], a level is 1-5, clone, ai or ai2\n" );
		return;
	}
	if ( match.on ) {
		trap->Print( "vs_trainermatch: a match runs already\n" );
		return;
	}
	if ( level.gametype != GT_FFA || !trap->Cvar_VariableIntegerValue( "bot_enable" ) ) {
		trap->Print( "vs_trainermatch: needs free for all and bot_enable 1\n" );
		return;
	}
	for ( i = 0; i < 2; i++ ) {
		trap->Argv( 1 + i, arg, sizeof( arg ) );
		lvl[i] = Trainer_ParseLevel( arg );
		if ( (lvl[i] & ~TRAINER_HOP) < 1 || (lvl[i] & ~TRAINER_HOP) > TRAINER_LAST ) {
			trap->Print( "vs_trainermatch: level %s is not 1 to %d (5h: one that hops), clone, ai or ai2\n", arg, TRAINER_LEVELS );
			return;
		}
		if ( !Trainer_LevelReady( lvl[i] ) ) {
			trap->Print( "vs_trainermatch: no model for %s (vs_cloneModel, vs_policyModel, vs_policyOpponent)\n", arg );
			return;
		}
	}
	trap->Argv( 3, arg, sizeof( arg ) );
	match.rounds = Com_Clampi( 1, 10000, atoi( arg ) );
	trap->Argv( 4, match.file, sizeof( match.file ) );
	trap->Argv( 5, arg, sizeof( arg ) );
	match.quit = (qboolean)( !Q_stricmp( arg, "quit" ) );

	// owners are set once both are there: no slot is known before
	for ( i = 0; i < 2; i++ ) {
		Com_sprintf( overrides, sizeof( overrides ), TRAINER_USERINFO "\\vs_trainer\\%d %d", MAX_CLIENTS, lvl[i] );
		bot[i] = G_AddBot( TRAINER_BOT, 5.0f, "", 0, i ? TRAINER_NAME "B" : TRAINER_NAME "A", overrides );
		if ( bot[i] < 0 ) {
			trap->Print( "vs_trainermatch: no free slot\n" );
			if ( i )
				trap->DropClient( bot[0], "left the match" );
			return;
		}
	}
	for ( i = 0; i < 2; i++ ) {
		Trainer_MatchBot( bot[i], bot[1 - i], lvl[i] );
		Policy_MatchStart( bot[i], lvl[1 - i] );
		match.bot[i] = bot[i];
		match.level[i] = lvl[i];
		match.dealt[i] = match.kills[i] = match.kicks[i] = 0;
		match.alive[i] = qtrue;
	}
	match.round = 0;
	match.start = match.roundStart = level.time;
	match.place = TRAINER_MATCH_TRIES;
	match.on = qtrue;
	trap->Print( "vs_trainermatch: level %d against level %d, %d rounds, into %s\n", lvl[0], lvl[1], match.rounds, match.file );
}

static void Trainer_Print( const gentity_t *ent, const char *text ) {
	trap->SendServerCommand( ent - g_entities, va( "print \"%s\n\"", text ) );
}

/*
=================
Cmd_Trainer_f

vs_trainer [1-5|clone|ai]: calls the training bot of the player, or sets its level (3 without a number); clone is a
bot that plays like the player of the clone model (g_clone.c), ai the learned policy (g_policy.c)
vs_trainer off: sends it away
vs_trainer info: what it learned about the player
=================
*/
void Cmd_Trainer_f( gentity_t *ent ) {
	const int	self = ent - g_entities;
	char		arg[MAX_TOKEN_CHARS], overrides[MAX_INFO_STRING];
	trainer_t	*mine = NULL;
	int			i, count = 0, lvl, bot;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		trainer_t *t = Trainer_Get( i );

		if ( t && !t->drop ) {
			count++;
			if ( t->owner == self )
				mine = t;
		}
	}

	trap->Argv( 1, arg, sizeof( arg ) );
	if ( !Q_stricmp( arg, "info" ) ) {
		if ( !mine ) {
			Trainer_Print( ent, "You have no training bot." );
			return;
		}
		Trainer_Learn( mine );
		if ( mine->clone ) {
			Trainer_Print( ent, "Your training bot is the clone: it plays like the player it learned from." );
			return;
		}
		if ( mine->policy >= 0 ) {
			Trainer_Print( ent, "Your training bot is the learned one: it learned from fights against bots, not against you." );
			return;
		}
		Trainer_Print( ent, va( "Training bot, level %d. Health dealt minus taken per try, as it learned it against you:", mine->level ) );
		for ( i = 0; i < TA_NUM; i++ )
			Trainer_Print( ent, va( "  %-10s %6.1f  (%d tries)", trainerApproachNames[i], mine->value[i], mine->tries[i] ) );
		return;
	}
	if ( !Q_stricmp( arg, "off" ) ) {
		if ( mine )
			mine->drop = qtrue;
		else
			Trainer_Print( ent, "You have no training bot." );
		return;
	}

	if ( vs_allowTrainer.integer <= 0 ) {
		Trainer_Print( ent, "Training bots are off on this server." );
		return;
	}
	if ( level.gametype != GT_FFA ) {
		Trainer_Print( ent, "Training bots only work in free for all." );
		return;
	}
	if ( !trap->Cvar_VariableIntegerValue( "bot_enable" ) ) {
		Trainer_Print( ent, "Training bots need bot_enable 1 on the server." );
		return;
	}

	lvl = arg[0] ? Trainer_ParseLevel( arg ) : 3;
	if ( (lvl & ~TRAINER_HOP) < 1 || (lvl & ~TRAINER_HOP) > TRAINER_POLICY ) {
		Trainer_Print( ent, "Usage: vs_trainer [1-5|1h-5h|clone|ai|off|info], h: one that hops" );
		return;
	}
	if ( !Trainer_LevelReady( lvl ) ) {
		Trainer_Print( ent, lvl == TRAINER_CLONE ? "There is no clone on this server." : "There is no learned bot on this server." );
		return;
	}

	if ( mine ) {
		Trainer_SetLevel( mine, lvl );
		Trainer_SetKey( mine - trainers, self, lvl );
		Trainer_Print( ent, mine->clone ? "Your training bot is the clone now." : mine->policy >= 0 ? "Your training bot is the learned one now."
			: va( "Your training bot is on level %d%s now.", lvl & ~TRAINER_HOP, ( lvl & TRAINER_HOP ) ? ", hopping" : "" ) );
		return;
	}
	if ( count >= vs_allowTrainer.integer ) {
		Trainer_Print( ent, va( "There are already %d training bots on this server.", count ) );
		return;
	}

	Com_sprintf( overrides, sizeof( overrides ), TRAINER_USERINFO "\\vs_trainer\\%d %d", self, lvl );
	bot = G_AddBot( TRAINER_BOT, 5.0f, "", 0, TRAINER_NAME, overrides );
	if ( bot < 0 || !Trainer_Get( bot ) ) {
		Trainer_Print( ent, "No training bot could join (no free slot?)." );
		return;
	}
	Trainer_Print( ent, lvl == TRAINER_CLONE ? "Your training bot is here, the clone. vs_trainer off sends it away."
		: lvl == TRAINER_POLICY ? "Your training bot is here, the learned one. vs_trainer off sends it away."
		: va( "Your training bot is here, level %d%s. vs_trainer off sends it away.", lvl & ~TRAINER_HOP, ( lvl & TRAINER_HOP ) ? ", hopping" : "" ) );
}
