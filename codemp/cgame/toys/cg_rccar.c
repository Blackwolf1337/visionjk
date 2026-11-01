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

// cg_rccar.c -- the remote controlled car, the third toy of issue #9 that only players of this tree see: a toy monster
// truck (models/vision/toys/rccar/ from tools/toys/rccar.py, Kenney's Toy Car Kit, CC0) on springs, whose wheels roll
// and steer, with lamps that light the way and brake lights. v_rccar (Controls, Extras) puts it down in front of the
// player and gives him the remote: W and S (or the arrows) drive, A and D steer, Shift is the handbrake, Space hops,
// the mouse looks around the car, Esc or v_rccar again puts it away. While he drives the cgame holds the keys, so his
// player stands still and shows the dots of talking (CL_CmdButtons()), which on a server of JA+ means nobody can hurt
// him. The camera follows the car.
//
// This cgame drives the car as arcade racing games do, RC_HZ steps a second with traces of its own against the level
// and the players: each wheel is a ray down from its mount with a spring and a damper (a raycast vehicle, as the
// WheelCollider of Unity or the raycast vehicle of Bullet), the body is pushed up by the four of them and pitches and
// rolls by how differently they push, and leans on throttle, brakes and in turns. The tires take the sideways speed
// away, at most as hard as the load on them allows: past that the car slides, more with the handbrake. In the air the
// throttle tips the nose as with a real RC car. The body is a box against walls and steps.
//
// What the wheels leave: the tread pressed into sand, dirt, mud and snow, rubber on hard ground while it slides or
// brakes hard (strips from where each wheel was to where it is, laid on the level once with R_MarkFragments, faded out
// over RC_TRACK_MSEC), dust of the ground and smoke of the tires; through the puddles of the water gun spray and rings,
// and then wet tracks of the rear tires that dry (toys/cg_water.c). A fast wheel is drawn with its tread and rim
// blurred by the turn (wheel_blur.skin).
//
// Its sounds are made by tools/toys/sounds.py: the whine of its electric motor in loops of four speeds that crossfade
// by how fast it goes (ext.S_AddLoopingSoundVolume of the engine, as racing games blend the loops of their engines),
// louder under throttle, the scrub of its tires sliding and their crunch over dirt, the buzz of the servo when the
// front wheels swing, the thump of a landing and the chirp of the receiver.
//
// The others hear where it is, how fast it goes and where it points six times a second (TOY_RC, game/toys/bg_toys.h,
// the way a snowball goes, toys/cg_toys.c; the point on the ground under it, as before the springs) and let it roll
// on from there between two of them, a jump to where it was heard fading out; its body leans by the ground under its
// wheels and by its turn. TOY_RC_GONE puts it away, and a car not heard of for RC_LOST_MSEC is gone. The server knows
// nothing of it. The level far from the player can lack what his server does not send him (other players, doors), the
// world itself is drawn from where the camera is. Not from JA+, see docs/japlus/toys.md.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "cgame/toys/cg_toys.h"

#define RC_SPEED			460.0f		// units a second forwards at most,
#define RC_REVERSE			220.0f		//	backwards
#define RC_ACCEL			900.0f		// units a second a second at a standstill, less the faster it goes
#define RC_BRAKE			1500.0f
#define RC_HANDBRAKE		600.0f
#define RC_DRAG				0.6f		// of the speed a second, with no throttle
#define RC_HOP				240.0f
#define RC_GRAVITY			800.0f
#define RC_HZ				120			// steps a second of the own car
#define RC_REST				3.5f		// units from a mount down to its axle without load,
#define RC_DROOP			6.0f		//	the most, hanging in the air
#define RC_SPRING			121.0f		// of a wheel, a unit compressed, over the mass of the car: the four hold it
										//	1.65 units down, about three and a half bounces a second
#define RC_DAMPER			6.0f		// of a wheel, a unit a second
#define RC_BUMP				900.0f		// the stop at full compression,
#define RC_BUMP_DAMPER		30.0f		//	and its damper
#define RC_PITCH_INERTIA	77.0f		// of the body over its mass (units squared): about its cross axis,
#define RC_ROLL_INERTIA		48.0f		//	about its long axis
#define RC_TILT_DAMP		1.5f		// of the pitching and rolling a second, besides the dampers
#define RC_TILT_MAX			1.1f		// radians it pitches and rolls at most
#define RC_HEIGHT			3.0f		// units over the ground its weight shifts from
#define RC_GRIP				14.0f		// of the speed across the car the tires take a second,
#define RC_MU				1.6f		//	pushing at most this much of the load while they grip,
#define RC_MU_SLIDE			1.0f		//	this much while they slide,
#define RC_MU_HANDBRAKE		0.45f		//	and with the handbrake
#define RC_AIR_TILT			5.0f		// radians a second a second the throttle tips it in the air
#define RC_AIR_TURN			90.0f		// degrees a second a second the steering turns it in the air
#define RC_STEP				10.0f		// the body rolls up steps this high
#define RC_SEND_MSEC		165			// the others hear of the own car this often
#define RC_LOST_MSEC		1500		// a car not heard of for this long is gone
#define RC_CAMERA_RANGE		90.0f
#define RC_CAMERA_HEIGHT	22.0f
#define RC_CAMERA_FOLLOW	6.0f		// of the turn of the car the camera follows a second
#define RC_TURN_MOUSE		0.15f		// degrees a count of the mouse turns the camera around the car
#define RC_BODY				"models/vision/toys/rccar/body.md3"
#define RC_WHEEL			"models/vision/toys/rccar/wheel.md3"
#define RC_WHEEL_RADIUS		5.57f		// of wheel.md3
#define RC_STEER			26.0f		// degrees the front wheels turn at most, half that at full speed,
#define RC_STEER_SPEED		220.0f		//	this many a second
#define RC_LAMP_GAP			4.2f		// units the two lamps of a pair are from the middle
#define RC_TIRE_HALF		2.8f		// half the width of a tire
#define RC_TRACKS			512			// pieces of the marks of the tires at once
#define RC_TRACK_MSEC		20000		// a mark lasts this long, the last third of it fading
#define RC_TRACK_STEP		6.0f		// units of a piece of a mark,
#define RC_TRACK_TILE		15.6f		//	of the picture of the tread along it (eight knobs)
#define RC_WET_STEP			16.0f		// units of a piece of a wet track,
#define RC_WET_REACH		320.0f		//	and how far a tire out of standing water leaves one
#define RC_EFFECT_MSEC		70			// a wheel throws dust, smoke or spray this often at most
#define RC_BLUR_SPEED		150.0f		// units a second over which the wheels are drawn blurred (120 back)
#define RC_MARK_POINTS		64
#define RC_MARK_FRAGMENTS	16
// how high the body is over the ground, resting on its springs: the origin of the model is at the axles
#define RC_RIDE				( RC_WHEEL_RADIUS - RC_GRAVITY / ( 4.0f * RC_SPRING ) )

