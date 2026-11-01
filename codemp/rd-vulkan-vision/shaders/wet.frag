#version 460

// A wet place of RE_AddWet (VK_DrawWet() in vk_post.cpp, the quad of wet.vert): the level under the pixel from the
// depth of the scene, and if it lies on the plane of the place and inside its ragged edge, it is drawn wet from the
// copy of the frame. Wet ground is darker, as its pores are full of water and the light goes into it instead of
// back out (Lagarde, "Water drop 3b: physically based wet surfaces", 2013: diffuse times lerp( 1, 0.2, porosity )
// at full wetness, about 0.55 for stone and plaster), a little richer in colour, and a film of water on it shines
// (Fresnel with F0 0.02 of water). Where water stands (a puddle on a floor) the surface is flat water: it mirrors
// the room, found by a march of the mirrored ray through the depth of the scene and the light of the level where
// that leaves the picture, and rings run over it from where the jet lands and from the drops that fall back. On a
// wall the water runs down in streaks. The edge is noise of the surface itself, and as wet shrinks it dries from
// the thin parts in. What is laid over the frame is the full look of wet ground, by how wet the pixel is.

#include "dialect.glsl"
#include "noise.glsl"

BINDING( 1, 0 ) uniform sampler2D u_image;		// the copy of the frame
BINDING( 2, 0 ) uniform sampler2D u_depth;		// the depth of the scene, read pixel for pixel

#ifdef VK_GLES
uniform vec4 u_wet[202];
#define W	u_wet
#else
BINDING( 0, 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[202];
} globals;
#define W	globals.wet
#endif

VARYING( 0 ) flat in uint v_spot;
layout( location = 0 ) out vec4 o_color;

#define DARKEN		0.55	// of the colour of soaked ground
#define UNDER		0.36	//	and of the ground under standing water
#define MIRROR		0.2		// of the room in standing water seen from above, on top of Fresnel
#define MARCH		32		// steps of the mirrored ray, each longer than the one before: about 2000 units
#define REFINE		5		//	and halvings where it went behind the scene

// how far in front of the eye the scene is at a pixel of the target
float SceneDistance( vec2 pixel ) {
	ivec2 size = textureSize( u_depth, 0 );
	float depth = texelFetch( u_depth, clamp( ivec2( pixel ), ivec2( 0 ), size - 1 ), 0 ).r;

	// (both are below zero: x / ( depth - 1 ) and a bit)
	return W[6].w / min( depth + W[7].w, -1e-7 );
}

// a place of the level as a pixel of the target, z: how far in front of the eye, -1 behind it
vec3 OnTarget( vec3 p ) {
	vec4 clip = mat4( W[0], W[1], W[2], W[3] ) * vec4( p, 1.0 );
	vec4 rect = W[8];

	if ( clip.w < 1.0 ) {
		return vec3( 0.0, 0.0, -1.0 );
	}
	vec2 ndc = clip.xy / clip.w;
	return vec3( rect.x + ( ndc.x * 0.5 + 0.5 ) * rect.z, rect.y + ( 0.5 - ndc.y * 0.5 ) * rect.w, dot( p - W[4].xyz, W[5].xyz ) );
}

// what the ray from o along dir meets in the picture, fallback where it leaves it or finds nothing
vec3 Mirror( vec3 o, vec3 dir, vec3 fallback ) {
	vec4 rect = W[8];
	vec3 q = o;
	vec3 last = o;
	float stride = 2.0;

	for ( int i = 0; i < MARCH; i++ ) {
		last = q;
		q += dir * stride;
		stride *= 1.18;
		vec3 s = OnTarget( q );
		if ( s.z <= 0.0 || s.x < rect.x || s.y < rect.y || s.x >= rect.x + rect.z || s.y >= rect.y + rect.w ) {
			break;
		}
		float behind = s.z - SceneDistance( s.xy );
		// behind the scene by more than a step: it passed behind something in front, that is no hit
		if ( behind <= 0.0 || behind > stride * 2.0 + 6.0 ) {
			continue;
		}
		vec3 a = last;
		vec3 b = q;
		for ( int k = 0; k < REFINE; k++ ) {
			vec3 m = ( a + b ) * 0.5;
			vec3 sm = OnTarget( m );
			if ( sm.z - SceneDistance( sm.xy ) > 0.0 ) {
				b = m;
			} else {
				a = m;
			}
		}
		s = OnTarget( b );
		// soft towards the edges of the view, where a ray that leaves it finds nothing
		vec2 edge = min( s.xy - rect.xy, rect.xy + rect.zw - s.xy ) / ( rect.zw * 0.08 );
		float fade = clamp( min( edge.x, edge.y ), 0.0, 1.0 );
		return mix( fallback, texture( u_image, s.xy * W[9].xy ).rgb, fade );
	}
	return fallback;
}

