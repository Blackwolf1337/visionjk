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

// cg_water.c -- the water gun, the second toy of issue #9 that only players of this tree see. Hold +v_water
// (Controls, Extras): a jet of water leaves the hand where the player looks. The jet is drops that fly on their
// own, each drawn as a piece of clear water to the drop before it, so it bends as it falls; on rd-vulkan-vision
// it bends the picture behind it too (screenfx/cg_screenfx.c). Where it hits it splashes
// (effects/vision/water_splash.efx); on a floor it makes a puddle that grows while the water comes and dries
// after a while, on a wall a wet patch. Who is hit in the face sees drops run down his view.
//
// The others hear of the jet the way they hear of a snowball (toys/cg_toys.c): a TOY_WATER with where the water
// leaves the gun and how fast, every WATER_SEND_MSEC while it sprays, and their cgame sprays from there for
// WATER_JET_MSEC. So the jet of somebody else turns in steps of that time, the own one with every frame. Nothing
// of it is in the game state of the server. Not from JA+, see docs/japlus/toys.md.

#include "cgame/cg_local.h"
#include "cgame/toys/cg_toys.h"

#define WATER_DROPS			384			// drops in the air at once
#define WATER_PUDDLES		24			// puddles and wet patches at once
#define WATER_DROP_MSEC		18			// a jet lets go of a drop this often
#define WATER_SPEED			720.0f		// units a second the water leaves the gun with
#define WATER_SPREAD		0.012f		// of the speed, each drop a little to one side
#define WATER_GRAVITY		800.0f
#define WATER_LIFE_MSEC		2500		// a drop that hit nothing by then is gone
#define WATER_OWN_MSEC		120			// so long a drop goes through the one who sprays it
#define WATER_SEND_MSEC		260			// the jet goes to the others this often (TOY_PER_SECOND)
#define WATER_JET_MSEC		340			// a jet that was heard sprays this long after it was heard
#define WATER_LINK			48.0f		// two drops further apart than this are no piece of the jet
#define WATER_SPLASH_MSEC	60			// a jet splashes this often at most,
#define WATER_SOUND_MSEC	200			//	and makes a sound this often
#define WATER_WET_MSEC		700			// the own face gets wet again at most this often
#define WATER_DRY_MSEC		20000		// a puddle dries in this time, the second half of it it fades
#define WATER_PUDDLE_MAX	36.0f		// the radius a puddle grows to at most
#define WATER_PATCH_MAX		18.0f		//	and a wet patch on a wall
#define WATER_REFRACTS		12			// pieces of the jets that bend the picture, in one frame

typedef struct {
	qboolean	active;
	int			owner;
	int			born;			// cg.time it left the gun
	int			time;			// cg.time it was moved to last
	int			prev;			// the drop before it in its jet, -1: none
	int			prevBorn;		//	and when that one was born, so a slot used anew is not taken for it
	vec3_t		origin;
	vec3_t		velocity;
} waterDrop_t;

typedef struct {
	int			until;			// cg.time it sprays until, 0: not at all
	int			nextDrop;		// cg.time of the next drop
	int			lastDrop;		// the drop it let go of last, -1: none
	int			splashTime, soundTime;
	vec3_t		origin, velocity;
} waterJet_t;

typedef struct {
	qboolean	active;
	qboolean	floor;			// a puddle; else a wet patch on a wall
	int			wetTime;		// cg.time water came last
	float		size, angle;
	vec3_t		origin, normal;
} waterPuddle_t;

static struct {
	waterDrop_t		drops[WATER_DROPS];
	waterJet_t		jets[MAX_CLIENTS];
	waterPuddle_t	puddles[WATER_PUDDLES];
	int				nextSlot;
	qboolean		spraying;		// +v_water is down
	int				sendTime;
	int				wetTime;
	qhandle_t		streamShader, dropShader, wetShader, puddleShader;
	int				splashFx;
	sfxHandle_t		splashSound[3], faceSound;
	qboolean		registered;
} water;

static void Water_Register( void ) {
	if ( water.registered )
		return;
	water.registered = qtrue;
	water.streamShader = trap->R_RegisterShader( "gfx/vision/toys/waterstream" );
	water.dropShader = trap->R_RegisterShader( "gfx/vision/toys/waterdrop" );
	water.wetShader = trap->R_RegisterShader( "gfx/vision/toys/wet" );
	water.puddleShader = trap->R_RegisterShader( "gfx/vision/toys/puddle" );
	water.splashFx = trap->FX_RegisterEffect( "effects/vision/water_splash.efx" );
	water.splashSound[0] = trap->S_RegisterSound( "sound/player/footsteps/water_run1.mp3" );
	water.splashSound[1] = trap->S_RegisterSound( "sound/player/footsteps/water_run2.mp3" );
	water.splashSound[2] = trap->S_RegisterSound( "sound/player/footsteps/water_run3.mp3" );
	water.faceSound = trap->S_RegisterSound( "sound/player/bodyfall_water1.mp3" );
}

