#version 460

// Clear water (RE_AddLiquid, VK_DrawLiquids): the ray of the pixel is cast against the round body of the piece,
// a capsule from a to b whose radius goes from the one at a to the one at b. Where it hits, the water is drawn from
// the copy of the frame (the draw's first texture): what lies behind it, bent by the surface and darkened by the
// way through it, and what lies in front of it, mirrored, as much as Fresnel's law gives at that angle; then the
// highlight of the light of the level and white foam where the jet tears. The surface has small waves that ride
// with the water (flow), stronger where it breaks up. The depth is the one of the hit, so pieces that overlap and
// the level cut each other where they really meet. The edge is soft over a pixel.

#include "common.glsl"
#include "noise.glsl"

#ifdef VK_GLES
uniform sampler2D u_tex0;
#define SCENE( draw_ )	u_tex0
#define DRAW			0u
#else
BINDING( 1, 0 ) uniform sampler2D u_textures[];
#define SCENE( draw_ )	u_textures[nonuniformEXT( draw_.tex0 )]
#define DRAW			v_draw
#endif

VARYING( 0 ) in vec3 v_position;
VARYING( 1 ) flat in vec4 v_a;				// a, radius at a
VARYING( 2 ) flat in vec4 v_b;				// b, flow at a
VARYING( 3 ) flat in vec4 v_flow;			// flow at b, breakup, the time, radius at b
VARYING( 4 ) flat in vec4 v_lightDir;
VARYING( 5 ) flat in vec4 v_ambient;		// at a, w: the part of it at b
VARYING( 6 ) flat in vec4 v_directed;
VARYING( 7 ) flat in vec3 v_left;
VARYING( 8 ) flat in vec3 v_up;
#ifndef VK_GLES
VARYING( 9 ) flat in uint v_draw;
#endif

layout( location = 0 ) out vec4 o_color;

// Draw.texMat0[0]: 1 / the width and height of the copy, the part of the copy the frame fills (x, y)
// Draw.texMat0[1]: x how strongly the water bends, y how much it darkens per unit, z the waves
// Draw.texMat1[0]: x, y: the depth of a distance d in front of the eye is x / d - y. z, w: a slope from the line of
// sight to a part of the picture, across and up

const float ETA = 0.75;		// air into water, 1 / 1.33

// The ball of radius r around c: where the ray enters, -1 when it misses; far: where it leaves
float Ball( vec3 ro, vec3 rd, vec3 c, float r, out float far ) {
	vec3 oc = ro - c;
	float b = dot( oc, rd );
	float d = b * b - dot( oc, oc ) + r * r;

	far = -1.0;
	if ( d < 0.0 ) {
		return -1.0;
	}
	d = sqrt( d );
	far = -b + d;
	return -b - d;
}

// The capsule of radius r from a to b: where the ray enters, -1 when it misses. h: where along a to b, 0 to 1,
// far: where the ray leaves (for the body: where it leaves the round side, which is all the thickness needs).
float Capsule( vec3 ro, vec3 rd, vec3 a, vec3 b, float r, out float h, out float far ) {
	vec3 ba = b - a;
	vec3 oa = ro - a;
	float baba = dot( ba, ba );
	float best = -1.0;
	float far2;

	h = 0.0;
	far = -1.0;
	if ( baba > 1e-6 ) {
		// the side: the points of the ray as far from the axis as r
		float bard = dot( ba, rd );
		float baoa = dot( ba, oa );
		float qa = baba - bard * bard;
		float qb = baba * dot( rd, oa ) - baoa * bard;
		float qc = baba * dot( oa, oa ) - baoa * baoa - r * r * baba;
		float d = qb * qb - qa * qc;

		if ( qa > 1e-8 && d >= 0.0 ) {
			float t = ( -qb - sqrt( d ) ) / qa;
			float y = baoa + t * bard;

			if ( t > 0.0 && y > 0.0 && y < baba ) {
				h = y / baba;
				far = ( -qb + sqrt( d ) ) / qa;
				return t;
			}
		}
	}
	// the round ends
	float t = Ball( ro, rd, a, r, far2 );
	if ( t > 0.0 ) {
		best = t;
		far = far2;
		h = 0.0;
	}
	t = Ball( ro, rd, b, r, far2 );
	if ( t > 0.0 && ( best < 0.0 || t < best ) ) {
		best = t;
		far = far2;
		h = 1.0;
	}
	return best;
}

