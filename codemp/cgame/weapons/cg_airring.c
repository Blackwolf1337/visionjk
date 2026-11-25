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

// cg_airring.c -- rings of pressure in the air (issue #22, items 8 and 20): a fully charged shot of the pistol, the
// bowcaster or the disruptor leaves the barrel with a ring of bent air and a short bright ring, and rings run along
// the beam of the concussion rifle from the barrel to where it hit; its missile trails one. The bent air is a ring of
// the screen effects (CG_ScreenFxRing, only with a renderer that draws them), the bright ring a quad facing the way
// of the shot, which any renderer draws.

#include "cgame/cg_local.h"
#include "cgame/weapons/cg_airring.h"
#include "cgame/weapons/cg_boltlight.h"

#define AIR_RINGS			12
#define CHARGE_FULL_MSEC	1000		// pistol and bowcaster: five units of 200 msec (g_weapon.c), the most a charge gives
#define CHARGE_MSEC			380			// the ring of a charged shot runs out in that time
#define CHARGE_TRAVEL		110.0f		// and goes that far with the shot
#define CHARGE_RADIUS		34.0f
#define CHARGE_SHINE_MSEC	220			// the bright ring is gone sooner than the bent air
#define BEAM_RINGS			3
#define BEAM_GAP			70			// msec between the rings along the beam of the concussion rifle
#define BEAM_SPEED			2600.0f		// units a second they run
#define BEAM_RADIUS			16.0f
#define TRAIL_BEHIND		26.0f		// the ring behind the missile of the concussion rifle, that far back
#define SAME_SHOT_MSEC		50			// a disruptor shot through a player sends its beam again from the same barrel

typedef struct {
	int			birth;		// 0: free; may lie ahead, a ring of a beam that has not started yet
	int			life;
	vec3_t		origin, dir;
	float		travel;		// units it runs along dir in its life
	float		radius;		// it grows to
	float		strength;
	vec3_t		color;		// of the bright ring
	int			shine;		// msec the bright ring lasts, 0: none
	qboolean	beam;		// runs at an even pace and keeps its size
} airRing_t;

static airRing_t	rings[AIR_RINGS];
static int			nextRing;
static qhandle_t	ringShader;
static int			lastShotTime;
static vec3_t		lastShotMuzzle;

void CG_AirRingsInit( void ) {
	memset( rings, 0, sizeof( rings ) );
	nextRing = lastShotTime = 0;
	VectorClear( lastShotMuzzle );
}

void CG_AirRingsRegister( void ) {
	ringShader = trap->R_RegisterShader( "gfx/misc/shockwave" );	// (of the game, additive)
}

static qboolean AirRing_On( void ) {
	return (qboolean)( v_weaponImpacts.integer && !CG_JPInSeparatedDuel() );
}

static airRing_t *AirRing_Alloc( void ) {
	airRing_t *ring = &rings[nextRing];

	nextRing = ( nextRing + 1 ) % AIR_RINGS;
	memset( ring, 0, sizeof( *ring ) );
	return ring;
}

// a flat ring at, facing dir, of radius units, its colour already faded (additive)
static void AirRing_Shine( const vec3_t at, const vec3_t dir, float radius, const vec3_t color ) {
	refEntity_t	re;
	int			i;

	if ( !ringShader || radius <= 0.5f ) {
		return;
	}
	memset( &re, 0, sizeof( re ) );
	re.reType = RT_ORIENTED_QUAD;
	VectorCopy( at, re.origin );
	VectorCopy( dir, re.axis[0] );
	MakeNormalVectors( dir, re.axis[1], re.axis[2] );
	re.radius = radius;
	re.customShader = ringShader;
	for ( i = 0; i < 3; i++ ) {
		re.shaderRGBA[i] = (byte)Com_Clamp( 0.0f, 255.0f, color[i] * 255.0f );
	}
	re.shaderRGBA[3] = 255;
	re.renderfx = RF_NOSHADOW;
	trap->R_AddRefEntityToScene( &re );
}

// where the shot of this player leaves his weapon, and the way it goes
static void AirRing_Muzzle( const centity_t *cent, vec3_t muzzle, vec3_t dir ) {
	const int num = cent->currentState.number;

	if ( cg.snap && num == cg.snap->ps.clientNum && !cg.renderingThirdPerson ) {
		// (the flash of the weapon in view, the frame before)
		if ( VectorLengthSquared( cg.lastFPFlashPoint ) > 0.0f ) {
			VectorCopy( cg.lastFPFlashPoint, muzzle );
		} else {
			VectorMA( cg.refdef.vieworg, 20.0f, cg.refdef.viewaxis[0], muzzle );
		}
		VectorCopy( cg.refdef.viewaxis[0], dir );
		return;
	}
	VectorCopy( cent->lerpOrigin, muzzle );
	muzzle[2] += 24.0f;
	CG_GetClientWeaponMuzzleBoltPoint( num, muzzle );	// (leaves it as it is when he has no weapon model)
	AngleVectors( cent->lerpAngles, dir, NULL, NULL );
}

