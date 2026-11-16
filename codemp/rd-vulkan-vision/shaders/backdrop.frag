#version 460

// VisionJK: frosted glass behind a panel (VK_BackdropPass). The frame blurred into a target of half its size is
// laid over the frame inside a rect with rounded corners, a pixel of soft edge. rect: x0 y0 x1 y1 in pixels of the
// frame. more.x: the radius of the corners in pixels. The full-screen triangle gives the place in the blurred target.
// Not made into GLSL ES (MPVulkanVisionVulkanShaders).

#include "dialect.glsl"

BINDING( 0, 0 ) uniform sampler2D u_image;

layout( push_constant ) uniform Push {
	vec4	rect;
	vec4	more;
} push;

VARYING( 0 ) in vec2 v_texCoord;
layout( location = 0 ) out vec4 o_color;

void main() {
	vec2	center = 0.5 * ( push.rect.xy + push.rect.zw );
	vec2	extent = 0.5 * ( push.rect.zw - push.rect.xy );
	float	radius = min( push.more.x, min( extent.x, extent.y ) );
	// how far the pixel is outside the rounded rect (negative inside)
	vec2	q = abs( gl_FragCoord.xy - center ) - extent + vec2( radius );
	float	outside = length( max( q, 0.0 ) ) + min( max( q.x, q.y ), 0.0 ) - radius;

	o_color = vec4( texture( u_image, v_texCoord ).rgb, clamp( 0.5 - outside, 0.0, 1.0 ) );
}
