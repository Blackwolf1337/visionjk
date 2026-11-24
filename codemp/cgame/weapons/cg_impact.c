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

// cg_impact.c -- what a shot does to the wall it hits (issue #22): sparks on metal, splinters on wood, dust on stone,
// each with a sound of its own over the stock effect, and a bolt that bounces off metal whines away. Most levels name
// no material, so the texture under the hit is looked up in steamaudio/materials.txt first (ext.R_SurfaceMaterial).

#include "cgame/cg_local.h"
#include "cgame/weapons/cg_impact.h"

#define IMPACT_SOUNDS		3
#define RICOCHET_SOUNDS		2
#define IMPACT_SOUND_GAP	70			// msec between two sounds of a kind: a burst of shards is not a machine gun
#define RICOCHET_GAP		140
#define IMPACT_PUFF_GAP		35
#define IMPACT_NEAR			1600.0f		// further away: no dust and splinters
#define IMPACT_PICKS		2			// lookups of the texture a frame, each walks the surfaces of the level
#define TRACK_GONE_MSEC		250			// not seen for this long: another missile in the same slot

typedef struct {
	gpuSparks_t	sparks;
	float		count;
	float		puff;		// radius of the dust, 0: none
	float		r, g, b;
	int			puffs;		// clouds of it
	int			chips;
} impactLook_t;

static const impactLook_t impactLooks[IMPACT_KINDS] = {
	{ GPUSPARKS_DRAG_STONE,	2.4f,	38.0f,	0.62f, 0.58f, 0.52f,	3,	0 },
	{ GPUSPARKS_DRAG_METAL,	2.6f,	0.0f,	0.0f, 0.0f, 0.0f,		0,	0 },
	{ GPUSPARKS_DRAG_WOOD,	1.2f,	20.0f,	0.42f, 0.30f, 0.18f,	2,	3 },
	{ GPUSPARKS_DRAG_GLASS,	3.0f,	0.0f,	0.0f, 0.0f, 0.0f,		0,	0 },
	{ GPUSPARKS_DRAG_COLD,	3.0f,	28.0f,	0.80f, 0.85f, 0.90f,	2,	0 },
	{ GPUSPARKS_DRAG_SOFT,	3.0f,	30.0f,	0.36f, 0.28f, 0.20f,	3,	0 },
};

static const char *impactNames[IMPACT_KINDS] = { "stone", "metal", "wood", "glass", "cold", "soft" };

typedef struct {
	int		trTime, seen;
	vec3_t	dir;
} boltTrack_t;

static sfxHandle_t	impactSounds[IMPACT_KINDS][IMPACT_SOUNDS];
static sfxHandle_t	ricochetSounds[RICOCHET_SOUNDS];
static qhandle_t	dustShader;
static int			nextSound[IMPACT_KINDS], nextRicochet, nextPuff;
static int			pickTime, picks;
static boltTrack_t	tracks[MAX_GENTITIES];

void CG_ImpactInit( void ) {
	memset( tracks, 0, sizeof( tracks ) );
	memset( nextSound, 0, sizeof( nextSound ) );
	nextRicochet = nextPuff = pickTime = picks = 0;
}

void CG_ImpactRegister( void ) {
	int k, i;

	for ( k = 0; k < IMPACT_KINDS; k++ ) {
		for ( i = 0; i < IMPACT_SOUNDS; i++ ) {
			impactSounds[k][i] = trap->S_RegisterSound( va( "sound/vision/impact/%s%i.wav", impactNames[k], i + 1 ) );
		}
	}
	for ( i = 0; i < RICOCHET_SOUNDS; i++ ) {
		ricochetSounds[i] = trap->S_RegisterSound( va( "sound/weapons/flechette/ricochet%i.wav", i + 1 ) );	// (of the game)
	}
	dustShader = trap->R_RegisterShader( "gfx/vision/impact_dust" );
}

impactKind_t CG_ImpactKindOf( int material ) {
	switch ( material ) {
	case MATERIAL_SOLIDMETAL:
	case MATERIAL_HOLLOWMETAL:
	case MATERIAL_ARMOR:
	case MATERIAL_COMPUTER:
		return IMPACT_METAL;
	case MATERIAL_SOLIDWOOD:
	case MATERIAL_HOLLOWWOOD:
		return IMPACT_WOOD;
	case MATERIAL_GLASS:
	case MATERIAL_BPGLASS:
	case MATERIAL_SHATTERGLASS:
		return IMPACT_GLASS;
	case MATERIAL_WATER:
	case MATERIAL_SNOW:
	case MATERIAL_ICE:
		return IMPACT_COLD;
	case MATERIAL_SHORTGRASS:
	case MATERIAL_LONGGRASS:
	case MATERIAL_DIRT:
	case MATERIAL_SAND:
	case MATERIAL_MUD:
	case MATERIAL_FLESH:
	case MATERIAL_DRYLEAVES:
	case MATERIAL_GREENLEAVES:
	case MATERIAL_FABRIC:
	case MATERIAL_CANVAS:
	case MATERIAL_RUBBER:
	case MATERIAL_PLASTIC:
	case MATERIAL_CARPET:
		return IMPACT_SOFT;
	default:
		return IMPACT_STONE;
	}
}

