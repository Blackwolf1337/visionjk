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

// vk_frame.cpp: one frame from its first draw to the window, and reading it back for screenshots
//
// The game draws into vk.color with vk.depth. At the end of the frame the gamma pass copies that into vk.final,
// vk.final is copied to the window. A frame has buffers of its own for what the CPU makes anew every frame
// (geometry from the tess arrays, transforms, draws). There are VK_FRAMES of them, so the CPU fills the next
// one while the GPU still works on the last.
//
// This file keeps the books of a frame: what is in its buffers, which batch was set up last, what the draw before
// had set. What that means for the device is vk_cmd.cpp.

#include "vk_local.h"

// The vertices that are built on the CPU go into four streams of the frame, as the tess arrays have them:
// positions, normals, texture coordinates, colors. A vertex has the same number in all four. So the streams are
// bound once, and a batch says with the vertex offset of its draw calls where its vertices begin: hundreds of
// small batches a frame (every spark is one) then need no bind calls.
#define VK_STREAM_BYTES		( sizeof( vec4_t ) * 2 + sizeof( vec2_t ) * NUM_TEX_COORDS + sizeof( color4ub_t ) )
#define VK_STREAM_VERTEXES	( ( VK_VERTEX_BYTES / VK_STREAM_BYTES ) & ~(size_t)3 )

static const vkSize_t	vk_streamStride[4] = { sizeof( vec4_t ), sizeof( vec4_t ), sizeof( vec2_t ) * NUM_TEX_COORDS, sizeof( color4ub_t ) };
static const vkSize_t	vk_streamOffset[4] = { 0, VK_STREAM_VERTEXES * sizeof( vec4_t ), VK_STREAM_VERTEXES * sizeof( vec4_t ) * 2,
	VK_STREAM_VERTEXES * ( sizeof( vec4_t ) * 2 + sizeof( vec2_t ) * NUM_TEX_COORDS ) };

// The 2D comes last in a frame. A sixteenth of what a frame has is kept for it: a level that brings more than fits
// loses some of its own draws, not the HUD (issue #11, 32 players with stencil shadows).
static int VK_FrameRoom( int total ) {
	return backEnd.projection2D ? total : total - total / 16;
}

// x, y, width, height in the coordinates of OpenGL: y counts from the lower edge, in pixels of the window
void VK_SetViewport( int x, int y, int width, int height, float minDepth, float maxDepth ) {
	if ( !vk.frame ) {
		return;
	}
	vk.viewport[0] = x;
	vk.viewport[1] = y;
	vk.viewport[2] = width;
	vk.viewport[3] = height;
	VK_ViewportAndScissor( x, y, width, height, minDepth, maxDepth );
}

// glDepthRange: the part of the depth buffer the following draws land in
void VK_SetDepthRange( float minDepth, float maxDepth ) {
	VK_SetViewport( vk.viewport[0], vk.viewport[1], vk.viewport[2], vk.viewport[3], minDepth, maxDepth );
}

/*
===============
VK_BeginFrame
===============
*/
void VK_BeginFrame( void ) {
	vkFrame_t *frame;
	vkGlobals_t *globals;

	if ( vk.frame || !vk.device ) {
		return;
	}
	frame = &vk.frames[vk.frameIndex];
	VK_BeginCommands( frame );
	// what the device wrote when these commands ran last can be read now
	VK_ReadProbes( frame );
	frame->numVertexes = 0;
	frame->indexUsed = 0;
	frame->numTransforms = 0;
	frame->numDraws = 0;
	frame->numBones = 0;
	VK_ModelsBeginFrame();

	globals = (vkGlobals_t *)frame->globals.mapped;
	memset( globals, 0, sizeof( *globals ) );
	globals->gamma[0] = 1.0f / r_gamma->value;
	globals->gamma[1] = (float)( 1 << tr.overbrightBits );
	globals->gamma[2] = tr.identityLight;
	globals->screen[0] = (float)vk.width;
	globals->screen[1] = (float)vk.height;

	vk.frame = frame;
	VK_BeginTarget( &vk.color, &vk.depth, 1, qfalse );
	VK_BindFrameSets();
	vk.rendering = qtrue;
	vk.stateValid = qfalse;
	vk.overflowed = qfalse;
	vk.numRanges = 0;
	VK_SetViewport( 0, 0, vk.width, vk.height, 0.0f, 1.0f );
}

