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
Hallucinations for one player ("schizo"): he hears and sees things nobody else does, and now and then
something small really happens to him.

Server side only, the client code is untouched. Everything is made of stock pieces the client already
knows: sounds, effects, an obituary and a screen shake as temporary event entities that only go to that one
client (SVF_SINGLECLIENT), blaster bolts only he gets and that hit nothing but walls, chat and screen text
as server commands, and the damage fields of his player state.

On top of that he has a stalker: a real NPC that is only sent to him. It appears out of nowhere behind him,
stares at him and walks after him (the stock "follow the leader" behaviour with him as the leader), is gone
when he walks into it or after a while, and is behind him again when he looks away. For everybody else it does
not exist: it is not sent to them, it is not solid, takes no damage, has no weapon and no enemies.

Toggled per player in the web admin panel or with the server console command "schizo <player>". The state
is kept here and is gone with the map, like everything in the memory of the module. A spectator who follows
the player gets the same things, the engine sends him the view of the followed client.

Not from JA+ and not from JKAdmin.
*/

#include "game/g_local.h"

gclient_t *ClientForString( const char *s );	// g_svcmds.c
gentity_t *NPC_Spawn_Do( gentity_t *ent );		// NPC_spawn.c
void G_ClearEnemy( gentity_t *self );			// NPC_combat.c
void ChangeWeapon( gentity_t *ent, int newWeapon );

// SFB_NOTSOLID of b_local.h for the NPC spawner: the NPC is not solid, and NPC_Begin does not kill whoever
// stands where it appears (its G_KillBox is skipped with this flag)
#define SCHIZO_NPC_SPAWNFLAGS	64
#define SCHIZO_NPC_TOUCH		56.0f		// he walked into it
#define SCHIZO_NPC_FAR			260.0f		// further away and not in his view: it comes closer

#define SCHIZO_MIN_PAUSE	2500		// msec between two episodes
#define SCHIZO_MAX_PAUSE	9000
#define SCHIZO_BOLT_SPEED	2300.0f		// a blaster bolt

typedef enum {
	SE_SHOTS,			// a burst of blaster bolts passes him
	SE_SABER,			// a saber is switched on behind him and swung
	SE_FOOTSTEPS,		// steps come closer from behind
	SE_VOICE,			// somebody talks, breathes or screams
	SE_EXPLOSION,		// a detonator beeps and goes off next to him
	SE_CHAT,			// a chat line another player never wrote
	SE_WHISPER,			// text on his screen
	SE_OBITUARY,		// a kill that did not happen
	SE_HIT,				// he is hit by nothing
	SE_SABEROFF,		// his saber goes off (real)
	SE_HOP,				// his legs jump (real)
	SE_LOCKON,			// the lock tone of a rocket launcher
	SE_THUNDER,			// thunder, a flash and a shaking view
	SE_NUM
} schizoEpisode_t;

typedef struct schizo_s {
	qboolean	active;
	int			nextEpisode;	// level.time
	int			episode;		// the one that runs, -1 between two
	int			step, steps;	// done and to do
	int			nextStep;		// level.time
	vec3_t		org;			// where it comes from
	vec3_t		dir;			// from there to him

	int			stalker;		// entity number of his NPC, ENTITYNUM_NONE without one
	int			stalkerBorn;	// level.time it was spawned, NPC_Begin runs a frame later
	qboolean	stalkerShown;	// he gets it at the moment
	int			stalkerNext;	// level.time it appears, goes or is spawned
	int			stalkerCheck;	// level.time of the next look whether it lost him
} schizo_t;

static schizo_t schizo[MAX_CLIENTS];

// NPC types without a weapon, the first one that spawns is taken. ragnos is the ghost of the single player game.
static const char *const schizoNPCTypes[] = { "ragnos", "prisoner" };

typedef enum {
	SS_BLASTER, SS_SABER_ON, SS_SWING1, SS_SWING2, SS_STEP1, SS_STEP2, SS_BREATH, SS_SCREAMS, SS_TAUNT1,
	SS_TAUNT2, SS_TAUNT3, SS_BEEP, SS_EXPLODE, SS_LOCK, SS_THUNDER, SS_GRIP, SS_NUM
} schizoSound_t;

