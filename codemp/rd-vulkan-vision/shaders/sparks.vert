#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

// v_gpuSparks (vk_sparks.cpp VK_SparksDraw): the sparks of the ring the device simulates (shaders/sparks.comp), six
// vertices each (two triangles, no index buffer), drawn with generic.frag over the solid level, added. A spark is a
// streak along the way it flies, as long as the distance it goes in a moment, as wide as its size, from its head back
// to its tail; its colour cools from its hot one through orange and deep red to nothing as its life runs out. Where it
// is dead its triangles have no area. An ember (one that lies on a floor, the fifth row) is a round dot that cools
// from the heat it landed with to a dark red glow, flickering, and goes out; the second draw (probeCount 1) lays the
// glow it throws round itself flat onto that floor, and has no area for the rest. One that floats (fifth row y above 0,
// the embers over lava) is a thin glowing speck drawn out along its way, that comes up softly, flickers its own way
// and cools to a dark red; never thinner than about a pixel, and that much fainter, so that far ones do not twinkle as
// single hard pixels. The address of the ring in tex5 and tex6 of the draw (low and high bits).

#include "stage.glsl"

layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer Sparks {
	vec4	rows[];
};

#define VERTEX		uint( gl_VertexIndex )

// the colour of something that glows as hot as heat (0 cold, 1 white hot), its brightness left to the caller
vec3 Glow( float heat ) {
	const vec3 keys[6] = vec3[6]( vec3( 0.0 ), vec3( 0.35, 0.02, 0.0 ), vec3( 1.0, 0.12, 0.0 ), vec3( 1.0, 0.42, 0.04 ),
		vec3( 1.0, 0.78, 0.32 ), vec3( 1.0, 0.95, 0.85 ) );
	float x = clamp( heat, 0.0, 1.0 ) * 5.0;
	int k = min( int( x ), 4 );
	return mix( keys[k], keys[k + 1], x - float( k ) );
}

// the light of a spark as hot as heat whose hot colour is hot: the colour of the heat, as bright as the hot one at its
// hottest and much darker as it cools, the hot one itself at the top
vec3 HeatColor( float heat, vec3 hot ) {
	float bright = max( hot.r, max( hot.g, hot.b ) );
	return mix( Glow( heat ) * bright * pow( clamp( heat, 0.0, 1.0 ), 1.5 ), hot, smoothstep( 0.75, 1.0, heat ) );
}

void Nothing() {
	StageVertex( StageInput( vec3( 0.0 ), vec3( 0.0, 0.0, 1.0 ), vec2( 0.0 ), vec2( 0.0 ), vec2( 0.0 ), vec2( 0.0 ), vec2( 0.0 ), vec4( 0.0 ) ) );
}

