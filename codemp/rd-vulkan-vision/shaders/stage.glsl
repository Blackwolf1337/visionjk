// One stage of a .shader script on a vertex: what rd-vanilla computes per vertex on the CPU (tr_shade_calc.cpp).
// Included by the vertex shaders, which differ in where the vertex comes from.

#include "common.glsl"

struct StageInput {
	vec3	position;		// in model space
	vec3	normal;
	vec2	texCoord;
	vec2	lightmap0, lightmap1, lightmap2, lightmap3;
	vec4	color;
};

layout( location = 0 ) out vec2 v_texCoord0;
layout( location = 1 ) out vec2 v_texCoord1;
layout( location = 2 ) out vec4 v_color;
layout( location = 3 ) out vec3 v_position;		// in model space
layout( location = 4 ) out float v_depth;		// distance in front of the eye

const float TWO_PI = 6.28318530718;

vec2 GenTexCoord( uint gen, vec4 vectors[2], vec3 viewOrigin, StageInput v ) {
	switch ( gen ) {
	case TCGEN_LIGHTMAP:	return v.lightmap0;
	case TCGEN_LIGHTMAP1:	return v.lightmap1;
	case TCGEN_LIGHTMAP2:	return v.lightmap2;
	case TCGEN_LIGHTMAP3:	return v.lightmap3;
	case TCGEN_TEXTURE:		return v.texCoord;
	case TCGEN_ENVIRONMENT: {
		vec3 viewer = normalize( viewOrigin - v.position );
		float d = dot( v.normal, viewer );
		vec3 reflected = v.normal * 2.0 * d - viewer;
		return vec2( 0.5 + reflected.y * 0.5, 0.5 - reflected.z * 0.5 );
	}
	case TCGEN_VECTOR:		return vec2( dot( v.position, vectors[0].xyz ), dot( v.position, vectors[1].xyz ) );
	default:				return vec2( 0.0 );
	}
}

vec2 ModTexCoord( vec2 st, vec4 mat[2], vec4 turb, StageInput v ) {
	vec2 result = vec2( dot( mat[0].xy, st ) + mat[0].z, dot( mat[1].xy, st ) + mat[1].z );

	if ( turb != vec4( 0.0 ) ) {
		float sinS = sin( ( ( v.position.x + v.position.z ) * ( 0.125 / 128.0 ) + mat[0].w ) * TWO_PI );
		float sinT = sin( ( v.position.y * ( 0.125 / 128.0 ) + mat[0].w ) * TWO_PI );

		result += vec2( turb.x * sinS + turb.y * sinT, turb.z * sinS + turb.w * sinT );
	}
	return result;
}

void StageVertex( StageInput v ) {
	Draw d = draws[push.draw];
	Transform t = transforms[d.transform];

	gl_Position = t.mvp * vec4( v.position, 1.0 );
	v_position = v.position;
	v_depth = gl_Position.w;
	// the depth range of OpenGL is -w to w, the one of Vulkan 0 to w
	gl_Position.z = ( gl_Position.z + gl_Position.w ) * 0.5;

	v_texCoord0 = ModTexCoord( GenTexCoord( TCGEN0( d.flags ), d.tcGenVec0, t.viewOrigin.xyz, v ), d.texMat0, d.texTurb0, v );
	v_texCoord1 = ModTexCoord( GenTexCoord( TCGEN1( d.flags ), d.tcGenVec1, t.viewOrigin.xyz, v ), d.texMat1, d.texTurb1, v );
	if ( RGBGEN( d.flags ) != 0u ) {
		// rgbGen lightingDiffuse: the light of the entity on the normal, times the color of the entity for
		// lightingDiffuseEntity (RB_CalcDiffuseColor and RB_CalcDiffuseEntityColor of rd-vanilla)
		float incoming = max( dot( v.normal, t.lightDir.xyz ), 0.0 );
		vec3 light = min( t.ambientLight.rgb + incoming * t.directedLight.rgb, vec3( 1.0 ) );

		v_color = vec4( light * d.baseColor.rgb, clamp( d.baseColor.a + d.vertColor.a * v.color.a, 0.0, 1.0 ) );
	} else {
		v_color = clamp( d.baseColor + d.vertColor * v.color, 0.0, 1.0 );
	}
}
