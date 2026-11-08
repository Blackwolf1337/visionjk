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

// cg_ragdoll.c -- VisionJK: the ragdoll of the bodies a saber cut through (cg_slice.c), their wish 2026-11-08: the
// engine's ragdoll (broadsword) felt old, and as it moves the whole skeleton in every copy, the two halves of a cut
// body stayed tied to each other by the hidden half of each and showed it when it swung across the cut.
//
// Here the body is split at the cut. Every bone of the humanoid skeleton that matters (pelvis to cranium, arms, legs)
// is a point; the points are held together by distances (the bones, and braces that keep pelvis and chest rigid) and
// by limits on the angles between them (knees and elbows bend one way only, the spine and the neck within cones), and
// fall under gravity, traced against the level (position based dynamics, fixed steps of 1/60 s). At the cut every
// bone goes to the side of the plane its joint lies on; the links across the plane are dropped and every group still
// linked becomes a part of its own (two, or more when a bent arm crosses the plane twice), with its own copy of the
// model. Nothing cut stays connected.
//
// A part is drawn with its own bones where its points are (ext.G2_SetBonesWorld, rd-vulkan-vision); the bones of the
// other side follow its bone at the cut as they were at the moment of the cut (they lie behind the plane and are
// clipped away, and the cut stays clean), and bones of another part on the same side are shrunk to nothing, so no
// piece is drawn twice. For half a second the angles hold the pose of the moment, then the body goes limp. Players and
// NPCs walking through a part push it, and so do force push, force pull and explosions. All of it is decided on this
// client alone; the server's body does not move.

#include "cg_local.h"
#include "ghoul2/G2.h"

#define RAG_MAX			10			// bodies
#define RAG_PARTS		4
#define RAG_STEP		( 1.0f / 60.0f )
#define RAG_MAX_STEPS	4
#define RAG_ITER		8
#define RAG_CONS		72
#define RAG_HOLD_MSEC	500			// the angles hold the pose of the cut for this long, weaker and weaker
#define RAG_SLEEP_STEPS	30			// steps without motion before a part sleeps
#define RAG_FRICTION	0.55f		// of the speed along a surface lost each step a point touches it
#define RAG_MAX_SPEED	900.0f

enum {
	RB_PELVIS, RB_LLUMBAR, RB_ULUMBAR, RB_THORACIC, RB_CERVICAL, RB_CRANIUM,
	RB_LHUMERUS, RB_LRADIUS, RB_LHAND, RB_RHUMERUS, RB_RRADIUS, RB_RHAND,
	RB_LFEMUR, RB_LTIBIA, RB_LTALUS, RB_RFEMUR, RB_RTIBIA, RB_RTALUS,
	RB_BONES,
	// points that are no bone: the top of the head, the tips of the hands, the toes (for collision and the frames)
	RP_HEADTOP = RB_BONES, RP_LHANDTIP, RP_RHANDTIP, RP_LTOE, RP_RTOE,
	RP_POINTS
};

static const struct {
	const char	*name;
	int			parent;
	float		radius;		// of the flesh around it, for collision
	float		mass;
	int			u0, u1;		// the frame of the bone turns with u0 -> u1,
	int			w0, w1;		//	and its roll with w0 -> w1 (-1: none)
} ragBones[RB_BONES] = {
	{ "pelvis",			-1,				7.0f,	3.0f,	RB_PELVIS, RB_LLUMBAR,		RB_LFEMUR, RB_RFEMUR },
	{ "lower_lumbar",	RB_PELVIS,		6.0f,	3.0f,	RB_LLUMBAR, RB_ULUMBAR,		RB_LFEMUR, RB_RFEMUR },
	{ "upper_lumbar",	RB_LLUMBAR,		7.0f,	3.0f,	RB_ULUMBAR, RB_THORACIC,	RB_LHUMERUS, RB_RHUMERUS },
	{ "thoracic",		RB_ULUMBAR,		8.0f,	3.0f,	RB_THORACIC, RB_CERVICAL,	RB_LHUMERUS, RB_RHUMERUS },
	{ "cervical",		RB_THORACIC,	4.0f,	1.5f,	RB_CERVICAL, RB_CRANIUM,	RB_LHUMERUS, RB_RHUMERUS },
	{ "cranium",		RB_CERVICAL,	5.0f,	1.5f,	RB_CRANIUM, RP_HEADTOP,		RB_LHUMERUS, RB_RHUMERUS },
	{ "lhumerus",		RB_THORACIC,	4.0f,	1.0f,	RB_LHUMERUS, RB_LRADIUS,	-1, -1 },
	{ "lradius",		RB_LHUMERUS,	3.5f,	1.0f,	RB_LRADIUS, RB_LHAND,		-1, -1 },
	{ "lhand",			RB_LRADIUS,		3.0f,	0.7f,	RB_LHAND, RP_LHANDTIP,		-1, -1 },
	{ "rhumerus",		RB_THORACIC,	4.0f,	1.0f,	RB_RHUMERUS, RB_RRADIUS,	-1, -1 },
	{ "rradius",		RB_RHUMERUS,	3.5f,	1.0f,	RB_RRADIUS, RB_RHAND,		-1, -1 },
	{ "rhand",			RB_RRADIUS,		3.0f,	0.7f,	RB_RHAND, RP_RHANDTIP,		-1, -1 },
	{ "lfemurYZ",		RB_PELVIS,		6.0f,	2.0f,	RB_LFEMUR, RB_LTIBIA,		-1, -1 },
	{ "ltibia",			RB_LFEMUR,		4.0f,	1.5f,	RB_LTIBIA, RB_LTALUS,		-1, -1 },
	{ "ltalus",			RB_LTIBIA,		3.5f,	1.0f,	RB_LTALUS, RP_LTOE,			-1, -1 },
	{ "rfemurYZ",		RB_PELVIS,		6.0f,	2.0f,	RB_RFEMUR, RB_RTIBIA,		-1, -1 },
	{ "rtibia",			RB_RFEMUR,		4.0f,	1.5f,	RB_RTIBIA, RB_RTALUS,		-1, -1 },
	{ "rtalus",			RB_RTIBIA,		3.5f,	1.0f,	RB_RTALUS, RP_RTOE,			-1, -1 },
};

// the points that are no bone: the bone they belong to, the radius
static const struct { int owner; float radius; } ragTips[RP_POINTS - RB_BONES] = {
	{ RB_CRANIUM, 6.0f }, { RB_LHAND, 2.5f }, { RB_RHAND, 2.5f }, { RB_LTALUS, 2.5f }, { RB_RTALUS, 2.5f },
};

typedef enum {
	RC_DIST,		// |a b| stays as it was
	RC_RANGE,		// |a b| between lo and hi (an angle limit at the joint between them); held at rest at first
	RC_HINGE,		// the joint a bends to one side of the line b c only: sign of the forward of frame
	RC_FRONT		// a stays in front of b by at least lo along the forward of frame
} ragKind_t;

// the frames of the body the one-sided limits look at
enum { RF_PELVIS, RF_CHEST };

typedef struct {
	byte		kind, a, b, c, frame;
	signed char	sign;
	float		lo, hi, rest;
} ragCon_t;

