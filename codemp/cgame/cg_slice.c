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

// cg_slice.c -- VisionJK: a saber kill cuts the body through along the killing swing (their wish 2026-11-08).
//
// The stock dismemberment of the server takes a limb off now and then (about 2 % of the kills at g_dismember 1) and
// never looks at the blade. Here every client works it out for itself: when a player is killed by a saber
// (EV_OBITUARY, MOD_SABER) and the blade of the killer passed through his body in the last moment (the blade
// history of cg_players.c, CG_SaberCutNear), the plane of that swing (the blade and the way it swept) cuts the body.
// The first time the body is drawn after that, it is split at the plane into ragdolls of their own (cg_ragdoll.c), one
// copy of the model per part, and from then on the body itself is never drawn whole again: not the dead player, not
// the copy the server leaves when he comes back (CG_BodyQueueCopy), not while it fades. Each part is drawn cut by the
// plane where its bone at the cut is now (the renderer's ext.R_AddCutEntity, rd-vulkan-vision), and once more with its
// back faces alone in a glowing colour (gfx/vision/slice/cut), which fills the opening: the burnt cut face, orange hot
// at first, fading through red to dark (the afterglow). With v_gore 1 or 2 blood runs out on the floor under the cut, a
// wet place of the kind WET_BLOOD (ext.R_AddWet); 1 also sprays it at the cut and onto the view (CG_GoreAir). The parts stay as long as the body would, and fade with it.
//
// Only a VisionJK client on a renderer that cuts and places bones (v_cgameCutEntity, v_cgameSetBones) sees it;
// everyone else sees the stock death, and the server is not asked for anything. The stock limbs of a body that was cut
// are not drawn.

#include "cg_local.h"
#include "ghoul2/G2.h"

#define SLICE_MAX			10
#define SLICE_REACH			48.0f		// units: how near the middle of the body the blade must have come
#define SLICE_GLOW_MSEC		10000		// the afterglow of the cut face
#define SLICE_BLOOD_MSEC	5000		// how long the blood takes to run out
#define SLICE_LIFE_MSEC		60000		// parts without a body (it came back without a copy) stay this long at most,
#define SLICE_ORPHAN_MSEC	30000		//	or this long after the body went
#define SLICE_FADE_MSEC		2000
#define SLICE_MARKS			96			// blood on the level of all cuts: splats and smears, the oldest goes
#define SLICE_DROPS			64			// drops of blood in flight
#define SLICE_SPOTS			90			// wet spots a frame the blood may take of WET_MAX_SPOTS
#define SLICE_BLEED_MSEC	20000		// a cut part bleeds less and less for this long
#define SLICE_PARTS			8			// (as RAG_PARTS of cg_ragdoll.c)
#define SLICE_MAX_CUTS		6			// joints a lying body can be cut at after its first cut (their choice 2026-11-15)
#define SLICE_CUT_PAUSE		250			// msec between two such cuts of one body, and after the first cut
#define SLICE_LYING_REACH	20.0f		// a blade this near the pelvis of a lying body cuts it
#define SLICE_JOINT_REACH	8.0f		// a blade this near a joint of a cut body cuts it there
#define SLICE_LYING_MSEC	150			// how recent the blade has to be for those
#define SLICE_BLAST_REACH	72.0f		// an explosion this near a body that dies by it bursts it (their choice 2026-11-15)
#define SLICE_BLAST_MSEC	400			// the kill message and the explosion this far apart still go together
#define SLICE_PRINTS		32			// bloody footprints at once, the oldest goes
#define SLICE_PRINT_MSEC	30000		// a bloody footprint fades in this time
#define SLICE_PRINT_STEPS	8			// steps out of a pool that leave prints, fainter with each
#define SLICE_FOOT			6.5f		// half the length of a print (a little more than WATER_FOOT of toys/cg_water.c: blood spreads)

typedef struct slice_s {
	qboolean	active;
	int			victim;			// client number
	int			body;			// the entity that has the body now: the victim while he is dead, then the copy of his body
								// (ET_BODY, CG_SliceBodyCopied), -1 when he came back without one
	qboolean	revived;		// the victim was seen alive again since the cut: his entity is his own again
	int			time;			// cg.time of the cut
	int			gone;			// cg.time the body went (0: it is still there), the parts fade a while later
	int			copied;			// cg.time the body copy was named (CG_SliceBodyCopied)
	qboolean	seen;			//	and whether it was in a snapshot since
	int			killer;			// client number, -1: a test cut
	vec4_t		plane;			// the normal towards the side with the feet
	vec3_t		sweep;			// which way the blade swept
	int			rag;			// the ragdolls of the parts (cg_ragdoll.c), -1 until the body was drawn once
	refEntity_t	ent;			// as the body was drawn when it was split
	float		alpha;			// 1, less while the body fades

	// the blood
	qboolean	blood;
	vec3_t		bloodOrigin, bloodNormal;
	float		bloodSeed, bloodRadius;
	qboolean	streakOn[SLICE_PARTS];	// a part lays a smear while it is dragged: from here,
	vec3_t		streakLast[SLICE_PARTS];
	int			streakMark[SLICE_PARTS];	//	the mark it lays now (-1: none)
	vec3_t		dripLast[SLICE_PARTS];	// a part in the air drips from its cut: where it last was,
	float		dripLeft[SLICE_PARTS];	//	and units to go to the next drop
	float		airSpeed[SLICE_PARTS];	// how fast it flew while in the air (a hard landing splashes)

	qboolean	expired;		// v_sliceTime ran out: parts and blood gone, the slot keeps the body from being drawn whole

	int			cuts;			// joints cut since, by sabers through the lying parts
	int			nextCut;		// cg.time from which the next of those may come
	qboolean	gib;			// burst by an explosion: more joints go at the split, the parts are thrown from gibOrigin
	vec3_t		gibOrigin;
} slice_t;



// blood left on the level by a cut: a splat (WET_BLOOD) or a piece of a smear (WET_BLOOD_STREAK)
typedef struct {
	qboolean	active;
	int			owner;			// the slice, its blood goes with it
	int			born;
	int			kind;
	vec3_t		origin, normal, hit;
	float		radius, width, strength, seed;
} sliceMark_t;

// a drop of blood in flight, a splat where it lands
typedef struct {
	qboolean	active;
	int			owner;
	int			born;
	vec3_t		origin, velocity;
	float		size;
} sliceDrop_t;

// the print of a foot that stepped in blood (CG_SliceStep)
typedef struct {
	qboolean	active;
	qboolean	right;
	int			born;
	vec3_t		origin, normal;
	float		yaw, wet;
} slicePrint_t;

static slice_t		slices[SLICE_MAX];
static slicePrint_t	slicePrints[SLICE_PRINTS];
static int			sliceNextPrint;
static float		sliceFeet[MAX_GENTITIES];	// how bloody the feet of an entity are, 1 just out of a pool
static vec3_t		sliceLastStep[MAX_GENTITIES];	// where its last foot came down,
static int			sliceLastStepTime[MAX_GENTITIES];	//	and when
static sliceMark_t	sliceMarks[SLICE_MARKS];
static sliceDrop_t	sliceDrops[SLICE_DROPS];
static qhandle_t sliceCutShader;

