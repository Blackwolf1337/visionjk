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
// at first, fading through red to dark (the afterglow). With v_gore 1 blood runs out on the floor under the cut, a wet
// place of the kind WET_BLOOD (ext.R_AddWet). The parts stay as long as the body would, and fade with it.
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
#define SLICE_PARTS			4			// (as RAG_PARTS of cg_ragdoll.c)

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

static slice_t		slices[SLICE_MAX];
static sliceMark_t	sliceMarks[SLICE_MARKS];
static sliceDrop_t	sliceDrops[SLICE_DROPS];
static qhandle_t sliceCutShader;

void CG_SliceInit( void ) {
	int i;

	CG_RagInit();
	memset( slices, 0, sizeof( slices ) );
	memset( sliceMarks, 0, sizeof( sliceMarks ) );
	memset( sliceDrops, 0, sizeof( sliceDrops ) );
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

static void CG_SliceFree( slice_t *s ) {
	const int	owner = (int)( s - slices );
	int			i;

	if ( s->rag >= 0 ) {
		CG_RagFree( s->rag );
	}
	// its blood goes with it
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
static qboolean CG_SliceStart( int victim, int killer, const vec4_t planeIn, const vec3_t sweep ) {
	const centity_t	*cent = &cg_entities[victim];
	vec4_t			plane;
	vec3_t			feet;
	int				i, above = 0, below = 0;
	slice_t			*s;

	VectorCopy4( planeIn, plane );
	// it has to pass through the body: the corners of his box on both sides
	for ( i = 0; i < 8; i++ ) {
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
	if ( !above || !below ) {
		return qfalse;
	}
	// the side of his feet stays with the body
	VectorCopy( cent->lerpOrigin, feet );
	feet[2] -= 22.0f;
	if ( DotProduct( feet, plane ) < plane[3] ) {
		VectorScale( plane, -1.0f, plane );
		plane[3] = -plane[3];
	}
	for ( i = 0; i < SLICE_MAX; i++ ) {
		if ( slices[i].active && slices[i].body == victim ) {
			CG_SliceFree( &slices[i] );		// (a new death of his while the old body is still his)
		}
	}
	s = CG_SliceNew();
	s->active = qtrue;
	s->victim = victim;
	s->killer = killer;
	s->body = victim;
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

"v_sliceTest [pitch yaw]" (cheats): the nearest dead player cut through his middle by a plane whose normal has these
angles (default: across the waist), for the tests of hidden runs
=================
*/
void CG_SliceTest_f( void ) {
	vec3_t	angles = { -90.0f, 0.0f, 0.0f }, normal, sweep, up = { 0.0f, 0.0f, 1.0f };
	vec4_t	plane;
	float	best = 1024.0f;
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
	plane[3] = DotProduct( normal, cg_entities[victim].lerpOrigin ) + 4.0f * normal[2];
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
	if ( s->rag >= 0 ) {
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
	if ( v_gore.integer ) {
		CG_SliceSpray( s, centre );
		CG_SliceBloodOnView( s, centre );
	}
	return qtrue;
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
	}

	if ( !CG_SliceOn() ) {
		return;
	}
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
		if ( v_gore.integer ) {
			CG_SliceBleed( s );
		}
		if ( s->blood && v_gore.integer && trap->ext.R_AddWet && numSpots < SLICE_SPOTS ) {
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
	if ( v_gore.integer ) {
		CG_SliceDrops();
		numSpots = CG_SliceMarkSpots( spots, numSpots );
	}
	if ( numSpots ) {
		trap->ext.R_AddWet( spots, numSpots );
	}
}