void CG_WaterInit( void ) {
	int i;

	memset( &water, 0, sizeof( water ) );
	for ( i = 0; i < MAX_CLIENTS; i++ )
		water.jets[i].lastDrop = -1;
}

// the jet of somebody else, from the game module or the masterserver: it sprays from there for a moment
void CG_WaterHeard( int client, const vec3_t origin, const vec3_t velocity ) {
	waterJet_t *jet = &water.jets[client];

	Water_Register();
	if ( !jet->until || cg.time >= jet->until )
		jet->nextDrop = cg.time;
	jet->until = cg.time + WATER_JET_MSEC;
	VectorCopy( origin, jet->origin );
	VectorCopy( velocity, jet->velocity );
}

void CG_WaterDown_f( void ) {
	water.spraying = qtrue;
}

void CG_WaterUp_f( void ) {
	water.spraying = qfalse;
}

// where the water of the own gun leaves the hand and how fast: from the right hand where the player looks
static void Water_Own( vec3_t origin, vec3_t velocity ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	vec3_t				forward, right, up;

	AngleVectors( cg.refdef.viewangles, forward, right, up );
	VectorCopy( ps->origin, origin );
	origin[2] += ps->viewheight;
	VectorMA( origin, 12.0f, forward, origin );
	VectorMA( origin, 7.0f, right, origin );
	VectorMA( origin, -6.0f, up, origin );
	VectorScale( forward, WATER_SPEED, velocity );
	VectorMA( velocity, 0.5f, ps->velocity, velocity );
}

/*
=================
CG_WaterFrame

Once a frame: the own gun sprays while +v_water is down, and tells the others every WATER_SEND_MSEC
=================
*/
void CG_WaterFrame( void ) {
	waterJet_t	*jet;
	vec3_t		origin, velocity;
	toyThrow_t	toy;
	int			i;

	if ( !water.spraying || !CG_ToyCanUse() || cg.clientNum < 0 || cg.clientNum >= MAX_CLIENTS )
		return;
	Water_Register();
	Water_Own( origin, velocity );
	jet = &water.jets[cg.clientNum];
	if ( !jet->until || cg.time >= jet->until )
		jet->nextDrop = cg.time;
	jet->until = cg.time + 50;
	VectorCopy( origin, jet->origin );
	VectorCopy( velocity, jet->velocity );
	if ( water.sendTime && cg.time - water.sendTime < WATER_SEND_MSEC && cg.time >= water.sendTime )
		return;
	water.sendTime = cg.time ? cg.time : 1;
	toy.kind = TOY_WATER;
	for ( i = 0; i < 3; i++ ) {
		toy.origin[i] = (int)origin[i];
		toy.velocity[i] = Com_Clampi( -TOY_MAX_SPEED, TOY_MAX_SPEED, (int)velocity[i] );
	}
	if ( BG_ToyValid( &toy ) )
		CG_ToySend( &toy );
}

// a drop leaves the gun of jet, at cg.time minus age milliseconds
static void Water_Drop( int owner, waterJet_t *jet, int age ) {
	waterDrop_t	*drop = &water.drops[water.nextSlot];
	const int	slot = water.nextSlot;
	const float	t = age * 0.001f;
	int			i;

	water.nextSlot = ( water.nextSlot + 1 ) % WATER_DROPS;
	memset( drop, 0, sizeof( *drop ) );
	drop->active = qtrue;
	drop->owner = owner;
	drop->born = cg.time - age;
	drop->time = cg.time;
	for ( i = 0; i < 3; i++ )
		drop->velocity[i] = jet->velocity[i] + Q_flrand( -1.0f, 1.0f ) * WATER_SPEED * WATER_SPREAD;
	// (it left the gun a little before this frame: so the drops of a frame lie apart as they would)
	VectorMA( jet->origin, t, drop->velocity, drop->origin );
	drop->origin[2] -= 0.5f * WATER_GRAVITY * t * t;
	drop->velocity[2] -= WATER_GRAVITY * t;
	drop->prev = -1;
	if ( jet->lastDrop >= 0 && water.drops[jet->lastDrop].active && water.drops[jet->lastDrop].owner == owner ) {
		drop->prev = jet->lastDrop;
		drop->prevBorn = water.drops[jet->lastDrop].born;
	}
	jet->lastDrop = slot;
}

