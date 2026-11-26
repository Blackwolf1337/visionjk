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

// cg_explosion.c -- what an explosion does besides its effect (issue #22). It throws bits of what it hit, by the
// material there, and a cloud of its dust, and lets dust and grit fall from a ceiling above it (item 2). On a floor it
// leaves a few small fires that light the room, die down and smoke (item 4). Close by it throws dirt onto the view
// (item 6, SCREENFX_DIRT of screenfx/cg_screenfx.c). A rocket fired throws up dust behind the shooter, and the view of
// the own player jolts (item 13). The thermal detonator is in cg_thermal.c (item 3).
//
// All of it is on v_weaponImpacts, the dirt on the view on v_fxBlast. The game sends the same events as ever, only the
// look changes.

#include "cgame/cg_local.h"
#include "cgame/weapons/cg_impact.h"
#include "cgame/weapons/cg_explosion.h"
#include "cgame/level/cg_gust.h"
#include "cgame/level/cg_ripples.h"
#include "cgame/level/cg_smoke.h"

#define SURFACE_REACH		48.0f		// units around the blast it looks for what it went off on
#define DEBRIS_NEAR			2500.0f		// further away: no bits, no dust cloud
#define CEILING_REACH		300.0f		// a ceiling up to this far above the blast sheds dust
#define DUSTFALL_GAP		300			// msec: the two bombs of a flechette shot shake one ceiling
#define FLOOR_NORMAL		0.7f		// a surface this flat is a floor that can burn
#define FIRES				8
#define FIRE_PATCHES		4
#define FIRE_SMOKE_MSEC		1500		// it smokes on that long after the last flame
#define FIRE_FAR			3000.0f		// further away the fires burn on unseen
#define DIRT_REACH			350.0f		// units from the eye, for a rocket

typedef struct {
	int		birth, life;				// birth 0: none; life: msec the flames burn
	vec3_t	center;						// where its light is
	int		numPatches;
	vec3_t	patches[FIRE_PATCHES];
	int		nextFlame[FIRE_PATCHES], nextSmoke[FIRE_PATCHES];
	float	seed;
} fire_t;

// the bits an explosion knocks off, by the kind of what it hit (the order of impactKind_t); glass has no chunk model,
// the renderer throws its shards
static const impactChips_t debrisChips[IMPACT_KINDS] = {
	{ CHUNK_ROCK1, CHUNK_ROCK2,			0.3f, 0.7f,		200.0f, 460.0f,		2500, 4000,	0.3f,	LEBS_ROCK },
	{ CHUNK_METAL1, CHUNK_METAL2,		0.25f, 0.5f,	220.0f, 480.0f,		2500, 4000,	0.4f,	LEBS_METAL },
	{ CHUNK_CRATE1, CHUNK_CRATE2,		0.2f, 0.45f,	200.0f, 440.0f,		2500, 4000,	0.3f,	LEBS_NONE },
	{ 0, 0,								0.0f, 0.0f,		0.0f, 0.0f,			0, 0,		0.0f,	LEBS_NONE },
	{ CHUNK_WHITE_METAL, CHUNK_WHITE_METAL,	0.2f, 0.4f,	180.0f, 400.0f,		2000, 3000,	0.25f,	LEBS_NONE },
	{ CHUNK_ROCK3, CHUNK_ROCK3,			0.2f, 0.4f,		160.0f, 360.0f,		2000, 3000,	0.2f,	LEBS_NONE },
};

// what a shaken ceiling lets fall, by what it is made of: small bits that drop more than fly (speed along the
// normal of the ceiling, which points down)
static const impactChips_t ceilingChips[IMPACT_KINDS] = {
	{ CHUNK_ROCK1, CHUNK_ROCK2,			0.08f, 0.2f,	10.0f, 60.0f,		2500, 3500,	0.25f,	LEBS_ROCK },
	{ CHUNK_METAL1, CHUNK_METAL2,		0.06f, 0.14f,	10.0f, 50.0f,		2500, 3500,	0.35f,	LEBS_METAL },
	{ CHUNK_CRATE1, CHUNK_CRATE2,		0.06f, 0.16f,	10.0f, 50.0f,		2500, 3500,	0.25f,	LEBS_NONE },
	{ 0, 0,								0.0f, 0.0f,		0.0f, 0.0f,			0, 0,		0.0f,	LEBS_NONE },
	{ CHUNK_WHITE_METAL, CHUNK_WHITE_METAL,	0.06f, 0.14f,	5.0f, 40.0f,	2000, 3000,	0.1f,	LEBS_NONE },
	{ CHUNK_ROCK3, CHUNK_ROCK3,			0.07f, 0.16f,	10.0f, 50.0f,		2000, 3000,	0.15f,	LEBS_NONE },
};

