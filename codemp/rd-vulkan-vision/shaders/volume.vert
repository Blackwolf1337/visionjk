#version 460

// v_volumetric (vk_post.cpp VK_Volumetric): one triangle over the whole view; the fragment shader marches the haze.

#include "dialect.glsl"

layout( location = 0 ) flat out uint v_draw;

void main() {
	vec2 corner = vec2( float( ( gl_VertexIndex << 1 ) & 2 ), float( gl_VertexIndex & 2 ) );

	gl_Position = vec4( corner * 2.0 - 1.0, 0.0, 1.0 );
	v_draw = uint( gl_InstanceIndex );
}
