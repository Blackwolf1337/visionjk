#version 460

// The wet places of RE_AddWet (wetSpot_t, VK_DrawWet() in vk_post.cpp), a quad each over the part of the view where
// the ball around its place is: shaders/wet.frag finds the level under every pixel of it from the depth. The
// numbers are the wet ones of the globals of the frame; the number of the instance says which place, the number of
// the vertex which corner, there is no vertex buffer. A place the eye is near or in gets the whole view.
//
//   0-3  the view and projection of the level (OpenGL clip space)
//   4    the eye, w: tan of half the field of view across
//   5    forward, w: the same up
//   6    left, w and 7 w: the depth of a distance d in front of the eye is 6.w / d - 7.w
//   7    up
//   8    the view on the target in pixels: left, top, width, height
//   9    1 / the size of the copy of the frame, the time in seconds, how many places
//   10   the height of the floor of the puddle view, 1 if it was drawn this frame (VK_BeginPuddle)
//   11+  six rows a place: origin, radius / normal, wet / hit, puddle / ambient, flow / directed, seed / light
//        dir, kind (WET_PLACE, ... of tr_types.h)

#include "dialect.glsl"

// VK_WET_VEC4S of vk_local.h
#ifdef VK_GLES
uniform vec4 u_wet[683];
#define W			u_wet
#define INSTANCE	uint( gl_InstanceID )
#else
BINDING( 0, 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[683];
} globals;
#define W			globals.wet
#define INSTANCE	uint( gl_InstanceIndex )
#endif

VARYING( 0 ) flat out uint v_spot;

const vec2 CORNERS[6] = vec2[6]( vec2( -1.0, -1.0 ), vec2( 1.0, -1.0 ), vec2( -1.0, 1.0 ), vec2( -1.0, 1.0 ), vec2( 1.0, -1.0 ),
	vec2( 1.0, 1.0 ) );

void main() {
	uint spot = INSTANCE;
	uint row = 11u + spot * 6u;
	vec2 corner = CORNERS[int( VERTEX_INDEX ) % 6];
	vec3 eye = W[4].xyz;
	vec3 forward = W[5].xyz;
	vec3 left = W[6].xyz;
	vec3 up = W[7].xyz;
	vec3 c = W[row].xyz;
	// the streaks that run down a wall reach twice as far, and the surface may lie a little off its plane
	float radius = W[row].w * 2.0 + 8.0;
	float z = dot( c - eye, forward );

	v_spot = spot;
	if ( z < radius + 6.0 ) {
		gl_Position = vec4( corner, 0.5, 1.0 );
		return;
	}
	// the ball on the plane one unit in front of the eye, as in liquid.vert, the quad on its near side
	vec2 p = vec2( dot( c - eye, left ), dot( c - eye, up ) ) / z;
	float s = radius / sqrt( max( z * z - radius * radius, 1.0 ) ) * 1.25;
	p += corner * s;
	float near = z - radius;
	vec3 position = eye + ( forward + left * p.x + up * p.y ) * near;
	mat4 mvp = mat4( W[0], W[1], W[2], W[3] );

	gl_Position = mvp * vec4( position, 1.0 );
#ifdef VK_GLES
	gl_Position.y = -gl_Position.y;
#else
	gl_Position.z = ( gl_Position.z + gl_Position.w ) * 0.5;
#endif
}