static fire_t		fires[FIRES];
static fxHandle_t	flameEffect, smokeEffect, dustFallEffect;
static fxHandle_t	rocketSmokeEffect, thermalSmokeEffect;	// 0: not rd-vulkan-vision, the effects of the game smoke
static qhandle_t	dustShader;
static int			nextDustFall;

void CG_ExplosionInit( void ) {
	memset( fires, 0, sizeof( fires ) );
	nextDustFall = 0;
	CG_ThermalInit();
}

void CG_ExplosionRegister( void ) {
	// (of the game)
	flameEffect = trap->FX_RegisterEffect( "effects/env/small_fire.efx" );
	smokeEffect = trap->FX_RegisterEffect( "effects/volumetric/black_smoke.efx" );
	dustFallEffect = trap->FX_RegisterEffect( "effects/chunks/dustfall.efx" );
	dustShader = trap->R_RegisterShader( "gfx/vision/impact_dust" );
	// (the rolling smoke of the two explosions that rd-vulkan-vision has its own effects for, effects/vision/fb: it is
	// apart from them, for where the blast leaves no smoke with depth)
	rocketSmokeEffect = thermalSmokeEffect = 0;
	if ( CG_SmokeSheet() ) {
		rocketSmokeEffect = trap->FX_RegisterEffect( "effects/vision/fb/rocket/smoke.efx" );
		thermalSmokeEffect = trap->FX_RegisterEffect( "effects/vision/fb/thermal/smoke.efx" );
	}
	CG_ThermalRegister();
}

// how big the blast of a weapon is, 1 for a rocket, 0 for what does not explode; and how many fires it leaves at most
// (the bolts of energy of the concussion rifle and the repeater set nothing on fire)
static float Explosion_Size( int weapon, qboolean altFire, int *numFires ) {
	*numFires = 0;
	switch ( weapon ) {
	case WP_ROCKET_LAUNCHER:
	case WP_THERMAL:
		*numFires = 3;
		return 1.0f;
	case WP_DET_PACK:
		*numFires = 4;
		return 1.3f;
	case WP_TRIP_MINE:
		*numFires = 3;
		return 1.15f;
	case WP_FLECHETTE:
		*numFires = altFire ? 1 : 0;
		return altFire ? 0.45f : 0.0f;
	case WP_CONCUSSION:
		return altFire ? 0.0f : 0.7f;
	case WP_REPEATER:
		return altFire ? 0.5f : 0.0f;
	default:
		return 0.0f;
	}
}

// the surface nearest to the blast: the event of a bomb carries no normal, and a blast on a player none of the floor
static qboolean Explosion_Surface( const vec3_t origin, const vec3_t dir, vec3_t at, vec3_t normal ) {
	static const vec3_t	ways[6] = { { 0, 0, -1 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 } };
	vec3_t				start, end, back, way;
	trace_t				tr;
	float				best = 1.0f;
	qboolean			hasDir;
	int					i;

	hasDir = (qboolean)( VectorNormalize2( dir, back ) > 0.0f );
	// (off the surface it sits on, or the first trace starts in it)
	VectorCopy( origin, start );
	if ( hasDir ) {
		VectorMA( origin, 4.0f, back, start );
		VectorScale( back, -1.0f, back );
	}
	for ( i = hasDir ? -1 : 0; i < (int)ARRAY_LEN( ways ); i++ ) {
		VectorCopy( i < 0 ? back : ways[i], way );
		VectorMA( start, SURFACE_REACH, way, end );
		CG_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.startsolid || tr.allsolid || tr.fraction >= best ) {
			continue;
		}
		best = tr.fraction;
		VectorCopy( tr.endpos, at );
		VectorCopy( tr.plane.normal, normal );
	}
	return (qboolean)( best < 1.0f );
}

