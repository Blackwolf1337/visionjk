#version 460

// The wet places of RE_AddWet (wetSpot_t, VK_DrawWet() in vk_post.cpp): shaders/wet.frag finds the level under every
// pixel from the depth. The numbers are the wet ones of the globals of the frame; the number of the instance says
// which place, the number of the vertex which corner, there is no vertex buffer.
//
// A place is drawn as the box its pixels can lie in (InPlace of wet.frag): as wide as its ball along its plane, as
// thick as the surface may lie off it. Only the faces the eye looks at from inside are drawn (the others collapse to
// nothing), so each pixel of the box comes once, also with the eye in it. A floor seen at a flat angle covers a
// third of the screen square around its ball, or less. The fragment shader gets the places whose balls touch this one
// as bits, it looks at no other.
//
//   0-3  the view and projection of the level (OpenGL clip space)
//   4    the eye, w: tan of half the field of view across
//   5    forward, w: the same up
//   6    left, w and 7 w: the depth of a distance d in front of the eye is 6.w / d - 7.w
//   7    up
//   8    the view on the target in pixels: left, top, width, height
//   9    1 / the size of the copy of the frame, the time in seconds, how many places
//   10   the height of the floor of the puddle view, 1 if it was drawn this frame (VK_BeginPuddle)
//   11+  six rows a place: origin, radius / normal, wet / hit, puddle / ambient, flow / directed, seed / light
//        dir, kind (WET_PLACE, ... of tr_types.h)

#include "dialect.glsl"

// VK_WET_VEC4S of vk_local.h
#ifdef VK_GLES
uniform vec4 u_wet[779];
#define W			u_wet
#define INSTANCE	uint( gl_InstanceID )
#else
BINDING( 0, 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
} globals;
#define W			globals.wet
#define INSTANCE	uint( gl_InstanceIndex )
#endif

VARYING( 0 ) flat out uint v_spot;
VARYING( 1 ) flat out uvec4 v_near;		// the places whose balls touch this one, this one too, a bit each

// the ball of a place, which holds its pixels (InPlace of wet.frag), and how far off its plane they may lie
float Reach( uint row ) {
	return W[row].w * 2.0 + 8.0;
}

// the six faces of the box as four corners each (bit 0: +x, 1: +y, 2: +z), the normal axis and side of each
const int FACES[24] = int[24]( 1, 3, 5, 7,  0, 4, 2, 6,  2, 6, 3, 7,  0, 1, 4, 5,  4, 5, 6, 7,  0, 2, 1, 3 );
const int QUAD[6] = int[6]( 0, 1, 2, 2, 1, 3 );

void main() {
	uint spot = INSTANCE;
	uint row = 11u + spot * 6u;
	uint count = uint( W[9].w );
	vec3 c = W[row].xyz;
	float reach = Reach( row );

	v_spot = spot;
	v_near = uvec4( 0u );
	for ( uint j = 0u; j < count; j++ ) {
		uint rj = 11u + j * 6u;
		if ( distance( W[rj].xyz, c ) < Reach( rj ) + reach ) {
			v_near[j >> 5u] |= 1u << ( j & 31u );
		}
	}

	// the axes of the box: two along the plane, the normal; a little larger than the ball and the slab, so no pixel
	// of a place lies on its edge
	vec3 n = W[row + 1u].xyz;
	vec3 t = normalize( abs( n.z ) < 0.9 ? cross( n, vec3( 0.0, 0.0, 1.0 ) ) : cross( n, vec3( 1.0, 0.0, 0.0 ) ) );
	vec3 b = cross( n, t );
	vec3 size = vec3( reach + 2.0, reach + 2.0, 4.0 + max( W[row].w, 1.0 ) * 0.12 );
	vec3 axes[3] = vec3[3]( t, b, n );

	int face = int( VERTEX_INDEX ) / 6;
	int corner = FACES[face * 4 + QUAD[int( VERTEX_INDEX ) % 6]];
	vec3 out_ = axes[face >> 1] * ( ( face & 1 ) == 0 ? 1.0 : -1.0 );
	vec3 eye = W[4].xyz;

	// a face the eye sees from outside is left out: the faces behind give the same pixels
	if ( dot( out_, eye - ( c + out_ * size[face >> 1] ) ) >= 0.0 ) {
		gl_Position = vec4( 0.0, 0.0, 0.0, 1.0 );
		return;
	}
	vec3 p = c + t * ( ( corner & 1 ) != 0 ? size.x : -size.x ) + b * ( ( corner & 2 ) != 0 ? size.y : -size.y )
		+ n * ( ( corner & 4 ) != 0 ? size.z : -size.z );
	vec4 position = mat4( W[0], W[1], W[2], W[3] ) * vec4( p, 1.0 );
#ifdef VK_GLES
	position.y = -position.y;
#else
	position.z = ( position.z + position.w ) * 0.5;
#endif
	// (no depth is tested or written: a far corner is kept on the far plane instead of cut off)
	position.z = min( position.z, position.w );
	gl_Position = position;
}
