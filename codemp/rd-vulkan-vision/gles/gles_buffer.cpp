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


// gles_buffer.cpp: buffers of OpenGL ES, and the buffers of a frame
//
// A host visible buffer is memory of the engine with, for most kinds, a buffer of OpenGL ES behind it: WebGL can
// not map a buffer. The shared files write into the memory. For the buffers of a frame GLES_UploadFrame() sends
// the part that was written when the frame ends, in one call each, before its draws are played. Everything else
// goes to the device at once with VK_CopyBuffer().

#include "vk_local.h"

// the four streams of frame->vertexes, as vk_frame.cpp fills them: xyz, normal, texCoords, colors
static const int	gles_streamStride[4] = { sizeof( vec4_t ), sizeof( vec4_t ), sizeof( vec2_t ) * NUM_TEX_COORDS, sizeof( color4ub_t ) };

/*
===============
GLES_BindBuffer

For writing into it. Which index buffer is bound belongs to the vertex array object that is bound, so for one of
those the object that is no object of this device takes it: the ones the draws use keep what they have.
===============
*/
void GLES_BindBuffer( const vkBuffer_t *buf ) {
	if ( buf->target == GL_ELEMENT_ARRAY_BUFFER ) {
		glBindVertexArray( 0 );
	}
	glBindBuffer( buf->target, buf->buffer );
}

void VK_CreateBuffer( vkBuffer_t *buf, vkSize_t size, vkBufferUsage_t usage, qboolean hostVisible ) {
	memset( buf, 0, sizeof( *buf ) );
	buf->size = size;
	if ( hostVisible ) {
		// zeroed: a frame sends parts it has not written, the normals of batches that have none
		buf->mapped = (byte *)Z_Malloc( (int)size, TAG_GENERAL, qtrue, 16 );
	}
	switch ( usage ) {
	case VKB_VERTEXES:	buf->target = GL_ARRAY_BUFFER;			break;
	case VKB_INDEXES:	buf->target = GL_ELEMENT_ARRAY_BUFFER;	break;
	case VKB_UNIFORMS:	buf->target = GL_UNIFORM_BUFFER;		break;
	default:			return;
	}
	glGenBuffers( 1, &buf->buffer );
	vk.statBuffers++;
	GLES_BindBuffer( buf );
	glBufferData( buf->target, (GLsizeiptr)size, NULL, hostVisible ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW );
}

void VK_DestroyBuffer( vkBuffer_t *buf ) {
	if ( buf->buffer ) {
		glDeleteBuffers( 1, &buf->buffer );
		vk.statBuffers--;
	}
	if ( buf->mapped ) {
		Z_Free( buf->mapped );
	}
	memset( buf, 0, sizeof( *buf ) );
}

void VK_CopyBuffer( const vkBuffer_t *src, vkSize_t srcOffset, vkBuffer_t *dst, vkSize_t dstOffset, vkSize_t size ) {
	GLES_BindBuffer( dst );
	glBufferSubData( dst->target, (GLintptr)dstOffset, (GLsizeiptr)size, src->mapped + srcOffset );
}

/*
===============
VK_Stage

The copy that follows hands the bytes to the device at once, so every piece starts at the beginning
===============
*/
byte *VK_Stage( vkSize_t size, vkSize_t *offset ) {
	if ( size > vk.staging.size ) {
		VK_DestroyBuffer( &vk.staging );
		VK_CreateBuffer( &vk.staging, size, VKB_STAGING, qtrue );
	}
	*offset = 0;
	return vk.staging.mapped;
}

void VK_FlushUploads( void ) {
}

/*
===============================================================================

THE BUFFERS OF A FRAME

===============================================================================
*/