// A cloud of the dust of what it hit, and bits of it. cloud: not where the blast left smoke with depth (level/
// cg_smoke.c): the sprites rose for two seconds over smoke that is there at once and falls.
static void Explosion_Debris( const vec3_t at, const vec3_t normal, impactKind_t kind, float size, qboolean cloud ) {
	const impactChips_t	*chips = &debrisChips[kind];
	vec3_t				color;
	int					i, k;

	if ( DistanceSquared( at, cg.refdef.vieworg ) > DEBRIS_NEAR * DEBRIS_NEAR ) {
		return;
	}
	if ( chips->lifeMax ) {
		CG_ImpactChips( at, normal, chips, (int)( 10.0f * size + 0.5f ) );
	} else if ( kind == IMPACT_GLASS ) {
		CG_GpuSparks( at, normal, GPUSPARKS_DRAG_GLASS, 4.0f * size );
		trap->S_StartSound( at, ENTITYNUM_WORLD, CHAN_AUTO, cgs.media.glassChunkSound );
	}
	if ( !cloud || !dustShader || !CG_ImpactDustColor( kind, color ) ) {
		return;
	}
	for ( i = (int)( 4.0f * size + 0.5f ); i > 0; i-- ) {
		vec3_t p, vel;

		VectorMA( at, 14.0f + i * 6.0f, normal, p );
		VectorScale( normal, flrand( 25.0f, 60.0f ), vel );
		for ( k = 0; k < 3; k++ ) {
			p[k] += flrand( -20.0f, 20.0f ) * size;
			vel[k] += flrand( -30.0f, 30.0f );
		}
		vel[2] += 10.0f;
		CG_SmokePuff( p, vel, flrand( 45.0f, 75.0f ) * size, color[0], color[1], color[2], 0.7f, Q_irand( 1600, 2600 ),
			cg.time, 0, 0, dustShader );
	}
}

// a ceiling close above the blast sheds dust for some seconds, and grit
static void Explosion_CeilingDust( const vec3_t origin, const vec3_t dir, float size ) {
	trace_t			tr;
	vec3_t			start, end, at, color;
	int				material, surfaceFlags, i;
	float			strength;
	impactKind_t	kind;

	if ( cg.time < nextDustFall && nextDustFall - cg.time <= DUSTFALL_GAP ) {
		return;
	}
	// (off what it hit: a rocket into the ceiling shakes that one)
	VectorNormalize2( dir, start );
	VectorMA( origin, 4.0f, start, start );
	VectorSet( end, start[0], start[1], start[2] + CEILING_REACH );
	CG_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.startsolid || tr.fraction >= 1.0f || tr.plane.normal[2] > -0.5f
		|| !CG_ImpactMaterial( tr.endpos, tr.plane.normal, qfalse, &material, &surfaceFlags ) ) {
		return;
	}
	if ( DistanceSquared( tr.endpos, cg.refdef.vieworg ) > DEBRIS_NEAR * DEBRIS_NEAR ) {
		return;
	}
	nextDustFall = cg.time + DUSTFALL_GAP;
	strength = size * ( 1.0f - 0.5f * tr.fraction );
	kind = CG_ImpactKindOf( material );
	VectorSet( at, tr.endpos[0], tr.endpos[1], tr.endpos[2] - 2.0f );
	// What falls is what the ceiling is made of (steamaudio/materials.txt): the grey dust and falling rocks of the
	// stock effect only from stone, bits of it from all but glass, glittering splinters from glass. Nothing hot: the
	// spark kinds of the hits glow.
	if ( kind == IMPACT_STONE && dustFallEffect ) {
		vec3_t down = { 0.0f, 0.0f, -1.0f };

		trap->FX_PlayEffectID( dustFallEffect, at, down, -1, -1, qfalse );
	}
	for ( i = 0; i < 3; i++ ) {
		vec3_t p;

		VectorSet( p, at[0] + flrand( -40.0f, 40.0f ), at[1] + flrand( -40.0f, 40.0f ), at[2] );
		if ( ceilingChips[kind].firstChunk ) {
			CG_ImpactChips( p, tr.plane.normal, &ceilingChips[kind], 1 + (int)( strength * ( kind == IMPACT_METAL ? 0.7f : 1.5f ) ) );
		} else if ( kind == IMPACT_GLASS ) {
			CG_GpuSparks( p, tr.plane.normal, GPUSPARKS_DRAG_GLASS, 0.6f * strength );
		}
	}
	if ( kind == IMPACT_GLASS ) {
		trap->S_StartSound( at, ENTITYNUM_WORLD, CHAN_AUTO, cgs.media.glassChunkSound );
		return;		// (glass has no dust to shed)
	}
	if ( !dustShader ) {
		return;
	}
	// (a metal ceiling sheds only the dust that lay on it: thin and dark)
	if ( !CG_ImpactDustColor( kind, color ) ) {
		VectorSet( color, 0.33f, 0.31f, 0.29f );
	}
	for ( i = 0; i < ( kind == IMPACT_METAL ? 1 : 2 + (int)strength ); i++ ) {
		vec3_t p, vel;

		VectorSet( p, at[0] + flrand( -40.0f, 40.0f ), at[1] + flrand( -40.0f, 40.0f ), at[2] - flrand( 14.0f, 24.0f ) );
		VectorSet( vel, flrand( -6.0f, 6.0f ), flrand( -6.0f, 6.0f ), -flrand( 10.0f, 22.0f ) );
		CG_SmokePuff( p, vel, flrand( 35.0f, 55.0f ) * Q_max( 0.6f, strength ), color[0], color[1], color[2], 0.45f,
			Q_irand( 2500, 3500 ), cg.time, 0, 0, dustShader );
	}
}

