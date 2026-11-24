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

// cg_saberdrop.c -- a player dies (issue #20): the hilt leaves the hand, its blade goes in on the way down, and it falls,
// bounces and rolls out on the floor. Client side only, so it may land elsewhere for each player. The server sets the
// weapon of a dead player to none in the same snapshot, so what the player held is remembered from the last frame alive. The
// hilt is a ghoul2 instance of its own; the body gets its weapon back at the respawn as it always did.

#include "cgame/cg_local.h"
#include "cgame/saber/cg_saberdrawn.h"
#include "cgame/saber/cg_saberdrop.h"

#define DROP_MAX			16
#define DROP_BLADES			4
#define DROP_LIFE_MSEC		15000
#define DROP_SINK_MSEC		500			// at the end of its life it sinks out of sight
#define DROP_BLADE_MSEC		340			// the blade going in
#define DROP_SEEN_MSEC		400			// dead for longer when first seen: died out of sight, nothing falls now
#define DROP_RADIUS			1.6f
#define DROP_GRAVITY		800.0f
#define DROP_BOUNCE			0.38f
#define DROP_FRICTION		0.72f
#define DROP_ROLL_DRAG		1.4f		// a second, rolling across its length
#define DROP_SLIDE_DRAG		4.5f		// a second, sliding along its length
#define DROP_SETTLE			420.0f		// degrees a second it tips over to lie flat
#define DROP_REST_SPEED		9.0f
#define DROP_STEP_MSEC		20			// the longest step of the physics

enum { DROP_ALIVE, DROP_DROPPED, DROP_KEPT };

typedef struct {
	vec3_t	start, dir;		// in the space of the model
	float	length, lengthMax, radius;
	int		color;
} dropBlade_t;

typedef struct {
	void		*ghoul2;
	vec3_t		origin, velocity, spin;		// spin: degrees a second round a world axis
	matrix3_t	axis;
	vec3_t		along;						// the length of the hilt, in the space of the model
	dropBlade_t	blades[DROP_BLADES];
	int			numBlades;
	int			birth, nextSound;
	qboolean	onFloor, resting;
} saberDrop_t;

static saberDrop_t	drops[DROP_MAX];
static int			nextDrop;
static int			dropState[MAX_CLIENTS], aliveTime[MAX_CLIENTS], held[MAX_CLIENTS];
static sfxHandle_t	bounceSounds[3];

static void Drop_Free( saberDrop_t *d ) {
	if ( d->ghoul2 ) {
		trap->G2API_CleanGhoul2Models( &d->ghoul2 );
	}
	memset( d, 0, sizeof( *d ) );
}

void CG_SaberDropShutdown( void ) {
	int i;

	for ( i = 0; i < DROP_MAX; i++ ) {
		Drop_Free( &drops[i] );
	}
}

void CG_SaberDropInit( void ) {
	CG_SaberDropShutdown();
	memset( dropState, 0, sizeof( dropState ) );
	memset( aliveTime, 0, sizeof( aliveTime ) );
	memset( held, 0, sizeof( held ) );
	nextDrop = 0;
}

void CG_SaberDropRegister( void ) {
	int i;

	for ( i = 0; i < 3; i++ ) {
		bounceSounds[i] = trap->S_RegisterSound( va( "sound/weapons/saber/bounce%d.wav", i + 1 ) );
	}
}

static void Drop_Rotate( saberDrop_t *d, const vec3_t axis, float degrees ) {
	vec3_t	turned;
	int		i;

	for ( i = 0; i < 3; i++ ) {
		RotatePointAroundVector( turned, axis, d->axis[i], degrees );
		VectorCopy( turned, d->axis[i] );
	}
}

static void Drop_ToWorld( const saberDrop_t *d, const vec3_t in, vec3_t out ) {
	int i;

	for ( i = 0; i < 3; i++ ) {
		out[i] = in[0] * d->axis[0][i] + in[1] * d->axis[1][i] + in[2] * d->axis[2][i];
	}
}

