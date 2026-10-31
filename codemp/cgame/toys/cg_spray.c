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

// cg_spray.c -- sprays, a toy of issue #9 that only players of this tree see, as in the games of Valve: v_spray
// (Controls, Extras) puts the picture of v_sprayImage on the wall the player looks at, upright, as bright as the
// light there. A player has one spray at a time, a new one takes the place of the old one, and it stays for the
// level. The pictures are the emoji of the chat (v_sprayImage heart is gfx/emoji/heart.png), the ones of
// gfx/vision/sprays.txt, whose line numbers go to the others (tools/toys/sprays.py writes it and a shader for each).
//
// The others hear of it as TOY_SPRAY (game/toys/bg_toys.h, the way a snowball goes, toys/cg_toys.c): the point,
// the angles of the wall and the number of the picture. The own spray is said again every SPRAY_AGAIN_MSEC, so a
// player who came later sees it too. Not from JA+, see docs/japlus/toys.md.

#include "cgame/cg_local.h"
#include "cgame/toys/cg_toys.h"

#define SPRAY_REACH			128.0f		// how far from the eyes a wall can be sprayed
#define SPRAY_RADIUS		24.0f
#define SPRAY_GAP_MSEC		2000		// between two own sprays at least
#define SPRAY_AGAIN_MSEC	45000		// the own spray is said again this often, for those who came later

typedef struct {
	qboolean	active;
	int			picture;		// line of gfx/vision/sprays.txt
	float		yaw;			// of the player who sprayed it: where the top of a picture on a floor points
	float		orientation;	// of the mark, so the picture is upright
	vec3_t		origin, normal;
} spray_t;

static struct {
	spray_t		sprays[MAX_CLIENTS];
	qhandle_t	shaders[TOY_SPRAY_MOST];
	int			lastSpray;
	int			againTime;
	sfxHandle_t	hiss;
} spray;

void CG_SprayInit( void ) {
	memset( &spray, 0, sizeof( spray ) );
}

/*
=================
Spray_Orientation

The turn of the mark (CG_ImpactMark) that has its picture upright: the rows of the picture run along the second
axis of the mark, so that axis goes down the wall. On a floor or a ceiling the top of the picture points away from
the player who sprayed it (along yaw).
=================
*/
static float Spray_Orientation( const vec3_t normal, float yaw ) {
	vec3_t	axis1, down, cross;

	PerpendicularVector( axis1, normal );
	if ( fabsf( normal[2] ) > 0.9f ) {
		vec3_t angles;

		VectorSet( angles, 0.0f, yaw, 0.0f );
		AngleVectors( angles, down, NULL, NULL );
		VectorScale( down, -1.0f, down );
	} else {
		VectorSet( down, 0.0f, 0.0f, -1.0f );
	}
	VectorMA( down, -DotProduct( down, normal ), normal, down );
	VectorNormalize( down );
	CrossProduct( axis1, down, cross );
	return RAD2DEG( atan2f( DotProduct( cross, normal ), DotProduct( axis1, down ) ) );
}

static void Spray_Put( int client, int picture, const vec3_t origin, const vec3_t normal, float yaw ) {
	spray_t *s = &spray.sprays[client];

	s->active = qtrue;
	s->picture = picture;
	VectorCopy( origin, s->origin );
	VectorCopy( normal, s->normal );
	s->yaw = yaw;
	s->orientation = Spray_Orientation( normal, yaw );
	if ( !spray.shaders[picture] )
		spray.shaders[picture] = trap->R_RegisterShader( va( "gfx/vision/spray/%i", picture ) );
	if ( !spray.hiss )
		spray.hiss = trap->S_RegisterSound( "sound/effects/air_burst.mp3" );
}

