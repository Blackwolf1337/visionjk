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

// cg_screenfx.c -- the screen effects of rd-vulkan-vision. Not from JA+.
//
// The renderer bends and colours the view of the level as this file asks once a frame (trap->ext.R_SetScreenEffect,
// screenEffect_t). What happens in the level adds sources while the frame is built, CG_ScreenFxScene() sends the
// strongest of them before the scene is rendered. Only the look changes, so it works on every server. The renderer
// leaves a source out where the level is nearer than it: nothing is seen through a wall. rd-vanilla: none of it.
//
// v_screenFx 0 switches all of it off, and each look has a cvar of its own:
// - v_fxBlast: an explosion sends a ring through the air that bends the picture as it runs outwards. Close by the
//   colours part for a moment.
// - v_fxHeat: the air shimmers over the flames of a jetpack and of the flamethrower, and over the barrel of the own
//   repeater or blaster after a long burst, which smokes too when it gets very hot. The hot air is left in the
//   level as puffs that rise and fade, so it trails behind whatever moves. The smoke is no sprite but a puff the
//   renderer draws from noise that folds into itself, so it curls as it rises.

#include "cgame/cg_local.h"

qboolean CG_CalcMuzzlePoint( int entityNum, vec3_t muzzle );		// cg_weapons.c

#define FX_BLASTS			8
#define BLAST_MSEC			600			// the ring runs out in that time
#define BLAST_REACH			1.25f		// as far as this times the splash damage
#define SPLIT_MSEC			350			// and the colours come together again
#define BARREL_SHOT			0.07f		// heat of the barrel a shot adds, 0 to 1
#define BARREL_COOL			0.35f		// and loses a second
#define BARREL_HOT			0.3f		// from here on the air over it shimmers
#define BARREL_SMOKE		0.65f		// and it smokes
#define FX_EMITTERS			32
#define FX_PUFFS			128
#define PUFF_MSEC			70			// an emitter leaves a puff of hot air this often
#define PUFF_LIFE			900			// which takes this long to fade
#define PUFF_RISE			40.0f		// and rises faster and faster, units a second squared
#define DEAF_MSEC			3500		// the sound stays dull and the ears ring after an explosion close by
#define HIT_MSEC			500			// the rim towards a hit fades in that time
#define HIT_SPLIT_MSEC		200
#define LOW_HEALTH			30			// below that the colours drain and the heart beats
#define WATER_MSEC			250			// into the water and out of it
#define DROPS_MSEC			3500		// drops run down the view after surfacing
#define FX_DROPS			24			// drops a water gun left on the view (SCREENFX_DROP)
#define DROP_SLIDE			0.013f		// a drop larger than this (of the height of the view) runs down
#define DROP_DRY_MSEC		1500		// the last of its life it dries
#define FX_SNOWS			8			// clumps of snow a snowball left on the view (SCREENFX_SNOW)
#define FX_SMOKES			128
#define PROTECT_HIT_MSEC	600
#define ABSORB_PULSE_MSEC	500
#define FORCE_MSEC			300			// the looks of the Force powers come and go in that time
#define FX_RIPPLES			8
#define RIPPLE_MSEC			450			// the waves of a push or pull run out in that time
#define RIPPLE_REACH		240.0f		// units the front of a push runs out
#define FX_FLASHES			8
#define FLASH_MSEC			260
#define SMOKE_MSEC			90			// an emitter that smokes leaves a puff this often
#define SMOKE_LIFE			1600
#define SMOKE_RISE			28.0f		// units a second
#define NEAR_FADE			48.0f		// smoke closer to the eye than this is not drawn, and fades in
#define NEAR_FULL			160.0f		// up to this distance: the smoke one runs into does not fill the view
#define HEAT_NEAR_FADE		20.0f		// the same for hot air, which HEAT_MAX_SIZE keeps from filling the view
#define HEAT_NEAR_FULL		70.0f		// anyway: a blade or the own jetpack in third person stay whole
#define HEAT_MAX_SIZE		0.08f		// of the height of the view, however close the hot air is
#define SMOKE_MAX_SIZE		0.3f

typedef struct {
	int			birth;					// 0: none
	vec3_t		origin;
	float		radius;					// units the ring runs out to
} fxBlast_t;

typedef struct {
	int			id;						// see CG_ScreenFxEmit()
	int			seenTime;				// the frame it was there last, 0: free
	vec3_t		origin;
	float		radius, strength;
	int			nextPuff, nextSmoke;
} fxEmitter_t;

typedef struct {
	int			id;						// of its emitter
	int			birth;					// 0: none
	vec3_t		origin, velocity;
	float		radius, strength;
} fxPuff_t;

typedef struct {
	int			birth;					// 0: none
	vec3_t		origin, velocity;
	float		radius, density;
	float		seed;
} fxSmoke_t;

typedef struct {
	int			birth;					// 0: none
	vec3_t		origin;
	vec3_t		dir;					// the way the hands push, forward
	qboolean	pull;
} fxRipple_t;

typedef struct {
	int			birth;					// 0: none
	vec3_t		origin;
	float		size;					// units
	float		strength;
} fxFlash_t;

typedef struct {
	qboolean	active;
	int			born, life;		// cg.time it landed, msec it lasts
	float		x, y;			// fractions of the view from its upper left corner
	float		radius;			// a fraction of the height of the view
	float		speed;			// down, heights of the view a second
	float		trail;			// how far above it its trail reaches
	float		seed;
	int			stopUntil;		// a running drop that sticks for a moment, until then
} fxDrop_t;

typedef struct {
	qboolean	active;
	qboolean	dripped;		// it melted into drops
	int			born, life;		// cg.time it landed, msec it takes to melt
	float		x, y, radius;	// as a drop
	float		speed;			// it slides down slowly, heavy and wet
	float		seed;
} fxSnow_t;

static struct {
	qboolean			has;			// the engine and the renderer have R_SetScreenEffect
	qboolean			hasDrops;		//	and draw SCREENFX_DROP
	qboolean			hasSnow;		//	and SCREENFX_SNOW
	fxDrop_t			drops[FX_DROPS];
	fxSnow_t			snows[FX_SNOWS];
	fxBlast_t			blasts[FX_BLASTS];
	int					nextBlast;
	float				split;			// the colours part, 0 to 1, from splitTime on
	int					splitTime;
	float				barrel;			// heat of the barrel of the own weapon, 0 to 1, at barrelTime
	int					barrelTime;
	fxEmitter_t			emitters[FX_EMITTERS];
	fxPuff_t			puffs[FX_PUFFS];	// a ring
	int					nextPuff;
	fxSmoke_t			smokes[FX_SMOKES];	// a ring too
	int					nextSmoke;
	screenFxSource_t	sources[SCREENFX_MAX_SOURCES * 2];	// what this frame added so far
	int					numSources;
	float				deaf;			// how dull an explosion close by left the sound, 0 to 1, at deafTime
	int					deafTime;
	sfxHandle_t			beatSound;
	int					beatTime, nextBeat;	// the heart
	float				water;			// how far the view is under water, 0 to 1
	int					waterContents;	// CONTENTS_WATER, _SLIME or _LAVA, the last it was in
	int					surfaceTime;	// when it came up out of it
	float				muffle;			// what v_muffle was set to
	float				tinnitus;		// and v_tinnitus
	float				dream;			// and v_dreamReverb
	float				rage, rageRecovery, absorb, protect, speed, sight;	// the Force looks, 0 to 1
	fxRipple_t			ripples[FX_RIPPLES];
	int					nextRipple;
	int					pushAnim[MAX_GENTITIES];	// the torso animation of that player the frame before
	qboolean			pushFlip[MAX_GENTITIES];	// and its torsoFlip
	qboolean			pushArmed[MAX_GENTITIES];	// a push or pull animation started that has not sent its waves
	fxFlash_t			flashes[FX_FLASHES];
	int					nextFlash;
	sfxHandle_t			rageSound;
	int					protectHitTime;	// a hit while protect was on
	int					absorbTime;		// the Force power went up while absorb was on
	int					lastForcePower;
	qhandle_t			ghostShader;
	vec3_t				lastAngles;		// of the view, the frame before
} sfx;

