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

// vk_backend.cpp: runs the render commands of a frame: 2D pictures, the views of the scene, begin and end
//
// The front end (tr_cmds.cpp, tr_scene.cpp, tr_main.cpp of rd-vanilla) fills a list of commands while the game
// builds its frame. This file is what tr_backend.cpp is for rd-vanilla.

#include "vk_local.h"

backEndData_t	*backEndData;
backEndState_t	backEnd;

extern int skyboxportal;

// the refraction effect of force push and of some weapons, set by the game through the renderer interface
float		tr_distortionAlpha = 1.0f;		// opaque
float		tr_distortionStretch = 0.0f;	// no stretch override
qboolean	tr_distortionPrePost = qfalse;	// capture before postrender phase?
qboolean	tr_distortionNegate = qfalse;	// negative blend mode

/*
===============
RB_SetGL2D

Pictures are placed on a screen of 640 x 480, whatever the size of the frame
===============
*/
static void RB_SetGL2D( void ) {
	float ortho[16];

	backEnd.projection2D = qtrue;
	VK_SetViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight, 0.0f, 1.0f );

	// glOrtho( 0, 640, 480, 0, 0, 1 )
	memset( ortho, 0, sizeof( ortho ) );
	ortho[0] = 2.0f / 640.0f;
	ortho[5] = -2.0f / 480.0f;
	ortho[10] = -2.0f;
	ortho[12] = -1.0f;
	ortho[13] = 1.0f;
	ortho[14] = -1.0f;
	ortho[15] = 1.0f;
	VK_SetProjection( ortho );

	memset( &backEnd.ori, 0, sizeof( backEnd.ori ) );
	backEnd.ori.axis[0][0] = backEnd.ori.axis[1][1] = backEnd.ori.axis[2][2] = 1.0f;
	backEnd.ori.modelMatrix[0] = backEnd.ori.modelMatrix[5] = backEnd.ori.modelMatrix[10] = backEnd.ori.modelMatrix[15] = 1.0f;

	// set time for 2D shaders
	backEnd.refdef.time = ri.Milliseconds() * ri.Cvar_VariableValue( "timescale" );
	backEnd.refdef.floatTime = backEnd.refdef.time * 0.001f;
}

/*
===============
VK_Splash

The picture of the game on the window as soon as there is one, as R_Splash() of rd-vanilla puts it there: get
something on screen asap, the rest of the start takes a moment in which nothing else is drawn
===============
*/
void VK_Splash( void ) {
	image_t *image = R_FindImageFile( "menu/splash", qfalse, qfalse, qfalse, GL_CLAMP );

	if ( !image ) {
		return;		// invalid paths?
	}
	VK_BeginFrame();
	if ( !vk.frame ) {
		return;
	}
	RB_SetGL2D();
	GL_Bind( image );
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	VK_GL_Color( 1.0f, 1.0f, 1.0f, 1.0f );
	VK_GL_Begin( GL_TRIANGLE_STRIP );
	VK_GL_TexCoord( 0.0f, 0.0f );
	VK_GL_Vertex( 0.0f, 0.0f, 0.0f );
	VK_GL_TexCoord( 1.0f, 0.0f );
	VK_GL_Vertex( 640.0f, 0.0f, 0.0f );
	VK_GL_TexCoord( 0.0f, 1.0f );
	VK_GL_Vertex( 0.0f, 480.0f, 0.0f );
	VK_GL_TexCoord( 1.0f, 1.0f );
	VK_GL_Vertex( 640.0f, 480.0f, 0.0f );
	VK_GL_End();
	VK_EndFrame();
	backEnd.projection2D = qfalse;
}

// four vertices and two triangles in the tess arrays for a picture, returns the first vertex
static int RB_Begin2DQuad( shader_t *shader ) {
	int numVerts, numIndexes;

	VK_BeginFrame();
	if ( !backEnd.projection2D ) {
		RB_SetGL2D();
	}
	if ( shader != tess.shader ) {
		if ( tess.numIndexes ) {
			RB_EndSurface();
		}
		backEnd.currentEntity = &backEnd.entity2D;
		RB_BeginSurface( shader, 0 );
	}
	RB_CHECKOVERFLOW( 4, 6 );
	numVerts = tess.numVertexes;
	numIndexes = tess.numIndexes;
	tess.numVertexes += 4;
	tess.numIndexes += 6;

	tess.indexes[numIndexes] = numVerts + 3;
	tess.indexes[numIndexes + 1] = numVerts + 0;
	tess.indexes[numIndexes + 2] = numVerts + 2;
	tess.indexes[numIndexes + 3] = numVerts + 2;
	tess.indexes[numIndexes + 4] = numVerts + 0;
	tess.indexes[numIndexes + 5] = numVerts + 1;

	for ( int i = 0; i < 4; i++ ) {
		byteAlias_t *dest = (byteAlias_t *)&tess.vertexColors[numVerts + i];

		dest->ui = ( (byteAlias_t *)&backEnd.color2D )->ui;
		tess.xyz[numVerts + i][2] = 0.0f;
	}
	return numVerts;
}

static void RB_Set2DTexCoords( int numVerts, float s1, float t1, float s2, float t2 ) {
	VectorSet2( tess.texCoords[numVerts][0], s1, t1 );
	VectorSet2( tess.texCoords[numVerts + 1][0], s2, t1 );
	VectorSet2( tess.texCoords[numVerts + 2][0], s2, t2 );
	VectorSet2( tess.texCoords[numVerts + 3][0], s1, t2 );
}