/*
===============
VK_EndFrame

The device does the rest: gamma pass, copy to the window, submit, present
===============
*/
void VK_EndFrame( void ) {
	vkFrame_t *frame = vk.frame;
	const float gamma[4] = { 1.0f / r_gamma->value, (float)( 1 << tr.overbrightBits ), 0.0f, 0.0f };

	if ( !frame ) {
		return;
	}
	// the light styles as the cgame left them for this frame
	vkGlobals_t *globals = (vkGlobals_t *)frame->globals.mapped;

	for ( int i = 0; i < MAX_LIGHT_STYLES; i++ ) {
		VectorSet4( globals->styles[i], styleColors[i][0] / 256.0f, styleColors[i][1] / 256.0f, styleColors[i][2] / 256.0f, 0.0f );
	}
	VK_EndTarget();
	vk.rendering = qfalse;
	VK_PresentFrame( gamma );

	vk.statDraws[1] = frame->numDraws;
	vk.statTransforms[1] = frame->numTransforms;
	vk.statBones[1] = frame->numBones;
	vk.statCpuVertexes[1] = vk.statCpuVertexes[0];
	vk.statCpuIndexes[1] = vk.statCpuIndexes[0];
	vk.statStaticIndexes[1] = vk.statStaticIndexes[0];
	vk.statNumCpuShaders[1] = vk.statNumCpuShaders[0];
	memcpy( vk.statCpuShaders[1], vk.statCpuShaders[0], sizeof( vk.statCpuShaders[0][0] ) * vk.statNumCpuShaders[0] );
	vk.statCpuVertexes[0] = vk.statCpuIndexes[0] = vk.statStaticIndexes[0] = vk.statNumCpuShaders[0] = 0;

	vk.finalValid = qtrue;
	vk.frame = NULL;
	vk.frameIndex = ( vk.frameIndex + 1 ) % VK_FRAMES;
}

/*
===============
VK_FrameInfo_f

Part of gfxinfo: what the textures cost and what the last frame was made of
===============
*/
void VK_FrameInfo_f( void ) {
	if ( vk.bcTextures ) {
		ri.Printf( PRINT_ALL, "  packed into blocks since the window was made: %.1f million pixels in %.2f s\n", vk.statBcPixels * 1e-6,
			vk.statBcSeconds );
	}
	ri.Printf( PRINT_ALL, "last frame: %i draws, %i triangles from buffers on the device, %i triangles and %i vertices built on the CPU\n",
		vk.statDraws[1], vk.statStaticIndexes[1] / 3, vk.statCpuIndexes[1] / 3, vk.statCpuVertexes[1] );
	ri.Printf( PRINT_ALL, "  room in a frame: %i draws, %i vertices built on the CPU\n", VK_MAX_DRAWS, (int)VK_STREAM_VERTEXES );
	ri.Printf( PRINT_ALL, "  %i of %i transforms, %i of %i bones\n", vk.statTransforms[1], VK_MAX_TRANSFORMS, vk.statBones[1], VK_MAX_BONES );
	// the shaders with the most triangles from the CPU
	for ( int n = 0; n < 8; n++ ) {
		vkStatShader_t *most = NULL;

		for ( int i = 0; i < vk.statNumCpuShaders[1]; i++ ) {
			if ( vk.statCpuShaders[1][i].indexes > ( most ? most->indexes : 0 ) ) {
				most = &vk.statCpuShaders[1][i];
			}
		}
		if ( !most ) {
			break;
		}
		ri.Printf( PRINT_ALL, "  on the CPU: %5i triangles %s\n", most->indexes / 3, most->name );
		most->indexes = 0;
	}
}

/*
===============
VK_AddTransform

Returns the index the draws refer to
===============
*/
int VK_AddTransform( const vkTransform_t *transform ) {
	// what was added last, as a copy that is cheap to read: the buffer of the frame is for writing
	static vkTransform_t last;
	static int lastIndex = -1;
	vkFrame_t *frame = vk.frame;

	if ( !frame ) {
		return 0;
	}
	// Sprites, sparks and other effects are an entity each and come by the hundred, all with the matrices of
	// the world. They share one entry. An index that is not below numTransforms is one of an older frame.
	if ( lastIndex >= 0 && lastIndex < frame->numTransforms && !memcmp( transform, &last, sizeof( last ) ) ) {
		return lastIndex;
	}
	if ( frame->numTransforms >= VK_FrameRoom( VK_MAX_TRANSFORMS ) ) {
		return frame->numTransforms - 1;
	}
	memcpy( frame->transforms.mapped + VK_TRANSFORM_STRIDE * frame->numTransforms, transform, sizeof( *transform ) );
	last = *transform;
	lastIndex = frame->numTransforms;
	return frame->numTransforms++;
}

