/*
bg_props_math.h: the vector functions of q_math.c as inline functions, for the two files of the prop physics

q_math.c has them as functions that are called. The physics of the props calls them some ten million times a
second when many props move, and the call is most of what each one costs. Here they are the same statements
under the same names, so what they compute is the same to the last bit. Include it after q_shared.h, in
g_props.c and bg_props.c only.
*/

#pragma once

static QINLINE void PropVec_Add( const vec3_t vec1, const vec3_t vec2, vec3_t vecOut ) {
	vecOut[0] = vec1[0]+vec2[0];
	vecOut[1] = vec1[1]+vec2[1];
	vecOut[2] = vec1[2]+vec2[2];
}

static QINLINE void PropVec_Subtract( const vec3_t vec1, const vec3_t vec2, vec3_t vecOut ) {
	vecOut[0] = vec1[0]-vec2[0];
	vecOut[1] = vec1[1]-vec2[1];
	vecOut[2] = vec1[2]-vec2[2];
}

static QINLINE void PropVec_Scale( const vec3_t vecIn, float scale, vec3_t vecOut ) {
	vecOut[0] = vecIn[0]*scale;
	vecOut[1] = vecIn[1]*scale;
	vecOut[2] = vecIn[2]*scale;
}

static QINLINE void PropVec_MA( const vec3_t vec1, float scale, const vec3_t vec2, vec3_t vecOut ) {
	vecOut[0] = vec1[0] + scale*vec2[0];
	vecOut[1] = vec1[1] + scale*vec2[1];
	vecOut[2] = vec1[2] + scale*vec2[2];
}

static QINLINE void PropVec_Copy( const vec3_t vecIn, vec3_t vecOut ) {
	vecOut[0] = vecIn[0];
	vecOut[1] = vecIn[1];
	vecOut[2] = vecIn[2];
}

static QINLINE void PropVec_Clear( vec3_t vec ) {
	vec[0] = vec[1] = vec[2] = 0.0f;
}

static QINLINE void PropVec_Set( vec3_t vec, float x, float y, float z ) {
	vec[0]=x; vec[1]=y; vec[2]=z;
}

static QINLINE float PropVec_Length( const vec3_t vec ) {
	return (float)sqrt( vec[0]*vec[0] + vec[1]*vec[1] + vec[2]*vec[2] );
}

static QINLINE float PropVec_LengthSquared( const vec3_t vec ) {
	return (vec[0]*vec[0] + vec[1]*vec[1] + vec[2]*vec[2]);
}

static QINLINE float PropVec_Normalize( vec3_t vec ) {
	float	length, ilength;

	length = vec[0]*vec[0] + vec[1]*vec[1] + vec[2]*vec[2];
	length = sqrtf( length );

	if ( length ) {
		ilength = 1/length;
		vec[0] *= ilength;
		vec[1] *= ilength;
		vec[2] *= ilength;
	}

	return length;
}

static QINLINE float PropVec_Dot( const vec3_t vec1, const vec3_t vec2 ) {
	return vec1[0]*vec2[0] + vec1[1]*vec2[1] + vec1[2]*vec2[2];
}

static QINLINE void PropVec_Cross( const vec3_t vec1, const vec3_t vec2, vec3_t vecOut ) {
	vecOut[0] = vec1[1]*vec2[2] - vec1[2]*vec2[1];
	vecOut[1] = vec1[2]*vec2[0] - vec1[0]*vec2[2];
	vecOut[2] = vec1[0]*vec2[1] - vec1[1]*vec2[0];
}

static QINLINE float PropVec_Distance( const vec3_t p1, const vec3_t p2 ) {
	vec3_t	v;

	PropVec_Subtract( p2, p1, v );
	return PropVec_Length( v );
}

static QINLINE float PropVec_DistanceSquared( const vec3_t p1, const vec3_t p2 ) {
	vec3_t	v;

	PropVec_Subtract( p2, p1, v );
	return v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
}

#define VectorAdd			PropVec_Add
#define VectorSubtract		PropVec_Subtract
#define VectorScale			PropVec_Scale
#define VectorMA			PropVec_MA
#define VectorCopy			PropVec_Copy
#define VectorClear			PropVec_Clear
#define VectorSet			PropVec_Set
#define VectorLength		PropVec_Length
#define VectorLengthSquared	PropVec_LengthSquared
#define VectorNormalize		PropVec_Normalize
#define DotProduct			PropVec_Dot
#define CrossProduct		PropVec_Cross
#define Distance			PropVec_Distance
#define DistanceSquared		PropVec_DistanceSquared