// How near the ray passes the line from a to b, and where along it (0 to 1)
float RayToSegment( vec3 ro, vec3 rd, vec3 a, vec3 b, out float h ) {
	vec3 ba = b - a;
	float len = length( ba );
	vec3 v = ( len > 1e-4 ) ? ba / len : vec3( 0.0, 0.0, 1.0 );
	vec3 w = ro - a;
	float k = dot( rd, v );
	float denom = 1.0 - k * k;
	float along = ( denom > 1e-6 ) ? ( dot( v, w ) - k * dot( rd, w ) ) / denom : dot( v, w );

	along = clamp( along, 0.0, len );
	h = ( len > 1e-4 ) ? along / len : 0.0;
	vec3 onAxis = a + v * along;
	float t = max( dot( onAxis - ro, rd ), 0.0 );
	return length( ro + rd * t - onAxis );
}

// The highlight of a light seen at halfway h on a surface of normal n that runs along t (b across it): sharp around
// the jet, stretched along it, strength at its brightest
float Shine( vec3 n, vec3 t, vec3 b, vec3 h, float strength ) {
	const float ALONG = 24.0;
	const float AROUND = 1400.0;
	float hn = clamp( dot( n, h ), 0.0, 1.0 );
	float ht = dot( h, t );
	float hb = dot( h, b );
	float e = ( ALONG * ht * ht + AROUND * hb * hb ) / max( 1.0 - hn * hn, 1e-4 );

	return strength * pow( hn, min( e, 4000.0 ) );
}

// a place of the level as a place on the picture, 0 to 1 from the upper left corner; z: in front of the eye
vec3 OnScreen( mat4 mvp, vec3 p ) {
	vec4 clip = mvp * vec4( p, 1.0 );

	if ( clip.w <= 0.0 ) {
		return vec3( 0.0, 0.0, -1.0 );
	}
	return vec3( clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5, 1.0 );
}

