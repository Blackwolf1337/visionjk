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

// cg_ripples.c -- the water of the level reacts (issue #23, "Map water"). Rings run out over it where somebody wades:
// one a step, to the left and to the right of his way in turn, so who walks draws a wake behind him, and a faint one
// now and then while he stands. A large one where he comes into the water. A shot that goes into the water or comes
// out of it leaves a ring and throws up droplets, a rocket more of both, and what blows up near the surface sends a
// wide ring over it.
//
// The rings are wet places of the kind WET_SURFACE (ext.R_AddWet): rd-vulkan-vision draws them on the water itself
// (shaders/ripple.frag), bending the picture and catching the light. Where the renderer does not, nothing of this
// happens and the game draws its mark round a wading player as it always did. The droplets and their sound are
// those of the water gun (toys/cg_water.c).
//
// Water here is what a brush of the level holds (CONTENTS_WATER, and slime); lava is none. Its surface is level, a
// trace down into it finds the height. Where a ring would run out past the rim of a basin into the air it is cut
// (Ripples_Round); land hides it by itself.
//
// On v_levelFx. Only a look and a sound here: the game knows nothing of it.

#include "cgame/cg_local.h"
#include "cgame/level/cg_ripples.h"

#define RIPPLES				WET_MAX_SURFACE
#define RIPPLE_LIQUID		( CONTENTS_WATER | CONTENTS_SLIME )
#define RIPPLE_NEAR			2200.0f		// further from the eye no ring begins (FAR_OFF of shaders/ripple.frag)
#define RIPPLE_STRIDE		46.0f		// a wading player makes a ring each this many units of his way
#define RIPPLE_GAP_MSEC		400			// an entity not looked at for this long is looked at anew
#define RIPPLE_SECTORS		8			// directions round a ring in which it is asked where the water ends

typedef struct {
	int		time, life;			// cg.time it began, msec it runs; life 0: none
	float	radius, strength;
	float	phase;				// 0 to 1: where its crests are, so two rings are not the same
	int		round;				// where round it there is water, Ripples_Round
	vec3_t	origin;
} ripple_t;

typedef struct {
	int			seen;			// cg.time it was looked at last
	qboolean	wet;			// a player: he stood in water with the head out of it; a missile: it was in water
	qboolean	right;			// a player: his next ring begins to the right of his way
	int			nextIdle;		// cg.time of his next ring while he stands
	vec3_t		last;			// a player: where on the water his last ring began; a missile: where it was
} rippleEnt_t;

static ripple_t		ripples[RIPPLES];
static int			nextRipple;
static rippleEnt_t	rippleEnts[MAX_GENTITIES];

void CG_RipplesInit( void ) {
	memset( ripples, 0, sizeof( ripples ) );
	memset( rippleEnts, 0, sizeof( rippleEnts ) );
	nextRipple = 0;
}

static qboolean Ripples_On( void ) {
	return (qboolean)( v_levelFx.integer && CG_WaterWetDrawn() );
}

// Where round a place on the water there is water: a bit a direction at half the reach, and eight bits higher at all
// of it. Solid counts as water: land hides a ring by itself, only air beyond a rim would show it.
static int Ripples_Round( const vec3_t at, float radius ) {
	int bits = 0, i, k;

	for ( i = 0; i < RIPPLE_SECTORS; i++ ) {
		const float angle = i * ( 2.0f * M_PI / RIPPLE_SECTORS );

		for ( k = 0; k < 2; k++ ) {
			const float	r = radius * ( k ? 0.95f : 0.5f );
			vec3_t		p;

			VectorSet( p, at[0] + cosf( angle ) * r, at[1] + sinf( angle ) * r, at[2] - 0.5f );
			if ( trap->CM_PointContents( p, 0 ) & ( RIPPLE_LIQUID | CONTENTS_SOLID ) ) {
				bits |= 1 << ( i + k * RIPPLE_SECTORS );
			}
		}
	}
	return bits;
}

// a ring begins at a place on the water: how far it runs out, how strong (0 to 1), how long
static void Ripples_Add( const vec3_t at, float radius, float strength, int life ) {
	ripple_t *r = &ripples[nextRipple];

	if ( DistanceSquared( at, cg.refdef.vieworg ) > Square( RIPPLE_NEAR + radius ) ) {
		return;
	}
	nextRipple = ( nextRipple + 1 ) % RIPPLES;
	r->time = cg.time;
	r->life = life;
	r->radius = radius;
	r->strength = Com_Clamp( 0.0f, 1.0f, strength );
	r->phase = Q_flrand( 0.0f, 1.0f );
	r->round = Ripples_Round( at, radius );
	VectorCopy( at, r->origin );
}

