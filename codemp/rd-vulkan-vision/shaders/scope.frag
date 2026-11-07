#version 460

// The view of the level through the scope of a weapon (VK_ScopePass, scopeEffect_t), drawn over the frame. A
// large, clear lens: bent a little towards the rim (barrel distortion), its colours part there, sharp around what
// the crosshair is on and soft before and behind that, a thin rim of light. On the glass a fine reticle with a lit
// dot in its middle. Around the rim, on a dark ring of the housing, three arcs: the ammo on the right, the zoom on
// the left, the charge of the alt fire at the bottom. Outside the lens the scene stays, soft and dimmed. The
// tracker turns the lens into a dim red picture with the edges of the level, scan lines, grain and a rolling band,
// but what is inside its boxes keeps its colours.

#include "dialect.glsl"

BINDING( 0, 0 ) uniform sampler2D u_image;		// a copy of the frame
BINDING( 1, 0 ) uniform sampler2D u_soft;		// the frame blurred, half the size
BINDING( 2, 0 ) uniform sampler2D u_prep;		// scopeprep.frag: out of focus, edge
BINDING( 3, 0 ) uniform sampler2D u_mark;		// the boxes of the tracker

// [0] xy: the middle of the lens in texture coordinates, z: its radius as a share of the height, w: width / height
// [1] x: zoom 0 to 1, y: tracker 0 to 1, z: seconds, w: how much of the lens (0: the cgame draws the old mask), 2:
//     the binoculars, two lenses side by side
// [2] xy: texture coordinates of the frame to the ones of its copy, which may be larger. z: 1 / overbright factor,
//     the frame holds the colours before the gamma pass. w: 1 when u_mark holds the boxes of the tracker
// [3] x: the height of the frame in pixels, y: the charge of the alt fire 0 to 1, below 0 none, z: the ammo 0 to 1,
//     w: 1 for the reticle and the arcs
#ifdef VK_GLES
uniform vec4 u_parm[4];
#define PARMS	u_parm
#else
layout( push_constant ) uniform Push {
	vec4	parm[4];
} push;
#define PARMS	push.parm
#endif

VARYING( 0 ) in vec2 v_texCoord;
layout( location = 0 ) out vec4 o_color;

float Hash( vec2 p ) {
	return fract( sin( dot( p, vec2( 12.9898, 78.233 ) ) ) * 43758.5453 );
}

vec3 Frame( vec2 at ) {
	return texture( u_image, at * PARMS[2].xy ).rgb;
}

