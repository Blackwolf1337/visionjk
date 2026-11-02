// The quad over the part of the view where a ball of the level is, for the passes that lay something onto the
// level (wet.vert, paint.vert): the corner is the number of the vertex, there is no vertex buffer. A ball the eye
// is near or in gets the whole view. W names the numbers of the pass, the view first as VK_DrawWet() puts it.

const vec2 CORNERS[6] = vec2[6]( vec2( -1.0, -1.0 ), vec2( 1.0, -1.0 ), vec2( -1.0, 1.0 ), vec2( -1.0, 1.0 ), vec2( 1.0, -1.0 ),
	vec2( 1.0, 1.0 ) );

vec4 BallQuad( vec3 c, float radius ) {
	vec2 corner = CORNERS[int( VERTEX_INDEX ) % 6];
	vec3 eye = W[4].xyz;
	vec3 forward = W[5].xyz;
	vec3 left = W[6].xyz;
	vec3 up = W[7].xyz;
	float z = dot( c - eye, forward );
	vec4 position;

	if ( z < radius + 6.0 ) {
		return vec4( corner, 0.5, 1.0 );
	}
	// the ball on the plane one unit in front of the eye, as in liquid.vert, the quad on its near side
	vec2 p = vec2( dot( c - eye, left ), dot( c - eye, up ) ) / z;
	float s = radius / sqrt( max( z * z - radius * radius, 1.0 ) ) * 1.25;
	p += corner * s;
	float near = z - radius;

	position = mat4( W[0], W[1], W[2], W[3] ) * vec4( eye + ( forward + left * p.x + up * p.y ) * near, 1.0 );
#ifdef VK_GLES
	position.y = -position.y;
#else
	position.z = ( position.z + position.w ) * 0.5;
#endif
	return position;
}
