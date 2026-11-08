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

// cg_ragdoll.c -- VisionJK: the ragdoll of the bodies a saber cut through (cg_slice.c), their wish 2026-11-08.
//
// The body is twelve rigid bodies (pelvis, belly, chest, head, upper arms, forearms with the hands, thighs, shins with
// the feet), each a capsule measured from the bones at the moment of the cut, with mass and inertia, joined by joints
// with the limits of a body: cones and twist for spine, neck, shoulders and hips, one-sided hinges for elbows and
// knees. They are moved by extended position based dynamics (XPBD, Mueller et al. 2020, "Detailed Rigid Body Simulation
// with Extended Position Based Dynamics"): fixed steps of 1/60 s in four substeps, each moving the bodies and then
// correcting positions and turns once for every joint and contact, then friction on the speeds. A first version of
// points held together (Verlet) had no turn of its own and folded oddly (their word 2026-11-08: "the body
// physics/ragdolls are bad").
//
// The level is felt by traces: once a step every body's spheres (ends and middle of its capsule) are traced along
// the way they are going, and what they hit becomes a contact plane for the substeps, with static and sliding
// friction. Bodies that are not joined keep apart from each other, the parts of one cut too. Players and NPCs push
// them, as do force push, force pull and explosions. For 0.4 s the joints pull toward the pose of the moment, weaker
// and weaker, so the body slumps instead of folding; a part that has come to rest sleeps until something pushes it.
//
// At the cut every body goes to the side of the plane its middle lies on; the joints across the plane are dropped and
// every group still joined is a part of its own (two, or more when a bent arm crosses the plane twice), with its own
// copy of the model. A part is drawn with its own bones where its bodies are (ext.G2_SetBonesWorld, rd-vulkan-vision);
// the bones of the other side follow its body at the cut as they were at the moment of the cut (behind the plane,
// clipped away), and the bones of another part on the same side are placed with no axes, which the renderer's skinning
// takes as "not drawn here" (shaders/skinned.vert). All of it is decided on this client alone.

#include "cg_local.h"
#include "ghoul2/G2.h"

#define RAG_MAX			10			// cut bodies
#define RAG_PARTS		4
#define RAG_STEP		( 1.0f / 60.0f )
#define RAG_SUBSTEPS	4
#define RAG_MAX_STEPS	4
#define RAG_HOLD_MSEC	400			// the joints pull toward the pose of the cut for this long, weaker and weaker
#define RAG_SLEEP_STEPS	30			// steps at rest before a part sleeps
#define RAG_MU_STATIC	0.8f
#define RAG_MU_SLIDE	0.6f
#define RAG_MAX_SPEED	900.0f
#define RAG_MAX_SPIN	12.0f		// radians a second
#define RAG_SPHERES		4			// a body's spheres for contact
#define RAG_PLANES		2			// contact planes a sphere keeps

// the bones of the skeleton the ragdoll places
enum {
	RB_PELVIS, RB_LLUMBAR, RB_ULUMBAR, RB_THORACIC, RB_CERVICAL, RB_CRANIUM,
	RB_LHUMERUS, RB_LRADIUS, RB_LHAND, RB_RHUMERUS, RB_RRADIUS, RB_RHAND,
	RB_LFEMUR, RB_LTIBIA, RB_LTALUS, RB_RFEMUR, RB_RTIBIA, RB_RTALUS,
	RB_BONES
};

static const char *ragBoneNames[RB_BONES] = {
	"pelvis", "lower_lumbar", "upper_lumbar", "thoracic", "cervical", "cranium",
	"lhumerus", "lradius", "lhand", "rhumerus", "rradius", "rhand",
	"lfemurYZ", "ltibia", "ltalus", "rfemurYZ", "rtibia", "rtalus",
};

// the rigid bodies
enum {
	BD_PELVIS, BD_BELLY, BD_CHEST, BD_HEAD,
	BD_LUARM, BD_LFARM, BD_RUARM, BD_RFARM,
	BD_LTHIGH, BD_LSHIN, BD_RTHIGH, BD_RSHIN,
	BD_BODIES
};

// points the capsules are measured between, besides the bones
enum {
	RP_HEADTOP = RB_BONES, RP_LHANDTIP, RP_RHANDTIP, RP_LTOE, RP_RTOE,
	RP_POINTS
};

static const struct {
	int		bones[2];		// the bones that move with it (-1: none)
	int		from, to;		// its capsule
	float	radius, mass;
	int		extra;			// one more sphere at this point (-1: none)
	float	extraRadius;
} ragBodyDefs[BD_BODIES] = {
	{ { RB_PELVIS, -1 },			RB_LFEMUR, RB_RFEMUR,		6.5f, 12.0f,	RB_PELVIS, 7.0f },
	{ { RB_LLUMBAR, RB_ULUMBAR },	RB_LLUMBAR, RB_THORACIC,	6.5f, 10.0f,	-1, 0.0f },
	{ { RB_THORACIC, -1 },			RB_LHUMERUS, RB_RHUMERUS,	6.0f, 15.0f,	RB_THORACIC, 7.5f },
	{ { RB_CERVICAL, RB_CRANIUM },	RB_CERVICAL, RP_HEADTOP,	4.5f, 5.0f,		-1, 0.0f },
	{ { RB_LHUMERUS, -1 },			RB_LHUMERUS, RB_LRADIUS,	3.2f, 2.5f,		-1, 0.0f },
	{ { RB_LRADIUS, RB_LHAND },		RB_LRADIUS, RP_LHANDTIP,	2.8f, 2.0f,		-1, 0.0f },
	{ { RB_RHUMERUS, -1 },			RB_RHUMERUS, RB_RRADIUS,	3.2f, 2.5f,		-1, 0.0f },
	{ { RB_RRADIUS, RB_RHAND },		RB_RRADIUS, RP_RHANDTIP,	2.8f, 2.0f,		-1, 0.0f },
	{ { RB_LFEMUR, -1 },			RB_LFEMUR, RB_LTIBIA,		4.5f, 7.0f,		-1, 0.0f },
	{ { RB_LTIBIA, RB_LTALUS },		RB_LTIBIA, RB_LTALUS,		3.6f, 4.0f,		RP_LTOE, 2.5f },
	{ { RB_RFEMUR, -1 },			RB_RFEMUR, RB_RTIBIA,		4.5f, 7.0f,		-1, 0.0f },
	{ { RB_RTIBIA, RB_RTALUS },		RB_RTIBIA, RB_RTALUS,		3.6f, 4.0f,		RP_RTOE, 2.5f },
};

typedef enum { RJ_CONE, RJ_HINGE } ragJointKind_t;

// the joints: child body, parent body, the point they are joined at, and the limits (degrees)
static const struct {
	int				child, parent, at;
	ragJointKind_t	kind;
	float			swing, twist;	// RJ_CONE
	float			bend;			// RJ_HINGE: 0 straight to this
} ragJointDefs[] = {
	{ BD_BELLY,	BD_PELVIS,	RB_LLUMBAR,		RJ_CONE,	25.0f, 20.0f, 0.0f },
	{ BD_CHEST,	BD_BELLY,	RB_THORACIC,	RJ_CONE,	25.0f, 20.0f, 0.0f },
	{ BD_HEAD,	BD_CHEST,	RB_CERVICAL,	RJ_CONE,	40.0f, 40.0f, 0.0f },
	{ BD_LUARM,	BD_CHEST,	RB_LHUMERUS,	RJ_CONE,	100.0f, 70.0f, 0.0f },
	{ BD_RUARM,	BD_CHEST,	RB_RHUMERUS,	RJ_CONE,	100.0f, 70.0f, 0.0f },
	{ BD_LFARM,	BD_LUARM,	RB_LRADIUS,		RJ_HINGE,	0.0f, 0.0f, 145.0f },
	{ BD_RFARM,	BD_RUARM,	RB_RRADIUS,		RJ_HINGE,	0.0f, 0.0f, 145.0f },
	{ BD_LTHIGH, BD_PELVIS,	RB_LFEMUR,		RJ_CONE,	65.0f, 35.0f, 0.0f },
	{ BD_RTHIGH, BD_PELVIS,	RB_RFEMUR,		RJ_CONE,	65.0f, 35.0f, 0.0f },
	{ BD_LSHIN,	BD_LTHIGH,	RB_LTIBIA,		RJ_HINGE,	0.0f, 0.0f, 145.0f },
	{ BD_RSHIN,	BD_RTHIGH,	RB_RTIBIA,		RJ_HINGE,	0.0f, 0.0f, 145.0f },
};
#define RAG_JOINTS	( (int)ARRAY_LEN( ragJointDefs ) )

