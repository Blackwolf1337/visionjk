/*
misc_ball, misc_cube and misc_prop: props with physics of their own. misc_prop is the general one, any model of
the game with the shape of that model, or as a box or a ball of its size. The other two are a ball and a crate
with numbers that suit them.

The ball falls, bounces, rolls down slopes and comes to rest. Everything else is a rigid body with corners,
edges and faces (the hull of its model, bg_props.c, or a box): it lands on them, tips over, tumbles and slides
to a stop on one of its faces. Players kick the ball by running into it and shove a body, which is heavy and
slows them down. Force push and pull throw both, shots, sabers and explosions knock them away. They collide
with each other, each by its mass, so bodies can be pushed with bodies and stacked. With +button13 a player
takes the one he looks at: it hangs from a point in front of his eyes by the spot he took it at, a body by a
corner, and swings there until he lets go. A prop that comes at a player fast hurts him. An admin takes another
player the same way and carries him in the hug of amhug, right in front of him, until he lets go.

For the client they are plain model entities (ET_GENERAL) whose place and angles the server renews every frame.
For the engine their contents are those of a corpse, which shots hit and nothing is stopped by: an entity is a
box that is not turned there. So what a player does to them is worked out here, and a body is put in the way
of the moves of players here too (G_PropClipMove), with its real shape, so they stand and walk on it. The cgame
of the port predicts that with the same code. A ball is in nobody's way.

Not from JA+ and not from JKAdmin.
*/

#include "g_local.h"
#include "b_local.h"				// G_BoundsOverlap
#include "bg_props.h"				// the shape of a body: the hull of its model
#include "bg_props_math.h"			// the vector functions, inline

// +button13. The base game uses the button bits up to 2048 and JA+ takes 4096 (+button12) for the hook. 8192,
// 16384 and 32768 (+button13 to +button15) are free, and all 16 bits of the buttons reach the server.
#define BUTTON_GRAB			8192
// +button14 and +button15: with a prop in the beam of the physgun, a press moves it away or closer
#define BUTTON_GUN_FAR		16384
#define BUTTON_GUN_NEAR		32768

#define PROP_HEALTH			100000		// they take damage to feel the hits, they never break
#define PROP_PLAYER_RADIUS	15.0f		// half the width of a player
#define PROP_MIN_BOUNCE		60.0f		// slower into a surface than this and it stays on it
#define PROP_LOUD			140.0f		// faster into something than this and it is heard
#define PROP_STUCK_FRAMES	40			// that long inside something solid and it goes home
#define PROP_SOUND_MSEC		150
#define PROP_WAKE_SPEED		60.0f		// a prop that lies still is a wall for the others until one hits it faster
#define PROP_WAKE_DEPTH		4.0f		//	than this or is pushed this deep into it

#define BALL_REST_SPEED		8.0f		// slower than this on flat ground and it lies still
#define BALL_FLAT			0.98f		// z of the normal of ground it can lie on

#define BODY_STEP			0.0125f		// longest step of the rigid body in seconds, a server frame is cut into these
#define BODY_MAX_STEPS		8
#define BODY_ROUNDS			6			// times the contacts are gone through in a step
#define BODY_MAX_SPOTS		64			// the spots of a body that are looked at: its corners, the middles of its faces
										//	and of its edges, as many as there is room for. A box has 26.
#define BODY_MAX_CONTACTS	128			// its spots against the world, the rest against other props
#define BODY_SKIN			1.0f		// this close to a surface a spot counts as touching it
#define BODY_SLOP			0.05f		// this deep in a surface is left alone
#define BODY_CORE			0.55f		// part of the way from its middle to its nearest face: a box in it nothing gets into
#define BODY_GROUND			0.7f		// z of the normal of a surface that carries it
#define BODY_MAX_SPEED		3000.0f
#define BODY_MAX_SPIN		30.0f		// radians per second
#define BODY_CALM_SPEED		14.0f		// slower than this and turning slower than BODY_CALM_SPIN for
#define BODY_CALM_SPIN		0.5f		//	BODY_CALM_MSEC on something that carries it: it lies still
#define BODY_CALM_MSEC		300
#define BODY_REST_MSEC		1000		// a body that touches something and has not left its place by more than
#define BODY_REST_MOVE		0.25f		//	this, nor turned further than BODY_REST_TURN (radians), for that long lies
#define BODY_REST_TURN		0.01f		//	still as well: it is wedged, with no three spots under it
#define BODY_REST_DEEPER	1.0f		// in something this much deeper than when it came to rest: it was pushed in

#define GRAB_REACH			96.0f		// from the eye to the spot he takes it at
#define GRAB_AIM			32.0f		// that far off his line of sight at most
#define GRAB_HOLD			48.0f		// the hand: that far in front of the eye
#define GRAB_STIFF			18.0f		// how fast the held spot goes to the hand, per second, for a mass of 1
#define GRAB_MAX_SPEED		900.0f
#define GRAB_BREAK			144.0f		// further from the hand than this and it is let go

#define GRAB_PLAYER_LIFT	4.0f		// a player who is carried: that far off the ground of the one who carries him,
#define GRAB_PLAYER_SPEED	600.0f		//	is never moved faster than this, so not thrown faster either,
#define GRAB_PLAYER_BREAK	128.0f		//	and is let go further from his place than this
#define GRAB_LIMP_MSEC		150			// he has no grip on the ground for that long, renewed every frame
#define GRAB_ANIM_MSEC		300			//	and the animations of the hug have at least that long to go

#define SHOVE_SLOW_MSEC		150			// a player who shoves a cube is slowed for that long

#define PROP_PLAYER_TOUCH	8.0f		// a body stops a player at his box, which reaches this much further than
										//	PROP_PLAYER_RADIUS at its corners: within that he touches it
#define PROP_BRUSHES		24			// bodies whose planes for the moves of players are kept
#define PROP_HURT_SPEED		320.0f		// a prop that comes at a player faster than this hurts him:
#define PROP_HURT_SCALE		0.03f		//	this much for every unit a second above it, times its mass,
#define PROP_HURT_MAX		150			//	no more than this,
#define PROP_HURT_MSEC		500			//	and not again within this time
#define PROP_THROW_MSEC		5000		// that long after a player had his hands on it what it hurts is his doing
#define PROP_THROW_SAFE		400			//	and that long it does not hurt him himself
#define PHYSGUN_RANGE		2048.0f		// the physgun (the stun baton after amphysgun): it takes a prop from this far,
#define PHYSGUN_STIFF		12.0f		//	the prop goes to its place in the beam this fast, per second, whatever its mass,
#define PHYSGUN_SPEED		1500.0f		//	and no faster than this,
#define PHYSGUN_PLAYER_SPEED	1000.0f	//	a player in it no faster than this
#define PHYSGUN_NEAR		48.0f		//	the point it holds the prop at is never closer than this,
#define PHYSGUN_STEP		0.1f		//	and a press of +button14 or +button15 moves it by this part of the distance,
#define PHYSGUN_MIN_STEP	16.0f		//	by this at least

#define PROP_KICK_REACH		60.0f		// a body this close in front of a player who flips off one is the one he kicked
#define PROP_KICK_SPEED		900.0f		// how hard that is: the speed a body of mass 1 gets

#define MODEL_MIN_EDGE		8.0f		// the longest edge of the box of a misc_prop: its model is scaled into these
#define MODEL_MAX_EDGE		384.0f

// gentity_t fields in use:
//	radius			ball: its size. cube: from its middle to a corner
//	movedir			velocity
//	pos1			where it was spawned, its home
//	physicsBounce	part of the speed it keeps in a bounce
//	random			ball: rolling resistance, units per second squared. cube: how well it grips the ground
//	mass			the heavier, the less a kick, a push or a hit moves it
//	speed			how hard players kick it
//	genericValue1	time of the last think			genericValue2	1: lies still
//	genericValue3	sound index of the bounce		genericValue4	time of the next bounce sound
//	genericValue5	time of the next look at the ground while it lies still
//	genericValue6	frames in a row inside something solid

// what does not fit into a gentity_t, by entity number. Of a player (the numbers below MAX_CLIENTS) only holder
// and hand are in use: who carries him, and where his place in front of that one was in the last frame.
typedef struct prop_s {
	qboolean	box;			// a body with corners and faces, else a ball
	matrix3_t	axis;			// how it is turned: forward, left, up
	vec3_t		spin;			// angular velocity in the world, radians per second
	propShape_t	shape;			// body: its corners and faces, the hull of its model or a box
	int			numSpots;		// body: the spots it is asked about, from its middle, in the model
	vec3_t		spots[BODY_MAX_SPOTS];
	float		inertia;		// moment of inertia of a mass of 1
	gentity_t	*holder;		// who has it in his hand
	vec3_t		grab;			// the spot he holds it at, in the prop
	vec3_t		hand;			// where his hand was in the last frame
	int			calmTime;		// since when it hardly moves, 0: it moves
	int			restTime;		// since when it is where restAt and restAxis say, 0: it is not watched
	vec3_t		restAt;
	vec3_t		restAxis[2];
	qboolean	leaves;			// it lay still there: when it leaves that place, what lies still next to it is woken
	int			restContacts;	// it lies still because it is wedged, with that many spots at something. 0: on three spots
	float		restSep;		// how deep it was in something when it came to rest, 0 or below. 0 while it moves
	int			solid;			// body: a player can stand on it, PROP_SOLID_ says how its shape is made of its model
	qboolean	gun;			// the holder has it in the beam of the physgun, not in his hand:
	float		gunDist;		//	that far along his line of sight is the point it was taken at,
	vec3_t		gunOffset;		//	from there to its middle,
	float		gunYaw;			//	the way its holder looked in the last frame,
	vec3_t		gunSpin;		//	and how his mouse turns it now
	qboolean	frozen;			// the physgun froze it where it was: nothing moves it until the physgun takes it again
	gentity_t	*thrower;		// the player who threw, shoved or shot it last
	int			throwTime;		//	and when
	int			hurtTime;		// it does not hurt anybody before
} prop_t;

// a body as the planes the move of a player is cut against, for where it stands now
typedef struct propSolid_s {
	int			number;			// of its entity, 0: free
	vec3_t		origin;
	matrix3_t	axis;
	propBrush_t	brush;
} propSolid_t;

// a spot of the cube at a surface of the world or of another prop
typedef struct contact_s {
	vec3_t		r;				// from the middle of the cube to the spot
	vec3_t		normal;			// of the surface, towards the cube
	float		sep;			// above 0: a gap, below: it is in that deep
	gentity_t	*other;			// the prop it touches there. NULL: the world, or a prop that counts as world
	vec3_t		r2;				// from the middle of that prop to the spot
	float		ratio;			// mass of the cube over the mass of that prop: its part of an impulse
	float		kn;				// what an impulse of 1 along the normal does to the speed of the spot
	float		want;			// speed along the normal the spot has to have at least
	float		jn;				// impulse along the normal so far
	vec3_t		jt;				// friction so far
} contact_t;

static prop_t	props[MAX_GENTITIES];
static qboolean	grabDown[MAX_CLIENTS];		// the grab button in the last frame
static gentity_t	*mates[MAX_CLIENTS];	// the other one of his hug, and after it until the two are out of each other
static int		shoveTime[MAX_CLIENTS];		// he shoves a cube until then
static float	shoveScale[MAX_CLIENTS];	//	and that much of his speed is left
static qboolean	gunOn[MAX_CLIENTS];			// he has the physgun (amphysgun)
static gentity_t	*gunProp[MAX_CLIENTS];	// the prop or the player in its beam
static gentity_t	*gunBeam[MAX_CLIENTS];	// the entity that tells the clients about the beam
static int		gunButtons[MAX_CLIENTS];	// his buttons in the last frame
static float	gunAngles[MAX_CLIENTS][2];	// pitch and yaw of his last command
static int		gunCmdButtons[MAX_CLIENTS];	// and its buttons
static float	gunTurn[MAX_CLIENTS][2];	// how far his mouse went since the last frame while it turns a prop
static short	propList[MAX_GENTITIES];	// the numbers of the props, made anew every frame
static int		propCount;
// The entities with a client, players and NPCs, made anew every frame. A prop looks for the ones that touch it in
// every frame, also while it lies still, and for the props next to it in every step. Both went over all entities
// of the map.
static short	moverList[MAX_GENTITIES];
static int		moverCount;
// Where the props are and how far they reach, by their place in propList. A body asks of every other prop whether
// it is near, in every step: with 300 props that is 5 million questions a second. Asked of the entities, each one
// is two pieces of memory far from all others, and that was most of what a pile cost. Here they lie side by side.
static float	propSeenAt[3][MAX_GENTITIES];
static float	propSeenRadius[MAX_GENTITIES];
static short	propPlace[MAX_GENTITIES];	// by entity number: its place in propList plus 1, 0: it is not in it
static propSolid_t	propSolids[PROP_BRUSHES];
static int		propSolidNext;

static void prop_pain( gentity_t *self, gentity_t *attacker, int damage );

static prop_t *Prop_State( const gentity_t *self ) {
	return &props[self - g_entities];
}

// the prop is somewhere else now: for the ones that ask where the others are
static void Prop_Seen( const gentity_t *self ) {
	const int place = propPlace[self - g_entities] - 1;

	if ( place < 0 || place >= propCount || propList[place] != self - g_entities )
		return;
	propSeenAt[0][place] = self->r.currentOrigin[0];
	propSeenAt[1][place] = self->r.currentOrigin[1];
	propSeenAt[2][place] = self->r.currentOrigin[2];
	propSeenRadius[place] = self->radius;
}

// how far a point is from the middle of the prop at a place of propList, squared
static float Prop_SeenDistanceSquared( const vec3_t at, int place ) {
	const float dx = propSeenAt[0][place] - at[0];
	const float dy = propSeenAt[1][place] - at[1];
	const float dz = propSeenAt[2][place] - at[2];

	return dx * dx + dy * dy + dz * dz;
}

static qboolean Prop_Is( const gentity_t *ent ) {
	return ( ent->inuse && ent->pain == prop_pain ) ? qtrue : qfalse;
}

static void Prop_Wake( gentity_t *self );

// what lies still next to the place a prop lay at is woken: it may have lain or leaned on it
static void Prop_WakeAround( gentity_t *self, const vec3_t at ) {
	int i;

	for ( i = 0; i < propCount; i++ ) {
		gentity_t	*other = &g_entities[propList[i]];
		const float	reach = self->radius + propSeenRadius[i] + 2.0f;

		if ( Prop_SeenDistanceSquared( at, i ) > reach * reach )
			continue;
		if ( other == self || !Prop_Is( other ) || !other->genericValue2 )
			continue;
		Prop_Wake( other );
	}
}

static void Prop_Wake( gentity_t *self ) {
	prop_t			*p = Prop_State( self );
	const qboolean	slept = self->genericValue2 ? qtrue : qfalse;

	// the physgun froze it: that holds until the physgun takes it again
	if ( p->frozen )
		return;
	self->genericValue2 = 0;
	p->calmTime = 0;
	p->restContacts = 0;
	p->restSep = 0.0f;
	if ( !slept ) {
		// something is at it: the time it has been at its place starts again
		if ( p->restTime )
			p->restTime = level.time;
		return;
	}

	// It lay still and may have carried others. A body wakes them once it leaves its place (Body_Left), not
	// because it woke: a pile of them woke as a whole for every body in it that did, and that never ended.
	if ( p->box ) {
		p->leaves = qtrue;
		p->restTime = level.time;
		VectorCopy( self->r.currentOrigin, p->restAt );
		VectorCopy( p->axis[0], p->restAxis[0] );
		VectorCopy( p->axis[1], p->restAxis[1] );
		return;
	}
	Prop_WakeAround( self, self->r.currentOrigin );
}

/*
=================
Prop_Fixed

Does another prop count as part of the world for a contact? It does while it lies still, and while it rests on
something and carries the one that asks (normal: from it to that one). That is what lets a pile come to rest.
A hit that is hard for its mass or a push deep into it wakes it, then it takes its part of the impulse.

into: how fast they come together, ratio: mass of the one that asks over its mass
=================
*/
static qboolean Prop_Fixed( gentity_t *other, const prop_t *q, const vec3_t normal, float into, float sep, float ratio ) {
	const qboolean carries = ( normal[2] > BODY_GROUND ) ? qtrue : qfalse;

	if ( q->frozen )
		return qtrue;
	if ( !other->genericValue2 && !( q->calmTime && carries ) )
		return qfalse;
	// what lies on it does not wake it with its weight, whatever that is
	if ( into * ( carries ? 1.0f : ratio ) > PROP_WAKE_SPEED
		|| ( sep < -PROP_WAKE_DEPTH && sep < q->restSep - BODY_REST_DEEPER ) ) {
		Prop_Wake( other );
		return qfalse;
	}
	return qtrue;
}

