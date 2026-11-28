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

// cg_viewweapon.c -- the weapon in first person moves (issue #22): each shot kicks it back, by the weapon and harder
// after a full charge, it lags behind when the view turns, and it is lowered when the player stands close to a wall.
// Every weapon is put away and drawn with a move of its own (the game lowers and raises it in its weaponstate, the
// drawn weapon only follows that, the time stays the game's). Only the drawn weapon moves, never where the player
// aims. All of it goes with the stock cg_weaponBob (0: off), it has no setting of its own. The saber and the fists
// have no weapon in first person, and the scope of the disruptor hides it.

#include "cgame/cg_local.h"
#include "cgame/weapons/cg_viewweapon.h"
#include "cgame/portal/cg_portal_draw.h"

#define KICK_RISE_MSEC		18.0f		// a kick comes this fast
#define KICK_FALL_MSEC		110.0f		//	and goes back this slowly, so the shots of a repeater pile up a little
#define KICK_MAX_BACK		7.0f		// units, however many shots are on top of each other
#define KICK_MAX_PITCH		10.0f		// degrees
#define KICK_SIDE			0.2f		// of the pitch, to a random side
#define KICK_LIFT			0.15f		// of how far it goes back, it goes up
#define CHARGE_PART			0.5f		// a charged shot kicks up to this much harder
#define CHARGE_FULL			2.0f		//	and a fully charged one this many times

#define SWAY_SCALE			0.012f		// degrees it lags per degree a second the view turns
#define SWAY_MAX			3.5f
#define SWAY_SMOOTH_MSEC	70.0f		// a mouse is read in steps, at a high frame rate most frames see no turn
#define SWAY_ROLL			0.6f		// of the lag to the side, it leans into the turn
#define SWAY_JUMP			90.0f		// degrees in one frame: the view jumped (spawn, teleporter), it did not turn

#define WALL_REACH			40.0f		// units from the eye
#define WALL_NEAR			18.0f		//	fully lowered (the box of a player is 15 to its side)
#define WALL_FAR			34.0f		//	not lowered
#define WALL_MSEC			90.0f		// how fast it goes down and comes up again
#define WALL_PITCH			24.0f		// degrees the muzzle points down
#define WALL_BACK			4.0f		// units it is pulled in
#define WALL_DOWN			3.0f

#define VW_GAP_MSEC			250			// longer without a frame of the weapon: it was away, start over

// where the hands hold a weapon, from the eye along the axes of the weapon (forward, left, up): the poses turn it there
static const vec3_t VW_GRIP = { 14.0f, -6.0f, -8.0f };

// a pose of the drawn weapon, from its place: units down the view, toward the eye and to the right, degrees the muzzle
// goes down, to the left and the weapon rolls to the right
typedef struct {
	float	down, back, side;
	float	pitch, yaw, roll;
} vwPose_t;

// where each weapon is when it is put away, and how far its draw swings past its place before it settles
typedef struct {
	vwPose_t	away;
	float		overshoot;
} vwSwitch_t;

typedef enum {
	KICK_BACK,
	KICK_PITCH,
	KICK_YAW,
	KICK_AXES
} kickAxis_t;

static struct {
	int		time;				// cg.time of the last frame it moved, 0: none
	vec3_t	angles;				// of the view then
	float	rate[2];			// degrees a second the view turns, pitch and yaw, smoothed
	float	wall;				// 0 to 1, lowered at a wall
	float	slow[KICK_AXES];	// each kick lies in both, the slow one minus the fast one is how far it is out
	float	fast[KICK_AXES];
	float	peak;				// the most that difference gets for a kick of 1
	// the change of weapons: the weaponstate of the game, since when, and how long it takes
	int		state;
	int		stateTotal;
} vw;

void CG_ViewWeaponInit( void ) {
	const float top = logf( KICK_FALL_MSEC / KICK_RISE_MSEC ) * KICK_RISE_MSEC * KICK_FALL_MSEC / ( KICK_FALL_MSEC - KICK_RISE_MSEC );

	memset( &vw, 0, sizeof( vw ) );
	vw.peak = expf( -top / KICK_FALL_MSEC ) - expf( -top / KICK_RISE_MSEC );
}

static void ViewWeapon_ClearKicks( void ) {
	memset( vw.slow, 0, sizeof( vw.slow ) );
	memset( vw.fast, 0, sizeof( vw.fast ) );
}

