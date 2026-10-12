#version 460

#include "common.glsl"

#ifdef VK_GLES
// the two textures of the draw, the device binds them for every draw call
uniform sampler2D u_tex0;
uniform sampler2D u_tex1;
#define TEX0( draw_ )	u_tex0
#define TEX1( draw_ )	u_tex1
#else
BINDING( 1, 0 ) uniform sampler2D u_textures[];
#define TEX0( draw_ )	u_textures[nonuniformEXT( draw_.tex0 )]
#define TEX1( draw_ )	u_textures[nonuniformEXT( draw_.tex1 )]
#endif

VARYING( 0 ) in vec2 v_texCoord0;
VARYING( 1 ) in vec2 v_texCoord1;
VARYING( 2 ) in vec4 v_color;
VARYING( 3 ) in vec3 v_position;
VARYING( 4 ) in float v_depth;
#ifdef VK_GLES
in float v_clip;
#else
VARYING( 5 ) flat in uint v_draw;
#endif

layout( location = 0 ) out vec4 o_color;

// The gold of the Vision Temple on the surfaces of a level whose shader script says so ("q3map_vision gold",
// rd-vanilla/tr_shader.cpp): a place on them is shaded as shaders/temple.frag shades the gold it finds itself,
// by where it is and not by a texture. (The clouds of vision.glsl are not used here, their numbers are what
// makes the file complete.)
#define CLOUD_ROOF	266.0
#define CLOUD_DISC	vec3( 0.0, 0.0, 208.0 )
#define CLOUD_LANE	vec3( -524.0, -170.0, 28.0 )
#include "vision.glsl"
#include "light.glsl"
#include "gold.glsl"

vec4 Gold( Draw d ) {
	vec3 ro = TRANSFORMS( d.transform ).viewOrigin.xyz;
	vec3 rd = normalize( v_position - ro );
	vec3 color = goldLit( v_position, rd, d.parm.y, d.parm.z );
	vec2 at = gl_FragCoord.xy / globals.screen.xy * 2.0 - 1.0;

	color = mix( color, haze( rd ), 1.0 - exp( -length( v_position - ro ) / 6000.0 ) );
	// a little darker towards the corners, and the curve, as the sky behind it has them
	color *= 1.0 - 0.22 * dot( at, at );
	return vec4( film( color * 0.62 ), 1.0 );
}

// One dynamic light on what is already drawn there, added by a pass of its own. rd-vanilla projects the round
// light image onto every triangle along its normal and fades it with the distance of the light from the plane
// of the triangle (tr_shade.cpp ProjectDlightTexture2). The same per pixel: tex0 is the texture of the surface,
// tex1 the light image.
vec4 DynamicLight( Draw d ) {
	vec3 normal = normalize( cross( dFdx( v_position ), dFdy( v_position ) ) );
	vec3 toLight = d.parm.xyz - v_position;
	float radius = d.parm.w;

	// the side of the triangle that is looked at
	if ( dot( normal, TRANSFORMS( d.transform ).viewOrigin.xyz - v_position ) < 0.0 ) {
		normal = -normal;
	}
	float fac = dot( normal, toLight );
	if ( fac <= 0.0 || fac >= radius ) {
		discard;
	}
	float modulate = 1.0 - ( fac * fac ) / ( radius * radius );

	// where the pixel is in the plane, seen from the point under the light. The image is round, any axes do.
	vec3 inPlane = normal * fac - toLight;
	vec3 e1 = normalize( abs( normal.z ) < 0.9 ? cross( normal, vec3( 0.0, 0.0, 1.0 ) ) : cross( normal, vec3( 1.0, 0.0, 0.0 ) ) );
	vec3 e2 = cross( normal, e1 );
	vec2 st = vec2( dot( inPlane, e1 ), dot( inPlane, e2 ) ) * ( 0.5 / sqrt( radius * radius - fac * fac ) ) + 0.5;
	if ( any( lessThan( st, vec2( 0.0 ) ) ) || any( greaterThan( st, vec2( 1.0 ) ) ) ) {
		discard;
	}
	vec3 light = texture( TEX1( d ), st ).rgb * d.baseColor.rgb * modulate;
	return vec4( texture( TEX0( d ), v_texCoord0 ).rgb * light, 1.0 );
}

void main() {
	Draw d = DRAWS( v_draw );

#ifdef VK_GLES
	// the plane of a portal or mirror, see stage.glsl
	if ( v_clip < 0.0 ) {
		discard;
	}
#endif
	if ( DLIGHT( d.flags ) ) {
		o_color = DynamicLight( d );
		return;
	}
	if ( VISION( d.flags ) == 1u ) {
		o_color = Gold( d );
		return;
	}

	vec4 color = texture( TEX0( d ), v_texCoord0 ) * v_color;

	// the second texture of a stage, combined the way glTexEnv did it
	uint env = TEXENV( d.flags );
	if ( env != 0u ) {
		vec4 second = texture( TEX1( d ), v_texCoord1 );

		if ( env == 1u ) {				// modulate
			color *= second;
		} else if ( env == 2u ) {		// add
			color = vec4( color.rgb + second.rgb, color.a * second.a );
		} else if ( env == 3u ) {		// decal
			color.rgb = mix( color.rgb, second.rgb, second.a );
		} else {						// replace
			color = second;
		}
	}

	uint atest = ATEST( d.flags );
	if ( atest == 1u && color.a <= 0.0 ) {
		discard;
	} else if ( atest == 2u && color.a >= 0.5 ) {
		discard;
	} else if ( atest == 3u && color.a < 0.5 ) {
		discard;
	} else if ( atest == 4u && color.a < 0.75 ) {
		discard;
	}

	// fog over everything as glFog did it: the color fades to the fog color with the distance, alpha stays
	uint fog = FOG( d.flags );
	if ( fog != 0u ) {
		float f;

		if ( fog == 1u ) {
			float x = d.fogParm.x * v_depth;
			f = exp( -x * x );
		} else {
			f = ( d.fogParm.y - v_depth ) / ( d.fogParm.y - d.fogParm.x );
		}
		color.rgb = mix( d.fogColor.rgb, color.rgb, clamp( f, 0.0, 1.0 ) );
	}
	o_color = color;
}