static void Prop_BounceSound( gentity_t *self ) {
	if ( !self->genericValue3 || level.time < self->genericValue4 )
		return;
	self->genericValue4 = level.time + PROP_SOUND_MSEC;
	G_Sound( self, CHAN_AUTO, self->genericValue3 );
}

// from the middle of a body to its nearest face
static float Prop_MinHalf( const prop_t *p ) {
	return p->shape.inner;
}

/*
===============================================================================

The rigid body: a turn, a spin, impulses at a spot

===============================================================================
*/

// a spot given in the prop as an offset from its middle in the world
static void Body_ToWorld( const prop_t *p, const vec3_t local, vec3_t out ) {
	VectorScale( p->axis[0], local[0], out );
	VectorMA( out, local[1], p->axis[1], out );
	VectorMA( out, local[2], p->axis[2], out );
}

// does it turn for real: a cube, or a ball in a hand. A free ball only shows a roll.
static qboolean Prop_Turns( const prop_t *p ) {
	return ( p->box || p->holder ) ? qtrue : qfalse;
}

// how fast the spot at the offset r from the middle moves
static void Body_SpeedAt( const gentity_t *self, const prop_t *p, const vec3_t r, vec3_t out ) {
	if ( !Prop_Turns( p ) ) {
		VectorCopy( self->movedir, out );
		return;
	}
	CrossProduct( p->spin, r, out );
	VectorAdd( out, self->movedir, out );
}

// an impulse, for a mass of 1, at the offset r from the middle: it moves the body and turns it
static void Body_Push( gentity_t *self, prop_t *p, const vec3_t r, const vec3_t impulse ) {
	vec3_t turn;

	VectorAdd( self->movedir, impulse, self->movedir );
	if ( !Prop_Turns( p ) )
		return;
	CrossProduct( r, impulse, turn );
	VectorMA( p->spin, 1.0f / p->inertia, turn, p->spin );
}

// what an impulse of 1 along dir at the offset r does to the speed of that spot along dir
static float Body_Give( const prop_t *p, const vec3_t r, const vec3_t dir ) {
	vec3_t cross;

	if ( !Prop_Turns( p ) )
		return 1.0f;
	CrossProduct( r, dir, cross );
	return 1.0f + VectorLengthSquared( cross ) / p->inertia;
}

// a direction or an offset in the world as the body has it: along its own axes
static void Body_ToLocal( const prop_t *p, const vec3_t world, vec3_t out ) {
	VectorSet( out, DotProduct( world, p->axis[0] ), DotProduct( world, p->axis[1] ), DotProduct( world, p->axis[2] ) );
}

// one of the spots of the body, as an offset from its middle in the world
static void Body_Spot( const prop_t *p, int n, vec3_t out ) {
	Body_ToWorld( p, p->spots[n], out );
}

// how far the body reaches from its middle along a direction
static float Body_Reach( const prop_t *p, const vec3_t dir ) {
	vec3_t local;

	Body_ToLocal( p, dir, local );
	return BG_PropReach( &p->shape, local );
}

// is a point in the body or within BODY_SKIN of it
static qboolean Body_Holds( const gentity_t *ent, const prop_t *p, const vec3_t point ) {
	vec3_t rel, local;

	VectorSubtract( point, ent->r.currentOrigin, rel );
	Body_ToLocal( p, rel, local );
	return BG_PropHolds( &p->shape, local, BODY_SKIN );
}

// the spot of the body next to an offset from its middle. Returns how far the offset is from it, 0 inside.
static float Body_Nearest( const prop_t *p, const vec3_t rel, vec3_t out ) {
	vec3_t		local, spot;
	const float	dist = ( Body_ToLocal( p, rel, local ), BG_PropNearest( &p->shape, local, spot ) );

	Body_ToWorld( p, spot, out );
	return dist;
}

// the box in the world the turned body fits in, a unit smaller
static void Body_Bounds( const prop_t *p, vec3_t mins, vec3_t maxs ) {
	vec3_t	dir;
	int		k;

	for ( k = 0; k < 3; k++ ) {
		VectorClear( dir );
		dir[k] = 1.0f;
		maxs[k] = Body_Reach( p, dir ) - 1.0f;
		dir[k] = -1.0f;
		mins[k] = -( Body_Reach( p, dir ) - 1.0f );
		if ( maxs[k] < 1.0f )
			maxs[k] = 1.0f;
		if ( mins[k] > -1.0f )
			mins[k] = -1.0f;
	}
}

// the angles AnglesToAxis makes these axes from
static void Body_Angles( const prop_t *p, vec3_t angles ) {
	const float flat = sqrtf( p->axis[0][0] * p->axis[0][0] + p->axis[0][1] * p->axis[0][1] );

	if ( flat > 0.001f ) {
		angles[PITCH] = RAD2DEG( atan2f( -p->axis[0][2], flat ) );
		angles[YAW] = RAD2DEG( atan2f( p->axis[0][1], p->axis[0][0] ) );
		angles[ROLL] = RAD2DEG( atan2f( p->axis[1][2], p->axis[2][2] ) );
	} else {
		// forward is straight up or down: yaw and roll turn about the same line, all of it goes into the roll
		const float sp = ( p->axis[0][2] > 0.0f ) ? -1.0f : 1.0f;

		angles[PITCH] = sp * 90.0f;
		angles[YAW] = 0.0f;
		angles[ROLL] = RAD2DEG( atan2f( sp * p->axis[1][0], p->axis[1][1] ) );
	}
}

// turns the body by its spin over dt
static void Body_Turn( prop_t *p, float dt ) {
	vec3_t		about;
	const float	angle = VectorNormalize2( p->spin, about ) * dt;
	const float	c = cosf( angle ), s = sinf( angle );
	int			i, k;

	if ( angle < 0.00001f )
		return;

	for ( i = 0; i < 3; i++ ) {
		vec3_t	old, cross;
		float	along;

		VectorCopy( p->axis[i], old );
		CrossProduct( about, old, cross );
		along = DotProduct( about, old ) * ( 1.0f - c );
		for ( k = 0; k < 3; k++ )
			p->axis[i][k] = old[k] * c + cross[k] * s + about[k] * along;
	}

	// the three stay at right angles and of length 1
	VectorNormalize( p->axis[0] );
	CrossProduct( p->axis[0], p->axis[1], p->axis[2] );
	VectorNormalize( p->axis[2] );
	CrossProduct( p->axis[2], p->axis[0], p->axis[1] );
}

// keeps the speeds in bounds. qfalse: the numbers went bad.
static qboolean Body_Sane( gentity_t *self, prop_t *p ) {
	const float speed = VectorLength( self->movedir ), spin = VectorLength( p->spin );

	if ( !isfinite( speed ) || !isfinite( spin ) )
		return qfalse;
	if ( speed > BODY_MAX_SPEED )
		VectorScale( self->movedir, BODY_MAX_SPEED / speed, self->movedir );
	if ( spin > BODY_MAX_SPIN )
		VectorScale( p->spin, BODY_MAX_SPIN / spin, p->spin );
	return qtrue;
}

/*
===============================================================================

What the client gets, home

===============================================================================
*/

// the free ball: a linear trajectory the server renews every frame, with a second one for the rolling
static void Ball_Net( gentity_t *self ) {
	const float	speed = VectorLength( self->movedir );
	vec3_t		angles;

	VectorCopy( self->r.currentOrigin, self->s.pos.trBase );
	self->s.pos.trTime = level.time;
	self->s.pos.trDuration = 0;

	BG_EvaluateTrajectory( &self->s.apos, level.time, angles );
	self->s.apos.trTime = level.time;
	self->s.apos.trDuration = 0;

	if ( self->genericValue2 || speed < 1.0f ) {
		self->s.pos.trType = TR_STATIONARY;
		VectorClear( self->s.pos.trDelta );
		self->s.apos.trType = TR_STATIONARY;
		VectorCopy( angles, self->s.apos.trBase );
		VectorClear( self->s.apos.trDelta );
	} else {
		self->s.pos.trType = TR_LINEAR;
		VectorCopy( self->movedir, self->s.pos.trDelta );

		// it rolls the way it moves: one turn for every circumference
		self->s.apos.trType = TR_LINEAR;
		VectorSet( self->s.apos.trBase, AngleNormalize360( angles[PITCH] ), vectoyaw( self->movedir ), 0 );
		VectorSet( self->s.apos.trDelta, speed / self->radius * ( 180.0f / M_PI ), 0, 0 );
	}

	trap->LinkEntity( (sharedEntity_t *)self );
}

// the rigid body: where the origin of its model is and how it is turned. The client goes from one frame of the
// server to the next with both (TR_INTERPOLATE), like it does with players.
static void Body_Net( gentity_t *self, const prop_t *p ) {
	const trType_t	type = self->genericValue2 ? TR_STATIONARY : TR_INTERPOLATE;
	vec3_t			org, angles;
	int				k;

	// the origin of a model is not always its middle
	VectorCopy( self->r.currentOrigin, org );
	for ( k = 0; k < 3; k++ )
		VectorMA( org, -p->shape.centre[k], p->axis[k], org );
	Body_Angles( p, angles );

	VectorCopy( org, self->s.pos.trBase );
	VectorClear( self->s.pos.trDelta );
	self->s.pos.trType = type;
	self->s.pos.trTime = level.time;
	self->s.pos.trDuration = 0;

	VectorCopy( angles, self->s.apos.trBase );
	VectorClear( self->s.apos.trDelta );
	self->s.apos.trType = type;
	self->s.apos.trTime = level.time;
	self->s.apos.trDuration = 0;

	// What shots, sabers and explosions are traced against. An entity has a box that does not turn, so it is the
	// one the turned body just fits in: right for a crate on its face, generous at the ends of a beam that lies
	// across the axes.
	// A ball in a hand comes through here too and keeps the box of its radius.
	if ( p->box ) {
		Body_Bounds( p, self->r.mins, self->r.maxs );
		for ( k = 0; k < 3; k++ ) {
			self->r.mins[k] -= 1.0f;
			self->r.maxs[k] += 1.0f;
		}
	}

	// For the clients, who predict the moves of their players. The one of the port makes the shape from the
	// model itself, as the server did: generic1 says how, and who holds it walks through it.
	self->s.generic1 = p->solid;
	self->s.otherEntityNum = p->holder ? p->holder->s.number : ENTITYNUM_NONE;

	trap->LinkEntity( (sharedEntity_t *)self );

	// A client that knows nothing of that predicts with the box every solid entity has for it: not turned, as
	// wide as it is long, around the origin of the model. The link took it away (the contents of a prop are not
	// solid for the engine), so it is set here: the box around the body as well as such a box can be. Not while
	// somebody carries it, he would run into it.
	if ( p->solid && !p->holder ) {
		vec3_t	lo, hi;
		int		x, down, up;

		VectorAdd( self->r.currentOrigin, self->r.mins, lo );
		VectorSubtract( lo, org, lo );
		VectorAdd( self->r.currentOrigin, self->r.maxs, hi );
		VectorSubtract( hi, org, hi );
		x = Com_Clampi( 1, 255, (int)( ( fabsf( lo[0] ) + fabsf( hi[0] ) + fabsf( lo[1] ) + fabsf( hi[1] ) ) * 0.25f ) );
		down = Com_Clampi( 1, 255, (int)-lo[2] );
		up = Com_Clampi( 1, 255, (int)hi[2] + 32 );
		self->s.solid = ( up << 16 ) | ( down << 8 ) | x;
	}
}

static void Prop_Home( gentity_t *self ) {
	prop_t *p = Prop_State( self );

	VectorCopy( self->pos1, self->r.currentOrigin );
	Prop_Seen( self );
	VectorClear( self->movedir );
	VectorClear( p->spin );
	p->holder = NULL;
	p->gun = qfalse;
	p->frozen = qfalse;
	self->genericValue6 = 0;
	self->s.eFlags ^= EF_TELEPORT_BIT;
	Prop_Wake( self );
	if ( p->box )
		Body_Net( self, p );
	else
		Ball_Net( self );
}

// adds a speed to it, at an offset from its middle (cube, or a ball in a hand) or through the middle. The mass
// takes its part.
static void Prop_Impulse( gentity_t *self, const vec3_t dir, float speed, const vec3_t at ) {
	prop_t	*p = Prop_State( self );
	vec3_t	impulse;

	if ( p->frozen )
		return;
	VectorScale( dir, speed / self->mass, impulse );
	if ( at && ( p->box || p->holder ) )
		Body_Push( self, p, at, impulse );
	else
		VectorAdd( self->movedir, impulse, self->movedir );
	Prop_Wake( self );
}

/*
===============================================================================

Players and NPCs who run into them

===============================================================================
*/

// can this one kick or shove a prop: a living player or NPC with a body
static qboolean Prop_Mover( const gentity_t *other ) {
	const gclient_t *client = other->client;

	if ( !other->inuse || !client || other->health <= 0 || client->noclip )
		return qfalse;
	// only what has a body: not a ghost, not the stalker of g_schizo.c
	if ( !(other->r.contents & CONTENTS_BODY) )
		return qfalse;
	if ( other - g_entities < MAX_CLIENTS && ( client->pers.connected != CON_CONNECTED
		|| client->sess.sessionTeam == TEAM_SPECTATOR || client->tempSpectate >= level.time ) )
		return qfalse;
	return qtrue;
}

// the player as an upright stick with round ends: the point of it at a height
static void Prop_PlayerSpot( const gentity_t *other, float height, vec3_t spot ) {
	const gclient_t	*client = other->client;
	const float		lo = client->ps.origin[2] + other->r.mins[2] + PROP_PLAYER_RADIUS;
	float			hi = client->ps.origin[2] + other->r.maxs[2] - PROP_PLAYER_RADIUS;

	if ( hi < lo )
		hi = lo;
	VectorSet( spot, client->ps.origin[0], client->ps.origin[1], Com_Clamp( lo, hi, height ) );
}

/*
=================
Prop_PlayerPush

How fast a player goes towards a prop: his speed, or along normal (from him to it) the speed he asks for with
his keys if that is more. A body is in his way and stops him, so his own speed says nothing about how hard he
pushes against it.
=================
*/
static void Prop_PlayerPush( const gentity_t *other, const vec3_t normal, vec3_t vel ) {
	const gclient_t	*client = other->client;
	const usercmd_t	*cmd = &client->pers.cmd;
	vec3_t			angles, fwd, right, wish;
	float			keys, more;

	VectorCopy( client->ps.velocity, vel );
	if ( other - g_entities >= MAX_CLIENTS || ( !cmd->forwardmove && !cmd->rightmove ) )
		return;

	VectorSet( angles, 0.0f, client->ps.viewangles[YAW], 0.0f );
	AngleVectors( angles, fwd, right, NULL );
	VectorScale( fwd, cmd->forwardmove, wish );
	VectorMA( wish, cmd->rightmove, right, wish );
	keys = VectorNormalize( wish );
	VectorScale( wish, client->ps.speed * ( keys < 127.0f ? keys / 127.0f : 1.0f ), wish );
	more = DotProduct( wish, normal ) - DotProduct( vel, normal );
	if ( more > 0.0f )
		VectorMA( vel, more, normal, vel );
}

/*
=================
Prop_Hurt

A prop touches a player or an NPC (spot: the point of him next to it) or will when this frame is over: if it
comes at him fast enough it hurts him, the more the heavier it is. Who threw, shoved or shot it a moment ago
did that to him, and is not hurt by it himself right away.
=================
*/
static void Prop_Hurt( gentity_t *self, prop_t *p, gentity_t *other, const vec3_t spot ) {
	const qboolean	thrown = ( p->thrower && p->thrower->inuse && p->thrower->client
						&& level.time - p->throwTime < PROP_THROW_MSEC ) ? qtrue : qfalse;
	vec3_t			dir;
	float			hit;
	int				damage;

	// how fast its middle comes at him. Not along the face that touches him: a fast one is deep in him by the
	// time it is seen, and what is next to him then is some side of it.
	VectorSubtract( spot, self->r.currentOrigin, dir );
	VectorNormalize( dir );
	hit = DotProduct( self->movedir, dir );
	if ( hit < PROP_HURT_SPEED || level.time < p->hurtTime )
		return;
	if ( thrown && other == p->thrower && level.time - p->throwTime < PROP_THROW_SAFE )
		return;
	p->hurtTime = level.time + PROP_HURT_MSEC;

	damage = 1 + (int)( ( hit - PROP_HURT_SPEED ) * PROP_HURT_SCALE * self->mass );
	if ( damage > PROP_HURT_MAX )
		damage = PROP_HURT_MAX;
	G_Damage( other, self, thrown ? p->thrower : self, dir, other->r.currentOrigin, damage, 0, MOD_CRUSH );
}

