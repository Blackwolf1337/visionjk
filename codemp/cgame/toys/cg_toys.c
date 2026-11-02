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

// cg_toys.c -- the toys of issue #9 that only players of this tree see: the snowball here, the water gun in
// cg_water.c, the remote controlled car in cg_rccar.c, the sprays in cg_spray.c, the emoji sent to a player in
// cg_emote.c. Hold +v_snowball
// (Controls, Extras) and let go: the longer it was held, the further it flies. Meanwhile the ball lies in the hand
// (in the view in first person) and the player pulls back to throw, as with a thermal detonator; the throw is seen by
// everybody who hears of it. The ball is a lump of packed snow (models/vision/toys/snowball/ from
// tools/toys/snowball.py) that spins as it flies. It is no entity of the server:
// this cgame lets it fly, and tells the others where it left the hand and how fast (game/toys/bg_toys.h). On a
// server of this tree that goes through the game module (vs_caps bit 9, toys/g_toys.c), on any other through the
// masterserver (VOICE_TOY, voice/cg_voice.c). Every cgame that hears of a throw lets the ball fly on its own, so
// where it lands can differ a little from one player to the next. It hurts nobody.
//
// Its sounds are made by tools/toys/sounds.py: the snow packed in the hands, the throw, the burst on stone, on soft
// ground and on a player, and in the own face.
//
// A ball bursts on what it hits: powder (effects/vision/snowball_hit.efx) and lumps of snow that fly off, bounce, come
// to rest and melt (chunk1-3.md3). On a wall or the floor it leaves a splat of snow: with rd-vulkan-vision a thick
// layer of it with clumps, lit as snow and glinting, drawn by the renderer with the wet places (WET_SNOW, shaders/
// wet.frag), which on a wall slides down a little and leaves a smear, melts from its thin edges in and leaves the
// ground wet (CG_WaterSoak); elsewhere a mark that melts away like the others. Who is hit sees snow on his screen for
// a moment. v_toys 0: the toys of the others are not drawn. Not from JA+, see docs/japlus/toys.md.

#include "cgame/cg_local.h"
#include "cgame/toys/cg_toys.h"

#define TOY_BALLS			32			// balls in the air at once
#define TOY_SPLATS			4			// snow on the own screen at once
#define TOY_CHARGE_MSEC		800			// holding this long throws as far as it goes
#define TOY_SPEED_MIN		450.0f
#define TOY_SPEED_MAX		1150.0f
#define TOY_GAP_MSEC		300			// between two own throws at least (TOY_PER_SECOND)
#define TOY_GRAVITY			800.0f
#define TOY_RADIUS			3.0f
#define TOY_LIFE_MSEC		6000		// a ball that hit nothing by then is gone
#define TOY_OWN_MSEC		150			// so long a ball goes through the one who threw it
#define TOY_HELLO_MSEC		20000		// the masterserver is told this often that this cgame draws toys
#define TOY_SPLAT_MSEC		3000		// snow on the screen, the last third of it fades
#define TOY_THROW_MSEC		450			// the throw of the arm, after the ball left the hand
#define TOY_SNOWS			16			// splats of snow on the level at once (with WET_MAX_SPOTS of tr_types.h)
#define TOY_SNOW_MSEC		25000		// a splat melts in this time,
#define TOY_SLIDE_MSEC		2500		//	and on a wall slides down for this long
#define TOY_CHUNKS			48			// lumps of snow from the bursts at once
#define TOY_CHUNK_RADIUS	1.2f
#define TOY_VIEW_FORWARD	13.0f		// where the ball lies in the view while it is held: ahead,
#define TOY_VIEW_RIGHT		7.0f		//	to the right,
#define TOY_VIEW_UP			-6.0f		//	and down, pulled back as the throw is charged

typedef struct {
	qboolean	active;
	int			owner;			// the client who threw it
	int			start;			// cg.time of the throw
	int			time;			// cg.time it was moved to last
	vec3_t		origin;
	vec3_t		velocity;
	float		spin;
} toyBall_t;

typedef struct {
	int			start;			// cg.time it hit, 0: none
	float		x, y, size, angle;
} toySplat_t;

// a splat of snow on the level, drawn by the renderer (WET_SNOW)
typedef struct {
	int			start;			// cg.time it hit, 0: none
	vec3_t		hit, normal;	// where it hit, and the way the surface faces there
	float		radius, slide, seed;	// how far it slides down a wall
} toySnow_t;

// a lump of snow that flew off a burst
typedef struct {
	int			start, life;	// cg.time it flew off, 0: none, and msec it lasts
	int			time;			// cg.time it was moved to last
	int			model;
	qboolean	resting;
	float		scale;
	vec3_t		origin, velocity, angles, spin;
} toyChunk_t;

