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

// vk_gl.cpp: the OpenGL drawing that is left in the files shared with rd-vanilla
//
// A few places of the shared files draw on their own with glBegin() and glEnd(), with a state they set through
// GL_State() and GL_Bind() and with a modelview matrix they change: the sides of the sky box, the sun, the beam
// and axis surfaces, debug polygons, the automap. vk_hooks.h sends those calls here. Vertices are collected and
// glEnd() draws them the way everything else is drawn. So those files stay as they are.

#include "vk_local.h"

#define VK_GL_MAX_VERTS		6000		// a multiple of 3 and of 4: a full buffer ends on a whole triangle or quad

static vec4_t		gl_xyz[VK_GL_MAX_VERTS];
static vec4_t		gl_normal[VK_GL_MAX_VERTS];			// stays zero
static vec2_t		gl_texCoords[VK_GL_MAX_VERTS][NUM_TEX_COORDS];
static color4ub_t	gl_colors[VK_GL_MAX_VERTS];
static glIndex_t	gl_indexes[VK_GL_MAX_VERTS * 3];

static int			gl_mode;
static int			gl_numVerts;
static int			gl_primStart;			// the first vertex of the primitive between glBegin() and glEnd()
static int			gl_numIndexes;			// of the primitives that wait to be drawn
static float		gl_texCoord[2];
static float		gl_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static uint32_t		gl_stateBits;

// Stencil shadows (tr_shadows.cpp). While the color mask is off, glEnd() does not draw but collects: the volume
// of a model comes as one glBegin() to glEnd() per edge, and it becomes one draw.
static vkStencil_t	gl_stencil;
static qboolean		gl_colorMask = qtrue;
static qboolean		gl_collect;				// the automap is one glBegin() to glEnd() per surface of the level: collected as well
static qboolean		gl_texture = qtrue;		// GL_TEXTURE_2D: off means no texture, which is the white one

// glFog over what is drawn here
static qboolean		gl_fog;
static float		gl_fogDensity;
static float		gl_fogColor[4];

// client arrays
static const float	*gl_vertexArray, *gl_texCoordArray;
static const byte	*gl_colorArray;
static int			gl_vertexStride, gl_texCoordStride, gl_colorStride;
static qboolean		gl_colorArrayOn;

static float		gl_matrix[16];
static qboolean		gl_matrixSet;			// qfalse: the modelview matrix is the one of the entity
static float		gl_savedMatrix[16];
static qboolean		gl_savedMatrixSet;

/*
===============
GL_State

The state of the next glBegin() to glEnd(). Batches of the tess arrays take the state of their shader stage.
===============
*/
void GL_State( uint32_t stateBits ) {
	if ( stateBits != gl_stateBits ) {
		VK_GL_Flush();
	}
	gl_stateBits = stateBits;
}

void GL_Cull( int cullType ) {
}

void GL_CheckErrors( void ) {
}

/*
===============================================================================

MODELVIEW MATRIX

===============================================================================
*/

const float *VK_GL_ModelView( void ) {
	return gl_matrixSet ? gl_matrix : backEnd.ori.modelMatrix;
}

// the back end moved on to another entity or view
void VK_GL_ResetMatrix( void ) {
	gl_matrixSet = qfalse;
}

void VK_GL_PushMatrix( void ) {
	memcpy( gl_savedMatrix, gl_matrix, sizeof( gl_savedMatrix ) );
	gl_savedMatrixSet = gl_matrixSet;
}

void VK_GL_PopMatrix( void ) {
	VK_GL_Flush();
	memcpy( gl_matrix, gl_savedMatrix, sizeof( gl_matrix ) );
	gl_matrixSet = gl_savedMatrixSet;
	VK_TransformChanged();
}

// NULL: identity
void VK_GL_LoadMatrix( const float *m ) {
	VK_GL_Flush();
	if ( m ) {
		memcpy( gl_matrix, m, sizeof( gl_matrix ) );
	} else {
		memset( gl_matrix, 0, sizeof( gl_matrix ) );
		gl_matrix[0] = gl_matrix[5] = gl_matrix[10] = gl_matrix[15] = 1.0f;
	}
	gl_matrixSet = qtrue;
	VK_TransformChanged();
}

void VK_GL_Translate( float x, float y, float z ) {
	VK_GL_Flush();
	if ( !gl_matrixSet ) {
		memcpy( gl_matrix, backEnd.ori.modelMatrix, sizeof( gl_matrix ) );
		gl_matrixSet = qtrue;
	}
	for ( int i = 0; i < 4; i++ ) {
		gl_matrix[12 + i] += gl_matrix[i] * x + gl_matrix[4 + i] * y + gl_matrix[8 + i] * z;
	}
	VK_TransformChanged();
}

void VK_GL_DepthRange( float zNear, float zFar ) {
	VK_SetDepthRange( zNear, zFar );
}

/*
===============================================================================

IMMEDIATE MODE

===============================================================================
*/

void VK_GL_Color( float r, float g, float b, float a ) {
	VectorSet4( gl_color, r, g, b, a );
}

