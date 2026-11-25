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

// cg_thermal.c -- a thermal detonator beeps, faster and faster towards the blast, and the glowing ring round it
// throws a stripe of light onto the floor and the walls, pulsing with it (issue #22, item 3). It rolls and bounces
// smoothly where the stock one turned by the clock about the speed of its last throw or bounce and jumped back out of
// the floor with the next snapshot (CG_ThermalPose). Its steady sound stays
// (the loop the game puts on it). The game sends nothing of its fuse: it blows up TD_TIME after the throw plus the
// half second of its warning (game/g_weapon.c thermalDetonatorExplode), the same for both fires, so the time left is
// counted from when the missile was thrown. On v_weaponImpacts.

#include "cgame/cg_local.h"
#include "cgame/weapons/cg_explosion.h"

#define THERMAL_FUSE		3500		// msec from the throw to the blast
#define THERMAL_LOST		300			// not seen this long: another entity in the slot
#define BEEP_SLOW			700			// msec between two beeps right after the throw
#define BEEP_FAST			90			// and at the end
// The ring: in thermal_proj.md3 the glowing part of thermal_glow.jpg lies round the z axis of the model, about 3.5
// units out and 1.5 high. Its stage (shaders/models.shader thermal_w_2) is rgbGen wave sin 0.6 0.4 0 3: it pulses
// three times a second between 0.2 and 1.0 of its colour, on the time of the scene.
#define RING_WAVE_BASE		0.6f
#define RING_WAVE_AMP		0.4f
#define RING_WAVE_HZ		3.0f
#define RING_SPREAD			0.3f		// sine of the angle out of the plane of the ring where most of its light is gone
#define RING_REACH			170.0f
#define RING_RED_MSEC		900			// this long before the blast the light of the ring turns red,
#define RING_RED_FADE		250			//	over this long

#define BALL_RADIUS			3.5f		// of thermal_proj.md3
#define BALL_CLEAR			3.0f		// it lies as low as the game has it (its box is smaller than the model)
#define POSE_SETTLE			0.04f		// sec: a jump to a new trajectory of the server goes in about three of these
#define POSE_JUMP			64.0f		// a jump further than this is no bounce: shown at once
#define SPIN_AIR_KEEP		0.6f		// of its turn a second it keeps in the air
#define THROW_SPIN			3.0f		// radians a second it leaves the hand with (9 at first: the stripe of its ring flickered)
#define ROLL_SHARE			0.15f		// of the turn a ball rolling that way really has: a real one turns 14 times a second
#define SPIN_MOST			5.0f		//	thrown, and never more than this, or the stripe of light of its ring strobes

typedef struct {
	int		thrown, seen;
	int		nextBeep;
	// how it is shown (CG_ThermalPose)
	int		poseSeen;
	int		trTime, trType;		// of the trajectory last seen: a new one, it bounced
	vec3_t	shown, offset;		// where it was drawn, and how far that is still from its trajectory
	matrix3_t axis;
	vec3_t	spin;				// radians a second about the axis it points along
} thermalTrack_t;

static thermalTrack_t	thermals[MAX_GENTITIES];
static sfxHandle_t		beepSound;
static int				bandLight = -1;		// the renderer takes bands of light (ext.R_AddBandLight), asked once

void CG_ThermalInit( void ) {
	memset( thermals, 0, sizeof( thermals ) );
}

void CG_ThermalRegister( void ) {
	// (of the game: the short high tick of the lock of the rocket launcher)
	beepSound = trap->S_RegisterSound( "sound/weapons/rocket/tick.wav" );
}

void CG_ThermalFuse( const centity_t *cent ) {
	const entityState_t	*s = &cent->currentState;
	thermalTrack_t		*t;
	float				left;

	if ( s->weapon != WP_THERMAL || !v_weaponImpacts.integer || s->number < 0 || s->number >= MAX_GENTITIES
		|| CG_JPInSeparatedDuel() ) {
		return;
	}
	t = &thermals[s->number];
	if ( !t->seen || cg.time < t->seen || cg.time - t->seen > THERMAL_LOST ) {
		// the game starts its trajectory at the throw; one seen later (it came into view) bounced since, or not
		t->thrown = ( s->pos.trTime <= cg.time && cg.time - s->pos.trTime < THERMAL_FUSE ) ? s->pos.trTime : cg.time;
		t->nextBeep = 0;
	}
	t->seen = cg.time;
	left = Com_Clamp( 0.0f, 1.0f, 1.0f - (float)( cg.time - t->thrown ) / THERMAL_FUSE );
	if ( cg.time >= t->nextBeep ) {
		trap->S_StartSound( NULL, s->number, CHAN_AUTO, beepSound );
		t->nextBeep = cg.time + BEEP_FAST + (int)( ( BEEP_SLOW - BEEP_FAST ) * powf( left, 1.3f ) );
	}
}