// water came to a floor (a puddle) or a wall (a wet patch) here: the one near grows, else there is a new one
static void Water_Puddle( const vec3_t origin, const vec3_t normal ) {
	const qboolean	floor = (qboolean)( normal[2] > 0.7f );
	waterPuddle_t	*p = NULL, *oldest = &water.puddles[0];
	int				i;

	for ( i = 0; i < WATER_PUDDLES && !p; i++ ) {
		waterPuddle_t *q = &water.puddles[i];

		if ( q->active && q->floor == floor && DistanceSquared( q->origin, origin ) < q->size * q->size * 0.8f
			&& DotProduct( q->normal, normal ) > 0.9f ) {
			p = q;
		} else if ( !q->active || q->wetTime < oldest->wetTime ) {
			oldest = q;
		}
	}
	if ( !p ) {
		p = oldest;
		memset( p, 0, sizeof( *p ) );
		p->active = qtrue;
		p->floor = floor;
		p->size = floor ? 6.0f : 5.0f;
		p->angle = Q_flrand( 0.0f, 360.0f );
		VectorCopy( origin, p->origin );
		VectorCopy( normal, p->normal );
	}
	// (a puddle grows slower the larger it is)
	p->size = Q_min( floor ? WATER_PUDDLE_MAX : WATER_PATCH_MAX, p->size + 0.6f * 12.0f / p->size );
	p->wetTime = cg.time ? cg.time : 1;
}

// a drop hit something at tr
static void Water_Hit( const waterDrop_t *drop, const trace_t *tr ) {
	waterJet_t	*jet = &water.jets[drop->owner];
	vec3_t		normal;

	VectorCopy( tr->plane.normal, normal );
	if ( VectorNormalize( normal ) < 0.5f ) {
		VectorScale( drop->velocity, -1.0f, normal );
		VectorNormalize( normal );
	}
	if ( !jet->splashTime || cg.time - jet->splashTime >= WATER_SPLASH_MSEC || cg.time < jet->splashTime ) {
		jet->splashTime = cg.time ? cg.time : 1;
		trap->FX_PlayEffectID( water.splashFx, (float *)tr->endpos, normal, -1, -1, qfalse );
	}
	if ( !jet->soundTime || cg.time - jet->soundTime >= WATER_SOUND_MSEC || cg.time < jet->soundTime ) {
		jet->soundTime = cg.time ? cg.time : 1;
		trap->S_StartSound( (float *)tr->endpos, ENTITYNUM_WORLD, CHAN_AUTO, water.splashSound[rand() % 3] );
	}
	if ( tr->entityNum < MAX_CLIENTS ) {
		// the own face: drops run down the view
		if ( tr->entityNum == cg.clientNum && cg.snap && cg.snap->ps.clientNum == cg.clientNum
			&& ( !water.wetTime || cg.time - water.wetTime >= WATER_WET_MSEC || cg.time < water.wetTime ) ) {
			water.wetTime = cg.time ? cg.time : 1;
			CG_ScreenFxWet();
			trap->S_StartLocalSound( water.faceSound, CHAN_LOCAL_SOUND );
		}
		return;
	}
	if ( tr->entityNum == ENTITYNUM_WORLD && !( tr->surfaceFlags & SURF_SKY ) )
		Water_Puddle( tr->endpos, normal );
}

// moves a drop to cg.time: qfalse, it is gone
static qboolean Water_Move( waterDrop_t *drop ) {
	float	dt = ( cg.time - drop->time ) * 0.001f;
	vec3_t	end;
	trace_t	tr;

	if ( cg.time - drop->born > WATER_LIFE_MSEC || drop->origin[2] < -TOY_MAX_COORD )
		return qfalse;
	if ( dt <= 0.0f )
		return qtrue;
	dt = Q_min( dt, 0.1f );
	drop->time = cg.time;
	VectorMA( drop->origin, dt, drop->velocity, end );
	end[2] -= 0.5f * WATER_GRAVITY * dt * dt;
	drop->velocity[2] -= WATER_GRAVITY * dt;
	CG_Trace( &tr, drop->origin, vec3_origin, vec3_origin, end, cg.time - drop->born < WATER_OWN_MSEC ? drop->owner : ENTITYNUM_NONE,
		MASK_SHOT | CONTENTS_WATER );
	if ( tr.startsolid || tr.fraction < 1.0f ) {
		if ( !tr.startsolid && !( tr.contents & CONTENTS_WATER ) )
			Water_Hit( drop, &tr );
		return qfalse;
	}
	VectorCopy( end, drop->origin );
	return qtrue;
}

