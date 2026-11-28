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

// cg_lightning.c -- force lightning (issue #19). In the place of the effect of the game:
//
// - Bolts: drawn here as ribbons that face the eye, in three layers: a thin white-hot channel, a blue-violet glow of
//   the air round it and a wide faint haze (shaders gfx/vision/arc_core and arc_glow, added, bloom). The path is made
//   by midpoint displacement from a seed, so it is jagged at every scale, and branches fork off it, thinner and dimmer
//   with each fork, fading to their tips. As real lightning does, a channel keeps its path over several strokes
//   (CHANNEL_MIN to CHANNEL_MAX msec) while the branches change faster; each stroke flashes bright and dies down, and
//   the channel before shows faint for a moment when a new one strikes (the eye keeps it). The stock bolt
//   (RT_ELECTRICITY) shook its fine shape anew every frame, which read as a wriggling noodle.
//   Where the game deals the damage: levels 1 and 2 along the view as far as 2048 (ForceShootLightning, a trace),
//   level 3 to everybody in the cone of 300 units in front, and a few more into the cone that strike the walls. Each
//   strong bolt is a tube of light (ext.R_AddLineLight), flickering with the strokes.
// - Two hands: with bare hands at level 3 the game deals double damage and shows both hands out
//   (BOTH_FORCE_2HANDEDLIGHTNING_HOLD; JA+ does the same, nothing more). The game drew the effect from the left hand
//   alone; here bolts leave both hands, thicker and brighter, more of them stray into the cone, more sparks, larger
//   burn marks.
// - At the hand a glow and small arcs that crawl off the fingers; where a bolt strikes, a white flash, and with every
//   stroke sparks by what it struck (molten steel bounces, stone throws orange grit, wood embers) and blue specks.
// - Lightning that jumps: where a bolt strikes the level, side arcs jump to metal close by, and water close by
//   flashes up.
// - Arcs that stay: on the metal it struck, small arcs crawl on for a second after the lightning ends.
// - Burn marks: where a bolt strikes the level (cg_burnmark.c).
//
// The same bolts elsewhere (cg_lightning.h): the DEMP2 (its shot crackles, its charged blast sends bolts out to the
// walls in its reach, the arcs it leaves on walls, weapons/cg_wallfx.c; under v_weaponImpacts) and anybody shocked by
// lightning or the DEMP2 (arcs jump over the body and now and then to the floor, over the shell of the game).
//
// Part of Force Looks (v_fxForce). The bolts and marks are drawn by any renderer, the light needs one that takes
// tubes of light, the sparks one that simulates them.

#include "cgame/cg_local.h"
#include "cgame/force/cg_lightning.h"
#include "cgame/force/cg_burnmark.h"
#include "cgame/force/cg_ribbon.h"
#include "cgame/weapons/cg_impact.h"

#define LINE_REACH			2048.0f	// levels 1 and 2: the trace of the game
#define CONE_REACH			300.0f	// level 3: FORCE_LIGHTNING_RADIUS
#define CONE_DOT			0.5f	//	and its cone
#define CONE_TARGETS		4		// bolts to targets in the cone, at most
#define CONE_STRAYS			3		// bolts into the cone that strike the walls
#define CONE_STRAYS_TWO		5		//	with two hands
#define TWO_HANDS_WIDTH		1.25f	// two hands: the bolts that much thicker
#define TWO_HANDS_BRIGHT	1.15f	//	and brighter

#define CHANNEL_MIN			110		// a channel keeps its path that long (the strokes of a flash run down one channel)
#define CHANNEL_MAX			220
#define BRANCH_MIN			40		// its branches change that often
#define BRANCH_MAX			75
#define GHOST_MSEC			90		// the channel before shows that long, faint
#define STROKE_DECAY		55.0f	// msec in which a stroke dies down to a third
#define STROKE_FLOOR		0.45f	// how bright between strokes
#define SMALL_MSEC			35		// the small arcs at the hand change that often
#define FADE_MSEC			220		// when the cast ends, its last bolts fade out in this time

#define STRAND_POINTS		RIBBON_POINTS	// the most points of one line of a bolt (2^n + 1, midpoint displacement)
#define BOLT_STRANDS		12		// the channel and its branches

#define CORE_WIDTH			0.8f	// half widths of the layers, units, of the channel of a main bolt
#define GLOW_WIDTH			3.5f
#define HAZE_WIDTH			10.0f

#define JUMP_REACH			130.0f	// side arcs find metal that close to where a bolt strikes
#define JUMP_TRIES			2
#define WATER_REACH			128.0f	// water that close below a strike flashes
#define WATER_MSEC			180

#define STAY_SPOTS			16
#define STAY_MSEC			1100	// arcs crawl on metal that long after it was struck last
#define STAY_MERGE			24.0f
#define STAY_SIZE			18.0f
#define STAY_STEP_MSEC		60

#define BURN_GAP			28.0f	// a strike further than this from the last burn mark of the bolt burns a new one
#define BURN_MSEC			900		//	and the same spot again after this long
#define BURN_EVERY			450		// one mark of a caster at most this often, whatever his bolts strike
#define BURN_EVERY_TWO		300		//	with two hands
#define BURN_MAX			( CONE_TARGETS + CONE_STRAYS_TWO )

#define SPARK_FAR			2500.0f
#define SURFACE_GAP			4.0f	// a bolt stays that far in front of the surface it strikes

typedef enum {
	BOLT_MAIN,			// to where the game deals the damage, or to somebody
	BOLT_STRAY,			// level 3, into the walls
	BOLT_JUMP,			// to metal close by
	BOLT_SMALL			// off the hand, crawling on metal
} boltKind_t;

typedef struct {
	int		points;			// 2^n + 1
	float	rough;			// how far the path strays, of its length
	float	maxStray;		//	but no more than this, units
	float	width[2];		// at its start and its end, times the half widths of the layers
	float	bright[2];
	int		branches;		// forks off the channel
	qboolean haze;			// the wide haze round the channel
} boltLook_t;

