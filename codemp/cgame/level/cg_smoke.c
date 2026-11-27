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

// cg_smoke.c -- smoke that reacts (issue #23, "Smoke that reacts"). The smoke of the effects of the game is gone a
// second or two after what made it. Here an explosion leaves smoke that hangs in the room, sinks to the floor and lies
// there for a while, a rocket leaves a trail of it behind, and a shot that hits a wall knocks a puff of it out.
// Indoors it stays, held by the walls; under the open sky the wind takes it and it is gone sooner.
//
// The puffs go to the renderer as a volume (ext.R_SetSmoke, smokeVolume_t; rd-vulkan-vision, vk_smoke.cpp), which
// marches the rays of the view through them: the smoke has depth, a player stands in it and it ends at a wall.
// Sprites of smoke were tried first: flat pictures that turn with the eye (their words 2026-11-26). Without that call
// of the engine: sprites of the smoke sheet of the explosions (gfx/vision/fb/smoke, 8 x 8 tiles).
//
// What goes through the smoke leaves a hole in that volume, clear at once and closing again over some seconds, as
// shots and grenades open the smoke of other games: a shot bores a way, a player who walks through leaves one behind
// him, a blade cuts it open, a Force Push clears its line. A blast thins the smoke that was there round it, which comes
// back too. Nothing moves the smoke itself. That was
// tried: pushes, blasts, blades and bodies gave the puffs speed, later tore them into parts that flowed, and both
// looked like balls being shoved about (their words 2026-11-26).
//
// On v_levelFx. Only a look: the game knows nothing of it.

#include "cgame/cg_local.h"
#include "cgame/level/cg_gust.h"
#include "cgame/level/cg_smoke.h"

#define SMOKE_PUFFS			512
#define SMOKE_TILES			8			// the sheet has that many tiles a side
#define SMOKE_DRAG			1.3f		// of its speed a second: the air stops it
#define SMOKE_MAX_SPEED		520.0f
#define SMOKE_LOOK_MSEC		200			// a puff looks that often for the wall it drifts towards
#define SMOKE_FAST			70.0f		// units a second from which it looks every frame
#define SMOKE_FAR			3000.0f		// further from the eye nothing is left behind
// It settles: after its first moments a cloud sinks to the floor and lies down on it, flatter and wider, as smoke
// does when it has cooled (their word 2026-11-26). From when, how fast, over what time, how flat and how much wider:
// v_smokeSinkDelay, v_smokeSinkSpeed, v_smokeSinkTime, v_smokeHeight, v_smokeSpread (cg_xcvar.h).
#define SETTLE_REST			0.4f		// how much of its height its middle is above the floor: it lies in it a little
#define SETTLE_LOOK_MSEC	700			// it looks that often where the floor is
// what an explosion leaves
#define BLAST_PUFFS			16			// at size 1: many small ones, a cloud of billows (times v_smokeAmount)
#define SMOKE_COME_MSEC		60			// a puff is there that fast: an explosion throws its smoke out at once
#define SMOKE_GROW_FADE		1.4f		// a second: its growing is over that soon (it is e times less each 1 / this)
// puffs that have lain down next to each other become one: fewer rims where they meet, and less to draw
#define MERGE_MSEC			250			// they are looked through that often
#define MERGE_NEAR			0.75f		// of the larger radius the middles of two that lie are apart at most
#define MERGE_NEAR_AIR		0.3f		// and of two in the air, or of the dust of shots: only what is nearly in one place,
										//	the heap of a blast keeps its shape
#define MERGE_AIR_MSEC		400			// those are that old at least: a blast is a heap of billows at first
#define MERGE_MAX_SIZE		50.0f		// a puff that large takes no more in (times v_smokeSize)
#define MERGE_OVER			8.0f		// a second: how fast the one grows as the other goes (e times nearer each 1 / this)
#define MERGE_GONE_MSEC		280			// the one that was taken in is gone after that
#define SMOKE_FADE			0.4f		// of the time it has a puff goes over at its end
#define TRAIL_STEP			20.0f		// units of flight a puff: they lie into each other, a streak and not a row of balls
#define TRAIL_LIFE			2200
// what moves it
#define PUSH_REACH			440.0f		// units a Force Push carries smoke along
#define BLADE_FAST			220.0f		// units a second the blade must move for that
#define CUT_PUSH_RADIUS		70.0f		// units round the line of a Force Push
#define CUT_PUSH_MSEC		3200		// and how long until it has closed again
#define CUT_BODY_RADIUS		44.0f		// round the way of a player who walks through
#define CUT_BODY_MSEC		2200
#define CUT_BODY_STEP		110.0f		// units of his way a hole: it stays behind and closes, the next one goes on
#define CUT_BLAST_RADIUS	150.0f		// at size 1: a blast clears the smoke that is there this far
#define CUT_BLAST_MSEC		3200
#define CUT_BLADE_RADIUS	32.0f
#define CUT_BLADE_MSEC		1000
#define CUT_SHOT_RADIUS		17.0f		// round the way of a shot
#define CUT_SHOT_MSEC		2200
#define CUT_FRESH_MSEC		250			// a shot or a blade keeps its hole while it was seen that lately