// The water under "top" down to "bottom", if top is not in it: where its surface is. Only from above: the side of a
// brush of water is no surface.
static qboolean Ripples_Surface( const vec3_t top, const vec3_t bottom, vec3_t at ) {
	trace_t tr;

	trap->CM_Trace( &tr, top, bottom, NULL, NULL, 0, RIPPLE_LIQUID, 0 );
	if ( tr.startsolid || tr.allsolid || tr.fraction >= 1.0f || tr.plane.normal[2] < 0.7f ) {
		return qfalse;
	}
	VectorCopy( tr.endpos, at );
	return qtrue;
}

// something went through the surface at "at": its ring and the droplets it throws up
static void Ripples_Splash( const vec3_t at, qboolean heavy ) {
	const vec3_t up = { 0.0f, 0.0f, 1.0f };

	if ( DistanceSquared( at, cg.refdef.vieworg ) > RIPPLE_NEAR * RIPPLE_NEAR ) {
		return;
	}
	Ripples_Add( at, heavy ? 86.0f : 46.0f, heavy ? 0.9f : 0.6f, heavy ? 2000 : 1500 );
	CG_WaterCrown( at, heavy ? 1.0f : 0.45f );
	if ( heavy ) {
		trap->FX_PlayEffectID( cgs.effects.waterSplash, (float *)at, (float *)up, -1, -1, qfalse );
	}
}

/*
=================
CG_RipplePlayer

A player or a creature that is drawn this frame (CG_Player): where he stands in water with his head out of it he
makes rings. qtrue: the rings are drawn, they stand in for the mark of the game round him.
=================
*/
qboolean CG_RipplePlayer( centity_t *cent ) {
	const int	num = cent->currentState.number;
	rippleEnt_t	*e;
	vec3_t		head, feet, at, way;
	qboolean	fresh;
	float		dist, speed;

	if ( !Ripples_On() ) {
		return qfalse;
	}
	if ( num < 0 || num >= MAX_GENTITIES ) {
		return qtrue;
	}
	e = &rippleEnts[num];
	fresh = (qboolean)( cg.time - e->seen > RIPPLE_GAP_MSEC || cg.time < e->seen );
	e->seen = cg.time;
	VectorSet( feet, cent->lerpOrigin[0], cent->lerpOrigin[1], cent->lerpOrigin[2] - 23.0f );
	VectorSet( head, cent->lerpOrigin[0], cent->lerpOrigin[1], cent->lerpOrigin[2] + 32.0f );
	if ( DistanceSquared( cent->lerpOrigin, cg.refdef.vieworg ) > RIPPLE_NEAR * RIPPLE_NEAR
		|| !( trap->CM_PointContents( feet, 0 ) & RIPPLE_LIQUID )
		|| ( trap->CM_PointContents( head, 0 ) & ( RIPPLE_LIQUID | CONTENTS_SOLID ) )
		|| !Ripples_Surface( head, feet, at ) ) {
		e->wet = qfalse;
		return qtrue;
	}
	if ( !e->wet ) {
		// into the water: a large ring, by how fast he fell. (One who is first seen standing in it came in before.)
		speed = fabsf( cent->currentState.pos.trDelta[2] );
		if ( !fresh || speed > 120.0f ) {
			Ripples_Add( at, Q_min( 80.0f + speed * 0.16f, 170.0f ), Com_Clamp( 0.45f, 1.0f, 0.45f + speed / 450.0f ), 2300 );
		}
		e->wet = qtrue;
		e->nextIdle = cg.time + Q_irand( 900, 1500 );
		VectorCopy( at, e->last );
		return qtrue;
	}
	VectorSet( way, at[0] - e->last[0], at[1] - e->last[1], 0.0f );
	dist = VectorLength( way );
	if ( dist > 300.0f ) {
		// (he was put somewhere else)
		VectorCopy( at, e->last );
	} else if ( dist >= RIPPLE_STRIDE ) {
		// a step: its ring a little to one side of his way
		const float side = e->right ? 5.0f : -5.0f;
		vec3_t		from;

		speed = Q_min( sqrtf( Square( cent->currentState.pos.trDelta[0] ) + Square( cent->currentState.pos.trDelta[1] ) ), 400.0f );
		VectorSet( from, at[0] + way[1] / dist * side, at[1] - way[0] / dist * side, at[2] );
		Ripples_Add( from, 56.0f + speed * 0.06f, Com_Clamp( 0.4f, 0.85f, 0.4f + speed / 650.0f ), 2200 );
		e->right = (qboolean)!e->right;
		e->nextIdle = cg.time + 1200;
		VectorCopy( at, e->last );
	} else if ( cg.time >= e->nextIdle && !( cent->currentState.eFlags & EF_DEAD ) ) {
		Ripples_Add( at, 34.0f, 0.15f, 1900 );
		e->nextIdle = cg.time + Q_irand( 1400, 2300 );
	}
	return qtrue;
}

