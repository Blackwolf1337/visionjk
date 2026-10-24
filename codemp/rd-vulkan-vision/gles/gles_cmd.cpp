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


// gles_cmd.cpp: what a frame is for OpenGL ES: it is written down while the shared files build it, and played
// when it ends
//
// vk_frame.cpp and vk_post.cpp say what is drawn and in which order. Every such call becomes a command of fixed
// size in a list, with a copy of what it points at. VK_PresentFrame() sends the buffers of the frame to the device
// (GLES_UploadFrame), plays the list, does the gamma pass and puts the picture into the window. So a buffer of a
// frame is written once a frame and never while a draw that reads it is on its way, and the state of OpenGL ES is
// only set while the list is played: no other code of the renderer has to leave it as it found it.

#include "vk_local.h"

// Every call of OpenGL ES that playing a frame makes is counted, for gfxinfo: in a browser each one is a call from
// the module into JavaScript, and their number is most of what a frame costs there. The names below are the ones
// this file calls while it plays.
static int	gles_calls;

#define glActiveTexture( ... )			( gles_calls++, glActiveTexture( __VA_ARGS__ ) )
#define glBeginQuery( ... )				( gles_calls++, glBeginQuery( __VA_ARGS__ ) )
#define glBindBuffer( ... )				( gles_calls++, glBindBuffer( __VA_ARGS__ ) )
#define glBindBufferRange( ... )		( gles_calls++, glBindBufferRange( __VA_ARGS__ ) )
#define glBindFramebuffer( ... )		( gles_calls++, glBindFramebuffer( __VA_ARGS__ ) )
#define glBindTexture( ... )			( gles_calls++, glBindTexture( __VA_ARGS__ ) )
#define glBindVertexArray( ... )		( gles_calls++, glBindVertexArray( __VA_ARGS__ ) )
#define glBlendFunc( ... )				( gles_calls++, glBlendFunc( __VA_ARGS__ ) )
#define glBlitFramebuffer( ... )		( gles_calls++, glBlitFramebuffer( __VA_ARGS__ ) )
#define glClear( ... )					( gles_calls++, glClear( __VA_ARGS__ ) )
#define glClearColor( ... )				( gles_calls++, glClearColor( __VA_ARGS__ ) )
#define glClearDepthf( ... )			( gles_calls++, glClearDepthf( __VA_ARGS__ ) )
#define glClearStencil( ... )			( gles_calls++, glClearStencil( __VA_ARGS__ ) )
#define glColorMask( ... )				( gles_calls++, glColorMask( __VA_ARGS__ ) )
#define glCopyTexSubImage2D( ... )		( gles_calls++, glCopyTexSubImage2D( __VA_ARGS__ ) )
#define glCullFace( ... )				( gles_calls++, glCullFace( __VA_ARGS__ ) )
#define glDepthFunc( ... )				( gles_calls++, glDepthFunc( __VA_ARGS__ ) )
#define glDepthMask( ... )				( gles_calls++, glDepthMask( __VA_ARGS__ ) )
#define glDepthRangef( ... )			( gles_calls++, glDepthRangef( __VA_ARGS__ ) )
#define glDisable( ... )				( gles_calls++, glDisable( __VA_ARGS__ ) )
#define glDrawArrays( ... )				( gles_calls++, glDrawArrays( __VA_ARGS__ ) )
#define glDrawElements( ... )			( gles_calls++, glDrawElements( __VA_ARGS__ ) )
#define glEnable( ... )					( gles_calls++, glEnable( __VA_ARGS__ ) )
#define glEndQuery( ... )				( gles_calls++, glEndQuery( __VA_ARGS__ ) )
#define glPolygonOffset( ... )			( gles_calls++, glPolygonOffset( __VA_ARGS__ ) )
#define glScissor( ... )				( gles_calls++, glScissor( __VA_ARGS__ ) )
#define glStencilFunc( ... )			( gles_calls++, glStencilFunc( __VA_ARGS__ ) )
#define glStencilMask( ... )			( gles_calls++, glStencilMask( __VA_ARGS__ ) )
#define glStencilOpSeparate( ... )		( gles_calls++, glStencilOpSeparate( __VA_ARGS__ ) )
#define glUniform4fv( ... )				( gles_calls++, glUniform4fv( __VA_ARGS__ ) )
#define glUseProgram( ... )				( gles_calls++, glUseProgram( __VA_ARGS__ ) )
#define glVertexAttribIPointer( ... )	( gles_calls++, glVertexAttribIPointer( __VA_ARGS__ ) )
#define glVertexAttribPointer( ... )	( gles_calls++, glVertexAttribPointer( __VA_ARGS__ ) )
#define glViewport( ... )				( gles_calls++, glViewport( __VA_ARGS__ ) )

#define GLES_MAX_COMMANDS	( VK_MAX_DRAWS + 16384 )	// a draw is one command, the rest is binds, viewports, clears and passes
#define GLES_MAX_RANGES		65536						// runs of indexes of all draws of a frame that have such (surfaces of models)

typedef enum {
	GC_BEGIN_TARGET,
	GC_VIEWPORT,
	GC_CLEAR_DEPTH,
	GC_CLEAR_COLOR,
	GC_VERTEX_BUFFER,
	GC_VERTEX_STREAM,
	GC_INDEX_BUFFER,
	GC_STENCIL,
	GC_DRAW,
	GC_BLUR,
	GC_FOCUS,
	GC_SCOPEPREP,
	GC_SCOPE_SOURCES,
	GC_SCOPE,
	GC_SCREENFXPREP,
	GC_SCREENFX_SOURCES,
	GC_SCREENFX,
	GC_SCREENFXFOG,
	GC_SCREENFXSMOKE,
	GC_ADD,
	GC_COPY,
	GC_PROBES
} glesCommandType_t;

typedef struct glesCommand_s {
	glesCommandType_t	type;
	union {
		struct {
			vkTarget_t	*color, *depth;
			int			colorLoad;
			qboolean	depthLoad;
		} target;
		struct {				// rectangles count from the upper left corner, as the rows of the targets do
			int			rect[4];
			int			scissor[4];
			float		minDepth, maxDepth;
		} viewport;
		struct {
			int			rect[4];
			float		color[4];
		} clear;
		struct {
			GLuint		buffer;
			int			numBindings;
			uint32_t	offsets[4];
		} vertexBuffer;
		struct {
			int			binding;
			GLuint		buffer;
			uint32_t	offset;
		} vertexStream;
		struct {
			GLuint		buffer;
		} indexBuffer;
		vkStencil_t		stencil;
		struct {
			int			program;
			uint32_t	stateBits, state;
			uint32_t	drawIndex;
			uint32_t	first, count;		// numRanges 0: indexes. Else runs in gles_ranges.
			int			numRanges;
		} draw;
		struct {
			vkTarget_t	*source, *target;
			float		parm[4];
		} blur;
		struct {
			vkTarget_t	*source;
			vkAdd_t		how;
		} add;
		struct {				// its numbers are in gles_scopeParm, there is one a frame
			vkTarget_t	*soft, *prep, *mark;
		} scope;
		struct {				// its numbers are in gles_screenFxParm, there is one a frame
			vkTarget_t	*prep, *fog, *smoke;
		} screenFx;
		struct {
			vkTarget_t	*source, *dest;
		} copy;
		struct {
			int			first, count;
		} probes;
	};
} glesCommand_t;