static const boltLook_t boltLooks[] = {
	/* main */	{ 65,	0.11f,	48.0f,	{ 1.0f, 0.75f },	{ 1.0f, 0.9f },		5,	qtrue },
	/* stray */	{ 33,	0.13f,	36.0f,	{ 0.7f, 0.5f },		{ 0.8f, 0.65f },	2,	qfalse },
	/* jump */	{ 9,	0.18f,	14.0f,	{ 0.45f, 0.35f },	{ 0.75f, 0.6f },	0,	qfalse },
	/* small */	{ 5,	0.3f,	4.0f,	{ 0.3f, 0.06f },	{ 0.8f, 0.15f },	0,	qfalse },
};

// one line of a bolt: the channel or a branch (kind: it has the wide haze)
typedef ribbon_t strand_t;

// a place a bolt struck: the level (with its normal) or somebody
typedef struct {
	vec3_t			end;
	vec3_t			normal;
	int				target;			// the entity it strikes, -1: none
	qboolean		world;			// it struck the level, not a player
	impactKind_t	kind;			// of the level
} strike_t;

typedef struct {
	int			channelTime, channelLife, channelSeed;
	int			branchTime, branchLife, branchSeed;
	int			strokeTime;
	float		strokePeak;
	int			numStrikes;
	strike_t	strikes[BURN_MAX];
	vec3_t		jumps[BURN_MAX][JUMP_TRIES];
	int			numJumps[BURN_MAX];
	// the channels before, shown faint for a moment
	int			ghostTime, ghostSeed, numGhosts;
	strike_t	ghosts[BURN_MAX];
	vec3_t		burnAt[BURN_MAX];
	int			burnTime[BURN_MAX];
	int			lastBurn;			// when he left a burn mark last
	int			nextBurnBolt;		// which of his bolts burns next
	qboolean	twoHands;
	// the last frame he cast, for the fade when it ends
	int			lastCast;
	int			numHands;
	vec3_t		hands[2], from, dir;
	float		lastBright;
} caster_t;

typedef struct {
	vec3_t		origin;
	vec3_t		normal;
	int			until;
	int			seed;
} staySpot_t;

typedef struct {
	vec3_t		origin;
	int			until;
} waterFlash_t;

enum { LAYER_CORE, LAYER_GLOW, LAYERS };

// the charged blast of a DEMP2
typedef struct {
	vec3_t		origin;
	float		radius;
	int			start;			// 0: free
	int			seed;
} burst_t;

#define BURSTS				4
#define BURST_MSEC			800		// the shell of the game grows and fades that long (FX_DEMP2_AltDetonate)
#define BURST_ARCS			8
#define SHOCK_STEP_MSEC		50
#define SHOCK_ARCS			3
#define ORB_STEP_MSEC		40

static burst_t		bursts[BURSTS];
static caster_t		casters[MAX_GENTITIES];
static staySpot_t	stays[STAY_SPOTS];
static waterFlash_t	flashes[4];
static qhandle_t	layerShaders[LAYERS];
static qhandle_t	flareShader;

static const vec3_t coreColor = { 0.82f, 0.88f, 1.0f };
static const vec3_t glowColor = { 0.3f, 0.34f, 1.0f };
static const vec3_t hazeColor = { 0.42f, 0.28f, 0.95f };

void CG_LightningInit( void ) {
	memset( casters, 0, sizeof( casters ) );
	memset( stays, 0, sizeof( stays ) );
	memset( flashes, 0, sizeof( flashes ) );
	memset( bursts, 0, sizeof( bursts ) );
	CG_BurnMarksInit();
}

void CG_LightningRegister( void ) {
	layerShaders[LAYER_CORE] = trap->R_RegisterShader( "gfx/vision/arc_core" );
	layerShaders[LAYER_GLOW] = trap->R_RegisterShader( "gfx/vision/arc_glow" );
	flareShader = trap->R_RegisterShader( "gfx/effects/whiteFlare" );		// (of the game)
	CG_BurnMarksRegister();
}

static qboolean Lightning_On( void ) {
	return (qboolean)( v_fxForce.integer && layerShaders[LAYER_CORE] && layerShaders[LAYER_GLOW] );
}

static qboolean Lightning_HasLineLight( void ) {
	static int has = -1;

	if ( has < 0 ) {
		char value[8];

		trap->Cvar_VariableStringBuffer( "v_cgameLineLight", value, sizeof( value ) );
		has = ( atoi( value ) && trap->ext.R_AddLineLight ) ? 1 : 0;
	}
	return (qboolean)( has > 0 );
}

/*
===============================================================================

Drawing

===============================================================================
*/

static void Strand_Draw( const strand_t *s, float level, float width ) {
	ribbonLayer_t layers[3];

	if ( s->numPoints < 2 || level <= 0.0f ) {
		return;
	}
	memset( layers, 0, sizeof( layers ) );
	layers[0].shader = layerShaders[LAYER_CORE];
	layers[0].halfWidth = CORE_WIDTH * width;
	layers[0].minPixels = 1.0f;
	VectorCopy( coreColor, layers[0].color );
	layers[0].level = level;
	layers[1].shader = layerShaders[LAYER_GLOW];
	layers[1].halfWidth = GLOW_WIDTH * width;
	layers[1].minPixels = 2.5f;
	VectorCopy( glowColor, layers[1].color );
	layers[1].level = level * 0.42f;
	layers[2].shader = layerShaders[LAYER_GLOW];
	layers[2].halfWidth = HAZE_WIDTH * width;
	layers[2].minPixels = 6.0f;
	VectorCopy( hazeColor, layers[2].color );
	layers[2].level = level * 0.08f;
	CG_RibbonDraw( s, layers, s->kind ? 3 : 2 );
}

static void Lightning_Sprite( const vec3_t at, float radius, const vec3_t color, float level ) {
	CG_RibbonSprite( flareShader, at, radius, color, level );
}

/*
===============================================================================

Shapes

===============================================================================
*/

static void Lightning_Basis( const vec3_t fwd, vec3_t right, vec3_t up ) {
	CG_RibbonBasis( fwd, right, up );
}

