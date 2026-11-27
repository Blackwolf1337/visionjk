#version 460

// Smoke as a volume (vk_smoke.cpp VK_SmokeDraw, ext.SetSmoke, cgame/level/cg_smoke.c). Drawn into a picture of its own
// at half the size of the frame (a pixel of it stands for S[356].x pixels of the frame); shaders/smokeup.frag lays
// that over the solid level. The ray of
// each pixel is marched through the blobs of smoke it passes, up to what the scene has there: how much of the smoke
// lies on the way decides how much of the level behind is left, so the smoke has depth, ends softly at a wall and a
// player stands in it. Its shape is the blobs cut into by a noise that rises slowly, so its edge billows; a hole takes
// it out round a line (a push, a blast, a blade, a shot). Each place is lit by the light of its blob, less under a
// thick layer of smoke (a look up), and by the moving lights around.
//
// What it costs is its steps, 12 a ray, times the blobs a step looks at (measured 2026-11-26 at 2560x1440 with the
// eye in the smoke of eight rockets: 4.1 ms with 16 steps and three loops over all the blobs of the ray at each, 2.3 ms
// as it is now). So a step has one loop, over the blobs on whose part of the ray it lies, for the place and for its
// light at once; the noise of the billows is read from a volume that repeats (u_noise, three lookups a step, not six
// noises worked out); and the holes and lights a ray does not pass are left out for all its steps. What a pixel
// keeps at hand while it marches costs too (the more it is, the fewer pixels the device works on at once): a list
// of blobs with their part of the ray, and a bit each for the holes and lights. A way round that was tried cost
// more: lists of the blobs of each step, and the distance from numbers worked out once a ray. The rest is how many
// blobs lie over each other, which the client game keeps low (cgame/level/cg_smoke.c Smoke_Merge).
//
// S (globals.smoke): rows 0-10 the view as the passes over the level have it; row 11: how many blobs, holes, the
// seconds, how many lights; then three rows a blob (its middle and where it ends, its colour and how thick, how
// high it is against its width and how far it moved since it came), two a
// hole (start and radius, end and how much it takes), two a light (place and reach, colour).

#include "dialect.glsl"

layout( set = 0, binding = 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
	vec4	rt[382];
	vec4	lights[34];
	vec4	ssr[7];
	vec4	glass[10];		// the views of what lies behind thick glass (VK_GlassRows of vk_post.cpp, VK_GLASS_VEC4S)
	vec4	blades[247];
	vec4	smoke[359];
} globals;
// the noise of the billows, a volume that repeats (vk_smoke.cpp Smoke_CreateNoise): red the billows, three octaves
// that repeat after 4, 8 and 16 cells; green the lumps, 8 cells; blue and alpha bend the place the billows are read
// at, 8 cells
layout( set = 1, binding = 0 ) uniform sampler3D u_noise;
layout( set = 2, binding = 0 ) uniform sampler2D u_depth;	// the depth of the scene, read pixel for pixel; or how far it is (row 358 x)
layout( set = 3, binding = 0 ) uniform sampler2D u_begin;	// row 358 y: how far the rays begin (the smoke behind thick glass)

#define S		globals.smoke
#define W		globals.smoke
// (row 358 x: u_depth holds how far the scene is, not its depth: the part of the smoke in front of the glass and the
// part behind it are marched to distances of their own, vk_smoke.cpp VK_SmokeDraw)
#define SCENE_IS_DISTANCE	( S[358].x > 0.5 )

layout( location = 0 ) out vec4 o_color;

#include "scene.glsl"
#include "geom.glsl"

#define HEAD		11
#define BLOBS		96
#define HOLES		24
#define BLOB( i )	( HEAD + 1 + ( i ) * 3 )
#define HOLE( i )	( HEAD + 1 + BLOBS * 3 + ( i ) * 2 )
#define LIGHT( i )	( HEAD + 1 + BLOBS * 3 + HOLES * 2 + ( i ) * 2 )
#define NEAREST		28			// blobs a ray counts
#define STEPS		12
#define LIGHTS		4
// the place the light is looked for from a place in the smoke: the light comes mostly from above
#define TO_LIGHT	( vec3( 0.26, 0.17, 0.95 ) * 26.0 )

