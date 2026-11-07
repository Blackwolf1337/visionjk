#version 460

// a target as it is, the pipeline decides how it lands on what is there (the glow is added)

#include "post.glsl"

void main() {
	o_color = texture( u_image, v_texCoord );
}
