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

// the part of the depth buffer an entity is drawn into: all of it, the front for the view weapon, none
static void RB_SetDepthRange( int depthRange ) {
	const viewParms_t *view = &backEnd.viewParms;

	VK_SetViewport( view->viewportX, view->viewportY, view->viewportWidth, view->viewportHeight, 0.0f,
		depthRange == 1 ? 0.3f : ( depthRange == 2 ? 0.0f : 1.0f ) );
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

		// change the tess parameters if needed. A "entityMergable" shader is a shader that can have surfaces
		// from separate entities merged into a single batch, like smoke and blood puff sprites
		if ( shader != oldShader || fogNum != oldFogNum || dlighted != oldDlighted
			|| ( entityNum != oldEntityNum && !shader->entityMergable ) ) {
			if ( oldShader != NULL ) {
				RB_EndSurface();
			}
			RB_BeginSurface( shader, fogNum );
			oldShader = shader;
			oldFogNum = fogNum;
			oldDlighted = dlighted;
		}

		// change the modelview matrix if needed
		if ( entityNum != oldEntityNum ) {
			depthRange = 0;
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

	// go back to the world modelview matrix
	backEnd.currentEntity = &tr.worldEntity;
	backEnd.ori = backEnd.viewParms.world;
	VK_InvalidateTransform();
	if ( depthRange ) {
		RB_SetDepthRange( 0 );
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
	// the next picture has to set the 2D projection up again
	tess.shader = NULL;
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

static const void *RB_SwapBuffers( const void *data ) {
	const swapBuffersCommand_t *cmd = (const swapBuffersCommand_t *)data;

	VK_BeginFrame();
	// finish any 2D drawing if needed
	if ( tess.numIndexes ) {
		RB_EndSurface();
	}
	VK_EndFrame();
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
		case RC_AUTO_MAP:
			// weather and the automap are not drawn by this renderer yet
			data = (const void *)( (const drawBufferCommand_t *)data + 1 );
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

	if ( VK_UploadTess() ) {
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