typedef struct {
	qboolean	used;
	qboolean	own[RP_POINTS];
	vec3_t		x[RP_POINTS];		// where the points are
	vec3_t		old[RP_POINTS];		// where they were a step ago (the speed)
	vec3_t		prev[RP_POINTS];	// where they were before the last step, for drawing between steps
	vec3_t		draw[RP_POINTS];
	vec3_t		normal[RP_POINTS];	// of the surface a point touches in this step
	qboolean	touch[RP_POINTS];
	float		turn[RB_BONES][3][3];	// how each own bone turned since the cut
	byte		u0[RB_BONES], u1[RB_BONES];	// the line of points each own bone turns with (u0 == u1: with the anchor)
	vec3_t		lastU[RB_BONES];
	ragCon_t	con[RAG_CONS];
	int			numCons;
	int			anchor;				// its bone nearest the cut
	int			side;				// 1: the side the normal of the cut points to
	void		*ghoul2;
	vec3_t		origin;				// drawn at, angles 0
	int			still;
	qboolean	asleep;
	qboolean	checked;			// (developer: the bone round trip was looked at)
	qboolean	posed;				// asleep and its bones placed
} ragPart_t;

typedef struct {
	qboolean	active;
	int			start;
	float		acc;				// seconds not stepped yet
	int			boneNum[RB_BONES];	// in the skeleton of the model
	float		rest[RB_BONES][3][3];	// the axes of each bone at the cut (columns, as G2API_GetBoltMatrix gives them)
	vec3_t		p0[RP_POINTS];		// the points at the cut
	float		invMass[RP_POINTS];
	float		radius[RP_POINTS];
	vec3_t		scale;
	vec4_t		plane;				// the cut, the normal towards side 1
	vec3_t		centre;
	int			partOf[RB_BONES];
	int			numParts;
	ragPart_t	part[RAG_PARTS];
} ragBody_t;

static ragBody_t	rags[RAG_MAX];

// the limits of the body, made into constraints with the lengths of the moment of the cut
typedef struct { byte kind, a, b, c, frame; signed char sign; float lo, hi; } ragTemplate_t;

static const ragTemplate_t ragLimits[] = {
	// spine and neck bend within cones (degrees from straight)
	{ RC_RANGE, RB_PELVIS, RB_LLUMBAR, RB_ULUMBAR, 0, 0,		0.0f, 30.0f },
	{ RC_RANGE, RB_LLUMBAR, RB_ULUMBAR, RB_THORACIC, 0, 0,		0.0f, 30.0f },
	{ RC_RANGE, RB_THORACIC, RB_CERVICAL, RB_CRANIUM, 0, 0,		0.0f, 55.0f },
	{ RC_RANGE, RB_CERVICAL, RB_CRANIUM, RP_HEADTOP, 0, 0,		0.0f, 35.0f },
	// shoulders nearly free, elbows and wrists
	{ RC_RANGE, RB_THORACIC, RB_LHUMERUS, RB_LRADIUS, 0, 0,		0.0f, 165.0f },
	{ RC_RANGE, RB_THORACIC, RB_RHUMERUS, RB_RRADIUS, 0, 0,		0.0f, 165.0f },
	{ RC_RANGE, RB_LHUMERUS, RB_LRADIUS, RB_LHAND, 0, 0,		0.0f, 150.0f },
	{ RC_RANGE, RB_RHUMERUS, RB_RRADIUS, RB_RHAND, 0, 0,		0.0f, 150.0f },
	{ RC_RANGE, RB_LRADIUS, RB_LHAND, RP_LHANDTIP, 0, 0,		0.0f, 80.0f },
	{ RC_RANGE, RB_RRADIUS, RB_RHAND, RP_RHANDTIP, 0, 0,		0.0f, 80.0f },
	// hips, knees, ankles
	{ RC_RANGE, RB_LLUMBAR, RB_LFEMUR, RB_LTIBIA, 0, 0,			0.0f, 120.0f },
	{ RC_RANGE, RB_LLUMBAR, RB_RFEMUR, RB_RTIBIA, 0, 0,			0.0f, 120.0f },
	{ RC_RANGE, RB_LFEMUR, RB_LTIBIA, RB_LTALUS, 0, 0,			0.0f, 150.0f },
	{ RC_RANGE, RB_RFEMUR, RB_RTIBIA, RB_RTALUS, 0, 0,			0.0f, 150.0f },
	{ RC_RANGE, RB_LTIBIA, RB_LTALUS, RP_LTOE, 0, 0,			50.0f, 130.0f },
	{ RC_RANGE, RB_RTIBIA, RB_RTALUS, RP_RTOE, 0, 0,			50.0f, 130.0f },
	// knees bend forward, elbows backward; legs not far behind the body, the back not far backward
	{ RC_HINGE, RB_LTIBIA, RB_LFEMUR, RB_LTALUS, RF_PELVIS, 1,	0.0f, 0.0f },
	{ RC_HINGE, RB_RTIBIA, RB_RFEMUR, RB_RTALUS, RF_PELVIS, 1,	0.0f, 0.0f },
	{ RC_HINGE, RB_LRADIUS, RB_LHUMERUS, RB_LHAND, RF_CHEST, -1,	0.0f, 0.0f },
	{ RC_HINGE, RB_RRADIUS, RB_RHUMERUS, RB_RHAND, RF_CHEST, -1,	0.0f, 0.0f },
	{ RC_FRONT, RB_LTIBIA, RB_LFEMUR, 0, RF_PELVIS, 0,			-0.45f, 0.0f },
	{ RC_FRONT, RB_RTIBIA, RB_RFEMUR, 0, RF_PELVIS, 0,			-0.45f, 0.0f },
	{ RC_FRONT, RB_CERVICAL, RB_PELVIS, 0, RF_PELVIS, 0,		-0.35f, 0.0f },
};

// the rigid braces of pelvis and chest, and the twist of the spine
static const byte ragBraces[][2] = {
	{ RB_LFEMUR, RB_RFEMUR }, { RB_LFEMUR, RB_LLUMBAR }, { RB_RFEMUR, RB_LLUMBAR },
	{ RB_LHUMERUS, RB_RHUMERUS }, { RB_LHUMERUS, RB_CERVICAL }, { RB_RHUMERUS, RB_CERVICAL },
	{ RB_LHUMERUS, RB_ULUMBAR }, { RB_RHUMERUS, RB_ULUMBAR }, { RB_ULUMBAR, RB_CERVICAL },
};
static const byte ragTwist[][2] = {
	{ RB_LHUMERUS, RB_LFEMUR }, { RB_RHUMERUS, RB_RFEMUR }, { RB_LHUMERUS, RB_RFEMUR }, { RB_RHUMERUS, RB_LFEMUR },
};

// ------------------------------------------------------------------------------------------------ small maths

static void Rag_Identity( float m[3][3] ) {
	memset( m, 0, sizeof( float ) * 9 );
	m[0][0] = m[1][1] = m[2][2] = 1.0f;
}

static void Rag_Mul( const float a[3][3], const float b[3][3], float out[3][3] ) {
	float	t[3][3];
	int		i, j;

	for ( i = 0; i < 3; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			t[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
		}
	}
	memcpy( out, t, sizeof( t ) );
}

static void Rag_Apply( const float m[3][3], const vec3_t v, vec3_t out ) {
	vec3_t t;

	t[0] = m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2];
	t[1] = m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2];
	t[2] = m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2];
	VectorCopy( t, out );
}

