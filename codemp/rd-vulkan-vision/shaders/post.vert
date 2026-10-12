#version 460

// one triangle over the whole target

#include "dialect.glsl"

VARYING( 0 ) out vec2 v_texCoord;

void main() {
	v_texCoord = vec2( ( VERTEX_INDEX << 1 ) & 2, VERTEX_INDEX & 2 );
	// As far away as there is. Only the pass of the Vision Temple has a depth (temple.frag writes the one of its
	// gold): seen on 2026-10-12 with more than about 400 frames a second, some frames kept the depth of this
	// triangle in the place of the one the shader wrote. That was the nearest there is then, and nothing of the
	// scene was drawn in such a frame. Behind everything a frame like that only loses the depth of that gold.
	gl_Position = vec4( v_texCoord * 2.0 - 1.0, 1.0, 1.0 );
#ifdef POST_UPSIDE_DOWN
	// The copy of the finished frame to the window of the device for OpenGL ES, which has the bottom row first where
	// the targets have the top one. That device compiles this file a second time for it.
	gl_Position.y = -gl_Position.y;
#endif
}
