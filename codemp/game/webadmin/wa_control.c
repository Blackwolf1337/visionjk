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

// wa_control.c -- a player who is moved from the page of the web admin panel: /api/control takes him, and
// what he does comes from there until he is let go.

#include "game/webadmin/wa_local.h"

/*
=================
WA_ApiControl, WebAdmin_ClientCommand

Not in JKAdmin. One player at a time, a bot or a human. The page sends what its keys and its mouse say, or a
point to walk to, and ClientThink_real asks here for every command of that player before it runs it: the
moves, the buttons and the view of the command are replaced. A bot goes on thinking and is not listened to.
A human goes on predicting his own keys, so his own view jitters while he is held.

An input counts for WA_CONTROL_HOLD, so a page that went away leaves him standing, and after
WA_CONTROL_RELEASE without a request he is his own again. A page that only sends him to places says its goal
again and again to keep him.

POST /api/control: op start (id), input (id, f, r, u, buttons, yaw, pitch), goal (id, x, y, z) or stop.
The answer names who is held now, -1 for nobody.
=================
*/
#define WA_CONTROL_HOLD		500			// msec an input of the page counts
#define WA_CONTROL_RELEASE	5000		// msec without a request until the player is let go
#define WA_CONTROL_BUTTONS	( BUTTON_ATTACK | BUTTON_USE_HOLDABLE | BUTTON_GESTURE | BUTTON_WALKING | BUTTON_USE \
							| BUTTON_FORCEGRIP | BUTTON_ALT_ATTACK | BUTTON_FORCEPOWER | BUTTON_FORCE_LIGHTNING | BUTTON_FORCE_DRAIN )
#define WA_GOAL_NEAR		24.0f		// this close to the point he has arrived
#define WA_GOAL_CHECK		400			// msec between two looks at whether he gets on
#define WA_GOAL_STEP		8.0f		//	and how far he has to have come in that time
#define WA_GOAL_TRIES		6			// jumps against what is in the way before he gives up

static struct {
	int			client;			// -1: nobody
	int			enterTime;		// of that player: another one in his slot is not him
	int			lastRequest, inputTime;
	signed char	forward, right, up;
	int			buttons;
	vec3_t		angles;
	qboolean	walking;		// to goal
	qboolean	hasGoal;		// goal is one the page gave
	vec3_t		goal, lastSpot;
	int			lastCheck, jumpUntil, tries;
} control = { -1 };

