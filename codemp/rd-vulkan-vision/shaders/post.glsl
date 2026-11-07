// what the fragment shaders of the passes over a whole target see: the target they read, four numbers of the
// pass, where in the target the pixel is

#include "dialect.glsl"

BINDING( 0, 0 ) uniform sampler2D u_image;

#ifdef VK_GLES
uniform vec4 u_parm;
#define PARM	u_parm
#else
layout( push_constant ) uniform Push {
	vec4	parm;
} push;
#define PARM	push.parm
#endif

VARYING( 0 ) in vec2 v_texCoord;
layout( location = 0 ) out vec4 o_color;