// the angles of AnglesToAxis back from its axis: a ghoul2 model is drawn by its angles
static void Drop_Angles( matrix3_t axis, vec3_t angles ) {
	const float sp = -axis[0][2];

	angles[PITCH] = RAD2DEG( asinf( Com_Clamp( -1.0f, 1.0f, sp ) ) );
	if ( fabsf( sp ) > 0.9999f ) {
		angles[YAW] = RAD2DEG( atan2f( -axis[1][0], axis[1][1] ) );
		angles[ROLL] = 0.0f;
	} else {
		angles[YAW] = RAD2DEG( atan2f( axis[0][1], axis[0][0] ) );
		angles[ROLL] = RAD2DEG( atan2f( axis[1][2], axis[2][2] ) );
	}
}

// where the blades sit on the model, with the model at the origin unturned
static void Drop_FindBlades( saberDrop_t *d, int num, int saberNum, const saberInfo_t *saber ) {
	static const vec3_t	zero = { 0.0f, 0.0f, 0.0f }, one = { 1.0f, 1.0f, 1.0f };
	const int			count = Q_min( saber->numBlades, DROP_BLADES );
	int					b;

	VectorSet( d->along, 1.0f, 0.0f, 0.0f );
	for ( b = 0; b < count; b++ ) {
		const saberDrawn_t	*drawn = CG_SaberDrawnLast( num, saberNum, b );
		dropBlade_t			*blade = &d->blades[d->numBlades];
		mdxaBone_t			matrix;
		int					bolt = trap->G2API_AddBolt( d->ghoul2, 0, va( "*blade%d", b + 1 ) );

		if ( bolt == -1 && !b ) {
			bolt = trap->G2API_AddBolt( d->ghoul2, 0, "*flash" );
		}
		if ( bolt == -1 ) {
			break;
		}
		trap->G2API_GetBoltMatrix( d->ghoul2, 0, bolt, &matrix, zero, zero, cg.time, cgs.gameModels, (float *)one );
		BG_GiveMeVectorFromMatrix( &matrix, ORIGIN, blade->start );
		BG_GiveMeVectorFromMatrix( &matrix, NEGATIVE_Y, blade->dir );
		if ( !b ) {
			VectorCopy( blade->dir, d->along );
		}
		if ( drawn && cg.time - drawn->time <= DROP_SEEN_MSEC ) {
			blade->length = VectorLength( drawn->span );
			blade->lengthMax = drawn->lengthMax;
			blade->radius = drawn->radius;
			blade->color = drawn->color;
		}
		d->numBlades++;
	}
}

static void Drop_Spawn( const centity_t *cent, const saberInfo_t *saber, int saberNum, int bolt ) {
	const int			num = cent->currentState.number;
	const saberDrawn_t	*drawn;
	saberDrop_t			*d = &drops[nextDrop];
	mdxaBone_t			matrix;
	vec3_t				hand, velocity;
	int					i;

	nextDrop = ( nextDrop + 1 ) % DROP_MAX;
	Drop_Free( d );
	trap->G2API_InitGhoul2Model( &d->ghoul2, saber->model, 0, saber->skin, 0, 0, 0 );
	if ( !d->ghoul2 ) {
		return;
	}
	if ( saber->skin ) {
		trap->G2API_SetSkin( d->ghoul2, 0, saber->skin, saber->skin );
	}
	Drop_FindBlades( d, num, saberNum, saber );
	AxisClear( d->axis );

	drawn = CG_SaberDrawnLast( num, saberNum, 0 );
	if ( drawn && cg.time - drawn->time <= DROP_SEEN_MSEC ) {
		// (it leaves the hand as it was held, and from where the blade began)
		vec3_t was, turn;

		VectorCopy( drawn->span, was );
		if ( VectorNormalize( was ) > 0.1f ) {
			CrossProduct( d->along, was, turn );
			if ( VectorNormalize( turn ) > 0.001f ) {
				Drop_Rotate( d, turn, RAD2DEG( acosf( Com_Clamp( -1.0f, 1.0f, DotProduct( d->along, was ) ) ) ) );
			}
		}
		VectorCopy( drawn->start, hand );
	} else {
		trap->G2API_GetBoltMatrix( cent->ghoul2, 0, bolt, &matrix, cent->turAngles, cent->lerpOrigin, cg.time, cgs.gameModels,
			(float *)cent->modelScale );
		BG_GiveMeVectorFromMatrix( &matrix, ORIGIN, hand );
	}
	if ( num == cg.snap->ps.clientNum ) {
		VectorCopy( cg.predictedPlayerState.velocity, velocity );
	} else {
		VectorCopy( cent->currentState.pos.trDelta, velocity );
	}
	VectorCopy( hand, d->origin );
	VectorScale( velocity, 0.4f, d->velocity );
	d->velocity[0] += flrand( -70.0f, 70.0f );
	d->velocity[1] += flrand( -70.0f, 70.0f );
	d->velocity[2] += flrand( 70.0f, 150.0f );
	for ( i = 0; i < 3; i++ ) {
		d->spin[i] = flrand( -480.0f, 480.0f );
	}
	d->birth = cg.time;
}

