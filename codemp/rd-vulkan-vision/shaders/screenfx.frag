#version 460

// The screen effects (VK_ScreenFx, screenEffect_t) over the view of the level: every pixel is taken again from a
// copy of the frame, from where the sources bend it to. A ring is the wave of an explosion, it squeezes the
// picture into a thin bright shell that runs outwards. Heat shimmers along a line, as the air over a flame. Smoke
// is a soft puff of noise that is folded into itself and drifts, so it curls as it rises, lit about as brightly
// as what is behind it. A source is left out where the level (or a player) is nearer than it, smoke fades out
// softly against it. Then the colours part towards the edges. Under water the picture sways, fades into the colour
// of the water with the distance and light plays over the level, after surfacing drops run down the view. A Force
// push sends waves through the air, clashing blades flash as a star. Then streaks from the middle (Force Speed),
// Force Sight (the level cold and grey with its edges drawn, what is strongly coloured keeps it: the players in
// their shells), a tint, grey, and a rim of colour, all around or on one side (where a hit came from).

#include "dialect.glsl"
#include "noise.glsl"

BINDING( 1, 0 ) uniform sampler2D u_image;		// a copy of the frame
BINDING( 2, 0 ) uniform sampler2D u_prep;		// screenfxprep.frag: the distance from the eye
BINDING( 3, 0 ) uniform sampler2D u_fog;		// screenfxfog.frag: the mist of Force Sight, only while it is on
BINDING( 4, 0 ) uniform sampler2D u_smoke;		// screenfxsmoke.frag: the smoke of its ghosts, the same

// see VK_ScreenFx() for what is where
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
#define RING		1.0
#define HEAT		2.0
#define SMOKE		3.0
#define RIPPLE		4.0
#define FLASH		5.0
#define GHOSTSMOKE	7.0

VARYING( 0 ) in vec2 v_texCoord;
layout( location = 0 ) out vec4 o_color;

float Distance( vec2 at ) {
	ivec2 size = textureSize( u_prep, 0 );
	vec4 d = texelFetch( u_prep, clamp( ivec2( at * vec2( size ) ), ivec2( 0 ), size - 1 ), 0 );

	return exp2( ( d.r * 255.0 + d.g ) / 255.0 * 16.0 );
}

// The same between the pixels of the soft buffer: the four around, each read as it is and then mixed by where
// the pixel lies between them (the channels can not be filtered as they are). For what is worked out from where the
// pixel is in the level, which otherwise comes out in blocks of two by two pixels.
float SmoothDistance( vec2 at ) {
	vec2 size = vec2( textureSize( u_prep, 0 ) );
	vec2 p = at * size - 0.5;
	vec2 f = fract( p );
	vec2 corner = ( floor( p ) + 0.5 ) / size;
	float a = log2( Distance( corner ) ), b = log2( Distance( corner + vec2( 1.0, 0.0 ) / size ) );
	float c = log2( Distance( corner + vec2( 0.0, 1.0 ) / size ) ), d = log2( Distance( corner + vec2( 1.0, 1.0 ) / size ) );

	return exp2( mix( mix( a, b, f.x ), mix( c, d, f.x ), f.y ) );
}

// The mist of screenfxfog.frag, which is half the size and grainy from its march: the 25 pixels of it around,
// each the more the nearer and the more its distance (prep, the same pixels) is that of this pixel, so it comes out
// smooth and does not spill over an edge onto what is in front of it or behind.
vec4 Mist( vec2 at ) {
	ivec2 size = textureSize( u_fog, 0 );
	vec2 p = at * vec2( size ) - 0.5;
	ivec2 base = ivec2( floor( p ) );
	float here = log2( SmoothDistance( at ) );
	vec4 sum = vec4( 0.0 );
	float weights = 0.0;

	for ( int y = -2; y <= 2; y++ ) {
		for ( int x = -2; x <= 2; x++ ) {
			ivec2 c = clamp( base + ivec2( x, y ), ivec2( 0 ), size - 1 );
			vec2 off = vec2( c ) - p;
			vec4 d = texelFetch( u_prep, c, 0 );
			float w = exp( -dot( off, off ) * 0.25 ) * exp( -abs( ( d.r * 255.0 + d.g ) / 255.0 * 16.0 - here ) * 3.0 );

			sum += texelFetch( u_fog, c, 0 ) * w;
			weights += w;
		}
	}
	return sum / max( weights, 1e-4 );
}