// a jagged line from from to to by midpoint displacement: each halving moves the new middle points aside by less
static void Strand_Make( strand_t *s, const vec3_t from, const vec3_t to, int points, float rough, float maxStray,
	const float width[2], const float bright[2], int *seed ) {
	vec3_t	fwd, right, up;
	float	length, stray;
	int		step, i;

	VectorSubtract( to, from, fwd );
	length = VectorNormalize( fwd );
	Lightning_Basis( fwd, right, up );
	points = Q_min( points, STRAND_POINTS );
	s->numPoints = points;
	s->kind = 0;
	VectorCopy( from, s->p[0] );
	VectorCopy( to, s->p[points - 1] );
	stray = Q_min( rough * length, maxStray );
	for ( step = points - 1; step > 1; step /= 2 ) {
		const int half = step / 2;

		for ( i = 0; i + step < points; i += step ) {
			vec3_t *mid = &s->p[i + half];

			VectorAdd( s->p[i], s->p[i + step], *mid );
			VectorScale( *mid, 0.5f, *mid );
			VectorMA( *mid, Q_crandom( seed ) * stray, right, *mid );
			VectorMA( *mid, Q_crandom( seed ) * stray, up, *mid );
			VectorMA( *mid, Q_crandom( seed ) * stray * 0.2f, fwd, *mid );
		}
		// (a bit more than half: rougher than a smooth curve, as lightning is)
		stray *= 0.56f;
	}
	for ( i = 0; i < points; i++ ) {
		const float f = i / (float)( points - 1 );

		s->width[i] = width[0] + ( width[1] - width[0] ) * f;
		s->bright[i] = bright[0] + ( bright[1] - bright[0] ) * f;
	}
}

// a fork off strand parent at one of its points, thinner and dimmer, fading to its tip
static qboolean Strand_Fork( strand_t *out, const strand_t *parent, const vec3_t mainFwd, float reach, int points, int *seed ) {
	const int	n = parent->numPoints;
	vec3_t		along, right, up, dir, end;
	float		width[2], bright[2], length;
	int			i;

	if ( n < 5 ) {
		return qfalse;
	}
	i = 2 + (int)( Q_random( seed ) * ( n - 5 ) );
	VectorSubtract( parent->p[i + 1], parent->p[i], along );
	VectorNormalize( along );
	Lightning_Basis( mainFwd, right, up );
	// mostly on the way the bolt goes, off to a side
	VectorScale( mainFwd, 0.65f, dir );
	VectorMA( dir, 0.35f, along, dir );
	VectorMA( dir, Q_crandom( seed ) * 0.9f, right, dir );
	VectorMA( dir, Q_crandom( seed ) * 0.9f, up, dir );
	VectorNormalize( dir );
	length = reach * ( 1.0f - i / (float)( n - 1 ) ) * ( 0.3f + 0.5f * Q_random( seed ) ) + 12.0f;
	length = Q_min( length, 150.0f );
	VectorMA( parent->p[i], length, dir, end );
	width[0] = parent->width[i] * 0.5f;
	width[1] = width[0] * 0.15f;
	bright[0] = parent->bright[i] * 0.55f;
	bright[1] = 0.0f;
	Strand_Make( out, parent->p[i], end, points, 0.2f, length * 0.25f, width, bright, seed );
	return qtrue;
}

// the strands of a bolt from from to to: the channel from channelSeed, its branches from branchSeed (0: none)
static int Bolt_Build( strand_t *strands, const vec3_t from, const vec3_t to, boltKind_t kind, int channelSeed, int branchSeed ) {
	const boltLook_t	*look = &boltLooks[kind];
	vec3_t				fwd;
	float				length;
	int					count = 1, b;

	VectorSubtract( to, from, fwd );
	length = VectorNormalize( fwd );
	if ( length < 1.0f ) {
		return 0;
	}
	Strand_Make( &strands[0], from, to, look->points, look->rough, look->maxStray, look->width, look->bright, &channelSeed );
	strands[0].kind = look->haze;
	// all bolts of a cast leave the hand at one point, where their layers would add up to a white blob: each comes up
	// over its first units out of the glow of the hand
	if ( kind == BOLT_MAIN || kind == BOLT_STRAY ) {
		int i;

		for ( i = 0; i < strands[0].numPoints; i++ ) {
			const float out = Distance( strands[0].p[i], from ) / 28.0f;

			if ( out >= 1.0f ) {
				break;
			}
			strands[0].bright[i] *= 0.25f + 0.75f * out;
		}
	}
	if ( !branchSeed ) {
		return count;
	}
	for ( b = 0; b < look->branches && count < BOLT_STRANDS; b++ ) {
		const int first = count;

		if ( !Strand_Fork( &strands[count], &strands[0], fwd, length * 0.45f, 9, &branchSeed ) ) {
			break;
		}
		count++;
		// main bolts fork again now and then
		if ( kind == BOLT_MAIN && count < BOLT_STRANDS && Q_random( &branchSeed ) < 0.5f ) {
			vec3_t sub;

			VectorSubtract( strands[first].p[strands[first].numPoints - 1], strands[first].p[0], sub );
			VectorNormalize( sub );
			if ( Strand_Fork( &strands[count], &strands[first], sub, length * 0.15f, 5, &branchSeed ) ) {
				count++;
			}
		}
	}
	return count;
}

// onto (may be NULL): the strike the bolt ends in; on the level every point of it stays SURFACE_GAP in front of that
// surface, and branches that would run into it crawl along it. A bit that slips behind a pane of thick glass is drawn
// before the glass and shown through it, blurred (rd-vulkan-vision RB_SurfBehindGlass takes a polygon by its first
// corner).
static void Bolt_Draw( const vec3_t from, const vec3_t to, boltKind_t kind, int channelSeed, int branchSeed, float level, float width,
	const strike_t *onto ) {
	strand_t	strands[BOLT_STRANDS];
	const int	count = Bolt_Build( strands, from, to, kind, channelSeed, branchSeed );
	int			i, j;

	if ( onto && onto->world ) {
		for ( i = 0; i < count; i++ ) {
			for ( j = 0; j < strands[i].numPoints; j++ ) {
				vec3_t		off;
				const float	d = DotProduct( strands[i].p[j], onto->normal ) - DotProduct( onto->end, onto->normal );

				if ( d < SURFACE_GAP ) {
					VectorScale( onto->normal, SURFACE_GAP - d, off );
					VectorAdd( strands[i].p[j], off, strands[i].p[j] );
				}
			}
		}
	}

	for ( i = 0; i < count; i++ ) {
		Strand_Draw( &strands[i], level, width );
	}
}