// a ball resting on or rolling along a surface just under it: its normal
static qboolean Thermal_Ground( const vec3_t at, vec3_t normal ) {
	const vec3_t	mins = { -BALL_RADIUS * 0.8f, -BALL_RADIUS * 0.8f, -BALL_RADIUS * 0.8f };
	const vec3_t	maxs = { BALL_RADIUS * 0.8f, BALL_RADIUS * 0.8f, BALL_RADIUS * 0.8f };
	vec3_t			end;
	trace_t			tr;

	VectorSet( end, at[0], at[1], at[2] - BALL_RADIUS * 0.6f );
	CG_Trace( &tr, at, mins, maxs, end, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.fraction >= 1.0f || tr.startsolid || tr.plane.normal[2] < 0.5f ) {
		return qfalse;
	}
	VectorCopy( tr.plane.normal, normal );
	return qtrue;
}

/*
=================
CG_ThermalPose

Where and how turned a thermal detonator is drawn (cg_ents.c CG_Missile, ent its refEntity, origin and axis as the
stock code made them). A new trajectory of the server (a bounce, which the client only hears of with the next
snapshot, having flown it on into the floor meanwhile) is not jumped to: the gap goes in a few hundredths of a
second, and the drawn ball never moves into what is solid. It turns by the way it went: rolling on a surface, about
the axis across its way, as far as its radius takes it; in the air it keeps most of the turn it had. At rest it keeps
the way it lies.
=================
*/
void CG_ThermalPose( const centity_t *cent, refEntity_t *ent ) {
	const entityState_t	*s = &cent->currentState;
	const float			dt = Com_Clamp( 0.0f, 0.1f, cg.frametime * 0.001f );
	thermalTrack_t		*t;
	vec3_t				target, step, normal;
	float				dist;

	if ( s->weapon != WP_THERMAL || !v_weaponImpacts.integer || s->number < 0 || s->number >= MAX_GENTITIES ) {
		return;
	}
	t = &thermals[s->number];
	VectorAdd( cent->lerpOrigin, t->offset, target );
	if ( !t->poseSeen || cg.time < t->poseSeen || cg.time - t->poseSeen > THERMAL_LOST || Distance( target, t->shown ) > POSE_JUMP * 2.0f ) {
		// (new, or back in view: as the stock code has it)
		VectorCopy( cent->lerpOrigin, t->shown );
		VectorClear( t->offset );
		AxisCopy( ent->axis, t->axis );
		// thrown: a turn forward about the axis across its flight, as a hand lets a ball go
		VectorClear( t->spin );
		if ( s->pos.trType != TR_STATIONARY ) {
			const vec3_t	up = { 0.0f, 0.0f, 1.0f };
			vec3_t			across;

			CrossProduct( s->pos.trDelta, up, across );
			if ( VectorNormalize( across ) > 0.0f ) {
				VectorScale( across, -THROW_SPIN, t->spin );
			}
		}
		t->trTime = s->pos.trTime;
		t->trType = s->pos.trType;
		t->poseSeen = cg.time;
		return;
	}
	t->poseSeen = cg.time;
	if ( s->pos.trTime != t->trTime || s->pos.trType != t->trType ) {
		// a new trajectory: from where it is drawn, not from where the new one says it is now
		VectorSubtract( t->shown, cent->lerpOrigin, t->offset );
		if ( VectorLength( t->offset ) > POSE_JUMP ) {
			VectorClear( t->offset );
		}
		t->trTime = s->pos.trTime;
		t->trType = s->pos.trType;
	}
	VectorScale( t->offset, expf( -dt / POSE_SETTLE ), t->offset );
	VectorAdd( cent->lerpOrigin, t->offset, target );
	// never into the floor or a wall on the way there
	{
		const vec3_t	mins = { -BALL_CLEAR, -BALL_CLEAR, -BALL_CLEAR };
		const vec3_t	maxs = { BALL_CLEAR, BALL_CLEAR, BALL_CLEAR };
		trace_t			tr;

		CG_Trace( &tr, t->shown, mins, maxs, target, ENTITYNUM_NONE, MASK_SOLID );
		if ( !tr.startsolid && tr.fraction < 1.0f ) {
			VectorCopy( tr.endpos, target );
		}
	}
	VectorSubtract( target, t->shown, step );
	VectorCopy( target, t->shown );
	dist = VectorLength( step );

	if ( dt > 0.0f ) {
		if ( dist > 0.01f && Thermal_Ground( target, normal ) ) {
			// rolling: about the axis across its way along the surface, its way over its radius
			vec3_t along, roll;

			VectorMA( step, -DotProduct( step, normal ), normal, along );
			CrossProduct( normal, along, roll );
			VectorScale( roll, ROLL_SHARE / ( BALL_RADIUS * dt ), t->spin );
		} else if ( dist <= 0.01f ) {
			VectorClear( t->spin );		// lying there
		} else {
			VectorScale( t->spin, powf( SPIN_AIR_KEEP, dt ), t->spin );
		}
		{
			float	rate = VectorLength( t->spin );
			vec3_t	dir, turned;
			int		k;

			if ( rate > SPIN_MOST ) {
				VectorScale( t->spin, SPIN_MOST / rate, t->spin );
				rate = SPIN_MOST;
			}
			if ( rate * dt > 0.0001f ) {
				VectorScale( t->spin, 1.0f / rate, dir );
				for ( k = 0; k < 3; k++ ) {
					RotatePointAroundVector( turned, dir, t->axis[k], RAD2DEG( rate * dt ) );
					VectorCopy( turned, t->axis[k] );
				}
				// (keep it a rotation: the steps add up their errors)
				VectorNormalize( t->axis[0] );
				CrossProduct( t->axis[0], t->axis[1], t->axis[2] );
				VectorNormalize( t->axis[2] );
				CrossProduct( t->axis[2], t->axis[0], t->axis[1] );
			}
		}
	}
	VectorCopy( t->shown, ent->origin );
	VectorCopy( t->shown, ent->oldorigin );
	VectorCopy( t->shown, ent->lightingOrigin );
	AxisCopy( t->axis, ent->axis );
}

