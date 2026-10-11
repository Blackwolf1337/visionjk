#version 460
// The depth of field a module asked for (RE_SetDepthOfField): the frame out of focus, with how much of it a pixel
// shows in its alpha. Nothing up to the distance that is in focus, all of it far behind that: what stands at the
// distance of the menu in the level, the player next to it, stays as sharp as it was drawn. PARM: xy: the depth
// of a pixel to its distance from the eye, x / ( depth + y ). z: the distance in focus. w: how much of the
// effect there is, 0 to 1.
#include "post.glsl"

BINDING( 1, 0 ) uniform sampler2D u_depth;

void main() {
	float dist = PARM.x / ( texture( u_depth, v_texCoord ).r + PARM.y );
	// 0 at the distance in focus, towards 1 far behind it
	float behind = 1.0 - PARM.z / max( dist, PARM.z );

	o_color = vec4( texture( u_image, v_texCoord ).rgb, PARM.w * smoothstep( 0.2, 0.6, behind ) );
}
