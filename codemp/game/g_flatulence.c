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

/*
Flatulence for one player: every few seconds he breaks wind. Everybody near him hears it and sees a green
cloud come out behind him. Now and then it is a big one that shoves him forward.

Server side only, the client code is untouched: the sound and the cloud are the stock sound and effect
events, made of files of the game (the snorts of the rancor, the talk of the gonk droid, an air burst, and
the gas cloud of the noghri stick).

Toggled per player in the web admin panel or with the server console command "flatulence <player>". The
state is kept here and is gone with the map.

Not from JA+ and not from JKAdmin.
*/

#include "g_local.h"

gclient_t *ClientForString( const char *s );	// g_svcmds.c

#define FLATULENCE_MIN_PAUSE	1500		// msec between two
#define FLATULENCE_MAX_PAUSE	6000
#define FLATULENCE_BIG_ONE		6			// one in this many is a big one

static struct {
	qboolean	active;
	int			next;		// level.time
} flatulence[MAX_CLIENTS];

// all of these are in the assets of the game
static const char *const flatulenceSoundNames[] = {
	"sound/chars/rancor/snort_1.wav",
	"sound/chars/rancor/snort_2.wav",
	"sound/chars/rancor/snort_3.wav",
	"sound/chars/rancor/snort_4.wav",
	"sound/chars/gonk/misc/gonktalk1.mp3",
	"sound/effects/air_burst.mp3",
};
#define FLATULENCE_CLOUD	"noghri_stick/gas_cloud"

// configstring numbers, looked up when first needed. 0: not yet, or the table of the map is full
static int flatulenceSounds[ARRAY_LEN( flatulenceSoundNames )];
static int flatulenceCloud;

static void Flatulence_Sound( gentity_t *ent ) {
	const int n = Q_irand( 0, ARRAY_LEN( flatulenceSoundNames ) - 1 );

	if ( !flatulenceSounds[n] )
		flatulenceSounds[n] = G_TryConfigstringIndex( flatulenceSoundNames[n], CS_SOUNDS, MAX_SOUNDS );
	if ( flatulenceSounds[n] )
		G_Sound( ent, CHAN_AUTO, flatulenceSounds[n] );
}

static void Flatulence_Fart( gentity_t *ent, qboolean big ) {
	gclient_t	*client = ent->client;
	vec3_t		angles, fwd, org;
	int			i;

	VectorSet( angles, 0, client->ps.viewangles[YAW], 0 );
	AngleVectors( angles, fwd, NULL, NULL );

	// behind him, at the height of his hips
	VectorMA( client->ps.origin, -14.0f, fwd, org );
	org[2] += 2.0f;

	if ( !flatulenceCloud )
		flatulenceCloud = G_TryConfigstringIndex( FLATULENCE_CLOUD, CS_EFFECTS, MAX_FX );

	// the cloud goes backwards
	angles[YAW] += 180.0f;
	for ( i = 0; i < ( big ? 3 : 1 ); i++ ) {
		Flatulence_Sound( ent );
		if ( flatulenceCloud )
			G_PlayEffectID( flatulenceCloud, org, angles );
		VectorMA( org, -18.0f, fwd, org );
	}

	// a big one moves him. Not in a duel, that would decide fights.
	if ( big && !client->ps.duelInProgress ) {
		VectorMA( client->ps.velocity, 140.0f, fwd, client->ps.velocity );
		if ( client->ps.groundEntityNum != ENTITYNUM_NONE )
			client->ps.velocity[2] += 90.0f;
	}
}

/*
=================
G_FlatulenceFrame

Once per server frame
=================
*/
void G_FlatulenceFrame( void ) {
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *ent = &g_entities[i];

		if ( !flatulence[i].active )
			continue;

		// it goes with the player, the next one in his slot starts without
		if ( !ent->inuse || !ent->client || ent->client->pers.connected == CON_DISCONNECTED ) {
			flatulence[i].active = qfalse;
			continue;
		}
		if ( ent->client->pers.connected != CON_CONNECTED || ent->client->sess.sessionTeam == TEAM_SPECTATOR
			|| ent->health <= 0 || level.intermissiontime || level.time < flatulence[i].next )
			continue;

		flatulence[i].next = level.time + Q_irand( FLATULENCE_MIN_PAUSE, FLATULENCE_MAX_PAUSE );
		Flatulence_Fart( ent, (qboolean)( Q_irand( 1, FLATULENCE_BIG_ONE ) == 1 ) );
	}
}

qboolean G_FlatulenceActive( int clientNum ) {
	return (qboolean)( clientNum >= 0 && clientNum < MAX_CLIENTS && flatulence[clientNum].active );
}

// switches it for this player, returns what it is now
qboolean G_FlatulenceToggle( gentity_t *ent ) {
	const int num = ent-g_entities;

	if ( num < 0 || num >= MAX_CLIENTS || !ent->client )
		return qfalse;

	flatulence[num].active = (qboolean)!flatulence[num].active;
	flatulence[num].next = level.time + 500;
	return flatulence[num].active;
}

/*
=================
Svcmd_Flatulence_f

flatulence <player>: server console and rcon
=================
*/
void Svcmd_Flatulence_f( void ) {
	char		arg[MAX_TOKEN_CHARS];
	gclient_t	*cl;

	if ( trap->Argc() < 2 ) {
		trap->Print( "Usage: flatulence <player>\n" );
		return;
	}
	trap->Argv( 1, arg, sizeof( arg ) );
	if ( !(cl = ClientForString( arg )) )
		return;

	trap->Print( "flatulence %s for %s\n", G_FlatulenceToggle( &g_entities[cl - level.clients] ) ? "on" : "off", cl->pers.netname );
}
