#version 460

// The wet places of RE_AddWet (VK_DrawWet() in vk_post.cpp, the quads of wet.vert): the level under the pixel from the
// depth of the scene, and if it lies on the plane of places and inside their ragged edge, it is drawn wet from the
// copy of the frame. Wet ground is darker, as its pores are full of water and the light goes into it instead of
// back out (Lagarde, "Water drop 3b: physically based wet surfaces", 2013: diffuse times lerp( 1, 0.2, porosity )
// at full wetness, about 0.55 for stone and plaster), a little richer in colour, and a film of water on it shines
// (Fresnel with F0 0.02 of water). Where water stands (a puddle on a floor) the surface is flat water: it mirrors
// the room: at the floor of the puddle view what that view of the level mirrored there shows, elsewhere what a
// march of the mirrored ray through the depth of the scene finds and the light of the level where that leaves the
// picture, and rings run over it from where the jet lands and from the drops that fall back. On a wall the water
// runs down in streaks. The edge is noise of the surface itself, and as wet shrinks it dries from the thin parts
// in. What is laid over the frame is the full look of wet ground, by how wet the pixel is.
//
// Places that touch are one wet surface: a pixel is drawn once, by the first place whose ball holds it, with the
// edges of all places there joined by a smooth minimum, so two puddles flow into one and mirror the room once. The
// noise lies on the surface of the level, not on a place, so it goes on from one place into the next.

#include "dialect.glsl"
#include "noise.glsl"

BINDING( 1, 0 ) uniform sampler2D u_image;		// the copy of the frame
BINDING( 2, 0 ) uniform sampler2D u_depth;		// the depth of the scene, read pixel for pixel
BINDING( 3, 0 ) uniform sampler2D u_reflect;	// the view mirrored at the floor of the puddles (VK_BeginPuddle)

// VK_WET_VEC4S of vk_local.h
#ifdef VK_GLES
uniform vec4 u_wet[779];
#define W	u_wet
#else
BINDING( 0, 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
} globals;
#define W	globals.wet
#endif

VARYING( 0 ) flat in uint v_spot;
layout( location = 0 ) out vec4 o_color;

#define DARKEN		0.55	// of the colour of soaked ground
#define UNDER		0.36	//	and of the ground under standing water
#define MIRROR		0.2		// of the room in standing water seen from above, on top of Fresnel,
#define MIRROR_VIEW	0.4		//	and where the puddle view gives the whole room, not only what is on the screen
#define MARCH		32		// steps of the mirrored ray, each longer than the one before: about 2000 units
#define REFINE		5		//	and halvings where it went behind the scene
#define JOIN		8.0		// units over which the edges of two places run into one
#define POOL		0.25	// of the water of the places added up where it stands (one full place: at 0.7 of its radius)
#define FAR			1e6
#define WET_RING		3		// the kinds of places of tr_types.h
#define WET_FOOT_LEFT	1
#define WET_FOOT_RIGHT	2
#define WET_TRACK		4
#define WET_SNOW		5
#define SNOW_ALBEDO		vec3( 0.93, 0.95, 0.98 )
#define SNOW_HOLLOW		vec3( 0.72, 0.81, 0.95 )

// how far in front of the eye the scene is at a pixel of the target
float SceneDistance( vec2 pixel ) {
	ivec2 size = textureSize( u_depth, 0 );
	float depth = texelFetch( u_depth, clamp( ivec2( pixel ), ivec2( 0 ), size - 1 ), 0 ).r;

	// (both are below zero: x / ( depth - 1 ) and a bit)
	return W[6].w / min( depth + W[7].w, -1e-7 );
}