static const void *RB_SetColor( const void *data ) {
	const setColorCommand_t *cmd = (const setColorCommand_t *)data;

	for ( int i = 0; i < 4; i++ ) {
		backEnd.color2D[i] = (byte)( cmd->color[i] * 255 );
	}
	return (const void *)( cmd + 1 );
}

static const void *RB_StretchPic( const void *data ) {
	const stretchPicCommand_t *cmd = (const stretchPicCommand_t *)data;
	const int numVerts = RB_Begin2DQuad( cmd->shader );

	VectorSet2( tess.xyz[numVerts], cmd->x, cmd->y );
	VectorSet2( tess.xyz[numVerts + 1], cmd->x + cmd->w, cmd->y );
	VectorSet2( tess.xyz[numVerts + 2], cmd->x + cmd->w, cmd->y + cmd->h );
	VectorSet2( tess.xyz[numVerts + 3], cmd->x, cmd->y + cmd->h );
	RB_Set2DTexCoords( numVerts, cmd->s1, cmd->t1, cmd->s2, cmd->t2 );
	return (const void *)( cmd + 1 );
}

// a picture turned around its upper right corner (centered: around its middle)
static const void *RB_RotatePic( const void *data, qboolean centered ) {
	const rotatePicCommand_t *cmd = (const rotatePicCommand_t *)data;
	const float angle = DEG2RAD( cmd->a );
	const float s = sinf( angle ), c = cosf( angle );
	// the corners in front of the rotation, relative to the point the picture turns around
	const float left = centered ? -cmd->w * 0.5f : -cmd->w, right = centered ? cmd->w * 0.5f : 0.0f;
	const float top = centered ? -cmd->h * 0.5f : 0.0f, bottom = centered ? cmd->h * 0.5f : cmd->h;
	const float ox = centered ? cmd->x : cmd->x + cmd->w, oy = cmd->y;
	const float corners[4][2] = { { left, top }, { right, top }, { right, bottom }, { left, bottom } };

	if ( !cmd->shader->numUnfoggedPasses ) {
		return (const void *)( cmd + 1 );
	}
	const int numVerts = RB_Begin2DQuad( cmd->shader );

	for ( int i = 0; i < 4; i++ ) {
		tess.xyz[numVerts + i][0] = c * corners[i][0] - s * corners[i][1] + ox;
		tess.xyz[numVerts + i][1] = s * corners[i][0] + c * corners[i][1] + oy;
	}
	RB_Set2DTexCoords( numVerts, cmd->s1, cmd->t1, cmd->s2, cmd->t2 );
	return (const void *)( cmd + 1 );
}

/*
===============
RB_BeginDrawingView

Any mirrored or portaled views have already been drawn, so prepare to actually render the visible surfaces
for this view
===============
*/
static void RB_BeginDrawingView( void ) {
	const viewParms_t *view = &backEnd.viewParms;

	// the stencil buffer is cleared with the depth below, or not looked at again (the glow pass)
	tr_stencilled = false;

	// we will need to change the projection matrix before drawing 2D images again
	backEnd.projection2D = qfalse;
	VK_SetViewport( view->viewportX, view->viewportY, view->viewportWidth, view->viewportHeight, 0.0f, 1.0f );
	VK_SetProjection( view->projectionMatrix );

	// in the glow pass the depth of the scene stays
	if ( !g_bRenderGlowingObjects ) {
		VK_ClearDepth( view->viewportX, view->viewportY, view->viewportWidth, view->viewportHeight );
	}
	if ( backEnd.refdef.rdflags & RDF_HYPERSPACE ) {
		// a flashing gray while the level changes under the player
		const float c = ( backEnd.refdef.time & 255 ) / 255.0f;
		const float color[4] = { c, c, c, 1.0f };

		VK_ClearColor( view->viewportX, view->viewportY, view->viewportWidth, view->viewportHeight, color );
		backEnd.isHyperspace = qtrue;
		return;
	}
	if ( skyboxportal ) {
		// portal scene, clear whatever is necessary
		if ( ( backEnd.refdef.rdflags & RDF_SKYBOXPORTAL ) && ( r_fastsky->integer || ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) ) ) {
			const float color[4] = { 0.5f, 0.5f, 0.5f, 1.0f };

			VK_ClearColor( view->viewportX, view->viewportY, view->viewportWidth, view->viewportHeight, color );
		}
	} else if ( r_fastsky->integer && !( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) && !g_bRenderGlowingObjects ) {
		const float color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

		VK_ClearColor( view->viewportX, view->viewportY, view->viewportWidth, view->viewportHeight, color );
	}
	if ( ( ( tr.refdef.rdflags & RDF_AUTOMAP )
		|| ( !( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) && r_DynamicGlow->integer && !g_bRenderGlowingObjects ) )
		&& tr.world && tr.world->globalFog != -1 ) {
		// this is because of a bug in multiple scenes I think, it needs to clear for the second scene but it doesn't normally.
		const fog_t *fog = &tr.world->fogs[tr.world->globalFog];
		const float color[4] = { fog->parms.color[0], fog->parms.color[1], fog->parms.color[2], 1.0f };

		VK_ClearColor( view->viewportX, view->viewportY, view->viewportWidth, view->viewportHeight, color );
	}
	backEnd.isHyperspace = qfalse;
	// we haven't drawn anything yet this view
	backEnd.skyRenderedThisView = qfalse;
}