typedef struct smokePuff_s {
	int			birth, life;			// life 0: free. birth may lie ahead: it comes then
	vec3_t		origin, velocity;
	float		size, grow;				// its radius, and what it gains a second (less and less)
	float		alpha;					// at its thickest
	float		rise;					// units a second it drifts up while it is young
	float		rotation, spin;
	int			tile, tiles;			// the first tile of the sheet and how many it runs through
	byte		shade;
	qboolean	open;					// under the sky: the wind has it
	int			nextLook;
	vec3_t		color;					// the smoke in the light of its place, looked up now and then
	int			nextLight;
	qboolean	settles;				// it sinks to the floor and lies flat there
	float		flat;					// its height against its width, 1 a ball
	float		floorZ;					// the floor under it, as looked up last
	int			nextFloor;
	vec3_t		tint;					// its colour in white light (the dust of a wall that was hit), the grey from shade
	vec3_t		home;					// where it came: its billows are the ones of that place, they travel with it
	int			blown;					// cg.time a blast went off at it, 0: none
	float		blownBy;				// how much of it that took, 1 all: it comes back over CUT_BLAST_MSEC
	int			fade;					// msec over which it goes at its end
	float		shown;					// how thick it is now: what it should be, a moment late (no jump when it takes one in)
	qboolean	merges;					// puffs of its kind that lie in each other become one (Smoke_Merge)
	float		gain;					// radius it still gains from the ones it took in
	vec3_t		shift;					// and the way its middle still goes to lie between them
	int			taken;					// cg.time another one took it in: it goes as that one grows, 0: not
} smokePuff_t;

// a hole in the smoke round a line. owner: what keeps it open and moves it along (a shot, a blade), 0: nobody
typedef struct {
	int			birth, life;			// life 0: free
	int			owner, touched;
	vec3_t		start, end;
	float		radius;
} smokeCut_t;

typedef struct {
	qboolean	valid;
	int			time;
	vec3_t		base, tip;
} smokeBlade_t;

static smokePuff_t	puffs[SMOKE_PUFFS];
static qhandle_t	smokeShader;
static vec3_t		trailAt[MAX_GENTITIES];		// where a rocket left its last puff
static int			trailTime[MAX_GENTITIES];	// cg.time of it, 0: none yet
static int			trailJump[MAX_GENTITIES];	// its EF_TELEPORT_BIT then
static smokeBlade_t	blades[MAX_CLIENTS][MAX_SABERS][MAX_BLADES];
static smokeCut_t	cuts[SMOKE_MAX_HOLES];
static int			volumeChecked = -1;			// whether the engine has ext.R_SetSmoke

void CG_SmokeInit( void ) {
	memset( puffs, 0, sizeof( puffs ) );
	memset( trailTime, 0, sizeof( trailTime ) );
	memset( blades, 0, sizeof( blades ) );
	memset( cuts, 0, sizeof( cuts ) );
}

// the renderer draws the smoke as a volume
static qboolean Smoke_Volume( void ) {
	if ( volumeChecked < 0 ) {
		char value[8];

		trap->Cvar_VariableStringBuffer( "v_cgameSmoke", value, sizeof( value ) );
		volumeChecked = atoi( value ) == 1 ? 1 : 0;
	}
	return (qboolean)( volumeChecked && trap->ext.R_SetSmoke );
}

// a value the player tunes, kept to what works
static float Smoke_Tune( const vmCvar_t *cv, float least, float most ) {
	return Com_Clamp( least, most, cv->value );
}

// its height against its width when it lies (the renderer draws nothing flatter than 0.15)
static float Smoke_Flat( void ) {
	return Smoke_Tune( &v_smokeHeight, 0.15f, 0.9f );
}

// A hole round the line from start to end. A shot or a blade (owner) keeps the one it has and moves its end along.
static void Smoke_Cut( int owner, const vec3_t start, const vec3_t end, float radius, int life ) {
	smokeCut_t	*cut = NULL, *oldest = NULL;
	int			i;

	for ( i = 0; i < SMOKE_MAX_HOLES; i++ ) {
		smokeCut_t *c = &cuts[i];

		if ( c->life && cg.time - c->birth >= c->life ) {
			c->life = 0;
		}
		if ( owner && c->life && c->owner == owner && cg.time - c->touched < CUT_FRESH_MSEC ) {
			VectorCopy( end, c->end );
			if ( owner >= 10000 && owner < 20000 ) {
				// (a blade: the hole is where the blade is)
				VectorCopy( start, c->start );
			}
			c->touched = c->birth = cg.time;
			return;
		}
		if ( !c->life ) {
			if ( !cut ) {
				cut = c;
			}
		} else if ( !oldest || c->birth + c->life < oldest->birth + oldest->life ) {
			oldest = c;
		}
	}
	if ( !cut ) {
		cut = oldest;
	}
	cut->birth = cut->touched = cg.time;
	cut->life = life;
	cut->owner = owner;
	VectorCopy( start, cut->start );
	VectorCopy( end, cut->end );
	cut->radius = radius;
}

qboolean CG_SmokeSheet( void ) {
	char renderer[64];

	trap->Cvar_VariableStringBuffer( "cl_renderer", renderer, sizeof( renderer ) );
	return (qboolean)!Q_stricmp( renderer, "rd-vulkan-vision" );
}

void CG_SmokeRegister( void ) {

	smokeShader = CG_SmokeSheet() ? trap->R_RegisterShader( "gfx/vision/fb/smoke" ) : 0;
}

// a free puff, else the one nearest to its end
static smokePuff_t *Smoke_Alloc( void ) {
	smokePuff_t	*best = NULL;
	float		bestLeft = 2.0f;
	int			i;

	for ( i = 0; i < SMOKE_PUFFS; i++ ) {
		smokePuff_t	*p = &puffs[i];
		float		left;

		if ( !p->life ) {
			return p;
		}
		left = 1.0f - ( cg.time - p->birth ) / (float)p->life;
		if ( left < bestLeft ) {
			bestLeft = left;
			best = p;
		}
	}
	return best;
}

// under the open sky the wind takes the smoke
static qboolean Smoke_UnderSky( const vec3_t at ) {
	trace_t	tr;
	vec3_t	up;

	VectorSet( up, at[0], at[1], at[2] + 4096.0f );
	CG_Trace( &tr, at, NULL, NULL, up, ENTITYNUM_NONE, MASK_SOLID );
	return (qboolean)( tr.fraction < 1.0f && ( tr.surfaceFlags & SURF_SKY ) );
}

