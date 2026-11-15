#version 460

// v_autoExposure (vk_post.cpp VK_AutoExposure): the copy of the frame (vk.screen) in cells, 64 by 36 of them, each the
// average of the log2 of the brightness of 16 pixels spread over it. The CPU reads them back a few frames later,
// weighs the middle of the view more and adapts the exposure of the tone map to them. PARM: xy the size of the frame.

#include "post.glsl"

void main() {
	vec2 cell = PARM.xy / vec2( 64.0, 36.0 );
	vec2 corner = floor( gl_FragCoord.xy ) * cell;
	float sum = 0.0;

	for ( int y = 0; y < 4; y++ ) {
		for ( int x = 0; x < 4; x++ ) {
			ivec2 pixel = ivec2( corner + ( vec2( x, y ) + 0.5 ) * cell * 0.25 );
			vec3 c = texelFetch( u_image, clamp( pixel, ivec2( 0 ), ivec2( PARM.xy ) - 1 ), 0 ).rgb;
			float l = dot( max( c, vec3( 0.0 ) ), vec3( 0.2126, 0.7152, 0.0722 ) );

			// (NaN from elsewhere counts as black)
			sum += log2( max( isnan( l ) ? 0.0 : l, 1.0 / 1024.0 ) );
		}
	}
	o_color = vec4( sum / 16.0, 0.0, 0.0, 1.0 );
}