void CG_SliceInit( void ) {
	int i;

	CG_RagInit();
	memset( slices, 0, sizeof( slices ) );
	memset( sliceMarks, 0, sizeof( sliceMarks ) );
	memset( sliceDrops, 0, sizeof( sliceDrops ) );
	memset( slicePrints, 0, sizeof( slicePrints ) );
	memset( sliceFeet, 0, sizeof( sliceFeet ) );
	memset( sliceLastStepTime, 0, sizeof( sliceLastStepTime ) );
	for ( i = 0; i < SLICE_MAX; i++ ) {
		slices[i].rag = -1;
	}
	sliceCutShader = trap->R_RegisterShader( "gfx/vision/slice/cut" );
}

// whether bodies are cut here: the player wants it and the renderer can
static qboolean CG_SliceOn( void ) {
	static int has = -1;

	if ( has < 0 ) {
		char value[8];

		trap->Cvar_VariableStringBuffer( "v_cgameCutEntity", value, sizeof( value ) );
		has = ( atoi( value ) && trap->ext.R_AddCutEntity && CG_RagOn() ) ? 1 : 0;
	}
	return (qboolean)( has > 0 && v_slice.integer );
}

// v_gore: 0 no blood (the burnt cut stays), 1 on the screen and on the ground, 2 on the ground alone: the pools,
// the drips of flying parts, the streaks and the marks, without the spray at the cut and the blood on the view
qboolean CG_GoreAir( void ) {
	return (qboolean)( v_gore.integer == 1 );
}

qboolean CG_GoreGround( void ) {
	return (qboolean)( v_gore.integer == 1 || v_gore.integer == 2 );
}

// the parts and the blood of a cut go (CG_SliceFree, and when v_sliceTime runs out)
static void CG_SliceClear( slice_t *s ) {
	const int	owner = (int)( s - slices );
	int			i;

	if ( s->rag >= 0 ) {
		CG_RagFree( s->rag );
	}
	s->rag = -1;
	s->blood = qfalse;
	for ( i = 0; i < SLICE_MARKS; i++ ) {
		if ( sliceMarks[i].owner == owner ) {
			sliceMarks[i].active = qfalse;
		}
	}
	for ( i = 0; i < SLICE_DROPS; i++ ) {
		if ( sliceDrops[i].owner == owner ) {
			sliceDrops[i].active = qfalse;
		}
	}
}

static void CG_SliceFree( slice_t *s ) {
	CG_SliceClear( s );
	memset( s, 0, sizeof( *s ) );
	s->rag = -1;
}

// a free slot, else the oldest
static slice_t *CG_SliceNew( void ) {
	slice_t	*oldest = &slices[0];
	int		i;

	for ( i = 0; i < SLICE_MAX; i++ ) {
		if ( !slices[i].active ) {
			return &slices[i];
		}
		if ( slices[i].time < oldest->time ) {
			oldest = &slices[i];
		}
	}
	CG_SliceFree( oldest );
	return oldest;
}

// the cut that holds the body of entity num: the copy of his body, or the dead victim himself until he was seen alive
static slice_t *CG_SliceOf( int num ) {
	int i;

	for ( i = 0; i < SLICE_MAX; i++ ) {
		slice_t *s = &slices[i];

		if ( !s->active || s->gone ) {
			continue;
		}
		if ( s->body == num && num != s->victim ) {
			return s;
		}
		if ( s->victim == num && !s->revived && ( cg_entities[num].currentState.eFlags & EF_DEAD ) ) {
			return s;
		}
	}
	return NULL;
}

// whether the body of entity num was cut: it is drawn in its parts, never whole (cg_players.c, cg_ents.c)
qboolean CG_Sliced( int num ) {
	return (qboolean)( CG_SliceOf( num ) != NULL );
}

// Starts a cut of the body of victim by plane (the normal towards what stays with the body after the flip below),
// the blade having swept along sweep. qfalse: the plane misses the body.
static qboolean CG_SliceStartAt( int victim, int body, int killer, const vec4_t planeIn, const vec3_t sweep, const vec3_t keep );

static qboolean CG_SliceStart( int victim, int killer, const vec4_t planeIn, const vec3_t sweep ) {
	return CG_SliceStartAt( victim, victim, killer, planeIn, sweep, NULL );
}

// victim: the client whose body it is, body: the entity that shows it now (the dead player himself, or the copy the
// server left). keep: the point whose side stays the body (NULL: his feet, standing; and the plane has to pass through
// his box). A body lying there has no box to test, the ragdoll split tells whether the plane misses it.
static qboolean CG_SliceStartAt( int victim, int body, int killer, const vec4_t planeIn, const vec3_t sweep, const vec3_t keep ) {
	const centity_t	*cent = &cg_entities[body];
	vec4_t			plane;
	vec3_t			feet;
	int				i, above = 0, below = 0;
	slice_t			*s;

	VectorCopy4( planeIn, plane );
	// it has to pass through the body: the corners of his box on both sides
	for ( i = 0; i < 8 && !keep; i++ ) {
		vec3_t corner;

		corner[0] = cent->lerpOrigin[0] + ( ( i & 1 ) ? 15.0f : -15.0f );
		corner[1] = cent->lerpOrigin[1] + ( ( i & 2 ) ? 15.0f : -15.0f );
		corner[2] = cent->lerpOrigin[2] + ( ( i & 4 ) ? 40.0f : -24.0f );
		if ( DotProduct( corner, plane ) > plane[3] ) {
			above++;
		} else {
			below++;
		}
	}
	if ( !keep && ( !above || !below ) ) {
		return qfalse;
	}
	// the side of his feet stays with the body
	if ( keep ) {
		VectorCopy( keep, feet );
	} else {
		VectorCopy( cent->lerpOrigin, feet );
		feet[2] -= 22.0f;
	}
	if ( DotProduct( feet, plane ) < plane[3] ) {
		VectorScale( plane, -1.0f, plane );
		plane[3] = -plane[3];
	}
	for ( i = 0; i < SLICE_MAX; i++ ) {
		if ( slices[i].active && slices[i].body == body ) {
			CG_SliceFree( &slices[i] );		// (a new death of his while the old body is still his)
		}
	}
	s = CG_SliceNew();
	s->active = qtrue;
	s->victim = victim;
	s->killer = killer;
	s->body = body;
	if ( body != victim ) {
		s->copied = cg.time;		// (the copy is there already)
		s->seen = qtrue;
	}
	s->nextCut = cg.time + SLICE_CUT_PAUSE;
	s->time = cg.time;
	s->rag = -1;
	s->alpha = 1.0f;
	VectorCopy4( plane, s->plane );
	VectorCopy( sweep, s->sweep );
	s->bloodSeed = Q_flrand( 0.0f, 1.0f );
	s->bloodRadius = Q_flrand( 18.0f, 26.0f );
	return qtrue;
}