// the point of the level at the middle of a pixel of the target
vec3 ScenePoint( vec2 pixel ) {
	vec4 rect = W[8];
	vec2 ndc = vec2( ( pixel.x - rect.x ) / rect.z * 2.0 - 1.0, 1.0 - ( pixel.y - rect.y ) / rect.w * 2.0 );
	vec3 ray = W[5].xyz - W[6].xyz * ( ndc.x * W[4].w ) + W[7].xyz * ( ndc.y * W[5].w );

	return W[4].xyz + ray * SceneDistance( pixel );
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

// the smaller of two distances to an edge, rounded where they are near each other (Quilez, polynomial)
float SoftMin( float a, float b, float k ) {
	float h = max( k - abs( a - b ), 0.0 ) / k;
	return min( a, b ) - h * h * k * 0.25;
}

// how far a point of the surface is outside the print of a foot, in units: q along the foot (toes ahead) and across
// it (towards the inside of the foot), the foot length / 2: the ball and toes as an ellipse, the heel as a circle,
// joined at the narrow arch
float Footprint( vec2 q, float half_ ) {
	vec2 ball = ( q - vec2( 0.3, -0.06 ) * half_ ) / ( vec2( 0.62, 0.36 ) * half_ );
	float sole = ( length( ball ) - 1.0 ) * 0.36 * half_;
	float heel = length( q - vec2( -0.62, -0.02 ) * half_ ) - 0.27 * half_;
	return SoftMin( sole, heel, 0.2 * half_ );
}

// whether the point p of the level with its surface facing that way belongs to place j: on its plane and in the ball
// of its quad (wet.vert)
bool InPlace( uint j, vec3 p, vec3 surface ) {
	uint row = 11u + j * 6u;
	vec3 rel = p - W[row].xyz;
	float radius = max( W[row].w, 1.0 );
	vec3 n = W[row + 1u].xyz;

	return abs( dot( rel, n ) ) <= 3.0 + radius * 0.12 && dot( surface, n ) >= 0.6 && length( rel ) < W[row].w * 2.0 + 8.0;
}

void main() {
	uint count = uint( W[9].w );
	vec3 eye = W[4].xyz;
	float time = W[9].z;
	vec4 rect = W[8];

	// The level under the pixel, and the way its surface faces: from the neighbours on the side where the depth goes
	// on smoothly. (The slopes of the block of pixels mix the floor with what stands on it, a player, and a ring of
	// pixels around him would not be on the floor.)
	vec3 p = ScenePoint( gl_FragCoord.xy );
	vec3 pl = ScenePoint( gl_FragCoord.xy - vec2( 1.0, 0.0 ) ), pr = ScenePoint( gl_FragCoord.xy + vec2( 1.0, 0.0 ) );
	vec3 pd = ScenePoint( gl_FragCoord.xy - vec2( 0.0, 1.0 ) ), pu = ScenePoint( gl_FragCoord.xy + vec2( 0.0, 1.0 ) );
	vec3 dx = ( abs( dot( pr - p, W[5].xyz ) ) < abs( dot( p - pl, W[5].xyz ) ) ) ? pr - p : p - pl;
	vec3 dy = ( abs( dot( pu - p, W[5].xyz ) ) < abs( dot( p - pd, W[5].xyz ) ) ) ? pu - p : p - pd;
	vec3 surface = cross( dx, dy );
	surface = ( dot( surface, surface ) > 1e-10 ) ? normalize( surface ) : W[11u + v_spot * 6u + 1u].xyz;
	surface = ( dot( surface, eye - p ) < 0.0 ) ? -surface : surface;

	// the first place that holds the pixel draws it, the others leave it
	if ( !InPlace( v_spot, p, surface ) ) {
		discard;
	}
	for ( uint j = 0u; j < v_spot; j++ ) {
		if ( InPlace( j, p, surface ) ) {
			discard;
		}
	}

	// two directions on the surface of the level: on a wall down it and across, on a floor the axes of the level
	vec3 n = W[11u + v_spot * 6u + 1u].xyz;
	bool wall = abs( n.z ) < 0.5;
	vec3 down = vec3( 0.0, 0.0, -1.0 ) + n * n.z;
	vec3 t, b;
	if ( wall && dot( down, down ) > 1e-4 ) {
		b = normalize( down );
		t = cross( n, b );
	} else {
		t = normalize( vec3( 1.0, 0.0, 0.0 ) - n * n.x );
		b = cross( n, t );
	}
	vec2 uv = vec2( dot( p, t ), dot( p, b ) );
	float along = dot( surface, n );

	// all places here: their edges joined, their light and size by how near they are, the slopes of their water
	float edgeWet = FAR, sizeWet = 0.0, placeWeights = 0.0, weights = 0.0, water = 0.0, fade = 0.0, streak = 0.0, prints = 0.0;
	float flowNear = 0.0, snow = 0.0, snowHeight = 0.0, snowMelt = 0.0;
	vec2 snowAt = vec2( 0.0 );
	vec3 snowN = vec3( 0.0, 0.0, 1.0 ), snowT = vec3( 1.0, 0.0, 0.0 ), snowB = vec3( 0.0, 1.0, 0.0 );
	vec3 ambient = vec3( 0.0 ), directed = vec3( 0.0 ), lightDir = vec3( 0.0 );
	vec2 slope = vec2( 0.0 );
	for ( uint j = 0u; j < count; j++ ) {
		if ( !InPlace( j, p, surface ) ) {
			continue;
		}
		uint row = 11u + j * 6u;
		vec3 centre = W[row].xyz;
		float radius = max( W[row].w, 1.0 );
		vec3 nj = W[row + 1u].xyz;
		float wet = W[row + 1u].w;
		vec3 hit = W[row + 2u].xyz;
		float puddle = W[row + 2u].w;
		float flow = W[row + 3u].w;
		float seed = W[row + 4u].w;
		int kind = int( W[row + 5u].w + 0.5 );
		vec3 rel = p - centre;
		float off = dot( rel, nj );
		float thick = 3.0 + radius * 0.12;
		vec3 planar = rel - nj * off;
		float r = length( planar );
		float w = max( 1.0 - r / ( radius * 1.5 ), 0.02 );

		weights += w;
		ambient += W[row + 3u].rgb * w;
		directed += W[row + 4u].rgb * w;
		lightDir += W[row + 5u].xyz * w;
		fade = max( fade, smoothstep( 0.6, 0.85, dot( surface, nj ) ) * ( 1.0 - smoothstep( thick * 0.6, thick, abs( off ) ) ) );

		if ( kind == WET_RING ) {
			// a ring that runs out over standing water from where something came into it, slower as it goes: a few
			// crests that flatten (flow: how far through its life, wet: how strong)
			float front = radius * ( 1.0 - ( 1.0 - flow ) * ( 1.0 - flow ) );
			float d = r - front;
			float width = 2.5 + 5.0 * flow;
			vec2 out_ = vec2( dot( planar, t ), dot( planar, b ) ) / max( r, 1e-3 );
			slope += out_ * cos( d * 1.3 ) * exp( -d * d / ( width * width ) ) * wet * ( 1.0 - flow ) * ( 1.0 - flow ) * 0.9;
			continue;
		}
		if ( kind == WET_SNOW ) {
			// A splat of snow: a lump where it is now and, on a wall, a smear above it from where it hit and slid down
			// from (hit). Thick in the middle and in clumps, ragged at the edge; it melts from the thin parts in as wet
			// goes from 1 to 0. The splat that is thickest here draws the pixel.
			vec2 local = vec2( dot( planar, t ), dot( planar, b ) );
			vec3 h0 = hit - centre;
			h0 -= nj * dot( h0, nj );
			vec2 from = vec2( dot( h0, t ), dot( h0, b ) );
			vec2 seg = -from;
			float len2 = dot( seg, seg );
			float k = len2 > 1e-4 ? clamp( dot( local - from, seg ) / len2, 0.0, 1.0 ) : 1.0;
			float dSmear = length( local - from - seg * k ) / ( radius * ( 0.3 + 0.45 * k ) );
			float d = min( length( local * vec2( 1.0, 0.85 ) ) / radius, dSmear );
			vec2 q = local + seed * 97.0;
			float height = ( 1.0 - d ) * 1.25 + ( Fbm( q * 0.45 ) - 0.5 ) * 0.9 + ( Noise( q * 1.7 ) - 0.5 ) * 0.12;
			float melt = ( 1.0 - wet ) * 1.15;
			float cover = smoothstep( melt, melt + 0.08, height );
			if ( cover > snow ) {
				snow = cover;
				snowHeight = height;
				snowMelt = melt;
				snowAt = q;
				snowN = nj;
				snowT = t;
				snowB = b;
			}
			continue;
		}
		if ( kind == WET_TRACK ) {
			// a piece of the track of a wet tire, from 2 centre - hit to hit, half as wide as seed: the knobs of the
			// tread wetter than between them, drying from the edge in
			if ( !wall ) {
				vec3 b_ = hit - nj * dot( hit - centre, nj );
				vec3 a_ = centre * 2.0 - b_;
				vec3 ab = b_ - a_;
				float len = length( ab );
				vec3 dir = ab / max( len, 1e-3 );
				vec3 ap = rel + centre - a_;
				ap -= nj * dot( ap, nj );
				float along = dot( ap, dir );
				float side = dot( ap, cross( nj, dir ) );
				float d = length( ap - dir * clamp( along, 0.0, len ) ) - seed;
				float knob = step( 0.32, fract( along / 3.1 + ( side > 0.0 ? 0.5 : 0.0 ) ) );
				d += ( Noise( uv * 1.2 ) - 0.5 ) * 0.3 * seed + ( 1.0 - wet ) * 0.6 * seed;
				prints = max( prints, ( 1.0 - smoothstep( -0.15 * seed, 0.1 * seed, d ) ) * smoothstep( 0.0, 0.15, wet ) * ( 0.55 + 0.45 * knob ) );
			}
			continue;
		}
		if ( kind == WET_FOOT_LEFT || kind == WET_FOOT_RIGHT ) {
			// the print of a wet foot: it dries from its edge in, the thin arch first
			if ( !wall ) {
				float a = seed * 6.2831853;
				vec3 ahead = vec3( cos( a ), sin( a ), 0.0 );
				ahead = normalize( ahead - nj * dot( ahead, nj ) );
				vec3 inside = cross( nj, ahead ) * ( kind == WET_FOOT_LEFT ? -1.0 : 1.0 );
				float d = Footprint( vec2( dot( planar, ahead ), dot( planar, inside ) ), radius );
				d += ( Noise( uv * 1.4 + seed * 50.0 ) - 0.5 ) * 0.14 * radius + ( 1.0 - wet ) * 0.3 * radius;
				prints = max( prints, ( 1.0 - smoothstep( -0.1 * radius, 0.06 * radius, d ) ) * smoothstep( 0.0, 0.15, wet ) );
			}
			continue;
		}
		edgeWet = SoftMin( edgeWet, r - radius * wet, JOIN );
		sizeWet += radius * w;
		placeWeights += w;

		if ( wall ) {
			// Rivulets that run down from the wet patch: in strips of the wall across, a strip has one or none, where
			// in the strip, how wide and how far down its hash says. It wanders a little sideways on its way and ends
			// in a drop that is a little wider. They grow with the patch and dry with it.
			const float STRIP = 4.5;
			vec2 local = vec2( dot( planar, t ), dot( planar, b ) );
			for ( int k = -1; k <= 1; k++ ) {
				float strip = floor( local.x / STRIP ) + float( k );
				uvec3 hs = Pcg3( uvec3( ivec3( int( strip ), int( seed * 4096.0 ), 23 ) ) );
				vec3 rs = vec3( hs & 0xffffu ) / 65535.0;
				if ( rs.x > 0.5 || abs( strip * STRIP + STRIP * 0.5 ) > radius * 0.8 ) {
					continue;
				}
				float reach = radius * wet * ( 0.6 + 1.2 * rs.y );
				float x = ( strip + 0.2 + 0.6 * rs.z ) * STRIP + ( Noise( vec2( local.y * 0.07, strip * 3.1 + seed * 9.0 ) ) - 0.5 ) * 2.4;
				float width = 0.45 + 0.6 * fract( rs.y * 7.3 );
				float tip = smoothstep( reach - 3.0, reach - 0.5, local.y ) * ( 1.0 - smoothstep( reach - 0.5, reach + 0.6, local.y ) );
				float runs = smoothstep( -radius * 0.1, radius * 0.25, local.y ) * ( 1.0 - smoothstep( reach - 0.6, reach + 0.6, local.y ) );
				float across = 1.0 - smoothstep( width * ( 1.0 + 1.2 * tip ), width * ( 1.0 + 1.2 * tip ) + 0.5, abs( local.x - x ) );
				streak = max( streak, across * max( runs, tip ) * smoothstep( 0.0, 0.1, wet ) );
			}
		} else if ( puddle > 0.0 ) {
			// standing water adds up: a bump for each place, high in its middle, so between two near ones it fills
			float x = min( r / radius, 1.0 );
			water += puddle * ( 1.0 - x * x ) * ( 1.0 - x * x );

			// the rings of the jet run out from where it lands, and close to it the water is all chop (they are not
			// even: the water lands a little here and a little there, so their phase wanders with the noise of where
			// they are and of the time)
			vec3 fromHit = rel - ( hit - centre );
			vec2 h = vec2( dot( fromHit, t ), dot( fromHit, b ) );
			float hr = length( h );
			float wave = hr * 0.75 - time * 9.0 + ( Noise( uv * 0.12 + vec2( time * 0.7, 0.0 ) ) - 0.5 ) * 5.0;
			float calm = exp( -hr / ( radius * 0.28 ) ) * flow;
			float chop = flow * ( 1.0 - smoothstep( 2.0, 7.0, hr ) );
			slope += h / max( hr, 1e-3 ) * cos( wave ) * calm * 0.2;
			slope += ( vec2( Noise( uv * 0.9 + time * 6.0 ), Noise( uv * 0.9 - time * 6.0 + 13.0 ) ) - 0.5 ) * chop * 0.8;
			flowNear = max( flowNear, flow * exp( -hr / ( radius * 0.6 ) ) );
		}
	}
	sizeWet = placeWeights > 0.0 ? sizeWet / placeWeights : 1.0;
	ambient /= weights;
	directed /= weights;

	// the ragged edge, in units of the surface, coarse and fine
	float edge = edgeWet + ( ( Fbm( uv * 0.06 ) - 0.47 ) * 0.9 + ( Noise( uv * 0.45 + 31.0 ) - 0.5 ) * 0.14 ) * sizeWet;
	float mask = 1.0 - smoothstep( -0.1 * sizeWet, 0.02 * sizeWet, edge );
	mask = max( max( mask, streak ), prints * 0.85 );
	mask *= fade;
	snow *= fade;
	if ( max( mask, snow ) <= 0.002 ) {
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
	vec3 bumpy = normalize( n + ( t * ( Noise( uv * 0.8 ) - 0.5 ) + b * ( Noise( uv * 0.8 + 17.0 ) - 0.5 ) ) * 0.35 );
	float facing = clamp( dot( -view, bumpy ), 0.0, 1.0 );
	float fresnel = 0.02 + 0.98 * pow( 1.0 - facing, 5.0 );
	vec3 half_ = normalize( l - view );
	vec3 sky = ambient * 0.8 + directed * 0.3;
	vec3 film = sky * fresnel * 0.6 + directed * pow( clamp( dot( bumpy, half_ ), 0.0, 1.0 ), 90.0 ) * 1.4 * ( 0.04 + 0.96 * pow( 1.0 - clamp( dot( half_, -view ), 0.0, 1.0 ), 5.0 ) );
	vec3 color = soaked + film;

	// standing water, on a floor that is flat there
	float pool = 0.0;
	if ( water > 0.0 ) {
		float level = water + ( Fbm( uv * 0.05 + 40.0 ) - 0.47 ) * 0.3;
		pool = smoothstep( POOL - 0.03, POOL + 0.02, level ) * smoothstep( 0.85, 0.95, n.z ) * smoothstep( 0.9, 0.97, along );
	}
	if ( pool > 0.001 ) {
		slope += Rings( uv, time ) * 0.12 * flowNear;
		vec3 water = normalize( n - t * slope.x - b * slope.y );

		float wf = clamp( dot( -view, water ), 0.0, 1.0 );
		float wFresnel = 0.02 + 0.98 * pow( 1.0 - wf, 5.0 );
		vec3 mirrored = reflect( view, water );
		vec3 fallback = ambient * 0.75 + directed * 0.45 * pow( clamp( dot( mirrored, l ), 0.0, 1.0 ), 2.0 );
		vec3 seen;
		float mirror = MIRROR;
		if ( W[10].y > 0.0 && abs( p.z - W[10].x ) < 3.0 ) {
			// the puddle view has the shape of this one: its pixel here, moved by the slope of the water
			vec2 shift = -vec2( dot( water - n, W[6].xyz ), dot( water - n, W[7].xyz ) ) * rect.w * 0.04;
			seen = texture( u_reflect, ( gl_FragCoord.xy + shift ) * W[9].xy ).rgb;
			mirror = MIRROR_VIEW;
		} else {
			seen = Mirror( p + n * 0.5, mirrored, fallback );
		}
		// the ground under the water, bent a little by the rings
		vec2 bend = vec2( -dot( water - n, W[6].xyz ), -dot( water - n, W[7].xyz ) ) * 0.015;
		vec3 under = texture( u_image, here + bend ).rgb * UNDER;
		vec3 glint = directed * pow( clamp( dot( water, half_ ), 0.0, 1.0 ), 700.0 ) * 6.0 * ( 0.02 + 0.98 * pow( 1.0 - clamp( dot( half_, -view ), 0.0, 1.0 ), 5.0 ) );
		// (more of the mirror than Fresnel's law gives: a puddle of a game reads as water by what it mirrors, and
		// the mirror of a real one is seen against ground that is darker than the floors of most levels)
		vec3 pond = mix( under, seen, clamp( wFresnel * 2.0 + mirror, 0.0, 0.92 ) ) + glint;
		color = mix( color, pond, pool );
	}

	// Snow over all of it: its clumps as bumps (the slope of their noise), lit as the snow of the ball is (light
	// wrapping into the shade with the blue of the sky, generic.frag), bluer in the hollows and where it is thin, and
	// its crystals glinting
	if ( snow > 0.002 ) {
		const float E = 0.35;
		float h = Fbm( snowAt * 0.45 );
		vec2 grad = vec2( Fbm( ( snowAt + vec2( E, 0.0 ) ) * 0.45 ) - h, Fbm( ( snowAt + vec2( 0.0, E ) ) * 0.45 ) - h ) / E;
		vec3 sn = normalize( snowN - ( snowT * grad.x + snowB * grad.y ) * 1.6 );
		float wrapped = max( ( dot( sn, l ) + 0.6 ) / 1.6, 0.0 );
		float lit = max( dot( sn, l ), 0.0 );
		float depth = smoothstep( 0.0, 0.7, snowHeight - snowMelt );
		vec3 albedo = mix( SNOW_HOLLOW, SNOW_ALBEDO, clamp( h * 1.4 - 0.2, 0.0, 1.0 ) * ( 0.6 + 0.4 * depth ) );
		vec3 lightOn = min( ambient + directed * wrapped, vec3( 1.2 ) ) + ambient * 0.18 * vec3( 0.55, 0.75, 1.0 ) * ( 1.0 - lit );
		vec3 snowColor = albedo * lightOn;

		vec2 g = snowAt * 1.6;
		vec2 cell = floor( g );
		uvec3 hs = Pcg3( uvec3( ivec3( ivec2( cell ) + 4096, 77 ) ) );
		vec3 rs = vec3( hs & 0xffffu ) / 65535.0;
		float near = 1.0 - smoothstep( 0.06, 0.14, length( g - cell - 0.2 - 0.6 * rs.xy ) );
		vec3 facet = normalize( sn + ( vec3( Pcg3( hs ) & 0xffffu ) / 65535.0 - 0.5 ) * 1.6 );
		float glint = near * pow( max( dot( reflect( -l, facet ), -view ), 0.0 ), 120.0 ) * ( 1.0 - smoothstep( 0.3, 0.9, length( fwidth( g ) ) ) );
		snowColor += max( directed, ambient * 0.5 ) * 4.0 * glint;

		color = mix( color, snowColor, snow );
		mask = max( mask, snow );
	}

	o_color = vec4( color, mask );
}
