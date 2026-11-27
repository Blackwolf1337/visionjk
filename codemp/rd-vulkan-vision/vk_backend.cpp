/*
===========================================================================
Copyright (C) 2026, OpenJK contributors
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

Pictures are placed on a screen of 640 x 480, whatever the size of the frame. On a wide window the screen is wider
than 640 by the bias on either side, see R_2DBias()
===============
*/
static void RB_SetGL2D( void ) {
	const float	bias = R_2DBias(), biasY = R_2DBiasY();
	const float	width = 640.0f + 2.0f * bias, height = 480.0f + 2.0f * biasY;
	float		ortho[16];

	backEnd.projection2D = qtrue;
	// On a plane in the level the 2D still lands at the front of the depth buffer, where the flat 2D is. With the
	// depth of the plane, the stages that test it (most shader scripts of the menus do, their text does not) were
	// cut out in the shape of what stands between the camera and the plane: the player.
	VK_SetViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight, 0.0f, backEnd.plane2D ? 0.0f : 1.0f );

	// glOrtho( -bias, 640 + bias, 480 + biasY, -biasY, 0, 1 )
	memset( ortho, 0, sizeof( ortho ) );
	ortho[0] = 2.0f / width;
	ortho[5] = -2.0f / height;
	ortho[10] = -2.0f;
	ortho[12] = 2.0f * bias / width - 1.0f;
	ortho[13] = 1.0f - 2.0f * biasY / height;
	ortho[14] = -1.0f;
	ortho[15] = 1.0f;
	// or on a plane in the level, see RE_Set2DPlane()
	VK_SetProjection( backEnd.plane2D ? backEnd.planeMatrix : ortho );

	memset( &backEnd.ori, 0, sizeof( backEnd.ori ) );
	backEnd.ori.axis[0][0] = backEnd.ori.axis[1][1] = backEnd.ori.axis[2][2] = 1.0f;
	backEnd.ori.modelMatrix[0] = backEnd.ori.modelMatrix[5] = backEnd.ori.modelMatrix[10] = backEnd.ori.modelMatrix[15] = 1.0f;

	// set time for 2D shaders
	backEnd.refdef.time = ri.Milliseconds() * ri.Cvar_VariableValue( "timescale" );
	backEnd.refdef.floatTime = backEnd.refdef.time * 0.001f;
}

// VisionJK: another sun was set (tr_material.cpp): the traced shadows follow it
void VK_SunChanged( void ) {
#ifndef RD_VISION_GLES
	VK_RT_SunChanged();
#endif
}

/*
===============
VK_InitShared

What every start of the renderer begins with, whatever the device. qtrue: the window of the last start is still
there and with it everything on the device (a map change), so the start is over.
===============
*/
qboolean VK_InitShared( void ) {
	v_vkStatic = ri.Cvar_Get( "v_vkStatic", "1", CVAR_CHEAT, "Level and models from buffers on the device. 0: all vertices are built on the CPU every frame" );
	v_vkMerge = ri.Cvar_Get( "v_vkMerge", "1", CVAR_CHEAT, "Sprites of several entities in one batch where the picture stays the same. 0: a batch ends with its entity" );
	v_vkSprites = ri.Cvar_Get( "v_vkSprites", "1", CVAR_CHEAT, "Sprites, the glow of blades, lines and bolts made in the vertex shader. 0: their corners are built on the CPU every frame" );
	v_vkGrass = ri.Cvar_Get( "v_vkGrass", "1", CVAR_CHEAT | CVAR_LATCH, "Surface sprites of the level placed once and drawn from the device. 0: placed on the CPU every frame, and the surfaces under them too" );
	v_dlightScale = ri.Cvar_Get( "v_dlightScale", "1", CVAR_ARCHIVE_ND, "How bright the moving lights (sabers, shots, explosions) are on the surfaces they light, 1 as meant" );
	v_pbrEnergy = ri.Cvar_Get( "v_pbrEnergy", "1", CVAR_ARCHIVE_ND, "Surfaces with a material lose of their diffuse light what their shine takes, as light does (energy conserving). 0: shine on top of the full diffuse light, as before" );
	v_tessellation = ri.Cvar_Get( "v_tessellation", "1", CVAR_ARCHIVE_ND, "Materials with tessellation get real bumps: their triangles cut finer and moved on the graphics card (Vulkan, a card with tessellation shaders). 0: they keep their parallax instead" );
	v_pbrProbes = ri.Cvar_Get( "v_pbrProbes", "1", CVAR_ARCHIVE_ND, "Surfaces with a material mirror the level around them: it is drawn from up to 32 points in all directions while it loads. 0: they mirror the light of their lightmap" );
	v_depthPrepass = ri.Cvar_Get( "v_depthPrepass", "1", CVAR_ARCHIVE_ND, "The solid surfaces of the level go into the depth buffer first, so each pixel of them is shaded once (materials cost less); only while v_pbr is on, where it pays. 0: drawn as before" );
	v_parallax = ri.Cvar_Get( "v_parallax", "1", CVAR_ARCHIVE_ND, "Surfaces with a height in their normal map look carved (parallax occlusion, up to 32 reads of the texture a pixel). 0: flat, faster" );
	v_pbr = ri.Cvar_Get( "v_pbr", "1", CVAR_ARCHIVE_ND, "Surfaces with a material (material keywords of shaders, v_matdebug) lit with their normal map, roughness and metalness. 0: drawn as without it. Views to check them: 2 the light alone as without a material, 3 the shine alone, 4 the normals, 5 the direction of the light of the grid, 6 roughness, metalness and occlusion, 7 which maps a surface has, 8 the surfaces as mirrors of the reflection probes, 9 the albedo checked against real surfaces (blue too dark, red too bright, magenta a dark metal), 10 roughness alone, 11 metalness alone, 12 the mirror of the light around alone" );
	v_lodPixels = ri.Cvar_Get( "v_lodPixels", "1", CVAR_ARCHIVE_ND, "Models keep their detail by how many pixels they cover, as at 640 x 480 (R_LodScale). 0: by the share of the screen they cover, as before" );

	// the builtin images are made again, the copy of the frame has to be put into the texture table again
	vk.screenSlot = -1;
	// the buffers of the level and of the models are filled again when they are first drawn
	VK_WorldChanged();
	VK_ModelsChanged();
#ifndef RD_VISION_GLES
	VK_RT_ModelsChanged();
#endif
	VK_ClearFlares();
	if ( glConfig.vidWidth ) {
		// the window stays, the textures get their filter again as GL_SetDefaultState() of rd-vanilla gives it to them
		GL_TextureMode( r_textureMode->string );
		return qtrue;
	}
	return qfalse;
}

/*
===============
VK_InitConfig

What the engine, cgame and ui want to know about the renderer and what is the same for every device. The device
has said who it is, how large a texture may be and how much anisotropic filter there is, and has set vk.bcTextures.
===============
*/
void VK_InitConfig( void ) {
	glConfig.extensions_string = "";
	glConfig.maxActiveTextures = NUM_TEXTURE_BUNDLES;
	glConfig.colorBits = 32;
	glConfig.depthBits = 24;
	glConfig.stencilBits = 8;
	// tr_image.cpp then asks for the formats of GL_EXT_texture_compression_s3tc, see VK_ImageLevel()
	glConfig.textureCompression = vk.bcTextures ? TC_S3TC_DXT : TC_NONE;
	glConfig.textureEnvAddAvailable = qtrue;
	glConfig.clampToEdgeAvailable = qtrue;
	glConfig.stereoEnabled = qfalse;
	// gamma and overbright are one pass over the finished frame (shaders/gamma.frag), not a ramp of the display
	glConfig.deviceSupportsGamma = qfalse;

	// the setup menus offer the anisotropic filter only if this cvar says how much of it there is
	ri.Cvar_SetValue( "r_ext_texture_filter_anisotropic_avail", glConfig.maxTextureFilterAnisotropy );
	// The filter new textures are made with (r_textureMode) lives in tr_image.cpp and is what it was compiled as in a
	// library that was just loaded. rd-vanilla sets it in GL_SetDefaultState() at every start, RE_BeginFrame() only
	// when the cvar changes.
	GL_TextureMode( r_textureMode->string );
}