/*
===============
VK_UploadGeometry

Copies vertices and indexes into the buffers of the frame and binds them. The arrays have the layout of the
tess arrays. What is drawn with VK_DrawTess() after this comes from there. qfalse: no room, nothing to draw.
normal NULL: no stage of the batch reads the normals (stage.glsl wants them for tcGen environment, rgbGen
lightingDiffuse and alphaGen lightingSpecular, and for deforms, which only batches from the device have there).
They are a fifth of a vertex and are left out then, the buffer keeps what it had in their place. Measured with the
fight demo: 3 to 4 % more frames a second.
The texture coordinates after the first set are four tenths of a vertex and an effect does not need them either,
but leaving them out was a loss of 4 %: they lie between the first sets, and writing every fifth pair of floats
into the memory of the device is slower than writing all of them in one go.
===============
*/
qboolean VK_UploadGeometry( const vec4_t *xyz, const vec4_t *normal, const vec2_t ( *texCoords )[NUM_TEX_COORDS],
	const color4ub_t *colors, int numVerts, const glIndex_t *indexes, int numIndexes ) {
	vkFrame_t *frame = vk.frame;
	const void *sources[4] = { xyz, normal, texCoords, colors };
	const vkSize_t indexBytes = (vkSize_t)numIndexes * sizeof( glIndex_t );

	if ( !frame || !numIndexes || !numVerts ) {
		return qfalse;
	}
	if ( frame->numVertexes + numVerts > VK_FrameRoom( (int)VK_STREAM_VERTEXES )
		|| frame->indexUsed + indexBytes > (vkSize_t)VK_FrameRoom( (int)VK_INDEX_BYTES ) ) {
		if ( !vk.overflowed ) {
			vk.overflowed = qtrue;
			ri.Printf( PRINT_DEVELOPER, "VK_UploadGeometry: the frame has more geometry than fits, the rest is not drawn\n" );
		}
		return qfalse;
	}
	for ( int i = 0; i < 4; i++ ) {
		if ( !sources[i] ) {
			continue;
		}
		memcpy( frame->vertexes.mapped + vk_streamOffset[i] + frame->numVertexes * vk_streamStride[i], sources[i],
			(size_t)( numVerts * vk_streamStride[i] ) );
	}
	if ( VK_DEVICE_BASE_VERTEX ) {
		memcpy( frame->indexes.mapped + frame->indexUsed, indexes, (size_t)indexBytes );
	} else {
		// the draw call can not say where the vertices of the batch begin, so the indexes do
		glIndex_t *out = (glIndex_t *)( frame->indexes.mapped + frame->indexUsed );

		for ( int i = 0; i < numIndexes; i++ ) {
			out[i] = indexes[i] + (glIndex_t)frame->numVertexes;
		}
	}
	VK_BindVertexBuffer( &frame->vertexes, vk_streamOffset, 4 );
	VK_BindIndexBuffer( &frame->indexes );
	vk.firstIndex = (uint32_t)( frame->indexUsed / sizeof( glIndex_t ) );
	vk.vertexOffset = frame->numVertexes;
	vk.numBoundIndexes = numIndexes;
	vk.numRanges = 0;
	frame->numVertexes += numVerts;
	frame->indexUsed += indexBytes;
	vk.statCpuVertexes[0] += numVerts;
	vk.statCpuIndexes[0] += numIndexes;
	return qtrue;
}