vec3 Frame( vec2 at ) {
	return texture( u_image, at * FX[1].xy ).rgb;
}

// The light that water throws on the ground, a known pattern of folded waves (after "Tileable Water Caustic" of
// joltz0r, Shadertoy MdlXz8): p in radians, it repeats every 2 pi.
float Caustic( vec2 p, float time ) {
	vec2 i = p;
	float c = 1.0;

	for ( int n = 0; n < 4; n++ ) {
		float t = time * ( 1.0 - 3.5 / float( n + 1 ) );

		i = p + vec2( cos( t - i.x ) + sin( t + i.y ), sin( t - i.y ) + cos( t + i.x ) );
		vec2 w = vec2( sin( i.x + t ), cos( i.y + t ) );

		w = mix( vec2( 1.0 ), vec2( -1.0 ), lessThan( w, vec2( 0.0 ) ) ) * max( abs( w ), vec2( 0.0001 ) );
		c += 1.0 / max( length( p * 0.005 / w ), 0.001 );
	}
	c = 1.17 - pow( c / 4.0, 1.4 );
	return clamp( pow( abs( c ), 8.0 ), 0.0, 2.0 );
}

// Drops on the view in two layers of cells, each with a drop that slides down in stops and starts and a thin trail
// above it. xy: the offset they bend the picture by, z: the light on their rim.
vec3 Drops( vec2 uv, float time ) {
	vec3 result = vec3( 0.0 );

	for ( int layer = 0; layer < 2; layer++ ) {
		vec2 grid = vec2( 16.0, 3.0 ) * ( 1.0 + float( layer ) * 0.7 );
		vec2 p = uv * grid + vec2( float( layer ) * 3.1, 0.0 );
		vec2 cell = floor( p );
		float h = Hash( cell + vec2( float( layer ) * 17.0, 0.0 ) );

		if ( h < 0.45 ) {
			continue;
		}
		float h2 = Hash( cell + vec2( 3.3, float( layer ) * 5.0 ) );
		float size = 0.007 + 0.011 * h2;
		float t = fract( time * ( 0.12 + 0.18 * h ) + h * 7.0 );
		vec2 at = vec2( 0.5 + ( h - 0.5 ) * 0.6, t + sin( t * 14.0 + h * 6.0 ) * 0.03 );
		// a little taller than wide, as a drop that runs
		vec2 d = ( fract( p ) - at ) / grid * vec2( 1.0, 0.8 );
		float r = length( d );
		float drop = 1.0 - smoothstep( size * 0.6, size, r );
		float trail = ( 1.0 - smoothstep( 0.0008, 0.0025, abs( d.x ) ) ) * step( d.y, 0.0 ) * ( 1.0 - smoothstep( 0.0, 0.12, -d.y ) ) * 0.6;

		// a lens: the picture through it is turned around a little, the light catches its upper edge
		result.xy -= d * drop * 1.8;
		result.x -= sign( d.x ) * trail * 0.002;
		result.z += drop * smoothstep( 0.2, 0.9, dot( d / max( r, 1e-5 ), vec2( -0.45, -0.89 ) ) ) * smoothstep( size * 0.3, size * 0.8, r ) * 0.35;
	}
	return result;
}

// A grid of hexagons: xy where p is in its cell from the middle of it, zw the middle of the cell (tells one cell
// from the next). The cells are one across their flat sides.
vec4 HexGrid( vec2 p ) {
	const vec2 r = vec2( 1.0, 1.7320508 );
	vec2 a = mod( p, r ) - r * 0.5;
	vec2 b = mod( p - r * 0.5, r ) - r * 0.5;
	vec2 inCell = dot( a, a ) < dot( b, b ) ? a : b;

	return vec4( inCell, p - inCell );
}

// how far a place in a cell of HexGrid() is from its middle, 0.5 on the edge
float HexEdge( vec2 inCell ) {
	vec2 q = abs( inCell );

	return max( dot( q, vec2( 0.5, 0.8660254 ) ), q.x );
}

// where the pixel is in the level, from its distance along the view
vec3 World( vec2 ndc, float here ) {
	return FX[9].xyz + ( FX[10].xyz + FX[11].xyz * ndc.x * FX[9].w + FX[12].xyz * ndc.y * FX[10].w ) * here;
}

