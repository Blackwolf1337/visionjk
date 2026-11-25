// Geometry for the shaders of the level (generic.frag, lightmask.glsl).

// the point of the segment from a to b nearest p
vec3 NearestOnSegment( vec3 a, vec3 b, vec3 p ) {
	vec3 ab = b - a;
	return a + ab * clamp( dot( p - a, ab ) / max( dot( ab, ab ), 1e-4 ), 0.0, 1.0 );
}

// the point of the rectangle round c with the half edges r and u (at right angles) nearest to p
vec3 NearestOnRect( vec3 c, vec3 r, vec3 u, vec3 p ) {
	vec3 d = p - c;
	return c + r * clamp( dot( d, r ) / max( dot( r, r ), 1e-4 ), -1.0, 1.0 ) + u * clamp( dot( d, u ) / max( dot( u, u ), 1e-4 ), -1.0, 1.0 );
}
