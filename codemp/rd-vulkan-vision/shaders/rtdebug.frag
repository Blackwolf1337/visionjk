#version 460
#extension GL_EXT_ray_query : require

// v_rtDebug (vk_rt.cpp): what the rays see, over the view of the level. The numbers are the rt ones of the globals:
// the view as the wet places have it (rows 0-8), then row 11: the direction to the sun, w the mode.
//   1: a ray from each pixel towards the sun. White where it gets out, dark where the level is in the way: outdoors
//      that has to be the shadow in the lightmaps.
//   2: a ray from the eye through each pixel against the depth of the scene: black where they agree, red where the
//      ray stops in front of the depth (something in the structure that is not drawn), green where it goes past it
//      (something drawn that is not in the structure: what moves, glass, grates, the sky).
//   3: the rays towards the sun against the players alone (mask 2): dark where a player is in the way, their sun
//      shadows, also on themselves.
//   4: the lights of the level (the fixtures that lit the light grid, VK_RT_LightsFromCells) as yellow dots over the
//      scene, larger the stronger they are, dim where something is in front of them (their middle is often inside
//      the fixture).

layout( set = 0, binding = 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
	vec4	rt[16];
} globals;
layout( set = 1, binding = 0 ) uniform accelerationStructureEXT u_top;
layout( set = 1, binding = 2, std430 ) readonly buffer Lights {
	vec4	lights[];
};
layout( set = 2, binding = 0 ) uniform sampler2D u_depth;	// the depth of the scene, read pixel for pixel

#define W	globals.rt

layout( location = 0 ) out vec4 o_color;

#include "scene.glsl"

// the distance to the first triangle of what mask selects along the ray (1 the level, 2 the players), or tMax
float Trace( vec3 origin, vec3 dir, float tMax, uint mask ) {
	rayQueryEXT query;

	rayQueryInitializeEXT( query, u_top, gl_RayFlagsOpaqueEXT, mask, origin, 0.0, dir, tMax );
	while ( rayQueryProceedEXT( query ) ) {
	}
	if ( rayQueryGetIntersectionTypeEXT( query, true ) == gl_RayQueryCommittedIntersectionNoneEXT ) {
		return tMax;
	}
	return rayQueryGetIntersectionTEXT( query, true );
}

void main() {
	vec3 p = ScenePoint( gl_FragCoord.xy );
	int mode = int( W[11].w );

	if ( mode == 4 ) {
		vec3 eye = W[4].xyz;
		vec3 dir = normalize( p - eye );
		float depth = distance( p, eye );

		for ( int i = 0; i < int( W[15].z ); i++ ) {
			vec3 to = lights[i * 3].xyz - eye;
			float t = dot( to, dir );
			float size = 3.0 + 0.004 * t + log2( 1.0 + lights[i * 3 + 1].x ) * 0.25;

			if ( t > 0.0 && length( to - dir * t ) < size ) {
				o_color = t < depth + size ? vec4( 1.0, 0.85, 0.2, 1.0 ) : vec4( 0.5, 0.35, 0.05, 1.0 );
				return;
			}
		}
		discard;
	} else if ( mode == 1 || mode == 3 ) {
		vec3 sun = normalize( W[11].xyz );
		vec3 normal = SceneSurface( gl_FragCoord.xy, p, sun );
		// off the surface a little, so the ray does not hit the triangle it starts on
		float t = Trace( p + normal * 1.0, sun, 65536.0, mode == 1 ? 0x01u : 0x02u );

		o_color = vec4( vec3( t >= 65536.0 ? 1.0 : 0.12 ), 1.0 );
	} else {
		vec3 eye = W[4].xyz;
		float depth = distance( p, eye );
		float t = Trace( eye, normalize( p - eye ), depth + 64.0, 0x01u );
		float diff = clamp( ( depth - t ) / 16.0, -1.0, 1.0 );

		o_color = vec4( max( diff, 0.0 ), max( -diff, 0.0 ), 0.0, 1.0 );
	}
}
