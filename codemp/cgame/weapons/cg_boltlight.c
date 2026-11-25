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

// cg_boltlight.c -- every bolt lights what it flies past and flashes at a blade that deflects it (issue #22). The
// renderer has 32 lights for everything, so only the bolts nearest to the view get one. No switch of its own: these are
// moving lights like those of the game (v_dlightScale).

#include "cgame/cg_local.h"
#include "cgame/weapons/cg_boltlight.h"

#define BOLT_LIGHTS		8
#define BOLT_FLASHES	6
#define FLASH_MSEC		140
#define FLASH_RADIUS	150.0f
#define FLASH_MERGE		96.0f		// closer to a flash still burning: the same one
#define BOLT_REACH		1.6f		// of the radius of Bolt_Look: far enough for the shadows it casts to be seen
#define CHARGE_REACH	0.8f		// of the same radius, for a weapon fully charged

typedef struct {
	vec3_t	at, color;
	float	radius, dist;
} boltLight_t;

typedef struct {
	vec3_t	at;
	int		time;
} boltFlash_t;

static boltLight_t	lights[BOLT_LIGHTS];
static int			numLights;
static boltFlash_t	flashes[BOLT_FLASHES];
static int			nextFlash;

void CG_BoltLightsInit( void ) {
	memset( flashes, 0, sizeof( flashes ) );
	numLights = nextFlash = 0;
}

static qboolean Bolt_Look( int weapon, qboolean altFire, float *radius, vec3_t color ) {
	switch ( weapon ) {
	case WP_BRYAR_PISTOL:
	case WP_BRYAR_OLD:
	case WP_BLASTER:
	case WP_TURRET:
	case WP_EMPLACED_GUN:
		*radius = 120.0f;
		VectorSet( color, 1.0f, 0.32f, 0.12f );
		return qtrue;
	case WP_BOWCASTER:
		*radius = 130.0f;
		VectorSet( color, 0.25f, 1.0f, 0.25f );
		return qtrue;
	case WP_REPEATER:
		*radius = altFire ? 150.0f : 100.0f;
		if ( altFire ) {
			VectorSet( color, 0.4f, 0.7f, 1.0f );
		} else {
			VectorSet( color, 1.0f, 0.75f, 0.2f );
		}
		return qtrue;
	case WP_DEMP2:
		*radius = 130.0f;
		VectorSet( color, 0.35f, 0.55f, 1.0f );
		return qtrue;
	case WP_DISRUPTOR:		// (has no bolt, this is for its charge)
		*radius = 150.0f;
		VectorSet( color, 1.0f, 0.7f, 0.2f );
		return qtrue;
	case WP_FLECHETTE:
		*radius = 75.0f;
		VectorSet( color, 1.0f, 0.7f, 0.25f );
		return qtrue;
	case WP_ROCKET_LAUNCHER:
		*radius = 180.0f;
		VectorSet( color, 1.0f, 0.55f, 0.2f );
		return qtrue;
	case WP_CONCUSSION:
		*radius = 160.0f;
		VectorSet( color, 0.4f, 0.6f, 1.0f );
		return qtrue;
	default:
		return qfalse;
	}
}

// a bolt in one of the slots, the nearest ones to the view win
static void Bolt_Add( const centity_t *cent, float radius, const vec3_t color ) {
	boltLight_t	*slot;
	float		dist;

	dist = DistanceSquared( cent->lerpOrigin, cg.refdef.vieworg );
	if ( numLights < BOLT_LIGHTS ) {
		slot = &lights[numLights++];
	} else {
		int i;

		slot = &lights[0];
		for ( i = 1; i < BOLT_LIGHTS; i++ ) {
			if ( lights[i].dist > slot->dist ) {
				slot = &lights[i];
			}
		}
		if ( dist >= slot->dist ) {
			return;
		}
	}
	VectorCopy( cent->lerpOrigin, slot->at );
	VectorCopy( color, slot->color );
	slot->radius = radius * BOLT_REACH;
	slot->dist = dist;
}

qboolean CG_BoltLight( const centity_t *cent ) {
	const entityState_t	*s = &cent->currentState;
	float				radius;
	vec3_t				color;

	if ( !Bolt_Look( s->weapon, (qboolean)!!( s->eFlags & EF_ALT_FIRING ), &radius, color ) ) {
		return qfalse;
	}
	Bolt_Add( cent, radius, color );
	return qtrue;
}

