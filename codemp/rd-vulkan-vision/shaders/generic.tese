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
layout( location = 8 ) patch in uint c_borders;

// what generic.frag reads (stage.glsl)
layout( location = 0 ) out vec2 v_texCoord0;
layout( location = 1 ) out vec2 v_texCoord1;
layout( location = 2 ) out vec4 v_color;
layout( location = 3 ) out vec3 v_position;
layout( location = 4 ) out float v_depth;
layout( location = 5 ) flat out uint v_draw;
layout( location = 6 ) noperspective out vec2 v_screenCoord0;
layout( location = 7 ) out vec3 v_normal;

// the passes after the stages (dynamic light, fog, VK_TessPass of vk_shade.cpp) test their depth for EQUAL against
// the tessellated stage, so the same vertex must give the same place in every pipeline
invariant gl_Position;

#define WEIGH( a )	( gl_TessCoord.x * a[0] + gl_TessCoord.y * a[1] + gl_TessCoord.z * a[2] )

void main() {
	v_draw = c_draw[0];
	Draw d = DRAWS( v_draw );
	Transform t = TRANSFORMS( d.transform );

	vec3 position = WEIGH( c_position );
	vec3 normal = WEIGH( c_normal );
	// the texture coordinates of the material: of the first texture, or for a pass after the stages, which needs the
	// first for itself, of the second (tess.w below 0)
	vec2 uv = d.tess.w < 0.0 ? WEIGH( c_texCoord1 ) : WEIGH( c_texCoord0 );

	if ( dot( normal, normal ) > 0.0 ) {
		// The mip level whose texels are about as far apart as the new vertices, so the height does not shimmer. It
		// comes from the point alone, its distance from the eye, with the texels a unit of the default mapping of a
		// level (two): from the texture coordinates of the triangle it differed between neighbours on a curved patch,
		// whose texture is stretched unevenly, and the shared edge got two heights and opened.
		float step = distance( position, t.viewOrigin.xyz ) * max( d.tess.z, 1.0 ) / globals.screen.y;
		float texels = step * 2.0;
		float height = textureLod( u_textures[nonuniformEXT( d.tex2 )], uv, log2( max( texels, 1.0 ) ) ).a;

		// Toward a border of the surface the bumps go down to nothing over about one and a half pieces, so on a border
		// both sides lie on their polygons whatever their normals and cuts (no gap opens). On each edge the fade
		// comes from that edge alone, so the triangle on its other side fades the same way along it: 0 on a border
		// edge, else a ramp from those of its ends that lie on a border (c_borders bits 8 << vertex). Inside the
		// triangle the three are blended so that each counts alone on its edge (weight b_j b_k for the edge across
		// from vertex i, which is 0 on the other two).
		float ramp = 1.5 * step;
		vec3 b = gl_TessCoord;
		float g[3];
		for ( int i = 0; i < 3; i++ ) {
			int j = ( i + 1 ) % 3, k = ( i + 2 ) % 3;

			if ( ( c_borders & ( 1u << uint( i ) ) ) != 0u ) {
				g[i] = 0.0;
				continue;
			}
			float toK = ( b[j] + b[k] > 0.0 ) ? b[k] / ( b[j] + b[k] ) : 0.5;		// how far along from j to k
			float len = distance( c_position[j], c_position[k] );
			float fromJ = ( c_borders & ( 8u << uint( j ) ) ) != 0u ? smoothstep( 0.0, 1.0, toK * len / ramp ) : 1.0;
			float fromK = ( c_borders & ( 8u << uint( k ) ) ) != 0u ? smoothstep( 0.0, 1.0, ( 1.0 - toK ) * len / ramp ) : 1.0;
			g[i] = min( fromJ, fromK );
		}
		vec3 w = vec3( b.y * b.z, b.z * b.x, b.x * b.y );
		float sum = w.x + w.y + w.z;
		float fade;
		if ( sum > 1e-7 ) {
			fade = ( w.x * g[0] + w.y * g[1] + w.z * g[2] ) / sum;
		} else {
			// a corner: the vertex itself
			int c = ( b.x >= b.y && b.x >= b.z ) ? 0 : ( b.y >= b.z ? 1 : 2 );
			fade = ( c_borders & ( 8u << uint( c ) ) ) != 0u ? 0.0 : 1.0;
		}

		position += normalize( normal ) * ( height - d.tess.y ) * d.tess.x * fade;
	}

	gl_Position = t.mvp * vec4( position, 1.0 );
	gl_ClipDistance[0] = ( t.clipPlane.xyz == vec3( 0.0 ) ) ? 1.0
		: dot( ( t.model * vec4( position, 1.0 ) ).xyz, t.clipPlane.xyz ) - t.clipPlane.w;
	v_depth = gl_Position.w;
	// the depth range of OpenGL is -w to w, the one of Vulkan 0 to w
	gl_Position.z = ( gl_Position.z + gl_Position.w ) * 0.5;

	v_position = position;
	v_normal = normal;
	v_texCoord0 = WEIGH( c_texCoord0 );
	v_texCoord1 = WEIGH( c_texCoord1 );
	v_color = WEIGH( c_color );
	v_screenCoord0 = WEIGH( c_screenCoord0 );
}
