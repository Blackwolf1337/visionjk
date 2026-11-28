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

// g_forcemoves.c -- what the Force does to bodies (issue #19, server side, vs_forceMoves; 0: the stock game):
//
// - Force Grip level 3 holds the gripped in front of the gripper every frame, drawn to the spot as by a spring and
//   carried along with the gripper. The game set his speed every 300 ms in four steps ("to avoid heavy bandwidth
//   usage", which the player state costs anyway), so he trailed behind the aim in jerks.
// - A push while gripping somebody throws him along the view; a pull drags him down at the feet. Either knocks him
//   down.
// - Who is thrown, pushed at level 3 or pushed with both hands and hits a wall or a ceiling hard takes damage by how
//   fast he hit it (credited to who sent him), is knocked down, and the wall thuds and gives off dust (stock sound and
//   effect, so every client sees them), his view shakes. Who he flies into is hurt and knocked down too. Windows and
//   what breaks easily give way already (DoImpact).
// - With bare hands the push is two-handed (the start of the two-handed lightning, the game has no two-handed push):
//   stronger, and it knocks down everybody it moves.
// - Lightning that strikes water shocks everybody in that water.
// Legacy clients see the stock animations, sounds and effects of all of it.

#include "game/g_local.h"
#include "game/force/g_forcemoves.h"
#include "game/w_saber.h"		// FORCE_LIGHTNING_RADIUS

// (of w_force.c)
qboolean WP_ForcePowerUsable( gentity_t *self, forcePowers_t forcePower );
void WP_ForcePowerStart( gentity_t *self, forcePowers_t forcePower, int overrideAmt );
void ForceLightningDamage( gentity_t *self, gentity_t *traceEnt, vec3_t dir, vec3_t impactPoint );

#define GRIP_HOLD_AHEAD		128.0f	// the spot in front of the gripper (as the game: 128 along the view, 16 up)
#define GRIP_HOLD_UP		16.0f
#define GRIP_SPRING			9.0f	// a second: how fast the gripped is drawn to the spot
#define GRIP_MAX_SPEED		1000.0f

#define THROW_SPEED			550.0f	// the throw: this plus THROW_SPEED_LEVEL a level of push
#define THROW_SPEED_LEVEL	150.0f
#define THROW_LIFT			150.0f
#define THROW_MSEC			1500	// a wall hit within this hurts
#define YANK_FEET			72.0f	// a pull drops him this close in front of the feet
#define KNOCKDOWN_MSEC		1400

#define IMPACT_MIN_SPEED	300.0f	// units a second into the wall from which it hurts
#define IMPACT_DAMAGE_STEP	10.0f	//	a point of damage for this much more
#define IMPACT_MAX_DAMAGE	45
#define TWOHAND_POWER		1.6f	// the two-handed push, times the push of the game

#define WATER_REACH			400.0f	// lightning in water reaches who is in it this far from where it struck

typedef struct {
	int		thrower;
	int		until;			// 0: not flying
	float	strength;
	qboolean yank;			// pulled down at the feet of thrower
} flying_t;

static flying_t flying[MAX_GENTITIES];

qboolean G_ForceMovesOn( void ) {
	return (qboolean)( vs_forceMoves.integer != 0 );
}

void G_ForceMovesInit( void ) {
	memset( flying, 0, sizeof( flying ) );
}

/*
=================
Grip
=================
*/

void G_ForceMovesGrip( gentity_t *self, gentity_t *gripEnt ) {
	vec3_t	fwd, spot, vel;
	float	speed;

	AngleVectors( self->client->ps.viewangles, fwd, NULL, NULL );
	VectorMA( self->client->ps.origin, GRIP_HOLD_AHEAD, fwd, spot );
	spot[2] += GRIP_HOLD_UP;
	// towards the spot, faster the further it is, and along with the gripper
	VectorSubtract( spot, gripEnt->client->ps.origin, vel );
	VectorScale( vel, GRIP_SPRING, vel );
	VectorAdd( vel, self->client->ps.velocity, vel );
	speed = VectorLength( vel );
	if ( speed > GRIP_MAX_SPEED ) {
		VectorScale( vel, GRIP_MAX_SPEED / speed, vel );
	}
	VectorCopy( vel, gripEnt->client->ps.velocity );
}

