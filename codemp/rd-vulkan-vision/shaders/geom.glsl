// Geometry for the shaders of the level (generic.frag, lightmask.glsl).

// the point of the segment from a to b nearest p
vec3 NearestOnSegment( vec3 a, vec3 b, vec3 p ) {
	vec3 ab = b - a;
	return a + ab * clamp( dot( p - a, ab ) / max( dot( ab, ab ), 1e-4 ), 0.0, 1.0 );
}