// a shot and not something that blows up, which throws its own bits; and how much of it reaches the wall
static float Impact_Scale( int weapon, qboolean altFire ) {
	switch ( weapon ) {
	case WP_BRYAR_PISTOL:
	case WP_BRYAR_OLD:
	case WP_BLASTER:
	case WP_BOWCASTER:
	case WP_TURRET:
	case WP_EMPLACED_GUN:
		return 1.0f;
	case WP_DISRUPTOR:
		return 1.4f;
	case WP_REPEATER:
		return altFire ? 0.0f : 0.6f;
	case WP_DEMP2:
		return altFire ? 0.0f : 1.0f;
	case WP_FLECHETTE:
		return altFire ? 0.0f : 0.5f;
	case WP_CONCUSSION:
		return altFire ? 1.2f : 0.0f;
	default:
		return 0.0f;
	}
}

static int Impact_TableMaterial( const vec3_t start, const vec3_t end ) {
	static int	checked = -1;

	if ( checked < 0 ) {
		char value[8];

		trap->Cvar_VariableStringBuffer( "v_cgameSurfaceMaterial", value, sizeof( value ) );
		checked = atoi( value ) ? 1 : 0;
	}
	if ( !checked || !trap->ext.R_SurfaceMaterial ) {
		return MATERIAL_NONE;
	}
	if ( pickTime != cg.time ) {
		pickTime = cg.time;
		picks = 0;
	}
	if ( picks >= IMPACT_PICKS ) {
		return MATERIAL_NONE;
	}
	picks++;
	return trap->ext.R_SurfaceMaterial( start, end );
}

// qfalse: no wall there, or one that takes no marks
static qboolean Impact_Kind( const vec3_t at, const vec3_t normal, qboolean metal, impactKind_t *kind ) {
	trace_t	tr;
	vec3_t	start, end;
	int		material = MATERIAL_NONE;

	VectorMA( at, 3.0f, normal, start );
	VectorMA( at, -8.0f, normal, end );
	CG_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.fraction >= 1.0f || ( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT ) ) ) {
		return qfalse;
	}
	if ( tr.entityNum == ENTITYNUM_WORLD ) {
		material = Impact_TableMaterial( start, end );
	}
	if ( material == MATERIAL_NONE ) {
		material = tr.surfaceFlags & MATERIAL_MASK;
	}
	if ( material == MATERIAL_NONE && ( metal || ( tr.surfaceFlags & SURF_METALSTEPS ) ) ) {
		material = MATERIAL_SOLIDMETAL;
	}
	*kind = CG_ImpactKindOf( material );
	return qtrue;
}

static void Impact_Chips( const vec3_t at, const vec3_t normal, int count ) {
	int i, k;

	for ( i = 0; i < count; i++ ) {
		const qhandle_t	model = cgs.media.chunkModels[Q_irand( CHUNK_CRATE1, CHUNK_CRATE2 )][Q_irand( 0, 3 )];
		localEntity_t	*le;
		refEntity_t		*re;
		vec3_t			dir;

		if ( !model ) {
			continue;
		}
		le = CG_AllocLocalEntity();
		re = &le->refEntity;
		re->hModel = model;
		re->renderfx |= RF_MINLIGHT;	// (a splinter is no black speck in a dark room)
		le->leType = LE_FRAGMENT;
		le->endTime = cg.time + Q_irand( 700, 1200 );
		VectorMA( at, 1.5f, normal, re->origin );
		VectorCopy( re->origin, le->pos.trBase );
		VectorSet( dir, normal[0] + flrand( -0.6f, 0.6f ), normal[1] + flrand( -0.6f, 0.6f ), normal[2] + flrand( -0.2f, 0.8f ) );
		VectorNormalize( dir );
		VectorScale( dir, flrand( 90.0f, 210.0f ), le->pos.trDelta );
		VectorSet( le->angles.trBase, flrand( 0.0f, 360.0f ), flrand( 0.0f, 360.0f ), flrand( 0.0f, 360.0f ) );
		VectorSet( le->angles.trDelta, flrand( -700.0f, 700.0f ), flrand( -700.0f, 700.0f ), 0.0f );
		le->pos.trType = TR_GRAVITY;
		le->angles.trType = TR_LINEAR;
		le->pos.trTime = le->angles.trTime = cg.time;
		le->bounceFactor = 0.25f;
		le->leFlags |= LEF_TUMBLE;
		le->leBounceSoundType = LEBS_NONE;
		le->radius = flrand( 0.07f, 0.14f );
		re->nonNormalizedAxes = qtrue;
		AxisCopy( axisDefault, re->axis );
		for ( k = 0; k < 3; k++ ) {
			re->modelScale[k] = le->radius;
		}
		ScaleModelAxis( re );
	}
}

