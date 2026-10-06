#version 460

// A vertex of a Ghoul2 surface: moved by up to four bones of its skeleton, what RB_SurfaceGhoul() of rd-vanilla
// does on the CPU. The position is the weighted sum of the bones, the normal follows the first bone only.

#include "stage.glsl"

layout( set = 0, binding = 3, std430 ) readonly buffer Bones {
	vec4	bones[];		// three rows a bone: x' = dot( row.xyz, x ) + row.w
};

layout( location = 0 ) in vec3 a_position;
layout( location = 1 ) in vec3 a_normal;
layout( location = 2 ) in vec2 a_texCoord;
layout( location = 3 ) in uvec4 a_bones;
layout( location = 4 ) in vec4 a_weights;

void main() {
	uint base = draws[push.draw].bones;
	vec3 position = vec3( 0.0 );
	vec3 normal = vec3( 0.0 );

	for ( int i = 0; i < 4; i++ ) {
		uint row = base + a_bones[i] * 3u;

		if ( a_weights[i] > 0.0 ) {
			position += a_weights[i] * vec3( dot( bones[row].xyz, a_position ) + bones[row].w,
				dot( bones[row + 1u].xyz, a_position ) + bones[row + 1u].w,
				dot( bones[row + 2u].xyz, a_position ) + bones[row + 2u].w );
		}
		if ( i == 0 ) {
			normal = vec3( dot( bones[row].xyz, a_normal ), dot( bones[row + 1u].xyz, a_normal ), dot( bones[row + 2u].xyz, a_normal ) );
		}
	}
	StageVertex( StageInput( position, normal, a_texCoord, vec2( 0.0 ), vec2( 0.0 ), vec2( 0.0 ), vec2( 0.0 ), vec4( 1.0 ) ) );
}
