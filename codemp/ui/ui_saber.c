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

/*
=======================================================================

USER INTERFACE SABER LOADING & DISPLAY CODE

=======================================================================
*/

#include "ui_local.h"
#include "ui_shared.h"
#include "ui_saberfx.h"

void WP_SaberLoadParms( void );
qboolean WP_SaberParseParm( const char *saberName, const char *parmname, char *saberData, int saberDataSize );
saber_colors_t TranslateSaberColor( const char *name );
const char *SaberColorToString( saber_colors_t color );
saber_styles_t TranslateSaberStyle( const char *name );
saberType_t TranslateSaberType( const char *name );

qboolean	ui_saber_parms_parsed = qfalse;

static qhandle_t redSaberGlowShader;
static qhandle_t redSaberCoreShader;
static qhandle_t orangeSaberGlowShader;
static qhandle_t orangeSaberCoreShader;
static qhandle_t yellowSaberGlowShader;
static qhandle_t yellowSaberCoreShader;
static qhandle_t greenSaberGlowShader;
static qhandle_t greenSaberCoreShader;
static qhandle_t blueSaberGlowShader;
static qhandle_t blueSaberCoreShader;
static qhandle_t purpleSaberGlowShader;
static qhandle_t purpleSaberCoreShader;
// JA+
static qhandle_t rgbSaberGlowShader;
static qhandle_t rgbSaberGlow4Shader;
static qhandle_t rgbSaberCoreShader[SABER_RGB_LAST - SABER_RGB_FIRST + 1];
static qhandle_t blackSaberGlowShader;
static qhandle_t blackSaberCoreShader;

void UI_CacheSaberGlowGraphics( void )
{//FIXME: these get fucked by vid_restarts
	redSaberGlowShader			= trap->R_RegisterShaderNoMip( "gfx/effects/sabers/red_glow" );
	redSaberCoreShader			= trap->R_RegisterShaderNoMip( "gfx/effects/sabers/red_line" );
	orangeSaberGlowShader		= trap->R_RegisterShaderNoMip( "gfx/effects/sabers/orange_glow" );
	orangeSaberCoreShader		= trap->R_RegisterShaderNoMip( "gfx/effects/sabers/orange_line" );
	yellowSaberGlowShader		= trap->R_RegisterShaderNoMip( "gfx/effects/sabers/yellow_glow" );
	yellowSaberCoreShader		= trap->R_RegisterShaderNoMip( "gfx/effects/sabers/yellow_line" );
	greenSaberGlowShader		= trap->R_RegisterShaderNoMip( "gfx/effects/sabers/green_glow" );
	greenSaberCoreShader		= trap->R_RegisterShaderNoMip( "gfx/effects/sabers/green_line" );
	blueSaberGlowShader			= trap->R_RegisterShaderNoMip( "gfx/effects/sabers/blue_glow" );
	blueSaberCoreShader			= trap->R_RegisterShaderNoMip( "gfx/effects/sabers/blue_line" );
	purpleSaberGlowShader		= trap->R_RegisterShaderNoMip( "gfx/effects/sabers/purple_glow" );
	purpleSaberCoreShader		= trap->R_RegisterShaderNoMip( "gfx/effects/sabers/purple_line" );
	// JA+
	rgbSaberGlowShader			= trap->R_RegisterShaderNoMip( "gfx/effects/sabers/RGBGlow" );
	rgbSaberGlow4Shader			= trap->R_RegisterShaderNoMip( "gfx/effects/sabers/RGBGlow4" );
	rgbSaberCoreShader[0]		= trap->R_RegisterShaderNoMip( "gfx/effects/sabers/RGBLine" );
	rgbSaberCoreShader[1]		= trap->R_RegisterShaderNoMip( "gfx/effects/sabers/RGBcore2" );
	rgbSaberCoreShader[2]		= trap->R_RegisterShaderNoMip( "gfx/effects/sabers/RGBcore3" );
	rgbSaberCoreShader[3]		= trap->R_RegisterShaderNoMip( "gfx/effects/sabers/RGBcore4" );
	rgbSaberCoreShader[4]		= trap->R_RegisterShaderNoMip( "gfx/effects/sabers/RGBcore5" );
	blackSaberGlowShader		= trap->R_RegisterShaderNoMip( "gfx/effects/sabers/black_glow" );
	blackSaberCoreShader		= trap->R_RegisterShaderNoMip( "gfx/effects/sabers/black_line" );
}

qboolean UI_SaberModelForSaber( const char *saberName, char *saberModel, int saberModelSize )
{
	return WP_SaberParseParm( saberName, "saberModel", saberModel, saberModelSize );
}