/*
===============
SetViewportAndScissor

The viewport and the projection of the view again, for code of the shared files that draws after the scene
(the weather)
===============
*/
void SetViewportAndScissor( void ) {
	const viewParms_t *view = &backEnd.viewParms;

	VK_SetViewport( view->viewportX, view->viewportY, view->viewportWidth, view->viewportHeight, 0.0f, 1.0f );
	VK_SetProjection( view->projectionMatrix );
}

// the part of the depth buffer an entity is drawn into: all of it, the front for the view weapon, none
static void RB_SetDepthRange( int depthRange ) {
	const viewParms_t *view = &backEnd.viewParms;

	VK_SetViewport( view->viewportX, view->viewportY, view->viewportWidth, view->viewportHeight, 0.0f,
		depthRange == 1 ? 0.3f : ( depthRange == 2 ? 0.0f : 1.0f ) );
}

/*
===============
RB_DistortionFill

Surfaces with the shader internal_distortion have marked their shape in the stencil buffer (RB_StageIteratorGeneric).
That shape is filled with a copy of the screen that is stretched a little, more and less with the time, so that
what lies behind seems to bend: a cloaked player, the trail of cg_saberTrail 2. This is RB_DistortionFill() of
rd-vanilla (tr_shadows.cpp) with its numbers.

rd-vanilla copies a part of the screen into a texture for this, a centered rectangle whose sides are the largest
powers of two that fit, and puts that over the whole view. The copy here is the whole frame, so the texture
coordinates are moved into that rectangle.
===============
*/
static void RB_DistortionQuad( float spost, float spost2, float alpha ) {
	const int width = glConfig.vidWidth, height = glConfig.vidHeight;
	// x, y, s, t as rd-vanilla has them
	const float corners[4][4] = { { 0.0f, 0.0f, spost2, 1.0f - spost }, { 0.0f, (float)height, spost2, spost },
		{ (float)width, (float)height, 1.0f - spost2, spost }, { (float)width, 0.0f, 1.0f - spost2, 1.0f - spost } };
	// the rectangle of RB_CaptureScreenImage()
	int radX = Q_min( 2048, glConfig.maxTextureSize ), radY = radX;

	while ( width < radX && radX > 1 ) {
		radX /= 2;
	}
	while ( height < radY && radY > 1 ) {
		radY /= 2;
	}
	const int cX = width / 2 - radX / 2, cY = height / 2 - radY / 2;

	VK_GL_Begin( GL_QUADS );
	VK_GL_Color( 1.0f, 1.0f, 1.0f, alpha );
	for ( int i = 0; i < 4; i++ ) {
		// t counts from the bottom of the screen, the copy has its first row at the top
		VK_GL_TexCoord( ( cX + corners[i][2] * radX ) / width, 1.0f - ( cY + corners[i][3] * radY ) / height );
		VK_GL_Vertex( corners[i][0], corners[i][1], 0.0f );
	}
	VK_GL_End();
}

static void RB_DistortionFill( void ) {
	const float width = (float)glConfig.vidWidth, height = (float)glConfig.vidHeight;
	const qboolean projection2D = backEnd.projection2D;
	float alpha = tr_distortionAlpha;
	float spost, spost2;
	float ortho[16];

	if ( !vk.frame || !tr.screenImage ) {
		return;
	}
	// ok, cap the stupid thing now I guess
	VK_CaptureFrame();

	// reset the view matrices and go into ortho mode: glOrtho( 0, width, height, 32, -1, 1 )
	memset( ortho, 0, sizeof( ortho ) );
	ortho[0] = 2.0f / width;
	ortho[5] = 2.0f / ( 32.0f - height );
	ortho[10] = -1.0f;
	ortho[12] = -1.0f;
	ortho[13] = -( 32.0f + height ) / ( 32.0f - height );
	ortho[15] = 1.0f;
	// as 2D the plane of a portal does not clip it
	backEnd.projection2D = qtrue;
	VK_SetProjection( ortho );
	VK_GL_PushMatrix();
	VK_GL_LoadMatrix( NULL );

	VK_GL_Enable( GL_STENCIL_TEST, 1 );
	VK_GL_StencilFunc( GL_NOTEQUAL, 0 );
	VK_GL_StencilOp( GL_FRONT_AND_BACK, GL_KEEP, GL_KEEP, GL_KEEP );
	GL_Bind( tr.screenImage );

	if ( tr_distortionStretch ) {
		// override
		spost = spost2 = tr_distortionStretch;
	} else {
		// do slow stretchy effect
		spost = fabsf( sinf( tr.refdef.time * 0.0005f ) ) * 0.2f;
		spost2 = fabsf( sinf( tr.refdef.time * 0.0005f ) ) * 0.08f;
	}
	// blend, or be sure to reset the draw state
	GL_State( alpha != 1.0f ? ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_SRC_ALPHA ) : 0 );
	RB_DistortionQuad( spost, spost2, alpha );

	if ( tr_distortionAlpha == 1.0f && tr_distortionStretch == 0.0f ) {
		// no overrides
		if ( tr_distortionNegate ) {
			// probably the crazy alternate saber trail
			alpha = 0.8f;
			GL_State( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE_MINUS_SRC_COLOR );
		} else {
			alpha = 0.5f;
			GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_SRC_ALPHA );
		}
		spost = fabsf( sinf( tr.refdef.time * 0.0008f ) ) * 0.08f;
		spost2 = fabsf( sinf( tr.refdef.time * 0.0008f ) ) * 0.2f;
		RB_DistortionQuad( spost, spost2, alpha );
	}

	// pop the view matrices back
	VK_GL_Enable( GL_STENCIL_TEST, 0 );
	VK_GL_PopMatrix();
	backEnd.projection2D = projection2D;
	VK_SetProjection( backEnd.viewParms.projectionMatrix );
}

