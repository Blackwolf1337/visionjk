// The level under the pixels of the view, from the depth of the scene: for the passes that lay something onto the
// level after it is drawn (wet.frag, paint.frag). Before it is included W names the numbers of the pass, whose
// first eleven rows are the view as VK_DrawWet() puts it (wet.vert), and u_depth the depth of the scene.

// how far in front of the eye the scene is at a pixel of the target
float SceneDistance( vec2 pixel ) {
	ivec2 size = textureSize( u_depth, 0 );
	float depth = texelFetch( u_depth, clamp( ivec2( pixel ), ivec2( 0 ), size - 1 ), 0 ).r;

	// (both are below zero: x / ( depth - 1 ) and a bit)
	return W[6].w / min( depth + W[7].w, -1e-7 );
}

// the point of the level at the middle of a pixel of the target
vec3 ScenePoint( vec2 pixel ) {
	vec4 rect = W[8];
	vec2 ndc = vec2( ( pixel.x - rect.x ) / rect.z * 2.0 - 1.0, 1.0 - ( pixel.y - rect.y ) / rect.w * 2.0 );
	vec3 ray = W[5].xyz - W[6].xyz * ( ndc.x * W[4].w ) + W[7].xyz * ( ndc.y * W[5].w );

	return W[4].xyz + ray * SceneDistance( pixel );
}

// a place of the level as a pixel of the target, z: how far in front of the eye, -1 behind it
vec3 OnTarget( vec3 p ) {
	vec4 clip = mat4( W[0], W[1], W[2], W[3] ) * vec4( p, 1.0 );
	vec4 rect = W[8];

	if ( clip.w < 1.0 ) {
		return vec3( 0.0, 0.0, -1.0 );
	}
	vec2 ndc = clip.xy / clip.w;
	return vec3( rect.x + ( ndc.x * 0.5 + 0.5 ) * rect.z, rect.y + ( 0.5 - ndc.y * 0.5 ) * rect.w, dot( p - W[4].xyz, W[5].xyz ) );
}

// The way the surface faces at the point p of a pixel, towards the eye: from the neighbours on the side where the
// depth goes on smoothly. (The slopes of the block of pixels mix the floor with what stands on it, a player, and a
// ring of pixels around him would not be on the floor.) fallback where the neighbours give no plane.
vec3 SceneSurface( vec2 pixel, vec3 p, vec3 fallback ) {
	vec3 pl = ScenePoint( pixel - vec2( 1.0, 0.0 ) ), pr = ScenePoint( pixel + vec2( 1.0, 0.0 ) );
	vec3 pd = ScenePoint( pixel - vec2( 0.0, 1.0 ) ), pu = ScenePoint( pixel + vec2( 0.0, 1.0 ) );
	vec3 dx = ( abs( dot( pr - p, W[5].xyz ) ) < abs( dot( p - pl, W[5].xyz ) ) ) ? pr - p : p - pl;
	vec3 dy = ( abs( dot( pu - p, W[5].xyz ) ) < abs( dot( p - pd, W[5].xyz ) ) ) ? pu - p : p - pd;
	vec3 surface = cross( dx, dy );

	surface = ( dot( surface, surface ) > 1e-10 ) ? normalize( surface ) : fallback;
	return ( dot( surface, W[4].xyz - p ) < 0.0 ) ? -surface : surface;
}