// all of these are in the assets of the game
static const char *const schizoSoundNames[SS_NUM] = {
	"sound/weapons/blaster/fire.mp3",
	"sound/weapons/saber/enemy_saber_on.mp3",
	"sound/weapons/saber/saberhup1.mp3",
	"sound/weapons/saber/saberhup5.mp3",
	"sound/player/footsteps/boot1.mp3",
	"sound/player/footsteps/boot2.mp3",
	"sound/chars/rancor/breath1.mp3",
	"sound/effects/student_screams.mp3",
	"sound/chars/cultist1/misc/taunt1.mp3",
	"sound/chars/alora/misc/gloat1.mp3",
	"sound/chars/boba/misc/taunt.mp3",
	"sound/weapons/thermal/warning.mp3",
	"sound/weapons/thermal/explode.mp3",
	"sound/weapons/rocket/lock.mp3",
	"sound/ambience/thunder_close1.mp3",
	"sound/weapons/force/grip.mp3",
};

typedef enum { SF_EXPLODE, SF_ELECTRIC, SF_NUM } schizoFx_t;

static const char *const schizoFxNames[SF_NUM] = {
	"env/small_explode",
	"env/small_electricity",
};

// configstring numbers, looked up when first needed through G_TryConfigstringIndex, which gives 0 and no error
// when the table of the map is full. 0: not yet, or full
static int schizoSounds[SS_NUM];
static int schizoFx[SF_NUM];

static const char *const schizoWhispers[] = {
	"They are right behind you.",
	"Did you hear that?",
	"Do not turn around.",
	"Nobody else can see it.",
	"You are not alone here.",
	"It knows your name.",
	"Stop. Listen.",
	"Something moved.",
};

// %s is the name of the player
static const char *const schizoChats[] = {
	"who is that standing behind %s^2?",
	"%s^2 why are you doing that",
	"lol look at %s",
	"%s^2 stop following me",
	"did anyone else hear that",
	"%s^2 who are you talking to?",
	"there is nobody there %s",
	"%s^2 behind you",
};

// a temporary entity that only this client gets, wherever he looks
static void Schizo_Only( gentity_t *te, int clientNum ) {
	te->r.svFlags |= SVF_SINGLECLIENT|SVF_BROADCAST;
	te->r.singleClient = clientNum;
}

static void Schizo_Sound( int clientNum, vec3_t origin, schizoSound_t sound ) {
	gentity_t *te;

	if ( !schizoSounds[sound] )
		schizoSounds[sound] = G_TryConfigstringIndex( schizoSoundNames[sound], CS_SOUNDS, MAX_SOUNDS );
	if ( !schizoSounds[sound] )
		return;

	// G_SoundAtLoc
	te = G_TempEntity( origin, EV_GENERAL_SOUND );
	te->s.eventParm = schizoSounds[sound];
	te->s.saberEntityNum = CHAN_AUTO;
	Schizo_Only( te, clientNum );
}

static void Schizo_Effect( int clientNum, vec3_t origin, schizoFx_t fx ) {
	vec3_t up = { -90, 0, 0 };

	if ( !schizoFx[fx] )
		schizoFx[fx] = G_TryConfigstringIndex( schizoFxNames[fx], CS_EFFECTS, MAX_FX );
	if ( !schizoFx[fx] )
		return;

	Schizo_Only( G_PlayEffectID( schizoFx[fx], origin, up ), clientNum );
}

/*
=================
Schizo_Spot

A point around the player: yaw relative to his view (180 is behind him), dist away, height above his eyes.
Pulled in front of a wall that is in between.
=================
*/
static void Schizo_Spot( gentity_t *ent, float yaw, float dist, float height, vec3_t out ) {
	vec3_t	eye, angles, fwd, want;
	trace_t	tr;

	VectorCopy( ent->client->ps.origin, eye );
	eye[2] += ent->client->ps.viewheight;

	VectorSet( angles, 0, ent->client->ps.viewangles[YAW] + yaw, 0 );
	AngleVectors( angles, fwd, NULL, NULL );
	VectorMA( eye, dist, fwd, want );
	want[2] += height;

	trap->Trace( &tr, eye, NULL, NULL, want, ent-g_entities, MASK_SOLID, qfalse, 0, 0 );
	VectorMA( tr.endpos, -16.0f, fwd, out );
}

// another player in the game, -1 if there is none. skip: one more who is not wanted
static int Schizo_OtherPlayer( int clientNum, int skip ) {
	int candidates[MAX_CLIENTS], n = 0, i;

	for ( i = 0; i < level.maxclients; i++ ) {
		if ( i != clientNum && i != skip && g_entities[i].inuse && level.clients[i].pers.connected == CON_CONNECTED )
			candidates[n++] = i;
	}
	return n ? candidates[Q_irand( 0, n - 1 )] : -1;
}