static struct {
	toyBall_t	balls[TOY_BALLS];
	toySplat_t	splats[TOY_SPLATS];
	toySnow_t	snows[TOY_SNOWS];
	toyChunk_t	chunks[TOY_CHUNKS];
	int			nextSnow, nextChunk;
	int			chargeStart;	// cg.time +v_snowball went down, 0: it is up
	int			lastThrow;
	int			helloTime;
	qhandle_t	ballModel, chunkModels[3], splatShader, screenShader;
	int			throwTime[MAX_CLIENTS];	// cg.time of the last throw of each player, for the throw of his arm
	int			hitFx;
	sfxHandle_t	packSound, throwSound, hardSound[3], softSound[2], bodySound, faceSound;
	qboolean	registered;
} toys;

// the pictures of sprays and emoji, gfx/vision/sprays.txt (tools/toys/sprays.py): the others hear of one as its line
static struct {
	qboolean	listed;
	int			count;
	char		names[TOY_SPRAY_MOST][TOY_PICTURE_NAME];
} toyPictures;

static void Toys_ListPictures( void ) {
	static char		text[TOY_SPRAY_MOST * 24];
	fileHandle_t	f;
	char			*p, *line;
	int				len;

	if ( toyPictures.listed )
		return;
	toyPictures.listed = qtrue;
	len = trap->FS_Open( "gfx/vision/sprays.txt", &f, FS_READ );
	if ( !f )
		return;
	len = trap->FS_Read( text, Q_min( len, (int)sizeof( text ) - 1 ), f );
	trap->FS_Close( f );
	text[Q_max( len, 0 )] = '\0';
	for ( line = text; *line && toyPictures.count < TOY_SPRAY_MOST; line = p ) {
		p = strchr( line, '\n' );
		if ( p )
			*p++ = '\0';
		else
			p = line + strlen( line );
		if ( strlen( line ) > 4 )
			Q_strncpyz( toyPictures.names[toyPictures.count++], line, sizeof( toyPictures.names[0] ) );
	}
}

int CG_ToyPictures( void ) {
	Toys_ListPictures();
	return toyPictures.count;
}

const char *CG_ToyPictureName( int picture ) {
	Toys_ListPictures();
	return picture >= 0 && picture < toyPictures.count ? toyPictures.names[picture] : "";
}

// the number of the picture a name names (with or without ".png"), -1: none
int CG_ToyPicture( const char *name ) {
	char	want[TOY_PICTURE_NAME];
	int		i;

	Toys_ListPictures();
	Q_strncpyz( want, name, sizeof( want ) - 4 );
	if ( !want[0] )
		return -1;
	if ( Q_stricmp( want + Q_max( 0, (int)strlen( want ) - 4 ), ".png" ) )
		Q_strcat( want, sizeof( want ), ".png" );
	for ( i = 0; i < toyPictures.count; i++ ) {
		if ( !Q_stricmp( toyPictures.names[i], want ) )
			return i;
	}
	return -1;
}

static void Toys_Register( void ) {
	if ( toys.registered )
		return;
	toys.registered = qtrue;
	toys.ballModel = trap->R_RegisterModel( "models/vision/toys/snowball/ball.md3" );
	toys.chunkModels[0] = trap->R_RegisterModel( "models/vision/toys/snowball/chunk1.md3" );
	toys.chunkModels[1] = trap->R_RegisterModel( "models/vision/toys/snowball/chunk2.md3" );
	toys.chunkModels[2] = trap->R_RegisterModel( "models/vision/toys/snowball/chunk3.md3" );
	toys.splatShader = trap->R_RegisterShader( "gfx/vision/toys/snowsplat" );
	toys.screenShader = trap->R_RegisterShaderNoMip( "gfx/vision/toys/snowscreen" );
	toys.hitFx = trap->FX_RegisterEffect( "effects/vision/snowball_hit.efx" );
	// (the sounds are made by tools/toys/sounds.py)
	toys.packSound = trap->S_RegisterSound( "sound/vision/toys/snow_pack.wav" );
	toys.throwSound = trap->S_RegisterSound( "sound/vision/toys/snow_throw.wav" );
	toys.hardSound[0] = trap->S_RegisterSound( "sound/vision/toys/snow_hard1.wav" );
	toys.hardSound[1] = trap->S_RegisterSound( "sound/vision/toys/snow_hard2.wav" );
	toys.hardSound[2] = trap->S_RegisterSound( "sound/vision/toys/snow_hard3.wav" );
	toys.softSound[0] = trap->S_RegisterSound( "sound/vision/toys/snow_soft1.wav" );
	toys.softSound[1] = trap->S_RegisterSound( "sound/vision/toys/snow_soft2.wav" );
	toys.bodySound = trap->S_RegisterSound( "sound/vision/toys/snow_body.wav" );
	toys.faceSound = trap->S_RegisterSound( "sound/vision/toys/snow_face.wav" );
}

