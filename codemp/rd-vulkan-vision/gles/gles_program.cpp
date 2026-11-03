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


// gles_program.cpp: the shaders as programs of OpenGL ES
//
// The sources are the ones of the Vulkan device (shaders/). The build makes one text of each with its includes
// put in (cmake/Modules/BundleShader.cmake), and here gles_prefix.glsl goes in front, which says GLSL ES 3.00 and
// defines VK_GLES. GLSL ES has no bindings in the source: where a uniform block is bound and which texture unit a
// sampler reads is set here by name, once a program is linked.

#include "vk_local.h"

// the texts, the build writes them as arrays with a zero at their end
#include "shaders/prefix_es.inc"
#include "shaders/generic_vert_es.inc"
#include "shaders/skinned_vert_es.inc"
#include "shaders/sprite_vert_es.inc"
#include "shaders/grass_vert_es.inc"
#include "shaders/liquid_vert_es.inc"
#include "shaders/generic_frag_es.inc"
#include "shaders/liquid_frag_es.inc"
#include "shaders/post_vert_es.inc"
#include "shaders/gamma_frag_es.inc"
#include "shaders/blur_frag_es.inc"
#include "shaders/copy_frag_es.inc"
#include "shaders/focus_frag_es.inc"
#include "shaders/scopeprep_frag_es.inc"
#include "shaders/scope_frag_es.inc"
#include "shaders/screenfxprep_frag_es.inc"
#include "shaders/screenfx_frag_es.inc"
#include "shaders/screenfxfog_frag_es.inc"
#include "shaders/screenfxsmoke_frag_es.inc"

static const struct {
	const char			*name;				// for what the console says when it does not compile
	const unsigned char	*vertex, *fragment;
	const char			*defines;			// between the prefix and the text of the vertex shader
} gles_programs[GLP_NUM_PROGRAMS] = {
	{ "generic", vk_es_generic_vert, vk_es_generic_frag, "" },
	{ "skinned", vk_es_skinned_vert, vk_es_generic_frag, "" },
	{ "sprite", vk_es_sprite_vert, vk_es_generic_frag, "" },
	{ "grass", vk_es_grass_vert, vk_es_generic_frag, "" },
	{ "liquid", vk_es_liquid_vert, vk_es_liquid_frag, "" },
	{ "gamma", vk_es_post_vert, vk_es_gamma_frag, "" },
	{ "blur", vk_es_post_vert, vk_es_blur_frag, "" },
	{ "copy", vk_es_post_vert, vk_es_copy_frag, "" },
	{ "focus", vk_es_post_vert, vk_es_focus_frag, "" },
	{ "scopeprep", vk_es_post_vert, vk_es_scopeprep_frag, "" },
	{ "scope", vk_es_post_vert, vk_es_scope_frag, "" },
	{ "screenfxprep", vk_es_post_vert, vk_es_screenfxprep_frag, "" },
	{ "screenfx", vk_es_post_vert, vk_es_screenfx_frag, "" },
	{ "screenfxfog", vk_es_post_vert, vk_es_screenfxfog_frag, "" },
	{ "screenfxsmoke", vk_es_post_vert, vk_es_screenfxsmoke_frag, "" },
	{ "present", vk_es_post_vert, vk_es_copy_frag, "#define POST_UPSIDE_DOWN\n" },
};

// What the compiler or the linker of the device had to say, line by line: in a browser the console of the engine
// is the one place it can be read.
static void VK_PrintLog( const char *what, const char *name, char *log ) {
	ri.Printf( PRINT_ALL, S_COLOR_RED "OpenGL ES: %s %s:\n", what, name );
	for ( char *line = log; line && *line; ) {
		char *end = strchr( line, '\n' );

		if ( end ) {
			*end = 0;
		}
		ri.Printf( PRINT_ALL, "%s\n", line );
		line = end ? end + 1 : NULL;
	}
}

static GLuint VK_CompileShader( GLenum stage, const char *name, const char *defines, const unsigned char *text ) {
	const GLchar *strings[3] = { (const GLchar *)vk_es_prefix, defines, (const GLchar *)text };
	const GLuint shader = glCreateShader( stage );
	GLint compiled = GL_FALSE;

	glShaderSource( shader, 3, strings, NULL );
	glCompileShader( shader );
	glGetShaderiv( shader, GL_COMPILE_STATUS, &compiled );
	if ( !compiled ) {
		static char log[8192];

		log[0] = 0;
		glGetShaderInfoLog( shader, sizeof( log ), NULL, log );
		VK_PrintLog( stage == GL_VERTEX_SHADER ? "the vertex shader of program" : "the fragment shader of program", name, log );
		glDeleteShader( shader );
		return 0;
	}
	return shader;
}

static void VK_BindBlock( GLuint program, const char *name, GLuint binding ) {
	const GLuint index = glGetUniformBlockIndex( program, name );

	// a block the program does not have has no index
	if ( index != GL_INVALID_INDEX ) {
		glUniformBlockBinding( program, index, binding );
	}
}