typedef struct {
	vec3_t		x, v, w;			// centre of mass, speed, spin (world)
	vec4_t		q;					// turn from the body frame into the world (x, y, z, w)
	vec3_t		px;					// at the start of the substep
	vec4_t		pq;
	vec3_t		sx;					// at the start of the step, for drawing between steps
	vec4_t		sq;
	vec3_t		dx;					// as drawn
	vec4_t		dq;
	float		invMass;
	vec3_t		invInertia;			// in the body frame
	int			numSpheres;
	vec3_t		sphere[RAG_SPHERES];	// in the body frame
	float		radius[RAG_SPHERES];
	vec3_t		planeN[RAG_SPHERES][RAG_PLANES];	// contact planes of this step: normal and distance
	float		planeD[RAG_SPHERES][RAG_PLANES];
	int			numPlanes[RAG_SPHERES];
	float		contactLambda[RAG_SPHERES];			// normal correction of the substep, for the sliding friction
	vec3_t		contactNormal[RAG_SPHERES];
	vec3_t		contactR[RAG_SPHERES];
	qboolean	touched[RAG_SPHERES];
	int			part;
} ragRigid_t;

typedef struct {
	qboolean	used;
	int			child, parent;
	ragJointKind_t kind;
	vec3_t		rc, rp;				// the point of the joint in each body frame
	vec3_t		axisC, axisP;		// cone: the child's line and its neutral line in the parent; hinge: the axis
	vec3_t		refC, refP;			// cone: twist references; hinge: the lines the bend is measured between
	float		swing, twist, lo, hi;	// radians
	vec4_t		rest;				// the turn of the child against the parent at the cut, for the pose hold
} ragJoint_t;

typedef struct {
	qboolean	used;
	int			side;				// 1: the side the normal of the cut points to
	int			anchor;				// its body nearest the cut
	void		*ghoul2;
	vec3_t		origin;				// drawn at, angles 0
	int			still;
	qboolean	asleep;
	qboolean	posed;				// asleep and its bones placed
	qboolean	checked;			// (developer: the bone round trip was looked at)
	qboolean	cutSent;			// the renderer has the cut of the first pose (ext.G2_SetBonesWorld)
	vec3_t		restX[BD_BODIES];	// where its bodies were when it last moved, for the sleep test
	vec4_t		restQ[BD_BODIES];
} ragPart_t;

typedef struct {
	qboolean	active;
	int			start;
	float		acc;
	int			boneNum[RB_BONES];
	float		boneRot[RB_BONES][3][3];	// each bone in the frame of its body
	vec3_t		bonePos[RB_BONES];
	int			boneBody[RB_BONES];
	vec3_t		p0[RP_POINTS];		// the points at the cut
	vec3_t		bx0[BD_BODIES];		// the bodies at the cut
	vec4_t		bq0[BD_BODIES];
	vec3_t		scale;
	vec4_t		plane;				// the cut, the normal towards side 1
	vec3_t		centre;
	ragRigid_t	body[BD_BODIES];
	ragJoint_t	joint[RAG_JOINTS];
	qboolean	apart[BD_BODIES][BD_BODIES];	// bodies that keep apart (not joined, not touching at the cut)
	int			numParts;
	ragPart_t	part[RAG_PARTS];
} ragBody_t;

static ragBody_t	rags[RAG_MAX];

// ------------------------------------------------------------------------------------------------ maths