// how far a shot kicks the weapon back (units) and up (degrees), 0: it throws or swings, nothing kicks
static void ViewWeapon_Kick( int weapon, qboolean altFire, float *back, float *pitch ) {
	*back = *pitch = 0.0f;
	switch ( weapon ) {
	case WP_BRYAR_PISTOL:
	case WP_BRYAR_OLD:
		*back = 1.2f; *pitch = 1.8f;
		break;
	case WP_BLASTER:
		*back = altFire ? 0.8f : 1.1f; *pitch = altFire ? 1.1f : 1.5f;
		break;
	case WP_DISRUPTOR:
		*back = altFire ? 2.6f : 2.0f; *pitch = altFire ? 3.8f : 3.0f;
		break;
	case WP_BOWCASTER:
		*back = 3.0f; *pitch = 4.8f;
		break;
	case WP_REPEATER:
		*back = altFire ? 3.2f : 0.55f; *pitch = altFire ? 5.0f : 0.7f;
		break;
	case WP_DEMP2:
		*back = altFire ? 2.4f : 1.5f; *pitch = altFire ? 3.6f : 2.4f;
		break;
	case WP_FLECHETTE:
		*back = altFire ? 3.4f : 3.0f; *pitch = altFire ? 5.6f : 5.2f;
		break;
	case WP_ROCKET_LAUNCHER:
		*back = 4.5f; *pitch = 6.5f;
		break;
	case WP_CONCUSSION:
		*back = altFire ? 2.4f : 4.0f; *pitch = altFire ? 3.6f : 6.0f;
		break;
	default:
		break;
	}
}

// How much harder a shot kicks for how long it was charged. The game counts the charge in steps (g_weapon.c): the
// pistol and the bowcaster are full after 5 of 200 ms, the scope of the disruptor after 30 of 50 ms (as the bar of
// cg_draw.c), the DEMP2 after 3 of 700 ms.
static float ViewWeapon_Charge( int weapon, qboolean altFire ) {
	float full, part;

	if ( ( weapon == WP_BRYAR_PISTOL || weapon == WP_BRYAR_OLD ) && altFire ) {
		full = 1000.0f;
	} else if ( weapon == WP_BOWCASTER && !altFire ) {
		full = 1000.0f;
	} else if ( weapon == WP_DISRUPTOR && altFire ) {
		full = 1500.0f;
	} else if ( weapon == WP_DEMP2 && altFire ) {
		full = 2100.0f;
	} else {
		return 1.0f;
	}
	// (the predicted state: the entity of the own player may still have the charge before this one)
	part = ( cg.time - cg.predictedPlayerState.weaponChargeTime ) / full;
	if ( part >= 1.0f ) {
		return CHARGE_FULL;
	}
	return 1.0f + CHARGE_PART * Com_Clamp( 0.0f, 1.0f, part );
}

void CG_ViewWeaponFired( int weapon, qboolean altFire ) {
	float back, pitch, charge, side;

	if ( !cg_weaponBob.value ) {
		return;
	}
	ViewWeapon_Kick( weapon, altFire, &back, &pitch );
	if ( back <= 0.0f ) {
		return;
	}
	// the weapon was away for a while: what kicked it then is long over
	if ( !vw.time || cg.time < vw.time || cg.time - vw.time > VW_GAP_MSEC ) {
		ViewWeapon_ClearKicks();
	}
	charge = ViewWeapon_Charge( weapon, altFire );
	side = flrand( -1.0f, 1.0f ) * KICK_SIDE * pitch;
	vw.slow[KICK_BACK] += back * charge;
	vw.fast[KICK_BACK] += back * charge;
	vw.slow[KICK_PITCH] += pitch * charge;
	vw.fast[KICK_PITCH] += pitch * charge;
	vw.slow[KICK_YAW] += side;
	vw.fast[KICK_YAW] += side;
}

// what is in front of the eye: 1 a wall right there, 0 none in reach. The floor and the ceiling are no wall.
static float ViewWeapon_WallAhead( void ) {
	static const vec3_t	mins = { -3.0f, -3.0f, -3.0f }, maxs = { 3.0f, 3.0f, 3.0f };
	trace_t				tr;
	vec3_t				end;

	VectorMA( cg.refdef.vieworg, WALL_REACH, cg.refdef.viewaxis[0], end );
	CG_Trace( &tr, cg.refdef.vieworg, mins, maxs, end, cg.predictedPlayerState.clientNum, MASK_SOLID );
	if ( tr.startsolid || tr.fraction >= 1.0f || fabsf( tr.plane.normal[2] ) > 0.7f || CG_PortalOpeningAt( tr.endpos, mins, maxs ) ) {
		return 0.0f;
	}
	return Com_Clamp( 0.0f, 1.0f, ( WALL_FAR - tr.fraction * WALL_REACH ) / ( WALL_FAR - WALL_NEAR ) );
}