// entities that have to be drawn after everything else: the ones that fade (RF_FORCE_ENT_ALPHA), the ones that
// ask for it (RF_FORCEPOST) and the ones that bend the screen (RF_DISTORTION)
#define MAX_POST_RENDERS	128

typedef struct postRender_s {
	int			fogNum;
	int			entNum;
	int			dlighted;
	int			depthRange;
	drawSurf_t	*drawSurf;
	shader_t	*shader;
} postRender_t;

static postRender_t	g_postRenders[MAX_POST_RENDERS];
static int			g_numPostRenders;

// makes the entity the one that is drawn: its time, its matrices, its lights. Returns its depth range.
static int RB_SetEntity( int entityNum, float originalTime ) {
	int depthRange = 0;

	if ( entityNum != REFENTITYNUM_WORLD ) {
		backEnd.currentEntity = &backEnd.refdef.entities[entityNum];
		backEnd.refdef.floatTime = originalTime - backEnd.currentEntity->e.shaderTime;
		// we have to reset the shaderTime as well otherwise image animations start from the wrong frame
		tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;

		// set up the transformation matrix
		R_RotateForEntity( backEnd.currentEntity, &backEnd.viewParms, &backEnd.ori );
		// set up the dynamic lighting if needed
		if ( backEnd.currentEntity->needDlights ) {
			R_TransformDlights( backEnd.refdef.num_dlights, backEnd.refdef.dlights, &backEnd.ori );
		}
		if ( backEnd.currentEntity->e.renderfx & RF_NODEPTH ) {
			// No depth at all, very rare but some things for seeing through walls
			depthRange = 2;
		} else if ( backEnd.currentEntity->e.renderfx & RF_DEPTHHACK ) {
			// hack the depth range to prevent view model from poking into walls
			depthRange = 1;
		}
	} else {
		backEnd.currentEntity = &tr.worldEntity;
		backEnd.refdef.floatTime = originalTime;
		backEnd.ori = backEnd.viewParms.world;
		// we have to reset the shaderTime as well otherwise image animations on the world (like water)
		// continue with the wrong frame
		tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
		R_TransformDlights( backEnd.refdef.num_dlights, backEnd.refdef.dlights, &backEnd.ori );
	}
	VK_InvalidateTransform();
	return depthRange;
}

/*
===============
VK_ShaderMergesEntities, VK_EntitiesMerge

An effect of the game is many entities that are one sprite or one line each (a fight has more than a thousand of
them in a frame), and a batch ends where the entity changes. rd-vanilla only lets it go on for a shader that
says entityMergable. Here it also goes on when that changes nothing in the picture.

The vertices and their colors are made for each surface with its own entity. What a batch takes from an entity
when it is drawn: its matrices (all entities that are not models have the ones of the level), the time of its
shader, its color for rgbGen and alphaGen entity, its light, its texture offset for tcMod entityTranslate, the
frame of an animMap, and what some render flags ask for (depth range, forced alpha, tint, burning away). So the
shader must take none of that, or it must be the same for both entities.

VK_ShaderMergesEntities: 0 the shader takes something from its entity, 1 it takes nothing, 2 only the time.
===============
*/
cvar_t	*r_vkMerge;		// 0: a batch ends with its entity, as in rd-vanilla

// the render flags that change how a batch is drawn
#define VK_RF_BATCH	( RF_DEPTHHACK | RF_NODEPTH | RF_VOLUMETRIC | RF_FORCE_ENT_ALPHA | RF_RGB_TINT | RF_DISTORTION \
	| RF_DISINTEGRATE1 | RF_DISINTEGRATE2 | RF_SETANIMINDEX | RF_ALPHA_DEPTH | RF_FORCEPOST )

static int VK_ShaderMergesEntities( const shader_t *shader ) {
	int result = 1;

	if ( shader->sky || shader == tr.shadowShader || shader == tr.projectionShadowShader ) {
		return 0;
	}
	for ( int i = 0; i < shader->numDeforms; i++ ) {
		switch ( shader->deforms[i]->deformation ) {
		case DEFORM_WAVE:
		case DEFORM_NORMALS:
		case DEFORM_BULGE:
		case DEFORM_MOVE:
			result = 2;
			break;
		case DEFORM_PROJECTION_SHADOW:
			return 0;
		default:
			// the sprites and the texts are made vertex by vertex from the view
			break;
		}
	}
	for ( int i = 0; i < shader->numUnfoggedPasses; i++ ) {
		const shaderStage_t *pStage = &shader->stages[i];

		if ( !pStage->active ) {
			break;
		}
		if ( pStage->ss ) {
			return 0;
		}
		switch ( pStage->rgbGen ) {
		case CGEN_IDENTITY:
		case CGEN_IDENTITY_LIGHTING:
		case CGEN_EXACT_VERTEX:
		case CGEN_VERTEX:
		case CGEN_ONE_MINUS_VERTEX:
		case CGEN_CONST:
		case CGEN_FOG:
		case CGEN_LIGHTMAPSTYLE:
			break;
		case CGEN_WAVEFORM:
			result = 2;
			break;
		default:
			return 0;
		}
		switch ( pStage->alphaGen ) {
		case AGEN_IDENTITY:
		case AGEN_SKIP:
		case AGEN_VERTEX:
		case AGEN_ONE_MINUS_VERTEX:
		case AGEN_CONST:
		case AGEN_PORTAL:
			break;
		case AGEN_WAVEFORM:
			result = 2;
			break;
		default:
			return 0;
		}
		for ( int b = 0; b < NUM_TEXTURE_BUNDLES; b++ ) {
			const textureBundle_t *bundle = &pStage->bundle[b];

			if ( bundle->numImageAnimations > 1 ) {
				result = 2;
			}
			for ( int k = 0; k < bundle->numTexMods; k++ ) {
				switch ( bundle->texMods[k].type ) {
				case TMOD_NONE:
				case TMOD_TRANSFORM:
				case TMOD_SCALE:
					break;
				case TMOD_ENTITY_TRANSLATE:
					return 0;
				default:
					result = 2;
					break;
				}
			}
		}
	}
	return result;
}