/*
=================
CG_SliceKill

EV_OBITUARY with MOD_SABER (cg_event.c): if a blade of the killer passed through the body of the victim in the last
moment, the body is cut along that swing
=================
*/
void CG_SliceKill( int victim, int killer ) {
	qboolean	tell;
	vec3_t		base, tip, sweep, axis, across;
	vec4_t		plane;
	char		value[8];

	if ( !CG_SliceOn() || victim < 0 || victim >= MAX_CLIENTS || killer < 0 || killer >= MAX_CLIENTS || victim == killer ) {
		return;
	}
	// (developer 1 says why a saber kill was not cut)
	trap->Cvar_VariableStringBuffer( "developer", value, sizeof( value ) );
	tell = (qboolean)( atoi( value ) != 0 );
	if ( !CG_SaberCutNear( &cgs.clientinfo[killer], cg_entities[victim].lerpOrigin, SLICE_REACH, base, tip, sweep ) ) {
		if ( tell ) {
			Com_Printf( "v_slice: client %i not cut, no blade of client %i came within %.0f units in the last 600 ms\n", victim,
				killer, SLICE_REACH );
		}
		return;
	}
	// the plane of the blade and of the way it swept; a stab (along the blade) cuts nothing
	VectorSubtract( tip, base, axis );
	if ( VectorNormalize( axis ) < 1.0f ) {
		return;
	}
	VectorMA( sweep, -DotProduct( sweep, axis ), axis, across );
	if ( VectorNormalize( across ) < 1.0f ) {
		if ( tell ) {
			Com_Printf( "v_slice: client %i not cut, a stab\n", victim );
		}
		return;
	}
	CrossProduct( axis, across, plane );
	VectorNormalize( plane );
	plane[3] = DotProduct( plane, base );
	if ( !CG_SliceStart( victim, killer, plane, across ) ) {
		if ( tell ) {
			Com_Printf( "v_slice: client %i not cut, the plane of the swing misses his body\n", victim );
		}
	} else if ( tell ) {
		Com_Printf( "v_slice: client %i cut by client %i\n", victim, killer );
	}
}

/*
=================
CG_SliceTest_f

"v_sliceTest [pitch yaw [height]]" (cheats): the nearest dead player cut through his middle by a plane whose normal has
these angles (default: across the waist), height units above it (e.g. 20 the chest, 34 the neck), for the tests of
hidden runs
=================
*/
void CG_SliceTest_f( void ) {
	vec3_t	angles = { -90.0f, 0.0f, 0.0f }, normal, sweep, up = { 0.0f, 0.0f, 1.0f };
	vec4_t	plane;
	float	best = 1024.0f, height = 0.0f;
	int		i, victim = -1;
	char	value[8];

	trap->Cvar_VariableStringBuffer( "sv_cheats", value, sizeof( value ) );
	if ( !atoi( value ) ) {
		Com_Printf( "v_sliceTest needs cheats\n" );
		return;
	}
	if ( trap->Cmd_Argc() >= 3 ) {
		angles[PITCH] = atof( CG_Argv( 1 ) );
		angles[YAW] = atof( CG_Argv( 2 ) );
	}
	if ( trap->Cmd_Argc() >= 4 ) {
		height = atof( CG_Argv( 3 ) );
	}
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const centity_t *cent = &cg_entities[i];
		const float d = Distance( cent->lerpOrigin, cg.refdef.vieworg );

		// (the own player is not in the entities of the snapshot, he is predicted)
		if ( ( cent->currentValid || ( cg.snap && i == cg.snap->ps.clientNum ) ) && ( cent->currentState.eFlags & EF_DEAD ) && d < best ) {
			best = d;
			victim = i;
		}
	}
	if ( victim < 0 ) {
		Com_Printf( "v_sliceTest: no dead player near\n" );
		return;
	}
	AngleVectors( angles, normal, NULL, NULL );
	VectorCopy( normal, plane );
	plane[3] = DotProduct( normal, cg_entities[victim].lerpOrigin ) + ( 4.0f + height ) * normal[2];
	CrossProduct( normal, up, sweep );
	if ( VectorNormalize( sweep ) < 0.1f ) {
		VectorSet( sweep, 1.0f, 0.0f, 0.0f );
	}
	Com_Printf( "v_sliceTest: client %i %s\n", victim, CG_SliceStart( victim, -1, plane, sweep ) ? "cut" : "missed" );
}

// The colour of the cut face: orange hot, then red, and dark (the afterglow). It started white hot, and a body cut
// lengthwise shows a large face: the whole half looked on fire.
static void CG_SliceGlow( const slice_t *s, byte *rgba ) {
	static const float stops[5][4] = {
		{ 0.00f, 230.0f, 140.0f, 50.0f },
		{ 0.08f, 210.0f, 90.0f, 20.0f },
		{ 0.30f, 170.0f, 40.0f, 8.0f },
		{ 0.65f, 90.0f, 10.0f, 2.0f },
		{ 1.00f, 35.0f, 4.0f, 2.0f },
	};
	const float	age = Com_Clamp( 0.0f, 1.0f, (float)( cg.time - s->time ) / SLICE_GLOW_MSEC );
	int			i;

	for ( i = 0; i < 4 && age > stops[i + 1][0]; i++ ) {
	}
	{
		const float f = Com_Clamp( 0.0f, 1.0f, ( age - stops[i][0] ) / Q_max( stops[i + 1][0] - stops[i][0], 0.001f ) );

		rgba[0] = (byte)( stops[i][1] + ( stops[i + 1][1] - stops[i][1] ) * f );
		rgba[1] = (byte)( stops[i][2] + ( stops[i + 1][2] - stops[i][2] ) * f );
		rgba[2] = (byte)( stops[i][3] + ( stops[i + 1][3] - stops[i][3] ) * f );
		rgba[3] = 255;
	}
}

// a part of the body cut by plane, and its cut face
static void CG_SliceAdd( const slice_t *s, const refEntity_t *ent, const vec4_t plane ) {
	refEntity_t face;

	trap->ext.R_AddCutEntity( ent, plane );
	face = *ent;
	face.customShader = sliceCutShader;
	face.customSkin = 0;
	face.renderfx &= ~( RF_FORCE_ENT_ALPHA | RF_RGB_TINT | RF_SHADOW_PLANE | RF_MINLIGHT );
	CG_SliceGlow( s, face.shaderRGBA );
	if ( s->alpha < 1.0f ) {
		face.shaderRGBA[0] = (byte)( face.shaderRGBA[0] * s->alpha );
		face.shaderRGBA[1] = (byte)( face.shaderRGBA[1] * s->alpha );
		face.shaderRGBA[2] = (byte)( face.shaderRGBA[2] * s->alpha );
	}
	trap->ext.R_AddCutEntity( &face, plane );
}

// a mark of blood on the level: a free one, else the oldest. Returns its index.
static int CG_SliceAddMark( int owner, int kind, const vec3_t origin, const vec3_t normal, const vec3_t hit, float radius,
	float width, float strength ) {
	sliceMark_t	*m = &sliceMarks[0];
	int			i;

	for ( i = 0; i < SLICE_MARKS; i++ ) {
		if ( !sliceMarks[i].active ) {
			m = &sliceMarks[i];
			break;
		}
		if ( sliceMarks[i].born < m->born ) {
			m = &sliceMarks[i];
		}
	}
	memset( m, 0, sizeof( *m ) );
	m->active = qtrue;
	m->owner = owner;
	m->born = cg.time;
	m->kind = kind;
	VectorCopy( origin, m->origin );
	VectorCopy( normal, m->normal );
	VectorCopy( hit, m->hit );
	m->radius = radius;
	m->width = width;
	m->strength = strength;
	m->seed = Q_flrand( 0.0f, 1.0f );
	return (int)( m - sliceMarks );
}

static void CG_SliceAddDrop( int owner, const vec3_t origin, const vec3_t velocity, float size ) {
	sliceDrop_t	*d = &sliceDrops[0];
	int			i;

	for ( i = 0; i < SLICE_DROPS; i++ ) {
		if ( !sliceDrops[i].active ) {
			d = &sliceDrops[i];
			break;
		}
		if ( sliceDrops[i].born < d->born ) {
			d = &sliceDrops[i];
		}
	}
	d->active = qtrue;
	d->owner = owner;
	d->born = cg.time;
	VectorCopy( origin, d->origin );
	VectorCopy( velocity, d->velocity );
	d->size = size;
}

