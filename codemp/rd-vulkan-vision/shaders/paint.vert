#version 460

// The sprayed pictures of RE_AddPaint (paintDecal_t, VK_DrawWet() in vk_post.cpp), a quad each over the part of the
// view where the ball around the picture is (ball.glsl): shaders/paint.frag finds the level under every pixel of it
// from the depth. The numbers are the paint ones of the globals of the frame: the view as wet.vert has it in rows 0
// to 10 (9 w: how many pictures), then seven rows a picture:
//
//   0    origin, w: half the side of the picture
//   1    normal of the surface, w: how much of it is sprayed on yet (reveal)
//   2    up along the surface, w: how fresh the paint is (1 wet, 0 dry)
//   3    ambient light, w: the number of the image of its sheet in the table of the textures
//   4    directed light, w: seed
//   5    light dir, w: pixels of a frame each way
//   6    the frame shown in the sheet: left, top, width, height (0 to 1)

#include "dialect.glsl"

// VK_PAINT_VEC4S of vk_local.h
#ifdef VK_GLES
uniform vec4 u_paint[459];
#define W			u_paint
#define INSTANCE	uint( gl_InstanceID )
#else
BINDING( 0, 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
} globals;
#define W			globals.paint
#define INSTANCE	uint( gl_InstanceIndex )
#endif

VARYING( 0 ) flat out uint v_decal;

#include "ball.glsl"

void main() {
	uint decal = INSTANCE;
	uint row = 11u + decal * 7u;

	v_decal = decal;
	// the drips run down below the picture, and the wall may lie a little off its plane
	gl_Position = BallQuad( W[row].xyz, W[row].w * 2.1 + 6.0 );
}