// A ball starts to fly here. A ball of the same thrower that is still in the air stays.
static void Toys_Spawn( int owner, const vec3_t origin, const vec3_t velocity ) {
	toyBall_t	*ball = NULL;
	int			i, oldest = 0;

	Toys_Register();
	for ( i = 0; i < TOY_BALLS && !ball; i++ ) {
		if ( !toys.balls[i].active )
			ball = &toys.balls[i];
		else if ( toys.balls[i].start < toys.balls[oldest].start )
			oldest = i;
	}
	if ( !ball )
		ball = &toys.balls[oldest];
	memset( ball, 0, sizeof( *ball ) );
	ball->active = qtrue;
	ball->owner = owner;
	ball->start = ball->time = cg.time;
	ball->spin = Q_flrand( 0.0f, 360.0f );
	VectorCopy( origin, ball->origin );
	VectorCopy( velocity, ball->velocity );
	if ( owner >= 0 && owner < MAX_CLIENTS ) {
		toys.throwTime[owner] = cg.time ? cg.time : 1;
		trap->S_StartSound( (float *)origin, owner, CHAN_AUTO, toys.throwSound );
	}
}

// a throw of somebody else, from the game module or the masterserver: a snowball, the jet of a water gun, ...
static void Toys_Heard( int client, const toyThrow_t *toy ) {
	vec3_t origin, velocity;

	if ( !v_toys.integer || client < 0 || client >= MAX_CLIENTS || client == cg.clientNum )
		return;
	VectorSet( origin, (float)toy->origin[0], (float)toy->origin[1], (float)toy->origin[2] );
	VectorSet( velocity, (float)toy->velocity[0], (float)toy->velocity[1], (float)toy->velocity[2] );
	if ( toy->kind == TOY_SNOWBALL )
		Toys_Spawn( client, origin, velocity );
	else if ( toy->kind == TOY_WATER )
		CG_WaterHeard( client, origin, velocity );
	else if ( toy->kind == TOY_RC || toy->kind == TOY_RC_GONE )
		CG_RcHeard( client, toy->kind, origin, velocity );
	else if ( toy->kind == TOY_SPRAY || toy->kind == TOY_SPRAY_STORE )
		CG_SprayHeard( client, toy->kind, origin, velocity );
	else if ( toy->kind == TOY_EMOJI )
		CG_EmoteHeard( client, origin, velocity );
	else if ( toy->kind >= TOY_TABLE && toy->kind <= TOY_PONG_PADDLE )
		CG_PongHeard( client, toy->kind, origin, velocity );
	else if ( toy->kind == TOY_BOARD )
		CG_BoardHeard( client, velocity );
}

/*
=================
CG_ToyServerCommand

vs_toy <client> <kind> <x> <y> <z> <vx> <vy> <vz>, from the game module (toys/g_toys.c)
=================
*/
void CG_ToyServerCommand( void ) {
	toyThrow_t	toy;
	int			i;

	if ( trap->Cmd_Argc() != 9 )
		return;
	toy.kind = atoi( CG_Argv( 2 ) );
	for ( i = 0; i < 3; i++ ) {
		toy.origin[i] = atoi( CG_Argv( 3 + i ) );
		toy.velocity[i] = atoi( CG_Argv( 6 + i ) );
	}
	if ( BG_ToyValid( &toy ) )
		Toys_Heard( atoi( CG_Argv( 1 ) ), &toy );
}

// a VOICE_TOY the masterserver handed on (voice/cg_voice.c)
void CG_ToyFromMaster( int client, const byte *data, int len ) {
	toyThrow_t toy;

	if ( BG_ToyUnpack( data, len, &toy ) && toy.kind != TOY_HELLO )
		Toys_Heard( client, &toy );
}

// the own player can use a toy now: in the game, alive, playing
qboolean CG_ToyCanUse( void ) {
	const playerState_t *ps = &cg.predictedPlayerState;

	return (qboolean)( cg.snap && !cg.demoPlayback && cg.snap->ps.clientNum == cg.clientNum && ps->pm_type != PM_SPECTATOR
		&& ps->pm_type != PM_DEAD && ps->pm_type != PM_INTERMISSION && ps->stats[STAT_HEALTH] > 0 );
}

void CG_SnowballDown_f( void ) {
	if ( CG_ToyCanUse() && !toys.chargeStart ) {
		Toys_Register();
		toys.chargeStart = cg.time ? cg.time : 1;
		trap->S_StartLocalSound( toys.packSound, CHAN_LOCAL_SOUND );
	}
}

