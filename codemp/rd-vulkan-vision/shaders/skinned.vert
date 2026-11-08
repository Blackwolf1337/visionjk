#version 460

// A vertex of a Ghoul2 surface: moved by up to four bones of its skeleton, what RB_SurfaceGhoul() of rd-vanilla
// does on the CPU. The position is the weighted sum of the bones, the normal follows the first bone only.

#include "stage.glsl"

#ifdef VK_GLES
// the device binds the place of its bones where the skeleton of the draw starts: 256 bones, which is the most a
// skeleton has (VK_FrameSkeleton() of vk_model.cpp)
uniform Bones {
	vec4	bones[768];
};
#define SKELETON	0u
#else
layout( set = 0, binding = 3, std430 ) readonly buffer Bones {
	vec4	bones[];		// three rows a bone: x' = dot( row.xyz, x ) + row.w
};
#define SKELETON	DRAWS( DRAW ).bones
#endif

#define BONE_ROW( r )	bones[r]
#include "skin.glsl"

layout( location = 0 ) in vec3 a_position;
layout( location = 1 ) in vec3 a_normal;
layout( location = 2 ) in vec2 a_texCoord;
layout( location = 3 ) in uvec4 a_bones;
layout( location = 4 ) in vec4 a_weights;

void main() {
	uint base = SKELETON;
	vec3 position = SkinPosition( base, a_bones, a_weights, a_position );
	// the normal follows the first bone alone
	uint row = base + a_bones[0] * 3u;
	vec3 normal = vec3( dot( bones[row].xyz, a_normal ), dot( bones[row + 1u].xyz, a_normal ), dot( bones[row + 2u].xyz, a_normal ) );
	// bones with no axes: another part of a cut body (stage.glsl stageHidden)
	for ( int i = 0; i < 4; i++ ) {
		uint r = base + a_bones[i] * 3u;

		if ( a_weights[i] > 0.0 && dot( bones[r].xyz, bones[r].xyz ) < 1e-8 && dot( bones[r + 1u].xyz, bones[r + 1u].xyz ) < 1e-8 ) {
			stageHidden += a_weights[i];
		}
	}
	StageVertex( StageInput( position, normal, a_texCoord, vec2( 0.0 ), vec2( 0.0 ), vec2( 0.0 ), vec2( 0.0 ), vec4( 1.0 ) ) );
}