static smokePuff_t *Smoke_Add( const vec3_t at, const vec3_t velocity, float size, float grow, float alpha, int life, int delay ) {
	smokePuff_t *p = Smoke_Alloc();

	memset( p, 0, sizeof( *p ) );
	p->birth = cg.time + delay;
	p->life = life;
	p->fade = (int)( life * SMOKE_FADE );
	VectorCopy( at, p->origin );
	VectorCopy( velocity, p->velocity );
	p->size = size;
	p->grow = grow;
	p->alpha = alpha;
	p->rotation = flrand( 0.0f, 360.0f );
	p->spin = flrand( -14.0f, 14.0f );
	p->nextLook = cg.time + Q_irand( 0, SMOKE_LOOK_MSEC );
	p->flat = 1.0f;
	p->shade = 255;
	VectorSet( p->tint, 1.0f, 1.0f, 1.0f );
	VectorCopy( at, p->home );
	return p;
}

// qtrue: it left smoke with depth there (the caller leaves its cloud of sprites away)
qboolean CG_SmokeBlast( const vec3_t origin, const vec3_t dir, float size ) {
	vec3_t		at, out;
	qboolean	open;
	int			i, count;

	// off what it went off on: there the smoke would sit in the wall, and v_softParticles fades a sprite that close
	// to a surface
	VectorCopy( dir, out );
	if ( VectorNormalize( out ) < 0.1f ) {
		VectorSet( out, 0.0f, 0.0f, 1.0f );
	}
	VectorMA( origin, 20.0f, out, at );

	if ( !v_levelFx.integer || ( !smokeShader && !Smoke_Volume() ) || size <= 0.0f ) {
		return qfalse;
	}
	// Its blast clears the smoke that is there, for a moment. Not as a hole of the volume, as a shot leaves one: its
	// own smoke came in that hole and was seen only as it closed, seconds later (their word 2026-11-26: it shall be
	// there at once).
	{
		const float reach = CUT_BLAST_RADIUS * Com_Clamp( 0.5f, 1.4f, size );

		for ( i = 0; i < SMOKE_PUFFS; i++ ) {
			smokePuff_t	*p = &puffs[i];
			float		by;

			if ( !p->life || cg.time < p->birth ) {
				continue;
			}
			by = 1.0f - Com_Clamp( 0.0f, 1.0f, ( Distance( p->origin, at ) - reach * 0.5f ) / ( reach * 0.5f + p->size ) );
			if ( by > 0.0f ) {
				p->blown = cg.time;
				p->blownBy = by;
			}
		}
	}
	if ( DistanceSquared( at, cg.refdef.vieworg ) > SMOKE_FAR * SMOKE_FAR ) {
		return qfalse;
	}
	// and its own stays: dark at first, it comes as the ball of fire goes, spreads and thins out
	open = Smoke_UnderSky( at );
	count = (int)( BLAST_PUFFS * Smoke_Tune( &v_smokeAmount, 0.0f, 4.0f ) * Com_Clamp( 0.4f, 1.5f, size ) + 0.5f );
	size *= Smoke_Tune( &v_smokeSize, 0.2f, 4.0f );
	for ( i = 0; i < count; i++ ) {
		smokePuff_t	*p;
		vec3_t		spot, velocity;
		trace_t		tr;
		// (it lies on the floor for a good while)
		const int	stays = (int)( Smoke_Tune( &v_smokeTime, 2.0f, 300.0f ) * 1000.0f );
		const int	life = (int)( ( stays + Q_irand( 0, stays / 2 ) ) * ( open ? 0.45f : 1.0f ) );

		// away from the surface, to all sides of that and a little up
		VectorSet( velocity, flrand( -1.0f, 1.0f ), flrand( -1.0f, 1.0f ), flrand( -0.3f, 1.0f ) );
		VectorMA( velocity, 1.1f, out, velocity );
		VectorNormalize( velocity );
		// (not into another wall)
		VectorMA( at, flrand( 10.0f, 50.0f ) * size, velocity, spot );
		CG_Trace( &tr, at, NULL, NULL, spot, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			VectorMA( tr.endpos, 14.0f, tr.plane.normal, spot );
		}
		// The blast throws it out at once, as large as it gets: it is there when the ball of fire goes, and falls
		// (their word 2026-11-26; it came over a second and rose first).
		VectorScale( velocity, flrand( 30.0f, 90.0f ), velocity );
		p = Smoke_Add( spot, velocity, flrand( 16.0f, 22.0f ) * size, flrand( 4.0f, 8.0f ), flrand( 0.26f, 0.38f ), life, 0 );
		// (It takes the light of the room, v_litParticles, as the walls behind it do: a grey as dark as they are was
		// not to be seen against them at all. Smoke is seen because it is lighter than what is behind it.)
		p->shade = (byte)Q_irand( 185, 225 );
		p->tile = Q_irand( 18, 28 );
		p->tiles = SMOKE_TILES * SMOKE_TILES - 1 - p->tile;
		p->open = open;
		p->settles = qtrue;
		p->merges = qtrue;
	}
	return Smoke_Volume();
}