// one blaster bolt from s->org past his head. Only he gets it and it only hits the world.
static void Schizo_Bolt( gentity_t *ent, schizo_t *s ) {
	vec3_t		start, target, dir;
	gentity_t	*bolt;

	VectorCopy( s->org, start );
	VectorCopy( ent->client->ps.origin, target );
	target[0] += flrand( -40.0f, 40.0f );
	target[1] += flrand( -40.0f, 40.0f );
	target[2] += ent->client->ps.viewheight + flrand( -20.0f, 30.0f );
	VectorSubtract( target, start, dir );
	if ( VectorNormalize( dir ) < 32.0f )
		return;

	// WP_FireBlasterMissile without the damage
	bolt = CreateMissile( start, dir, SCHIZO_BOLT_SPEED, 3000, ent, qfalse );
	bolt->classname = "blaster_proj";
	bolt->s.weapon = WP_BLASTER;
	bolt->damage = 0;
	bolt->splashDamage = 0;
	bolt->methodOfDeath = MOD_BLASTER;
	bolt->clipmask = MASK_SOLID;
	bolt->bounceCount = 0;
	bolt->r.svFlags |= SVF_SINGLECLIENT;
	bolt->r.singleClient = ent-g_entities;

	Schizo_Sound( ent-g_entities, start, SS_BLASTER );
}

/*
==============================================================================

The stalker

==============================================================================
*/

// a place to stand around the player (see Schizo_Spot). qfalse if there is no floor or no room.
static qboolean Schizo_Floor( gentity_t *ent, float yaw, float dist, vec3_t out ) {
	vec3_t	top, bottom;
	vec3_t	mins = { -15, -15, -24 }, maxs = { 15, 15, 40 };	// a player
	trace_t	tr;

	Schizo_Spot( ent, yaw, dist, 0.0f, top );
	VectorCopy( top, bottom );
	bottom[2] -= 512.0f;

	trap->Trace( &tr, top, mins, maxs, bottom, ent-g_entities, MASK_PLAYERSOLID, qfalse, 0, 0 );
	if ( tr.startsolid || tr.allsolid || tr.fraction == 1.0f )
		return qfalse;

	VectorCopy( tr.endpos, out );
	return qtrue;
}

// the player has it in front of him with nothing in between
static qboolean Schizo_CanSee( gentity_t *ent, gentity_t *npc ) {
	vec3_t	eye, head, dir, fwd;
	trace_t	tr;

	VectorCopy( ent->client->ps.origin, eye );
	eye[2] += ent->client->ps.viewheight;
	VectorCopy( npc->r.currentOrigin, head );
	head[2] += 24.0f;

	VectorSubtract( head, eye, dir );
	VectorNormalize( dir );
	AngleVectors( ent->client->ps.viewangles, fwd, NULL, NULL );
	if ( DotProduct( dir, fwd ) < 0.4f )
		return qfalse;

	trap->Trace( &tr, eye, NULL, NULL, head, ent-g_entities, MASK_SOLID, qfalse, 0, 0 );
	return (qboolean)( tr.fraction == 1.0f );
}

// puts the NPC somewhere without the effects and the kill box of TeleportPlayer, facing the player
static void Schizo_PlaceNPC( gentity_t *npc, vec3_t origin, gentity_t *ent ) {
	vec3_t dir, angles;

	VectorSubtract( ent->client->ps.origin, origin, dir );
	VectorSet( angles, 0, vectoyaw( dir ), 0 );

	trap->UnlinkEntity( (sharedEntity_t *)npc );
	VectorCopy( origin, npc->client->ps.origin );
	VectorClear( npc->client->ps.velocity );
	npc->client->ps.eFlags ^= EF_TELEPORT_BIT;		// the client does not move it there, it is there
	SetClientViewAngle( npc, angles );
	npc->NPC->desiredYaw = angles[YAW];

	BG_PlayerStateToEntityState( &npc->client->ps, &npc->s, qtrue );
	npc->s.eType = ET_NPC;
	VectorCopy( origin, npc->r.currentOrigin );
	trap->LinkEntity( (sharedEntity_t *)npc );
}

