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
// The part that holds the feet stays with the body, tied to its pelvis so the cut bends with the death animation;
// the other part is a copy of the model in the pose of the moment that is thrown off along the swing, tips over and
// comes to rest on the floor. Both are drawn cut by the plane (the renderer's ext.R_AddCutEntity, rd-vulkan-vision),
// each once more with its back faces alone in a glowing colour (gfx/vision/slice/cut), which fills the opening: the
// burnt cut face, white hot at first, fading through red to dark (the afterglow). With v_gore 1 blood runs out on
// the floor under the cut, a wet place of the kind WET_BLOOD (ext.R_AddWet). It all goes when the body goes.
//
// Only a VisionJK client on a renderer that cuts (v_cgameCutEntity) sees it; everyone else sees the stock death, and
// the server is not asked for anything. The stock limbs of a body that was cut are not drawn.

#include "cg_local.h"
#include "ghoul2/G2.h"

#define SLICE_MAX			16
#define SLICE_REACH			40.0f		// units: how near the middle of the body the blade must have come
#define SLICE_GLOW_MSEC		10000		// the afterglow of the cut face
#define SLICE_BLOOD_MSEC	5000		// how long the blood takes to run out
#define SLICE_LOST_MSEC		3000		// a body not drawn for this long is gone
#define SLICE_PIECE_SPEED	90.0f		// units a second the piece is thrown off along the swing
#define SLICE_PIECE_SPIN	160.0f		// degrees a second it tips over,
#define SLICE_PIECE_TURN	95.0f		//	at most this far