typedef struct {
	qboolean	active;
	vec3_t		origin;			// of the body: the middle between its axles
	vec3_t		velocity;		// the own car: in the level; somebody else's: over the ground, as it was heard
	float		yaw, yawSpeed;	// degrees, degrees a second
	float		pitch, roll;	// radians: the nose up, the left side up
	float		pitchSpeed, rollSpeed;
	float		reach[4];		// units from each mount down to its axle
	qboolean	touch[4];		// the wheel is on the ground
	float		steer;			// degrees the front wheels are turned, to the left
	float		spin;			// degrees the wheels have rolled
	qboolean	braking, sliding;
	qboolean	blurred;		// its wheels are drawn blurred
	float		airborne;		// the own car: seconds since a wheel touched
	float		servoFrom;		// where the front wheels were at the last buzz of the servo,
	int			servoTime;		//	and its cg.time
	vec3_t		ground[4];		// where each wheel touches the ground,
	vec3_t		groundNormal[4];	//	the way the ground faces there,
	int			material[4];	//	and what it is made of (MATERIAL_*)
	vec3_t		markFrom[4];	// where the mark of each wheel was laid to last,
	float		markAlong[4];	//	how far it has run (the tread of the picture along it),
	int			markKind[4];	//	and which mark it is (RC_MARK_*)
	float		wetTire[4];		// how wet each tire is, from the puddles (CG_WaterWheel)
	int			effectTime[4];	// cg.time of the last dust, smoke or spray of each wheel
	float		pending;		// the own car: seconds not stepped yet
	int			heardTime;		// somebody else's car: when it was heard last,
	float		heardSpeed;		//	how fast it went then (for its brake lights),
	float		heardTurn;		//	how fast it turned, degrees a second (for its wheels and its lean)
	vec3_t		error;			//	and where it was drawn minus where it was heard, which fades out
} rcCar_t;

// The body against the level, around its origin: over the wheels, which the rays of the springs carry over the ground,
// so a slope or a low step meets the wheels first and not the corner of the box (which does not pitch with the car)
static const vec3_t rcMins = { -9.0f, -9.0f, 3.0f }, rcMaxs = { 9.0f, 9.0f, 16.0f };
static const char *rcWheelTags[4] = { "tag_wheel_fl", "tag_wheel_fr", "tag_wheel_bl", "tag_wheel_br" };

enum { RC_MARK_NONE, RC_MARK_TRACK, RC_MARK_SKID, RC_MARK_WET };

// a piece of a mark of the tires, on the level as R_MarkFragments cut it
typedef struct {
	int			time;			// cg.time it was laid, 0: free
	qhandle_t	shader;
	float		strength;
	int			numVerts;
	polyVert_t	verts[MAX_VERTS_ON_POLY];
} rcTrack_t;

static rcTrack_t	rcTracks[RC_TRACKS];
static int			rcNextTrack;

static struct {
	rcCar_t		cars[MAX_CLIENTS];
	qboolean	driving;
	qboolean	keys[6];		// forward, back, left, right, hop, handbrake
	float		lookYaw, lookPitch;		// of the camera, relative to the car
	float		cameraYaw;				//	which follows the car a little behind
	int			sendTime;
	qhandle_t	body, wheel, lampShader, trackShader, skidShader, blurSkin;
	int			smokeFx;
	qboolean	loopVolume;		// the engine plays loops at a volume of their own (v_cgameLoopVolume)
	vec3_t		wheels[4];		// where the axles are on the body (its tags)
	vec3_t		head, tail;		//	and where the lamps shine from
	sfxHandle_t	motor[4], scrub, crunch, servo, land[2], power;
	qboolean	registered;
} rc;

enum { RC_FORWARD, RC_BACK, RC_LEFT, RC_RIGHT, RC_JUMP, RC_HANDBRAKE_KEY };

// the direction a yaw points in, flat
static void Rc_Forward( float yaw, vec3_t forward ) {
	vec3_t angles;

	VectorSet( angles, 0.0f, yaw, 0.0f );
	AngleVectors( angles, forward, NULL, NULL );
}

// the axes of the body: pitch and roll of the car turned into the angles of the game (its pitch is the nose down,
// its roll the right side down)
static void Rc_Axes( const rcCar_t *car, float yaw, vec3_t axis[3] ) {
	vec3_t angles;

	VectorSet( angles, -RAD2DEG( car->pitch ), yaw, RAD2DEG( car->roll ) );
	AnglesToAxis( angles, axis );
}

// where the spring of a wheel is held on the body: over its axle by the length of a spring without load
static void Rc_Mount( const vec3_t origin, const vec3_t axis[3], int wheel, vec3_t out ) {
	VectorMA( origin, rc.wheels[wheel][0], axis[0], out );
	VectorMA( out, rc.wheels[wheel][1], axis[1], out );
	VectorMA( out, RC_REST, axis[2], out );
}

static void Rc_Register( void ) {
	int i;

	if ( rc.registered )
		return;
	rc.registered = qtrue;
	rc.body = trap->R_RegisterModel( RC_BODY );
	rc.wheel = trap->R_RegisterModel( RC_WHEEL );
	rc.lampShader = trap->R_RegisterShader( "models/vision/toys/rccar/lamp" );
	rc.trackShader = trap->R_RegisterShader( "models/vision/toys/rccar/track" );
	rc.skidShader = trap->R_RegisterShader( "models/vision/toys/rccar/skid" );
	rc.blurSkin = trap->R_RegisterSkin( "models/vision/toys/rccar/wheel_blur.skin" );
	rc.smokeFx = trap->FX_RegisterEffect( "effects/vision/tire_smoke.efx" );
	for ( i = 0; i < 4; i++ ) {
		orientation_t tag;

		// (what tools/toys/rccar.py printed, should the tags be missing)
		VectorSet( rc.wheels[i], i < 2 ? 7.43f : -7.43f, ( i & 1 ) ? -6.5f : 6.5f, 0.0f );
		if ( trap->R_LerpTag( &tag, rc.body, 0, 0, 1.0f, rcWheelTags[i] ) )
			VectorCopy( tag.origin, rc.wheels[i] );
	}
	VectorSet( rc.head, 9.3f, 0.0f, 8.8f );
	VectorSet( rc.tail, -9.3f, 0.0f, 8.8f );
	{
		orientation_t tag;

		if ( trap->R_LerpTag( &tag, rc.body, 0, 0, 1.0f, "tag_head" ) )
			VectorCopy( tag.origin, rc.head );
		if ( trap->R_LerpTag( &tag, rc.body, 0, 0, 1.0f, "tag_tail" ) )
			VectorCopy( tag.origin, rc.tail );
	}
	// (the sounds are made by tools/toys/sounds.py)
	for ( i = 0; i < 4; i++ )
		rc.motor[i] = trap->S_RegisterSound( va( "sound/vision/toys/rc_motor%i.wav", i + 1 ) );
	rc.scrub = trap->S_RegisterSound( "sound/vision/toys/rc_scrub.wav" );
	rc.crunch = trap->S_RegisterSound( "sound/vision/toys/rc_crunch.wav" );
	rc.servo = trap->S_RegisterSound( "sound/vision/toys/rc_servo.wav" );
	rc.land[0] = trap->S_RegisterSound( "sound/vision/toys/rc_land1.wav" );
	rc.land[1] = trap->S_RegisterSound( "sound/vision/toys/rc_land2.wav" );
	rc.power = trap->S_RegisterSound( "sound/vision/toys/rc_on.wav" );
	{
		char value[8];

		trap->Cvar_VariableStringBuffer( "v_cgameLoopVolume", value, sizeof( value ) );
		rc.loopVolume = (qboolean)( atoi( value ) && trap->ext.S_AddLoopingSoundVolume );
	}
}

