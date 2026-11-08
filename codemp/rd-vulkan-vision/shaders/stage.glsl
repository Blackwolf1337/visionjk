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

VARYING( 0 ) out vec2 v_texCoord0;
VARYING( 1 ) out vec2 v_texCoord1;
VARYING( 2 ) out vec4 v_color;
VARYING( 3 ) out vec3 v_position;		// in model space
VARYING( 4 ) out float v_depth;		// distance in front of the eye
VARYING( 6 ) NOPERSPECTIVE out vec2 v_screenCoord0;	// TCGEN_REST: v_texCoord0 as the screen counts it, see RestTexCoord()
VARYING( 7 ) out vec3 v_normal;		// in model space, zero where the geometry has none
#ifdef VK_GLES
out float v_clip;		// GLSL ES has no clip distances: generic.frag drops what is behind the plane
#define CLIP_DISTANCE	v_clip
#else
VARYING( 5 ) flat out uint v_draw;	// index into draws
#define CLIP_DISTANCE	gl_ClipDistance[0]

#define DRAW	uint( gl_InstanceIndex )
#endif

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
// all of them (RB_CalcDeformVertexes, RB_CalcBulgeVertexes, RB_CalcMoveVertexes of rd-vanilla). Autosprite and
// autosprite2 turn the quads of a surface of the level to the eye (AutospriteDeform, Autosprite2Deform), from what
// VK_PrepareAutosprites() of vk_world.cpp put into the vertices: the middle of the quad in the place of each corner
// and the corner and radius in lightmap2, or the middle of its short edge and the long axis and half the width in
// lightmap2 and lightmap3.
vec3 Deform( Draw d, inout StageInput v ) {
	uint kind = DEFORM( d.flags );

	if ( kind == 3u ) {
		// deformWave: the left of the view in model space, w: -1 in a mirror, times the scale of the axes.
		// deformVec: up, w: that scale.
		uint corner = uint( v.lightmap2.x );
		float r = v.lightmap2.y;
		vec3 left = d.deformWave.xyz * ( r * d.deformWave.w );
		vec3 up = d.deformVec.xyz * ( r * d.deformVec.w );

		v.normal = -cross( d.deformWave.xyz, d.deformVec.xyz );
		return v.position + ( ( corner == 0u || corner == 3u ) ? left : -left ) + ( corner < 2u ? up : -up );
	}
	if ( kind == 4u ) {
		// deformVec: forward of the view in model space
		vec3 minor = cross( vec3( v.lightmap2, v.lightmap3.x ), d.deformVec.xyz );

		minor = ( dot( minor, minor ) > 0.0 ) ? normalize( minor ) : vec3( 0.0 );
		return v.position + minor * v.lightmap3.y;
	}

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
	case TCGEN_FOG:			return FogCoords( v.position, DRAWS( DRAW ).fogVec, DRAWS( DRAW ).fogParm.z );
	default:				return vec2( 0.0 );
	}
}

