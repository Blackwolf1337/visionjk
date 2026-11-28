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

// cg_wallfx.c -- what stays on a wall after a shot for a while (issue #22, items 18 and 19): the DEMP2 leaves arcs
// that crawl over the surface around the hit for a second and light it blue, a shard of the flechette stays stuck in
// the wall the way it flew in, glows orange-white, cools to dark and goes after some seconds.
// The game turns a missile into its impact event in the same frame it stops it, with its speed cleared, so the way a
// shard flew comes from where the cgame saw it last (CG_WallFxTrack).

#include "cgame/cg_local.h"
#include "cgame/weapons/cg_wallfx.h"
#include "cgame/force/cg_lightning.h"

#define ARC_PATCHES			8
#define ARC_MSEC			1000
#define ARC_ALT_MSEC		1300
#define ARC_STEP_MSEC		55			// the arcs jump to a new shape this often
#define ARC_FLICKER_MSEC	40
#define ARC_POINTS			7
#define ARC_DIRS			16			// how far the surface reaches round a hit, measured once
#define ARC_LIT_APART		48.0f		// one light for hits closer together than this
#define ARC_LIGHTS			4
#define ARC_FAR				2500.0f
#define SHARDS				24			// stuck at once, the oldest goes
#define SHARD_TRACKS		32
#define SHARD_TRACK_MSEC	250			// a shard not seen for this long hit nothing of ours
#define SHARD_TRACK_REACH	400.0f		// units from where it was seen last to the hit
#define SHARD_HOT_MSEC		3000		// it cools to dark in that time
#define SHARD_DARK_MSEC		4000		// stays dark that long
#define SHARD_FADE_MSEC		1000		// and fades out
#define SHARD_LIGHT_MSEC	500
#define SHARD_LIGHTS		4
#define SHARD_FAR			2500.0f

typedef struct {
	int		birth;			// 0: free
	int		life;
	vec3_t	origin, normal, right, up;
	float	radius;
	int		arcs;
	int		seed;
	float	reach[ARC_DIRS];	// along the surface in each direction, before it ends or a wall stands in the way
} arcPatch_t;

typedef struct {
	int		birth;			// 0: free
	vec3_t	origin;			// of the model, a bit into the wall
	vec3_t	glow;			// its end out of the wall
	matrix3_t axis;			// scaled
	int		model;
} shard_t;

typedef struct {
	int		number, seen;	// seen 0: free
	int		trTime;			// of its flight: a new one, it bounced
	float	speed;
	vec3_t	origin, dir;
} shardTrack_t;

static arcPatch_t	patches[ARC_PATCHES];
static int			nextPatch;
static shard_t		shards[SHARDS];
static int			nextShard;
static shardTrack_t	tracks[SHARD_TRACKS];
static qhandle_t	flashShader, flareShader;

void CG_WallFxInit( void ) {
	memset( patches, 0, sizeof( patches ) );
	memset( shards, 0, sizeof( shards ) );
	memset( tracks, 0, sizeof( tracks ) );
	nextPatch = nextShard = 0;
}

void CG_WallFxRegister( void ) {
	// (all of the game, additive)
	flashShader = trap->R_RegisterShader( "gfx/misc/lightningFlash" );
	flareShader = trap->R_RegisterShader( "gfx/effects/whiteFlare" );
}

// the surface under a hit; qfalse: none there, or the sky. world: it is the level and not a door or a lift
static qboolean WallFx_Surface( const vec3_t at, const vec3_t normal, qboolean *world ) {
	trace_t	tr;
	vec3_t	start, end;

	VectorMA( at, 3.0f, normal, start );
	VectorMA( at, -8.0f, normal, end );
	CG_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.fraction >= 1.0f || ( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT ) ) ) {
		return qfalse;
	}
	*world = (qboolean)( tr.entityNum == ENTITYNUM_WORLD );
	return qtrue;
}

static void WallFx_Sprite( qhandle_t shader, const vec3_t at, float radius, const vec3_t color, float level ) {
	refEntity_t	re;
	int			i;

	memset( &re, 0, sizeof( re ) );
	re.reType = RT_SPRITE;
	VectorCopy( at, re.origin );
	re.radius = radius;
	re.customShader = shader;
	for ( i = 0; i < 3; i++ ) {
		re.shaderRGBA[i] = (byte)Com_Clamp( 0.0f, 255.0f, color[i] * level * 255.0f );
	}
	re.shaderRGBA[3] = 255;
	re.renderfx = RF_NOSHADOW;
	trap->R_AddRefEntityToScene( &re );
}