static void Impact_Show( impactKind_t kind, const vec3_t at, const vec3_t normal, float scale ) {
	const impactLook_t	*look = &impactLooks[kind];

	CG_GpuSparks( at, normal, look->sparks, look->count * scale );
	if ( DistanceSquared( at, cg.refdef.vieworg ) > IMPACT_NEAR * IMPACT_NEAR ) {
		return;
	}
	if ( look->puff > 0.0f && dustShader && cg.time >= nextPuff ) {
		const int	puffs = Q_max( 1, (int)( look->puffs * scale + 0.5f ) );
		int			i, k;

		for ( i = 0; i < puffs; i++ ) {
			vec3_t p, vel;

			// (off the wall by what v_softParticles fades away, or there is nothing left of it)
			VectorMA( at, 11.0f + i * 5.0f, normal, p );
			VectorScale( normal, flrand( 30.0f, 75.0f ), vel );
			for ( k = 0; k < 3; k++ ) {
				p[k] += flrand( -look->puff, look->puff ) * 0.25f;
				vel[k] += flrand( -22.0f, 22.0f );
			}
			vel[2] += 9.0f;
			CG_SmokePuff( p, vel, look->puff * ( 0.7f + 0.3f * scale ) * flrand( 0.8f, 1.2f ), look->r, look->g, look->b, 0.85f,
				Q_irand( 850, 1400 ), cg.time, 0, 0, dustShader );
		}
		nextPuff = cg.time + IMPACT_PUFF_GAP;
	}
	if ( look->chips ) {
		Impact_Chips( at, normal, scale >= 1.0f ? look->chips : 1 );
	}
}

void CG_BoltImpact( int weapon, qboolean altFire, const vec3_t at, const vec3_t normal, qboolean metal ) {
	const float		scale = Impact_Scale( weapon, altFire );
	impactKind_t	kind;

	if ( !v_weaponImpacts.integer || scale <= 0.0f || CG_JPInSeparatedDuel() || !Impact_Kind( at, normal, metal, &kind ) ) {
		return;
	}
	Impact_Show( kind, at, normal, scale );
	if ( cg.time >= nextSound[kind] ) {
		trap->S_StartSound( at, ENTITYNUM_WORLD, CHAN_AUTO, impactSounds[kind][Q_irand( 0, IMPACT_SOUNDS - 1 )] );
		nextSound[kind] = cg.time + IMPACT_SOUND_GAP;
	}
}

void CG_BoltTrack( const centity_t *cent ) {
	const entityState_t	*s = &cent->currentState;
	boltTrack_t			*t;
	vec3_t				dir, normal;
	impactKind_t		kind;
	qboolean			fresh;

	if ( !v_weaponImpacts.integer || s->number < 0 || s->number >= MAX_GENTITIES
		|| Impact_Scale( s->weapon, (qboolean)!!( s->eFlags & EF_ALT_FIRING ) ) <= 0.0f ) {
		return;
	}
	t = &tracks[s->number];
	if ( VectorNormalize2( s->pos.trDelta, dir ) <= 0.0f ) {
		t->seen = 0;
		return;
	}
	fresh = (qboolean)( !t->seen || cg.time < t->seen || cg.time - t->seen > TRACK_GONE_MSEC );
	// (a turn with no wall next to it was a blade or a push)
	if ( !fresh && s->pos.trTime != t->trTime && DotProduct( dir, t->dir ) < 0.97f ) {
		VectorSubtract( dir, t->dir, normal );
		if ( VectorNormalize( normal ) > 0.0f && !CG_JPInSeparatedDuel() && Impact_Kind( s->pos.trBase, normal, qfalse, &kind ) ) {
			if ( kind == IMPACT_METAL ) {
				CG_GpuSparks( s->pos.trBase, dir, GPUSPARKS_DRAG_METAL, 1.2f );
				if ( cg.time >= nextRicochet ) {
					trap->S_StartSound( NULL, s->number, CHAN_AUTO, ricochetSounds[Q_irand( 0, RICOCHET_SOUNDS - 1 )] );
					nextRicochet = cg.time + RICOCHET_GAP;
				}
			} else {
				Impact_Show( kind, s->pos.trBase, normal, 0.5f );
			}
		}
	}
	t->trTime = s->pos.trTime;
	t->seen = cg.time;
	VectorCopy( dir, t->dir );
}