/*
=================
Schizo_Harmless

Every frame: NPC_Begin and the AI set some of this again. Only the player gets it, nothing touches it, it has
no weapon and no enemy, and all it does is to follow him slowly.
=================
*/
static void Schizo_Harmless( gentity_t *npc, gentity_t *ent ) {
	npc->r.svFlags |= SVF_SINGLECLIENT;
	npc->r.singleClient = ent-g_entities;
	npc->r.contents = 0;
	npc->takedamage = qfalse;
	npc->flags |= FL_NOTARGET;

	npc->client->leader = ent;
	npc->client->playerTeam = npc->client->enemyTeam = NPCTEAM_NEUTRAL;
	if ( npc->enemy )
		G_ClearEnemy( npc );
	if ( npc->client->ps.weapon != WP_NONE ) {
		ChangeWeapon( npc, WP_NONE );
		npc->client->ps.stats[STAT_WEAPONS] = ( 1 << WP_NONE );
	}

	npc->NPC->behaviorState = npc->NPC->defaultBehavior = BS_FOLLOW_LEADER;
	npc->NPC->tempBehavior = BS_DEFAULT;
	npc->NPC->followDist = 90.0f;
	npc->NPC->scriptFlags |= SCF_WALKING|SCF_DONT_FIRE|SCF_NO_COMBAT_TALK|SCF_IGNORE_ALERTS;
	npc->NPC->scriptFlags &= ~(SCF_RUNNING|SCF_LOOK_FOR_ENEMIES|SCF_CHASE_ENEMIES);
}

// his NPC, NULL if there is none or the entity is not it any more
static gentity_t *Schizo_Stalker( schizo_t *s, gentity_t *ent ) {
	gentity_t *npc;

	if ( s->stalker < MAX_CLIENTS || s->stalker >= ENTITYNUM_MAX_NORMAL )
		return NULL;

	npc = &g_entities[s->stalker];
	if ( !npc->inuse || !npc->client || !npc->NPC || npc->client->leader != ent
		|| !(npc->r.svFlags & SVF_SINGLECLIENT) || npc->r.singleClient != ent-g_entities )
		return NULL;
	return npc;
}

static void Schizo_FreeStalker( schizo_t *s, gentity_t *ent ) {
	gentity_t *npc = Schizo_Stalker( s, ent );

	s->stalker = ENTITYNUM_NONE;
	s->stalkerShown = qfalse;
	if ( !npc )
		return;

	// player_die frees this helper of the NPC too
	if ( npc->NPC->tempGoal ) {
		G_FreeEntity( npc->NPC->tempGoal );
		npc->NPC->tempGoal = NULL;
	}
	G_FreeEntity( npc );
}

// the way NPC_SpawnType does it, at a place of our choice. The NPC starts hidden.
static void Schizo_SpawnStalker( gentity_t *ent, schizo_t *s ) {
	gentity_t	*spawner, *npc = NULL;
	vec3_t		spot;
	size_t		i;

	// G_Spawn raises an error when the entities run out, and an error ends a dedicated server
	if ( level.num_entities > ENTITYNUM_MAX_NORMAL - 128 )
		return;
	if ( !Schizo_Floor( ent, 180.0f + flrand( -60.0f, 60.0f ), flrand( 200.0f, 320.0f ), spot ) )
		return;

	for ( i = 0; i < ARRAY_LEN( schizoNPCTypes ) && !npc; i++ ) {
		spawner = G_Spawn();
		spawner->think = G_FreeEntity;
		spawner->nextthink = level.time + FRAMETIME;
		G_SetOrigin( spawner, spot );
		VectorCopy( spawner->r.currentOrigin, spawner->s.origin );
		spawner->s.angles[YAW] = ent->client->ps.viewangles[YAW];
		spawner->NPC_type = G_NewString( schizoNPCTypes[i] );
		spawner->count = 1;
		spawner->delay = 0;
		spawner->spawnflags = SCHIZO_NPC_SPAWNFLAGS;
		trap->LinkEntity( (sharedEntity_t *)spawner );

		npc = NPC_Spawn_Do( spawner );
	}
	if ( !npc || !npc->client || !npc->NPC )
		return;

	npc->r.svFlags |= SVF_SINGLECLIENT|SVF_NOCLIENT;
	npc->r.singleClient = ent-g_entities;
	npc->client->leader = ent;

	s->stalker = npc-g_entities;
	s->stalkerBorn = level.time;
	s->stalkerShown = qfalse;
	s->stalkerNext = level.time + Q_irand( 4000, 12000 );
}