static qboolean ScreenFx_Engine( void ) {
	char value[8];

	trap->Cvar_VariableStringBuffer( "v_cgameScreenFx", value, sizeof( value ) );
	return (qboolean)( atoi( value ) == 1 );
}

void CG_ScreenFxInit( void ) {
	memset( &sfx, 0, sizeof( sfx ) );
	// an engine without the cvar has no such entry in trap->ext, it may not be read
	sfx.has = (qboolean)( ScreenFx_Engine() && trap->ext.R_SetScreenEffect && trap->ext.R_SetScreenEffect( NULL ) );
	{
		char value[8];

		trap->Cvar_VariableStringBuffer( "v_screenFxDrops", value, sizeof( value ) );
		sfx.hasDrops = (qboolean)( sfx.has && atoi( value ) == 1 );
		trap->Cvar_VariableStringBuffer( "v_screenFxSnow", value, sizeof( value ) );
		sfx.hasSnow = (qboolean)( sfx.hasDrops && atoi( value ) == 1 );
	}
	sfx.beatSound = trap->S_RegisterSound( "sound/vision/heartbeat.wav" );
	sfx.rageSound = trap->S_RegisterSound( "sound/vision/rage_hum.wav" );
	sfx.ghostShader = trap->R_RegisterShader( "gfx/vision/ghost" );
	// What the cgame before this one set stays in the cvar when it ends (a level change while a menu muffled the
	// sound), and this one would take it for 0: the sound stayed dull until a menu was opened and closed.
	trap->Cvar_Set( "v_muffle", "0" );
	trap->Cvar_Set( "v_tinnitus", "0" );
	trap->Cvar_Set( "v_dreamReverb", "0" );
}

static qboolean CG_ScreenFxOn( void ) {
	return (qboolean)( sfx.has && v_screenFx.integer );
}

// what the renderer draws in the place of the looks of the game: the blot of a hit (CG_DamageBlendBlob), the tint
// under water (CG_Draw2DScreenTints)
qboolean CG_ScreenFxHitDrawn( void ) {
	return (qboolean)( CG_ScreenFxOn() && v_fxHit.integer );
}

qboolean CG_ScreenFxWaterDrawn( void ) {
	return (qboolean)( CG_ScreenFxOn() && v_fxWater.integer );
}

// and the tints of rage, absorb and protect (CG_Draw2DScreenTints)
qboolean CG_ScreenFxForceDrawn( void ) {
	return (qboolean)( CG_ScreenFxOn() && v_fxForce.integer );
}

// the own player, not one who is followed or a spectator
static qboolean CG_ScreenFxOwnPlayer( void ) {
	return (qboolean)( cg.snap && !( cg.snap->ps.pm_flags & PMF_FOLLOW ) && cg.snap->ps.pm_type != PM_SPECTATOR
		&& cg.predictedPlayerState.stats[STAT_HEALTH] > 0 );
}

/*
=================
CG_ScreenFxProject

A point of the level to fractions of the view from its upper left corner, and its distance from the eye along the
view (what the depth of the renderer holds). qfalse: behind the eye.
=================
*/
static qboolean CG_ScreenFxProject( const vec3_t point, float *frac, float *depth ) {
	const float	px = tanf( DEG2RAD( cg.refdef.fov_x * 0.5f ) );
	const float	py = tanf( DEG2RAD( cg.refdef.fov_y * 0.5f ) );
	vec3_t		delta;
	float		z;

	VectorSubtract( point, cg.refdef.vieworg, delta );
	z = DotProduct( delta, cg.refdef.viewaxis[0] );
	if ( z < 1.0f ) {
		return qfalse;
	}
	frac[0] = 0.5f - 0.5f * DotProduct( delta, cg.refdef.viewaxis[1] ) / ( z * px );
	frac[1] = 0.5f - 0.5f * DotProduct( delta, cg.refdef.viewaxis[2] ) / ( z * py );
	*depth = z;
	return qtrue;
}

// what is right in front of the eye fades out, from fade on to full: a trail one walks into, the own barrel in first
// person
static float CG_ScreenFxNear( float depth, float fade, float full ) {
	const float t = Com_Clamp( 0.0f, 1.0f, ( depth - fade ) / ( full - fade ) );

	return t * t * ( 3.0f - 2.0f * t );
}

// units at that distance to a fraction of the height of the view
static float CG_ScreenFxSize( float units, float depth ) {
	return units / ( 2.0f * depth * tanf( DEG2RAD( cg.refdef.fov_y * 0.5f ) ) );
}

static screenFxSource_t *CG_ScreenFxSource( void ) {
	if ( sfx.numSources >= (int)ARRAY_LEN( sfx.sources ) ) {
		return NULL;
	}
	return memset( &sfx.sources[sfx.numSources++], 0, sizeof( screenFxSource_t ) );
}

/*
=================
CG_ScreenFxBlast

An explosion at origin, whose wave reaches radius units
=================
*/
void CG_ScreenFxBlast( const vec3_t origin, float radius ) {
	fxBlast_t	*blast;
	float		near;

	// close by the ears ring and the sound is dull for a while (CG_ScreenFxMuffle): a sound of its own
	// (v_soundTinnitus), with any renderer and without the looks
	near = 1.0f - Distance( origin, cg.refdef.vieworg ) / ( radius * 2.0f );
	if ( v_soundTinnitus.integer && near > 0.25f && CG_ScreenFxOwnPlayer() ) {
		const float	left = sfx.deaf * ( 1.0f - ( cg.time - sfx.deafTime ) / (float)DEAF_MSEC );

		sfx.deaf = Q_max( near, left );
		sfx.deafTime = cg.time;
	}
	if ( !v_screenFx.integer || !v_fxBlast.integer || !sfx.has ) {
		return;
	}
	blast = &sfx.blasts[sfx.nextBlast];
	sfx.nextBlast = ( sfx.nextBlast + 1 ) % FX_BLASTS;
	blast->birth = cg.time;
	VectorCopy( origin, blast->origin );
	blast->radius = radius;

	// close by the colours part, the closer the more
	near = 1.0f - Distance( origin, cg.refdef.vieworg ) / ( radius * 2.5f );
	if ( near > 0.0f ) {
		const float	left = sfx.split * ( 1.0f - ( cg.time - sfx.splitTime ) / (float)SPLIT_MSEC );

		sfx.split = Q_max( near, left );
		sfx.splitTime = cg.time;
	}
}

// the explosions of the missiles, about as far as their splash damage reaches. The bombs of the flechette, the det
// pack and the trip mine come as an EV_PLAY_EFFECT.
void CG_ScreenFxWeaponBlast( int weapon, qboolean altFire, const vec3_t origin ) {
	float radius;

	switch ( weapon ) {
	case WP_ROCKET_LAUNCHER:	radius = 160.0f; break;
	case WP_THERMAL:			radius = 256.0f; break;
	case WP_CONCUSSION:			radius = altFire ? 0.0f : 200.0f; break;
	case WP_REPEATER:			radius = altFire ? 128.0f : 0.0f; break;
	default:					radius = 0.0f; break;
	}
	if ( radius > 0.0f ) {
		CG_ScreenFxBlast( origin, radius );
	}
}