qboolean UI_SaberSkinForSaber( const char *saberName, char *saberSkin, int saberSkinSize )
{
	return WP_SaberParseParm( saberName, "customSkin", saberSkin, saberSkinSize );
}

qboolean UI_SaberTypeForSaber( const char *saberName, char *saberType, int saberTypeSize )
{
	return WP_SaberParseParm( saberName, "saberType", saberType, saberTypeSize );
}

int UI_SaberNumBladesForSaber( const char *saberName )
{
	int numBlades;
	char	numBladesString[8]={0};
	WP_SaberParseParm( saberName, "numBlades", numBladesString, sizeof( numBladesString ) );
	numBlades = atoi( numBladesString );
	if ( numBlades < 1 )
	{
		numBlades = 1;
	}
	else if ( numBlades > 8 )
	{
		numBlades = 8;
	}
	return numBlades;
}

qboolean UI_SaberShouldDrawBlade( const char *saberName, int bladeNum )
{
	int bladeStyle2Start = 0, noBlade = 0;
	char	bladeStyle2StartString[8]={0};
	char	noBladeString[8]={0};
	WP_SaberParseParm( saberName, "bladeStyle2Start", bladeStyle2StartString, sizeof( bladeStyle2StartString ) );
	if ( bladeStyle2StartString[0] )
	{
		bladeStyle2Start = atoi( bladeStyle2StartString );
	}
	if ( bladeStyle2Start
		&& bladeNum >= bladeStyle2Start )
	{//use second blade style
		WP_SaberParseParm( saberName, "noBlade2", noBladeString, sizeof( noBladeString ) );
		if ( noBladeString[0] )
		{
			noBlade = atoi( noBladeString );
		}
	}
	else
	{//use first blade style
		WP_SaberParseParm( saberName, "noBlade", noBladeString, sizeof( noBladeString ) );
		if ( noBladeString[0] )
		{
			noBlade = atoi( noBladeString );
		}
	}
	return ((qboolean)(noBlade==0));
}

qboolean UI_IsSaberTwoHanded( const char *saberName )
{
	int twoHanded;
	char	twoHandedString[8]={0};
	WP_SaberParseParm( saberName, "twoHanded", twoHandedString, sizeof( twoHandedString ) );
	if ( !twoHandedString[0] )
	{//not defined defaults to "no"
		return qfalse;
	}
	twoHanded = atoi( twoHandedString );
	return ((qboolean)(twoHanded!=0));
}

float UI_SaberBladeLengthForSaber( const char *saberName, int bladeNum )
{
	char	lengthString[8]={0};
	float	length = 40.0f;
	WP_SaberParseParm( saberName, "saberLength", lengthString, sizeof( lengthString ) );
	if ( lengthString[0] )
	{
		length = atof( lengthString );
		if ( length < 0.0f )
		{
			length = 0.0f;
		}
	}

	WP_SaberParseParm( saberName, va("saberLength%d", bladeNum+1), lengthString, sizeof( lengthString ) );
	if ( lengthString[0] )
	{
		length = atof( lengthString );
		if ( length < 0.0f )
		{
			length = 0.0f;
		}
	}

	return length;
}

float UI_SaberBladeRadiusForSaber( const char *saberName, int bladeNum )
{
	char	radiusString[8]={0};
	float	radius = 3.0f;
	WP_SaberParseParm( saberName, "saberRadius", radiusString, sizeof( radiusString ) );
	if ( radiusString[0] )
	{
		radius = atof( radiusString );
		if ( radius < 0.0f )
		{
			radius = 0.0f;
		}
	}

	WP_SaberParseParm( saberName, va("saberRadius%d", bladeNum+1), radiusString, sizeof( radiusString ) );
	if ( radiusString[0] )
	{
		radius = atof( radiusString );
		if ( radius < 0.0f )
		{
			radius = 0.0f;
		}
	}

	return radius;
}

qboolean UI_SaberProperNameForSaber( const char *saberName, char *saberProperName )
{
	char	stringedSaberName[1024];
	qboolean ret = WP_SaberParseParm( saberName, "name", stringedSaberName, sizeof( stringedSaberName ) );
	// if it's a stringed reference translate it
	if( ret && stringedSaberName[0] == '@')
	{
		trap->SE_GetStringTextString(&stringedSaberName[1], saberProperName, 1024);
	}
	else
	{
		// no stringed so just use it as it
		strcpy( saberProperName, stringedSaberName );
	}

	return ret;
}

qboolean UI_SaberValidForPlayerInMP( const char *saberName )
{
	char allowed [8]={0};
	if ( !WP_SaberParseParm( saberName, "notInMP", allowed, sizeof( allowed ) ) )
	{//not defined, default is yes
		return qtrue;
	}
	if ( !allowed[0] )
	{//not defined, default is yes
		return qtrue;
	}
	else
	{//return value
		return ((qboolean)(atoi(allowed)==0));
	}
}