static void Q_Mul( const vec4_t a, const vec4_t b, vec4_t out ) {
	vec4_t t;

	t[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	t[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	t[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	t[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
	VectorCopy4( t, out );
}

static void Q_Conj( const vec4_t q, vec4_t out ) {
	out[0] = -q[0];
	out[1] = -q[1];
	out[2] = -q[2];
	out[3] = q[3];
}

static void Q_Normalize( vec4_t q ) {
	const float l = sqrtf( q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3] );

	if ( l > 0.000001f ) {
		q[0] /= l;
		q[1] /= l;
		q[2] /= l;
		q[3] /= l;
	} else {
		q[0] = q[1] = q[2] = 0.0f;
		q[3] = 1.0f;
	}
}

static void Q_Rotate( const vec4_t q, const vec3_t v, vec3_t out ) {
	// v + 2 q.xyz x ( q.xyz x v + w v )
	vec3_t	t, u;

	CrossProduct( q, v, t );
	VectorMA( t, q[3], v, t );
	CrossProduct( q, t, u );
	VectorMA( v, 2.0f, u, out );
}

static void Q_Unrotate( const vec4_t q, const vec3_t v, vec3_t out ) {
	vec4_t c;

	Q_Conj( q, c );
	Q_Rotate( c, v, out );
}

// columns of the matrix are the axes of the frame
static void Q_ToMatrix( const vec4_t q, float m[3][3] ) {
	const float x = q[0], y = q[1], z = q[2], w = q[3];

	m[0][0] = 1.0f - 2.0f * ( y * y + z * z );	m[0][1] = 2.0f * ( x * y - z * w );			m[0][2] = 2.0f * ( x * z + y * w );
	m[1][0] = 2.0f * ( x * y + z * w );			m[1][1] = 1.0f - 2.0f * ( x * x + z * z );	m[1][2] = 2.0f * ( y * z - x * w );
	m[2][0] = 2.0f * ( x * z - y * w );			m[2][1] = 2.0f * ( y * z + x * w );			m[2][2] = 1.0f - 2.0f * ( x * x + y * y );
}

static void Q_FromMatrix( const float m[3][3], vec4_t q ) {
	const float tr = m[0][0] + m[1][1] + m[2][2];

	if ( tr > 0.0f ) {
		const float s = sqrtf( tr + 1.0f ) * 2.0f;

		q[3] = 0.25f * s;
		q[0] = ( m[2][1] - m[1][2] ) / s;
		q[1] = ( m[0][2] - m[2][0] ) / s;
		q[2] = ( m[1][0] - m[0][1] ) / s;
	} else if ( m[0][0] > m[1][1] && m[0][0] > m[2][2] ) {
		const float s = sqrtf( 1.0f + m[0][0] - m[1][1] - m[2][2] ) * 2.0f;

		q[3] = ( m[2][1] - m[1][2] ) / s;
		q[0] = 0.25f * s;
		q[1] = ( m[0][1] + m[1][0] ) / s;
		q[2] = ( m[0][2] + m[2][0] ) / s;
	} else if ( m[1][1] > m[2][2] ) {
		const float s = sqrtf( 1.0f + m[1][1] - m[0][0] - m[2][2] ) * 2.0f;

		q[3] = ( m[0][2] - m[2][0] ) / s;
		q[0] = ( m[0][1] + m[1][0] ) / s;
		q[1] = 0.25f * s;
		q[2] = ( m[1][2] + m[2][1] ) / s;
	} else {
		const float s = sqrtf( 1.0f + m[2][2] - m[0][0] - m[1][1] ) * 2.0f;

		q[3] = ( m[1][0] - m[0][1] ) / s;
		q[0] = ( m[0][2] + m[2][0] ) / s;
		q[1] = ( m[1][2] + m[2][1] ) / s;
		q[2] = 0.25f * s;
	}
	Q_Normalize( q );
}

// the turn from a to b as a rotation vector
static void Q_Delta( const vec4_t from, const vec4_t to, vec3_t rot ) {
	vec4_t	c, d;
	float	s, angle;

	Q_Conj( from, c );
	Q_Mul( to, c, d );
	if ( d[3] < 0.0f ) {
		d[0] = -d[0];
		d[1] = -d[1];
		d[2] = -d[2];
		d[3] = -d[3];
	}
	s = VectorLength( d );
	if ( s < 0.000001f ) {
		VectorClear( rot );
		return;
	}
	angle = 2.0f * atan2f( s, d[3] );
	VectorScale( d, angle / s, rot );
}

// q turned by the rotation vector r times scale (small turns)
static void Q_AddRotation( vec4_t q, const vec3_t r, float scale ) {
	vec4_t w, d;

	w[0] = r[0] * scale;
	w[1] = r[1] * scale;
	w[2] = r[2] * scale;
	w[3] = 0.0f;
	Q_Mul( w, q, d );
	q[0] += 0.5f * d[0];
	q[1] += 0.5f * d[1];
	q[2] += 0.5f * d[2];
	q[3] += 0.5f * d[3];
	Q_Normalize( q );
}

static void M_Mul( const float a[3][3], const float b[3][3], float out[3][3] ) {
	float	t[3][3];
	int		i, j;

	for ( i = 0; i < 3; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			t[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
		}
	}
	memcpy( out, t, sizeof( t ) );
}

static void M_TransposeMul( const float a[3][3], const float b[3][3], float out[3][3] ) {
	float	t[3][3];
	int		i, j;

	for ( i = 0; i < 3; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			t[i][j] = a[0][i] * b[0][j] + a[1][i] * b[1][j] + a[2][i] * b[2][j];
		}
	}
	memcpy( out, t, sizeof( t ) );
}

// the shortest turn taking unit a to unit b, applied to v
static void V_TurnLike( const vec3_t a, const vec3_t b, const vec3_t v, vec3_t out ) {
	vec3_t	axis;
	float	s, c;
	vec4_t	q;

	CrossProduct( a, b, axis );
	s = VectorNormalize( axis );
	c = DotProduct( a, b );
	if ( s < 0.00001f ) {
		VectorCopy( v, out );
		return;
	}
	{
		const float half = 0.5f * atan2f( s, c );
		const float sh = sinf( half );

		q[0] = axis[0] * sh;
		q[1] = axis[1] * sh;
		q[2] = axis[2] * sh;
		q[3] = cosf( half );
	}
	Q_Rotate( q, v, out );
}

// ------------------------------------------------------------------------------------------------ the corrections

static void Rag_InvInertiaWorld( const ragRigid_t *b, const vec3_t v, vec3_t out ) {
	vec3_t l;

	Q_Unrotate( b->q, v, l );
	l[0] *= b->invInertia[0];
	l[1] *= b->invInertia[1];
	l[2] *= b->invInertia[2];
	Q_Rotate( b->q, l, out );
}

// how hard a body is to move at the offset r along n (inverse mass seen there)
static float Rag_GenInvMass( const ragRigid_t *b, const vec3_t r, const vec3_t n ) {
	vec3_t rn, irn;

	if ( !b ) {
		return 0.0f;
	}
	CrossProduct( r, n, rn );
	Rag_InvInertiaWorld( b, rn, irn );
	return b->invMass + DotProduct( rn, irn );
}

// Moves the point of a (offset ra from its centre) by corr against the point of b (ra, rb in the world; b may be
// NULL: the level). compliance 0: all of it. Returns the size of the correction (lambda).
static float Rag_Positional( ragRigid_t *a, const vec3_t ra, ragRigid_t *b, const vec3_t rb, const vec3_t corr,
	float compliance, float h ) {
	vec3_t	n, p, t, it;
	float	c, w, lambda;

	VectorCopy( corr, n );
	c = VectorNormalize( n );
	if ( c < 0.00001f ) {
		return 0.0f;
	}
	w = Rag_GenInvMass( a, ra, n ) + Rag_GenInvMass( b, rb, n );
	if ( w <= 0.0f ) {
		return 0.0f;
	}
	lambda = c / ( w + compliance / ( h * h ) );
	VectorScale( n, lambda, p );
	VectorMA( a->x, a->invMass, p, a->x );
	CrossProduct( ra, p, t );
	Rag_InvInertiaWorld( a, t, it );
	Q_AddRotation( a->q, it, 1.0f );
	if ( b ) {
		VectorMA( b->x, -b->invMass, p, b->x );
		CrossProduct( rb, p, t );
		Rag_InvInertiaWorld( b, t, it );
		Q_AddRotation( b->q, it, -1.0f );
	}
	return lambda;
}

// Turns a by the rotation vector corr against b (shared by their inertia); compliance 0: all of it
static void Rag_Angular( ragRigid_t *a, ragRigid_t *b, const vec3_t corr, float compliance, float h ) {
	vec3_t	n, ia, ib, p;
	float	theta, w, lambda;

	VectorCopy( corr, n );
	theta = VectorNormalize( n );
	if ( theta < 0.00001f ) {
		return;
	}
	Rag_InvInertiaWorld( a, n, ia );
	Rag_InvInertiaWorld( b, n, ib );
	w = DotProduct( n, ia ) + DotProduct( n, ib );
	if ( w <= 0.0f ) {
		return;
	}
	lambda = theta / ( w + compliance / ( h * h ) );
	VectorScale( n, lambda, p );
	Rag_InvInertiaWorld( a, p, ia );
	Rag_InvInertiaWorld( b, p, ib );
	Q_AddRotation( a->q, ia, 1.0f );
	Q_AddRotation( b->q, ib, -1.0f );
}

// ------------------------------------------------------------------------------------------------ building

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

// a world point of a body at the cut into its frame
static void Rag_ToBody( const ragBody_t *rag, int body, const vec3_t world, vec3_t local ) {
	vec3_t d;

	VectorSubtract( world, rag->bx0[body], d );
	Q_Unrotate( rag->bq0[body], d, local );
}

static void Rag_DirToBody( const ragBody_t *rag, int body, const vec3_t world, vec3_t local ) {
	Q_Unrotate( rag->bq0[body], world, local );
}

// a unit vector across a, as near to hint as can be
static void Rag_Across( const vec3_t a, const vec3_t hint, vec3_t out ) {
	VectorMA( hint, -DotProduct( hint, a ), a, out );
	if ( VectorNormalize( out ) < 0.01f ) {
		PerpendicularVector( out, a );
	}
}

// the body frames and capsules from the points of the cut
static void Rag_MakeBodies( ragBody_t *rag, const vec3_t forward, const vec3_t right ) {
	int i, j;

	for ( i = 0; i < BD_BODIES; i++ ) {
		ragRigid_t	*b = &rag->body[i];
		vec3_t		axis, across, third, start, end;
		float		m[3][3], half, r = ragBodyDefs[i].radius, length, cyl;

		VectorCopy( rag->p0[ragBodyDefs[i].from], start );
		VectorCopy( rag->p0[ragBodyDefs[i].to], end );
		if ( i == BD_CHEST ) {
			// (the shoulder joints are at the ends of the chest: a little inside)
			vec3_t d;

			VectorSubtract( end, start, d );
			VectorMA( start, 0.15f, d, start );
			VectorMA( end, -0.15f, d, end );
		}
		VectorSubtract( end, start, axis );
		length = VectorNormalize( axis );
		if ( length < 1.0f ) {
			VectorCopy( forward, axis );
			length = 1.0f;
		}
		Rag_Across( axis, ( i == BD_PELVIS || i == BD_CHEST ) ? forward : right, across );
		CrossProduct( axis, across, third );
		for ( j = 0; j < 3; j++ ) {
			m[j][0] = axis[j];
			m[j][1] = across[j];
			m[j][2] = third[j];
		}
		Q_FromMatrix( m, rag->bq0[i] );
		VectorAdd( start, end, rag->bx0[i] );
		VectorScale( rag->bx0[i], 0.5f, rag->bx0[i] );

		half = 0.5f * length;
		b->numSpheres = 0;
		VectorSet( b->sphere[b->numSpheres], -half, 0.0f, 0.0f );
		b->radius[b->numSpheres++] = r;
		VectorSet( b->sphere[b->numSpheres], 0.0f, 0.0f, 0.0f );
		b->radius[b->numSpheres++] = r;
		VectorSet( b->sphere[b->numSpheres], half, 0.0f, 0.0f );
		b->radius[b->numSpheres++] = r;
		if ( ragBodyDefs[i].extra >= 0 ) {
			Rag_ToBody( rag, i, rag->p0[ragBodyDefs[i].extra], b->sphere[b->numSpheres] );
			b->radius[b->numSpheres++] = ragBodyDefs[i].extraRadius;
		}

		// a solid cylinder of the capsule's length and radius
		b->invMass = 1.0f / ragBodyDefs[i].mass;
		cyl = ragBodyDefs[i].mass * ( 3.0f * r * r + length * length ) / 12.0f;
		b->invInertia[0] = 1.0f / ( 0.5f * ragBodyDefs[i].mass * r * r );
		b->invInertia[1] = b->invInertia[2] = 1.0f / Q_max( cyl, 0.0001f );

		VectorCopy( rag->bx0[i], b->x );
		VectorCopy4( rag->bq0[i], b->q );
	}
}

// the joints, with the limits widened to the pose of the cut where it is past them
static void Rag_MakeJoints( ragBody_t *rag, const vec3_t forward, const vec3_t right, const vec3_t up ) {
	vec3_t	chestUp, chestRight, chestForward;
	int		i;

	VectorSubtract( rag->p0[RB_CERVICAL], rag->p0[RB_THORACIC], chestUp );
	VectorNormalize( chestUp );
	VectorSubtract( rag->p0[RB_RHUMERUS], rag->p0[RB_LHUMERUS], chestRight );
	VectorNormalize( chestRight );
	CrossProduct( chestUp, chestRight, chestForward );
	VectorNormalize( chestForward );

	for ( i = 0; i < RAG_JOINTS; i++ ) {
		ragJoint_t	*j = &rag->joint[i];
		const int	c = ragJointDefs[i].child, p = ragJointDefs[i].parent;
		vec3_t		at, childLine, neutral, ref, childRef;
		vec4_t		cc;

		j->used = qtrue;
		j->child = c;
		j->parent = p;
		j->kind = ragJointDefs[i].kind;
		VectorCopy( rag->p0[ragJointDefs[i].at], at );
		Rag_ToBody( rag, c, at, j->rc );
		Rag_ToBody( rag, p, at, j->rp );
		Q_Conj( rag->bq0[p], cc );
		Q_Mul( cc, rag->bq0[c], j->rest );

		// the line of the child away from the joint
		switch ( c ) {
		case BD_BELLY:	VectorSubtract( rag->p0[RB_THORACIC], rag->p0[RB_LLUMBAR], childLine ); break;
		case BD_CHEST:	VectorCopy( chestUp, childLine ); break;
		case BD_HEAD:	VectorSubtract( rag->p0[RP_HEADTOP], rag->p0[RB_CERVICAL], childLine ); break;
		default:		VectorSubtract( rag->p0[ragBodyDefs[c].to], rag->p0[ragBodyDefs[c].from], childLine ); break;
		}
		VectorNormalize( childLine );

		if ( j->kind == RJ_CONE ) {
			float now;

			// where the child points when it is at rest
			switch ( c ) {
			case BD_BELLY:	VectorCopy( up, neutral ); break;
			case BD_CHEST:	VectorSubtract( rag->p0[RB_THORACIC], rag->p0[RB_LLUMBAR], neutral ); break;
			case BD_HEAD:	VectorCopy( chestUp, neutral ); break;
			case BD_LUARM:	VectorScale( chestRight, -0.7f, neutral ); VectorMA( neutral, -0.7f, chestUp, neutral ); break;
			case BD_RUARM:	VectorScale( chestRight, 0.7f, neutral ); VectorMA( neutral, -0.7f, chestUp, neutral ); break;
			default:		VectorScale( up, -1.0f, neutral ); VectorMA( neutral, 0.45f, forward, neutral ); break;	// thighs
			}
			VectorNormalize( neutral );
			Rag_DirToBody( rag, c, childLine, j->axisC );
			Rag_DirToBody( rag, p, neutral, j->axisP );
			// twist references: across the neutral line, and the same carried the shortest way onto the child's
			Rag_Across( neutral, ( c == BD_LUARM || c == BD_RUARM ) ? chestForward : forward, ref );
			V_TurnLike( neutral, childLine, ref, childRef );
			Rag_DirToBody( rag, p, ref, j->refP );
			Rag_DirToBody( rag, c, childRef, j->refC );
			now = acosf( Com_Clamp( -1.0f, 1.0f, DotProduct( neutral, childLine ) ) );
			j->swing = Q_max( DEG2RAD( ragJointDefs[i].swing ), now + DEG2RAD( 5.0f ) );
			j->twist = DEG2RAD( ragJointDefs[i].twist );
		} else {
			// a hinge: the axis is the one it bends about now if it is bent, else across the parent and the body's side;
			// it bends from 0 (straight) the way it is bent, or forward (elbow) or backward (knee) when straight
			vec3_t	parentLine, axis, bendDir;
			float	bend, s;

			VectorSubtract( rag->p0[ragBodyDefs[p].to], rag->p0[ragBodyDefs[p].from], parentLine );
			VectorNormalize( parentLine );
			CrossProduct( parentLine, childLine, axis );
			s = VectorNormalize( axis );
			if ( s < sinf( DEG2RAD( 20.0f ) ) ) {
				// bending turns the forearm forward, the shin backward
				if ( c == BD_LFARM || c == BD_RFARM ) {
					VectorCopy( chestForward, bendDir );
				} else {
					VectorScale( forward, -1.0f, bendDir );
				}
				CrossProduct( parentLine, bendDir, axis );
				if ( VectorNormalize( axis ) < 0.01f ) {
					Rag_Across( parentLine, right, axis );
				}
			}
			Rag_DirToBody( rag, p, axis, j->axisP );
			Rag_DirToBody( rag, c, axis, j->axisC );
			Rag_DirToBody( rag, p, parentLine, j->refP );
			Rag_DirToBody( rag, c, childLine, j->refC );
			bend = atan2f( s, DotProduct( parentLine, childLine ) );
			j->lo = 0.0f;
			j->hi = Q_max( DEG2RAD( ragJointDefs[i].bend ), bend + DEG2RAD( 5.0f ) );
		}
	}
}

// Which bodies keep apart: not those joined, nor those already touching in the pose of the cut (an upper arm against
// the belly would push it away for ever)
static void Rag_MakeApart( ragBody_t *rag ) {
	int a, b, sa, sb, j;

	for ( a = 0; a < BD_BODIES; a++ ) {
		for ( b = 0; b < BD_BODIES; b++ ) {
			const ragRigid_t	*ba = &rag->body[a], *bb = &rag->body[b];
			qboolean			apart = (qboolean)( a != b );

			for ( j = 0; j < RAG_JOINTS && apart; j++ ) {
				if ( ( ragJointDefs[j].child == a && ragJointDefs[j].parent == b )
					|| ( ragJointDefs[j].child == b && ragJointDefs[j].parent == a ) ) {
					apart = qfalse;
				}
			}
			for ( sa = 0; sa < ba->numSpheres && apart; sa++ ) {
				for ( sb = 0; sb < bb->numSpheres && apart; sb++ ) {
					vec3_t pa, pb;

					Q_Rotate( ba->q, ba->sphere[sa], pa );
					VectorAdd( pa, ba->x, pa );
					Q_Rotate( bb->q, bb->sphere[sb], pb );
					VectorAdd( pb, bb->x, pb );
					if ( Distance( pa, pb ) < ba->radius[sa] + bb->radius[sb] + 1.0f ) {
						apart = qfalse;
					}
				}
			}
			rag->apart[a][b] = apart;
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
	ragBody_t	*rag;
	vec3_t		before[RB_BONES], forward, up, right, d;
	float		rotNow[RB_BONES][3][3], rotBefore[RB_BONES][3][3];
	int			side[BD_BODIES], count[RAG_PARTS], i, j, k, handle = -1, oldest = 0;

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
		const int	bolt = trap->G2API_AddBolt( ghoul2, 0, ragBoneNames[i] );
		mdxaBone_t	m;

		rag->boneNum[i] = trap->ext.G2_BoneNumber( ghoul2, 0, ragBoneNames[i], NULL );
		if ( bolt < 0 || rag->boneNum[i] < 0 ) {
			return -1;
		}
		for ( k = 0; k < 2; k++ ) {
			float ( *rot )[3] = k ? rotNow[i] : rotBefore[i];

			trap->G2API_GetBoltMatrix( ghoul2, 0, bolt, &m, angles, origin, k ? cg.time : cg.time - 25, cgs.gameModels,
				(float *)scale );
			BG_GiveMeVectorFromMatrix( &m, ORIGIN, k ? rag->p0[i] : before[i] );
			for ( j = 0; j < 3; j++ ) {
				vec3_t axis;

				axis[0] = m.matrix[0][j];
				axis[1] = m.matrix[1][j];
				axis[2] = m.matrix[2][j];
				VectorNormalize( axis );
				rot[0][j] = axis[0];
				rot[1][j] = axis[1];
				rot[2][j] = axis[2];
			}
		}
	}

	// the frame of the body and the points that are no bone
	VectorSubtract( rag->p0[RB_LLUMBAR], rag->p0[RB_PELVIS], up );
	VectorNormalize( up );
	VectorSubtract( rag->p0[RB_RFEMUR], rag->p0[RB_LFEMUR], right );
	VectorNormalize( right );
	CrossProduct( up, right, forward );
	VectorNormalize( forward );
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

	Rag_MakeBodies( rag, forward, right );
	Rag_MakeJoints( rag, forward, right, up );
	Rag_MakeApart( rag );

	// every bone in the frame of its body
	for ( i = 0; i < BD_BODIES; i++ ) {
		for ( j = 0; j < 2; j++ ) {
			const int bone = ragBodyDefs[i].bones[j];
			float bq[3][3];

			if ( bone < 0 ) {
				continue;
			}
			rag->boneBody[bone] = i;
			Q_ToMatrix( rag->bq0[i], bq );
			M_TransposeMul( bq, rotNow[bone], rag->boneRot[bone] );
			Rag_ToBody( rag, i, rag->p0[bone], rag->bonePos[bone] );
		}
	}

	// the speed and spin of every body from its first bone now and a moment ago
	for ( i = 0; i < BD_BODIES; i++ ) {
		ragRigid_t	*b = &rag->body[i];
		const int	bone = ragBodyDefs[i].bones[0];
		vec4_t		qa, qb;
		vec3_t		spin;
		float		speed;

		VectorSubtract( rag->p0[bone], before[bone], b->v );
		VectorScale( b->v, 1000.0f / 25.0f, b->v );
		VectorAdd( b->v, velocity, b->v );
		Q_FromMatrix( rotBefore[bone], qa );
		Q_FromMatrix( rotNow[bone], qb );
		Q_Delta( qa, qb, spin );
		VectorScale( spin, 1000.0f / 25.0f, b->w );
		speed = VectorLength( b->w );
		if ( speed > RAG_MAX_SPIN ) {
			VectorScale( b->w, RAG_MAX_SPIN / speed, b->w );
		}
	}

	// the side of every body by its middle; a side without one takes the body reaching furthest into it
	{
		qboolean any[2] = { qfalse, qfalse };

		for ( i = 0; i < BD_BODIES; i++ ) {
			side[i] = ( DotProduct( rag->bx0[i], plane ) - plane[3] >= 0.0f ) ? 1 : 0;
			any[side[i]] = qtrue;
		}
		for ( j = 0; j < 2; j++ ) {
			float	best = 0.0f;
			int		pick = -1;

			if ( any[j] ) {
				continue;
			}
			for ( i = 0; i < BD_BODIES; i++ ) {
				ragRigid_t *b = &rag->body[i];

				for ( k = 0; k < b->numSpheres; k++ ) {
					vec3_t	at;
					float	dist;

					Q_Rotate( b->q, b->sphere[k], at );
					VectorAdd( at, b->x, at );
					dist = ( DotProduct( at, plane ) - plane[3] ) * ( j ? 1.0f : -1.0f ) + b->radius[k];
					if ( dist > best ) {
						best = dist;
						pick = i;
					}
				}
			}
			if ( pick < 0 ) {
				return -1;		// (the plane misses the body)
			}
			side[pick] = j;
		}
	}

	// the groups still joined on each side are the parts; the joints across the cut go
	for ( i = 0; i < BD_BODIES; i++ ) {
		rag->body[i].part = -1;
	}
	for ( i = 0; i < RAG_JOINTS; i++ ) {
		if ( side[rag->joint[i].child] != side[rag->joint[i].parent] ) {
			rag->joint[i].used = qfalse;
		}
	}
	memset( count, 0, sizeof( count ) );
	for ( i = 0; i < BD_BODIES; i++ ) {
		int stack[BD_BODIES], top = 0, p;

		if ( rag->body[i].part >= 0 ) {
			continue;
		}
		if ( rag->numParts < RAG_PARTS ) {
			p = rag->numParts++;
		} else {
			// more groups than parts (rare): to the largest part of the same side, joined to it no more than before
			p = 0;
			for ( j = 0; j < RAG_PARTS; j++ ) {
				if ( rag->part[j].side == side[i] && count[j] > count[p] ) {
					p = j;
				}
			}
		}
		rag->part[p].side = side[i];
		rag->body[i].part = p;
		stack[top++] = i;
		while ( top ) {
			const int b = stack[--top];

			count[p]++;
			for ( j = 0; j < RAG_JOINTS; j++ ) {
				const ragJoint_t *jt = &rag->joint[j];
				int other = -1;

				if ( !jt->used ) {
					continue;
				}
				if ( jt->child == b ) {
					other = jt->parent;
				} else if ( jt->parent == b ) {
					other = jt->child;
				}
				if ( other >= 0 && rag->body[other].part < 0 ) {
					rag->body[other].part = p;
					stack[top++] = other;
				}
			}
		}
	}

	for ( i = 0; i < rag->numParts; i++ ) {
		ragPart_t	*part = &rag->part[i];
		float		best = 1e9f;
		vec3_t		tip;
		static const struct { int model, body; } extra[] = { { 1, BD_RFARM }, { 2, BD_LFARM }, { 3, BD_CHEST } };

		part->used = qtrue;
		// the part away from the feet thrown along the swing, every part tipped a little to a random side (a leg
		// cut off a standing body stood on its foot)
		{
			const float yaw = Q_flrand( 0.0f, 2.0f * M_PI );

			VectorSet( tip, cosf( yaw ), sinf( yaw ), 0.0f );
		}
		for ( j = 0; j < BD_BODIES; j++ ) {
			ragRigid_t *b = &rag->body[j];
			float speed;

			if ( b->part != i ) {
				continue;
			}
			if ( !part->side && throwDir ) {
				VectorMA( b->v, 100.0f, throwDir, b->v );
				b->v[2] += 50.0f;
			}
			VectorMA( b->v, 20.0f, tip, b->v );
			speed = VectorLength( b->v );
			if ( speed > RAG_MAX_SPEED ) {
				VectorScale( b->v, RAG_MAX_SPEED / speed, b->v );
			}
			VectorCopy( b->x, b->sx );
			VectorCopy( b->x, b->dx );
			VectorCopy4( b->q, b->sq );
			VectorCopy4( b->q, b->dq );
			if ( Distance( b->x, centre ) < best ) {
				best = Distance( b->x, centre );
				part->anchor = j;
			}
		}

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
			if ( rag->body[extra[j].body].part != i && trap->G2API_HasGhoul2ModelOnIndex( &part->ghoul2, extra[j].model ) ) {
				trap->G2API_RemoveGhoul2Model( &part->ghoul2, extra[j].model );
			}
		}
		VectorCopy( rag->body[part->anchor].x, part->origin );
	}
	rag->active = qtrue;
	rag->start = cg.time;
	return handle;
}

// ------------------------------------------------------------------------------------------------ simulation

typedef struct {
	vec3_t	origin, velocity;
} ragMover_t;

static ragMover_t	ragMovers[64];
static int			ragNumMovers;

static void Rag_Wake( ragPart_t *part ) {
	part->asleep = qfalse;
	part->posed = qfalse;
	part->still = 0;
}

static void Rag_SphereWorld( const ragRigid_t *b, int s, vec3_t out ) {
	Q_Rotate( b->q, b->sphere[s], out );
	VectorAdd( out, b->x, out );
}

// once a step: the planes of the level each sphere may touch on its way
static void Rag_FindContacts( ragRigid_t *b, float gravity ) {
	int s;

	for ( s = 0; s < b->numSpheres; s++ ) {
		trace_t	tr;
		vec3_t	from, to, way, mins, maxs, r;
		float	half = Q_max( b->radius[s] * 0.8f, 1.5f ), len;

		Rag_SphereWorld( b, s, from );
		// where it is heading this step, with a margin
		VectorSubtract( from, b->x, r );
		CrossProduct( b->w, r, way );
		VectorAdd( way, b->v, way );
		VectorScale( way, RAG_STEP, way );
		way[2] -= gravity * RAG_STEP * RAG_STEP + 0.5f;
		len = VectorLength( way );
		VectorMA( from, ( len + 1.0f ) / Q_max( len, 0.001f ), way, to );
		VectorSet( mins, -half, -half, -half );
		VectorSet( maxs, half, half, half );
		CG_Trace( &tr, from, mins, maxs, to, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.allsolid || tr.fraction >= 1.0f ) {
			if ( !tr.allsolid ) {
				b->numPlanes[s] = 0;
			}
			continue;
		}
		// the newest plane first, the one before kept when it differs (a corner)
		if ( b->numPlanes[s] && DotProduct( b->planeN[s][0], tr.plane.normal ) < 0.95f ) {
			VectorCopy( b->planeN[s][0], b->planeN[s][1] );
			b->planeD[s][1] = b->planeD[s][0];
			b->numPlanes[s] = 2;
		} else {
			b->numPlanes[s] = 1;
		}
		VectorCopy( tr.plane.normal, b->planeN[s][0] );
		// (the distance of the sphere's centre where it touches)
		b->planeD[s][0] = DotProduct( tr.endpos, tr.plane.normal ) + ( b->radius[s] - half );
	}
}

static void Rag_Contacts( ragBody_t *rag, ragRigid_t *b, float h ) {
	int s, k;

	for ( s = 0; s < b->numSpheres; s++ ) {
		b->touched[s] = qfalse;
		b->contactLambda[s] = 0.0f;
		for ( k = 0; k < b->numPlanes[s]; k++ ) {
			vec3_t	at, r, corr, prevAt, move, tangent;
			float	depth, lambda, slide;

			Rag_SphereWorld( b, s, at );
			depth = b->planeD[s][k] - DotProduct( at, b->planeN[s][k] );
			if ( depth <= 0.0f ) {
				continue;
			}
			VectorSubtract( at, b->x, r );
			VectorScale( b->planeN[s][k], depth, corr );
			lambda = Rag_Positional( b, r, NULL, vec3_origin, corr, 0.0f, h );

			// static friction: the point does not slide while the push into the surface is large enough
			Rag_SphereWorld( b, s, at );
			Q_Rotate( b->pq, b->sphere[s], prevAt );
			VectorAdd( prevAt, b->px, prevAt );
			VectorSubtract( at, prevAt, move );
			VectorMA( move, -DotProduct( move, b->planeN[s][k] ), b->planeN[s][k], tangent );
			slide = VectorLength( tangent );
			if ( slide > 0.0f && slide < RAG_MU_STATIC * depth ) {
				VectorSubtract( at, b->x, r );
				VectorScale( tangent, -1.0f, corr );
				Rag_Positional( b, r, NULL, vec3_origin, corr, 0.0f, h );
			}
			b->touched[s] = qtrue;
			b->contactLambda[s] += lambda;
			VectorCopy( b->planeN[s][k], b->contactNormal[s] );
			Rag_SphereWorld( b, s, at );
			VectorSubtract( at, b->x, b->contactR[s] );
		}
	}
	(void)rag;
}

// spheres of bodies that are not joined keep apart (the parts of one cut too)
static void Rag_BodyContacts( ragBody_t *rag, float h ) {
	int a, b, sa, sb;

	for ( a = 0; a < BD_BODIES; a++ ) {
		ragRigid_t *ba = &rag->body[a];

		for ( b = a + 1; b < BD_BODIES; b++ ) {
			ragRigid_t	*bb = &rag->body[b];

			if ( !rag->apart[a][b] || ( rag->part[ba->part].asleep && rag->part[bb->part].asleep ) ) {
				continue;
			}
			for ( sa = 0; sa < ba->numSpheres; sa++ ) {
				for ( sb = 0; sb < bb->numSpheres; sb++ ) {
					vec3_t	pa, pb, d, ra, rb, corr;
					float	dist, overlap;

					Rag_SphereWorld( ba, sa, pa );
					Rag_SphereWorld( bb, sb, pb );
					VectorSubtract( pa, pb, d );
					dist = VectorNormalize( d );
					overlap = ba->radius[sa] + bb->radius[sb] - dist;
					if ( overlap <= 0.0f || dist < 0.0001f ) {
						continue;
					}
					VectorSubtract( pa, ba->x, ra );
					VectorSubtract( pb, bb->x, rb );
					VectorScale( d, overlap, corr );
					Rag_Positional( ba, ra, bb, rb, corr, 0.0f, h );
					if ( rag->part[ba->part].asleep || rag->part[bb->part].asleep ) {
						Rag_Wake( &rag->part[ba->part] );
						Rag_Wake( &rag->part[bb->part] );
					}
				}
			}
		}
	}
}

// players and NPCs push spheres out sideways and hand over some of their speed
static void Rag_Movers( ragBody_t *rag, ragRigid_t *b, float h ) {
	int s, k;

	for ( k = 0; k < ragNumMovers; k++ ) {
		const ragMover_t *m = &ragMovers[k];

		for ( s = 0; s < b->numSpheres; s++ ) {
			vec3_t	at, d, r, corr;
			float	len, reach = 15.0f + b->radius[s];

			Rag_SphereWorld( b, s, at );
			if ( at[2] < m->origin[2] - 24.0f - b->radius[s] || at[2] > m->origin[2] + 40.0f + b->radius[s] ) {
				continue;
			}
			d[0] = at[0] - m->origin[0];
			d[1] = at[1] - m->origin[1];
			d[2] = 0.0f;
			len = VectorNormalize( d );
			if ( len >= reach ) {
				continue;
			}
			if ( len < 0.01f ) {
				VectorSet( d, 1.0f, 0.0f, 0.0f );
			}
			// pushed out a little each substep, about as fast as a player walks: all of it at once would be a speed of
			// thousands of units a second (the speed of position based dynamics comes from how far a body moved)
			VectorSubtract( at, b->x, r );
			VectorScale( d, Q_min( reach - len, 0.6f ), corr );
			// (a little upward too: a foot lifts a limb instead of pressing it into the floor)
			corr[2] += 0.05f * Q_min( reach - len, 0.6f );
			Rag_Positional( b, r, NULL, vec3_origin, corr, 0.0f, h );
		}
	}
	(void)rag;
}

static void Rag_Joints( ragBody_t *rag, float h, float hold ) {
	int i;

	for ( i = 0; i < RAG_JOINTS; i++ ) {
		ragJoint_t	*j = &rag->joint[i];
		ragRigid_t	*c, *p;
		vec3_t		rc, rp, pc, pp, corr;

		if ( !j->used || rag->part[rag->body[j->child].part].asleep ) {
			continue;
		}
		c = &rag->body[j->child];
		p = &rag->body[j->parent];

		// the pose of the cut, weaker and weaker
		if ( hold > 0.0f ) {
			vec4_t want;

			Q_Mul( p->q, j->rest, want );
			Q_Delta( c->q, want, corr );
			VectorScale( corr, hold, corr );
			Rag_Angular( c, p, corr, 0.0f, h );
		}

		if ( j->kind == RJ_CONE ) {
			vec3_t	a, n, m, u, v, axis;
			float	angle, twist;

			Q_Rotate( c->q, j->axisC, a );
			Q_Rotate( p->q, j->axisP, n );
			angle = acosf( Com_Clamp( -1.0f, 1.0f, DotProduct( a, n ) ) );
			if ( angle > j->swing ) {
				CrossProduct( a, n, axis );
				if ( VectorNormalize( axis ) > 0.0001f ) {
					VectorScale( axis, angle - j->swing, corr );
					Rag_Angular( c, p, corr, 0.0f, h );
				}
			}
			// the twist about the middle of both lines
			Q_Rotate( c->q, j->axisC, a );
			Q_Rotate( p->q, j->axisP, n );
			VectorAdd( a, n, m );
			if ( VectorNormalize( m ) > 0.001f ) {
				Q_Rotate( p->q, j->refP, u );
				Q_Rotate( c->q, j->refC, v );
				VectorMA( u, -DotProduct( u, m ), m, u );
				VectorMA( v, -DotProduct( v, m ), m, v );
				if ( VectorNormalize( u ) > 0.001f && VectorNormalize( v ) > 0.001f ) {
					vec3_t cr;

					CrossProduct( u, v, cr );
					twist = atan2f( DotProduct( cr, m ), DotProduct( u, v ) );
					if ( fabsf( twist ) > j->twist ) {
						VectorScale( m, ( twist > 0.0f ? j->twist : -j->twist ) - twist, corr );
						Rag_Angular( c, p, corr, 0.0f, h );
					}
				}
			}
		} else {
			vec3_t	hc, hp, lc, lp, cr;
			float	bend, want;

			// the axes in line
			Q_Rotate( c->q, j->axisC, hc );
			Q_Rotate( p->q, j->axisP, hp );
			CrossProduct( hc, hp, corr );
			{
				const float s = VectorLength( corr );

				if ( s > 0.00001f ) {
					VectorScale( corr, asinf( Com_Clamp( 0.0f, 1.0f, s ) ) / s, corr );
					Rag_Angular( c, p, corr, 0.0f, h );
				}
			}
			// the bend within its range
			Q_Rotate( p->q, j->axisP, hp );
			Q_Rotate( p->q, j->refP, lp );
			Q_Rotate( c->q, j->refC, lc );
			CrossProduct( lp, lc, cr );
			bend = atan2f( DotProduct( cr, hp ), DotProduct( lp, lc ) );
			want = Com_Clamp( j->lo, j->hi, bend );
			if ( want != bend ) {
				VectorScale( hp, want - bend, corr );
				Rag_Angular( c, p, corr, 0.0f, h );
			}
		}

		// the points of the joint together
		Q_Rotate( c->q, j->rc, rc );
		Q_Rotate( p->q, j->rp, rp );
		VectorAdd( c->x, rc, pc );
		VectorAdd( p->x, rp, pp );
		VectorSubtract( pp, pc, corr );
		Rag_Positional( c, rc, p, rp, corr, 0.0f, h );
	}
}

// sliding friction and no bounce on the speeds, after a substep
static void Rag_ContactSpeeds( ragRigid_t *b, float h ) {
	qboolean	any = qfalse;
	int			s;

	for ( s = 0; s < b->numSpheres; s++ ) {
		vec3_t	v, vt, dv, p, t, it;
		float	vn, vtl, friction, w;
		const float *n = b->contactNormal[s];

		if ( !b->touched[s] ) {
			continue;
		}
		any = qtrue;
		CrossProduct( b->w, b->contactR[s], v );
		VectorAdd( v, b->v, v );
		vn = DotProduct( v, n );
		VectorMA( v, -vn, n, vt );
		vtl = VectorLength( vt );
		VectorClear( dv );
		if ( vtl > 0.0001f ) {
			// the push into the surface over the substep is the normal force
			friction = Q_min( RAG_MU_SLIDE * b->contactLambda[s] / h, vtl );
			VectorMA( dv, -friction / vtl, vt, dv );
		}
		if ( vn > 0.0f ) {
			VectorMA( dv, -vn * 0.8f, n, dv );		// (leaving the surface fast: damped, no bounce)
		}
		{
			vec3_t dir;
			float len;

			VectorCopy( dv, dir );
			len = VectorNormalize( dir );
			if ( len < 0.00001f ) {
				continue;
			}
			w = Rag_GenInvMass( b, b->contactR[s], dir );
			if ( w <= 0.0f ) {
				continue;
			}
			VectorScale( dir, len / w, p );
		}
		VectorMA( b->v, b->invMass, p, b->v );
		CrossProduct( b->contactR[s], p, t );
		Rag_InvInertiaWorld( b, t, it );
		VectorAdd( b->w, it, b->w );
	}
	// rolling resistance: a limb on the floor does not roll on for ever
	if ( any ) {
		VectorScale( b->w, 1.0f / ( 1.0f + 6.0f * h ), b->w );
	}
}

static void Rag_Step( ragBody_t *rag, float gravity, float hold ) {
	const float	h = RAG_STEP / RAG_SUBSTEPS;
	int			i, s;
	qboolean	awake[RAG_PARTS];

	for ( i = 0; i < rag->numParts; i++ ) {
		awake[i] = (qboolean)!rag->part[i].asleep;
	}
	for ( i = 0; i < BD_BODIES; i++ ) {
		ragRigid_t *b = &rag->body[i];

		VectorCopy( b->x, b->sx );
		VectorCopy4( b->q, b->sq );
		if ( awake[b->part] ) {
			Rag_FindContacts( b, gravity );
		}
	}
	for ( s = 0; s < RAG_SUBSTEPS; s++ ) {
		for ( i = 0; i < BD_BODIES; i++ ) {
			ragRigid_t *b = &rag->body[i];

			if ( !awake[b->part] ) {
				continue;
			}
			VectorCopy( b->x, b->px );
			VectorCopy4( b->q, b->pq );
			b->v[2] -= gravity * h;
			VectorMA( b->x, h, b->v, b->x );
			Q_AddRotation( b->q, b->w, h );
		}
		Rag_Joints( rag, h, hold );
		Rag_BodyContacts( rag, h );
		for ( i = 0; i < BD_BODIES; i++ ) {
			ragRigid_t *b = &rag->body[i];

			if ( !awake[b->part] ) {
				continue;
			}
			Rag_Movers( rag, b, h );
			Rag_Contacts( rag, b, h );
		}
		for ( i = 0; i < BD_BODIES; i++ ) {
			ragRigid_t	*b = &rag->body[i];
			float		speed;

			if ( !awake[b->part] ) {
				continue;
			}
			VectorSubtract( b->x, b->px, b->v );
			VectorScale( b->v, 1.0f / h, b->v );
			Q_Delta( b->pq, b->q, b->w );
			VectorScale( b->w, 1.0f / h, b->w );
			Rag_ContactSpeeds( b, h );
			// a little damping, more on the turns
			VectorScale( b->v, 1.0f / ( 1.0f + 0.05f * h ), b->v );
			VectorScale( b->w, 1.0f / ( 1.0f + 1.5f * h ), b->w );
			speed = VectorLength( b->v );
			if ( speed > RAG_MAX_SPEED ) {
				VectorScale( b->v, RAG_MAX_SPEED / speed, b->v );
			}
			speed = VectorLength( b->w );
			if ( speed > RAG_MAX_SPIN * 2.0f ) {
				VectorScale( b->w, RAG_MAX_SPIN * 2.0f / speed, b->w );
			}
		}
	}

	// a part that has stayed where it is for a while sleeps (by how far it moved, not by its speed: two limbs on the
	// floor can tremble in place without going anywhere)
	for ( i = 0; i < rag->numParts; i++ ) {
		ragPart_t	*part = &rag->part[i];
		qboolean	moved = qfalse;
		int			k;

		if ( part->asleep ) {
			continue;
		}
		for ( k = 0; k < BD_BODIES; k++ ) {
			const ragRigid_t	*b = &rag->body[k];
			vec3_t				turn;

			if ( b->part != i ) {
				continue;
			}
			Q_Delta( part->restQ[k], b->q, turn );
			if ( Distance( part->restX[k], b->x ) > 0.75f || VectorLength( turn ) > DEG2RAD( 4.0f ) ) {
				moved = qtrue;
			}
		}
		if ( moved || part->still == 0 || cg.time - rag->start < RAG_HOLD_MSEC ) {
			for ( k = 0; k < BD_BODIES; k++ ) {
				VectorCopy( rag->body[k].x, part->restX[k] );
				VectorCopy4( rag->body[k].q, part->restQ[k] );
			}
			part->still = moved ? 0 : 1;
			continue;
		}
		if ( ++part->still > RAG_SLEEP_STEPS ) {
			part->asleep = qtrue;
			for ( k = 0; k < BD_BODIES; k++ ) {
				if ( rag->body[k].part == i ) {
					VectorClear( rag->body[k].v );
					VectorClear( rag->body[k].w );
				}
			}
		}
	}
}

// whether a player or an NPC stands in a sleeping part
static qboolean Rag_Touched( const ragBody_t *rag, int partNum ) {
	int i, s, k;

	for ( k = 0; k < ragNumMovers; k++ ) {
		const ragMover_t *m = &ragMovers[k];

		for ( i = 0; i < BD_BODIES; i++ ) {
			const ragRigid_t *b = &rag->body[i];

			if ( b->part != partNum ) {
				continue;
			}
			for ( s = 0; s < b->numSpheres; s++ ) {
				vec3_t		at;
				const float	reach = 15.0f + b->radius[s];

				Rag_SphereWorld( b, s, at );
				if ( at[2] > m->origin[2] - 24.0f - b->radius[s] && at[2] < m->origin[2] + 40.0f + b->radius[s]
					&& Square( at[0] - m->origin[0] ) + Square( at[1] - m->origin[1] ) < reach * reach ) {
					return qtrue;
				}
			}
		}
	}
	return qfalse;
}

// a push of speed along dir at the centre of every body of a part near a point (force powers, explosions)
static void Rag_Kick( ragBody_t *rag, int body, const vec3_t dir, float speed ) {
	ragRigid_t	*b = &rag->body[body];
	vec3_t		spin;

	VectorMA( b->v, speed, dir, b->v );
	// (a turn too, as if it was hit off its middle)
	VectorSet( spin, Q_flrand( -1.0f, 1.0f ), Q_flrand( -1.0f, 1.0f ), Q_flrand( -1.0f, 1.0f ) );
	VectorMA( b->w, speed * 0.004f, spin, b->w );
	Rag_Wake( &rag->part[b->part] );
}

// the players and NPCs that push parts this frame, and force push and pull
static void Rag_GatherMovers( void ) {
	static int	pushing[MAX_CLIENTS];
	int			i, b, k;

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

	// force push and pull: when the hand effect of a player begins (PW_DISINT_4, PW_PULL for pull), the bodies in front
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
			for ( k = 0; k < BD_BODIES; k++ ) {
				vec3_t	d;
				float	len, speed;

				VectorSubtract( rags[b].body[k].x, from, d );
				len = VectorNormalize( d );
				if ( len > 512.0f || DotProduct( d, forward ) < 0.5f ) {
					continue;
				}
				speed = 150.0f + 450.0f * ( 1.0f - len / 512.0f );
				if ( pull ) {
					VectorScale( d, -1.0f, d );
					speed = Q_min( speed, len * 3.0f );
				}
				d[2] += 0.3f;
				VectorNormalize( d );
				Rag_Kick( &rags[b], k, d, speed );
			}
		}
	}
}

