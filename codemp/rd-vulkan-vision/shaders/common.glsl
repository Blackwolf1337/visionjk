// what every shader of rd-vulkan-vision that draws geometry sees. The structures are vkGlobals_t, vkTransform_t
// and vkDraw_t of vk_local.h, keep them the same.

#include "dialect.glsl"

#ifndef VK_GLES
#extension GL_EXT_nonuniform_qualifier : require
#endif

struct Transform {
	mat4	mvp;			// OpenGL clip space
	mat4	model;			// model to world
	vec4	viewOrigin;		// in model space
	vec4	lightDir;		// in model space. w: 1 for a model, 0 for the level and everything without light of its own
	vec4	ambientLight;
	vec4	directedLight;
	vec4	clipPlane;		// in world space, xyz normal, w distance. Zero: nothing is clipped
};

struct Draw {
	uint	transform;
	uint	tex0, tex1;
	uint	flags;
	uint	tex2, tex3, tex4;	// PBR: normal map, rmo map, light grid
	uint	pbrFlags;
	uint	tex5, tex6;			//	which probe a cell takes, the faces of the probes
	uint	probeSize;
	uint	probeCount;
	vec4	baseColor;		// color = baseColor + vertColor * vertex color
	vec4	vertColor;
	vec4	texMat0[2];		// s' = dot( row0.xy, st ) + row0.z, row0.w: phase of the turbulence
	vec4	texTurb0;		// s' += x * sinS + y * sinT, t' += z * sinS + w * sinT
	vec4	texMat1[2];
	vec4	texTurb1;
	vec4	tcGenVec0[2];
	vec4	tcGenVec1[2];
	vec4	parm;			// x: portal range, y: time of the shader. Dynamic light: origin in model space, w: radius
	vec4	fogColor;
	vec4	fogParm;		// exp2: x: density. linear: x: start, y: end. z: fog volume, where the eye is
	vec4	fogVec[2];		// fog volume: distance and depth vector
	vec4	deformWave;		// deformVertexes along the normal: x + y * wave( z + dot( deformVec.xyz, position ) + deformVec.w * s ), w: which wave
	vec4	deformVec;		// for a move, xyz: by how much
	uint	bones;			// skinned: where the skeleton starts in the bones of the frame, in rows
	float	fade;			// a saber mark that fades: its alpha
	uint	fadeMode;		//	0: it does not, 1: the alpha, 2: the color is no brighter than that either
	uint	styles;			// a vertex lit surface of several light styles: the styles + 1, a byte each, 0: not one
	vec4	material;		// MATERIAL: shine strength, exponent, clear coat, 1 with a normal map in tex1
	vec4	material2;		//	and light wrapped into the shadow, glints of crystals
	vec4	pbr;			// PBR: roughness, metalness, parallax depth
	vec4	grid0;			//	the light grid: origin, w its layers across
	vec4	grid1;			//	1 / the size of a cell, w the height of its picture
	vec4	grid2;			//	cells each way, w the width of its picture
	vec4	pbr2;			// PBR: where the surface lies in the height of the parallax (0 bottom to 1 top), clear coat,
							//	its roughness, anisotropy; glass: transmission, thickness, absorption distance
	vec4	pbr3;			// PBR: emission colour times strength, the threshold of the brightness that gives off
	vec4	tess;			// generic.tesc/.tese: units of the height, where the polygon lies in it, pixels an edge is
							//	cut down to, the most pieces an edge is cut into
};