void UI_SaberLoadParms( void )
{
	ui_saber_parms_parsed = qtrue;
	UI_CacheSaberGlowGraphics();

	WP_SaberLoadParms();
}

// the glow, the core and the light of a blade colour. JA+: saberNum picks the sliders (ui_sbRed1.. or ui_sbRed2..)
// that color an RGB blade
static void UI_SaberShaders( saber_colors_t color, int saberNum, qhandle_t *glowOut, qhandle_t *bladeOut, vec3_t rgb )
{
	qhandle_t	blade = 0, glow = 0;

	VectorSet( rgb, 1.0f, 1.0f, 1.0f );
	switch( (int)color )
	{
		case SABER_RED:
			glow = redSaberGlowShader;
			blade = redSaberCoreShader;
			VectorSet( rgb, 1.0f, 0.2f, 0.2f );
			break;
		case SABER_ORANGE:
			glow = orangeSaberGlowShader;
			blade = orangeSaberCoreShader;
			VectorSet( rgb, 1.0f, 0.5f, 0.1f );
			break;
		case SABER_YELLOW:
			glow = yellowSaberGlowShader;
			blade = yellowSaberCoreShader;
			VectorSet( rgb, 1.0f, 1.0f, 0.2f );
			break;
		case SABER_GREEN:
			glow = greenSaberGlowShader;
			blade = greenSaberCoreShader;
			VectorSet( rgb, 0.2f, 1.0f, 0.2f );
			break;
		case SABER_BLUE:
			glow = blueSaberGlowShader;
			blade = blueSaberCoreShader;
			VectorSet( rgb, 0.2f, 0.4f, 1.0f );
			break;
		case SABER_PURPLE:
			glow = purpleSaberGlowShader;
			blade = purpleSaberCoreShader;
			VectorSet( rgb, 0.9f, 0.2f, 1.0f );
			break;
		// JA+
		case SABER_RGB_FIRST:
		case SABER_RGB_FIRST+1:
		case SABER_RGB_FIRST+2:
		case SABER_RGB_FIRST+3:
		case SABER_RGB_LAST:
			glow = ( color == SABER_RGB_FIRST+3 ) ? rgbSaberGlow4Shader : rgbSaberGlowShader;
			blade = rgbSaberCoreShader[color - SABER_RGB_FIRST];
			if ( saberNum == 0 )
				VectorSet( rgb, ui_sbRed1.value, ui_sbGreen1.value, ui_sbBlue1.value );
			else
				VectorSet( rgb, ui_sbRed2.value, ui_sbGreen2.value, ui_sbBlue2.value );
			VectorScale( rgb, 1.0f / 255.0f, rgb );
			break;
		case SABER_BLACK:
			glow = blackSaberGlowShader;
			blade = blackSaberCoreShader;
			break;
		default:
			break;
	}

	*glowOut = glow;
	*bladeOut = blade;
}

// v_saberCoreScale, v_saberGlowScale
static float UI_SaberScale( const vmCvar_t *cv )
{
	return Com_Clamp( 0.25f, 3.0f, cv->value );
}

void UI_DoSaber( vec3_t origin, vec3_t dir, float length, float lengthMax, float radius, saber_colors_t color, int saberNum )
{
	vec3_t		mid, rgb;
	qhandle_t	blade = 0, glow = 0;
	refEntity_t saber;
	float radiusmult;
	float radiusRange;
	float radiusStart;

	if ( length < 0.5f )
	{
		// if the thing is so short, just forget even adding me.
		return;
	}

	// Find the midpoint of the saber for lighting purposes
	VectorMA( origin, length * 0.5f, dir, mid );

	UI_SaberShaders( color, saberNum, &glow, &blade, rgb );

	memset( &saber, 0, sizeof( refEntity_t ));

	// Saber glow is it's own ref type because it uses a ton of sprites, otherwise it would eat up too many
	//	refEnts to do each glow blob individually
	saber.saberLength = length;

	// Jeff, I did this because I foolishly wished to have a bright halo as the saber is unleashed.
	// It's not quite what I'd hoped tho.  If you have any ideas, go for it!  --Pat
	if (length < lengthMax )
	{
		radiusmult = 1.0 + (2.0 / length);		// Note this creates a curve, and length cannot be < 0.5.
	}
	else
	{
		radiusmult = 1.0;
	}

	radiusRange = radius * 0.075f;
	radiusStart = radius-radiusRange;

	saber.radius = (radiusStart + Q_flrand(-1.0f, 1.0f) * radiusRange)*radiusmult*UI_SaberScale( &v_saberGlowScale );
	//saber.radius = (2.8f + Q_flrand(-1.0f, 1.0f) * 0.2f)*radiusmult;

	VectorCopy( origin, saber.origin );
	VectorCopy( dir, saber.axis[0] );
	saber.reType = RT_SABER_GLOW;
	saber.customShader = glow;
	saber.shaderRGBA[0] = saber.shaderRGBA[1] = saber.shaderRGBA[2] = saber.shaderRGBA[3] = 0xff;
	if ( (int)color >= SABER_RGB_FIRST && (int)color <= SABER_RGB_LAST )
	{// JA+: glow and core in the color of the sliders
		int i;

		for ( i = 0; i < 3; i++ )
			saber.shaderRGBA[i] = (int)( rgb[i] * 255.0f );
	}
	//saber.renderfx = rfx;

	trap->R_AddRefEntityToScene( &saber );

	// Do the hot core
	VectorMA( origin, length, dir, saber.origin );
	VectorMA( origin, -1, dir, saber.oldorigin );
	saber.customShader = blade;
	saber.reType = RT_LINE;
	radiusStart = radius/3.0f;
	saber.radius = (radiusStart + Q_flrand(-1.0f, 1.0f) * radiusRange)*radiusmult*UI_SaberScale( &v_saberCoreScale );
//	saber.radius = (1.0 + Q_flrand(-1.0f, 1.0f) * 0.2f)*radiusmult;

	trap->R_AddRefEntityToScene( &saber );
}