void main() {
	vec2 center = PARMS[0].xy;
	float radius = PARMS[0].z;
	float aspect = PARMS[0].w;
	float zoom = PARMS[1].x;
	float motion = PARMS[1].y;
	float time = PARMS[1].z;
	float lens = min( PARMS[1].w, 1.0 );
	float light = PARMS[2].z;
	// the binoculars: the middle of the lens on this side of the view, so the two make one shape; where they meet the
	// glass bends less, or the picture of the one would jump into the one of the other
	float seam = 1.0;

	if ( PARMS[1].w > 1.5 ) {
		float apart = radius * 0.62 / aspect;
		float side = v_texCoord.x < center.x ? -1.0 : 1.0;

		seam = smoothstep( 0.0, 0.25, abs( v_texCoord.x - center.x ) * aspect / radius );
		center.x += side * apart;
	}

	// where the pixel is on the lens: 0 in the middle, 1 at the rim
	vec2 delta = v_texCoord - center;
	vec2 onLens = vec2( delta.x * aspect, delta.y );
	float r = length( onLens ) / radius;
	// the size of a pixel in units of the radius, for lines that stay sharp at any size of the window
	float px = 1.0 / max( PARMS[3].x * radius, 1.0 );
	float inside = 1.0 - smoothstep( 1.0 - px, 1.0, r ) * lens;

	// barrel distortion: the samples move out towards the rim, so the middle is a little larger
	float bend = ( 0.025 + 0.035 * zoom ) * lens * seam;
	vec2 at = center + delta * ( 1.0 + bend * r * r );
	// the colours part along the radius, only near the rim
	vec2 split = delta * ( 0.006 + 0.006 * zoom ) * r * r * r * r * lens * seam;
	vec3 sharp = vec3( Frame( at + split ).r, Frame( at ).g, Frame( at - split ).b );

	vec4 prep = texture( u_prep, at );
	vec3 soft = texture( u_soft, at ).rgb;
	vec3 color = mix( sharp, soft, prep.r * ( 0.3 + 0.5 * zoom ) * lens );

	// coated glass: the faintest cool tint, a little shade only close to the rim, a soft sheen to the upper left
	color = mix( color, color * vec3( 0.96, 1.0, 1.03 ), lens );
	color *= 1.0 - 0.35 * smoothstep( 0.82, 1.0, r ) * lens;
	float sheen = 1.0 - smoothstep( 0.0, 0.9, length( onLens / radius - vec2( -0.45, -0.5 ) ) );
	color += vec3( 0.6, 0.68, 0.75 ) * sheen * sheen * 0.035 * light * lens;

	if ( motion > 0.0 ) {
		float lum = dot( color, vec3( 0.299, 0.587, 0.114 ) );
		vec3 view = vec3( 0.55, 0.06, 0.03 ) * lum + vec3( 0.035, 0.0, 0.0 ) * light;

		// the edges of the level
		view += vec3( 0.75, 0.16, 0.06 ) * prep.g * 0.3 * light;
		// scan lines, every other pair of rows, and a band that rolls down slowly
		view *= 0.82 + 0.18 * step( 0.5, fract( gl_FragCoord.y * 0.25 ) );
		float band = fract( v_texCoord.y * 0.7 - time * 0.12 );
		view += vec3( 0.5, 0.08, 0.03 ) * exp( -pow( ( band - 0.5 ) * 30.0, 2.0 ) ) * 0.12 * light;
		// grain, new every frame
		view += vec3( 0.6, 0.15, 0.1 ) * ( Hash( gl_FragCoord.xy + fract( time * 7.13 ) * 113.0 ) - 0.5 ) * 0.07 * light;

		if ( PARMS[2].w > 0.5 ) {
			// the boxes of the tracker: the level as it is, a little brighter
			float box = texture( u_mark, at ).r;

			view = mix( view, color * 1.08, box );
		}
		color = mix( color, view, motion );
	}

	// the reticle on the glass: thin lines with a gap around the middle, thicker posts towards the rim at the left,
	// the right and the bottom, small marks every tenth of the radius, a lit dot in the middle
	if ( PARMS[3].w > 0.0 ) {
		vec2 p = onLens / radius;
		vec2 a = abs( p );
		float thin = 0.6 * px;
		float post = 2.2 * px;
		float ret = 0.0;

		ret = max( ret, ( 1.0 - smoothstep( thin, thin + px, a.y ) ) * step( 0.035, a.x ) );
		ret = max( ret, ( 1.0 - smoothstep( thin, thin + px, a.x ) ) * step( 0.035, a.y ) );
		ret = max( ret, ( 1.0 - smoothstep( post, post + px, a.y ) ) * step( 0.62, a.x ) );
		ret = max( ret, ( 1.0 - smoothstep( post, post + px, a.x ) ) * step( 0.62, p.y ) );
		// the marks: short across the lines, every tenth up to half the radius
		vec2 tick = abs( fract( p * 10.0 + 0.5 ) - 0.5 ) / 10.0;
		ret = max( ret, ( 1.0 - smoothstep( thin, thin + px, tick.x ) ) * step( a.y, 0.018 ) * step( a.x, 0.52 ) * step( 0.05, a.x ) );
		ret = max( ret, ( 1.0 - smoothstep( thin, thin + px, tick.y ) ) * step( a.x, 0.018 ) * step( a.y, 0.52 ) * step( 0.05, a.y ) );
		ret *= step( r, 1.0 );
		color = mix( color, vec3( 0.015, 0.02, 0.025 ) * light, ret * 0.85 * lens );

		float lit = 1.0 - smoothstep( 2.0 * px, 3.0 * px, length( p ) );
		float glow = exp( -length( p ) / ( 6.0 * px ) ) * 0.35;
		color += vec3( 1.0, 0.18, 0.1 ) * ( lit * 1.4 + glow ) * light * lens;
	}

	// outside of the lens the scene stays to be seen, soft, dimmed and a little grey
	vec3 around = texture( u_soft, v_texCoord ).rgb;
	vec3 outside = mix( around, vec3( dot( around, vec3( 0.299, 0.587, 0.114 ) ) ), 0.35 ) * 0.42;

	// the housing: a dark ring just outside the glass, a thin rim of light on the edge of the glass
	float ring = smoothstep( 1.0, 1.0 + px, r ) * ( 1.0 - smoothstep( 1.075, 1.075 + px * 2.0, r ) );
	outside = mix( outside, vec3( 0.02, 0.025, 0.03 ) * light, ring * 0.82 * lens );
	float rim = ( 1.0 - smoothstep( 0.0, 1.5 * px, abs( r - 1.0 ) ) );
	vec3 result = mix( outside, color, inside );
	result = mix( result, vec3( 0.75, 0.8, 0.85 ) * light, rim * 0.55 * lens );

	// the arcs on the housing, in segments: angle 0 to the right, up positive
	if ( PARMS[3].w > 0.0 && ring > 0.0 ) {
		vec2 up = vec2( onLens.x, -onLens.y );
		float ang = atan( up.y, up.x );
		float band = 1.0 - smoothstep( 0.0, px, abs( r - 1.0375 ) - 0.016 );
		float span = 0.62;
		float t = -1.0, fill = 0.0;
		vec3 lit = vec3( 0.85, 0.92, 1.0 );
		// the same angle measured from the left
		float left = atan( up.y, -up.x );

		if ( abs( ang ) < span ) {
			// the ammo on the right, from below, red when it runs out
			t = ( ang + span ) / ( 2.0 * span );
			fill = PARMS[3].z;
			lit = fill < 0.15 ? vec3( 1.0, 0.25, 0.15 ) : vec3( 0.7, 0.92, 1.0 );
		} else if ( abs( ang + 1.5708 ) < span && PARMS[3].y >= 0.0 ) {
			// the charge at the bottom, from the left
			t = ( ang + 1.5708 + span ) / ( 2.0 * span );
			fill = PARMS[3].y;
			lit = vec3( 1.0, 0.7, 0.2 );
		} else if ( abs( left ) < span ) {
			// the zoom on the left, from below
			t = ( left + span ) / ( 2.0 * span );
			fill = zoom;
			lit = vec3( 0.9, 0.95, 1.0 );
		}
		if ( t >= 0.0 ) {
			float gap = step( fract( t * 24.0 ), 0.78 );
			vec3 seg = t <= fill ? lit : vec3( 0.12, 0.14, 0.16 );
			result = mix( result, seg * light, band * gap * ring * lens * ( t <= fill ? 0.95 : 0.6 ) );
		}
	}
	o_color = vec4( result, 1.0 );
}
