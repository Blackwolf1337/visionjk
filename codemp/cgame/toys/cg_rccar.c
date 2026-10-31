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

// cg_rccar.c -- the remote controlled car, the third toy of issue #9 that only players of this tree see: the mouse
// droid of the game on wheels. v_rccar (Controls, Extras) puts it down in front of the player and gives him the
// remote: W and S (or the arrows) drive, A and D steer, Space hops, the mouse looks around the car, Esc or v_rccar
// again puts it away. While he drives the cgame holds the keys, so his player stands still and shows the dots of
// talking (CL_CmdButtons()), which on a server of JA+ means nobody can hurt him. The camera follows the car.
//
// This cgame drives the car with traces of its own against the level and the players: it rolls up steps and
// slopes, slides along walls, falls and lands. The others hear where it is, how fast it goes and where it points
// six times a second (TOY_RC, game/toys/bg_toys.h, the way a snowball goes, toys/cg_toys.c) and let it roll on
// from there between two of them; TOY_RC_GONE puts it away, and a car not heard of for RC_LOST_MSEC is gone. The
// server knows nothing of it. The level far from the player can lack what his server does not send him (other
// players, doors), the world itself is drawn from where the camera is. Not from JA+, see docs/japlus/toys.md.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "cgame/toys/cg_toys.h"

#define RC_SPEED			420.0f		// units a second forwards at most,
#define RC_REVERSE			200.0f		//	backwards
#define RC_ACCEL			700.0f		// units a second a second
#define RC_BRAKE			1400.0f
#define RC_DRAG				2.2f		// of the speed a second, with no throttle
#define RC_TURN				170.0f		// degrees a second at full speed
#define RC_HOP				260.0f
#define RC_GRAVITY			800.0f
#define RC_STEP				14.0f		// it rolls up steps this high
#define RC_SEND_MSEC		165			// the others hear of the own car this often
#define RC_LOST_MSEC		1500		// a car not heard of for this long is gone
#define RC_CAMERA_RANGE		90.0f
#define RC_CAMERA_HEIGHT	22.0f
#define RC_TURN_MOUSE		0.15f		// degrees a count of the mouse turns the camera around the car
#define RC_MODEL			"models/players/mouse/lower.md3"

typedef struct {
	qboolean	active;
	vec3_t		origin;
	vec3_t		velocity;		// over the ground; of somebody else's car: as it was heard
	float		yaw;
	float		vz;				// up and down, the own car only
	qboolean	onGround;
	int			heardTime;		// of somebody else's car: when it was heard last
} rcCar_t;

static const vec3_t rcMins = { -9.0f, -9.0f, 0.0f }, rcMaxs = { 9.0f, 9.0f, 12.0f };

static struct {
	rcCar_t		cars[MAX_CLIENTS];
	qboolean	driving;
	qboolean	keys[6];		// forward, back, left, right, hop, (unused)
	float		lookYaw, lookPitch;		// of the camera, relative to the car
	int			sendTime;
	qhandle_t	model;
	sfxHandle_t	motor, beep[3];
	qboolean	registered;
} rc;

enum { RC_FORWARD, RC_BACK, RC_LEFT, RC_RIGHT, RC_JUMP };

// the direction a yaw points in, flat
static void Rc_Forward( float yaw, vec3_t forward ) {
	vec3_t angles;

	VectorSet( angles, 0.0f, yaw, 0.0f );
	AngleVectors( angles, forward, NULL, NULL );
}

static void Rc_Register( void ) {
	if ( rc.registered )
		return;
	rc.registered = qtrue;
	rc.model = trap->R_RegisterModel( RC_MODEL );
	rc.motor = trap->S_RegisterSound( "sound/chars/mouse/misc/mouse_lp.wav" );
	rc.beep[0] = trap->S_RegisterSound( "sound/chars/mouse/misc/mousego1.mp3" );
	rc.beep[1] = trap->S_RegisterSound( "sound/chars/mouse/misc/mousego2.mp3" );
	rc.beep[2] = trap->S_RegisterSound( "sound/chars/mouse/misc/mousego3.mp3" );
}

static void Rc_Send( int kind ) {
	const rcCar_t	*car = &rc.cars[cg.clientNum];
	toyThrow_t		toy;
	int				i;

	toy.kind = kind;
	for ( i = 0; i < 3; i++ )
		toy.origin[i] = (int)car->origin[i];
	toy.velocity[0] = kind == TOY_RC ? Com_Clampi( -TOY_MAX_SPEED, TOY_MAX_SPEED, (int)car->velocity[0] ) : 0;
	toy.velocity[1] = kind == TOY_RC ? Com_Clampi( -TOY_MAX_SPEED, TOY_MAX_SPEED, (int)car->velocity[1] ) : 0;
	toy.velocity[2] = kind == TOY_RC ? (int)AngleNormalize180( car->yaw ) : 0;
	if ( BG_ToyValid( &toy ) )
		CG_ToySend( &toy );
	rc.sendTime = cg.time ? cg.time : 1;
}