// the shortest turn from unit a to unit b
static void Rag_Arc( const vec3_t a, const vec3_t b, float m[3][3] ) {
	vec3_t	v;
	float	c = DotProduct( a, b ), k;

	if ( c < -0.9995f ) {
		// half a turn about any axis across a
		vec3_t axis;

		PerpendicularVector( axis, a );
		m[0][0] = 2.0f * axis[0] * axis[0] - 1.0f; m[0][1] = 2.0f * axis[0] * axis[1]; m[0][2] = 2.0f * axis[0] * axis[2];
		m[1][0] = 2.0f * axis[1] * axis[0]; m[1][1] = 2.0f * axis[1] * axis[1] - 1.0f; m[1][2] = 2.0f * axis[1] * axis[2];
		m[2][0] = 2.0f * axis[2] * axis[0]; m[2][1] = 2.0f * axis[2] * axis[1]; m[2][2] = 2.0f * axis[2] * axis[2] - 1.0f;
		return;
	}
	CrossProduct( a, b, v );
	k = 1.0f / ( 1.0f + c );
	m[0][0] = c + v[0] * v[0] * k;			m[0][1] = v[0] * v[1] * k - v[2];	m[0][2] = v[0] * v[2] * k + v[1];
	m[1][0] = v[1] * v[0] * k + v[2];		m[1][1] = c + v[1] * v[1] * k;		m[1][2] = v[1] * v[2] * k - v[0];
	m[2][0] = v[2] * v[0] * k - v[1];		m[2][1] = v[2] * v[1] * k + v[0];	m[2][2] = c + v[2] * v[2] * k;
}

// an orthonormal frame (columns) out of a direction and a second one across it; qfalse when they are in line
static qboolean Rag_Basis( const vec3_t u, const vec3_t w, float m[3][3] ) {
	vec3_t e1, e2, e3;

	VectorCopy( u, e1 );
	if ( VectorNormalize( e1 ) < 0.001f ) {
		return qfalse;
	}
	CrossProduct( e1, w, e3 );
	if ( VectorNormalize( e3 ) < 0.05f * VectorLength( w ) + 0.0001f ) {
		return qfalse;
	}
	CrossProduct( e3, e1, e2 );
	m[0][0] = e1[0]; m[0][1] = e2[0]; m[0][2] = e3[0];
	m[1][0] = e1[1]; m[1][1] = e2[1]; m[1][2] = e3[1];
	m[2][0] = e1[2]; m[2][1] = e2[2]; m[2][2] = e3[2];
	return qtrue;
}

// ------------------------------------------------------------------------------------------------ building

static float Rag_PointRadius( int p ) {
	return p < RB_BONES ? ragBones[p].radius : ragTips[p - RB_BONES].radius;
}

static int Rag_PointBone( int p ) {
	return p < RB_BONES ? p : ragTips[p - RB_BONES].owner;
}

static void Rag_FreePart( ragPart_t *part ) {
	if ( part->ghoul2 && trap->G2_HaveWeGhoul2Models( part->ghoul2 ) ) {
		trap->G2API_CleanGhoul2Models( &part->ghoul2 );
	}
	memset( part, 0, sizeof( *part ) );
}

void CG_RagFree( int handle ) {
	ragBody_t	*rag;
	int			i;

	if ( handle < 0 || handle >= RAG_MAX ) {
		return;
	}
	rag = &rags[handle];
	for ( i = 0; i < RAG_PARTS; i++ ) {
		Rag_FreePart( &rag->part[i] );
	}
	memset( rag, 0, sizeof( *rag ) );
}

void CG_RagInit( void ) {
	int i;

	for ( i = 0; i < RAG_MAX; i++ ) {
		CG_RagFree( i );
	}
}

// whether the cgame can place bones here
qboolean CG_RagOn( void ) {
	static int has = -1;

	if ( has < 0 ) {
		char value[8];

		trap->Cvar_VariableStringBuffer( "v_cgameSetBones", value, sizeof( value ) );
		has = ( atoi( value ) && trap->ext.G2_BoneNumber && trap->ext.G2_SetBonesWorld ) ? 1 : 0;
	}
	return (qboolean)( has > 0 );
}

static float Rag_Bend( float l1, float l2, float degrees ) {
	const float c = cosf( DEG2RAD( degrees ) );

	return sqrtf( Q_max( l1 * l1 + l2 * l2 + 2.0f * l1 * l2 * c, 0.0f ) );
}

static void Rag_AddCon( ragBody_t *rag, ragPart_t *part, ragKind_t kind, int a, int b, int c, int frame, int sign, float lo,
	float hi ) {
	ragCon_t *con;

	if ( part->numCons >= RAG_CONS ) {
		return;
	}
	con = &part->con[part->numCons++];
	con->kind = (byte)kind;
	con->a = (byte)a;
	con->b = (byte)b;
	con->c = (byte)c;
	con->frame = (byte)frame;
	con->sign = (signed char)sign;
	con->lo = lo;
	con->hi = hi;
	con->rest = Distance( rag->p0[a], rag->p0[b] );
}

// the forward of a frame of the body (x of the model: up x right)
static void Rag_Forward( const vec3_t *x, int frame, vec3_t forward ) {
	vec3_t up, right;

	if ( frame == RF_PELVIS ) {
		VectorSubtract( x[RB_LLUMBAR], x[RB_PELVIS], up );
		VectorSubtract( x[RB_RFEMUR], x[RB_LFEMUR], right );
	} else {
		VectorSubtract( x[RB_CERVICAL], x[RB_ULUMBAR], up );
		VectorSubtract( x[RB_RHUMERUS], x[RB_LHUMERUS], right );
	}
	CrossProduct( up, right, forward );
	VectorNormalize( forward );
}

// whether the points of a frame of the body are all in the part
static qboolean Rag_HasFrame( const ragPart_t *part, int frame ) {
	if ( frame == RF_PELVIS ) {
		return (qboolean)( part->own[RB_PELVIS] && part->own[RB_LLUMBAR] && part->own[RB_LFEMUR] && part->own[RB_RFEMUR] );
	}
	return (qboolean)( part->own[RB_ULUMBAR] && part->own[RB_CERVICAL] && part->own[RB_LHUMERUS] && part->own[RB_RHUMERUS] );
}

// the line of points an own bone turns with: its own, else from its parent, a child or a tip in the part
static void Rag_TurnLine( ragPart_t *part, int bone ) {
	int i;

	part->u0[bone] = part->u1[bone] = (byte)bone;
	if ( part->own[ragBones[bone].u0] && part->own[ragBones[bone].u1] ) {
		part->u0[bone] = (byte)ragBones[bone].u0;
		part->u1[bone] = (byte)ragBones[bone].u1;
		return;
	}
	if ( ragBones[bone].parent >= 0 && part->own[ragBones[bone].parent] ) {
		part->u0[bone] = (byte)ragBones[bone].parent;
		return;
	}
	for ( i = 0; i < RP_POINTS; i++ ) {
		if ( part->own[i] && i != bone && ( i < RB_BONES ? ragBones[i].parent : ragTips[i - RB_BONES].owner ) == bone ) {
			part->u1[bone] = (byte)i;
			return;
		}
	}
}

