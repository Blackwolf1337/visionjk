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

#define VK_GL_MAX_VERTS		4096

static vec4_t		gl_xyz[VK_GL_MAX_VERTS];
static vec4_t		gl_normal[VK_GL_MAX_VERTS];			// stays zero
static vec2_t		gl_texCoords[VK_GL_MAX_VERTS][NUM_TEX_COORDS];
static color4ub_t	gl_colors[VK_GL_MAX_VERTS];
static glIndex_t	gl_indexes[VK_GL_MAX_VERTS * 3];

static int			gl_mode;
static int			gl_numVerts;
static float		gl_texCoord[2];
static float		gl_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static uint32_t		gl_stateBits;

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
	memcpy( gl_matrix, gl_savedMatrix, sizeof( gl_matrix ) );
	gl_matrixSet = gl_savedMatrixSet;
	VK_TransformChanged();
}

// NULL: identity
void VK_GL_LoadMatrix( const float *m ) {
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
	gl_mode = mode;
	gl_numVerts = 0;
}

void VK_GL_Vertex( float x, float y, float z ) {
	if ( gl_numVerts == VK_GL_MAX_VERTS ) {
		return;
	}
	VectorSet( gl_xyz[gl_numVerts], x, y, z );
	gl_texCoords[gl_numVerts][0][0] = gl_texCoord[0];
	gl_texCoords[gl_numVerts][0][1] = gl_texCoord[1];
	for ( int i = 0; i < 4; i++ ) {
		gl_colors[gl_numVerts][i] = (byte)( Com_Clamp( 0.0f, 1.0f, gl_color[i] ) * 255.0f );
	}
	gl_numVerts++;
}

void VK_GL_End( void ) {
	int numIndexes = 0;
	const image_t *image = vk.boundImage;
	vkDraw_t draw;

	// triangles out of whatever the vertices were meant as. Lines are not drawn: they are debug output only.
	switch ( gl_mode ) {
	case GL_TRIANGLES:
		for ( int i = 0; i + 2 < gl_numVerts; i += 3 ) {
			gl_indexes[numIndexes++] = i;
			gl_indexes[numIndexes++] = i + 1;
			gl_indexes[numIndexes++] = i + 2;
		}
		break;
	case GL_TRIANGLE_STRIP:
		for ( int i = 0; i + 2 < gl_numVerts; i++ ) {
			gl_indexes[numIndexes++] = ( i & 1 ) ? i + 1 : i;
			gl_indexes[numIndexes++] = ( i & 1 ) ? i : i + 1;
			gl_indexes[numIndexes++] = i + 2;
		}
		break;
	case GL_TRIANGLE_FAN:
	case GL_POLYGON:
		for ( int i = 1; i + 1 < gl_numVerts; i++ ) {
			gl_indexes[numIndexes++] = 0;
			gl_indexes[numIndexes++] = i;
			gl_indexes[numIndexes++] = i + 1;
		}
		break;
	case GL_QUADS:
		for ( int i = 0; i + 3 < gl_numVerts; i += 4 ) {
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
	if ( !numIndexes || !VK_UploadGeometry( gl_xyz, gl_normal, gl_texCoords, gl_colors, gl_numVerts, gl_indexes, numIndexes ) ) {
		gl_numVerts = 0;
		return;
	}

	memset( &draw, 0, sizeof( draw ) );
	draw.transform = VK_CurrentTransform();
	// the texture GL_Bind() chose, if it is one that is on the device
	if ( image && image->texnum < VK_MAX_IMAGES && vk.images[image->texnum].owner == image && vk.images[image->texnum].uploaded ) {
		draw.tex0 = image->texnum;
	} else {
		draw.tex0 = tr.whiteImage->texnum;
	}
	draw.tex1 = draw.tex0;
	draw.flags = (uint32_t)TCGEN_TEXTURE << VKD_TCGEN0_SHIFT;
	draw.vertColor[0] = draw.vertColor[1] = draw.vertColor[2] = draw.vertColor[3] = 1.0f;
	draw.texMat0[0] = draw.texMat0[5] = 1.0f;
	VK_DrawTess( &draw, VKP_GENERIC, gl_stateBits, VK_CULL_MODE_NONE, qfalse );
	gl_numVerts = 0;
}