static glesCommand_t	gles_commands[GLES_MAX_COMMANDS];
static int				gles_numCommands;
static float			gles_scopeParm[16];		// what the last VK_ScopePass() was given
static float			gles_screenFxParm[VK_SCREENFX_VEC4S * 4];	// and VK_ScreenFxPass()
static vkIndexRange_t	gles_ranges[GLES_MAX_RANGES];
static int				gles_numRanges;
static qboolean			gles_overflowed;		// this frame has more commands than fit, said once
// what was written down last. Binding it again is left out: most batches follow one of their own kind, and a
// vertex buffer is always bound with the same offsets (the streams of the frame, the level, the models).
static GLuint			gles_recordedVertexBuffer, gles_recordedIndexBuffer;

/*
===============================================================================

WRITING A FRAME DOWN

===============================================================================
*/

// NULL: the list is full, what is not in it is not drawn
static glesCommand_t *GLES_Command( glesCommandType_t type ) {
	glesCommand_t *cmd;

	if ( gles_numCommands == GLES_MAX_COMMANDS ) {
		if ( !gles_overflowed ) {
			gles_overflowed = qtrue;
			ri.Printf( PRINT_DEVELOPER, "GLES_Command: the frame has more commands than fit, the rest is not drawn\n" );
		}
		return NULL;
	}
	cmd = &gles_commands[gles_numCommands++];
	cmd->type = type;
	return cmd;
}

/*
===============
VK_BeginCommands

A frame was played when it ended, so its buffers are free at once. What is still on its way are the answers to
what the last frames asked about their flares: the ones that are there now are taken, an older frame first so
that the newest answer for a flare stays. Nothing waits for one that is not there.
===============
*/
void VK_BeginCommands( vkFrame_t *frame ) {
	gles_numCommands = 0;
	gles_numRanges = 0;
	gles_overflowed = qfalse;
	gles_recordedVertexBuffer = gles_recordedIndexBuffer = 0;

	for ( int i = 1; i <= GLES_PROBE_SETS; i++ ) {
		glesProbeSet_t *set = &frame->probeSets[( frame->probeSet + i ) % GLES_PROBE_SETS];
		GLuint there = GL_FALSE;

		if ( !set->pending || !set->count ) {
			continue;
		}
		// they were asked one after the other: when the last one has its answer, all have
		glGetQueryObjectuiv( set->queries[set->count - 1], GL_QUERY_RESULT_AVAILABLE, &there );
		if ( !there ) {
			continue;
		}
		vk.statFlares = set->count;
		vk.statFlaresSeen = 0;
		for ( int j = 0; j < set->count; j++ ) {
			GLuint seen = 0;

			glGetQueryObjectuiv( set->queries[j], GL_QUERY_RESULT, &seen );
			vk.flareSeen[set->flare[j]] = (byte)( seen != 0 );
			vk.statFlaresSeen += ( seen != 0 );
		}
		set->pending = qfalse;
	}
	// the oldest set is asked with again, whether its answers came or not
	frame->probeSet = ( frame->probeSet + 1 ) % GLES_PROBE_SETS;
	frame->probeSets[frame->probeSet].count = 0;
	frame->probeSets[frame->probeSet].pending = qfalse;
}

void VK_BeginTarget( vkTarget_t *color, vkTarget_t *depth, int colorLoad, qboolean depthLoad ) {
	glesCommand_t *cmd = GLES_Command( GC_BEGIN_TARGET );

	if ( cmd ) {
		cmd->target.color = color;
		cmd->target.depth = depth;
		cmd->target.colorLoad = colorLoad;
		cmd->target.depthLoad = depthLoad;
	}
}

// a target is not drawn into any more when another one is begun
void VK_EndTarget( void ) {
}

// a program has its blocks and samplers where they are from the moment it is linked
void VK_BindFrameSets( void ) {
}

// A rectangle as OpenGL counts it (y from the lower edge of the frame) as the targets have it (rows from the top),
// cut to the frame. qfalse: nothing of it is in the frame.
static qboolean GLES_RowsFromTop( int x, int y, int width, int height, int *out ) {
	const int top = vk.height - ( y + height );
	const int left = Q_max( x, 0 ), up = Q_max( top, 0 );
	const int right = Q_min( x + width, vk.width ), down = Q_min( top + height, vk.height );

	out[0] = left;
	out[1] = up;
	out[2] = Q_max( right - left, 0 );
	out[3] = Q_max( down - up, 0 );
	return (qboolean)( right > left && down > up );
}

void VK_ViewportAndScissor( int x, int y, int width, int height, float minDepth, float maxDepth ) {
	glesCommand_t *cmd = GLES_Command( GC_VIEWPORT );

	if ( cmd ) {
		cmd->viewport.rect[0] = x;
		cmd->viewport.rect[1] = vk.height - ( y + height );
		cmd->viewport.rect[2] = width;
		cmd->viewport.rect[3] = height;
		GLES_RowsFromTop( x, y, width, height, cmd->viewport.scissor );
		cmd->viewport.minDepth = minDepth;
		cmd->viewport.maxDepth = maxDepth;
	}
}

void VK_ClearDepth( int x, int y, int width, int height ) {
	int rect[4];
	glesCommand_t *cmd;

	if ( vk.frame && GLES_RowsFromTop( x, y, width, height, rect ) && ( cmd = GLES_Command( GC_CLEAR_DEPTH ) ) != NULL ) {
		memcpy( cmd->clear.rect, rect, sizeof( rect ) );
	}
}

void VK_ClearColor( int x, int y, int width, int height, const float *color ) {
	int rect[4];
	glesCommand_t *cmd;

	if ( vk.frame && GLES_RowsFromTop( x, y, width, height, rect ) && ( cmd = GLES_Command( GC_CLEAR_COLOR ) ) != NULL ) {
		memcpy( cmd->clear.rect, rect, sizeof( rect ) );
		VectorCopy4( color, cmd->clear.color );
	}
}