void VK_GL_TexCoord( float s, float t ) {
	gl_texCoord[0] = s;
	gl_texCoord[1] = t;
}

void VK_GL_Begin( int mode ) {
	// room for a primitive of the size the shadow volumes have
	if ( gl_numVerts > VK_GL_MAX_VERTS - 64 ) {
		VK_GL_Flush();
	}
	gl_mode = mode;
	gl_primStart = gl_numVerts;
}

void VK_GL_Vertex( float x, float y, float z ) {
	if ( gl_numVerts == VK_GL_MAX_VERTS ) {
		// the particles of the weather are more than fit: draw what is there and go on
		const int mode = gl_mode;

		if ( mode != GL_TRIANGLES && mode != GL_QUADS ) {
			return;
		}
		VK_GL_End();
		VK_GL_Flush();
		gl_mode = mode;
	}
	VectorSet( gl_xyz[gl_numVerts], x, y, z );
	gl_texCoords[gl_numVerts][0][0] = gl_texCoord[0];
	gl_texCoords[gl_numVerts][0][1] = gl_texCoord[1];
	for ( int i = 0; i < 4; i++ ) {
		gl_colors[gl_numVerts][i] = (byte)( Com_Clamp( 0.0f, 1.0f, gl_color[i] ) * 255.0f );
	}
	gl_numVerts++;
}

/*
===============
VK_GL_End

Triangles out of whatever the vertices were meant as. Lines are not drawn: they are debug output only.
===============
*/
void VK_GL_End( void ) {
	const int first = gl_primStart;
	int numIndexes = gl_numIndexes;

	switch ( gl_mode ) {
	case GL_TRIANGLES:
		for ( int i = first; i + 2 < gl_numVerts; i += 3 ) {
			gl_indexes[numIndexes++] = i;
			gl_indexes[numIndexes++] = i + 1;
			gl_indexes[numIndexes++] = i + 2;
		}
		break;
	case GL_TRIANGLE_STRIP:
		for ( int i = first; i + 2 < gl_numVerts; i++ ) {
			gl_indexes[numIndexes++] = ( ( i - first ) & 1 ) ? i + 1 : i;
			gl_indexes[numIndexes++] = ( ( i - first ) & 1 ) ? i : i + 1;
			gl_indexes[numIndexes++] = i + 2;
		}
		break;
	case GL_TRIANGLE_FAN:
	case GL_POLYGON:
		for ( int i = first + 1; i + 1 < gl_numVerts; i++ ) {
			gl_indexes[numIndexes++] = first;
			gl_indexes[numIndexes++] = i;
			gl_indexes[numIndexes++] = i + 1;
		}
		break;
	case GL_QUADS:
		for ( int i = first; i + 3 < gl_numVerts; i += 4 ) {
			gl_indexes[numIndexes++] = i;
			gl_indexes[numIndexes++] = i + 1;
			gl_indexes[numIndexes++] = i + 2;
			gl_indexes[numIndexes++] = i;
			gl_indexes[numIndexes++] = i + 2;
			gl_indexes[numIndexes++] = i + 3;
		}
		break;
	default:
		break;
	}
	gl_numIndexes = numIndexes;
	gl_primStart = gl_numVerts;
	if ( gl_colorMask && !gl_collect ) {
		VK_GL_Flush();
	}
}

