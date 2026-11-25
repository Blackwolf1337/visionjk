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

// cg_boltlook.c -- the bolts of the blasters (issue #22, item 12): a bright core, a soft glow and a short trail that
// fades in the air, over the stock effect of the shot and in the colour of the light of the bolt (cg_boltlight.c).
// The trail is the way the bolt came since it was fired or bounced, cut to a length that goes with its speed, so it
// looks the same at any frame rate. Once a frame of every missile, it also hands the missile to the other looks that
// follow one (the ring behind the concussion missile, the shards of the flechette).

#include "cgame/cg_local.h"
#include "cgame/weapons/cg_boltlook.h"
#include "cgame/weapons/cg_boltlight.h"
#include "cgame/weapons/cg_airring.h"
#include "cgame/weapons/cg_wallfx.h"

#define TRAIL_SEGMENTS		5
#define TRAIL_SECONDS		0.05f		// of its flight the trail shows
#define TRAIL_MAX			110.0f		// units, at scale 1
#define BOLT_MIN_SPEED		200.0f		// slower: it lies or sticks somewhere, no bolt
#define BOLT_FAR			4000.0f

static qhandle_t	flareShader, lineShader;

void CG_BoltLookRegister( void ) {
	flareShader = trap->R_RegisterShader( "gfx/effects/whiteFlare" );	// (of the game, additive)
	lineShader = trap->R_RegisterShader( "gfx/misc/whiteLine2" );		// (of the game, additive, glows)
}

// how large the bolt of this weapon is drawn, 0: it has no bolt of this kind
static float Bolt_Scale( const entityState_t *s, qboolean altFire ) {
	switch ( s->weapon ) {
	case WP_BRYAR_PISTOL:
	case WP_BRYAR_OLD:
		// (a charged shot: more of it, generic1 is its charge from 1 to 5)
		return altFire ? 1.0f + 0.2f * Com_Clamp( 0.0f, 4.0f, (float)( s->generic1 - 1 ) ) : 1.0f;
	case WP_BLASTER:
	case WP_TURRET:
	case WP_EMPLACED_GUN:
		return 1.0f;
	case WP_BOWCASTER:
		return 1.1f;
	case WP_REPEATER:
		return altFire ? 0.0f : 0.75f;
	default:
		return 0.0f;
	}
}

static void Bolt_Sprite( const vec3_t at, float radius, const vec3_t color, float level ) {
	refEntity_t	re;
	int			i;

	memset( &re, 0, sizeof( re ) );
	re.reType = RT_SPRITE;
	VectorCopy( at, re.origin );
	re.radius = radius;
	re.customShader = flareShader;
	for ( i = 0; i < 3; i++ ) {
		re.shaderRGBA[i] = (byte)Com_Clamp( 0.0f, 255.0f, color[i] * level * 255.0f );
	}
	re.shaderRGBA[3] = 255;
	re.renderfx = RF_NOSHADOW;
	trap->R_AddRefEntityToScene( &re );
}

static void Bolt_Line( const vec3_t from, const vec3_t to, float radius, const vec3_t color, float level ) {
	refEntity_t	re;
	int			i;

	memset( &re, 0, sizeof( re ) );
	re.reType = RT_LINE;
	VectorCopy( from, re.origin );
	VectorCopy( to, re.oldorigin );
	re.radius = radius;
	re.customShader = lineShader;
	re.shaderTexCoord[0] = re.shaderTexCoord[1] = 1.0f;
	for ( i = 0; i < 3; i++ ) {
		re.shaderRGBA[i] = (byte)Com_Clamp( 0.0f, 255.0f, color[i] * level * 255.0f );
	}
	re.shaderRGBA[3] = 255;
	re.renderfx = RF_NOSHADOW;
	trap->R_AddRefEntityToScene( &re );
}

static void Bolt_Draw( const centity_t *cent, const vec3_t color, float scale ) {
	const entityState_t	*s = &cent->currentState;
	vec3_t				dir, white, from, to;
	float				speed, length;
	int					i;

	speed = VectorNormalize2( s->pos.trDelta, dir );
	if ( speed < BOLT_MIN_SPEED || DistanceSquared( cent->lerpOrigin, cg.refdef.vieworg ) > BOLT_FAR * BOLT_FAR ) {
		return;
	}
	for ( i = 0; i < 3; i++ ) {
		white[i] = 0.35f * color[i] + 0.65f;
	}
	Bolt_Sprite( cent->lerpOrigin, 2.4f * scale, white, 1.0f );
	Bolt_Sprite( cent->lerpOrigin, 9.0f * scale, color, 0.35f );

	// (trBase is where it was fired or last bounced: no trail through the wall behind it)
	length = Q_min( speed * TRAIL_SECONDS, TRAIL_MAX * scale );
	if ( s->pos.trType == TR_LINEAR || s->pos.trType == TR_LINEAR_STOP ) {
		length = Q_min( length, Distance( cent->lerpOrigin, s->pos.trBase ) );
	}
	if ( length < 4.0f ) {
		return;
	}
	VectorMA( cent->lerpOrigin, -1.5f * scale, dir, from );
	for ( i = 0; i < TRAIL_SEGMENTS; i++ ) {
		const float	f = ( i + 1 ) / (float)TRAIL_SEGMENTS;
		const float	mid = 1.0f - ( i + 0.5f ) / TRAIL_SEGMENTS;

		VectorMA( cent->lerpOrigin, -length * f, dir, to );
		Bolt_Line( from, to, 1.6f * scale * ( 1.0f - 0.5f * f ), color, 0.85f * mid * mid );
		VectorCopy( to, from );
	}
}

void CG_MissileLooks( const centity_t *cent ) {
	const entityState_t	*s = &cent->currentState;
	const qboolean		altFire = (qboolean)!!( s->eFlags & EF_ALT_FIRING );
	vec3_t				color;
	float				scale;

	if ( !v_weaponImpacts.integer || CG_JPInSeparatedDuel() ) {
		return;
	}
	CG_WallFxTrack( cent );
	if ( s->weapon == WP_CONCUSSION && !altFire ) {
		CG_AirRingTrail( cent );
		return;
	}
	// JA+: a pistol missile that names a player or client 32 is the grappling hook (FX_BryarProjectileThink)
	if ( s->weapon == WP_BRYAR_PISTOL && cgs.jpVersion > 0.0f && s->otherEntityNum >= 0 && s->otherEntityNum <= MAX_CLIENTS ) {
		return;
	}
	if ( ( s->eFlags & EF_JETPACK_ACTIVE ) && s->otherEntityNum2 ) {
		// (the shot of a vehicle: the game says WP_BLASTER whatever it is, its weapon file says what it is)
		if ( s->otherEntityNum2 >= MAX_VEH_WEAPONS || g_vehWeaponInfo[s->otherEntityNum2].iModel != NULL_HANDLE
			|| !CG_VehicleLaserColor( &g_vehWeaponInfo[s->otherEntityNum2], color ) ) {
			return;
		}
		Bolt_Draw( cent, color, 1.2f );
		return;
	}
	scale = Bolt_Scale( s, altFire );
	if ( scale <= 0.0f || !CG_BoltColor( s->weapon, altFire, color ) ) {
		return;
	}
	Bolt_Draw( cent, color, scale );
}