/*
=================
CG_ScreenFxHeatLine

Hot air along the line from a to b, radius units around it (the larger of the two ends), strength 0 to 1, for this
frame. inner: units around the line that stay still, the core of a blade
=================
*/
static void CG_ScreenFxHeatLine( const vec3_t a, const vec3_t b, float radius, float inner, float strength ) {
	screenFxSource_t	*src;
	float				depthA, depthB, depth;
	float				fracA[2], fracB[2];

	if ( strength <= 0.01f ) {
		return;
	}
	if ( !CG_ScreenFxProject( a, fracA, &depthA ) || !CG_ScreenFxProject( b, fracB, &depthB ) ) {
		return;
	}
	depth = Q_min( depthA, depthB );
	strength *= CG_ScreenFxNear( depth, HEAT_NEAR_FADE, HEAT_NEAR_FULL );
	if ( strength <= 0.01f ) {
		return;
	}
	src = CG_ScreenFxSource();
	if ( !src ) {
		return;
	}
	src->kind = SCREENFX_HEAT;
	VectorCopy2( fracA, src->a );
	VectorCopy2( fracB, src->b );
	src->radius = CG_ScreenFxSize( radius, depth );
	src->width = CG_ScreenFxSize( inner, depth );
	if ( src->radius > HEAT_MAX_SIZE ) {
		src->width *= HEAT_MAX_SIZE / src->radius;
		src->radius = HEAT_MAX_SIZE;
	}
	src->strength = Com_Clamp( 0.0f, 1.0f, strength );
	src->distance = depth;
}

static fxEmitter_t *CG_ScreenFxEmitter( int id ) {
	fxEmitter_t	*free = NULL;
	int			i;

	for ( i = 0; i < FX_EMITTERS; i++ ) {
		fxEmitter_t *e = &sfx.emitters[i];

		if ( e->id == id && e->seenTime ) {
			return e;
		}
		// one whose trail has blown away
		if ( !free && ( !e->seenTime || cg.time - e->seenTime > PUFF_LIFE || e->seenTime > cg.time ) ) {
			free = e;
		}
	}
	if ( free ) {
		memset( free, 0, sizeof( *free ) );
		free->id = id;
	}
	return free;
}

/*
=================
CG_ScreenFxEmit

Something hot at origin this frame, blowing its gas towards dir (a length, units a second): flames, a hot barrel.
id tells one from the others (entity number * 4 + a slot). Every so often it leaves a puff of hot air in the level
that rises, widens and fades, so the shimmer trails behind what moves. smoke: and puffs of smoke that thick, 0 to 1,
drawn by the renderer as well (CG_ScreenFxSmokes).
=================
*/
void CG_ScreenFxEmit( int id, const vec3_t origin, const vec3_t dir, float radius, float strength, float smoke ) {
	fxEmitter_t *e;

	if ( !CG_ScreenFxOn() || !v_fxHeat.integer || strength <= 0.0f ) {
		return;
	}
	e = CG_ScreenFxEmitter( id );
	if ( !e ) {
		return;
	}
	e->seenTime = cg.time;
	VectorCopy( origin, e->origin );
	e->radius = radius;
	e->strength = strength;
	if ( e->nextPuff <= cg.time || e->nextPuff - cg.time > PUFF_MSEC ) {
		fxPuff_t *puff = &sfx.puffs[sfx.nextPuff];

		sfx.nextPuff = ( sfx.nextPuff + 1 ) % FX_PUFFS;
		puff->id = id;
		puff->birth = cg.time;
		VectorCopy( origin, puff->origin );
		VectorCopy( dir, puff->velocity );
		puff->radius = radius;
		puff->strength = strength;
		e->nextPuff = cg.time + PUFF_MSEC;
	}
	if ( smoke > 0.0f && ( e->nextSmoke <= cg.time || e->nextSmoke - cg.time > SMOKE_MSEC ) ) {
		fxSmoke_t *puff = &sfx.smokes[sfx.nextSmoke];

		sfx.nextSmoke = ( sfx.nextSmoke + 1 ) % FX_SMOKES;
		puff->birth = cg.time;
		VectorCopy( origin, puff->origin );
		// slower than the gas that carries it, a little to one side
		VectorScale( dir, 0.4f, puff->velocity );
		puff->velocity[0] += flrand( -8.0f, 8.0f );
		puff->velocity[1] += flrand( -8.0f, 8.0f );
		puff->radius = radius * 1.5f;
		puff->density = Com_Clamp( 0.0f, 1.0f, smoke );
		puff->seed = flrand( 0.0f, 16.0f );
		e->nextSmoke = cg.time + SMOKE_MSEC;
	}
}

/*
=================
CG_ScreenFxRefract

Clear water along the line from a to b, radius units around it, for this frame: it bends what is behind it the
way hot air does (the jet of the water gun, toys/cg_water.c). With v_fxWater.
=================
*/
void CG_ScreenFxRefract( const vec3_t a, const vec3_t b, float radius, float strength ) {
	if ( CG_ScreenFxOn() && v_fxWater.integer ) {
		CG_ScreenFxHeatLine( a, b, radius, 0.0f, strength );
	}
}

// a drop of water on the view at x, y (fractions of the view) of radius (of its height), for life msec
static void ScreenFx_AddDrop( float x, float y, float radius, int life ) {
	int i;

	for ( i = 0; i < FX_DROPS; i++ ) {
		fxDrop_t *d = &sfx.drops[i];

		if ( d->active ) {
			continue;
		}
		memset( d, 0, sizeof( *d ) );
		d->active = qtrue;
		d->born = cg.time;
		d->life = life;
		d->x = x;
		d->y = y;
		d->radius = radius;
		d->seed = Q_flrand( 0.0f, 1.0f );
		return;
	}
}

/*
=================
CG_ScreenFxWet

The own face got wet (the water gun of somebody, toys/cg_water.c), the water flying with velocity: drops land on
the view where it came from (water from the right on the right side, from above on the upper part), a few large
ones in the middle of the splash and smaller ones around them. The large ones run down, stopping now and then, and
leave a trail; all of them dry after some seconds (CG_ScreenFxDrops). A renderer without SCREENFX_DROP shows the
drops of coming up out of the water instead.
=================
*/
void CG_ScreenFxWet( const vec3_t velocity ) {
	vec3_t	dir;
	float	cx, cy;
	int		i, count;

	if ( cg.refdef.viewContents & ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA ) ) {
		return;
	}
	if ( !sfx.hasDrops ) {
		sfx.surfaceTime = cg.time ? cg.time : 1;
		sfx.waterContents = CONTENTS_WATER;
		return;
	}
	VectorCopy( velocity, dir );
	VectorNormalize( dir );
	cx = Com_Clamp( 0.15f, 0.85f, 0.5f - 0.3f * DotProduct( dir, cg.refdef.viewaxis[1] ) + Q_flrand( -0.08f, 0.08f ) );
	cy = Com_Clamp( 0.15f, 0.8f, 0.45f + 0.3f * DotProduct( dir, cg.refdef.viewaxis[2] ) + Q_flrand( -0.08f, 0.08f ) );
	// a few large ones, and around them the fine spray (the last ones the largest and in the middle of the splash)
	count = 12 + rand() % 5;
	for ( i = count - 1; i >= 0; i-- ) {
		const float spread = i > 4 ? 0.2f : 0.09f;

		ScreenFx_AddDrop( cx + Q_flrand( -1.0f, 1.0f ) * spread * 0.7f, cy + Q_flrand( -1.0f, 1.0f ) * spread,
			i > 4 ? Q_flrand( 0.003f, 0.008f ) : Q_flrand( 0.016f, 0.04f ), (int)Q_flrand( 3000.0f, 6000.0f ) );
	}
}