BINDING( 0, 0 ) uniform Globals {
	vec4	gamma;			// x: 1 / r_gamma, y: 2 ^ overbright bits, z: identity light
	vec4	screen;
	vec4	screenFx[160];	// only screenfx.frag reads them (VK_SCREENFX_VEC4S)
	vec4	styles[64];		// the colors of the light styles over 256 (MAX_LIGHT_STYLES)
#ifndef VK_GLES
	vec4	wet[779];		// (wet.frag, paint.frag, the ray tracing shaders read these)
	vec4	paint[459];
	vec4	rt[319];
	vec4	lights[34];		// light to judge materials by (VK_LightRows of vk_frame.cpp, VK_LIGHT_VEC4S); not on GLES
	vec4	ssr[7];			// v_ssr (VK_SsrPrep of vk_post.cpp): 0-3 the view of the frame before, 4 its rect, 5 its eye
							//	and w the slot of its picture + 1 (0: none), 6 its forward and w the strength; not on GLES
#endif
} globals;

#ifdef VK_GLES
// GLSL ES has no buffers a shader could index, so a block holds one transform and one draw, and the device binds
// the place of its buffers where the ones of a draw call lie. std140 puts every member where std430 has it: they
// are all scalars of 4 bytes in fours, vec4 and mat4.
uniform Transforms {
	Transform	u_transform;
};

uniform Draws {
	Draw		u_draw;
};

#define TRANSFORMS( index )	u_transform
#define DRAWS( index )		u_draw
#else
layout( set = 0, binding = 1, std430 ) readonly buffer Transforms {
	Transform	transforms[];
};

layout( set = 0, binding = 2, std430 ) readonly buffer Draws {
	Draw		draws[];
};

// Which of the draws a draw call is comes as its first instance: gl_InstanceIndex in the vertex shaders (DRAW in
// stage.glsl), which hand it on to the fragment shader as v_draw. That costs no call of its own per draw.
#define TRANSFORMS( index )	transforms[index]
#define DRAWS( index )		draws[index]
#endif

// Draw.flags
#define ATEST( flags )		( ( flags ) & 7u )
#define TEXENV( flags )		( ( ( flags ) >> 3 ) & 7u )
#define TCGEN0( flags )		( ( ( flags ) >> 6 ) & 15u )
#define TCGEN1( flags )		( ( ( flags ) >> 10 ) & 15u )
#define RGBGEN( flags )		( ( ( flags ) >> 14 ) & 3u )
#define AGEN( flags )		( ( ( flags ) >> 16 ) & 7u )
#define DLIGHT( flags )		( ( ( flags ) & 0x80000u ) != 0u )
#define FOG( flags )		( ( ( flags ) >> 20 ) & 3u )
#define ACFF( flags )		( ( ( flags ) >> 22 ) & 3u )
#define DEFORM( flags )		( ( ( flags ) >> 24 ) & 7u )
#define MATERIAL( flags )	( ( ( flags ) & 0x08000000u ) != 0u )
#define PBR( flags )		( ( ( flags ) & 0x10000000u ) != 0u )

// Draw.pbrFlags (VKP_* of vk_local.h)
#define PBR_NORMAL		0x01u
#define PBR_RMO			0x02u
#define PBR_ORM			0x04u
#define PBR_HEIGHT		0x08u
#define PBR_ENTITY		0x10u
#define PBR_LIGHTMAP	0x20u
#define PBR_GRID		0x40u
#define PBR_PROBES		0x80u
#define PBR_RMO_RELATIVE	0x100u
#define PBR_GLASS		0x200u
#define PBR_SPECGLOSS	0x400u
#define PBR_SHEEN_ALBEDO	0x800u
#define PBR_ENERGY		0x1000u

// texCoordGen_t
#define TCGEN_IDENTITY		1u
#define TCGEN_LIGHTMAP		2u
#define TCGEN_LIGHTMAP1		3u
#define TCGEN_LIGHTMAP2		4u
#define TCGEN_LIGHTMAP3		5u
#define TCGEN_TEXTURE		6u
#define TCGEN_ENVIRONMENT	7u
#define TCGEN_FOG			8u
#define TCGEN_VECTOR		9u
#define TCGEN_REST			10u		// VK_TCGEN_REST of vk_local.h: RF_REFRACT_REST
