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
it keeps TRAINER_RING_NEAR to TRAINER_RING_FAR off and circles, against a swing it steps to a side and swings into
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
the TRAINER_TRY_MSEC after it began, the scores are kept as a moving average, and the next approach is the best one
with a bonus for the ones tried less and some chance by level. "vs_trainer info" shows the scores. They are gone
with the map.

A fourth recorded fight (7.5 minutes, the bot dealt 131 a minute and took 160, it won 9 of 20 kills) showed where
the player puts his hook: 279 of 295 times into the floor, median 76 units short of the bot at its feet and a little
to a side, looking 42 degrees down, the bot hooked into his body and was pulled into his saber. After 79 of 117
missed kicks he hooked the floor at once (median 100 ms later), 186 units off, behind him or to a side as often as
past the bot, held it 200 ms and was 115 units off again for the next kick. The bot does both now.

Owner and level are a userinfo key of the bot ("vs_trainer" "<owner> <level>"), so a bot that stays over a map
change is a trainer again. It leaves when its player leaves or sends it away.

Not from JA+.
*/

#include "game/g_local.h"
#include "game/ai_main.h"

qboolean PM_SaberInStart( int move );					// bg_panimate.c
qboolean PM_SaberInReturn( int move );
qboolean PM_InKnockDown( playerState_t *ps );
qboolean BG_InKnockDownOnGround( playerState_t *ps );

#define TRAINER_LEVELS			5
#define TRAINER_BOT				"Jedi Trainer"	// botfiles/bots.txt of the game
#define TRAINER_NAME			"Trainer"
#define TRAINER_USERINFO		"\\saber1\\single_1\\saber2\\none"	// the staff of the profile has no red style
// levitation 3 for the flip kick, saber offense and defense 3 for the red style (rank 7, light side)
#define TRAINER_FORCEPOWERS		"7-1-030000000000000330"