// qtrue: the surfaces of entity b can go into the batch of this shader that has surfaces of entity a
static qboolean VK_EntitiesMerge( int shaderMerges, int a, int b ) {
	const trRefEntity_t *ea, *eb;

	if ( !shaderMerges || a < 0 || a == REFENTITYNUM_WORLD || b == REFENTITYNUM_WORLD ) {
		return qfalse;
	}
	ea = &backEnd.refdef.entities[a];
	eb = &backEnd.refdef.entities[b];
	// a model has matrices of its own
	if ( ea->e.reType == RT_MODEL || eb->e.reType == RT_MODEL ) {
		return qfalse;
	}
	if ( ( ea->e.renderfx | eb->e.renderfx ) & VK_RF_BATCH ) {
		return qfalse;
	}
	// deformVertexes autosprite sizes its sprites by such axes
	if ( ea->e.nonNormalizedAxes || eb->e.nonNormalizedAxes ) {
		return qfalse;
	}
	// RB_SetEntity() moves the lights for an entity that needs them
	if ( ea->needDlights != eb->needDlights ) {
		return qfalse;
	}
	if ( shaderMerges == 2 && ea->e.shaderTime != eb->e.shaderTime ) {
		return qfalse;
	}
	return qtrue;
}

/*
===============
RB_CaptureForEntity

An entity that bends the screen behind it: copy the frame and tell the stages which square of it is theirs.
The square is e.radius pixels wide and lies around the origin of the entity on the screen.
===============
*/
static void RB_CaptureForEntity( const trRefEntity_t *ent ) {
	const int xcenter = glConfig.vidWidth / 2, ycenter = glConfig.vidHeight / 2;
	const int rad = (int)ent->e.radius;
	vec3_t local;
	float left, up, forward;
	int x, y, cX, cY;

	VectorSubtract( ent->e.origin, backEnd.refdef.vieworg, local );
	left = DotProduct( local, backEnd.refdef.viewaxis[1] );
	up = DotProduct( local, backEnd.refdef.viewaxis[2] );
	forward = DotProduct( local, backEnd.refdef.viewaxis[0] );
	if ( forward < 0.01f || rad <= 0 ) {
		// behind the view: what was captured last stays
		return;
	}
	// as R_WorldCoordToScreenCoord() of rd-vanilla has it: x counts from the right edge, y from the top
	x = (int)( xcenter + xcenter / forward * ( 90.0f / backEnd.refdef.fov_x ) * left );
	y = (int)( ycenter - ycenter / forward * ( 90.0f / backEnd.refdef.fov_y ) * up );
	cX = glConfig.vidWidth - x - rad / 2;
	cY = glConfig.vidHeight - y - rad / 2;
	// would it go off screen? cap it off at 0
	cX = ( cX + rad > glConfig.vidWidth ) ? glConfig.vidWidth - rad : ( cX < 0 ? 0 : cX );
	cY = ( cY + rad > glConfig.vidHeight ) ? glConfig.vidHeight - rad : ( cY < 0 ? 0 : cY );

	VK_CaptureFrame();
	VK_SetRefraction( qtrue, cX, cY, rad );
}