void VK_BindVertexBuffer( const vkBuffer_t *buffer, const vkSize_t *offsets, int numBindings ) {
	glesCommand_t *cmd;

	if ( gles_recordedVertexBuffer == buffer->buffer || ( cmd = GLES_Command( GC_VERTEX_BUFFER ) ) == NULL ) {
		return;
	}
	cmd->vertexBuffer.buffer = buffer->buffer;
	cmd->vertexBuffer.numBindings = numBindings;
	for ( int i = 0; i < numBindings; i++ ) {
		cmd->vertexBuffer.offsets[i] = (uint32_t)offsets[i];
	}
	gles_recordedVertexBuffer = buffer->buffer;
	if ( buffer == &vk.frame->vertexes ) {
		// GLES_UploadFrame() sends what the frame put into its streams
		memcpy( vk.frame->streamOffsets, offsets, sizeof( vk.frame->streamOffsets ) );
	}
}

void VK_BindVertexStream( int binding, const vkBuffer_t *buffer, vkSize_t offset ) {
	glesCommand_t *cmd = GLES_Command( GC_VERTEX_STREAM );

	if ( cmd ) {
		cmd->vertexStream.binding = binding;
		cmd->vertexStream.buffer = buffer->buffer;
		cmd->vertexStream.offset = (uint32_t)offset;
	}
}

void VK_BindIndexBuffer( const vkBuffer_t *buffer ) {
	glesCommand_t *cmd;

	if ( gles_recordedIndexBuffer == buffer->buffer || ( cmd = GLES_Command( GC_INDEX_BUFFER ) ) == NULL ) {
		return;
	}
	cmd->indexBuffer.buffer = buffer->buffer;
	gles_recordedIndexBuffer = buffer->buffer;
}

/*
===============
VK_Draw

What the draw differs in from the one before it is found out again when it is played: the passes over whole
targets set state of their own in between.
===============
*/
void VK_Draw( vkProgram_t program, uint32_t stateBits, uint32_t state, uint32_t, uint32_t drawIndex ) {
	glesCommand_t *cmd;

	if ( vk.numRanges && gles_numRanges + vk.numRanges > GLES_MAX_RANGES ) {
		return;
	}
	if ( state & VKS_STENCIL ) {
		// what vk.stencil points at changes with the next thing vk_gl.cpp is told
		if ( ( cmd = GLES_Command( GC_STENCIL ) ) == NULL ) {
			return;
		}
		cmd->stencil = *vk.stencil;
	}
	if ( ( cmd = GLES_Command( GC_DRAW ) ) == NULL ) {
		return;
	}
	cmd->draw.program = program;
	cmd->draw.stateBits = stateBits;
	cmd->draw.state = state;
	cmd->draw.drawIndex = drawIndex;
	cmd->draw.numRanges = vk.numRanges;
	if ( vk.numRanges ) {
		// and so do the runs of a batch of model surfaces
		cmd->draw.first = (uint32_t)gles_numRanges;
		cmd->draw.count = 0;
		memcpy( &gles_ranges[gles_numRanges], vk.ranges, sizeof( gles_ranges[0] ) * vk.numRanges );
		gles_numRanges += vk.numRanges;
	} else {
		// the indexes count from the first vertex of the buffer, see VK_DEVICE_BASE_VERTEX
		cmd->draw.first = vk.firstIndex;
		cmd->draw.count = (uint32_t)vk.numBoundIndexes;
	}
}

void VK_BlurPass( vkTarget_t *source, vkTarget_t *target, const float *parm ) {
	glesCommand_t *cmd = GLES_Command( GC_BLUR );

	if ( cmd ) {
		cmd->blur.source = source;
		cmd->blur.target = target;
		VectorCopy4( parm, cmd->blur.parm );
	}
}

void VK_FocusPass( vkTarget_t *source, vkTarget_t *target, const float *parm ) {
	glesCommand_t *cmd = GLES_Command( GC_FOCUS );

	if ( cmd ) {
		cmd->blur.source = source;
		cmd->blur.target = target;
		VectorCopy4( parm, cmd->blur.parm );
	}
}

void VK_ScopePrepPass( vkTarget_t *source, vkTarget_t *target, const float *parm ) {
	glesCommand_t *cmd = GLES_Command( GC_SCOPEPREP );

	if ( cmd ) {
		cmd->blur.source = source;
		cmd->blur.target = target;
		VectorCopy4( parm, cmd->blur.parm );
	}
}

// The sources are resolved before the frame is begun again: a resolve binds the framebuffer of its target.
void VK_ScopePass( vkTarget_t *soft, vkTarget_t *prep, vkTarget_t *mark, const float *parm ) {
	glesCommand_t *cmd = GLES_Command( GC_SCOPE_SOURCES );

	if ( cmd ) {
		cmd->scope.soft = soft;
		cmd->scope.prep = prep;
		cmd->scope.mark = mark;
		memcpy( gles_scopeParm, parm, sizeof( gles_scopeParm ) );
	}
	VK_BeginTarget( &vk.color, &vk.depth, 2, qtrue );
	GLES_Command( GC_SCOPE );
}

void VK_ScreenFxPrepPass( vkTarget_t *source, vkTarget_t *target, const float *parm ) {
	glesCommand_t *cmd = GLES_Command( GC_SCREENFXPREP );

	if ( cmd ) {
		cmd->blur.source = source;
		cmd->blur.target = target;
		VectorCopy4( parm, cmd->blur.parm );
	}
}

// the numbers of the screen effects, which the mist reads before them
void VK_ScreenFxFogPass( vkTarget_t *target, const float *fx ) {
	glesCommand_t *cmd = GLES_Command( GC_SCREENFXFOG );

	if ( cmd ) {
		cmd->blur.target = target;
		memcpy( gles_screenFxParm, fx, sizeof( gles_screenFxParm ) );
	}
}

// the same numbers as the mist: the frame writes them down once
void VK_ScreenFxSmokePass( vkTarget_t *target, vkTarget_t *last, vkTarget_t *prep, const float *fx ) {
	glesCommand_t *cmd = GLES_Command( GC_SCREENFXSMOKE );

	if ( cmd ) {
		cmd->screenFx.prep = prep;
		cmd->screenFx.fog = target;
		cmd->screenFx.smoke = last;
		memcpy( gles_screenFxParm, fx, sizeof( gles_screenFxParm ) );
	}
}

// as VK_ScopePass()
void VK_ScreenFxPass( vkTarget_t *prep, vkTarget_t *fog, vkTarget_t *smoke, const float *fx ) {
	glesCommand_t *cmd = GLES_Command( GC_SCREENFX_SOURCES );

	if ( cmd ) {
		cmd->screenFx.prep = prep;
		cmd->screenFx.fog = fog;
		cmd->screenFx.smoke = smoke;
		memcpy( gles_screenFxParm, fx, sizeof( gles_screenFxParm ) );
	}
	VK_BeginTarget( &vk.color, &vk.depth, 2, qtrue );
	GLES_Command( GC_SCREENFX );
}