static void Rc_Leave( void ) {
	if ( !rc.driving )
		return;
	rc.driving = qfalse;
	memset( rc.keys, 0, sizeof( rc.keys ) );
	rc.cars[cg.clientNum].active = qfalse;
	Rc_Send( TOY_RC_GONE );
	trap->Key_SetCatcher( trap->Key_GetCatcher() & ~KEYCATCH_CGAME );
}

// v_rccar: the car goes down in front of the player and he drives it, or it is put away
void CG_RcCar_f( void ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	rcCar_t				*car;
	vec3_t				forward, start, end;
	trace_t				tr;

	if ( rc.driving ) {
		Rc_Leave();
		return;
	}
	if ( !CG_ToyCanUse() || cg.clientNum < 0 || cg.clientNum >= MAX_CLIENTS )
		return;
	Rc_Register();
	car = &rc.cars[cg.clientNum];
	memset( car, 0, sizeof( *car ) );
	Rc_Forward( cg.refdef.viewangles[YAW], forward );
	// in front of the feet, as far as there is room, then down to the floor
	VectorCopy( ps->origin, start );
	start[2] += 8.0f;
	VectorMA( start, 48.0f, forward, end );
	CG_Trace( &tr, start, rcMins, rcMaxs, end, cg.clientNum, MASK_PLAYERSOLID );
	if ( tr.startsolid )
		return;
	VectorCopy( tr.endpos, start );
	VectorCopy( start, end );
	end[2] -= 128.0f;
	CG_Trace( &tr, start, rcMins, rcMaxs, end, cg.clientNum, MASK_PLAYERSOLID );
	VectorCopy( tr.endpos, car->origin );
	car->yaw = cg.refdef.viewangles[YAW];
	car->onGround = (qboolean)( tr.fraction < 1.0f );
	car->active = qtrue;
	rc.driving = qtrue;
	rc.lookYaw = 0.0f;
	rc.lookPitch = 15.0f;
	memset( rc.keys, 0, sizeof( rc.keys ) );
	trap->Key_SetCatcher( trap->Key_GetCatcher() | KEYCATCH_CGAME );
	// he holds a remote, he is not posing: the dots of talking stay (CL_CmdButtons())
	trap->Cvar_Set( "v_catchQuiet", "0" );
	trap->S_StartSound( car->origin, cg.clientNum, CHAN_AUTO, rc.beep[rand() % 3] );
	Rc_Send( TOY_RC );
}

qboolean CG_RcDriving( void ) {
	return rc.driving;
}

// somebody else's car, from the game module or the masterserver
void CG_RcHeard( int client, int kind, const vec3_t origin, const vec3_t velocity ) {
	rcCar_t *car = &rc.cars[client];

	if ( kind == TOY_RC_GONE ) {
		car->active = qfalse;
		return;
	}
	Rc_Register();
	if ( !car->active )
		trap->S_StartSound( (float *)origin, client, CHAN_AUTO, rc.beep[rand() % 3] );
	car->active = qtrue;
	car->heardTime = cg.time ? cg.time : 1;
	VectorCopy( origin, car->origin );
	VectorSet( car->velocity, velocity[0], velocity[1], 0.0f );
	car->yaw = velocity[2];
}

