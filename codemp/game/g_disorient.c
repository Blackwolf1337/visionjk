/*
Disorientation for one player ("disorient"): the view does not hold still any more. It sways like the deck of a
ship, every few seconds it is thrown around to another side with a shake and the player stumbles a step, and now
and then the world turns around once or twice.

Server side only, the client code is untouched. The view of a player is the angles of the commands the client
sends plus ps.delta_angles, which belong to the server: the sway and the turns are added there. The client
predicts with the same numbers, so the movement and the aim go where the view goes and nothing snaps back. The
roll of the sway shows in the first person view only, the third person camera of the game looks along the view
and has no roll. The shake is the stock screen shake event, sent to this one client (SVF_SINGLECLIENT).

Toggled per player in the web admin panel or with the server console command "disorient <player>". The state
is kept here and is gone with the map. A spectator who follows the player gets the same view, the engine sends
the view of the followed client. A bot does not care: it turns to where it wants to look whatever the offset is.

Not from JA+ and not from JKAdmin.
*/

#include "g_local.h"

gclient_t *ClientForString( const char *s );	// g_svcmds.c

#define DISORIENT_MIN_PAUSE		3500		// msec between two lurches
#define DISORIENT_MAX_PAUSE		9000
#define DISORIENT_WHIRL			4			// one lurch in this many is a whirl
#define DISORIENT_LURCH_RATE	540.0f		// degrees a second the view is thrown around with
#define DISORIENT_WHIRL_RATE	300.0f		// and the world turns with
#define DISORIENT_STUMBLE		170.0f		// speed of the step to the side

// degrees in the units of delta_angles, signed (ANGLE2SHORT cuts to 16 bits, which a difference must not be)
#define DISORIENT_UNITS( deg )	( (int)( ( deg ) * ( 65536.0f / 360.0f ) ) )

// The sway: two sines for each axis, degrees and radians a second. The periods do not fit together, so it does
// not repeat in a way one could learn to steer against.
static const struct {
	float	amp1, freq1;
	float	amp2, freq2;
} disorientSway[3] = {
	{ 8.0f, 1.7f, 3.0f, 0.6f },		// PITCH
	{ 18.0f, 1.3f, 9.0f, 0.5f },	// YAW
	{ 16.0f, 1.1f, 6.0f, 2.3f },	// ROLL
};

typedef struct disorient_s {
	qboolean	active;
	qboolean	paused;			// dead, a spectator, in a vehicle: the sway was taken out
	int			start;			// level.time it was switched on, the sway counts from there
	int			applied[2];		// what the sway has put into delta_angles for pitch and yaw
	int			teleportBit;	// EF_TELEPORT_BIT when applied was right: a spawn or a teleport sets the angles anew
	float		turn;			// degrees the running lurch still has to turn the view, signed
	float		turnRate;		// degrees a second
	int			nextLurch;		// level.time
} disorient_t;

static disorient_t disorient[MAX_CLIENTS];

// takes the sway out of the view again
static void Disorient_Still( gentity_t *ent, disorient_t *d ) {
	gclient_t *client = ent->client;

	if ( ( client->ps.eFlags & EF_TELEPORT_BIT ) == d->teleportBit ) {
		client->ps.delta_angles[PITCH] -= d->applied[PITCH];
		client->ps.delta_angles[YAW] -= d->applied[YAW];
	}
	// nothing else turns this axis for a player on foot, so it is set outright
	client->ps.delta_angles[ROLL] = -client->pers.cmd.angles[ROLL];
	d->applied[PITCH] = d->applied[YAW] = 0;
	d->turn = 0.0f;
}

// the sway of this moment and the piece of the running lurch that falls into this frame
static void Disorient_View( gentity_t *ent, disorient_t *d ) {
	gclient_t	*client = ent->client;
	const float	t = ( level.time - d->start ) * 0.001f;
	const int	teleportBit = client->ps.eFlags & EF_TELEPORT_BIT;
	int			i;

	// a spawn or a teleport has set the angles anew, the sway is not in them any more
	if ( teleportBit != d->teleportBit ) {
		d->teleportBit = teleportBit;
		d->applied[PITCH] = d->applied[YAW] = 0;
	}

	for ( i = 0; i < 3; i++ ) {
		const float	deg = disorientSway[i].amp1 * sinf( t * disorientSway[i].freq1 )
			+ disorientSway[i].amp2 * sinf( t * disorientSway[i].freq2 );
		const int	want = DISORIENT_UNITS( deg );

		if ( i == ROLL ) {
			client->ps.delta_angles[ROLL] = want - client->pers.cmd.angles[ROLL];
		} else {
			// the player's own aim is in the same number: only the change of the sway goes in
			client->ps.delta_angles[i] += want - d->applied[i];
			d->applied[i] = want;
		}
	}

	if ( d->turn != 0.0f ) {
		float step = d->turnRate * ( level.time - level.previousTime ) * 0.001f;

		if ( step > fabsf( d->turn ) )
			step = fabsf( d->turn );
		if ( d->turn < 0.0f )
			step = -step;
		d->turn -= step;
		client->ps.delta_angles[YAW] += DISORIENT_UNITS( step );
	}
}