#define TRAINER_HOOK_MIN		250.0f	// further away a long pull brings it in
#define TRAINER_HOOK_MAX		1200.0f
#define TRAINER_HOOK_NEAR		200.0f	// a long pull is let go here
#define TRAINER_HOOK_FUEL		20		// JP_HOOK_MIN_FUEL is 10, a bit more so the rope does not run dry halfway
#define TRAINER_HOOK_TIME		1600	// msec on a long pull at most
#define TRAINER_YANK_FROM		220.0f	// in the air this close: the short pull (the player: up to 465, mostly 160)
#define TRAINER_YANK_CHAIN		230.0f	// during the flip back after a kick from this close
#define TRAINER_YANK_TO			105.0f	// let go here
#define TRAINER_YANK_MSEC		60		// or after this long on the rope
#define TRAINER_YANK_MISS		350		// the hook hit nothing
#define TRAINER_YANK_ABOVE		10.0f	// only this far above the player: the pull goes down in front of him
#define TRAINER_YANK_RISE		250.0f	// and not rising fast any more
#define TRAINER_YANK_LEAD		0.12f	// sec: let go where the speed of the pull would bring it in this time
#define TRAINER_CHAIN_WAIT		100		// msec after a kick before the hook pulls it in again
#define TRAINER_HOOK_SHORT		76.0f	// the hook goes into the floor this far in front of the player, at his feet
#define TRAINER_HOOK_SIDE		40.0f	// and up to this far to a side
#define TRAINER_ESCAPE_LEN		186.0f	// after a missed kick a hook into the floor this far off (the player: 106 to 318)
#define TRAINER_ESCAPE_HOLD		200		// msec on it (the player: 150 to 200)
#define TRAINER_ESCAPE_AIM		10.0f	// degrees off at most when that hook goes out
#define TRAINER_ESCAPE_WAIT		250		// msec it may take to turn there
#define TRAINER_HOP_RANGE		230.0f	// jumps here to hook from the air
#define TRAINER_ATTACK_RANGE	260.0f	// runs in for a kick or a swing from here
#define TRAINER_TAKEOFF			55.0f	// the kick jump from the ground, flat distance, and
#define TRAINER_TAKEOFF_SPEED	0.15f	// the speed times this further (the player: 75 to 106 at 250 to 466)
#define TRAINER_TAKEOFF_MAX		130.0f
#define TRAINER_TOO_CLOSE		35.0f	// nearer than this flat, or on top of him: a step back first
#define TRAINER_LEVEL_DZ		32.0f	// about the same height
#define TRAINER_JUMP_HOLD		100		// msec the kick jump is held, the force jump keeps the speed up
#define TRAINER_JUMP_GAP		50		// msec it is let go before the press that kicks (PMF_JUMP_HELD)
#define TRAINER_KICK_TIME		2500
#define TRAINER_SIDE_RANGE		140.0f	// strafes in for a side kick from here
#define TRAINER_SIDE_JUMP		40.0f	// and jumps here: the trace of PM_CheckJump reaches 16 units past its box
#define TRAINER_SIDE_LEVEL		16.0f	// the player at about the same height
#define TRAINER_SIDE_AIM		25.0f	// degrees off 90 at most when it jumps
#define TRAINER_SIDE_TIME		2000
#define TRAINER_GRAB_RANGE		70.0f	// a grab is started from here
#define TRAINER_GRAB_REACH		46.0f	// the hand reaches the player
#define TRAINER_GRAB_TIME		3000
#define TRAINER_SWING_RANGE		90.0f
#define TRAINER_SWING_TIME		1500
#define TRAINER_RED_WAIT		800		// msec it waits for the red style before it swings in the style it has
#define TRAINER_STRIKE_RANGE	120.0f	// the attack button of the player counts as an attack this close
#define TRAINER_RING_NEAR		110.0f	// between attacks it keeps this far off and circles
#define TRAINER_RING_FAR		220.0f
#define TRAINER_PUNISH_RANGE	100.0f	// swings at a player who is open from here, without the pause
#define TRAINER_KICK_BACK		110.0f	// after stepping out of his hook or kick it kicks back from here
#define TRAINER_MEET_RANGE		135.0f	// a player coming through the air is met with a kick from this flat distance
#define TRAINER_MEET_BELOW		10.0f	// if he is not further below than this
#define TRAINER_PRESS_SEC		0.15f	// from the kick jump to the press that kicks (TRAINER_JUMP_HOLD + TRAINER_JUMP_GAP)
#define TRAINER_REACH_NEAR		28.0f	// at the press the player is this far off (flat): the kick trace hits him
#define TRAINER_REACH_FAR		70.0f	// (32 units past the box, the player kicked at 30 to 68)
#define TRAINER_ARRIVE_SEC		0.3f	// a player in the air who reaches this in this time is waited for, not dodged
#define TRAINER_WAIT_MSEC		1500	// the approach TA_WAIT: in the ring for the player to come
#define TRAINER_TRY_MSEC		2500	// an approach is scored on the damage of this time after it began
#define TRAINER_LEARN_RATE		0.25f	// weight of the newest score in the moving average
#define TRAINER_EXPLORE			15.0f	// bonus (in health) for approaches tried less
#define TRAINER_CIRCLE_MIN		700		// msec before it may circle the other way
#define TRAINER_CIRCLE_MAX		1600
#define TRAINER_COUNTER_KICK	220.0f	// a player jumping at it from here is a flip kick
#define TRAINER_COUNTER_GRAB	110.0f

typedef struct {
	int		reactMs;		// from the first sign of an attack until the trainer reacts, half of it for a counter
	float	turnSpeed;		// degrees a second the view turns
	int		readChance;		// percent: an attack of the player is seen at all
	int		counterChance;	// percent: a hook, flip kick or grab of the player is answered
	int		pauseMs;		// between two attacks of the trainer
	float	leadSec;		// aims where the player will be this much later
	float	chance;			// random part (in health) when it picks an approach, less is more of what works
} trainerLevel_t;

static const trainerLevel_t trainerLevels[TRAINER_LEVELS] = {
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

static const char *trainerApproachNames[TA_NUM] = { "hook kick", "run kick", "side kick", "red swing", "wait" };
static const float trainerApproachPrior[TA_NUM] = { 2.0f, 0.0f, 0.0f, -2.0f, 0.0f };	// hook kicks did best in the logs

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
	int				circle;		// 1 circles to the right, -1 to the left
	int				circleTime;	// level.time it may turn round
} trainer_t;