static void Moves_Knockdown( gentity_t *victim, int msec ) {
	if ( victim->client && BG_KnockDownable( &victim->client->ps ) ) {
		victim->client->ps.forceHandExtend = HANDEXTEND_KNOCKDOWN;
		victim->client->ps.forceHandExtendTime = level.time + msec;
		victim->client->ps.forceDodgeAnim = 0;
		victim->client->ps.quickerGetup = qfalse;
	}
}

static void Moves_Credit( gentity_t *victim, gentity_t *thrower ) {
	victim->client->ps.otherKiller = thrower->s.number;
	victim->client->ps.otherKillerTime = level.time + 5000;
	victim->client->ps.otherKillerDebounceTime = level.time + 100;
}

void G_ForceMovesFlying( gentity_t *victim, gentity_t *thrower, float strength, int msec ) {
	flying_t *f;

	if ( !victim || !victim->client || !thrower || victim->s.number < 0 || victim->s.number >= MAX_GENTITIES ) {
		return;
	}
	f = &flying[victim->s.number];
	f->thrower = thrower->s.number;
	f->until = level.time + msec;
	f->strength = strength;
	f->yank = qfalse;
}

qboolean G_ForceMovesGripThrow( gentity_t *self, qboolean pull ) {
	playerState_t	*ps = &self->client->ps;
	const forcePowers_t	power = pull ? FP_PULL : FP_PUSH;
	gentity_t		*victim;
	vec3_t			fwd, vel;
	int				pushLevel;

	if ( !G_ForceMovesOn() || !( ps->fd.forcePowersActive & ( 1 << FP_GRIP ) ) ) {
		return qfalse;
	}
	if ( ps->fd.forceGripEntityNum < 0 || ps->fd.forceGripEntityNum >= ENTITYNUM_WORLD ) {
		return qfalse;
	}
	victim = &g_entities[ps->fd.forceGripEntityNum];
	if ( !victim->inuse || !victim->client || victim->health <= 0 ) {
		return qfalse;
	}
	// (no PW_DISINT_4 check: the grip itself holds it for a minute, for the glow of the hand)
	if ( self->health <= 0 || !WP_ForcePowerUsable( self, power ) ) {
		return qtrue;		// (the hand holds him: no push of the game either)
	}
	pushLevel = Com_Clampi( FORCE_LEVEL_1, FORCE_LEVEL_3, ps->fd.forcePowerLevel[power] );

	// let go of him, and do not take him again at once while the grip is still held
	WP_ForcePowerStop( self, FP_GRIP );
	ps->fd.forceGripUseTime = level.time + 1000;
	victim->client->ps.forceGripChangeMovetype = PM_NORMAL;
	victim->client->ps.fd.forceGripBeingGripped = 0;
	WP_ForcePowerStart( self, power, 0 );

	G_Sound( self, CHAN_BODY, G_SoundIndex( pull ? "sound/weapons/force/pull.wav" : "sound/weapons/force/push.wav" ) );
	if ( !pull && G_ForceMovesTwoHandPush( self ) ) {
		ps->forceHandExtend = HANDEXTEND_TAUNT;
		ps->forceDodgeAnim = BOTH_FORCE_2HANDEDLIGHTNING;
	} else {
		ps->forceHandExtend = pull ? HANDEXTEND_FORCEPULL : HANDEXTEND_FORCEPUSH;
	}
	ps->forceHandExtendTime = level.time + ( pull ? 400 : 700 );
	ps->powerups[PW_DISINT_4] = level.time + 1100;
	ps->powerups[PW_PULL] = pull ? ps->powerups[PW_DISINT_4] : 0;

	AngleVectors( ps->viewangles, fwd, NULL, NULL );
	if ( pull ) {
		// down at the feet: an arc that ends YANK_FEET in front of him
		vec3_t	feet, way;
		float	dist;

		VectorMA( ps->origin, YANK_FEET, fwd, feet );
		VectorSubtract( feet, victim->client->ps.origin, way );
		way[2] = 0.0f;
		dist = VectorNormalize( way );
		VectorScale( way, Q_min( dist * 3.0f, 900.0f ), vel );
		vel[2] = 120.0f + dist * 0.25f;
	} else {
		VectorScale( fwd, THROW_SPEED + THROW_SPEED_LEVEL * pushLevel, vel );
		vel[2] += THROW_LIFT;
	}
	VectorCopy( vel, victim->client->ps.velocity );
	victim->client->pushEffectTime = level.time + 600;
	Moves_Credit( victim, self );
	Moves_Knockdown( victim, KNOCKDOWN_MSEC );
	G_ForceMovesFlying( victim, self, pull ? 0.6f : 1.0f, THROW_MSEC );
	flying[victim->s.number].yank = pull;
	return qtrue;
}