// The shots of vehicles fly with an effect of their own and the game gives them WP_BLASTER whatever they are, so the
// colour goes by the weapon file (ext_data/vehicles/weapons/*.vwp): the lasers of the Empire (TIE, imp_laser) green,
// those of the rebels, the YT-1300, the AT-ST and the swoops red, the ion blaster blue, concussion missiles blue-white,
// torpedoes and rockets orange and larger. Bombs and mines that drift or lie about have none. laser: one of the first
// two, a bolt like that of a blaster.
static qboolean VehicleBolt_Look( const vehWeaponInfo_t *weapon, float *radius, vec3_t color, qboolean *laser ) {
	const char	*name = weapon->name ? weapon->name : "";

	*radius = 120.0f;
	*laser = qfalse;
	if ( weapon->bHasGravity || Q_stristr( name, "mine" ) || Q_stristr( name, "bomb" ) ) {
		return qfalse;
	}
	if ( weapon->bIonWeapon ) {
		*radius = 150.0f;
		VectorSet( color, 0.35f, 0.55f, 1.0f );
	} else if ( Q_stristr( name, "conc" ) ) {
		*radius = 160.0f;
		VectorSet( color, 0.4f, 0.6f, 1.0f );
	} else if ( weapon->fWidth > 0.0f || weapon->fHeight > 0.0f || Q_stristr( name, "torp" ) || Q_stristr( name, "rocket" )
		|| Q_stristr( name, "missile" ) ) {
		*radius = 180.0f;
		VectorSet( color, 1.0f, 0.55f, 0.2f );
	} else if ( Q_stristr( name, "imp" ) || Q_stristr( name, "tie" ) ) {
		VectorSet( color, 0.25f, 1.0f, 0.25f );
		*laser = qtrue;
	} else {
		VectorSet( color, 1.0f, 0.32f, 0.12f );
		*laser = qtrue;
	}
	return qtrue;
}

qboolean CG_VehicleBoltLight( const centity_t *cent, const vehWeaponInfo_t *weapon ) {
	float		radius;
	vec3_t		color;
	qboolean	laser;

	if ( !VehicleBolt_Look( weapon, &radius, color, &laser ) ) {
		return qfalse;
	}
	Bolt_Add( cent, radius, color );
	return qtrue;
}

qboolean CG_BoltColor( int weapon, qboolean altFire, vec3_t color ) {
	float radius;

	return Bolt_Look( weapon, altFire, &radius, color );
}

qboolean CG_VehicleLaserColor( const vehWeaponInfo_t *weapon, vec3_t color ) {
	float		radius;
	qboolean	laser;

	return (qboolean)( VehicleBolt_Look( weapon, &radius, color, &laser ) && laser );
}

void CG_ChargeLight( int weapon, const vec3_t at, float charge ) {
	float	radius, level;
	vec3_t	color;

	if ( !Bolt_Look( weapon, qfalse, &radius, color ) ) {
		return;
	}
	charge = Com_Clamp( 0.0f, 1.0f, charge );
	level = 0.25f + 0.75f * charge;
	trap->R_AddLightToScene( at, 30.0f + charge * radius * CHARGE_REACH, color[0] * level, color[1] * level, color[2] * level );
}

void CG_BoltLightFlash( const vec3_t at ) {
	int i;

	// (a blade under fire is one light that goes on flashing: added up they burn out the whole room)
	for ( i = 0; i < BOLT_FLASHES; i++ ) {
		if ( flashes[i].time && cg.time - flashes[i].time < FLASH_MSEC
			&& DistanceSquared( at, flashes[i].at ) < FLASH_MERGE * FLASH_MERGE ) {
			VectorCopy( at, flashes[i].at );
			flashes[i].time = cg.time;
			return;
		}
	}
	VectorCopy( at, flashes[nextFlash].at );
	flashes[nextFlash].time = cg.time;
	nextFlash = ( nextFlash + 1 ) % BOLT_FLASHES;
}

void CG_BoltLightsAdd( qboolean skyPortal ) {
	int i;

	for ( i = 0; i < numLights; i++ ) {
		trap->R_AddLightToScene( lights[i].at, lights[i].radius, lights[i].color[0], lights[i].color[1], lights[i].color[2] );
	}
	numLights = 0;
	if ( skyPortal ) {
		return;
	}
	for ( i = 0; i < BOLT_FLASHES; i++ ) {
		const int age = cg.time - flashes[i].time;

		if ( flashes[i].time && age >= 0 && age < FLASH_MSEC ) {
			const float left = 1.0f - age / (float)FLASH_MSEC;

			trap->R_AddLightToScene( flashes[i].at, FLASH_RADIUS * left, 0.7f, 0.6f, 0.42f );
		}
	}
}