// TCGEN_REST (RF_REFRACT_REST): where the vertex lies when nothing bends it, in the square of the screen the
// renderer copied around the origin of the entity (RB_CaptureForEntity): s to the right, t up, 1 its edge.
// vectors[0]: x, y half the window over the edge of the square in pixels, z the radius of the model's disc, w how
// many times as far from the vertex as it rests the picture is taken (a stronger bend).
vec2 RestTexCoord( vec4 vectors[2], mat4 mvp, StageInput v ) {
	vec3 at = vec3( 0.0, ( v.texCoord - 0.5 ) * 2.0 * vectors[0].z );
	vec4 rest = mvp * vec4( v.position + ( at - v.position ) * vectors[0].w, 1.0 );
	vec4 origin = mvp * vec4( 0.0, 0.0, 0.0, 1.0 );

	return vec2( 0.5 ) + ( rest.xy / rest.w - origin.xy / origin.w ) * vectors[0].xy;
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

// VisionJK: how much of a skinned vertex hangs on bones placed with no axes (skinned.vert): such bones belong to another
// part of a body a saber cut through, drawn by its own copy (cgame/cg_ragdoll.c); more than half of it is not drawn
float stageHidden = 0.0;
// and with the cut of such a part: how far in front of the cut the vertex lay at the moment of the cut, which keeps it
// on its side however the part bends (skinned.vert, Draw.cutBones)
bool stageHasCut = false;
float stageCut = 0.0;

void StageVertex( StageInput v ) {
	Draw d = DRAWS( DRAW );
	Transform t = TRANSFORMS( d.transform );

#ifndef VK_GLES
	v_draw = DRAW;
#endif
	v.position = Deform( d, v );
	gl_Position = t.mvp * vec4( v.position, 1.0 );
	v_position = v.position;
	v_normal = v.normal;
	// the view through a portal or mirror must not show what lies between the eye and the portal
	CLIP_DISTANCE = ( t.clipPlane.xyz == vec3( 0.0 ) ) ? 1.0
		: dot( ( t.model * vec4( v.position, 1.0 ) ).xyz, t.clipPlane.xyz ) - t.clipPlane.w;
	if ( stageHasCut ) {
		CLIP_DISTANCE = stageCut;
	}
	if ( stageHidden > 0.0 ) {
		CLIP_DISTANCE = min( CLIP_DISTANCE, 0.5 - stageHidden );
	}
	v_depth = gl_Position.w;
#ifdef VK_GLES
	// The targets of that device have the top row first, as the images of Vulkan have it, and OpenGL ES counts its
	// rows from the bottom: the picture is drawn upside down and turned again on its way to the window.
	gl_Position.y = -gl_Position.y;
#else
	// the depth range of OpenGL is -w to w, the one of Vulkan 0 to w
	gl_Position.z = ( gl_Position.z + gl_Position.w ) * 0.5;
#endif

	// A place on the screen is the same part of the way across a triangle on the screen, not in the level: the
	// fragment takes it from v_screenCoord0, which is not corrected for the depth.
	if ( TCGEN0( d.flags ) == TCGEN_REST ) {
		v_texCoord0 = ModTexCoord( RestTexCoord( d.tcGenVec0, t.mvp, v ), d.texMat0, vec4( 0.0 ), v );
	} else {
		v_texCoord0 = ModTexCoord( GenTexCoord( TCGEN0( d.flags ), d.tcGenVec0, t.viewOrigin.xyz, v ), d.texMat0, d.texTurb0, v );
	}
	v_screenCoord0 = v_texCoord0;
	v_texCoord1 = ModTexCoord( GenTexCoord( TCGEN1( d.flags ), d.tcGenVec1, t.viewOrigin.xyz, v ), d.texMat1, d.texTurb1, v );
	if ( RGBGEN( d.flags ) != 0u ) {
		// rgbGen lightingDiffuse: the light of the entity on the normal, times the color of the entity for
		// lightingDiffuseEntity (RB_CalcDiffuseColor and RB_CalcDiffuseEntityColor of rd-vanilla). A stage with a
		// material is lit per pixel instead (generic.frag).
		float incoming = max( dot( v.normal, t.lightDir.xyz ), 0.0 );
		bool perPixel = MATERIAL( d.flags ) || ( PBR( d.flags ) && ( d.pbrFlags & PBR_ENTITY ) != 0u );
		vec3 light = perPixel ? vec3( 1.0 ) : min( t.ambientLight.rgb + incoming * t.directedLight.rgb, vec3( 1.0 ) );
		// VisionJK: a model baked into the level (misc_model, q3map_nolightmap) keeps its rgbGen lightingDiffuse, but
		// the level has no light of an entity, so rd-vanilla draws it black (the cart and the air purifier of
		// mp/ffa3). The light q3map2 baked into its vertices stands in for it.
		if ( !perPixel && t.lightDir.w == 0.0 && t.ambientLight.rgb == vec3( 0.0 ) && t.directedLight.rgb == vec3( 0.0 ) ) {
			light = v.color.rgb;
		}

		v_color = vec4( light * d.baseColor.rgb, clamp( d.baseColor.a + d.vertColor.a * v.color.a, 0.0, 1.0 ) );
	} else {
		vec4 color = v.color;

		if ( d.styles != 0u ) {
			// ComputeFinalVertexColor() of rd-vanilla: the color of each light style of the vertex times the color of
			// that style. The ones after the first are in lightmap1 and lightmap2, three bytes in a float each
			// (VK_PackStyles in vk_world.cpp).
			float more[3] = float[3]( v.lightmap1.x, v.lightmap1.y, v.lightmap2.x );	// (not "packed": a reserved word of GLSL ES)
			vec3 sum = v.color.rgb * globals.styles[( d.styles & 255u ) - 1u].rgb;

			for ( int k = 1; k < 4; k++ ) {
				uint style = ( d.styles >> uint( 8 * k ) ) & 255u;

				if ( style == 0u ) {
					break;
				}
				uint p = uint( more[k - 1] );
				sum += vec3( uvec3( p, p >> 8, p >> 16 ) & 255u ) / 255.0 * globals.styles[style - 1u].rgb;
			}
			color.rgb = min( sum, vec3( 1.0 ) );
		}
		v_color = clamp( d.baseColor + d.vertColor * color, 0.0, 1.0 );
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
	// a saber mark that fades out over all the stage gives it, as RB_SurfaceGhoul() of rd-vanilla sets its colors
	if ( d.fadeMode != 0u ) {
		v_color.a = d.fade;
		if ( d.fadeMode == 2u ) {
			v_color.rgb = min( v_color.rgb, vec3( d.fade ) );
		}
	}
}
