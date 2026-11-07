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

// ui_saberfx.c -- the blade style "SFX", see ui_saberfx.h.
//
// Four surfaces: the blade, the blade a moment ago, and the two sides between them (from muzzle to muzzle and from
// tip to tip). Each is a glow (RT_SABER_GLOW in the shader of the colour) and a core (RT_LINE in
// gfx/sfx_sabers/*). The glow fades the more the blade points at the eye and the less it moves.

#ifdef _CGAME
	#include "cgame/cg_local.h"
#else
	#include "ui/ui_local.h"
#endif

#include "ui_shared.h"
#include "ui_saberfx.h"

extern displayContextDef_t *DC;

#define SFX_GLOW_SCALE		1.25f	// the glow of the 2019 default (h_saberBladeGlowMulti) at v_saberGlowScale 1

static struct {
	qboolean	tried;
	qhandle_t	blade, bladeRGB, end, endRGB;
} sfx;

// the renderer was started anew (or the module was): every handle is gone
void SaberFX_Reset( void ) {
	memset( &sfx, 0, sizeof( sfx ) );
}

static void SaberFX_Register( void ) {
	if ( sfx.tried ) {
		return;
	}
	sfx.tried = qtrue;
	sfx.blade = DC->registerShaderNoMip( "gfx/sfx_sabers/saber_blade" );
	sfx.bladeRGB = DC->registerShaderNoMip( "gfx/sfx_sabers/saber_blade_rgb" );
	sfx.end = DC->registerShaderNoMip( "gfx/sfx_sabers/saber_end" );
	sfx.endRGB = DC->registerShaderNoMip( "gfx/sfx_sabers/saber_end_rgb" );
}

// The six colours of the game and the plain RGB blade. The other RGB styles and black have cores of their own
// and stay in the classic style.
qboolean SaberFX_Colour( int color ) {
	return (qboolean)( ( color >= SABER_RED && color <= SABER_PURPLE ) || color == SABER_RGB_FIRST );
}

static void SaberFX_SetColour( const saberFX_t *fx, refEntity_t *re, float alpha ) {
	int i;

	for ( i = 0; i < 3; i++ ) {
		re->shaderRGBA[i] = (byte)( ( fx->isRGB ? fx->rgb[i] : 255 ) * alpha );
	}
	re->shaderRGBA[3] = (byte)( 255 * alpha );
}

// a glow from start along dir, and a core from a little behind start to length along dir
static void SaberFX_Surface( const saberFX_t *fx, const vec3_t start, const vec3_t dir, float glowStart, float glowLength,
	float length, float back, float glowRadius, float coreRadius, float alpha, qhandle_t core, qhandle_t coreRGB ) {
	refEntity_t re;

	memset( &re, 0, sizeof( re ) );
	re.renderfx = fx->renderfx;

	if ( glowLength > 0.0f ) {
		re.reType = RT_SABER_GLOW;
		re.customShader = fx->glow;
		re.radius = glowRadius;
		re.saberLength = glowLength;
		VectorMA( start, glowStart, dir, re.origin );
		VectorCopy( dir, re.axis[0] );
		SaberFX_SetColour( fx, &re, alpha );
		DC->addRefEntityToScene( &re );
	}

	re.reType = RT_LINE;
	re.customShader = core;
	re.radius = coreRadius;
	re.saberLength = length;
	re.shaderTexCoord[0] = re.shaderTexCoord[1] = 1.0f;
	VectorMA( start, length, dir, re.origin );
	VectorMA( start, -back, dir, re.oldorigin );
	SaberFX_SetColour( fx, &re, 1.0f );
	DC->addRefEntityToScene( &re );

	if ( fx->isRGB ) {
		// the white middle over the core in the colour
		re.customShader = coreRGB;
		re.shaderRGBA[0] = re.shaderRGBA[1] = re.shaderRGBA[2] = re.shaderRGBA[3] = 255;
		DC->addRefEntityToScene( &re );
	}
}

static float SaberFX_Diff( const vec3_t a, const vec3_t b, const vec3_t eye ) {
	return fabsf( Distance( a, eye ) - Distance( b, eye ) );
}