/*
===============
RB_RenderDrawSurfList
===============
*/
static void RB_RenderDrawSurfList( drawSurf_t *drawSurfs, int numDrawSurfs ) {
	shader_t *shader = NULL, *oldShader = NULL;
	int fogNum = 0, oldFogNum = -1;
	int entityNum = 0, oldEntityNum = -1;
	int dlighted = 0, oldDlighted = qfalse;
	int depthRange = 0, oldDepthRange = 0;
	unsigned int oldSort = (unsigned int)-1;
	const float originalTime = backEnd.refdef.floatTime;	// save original time for entity shader offsets
	drawSurf_t *drawSurf = drawSurfs;
	// the stencil shadows are darkened once, and not in the glow pass
	qboolean didShadowPass = (qboolean)g_bRenderGlowingObjects;
	// what VK_ShaderMergesEntities() says about a shader, asked once for a run of surfaces
	const shader_t *mergeShader = NULL;
	int shaderMerges = 0;
	// the entity came into a batch that was begun, RB_SetEntity() has not run for it
	qboolean mergedEntity = qfalse;

	RB_BeginDrawingView();
	if ( backEnd.isHyperspace ) {
		return;
	}
	backEnd.currentEntity = &tr.worldEntity;
	backEnd.pc.c_surfaces += numDrawSurfs;

	for ( int i = 0; i < numDrawSurfs; i++, drawSurf++ ) {
		if ( drawSurf->sort == oldSort ) {
			// fast path, same as previous sort
			VK_AddSurface( drawSurf->surface );
			continue;
		}
		R_DecomposeSort( drawSurf->sort, &entityNum, &shader, &fogNum, &dlighted );
		// if we're rendering glowing objects, and this shader has no stages with glow, skip it
		if ( g_bRenderGlowingObjects && !shader->hasGlow ) {
			shader = oldShader;
			entityNum = oldEntityNum;
			fogNum = oldFogNum;
			dlighted = oldDlighted;
			continue;
		}
		oldSort = drawSurf->sort;

		if ( entityNum != REFENTITYNUM_WORLD && g_numPostRenders < MAX_POST_RENDERS
			&& ( backEnd.refdef.entities[entityNum].e.renderfx & ( RF_DISTORTION | RF_FORCEPOST | RF_FORCE_ENT_ALPHA ) ) ) {
			// must render last
			const trRefEntity_t *ent = &backEnd.refdef.entities[entityNum];
			postRender_t *post = &g_postRenders[g_numPostRenders++];

			post->depthRange = ( ent->e.renderfx & RF_NODEPTH ) ? 2 : ( ( ent->e.renderfx & RF_DEPTHHACK ) ? 1 : 0 );
			post->entNum = entityNum;
			post->drawSurf = drawSurf;
			post->dlighted = dlighted;
			post->fogNum = fogNum;
			post->shader = shader;

			shader = oldShader;
			entityNum = oldEntityNum;
			fogNum = oldFogNum;
			dlighted = oldDlighted;
			oldSort = (unsigned int)-20;	// invalidate this thing, cause we may want to postrender more surfs of the same sort
			continue;
		}

		// the sprites of an effect are an entity each: they stay one batch where that changes nothing
		qboolean merge = qfalse;

		if ( entityNum != oldEntityNum && shader == oldShader && fogNum == oldFogNum && dlighted == oldDlighted
			&& !shader->entityMergable && r_vkMerge->integer ) {
			if ( shader != mergeShader ) {
				mergeShader = shader;
				shaderMerges = VK_ShaderMergesEntities( shader );
			}
			merge = VK_EntitiesMerge( shaderMerges, oldEntityNum, entityNum );
		}

		// change the tess parameters if needed. A "entityMergable" shader is a shader that can have surfaces
		// from separate entities merged into a single batch, like smoke and blood puff sprites
		if ( shader != oldShader || fogNum != oldFogNum || dlighted != oldDlighted
			|| ( entityNum != oldEntityNum && !shader->entityMergable && !merge ) ) {
			if ( oldShader != NULL ) {
				RB_EndSurface();
				// the shadow volumes come with the sort of banners: when they are through, darken the frame
				// where they left a mark in the stencil buffer, before anything transparent is drawn
				if ( !didShadowPass && shader->sort > SS_BANNER ) {
					RB_ShadowFinish();
					didShadowPass = qtrue;
				}
			}
			RB_BeginSurface( shader, fogNum );
			oldShader = shader;
			oldFogNum = fogNum;
			oldDlighted = dlighted;
		}

		// change the modelview matrix if needed
		if ( merge ) {
			// the surface is made with its entity, everything else the batch has already
			backEnd.currentEntity = &backEnd.refdef.entities[entityNum];
			oldEntityNum = entityNum;
			mergedEntity = qtrue;
		} else if ( entityNum != oldEntityNum || mergedEntity ) {
			mergedEntity = qfalse;
			depthRange = RB_SetEntity( entityNum, originalTime );
			if ( oldDepthRange != depthRange ) {
				RB_SetDepthRange( depthRange );
				oldDepthRange = depthRange;
			}
			oldEntityNum = entityNum;
		}

		// add the triangles for this surface
		VK_AddSurface( drawSurf->surface );
	}
	backEnd.refdef.floatTime = originalTime;

	// draw the contents of the last shader batch
	if ( oldShader != NULL ) {
		RB_EndSurface();
	}

	if ( tr_stencilled && tr_distortionPrePost ) {
		// ok, cap it now: without what is drawn after everything else
		RB_DistortionFill();
	}

	// what was put off to the end, the last first as rd-vanilla has it
	int lastPostEnt = -1;

	while ( g_numPostRenders > 0 ) {
		const postRender_t *post = &g_postRenders[--g_numPostRenders];

		RB_BeginSurface( post->shader, post->fogNum );
		depthRange = RB_SetEntity( post->entNum, originalTime );
		if ( ( backEnd.currentEntity->e.renderfx & RF_DISTORTION ) && lastPostEnt != post->entNum ) {
			// do the capture now, we only need to do it once per ent
			RB_CaptureForEntity( backEnd.currentEntity );
			lastPostEnt = post->entNum;
		}
		RB_SetDepthRange( post->depthRange );
		depthRange = post->depthRange;
		VK_AddSurface( post->drawSurf->surface );
		RB_EndSurface();
	}
	VK_SetRefraction( qfalse, 0, 0, 0 );
	backEnd.refdef.floatTime = originalTime;

	// go back to the world modelview matrix
	backEnd.currentEntity = &tr.worldEntity;
	backEnd.ori = backEnd.viewParms.world;
	VK_InvalidateTransform();
	if ( depthRange ) {
		RB_SetDepthRange( 0 );
	}
	if ( tr_stencilled && !tr_distortionPrePost ) {
		// draw in the stencil buffer's cutout
		RB_DistortionFill();
	}
	if ( !didShadowPass ) {
		// darken down any stencil shadows
		RB_ShadowFinish();
	}
}

