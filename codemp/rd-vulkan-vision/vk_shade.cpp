/*
===========================================================================
Copyright (C) 2026, OpenJK contributors

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

// vk_shade.cpp: a batch of geometry with a .shader script becomes draws
//
// This is the place of tr_shade.cpp of rd-vanilla. There the stages of a shader are computed per vertex on the
// CPU (colors and texture coordinates into tess.svars) and handed to OpenGL stage by stage. Here a stage becomes
// one vkDraw_t: what is the same for all vertices of the batch (the wave of an rgbGen, the matrix of the tcMods,
// which textures) is worked out once, what differs per vertex is left to shaders/generic.vert.

#include "vk_local.h"

shaderCommands_t	tess;
color4ub_t			styleColors[MAX_LIGHT_STYLES];

bool g_bRenderGlowingObjects = false;
bool g_bDynamicGlowSupported = false;

extern int skyboxportal;
extern int drawskyboxportal;

void myGlMultMatrix( const float *a, const float *b, float *out );

/*
===============================================================================

TRANSFORM

The matrices of the entity being drawn. Added to the frame the first time a draw needs them.

===============================================================================
*/

static float	vk_projection[16];
static int		vk_transform = -1;

// the entity or the view changed
void VK_InvalidateTransform( void ) {
	vk_transform = -1;
	VK_GL_ResetMatrix();
}

// the same entity, but the matrices are not the ones that were added
void VK_TransformChanged( void ) {
	vk_transform = -1;
}

void VK_SetProjection( const float *projection ) {
	memcpy( vk_projection, projection, sizeof( vk_projection ) );
	VK_InvalidateTransform();
}

uint32_t VK_CurrentTransform( void ) {
	if ( vk_transform < 0 ) {
		vkTransform_t t;
		const trRefEntity_t *ent = backEnd.currentEntity;

		memset( &t, 0, sizeof( t ) );
		myGlMultMatrix( VK_GL_ModelView(), vk_projection, t.mvp );
		for ( int i = 0; i < 3; i++ ) {
			t.model[i * 4 + 0] = backEnd.ori.axis[i][0];
			t.model[i * 4 + 1] = backEnd.ori.axis[i][1];
			t.model[i * 4 + 2] = backEnd.ori.axis[i][2];
			t.model[12 + i] = backEnd.ori.origin[i];
		}
		t.model[15] = 1.0f;
		VectorCopy( backEnd.ori.viewOrigin, t.viewOrigin );
		if ( ent ) {
			VectorCopy( ent->lightDir, t.lightDir );
			VectorScale( ent->ambientLight, 1.0f / 255.0f, t.ambientLight );
			VectorScale( ent->directedLight, 1.0f / 255.0f, t.directedLight );
		}
		vk_transform = VK_AddTransform( &t );
	}
	return (uint32_t)vk_transform;
}

/*
===============================================================================

TEXTURES OF A STAGE

===============================================================================
*/

// the image R_BindAnimatedImage() of rd-vanilla would bind
static uint32_t VK_BundleTexture( const textureBundle_t *bundle ) {
	int index;

	if ( bundle->isVideoMap ) {
		ri.CIN_RunCinematic( bundle->videoMapHandle );
		ri.CIN_UploadCinematic( bundle->videoMapHandle );
		return tr.scratchImage[bundle->videoMapHandle]->texnum;
	}
	if ( r_fullbright->value && bundle->isLightmap ) {
		return tr.whiteImage->texnum;
	}
	if ( !bundle->image ) {
		return tr.defaultImage->texnum;
	}
	if ( bundle->numImageAnimations <= 1 ) {
		return bundle->image->texnum;
	}

	if ( backEnd.currentEntity->e.renderfx & RF_SETANIMINDEX ) {
		index = backEnd.currentEntity->e.skinNum;
	} else {
		// it is necessary to do this messy calc to make sure animations line up
		// exactly with waveforms of the same frequency
		index = Q_ftol( tess.shaderTime * bundle->imageAnimationSpeed * FUNCTABLE_SIZE );
		index >>= FUNCTABLE_SIZE2;
		if ( index < 0 ) {
			index = 0;	// may happen with shader time offsets
		}
	}
	if ( bundle->oneShotAnimMap ) {
		if ( index >= bundle->numImageAnimations ) {
			index = bundle->numImageAnimations - 1;
		}
	} else {
		index %= bundle->numImageAnimations;
	}
	return ( *( (image_t **)bundle->image + index ) )->texnum;
}