static void AirRing_Charged( int weapon, qboolean altFire, const vec3_t muzzle, const vec3_t dir ) {
	airRing_t	*ring = AirRing_Alloc();
	vec3_t		color;
	int			i;

	if ( !CG_BoltColor( weapon, altFire, color ) ) {
		VectorSet( color, 1.0f, 1.0f, 1.0f );
	}
	ring->birth = cg.time;
	ring->life = CHARGE_MSEC;
	VectorCopy( muzzle, ring->origin );
	VectorCopy( dir, ring->dir );
	ring->travel = CHARGE_TRAVEL;
	ring->radius = CHARGE_RADIUS;
	ring->strength = 0.9f;
	ring->shine = CHARGE_SHINE_MSEC;
	// (towards white: the ring is hot, the bolt has the colour)
	for ( i = 0; i < 3; i++ ) {
		ring->color[i] = 0.45f + 0.55f * color[i];
	}
}

void CG_ChargeRingFired( const centity_t *cent, qboolean altFire ) {
	const entityState_t	*es = &cent->currentState;
	vec3_t				muzzle, dir;

	if ( !AirRing_On() ) {
		return;
	}
	if ( !( ( ( es->weapon == WP_BRYAR_PISTOL || es->weapon == WP_BRYAR_OLD ) && altFire )
		|| ( es->weapon == WP_BOWCASTER && !altFire ) ) ) {
		return;
	}
	// (the time the charge started, as the stock shake of the view reads it)
	if ( cg.time - es->constantLight < CHARGE_FULL_MSEC ) {
		return;
	}
	AirRing_Muzzle( cent, muzzle, dir );
	AirRing_Charged( es->weapon, altFire, muzzle, dir );
}

void CG_ChargeRingDisruptor( const vec3_t muzzle, const vec3_t end, qboolean fullCharge ) {
	vec3_t dir;

	if ( !fullCharge || !AirRing_On() ) {
		return;
	}
	if ( cg.time >= lastShotTime && cg.time - lastShotTime < SAME_SHOT_MSEC
		&& DistanceSquared( muzzle, lastShotMuzzle ) < 8.0f * 8.0f ) {
		return;
	}
	lastShotTime = cg.time;
	VectorCopy( muzzle, lastShotMuzzle );
	VectorSubtract( end, muzzle, dir );
	if ( VectorNormalize( dir ) <= 0.0f ) {
		return;
	}
	AirRing_Charged( WP_DISRUPTOR, qtrue, muzzle, dir );
}

void CG_AirRingBeam( const vec3_t start, const vec3_t dir, float length ) {
	int i;

	if ( !AirRing_On() || length < 32.0f ) {
		return;
	}
	for ( i = 0; i < BEAM_RINGS; i++ ) {
		airRing_t *ring = AirRing_Alloc();

		ring->birth = cg.time + i * BEAM_GAP;
		ring->life = (int)Com_Clamp( 150.0f, 900.0f, length / BEAM_SPEED * 1000.0f );
		VectorCopy( start, ring->origin );
		VectorCopy( dir, ring->dir );
		ring->travel = length;
		ring->radius = BEAM_RADIUS;
		ring->strength = 0.85f - 0.2f * i;
		ring->beam = qtrue;
		ring->shine = ring->life;
		VectorSet( ring->color, 0.22f, 0.27f, 0.4f );	// (faint: the beam itself is bright)
	}
}

void CG_AirRingTrail( const centity_t *cent ) {
	vec3_t dir, at;

	if ( !AirRing_On() || VectorNormalize2( cent->currentState.pos.trDelta, dir ) <= 0.0f ) {
		return;
	}
	VectorMA( cent->lerpOrigin, -TRAIL_BEHIND, dir, at );
	CG_ScreenFxRing( at, 11.0f + 2.5f * sinf( cg.time * 0.025f ), 4.0f, 0.55f );
}

void CG_AirRingsScene( void ) {
	int i;

	for ( i = 0; i < AIR_RINGS; i++ ) {
		airRing_t	*ring = &rings[i];
		const int	age = cg.time - ring->birth;
		float		t, out, along, radius;
		vec3_t		at;

		if ( !ring->birth || age < 0 ) {
			continue;
		}
		if ( age >= ring->life || age > 5000 ) {
			ring->birth = 0;
			continue;
		}
		t = age / (float)ring->life;
		out = 1.0f - ( 1.0f - t ) * ( 1.0f - t );
		if ( ring->beam ) {
			along = ring->travel * t;
			radius = ring->radius * ( 0.7f + 0.3f * t );
		} else {
			// fast out of the barrel, then slower, growing as it goes
			along = ring->travel * out;
			radius = ring->radius * ( 0.12f + 0.88f * out );
		}
		VectorMA( ring->origin, along, ring->dir, at );
		CG_ScreenFxRing( at, radius, radius * 0.3f, ring->strength * ( 1.0f - t ) );
		if ( ring->shine && age < ring->shine ) {
			const float	left = 1.0f - age / (float)ring->shine;
			vec3_t		color;

			VectorScale( ring->color, left * left, color );
			AirRing_Shine( at, ring->dir, radius * 0.75f, color );
		}
	}
}
