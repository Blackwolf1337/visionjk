// what every shader of rd-vulkan-vision sees. The structures are vkGlobals_t, vkTransform_t and vkDraw_t of
// vk_local.h, keep them the same.

#extension GL_EXT_nonuniform_qualifier : require

struct Transform {
	mat4	mvp;			// OpenGL clip space
	mat4	model;			// model to world
	vec4	viewOrigin;		// in model space
	vec4	lightDir;		// in model space
	vec4	ambientLight;
	vec4	directedLight;
};

struct Draw {
	uint	transform;
	uint	tex0, tex1;
	uint	flags;
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
	vec4	fogParm;		// exp2: x: density. linear: x: start, y: end
	uint	bones;			// skinned: where the skeleton starts in the bones of the frame, in rows
	uint	unused0, unused1, unused2;
};

layout( set = 0, binding = 0 ) uniform Globals {
	vec4	gamma;			// x: 1 / r_gamma, y: 2 ^ overbright bits, z: identity light
	vec4	screen;
} globals;

layout( set = 0, binding = 1, std430 ) readonly buffer Transforms {
	Transform	transforms[];
};

layout( set = 0, binding = 2, std430 ) readonly buffer Draws {
	Draw		draws[];
};

layout( push_constant ) uniform Push {
	uint	draw;
} push;

// Draw.flags
#define ATEST( flags )		( ( flags ) & 7u )
#define TEXENV( flags )		( ( ( flags ) >> 3 ) & 7u )
#define TCGEN0( flags )		( ( ( flags ) >> 6 ) & 15u )
#define TCGEN1( flags )		( ( ( flags ) >> 10 ) & 15u )
#define RGBGEN( flags )		( ( ( flags ) >> 14 ) & 3u )
#define AGEN( flags )		( ( ( flags ) >> 16 ) & 7u )
#define DLIGHT( flags )		( ( ( flags ) & 0x80000u ) != 0u )
#define FOG( flags )		( ( ( flags ) >> 20 ) & 3u )

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