/*
=================
CG_ScreenFxSnow

A snowball hit the own face (toys/cg_toys.c), flying with velocity: clumps of snow stick on the view where it came
from, one to three large ones and some small bits; they slide down a little, melt from the edges in and turn into
drops of water that run down and dry (CG_ScreenFxSnows). qfalse: the renderer has no snow on the view, the cgame
draws its picture instead.
=================
*/
qboolean CG_ScreenFxSnow( const vec3_t velocity ) {
	vec3_t	dir;
	float	cx, cy;
	int		i, count;

	if ( !sfx.hasSnow || ( cg.refdef.viewContents & ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA ) ) ) {
		return qfalse;
	}
	VectorCopy( velocity, dir );
	VectorNormalize( dir );
	cx = Com_Clamp( 0.2f, 0.8f, 0.5f - 0.3f * DotProduct( dir, cg.refdef.viewaxis[1] ) + Q_flrand( -0.1f, 0.1f ) );
	cy = Com_Clamp( 0.2f, 0.75f, 0.45f + 0.3f * DotProduct( dir, cg.refdef.viewaxis[2] ) + Q_flrand( -0.1f, 0.1f ) );
	count = 4 + rand() % 3;
	for ( i = 0; i < count; i++ ) {
		fxSnow_t	*s = &sfx.snows[0];
		const int	large = i < 1 + rand() % 3;
		int			k;

		// a free one, else the oldest
		for ( k = 0; k < FX_SNOWS; k++ ) {
			if ( !sfx.snows[k].active ) {
				s = &sfx.snows[k];
				break;
			}
			if ( sfx.snows[k].born < s->born ) {
				s = &sfx.snows[k];
			}
		}
		memset( s, 0, sizeof( *s ) );
		s->active = qtrue;
		s->born = cg.time;
		s->life = (int)Q_flrand( 3000.0f, 4500.0f );
		s->x = cx + Q_flrand( -1.0f, 1.0f ) * ( large ? 0.06f : 0.16f );
		s->y = cy + Q_flrand( -1.0f, 1.0f ) * ( large ? 0.06f : 0.14f );
		s->radius = large ? Q_flrand( 0.07f, 0.12f ) : Q_flrand( 0.015f, 0.035f );
		s->seed = Q_flrand( 0.0f, 1.0f );
	}
	return qtrue;
}

// The clumps of snow on the view: they slide down slowly, melt, and when little is left turn into drops where they are
static void CG_ScreenFxSnows( void ) {
	const float	dt = Com_Clamp( 0.0f, 0.1f, cg.frametime * 0.001f );
	int			i;

	for ( i = 0; i < FX_SNOWS; i++ ) {
		fxSnow_t			*s = &sfx.snows[i];
		screenFxSource_t	*src;
		const int			age = cg.time - s->born;
		float				left;

		if ( !s->active ) {
			continue;
		}
		if ( age >= s->life || age < 0 || ( cg.refdef.viewContents & ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA ) ) ) {
			s->active = qfalse;
			continue;
		}
		left = 1.0f - (float)age / s->life;
		// (it holds a moment, then the weight of the wet snow pulls it down)
		if ( age > 600 ) {
			s->speed = Q_min( s->radius * 0.5f, s->speed + s->radius * 0.6f * dt );
			s->y += s->speed * dt;
		}
		if ( !s->dripped && left < 0.35f ) {
			const int drops = s->radius > 0.05f ? 3 + rand() % 3 : 1;
			int k;

			s->dripped = qtrue;
			for ( k = 0; k < drops; k++ ) {
				ScreenFx_AddDrop( s->x + Q_flrand( -0.6f, 0.6f ) * s->radius * cgs.glconfig.vidHeight / Q_max( cgs.glconfig.vidWidth, 1 ),
					s->y + Q_flrand( -0.4f, 0.6f ) * s->radius, s->radius * ( k ? Q_flrand( 0.12f, 0.3f ) : Q_flrand( 0.3f, 0.45f ) ),
					(int)Q_flrand( 2500.0f, 5000.0f ) );
			}
		}
		src = CG_ScreenFxSource();
		if ( !src ) {
			break;
		}
		src->kind = SCREENFX_SNOW;
		src->a[0] = src->b[0] = s->x;
		src->a[1] = src->b[1] = s->y;
		src->radius = s->radius;
		src->width = 0.0f;
		src->strength = Com_Clamp( 0.0f, 1.0f, left * 1.15f );
		src->distance = 0.0f;
		src->seed = s->seed;
	}
}

// The drops on the view move on: a large one runs down, faster the larger it is, and sticks for a moment now and
// then; its trail grows behind it. They shrink a little as they dry. Each is a source of the frame.
static void CG_ScreenFxDrops( void ) {
	const float	dt = Com_Clamp( 0.0f, 0.1f, cg.frametime * 0.001f );
	int			i;

	for ( i = 0; i < FX_DROPS; i++ ) {
		fxDrop_t			*d = &sfx.drops[i];
		screenFxSource_t	*src;
		int					age = cg.time - d->born;
		float				wet;

		if ( !d->active ) {
			continue;
		}
		if ( age >= d->life || age < 0 || d->y - d->radius > 1.05f
			|| ( cg.refdef.viewContents & ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA ) ) ) {
			d->active = qfalse;
			continue;
		}
		wet = age > d->life - DROP_DRY_MSEC ? (float)( d->life - age ) / DROP_DRY_MSEC : 1.0f;
		if ( d->radius > DROP_SLIDE && cg.time >= d->stopUntil ) {
			const float top = ( d->radius - DROP_SLIDE ) * 9.0f;

			d->speed = Q_min( top, d->speed + top * 3.0f * dt );
			d->y += d->speed * dt;
			d->trail = Q_min( 0.35f, d->trail + d->speed * dt );
			// (water runs in fits: it stops where the glass holds it a moment)
			if ( Q_flrand( 0.0f, 1.0f ) < dt * 1.5f ) {
				d->stopUntil = cg.time + (int)Q_flrand( 150.0f, 600.0f );
				d->speed = 0.0f;
			}
			// it loses water into its trail
			d->radius -= d->speed * dt * 0.012f;
		}
		src = CG_ScreenFxSource();
		if ( !src ) {
			break;
		}
		src->kind = SCREENFX_DROP;
		src->a[0] = src->b[0] = d->x;
		src->a[1] = src->b[1] = d->y;
		src->radius = d->radius * ( 0.75f + 0.25f * wet );
		src->width = d->trail * wet;
		src->strength = wet * wet * ( 3.0f - 2.0f * wet );
		src->distance = 0.0f;
		src->seed = d->seed;
	}
}

// the own weapon was fired: a repeater or a blaster gets hot
void CG_ScreenFxFired( int weapon ) {
	if ( weapon != WP_REPEATER && weapon != WP_BLASTER ) {
		return;
	}
	sfx.barrel = Com_Clamp( 0.0f, 1.0f, Q_max( 0.0f, sfx.barrel - BARREL_COOL * ( cg.time - sfx.barrelTime ) * 0.001f ) + BARREL_SHOT );
	sfx.barrelTime = cg.time;
}

