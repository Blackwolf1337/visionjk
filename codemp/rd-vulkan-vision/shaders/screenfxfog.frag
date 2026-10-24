#version 460

// The mist of Force Sight (VK_ScreenFxFogPass), at half the size of the frame: how much mist lies between the eye
// and every pixel, and how bright it is there. It fills the space around the eye, as far as RADIUS, thinning out
// towards it, so it goes with the player wherever he is, up a ledge or down. Its banks are noise that stays in the
// level and rolls with a wind, so he walks through them; there is no plane of it that could be seen to move. A march
// of the view ray, the steps closer together near the eye where the mist is seen in detail. r: how much it hides
// (1 - what comes through), g: how bright it is (its denser parts lit more). The numbers are the ones of
// screenfx.frag (VK_ScreenFx).

#include "dialect.glsl"
#include "noise.glsl"

BINDING( 1, 0 ) uniform sampler2D u_depth;

#ifdef VK_GLES
uniform vec4 u_parm[160];
#define FX	u_parm
#else
BINDING( 0, 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
} globals;
#define FX	globals.screenFx
#endif

#define STEPS		14
#define RADIUS		800.0		// units from the eye to where the mist has thinned out
#define THICK		0.006		// a unit, in a bank of it

VARYING( 0 ) in vec2 v_texCoord;
layout( location = 0 ) out vec4 o_color;

void main() {
	float time = FX[0].x;
	vec4 rect = FX[13];
	vec2 ndc = vec2( ( v_texCoord.x - rect.x ) / rect.z * 2.0 - 1.0, 1.0 - ( v_texCoord.y - rect.y ) / rect.w * 2.0 );
	vec3 eye = FX[9].xyz;
	vec3 dir = FX[10].xyz + FX[11].xyz * ndc.x * FX[9].w + FX[12].xyz * ndc.y * FX[10].w;
	float stretch = length( dir );
	vec3 rd = dir / stretch;
	// the depth holds the distance along the view, the ray is longer by its slant. The nearest of the four pixels
	// under this one, as screenfxprep.frag takes it, so the two buffers agree where screenfx.frag compares them.
	vec2 texel = 0.5 / vec2( textureSize( u_depth, 0 ) );
	float depth = min( min( texture( u_depth, v_texCoord + vec2( -texel.x, -texel.y ) ).r, texture( u_depth, v_texCoord + vec2( texel.x, -texel.y ) ).r ),
		min( texture( u_depth, v_texCoord + vec2( -texel.x, texel.y ) ).r, texture( u_depth, v_texCoord + vec2( texel.x, texel.y ) ).r ) );
	float dist = min( FX[2].x / ( depth + FX[2].y ) * stretch, RADIUS );
	// the first step at another place for every pixel and frame (interleaved gradient noise), so the steps show as a
	// fine grain the filtered read in screenfx.frag smooths out, and not as bands
	float jitter = fract( 52.9829189 * fract( dot( gl_FragCoord.xy, vec2( 0.06711056, 0.00583715 ) ) ) + time * 0.618 );
	vec3 wind = vec3( time * 18.0, time * 11.0, time * 2.0 );
	float through = 1.0;
	float light = 0.0;
	float last = 0.0;

	for ( int i = 0; i < STEPS; i++ ) {
		// the steps grow with the square of the way, so the near mist has most of them
		float k = ( float( i ) + jitter ) / float( STEPS );
		float t = dist * k * k;
		float dt = t - last;
		vec3 p = eye + rd * t;
		vec3 q = ( p + wind ) * 0.009;
		// rolled by a slow warp of its own
		q += vec3( Noise3( q * 0.5 + vec3( 0.0, 0.0, time * 0.05 ) ), Noise3( q * 0.5 + vec3( 5.2, 1.3, time * 0.04 ) ), 0.0 ) * 1.5;
		float n = Fbm3( q );
		// in banks, thinning out to nothing at RADIUS
		float sigma = THICK * smoothstep( 0.4, 0.7, n ) * ( 1.0 - smoothstep( RADIUS * 0.4, RADIUS, t ) );
		float take = exp( -sigma * dt );

		light += through * ( 1.0 - take ) * ( 0.6 + 0.8 * n );
		through *= take;
		last = t;
	}
	o_color = vec4( 1.0 - through, clamp( light / max( 1.0 - through, 0.001 ), 0.0, 1.0 ), 0.0, 1.0 );
}