static trainer_t trainers[MAX_CLIENTS];

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
			t->level = Com_Clampi( 1, TRAINER_LEVELS, lvl );
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
		t->hookSide = Q_flrand( -TRAINER_HOOK_SIDE, TRAINER_HOOK_SIDE );
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
	return (qboolean)( dist < TRAINER_STRIKE_RANGE && (buttons & (BUTTON_ATTACK | BUTTON_ALT_ATTACK)) != 0 );
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
	VectorMA( e, Q_min( TRAINER_HOOK_SHORT, flat * 0.5f ), dir, from );
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
		VectorMA( self->client->ps.origin, TRAINER_ESCAPE_LEN, dir, from );
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
	if ( ps->torsoAnim == BOTH_KYLE_GRAB && dist < TRAINER_COUNTER_GRAB )
		return TT_GRAB;
	if ( ps->groundEntityNum == ENTITYNUM_NONE && dist < TRAINER_COUNTER_KICK ) {
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
		&& client->ps.jetpackFuel >= TRAINER_HOOK_FUEL && client->pers.jp.hookTime + jp_hookFloodProtect.integer < level.time );
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

// the saber on and in the red style, as far as it has one: one key at a time, the way a player presses it
static void Trainer_Saber( gentity_t *self, trainer_t *t ) {
	const playerState_t *ps = &self->client->ps;

	if ( ps->weapon != WP_SABER || ps->weaponTime > 0 || t->styleTime > level.time )
		return;
	if ( ps->saberHolstered ) {
		t->styleTime = level.time + 500;
		Cmd_ToggleSaber_f( self );
	}
	else if ( ps->fd.saberAnimLevel != SS_STRONG ) {
		t->styleTime = level.time + 400;
		Cmd_SaberAttackCycle_f( self );
	}
}

static qboolean Trainer_SaberReady( const playerState_t *ps ) {
	return (qboolean)( ps->weapon == WP_SABER && !ps->saberHolstered && ps->weaponTime <= 0 );
}

static void Trainer_Learn( trainer_t *t ) {
	int i;

	if ( !t->learned ) {
		for ( i = 0; i < TA_NUM; i++ )
			t->value[i] = trainerApproachPrior[i];
		t->learned = qtrue;
	}
}