void SaberFX_Draw( const saberFX_t *fx ) {
	vec3_t	bladeDir, trailDir, endDir, baseDir, mid, bladeTip, trailTip;
	float	bladeLen, trailLen, endLen, baseLen, dist, v1, v2, alpha, angleScale, diff, radiusMult;
	float	glowRadius, coreRadius, glowScale = 0.5f, glowR, endScale;

	SaberFX_Register();

	VectorSubtract( fx->bladeTip, fx->bladeMuzzle, bladeDir );
	bladeLen = VectorNormalize( bladeDir );
	if ( bladeLen < 1.0f ) {
		return;
	}
	VectorSubtract( fx->trailTip, fx->trailMuzzle, trailDir );
	trailLen = VectorNormalize( trailDir );
	VectorSubtract( fx->trailTip, fx->bladeTip, endDir );
	endLen = VectorNormalize( endDir );
	VectorSubtract( fx->trailMuzzle, fx->bladeMuzzle, baseDir );
	baseLen = VectorNormalize( baseDir );

	// thicker from afar, so a blade does not get lost in the distance
	VectorMA( fx->bladeMuzzle, bladeLen * 0.5f, bladeDir, mid );
	dist = Com_Clamp( 1.0f, 4000.0f, Distance( mid, fx->viewOrigin ) );
	v1 = ( dist + 400.0f ) / 400.0f;
	v2 = ( dist + 4000.0f ) / 4000.0f;

	// more glow the more the blade moves
	if ( endLen > 1.0f || baseLen > 1.0f ) {
		glowScale = Q_min( 1.0f, ( Q_max( endLen, baseLen ) + 4.0f ) * 0.1f );
	}

	// less glow the more the blade points at the eye
	diff = SaberFX_Diff( fx->bladeTip, fx->bladeMuzzle, fx->viewOrigin ) / bladeLen;
	angleScale = Com_Clamp( 0.2f, 1.0f, 1.2f - diff * diff );
	alpha = glowScale * angleScale;
	angleScale = Com_Clamp( 0.4f, 1.0f, angleScale + 0.3f );

	radiusMult = ( bladeLen < fx->lengthMax ) ? 0.5f + ( bladeLen / fx->lengthMax ) * 0.5f : 1.0f;
	glowRadius = ( fx->radius * 1.6f * v1 + Q_flrand( -1.0f, 1.0f ) * 0.1f ) * radiusMult * SFX_GLOW_SCALE * fx->glowScale;
	coreRadius = ( fx->radius * 0.4f * v2 + Q_flrand( -1.0f, 1.0f ) * 0.1f ) * radiusMult * fx->coreScale;
	glowR = glowRadius * angleScale;

	SaberFX_Surface( fx, fx->bladeMuzzle, bladeDir, 0.0f, bladeLen - glowR * 0.5f, bladeLen, 1.0f, glowR, coreRadius,
		alpha, sfx.blade, sfx.bladeRGB );
	SaberFX_Surface( fx, fx->trailMuzzle, trailDir, 0.0f, trailLen - glowR * 0.5f, trailLen, 1.0f, glowR, coreRadius,
		alpha, sfx.blade, sfx.bladeRGB );

	VectorMA( fx->bladeMuzzle, bladeLen - 0.5f, bladeDir, bladeTip );
	VectorMA( fx->trailMuzzle, trailLen - 0.5f, trailDir, trailTip );

	if ( baseLen > 2.0f ) {
		SaberFX_Surface( fx, fx->bladeMuzzle, baseDir, glowR * 0.5f, baseLen - glowR, baseLen, 0.1f, glowR, coreRadius,
			alpha, sfx.blade, sfx.bladeRGB );
	}

	if ( endLen <= 0.0f ) {
		return;
	}

	// the side from tip to tip fades when it is seen along its length
	diff = SaberFX_Diff( bladeTip, trailTip, fx->viewOrigin );
	if ( diff > endLen * 0.9f ) {
		alpha *= 0.3f;
	} else if ( diff > endLen * 0.8f ) {
		alpha *= 0.5f;
	} else if ( diff > endLen * 0.7f ) {
		alpha *= 0.7f;
	}

	endScale = endLen > 9.0f ? 5.0f : ( endLen < 3.0f ? 1.0f : endLen / 5.0f );
	endScale -= ( diff / endLen ) * ( diff / endLen ) * endScale;
	endScale = Q_max( 0.8f, endScale );

	SaberFX_Surface( fx, bladeTip, endDir, glowR * 0.5f, endLen - glowR, endLen, 0.1f, glowR, coreRadius * endScale,
		alpha, sfx.end, sfx.endRGB );
}