static void Rag_MakeCons( ragBody_t *rag, ragPart_t *part ) {
	int i;

	part->numCons = 0;
	// the bones, and the points that are no bone on theirs
	for ( i = 1; i < RB_BONES; i++ ) {
		if ( part->own[i] && part->own[ragBones[i].parent] ) {
			Rag_AddCon( rag, part, RC_DIST, i, ragBones[i].parent, 0, 0, 0, 0.0f, 0.0f );
		}
	}
	for ( i = RB_BONES; i < RP_POINTS; i++ ) {
		if ( part->own[i] && part->own[ragTips[i - RB_BONES].owner] ) {
			Rag_AddCon( rag, part, RC_DIST, i, ragTips[i - RB_BONES].owner, 0, 0, 0, 0.0f, 0.0f );
		}
	}
	for ( i = 0; i < (int)ARRAY_LEN( ragBraces ); i++ ) {
		if ( part->own[ragBraces[i][0]] && part->own[ragBraces[i][1]] ) {
			Rag_AddCon( rag, part, RC_DIST, ragBraces[i][0], ragBraces[i][1], 0, 0, 0, 0.0f, 0.0f );
		}
	}
	for ( i = 0; i < (int)ARRAY_LEN( ragTwist ); i++ ) {
		if ( part->own[ragTwist[i][0]] && part->own[ragTwist[i][1]] ) {
			const float d = Distance( rag->p0[ragTwist[i][0]], rag->p0[ragTwist[i][1]] );

			Rag_AddCon( rag, part, RC_RANGE, ragTwist[i][0], ragTwist[i][1], 0, 0, 0, d * 0.85f, d * 1.1f );
		}
	}
	for ( i = 0; i < (int)ARRAY_LEN( ragLimits ); i++ ) {
		const ragTemplate_t *t = &ragLimits[i];

		if ( !part->own[t->a] || !part->own[t->b] || ( t->kind != RC_FRONT && !part->own[t->c] ) ) {
			continue;
		}
		if ( t->kind == RC_RANGE ) {
			// the angle at b between a b and b c: a and c kept within the distances of those angles, the angle of the
			// moment of the cut always allowed
			const float l1 = Distance( rag->p0[t->a], rag->p0[t->b] ), l2 = Distance( rag->p0[t->b], rag->p0[t->c] );
			const float now = Distance( rag->p0[t->a], rag->p0[t->c] );
			const float lo = Q_min( Rag_Bend( l1, l2, t->hi ), now ), hi = Q_max( Rag_Bend( l1, l2, t->lo ), now );

			Rag_AddCon( rag, part, RC_RANGE, t->a, t->c, 0, 0, 0, lo, hi );
		} else if ( Rag_HasFrame( part, t->frame ) ) {
			vec3_t f, d;

			Rag_Forward( rag->p0, t->frame, f );
			if ( t->kind == RC_FRONT ) {
				VectorSubtract( rag->p0[t->a], rag->p0[t->b], d );
				Rag_AddCon( rag, part, RC_FRONT, t->a, t->b, 0, t->frame, 0, 0.0f, 0.0f );
				part->con[part->numCons - 1].lo = Q_min( t->lo * part->con[part->numCons - 1].rest, DotProduct( d, f ) - 0.5f );
			} else {
				// (not when the joint is bent the other way already at the cut: it would snap over)
				vec3_t line, at;
				float t2;

				VectorSubtract( rag->p0[t->c], rag->p0[t->b], line );
				VectorSubtract( rag->p0[t->a], rag->p0[t->b], at );
				t2 = Com_Clamp( 0.0f, 1.0f, DotProduct( at, line ) / Q_max( DotProduct( line, line ), 0.0001f ) );
				VectorMA( rag->p0[t->b], t2, line, at );
				VectorSubtract( rag->p0[t->a], at, at );
				if ( DotProduct( at, f ) * t->sign > -1.0f ) {
					Rag_AddCon( rag, part, (ragKind_t)t->kind, t->a, t->b, t->c, t->frame, t->sign, 0.0f, 0.0f );
				}
			}
		}
	}
}

