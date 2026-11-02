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
uniform vec4 u_wet[779];
#define W			u_wet
#define INSTANCE	uint( gl_InstanceID )
#else
BINDING( 0, 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
} globals;
#define W			globals.wet
#define INSTANCE	uint( gl_InstanceIndex )
#endif

VARYING( 0 ) flat out uint v_spot;

#include "ball.glsl"

void main() {
	uint spot = INSTANCE;
	uint row = 11u + spot * 6u;

	v_spot = spot;
	// the streaks that run down a wall reach twice as far, and the surface may lie a little off its plane
	gl_Position = BallQuad( W[row].xyz, W[row].w * 2.0 + 8.0 );
}