// the barrel of the own weapon after a long burst: hot air, and smoke when it is very hot
static void CG_ScreenFxBarrel( void ) {
	const float	heat = sfx.barrel - BARREL_COOL * ( cg.time - sfx.barrelTime ) * 0.001f;
	const int	weapon = cg.predictedPlayerState.weapon;
	const vec3_t rise = { 0.0f, 0.0f, 20.0f };
	vec3_t		muzzle;

	if ( heat <= BARREL_HOT || ( weapon != WP_REPEATER && weapon != WP_BLASTER ) || cg.predictedPlayerState.stats[STAT_HEALTH] <= 0 ) {
		return;
	}
	if ( !CG_CalcMuzzlePoint( cg.predictedPlayerState.clientNum, muzzle ) ) {
		return;
	}
	CG_ScreenFxEmit( cg.predictedPlayerState.clientNum * 4 + 3, muzzle, rise, 3.5f, ( heat - BARREL_HOT ) / ( 1.0f - BARREL_HOT ),
		heat > BARREL_SMOKE ? 0.4f + ( heat - BARREL_SMOKE ) / ( 1.0f - BARREL_SMOKE ) * 0.6f : 0.0f );
}

// The trails of the emitters: from where each is now (when it is still there) through its puffs, newest first, a
// line of hot air from one to the next. A puff rises and drifts the way it was blown, and widens while it fades.
static void CG_ScreenFxTrails( void ) {
	int i, j, k;

	for ( i = 0; i < FX_EMITTERS; i++ ) {
		const fxEmitter_t	*e = &sfx.emitters[i];
		vec3_t				points[FX_PUFFS / 4 + 1];
		float				radius[FX_PUFFS / 4 + 1], strength[FX_PUFFS / 4 + 1];
		int					birth[FX_PUFFS / 4 + 1];
		int					count = 0;

		if ( !e->seenTime || cg.time - e->seenTime > PUFF_LIFE ) {
			continue;
		}
		if ( e->seenTime == cg.time ) {
			VectorCopy( e->origin, points[0] );
			radius[0] = e->radius;
			strength[0] = e->strength;
			birth[0] = cg.time;
			count = 1;
		}
		for ( j = 0; j < FX_PUFFS && count < (int)ARRAY_LEN( points ); j++ ) {
			const fxPuff_t	*puff = &sfx.puffs[j];
			const float		age = ( cg.time - puff->birth ) * 0.001f;
			const float		t = ( cg.time - puff->birth ) / (float)PUFF_LIFE;

			if ( puff->id != e->id || !puff->birth || t < 0.0f || t >= 1.0f ) {
				continue;
			}
			// in order of birth, the newest first
			for ( k = count; k > 0 && birth[k - 1] < puff->birth; k-- ) {
				VectorCopy( points[k - 1], points[k] );
				radius[k] = radius[k - 1];
				strength[k] = strength[k - 1];
				birth[k] = birth[k - 1];
			}
			VectorMA( puff->origin, age, puff->velocity, points[k] );
			points[k][2] += PUFF_RISE * age * age;
			radius[k] = puff->radius * ( 1.0f + 2.0f * t );
			strength[k] = puff->strength * ( 1.0f - t );
			birth[k] = puff->birth;
			count++;
		}
		for ( j = 0; j + 1 < count; j++ ) {
			CG_ScreenFxHeatLine( points[j], points[j + 1], Q_max( radius[j], radius[j + 1] ), 0.0f, 0.5f * ( strength[j] + strength[j + 1] ) );
		}
	}
}

// The puffs of smoke: they rise faster than they were blown, widen to four times their size, come in quickly and
// thin out slowly.
static void CG_ScreenFxSmokes( void ) {
	int i;

	for ( i = 0; i < FX_SMOKES; i++ ) {
		const fxSmoke_t		*puff = &sfx.smokes[i];
		const float			age = ( cg.time - puff->birth ) * 0.001f;
		const float			t = ( cg.time - puff->birth ) / (float)SMOKE_LIFE;
		screenFxSource_t	*src;
		vec3_t				at;
		float				frac[2], depth, radius;

		if ( !puff->birth || t < 0.0f || t >= 1.0f ) {
			continue;
		}
		VectorMA( puff->origin, age, puff->velocity, at );
		at[2] += SMOKE_RISE * age;
		radius = puff->radius * ( 1.0f + 3.0f * sqrtf( t ) );
		if ( !CG_ScreenFxProject( at, frac, &depth ) ) {
			continue;
		}
		src = CG_ScreenFxSource();
		if ( !src ) {
			return;
		}
		src->kind = SCREENFX_SMOKE;
		VectorCopy2( frac, src->a );
		src->radius = Q_min( CG_ScreenFxSize( radius, depth ), SMOKE_MAX_SIZE );
		src->strength = puff->density * Q_min( 1.0f, t * 8.0f ) * ( 1.0f - t ) * ( 1.0f - t ) * CG_ScreenFxNear( depth, NEAR_FADE, NEAR_FULL );
		src->distance = depth;
		src->seed = puff->seed;
	}
}

/*
=================
CG_ScreenFxPlayer

Once a frame for every player that is drawn. A push or a pull with the Force (his torso starts the animation of
it, or starts it again: torsoFlip changes) sends a wave through the air from his hands the way they push, a pull
draws it back to them. The own one in first person starts in front of the eye, where the hand is seen. Mind trick plays the same animation;
only a push or pull sets PW_DISINT_4 (ForceThrow of the game), so the waves wait for that while it plays.
=================
*/
void CG_ScreenFxPlayer( centity_t *cent ) {
	const int		num = cent->currentState.number;
	const int		anim = cent->currentState.torsoAnim;
	const qboolean	flip = cent->currentState.torsoFlip;
	qboolean		started;
	fxRipple_t		*ripple;

	if ( num < 0 || num >= MAX_GENTITIES ) {
		return;
	}
	started = (qboolean)( anim != sfx.pushAnim[num] || flip != sfx.pushFlip[num] );
	sfx.pushAnim[num] = anim;
	sfx.pushFlip[num] = flip;
	if ( anim != BOTH_FORCEPUSH && anim != BOTH_FORCEPULL ) {
		sfx.pushArmed[num] = qfalse;
		return;
	}
	if ( started ) {
		sfx.pushArmed[num] = qtrue;
	}
	if ( !sfx.pushArmed[num] || !( cent->currentState.powerups & ( 1 << PW_DISINT_4 ) ) || !CG_ScreenFxOn() || !v_fxForce.integer ) {
		return;
	}
	sfx.pushArmed[num] = qfalse;
	ripple = &sfx.ripples[sfx.nextRipple];
	sfx.nextRipple = ( sfx.nextRipple + 1 ) % FX_RIPPLES;
	ripple->birth = cg.time;
	ripple->pull = (qboolean)( anim == BOTH_FORCEPULL || ( cent->currentState.powerups & ( 1 << PW_PULL ) ) );
	if ( num == cg.snap->ps.clientNum && !cg.renderingThirdPerson ) {
		VectorCopy( cg.refdef.viewaxis[0], ripple->dir );
		VectorMA( cg.refdef.vieworg, 32.0f, ripple->dir, ripple->origin );
	} else {
		AngleVectors( cent->lerpAngles, ripple->dir, NULL, NULL );
		VectorMA( cent->lerpOrigin, 32.0f, ripple->dir, ripple->origin );
		ripple->origin[2] += 20.0f;
	}
}

// Blades that clash or block: a star of light at origin, size units across
void CG_ScreenFxFlash( const vec3_t origin, float size, float strength ) {
	fxFlash_t *flash;

	if ( !CG_ScreenFxOn() || !v_fxSaber.integer ) {
		return;
	}
	flash = &sfx.flashes[sfx.nextFlash];
	sfx.nextFlash = ( sfx.nextFlash + 1 ) % FX_FLASHES;
	flash->birth = cg.time;
	VectorCopy( origin, flash->origin );
	flash->size = size;
	flash->strength = strength;
}