static const void *RB_DrawSurfs( const void *data ) {
	const drawSurfsCommand_t *cmd = (const drawSurfsCommand_t *)data;

	VK_BeginFrame();
	// finish any 2D drawing if needed
	if ( tess.numIndexes ) {
		RB_EndSurface();
	}
	backEnd.refdef = cmd->refdef;
	backEnd.viewParms = cmd->viewParms;
	RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs );
	// the flares of this view want to know what the depth is where they are, before the next view clears it
	VK_CopyProbes();
	// the stages that glow once more, into a buffer that is blurred and added to the scene (vk_post.cpp)
	if ( !( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) && r_DynamicGlow->integer ) {
		VK_BeginGlow();
		g_bRenderGlowingObjects = true;
		RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs );
		g_bRenderGlowingObjects = false;
		VK_EndGlow();
	}
	// the next picture has to set the 2D projection up again
	tess.shader = NULL;
	return (const void *)( cmd + 1 );
}

/*
===============
RB_WorldEffects

Rain and snow over the view that was just drawn. The particles are moved and drawn by tr_WorldEffects.cpp of
rd-vanilla, its drawing goes through vk_gl.cpp.
===============
*/
void RB_RenderWorldEffects( void );

static const void *RB_WorldEffects( const void *data ) {
	const drawBufferCommand_t *cmd = (const drawBufferCommand_t *)data;

	VK_BeginFrame();
	// Always flush the tess buffer
	if ( tess.shader && tess.numIndexes ) {
		RB_EndSurface();
	}
	RB_RenderWorldEffects();
	if ( tess.shader ) {
		RB_BeginSurface( tess.shader, tess.fogNum );
	}
	return (const void *)( cmd + 1 );
}

static const void *RB_DrawBuffer( const void *data ) {
	const drawBufferCommand_t *cmd = (const drawBufferCommand_t *)data;

	VK_BeginFrame();
	if ( tr.world && tr.world->globalFog != -1 ) {
		// a level with fog all over starts every frame in the color of the fog: a sky without a box or clouds
		// leaves it standing
		const fog_t *fog = &tr.world->fogs[tr.world->globalFog];
		const float color[4] = { fog->parms.color[0], fog->parms.color[1], fog->parms.color[2], 1.0f };

		VK_ClearColor( 0, 0, glConfig.vidWidth, glConfig.vidHeight, color );
	} else if ( r_clear->integer ) {
		// clear screen for debugging: pink, or red, green, yellow, blue, cyan, magenta, white, black for 1 to 8
		static const float colors[9][4] = { { 1, 0, 0.5f, 1 }, { 1, 0, 0, 1 }, { 0, 1, 0, 1 }, { 1, 1, 0, 1 }, { 0, 0, 1, 1 },
			{ 0, 1, 1, 1 }, { 1, 0, 1, 1 }, { 1, 1, 1, 1 }, { 0, 0, 0, 1 } };
		int i = r_clear->integer;

		if ( i == 42 ) {
			i = Q_irand( 0, 8 );
		}
		VK_ClearColor( 0, 0, glConfig.vidWidth, glConfig.vidHeight, colors[( i >= 1 && i <= 8 ) ? i : 0] );
	}
	return (const void *)( cmd + 1 );
}

const void *R_DrawWireframeAutomap( const void *data );		// tr_world.cpp

/*
===============
VK_ShowImages

r_showImages: all the images on the screen, on top of whatever was there. 2: in proportional size.
rd-vanilla sizes the tiles by the width of the window but draws them in the 640 x 480 of the menus, so in a large
window most of them are outside. Here they are sized for the 640 x 480 and all can be seen.
===============
*/
static void VK_ShowImages( void ) {
	image_t *image;
	int i = 0;

	if ( !backEnd.projection2D ) {
		RB_SetGL2D();
	}
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	qglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	R_Images_StartIteration();
	while ( ( image = R_Images_GetNextIteration() ) != NULL ) {
		float w = SCREEN_WIDTH / 20.0f;
		float h = SCREEN_HEIGHT / 15.0f;
		const float x = i % 20 * w;
		const float y = i / 20 % 15 * h;

		if ( r_showImages->integer == 2 ) {
			w *= image->width / 512.0f;
			h *= image->height / 512.0f;
		}
		GL_Bind( image );
		qglBegin( GL_QUADS );
		qglTexCoord2f( 0.0f, 0.0f );
		qglVertex3f( x, y, 0.0f );
		qglTexCoord2f( 1.0f, 0.0f );
		qglVertex3f( x + w, y, 0.0f );
		qglTexCoord2f( 1.0f, 1.0f );
		qglVertex3f( x + w, y + h, 0.0f );
		qglTexCoord2f( 0.0f, 1.0f );
		qglVertex3f( x, y + h, 0.0f );
		qglEnd();
		i++;
	}
}

static const void *RB_SwapBuffers( const void *data ) {
	const swapBuffersCommand_t *cmd = (const swapBuffersCommand_t *)data;

	VK_BeginFrame();
	// finish any 2D drawing if needed
	if ( tess.numIndexes ) {
		RB_EndSurface();
	}
	if ( r_showImages->integer ) {
		VK_ShowImages();
	}
	VK_EndFrame();
	// recording to a file: now the frame has a picture
	VK_WriteVideoFrame();
	backEnd.projection2D = qfalse;
	tess.shader = NULL;
	return (const void *)( cmd + 1 );
}