void VK_AddTarget( vkTarget_t *source, vkAdd_t how ) {
	glesCommand_t *cmd = GLES_Command( GC_ADD );

	if ( cmd ) {
		cmd->add.source = source;
		cmd->add.how = how;
	}
}

// (tay_renderScale is not for this device, VK_CreateSceneTargets() says no)
void VK_DrawScene( const int *to ) {
	VK_BeginTarget( &vk.color, &vk.depth, 2, qtrue );
}

void VK_CopyTarget( vkTarget_t *source, vkTarget_t *dest ) {
	glesCommand_t *cmd = GLES_Command( GC_COPY );

	if ( cmd ) {
		cmd->copy.source = source;
		cmd->copy.dest = dest;
	}
}

/*
===============
VK_CopyProbeDepths, VK_ProbeDepth

WebGL reads no depth back. So the device is asked what the shared code wants to know of the depth: is a flare
covered, which is by something more than 24 units in front of it (RB_TestZFlare() of rd-vanilla). A query counts
whether a pixel at the place of the flare and 24 units in front of it is seen, see GLES_Probes(). The shared code
gets the answer as a depth it reads the same thing from: the far end of the depth buffer when the pixel was
seen, the near end when not.
===============
*/
void VK_CopyProbeDepths( int first, int count ) {
	glesCommand_t *cmd = GLES_Command( GC_PROBES );

	if ( cmd ) {
		cmd->probes.first = first;
		cmd->probes.count = count;
	}
}

float VK_ProbeDepth( const vkFrame_t *frame, int probe ) {
	return vk.flareSeen[frame->probe[probe].flare] ? 1.0f : 0.0f;
}

/*
===============================================================================

PLAYING A FRAME

===============================================================================
*/

// What OpenGL ES is set to while a frame is played, so that only what changes is said to it. Nothing of it is
// taken for granted at the start of a frame: textures and buffers were written in between.
static struct {
	GLuint		program;
	int			layout;						// glesLayout_t of the vertex array object that is bound, -1: none of them
	struct {
		GLuint		buffer;					// 0: where its attributes come from is not known
		uint32_t	offsets[4];
		GLuint		indexBuffer;
	} arrays[GLV_NUM_LAYOUTS];				// what the vertex array objects are set to
	GLuint		vertexBuffer;				// what the commands bound: the draws that follow read this
	int			numBindings;
	uint32_t	offsets[4];
	GLuint		indexBuffer;
	GLuint		textures[GLES_UNITS];		// bound to the units, ~0u: not known
	int			unit;
	uint32_t	transform, bones;			// what the blocks are bound to, ~0u: not known
	uint32_t	state;						// VKS_* that is set
	uint32_t	blend;						// the blend bits of GLS_* that are set
	qboolean	stateValid;
	vkStencil_t	stencil;					// of the draws with VKS_STENCIL
	int			viewport[4], scissor[4];	// of the draws: a pass over a whole target sets its own and puts these back
	float		minDepth, maxDepth;
} gles;

static void GLES_Program( glesProgram_t program ) {
	if ( gles.program != vk.programs[program] ) {
		gles.program = vk.programs[program];
		glUseProgram( gles.program );
	}
}

static void GLES_Layout( glesLayout_t layout ) {
	if ( gles.layout != layout ) {
		gles.layout = layout;
		glBindVertexArray( vk.vertexArrays[layout] );
	}
}

static void GLES_Texture( int unit, GLuint texture ) {
	if ( gles.textures[unit] == texture ) {
		return;
	}
	if ( gles.unit != unit ) {
		gles.unit = unit;
		glActiveTexture( GL_TEXTURE0 + unit );
	}
	glBindTexture( GL_TEXTURE_2D, texture );
	gles.textures[unit] = texture;
}

static void GLES_BeginReplay( const vkFrame_t *frame ) {
	memset( &gles, 0, sizeof( gles ) );
	gles.layout = -1;
	memset( gles.textures, 0xff, sizeof( gles.textures ) );
	gles.unit = -1;
	gles.transform = gles.bones = ~0u;
	gles.scissor[2] = gles.viewport[2] = vk.width;
	gles.scissor[3] = gles.viewport[3] = vk.height;
	gles.maxDepth = 1.0f;
	glUseProgram( 0 );
	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	glStencilMask( 0xff );
	glEnable( GL_SCISSOR_TEST );
	glViewport( 0, 0, vk.width, vk.height );
	glScissor( 0, 0, vk.width, vk.height );
	glDepthRangef( 0.0f, 1.0f );
	// the same for every draw of the frame
	glBindBufferRange( GL_UNIFORM_BUFFER, GLES_BLOCK_GLOBALS, frame->globals.buffer, 0, sizeof( vkGlobals_t ) );
}

// the state of a pass over a whole target: nothing is tested, nothing is cut off. With blend 0 nothing is blended.
static void GLES_PassState( int width, int height, GLenum srcBlend, GLenum dstBlend ) {
	glDisable( GL_SCISSOR_TEST );
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );
	glDisable( GL_CULL_FACE );
	glDisable( GL_POLYGON_OFFSET_FILL );
	glDepthMask( GL_FALSE );
	if ( srcBlend ) {
		glEnable( GL_BLEND );
		glBlendFunc( srcBlend, dstBlend );
	} else {
		glDisable( GL_BLEND );
	}
	glViewport( 0, 0, width, height );
	gles.stateValid = qfalse;
}

// back to where the draws of the frame land
static void GLES_DrawViewport( void ) {
	glEnable( GL_SCISSOR_TEST );
	glViewport( gles.viewport[0], gles.viewport[1], gles.viewport[2], gles.viewport[3] );
	glScissor( gles.scissor[0], gles.scissor[1], gles.scissor[2], gles.scissor[3] );
	glDepthRangef( gles.minDepth, gles.maxDepth );
}

/*
===============
GLES_Resolve

The texture of a target gets the average of what was drawn into its renderbuffer of several samples. Called
before something reads the texture. Leaves the framebuffer of the texture bound for drawing.
===============
*/
static void GLES_Resolve( vkTarget_t *target ) {
	GLuint framebuffer;

	if ( !target->unresolved ) {
		return;
	}
	target->unresolved = qfalse;
	framebuffer = VK_TargetFramebuffer( target, NULL );
	glBindFramebuffer( GL_READ_FRAMEBUFFER, target->framebufferSamples );
	glBindFramebuffer( GL_DRAW_FRAMEBUFFER, framebuffer );
	// the scissor cuts a blit too
	glDisable( GL_SCISSOR_TEST );
	glBlitFramebuffer( 0, 0, target->width, target->height, 0, 0, target->width, target->height, GL_COLOR_BUFFER_BIT, GL_NEAREST );
	glEnable( GL_SCISSOR_TEST );
}

