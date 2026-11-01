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
#define WATER_RADIUS		0.75f		// of the jet where it leaves the gun, drawn by the renderer
#define WATER_TEAR_MSEC		170			// a jet starts to fall apart into drops this long after it left
#define WATER_TORN_MSEC		650			//	and is all drops by then
#define WATER_HOLD_MSEC		450			// the gun stays in the hand this long after the last water

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
	int				liquid;			// the renderer draws the water (ext.R_AddLiquid): 1 yes, -1 no, 0 not asked yet
	qhandle_t		gunModel;		// models/vision/toys/watergun.md3, its grip at the origin
	vec3_t			flash;			//	and where the water leaves it (tag_flash)
	int				holdUntil[MAX_CLIENTS];		// cg.time the gun is in his hand until
	vec3_t			muzzle[MAX_CLIENTS];		// where the water left the gun drawn last
	int				muzzleTime[MAX_CLIENTS];	//	and its cg.time
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
	water.gunModel = trap->R_RegisterModel( "models/vision/toys/watergun.md3" );
	VectorSet( water.flash, 21.0f, 0.0f, 3.0f );
	if ( water.gunModel ) {
		orientation_t tag;

		if ( trap->R_LerpTag( &tag, water.gunModel, 0, 0, 1.0f, "tag_flash" ) )
			VectorCopy( tag.origin, water.flash );
	}
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
	water.holdUntil[client] = jet->until + WATER_HOLD_MSEC;
	VectorCopy( origin, jet->origin );
	VectorCopy( velocity, jet->velocity );
}

void CG_WaterDown_f( void ) {
	water.spraying = qtrue;
}

void CG_WaterUp_f( void ) {
	water.spraying = qfalse;
}

// Where the water of the own gun leaves the hand and how fast: from the right hand where the player looks. The
// angles of the player, not of the view: a camera turned around him in third person does not aim the gun.
static void Water_Own( vec3_t origin, vec3_t velocity ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	vec3_t				forward, right, up;

	AngleVectors( ps->viewangles, forward, right, up );
	if ( cg.clientNum >= 0 && cg.clientNum < MAX_CLIENTS && water.muzzleTime[cg.clientNum]
		&& cg.time - water.muzzleTime[cg.clientNum] < 100 && cg.time >= water.muzzleTime[cg.clientNum] ) {
		// the muzzle of the gun as it was drawn the frame before: in the hand, or in the view
		VectorCopy( water.muzzle[cg.clientNum], origin );
	} else {
		VectorCopy( ps->origin, origin );
		origin[2] += ps->viewheight;
		VectorMA( origin, 12.0f, forward, origin );
		VectorMA( origin, 7.0f, right, origin );
		VectorMA( origin, -6.0f, up, origin );
	}
	VectorScale( forward, WATER_SPEED, velocity );
	VectorMA( velocity, 0.5f, ps->velocity, velocity );
}

// the water gun is in his hand: while he sprays and a moment after
qboolean CG_WaterHolding( int client ) {
	if ( client < 0 || client >= MAX_CLIENTS || !water.registered || !water.gunModel )
		return qfalse;
	if ( client != cg.clientNum && !v_toys.integer )
		return qfalse;
	return (qboolean)( water.holdUntil[client] > cg.time && water.holdUntil[client] - cg.time <= WATER_JET_MSEC + WATER_HOLD_MSEC );
}

// the way the gun of a player points: where he looks, upright
static void Water_GunAxis( centity_t *cent, vec3_t axis[3] ) {
	vec3_t angles;

	if ( cent->currentState.number == cg.clientNum )
		VectorCopy( cg.predictedPlayerState.viewangles, angles );
	else
		VectorCopy( cent->lerpAngles, angles );
	angles[ROLL] = 0.0f;
	AnglesToAxis( angles, axis );
}

// where the water leaves a gun whose grip is at origin
static void Water_Muzzle( int client, const vec3_t origin, vec3_t axis[3] ) {
	VectorMA( origin, water.flash[0], axis[0], water.muzzle[client] );
	VectorMA( water.muzzle[client], water.flash[1], axis[1], water.muzzle[client] );
	VectorMA( water.muzzle[client], water.flash[2], axis[2], water.muzzle[client] );
	water.muzzleTime[client] = cg.time ? cg.time : 1;
}

