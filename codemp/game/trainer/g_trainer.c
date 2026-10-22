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

Owner and level are a userinfo key of the bot ("vs_trainer" "<owner> <level>"), so a bot that stays over a map
change is a trainer again. It leaves when its player leaves or sends it away.

Not from JA+.
*/

#include "game/g_local.h"
#include "game/ai_main.h"

qboolean PM_SaberInStart( int move );					// bg_panimate.c
qboolean BG_InKnockDownOnGround( playerState_t *ps );

#define TRAINER_LEVELS			5
#define TRAINER_BOT				"Jedi Trainer"	// botfiles/bots.txt of the game
#define TRAINER_NAME			"Trainer"
#define TRAINER_USERINFO		"\\saber1\\single_1\\saber2\\none"	// the staff of the profile has no red style
// levitation 3 for the flip kick, saber offense and defense 3 for the red style (rank 7, light side)
#define TRAINER_FORCEPOWERS		"7-1-030000000000000330"

#define TRAINER_HOOK_MIN		300.0f	// further away the hook pulls it in
#define TRAINER_HOOK_MAX		1200.0f
#define TRAINER_HOOK_LETGO		110.0f	// close enough: let go and kick
#define TRAINER_HOOK_FUEL		20		// JP_HOOK_MIN_FUEL is 10, a bit more so the rope does not run dry halfway
#define TRAINER_HOOK_TIME		1600	// msec on the rope at most
#define TRAINER_ATTACK_RANGE	260.0f	// runs in for a flip kick or a swing from here
#define TRAINER_KICK_JUMP		76.0f	// jumps here: the kick trace of PM_CheckJump reaches 32 units past its box
#define TRAINER_KICK_TIME		2000
#define TRAINER_GRAB_RANGE		70.0f	// a grab is started from here
#define TRAINER_GRAB_REACH		46.0f	// the hand reaches the player
#define TRAINER_GRAB_TIME		3000
#define TRAINER_SWING_RANGE		90.0f
#define TRAINER_SWING_TIME		1500
#define TRAINER_RED_WAIT		800		// msec it waits for the red style before it swings in the style it has
#define TRAINER_GUARD_RANGE		120.0f	// backs off from an attacking player closer than this
#define TRAINER_COUNTER_KICK	220.0f	// a player jumping at it from here is a flip kick
#define TRAINER_COUNTER_GRAB	110.0f
#define TRAINER_COUNTER_SWING	160.0f	// swings at a player who comes in on the hook

typedef struct {
	int		reactMs;		// from the first sign of an attack until the trainer reacts, half of it for a counter
	float	turnSpeed;		// degrees a second the view turns
	int		readChance;		// percent: an attack of the player is seen at all
	int		counterChance;	// percent: a hook, flip kick or grab of the player is answered
	int		pauseMs;		// between two attacks of the trainer
} trainerLevel_t;

static const trainerLevel_t trainerLevels[TRAINER_LEVELS] = {
	{ 450, 220.0f, 45, 20, 2600 },
	{ 330, 340.0f, 60, 40, 1900 },
	{ 230, 480.0f, 75, 60, 1300 },
	{ 150, 650.0f, 88, 80,  850 },
	{  80, 900.0f, 97, 95,  500 },
};

typedef enum {
	TS_CHASE,		// runs to the player, nothing started
	TS_GUARD,		// the player attacks: back off and block
	TS_COUNTER,		// answers a hook, flip kick or grab of the player
	TS_HOOK,		// on the rope to the player
	TS_KICK,		// jumps and flip kicks off the player
	TS_GRAB,		// melee grab with a punch, then a flip kick
	TS_SWING		// red style, from above
} trainerState_t;

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
	int				nextAttack;
	int				styleTime;	// no saber key before this
	trainerSign_t	attack;		// the player attacks
	trainerSign_t	threat;		// a trainerThreat_t of the player
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
	}
}

// an attack is over: back to running after the player, the next one after a pause of its level
static void Trainer_Done( trainer_t *t ) {
	const int pause = trainerLevels[t->level - 1].pauseMs;

	Trainer_SetState( t, TS_CHASE );
	t->nextAttack = level.time + Q_irand( pause * 7 / 10, pause * 13 / 10 );
}

// the player swings, kicks, shoots or is about to (the attack button is down)
static qboolean Trainer_Attacking( const gentity_t *enemy ) {
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
	return (qboolean)( (buttons & (BUTTON_ATTACK | BUTTON_ALT_ATTACK)) != 0 );
}

