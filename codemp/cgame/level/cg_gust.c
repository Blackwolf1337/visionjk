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

// cg_gust.c -- air that moves in the level (issue #23, "Plants and dust"). A Force Push or Pull throws up the dust of
// the floor in front of the player, by what the floor is made of (steamaudio/materials.txt, as the impacts of shots
// do), and on a floor of plants leaves, which flutter down and lie there for a while. An explosion on such a floor
// throws leaves too (its dust is weapons/cg_explosion.c). The grass of the level (the surface sprites, grass.vert of
// rd-vulkan-vision) parts round the players that stand in it and lies down under a push or a blast: the renderer gets
// the spots once a frame (ext.R_AddGrassPush).
//
// All of it is on v_levelFx. The game sends what it always sent, only the look changes.

#include "cgame/cg_local.h"
#include "cgame/weapons/cg_impact.h"
#include "cgame/level/cg_gust.h"

#define GUST_NEAR			2500.0f		// further from the eye: nothing
#define PUSH_SPOTS			7			// places along a push where the floor is looked at
#define PUSH_FIRST			44.0f		// units in front of the player the first one is, and from one to the next
#define PUSH_STEP			44.0f
#define PUSH_FAN			22.0f		// degrees to each side
#define PUSH_FLOOR			110.0f		// a floor this far below the push still takes it
#define PUSH_CLIMB			30.0f		// how far the ground may rise from one place to the next
#define FLOOR_NORMAL		0.7f
#define LEAVES				128
#define LEAF_SHAPES			4			// in the picture, 2 x 2
#define LEAF_GRAVITY		160.0f
#define LEAF_SINK			38.0f		// units a second it falls at most
#define LEAF_DRAG			2.4f
#define LEAF_SHRINK			900			// msec at the end of its life it shrinks away
#define GRASS_PUSHES		8			// what grass.vert takes
#define GRASS_PLAYER		44.0f		// how far round a player the grass parts
#define GRASS_GUST_MSEC		700			// a push or a blast holds the grass down that long

typedef struct {
	int			birth, life;			// birth 0: none
	vec3_t		origin, velocity;
	float		size;
	float		yaw, turn, turnRate;	// degrees: where its stem points, and round the stem
	float		phase;					// of its flutter
	byte		color[3];
	byte		shape;
	qboolean	rests;
} leaf_t;

typedef struct {
	int			time;					// 0: none
	vec3_t		origin;
	float		radius;
} gust_t;

static leaf_t		leaves[LEAVES];
static gust_t		gusts[4];
static forcePush_t	pushes[MAX_CLIENTS];
static int			numPushes;
static int			pushing[MAX_CLIENTS];
static qhandle_t	leafShader;
static int			gustLevel = -1;		// cgs.levelStartTime the above are of

void CG_GustInit( void ) {
	memset( leaves, 0, sizeof( leaves ) );
	memset( gusts, 0, sizeof( gusts ) );
	memset( pushing, 0, sizeof( pushing ) );
	numPushes = 0;
}

void CG_GustRegister( void ) {
	leafShader = trap->R_RegisterShader( "gfx/vision/leaves" );
}

int CG_ForcePushes( const forcePush_t **list ) {
	*list = pushes;
	return numPushes;
}

// the leaves a floor of this material has, in the light of the place: qfalse, it has none
static qboolean Gust_LeafColor( int material, const vec3_t at, byte *color ) {
	// (fresh ones, and what lies under them; dry ones)
	static const vec3_t fresh[3] = { { 0.33f, 0.47f, 0.16f }, { 0.44f, 0.52f, 0.2f }, { 0.55f, 0.47f, 0.2f } };
	static const vec3_t dry[3] = { { 0.5f, 0.36f, 0.17f }, { 0.58f, 0.44f, 0.2f }, { 0.4f, 0.27f, 0.14f } };
	const vec3_t	*tint;
	vec3_t			point, ambient, directed, dir;
	int				i;

	switch ( material ) {
	case MATERIAL_SHORTGRASS:
	case MATERIAL_LONGGRASS:
	case MATERIAL_GREENLEAVES:
		tint = &fresh[Q_irand( 0, 2 )];
		break;
	case MATERIAL_DRYLEAVES:
		tint = &dry[Q_irand( 0, 2 )];
		break;
	default:
		return qfalse;
	}
	VectorCopy( at, point );
	if ( !trap->R_LightForPoint( point, ambient, directed, dir ) ) {
		VectorSet( ambient, 128.0f, 128.0f, 128.0f );
		VectorClear( directed );
	}
	for ( i = 0; i < 3; i++ ) {
		color[i] = (byte)Com_Clamp( 12.0f, 255.0f, ( *tint )[i] * ( ambient[i] + 0.5f * directed[i] ) * 1.6f );
	}
	return qtrue;
}

