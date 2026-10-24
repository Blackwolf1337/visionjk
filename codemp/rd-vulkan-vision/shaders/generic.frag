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
VARYING( 6 ) NOPERSPECTIVE in vec2 v_screenCoord0;
VARYING( 7 ) in vec3 v_normal;
#ifdef VK_GLES
in float v_clip;
#else
VARYING( 5 ) flat in uint v_draw;
#endif

layout( location = 0 ) out vec4 o_color;

// One dynamic light on what is already drawn there, added by a pass of its own. rd-vanilla projects the round
// light image onto every triangle along its normal and fades it with the distance of the light from the plane
// of the triangle (tr_shade.cpp ProjectDlightTexture2). The same per pixel: tex0 is the texture of the surface,
// tex1 the light image.
vec4 DynamicLight( Draw d ) {
	// The normal of the vertices, exact for the faces of the level. The one of the plane of the pixels, from how the
	// position changes from one pixel to the next, only where they have none: far from the middle of a level and
	// on a floor seen flat that one is rough.
	vec3 normal = dot( v_normal, v_normal ) > 0.25 ? normalize( v_normal ) : normalize( cross( dFdx( v_position ), dFdy( v_position ) ) );
	vec3 toLight = d.parm.xyz - v_position;
	float radius = d.parm.w;

	// the side of the triangle that is looked at
	if ( dot( normal, TRANSFORMS( d.transform ).viewOrigin.xyz - v_position ) < 0.0 ) {
		normal = -normal;
	}
	// A light a little below the plane still lights it: the light of an explosion lies just under the floor it hit,
	// and two faces of a map a hair apart (which never show that they fight over the depth) would light up in rows
	// where the lower one wins, as on mp/ffa4.
	float fac = dot( normal, toLight );
	if ( fac <= -12.0 || fac >= radius ) {
		discard;
	}
	fac = max( abs( fac ), 0.001 );
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

	vec4 color = texture( TEX0( d ), ( TCGEN0( d.flags ) == TCGEN_REST ) ? v_screenCoord0 : v_texCoord0 ) * v_color;

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