// the own car to the others: the point on the ground under it, its speed over the ground and its yaw
static void Rc_Send( int kind ) {
	const rcCar_t	*car = &rc.cars[cg.clientNum];
	toyThrow_t		toy;
	int				i;

	toy.kind = kind;
	for ( i = 0; i < 3; i++ )
		toy.origin[i] = (int)car->origin[i];
	toy.origin[2] = (int)( car->origin[2] - RC_RIDE );
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
	int					i;

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
	// resting on its springs over the floor the bottom of the box found
	VectorCopy( tr.endpos, car->origin );
	car->origin[2] += rcMins[2] + RC_RIDE;
	car->yaw = cg.refdef.viewangles[YAW];
	for ( i = 0; i < 4; i++ )
		car->reach[i] = RC_REST - ( RC_WHEEL_RADIUS - RC_RIDE );
	car->active = qtrue;
	rc.driving = qtrue;
	rc.lookYaw = 0.0f;
	rc.lookPitch = 15.0f;
	rc.cameraYaw = car->yaw;
	memset( rc.keys, 0, sizeof( rc.keys ) );
	trap->Key_SetCatcher( trap->Key_GetCatcher() | KEYCATCH_CGAME );
	// he holds a remote, he is not posing: the dots of talking stay (CL_CmdButtons())
	trap->Cvar_Set( "v_catchQuiet", "0" );
	trap->S_StartSound( car->origin, cg.clientNum, CHAN_AUTO, rc.power );
	Rc_Send( TOY_RC );
}

qboolean CG_RcDriving( void ) {
	return rc.driving;
}

// where somebody else's car is drawn now: on from where it was heard, at most until the next one is due
static void Rc_Heard( const rcCar_t *car, vec3_t origin, float *yaw ) {
	const float since = Q_min( cg.time - car->heardTime, RC_SEND_MSEC * 2 ) * 0.001f;

	VectorMA( car->origin, since, car->velocity, origin );
	VectorAdd( origin, car->error, origin );
	*yaw = car->yaw + car->heardTurn * since;
}

// somebody else's car, from the game module or the masterserver
void CG_RcHeard( int client, int kind, const vec3_t origin, const vec3_t velocity ) {
	rcCar_t *car = &rc.cars[client];
	vec3_t	drawn, now;
	float	drawnYaw = 0.0f;

	if ( kind == TOY_RC_GONE ) {
		car->active = qfalse;
		return;
	}
	Rc_Register();
	if ( !car->active )
		trap->S_StartSound( (float *)origin, client, CHAN_AUTO, rc.power );
	VectorSet( now, origin[0], origin[1], origin[2] + RC_RIDE );
	VectorClear( car->error );
	if ( car->active && cg.time > car->heardTime ) {
		const float	speed = sqrtf( velocity[0] * velocity[0] + velocity[1] * velocity[1] );
		const float	since = ( cg.time - car->heardTime ) * 0.001f;

		// the jump from where it was drawn to where it is fades out instead (unless it is a long way)
		Rc_Heard( car, drawn, &drawnYaw );
		VectorSubtract( drawn, now, car->error );
		if ( VectorLengthSquared( car->error ) > 64.0f * 64.0f )
			VectorClear( car->error );
		car->braking = (qboolean)( speed < car->heardSpeed - 15.0f );
		car->heardTurn = AngleNormalize180( velocity[2] - car->yaw ) / since;
		car->heardSpeed = speed;
	} else {
		car->pitch = car->roll = 0.0f;
		car->heardTurn = 0.0f;
		car->heardSpeed = 0.0f;
	}
	car->active = qtrue;
	car->heardTime = cg.time ? cg.time : 1;
	VectorCopy( now, car->origin );
	VectorSet( car->velocity, velocity[0], velocity[1], 0.0f );
	car->yaw = velocity[2];
}

// the body against the level by dt: it slides along what it hits, bounces off a little, and rolls up low steps
static void Rc_Move( rcCar_t *car, float dt ) {
	vec3_t	move, end, start, up;
	trace_t	tr;
	int		bump;

	VectorScale( car->velocity, dt, move );
	for ( bump = 0; bump < 3 && VectorLengthSquared( move ) > 0.0001f; bump++ ) {
		float along;

		VectorAdd( car->origin, move, end );
		CG_Trace( &tr, car->origin, rcMins, rcMaxs, end, cg.clientNum, MASK_PLAYERSOLID );
		if ( tr.startsolid ) {
			// (stuck in something that moved into it: up and out of it if it can)
			car->origin[2] += 1.0f;
			break;
		}
		if ( tr.fraction >= 1.0f ) {
			VectorCopy( end, car->origin );
			break;
		}
		// a step: up, along, and down again onto it
		if ( tr.plane.normal[2] < 0.7f && ( car->touch[0] || car->touch[1] ) ) {
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
				end[2] -= RC_STEP;
				CG_Trace( &stepTr, start, rcMins, rcMaxs, end, cg.clientNum, MASK_PLAYERSOLID );
				VectorCopy( stepTr.endpos, car->origin );
				break;
			}
		}
		VectorCopy( tr.endpos, car->origin );
		// what is left of the move slides along what it hit, and the speed into it comes back a little
		VectorScale( move, 1.0f - tr.fraction, move );
		along = DotProduct( move, tr.plane.normal );
		VectorMA( move, -along, tr.plane.normal, move );
		along = DotProduct( car->velocity, tr.plane.normal );
		if ( along < 0.0f )
			VectorMA( car->velocity, -1.3f * along, tr.plane.normal, car->velocity );
	}
}