/*
===============
VK_BundleTexCoords

The tcMods of a bundle as one matrix. All of them but the turbulence move every point the same way, so the
functions of tr_shade_calc.cpp are run on three points and the matrix is read off the result. The turbulence
depends on the position of the vertex and stays in the vertex shader. It gets the linear part of the tcMods
that come after it.
===============
*/
static void VK_BundleTexCoords( const textureBundle_t *bundle, float *mat, float *turb ) {
	float points[3][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f } };
	float after[3][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f } };
	float amplitude = 0.0f, now = 0.0f;
	qboolean turbulent = qfalse;
	const int numVertexes = tess.numVertexes;

	tess.numVertexes = 3;
	for ( int tm = 0; tm < bundle->numTexMods; tm++ ) {
		const texModInfo_t *mod = &bundle->texMods[tm];

		if ( mod->type == TMOD_NONE ) {
			break;
		}
		if ( mod->type == TMOD_TURBULENT ) {
			turbulent = qtrue;
			amplitude = mod->wave.amplitude;
			now = mod->wave.phase + tess.shaderTime * mod->wave.frequency;
			VectorSet2( after[0], 0.0f, 0.0f );
			VectorSet2( after[1], 1.0f, 0.0f );
			VectorSet2( after[2], 0.0f, 1.0f );
			continue;
		}
		for ( int pass = 0; pass < ( turbulent ? 2 : 1 ); pass++ ) {
			float *st = pass ? after[0] : points[0];

			switch ( mod->type ) {
			case TMOD_ENTITY_TRANSLATE:
				RB_CalcScrollTexCoords( backEnd.currentEntity->e.shaderTexCoord, st );
				break;
			case TMOD_SCROLL:
				RB_CalcScrollTexCoords( mod->translate, st );	// scroll unioned
				break;
			case TMOD_SCALE:
				RB_CalcScaleTexCoords( mod->translate, st );
				break;
			case TMOD_STRETCH:
				RB_CalcStretchTexCoords( &mod->wave, st );
				break;
			case TMOD_TRANSFORM:
				RB_CalcTransformTexCoords( mod, st );
				break;
			case TMOD_ROTATE:
				RB_CalcRotateTexCoords( mod->translate[0], st );
				break;
			default:
				tess.numVertexes = numVertexes;
				Com_Error( ERR_DROP, "ERROR: unknown texmod '%d' in shader '%s'\n", mod->type, tess.shader->name );
				break;
			}
		}
	}
	tess.numVertexes = numVertexes;

	mat[0] = points[1][0] - points[0][0];
	mat[1] = points[2][0] - points[0][0];
	mat[2] = points[0][0];
	mat[3] = now;
	mat[4] = points[1][1] - points[0][1];
	mat[5] = points[2][1] - points[0][1];
	mat[6] = points[0][1];
	mat[7] = 0.0f;
	VectorSet4( turb, 0.0f, 0.0f, 0.0f, 0.0f );
	if ( turbulent ) {
		turb[0] = ( after[1][0] - after[0][0] ) * amplitude;
		turb[1] = ( after[2][0] - after[0][0] ) * amplitude;
		turb[2] = ( after[1][1] - after[0][1] ) * amplitude;
		turb[3] = ( after[2][1] - after[0][1] ) * amplitude;
	}
}

static uint32_t VK_BundleTcGen( const textureBundle_t *bundle, float *vectors ) {
	memset( vectors, 0, sizeof( float ) * 8 );
	switch ( bundle->tcGen ) {
	case TCGEN_ENVIRONMENT_MAPPED:
		return r_environmentMapping->integer ? TCGEN_ENVIRONMENT_MAPPED : TCGEN_IDENTITY;
	case TCGEN_VECTOR:
		VectorCopy( bundle->tcGenVectors[0], vectors );
		VectorCopy( bundle->tcGenVectors[1], vectors + 4 );
		return TCGEN_VECTOR;
	case TCGEN_FOG:		// only the fog pass has it, and that is drawn in another way
	case TCGEN_BAD:
		return TCGEN_IDENTITY;
	default:
		return (uint32_t)bundle->tcGen;
	}
}