// a player had his hands on it: what it hits now is his doing
static void Prop_Thrown( prop_t *p, gentity_t *by ) {
	if ( by && by->client ) {
		p->thrower = by;
		p->throwTime = level.time;
	}
}

/*
=================
Ball_Players

A player or NPC who touches the ball kicks it: the ball bounces off him like off something much heavier
that moves, and it is put outside of him. A ball that comes at him fast hurts him.
=================
*/
static void Ball_Players( gentity_t *self, prop_t *p, float dt ) {
	const float	reach = self->radius + PROP_PLAYER_RADIUS;
	int			i;

	for ( i = 0; i < moverCount; i++ ) {
		gentity_t	*other = &g_entities[moverList[i]];
		gclient_t	*client = other->client;
		vec3_t		spot, normal, rel, out;
		float		dist, into;
		trace_t		tr;

		// the one who holds it does not kick it out of his own hand
		if ( !Prop_Mover( other ) || other == p->holder )
			continue;
		Prop_PlayerSpot( other, self->r.currentOrigin[2], spot );

		// a fast one is past him before it was ever seen next to him: where it will be when this frame is over
		VectorMA( self->r.currentOrigin, dt, self->movedir, out );
		if ( Distance( out, spot ) < reach )
			Prop_Hurt( self, p, other, spot );

		VectorSubtract( self->r.currentOrigin, spot, normal );
		dist = VectorNormalize( normal );
		if ( dist >= reach )
			continue;
		if ( dist < 0.1f )
			VectorSet( normal, 0, 0, 1 );
		Prop_Hurt( self, p, other, spot );

		// how fast they come together
		VectorSubtract( self->movedir, client->ps.velocity, rel );
		into = DotProduct( rel, normal );
		if ( into < 0.0f ) {
			VectorMA( self->movedir, -( 1.0f + self->physicsBounce ) * into * self->speed / self->mass, normal, self->movedir );
			// a kicked ball comes off the ground a little
			self->movedir[2] += -into * 0.15f * self->speed / self->mass;
			if ( -into > PROP_LOUD )
				Prop_BounceSound( self );
			Prop_Thrown( p, other );
		}
		Prop_Wake( self );

		// out of the player, as far as the world lets it
		VectorMA( self->r.currentOrigin, reach - dist + 0.5f, normal, out );
		trap->Trace( &tr, self->r.currentOrigin, self->r.mins, self->r.maxs, out, self->s.number, MASK_SOLID, qfalse, 0, 0 );
		if ( !tr.startsolid && !tr.allsolid )
			VectorCopy( tr.endpos, self->r.currentOrigin );
	}
}

/*
=================
Cube_Players

A player or NPC who walks against the body shoves it: an impulse at the spot of the body next to him, which
turns it too when that spot is off its middle. It is heavy: only part of his speed gets into it, and he is
slowed down while he shoves (G_PropPlayerSpeed). A body that comes at him fast hurts him.

The body is in the way of a player (G_PropClipMove), so he stops at its faces with his box and shoves from
there. Should he be in it all the same, because it fell on him or was thrown at him, it is put outside of him.
=================
*/
static void Cube_Players( gentity_t *self, prop_t *p, float dt ) {
	const qboolean	fast = ( VectorLengthSquared( self->movedir ) * dt * dt > PROP_PLAYER_TOUCH * PROP_PLAYER_TOUCH ) ? qtrue : qfalse;
	vec3_t			mins, maxs;
	int				i;

	for ( i = 0; i < moverCount; i++ ) {
		gentity_t	*other = &g_entities[moverList[i]];
		gclient_t	*client = other->client;
		vec3_t		spot, rel, r, normal, speed, vel, out;
		float		dist, depth, into;
		trace_t		tr;

		if ( !Prop_Mover( other ) || other == p->holder )
			continue;
		Prop_PlayerSpot( other, self->r.currentOrigin[2], spot );

		VectorSubtract( spot, self->r.currentOrigin, rel );
		// a fast one is in him before it was ever seen next to him: where it will be when this frame is over
		if ( fast ) {
			VectorMA( rel, -dt, self->movedir, out );
			if ( Body_Nearest( p, out, r ) < PROP_PLAYER_RADIUS )
				Prop_Hurt( self, p, other, spot );
		}
		dist = Body_Nearest( p, rel, r );
		if ( dist >= PROP_PLAYER_RADIUS + PROP_PLAYER_TOUCH )
			continue;
		Prop_Hurt( self, p, other, spot );
		if ( dist > 0.1f ) {
			// from him to the body
			VectorSubtract( r, rel, normal );
			VectorScale( normal, 1.0f / dist, normal );
			depth = PROP_PLAYER_RADIUS - dist;
		} else {
			// he is in the body: out to the side he is not on, a bit in every frame
			VectorSet( normal, -rel[0], -rel[1], 0.0f );
			if ( VectorNormalize( normal ) < 1.0f ) {
				const float yaw = DEG2RAD( client->ps.viewangles[YAW] );

				VectorSet( normal, cosf( yaw ), sinf( yaw ), 0.0f );
			}
			VectorClear( r );
			depth = 8.0f;
		}

		// how fast they come together at that spot
		Body_SpeedAt( self, p, r, speed );
		Prop_PlayerPush( other, normal, vel );
		VectorSubtract( speed, vel, speed );
		into = DotProduct( speed, normal );
		if ( into < -1.0f ) {
			vec3_t rn, impulse;

			CrossProduct( r, normal, rn );
			VectorScale( normal, -( 1.0f + self->physicsBounce ) * into * self->speed
				/ ( self->mass * ( 1.0f + VectorLengthSquared( rn ) / p->inertia ) ), impulse );
			Body_Push( self, p, r, impulse );
			if ( -into > PROP_LOUD )
				Prop_BounceSound( self );
			Prop_Thrown( p, other );
			Prop_Wake( self );

			// shoving it is work
			if ( other->s.number < MAX_CLIENTS && DotProduct( vel, normal ) > 10.0f ) {
				shoveTime[other->s.number] = level.time + SHOVE_SLOW_MSEC;
				shoveScale[other->s.number] = Com_Clamp( 0.25f, 1.0f, 1.0f / ( 1.0f + 0.2f * self->mass ) );
			}
		}

		// He touches it and no more, or he stands on it: there is nowhere to put it then but into the ground.
		// A body a player stands on is left asleep, it does not shake under him.
		if ( depth <= 0.0f || normal[2] < -0.5f )
			continue;
		Prop_Wake( self );

		// out of him, as far as the world lets it
		Body_Bounds( p, mins, maxs );
		VectorMA( self->r.currentOrigin, depth + 0.5f, normal, out );
		trap->Trace( &tr, self->r.currentOrigin, mins, maxs, out, self->s.number, MASK_SOLID, qfalse, 0, 0 );
		if ( !tr.startsolid && !tr.allsolid )
			VectorCopy( tr.endpos, self->r.currentOrigin );
	}
}

/*
=================
G_PropPlayerSpeed

For ClientThink_real: what is left of the speed of a player while he shoves a cube. The speed is part of the
player state, so the client predicts it.
=================
*/
float G_PropPlayerSpeed( const gentity_t *ent ) {
	const int n = ent - g_entities;

	if ( n < 0 || n >= MAX_CLIENTS || shoveTime[n] <= level.time )
		return 1.0f;
	return shoveScale[n];
}

/*
===============================================================================

The hand: +button13 takes the prop he looks at

===============================================================================
*/

// in the game and alive
static qboolean Prop_PlayerOK( const gentity_t *ent ) {
	const gclient_t *client = ent->client;

	if ( !ent->inuse || !client || client->pers.connected != CON_CONNECTED || ent->health <= 0 )
		return qfalse;
	if ( client->sess.sessionTeam == TEAM_SPECTATOR || client->tempSpectate >= level.time )
		return qfalse;
	return qtrue;
}

static qboolean Prop_HolderOK( const gentity_t *holder ) {
	return ( Prop_PlayerOK( holder ) && (holder->client->pers.cmd.buttons & BUTTON_GRAB) ) ? qtrue : qfalse;
}

// may carry a player and be carried by one: duels are left alone, and so is who sits in a vehicle or flies
// through the walls
static qboolean Prop_PlayerFree( const gentity_t *ent ) {
	const gclient_t *client = ent->client;

	return ( client->ps.duelInProgress || client->ps.m_iVehicleNum || client->noclip ) ? qfalse : qtrue;
}

// taking players is for the admins, of any rank of JA+
static qboolean Prop_Carrier( const gentity_t *ent ) {
	return ( ent->client->sess.jpAdminLevel != JPADM_NONE && Prop_PlayerFree( ent ) ) ? qtrue : qfalse;
}

// the point in front of his eyes he holds things at, not behind a wall
static void Prop_Hand( const gentity_t *holder, vec3_t hand ) {
	vec3_t	eye, fwd;
	trace_t	tr;

	VectorCopy( holder->client->ps.origin, eye );
	eye[2] += holder->client->ps.viewheight;
	AngleVectors( holder->client->ps.viewangles, fwd, NULL, NULL );
	VectorMA( eye, GRAB_HOLD, fwd, hand );
	trap->Trace( &tr, eye, NULL, NULL, hand, holder - g_entities, MASK_SOLID, qfalse, 0, 0 );
	VectorCopy( tr.endpos, hand );
}

// where a player is carried: right in front of the one who took him, on his level and a bit off the ground
static void Prop_HugSpot( const gentity_t *holder, vec3_t spot ) {
	vec3_t angles, fwd;

	VectorSet( angles, 0.0f, holder->client->ps.viewangles[YAW], 0.0f );
	AngleVectors( angles, fwd, NULL, NULL );
	VectorMA( holder->client->ps.origin, Com_Clamp( 0.0f, 64.0f, g_grabHugDist.value ), fwd, spot );
	spot[2] += GRAB_PLAYER_LIFT;
}

static void Prop_Release( gentity_t *self ) {
	prop_t *p = Prop_State( self );

	p->holder = NULL;
	p->gun = qfalse;
	Prop_Wake( self );
}

/*
===============================================================================

The physgun: the stun baton of an admin who typed amphysgun. Attack takes the prop he looks at into its beam,
from far away, and holds it where it was taken as he moves and looks around: stiff, without the swinging of the
hand, and it still bumps into the world. Use held next to attack and his mouse turns the prop instead of his
view (the move code keeps his view still, PmoveSingle). Alt attack next to attack freezes the prop where it is,
in the air too: nothing moves it then, until the physgun takes it again.

===============================================================================
*/

// he has the physgun in his hand and may use it
static qboolean Prop_GunOK( const gentity_t *ent ) {
	return ( Prop_PlayerOK( ent ) && ent->s.number < MAX_CLIENTS && gunOn[ent->s.number]
		&& ent->client->sess.jpAdminLevel != JPADM_NONE && ent->client->ps.weapon == WP_STUN_BATON ) ? qtrue : qfalse;
}

// does the one who holds a prop still hold it: the grab button, or attack with the physgun
static qboolean Prop_Held( const prop_t *p ) {
	if ( p->gun )
		return ( Prop_GunOK( p->holder ) && (p->holder->client->pers.cmd.buttons & BUTTON_ATTACK) ) ? qtrue : qfalse;
	return Prop_HolderOK( p->holder );
}

// where the beam holds the middle of a prop
static void Prop_GunHand( const gentity_t *holder, const prop_t *p, vec3_t hand ) {
	vec3_t eye, fwd;

	VectorCopy( holder->client->ps.origin, eye );
	eye[2] += holder->client->ps.viewheight;
	AngleVectors( holder->client->ps.viewangles, fwd, NULL, NULL );
	VectorMA( eye, p->gunDist, fwd, hand );
	VectorAdd( hand, p->gunOffset, hand );
}

// The holder of a physgun turned since the last frame: what is in his beam goes around with him, as if it were
// fixed to him. Its place from the point it is held at is turned here, the angle is returned in degrees for the
// turn of the thing itself.
static float Prop_GunTurn( const gentity_t *holder, prop_t *p ) {
	const float	yaw = holder->client->ps.viewangles[YAW], turn = AngleSubtract( yaw, p->gunYaw );
	const float	s = sinf( DEG2RAD( turn ) ), c = cosf( DEG2RAD( turn ) );
	const float	x = p->gunOffset[0], y = p->gunOffset[1];

	p->gunYaw = yaw;
	p->gunOffset[0] = c * x - s * y;
	p->gunOffset[1] = s * x + c * y;
	return turn;
}

// attack was pressed: the prop or the player he looks at goes into the beam, a frozen prop is free again
static void Prop_GunTake( gentity_t *ent ) {
	const int	n = ent->s.number;
	gentity_t	*prop;
	prop_t		*p;
	vec3_t		eye, fwd, end;
	trace_t		tr;

	VectorCopy( ent->client->ps.origin, eye );
	eye[2] += ent->client->ps.viewheight;
	AngleVectors( ent->client->ps.viewangles, fwd, NULL, NULL );
	VectorMA( eye, PHYSGUN_RANGE, fwd, end );
	// what a shot hits: the world, players, and the box of a prop
	trap->Trace( &tr, eye, NULL, NULL, end, n, MASK_SHOT, qfalse, 0, 0 );
	if ( tr.fraction >= 1.0f || tr.entityNum >= ENTITYNUM_WORLD )
		return;
	prop = &g_entities[tr.entityNum];
	p = Prop_State( prop );

	if ( tr.entityNum < MAX_CLIENTS ) {
		// A player: he is carried the way Prop_CarryPlayer does it, by the beam instead of in a hug. The same
		// rules as there: no duels, nobody who is carried takes one, a pair stays a pair until it is apart.
		if ( p->holder || Prop_State( ent )->holder || !Prop_PlayerOK( prop ) || !Prop_PlayerFree( prop ) || !Prop_PlayerFree( ent )
			|| ( mates[n] && mates[n] != prop ) || ( mates[tr.entityNum] && mates[tr.entityNum] != ent ) )
			return;
		p->holder = ent;
		p->gun = qtrue;
		p->gunDist = Distance( eye, tr.endpos );
		p->gunYaw = ent->client->ps.viewangles[YAW];
		VectorSubtract( prop->client->ps.origin, tr.endpos, p->gunOffset );
		Prop_GunHand( ent, p, p->hand );
		gunProp[n] = prop;
		return;
	}
	if ( !Prop_Is( prop ) || p->holder )
		return;

	p->frozen = qfalse;
	p->holder = ent;
	p->gun = qtrue;
	// held by its middle, so it does not swing
	VectorClear( p->grab );
	VectorClear( p->gunSpin );
	p->gunDist = Distance( eye, tr.endpos );
	p->gunYaw = ent->client->ps.viewangles[YAW];
	VectorSubtract( prop->r.currentOrigin, tr.endpos, p->gunOffset );
	if ( !p->box ) {
		// a free ball only shows a roll, held it turns for real: from what the client sees of it now
		vec3_t angles;

		BG_EvaluateTrajectory( &prop->s.apos, level.time, angles );
		AnglesToAxis( angles, p->axis );
	}
	VectorClear( p->spin );
	Prop_GunHand( ent, p, p->hand );
	gunProp[n] = prop;
	Prop_Wake( prop );
}

// use with nothing in the beam: every prop a physgun froze is free again
static void Prop_GunThaw( gentity_t *ent ) {
	int i, count = 0;

	for ( i = 0; i < propCount; i++ ) {
		gentity_t	*prop = &g_entities[propList[i]];
		prop_t		*p = &props[propList[i]];

		if ( !Prop_Is( prop ) || !p->frozen )
			continue;
		p->frozen = qfalse;
		Prop_Wake( prop );
		count++;
	}
	if ( count )
		trap->SendServerCommand( ent->s.number, va( "print \"^7Physgun: %i frozen props are free again.\n\"", count ) );
}

// alt attack with a prop in the beam: it stays where it is
static void Prop_GunFreeze( gentity_t *prop ) {
	prop_t *p = Prop_State( prop );

	Prop_Release( prop );
	p->frozen = qtrue;
	VectorClear( prop->movedir );
	VectorClear( p->spin );
	prop->genericValue2 = 1;
	if ( p->box )
		Body_Net( prop, p );
	else
		Ball_Net( prop );
}

