// The mask of the shadows from the moving lights (vk_rt.cpp): shaders/smlights.frag (the players from the cubes of
// the shadow maps, every device) and shaders/rtlightmask.frag (the same and the level by rays, LEVEL_RAYS, where the
// device traces rays) include it.

layout( set = 0, binding = 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
	vec4	rt[382];
} globals;
layout( set = 1, binding = 4 ) uniform sampler2D u_atlas;	// the depth of the shadow maps
layout( set = 2, binding = 0 ) uniform sampler2D u_depth;	// the depth of the scene, read pixel for pixel

#define W		globals.rt
#define TILE	256.0

// The target is two masks high: lights 0 to 3 above, 4 to 7 below (vk_rt.cpp). g_maskCoord is the pixel within the one
// this pixel is of, set by main; everything here that asked gl_FragCoord asks it.
vec2 g_maskCoord;
#define MASK_COORD	g_maskCoord
// how many pixels of the frame a pixel of this mask stands for (v_rtMaskScale, vk_rt.cpp; rtlightresolve.frag brings
// the mask to the size of the frame afterwards)
#define MASK_SCALE	W[381].y
#define COLUMNS	8

layout( location = 0 ) out vec4 o_color;

#include "scene.glsl"

#include "geom.glsl"

// A moving light is taken as this wide: the shadow of what stands near it is sharp at the feet and blurs with the distance
// behind them (found by the depth of what is in the way, then filtered as wide as that says), as the soft traced shadows
// of the light of the level do. Their hard outlines did not go with those (their picture 2026-11-24).
#define LIGHT_WIDTH		5.0
#define SOFT_SAMPLES	16
#define SOFT_TEXELS		16.0

// the depth in a tile that the plane of the surface has at a place of the tile: a wide filter on a slanted surface would
// otherwise find the surface itself in the way
float PlaneDepth( vec2 at, vec3 ahead, vec3 right, vec3 up, vec3 normal, float plane, float w, float a, float b ) {
	float facing = dot( ahead + right * at.x + up * at.y, normal );
	float there = ( facing < -0.001 && plane < 0.0 ) ? clamp( plane / facing, w * 0.5, w * 2.0 ) : w;

	return a + b / there;
}