// the drop before this one in its jet, if it is still there and near enough to be one piece of water with it
static const waterDrop_t *Water_Prev( const waterDrop_t *drop ) {
	const waterDrop_t *prev;

	if ( drop->prev < 0 )
		return NULL;
	prev = &water.drops[drop->prev];
	if ( !prev->active || prev->owner != drop->owner || prev->born != drop->prevBorn
		|| DistanceSquared( prev->origin, drop->origin ) > WATER_LINK * WATER_LINK ) {
		return NULL;
	}
	return prev;
}

// the puddles and wet patches, drawn anew each frame with the size they have and as wet as they still are
static void Water_DrawPuddles( void ) {
	int i;

	for ( i = 0; i < WATER_PUDDLES; i++ ) {
		waterPuddle_t	*p = &water.puddles[i];
		const int		age = cg.time - p->wetTime;
		float			wet;
		vec3_t			light;

		if ( !p->active )
			continue;
		if ( age >= WATER_DRY_MSEC || age < 0 ) {
			p->active = qfalse;
			continue;
		}
		wet = age < WATER_DRY_MSEC / 2 ? 1.0f : 1.0f - (float)( age - WATER_DRY_MSEC / 2 ) / ( WATER_DRY_MSEC / 2 );
		CG_ToyLight( p->origin, light );
		CG_ImpactMark( p->floor ? water.puddleShader : water.wetShader, p->origin, p->normal, p->angle,
			light[0] * wet, light[1] * wet, light[2] * wet, wet, qfalse, p->size * ( 0.85f + 0.15f * wet ), qtrue );
	}
}

/*
=================
CG_WaterScene

While the scene of the frame is built: the jets let go of their drops, the drops fly on, and the water is drawn
=================
*/
void CG_WaterScene( void ) {
	refEntity_t	re;
	int			i, refracts = 0;

	if ( !water.registered )
		return;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		waterJet_t *jet = &water.jets[i];

		if ( !jet->until )
			continue;
		if ( cg.time >= jet->until || ( i != cg.clientNum && !v_toys.integer ) ) {
			jet->until = 0;
			jet->lastDrop = -1;
			continue;
		}
		// (at most a tenth of a second at once: a long frame is no flood)
		if ( jet->nextDrop < cg.time - 100 )
			jet->nextDrop = cg.time - 100;
		while ( jet->nextDrop <= cg.time ) {
			Water_Drop( i, jet, cg.time - jet->nextDrop );
			jet->nextDrop += WATER_DROP_MSEC;
		}
	}

	memset( &re, 0, sizeof( re ) );
	AxisClear( re.axis );
	for ( i = 0; i < WATER_DROPS; i++ ) {
		waterDrop_t			*drop = &water.drops[i];
		const waterDrop_t	*prev;
		vec3_t				light;
		float				age;

		if ( !drop->active )
			continue;
		if ( !Water_Move( drop ) ) {
			drop->active = qfalse;
			continue;
		}
		age = ( cg.time - drop->born ) / (float)WATER_LIFE_MSEC;
		CG_ToyLight( drop->origin, light );
		re.shaderRGBA[0] = (byte)( light[0] * 255.0f );
		re.shaderRGBA[1] = (byte)( light[1] * 255.0f );
		re.shaderRGBA[2] = (byte)( light[2] * 255.0f );
		re.shaderRGBA[3] = (byte)( 170.0f * ( 1.0f - 0.5f * age ) );
		VectorCopy( drop->origin, re.origin );
		prev = Water_Prev( drop );
		if ( prev ) {
			// a piece of the jet from this drop to the one before it, thinner as it falls apart
			re.reType = RT_LINE;
			re.customShader = water.streamShader;
			re.radius = 0.9f - 0.5f * age;
			VectorCopy( prev->origin, re.oldorigin );
			trap->R_AddRefEntityToScene( &re );
			if ( refracts < WATER_REFRACTS && ( i % 6 ) == 0 ) {
				CG_ScreenFxRefract( drop->origin, prev->origin, 5.0f, 0.5f * ( 1.0f - age ) );
				refracts++;
			}
		} else {
			re.reType = RT_SPRITE;
			re.customShader = water.dropShader;
			re.radius = 1.0f;
			trap->R_AddRefEntityToScene( &re );
		}
	}
	Water_DrawPuddles();
}
