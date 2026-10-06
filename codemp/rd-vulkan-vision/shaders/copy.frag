#version 460

// a target as it is, the pipeline decides how it lands on what is there (the glow is added)

layout( set = 0, binding = 0 ) uniform sampler2D u_image;

layout( location = 0 ) in vec2 v_texCoord;
layout( location = 0 ) out vec4 o_color;

void main() {
	o_color = texture( u_image, v_texCoord );
}