qboolean CG_SaberDropUpdate( centity_t *cent, clientInfo_t *ci ) {
	const int	num = cent->currentState.number;
	int			s;

	if ( num < 0 || num >= MAX_CLIENTS ) {
		return qfalse;
	}
	if ( !( cent->currentState.eFlags & EF_DEAD ) ) {
		dropState[num] = DROP_ALIVE;
		aliveTime[num] = cg.time;
		held[num] = 0;
		if ( cent->currentState.weapon == WP_SABER ) {
			// (the first one may be away, thrown)
			if ( ci->saber[0].model[0] && !cent->currentState.saberInFlight && cent->currentState.saberEntityNum ) {
				held[num] |= 1;
			}
			if ( ci->saber[1].model[0] ) {
				held[num] |= 2;
			}
		}
		return qfalse;
	}
	if ( dropState[num] != DROP_ALIVE ) {
		return (qboolean)( dropState[num] == DROP_DROPPED );
	}
	if ( !v_saberHilts.integer || !held[num] || cg.time - aliveTime[num] > DROP_SEEN_MSEC || !cent->ghoul2 ) {
		dropState[num] = DROP_KEPT;
		return qfalse;
	}
	for ( s = 0; s < MAX_SABERS; s++ ) {
		if ( held[num] & ( 1 << s ) ) {
			Drop_Spawn( cent, &ci->saber[s], s, s ? ci->bolt_lhand : ci->bolt_rhand );
		}
		if ( trap->G2API_HasGhoul2ModelOnIndex( &cent->ghoul2, s + 1 ) ) {
			trap->G2API_RemoveGhoul2Model( &cent->ghoul2, s + 1 );
		}
		BG_SI_SetDesiredLength( &ci->saber[s], 0, -1 );
		BG_SI_SetLength( &ci->saber[s], 0 );
	}
	dropState[num] = DROP_DROPPED;
	return qtrue;
}

// on the floor: it tips over until it lies flat, rolls across its length and hardly slides along it
static void Drop_OnFloor( saberDrop_t *d, const vec3_t normal, float dt ) {
	vec3_t		along, flat, turn, rollAxis, across;
	float		off, alongSpeed, drag;

	Drop_ToWorld( d, d->along, along );
	VectorMA( along, -DotProduct( along, normal ), normal, flat );
	if ( VectorNormalize( flat ) < 0.01f ) {
		// (standing on its end: over the way it moves, or any way)
		PerpendicularVector( flat, normal );
	}
	CrossProduct( along, flat, turn );
	off = RAD2DEG( acosf( Com_Clamp( -1.0f, 1.0f, DotProduct( along, flat ) ) ) );
	if ( VectorNormalize( turn ) > 0.001f ) {
		Drop_Rotate( d, turn, Q_min( off, DROP_SETTLE * dt ) );
	}

	alongSpeed = DotProduct( d->velocity, flat );
	VectorMA( d->velocity, -alongSpeed, flat, across );
	drag = Q_max( 0.0f, 1.0f - DROP_ROLL_DRAG * dt );
	VectorScale( across, drag, d->velocity );
	VectorMA( d->velocity, alongSpeed * Q_max( 0.0f, 1.0f - DROP_SLIDE_DRAG * dt ), flat, d->velocity );

	// a cylinder rolling without slipping: round its length, by how fast it goes across
	CrossProduct( normal, d->velocity, rollAxis );
	VectorScale( flat, RAD2DEG( DotProduct( rollAxis, flat ) / DROP_RADIUS ), d->spin );

	if ( VectorLength( d->velocity ) < DROP_REST_SPEED && off < 3.0f ) {
		VectorClear( d->velocity );
		VectorClear( d->spin );
		d->resting = qtrue;
	}
}