/*
=================
CG_WaterGun

The gun in the right hand of a player who sprays, his grip on its grip, pointing where he looks (his torso holds a
rifle up, CG_WaterTorsoAnim). cg_players.c has put his weapon away. Also for the own player, in third person and in
mirrors.
=================
*/
qboolean CG_WaterGun( centity_t *cent ) {
	const int		client = cent->currentState.number;
	clientInfo_t	*ci;
	mdxaBone_t		bolt;
	refEntity_t		re;

	if ( !CG_WaterHolding( client ) || !cent->ghoul2 )
		return qfalse;
	ci = &cgs.clientinfo[client];
	if ( ci->bolt_rhand < 0 )
		return qfalse;
	trap->G2API_GetBoltMatrix( cent->ghoul2, 0, ci->bolt_rhand, &bolt, cent->turAngles, cent->lerpOrigin, cg.time,
		cgs.gameModels, cent->modelScale );
	memset( &re, 0, sizeof( re ) );
	re.hModel = water.gunModel;
	BG_GiveMeVectorFromMatrix( &bolt, ORIGIN, re.origin );
	VectorCopy( re.origin, re.lightingOrigin );
	Water_GunAxis( cent, re.axis );
	if ( client == cg.clientNum && !cg.renderingThirdPerson )
		re.renderfx |= RF_THIRD_PERSON;
	trap->R_AddRefEntityToScene( &re );
	if ( client != cg.clientNum || cg.renderingThirdPerson )
		Water_Muzzle( client, re.origin, re.axis );
	return qtrue;
}

/*
=================
CG_WaterViewGun

The own gun in first person, low at the right as a weapon is held, in place of the view weapon
=================
*/
#define WATER_VIEW_FORWARD	11.0f
#define WATER_VIEW_RIGHT	6.0f
#define WATER_VIEW_UP		-8.5f
#define WATER_VIEW_SCALE	0.7f		// smaller than in the hand of a player: it is held close to the eye

qboolean CG_WaterViewGun( void ) {
	refEntity_t re;

	if ( !CG_WaterHolding( cg.clientNum ) )
		return qfalse;
	memset( &re, 0, sizeof( re ) );
	re.hModel = water.gunModel;
	VectorCopy( cg.refdef.vieworg, re.origin );
	VectorMA( re.origin, WATER_VIEW_FORWARD, cg.refdef.viewaxis[0], re.origin );
	VectorMA( re.origin, -WATER_VIEW_RIGHT, cg.refdef.viewaxis[1], re.origin );
	VectorMA( re.origin, WATER_VIEW_UP, cg.refdef.viewaxis[2], re.origin );
	// a little up and in, so the muzzle points at the crosshair
	{
		vec3_t angles;

		VectorCopy( cg.refdef.viewangles, angles );
		angles[PITCH] -= 3.0f;
		angles[YAW] += 2.0f;
		AnglesToAxis( angles, re.axis );
	}
	VectorSet( re.modelScale, WATER_VIEW_SCALE, WATER_VIEW_SCALE, WATER_VIEW_SCALE );
	ScaleModelAxis( &re );
	VectorCopy( cg.refdef.vieworg, re.lightingOrigin );
	re.renderfx = RF_DEPTHHACK | RF_FIRST_PERSON | RF_MINLIGHT;
	trap->R_AddRefEntityToScene( &re );
	// (the axes are scaled: the muzzle comes out where it is drawn)
	Water_Muzzle( cg.clientNum, re.origin, re.axis );
	return qtrue;
}

