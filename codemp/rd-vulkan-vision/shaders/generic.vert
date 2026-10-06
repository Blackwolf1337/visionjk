#version 460

// one stage of a .shader script on vertices that come from the tess arrays of the back end. What rd-vanilla
// computes per vertex on the CPU (tr_shade_calc.cpp) is done here: texture coordinate generation and
// modification, the color.

#include "common.glsl"

layout( location = 0 ) in vec3 a_position;
layout( location = 1 ) in vec3 a_normal;
layout( location = 2 ) in vec2 a_texCoord;
layout( location = 3 ) in vec2 a_lightmap0;
layout( location = 4 ) in vec2 a_lightmap1;
layout( location = 5 ) in vec2 a_lightmap2;
layout( location = 6 ) in vec2 a_lightmap3;
layout( location = 7 ) in vec4 a_color;

layout( location = 0 ) out vec2 v_texCoord0;
layout( location = 1 ) out vec2 v_texCoord1;
layout( location = 2 ) out vec4 v_color;
layout( location = 3 ) out vec3 v_position;		// in model space
layout( location = 4 ) out float v_depth;		// distance in front of the eye

const float TWO_PI = 6.28318530718;

vec2 GenTexCoord( uint gen, vec4 vectors[2], vec3 viewOrigin ) {
	switch ( gen ) {
	case TCGEN_LIGHTMAP:	return a_lightmap0;
	case TCGEN_LIGHTMAP1:	return a_lightmap1;
	case TCGEN_LIGHTMAP2:	return a_lightmap2;
	case TCGEN_LIGHTMAP3:	return a_lightmap3;
	case TCGEN_TEXTURE:		return a_texCoord;
	case TCGEN_ENVIRONMENT: {
		vec3 viewer = normalize( viewOrigin - a_position );
		float d = dot( a_normal, viewer );
		vec3 reflected = a_normal * 2.0 * d - viewer;
		return vec2( 0.5 + reflected.y * 0.5, 0.5 - reflected.z * 0.5 );
	}
	case TCGEN_VECTOR:		return vec2( dot( a_position, vectors[0].xyz ), dot( a_position, vectors[1].xyz ) );
	default:				return vec2( 0.0 );
	}
}

vec2 ModTexCoord( vec2 st, vec4 mat[2], vec4 turb ) {
	vec2 result = vec2( dot( mat[0].xy, st ) + mat[0].z, dot( mat[1].xy, st ) + mat[1].z );

	if ( turb != vec4( 0.0 ) ) {
		float sinS = sin( ( ( a_position.x + a_position.z ) * ( 0.125 / 128.0 ) + mat[0].w ) * TWO_PI );
		float sinT = sin( ( a_position.y * ( 0.125 / 128.0 ) + mat[0].w ) * TWO_PI );

		result += vec2( turb.x * sinS + turb.y * sinT, turb.z * sinS + turb.w * sinT );
	}
	return result;
}

void main() {
	Draw d = draws[push.draw];
	Transform t = transforms[d.transform];

	gl_Position = t.mvp * vec4( a_position, 1.0 );
	v_position = a_position;
	v_depth = gl_Position.w;
	// the depth range of OpenGL is -w to w, the one of Vulkan 0 to w
	gl_Position.z = ( gl_Position.z + gl_Position.w ) * 0.5;

	v_texCoord0 = ModTexCoord( GenTexCoord( TCGEN0( d.flags ), d.tcGenVec0, t.viewOrigin.xyz ), d.texMat0, d.texTurb0 );
	v_texCoord1 = ModTexCoord( GenTexCoord( TCGEN1( d.flags ), d.tcGenVec1, t.viewOrigin.xyz ), d.texMat1, d.texTurb1 );
	v_color = clamp( d.baseColor + d.vertColor * a_color, 0.0, 1.0 );
}