// a floor that burns there: solid, flat, not under water, one that takes marks
static qboolean Explosion_FireSpot( const vec3_t from, float height, vec3_t spot ) {
	trace_t	tr;
	vec3_t	start, end;

	VectorSet( start, from[0], from[1], height + 24.0f );
	VectorSet( end, from[0], from[1], height - 40.0f );
	CG_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.startsolid || tr.fraction >= 1.0f || tr.plane.normal[2] < FLOOR_NORMAL || fabsf( tr.endpos[2] - height ) > 20.0f
		|| ( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT | SURF_NOMARKS ) ) ) {
		return qfalse;
	}
	VectorSet( spot, tr.endpos[0], tr.endpos[1], tr.endpos[2] + 1.0f );
	if ( CG_PointContents( spot, ENTITYNUM_NONE ) & ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA ) ) {
		return qfalse;
	}
	return qtrue;
}

static void Explosion_Fires( const vec3_t at, float size, int count ) {
	fire_t	*f = &fires[0];
	int		i, tries;

	// a free one, else the oldest
	for ( i = 0; i < FIRES; i++ ) {
		if ( !fires[i].birth ) {
			f = &fires[i];
			break;
		}
		if ( fires[i].birth < f->birth ) {
			f = &fires[i];
		}
	}
	memset( f, 0, sizeof( *f ) );
	count = Q_irand( Q_max( 1, count - 1 ), count );
	for ( tries = 0; tries < count * 3 && f->numPatches < count; tries++ ) {
		const float	angle = flrand( 0.0f, 2.0f * M_PI );
		const float	dist = tries ? flrand( 10.0f, 44.0f * size ) : flrand( 0.0f, 12.0f );
		vec3_t		from;

		VectorSet( from, at[0] + cosf( angle ) * dist, at[1] + sinf( angle ) * dist, at[2] );
		if ( Explosion_FireSpot( from, at[2], f->patches[f->numPatches] ) ) {
			f->nextFlame[f->numPatches] = cg.time + Q_irand( 0, 120 );
			f->numPatches++;
		}
	}
	if ( !f->numPatches ) {
		return;
	}
	f->birth = cg.time ? cg.time : 1;
	f->life = Q_irand( 4000, 8000 );
	f->seed = flrand( 0.0f, 100.0f );
	VectorSet( f->center, at[0], at[1], at[2] + 16.0f );
}

static void Explosion_Dirt( const vec3_t origin, float size ) {
	const float	reach = DIRT_REACH * ( 0.6f + 0.4f * Q_min( size, 1.0f ) );
	vec3_t		delta;
	trace_t		tr;
	float		dist;

	VectorSubtract( origin, cg.refdef.vieworg, delta );
	dist = VectorLength( delta );
	// (behind the head it does not reach the eyes)
	if ( dist >= reach || DotProduct( delta, cg.refdef.viewaxis[0] ) < -0.3f * dist ) {
		return;
	}
	// the eye sees it; the blast sits on a wall, so a trace may stop a little short of it
	CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, origin, cg.predictedPlayerState.clientNum, MASK_SOLID );
	if ( tr.fraction < 1.0f && DistanceSquared( tr.endpos, origin ) > 16.0f * 16.0f ) {
		return;
	}
	CG_ScreenFxDirt( origin, ( 1.0f - dist / reach ) * Com_Clamp( 0.4f, 1.0f, size ) );
}

