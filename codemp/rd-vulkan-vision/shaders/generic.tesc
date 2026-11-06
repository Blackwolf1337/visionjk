#version 460

// VKP_TESS, between generic.vert and generic.tese: a stage with a material that has tessellation (tessellation
// <units> [plane] [pixels], tr_shader.cpp). The three vertices of a triangle as generic.vert made them pass on as
// they are; how often each edge is cut follows how long it is on the screen, so that a piece is about tess.z
// pixels long, at most tess.w pieces. An edge gets its number from its two ends alone, so the two triangles that
// share it cut it the same way and no crack opens between them.

#include "common.glsl"

layout( vertices = 3 ) out;

// (gl_in and gl_out as the language declares them: their blocks must match the one generic.vert writes)

// what generic.vert writes (stage.glsl), at the same places
layout( location = 0 ) in vec2 v_texCoord0[];
layout( location = 1 ) in vec2 v_texCoord1[];
layout( location = 2 ) in vec4 v_color[];
layout( location = 3 ) in vec3 v_position[];
layout( location = 4 ) in float v_depth[];		// (made again from the moved vertex in generic.tese)
layout( location = 5 ) in uint v_draw[];
layout( location = 6 ) in vec2 v_screenCoord0[];
layout( location = 7 ) in vec3 v_normal[];

layout( location = 0 ) out vec2 c_texCoord0[];
layout( location = 1 ) out vec2 c_texCoord1[];
layout( location = 2 ) out vec4 c_color[];
layout( location = 3 ) out vec3 c_position[];
layout( location = 5 ) out uint c_draw[];
layout( location = 6 ) out vec2 c_screenCoord0[];
layout( location = 7 ) out vec3 c_normal[];
// which edges of the triangle are borders (VK_TessBorders of vk_world.cpp): bits 1, 2, 4 across from vertex 0, 1, 2
layout( location = 8 ) patch out uint c_borders;

// the floats of VK_TessBorders, four triangles to a row, from draw.bones on
layout( set = 0, binding = 3, std430 ) readonly buffer Bones {
	vec4	bones[];
};

// pieces for the edge from a to b: its length over its distance from the eye is the angle it fills, times the
// pixels the screen has for an angle of one (about its height for the field of view of the game)
float EdgeLevel( Draw d, Transform t, int a, int b ) {
	vec3 middle = ( v_position[a] + v_position[b] ) * 0.5;
	float pixels = distance( v_position[a], v_position[b] ) / max( distance( middle, t.viewOrigin.xyz ), 1.0 ) * globals.screen.y;
	return clamp( pixels / max( d.tess.z, 1.0 ), 1.0, max( d.tess.w, 1.0 ) );
}

void main() {
	gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
	gl_out[gl_InvocationID].gl_ClipDistance[0] = gl_in[gl_InvocationID].gl_ClipDistance[0];
	c_texCoord0[gl_InvocationID] = v_texCoord0[gl_InvocationID];
	c_texCoord1[gl_InvocationID] = v_texCoord1[gl_InvocationID];
	c_color[gl_InvocationID] = v_color[gl_InvocationID];
	c_position[gl_InvocationID] = v_position[gl_InvocationID];
	c_draw[gl_InvocationID] = v_draw[gl_InvocationID];
	c_screenCoord0[gl_InvocationID] = v_screenCoord0[gl_InvocationID];
	c_normal[gl_InvocationID] = v_normal[gl_InvocationID];

	if ( gl_InvocationID == 0 ) {
		Draw d = DRAWS( v_draw[0] );
		Transform t = TRANSFORMS( d.transform );

		c_borders = uint( bones[d.bones + uint( gl_PrimitiveID ) / 4u][gl_PrimitiveID % 4] );

		// outer level i is the edge across from vertex i
		gl_TessLevelOuter[0] = EdgeLevel( d, t, 1, 2 );
		gl_TessLevelOuter[1] = EdgeLevel( d, t, 2, 0 );
		gl_TessLevelOuter[2] = EdgeLevel( d, t, 0, 1 );
		gl_TessLevelInner[0] = max( gl_TessLevelOuter[0], max( gl_TessLevelOuter[1], gl_TessLevelOuter[2] ) );
	}
}
