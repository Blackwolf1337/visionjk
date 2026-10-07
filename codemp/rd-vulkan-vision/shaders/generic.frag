#version 460

#include "common.glsl"

// The texture table and the samplers its textures are read with. A draw names a texture as its index in the
// table and, above bit 16, the index of its sampler (VK_SAMPLER_SHIFT, VK_MAX_SAMPLERS of vk_local.h).
layout( set = 1, binding = 0 ) uniform texture2D u_textures[];
layout( set = 1, binding = 1 ) uniform sampler u_samplers[32];

#define TEXTURE( ref )	sampler2D( u_textures[nonuniformEXT( ( ref ) & 0xffffu )], u_samplers[nonuniformEXT( ( ref ) >> 16 )] )

layout( location = 0 ) in vec2 v_texCoord0;
layout( location = 1 ) in vec2 v_texCoord1;
layout( location = 2 ) in vec4 v_color;
layout( location = 3 ) in vec3 v_position;
layout( location = 4 ) in float v_depth;
layout( location = 5 ) flat in uint v_draw;

layout( location = 0 ) out vec4 o_color;

// One dynamic light on what is already drawn there, added by a pass of its own. rd-vanilla projects the round
// light image onto every triangle along its normal and fades it with the distance of the light from the plane
// of the triangle (tr_shade.cpp ProjectDlightTexture2). The same per pixel: tex0 is the texture of the surface,
// tex1 the light image.
vec4 DynamicLight( Draw d ) {
	vec3 normal = normalize( cross( dFdx( v_position ), dFdy( v_position ) ) );
	vec3 toLight = d.parm.xyz - v_position;
	float radius = d.parm.w;

	// the side of the triangle that is looked at
	if ( dot( normal, transforms[d.transform].viewOrigin.xyz - v_position ) < 0.0 ) {
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
	vec3 light = texture( TEXTURE( d.tex1 ), st ).rgb * d.baseColor.rgb * modulate;
	return vec4( texture( TEXTURE( d.tex0 ), v_texCoord0 ).rgb * light, 1.0 );
}

void main() {
	Draw d = draws[v_draw];

	if ( DLIGHT( d.flags ) ) {
		o_color = DynamicLight( d );
		return;
	}

	vec4 color = texture( TEXTURE( d.tex0 ), v_texCoord0 ) * v_color;

	// the second texture of a stage, combined the way glTexEnv did it
	uint env = TEXENV( d.flags );
	if ( env != 0u ) {
		vec4 second = texture( TEXTURE( d.tex1 ), v_texCoord1 );

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