// leaves off the floor at this spot, thrown along dir at about this speed
static void Gust_Leaves( const vec3_t at, const vec3_t dir, int material, int count, float speed ) {
	int i, k;

	if ( !leafShader ) {
		return;
	}
	for ( i = 0; i < LEAVES && count > 0; i++ ) {
		leaf_t *leaf = &leaves[i];

		if ( leaf->birth && cg.time < leaf->birth + leaf->life ) {
			continue;
		}
		if ( !Gust_LeafColor( material, at, leaf->color ) ) {
			return;
		}
		leaf->birth = cg.time;
		leaf->life = Q_irand( 5000, 8000 );
		leaf->rests = qfalse;
		for ( k = 0; k < 3; k++ ) {
			leaf->origin[k] = at[k] + flrand( -14.0f, 14.0f );
			leaf->velocity[k] = dir[k] * speed * flrand( 0.5f, 1.2f ) + flrand( -40.0f, 40.0f );
		}
		leaf->origin[2] = at[2] + flrand( 2.0f, 10.0f );
		leaf->velocity[2] = fabsf( leaf->velocity[2] ) + flrand( 60.0f, 150.0f );
		leaf->size = flrand( 2.8f, 5.0f );
		leaf->yaw = flrand( 0.0f, 360.0f );
		leaf->turn = flrand( 0.0f, 360.0f );
		leaf->turnRate = flrand( 250.0f, 700.0f ) * ( Q_irand( 0, 1 ) ? 1.0f : -1.0f );
		leaf->phase = flrand( 0.0f, 6.28f );
		leaf->shape = (byte)Q_irand( 0, LEAF_SHAPES - 1 );
		count--;
	}
}

// the grass lies down round this spot for a moment
static void Gust_Grass( const vec3_t origin, float radius ) {
	int i, oldest = 0;

	for ( i = 0; i < (int)ARRAY_LEN( gusts ); i++ ) {
		if ( gusts[i].time < gusts[oldest].time ) {
			oldest = i;
		}
	}
	gusts[oldest].time = cg.time;
	gusts[oldest].radius = radius;
	VectorCopy( origin, gusts[oldest].origin );
}

void CG_GustBlast( const vec3_t at, const vec3_t normal, int material, float size ) {
	if ( !v_levelFx.integer || DistanceSquared( at, cg.refdef.vieworg ) > GUST_NEAR * GUST_NEAR ) {
		return;
	}
	Gust_Grass( at, 140.0f + 110.0f * size );
	if ( normal[2] >= FLOOR_NORMAL ) {
		Gust_Leaves( at, normal, material, (int)( 10.0f * size + 0.5f ), 120.0f );
	}
}