// the score of the approach that ran: what it dealt minus what it took, after TRAINER_TRY_MSEC or when the next
// one begins (now)
static void Trainer_Score( trainer_t *t, qboolean now ) {
	if ( t->approach < 0 || (!now && level.time - t->approachTime < TRAINER_TRY_MSEC) )
		return;
	Trainer_Learn( t );
	t->value[t->approach] += TRAINER_LEARN_RATE * ( (float)( t->dealt - t->taken ) - t->value[t->approach] );
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

	if ( dist >= TRAINER_HOOK_MIN && dist <= TRAINER_HOOK_MAX && Trainer_CanHook( self ) && Trainer_Visible( bs, enemy ) ) {
		Trainer_SetState( t, TS_HOOK );		// far off: the long pull, not scored
		return;
	}
	if ( dist <= TRAINER_GRAB_RANGE && onGround && g_debugMelee.integer && (ps->stats[STAT_WEAPONS] & (1 << WP_MELEE))
		&& !Q_irand( 0, 4 ) ) {
		Trainer_SetState( t, TS_GRAB );
		return;
	}
	if ( dist > TRAINER_ATTACK_RANGE )
		return;

	fits[TA_HOOKKICK] = canKick && dist <= TRAINER_HOP_RANGE && Trainer_CanHook( self );
	fits[TA_RUNKICK] = canKick;
	fits[TA_SIDEKICK] = canKick && onGround && dist <= TRAINER_SIDE_RANGE
		&& fabsf( enemy->client->ps.origin[2] - ps->origin[2] ) < TRAINER_SIDE_LEVEL;
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
		score = t->value[i] + TRAINER_EXPLORE * sqrtf( logf( (float)total + 1.0f ) / ( (float)t->tries[i] + 1.0f ) )
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
		t->nextAttack = level.time + TRAINER_WAIT_MSEC;	// stays in the ring, his attack is countered
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
		t->circleTime = level.time + Q_irand( TRAINER_CIRCLE_MIN, TRAINER_CIRCLE_MAX );
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
				&& ps->fd.forcePowerLevel[FP_LEVITATION] > FORCE_LEVEL_1 && flat >= TRAINER_TOO_CLOSE && flat <= TRAINER_MEET_RANGE;
			float			above;
			const float		atPress = Trainer_FlatIn( self, enemy, TRAINER_PRESS_SEC, &above );

			if ( canMeet && atPress >= TRAINER_REACH_NEAR && atPress <= TRAINER_REACH_FAR && above >= -TRAINER_MEET_BELOW ) {
				Trainer_SetState( t, TS_KICK );
				Trainer_Step( t, KS_HOLD );
				cmd->forwardmove = 127;
				cmd->upmove = 127;
			}
			else if ( canMeet && atPress > TRAINER_REACH_FAR
				&& Trainer_FlatIn( self, enemy, TRAINER_ARRIVE_SEC, NULL ) <= TRAINER_REACH_FAR ) {
				// he arrives in a moment: stay in front of him
			}
			else {
				vec3_t dir, angles;

				VectorSubtract( ps->origin, enemy->client->ps.origin, dir );
				vectoangles( dir, angles );
				// it is to the left of where he looks: further to his left, which is its right
				t->circle = ( AngleSubtract( angles[YAW], enemy->client->ps.viewangles[YAW] ) >= 0.0f ) ? 1 : -1;
				t->circleTime = level.time + TRAINER_CIRCLE_MIN;
				cmd->rightmove = t->circle * 127;
			}
		}
		break;
	case TT_GRAB:
		// out of reach of the hand, the grab breaks off past 64 units
		cmd->forwardmove = -127;
		if ( dist < TRAINER_GRAB_RANGE && Trainer_SaberReady( ps ) )
			cmd->buttons |= BUTTON_ATTACK;
		break;
	default:
		break;
	}
}