// A server frame of a client slot and its physgun. By the number of the slot: the entity of a slot nobody is
// in has the number 0, and with that every empty slot would write into the state of client 0.
static void Prop_GunFrame( int n ) {
	gentity_t	*ent = &g_entities[n];
	const int	buttons = ( ent->inuse && ent->client ) ? ent->client->pers.cmd.buttons : 0;
	gentity_t	*prop = gunProp[n];

	gentity_t	*beam = gunBeam[n];

	if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED )
		gunOn[n] = qfalse;
	if ( prop && ( ( !Prop_Is( prop ) && !prop->client ) || Prop_State( prop )->holder != ent || !Prop_State( prop )->gun ) )
		prop = gunProp[n] = NULL;

	if ( Prop_GunOK( ent ) ) {
		if ( !prop && (buttons & BUTTON_ATTACK) && !(gunButtons[n] & BUTTON_ATTACK) )
			Prop_GunTake( ent );
		// a player is not frozen
		else if ( prop && !prop->client && (buttons & BUTTON_ALT_ATTACK) && !(gunButtons[n] & BUTTON_ALT_ATTACK) )
			Prop_GunFreeze( prop );
	}
	gunButtons[n] = buttons;

	// The beam: an entity that names its two ends, the cgame of the port draws a line between them. The entities
	// of a map that is started again are gone without a word, so the one that is remembered is looked at first.
	prop = gunProp[n];
	if ( beam && ( !beam->inuse || beam->s.eType != ET_INVISIBLE || beam->s.generic1 != PROP_BEAM || beam->s.otherEntityNum != n ) )
		beam = gunBeam[n] = NULL;
	if ( prop ) {
		if ( !beam ) {
			beam = gunBeam[n] = G_Spawn();
			beam->classname = "physgun_beam";
			beam->s.eType = ET_INVISIBLE;
			beam->s.generic1 = PROP_BEAM;
			beam->s.otherEntityNum = n;
			// its ends can be anywhere, everybody gets it
			beam->r.svFlags = SVF_BROADCAST;
		}
		beam->s.otherEntityNum2 = prop->s.number;
		G_SetOrigin( beam, ent->client->ps.origin );
		trap->LinkEntity( (sharedEntity_t *)beam );
	} else if ( beam ) {
		G_FreeEntity( beam );
		gunBeam[n] = NULL;
	}

	// he keeps the baton over a death while he has the physgun
	if ( gunOn[n] && Prop_PlayerOK( ent ) )
		ent->client->ps.stats[STAT_WEAPONS] |= ( 1 << WP_STUN_BATON );
}

/*
=================
G_PropGunCmd

For ClientThink_real, with every command of a player: while he turns a prop with the physgun (use and attack
held) his view stands still and what his mouse does is kept for the prop. The angles of a command go on with
the mouse whatever his view does.

And the distance of the beam: every press of +button14 moves the prop away along it, every press of +button15
brings it closer. The cgame of the port presses them for one command when the next or previous weapon is asked
for with a prop in the beam (the mouse wheel), any client can bind them. A press that short is in one command
only, so it is looked for here and not once a frame.
=================
*/
void G_PropGunCmd( gentity_t *ent, const usercmd_t *ucmd ) {
	const int	n = ent->s.number;
	float		pitch, yaw;
	int			pressed;

	if ( n < 0 || n >= MAX_CLIENTS )
		return;

	pressed = ucmd->buttons & ~gunCmdButtons[n];
	gunCmdButtons[n] = ucmd->buttons;
	// use alone, with nothing in the beam: the reload key of that other game
	if ( (pressed & BUTTON_USE) && !(ucmd->buttons & BUTTON_ATTACK) && !gunProp[n] && Prop_GunOK( ent ) )
		Prop_GunThaw( ent );
	if ( gunProp[n] && (pressed & (BUTTON_GUN_FAR|BUTTON_GUN_NEAR)) && Prop_GunOK( ent ) ) {
		prop_t	*p = Prop_State( gunProp[n] );
		float	step = PHYSGUN_STEP * p->gunDist;

		if ( step < PHYSGUN_MIN_STEP )
			step = PHYSGUN_MIN_STEP;
		if ( pressed & BUTTON_GUN_NEAR )
			step = -step;
		p->gunDist = Com_Clamp( PHYSGUN_NEAR, PHYSGUN_RANGE, p->gunDist + step );
	}
	pitch = SHORT2ANGLE( ucmd->angles[PITCH] );
	yaw = SHORT2ANGLE( ucmd->angles[YAW] );
	if ( gunProp[n] && (ucmd->buttons & BUTTON_USE) && (ucmd->buttons & BUTTON_ATTACK) && Prop_GunOK( ent ) ) {
		gunTurn[n][PITCH] += AngleSubtract( pitch, gunAngles[n][PITCH] );
		gunTurn[n][YAW] += AngleSubtract( yaw, gunAngles[n][YAW] );
	}
	gunAngles[n][PITCH] = pitch;
	gunAngles[n][YAW] = yaw;
}

/*
=================
G_PropGunToggle

The command amphysgun of an admin: gives him the physgun or takes it away. It is the stun baton, which he gets
with it.
=================
*/
void G_PropGunToggle( gentity_t *ent ) {
	const int n = ent->s.number;

	if ( n < 0 || n >= MAX_CLIENTS || !ent->client )
		return;
	if ( ent->client->sess.jpAdminLevel == JPADM_NONE ) {
		trap->SendServerCommand( n, "print \"^7 You are not allowed to execute this command! \n\"" );
		return;
	}

	gunOn[n] = (qboolean)!gunOn[n];
	if ( gunOn[n] ) {
		ent->client->ps.stats[STAT_WEAPONS] |= ( 1 << WP_STUN_BATON );
		trap->SendServerCommand( n, "print \"^7Physgun ^2on^7: it is the stun baton. Attack takes the prop or the player you look "
			"at, use with it turns it with the mouse, alt attack with it freezes a prop where it is, the wheel moves it away "
			"and closer. Use alone frees every frozen prop.\n\"" );
	} else {
		// what is in the beam finds out in its next frame that nobody holds it
		trap->SendServerCommand( n, "print \"^7Physgun ^1off^7.\n\"" );
	}
}

// how well a spot lies on his line of sight, the lower the better. Below 0: he can not take it there.
static float Prop_AimScore( const vec3_t eye, const vec3_t fwd, const vec3_t spot ) {
	vec3_t	dir;
	float	along, off;

	VectorSubtract( spot, eye, dir );
	along = DotProduct( dir, fwd );
	if ( along < 0.0f || along > GRAB_REACH )
		return -1.0f;
	VectorMA( dir, -along, fwd, dir );
	off = VectorLength( dir );
	if ( off > GRAB_AIM )
		return -1.0f;
	// of two spots as far off the line the one closer to him
	return off + 0.25f * along;
}

// a spot that lies better on his line of sight than the best one so far takes its place
static void Prop_AimBetter( const vec3_t eye, const vec3_t fwd, const vec3_t spot, float *score, vec3_t best ) {
	const float s = Prop_AimScore( eye, fwd, spot );

	if ( s >= 0.0f && ( *score < 0.0f || s < *score ) ) {
		*score = s;
		VectorCopy( spot, best );
	}
}

/*
=================
Prop_Grab

The player pressed the button: of the props in front of him, and of the players if he is an admin, he takes
the one with the spot closest to his line of sight. A cube is taken at a corner, a ball at the spot next to his
eye. A player is looked for at his legs, his chest and his head. Wherever that was, he is hugged and carried
(Prop_CarryPlayer).
=================
*/
static void Prop_Grab( gentity_t *ent ) {
	// who is carried takes no player himself: two who hold each other would lift each other off the ground.
	const qboolean	players = ( Prop_Carrier( ent ) && !Prop_State( ent )->holder ) ? qtrue : qfalse;
	gentity_t	*best = NULL;
	vec3_t		eye, fwd, bestSpot, rel;
	float		bestScore = 0.0f;
	prop_t		*p;
	int			i, k;

	VectorCopy( ent->client->ps.origin, eye );
	eye[2] += ent->client->ps.viewheight;
	AngleVectors( ent->client->ps.viewangles, fwd, NULL, NULL );
	VectorClear( bestSpot );

	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t	*prop = &g_entities[i];
		vec3_t		spot;
		float		score = -1.0f;
		trace_t		tr;

		p = &props[i];
		VectorClear( spot );
		if ( i < MAX_CLIENTS ) {
			int h;

			// and a hug is for two: who was in one with somebody else and is not out of him yet waits
			if ( !players || prop == ent || p->holder || !Prop_PlayerOK( prop ) || !Prop_PlayerFree( prop )
				|| ( mates[ent->s.number] && mates[ent->s.number] != prop ) || ( mates[i] && mates[i] != ent ) )
				continue;
			for ( h = 1; h <= 3; h++ ) {
				vec3_t at;

				VectorCopy( prop->client->ps.origin, at );
				at[2] += prop->r.mins[2] + 0.25f * h * ( prop->r.maxs[2] - prop->r.mins[2] );
				Prop_AimBetter( eye, fwd, at, &score, spot );
			}
		} else if ( !Prop_Is( prop ) || p->holder || p->frozen
			|| Distance( prop->r.currentOrigin, eye ) > GRAB_REACH + prop->radius ) {
			continue;
		} else if ( p->box ) {
			int c;

			for ( c = 0; c < p->shape.numVerts; c++ ) {
				vec3_t corner;

				Body_ToWorld( p, p->shape.verts[c], corner );
				VectorAdd( corner, prop->r.currentOrigin, corner );
				Prop_AimBetter( eye, fwd, corner, &score, spot );
			}
		} else {
			VectorSubtract( eye, prop->r.currentOrigin, spot );
			VectorNormalize( spot );
			VectorMA( prop->r.currentOrigin, prop->radius, spot, spot );
			score = Prop_AimScore( eye, fwd, spot );
		}
		if ( score < 0.0f || ( best && score >= bestScore ) )
			continue;

		// not through a wall
		trap->Trace( &tr, eye, NULL, NULL, spot, ent - g_entities, MASK_SOLID, qfalse, 0, 0 );
		if ( tr.fraction < 0.95f )
			continue;

		best = prop;
		bestScore = score;
		VectorCopy( spot, bestSpot );
	}
	if ( !best )
		return;

	p = Prop_State( best );
	p->holder = ent;
	if ( best->client ) {
		// the two of amhug, with their sabers away as for the emote. It goes on if one of them stays out.
		JP_HolsterPair( ent, best );
		JP_Hug( ent, best, SETANIM_TORSO );
		Prop_HugSpot( ent, p->hand );
		return;
	}
	if ( !p->box ) {
		// a free ball only shows a roll. In the hand it turns for real, from what the client sees of it now.
		vec3_t angles;

		BG_EvaluateTrajectory( &best->s.apos, level.time, angles );
		AnglesToAxis( angles, p->axis );
		VectorClear( p->spin );
	}
	VectorSubtract( bestSpot, best->r.currentOrigin, rel );
	for ( k = 0; k < 3; k++ )
		p->grab[k] = DotProduct( rel, p->axis[k] );
	Prop_Hand( ent, p->hand );
	Prop_Wake( best );
}

// Keeps an animation of the hug on a part of him: the upper body of the one who hugs, whose legs go on walking,
// and the legs of the one who is hugged. It is started again if something else took its place, and its timer
// is kept from running out: it does not loop, so that holds its last frame.
static void Prop_HugAnim( gentity_t *ent, int part, int anim ) {
	playerState_t	*ps = &ent->client->ps;
	int				*timer = ( part == SETANIM_TORSO ) ? &ps->torsoTimer : &ps->legsTimer;

	G_SetAnim( ent, NULL, part, anim, SETANIM_FLAG_OVERRIDE|SETANIM_FLAG_HOLD|SETANIM_FLAG_HOLDLESS, 0 );
	if ( *timer < GRAB_ANIM_MSEC )
		*timer = GRAB_ANIM_MSEC;
}

// The one who is hugged has the animation of his upper body as a hand extend, the way a taunt is played. While
// it lasts he can not attack, not use the Force with his hands and not take out his saber, which is put away.
// Whatever else has his hands (he lies on the ground, he is choked) is left to run out first.
static void Prop_HugHands( gentity_t *victim ) {
	playerState_t *ps = &victim->client->ps;

	if ( ps->forceHandExtend != HANDEXTEND_NONE && ps->forceHandExtend != HANDEXTEND_WEAPONREADY
		&& ps->forceHandExtend != HANDEXTEND_TAUNT )
		return;

	// the command that puts a saber away refuses with a hand extend
	ps->forceHandExtend = HANDEXTEND_NONE;
	JP_HolsterNow( victim );
	ps->forceHandExtend = HANDEXTEND_TAUNT;
	ps->forceDodgeAnim = BOTH_HUGGEE1;
	ps->forceHandExtendTime = level.time + GRAB_ANIM_MSEC;
}

// The hug is over, the animations end where they are, or the beam of the physgun let him go. He keeps the speed
// he has, so he can be thrown.
static void Prop_DropPlayer( gentity_t *victim ) {
	prop_t			*p = Prop_State( victim );
	playerState_t	*ps = &victim->client->ps, *arms = &p->holder->client->ps;

	if ( !p->gun ) {
		if ( arms->torsoAnim == BOTH_HUGGER1 )
			arms->torsoTimer = 0;
		if ( ps->legsAnim == BOTH_HUGGEE1 )
			ps->legsTimer = 0;
		if ( ps->forceHandExtend == HANDEXTEND_TAUNT && ps->forceDodgeAnim == BOTH_HUGGEE1 )
			ps->forceHandExtendTime = 0;
	}
	p->holder = NULL;
	p->gun = qfalse;
}

/*
=================
Prop_CarryPlayer

A server frame of a player in the arms of another one. He is no body the physics here could move, so he is
given a speed: the one that takes him to his place in front of the other and goes along with it. His own
movement code takes him there and stops him at walls. He is turned to the one who carries him, and both keep
the animations of amhug for as long as it lasts: he with all of his body, the one who carries him with his
upper body only, his legs walk.

For the hug to fit the two stand closer than their boxes allow. So neither is there for the moves of the
other while it lasts, see G_PropMate.
=================
*/
static void Prop_CarryPlayer( gentity_t *victim, float dt ) {
	prop_t			*p = Prop_State( victim );
	gentity_t		*holder = p->holder;
	playerState_t	*ps;
	vec3_t			spot, want, way, angles;
	float			len, fastest, turn = 0.0f;

	// the button was let go, one of them died, left or went into a duel, the admin logged out
	if ( ( p->gun ? !Prop_Held( p ) : ( !Prop_HolderOK( holder ) || !Prop_Carrier( holder ) ) )
		|| !Prop_PlayerOK( victim ) || !Prop_PlayerFree( victim ) ) {
		Prop_DropPlayer( victim );
		return;
	}
	ps = &victim->client->ps;

	if ( p->gun ) {
		turn = Prop_GunTurn( holder, p );
		Prop_GunHand( holder, p, spot );
	} else {
		Prop_HugSpot( holder, spot );
	}
	VectorSubtract( spot, ps->origin, want );
	// caught on something, or one of them was taken somewhere else. The beam does not let go.
	if ( !p->gun && VectorLength( want ) > GRAB_PLAYER_BREAK ) {
		Prop_DropPlayer( victim );
		return;
	}

	// the way his place went since the last frame
	VectorSubtract( spot, p->hand, way );
	VectorCopy( spot, p->hand );

	VectorCopy( ps->viewangles, angles );
	if ( p->gun ) {
		// In the beam of the physgun: no hug, his hands are free. He turns with the one who holds him, and
		// use next to attack and the mouse of that one turns him about the vertical.
		const int n = holder->s.number;

		turn += gunTurn[n][YAW];
		if ( turn != 0.0f ) {
			angles[YAW] = AngleNormalize180( angles[YAW] + turn );
			SetClientViewAngle( victim, angles );
		}
		gunTurn[n][PITCH] = gunTurn[n][YAW] = 0.0f;
	} else {
		angles[YAW] = AngleNormalize180( holder->client->ps.viewangles[YAW] + 180.0f );
		SetClientViewAngle( victim, angles );
		Prop_HugAnim( holder, SETANIM_TORSO, BOTH_HUGGER1 );
		Prop_HugAnim( victim, SETANIM_LEGS, BOTH_HUGGEE1 );
		Prop_HugHands( victim );
	}

	if ( dt < 0.001f )
		return;

	// The speed is set once a frame, to what has him at his place when the next snapshot is made: that is where
	// a client sees the two together. A bot makes its move later in this frame, so the way to his place is all.
	// A human makes his with the commands that come in until the next frame, and by then the place has gone on,
	// about as far as it did since the last one.
	VectorScale( want, 1.0f / dt, want );
	if ( !(victim->r.svFlags & SVF_BOT) )
		VectorMA( want, 1.0f / dt, way, want );
	len = VectorLength( want );
	fastest = p->gun ? PHYSGUN_PLAYER_SPEED : GRAB_PLAYER_SPEED;
	if ( len > fastest )
		VectorScale( want, fastest / len, want );
	// and what gravity takes of it until then, he hangs in the air
	want[2] += 0.5f * g_gravity.value * dt;
	VectorCopy( want, ps->velocity );

	// he has no grip on the ground and no say in where he goes, the way G_Damage does it for a hit
	if ( !ps->pm_time || (ps->pm_flags & PMF_TIME_KNOCKBACK) ) {
		ps->pm_time = GRAB_LIMP_MSEC;
		ps->pm_flags |= PMF_TIME_KNOCKBACK;
	}

	// who throws him into a pit gets the kill, as with force push and grip
	ps->otherKiller = holder->s.number;
	ps->otherKillerTime = level.time + 5000;
	ps->otherKillerDebounceTime = level.time + 100;
}