/*
=================
Rc_Step

One step of the own car, of dt seconds. The springs: each wheel a ray down from its mount along the body, the
spring compressed by as much as the ground is nearer than the wheel would hang without load, a stop at the end of it,
a solid one past that; the dampers take speed away implicitly. Their push lifts the body along its up axis and tips it by where they push.
The tires, where any wheel touches: the throttle, the brakes and the handbrake along the car, the sideways speed
taken away as hard as the load allows, the turn the front wheels ask for (a bicycle: speed times the tangent of the
steering over the wheelbase). The weight shifts by how the car speeds up and turns, from RC_HEIGHT over the ground.
=================
*/
static void Rc_Step( rcCar_t *car, float dt ) {
	const float	throttle = ( rc.keys[RC_FORWARD] ? 1.0f : 0.0f ) - ( rc.keys[RC_BACK] ? 1.0f : 0.0f );
	const float	steering = ( rc.keys[RC_LEFT] ? 1.0f : 0.0f ) - ( rc.keys[RC_RIGHT] ? 1.0f : 0.0f );
	const qboolean handbrake = rc.keys[RC_HANDBRAKE_KEY];
	const float	base = rc.wheels[0][0] - rc.wheels[2][0];
	vec3_t		axis[3], forward, left, start, end;
	trace_t		tr;
	float		reach[4], load = 0.0f, pitchTorque = 0.0f, rollTorque = 0.0f, deepest = 0.0f;
	float		damping = 0.0f, pitchDamping = 0.0f, rollDamping = 0.0f;
	int			i, touching = 0;

	Rc_Axes( car, car->yaw, axis );
	// the rays of the four springs, down along the body
	for ( i = 0; i < 4; i++ ) {
		reach[i] = RC_DROOP;
		Rc_Mount( car->origin, (const vec3_t *)axis, i, start );
		VectorMA( start, -( RC_DROOP + RC_WHEEL_RADIUS ), axis[2], end );
		CG_Trace( &tr, start, NULL, NULL, end, cg.clientNum, MASK_PLAYERSOLID );
		car->touch[i] = (qboolean)( tr.startsolid || tr.fraction < 1.0f );
		// (the mount itself in the ground: too deep, the stop lifts it out a little each step)
		if ( tr.startsolid )
			reach[i] = -2.0f;
		else if ( tr.fraction < 1.0f )
			reach[i] = tr.fraction * ( RC_DROOP + RC_WHEEL_RADIUS ) - RC_WHEEL_RADIUS;
		if ( car->touch[i] ) {
			VectorCopy( tr.startsolid ? start : tr.endpos, car->ground[i] );
			VectorCopy( tr.startsolid ? axis[2] : tr.plane.normal, car->groundNormal[i] );
			car->material[i] = tr.surfaceFlags & MATERIAL_MASK;
			deepest = Q_min( deepest, reach[i] );
			touching++;
		}
	}
	// A wheel further into the ground than its spring goes: the body stops there, as on a solid stop, without
	// springing back out of it (a hard landing would sink the wheels into the floor)
	if ( deepest < 0.0f ) {
		const float down = DotProduct( car->velocity, axis[2] );

		VectorMA( car->origin, -deepest, axis[2], car->origin );
		for ( i = 0; i < 4; i++ ) {
			if ( car->touch[i] )
				reach[i] -= deepest;
		}
		if ( down < 0.0f )
			VectorMA( car->velocity, -down, axis[2], car->velocity );
		car->pitchSpeed *= 0.5f;
		car->rollSpeed *= 0.5f;
	}
	// the springs push by how far they are pressed together, the stop at the end of them harder
	for ( i = 0; i < 4; i++ ) {
		float push, damper;

		car->reach[i] = Com_Clamp( 0.0f, RC_DROOP, reach[i] );
		if ( !car->touch[i] )
			continue;
		push = RC_SPRING * ( RC_REST - reach[i] );
		damper = RC_DAMPER;
		if ( reach[i] < 0.5f ) {
			push += RC_BUMP * ( 0.5f - reach[i] );
			damper += RC_BUMP_DAMPER;
		}
		push = Q_max( push, 0.0f );
		load += push;
		pitchTorque += push * rc.wheels[i][0];
		rollTorque += push * rc.wheels[i][1];
		damping += damper;
		pitchDamping += damper * rc.wheels[i][0] * rc.wheels[i][0];
		rollDamping += damper * rc.wheels[i][1] * rc.wheels[i][1];
	}
	// a landing after a while in the air thumps as hard as it came down
	if ( !touching ) {
		car->airborne += dt;
	} else {
		if ( car->airborne > 0.15f && car->velocity[2] < -150.0f )
			trap->S_StartSound( car->origin, cg.clientNum, CHAN_AUTO, rc.land[rand() & 1] );
		car->airborne = 0.0f;
	}
	VectorMA( car->velocity, load * dt, axis[2], car->velocity );
	car->velocity[2] -= RC_GRAVITY * dt;
	// The dampers take the speed of the body along its up axis away, implicitly: the speed over one and their
	// strength by the step, never more than there is however stiff they are (a step of an explicit damper would
	// throw the car back up from a hard landing). Their pitching and rolling below, the same way.
	{
		const float up = DotProduct( car->velocity, axis[2] );

		VectorMA( car->velocity, up / ( 1.0f + damping * dt ) - up, axis[2], car->velocity );
	}

	// the front wheels: less at speed, which keeps it on its wheels
	{
		const float most = RC_STEER * ( 1.0f - 0.5f * Q_min( 1.0f, fabsf( DotProduct( car->velocity, axis[0] ) ) / RC_SPEED ) );

		car->steer += Com_Clamp( -RC_STEER_SPEED * dt, RC_STEER_SPEED * dt, steering * most - car->steer );
	}

	Rc_Forward( car->yaw, forward );
	VectorSet( left, -forward[1], forward[0], 0.0f );
	if ( touching ) {
		const float	grip = Com_Clamp( 0.0f, 2.0f, load / RC_GRAVITY );
		float		ahead = DotProduct( car->velocity, forward ), across = DotProduct( car->velocity, left );
		const float	before = ahead;
		float		mu, most, take, turn;

		car->braking = (qboolean)( ( throttle < 0.0f && ahead > 10.0f ) || ( throttle > 0.0f && ahead < -10.0f ) );
		if ( throttle > 0.0f )
			ahead += ( ahead < -5.0f ? RC_BRAKE : RC_ACCEL * Q_max( 0.0f, 1.0f - ahead / RC_SPEED ) ) * grip * dt;
		else if ( throttle < 0.0f )
			ahead -= ( ahead > 5.0f ? RC_BRAKE : RC_ACCEL * Q_max( 0.0f, 1.0f + ahead / RC_REVERSE ) ) * grip * dt;
		else
			ahead -= ahead * Q_min( 1.0f, RC_DRAG * dt );
		if ( handbrake )
			ahead -= Com_Clamp( -RC_HANDBRAKE * dt, RC_HANDBRAKE * dt, ahead );

		// sideways: at most as hard as the load on the tires allows, past that it slides
		mu = handbrake ? RC_MU_HANDBRAKE : ( car->sliding ? RC_MU_SLIDE : RC_MU );
		most = mu * RC_GRAVITY * grip;
		take = across * RC_GRIP;
		car->sliding = (qboolean)( fabsf( take ) > most );
		across -= Com_Clamp( -most, most, take ) * dt;

		// The turn the front wheels ask for, less of it while it slides, and no more than the tires can hold it in
		// (speed times turn is the pull out of the turn): at speed it pushes wide. The handbrake lets the back swing.
		turn = RAD2DEG( ahead * tanf( DEG2RAD( car->steer ) ) / base ) * ( car->sliding ? 0.7f : 1.0f );
		if ( !handbrake && fabsf( ahead ) > 1.0f )
			turn = Com_Clamp( -RAD2DEG( most / fabsf( ahead ) ), RAD2DEG( most / fabsf( ahead ) ), turn );
		car->yawSpeed += ( turn - car->yawSpeed ) * Q_min( 1.0f, ( handbrake ? 3.0f : 10.0f ) * dt );

		// the weight shifts: back as it speeds up, forward as it brakes, out of the turn
		pitchTorque += ( ahead - before ) / dt * RC_HEIGHT;
		rollTorque += Com_Clamp( -most, most, ahead * DEG2RAD( car->yawSpeed ) ) * RC_HEIGHT;

		car->velocity[0] = forward[0] * ahead + left[0] * across;
		car->velocity[1] = forward[1] * ahead + left[1] * across;
		if ( rc.keys[RC_JUMP] && touching >= 3 )
			VectorMA( car->velocity, RC_HOP, axis[2], car->velocity );
	} else {
		// in the air: the throttle tips the nose up, the brakes down, the steering turns it slowly
		car->braking = qfalse;
		car->pitchSpeed += throttle * RC_AIR_TILT * dt;
		car->yawSpeed += steering * RC_AIR_TURN * dt;
		car->yawSpeed -= car->yawSpeed * Q_min( 1.0f, 0.5f * dt );
	}
	rc.keys[RC_JUMP] = qfalse;

	car->pitchSpeed += pitchTorque / RC_PITCH_INERTIA * dt;
	car->rollSpeed += rollTorque / RC_ROLL_INERTIA * dt;
	car->pitchSpeed /= 1.0f + pitchDamping / RC_PITCH_INERTIA * dt;
	car->rollSpeed /= 1.0f + rollDamping / RC_ROLL_INERTIA * dt;
	car->pitchSpeed -= car->pitchSpeed * Q_min( 1.0f, RC_TILT_DAMP * dt );
	car->rollSpeed -= car->rollSpeed * Q_min( 1.0f, RC_TILT_DAMP * dt );
	car->pitch += car->pitchSpeed * dt;
	car->roll += car->rollSpeed * dt;
	if ( fabsf( car->pitch ) > RC_TILT_MAX ) {
		car->pitch = Com_Clamp( -RC_TILT_MAX, RC_TILT_MAX, car->pitch );
		car->pitchSpeed = 0.0f;
	}
	if ( fabsf( car->roll ) > RC_TILT_MAX ) {
		car->roll = Com_Clamp( -RC_TILT_MAX, RC_TILT_MAX, car->roll );
		car->rollSpeed = 0.0f;
	}
	car->yaw = AngleNormalize360( car->yaw + car->yawSpeed * dt );

	Rc_Move( car, dt );
	if ( car->origin[2] < -TOY_MAX_COORD + 1024 )
		Rc_Leave();
}