/*
===============
VK_GL_Flush

Draws the primitives that were specified
===============
*/
void VK_GL_Flush( void ) {
	const int numVerts = gl_numVerts, numIndexes = gl_numIndexes;
	const image_t *image = vk.boundImage;
	uint32_t stateBits = gl_stateBits;
	vkDraw_t draw;

	gl_numVerts = gl_primStart = gl_numIndexes = 0;
	if ( !numIndexes || !VK_UploadGeometry( gl_xyz, gl_normal, gl_texCoords, gl_colors, numVerts, gl_indexes, numIndexes ) ) {
		return;
	}

	memset( &draw, 0, sizeof( draw ) );
	draw.transform = VK_CurrentTransform();
	// the texture GL_Bind() chose, if it is one that is on the device
	if ( gl_texture && image && image->texnum < VK_MAX_IMAGES && vk.images[image->texnum].owner == image && vk.images[image->texnum].uploaded ) {
		draw.tex0 = image->texnum;
	} else {
		draw.tex0 = tr.whiteImage->texnum;
	}
	draw.tex1 = draw.tex0;
	draw.flags = (uint32_t)TCGEN_TEXTURE << VKD_TCGEN0_SHIFT;
	draw.vertColor[0] = draw.vertColor[1] = draw.vertColor[2] = draw.vertColor[3] = 1.0f;
	draw.texMat0[0] = draw.texMat0[5] = 1.0f;
	if ( gl_fog ) {
		draw.flags |= 1u << VKD_FOG_SHIFT;
		draw.fogParm[0] = gl_fogDensity;
		VectorCopy( gl_fogColor, draw.fogColor );
	}
	draw.flags |= ( stateBits & GLS_ATEST_BITS ) == GLS_ATEST_GT_0 ? 1u : ( stateBits & GLS_ATEST_BITS ) == GLS_ATEST_LT_80 ? 2u
		: ( stateBits & GLS_ATEST_BITS ) == GLS_ATEST_GE_80 ? 3u : ( stateBits & GLS_ATEST_BITS ) == GLS_ATEST_GE_C0 ? 4u : 0u;
	if ( !gl_colorMask ) {
		// nothing is to reach the color: a blend that leaves what is there does it without another pipeline state
		stateBits = ( stateBits & ~( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) | GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE;
	}
	vk.stencil = ( gl_stencil.test || gl_stencil.depthLess ) ? &gl_stencil : NULL;
	VK_DrawTess( &draw, VKP_GENERIC, stateBits, VK_CULL_MODE_NONE, qfalse );
	vk.stencil = NULL;
}

/*
===============================================================================

STENCIL

What tr_shadows.cpp sets before it draws. A change draws first what was collected with the state before it.

===============================================================================
*/

void VK_GL_StencilFunc( int func, int ref ) {
	VK_GL_Flush();
	gl_stencil.func = func;
	gl_stencil.ref = ref;
}

void VK_GL_StencilOp( int face, int fail, int zfail, int zpass ) {
	VK_GL_Flush();
	for ( int i = 0; i < 2; i++ ) {
		if ( face == GL_FRONT_AND_BACK || face == ( i ? GL_BACK : GL_FRONT ) ) {
			gl_stencil.ops[i][0] = fail;
			gl_stencil.ops[i][1] = zfail;
			gl_stencil.ops[i][2] = zpass;
		}
	}
}

void VK_GL_ColorMask( int on ) {
	VK_GL_Flush();
	gl_colorMask = (qboolean)( on != 0 );
}

void VK_GL_DepthFunc( int func ) {
	VK_GL_Flush();
	gl_stencil.depthLess = (qboolean)( func == GL_LESS );
}

/*
===============================================================================

CLIENT ARRAYS AND FOG

The sprites on surfaces (tr_quicksprite.cpp) hand over arrays of quads and switch glFog on for them

===============================================================================
*/

void VK_GL_Pointer( int which, int stride, const void *data ) {
	switch ( which ) {
	case GL_VERTEX_ARRAY:
		gl_vertexArray = (const float *)data;
		gl_vertexStride = stride ? stride : (int)sizeof( float ) * 3;
		break;
	case GL_TEXTURE_COORD_ARRAY:
		gl_texCoordArray = (const float *)data;
		gl_texCoordStride = stride ? stride : (int)sizeof( float ) * 2;
		break;
	case GL_COLOR_ARRAY:
		gl_colorArray = (const byte *)data;
		gl_colorStride = stride ? stride : 4;
		break;
	}
}

void VK_GL_ClientState( int which, int on ) {
	if ( which == GL_COLOR_ARRAY ) {
		gl_colorArrayOn = (qboolean)( on != 0 );
	}
}

void VK_GL_DrawArrays( int mode, int first, int count ) {
	const float color[4] = { gl_color[0], gl_color[1], gl_color[2], gl_color[3] };

	if ( !gl_vertexArray || !gl_texCoordArray ) {
		return;
	}
	VK_GL_Begin( mode );
	for ( int i = first; i < first + count; i++ ) {
		const float *xyz = (const float *)( (const byte *)gl_vertexArray + (size_t)i * gl_vertexStride );
		const float *st = (const float *)( (const byte *)gl_texCoordArray + (size_t)i * gl_texCoordStride );

		if ( gl_colorArrayOn && gl_colorArray ) {
			const byte *c = gl_colorArray + (size_t)i * gl_colorStride;

			VK_GL_Color( c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, c[3] / 255.0f );
		}
		VK_GL_TexCoord( st[0], st[1] );
		VK_GL_Vertex( xyz[0], xyz[1], xyz[2] );
	}
	VK_GL_End();
	VectorCopy4( color, gl_color );
}

// of everything glEnable() can switch the fog, the stencil test and the texture mean something here
void VK_GL_Enable( int cap, int on ) {
	if ( cap == GL_FOG ) {
		gl_fog = (qboolean)( on != 0 );
	} else if ( cap == GL_STENCIL_TEST ) {
		VK_GL_Flush();
		gl_stencil.test = (qboolean)( on != 0 );
	} else if ( cap == GL_TEXTURE_2D ) {
		VK_GL_Flush();
		gl_texture = (qboolean)( on != 0 );
	}
}

/*
===============
VK_GL_Collect

On: glEnd() does not draw, the primitives wait until the state changes or this is switched off. For code that
draws a great many small primitives with one state, like the automap.
===============
*/
void VK_GL_Collect( qboolean on ) {
	VK_GL_Flush();
	gl_collect = on;
}

void VK_GL_Fogf( int name, float value ) {
	if ( name == GL_FOG_DENSITY ) {
		gl_fogDensity = value;
	}
}

void VK_GL_Fogfv( int name, const float *value ) {
	if ( name == GL_FOG_COLOR ) {
		VectorCopy( value, gl_fogColor );
	}
}