static void WA_ControlResult( waConnection_t *c, qboolean ok, const char *msg ) {
	waBuffer_t b = { 0 };

	WA_BufPrintf( &b, "{\"ok\":%s,\"id\":%d,\"message\":", WA_BOOL( ok ), control.client );
	WA_BufJsonString( &b, msg );
	WA_BufAppend( &b, "}", 1 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

// the player who is held, NULL when he is gone or dead: then nobody is held any more
static gentity_t *WA_ControlHeld( void ) {
	gentity_t *ent;

	if ( control.client < 0 )
		return NULL;
	ent = &g_entities[control.client];
	if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED || ent->client->pers.enterTime != control.enterTime
		|| level.time - control.lastRequest > WA_CONTROL_RELEASE ) {
		control.client = -1;
		return NULL;
	}
	return ent;
}

static int WA_ControlMove( const char *body, const char *key ) {
	char value[16];

	return WA_FormValue( body, key, value, sizeof( value ) ) ? Com_Clampi( -127, 127, atoi( value ) ) : 0;
}

void WA_ApiControl( waConnection_t *c, const char *body ) {
	static const char *const axes[3] = { "x", "y", "z" };
	char		op[16], value[32];
	gentity_t	*ent;
	int			id = -1, k;

	WA_FormValue( body, "op", op, sizeof( op ) );
	if ( WA_FormValue( body, "id", value, sizeof( value ) ) && value[0] )
		id = atoi( value );

	if ( !strcmp( op, "stop" ) ) {
		if ( control.client >= 0 )
			WA_LogUse( c, va( "control stop id %d", control.client ) );
		control.client = -1;
		WA_ControlResult( c, qtrue, "released" );
		return;
	}

	if ( !strcmp( op, "start" ) ) {
		if ( id < 0 || id >= level.maxclients || !g_entities[id].inuse || !g_entities[id].client
			|| g_entities[id].client->pers.connected != CON_CONNECTED ) {
			WA_ControlResult( c, qfalse, "no such player" );
			return;
		}
		ent = &g_entities[id];
		if ( ent->health <= 0 || ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
			WA_ControlResult( c, qfalse, "player is not alive" );
			return;
		}
		WA_LogUse( c, va( "control start id %d", id ) );
		memset( &control, 0, sizeof( control ) );
		control.client = id;
		control.enterTime = ent->client->pers.enterTime;
		control.lastRequest = level.time;
		VectorCopy( ent->client->ps.viewangles, control.angles );
		WA_ControlResult( c, qtrue, "taken" );
		return;
	}

	ent = WA_ControlHeld();
	if ( !ent || id != control.client ) {
		WA_ControlResult( c, qfalse, "this player is not held any more" );
		return;
	}
	control.lastRequest = level.time;

	if ( !strcmp( op, "input" ) ) {
		control.walking = control.hasGoal = qfalse;
		control.inputTime = level.time;
		control.forward = (signed char)WA_ControlMove( body, "f" );
		control.right = (signed char)WA_ControlMove( body, "r" );
		control.up = (signed char)WA_ControlMove( body, "u" );
		control.buttons = WA_FormValue( body, "buttons", value, sizeof( value ) ) ? ( atoi( value ) & WA_CONTROL_BUTTONS ) : 0;
		if ( WA_FormValue( body, "yaw", value, sizeof( value ) ) && value[0] )
			control.angles[YAW] = AngleNormalize180( atof( value ) );
		if ( WA_FormValue( body, "pitch", value, sizeof( value ) ) && value[0] )
			control.angles[PITCH] = Com_Clamp( -89.0f, 89.0f, atof( value ) );
		control.angles[ROLL] = 0.0f;
		WA_ControlResult( c, qtrue, "" );
	} else if ( !strcmp( op, "goal" ) ) {
		vec3_t goal;

		for ( k = 0; k < 3; k++ ) {
			if ( !WA_FormValue( body, axes[k], value, sizeof( value ) ) ) {
				WA_ControlResult( c, qfalse, "no place to walk to" );
				return;
			}
			goal[k] = Com_Clamp( -MAX_WORLD_COORD, MAX_WORLD_COORD, atof( value ) );
		}
		// The page says its goal again every two seconds, which is what keeps him held while it sends no keys.
		// The same place again is nothing new: he does not set out anew for a point he reached or gave up on.
		if ( !control.hasGoal || !VectorCompare( goal, control.goal ) ) {
			VectorCopy( goal, control.goal );
			control.hasGoal = control.walking = qtrue;
			control.tries = 0;
			control.jumpUntil = 0;
			control.lastCheck = level.time;
			VectorCopy( ent->client->ps.origin, control.lastSpot );
		}
		WA_ControlResult( c, qtrue, control.walking ? "walking there" : "" );
	} else {
		WA_ControlResult( c, qfalse, "unknown op" );
	}
}

// Straight to the point, looking where he goes. He is at it, or what is in the way does not give after a few
// jumps: he stands.
static void WA_ControlWalk( const gentity_t *ent, usercmd_t *ucmd ) {
	vec3_t	to;

	VectorSubtract( control.goal, ent->client->ps.origin, to );
	to[2] = 0.0f;
	if ( VectorLength( to ) < WA_GOAL_NEAR ) {
		control.walking = qfalse;
		return;
	}
	if ( level.time - control.lastCheck >= WA_GOAL_CHECK ) {
		vec3_t came;

		VectorSubtract( ent->client->ps.origin, control.lastSpot, came );
		came[2] = 0.0f;
		if ( VectorLength( came ) >= WA_GOAL_STEP ) {
			control.tries = 0;
		} else if ( ++control.tries > WA_GOAL_TRIES ) {
			control.walking = qfalse;
			return;
		} else {
			control.jumpUntil = level.time + 200;
		}
		control.lastCheck = level.time;
		VectorCopy( ent->client->ps.origin, control.lastSpot );
	}
	VectorSet( control.angles, 0.0f, vectoyaw( to ), 0.0f );
	ucmd->forwardmove = 127;
	if ( level.time < control.jumpUntil )
		ucmd->upmove = 127;
}

void WebAdmin_ClientCommand( gentity_t *ent, usercmd_t *ucmd ) {
	int i;

	if ( control.client != (int)( ent-g_entities ) || !WA_ControlHeld() )
		return;
	// dead: his own command brings him back
	if ( ent->health <= 0 || ent->client->sess.sessionTeam == TEAM_SPECTATOR )
		return;

	ucmd->forwardmove = ucmd->rightmove = ucmd->upmove = 0;
	ucmd->buttons = 0;
	if ( control.walking ) {
		WA_ControlWalk( ent, ucmd );
	} else if ( level.time - control.inputTime <= WA_CONTROL_HOLD ) {
		ucmd->forwardmove = control.forward;
		ucmd->rightmove = control.right;
		ucmd->upmove = control.up;
		ucmd->buttons = control.buttons;
	}
	// the view of a command is what it says plus delta_angles (PM_UpdateViewAngles)
	for ( i = 0; i < 3; i++ )
		ucmd->angles[i] = ANGLE2SHORT( control.angles[i] ) - ent->client->ps.delta_angles[i];
}