/*
===============================================================================

Strikes

===============================================================================
*/

// the light of a bolt: a tube along it, else a point where it strikes
static void Lightning_Light( const vec3_t from, const vec3_t to, float range, float strength ) {
	const float r = 0.45f * strength, g = 0.55f * strength, b = 1.0f * strength;

	if ( Lightning_HasLineLight() ) {
		trap->ext.R_AddLineLight( from, to, range, r, g, b );
	} else {
		trap->R_AddLightToScene( to, range * 1.5f, r, g, b );
	}
}

static void Lightning_Stay( const vec3_t origin, const vec3_t normal ) {
	staySpot_t	*spot = NULL;
	int			i, oldest = 0;

	for ( i = 0; i < STAY_SPOTS; i++ ) {
		if ( stays[i].until > cg.time && DistanceSquared( stays[i].origin, origin ) < STAY_MERGE * STAY_MERGE ) {
			stays[i].until = cg.time + STAY_MSEC;
			return;
		}
		if ( stays[i].until <= cg.time ) {
			spot = &stays[i];
		} else if ( stays[i].until < stays[oldest].until ) {
			oldest = i;
		}
	}
	if ( !spot ) {
		spot = &stays[oldest];
	}
	VectorCopy( origin, spot->origin );
	VectorCopy( normal, spot->normal );
	spot->until = cg.time + STAY_MSEC;
	spot->seed = rand();
}

static void Lightning_WaterFlash( const vec3_t origin ) {
	int i, oldest = 0;

	for ( i = 0; i < (int)ARRAY_LEN( flashes ); i++ ) {
		if ( flashes[i].until > cg.time && DistanceSquared( flashes[i].origin, origin ) < 96.0f * 96.0f ) {
			flashes[i].until = cg.time + WATER_MSEC;
			return;
		}
		if ( flashes[i].until < flashes[oldest].until ) {
			oldest = i;
		}
	}
	VectorCopy( origin, flashes[oldest].origin );
	flashes[oldest].until = cg.time + WATER_MSEC;
}

// what a stroke knocks off the level where it strikes, by what it is made of
static void Lightning_Sparks( const strike_t *s, float more ) {
	sparkBurst_t b;

	if ( !s->world || s->kind == IMPACT_COLD || DistanceSquared( s->end, cg.refdef.vieworg ) > SPARK_FAR * SPARK_FAR ) {
		return;
	}
	memset( &b, 0, sizeof( b ) );
	VectorMA( s->end, 1.0f, s->normal, b.origin );
	VectorCopy( s->normal, b.dir );
	switch ( s->kind ) {
	case IMPACT_METAL:
		// molten steel: white-hot drops that bounce and lie glowing a moment
		b.count = 12; b.speed = 380.0f; b.spread = 1.2f; b.life = 0.6f; b.size = 0.3f;
		b.gravity = 800.0f; b.drag = 0.9f; b.bounce = 0.45f; b.cooling = 1.3f; b.rest = 0.5f;
		VectorSet( b.color, 5.0f, 4.5f, 3.8f );
		break;
	case IMPACT_WOOD:
	case IMPACT_SOFT:
		// burning bits that fly off and fall, deep orange
		b.count = 6; b.speed = 260.0f; b.spread = 1.1f; b.life = 0.55f; b.size = 0.3f;
		b.gravity = 550.0f; b.drag = 1.5f; b.bounce = 0.3f; b.cooling = 1.2f; b.rest = 0.5f;
		VectorSet( b.color, 4.5f, 1.8f, 0.5f );
		break;
	case IMPACT_GLASS:
		b.count = 5; b.speed = 240.0f; b.spread = 1.0f; b.life = 0.3f; b.size = 0.25f;
		b.gravity = 700.0f; b.drag = 1.5f; b.bounce = 0.5f; b.cooling = 1.6f;
		VectorSet( b.color, 4.0f, 4.0f, 4.5f );
		break;
	default:
		// orange grit
		b.count = 6; b.speed = 300.0f; b.spread = 1.0f; b.life = 0.35f; b.size = 0.28f;
		b.gravity = 800.0f; b.drag = 1.4f; b.bounce = 0.3f; b.cooling = 1.5f; b.rest = 0.25f;
		VectorSet( b.color, 4.2f, 2.2f, 0.9f );
		break;
	}
	b.count = (int)( b.count * more + 0.5f );
	CG_EmitSparks( &b );
	// and blue specks of the discharge, gone at once
	b.rest = b.swirl = b.streak = b.age = 0.0f;
	b.count = 6; b.speed = 350.0f; b.spread = 2.0f; b.life = 0.12f; b.size = 0.2f;
	b.gravity = 0.0f; b.drag = 3.0f; b.bounce = 0.0f; b.cooling = 2.0f;
	VectorSet( b.color, 3.0f, 3.5f, 6.0f );
	b.count = (int)( b.count * more + 0.5f );
	CG_EmitSparks( &b );
}