/*
===============================================================================

COLOR OF A STAGE

color = baseColor + vertColor * vertex color covers every rgbGen and alphaGen that is the same for the whole
batch or a multiple of the vertex color. The generators that need more per vertex (diffuse light, specular,
portal distance, fog) are still computed on the CPU with the functions of tr_shade_calc.cpp and then go in as
vertex colors.

===============================================================================
*/

// the value RB_CalcWaveColor() and RB_CalcWaveAlpha() give every vertex
static float VK_WaveValue( const waveForm_t *wf, qboolean alpha ) {
	byte color[4] = { 0, 0, 0, 0 };
	const int numVertexes = tess.numVertexes;

	tess.numVertexes = 1;
	if ( alpha ) {
		RB_CalcWaveAlpha( wf, color );
	} else {
		RB_CalcWaveColor( wf, color );
	}
	tess.numVertexes = numVertexes;
	return ( alpha ? color[3] : color[0] ) / 255.0f;
}

static void VK_ByteColor( const byte *in, float *out ) {
	for ( int i = 0; i < 4; i++ ) {
		out[i] = in[i] / 255.0f;
	}
}

// qtrue: the colors are in tess.svars.colors and the draw takes them as they are
static qboolean VK_StageColors( const shaderStage_t *pStage, int forceRGBGen, vkDraw_t *draw ) {
	const trRefEntity_t *ent = backEnd.currentEntity;
	const int rgbGen = forceRGBGen ? forceRGBGen : pStage->rgbGen;
	alphaGen_t alphaGen = pStage->alphaGen;
	float *base = draw->baseColor, *vert = draw->vertColor;
	const float il = tr.identityLight;
	qboolean perVertexRGB = qfalse, perVertexAlpha = qfalse, perVertexFog;
	const qboolean killGen = (qboolean)( tess.shader != tr.projectionShadowShader && tess.shader != tr.shadowShader
		&& ( ent->e.renderfx & ( RF_DISINTEGRATE1 | RF_DISINTEGRATE2 | RF_VOLUMETRIC ) ) );

	VectorSet4( base, 0.0f, 0.0f, 0.0f, 0.0f );
	VectorSet4( vert, 0.0f, 0.0f, 0.0f, 0.0f );

	switch ( rgbGen ) {
	case CGEN_IDENTITY:
		VectorSet4( base, 1.0f, 1.0f, 1.0f, 1.0f );
		break;
	default:
	case CGEN_IDENTITY_LIGHTING:
		VectorSet4( base, il, il, il, il );
		break;
	case CGEN_LIGHTING_DIFFUSE:
		perVertexRGB = qtrue;
		break;
	case CGEN_LIGHTING_DIFFUSE_ENTITY:
		perVertexRGB = qtrue;
		if ( alphaGen == AGEN_IDENTITY && ent->e.shaderRGBA[3] == 0xff ) {
			alphaGen = AGEN_SKIP;	// already got it in this set since it does all 4 components
		}
		break;
	case CGEN_EXACT_VERTEX:
		VectorSet4( vert, 1.0f, 1.0f, 1.0f, 1.0f );
		break;
	case CGEN_CONST:
		VK_ByteColor( pStage->constantColor, base );
		break;
	case CGEN_VERTEX:
		VectorSet4( vert, il, il, il, 1.0f );
		break;
	case CGEN_ONE_MINUS_VERTEX:
		VectorSet4( base, il, il, il, 1.0f );
		VectorSet4( vert, -il, -il, -il, 0.0f );
		break;
	case CGEN_FOG:
		if ( tr.world ) {
			VK_ByteColor( (const byte *)&( tr.world->fogs + tess.fogNum )->colorInt, base );
		}
		break;
	case CGEN_WAVEFORM:
		base[0] = base[1] = base[2] = VK_WaveValue( &pStage->rgbWave, qfalse );
		base[3] = 1.0f;
		break;
	case CGEN_ENTITY:
		VK_ByteColor( ent->e.shaderRGBA, base );
		if ( alphaGen == AGEN_IDENTITY && ent->e.shaderRGBA[3] == 0xff ) {
			alphaGen = AGEN_SKIP;
		}
		break;
	case CGEN_ONE_MINUS_ENTITY:
		VK_ByteColor( ent->e.shaderRGBA, base );
		for ( int i = 0; i < 4; i++ ) {
			base[i] = 1.0f - base[i];
		}
		break;
	case CGEN_LIGHTMAPSTYLE:
		VK_ByteColor( styleColors[pStage->lightmapStyle], base );
		break;
	}

	switch ( alphaGen ) {
	case AGEN_SKIP:
		break;
	case AGEN_IDENTITY:
		if ( rgbGen != CGEN_IDENTITY && ( rgbGen != CGEN_VERTEX || il != 1.0f ) ) {
			base[3] = 1.0f;
			vert[3] = 0.0f;
		}
		break;
	case AGEN_CONST:
		if ( rgbGen != CGEN_CONST ) {
			base[3] = pStage->constantColor[3] / 255.0f;
			vert[3] = 0.0f;
		}
		break;
	case AGEN_WAVEFORM:
		base[3] = VK_WaveValue( &pStage->alphaWave, qtrue );
		vert[3] = 0.0f;
		break;
	case AGEN_ENTITY:
		base[3] = ent->e.shaderRGBA[3] / 255.0f;
		vert[3] = 0.0f;
		break;
	case AGEN_ONE_MINUS_ENTITY:
		base[3] = 1.0f - ent->e.shaderRGBA[3] / 255.0f;
		vert[3] = 0.0f;
		break;
	case AGEN_VERTEX:
		if ( rgbGen != CGEN_VERTEX ) {
			base[3] = 0.0f;
			vert[3] = 1.0f;
		}
		break;
	case AGEN_ONE_MINUS_VERTEX:
		base[3] = 1.0f;
		vert[3] = -1.0f;
		break;
	case AGEN_LIGHTING_SPECULAR:
	case AGEN_PORTAL:
		perVertexAlpha = qtrue;
		break;
	case AGEN_BLEND:
		if ( rgbGen != CGEN_VERTEX ) {
			perVertexAlpha = qtrue;
		}
		break;
	default:
		break;
	}

	perVertexFog = (qboolean)( tess.fogNum && pStage->adjustColorsForFog != ACFF_NONE );
	if ( !killGen && !perVertexRGB && !perVertexAlpha && !perVertexFog ) {
		return qfalse;
	}

	// the slow way: every vertex on the CPU, in the order tr_shade.cpp ComputeColors() has
	color4ub_t *colors = tess.svars.colors;

	for ( int i = 0; i < tess.numVertexes; i++ ) {
		for ( int j = 0; j < 4; j++ ) {
			colors[i][j] = (byte)Com_Clampi( 0, 255, (int)( ( base[j] + vert[j] * tess.vertexColors[i][j] / 255.0f ) * 255.0f + 0.5f ) );
		}
	}
	if ( killGen ) {
		if ( ent->e.renderfx & ( RF_DISINTEGRATE1 | RF_DISINTEGRATE2 ) ) {
			RB_CalcDisintegrateColors( (unsigned char *)colors );
		}
		if ( ent->e.renderfx & RF_VOLUMETRIC ) {
			// does not work for rotated models
			for ( int i = 0; i < tess.numVertexes; i++ ) {
				float d = DotProduct( tess.normal[i], backEnd.refdef.viewaxis[0] );

				d *= d * d * d;
				if ( d < 0.2f ) {
					d = 0.0f;
				}
				colors[i][0] = colors[i][1] = colors[i][2] = colors[i][3] = (byte)Q_ftol( ent->e.shaderRGBA[0] * ( 1.0f - d ) );
			}
		}
	} else {
		if ( rgbGen == CGEN_LIGHTING_DIFFUSE ) {
			RB_CalcDiffuseColor( (unsigned char *)colors );
		} else if ( rgbGen == CGEN_LIGHTING_DIFFUSE_ENTITY ) {
			RB_CalcDiffuseEntityColor( (unsigned char *)colors );
		}
		if ( perVertexRGB && alphaGen != AGEN_SKIP && !perVertexAlpha ) {
			// the diffuse functions write all four components, the alpha of the stage comes after them
			for ( int i = 0; i < tess.numVertexes; i++ ) {
				colors[i][3] = (byte)Com_Clampi( 0, 255, (int)( ( base[3] + vert[3] * tess.vertexColors[i][3] / 255.0f ) * 255.0f + 0.5f ) );
			}
		}
		if ( alphaGen == AGEN_LIGHTING_SPECULAR ) {
			RB_CalcSpecularAlpha( (unsigned char *)colors );
		} else if ( alphaGen == AGEN_PORTAL ) {
			for ( int i = 0; i < tess.numVertexes; i++ ) {
				vec3_t v;
				float len;

				VectorSubtract( tess.xyz[i], backEnd.viewParms.ori.origin, v );
				len = VectorLength( v ) / tess.shader->portalRange;
				colors[i][3] = (byte)( Com_Clamp( 0.0f, 1.0f, len ) * 255.0f );
			}
		} else if ( alphaGen == AGEN_BLEND && perVertexAlpha ) {
			for ( int i = 0; i < tess.numVertexes; i++ ) {
				colors[i][3] = tess.vertexAlphas[i][pStage->index];
			}
		}
	}
	if ( perVertexFog ) {
		switch ( pStage->adjustColorsForFog ) {
		case ACFF_MODULATE_RGB:		RB_CalcModulateColorsByFog( (unsigned char *)colors );	break;
		case ACFF_MODULATE_ALPHA:	RB_CalcModulateAlphasByFog( (unsigned char *)colors );	break;
		case ACFF_MODULATE_RGBA:	RB_CalcModulateRGBAsByFog( (unsigned char *)colors );	break;
		default:					break;
		}
	}
	VectorSet4( base, 0.0f, 0.0f, 0.0f, 0.0f );
	VectorSet4( vert, 1.0f, 1.0f, 1.0f, 1.0f );
	return qtrue;
}