/*
===============
VK_Splash

The picture of the game on the window as soon as there is one, as R_Splash() of rd-vanilla puts it there: get
something on screen asap, the rest of the start takes a moment in which nothing else is drawn
===============
*/
void VK_Splash( void ) {
	const float	bias = R_2DBias();		// over the whole window
	image_t		*image = bias > 0.0f ? R_FindImageFile( "menu/splash_16_9", qfalse, qfalse, qfalse, GL_CLAMP ) : NULL;

	if ( !image ) {
		image = R_FindImageFile( "menu/splash", qfalse, qfalse, qfalse, GL_CLAMP );
	}

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
	VK_GL_Vertex( -bias, 0.0f, 0.0f );
	VK_GL_TexCoord( 1.0f, 0.0f );
	VK_GL_Vertex( 640.0f + bias, 0.0f, 0.0f );
	VK_GL_TexCoord( 0.0f, 1.0f );
	VK_GL_Vertex( -bias, 480.0f, 0.0f );
	VK_GL_TexCoord( 1.0f, 1.0f );
	VK_GL_Vertex( 640.0f + bias, 480.0f, 0.0f );
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

// the 2D goes onto a plane in the level from here on, or flat onto the screen again (RE_Set2DPlane)
static const void *RB_Set2DPlane( const void *data ) {
	const set2DPlaneCommand_t *cmd = (const set2DPlaneCommand_t *)data;

	// what waits to be drawn belongs to the projection that was
	if ( tess.numIndexes ) {
		RB_EndSurface();
	}
	backEnd.plane2D = cmd->active;
	if ( cmd->active ) {
		memcpy( backEnd.planeMatrix, cmd->matrix, sizeof( backEnd.planeMatrix ) );
	}
	if ( backEnd.projection2D ) {
		RB_SetGL2D();
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

// VisionJK: frosted glass behind a panel (RE_DrawBackdrop): the rect of the 2D in pixels of the frame, as the
// projection of RB_SetGL2D() puts it there, and the frame blurred inside it. Not on a 2D plane in the level.
static const void *RB_Backdrop( const void *data ) {
	const backdropCommand_t *cmd = (const backdropCommand_t *)data;
#ifndef RD_VISION_GLES
	const float bias = R_2DBias(), biasY = R_2DBiasY();
	const float sx = vk.color.width / ( 640.0f + 2.0f * bias ), sy = vk.color.height / ( 480.0f + 2.0f * biasY );

	VK_BeginFrame();
	if ( backEnd.plane2D ) {
		return (const void *)( cmd + 1 );
	}
	// what waits to be drawn is behind the panel
	if ( tess.numIndexes ) {
		RB_EndSurface();
	}
	VK_BackdropPass( (int)floorf( ( cmd->x + bias ) * sx ), (int)floorf( ( cmd->y + biasY ) * sy ),
		(int)ceilf( ( cmd->x + cmd->w + bias ) * sx ), (int)ceilf( ( cmd->y + cmd->h + biasY ) * sy ), cmd->radius * sx,
		Com_Clamp( 0.0f, 3.0f, v_backdropBlur->value ) );
	// the 2D goes on with its own state
	VK_BindFrameSets();
	vk.stateValid = qfalse;
	RB_SetGL2D();
#endif
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
RB_DrawTris2D

VisionJK: the triangles of RE_DrawTriangles2D, in pixels of the window and always flat on it, in batches of
what tess holds: the whole list when it fits, else triangle by triangle with their own corners. The 2D of the
640 x 480 screen is set up again by the next picture after them.
===============
*/
static void RB_Tris2DVertex( const polyVert_t *v ) {
	const int n = tess.numVertexes++;

	tess.xyz[n][0] = v->xyz[0];
	tess.xyz[n][1] = v->xyz[1];
	tess.xyz[n][2] = 0.0f;
	tess.texCoords[n][0][0] = v->st[0];
	tess.texCoords[n][0][1] = v->st[1];
	memcpy( tess.vertexColors[n], v->modulate, sizeof( tess.vertexColors[n] ) );
}

static const void *RB_DrawTris2D( const void *data ) {
	const drawTris2DCommand_t *cmd = (const drawTris2DCommand_t *)data;
	float ortho[16];

	if ( !cmd->shader->numUnfoggedPasses ) {
		return (const void *)( cmd + 1 );
	}
	VK_BeginFrame();
	if ( tess.numIndexes ) {
		RB_EndSurface();
	}
	VK_SetViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight, 0.0f, 1.0f );
	memset( ortho, 0, sizeof( ortho ) );
	ortho[0] = 2.0f / glConfig.vidWidth;
	ortho[5] = -2.0f / glConfig.vidHeight;
	ortho[10] = -2.0f;
	ortho[12] = -1.0f;
	ortho[13] = 1.0f;
	ortho[14] = -1.0f;
	ortho[15] = 1.0f;
	VK_SetProjection( ortho );
	memset( &backEnd.ori, 0, sizeof( backEnd.ori ) );
	backEnd.ori.axis[0][0] = backEnd.ori.axis[1][1] = backEnd.ori.axis[2][2] = 1.0f;
	backEnd.ori.modelMatrix[0] = backEnd.ori.modelMatrix[5] = backEnd.ori.modelMatrix[10] = backEnd.ori.modelMatrix[15] = 1.0f;
	backEnd.refdef.time = ri.Milliseconds() * ri.Cvar_VariableValue( "timescale" );
	backEnd.refdef.floatTime = backEnd.refdef.time * 0.001f;
	// the whole frame is there for the 2D (VK_FrameRoom)
	backEnd.projection2D = qtrue;
	if ( cmd->clipped ) {
		VK_Scissor( cmd->clip[0], cmd->clip[1], cmd->clip[2], cmd->clip[3] );
	}

	backEnd.currentEntity = &backEnd.entity2D;
	RB_BeginSurface( cmd->shader, 0 );
	if ( cmd->numVerts < SHADER_MAX_VERTEXES && cmd->numIndexes < SHADER_MAX_INDEXES ) {
		for ( int i = 0; i < cmd->numVerts; i++ ) {
			RB_Tris2DVertex( &cmd->verts[i] );
		}
		for ( int i = 0; i < cmd->numIndexes; i++ ) {
			const int index = cmd->indexes[i];

			tess.indexes[tess.numIndexes++] = ( index >= 0 && index < cmd->numVerts ) ? (glIndex_t)index : 0;
		}
	} else {
		for ( int i = 0; i + 2 < cmd->numIndexes; i += 3 ) {
			RB_CHECKOVERFLOW( 3, 3 );
			for ( int k = 0; k < 3; k++ ) {
				const int index = cmd->indexes[i + k];

				tess.indexes[tess.numIndexes++] = (glIndex_t)tess.numVertexes;
				RB_Tris2DVertex( &cmd->verts[( index >= 0 && index < cmd->numVerts ) ? index : 0] );
			}
		}
	}
	RB_EndSurface();

	// the scissor back to the viewport, and the next picture sets the 640 x 480 screen up again
	VK_SetViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight, 0.0f, 1.0f );
	backEnd.projection2D = qfalse;
	return (const void *)( cmd + 1 );
}

// RF_AFTERGLOW: the entities that are drawn after the glow has been added. The list of surfaces is run once more
// for them, without them the first time.
// (the first two are read by R_GhoulSurfaceDone())
bool			rb_glowView;		// this view adds a glow
bool			rb_lateEntities;	// this run of the list draws those entities and nothing else
static qboolean	rb_sawLate;			// the first run left some out
// a run of the list that draws into the picture and the depth of the run before it
#define RB_KEEPS_FRAME()	( g_bRenderGlowingObjects || rb_lateEntities )

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

	// in the glow pass, and in the run for the entities that come after it, the depth of the scene stays
	if ( !RB_KEEPS_FRAME() ) {
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
	} else if ( r_fastsky->integer && !( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) && !RB_KEEPS_FRAME() ) {
		const float color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

		VK_ClearColor( view->viewportX, view->viewportY, view->viewportWidth, view->viewportHeight, color );
	}
	if ( ( ( tr.refdef.rdflags & RDF_AUTOMAP )
		|| ( !( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) && r_DynamicGlow->integer && !RB_KEEPS_FRAME() ) )
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
rd-vanilla with its numbers.

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

// Entities that have to be drawn after everything else: the ones that fade (RF_FORCE_ENT_ALPHA), the ones that
// ask for it (RF_FORCEPOST) and the ones that bend the screen (RF_DISTORTION). A surface each: the outline of the
// pointer of VisionJK alone is six shells of every surface of a player, so rd-vanilla's 128 were full at times, and
// a distortion after them was drawn as its white picture (the shockwave of the boombox).
#define MAX_POST_RENDERS	1024

typedef struct postRender_s {
	int			fogNum;
	int			entNum;
	int			dlighted;
	int			depthRange;
	drawSurf_t	*drawSurf;
	shader_t	*shader;
} postRender_t;

static postRender_t	g_postRenders[MAX_POST_RENDERS];
// a view drawn in the middle of another one leaves the entries of that one in the list (RB_DrawDeferredViews)
static int			rb_postBase;
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
cvar_t	*v_vkMerge;		// 0: a batch ends with its entity, as in rd-vanilla
cvar_t	*v_lodPixels;	// 0: models choose their detail by the share of the screen, as in rd-vanilla (R_LodScale)

// the render flags that change how a batch is drawn
#define VK_RF_BATCH	( RF_DEPTHHACK | RF_NODEPTH | RF_VOLUMETRIC | RF_FORCE_ENT_ALPHA | RF_RGB_TINT | RF_DISTORTION \
	| RF_DISINTEGRATE1 | RF_DISINTEGRATE2 | RF_SETANIMINDEX | RF_ALPHA_DEPTH | RF_FORCEPOST | RF_REFRACT_REST )

static int VK_ShaderMergesEntities( const shader_t *shader ) {
	int result = 1;

	if ( shader->sky ) {
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
	// x counts from the right edge, y from the top. With the projection of the view: rd-vanilla had the guess of
	// R_WorldCoordToScreenCoord() (90 / fov for 1 / tan( fov / 2 )), which put the square beside its entity away from
	// the middle of a wide view, and a model that shows the screen behind it as it is (the shockwave of the
	// boombox, sound_wave.md3) showed it shifted.
	x = (int)( xcenter + xcenter / forward / tanf( DEG2RAD( backEnd.refdef.fov_x * 0.5f ) ) * left );
	y = (int)( ycenter - ycenter / forward / tanf( DEG2RAD( backEnd.refdef.fov_y * 0.5f ) ) * up );
	// The square stays round its entity also where it reaches past the edge of the screen. rd-vanilla moved it in
	// there, since it copied that square alone: the copy then lay beside the entity, and near the edge a model that
	// shows the screen behind it (the shockwave of the boombox) showed a shifted picture, as a lens. Here the whole
	// frame is copied, and what the square has beyond the edge is beyond the edge of the model on the screen too.
	cX = glConfig.vidWidth - x - rad / 2;
	cY = glConfig.vidHeight - y - rad / 2;

	VK_CaptureFrame();
	VK_SetRefraction( qtrue, cX, cY, rad );
}

// The scene is put out of focus when it is drawn (VK_BlurFrame): what stands over all of it (RGBA_SHADER_OVER,
// the panel behind the menu in the level) is left out of it then and drawn onto the soft frame, so it is as
// sharp as the menu on it. rb_sawOver: the list had such surfaces.
static qboolean	rb_overLater, rb_sawOver;

// The wet places of RE_AddWet are found by the depth of the scene, so they go onto the level before anything
// transparent is drawn, which leaves no depth: a cloaked or faded player and effects stay on top of the wet ground.
// rb_wetLater: this run of the list still has to draw them.
static qboolean	rb_wetLater;
// v_softParticles: the distance of the solid level is wanted there for this view (VK_SoftDepth)
static qboolean	rb_softLater;

// VisionJK: the moving lights of the solid surfaces wait for the shadows of the players (vk_rt.cpp): those multiply
// into the frame, which darkened the light of a saber as well where a player stood between the light of the level
// and the floor. With rb_deferDlights the first run of the list draws no light pass; RB_DrawWet runs the list again
// with rb_dlightsOnly after the shadows, for the light passes of the solid surfaces alone, before anything
// transparent. They then also read the shadows from the moving lights of this frame, not of the one before. (Fog
// over a surface lies under its light then: a limit.)
qboolean			rb_deferDlights;
qboolean			rb_dlightsOnly;
int					rb_viewSerial;		// counts the views that are drawn: what is made once a view knows by it that a new one began
int					rb_surfaceDlights;
static drawSurf_t	*rb_sweepSurfs;
static int			rb_sweepCount;
// A view with thick glass (RB_GlassLayers): what stands behind a pane of it is drawn before the glass, which shows it
// moved with everything else behind it: the saber blades (VK_SaberBlades, early) and what is transparent (rb_fxPass).
// What stands in front of the glass is drawn as everything else is; the glass reads a picture of what lies behind
// its plane there (R_GlassViews of tr_main.cpp).
static int			rb_glassPanes;		// how many panes of thick glass the view has (RB_GlassLayers)
// the saber blades behind thick glass are added before the surface of the list with this number (the first thick
// glass), so the glass shows them moved with the hand that holds them (VK_SaberBlades, early)
static qboolean		rb_bladePending;
static int			rb_bladeAt;
static qboolean		rb_layering;		// the run of the list of the view of the level, and the runs it starts
// the views of their own (behind glass, through portals) that wait for the view of the level they were made for
// (RB_DrawDeferredViews)
static const struct drawSurfsCommand_s	*rb_deferred[SUB_VIEWS];
static int			rb_numDeferred;
static qboolean		rb_nested;			// such a view is being drawn
static cvar_t		*v_glassDebug;

static void RB_RenderDrawSurfList( drawSurf_t *drawSurfs, int numDrawSurfs );

/*
===============
RB_DepthPrepass

VisionJK (v_depthPrepass): the solid surfaces of the level of the main view go into the depth buffer first, alone
(VKP_DEPTH: no fragment shader, no colour). Their first stage is then drawn with an equal test and no depth write,
so the costly shading of a material (generic.frag PbrStage) runs once a pixel and not for every surface the sort
by shader happens to draw before the nearest one (2.3 times a pixel on ffa3, v_gpuTimes). Only surfaces of the
world whose shader is solid: sort opaque, a first stage that writes depth, not blended, no alpha test, no polygon
offset, no deform, no sky, no glass and no tessellation (which moves the triangles on the device). A shader is
marked when it was drawn into the pre-pass, and only marked shaders test equal, so the two runs can not disagree.
===============
*/
cvar_t				*v_depthPrepass;
qboolean			rb_prepassWanted;	// set by RB_DrawSurfs: this run of the list is the view of the level
qboolean			rb_depthPrepass;
qboolean			rb_glassPrepass;	// RB_GlassPrepass: the depth of the glass is put back afterwards, its shaders are not marked as pre-passed
int					rb_glassRound;		// RB_DrawGlassRounds: 1 the thick glass behind other glass, 2 the nearest, 0 all
qboolean			rb_prepassOn;
static int			rb_prepassView;
static int			rb_prepassShaders[MAX_SHADERS];

static qboolean RB_PrepassShader( const shader_t *shader ) {
	if ( shader->sort != SS_OPAQUE || shader->sky || shader->polygonOffset || shader->numDeforms || shader == tr.distortionShader
		|| shader->numUnfoggedPasses < 1 ) {
		return qfalse;
	}
	const shaderStage_t *first = &shader->stages[0];
	const uint32_t bits = first->stateBits;
	const uint32_t blend = bits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );

	if ( !first->active || first->glowOnly || ( first->ss && first->ss->surfaceSpriteType ) || !( bits & GLS_DEPTHMASK_TRUE )
		|| ( bits & ( GLS_ATEST_BITS | GLS_DEPTHFUNC_EQUAL | GLS_DEPTHTEST_DISABLE ) )
		|| ( blend && blend != ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO ) ) ) {
		return qfalse;
	}
	for ( int i = 0; i < shader->numUnfoggedPasses; i++ ) {
		const stageMaterial_t *mat = shader->stages[i].mat;

		if ( shader->stages[i].active && mat && ( mat->tessHeight > 0.0f || mat->transmission > 0.0f ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

qboolean RB_ShaderPrepassed( const shader_t *shader ) {
	return (qboolean)( rb_prepassOn && rb_prepassShaders[shader->index] == rb_prepassView );
}

void RB_MarkPrepassed( const shader_t *shader ) {
	rb_prepassShaders[shader->index] = rb_prepassView;
}

static void RB_DepthPrepass( drawSurf_t *drawSurfs, int numDrawSurfs ) {
	shader_t	*shader, *oldShader = NULL;
	int			entityNum, fogNum, dlighted;

	if ( ++rb_prepassView == 0 ) {
		memset( rb_prepassShaders, 0, sizeof( rb_prepassShaders ) );
		rb_prepassView = 1;
	}
	backEnd.currentEntity = &tr.worldEntity;
	backEnd.ori = backEnd.viewParms.world;
	VK_InvalidateTransform();
	rb_depthPrepass = qtrue;
	for ( int i = 0; i < numDrawSurfs; i++ ) {
		R_DecomposeSort( drawSurfs[i].sort, &entityNum, &shader, &fogNum, &dlighted );
		// (what the shader is remapped to decides, as it is what gets drawn: a server that remaps a texture to a
		// shader without stages, a sky, makes the surface invisible, and its depth alone would leave it standing
		// there in black)
		if ( entityNum != REFENTITYNUM_WORLD || !RB_PrepassShader( shader->remappedShader ? shader->remappedShader : shader ) ) {
			continue;
		}
		if ( shader != oldShader ) {
			if ( oldShader ) {
				RB_EndSurface();
			}
			RB_BeginSurface( shader, fogNum );
			oldShader = shader;
		}
		VK_AddSurface( drawSurfs[i].surface );
	}
	if ( oldShader ) {
		RB_EndSurface();
	}
	rb_depthPrepass = qfalse;
	rb_prepassOn = qtrue;
	VK_InvalidateTransform();
}

// A surface of the level gets the moving lights it has in this view back from its draw surface before it is drawn
// (tess.dlightBits takes them from the surface): the frontend made the other views of the frame after this one,
// and the last of them left its own bits on the surface (drawSurf_t dlightBits). That took the saber light from
// pieces of a floor, along their edges (2026-11-08).
static void RB_AddDrawSurf( const drawSurf_t *drawSurf ) {
	switch ( *drawSurf->surface ) {
	case SF_FACE:
		( (srfSurfaceFace_t *)drawSurf->surface )->dlightBits = drawSurf->dlightBits;
		break;
	case SF_GRID:
		( (srfGridMesh_t *)drawSurf->surface )->dlightBits = drawSurf->dlightBits;
		break;
	case SF_TRIANGLES:
		( (srfTriangles_t *)drawSurf->surface )->dlightBits = drawSurf->dlightBits;
		break;
	default:
		// a model (R_AddModelDrawSurf): its surface functions know nothing of them; again after it, in case it began a
		// new batch on the way
		tess.dlightBits |= drawSurf->dlightBits;
		break;
	}
	// (the type first: a Ghoul2 surface may be freed by drawing it, R_GhoulSurfaceDone, which asks for its lights here:
	// tess.dlightBits are gone when the surface began a new batch on the way)
	const surfaceType_t type = *drawSurf->surface;

	rb_surfaceDlights = drawSurf->dlightBits | ( backEnd.currentEntity != &tr.worldEntity && backEnd.currentEntity->numLamps ? 1 : 0 );
	VK_AddSurface( drawSurf->surface );
	rb_surfaceDlights = 0;
	if ( type != SF_FACE && type != SF_GRID && type != SF_TRIANGLES ) {
		tess.dlightBits |= drawSurf->dlightBits;
	}
}

#ifndef RD_VISION_GLES
static void RB_DrawDeferredViews( void );

// the view being drawn is the view of the level itself (no mirror, portal, puddle or view of its own, no model of the HUD)
static qboolean RB_IsLevelView( void ) {
	return (qboolean)( !backEnd.viewParms.isPortal && !( backEnd.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) ) );
}
#endif

static void RB_DrawWet( int depthRange ) {
	if ( !rb_wetLater ) {
		return;
	}
	rb_wetLater = qfalse;
	VK_LevelFragments( qfalse );
	VK_GpuMark( "level" );
#ifndef RD_VISION_GLES
	if ( rb_nested ) {
		// a view of its own: the shadows again, against what the view of the level built
		VK_RT_SubPass();
	} else {
		VK_RT_Pass();
		VK_GpuMark( VK_RT_Rays() ? "rt shadows" : "shadows (maps)" );
		// the views behind the glass of the level, with these shadows, before the level does the rest over its solid
		// surfaces (what they make of the distance of the solid level and of the smoke it makes again then)
		if ( rb_layering && rb_numDeferred && RB_IsLevelView() ) {
			RB_DrawDeferredViews();
			VK_GpuMark( "glass views" );
		}
	}
#endif
	VK_DrawWet();
	VK_GpuMark( "wet and paint" );
	if ( rb_deferDlights ) {
		rb_deferDlights = qfalse;
		rb_dlightsOnly = qtrue;
		RB_RenderDrawSurfList( rb_sweepSurfs, rb_sweepCount );
		rb_dlightsOnly = qfalse;
		VK_GpuMark( "moving lights" );
	}
	// v_softParticles: the distance of the solid level for the sprites that come now
#ifndef RD_VISION_GLES
	if ( rb_softLater ) {
		VK_SoftDepth();
		VK_GpuMark( "soft particles" );
	}
	// v_gpuSparks: the sparks the device moved, over the solid level, in the frame of the level
	{
		trRefEntity_t *entity = backEnd.currentEntity;
		const orientationr_t ori = backEnd.ori;

		backEnd.currentEntity = &tr.worldEntity;
		backEnd.ori = backEnd.viewParms.world;
		// (the matrices kept are those of the entity drawn last: in third person with a moving light, the own body of
		// the light passes above, and the sparks went where its model space put them)
		VK_InvalidateTransform();
		VK_SparksDraw( VK_CurrentTransform() );
		backEnd.currentEntity = entity;
		backEnd.ori = ori;
		VK_InvalidateTransform();
	}
	VK_GpuMark( "sparks" );
#endif
	// it leaves the viewport with the whole depth
	if ( depthRange ) {
		RB_SetDepthRange( depthRange );
	}
}

/*
===============
RB_RenderDrawSurfList
===============
*/
#ifndef RD_VISION_GLES
// VisionJK: how often the frame is copied for glass in a view (VK_GlassDraw), the copy before each glass shader
static cvar_t *v_glassCaptures;

static void RB_GlassCvars( void ) {
	if ( v_glassCaptures ) {
		return;
	}
	v_glassCaptures = ri.Cvar_Get( "v_glassCaptures", "4", CVAR_ARCHIVE_ND, "How often the frame is copied in a view for glass that bends what is behind it: before each glass shader, so glass behind glass shows. 0: glass is drawn as its shader text says" );
	v_glassDebug = ri.Cvar_Get( "v_glassDebug", "0", 0,
		"How thick glass gets what it shows. 1: in colours, blue the glass, red what stands in front of it, where the glass "
		"reads the view of what lies behind its plane. 2: without those views, to compare. 3: the planes of the glass and "
		"which got a view, printed once a second. 4: the glass shows that view alone in its part. 6: the distance of the "
		"solid level in bands of 32 units, as the passes at the end of the view have it" );
}

/*
Whether thick glass of the level gets views of what lies behind it (R_GlassViews of tr_main.cpp, which asks before it
makes one): where glass is drawn as glass, and the device kept their pictures.
*/
int VK_GlassDebug( void ) {
	RB_GlassCvars();
	return v_glassDebug->integer;
}

qboolean VK_GlassViews( void ) {
	RB_GlassCvars();
	return (qboolean)( v_pbr->integer && v_glassCaptures->integer > 0 && v_glassDebug->integer != 2 && vk.depthReadable
		&& vk.behind[0].image );
}

int VK_PortalPictures( void ) {
#ifdef RD_VISION_GLES
	return 0;
#else
	int count = 0;

	if ( !vk.depthReadable || !vk.sub.image ) {
		return 0;
	}
	while ( count < PORTAL_VIEWS && vk.behind[GLASS_VIEWS + count].image ) {
		count++;
	}
	return count;
#endif
}

int VK_PortalDepth( void ) {
	return v_portalDepth ? Com_Clampi( 1, PORTAL_DEPTH, v_portalDepth->integer ) : 1;
}

int VK_PortalDebug( void ) {
	return v_portalDebug ? v_portalDebug->integer : 0;
}

// a stage of the shader (or of what it is remapped to) is glass
static qboolean RB_ShaderHasGlass( const shader_t *shader ) {
	const shader_t *state = shader->remappedShader ? shader->remappedShader : shader;

	for ( int i = 0; i < state->numUnfoggedPasses; i++ ) {
		if ( state->stages[i].active && state->stages[i].mat && state->stages[i].mat->transmission > 0.0f ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
The glass that begins at drawSurfs[0] and goes on as far as the list has glass (R_SortGlass puts it together): its depth
alone, so the picture of what the eye sees of each pane can be taken (VK_GlassMask) before any of them is drawn. Of the
level only: glass on a model gets no such picture and is drawn as before. The glass itself is drawn after it at the
depth that is there.
*/
static void RB_GlassPrepass( drawSurf_t *drawSurfs, int numDrawSurfs ) {
	shader_t	*shader, *oldShader = NULL;
	int			entityNum, fogNum, dlighted;

	backEnd.currentEntity = &tr.worldEntity;
	backEnd.ori = backEnd.viewParms.world;
	VK_InvalidateTransform();
	rb_depthPrepass = qtrue;
	rb_glassPrepass = qtrue;
	for ( int i = 0; i < numDrawSurfs; i++ ) {
		R_DecomposeSort( drawSurfs[i].sort, &entityNum, &shader, &fogNum, &dlighted );
		if ( !RB_ShaderHasGlass( shader ) ) {
			break;
		}
		if ( entityNum != REFENTITYNUM_WORLD ) {
			continue;
		}
		if ( shader != oldShader ) {
			if ( oldShader ) {
				RB_EndSurface();
			}
			RB_BeginSurface( shader, fogNum );
			oldShader = shader;
		}
		VK_AddSurface( drawSurfs[i].surface );
	}
	if ( oldShader ) {
		RB_EndSurface();
	}
	rb_depthPrepass = qfalse;
	rb_glassPrepass = qfalse;
	VK_InvalidateTransform();
}

/*
===============
RB_GlassLayers

Glass shows what lies behind it from a copy of the frame, read to the side (generic.frag GlassStage), and it writes
its depth. So what stands behind a pane of thick glass and is drawn after the solid level has to come before the
glass: the saber blades and what is transparent. This finds the panes of the view and what of those lies behind one.
(The models in front of glass were held back until after it here for a day, 2026-11-26: they lost their shadows and
the smoke in front of them. The glass now reads a picture of what lies behind its plane where one stands in front
of it, R_GlassViews of tr_main.cpp.)
===============
*/
#define GLASS_LAYER_PANES	64

typedef struct {
	vec3_t		mins, maxs;
	qboolean	flat;
	vec3_t		normal;
	float		dist;
	int			index;		// of its surface in the list: the glass is drawn far to near
} glassPane_t;

static qboolean RB_InPane( const glassPane_t *pane, const vec3_t point, float margin ) {
	for ( int k = 0; k < 3; k++ ) {
		if ( point[k] < pane->mins[k] - margin || point[k] > pane->maxs[k] + margin ) {
			return qfalse;
		}
	}
	return qtrue;
}

// the panes of thick glass of the view that is drawn (RB_GlassLayers, rb_glassPanes of them)
static glassPane_t	rb_panes[GLASS_LAYER_PANES];

/*
What is transparent and no glass (the sprites of shots, sparks, smoke, flashes, marks) is drawn after the thick glass,
which writes its depth: so of it what lay behind a pane was not drawn at all (their word 2026-11-27: "effects behind
glass don't show"). As with the models and the blades, what lies behind a pane belongs before the glass, which then
shows it, moved as everything behind it; what lies in front or beside comes after it as before. The list is run twice
from the first thick glass on: rb_fxPass 1, right before that glass, draws what lies behind a pane and nothing else;
the rest of the main run, rb_fxPass 2, leaves that out. A chain of small entities (the bits of an effect) is in both,
each of its bits in the run it belongs to (RB_FxWanted).
*/
int					rb_fxPass;
static qboolean		rb_fxPending;
static int			rb_fxAt;			// before the surface of the list with this number: the first thick glass

// a flat pane of thick glass lies between the eye and the point
static qboolean RB_BehindGlass( const vec3_t point ) {
	const float	*eye = backEnd.viewParms.ori.origin;
	vec3_t		to;

	VectorSubtract( point, eye, to );
	for ( int k = 0; k < rb_glassPanes; k++ ) {
		const glassPane_t	*pane = &rb_panes[k];
		vec3_t				at;

		if ( !pane->flat ) {
			continue;
		}
		const float se = DotProduct( eye, pane->normal ) - pane->dist;
		const float sm = DotProduct( point, pane->normal ) - pane->dist;

		if ( fabsf( se ) < 1.0f || ( se > 0.0f ) == ( sm > 0.0f ) ) {
			continue;
		}
		VectorMA( eye, se / ( se - sm ), to, at );
		if ( RB_InPane( pane, at, 4.0f ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

// a bit of an effect at origin is drawn by the run of the list that is on
qboolean RB_FxWanted( const vec3_t origin ) {
	return (qboolean)( rb_fxPass == 0 || RB_BehindGlass( origin ) == (qboolean)( rb_fxPass == 1 ) );
}

// a transparent surface of the list: 0 it does not lie behind thick glass, 1 it does, 2 a chain of small entities,
// which may hold some of each
static int RB_SurfBehindGlass( const drawSurf_t *drawSurf, int entityNum ) {
	vec3_t center;

	if ( entityNum != REFENTITYNUM_WORLD ) {
		const trRefEntity_t *ent = &backEnd.refdef.entities[entityNum];

		// (what the list puts off to its end and the view weapon stay where they are)
		if ( ent->e.renderfx & ( RF_DISTORTION | RF_FORCEPOST | RF_FORCE_ENT_ALPHA | RF_DEPTHHACK | RF_NODEPTH ) ) {
			return 0;
		}
		if ( ent->e.reType == RT_ENT_CHAIN ) {
			return 2;
		}
		return RB_BehindGlass( ent->e.origin ) ? 1 : 0;
	}
	switch ( *drawSurf->surface ) {
	case SF_POLY: {
		const srfPoly_t *poly = (const srfPoly_t *)drawSurf->surface;

		return ( poly->numVerts > 0 && RB_BehindGlass( poly->verts[0].xyz ) ) ? 1 : 0;
	}
	case SF_FACE: {
		const srfSurfaceFace_t *face = (const srfSurfaceFace_t *)drawSurf->surface;

		if ( face->numPoints < 1 ) {
			return 0;
		}
		VectorClear( center );
		for ( int i = 0; i < face->numPoints; i++ ) {
			VectorAdd( center, face->points[i], center );
		}
		VectorScale( center, 1.0f / face->numPoints, center );
		break;
	}
	case SF_GRID: {
		const srfGridMesh_t *grid = (const srfGridMesh_t *)drawSurf->surface;

		VectorAdd( grid->meshBounds[0], grid->meshBounds[1], center );
		VectorScale( center, 0.5f, center );
		break;
	}
	case SF_TRIANGLES: {
		const srfTriangles_t *tris = (const srfTriangles_t *)drawSurf->surface;

		VectorAdd( tris->bounds[0], tris->bounds[1], center );
		VectorScale( center, 0.5f, center );
		break;
	}
	default:
		return 0;
	}
	return RB_BehindGlass( center ) ? 1 : 0;
}

static void RB_GlassLayers( drawSurf_t *drawSurfs, int numDrawSurfs ) {
	glassPane_t			*panes = rb_panes;
	const float			*eye = backEnd.viewParms.ori.origin;
	int					numPanes = 0;

	rb_bladePending = qfalse;
	rb_fxPending = qfalse;
	VK_SetEarlyBlades( NULL, 0 );
	RB_GlassCvars();
	if ( !v_pbr->integer || v_glassCaptures->integer <= 0 ) {
		return;
	}
	// the thick glass of the level: the list has what is transparent at its end
	for ( int i = numDrawSurfs - 1; i >= 0 && numPanes < GLASS_LAYER_PANES; i-- ) {
		shader_t		*shader;
		int				entityNum, fogNum, dlighted;
		glassPane_t		*pane = &panes[numPanes];

		R_DecomposeSort( drawSurfs[i].sort, &entityNum, &shader, &fogNum, &dlighted );
		if ( shader->sort <= SS_OPAQUE ) {
			break;
		}
		if ( entityNum != REFENTITYNUM_WORLD || !RB_ShaderHasGlass( shader ) || R_IsThinGlass( shader ) ) {
			continue;
		}
		pane->flat = qfalse;
		pane->index = i;
		switch ( *drawSurfs[i].surface ) {
		case SF_FACE: {
			const srfSurfaceFace_t *face = (const srfSurfaceFace_t *)drawSurfs[i].surface;

			if ( face->numPoints < 3 ) {
				continue;
			}
			ClearBounds( pane->mins, pane->maxs );
			for ( int k = 0; k < face->numPoints; k++ ) {
				AddPointToBounds( face->points[k], pane->mins, pane->maxs );
			}
			pane->flat = qtrue;
			VectorCopy( face->plane.normal, pane->normal );
			pane->dist = face->plane.dist;
			break;
		}
		case SF_GRID: {
			const srfGridMesh_t *grid = (const srfGridMesh_t *)drawSurfs[i].surface;

			VectorCopy( grid->meshBounds[0], pane->mins );
			VectorCopy( grid->meshBounds[1], pane->maxs );
			break;
		}
		case SF_TRIANGLES: {
			const srfTriangles_t *tris = (const srfTriangles_t *)drawSurfs[i].surface;

			VectorCopy( tris->bounds[0], pane->mins );
			VectorCopy( tris->bounds[1], pane->maxs );
			break;
		}
		default:
			continue;
		}
		numPanes++;
	}
	rb_glassPanes = numPanes;
	if ( !numPanes ) {
		return;
	}
	// The saber blades are light added onto the view at its end, in their true place. One that stands behind a pane
	// stood beside the hand that holds it then, which the pane shows moved (test level vjk_glass, 2026-11-27). Those
	// are added before the glass.
	{
		byte		early[MAX_SABER_BLADES];
		const int	count = Q_min( backEnd.refdef.numSaberBlades, MAX_SABER_BLADES );
		int			start = 0x7fffffff;
		qboolean	any = qfalse;

		memset( early, 0, sizeof( early ) );
		for ( int k = 0; k < numPanes; k++ ) {
			start = Q_min( start, panes[k].index );
		}
		for ( int i = 0; i < count; i++ ) {
			const saberBlade_t	*blade = &backEnd.refdef.saberBlades[i];
			vec3_t				mid, to;

			if ( blade->renderfx & RF_THIRD_PERSON ) {
				continue;
			}
			VectorAdd( blade->start, blade->end, mid );
			VectorScale( mid, 0.5f, mid );
			VectorSubtract( mid, eye, to );
			for ( int k = 0; k < numPanes; k++ ) {
				const glassPane_t	*pane = &panes[k];
				vec3_t				at;

				if ( !pane->flat ) {
					continue;
				}
				const float se = DotProduct( eye, pane->normal ) - pane->dist;
				const float sm = DotProduct( mid, pane->normal ) - pane->dist;

				if ( fabsf( se ) < 1.0f || ( se > 0.0f ) == ( sm > 0.0f ) ) {
					continue;
				}
				VectorMA( eye, se / ( se - sm ), to, at );
				if ( !RB_InPane( pane, at, 8.0f ) ) {
					continue;
				}
				early[i] = 1;
				any = qtrue;
			}
		}
		// (and what is transparent and lies behind a pane, RB_BehindGlass)
		rb_fxPending = qtrue;
		rb_fxAt = start;
		if ( any ) {
			VK_SetEarlyBlades( early, count );
			rb_bladePending = qtrue;
			rb_bladeAt = start;
		}
	}
}

#endif

#ifndef RD_VISION_GLES
// The saber blades that stand behind the plane of a view of what lies behind glass (R_GlassViews), onto it, held against
// its depth: the pass of the blades behind thick glass (VK_SaberBlades, early). The view of the level sorts its own out
// after this (RB_GlassLayers).
static void RB_GlassViewBlades( void ) {
	byte			early[MAX_SABER_BLADES];
	const int		count = Q_min( backEnd.refdef.numSaberBlades, MAX_SABER_BLADES );
	const cplane_t	*plane = &backEnd.viewParms.portalPlane;
	qboolean		any = qfalse;

	memset( early, 0, sizeof( early ) );
	for ( int i = 0; i < count; i++ ) {
		const saberBlade_t *blade = &backEnd.refdef.saberBlades[i];

		if ( !( blade->renderfx & RF_THIRD_PERSON ) && DotProduct( blade->start, plane->normal ) > plane->dist
			&& DotProduct( blade->end, plane->normal ) > plane->dist ) {
			early[i] = 1;
			any = qtrue;
		}
	}
	if ( any ) {
		VK_SetEarlyBlades( early, count );
		VK_SaberBlades( qfalse, qfalse, qtrue );
		VK_SetEarlyBlades( NULL, 0 );
	}
}
#endif

#ifndef RD_VISION_GLES
/*
===============
RB_DrawDeferredViews

The views of their own, of what lies behind glass (R_GlassViews) and through the portals of the portal gun
(R_PortalViews), come before the view of the level they were made for, as the front end makes them. They are kept
until that view has made the shadows of the frame (its phase 3), and drawn then, into targets of their own
(VK_BeginSubView): so they can have what the view of the level has. Everything of the view of the level that drawing
another view changes is kept here and given back after it.

The views through portals first, in the order they came: a view through a portal seen in another one comes before
that one (R_PortalViews makes it inside it), so the openings a view draws find the pictures of their views drawn. Then
those behind glass, which have the eye of the view of the level and show its openings with the same pictures it does.
===============
*/
static const void *RB_DrawSurfs( const void *data );

static void RB_DrawDeferredViews( void ) {
	static vkGlobals_t	globals;
	byte				early[MAX_SABER_BLADES];

	if ( !rb_numDeferred || rb_nested ) {
		return;
	}
	if ( tess.numIndexes ) {
		RB_EndSurface();
	}
	const backEndState_t	saved = backEnd;
	const qboolean			overLater = rb_overLater, sawOver = rb_sawOver, glowView = (qboolean)rb_glowView, sawLate = rb_sawLate;
	const qboolean			wetLater = rb_wetLater, softLater = rb_softLater, deferDlights = rb_deferDlights;
	drawSurf_t				*sweepSurfs = rb_sweepSurfs;
	const int				sweepCount = rb_sweepCount;
	const qboolean			prepassOn = rb_prepassOn, prepassWanted = rb_prepassWanted, layering = rb_layering;
	const int				prepassView = rb_prepassView, glassPanes = rb_glassPanes, fxPass = rb_fxPass, fxAt = rb_fxAt, bladeAt = rb_bladeAt;
	const qboolean			fxPending = rb_fxPending, bladePending = rb_bladePending;
	const qboolean			softReady = vk.softReady, frontReady = vk.frontReady, solidReady = vk.solidReady;
	const qboolean			glassReady = vk.glassReady, worldReady = vk.worldReady, glassReadyDraw = vk_glassReady;
	const bool				stencilled = tr_stencilled;
	const qboolean			distortionPrePost = tr_distortionPrePost;
	const int				postBase = rb_postBase;
	static glassPane_t		panes[GLASS_LAYER_PANES];

	VK_Checkpoint( "the views of their own begin" );
	memcpy( panes, rb_panes, sizeof( panes ) );
	VK_GetEarlyBlades( early );
	memcpy( &globals, VK_GlobalsEdit(), sizeof( globals ) );
	rb_postBase = g_numPostRenders;
	rb_nested = qtrue;

	for ( int pass = 0; pass < 2; pass++ ) {
		for ( int i = 0; i < rb_numDeferred; i++ ) {
			if ( ( rb_deferred[i]->viewParms.portalView != qfalse ) == ( pass == 0 ) ) {
				RB_DrawSurfs( rb_deferred[i] );
			}
		}
	}

	rb_nested = qfalse;
	rb_numDeferred = 0;
	rb_postBase = postBase;
	VK_Checkpoint( "the views of their own are done" );
	backEnd = saved;
	// (what is made once a view knows it by this number, the list of the moving lights with the part of the picture
	// each lights, vk_shade.cpp VK_LightList: the views drawn here counted it on, and the rest of this view took the
	// list of the last of them. Through a portal the eye is another one, and the lights of the level showed as squares
	// where that eye had them, their picture 2026-11-27.)
	rb_viewSerial++;
	rb_overLater = overLater;
	rb_sawOver = sawOver;
	rb_glowView = glowView;
	rb_sawLate = sawLate;
	rb_wetLater = wetLater;
	rb_softLater = softLater;
	rb_deferDlights = deferDlights;
	rb_sweepSurfs = sweepSurfs;
	rb_sweepCount = sweepCount;
	rb_prepassOn = prepassOn;
	rb_prepassWanted = prepassWanted;
	rb_layering = layering;
	rb_prepassView = prepassView;
	rb_glassPanes = glassPanes;
	rb_fxPass = fxPass;
	rb_fxAt = fxAt;
	rb_bladeAt = bladeAt;
	rb_fxPending = fxPending;
	rb_bladePending = bladePending;
	vk.softReady = softReady;
	vk.frontReady = frontReady;
	vk.solidReady = solidReady;
	vk.glassReady = glassReady;
	vk.worldReady = worldReady;
	vk_glassReady = glassReadyDraw;
	tr_stencilled = stencilled;
	tr_distortionPrePost = distortionPrePost;
	memcpy( rb_panes, panes, sizeof( panes ) );
	VK_SetEarlyBlades( early, MAX_SABER_BLADES );
	// the numbers of the frame as the view of the level had them, with the views behind its glass now drawn
	memcpy( VK_GlobalsEdit(), &globals, sizeof( globals ) );
	VK_GlassRows();
	// and its view
	tess.shader = NULL;
	VK_SetProjection( backEnd.viewParms.projectionMatrix );
	VK_SetViewport( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY, backEnd.viewParms.viewportWidth,
		backEnd.viewParms.viewportHeight, 0.0f, 1.0f );
	backEnd.currentEntity = &tr.worldEntity;
	backEnd.ori = backEnd.viewParms.world;
	VK_InvalidateTransform();
}
#endif

/*
===============
RB_RenderDrawSurfList

A view draws its list in a fixed order of phases (2026-11-27; before, the passes were started by the run of the list
when it came to certain surfaces, and parts of it were run again under global switches):

1. the clear of the view, the depth pre-pass of the solid level, the distance of the level alone (RB_DepthPrepass,
   VK_WorldDepth)
2. the solid surfaces (the list up to the first one past SS_BANNER)
3. what the level does over its solid surfaces: shadows, wet places, the light passes of the solid surfaces, smoke,
   the distance for soft sprites, sparks (RB_DrawWet)
4. what is transparent before thick glass in the list; with thick glass in the view: the saber blades behind it
   (VK_SaberBlades early) and what is transparent and lies behind a pane of it (rbRun_t behind)
5. the glass and everything after it in the list, the glass with its copies of the frame; what lies behind a pane was
   drawn in 4 and is left out here
6. what the list put off to its end, the distortions (RB_DistortionFill)

The glow pass and the run for the entities after the glow go through the same phases (3 and 4 have nothing to do
then). The light passes of the solid surfaces are one part of the list drawn in a mode of its own (rb_dlightsOnly).
===============
*/
typedef enum {
	RB_RUN_ALL,			// as it comes
	RB_RUN_BEHIND,		// what is transparent, no glass, and lies behind a pane of thick glass (rb_fxPass 1)
} rbRunMode_t;

typedef struct {
	int			depthRange;			// what the last batch left the viewport at
	int			glassCaptures;		// copies of the frame for glass in this run (v_glassCaptures)
	qboolean	forceCapture;		// the next glass copies the frame in any case (what it shows was drawn after the last copy)
	drawSurf_t	*listEnd;			// the end of the whole list (the glass pre-pass looks on to it)
} rbRun_t;

// the surfaces start to end of the list, batched, onto the view as it is
static void RB_DrawRange( rbRun_t *run, drawSurf_t *drawSurfs, int start, int end, rbRunMode_t mode ) {
	shader_t *shader = NULL, *oldShader = NULL;
	int fogNum = 0, oldFogNum = -1;
	int entityNum = 0, oldEntityNum = -1;
	int dlighted = 0, oldDlighted = qfalse;
	int depthRange = 0, oldDepthRange = run->depthRange;
	uint64_t oldSort = (uint64_t)-1;
	const float originalTime = backEnd.refdef.floatTime;	// save original time for entity shader offsets
	drawSurf_t *drawSurf = drawSurfs + start;
	// what VK_ShaderMergesEntities() says about a shader, asked once for a run of surfaces
	const shader_t *mergeShader = NULL;
	int shaderMerges = 0;
	// the entity came into a batch that was begun, RB_SetEntity() has not run for it
	qboolean mergedEntity = qfalse;

	backEnd.currentEntity = &tr.worldEntity;
	for ( int i = start; i < end; i++, drawSurf++ ) {
		// VisionJK: marks on a tessellated surface are a batch of their own for each such surface (srfPoly_t tessShader)
		shader_t *markTess = ( *drawSurf->surface == SF_POLY ) ? ( (srfPoly_t *)drawSurf->surface )->tessShader : NULL;

		if ( drawSurf->sort == oldSort && markTess == tess.markTess && !rb_fxPass ) {
			// fast path, same as previous sort
			RB_AddDrawSurf( drawSurf );
			continue;
		}
		R_DecomposeSort( drawSurf->sort, &entityNum, &shader, &fogNum, &dlighted );
		if ( rb_overLater && shader->over ) {
			rb_sawOver = qtrue;
		}
		// if we're rendering glowing objects, and this shader has no stages with glow, skip it. And what stands
		// over the scene while that is put out of focus: RB_RenderOverSurfs() draws it
		// And RF_AFTERGLOW, in a view that adds a glow: left out of the first run, and the only thing in the last.
		// (The glow pass between them draws what glows of such an entity as of any other.)
		qboolean wrongRun = qfalse;
		if ( rb_glowView && !g_bRenderGlowingObjects ) {
			const qboolean late = (qboolean)( entityNum != REFENTITYNUM_WORLD
				&& ( backEnd.refdef.entities[entityNum].e.renderfx & RF_AFTERGLOW ) != 0 );

			wrongRun = (qboolean)( late != (qboolean)rb_lateEntities );
			if ( wrongRun && late ) {
				rb_sawLate = qtrue;
			}
		}
		if ( rb_dlightsOnly && shader->sort > SS_OPAQUE ) {
			break;		// the light passes are for the solid surfaces alone, and the list has them first
		}
#ifndef RD_VISION_GLES
		// VisionJK: what is transparent, from the first thick glass on: the run before the glass has what lies behind
		// a pane and no glass, the main run the rest (RB_SurfBehindGlass)
		if ( rb_fxPass ) {
			const qboolean behindRun = (qboolean)( mode == RB_RUN_BEHIND );
			qboolean mine = (qboolean)( !behindRun );

			if ( shader->sort > SS_OPAQUE && !RB_ShaderHasGlass( shader ) ) {
				const int behind = RB_SurfBehindGlass( drawSurf, entityNum );

				mine = (qboolean)( behind == 2 || ( behind == 1 ) == behindRun );
			}
			if ( !mine ) {
				shader = oldShader;
				entityNum = oldEntityNum;
				fogNum = oldFogNum;
				dlighted = oldDlighted;
				continue;
			}
		}
#endif
		// the run of the light passes: only what a light reaches, and nothing that the end of the list draws (their
		// light passes come with them there)
		const qboolean notLit = (qboolean)( rb_dlightsOnly && ( ( !drawSurf->dlightBits
			&& ( entityNum == REFENTITYNUM_WORLD || !backEnd.refdef.entities[entityNum].numLamps ) ) || ( entityNum != REFENTITYNUM_WORLD
			&& ( backEnd.refdef.entities[entityNum].e.renderfx & ( RF_DISTORTION | RF_FORCEPOST | RF_FORCE_ENT_ALPHA ) ) ) ) );
		if ( ( g_bRenderGlowingObjects && !shader->hasGlow ) || ( rb_overLater && shader->over ) || wrongRun || notLit ) {
			shader = oldShader;
			entityNum = oldEntityNum;
			fogNum = oldFogNum;
			dlighted = oldDlighted;
			continue;
		}
		oldSort = drawSurf->sort;

		// (a view of its own leaves those out: the view of the level draws them after its glass, which hides the ones
		// behind it, and a distortion wants a copy of the frame. A view through a portal of the portal gun is the only
		// one that shows what it sees: it draws them at its end as the level view does, a distortion aside; the own
		// body the third person camera fades is one of them)
		if ( entityNum != REFENTITYNUM_WORLD && backEnd.viewParms.subView
			&& ( backEnd.refdef.entities[entityNum].e.renderfx & ( RF_DISTORTION | RF_FORCEPOST | RF_FORCE_ENT_ALPHA ) )
			&& ( !backEnd.viewParms.portalView || ( backEnd.refdef.entities[entityNum].e.renderfx & RF_DISTORTION ) ) ) {
			shader = oldShader;
			entityNum = oldEntityNum;
			fogNum = oldFogNum;
			dlighted = oldDlighted;
			oldSort = (uint64_t)-20;
			continue;
		}
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
			oldSort = (uint64_t)-20;	// invalidate this thing, cause we may want to postrender more surfs of the same sort
			continue;
		}
		// A distortion that finds the list full is left out: drawn now it would be its picture (white), not the screen.
		if ( entityNum != REFENTITYNUM_WORLD && ( backEnd.refdef.entities[entityNum].e.renderfx & RF_DISTORTION ) ) {
			shader = oldShader;
			entityNum = oldEntityNum;
			fogNum = oldFogNum;
			dlighted = oldDlighted;
			oldSort = (uint64_t)-20;
			continue;
		}

		// the sprites of an effect are an entity each: they stay one batch where that changes nothing
		qboolean merge = qfalse;

		if ( entityNum != oldEntityNum && shader == oldShader && fogNum == oldFogNum && dlighted == oldDlighted
			&& !shader->entityMergable && v_vkMerge->integer ) {
			if ( shader != mergeShader ) {
				mergeShader = shader;
				shaderMerges = VK_ShaderMergesEntities( shader );
			}
			merge = VK_EntitiesMerge( shaderMerges, oldEntityNum, entityNum );
		}

		// change the tess parameters if needed. A "entityMergable" shader is a shader that can have surfaces
		// from separate entities merged into a single batch, like smoke and blood puff sprites
		if ( shader != oldShader || fogNum != oldFogNum || dlighted != oldDlighted
			|| ( entityNum != oldEntityNum && !shader->entityMergable && !merge ) || markTess != tess.markTess ) {
			if ( oldShader != NULL ) {
				RB_EndSurface();
			}
#ifndef RD_VISION_GLES
			// VisionJK: glass shows the scene as drawn so far, so the frame is copied before each glass shader, up to
			// v_glassCaptures a view (the glass that is farther drawn first shows behind the nearer); not in the faces
			// of the reflection probes and not in the glow pass. (Also in a view of what lies behind glass: drawn as
			// its shader text says there, a pane that is clear as glass stood in that view as the glowing texture it
			// was made of, and the glass in front of it showed that round a player, t3_bounty 2026-11-27.)
			if ( shader != oldShader && ( run->glassCaptures < v_glassCaptures->integer || run->forceCapture ) && !g_bRenderGlowingObjects
				&& !backEnd.viewParms.probeFace && v_pbr->integer && RB_ShaderHasGlass( shader ) ) {
				// where glass begins in the list (the thick glass, later the thin panes): its depth first, and the
				// picture of the faces the eye sees (VK_GlassMask); only the view of the level
				if ( ( !oldShader || !RB_ShaderHasGlass( oldShader ) ) && rb_glassRound != 2 && !rb_dlightsOnly && vk.depthReadable
					&& !backEnd.viewParms.isPortal && !( backEnd.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) ) ) {
					// (in the whole depth: after the view weapon, which is drawn into the nearest part of it, the depth
					// of the glass went there as well, and the glass, drawn behind its own depth, was gone in first
					// person)
					if ( oldDepthRange ) {
						RB_SetDepthRange( 0 );
					}
					// (the depth of the frame without the glass is kept and put back after the mask: each pane is then
					// drawn far to near as it writes its depth, and glass behind glass shows, vk_post.cpp VK_KeepDepth)
					const qboolean keptDepth = VK_KeepDepth();

					RB_GlassPrepass( drawSurf, (int)( run->listEnd - drawSurf ) );
					VK_GlassMask();
					if ( keptDepth ) {
						VK_PutBackDepth();
					}
					// the first, thick glass: its faces kept for the smoke and the rings in front of it, and the smoke
					// and the rings on the water behind it now, so they are in the copy of the frame the glass bends
					// (vk_smoke.cpp, vk_ripple.cpp)
					if ( !vk.thickReady && !R_IsThinGlass( shader ) && RB_IsLevelView() ) {
						VK_KeepThickGlass();
						VK_SmokeDraw( SMOKE_BEHIND_GLASS );
						VK_GpuMark( "smoke" );
						if ( r_numRipples ) {
							VK_RippleDraw( RIPPLE_BEHIND_GLASS );
							VK_GpuMark( "water rings" );
						}
					}
					if ( oldDepthRange ) {
						RB_SetDepthRange( oldDepthRange );
					}
					// (the entity of the list is set anew: the pre-pass drew as the level)
					oldEntityNum = -1;
					mergedEntity = qfalse;
				}
				VK_Checkpoint( "a copy of the frame for glass" );
				VK_CaptureFrame();
				run->glassCaptures++;
				run->forceCapture = qfalse;
				vk_glassReady = qtrue;
			}
#endif
			RB_BeginSurface( shader, fogNum );
			tess.markTess = markTess;
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
		RB_AddDrawSurf( drawSurf );
	}
	backEnd.refdef.floatTime = originalTime;

	// draw the contents of the last shader batch
	if ( oldShader != NULL ) {
		RB_EndSurface();
	}
	run->depthRange = oldDepthRange;
}

/*
===============
RB_DrawGlassRounds

From the surface from on to end: what is transparent before the thick glass, the thick glass in two rounds, the rest.
The thick glass is sorted far to near by the middle of each surface (tr_main.cpp R_SortGlass) and writes its depth,
so a large surface close to the eye came after a pane farther off that stands in front of part of it, and was gone
behind its depth there: water behind a light shaft made glass (their word 2026-11-27). Round 1 draws the glass that
lies behind other glass and faces the eye, round 2 with a new copy of the frame the nearest glass, told apart by the
faces the eye sees (VK_GlassMask, generic.frag GlassStage). Only where the view takes that picture.
===============
*/
static void RB_DrawGlassRounds( rbRun_t *run, drawSurf_t *drawSurfs, int from, int end ) {
	int thickStart = end, thickEnd = end;

#ifndef RD_VISION_GLES
	if ( v_pbr->integer && v_glassCaptures->integer > 0 && vk.depthReadable && !g_bRenderGlowingObjects && !rb_dlightsOnly
		&& !backEnd.viewParms.isPortal && !backEnd.viewParms.probeFace
		&& !( backEnd.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) ) ) {
		for ( int i = from; i < end; i++ ) {
			shader_t	*shader;
			int			entityNum, fogNum, dlighted;

			R_DecomposeSort( drawSurfs[i].sort, &entityNum, &shader, &fogNum, &dlighted );
			const qboolean thick = (qboolean)( RB_ShaderHasGlass( shader ) && !R_IsThinGlass( shader ) );

			if ( thick && thickStart == end ) {
				thickStart = i;
			} else if ( !thick && thickStart < end ) {
				thickEnd = i;
				break;
			}
		}
	}
#endif
	if ( thickStart >= end ) {
		RB_DrawRange( run, drawSurfs, from, end, RB_RUN_ALL );
		return;
	}
	if ( from < thickStart ) {
		RB_DrawRange( run, drawSurfs, from, thickStart, RB_RUN_ALL );
	}
	rb_glassRound = 1;
	RB_DrawRange( run, drawSurfs, thickStart, thickEnd, RB_RUN_ALL );
#ifndef RD_VISION_GLES
	// (without the picture of the faces round 1 drew all of it)
	if ( vk.glassReady ) {
		VK_Checkpoint( "the nearest thick glass" );
		rb_glassRound = 2;
		run->forceCapture = qtrue;
		RB_DrawRange( run, drawSurfs, thickStart, thickEnd, RB_RUN_ALL );
	}
#endif
	rb_glassRound = 0;
	if ( thickEnd < end ) {
		RB_DrawRange( run, drawSurfs, thickEnd, end, RB_RUN_ALL );
	}
}

static void RB_RenderDrawSurfList( drawSurf_t *drawSurfs, int numDrawSurfs ) {
	rbRun_t run;

	memset( &run, 0, sizeof( run ) );
	run.listEnd = drawSurfs + numDrawSurfs;
#ifndef RD_VISION_GLES
	RB_GlassCvars();
	vk_glassReady = qfalse;
#endif
	// the light passes of the solid surfaces: the view, its depth and its stencil stay as the run they belong to left them
	if ( rb_dlightsOnly ) {
		RB_DrawRange( &run, drawSurfs, 0, numDrawSurfs, RB_RUN_ALL );
		return;
	}

	// 1. the view begins
	RB_BeginDrawingView();
	if ( backEnd.isHyperspace ) {
		return;
	}
	backEnd.pc.c_surfaces += numDrawSurfs;
#ifndef RD_VISION_GLES
	// (after the clear of the view, before its first surface; only while the materials are lit per pixel: a
	// texture times its lightmap costs about what the pre-pass costs, so without them it gains nothing)
	if ( rb_prepassWanted && !g_bRenderGlowingObjects && v_depthPrepass->integer && v_pbr->integer ) {
		RB_DepthPrepass( drawSurfs, numDrawSurfs );
		// (a view with thick glass: the distance of the level alone, while the depth holds nothing else)
		if ( rb_layering && rb_glassPanes ) {
			VK_WorldDepth();
		}
	}
#endif

	// where the phases begin in the list: the first surface past the solid ones, and the first thick glass when
	// something is drawn before it (RB_GlassLayers)
	int solidEnd = numDrawSurfs;

	for ( int i = 0; i < numDrawSurfs; i++ ) {
		shader_t	*shader;
		int			entityNum, fogNum, dlighted;

		R_DecomposeSort( drawSurfs[i].sort, &entityNum, &shader, &fogNum, &dlighted );
		if ( shader->sort > SS_BANNER ) {
			solidEnd = i;
			break;
		}
	}
	int glassAt = numDrawSurfs;
#ifndef RD_VISION_GLES
	if ( rb_layering && ( rb_fxPending || rb_bladePending ) ) {
		glassAt = Com_Clampi( 0, numDrawSurfs, rb_fxPending ? rb_fxAt : rb_bladeAt );
	}
#endif
	const int first = Q_min( solidEnd, glassAt );

	// 2. the solid surfaces
	VK_Checkpoint( "phase 2: the solid surfaces" );
	RB_DrawRange( &run, drawSurfs, 0, first, RB_RUN_ALL );
	VK_Checkpoint( "phase 3: over the solid surfaces" );
	// 3. what the level does over them, and the views behind its glass (they need the shadows of the frame)
	RB_DrawWet( run.depthRange );
#ifndef RD_VISION_GLES
	if ( rb_layering && RB_IsLevelView() ) {
		RB_DrawDeferredViews();
		RB_SetDepthRange( run.depthRange );
	}
	// the openings of the portals of the portal gun, over the solid level and with the views through them now drawn
	// (vk_portal.cpp); in a view of its own as well, where they show the pictures they have
	if ( rb_layering && ( RB_IsLevelView() || backEnd.viewParms.subView ) && backEnd.refdef.numPortals ) {
		VK_GpuMark( "level" );
		VK_PortalDraw();
		VK_GpuMark( "portals" );
		RB_SetDepthRange( run.depthRange );
	}
#endif
	// 4. what is transparent before the thick glass, the blades and what is transparent behind it
#ifndef RD_VISION_GLES
	if ( glassAt < numDrawSurfs ) {
		if ( solidEnd < glassAt ) {
			RB_DrawRange( &run, drawSurfs, solidEnd, glassAt, RB_RUN_ALL );
		}
		if ( rb_bladePending ) {
			rb_bladePending = qfalse;
			VK_GpuMark( "level" );
			VK_SaberBlades( qfalse, qfalse, qtrue );
			VK_GpuMark( "saber blades" );
			RB_SetDepthRange( run.depthRange );
			// (the glass that comes takes its picture of the frame now, with them in it)
			run.forceCapture = qtrue;
		}
		if ( rb_fxPending ) {
			VK_Checkpoint( "phase 4: what is transparent behind glass" );
			rb_fxPending = qfalse;
			rb_fxPass = 1;
			RB_DrawRange( &run, drawSurfs, glassAt, numDrawSurfs, RB_RUN_BEHIND );
			// (from here on what that drew is left out)
			rb_fxPass = 2;
			vk_glassReady = qfalse;
			RB_SetDepthRange( run.depthRange );
			run.forceCapture = qtrue;
		}
		// 5. the glass and the rest
		VK_Checkpoint( "phase 5: the glass and the rest" );
		RB_DrawGlassRounds( &run, drawSurfs, glassAt, numDrawSurfs );
	} else
#endif
	{
		// 4 and 5 without anything drawn before the thick glass
		RB_DrawGlassRounds( &run, drawSurfs, first, numDrawSurfs );
	}
#ifndef RD_VISION_GLES
	vk_glassReady = qfalse;
#endif

	// 6. what was put off to the end
	VK_Checkpoint( "phase 6: what was put off" );
	const float originalTime = backEnd.refdef.floatTime;
	int depthRange = run.depthRange;

	if ( tr_stencilled && tr_distortionPrePost ) {
		// ok, cap it now: without what is drawn after everything else
		RB_DistortionFill();
	}

	// the last first, as rd-vanilla has it
	int lastPostEnt = -1;

	while ( g_numPostRenders > rb_postBase ) {
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
		RB_AddDrawSurf( post->drawSurf );
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
}

/*
===============
RB_RenderOverSurfs

The surfaces of the list that RB_RenderDrawSurfList() left out because they stand over the scene, onto the frame
as it is now. They test no depth and leave none.
===============
*/
static void RB_RenderOverSurfs( drawSurf_t *drawSurfs, int numDrawSurfs ) {
	shader_t *shader = NULL, *oldShader = NULL;
	int fogNum = 0, oldFogNum = -1;
	int entityNum = 0, oldEntityNum = -1;
	int dlighted = 0;
	uint64_t oldSort = (uint64_t)-1;
	const float originalTime = backEnd.refdef.floatTime;
	drawSurf_t *drawSurf = drawSurfs;
	qboolean over = qfalse;

	SetViewportAndScissor();
	for ( int i = 0; i < numDrawSurfs; i++, drawSurf++ ) {
		if ( drawSurf->sort != oldSort ) {
			oldSort = drawSurf->sort;
			R_DecomposeSort( drawSurf->sort, &entityNum, &shader, &fogNum, &dlighted );
			over = (qboolean)shader->over;
			if ( !over ) {
				continue;
			}
			if ( shader != oldShader || fogNum != oldFogNum || entityNum != oldEntityNum ) {
				if ( oldShader != NULL ) {
					RB_EndSurface();
				}
				RB_BeginSurface( shader, fogNum );
				oldShader = shader;
				oldFogNum = fogNum;
			}
			if ( entityNum != oldEntityNum ) {
				RB_SetEntity( entityNum, originalTime );
				oldEntityNum = entityNum;
			}
		} else if ( !over ) {
			continue;
		}
		RB_AddDrawSurf( drawSurf );
	}
	if ( oldShader != NULL ) {
		RB_EndSurface();
	}
	backEnd.refdef.floatTime = originalTime;

	// go back to the world modelview matrix
	backEnd.currentEntity = &tr.worldEntity;
	backEnd.ori = backEnd.viewParms.world;
	VK_InvalidateTransform();
}

static const void *RB_DrawSurfs( const void *data ) {
	const drawSurfsCommand_t *cmd = (const drawSurfsCommand_t *)data;

#ifndef RD_VISION_GLES
	// a view of its own is drawn in the middle of the view of the level it was made for, which comes after it
	// (RB_DrawDeferredViews)
	if ( cmd->viewParms.subView && !rb_nested ) {
		if ( rb_numDeferred < SUB_VIEWS ) {
			rb_deferred[rb_numDeferred++] = cmd;
		}
		return (const void *)( cmd + 1 );
	}
#endif
	VK_BeginFrame();
	// finish any 2D drawing if needed
	if ( tess.numIndexes ) {
		RB_EndSurface();
	}
	// what came before this view: the start of the frame (uploads, passes of their own) before the first one, the 2D
	// between two scenes (the models of the HUD and the menus) before the others
	if ( !rb_nested ) {
		VK_GpuMark( VK_GpuFirstMark() ? "frame start" : "2d between views" );
	}
	backEnd.refdef = cmd->refdef;
	backEnd.viewParms = cmd->viewParms;
	VK_Checkpoint( backEnd.viewParms.portalView ? "a view through a portal begins" : backEnd.viewParms.subView ? "a view behind glass begins" : backEnd.viewParms.isPuddle ? "the puddle view begins"
		: backEnd.viewParms.probeFace ? "a probe face begins" : backEnd.viewParms.isPortal ? "a mirror or portal view begins"
		: ( backEnd.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) ) ? "a view without the level begins" : "the view of the level begins" );
	// the view mirrored at the floor of the puddles goes into targets of its own, which the wet places read
	const qboolean puddle = backEnd.viewParms.isPuddle;
	if ( puddle && !VK_BeginPuddle() ) {
		return (const void *)( cmd + 1 );
	}
	// a face of a reflection probe as well (VK_BeginProbe)
	const int probeFace = backEnd.viewParms.probeFace;
	if ( probeFace && !VK_BeginProbe() ) {
		return (const void *)( cmd + 1 );
	}
	// a view of its own, behind a plane of thick glass (R_GlassViews) or through a portal (R_PortalViews): into its part
	// of the frame, and kept (VK_EndSubView)
	const int subView = backEnd.viewParms.subView;
	if ( subView && !VK_BeginSubView() ) {
		return (const void *)( cmd + 1 );
	}
	// A module asked for the scene out of focus (RE_SetDepthOfField): the view of the level itself, not a
	// mirror in it, the sky of a portal or a model of the HUD. The 2D comes afterwards and is sharp.
	const qboolean focus = (qboolean)( r_dofAmount > 0.0f && v_menuBlur->value > 0.0f && !backEnd.viewParms.isPortal
		&& !( backEnd.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) ) );

	// RE_SetScopeEffect(): the view of the level itself, as the focus below
	const qboolean scope = (qboolean)( r_scopeFx.flags && !backEnd.viewParms.isPortal
		&& !( backEnd.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) ) && VK_HasScope() );

	rb_overLater = focus;
	rb_sawOver = qfalse;
	// (not the puddle view: its targets are half the size of the glow buffer, VK_PuddleSize)
	rb_glowView = !( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) && r_DynamicGlow->integer && !probeFace && !puddle && !subView;
	rb_sawLate = qfalse;
	// the sprayed pictures of RE_AddPaint, the wet places of RE_AddWet and the water of RE_AddLiquid: the view of the
	// level itself, before its glow. Paint and wet ground in the list before what is transparent (RB_DrawWet), the
	// water over what was drawn.
	const qboolean levelView = (qboolean)( !backEnd.viewParms.isPortal
		&& !( backEnd.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) ) );
	rb_viewSerial++;
#ifdef RD_VISION_GLES
	rb_wetLater = (qboolean)( levelView && ( r_numWet || r_numPaint ) );
#else
	// (the sparks bounce off it: v_gpuSparks wants it too; and glass that has views of what lies behind it, to tell
	// what stands in front of it)
	// (a view of its own of the level has what the view of the level does over its solid surfaces too: it is drawn in
	// the middle of that one, RB_DrawDeferredViews)
	const qboolean fullView = (qboolean)( levelView || subView );

	rb_softLater = (qboolean)( fullView && ( v_softParticles->integer || v_gpuSparks->integer || backEnd.viewParms.numGlassViews )
		&& vk.depthReadable && !probeFace && !puddle );
	vk.softReady = qfalse;
	vk.frontReady = qfalse;
	vk.solidReady = qfalse;
	vk.glassReady = qfalse;
	vk.thickReady = qfalse;
	rb_wetLater = (qboolean)( fullView && ( r_numWet || r_numPaint || VK_RT_Wanted() || rb_softLater ) );
	// the moving lights after the shadows of the players (RB_DrawWet)
	rb_deferDlights = (qboolean)( levelView && VK_RT_Wanted() );
	rb_sweepSurfs = cmd->drawSurfs;
	rb_sweepCount = cmd->numDrawSurfs;
#endif
	if ( levelView ) {
		VK_LevelFragments( qtrue );
#ifndef RD_VISION_GLES
		VK_GlassRows();
#endif
	}
	rb_prepassOn = qfalse;
	rb_prepassWanted = levelView;
#ifndef RD_VISION_GLES
	// a view with thick glass: what of it is drawn before the glass
	rb_glassPanes = 0;
	rb_bladePending = qfalse;
	rb_fxPending = qfalse;
	vk.worldReady = qfalse;
	if ( levelView && !probeFace && !puddle && vk.depthReadable ) {
		RB_GlassLayers( cmd->drawSurfs, cmd->numDrawSurfs );
	}
	rb_layering = qtrue;
	rb_fxPass = 0;
#endif
	RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs );
#ifndef RD_VISION_GLES
	rb_layering = qfalse;
	rb_fxPass = 0;
	// (views behind glass that were kept and not drawn, the view of the level had nothing to draw: their commands are
	// of this frame alone. A mirror or puddle view that comes between them and the view of the level leaves them.)
	if ( !rb_nested && levelView ) {
		rb_numDeferred = 0;
	}
#endif
	rb_prepassWanted = qfalse;
	VK_LevelFragments( qfalse );
	rb_wetLater = qfalse;
	rb_softLater = qfalse;
#ifndef RD_VISION_GLES
	vk.solidReady = vk.frontReady;
	vk.softReady = qfalse;
	vk.frontReady = qfalse;
#endif
	rb_deferDlights = qfalse;
#ifndef RD_VISION_GLES
	if ( subView ) {
		if ( r_numRipples ) {
			VK_RippleDraw( RIPPLE_IN_FRONT );
		}
		VK_SmokeDraw( SMOKE_IN_FRONT );
		RB_GlassViewBlades();
		VK_GpuMark( backEnd.viewParms.portalView ? "portal views" : "glass views" );
		VK_EndSubView();
		tess.shader = NULL;
		return (const void *)( cmd + 1 );
	}
#endif
#ifndef RD_VISION_GLES
	// the rings on the water of the level, over its transparent surfaces and under the clear water of the cgame
	if ( levelView && r_numRipples ) {
		VK_GpuMark( "level" );
		VK_RippleDraw( RIPPLE_IN_FRONT );
		VK_GpuMark( "water rings" );
	}
	// the smoke in front of the thick glass, over everything transparent (the part behind it was drawn before the glass)
	if ( levelView ) {
		VK_GpuMark( "level" );
		VK_SmokeDraw( SMOKE_IN_FRONT );
		VK_GpuMark( "smoke" );
	}
	// The saber blades (shaders/saber.glsl), added onto the view of the level once all of it is drawn. They were added
	// before what is transparent: water or glass behind a blade was then drawn over it, and a stage that multiplies
	// (the lightmap of water) took the blade away (their picture 2026-11-26). Light that is added does not care for the
	// order among sparks and flashes, which are added too; of what covers, a blade now shines through what lies in
	// front of it (smoke), which a glowing blade may. Glass writes its depth: the blades are held against the solid
	// level alone (vk.solidReady), or a blade behind a pane were gone.
	if ( levelView && backEnd.refdef.numSaberBlades > 0 ) {
		VK_GpuMark( "level" );
		VK_SaberBlades( qfalse );
		VK_GpuMark( "saber blades" );
	}
	// v_glassDebug 1: the glass and what stands in front of it in colours (vk_glass.cpp)
	if ( levelView && v_glassDebug && ( v_glassDebug->integer == 1 || v_glassDebug->integer == 6 ) ) {
		extern qboolean vk_glassDebugSolid;

		vk_glassDebugSolid = (qboolean)( v_glassDebug->integer == 6 );
		VK_GlassDebugDraw();
	}
#endif
	if ( levelView && r_numLiquids ) {
		VK_DrawLiquids();
	}
	VK_GpuMark( probeFace ? "probe faces" : ( puddle ? "puddle view" : ( levelView ? "level" : "portals and models" ) ) );
	if ( probeFace ) {
		VK_EndProbe( probeFace - 1, backEnd.viewParms.probeIndex, tr.numProbes );
		tess.shader = NULL;
		return (const void *)( cmd + 1 );
	}
#ifndef RD_VISION_GLES
	// the saber blades in a mirror or portal of the level (the view of the level drew its own above)
	if ( backEnd.viewParms.isPortal && !puddle && !( backEnd.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) ) ) {
		VK_SaberBlades( qtrue );
		VK_GpuMark( "saber blades in a portal" );
	}
#endif
	// the flares of this view want to know what the depth is where they are, before the next view clears it
	VK_CopyProbes();
	// the stages that glow once more, into a buffer that is blurred and added to the scene (vk_post.cpp)
	if ( rb_glowView ) {
		VK_BeginGlow();
		g_bRenderGlowingObjects = true;
		RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs );
		g_bRenderGlowingObjects = false;
		// the 3D saber blades in their colour, so they get the halo of the flat ones
		if ( levelView ) {
			VK_SaberBlades( qfalse, qtrue );
		}
		VK_EndGlow();
		// the entities that lie on that glow (RF_AFTERGLOW), into the frame and against the depth that are there
		if ( rb_sawLate ) {
			rb_lateEntities = true;
			RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs );
			rb_lateEntities = false;
		}
		VK_GpuMark( "glow" );
	}
	rb_glowView = false;
	if ( puddle ) {
		VK_EndPuddle();
		tess.shader = NULL;
		return (const void *)( cmd + 1 );
	}
	// tay_bloom, v_tonemap and v_smaa: the view of the level itself, as the focus below. The bloom takes the light
	// above white before the tone map, SMAA finds its edges in the colours of the screen after it.
	if ( !backEnd.viewParms.isPortal && !( backEnd.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) ) ) {
		VK_BeginPostSteps();
		VK_SsrPrep();
		VK_GpuMark( "ssr picture" );
		VK_Bloom();
		VK_GpuMark( "bloom" );
		VK_Tonemap();
		VK_GpuMark( "tonemap" );
		VK_Smaa();
		VK_GpuMark( "smaa" );
		VK_EndPostSteps();
	}
	// RE_SetScreenEffect(): the view of the level itself, before the scope, which lays its lens over it
	if ( r_screenFxSet && !backEnd.viewParms.isPortal && !( backEnd.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) )
		&& VK_HasScope() ) {
		VK_ScreenFx( &r_screenFx );
		VK_GpuMark( "screen effects" );
	}
	if ( scope ) {
		// the boxes of the tracker into the glow buffer, where the lens keeps the colours of the level
		const qboolean boxes = (qboolean)( ( r_scopeFx.flags & SCOPEFX_TRACKER ) && r_scopeFx.numBoxes > 0 );

		if ( boxes ) {
			VK_ScopeBoxes( &r_scopeFx );
		}
		VK_Scope( &r_scopeFx, boxes );
	}
	if ( focus ) {
		VK_BlurFrame( r_dofAmount, r_dofFocus );
		if ( rb_sawOver ) {
			RB_RenderOverSurfs( cmd->drawSurfs, cmd->numDrawSurfs );
		}
	}
	rb_overLater = qfalse;
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
	VK_GpuMark( "weather" );
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
	// What the engine does with its window once a frame, as rd-vanilla has it at this place: a change of
	// r_fullscreen is carried out, and fullscreen is refused with in_nograb 1. The picture is not its business
	// here, the swapchain showed it.
	ri.WIN_Present( &window );
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
		case RC_SET_2D_PLANE:
			data = RB_Set2DPlane( data );
			break;
		case RC_STRETCH_PIC:
			data = RB_StretchPic( data );
			break;
		case RC_BACKDROP:
			data = RB_Backdrop( data );
			break;
		case RC_ROTATE_PIC:
			data = RB_RotatePic( data, qfalse );
			break;
		case RC_ROTATE_PIC2:
			data = RB_RotatePic( data, qtrue );
			break;
		case RC_DRAW_TRIS_2D:
			data = RB_DrawTris2D( data );
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
		VK_DrawTess( &draw, VKP_GENERIC, GLS_DEPTHTEST_DISABLE, VKC_NONE, qfalse );
	}
	tess.numIndexes = 0;
	tess.numVertexes = 0;
}