/*
===============
RB_ExecuteRenderCommands
===============
*/
void RB_ExecuteRenderCommands( const void *data ) {
	const int start = ri.Milliseconds() * ri.Cvar_VariableValue( "timescale" );

	while ( 1 ) {
		data = PADP( data, sizeof( void * ) );

		switch ( *(const int *)data ) {
		case RC_SET_COLOR:
			data = RB_SetColor( data );
			break;
		case RC_STRETCH_PIC:
			data = RB_StretchPic( data );
			break;
		case RC_ROTATE_PIC:
			data = RB_RotatePic( data, qfalse );
			break;
		case RC_ROTATE_PIC2:
			data = RB_RotatePic( data, qtrue );
			break;
		case RC_DRAW_SURFS:
			data = RB_DrawSurfs( data );
			break;
		case RC_DRAW_BUFFER:
			data = RB_DrawBuffer( data );
			break;
		case RC_SWAP_BUFFERS:
			data = RB_SwapBuffers( data );
			break;
		case RC_VIDEOFRAME:
			data = RB_TakeVideoFrameCmd( data );
			break;
		case RC_WORLD_EFFECTS:
			data = RB_WorldEffects( data );
			break;
		case RC_AUTO_MAP:
			// the level as colored triangles or lines for the radar (r_autoMap). tr_world.cpp draws it with one
			// glBegin() per surface, over the scene the game has set up for it.
			VK_BeginFrame();
			if ( tess.numIndexes ) {
				RB_EndSurface();
			}
			VK_GL_Collect( qtrue );
			data = R_DrawWireframeAutomap( data );
			VK_GL_Collect( qfalse );
			break;
		case RC_END_OF_LIST:
		default:
			// stop rendering
			backEnd.pc.msec = ri.Milliseconds() * ri.Cvar_VariableValue( "timescale" ) - start;
			return;
		}
	}
}

/*
===============
RE_UploadCinematic

A frame of a video for the scratch image of the client
===============
*/
void RE_UploadCinematic( int cols, int rows, const byte *data, int client, qboolean dirty ) {
	image_t *image = tr.scratchImage[client];

	if ( !image ) {
		return;
	}
	// if the scratchImage isn't in the format we want, specify it as a new texture
	if ( cols != image->width || rows != image->height ) {
		VK_UpdateImage( image, cols, rows, data );
		GL_Bind( image );
		VK_ImageParm( GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		VK_ImageParm( GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		VK_ImageParm( GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		VK_ImageParm( GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	} else if ( dirty ) {
		// otherwise, just update it
		VK_UpdateImage( image, cols, rows, data );
	}
}

/*
===============
RE_StretchRaw

Stretches a raw 32 bit power of 2 bitmap image over the given screen rectangle. Used for cinematics.
===============
*/
void RE_StretchRaw( int x, int y, int w, int h, int cols, int rows, const byte *data, int client, qboolean dirty ) {
	vkDraw_t draw;

	if ( !tr.registered ) {
		return;
	}
	R_IssuePendingRenderCommands();
	if ( tess.numIndexes ) {
		RB_EndSurface();
	}
	// make sure rows and cols are powers of 2
	if ( ( cols & ( cols - 1 ) ) || ( rows & ( rows - 1 ) ) ) {
		Com_Error( ERR_DROP, "Draw_StretchRaw: size not a power of 2: %i by %i", cols, rows );
	}
	RE_UploadCinematic( cols, rows, data, client, dirty );

	VK_BeginFrame();
	RB_SetGL2D();
	tess.shader = NULL;
	tess.numVertexes = 4;
	tess.numIndexes = 6;
	tess.indexes[0] = 3;
	tess.indexes[1] = 0;
	tess.indexes[2] = 2;
	tess.indexes[3] = 2;
	tess.indexes[4] = 0;
	tess.indexes[5] = 1;
	VectorSet( tess.xyz[0], x, y, 0 );
	VectorSet( tess.xyz[1], x + w, y, 0 );
	VectorSet( tess.xyz[2], x + w, y + h, 0 );
	VectorSet( tess.xyz[3], x, y + h, 0 );
	VectorSet2( tess.texCoords[0][0], 0.5f / cols, 0.5f / rows );
	VectorSet2( tess.texCoords[1][0], ( cols - 0.5f ) / cols, 0.5f / rows );
	VectorSet2( tess.texCoords[2][0], ( cols - 0.5f ) / cols, ( rows - 0.5f ) / rows );
	VectorSet2( tess.texCoords[3][0], 0.5f / cols, ( rows - 0.5f ) / rows );

	// not VK_UploadTess(): that one is for a batch with a shader, and there is none here
	if ( VK_UploadGeometry( tess.xyz, NULL, tess.texCoords, tess.vertexColors, tess.numVertexes, tess.indexes, tess.numIndexes ) ) {
		memset( &draw, 0, sizeof( draw ) );
		backEnd.currentEntity = &backEnd.entity2D;
		VK_InvalidateTransform();
		draw.transform = VK_CurrentTransform();
		draw.tex0 = draw.tex1 = tr.scratchImage[client]->texnum;
		draw.flags = (uint32_t)TCGEN_TEXTURE << VKD_TCGEN0_SHIFT;
		draw.baseColor[0] = draw.baseColor[1] = draw.baseColor[2] = tr.identityLight;
		draw.baseColor[3] = 1.0f;
		draw.texMat0[0] = draw.texMat0[5] = 1.0f;
		VK_DrawTess( &draw, VKP_GENERIC, GLS_DEPTHTEST_DISABLE, VK_CULL_MODE_NONE, qfalse );
	}
	tess.numIndexes = 0;
	tess.numVertexes = 0;
}
