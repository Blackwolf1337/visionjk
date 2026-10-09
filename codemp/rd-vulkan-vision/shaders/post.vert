#version 460

// one triangle over the whole target

#include "dialect.glsl"

VARYING( 0 ) out vec2 v_texCoord;

void main() {
	v_texCoord = vec2( ( VERTEX_INDEX << 1 ) & 2, VERTEX_INDEX & 2 );
	gl_Position = vec4( v_texCoord * 2.0 - 1.0, 0.0, 1.0 );
}