int g_list[NEAREST];		// the blobs the ray passes
vec2 g_span[NEAREST];		// the part of the ray on which each can matter to a step: from, to
int g_count;
uint g_holes;				// the holes and the lights the ray passes, a bit each
uint g_lights;
vec3 g_drift;				// how far the smoke at the last step had moved since it came

// The smoke at p, which is t along the ray: how thick it is there (x) and at the place its light is looked for from
// (y), and its colour. The blobs with their rims moved by the billows, and the holes.
vec2 Smoke( vec3 p, float t, out vec3 color ) {
	float time = S[HEAD].z;
	// The billows travel with the smoke: they are read where this smoke was when it came (the way its blobs moved
	// since, by how much of each is here; as it was a step before, it changes slowly). Read at the place itself they
	// stand still in the room, and a blob that is pushed slides along under them as a ball (their word 2026-11-26).
	vec3 at = p - g_drift;
	// Billows that rise and turn over slowly. The place they are read at is bent by a slower, larger noise, so they
	// are swells and folds and no grid of lumps. (The noise stays near its middle: spread to all of 0 to 1.) They
	// move the surface of the smoke in and out, so the cloud is a heap of swells with an edge, as thick smoke is,
	// and not a ball. (u_noise repeats after 8 cells of the bend and 4 of the billows: their places over 8 and 4.)
	vec3 q = at * 0.014 + vec3( time * 0.010, -time * 0.007, -time * 0.05 );
	q.xy += ( textureLod( u_noise, q * ( 0.4 / 8.0 ), 0.0 ).ba - 0.5 ) * 1.8;
	float billow = clamp( ( textureLod( u_noise, q * 0.25, 0.0 ).r - 0.3 ) / 0.4, 0.0, 1.0 );
	// small lumps: on the swells of a cloud, and the whole shape of a small puff
	float lumps = textureLod( u_noise, ( at * 0.055 + vec3( -time * 0.02, time * 0.015, -time * 0.09 ) ) * 0.125, 0.0 ).g;
	billow = clamp( billow + ( lumps - 0.5 ) * 0.5, 0.0, 1.0 );
	// (they move the rim of every blob: 0.5 leaves it where it is, more pushes it out by up to a quarter of its
	// radius, less pulls it in. The billows for the large blobs of a cloud; the lumps for a small blob, the dust of
	// a shot, which the billows would only move as a whole and leave a ball.)
	vec2 swell = vec2( billow, clamp( ( lumps - 0.3 ) / 0.4, 0.0, 1.0 ) );

	// Blobs that lie in each other are one cloud, not twice the smoke: what is left free of each is multiplied, so
	// the smoke where two overlap is as thick as in the middle of one. Added up, the shape of every overlap showed
	// as a lens with a rim, circles in the smoke where clouds met (their picture 2026-11-26).
	vec2 open = vec2( 1.0 );
	float weights = 0.0, drifts = 0.0;
	vec3 sum = vec3( 0.0 ), drift = vec3( 0.0 );

	for ( int i = 0; i < g_count; i++ ) {
		if ( t < g_span[i].x || t > g_span[i].y ) {
			continue;
		}
		int row = BLOB( g_list[i] );
		vec4 a = S[row], c = S[row + 2];
		// (smoke that settled is a cushion: as wide as its radius, less high)
		vec3 d = p - a.xyz, e = d + TO_LIGHT;
		d.z /= c.x;
		e.z /= c.x;
		vec2 near = vec2( length( d ), length( e ) ) / a.w;
		vec2 x = near + ( 0.5 - mix( swell.y, swell.x, smoothstep( 35.0, 150.0, a.w ) ) ) * 0.6;

		if ( min( x.x, x.y ) < 1.0 ) {
			vec4 b = S[row + 1];
			// (thin smoke with a hard rim was a glass ball: it thins out over its outer half)
			vec2 w = min( b.w * smoothstep( vec2( 1.0 ), vec2( 0.45 ), x ), vec2( 0.96 ) );
			float here = max( 0.0, 1.3 - near.x ) * b.w;

			open *= 1.0 - w;
			weights += w.x;
			sum += b.rgb * w.x;
			drifts += here;
			drift += c.yzw * here;
		}
	}
	color = weights > 0.0 ? sum / weights : vec3( 0.0 );
	if ( drifts > 0.0 ) {
		g_drift = drift / drifts;
	}
	vec2 thick = min( 1.0 - open, vec2( 1.6 ) );

	if ( thick.x <= 0.0 ) {
		return thick;
	}
	for ( uint left = g_holes; left != 0u; left &= left - 1u ) {
		int j = findLSB( left );
		vec4 a = S[HOLE( j )], b = S[HOLE( j ) + 1];
		float off = distance( p, NearestOnSegment( a.xyz, b.xyz, p ) );

		// (its rim follows the billows a little, so it is no clean tube)
		thick.x *= 1.0 - b.w * ( 1.0 - smoothstep( a.w * 0.6, a.w, off + ( billow - 0.5 ) * a.w * 0.3 ) );
	}
	return thick;
}

