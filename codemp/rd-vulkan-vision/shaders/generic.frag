#version 460

#include "common.glsl"

layout( set = 1, binding = 0 ) uniform sampler2D u_textures[];

layout( location = 0 ) in vec2 v_texCoord0;
layout( location = 1 ) in vec2 v_texCoord1;
layout( location = 2 ) in vec4 v_color;

layout( location = 0 ) out vec4 o_color;

void main() {
	Draw d = draws[push.draw];
	vec4 color = texture( u_textures[nonuniformEXT( d.tex0 )], v_texCoord0 ) * v_color;

	// the second texture of a stage, combined the way glTexEnv did it
	uint env = TEXENV( d.flags );
	if ( env != 0u ) {
		vec4 second = texture( u_textures[nonuniformEXT( d.tex1 )], v_texCoord1 );

		if ( env == 1u ) {				// modulate
			color *= second;
		} else if ( env == 2u ) {		// add
			color = vec4( color.rgb + second.rgb, color.a * second.a );
		} else if ( env == 3u ) {		// decal
			color.rgb = mix( color.rgb, second.rgb, second.a );
		} else {						// replace
			color = second;
		}
	}

	uint atest = ATEST( d.flags );
	if ( atest == 1u && color.a <= 0.0 ) {
		discard;
	} else if ( atest == 2u && color.a >= 0.5 ) {
		discard;
	} else if ( atest == 3u && color.a < 0.5 ) {
		discard;
	} else if ( atest == 4u && color.a < 0.75 ) {
		discard;
	}
	o_color = color;
}
