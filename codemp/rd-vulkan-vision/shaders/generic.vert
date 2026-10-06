#version 460

// vertices in the layout of the tess arrays of the back end: geometry that was built on the CPU this frame, and
// the surfaces of the level that lie in a buffer on the device

#include "stage.glsl"

layout( location = 0 ) in vec3 a_position;
layout( location = 1 ) in vec3 a_normal;
layout( location = 2 ) in vec2 a_texCoord;
layout( location = 3 ) in vec2 a_lightmap0;
layout( location = 4 ) in vec2 a_lightmap1;
layout( location = 5 ) in vec2 a_lightmap2;
layout( location = 6 ) in vec2 a_lightmap3;
layout( location = 7 ) in vec4 a_color;

void main() {
	StageVertex( StageInput( a_position, a_normal, a_texCoord, a_lightmap0, a_lightmap1, a_lightmap2, a_lightmap3, a_color ) );
}