/*
=================
DEMP2 arcs
=================
*/

// How far the surface of the hit reaches in each direction: the arcs crawl over it, and not on into the air past
// the edge of a crate or into a wall next to it. Steps out along the surface until it is gone under the point or a
// wall is in the way.
static void Arc_Reach( arcPatch_t *patch ) {
	const float step = 4.0f;
	int			d;

	for ( d = 0; d < ARC_DIRS; d++ ) {
		const float	a = d * ( 2.0f * M_PI / ARC_DIRS );
		vec3_t		dir, last, point, above, below;
		trace_t		tr;
		float		r;

		VectorScale( patch->right, cosf( a ), dir );
		VectorMA( dir, sinf( a ), patch->up, dir );
		VectorMA( patch->origin, 1.0f, patch->normal, last );
		patch->reach[d] = 0.0f;
		for ( r = step; r <= patch->radius * 1.1f; r += step ) {
			VectorMA( patch->origin, r, dir, point );
			VectorMA( point, 1.0f, patch->normal, point );
			CG_Trace( &tr, last, NULL, NULL, point, ENTITYNUM_NONE, MASK_SOLID );
			if ( tr.fraction < 1.0f || tr.startsolid ) {
				break;
			}
			VectorMA( point, 3.0f, patch->normal, above );
			VectorMA( point, -4.0f, patch->normal, below );
			CG_Trace( &tr, above, NULL, NULL, below, ENTITYNUM_NONE, MASK_SOLID );
			if ( tr.fraction >= 1.0f || tr.startsolid || DotProduct( tr.plane.normal, patch->normal ) < 0.7f ) {
				break;
			}
			patch->reach[d] = r;
			VectorCopy( point, last );
		}
	}
}

static void WallFx_Arcs( const vec3_t hit, const vec3_t hitNormal, qboolean altFire ) {
	arcPatch_t	*patch = &patches[nextPatch];
	trace_t		tr;
	vec3_t		start, end, at, normal;

	nextPatch = ( nextPatch + 1 ) % ARC_PATCHES;
	patch->birth = cg.time;
	patch->life = altFire ? ARC_ALT_MSEC : ARC_MSEC;
	// onto the surface: a shot ends a few units in front of it, and from there Arc_Reach found no surface under its
	// steps, so the arcs stayed a speck in the middle
	VectorCopy( hit, at );
	VectorCopy( hitNormal, normal );
	VectorMA( hit, 8.0f, hitNormal, start );
	VectorMA( hit, -16.0f, hitNormal, end );
	CG_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.fraction < 1.0f && !tr.startsolid ) {
		VectorCopy( tr.endpos, at );
		VectorCopy( tr.plane.normal, normal );
	}
	// (off the wall, or the lines that face the view cut into it)
	VectorMA( at, 1.2f, normal, patch->origin );
	VectorCopy( normal, patch->normal );
	MakeNormalVectors( normal, patch->right, patch->up );
	patch->radius = altFire ? 40.0f : 24.0f;
	patch->arcs = altFire ? 5 : 3;
	patch->seed = rand();
	Arc_Reach( patch );
}

// a point on the surface of the patch at r units and the angle a (radians), lift units off it; no further out than
// the surface reaches there (Arc_Reach)
static void Arc_Point( const arcPatch_t *patch, float r, float a, float lift, vec3_t out ) {
	const int sector = ( (int)floorf( a / ( 2.0f * M_PI / ARC_DIRS ) + 0.5f ) % ARC_DIRS + ARC_DIRS ) % ARC_DIRS;

	r = Q_min( r, patch->reach[sector] );
	VectorMA( patch->origin, r * cosf( a ), patch->right, out );
	VectorMA( out, r * sinf( a ), patch->up, out );
	VectorMA( out, lift, patch->normal, out );
}

// drawn as the bolts of force lightning (force/cg_lightning.c CG_LightningStrand): white core, blue glow
#define ARC_SPLIT			4			// pieces between two points of an arc, each moved aside a little: jagged at every size

