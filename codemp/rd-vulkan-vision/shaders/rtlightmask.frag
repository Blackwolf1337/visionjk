#version 460
#extension GL_EXT_ray_query : require

// The shadows from the moving lights where the device traces rays (vk_rt.cpp): what shaders/smlights.frag writes
// (the players from the cubes of the shadow maps), and the level too, by a ray from each point to the light against
// it (shaders/lightmask.glsl Taken). The shadows of the level from the lights of the map are in the lightmap
// already; those of a saber, a shot or an explosion are not, and their light went through walls.

#define LEVEL_RAYS

layout( set = 1, binding = 0 ) uniform accelerationStructureEXT u_top;

// whether the level (mask 1) or a door, lift or other brush model where it is now (mask 4) is in the way from origin
// along dir before tMax
bool LevelBlocks( vec3 origin, vec3 dir, float tMax ) {
	rayQueryEXT query;

	rayQueryInitializeEXT( query, u_top, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT, 0x05u, origin, 0.5, dir, tMax );
	while ( rayQueryProceedEXT( query ) ) {
	}
	return rayQueryGetIntersectionTypeEXT( query, true ) != gl_RayQueryCommittedIntersectionNoneEXT;
}

// how far along dir from origin the level is (from 0.5 on), -1 for nothing before tMax (scene.glsl OnLevel)
float LevelHitAt( vec3 origin, vec3 dir, float tMax ) {
	rayQueryEXT query;

	rayQueryInitializeEXT( query, u_top, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT, 0x01u, origin, 0.5, dir, tMax );
	while ( rayQueryProceedEXT( query ) ) {
	}
	return rayQueryGetIntersectionTypeEXT( query, true ) != gl_RayQueryCommittedIntersectionNoneEXT
		? rayQueryGetIntersectionTEXT( query, true ) : -1.0;
}

#include "lightmask.glsl"
