/*
===========================================================================
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors
Copyright (C) 2026, VisionJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
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

// Disruptor Weapon

#include "cg_local.h"
#include "fx_local.h"

/*
---------------------------
FX_DisruptorMainShot
---------------------------
*/
static vec3_t WHITE={1.0f,1.0f,1.0f};

void FX_DisruptorMainShot( vec3_t start, vec3_t end )
{
	// JA+ client plugin: not while in a separated private duel
	if ( CG_JPInSeparatedDuel() )
		return;

//	vec3_t	dir;
//	float	len;

	trap->FX_AddLine( start, end, 0.1f, 6.0f, 0.0f,
							1.0f, 0.0f, 0.0f,
							WHITE, WHITE, 0.0f,
							150, trap->R_RegisterShader( "gfx/effects/redLine" ),
							FX_SIZE_LINEAR | FX_ALPHA_LINEAR );

//	VectorSubtract( end, start, dir );
//	len = VectorNormalize( dir );

//	FX_AddCylinder( start, dir, 5.0f, 5.0f, 0.0f,
//								5.0f, 5.0f, 0.0f,
//								len, len, 0.0f,
//								1.0f, 1.0f, 0.0f,
//								WHITE, WHITE, 0.0f,
//								400, cgi_R_RegisterShader( "gfx/effects/spiral" ), 0 );
}


/*
---------------------------
FX_DisruptorAltShot
---------------------------
*/
void FX_DisruptorAltShot( vec3_t start, vec3_t end, qboolean fullCharge )
{
	// JA+ client plugin: not while in a separated private duel
	if ( CG_JPInSeparatedDuel() )
		return;

	trap->FX_AddLine( start, end, 0.1f, 10.0f, 0.0f,
							1.0f, 0.0f, 0.0f,
							WHITE, WHITE, 0.0f,
							175, trap->R_RegisterShader( "gfx/effects/redLine" ),
							FX_SIZE_LINEAR | FX_ALPHA_LINEAR );

	if ( fullCharge )
	{
		vec3_t	YELLER={0.8f,0.7f,0.0f};

		// add some beef
		trap->FX_AddLine( start, end, 0.1f, 7.0f, 0.0f,
							1.0f, 0.0f, 0.0f,
							YELLER, YELLER, 0.0f,
							150, trap->R_RegisterShader( "gfx/misc/whiteline2" ),
							FX_SIZE_LINEAR | FX_ALPHA_LINEAR );
	}
}


/*
---------------------------
FX_DisruptorAltMiss
---------------------------
*/
#define FX_ALPHA_WAVE		0x00000008

void FX_DisruptorAltMiss( vec3_t origin, vec3_t normal )
{
	vec3_t pos, c1, c2;
	addbezierArgStruct_t b;

	VectorMA( origin, 4.0f, normal, c1 );
	VectorCopy( c1, c2 );
	c1[2] += 4;
	c2[2] += 12;

	VectorAdd( origin, normal, pos );
	pos[2] += 28;

	/*
	FX_AddBezier( origin, pos, c1, vec3_origin, c2, vec3_origin, 6.0f, 6.0f, 0.0f, 0.0f, 0.2f, 0.5f,
	WHITE, WHITE, 0.0f, 4000, trap->R_RegisterShader( "gfx/effects/smokeTrail" ), FX_ALPHA_WAVE );
	*/

	VectorCopy(origin, b.start);
	VectorCopy(pos, b.end);
	VectorCopy(c1, b.control1);
	VectorCopy(vec3_origin, b.control1Vel);
	VectorCopy(c2, b.control2);
	VectorCopy(vec3_origin, b.control2Vel);

	b.size1 = 6.0f;
	b.size2 = 6.0f;
	b.sizeParm = 0.0f;
	b.alpha1 = 0.0f;
	b.alpha2 = 0.2f;
	b.alphaParm = 0.5f;

	VectorCopy(WHITE, b.sRGB);
	VectorCopy(WHITE, b.eRGB);

	b.rgbParm = 0.0f;
	b.killTime = 4000;
	b.shader = trap->R_RegisterShader( "gfx/effects/smokeTrail" );
	b.flags = FX_ALPHA_WAVE;

	trap->FX_AddBezier(&b);

	trap->FX_PlayEffectID( cgs.effects.disruptorAltMissEffect, origin, normal, -1, -1, qfalse );
}

/*
---------------------------
FX_DisruptorAltHit
---------------------------
*/

void FX_DisruptorAltHit( vec3_t origin, vec3_t normal )
{
	trap->FX_PlayEffectID( cgs.effects.disruptorAltHitEffect, origin, normal, -1, -1, qfalse );
}



/*
---------------------------
FX_DisruptorHitWall
---------------------------
*/

void FX_DisruptorHitWall( vec3_t origin, vec3_t normal )
{
	// JA+ client plugin: not while in a separated private duel
	if ( CG_JPInSeparatedDuel() )
		return;

	trap->FX_PlayEffectID( cgs.effects.disruptorWallImpactEffect, origin, normal, -1, -1, qfalse );
}

/*
---------------------------
FX_DisruptorHitPlayer
---------------------------
*/

