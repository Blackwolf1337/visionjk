// what every shader of rd-vulkan-vision sees. The structures are vkGlobals_t, vkTransform_t and vkDraw_t of
// vk_local.h, keep them the same.

#extension GL_EXT_nonuniform_qualifier : require

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

// Which of the draws a draw call is comes as its first instance: gl_InstanceIndex in the vertex shaders (DRAW in
// stage.glsl), which hand it on to the fragment shader as v_draw. That costs no call of its own per draw.

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
#define DEFORM( flags )		( ( ( flags ) >> 24 ) & 3u )

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