// a strike of the level: what it is made of, metal close by to jump to, water below to flash
static void Lightning_Strike( caster_t *c, int k, const trace_t *tr, int skip ) {
	strike_t	*s = &c->strikes[k];
	int			material, flags, j;
	trace_t		wt;
	vec3_t		from, to;

	VectorCopy( tr->endpos, s->end );
	VectorCopy( tr->plane.normal, s->normal );
	s->target = -1;
	s->world = (qboolean)( tr->fraction < 1.0f && tr->entityNum == ENTITYNUM_WORLD && !( tr->surfaceFlags & ( SURF_SKY | SURF_NOIMPACT ) ) );
	s->kind = IMPACT_STONE;
	c->numJumps[k] = 0;
	if ( !s->world ) {
		return;
	}
	if ( CG_ImpactMaterial( s->end, s->normal, qfalse, &material, &flags ) ) {
		s->kind = CG_ImpactKindOf( material );
	}
	if ( s->kind == IMPACT_METAL ) {
		Lightning_Stay( s->end, s->normal );
	}
	// side arcs to metal close by
	for ( j = 0; j < JUMP_TRIES; j++ ) {
		vec3_t dir;
		int i;

		for ( i = 0; i < 3; i++ ) {
			dir[i] = s->normal[i] + flrand( -1.0f, 1.0f ) * 1.3f;
		}
		VectorNormalize( dir );
		VectorMA( s->end, 4.0f, s->normal, from );
		VectorMA( from, JUMP_REACH, dir, to );
		CG_Trace( &wt, from, NULL, NULL, to, skip, MASK_SOLID );
		if ( wt.fraction >= 1.0f || wt.entityNum != ENTITYNUM_WORLD || ( wt.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT ) ) ) {
			continue;
		}
		if ( !CG_ImpactMaterial( wt.endpos, wt.plane.normal, qfalse, &material, &flags ) || CG_ImpactKindOf( material ) != IMPACT_METAL ) {
			continue;
		}
		VectorCopy( wt.endpos, c->jumps[k][c->numJumps[k]++] );
		Lightning_Stay( wt.endpos, wt.plane.normal );
	}
	// water below lights up
	VectorMA( s->end, 8.0f, s->normal, from );
	VectorCopy( from, to );
	to[2] -= WATER_REACH;
	CG_Trace( &wt, from, NULL, NULL, to, skip, MASK_SOLID | CONTENTS_WATER );
	if ( wt.fraction < 1.0f && ( wt.contents & CONTENTS_WATER ) ) {
		Lightning_WaterFlash( wt.endpos );
	}
	if ( CG_PointContents( s->end, -1 ) & CONTENTS_WATER ) {
		Lightning_WaterFlash( s->end );
	}
}

// burn marks where his bolts strike the level: one mark now and then, by turns of his bolts, and not twice on one
// spot soon (the stray bolts of level 3 strike somewhere else with every channel)
static void Lightning_Burn( caster_t *c ) {
	int i;

	if ( cg.time - c->lastBurn < ( c->twoHands ? BURN_EVERY_TWO : BURN_EVERY ) && cg.time >= c->lastBurn ) {
		return;
	}
	for ( i = 0; i < c->numStrikes; i++ ) {
		const int		k = ( c->nextBurnBolt + i ) % c->numStrikes;
		const strike_t	*s = &c->strikes[k];

		if ( !s->world ) {
			continue;
		}
		if ( c->burnTime[k] && cg.time - c->burnTime[k] < BURN_MSEC && DistanceSquared( c->burnAt[k], s->end ) < BURN_GAP * BURN_GAP ) {
			continue;
		}
		c->burnTime[k] = c->lastBurn = cg.time;
		c->nextBurnBolt = k + 1;
		VectorCopy( s->end, c->burnAt[k] );
		CG_BurnMark( s->end, s->normal, s->kind, flrand( 16.0f, 26.0f ) * ( c->twoHands ? 1.25f : 1.0f ) );
		return;
	}
}

// a new channel: where the bolts go, what they strike
static void Lightning_Shape( caster_t *c, centity_t *cent, const vec3_t hand, const vec3_t dir, int level ) {
	const int	skip = cent->currentState.number;
	trace_t		tr;
	vec3_t		end;
	int			i;

	c->numStrikes = 0;
	if ( level < FORCE_LEVEL_3 ) {
		VectorMA( hand, LINE_REACH, dir, end );
		CG_Trace( &tr, hand, NULL, NULL, end, skip, MASK_SHOT );
		Lightning_Strike( c, c->numStrikes++, &tr, skip );
		return;
	}
	// level 3: who is in the cone and can be seen
	for ( i = 0; i < cg.snap->numEntities && c->numStrikes < CONE_TARGETS; i++ ) {
		const entityState_t	*es = &cg.snap->entities[i];
		centity_t			*other = &cg_entities[es->number];
		vec3_t				to, way;

		if ( es->number == skip || ( es->eType != ET_PLAYER && es->eType != ET_NPC ) || ( es->eFlags & EF_DEAD ) ) {
			continue;
		}
		VectorCopy( other->lerpOrigin, to );
		to[2] += 8.0f;
		VectorSubtract( to, hand, way );
		if ( VectorNormalize( way ) > CONE_REACH + 16.0f || DotProduct( way, dir ) < CONE_DOT ) {
			continue;
		}
		CG_Trace( &tr, hand, NULL, NULL, to, skip, MASK_SHOT );
		if ( tr.fraction < 1.0f && tr.entityNum != es->number ) {
			continue;
		}
		VectorCopy( to, tr.endpos );
		tr.fraction = 0.5f;
		tr.entityNum = es->number;
		Lightning_Strike( c, c->numStrikes, &tr, skip );
		c->strikes[c->numStrikes++].target = es->number;
	}
	// and a few more that strike what is in the cone
	for ( i = 0; i < ( c->twoHands ? CONE_STRAYS_TWO : CONE_STRAYS ); i++ ) {
		vec3_t way, right, up;

		PerpendicularVector( right, dir );
		CrossProduct( dir, right, up );
		VectorMA( dir, flrand( -1.0f, 1.0f ) * ( c->twoHands ? 0.7f : 0.55f ), right, way );
		VectorMA( way, flrand( -1.0f, 1.0f ) * 0.4f, up, way );
		VectorNormalize( way );
		VectorMA( hand, CONE_REACH, way, end );
		CG_Trace( &tr, hand, NULL, NULL, end, skip, MASK_SHOT );
		Lightning_Strike( c, c->numStrikes++, &tr, skip );
	}
}