/*
===============
VK_UploadIndexes

Indexes into vertices that are in a buffer on the device already: offsets are where its vertex streams start
(xyz, normal, texCoords and colors of the level, the one stream of the models). vertexBuffer NULL: the draw reads
no vertices, the vertex shader makes them from the number of each (shaders/sprite.vert)
===============
*/
qboolean VK_UploadIndexes( const vkBuffer_t *vertexBuffer, const vkSize_t *offsets, int numBindings, const glIndex_t *indexes, int numIndexes ) {
	vkFrame_t *frame = vk.frame;
	const vkSize_t indexBytes = (vkSize_t)numIndexes * sizeof( glIndex_t );

	if ( !frame || !numIndexes || ( vertexBuffer && !vertexBuffer->buffer )
		|| frame->indexUsed + indexBytes > (vkSize_t)VK_FrameRoom( (int)VK_INDEX_BYTES ) ) {
		return qfalse;
	}
	memcpy( frame->indexes.mapped + frame->indexUsed, indexes, (size_t)indexBytes );
	if ( vertexBuffer ) {
		VK_BindVertexBuffer( vertexBuffer, offsets, numBindings );
	}
	VK_BindIndexBuffer( &frame->indexes );
	vk.firstIndex = (uint32_t)( frame->indexUsed / sizeof( glIndex_t ) );
	vk.vertexOffset = 0;
	vk.numBoundIndexes = numIndexes;
	vk.numRanges = 0;
	frame->indexUsed += indexBytes;
	vk.statStaticIndexes[0] += numIndexes;
	return qtrue;
}

// the batch in the tess arrays
qboolean VK_UploadTess( void ) {
	vkStatShader_t *stat = vk.statCpuShaders[0];
	int i;

	// for gfxinfo: which shaders still take this way
	for ( i = 0; i < vk.statNumCpuShaders[0] && stat[i].shader != tess.shader; i++ ) {
	}
	if ( i < VK_MAX_STAT_SHADERS ) {
		if ( i == vk.statNumCpuShaders[0] ) {
			stat[i].shader = tess.shader;
			Q_strncpyz( stat[i].name, tess.shader->name, sizeof( stat[i].name ) );
			stat[i].indexes = 0;
			vk.statNumCpuShaders[0]++;
		}
		stat[i].indexes += tess.numIndexes;
	}
	// the normals only for a stage that takes them, see VK_UploadGeometry()
	qboolean normals = qfalse;

	for ( i = 0; i < tess.shader->numUnfoggedPasses && tess.xstages[i].active && !normals; i++ ) {
		const shaderStage_t *pStage = &tess.xstages[i];

		normals = (qboolean)( pStage->rgbGen == CGEN_LIGHTING_DIFFUSE || pStage->rgbGen == CGEN_LIGHTING_DIFFUSE_ENTITY
			|| pStage->alphaGen == AGEN_LIGHTING_SPECULAR || pStage->bundle[0].tcGen == TCGEN_ENVIRONMENT_MAPPED
			|| pStage->bundle[1].tcGen == TCGEN_ENVIRONMENT_MAPPED );
	}
	return VK_UploadGeometry( tess.xyz, normals ? tess.normal : NULL, tess.texCoords, tess.vertexColors, tess.numVertexes,
		tess.indexes, tess.numIndexes );
}

/*
===============
VK_TessColors

A stage whose colors were computed on the CPU draws with them in the place of the vertex colors. NULL: back
to the vertex colors VK_UploadTess() put there.
===============
*/
void VK_TessColors( const color4ub_t *colors ) {
	vkFrame_t *frame = vk.frame;
	vkSize_t offset = vk_streamOffset[3];

	if ( !frame ) {
		return;
	}
	if ( colors ) {
		if ( frame->numVertexes + tess.numVertexes > VK_FrameRoom( (int)VK_STREAM_VERTEXES ) ) {
			return;
		}
		// the colors take a run of vertices of their own. The draws of the batch count from its first vertex, so
		// the stream is bound as far ahead as that run lies behind the batch.
		memcpy( frame->vertexes.mapped + offset + frame->numVertexes * sizeof( color4ub_t ), colors, (size_t)tess.numVertexes * sizeof( color4ub_t ) );
		offset += (vkSize_t)( frame->numVertexes - vk.vertexOffset ) * sizeof( color4ub_t );
		frame->numVertexes += tess.numVertexes;
	}
	VK_BindVertexStream( 3, &frame->vertexes, offset );
}

