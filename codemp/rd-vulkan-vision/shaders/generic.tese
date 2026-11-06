#version 460

// VKP_TESS, after generic.tesc: each new vertex gets what the three corners of its triangle have, weighed by where it
// lies in it, and moves along the normal by the height in the alpha of the normal map (tex2, the same height the
// parallax reads): (height - tess.y) * tess.x units, so tess.y 0 makes the surface rise out of the polygon. Then it
// goes to the screen as generic.vert sends a vertex (stage.glsl StageVertex). The fragment shader takes the normal
// of the polygon for its tangent frame, which the move along that normal does not tilt, so the bumps of the normal
// map are not doubled.

#include "common.glsl"

// with the origin of the domain at the lower left, as in OpenGL, ccw keeps the winding of the triangle
// (VkPipelineTessellationDomainOriginStateCreateInfo in vk_pipeline.cpp)
layout( triangles, fractional_odd_spacing, ccw ) in;

BINDING( 1, 0 ) uniform sampler2D u_textures[];

layout( location = 0 ) in vec2 c_texCoord0[];
layout( location = 1 ) in vec2 c_texCoord1[];
layout( location = 2 ) in vec4 c_color[];
layout( location = 3 ) in vec3 c_position[];
layout( location = 5 ) in uint c_draw[];
layout( location = 6 ) in vec2 c_screenCoord0[];
layout( location = 7 ) in vec3 c_normal[];

// what generic.frag reads (stage.glsl)
layout( location = 0 ) out vec2 v_texCoord0;
layout( location = 1 ) out vec2 v_texCoord1;
layout( location = 2 ) out vec4 v_color;
layout( location = 3 ) out vec3 v_position;
layout( location = 4 ) out float v_depth;
layout( location = 5 ) flat out uint v_draw;
layout( location = 6 ) noperspective out vec2 v_screenCoord0;
layout( location = 7 ) out vec3 v_normal;

#define WEIGH( a )	( gl_TessCoord.x * a[0] + gl_TessCoord.y * a[1] + gl_TessCoord.z * a[2] )

void main() {
	v_draw = c_draw[0];
	Draw d = DRAWS( v_draw );
	Transform t = TRANSFORMS( d.transform );

	vec3 position = WEIGH( c_position );
	vec3 normal = WEIGH( c_normal );
	vec2 uv = WEIGH( c_texCoord0 );

	if ( dot( normal, normal ) > 0.0 ) {
		// The mip level whose texels are about as far apart as the new vertices, so the height does not shimmer. It
		// comes from the point alone (its distance from the eye, the texels a unit of the mapping), not from the
		// triangle, so that two triangles that share an edge read the same height on it.
		vec2 size = vec2( textureSize( u_textures[nonuniformEXT( d.tex2 )], 0 ) );
		float uvPerUnit = max( max( distance( c_texCoord0[0], c_texCoord0[1] ) / max( distance( c_position[0], c_position[1] ), 1e-3 ),
			distance( c_texCoord0[1], c_texCoord0[2] ) / max( distance( c_position[1], c_position[2] ), 1e-3 ) ),
			distance( c_texCoord0[2], c_texCoord0[0] ) / max( distance( c_position[2], c_position[0] ), 1e-3 ) );
		float step = distance( position, t.viewOrigin.xyz ) * max( d.tess.z, 1.0 ) / globals.screen.y;
		float texels = uvPerUnit * step * max( size.x, size.y );
		float height = textureLod( u_textures[nonuniformEXT( d.tex2 )], uv, log2( max( texels, 1.0 ) ) ).a;

		position += normalize( normal ) * ( height - d.tess.y ) * d.tess.x;
	}

	gl_Position = t.mvp * vec4( position, 1.0 );
	gl_ClipDistance[0] = ( t.clipPlane.xyz == vec3( 0.0 ) ) ? 1.0
		: dot( ( t.model * vec4( position, 1.0 ) ).xyz, t.clipPlane.xyz ) - t.clipPlane.w;
	v_depth = gl_Position.w;
	// the depth range of OpenGL is -w to w, the one of Vulkan 0 to w
	gl_Position.z = ( gl_Position.z + gl_Position.w ) * 0.5;

	v_position = position;
	v_normal = normal;
	v_texCoord0 = uv;
	v_texCoord1 = WEIGH( c_texCoord1 );
	v_color = WEIGH( c_color );
	v_screenCoord0 = WEIGH( c_screenCoord0 );
}