// The light of the ring, once the model is turned (cg_ents.c CG_Missile, ent its refEntity): out of the body only
// round the plane of the ring, so a stripe across the floor and up the walls where it lies on its side, a line round
// the walls where it stands, turning as it rolls; it pulses with the glow of the ring. A renderer without bands of
// light gets a dimmer point light. Shortly before the blast the light turns red (the fuse CG_ThermalFuse counts,
// which runs before this in a frame).
void CG_ThermalRing( const centity_t *cent, const refEntity_t *ent ) {
	static const vec3_t	cold = { 0.8f, 0.88f, 1.0f }, red = { 1.0f, 0.1f, 0.06f };
	const float			wave = RING_WAVE_BASE + RING_WAVE_AMP * sinf( cg.time * 0.001f * RING_WAVE_HZ * 2.0f * M_PI );
	const int			number = cent->currentState.number;
	float				late = 0.0f;
	vec3_t				color;

	if ( cent->currentState.weapon != WP_THERMAL || !v_weaponImpacts.integer || CG_JPInSeparatedDuel() ) {
		return;
	}
	if ( number >= 0 && number < MAX_GENTITIES && thermals[number].seen == cg.time ) {
		late = Com_Clamp( 0.0f, 1.0f, ( RING_RED_MSEC - ( thermals[number].thrown + THERMAL_FUSE - cg.time ) ) / (float)RING_RED_FADE );
	}
	VectorScale( cold, ( 1.0f - late ) * wave, color );
	VectorMA( color, late * wave, red, color );
	if ( bandLight < 0 ) {
		char value[8];

		trap->Cvar_VariableStringBuffer( "v_cgameBandLight", value, sizeof( value ) );
		bandLight = ( atoi( value ) && trap->ext.R_AddBandLight ) ? 1 : 0;
	}
	if ( bandLight ) {
		trap->ext.R_AddBandLight( ent->origin, ent->axis[2], RING_SPREAD, RING_REACH, color[0], color[1], color[2] );
	} else {
		trap->R_AddLightToScene( ent->origin, RING_REACH * 0.5f, 0.6f * color[0], 0.6f * color[1], 0.6f * color[2] );
	}
}