// The blood thrown out of the cut as it is made: drops along the swing and out of both cut faces, which land as
// splats on floors and walls, and the stock spray of a saber hit in the air
static void CG_SliceSpray( slice_t *s, const vec3_t centre ) {
	const int	owner = (int)( s - slices );
	const int	count = 12 + rand() % 9;
	int			i;

	trap->FX_PlayEffectID( cgs.effects.mSaberBloodSparks, (float *)centre, s->sweep, -1, -1, qfalse );
	for ( i = 0; i < count; i++ ) {
		vec3_t at, v;

		VectorSet( at, centre[0] + Q_flrand( -4.0f, 4.0f ), centre[1] + Q_flrand( -4.0f, 4.0f ), centre[2] + Q_flrand( -4.0f, 4.0f ) );
		// (the blade carries most of it along; the rest out of either face of the cut)
		VectorScale( s->sweep, Q_flrand( 60.0f, 260.0f ), v );
		VectorMA( v, Q_flrand( -140.0f, 140.0f ), s->plane, v );
		v[0] += Q_flrand( -60.0f, 60.0f );
		v[1] += Q_flrand( -60.0f, 60.0f );
		v[2] += Q_flrand( 40.0f, 170.0f );
		CG_SliceAddDrop( owner, at, v, Q_flrand( 0.6f, 1.4f ) );
	}
}

// Blood of a cut on the own view (screenfx/cg_screenfx.c): the own cut up close, any cut near that is looked at, and
// the own death by a cut
static void CG_SliceBloodOnView( const slice_t *s, const vec3_t centre ) {
	const int	me = cg.snap ? cg.snap->ps.clientNum : -1;
	vec3_t		dir;
	float		dist, strength = 0.0f;

	VectorSubtract( centre, cg.refdef.vieworg, dir );
	dist = VectorNormalize( dir );
	if ( s->victim == me ) {
		strength = 0.9f;
	} else if ( s->killer == me && dist < 128.0f ) {
		strength = Q_max( 0.3f, 1.0f - dist / 128.0f );
	} else if ( dist < 96.0f && DotProduct( dir, cg.refdef.viewaxis[0] ) > 0.3f ) {
		strength = 0.6f * ( 1.0f - dist / 96.0f );
	}
	if ( strength > 0.05f ) {
		CG_ScreenFxBlood( centre, strength );
	}
}

/*
=================
CG_SliceDrawBody

Where the body of cent would be drawn (the dead player in CG_Player, the copy of his body in CG_General), with ent as
it would be drawn and angles its ghoul2 is drawn with. qtrue: it was cut, do not draw it (nor anything that shows the
whole body); the first time it is split into its parts here. qfalse: not cut, draw it as always.
=================
*/
qboolean CG_SliceDrawBody( centity_t *cent, refEntity_t *ent, const vec3_t angles ) {
	const int	num = (int)( cent - cg_entities );
	slice_t		*s;
	vec3_t		centre, velocity, down;
	trace_t		tr;

	if ( !CG_SliceOn() || !( s = CG_SliceOf( num ) ) ) {
		return qfalse;
	}
	if ( s->rag >= 0 || s->expired ) {
		return qtrue;
	}
	if ( !ent->ghoul2 ) {
		return qfalse;
	}
	// the middle of the cut, the speed of the body
	VectorMA( cent->lerpOrigin, -( DotProduct( cent->lerpOrigin, s->plane ) - s->plane[3] ), s->plane, centre );
	if ( cg.snap && num == cg.snap->ps.clientNum ) {
		VectorCopy( cg.predictedPlayerState.velocity, velocity );
	} else if ( cent->currentState.pos.trType != TR_STATIONARY ) {
		VectorCopy( cent->currentState.pos.trDelta, velocity );
	} else {
		VectorClear( velocity );
	}
	s->rag = CG_RagSplit( ent->ghoul2, angles, ent->origin, cent->modelScale, s->plane, centre, velocity, s->sweep );
	if ( s->rag < 0 ) {
		CG_SliceFree( s );		// (no humanoid skeleton: the stock death)
		return qfalse;
	}
	s->ent = *ent;
	s->ent.ghoul2 = NULL;
	VectorCopy( cent->modelScale, s->ent.modelScale );		// (the scale the ragdoll placed the bones for)
	s->ent.renderfx &= ~( RF_THIRD_PERSON | RF_FIRST_PERSON | RF_FORCE_ENT_ALPHA | RF_RGB_TINT );
	s->ent.renderfx |= RF_LIGHTING_ORIGIN;
	s->ent.shaderRGBA[3] = 255;

	// the blood runs out under the cut
	VectorCopy( centre, down );
	down[2] -= 160.0f;
	CG_Trace( &tr, centre, NULL, NULL, down, num, MASK_SOLID );
	if ( tr.fraction < 1.0f && tr.plane.normal[2] > 0.7f ) {
		s->blood = qtrue;
		VectorCopy( tr.endpos, s->bloodOrigin );
		VectorCopy( tr.plane.normal, s->bloodNormal );
	}
	{
		int k;

		for ( k = 0; k < SLICE_PARTS; k++ ) {
			s->streakMark[k] = -1;
			s->dripLeft[k] = Q_flrand( 8.0f, 20.0f );
			VectorCopy( centre, s->dripLast[k] );
		}
	}
	if ( s->gib ) {
		// burst by an explosion: two to four more of the limbs off at their joints, and every part thrown from it
		static const int limbs[] = { 2, 3, 4, 7, 8, 5, 6, 9, 10 };	// (cg_ragdoll.c ragJointDefs: neck, shoulders, hips, elbows, knees)
		int		order[ARRAY_LEN( limbs )], want = 2 + rand() % 3, cut = 0, j;
		vec3_t	away;

		for ( j = 0; j < (int)ARRAY_LEN( limbs ); j++ ) {
			order[j] = limbs[j];
		}
		for ( j = (int)ARRAY_LEN( limbs ) - 1; j > 0; j-- ) {
			const int r = rand() % ( j + 1 ), t = order[j];

			order[j] = order[r];
			order[r] = t;
		}
		VectorSubtract( centre, s->gibOrigin, away );
		away[2] = Q_max( away[2], 0.0f ) + 16.0f;
		VectorNormalize( away );
		for ( j = 0; j < (int)ARRAY_LEN( limbs ) && cut < want; j++ ) {
			const int n = CG_RagCutJoint( s->rag, order[j], away, 90.0f );

			if ( n >= 0 && n < SLICE_PARTS ) {
				CG_RagJoint( s->rag, order[j], s->dripLast[n], NULL );
				cut++;
			}
		}
		s->cuts = SLICE_MAX_CUTS;		// (in pieces already)
		CG_RagExplosionScaled( s->gibOrigin, 160.0f, 0.45f );
	}
	if ( CG_GoreAir() ) {
		CG_SliceSpray( s, centre );
		if ( s->gib ) {
			CG_SliceSpray( s, centre );
		}
		CG_SliceBloodOnView( s, centre );
	}
	return qtrue;
}

// ------------------------------------------------------------------------------------------------ cuts after the kill