// a Force Push or Pull: the dust and the leaves of the floor in front of the player, away from him or towards him
static void Gust_Push( const forcePush_t *push ) {
	const qhandle_t	dust = CG_ImpactDustShader();
	vec3_t			angles, flat, at;
	int				i, k, n;

	if ( DistanceSquared( push->from, cg.refdef.vieworg ) > GUST_NEAR * GUST_NEAR ) {
		return;
	}
	vectoangles( push->forward, angles );
	angles[PITCH] = 0.0f;
	AngleVectors( angles, flat, NULL, NULL );
	VectorMA( push->from, PUSH_FIRST + PUSH_STEP * PUSH_SPOTS * 0.5f, flat, angles );
	Gust_Grass( angles, PUSH_STEP * PUSH_SPOTS * 0.6f );

	// along the push over the ground, which may rise or fall in front of him
	VectorCopy( push->from, at );
	for ( i = 0; i < PUSH_SPOTS; i++ ) {
		// (a pull draws it in: slower the nearer it already is)
		const float	speed = push->pull ? -( 40.0f + 100.0f * i / PUSH_SPOTS ) : 170.0f - 110.0f * i / PUSH_SPOTS;
		vec3_t		dir, spot, below, color, vel;
		trace_t		tr;
		int			material;

		VectorSet( angles, 0.0f, vectoyaw( flat ) + flrand( -PUSH_FAN, PUSH_FAN ), 0.0f );
		AngleVectors( angles, dir, NULL, NULL );
		VectorMA( at, ( i ? PUSH_STEP : PUSH_FIRST ) + flrand( -10.0f, 10.0f ), dir, spot );
		spot[2] += PUSH_CLIMB;
		CG_Trace( &tr, at, NULL, NULL, spot, push->clientNum, MASK_SOLID );
		if ( tr.startsolid || ( tr.fraction < 1.0f && tr.plane.normal[2] < FLOOR_NORMAL ) ) {
			// a wall: what is behind it is out of the push
			break;
		}
		VectorCopy( tr.endpos, spot );
		VectorSet( below, spot[0], spot[1], spot[2] - PUSH_FLOOR - PUSH_CLIMB );
		CG_Trace( &tr, spot, NULL, NULL, below, push->clientNum, MASK_SOLID );
		if ( tr.startsolid || tr.fraction >= 1.0f || tr.plane.normal[2] < FLOOR_NORMAL ) {
			// (over a drop: on at this height)
			VectorCopy( spot, at );
			at[2] -= PUSH_CLIMB;
			continue;
		}
		VectorSet( at, tr.endpos[0], tr.endpos[1], tr.endpos[2] + 20.0f );
		if ( !CG_ImpactMaterial( tr.endpos, tr.plane.normal, qfalse, &material, NULL ) ) {
			continue;
		}
		Gust_Leaves( tr.endpos, dir, material, Q_irand( 3, 5 ), speed );
		if ( !dust || !CG_ImpactDustColor( CG_ImpactKindOf( material ), color ) ) {
			continue;
		}
		// (dust in the air is lighter than the ground it came off, or it would not show over it)
		for ( k = 0; k < 3; k++ ) {
			color[k] += ( 1.0f - color[k] ) * 0.3f;
		}
		for ( n = 0; n < 2; n++ ) {
			VectorScale( dir, speed * flrand( 0.6f, 1.1f ), vel );
			for ( k = 0; k < 2; k++ ) {
				vel[k] += flrand( -18.0f, 18.0f );
			}
			vel[2] = flrand( 18.0f, 42.0f );
			// (v_softParticles fades a sprite within 10 units of the floor)
			VectorSet( spot, tr.endpos[0] + flrand( -12.0f, 12.0f ), tr.endpos[1] + flrand( -12.0f, 12.0f ), tr.endpos[2] + 14.0f + 10.0f * n );
			CG_SmokePuff( spot, vel, flrand( 30.0f, 50.0f ), color[0], color[1], color[2], 0.7f, Q_irand( 1300, 2100 ),
				cg.time, 0, 0, dust );
		}
	}
}

// the players that begin a push or a pull this frame: the hand effect of the game (PW_DISINT_4, PW_PULL with it)
static void Gust_Pushes( void ) {
	int i;

	numPushes = 0;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const centity_t	*cent = &cg_entities[i];
		const qboolean	own = (qboolean)( cg.snap && i == cg.snap->ps.clientNum );
		const int		now = ( ( own || cent->currentValid ) && ( cent->currentState.powerups & ( 1 << PW_DISINT_4 ) ) ) ? 1 : 0;
		forcePush_t		*push;

		if ( !now || pushing[i] ) {
			pushing[i] = now;
			continue;
		}
		pushing[i] = now;
		push = &pushes[numPushes++];
		push->clientNum = i;
		push->pull = (qboolean)( ( cent->currentState.powerups & ( 1 << PW_PULL ) ) != 0 );
		if ( own ) {
			VectorCopy( cg.predictedPlayerState.origin, push->from );
			AngleVectors( cg.predictedPlayerState.viewangles, push->forward, NULL, NULL );
		} else {
			VectorCopy( cent->lerpOrigin, push->from );
			AngleVectors( cent->lerpAngles, push->forward, NULL, NULL );
		}
		push->from[2] += 20.0f;
	}
}