static void VK_BindSampler( GLuint program, const char *name, GLint unit ) {
	const GLint location = glGetUniformLocation( program, name );

	if ( location >= 0 ) {
		glUniform1i( location, unit );
	}
}

static GLuint VK_LinkProgram( glesProgram_t which ) {
	const GLuint vertex = VK_CompileShader( GL_VERTEX_SHADER, gles_programs[which].name, gles_programs[which].defines, gles_programs[which].vertex );
	const GLuint fragment = VK_CompileShader( GL_FRAGMENT_SHADER, gles_programs[which].name, "", gles_programs[which].fragment );
	GLuint program = 0;
	GLint linked = GL_FALSE;

	if ( vertex && fragment ) {
		program = glCreateProgram();
		glAttachShader( program, vertex );
		glAttachShader( program, fragment );
		glLinkProgram( program );
		glGetProgramiv( program, GL_LINK_STATUS, &linked );
		if ( !linked ) {
			static char log[8192];

			log[0] = 0;
			glGetProgramInfoLog( program, sizeof( log ), NULL, log );
			VK_PrintLog( "linking program", gles_programs[which].name, log );
			glDeleteProgram( program );
			program = 0;
		}
	}
	// a linked program keeps what it was made of
	if ( vertex ) {
		glDeleteShader( vertex );
	}
	if ( fragment ) {
		glDeleteShader( fragment );
	}
	if ( !program ) {
		return 0;
	}

	glUseProgram( program );
	VK_BindBlock( program, "Globals", GLES_BLOCK_GLOBALS );
	VK_BindBlock( program, "Transforms", GLES_BLOCK_TRANSFORMS );
	VK_BindBlock( program, "Draws", GLES_BLOCK_DRAWS );
	VK_BindBlock( program, "Bones", GLES_BLOCK_BONES );
	VK_BindSampler( program, "u_tex0", 0 );
	VK_BindSampler( program, "u_tex1", 1 );
	VK_BindSampler( program, "u_tex2", GLES_UNIT_TEX2 );
	VK_BindSampler( program, "u_tex3", GLES_UNIT_TEX3 );
	VK_BindSampler( program, "u_tex4", GLES_UNIT_TEX4 );
	VK_BindSampler( program, "u_image", 0 );
	VK_BindSampler( program, "u_depth", 1 );
	VK_BindSampler( program, "u_soft", GLES_UNIT_SOFT );
	VK_BindSampler( program, "u_prep", GLES_UNIT_PREP );
	VK_BindSampler( program, "u_mark", GLES_UNIT_MARK );
	VK_BindSampler( program, "u_fog", GLES_UNIT_FOG );
	VK_BindSampler( program, "u_smoke", GLES_UNIT_SMOKE );
	vk.programParm[which] = glGetUniformLocation( program, "u_parm" );
	return program;
}

/*
===============
VK_InitPrograms

qfalse: a shader did not compile or a program did not link, the console has what the device said about it
===============
*/
qboolean VK_InitPrograms( void ) {
	for ( int i = 0; i < GLP_NUM_PROGRAMS; i++ ) {
		vk.programs[i] = VK_LinkProgram( (glesProgram_t)i );
		if ( !vk.programs[i] ) {
			return qfalse;
		}
	}
	glUseProgram( 0 );

	// Which attributes a draw has is fixed with its program: the eight of shaders/generic.vert, the five of
	// skinned.vert. Where they come from is set when a draw is played (gles_cmd.cpp). The passes over a whole target
	// have no vertices at all, and WebGL wants every attribute that is switched on to have a buffer.
	glGenVertexArrays( GLV_NUM_LAYOUTS, vk.vertexArrays );
	glBindVertexArray( vk.vertexArrays[GLV_GENERIC] );
	for ( GLuint i = 0; i < 8; i++ ) {
		glEnableVertexAttribArray( i );
	}
	glBindVertexArray( vk.vertexArrays[GLV_SKINNED] );
	for ( GLuint i = 0; i < 5; i++ ) {
		glEnableVertexAttribArray( i );
	}
	glBindVertexArray( vk.vertexArrays[GLV_GRASS] );
	for ( GLuint i = 0; i < 3; i++ ) {
		glEnableVertexAttribArray( i );
	}
	glBindVertexArray( 0 );
	return qtrue;
}

void VK_ShutdownPrograms( void ) {
	for ( int i = 0; i < GLP_NUM_PROGRAMS; i++ ) {
		if ( vk.programs[i] ) {
			glDeleteProgram( vk.programs[i] );
			vk.programs[i] = 0;
		}
	}
	if ( vk.vertexArrays[0] ) {
		glDeleteVertexArrays( GLV_NUM_LAYOUTS, vk.vertexArrays );
		memset( vk.vertexArrays, 0, sizeof( vk.vertexArrays ) );
	}
}