static void Arc_Draw( const arcPatch_t *patch, int arc, float t, float level ) {
	ribbon_t			r;
	vec3_t				points[ARC_POINTS];
	// the same shape until the next step: random from a seed of the patch, the arc and the step
	int					seed = patch->seed + arc * 7919 + ( cg.time / ARC_STEP_MSEC ) * 104729;
	float				a0, a1, r0, r1;
	int					i, k;

	if ( Q_random( &seed ) < 0.25f ) {
		return;		// (out for a moment)
	}
	// they crawl around the hit as the second goes and reach further out
	a0 = arc * ( 2.0f * M_PI / patch->arcs ) + t * 2.5f + Q_crandom( &seed ) * 0.4f;
	a1 = a0 + Q_crandom( &seed ) * 0.9f;
	r0 = patch->radius * 0.15f * Q_random( &seed );
	r1 = patch->radius * ( 0.5f + 0.5f * Q_random( &seed ) ) * ( 0.7f + 0.3f * t );
	Arc_Point( patch, r0, a0, 0.4f, points[0] );
	for ( i = 1; i < ARC_POINTS; i++ ) {
		const float f = i / (float)( ARC_POINTS - 1 );
		float		jag = 0.0f;

		if ( i < ARC_POINTS - 1 ) {
			jag = Q_crandom( &seed ) * patch->radius * 0.12f;
		}
		Arc_Point( patch, r0 + ( r1 - r0 ) * f + jag * 0.5f, a0 + ( a1 - a0 ) * f + jag / Q_max( 4.0f, r1 ), 0.4f + Q_random( &seed ) * 0.8f, points[i] );
	}
	// drawn as the bolts of force lightning (force/cg_lightning.c): white core, blue glow, thinner to its end
	r.numPoints = ( ARC_POINTS - 1 ) * ARC_SPLIT + 1;
	r.kind = 0;
	for ( i = 0; i < ARC_POINTS - 1; i++ ) {
		const float length = Distance( points[i], points[i + 1] );

		for ( k = 0; k < ARC_SPLIT; k++ ) {
			const int	n = i * ARC_SPLIT + k;
			const float	f = k / (float)ARC_SPLIT;
			vec3_t		*p = &r.p[n];

			VectorSubtract( points[i + 1], points[i], *p );
			VectorMA( points[i], f, *p, *p );
			if ( k ) {
				VectorMA( *p, Q_crandom( &seed ) * length * 0.15f, patch->right, *p );
				VectorMA( *p, Q_crandom( &seed ) * length * 0.15f, patch->up, *p );
			}
		}
	}
	VectorCopy( points[ARC_POINTS - 1], r.p[r.numPoints - 1] );
	for ( i = 0; i < r.numPoints; i++ ) {
		r.width[i] = 0.55f - 0.35f * i / (float)( r.numPoints - 1 );
		r.bright[i] = 1.0f;
	}
	CG_LightningStrand( &r, Q_min( level * 1.3f, 1.0f ), 1.6f );
}

static void WallFx_ArcsScene( void ) {
	vec3_t	lit[ARC_LIGHTS];
	int		lights = 0;
	int		i, k;

	for ( i = 0; i < ARC_PATCHES; i++ ) {
		arcPatch_t	*patch = &patches[i];
		const int	age = cg.time - patch->birth;
		float		t, level, flicker;
		int			seed;
		vec3_t		at;

		if ( !patch->birth ) {
			continue;
		}
		if ( age < 0 || age >= patch->life ) {
			patch->birth = 0;
			continue;
		}
		if ( DistanceSquared( patch->origin, cg.refdef.vieworg ) > ARC_FAR * ARC_FAR ) {
			continue;
		}
		t = age / (float)patch->life;
		seed = patch->seed ^ ( cg.time / ARC_FLICKER_MSEC );
		flicker = 0.55f + 0.45f * Q_random( &seed );
		level = sqrtf( 1.0f - t ) * flicker;
		for ( k = 0; k < patch->arcs; k++ ) {
			Arc_Draw( patch, k, t, level );
		}
		{
			const vec3_t white = { 0.7f, 0.8f, 1.0f };

			WallFx_Sprite( flashShader, patch->origin, patch->radius * 0.3f * flicker, white, level );
		}
		// (one light for hits close together: four on one spot burnt it out to a flat disc with a hard edge)
		for ( k = 0; k < lights; k++ ) {
			if ( DistanceSquared( lit[k], patch->origin ) < ARC_LIT_APART * ARC_LIT_APART ) {
				break;
			}
		}
		if ( k == lights && lights < ARC_LIGHTS ) {
			VectorMA( patch->origin, 10.0f, patch->normal, at );
			trap->R_AddLightToScene( at, patch->radius * 4.0f * ( 0.6f + 0.4f * flicker ) * sqrtf( 1.0f - t ),
				0.3f, 0.4f, 0.7f );
			VectorCopy( patch->origin, lit[lights] );
			lights++;
		}
	}
	CG_RibbonFlush();
}