// a lit blade from base to tip this frame: the air around it is hot and shimmers, the blade itself stays still
void CG_ScreenFxBlade( const vec3_t base, const vec3_t tip ) {
	if ( CG_ScreenFxOn() && v_fxSaber.integer ) {
		CG_ScreenFxHeatLine( base, tip, 10.0f, 3.0f, 0.4f );
	}
}

// Force Sight is drawn as the other world, its players as ghosts (CG_Player asks)
qboolean CG_ScreenFxSightDrawn( void ) {
	return (qboolean)( CG_ScreenFxOn() && v_fxForce.integer );
}

/*
=================
CG_ScreenFxGhostBody

The body of a player Force Sight shows, as a ghost: half seen through, pale green, lit by itself
=================
*/
void CG_ScreenFxGhostBody( refEntity_t *body ) {
	body->renderfx |= RF_FORCE_ENT_ALPHA | RF_RGB_TINT | RF_MINLIGHT;
	body->shaderRGBA[0] = 150;
	body->shaderRGBA[1] = 255;
	body->shaderRGBA[2] = 200;
	body->shaderRGBA[3] = 70;
}

/*
=================
CG_ScreenFxGhostShell

Over a player Force Sight shows (body as CG_Player has him), the ghost: gfx/vision/ghost, smoke that rises up
him with holes wandering through it, glows over him and flickers a little, through walls where the game lets the power see through them. And he smokes: his body, a line
from his feet to his head, puts smoke into the smoke of the view (screenfxsmoke.frag), which rises off him and
smears when he or the eye moves.
=================
*/
void CG_ScreenFxGhostShell( centity_t *cent, const refEntity_t *body, qboolean throughWalls ) {
	const int			num = cent->currentState.number;
	const float			flicker = 0.85f + 0.15f * sinf( cg.time * 0.013f + num * 1.7f ) * sinf( cg.time * 0.0071f + num );
	refEntity_t			shell = *body;
	screenFxSource_t	*src;
	vec3_t				feet, head;
	float				fracA[2], fracB[2], depthA, depthB;

	if ( !sfx.ghostShader || num < 0 || num >= MAX_CLIENTS ) {
		return;
	}
	shell.customShader = sfx.ghostShader;
	shell.renderfx = ( shell.renderfx & ~( RF_FORCE_ENT_ALPHA | RF_RGB_TINT ) ) | RF_MINLIGHT;
	if ( throughWalls ) {
		shell.renderfx |= RF_NODEPTH;
	}
	shell.shaderRGBA[0] = (byte)( 130 * flicker );
	shell.shaderRGBA[1] = (byte)( 255 * flicker );
	shell.shaderRGBA[2] = (byte)( 190 * flicker );
	// how much of the smoke of gfx/vision/ghost the alpha test leaves: about a third, more and less as he breathes
	shell.shaderRGBA[3] = (byte)( 255 * ( 0.73f + 0.07f * sinf( cg.time * 0.0023f + num ) ) );
	trap->R_AddRefEntityToScene( &shell );

	VectorCopy( cent->lerpOrigin, feet );
	VectorCopy( cent->lerpOrigin, head );
	feet[2] += DEFAULT_MINS_2 + 6.0f;
	head[2] += 34.0f;
	if ( !CG_ScreenFxProject( feet, fracA, &depthA ) || !CG_ScreenFxProject( head, fracB, &depthB ) ) {
		return;
	}
	src = CG_ScreenFxSource();
	if ( !src ) {
		return;
	}
	src->kind = SCREENFX_GHOSTSMOKE;
	VectorCopy2( fracA, src->a );
	VectorCopy2( fracB, src->b );
	src->radius = Q_min( CG_ScreenFxSize( 20.0f, Q_min( depthA, depthB ) ), SMOKE_MAX_SIZE );
	// strong, so it is not the first source left out when there are too many
	src->strength = 1.0f;
	src->distance = throughWalls ? 0.0f : Q_min( depthA, depthB );
}

static void CG_ScreenFxRipples( void ) {
	int i;

	for ( i = 0; i < FX_RIPPLES; i++ ) {
		const fxRipple_t	*ripple = &sfx.ripples[i];
		const float			t = ( cg.time - ripple->birth ) / (float)RIPPLE_MSEC;
		screenFxSource_t	*src;
		vec3_t				front;
		float				frac[2], depth, radius, along;

		if ( !ripple->birth || t < 0.0f || t >= 1.0f ) {
			continue;
		}
		// The front runs from the hands the way they push and opens as a cone; a pull runs it the other way, from
		// the far end back to the hands.
		along = RIPPLE_REACH * ( ripple->pull ? ( 1.0f - t ) : ( 1.0f - ( 1.0f - t ) * ( 1.0f - t ) ) );
		VectorMA( ripple->origin, along, ripple->dir, front );
		if ( !CG_ScreenFxProject( front, frac, &depth ) ) {
			continue;
		}
		src = CG_ScreenFxSource();
		if ( !src ) {
			return;
		}
		radius = 12.0f + along * 0.45f;
		src->kind = SCREENFX_RIPPLE;
		VectorCopy2( frac, src->a );
		src->radius = CG_ScreenFxSize( radius, depth );
		src->width = src->radius * 0.35f;
		src->strength = 1.0f - t;
		src->distance = depth - radius;
		src->seed = ripple->pull ? -1.0f : 1.0f;
	}
}

static void CG_ScreenFxFlashes( void ) {
	int i;

	for ( i = 0; i < FX_FLASHES; i++ ) {
		const fxFlash_t		*flash = &sfx.flashes[i];
		const float			t = ( cg.time - flash->birth ) / (float)FLASH_MSEC;
		screenFxSource_t	*src;
		float				frac[2], depth;

		if ( !flash->birth || t < 0.0f || t >= 1.0f || !CG_ScreenFxProject( flash->origin, frac, &depth ) ) {
			continue;
		}
		src = CG_ScreenFxSource();
		if ( !src ) {
			return;
		}
		src->kind = SCREENFX_FLASH;
		VectorCopy2( frac, src->a );
		src->radius = CG_ScreenFxSize( flash->size, depth );
		src->strength = flash->strength * ( 1.0f - t ) * ( 1.0f - t );
		src->distance = depth - flash->size;
	}
}

static float CG_ScreenFxEase( float value, qboolean on ) {
	const float step = cg.frametime / (float)FORCE_MSEC;

	return on ? Q_min( 1.0f, value + step ) : Q_max( 0.0f, value - step );
}