/*
=================
CG_RagSplit

The body of ghoul2, drawn at angles and origin with scale, split by plane (the normal towards side 1) into ragdolls of
its own: copies of the model, one per part. centre is the middle of the cut; velocity the speed of the body; the parts
on side 0 (away from the normal) are thrown along throwDir. Returns a handle, -1 when it can not be done (no such
skeleton, a renderer that can not place bones).
=================
*/
int CG_RagSplit( void *ghoul2, const vec3_t angles, const vec3_t origin, const vec3_t scale, const vec4_t plane,
	const vec3_t centre, const vec3_t velocity, const vec3_t throwDir ) {
	ragBody_t	*rag = NULL;
	vec3_t		before[RB_BONES], forward, up, right;
	int			side[RB_BONES], count[RAG_PARTS], i, j, handle = -1, oldest = 0;
	qboolean	any[2];

	if ( !CG_RagOn() || !ghoul2 || !trap->G2_HaveWeGhoul2Models( ghoul2 ) ) {
		return -1;
	}
	for ( i = 0; i < RAG_MAX; i++ ) {
		if ( !rags[i].active ) {
			handle = i;
			break;
		}
		if ( rags[i].start < rags[oldest].start ) {
			oldest = i;
		}
	}
	if ( handle < 0 ) {
		CG_RagFree( oldest );
		handle = oldest;
	}
	rag = &rags[handle];
	memset( rag, 0, sizeof( *rag ) );
	VectorCopy( scale, rag->scale );
	VectorCopy4( plane, rag->plane );
	VectorCopy( centre, rag->centre );

	// the bones now and a moment ago (the speed of the animation)
	for ( i = 0; i < RB_BONES; i++ ) {
		const int	bolt = trap->G2API_AddBolt( ghoul2, 0, ragBones[i].name );
		mdxaBone_t	m;
		vec3_t		axis;

		rag->boneNum[i] = trap->ext.G2_BoneNumber( ghoul2, 0, ragBones[i].name, NULL );
		if ( bolt < 0 || rag->boneNum[i] < 0 ) {
			return -1;
		}
		trap->G2API_GetBoltMatrix( ghoul2, 0, bolt, &m, angles, origin, cg.time - 25, cgs.gameModels, (float *)scale );
		BG_GiveMeVectorFromMatrix( &m, ORIGIN, before[i] );
		trap->G2API_GetBoltMatrix( ghoul2, 0, bolt, &m, angles, origin, cg.time, cgs.gameModels, (float *)scale );
		BG_GiveMeVectorFromMatrix( &m, ORIGIN, rag->p0[i] );
		for ( j = 0; j < 3; j++ ) {
			axis[0] = m.matrix[0][j];
			axis[1] = m.matrix[1][j];
			axis[2] = m.matrix[2][j];
			VectorNormalize( axis );
			rag->rest[i][0][j] = axis[0];
			rag->rest[i][1][j] = axis[1];
			rag->rest[i][2][j] = axis[2];
		}
	}
	// the points that are no bone
	VectorSubtract( rag->p0[RB_LLUMBAR], rag->p0[RB_PELVIS], up );
	VectorSubtract( rag->p0[RB_RFEMUR], rag->p0[RB_LFEMUR], right );
	CrossProduct( up, right, forward );
	VectorNormalize( forward );
	VectorNormalize( up );
	{
		vec3_t d;

		VectorSubtract( rag->p0[RB_CRANIUM], rag->p0[RB_CERVICAL], d );
		VectorNormalize( d );
		VectorMA( rag->p0[RB_CRANIUM], 9.0f, d, rag->p0[RP_HEADTOP] );
		VectorSubtract( rag->p0[RB_LHAND], rag->p0[RB_LRADIUS], d );
		VectorNormalize( d );
		VectorMA( rag->p0[RB_LHAND], 5.0f, d, rag->p0[RP_LHANDTIP] );
		VectorSubtract( rag->p0[RB_RHAND], rag->p0[RB_RRADIUS], d );
		VectorNormalize( d );
		VectorMA( rag->p0[RB_RHAND], 5.0f, d, rag->p0[RP_RHANDTIP] );
		VectorMA( rag->p0[RB_LTALUS], 6.0f, forward, rag->p0[RP_LTOE] );
		VectorMA( rag->p0[RP_LTOE], -1.5f, up, rag->p0[RP_LTOE] );
		VectorMA( rag->p0[RB_RTALUS], 6.0f, forward, rag->p0[RP_RTOE] );
		VectorMA( rag->p0[RP_RTOE], -1.5f, up, rag->p0[RP_RTOE] );
	}
	for ( i = 0; i < RP_POINTS; i++ ) {
		rag->radius[i] = Rag_PointRadius( i );
		rag->invMass[i] = 1.0f / ( i < RB_BONES ? ragBones[i].mass : 0.5f );
	}

	// the side of every bone; a side without one (a cut through the top of the head, the fingers) takes the bone
	// reaching furthest into it
	any[0] = any[1] = qfalse;
	for ( i = 0; i < RB_BONES; i++ ) {
		side[i] = ( DotProduct( rag->p0[i], plane ) - plane[3] >= 0.0f ) ? 1 : 0;
		any[side[i]] = qtrue;
	}
	for ( j = 0; j < 2; j++ ) {
		float	best = 0.0f;
		int		pick = -1;

		if ( any[j] ) {
			continue;
		}
		for ( i = 0; i < RP_POINTS; i++ ) {
			const float d = ( DotProduct( rag->p0[i], plane ) - plane[3] ) * ( j ? 1.0f : -1.0f );
			// the bone a point hangs from
			const int bone = i < RB_BONES ? ragBones[i].parent : ragTips[i - RB_BONES].owner;

			if ( bone >= 0 && d > best ) {
				best = d;
				pick = bone;
			}
		}
		if ( pick < 0 ) {
			return -1;		// (the plane misses the skeleton)
		}
		side[pick] = j;
	}

	// the groups still linked on each side are the parts
	for ( i = 0; i < RB_BONES; i++ ) {
		rag->partOf[i] = -1;
	}
	memset( count, 0, sizeof( count ) );
	for ( i = 0; i < RB_BONES; i++ ) {
		int stack[RB_BONES], top = 0, p;

		if ( rag->partOf[i] >= 0 ) {
			continue;
		}
		if ( rag->numParts < RAG_PARTS ) {
			p = rag->numParts++;
		} else {
			// more groups than parts (rare): to the largest part of the same side
			p = -1;
			for ( j = 0; j < RAG_PARTS; j++ ) {
				if ( rag->part[j].side == side[i] && ( p < 0 || count[j] > count[p] ) ) {
					p = j;
				}
			}
			if ( p < 0 ) {
				p = 0;
			}
		}
		rag->part[p].side = side[i];
		rag->partOf[i] = p;
		stack[top++] = i;
		while ( top ) {
			const int b = stack[--top];

			count[p]++;
			for ( j = 0; j < RB_BONES; j++ ) {
				if ( rag->partOf[j] < 0 && side[j] == side[b] && ( ragBones[j].parent == b || ragBones[b].parent == j ) ) {
					rag->partOf[j] = p;
					stack[top++] = j;
				}
			}
		}
	}

	for ( i = 0; i < rag->numParts; i++ ) {
		ragPart_t	*part = &rag->part[i];
		vec3_t		push, tip;
		float		best = 1e9f, low;
		static const struct { int model, bone; } extra[] = { { 1, RB_RHAND }, { 2, RB_LHAND }, { 3, RB_THORACIC } };

		part->used = qtrue;
		for ( j = 0; j < RP_POINTS; j++ ) {
			part->own[j] = (qboolean)( rag->partOf[Rag_PointBone( j )] == i );
		}
		VectorCopy( velocity, push );
		if ( !part->side && throwDir ) {
			VectorMA( push, 160.0f, throwDir, push );
			push[2] += 60.0f;
		}
		// a small tip to a random side, more the higher a point is: a leg cut off a standing body stood on its foot
		{
			const float yaw = Q_flrand( 0.0f, 2.0f * M_PI );

			VectorSet( tip, cosf( yaw ), sinf( yaw ), 0.0f );
			low = 1e9f;
			for ( j = 0; j < RP_POINTS; j++ ) {
				if ( part->own[j] ) {
					low = Q_min( low, rag->p0[j][2] );
				}
			}
		}
		for ( j = 0; j < RP_POINTS; j++ ) {
			vec3_t v;

			if ( j < RB_BONES ) {
				VectorSubtract( rag->p0[j], before[j], v );
				VectorScale( v, 1000.0f / 25.0f, v );
			} else {
				VectorSubtract( rag->p0[ragTips[j - RB_BONES].owner], before[ragTips[j - RB_BONES].owner], v );
				VectorScale( v, 1000.0f / 25.0f, v );
			}
			VectorAdd( v, push, v );
			VectorMA( v, 30.0f * Com_Clamp( 0.0f, 1.0f, ( rag->p0[j][2] - low ) / 40.0f ), tip, v );
			if ( VectorLength( v ) > RAG_MAX_SPEED ) {
				VectorNormalize( v );
				VectorScale( v, RAG_MAX_SPEED, v );
			}
			VectorCopy( rag->p0[j], part->x[j] );
			VectorCopy( rag->p0[j], part->prev[j] );
			VectorCopy( rag->p0[j], part->draw[j] );
			VectorMA( rag->p0[j], -RAG_STEP, v, part->old[j] );
		}
		for ( j = 0; j < RB_BONES; j++ ) {
			Rag_Identity( part->turn[j] );
			if ( part->own[j] ) {
				Rag_TurnLine( part, j );
				VectorSubtract( rag->p0[part->u1[j]], rag->p0[part->u0[j]], part->lastU[j] );
				VectorNormalize( part->lastU[j] );
			}
			if ( part->own[j] && Distance( rag->p0[j], centre ) < best ) {
				best = Distance( rag->p0[j], centre );
				part->anchor = j;
			}
		}
		Rag_MakeCons( rag, part );

		// its own copy of the model, the animation held, the weapons kept only by the part with the hand
		trap->G2API_DuplicateGhoul2Instance( ghoul2, &part->ghoul2 );
		if ( !part->ghoul2 ) {
			CG_RagFree( handle );
			return -1;
		}
		trap->G2API_SetRagDoll( part->ghoul2, NULL );		// (the body may have been in the engine's ragdoll)
		{
			static const char *held[] = { "model_root", "lower_lumbar", "Motion" };

			for ( j = 0; j < (int)ARRAY_LEN( held ); j++ ) {
				float frame;

				if ( trap->G2API_DoesBoneExist( part->ghoul2, 0, held[j] )
					&& trap->G2API_GetBoneFrame( part->ghoul2, held[j], cg.time, &frame, cgs.gameModels, 0 ) ) {
					trap->G2API_SetBoneAnim( part->ghoul2, 0, held[j], (int)frame, (int)frame + 1, BONE_ANIM_OVERRIDE_FREEZE,
						1.0f, cg.time, frame, 0 );
				}
			}
		}
		for ( j = 0; j < (int)ARRAY_LEN( extra ); j++ ) {
			if ( rag->partOf[extra[j].bone] != i && trap->G2API_HasGhoul2ModelOnIndex( &part->ghoul2, extra[j].model ) ) {
				trap->G2API_RemoveGhoul2Model( &part->ghoul2, extra[j].model );
			}
		}
		VectorCopy( rag->p0[part->anchor], part->origin );
	}
	rag->active = qtrue;
	rag->start = cg.time;
	return handle;
}

// ------------------------------------------------------------------------------------------------ simulation

typedef struct {
	vec3_t	origin, velocity;
} ragMover_t;