/*
===============
GLES_ResolveDepth

The texture of vk.depth gets a sample of every pixel of its renderbuffer of several. Only the depth of field reads
that texture, so this is done when it asks and not with every pass, as GLES_Resolve() is for a color.
===============
*/
static void GLES_ResolveDepth( void ) {
	GLuint samples, plain;

	if ( !vk.depth.renderbuffer ) {
		return;
	}
	samples = VK_TargetFramebuffer( &vk.color, &vk.depth );
	plain = VK_DepthFramebuffer();
	glBindFramebuffer( GL_READ_FRAMEBUFFER, samples );
	glBindFramebuffer( GL_DRAW_FRAMEBUFFER, plain );
	// the scissor cuts a blit too
	glDisable( GL_SCISSOR_TEST );
	glBlitFramebuffer( 0, 0, vk.depth.width, vk.depth.height, 0, 0, vk.depth.width, vk.depth.height, GL_DEPTH_BUFFER_BIT, GL_NEAREST );
	glEnable( GL_SCISSOR_TEST );
}

// one triangle over the target that is bound, with one of the programs of shaders/post.vert
static void GLES_DrawPass( glesProgram_t program, GLuint texture, const float *parm ) {
	GLES_Program( program );
	if ( parm && vk.programParm[program] >= 0 ) {
		glUniform4fv( vk.programParm[program], 1, parm );
	}
	GLES_Texture( 0, texture );
	GLES_Layout( GLV_NONE );
	glDrawArrays( GL_TRIANGLES, 0, 3 );
}

static void GLES_BeginTarget( const glesCommand_t *cmd ) {
	const GLuint framebuffer = VK_TargetFramebuffer( cmd->target.color, cmd->target.depth );
	GLbitfield clear = 0;

	glBindFramebuffer( GL_FRAMEBUFFER, framebuffer );
	if ( framebuffer == cmd->target.color->framebufferSamples ) {
		cmd->target.color->unresolved = qtrue;
	}
	if ( cmd->target.colorLoad == 1 ) {
		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		clear |= GL_COLOR_BUFFER_BIT;
	}
	if ( cmd->target.depth && !cmd->target.depthLoad ) {
		glDepthMask( GL_TRUE );
		glClearDepthf( 1.0f );
		glClearStencil( 0 );
		clear |= GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
		gles.stateValid = qfalse;
	}
	if ( clear ) {
		// all of the target, whatever the draws are cut to
		glDisable( GL_SCISSOR_TEST );
		glClear( clear );
		glEnable( GL_SCISSOR_TEST );
	}
}

static void GLES_Clear( const glesCommand_t *cmd ) {
	glScissor( cmd->clear.rect[0], cmd->clear.rect[1], cmd->clear.rect[2], cmd->clear.rect[3] );
	if ( cmd->type == GC_CLEAR_DEPTH ) {
		glDepthMask( GL_TRUE );
		glClearDepthf( 1.0f );
		glClearStencil( 0 );
		glClear( GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
		gles.stateValid = qfalse;
	} else {
		glClearColor( cmd->clear.color[0], cmd->clear.color[1], cmd->clear.color[2], cmd->clear.color[3] );
		glClear( GL_COLOR_BUFFER_BIT );
	}
	glScissor( gles.scissor[0], gles.scissor[1], gles.scissor[2], gles.scissor[3] );
}

/*
===============
GLES_Vertexes

The attributes of the program of a draw read the buffer the commands bound last. qfalse: there is none, or not
one with the streams this program reads.
===============
*/
static qboolean GLES_Vertexes( glesLayout_t layout ) {
	const int numBindings = ( layout == GLV_GENERIC ) ? 4 : 1;
	const qboolean other = (qboolean)( gles.arrays[layout].buffer != gles.vertexBuffer );
	qboolean bound = qfalse;

	if ( !gles.vertexBuffer || !gles.indexBuffer || gles.numBindings != numBindings ) {
		return qfalse;
	}
	GLES_Layout( layout );
	for ( int i = 0; i < numBindings; i++ ) {
		const uint32_t offset = gles.offsets[i];

		if ( !other && gles.arrays[layout].offsets[i] == offset ) {
			continue;
		}
		if ( !bound ) {
			glBindBuffer( GL_ARRAY_BUFFER, gles.vertexBuffer );
			bound = qtrue;
		}
		gles.arrays[layout].offsets[i] = offset;
		if ( layout == GLV_SKINNED ) {
			// vkSkinVertex_t, as vk_pipeline.cpp describes it to Vulkan. The numbers of the bones are whole numbers
			// for the shader, which OpenGL ES wants to be told with a call of its own.
			const GLsizei stride = sizeof( vkSkinVertex_t );

			glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, stride, (const void *)(size_t)( offset + offsetof( vkSkinVertex_t, xyz ) ) );
			glVertexAttribPointer( 1, 3, GL_FLOAT, GL_FALSE, stride, (const void *)(size_t)( offset + offsetof( vkSkinVertex_t, normal ) ) );
			glVertexAttribPointer( 2, 2, GL_FLOAT, GL_FALSE, stride, (const void *)(size_t)( offset + offsetof( vkSkinVertex_t, st ) ) );
			glVertexAttribIPointer( 3, 4, GL_UNSIGNED_BYTE, stride, (const void *)(size_t)( offset + offsetof( vkSkinVertex_t, bones ) ) );
			glVertexAttribPointer( 4, 4, GL_UNSIGNED_SHORT, GL_TRUE, stride, (const void *)(size_t)( offset + offsetof( vkSkinVertex_t, weights ) ) );
			continue;
		}
		// the arrays of shaderCommands_t, a stream each: xyz, normal, texCoords, vertexColors
		switch ( i ) {
		case 0:
		case 1:
			glVertexAttribPointer( i, 3, GL_FLOAT, GL_FALSE, sizeof( vec4_t ), (const void *)(size_t)offset );
			break;
		case 2:
			for ( int t = 0; t < NUM_TEX_COORDS; t++ ) {
				glVertexAttribPointer( 2 + t, 2, GL_FLOAT, GL_FALSE, sizeof( vec2_t ) * NUM_TEX_COORDS,
					(const void *)(size_t)( offset + sizeof( vec2_t ) * t ) );
			}
			break;
		default:
			glVertexAttribPointer( 7, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof( color4ub_t ), (const void *)(size_t)offset );
			break;
		}
	}
	gles.arrays[layout].buffer = gles.vertexBuffer;
	// which index buffer is bound belongs to the vertex array object
	if ( gles.arrays[layout].indexBuffer != gles.indexBuffer ) {
		gles.arrays[layout].indexBuffer = gles.indexBuffer;
		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, gles.indexBuffer );
	}
	return qtrue;
}

