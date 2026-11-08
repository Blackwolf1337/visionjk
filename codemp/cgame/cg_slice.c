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
// The part that holds the feet stays with the body, the other part is a copy of the model in the pose of the moment;
// both go limp in the ragdoll of the engine (the body at once, broadsword or not: CG_RagDoll; the part thrown off along
// the swing), each with its cut tied to its own bone nearest the cut, so the cut moves with the part as it slumps. The
// death animation of a whole body did not fit one in two parts (their word 2026-11-08). Both are drawn cut by the plane
// (the renderer's ext.R_AddCutEntity, rd-vulkan-vision), each once more with its back faces alone in a glowing colour
// (gfx/vision/slice/cut), which fills the opening: the burnt cut face, orange hot at first, fading through red to dark
// (the afterglow). With v_gore 1 blood runs out on the floor under the cut, a wet place of the kind WET_BLOOD
// (ext.R_AddWet). The copy of the body the server leaves when the victim comes back (CG_BodyQueueCopy) stays cut. It all
// goes when the body goes.
//
// Only a VisionJK client on a renderer that cuts (v_cgameCutEntity) sees it; everyone else sees the stock death, and
// the server is not asked for anything. The stock limbs of a body that was cut are not drawn.

#include "cg_local.h"
#include "ghoul2/G2.h"

#define SLICE_MAX			16
#define SLICE_REACH			48.0f		// units: how near the middle of the body the blade must have come
#define SLICE_GLOW_MSEC		10000		// the afterglow of the cut face
#define SLICE_BLOOD_MSEC	5000		// how long the blood takes to run out
#define SLICE_LOST_MSEC		3000		// a body not drawn for this long is gone
#define SLICE_PIECE_SPEED	160.0f		// units a second the part cut off is thrown along the swing,
#define SLICE_PIECE_THROW	120			//	for this many milliseconds of its ragdoll

// a part of the body and the cut on it, tied to the bone of that part nearest the cut
typedef struct slicePart_s {
	void		*boundG2;		// the ghoul2 instance the bone is of
	int			bolt;			// -1: the plane stays in the world
	vec3_t		localN, localP;	// the plane in the frame of that bone
	vec4_t		plane;			// in the world, as drawn last; what lies on the side the normal points to is kept
} slicePart_t;

typedef struct slice_s {
	qboolean	active;
	int			victim;			// client number
	int			body;			// the entity that has the body now: the victim while he is dead, then the copy of his body
								// (ET_BODY, CG_SliceBodyCopied), -1 when he came back without one
	int			time;			// cg.time of the cut
	int			drawn;			// cg.time the body was last drawn
	vec3_t		sweep;			// which way the blade swept
	vec3_t		centre;			// the middle of the cut, when it was drawn first
	slicePart_t	part;			// the part with the feet

	// the part that was cut off, a ragdoll of its own
	void		*pieceG2;
	refEntity_t	pieceEnt;		// as the body was drawn when it was cut
	vec3_t		pieceAngles;
	int			pieceTime;		// cg.time its ragdoll began
	slicePart_t	piece;

	// the blood
	qboolean	blood;
	vec3_t		bloodOrigin, bloodNormal;
	float		bloodSeed, bloodRadius;
} slice_t;

static slice_t	slices[SLICE_MAX];
static qhandle_t sliceCutShader;

void CG_SliceInit( void ) {
	int i;

	for ( i = 0; i < SLICE_MAX; i++ ) {
		if ( slices[i].pieceG2 && trap->G2_HaveWeGhoul2Models( slices[i].pieceG2 ) ) {
			trap->G2API_CleanGhoul2Models( &slices[i].pieceG2 );
		}
	}
	memset( slices, 0, sizeof( slices ) );
	sliceCutShader = trap->R_RegisterShader( "gfx/vision/slice/cut" );
}

// whether bodies are cut here: the player wants it and the renderer can
static qboolean CG_SliceOn( void ) {
	static int has = -1;

	if ( has < 0 ) {
		char value[8];

		trap->Cvar_VariableStringBuffer( "v_cgameCutEntity", value, sizeof( value ) );
		has = ( atoi( value ) && trap->ext.R_AddCutEntity ) ? 1 : 0;
	}
	return (qboolean)( has > 0 && v_slice.integer );
}