// one step of the own car, of dt seconds: throttle, steering, gravity, and the move against the level
static void Rc_Drive( rcCar_t *car, float dt ) {
	const float	throttle = ( rc.keys[RC_FORWARD] ? 1.0f : 0.0f ) - ( rc.keys[RC_BACK] ? 1.0f : 0.0f );
	const float	steer = ( rc.keys[RC_LEFT] ? 1.0f : 0.0f ) - ( rc.keys[RC_RIGHT] ? 1.0f : 0.0f );
	vec3_t		forward, move, start, end, up;
	trace_t		tr;
	float		speed, along;
	int			bump;

	Rc_Forward( car->yaw, forward );
	speed = DotProduct( car->velocity, forward );
	if ( car->onGround ) {
		if ( throttle > 0.0f )
			speed += ( speed < 0.0f ? RC_BRAKE : RC_ACCEL ) * dt;
		else if ( throttle < 0.0f )
			speed -= ( speed > 0.0f ? RC_BRAKE : RC_ACCEL ) * dt;
		else
			speed -= speed * Q_min( 1.0f, RC_DRAG * dt );
		speed = Com_Clamp( -RC_REVERSE, RC_SPEED, speed );
		// it turns as fast as it rolls, the other way round backwards
		car->yaw += steer * RC_TURN * dt * Com_Clamp( -1.0f, 1.0f, speed / ( RC_SPEED * 0.5f ) );
		Rc_Forward( car->yaw, forward );
		VectorScale( forward, speed, car->velocity );
		if ( rc.keys[RC_JUMP] ) {
			car->vz = RC_HOP;
			car->onGround = qfalse;
			rc.keys[RC_JUMP] = qfalse;
		}
	}
	car->vz -= RC_GRAVITY * dt;

	VectorScale( car->velocity, dt, move );
	move[2] = car->vz * dt;
	for ( bump = 0; bump < 3 && VectorLengthSquared( move ) > 0.01f; bump++ ) {
		VectorAdd( car->origin, move, end );
		CG_Trace( &tr, car->origin, rcMins, rcMaxs, end, cg.clientNum, MASK_PLAYERSOLID );
		if ( tr.fraction >= 1.0f ) {
			VectorCopy( end, car->origin );
			break;
		}
		// a step: up, along, and down again
		if ( tr.plane.normal[2] < 0.7f && car->onGround ) {
			trace_t stepTr;

			VectorCopy( car->origin, start );
			VectorCopy( start, up );
			up[2] += RC_STEP;
			CG_Trace( &stepTr, start, rcMins, rcMaxs, up, cg.clientNum, MASK_PLAYERSOLID );
			VectorCopy( stepTr.endpos, start );
			VectorSet( end, start[0] + move[0], start[1] + move[1], start[2] );
			CG_Trace( &stepTr, start, rcMins, rcMaxs, end, cg.clientNum, MASK_PLAYERSOLID );
			if ( stepTr.fraction >= 1.0f && !stepTr.startsolid ) {
				VectorCopy( stepTr.endpos, start );
				VectorCopy( start, end );
				end[2] -= RC_STEP + 2.0f;
				CG_Trace( &stepTr, start, rcMins, rcMaxs, end, cg.clientNum, MASK_PLAYERSOLID );
				VectorCopy( stepTr.endpos, car->origin );
				break;
			}
		}
		VectorCopy( tr.endpos, car->origin );
		// what is left of the move slides along what it hit, and so does the speed
		VectorScale( move, 1.0f - tr.fraction, move );
		along = DotProduct( move, tr.plane.normal );
		VectorMA( move, -along, tr.plane.normal, move );
		along = DotProduct( car->velocity, tr.plane.normal );
		if ( along < 0.0f )
			VectorMA( car->velocity, -along, tr.plane.normal, car->velocity );
		if ( tr.plane.normal[2] > 0.7f && car->vz < 0.0f )
			car->vz = 0.0f;
	}

	// on the ground: a short trace down
	VectorCopy( car->origin, end );
	end[2] -= 1.0f;
	CG_Trace( &tr, car->origin, rcMins, rcMaxs, end, cg.clientNum, MASK_PLAYERSOLID );
	car->onGround = (qboolean)( tr.fraction < 1.0f && tr.plane.normal[2] > 0.7f && car->vz <= 0.0f );
	if ( car->onGround )
		car->vz = 0.0f;
	if ( car->origin[2] < -TOY_MAX_COORD + 1024 )
		Rc_Leave();
}

/*
=================
CG_RcFrame

Once a frame before the view is worked out: the own car drives (qtrue: the view follows it)
=================
*/
qboolean CG_RcFrame( void ) {
	rcCar_t	*car;
	float	dt;

	if ( !rc.driving )
		return qfalse;
	// a menu or the console took the keys, the player died or left the game
	if ( !CG_ToyCanUse() || !( trap->Key_GetCatcher() & KEYCATCH_CGAME ) ) {
		Rc_Leave();
		return qfalse;
	}
	car = &rc.cars[cg.clientNum];
	dt = Q_min( cg.frametime * 0.001f, 0.05f );
	if ( dt > 0.0f )
		Rc_Drive( car, dt );
	if ( rc.driving && ( !rc.sendTime || cg.time - rc.sendTime >= RC_SEND_MSEC || cg.time < rc.sendTime ) )
		Rc_Send( TOY_RC );
	return rc.driving;
}

