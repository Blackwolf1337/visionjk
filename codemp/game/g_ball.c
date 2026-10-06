/*
misc_ball: a ball with physics of its own. It falls, bounces, rolls down slopes and comes to rest. Players
kick it by running into it, force push and pull throw it, shots, sabers and explosions knock it away.

Server side only, no client code knows about it. For the client it is a plain model entity (ET_GENERAL) on a
linear trajectory the server renews every frame, with a second trajectory for the turning. Players are not
blocked by it: its contents are those of a corpse, which shots hit and players walk through, and the kick is
worked out here.

Not from JA+ and not from JKAdmin.
*/

#include "g_local.h"

#define BALL_HEALTH			100000		// it takes damage to feel the hits, it never breaks
#define BALL_PLAYER_RADIUS	15.0f		// half the width of a player
#define BALL_MIN_BOUNCE		60.0f		// slower into a surface than this and it stays on it
#define BALL_REST_SPEED		8.0f		// slower than this on flat ground and it lies still
#define BALL_FLAT			0.98f		// z of the normal of ground it can lie on
#define BALL_STUCK_FRAMES	40			// that long inside something solid and it goes home
#define BALL_SOUND_MSEC		150

// gentity_t fields in use:
//	radius			size of the ball
//	movedir			velocity
//	pos1			where it was spawned, its home
//	physicsBounce	part of the speed it keeps in a bounce
//	random			rolling resistance, units per second squared
//	mass			the heavier, the less a kick, a push or a hit moves it
//	speed			how hard players kick it
//	genericValue1	time of the last think			genericValue2	1: lies still
//	genericValue3	sound index of the bounce		genericValue4	time of the next bounce sound
//	genericValue5	time of the next look at the ground while it lies still
//	genericValue6	frames in a row inside something solid

static void ball_pain( gentity_t *self, gentity_t *attacker, int damage );

static void Ball_Wake( gentity_t *self ) {
	self->genericValue2 = 0;
}

// what the client gets: where it is, where it goes and how it turns
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

static void Ball_Home( gentity_t *self ) {
	VectorCopy( self->pos1, self->r.currentOrigin );
	VectorClear( self->movedir );
	self->genericValue6 = 0;
	self->s.eFlags ^= EF_TELEPORT_BIT;
	Ball_Wake( self );
	Ball_Net( self );
}

static void Ball_BounceSound( gentity_t *self ) {
	if ( !self->genericValue3 || level.time < self->genericValue4 )
		return;
	self->genericValue4 = level.time + BALL_SOUND_MSEC;
	G_Sound( self, CHAN_AUTO, self->genericValue3 );
}

// adds a speed to the ball. The mass takes its part.
static void Ball_Impulse( gentity_t *self, const vec3_t dir, float speed ) {
	VectorMA( self->movedir, speed / self->mass, dir, self->movedir );
	Ball_Wake( self );
}