// the Force powers of the own player that have a look, eased in and out
static void CG_ScreenFxForce( void ) {
	const int		active = cg.snap->ps.fd.forcePowersActive;
	const qboolean	own = (qboolean)( v_fxForce.integer && !( cg.snap->ps.pm_flags & PMF_FOLLOW ) && cg.snap->ps.pm_type != PM_SPECTATOR );

	sfx.rage = CG_ScreenFxEase( sfx.rage, (qboolean)( own && ( active & ( 1 << FP_RAGE ) ) ) );
	sfx.rageRecovery = CG_ScreenFxEase( sfx.rageRecovery, (qboolean)( own && !( active & ( 1 << FP_RAGE ) )
		&& cg.snap->ps.fd.forceRageRecoveryTime > cg.time ) );
	sfx.absorb = CG_ScreenFxEase( sfx.absorb, (qboolean)( own && ( active & ( 1 << FP_ABSORB ) ) ) );
	sfx.protect = CG_ScreenFxEase( sfx.protect, (qboolean)( own && ( active & ( 1 << FP_PROTECT ) ) ) );
	sfx.speed = CG_ScreenFxEase( sfx.speed, (qboolean)( own && ( active & ( 1 << FP_SPEED ) ) ) );
	sfx.sight = CG_ScreenFxEase( sfx.sight, (qboolean)( own && ( active & ( 1 << FP_SEE ) ) ) );

	// a hit on the shield of protect, the Force taken in by absorb
	if ( ( active & ( 1 << FP_PROTECT ) ) && cg.damageTime > sfx.protectHitTime && cg.damageTime <= cg.time ) {
		sfx.protectHitTime = cg.damageTime;
	}
	// (more than the power comes back by itself, a point at a time)
	if ( ( active & ( 1 << FP_ABSORB ) ) && cg.snap->ps.fd.forcePower > sfx.lastForcePower + 2 ) {
		sfx.absorbTime = cg.time;
	}
	sfx.lastForcePower = cg.snap->ps.fd.forcePower;
	// rage growls quietly in the ears
	if ( sfx.rage > 0.0f ) {
		trap->S_AddLoopingSound( cg.snap->ps.clientNum, cg.refdef.vieworg, vec3_origin, sfx.rageSound );
	}
}

// A rim of colour, mixed with the one there by how strong each is; the stronger one wins the arc of a hit.
static void CG_ScreenFxRim( screenEffect_t *fx, float r, float g, float b, float amount ) {
	const float total = fx->vignette[3] + amount;

	if ( amount <= 0.0f ) {
		return;
	}
	fx->vignette[0] = ( fx->vignette[0] * fx->vignette[3] + r * amount ) / total;
	fx->vignette[1] = ( fx->vignette[1] * fx->vignette[3] + g * amount ) / total;
	fx->vignette[2] = ( fx->vignette[2] * fx->vignette[3] + b * amount ) / total;
	if ( amount > fx->vignette[3] ) {
		fx->vignette[3] = amount;
		VectorClear( fx->vignetteArc );
	}
}

// Under water, in slime or in lava, eased in and out. Coming up starts the drops.
static void CG_ScreenFxWater( void ) {
	const int	contents = cg.refdef.viewContents & ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA );
	const float	step = cg.frametime / (float)WATER_MSEC;

	if ( contents && v_screenFx.integer && v_fxWater.integer ) {
		sfx.water = Q_min( 1.0f, sfx.water + step );
		sfx.waterContents = contents;
		sfx.surfaceTime = 0;
	} else {
		if ( sfx.water >= 1.0f && !contents ) {
			sfx.surfaceTime = cg.time;
		}
		sfx.water = Q_max( 0.0f, sfx.water - step );
	}
}

/*
=================
CG_ScreenFxMuffle

The one place that sets v_muffle, v_tinnitus and v_dreamReverb (the engine dulls the sound of the game by the first,
lets the ears ring by the second and lets everything ring long as in a dream by the third: S_MufflePaint,
S_TinnitusPaint, S_SpatialRenderDream with Steam Audio): the most of the menu in the level, being under water and an
explosion close by; Force Sight dulls a little and dreams. After the bang the ringing is loud at once and dies away
slower than the dull sound.
=================
*/
static void CG_ScreenFxMuffle( void ) {
	float muffle = CG_Menu3DMuffle(), tinnitus = 0.0f;

	muffle = Q_max( muffle, sfx.water * 0.85f );
	muffle = Q_max( muffle, sfx.sight * 0.35f );
	if ( sfx.deaf > 0.0f && cg.time >= sfx.deafTime && cg.time - sfx.deafTime < DEAF_MSEC ) {
		const float left = 1.0f - ( cg.time - sfx.deafTime ) / (float)DEAF_MSEC;

		muffle = Q_max( muffle, 0.9f * sfx.deaf * left * left );
		tinnitus = sfx.deaf * sqrtf( left );
	}
	muffle = Com_Clamp( 0.0f, 1.0f, muffle );
	if ( fabsf( muffle - sfx.muffle ) > 0.02f || ( muffle != sfx.muffle && ( muffle == 0.0f || muffle == 1.0f ) ) ) {
		sfx.muffle = muffle;
		trap->Cvar_Set( "v_muffle", va( "%.2f", muffle ) );
	}
	if ( fabsf( tinnitus - sfx.tinnitus ) > 0.02f || ( tinnitus != sfx.tinnitus && tinnitus == 0.0f ) ) {
		sfx.tinnitus = tinnitus;
		trap->Cvar_Set( "v_tinnitus", va( "%.2f", tinnitus ) );
	}
	if ( fabsf( sfx.sight - sfx.dream ) > 0.02f || ( sfx.sight != sfx.dream && ( sfx.sight == 0.0f || sfx.sight == 1.0f ) ) ) {
		sfx.dream = sfx.sight;
		trap->Cvar_Set( "v_dreamReverb", va( "%.2f", sfx.dream ) );
	}
}

// The heart below LOW_HEALTH: a beat that comes faster the lower the health, with any renderer. How strongly it
// shows on the rim right now, 0 to 1.
static float CG_ScreenFxHeart( void ) {
	const int	health = cg.predictedPlayerState.stats[STAT_HEALTH];
	float		since;

	if ( !v_screenFx.integer || !v_fxHit.integer || !CG_ScreenFxOwnPlayer() || health >= LOW_HEALTH ) {
		sfx.nextBeat = 0;
		return 0.0f;
	}
	if ( cg.time >= sfx.nextBeat || sfx.nextBeat - cg.time > 2000 ) {
		trap->S_StartLocalSound( sfx.beatSound, CHAN_LOCAL_SOUND );
		sfx.beatTime = cg.time;
		sfx.nextBeat = cg.time + 550 + health * 15;
	}
	// the two beats of one, 190 msec apart as in the sound
	since = (float)( cg.time - sfx.beatTime );
	return expf( -since / 140.0f ) + ( since > 190.0f ? 0.6f * expf( -( since - 190.0f ) / 140.0f ) : 0.0f );
}

// the rings of the explosions that still run
static void CG_ScreenFxBlasts( void ) {
	int i;

	for ( i = 0; i < FX_BLASTS; i++ ) {
		const fxBlast_t		*blast = &sfx.blasts[i];
		const float			t = ( cg.time - blast->birth ) / (float)BLAST_MSEC;
		screenFxSource_t	*src;
		float				frac[2], depth, radius;

		if ( !blast->birth || t < 0.0f || t >= 1.0f ) {
			continue;
		}
		if ( !CG_ScreenFxProject( blast->origin, frac, &depth ) ) {
			continue;
		}
		src = CG_ScreenFxSource();
		if ( !src ) {
			return;
		}
		// fast at first, then slower, and fainter as it goes
		radius = BLAST_REACH * blast->radius * ( 1.0f - ( 1.0f - t ) * ( 1.0f - t ) );
		src->kind = SCREENFX_RING;
		VectorCopy2( frac, src->a );
		src->radius = CG_ScreenFxSize( radius, depth );
		src->width = Q_max( 0.006f, src->radius * 0.25f );
		src->strength = 1.0f - t * t;
		src->distance = depth - radius;
	}
}

static int QDECL CG_ScreenFxStronger( const void *a, const void *b ) {
	const float sa = ( (const screenFxSource_t *)a )->strength, sb = ( (const screenFxSource_t *)b )->strength;

	return sa < sb ? 1 : ( sa > sb ? -1 : 0 );
}