/*
=================
Flechette shards
=================
*/

static void WallFx_Shard( const vec3_t at, const vec3_t normal, const vec3_t flew );

// A flechette shard bounces off walls five to eight times (g_missile.c G_BounceMissile, FL_BOUNCE_SHRAPNEL) and
// sends no event for it, and one that comes to rest on the floor goes without one: so a piece of it stays in the
// wall where it hit one at speed, seen here as a new flight in another direction (as CG_BoltTrack finds a ricochet).
#define SHARD_STICK_SPEED	1500.0f		// after the first bounce it has a quarter of its speed: one piece a shard

void CG_WallFxTrack( const centity_t *cent ) {
	const entityState_t	*s = &cent->currentState;
	shardTrack_t		*slot = NULL;
	vec3_t				dir;
	float				speed;
	qboolean			same = qfalse;
	int					i;

	if ( s->weapon != WP_FLECHETTE || ( s->eFlags & EF_ALT_FIRING ) || ( speed = VectorNormalize2( s->pos.trDelta, dir ) ) <= 0.0f ) {
		return;
	}
	for ( i = 0; i < SHARD_TRACKS; i++ ) {
		if ( tracks[i].seen && tracks[i].number == s->number ) {
			slot = &tracks[i];
			same = (qboolean)( cg.time >= slot->seen && cg.time - slot->seen <= SHARD_TRACK_MSEC );
			break;
		}
		if ( !slot || tracks[i].seen < slot->seen ) {
			slot = &tracks[i];
		}
	}
	if ( same && s->pos.trTime != slot->trTime && slot->speed >= SHARD_STICK_SPEED && DotProduct( dir, slot->dir ) < 0.97f
		&& v_weaponImpacts.integer && !CG_JPInSeparatedDuel() ) {
		vec3_t		normal;
		qboolean	world;

		VectorSubtract( dir, slot->dir, normal );
		if ( VectorNormalize( normal ) > 0.0f && WallFx_Surface( s->pos.trBase, normal, &world ) && world ) {
			WallFx_Shard( s->pos.trBase, normal, slot->dir );
		}
	}
	slot->number = s->number;
	slot->seen = cg.time;
	slot->trTime = s->pos.trTime;
	slot->speed = speed;
	VectorCopy( cent->lerpOrigin, slot->origin );
	VectorCopy( dir, slot->dir );
}

// the way the shard that hit at flew, from the shard seen last nearest to there; qfalse: none known
static qboolean WallFx_ShardDir( const vec3_t at, vec3_t dir ) {
	shardTrack_t	*best = NULL;
	float			bestDist = SHARD_TRACK_REACH * SHARD_TRACK_REACH;
	int				i;

	for ( i = 0; i < SHARD_TRACKS; i++ ) {
		shardTrack_t	*track = &tracks[i];
		float			dist;

		if ( !track->seen || cg.time < track->seen || cg.time - track->seen > SHARD_TRACK_MSEC ) {
			continue;
		}
		dist = DistanceSquared( track->origin, at );
		if ( dist < bestDist ) {
			bestDist = dist;
			best = track;
		}
	}
	if ( !best ) {
		return qfalse;
	}
	VectorCopy( best->dir, dir );
	best->seen = 0;
	return qtrue;
}

// flew: the way it came, NULL: from the shard seen last nearest to there (WallFx_ShardDir)
static void WallFx_Shard( const vec3_t at, const vec3_t normal, const vec3_t flew ) {
	shard_t		*shard;
	vec3_t		dir, side;
	float		into;
	qhandle_t	model = cgs.media.chunkModels[CHUNK_METAL2][0];	// (metal1_1.md3: long along its y)

	if ( !model ) {
		return;
	}
	if ( flew ) {
		VectorCopy( flew, dir );
	} else if ( !WallFx_ShardDir( at, dir ) ) {
		VectorNegate( normal, dir );
	}
	// into the wall at least a bit, a shard that grazed it would lie flat on it
	into = DotProduct( dir, normal );
	if ( into > -0.35f ) {
		VectorMA( dir, -( into + 0.35f ), normal, dir );
		if ( VectorNormalize( dir ) <= 0.0f ) {
			VectorNegate( normal, dir );
		}
	}
	shard = &shards[nextShard];
	nextShard = ( nextShard + 1 ) % SHARDS;
	shard->birth = cg.time;
	shard->model = model;
	VectorMA( at, 0.6f, dir, shard->origin );
	VectorMA( at, -2.6f, dir, shard->glow );
	// the long axis of the model the way it flew, turned at random about it
	PerpendicularVector( side, dir );
	RotatePointAroundVector( shard->axis[0], dir, side, flrand( 0.0f, 360.0f ) );
	CrossProduct( shard->axis[0], dir, shard->axis[2] );
	VectorScale( shard->axis[0], 0.25f, shard->axis[0] );
	VectorScale( dir, 0.75f, shard->axis[1] );
	VectorScale( shard->axis[2], 0.25f, shard->axis[2] );
}

