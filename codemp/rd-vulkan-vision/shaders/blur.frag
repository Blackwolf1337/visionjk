#version 460

// One pass of the blur of the glow: four samples around the pixel, as the vertex and fragment programs of
// rd-vanilla take them (tr_arb.cpp). Each pass moves the samples further out.

layout( set = 0, binding = 0 ) uniform sampler2D u_image;

layout( push_constant ) uniform Push {
	vec4	parm;		// xy: how far out the samples are, in texture coordinates. z: the weight of each
} push;

layout( location = 0 ) in vec2 v_texCoord;
layout( location = 0 ) out vec4 o_color;

void main() {
	vec2 o = push.parm.xy;
	vec3 color = texture( u_image, v_texCoord + vec2( -o.x, -o.y ) ).rgb + texture( u_image, v_texCoord + vec2( -o.x, o.y ) ).rgb
		+ texture( u_image, v_texCoord + vec2( o.x, -o.y ) ).rgb + texture( u_image, v_texCoord + o ).rgb;

	o_color = vec4( color * push.parm.z, 1.0 );
}