/*
===============================================================================

THE BATCH

===============================================================================
*/

// which side OpenGL culls for the shader: Quake has clockwise front faces
static int VK_CullMode( cullType_t type ) {
	qboolean front = (qboolean)( type == CT_FRONT_SIDED );

	if ( type == CT_TWO_SIDED || backEnd.projection2D ) {
		return VK_CULL_MODE_NONE;
	}
	if ( backEnd.viewParms.isMirror ) {
		front = (qboolean)!front;
	}
	return front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT;
}

static uint32_t VK_TexEnv( int env ) {
	switch ( env ) {
	case GL_MODULATE:	return 1;
	case GL_ADD:		return 2;
	case GL_DECAL:		return 3;
	case GL_REPLACE:	return 4;
	default:			return 1;
	}
}

static uint32_t VK_AlphaTest( uint32_t stateBits ) {
	switch ( stateBits & GLS_ATEST_BITS ) {
	case GLS_ATEST_GT_0:	return 1;
	case GLS_ATEST_LT_80:	return 2;
	case GLS_ATEST_GE_80:	return 3;
	case GLS_ATEST_GE_C0:	return 4;
	default:				return 0;
	}
}

static void VK_IdentityTexCoords( vkDraw_t *draw ) {
	draw->flags |= (uint32_t)TCGEN_TEXTURE << VKD_TCGEN0_SHIFT;
	draw->texMat0[0] = draw->texMat0[5] = 1.0f;
}