// +v_snowball let go: the ball leaves the hand, over the right shoulder in the direction of the view
void CG_SnowballUp_f( void ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	const int			held = toys.chargeStart ? cg.time - toys.chargeStart : 0;
	vec3_t				angles, forward, right, up, origin, velocity;
	toyThrow_t			toy;
	float				power, speed;
	int					i;

	toys.chargeStart = 0;
	if ( !held || !CG_ToyCanUse() || cg.time - toys.lastThrow < TOY_GAP_MSEC )
		return;
	toys.lastThrow = cg.time;
	power = Com_Clamp( 0.25f, 1.0f, (float)held / TOY_CHARGE_MSEC );
	speed = TOY_SPEED_MIN + ( TOY_SPEED_MAX - TOY_SPEED_MIN ) * power;

	VectorCopy( cg.refdef.viewangles, angles );
	angles[PITCH] -= 4.0f;		// a throw goes a little up
	AngleVectors( angles, forward, right, up );
	VectorCopy( ps->origin, origin );
	origin[2] += ps->viewheight;
	VectorMA( origin, 10.0f, forward, origin );
	VectorMA( origin, 6.0f, right, origin );
	VectorMA( origin, -4.0f, up, origin );
	VectorScale( forward, speed, velocity );
	VectorMA( velocity, 0.5f, ps->velocity, velocity );

	toy.kind = TOY_SNOWBALL;
	for ( i = 0; i < 3; i++ ) {
		toy.origin[i] = (int)origin[i];
		toy.velocity[i] = Com_Clampi( -TOY_MAX_SPEED, TOY_MAX_SPEED, (int)velocity[i] );
		velocity[i] = (float)toy.velocity[i];
	}
	if ( !BG_ToyValid( &toy ) )
		return;
	// the others get the same numbers this cgame flies with
	VectorSet( origin, (float)toy.origin[0], (float)toy.origin[1], (float)toy.origin[2] );
	Toys_Spawn( cg.clientNum, origin, velocity );
	CG_ToySend( &toy );
}

// how far the own throw is charged, 0 to 1; -1: no ball is held
static float Toys_Held( void ) {
	if ( !toys.chargeStart || !CG_ToyCanUse() )
		return -1.0f;
	return Com_Clamp( 0.0f, 1.0f, (float)( cg.time - toys.chargeStart ) / TOY_CHARGE_MSEC );
}

// whether the player holds a snowball now (the own one only: the others are heard when they throw), so his weapon is
// put away meanwhile
qboolean CG_SnowballHolding( int client ) {
	return (qboolean)( client == cg.clientNum && Toys_Held() >= 0.0f );
}

// The arm of a player: raised as a thermal detonator is held while the own ball is held, then the throw of a thermal
// (the throws of the others are seen when they are heard)
int CG_SnowballTorsoAnim( centity_t *cent, int anim ) {
	const int client = cent->currentState.number;

	if ( cent->currentState.eType != ET_PLAYER || client < 0 || client >= MAX_CLIENTS )
		return anim;
	if ( client == cg.clientNum && Toys_Held() >= 0.0f )
		return TORSO_WEAPONREADY10;
	if ( toys.throwTime[client] && cg.time - toys.throwTime[client] < TOY_THROW_MSEC && cg.time >= toys.throwTime[client] )
		return BOTH_THERMAL_THROW;
	return anim;
}

// the own ball in the right hand while it is held, seen in third person and in mirrors
void CG_SnowballHand( centity_t *cent ) {
	clientInfo_t	*ci;
	mdxaBone_t		bolt;
	refEntity_t		re;

	if ( cent->currentState.number != cg.clientNum || Toys_Held() < 0.0f || !cent->ghoul2 || !toys.ballModel )
		return;
	ci = &cgs.clientinfo[cg.clientNum];
	if ( ci->bolt_rhand < 0 )
		return;
	trap->G2API_GetBoltMatrix( cent->ghoul2, 0, ci->bolt_rhand, &bolt, cent->turAngles, cent->lerpOrigin, cg.time,
		cgs.gameModels, cent->modelScale );
	memset( &re, 0, sizeof( re ) );
	re.hModel = toys.ballModel;
	BG_GiveMeVectorFromMatrix( &bolt, ORIGIN, re.origin );
	VectorCopy( re.origin, re.lightingOrigin );
	AxisCopy( axisDefault, re.axis );
	re.renderfx = RF_LIGHTING_ORIGIN | ( cg.renderingThirdPerson ? 0 : RF_THIRD_PERSON );
	trap->R_AddRefEntityToScene( &re );
}