/*
===============
CG_SmokeImpact

A shot hit a wall: the dust it knocks out, as smoke of the volume (weapons/cg_impact.c). qfalse: there is no volume,
the caller makes its sprites as before.
===============
*/
qboolean CG_SmokeImpact( const vec3_t at, const vec3_t normal, float radius, const vec3_t color, int count ) {
	static int	nextWisp;
	vec3_t		smoke;
	int			i;

	if ( !v_levelFx.integer || !Smoke_Volume() ) {
		return qfalse;
	}
	radius *= Smoke_Tune( &v_smokeImpact, 0.0f, 4.0f );
	if ( radius <= 0.0f ) {
		return qtrue;
	}
	// As a shot hits in the films: a puff jumps out of the wall, pale grey more than the colour of the wall, and the
	// hole goes on smoking for a moment, a thin wisp that rises from it.
	VectorSet( smoke, 0.5f * color[0] + 0.42f, 0.5f * color[1] + 0.42f, 0.5f * color[2] + 0.42f );
	for ( i = 0; i < count; i++ ) {
		smokePuff_t	*p;
		vec3_t		spot, velocity;
		int			k;

		// the puff that jumps out: fast at first, the air stops it
		VectorMA( at, 8.0f + i * 5.0f, normal, spot );
		VectorScale( normal, flrand( 70.0f, 130.0f ), velocity );
		for ( k = 0; k < 3; k++ ) {
			spot[k] += flrand( -radius, radius ) * 0.15f;
			velocity[k] += flrand( -22.0f, 22.0f );
		}
		p = Smoke_Add( spot, velocity, radius * flrand( 0.2f, 0.28f ), radius * 0.6f, flrand( 0.32f, 0.44f ),
			(int)( Q_irand( 2600, 3800 ) * Smoke_Tune( &v_smokeImpactTime, 0.2f, 10.0f ) ), 0 );
		p->rise = flrand( 5.0f, 11.0f );
		p->merges = qtrue;
		VectorCopy( smoke, p->tint );
		p->tile = Q_irand( 10, 20 );
		p->tiles = SMOKE_TILES * SMOKE_TILES - 1 - p->tile;
	}
	// the wisp from the hole: small puffs one after the other, each rising where the one before went (not from every
	// hit of a burst: a repeater would fill the place with them)
	if ( cg.time < nextWisp ) {
		return qtrue;
	}
	nextWisp = cg.time + 170;
	for ( i = 0; i < 4; i++ ) {
		smokePuff_t	*p;
		vec3_t		spot, velocity;

		VectorMA( at, 7.0f, normal, spot );
		VectorScale( normal, flrand( 6.0f, 14.0f ), velocity );
		velocity[0] += flrand( -5.0f, 5.0f );
		velocity[1] += flrand( -5.0f, 5.0f );
		velocity[2] += flrand( 16.0f, 26.0f );
		p = Smoke_Add( spot, velocity, radius * flrand( 0.12f, 0.17f ), radius * 0.32f, flrand( 0.26f, 0.36f ),
			(int)( Q_irand( 2200, 3200 ) * Smoke_Tune( &v_smokeImpactTime, 0.2f, 10.0f ) ), 180 + i * 300 + Q_irand( 0, 120 ) );
		p->rise = flrand( 10.0f, 18.0f );
		p->merges = qtrue;
		VectorCopy( smoke, p->tint );
		p->tile = Q_irand( 10, 20 );
		p->tiles = SMOKE_TILES * SMOKE_TILES - 1 - p->tile;
	}
	return qtrue;
}

// the rockets in flight leave a trail
static void Smoke_Trails( void ) {
	int i;

	for ( i = 0; i < cg.snap->numEntities; i++ ) {
		const entityState_t	*es = &cg.snap->entities[i];
		const centity_t		*cent = &cg_entities[es->number];
		smokePuff_t			*p;
		vec3_t				velocity;
		float				moved;

		if ( es->eType != ET_MISSILE ) {
			continue;
		}
		// every shot bores a way through the smoke, which closes behind it
		Smoke_Cut( 1 + es->number, cent->lerpOrigin, cent->lerpOrigin, CUT_SHOT_RADIUS, CUT_SHOT_MSEC );
		if ( es->weapon != WP_ROCKET_LAUNCHER ) {
			continue;
		}
		// A rocket that was not seen for a while starts again where it is; one seen for the first time right after it
		// was fired (it is a snapshot or two old then) starts where it was fired from, so the trail begins at the tube.
		if ( !trailTime[es->number] || cg.time - trailTime[es->number] > 400 ) {
			if ( cg.time - es->pos.trTime < 400 && Distance( es->pos.trBase, cent->lerpOrigin ) < 600.0f ) {
				VectorCopy( es->pos.trBase, trailAt[es->number] );
			} else {
				VectorCopy( cent->lerpOrigin, trailAt[es->number] );
			}
			trailTime[es->number] = cg.time;
			trailJump[es->number] = es->eFlags & EF_TELEPORT_BIT;
		}
		// one that was moved at once, through a portal of the portal gun (game/portal/g_portal.c flips the bit): the
		// trail goes on from where it is, not across the room between the two portals
		if ( ( es->eFlags & EF_TELEPORT_BIT ) != trailJump[es->number] ) {
			trailJump[es->number] = es->eFlags & EF_TELEPORT_BIT;
			VectorCopy( cent->lerpOrigin, trailAt[es->number] );
		}
		// a puff every TRAIL_STEP units of the way since the last one
		while ( ( moved = Distance( cent->lerpOrigin, trailAt[es->number] ) ) >= TRAIL_STEP ) {
			vec3_t step;

			if ( DistanceSquared( cent->lerpOrigin, cg.refdef.vieworg ) < SMOKE_FAR * SMOKE_FAR ) {
				VectorSet( velocity, flrand( -6.0f, 6.0f ), flrand( -6.0f, 6.0f ), flrand( -3.0f, 7.0f ) );
				p = Smoke_Add( trailAt[es->number], velocity, flrand( 9.0f, 13.0f ), flrand( 14.0f, 20.0f ), flrand( 0.2f, 0.3f ),
					TRAIL_LIFE + Q_irand( 0, 800 ), 0 );
				p->rise = flrand( 2.0f, 6.0f );
				p->shade = (byte)Q_irand( 215, 245 );
				p->tile = Q_irand( 6, 14 );
				p->tiles = SMOKE_TILES * SMOKE_TILES - 1 - p->tile;
				p->open = qfalse;
			}
			VectorSubtract( cent->lerpOrigin, trailAt[es->number], step );
			VectorMA( trailAt[es->number], TRAIL_STEP / moved, step, trailAt[es->number] );
			trailTime[es->number] = cg.time;
		}
		trailTime[es->number] = cg.time;
	}
}