/*
=================
CG_RcFrame

Once a frame before the view is worked out: the own car drives RC_HZ steps a second (qtrue: the view follows it)
=================
*/
qboolean CG_RcFrame( void ) {
	const float	step = 1.0f / RC_HZ;
	rcCar_t		*car;

	if ( !rc.driving )
		return qfalse;
	// a menu or the console took the keys, the player died or left the game
	if ( !CG_ToyCanUse() || !( trap->Key_GetCatcher() & KEYCATCH_CGAME ) ) {
		Rc_Leave();
		return qfalse;
	}
	car = &rc.cars[cg.clientNum];
	// (a long frame is no flood of steps)
	car->pending = Q_min( car->pending + cg.frametime * 0.001f, 0.1f );
	while ( rc.driving && car->pending >= step ) {
		Rc_Step( car, step );
		car->pending -= step;
	}
	if ( rc.driving && ( !rc.sendTime || cg.time - rc.sendTime >= RC_SEND_MSEC || cg.time < rc.sendTime ) )
		Rc_Send( TOY_RC );
	return rc.driving;
}

/*
=================
CG_RcView

After the view is worked out: the camera behind the car, following its turn a little late, turned by the mouse,
held off the walls
=================
*/
void CG_RcView( void ) {
	static const vec3_t	mins = { -4.0f, -4.0f, -4.0f }, maxs = { 4.0f, 4.0f, 4.0f };
	const rcCar_t		*car = &rc.cars[cg.clientNum];
	vec3_t				target, angles, forward, eye;
	trace_t				tr;

	if ( !rc.driving )
		return;
	rc.cameraYaw += AngleNormalize180( car->yaw - rc.cameraYaw ) * Q_min( 1.0f, RC_CAMERA_FOLLOW * cg.frametime * 0.001f );
	VectorCopy( car->origin, target );
	target[2] += RC_CAMERA_HEIGHT - RC_RIDE;
	VectorSet( angles, rc.lookPitch, rc.cameraYaw + rc.lookYaw, 0.0f );
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
	case A_SHIFT:								rc.keys[RC_HANDBRAKE_KEY] = down; break;
	case A_SPACE:								if ( down ) rc.keys[RC_JUMP] = qtrue; break;
	case A_MOUSE2:								if ( down ) rc.lookYaw = 0.0f; break;
	default:									break;
	}
	return qtrue;
}