/*
===============
VK_GlobalFog

Fog that lies over the whole level (and the special fog of a level) is not a pass of its own: every stage of a
surface in it fades to the fog color with the distance, the way glFog did it for rd-vanilla. Fills the fog
part of the draw, qfalse: this batch has no such fog.
===============
*/
static qboolean VK_GlobalFog( vkDraw_t *draw ) {
	// exp( -( density * depth ) ^ 2 ) is 1 / 255 at depthForOpaque
	static const float logtestExp2 = sqrtf( -logf( 1.0f / 255.0f ) );
	const fog_t *fog;

	if ( !tr.world || !tess.fogNum || !tess.shader->fogPass || r_drawfog->value != 2
		|| ( tess.fogNum != tr.world->globalFog && tess.fogNum != tr.world->numfogs ) ) {
		return qfalse;
	}
	fog = tr.world->fogs + tess.fogNum;
	if ( tr.rangedFog ) {
		// ranged fog, used for sniper scope
		float start = fog->parms.depthForOpaque;

		if ( tr.rangedFog < 0.0f ) {
			// special designer override
			start = -tr.rangedFog;
		} else if ( ( tr.distanceCull - start ) < tr.rangedFog ) {
			// assure a minimum range between fog beginning and cutoff distance
			start = Q_max( tr.distanceCull - tr.rangedFog, 16.0f );
		}
		draw->flags |= 2u << VKD_FOG_SHIFT;
		draw->fogParm[0] = start;
		draw->fogParm[1] = tr.distanceCull;
	} else {
		draw->flags |= 1u << VKD_FOG_SHIFT;
		draw->fogParm[0] = logtestExp2 / fog->parms.depthForOpaque;
	}
	// in the glow pass the fog is black: what is far away glows less
	if ( !g_bRenderGlowingObjects ) {
		VectorCopy( fog->parms.color, draw->fogColor );
	}
	return qtrue;
}