// the long pull from afar, let go near the player for the kick (Trainer_LetGo)
static void Trainer_Hook( trainer_t *t, const gentity_t *self, float dist, usercmd_t *cmd ) {
	if ( dist <= TRAINER_HOOK_NEAR ) {
		Trainer_SetState( t, TS_KICK );
		return;
	}
	if ( level.time - t->stateTime > TRAINER_HOOK_TIME || (level.time - t->stateTime > 400 && !self->client->hook) ) {
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

	return (qboolean)( ps->groundEntityNum == ENTITYNUM_NONE && dist <= from && dist > TRAINER_YANK_TO
		&& dz <= -TRAINER_YANK_ABOVE && ps->velocity[2] < TRAINER_YANK_RISE && Trainer_CanHook( self ) );
}

// The kick jump now: the player will be in reach at the press (both carried on for TRAINER_PRESS_SEC), or, while
// he stands still, the flat distance by its own speed (the player took off at 75 to 106 at 250 to 466).
static qboolean Trainer_TakeOff( const gentity_t *self, const gentity_t *enemy, float flat, float speed ) {
	const float atPress = Trainer_FlatIn( self, enemy, TRAINER_PRESS_SEC, NULL );

	if ( atPress >= TRAINER_REACH_NEAR && atPress <= TRAINER_REACH_FAR )
		return qtrue;
	return (qboolean)( atPress > TRAINER_REACH_FAR && flat <= Q_min( TRAINER_TAKEOFF + speed * TRAINER_TAKEOFF_SPEED, TRAINER_TAKEOFF_MAX )
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
	if ( level.time - t->stateTime > TRAINER_KICK_TIME ) {
		Trainer_Done( t );
		return;
	}

	switch ( t->step ) {
	case KS_RUN:
		if ( !onGround ) {
			if ( !t->noHop && Trainer_YankNow( self, dist, dz, TRAINER_YANK_FROM ) )
				Trainer_Yank( t, cmd );
		}
		else if ( flat < TRAINER_TOO_CLOSE || dz < -TRAINER_LEVEL_DZ )
			cmd->forwardmove = -127;		// right at him or on top of him: no room for the kick
		else if ( dz < TRAINER_LEVEL_DZ && Trainer_TakeOff( self, enemy, flat, speed ) ) {
			cmd->upmove = 127;
			Trainer_Step( t, KS_HOLD );
		}
		else if ( !t->noHop && dist <= TRAINER_HOP_RANGE && Trainer_CanHook( self ) ) {
			cmd->upmove = 127;
			Trainer_Step( t, KS_HOP );
		}
		else if ( dist > TRAINER_ATTACK_RANGE * 1.5f )
			Trainer_Done( t );
		break;
	case KS_HOP:
		if ( Trainer_YankNow( self, dist, dz, TRAINER_YANK_FROM ) )
			Trainer_Yank( t, cmd );
		else if ( onGround && msec > 150 )
			Trainer_Step( t, KS_RUN );
		break;
	case KS_YANK:
		if ( self->client->pers.jp.hooked && !t->hookedTime )
			t->hookedTime = level.time;
		// let go: without the hook button Trainer_LetGo presses use
		if ( onGround || dist - speed * TRAINER_YANK_LEAD <= TRAINER_YANK_TO
			|| (t->hookedTime && level.time - t->hookedTime >= TRAINER_YANK_MSEC)
			|| (!t->hookedTime && msec > TRAINER_YANK_MISS) )
			Trainer_Step( t, KS_RUN );
		else
			cmd->buttons |= BUTTON_HOOK;
		break;
	case KS_HOLD:
		if ( msec >= TRAINER_JUMP_HOLD )
			Trainer_Step( t, KS_GAP );		// this command lets go
		else
			cmd->upmove = 127;
		break;
	case KS_GAP:
		if ( onGround && msec > 100 )
			Trainer_Step( t, KS_RUN );		// down again without a kick
		else if ( msec >= TRAINER_JUMP_GAP ) {
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
		else if ( msec >= TRAINER_CHAIN_WAIT && t->chain <= t->level / 2 && Trainer_YankNow( self, dist, dz, TRAINER_YANK_CHAIN ) ) {
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
			if ( aimOff <= TRAINER_ESCAPE_AIM || msec >= TRAINER_ESCAPE_WAIT ) {
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
		else if ( level.time - t->hookedTime < TRAINER_ESCAPE_HOLD && self->client->hook )
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
		if ( ps->groundEntityNum != ENTITYNUM_NONE && dist <= TRAINER_SIDE_JUMP && fabsf( yawLeft ) < TRAINER_SIDE_AIM ) {
			cmd->upmove = 127;
			t->step = 1;
			t->stepTime = level.time;
		}
		else if ( level.time - t->stateTime > TRAINER_SIDE_TIME || dist > TRAINER_SIDE_RANGE * 1.3f )
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
		cmd->forwardmove = (dist > TRAINER_GRAB_REACH) ? 127 : 0;
		if ( ps->weapon == WP_MELEE && ps->weaponTime <= 0 && dist <= TRAINER_GRAB_REACH && ps->groundEntityNum != ENTITYNUM_NONE ) {
			cmd->buttons |= BUTTON_ATTACK | BUTTON_ALT_ATTACK;
			t->step = 1;
			t->stepTime = level.time;
		}
		else if ( level.time - t->stateTime > TRAINER_GRAB_TIME / 2 || dist > TRAINER_ATTACK_RANGE )
			Trainer_Done( t );
		return;
	}
	// forward when the hand gets the player is the punch (G_GrabSomeMofos), out of the grab comes the flip kick
	cmd->forwardmove = 127;
	if ( !BG_InGrappleMove( ps->torsoAnim ) && level.time - t->stepTime > 300 )
		Trainer_SetState( t, TS_KICK );
	else if ( level.time - t->stepTime > TRAINER_GRAB_TIME )
		Trainer_Done( t );
}

// red style, forward and attack: the swing from above (LS_A_T2B)
static void Trainer_Swing( trainer_t *t, gentity_t *self, float dist, usercmd_t *cmd ) {
	const playerState_t *ps = &self->client->ps;

	Trainer_Saber( self, t );
	cmd->forwardmove = 127;
	if ( t->step == 0 ) {
		if ( dist <= TRAINER_SWING_RANGE && Trainer_SaberReady( ps )
			&& (ps->fd.saberAnimLevel == SS_STRONG || level.time - t->stateTime > TRAINER_RED_WAIT) ) {
			cmd->buttons |= BUTTON_ATTACK;
			t->step = 1;
			t->stepTime = level.time;
		}
		else if ( level.time - t->stateTime > TRAINER_SWING_TIME )
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
	lvl = &trainerLevels[t->level - 1];

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
		else if ( t->state == TS_COUNTER && dist <= TRAINER_KICK_BACK && JP_FEATURE( JPF_FLIPKICK ) )
			Trainer_SetState( t, TS_KICK );		// out of his hook or kick: kicks back at once
		else if ( t->state == TS_COUNTER || t->state == TS_GUARD )
			Trainer_Done( t );
		if ( t->state == TS_CHASE && open && dist <= TRAINER_PUNISH_RANGE && threat == TT_NONE )
			Trainer_SetState( t, TS_SWING );		// no pause for a player who can not defend himself
		else if ( t->state == TS_CHASE && level.time >= t->nextAttack )
			Trainer_Choose( t, bs, self, enemy, dist );
	}

	switch ( t->state ) {
	case TS_GUARD:
		// to a side out of the line of the swing, the saber blocks by itself while it faces the player. Backing off
		// straight was followed and hit in the recorded fight. When the swing returns it is open (punish above).
		Trainer_Saber( self, t );
		Trainer_Circle( t, cmd );
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
		if ( dist > TRAINER_RING_FAR )
			cmd->forwardmove = 127;
		else {
			if ( dist < TRAINER_RING_NEAR )
				cmd->forwardmove = -127;
			Trainer_Circle( t, cmd );
		}
		break;
	}
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

// from WP_InitForcePowers: the powers of a training bot, whatever its profile has
void G_TrainerForcePowers( int clientNum, char *forcePowers, size_t size ) {
	if ( Trainer_Get( clientNum ) )
		Q_strncpyz( forcePowers, TRAINER_FORCEPOWERS, size );
}

// from G_Damage, after the damage is taken: what the approach that runs dealt to the player and took from him
void G_TrainerDamage( const gentity_t *targ, const gentity_t *attacker, int take ) {
	int i;

	if ( take <= 0 || !attacker )
		return;
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
	memset( &trainers[clientNum], 0, sizeof( trainers[0] ) );
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( trainers[i].active && trainers[i].owner == clientNum )
			trainers[i].drop = qtrue;
	}
}

// every frame: trainers without their player, or which the server does not allow any more, leave
void G_TrainerFrame( void ) {
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		const gentity_t *ent = &g_entities[i];
		trainer_t *t;

		if ( !ent->inuse || !ent->client || ent->client->pers.connected == CON_DISCONNECTED || !(t = Trainer_Get( i )) )
			continue;
		if ( t->drop || !Trainer_Owner( t ) || vs_allowTrainer.integer <= 0 || level.gametype != GT_FFA )
			trap->DropClient( i, "left the training" );
	}
}

static void Trainer_Print( const gentity_t *ent, const char *text ) {
	trap->SendServerCommand( ent - g_entities, va( "print \"%s\n\"", text ) );
}

/*
=================
Cmd_Trainer_f

vs_trainer [1-5]: calls the training bot of the player, or sets its level (3 without a number)
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

	lvl = arg[0] ? atoi( arg ) : 3;
	if ( lvl < 1 || lvl > TRAINER_LEVELS ) {
		Trainer_Print( ent, "Usage: vs_trainer [1-5|off|info]" );
		return;
	}

	if ( mine ) {
		mine->level = lvl;
		Trainer_SetKey( mine - trainers, self, lvl );
		Trainer_Print( ent, va( "Your training bot is on level %d now.", lvl ) );
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
	Trainer_Print( ent, va( "Your training bot is here, level %d. vs_trainer off sends it away.", lvl ) );
}