/*
=================
Schizo_StalkerThink

Every frame for a player who is in the game
=================
*/
static void Schizo_StalkerThink( gentity_t *ent, schizo_t *s ) {
	gentity_t	*npc = Schizo_Stalker( s, ent );
	vec3_t		spot;
	float		dist;

	if ( !npc ) {
		s->stalker = ENTITYNUM_NONE;
		s->stalkerShown = qfalse;
		if ( level.time >= s->stalkerNext ) {
			s->stalkerNext = level.time + 10000;		// the next try if this one finds no place
			Schizo_SpawnStalker( ent, s );
		}
		return;
	}

	Schizo_Harmless( npc, ent );

	// NPC_Begin has to run before it can be moved
	if ( level.time < s->stalkerBorn + 500 )
		return;

	// the map killed it: a new one comes later
	if ( npc->health <= 0 ) {
		Schizo_FreeStalker( s, ent );
		s->stalkerNext = level.time + 15000;
		return;
	}

	if ( !s->stalkerShown ) {
		npc->r.svFlags |= SVF_NOCLIENT;
		if ( level.time < s->stalkerNext )
			return;

		// out of nowhere, behind him or at his side
		if ( !Schizo_Floor( ent, 180.0f + flrand( -80.0f, 80.0f ), flrand( 170.0f, 320.0f ), spot ) ) {
			s->stalkerNext = level.time + 2000;
			return;
		}
		Schizo_PlaceNPC( npc, spot, ent );
		npc->r.svFlags &= ~SVF_NOCLIENT;
		s->stalkerShown = qtrue;
		s->stalkerNext = level.time + Q_irand( 8000, 18000 );
		s->stalkerCheck = level.time + 2000;
		return;
	}

	dist = Distance( npc->r.currentOrigin, ent->client->ps.origin );

	// he walked into it, or its time is over: gone
	if ( dist < SCHIZO_NPC_TOUCH || level.time >= s->stalkerNext ) {
		if ( dist < SCHIZO_NPC_TOUCH )
			Schizo_Sound( ent-g_entities, ent->client->ps.origin, SS_SCREAMS );
		npc->r.svFlags |= SVF_NOCLIENT;
		s->stalkerShown = qfalse;
		s->stalkerNext = level.time + Q_irand( 6000, 20000 );
		return;
	}

	// he left it behind and does not look: it is behind him again, closer
	if ( level.time >= s->stalkerCheck ) {
		s->stalkerCheck = level.time + 2000;
		if ( dist > SCHIZO_NPC_FAR && !Schizo_CanSee( ent, npc )
			&& Schizo_Floor( ent, 180.0f + flrand( -50.0f, 50.0f ), flrand( 130.0f, 220.0f ), spot ) )
			Schizo_PlaceNPC( npc, spot, ent );
	}
}

/*
==============================================================================

The episodes

==============================================================================
*/

/*
=================
Schizo_Begin

Picks the next episode and sets it up. Episodes that need something that is not there fall back to a whisper.
=================
*/
static void Schizo_Begin( gentity_t *ent, schizo_t *s ) {
	gclient_t *client = ent->client;

	s->episode = Q_irand( 0, SE_NUM - 1 );
	s->step = 0;
	s->steps = 1;
	s->nextStep = level.time;

	switch ( s->episode ) {
	case SE_SHOTS:
		s->steps = Q_irand( 3, 7 );
		Schizo_Spot( ent, flrand( 0.0f, 360.0f ), flrand( 400.0f, 700.0f ), flrand( -10.0f, 60.0f ), s->org );
		break;
	case SE_SABER:
		s->steps = 3;
		Schizo_Spot( ent, 180.0f + flrand( -40.0f, 40.0f ), 70.0f, -10.0f, s->org );
		break;
	case SE_FOOTSTEPS:
		s->steps = 6;
		break;
	case SE_EXPLOSION:
		s->steps = 4;
		Schizo_Spot( ent, flrand( 0.0f, 360.0f ), flrand( 90.0f, 180.0f ), -30.0f, s->org );
		break;
	case SE_LOCKON:
		s->steps = 5;
		break;
	case SE_CHAT:
		if ( Schizo_OtherPlayer( ent-g_entities, -1 ) < 0 )
			s->episode = SE_WHISPER;
		break;
	case SE_OBITUARY:
		if ( Schizo_OtherPlayer( ent-g_entities, Schizo_OtherPlayer( ent-g_entities, -1 ) ) < 0 )
			s->episode = SE_WHISPER;
		break;
	case SE_SABEROFF:
		if ( client->ps.weapon != WP_SABER || client->ps.saberHolstered || client->ps.duelInProgress )
			s->episode = SE_WHISPER;
		break;
	case SE_HOP:
		if ( client->ps.groundEntityNum == ENTITYNUM_NONE || client->ps.duelInProgress )
			s->episode = SE_WHISPER;
		break;
	default:
		break;
	}
}