static void Explosion_Go( int weapon, qboolean altFire, const vec3_t origin, const vec3_t dir ) {
	int				numFires, material, surfaceFlags;
	const float		size = Explosion_Size( weapon, altFire, &numFires );
	qboolean		smoke;
	vec3_t			at, normal;
	impactKind_t	kind;

	if ( size <= 0.0f ) {
		return;
	}
	Explosion_Dirt( origin, size );
	// water close by gets a ring and throws up spray (level/cg_ripples.c)
	CG_RippleBlast( origin, size );
	// its blast moves the smoke in the room and its own stays for a while (level/cg_smoke.c)
	smoke = CG_SmokeBlast( origin, dir, size );
	if ( !smoke ) {
		const fxHandle_t rolling = weapon == WP_ROCKET_LAUNCHER ? rocketSmokeEffect : weapon == WP_THERMAL ? thermalSmokeEffect : 0;

		if ( rolling ) {
			vec3_t from, out;

			// (in the air dir has no length)
			VectorCopy( origin, from );
			VectorCopy( dir, out );
			if ( VectorNormalize( out ) < 0.1f ) {
				VectorSet( out, 0.0f, 0.0f, 1.0f );
			}
			trap->FX_PlayEffectID( rolling, from, out, -1, -1, qfalse );
		}
	}
	if ( !v_weaponImpacts.integer ) {
		return;
	}
	if ( Explosion_Surface( origin, dir, at, normal ) && CG_ImpactMaterial( at, normal, qfalse, &material, &surfaceFlags ) ) {
		kind = CG_ImpactKindOf( material );
		Explosion_Debris( at, normal, kind, size, (qboolean)!smoke );
		CG_GustBlast( at, normal, material, size );
		// (nothing burns on snow, ice or water)
		if ( numFires && normal[2] >= FLOOR_NORMAL && kind != IMPACT_COLD && !( surfaceFlags & SURF_NOMARKS ) ) {
			Explosion_Fires( at, size, numFires );
		}
	}
	Explosion_CeilingDust( origin, dir, size );
}

void CG_Explosion( int weapon, qboolean altFire, const vec3_t origin, const vec3_t dir ) {
	// (the trip mine sends an EV_MISSILE_MISS besides its effect)
	if ( weapon == WP_TRIP_MINE || weapon == WP_DET_PACK || weapon == WP_FLECHETTE ) {
		return;
	}
	Explosion_Go( weapon, altFire, origin, dir );
}

void CG_ExplosionEffect( int effect, const vec3_t origin, const vec3_t dir ) {
	switch ( effect ) {
	case EFFECT_EXPLOSION_DETPACK:
		Explosion_Go( WP_DET_PACK, qfalse, origin, dir );
		break;
	case EFFECT_EXPLOSION_TRIPMINE:
		Explosion_Go( WP_TRIP_MINE, qfalse, origin, dir );
		break;
	case EFFECT_EXPLOSION_FLECHETTE:
		Explosion_Go( WP_FLECHETTE, qtrue, origin, dir );
		break;
	default:
		break;
	}
}

void CG_ExplosionsAdd( void ) {
	vec3_t	up = { 0.0f, 0.0f, 1.0f };
	int		i, k;

	for ( i = 0; i < FIRES; i++ ) {
		fire_t		*f = &fires[i];
		const int	age = cg.time - f->birth;
		float		heat, flicker;
		qboolean	seen;

		if ( !f->birth ) {
			continue;
		}
		if ( age < 0 || age >= f->life + FIRE_SMOKE_MSEC || !v_weaponImpacts.integer ) {
			f->birth = 0;
			continue;
		}
		// the flames burn high first and die down, the smoke comes as they sink
		heat = age < f->life ? 1.0f - (float)age / f->life : 0.0f;
		heat = sqrtf( heat );
		seen = (qboolean)( DistanceSquared( f->center, cg.refdef.vieworg ) < FIRE_FAR * FIRE_FAR );
		for ( k = 0; k < f->numPatches && seen; k++ ) {
			if ( heat > 0.1f && cg.time >= f->nextFlame[k] && flameEffect ) {
				trap->FX_PlayEffectID( flameEffect, f->patches[k], up, -1, -1, qfalse );
				f->nextFlame[k] = cg.time + 120 + (int)( ( 1.0f - heat ) * 480.0f ) + Q_irand( 0, 60 );
			}
			if ( age > f->life / 4 && cg.time >= f->nextSmoke[k] && smokeEffect ) {
				vec3_t p;

				VectorSet( p, f->patches[k][0], f->patches[k][1], f->patches[k][2] + 6.0f );
				trap->FX_PlayEffectID( smokeEffect, p, up, -1, -1, qfalse );
				f->nextSmoke[k] = cg.time + 220 + ( age > f->life ? 200 : 0 ) + Q_irand( 0, 140 );
			}
		}
		if ( heat <= 0.05f ) {
			continue;
		}
		// one light for the patches of an explosion: the renderer has few
		flicker = 0.82f + 0.1f * sinf( cg.time * 0.031f + f->seed ) + 0.08f * sinf( cg.time * 0.077f + f->seed * 3.0f );
		trap->R_AddLightToScene( f->center, ( 80.0f + 90.0f * heat ) * flicker, 1.0f * heat, 0.5f * heat, 0.16f * heat );
	}
}