// rings where drops fall into standing water, in cells of the surface: a ring each that grows and flattens, the
// slope it gives the water there along the surface (uv)
vec2 Rings( vec2 uv, float time ) {
	const float CELL = 7.0;
	vec2 cell = floor( uv / CELL );
	vec2 slope = vec2( 0.0 );

	for ( int y = -1; y <= 1; y++ ) {
		for ( int x = -1; x <= 1; x++ ) {
			vec2 c = cell + vec2( float( x ), float( y ) );
			uvec3 h = Pcg3( uvec3( ivec3( ivec2( c ), 41 ) ) );
			vec3 r = vec3( h & 0xffffu ) / 65535.0;
			float age = fract( time * ( 0.9 + 0.5 * r.z ) + r.z * 7.0 );
			vec2 centre = ( c + 0.15 + 0.7 * r.xy ) * CELL;
			vec2 d = uv - centre;
			float dist = length( d );
			float off = dist - age * CELL * 0.75;
			float ring = ( 1.0 - age ) * ( 1.0 - age ) * ( 1.0 - smoothstep( 0.0, 1.6, abs( off ) ) );
			slope += d / max( dist, 1e-3 ) * cos( off * 3.0 ) * ring;
		}
	}
	return slope;
}

void main() {
	uint row = 10u + v_spot * 6u;
	vec3 centre = W[row].xyz;
	float radius = max( W[row].w, 1.0 );
	vec3 n = W[row + 1u].xyz;
	float wet = W[row + 1u].w;
	vec3 hit = W[row + 2u].xyz;
	float puddle = W[row + 2u].w;
	vec3 ambient = W[row + 3u].rgb;
	float flow = W[row + 3u].w;
	vec3 directed = W[row + 4u].rgb;
	float seed = W[row + 4u].w;
	vec3 lightDir = W[row + 5u].xyz;
	vec3 eye = W[4].xyz;
	float time = W[9].z;
	vec4 rect = W[8];

	// the level under the pixel, and the way its surface faces (before anything is thrown away: the slopes of a
	// pixel need its neighbours)
	vec2 ndc = vec2( ( gl_FragCoord.x - rect.x ) / rect.z * 2.0 - 1.0, 1.0 - ( gl_FragCoord.y - rect.y ) / rect.w * 2.0 );
	vec3 ray = W[5].xyz - W[6].xyz * ( ndc.x * W[4].w ) + W[7].xyz * ( ndc.y * W[5].w );
	vec3 p = eye + ray * SceneDistance( gl_FragCoord.xy );
	vec3 surface = cross( dFdx( p ), dFdy( p ) );

	vec3 rel = p - centre;
	float off = dot( rel, n );
	float thick = 3.0 + radius * 0.12;
	if ( abs( off ) > thick ) {
		discard;
	}
	surface = ( dot( surface, surface ) > 1e-10 ) ? normalize( surface ) : n;
	surface = ( dot( surface, eye - p ) < 0.0 ) ? -surface : surface;
	float along = dot( surface, n );
	if ( along < 0.6 ) {
		discard;
	}

	// two directions on the surface: on a wall down it and across, on a floor turned by the seed
	vec3 down = vec3( 0.0, 0.0, -1.0 ) + n * n.z;
	bool wall = abs( n.z ) < 0.5;
	vec3 t, b;
	if ( wall && dot( down, down ) > 1e-4 ) {
		b = normalize( down );
		t = cross( n, b );
	} else {
		t = normalize( cross( n, abs( n.z ) < 0.9 ? vec3( 0.0, 0.0, 1.0 ) : vec3( 1.0, 0.0, 0.0 ) ) );
		b = cross( n, t );
		float a = seed * 6.2831853;
		vec3 t2 = t * cos( a ) + b * sin( a );
		b = cross( n, t2 );
		t = t2;
	}
	vec3 planar = rel - n * off;
	vec2 uv = vec2( dot( planar, t ), dot( planar, b ) );
	vec2 shift = vec2( seed * 311.7, seed * 173.3 );
	float r = length( uv ) / radius;

	// the ragged edge, in units of the surface, coarse and fine
	float edge = r + ( Fbm( uv * 0.06 + shift ) - 0.47 ) * 0.9 + ( Noise( uv * 0.45 + shift * 1.7 ) - 0.5 ) * 0.14;
	float mask = 1.0 - smoothstep( wet - 0.1, wet + 0.02, edge );
	if ( wall ) {
		// Rivulets that run down from the wet patch: in strips of the wall across, a strip has one or none, where in
		// the strip, how wide and how far down its hash says. It wanders a little sideways on its way and ends in a
		// drop that is a little wider. They grow with the patch and dry with it.
		const float STRIP = 4.5;
		float streak = 0.0;
		for ( int k = -1; k <= 1; k++ ) {
			float strip = floor( uv.x / STRIP ) + float( k );
			uvec3 hs = Pcg3( uvec3( ivec3( int( strip ), int( seed * 4096.0 ), 23 ) ) );
			vec3 rs = vec3( hs & 0xffffu ) / 65535.0;
			if ( rs.x > 0.5 || abs( strip * STRIP + STRIP * 0.5 ) > radius * 0.8 ) {
				continue;
			}
			float reach = radius * wet * ( 0.6 + 1.2 * rs.y );
			float x = ( strip + 0.2 + 0.6 * rs.z ) * STRIP + ( Noise( vec2( uv.y * 0.07, strip * 3.1 + seed * 9.0 ) ) - 0.5 ) * 2.4;
			float width = 0.45 + 0.6 * fract( rs.y * 7.3 );
			float tip = smoothstep( reach - 3.0, reach - 0.5, uv.y ) * ( 1.0 - smoothstep( reach - 0.5, reach + 0.6, uv.y ) );
			float along_ = smoothstep( -radius * 0.1, radius * 0.25, uv.y ) * ( 1.0 - smoothstep( reach - 0.6, reach + 0.6, uv.y ) );
			float across = 1.0 - smoothstep( width * ( 1.0 + 1.2 * tip ), width * ( 1.0 + 1.2 * tip ) + 0.5, abs( uv.x - x ) );
			streak = max( streak, across * max( along_, tip ) );
		}
		mask = max( mask, streak * smoothstep( 0.0, 0.1, wet ) );
	}
	mask *= smoothstep( 0.6, 0.85, along ) * ( 1.0 - smoothstep( thick * 0.6, thick, abs( off ) ) );
	if ( mask <= 0.002 ) {
		discard;
	}

	vec2 here = gl_FragCoord.xy * W[9].xy;
	vec3 view = normalize( p - eye );
	vec3 base = texture( u_image, here ).rgb;

	// soaked ground: darker, the colours a little stronger
	vec3 soaked = base * DARKEN;
	float grey = dot( soaked, vec3( 0.299, 0.587, 0.114 ) );
	soaked = max( mix( vec3( grey ), soaked, 1.2 ), 0.0 );

	// the film of water on it: not a mirror, the surface under it is rough, so the shine of the light of the level
	// is broad and weak and the room is only a glow in it
	vec3 l = normalize( lightDir );
	vec3 bumpy = normalize( n + ( t * ( Noise( uv * 0.8 + shift ) - 0.5 ) + b * ( Noise( uv * 0.8 + shift + 17.0 ) - 0.5 ) ) * 0.35 );
	float facing = clamp( dot( -view, bumpy ), 0.0, 1.0 );
	float fresnel = 0.02 + 0.98 * pow( 1.0 - facing, 5.0 );
	vec3 half_ = normalize( l - view );
	vec3 sky = ambient * 0.8 + directed * 0.3;
	vec3 film = sky * fresnel * 0.6 + directed * pow( clamp( dot( bumpy, half_ ), 0.0, 1.0 ), 90.0 ) * 1.4 * ( 0.04 + 0.96 * pow( 1.0 - clamp( dot( half_, -view ), 0.0, 1.0 ), 5.0 ) );
	vec3 color = soaked + film;

	// standing water, on a floor that is flat there
	float pool = 0.0;
	if ( puddle > 0.0 && !wall ) {
		float pEdge = r + ( Fbm( uv * 0.05 + shift + 40.0 ) - 0.47 ) * 0.6;
		pool = ( 1.0 - smoothstep( puddle - 0.08, puddle, pEdge ) ) * smoothstep( 0.85, 0.95, n.z ) * smoothstep( 0.9, 0.97, along );
	}
	if ( pool > 0.001 ) {
		// the rings: the ones of the jet run out from where it lands, the ones of the drops are around it
		vec3 fromHit = planar - ( ( hit - centre ) - n * dot( hit - centre, n ) );
		vec2 h = vec2( dot( fromHit, t ), dot( fromHit, b ) );
		float hr = length( h );
		// (the rings of a jet are not even: the water lands a little here and a little there, so their phase wanders
		// with the noise of where they are and of the time, and close to it the water is all chop)
		float wave = hr * 0.75 - time * 9.0 + ( Noise( uv * 0.12 + vec2( time * 0.7, 0.0 ) ) - 0.5 ) * 5.0;
		float calm = exp( -hr / ( radius * 0.28 ) ) * flow;
		vec2 slope = h / max( hr, 1e-3 ) * cos( wave ) * calm * 0.2;
		float chop = flow * ( 1.0 - smoothstep( 2.0, 7.0, hr ) );
		slope += ( vec2( Noise( uv * 0.9 + time * 6.0 ), Noise( uv * 0.9 - time * 6.0 + 13.0 ) ) - 0.5 ) * chop * 0.8;
		slope += Rings( uv, time ) * 0.12 * flow * exp( -hr / ( radius * 0.6 ) );
		vec3 water = normalize( n - t * slope.x - b * slope.y );

		float wf = clamp( dot( -view, water ), 0.0, 1.0 );
		float wFresnel = 0.02 + 0.98 * pow( 1.0 - wf, 5.0 );
		vec3 mirrored = reflect( view, water );
		vec3 fallback = ambient * 0.75 + directed * 0.45 * pow( clamp( dot( mirrored, l ), 0.0, 1.0 ), 2.0 );
		vec3 seen = Mirror( p + n * 0.5, mirrored, fallback );
		// the ground under the water, bent a little by the rings
		vec2 bend = vec2( -dot( water - n, W[6].xyz ), -dot( water - n, W[7].xyz ) ) * 0.015;
		vec3 under = texture( u_image, here + bend ).rgb * UNDER;
		vec3 glint = directed * pow( clamp( dot( water, half_ ), 0.0, 1.0 ), 700.0 ) * 6.0 * ( 0.02 + 0.98 * pow( 1.0 - clamp( dot( half_, -view ), 0.0, 1.0 ), 5.0 ) );
		// (more of the mirror than Fresnel's law gives: a puddle of a game reads as water by what it mirrors, and
		// the mirror of a real one is seen against ground that is darker than the floors of most levels)
		vec3 pond = mix( under, seen, clamp( wFresnel * 2.0 + MIRROR, 0.0, 0.92 ) ) + glint;
		color = mix( color, pond, pool );
	}

	o_color = vec4( color, mask );
}