/*
=================
Rc_Settle

Somebody else's car, where it is drawn: its body on the ground under its four wheels, which says how it pitches and
rolls, leaning out of its turn as the own car does; the wheels on the ground
=================
*/
static void Rc_Settle( rcCar_t *car, vec3_t origin, float yaw, float dt ) {
	vec3_t	axis[3], start, end;
	trace_t	tr;
	float	ground[4], mid = 0.0f, pitch, roll, lean;
	int		i, found = 0;

	Rc_Axes( car, yaw, axis );
	for ( i = 0; i < 4; i++ ) {
		VectorMA( origin, rc.wheels[i][0], axis[0], start );
		VectorMA( start, rc.wheels[i][1], axis[1], start );
		start[2] = origin[2] + RC_REST + 8.0f;
		VectorCopy( start, end );
		end[2] -= 40.0f;
		CG_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, MASK_PLAYERSOLID );
		ground[i] = tr.fraction < 1.0f && !tr.startsolid ? tr.endpos[2] : origin[2] - RC_RIDE;
		car->touch[i] = (qboolean)( tr.fraction < 1.0f && !tr.startsolid && tr.endpos[2] > origin[2] - RC_RIDE - RC_DROOP );
		if ( car->touch[i] ) {
			VectorCopy( tr.endpos, car->ground[i] );
			VectorCopy( tr.plane.normal, car->groundNormal[i] );
			car->material[i] = tr.surfaceFlags & MATERIAL_MASK;
		}
		mid += ground[i];
		found += tr.fraction < 1.0f;
	}
	mid *= 0.25f;
	if ( found >= 3 )
		origin[2] = mid + RC_RIDE;
	pitch = atanf( ( ( ground[0] + ground[1] ) - ( ground[2] + ground[3] ) ) * 0.5f / ( rc.wheels[0][0] - rc.wheels[2][0] ) );
	roll = atanf( ( ( ground[0] + ground[2] ) - ( ground[1] + ground[3] ) ) * 0.5f / ( rc.wheels[0][1] - rc.wheels[1][1] ) );
	// out of the turn by as much as the springs of the own car let it (sideways speed times height over their stiffness)
	lean = car->heardSpeed * DEG2RAD( car->heardTurn ) * RC_HEIGHT / ( RC_SPRING * 4.0f * rc.wheels[0][1] * rc.wheels[0][1] );
	roll += Com_Clamp( -0.25f, 0.25f, lean );
	car->pitch += ( pitch - car->pitch ) * Q_min( 1.0f, 10.0f * dt );
	car->roll += ( roll - car->roll ) * Q_min( 1.0f, 10.0f * dt );
	Rc_Axes( car, yaw, axis );
	for ( i = 0; i < 4; i++ ) {
		Rc_Mount( origin, (const vec3_t *)axis, i, start );
		car->reach[i] = Com_Clamp( 0.0f, RC_DROOP, start[2] - ground[i] - RC_WHEEL_RADIUS );
	}
}

// A piece of a mark of a tire from "from" to "to" on the ground that faces "normal": a strip as wide as the tire, cut
// to the level once (R_MarkFragments, as CG_ImpactMark does), the picture across it and along it from "along" on
static void Rc_Track( const vec3_t from, const vec3_t to, const vec3_t normal, qhandle_t shader, float strength, float along ) {
	vec3_t			dir, side, points[4], projection, markPoints[RC_MARK_POINTS];
	markFragment_t	fragments[RC_MARK_FRAGMENTS];
	float			length;
	int				i, j, count;

	VectorSubtract( to, from, dir );
	VectorMA( dir, -DotProduct( dir, normal ), normal, dir );
	length = VectorNormalize( dir );
	if ( length < 0.5f )
		return;
	CrossProduct( normal, dir, side );
	// (a little off the ground, cut down into it)
	for ( i = 0; i < 4; i++ ) {
		VectorMA( i < 2 ? from : to, ( i == 0 || i == 3 ) ? -RC_TIRE_HALF : RC_TIRE_HALF, side, points[i] );
		VectorMA( points[i], 4.0f, normal, points[i] );
	}
	VectorScale( normal, -8.0f, projection );
	count = trap->R_MarkFragments( 4, (const vec3_t *)points, projection, RC_MARK_POINTS, markPoints[0], RC_MARK_FRAGMENTS, fragments );
	for ( i = 0; i < count; i++ ) {
		rcTrack_t *track = &rcTracks[rcNextTrack];

		rcNextTrack = ( rcNextTrack + 1 ) % RC_TRACKS;
		track->time = cg.time ? cg.time : 1;
		track->shader = shader;
		track->strength = strength;
		track->numVerts = Q_min( fragments[i].numPoints, MAX_VERTS_ON_POLY );
		for ( j = 0; j < track->numVerts; j++ ) {
			polyVert_t	*v = &track->verts[j];
			vec3_t		delta;

			VectorCopy( markPoints[fragments[i].firstPoint + j], v->xyz );
			VectorSubtract( v->xyz, from, delta );
			v->st[0] = 0.5f + DotProduct( delta, side ) / ( 2.0f * RC_TIRE_HALF );
			v->st[1] = ( along + DotProduct( delta, dir ) ) / RC_TRACK_TILE;
			v->modulate[3] = 255;
		}
	}
}

// the mark of one wheel goes on to where it touches now, a piece at a time; a new kind of mark starts a new one
static void Rc_Mark( rcCar_t *car, int wheel, int kind ) {
	const float	step = kind == RC_MARK_WET ? RC_WET_STEP : RC_TRACK_STEP;
	float		dist;

	if ( kind != car->markKind[wheel] || kind == RC_MARK_NONE ) {
		car->markKind[wheel] = kind;
		car->markAlong[wheel] = 0.0f;
		VectorCopy( car->ground[wheel], car->markFrom[wheel] );
		return;
	}
	dist = Distance( car->markFrom[wheel], car->ground[wheel] );
	if ( dist < step )
		return;
	// (a jump far away, or the car was heard somewhere else: no mark across)
	if ( dist < step * 4.0f ) {
		if ( kind == RC_MARK_WET ) {
			CG_WaterTrack( car->markFrom[wheel], car->ground[wheel], car->groundNormal[wheel], RC_TIRE_HALF, car->wetTire[wheel] );
			car->wetTire[wheel] = Q_max( 0.0f, car->wetTire[wheel] - dist / RC_WET_REACH );
		} else {
			Rc_Track( car->markFrom[wheel], car->ground[wheel], car->groundNormal[wheel],
				kind == RC_MARK_TRACK ? rc.trackShader : rc.skidShader, kind == RC_MARK_TRACK ? 0.5f : 0.45f, car->markAlong[wheel] );
		}
		car->markAlong[wheel] += dist;
	}
	VectorCopy( car->ground[wheel], car->markFrom[wheel] );
}