// how much of a cube (the views of its six tiles from first on, around center; the depth from the tiles from atlas on, the
// same as first but for the tiles of the shooter alone) lies in front of the point q on a surface facing normal;
// close: how strong the shadow is of what stands right at the light; playersOnly: the cube holds players and not the level
float CubeShadow( int first, int atlas, vec3 center, vec3 q, vec3 normal, float close, bool playersOnly ) {
#ifdef LEVEL_RAYS
	// (with rays only the players are in the cubes)
	if ( W[21].x <= 0.0 ) {
		return 0.0;
	}
#endif
	vec3 d = q - center, ad = abs( d );
	int face;
	if ( ad.x >= ad.y && ad.x >= ad.z ) {
		face = d.x > 0.0 ? 0 : 1;
	} else if ( ad.y >= ad.z ) {
		face = d.y > 0.0 ? 2 : 3;
	} else {
		face = d.z > 0.0 ? 4 : 5;
	}
	int t = first + face;
	mat4 view = mat4( W[47 + t * 4], W[48 + t * 4], W[49 + t * 4], W[50 + t * 4] );
	vec4 clip = view * vec4( q, 1.0 );
	if ( clip.w <= 4.0 ) {
		return 0.0;
	}
	vec3 ndc = clip.xyz / clip.w;
	if ( ndc.z >= 1.0 ) {
		return 0.0;
	}
	int ta = atlas + face;
	vec2 corner = vec2( float( ta % COLUMNS ), float( ta / COLUMNS ) ) * TILE;
	ivec2 lo = ivec2( corner ), hi = ivec2( corner ) + ivec2( int( TILE ) - 1 );
	// The depth goes with 1 / distance (near plane 4): a fixed margin in it was 1 unit at 100 from the light and 19 at
	// 400, so what stands just behind a ledge kept the light. In units: about what a texel spans at that distance.
	float margin = ( 1.5 + clip.w / 96.0 ) * 4.0 / ( clip.w * clip.w );
	vec3 right = vec3( view[0][0], view[1][0], view[2][0] );
	vec3 up = vec3( view[0][1], view[1][1], view[2][1] );
	vec3 ahead = vec3( view[0][3], view[1][3], view[2][3] );
	// (depth = a + b / distance along the axis of the tile, VK_SM_FaceView)
	float a = dot( vec3( view[0][2], view[1][2], view[2][2] ), ahead );
	float b = clip.z - a * clip.w;
	float plane = dot( d, normal );
	float spin = 6.2831853 * fract( 52.9829189 * fract( dot( MASK_COORD, vec2( 0.06711056, 0.00583715 ) ) ) );

	// what is in the way, and how far in front of the surface
	float nearer = 0.0, found = 0.0;
	for ( int i = 0; i < SOFT_SAMPLES; i++ ) {
		float angle = float( i ) * 2.39996 + spin;
		vec2 at = ndc.xy + vec2( cos( angle ), sin( angle ) ) * sqrt( ( float( i ) + 0.5 ) / float( SOFT_SAMPLES ) ) * SOFT_TEXELS * 2.0 / TILE;
		float depth = texelFetch( u_atlas, clamp( ivec2( corner + clamp( at * 0.5 + 0.5, 0.0, 0.999 ) * TILE ), lo, hi ), 0 ).r;

		if ( depth < PlaneDepth( at, ahead, right, up, normal, plane, clip.w, a, b ) - margin ) {
			nearer += b / ( depth - a );
			found += 1.0;
		}
	}
	if ( found <= 0.0 ) {
		return 0.0;
	}
	nearer /= found;
	float behind = max( clip.w - nearer, 0.0 );
	float width = clamp( LIGHT_WIDTH * behind / max( nearer, 4.0 ) / clip.w * TILE * 0.5, 1.0, SOFT_TEXELS );

	float blocked = 0.0;
	for ( int i = 0; i < SOFT_SAMPLES; i++ ) {
		float angle = float( i ) * 2.39996 + spin;
		vec2 at = ndc.xy + vec2( cos( angle ), sin( angle ) ) * sqrt( ( float( i ) + 0.5 ) / float( SOFT_SAMPLES ) ) * width * 2.0 / TILE;
		float depth = texelFetch( u_atlas, clamp( ivec2( corner + clamp( at * 0.5 + 0.5, 0.0, 0.999 ) * TILE ), lo, hi ), 0 ).r;

		blocked += ( depth < PlaneDepth( at, ahead, right, up, normal, plane, clip.w, a, b ) - margin ) ? 1.0 : 0.0;
	}
	blocked /= float( SOFT_SAMPLES );
	if ( playersOnly ) {
		// (the shadow of a player thins out far behind him; a cube with the level in it (no rays) does not, a wall must go on
		// hiding what is far behind it)
		float strength = 1.0 - 0.7 * smoothstep( 64.0, 256.0, behind );
		// (the body of the shooter right at a muzzle flash would black out half the room: it keeps a light shadow. This is in
		// linear light: 0.8 takes about half of what the eye sees, their word 2026-11-24: 0.65 was too faint when charging)
		if ( close < 1.0 ) {
			strength = mix( close, strength, smoothstep( 48.0, 112.0, nearer ) );
		}
		blocked *= strength;
	}
	return blocked;
}