static void Gust_AddLeaves( void ) {
	const float	dt = Com_Clamp( 0.0f, 0.1f, cg.frametime * 0.001f );
	const float	keep = expf( -LEAF_DRAG * dt );
	int			i, k;

	for ( i = 0; i < LEAVES; i++ ) {
		leaf_t		*leaf = &leaves[i];
		polyVert_t	verts[4];
		vec3_t		stem, side, across, to;
		trace_t		tr;
		float		size, s, c;
		int			left;

		if ( !leaf->birth ) {
			continue;
		}
		left = leaf->birth + leaf->life - cg.time;
		if ( left <= 0 ) {
			leaf->birth = 0;
			continue;
		}
		if ( !leaf->rests ) {
			const float flutter = sinf( leaf->phase + cg.time * 0.006f );

			leaf->velocity[0] *= keep;
			leaf->velocity[1] *= keep;
			leaf->velocity[2] -= LEAF_GRAVITY * dt;
			// (it sinks slower while it lies flat on the air)
			leaf->velocity[2] = Q_max( leaf->velocity[2], -LEAF_SINK * ( 0.6f + 0.4f * fabsf( flutter ) ) );
			leaf->yaw += flutter * 90.0f * dt;
			leaf->turn += leaf->turnRate * dt;
			VectorMA( leaf->origin, dt, leaf->velocity, to );
			to[0] += cosf( DEG2RAD( leaf->yaw ) ) * flutter * 14.0f * dt;
			to[1] += sinf( DEG2RAD( leaf->yaw ) ) * flutter * 14.0f * dt;
			// (the ground rises and falls under it: where it comes down it lies, at a wall it drops)
			CG_Trace( &tr, leaf->origin, NULL, NULL, to, ENTITYNUM_NONE, MASK_SOLID );
			if ( tr.fraction < 1.0f && !tr.startsolid ) {
				VectorMA( tr.endpos, 0.6f, tr.plane.normal, leaf->origin );
				if ( tr.plane.normal[2] >= FLOOR_NORMAL ) {
					leaf->rests = qtrue;
				} else {
					VectorClear( leaf->velocity );
				}
			} else {
				VectorCopy( to, leaf->origin );
			}
		}
		size = leaf->size * Q_min( 1.0f, left / (float)LEAF_SHRINK );
		s = sinf( DEG2RAD( leaf->yaw ) );
		c = cosf( DEG2RAD( leaf->yaw ) );
		VectorSet( stem, c, s, 0.0f );
		VectorSet( side, -s, c, 0.0f );
		if ( leaf->rests ) {
			VectorCopy( side, across );
		} else {
			const float ts = sinf( DEG2RAD( leaf->turn ) ), tc = cosf( DEG2RAD( leaf->turn ) );

			VectorSet( across, side[0] * tc, side[1] * tc, ts );
		}
		for ( k = 0; k < 4; k++ ) {
			const float a = ( k == 0 || k == 3 ) ? -size : size, b = ( k < 2 ) ? -size : size;

			VectorMA( leaf->origin, a, stem, verts[k].xyz );
			VectorMA( verts[k].xyz, b, across, verts[k].xyz );
			verts[k].st[0] = ( ( leaf->shape & 1 ) + ( a > 0.0f ? 1.0f : 0.0f ) ) * 0.5f;
			verts[k].st[1] = ( ( leaf->shape >> 1 ) + ( b > 0.0f ? 1.0f : 0.0f ) ) * 0.5f;
			verts[k].modulate[0] = leaf->color[0];
			verts[k].modulate[1] = leaf->color[1];
			verts[k].modulate[2] = leaf->color[2];
			verts[k].modulate[3] = 255;
		}
		trap->R_AddPolysToScene( leafShader, 4, verts, 1 );
	}
}

// who and what moves the grass this frame: the pushes and blasts of the last moments first, then the players and
// NPCs near the eye
static void Gust_AddGrass( void ) {
	static int	checked = -1;
	int			i, count = 0;

	if ( checked < 0 ) {
		char value[8];

		trap->Cvar_VariableStringBuffer( "v_cgameGrassPush", value, sizeof( value ) );
		checked = atoi( value ) == 1 ? 1 : 0;
	}
	if ( !checked || !trap->ext.R_AddGrassPush ) {
		return;
	}
	for ( i = 0; i < (int)ARRAY_LEN( gusts ) && count < GRASS_PUSHES; i++ ) {
		const int age = cg.time - gusts[i].time;

		if ( !gusts[i].time || age < 0 || age >= GRASS_GUST_MSEC ) {
			continue;
		}
		// (down at once, up again slowly)
		trap->ext.R_AddGrassPush( gusts[i].origin, gusts[i].radius, 1.0f - (float)age / GRASS_GUST_MSEC );
		count++;
	}
	if ( !cg.snap ) {
		return;
	}
	if ( cg.predictedPlayerState.pm_type != PM_SPECTATOR && count < GRASS_PUSHES ) {
		trap->ext.R_AddGrassPush( cg.predictedPlayerState.origin, GRASS_PLAYER, 1.0f );
		count++;
	}
	for ( i = 0; i < cg.snap->numEntities && count < GRASS_PUSHES; i++ ) {
		const entityState_t	*es = &cg.snap->entities[i];
		const centity_t		*cent = &cg_entities[es->number];

		if ( ( es->eType != ET_PLAYER && es->eType != ET_NPC ) || ( es->eFlags & EF_DEAD )
			|| DistanceSquared( cent->lerpOrigin, cg.refdef.vieworg ) > 1200.0f * 1200.0f ) {
			continue;
		}
		trap->ext.R_AddGrassPush( cent->lerpOrigin, GRASS_PLAYER, 1.0f );
		count++;
	}
}

void CG_GustFrame( void ) {
	int i;

	if ( gustLevel != cgs.levelStartTime ) {
		gustLevel = cgs.levelStartTime;
		CG_GustInit();
	}
	Gust_Pushes();
	if ( !v_levelFx.integer ) {
		return;
	}
	for ( i = 0; i < numPushes; i++ ) {
		Gust_Push( &pushes[i] );
	}
	Gust_AddLeaves();
	Gust_AddGrass();
}