void main() {
	Draw d = DRAWS( DRAW );
	Transform t = TRANSFORMS( d.transform );
	Sparks sparks = Sparks( uvec2( d.tex5, d.tex6 ) );
	uint id = VERTEX / 6u, corner = VERTEX % 6u, r = id * 5u;
	vec4 a = sparks.rows[r], v = sparks.rows[r + 1u], c = sparks.rows[r + 2u], m = sparks.rows[r + 3u], lie = sparks.rows[r + 4u];
	// the corners of the two triangles: 0 head left, 1 head right, 2 tail right, 0, 2, 3 tail left
	const uint order[6] = uint[6]( 0u, 1u, 2u, 0u, 2u, 3u );
	uint k = order[corner];
	bool ember = lie.w > 0.0;
	bool floorGlow = d.probeCount == 1u;

	if ( a.w <= 0.0 || ( floorGlow && !ember ) ) {
		Nothing();
		return;
	}
	vec3 eye = t.viewOrigin.xyz;
	vec3 toEye = normalize( eye - a.xyz );
	vec2 corner2 = vec2( k == 0u || k == 3u ? -1.0 : 1.0, k < 2u ? 1.0 : -1.0 );

	if ( ember ) {
		float left = clamp( a.w / lie.w, 0.0, 1.0 );		// 1 just landed, 0 out
		// cooling fast at first to a dark red that lasts, out over the last quarter
		float heat = mix( 0.3, v.x, left * left ) * smoothstep( 0.0, 0.25, left );
		// flickering, each ember its own way
		float phase = float( id ) * 1.618;
		float flicker = 0.8 + 0.12 * sin( a.w * 11.0 + phase ) + 0.08 * sin( a.w * 23.0 + phase * 3.7 );
		vec3 color = HeatColor( heat, c.rgb ) * flicker * 1.4;
		vec3 position;

		if ( floorGlow ) {
			// flat on the floor under it, five times its size, faint, and gone sooner than the ember as it cools
			vec3 n = normalize( lie.xyz );
			vec3 s1 = normalize( abs( n.z ) < 0.9 ? cross( n, vec3( 0.0, 0.0, 1.0 ) ) : cross( n, vec3( 1.0, 0.0, 0.0 ) ) );
			vec3 s2 = cross( n, s1 );
			float radius = c.w * 5.0;
			position = a.xyz - n * 0.3 + ( s1 * corner2.x + s2 * corner2.y ) * radius;
			color *= 0.11 * smoothstep( 0.15, 0.6, heat );
		} else {
			// a round dot facing the eye
			vec3 s1 = normalize( cross( toEye, abs( toEye.z ) < 0.9 ? vec3( 0.0, 0.0, 1.0 ) : vec3( 1.0, 0.0, 0.0 ) ) );
			vec3 s2 = cross( toEye, s1 );
			float size = c.w * 1.4;
			position = a.xyz + ( s1 * corner2.x + s2 * corner2.y ) * size;
		}
		vec2 texCoord = corner2 * 0.5 + 0.5;
		StageVertex( StageInput( position, floorGlow ? lie.xyz : toEye, texCoord, texCoord, vec2( 0.0 ), vec2( 0.0 ), vec2( 0.0 ), vec4( color, 1.0 ) ) );
		return;
	}

	float left = clamp( a.w / max( v.w, 0.001 ), 0.0, 1.0 );		// 1 new, 0 dead
	// cooling: m.w how much sooner than its life
	float heat = pow( left, max( m.w, 0.1 ) );

	if ( lie.w == 0.0 && lie.y > 0.0 ) {
		if ( floorGlow ) {
			Nothing();
			return;
		}
		float born = v.w - a.w;			// seconds since it was made
		float own = lie.z;
		// (two rates that are no multiples of each other, so no ember repeats itself soon, and none blinks with its
		// neighbour; a hot one is steadier)
		float flicker = 1.0 - ( 0.28 * ( 0.5 + 0.5 * sin( born * ( 7.0 + 9.0 * own ) + own * 40.0 ) )
			+ 0.22 * ( 0.5 + 0.5 * sin( born * ( 19.0 + 11.0 * own ) + own * 91.0 ) ) ) * ( 1.0 - 0.5 * heat );
		// A thin bright speck, as an ember is: millimetres wide. Never thinner than about three quarters of a pixel, and
		// that much fainter. Drawn out along its way across the view by what it flies while a shutter is open (lie.x
		// seconds), which is what makes the thin bright lines of embers on film; the longer the line the fainter.
		float width = c.w * ( 0.6 + 0.4 * left );
		float shown = max( width, distance( eye, a.xyz ) * 0.0007 );
		vec3 way = v.xyz - toEye * dot( v.xyz, toEye );
		float speed = length( way );
		vec3 along = speed > 0.01 ? way / speed : normalize( cross( toEye, abs( toEye.z ) < 0.9 ? vec3( 0.0, 0.0, 1.0 ) : vec3( 1.0, 0.0, 0.0 ) ) );
		vec3 across = cross( along, toEye );
		// (the round picture of the lights is bright only in its middle: the quad is that much wider than the line that
		// shows, its skirt is the faint glow beside it)
		float side = shown * 2.2;
		float reach = side + speed * lie.x * 0.5;
		vec3 color = HeatColor( heat, c.rgb ) * flicker * smoothstep( 0.0, 0.3, born ) * ( width / shown ) * pow( side / reach, 0.25 );
		if ( m.w == 0.0 ) {
			// dust in light: it does not glow of its own. The colour it was given, coming and going slowly, and glinting
			// as it turns.
			color = c.rgb * smoothstep( 0.0, 1.2, born ) * smoothstep( 0.0, 0.3, left ) * ( width / shown )
				* ( 0.55 + 0.45 * sin( born * ( 1.3 + 2.2 * own ) + own * 40.0 ) );
		}
		vec3 position = a.xyz + across * ( corner2.x * side ) + along * ( corner2.y * reach );
		vec2 texCoord = corner2 * 0.5 + 0.5;

		StageVertex( StageInput( position, toEye, texCoord, texCoord, vec2( 0.0 ), vec2( 0.0 ), vec2( 0.0 ), vec4( color, 1.0 ) ) );
		return;
	}
	// the streak: the way it went in a moment, longer while it is hot, at least as long as it is wide
	vec3 trail = v.xyz * mix( 0.012, 0.03, heat );
	float len = length( trail );
	float size = c.w * ( 0.5 + 0.5 * left );
	if ( len < size ) {
		trail = ( len > 0.001 ? trail / len : vec3( 0.0, 0.0, 1.0 ) ) * size;
	}
	vec3 side = cross( trail, toEye );
	side = ( dot( side, side ) > 1e-8 ? normalize( side ) : vec3( 1.0, 0.0, 0.0 ) ) * size;
	vec3 head = a.xyz + trail * 0.25, tail = a.xyz - trail;
	vec3 position = ( k < 2u ? head : tail ) + ( k == 0u || k == 3u ? side : -side );
	vec2 texCoord = vec2( k == 0u || k == 3u ? 0.0 : 1.0, k < 2u ? 0.35 : 0.65 );

	StageVertex( StageInput( position, toEye, texCoord, texCoord, vec2( 0.0 ), vec2( 0.0 ), vec2( 0.0 ), vec4( HeatColor( heat, c.rgb ), 1.0 ) ) );
}