/*
===============
VK_DynamicLights

One more pass over the batch for every dynamic light that reaches it. The light is worked out per pixel in
generic.frag and added to what the stages left there, times the texture of the surface.
===============
*/
static void VK_DynamicLights( uint32_t transform, int cullMode ) {
	const shader_t *shader = tess.shader;
	const shaderStage_t *textured = NULL;
	const textureBundle_t *bundle = NULL;

	if ( !backEnd.refdef.num_dlights || !tr.dlightImage ) {
		return;
	}
	// the first opaque stage with a plain texture is what the light shines on
	for ( int i = 0; i < shader->numUnfoggedPasses && !textured; i++ ) {
		const shaderStage_t *pStage = &shader->stages[i];

		if ( pStage->stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) {
			continue;
		}
		for ( int b = 0; b < NUM_TEXTURE_BUNDLES; b++ ) {
			const textureBundle_t *candidate = &pStage->bundle[b];

			if ( candidate->image && !candidate->isLightmap && !candidate->numTexMods
				&& candidate->tcGen != TCGEN_ENVIRONMENT_MAPPED && candidate->tcGen != TCGEN_FOG ) {
				textured = pStage;
				bundle = candidate;
				break;
			}
		}
	}

	for ( int l = 0; l < backEnd.refdef.num_dlights; l++ ) {
		const dlight_t *dl = &backEnd.refdef.dlights[l];
		vkDraw_t draw;

		if ( !( tess.dlightBits & ( 1 << l ) ) ) {
			continue;	// this surface definately doesn't have any of this light
		}
		memset( &draw, 0, sizeof( draw ) );
		draw.transform = transform;
		draw.flags = VKD_DLIGHT;
		VK_IdentityTexCoords( &draw );
		draw.tex0 = bundle ? VK_BundleTexture( bundle ) : tr.whiteImage->texnum;
		draw.tex1 = tr.dlightImage->texnum;
		VectorCopy( dl->color, draw.baseColor );
		VectorCopy( dl->transformed, draw.parm );
		draw.parm[3] = dl->radius;
		VK_DrawTess( &draw, VKP_GENERIC, ( textured ? ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE ) : ( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE ) )
			| GLS_DEPTHFUNC_EQUAL, cullMode, (qboolean)shader->polygonOffset );
	}
}

/*
===============
VK_FogPass

A surface inside a fog volume gets the fog as one more pass: the fog color, with an alpha that grows with the
way through the fog. The alpha comes from the fog image, the coordinates into it are still computed on the CPU.
===============
*/
static void VK_FogPass( uint32_t transform, int cullMode ) {
	const fog_t *fog = tr.world->fogs + tess.fogNum;
	vkDraw_t draw;

	RB_CalcFogTexCoords( (float *)tess.svars.texcoords[0] );
	VK_TessTexCoords( tess.svars.texcoords[0] );
	memset( &draw, 0, sizeof( draw ) );
	draw.transform = transform;
	VK_IdentityTexCoords( &draw );
	draw.tex0 = draw.tex1 = tr.fogImage->texnum;
	VK_ByteColor( (const byte *)&fog->colorInt, draw.baseColor );
	VK_DrawTess( &draw, VKP_GENERIC, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA
		| ( tess.shader->fogPass == FP_EQUAL ? GLS_DEPTHFUNC_EQUAL : 0 ), cullMode, (qboolean)tess.shader->polygonOffset );
	VK_TessTexCoords( NULL );
}