static ragMover_t	ragMovers[MAX_GENTITIES / 8];
static int			ragNumMovers;

static void Rag_Wake( ragPart_t *part ) {
	part->asleep = qfalse;
	part->posed = qfalse;
	part->still = 0;
}

// whether a player or an NPC stands in a sleeping part
static qboolean Rag_Touched( const ragBody_t *rag, const ragPart_t *part ) {
	int i, k;

	for ( k = 0; k < ragNumMovers; k++ ) {
		const ragMover_t *m = &ragMovers[k];

		for ( i = 0; i < RP_POINTS; i++ ) {
			const float reach = 15.0f + rag->radius[i];

			if ( part->own[i] && part->x[i][2] > m->origin[2] - 24.0f - rag->radius[i]
				&& part->x[i][2] < m->origin[2] + 40.0f + rag->radius[i]
				&& Square( part->x[i][0] - m->origin[0] ) + Square( part->x[i][1] - m->origin[1] ) < reach * reach ) {
				return qtrue;
			}
		}
	}
	return qfalse;
}

// a push of speed at point p of a part
static void Rag_Kick( ragPart_t *part, int p, const vec3_t dir, float speed ) {
	VectorMA( part->old[p], -speed * RAG_STEP, dir, part->old[p] );
	Rag_Wake( part );
}

static void Rag_SolveDist( ragBody_t *rag, ragPart_t *part, int a, int b, float target, float stiffness ) {
	vec3_t	d;
	float	len, w, diff;

	VectorSubtract( part->x[b], part->x[a], d );
	len = VectorLength( d );
	w = rag->invMass[a] + rag->invMass[b];
	if ( len < 0.0001f || w <= 0.0f ) {
		return;
	}
	diff = ( len - target ) / len * stiffness / w;
	VectorMA( part->x[a], diff * rag->invMass[a], d, part->x[a] );
	VectorMA( part->x[b], -diff * rag->invMass[b], d, part->x[b] );
}

static void Rag_Solve( ragBody_t *rag, ragPart_t *part, float hold ) {
	vec3_t	forward[2];
	int		i;

	if ( Rag_HasFrame( part, RF_PELVIS ) ) {
		Rag_Forward( part->x, RF_PELVIS, forward[RF_PELVIS] );
	}
	if ( Rag_HasFrame( part, RF_CHEST ) ) {
		Rag_Forward( part->x, RF_CHEST, forward[RF_CHEST] );
	}
	for ( i = 0; i < part->numCons; i++ ) {
		const ragCon_t *con = &part->con[i];

		switch ( con->kind ) {
		case RC_DIST:
			Rag_SolveDist( rag, part, con->a, con->b, con->rest, 1.0f );
			break;
		case RC_RANGE: {
			const float len = Distance( part->x[con->a], part->x[con->b] );

			if ( len < con->lo ) {
				Rag_SolveDist( rag, part, con->a, con->b, con->lo, 1.0f );
			} else if ( len > con->hi ) {
				Rag_SolveDist( rag, part, con->a, con->b, con->hi, 1.0f );
			}
			if ( hold > 0.0f ) {
				Rag_SolveDist( rag, part, con->a, con->b, con->rest, hold );
			}
			break;
		}
		case RC_HINGE: {
			const float	*f = forward[con->frame];
			vec3_t		line, at, corr;
			float		t, e, wa, wbc;

			VectorSubtract( part->x[con->c], part->x[con->b], line );
			t = DotProduct( line, line );
			if ( t < 0.0001f ) {
				break;
			}
			VectorSubtract( part->x[con->a], part->x[con->b], at );
			t = Com_Clamp( 0.0f, 1.0f, DotProduct( at, line ) / t );
			VectorMA( part->x[con->b], t, line, at );
			VectorSubtract( part->x[con->a], at, at );
			e = DotProduct( at, f ) * con->sign;
			if ( e >= 0.0f ) {
				break;
			}
			VectorScale( f, -e * con->sign, corr );
			wa = rag->invMass[con->a];
			wbc = 0.5f * ( rag->invMass[con->b] + rag->invMass[con->c] );
			VectorMA( part->x[con->a], wa / ( wa + wbc ), corr, part->x[con->a] );
			VectorMA( part->x[con->b], -0.5f * wbc / ( wa + wbc ), corr, part->x[con->b] );
			VectorMA( part->x[con->c], -0.5f * wbc / ( wa + wbc ), corr, part->x[con->c] );
			break;
		}
		case RC_FRONT: {
			const float	*f = forward[con->frame];
			vec3_t		d;
			float		e, wa, wb;

			VectorSubtract( part->x[con->a], part->x[con->b], d );
			e = DotProduct( d, f ) - con->lo;
			if ( e >= 0.0f ) {
				break;
			}
			wa = rag->invMass[con->a];
			wb = rag->invMass[con->b];
			VectorMA( part->x[con->a], -e * wa / ( wa + wb ), f, part->x[con->a] );
			VectorMA( part->x[con->b], e * wb / ( wa + wb ), f, part->x[con->b] );
			break;
		}
		}
	}
}

// a point from where it was to where it wants to be, stopped by the level
static void Rag_Collide( ragBody_t *rag, ragPart_t *part, int p, const vec3_t from ) {
	trace_t	tr;
	vec3_t	mins, maxs;
	float	r = Q_max( rag->radius[p] * 0.7f, 1.5f );

	if ( VectorCompare( from, part->x[p] ) ) {
		return;
	}
	VectorSet( mins, -r, -r, -r );
	VectorSet( maxs, r, r, r );
	CG_Trace( &tr, from, mins, maxs, part->x[p], ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.allsolid ) {
		return;		// (inside a wall from the start: the bones pull it out)
	}
	if ( tr.fraction < 1.0f ) {
		VectorMA( tr.endpos, 0.1f, tr.plane.normal, part->x[p] );
		VectorCopy( tr.plane.normal, part->normal[p] );
		part->touch[p] = qtrue;
	}
}