// the planes of a body for where it stands now. Kept for a few of them: a player makes many moves a frame
// against the same ones, and most bodies lie still.
static const propBrush_t *Prop_Brush( const gentity_t *ent, const prop_t *p ) {
	propSolid_t	*solid = NULL;
	int			i;

	for ( i = 0; i < PROP_BRUSHES; i++ ) {
		if ( propSolids[i].number != ent->s.number )
			continue;
		solid = &propSolids[i];
		if ( VectorCompare( solid->origin, ent->r.currentOrigin ) && !memcmp( solid->axis, p->axis, sizeof( solid->axis ) ) )
			return &solid->brush;
		break;
	}
	if ( !solid ) {
		solid = &propSolids[propSolidNext];
		propSolidNext = ( propSolidNext + 1 ) % PROP_BRUSHES;
	}
	solid->number = ent->s.number;
	VectorCopy( ent->r.currentOrigin, solid->origin );
	memcpy( solid->axis, p->axis, sizeof( solid->axis ) );
	BG_PropBrush( &p->shape, ent->r.currentOrigin, solid->axis, &solid->brush );
	return &solid->brush;
}

/*
=================
G_PropClipMove

For the movement trace of the server (SV_PMTrace), after the engine made it: the bodies are in the way of
players too. The engine can not do that, an entity is a box that is not turned for it, so the move is cut
here against the shape of every body it comes near (BG_PropClipBox), turned as it is. The cgame does the same
with the same code when it predicts.

They count as bodies for the mask: who walks through players (a ghost, a spectator, a corpse) walks through
props. A body is not in the way of the one who holds it, and the moves of a player who is in one are not cut
by it: he walks out.
=================
*/
void G_PropClipMove( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end,
		int passEntityNum, int contentMask ) {
	vec3_t	lo, hi;
	int		i, k;

	if ( !propCount || !(contentMask & CONTENTS_BODY) || results->allsolid )
		return;
	if ( !mins )
		mins = vec3_origin;
	if ( !maxs )
		maxs = vec3_origin;
	for ( k = 0; k < 3; k++ ) {
		lo[k] = ( start[k] < end[k] ? start[k] : end[k] ) + mins[k] - 1.0f;
		hi[k] = ( start[k] > end[k] ? start[k] : end[k] ) + maxs[k] + 1.0f;
	}

	for ( i = 0; i < propCount; i++ ) {
		const gentity_t	*ent = &g_entities[propList[i]];
		const prop_t	*p = &props[propList[i]];

		// the list is of the start of the frame
		if ( !Prop_Is( ent ) || !p->solid || ( p->holder && p->holder->s.number == passEntityNum ) )
			continue;
		for ( k = 0; k < 3; k++ ) {
			if ( ent->r.currentOrigin[k] + ent->radius < lo[k] || ent->r.currentOrigin[k] - ent->radius > hi[k] )
				break;
		}
		if ( k < 3 )
			continue;
		BG_PropClipBox( results, start, mins, maxs, end, Prop_Brush( ent, p ) );
	}
}

/*
=================
G_PropCarried

For the movement code of the server: this player is in the arms of another one. His own moves are taken from
him there, as for a player the admin froze: no walking, and no jump, with which he could kick off the one who
holds him.
=================
*/
qboolean G_PropCarried( int clientNum ) {
	return ( clientNum >= 0 && clientNum < MAX_CLIENTS && props[clientNum].holder ) ? qtrue : qfalse;
}

/*
=================
G_PropMate

For the movement trace of the server (SV_PMTrace): the other one of the hug this player is in, who is not
there for his moves, NULL without one. The two stand in each other. It goes on after the hug until they are
out of each other, or they would be stuck.
=================
*/
gentity_t *G_PropMate( int clientNum ) {
	return ( clientNum >= 0 && clientNum < MAX_CLIENTS ) ? mates[clientNum] : NULL;
}

/*
=================
G_PropFrame

Once per server frame: who pressed the grab button since the last one takes the prop or the player he looks
at. Letting go is the business of the prop, it looks at the button of its holder in its think. A player has no
think of that kind, the ones who are carried are moved from here.
=================
*/
void G_PropFrame( void ) {
	static int	lastTime;
	// the time since the last frame, capped for a server that hitched
	const float	dt = Com_Clamp( 0.0f, 0.1f, ( level.time - lastTime ) * 0.001f );
	int			i;

	lastTime = level.time;

	// the props there are, for the moves of the players (G_PropClipMove) and for each other, and who can touch them
	propCount = 0;
	memset( propPlace, 0, sizeof( propPlace ) );
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		if ( Prop_Is( &g_entities[i] ) ) {
			propList[propCount++] = i;
			propPlace[i] = propCount;
			Prop_Seen( &g_entities[i] );
		}
	}
	moverCount = 0;
	for ( i = 0; i < level.num_entities; i++ ) {
		if ( g_entities[i].inuse && g_entities[i].client )
			moverList[moverCount++] = i;
	}

	for ( i = 0; i < level.maxclients; i++ )
		Prop_GunFrame( i );

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t		*ent = &g_entities[i];
		const qboolean	down = ( ent->inuse && ent->client && (ent->client->pers.cmd.buttons & BUTTON_GRAB) ) ? qtrue : qfalse;

		if ( down && !grabDown[i] && Prop_HolderOK( ent ) )
			Prop_Grab( ent );
		grabDown[i] = down;
	}
	for ( i = 0; i < level.maxclients; i++ ) {
		if ( props[i].holder )
			Prop_CarryPlayer( &g_entities[i], dt );
		// still in his arms after that
		if ( props[i].holder ) {
			mates[i] = props[i].holder;
			mates[props[i].holder - g_entities] = &g_entities[i];
		}
	}

	// Who is whose, for the moves (G_PropMate) and for the clients of the two: STAT_CARRY lets them leave the
	// other one out of their prediction and draw him where he belongs. After a hug the two stay a pair until
	// their boxes are apart.
	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t	*ent = &g_entities[i], *mate = mates[i];
		int			carry = 0;

		if ( mate && !props[i].holder && Prop_State( mate )->holder != ent
			&& ( !Prop_PlayerOK( ent ) || !Prop_PlayerOK( mate )
				|| !G_BoundsOverlap( ent->r.absmin, ent->r.absmax, mate->r.absmin, mate->r.absmax ) ) )
			mate = mates[i] = NULL;
		if ( mate ) {
			carry = mate->s.number + 1;
			if ( props[i].holder )
				carry |= CARRY_HELD;
			else if ( Prop_State( mate )->holder == ent )
				carry |= CARRY_HOLDS;
		}
		// and the physgun: the move code of both sides reads these
		if ( gunOn[i] )
			carry |= CARRY_PHYSGUN;
		if ( gunProp[i] )
			carry |= CARRY_GUNHOLD;
		if ( ent->inuse && ent->client )
			ent->client->ps.stats[STAT_CARRY] = carry;
	}
}

/*
===============================================================================

The ball on its own

===============================================================================
*/

/*
=================
Ball_Move

Gravity, the way through the world with up to four bounces in a frame, rolling resistance on the ground
=================
*/
static void Ball_Move( gentity_t *self, float dt ) {
	float		*vel = self->movedir;
	vec3_t		org, end;
	float		left = dt, speed, groundZ = 0.0f;
	qboolean	ground = qfalse;
	trace_t		tr;
	int			bump;

	VectorCopy( self->r.currentOrigin, org );
	vel[2] -= g_gravity.value * dt;

	for ( bump = 0; bump < 4 && left > 0.0f; bump++ ) {
		float into;

		VectorMA( org, left, vel, end );
		trap->Trace( &tr, org, self->r.mins, self->r.maxs, end, self->s.number, MASK_SOLID, qfalse, 0, 0 );

		if ( tr.startsolid || tr.allsolid ) {
			// a door closed on it or it was put into a wall: up, and home if that does not help
			if ( ++self->genericValue6 > PROP_STUCK_FRAMES ) {
				Prop_Home( self );
				return;
			}
			org[2] += 2.0f;
			VectorClear( vel );
			break;
		}
		self->genericValue6 = 0;

		VectorCopy( tr.endpos, org );
		if ( tr.fraction >= 1.0f )
			break;
		left *= 1.0f - tr.fraction;

		into = DotProduct( vel, tr.plane.normal );
		if ( into < 0.0f ) {
			// fast enough it bounces, else it stays on the surface and slides or rolls along it
			const float bounce = ( -into > PROP_MIN_BOUNCE ) ? self->physicsBounce : 0.0f;

			VectorMA( vel, -( 1.0f + bounce ) * into, tr.plane.normal, vel );
			if ( -into > PROP_LOUD )
				Prop_BounceSound( self );
		}
		if ( tr.plane.normal[2] > 0.7f ) {
			ground = qtrue;
			groundZ = tr.plane.normal[2];
		}
	}

	VectorCopy( org, self->r.currentOrigin );

	speed = VectorLength( vel );
	if ( ground ) {
		if ( speed > 0.0f ) {
			const float slower = speed - self->random * dt;

			VectorScale( vel, slower > 0.0f ? slower / speed : 0.0f, vel );
			speed = slower > 0.0f ? slower : 0.0f;
		}
		if ( speed < BALL_REST_SPEED && groundZ >= BALL_FLAT ) {
			VectorClear( vel );
			self->genericValue2 = 1;
			self->genericValue5 = level.time + 500;
		}
	} else if ( speed > 0.0f ) {
		// a little drag in the air
		VectorScale( vel, 1.0f - 0.05f * dt, vel );
	}

	// fell out of the world or into a pit nothing comes back from
	if ( org[2] < self->pos1[2] - 8192.0f || ( trap->PointContents( org, self->s.number ) & CONTENTS_NODROP ) )
		Prop_Home( self );
}

/*
=================
Ball_Props

The ball against the other balls and the cubes: they bounce off each other, each by its mass, and are put
apart. A cube that lies still is a wall for the ball until it is hit hard. On top of a cube the ball rolls out
like on the ground.
=================
*/
static void Ball_Props( gentity_t *self, float dt ) {
	int i;

	for ( i = 0; i < propCount; i++ ) {
		gentity_t	*other = &g_entities[propList[i]];
		prop_t		*q = &props[propList[i]];
		const float	reach = self->radius + propSeenRadius[i];
		vec3_t		rel, normal, r2, speed, out;
		float		depth, into, ratio, len;
		qboolean	fixed;
		trace_t		tr;

		if ( Prop_SeenDistanceSquared( self->r.currentOrigin, i ) >= reach * reach )
			continue;
		if ( other == self || !Prop_Is( other ) )
			continue;
		VectorSubtract( self->r.currentOrigin, other->r.currentOrigin, rel );
		if ( VectorLengthSquared( rel ) >= reach * reach )
			continue;

		if ( q->box ) {
			// from the spot of the cube next to the middle of the ball
			depth = self->radius - Body_Nearest( q, rel, r2 );
			if ( depth <= 0.0f )
				continue;
			VectorSubtract( rel, r2, normal );
		} else {
			depth = reach - VectorLength( rel );
			VectorCopy( rel, normal );
		}
		if ( VectorNormalize( normal ) < 0.1f ) {
			// the middle of the ball is in it: out the way their middles lie, or up
			VectorCopy( rel, normal );
			if ( VectorNormalize( normal ) < 0.1f )
				VectorSet( normal, 0, 0, 1 );
		}
		if ( !q->box )
			VectorScale( normal, other->radius, r2 );

		// how fast they come together
		Body_SpeedAt( other, q, r2, speed );
		VectorSubtract( self->movedir, speed, speed );
		into = -DotProduct( speed, normal );
		fixed = Prop_Fixed( other, q, normal, into, -depth, self->mass / other->mass );
		ratio = fixed ? 0.0f : self->mass / other->mass;

		if ( into > 0.0f ) {
			const float	softer = ( other->physicsBounce < self->physicsBounce ) ? other->physicsBounce : self->physicsBounce;
			const float	bounce = ( into > PROP_MIN_BOUNCE ) ? softer : 0.0f;
			vec3_t		impulse;

			VectorScale( normal, ( 1.0f + bounce ) * into / ( 1.0f + ratio * Body_Give( q, r2, normal ) ), impulse );
			VectorAdd( self->movedir, impulse, self->movedir );
			if ( !fixed ) {
				VectorScale( impulse, -ratio, impulse );
				Body_Push( other, q, r2, impulse );
			}
			if ( into > PROP_LOUD )
				Prop_BounceSound( self );
			self->genericValue2 = 0;
		}

		if ( normal[2] > BODY_GROUND ) {
			// rolling resistance against its speed along the surface, seen from the other
			Body_SpeedAt( other, q, r2, speed );
			VectorSubtract( self->movedir, speed, speed );
			VectorMA( speed, -DotProduct( speed, normal ), normal, speed );
			len = VectorLength( speed );
			if ( len > 0.0f ) {
				const float slower = ( self->random * dt < len ) ? self->random * dt : len;

				VectorMA( self->movedir, -slower / len, speed, self->movedir );
			}
		}

		// apart, as far as the world lets them. The lighter one goes more of the way.
		if ( depth > 0.5f )
			self->genericValue2 = 0;
		VectorMA( self->r.currentOrigin, depth / ( 1.0f + ratio ), normal, out );
		trap->Trace( &tr, self->r.currentOrigin, self->r.mins, self->r.maxs, out, self->s.number, MASK_SOLID, qfalse, 0, 0 );
		if ( !tr.startsolid && !tr.allsolid )
			VectorCopy( tr.endpos, self->r.currentOrigin );
		// a cube finds the ball in its own step and goes its part there
		if ( !fixed && !q->box ) {
			VectorMA( other->r.currentOrigin, -depth * ratio / ( 1.0f + ratio ), normal, out );
			trap->Trace( &tr, other->r.currentOrigin, other->r.mins, other->r.maxs, out, other->s.number, MASK_SOLID, qfalse, 0, 0 );
			if ( !tr.startsolid && !tr.allsolid )
				VectorCopy( tr.endpos, other->r.currentOrigin );
		}
	}
}

/*
===============================================================================

The cube, and either prop in a hand

===============================================================================
*/

/*
=================
Contact_Add

A contact of the cube with another prop. Whether that one takes its part of the impulses is decided here
(Prop_Fixed), from how fast the two come together at the spot.
=================
*/
static int Contact_Add( gentity_t *self, const prop_t *p, gentity_t *other, const prop_t *q, contact_t *list, int count,
		const vec3_t r, const vec3_t r2, const vec3_t normal, float sep ) {
	const float	ratio = self->mass / other->mass;
	contact_t	*c;
	vec3_t		mine, theirs;

	if ( count >= BODY_MAX_CONTACTS )
		return count;

	c = &list[count];
	VectorCopy( r, c->r );
	VectorCopy( r2, c->r2 );
	VectorCopy( normal, c->normal );
	c->sep = sep;

	Body_SpeedAt( self, p, r, mine );
	Body_SpeedAt( other, q, r2, theirs );
	VectorSubtract( mine, theirs, mine );
	if ( Prop_Fixed( other, q, normal, -DotProduct( mine, normal ), sep, ratio ) ) {
		c->other = NULL;
		c->ratio = 0.0f;
	} else {
		c->other = other;
		c->ratio = ratio;
	}
	return count + 1;
}