/*
=================
Ball_Players

A player or NPC who touches the ball kicks it: the ball bounces off him like off something much heavier
that moves, and it is put outside of him.
=================
*/
static void Ball_Players( gentity_t *self ) {
	const float	reach = self->radius + BALL_PLAYER_RADIUS;
	int			i;

	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t	*other = &g_entities[i];
		gclient_t	*client = other->client;
		vec3_t		spot, normal, rel, out;
		float		lo, hi, dist, into;
		trace_t		tr;

		if ( !other->inuse || !client || other->health <= 0 || client->noclip )
			continue;
		// only what has a body: not a ghost, not the stalker of g_schizo.c
		if ( !(other->r.contents & CONTENTS_BODY) )
			continue;
		if ( i < MAX_CLIENTS && ( client->pers.connected != CON_CONNECTED || client->sess.sessionTeam == TEAM_SPECTATOR
			|| client->tempSpectate >= level.time ) )
			continue;

		// the player as an upright stick with round ends: the point of it next to the ball
		lo = client->ps.origin[2] + other->r.mins[2] + BALL_PLAYER_RADIUS;
		hi = client->ps.origin[2] + other->r.maxs[2] - BALL_PLAYER_RADIUS;
		if ( hi < lo )
			hi = lo;
		VectorSet( spot, client->ps.origin[0], client->ps.origin[1], Com_Clamp( lo, hi, self->r.currentOrigin[2] ) );

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
			if ( -into > 140.0f )
				Ball_BounceSound( self );
		}
		Ball_Wake( self );

		// out of the player, as far as the world lets it
		VectorMA( self->r.currentOrigin, reach - dist + 0.5f, normal, out );
		trap->Trace( &tr, self->r.currentOrigin, self->r.mins, self->r.maxs, out, self->s.number, MASK_SOLID, qfalse, 0, 0 );
		if ( !tr.startsolid && !tr.allsolid )
			VectorCopy( tr.endpos, self->r.currentOrigin );
	}
}

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
			if ( ++self->genericValue6 > BALL_STUCK_FRAMES ) {
				Ball_Home( self );
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
			const float bounce = ( -into > BALL_MIN_BOUNCE ) ? self->physicsBounce : 0.0f;

			VectorMA( vel, -( 1.0f + bounce ) * into, tr.plane.normal, vel );
			if ( -into > 140.0f )
				Ball_BounceSound( self );
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
		Ball_Home( self );
}

static void ball_think( gentity_t *self ) {
	// the time since the last frame, capped for a server that hitched
	const float dt = Com_Clamp( 0.0f, 0.1f, ( level.time - self->genericValue1 ) * 0.001f );

	self->genericValue1 = level.time;
	self->nextthink = level.time + 1;		// every server frame

	Ball_Players( self );

	if ( self->genericValue2 ) {
		// it lies still. Now and then: is the ground still there?
		if ( level.time >= self->genericValue5 ) {
			vec3_t	below;
			trace_t	tr;

			self->genericValue5 = level.time + 500;
			VectorCopy( self->r.currentOrigin, below );
			below[2] -= 2.0f;
			trap->Trace( &tr, self->r.currentOrigin, self->r.mins, self->r.maxs, below, self->s.number, MASK_SOLID, qfalse, 0, 0 );
			if ( tr.fraction >= 1.0f || tr.startsolid )
				Ball_Wake( self );
		}
		if ( self->genericValue2 )
			return;
	}

	Ball_Move( self, dt );
	Ball_Net( self );
}

/*
=================
ball_pain, ball_die

A shot, a saber or an explosion: away from where it was hit, the more damage the faster
=================
*/
static void ball_pain( gentity_t *self, gentity_t *attacker, int damage ) {
	vec3_t dir;

	self->health = BALL_HEALTH;

	VectorSubtract( self->r.currentOrigin, gPainPoint, dir );
	if ( VectorNormalize( dir ) < 1.0f ) {
		// splash damage comes without a point: away from who did it
		if ( attacker )
			VectorSubtract( self->r.currentOrigin, attacker->r.currentOrigin, dir );
		if ( !attacker || VectorNormalize( dir ) < 1.0f )
			VectorSet( dir, 0, 0, 1 );
	}
	Ball_Impulse( self, dir, Com_Clamp( 40.0f, 900.0f, damage * 9.0f ) );
}

static void ball_die( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod ) {
	ball_pain( self, attacker, damage );
	self->takedamage = qtrue;
}

// a use (targetname) brings it home
static void ball_use( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	Ball_Home( self );
}