// the view is thrown around to another side, or the world turns once or twice
static void Disorient_Lurch( gentity_t *ent, disorient_t *d ) {
	gclient_t		*client = ent->client;
	const qboolean	whirl = (qboolean)( Q_irand( 1, DISORIENT_WHIRL ) == 1 );
	gentity_t		*te;

	d->turn = whirl ? flrand( 360.0f, 720.0f ) : flrand( 70.0f, 200.0f );
	d->turnRate = whirl ? DISORIENT_WHIRL_RATE : DISORIENT_LURCH_RATE;
	if ( Q_irand( 0, 1 ) )
		d->turn = -d->turn;

	// The stock screen shake, for this client alone wherever the view is. G_ScreenShake() names its target by
	// s.number, which is the player's own number while not a spectator.
	te = G_ScreenShake( client->ps.origin, ent, whirl ? 2.5f : 5.0f, whirl ? 1800 : 700, qfalse );
	te->r.svFlags |= SVF_SINGLECLIENT|SVF_BROADCAST;
	te->r.singleClient = ent-g_entities;

	// a stumble to the side the view came from. Not in a duel, that would decide fights, and not in the air.
	if ( !whirl && !client->ps.duelInProgress && client->ps.groundEntityNum != ENTITYNUM_NONE ) {
		vec3_t angles, right;

		VectorSet( angles, 0, client->ps.viewangles[YAW], 0 );
		AngleVectors( angles, NULL, right, NULL );
		VectorMA( client->ps.velocity, d->turn > 0.0f ? DISORIENT_STUMBLE : -DISORIENT_STUMBLE, right, client->ps.velocity );
	}
}

/*
=================
G_DisorientFrame

Once per server frame
=================
*/
void G_DisorientFrame( void ) {
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		disorient_t	*d = &disorient[i];
		gentity_t	*ent = &g_entities[i];

		if ( !d->active )
			continue;

		// it goes with the player, the next one in the slot starts without
		if ( !ent->inuse || !ent->client || ent->client->pers.connected == CON_DISCONNECTED ) {
			d->active = qfalse;
			continue;
		}
		// Not while the player is not in the game. The view of a dead player does not move, and a vehicle has
		// rules of its own for the angles.
		if ( ent->client->pers.connected != CON_CONNECTED || ent->client->sess.sessionTeam == TEAM_SPECTATOR
			|| ent->health <= 0 || level.intermissiontime || ent->client->ps.m_iVehicleNum ) {
			if ( !d->paused ) {
				Disorient_Still( ent, d );
				d->paused = qtrue;
			}
			continue;
		}
		d->paused = qfalse;

		if ( level.time >= d->nextLurch ) {
			d->nextLurch = level.time + Q_irand( DISORIENT_MIN_PAUSE, DISORIENT_MAX_PAUSE );
			Disorient_Lurch( ent, d );
		}
		Disorient_View( ent, d );
	}
}

qboolean G_DisorientActive( int clientNum ) {
	return (qboolean)( clientNum >= 0 && clientNum < MAX_CLIENTS && disorient[clientNum].active );
}

// switches it for this player, returns what it is now
qboolean G_DisorientToggle( gentity_t *ent ) {
	const int	num = ent-g_entities;
	disorient_t	*d;
	qboolean	on;

	if ( num < 0 || num >= MAX_CLIENTS || !ent->client )
		return qfalse;

	d = &disorient[num];
	on = (qboolean)!d->active;
	if ( d->active && !d->paused )
		Disorient_Still( ent, d );

	memset( d, 0, sizeof( *d ) );
	d->active = on;
	d->start = level.time;
	d->teleportBit = ent->client->ps.eFlags & EF_TELEPORT_BIT;
	d->nextLurch = level.time + 2000;
	return on;
}

/*
=================
Svcmd_Disorient_f

disorient <player>: server console and rcon
=================
*/
void Svcmd_Disorient_f( void ) {
	char		arg[MAX_TOKEN_CHARS];
	gclient_t	*cl;

	if ( trap->Argc() < 2 ) {
		trap->Print( "Usage: disorient <player>\n" );
		return;
	}
	trap->Argv( 1, arg, sizeof( arg ) );
	if ( !(cl = ClientForString( arg )) )
		return;

	trap->Print( "disorient %s for %s\n", G_DisorientToggle( &g_entities[cl - level.clients] ) ? "on" : "off", cl->pers.netname );
}