// the own ball in the view in first person while it is held, in place of the weapon: pulled back as it is charged
qboolean CG_SnowballViewBall( void ) {
	const float	held = Toys_Held();
	refEntity_t	re;
	vec3_t		angles;

	if ( held < 0.0f || !toys.ballModel )
		return qfalse;
	memset( &re, 0, sizeof( re ) );
	re.hModel = toys.ballModel;
	VectorCopy( cg.refdef.vieworg, re.origin );
	VectorMA( re.origin, TOY_VIEW_FORWARD - 4.0f * held, cg.refdef.viewaxis[0], re.origin );
	VectorMA( re.origin, -TOY_VIEW_RIGHT - 1.5f * held, cg.refdef.viewaxis[1], re.origin );
	VectorMA( re.origin, TOY_VIEW_UP + 1.0f * held + 0.3f * sinf( cg.time * 0.004f ), cg.refdef.viewaxis[2], re.origin );
	VectorSet( angles, 20.0f * held, cg.refdef.viewangles[YAW] + 30.0f, 0.0f );
	AnglesToAxis( angles, re.axis );
	VectorCopy( cg.refdef.vieworg, re.lightingOrigin );
	re.renderfx = RF_DEPTHHACK | RF_FIRST_PERSON | RF_MINLIGHT | RF_LIGHTING_ORIGIN;
	trap->R_AddRefEntityToScene( &re );
	return qtrue;
}

// A toy of the own player goes to the others: through the game module of a server of this tree, else through the
// masterserver
void CG_ToySend( const toyThrow_t *toy ) {
	if ( atoi( Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "vs_caps" ) ) & 512 ) {
		trap->SendClientCommand( va( "vs_toy %i %i %i %i %i %i %i", toy->kind, toy->origin[0], toy->origin[1], toy->origin[2],
			toy->velocity[0], toy->velocity[1], toy->velocity[2] ) );
	} else if ( CG_VoiceToys() ) {
		byte buf[TOY_BYTES];

		CG_VoiceSendToy( buf, BG_ToyPack( toy, buf ) );
	}
}

// snow on the own screen, where it hit from
static void Toys_SplatScreen( void ) {
	toySplat_t	*splat = &toys.splats[0];
	int			i;

	for ( i = 1; i < TOY_SPLATS; i++ ) {
		if ( toys.splats[i].start < splat->start )
			splat = &toys.splats[i];
	}
	splat->start = cg.time ? cg.time : 1;
	splat->size = Q_flrand( 200.0f, 320.0f );
	splat->x = Q_flrand( 60.0f, SCREEN_WIDTH - 60.0f );
	splat->y = Q_flrand( 60.0f, SCREEN_HEIGHT - 120.0f );
	splat->angle = Q_flrand( 0.0f, 360.0f );
}

// How bright a toy is at point, 0 to 1 for red, green and blue: the light of the level there (its light grid), so
// a ball, a splat or a jet of water in the dark are not white
void CG_ToyLight( const vec3_t point, vec3_t out ) {
	vec3_t	ambient, directed, dir, at;
	int		i;

	VectorCopy( point, at );
	if ( !trap->R_LightForPoint( at, ambient, directed, dir ) ) {
		VectorSet( out, 1.0f, 1.0f, 1.0f );
		return;
	}
	for ( i = 0; i < 3; i++ ) {
		out[i] = Com_Clamp( 0.15f, 1.0f, ( ambient[i] + directed[i] * 0.6f ) / 200.0f );
	}
}

// lumps of snow fly off a burst at "at": off the surface, a little the way the ball went, and round about
static void Toys_Chunks( const toyBall_t *ball, const vec3_t at, const vec3_t normal ) {
	const int	count = 5 + rand() % 4;
	vec3_t		on;
	int			i;

	// the speed of the ball along the surface
	VectorMA( ball->velocity, -DotProduct( ball->velocity, normal ), normal, on );
	for ( i = 0; i < count; i++ ) {
		toyChunk_t	*chunk = &toys.chunks[toys.nextChunk];
		int			k;

		toys.nextChunk = ( toys.nextChunk + 1 ) % TOY_CHUNKS;
		memset( chunk, 0, sizeof( *chunk ) );
		chunk->start = chunk->time = cg.time ? cg.time : 1;
		chunk->life = (int)Q_flrand( 5000.0f, 8000.0f );
		chunk->model = rand() % 3;
		chunk->scale = Q_flrand( 1.0f, 1.8f );
		VectorMA( at, 1.5f, normal, chunk->origin );
		VectorScale( normal, Q_flrand( 50.0f, 170.0f ), chunk->velocity );
		VectorMA( chunk->velocity, Q_flrand( 0.05f, 0.25f ), on, chunk->velocity );
		for ( k = 0; k < 3; k++ ) {
			chunk->velocity[k] += Q_flrand( -90.0f, 90.0f );
			chunk->angles[k] = Q_flrand( 0.0f, 360.0f );
			chunk->spin[k] = Q_flrand( -540.0f, 540.0f );
		}
	}
}