// whether there is smoke at a place, or within margin of it
static qboolean Smoke_Near( const vec3_t at, float margin ) {
	int i;

	for ( i = 0; i < SMOKE_PUFFS; i++ ) {
		const smokePuff_t *p = &puffs[i];

		if ( p->life && cg.time >= p->birth && Distance( p->origin, at ) < p->size * 1.35f + margin ) {
			return qtrue;
		}
	}
	return qfalse;
}

// The players: who walks through smoke leaves a way through it behind him that closes again, as a shot does, and a
// blade swung through it cuts it open. Nothing shoves the smoke about: a push, a body or a blade moved whole clouds
// before, which looked like balls rolling off (their word 2026-11-26).
static void Smoke_Players( void ) {
	static vec3_t	wayFrom[MAX_CLIENTS];		// where the piece of his way he is on began
	static int		wayTime[MAX_CLIENTS], wayCount[MAX_CLIENTS];
	int				i, s, b;

	for ( i = 0; i < cg.snap->numEntities + 1; i++ ) {
		// (the last one is the player of this client, who is not among the entities of the snapshot)
		const qboolean		own = (qboolean)( i == cg.snap->numEntities );
		const centity_t		*cent = own ? &cg_entities[cg.predictedPlayerState.clientNum] : &cg_entities[cg.snap->entities[i].number];
		const entityState_t	*es = own ? &cent->currentState : &cg.snap->entities[i];
		clientInfo_t		*ci;
		vec3_t				middle;
		int					n;

		if ( es->eType != ET_PLAYER || es->number < 0 || es->number >= MAX_CLIENTS || ( es->eFlags & EF_DEAD ) ) {
			continue;
		}
		n = es->number;
		ci = &cgs.clientinfo[n];
		// his body: a hole round the piece of his way he is on, which stays behind and closes when he has gone on
		VectorCopy( own ? cg.predictedPlayerState.origin : cent->lerpOrigin, middle );
		middle[2] += 8.0f;
		if ( Smoke_Near( middle, CUT_BODY_RADIUS ) ) {
			if ( !wayTime[n] || cg.time - wayTime[n] > CUT_FRESH_MSEC || Distance( wayFrom[n], middle ) > CUT_BODY_STEP ) {
				VectorCopy( middle, wayFrom[n] );
				wayCount[n]++;
			}
			wayTime[n] = cg.time;
			Smoke_Cut( 20000 + n * 8 + ( wayCount[n] & 7 ), wayFrom[n], middle, CUT_BODY_RADIUS, CUT_BODY_MSEC );
		} else {
			wayTime[n] = 0;
		}
		// his blades
		for ( s = 0; s < MAX_SABERS; s++ ) {
			for ( b = 0; b < MAX_BLADES; b++ ) {
				const bladeInfo_t	*blade = &ci->saber[s].blade[b];
				smokeBlade_t		*was = &blades[n][s][b];
				vec3_t				tip, mid, midWas, swing;

				if ( es->weapon != WP_SABER || !blade->active || blade->length < 8.0f || b >= ci->saber[s].numBlades ) {
					was->valid = qfalse;
					continue;
				}
				VectorMA( blade->muzzlePoint, blade->length, blade->muzzleDir, tip );
				if ( was->valid && cg.time - was->time < 200 && cg.time > was->time ) {
					const float seconds = ( cg.time - was->time ) * 0.001f;

					// how fast its middle moves
					VectorAdd( blade->muzzlePoint, tip, mid );
					VectorAdd( was->base, was->tip, midWas );
					VectorSubtract( mid, midWas, swing );
					VectorScale( swing, 0.5f / seconds, swing );
					if ( VectorLengthSquared( swing ) > BLADE_FAST * BLADE_FAST ) {
						Smoke_Cut( 10000 + n * 16 + s * 8 + b, blade->muzzlePoint, tip, CUT_BLADE_RADIUS, CUT_BLADE_MSEC );
					}
				}
				was->valid = qtrue;
				was->time = cg.time;
				VectorCopy( blade->muzzlePoint, was->base );
				VectorCopy( tip, was->tip );
			}
		}
	}
}

// the Force Pushes and Pulls that began this frame clear their way through the smoke, which closes again
static void Smoke_Pushes( void ) {
	const forcePush_t	*list;
	const int			count = CG_ForcePushes( &list );
	int					k;

	for ( k = 0; k < count; k++ ) {
		vec3_t near_, far_;

		VectorMA( list[k].from, 30.0f, list[k].forward, near_ );
		VectorMA( list[k].from, PUSH_REACH * 0.85f, list[k].forward, far_ );
		Smoke_Cut( 0, near_, far_, CUT_PUSH_RADIUS, CUT_PUSH_MSEC );
	}
}

// a moves that share of the way to b
static void Smoke_Blend( vec3_t a, const vec3_t b, float share ) {
	vec3_t	way;

	VectorSubtract( b, a, way );
	VectorMA( a, share, way, a );
}