// v_saberStyle 1 (ui_saberfx.c): a blade that stands still
static void UI_DoSFXSaber( vec3_t origin, vec3_t dir, float length, float radius, saber_colors_t color, int saberNum )
{
	saberFX_t	fx;
	qhandle_t	core;
	vec3_t		rgb;
	int			i;

	memset( &fx, 0, sizeof( fx ) );
	VectorCopy( origin, fx.bladeMuzzle );
	VectorMA( origin, length, dir, fx.bladeTip );
	VectorCopy( fx.bladeMuzzle, fx.trailMuzzle );
	VectorCopy( fx.bladeTip, fx.trailTip );
	fx.lengthMax = length;
	fx.radius = radius;
	UI_SaberShaders( color, saberNum, &fx.glow, &core, rgb );
	fx.isRGB = (qboolean)( (int)color >= SABER_RGB_FIRST && (int)color <= SABER_RGB_LAST );
	for ( i = 0; i < 3; i++ )
	{
		fx.rgb[i] = (byte)( Com_Clamp( 0.0f, 1.0f, rgb[i] ) * 255.0f );
	}
	fx.coreScale = UI_SaberScale( &v_saberCoreScale );
	fx.glowScale = UI_SaberScale( &v_saberGlowScale );
	SaberFX_Draw( &fx );
}

