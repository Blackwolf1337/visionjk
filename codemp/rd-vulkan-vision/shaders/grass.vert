#version 460

// A surface sprite of the level, grass and the like: what RB_VerticalSurfaceSprite(),
// RB_VerticalSurfaceSpriteWindPoint() and RB_OrientedSurfaceSprite() of tr_surfacesprites.cpp build on the CPU every
// frame. Where each one stands, how big it is and the random numbers of its fade were worked out once when the
// level was loaded (VK_PlaceGrass() in vk_world.cpp), four vertices a sprite, one for each corner. The view and the
// wind come as twelve rows in the bone buffer (VK_BindGrass()):
//
//   0  the eye, tr.refdef.time in msec            6, 7  the other two right vectors
//   1  ssfwdvector, curWindSpeed                  8  fade distance squared, 1 / (cut - fade squared), faderange, fadeScale
//   2  curWindGrassDir, force of the wind point   9  wind, windIdle of the stage
//   3  curWindPoint, the flags                    10  ssViewRight
//   4, 5  two of the four ssrightvectors          11  ssViewUp

#include "stage.glsl"

#ifdef VK_GLES
uniform Bones {
	vec4	bones[768];
};
#define VIEW		0u
#else
layout( set = 0, binding = 3, std430 ) readonly buffer Bones {
	vec4	bones[];
};
#define VIEW		DRAWS( DRAW ).bones
#endif

// the flags of row 3
#define GRASS_HANGDOWN		1u
#define GRASS_FLATTENED		2u
#define GRASS_ADDITIVE		4u
#define GRASS_ORIENTED		8u
#define GRASS_FACEUP		16u
#define GRASS_WINDPOINT		32u

layout( location = 0 ) in vec4 a_position;		// w: the light, 0 to 255
layout( location = 1 ) in vec4 a_size;			// width (negative: the texture is turned round), height, skew
layout( location = 2 ) in vec4 a_parm;			// randomchart of its fade, spread, corner + 4 * which right vector