static void Spray_Send( void ) {
	const spray_t	*s = &spray.sprays[cg.clientNum];
	vec3_t			angles;
	toyThrow_t		toy;
	int				i;

	if ( !s->active )
		return;
	vectoangles( s->normal, angles );
	toy.kind = TOY_SPRAY;
	for ( i = 0; i < 3; i++ )
		toy.origin[i] = (int)floorf( s->origin[i] + 0.5f );
	toy.velocity[0] = (int)Com_Clamp( -90.0f, 90.0f, AngleNormalize180( angles[PITCH] ) );
	// (the yaw of a floor or a ceiling says nothing: there it is the one of the player, for the top of the picture)
	toy.velocity[1] = (int)AngleNormalize180( fabsf( s->normal[2] ) > 0.9f ? s->yaw : angles[YAW] );
	toy.velocity[2] = s->picture;
	if ( BG_ToyValid( &toy ) )
		CG_ToySend( &toy );
	spray.againTime = cg.time ? cg.time : 1;
}

// v_spray: the picture of v_sprayImage on the wall the player looks at
void CG_Spray_f( void ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	vec3_t				start, end, forward;
	trace_t				tr;
	int					picture;

	if ( !CG_ToyCanUse() || ( spray.lastSpray && cg.time - spray.lastSpray < SPRAY_GAP_MSEC && cg.time >= spray.lastSpray ) )
		return;
	picture = CG_ToyPicture( v_sprayImage.string );
	if ( picture < 0 ) {
		Com_Printf( "v_sprayImage names no picture of gfx/vision/sprays.txt (an emoji of the chat, as heart)\n" );
		return;
	}
	VectorCopy( ps->origin, start );
	start[2] += ps->viewheight;
	AngleVectors( cg.refdef.viewangles, forward, NULL, NULL );
	VectorMA( start, SPRAY_REACH, forward, end );
	CG_Trace( &tr, start, NULL, NULL, end, cg.clientNum, MASK_SOLID );
	if ( tr.fraction >= 1.0f || tr.entityNum != ENTITYNUM_WORLD || ( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT ) ) )
		return;
	spray.lastSpray = cg.time ? cg.time : 1;
	Spray_Put( cg.clientNum, picture, tr.endpos, tr.plane.normal, cg.refdef.viewangles[YAW] );
	trap->S_StartSound( tr.endpos, cg.clientNum, CHAN_AUTO, spray.hiss );
	Spray_Send();
}

// a spray of somebody else, from the game module or the masterserver
void CG_SprayHeard( int client, const vec3_t origin, const vec3_t velocity ) {
	vec3_t		angles, normal;
	int			picture = (int)velocity[2];
	qboolean	moved;

	if ( picture < 0 || picture >= CG_ToyPictures() )
		return;
	VectorSet( angles, velocity[0], velocity[1], 0.0f );
	AngleVectors( angles, normal, NULL, NULL );
	moved = (qboolean)( !spray.sprays[client].active || !VectorCompare( spray.sprays[client].origin, origin ) );
	Spray_Put( client, picture, origin, normal, velocity[1] );
	if ( moved )
		trap->S_StartSound( (float *)origin, client, CHAN_AUTO, spray.hiss );
}

// once a frame: the own spray is said again now and then, a spray of a player who left goes
void CG_SprayFrame( void ) {
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( spray.sprays[i].active && i != cg.clientNum && !cgs.clientinfo[i].infoValid )
			spray.sprays[i].active = qfalse;
	}
	if ( cg.clientNum >= 0 && cg.clientNum < MAX_CLIENTS && spray.sprays[cg.clientNum].active
		&& ( cg.time - spray.againTime >= SPRAY_AGAIN_MSEC || cg.time < spray.againTime ) ) {
		Spray_Send();
	}
}

// While the scene is built: every spray, drawn anew each frame as bright as the light where it is
void CG_SprayScene( void ) {
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const spray_t	*s = &spray.sprays[i];
		vec3_t			light;

		if ( !s->active || ( i != cg.clientNum && !v_toys.integer ) || !spray.shaders[s->picture] )
			continue;
		CG_ToyLight( s->origin, light );
		CG_ImpactMark( spray.shaders[s->picture], s->origin, s->normal, s->orientation, light[0], light[1], light[2], 1.0f,
			qfalse, SPRAY_RADIUS, qtrue );
	}
}