qboolean G_ForceMovesTwoHandPush( const gentity_t *self ) {
	return (qboolean)( G_ForceMovesOn() && self->client && self->client->ps.weapon == WP_MELEE );
}

/*
=================
Hitting walls
=================
*/

static void Moves_Impact( gentity_t *ent, flying_t *f, const trace_t *tr, float into, const vec3_t velocity ) {
	gentity_t	*thrower = ( f->thrower >= 0 && f->thrower < ENTITYNUM_WORLD && g_entities[f->thrower].inuse ) ? &g_entities[f->thrower] : NULL;
	const int	damage = Com_Clampi( 0, IMPACT_MAX_DAMAGE, (int)( ( into - IMPACT_MIN_SPEED ) / IMPACT_DAMAGE_STEP * f->strength ) + 3 );
	vec3_t		dir, angles;

	VectorCopy( velocity, dir );
	VectorNormalize( dir );
	// the thud, the dust off the wall, a shake of his view
	G_Sound( ent, CHAN_BODY, G_SoundIndex( va( "sound/effects/body_slam%i.mp3", Q_irand( 1, 2 ) ) ) );
	vectoangles( tr->plane.normal, angles );
	G_PlayEffectID( G_EffectIndex( into > 600.0f ? "env/impact_dust" : "env/impact_dustonly" ), (float *)tr->endpos, angles );
	G_ScreenShake( (float *)tr->endpos, ent, Com_Clamp( 2.0f, 8.0f, into / 120.0f ), 350, qfalse );
	Moves_Knockdown( ent, KNOCKDOWN_MSEC );
	if ( damage > 0 ) {
		G_Damage( ent, thrower, thrower, dir, (float *)tr->endpos, damage, DAMAGE_NO_ARMOR, MOD_FALLING );
	}
	// who he flew into
	if ( tr->entityNum >= 0 && tr->entityNum < ENTITYNUM_WORLD ) {
		gentity_t *other = &g_entities[tr->entityNum];

		if ( other->inuse && other->client && other->health > 0 && other != thrower ) {
			Moves_Knockdown( other, KNOCKDOWN_MSEC );
			if ( thrower ) {
				Moves_Credit( other, thrower );
			}
			if ( damage > 1 ) {
				G_Damage( other, thrower, thrower, dir, (float *)tr->endpos, damage / 2, DAMAGE_NO_ARMOR, MOD_FALLING );
			}
		}
	}
}