/*
=================
Rc_Wheels

What the wheels of a car do to the ground, each frame where it is drawn (speed: along the car). Standing water of the
water gun sprays and makes its tire wet; a wet rear tire leaves a wet track. Sand, dirt, mud and snow take the tread
and throw up dust; hard ground takes rubber from the rear tires and smokes while the car slides or brakes hard.
=================
*/
static void Rc_Wheels( rcCar_t *car, float speed ) {
	const qboolean	skidding = (qboolean)( car->sliding || ( car->braking && fabsf( speed ) > 150.0f ) );
	int				i;

	for ( i = 0; i < 4; i++ ) {
		const int		material = car->material[i];
		const qboolean	soft = (qboolean)( material == MATERIAL_SAND || material == MATERIAL_DIRT || material == MATERIAL_MUD
			|| material == MATERIAL_SNOW || material == MATERIAL_GRAVEL );
		const qboolean	due = (qboolean)( cg.time - car->effectTime[i] >= RC_EFFECT_MSEC || cg.time < car->effectTime[i] );
		float			wet;
		int				kind, fx = -1;

		if ( !car->touch[i] ) {
			Rc_Mark( car, i, RC_MARK_NONE );
			continue;
		}
		wet = CG_WaterWheel( car->ground[i], car->groundNormal[i], car->velocity, (qboolean)( due && fabsf( speed ) > 40.0f ) );
		car->wetTire[i] = Q_max( car->wetTire[i], wet );
		if ( wet >= 1.0f )
			kind = RC_MARK_NONE;
		else if ( car->wetTire[i] > 0.0f && i >= 2 )
			kind = RC_MARK_WET;
		else if ( soft )
			kind = RC_MARK_TRACK;
		else if ( skidding && i >= 2 )
			kind = RC_MARK_SKID;
		else
			kind = RC_MARK_NONE;
		Rc_Mark( car, i, kind );

		if ( wet > 0.0f || !due )
			continue;
		if ( soft && ( fabsf( speed ) > 120.0f || car->sliding ) ) {
			fx = material == MATERIAL_MUD ? cgs.effects.footstepMud : ( material == MATERIAL_SNOW ? cgs.effects.footstepSnow
				: cgs.effects.footstepSand );
		} else if ( !soft && skidding && fabsf( speed ) > 60.0f ) {
			fx = rc.smokeFx;
		}
		if ( fx > 0 ) {
			vec3_t dir;

			// up off the ground and back from where the wheel goes
			VectorNormalize2( car->velocity, dir );
			VectorScale( dir, -0.6f, dir );
			VectorAdd( dir, car->groundNormal[i], dir );
			VectorNormalize( dir );
			trap->FX_PlayEffectID( fx, car->ground[i], dir, -1, -1, qfalse );
			car->effectTime[i] = cg.time;
		}
	}
}

// the marks of the tires, each frame; the last third of its time a mark fades out
static void Rc_AddTracks( void ) {
	int i, j;

	for ( i = 0; i < RC_TRACKS; i++ ) {
		rcTrack_t	*track = &rcTracks[i];
		const int	age = cg.time - track->time;
		byte		shade;

		if ( !track->time )
			continue;
		if ( age >= RC_TRACK_MSEC || age < 0 ) {
			track->time = 0;
			continue;
		}
		shade = (byte)( 255.0f * track->strength * Q_min( 1.0f, ( RC_TRACK_MSEC - age ) / ( RC_TRACK_MSEC / 3.0f ) ) );
		for ( j = 0; j < track->numVerts; j++ )
			track->verts[j].modulate[0] = track->verts[j].modulate[1] = track->verts[j].modulate[2] = shade;
		trap->R_AddPolysToScene( track->shader, track->numVerts, track->verts, 1 );
	}
}

// a looping sound of a car as loud as level (0 to 1); an engine without loops at their own volume plays only the
// loud ones, at full volume
static void Rc_Loop( int client, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx, float level ) {
	if ( rc.loopVolume )
		trap->ext.S_AddLoopingSoundVolume( client, origin, velocity, sfx, (int)( Com_Clamp( 0.0f, 1.0f, level ) * 255.0f ) );
	else if ( level >= 0.5f )
		trap->S_AddLoopingSound( client, origin, velocity, sfx );
}

/*
=================
Rc_Sounds

What a car sounds like this frame (speed: along the car). The motor: of the loops of its four speeds the two its speed
lies between, crossfaded with equal power, louder under throttle (the own car: the keys; somebody else's: while it
speeds up). The tires: the scrub of rubber while it slides on hard ground, the crunch of dirt and gravel by its speed.
The servo buzzes as the front wheels swing.
=================
*/
static void Rc_Sounds( rcCar_t *car, int client, const vec3_t origin, float speed, qboolean own ) {
	static const float	speeds[4] = { 70.0f, 180.0f, 310.0f, 450.0f };		// where each loop sounds as made
	const float			fast = fabsf( speed );
	const float			throttle = own ? ( ( rc.keys[RC_FORWARD] || rc.keys[RC_BACK] ) ? 1.0f : 0.0f ) : ( car->braking ? 0.0f : 0.7f );
	const float			level = ( 0.35f + 0.65f * throttle ) * Q_min( 1.0f, 0.25f + fast / 120.0f );
	float				weights[4] = { 0.0f, 0.0f, 0.0f, 0.0f }, hard = 0.0f, soft = 0.0f;
	int					i;

	if ( fast <= speeds[0] ) {
		weights[0] = fast / speeds[0];
	} else if ( fast >= speeds[3] ) {
		weights[3] = 1.0f;
	} else {
		for ( i = 0; i < 3; i++ ) {
			if ( fast < speeds[i + 1] ) {
				const float f = ( fast - speeds[i] ) / ( speeds[i + 1] - speeds[i] );

				weights[i] = sqrtf( 1.0f - f );
				weights[i + 1] = sqrtf( f );
				break;
			}
		}
	}
	for ( i = 0; i < 4; i++ ) {
		if ( weights[i] > 0.01f )
			Rc_Loop( client, origin, car->velocity, rc.motor[i], weights[i] * level );
	}

	for ( i = 0; i < 4; i++ ) {
		const int m = car->material[i];

		if ( !car->touch[i] )
			continue;
		if ( m == MATERIAL_SAND || m == MATERIAL_DIRT || m == MATERIAL_MUD || m == MATERIAL_SNOW || m == MATERIAL_GRAVEL )
			soft += 0.25f;
		else
			hard += 0.25f;
	}
	if ( car->sliding && hard > 0.0f )
		Rc_Loop( client, origin, car->velocity, rc.scrub, hard * Q_min( 1.0f, fast / 200.0f + 0.3f ) );
	if ( soft > 0.0f && fast > 20.0f )
		Rc_Loop( client, origin, car->velocity, rc.crunch, soft * Q_min( 1.0f, fast / 300.0f ) );

	if ( fabsf( car->steer - car->servoFrom ) > 10.0f && ( cg.time - car->servoTime > 200 || cg.time < car->servoTime ) ) {
		trap->S_StartSound( (float *)origin, client, CHAN_AUTO, rc.servo );
		car->servoFrom = car->steer;
		car->servoTime = cg.time;
	}
}

// a lamp of a car: a glowing sprite at "at", which the glow of the renderer spreads
static void Rc_Lamp( const vec3_t at, float radius, int r, int g, int b ) {
	refEntity_t ent;

	memset( &ent, 0, sizeof( ent ) );
	ent.reType = RT_SPRITE;
	ent.customShader = rc.lampShader;
	ent.radius = radius;
	VectorCopy( at, ent.origin );
	ent.shaderRGBA[0] = (byte)r;
	ent.shaderRGBA[1] = (byte)g;
	ent.shaderRGBA[2] = (byte)b;
	ent.shaderRGBA[3] = 255;
	trap->R_AddRefEntityToScene( &ent );
}