void VK_InitFrames( void ) {
	for ( int i = 0; i < VK_FRAMES; i++ ) {
		vkFrame_t *frame = &vk.frames[i];

		VK_CreateBuffer( &frame->vertexes, VK_VERTEX_BYTES, VKB_VERTEXES, qtrue );
		VK_CreateBuffer( &frame->indexes, VK_INDEX_BYTES, VKB_INDEXES, qtrue );
		// No shader reads the globals, but their block is in every program of geometry: a block of the layout
		// std140 is never left out, and WebGL draws nothing while one of a program has no buffer.
		VK_CreateBuffer( &frame->globals, sizeof( vkGlobals_t ), VKB_UNIFORMS, qtrue );
		VK_CreateBuffer( &frame->transforms, (vkSize_t)VK_TRANSFORM_STRIDE * VK_MAX_TRANSFORMS, VKB_UNIFORMS, qtrue );
		VK_CreateBuffer( &frame->draws, (vkSize_t)VK_DRAW_STRIDE * VK_MAX_DRAWS, VKB_UNIFORMS, qtrue );
		// The block of the bones is bound from where a skeleton starts and is 256 bones long whatever the skeleton
		// has, and WebGL wants all of it to lie in the buffer: so much more room behind the last start.
		VK_CreateBuffer( &frame->bones, sizeof( float ) * 12 * VK_MAX_BONES + GLES_BONES_BYTES, VKB_UNIFORMS, qtrue );
		frame->numProbes = frame->numProbesCopied = 0;
		for ( int j = 0; j < GLES_PROBE_SETS; j++ ) {
			glGenQueries( VK_MAX_FLARES, frame->probeSets[j].queries );
			vk.statQueries += VK_MAX_FLARES;
			frame->probeSets[j].count = 0;
			frame->probeSets[j].pending = qfalse;
		}
	}
}

void VK_ShutdownFrames( void ) {
	for ( int i = 0; i < VK_FRAMES; i++ ) {
		vkFrame_t *frame = &vk.frames[i];

		VK_DestroyBuffer( &frame->vertexes );
		VK_DestroyBuffer( &frame->indexes );
		VK_DestroyBuffer( &frame->globals );
		VK_DestroyBuffer( &frame->transforms );
		VK_DestroyBuffer( &frame->draws );
		VK_DestroyBuffer( &frame->bones );
		for ( int j = 0; j < GLES_PROBE_SETS; j++ ) {
			glDeleteQueries( VK_MAX_FLARES, frame->probeSets[j].queries );
			vk.statQueries -= VK_MAX_FLARES;
		}
	}
	vk.frame = NULL;
}

// size bytes from the start of the memory of a buffer to the device. Returns how many calls of OpenGL ES it was.
static int GLES_UploadBuffer( const vkBuffer_t *buf, vkSize_t size ) {
	if ( !size ) {
		return 0;
	}
	GLES_BindBuffer( buf );
	glBufferSubData( buf->target, 0, (GLsizeiptr)size, buf->mapped );
	return ( buf->target == GL_ELEMENT_ARRAY_BUFFER ) ? 3 : 2;
}

/*
===============
GLES_UploadFrame

What the frame wrote into its buffers goes to the device, before its draws are played. Returns how many calls
of OpenGL ES it was, for gfxinfo.
===============
*/
int GLES_UploadFrame( const vkFrame_t *frame ) {
	int calls = 0;

	if ( frame->numVertexes ) {
		calls = 5;
		GLES_BindBuffer( &frame->vertexes );
		for ( int i = 0; i < 4; i++ ) {
			glBufferSubData( GL_ARRAY_BUFFER, (GLintptr)frame->streamOffsets[i], (GLsizeiptr)frame->numVertexes * gles_streamStride[i],
				frame->vertexes.mapped + frame->streamOffsets[i] );
		}
	}
	calls += GLES_UploadBuffer( &frame->indexes, frame->indexUsed );
	calls += GLES_UploadBuffer( &frame->globals, sizeof( vkGlobals_t ) );
	calls += GLES_UploadBuffer( &frame->transforms, (vkSize_t)VK_TRANSFORM_STRIDE * frame->numTransforms );
	calls += GLES_UploadBuffer( &frame->draws, (vkSize_t)VK_DRAW_STRIDE * frame->numDraws );
	calls += GLES_UploadBuffer( &frame->bones, sizeof( float ) * 12 * frame->numBones );
	return calls;
}