// how much the level hides of the light from a to b (a point when b is a) at q, by rays against it (mask 1) with
// LEVEL_RAYS (the device traces rays, shaders/rtlightmask.frag); without, the level is in the cubes of CubeShadow
// (vk_rt.cpp VK_SM_BuildLevel) and this is 0. A blade is a line of light: each pixel takes
// four points of its own along it (one in each quarter, at another place in each pixel and frame), so the shadow of a
// pillar fades out over what the blade spans; from three points the same for every pixel it ended in a hard line.
// A blade dipping into the floor, or an explosion inside what it hit, still lights that surface (generic.frag
// DynamicLight takes it as standing a hand above the plane), so with lifted the ray goes to that place: rays to the point
// itself hit the floor they start on (their picture 2026-11-08: a dark hole where the tip of a blade touched the floor).
// Any other light is in the open: its ray goes to where it is, so a muzzle flash just past the plane of the far side of
// a pillar is hidden by that pillar (their picture 2026-11-24: that side lit through it).
// The rays also start at four places across the pixel of the mask, on the plane of the surface, so the edge of a shadow
// from a point of light gets steps between lit and dark instead of a staircase (mp/ffa2, below the fire bowls).
float LevelTaken( vec3 q, vec3 p, vec3 normal, vec3 a, vec3 b, bool lifted ) {
#ifdef LEVEL_RAYS
	const int POINTS = 4;
	const vec2 across[POINTS] = vec2[POINTS]( vec2( -0.375, -0.125 ), vec2( 0.125, -0.375 ), vec2( 0.375, 0.125 ), vec2( -0.125, 0.375 ) );
	vec2 middle = ( floor( MASK_COORD ) + 0.5 ) * MASK_SCALE;
	uvec3 v = uvec3( uvec2( MASK_COORD ), uint( W[13].w ) );
	float hidden = 0.0, counted = 0.0;

	v = v * 1664525u + 1013904223u;
	v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
	v ^= v >> 16u;
	for ( int i = 0; i < POINTS; i++ ) {
		v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
		float along = ( float( i ) + float( v.x & 0xffffu ) / 65536.0 ) / float( POINTS );
		vec3 point = mix( a, b, along );
		if ( lifted ) {
			float above = dot( normal, point - p );

			if ( above <= -12.0 ) {
				continue;
			}
			point += normal * max( 16.0 - above, 0.0 );
		}
		vec3 ray = SceneRay( middle + across[i] * MASK_SCALE );
		float facing = dot( ray, normal );
		vec3 from = q;
		if ( abs( facing ) > 1e-4 ) {
			vec3 onPlane = W[4].xyz + ray * ( dot( p - W[4].xyz, normal ) / facing );
			// (a surface seen nearly edge on: that place can be far off, the pixel stays where it is)
			if ( distance( onPlane, p ) < 16.0 ) {
				from = onPlane + q - p;
			}
		}
		vec3 to = point - from;
		float d = length( to );

		counted += 1.0;
		if ( d > 2.0 && LevelBlocks( from, to / d, d - 2.0 ) ) {
			hidden += 1.0;
		}
	}
	return counted > 0.0 ? hidden / counted : 0.0;
#else
	return 0.0;
#endif
}

// how much of light k the players and the level take at p: the players from the cubes at three points along a blade
// (as VK_SM_DrawAtlas has them), blended, so their shadow is soft along the blade and sweeps as it turns; the level by
// LevelTaken. What the one does not take the other may.
float LightShadow( int k, vec3 p, vec3 normal ) {
	// (lights 4 to 7: rows 372 on, the level alone hides them, by rays)
	int row = k < 2 ? 38 + k * 2 : k < 4 ? 303 + ( k - 2 ) * 2 : 372 + ( k - 4 ) * 2;
	vec4 a = W[row], b = W[row + 1];
	float range = a.w;

	if ( range <= 0.0 ) {
		return 0.0;
	}
	vec3 nearest = NearestOnSegment( a.xyz, b.xyz, p );
	// (Out of its reach the light adds nothing whatever this says. "Hidden" so the filtered mask does not carry "lit" from
	// a far wall onto the dark floor in front of it, a dashed line of light along such edges.)
	if ( distance( nearest, p ) >= range ) {
		return 1.0;
	}
	// The normal here comes from the depth, and in a corner or at the edge of an object it can be the one of the surface
	// next to this pixel. Facing away from a light it said "nothing hides it", which left a dotted line of light along
	// the edges of a shadow (mp/ffa2, 2026-11-24): such a pixel is tested from a step towards the light instead.
	if ( dot( normal, nearest - p ) < -12.0 ) {
		normal = normalize( nearest - p );
	}
	vec3 q = p + normal * ( 3.0 + distance( nearest, p ) / 128.0 );
	float players = 0.0;
	if ( b.w <= 0.0 ) {
		int first = k < 2 ? 16 + k * 18 : 52 + ( k - 2 ) * 6;
#ifdef LEVEL_RAYS
		// a light may be set to have the shadows of the players alone (-2) or of the level alone (-3, -4: inside what
		// it hit), vk_rt.cpp; without rays the level is in the cubes of the players and both stay
		bool lifted = ( b.w < -0.5 && b.w > -1.5 ) || b.w < -3.5;
		if ( b.w > -2.5 && k < 4 ) {
			players = CubeShadow( first, first, a.xyz, q, normal, 0.8, true );
		}
		if ( b.w < -1.5 && b.w > -2.5 ) {
			return players;
		}
		return 1.0 - ( 1.0 - players ) * ( 1.0 - LevelTaken( q, p, normal, a.xyz, a.xyz, lifted ) );
#else
		// The level is in this cube, so the shooter right at the light is not (vk_rt.cpp VK_SM_DrawAtlas): he is in
		// tiles of his own (64 + 6 k, empty for anyone else's light), and only his shadow is made a light one there.
		players = CubeShadow( first, first, a.xyz, q, normal, 1.0, false );
		players = 1.0 - ( 1.0 - players ) * ( 1.0 - CubeShadow( first, 64 + k * 6, a.xyz, q, normal, 0.8, true ) );
#endif
		return 1.0 - ( 1.0 - players ) * ( 1.0 - LevelTaken( q, p, normal, a.xyz, a.xyz, b.w < 0.0 ) );
	}
	// (where along the blade the three are this frame: rows[21] y, z, w, they jump every frame, VK_SM_DrawAtlas)
	for ( int i = 0; i < 3; i++ ) {
		int first = 16 + ( k * 3 + i ) * 6;

#ifdef LEVEL_RAYS
		players += CubeShadow( first, first, mix( a.xyz, b.xyz, W[21][1 + i] ), q, normal, 1.0, true );
#else
		players += CubeShadow( first, first, mix( a.xyz, b.xyz, W[21][1 + i] ), q, normal, 1.0, false );
#endif
	}
	return 1.0 - ( 1.0 - players / 3.0 ) * ( 1.0 - LevelTaken( q, p, normal, a.xyz, b.xyz, true ) );
}