void CG_Backblast( centity_t *cent ) {
	const qboolean	own = (qboolean)( cent->currentState.number == cg.predictedPlayerState.clientNum );
	vec3_t			origin, angles, forward, right, shoulder, back, end, color, away;
	trace_t			tr;
	float			height;
	int				i, material;

	if ( cent->currentState.weapon != WP_ROCKET_LAUNCHER || !v_weaponImpacts.integer ) {
		return;
	}
	if ( own ) {
		VectorCopy( cg.predictedPlayerState.origin, origin );
		VectorCopy( cg.predictedPlayerState.viewangles, angles );
		height = cg.predictedPlayerState.viewheight;
		// a jolt of the picture only: the view kick of the game leaves the aim where it was
		if ( !cg.renderingThirdPerson ) {
			cg.kick_angles[PITCH] = flrand( -4.5f, -3.0f );
			cg.kick_angles[YAW] = flrand( -1.2f, 1.2f );
			cg.kick_angles[ROLL] = 0.0f;
			cg.kick_time = cg.time;
		}
	} else {
		VectorCopy( cent->lerpOrigin, origin );
		VectorCopy( cent->lerpAngles, angles );
		height = DEFAULT_VIEWHEIGHT;
	}
	if ( !dustShader || DistanceSquared( origin, cg.refdef.vieworg ) > DEBRIS_NEAR * DEBRIS_NEAR ) {
		return;
	}
	AngleVectors( angles, forward, right, NULL );
	// the end of the tube on the right shoulder, not through a wall behind him
	VectorSet( shoulder, origin[0], origin[1], origin[2] + height - 6.0f );
	VectorMA( shoulder, 6.0f, right, shoulder );
	VectorMA( shoulder, -26.0f, forward, end );
	CG_Trace( &tr, shoulder, NULL, NULL, end, cent->currentState.number, MASK_SOLID );
	VectorCopy( tr.endpos, back );
	for ( i = 0; i < 5; i++ ) {
		vec3_t vel;

		VectorScale( forward, -flrand( 120.0f, 260.0f ), vel );
		vel[0] += flrand( -30.0f, 30.0f );
		vel[1] += flrand( -30.0f, 30.0f );
		vel[2] += flrand( -10.0f, 30.0f );
		CG_SmokePuff( back, vel, flrand( 18.0f, 30.0f ), 0.6f, 0.58f, 0.55f, 0.6f, Q_irand( 700, 1100 ), cg.time, 0, 0, dustShader );
	}
	// and the dust of the floor below it, blown away from him
	VectorSet( end, back[0], back[1], back[2] - 96.0f );
	CG_Trace( &tr, back, NULL, NULL, end, cent->currentState.number, MASK_SOLID );
	if ( tr.startsolid || tr.fraction >= 1.0f || tr.plane.normal[2] < FLOOR_NORMAL
		|| !CG_ImpactMaterial( tr.endpos, tr.plane.normal, qfalse, &material, NULL )
		|| !CG_ImpactDustColor( CG_ImpactKindOf( material ), color ) ) {
		return;
	}
	VectorSet( away, -forward[0], -forward[1], 0.0f );
	if ( VectorNormalize( away ) <= 0.0f ) {
		VectorSet( away, 1.0f, 0.0f, 0.0f );
	}
	for ( i = 0; i < 5; i++ ) {
		vec3_t p, vel;

		VectorSet( p, tr.endpos[0], tr.endpos[1], tr.endpos[2] + 10.0f );
		VectorScale( away, flrand( 80.0f, 200.0f ) * ( 1.0f - 0.5f * tr.fraction ), vel );
		VectorMA( vel, flrand( -70.0f, 70.0f ), right, vel );
		vel[2] = flrand( 20.0f, 50.0f );
		CG_SmokePuff( p, vel, flrand( 22.0f, 34.0f ), color[0], color[1], color[2], 0.7f, Q_irand( 900, 1400 ), cg.time, 0, 0,
			dustShader );
	}
}