static void Rag_Step( ragBody_t *rag, ragPart_t *part, float gravity, float hold ) {
	vec3_t	moved[RP_POINTS];
	float	most = 0.0f;
	int		i, k;

	for ( i = 0; i < RP_POINTS; i++ ) {
		vec3_t v;

		if ( !part->own[i] ) {
			continue;
		}
		VectorCopy( part->x[i], part->prev[i] );
		VectorSubtract( part->x[i], part->old[i], v );
		VectorScale( v, 0.998f, v );
		VectorCopy( part->x[i], part->old[i] );
		VectorAdd( part->x[i], v, part->x[i] );
		part->x[i][2] -= gravity * RAG_STEP * RAG_STEP;
		part->touch[i] = qfalse;
		Rag_Collide( rag, part, i, part->old[i] );

		// players and NPCs push it aside, and give it some of their speed
		for ( k = 0; k < ragNumMovers; k++ ) {
			const ragMover_t	*m = &ragMovers[k];
			const float			reach = 15.0f + rag->radius[i];
			vec3_t				d;
			float				len, along, want;

			if ( part->x[i][2] < m->origin[2] - 24.0f - rag->radius[i] || part->x[i][2] > m->origin[2] + 40.0f + rag->radius[i] ) {
				continue;
			}
			d[0] = part->x[i][0] - m->origin[0];
			d[1] = part->x[i][1] - m->origin[1];
			d[2] = 0.0f;
			len = VectorNormalize( d );
			if ( len >= reach ) {
				continue;
			}
			if ( len < 0.01f ) {
				VectorSet( d, 1.0f, 0.0f, 0.0f );
			}
			VectorMA( part->x[i], reach - len, d, part->x[i] );
			along = ( DotProduct( part->x[i], d ) - DotProduct( part->old[i], d ) ) / RAG_STEP;
			want = Q_max( DotProduct( m->velocity, d ), 60.0f );
			if ( along < want ) {
				VectorMA( part->old[i], -( want - along ) * RAG_STEP, d, part->old[i] );
			}
			Rag_Wake( part );
		}
		VectorCopy( part->x[i], moved[i] );
	}
	for ( k = 0; k < RAG_ITER; k++ ) {
		Rag_Solve( rag, part, hold );
	}
	for ( i = 0; i < RP_POINTS; i++ ) {
		vec3_t v;
		float vn;

		if ( !part->own[i] ) {
			continue;
		}
		// what the bones moved, stopped by the level too
		Rag_Collide( rag, part, i, moved[i] );
		if ( part->touch[i] ) {
			// it slides less, and does not bounce
			VectorSubtract( part->x[i], part->old[i], v );
			vn = DotProduct( v, part->normal[i] );
			VectorMA( v, -vn, part->normal[i], v );
			if ( VectorLength( v ) < 0.05f ) {
				VectorClear( v );
			} else {
				VectorScale( v, 1.0f - RAG_FRICTION, v );
			}
			if ( vn > 0.0f ) {
				VectorMA( v, vn * 0.2f, part->normal[i], v );
			}
			VectorSubtract( part->x[i], v, part->old[i] );
		}
		most = Q_max( most, Distance( part->x[i], part->prev[i] ) );
	}
	if ( most < 0.04f ) {
		if ( ++part->still > RAG_SLEEP_STEPS ) {
			part->asleep = qtrue;
			for ( i = 0; i < RP_POINTS; i++ ) {
				VectorCopy( part->x[i], part->old[i] );
				VectorCopy( part->x[i], part->prev[i] );
			}
		}
	} else {
		part->still = 0;
	}
}

// the players and NPCs that push parts this frame, and force push and pull
static void Rag_Movers( void ) {
	static int	pushing[MAX_CLIENTS];
	int			i, b, p, k;

	ragNumMovers = 0;
	for ( i = 0; i < MAX_GENTITIES && ragNumMovers < (int)ARRAY_LEN( ragMovers ); i++ ) {
		const centity_t	*cent = &cg_entities[i];
		const qboolean	own = (qboolean)( cg.snap && i == cg.snap->ps.clientNum );
		ragMover_t		*m;

		if ( !own && !cent->currentValid ) {
			continue;
		}
		if ( cent->currentState.eType != ET_PLAYER && cent->currentState.eType != ET_NPC ) {
			continue;
		}
		if ( cent->currentState.eFlags & ( EF_DEAD | EF_NODRAW ) ) {
			continue;
		}
		if ( own && ( cg.predictedPlayerState.pm_type == PM_SPECTATOR || cg.predictedPlayerState.stats[STAT_HEALTH] <= 0 ) ) {
			continue;
		}
		m = &ragMovers[ragNumMovers++];
		if ( own ) {
			VectorCopy( cg.predictedPlayerState.origin, m->origin );
			VectorCopy( cg.predictedPlayerState.velocity, m->velocity );
		} else {
			VectorCopy( cent->lerpOrigin, m->origin );
			VectorCopy( cent->currentState.pos.trDelta, m->velocity );
		}
	}

	// force push and pull: when the hand effect of a player begins (PW_DISINT_4, PW_PULL for pull), the parts in front
	// of him within reach
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const centity_t	*cent = &cg_entities[i];
		const qboolean	own = (qboolean)( cg.snap && i == cg.snap->ps.clientNum );
		const int		now = ( ( own || cent->currentValid ) && ( cent->currentState.powerups & ( 1 << PW_DISINT_4 ) ) ) ? 1 : 0;
		vec3_t			from, forward;
		qboolean		pull;

		if ( !now || pushing[i] ) {
			pushing[i] = now;
			continue;
		}
		pushing[i] = now;
		pull = (qboolean)( ( cent->currentState.powerups & ( 1 << PW_PULL ) ) != 0 );
		if ( own ) {
			VectorCopy( cg.predictedPlayerState.origin, from );
			AngleVectors( cg.predictedPlayerState.viewangles, forward, NULL, NULL );
		} else {
			VectorCopy( cent->lerpOrigin, from );
			AngleVectors( cent->lerpAngles, forward, NULL, NULL );
		}
		from[2] += 20.0f;
		for ( b = 0; b < RAG_MAX; b++ ) {
			if ( !rags[b].active ) {
				continue;
			}
			for ( p = 0; p < rags[b].numParts; p++ ) {
				ragPart_t *part = &rags[b].part[p];

				for ( k = 0; k < RP_POINTS; k++ ) {
					vec3_t	d;
					float	len, speed;

					if ( !part->own[k] ) {
						continue;
					}
					VectorSubtract( part->x[k], from, d );
					len = VectorNormalize( d );
					if ( len > 512.0f || DotProduct( d, forward ) < 0.5f ) {
						continue;
					}
					speed = 150.0f + 500.0f * ( 1.0f - len / 512.0f );
					if ( pull ) {
						VectorScale( d, -1.0f, d );
						speed = Q_min( speed, len * 3.0f );
					}
					d[2] += 0.25f;
					Rag_Kick( part, k, d, speed );
				}
			}
		}
	}
}