static void CG_SliceFree( slice_t *s ) {
	if ( s->pieceG2 && trap->G2_HaveWeGhoul2Models( s->pieceG2 ) ) {
		trap->G2API_CleanGhoul2Models( &s->pieceG2 );
	}
	memset( s, 0, sizeof( *s ) );
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

// whether client num was cut and is still lying there: the stock limbs of his body are not drawn then (cg_ents.c)
qboolean CG_Sliced( int num ) {
	int i;

	for ( i = 0; i < SLICE_MAX; i++ ) {
		if ( slices[i].active && slices[i].body == num ) {
			return qtrue;
		}
	}
	return qfalse;
}

// Starts a cut of the body of victim by plane (the normal towards what stays with the body after the flip below),
// the blade having swept along sweep. qfalse: the plane misses the body.
static qboolean CG_SliceStart( int victim, const vec4_t planeIn, const vec3_t sweep ) {
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
	s->body = victim;
	s->time = s->drawn = cg.time;
	VectorCopy4( plane, s->part.plane );
	VectorScale( plane, -1.0f, s->piece.plane );
	s->piece.plane[3] = -plane[3];
	VectorCopy( sweep, s->sweep );
	s->part.bolt = s->piece.bolt = -1;
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
	if ( !CG_SliceStart( victim, plane, across ) ) {
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
	Com_Printf( "v_sliceTest: client %i %s\n", victim, CG_SliceStart( victim, plane, sweep ) ? "cut" : "missed" );
}

// the frame of a bone into the world
static void CG_SliceBoltFrame( mdxaBone_t *m, vec3_t origin, vec3_t axis[3] ) {
	BG_GiveMeVectorFromMatrix( m, ORIGIN, origin );
	BG_GiveMeVectorFromMatrix( m, POSITIVE_X, axis[0] );
	BG_GiveMeVectorFromMatrix( m, POSITIVE_Y, axis[1] );
	BG_GiveMeVectorFromMatrix( m, POSITIVE_Z, axis[2] );
}

/*
=================
CG_SliceBind

The cut of a part tied to the bone of ghoul2 (drawn at origin and angles) nearest the middle of the cut on the side the
part keeps, so the cut moves with that part (one bone for the whole body, the pelvis, made a cut through the legs slide
over them as the body fell). No such bone: the plane stays in the world.
=================
*/
static void CG_SliceBind( slicePart_t *part, void *ghoul2, const vec3_t angles, const vec3_t origin, const vec3_t scale,
	const vec3_t centre ) {
	static const char *bones[] = { "pelvis", "lower_lumbar", "upper_lumbar", "thoracic", "cervical", "cranium",
		"lhumerus", "rhumerus", "lradius", "rradius", "lfemurYZ", "rfemurYZ", "ltibia", "rtibia" };
	mdxaBone_t	m;
	vec3_t		at, axis[3], point;
	float		best = 1e9f;
	int			i;

	part->boundG2 = ghoul2;
	part->bolt = -1;
	for ( i = 0; i < (int)ARRAY_LEN( bones ); i++ ) {
		const int bolt = trap->G2API_AddBolt( ghoul2, 0, bones[i] );
		float d;

		if ( bolt < 0 || !trap->G2API_GetBoltMatrix( ghoul2, 0, bolt, &m, angles, origin, cg.time, cgs.gameModels, (float *)scale ) ) {
			continue;
		}
		BG_GiveMeVectorFromMatrix( &m, ORIGIN, at );
		d = Distance( at, centre );
		if ( DotProduct( at, part->plane ) - part->plane[3] > 1.0f && d < best ) {
			best = d;
			part->bolt = bolt;
		}
	}
	if ( part->bolt < 0 || !trap->G2API_GetBoltMatrix( ghoul2, 0, part->bolt, &m, angles, origin, cg.time, cgs.gameModels,
		(float *)scale ) ) {
		part->bolt = -1;
		return;
	}
	CG_SliceBoltFrame( &m, at, axis );
	VectorScale( part->plane, part->plane[3], point );
	VectorSubtract( point, at, point );
	for ( i = 0; i < 3; i++ ) {
		part->localN[i] = DotProduct( part->plane, axis[i] ) / Q_max( VectorLengthSquared( axis[i] ), 1e-6f );
		part->localP[i] = DotProduct( point, axis[i] ) / Q_max( VectorLengthSquared( axis[i] ), 1e-6f );
	}
}

// the plane of a part where its bone is now
static void CG_SliceFollow( slicePart_t *part, void *ghoul2, const vec3_t angles, const vec3_t origin, const vec3_t scale ) {
	mdxaBone_t	m;
	vec3_t		at, axis[3], point, normal;
	int			i;

	if ( part->bolt < 0 || !trap->G2API_GetBoltMatrix( ghoul2, 0, part->bolt, &m, angles, origin, cg.time, cgs.gameModels,
		(float *)scale ) ) {
		return;
	}
	CG_SliceBoltFrame( &m, at, axis );
	VectorClear( normal );
	VectorCopy( at, point );
	for ( i = 0; i < 3; i++ ) {
		VectorMA( normal, part->localN[i], axis[i], normal );
		VectorMA( point, part->localP[i], axis[i], point );
	}
	if ( VectorNormalize( normal ) > 0.1f ) {
		VectorCopy( normal, part->plane );
		part->plane[3] = DotProduct( normal, point );
	}
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
	trap->ext.R_AddCutEntity( &face, plane );
}

// the part that was cut off, made the first time the body is drawn cut: a copy of the model in the pose of now, which
// goes into a ragdoll of its own (CG_SlicePiece)
static void CG_SliceMakePiece( slice_t *s, centity_t *cent, const refEntity_t *ent, const vec3_t angles ) {
	static const char *bones[] = { "model_root", "lower_lumbar", "Motion" };
	int i;

	if ( !ent->ghoul2 || !trap->G2_HaveWeGhoul2Models( ent->ghoul2 ) ) {
		return;
	}
	trap->G2API_DuplicateGhoul2Instance( ent->ghoul2, &s->pieceG2 );
	if ( !s->pieceG2 ) {
		return;
	}
	// (its animation held in the frame it is in now, as CG_RagDoll does before the ragdoll begins)
	for ( i = 0; i < (int)ARRAY_LEN( bones ); i++ ) {
		float frame;

		if ( trap->G2API_DoesBoneExist( s->pieceG2, 0, bones[i] )
			&& trap->G2API_GetBoneFrame( s->pieceG2, bones[i], cg.time, &frame, cgs.gameModels, 0 ) ) {
			trap->G2API_SetBoneAnim( s->pieceG2, 0, bones[i], (int)frame, (int)frame + 1, BONE_ANIM_OVERRIDE_FREEZE, 1.0f, cg.time,
				frame, 0 );
		}
	}
	s->pieceEnt = *ent;
	s->pieceEnt.ghoul2 = s->pieceG2;
	VectorCopy( angles, s->pieceAngles );
	s->pieceTime = cg.time;
	CG_SliceBind( &s->piece, s->pieceG2, s->pieceAngles, s->pieceEnt.origin, cent->modelScale, s->centre );
}

/*
=================
CG_SliceDrawBody

The body of a cent that was cut (the victim while he is dead, then the copy of his body): drawn cut and its cut face,
in place of R_AddRefEntityToScene. angles and the origin of ent are the ones its ghoul2 is drawn with. qfalse: not cut,
draw it as always.
=================
*/
qboolean CG_SliceDrawBody( centity_t *cent, refEntity_t *ent, const vec3_t angles ) {
	const int	num = cent->currentState.number;
	slice_t		*s = NULL;
	int			i;

	if ( !CG_SliceOn() || !ent->ghoul2 ) {
		return qfalse;
	}
	for ( i = 0; i < SLICE_MAX && !s; i++ ) {
		if ( slices[i].active && slices[i].body == num && ( cent->currentState.eType == ET_BODY
			|| ( cent->currentState.eFlags & EF_DEAD ) ) ) {
			s = &slices[i];
		}
	}
	if ( !s ) {
		return qfalse;
	}
	s->drawn = cg.time;

	if ( s->part.boundG2 != ent->ghoul2 ) {
		if ( !s->pieceG2 && s->body == s->victim ) {
			// the middle of the cut, and from there the part cut off and the blood
			vec3_t	down;
			trace_t	tr;

			VectorMA( cent->lerpOrigin, -( DotProduct( cent->lerpOrigin, s->part.plane ) - s->part.plane[3] ), s->part.plane,
				s->centre );
			CG_SliceMakePiece( s, cent, ent, angles );
			VectorCopy( s->centre, down );
			down[2] -= 160.0f;
			CG_Trace( &tr, s->centre, NULL, NULL, down, num, MASK_SOLID );
			if ( tr.fraction < 1.0f && tr.plane.normal[2] > 0.7f ) {
				s->blood = qtrue;
				VectorCopy( tr.endpos, s->bloodOrigin );
				VectorCopy( tr.plane.normal, s->bloodNormal );
			}
		}
		// (the copy of the body: tied to its own bones, from where the cut was drawn last)
		CG_SliceBind( &s->part, ent->ghoul2, angles, ent->origin, cent->modelScale, s->centre );
	} else {
		CG_SliceFollow( &s->part, ent->ghoul2, angles, ent->origin, cent->modelScale );
	}
	CG_SliceAdd( s, ent, s->part.plane );
	return qtrue;
}

// the copy of the body of clientNum the server leaves when he comes back (CG_BodyQueueCopy): it stays cut
void CG_SliceBodyCopied( int clientNum, int body ) {
	int i;

	for ( i = 0; i < SLICE_MAX; i++ ) {
		// (also when his coming back was seen before the copy)
		if ( slices[i].active && ( slices[i].body == clientNum || ( slices[i].body < 0 && slices[i].victim == clientNum ) ) ) {
			slices[i].body = body;
			slices[i].part.boundG2 = NULL;
			slices[i].drawn = cg.time;
		}
	}
}

// the part that was cut off: its ragdoll, thrown along the swing at first, and its cut where its bone is now
static void CG_SlicePiece( slice_t *s ) {
	centity_t	*victim = &cg_entities[s->victim];
	vec3_t		velocity;

	VectorClear( velocity );
	if ( cg.time - s->pieceTime < SLICE_PIECE_THROW ) {
		VectorScale( s->sweep, SLICE_PIECE_SPEED, velocity );
		velocity[2] += 60.0f;
	}
	CG_SliceRag( victim, s->pieceG2, s->pieceEnt.origin, s->pieceAngles, velocity );
	CG_SliceFollow( &s->piece, s->pieceG2, s->pieceAngles, s->pieceEnt.origin, victim->modelScale );
	CG_SliceAdd( s, &s->pieceEnt, s->piece.plane );
}

/*
=================
CG_SliceScene

Every frame (cg_view.c): the pieces that were cut off, the blood, and the end of the cuts whose body is gone
=================
*/
void CG_SliceScene( void ) {
	wetSpot_t	spots[SLICE_MAX];
	int			i, numSpots = 0;

	for ( i = 0; i < SLICE_MAX; i++ ) {
		slice_t *s = &slices[i];

		if ( !s->active ) {
			continue;
		}
		// the victim came back: his body is his body copy now, still to be found
		if ( s->body == s->victim && !( cg_entities[s->victim].currentState.eFlags & EF_DEAD ) ) {
			s->body = -1;
			s->drawn = cg.time;
		}
		if ( cg.time - s->drawn > SLICE_LOST_MSEC || cg.time < s->time ) {
			CG_SliceFree( s );
			continue;
		}
		if ( !CG_SliceOn() ) {
			continue;
		}
		if ( s->pieceG2 ) {
			CG_SlicePiece( s );
		}
		if ( s->blood && v_gore.integer && trap->ext.R_AddWet ) {
			wetSpot_t *w = &spots[numSpots++];
			const float grow = Com_Clamp( 0.0f, 1.0f, (float)( cg.time - s->time ) / SLICE_BLOOD_MSEC );

			memset( w, 0, sizeof( *w ) );
			VectorCopy( s->bloodOrigin, w->origin );
			VectorCopy( s->bloodNormal, w->normal );
			VectorCopy( s->bloodOrigin, w->hit );
			w->radius = s->bloodRadius;
			w->wet = 1.0f - ( 1.0f - grow ) * ( 1.0f - grow );		// fast at first, slower as it spreads
			w->seed = s->bloodSeed;
			w->kind = WET_BLOOD;
		}
	}
	if ( numSpots ) {
		trap->ext.R_AddWet( spots, numSpots );
	}
}