// one step of dt msec: the kicks go back, the turn of the view and the wall are followed
static void ViewWeapon_Step( int dt ) {
	const float	fall = expf( -dt / KICK_FALL_MSEC ), rise = expf( -dt / KICK_RISE_MSEC );
	const float	follow = 1.0f - expf( -dt / SWAY_SMOOTH_MSEC );
	const int	axes[2] = { PITCH, YAW };
	int			i;

	for ( i = 0; i < KICK_AXES; i++ ) {
		vw.slow[i] *= fall;
		vw.fast[i] *= rise;
	}
	for ( i = 0; i < 2; i++ ) {
		const float turned = AngleSubtract( cg.refdef.viewangles[axes[i]], vw.angles[axes[i]] );

		if ( fabsf( turned ) > SWAY_JUMP ) {
			vw.rate[i] = 0.0f;
		} else {
			vw.rate[i] += ( turned * 1000.0f / dt - vw.rate[i] ) * follow;
		}
	}
	VectorCopy( cg.refdef.viewangles, vw.angles );
	vw.wall += ( ViewWeapon_WallAhead() - vw.wall ) * ( 1.0f - expf( -dt / WALL_MSEC ) );
}

static const vwSwitch_t *ViewWeapon_Switch( int weapon ) {
	//																  down  back side  pitch  yaw  roll   overshoot
	static const vwSwitch_t	baton =		{ { 10.0f, 2.0f,  3.0f,  20.0f,   0.0f,  35.0f }, 1.2f };
	static const vwSwitch_t	pistol =	{ {  9.0f, 3.0f,  4.0f,  30.0f,  15.0f,  40.0f }, 1.6f };	// a flick
	static const vwSwitch_t	blaster =	{ { 12.0f, 4.0f, -2.0f,  35.0f, -10.0f, -20.0f }, 1.2f };
	static const vwSwitch_t	disruptor =	{ { 13.0f, 6.0f, -4.0f,  22.0f, -30.0f, -10.0f }, 0.9f };	// swung across
	static const vwSwitch_t	bowcaster =	{ { 12.0f, 4.0f,  2.0f,  15.0f,  10.0f, -55.0f }, 1.0f };	// on its side
	static const vwSwitch_t	repeater =	{ { 16.0f, 5.0f,  3.0f,  18.0f,   5.0f,  25.0f }, 0.6f };	// heavy
	static const vwSwitch_t	demp2 =		{ { 12.0f, 4.0f,  3.0f,  20.0f,  10.0f,  60.0f }, 1.1f };
	static const vwSwitch_t	flechette =	{ { 14.0f, 5.0f, -2.0f,  30.0f,  -8.0f, -25.0f }, 0.9f };
	static const vwSwitch_t	rocket =	{ { 18.0f, 8.0f,  2.0f, -12.0f,   5.0f,  10.0f }, 0.5f };	// up onto the shoulder
	static const vwSwitch_t	concussion ={ { 15.0f, 5.0f, -3.0f,  28.0f, -20.0f, -12.0f }, 0.8f };
	static const vwSwitch_t	thrown =	{ { 12.0f, 2.0f,  2.0f,  40.0f,   0.0f,  15.0f }, 1.4f };	// the hand drops
	static const vwSwitch_t	placed =	{ { 12.0f, 2.0f,  2.0f,  45.0f,   0.0f, -15.0f }, 1.4f };

	switch ( weapon ) {
	case WP_STUN_BATON:			return &baton;
	case WP_BRYAR_PISTOL:
	case WP_BRYAR_OLD:			return &pistol;
	case WP_BLASTER:			return &blaster;
	case WP_DISRUPTOR:			return &disruptor;
	case WP_BOWCASTER:			return &bowcaster;
	case WP_REPEATER:			return &repeater;
	case WP_DEMP2:				return &demp2;
	case WP_FLECHETTE:			return &flechette;
	case WP_ROCKET_LAUNCHER:	return &rocket;
	case WP_CONCUSSION:			return &concussion;
	case WP_THERMAL:			return &thrown;
	case WP_TRIP_MINE:
	case WP_DET_PACK:			return &placed;
	default:					return &blaster;
	}
}

static void ViewWeapon_AddPose( vwPose_t *to, const vwPose_t *pose, float amount ) {
	to->down += pose->down * amount;
	to->back += pose->back * amount;
	to->side += pose->side * amount;
	to->pitch += pose->pitch * amount;
	to->yaw += pose->yaw * amount;
	to->roll += pose->roll * amount;
}