/*
=================
Schizo_Step

One step of the running episode
=================
*/
static void Schizo_Step( gentity_t *ent, schizo_t *s ) {
	const int	num = ent-g_entities;
	gclient_t	*client = ent->client;
	vec3_t		spot;
	int			a, b;

	switch ( s->episode ) {
	case SE_SHOTS:
		Schizo_Bolt( ent, s );
		s->nextStep = level.time + Q_irand( 90, 160 );
		break;

	case SE_SABER:
		Schizo_Sound( num, s->org, s->step == 0 ? SS_SABER_ON : ( s->step == 1 ? SS_SWING1 : SS_SWING2 ) );
		s->nextStep = level.time + ( s->step == 0 ? 900 : 450 );
		break;

	case SE_FOOTSTEPS:
		// from 420 units behind him up to his back
		Schizo_Spot( ent, 180.0f, 420.0f - s->step * 70.0f, -40.0f, spot );
		Schizo_Sound( num, spot, ( s->step & 1 ) ? SS_STEP2 : SS_STEP1 );
		s->nextStep = level.time + 320;
		break;

	case SE_VOICE:
		Schizo_Spot( ent, 180.0f + flrand( -60.0f, 60.0f ), flrand( 60.0f, 200.0f ), 0.0f, spot );
		switch ( Q_irand( 0, 5 ) ) {
		case 0:		Schizo_Sound( num, spot, SS_BREATH );	break;
		case 1:		Schizo_Sound( num, spot, SS_SCREAMS );	break;
		case 2:		Schizo_Sound( num, spot, SS_TAUNT1 );	break;
		case 3:		Schizo_Sound( num, spot, SS_TAUNT2 );	break;
		case 4:		Schizo_Sound( num, spot, SS_TAUNT3 );	break;
		default:	Schizo_Sound( num, spot, SS_GRIP );		break;
		}
		break;

	case SE_EXPLOSION:
		if ( s->step < 3 ) {
			Schizo_Sound( num, s->org, SS_BEEP );
			s->nextStep = level.time + 450;
		} else {
			Schizo_Effect( num, s->org, SF_EXPLODE );
			Schizo_Sound( num, s->org, SS_EXPLODE );
			Schizo_Only( G_ScreenShake( s->org, ent, 4.0f, 500, qfalse ), num );
		}
		break;

	case SE_CHAT:
		if ( (a = Schizo_OtherPlayer( num, -1 )) >= 0 ) {
			char text[MAX_SAY_TEXT];

			// the line G_SayTo sends for a say
			Com_sprintf( text, sizeof( text ), schizoChats[Q_irand( 0, ARRAY_LEN( schizoChats ) - 1 )], client->pers.netname );
			trap->SendServerCommand( num, va( "chat \"%s%c%c" EC ": %c%c%s\"", level.clients[a].pers.netname,
				Q_COLOR_ESCAPE, COLOR_WHITE, Q_COLOR_ESCAPE, COLOR_GREEN, text ) );
		}
		break;

	case SE_WHISPER:
		trap->SendServerCommand( num, va( "cp \"%s\n\"", schizoWhispers[Q_irand( 0, ARRAY_LEN( schizoWhispers ) - 1 )] ) );
		break;

	case SE_OBITUARY:
		a = Schizo_OtherPlayer( num, -1 );
		b = Schizo_OtherPlayer( num, a );
		if ( a >= 0 && b >= 0 ) {
			// the event of player_die, with two players who did nothing
			gentity_t *te = G_TempEntity( client->ps.origin, EV_OBITUARY );

			te->s.eventParm = MOD_SABER;
			te->s.otherEntityNum = a;
			te->s.otherEntityNum2 = b;
			Schizo_Only( te, num );
		}
		break;

	case SE_HIT:
		// what P_DamageFeedback sets: the client shows a hit from that side and kicks the view. No damage.
		client->ps.damageEvent++;
		client->ps.damageYaw = Q_irand( 0, 255 );
		client->ps.damagePitch = Q_irand( 96, 160 );
		client->ps.damageCount = Q_irand( 10, 40 );
		break;

	case SE_SABEROFF:
		if ( client->ps.weapon == WP_SABER && !client->ps.saberHolstered && !client->ps.duelInProgress )
			Cmd_ToggleSaber_f( ent );
		break;

	case SE_HOP:
		if ( client->ps.groundEntityNum != ENTITYNUM_NONE && !client->ps.duelInProgress )
			client->ps.velocity[2] = 270.0f;
		break;

	case SE_LOCKON:
		Schizo_Spot( ent, 0.0f, 8.0f, 0.0f, spot );
		Schizo_Sound( num, spot, SS_LOCK );
		s->nextStep = level.time + 260;
		break;

	case SE_THUNDER:
		Schizo_Spot( ent, flrand( 0.0f, 360.0f ), 160.0f, 40.0f, spot );
		Schizo_Sound( num, spot, SS_THUNDER );
		Schizo_Effect( num, spot, SF_ELECTRIC );
		Schizo_Only( G_ScreenShake( spot, ent, 3.0f, 700, qfalse ), num );
		break;

	default:
		break;
	}

	if ( ++s->step >= s->steps ) {
		s->episode = -1;
		s->nextEpisode = level.time + Q_irand( SCHIZO_MIN_PAUSE, SCHIZO_MAX_PAUSE );
	}
}