// A client who swings a lit saber now: his blade history may cut a body that lies there
static qboolean CG_SliceSwinging( int client ) {
	const centity_t	*cent = &cg_entities[client];
	int				move, holstered, weapon;

	if ( !cgs.clientinfo[client].infoValid ) {
		return qfalse;
	}
	if ( cg.snap && client == cg.snap->ps.clientNum ) {
		if ( cg.predictedPlayerState.stats[STAT_HEALTH] <= 0 ) {
			return qfalse;
		}
		move = cg.predictedPlayerState.saberMove;
		holstered = cg.predictedPlayerState.saberHolstered;
		weapon = cg.predictedPlayerState.weapon;
	} else {
		if ( !cent->currentValid || ( cent->currentState.eFlags & EF_DEAD ) ) {
			return qfalse;
		}
		move = cent->currentState.saberMove;
		holstered = cent->currentState.saberHolstered;
		weapon = cent->currentState.weapon;
	}
	return (qboolean)( weapon == WP_SABER && holstered < 2 && ( BG_SaberInAttack( move ) || BG_SaberInSpecial( move ) ) );
}

static void CG_SliceSwingOrigin( int client, vec3_t out ) {
	if ( cg.snap && client == cg.snap->ps.clientNum ) {
		VectorCopy( cg.predictedPlayerState.origin, out );
	} else {
		VectorCopy( cg_entities[client].lerpOrigin, out );
	}
}

// the plane of a blade and the way it swept (as CG_SliceKill), qfalse for a stab
static qboolean CG_SlicePlane( const vec3_t base, const vec3_t tip, const vec3_t sweep, vec4_t plane, vec3_t across ) {
	vec3_t axis;

	VectorSubtract( tip, base, axis );
	if ( VectorNormalize( axis ) < 1.0f ) {
		return qfalse;
	}
	VectorMA( sweep, -DotProduct( sweep, axis ), axis, across );
	if ( VectorNormalize( across ) < 1.0f ) {
		return qfalse;
	}
	CrossProduct( axis, across, plane );
	VectorNormalize( plane );
	plane[3] = DotProduct( plane, base );
	return qtrue;
}

// a bolt of a model as it is drawn now
static qboolean CG_SliceBolt( centity_t *cent, const char *name, vec3_t out ) {
	mdxaBone_t	m;
	const int	bolt = cent->ghoul2 ? trap->G2API_AddBolt( cent->ghoul2, 0, name ) : -1;

	if ( bolt < 0 || !trap->G2API_GetBoltMatrix( cent->ghoul2, 0, bolt, &m, cent->lerpAngles, cent->lerpOrigin, cg.time,
		cgs.gameModels, cent->modelScale ) ) {
		return qfalse;
	}
	BG_GiveMeVectorFromMatrix( &m, ORIGIN, out );
	return qtrue;
}