/*
=================
Body_Body

Where this body touches another one. First the direction they are pushed apart along: of the directions the
faces of the two look into, the one along which they overlap least. Without an overlap along one of them they
do not touch. Then every spot of one that is in the other is a contact.
=================
*/
static int Body_Body( gentity_t *self, const prop_t *p, gentity_t *other, const prop_t *q, contact_t *list, int count ) {
	vec3_t		apart, normal, dir, back, r, r2, point;
	float		least = 0.0f, reachSelf, reachOther;
	qboolean	found = qfalse;
	int			n;

	VectorSubtract( self->r.currentOrigin, other->r.currentOrigin, apart );
	VectorClear( normal );
	for ( n = 0; n < q->shape.numPlanes + p->shape.numPlanes; n++ ) {
		const qboolean	theirs = ( n < q->shape.numPlanes ) ? qtrue : qfalse;
		const prop_t	*owner = theirs ? q : p, *facing = theirs ? p : q;
		const float		*plane = owner->shape.planes[theirs ? n : n - q->shape.numPlanes];
		float			gap;

		// how far the one this face looks at is in front of it
		Body_ToWorld( owner, plane, dir );
		VectorNegate( dir, back );
		gap = ( theirs ? 1.0f : -1.0f ) * DotProduct( apart, dir ) - Body_Reach( facing, back ) - plane[3];
		if ( gap > BODY_SKIN )
			return count;
		if ( !found || -gap < least ) {
			// from the other one to this one
			found = qtrue;
			least = -gap;
			VectorScale( dir, theirs ? 1.0f : -1.0f, normal );
		}
	}
	reachOther = Body_Reach( q, normal );
	VectorNegate( normal, back );
	reachSelf = Body_Reach( p, back );

	for ( n = 0; n < p->numSpots; n++ ) {
		// a spot of this body in the other one: how far it is under the other one along that direction
		Body_Spot( p, n, r );
		VectorAdd( self->r.currentOrigin, r, point );
		if ( Body_Holds( other, q, point ) ) {
			VectorSubtract( point, other->r.currentOrigin, r2 );
			count = Contact_Add( self, p, other, q, list, count, r, r2, normal, DotProduct( normal, r2 ) - reachOther );
		}
	}
	for ( n = 0; n < q->numSpots; n++ ) {
		// and a spot of the other one in this body
		Body_Spot( q, n, r2 );
		VectorAdd( other->r.currentOrigin, r2, point );
		if ( Body_Holds( self, p, point ) ) {
			VectorSubtract( point, self->r.currentOrigin, r );
			count = Contact_Add( self, p, other, q, list, count, r, r2, normal, -DotProduct( normal, r ) - reachSelf );
		}
	}
	return count;
}

// where this cube touches a ball: at its spot next to the middle of the ball
static int Cube_Ball( gentity_t *self, const prop_t *p, gentity_t *other, const prop_t *q, contact_t *list, int count ) {
	vec3_t	rel, r, r2, normal;
	float	dist;

	VectorSubtract( other->r.currentOrigin, self->r.currentOrigin, rel );
	dist = Body_Nearest( p, rel, r );
	if ( dist > other->radius + BODY_SKIN )
		return count;

	// from the ball to the cube
	VectorSubtract( r, rel, normal );
	if ( VectorNormalize( normal ) < 0.1f ) {
		// the middle of the ball is in the cube: the way their middles lie, or up
		VectorNegate( rel, normal );
		if ( VectorNormalize( normal ) < 0.1f )
			VectorSet( normal, 0, 0, 1 );
	}
	VectorScale( normal, other->radius, r2 );
	return Contact_Add( self, p, other, q, list, count, r, r2, normal, dist - other->radius );
}

/*
=================
Body_Contacts

Where the body touches the world and the other props.

The world: the engine only traces boxes that are not turned, so the spots of the body are looked at one by
one: its corners and the middles of its faces and edges. A line from the middle of the body through each spot
finds the surface the spot is in or close to.

Props: they are not in those traces (shots and players have to get through them), their shapes are known here.
=================
*/
static int Body_Contacts( gentity_t *self, const prop_t *p, contact_t *list ) {
	int count = 0, n;

	for ( n = 0; n < p->numSpots && count < BODY_MAX_CONTACTS; n++ ) {
		contact_t	*c = &list[count];
		vec3_t		spot;
		float		len;
		trace_t		tr;

		Body_Spot( p, n, c->r );
		len = VectorLength( c->r );
		VectorMA( self->r.currentOrigin, ( len + BODY_SKIN ) / len, c->r, spot );
		trap->Trace( &tr, self->r.currentOrigin, NULL, NULL, spot, self->s.number, MASK_SOLID, qfalse, 0, 0 );
		// the middle is inside something: Body_Step deals with that
		if ( tr.startsolid || tr.allsolid )
			return 0;
		if ( tr.fraction >= 1.0f || VectorLengthSquared( tr.plane.normal ) < 0.5f )
			continue;

		VectorCopy( tr.plane.normal, c->normal );
		VectorAdd( self->r.currentOrigin, c->r, spot );
		VectorSubtract( spot, tr.endpos, spot );
		c->sep = DotProduct( spot, c->normal );
		c->other = NULL;
		c->ratio = 0.0f;
		count++;
	}

	for ( n = 0; n < propCount && count < BODY_MAX_CONTACTS; n++ ) {
		gentity_t		*other = &g_entities[propList[n]];
		const prop_t	*q = &props[propList[n]];
		const float		reach = self->radius + propSeenRadius[n] + BODY_SKIN;

		if ( Prop_SeenDistanceSquared( self->r.currentOrigin, n ) > reach * reach )
			continue;
		if ( other == self || !Prop_Is( other ) )
			continue;
		count = q->box ? Body_Body( self, p, other, q, list, count ) : Cube_Ball( self, p, other, q, list, count );
	}
	return count;
}

// how fast the cube moves at a contact, seen from what it touches there
static void Contact_Speed( const gentity_t *self, const prop_t *p, const contact_t *c, vec3_t out ) {
	Body_SpeedAt( self, p, c->r, out );
	if ( c->other ) {
		vec3_t theirs;

		Body_SpeedAt( c->other, Prop_State( c->other ), c->r2, theirs );
		VectorSubtract( out, theirs, out );
	}
}

// an impulse at a contact: the cube gets it, a prop it touches there gets its part the other way
static void Contact_Push( gentity_t *self, prop_t *p, const contact_t *c, const vec3_t impulse ) {
	Body_Push( self, p, c->r, impulse );
	if ( c->other ) {
		vec3_t back;

		VectorScale( impulse, -c->ratio, back );
		Body_Push( c->other, Prop_State( c->other ), c->r2, back );
	}
}

// does it lie on something: three spots that are not in one line, on ground that carries
static qboolean Body_Carried( const prop_t *p, const contact_t *list, int count ) {
	const float		apart = 0.3f * Prop_MinHalf( p );
	const contact_t	*a = NULL, *b = NULL;
	float			widest = 0.0f;
	vec3_t			ab, ac, cross;
	int				i;

	// the first spot on the ground and the one furthest from it
	for ( i = 0; i < count; i++ ) {
		const contact_t	*c = &list[i];
		float			d;

		if ( c->normal[2] < BODY_GROUND || c->sep > 0.5f )
			continue;
		if ( !a ) {
			a = c;
			continue;
		}
		d = DistanceSquared( a->r, c->r );
		if ( d > widest ) {
			widest = d;
			b = c;
		}
	}
	if ( !b )
		return qfalse;

	// and a third one off their line
	VectorSubtract( b->r, a->r, ab );
	for ( i = 0; i < count; i++ ) {
		const contact_t *c = &list[i];

		if ( c->normal[2] < BODY_GROUND || c->sep > 0.5f )
			continue;
		VectorSubtract( c->r, a->r, ac );
		CrossProduct( ab, ac, cross );
		if ( VectorLengthSquared( cross ) > widest * apart * apart )
			return qtrue;
	}
	return qfalse;
}

/*
=================
Body_Step

One step of the rigid body. Gravity first. Then the speeds are put right by impulses, a few rounds until they
agree: the hand pulls the spot it holds along, and every spot at a surface loses its speed into the surface
(or bounces off it) and is rubbed by it, no harder than the grip times what presses it on. Where the surface
is another prop that can move, that one gets the same impulse the other way, by its mass. Then the body goes
its way and turns.

Returns qfalse when it went home or is inside something, the frame ends there.
=================
*/
static qboolean Body_Step( gentity_t *self, prop_t *p, float dt, const vec3_t hand, const vec3_t handVel,
		contact_t *contacts, int *numContacts ) {
	float		*vel = self->movedir;
	const int	count = p->box ? Body_Contacts( self, p, contacts ) : 0;
	const float	stiff = p->gun ? PHYSGUN_STIFF : GRAB_STIFF / sqrtf( self->mass );
	const float	fastest = p->gun ? PHYSGUN_SPEED : GRAB_MAX_SPEED;
	vec3_t		grab, corr, end, mins, maxs;
	float		loud = 0.0f, len;
	trace_t		tr;
	int			i, round;

	*numContacts = count;

	vel[2] -= g_gravity.value * dt;
	// a little drag in the air, more on the turning where it touches or hangs
	VectorScale( vel, 1.0f - 0.05f * dt, vel );
	VectorScale( p->spin, 1.0f - ( count ? 1.5f : p->holder ? 0.6f : 0.2f ) * dt, p->spin );
	// in the beam of the physgun it does not turn by itself, only as the mouse of its holder says
	if ( p->gun )
		VectorCopy( p->gunSpin, p->spin );

	for ( i = 0; i < count; i++ ) {
		contact_t	*c = &contacts[i];
		vec3_t		speed;
		float		into;

		c->kn = Body_Give( p, c->r, c->normal );
		if ( c->other )
			c->kn += c->ratio * Body_Give( Prop_State( c->other ), c->r2, c->normal );
		c->jn = 0.0f;
		VectorClear( c->jt );

		// a spot over the surface may close the gap, one in it has to stop, a fast one bounces
		Contact_Speed( self, p, c, speed );
		into = -DotProduct( speed, c->normal );
		c->want = ( c->sep > 0.0f ) ? -c->sep / dt : 0.0f;
		if ( into > PROP_MIN_BOUNCE && into * self->physicsBounce > c->want )
			c->want = into * self->physicsBounce;
		if ( into > loud )
			loud = into;
	}

	VectorClear( grab );
	if ( p->holder )
		Body_ToWorld( p, p->grab, grab );

	for ( round = 0; round < BODY_ROUNDS; round++ ) {
		if ( p->holder ) {
			// the spot he holds goes to his hand and with it. The impulse that gives a spot of a body with
			// inertia I a change of speed d is ( d + r (r.d) / I ) / ( 1 + r.r / I ).
			vec3_t want, speed;

			VectorAdd( self->r.currentOrigin, grab, want );
			VectorSubtract( hand, want, want );
			VectorScale( want, stiff, want );
			VectorAdd( want, handVel, want );
			len = VectorLength( want );
			if ( len > fastest )
				VectorScale( want, fastest / len, want );

			Body_SpeedAt( self, p, grab, speed );
			VectorSubtract( want, speed, want );
			VectorMA( want, DotProduct( grab, want ) / p->inertia, grab, want );
			VectorScale( want, 1.0f / ( 1.0f + VectorLengthSquared( grab ) / p->inertia ), want );
			Body_Push( self, p, grab, want );
		}

		for ( i = 0; i < count; i++ ) {
			contact_t	*c = &contacts[i];
			vec3_t		speed, slide, total;
			float		old, limit, give;

			// along the normal: it only ever pushes, never holds on
			Contact_Speed( self, p, c, speed );
			old = c->jn;
			c->jn = old + ( c->want - DotProduct( speed, c->normal ) ) / c->kn;
			if ( c->jn < 0.0f )
				c->jn = 0.0f;
			VectorScale( c->normal, c->jn - old, total );
			Contact_Push( self, p, c, total );

			// friction: against the speed along the surface
			Contact_Speed( self, p, c, speed );
			VectorMA( speed, -DotProduct( speed, c->normal ), c->normal, slide );
			len = VectorNormalize( slide );
			if ( len < 0.01f )
				continue;
			give = Body_Give( p, c->r, slide );
			if ( c->other )
				give += c->ratio * Body_Give( Prop_State( c->other ), c->r2, slide );
			VectorMA( c->jt, -len / give, slide, total );
			limit = self->random * c->jn;
			len = VectorLength( total );
			if ( len > limit )
				VectorScale( total, limit / len, total );
			VectorSubtract( total, c->jt, slide );
			VectorCopy( total, c->jt );
			Contact_Push( self, p, c, slide );
		}
	}

	if ( loud > PROP_LOUD )
		Prop_BounceSound( self );

	if ( !Body_Sane( self, p ) ) {
		AnglesToAxis( vec3_origin, p->axis );
		Prop_Home( self );
		return qfalse;
	}

	// out of what it is in: along the normals, every surface only as far as the ones before left it in. Out of
	// a prop that can move it only goes its part of the way, the lighter one more, the other goes the rest itself.
	VectorClear( corr );
	for ( i = 0; i < count; i++ ) {
		const contact_t	*c = &contacts[i];
		const float		part = c->other ? 1.0f / ( 1.0f + c->ratio ) : 1.0f;
		const float		need = ( -c->sep - BODY_SLOP ) * 0.8f * part - DotProduct( corr, c->normal );

		if ( need > 0.0f )
			VectorMA( corr, need, c->normal, corr );
	}
	len = VectorLength( corr );
	if ( len > 8.0f )
		VectorScale( corr, 8.0f / len, corr );

	VectorMA( self->r.currentOrigin, dt, vel, end );
	VectorAdd( end, corr, end );

	// the way of the middle. The spots above can miss something thin, so a box in the middle of the cube is
	// traced that fits into it however it is turned. A ball in a hand has no spots, all of it is traced.
	if ( p->box ) {
		const float core = BODY_CORE * Prop_MinHalf( p );

		VectorSet( mins, -core, -core, -core );
		VectorSet( maxs, core, core, core );
	} else {
		VectorCopy( self->r.mins, mins );
		VectorCopy( self->r.maxs, maxs );
	}
	trap->Trace( &tr, self->r.currentOrigin, mins, maxs, end, self->s.number, MASK_SOLID, qfalse, 0, 0 );
	if ( tr.startsolid || tr.allsolid ) {
		// a door closed on it or it was put into a wall: up, and home if that does not help
		if ( ++self->genericValue6 > PROP_STUCK_FRAMES ) {
			Prop_Home( self );
			return qfalse;
		}
		self->r.currentOrigin[2] += 2.0f;
		VectorClear( vel );
		VectorClear( p->spin );
		return qfalse;
	}
	self->genericValue6 = 0;

	VectorCopy( tr.endpos, self->r.currentOrigin );
	if ( tr.fraction < 1.0f ) {
		const float into = -DotProduct( vel, tr.plane.normal );

		if ( into > 0.0f ) {
			const float bounce = ( into > PROP_MIN_BOUNCE ) ? self->physicsBounce : 0.0f;

			VectorMA( vel, ( 1.0f + bounce ) * into, tr.plane.normal, vel );
			if ( into > PROP_LOUD )
				Prop_BounceSound( self );
		}
	}

	Body_Turn( p, dt );
	return qtrue;
}

// Has it left the place it was last seen at? Then its place is where it is now, and if it lay still at the old
// one, what lies still next to that is woken.
static qboolean Body_Left( gentity_t *self, prop_t *p ) {
	if ( p->restTime && DistanceSquared( self->r.currentOrigin, p->restAt ) <= BODY_REST_MOVE * BODY_REST_MOVE
		&& DistanceSquared( p->axis[0], p->restAxis[0] ) <= BODY_REST_TURN * BODY_REST_TURN
		&& DistanceSquared( p->axis[1], p->restAxis[1] ) <= BODY_REST_TURN * BODY_REST_TURN )
		return qfalse;

	if ( p->leaves ) {
		p->leaves = qfalse;
		Prop_WakeAround( self, p->restAt );
	}
	p->restTime = level.time;
	VectorCopy( self->r.currentOrigin, p->restAt );
	VectorCopy( p->axis[0], p->restAxis[0] );
	VectorCopy( p->axis[1], p->restAxis[1] );
	return qtrue;
}