typedef struct slice_s {
	qboolean	active;
	int			victim;			// client number
	int			body;			// the entity that has the body now: the victim while he is dead, his body copy (ET_BODY)
								// after he came back, -1 while that copy is not found yet
	int			time;			// cg.time of the cut
	int			drawn;			// cg.time the body was last drawn
	vec3_t		lastOrigin;		//	and where
	vec4_t		plane;			// in the world, as drawn last; what lies on the side the normal points to stays
	vec3_t		sweep;			// which way the blade swept

	// the plane in the frame of the pelvis of the ghoul2 instance that is drawn
	void		*boundG2;
	int			bolt;
	vec3_t		localN, localP;

	// the part that was cut off
	void		*pieceG2;
	refEntity_t	pieceEnt;		// as the body was drawn when it was cut
	vec3_t		pieceCentre0;	// the point it turns round, when it was cut
	vec3_t		pieceCentre, pieceVel, pieceSpinAxis;
	vec3_t		piecePoint0;	// a point of its cut, when it was cut
	float		pieceSpin;
	qboolean	pieceRest;
	int			piecePhys;

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
	VectorCopy4( plane, s->plane );
	VectorCopy( sweep, s->sweep );
	s->bolt = -1;
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
	vec3_t	base, tip, sweep, axis, across;
	vec4_t	plane;

	if ( !CG_SliceOn() || victim < 0 || victim >= MAX_CLIENTS || killer < 0 || killer >= MAX_CLIENTS || victim == killer ) {
		return;
	}
	if ( !CG_SaberCutNear( &cgs.clientinfo[killer], cg_entities[victim].lerpOrigin, SLICE_REACH, base, tip, sweep ) ) {
		return;
	}
	// the plane of the blade and of the way it swept; a stab (along the blade) cuts nothing
	VectorSubtract( tip, base, axis );
	if ( VectorNormalize( axis ) < 1.0f ) {
		return;
	}
	VectorMA( sweep, -DotProduct( sweep, axis ), axis, across );
	if ( VectorNormalize( across ) < 1.0f ) {
		return;
	}
	CrossProduct( axis, across, plane );
	VectorNormalize( plane );
	plane[3] = DotProduct( plane, base );
	CG_SliceStart( victim, plane, across );
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

// the plane from the frame of the bolt (the pelvis) into the world and back
static void CG_SliceBoltFrame( mdxaBone_t *m, vec3_t origin, vec3_t axis[3] ) {
	BG_GiveMeVectorFromMatrix( m, ORIGIN, origin );
	BG_GiveMeVectorFromMatrix( m, POSITIVE_X, axis[0] );
	BG_GiveMeVectorFromMatrix( m, POSITIVE_Y, axis[1] );
	BG_GiveMeVectorFromMatrix( m, POSITIVE_Z, axis[2] );
}

// the colour of the cut face: white hot, then orange, red, and dark (the afterglow)
static void CG_SliceGlow( const slice_t *s, byte *rgba ) {
	static const float stops[5][4] = {
		{ 0.00f, 255.0f, 235.0f, 190.0f },
		{ 0.06f, 255.0f, 170.0f, 60.0f },
		{ 0.25f, 230.0f, 70.0f, 10.0f },
		{ 0.60f, 120.0f, 12.0f, 2.0f },
		{ 1.00f, 40.0f, 4.0f, 2.0f },
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

// the part that was cut off, made the first time the body is drawn cut: a copy of the model in the pose of now
static void CG_SliceMakePiece( slice_t *s, const refEntity_t *ent, const vec3_t centre ) {
	static const char *bones[] = { "model_root", "lower_lumbar", "Motion" };
	int i;

	if ( !ent->ghoul2 || !trap->G2_HaveWeGhoul2Models( ent->ghoul2 ) ) {
		return;
	}
	trap->G2API_DuplicateGhoul2Instance( ent->ghoul2, &s->pieceG2 );
	if ( !s->pieceG2 ) {
		return;
	}
	// (frozen in the frame it is in now)
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
	// it turns round a point a little into it from the cut, thrown off along the swing and a little up, tipping over
	// along the swing
	VectorCopy( centre, s->piecePoint0 );
	VectorMA( centre, -12.0f, s->plane, s->pieceCentre0 );
	VectorCopy( s->pieceCentre0, s->pieceCentre );
	VectorScale( s->sweep, SLICE_PIECE_SPEED, s->pieceVel );
	s->pieceVel[2] += 40.0f;
	{
		vec3_t up = { 0.0f, 0.0f, 1.0f };

		CrossProduct( s->sweep, up, s->pieceSpinAxis );
		if ( VectorNormalize( s->pieceSpinAxis ) < 0.1f ) {
			PerpendicularVector( s->pieceSpinAxis, s->plane );
		}
		VectorScale( s->pieceSpinAxis, -1.0f, s->pieceSpinAxis );
	}
	s->pieceSpin = 0.0f;
	s->pieceRest = qfalse;
	s->piecePhys = cg.time;
}

/*
=================
CG_SliceDrawBody

The body of a cent that was cut (the victim while he is dead, then his body copy): drawn cut and its cut face, in
place of R_AddRefEntityToScene. angles and the origin of ent are the ones its ghoul2 is drawn with. qfalse: not cut,
draw it as always.
=================
*/
qboolean CG_SliceDrawBody( centity_t *cent, refEntity_t *ent, const vec3_t angles ) {
	const int	num = cent->currentState.number;
	slice_t		*s = NULL;
	mdxaBone_t	m;
	vec3_t		origin, axis[3];
	int			i;

	if ( !CG_SliceOn() || !ent->ghoul2 ) {
		return qfalse;
	}
	for ( i = 0; i < SLICE_MAX && !s; i++ ) {
		slice_t *t = &slices[i];

		if ( !t->active ) {
			continue;
		}
		if ( cent->currentState.eType == ET_BODY ) {
			if ( t->body == num ) {
				s = t;
			}
		} else if ( t->body == num && ( cent->currentState.eFlags & EF_DEAD ) ) {
			s = t;
		}
	}
	if ( !s && cent->currentState.eType == ET_BODY ) {
		// the copy of the body of a victim who came back: at the place his body lay
		for ( i = 0; i < SLICE_MAX && !s; i++ ) {
			slice_t *t = &slices[i];

			if ( t->active && t->body < 0 && Distance( t->lastOrigin, cent->lerpOrigin ) < 80.0f ) {
				s = t;
				s->body = num;
				s->boundG2 = NULL;
			}
		}
	}
	if ( !s ) {
		return qfalse;
	}
	s->drawn = cg.time;
	VectorCopy( cent->lerpOrigin, s->lastOrigin );

	// the plane in the frame of the pelvis of this instance: bound the first time, from the world plane of the last draw
	if ( s->boundG2 != ent->ghoul2 ) {
		s->boundG2 = ent->ghoul2;
		s->bolt = trap->G2API_AddBolt( ent->ghoul2, 0, "pelvis" );
		if ( s->bolt >= 0 && trap->G2API_GetBoltMatrix( ent->ghoul2, 0, s->bolt, &m, angles, ent->origin, cg.time, cgs.gameModels,
			cent->modelScale ) ) {
			vec3_t point;

			CG_SliceBoltFrame( &m, origin, axis );
			VectorScale( s->plane, s->plane[3], point );
			VectorSubtract( point, origin, point );
			for ( i = 0; i < 3; i++ ) {
				s->localN[i] = DotProduct( s->plane, axis[i] ) / Q_max( VectorLengthSquared( axis[i] ), 1e-6f );
				s->localP[i] = DotProduct( point, axis[i] ) / Q_max( VectorLengthSquared( axis[i] ), 1e-6f );
			}
		} else {
			s->bolt = -1;
		}
		if ( !s->pieceG2 && s->body == s->victim ) {
			// the cut point at the middle of the body, and the piece and the blood from there
			vec3_t centre, down;
			trace_t tr;

			VectorMA( cent->lerpOrigin, -( DotProduct( cent->lerpOrigin, s->plane ) - s->plane[3] ), s->plane, centre );
			CG_SliceMakePiece( s, ent, centre );
			VectorCopy( centre, down );
			down[2] -= 160.0f;
			CG_Trace( &tr, centre, NULL, NULL, down, num, MASK_SOLID );
			if ( tr.fraction < 1.0f && tr.plane.normal[2] > 0.7f ) {
				s->blood = qtrue;
				VectorCopy( tr.endpos, s->bloodOrigin );
				VectorCopy( tr.plane.normal, s->bloodNormal );
			}
		}
	} else if ( s->bolt >= 0 && trap->G2API_GetBoltMatrix( ent->ghoul2, 0, s->bolt, &m, angles, ent->origin, cg.time, cgs.gameModels,
		cent->modelScale ) ) {
		// it bends with the body
		vec3_t point, normal;

		CG_SliceBoltFrame( &m, origin, axis );
		VectorClear( normal );
		VectorCopy( origin, point );
		for ( i = 0; i < 3; i++ ) {
			VectorMA( normal, s->localN[i], axis[i], normal );
			VectorMA( point, s->localP[i], axis[i], point );
		}
		if ( VectorNormalize( normal ) > 0.1f ) {
			VectorCopy( normal, s->plane );
			s->plane[3] = DotProduct( normal, point );
		}
	}
	CG_SliceAdd( s, ent, s->plane );
	return qtrue;
}

// the part that was cut off: it flies, tips over and comes to rest
static void CG_SlicePiece( slice_t *s ) {
	static const vec3_t mins = { -8.0f, -8.0f, -6.0f }, maxs = { 8.0f, 8.0f, 6.0f };
	refEntity_t	ent;
	vec3_t		rel, point;
	vec4_t		plane;
	float		dt;
	int			i;

	dt = Com_Clamp( 0.0f, 0.1f, ( cg.time - s->piecePhys ) * 0.001f );
	s->piecePhys = cg.time;
	if ( !s->pieceRest && dt > 0.0f ) {
		vec3_t	next;
		trace_t	tr;

		s->pieceVel[2] -= 800.0f * dt;
		VectorMA( s->pieceCentre, dt, s->pieceVel, next );
		CG_Trace( &tr, s->pieceCentre, mins, maxs, next, s->body >= 0 ? s->body : ENTITYNUM_NONE, MASK_SOLID );
		VectorCopy( tr.endpos, s->pieceCentre );
		if ( tr.fraction < 1.0f ) {
			if ( tr.plane.normal[2] > 0.6f ) {
				s->pieceRest = qtrue;
				VectorClear( s->pieceVel );
			} else {
				// off a wall
				VectorMA( s->pieceVel, -1.3f * DotProduct( s->pieceVel, tr.plane.normal ), tr.plane.normal, s->pieceVel );
				VectorScale( s->pieceVel, 0.5f, s->pieceVel );
			}
		}
		s->pieceSpin = Q_min( SLICE_PIECE_TURN, s->pieceSpin + SLICE_PIECE_SPIN * dt );
	}

	// the model turned round the centre by the spin and carried with it
	ent = s->pieceEnt;
	VectorSubtract( s->pieceEnt.origin, s->pieceCentre0, rel );
	RotatePointAroundVector( point, s->pieceSpinAxis, rel, s->pieceSpin );
	VectorAdd( s->pieceCentre, point, ent.origin );
	for ( i = 0; i < 3; i++ ) {
		RotatePointAroundVector( ent.axis[i], s->pieceSpinAxis, s->pieceEnt.axis[i], s->pieceSpin );
	}
	VectorCopy( ent.origin, ent.lightingOrigin );
	VectorCopy( ent.origin, ent.oldorigin );
	// its plane: the other side of the cut, turned and carried the same
	RotatePointAroundVector( plane, s->pieceSpinAxis, s->plane, s->pieceSpin );
	VectorScale( plane, -1.0f, plane );
	VectorSubtract( s->piecePoint0, s->pieceCentre0, rel );
	RotatePointAroundVector( point, s->pieceSpinAxis, rel, s->pieceSpin );
	VectorAdd( s->pieceCentre, point, point );
	plane[3] = DotProduct( plane, point );
	CG_SliceAdd( s, &ent, plane );
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