// the bolts follow what moves between two channels: the view of levels 1 and 2, the people they strike
static void Lightning_Follow( caster_t *c, centity_t *cent, const vec3_t hand, const vec3_t dir, int level ) {
	int k;

	if ( level < FORCE_LEVEL_3 && c->numStrikes ) {
		strike_t	*s = &c->strikes[0];
		trace_t		tr;
		vec3_t		end;

		VectorMA( hand, LINE_REACH, dir, end );
		CG_Trace( &tr, hand, NULL, NULL, end, cent->currentState.number, MASK_SHOT );
		VectorCopy( tr.endpos, s->end );
		VectorCopy( tr.plane.normal, s->normal );
		s->world = (qboolean)( tr.fraction < 1.0f && tr.entityNum == ENTITYNUM_WORLD && !( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT ) ) );
		return;
	}
	for ( k = 0; k < c->numStrikes; k++ ) {
		strike_t *s = &c->strikes[k];

		if ( s->target >= 0 && s->target < MAX_GENTITIES ) {
			VectorCopy( cg_entities[s->target].lerpOrigin, s->end );
			s->end[2] += 8.0f;
		}
	}
}

// a stroke runs down the channel: bright, then it dies down
static float Lightning_Level( const caster_t *c ) {
	const float since = (float)( cg.time - c->strokeTime );

	return ( STROKE_FLOOR + ( c->strokePeak - STROKE_FLOOR ) * expf( -since / STROKE_DECAY ) ) * flrand( 0.88f, 1.0f );
}

static void Lightning_Draw( caster_t *c, int n, float bright, qboolean casting );

qboolean CG_LightningCast( centity_t *cent, const vec3_t hand, const vec3_t otherHand, const vec3_t dir, int level ) {
	const int	n = cent->currentState.number;
	const int	numHands = otherHand ? 2 : 1;
	caster_t	*c;
	vec3_t		hands[2], from;
	float		bright;
	int			k;

	if ( !Lightning_On() || n < 0 || n >= MAX_GENTITIES || !cg.snap ) {
		return qfalse;
	}
	c = &casters[n];
	VectorCopy( hand, hands[0] );
	VectorCopy( otherHand ? otherHand : hand, hands[1] );
	// (the traces go from between the hands)
	VectorAdd( hands[0], hands[1], from );
	VectorScale( from, 0.5f, from );
	c->twoHands = (qboolean)( otherHand != NULL );

	if ( cg.time - c->channelTime >= c->channelLife || cg.time < c->channelTime ) {
		// a new channel; the one before stays faint a moment, if it was cast just now
		if ( c->numStrikes && cg.time - c->channelTime < CHANNEL_MAX * 2 && cg.time >= c->channelTime ) {
			c->numGhosts = c->numStrikes;
			c->ghostSeed = c->channelSeed;
			c->ghostTime = cg.time;
			for ( k = 0; k < c->numStrikes; k++ ) {
				c->ghosts[k] = c->strikes[k];
			}
		} else {
			c->numGhosts = 0;
		}
		Lightning_Shape( c, cent, from, dir, level );
		c->channelTime = c->branchTime = c->strokeTime = cg.time;
		c->channelLife = Q_irand( CHANNEL_MIN, CHANNEL_MAX );
		c->branchLife = Q_irand( BRANCH_MIN, BRANCH_MAX );
		c->channelSeed = rand() | 1;
		c->branchSeed = rand() | 1;
		c->strokePeak = 1.0f;
		for ( k = 0; k < c->numStrikes; k++ ) {
			Lightning_Sparks( &c->strikes[k], otherHand ? 1.5f : 1.0f );
		}
	} else {
		if ( cg.time - c->branchTime >= c->branchLife ) {
			// a stroke down the same channel, new branches
			c->branchTime = c->strokeTime = cg.time;
			c->branchLife = Q_irand( BRANCH_MIN, BRANCH_MAX );
			c->branchSeed = rand() | 1;
			c->strokePeak = flrand( 0.65f, 0.85f );
		}
		Lightning_Follow( c, cent, from, dir, level );
	}
	bright = Lightning_Level( c ) * ( otherHand ? TWO_HANDS_BRIGHT : 1.0f );
	c->lastCast = cg.time;
	c->numHands = numHands;
	VectorCopy( hands[0], c->hands[0] );
	VectorCopy( hands[1], c->hands[1] );
	VectorCopy( from, c->from );
	VectorCopy( dir, c->dir );
	c->lastBright = bright;
	Lightning_Draw( c, n, bright, qtrue );
	Lightning_Burn( c );
	return qtrue;
}