/*
===============
RB_StageIteratorGeneric

The stages of tess.shader on the geometry in tess
===============
*/
void RB_StageIteratorGeneric( void ) {
	const shader_t *shader = tess.shader;
	uint32_t transform;
	int cullMode;

	RB_DeformTessGeometry();
	if ( !VK_UploadTess() ) {
		return;
	}
	transform = VK_CurrentTransform();
	cullMode = VK_CullMode( shader->cullType );

	for ( int stage = 0; stage < shader->numUnfoggedPasses; stage++ ) {
		const shaderStage_t *pStage = &tess.xstages[stage];
		const trRefEntity_t *ent = backEnd.currentEntity;
		uint32_t stateBits;
		int forceRGBGen = 0;
		qboolean cpuColors = qfalse;
		vkDraw_t draw;

		if ( !pStage->active ) {
			break;
		}
		// in the glow pass only the stages that glow
		if ( g_bRenderGlowingObjects && !pStage->glow ) {
			continue;
		}
		if ( stage && r_lightmap->integer
			&& !( pStage->bundle[0].isLightmap || pStage->bundle[1].isLightmap || pStage->bundle[0].vertexLightmap ) ) {
			break;
		}
		// surface sprites are generated from the stage, they are not a stage that is drawn
		if ( pStage->ss && pStage->ss->surfaceSpriteType ) {
			continue;
		}

		stateBits = pStage->stateBits;
		if ( ent->e.renderfx & RF_DISINTEGRATE1 ) {
			// we want to be able to rip a hole in the thing being disintegrated, and by doing the depth-testing it avoids some kinds of artefacts, but will probably introduce others?
			stateBits = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHMASK_TRUE | GLS_ATEST_GE_C0;
		}
		if ( ent->e.renderfx & RF_RGB_TINT ) {
			forceRGBGen = CGEN_ENTITY;
		}

		memset( &draw, 0, sizeof( draw ) );
		draw.transform = transform;
		draw.parm[0] = shader->portalRange;
		draw.parm[1] = tess.shaderTime;
		if ( !tess.fading ) {
			cpuColors = VK_StageColors( pStage, forceRGBGen, &draw );
		} else {
			// a surface that fades out brings its colors in tess.svars.colors
			draw.vertColor[0] = draw.vertColor[1] = draw.vertColor[2] = draw.vertColor[3] = 1.0f;
			cpuColors = qtrue;
		}

		if ( pStage->bundle[0].vertexLightmap && r_vertexLight->integer && !r_uiFullScreen->integer && r_lightmap->integer ) {
			draw.tex0 = tr.whiteImage->texnum;
		} else {
			draw.tex0 = VK_BundleTexture( &pStage->bundle[0] );
		}
		VK_BundleTexCoords( &pStage->bundle[0], draw.texMat0, draw.texTurb0 );
		draw.flags = VK_BundleTcGen( &pStage->bundle[0], draw.tcGenVec0 ) << VKD_TCGEN0_SHIFT;
		draw.tex1 = draw.tex0;
		if ( pStage->bundle[1].image ) {
			draw.tex1 = VK_BundleTexture( &pStage->bundle[1] );
			VK_BundleTexCoords( &pStage->bundle[1], draw.texMat1, draw.texTurb1 );
			draw.flags |= VK_BundleTcGen( &pStage->bundle[1], draw.tcGenVec1 ) << VKD_TCGEN1_SHIFT;
			draw.flags |= ( r_lightmap->integer ? 4u : VK_TexEnv( shader->multitextureEnv ) ) << VKD_TEXENV_SHIFT;
		} else if ( ent->e.renderfx & RF_FORCE_ENT_ALPHA ) {
			// the alpha of the entity over whatever the stage says
			draw.baseColor[3] = ent->e.shaderRGBA[3] / 255.0f;
			draw.vertColor[3] = 0.0f;
			stateBits = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
			if ( ent->e.renderfx & RF_ALPHA_DEPTH ) {
				// depth write, so faces through the model will be stomped over by nearer ones
				stateBits |= GLS_DEPTHMASK_TRUE;
			}
		}
		draw.flags |= VK_AlphaTest( stateBits );
		if ( VK_GlobalFog( &draw ) && pStage->mGLFogColorOverride ) {
			// some stages want to fade to black or white, not to the fog color
			const float c = ( pStage->mGLFogColorOverride == GLFOGOVERRIDE_WHITE ) ? 1.0f : 0.0f;

			VectorSet( draw.fogColor, c, c, c );
		}

		if ( cpuColors ) {
			VK_TessColors( tess.svars.colors );
		}
		VK_DrawTess( &draw, VKP_GENERIC, stateBits, cullMode, (qboolean)shader->polygonOffset );
		if ( cpuColors ) {
			VK_TessColors( NULL );
		}
	}

	// now do any dynamic lighting needed
	if ( tess.dlightBits && shader->sort <= SS_OPAQUE && !( shader->surfaceFlags & ( SURF_NODLIGHT | SURF_SKY ) )
		&& !g_bRenderGlowingObjects ) {
		VK_DynamicLights( transform, cullMode );
	}
	// now do fog
	if ( tr.world && ( tess.fogNum != tr.world->globalFog || r_drawfog->value != 2 ) && r_drawfog->value && tess.fogNum
		&& shader->fogPass ) {
		VK_FogPass( transform, cullMode );
	}
}