/*
=================
CG_RippleMissile

A missile that is drawn this frame (CG_Missile): where it goes through the surface of water, into it or out of it
=================
*/
void CG_RippleMissile( centity_t *cent ) {
	const int	num = cent->currentState.number;
	const int	weapon = cent->currentState.weapon;
	rippleEnt_t	*e;
	qboolean	fresh, in;
	vec3_t		at;

	if ( !Ripples_On() || num < 0 || num >= MAX_GENTITIES ) {
		return;
	}
	e = &rippleEnts[num];
	fresh = (qboolean)( cg.time - e->seen > RIPPLE_GAP_MSEC || cg.time < e->seen );
	e->seen = cg.time;
	in = (qboolean)( ( trap->CM_PointContents( cent->lerpOrigin, 0 ) & RIPPLE_LIQUID ) != 0 );
	// (a way too long for one frame: another missile got the number)
	if ( !fresh && in != e->wet && DistanceSquared( e->last, cent->lerpOrigin ) < 600.0f * 600.0f
		&& ( in ? Ripples_Surface( e->last, cent->lerpOrigin, at ) : Ripples_Surface( cent->lerpOrigin, e->last, at ) ) ) {
		Ripples_Splash( at, (qboolean)( weapon == WP_ROCKET_LAUNCHER || weapon == WP_THERMAL || weapon == WP_CONCUSSION
			|| weapon == WP_DET_PACK || weapon == WP_TRIP_MINE ) );
	}
	e->wet = in;
	VectorCopy( cent->lerpOrigin, e->last );
}

// a shot that is there at once (the disruptor), from start to where it hit: where it goes into water
void CG_RippleShot( const vec3_t start, const vec3_t end ) {
	vec3_t at;

	if ( !Ripples_On() || ( trap->CM_PointContents( start, 0 ) & RIPPLE_LIQUID ) || !Ripples_Surface( start, end, at ) ) {
		return;
	}
	Ripples_Splash( at, qfalse );
}

/*
=================
CG_RippleBlast

Something blew up at origin (weapons/cg_explosion.c, size as there): water close over or under it gets a wide ring,
the stronger the closer, and a column of spray
=================
*/
void CG_RippleBlast( const vec3_t origin, float size ) {
	const vec3_t	up = { 0.0f, 0.0f, 1.0f };
	const float		reach = 70.0f + 90.0f * Com_Clamp( 0.3f, 1.5f, size );
	vec3_t			top, bottom, at;
	float			near_;

	if ( !Ripples_On() ) {
		return;
	}
	VectorSet( top, origin[0], origin[1], origin[2] + reach );
	VectorSet( bottom, origin[0], origin[1], origin[2] - reach );
	// (too deep under water, or none there)
	if ( ( trap->CM_PointContents( top, 0 ) & RIPPLE_LIQUID ) || !Ripples_Surface( top, bottom, at ) ) {
		return;
	}
	near_ = 1.0f - fabsf( at[2] - origin[2] ) / reach;
	Ripples_Add( at, Q_min( 110.0f + 100.0f * size, 260.0f ), Com_Clamp( 0.5f, 1.0f, 0.5f + 0.5f * near_ ), 2800 );
	if ( DistanceSquared( at, cg.refdef.vieworg ) < RIPPLE_NEAR * RIPPLE_NEAR ) {
		CG_WaterCrown( at, 1.0f );
		if ( near_ > 0.4f ) {
			trap->FX_PlayEffectID( cgs.effects.waterSplash, at, (float *)up, -1, -1, qfalse );
		}
	}
}

// once a frame of the main view: the rings that run, to the renderer
void CG_RipplesFrame( void ) {
	wetSpot_t	spots[RIPPLES];
	int			count = 0, i;

	if ( !Ripples_On() ) {
		return;
	}
	for ( i = 0; i < RIPPLES; i++ ) {
		ripple_t	*r = &ripples[i];
		const int	age = cg.time - r->time;
		wetSpot_t	*spot;

		if ( !r->life ) {
			continue;
		}
		if ( age < 0 || age >= r->life ) {
			r->life = 0;
			continue;
		}
		spot = &spots[count++];
		memset( spot, 0, sizeof( *spot ) );
		VectorCopy( r->origin, spot->origin );
		VectorSet( spot->normal, 0.0f, 0.0f, 1.0f );
		VectorCopy( r->origin, spot->hit );
		spot->radius = r->radius;
		spot->wet = r->strength;
		spot->flow = (float)age / r->life;
		spot->puddle = r->phase;
		spot->seed = (float)r->round;
		spot->kind = WET_SURFACE;
	}
	if ( count ) {
		trap->ext.R_AddWet( spots, count );
	}
}
