#version 460

// The pieces of water of RE_AddLiquid (liquidSegment_t), a quad each that covers the round body from a to b on the
// screen: shaders/liquid.frag casts the ray of every pixel against that body and draws what it hits. A batch is
// four rows of the view in the bone buffer and six rows a piece (VK_DrawLiquids() in vk_post.cpp), with no vertex
// buffer: the number of a vertex says which piece and which corner it is, as in sprite.vert.
//
//   view:   forward, left, up, the eye
//   piece:  a (w: radius at a), b (w: radius at b), flow at a, flow at b, breakup, the light of the level there:
//           its direction, the ambient and the directed colour at a (w of the direction: the time in seconds, w of the
//           colours: what part of them there is at b)

#include "common.glsl"

#ifdef VK_GLES
uniform Bones {
	vec4	bones[768];
};
#define PIECES		0u
#define VERTEX		uint( gl_VertexID )
#else
layout( set = 0, binding = 3, std430 ) readonly buffer Bones {
	vec4	bones[];
};
#define DRAW		uint( gl_InstanceIndex )
#define PIECES		DRAWS( DRAW ).bones
#define VERTEX		uint( gl_VertexIndex )
#endif

VARYING( 0 ) out vec3 v_position;			// on the quad, in the level
VARYING( 1 ) flat out vec4 v_a;				// the body: a, radius
VARYING( 2 ) flat out vec4 v_b;				//	b, flow at a
VARYING( 3 ) flat out vec4 v_flow;			// flow at b, breakup, the time
VARYING( 4 ) flat out vec4 v_lightDir;		// towards the light of the level
VARYING( 5 ) flat out vec4 v_ambient;		// at a, w: the part of it at b
VARYING( 6 ) flat out vec4 v_directed;
VARYING( 7 ) flat out vec3 v_left;			// of the view, for the bend on the screen
VARYING( 8 ) flat out vec3 v_up;
#ifndef VK_GLES
VARYING( 9 ) flat out uint v_draw;
#endif

const float NEAR = 4.5;		// a hair beyond r_znear

void main() {
	uint base = PIECES;
	uint corner = VERTEX & 3u;
	uint row = base + 4u + ( VERTEX >> 2 ) * 6u;
	vec3 forward = bones[base].xyz;
	vec3 left = bones[base + 1u].xyz;
	vec3 up = bones[base + 2u].xyz;
	vec3 eye = bones[base + 3u].xyz;
	vec4 a = bones[row];
	vec4 b = bones[row + 1u];
	vec4 flow = bones[row + 2u];
	vec4 lightDir = bones[row + 3u];
	float radius = max( a.w, b.w );
	float za = dot( a.xyz - eye, forward );
	float zb = dot( b.xyz - eye, forward );

#ifndef VK_GLES
	v_draw = DRAW;
#endif
	// A body that reaches behind the eye is cut where it is near: what is left lies in front of the near plane
	if ( za < NEAR + radius && zb < NEAR + radius ) {
		gl_Position = vec4( 2.0, 2.0, 2.0, 1.0 );
		return;
	}
	if ( za < NEAR + radius ) {
		a.xyz = mix( a.xyz, b.xyz, ( NEAR + radius - za ) / ( zb - za ) );
		za = NEAR + radius;
	} else if ( zb < NEAR + radius ) {
		b.xyz = mix( b.xyz, a.xyz, ( NEAR + radius - zb ) / ( za - zb ) );
		zb = NEAR + radius;
	}

	// The ends on the plane one unit in front of the eye, and how far a ball of that radius reaches there: the
	// silhouette of a ball at distance z is r / sqrt( z * z - r * r ) wide, a quarter more for one that is off the
	// middle of the view
	vec2 pa = vec2( dot( a.xyz - eye, left ), dot( a.xyz - eye, up ) ) / za;
	vec2 pb = vec2( dot( b.xyz - eye, left ), dot( b.xyz - eye, up ) ) / zb;
	float sa = a.w / sqrt( max( za * za - a.w * a.w, 1.0 ) );
	float sb = b.w / sqrt( max( zb * zb - b.w * b.w, 1.0 ) );
	float s = max( sa, sb ) * 1.25 + 0.002;
	vec2 along = pb - pa;
	along = ( dot( along, along ) > 1e-10 ) ? normalize( along ) : vec2( 1.0, 0.0 );
	vec2 across = vec2( -along.y, along.x );
	bool atB = ( corner == 1u || corner == 2u );
	vec2 p = ( atB ? pb + along * s : pa - along * s ) + ( ( corner < 2u ) ? across : -across ) * s;

	// all four corners at the depth of the nearest point of the body, where they cover it whatever is behind
	float z = max( min( za, zb ) - radius, NEAR );
	vec3 position = eye + forward * z + left * ( p.x * z ) + up * ( p.y * z );
	Transform t = TRANSFORMS( DRAWS( DRAW ).transform );

	v_position = position;
	v_a = a;
	v_b = vec4( b.xyz, flow.x );
	v_flow = vec4( flow.y, flow.z, lightDir.w, b.w );
	v_lightDir = vec4( lightDir.xyz, 0.0 );
	v_ambient = bones[row + 4u];
	v_directed = bones[row + 5u];
	v_left = left;
	v_up = up;
	gl_Position = t.mvp * vec4( position, 1.0 );
#ifdef VK_GLES
	gl_Position.y = -gl_Position.y;
#else
	gl_Position.z = ( gl_Position.z + gl_Position.w ) * 0.5;
#endif
}