void FX_DisruptorHitPlayer( vec3_t origin, vec3_t normal, qboolean humanoid )
{
	// JA+ client plugin: not while in a separated private duel
	if ( CG_JPInSeparatedDuel() )
		return;

	trap->FX_PlayEffectID( cgs.effects.disruptorFleshImpactEffect, origin, normal, -1, -1, qfalse );
}

/*
---------------------------
FX_DisruptorBeam

tay_disruptorBeam (2026-10-17, TaystJK's cg_disruptorNew and cg_disruptorSpiral, after the rail of Quake 3): the shot
as a line with a glow, 1, and with a spiral of sparks around it, 2. The colour is tay_disruptorColor ("r g b"),
empty cyan, the team's in a team game. qfalse with 0: the shot of the game is drawn
---------------------------
*/
static void FX_BeamLine( const vec3_t start, const vec3_t end, const vec3_t color, float radius, float alpha, int life, qboolean tint ) {
	localEntity_t	*le = CG_AllocLocalEntity();
	refEntity_t		*re = &le->refEntity;

	le->leType = LE_FADE_RGB;
	le->startTime = cg.time;
	le->endTime = cg.time + life;
	le->lifeRate = 1.0f / ( le->endTime - le->startTime );
	re->shaderTime = cg.time / 1600.0f;
	re->reType = RT_LINE;
	re->radius = radius;
	re->customShader = trap->R_RegisterShader( "gfx/misc/whiteline2" );
	VectorCopy( start, re->origin );
	VectorCopy( end, re->oldorigin );
	re->shaderRGBA[0] = color[0] * 255;
	re->shaderRGBA[1] = color[1] * 255;
	re->shaderRGBA[2] = color[2] * 255;
	re->shaderRGBA[3] = 255;
	if ( tint ) {
		VectorScale( color, 0.75f, le->color );
	} else {
		VectorSet( le->color, 1.0f, 1.0f, 1.0f );
	}
	le->color[3] = alpha;
	AxisClear( re->axis );
}

qboolean FX_DisruptorBeam( int shooter, vec3_t start, vec3_t end, qboolean alt ) {
	const int	life = alt ? 350 : 175;
	vec3_t		color = { 0.0f, 0.878f, 1.0f };
	vec3_t		axis[36], dir, side, move;
	float		len;
	int			i, j;

	if ( !tay_disruptorBeam.integer ) {
		return qfalse;
	}
	if ( cgs.gametype >= GT_TEAM && shooter >= 0 && shooter < MAX_CLIENTS && cgs.clientinfo[shooter].team == TEAM_RED ) {
		VectorSet( color, 1.0f, 0.1f, 0.1f );
	} else if ( cgs.gametype >= GT_TEAM && shooter >= 0 && shooter < MAX_CLIENTS && cgs.clientinfo[shooter].team == TEAM_BLUE ) {
		VectorSet( color, 0.1f, 0.3f, 1.0f );
	} else if ( tay_disruptorColor.string[0] ) {
		sscanf( tay_disruptorColor.string, "%f %f %f", &color[0], &color[1], &color[2] );
	}

	FX_BeamLine( start, end, color, 3.0f, 1.0f, life, qtrue );		// glow
	FX_BeamLine( start, end, color, 1.0f, 0.6f, life, qfalse );		// core
	if ( tay_disruptorBeam.integer < 2 ) {
		return qtrue;
	}

	// sparks on a spiral of radius 4 around it, one every 5 units, a turn of 10 degrees each
	VectorSubtract( end, start, dir );
	len = VectorNormalize( dir );
	PerpendicularVector( side, dir );
	for ( i = 0; i < 36; i++ ) {
		RotatePointAroundVector( axis[i], dir, side, i * 10 );
	}
	VectorMA( start, 20.0f, dir, move );
	for ( i = 20, j = 18; i < len; i += 10, j = ( j + 2 ) % 36 ) {
		localEntity_t	*le = CG_AllocLocalEntity();
		refEntity_t		*re = &le->refEntity;

		le->leFlags = LEF_PUFF_DONT_SCALE;
		le->leType = LE_MOVE_SCALE_FADE;
		le->startTime = cg.time;
		le->endTime = cg.time + ( i >> 1 ) + (int)( life / 1.337f );
		le->lifeRate = 1.0f / ( le->endTime - le->startTime );
		re->shaderTime = cg.time / 2000.0f;
		re->reType = RT_SPRITE;
		re->radius = 1.8f;
		re->customShader = cgs.media.enlightenmentShader;
		re->renderfx |= RF_RGB_TINT;
		re->shaderRGBA[0] = color[0] * 255;
		re->shaderRGBA[1] = color[1] * 255;
		re->shaderRGBA[2] = color[2] * 255;
		re->shaderRGBA[3] = 255;
		VectorScale( color, 0.75f, le->color );
		le->color[3] = 1.0f;
		le->pos.trType = TR_LINEAR;
		le->pos.trTime = cg.time;
		VectorMA( move, 4.0f, axis[j], le->pos.trBase );
		VectorScale( axis[j], 6.0f, le->pos.trDelta );
		VectorMA( move, 10.0f, dir, move );
	}
	return qtrue;
}