void UI_SaberDrawBlade( itemDef_t *item, char *saberName, int saberModel, saberType_t saberType, vec3_t origin, vec3_t angles, int bladeNum )
{
	char bladeColorString[MAX_QPATH];
	saber_colors_t bladeColor;
	int saberNum;	// JA+
	float bladeLength,bladeRadius;
	vec3_t	bladeOrigin={0};
	matrix3_t	axis;
//	vec3_t	angles={0};
	mdxaBone_t	boltMatrix;
	qboolean tagHack = qfalse;
	char *tagName;
	int bolt;
	float scale;

    memset (axis, 0, sizeof (axis));

	if ( (item->flags&ITF_ISSABER) && saberModel < 2 )
	{
		trap->Cvar_VariableStringBuffer("ui_saber_color", bladeColorString, sizeof(bladeColorString) );
		saberNum = 0;
	}
	else//if ( item->flags&ITF_ISSABER2 ) - presumed
	{
		trap->Cvar_VariableStringBuffer("ui_saber2_color", bladeColorString, sizeof(bladeColorString) );
		saberNum = 1;
	}

	if ( !trap->G2API_HasGhoul2ModelOnIndex(&(item->ghoul2),saberModel) )
	{//invalid index!
		return;
	}

	bladeColor = TranslateSaberColor( bladeColorString );

	bladeLength = UI_SaberBladeLengthForSaber( saberName, bladeNum );
	bladeRadius = UI_SaberBladeRadiusForSaber( saberName, bladeNum );

	tagName = va( "*blade%d", bladeNum+1 );
	bolt = trap->G2API_AddBolt( item->ghoul2,saberModel, tagName );

	if ( bolt == -1 )
	{
		tagHack = qtrue;
		//hmm, just fall back to the most basic tag (this will also make it work with pre-JKA saber models
		bolt = trap->G2API_AddBolt( item->ghoul2,saberModel, "*flash" );
		if ( bolt == -1 )
		{//no tag_flash either?!!
			bolt = 0;
		}
	}

//	angles[PITCH] = curYaw;
//	angles[ROLL] = 0;

	trap->G2API_GetBoltMatrix( item->ghoul2, saberModel, bolt, &boltMatrix, angles, origin, uiInfo.uiDC.realTime, NULL, vec3_origin );//NULL was cgs.model_draw

	// work the matrix axis stuff into the original axis and origins used.
	BG_GiveMeVectorFromMatrix(&boltMatrix, ORIGIN, bladeOrigin);
	BG_GiveMeVectorFromMatrix(&boltMatrix, NEGATIVE_Y, axis[0]);//front (was NEGATIVE_Y, but the md3->glm exporter screws up this tag somethin' awful)
																//		...changed this back to NEGATIVE_Y
	BG_GiveMeVectorFromMatrix(&boltMatrix, NEGATIVE_X, axis[1]);//right ... and changed this to NEGATIVE_X
	BG_GiveMeVectorFromMatrix(&boltMatrix, POSITIVE_Z, axis[2]);//up

	// Where do I get scale from?
//	scale = DC->xscale;
	scale = 1.0f;

	if ( tagHack )
	{
		switch ( saberType )
		{
		case SABER_SINGLE:
				VectorMA( bladeOrigin, scale, axis[0], bladeOrigin );
			break;
		case SABER_DAGGER:
		case SABER_LANCE:
			break;
		case SABER_STAFF:
			if ( bladeNum == 0 )
			{
				VectorMA( bladeOrigin, 12*scale, axis[0], bladeOrigin );
			}
			if ( bladeNum == 1 )
			{
				VectorScale( axis[0], -1, axis[0] );
				VectorMA( bladeOrigin, 12*scale, axis[0], bladeOrigin );
			}
			break;
		case SABER_BROAD:
			if ( bladeNum == 0 )
			{
				VectorMA( bladeOrigin, -1*scale, axis[1], bladeOrigin );
			}
			else if ( bladeNum == 1 )
			{
				VectorMA( bladeOrigin, 1*scale, axis[1], bladeOrigin );
			}
			break;
		case SABER_PRONG:
			if ( bladeNum == 0 )
			{
				VectorMA( bladeOrigin, -3*scale, axis[1], bladeOrigin );
			}
			else if ( bladeNum == 1 )
			{
				VectorMA( bladeOrigin, 3*scale, axis[1], bladeOrigin );
			}
			break;
		case SABER_ARC:
			VectorSubtract( axis[1], axis[2], axis[1] );
			VectorNormalize( axis[1] );
			switch ( bladeNum )
			{
			case 0:
				VectorMA( bladeOrigin, 8*scale, axis[0], bladeOrigin );
				VectorScale( axis[0], 0.75f, axis[0] );
				VectorScale( axis[1], 0.25f, axis[1] );
				VectorAdd( axis[0], axis[1], axis[0] );
				break;
			case 1:
				VectorScale( axis[0], 0.25f, axis[0] );
				VectorScale( axis[1], 0.75f, axis[1] );
				VectorAdd( axis[0], axis[1], axis[0] );
				break;
			case 2:
				VectorMA( bladeOrigin, -8*scale, axis[0], bladeOrigin );
				VectorScale( axis[0], -0.25f, axis[0] );
				VectorScale( axis[1], 0.75f, axis[1] );
				VectorAdd( axis[0], axis[1], axis[0] );
				break;
			case 3:
				VectorMA( bladeOrigin, -16*scale, axis[0], bladeOrigin );
				VectorScale( axis[0], -0.75f, axis[0] );
				VectorScale( axis[1], 0.25f, axis[1] );
				VectorAdd( axis[0], axis[1], axis[0] );
				break;
			}
			break;
		case SABER_SAI:
			if ( bladeNum == 1 )
			{
				VectorMA( bladeOrigin, -3*scale, axis[1], bladeOrigin );
			}
			else if ( bladeNum == 2 )
			{
				VectorMA( bladeOrigin, 3*scale, axis[1], bladeOrigin );
			}
			break;
		case SABER_CLAW:
			switch ( bladeNum )
			{
			case 0:
				VectorMA( bladeOrigin, 2*scale, axis[0], bladeOrigin );
				VectorMA( bladeOrigin, 2*scale, axis[2], bladeOrigin );
				break;
			case 1:
				VectorMA( bladeOrigin, 2*scale, axis[0], bladeOrigin );
				VectorMA( bladeOrigin, 2*scale, axis[2], bladeOrigin );
				VectorMA( bladeOrigin, 2*scale, axis[1], bladeOrigin );
				break;
			case 2:
				VectorMA( bladeOrigin, 2*scale, axis[0], bladeOrigin );
				VectorMA( bladeOrigin, 2*scale, axis[2], bladeOrigin );
				VectorMA( bladeOrigin, -2*scale, axis[1], bladeOrigin );
				break;
			}
			break;
		case SABER_STAR:
			switch ( bladeNum )
			{
			case 0:
				VectorMA( bladeOrigin, 8*scale, axis[0], bladeOrigin );
				break;
			case 1:
				VectorScale( axis[0], 0.33f, axis[0] );
				VectorScale( axis[2], 0.67f, axis[2] );
				VectorAdd( axis[0], axis[2], axis[0] );
				VectorMA( bladeOrigin, 8*scale, axis[0], bladeOrigin );
				break;
			case 2:
				VectorScale( axis[0], -0.33f, axis[0] );
				VectorScale( axis[2], 0.67f, axis[2] );
				VectorAdd( axis[0], axis[2], axis[0] );
				VectorMA( bladeOrigin, 8*scale, axis[0], bladeOrigin );
				break;
			case 3:
				VectorScale( axis[0], -1, axis[0] );
				VectorMA( bladeOrigin, 8*scale, axis[0], bladeOrigin );
				break;
			case 4:
				VectorScale( axis[0], -0.33f, axis[0] );
				VectorScale( axis[2], -0.67f, axis[2] );
				VectorAdd( axis[0], axis[2], axis[0] );
				VectorMA( bladeOrigin, 8*scale, axis[0], bladeOrigin );
				break;
			case 5:
				VectorScale( axis[0], 0.33f, axis[0] );
				VectorScale( axis[2], -0.67f, axis[2] );
				VectorAdd( axis[0], axis[2], axis[0] );
				VectorMA( bladeOrigin, 8*scale, axis[0], bladeOrigin );
				break;
			}
			break;
		case SABER_TRIDENT:
			switch ( bladeNum )
			{
			case 0:
				VectorMA( bladeOrigin, 24*scale, axis[0], bladeOrigin );
				break;
			case 1:
				VectorMA( bladeOrigin, -6*scale, axis[1], bladeOrigin );
				VectorMA( bladeOrigin, 24*scale, axis[0], bladeOrigin );
				break;
			case 2:
				VectorMA( bladeOrigin, 6*scale, axis[1], bladeOrigin );
				VectorMA( bladeOrigin, 24*scale, axis[0], bladeOrigin );
				break;
			case 3:
				VectorMA( bladeOrigin, -32*scale, axis[0], bladeOrigin );
				VectorScale( axis[0], -1, axis[0] );
				break;
			}
			break;
		case SABER_SITH_SWORD:
			//no blade
			break;
		default:
			break;
		}
	}
	if ( saberType == SABER_SITH_SWORD )
	{//draw no blade
		return;
	}

	if ( v_saberStyle.integer == 1 && SaberFX_Colour( bladeColor ) )
	{
		UI_DoSFXSaber( bladeOrigin, axis[0], bladeLength, bladeRadius, bladeColor, saberNum );
		return;
	}
	UI_DoSaber( bladeOrigin, axis[0], bladeLength, bladeLength, bladeRadius, bladeColor, saberNum );
}