#include "rtmask.glsl"

void main() {
	// which of the two masks this pixel is in (W[381].x: the height of one)
	bool lower = gl_FragCoord.y >= W[381].x;
	g_maskCoord = lower ? gl_FragCoord.xy - vec2( 0.0, W[381].x ) : gl_FragCoord.xy;
#ifndef LEVEL_RAYS
	if ( lower ) {
		o_color = vec4( 0.0 );
		return;
	}
#endif
	if ( lower && W[372].w <= 0.0 ) {
		// (they are filled from the front: no light 4, none after it)
		o_color = vec4( 0.0 );
		return;
	}
	vec2 pixel = FramePixel();
	vec3 p = ScenePoint( pixel );
	vec3 eye = W[4].xyz;

	float d = distance( p, eye );

	if ( d > 8192.0 || d < VIEW_WEAPON_RANGE ) {
		o_color = vec4( 0.0 );
		return;
	}
#ifdef LEVEL_RAYS
	// Out of reach of all four lights of this mask: what LightShadow() gives then is known without the way the place
	// faces and where it lies on the level (four more depths and a ray, which every pixel of the picture took first;
	// OnLevel() moves it by less than ten units, and how far it is from a light changes by no more than that).
	{
		vec4 out_ = vec4( 0.0 );
		bool near = false;

		for ( int i = 0; i < 4; i++ ) {
			int k = lower ? 4 + i : i;
			int row = k < 2 ? 38 + k * 2 : k < 4 ? 303 + ( k - 2 ) * 2 : 372 + ( k - 4 ) * 2;
			vec4 a = W[row], b = W[row + 1];

			if ( a.w > 0.0 ) {
				out_[i] = 1.0;
				near = near || distance( NearestOnSegment( a.xyz, b.xyz, p ), p ) < a.w + 10.0;
			}
		}
		if ( !near ) {
			o_color = out_;
			return;
		}
	}
#endif
	vec3 normal = SceneSurface( pixel, p, normalize( eye - p ) );
#ifdef LEVEL_RAYS
	p = OnLevel( p, normal );
#endif
	if ( lower ) {
#ifdef LEVEL_RAYS
		o_color = vec4( LightShadow( 4, p, normal ), LightShadow( 5, p, normal ), LightShadow( 6, p, normal ), LightShadow( 7, p, normal ) );
#else
		o_color = vec4( 0.0 );
#endif
		return;
	}
	o_color = vec4( LightShadow( 0, p, normal ), LightShadow( 1, p, normal ), LightShadow( 2, p, normal ), LightShadow( 3, p, normal ) );
}