// the torso of a player who holds the gun holds a rifle up, whatever his weapon
int CG_WaterTorsoAnim( centity_t *cent, int anim ) {
	if ( cent->currentState.eType == ET_PLAYER && CG_WaterHolding( cent->currentState.number ) )
		return TORSO_WEAPONREADY3;
	return anim;
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
	water.holdUntil[cg.clientNum] = cg.time + WATER_HOLD_MSEC;
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

// the renderer draws clear water itself (rd-vulkan-vision): asked once
static qboolean Water_Liquid( void ) {
	if ( !water.liquid ) {
		char value[8];

		trap->Cvar_VariableStringBuffer( "v_cgameLiquid", value, sizeof( value ) );
		water.liquid = ( atoi( value ) && trap->ext.R_AddLiquid && trap->ext.R_AddLiquid( NULL, 0 ) ) ? 1 : -1;
	}
	return (qboolean)( water.liquid > 0 );
}

// 0 to 1 for a drop, the same every frame
static float Water_Random( const waterDrop_t *drop, int salt ) {
	unsigned int h = (unsigned int)drop->born * 2654435761u ^ (unsigned int)( drop - water.drops ) * 40503u ^ (unsigned int)salt * 97u;

	h ^= h >> 15;
	h *= 2246822519u;
	h ^= h >> 13;
	return ( h & 0xffffu ) / 65535.0f;
}

// how far the water of a drop has fallen apart, 0 a smooth jet to 1 drops
static float Water_Breakup( const waterDrop_t *drop ) {
	const float t = (float)( cg.time - drop->born - WATER_TEAR_MSEC ) / ( WATER_TORN_MSEC - WATER_TEAR_MSEC );

	return Com_Clamp( 0.0f, 1.0f, t );
}

// where the water of a drop is on the water itself, in units: what rides on its surface moves with it
static float Water_Flow( const waterDrop_t *drop ) {
	return (float)( drop->born % 60000 ) * 0.001f * WATER_SPEED;
}

// The radius of the water at a drop: thinner as the jet stretches, a little uneven from the start (the pump does
// not push evenly), and once it tears every drop pulls itself into a bead of its own, some larger, some smaller
// (as a real jet does, Plateau and Rayleigh)
static float Water_Radius( const waterDrop_t *drop, float breakup ) {
	const float age = ( cg.time - drop->born ) / (float)WATER_LIFE_MSEC;
	const float uneven = Water_Random( drop, 1 ) - 0.5f;

	return WATER_RADIUS * ( 1.0f - 0.35f * age ) * ( 1.0f + uneven * 0.14f + breakup * ( uneven + 0.15f ) * 0.9f );
}

/*
=================
Water_AddLiquid

The jets as the renderer draws water: a piece of round water from each drop to the one before it while the jet
is whole, one from the gun to the newest drop, and where it has torn a drop by itself, stretched along the way it
flies. A drop lets go of the one before it once the jet tears, the later the more it has fallen apart.
=================
*/
static void Water_AddLiquid( void ) {
	static liquidSegment_t	segs[WATER_DROPS + MAX_CLIENTS];
	int						count = 0, i;

	for ( i = 0; i < WATER_DROPS; i++ ) {
		const waterDrop_t	*drop = &water.drops[i];
		const waterDrop_t	*prev;
		liquidSegment_t		*seg;
		float				breakup;

		if ( !drop->active )
			continue;
		breakup = Water_Breakup( drop );
		prev = Water_Prev( drop );
		if ( prev && Water_Random( drop, 2 ) < breakup * 1.15f )
			prev = NULL;
		seg = &segs[count++];
		VectorCopy( drop->origin, seg->b );
		seg->radius[1] = Water_Radius( drop, breakup );
		seg->flow[1] = Water_Flow( drop );
		seg->breakup = breakup;
		if ( prev ) {
			const float prevBreakup = Water_Breakup( prev );

			VectorCopy( prev->origin, seg->a );
			seg->radius[0] = Water_Radius( prev, prevBreakup );
			seg->flow[0] = Water_Flow( prev );
		} else {
			// a drop alone: a little longer than round along the way it flies
			VectorMA( drop->origin, -0.007f - 0.007f * Water_Random( drop, 3 ), drop->velocity, seg->a );
			seg->radius[0] = seg->radius[1] * 0.8f;
			seg->flow[0] = seg->flow[1] - 3.0f;
		}
	}
	// the piece from the gun to the drop it let go of last
	for ( i = 0; i < MAX_CLIENTS && count < WATER_DROPS + MAX_CLIENTS; i++ ) {
		const waterJet_t	*jet = &water.jets[i];
		const waterDrop_t	*last;
		liquidSegment_t		*seg;

		if ( !jet->until || jet->lastDrop < 0 )
			continue;
		last = &water.drops[jet->lastDrop];
		if ( !last->active || last->owner != i || DistanceSquared( last->origin, jet->origin ) > WATER_LINK * WATER_LINK )
			continue;
		seg = &segs[count++];
		VectorCopy( jet->origin, seg->a );
		VectorCopy( last->origin, seg->b );
		seg->radius[0] = WATER_RADIUS * 0.85f;
		seg->radius[1] = Water_Radius( last, 0.0f );
		seg->flow[1] = Water_Flow( last );
		seg->flow[0] = seg->flow[1] + ( cg.time - last->born ) * 0.001f * WATER_SPEED;
		seg->breakup = 0.0f;
	}
	if ( count )
		trap->ext.R_AddLiquid( segs, count );
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
		// the jet of somebody else leaves the gun this cgame drew in his hand this frame
		if ( i != cg.clientNum && water.muzzleTime[i] == cg.time )
			VectorCopy( water.muzzle[i], jet->origin );
		// (at most a tenth of a second at once: a long frame is no flood)
		if ( jet->nextDrop < cg.time - 100 )
			jet->nextDrop = cg.time - 100;
		while ( jet->nextDrop <= cg.time ) {
			Water_Drop( i, jet, cg.time - jet->nextDrop );
			jet->nextDrop += WATER_DROP_MSEC;
		}
	}

	if ( Water_Liquid() ) {
		for ( i = 0; i < WATER_DROPS; i++ ) {
			if ( water.drops[i].active && !Water_Move( &water.drops[i] ) )
				water.drops[i].active = qfalse;
		}
		Water_AddLiquid();
		Water_DrawPuddles();
		return;
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
