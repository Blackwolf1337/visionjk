/*
misc_blackhole: a point that pulls players, NPCs and the props of g_props.c in and crushes who reaches its core.

Server side only, no client code knows about it. What the client shows are stock entity features: the entity
is an ET_FX one like fx_runner (the client plays its effect in a loop), and it carries a looping sound and a
light, which the client adds for every entity type (CG_EntityEffects).

Not from JA+ and not from JKAdmin.
*/

#include "g_local.h"

#define BLACKHOLE_START_OFF		1
#define BLACKHOLE_HURT_MSEC		100		// the core hurts this often

// gentity_t fields in use: radius (reach), speed (pull at the core), splashRadius (core), damage, delay (effect),
// genericValue1 time of the last think, genericValue2 time of the next damage, genericValue3 sound index,
// genericValue4 light

/*
=================
blackhole_think

Runs every server frame while the hole is on: a pull in steps of 100 msec would jerk the players around.
=================
*/
static void blackhole_think( gentity_t *self ) {
	// the pull is an acceleration, it needs the time since the last frame. Capped for a server that hitched.
	const float		dt = Com_Clamp( 0.0f, 0.1f, ( level.time - self->genericValue1 ) * 0.001f );
	const qboolean	hurt = (qboolean)( self->damage > 0 && level.time >= self->genericValue2 );
	int				i;

	self->genericValue1 = level.time;
	self->nextthink = level.time + 1;
	if ( hurt )
		self->genericValue2 = level.time + BLACKHOLE_HURT_MSEC;

	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t	*other = &g_entities[i];
		gclient_t	*client = other->client;
		const float	*origin;
		vec3_t		dir;
		float		dist, pull;

		if ( !other->inuse )
			continue;
		if ( client ) {
			if ( client->noclip )
				continue;
			// of the players only those in the game. NPCs have a client too and are pulled like them.
			if ( i < MAX_CLIENTS && ( client->pers.connected != CON_CONNECTED || client->sess.sessionTeam == TEAM_SPECTATOR
				|| client->tempSpectate >= level.time ) )
				continue;
			origin = client->ps.origin;
		} else if ( G_PropIs( other ) ) {
			// balls, cubes and props: pulled like the players, by the middle of their bodies
			origin = other->r.currentOrigin;
		} else {
			continue;
		}

		VectorSubtract( self->r.currentOrigin, origin, dir );
		dist = VectorNormalize( dir );
		if ( dist >= self->radius )
			continue;

		// nothing at the edge, the full strength at the core, in between with the square
		pull = 1.0f - dist / self->radius;
		if ( !client ) {
			// a prop does not break, so the core does nothing to it. Damage would knock it out of the hole.
			G_PropPull( other, dir, self->speed * pull * pull, dt );
			continue;
		}
		VectorMA( client->ps.velocity, self->speed * pull * pull * dt, dir, client->ps.velocity );

		if ( hurt && dist <= self->splashRadius && other->health > 0 )
			G_Damage( other, self, self, NULL, NULL, self->damage, DAMAGE_NO_KNOCKBACK|DAMAGE_NO_ARMOR, MOD_CRUSH );
	}
}

static void blackhole_set( gentity_t *self, qboolean on ) {
	if ( on ) {
		self->s.modelindex2 = FX_STATE_CONTINUOUS;
		self->s.loopSound = self->genericValue3;
		self->s.constantLight = self->genericValue4;
		self->genericValue1 = level.time;
		self->genericValue2 = level.time;
		self->think = blackhole_think;
		self->nextthink = level.time + 1;
	} else {
		self->s.modelindex2 = FX_STATE_OFF;
		self->s.loopSound = 0;
		self->s.constantLight = 0;
		self->think = NULL;
		self->nextthink = 0;
	}
}

static void blackhole_use( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	blackhole_set( self, (qboolean)( self->think == NULL ) );
}

/*QUAKED misc_blackhole (0.4 0.1 0.9) (-8 -8 -8) (8 8 8) START_OFF
Pulls players and NPCs towards itself and crushes who reaches its core. Players in noclip and spectators are
left alone. The props with physics (misc_prop, misc_cube, misc_ball) are pulled in the same way, whatever their
mass, and are not hurt by the core. Using it (targetname) turns it on and off.

"radius"		reach of the pull, default 600
"strength"		pull at the core in units per second squared, default 2400 (the normal gravity is 800). It falls
				off towards the edge with the square of the distance
"coreradius"	who gets this close to the origin is hurt, default 48
"dmg"			damage in the core, ten times a second, default 1000. 0: nobody is hurt
"fxFile"		effect that shows it, default volumetric/black_smoke. "none": no effect
"delay"			msec between two plays of the effect, default 100
"noise"			looping sound, default sound/ambience/outdoor/windy1_lp.wav. "none": silent
"light"			strength of its purple light, default 250, 0 for none
*/
void SP_misc_blackhole( gentity_t *ent ) {
	char	*s;
	int		light;

	G_SpawnFloat( "radius", "600", &ent->radius );
	G_SpawnFloat( "strength", "2400", &ent->speed );
	G_SpawnInt( "coreradius", "48", &ent->splashRadius );
	G_SpawnInt( "dmg", "1000", &ent->damage );
	G_SpawnInt( "delay", "100", &ent->delay );
	G_SpawnInt( "light", "250", &light );

	// the pull divides by the radius
	if ( ent->radius < 1.0f )
		ent->radius = 1.0f;
	if ( ent->delay < 50 )
		ent->delay = 50;

	// the client plays the effect of an ET_FX entity by itself, see fx_runner
	G_SpawnString( "fxFile", "volumetric/black_smoke", &s );
	ent->s.eType = ET_FX;
	ent->s.modelindex = ( s[0] && Q_stricmp( s, "none" ) ) ? G_EffectIndex( s ) : 0;
	ent->s.speed = ent->delay;
	ent->s.time = 0;
	if ( VectorCompare( ent->s.angles, vec3_origin ) )
		VectorSet( ent->s.angles, -90, 0, 0 );		// upwards without angles, like fx_runner

	G_SpawnString( "noise", "sound/ambience/outdoor/windy1_lp.wav", &s );
	ent->genericValue3 = ( s[0] && Q_stricmp( s, "none" ) ) ? G_SoundIndex( s ) : 0;

	// constantLight: red, green, blue and a quarter of the strength, one byte each
	light = Com_Clampi( 0, 1020, light );
	ent->genericValue4 = light ? (int)( 140u | ( 40u << 8 ) | ( 255u << 16 ) | ( (unsigned)( light / 4 ) << 24 ) ) : 0;

	G_SetOrigin( ent, ent->s.origin );
	VectorSet( ent->r.mins, -8, -8, -8 );
	VectorSet( ent->r.maxs, 8, 8, 8 );
	ent->r.contents = 0;
	ent->use = blackhole_use;
	blackhole_set( ent, (qboolean)!( ent->spawnflags & BLACKHOLE_START_OFF ) );

	trap->LinkEntity( (sharedEntity_t *)ent );
}