// It lies still from now on. What it touches is looked at once more, where it stands now and the way
// Prop_OnGround will: how deep it is in something, and for a wedged one how many of its spots touch (none: it
// does not lie still).
static void Body_LieStill( gentity_t *self, prop_t *p, contact_t *contacts, qboolean wedged ) {
	const int	count = Body_Contacts( self, p, contacts );
	int			i;

	if ( wedged && !count )
		return;
	p->restContacts = wedged ? count : 0;
	p->restSep = 0.0f;
	for ( i = 0; i < count; i++ ) {
		if ( contacts[i].sep < p->restSep )
			p->restSep = contacts[i].sep;
	}
	VectorClear( self->movedir );
	VectorClear( p->spin );
	self->genericValue2 = 1;
	self->genericValue5 = level.time + 500;
}

/*
=================
Body_Move

A server frame of the rigid body, in steps short enough for the contacts to hold. Then: does the hand still
reach it, did it fall out of the world, has the cube come to rest.
=================
*/
static void Body_Move( gentity_t *self, prop_t *p, float dt ) {
	contact_t	contacts[BODY_MAX_CONTACTS];
	vec3_t		hand, handVel, way;
	int			steps, count = 0, i;
	float		len;
	qboolean	left;

	if ( dt < 0.001f )
		return;
	steps = Com_Clampi( 1, BODY_MAX_STEPS, (int)ceilf( dt / BODY_STEP ) );

	VectorCopy( p->hand, hand );
	VectorClear( handVel );
	VectorClear( way );
	if ( p->holder ) {
		const float fastest = p->gun ? PHYSGUN_SPEED : GRAB_MAX_SPEED;

		Prop_Thrown( p, p->holder );
		// where his hand is now and how fast it got there
		if ( p->gun ) {
			const int	n = p->holder->s.number;
			const float	turn = Prop_GunTurn( p->holder, p );
			vec3_t		right;

			Prop_GunHand( p->holder, p, hand );
			// It turns with him when he turns. And what his mouse did since the last frame while he held use
			// turns it: left and right about the vertical, up and down about the line through his shoulders, the
			// way his view would have turned.
			AngleVectors( p->holder->client->ps.viewangles, NULL, right, NULL );
			VectorSet( p->gunSpin, 0.0f, 0.0f, DEG2RAD( gunTurn[n][YAW] + turn ) / dt );
			VectorMA( p->gunSpin, -DEG2RAD( gunTurn[n][PITCH] ) / dt, right, p->gunSpin );
			gunTurn[n][PITCH] = gunTurn[n][YAW] = 0.0f;
		} else {
			Prop_Hand( p->holder, hand );
		}
		VectorSubtract( hand, p->hand, way );
		VectorScale( way, 1.0f / dt, handVel );
		len = VectorLength( handVel );
		if ( len > fastest )
			VectorScale( handVel, fastest / len, handVel );
	}

	for ( i = 1; i <= steps; i++ ) {
		vec3_t now;

		// the hand on its way through the frame
		VectorMA( p->hand, (float)i / steps, way, now );
		if ( !Body_Step( self, p, dt / steps, now, handVel, contacts, &count ) )
			break;
	}
	left = Body_Left( self, p );

	// fell out of the world or into a pit nothing comes back from
	if ( self->r.currentOrigin[2] < self->pos1[2] - 8192.0f
		|| ( trap->PointContents( self->r.currentOrigin, self->s.number ) & CONTENTS_NODROP ) ) {
		Prop_Home( self );
		return;
	}

	if ( p->holder ) {
		vec3_t spot;

		VectorCopy( hand, p->hand );
		// caught on something or he was taken away from it: it is let go
		Body_ToWorld( p, p->grab, spot );
		VectorAdd( spot, self->r.currentOrigin, spot );
		// (the beam of the physgun does not let go)
		if ( !p->gun && Distance( spot, hand ) > GRAB_BREAK )
			Prop_Release( self );
		return;
	}

	// a ball goes on by itself (Ball_Move), and a frame that ended early says nothing about rest
	if ( !p->box || i <= steps )
		return;

	if ( VectorLength( self->movedir ) < BODY_CALM_SPEED && VectorLength( p->spin ) < BODY_CALM_SPIN
		&& Body_Carried( p, contacts, count ) ) {
		if ( !p->calmTime )
			p->calmTime = level.time;
		else if ( level.time - p->calmTime >= BODY_CALM_MSEC )
			Body_LieStill( self, p, contacts, qfalse );
	} else {
		p->calmTime = 0;
	}

	// A body that leans between a floor and a wall or in a pile is not carried by three spots and went on thinking
	// for good, 26 traces a step. It lies still once it has not left its place for a while.
	if ( !left && !self->genericValue2 && level.time - p->restTime >= BODY_REST_MSEC )
		Body_LieStill( self, p, contacts, qtrue );
}

// while it lies still: is what it lies on still there
static qboolean Prop_OnGround( gentity_t *self, const prop_t *p ) {
	vec3_t	below;
	trace_t	tr;

	if ( p->box ) {
		contact_t	contacts[BODY_MAX_CONTACTS];
		const int	count = Body_Contacts( self, p, contacts );
		int			i;

		// something that moves pushed into it: it is in deeper than it lay when it came to rest
		for ( i = 0; i < count; i++ ) {
			if ( contacts[i].sep < -2.0f && contacts[i].sep < p->restSep - BODY_REST_DEEPER )
				return qfalse;
		}
		if ( Body_Carried( p, contacts, count ) )
			return qtrue;
		// wedged: nothing it leaned on is gone
		return ( p->restContacts && count >= p->restContacts ) ? qtrue : qfalse;
	}

	VectorCopy( self->r.currentOrigin, below );
	below[2] -= 2.0f;
	trap->Trace( &tr, self->r.currentOrigin, self->r.mins, self->r.maxs, below, self->s.number, MASK_SOLID, qfalse, 0, 0 );
	return ( tr.fraction < 1.0f && !tr.startsolid ) ? qtrue : qfalse;
}

/*
===============================================================================

Think, damage, force powers

===============================================================================
*/

static void Prop_Think( gentity_t *self );

static void prop_think( gentity_t *self ) {
	Prop_Think( self );
	// where it is now, for the props that think after it
	Prop_Seen( self );
}

static void Prop_Think( gentity_t *self ) {
	prop_t		*p = Prop_State( self );
	// the time since the last frame, capped for a server that hitched
	const float	dt = Com_Clamp( 0.0f, 0.1f, ( level.time - self->genericValue1 ) * 0.001f );

	self->genericValue1 = level.time;
	self->nextthink = level.time + 1;		// every server frame

	// he let go of the button, died or left
	if ( p->holder && !Prop_Held( p ) )
		Prop_Release( self );

	// the physgun froze it: it stays as it is, in the way of players and of other props like a piece of the map
	if ( p->frozen ) {
		VectorClear( self->movedir );
		VectorClear( p->spin );
		self->genericValue2 = 1;
		return;
	}

	if ( p->box )
		Cube_Players( self, p, dt );
	else
		Ball_Players( self, p, dt );

	if ( self->genericValue2 ) {
		// it lies still. Now and then: is the ground still there?
		if ( level.time >= self->genericValue5 ) {
			self->genericValue5 = level.time + 500;
			if ( !Prop_OnGround( self, p ) )
				Prop_Wake( self );
		}
		if ( self->genericValue2 )
			return;
	}

	if ( p->box || p->holder ) {
		Body_Move( self, p, dt );
		if ( !p->box )
			Ball_Props( self, dt );
		Body_Net( self, p );
	} else {
		Ball_Move( self, dt );
		Ball_Props( self, dt );
		Ball_Net( self );
	}
}

/*
=================
prop_pain, prop_die

A shot, a saber or an explosion: away from where it was hit, the more damage the faster. A hit on the cube
itself pushes along the shot at the spot it hit, which makes it spin when that spot is off its middle.
=================
*/
static void prop_pain( gentity_t *self, gentity_t *attacker, int damage ) {
	prop_t	*p = Prop_State( self );
	vec3_t	at, dir, spot;

	self->health = PROP_HEALTH;
	Prop_Thrown( p, attacker );

	VectorSubtract( gPainPoint, self->r.currentOrigin, at );
	VectorNegate( at, dir );
	if ( VectorNormalize( dir ) < 1.0f ) {
		// splash damage comes without a point: away from who did it
		if ( attacker )
			VectorSubtract( self->r.currentOrigin, attacker->r.currentOrigin, dir );
		if ( !attacker || VectorNormalize( dir ) < 1.0f )
			VectorSet( dir, 0, 0, 1 );
		VectorClear( at );
	} else if ( p->box && Body_Nearest( p, at, spot ) < 0.5f * Prop_MinHalf( p ) ) {
		VectorCopy( spot, at );
		if ( attacker ) {
			vec3_t from, shot;

			VectorCopy( attacker->r.currentOrigin, from );
			if ( attacker->client )
				from[2] += attacker->client->ps.viewheight;
			VectorSubtract( gPainPoint, from, shot );
			if ( VectorNormalize( shot ) > 1.0f )
				VectorCopy( shot, dir );
		}
	} else {
		// a ball, or an explosion next to the cube: through the middle
		VectorClear( at );
	}
	Prop_Impulse( self, dir, Com_Clamp( 40.0f, 900.0f, damage * 9.0f ), at );
}

static void prop_die( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod ) {
	prop_pain( self, attacker, damage );
	self->takedamage = qtrue;
}

// a use (targetname) brings it home
static void prop_use( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	Prop_Home( self );
}

/*
=================
G_PropForceThrow

From ForceThrow: every ball and cube in front of the player and in his sight is pushed along his view or
pulled towards him. Reach and strength grow with the level of the power. What he holds leaves his hand, so a
push throws it.
=================
*/
void G_PropForceThrow( gentity_t *self, qboolean pull ) {
	const int	power = self->client->ps.fd.forcePowerLevel[pull ? FP_PULL : FP_PUSH];
	const float	reach = 256.0f + 192.0f * power;
	vec3_t		eye, fwd;
	int			i;

	VectorCopy( self->client->ps.origin, eye );
	eye[2] += self->client->ps.viewheight;
	AngleVectors( self->client->ps.viewangles, fwd, NULL, NULL );

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t	*prop = &g_entities[i];
		prop_t		*p = &props[i];
		vec3_t		dir, at;
		float		dist;
		trace_t		tr;

		if ( !Prop_Is( prop ) )
			continue;

		VectorSubtract( prop->r.currentOrigin, eye, dir );
		dist = VectorNormalize( dir );
		if ( dist > reach || DotProduct( dir, fwd ) < 0.7f )
			continue;
		trap->Trace( &tr, eye, NULL, NULL, prop->r.currentOrigin, self-g_entities, MASK_SOLID, qfalse, 0, 0 );
		if ( tr.fraction < 1.0f )
			continue;

		if ( p->holder == self )
			Prop_Release( prop );
		Prop_Thrown( p, self );

		// the cube is taken a bit above its middle, that makes it tumble
		VectorSet( at, 0, 0, p->box ? 0.4f * Prop_MinHalf( p ) : 0.0f );
		if ( pull ) {
			VectorNegate( dir, dir );
			Prop_Impulse( prop, dir, 300.0f + 120.0f * power, at );
		} else {
			Prop_Impulse( prop, fwd, 350.0f + 150.0f * power, at );
		}
		prop->movedir[2] += 80.0f / prop->mass;
	}
}

/*
=================
G_PropFlipKick

For ClientThink_real: this player flipped off a prop, the code of the moves said so in forceKickFlip
(PROP_KICK_FLIP). He pushed himself off it, so it lies where he comes from: back along the way he flies now,
which is behind him after a flip to the side and in front of him after a flip backwards. The body that is
closest there gets his kick at the spot next to him, that way and a bit up: off its middle that turns it, a
kick near the top of a crate knocks it over.
=================
*/
void G_PropFlipKick( gentity_t *ent ) {
	const playerState_t	*ps = &ent->client->ps;
	gentity_t			*best = NULL;
	vec3_t				angles, away, at, dir;
	float				bestDist = PROP_KICK_REACH;
	int					i;

	VectorSet( away, -ps->velocity[0], -ps->velocity[1], 0.0f );
	if ( VectorNormalize( away ) < 1.0f ) {
		// he does not move sideways at all: where he looks then
		VectorSet( angles, 0.0f, ps->viewangles[YAW], 0.0f );
		AngleVectors( angles, away, NULL, NULL );
	}
	VectorClear( at );

	for ( i = 0; i < propCount; i++ ) {
		gentity_t		*prop = &g_entities[propList[i]];
		const prop_t	*p = &props[propList[i]];
		vec3_t			rel, r;
		float			dist;

		if ( !Prop_Is( prop ) || !p->solid || p->holder == ent )
			continue;
		VectorSubtract( ps->origin, prop->r.currentOrigin, rel );
		if ( VectorLength( rel ) > prop->radius + bestDist )
			continue;
		dist = Body_Nearest( p, rel, r );
		// from him to that spot: it has to lie on the side he pushed off
		VectorSubtract( r, rel, dir );
		if ( dist >= bestDist || DotProduct( dir, away ) <= 0.0f )
			continue;
		best = prop;
		bestDist = dist;
		VectorCopy( r, at );
	}
	if ( !best )
		return;

	VectorCopy( away, dir );
	dir[2] = 0.25f;
	VectorNormalize( dir );
	Prop_Thrown( Prop_State( best ), ent );
	Prop_Impulse( best, dir, PROP_KICK_SPEED, at );
	G_Sound( best, CHAN_AUTO, G_SoundIndex( va( "sound/weapons/melee/punch%d", Q_irand( 1, 4 ) ) ) );
}

// for the code that pulls things in or pushes them about and is not in this file: is this one of the props
qboolean G_PropIs( const gentity_t *ent ) {
	return Prop_Is( ent );
}

/*
=================
G_PropPull

For what pulls things in (misc_blackhole): a prop gains speed along dir, accel units a second every second,
whatever its mass, as with gravity. One that lies still is only woken by a pull of half the gravity or more: a
weaker one does not move it off the ground, and everything in reach of a hole would be awake for good.
=================
*/
void G_PropPull( gentity_t *ent, const vec3_t dir, float accel, float dt ) {
	if ( Prop_State( ent )->frozen )
		return;
	if ( ent->genericValue2 ) {
		if ( accel < 0.5f * g_gravity.value )
			return;
		Prop_Wake( ent );
	}
	VectorMA( ent->movedir, accel * dt, dir, ent->movedir );
}

/*
=================
Svcmd_Props_f

props: lists the props of the map with what the physics knows of them. props <number>: what that body touches.
=================
*/
void Svcmd_Props_f( void ) {
	char	arg[16];
	int		i, m, count = 0;

	// with the number of a body: what it touches now
	trap->Argv( 1, arg, sizeof( arg ) );
	i = atoi( arg );
	if ( arg[0] && i >= MAX_CLIENTS && i < level.num_entities && Prop_Is( &g_entities[i] ) && props[i].box ) {
		contact_t		contacts[BODY_MAX_CONTACTS];
		gentity_t		*ent = &g_entities[i];
		const prop_t	*p = &props[i];
		vec3_t			angles;

		count = Body_Contacts( ent, p, contacts );
		Body_Angles( p, angles );
		trap->Print( "%i: angles %.1f %.1f %.1f, spin %.2f %.2f %.2f, speed %.1f %.1f %.1f, %i contacts, carried %i\n", i, angles[0], angles[1],
			angles[2], p->spin[0], p->spin[1], p->spin[2], ent->movedir[0], ent->movedir[1], ent->movedir[2], count,
			Body_Carried( p, contacts, count ) );
		for ( m = 0; m < count; m++ ) {
			const contact_t *c = &contacts[m];

			trap->Print( "  at %6.1f %6.1f %6.1f  normal %5.2f %5.2f %5.2f  sep %6.2f  %s\n", c->r[0], c->r[1], c->r[2], c->normal[0],
				c->normal[1], c->normal[2], c->sep, c->other ? va( "prop %i", c->other->s.number ) : "world or a prop that lies still" );
		}
		return;
	}

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		const gentity_t	*ent = &g_entities[i];
		const prop_t	*p = &props[i];
		float			lowest = ent->r.currentOrigin[2] - ent->radius, highest = ent->r.currentOrigin[2] + ent->radius;
		char			shape[64];

		if ( !Prop_Is( ent ) )
			continue;
		if ( p->box ) {
			// its lowest corner: on a floor that is where the floor is
			for ( m = 0; m < p->shape.numVerts; m++ ) {
				vec3_t corner;

				Body_ToWorld( p, p->shape.verts[m], corner );
				if ( !m || ent->r.currentOrigin[2] + corner[2] < lowest )
					lowest = ent->r.currentOrigin[2] + corner[2];
				if ( !m || ent->r.currentOrigin[2] + corner[2] > highest )
					highest = ent->r.currentOrigin[2] + corner[2];
			}
			Com_sprintf( shape, sizeof( shape ), "%i corners %i faces %i edges %i spots", p->shape.numVerts, p->shape.numPlanes,
				p->shape.numEdges, p->numSpots );
		} else {
			Com_sprintf( shape, sizeof( shape ), "ball of %.1f", ent->radius );
		}
		trap->Print( "%4i %s: %s, mass %.1f, at %.1f %.1f %.1f, lowest %.2f, highest %.2f, speed %.0f%s%s\n", i, ent->classname, shape,
			ent->mass, ent->r.currentOrigin[0], ent->r.currentOrigin[1], ent->r.currentOrigin[2], lowest, highest, VectorLength( ent->movedir ),
			ent->genericValue2 ? ", lies still" : "", p->holder ? ", in a hand" : "" );
		count++;
	}
	trap->Print( "%i props\n", count );
}

