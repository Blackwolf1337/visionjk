/*
misc_ball, misc_cube and misc_prop: props with physics of their own. misc_prop is the general one, any model of
the game as a box or a ball of its size. The other two are a ball and a crate with numbers that suit them.

The ball falls, bounces, rolls down slopes and comes to rest. The cube is a rigid body with corners and edges:
it lands on them, tips over, tumbles and slides to a stop on one of its faces. Players kick the ball by running
into it and shove the cube, which is heavy and slows them down. Force push and pull throw both, shots, sabers
and explosions knock them away. They collide with each other, each by its mass, so cubes can be pushed with
cubes and stacked. With +button13 a player takes the one he looks at: it hangs from a point in front of his
eyes by the spot he took it at, the cube by a corner, and swings there until he lets go. An admin takes another
player the same way and carries him in the hug of amhug, right in front of him, until he lets go.

Server side only, no client code knows about them. For the client they are plain model entities (ET_GENERAL)
whose place and angles the server renews every frame. Players are not blocked by them: their contents are those
of a corpse, which shots hit and players walk through, and what a player does to them is worked out here.

Not from JA+ and not from JKAdmin.
*/

#include "g_local.h"
#include "b_local.h"				// G_BoundsOverlap
#include "qcommon/qfiles.h"			// the header of an md3, for the bounds of the model of a box
#include "rd-common/mdx_format.h"	// and what a Ghoul2 model is made of, for the same

// +button13. The base game uses the button bits up to 2048 and JA+ takes 4096 (+button12) for the hook. 8192,
// 16384 and 32768 (+button13 to +button15) are free, and all 16 bits of the buttons reach the server.
#define BUTTON_GRAB			8192

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
#define BODY_SPOTS			27			// the spots of a cube that are looked at: 8 corners, the middles of 12 edges and
#define BODY_SPOT_MIDDLE	13			//	6 faces. With its middle, which is left out, three by three by three.
#define BODY_MAX_CONTACTS	96			// 26 against the world, the rest against other props
#define BODY_SKIN			1.0f		// this close to a surface a spot counts as touching it
#define BODY_SLOP			0.05f		// this deep in a surface is left alone
#define BODY_CORE			0.55f		// part of the smallest half edge: a box in the middle nothing gets into
#define BODY_GROUND			0.7f		// z of the normal of a surface that carries it
#define BODY_MAX_SPEED		3000.0f
#define BODY_MAX_SPIN		30.0f		// radians per second
#define BODY_CALM_SPEED		14.0f		// slower than this and turning slower than BODY_CALM_SPIN for
#define BODY_CALM_SPIN		0.5f		//	BODY_CALM_MSEC on something that carries it: it lies still
#define BODY_CALM_MSEC		300

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

#define MODEL_MIN_EDGE		8.0f		// the longest edge of the box of a misc_prop: its model is scaled into these
#define MODEL_MAX_EDGE		384.0f
#define MODEL_MAX_BYTES		( 32 * 1024 * 1024 )	// of a Ghoul2 model that is read for its bounds

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
	qboolean	box;			// a cube, else a ball
	matrix3_t	axis;			// how it is turned: forward, left, up
	vec3_t		spin;			// angular velocity in the world, radians per second
	vec3_t		half;			// cube: half its edges
	vec3_t		centre;			// cube: from the origin of the model to its middle, in the model
	float		inertia;		// moment of inertia of a mass of 1
	gentity_t	*holder;		// who has it in his hand
	vec3_t		grab;			// the spot he holds it at, in the prop
	vec3_t		hand;			// where his hand was in the last frame
	int			calmTime;		// since when it hardly moves, 0: it moves
} prop_t;

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

static void prop_pain( gentity_t *self, gentity_t *attacker, int damage );

static prop_t *Prop_State( const gentity_t *self ) {
	return &props[self - g_entities];
}

static qboolean Prop_Is( const gentity_t *ent ) {
	return ( ent->inuse && ent->pain == prop_pain ) ? qtrue : qfalse;
}