// What two puffs cover together, as the radius of one that covers as much, and how much of the smaller one lies in
// the other (0 to 1). a, b: their radii, apart: their middles. Two cushions of one height (flat): the area of two
// discs; else the room of two balls.
static float Smoke_Both( float a, float b, float apart, qboolean flat, float *inside ) {
	const float	large = Q_max( a, b ), small = Q_min( a, b );
	float		shared;

	if ( apart >= a + b ) {
		*inside = 0.0f;
		return flat ? sqrtf( a * a + b * b ) : cbrtf( a * a * a + b * b * b );
	}
	if ( apart <= large - small ) {
		*inside = 1.0f;
		return large;
	}
	if ( flat ) {
		// (the lens two discs share)
		shared = a * a * acosf( Com_Clamp( -1.0f, 1.0f, ( apart * apart + a * a - b * b ) / ( 2.0f * apart * a ) ) )
			+ b * b * acosf( Com_Clamp( -1.0f, 1.0f, ( apart * apart + b * b - a * a ) / ( 2.0f * apart * b ) ) )
			- 0.5f * sqrtf( Q_max( 0.0f, ( -apart + a + b ) * ( apart + a - b ) * ( apart - a + b ) * ( apart + a + b ) ) );
		*inside = Com_Clamp( 0.0f, 1.0f, shared / ( M_PI * small * small ) );
		return sqrtf( Q_max( large * large, a * a + b * b - shared / M_PI ) );
	}
	// (the lens two balls share)
	shared = M_PI * Square( a + b - apart ) * ( apart * apart + 2.0f * apart * ( a + b ) - 3.0f * Square( a - b ) ) / ( 12.0f * apart );
	*inside = Com_Clamp( 0.0f, 1.0f, shared / ( 4.0f / 3.0f * M_PI * small * small * small ) );
	return cbrtf( Q_max( large * large * large, a * a * a + b * b * b - shared * 3.0f / ( 4.0f * M_PI ) ) );
}

// Puffs that lie in each other become one that covers what both did: the smoke that has lain down, the heap of a
// blast in the air once its first moment is over, and the dust of shots that hit one place. A ray of the renderer
// goes through every puff on its way at each of its steps, that is what the smoke costs (shaders/smoke.frag), and a
// ray counts only so many of them. The one covers what the two covered together and is as thick as they were where
// they lay in each other (Smoke_Both): it is the puffs over each other that cost, and as large as both added up the
// one lay over its neighbours as much as the two had, and a blast became one bank of fog through the hall. The one
// grows and moves over a moment as the other goes, so nothing jumps.
static void Smoke_Merge( void ) {
	const float	lies = Smoke_Flat() + 0.02f;
	const float	most = MERGE_MAX_SIZE * Smoke_Tune( &v_smokeSize, 0.2f, 4.0f );
	// (how thick a blob is for what a puff has, CG_SmokeFrame(); the renderer takes no blob for thicker than 0.96)
	const float	thick = 3.2f * Smoke_Tune( &v_smokeDensity, 0.05f, 4.0f );
	int			i, k;

	if ( !v_smokeMerge.integer ) {
		return;
	}
	for ( i = 0; i < SMOKE_PUFFS; i++ ) {
		smokePuff_t *a = &puffs[i];

		if ( !a->life || !a->merges || a->taken || cg.time - a->birth < MERGE_AIR_MSEC ) {
			continue;
		}
		for ( k = i + 1; k < SMOKE_PUFFS; k++ ) {
			smokePuff_t	*b = &puffs[k];
			vec3_t		at, to;
			float		sizeA, sizeB, apart, share, both, inside, thickA, thickB;
			qboolean	lying;

			if ( !b->life || !b->merges || b->taken || b->settles != a->settles || cg.time - b->birth < MERGE_AIR_MSEC ) {
				continue;
			}
			// (as large as they will be, where they will be)
			sizeA = a->size + a->gain;
			sizeB = b->size + b->gain;
			if ( sizeA >= most ) {
				break;
			}
			if ( sizeB >= most ) {
				continue;
			}
			VectorAdd( a->origin, a->shift, at );
			VectorAdd( b->origin, b->shift, to );
			apart = Distance( at, to );
			lying = (qboolean)( a->settles && a->flat <= lies && b->flat <= lies );
			if ( apart > ( lying ? MERGE_NEAR : MERGE_NEAR_AIR ) * Q_max( sizeA, sizeB ) * 1.35f ) {
				continue;
			}
			// the larger one stays: its billows are the ones seen there
			if ( sizeB > sizeA ) {
				const smokePuff_t	swap = *a;
				const float			size = sizeA;

				*a = *b;
				*b = swap;
				sizeA = sizeB;
				sizeB = size;
				VectorAdd( a->origin, a->shift, at );
				VectorAdd( b->origin, b->shift, to );
			}
			share = lying ? sizeB * sizeB / ( sizeA * sizeA + sizeB * sizeB )
				: sizeB * sizeB * sizeB / ( sizeA * sizeA * sizeA + sizeB * sizeB * sizeB );
			both = Smoke_Both( sizeA, sizeB, apart / 1.35f, lying, &inside );
			a->gain += both - sizeA;
			VectorSubtract( to, at, to );
			VectorMA( a->shift, share, to, a->shift );
			Smoke_Blend( a->velocity, b->velocity, share );
			Smoke_Blend( a->tint, b->tint, share );
			a->shade = (byte)( a->shade + ( b->shade - a->shade ) * share );
			// (where the two lay in each other what one left free the other took its part of)
			thickA = Q_min( a->alpha * thick, 0.96f );
			thickB = Q_min( b->alpha * thick, 0.96f );
			a->alpha = Q_max( a->alpha, ( thickA + inside * thickB * ( 1.0f - thickA ) ) / thick );
			// it lasts as long as the one that had longer to go
			if ( b->birth + b->life > a->birth + a->life ) {
				a->life = b->birth + b->life - a->birth;
				a->fade = b->fade;
			}
			b->taken = cg.time;
		}
	}
}

