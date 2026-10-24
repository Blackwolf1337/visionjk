#version 460

// The smoke of the ghosts of Force Sight (VK_ScreenFxSmokePass), at half the size of the frame and kept from frame to
// frame: what the last frame held rises up the view, is turned a little by a slow noise and thins out, and every
// ghost (a SCREENFX_GHOSTSMOKE source, his body as a line from his feet to his head) puts new smoke where he is.
// It goes only half of the way the picture goes when the eye turns, so it lags behind the turn and leans away from
// it, and it smears as in a long exposure. r: how thick it is. The numbers are the ones of screenfx.frag
// (VK_ScreenFx), FX[15]: xw how far the picture moved as the eye turned, y the seconds since the last frame, z 1 when
// u_image holds the smoke of the frame before.

#include "dialect.glsl"
#include "noise.glsl"

BINDING( 1, 0 ) uniform sampler2D u_image;		// the smoke of the frame before
BINDING( 2, 0 ) uniform sampler2D u_prep;		// screenfxprep.frag: the distance from the eye

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

#define SOURCES		48
#define GHOSTSMOKE	7.0
#define RISE		0.14		// of the height of the view a second
#define LIFE		0.4			// seconds the smoke takes to thin out to a third
#define FOLLOW		0.5			// of the way the picture moves as the eye turns, the smoke goes along

VARYING( 0 ) in vec2 v_texCoord;
layout( location = 0 ) out vec4 o_color;

float Distance( vec2 at ) {
	ivec2 size = textureSize( u_prep, 0 );
	vec4 d = texelFetch( u_prep, clamp( ivec2( at * vec2( size ) ), ivec2( 0 ), size - 1 ), 0 );

	return exp2( ( d.r * 255.0 + d.g ) / 255.0 * 16.0 );
}

void main() {
	float time = FX[0].x;
	vec2 aspect = vec2( FX[0].y, 1.0 );
	float dt = clamp( FX[15].y, 0.0, 0.1 );
	int count = int( FX[0].w + 0.5 );
	float here = Distance( v_texCoord );
	float smoke = 0.0;

	// what was here a moment ago lay a little lower, and a little to one side as the noise turns it
	if ( FX[15].z > 0.5 ) {
		vec2 p = v_texCoord * aspect * 5.0;
		vec2 turn = vec2( Noise( p + vec2( time * 0.3, 0.0 ) ), Noise( p + vec2( 4.3, time * 0.25 ) ) ) - 0.5;
		vec2 from = v_texCoord + ( vec2( turn.x * 0.6, 1.0 ) * RISE + vec2( 0.0, turn.y * 0.05 ) ) * dt / aspect
			- FX[15].xw * FOLLOW;

		smoke = texture( u_image, from ).r * exp( -dt * 1.1 / LIFE );
	}

	// new smoke from every ghost, thicker in the middle of his body, broken by a noise that rises with it
	for ( int i = 0; i < SOURCES; i++ ) {
		if ( i >= count ) {
			break;
		}
		vec4 ends = FX[16 + 3 * i];
		vec4 size = FX[17 + 3 * i];		// radius, width, strength, distance
		float kind = FX[18 + 3 * i].x;

		if ( abs( kind - GHOSTSMOKE ) > 0.5 || here < size.w - 24.0 ) {
			continue;
		}
		vec2 pa = ( v_texCoord - ends.xy ) * aspect;
		vec2 ba = ( ends.zw - ends.xy ) * aspect;
		float h = clamp( dot( pa, ba ) / max( dot( ba, ba ), 1e-8 ), 0.0, 1.0 );
		float body = 1.0 - smoothstep( 0.2, 1.0, length( pa - ba * h ) / max( size.x, 0.0001 ) );
		// broken by two noises, a coarse and a fine one, so it leaves many threads
		float broken = max( smoothstep( 0.25, 0.75, Noise( v_texCoord * aspect * 40.0 + vec2( 0.0, time * 3.0 ) ) ),
			0.8 * smoothstep( 0.35, 0.8, Noise( v_texCoord * aspect * 95.0 + vec2( time * 0.7, time * 5.0 ) ) ) );

		smoke += body * broken * size.z * dt * 9.0;
	}
	o_color = vec4( clamp( smoke, 0.0, 1.0 ), 0.0, 0.0, 1.0 );
}