/*
=================
CG_SliceLying

Every frame (CG_SliceScene): sabers swung through bodies that lie there after the kill (their wish 2026-11-15). A dead
player or a body the server left, not cut yet, is cut along the swing that went through it; a cut body is cut again at
the joint a blade went through, up to SLICE_MAX_CUTS joints, each part a ragdoll of its own.
=================
*/
static void CG_SliceLying( void ) {
	int		swingers[MAX_CLIENTS], numSwingers = 0, i, k, j;
	vec3_t	swingAt[MAX_CLIENTS];

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( CG_SliceSwinging( i ) ) {
			CG_SliceSwingOrigin( i, swingAt[numSwingers] );
			swingers[numSwingers++] = i;
		}
	}
	if ( !numSwingers || !cg.snap ) {
		return;
	}

	// bodies not cut yet: the dead players and the bodies the server left
	for ( i = -1; i < cg.snap->numEntities; i++ ) {
		const int	num = i < 0 ? -1 : cg.snap->entities[i].number;
		centity_t	*cent;
		vec3_t		pelvis, feet, base, tip, sweep, across;
		vec4_t		plane;
		int			victim;

		if ( num < 0 ) {
			continue;
		}
		cent = &cg_entities[num];
		if ( !cent->ghoul2 || CG_Sliced( num ) ) {
			continue;
		}
		if ( num < MAX_CLIENTS && cent->currentState.eType == ET_PLAYER && ( cent->currentState.eFlags & EF_DEAD ) ) {
			victim = num;
		} else if ( cent->currentState.eType == ET_BODY && !cent->bodyFadeTime ) {
			victim = cent->currentState.clientNum;
			if ( victim < 0 || victim >= MAX_CLIENTS ) {
				continue;
			}
		} else {
			continue;
		}
		for ( k = 0; k < numSwingers; k++ ) {
			if ( swingers[k] == victim || DistanceSquared( swingAt[k], cent->lerpOrigin ) > 200.0f * 200.0f ) {
				continue;
			}
			if ( !CG_SliceBolt( cent, "pelvis", pelvis ) || !CG_SliceBolt( cent, "ltalus", feet ) ) {
				break;
			}
			if ( CG_SaberCutNearWithin( &cgs.clientinfo[swingers[k]], pelvis, SLICE_LYING_REACH, SLICE_LYING_MSEC, base, tip, sweep )
				&& CG_SlicePlane( base, tip, sweep, plane, across ) ) {
				vec3_t chest, axis;

				// A swing along a lying body would split it lengthwise: cut it across the waist instead.
				if ( CG_SliceBolt( cent, "thoracic", chest ) ) {
					VectorSubtract( chest, pelvis, axis );
					if ( VectorNormalize( axis ) > 1.0f && fabsf( DotProduct( plane, axis ) ) < 0.5f ) {
						VectorCopy( axis, plane );
						plane[3] = DotProduct( plane, pelvis );
					}
				}
				CG_SliceStartAt( victim, num, swingers[k], plane, across, feet );
				break;
			}
		}
	}

	// cut bodies: a blade through one of the joints of a part
	for ( i = 0; i < SLICE_MAX; i++ ) {
		slice_t *s = &slices[i];

		if ( !s->active || s->rag < 0 || s->expired || s->cuts >= SLICE_MAX_CUTS || cg.time < s->nextCut ) {
			continue;
		}
		for ( j = 0; j < CG_RagJoints(); j++ ) {
			vec3_t	at, base, tip, sweep;
			int		part, n = -1;

			if ( !CG_RagJoint( s->rag, j, at, &part ) ) {
				continue;
			}
			for ( k = 0; k < numSwingers && n < 0; k++ ) {
				if ( DistanceSquared( swingAt[k], at ) > 150.0f * 150.0f
					|| !CG_SaberCutNearWithin( &cgs.clientinfo[swingers[k]], at, SLICE_JOINT_REACH, SLICE_LYING_MSEC, base, tip, sweep ) ) {
					continue;
				}
				if ( VectorNormalize( sweep ) < 0.01f ) {
					VectorSet( sweep, 0.0f, 0.0f, 1.0f );
				}
				n = CG_RagCutJoint( s->rag, j, sweep, 140.0f );
				if ( n >= 0 ) {
					s->cuts++;
					s->nextCut = cg.time + SLICE_CUT_PAUSE;
					if ( n < SLICE_PARTS ) {
						s->streakMark[n] = -1;
						s->dripLeft[n] = Q_flrand( 8.0f, 20.0f );
						VectorCopy( at, s->dripLast[n] );
					}
					if ( CG_GoreAir() ) {
						VectorCopy( sweep, s->sweep );
						CG_SliceSpray( s, at );
					}
				}
			}
			if ( n >= 0 ) {
				break;		// (one joint a frame)
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------ explosions

static struct {
	vec3_t	origin;
	int		time;
} sliceBlasts[8];
static int	sliceNextBlast;
static int	sliceBlastDeath[MAX_CLIENTS];	// cg.time of a death by an explosion whose explosion has not come yet

static qboolean CG_SliceBlastMod( int mod ) {
	switch ( mod ) {
	case MOD_REPEATER_ALT:
	case MOD_REPEATER_ALT_SPLASH:
	case MOD_FLECHETTE_ALT_SPLASH:
	case MOD_ROCKET:
	case MOD_ROCKET_SPLASH:
	case MOD_ROCKET_HOMING:
	case MOD_ROCKET_HOMING_SPLASH:
	case MOD_THERMAL:
	case MOD_THERMAL_SPLASH:
	case MOD_TRIP_MINE_SPLASH:
	case MOD_TIMED_MINE_SPLASH:
	case MOD_DET_PACK_SPLASH:
	case MOD_CONC:
	case MOD_CONC_ALT:
		return qtrue;
	default:
		return qfalse;
	}
}

// the body of victim bursts by the explosion at origin: cut through the waist, the rest at the split (CG_SliceDrawBody)
static void CG_SliceBurst( int victim, const vec3_t origin ) {
	const centity_t	*cent = &cg_entities[victim];
	vec4_t			plane;
	vec3_t			feet, away;
	int				i;

	VectorSet( plane, Q_flrand( -0.3f, 0.3f ), Q_flrand( -0.3f, 0.3f ), 1.0f );
	VectorNormalize( plane );
	plane[3] = DotProduct( plane, cent->lerpOrigin ) + Q_flrand( -4.0f, 8.0f );
	VectorCopy( cent->lerpOrigin, feet );
	feet[2] -= 22.0f;
	VectorSubtract( cent->lerpOrigin, origin, away );
	away[2] = Q_max( away[2], 0.0f ) + 16.0f;
	VectorNormalize( away );
	if ( !CG_SliceStartAt( victim, victim, -1, plane, away, NULL ) ) {
		return;
	}
	for ( i = 0; i < SLICE_MAX; i++ ) {
		if ( slices[i].active && slices[i].body == victim ) {
			slices[i].gib = qtrue;
			VectorCopy( origin, slices[i].gibOrigin );
		}
	}
}

/*
=================
CG_SliceBlastKill

EV_OBITUARY (cg_event.c): a kill by an explosion that went off right at the body bursts it into pieces, if that
explosion was seen here a moment ago; else it is waited for (CG_SliceExplosion).
=================
*/
void CG_SliceBlastKill( int victim, int mod ) {
	int i;

	if ( !CG_SliceOn() || victim < 0 || victim >= MAX_CLIENTS || !CG_SliceBlastMod( mod ) ) {
		return;
	}
	for ( i = 0; i < (int)ARRAY_LEN( sliceBlasts ); i++ ) {
		if ( sliceBlasts[i].time && cg.time - sliceBlasts[i].time <= SLICE_BLAST_MSEC && cg.time >= sliceBlasts[i].time
			&& Distance( sliceBlasts[i].origin, cg_entities[victim].lerpOrigin ) <= SLICE_BLAST_REACH ) {
			CG_SliceBurst( victim, sliceBlasts[i].origin );
			return;
		}
	}
	sliceBlastDeath[victim] = cg.time;
}

/*
=================
CG_SliceExplosion

An explosion went off at origin (cg_weapons.c): kept a moment for the kill message, and a death waiting for it bursts
=================
*/
void CG_SliceExplosion( const vec3_t origin ) {
	int i;

	VectorCopy( origin, sliceBlasts[sliceNextBlast].origin );
	sliceBlasts[sliceNextBlast].time = cg.time;
	sliceNextBlast = ( sliceNextBlast + 1 ) % ARRAY_LEN( sliceBlasts );
	if ( !CG_SliceOn() ) {
		return;
	}
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( sliceBlastDeath[i] && cg.time - sliceBlastDeath[i] <= SLICE_BLAST_MSEC && cg.time >= sliceBlastDeath[i]
			&& Distance( origin, cg_entities[i].lerpOrigin ) <= SLICE_BLAST_REACH ) {
			sliceBlastDeath[i] = 0;
			CG_SliceBurst( i, origin );
		}
	}
}

// The copy of the body of clientNum the server leaves when he comes back (CG_BodyQueueCopy, body is its entity): the
// parts belong to it now. A cut whose body copy the server used again for another body is over.
void CG_SliceBodyCopied( int clientNum, int body ) {
	int i;

	for ( i = 0; i < SLICE_MAX; i++ ) {
		slice_t *s = &slices[i];

		if ( s->active && s->body == body && body != s->victim && !s->gone ) {
			s->gone = cg.time;
		}
	}
	for ( i = 0; i < SLICE_MAX; i++ ) {
		slice_t *s = &slices[i];

		// (also when his coming back was seen before the copy)
		if ( s->active && !s->gone && s->victim == clientNum && ( s->body == clientNum || s->body < 0 ) ) {
			s->body = body;
			s->copied = cg.time;
			s->seen = qfalse;
		}
	}
}

// A cut part bleeds: in the air it drips from its cut (a drop every so many units it flies), dragged or pushed over a
// floor it smears a streak behind its body at the cut, joined piece to piece; both less and less as it bleeds out
static void CG_SliceBleed( slice_t *s ) {
	const int	owner = (int)( s - slices );
	const float	bleed = 1.0f - (float)( cg.time - s->time ) / SLICE_BLEED_MSEC;
	int			n;

	if ( bleed <= 0.0f || s->gone ) {
		return;
	}
	for ( n = 0; n < CG_RagParts( s->rag ) && n < SLICE_PARTS; n++ ) {
		vec3_t		cut, velocity, floorAt;
		float		slide;
		const qboolean onFloor = CG_RagBleed( s->rag, n, cut, velocity, floorAt, &slide );
		if ( !onFloor ) {
			s->streakOn[n] = qfalse;
			s->streakMark[n] = -1;
			s->airSpeed[n] = VectorLength( velocity );
			// drips while it flies
			if ( VectorLength( velocity ) > 60.0f && bleed > 0.3f ) {
				s->dripLeft[n] -= Distance( cut, s->dripLast[n] );
				if ( s->dripLeft[n] <= 0.0f ) {
					vec3_t v;

					VectorScale( velocity, 0.4f, v );
					v[0] += Q_flrand( -20.0f, 20.0f );
					v[1] += Q_flrand( -20.0f, 20.0f );
					CG_SliceAddDrop( owner, cut, v, Q_flrand( 0.5f, 0.9f ) );
					s->dripLeft[n] = Q_flrand( 14.0f, 26.0f );
				}
			}
			VectorCopy( cut, s->dripLast[n] );
			continue;
		}
		VectorCopy( cut, s->dripLast[n] );
		// a hard landing splashes where it hit
		if ( s->airSpeed[n] > 120.0f ) {
			const vec3_t up = { 0.0f, 0.0f, 1.0f };

			CG_SliceAddMark( owner, WET_BLOOD, floorAt, up, floorAt, Com_Clamp( 3.0f, 9.0f, s->airSpeed[n] / 40.0f ) * ( 0.4f + 0.6f * bleed ),
				0.0f, 1.0f );
		}
		s->airSpeed[n] = 0.0f;
		if ( !s->streakOn[n] ) {
			if ( slide > 8.0f ) {
				s->streakOn[n] = qtrue;
				s->streakMark[n] = -1;
				VectorCopy( floorAt, s->streakLast[n] );
			}
			continue;
		}
		if ( Distance( floorAt, s->streakLast[n] ) < 3.0f ) {
			continue;
		}
		{
			const vec3_t	up = { 0.0f, 0.0f, 1.0f };
			const float		width = 2.5f + 3.5f * bleed;
			sliceMark_t		*m = s->streakMark[n] >= 0 ? &sliceMarks[s->streakMark[n]] : NULL;
			vec3_t			mid, dirNew, dirOld, start;

			VectorSubtract( floorAt, s->streakLast[n], dirNew );
			VectorNormalize( dirNew );
			// the same piece goes on while the way stays straight and the piece short; else a new piece from its end
			if ( m && m->active && m->owner == owner && m->kind == WET_BLOOD_STREAK ) {
				VectorScale( m->origin, 2.0f, start );
				VectorSubtract( start, m->hit, start );
				VectorSubtract( m->hit, start, dirOld );
				if ( VectorNormalize( dirOld ) > 0.0f && DotProduct( dirOld, dirNew ) > 0.96f && Distance( start, floorAt ) < 56.0f ) {
					VectorAdd( start, floorAt, mid );
					VectorScale( mid, 0.5f, m->origin );
					VectorCopy( floorAt, m->hit );
					m->radius = 0.5f * Distance( start, floorAt ) + m->width;
					VectorCopy( floorAt, s->streakLast[n] );
					continue;
				}
			}
			VectorAdd( s->streakLast[n], floorAt, mid );
			VectorScale( mid, 0.5f, mid );
			s->streakMark[n] = CG_SliceAddMark( owner, WET_BLOOD_STREAK, mid, up, floorAt,
				0.5f * Distance( s->streakLast[n], floorAt ) + width, width, 0.4f + 0.6f * bleed );
			VectorCopy( floorAt, s->streakLast[n] );
		}
	}
}

// the drops of blood in flight fall, and where one hits the level it leaves a splat, on a floor or a wall
static void CG_SliceDrops( void ) {
	const float	dt = Com_Clamp( 0.0f, 0.1f, cg.frametime * 0.001f );
	const float	gravity = cg.predictedPlayerState.gravity > 0 ? (float)cg.predictedPlayerState.gravity : 800.0f;
	int			i;

	for ( i = 0; i < SLICE_DROPS; i++ ) {
		sliceDrop_t	*d = &sliceDrops[i];
		vec3_t		to;
		trace_t		tr;
		const vec3_t mins = { -0.5f, -0.5f, -0.5f }, maxs = { 0.5f, 0.5f, 0.5f };

		if ( !d->active ) {
			continue;
		}
		if ( cg.time - d->born > 3000 || cg.time < d->born ) {
			d->active = qfalse;
			continue;
		}
		d->velocity[2] -= gravity * dt;
		VectorMA( d->origin, dt, d->velocity, to );
		CG_Trace( &tr, d->origin, mins, maxs, to, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.startsolid ) {
			d->active = qfalse;
			continue;
		}
		if ( tr.fraction < 1.0f ) {
			// the faster it hit, the wider it splashed
			const float	speed = VectorLength( d->velocity );
			const float	radius = Com_Clamp( 1.5f, 7.0f, d->size * ( 1.5f + speed / 160.0f ) );

			CG_SliceAddMark( d->owner, WET_BLOOD, tr.endpos, tr.plane.normal, tr.endpos, radius, 0.0f, 1.0f );
			d->active = qfalse;
			continue;
		}
		VectorCopy( to, d->origin );
	}
}

// The marks of blood into the wet spots of the frame after the pools, the newest first while there is room. A splat
// spreads in a moment; all of them fade with their body.
static int CG_SliceMarkSpots( wetSpot_t *spots, int numSpots ) {
	int order[SLICE_MARKS], count = 0, i, j;

	for ( i = 0; i < SLICE_MARKS; i++ ) {
		if ( sliceMarks[i].active ) {
			order[count++] = i;
		}
	}
	// newest first (a few dozen: a simple sort)
	for ( i = 1; i < count; i++ ) {
		const int k = order[i];

		for ( j = i - 1; j >= 0 && sliceMarks[order[j]].born < sliceMarks[k].born; j-- ) {
			order[j + 1] = order[j];
		}
		order[j + 1] = k;
	}
	for ( i = 0; i < count && numSpots < SLICE_SPOTS; i++ ) {
		const sliceMark_t	*m = &sliceMarks[order[i]];
		const slice_t		*s = &slices[m->owner];
		wetSpot_t			*w = &spots[numSpots++];
		const float			grow = Com_Clamp( 0.0f, 1.0f, (float)( cg.time - m->born ) / 250.0f );

		memset( w, 0, sizeof( *w ) );
		VectorCopy( m->origin, w->origin );
		VectorCopy( m->normal, w->normal );
		VectorCopy( m->hit, w->hit );
		w->radius = m->radius;
		w->seed = m->seed;
		w->kind = m->kind;
		if ( m->kind == WET_BLOOD_STREAK ) {
			w->puddle = m->width;
			w->wet = m->strength * s->alpha;
		} else {
			w->wet = ( 1.0f - ( 1.0f - grow ) * ( 1.0f - grow ) ) * m->strength * s->alpha;
		}
	}
	return numSpots;
}

/*
=================
CG_SliceStep

A foot of an entity came down at "at" (_PlayerFootStep of cg_players.c, beside CG_WaterStep). Where the way from its last
step went through a pool of blood that has run out the feet get bloody (a running stride is longer than a pool is
wide); out of it a bloody foot leaves a print for SLICE_PRINT_STEPS steps, fainter with each. Only where the blood
lies on the ground (CG_GoreGround).
=================
*/
void CG_SliceStep( centity_t *cent, const vec3_t at, const vec3_t normal, float yaw, qboolean right ) {
	const int		num = (int)( cent - cg_entities );
	slicePrint_t	*print;
	vec3_t			from;
	qboolean		inside = qfalse;
	int				i;

	if ( !CG_SliceOn() || !CG_GoreGround() || normal[2] < 0.7f || num < 0 || num >= MAX_GENTITIES ) {
		return;
	}
	// (from the last step if it was a moment ago and near, else from here)
	if ( cg.time - sliceLastStepTime[num] < 1000 && cg.time >= sliceLastStepTime[num] && Distance( sliceLastStep[num], at ) < 96.0f ) {
		VectorCopy( sliceLastStep[num], from );
	} else {
		VectorCopy( at, from );
	}
	VectorCopy( at, sliceLastStep[num] );
	sliceLastStepTime[num] = cg.time;
	for ( i = 0; i < SLICE_MAX; i++ ) {
		const slice_t	*s = &slices[i];
		vec3_t			way, toPool, nearest;
		float			grow, reach, along, length;

		if ( !s->active || !s->blood || s->gone ) {
			continue;
		}
		grow = Com_Clamp( 0.0f, 1.0f, (float)( cg.time - s->time ) / SLICE_BLOOD_MSEC );
		grow = 1.0f - ( 1.0f - grow ) * ( 1.0f - grow );
		reach = s->bloodRadius * grow * 0.85f;
		if ( fabsf( at[2] - s->bloodOrigin[2] ) > 12.0f ) {
			continue;
		}
		if ( Distance( at, s->bloodOrigin ) < reach ) {
			inside = qtrue;
			break;
		}
		// the way from the last step, the point of it nearest the middle of the pool
		VectorSubtract( at, from, way );
		way[2] = 0.0f;
		length = VectorNormalize( way );
		VectorSubtract( s->bloodOrigin, from, toPool );
		toPool[2] = 0.0f;
		along = Com_Clamp( 0.0f, length, DotProduct( toPool, way ) );
		VectorMA( from, along, way, nearest );
		nearest[2] = s->bloodOrigin[2];
		if ( Distance( nearest, s->bloodOrigin ) < reach ) {
			sliceFeet[num] = 1.0f;
		}
	}
	if ( inside ) {
		sliceFeet[num] = 1.0f;
		return;
	}
	if ( sliceFeet[num] <= 0.0f ) {
		return;
	}
	print = &slicePrints[sliceNextPrint];
	sliceNextPrint = ( sliceNextPrint + 1 ) % SLICE_PRINTS;
	memset( print, 0, sizeof( *print ) );
	print->active = qtrue;
	print->right = right;
	print->born = cg.time;
	print->yaw = yaw;
	print->wet = sliceFeet[num];
	VectorCopy( at, print->origin );
	VectorCopy( normal, print->normal );
	sliceFeet[num] -= 1.0f / SLICE_PRINT_STEPS;
}

// the bloody footprints into the wet places of the frame, fading
static int CG_SlicePrintSpots( wetSpot_t *spots, int numSpots ) {
	int i;

	for ( i = 0; i < SLICE_PRINTS && numSpots < SLICE_SPOTS; i++ ) {
		slicePrint_t	*p = &slicePrints[i];
		const int		age = cg.time - p->born;
		wetSpot_t		*w;

		if ( !p->active ) {
			continue;
		}
		if ( age >= SLICE_PRINT_MSEC || age < 0 ) {
			p->active = qfalse;
			continue;
		}
		w = &spots[numSpots++];
		memset( w, 0, sizeof( *w ) );
		VectorCopy( p->origin, w->origin );
		VectorCopy( p->normal, w->normal );
		VectorCopy( p->origin, w->hit );
		w->radius = SLICE_FOOT;
		w->wet = p->wet * ( 1.0f - (float)age / SLICE_PRINT_MSEC );
		w->seed = AngleNormalize360( p->yaw ) / 360.0f;
		w->kind = p->right ? WET_BLOOD_FOOT_RIGHT : WET_BLOOD_FOOT_LEFT;
	}
	return numSpots;
}

/*
=================
CG_SliceScene

Every frame (cg_view.c): the ragdolls of the parts and the parts, the blood, and the end of the cuts whose body is gone
=================
*/
void CG_SliceScene( void ) {
	wetSpot_t	spots[SLICE_SPOTS];
	int			i, n, numSpots = 0;

	for ( i = 0; i < SLICE_MAX; i++ ) {
		slice_t			*s = &slices[i];
		const centity_t	*victim;

		if ( !s->active ) {
			continue;
		}
		if ( cg.time < s->time ) {
			CG_SliceFree( s );		// (time went back: a demo was wound back)
			continue;
		}
		victim = &cg_entities[s->victim];
		// the victim alive again (the dead flag may come a moment after the kill)
		if ( !s->revived && cg.time - s->time > 1000 && ( victim->currentValid || ( cg.snap && s->victim == cg.snap->ps.clientNum ) )
			&& !( victim->currentState.eFlags & EF_DEAD ) ) {
			s->revived = qtrue;
			if ( s->body == s->victim ) {
				s->body = -1;		// his body copy may still come
			}
		}
		// the body copy went, or never came
		if ( !s->gone ) {
			if ( s->body >= 0 && s->body != s->victim ) {
				const centity_t	*body = &cg_entities[s->body];
				const qboolean	there = (qboolean)( body->currentValid && body->currentState.eType == ET_BODY );

				// (the snapshot with the copy may come a moment after the command that names it)
				if ( there ) {
					s->seen = qtrue;
				} else if ( s->seen || cg.time - s->copied > 3000 ) {
					s->gone = cg.time;
				}
			} else if ( s->body < 0 && cg.time - s->time > SLICE_LIFE_MSEC ) {
				s->gone = cg.time - SLICE_ORPHAN_MSEC;
			}
		}
		// fading with the body, or a while after it went
		s->alpha = 1.0f;
		if ( s->gone ) {
			const int left = s->gone + SLICE_ORPHAN_MSEC + SLICE_FADE_MSEC - cg.time;

			if ( left <= 0 ) {
				CG_SliceFree( s );
				continue;
			}
			s->alpha = Com_Clamp( 0.0f, 1.0f, (float)left / SLICE_FADE_MSEC );
		} else if ( s->body >= 0 && s->body != s->victim && cg_entities[s->body].bodyFadeTime > cg.time ) {
			// (as CG_General fades a body)
			const int since = cg.time + 60000 - cg_entities[s->body].bodyFadeTime;

			s->alpha = Com_Clamp( 0.0f, 1.0f, ( 254.0f - since * 0.08f ) / 255.0f );
			if ( s->alpha <= 0.0f ) {
				CG_SliceFree( s );
				continue;
			}
		}
		// v_sliceTime: the parts and their blood fade that many seconds after the cut, also while the body is still
		// there; the slot stays until the body goes, else it would be drawn whole again
		if ( v_sliceTime.integer > 0 && s->rag >= 0 ) {
			const int left = s->time + v_sliceTime.integer * 1000 + SLICE_FADE_MSEC - cg.time;

			if ( left <= 0 ) {
				CG_SliceClear( s );
				s->expired = qtrue;
			} else {
				s->alpha = Q_min( s->alpha, (float)left / SLICE_FADE_MSEC );
			}
		}
	}

	if ( !CG_SliceOn() ) {
		return;
	}
	CG_SliceLying();
	CG_RagFrame();
	for ( i = 0; i < SLICE_MAX; i++ ) {
		slice_t *s = &slices[i];

		if ( !s->active || s->rag < 0 ) {
			continue;
		}
		for ( n = 0; n < CG_RagParts( s->rag ); n++ ) {
			refEntity_t	ent = s->ent;
			vec4_t		plane;

			if ( !CG_RagPart( s->rag, n, &ent.ghoul2, ent.origin, plane ) ) {
				continue;
			}
			VectorCopy( ent.origin, ent.oldorigin );
			VectorCopy( ent.origin, ent.lightingOrigin );
			// scaled as CG_Player scales a player (the bones were placed for this scale, cg_ragdoll.c)
			AxisClear( ent.axis );
			ent.nonNormalizedAxes = qfalse;
			ScaleModelAxis( &ent );
			if ( s->alpha < 1.0f ) {
				ent.renderfx |= RF_FORCE_ENT_ALPHA;
				ent.shaderRGBA[3] = (byte)( 255.0f * s->alpha );
			}
			if ( ent.shaderRGBA[3] ) {
				CG_SliceAdd( s, &ent, plane );
			}
		}
		if ( CG_GoreGround() ) {
			CG_SliceBleed( s );
		}
		if ( s->blood && CG_GoreGround() && trap->ext.R_AddWet && numSpots < SLICE_SPOTS ) {
			wetSpot_t *w = &spots[numSpots++];
			const float grow = Com_Clamp( 0.0f, 1.0f, (float)( cg.time - s->time ) / SLICE_BLOOD_MSEC );

			memset( w, 0, sizeof( *w ) );
			VectorCopy( s->bloodOrigin, w->origin );
			VectorCopy( s->bloodNormal, w->normal );
			VectorCopy( s->bloodOrigin, w->hit );
			w->radius = s->bloodRadius;
			w->wet = ( 1.0f - ( 1.0f - grow ) * ( 1.0f - grow ) ) * s->alpha;	// fast at first, slower as it spreads
			w->seed = s->bloodSeed;
			w->kind = WET_BLOOD;
		}
	}
	if ( CG_GoreGround() ) {
		CG_SliceDrops();
		numSpots = CG_SliceMarkSpots( spots, numSpots );
		numSpots = CG_SlicePrintSpots( spots, numSpots );
	}
	if ( numSpots ) {
		trap->ext.R_AddWet( spots, numSpots );
	}
}