/*
===============
GLES_Probes

For every probe one pixel at the place of its flare, 24 units nearer to the eye than the flare, with the depth
test and without a trace: a query counts whether it got through. The pixel is the triangle of the passes cut down
to it by the scissor, and its depth is where the depth range puts everything when its two ends are the same.
The target of the scene is still bound, with its depth.
===============
*/
static void GLES_Probes( vkFrame_t *frame, const glesCommand_t *cmd ) {
	glesProbeSet_t *set = &frame->probeSets[frame->probeSet];

	glViewport( 0, 0, vk.width, vk.height );
	glEnable( GL_SCISSOR_TEST );
	glEnable( GL_DEPTH_TEST );
	glDepthFunc( GL_LEQUAL );
	glDepthMask( GL_FALSE );
	glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	glDisable( GL_STENCIL_TEST );
	glDisable( GL_CULL_FACE );
	glDisable( GL_BLEND );
	glDisable( GL_POLYGON_OFFSET_FILL );
	gles.stateValid = qfalse;
	GLES_Program( GLP_COPY );
	// the program reads a texture, which one does not matter as long as it is not the one that is drawn into
	GLES_Texture( 0, vk.final.texture );
	GLES_Layout( GLV_NONE );

	for ( int i = cmd->probes.first; i < cmd->probes.first + cmd->probes.count && set->count < VK_MAX_FLARES; i++ ) {
		const vkProbe_t *probe = &frame->probe[i];
		// how far in front of the eye the pixel is, and what the projection of that view makes of it
		const float distance = probe->eyeZ - 24.0f;
		float depth = 0.0f;

		if ( distance > 0.0f ) {
			depth = Com_Clamp( 0.0f, 1.0f, 0.5f + 0.5f * ( probe->projection[2] - probe->projection[0] * distance ) / ( -probe->projection[1] * distance ) );
		}
		set->flare[set->count] = (short)probe->flare;
		glScissor( probe->x, probe->y, 1, 1 );
		glDepthRangef( depth, depth );
		glBeginQuery( GL_ANY_SAMPLES_PASSED, set->queries[set->count] );
		glDrawArrays( GL_TRIANGLES, 0, 3 );
		glEndQuery( GL_ANY_SAMPLES_PASSED );
		set->count++;
	}
	set->pending = qtrue;
	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	GLES_DrawViewport();
}

// the blend function of a .shader stage, as GL_State() of rd-vanilla hands it to OpenGL
static GLenum GLES_SrcBlend( uint32_t stateBits ) {
	switch ( stateBits & GLS_SRCBLEND_BITS ) {
	case GLS_SRCBLEND_ZERO:					return GL_ZERO;
	case GLS_SRCBLEND_DST_COLOR:			return GL_DST_COLOR;
	case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:	return GL_ONE_MINUS_DST_COLOR;
	case GLS_SRCBLEND_SRC_ALPHA:			return GL_SRC_ALPHA;
	case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:	return GL_ONE_MINUS_SRC_ALPHA;
	case GLS_SRCBLEND_DST_ALPHA:			return GL_DST_ALPHA;
	case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:	return GL_ONE_MINUS_DST_ALPHA;
	case GLS_SRCBLEND_ALPHA_SATURATE:		return GL_SRC_ALPHA_SATURATE;
	default:								return GL_ONE;
	}
}

static GLenum GLES_DstBlend( uint32_t stateBits ) {
	switch ( stateBits & GLS_DSTBLEND_BITS ) {
	case GLS_DSTBLEND_ONE:					return GL_ONE;
	case GLS_DSTBLEND_SRC_COLOR:			return GL_SRC_COLOR;
	case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:	return GL_ONE_MINUS_SRC_COLOR;
	case GLS_DSTBLEND_SRC_ALPHA:			return GL_SRC_ALPHA;
	case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:	return GL_ONE_MINUS_SRC_ALPHA;
	case GLS_DSTBLEND_DST_ALPHA:			return GL_DST_ALPHA;
	case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:	return GL_ONE_MINUS_DST_ALPHA;
	default:								return GL_ZERO;
	}
}

static void GLES_Switch( GLenum what, qboolean on ) {
	if ( on ) {
		glEnable( what );
	} else {
		glDisable( what );
	}
}

// what a draw sets beyond its program: the blend function of its state bits and the VKS_* word
static void GLES_DrawState( uint32_t stateBits, uint32_t state ) {
	const uint32_t blend = stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );
	const uint32_t changed = gles.stateValid ? ( state ^ gles.state ) : ~0u;

	if ( !gles.stateValid || blend != gles.blend ) {
		GLES_Switch( GL_BLEND, (qboolean)( blend != 0 ) );
		if ( blend ) {
			glBlendFunc( GLES_SrcBlend( stateBits ), GLES_DstBlend( stateBits ) );
		}
		gles.blend = blend;
	}
	if ( changed & ( 3u << VKS_CULL_SHIFT ) ) {
		const uint32_t cull = ( state >> VKS_CULL_SHIFT ) & 3u;

		GLES_Switch( GL_CULL_FACE, (qboolean)( cull != VKC_NONE ) );
		if ( cull != VKC_NONE ) {
			glCullFace( cull == VKC_FRONT ? GL_FRONT : GL_BACK );
		}
	}
	if ( changed & VKS_DEPTH_TEST ) {
		GLES_Switch( GL_DEPTH_TEST, (qboolean)( ( state & VKS_DEPTH_TEST ) != 0 ) );
	}
	if ( changed & VKS_DEPTH_WRITE ) {
		glDepthMask( ( state & VKS_DEPTH_WRITE ) ? GL_TRUE : GL_FALSE );
	}
	if ( changed & ( VKS_DEPTH_EQUAL | VKS_DEPTH_LESS ) ) {
		glDepthFunc( ( state & VKS_DEPTH_EQUAL ) ? GL_EQUAL : ( ( state & VKS_DEPTH_LESS ) ? GL_LESS : GL_LEQUAL ) );
	}
	if ( changed & VKS_STENCIL ) {
		GLES_Switch( GL_STENCIL_TEST, (qboolean)( ( state & VKS_STENCIL ) != 0 ) );
	}
	if ( state & VKS_STENCIL ) {
		// only the shadows come here: what the test does is set every time. It is in the words of OpenGL already.
		glStencilFunc( gles.stencil.func, gles.stencil.ref, 0xff );
		for ( int face = 0; face < 2; face++ ) {
			glStencilOpSeparate( face ? GL_BACK : GL_FRONT, gles.stencil.ops[face][0], gles.stencil.ops[face][1], gles.stencil.ops[face][2] );
		}
	}
	if ( changed & VKS_OFFSET ) {
		GLES_Switch( GL_POLYGON_OFFSET_FILL, (qboolean)( ( state & VKS_OFFSET ) != 0 ) );
		glPolygonOffset( r_offsetFactor->value, r_offsetUnits->value );
	}
	gles.state = state;
	gles.stateValid = qtrue;
}

