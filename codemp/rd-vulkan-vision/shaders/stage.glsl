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

// The wave functions of a .shader script, x counted in periods. rd-vanilla reads them from tables of 1024 steps
// (genFunc_t: 1 sin, 2 square, 3 triangle, 4 sawtooth, 5 inverse sawtooth).
float Wave( uint func, float x ) {
	float f = fract( x );

	switch ( func ) {
	case 1u:	return sin( f * TWO_PI );
	case 2u:	return ( f < 0.5 ) ? 1.0 : -1.0;
	case 3u:	return ( f < 0.25 ) ? f * 4.0 : ( ( f < 0.75 ) ? 2.0 - f * 4.0 : f * 4.0 - 4.0 );
	case 4u:	return f;
	case 5u:	return 1.0 - f;
	default:	return 0.0;
	}
}

// deformVertexes of a shader that has one of them: wave and bulge move a vertex along its normal, move shifts
// all of them (RB_CalcDeformVertexes, RB_CalcBulgeVertexes, RB_CalcMoveVertexes of rd-vanilla)
vec3 Deform( Draw d, StageInput v ) {
	uint kind = DEFORM( d.flags );

	if ( kind == 1u ) {
		float scale = d.deformWave.x;

		if ( d.deformWave.y != 0.0 ) {
			scale += d.deformWave.y * Wave( uint( d.deformWave.w ), d.deformWave.z + dot( d.deformVec.xyz, v.position ) + d.deformVec.w * v.texCoord.x );
		}
		return v.position + v.normal * scale;
	}
	if ( kind == 2u ) {
		return v.position + d.deformVec.xyz;
	}
	return v.position;
}

// Where a vertex is in a fog volume, as coordinates into the fog image: s grows with the distance from the eye,
// t says how much of the way lies in the fog (RB_CalcFogTexCoords of rd-vanilla)
vec2 FogCoords( vec3 position, vec4 vectors[2], float eyeT ) {
	float s = dot( position, vectors[0].xyz ) + vectors[0].w;
	float t = dot( position, vectors[1].xyz ) + vectors[1].w;

	if ( eyeT < 0.0 ) {
		// the eye is outside: cut the distance at the fog plane
		t = ( t < 1.0 ) ? 1.0 / 32.0 : 1.0 / 32.0 + 30.0 / 32.0 * t / ( t - eyeT );
	} else {
		t = ( t < 0.0 ) ? 1.0 / 32.0 : 31.0 / 32.0;
	}
	return vec2( s, t );
}

// how much fog there is at those coordinates, what the fog image holds (R_FogFactor)
float FogFactor( vec2 st ) {
	float s = st.x - 1.0 / 512.0;

	if ( s < 0.0 || st.y < 1.0 / 32.0 ) {
		return 0.0;
	}
	if ( st.y < 31.0 / 32.0 ) {
		s *= ( st.y - 1.0 / 32.0 ) / ( 30.0 / 32.0 );
	}
	return sqrt( min( s * 8.0, 1.0 ) );
}

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
	case TCGEN_FOG:			return FogCoords( v.position, draws[push.draw].fogVec, draws[push.draw].fogParm.z );
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

	v.position = Deform( d, v );
	gl_Position = t.mvp * vec4( v.position, 1.0 );
	v_position = v.position;
	// the view through a portal or mirror must not show what lies between the eye and the portal
	gl_ClipDistance[0] = ( t.clipPlane.xyz == vec3( 0.0 ) ) ? 1.0
		: dot( ( t.model * vec4( v.position, 1.0 ) ).xyz, t.clipPlane.xyz ) - t.clipPlane.w;
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

	uint agen = AGEN( d.flags );
	if ( agen == 1u ) {
		// alphaGen lightingSpecular (RB_CalcSpecularAlpha of rd-vanilla): how much of the light is thrown at the
		// eye. A model takes the direction its entity is lit from, everything else one fixed lamp of the level.
		vec3 lightDir = ( t.lightDir.w != 0.0 ) ? t.lightDir.xyz : normalize( vec3( -960.0, 1980.0, 96.0 ) - v.position );
		vec3 reflected = v.normal * ( 2.0 * dot( v.normal, lightDir ) ) - lightDir;
		vec3 viewer = t.viewOrigin.xyz - v.position;
		float l = dot( reflected, viewer ) * inversesqrt( dot( viewer, viewer ) );

		v_color.a = ( l < 0.0 ) ? 0.0 : min( l * l * l * l, 1.0 );
	} else if ( agen == 2u ) {
		// alphaGen portal: nothing at the eye, all of it from the portal range on
		v_color.a = clamp( length( v.position - t.viewOrigin.xyz ) / d.parm.x, 0.0, 1.0 );
	}

	// a stage in a fog volume may ask to fade with the fog instead of getting a fog pass (adjustColorsForFog)
	uint acff = ACFF( d.flags );
	if ( acff != 0u ) {
		float f = 1.0 - FogFactor( FogCoords( v.position, d.fogVec, d.fogParm.z ) );

		// the values of acff_t
		if ( acff == 1u ) {
			v_color.rgb *= f;
		} else if ( acff == 2u ) {
			v_color *= f;
		} else {
			v_color.a *= f;
		}
	}
}