// The light at a puff, looked up now and then: the smoke takes it as a pale grey would. (The sprites get theirs from
// the renderer, v_litParticles.)
static void Smoke_Light( smokePuff_t *p ) {
	vec3_t	ambient, directed, dir;
	int		c;

	if ( cg.time < p->nextLight ) {
		return;
	}
	p->nextLight = cg.time + 350 + Q_irand( 0, 100 );
	trap->R_LightForPoint( p->origin, ambient, directed, dir );
	for ( c = 0; c < 3; c++ ) {
		// (smoke scatters the light from all sides: all of the ambient, a good part of the directed)
		const float light = ( ambient[c] + directed[c] * 0.7f ) / 255.0f;

		// (nearly all of the light comes back from smoke, it is lighter than the walls behind it; the renderer takes
		// most of the colour of the light out and darkens what lies under more smoke)
		p->color[c] = Com_Clamp( 0.06f, 1.0f, light * 0.95f ) * ( p->shade / 255.0f ) * p->tint[c];
	}
}

void CG_SmokeFrame( void ) {
	const float		dt = Com_Clamp( 0.0f, 0.1f, cg.frametime * 0.001f );
	const qboolean	volume = Smoke_Volume();
	static smokeVolume_t	out;
	float			scores[SMOKE_MAX_BLOBS];
	int				i;

	out.numBlobs = out.numHoles = 0;

	if ( !v_levelFx.integer || ( !smokeShader && !volume ) || !cg.snap ) {
		if ( volume ) {
			trap->ext.R_SetSmoke( NULL );
		}
		return;
	}
	{
		static int nextMerge;

		if ( cg.time >= nextMerge || nextMerge - cg.time > MERGE_MSEC ) {
			nextMerge = cg.time + MERGE_MSEC;
			Smoke_Merge();
		}
	}
	Smoke_Trails();
	Smoke_Pushes();
	Smoke_Players();

	for ( i = 0; i < SMOKE_PUFFS; i++ ) {
		smokePuff_t	*p = &puffs[i];
		refEntity_t	re;
		float		t, speed, alpha, young;

		if ( !p->life || cg.time < p->birth ) {
			continue;
		}
		t = ( cg.time - p->birth ) / (float)p->life;
		if ( t >= 1.0f ) {
			p->life = 0;
			continue;
		}
		// it moves: the air stops it, it drifts up while it is warm, the wind has it under the sky
		speed = VectorLength( p->velocity );
		if ( speed > SMOKE_MAX_SPEED ) {
			VectorScale( p->velocity, SMOKE_MAX_SPEED / speed, p->velocity );
		}
		VectorScale( p->velocity, expf( -SMOKE_DRAG * dt ), p->velocity );
		// (young: the first third of the time it had when it came)
		young = 1.0f - Com_Clamp( 0.0f, 1.0f, ( cg.time - p->birth ) / ( p->fade / SMOKE_FADE / 3.0f + 1.0f ) );
		if ( p->gain != 0.0f || p->shift[0] != 0.0f || p->shift[1] != 0.0f || p->shift[2] != 0.0f ) {
			// it took others in: it grows to cover them and moves to lie between them (its billows stay where they
			// are: they do not go with its middle)
			const float	part = 1.0f - expf( -MERGE_OVER * dt );
			vec3_t		step;

			p->size += p->gain * part;
			p->gain = fabsf( p->gain ) > 0.01f ? p->gain * ( 1.0f - part ) : 0.0f;
			VectorScale( p->shift, part, step );
			VectorAdd( p->origin, step, p->origin );
			VectorAdd( p->home, step, p->home );
			if ( VectorLengthSquared( p->shift ) > 0.01f ) {
				VectorSubtract( p->shift, step, p->shift );
			} else {
				VectorClear( p->shift );
			}
		}
		p->origin[2] += p->rise * young * dt;
		if ( p->open ) {
			p->origin[0] += 22.0f * dt;
			p->origin[1] += 9.0f * dt;
			p->origin[2] += 6.0f * dt;
		}
		// The walls hold it. Smoke that drifts looks now and then where it is going; smoke that was pushed looks every
		// frame: a Force Push that pointed a little down carried a whole cloud through the floor between two looks.
		if ( speed > SMOKE_FAST ) {
			vec3_t	to;
			trace_t	tr;

			VectorMA( p->origin, dt, p->velocity, to );
			CG_Trace( &tr, p->origin, NULL, NULL, to, ENTITYNUM_NONE, MASK_SOLID );
			if ( tr.fraction < 1.0f && !tr.startsolid ) {
				// it stops a little in front of the wall and goes on along it
				VectorMA( tr.endpos, 3.0f, tr.plane.normal, p->origin );
				VectorMA( p->velocity, -DotProduct( p->velocity, tr.plane.normal ), tr.plane.normal, p->velocity );
				VectorScale( p->velocity, 0.7f, p->velocity );
			} else {
				VectorCopy( to, p->origin );
			}
		} else {
			if ( cg.time >= p->nextLook ) {
				vec3_t	ahead;
				trace_t	tr;

				p->nextLook = cg.time + SMOKE_LOOK_MSEC;
				VectorMA( p->origin, 0.35f, p->velocity, ahead );
				if ( DistanceSquared( ahead, p->origin ) > 4.0f ) {
					CG_Trace( &tr, p->origin, NULL, NULL, ahead, ENTITYNUM_NONE, MASK_SOLID );
					if ( tr.fraction < 1.0f && !tr.startsolid ) {
						// along the wall
						VectorMA( p->velocity, -DotProduct( p->velocity, tr.plane.normal ), tr.plane.normal, p->velocity );
						VectorScale( p->velocity, 0.6f, p->velocity );
					}
				}
			}
			VectorMA( p->origin, dt, p->velocity, p->origin );
		}
		// it settles: it sinks to the floor under it and lies down there, flatter and wider
		if ( p->settles ) {
			const float lying = Com_Clamp( 0.0f, 1.0f, ( cg.time - p->birth - Smoke_Tune( &v_smokeSinkDelay, 0.0f, 60000.0f ) )
				/ Smoke_Tune( &v_smokeSinkTime, 100.0f, 120000.0f ) );
			const float lowest = Smoke_Flat();
			float rest;

			if ( cg.time >= p->nextFloor ) {
				vec3_t	down;
				trace_t	tr;

				p->nextFloor = cg.time + SETTLE_LOOK_MSEC + ( i & 63 ) * 3;
				VectorSet( down, p->origin[0], p->origin[1], p->origin[2] - 700.0f );
				CG_Trace( &tr, p->origin, NULL, NULL, down, ENTITYNUM_NONE, MASK_SOLID );
				p->floorZ = tr.endpos[2];
			}
			{
				const float	flat = 1.0f - ( 1.0f - lowest ) * lying * lying * ( 3.0f - 2.0f * lying );

				// (wider by as much as it got flatter)
				p->size += p->size * Smoke_Tune( &v_smokeSpread, 0.0f, 2.0f ) * ( p->flat - flat ) / ( 1.0f - lowest );
				p->flat = flat;
			}
			// (where its lower side lies on the floor)
			rest = p->floorZ + p->size * 1.35f * p->flat * SETTLE_REST;
			if ( lying > 0.0f && p->origin[2] > rest ) {
				p->origin[2] -= Q_min( Smoke_Tune( &v_smokeSinkSpeed, 1.0f, 1000.0f ) * Q_min( lying * 3.0f, 1.0f ) * dt, p->origin[2] - rest );
			}
		}
		// it spreads, fast at first; smoke that is torn along thins out
		p->size += p->grow * expf( -SMOKE_GROW_FADE * ( cg.time - p->birth ) * 0.001f ) * dt;
		p->rotation += p->spin * dt;
		// there at once, a long time there, out slowly
		alpha = p->alpha * Com_Clamp( 0.0f, 1.0f, ( cg.time - p->birth ) / (float)SMOKE_COME_MSEC )
			* Com_Clamp( 0.0f, 1.0f, ( p->birth + p->life - cg.time ) / (float)Q_max( p->fade, 1 ) );
		if ( p->taken ) {
			// another one took it in
			if ( cg.time - p->taken >= MERGE_GONE_MSEC ) {
				p->life = 0;
				continue;
			}
			alpha *= 1.0f - ( cg.time - p->taken ) / (float)MERGE_GONE_MSEC;
		}
		if ( p->blown ) {
			// a blast cleared it: gone at first, then it closes again
			const float age = ( cg.time - p->blown ) / (float)CUT_BLAST_MSEC;

			if ( age >= 1.0f ) {
				p->blown = 0;
			} else if ( age >= 0.0f ) {
				alpha *= 1.0f - p->blownBy * ( age < 0.35f ? 1.0f : 1.0f - ( age - 0.35f ) / 0.65f );
			}
		}
		// (a moment late once it is there: a puff that was going and takes one in has longer again)
		if ( cg.time - p->birth < 300 ) {
			p->shown = alpha;
		} else {
			p->shown += ( alpha - p->shown ) * ( 1.0f - expf( -12.0f * dt ) );
		}
		alpha = p->shown;
		if ( alpha <= 0.004f ) {
			continue;
		}
		if ( volume ) {
			// a blob of the volume: the most there is to see of first, when they are more than it takes
			const float	score = alpha * p->size * p->size / ( 1.0f + Distance( p->origin, cg.refdef.vieworg ) / 600.0f );
			int			slot = out.numBlobs;
			smokeBlob_t	*blob;

			if ( slot == SMOKE_MAX_BLOBS ) {
				int k;

				slot = 0;
				for ( k = 1; k < SMOKE_MAX_BLOBS; k++ ) {
					if ( scores[k] < scores[slot] ) {
						slot = k;
					}
				}
				if ( score <= scores[slot] ) {
					continue;
				}
			} else {
				out.numBlobs++;
			}
			Smoke_Light( p );
			scores[slot] = score;
			blob = &out.blobs[slot];
			VectorCopy( p->origin, blob->origin );
			// (the blob ends where the picture of a puff ended, and is thick where that was)
			blob->radius = p->size * 1.35f;
			VectorCopy( p->color, blob->color );
			blob->density = alpha * 3.2f * Smoke_Tune( &v_smokeDensity, 0.05f, 4.0f );
			blob->flat = p->flat;
			VectorSubtract( p->origin, p->home, blob->drift );
			continue;
		}
		memset( &re, 0, sizeof( re ) );
		re.reType = RT_SPRITE;
		VectorCopy( p->origin, re.origin );
		re.radius = p->size;
		re.rotation = p->rotation;
		re.customShader = smokeShader;
		re.renderfx = RF_FLIPBOOK;
		re.frame = RF_FLIPBOOK_FRAME( p->tile + (int)( t * p->tiles ), SMOKE_TILES, SMOKE_TILES );
		re.shaderRGBA[0] = re.shaderRGBA[1] = re.shaderRGBA[2] = p->shade;
		re.shaderRGBA[3] = (byte)( alpha * 255.0f );
		trap->R_AddRefEntityToScene( &re );
	}
	if ( !volume ) {
		return;
	}
	// the holes: clear at first, then they close
	for ( i = 0; i < SMOKE_MAX_HOLES; i++ ) {
		const smokeCut_t	*c = &cuts[i];
		smokeHole_t			*hole;
		float				age;

		if ( !c->life || cg.time - c->birth >= c->life ) {
			continue;
		}
		age = ( cg.time - c->birth ) / (float)c->life;
		hole = &out.holes[out.numHoles++];
		VectorCopy( c->start, hole->start );
		VectorCopy( c->end, hole->end );
		hole->radius = c->radius;
		hole->strength = age < 0.35f ? 1.0f : 1.0f - ( age - 0.35f ) / 0.65f;
	}
	out.time = cg.time * 0.001f;
	trap->ext.R_SetSmoke( &out );
}