static void GLES_Draw( const vkFrame_t *frame, const glesCommand_t *cmd ) {
	const vkDraw_t *draw = (const vkDraw_t *)( frame->draws.mapped + (size_t)VK_DRAW_STRIDE * cmd->draw.drawIndex );
	const GLenum mode = ( cmd->draw.stateBits & VK_GLS_LINES ) ? GL_LINES : GL_TRIANGLES;

	if ( !GLES_Vertexes( cmd->draw.program == GLP_SKINNED ? GLV_SKINNED : GLV_GENERIC ) ) {
		return;
	}
	GLES_Program( (glesProgram_t)cmd->draw.program );
	GLES_DrawState( cmd->draw.stateBits, cmd->draw.state );

	// The blocks of the shaders hold one draw and one transform: the ones of this draw call. WebGL wants a block
	// to lie in its buffer with all it has, which is why the bones are bound in the length of 256 of them.
	glBindBufferRange( GL_UNIFORM_BUFFER, GLES_BLOCK_DRAWS, frame->draws.buffer, (GLintptr)VK_DRAW_STRIDE * cmd->draw.drawIndex, sizeof( vkDraw_t ) );
	if ( gles.transform != draw->transform ) {
		gles.transform = draw->transform;
		glBindBufferRange( GL_UNIFORM_BUFFER, GLES_BLOCK_TRANSFORMS, frame->transforms.buffer, (GLintptr)VK_TRANSFORM_STRIDE * draw->transform,
			sizeof( vkTransform_t ) );
	}
	if ( cmd->draw.program == GLP_SKINNED && gles.bones != draw->bones ) {
		gles.bones = draw->bones;
		glBindBufferRange( GL_UNIFORM_BUFFER, GLES_BLOCK_BONES, frame->bones.buffer, (GLintptr)draw->bones * sizeof( vec4_t ), GLES_BONES_BYTES );
	}
	GLES_Texture( 0, VK_SlotTexture( draw->tex0 ) );
	GLES_Texture( 1, VK_SlotTexture( draw->tex1 ) );

	if ( cmd->draw.numRanges ) {
		// surfaces of models: their triangles are in an index buffer on the device
		for ( int i = 0; i < cmd->draw.numRanges; i++ ) {
			const vkIndexRange_t *range = &gles_ranges[cmd->draw.first + i];

			glDrawElements( mode, (GLsizei)range->count, GL_UNSIGNED_INT, (const void *)( (size_t)range->first * sizeof( glIndex_t ) ) );
		}
	} else {
		glDrawElements( mode, (GLsizei)cmd->draw.count, GL_UNSIGNED_INT, (const void *)( (size_t)cmd->draw.first * sizeof( glIndex_t ) ) );
	}
}