static void Drop_Move( saberDrop_t *d, float dt ) {
	static const vec3_t	mins = { -DROP_RADIUS, -DROP_RADIUS, -DROP_RADIUS }, maxs = { DROP_RADIUS, DROP_RADIUS, DROP_RADIUS };
	trace_t				tr;
	vec3_t				end, spinAxis;
	float				spinSpeed;

	d->velocity[2] -= DROP_GRAVITY * dt;
	VectorMA( d->origin, dt, d->velocity, end );
	CG_Trace( &tr, d->origin, mins, maxs, end, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.startsolid ) {
		VectorClear( d->velocity );
		d->resting = qtrue;
		return;
	}
	VectorCopy( tr.endpos, d->origin );
	if ( tr.fraction < 1.0f ) {
		const float	into = DotProduct( d->velocity, tr.plane.normal );
		vec3_t		slide;

		VectorMA( d->velocity, -into, tr.plane.normal, slide );
		VectorScale( slide, DROP_FRICTION, d->velocity );
		if ( -into > 60.0f ) {
			VectorMA( d->velocity, -into * DROP_BOUNCE, tr.plane.normal, d->velocity );
			VectorScale( d->spin, 0.7f, d->spin );
		}
		if ( -into > 45.0f && cg.time >= d->nextSound ) {
			trap->S_StartSound( d->origin, ENTITYNUM_WORLD, CHAN_AUTO, bounceSounds[Q_irand( 0, 2 )] );
			d->nextSound = cg.time + 110;
		}
		if ( tr.plane.normal[2] > 0.6f && -into <= 60.0f ) {
			Drop_OnFloor( d, tr.plane.normal, dt );
		}
	}
	VectorCopy( d->spin, spinAxis );
	spinSpeed = VectorNormalize( spinAxis );
	if ( spinSpeed > 0.01f ) {
		Drop_Rotate( d, spinAxis, spinSpeed * dt );
	}
}

static void Drop_Blades( const saberDrop_t *d, int age ) {
	const float	left = 1.0f - age / (float)DROP_BLADE_MSEC;
	int			b;

	if ( left <= 0.0f ) {
		return;
	}
	for ( b = 0; b < d->numBlades; b++ ) {
		const dropBlade_t	*blade = &d->blades[b];
		vec3_t				start, dir;

		if ( blade->length < 1.0f ) {
			continue;
		}
		Drop_ToWorld( d, blade->start, start );
		VectorAdd( start, d->origin, start );
		Drop_ToWorld( d, blade->dir, dir );
		CG_SaberLooseBlade( start, dir, blade->length * left, blade->lengthMax, blade->radius, blade->color,
			1.0f - 0.4f * Q_flrand( 0.0f, 1.0f ) );
	}
}

void CG_SaberDropAdd( void ) {
	int i;

	for ( i = 0; i < DROP_MAX; i++ ) {
		saberDrop_t	*d = &drops[i];
		refEntity_t	ent;
		int			age;

		if ( !d->ghoul2 ) {
			continue;
		}
		age = cg.time - d->birth;
		if ( age < 0 || age >= DROP_LIFE_MSEC ) {
			Drop_Free( d );
			continue;
		}
		if ( cg.frametime > 0 ) {
			int left = cg.frametime;

			while ( left > 0 && !d->resting ) {
				const int step = Q_min( left, DROP_STEP_MSEC );

				Drop_Move( d, step * 0.001f );
				left -= step;
			}
		}
		memset( &ent, 0, sizeof( ent ) );
		ent.ghoul2 = d->ghoul2;
		ent.radius = 32.0f;
		VectorSet( ent.modelScale, 1.0f, 1.0f, 1.0f );
		VectorCopy( d->origin, ent.origin );
		if ( age > DROP_LIFE_MSEC - DROP_SINK_MSEC ) {
			ent.origin[2] -= 6.0f * ( age - ( DROP_LIFE_MSEC - DROP_SINK_MSEC ) ) / (float)DROP_SINK_MSEC;
		}
		VectorCopy( ent.origin, ent.oldorigin );
		VectorCopy( ent.origin, ent.lightingOrigin );
		Drop_Angles( d->axis, ent.angles );
		AnglesToAxis( ent.angles, ent.axis );
		trap->R_AddRefEntityToScene( &ent );
		Drop_Blades( d, age );
	}
}