/*
===============
RB_BeginSurface

We must set some things up before beginning any tesselation, because a surface may be forced to perform a
RB_End due to overflow.
===============
*/
void RB_BeginSurface( shader_t *shader, int fogNum ) {
	shader_t *state = ( shader->remappedShader ) ? shader->remappedShader : shader;

	tess.numIndexes = 0;
	tess.numVertexes = 0;
	tess.shader = state;
	tess.fogNum = fogNum;
	tess.dlightBits = 0;		// will be OR'd in by surface functions
	tess.xstages = state->stages;
	tess.numPasses = state->numUnfoggedPasses;
	tess.currentStageIteratorFunc = shader->sky ? RB_StageIteratorSky : RB_StageIteratorGeneric;
	tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
	if ( tess.shader->clampTime && tess.shaderTime >= tess.shader->clampTime ) {
		tess.shaderTime = tess.shader->clampTime;
	}
	tess.fading = false;
	tess.registration++;
}

/*
===============
RB_EndSurface
===============
*/
void RB_EndSurface( void ) {
	if ( tess.numIndexes == 0 ) {
		return;
	}
	if ( tess.indexes[SHADER_MAX_INDEXES - 1] != 0 ) {
		Com_Error( ERR_DROP, "RB_EndSurface() - SHADER_MAX_INDEXES hit" );
	}
	if ( tess.xyz[SHADER_MAX_VERTEXES - 1][0] != 0 ) {
		Com_Error( ERR_DROP, "RB_EndSurface() - SHADER_MAX_VERTEXES hit" );
	}

	// stencil shadows are not done in this renderer yet
	if ( tess.shader == tr.shadowShader ) {
		tess.numIndexes = 0;
		return;
	}
	// for debugging of sort order issues, stop rendering after a given sort value
	if ( r_debugSort->integer && r_debugSort->integer < tess.shader->sort ) {
		return;
	}
	if ( skyboxportal ) {
		// world
		if ( !( backEnd.refdef.rdflags & RDF_SKYBOXPORTAL ) ) {
			if ( tess.currentStageIteratorFunc == RB_StageIteratorSky ) {
				// don't process these tris at all
				return;
			}
		}
		// portal sky
		else if ( !drawskyboxportal ) {
			if ( tess.currentStageIteratorFunc != RB_StageIteratorSky ) {
				// /only/ process sky tris
				return;
			}
		}
	}

	backEnd.pc.c_shaders++;
	backEnd.pc.c_vertexes += tess.numVertexes;
	backEnd.pc.c_indexes += tess.numIndexes;
	backEnd.pc.c_totalIndexes += tess.numIndexes * tess.numPasses;

	tess.currentStageIteratorFunc();

	// clear shader so we can tell we don't have any unclosed surfaces
	tess.numIndexes = 0;
}

void RB_ProjectionShadowDeform( void ) {
}