/*
=================
CG_RagExplosion

An explosion at origin throws the bodies within radius away (cg_weapons.c)
=================
*/
void CG_RagExplosion( const vec3_t origin, float radius ) {
	int b, k;

	for ( b = 0; b < RAG_MAX; b++ ) {
		if ( !rags[b].active ) {
			continue;
		}
		for ( k = 0; k < BD_BODIES; k++ ) {
			vec3_t	d;
			float	len;

			VectorSubtract( rags[b].body[k].x, origin, d );
			len = VectorNormalize( d );
			if ( len > radius ) {
				continue;
			}
			d[2] += 0.5f;
			VectorNormalize( d );
			Rag_Kick( &rags[b], k, d, 80.0f + 420.0f * ( 1.0f - len / radius ) );
		}
	}
}

// ------------------------------------------------------------------------------------------------ drawing

static void Rag_BoneFrame( const ragBody_t *rag, int bone, const vec3_t x, const vec4_t q, qboolean hidden, float *out ) {
	float	bq[3][3], axes[3][3];
	vec3_t	at;
	int		i;

	Q_ToMatrix( q, bq );
	M_Mul( bq, rag->boneRot[bone], axes );
	Q_Rotate( q, rag->bonePos[bone], at );
	VectorAdd( at, x, at );
	for ( i = 0; i < 3; i++ ) {
		out[i * 4 + 0] = hidden ? 0.0f : axes[i][0];
		out[i * 4 + 1] = hidden ? 0.0f : axes[i][1];
		out[i * 4 + 2] = hidden ? 0.0f : axes[i][2];
		out[i * 4 + 3] = at[i];
	}
}