static void GLES_Replay( vkFrame_t *frame ) {
	GLES_BeginReplay( frame );
	for ( int i = 0; i < gles_numCommands; i++ ) {
		const glesCommand_t *cmd = &gles_commands[i];

		switch ( cmd->type ) {
		case GC_BEGIN_TARGET:
			GLES_BeginTarget( cmd );
			break;
		case GC_VIEWPORT:
			memcpy( gles.viewport, cmd->viewport.rect, sizeof( gles.viewport ) );
			memcpy( gles.scissor, cmd->viewport.scissor, sizeof( gles.scissor ) );
			gles.minDepth = cmd->viewport.minDepth;
			gles.maxDepth = cmd->viewport.maxDepth;
			GLES_DrawViewport();
			break;
		case GC_CLEAR_DEPTH:
		case GC_CLEAR_COLOR:
			GLES_Clear( cmd );
			break;
		case GC_VERTEX_BUFFER:
			gles.vertexBuffer = cmd->vertexBuffer.buffer;
			gles.numBindings = cmd->vertexBuffer.numBindings;
			memcpy( gles.offsets, cmd->vertexBuffer.offsets, sizeof( gles.offsets ) );
			break;
		case GC_VERTEX_STREAM:
			gles.offsets[cmd->vertexStream.binding] = cmd->vertexStream.offset;
			break;
		case GC_INDEX_BUFFER:
			gles.indexBuffer = cmd->indexBuffer.buffer;
			break;
		case GC_STENCIL:
			gles.stencil = cmd->stencil;
			break;
		case GC_DRAW:
			GLES_Draw( frame, cmd );
			break;
		case GC_BLUR:
			GLES_Resolve( cmd->blur.source );
			glBindFramebuffer( GL_FRAMEBUFFER, VK_TargetFramebuffer( cmd->blur.target, NULL ) );
			GLES_PassState( cmd->blur.target->width, cmd->blur.target->height, 0, 0 );
			GLES_DrawPass( GLP_BLUR, cmd->blur.source->texture, cmd->blur.parm );
			GLES_DrawViewport();
			break;
		case GC_FOCUS:
		case GC_SCOPEPREP:
		case GC_SCREENFXPREP:
			GLES_Resolve( cmd->blur.source );
			GLES_ResolveDepth();
			glBindFramebuffer( GL_FRAMEBUFFER, VK_TargetFramebuffer( cmd->blur.target, NULL ) );
			GLES_PassState( cmd->blur.target->width, cmd->blur.target->height, 0, 0 );
			GLES_Texture( 1, vk.depth.texture );
			GLES_DrawPass( cmd->type == GC_FOCUS ? GLP_FOCUS : ( cmd->type == GC_SCOPEPREP ? GLP_SCOPEPREP : GLP_SCREENFXPREP ),
				cmd->blur.source->texture, cmd->blur.parm );
			// Not left there: without several samples the scene is drawn into this very texture, and a browser
			// refuses a draw whose program could read what it draws into.
			GLES_Texture( 1, 0 );
			GLES_DrawViewport();
			break;
		case GC_SCOPE_SOURCES:
			GLES_Resolve( cmd->scope.soft );
			GLES_Resolve( cmd->scope.prep );
			GLES_Resolve( cmd->scope.mark );
			GLES_Texture( GLES_UNIT_SOFT, cmd->scope.soft->texture );
			GLES_Texture( GLES_UNIT_PREP, cmd->scope.prep->texture );
			GLES_Texture( GLES_UNIT_MARK, cmd->scope.mark->texture );
			break;
		case GC_SCOPE:
			// onto the frame, which was begun again after the sources were resolved
			GLES_PassState( vk.width, vk.height, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
			GLES_Program( GLP_SCOPE );
			if ( vk.programParm[GLP_SCOPE] >= 0 ) {
				glUniform4fv( vk.programParm[GLP_SCOPE], 4, gles_scopeParm );
			}
			GLES_Texture( 0, vk.screen.texture );
			GLES_Layout( GLV_NONE );
			glDrawArrays( GL_TRIANGLES, 0, 3 );
			// not left there: the mask is the glow buffer, which is drawn into
			GLES_Texture( GLES_UNIT_SOFT, 0 );
			GLES_Texture( GLES_UNIT_PREP, 0 );
			GLES_Texture( GLES_UNIT_MARK, 0 );
			GLES_DrawViewport();
			break;
		case GC_SCREENFX_SOURCES:
			GLES_Resolve( cmd->screenFx.prep );
			GLES_Resolve( cmd->screenFx.fog );
			GLES_Resolve( cmd->screenFx.smoke );
			GLES_Texture( GLES_UNIT_PREP, cmd->screenFx.prep->texture );
			GLES_Texture( GLES_UNIT_FOG, cmd->screenFx.fog->texture );
			GLES_Texture( GLES_UNIT_SMOKE, cmd->screenFx.smoke->texture );
			break;
		case GC_SCREENFXSMOKE:
			// into the new buffer (in fog) from the last one (in smoke)
			GLES_Resolve( cmd->screenFx.smoke );
			GLES_Resolve( cmd->screenFx.prep );
			glBindFramebuffer( GL_FRAMEBUFFER, VK_TargetFramebuffer( cmd->screenFx.fog, NULL ) );
			GLES_PassState( cmd->screenFx.fog->width, cmd->screenFx.fog->height, 0, 0 );
			GLES_Texture( GLES_UNIT_PREP, cmd->screenFx.prep->texture );
			GLES_Program( GLP_SCREENFXSMOKE );
			if ( vk.programParm[GLP_SCREENFXSMOKE] >= 0 ) {
				glUniform4fv( vk.programParm[GLP_SCREENFXSMOKE], VK_SCREENFX_VEC4S, gles_screenFxParm );
			}
			GLES_Texture( 0, cmd->screenFx.smoke->texture );
			GLES_Layout( GLV_NONE );
			glDrawArrays( GL_TRIANGLES, 0, 3 );
			GLES_Texture( GLES_UNIT_PREP, 0 );
			GLES_DrawViewport();
			break;
		case GC_SCREENFXFOG:
			GLES_ResolveDepth();
			glBindFramebuffer( GL_FRAMEBUFFER, VK_TargetFramebuffer( cmd->blur.target, NULL ) );
			GLES_PassState( cmd->blur.target->width, cmd->blur.target->height, 0, 0 );
			GLES_Texture( 1, vk.depth.texture );
			GLES_Program( GLP_SCREENFXFOG );
			if ( vk.programParm[GLP_SCREENFXFOG] >= 0 ) {
				glUniform4fv( vk.programParm[GLP_SCREENFXFOG], VK_SCREENFX_VEC4S, gles_screenFxParm );
			}
			GLES_Layout( GLV_NONE );
			glDrawArrays( GL_TRIANGLES, 0, 3 );
			// not left there, as for the passes that read the depth
			GLES_Texture( 1, 0 );
			GLES_DrawViewport();
			break;
		case GC_SCREENFX:
			GLES_PassState( vk.width, vk.height, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
			GLES_Program( GLP_SCREENFX );
			if ( vk.programParm[GLP_SCREENFX] >= 0 ) {
				glUniform4fv( vk.programParm[GLP_SCREENFX], VK_SCREENFX_VEC4S, gles_screenFxParm );
			}
			GLES_Texture( 0, vk.screen.texture );
			GLES_Layout( GLV_NONE );
			glDrawArrays( GL_TRIANGLES, 0, 3 );
			GLES_Texture( GLES_UNIT_PREP, 0 );
			GLES_Texture( GLES_UNIT_FOG, 0 );
			GLES_Texture( GLES_UNIT_SMOKE, 0 );
			GLES_DrawViewport();
			break;
		case GC_ADD:
			// onto the target that is drawn into, all of it
			if ( cmd->add.how == VKA_BLEND ) {
				GLES_PassState( vk.width, vk.height, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
			} else {
				GLES_PassState( vk.width, vk.height, GL_ONE, cmd->add.how == VKA_ADD_SOFT ? GL_ONE_MINUS_SRC_COLOR : GL_ONE );
			}
			GLES_DrawPass( GLP_COPY, cmd->add.source->texture, NULL );
			GLES_DrawViewport();
			break;
		case GC_COPY:
			// the texture of dest takes the pixels of the framebuffer that is read
			GLES_Resolve( cmd->copy.source );
			glBindFramebuffer( GL_READ_FRAMEBUFFER, VK_TargetFramebuffer( cmd->copy.source, NULL ) );
			GLES_Texture( 0, cmd->copy.dest->texture );
			glCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, cmd->copy.source->width, cmd->copy.source->height );
			break;
		case GC_PROBES:
			GLES_Probes( frame, cmd );
			break;
		}
	}
}

/*
===============
VK_PresentFrame

The buffers of the frame to the device, the commands of the frame, the gamma pass, the picture into the window.
The engine shows the window (ri.WIN_Present in RB_SwapBuffers).
===============
*/
void VK_PresentFrame( const float *gamma ) {
	vkFrame_t *frame = vk.frame;

	gles_calls = GLES_UploadFrame( frame );
	GLES_Replay( frame );

	GLES_Resolve( &vk.color );
	glBindFramebuffer( GL_FRAMEBUFFER, VK_TargetFramebuffer( &vk.final, NULL ) );
	GLES_PassState( vk.width, vk.height, 0, 0 );
	GLES_DrawPass( GLP_GAMMA, vk.color.texture, gamma );
	// The window has the bottom row first and gets the picture turned for it. Its own depth is not used: all of
	// the frame was drawn into the targets.
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	GLES_DrawPass( GLP_PRESENT, vk.final.texture, NULL );

	vk.statCommands = gles_numCommands;
	vk.statCalls = gles_calls;
	if ( v_glesDebug->integer ) {
		// this waits until the device has done all it was told
		for ( GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError() ) {
			if ( vk.statErrors++ < 32 ) {
				ri.Printf( PRINT_ALL, S_COLOR_RED "OpenGL ES: error 0x%04x in a frame of %i commands\n", error, gles_numCommands );
			}
		}
	}
}

/*
===============
VK_ReadFrame

The targets have the top row first, so that is what comes
===============
*/
const byte *VK_ReadFrame( int x, int y, int width, int height ) {
	const vkSize_t frameBytes = (vkSize_t)vk.width * vk.height * 4;

	// the memory stays for the next picture: a video asks for one every frame
	if ( vk.readbackSize < frameBytes ) {
		if ( vk.readback ) {
			Z_Free( vk.readback );
		}
		vk.readback = (byte *)Z_Malloc( (int)frameBytes, TAG_GENERAL, qfalse );
		vk.readbackSize = frameBytes;
	}
	glBindFramebuffer( GL_READ_FRAMEBUFFER, VK_TargetFramebuffer( &vk.final, NULL ) );
	glReadPixels( x, vk.height - ( y + height ), width, height, GL_RGBA, GL_UNSIGNED_BYTE, vk.readback );
	return vk.readback;
}