// A lump flies on to cg.time, bounces off what it hits, keeping little of its speed, and comes to rest on a floor
static void Toys_MoveChunk( toyChunk_t *chunk ) {
	static const vec3_t	mins = { -TOY_CHUNK_RADIUS, -TOY_CHUNK_RADIUS, -TOY_CHUNK_RADIUS };
	static const vec3_t	maxs = { TOY_CHUNK_RADIUS, TOY_CHUNK_RADIUS, TOY_CHUNK_RADIUS };
	const float			dt = Q_min( ( cg.time - chunk->time ) * 0.001f, 0.1f );
	vec3_t				end;
	trace_t				tr;

	chunk->time = cg.time;
	if ( chunk->resting || dt <= 0.0f )
		return;
	chunk->velocity[2] -= TOY_GRAVITY * dt;
	VectorMA( chunk->origin, dt, chunk->velocity, end );
	CG_Trace( &tr, chunk->origin, mins, maxs, end, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.startsolid ) {
		chunk->resting = qtrue;
		return;
	}
	VectorCopy( tr.endpos, chunk->origin );
	VectorMA( chunk->angles, dt, chunk->spin, chunk->angles );
	if ( tr.fraction < 1.0f ) {
		const float	into = DotProduct( chunk->velocity, tr.plane.normal );
		vec3_t		along;

		// a quarter of the speed into it comes back, of the speed along it a half stays
		VectorMA( chunk->velocity, -into, tr.plane.normal, along );
		VectorScale( along, 0.5f, along );
		VectorMA( along, -0.25f * into, tr.plane.normal, chunk->velocity );
		VectorScale( chunk->spin, 0.5f, chunk->spin );
		if ( tr.plane.normal[2] > 0.7f && VectorLength( chunk->velocity ) < 45.0f )
			chunk->resting = qtrue;
	}
}

// the lumps, moved and drawn, smaller as they melt in the last half of their time
static void Toys_AddChunks( void ) {
	int i;

	for ( i = 0; i < TOY_CHUNKS; i++ ) {
		toyChunk_t	*chunk = &toys.chunks[i];
		const int	age = cg.time - chunk->start;
		refEntity_t	ent;
		float		size;

		if ( !chunk->start )
			continue;
		if ( age >= chunk->life || age < 0 ) {
			chunk->start = 0;
			continue;
		}
		Toys_MoveChunk( chunk );
		size = chunk->scale * ( 1.0f - Q_max( 0.0f, ( age - chunk->life * 0.5f ) / ( chunk->life * 0.5f ) ) );
		if ( size < 0.05f )
			continue;
		memset( &ent, 0, sizeof( ent ) );
		ent.reType = RT_MODEL;
		ent.hModel = toys.chunkModels[chunk->model];
		VectorCopy( chunk->origin, ent.origin );
		VectorCopy( chunk->origin, ent.lightingOrigin );
		AnglesToAxis( chunk->angles, ent.axis );
		VectorScale( ent.axis[0], size, ent.axis[0] );
		VectorScale( ent.axis[1], size, ent.axis[1] );
		VectorScale( ent.axis[2], size, ent.axis[2] );
		ent.nonNormalizedAxes = qtrue;
		ent.renderfx = RF_LIGHTING_ORIGIN;
		trap->R_AddRefEntityToScene( &ent );
	}
}

// The splats of snow for the renderer: on a wall slid down from where they hit, slower as they go, melting
static void Toys_AddSnows( void ) {
	wetSpot_t	spots[TOY_SNOWS];
	int			i, count = 0;

	for ( i = 0; i < TOY_SNOWS; i++ ) {
		toySnow_t	*snow = &toys.snows[i];
		const int	age = cg.time - snow->start;
		wetSpot_t	*spot;
		vec3_t		down;
		float		f;

		if ( !snow->start )
			continue;
		if ( age >= TOY_SNOW_MSEC || age < 0 ) {
			snow->start = 0;
			continue;
		}
		spot = &spots[count++];
		memset( spot, 0, sizeof( *spot ) );
		// down along the wall
		VectorSet( down, 0.0f, 0.0f, -1.0f );
		VectorMA( down, -DotProduct( down, snow->normal ), snow->normal, down );
		VectorNormalize( down );
		f = Q_min( 1.0f, (float)age / TOY_SLIDE_MSEC );
		VectorMA( snow->hit, snow->slide * ( 1.0f - ( 1.0f - f ) * ( 1.0f - f ) ), down, spot->origin );
		VectorCopy( snow->hit, spot->hit );
		VectorCopy( snow->normal, spot->normal );
		spot->radius = snow->radius;
		// (it melts slowly at first, faster as it gets thin)
		f = (float)age / TOY_SNOW_MSEC;
		spot->wet = 1.0f - f * f * ( 3.0f - 2.0f * f );
		spot->seed = snow->seed;
		spot->kind = WET_SNOW;
	}
	if ( count )
		trap->ext.R_AddWet( spots, count );
}