void G_ForceMovesAfterMove( gentity_t *ent, const vec3_t velocity ) {
	flying_t	*f;
	trace_t		tr;
	vec3_t		end, mins, maxs;
	float		speed, into, after;

	if ( !G_ForceMovesOn() || !ent->client || ent->s.number < 0 || ent->s.number >= MAX_GENTITIES ) {
		return;
	}
	f = &flying[ent->s.number];
	if ( !f->until ) {
		return;
	}
	if ( level.time > f->until || ent->health <= 0 ) {
		f->until = 0;
		return;
	}
	// pulled down: dropped at the feet once there
	if ( f->yank && f->thrower >= 0 && f->thrower < ENTITYNUM_WORLD && g_entities[f->thrower].client ) {
		vec3_t way;

		VectorSubtract( ent->client->ps.origin, g_entities[f->thrower].client->ps.origin, way );
		way[2] = 0.0f;
		if ( VectorLength( way ) < YANK_FEET + 16.0f ) {
			ent->client->ps.velocity[0] *= 0.15f;
			ent->client->ps.velocity[1] *= 0.15f;
			f->until = 0;
			return;
		}
	}
	speed = VectorLength( velocity );
	if ( speed < IMPACT_MIN_SPEED ) {
		return;
	}
	// what lies just ahead along the way he flew
	VectorCopy( ent->r.mins, mins );
	VectorCopy( ent->r.maxs, maxs );
	VectorScale( velocity, 1.0f / speed, end );
	VectorMA( ent->client->ps.origin, 0.06f * speed + 4.0f, end, end );
	trap->Trace( &tr, ent->client->ps.origin, mins, maxs, end, ent->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
	if ( tr.fraction >= 1.0f || tr.startsolid || tr.allsolid ) {
		return;
	}
	// (the floor is the falling damage of the game)
	if ( tr.plane.normal[2] > 0.7f ) {
		return;
	}
	into = -DotProduct( velocity, tr.plane.normal );
	after = -DotProduct( ent->client->ps.velocity, tr.plane.normal );
	// it hurts when he came in fast and the wall stopped him
	if ( into < IMPACT_MIN_SPEED || after > into * 0.5f ) {
		return;
	}
	Moves_Impact( ent, f, &tr, into, velocity );
	f->until = 0;
}

/*
=================
Lightning in water
=================
*/

void G_ForceMovesLightningWater( gentity_t *self, const vec3_t forward ) {
	const float	reach = self->client->ps.fd.forcePowerLevel[FP_LIGHTNING] > FORCE_LEVEL_2 ? FORCE_LIGHTNING_RADIUS : 2048.0f;
	trace_t		tr;
	vec3_t		start, end, at;
	int			i;

	if ( !G_ForceMovesOn() ) {
		return;
	}
	VectorCopy( self->client->ps.origin, start );
	start[2] += self->client->ps.viewheight;
	// (the caster in the water himself strikes it where he stands)
	if ( trap->PointContents( self->client->ps.origin, self->s.number ) & CONTENTS_WATER ) {
		VectorCopy( self->client->ps.origin, at );
	} else {
		VectorMA( start, reach, forward, end );
		trap->Trace( &tr, start, NULL, NULL, end, self->s.number, MASK_SHOT | CONTENTS_WATER, qfalse, 0, 0 );
		if ( tr.fraction >= 1.0f || !( tr.contents & CONTENTS_WATER ) ) {
			return;
		}
		VectorCopy( tr.endpos, at );
		at[2] -= 4.0f;
	}
	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t	*other = &g_entities[i];
		vec3_t		feet, dir;

		if ( other == self || !other->inuse || !other->client || other->health <= 0 || !other->takedamage ) {
			continue;
		}
		if ( Distance( other->client->ps.origin, at ) > WATER_REACH ) {
			continue;
		}
		VectorCopy( other->client->ps.origin, feet );
		feet[2] += other->r.mins[2] + 4.0f;
		if ( !( trap->PointContents( feet, other->s.number ) & CONTENTS_WATER ) ) {
			continue;
		}
		// the same water: nothing solid between
		trap->Trace( &tr, at, NULL, NULL, feet, ENTITYNUM_NONE, MASK_SOLID, qfalse, 0, 0 );
		if ( tr.fraction < 1.0f ) {
			continue;
		}
		VectorSubtract( feet, at, dir );
		VectorNormalize( dir );
		ForceLightningDamage( self, other, dir, feet );
	}
}