/*
=================
CG_ScreenFxScene

Before the view of the level is rendered: what the renderer is to make of it
=================
*/
void CG_ScreenFxScene( void ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	screenEffect_t		fx;
	float				split, pulse;
	qboolean			any;

	// what is heard comes with any renderer: under water, the Force (the hum of rage, the dream of Force Sight)
	CG_ScreenFxWater();
	CG_ScreenFxForce();
	CG_ScreenFxMuffle();
	pulse = CG_ScreenFxHeart();
	if ( !CG_ScreenFxOn() ) {
		sfx.numSources = 0;
		return;
	}
	CG_ScreenFxBarrel();
	CG_ScreenFxTrails();
	CG_ScreenFxSmokes();
	CG_ScreenFxBlasts();
	CG_ScreenFxRipples();
	CG_ScreenFxFlashes();
	if ( v_fxWater.integer ) {
		CG_ScreenFxSnows();
		CG_ScreenFxDrops();
	}
	split = sfx.split * ( 1.0f - ( cg.time - sfx.splitTime ) / (float)SPLIT_MSEC );

	memset( &fx, 0, sizeof( fx ) );
	fx.time = ( cg.time & 0xfffff ) * 0.001f;
	fx.frameTime = cg.frametime * 0.001f;

	// The Force: rage beats red at the rim, its recovery drains the colours, absorb is a blue rim, protect a green
	// one, speed streaks from the middle, sight is a picture of its own (screenfx.frag)
	{
		// the beat of rage, twice as fast as a calm heart, sharp
		const float beat = 0.5f + 0.5f * sinf( cg.time * 0.0105f );

		fx.ragePulse = beat * beat * beat * beat;
	}
	fx.rage = sfx.rage;
	split = Q_max( split, 0.35f * fx.ragePulse * sfx.rage );
	fx.desaturate = 0.5f * sfx.rageRecovery;
	CG_ScreenFxRim( &fx, 0.08f, 0.08f, 0.08f, 0.3f * sfx.rageRecovery );
	CG_ScreenFxRim( &fx, 0.25f, 0.45f, 1.0f, 0.18f * sfx.absorb );
	fx.absorb = sfx.absorb;
	if ( cg.time >= sfx.absorbTime && cg.time - sfx.absorbTime < ABSORB_PULSE_MSEC ) {
		fx.absorbPulse = 1.0f - ( cg.time - sfx.absorbTime ) / (float)ABSORB_PULSE_MSEC;
	}
	CG_ScreenFxRim( &fx, 0.35f, 0.9f, 0.45f, 0.15f * sfx.protect );
	fx.protect = sfx.protect;
	fx.radialBlur = 0.45f * sfx.speed;
	fx.sight = sfx.sight;
	// how far the picture moved on the view as the eye turned since the frame before: turned left, the level moves
	// to the right; looking down, it moves up. A turn that is too large is a jump of the view, not a turn.
	{
		const float yaw = AngleDelta( cg.refdef.viewangles[YAW], sfx.lastAngles[YAW] );
		const float pitch = AngleDelta( cg.refdef.viewangles[PITCH], sfx.lastAngles[PITCH] );

		if ( fabsf( yaw ) < 45.0f && fabsf( pitch ) < 45.0f ) {
			fx.viewShift[0] = yaw / cg.refdef.fov_x;
			fx.viewShift[1] = -pitch / cg.refdef.fov_y;
		}
		VectorCopy( cg.refdef.viewangles, sfx.lastAngles );
	}


	// A hit: the rim red on the side it came from (all around when the game knows no side), blue for the
	// shields, and the colours part for a moment. Below LOW_HEALTH the colours drain and the rim beats.
	if ( v_fxHit.integer && CG_ScreenFxOwnPlayer() ) {
		const int	t = cg.time - cg.damageTime;
		const int	health = ps->stats[STAT_HEALTH];
		float		hit = 0.0f;

		if ( cg.damageValue && t >= 0 && t < HIT_MSEC ) {
			hit = Com_Clamp( 0.3f, 1.0f, cg.damageValue / 30.0f ) * ( 1.0f - t / (float)HIT_MSEC );
			if ( t < HIT_SPLIT_MSEC ) {
				split = Q_max( split, 0.6f * hit * ( 1.0f - t / (float)HIT_SPLIT_MSEC ) );
			}
		}
		if ( health < LOW_HEALTH ) {
			const float low = 1.0f - health / (float)LOW_HEALTH;

			fx.desaturate = Q_max( fx.desaturate, 0.75f * low );
			CG_ScreenFxRim( &fx, 0.3f, 0.0f, 0.0f, low * ( 0.35f + 0.35f * Q_min( 1.0f, pulse ) ) );
		}
		if ( hit * 0.85f > fx.vignette[3] ) {
			if ( cg.snap->ps.damageType == 1 ) {
				CG_ScreenFxRim( &fx, 0.2f, 0.45f, 0.9f, hit * 0.85f );
			} else if ( cg.snap->ps.damageType == 2 ) {
				CG_ScreenFxRim( &fx, 0.8f, 0.55f, 0.15f, hit * 0.85f );
			} else {
				CG_ScreenFxRim( &fx, 0.75f, 0.04f, 0.02f, hit * 0.85f );
			}
			fx.vignetteArc[0] = cg.damageX;
			fx.vignetteArc[1] = cg.damageY;
			fx.vignetteArc[2] = ( cg.damageX || cg.damageY ) ? 0.45f : 0.0f;
		}
	}

	if ( v_fxWater.integer ) {
		fx.water = sfx.water;
		if ( sfx.waterContents & CONTENTS_LAVA ) {
			VectorSet( fx.waterColor, 0.9f, 0.25f, 0.05f );
		} else if ( sfx.waterContents & CONTENTS_SLIME ) {
			VectorSet( fx.waterColor, 0.25f, 0.55f, 0.1f );
		} else {
			VectorSet( fx.waterColor, 0.05f, 0.3f, 0.4f );
		}
		if ( sfx.surfaceTime && cg.time - sfx.surfaceTime < DROPS_MSEC && cg.time >= sfx.surfaceTime ) {
			fx.drops = sqrtf( 1.0f - ( cg.time - sfx.surfaceTime ) / (float)DROPS_MSEC );
		}
	}

	// the shield of protect flares on the side the hit came from
	if ( fx.protect > 0.0f && cg.time >= sfx.protectHitTime && cg.time - sfx.protectHitTime < PROTECT_HIT_MSEC ) {
		fx.protectHit = 1.0f - ( cg.time - sfx.protectHitTime ) / (float)PROTECT_HIT_MSEC;
		fx.vignetteArc[0] = cg.damageX;
		fx.vignetteArc[1] = cg.damageY;
	}
	fx.split = Q_max( 0.0f, split );
	// the filter of the photo mode (photo/cg_photo.c)
	CG_PhotoGrade( &fx );
	any = (qboolean)( sfx.numSources || fx.split > 0.0f || fx.desaturate > 0.0f || fx.vignette[3] > 0.0f || fx.water > 0.0f
		|| fx.drops > 0.0f || fx.radialBlur > 0.0f || fx.sight > 0.0f || fx.tint[3] > 0.0f || fx.rage > 0.0f || fx.protect > 0.0f
		|| fx.absorb > 0.0f );
	if ( any ) {
		qsort( sfx.sources, sfx.numSources, sizeof( sfx.sources[0] ), CG_ScreenFxStronger );
		fx.numSources = Q_min( sfx.numSources, SCREENFX_MAX_SOURCES );
		memcpy( fx.sources, sfx.sources, sizeof( fx.sources[0] ) * fx.numSources );
		trap->ext.R_SetScreenEffect( &fx );
	}
	sfx.numSources = 0;
}