// the weapon is put away (eased in) and drawn (eased out, past its place by the overshoot of the weapon, back to it)
static void ViewWeapon_SwitchPose( vwPose_t *pose ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	const vwSwitch_t	*sw = ViewWeapon_Switch( ps->weapon );
	float				p, amount;

	if ( ps->weaponstate != vw.state ) {
		vw.state = ps->weaponstate;
		vw.stateTotal = Q_max( 50, ps->weaponTime );
	}
	if ( vw.state != WEAPON_DROPPING && vw.state != WEAPON_RAISING ) {
		return;
	}
	p = Com_Clamp( 0.0f, 1.0f, 1.0f - ps->weaponTime / (float)vw.stateTotal );
	if ( vw.state == WEAPON_DROPPING ) {
		amount = p * p;
	} else {
		// (ease out back: 1 at the end, a little beyond it before)
		const float c = sw->overshoot, q = p - 1.0f;

		amount = 1.0f - ( 1.0f + ( c + 1.0f ) * q * q * q + c * q * q );
	}
	ViewWeapon_AddPose( pose, &sw->away, amount );
}

qboolean CG_ViewWeaponOwnSwitch( void ) {
	return (qboolean)( cg_weaponBob.value && !cg.renderingThirdPerson );
}

// The weapon is drawn from the eye (its model reaches out to where the hands are), so turning its angles alone swings
// it about the eye, across the view. A pose turns it about the grip instead: the origin moves so that point stays.
static void ViewWeapon_ApplyPose( const vwPose_t *pose, vec3_t origin, vec3_t angles ) {
	vec3_t	before[3], after[3];
	int		i;

	AnglesToAxis( angles, before );
	angles[PITCH] += pose->pitch;
	angles[YAW] += pose->yaw;
	angles[ROLL] += pose->roll;
	AnglesToAxis( angles, after );
	for ( i = 0; i < 3; i++ ) {
		vec3_t moved;

		VectorSubtract( before[i], after[i], moved );
		VectorMA( origin, VW_GRIP[i], moved, origin );
	}
	VectorMA( origin, -pose->down, cg.refdef.viewaxis[2], origin );
	VectorMA( origin, -pose->back, cg.refdef.viewaxis[0], origin );
	VectorMA( origin, -pose->side, cg.refdef.viewaxis[1], origin );
}

void CG_ViewWeaponMove( vec3_t origin, vec3_t angles ) {
	const int	weapon = cg.predictedPlayerState.weapon;
	float		lagPitch, lagYaw, back, wall;
	int			dt;

	vwPose_t	pose;

	if ( !cg_weaponBob.value || cg.renderingThirdPerson || cg.predictedPlayerState.zoomMode || weapon == WP_SABER || weapon == WP_MELEE ) {
		vw.time = 0;
		ViewWeapon_ClearKicks();
		return;
	}
	dt = cg.time - vw.time;
	if ( !vw.time || dt < 0 || dt > VW_GAP_MSEC ) {
		// back after a while: no turn and no wall yet, a kick of this frame stays
		vw.time = cg.time;
		VectorCopy( cg.refdef.viewangles, vw.angles );
		vw.rate[0] = vw.rate[1] = vw.wall = 0.0f;
		dt = 0;
	}
	if ( dt > 0 ) {
		ViewWeapon_Step( dt );
		vw.time = cg.time;
	}

	// it lags behind the turn: turning left (yaw up) leaves it pointing right of the view
	lagPitch = Com_Clamp( -SWAY_MAX, SWAY_MAX, -vw.rate[0] * SWAY_SCALE );
	lagYaw = Com_Clamp( -SWAY_MAX, SWAY_MAX, -vw.rate[1] * SWAY_SCALE );
	angles[PITCH] += lagPitch;
	angles[YAW] += lagYaw;
	angles[ROLL] += lagYaw * SWAY_ROLL;

	back = Com_Clamp( 0.0f, KICK_MAX_BACK, ( vw.slow[KICK_BACK] - vw.fast[KICK_BACK] ) / vw.peak );
	VectorMA( origin, -back, cg.refdef.viewaxis[0], origin );
	VectorMA( origin, back * KICK_LIFT, cg.refdef.viewaxis[2], origin );
	angles[PITCH] -= Com_Clamp( 0.0f, KICK_MAX_PITCH, ( vw.slow[KICK_PITCH] - vw.fast[KICK_PITCH] ) / vw.peak );
	angles[YAW] += Com_Clamp( -KICK_MAX_PITCH, KICK_MAX_PITCH, ( vw.slow[KICK_YAW] - vw.fast[KICK_YAW] ) / vw.peak );

	// put away and drawn
	memset( &pose, 0, sizeof( pose ) );
	ViewWeapon_SwitchPose( &pose );
	ViewWeapon_ApplyPose( &pose, origin, angles );

	// at a wall it is lowered and pulled in, eased at both ends
	wall = vw.wall * vw.wall * ( 3.0f - 2.0f * vw.wall );
	angles[PITCH] += wall * WALL_PITCH;
	VectorMA( origin, -wall * WALL_BACK, cg.refdef.viewaxis[0], origin );
	VectorMA( origin, -wall * WALL_DOWN, cg.refdef.viewaxis[2], origin );
}
