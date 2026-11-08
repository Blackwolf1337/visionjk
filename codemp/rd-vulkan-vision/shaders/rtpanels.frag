#version 460
#extension GL_EXT_ray_query : require

// v_panelShine where the device traces rays (vk_rt.cpp VK_RT_PanelPass): how much of each of the panels that shine on
// the materials (globals.panels, VK_RT_PickPanels) the point of each pixel sees, nothing of the level or of the
// players between, into the mask (red to alpha: panel 0 to 3, 1 seen). The materials of the next frame read it where
// the point was in this view (generic.frag PanelShine): a lamp shines only where it is seen, not behind a pillar.

layout( set = 0, binding = 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
	vec4	rt[319];
	vec4	lights[34];
	vec4	ssr[7];
	vec4	panels[18];
} globals;
layout( set = 1, binding = 0 ) uniform accelerationStructureEXT u_top;
layout( set = 2, binding = 0 ) uniform sampler2D u_depth;	// the depth of the scene, read pixel for pixel

#define W		globals.rt

layout( location = 0 ) out vec4 o_color;

#include "scene.glsl"

// whether nothing of the level and of the players (masks 1 and 2) is on the way from origin along dir before tMax
bool Clear( vec3 origin, vec3 dir, float tMax ) {
	rayQueryEXT query;

	rayQueryInitializeEXT( query, u_top, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT, 0x03u, origin, 0.5, dir, tMax );
	while ( rayQueryProceedEXT( query ) ) {
	}
	return rayQueryGetIntersectionTypeEXT( query, true ) == gl_RayQueryCommittedIntersectionNoneEXT;
}

void main() {
	vec3 p = ScenePoint( gl_FragCoord.xy );
	vec3 eye = W[4].xyz;
	vec4 seen = vec4( 1.0 );

	if ( distance( p, eye ) > 8192.0 ) {
		o_color = seen;
		return;
	}
	vec3 normal = SceneSurface( gl_FragCoord.xy, p, normalize( eye - p ) );
	vec3 origin = p + normal * 2.0;
	int count = min( int( globals.panels[0].x ), 4 );

	for ( int k = 0; k < count; k++ ) {
		vec4 lamp = globals.panels[6 + k * 3];
		vec3 side = globals.panels[7 + k * 3].xyz;
		vec3 colour = globals.panels[8 + k * 3].rgb;

		if ( dot( colour, colour ) <= 0.0 ) {
			continue;
		}
		// (a little in front of the panel: it may be a part of the level itself)
		vec3 middle = lamp.xyz + ( dot( side, side ) > 0.5 ? side * 2.0 : vec3( 0.0 ) );
		vec3 to = middle - origin;
		float d = length( to );

		if ( d <= lamp.w ) {
			continue;		// on the panel or right by it
		}
		seen[k] = ( dot( normal, to ) > 0.0 && Clear( origin, to / d, d - 1.0 ) ) ? 1.0 : 0.0;
	}
	o_color = seen;
}