void UI_GetSaberForMenu( char *saber, int saberNum )
{
	char saberTypeString[MAX_QPATH]={0};
	saberType_t saberType = SABER_NONE;

	if ( saberNum == 0 )
	{
		trap->Cvar_VariableStringBuffer("ui_saber", saber, MAX_QPATH );
		if ( !UI_SaberValidForPlayerInMP( saber ) )
		{
			trap->Cvar_Set( "ui_saber", DEFAULT_SABER );
			trap->Cvar_VariableStringBuffer("ui_saber", saber, MAX_QPATH );
		}
	}
	else
	{
		trap->Cvar_VariableStringBuffer("ui_saber2", saber, MAX_QPATH );
		if ( !UI_SaberValidForPlayerInMP( saber ) )
		{
			trap->Cvar_Set( "ui_saber2", DEFAULT_SABER );
			trap->Cvar_VariableStringBuffer("ui_saber2", saber, MAX_QPATH );
		}
	}
	//read this from the sabers.cfg
	UI_SaberTypeForSaber( saber, saberTypeString, sizeof( saberTypeString ) );
	if ( saberTypeString[0] )
	{
		saberType = TranslateSaberType( saberTypeString );
	}

	switch ( uiInfo.movesTitleIndex )
	{
	case 0://MD_ACROBATICS:
		break;
	case 1://MD_SINGLE_FAST:
	case 2://MD_SINGLE_MEDIUM:
	case 3://MD_SINGLE_STRONG:
		if ( saberType != SABER_SINGLE )
		{
			Q_strncpyz(saber, DEFAULT_SABER, MAX_QPATH);
		}
		break;
	case 4://MD_DUAL_SABERS:
		if ( saberType != SABER_SINGLE )
		{
			Q_strncpyz(saber, DEFAULT_SABER, MAX_QPATH);
		}
		break;
	case 5://MD_SABER_STAFF:
		if ( saberType == SABER_SINGLE || saberType == SABER_NONE )
		{
			Q_strncpyz(saber, DEFAULT_SABER_STAFF, MAX_QPATH);
		}
		break;
	}
}