static void WallFx_ShardsScene( void ) {
	int lights = 0;
	int k;

	// newest first, they get the lights
	for ( k = 1; k <= SHARDS; k++ ) {
		shard_t		*shard = &shards[( nextShard - k + SHARDS ) % SHARDS];
		const int	age = cg.time - shard->birth;
		refEntity_t	re;
		float		heat;

		if ( !shard->birth ) {
			continue;
		}
		if ( age < 0 || age >= SHARD_HOT_MSEC + SHARD_DARK_MSEC + SHARD_FADE_MSEC ) {
			shard->birth = 0;
			continue;
		}
		if ( DistanceSquared( shard->origin, cg.refdef.vieworg ) > SHARD_FAR * SHARD_FAR ) {
			continue;
		}
		heat = age < SHARD_HOT_MSEC ? 1.0f - age / (float)SHARD_HOT_MSEC : 0.0f;

		memset( &re, 0, sizeof( re ) );
		re.reType = RT_MODEL;
		re.hModel = shard->model;
		VectorCopy( shard->origin, re.origin );
		VectorCopy( shard->origin, re.oldorigin );
		VectorCopy( shard->origin, re.lightingOrigin );
		AxisCopy( shard->axis, re.axis );
		re.nonNormalizedAxes = qtrue;
		// the metal tinted by its heat, dark when cold; lit as the wall around it, never black
		re.renderfx = RF_MINLIGHT | RF_NOSHADOW | RF_RGB_TINT;
		re.shaderRGBA[0] = (byte)( 70.0f + 185.0f * heat );
		re.shaderRGBA[1] = (byte)( 66.0f + 120.0f * heat * heat );
		re.shaderRGBA[2] = (byte)( 62.0f + 60.0f * heat * heat * heat );
		re.shaderRGBA[3] = 255;
		if ( age > SHARD_HOT_MSEC + SHARD_DARK_MSEC ) {
			re.renderfx |= RF_FORCE_ENT_ALPHA;
			re.shaderRGBA[3] = (byte)( 255.0f * ( 1.0f - ( age - SHARD_HOT_MSEC - SHARD_DARK_MSEC ) / (float)SHARD_FADE_MSEC ) );
		}
		trap->R_AddRefEntityToScene( &re );

		if ( heat > 0.0f ) {
			vec3_t color;

			// white-orange, then red, then nothing
			VectorSet( color, 1.0f, 0.35f + 0.55f * heat * heat, 0.08f + 0.5f * heat * heat * heat );
			WallFx_Sprite( flareShader, shard->glow, 2.5f + 3.5f * heat, color, powf( heat, 1.3f ) );
			if ( age < SHARD_LIGHT_MSEC && lights < SHARD_LIGHTS ) {
				const float left = 1.0f - age / (float)SHARD_LIGHT_MSEC;

				trap->R_AddLightToScene( shard->glow, 28.0f * left + 8.0f, 1.0f * left, 0.5f * left, 0.15f * left );
				lights++;
			}
		}
	}
}

void CG_WallFxHit( int weapon, qboolean altFire, const vec3_t at, const vec3_t wallNormal ) {
	qboolean	world;
	vec3_t		normal;

	// (the blast of the DEMP2 in the air, at the end of its reach, has no wall)
	if ( !v_weaponImpacts.integer || CG_JPInSeparatedDuel() || VectorNormalize2( wallNormal, normal ) < 0.5f ) {
		return;
	}
	if ( weapon == WP_DEMP2 ) {
		if ( WallFx_Surface( at, normal, &world ) ) {
			WallFx_Arcs( at, normal, altFire );
		}
	} else if ( weapon == WP_FLECHETTE && !altFire ) {
		// (on a door or a lift it would hang in the air once that moves)
		if ( WallFx_Surface( at, normal, &world ) && world ) {
			WallFx_Shard( at, normal, NULL );
		}
	}
}

void CG_WallFxScene( void ) {
	WallFx_ArcsScene();
	WallFx_ShardsScene();
}