/*
=================
G_BallForceThrow

From ForceThrow: every ball in front of the player and in his sight is pushed along his view or pulled
towards him. Reach and strength grow with the level of the power.
=================
*/
void G_BallForceThrow( gentity_t *self, qboolean pull ) {
	const int	power = self->client->ps.fd.forcePowerLevel[pull ? FP_PULL : FP_PUSH];
	const float	reach = 256.0f + 192.0f * power;
	vec3_t		eye, fwd;
	int			i;

	VectorCopy( self->client->ps.origin, eye );
	eye[2] += self->client->ps.viewheight;
	AngleVectors( self->client->ps.viewangles, fwd, NULL, NULL );

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t	*ball = &g_entities[i];
		vec3_t		dir;
		float		dist;
		trace_t		tr;

		if ( !ball->inuse || ball->pain != ball_pain )
			continue;

		VectorSubtract( ball->r.currentOrigin, eye, dir );
		dist = VectorNormalize( dir );
		if ( dist > reach || DotProduct( dir, fwd ) < 0.7f )
			continue;
		trap->Trace( &tr, eye, NULL, NULL, ball->r.currentOrigin, self-g_entities, MASK_SOLID, qfalse, 0, 0 );
		if ( tr.fraction < 1.0f )
			continue;

		if ( pull ) {
			VectorNegate( dir, dir );
			Ball_Impulse( ball, dir, 300.0f + 120.0f * power );
		} else {
			Ball_Impulse( ball, fwd, 350.0f + 150.0f * power );
		}
		ball->movedir[2] += 80.0f / ball->mass;
	}
}

/*QUAKED misc_ball (0.9 0.6 0.1) (-18 -18 -18) (18 18 18)
A ball. It falls, bounces and rolls, players kick it by running into it, force push and pull throw it, shots,
sabers and explosions knock it away. Players are not blocked by it. Using it (targetname) brings it back to
where it was spawned, so does falling out of the world or getting stuck.

"model"			default models/items/remote.md3, the ball of the training remote
"modelscale"	size of the model in percent, default 600 (that model is tiny)
"radius"		size of the ball for the physics, default 18. Should fit the scaled model
"bounce"		part of its speed it keeps when it bounces, 0 to 0.95, default 0.7
"friction"		how fast it slows down when it rolls, units per second squared, default 90
"mass"			the heavier, the less a kick, a push or a hit moves it, default 1
"kick"			how hard players kick it, default 1
*/
void SP_misc_ball( gentity_t *ent ) {
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
	ent->physicsBounce = Com_Clamp( 0.0f, 0.95f, ent->physicsBounce );
	ent->random = Com_Clamp( 0.0f, 2000.0f, ent->random );
	ent->mass = Com_Clamp( 0.1f, 100.0f, ent->mass );
	ent->speed = Com_Clamp( 0.0f, 10.0f, ent->speed );

	// without room for the model in the table of the map there is nothing to show
	ent->s.modelindex = G_TryConfigstringIndex( model, CS_MODELS, MAX_MODELS );
	if ( !ent->s.modelindex ) {
		Com_Printf( S_COLOR_YELLOW "misc_ball: no room for the model %s\n", model );
		G_FreeEntity( ent );
		return;
	}

	ent->s.eType = ET_GENERAL;
	ent->s.iModelScale = Com_Clampi( 1, 1023, scale );
	VectorSet( ent->r.mins, -ent->radius, -ent->radius, -ent->radius );
	VectorSet( ent->r.maxs, ent->radius, ent->radius, ent->radius );
	ent->r.contents = CONTENTS_CORPSE;		// shots hit it, players walk through
	ent->clipmask = MASK_SOLID;

	ent->takedamage = qtrue;
	ent->health = BALL_HEALTH;
	ent->pain = ball_pain;
	ent->die = ball_die;
	ent->use = ball_use;

	G_SetOrigin( ent, ent->s.origin );
	VectorCopy( ent->r.currentOrigin, ent->pos1 );
	VectorClear( ent->movedir );
	VectorClear( ent->s.apos.trBase );
	VectorClear( ent->s.apos.trDelta );
	ent->s.apos.trType = TR_STATIONARY;

	ent->genericValue1 = level.time;
	ent->genericValue2 = 0;
	ent->genericValue3 = G_TryConfigstringIndex( "sound/weapons/thermal/bounce1.mp3", CS_SOUNDS, MAX_SOUNDS );
	ent->genericValue4 = 0;
	ent->genericValue6 = 0;

	ent->think = ball_think;
	ent->nextthink = level.time + 1;

	trap->LinkEntity( (sharedEntity_t *)ent );
}