void main() {
	// the pixel of the frame this one stands for
	vec2 pixel = gl_FragCoord.xy * max( S[356].x, 1.0 );
	vec3 eye = W[4].xyz;
	vec3 ray = SceneRay( pixel );
	float rayLength = length( ray );
	vec3 dir = ray / rayLength;
	float scene = SceneDistance( pixel ) * rayLength;
	float from = 1e9, to = 0.0;
	// a view behind glass (row 357): the ray begins where it passes the plane of the glass
	float begin = 0.0;
	vec4 cut = S[357];
	if ( dot( cut.xyz, cut.xyz ) > 0.5 ) {
		float facing = dot( cut.xyz, dir ), left = cut.w - dot( cut.xyz, eye );

		begin = facing > 1e-5 ? max( left / facing, 0.0 ) : ( left > 0.0 ? scene : 0.0 );
	}
	// the part behind thick glass (row 358 y): from the glass on, where it is nearer than the scene
	if ( S[358].y > 0.5 ) {
		ivec2 last = textureSize( u_begin, 0 ) - 1;

		begin = max( begin, texelFetch( u_begin, clamp( ivec2( pixel ), ivec2( 0 ), last ), 0 ).r * rayLength );
		if ( begin >= scene - 1.0 ) {
			o_color = vec4( 0.0 );
			return;
		}
	}
	int blobs = int( S[HEAD].x );

	// The blobs this ray passes in front of the scene. When they are more than it counts, those that matter least go:
	// the ones with the least smoke on the ray. (It counted the first twelve it found: where clouds lay over each
	// other, others were left out from pixel to pixel and from frame to frame, which showed as the rims of circles
	// and flickered, their picture 2026-11-26.)
	float weight[NEAREST];
	float least = 0.0;		// when the list is full: the least of them, and where it is
	int leastAt = 0;
	g_count = 0;
	for ( int i = 0; i < blobs; i++ ) {
		vec4 a = S[BLOB( i )];
		vec3 oc = a.xyz - eye;
		float along = dot( oc, dir );
		// (As far as the billows push its rim out, Smoke(): 1.3 of its radius. At the radius itself a ray that just
		// missed a blob left its swells out, the ray next to it had them: the rim of a circle in the smoke where
		// clouds lay over each other, their picture 2026-11-26.)
		float off2 = dot( oc, oc ) - along * along, r2 = a.w * a.w * 1.69;

		if ( off2 >= r2 ) {
			continue;
		}
		float half_ = sqrt( r2 - off2 );
		float enter = max( along - half_, begin ), leave = min( along + half_, scene );
		if ( leave <= enter ) {
			continue;
		}
		float w = ( leave - enter ) * S[BLOB( i ) + 1].w;
		int slot = g_count;

		if ( g_count == NEAREST ) {
			if ( w <= least ) {
				continue;
			}
			slot = leastAt;
		} else {
			g_count++;
		}
		// (Its part of the ray for the steps: where a place, or the place its light is looked for from, TO_LIGHT
		// away, can be in it. A cushion is that much higher in its own measure.)
		float wide = a.w * 1.3 + length( TO_LIGHT * vec3( 1.0, 1.0, 1.0 / S[BLOB( i ) + 2].x ) );
		float reach = sqrt( wide * wide - off2 );

		g_list[slot] = i;
		g_span[slot] = vec2( along - reach, along + reach );
		weight[slot] = w;
		from = min( from, enter );
		to = max( to, leave );
		if ( g_count == NEAREST ) {
			leastAt = 0;
			for ( int k = 1; k < NEAREST; k++ ) {
				if ( weight[k] < weight[leastAt] ) {
					leastAt = k;
				}
			}
			least = weight[leastAt];
		}
	}
	if ( g_count == 0 || to <= from ) {
		discard;
	}
	// how far the smoke on the ray moved since it came, for its first step (Smoke())
	{
		float weights = 0.0;

		g_drift = vec3( 0.0 );
		for ( int i = 0; i < g_count; i++ ) {
			g_drift += S[BLOB( g_list[i] ) + 2].yzw * weight[i];
			weights += weight[i];
		}
		g_drift /= max( weights, 1e-6 );
	}
	// the holes and the lights it passes on that way (a ball round each)
	g_holes = 0u;
	for ( int j = 0; j < int( S[HEAD].y ); j++ ) {
		vec4 a = S[HOLE( j )], b = S[HOLE( j ) + 1];
		// (the billows move its rim by up to 0.15 of its radius, Smoke())
		float reach = distance( a.xyz, b.xyz ) * 0.5 + a.w * 1.15;
		vec3 oc = ( a.xyz + b.xyz ) * 0.5 - eye;
		float along = dot( oc, dir );

		if ( b.w > 0.0 && dot( oc, oc ) - along * along < reach * reach && along + reach > from && along - reach < to ) {
			g_holes |= 1u << j;
		}
	}
	g_lights = 0u;
	for ( int l = 0; l < min( int( S[HEAD].w ), LIGHTS ); l++ ) {
		vec4 a = S[LIGHT( l )];
		vec3 oc = a.xyz - eye;
		float along = dot( oc, dir );

		if ( dot( oc, oc ) - along * along < a.w * a.w && along + a.w > from && along - a.w < to ) {
			g_lights |= 1u << l;
		}
	}

	float stride = max( ( to - from ) / float( STEPS ), 5.0 );
	// each pixel starts at another place of its first step (interleaved gradient noise): bands become grain
	float start = fract( 52.9829189 * fract( dot( gl_FragCoord.xy, vec2( 0.06711056, 0.00583715 ) ) ) );
	float left = 1.0;
	vec3 sum = vec3( 0.0 );

	for ( int s = 0; s < STEPS; s++ ) {
		float t = from + ( float( s ) + start ) * stride;
		if ( t >= to ) {
			break;
		}
		vec3 p = eye + dir * t;
		vec3 color;
		vec2 thicks = Smoke( p, t, color );
		float thick = thicks.x;
		if ( thick <= 0.004 ) {
			continue;
		}
		// The light comes mostly from above. How much smoke lies between this place and the light is its shadow: the
		// top of a swell is light, a fold and the underside are dark. That is what gives the cloud its shape.
		// (with the billows of this place: the noise once more for every step doubled what the pass costs, 4 ms of a
		// frame with a cloud all over the view)
		float toLight = thicks.y;
		// (smoke is grey: half of the colour of the light)
		// (mostly: a cloud in the colour of a warm room was brown, and dark where it was thick)
		color = mix( color, vec3( dot( color, vec3( 0.3, 0.59, 0.11 ) ) ), 0.75 );
		vec3 lit = color * ( 0.5 + 0.8 * exp( -toLight * 2.2 ) );
		for ( uint some = g_lights; some != 0u; some &= some - 1u ) {
			int l = findLSB( some );
			vec4 a = S[LIGHT( l )], b = S[LIGHT( l ) + 1];
			float x = distance( p, a.xyz ) / a.w;

			if ( x < 1.0 ) {
				float w = 1.0 - x * x;

				lit += b.rgb * w * w;
			}
		}
		// (Thin: what is behind a cloud is dimmed, not hidden, their word 2026-11-26. At 0.06 a cloud was a wall.)
		float taken = 1.0 - exp( -thick * stride * 0.014 );
		sum += lit * taken * left;
		left *= 1.0 - taken;
		if ( left < 0.02 ) {
			left = 0.0;
			break;
		}
	}
	if ( left > 0.996 ) {
		discard;
	}
	// (smokeup.frag blends the frame as sum + frame * left)
	o_color = vec4( sum, 1.0 - left );
}