void main() {
	uint base = VIEW;
	vec4 eye = bones[base];
	vec4 fwd = bones[base + 1u];
	vec4 windDir = bones[base + 2u];
	vec4 windPoint = bones[base + 3u];
	vec4 fade = bones[base + 8u];
	vec4 stageWind = bones[base + 9u];
	uint flags = uint( windPoint.w );
	uint parm = uint( a_parm.z );
	uint corner = parm & 3u;
	vec3 loc = a_position.xyz;
	vec3 toEye = eye.xyz - loc;

	// The alpha rd-vanilla interpolates from the corners of the triangle the sprite stands on: the squared distance
	// of the sprite to the eye plus its spread is the interpolated squared distance of the corners.
	float alphapos = 1.0 - ( dot( toEye, toEye ) + a_parm.y - fade.x ) * fade.y;
	float fadeStart = fade.z + ( 1.0 - fade.z ) * a_parm.x;
	float alpha = 1.0 - ( fadeStart - alphapos ) / fade.z;

	if ( alpha <= 0.0 ) {
		// faded out: a quad outside of the view, nothing is drawn
		gl_Position = vec4( 2.0, 2.0, 2.0, 1.0 );
		return;
	}
	alpha = min( alpha, 1.0 );
	float light = a_position.w;
	if ( ( flags & GRASS_ADDITIVE ) != 0u ) {
		light = ( 128.0 + light * 0.5 ) * alpha;
		alpha = 1.0;
	}
	// the bytes rd-vanilla hands on
	vec4 color = vec4( vec3( floor( min( light, 255.0 ) ) ), floor( alpha * 255.0 ) ) / 255.0;

	float width = a_size.x;
	float height = a_size.y;
	if ( fade.w != 0.0 && alphapos < 1.0 ) {
		width *= 1.0 + fade.w * ( 1.0 - alphapos );
	}

	vec3 top, right;
	float lean = 0.0;		// how far the top left corner leans along fwd, times the width

	if ( ( flags & GRASS_ORIENTED ) != 0u ) {
		if ( ( flags & GRASS_FACEUP ) != 0u ) {
			// flat on the ground, a unit above it
			float w = width * 0.5;
			vec2 offsets[4] = vec2[4]( vec2( w, -w ), vec2( w, w ), vec2( -w, w ), vec2( -w, -w ) );

			StageInput v = StageInput( loc + vec3( offsets[corner], 1.0 ), vec3( 0.0, 0.0, 1.0 ),
				vec2( 0.0 ), vec2( 0.0 ), vec2( 0.0 ), vec2( 0.0 ), vec2( 0.0 ), color );
			v.texCoord = vec2( corner < 2u ? 1.0 : 0.0, ( corner == 0u || corner == 3u ) ? 1.0 : 0.0 );
			v.lightmap0 = v.texCoord;
			StageVertex( v );
			return;
		}
		top = loc + height * bones[base + 11u].xyz;
		right = bones[base + 10u].xyz * ( width * 0.5 );
	} else {
		float angle = ( loc.x + loc.y ) * 0.02 + eye.w * 0.0015;
		float windSpeed = fwd.w;
		float wind = stageWind.x;
		float windIdle = stageWind.y;
		vec2 skew = a_size.zw;

		top.z = ( ( flags & GRASS_HANGDOWN ) != 0u ) ? loc.z - height : loc.z + height;

		// the wind point: the force falls off over 750 units from where it blows (RB_DrawVerticalSurfaceSprites)
		float pointForce = 0.0;
		vec2 pointDir = vec2( 0.0 );
		if ( ( flags & GRASS_WINDPOINT ) != 0u ) {
			vec2 d = loc.xy - windPoint.xy;
			float dist2 = dot( d, d );

			if ( dist2 < 750.0 * 750.0 ) {
				if ( dist2 < 1.0 ) {
					pointForce = windDir.w;
				} else {
					float inv = inversesqrt( dist2 );

					pointDir = d * inv;
					pointForce = windDir.w * ( 1.0 - 1.0 / ( inv * 750.0 ) );
				}
			}
		}

		if ( pointForce > 0.0 && wind > 0.0 ) {
			float force = min( pointForce, 1.0 );

			if ( windSpeed < 80.0 ) {
				float sway = height * windIdle * 0.1 * ( 1.0 + force );

				top.xy = loc.xy + skew + vec2( cos( angle ), sin( angle ) ) * sway;
			} else {
				top.xy = loc.xy + skew;
			}
			if ( windSpeed > 0.001 ) {
				top += height * wind * windDir.xyz;
			}
			top.xy += height * pointDir * force;
			top.z -= height * force * ( 0.75 + 0.15 * sin( ( eye.w + 500.0 * force ) * 0.01 ) );
			lean = 0.15;
		} else {
			top.xy = loc.xy + skew;
			if ( windIdle > 0.0 ) {
				top.xy += vec2( cos( angle ), sin( angle ) ) * ( height * windIdle * 0.075 );
			}
			if ( wind > 0.0 && windSpeed > 0.001 ) {
				float sway = height * wind * 0.075 * ( windSpeed < 40.0 ? windSpeed * ( 1.0 / 100.0 ) : 0.4 );

				top += height * wind * windDir.xyz;
				top.z += sin( angle * 2.5 ) * sway;
			}
			lean = 0.2;
		}

		if ( ( flags & GRASS_FLATTENED ) != 0u ) {
			right = vec3( sin( radians( loc.x ) ) * width, cos( radians( loc.x ) ) * height, 0.0 );
		} else {
			right = bones[base + 4u + ( parm >> 2 )].xyz * ( width * 0.5 );
		}
	}

	// bottom right, top right, top left, bottom left, as CQuickSpriteSystem has its texture coordinates
	vec3 position;
	if ( corner == 0u ) {
		position = loc + right;
	} else if ( corner == 1u ) {
		position = top + right;
	} else if ( corner == 2u ) {
		position = top - right;
		position.xy += fwd.xy * ( width * lean );
	} else {
		position = loc - right;
	}
	vec2 texCoord = vec2( corner < 2u ? 1.0 : 0.0, ( corner == 0u || corner == 3u ) ? 1.0 : 0.0 );

	StageVertex( StageInput( position, vec3( 0.0, 0.0, 1.0 ), texCoord, texCoord, vec2( 0.0 ), vec2( 0.0 ), vec2( 0.0 ), color ) );
}
