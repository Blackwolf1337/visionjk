#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

// v_gpuSparks (vk_sparks.cpp VK_SparksDraw): the sparks of the ring the device simulates (shaders/sparks.comp), six
// vertices each (two triangles, no index buffer), drawn with generic.frag over the solid level, added. A spark is a
// streak along the way it flies, as long as the distance it goes in a moment, as wide as its size, from its head back
// to its tail; its colour cools from its hot one through orange and deep red to nothing as its life runs out. Where it
// is dead its triangles have no area. The address of the ring in tex5 and tex6 of the draw (low and high bits).

#include "stage.glsl"

layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer Sparks {
	vec4	rows[];
};

#define VERTEX		uint( gl_VertexIndex )

void main() {
	Draw d = DRAWS( DRAW );
	Transform t = TRANSFORMS( d.transform );
	Sparks sparks = Sparks( uvec2( d.tex5, d.tex6 ) );
	uint id = VERTEX / 6u, corner = VERTEX % 6u;
	vec4 a = sparks.rows[id * 4u], v = sparks.rows[id * 4u + 1u], c = sparks.rows[id * 4u + 2u], m = sparks.rows[id * 4u + 3u];
	// the corners of the two triangles: 0 head left, 1 head right, 2 tail right, 0, 2, 3 tail left
	const uint order[6] = uint[6]( 0u, 1u, 2u, 0u, 2u, 3u );
	uint k = order[corner];

	if ( a.w <= 0.0 ) {
		StageVertex( StageInput( vec3( 0.0 ), vec3( 0.0, 0.0, 1.0 ), vec2( 0.0 ), vec2( 0.0 ), vec2( 0.0 ), vec2( 0.0 ), vec2( 0.0 ), vec4( 0.0 ) ) );
		return;
	}
	float left = clamp( a.w / max( v.w, 0.001 ), 0.0, 1.0 );		// 1 new, 0 dead
	vec3 eye = t.viewOrigin.xyz;
	vec3 toEye = normalize( eye - a.xyz );
	// the streak: the way it went in the last 25 msec, at least as long as it is wide
	vec3 trail = v.xyz * 0.025;
	float len = length( trail );
	float size = c.w * ( 0.5 + 0.5 * left );
	if ( len < size ) {
		trail = ( len > 0.001 ? trail / len : vec3( 0.0, 0.0, 1.0 ) ) * size;
	}
	vec3 side = cross( trail, toEye );
	side = ( dot( side, side ) > 1e-8 ? normalize( side ) : vec3( 1.0, 0.0, 0.0 ) ) * size;
	vec3 head = a.xyz + trail * 0.25, tail = a.xyz - trail;
	vec3 position = ( k < 2u ? head : tail ) + ( k == 0u || k == 3u ? side : -side );
	vec2 texCoord = vec2( k == 0u || k == 3u ? 0.0 : 1.0, k < 2u ? 0.35 : 0.65 );

	// cooling: the hot colour while it is fresh, then orange, deep red, nothing (m.w: how much sooner than its life)
	float heat = pow( left, max( m.w, 0.1 ) );
	vec3 color = mix( vec3( 0.6, 0.08, 0.0 ), c.rgb, smoothstep( 0.1, 0.8, heat ) ) * smoothstep( 0.0, 0.25, heat );
	StageVertex( StageInput( position, toEye, texCoord, texCoord, vec2( 0.0 ), vec2( 0.0 ), vec2( 0.0 ), vec4( color, 1.0 ) ) );
}