// his bolts, the flashes where they strike, bright 0 to about 1; casting: and the glow of his hands (while it fades
// his arm is on its way down already)
static void Lightning_Draw( caster_t *c, int n, float bright, qboolean casting ) {
	const float	width = c->numHands > 1 ? TWO_HANDS_WIDTH : 1.0f;
	int			k, j, h;

	for ( k = 0; k < c->numStrikes; k++ ) {
		const strike_t	*s = &c->strikes[k];
		const qboolean	strong = (qboolean)( k == 0 || s->target >= 0 );

		for ( h = 0; h < c->numHands; h++ ) {
			Bolt_Draw( c->hands[h], s->end, strong ? BOLT_MAIN : BOLT_STRAY, c->channelSeed + k * 7919 + h * 31337,
				c->branchSeed + k * 104729 + h * 7177, bright, width, s );
		}
		// (the main bolt and those that strike somebody give light: all of them together washed the picture out; one
		// light a bolt for two hands as well)
		if ( strong ) {
			Lightning_Light( c->from, s->end, 130.0f, ( c->numHands > 1 ? 0.6f : 0.5f ) * bright );
		}
		// a white flash where it strikes
		if ( s->world || s->target >= 0 ) {
			const vec3_t flash = { 0.75f, 0.82f, 1.0f };
			vec3_t at;

			VectorMA( s->end, SURFACE_GAP, s->normal, at );
			Lightning_Sprite( s->world ? at : s->end, ( ( strong ? 5.0f : 3.5f ) + 5.0f * bright ) * width, flash, 0.25f + 0.35f * bright );
		}
		for ( j = 0; j < c->numJumps[k]; j++ ) {
			Bolt_Draw( s->end, c->jumps[k][j], BOLT_JUMP, c->branchSeed + k * 31 + j, 0, 0.75f * bright, width, NULL );
			Lightning_Light( s->end, c->jumps[k][j], 60.0f, 0.3f * bright );
		}
	}
	// the channels before, faint, without their branches
	if ( c->numGhosts && cg.time - c->ghostTime < GHOST_MSEC ) {
		const float left = 1.0f - ( cg.time - c->ghostTime ) / (float)GHOST_MSEC;

		for ( k = 0; k < c->numGhosts; k++ ) {
			for ( h = 0; h < c->numHands; h++ ) {
				Bolt_Draw( c->hands[h], c->ghosts[k].end, k == 0 ? BOLT_MAIN : BOLT_STRAY, c->ghostSeed + k * 7919 + h * 31337, 0,
					0.3f * left, width, &c->ghosts[k] );
			}
		}
	}
	// the hands glow, small arcs crawl off the fingers
	for ( h = 0; casting && h < c->numHands; h++ ) {
		const vec3_t	glow = { 0.55f, 0.62f, 1.0f };
		const int		step = cg.time / SMALL_MSEC;
		vec3_t			right, up;

		Lightning_Sprite( c->hands[h], ( 3.5f + 2.5f * bright ) * width, glow, 0.3f + 0.3f * bright );
		Lightning_Basis( c->dir, right, up );
		for ( j = 0; j < 3; j++ ) {
			int		seed = step * 131 + n * 17 + j * 7 + h * 53 + 1;
			vec3_t	way, to;

			VectorScale( c->dir, 0.5f, way );
			VectorMA( way, Q_crandom( &seed ), right, way );
			VectorMA( way, Q_crandom( &seed ), up, way );
			VectorNormalize( way );
			VectorMA( c->hands[h], 5.0f + 7.0f * Q_random( &seed ), way, to );
			Bolt_Draw( c->hands[h], to, BOLT_SMALL, seed, 0, 0.8f * bright, 1.0f, NULL );
		}
	}
	CG_RibbonFlush();
}

static void Lightning_Bursts( void );

void CG_LightningFrame( void ) {
	int i;

	Lightning_Bursts();
	if ( !Lightning_On() ) {
		return;
	}
	// a cast that ended: its last bolts stay where they were and fade out, flickering (no new shape, sparks or marks)
	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		caster_t	*c = &casters[i];
		const int	since = cg.time - c->lastCast;
		int			k;
		float		left;

		if ( !c->lastCast || since <= 0 || since >= FADE_MSEC ) {
			continue;
		}
		left = 1.0f - since / (float)FADE_MSEC;
		for ( k = 0; k < c->numStrikes; k++ ) {
			strike_t *s = &c->strikes[k];

			if ( s->target >= 0 && s->target < MAX_GENTITIES ) {
				VectorCopy( cg_entities[s->target].lerpOrigin, s->end );
				s->end[2] += 8.0f;
			}
		}
		Lightning_Draw( c, i, c->lastBright * left * left * flrand( 0.7f, 1.0f ), qfalse );
	}
	// small arcs crawl on the metal that was struck, weaker as they die out, a new shape now and then
	for ( i = 0; i < STAY_SPOTS; i++ ) {
		staySpot_t	*s = &stays[i];
		vec3_t		right, up, a, b;
		float		left;
		int			seed, j;

		if ( s->until <= cg.time ) {
			continue;
		}
		left = ( s->until - cg.time ) / (float)STAY_MSEC;
		seed = s->seed + ( cg.time / STAY_STEP_MSEC ) * 977;
		if ( Q_random( &seed ) > 0.35f + 0.6f * left ) {
			continue;
		}
		PerpendicularVector( right, s->normal );
		CrossProduct( s->normal, right, up );
		VectorMA( s->origin, 1.5f, s->normal, a );
		VectorCopy( a, b );
		for ( j = 0; j < 2; j++ ) {
			VectorMA( a, Q_crandom( &seed ) * STAY_SIZE, j ? up : right, a );
			VectorMA( b, Q_crandom( &seed ) * STAY_SIZE, j ? up : right, b );
		}
		Bolt_Draw( a, b, BOLT_JUMP, seed, 0, 0.4f + 0.5f * left, 1.0f, NULL );
		trap->R_AddLightToScene( s->origin, 40.0f + 40.0f * left, 0.3f * left, 0.4f * left, 0.8f * left );
	}
	CG_RibbonFlush();
	// water that lit up
	for ( i = 0; i < (int)ARRAY_LEN( flashes ); i++ ) {
		const waterFlash_t *f = &flashes[i];

		if ( f->until > cg.time ) {
			const float left = ( f->until - cg.time ) / (float)WATER_MSEC;

			trap->R_AddLightToScene( f->origin, 160.0f + 120.0f * left, 0.35f * left, 0.6f * left, 1.0f * left );
		}
	}
	CG_BurnMarksAdd();
}

/*
===============================================================================

The same bolts elsewhere

===============================================================================
*/

static qboolean Lightning_Ready( void ) {
	return (qboolean)( layerShaders[LAYER_CORE] && layerShaders[LAYER_GLOW] );
}

void CG_LightningStrand( const ribbon_t *r, float level, float width ) {
	if ( Lightning_Ready() ) {
		Strand_Draw( r, level, width );
	}
}

// a body point of a bolt of the model, qfalse: it has none
static qboolean Shock_Bolt( centity_t *cent, int bolt, vec3_t out ) {
	mdxaBone_t m;

	if ( bolt < 0 || !cent->ghoul2 ) {
		return qfalse;
	}
	trap->G2API_GetBoltMatrix( cent->ghoul2, 0, bolt, &m, cent->turAngles, cent->lerpOrigin, cg.time, cgs.gameModels, cent->modelScale );
	VectorSet( out, m.matrix[0][3], m.matrix[1][3], m.matrix[2][3] );
	return qtrue;
}

