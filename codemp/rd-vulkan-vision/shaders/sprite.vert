#version 460

// Sprites, the glow of blades, oriented quads and lines: what RB_SurfaceSprite(), DoSprite(),
// RB_SurfaceOrientedQuad(), RB_SurfaceLine(), RB_SurfaceOrientedLine() and DoLine2() of rd-vanilla build on the
// CPU, four corners each, and the sides of RB_SurfaceCylinder(). A batch of them is a record of six rows a quad in the
// bone buffer, after four rows of the view
// (VK_UploadSpriteBatch() in vk_world.cpp). It has no vertex buffer: the index buffer numbers four vertices a quad,
// and the number of a vertex says which quad and which corner it is.
//
//   view:  forward (w: -1 in a mirror), left, up, the eye
//   quad:  a (w: radius or width at the start), b (w: width at the end), c (w: the kind), s1 t1 s2 t2, color, d
//   kind 0 sprite:         a the middle, b.xy cosine and sine of the turn, the axes of the view
//   kind 1 oriented quad:  a the middle, b and c the vectors to the left and up, as long as they are
//   kind 2 line:           from a to b, wide to both sides across the line of sight (RB_SurfaceLine)
//   kind 3 oriented line:  from a to b, wide to both sides along c (RB_SurfaceOrientedLine, the bolts of
//                          RB_SurfaceElectricity)
//   kind 4 cylinder side:  side a.w of b.w, between the ring of d.xyz around a and the ring of s1 t1 s2 around b,
//                          both turned around the axis c (RB_SurfaceCylinder)
//   kind 5 triangle:       a, a + b and a + c, the texture at 1 0, 0 1 and 0 0 (a weather particle of three
//                          vertices, CWeatherParticleCloud::Render)

#include "stage.glsl"

#ifdef VK_GLES
// the device binds the place of its bones where the records of the batch start, 768 rows (gles_cmd.cpp)
uniform Bones {
	vec4	bones[768];
};
#define SPRITES		0u
#define VERTEX		uint( gl_VertexID )
#else
layout( set = 0, binding = 3, std430 ) readonly buffer Bones {
	vec4	bones[];
};
#define SPRITES		DRAWS( DRAW ).bones
#define VERTEX		uint( gl_VertexIndex )
#endif

void main() {
	uint base = SPRITES;
	uint corner = VERTEX & 3u;
	uint row = base + 4u + ( VERTEX >> 2 ) * 6u;
	vec4 forward = bones[base];
	vec4 a = bones[row];
	vec4 b = bones[row + 1u];
	vec4 c = bones[row + 2u];
	vec4 st = bones[row + 3u];
	vec4 color = bones[row + 4u];
	vec4 d = bones[row + 5u];
	uint kind = uint( c.w );
	// the corners in the order of RB_AddQuadStampExt(): +left +up, -left +up, -left -up, +left -up. A line goes
	// from its start (the first two) to its end, its first corners on the side its vector points to.
	bool first = ( corner == 0u || corner == 3u );
	bool top = ( corner < 2u );
	vec3 position;

	if ( kind == 5u ) {
		// the last corner on the one before it: the second triangle has no area
		vec3 offsets[4] = vec3[4]( vec3( 0.0 ), b.xyz, c.xyz, c.xyz );
		vec2 coords[4] = vec2[4]( vec2( 1.0, 0.0 ), vec2( 0.0, 1.0 ), vec2( 0.0 ), vec2( 0.0 ) );

		StageVertex( StageInput( a.xyz + offsets[corner], -forward.xyz, coords[corner], coords[corner], vec2( 0.0 ), vec2( 0.0 ),
			vec2( 0.0 ), color ) );
		return;
	}
	if ( kind == 4u ) {
		// RotatePointAroundVector() of q_math.c: the corners of this side and of the next one, the upper ring around
		// the origin, the lower one around the end
		uint side = ( corner == 0u || corner == 1u ) ? uint( a.w ) : uint( a.w ) + 1u;
		float segments = b.w;
		float angle = -radians( 360.0 / segments * float( side % uint( segments ) ) );
		float s = sin( angle ), co = cos( angle ), t = 1.0 - co;
		vec3 dir = c.xyz;
		// the rows of rd-vanilla as the columns: point * m is ( dot( point, row0 ), ... )
		mat3 m = mat3( t * dir.x * dir.x + co, t * dir.x * dir.y + s * dir.z, t * dir.x * dir.z - s * dir.y,
			t * dir.x * dir.y - s * dir.z, t * dir.y * dir.y + co, t * dir.y * dir.z + s * dir.x,
			t * dir.x * dir.z + s * dir.y, t * dir.y * dir.z - s * dir.x, t * dir.z * dir.z + co );
		bool upper = ( corner == 0u || corner == 3u );
		vec2 ringTexCoord = vec2( float( side ) / segments, upper ? 1.0 : 0.0 );

		position = upper ? a.xyz + d.xyz * m : b.xyz + st.xyz * m;
		StageVertex( StageInput( position, -forward.xyz, ringTexCoord, ringTexCoord, vec2( 0.0 ), vec2( 0.0 ), vec2( 0.0 ), color ) );
		return;
	}
	if ( kind >= 2u ) {
		vec3 side = c.xyz;

		if ( kind == 2u ) {
			vec3 eye = bones[base + 3u].xyz;

			side = cross( a.xyz - eye, b.xyz - eye );
			side = ( dot( side, side ) > 0.0 ) ? normalize( side ) : vec3( 0.0 );
		}
		position = top ? a.xyz + ( first ? side : -side ) * a.w : b.xyz + ( first ? side : -side ) * b.w;
	} else {
		vec3 left = b.xyz;
		vec3 up = c.xyz;

		if ( kind == 0u ) {
			vec3 axisLeft = bones[base + 1u].xyz;
			vec3 axisUp = bones[base + 2u].xyz;

			// in a mirror the left vector turns round after the turn (RB_SurfaceSprite)
			left = ( b.x * axisLeft - b.y * axisUp ) * ( a.w * forward.w );
			up = ( b.x * axisUp + b.y * axisLeft ) * a.w;
		}
		position = a.xyz + ( first ? left : -left ) + ( top ? up : -up );
	}
	vec2 texCoord = vec2( first ? st.x : st.z, top ? st.y : st.w );

	// both sets of texture coordinates are the same, the normal looks at the eye
	StageVertex( StageInput( position, -forward.xyz, texCoord, texCoord, vec2( 0.0 ), vec2( 0.0 ), vec2( 0.0 ), color ) );
}