/*
=================
CG_RagExplosion

An explosion at origin throws the parts within radius away (cg_weapons.c)
=================
*/
void CG_RagExplosion( const vec3_t origin, float radius ) {
	int b, p, k;

	for ( b = 0; b < RAG_MAX; b++ ) {
		if ( !rags[b].active ) {
			continue;
		}
		for ( p = 0; p < rags[b].numParts; p++ ) {
			ragPart_t *part = &rags[b].part[p];

			for ( k = 0; k < RP_POINTS; k++ ) {
				vec3_t	d;
				float	len;

				if ( !part->own[k] ) {
					continue;
				}
				VectorSubtract( part->x[k], origin, d );
				len = VectorNormalize( d );
				if ( len > radius ) {
					continue;
				}
				d[2] += 0.4f;
				VectorNormalize( d );
				Rag_Kick( part, k, d, 100.0f + 700.0f * ( 1.0f - len / radius ) );
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------ drawing

// how an own bone turned since the cut, from its points
static void Rag_Turn( ragBody_t *rag, ragPart_t *part, int bone ) {
	const int	u0 = part->u0[bone], u1 = part->u1[bone], w0 = ragBones[bone].w0, w1 = ragBones[bone].w1;
	vec3_t		u, w, u0v, w0v;
	float		now[3][3], then[3][3], step[3][3];

	if ( u0 == u1 ) {
		// (no second point in this part: it turns with the bone of the part at the cut)
		memcpy( part->turn[bone], part->turn[part->anchor], sizeof( part->turn[bone] ) );
		return;
	}
	VectorSubtract( part->draw[u1], part->draw[u0], u );
	if ( VectorNormalize( u ) < 0.01f ) {
		return;
	}
	// pelvis, spine, neck, head: their roll from the line of the hips or the shoulders
	if ( w0 >= 0 && u0 == ragBones[bone].u0 && u1 == ragBones[bone].u1 && part->own[w0] && part->own[w1] ) {
		VectorSubtract( part->draw[w1], part->draw[w0], w );
		VectorSubtract( rag->p0[u1], rag->p0[u0], u0v );
		VectorSubtract( rag->p0[w1], rag->p0[w0], w0v );
		if ( Rag_Basis( u, w, now ) && Rag_Basis( u0v, w0v, then ) ) {
			int i, j;

			for ( i = 0; i < 3; i++ ) {
				for ( j = 0; j < 3; j++ ) {
					part->turn[bone][i][j] = now[i][0] * then[j][0] + now[i][1] * then[j][1] + now[i][2] * then[j][2];
				}
			}
			VectorCopy( u, part->lastU[bone] );
			return;
		}
	}
	// arms and legs: turned the shortest way from where they pointed (their roll carried along, no jumps)
	Rag_Arc( part->lastU[bone], u, step );
	Rag_Mul( step, part->turn[bone], part->turn[bone] );
	VectorCopy( u, part->lastU[bone] );
}

static void Rag_Frame( const float rest[3][3], const float turn[3][3], const vec3_t at, float *out ) {
	float	axes[3][3];
	int		i;

	Rag_Mul( turn, rest, axes );
	for ( i = 0; i < 3; i++ ) {
		out[i * 4 + 0] = axes[i][0];
		out[i * 4 + 1] = axes[i][1];
		out[i * 4 + 2] = axes[i][2];
		out[i * 4 + 3] = at[i];
	}
}

// the bones of a part placed on its copy of the model
static void Rag_Pose( ragBody_t *rag, ragPart_t *part, int partNum ) {
	static const float	zero[3][3] = { { 0 } };
	float				world[RB_BONES][12];
	const int			a = part->anchor;
	int					i;

	// the anchor first: the others may turn with it
	Rag_Turn( rag, part, a );
	for ( i = 0; i < RB_BONES; i++ ) {
		if ( part->own[i] && i != a ) {
			Rag_Turn( rag, part, i );
		}
	}
	VectorCopy( part->draw[a], part->origin );
	for ( i = 0; i < RB_BONES; i++ ) {
		if ( part->own[i] ) {
			Rag_Frame( rag->rest[i], part->turn[i], part->draw[i], world[i] );
		} else {
			// the bones of the other side follow the bone at the cut as they were then (behind the plane, clipped);
			// another part on this side is shrunk to nothing, it is drawn by its own copy
			vec3_t off, at;

			VectorSubtract( rag->p0[i], rag->p0[a], off );
			Rag_Apply( part->turn[a], off, off );
			VectorAdd( part->draw[a], off, at );
			if ( rag->part[rag->partOf[i]].side == part->side ) {
				Rag_Frame( rag->rest[i], zero, at, world[i] );
			} else {
				Rag_Frame( rag->rest[i], part->turn[a], at, world[i] );
			}
		}
	}
	trap->ext.G2_SetBonesWorld( part->ghoul2, 0, vec3_origin, part->origin, rag->scale, RB_BONES, rag->boneNum, &world[0][0] );

	// (developer 1: the bone at the cut must come back where it was put)
	if ( !part->checked ) {
		char value[8];

		part->checked = qtrue;
		trap->Cvar_VariableStringBuffer( "developer", value, sizeof( value ) );
		if ( atoi( value ) ) {
			const int	bolt = trap->G2API_AddBolt( part->ghoul2, 0, ragBones[a].name );
			mdxaBone_t	m;
			vec3_t		got;

			if ( bolt >= 0 && trap->G2API_GetBoltMatrix( part->ghoul2, 0, bolt, &m, vec3_origin, part->origin, cg.time,
				cgs.gameModels, rag->scale ) ) {
				BG_GiveMeVectorFromMatrix( &m, ORIGIN, got );
				Com_Printf( "v_slice: part %i (%s, %i parts) bone round trip off by %.2f units\n", partNum, ragBones[a].name,
					rag->numParts, Distance( got, part->draw[a] ) );
			}
		}
	}
}

/*
=================
CG_RagFrame

Every frame, before the parts are drawn (cg_slice.c): the steps of all ragdolls since the last frame, and their bones
=================
*/
void CG_RagFrame( void ) {
	float	gravity = cg.predictedPlayerState.gravity > 0 ? (float)cg.predictedPlayerState.gravity : 800.0f;
	int		b, p, s, i;
	qboolean any = qfalse;

	for ( b = 0; b < RAG_MAX; b++ ) {
		any |= rags[b].active;
	}
	if ( !any ) {
		return;
	}
	Rag_Movers();
	for ( b = 0; b < RAG_MAX; b++ ) {
		ragBody_t	*rag = &rags[b];
		int			steps;
		float		alpha, hold;

		if ( !rag->active ) {
			continue;
		}
		rag->acc += Com_Clamp( 0.0f, 0.1f, cg.frametime * 0.001f );
		steps = (int)( rag->acc / RAG_STEP );
		rag->acc -= steps * RAG_STEP;
		if ( steps > RAG_MAX_STEPS ) {
			steps = RAG_MAX_STEPS;
		}
		hold = 0.5f * ( 1.0f - Com_Clamp( 0.0f, 1.0f, (float)( cg.time - rag->start ) / RAG_HOLD_MSEC ) );
		for ( p = 0; p < rag->numParts; p++ ) {
			ragPart_t *part = &rag->part[p];

			if ( part->asleep && Rag_Touched( rag, part ) ) {
				Rag_Wake( part );
			}
			for ( s = 0; s < steps && !part->asleep; s++ ) {
				Rag_Step( rag, part, gravity, hold );
			}
			if ( part->asleep && part->posed ) {
				continue;
			}
			part->posed = part->asleep;
			// drawn between the last two steps
			alpha = Com_Clamp( 0.0f, 1.0f, rag->acc / RAG_STEP );
			for ( i = 0; i < RP_POINTS; i++ ) {
				if ( part->asleep ) {
					VectorCopy( part->x[i], part->draw[i] );
				} else {
					VectorAdvance( part->prev[i], alpha, part->x[i], part->draw[i] );
				}
			}
			Rag_Pose( rag, part, p );
		}
	}
}

/*
=================
CG_RagPart

Part n of a ragdoll for drawing: its copy of the model (posed by CG_RagFrame, drawn at origin with angles 0) and its
cut (the normal towards the part). qfalse: no such part.
=================
*/
qboolean CG_RagPart( int handle, int n, void **ghoul2, vec3_t origin, vec4_t plane ) {
	const ragBody_t	*rag;
	const ragPart_t	*part;
	vec3_t			normal, centre;

	if ( handle < 0 || handle >= RAG_MAX || !rags[handle].active || n < 0 || n >= rags[handle].numParts ) {
		return qfalse;
	}
	rag = &rags[handle];
	part = &rag->part[n];
	if ( !part->ghoul2 ) {
		return qfalse;
	}
	*ghoul2 = part->ghoul2;
	VectorCopy( part->origin, origin );
	// the cut turns and moves with the bone of the part at the cut
	Rag_Apply( part->turn[part->anchor], rag->plane, normal );
	VectorSubtract( rag->centre, rag->p0[part->anchor], centre );
	Rag_Apply( part->turn[part->anchor], centre, centre );
	VectorAdd( centre, part->draw[part->anchor], centre );
	if ( !part->side ) {
		VectorScale( normal, -1.0f, normal );
	}
	VectorCopy( normal, plane );
	plane[3] = DotProduct( normal, centre );
	return qtrue;
}

int CG_RagParts( int handle ) {
	if ( handle < 0 || handle >= RAG_MAX || !rags[handle].active ) {
		return 0;
	}
	return rags[handle].numParts;
}
