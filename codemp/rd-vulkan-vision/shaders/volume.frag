#version 460
// A volume of a level that its shader script fills with something only this renderer draws ("q3map_vision" of the
// script, rd-vanilla/tr_shader.cpp; VK_VolumeFrame): the brush of a fog whose script says clouds. Any other
// renderer draws the plain fog of the same script there.
//
// The pass comes after what is solid in the view. It reads the frame and its depth, marches the part of the ray
// of a pixel that lies inside the brush, as far as to what stands there, and gives the pixel back with the clouds
// in front of it. So a player behind a heap of cloud is hidden by it and one in front of it is not.
//
// The frame went through the curve of film() already (shaders/temple.frag draws sky and gold in front of this
// pass, and what else stands there is a colour of the screen anyhow): it is taken back out of the curve, the
// clouds are laid over it in light, and the sum goes through the curve again. Laid over it as colours of the
// screen their thin edges would come out too bright.
//
// VIEW 0 to 4: as shaders/temple.frag has them. 5: xyz the low corner of the brush, w the x of the middle of the
// place the clouds leave free. 6: xyz its high corner, w the y of that middle. 7: x the radius of that place,
// yzw the lane they leave free (from x, to x, half its width). The last four are the numbers of the script.
#include "dialect.glsl"

BINDING( 0, 0 ) uniform sampler2D u_image;
BINDING( 1, 0 ) uniform sampler2D u_depth;

#ifdef VK_GLES
uniform vec4 u_view[8];
#define VIEW( i )	u_view[i]
#else
layout( push_constant ) uniform Push {
	vec4	view[8];
} push;
#define VIEW( i )	push.view[i]
#endif

VARYING( 0 ) in vec2 v_texCoord;
layout( location = 0 ) out vec4 o_color;

#define CLOUD_ROOF	VIEW( 6 ).z
#define CLOUD_DISC	vec3( VIEW( 5 ).w, VIEW( 6 ).w, VIEW( 7 ).x )
#define CLOUD_LANE	VIEW( 7 ).yzw
#include "vision.glsl"

void main() {
	float seconds = VIEW( 0 ).w;
	vec3 ro = VIEW( 0 ).xyz;
	vec2 at = vec2( v_texCoord.x * 2.0 - 1.0, 1.0 - v_texCoord.y * 2.0 );
	vec3 rd = normalize( VIEW( 1 ).xyz + at.x * VIEW( 2 ).xyz + at.y * VIEW( 3 ).xyz );
	// another number for every pixel, for the steps through the clouds
	float jitter = fract( 52.9829189 * fract( dot( gl_FragCoord.xy, vec2( 0.06711056, 0.00583715 ) ) ) );
	vec3 frame = texture( u_image, v_texCoord ).rgb;
	float depth = texture( u_depth, v_texCoord ).r;
	// how far along the ray the thing of this pixel is: its depth is x - y / d for a thing d ahead
	float tScene = depth < 0.999999 ? VIEW( 4 ).y / max( ( VIEW( 4 ).x - depth ) * dot( rd, VIEW( 1 ).xyz ), 1.0e-6 ) : 1.0e9;
	// the part of the ray inside the brush
	// (a ray along a side of it: a little off that, so nothing is divided by 0)
	vec3 inv = 1.0 / ( rd + ( 1.0 - step( vec3( 1.0e-6 ), abs( rd ) ) ) * 1.0e-6 );
	vec3 a = ( VIEW( 5 ).xyz - ro ) * inv;
	vec3 b = ( VIEW( 6 ).xyz - ro ) * inv;
	vec3 lo = min( a, b );
	vec3 hi = max( a, b );
	float tIn = max( max( lo.x, lo.y ), max( lo.z, 0.0 ) );
	float tOut = min( min( hi.x, hi.y ), min( hi.z, tScene ) );

	if ( tOut <= tIn ) {
		o_color = vec4( frame, 0.0 );
		return;
	}
	vec4 sea = clouds( ro, rd, tIn, tOut, seconds, jitter, VIEW( 2 ).w );
	// (the two things temple.frag does to its light in front of the curve: a little darker towards the corners)
	float scale = 0.62 * ( 1.0 - 0.22 * dot( at, at ) );
	vec3 color = film( scale * sea.rgb + sea.a * unfilm( frame ) );

	// a pixel no cloud is in front of stays as it is
	o_color = vec4( color, clamp( ( 1.0 - sea.a ) * 40.0, 0.0, 1.0 ) );
}