static void Prop_Wake( gentity_t *self ) {
	const qboolean	slept = self->genericValue2 ? qtrue : qfalse;
	int				i;

	self->genericValue2 = 0;
	Prop_State( self )->calmTime = 0;
	if ( !slept )
		return;

	// it lay still and may have carried others: what lies still next to it is woken too, down the whole pile
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t	*other = &g_entities[i];
		float		reach;

		if ( other == self || !Prop_Is( other ) || !other->genericValue2 )
			continue;
		reach = self->radius + other->radius + 2.0f;
		if ( DistanceSquared( self->r.currentOrigin, other->r.currentOrigin ) <= reach * reach )
			Prop_Wake( other );
	}
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

	if ( !other->genericValue2 && !( q->calmTime && carries ) )
		return qfalse;
	// what lies on it does not wake it with its weight, whatever that is
	if ( into * ( carries ? 1.0f : ratio ) > PROP_WAKE_SPEED || sep < -PROP_WAKE_DEPTH ) {
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

static float Prop_MinHalf( const prop_t *p ) {
	float least = p->half[0];

	if ( p->half[1] < least )
		least = p->half[1];
	if ( p->half[2] < least )
		least = p->half[2];
	return least;
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

// one of the spots of the cube, as an offset from its middle in the world
static void Body_Spot( const prop_t *p, int n, vec3_t out ) {
	vec3_t local;

	VectorSet( local, ( n % 3 - 1 ) * p->half[0], ( n / 3 % 3 - 1 ) * p->half[1], ( n / 9 - 1 ) * p->half[2] );
	Body_ToWorld( p, local, out );
}

// how far the cube reaches from its middle along a direction
static float Body_Reach( const prop_t *p, const vec3_t dir ) {
	return fabsf( DotProduct( dir, p->axis[0] ) ) * p->half[0] + fabsf( DotProduct( dir, p->axis[1] ) ) * p->half[1]
		+ fabsf( DotProduct( dir, p->axis[2] ) ) * p->half[2];
}

// is a point in the cube or within BODY_SKIN of it
static qboolean Body_Holds( const gentity_t *ent, const prop_t *p, const vec3_t point ) {
	vec3_t	rel;
	int		k;

	VectorSubtract( point, ent->r.currentOrigin, rel );
	for ( k = 0; k < 3; k++ ) {
		if ( fabsf( DotProduct( rel, p->axis[k] ) ) > p->half[k] + BODY_SKIN )
			return qfalse;
	}
	return qtrue;
}

// the spot of the cube next to an offset from its middle. Returns how far the offset is from it, 0 inside.
static float Body_Nearest( const prop_t *p, const vec3_t rel, vec3_t out ) {
	vec3_t	local;
	int		k;

	for ( k = 0; k < 3; k++ )
		local[k] = Com_Clamp( -p->half[k], p->half[k], DotProduct( rel, p->axis[k] ) );
	Body_ToWorld( p, local, out );
	return Distance( rel, out );
}

// the box in the world the turned cube fits in, a unit smaller
static void Body_Bounds( const prop_t *p, vec3_t mins, vec3_t maxs ) {
	int k, j;

	for ( k = 0; k < 3; k++ ) {
		float reach = -1.0f;

		for ( j = 0; j < 3; j++ )
			reach += fabsf( p->axis[j][k] ) * p->half[j];
		if ( reach < 1.0f )
			reach = 1.0f;
		mins[k] = -reach;
		maxs[k] = reach;
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
		VectorMA( org, -p->centre[k], p->axis[k], org );
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
	Body_Bounds( p, self->r.mins, self->r.maxs );
	for ( k = 0; k < 3; k++ ) {
		self->r.mins[k] -= 1.0f;
		self->r.maxs[k] += 1.0f;
	}

	trap->LinkEntity( (sharedEntity_t *)self );
}

static void Prop_Home( gentity_t *self ) {
	prop_t *p = Prop_State( self );

	VectorCopy( self->pos1, self->r.currentOrigin );
	VectorClear( self->movedir );
	VectorClear( p->spin );
	p->holder = NULL;
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
Ball_Players

A player or NPC who touches the ball kicks it: the ball bounces off him like off something much heavier
that moves, and it is put outside of him.
=================
*/
static void Ball_Players( gentity_t *self, const prop_t *p ) {
	const float	reach = self->radius + PROP_PLAYER_RADIUS;
	int			i;

	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t	*other = &g_entities[i];
		gclient_t	*client = other->client;
		vec3_t		spot, normal, rel, out;
		float		dist, into;
		trace_t		tr;

		// the one who holds it does not kick it out of his own hand
		if ( !Prop_Mover( other ) || other == p->holder )
			continue;
		Prop_PlayerSpot( other, self->r.currentOrigin[2], spot );

		VectorSubtract( self->r.currentOrigin, spot, normal );
		dist = VectorNormalize( normal );
		if ( dist >= reach )
			continue;
		if ( dist < 0.1f )
			VectorSet( normal, 0, 0, 1 );

		// how fast they come together
		VectorSubtract( self->movedir, client->ps.velocity, rel );
		into = DotProduct( rel, normal );
		if ( into < 0.0f ) {
			VectorMA( self->movedir, -( 1.0f + self->physicsBounce ) * into * self->speed / self->mass, normal, self->movedir );
			// a kicked ball comes off the ground a little
			self->movedir[2] += -into * 0.15f * self->speed / self->mass;
			if ( -into > PROP_LOUD )
				Prop_BounceSound( self );
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

A player or NPC who walks into the cube shoves it: an impulse at the spot of the cube next to him, which turns
it too when that spot is off its middle, and the cube is put outside of him. It is heavy: only part of his
speed gets into it, and he is slowed down while he shoves (G_PropPlayerSpeed).
=================
*/
static void Cube_Players( gentity_t *self, prop_t *p ) {
	vec3_t	mins, maxs;
	int		i;

	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t	*other = &g_entities[i];
		gclient_t	*client = other->client;
		vec3_t		spot, rel, r, normal, speed, out;
		float		dist, depth, into;
		trace_t		tr;

		if ( !Prop_Mover( other ) || other == p->holder )
			continue;
		Prop_PlayerSpot( other, self->r.currentOrigin[2], spot );

		VectorSubtract( spot, self->r.currentOrigin, rel );
		dist = Body_Nearest( p, rel, r );
		if ( dist >= PROP_PLAYER_RADIUS )
			continue;
		if ( dist > 0.1f ) {
			// from him to the cube
			VectorSubtract( r, rel, normal );
			VectorScale( normal, 1.0f / dist, normal );
			depth = PROP_PLAYER_RADIUS - dist;
		} else {
			// he is in the cube: out to the side he is not on, a bit in every frame
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
		VectorSubtract( speed, client->ps.velocity, speed );
		into = DotProduct( speed, normal );
		if ( into < 0.0f ) {
			vec3_t rn, impulse;

			CrossProduct( r, normal, rn );
			VectorScale( normal, -( 1.0f + self->physicsBounce ) * into * self->speed
				/ ( self->mass * ( 1.0f + VectorLengthSquared( rn ) / p->inertia ) ), impulse );
			Body_Push( self, p, r, impulse );
			if ( -into > PROP_LOUD )
				Prop_BounceSound( self );

			// shoving it is work
			if ( i < MAX_CLIENTS && DotProduct( client->ps.velocity, normal ) > 10.0f ) {
				shoveTime[i] = level.time + SHOVE_SLOW_MSEC;
				shoveScale[i] = Com_Clamp( 0.25f, 1.0f, 1.0f / ( 1.0f + 0.2f * self->mass ) );
			}
		}
		Prop_Wake( self );

		// he stands on it: there is nowhere to put it but into the ground
		if ( normal[2] < -0.5f )
			continue;

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
	Prop_State( self )->holder = NULL;
	Prop_Wake( self );
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
		} else if ( !Prop_Is( prop ) || p->holder
			|| Distance( prop->r.currentOrigin, eye ) > GRAB_REACH + prop->radius ) {
			continue;
		} else if ( p->box ) {
			int c;

			for ( c = 0; c < 8; c++ ) {
				vec3_t	local, corner;

				VectorSet( local, ( c & 1 ) ? p->half[0] : -p->half[0], ( c & 2 ) ? p->half[1] : -p->half[1],
					( c & 4 ) ? p->half[2] : -p->half[2] );
				Body_ToWorld( p, local, corner );
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

// the hug is over, the animations end where they are. He keeps the speed he has, so he can be thrown.
static void Prop_DropPlayer( gentity_t *victim ) {
	prop_t			*p = Prop_State( victim );
	playerState_t	*ps = &victim->client->ps, *arms = &p->holder->client->ps;

	if ( arms->torsoAnim == BOTH_HUGGER1 )
		arms->torsoTimer = 0;
	if ( ps->legsAnim == BOTH_HUGGEE1 )
		ps->legsTimer = 0;
	if ( ps->forceHandExtend == HANDEXTEND_TAUNT && ps->forceDodgeAnim == BOTH_HUGGEE1 )
		ps->forceHandExtendTime = 0;
	p->holder = NULL;
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
	float			len;

	// the button was let go, one of them died, left or went into a duel, the admin logged out
	if ( !Prop_HolderOK( holder ) || !Prop_Carrier( holder ) || !Prop_PlayerOK( victim ) || !Prop_PlayerFree( victim ) ) {
		Prop_DropPlayer( victim );
		return;
	}
	ps = &victim->client->ps;

	Prop_HugSpot( holder, spot );
	VectorSubtract( spot, ps->origin, want );
	// caught on something, or one of them was taken somewhere else
	if ( VectorLength( want ) > GRAB_PLAYER_BREAK ) {
		Prop_DropPlayer( victim );
		return;
	}

	// the way his place went since the last frame
	VectorSubtract( spot, p->hand, way );
	VectorCopy( spot, p->hand );

	VectorCopy( ps->viewangles, angles );
	angles[YAW] = AngleNormalize180( holder->client->ps.viewangles[YAW] + 180.0f );
	SetClientViewAngle( victim, angles );
	Prop_HugAnim( holder, SETANIM_TORSO, BOTH_HUGGER1 );
	Prop_HugAnim( victim, SETANIM_LEGS, BOTH_HUGGEE1 );
	Prop_HugHands( victim );

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
	if ( len > GRAB_PLAYER_SPEED )
		VectorScale( want, GRAB_PLAYER_SPEED / len, want );
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

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t	*other = &g_entities[i];
		prop_t		*q = &props[i];
		const float	reach = self->radius + other->radius;
		vec3_t		rel, normal, r2, speed, out;
		float		depth, into, ratio, len;
		qboolean	fixed;
		trace_t		tr;

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
Cube_Cube

Where this cube touches another one. First the direction they are pushed apart along: of the six directions
their faces look into, the one along which they overlap least. Without an overlap along one of them they do
not touch. Then every spot of one that is in the other is a contact.
=================
*/
static int Cube_Cube( gentity_t *self, const prop_t *p, gentity_t *other, const prop_t *q, contact_t *list, int count ) {
	vec3_t	apart, normal, r, r2, point;
	float	least = 0.0f, reachSelf = 0.0f, reachOther = 0.0f;
	int		k, n;

	VectorSubtract( self->r.currentOrigin, other->r.currentOrigin, apart );
	VectorClear( normal );
	for ( k = 0; k < 6; k++ ) {
		const float	*dir = ( k < 3 ) ? q->axis[k] : p->axis[k - 3];
		const float	dist = DotProduct( apart, dir );
		const float	mine = Body_Reach( p, dir ), theirs = Body_Reach( q, dir );
		const float	overlap = mine + theirs - fabsf( dist );

		if ( overlap < -BODY_SKIN )
			return count;
		if ( !k || overlap < least ) {
			// from the other one to this one
			least = overlap;
			VectorScale( dir, ( dist < 0.0f ) ? -1.0f : 1.0f, normal );
			reachSelf = mine;
			reachOther = theirs;
		}
	}

	for ( n = 0; n < BODY_SPOTS; n++ ) {
		if ( n == BODY_SPOT_MIDDLE )
			continue;

		// a spot of this cube in the other one: how far it is under the face of the other one
		Body_Spot( p, n, r );
		VectorAdd( self->r.currentOrigin, r, point );
		if ( Body_Holds( other, q, point ) ) {
			VectorSubtract( point, other->r.currentOrigin, r2 );
			count = Contact_Add( self, p, other, q, list, count, r, r2, normal, DotProduct( normal, r2 ) - reachOther );
		}

		// and the same spot of the other one in this cube
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

Where the cube touches the world and the other props.

The world: the engine only traces boxes that are not turned, so the spots of the cube are looked at one by
one: its corners and the middles of its edges and faces. A line from the middle of the cube through each spot
finds the surface the spot is in or close to.

Props: they are not in those traces (shots and players have to get through them), their shapes are known here.
=================
*/
static int Body_Contacts( gentity_t *self, const prop_t *p, contact_t *list ) {
	int count = 0, n;

	for ( n = 0; n < BODY_SPOTS; n++ ) {
		contact_t	*c = &list[count];
		vec3_t		spot;
		float		len;
		trace_t		tr;

		if ( n == BODY_SPOT_MIDDLE )
			continue;

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

	for ( n = MAX_CLIENTS; n < level.num_entities && count < BODY_MAX_CONTACTS; n++ ) {
		gentity_t		*other = &g_entities[n];
		const prop_t	*q = &props[n];
		float			reach;

		if ( other == self || !Prop_Is( other ) )
			continue;
		reach = self->radius + other->radius + BODY_SKIN;
		if ( DistanceSquared( self->r.currentOrigin, other->r.currentOrigin ) > reach * reach )
			continue;
		count = q->box ? Cube_Cube( self, p, other, q, list, count ) : Cube_Ball( self, p, other, q, list, count );
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
	const float	stiff = GRAB_STIFF / sqrtf( self->mass );
	vec3_t		grab, corr, end, mins, maxs;
	float		loud = 0.0f, len;
	trace_t		tr;
	int			i, round;

	*numContacts = count;

	vel[2] -= g_gravity.value * dt;
	// a little drag in the air, more on the turning where it touches or hangs
	VectorScale( vel, 1.0f - 0.05f * dt, vel );
	VectorScale( p->spin, 1.0f - ( count ? 1.5f : p->holder ? 0.6f : 0.2f ) * dt, p->spin );

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
			if ( len > GRAB_MAX_SPEED )
				VectorScale( want, GRAB_MAX_SPEED / len, want );

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

	if ( dt < 0.001f )
		return;
	steps = Com_Clampi( 1, BODY_MAX_STEPS, (int)ceilf( dt / BODY_STEP ) );

	VectorCopy( p->hand, hand );
	VectorClear( handVel );
	VectorClear( way );
	if ( p->holder ) {
		// where his hand is now and how fast it got there
		Prop_Hand( p->holder, hand );
		VectorSubtract( hand, p->hand, way );
		VectorScale( way, 1.0f / dt, handVel );
		len = VectorLength( handVel );
		if ( len > GRAB_MAX_SPEED )
			VectorScale( handVel, GRAB_MAX_SPEED / len, handVel );
	}

	for ( i = 1; i <= steps; i++ ) {
		vec3_t now;

		// the hand on its way through the frame
		VectorMA( p->hand, (float)i / steps, way, now );
		if ( !Body_Step( self, p, dt / steps, now, handVel, contacts, &count ) )
			break;
	}

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
		if ( Distance( spot, hand ) > GRAB_BREAK )
			Prop_Release( self );
		return;
	}

	// a ball goes on by itself (Ball_Move), and a frame that ended early says nothing about rest
	if ( !p->box || i <= steps )
		return;

	if ( VectorLength( self->movedir ) < BODY_CALM_SPEED && VectorLength( p->spin ) < BODY_CALM_SPIN
		&& Body_Carried( p, contacts, count ) ) {
		if ( !p->calmTime ) {
			p->calmTime = level.time;
		} else if ( level.time - p->calmTime >= BODY_CALM_MSEC ) {
			VectorClear( self->movedir );
			VectorClear( p->spin );
			self->genericValue2 = 1;
			self->genericValue5 = level.time + 500;
		}
	} else {
		p->calmTime = 0;
	}
}

// while it lies still: is what it lies on still there
static qboolean Prop_OnGround( gentity_t *self, const prop_t *p ) {
	vec3_t	below;
	trace_t	tr;

	if ( p->box ) {
		contact_t	contacts[BODY_MAX_CONTACTS];
		const int	count = Body_Contacts( self, p, contacts );
		int			i;

		// something that moves pushed into it
		for ( i = 0; i < count; i++ ) {
			if ( contacts[i].sep < -2.0f )
				return qfalse;
		}
		return Body_Carried( p, contacts, count );
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

static void prop_think( gentity_t *self ) {
	prop_t		*p = Prop_State( self );
	// the time since the last frame, capped for a server that hitched
	const float	dt = Com_Clamp( 0.0f, 0.1f, ( level.time - self->genericValue1 ) * 0.001f );

	self->genericValue1 = level.time;
	self->nextthink = level.time + 1;		// every server frame

	// he let go of the button, died or left
	if ( p->holder && !Prop_HolderOK( p->holder ) )
		Prop_Release( self );

	if ( p->box )
		Cube_Players( self, p );
	else
		Ball_Players( self, p );

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
	const prop_t	*p = Prop_State( self );
	vec3_t			at, dir, spot;

	self->health = PROP_HEALTH;

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
===============================================================================

Spawning

===============================================================================
*/

/*
=================
Prop_ModelBounds

The box a model fills: the bounds of the first frame of the md3. The server does not load models, so the file
is read here. With it the cube of the physics is the one the client draws, whatever the model.
=================
*/
static qboolean Prop_ModelBounds( const char *model, vec3_t mins, vec3_t maxs ) {
	const int		need = sizeof( md3Header_t ) + sizeof( md3Frame_t );
	md3Header_t		header;
	md3Frame_t		frame;
	fileHandle_t	f = NULL_FILE;
	const int		len = trap->FS_Open( model, &f, FS_READ );
	qboolean		ok = qfalse;
	int				skip, k;

	if ( !f )
		return qfalse;

	if ( len >= need ) {
		trap->FS_Read( &header, sizeof( header ), f );
		// the frames follow the header in every md3 of the game. There is no seek here: a short gap is read over.
		skip = LittleLong( header.ofsFrames ) - (int)sizeof( header );
		if ( LittleLong( header.ident ) == MD3_IDENT && LittleLong( header.numFrames ) >= 1
			&& skip >= 0 && skip <= 4096 && skip <= len - need ) {
			while ( skip > 0 ) {
				const int part = ( skip < (int)sizeof( frame ) ) ? skip : (int)sizeof( frame );

				trap->FS_Read( &frame, part, f );
				skip -= part;
			}
			trap->FS_Read( &frame, sizeof( frame ), f );

			ok = qtrue;
			for ( k = 0; k < 3; k++ ) {
				mins[k] = LittleFloat( frame.bounds[0][k] );
				maxs[k] = LittleFloat( frame.bounds[1][k] );
				if ( !isfinite( mins[k] ) || !isfinite( maxs[k] ) || maxs[k] - mins[k] < 0.5f )
					ok = qfalse;
			}
		}
	}
	trap->FS_Close( f );
	return ok;
}

/*
=================
Prop_GlmBounds

The same for a Ghoul2 model, which has no bounds in its file: the box around the vertices of its first level of
detail as they stand in the file, which is how the client shows it without an animation. Surfaces that are
switched off are left out, the client does not draw them either. Every offset is checked against the size of
the file.
=================
*/
static qboolean Prop_GlmBounds( const char *model, vec3_t mins, vec3_t maxs ) {
	fileHandle_t		f = NULL_FILE;
	const int			len = trap->FS_Open( model, &f, FS_READ );
	byte				*data;
	const mdxmHeader_t	*header;
	// the offsets of the entries of the surface hierarchy follow the header, and count from there
	const int			ofsHier = (int)sizeof( mdxmHeader_t );
	int					numSurfaces, ofsLODs, i, k, numVerts = 0;
	qboolean			ok = qfalse;

	if ( !f )
		return qfalse;
	if ( len < (int)sizeof( mdxmHeader_t ) || len > MODEL_MAX_BYTES || !( data = (byte *)malloc( len ) ) ) {
		trap->FS_Close( f );
		return qfalse;
	}
	trap->FS_Read( data, len, f );
	trap->FS_Close( f );

	header = (const mdxmHeader_t *)data;
	numSurfaces = LittleLong( header->numSurfaces );
	ofsLODs = LittleLong( header->ofsLODs );
	ClearBounds( mins, maxs );
	// the offsets of the surfaces of the first level of detail follow its mdxmLOD_t, and count from there
	if ( LittleLong( header->ident ) == MDXM_IDENT && LittleLong( header->numLODs ) >= 1
		&& numSurfaces >= 1 && numSurfaces <= 4096
		&& ofsLODs >= (int)sizeof( mdxmHeader_t ) && ofsLODs <= len - (int)sizeof( mdxmLOD_t ) - numSurfaces * 4
		&& ofsHier <= len - numSurfaces * 4 ) {
		const int base = ofsLODs + (int)sizeof( mdxmLOD_t );

		ok = qtrue;
		for ( i = 0; i < numSurfaces && ok; i++ ) {
			int						ofsSurf, ofsVerts, count, index, ofsInfo;
			const mdxmSurface_t		*surf;
			const mdxmVertex_t		*v;

			memcpy( &ofsSurf, data + base + i * 4, 4 );
			ofsSurf = base + LittleLong( ofsSurf );
			if ( ofsSurf < base || ofsSurf > len - (int)sizeof( mdxmSurface_t ) ) {
				ok = qfalse;
				break;
			}
			surf = (const mdxmSurface_t *)( data + ofsSurf );
			count = LittleLong( surf->numVerts );
			ofsVerts = ofsSurf + LittleLong( surf->ofsVerts );
			if ( count < 0 || count > 65536 || ofsVerts < ofsSurf || ofsVerts > len - count * (int)sizeof( mdxmVertex_t ) ) {
				ok = qfalse;
				break;
			}

			// its entry in the hierarchy says whether it is drawn
			index = LittleLong( surf->thisSurfaceIndex );
			if ( index >= 0 && index < numSurfaces ) {
				memcpy( &ofsInfo, data + ofsHier + index * 4, 4 );
				ofsInfo = ofsHier + LittleLong( ofsInfo );
				if ( ofsInfo >= ofsHier && ofsInfo <= len - (int)sizeof( mdxmSurfHierarchy_t ) ) {
					const mdxmSurfHierarchy_t *info = (const mdxmSurfHierarchy_t *)( data + ofsInfo );

					if ( LittleLong( info->flags ) & G2SURFACEFLAG_OFF )
						continue;
				}
			}

			v = (const mdxmVertex_t *)( data + ofsVerts );
			for ( k = 0; k < count; k++, v++ ) {
				vec3_t point;

				VectorSet( point, LittleFloat( v->vertCoords[0] ), LittleFloat( v->vertCoords[1] ), LittleFloat( v->vertCoords[2] ) );
				if ( !isfinite( point[0] ) || !isfinite( point[1] ) || !isfinite( point[2] ) ) {
					ok = qfalse;
					break;
				}
				AddPointToBounds( point, mins, maxs );
			}
			numVerts += count;
		}
	}
	free( data );

	for ( k = 0; k < 3 && ok; k++ ) {
		if ( !numVerts || maxs[k] - mins[k] < 0.5f )
			ok = qfalse;
	}
	return ok;
}

// what the ball and the cube share when they spawn. qfalse: it can not be shown and is gone.
static qboolean Prop_Spawn( gentity_t *ent, const char *model, int scale, float bound, const char *sound ) {
	prop_t *p = Prop_State( ent );

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
sabers and explosions knock it away. It bounces off other balls and off cubes. Players are not blocked by it. A
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

// A rigid body the size of the bounds of a model, scale in percent. What misc_cube and misc_prop share.
// qfalse: it can not be shown and is gone.
static qboolean Prop_SpawnBox( gentity_t *ent, const char *model, const vec3_t mins, const vec3_t maxs, int scale ) {
	prop_t	*p = Prop_State( ent );
	float	bound = 0.0f;
	int		k;

	memset( p, 0, sizeof( *p ) );
	p->box = qtrue;
	for ( k = 0; k < 3; k++ ) {
		p->half[k] = ( maxs[k] - mins[k] ) * 0.5f * scale / 100.0f;
		if ( p->half[k] < 2.0f )
			p->half[k] = 2.0f;
		p->centre[k] = ( maxs[k] + mins[k] ) * 0.5f * scale / 100.0f;
		if ( p->half[k] > bound )
			bound = p->half[k];
	}
	// a solid box, with the mean of its three moments so one number does for every axis. For a cube that is exact.
	p->inertia = ( 2.0f / 9.0f ) * VectorLengthSquared( p->half );
	ent->radius = VectorLength( p->half );

	// Body_Net() sets the box shots are traced against, this one is only there until it has
	return Prop_Spawn( ent, model, scale, bound, "sound/effects/metal_bounce.mp3" );
}

/*QUAKED misc_cube (0.9 0.6 0.1) (-16 -16 -16) (16 16 16)
A cube with corners and edges. It falls, lands on a corner or an edge, tips over, tumbles and slides to a stop
on one of its faces. It is heavy: players shove it slowly instead of kicking it away, force push and pull make
it tumble, shots turn it when they hit off its middle. Cubes push each other and can be stacked, balls bounce
off them. Players are not blocked by it. A player who looks at it
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
	char	*model;
	vec3_t	mins, maxs;
	float	size, longest = 0.0f;
	int		scale = 100, k;

	G_SpawnString( "model", "models/map_objects/cairn/cargo_sm.md3", &model );
	G_SpawnFloat( "size", "32", &size );
	G_SpawnFloat( "bounce", "0.25", &ent->physicsBounce );
	G_SpawnFloat( "friction", "0.7", &ent->random );
	G_SpawnFloat( "mass", "4", &ent->mass );
	G_SpawnFloat( "kick", "1", &ent->speed );

	size = Com_Clamp( 8.0f, 256.0f, size );
	ent->random = Com_Clamp( 0.0f, 2.0f, ent->random );

	if ( Prop_ModelBounds( model, mins, maxs ) ) {
		for ( k = 0; k < 3; k++ ) {
			if ( maxs[k] - mins[k] > longest )
				longest = maxs[k] - mins[k];
		}
		scale = Com_Clampi( 1, 1023, (int)( size / longest * 100.0f + 0.5f ) );
	} else {
		// not an md3 or none the server finds: a cube of that size around the origin of the model as it is
		Com_Printf( S_COLOR_YELLOW "misc_cube: no bounds in %s, taking a cube of %g\n", model, size );
		VectorSet( mins, -0.5f * size, -0.5f * size, -0.5f * size );
		VectorSet( maxs, 0.5f * size, 0.5f * size, 0.5f * size );
	}

	Prop_SpawnBox( ent, model, mins, maxs, scale );
}

/*QUAKED misc_prop (0.9 0.6 0.1) (-16 -16 -16) (16 16 16)
Any model of the game as a thing with the physics of misc_cube or misc_ball. It has the size of its model: the
box of the physics is the box around the model, edge by edge, so a bench is long and low and tips over its long
edge. Props collide with each other, with balls and with cubes, each by its mass, they can be pushed, stacked,
thrown with force push and pull and taken with +button13. Players are not blocked by it.

"model"			an md3 or a glm (a Ghoul2 model stands as it is in its file, without an animation)
"scale"			size of the model in percent, default 100. It is changed if the longest edge of the box would
				be under 8 or over 384 units
"shape"			"box" (default), or "ball" for something round: a ball as wide as the model that rolls. The
				middle of the ball is the origin of the model, so that fits models that are built around it
"mass"			the heavier, the less a shove, a push or a hit moves it. Default 0: from its size. A box of 32
				units has 4 like the cube, and the mass grows with the surface of the box, not with its volume:
				models are shells and not solid blocks, and a big one stays something a player can move
"bounce"		part of its speed it keeps when it hits something, 0 to 0.95, default 0.25 (ball 0.7)
"friction"		box: how well it grips the ground, 0 to 2, default 0.7. Ball: how fast it slows down when it
				rolls, units per second squared, default 90
"kick"			how hard players shove it, default 1
"angles"		how it is turned when it spawns
The origin is where the middle of the box is to be.
*/
void SP_misc_prop( gentity_t *ent ) {
	prop_t		*p = Prop_State( ent );
	char		*model, *shape;
	vec3_t		mins, maxs;
	float		longest = 0.0f, volume = 8.0f, fscale;
	qboolean	glm, ball;
	int			scale, k;

	G_SpawnString( "model", "", &model );
	G_SpawnString( "shape", "box", &shape );
	G_SpawnInt( "scale", "100", &scale );
	G_SpawnFloat( "mass", "0", &ent->mass );
	G_SpawnFloat( "kick", "1", &ent->speed );
	ball = (qboolean)!Q_stricmp( shape, "ball" );
	G_SpawnFloat( "bounce", ball ? "0.7" : "0.25", &ent->physicsBounce );
	G_SpawnFloat( "friction", ball ? "90" : "0.7", &ent->random );

	glm = (qboolean)!Q_stricmp( COM_GetExtension( model ), "glm" );
	if ( !model[0] || !( glm ? Prop_GlmBounds( model, mins, maxs ) : Prop_ModelBounds( model, mins, maxs ) ) ) {
		Com_Printf( S_COLOR_YELLOW "misc_prop: \"%s\" is not a model the server can read (an md3 or a glm)\n", model );
		G_FreeEntity( ent );
		return;
	}

	// into the sizes the physics is made for
	for ( k = 0; k < 3; k++ ) {
		if ( maxs[k] - mins[k] > longest )
			longest = maxs[k] - mins[k];
	}
	fscale = Com_Clamp( 1.0f, 1023.0f, (float)scale );
	fscale = Com_Clamp( MODEL_MIN_EDGE / longest * 100.0f, MODEL_MAX_EDGE / longest * 100.0f, fscale );
	scale = Com_Clampi( 1, 1023, (int)( fscale + 0.5f ) );

	for ( k = 0; k < 3; k++ ) {
		const float edge = ( maxs[k] - mins[k] ) * scale / 100.0f;

		volume *= ( edge < 4.0f ? 4.0f : edge ) * 0.5f;
	}
	if ( ent->mass <= 0.0f ) {
		// see above. volume is that of the box, 32 * 32 * 32 is the cube.
		ent->mass = 4.0f * powf( volume / 32768.0f, 2.0f / 3.0f );
	}

	if ( ball ) {
		ent->radius = Com_Clamp( 4.0f, 128.0f, ( ( maxs[0] - mins[0] ) + ( maxs[1] - mins[1] ) + ( maxs[2] - mins[2] ) ) / 6.0f * scale / 100.0f );
		ent->random = Com_Clamp( 0.0f, 2000.0f, ent->random );
		memset( p, 0, sizeof( *p ) );
		p->inertia = 0.4f * ent->radius * ent->radius;
		if ( !Prop_Spawn( ent, model, scale, ent->radius, "sound/weapons/thermal/bounce1.mp3" ) )
			return;
	} else {
		ent->random = Com_Clamp( 0.0f, 2.0f, ent->random );
		if ( !Prop_SpawnBox( ent, model, mins, maxs, scale ) )
			return;
	}

	if ( glm ) {
		// the client makes a Ghoul2 model of the model the entity names and looks for its default skin
		ent->s.modelGhoul2 = 1;
		ent->s.g2radius = Com_Clampi( 8, 255, (int)( longest * scale / 100.0f ) );
		trap->LinkEntity( (sharedEntity_t *)ent );
	}
}