/*
=================
G_SchizoFrame

Once per server frame
=================
*/
void G_SchizoFrame( void ) {
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		schizo_t	*s = &schizo[i];
		gentity_t	*ent = &g_entities[i];

		if ( !s->active )
			continue;

		// it goes with the player, the next one in his slot starts without
		if ( !ent->inuse || !ent->client || ent->client->pers.connected == CON_DISCONNECTED ) {
			Schizo_FreeStalker( s, ent );
			s->active = qfalse;
			continue;
		}
		// nothing while he is not in the game, and his stalker is away
		if ( ent->client->pers.connected != CON_CONNECTED || ent->client->sess.sessionTeam == TEAM_SPECTATOR
			|| ent->health <= 0 || level.intermissiontime ) {
			gentity_t *npc = Schizo_Stalker( s, ent );

			if ( npc ) {
				Schizo_Harmless( npc, ent );
				npc->r.svFlags |= SVF_NOCLIENT;
				s->stalkerShown = qfalse;
			}
			s->episode = -1;
			continue;
		}

		Schizo_StalkerThink( ent, s );

		if ( s->episode < 0 ) {
			if ( level.time < s->nextEpisode )
				continue;
			Schizo_Begin( ent, s );
		}
		if ( level.time >= s->nextStep )
			Schizo_Step( ent, s );
	}
}

qboolean G_SchizoActive( int clientNum ) {
	return (qboolean)( clientNum >= 0 && clientNum < MAX_CLIENTS && schizo[clientNum].active );
}

// switches it for this player, returns what it is now
qboolean G_SchizoToggle( gentity_t *ent ) {
	const int	num = ent-g_entities;
	schizo_t	*s;

	if ( num < 0 || num >= MAX_CLIENTS || !ent->client )
		return qfalse;

	s = &schizo[num];
	if ( s->active )
		Schizo_FreeStalker( s, ent );
	s->active = (qboolean)!s->active;
	s->episode = -1;
	s->nextEpisode = level.time + 1500;
	s->stalker = ENTITYNUM_NONE;
	s->stalkerShown = qfalse;
	s->stalkerNext = level.time + Q_irand( 5000, 12000 );
	return s->active;
}

/*
=================
Svcmd_Schizo_f

schizo <player>: server console and rcon
=================
*/
void Svcmd_Schizo_f( void ) {
	char		arg[MAX_TOKEN_CHARS];
	gclient_t	*cl;

	if ( trap->Argc() < 2 ) {
		trap->Print( "Usage: schizo <player>\n" );
		return;
	}
	trap->Argv( 1, arg, sizeof( arg ) );
	if ( !(cl = ClientForString( arg )) )
		return;

	trap->Print( "schizo %s for %s\n", G_SchizoToggle( &g_entities[cl - level.clients] ) ? "on" : "off", cl->pers.netname );
}