// The ball hit something at tr: it bursts, and leaves snow on what it hit
static void Toys_Burst( const toyBall_t *ball, const trace_t *tr ) {
	vec3_t normal;

	VectorCopy( tr->plane.normal, normal );
	if ( VectorNormalize( normal ) < 0.5f ) {
		VectorScale( ball->velocity, -1.0f, normal );
		VectorNormalize( normal );
	}
	trap->FX_PlayEffectID( toys.hitFx, (float *)tr->endpos, normal, -1, -1, qfalse );
	Toys_Chunks( ball, tr->endpos, normal );
	if ( tr->entityNum >= MAX_CLIENTS ) {
		// soft ground takes it deeper and duller than stone and metal
		const int m = tr->surfaceFlags & MATERIAL_MASK;

		if ( m == MATERIAL_SNOW || m == MATERIAL_SAND || m == MATERIAL_DIRT || m == MATERIAL_MUD || m == MATERIAL_SHORTGRASS
			|| m == MATERIAL_LONGGRASS || m == MATERIAL_DRYLEAVES || m == MATERIAL_CARPET || m == MATERIAL_FABRIC )
			trap->S_StartSound( (float *)tr->endpos, ENTITYNUM_WORLD, CHAN_AUTO, toys.softSound[rand() & 1] );
		else
			trap->S_StartSound( (float *)tr->endpos, ENTITYNUM_WORLD, CHAN_AUTO, toys.hardSound[rand() % 3] );
	}
	if ( tr->entityNum < MAX_CLIENTS ) {
		// on a player: no mark, and the one who is hit gets snow in his face
		if ( tr->entityNum == cg.clientNum && cg.snap && cg.snap->ps.clientNum == cg.clientNum ) {
			if ( !CG_ScreenFxSnow( ball->velocity ) )
				Toys_SplatScreen();
			trap->S_StartLocalSound( toys.faceSound, CHAN_LOCAL_SOUND );
		} else {
			trap->S_StartSound( (float *)tr->endpos, ENTITYNUM_WORLD, CHAN_AUTO, toys.bodySound );
		}
		return;
	}
	if ( tr->entityNum == ENTITYNUM_WORLD && !( tr->surfaceFlags & SURF_SKY ) ) {
		vec3_t light;

		if ( CG_WaterWetDrawn() ) {
			toySnow_t	*snow = &toys.snows[toys.nextSnow];
			vec3_t		on;

			// (the ball is traced as a box: on the surface itself, where it hit)
			VectorMA( tr->endpos, -( DotProduct( tr->endpos, tr->plane.normal ) - tr->plane.dist ), tr->plane.normal, on );
			toys.nextSnow = ( toys.nextSnow + 1 ) % TOY_SNOWS;
			snow->start = cg.time ? cg.time : 1;
			snow->radius = Q_flrand( 4.5f, 6.5f );
			snow->slide = fabsf( normal[2] ) < 0.5f ? snow->radius * Q_flrand( 0.3f, 1.1f ) : 0.0f;
			snow->seed = Q_flrand( 0.0f, 1.0f );
			VectorCopy( on, snow->hit );
			VectorCopy( normal, snow->normal );
			CG_WaterSoak( on, normal, snow->radius * 1.4f );
			return;
		}
		CG_ToyLight( tr->endpos, light );
		CG_ImpactMark( toys.splatShader, tr->endpos, normal, Q_flrand( 0.0f, 360.0f ), light[0], light[1], light[2], 1.0f, qtrue,
			Q_flrand( 7.0f, 11.0f ), qfalse );
	}
}