// the saber a model item of a saber shows (ui_saber or ui_saber2), set back to the default one when it is not
// allowed
static qboolean UI_SaberOfItem( itemDef_t *item, char *saber, int saberSize )
{
	const char *cvar;

	if ( item->flags & ITF_ISSABER )
		cvar = "ui_saber";
	else if ( item->flags & ITF_ISSABER2 )
		cvar = "ui_saber2";
	else
		return qfalse;

	trap->Cvar_VariableStringBuffer( cvar, saber, saberSize );
	if ( !UI_SaberValidForPlayerInMP( saber ) )
	{
		trap->Cvar_Set( cvar, DEFAULT_SABER );
		trap->Cvar_VariableStringBuffer( cvar, saber, saberSize );
	}
	return qtrue;
}

void UI_SaberDrawBlades( itemDef_t *item, vec3_t origin, vec3_t angles )
{
	//NOTE: only allows one saber type in view at a time
	char saber[MAX_QPATH];
	int saberNum = 0;
	int saberModel = 0;
	int	numSabers = 1;

	if ( (item->flags&ITF_ISCHARACTER)//hacked sabermoves sabers in character's hand
		&& uiInfo.movesTitleIndex == 4 /*MD_DUAL_SABERS*/ )
	{
		numSabers = 2;
	}

	for ( saberNum = 0; saberNum < numSabers; saberNum++ )
	{
		if ( (item->flags&ITF_ISCHARACTER) )//hacked sabermoves sabers in character's hand
		{
			UI_GetSaberForMenu( saber, saberNum );
			saberModel = saberNum + 1;
		}
		else if ( UI_SaberOfItem( item, saber, sizeof( saber ) ) )
		{
			saberModel = 0;
		}
		else
		{
			return;
		}
		if ( saber[0] )
		{
			saberType_t saberType;
			int curBlade = 0;
			int numBlades = UI_SaberNumBladesForSaber( saber );
			if ( numBlades )
			{//okay, here we go, time to draw each blade...
				char	saberTypeString[MAX_QPATH]={0};
				UI_SaberTypeForSaber( saber, saberTypeString, sizeof( saberTypeString ) );
				saberType = TranslateSaberType( saberTypeString );
				for ( curBlade = 0; curBlade < numBlades; curBlade++ )
				{
					if ( UI_SaberShouldDrawBlade( saber, curBlade ) )
					{
						UI_SaberDrawBlade( item, saber, saberModel, saberType, origin, angles, curBlade );
					}
				}
			}
		}
	}
}