void main() {
	float time = FX[0].x;
	vec2 aspect = vec2( FX[0].y, 1.0 );
	int count = int( FX[0].w + 0.5 );
	float here = Distance( v_texCoord );
	vec2 offset = vec2( 0.0 );
	float light = 0.0;
	float smoke = 0.0;		// how much of the picture the smoke hides, 0 to 1
	float flash = 0.0;		// light of the clashing blades, added

	for ( int i = 0; i < SOURCES; i++ ) {
		if ( i >= count ) {
			break;
		}
		vec4 ends = FX[16 + 3 * i];
		vec4 size = FX[17 + 3 * i];		// radius, width, strength, distance
		vec4 more = FX[18 + 3 * i];		// kind, seed
		float kind = more.x;

		// screenfxsmoke.frag has those
		if ( abs( kind - GHOSTSMOKE ) < 0.5 ) {
			continue;
		}

		if ( abs( kind - SMOKE ) < 0.5 ) {
			vec2 d = ( v_texCoord - ends.xy ) * aspect / max( size.x, 0.0001 );
			float r = length( d );

			if ( r >= 1.0 ) {
				continue;
			}
			// soft where it meets the level, as if it were a cloud and not a picture
			float soft = smoothstep( size.w - 40.0, size.w + 10.0, here );

			if ( soft <= 0.0 ) {
				continue;
			}
			// the noise folded by two more of it, moving up through the puff: it curls
			vec2 q = d * 1.4 + vec2( more.y * 7.13, more.y * 3.71 ) + vec2( 0.0, time * 0.45 );
			vec2 warp = vec2( Noise( q * 1.2 + vec2( time * 0.25, 0.0 ) ), Noise( q * 1.2 + vec2( 5.2, -time * 0.2 ) ) );
			float n = Fbm( q + warp * 1.6 );
			float dense = smoothstep( 0.3, 0.75, n ) * ( 1.0 - smoothstep( 0.25, 1.0, r ) ) * size.z * soft;

			smoke = 1.0 - ( 1.0 - smoke ) * ( 1.0 - dense * 0.7 );
			continue;
		}
		// the level is in front of it: gone, softly over a few units so the edge of a wall or of a player in front
		// does not flicker
		float seen = smoothstep( size.w - 12.0, size.w, here );

		if ( seen <= 0.0 ) {
			continue;
		}
		size.z *= seen;
		if ( kind > FLASH - 0.5 ) {
			vec2 d = ( v_texCoord - ends.xy ) * aspect / max( size.x, 0.0001 );
			// a soft glow and a star of four thin rays, the one across longer
			float glow = exp( -length( d ) * 3.0 );
			float rays = exp( -abs( d.y ) * 60.0 ) * exp( -abs( d.x ) * 0.9 ) + 0.6 * exp( -abs( d.x ) * 60.0 ) * exp( -abs( d.y ) * 1.6 );

			flash += ( glow + rays ) * size.z;
		} else if ( kind > RIPPLE - 0.5 ) {
			vec2 d = ( v_texCoord - ends.xy ) * aspect;
			float r = length( d );
			float x = ( r - size.x ) / max( size.y, 0.001 );
			vec2 dir = r > 0.0001 ? d / r : vec2( 0.0 );

			// a few waves in a band that runs out (in for a pull)
			// as far as a fixed share of the view at most, however close it is
			offset += dir / aspect * sin( x * 9.42 ) * exp( -x * x ) * min( size.y, 0.04 ) * 0.45 * size.z * more.y;
		} else if ( kind < RING + 0.5 ) {
			vec2 d = ( v_texCoord - ends.xy ) * aspect;
			float r = length( d );
			float x = ( r - size.x ) / max( size.y, 0.001 );
			float wave = exp( -x * x ) * size.z;
			vec2 dir = r > 0.0001 ? d / r : vec2( 0.0 );

			// out in front of the ring, in behind it: the picture is pressed into it, and the shell is bright
			offset -= dir / aspect * wave * x * min( size.y, 0.06 ) * 1.1;
			light += wave * 0.12;
		} else {
			vec2 pa = ( v_texCoord - ends.xy ) * aspect;
			vec2 ba = ( ends.zw - ends.xy ) * aspect;
			float h = clamp( dot( pa, ba ) / max( dot( ba, ba ), 1e-8 ), 0.0, 1.0 );
			float away = length( pa - ba * h );
			// around the line, and not within its width (the core of a blade stays still)
			float fall = ( 1.0 - smoothstep( 0.0, 1.0, away / max( size.x, 0.0001 ) ) ) * ( size.y > 0.0 ? smoothstep( size.y, size.y * 1.8, away ) : 1.0 );

			if ( fall > 0.0 ) {
				// waves in the size of the source that rise and wander, so a thin band around a blade shimmers as
				// much as the wide air over a flame. Only within a range of sizes: waves that grew without end with a
				// source coming close swam and jumped, and filled the view.
				float scale = clamp( size.x, 0.015, 0.08 );
				vec2 p = v_texCoord * aspect / scale;
				vec2 wobble = vec2( sin( p.y * 9.0 + time * 11.0 + sin( p.x * 7.0 + time * 3.0 ) ),
					cos( p.x * 8.0 - time * 9.0 + sin( p.y * 6.0 + time * 4.0 ) ) );

				offset += wobble / aspect * fall * fall * size.z * scale * 0.15;
			}
		}
	}

	vec4 rect = FX[13];
	// where the pixel is in the view: -1 to 1, y up
	vec2 ndc = vec2( ( v_texCoord.x - rect.x ) / rect.z * 2.0 - 1.0, 1.0 - ( v_texCoord.y - rect.y ) / rect.w * 2.0 );
	float water = FX[6].z;

	if ( water > 0.0 ) {
		offset += vec2( sin( v_texCoord.y * 23.0 + time * 1.9 ), cos( v_texCoord.x * 19.0 + time * 1.6 ) ) * 0.0035 * water;
	}
	vec2 square = vec2( ndc.x * FX[0].y * rect.z / rect.w, ndc.y );	// the view with its own width, round
	float rim = length( ndc );
	vec2 middle = rect.xy + rect.zw * 0.5;
	float rage = FX[8].y;
	float protect = FX[8].z;
	float wave = 0.0;		// the ripple of a hit running over the shield of protect
	vec4 hex = vec4( 0.0 );

	// Rage: every beat of the heart pulls the picture in a little, and the air at the edges boils
	if ( rage > 0.0 ) {
		offset += ( middle - v_texCoord ) * 0.012 * FX[14].z * rage;
		offset += vec2( sin( v_texCoord.y * 40.0 + time * 6.0 + sin( v_texCoord.x * 30.0 ) ), cos( v_texCoord.x * 36.0 - time * 5.0 ) )
			* 0.0025 * smoothstep( 0.6, 1.3, rim ) * rage;
	}
	// Protect: a dome of energy around the eye. Its edge bends the picture in as curved glass does, every facet of
	// it a little lens of its own, and a hit sends a ripple over it from the side it came from.
	if ( protect > 0.0 ) {
		float shell = smoothstep( 0.7, 1.35, rim );

		hex = HexGrid( square * 5.0 );
		offset += ( middle - v_texCoord ) * shell * shell * 0.05 * protect;
		offset -= hex.xy / aspect * 0.012 * shell * protect;
		if ( FX[14].x > 0.0 && dot( FX[6].xy, FX[6].xy ) > 0.0001 ) {
			vec2 from = normalize( FX[6].xy ) * vec2( FX[0].y * rect.z / rect.w, 1.0 );
			vec2 d = square - from;
			float x = ( length( d ) - ( 1.0 - FX[14].x ) * 2.5 ) / 0.15;

			wave = exp( -x * x ) * FX[14].x;
			offset -= normalize( d + vec2( 0.0001 ) ) * vec2( 1.0, -1.0 ) / aspect * wave * 0.015 * protect;
		}
	}
	// Absorb: the picture swims at the edges, where the streams are drawn in
	if ( FX[8].w > 0.0 ) {
		offset += vec2( sin( v_texCoord.y * 8.0 + time * 1.3 ), cos( v_texCoord.x * 7.0 + time * 1.1 ) ) * 0.004 * FX[8].w
			* smoothstep( 0.5, 1.2, rim ) * ( 1.0 + FX[14].y );
	}
	// Force Sight: the other world wavers slowly, and the colours part towards its edges
	if ( FX[8].x > 0.0 ) {
		offset += vec2( sin( v_texCoord.y * 5.0 + time * 0.7 ), cos( v_texCoord.x * 4.0 + time * 0.6 ) ) * 0.0012 * FX[8].x;
	}
	float glint = 0.0;

	if ( FX[6].w > 0.0 ) {
		vec3 drops = Drops( v_texCoord * aspect, time );

		offset += drops.xy * FX[6].w;
		glint = drops.z * FX[6].w;
	}

	vec2 at = v_texCoord + offset;
	vec2 fromMiddle = at - 0.5;
	vec2 split = fromMiddle * dot( fromMiddle, fromMiddle ) * FX[1].w * 0.05;
	vec3 color = vec3( Frame( at + split ).r, Frame( at ).g, Frame( at - split ).b ) * ( 1.0 + light );

	// streaks towards the middle of the view, longer towards its edges
	if ( FX[4].y > 0.0 ) {
		vec2 toMiddle = ( rect.xy + rect.zw * 0.5 - at ) * FX[4].y * 0.05;
		vec3 sum = color;

		for ( int i = 1; i < 8; i++ ) {
			sum += Frame( at + toMiddle * float( i ) / 7.0 );
		}
		color = sum / 8.0;
	}
	if ( water > 0.0 ) {
		float fog = 1.0 - exp( -here / 520.0 );
		vec3 world = World( ndc, SmoothDistance( v_texCoord ) );
		float c = Caustic( mod( world.xy * 0.02 + vec2( world.z * 0.006 ), 6.28318 ) - 250.0, time * 0.7 );

		color += ( color * 1.6 + FX[7].rgb * 0.15 * FX[0].z ) * c * water * ( 1.0 - fog );
		color *= mix( vec3( 1.0 ), FX[7].rgb * 1.2 + 0.35, 0.5 * water );
		color = mix( color, FX[7].rgb * FX[0].z, fog * 0.85 * water );
	}

	if ( smoke > 0.0 ) {
		// grey, about as bright as the place it hangs in, never black
		float lum = dot( color, vec3( 0.299, 0.587, 0.114 ) );
		vec3 grey = vec3( 0.22, 0.22, 0.23 ) * FX[0].z + vec3( lum * 0.8 );

		color = mix( color, grey, smoke );
	}

	color += vec3( 1.0, 0.95, 0.85 ) * FX[0].z * flash;
	// Force Sight, the other world. The colours stay, the balance moves: reds and oranges fade, the shadows turn to a
	// cold green and are lifted a little, a pale mist rolls over the floors (screenfxfog.frag, see Mist(); lights
	// behind it glow through), what is bright glows softly, and the view darkens to a deep green at
	// its edges. The players are ghosts (gfx/vision/ghost) that smoke (screenfxsmoke.frag).
	if ( FX[8].x > 0.0 ) {
		float sight = FX[8].x;
		float lum = dot( color, vec3( 0.299, 0.587, 0.114 ) ) / FX[0].z;
		float high = max( color.r, max( color.g, color.b ) );
		float warm = clamp( ( color.r - max( color.g, color.b ) * 0.8 ) / max( high, 0.001 ), 0.0, 1.0 );
		vec3 other = mix( color, vec3( dot( color, vec3( 0.299, 0.587, 0.114 ) ) ), warm * 0.85 );
		vec4 mist = Mist( v_texCoord );

		other = other * vec3( 0.8, 1.0, 0.92 ) + vec3( 0.01, 0.035, 0.03 ) * FX[0].z * ( 1.0 - smoothstep( 0.0, 0.4, lum ) );
		other = mix( other, vec3( 0.55, 0.8, 0.7 ) * FX[0].z * ( 0.5 + 0.6 * mist.g ) * ( 0.8 + 0.5 * lum ), mist.r * 0.85 );
		other += other * smoothstep( 0.5, 1.0, lum ) * 0.35;
		other = mix( other, vec3( 0.01, 0.05, 0.04 ) * FX[0].z, smoothstep( 0.55, 1.45, rim ) * 0.55 );
		color = mix( color, other, sight );
		// the smoke of the ghosts, broken up by a noise that rises with it, so it curls as it goes up
		float wisp = 0.45 + 0.7 * Fbm( v_texCoord * aspect * 7.0 + vec2( time * 0.15, time * 0.5 ) )
			+ 0.45 * Fbm( v_texCoord * aspect * 19.0 + vec2( -time * 0.2, time * 0.9 ) );
		float smoke = texture( u_smoke, v_texCoord ).r;

		color += vec3( 0.55, 1.0, 0.75 ) * FX[0].z * smoothstep( 0.0, 0.8, smoke * wisp ) * 0.75 * sight;
	}
	// Rage: the red stays and burns, the rest drains to a warm grey and its shadows sink, dark red closes in from
	// the edges with every beat, and fine veins run in over it
	if ( rage > 0.0 ) {
		float lum = dot( color, vec3( 0.299, 0.587, 0.114 ) );
		float red = clamp( ( color.r - max( color.g, color.b ) ) / max( color.r, 0.001 ), 0.0, 1.0 );
		vec3 graded = mix( vec3( lum ) * vec3( 1.05, 0.85, 0.8 ), color * vec3( 1.3, 0.85, 0.8 ), red );

		graded *= mix( 0.7, 1.0, smoothstep( 0.0, 0.35 * FX[0].z, lum ) );
		color = mix( color, graded, rage * 0.8 );
		color = mix( color, vec3( 0.16, 0.0, 0.0 ) * FX[0].z, smoothstep( 0.6 - 0.12 * FX[14].z, 1.35, rim ) * rage * ( 0.55 + 0.25 * FX[14].z ) );
		// fine veins as in bloodshot eyes: few and thin, only near the edge, finer where they branch towards the
		// middle, a muted deep red a little above the dark rim, and they lie on the bent picture, so they swim with
		// the boiling air.
		if ( rim > 0.7 ) {
			float edge = smoothstep( 0.7, 1.4, rim );
			vec2 p = at * aspect * 2.6 + vec2( sin( time * 0.3 ), cos( time * 0.25 ) ) * 0.1;
			float vein = 0.0;
			float weight = 1.0;

			for ( int i = 0; i < 2; i++ ) {
				vec2 warp = vec2( Noise( p * 0.6 + vec2( 3.1 * float( i ), 0.0 ) ), Noise( p * 0.6 + vec2( 7.7, float( i ) ) ) ) - 0.5;
				float thin = mix( 90.0, 40.0, edge ) + 40.0 * float( i );

				vein = max( vein, pow( 1.0 - abs( Noise( p + warp * 1.2 ) * 2.0 - 1.0 ), thin ) * weight
					* smoothstep( 0.7 + 0.15 * float( i ), 1.3, rim ) );
				p = p * 2.3 + vec2( 1.7, 4.3 );
				weight *= 0.6;
			}
			vein *= edge * rage * ( 0.55 + 0.2 * FX[14].z );
			color = mix( color, vec3( 0.36, 0.02, 0.02 ) * FX[0].z + color * 0.25, vein );
		}
	}
	// Protect: the dome shows only at its edge, a faint light in its facets and their seams, and where the ripple of
	// a hit runs
	if ( protect > 0.0 ) {
		float shell = smoothstep( 0.7, 1.35, rim );
		float seam = smoothstep( 0.44, 0.5, HexEdge( hex.xy ) );
		float shimmer = 0.5 + 0.5 * sin( time * 1.7 + hex.z * 1.3 + hex.w * 0.7 );

		color += vec3( 0.35, 1.0, 0.65 ) * FX[0].z * protect * ( shell * shell * ( 0.06 + seam * ( 0.1 + 0.12 * shimmer ) ) + wave * ( 0.15 + seam * 0.8 ) );
	}
	// Absorb: streams of blue light at the edges that are drawn in to the middle, brighter while something is taken in
	if ( FX[8].w > 0.0 ) {
		float r = length( square );
		vec2 dir = square / max( r, 0.0001 );
		float streams = smoothstep( 0.45, 0.85, Fbm( dir * ( 1.5 + r * 2.0 + time * 0.9 ) ) );

		color += vec3( 0.3, 0.55, 1.0 ) * FX[0].z * streams * smoothstep( 0.55, 1.3, rim ) * FX[8].w * ( 0.3 + 0.4 * FX[14].y );
	}
	color += vec3( glint ) * FX[0].z * 0.6;
	color = mix( color, vec3( dot( color, vec3( 0.299, 0.587, 0.114 ) ) ), FX[4].x );
	color = mix( color, FX[3].rgb * FX[0].z, FX[3].a );
	// the rim, on one side only when it has a direction
	if ( FX[4].w > 0.0 ) {
		float band = smoothstep( 0.45, 1.3, rim );
		float side = 1.0;

		if ( FX[5].w > 0.0 && dot( FX[6].xy, FX[6].xy ) > 0.0001 ) {
			float a = dot( normalize( ndc + vec2( 0.0001 ) ), normalize( FX[6].xy ) );

			side = smoothstep( 1.0 - 2.0 * FX[5].w, 1.4 - 2.0 * FX[5].w, a );
		}
		color = mix( color, FX[5].rgb * FX[0].z, band * side * FX[4].w );
	}
	o_color = vec4( color, 1.0 );
}