static trainerThreat_t Trainer_Threat( const gentity_t *self, const gentity_t *enemy, float dist ) {
	const playerState_t *ps = &enemy->client->ps;
	vec3_t toSelf;

	if ( enemy->client->hook && enemy->client->hook->enemy == self )
		return TT_HOOK;
	if ( ps->torsoAnim == BOTH_KYLE_GRAB && dist < TRAINER_COUNTER_GRAB )
		return TT_GRAB;
	if ( ps->groundEntityNum == ENTITYNUM_NONE && ps->velocity[2] > 0.0f && dist < TRAINER_COUNTER_KICK ) {
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

// turns the view at most maxTurn degrees toward target (NULL: keeps it) and puts it into the command
static void Trainer_Aim( bot_state_t *bs, const vec3_t target, float maxTurn, usercmd_t *cmd ) {
	vec3_t dir, want;
	int i;

	if ( target ) {
		VectorSubtract( target, bs->eye, dir );
		vectoangles( dir, want );
	}
	for ( i = 0; i < 3; i++ ) {
		const float delta = SHORT2ANGLE( bs->cur_ps.delta_angles[i] );
		float view = AngleMod( bs->viewangles[i] + delta );

		if ( target )
			view = AngleMod( view + Com_Clamp( -maxTurn, maxTurn, AngleSubtract( want[i], view ) ) );
		bs->viewangles[i] = AngleMod( view - delta );
		cmd->angles[i] = ANGLE2SHORT( view ) - bs->cur_ps.delta_angles[i];
	}
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

// what the next attack is, when there is one to start
static void Trainer_Choose( trainer_t *t, const bot_state_t *bs, const gentity_t *self, const gentity_t *enemy, float dist ) {
	const playerState_t *ps = &self->client->ps;
	const qboolean onGround = ps->groundEntityNum != ENTITYNUM_NONE && enemy->client->ps.groundEntityNum != ENTITYNUM_NONE;
	const qboolean canKick = JP_FEATURE( JPF_FLIPKICK ) && ps->fd.forcePowerLevel[FP_LEVITATION] > FORCE_LEVEL_1;

	if ( dist >= TRAINER_HOOK_MIN && dist <= TRAINER_HOOK_MAX && jp_allowHook.integer == 1
		&& ps->jetpackFuel >= TRAINER_HOOK_FUEL && Trainer_Visible( bs, enemy ) )
		Trainer_SetState( t, TS_HOOK );
	else if ( dist <= TRAINER_GRAB_RANGE && onGround && g_debugMelee.integer && (ps->stats[STAT_WEAPONS] & (1 << WP_MELEE))
		&& !Q_irand( 0, 2 ) )
		Trainer_SetState( t, TS_GRAB );
	else if ( dist <= TRAINER_ATTACK_RANGE )
		Trainer_SetState( t, (canKick && Q_irand( 0, 4 ) < 3) ? TS_KICK : TS_SWING );
}

// in the middle of something that can not be stopped: in the air for a kick or holding the player
static qboolean Trainer_Busy( const trainer_t *t ) {
	return (qboolean)( (t->state == TS_KICK && t->step > 0) || (t->state == TS_GRAB && t->step > 0) );
}

static void Trainer_Counter( gentity_t *self, trainer_t *t, trainerThreat_t threat, float dist, usercmd_t *cmd ) {
	const playerState_t *ps = &self->client->ps;

	Trainer_Saber( self, t );
	switch ( threat ) {
	case TT_HOOK:
		// out of the line of the rope, and a swing when the player arrives: the blade cuts the rope
		cmd->rightmove = ((level.time / 700) & 1) ? 127 : -127;
		if ( dist < TRAINER_COUNTER_SWING && Trainer_SaberReady( ps ) )
			cmd->buttons |= BUTTON_ATTACK;
		break;
	case TT_KICK:
		// swinging in the red style the kick does not knock down (g_active.c), crouching it does not stun
		if ( ps->fd.saberAnimLevel == SS_STRONG && Trainer_SaberReady( ps ) ) {
			cmd->forwardmove = 127;
			cmd->buttons |= BUTTON_ATTACK;
		}
		else
			cmd->upmove = -127;
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

static void Trainer_Hook( trainer_t *t, const gentity_t *self, float dist, usercmd_t *cmd ) {
	if ( dist <= TRAINER_HOOK_LETGO ) {
		// no hook button: a client without the JA+ plugin lets go of the rope with it (JP_HookClientThink)
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

/*
=================
Trainer_Kick

The JA+ flip kick (PM_CheckJump): in the air, still going up fast and near the ground, a new press of jump while
moving forward with the player right in front. So: jump, hold it for one command so the force jump keeps the
speed up, then let go and press again every other command until the kick comes.
=================
*/
static void Trainer_Kick( trainer_t *t, const gentity_t *self, const gentity_t *enemy, float dist, usercmd_t *cmd ) {
	const playerState_t *ps = &self->client->ps;
	const qboolean onGround = ps->groundEntityNum != ENTITYNUM_NONE;

	cmd->forwardmove = 127;
	if ( ps->legsAnim == BOTH_WALL_FLIP_BACK1 && t->step > 0 ) {
		// kicked: a player on the ground gets the red swing on top
		if ( enemy->client->ps.forceHandExtend == HANDEXTEND_KNOCKDOWN || BG_InKnockDownOnGround( &enemy->client->ps ) )
			Trainer_SetState( t, TS_SWING );
		else
			Trainer_Done( t );
		return;
	}
	if ( t->step == 0 ) {
		if ( onGround && dist <= TRAINER_KICK_JUMP ) {
			cmd->upmove = 127;
			t->step = 1;
			t->stepTime = level.time;
		}
		else if ( level.time - t->stateTime > TRAINER_KICK_TIME || dist > TRAINER_ATTACK_RANGE * 1.5f )
			Trainer_Done( t );
		return;
	}
	if ( onGround && level.time - t->stepTime > 150 ) {
		Trainer_Done( t );		// landed without a kick
		return;
	}
	cmd->upmove = (t->step & 1) ? 127 : 0;
	t->step++;
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

/*
=================
G_TrainerCmd

from BotUpdateInput, every frame: the usercmd of a training bot. qfalse for any other bot.
=================
*/
qboolean G_TrainerCmd( bot_state_t *bs, int time, int elapsed ) {
	trainer_t				*t = Trainer_Get( bs->client );
	const trainerLevel_t	*lvl;
	gentity_t				*self, *enemy;
	playerState_t			*ps;
	usercmd_t				*cmd = &bs->lastucmd;
	trainerThreat_t			threat;
	float					dist;
	qboolean				attacked;

	if ( !t )
		return qfalse;

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
		Trainer_Aim( bs, NULL, 0.0f, cmd );
		if ( level.framenum & 1 )
			cmd->buttons = BUTTON_ATTACK;		// respawn
		Trainer_SetState( t, TS_CHASE );
		return qtrue;
	}

	enemy = Trainer_Enemy( t );
	if ( !enemy ) {
		Trainer_Aim( bs, NULL, 0.0f, cmd );
		Trainer_SetState( t, TS_CHASE );
		return qtrue;
	}

	dist = Distance( ps->origin, enemy->client->ps.origin );
	Trainer_Aim( bs, enemy->client->ps.origin, lvl->turnSpeed * elapsed / 1000.0f, cmd );

	attacked = Trainer_Notice( &t->attack, Trainer_Attacking( enemy ), lvl->readChance, lvl->reactMs );
	threat = (trainerThreat_t)Trainer_Threat( self, enemy, dist );
	if ( !Trainer_Notice( &t->threat, threat, lvl->counterChance, lvl->reactMs / 2 ) )
		threat = TT_NONE;

	if ( !Trainer_Busy( t ) ) {
		if ( threat != TT_NONE )
			Trainer_SetState( t, TS_COUNTER );
		else if ( attacked )
			Trainer_SetState( t, TS_GUARD );		// no hook, grab or kick into an attack
		else if ( t->state == TS_COUNTER || t->state == TS_GUARD )
			Trainer_Done( t );
		if ( t->state == TS_CHASE && level.time >= t->nextAttack )
			Trainer_Choose( t, bs, self, enemy, dist );
	}

	switch ( t->state ) {
	case TS_GUARD:
		// the saber blocks by itself while it does not attack and faces the player
		Trainer_Saber( self, t );
		if ( dist < TRAINER_GUARD_RANGE )
			cmd->forwardmove = -127;
		if ( t->level >= 3 )
			cmd->rightmove = ((level.time / 600) & 1) ? 127 : -127;
		break;
	case TS_COUNTER:
		Trainer_Counter( self, t, (trainerThreat_t)t->threat.sign, dist, cmd );
		break;
	case TS_HOOK:
		Trainer_Hook( t, self, dist, cmd );
		break;
	case TS_KICK:
		Trainer_Kick( t, self, enemy, dist, cmd );
		break;
	case TS_GRAB:
		Trainer_Grab( t, self, dist, cmd );
		break;
	case TS_SWING:
		Trainer_Swing( t, self, dist, cmd );
		break;
	default:
		Trainer_Saber( self, t );
		if ( dist > TRAINER_SWING_RANGE * 0.7f )
			cmd->forwardmove = 127;
		break;
	}
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
		Trainer_Print( ent, "Usage: vs_trainer [1-5|off]" );
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
