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

// The tangent frame of a pixel from how its position and its texture coordinates change to the next pixel (Schüler,
// "Followup: Normal mapping without precomputed tangents", 2013): the vertices carry no tangents
mat3 CotangentFrame( vec3 n, vec3 p, vec2 uv ) {
	vec3 dp1 = dFdx( p ), dp2 = dFdy( p );
	vec2 duv1 = dFdx( uv ), duv2 = dFdy( uv );
	vec3 dp2perp = cross( dp2, n ), dp1perp = cross( n, dp1 );
	vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
	vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
	float invmax = inversesqrt( max( max( dot( t, t ), dot( b, b ) ), 1e-20 ) );

	return mat3( t * invmax, b * invmax, n );
}

// A stage of a model with a material (shader keywords normalMap and specular): lit per pixel by the light of its
// entity as the vertices are lit otherwise (ambient, directed and its direction in model space), with a broad
// shine of the surface, a sharp one of a clear coat over it that is smooth over the bumps, and the light of the
// room the coat throws back at a flat angle (Fresnel with F0 0.04)
vec3 MaterialLight( Draw d, vec3 albedo ) {
	Transform t = TRANSFORMS( d.transform );
	vec3 toEye = normalize( t.viewOrigin.xyz - v_position );
	vec3 smooth_ = normalize( v_normal );
	vec3 n = smooth_;

	if ( d.material.w > 0.0 ) {
		vec3 bump = texture( TEX1( d ), v_texCoord0 ).xyz * 2.0 - 1.0;
		n = normalize( CotangentFrame( smooth_, v_position, v_texCoord0 ) * bump );
	}
	vec3 l = normalize( t.lightDir.xyz );
	vec3 h = normalize( l + toEye );
	float facing = max( dot( n, l ), 0.0 );
	vec3 color = albedo * min( t.ambientLight.rgb + facing * t.directedLight.rgb, vec3( 1.0 ) );

	color += t.directedLight.rgb * d.material.x * pow( max( dot( n, h ), 0.0 ), max( d.material.y, 1.0 ) ) * step( 0.0001, facing );
	if ( d.material.z > 0.0 ) {
		float fresnel = 0.04 + 0.96 * pow( 1.0 - max( dot( smooth_, toEye ), 0.0 ), 5.0 );

		color += t.directedLight.rgb * d.material.z * 2.0 * pow( max( dot( smooth_, h ), 0.0 ), 300.0 ) * step( 0.0001, dot( smooth_, l ) );
		color += ( t.ambientLight.rgb + t.directedLight.rgb * 0.3 ) * d.material.z * fresnel * 0.6;
	}
	return color;
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
	if ( MATERIAL( d.flags ) ) {
		color.rgb = MaterialLight( d, color.rgb );
	}

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
