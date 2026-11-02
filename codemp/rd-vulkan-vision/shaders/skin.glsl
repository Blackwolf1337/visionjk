// The skinning of a Ghoul2 vertex by its bones, for shaders/skinned.vert and rtskin.comp: three rows a bone in the
// bone buffer of the frame, x' = dot( row.xyz, x ) + row.w, up to four bones with weights that add up to 1. Before
// it is included BONE_ROW( r ) names row r of that buffer.

vec3 SkinPosition( uint base, uvec4 boneNums, vec4 weights, vec3 position ) {
	vec3 result = vec3( 0.0 );

	for ( int i = 0; i < 4; i++ ) {
		uint row = base + boneNums[i] * 3u;

		if ( weights[i] > 0.0 ) {
			result += weights[i] * vec3( dot( BONE_ROW( row ).xyz, position ) + BONE_ROW( row ).w,
				dot( BONE_ROW( row + 1u ).xyz, position ) + BONE_ROW( row + 1u ).w,
				dot( BONE_ROW( row + 2u ).xyz, position ) + BONE_ROW( row + 2u ).w );
		}
	}
	return result;
}
