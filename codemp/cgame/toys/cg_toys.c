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
// cg_water.c, the remote controlled car in cg_rccar.c. Hold +v_snowball
// (Controls, Extras) and let go: the longer it was held, the further it flies. The ball is no entity of the server:
// this cgame lets it fly, and tells the others where it left the hand and how fast (game/toys/bg_toys.h). On a
// server of this tree that goes through the game module (vs_caps bit 9, toys/g_toys.c), on any other through the
// masterserver (VOICE_TOY, voice/cg_voice.c). Every cgame that hears of a throw lets the ball fly on its own, so
// where it lands can differ a little from one player to the next. It hurts nobody.
//
// A ball bursts on what it hits (effects/vision/snowball_hit.efx: powder, chunks, the crunch of snow) and leaves a
// splat on a wall or the floor that melts away like the other marks. Who is hit sees snow on his screen for a
// moment. v_toys 0: the toys of the others are not drawn. Not from JA+, see docs/japlus/toys.md.

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

static struct {
	toyBall_t	balls[TOY_BALLS];
	toySplat_t	splats[TOY_SPLATS];
	int			chargeStart;	// cg.time +v_snowball went down, 0: it is up
	int			lastThrow;
	int			helloTime;
	qhandle_t	ballShader, splatShader, screenShader;
	int			hitFx;
	sfxHandle_t	hitSound[3];
	qboolean	registered;
} toys;

static void Toys_Register( void ) {
	if ( toys.registered )
		return;
	toys.registered = qtrue;
	toys.ballShader = trap->R_RegisterShader( "gfx/vision/toys/snowball" );
	toys.splatShader = trap->R_RegisterShader( "gfx/vision/toys/snowsplat" );
	toys.screenShader = trap->R_RegisterShaderNoMip( "gfx/vision/toys/snowscreen" );
	toys.hitFx = trap->FX_RegisterEffect( "effects/vision/snowball_hit.efx" );
	toys.hitSound[0] = trap->S_RegisterSound( "sound/player/footsteps/snow_step1.mp3" );
	toys.hitSound[1] = trap->S_RegisterSound( "sound/player/footsteps/snow_step2.mp3" );
	toys.hitSound[2] = trap->S_RegisterSound( "sound/player/footsteps/snow_step3.mp3" );
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
}

// a throw of somebody else, from the game module or the masterserver: a snowball, or the jet of a water gun
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
	if ( CG_ToyCanUse() && !toys.chargeStart )
		toys.chargeStart = cg.time ? cg.time : 1;
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

// The ball hit something at tr: it bursts, and leaves snow on what it hit
static void Toys_Burst( const toyBall_t *ball, const trace_t *tr ) {
	vec3_t normal;

	VectorCopy( tr->plane.normal, normal );
	if ( VectorNormalize( normal ) < 0.5f ) {
		VectorScale( ball->velocity, -1.0f, normal );
		VectorNormalize( normal );
	}
	trap->FX_PlayEffectID( toys.hitFx, (float *)tr->endpos, normal, -1, -1, qfalse );
	if ( tr->entityNum < MAX_CLIENTS ) {
		// on a player: no mark, and the one who is hit gets snow in his face
		if ( tr->entityNum == cg.clientNum && cg.snap && cg.snap->ps.clientNum == cg.clientNum ) {
			Toys_SplatScreen();
			trap->S_StartLocalSound( toys.hitSound[rand() % 3], CHAN_LOCAL_SOUND );
		}
		return;
	}
	if ( tr->entityNum == ENTITYNUM_WORLD && !( tr->surfaceFlags & SURF_SKY ) ) {
		vec3_t light;

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
	for ( i = 0; i < TOY_BALLS; i++ ) {
		toyBall_t	*ball = &toys.balls[i];
		refEntity_t	ent;
		vec3_t		light;

		if ( !ball->active )
			continue;
		if ( !Toys_Move( ball ) ) {
			ball->active = qfalse;
			continue;
		}
		memset( &ent, 0, sizeof( ent ) );
		ent.reType = RT_SPRITE;
		VectorCopy( ball->origin, ent.origin );
		ent.radius = TOY_RADIUS + 0.5f;
		ent.rotation = ball->spin;
		ent.customShader = toys.ballShader;
		CG_ToyLight( ball->origin, light );
		ent.shaderRGBA[0] = (byte)( light[0] * 255.0f );
		ent.shaderRGBA[1] = (byte)( light[1] * 255.0f );
		ent.shaderRGBA[2] = (byte)( light[2] * 255.0f );
		ent.shaderRGBA[3] = 255;
		trap->R_AddRefEntityToScene( &ent );
	}
}

// once a frame: the masterserver hears now and then that this cgame draws toys
void CG_ToysFrame( void ) {
	trap->Cvar_Update( &v_toys );
	CG_WaterFrame();
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
}

// a new level or a new start of the cgame: nothing flies, nothing sticks
void CG_ToysInit( void ) {
	memset( &toys, 0, sizeof( toys ) );
	CG_WaterInit();
	CG_RcInit();
}
