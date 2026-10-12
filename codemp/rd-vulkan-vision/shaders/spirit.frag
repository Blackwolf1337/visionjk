#version 460
// The look of a world that stands still (RE_SetSpiritWorld, the Focus of the cgame): what is left of the frame is
// its light in dark blue, and a fog lies in it that is thicker with the distance and drifts like smoke. The pass
// reads the frame at half its size and a little soft, and its output is laid over the frame by its alpha, so
// a rest of the sharp picture stays. PARM: xy: the depth of a pixel to its distance from the eye,
// x / ( depth + y ). z: seconds, for the drift. w: how much of the effect there is, 0 to 1.
#include "post.glsl"

BINDING( 1, 0 ) uniform sampler2D u_depth;

float hash( vec2 p ) {
	return fract( sin( dot( p, vec2( 127.1, 311.7 ) ) ) * 43758.5453 );
}

float noise( vec2 p ) {
	vec2 i = floor( p );
	vec2 f = fract( p );

	f = f * f * ( 3.0 - 2.0 * f );
	return mix( mix( hash( i ), hash( i + vec2( 1.0, 0.0 ) ), f.x ),
		mix( hash( i + vec2( 0.0, 1.0 ) ), hash( i + vec2( 1.0, 1.0 ) ), f.x ), f.y );
}

// four layers of it, each twice as fine and half as strong: 0 to 1
float smoke( vec2 p ) {
	float sum = 0.0;
	float part = 0.5;

	for ( int i = 0; i < 4; i++ ) {
		sum += part * noise( p );
		p = p * 2.03 + vec2( 17.0, 9.0 );
		part *= 0.5;
	}
	return sum / 0.9375;
}

void main() {
	float seconds = PARM.z;
	float dist = PARM.x / ( texture( u_depth, v_texCoord ).r + PARM.y );
	vec3 soft = texture( u_image, v_texCoord ).rgb;
	float light = dot( soft, vec3( 0.299, 0.587, 0.114 ) );

	// the world: nearly without its colours, dark blue, what was bright a little paler
	vec3 world = mix( vec3( light ), soft, 0.15 ) * vec3( 0.34, 0.50, 0.95 );
	world += light * light * vec3( 0.10, 0.16, 0.20 );

	// The fog. Two fields of smoke that drift past each other, the wide one decides how thick the fog of the
	// distance is, the fine one hangs in the air right before the eye.
	vec2 p = v_texCoord * vec2( 3.2, 1.8 );
	float wide = smoke( p * 0.8 + vec2( -seconds * 0.021, seconds * 0.034 ) );
	float fine = smoke( p * 1.7 + vec2( seconds * 0.043, seconds * 0.071 ) + 5.0 );
	float far = 1.0 - exp( -dist / 900.0 );
	float fog = clamp( far * ( 0.30 + 0.95 * wide ) + 0.30 * fine * fine, 0.0, 1.0 );
	vec3 fogColor = vec3( 0.07, 0.13, 0.34 ) + vec3( 0.10, 0.17, 0.26 ) * fine;
	vec3 color = mix( world, fogColor, fog );

	// dark towards the edges of the picture
	vec2 fromMiddle = v_texCoord - 0.5;
	color *= 1.0 - 0.55 * smoothstep( 0.20, 0.95, dot( fromMiddle, fromMiddle ) * 2.0 );

	o_color = vec4( color, PARM.w * 0.9 );
}