void main() {
	Draw d = DRAWS( DRAW );
	Transform tr = TRANSFORMS( d.transform );
	vec3 eye = tr.viewOrigin.xyz;
	vec3 rd = normalize( v_position - eye );
	vec3 a = v_a.xyz;
	vec3 b = v_b.xyz;
	float ra = v_a.w;
	float rb = v_flow.w;
	float time = v_flow.z;
	float breakup = v_flow.y;
	// a pixel in units of the level where the quad is, for the soft edge
	float pixel = max( length( fwidth( v_position ) ), 1e-3 );

	// the soft edge: how much of the pixel the body covers, from how near the ray passes its axis
	float h;
	float miss = RayToSegment( eye, rd, a, b, h );
	float r = mix( ra, rb, h );
	float cover = clamp( ( r - miss ) / pixel + 0.5, 0.0, 1.0 );
	if ( cover <= 0.0 ) {
		discard;
	}

	vec3 axis = b - a;
	vec3 hit, n;
	float t, far;
	// twice: the radius of the second cast is the one where the first one hit
	t = Capsule( eye, rd, a, b, r, h, far );
	if ( t > 0.0 ) {
		r = mix( ra, rb, h );
		t = Capsule( eye, rd, a, b, r, h, far );
	}
	if ( t > 0.0 ) {
		hit = eye + rd * t;
		vec3 onAxis = a + axis * clamp( dot( hit - a, axis ) / max( dot( axis, axis ), 1e-6 ), 0.0, 1.0 );
		n = normalize( hit - onAxis );
	} else {
		// the pixel at the edge whose middle passes just outside: the side that faces away from the axis
		float s = max( dot( a + axis * h - eye, rd ), 0.0 );
		vec3 onAxis = a + axis * h;
		hit = eye + rd * s;
		n = hit - onAxis;
		n = ( dot( n, n ) > 1e-8 ) ? normalize( n ) : -rd;
		far = s;
		t = s;
	}
	float thickness = clamp( far - t, 0.0, 2.0 * r );
	vec3 ambient = v_ambient.rgb * mix( 1.0, v_ambient.w, h );
	vec3 directed = v_directed.rgb * mix( 1.0, v_directed.w, h );

	// The waves: noise over the water itself (where it is along the jet and around it) and the time, its slope
	// tilts the normal along the jet and around it. Waves along the jet are longer than around it.
	vec3 along = ( dot( axis, axis ) > 1e-6 ) ? normalize( axis ) : v_up;
	vec3 around = cross( along, n );
	around = ( dot( around, around ) > 1e-8 ) ? normalize( around ) : v_left;
	float flow = mix( v_b.w, v_flow.x, h );
	float angle = atan( dot( n, cross( around, along ) ), dot( n, around ) );
	vec3 q = vec3( flow * 0.22, angle * 1.6, time * 1.7 );
	const float e = 0.08;
	float w0 = Noise3( q );
	float wf = ( Noise3( q + vec3( e, 0.0, 0.0 ) ) - w0 ) / e;
	float wa = ( Noise3( q + vec3( 0.0, e, 0.0 ) ) - w0 ) / e;
	float fine = Noise3( q * vec3( 3.1, 2.7, 1.9 ) + 11.0 );
	float strength = d.texMat0[1].z * ( 0.6 + 1.4 * breakup );
	n = normalize( n + ( along * wf * 0.3 + around * wa * 0.18 ) * strength + around * ( fine - 0.5 ) * 0.25 * strength );

	float facing = clamp( dot( -rd, n ), 0.0, 1.0 );
	float fresnel = 0.02 + 0.98 * pow( 1.0 - facing, 5.0 );

	// what lies behind, bent: the refracted ray leaves the far side further over the more water there is, and
	// the colours part a little
	vec2 toCopy = d.texMat0[0].zw;
	vec2 here = gl_FragCoord.xy * d.texMat0[0].xy;
	// (the bend as a slope, times how far behind the water what it shows is, seen from the eye)
	vec3 bent = refract( rd, n, ETA ) - rd;
	vec2 shift = vec2( -dot( bent, v_left ), -dot( bent, v_up ) ) * d.texMat0[1].x * ( 0.5 + 0.5 * thickness / max( r, 0.3 ) )
		/ max( t, 8.0 ) * d.texMat1[0].zw;
	// no further than a few times its own width on the picture: what lies right behind a thin jet is near it
	vec2 reach = 2.5 * r / max( t, 8.0 ) * d.texMat1[0].zw;
	shift = clamp( shift, -reach, reach ) * toCopy;
	vec3 behind;
	behind.r = texture( SCENE( d ), clamp( here + shift * 0.97, vec2( 0.0 ), toCopy ) ).r;
	behind.g = texture( SCENE( d ), clamp( here + shift, vec2( 0.0 ), toCopy ) ).g;
	behind.b = texture( SCENE( d ), clamp( here + shift * 1.03, vec2( 0.0 ), toCopy ) ).b;
	// clear water takes the red out first, over a way of some units hardly any. A round body is a lens: its middle
	// gathers the light behind it into a bright band, towards its edges the rays turn away to darker places, which
	// is what shows a glass rod against a plain wall.
	behind *= exp( -thickness * d.texMat0[1].y * vec3( 1.0, 0.45, 0.3 ) );
	behind *= mix( 0.72, 1.0, smoothstep( 0.0, 0.5, facing ) ) * ( 1.0 + 0.3 * pow( facing, 6.0 ) );

	// what lies in front, mirrored: where the mirrored ray goes far off on the picture, the light of the level
	// where that is not on the picture
	vec3 mirrored = reflect( rd, n );
	vec3 sky = ambient * 0.9 + directed * 0.25;
	vec3 seen = OnScreen( tr.mvp, hit + mirrored * 2048.0 );
	vec3 reflection = sky;
	if ( seen.z > 0.0 ) {
		vec2 edge = min( seen.xy, 1.0 - seen.xy );
		float inside = clamp( min( edge.x, edge.y ) * 12.0, 0.0, 1.0 );
		reflection = mix( sky, texture( SCENE( d ), clamp( seen.xy, 0.0, 1.0 ) * toCopy ).rgb, inside );
	}

	// The light of the level, and a light from above that every room has in some way. The highlight of a round
	// body that runs along the jet is long along it and sharp around it (anisotropic, Ashikhmin and Shirley): a line
	// along the jet where its surface faces halfway between the light and the eye. On top some of the light caught
	// inside the water.
	vec3 view = -rd;
	vec3 l = normalize( v_lightDir.xyz );
	vec3 bi = normalize( cross( along, n ) );
	float facingLight = clamp( dot( n, l ) * 0.5 + 0.5, 0.0, 1.0 );
	vec3 highlight = directed * Shine( n, along, bi, normalize( l + view ), 1.6 )
		+ ( ambient + directed * 0.4 ) * Shine( n, along, bi, normalize( normalize( vec3( 0.25, 0.15, 1.0 ) ) + view ), 0.7 );
	vec3 inner = ( ambient + directed * facingLight ) * vec3( 0.6, 0.82, 0.92 ) * 0.06 * thickness / max( r, 0.5 );

	// Air in the water: a water gun's jet is a little milky from the start, more where it tears, and white foam
	// rides on it there
	float milk = 0.28 * breakup * ( 0.5 + Noise3( q * vec3( 1.3, 1.1, 0.9 ) + 5.0 ) );
	float foam = breakup * smoothstep( 0.6, 0.8, Noise3( q * vec3( 2.2, 2.0, 1.3 ) + 37.0 ) ) * 0.85;
	vec3 white = ( ambient + directed * ( 0.35 + 0.5 * facingLight ) ) * 0.95;

	vec3 color = behind * ( 1.0 - fresnel ) + reflection * fresnel + highlight + inner;
	color = mix( color, white, clamp( milk + foam, 0.0, 1.0 ) );
	o_color = vec4( color, cover );

	float ahead = dot( hit - eye, cross( v_left, v_up ) );
	gl_FragDepth = clamp( d.texMat1[0].x / max( ahead, 1.0 ) - d.texMat1[0].y, 0.0, 1.0 );
}