// moves a ball to cg.time: qfalse, it is gone
static qboolean Toys_Move( toyBall_t *ball ) {
	static const vec3_t	mins = { -TOY_RADIUS, -TOY_RADIUS, -TOY_RADIUS }, maxs = { TOY_RADIUS, TOY_RADIUS, TOY_RADIUS };
	float				dt = ( cg.time - ball->time ) * 0.001f;
	vec3_t				end;
	trace_t				tr;

	if ( cg.time - ball->start > TOY_LIFE_MSEC || ball->origin[2] < -TOY_MAX_COORD )
		return qfalse;
	if ( dt <= 0.0f )
		return qtrue;
	// (a long frame, or the game was paused: no ball jumps through a wall)
	dt = Q_min( dt, 0.1f );
	ball->time = cg.time;
	VectorMA( ball->origin, dt, ball->velocity, end );
	end[2] -= 0.5f * TOY_GRAVITY * dt * dt;
	ball->velocity[2] -= TOY_GRAVITY * dt;
	CG_Trace( &tr, ball->origin, mins, maxs, end, cg.time - ball->start < TOY_OWN_MSEC ? ball->owner : ENTITYNUM_NONE, MASK_SHOT );
	if ( tr.startsolid || tr.fraction < 1.0f ) {
		if ( !tr.startsolid )
			Toys_Burst( ball, &tr );
		return qfalse;
	}
	VectorCopy( end, ball->origin );
	ball->spin += dt * 540.0f;
	return qtrue;
}

/*
=================
CG_ToysScene

While the scene of the frame is built: the balls fly on and are drawn
=================
*/
void CG_ToysScene( void ) {
	int i;

	CG_WaterScene();
	CG_RcScene();
	CG_SprayScene();
	CG_EmoteScene();
	CG_PongScene();
	CG_BoardScene();
	if ( toys.registered ) {
		Toys_AddChunks();
		Toys_AddSnows();
	}
	for ( i = 0; i < TOY_BALLS; i++ ) {
		toyBall_t	*ball = &toys.balls[i];
		refEntity_t	ent;
		vec3_t		angles;

		if ( !ball->active )
			continue;
		if ( !Toys_Move( ball ) ) {
			ball->active = qfalse;
			continue;
		}
		// it spins about the axis across its flight
		memset( &ent, 0, sizeof( ent ) );
		ent.reType = RT_MODEL;
		ent.hModel = toys.ballModel;
		VectorCopy( ball->origin, ent.origin );
		VectorCopy( ball->origin, ent.lightingOrigin );
		vectoangles( ball->velocity, angles );
		angles[PITCH] += ball->spin;
		angles[ROLL] = ball->spin * 0.3f;
		AnglesToAxis( angles, ent.axis );
		ent.renderfx = RF_LIGHTING_ORIGIN;
		trap->R_AddRefEntityToScene( &ent );
	}
}

// once a frame: the masterserver hears now and then that this cgame draws toys
void CG_ToysFrame( void ) {
	trap->Cvar_Update( &v_toys );
	CG_WaterFrame();
	CG_SprayFrame();
	if ( toys.chargeStart && !CG_ToyCanUse() )
		toys.chargeStart = 0;
	if ( v_toys.integer && CG_VoiceToys() && ( !toys.helloTime || cg.time - toys.helloTime >= TOY_HELLO_MSEC || cg.time < toys.helloTime ) ) {
		toyThrow_t	hello;
		byte		buf[TOY_BYTES];

		memset( &hello, 0, sizeof( hello ) );
		hello.kind = TOY_HELLO;
		CG_VoiceSendToy( buf, BG_ToyPack( &hello, buf ) );
		toys.helloTime = cg.time ? cg.time : 1;
	}
}

/*
=================
CG_ToysDraw

In the 2D of the HUD: the snow on the own screen, which slides down a little and melts away
=================
*/
void CG_ToysDraw( void ) {
	int i;

	for ( i = 0; i < TOY_SPLATS; i++ ) {
		const toySplat_t	*splat = &toys.splats[i];
		const int			age = cg.time - splat->start;
		vec4_t				color = { 1.0f, 1.0f, 1.0f, 1.0f };
		float				slide;

		if ( !splat->start || age < 0 || age >= TOY_SPLAT_MSEC )
			continue;
		if ( age > TOY_SPLAT_MSEC * 2 / 3 )
			color[3] = 1.0f - (float)( age - TOY_SPLAT_MSEC * 2 / 3 ) / ( TOY_SPLAT_MSEC / 3 );
		slide = age * 0.012f;
		trap->R_SetColor( color );
		CG_DrawRotatePic2( splat->x, splat->y + slide, splat->size, splat->size, splat->angle, toys.screenShader );
	}
	trap->R_SetColor( NULL );
	CG_EmoteDraw();
}

// a new level or a new start of the cgame: nothing flies, nothing sticks
void CG_ToysInit( void ) {
	memset( &toys, 0, sizeof( toys ) );
	CG_WaterInit();
	CG_RcInit();
	CG_SprayInit();
	CG_EmoteInit();
	CG_PongInit();
	CG_BoardInit();
}