void CG_LightningShocked( centity_t *cent, clientInfo_t *ci, float strength ) {
	vec3_t	points[7], right, fwd;
	int		n = 0, seed, a, i, j;

	if ( !Lightning_Ready() || ( !v_fxForce.integer && !v_weaponImpacts.integer ) || !ci || strength <= 0.0f ) {
		return;
	}
	n += Shock_Bolt( cent, ci->bolt_head, points[n] );
	n += Shock_Bolt( cent, ci->bolt_lhand, points[n] );
	n += Shock_Bolt( cent, ci->bolt_rhand, points[n] );
	n += Shock_Bolt( cent, ci->bolt_llumbar, points[n] );
	n += Shock_Bolt( cent, ci->bolt_motion, points[n] );
	// (the feet have no bolt of their own: about where they stand)
	AngleVectors( cent->lerpAngles, fwd, right, NULL );
	for ( i = 0; i < 2; i++ ) {
		VectorMA( cent->lerpOrigin, i ? -6.0f : 6.0f, right, points[n] );
		points[n][2] -= 20.0f;
		n++;
	}
	seed = cent->currentState.number * 1013 + ( cg.time / SHOCK_STEP_MSEC ) * 7919;
	for ( a = 0; a < SHOCK_ARCS; a++ ) {
		if ( Q_random( &seed ) < 0.3f ) {
			continue;
		}
		i = (int)( Q_random( &seed ) * n ) % n;
		j = ( i + 1 + (int)( Q_random( &seed ) * ( n - 1 ) ) ) % n;
		Bolt_Draw( points[i], points[j], BOLT_JUMP, seed + a, 0, ( 0.5f + 0.5f * Q_random( &seed ) ) * strength, 0.8f, NULL );
	}
	// now and then one to the floor
	if ( Q_random( &seed ) < 0.35f ) {
		trace_t	tr;
		vec3_t	from, to;

		VectorCopy( points[n - 1 - (int)( Q_random( &seed ) * 2.0f )], from );
		VectorCopy( cent->lerpOrigin, to );
		to[0] += Q_crandom( &seed ) * 36.0f;
		to[1] += Q_crandom( &seed ) * 36.0f;
		to[2] -= 64.0f;
		CG_Trace( &tr, from, NULL, NULL, to, cent->currentState.number, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			Bolt_Draw( from, tr.endpos, BOLT_JUMP, seed, 0, 0.7f * strength, 0.8f, NULL );
		}
	}
	{
		vec3_t center;

		VectorCopy( cent->lerpOrigin, center );
		center[2] += 8.0f;
		trap->R_AddLightToScene( center, 70.0f + 30.0f * Q_random( &seed ), 0.25f * strength, 0.35f * strength, 0.8f * strength );
	}
	CG_RibbonFlush();
}

void CG_LightningOrb( const vec3_t origin, int seed0, float radius ) {
	int seed = seed0 * 977 + ( cg.time / ORB_STEP_MSEC ) * 131;
	int a;

	if ( !Lightning_Ready() || !v_weaponImpacts.integer ) {
		return;
	}
	for ( a = 0; a < 3; a++ ) {
		vec3_t dir, from, to;
		int i;

		for ( i = 0; i < 3; i++ ) {
			dir[i] = Q_crandom( &seed );
		}
		if ( VectorNormalize( dir ) < 0.01f ) {
			continue;
		}
		// (from the edge of the glow of the shot out, else they are lost in it)
		VectorMA( origin, radius * 0.35f, dir, from );
		VectorMA( origin, radius * ( 0.7f + 0.3f * Q_random( &seed ) ), dir, to );
		Bolt_Draw( from, to, BOLT_JUMP, seed, 0, 1.0f, 1.0f, NULL );
	}
	CG_RibbonFlush();
}

void CG_LightningBurst( const vec3_t origin, float radius ) {
	int i, oldest = 0;

	if ( !Lightning_Ready() || !v_weaponImpacts.integer || radius <= 0.0f ) {
		return;
	}
	for ( i = 0; i < BURSTS; i++ ) {
		if ( !bursts[i].start ) {
			oldest = i;
			break;
		}
		if ( bursts[i].start < bursts[oldest].start ) {
			oldest = i;
		}
	}
	VectorCopy( origin, bursts[oldest].origin );
	bursts[oldest].radius = radius;
	bursts[oldest].start = cg.time ? cg.time : 1;
	bursts[oldest].seed = rand();
}

// the blasts: bolts from the middle out as far as the shell has grown, to the walls in its reach
static void Lightning_Bursts( void ) {
	int i, a;

	if ( !Lightning_Ready() ) {
		return;
	}
	for ( i = 0; i < BURSTS; i++ ) {
		burst_t		*b = &bursts[i];
		const int	age = cg.time - b->start;
		float		t, reach, level;
		int			seed;

		if ( !b->start ) {
			continue;
		}
		if ( age < 0 || age >= BURST_MSEC ) {
			b->start = 0;
			continue;
		}
		t = age / (float)BURST_MSEC;
		reach = b->radius * ( 0.3f + 0.7f * sqrtf( t ) );
		level = ( 1.0f - t ) * flrand( 0.75f, 1.0f );
		for ( a = 0; a < BURST_ARCS; a++ ) {
			strike_t	s;
			trace_t		tr;
			vec3_t		dir, end;
			int			k;

			// (each arc keeps its way through the blast, its shape changes)
			seed = b->seed + a * 7919;
			for ( k = 0; k < 3; k++ ) {
				dir[k] = Q_crandom( &seed );
			}
			if ( VectorNormalize( dir ) < 0.01f ) {
				continue;
			}
			VectorMA( b->origin, reach, dir, end );
			CG_Trace( &tr, b->origin, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID );
			memset( &s, 0, sizeof( s ) );
			VectorCopy( tr.endpos, s.end );
			VectorCopy( tr.plane.normal, s.normal );
			s.target = -1;
			s.world = (qboolean)( tr.fraction < 1.0f );
			seed = b->seed + a * 31 + ( cg.time / SHOCK_STEP_MSEC ) * 977;
			Bolt_Draw( b->origin, s.end, BOLT_STRAY, seed | 1, ( seed * 3 ) | 1, level, 0.8f, &s );
		}
		trap->R_AddLightToScene( b->origin, reach * 1.5f, 0.3f * level, 0.45f * level, 1.0f * level );
	}
	CG_RibbonFlush();
}