// the bones of a part placed on its copy of the model
static void Rag_Pose( ragBody_t *rag, ragPart_t *part, int partNum ) {
	float		world[RB_BONES][12];
	const int	a = part->anchor;
	vec4_t		turn, c;
	int			i;

	int			hidden[RB_BONES];

	// how the body at the cut turned since the cut
	Q_Conj( rag->bq0[a], c );
	Q_Mul( rag->body[a].dq, c, turn );
	VectorCopy( rag->body[a].dx, part->origin );
	for ( i = 0; i < RB_BONES; i++ ) {
		const int			bd = rag->boneBody[i];
		const ragRigid_t	*b = &rag->body[bd];

		hidden[i] = 0;
		if ( b->part == partNum ) {
			Rag_BoneFrame( rag, i, b->dx, b->dq, qfalse, world[i] );
		} else {
			// as it was at the cut, carried along with the own body it was joined to (else the body at the cut), so
			// skin hanging on both does not stretch; the renderer leaves it out (the cut of the first pose, and the
			// bones of another part on this side, hidden)
			int		g = a, j;
			vec3_t	off, x;
			vec4_t	q, turnG, cg;

			for ( j = 0; j < RAG_JOINTS; j++ ) {
				const int other = ragJointDefs[j].child == bd ? ragJointDefs[j].parent : ragJointDefs[j].parent == bd ? ragJointDefs[j].child : -1;

				if ( other >= 0 && rag->body[other].part == partNum ) {
					g = other;
					break;
				}
			}
			Q_Conj( rag->bq0[g], cg );
			Q_Mul( rag->body[g].dq, cg, turnG );
			VectorSubtract( rag->bx0[bd], rag->bx0[g], off );
			Q_Rotate( turnG, off, off );
			VectorAdd( rag->body[g].dx, off, x );
			Q_Mul( turnG, rag->bq0[bd], q );
			Rag_BoneFrame( rag, i, x, q, qfalse, world[i] );
			hidden[i] = ( rag->part[b->part].side == part->side ) ? 1 : 0;
		}
	}
	if ( !part->cutSent ) {
		// the first pose: the cut where it is now, kept by the renderer for every vertex from here on
		vec4_t	plane;
		void	*g2;
		vec3_t	o;

		part->cutSent = qtrue;
		CG_RagPart( (int)( rag - rags ), partNum, &g2, o, plane );
		trap->ext.G2_SetBonesWorld( part->ghoul2, 0, vec3_origin, part->origin, rag->scale, RB_BONES, rag->boneNum, &world[0][0],
			plane, hidden );
	} else {
		trap->ext.G2_SetBonesWorld( part->ghoul2, 0, vec3_origin, part->origin, rag->scale, RB_BONES, rag->boneNum, &world[0][0],
			NULL, NULL );
	}

	// (developer 1: the bone at the cut must come back where it was put)
	if ( !part->checked ) {
		char value[8];

		part->checked = qtrue;
		trap->Cvar_VariableStringBuffer( "developer", value, sizeof( value ) );
		if ( atoi( value ) ) {
			const int	bone = ragBodyDefs[a].bones[0];
			const int	bolt = trap->G2API_AddBolt( part->ghoul2, 0, ragBoneNames[bone] );
			mdxaBone_t	m;
			vec3_t		got;

			if ( bolt >= 0 && trap->G2API_GetBoltMatrix( part->ghoul2, 0, bolt, &m, vec3_origin, part->origin, cg.time,
				cgs.gameModels, rag->scale ) ) {
				vec3_t want;

				BG_GiveMeVectorFromMatrix( &m, ORIGIN, got );
				VectorSet( want, world[bone][3], world[bone][7], world[bone][11] );
				Com_Printf( "v_slice: part %i (%s, %i parts) bone round trip off by %.2f units\n", partNum, ragBoneNames[bone],
					rag->numParts, Distance( got, want ) );
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
	const float	gravity = cg.predictedPlayerState.gravity > 0 ? (float)cg.predictedPlayerState.gravity : 800.0f;
	qboolean	any = qfalse;
	int			r, p, s, i;

	for ( r = 0; r < RAG_MAX; r++ ) {
		any |= rags[r].active;
	}
	if ( !any ) {
		return;
	}
	Rag_GatherMovers();
	for ( r = 0; r < RAG_MAX; r++ ) {
		ragBody_t	*rag = &rags[r];
		int			steps;
		float		alpha, hold;
		qboolean	awake = qfalse;

		if ( !rag->active ) {
			continue;
		}
		for ( p = 0; p < rag->numParts; p++ ) {
			if ( rag->part[p].asleep && Rag_Touched( rag, p ) ) {
				Rag_Wake( &rag->part[p] );
			}
			awake |= !rag->part[p].asleep;
		}
		rag->acc += Com_Clamp( 0.0f, 0.1f, cg.frametime * 0.001f );
		steps = (int)( rag->acc / RAG_STEP );
		rag->acc -= steps * RAG_STEP;
		if ( steps > RAG_MAX_STEPS ) {
			steps = RAG_MAX_STEPS;
		}
		hold = 0.3f * ( 1.0f - Com_Clamp( 0.0f, 1.0f, (float)( cg.time - rag->start ) / RAG_HOLD_MSEC ) );
		for ( s = 0; s < steps && awake; s++ ) {
			Rag_Step( rag, gravity, hold );
		}

		// drawn between the last two steps
		alpha = Com_Clamp( 0.0f, 1.0f, rag->acc / RAG_STEP );
		for ( i = 0; i < BD_BODIES; i++ ) {
			ragRigid_t *b = &rag->body[i];

			if ( rag->part[b->part].asleep ) {
				VectorCopy( b->x, b->dx );
				VectorCopy4( b->q, b->dq );
			} else {
				VectorAdvance( b->sx, alpha, b->x, b->dx );
				b->dq[0] = b->sq[0] + alpha * ( b->q[0] - b->sq[0] );
				b->dq[1] = b->sq[1] + alpha * ( b->q[1] - b->sq[1] );
				b->dq[2] = b->sq[2] + alpha * ( b->q[2] - b->sq[2] );
				b->dq[3] = b->sq[3] + alpha * ( b->q[3] - b->sq[3] );
				if ( b->sq[0] * b->q[0] + b->sq[1] * b->q[1] + b->sq[2] * b->q[2] + b->sq[3] * b->q[3] < 0.0f ) {
					VectorCopy4( b->q, b->dq );
				}
				Q_Normalize( b->dq );
			}
		}
		for ( p = 0; p < rag->numParts; p++ ) {
			ragPart_t *part = &rag->part[p];

			if ( part->asleep && part->posed ) {
				continue;
			}
			part->posed = part->asleep;
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
	vec4_t			turn, c;

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
	// the cut turns and moves with the body of the part at the cut
	Q_Conj( rag->bq0[part->anchor], c );
	Q_Mul( rag->body[part->anchor].dq, c, turn );
	Q_Rotate( turn, rag->plane, normal );
	VectorSubtract( rag->centre, rag->bx0[part->anchor], centre );
	Q_Rotate( turn, centre, centre );
	VectorAdd( centre, rag->body[part->anchor].dx, centre );
	if ( !part->side ) {
		VectorScale( normal, -1.0f, normal );
	}
	VectorCopy( normal, plane );
	plane[3] = DotProduct( normal, centre );
	return qtrue;
}

// "v_ragDump" (developer): every body of every ragdoll, for tests
void CG_RagDump_f( void ) {
	int r, i;

	for ( r = 0; r < RAG_MAX; r++ ) {
		const ragBody_t *rag = &rags[r];

		if ( !rag->active ) {
			continue;
		}
		Com_Printf( "ragdoll %i: %i parts, %i ms old\n", r, rag->numParts, cg.time - rag->start );
		for ( i = 0; i < rag->numParts; i++ ) {
			Com_Printf( "  part %i side %i anchor %i %s\n", i, rag->part[i].side, rag->part[i].anchor,
				rag->part[i].asleep ? "asleep" : "awake" );
		}
		for ( i = 0; i < BD_BODIES; i++ ) {
			const ragRigid_t *b = &rag->body[i];

			Com_Printf( "  body %2i part %i at %7.1f %7.1f %6.1f speed %6.1f spin %5.2f\n", i, b->part, b->x[0], b->x[1], b->x[2],
				VectorLength( b->v ), VectorLength( b->w ) );
		}
	}
}

/*
=================
CG_RagBleed

Where part n of a ragdoll bleeds (cg_slice.c): the middle of its cut and the speed of its body at the cut; and when
that body lies on a floor, where it touches it and how fast it slides along it. qtrue: it touches a floor.
=================
*/
qboolean CG_RagBleed( int handle, int n, vec3_t cutCentre, vec3_t velocity, vec3_t floorAt, float *slide ) {
	const ragBody_t		*rag;
	const ragPart_t		*part;
	const ragRigid_t	*b;
	vec4_t				turn, c;
	vec3_t				centre;
	int					s, best = -1;

	*slide = 0.0f;
	if ( handle < 0 || handle >= RAG_MAX || !rags[handle].active || n < 0 || n >= rags[handle].numParts ) {
		return qfalse;
	}
	rag = &rags[handle];
	part = &rag->part[n];
	b = &rag->body[part->anchor];
	Q_Conj( rag->bq0[part->anchor], c );
	Q_Mul( b->dq, c, turn );
	VectorSubtract( rag->centre, rag->bx0[part->anchor], centre );
	Q_Rotate( turn, centre, centre );
	VectorAdd( centre, b->dx, cutCentre );
	VectorCopy( b->v, velocity );
	if ( part->asleep ) {
		return qfalse;
	}
	// the lowest sphere of the body that lies on a floor
	{
		float lowest = 1e9f;

		for ( s = 0; s < b->numSpheres; s++ ) {
			if ( b->touched[s] && b->contactNormal[s][2] > 0.6f ) {
				vec3_t at;

				Rag_SphereWorld( b, s, at );
				VectorMA( at, -b->radius[s], b->contactNormal[s], at );
				if ( at[2] < lowest ) {
					lowest = at[2];
					VectorCopy( at, floorAt );
					best = s;
				}
			}
		}
	}
	if ( best < 0 ) {
		return qfalse;
	}
	*slide = sqrtf( b->v[0] * b->v[0] + b->v[1] * b->v[1] );
	return qtrue;
}

int CG_RagParts( int handle ) {
	if ( handle < 0 || handle >= RAG_MAX || !rags[handle].active ) {
		return 0;
	}
	return rags[handle].numParts;
}