// A car where it is (its body at origin, pointing to yaw): the body pitched and rolled on its springs, the wheels as
// far down as they reach, rolled and the front ones turned, two lamps at the front that light the way and two at the
// back, brighter while it brakes, and its shadow
static void Rc_AddCar( const rcCar_t *car, const vec3_t origin, float yaw ) {
	refEntity_t	body, wheel;
	vec3_t		angles, at;
	int			i;

	memset( &body, 0, sizeof( body ) );
	body.reType = RT_MODEL;
	body.hModel = rc.body;
	VectorCopy( origin, body.origin );
	VectorCopy( body.origin, body.lightingOrigin );
	Rc_Axes( car, yaw, body.axis );
	body.renderfx = RF_LIGHTING_ORIGIN;
	trap->R_AddRefEntityToScene( &body );

	for ( i = 0; i < 4; i++ ) {
		const qboolean right = (qboolean)( i & 1 );

		vec3_t local[3];

		wheel = body;
		wheel.hModel = rc.wheel;
		wheel.customSkin = car->blurred ? rc.blurSkin : 0;
		Rc_Mount( body.origin, (const vec3_t *)body.axis, i, wheel.origin );
		VectorMA( wheel.origin, -car->reach[i], body.axis[2], wheel.origin );
		// The wheel in the axes of the body: rolled, a front one turned, a right one the left one turned round (its hub
		// cap out, and it rolls the other way in its own axes); then into the level with the body, however it pitches
		// and rolls.
		VectorSet( angles, right ? -car->spin : car->spin, ( i < 2 ? car->steer : 0.0f ) + ( right ? 180.0f : 0.0f ), 0.0f );
		AnglesToAxis( angles, local );
		MatrixMultiply( local, body.axis, wheel.axis );
		trap->R_AddRefEntityToScene( &wheel );
	}

	for ( i = -1; i <= 1; i += 2 ) {
		VectorMA( body.origin, rc.head[0] + 0.3f, body.axis[0], at );
		VectorMA( at, rc.head[1] + i * RC_LAMP_GAP, body.axis[1], at );
		VectorMA( at, rc.head[2], body.axis[2], at );
		Rc_Lamp( at, 2.2f, 255, 240, 205 );
		VectorMA( body.origin, rc.tail[0] - 0.3f, body.axis[0], at );
		VectorMA( at, rc.tail[1] + i * RC_LAMP_GAP, body.axis[1], at );
		VectorMA( at, rc.tail[2], body.axis[2], at );
		if ( car->braking )
			Rc_Lamp( at, 1.9f, 255, 40, 25 );
		else
			Rc_Lamp( at, 1.6f, 150, 20, 12 );
	}
	// the light of the lamps on the floor ahead, and red behind while it brakes
	VectorMA( body.origin, rc.head[0] + 22.0f, body.axis[0], at );
	trap->R_AddLightToScene( at, 80.0f, 1.0f, 0.92f, 0.76f );
	if ( car->braking ) {
		VectorMA( body.origin, rc.tail[0] - 10.0f, body.axis[0], at );
		trap->R_AddLightToScene( at, 40.0f, 1.0f, 0.12f, 0.06f );
	}
	// a soft shadow on the ground under it
	{
		vec3_t	end;
		trace_t	tr;

		VectorSet( end, origin[0], origin[1], origin[2] - RC_RIDE - 32.0f );
		CG_Trace( &tr, body.origin, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID );
		// (the shadow takes how dark it is as its colour, as the one of a player)
		if ( tr.fraction < 1.0f && !tr.startsolid ) {
			const float dark = 0.75f * ( 1.0f - tr.fraction );

			CG_ImpactMark( cgs.media.shadowMarkShader, tr.endpos, tr.plane.normal, yaw, dark, dark, dark, 1.0f, qfalse, 15.0f, qtrue );
		}
	}
}

// While the scene is built: every car, the own one where it drives, the others rolled on from where they were heard
void CG_RcScene( void ) {
	const float	dt = Q_min( cg.frametime * 0.001f, 0.1f );
	int			i;

	if ( !rc.registered )
		return;
	Rc_AddTracks();
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		rcCar_t	*car = &rc.cars[i];
		vec3_t	origin, forward;
		float	yaw = car->yaw, speed;

		if ( !car->active )
			continue;
		VectorCopy( car->origin, origin );
		if ( i != cg.clientNum ) {
			if ( !v_toys.integer || cg.time - car->heardTime > RC_LOST_MSEC || cg.time < car->heardTime ) {
				car->active = qfalse;
				continue;
			}
			VectorScale( car->error, Q_max( 0.0f, 1.0f - 8.0f * dt ), car->error );
			Rc_Heard( car, origin, &yaw );
			Rc_Settle( car, origin, yaw, dt );
		}
		Rc_Forward( yaw, forward );
		speed = DotProduct( car->velocity, forward );
		car->spin = fmodf( car->spin + speed * dt / RC_WHEEL_RADIUS * ( 180.0f / M_PI ), 360.0f );
		if ( i != cg.clientNum ) {
			// the front wheels as far as the turn it made asks (wheelbase times turn over speed)
			const float base = rc.wheels[0][0] - rc.wheels[2][0];
			const float target = Com_Clamp( -RC_STEER, RC_STEER, RAD2DEG( atanf( base * DEG2RAD( car->heardTurn )
				/ Q_max( fabsf( speed ), 30.0f ) ) ) * ( speed < 0.0f ? -1.0f : 1.0f ) );

			car->steer += Com_Clamp( -RC_STEER_SPEED * dt, RC_STEER_SPEED * dt, target - car->steer );
			// it slides where its turn pulls harder than the tires of the own car hold
			car->sliding = (qboolean)( fabsf( speed * DEG2RAD( car->heardTurn ) ) > 0.9f * RC_MU * RC_GRAVITY );
		}
		car->blurred = (qboolean)( fabsf( speed ) > ( car->blurred ? RC_BLUR_SPEED - 30.0f : RC_BLUR_SPEED ) );
		Rc_Wheels( car, speed );
		Rc_AddCar( car, origin, yaw );
		// (loops: they have to be said every frame)
		Rc_Sounds( car, i, origin, speed, (qboolean)( i == cg.clientNum ) );
	}
}

// what the keys do, while driving
void CG_RcDraw( void ) {
	vec4_t		color;
	const char	*text;

	if ( !rc.driving )
		return;
	VectorSet4( color, 1.0f, 1.0f, 1.0f, 0.75f );
	text = CG_VisionText( "W/S: drive. A/D: steer. Shift: handbrake. Space: hop. Mouse: look. Esc: put it away." );
	CG_Text_Paint( 320.0f - CG_Text_Width( text, 0.45f, FONT_SMALL ) * 0.5f, 450.0f, 0.45f, color, text, 0, 0, ITEM_TEXTSTYLE_SHADOWED,
		FONT_SMALL );
}

void CG_RcInit( void ) {
	memset( &rc, 0, sizeof( rc ) );
}
