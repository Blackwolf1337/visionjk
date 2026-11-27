#version 460

// v_glassDebug 1 (vk_glass.cpp VK_GlassDebugDraw): how thick glass gets what it shows, in colours over the view of the
// level. u_solid: how far everything solid is, taken before the glass was drawn; u_glass: the nearest of that and the
// glass. Blue: a face of glass. A yellow frame: the part of the view a view of what lies behind a plane of the glass
// was drawn for (globals.glass, R_GlassViews of tr_main.cpp). Red inside it: what stands in front of that plane, where
// the glass reads that view in place of its copy of the frame (generic.frag GlassBehind).

layout( set = 0, binding = 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
	vec4	rt[382];
	vec4	lights[34];
	vec4	ssr[7];
	vec4	glass[10];		// the views of what lies behind thick glass (VK_GlassRows of vk_post.cpp, VK_GLASS_VEC4S)
	vec4	blades[247];
	vec4	smoke[357];
	vec4	ripple[256];
	vec4	glassDebug[12];	// the view (VK_ViewRows), then x: 1 the distance of the solid level in bands
} globals;
layout( set = 1, binding = 0 ) uniform sampler2D u_solid;
layout( set = 2, binding = 0 ) uniform sampler2D u_glass;
layout( set = 3, binding = 0 ) uniform sampler2D u_depth;

layout( location = 0 ) out vec4 o_color;

#define W globals.glassDebug
#include "scene.glsl"

void main() {
	ivec2 at = ivec2( gl_FragCoord.xy );
	float solid = texelFetch( u_solid, at, 0 ).r;
	float glass = texelFetch( u_glass, at, 0 ).r;
	float slack = 1.0 + glass * 0.01;

	// (x 1: the distance of the solid level, in bands of 32 units)
	if ( globals.glassDebug[11].x > 0.5 ) {
		o_color = vec4( vec3( fract( solid / 32.0 ) ), 1.0 );
		return;
	}

	vec2 uv = gl_FragCoord.xy / globals.screen.xy, pixel = 1.0 / globals.screen.xy;
	vec3 ray = SceneRay( gl_FragCoord.xy );
	int count = min( int( globals.glass[0].x ), 3 );

	for ( int i = 0; i < count; i++ ) {
		vec4 plane = globals.glass[1 + i * 3], part = globals.glass[2 + i * 3];

		if ( globals.glass[3 + i * 3].x < 0.5 || uv.x < part.x || uv.y < part.y || uv.x > part.z || uv.y > part.w ) {
			continue;
		}
		if ( uv.x < part.x + pixel.x * 3.0 || uv.y < part.y + pixel.y * 3.0 || uv.x > part.z - pixel.x * 3.0 || uv.y > part.w - pixel.y * 3.0 ) {
			o_color = vec4( 1.0, 0.85, 0.1, 0.9 );
			return;
		}
		// how far along the ray of the pixel the plane is
		float along = dot( plane.xyz, ray );
		float to = abs( along ) > 1e-6 ? ( plane.w - dot( plane.xyz, W[4].xyz ) ) / along : -1.0;

		if ( to > 0.0 && solid < to - 2.0 ) {
			o_color = vec4( 1.0, 0.15, 0.1, 0.45 );
			return;
		}
	}
	if ( glass < solid - slack ) {
		o_color = vec4( 0.15, 0.4, 1.0, 0.45 );
	} else {
		discard;
	}
}