/*
===============
VK_DrawTess

One stage of a shader on the geometry that was uploaded last. cullMode is one of VKC_*.
===============
*/
void VK_DrawTess( const vkDraw_t *draw, vkProgram_t program, uint32_t stateBits, int cullMode, qboolean polygonOffset ) {
	vkFrame_t *frame = vk.frame;
	uint32_t state, changed, drawIndex;

	if ( !frame || frame->numDraws >= VK_FrameRoom( VK_MAX_DRAWS ) ) {
		return;
	}

	state = ( (uint32_t)cullMode << VKS_CULL_SHIFT );
	if ( !( stateBits & GLS_DEPTHTEST_DISABLE ) ) {
		state |= VKS_DEPTH_TEST;
	}
	if ( stateBits & GLS_DEPTHMASK_TRUE ) {
		state |= VKS_DEPTH_WRITE;
	}
	if ( stateBits & GLS_DEPTHFUNC_EQUAL ) {
		state |= VKS_DEPTH_EQUAL;
	}
	if ( polygonOffset ) {
		state |= VKS_OFFSET;
	}
	if ( vk.stencil && vk.stencil->test ) {
		state |= VKS_STENCIL;
	}
	changed = vk.stateValid ? ( state ^ vk.boundState ) : ~0u;
	vk.boundState = state;
	vk.stateValid = qtrue;

	drawIndex = (uint32_t)frame->numDraws++;
	memcpy( frame->draws.mapped + VK_DRAW_STRIDE * drawIndex, draw, sizeof( *draw ) );
	VK_Draw( program, stateBits, state, changed, drawIndex );
}

/*
===============
RB_ReadPixels

The frame that was shown last, as tr_init.cpp wants it for its screenshot commands: RGB, three bytes a pixel,
the lowest row first. Free the result with Hunk_FreeTempMemory().
===============
*/
byte *RB_ReadPixels( int x, int y, int width, int height, size_t *offset, int *padlen ) {
	byte *buffer = (byte *)Hunk_AllocateTempMemory( width * height * 3 + (int)*offset );
	byte *out = buffer + *offset;
	const byte *pixels;

	*padlen = 0;
	memset( out, 0, (size_t)width * height * 3 );
	if ( !vk.device || !vk.finalValid || x < 0 || y < 0 || x + width > vk.width || y + height > vk.height ) {
		return buffer;
	}

	pixels = VK_ReadFrame( x, y, width, height );

	for ( int row = 0; row < height; row++ ) {
		const byte *in = pixels + (size_t)( height - 1 - row ) * width * 4;

		for ( int i = 0; i < width; i++, in += 4, out += 3 ) {
			out[0] = in[0];
			out[1] = in[1];
			out[2] = in[2];
		}
	}
	return buffer;
}

static videoFrameCommand_t	vk_videoFrame;		// width 0: none asked for

/*
===============
RB_TakeVideoFrameCmd

Recording to an AVI file. The frame this command belongs to does not exist yet, its commands are still being
recorded. The picture is taken when the frame has gone to the device, see VK_WriteVideoFrame().
===============
*/
const void *RB_TakeVideoFrameCmd( const void *data ) {
	const videoFrameCommand_t *cmd = (const videoFrameCommand_t *)data;

	vk_videoFrame = *cmd;
	return (const void *)( cmd + 1 );
}

/*
===============
VK_WriteVideoFrame

After a frame: its picture goes to the client, packed as RB_TakeVideoFrameCmd() of rd-vanilla packs it
===============
*/
void VK_WriteVideoFrame( void ) {
	const videoFrameCommand_t *cmd = &vk_videoFrame;
	const size_t linelen = (size_t)cmd->width * 3;
	size_t offset = 0;
	int padlen;
	byte *pixels;

	if ( !cmd->width ) {
		return;
	}
	pixels = RB_ReadPixels( 0, 0, cmd->width, cmd->height, &offset, &padlen );
	if ( cmd->motionJpeg ) {
		const size_t size = RE_SaveJPGToBuffer( cmd->encodeBuffer, linelen * cmd->height, r_aviMotionJpegQuality->integer,
			cmd->width, cmd->height, pixels, padlen );

		ri.CL_WriteAVIVideoFrame( cmd->encodeBuffer, (int)size );
	} else {
		const size_t avipadwidth = PAD( linelen, AVI_LINE_PADDING );
		const size_t avipadlen = avipadwidth - linelen;
		const byte *in = pixels;
		byte *out = cmd->encodeBuffer;

		// blue first, and the lines as long as AVI wants them
		for ( int row = 0; row < cmd->height; row++ ) {
			for ( int i = 0; i < cmd->width; i++, in += 3 ) {
				*out++ = in[2];
				*out++ = in[1];
				*out++ = in[0];
			}
			memset( out, 0, avipadlen );
			out += avipadlen;
		}
		ri.CL_WriteAVIVideoFrame( cmd->encodeBuffer, (int)( avipadwidth * cmd->height ) );
	}
	Hunk_FreeTempMemory( pixels );
	vk_videoFrame.width = 0;
}