/*
=================
UI_SaberFrame

All of a saber lying in a menu, in the space of its model: the middle of the line from the end of its hilt (or the
tip of a blade that points back) to the tip of its farthest blade, and how long that line is. The blades are not
in the bounds of the model, they come from the tags of the model and the lengths of the saber file. qfalse for an
item that is no saber or a saber without blades.
=================
*/
qboolean UI_SaberFrame( itemDef_t *item, vec3_t middle, float *length )
{
	char		saber[MAX_QPATH], typeName[MAX_QPATH] = { 0 }, modelPath[MAX_QPATH];
	vec3_t		points[2 + 2 * MAX_BLADES], dir, axisDir = { 0.0f, 0.0f, 0.0f };
	mdxaBone_t	boltMatrix;
	int			numPoints = 1, numBlades, i, lo = 0, hi = 0;
	float		proj, loProj, hiProj;

	if ( ( item->flags & ITF_ISCHARACTER ) || !item->ghoul2 || !trap->G2API_HasGhoul2ModelOnIndex( &item->ghoul2, 0 )
		|| !UI_SaberOfItem( item, saber, sizeof( saber ) ) ) {
		return qfalse;
	}
	numBlades = Q_min( UI_SaberNumBladesForSaber( saber ), MAX_BLADES );
	if ( numBlades <= 0 ) {
		return qfalse;
	}
	UI_SaberTypeForSaber( saber, typeName, sizeof( typeName ) );
	VectorClear( points[0] );		// the end of the hilt is the origin of the model
	for ( i = 0; i < numBlades; i++ ) {
		int bolt = trap->G2API_AddBolt( item->ghoul2, 0, va( "*blade%d", i + 1 ) );
		qboolean tagHack = qfalse;

		if ( bolt == -1 ) {
			// (old models: the tag of the flash, the blades as UI_SaberDrawBlade puts them on it)
			tagHack = qtrue;
			bolt = trap->G2API_AddBolt( item->ghoul2, 0, "*flash" );
			if ( bolt == -1 ) {
				bolt = 0;
			}
		}
		trap->G2API_GetBoltMatrix( item->ghoul2, 0, bolt, &boltMatrix, vec3_origin, vec3_origin, uiInfo.uiDC.realTime, NULL, vec3_origin );
		BG_GiveMeVectorFromMatrix( &boltMatrix, ORIGIN, points[numPoints] );
		BG_GiveMeVectorFromMatrix( &boltMatrix, NEGATIVE_Y, dir );
		if ( tagHack && i == 1 && TranslateSaberType( typeName ) == SABER_STAFF ) {
			VectorScale( dir, -1.0f, dir );
		}
		if ( i == 0 ) {
			VectorCopy( dir, axisDir );
		}
		VectorMA( points[numPoints], UI_SaberBladeLengthForSaber( saber, i ), dir, points[numPoints + 1] );
		numPoints += 2;
	}
	// the hilt: the two corners of the bounds of its model farthest along the first blade
	if ( UI_SaberModelForSaber( saber, modelPath, sizeof( modelPath ) ) ) {
		const qhandle_t model = trap->R_RegisterModel( modelPath );
		vec3_t			mins, maxs;

		if ( model ) {
			trap->R_ModelBounds( model, mins, maxs );
			for ( i = 0; i < 3; i++ ) {
				points[numPoints][i] = ( axisDir[i] < 0.0f ) ? maxs[i] : mins[i];
				points[numPoints + 1][i] = ( axisDir[i] < 0.0f ) ? mins[i] : maxs[i];
			}
			numPoints += 2;
		}
	}
	// the two points farthest apart along the first blade
	loProj = hiProj = 0.0f;
	for ( i = 1; i < numPoints; i++ ) {
		proj = DotProduct( points[i], axisDir );
		if ( proj < loProj ) {
			loProj = proj;
			lo = i;
		}
		if ( proj > hiProj ) {
			hiProj = proj;
			hi = i;
		}
	}
	VectorAdd( points[lo], points[hi], middle );
	VectorScale( middle, 0.5f, middle );
	*length = hiProj - loProj;
	return (qboolean)( *length > 1.0f );
}

void UI_SaberAttachToChar( itemDef_t *item )
{
	int	numSabers = 1;
 	int	saberNum = 0;

	if ( trap->G2API_HasGhoul2ModelOnIndex(&(item->ghoul2),2) )
	{//remove any extra models
		trap->G2API_RemoveGhoul2Model(&(item->ghoul2), 2);
	}
	if ( trap->G2API_HasGhoul2ModelOnIndex(&(item->ghoul2),1) )
	{//remove any extra models
		trap->G2API_RemoveGhoul2Model(&(item->ghoul2), 1);
	}

	if ( uiInfo.movesTitleIndex >= 6 /*MD_MELEE*/ )
	{// JA+: the melee moves are shown without saber
		return;
	}

	if ( uiInfo.movesTitleIndex == 4 /*MD_DUAL_SABERS*/ )
	{
		numSabers = 2;
	}

	for ( saberNum = 0; saberNum < numSabers; saberNum++ )
	{
		//bolt sabers
		char modelPath[MAX_QPATH];
		char skinPath[MAX_QPATH];
		char saber[MAX_QPATH];

		UI_GetSaberForMenu( saber, saberNum );

		if ( UI_SaberModelForSaber( saber, modelPath, sizeof( modelPath ) ) )
		{//successfully found a model
			int g2Saber = trap->G2API_InitGhoul2Model( &(item->ghoul2), modelPath, 0, 0, 0, 0, 0 ); //add the model
			if ( g2Saber )
			{
				int boltNum;
				//get the customSkin, if any
				if ( UI_SaberSkinForSaber( saber, skinPath, sizeof( skinPath ) ) )
				{
					int g2skin = trap->R_RegisterSkin(skinPath);
					trap->G2API_SetSkin( item->ghoul2, g2Saber, 0, g2skin );//this is going to set the surfs on/off matching the skin file
				}
				else
				{
					trap->G2API_SetSkin( item->ghoul2, g2Saber, 0, 0 );//turn off custom skin
				}
				if ( saberNum == 0 )
				{
					boltNum = trap->G2API_AddBolt( item->ghoul2, 0, "*r_hand");
				}
				else
				{
					boltNum = trap->G2API_AddBolt( item->ghoul2, 0, "*l_hand");
				}
				trap->G2API_AttachG2Model( item->ghoul2, g2Saber, item->ghoul2, boltNum, 0);
			}
		}
	}
}