/*
=================
CG_RcView

After the view is worked out: the camera behind the car, turned by the mouse, held off the walls
=================
*/
void CG_RcView( void ) {
	static const vec3_t	mins = { -4.0f, -4.0f, -4.0f }, maxs = { 4.0f, 4.0f, 4.0f };
	const rcCar_t		*car = &rc.cars[cg.clientNum];
	vec3_t				target, angles, forward, eye;
	trace_t				tr;

	if ( !rc.driving )
		return;
	VectorCopy( car->origin, target );
	target[2] += RC_CAMERA_HEIGHT;
	VectorSet( angles, rc.lookPitch, car->yaw + rc.lookYaw, 0.0f );
	AngleVectors( angles, forward, NULL, NULL );
	VectorMA( target, -RC_CAMERA_RANGE, forward, eye );
	CG_Trace( &tr, target, mins, maxs, eye, cg.clientNum, MASK_SOLID );
	VectorCopy( tr.endpos, cg.refdef.vieworg );
	VectorCopy( angles, cg.refdef.viewangles );
	AnglesToAxis( cg.refdef.viewangles, cg.refdef.viewaxis );
}

qboolean CG_RcMouseEvent( int x, int y ) {
	if ( !rc.driving )
		return qfalse;
	rc.lookYaw = AngleNormalize180( rc.lookYaw - x * RC_TURN_MOUSE );
	rc.lookPitch = Com_Clamp( -10.0f, 70.0f, rc.lookPitch + y * RC_TURN_MOUSE );
	return qtrue;
}

qboolean CG_RcKeyEvent( int key, qboolean down ) {
	if ( !rc.driving )
		return qfalse;
	switch ( key ) {
	case A_ESCAPE:
		if ( down )
			Rc_Leave();
		break;
	case 'w': case 'W': case A_CURSOR_UP:		rc.keys[RC_FORWARD] = down; break;
	case 's': case 'S': case A_CURSOR_DOWN:		rc.keys[RC_BACK] = down; break;
	case 'a': case 'A': case A_CURSOR_LEFT:		rc.keys[RC_LEFT] = down; break;
	case 'd': case 'D': case A_CURSOR_RIGHT:	rc.keys[RC_RIGHT] = down; break;
	case A_SPACE:								if ( down ) rc.keys[RC_JUMP] = qtrue; break;
	case A_MOUSE2:								if ( down ) rc.lookYaw = 0.0f; break;
	default:									break;
	}
	return qtrue;
}

// While the scene is built: every car, the own one where it drives, the others rolled on from where they were heard
void CG_RcScene( void ) {
	refEntity_t	ent;
	int			i;

	if ( !rc.registered )
		return;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		rcCar_t	*car = &rc.cars[i];
		vec3_t	origin, angles;
		float	since;

		if ( !car->active )
			continue;
		VectorCopy( car->origin, origin );
		if ( i != cg.clientNum ) {
			if ( !v_toys.integer || cg.time - car->heardTime > RC_LOST_MSEC || cg.time < car->heardTime ) {
				car->active = qfalse;
				continue;
			}
			// (on from where it was heard, at most until the next one is due)
			since = Q_min( cg.time - car->heardTime, RC_SEND_MSEC * 2 ) * 0.001f;
			VectorMA( origin, since, car->velocity, origin );
		}
		memset( &ent, 0, sizeof( ent ) );
		ent.reType = RT_MODEL;
		ent.hModel = rc.model;
		VectorCopy( origin, ent.origin );
		VectorCopy( origin, ent.lightingOrigin );
		// (the model hangs from 12 to 24 units under its origin, as the lower part of a player)
		ent.origin[2] += 24.0f;
		VectorSet( angles, 0.0f, car->yaw, 0.0f );
		AnglesToAxis( angles, ent.axis );
		ent.renderfx = RF_LIGHTING_ORIGIN;
		trap->R_AddRefEntityToScene( &ent );
		// the motor, louder the faster it goes (a loop: it has to be said every frame)
		if ( VectorLengthSquared( car->velocity ) > 100.0f )
			trap->S_AddLoopingSound( i, origin, car->velocity, rc.motor );
	}
}

// what the keys do, while driving
void CG_RcDraw( void ) {
	vec4_t		color;
	const char	*text;

	if ( !rc.driving )
		return;
	VectorSet4( color, 1.0f, 1.0f, 1.0f, 0.75f );
	text = CG_VisionText( "W/S: drive. A/D: steer. Space: hop. Mouse: look. Esc: put it away." );
	CG_Text_Paint( 320.0f - CG_Text_Width( text, 0.45f, FONT_SMALL ) * 0.5f, 450.0f, 0.45f, color, text, 0, 0, ITEM_TEXTSTYLE_SHADOWED,
		FONT_SMALL );
}

void CG_RcInit( void ) {
	memset( &rc, 0, sizeof( rc ) );
}