/*
===============================================================================

Spawning

===============================================================================
*/

// what the ball and the cube share when they spawn. qfalse: it can not be shown and is gone.
static qboolean Prop_Spawn( gentity_t *ent, const char *model, int scale, float bound, const char *sound ) {
	prop_t	*p = Prop_State( ent );
	int		k;

	// without room for the model in the table of the map there is nothing to show
	ent->s.modelindex = G_TryConfigstringIndex( model, CS_MODELS, MAX_MODELS );
	if ( !ent->s.modelindex ) {
		Com_Printf( S_COLOR_YELLOW "%s: no room for the model %s\n", ent->classname, model );
		G_FreeEntity( ent );
		return qfalse;
	}

	ent->physicsBounce = Com_Clamp( 0.0f, 0.95f, ent->physicsBounce );
	ent->mass = Com_Clamp( 0.1f, 100.0f, ent->mass );
	ent->speed = Com_Clamp( 0.0f, 10.0f, ent->speed );

	ent->s.eType = ET_GENERAL;
	ent->s.iModelScale = Com_Clampi( 1, 1023, scale );
	VectorSet( ent->r.mins, -bound, -bound, -bound );
	VectorSet( ent->r.maxs, bound, bound, bound );
	ent->r.contents = CONTENTS_CORPSE;		// shots hit it, players walk through
	ent->clipmask = MASK_SOLID;

	ent->takedamage = qtrue;
	ent->health = PROP_HEALTH;
	ent->pain = prop_pain;
	ent->die = prop_die;
	ent->use = prop_use;

	G_SetOrigin( ent, ent->s.origin );
	VectorCopy( ent->r.currentOrigin, ent->pos1 );
	VectorClear( ent->movedir );
	AnglesToAxis( ent->s.angles, p->axis );

	// the planes that are kept for the moves of players are those of what had this number before
	for ( k = 0; k < PROP_BRUSHES; k++ ) {
		if ( propSolids[k].number == ent->s.number )
			propSolids[k].number = 0;
	}

	ent->genericValue1 = level.time;
	ent->genericValue2 = 0;
	ent->genericValue3 = G_TryConfigstringIndex( sound, CS_SOUNDS, MAX_SOUNDS );
	ent->genericValue4 = 0;
	ent->genericValue6 = 0;

	ent->think = prop_think;
	ent->nextthink = level.time + 1;

	if ( p->box ) {
		Body_Net( ent, p );
	} else {
		VectorClear( ent->s.apos.trBase );
		VectorClear( ent->s.apos.trDelta );
		ent->s.apos.trType = TR_STATIONARY;
		trap->LinkEntity( (sharedEntity_t *)ent );
	}
	return qtrue;
}

/*QUAKED misc_ball (0.9 0.6 0.1) (-18 -18 -18) (18 18 18)
A ball. It falls, bounces and rolls, players kick it by running into it, force push and pull throw it, shots,
sabers and explosions knock it away. It bounces off other balls and off cubes. Players are not blocked by it and
it hurts who it hits fast. A
player who looks at it takes it with
+button13 and holds it until he lets go of the button. Using it (targetname) brings it back to where it was
spawned, so does falling out of the world or getting stuck.

"model"			default models/items/remote.md3, the ball of the training remote
"modelscale"	size of the model in percent, default 600 (that model is tiny)
"radius"		size of the ball for the physics, default 18. Should fit the scaled model
"bounce"		part of its speed it keeps when it bounces, 0 to 0.95, default 0.7
"friction"		how fast it slows down when it rolls, units per second squared, default 90
"mass"			the heavier, the less a kick, a push or a hit moves it, default 1
"kick"			how hard players kick it, default 1
*/
void SP_misc_ball( gentity_t *ent ) {
	prop_t	*p = Prop_State( ent );
	char	*model;
	int		scale;

	G_SpawnString( "model", "models/items/remote.md3", &model );
	G_SpawnInt( "modelscale", "600", &scale );
	G_SpawnFloat( "radius", "18", &ent->radius );
	G_SpawnFloat( "bounce", "0.7", &ent->physicsBounce );
	G_SpawnFloat( "friction", "90", &ent->random );
	G_SpawnFloat( "mass", "1", &ent->mass );
	G_SpawnFloat( "kick", "1", &ent->speed );

	ent->radius = Com_Clamp( 4.0f, 128.0f, ent->radius );
	ent->random = Com_Clamp( 0.0f, 2000.0f, ent->random );

	memset( p, 0, sizeof( *p ) );
	p->inertia = 0.4f * ent->radius * ent->radius;		// a solid ball

	Prop_Spawn( ent, model, scale, ent->radius, "sound/weapons/thermal/bounce1.mp3" );
}

/*
=================
Prop_SpawnBody

A rigid body with the shape that is in its state already (the caller cleared the state and made the shape), for
a model at a scale in percent. What misc_cube and misc_prop share. qfalse: it can not be shown and is gone.
=================
*/
static qboolean Prop_SpawnBody( gentity_t *ent, const char *model, int scale ) {
	prop_t				*p = Prop_State( ent );
	const propShape_t	*shape = &p->shape;
	vec3_t				half;
	int					n, m;

	p->box = qtrue;

	// The spots it is asked about: its corners, then the middles of its faces, then those of its edges. A world
	// that has edges of its own gets into a body between its corners.
	for ( n = 0; n < shape->numVerts && p->numSpots < BODY_MAX_SPOTS; n++ )
		VectorCopy( shape->verts[n], p->spots[p->numSpots++] );
	for ( n = 0; n < shape->numPlanes && p->numSpots < BODY_MAX_SPOTS; n++ ) {
		float *spot = p->spots[p->numSpots++];

		VectorClear( spot );
		for ( m = 0; m < shape->loopCount[n]; m++ )
			VectorAdd( spot, shape->verts[shape->loop[shape->loopStart[n] + m]], spot );
		VectorScale( spot, 1.0f / shape->loopCount[n], spot );
	}
	for ( n = 0; n < shape->numEdges && p->numSpots < BODY_MAX_SPOTS; n++ ) {
		float *spot = p->spots[p->numSpots++];

		VectorAdd( shape->verts[shape->edges[n][0]], shape->verts[shape->edges[n][1]], spot );
		VectorScale( spot, 0.5f, spot );
	}

	// A solid box of the size of the bounds, with the mean of its three moments so one number does for every
	// axis. For a cube that is exact.
	VectorSubtract( shape->maxs, shape->mins, half );
	VectorScale( half, 0.5f, half );
	p->inertia = ( 2.0f / 9.0f ) * VectorLengthSquared( half );
	ent->radius = shape->radius;

	// Body_Net() sets the box shots are traced against, this one is only there until it has
	return Prop_Spawn( ent, model, scale, shape->radius, "sound/effects/metal_bounce.mp3" );
}

/*QUAKED misc_cube (0.9 0.6 0.1) (-16 -16 -16) (16 16 16)
A cube with corners and edges. It falls, lands on a corner or an edge, tips over, tumbles and slides to a stop
on one of its faces. It is heavy: players shove it slowly instead of kicking it away, force push and pull make
it tumble, shots turn it when they hit off its middle. Cubes push each other and can be stacked, balls bounce
off them. It is in the way of players, they stand and walk on it. A player who looks at it
takes it with +button13 at the corner next to his line of sight, it hangs from there and swings until he lets
go of the button. Using it (targetname) brings it back to where it was spawned, so does falling out of the
world or getting stuck.

"model"			default models/map_objects/cairn/cargo_sm.md3, a small cargo crate. The box of the physics is
				taken from the bounds of the md3, so other models work too, cubes or not
"size"			length of its longest edge, default 32. The model is scaled to it
"bounce"		part of its speed it keeps when it hits something, 0 to 0.95, default 0.25
"friction"		how well it grips the ground, 0 to 2, default 0.7: how far it slides, how steep it stays put
"mass"			the heavier, the less a shove, a push or a hit moves it and the more it slows who shoves it,
				default 4
"kick"			how hard players shove it, default 1
"angles"		how it is turned when it spawns
*/
void SP_misc_cube( gentity_t *ent ) {
	prop_t		*p = Prop_State( ent );
	propCloud_t	cloud;
	char		*model;
	vec3_t		mins, maxs;
	float		size, longest = 0.0f;
	int			scale = 100, solid = 0, k;

	G_SpawnString( "model", "models/map_objects/cairn/cargo_sm.md3", &model );
	G_SpawnFloat( "size", "32", &size );
	G_SpawnFloat( "bounce", "0.25", &ent->physicsBounce );
	G_SpawnFloat( "friction", "0.7", &ent->random );
	G_SpawnFloat( "mass", "4", &ent->mass );
	G_SpawnFloat( "kick", "1", &ent->speed );

	size = Com_Clamp( 8.0f, 256.0f, size );
	ent->random = Com_Clamp( 0.0f, 2.0f, ent->random );

	BG_PropCloudClear( &cloud );
	if ( BG_PropCloudFromModel( model, &cloud ) ) {
		VectorCopy( cloud.mins, mins );
		VectorCopy( cloud.maxs, maxs );
		for ( k = 0; k < 3; k++ ) {
			if ( maxs[k] - mins[k] > longest )
				longest = maxs[k] - mins[k];
		}
		scale = Com_Clampi( 1, 1023, (int)( size / longest * 100.0f + 0.5f ) );
		// players stand on it. Its box is made the way the client makes it for that.
		solid = PROP_SOLID_BOX;
	} else {
		// not an md3 or none the server finds: a cube of that size around the origin of the model as it is
		Com_Printf( S_COLOR_YELLOW "misc_cube: no bounds in %s, taking a cube of %g\n", model, size );
		VectorSet( mins, -0.5f * size, -0.5f * size, -0.5f * size );
		VectorSet( maxs, 0.5f * size, 0.5f * size, 0.5f * size );
	}

	memset( p, 0, sizeof( *p ) );
	p->solid = solid;
	BG_PropShapeBox( mins, maxs, scale / 100.0f, &p->shape );
	BG_PropCloudFree( &cloud );
	Prop_SpawnBody( ent, model, scale );
}

/*QUAKED misc_prop (0.9 0.6 0.1) (-16 -16 -16) (16 16 16)
Any model of the game as a thing with physics. Its shape is taken from the model itself: the hull around it, a
body with up to 26 corners that lies on the faces the model has, rolls over the edges it has and is caught by
what sticks out of it. A hull has no holes: what is hollow in the model is filled. Props collide with each
other, with balls and with cubes, each by its mass, they can be pushed, stacked, thrown with force push and
pull and taken with +button13.

"model"			an md3 or a glm (a Ghoul2 model stands as it is in its file, without an animation)
"scale"			size of the model in percent, default 100. It is changed if the longest edge of the box around
				it would be under 8 or over 384 units
"shape"			"model" (default): the hull of the model. "box": the box around the model, edge by edge. "ball"
				for something round: a ball as wide as the model that rolls, its middle is the origin of the
				model, so that fits models that are built around it. A model that is flat or thinner than 4
				units somewhere gets the box.
"mass"			the heavier, the less a shove, a push or a hit moves it. Default 0: from its size. A box of 32
				units has 4 like the cube, and the mass grows with the surface of the box around the model, not
				with its volume: models are shells and not solid blocks, and a big one stays something a player
				can move
"bounce"		part of its speed it keeps when it hits something, 0 to 0.95, default 0.25 (ball 0.7)
"friction"		how well it grips the ground, 0 to 2, default 0.7. Ball: how fast it slows down when it rolls,
				units per second squared, default 90
"kick"			how hard players shove it, default 1
"angles"		how it is turned when it spawns
The origin is where its middle is to be: the centre of mass of the hull or the box.
*/
void SP_misc_prop( gentity_t *ent ) {
	prop_t		*p = Prop_State( ent );
	propCloud_t	cloud;
	char		*model, *shape;
	float		longest = 0.0f, volume = 8.0f, fscale;
	qboolean	glm, ball;
	int			scale, k;

	G_SpawnString( "model", "", &model );
	G_SpawnString( "shape", "model", &shape );
	G_SpawnInt( "scale", "100", &scale );
	G_SpawnFloat( "mass", "0", &ent->mass );
	G_SpawnFloat( "kick", "1", &ent->speed );
	ball = (qboolean)!Q_stricmp( shape, "ball" );
	G_SpawnFloat( "bounce", ball ? "0.7" : "0.25", &ent->physicsBounce );
	G_SpawnFloat( "friction", ball ? "90" : "0.7", &ent->random );

	glm = (qboolean)!Q_stricmp( COM_GetExtension( model ), "glm" );
	if ( !model[0] || !BG_PropCloudFromModel( model, &cloud ) ) {
		Com_Printf( S_COLOR_YELLOW "misc_prop: \"%s\" is not a model the server can read (an md3 or a glm)\n", model );
		G_FreeEntity( ent );
		return;
	}

	// into the sizes the physics is made for
	for ( k = 0; k < 3; k++ ) {
		if ( cloud.maxs[k] - cloud.mins[k] > longest )
			longest = cloud.maxs[k] - cloud.mins[k];
	}
	fscale = Com_Clamp( 1.0f, 1023.0f, (float)scale );
	fscale = Com_Clamp( MODEL_MIN_EDGE / longest * 100.0f, MODEL_MAX_EDGE / longest * 100.0f, fscale );
	scale = Com_Clampi( 1, 1023, (int)( fscale + 0.5f ) );

	for ( k = 0; k < 3; k++ ) {
		const float edge = ( cloud.maxs[k] - cloud.mins[k] ) * scale / 100.0f;

		volume *= ( edge < 4.0f ? 4.0f : edge ) * 0.5f;
	}
	if ( ent->mass <= 0.0f ) {
		// see above. volume is that of the box, 32 * 32 * 32 is the cube.
		ent->mass = 4.0f * powf( volume / 32768.0f, 2.0f / 3.0f );
	}

	memset( p, 0, sizeof( *p ) );
	if ( ball ) {
		ent->radius = Com_Clamp( 4.0f, 128.0f, ( ( cloud.maxs[0] - cloud.mins[0] ) + ( cloud.maxs[1] - cloud.mins[1] )
			+ ( cloud.maxs[2] - cloud.mins[2] ) ) / 6.0f * scale / 100.0f );
		ent->random = Com_Clamp( 0.0f, 2000.0f, ent->random );
		p->inertia = 0.4f * ent->radius * ent->radius;
		BG_PropCloudFree( &cloud );
		if ( !Prop_Spawn( ent, model, scale, ent->radius, "sound/weapons/thermal/bounce1.mp3" ) )
			return;
	} else {
		ent->random = Com_Clamp( 0.0f, 2.0f, ent->random );
		p->solid = Q_stricmp( shape, "box" ) ? PROP_SOLID_HULL : PROP_SOLID_BOX;
		BG_PropShapeOfKind( &cloud, p->solid, scale / 100.0f, &p->shape );
		BG_PropCloudFree( &cloud );
		if ( !Prop_SpawnBody( ent, model, scale ) )
			return;
	}

	if ( glm ) {
		// the client makes a Ghoul2 model of the model the entity names and looks for its default skin
		ent->s.modelGhoul2 = 1;
		ent->s.g2radius = Com_Clampi( 8, 255, (int)( longest * scale / 100.0f ) );
		trap->LinkEntity( (sharedEntity_t *)ent );
	}
}
