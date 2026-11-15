#version 460

// v_volumetric (vk_post.cpp VK_Volumetric): haze in the air of the level, lit by the light that reaches each place. Over
// the solid level of the view, before what is transparent: along the ray of each pixel to the level (its distance in
// tex5, vk.softDepth) or to the farthest the haze goes, in steps from a place that changes from pixel to pixel; at
// each step the light of the cell of the light grid there (tex4, tr.volumeGrid: where the sun or a lamp shines in the
// air is bright, the shade is dark, so light falls in soft bands through windows and doors) and the moving lights
// near it, scattered toward the eye by the haze there, which thins with height. Out: the light gathered and what of
// the level behind still shows, blended as frame * alpha + colour.
//   texMat0: forward and the tangent of half the width of the view, left and of half its height
//   texMat1: up and the density a unit, how fast it thins a unit up, the height it is thickest at, how far it goes,
//            the steps
//   fogColor: the colour of the haze (the light is times it), parm: the view in pixels from the upper left
//   grid0-2: as PbrGrid reads them; decals / numDecals: the moving lights, two rows each in the bones (origin and
//   reach, colour)

#include "common.glsl"

layout( location = 0 ) flat in uint v_draw;
layout( location = 0 ) out vec4 o_color;

BINDING( 1, 0 ) uniform sampler2D u_textures[];
layout( set = 0, binding = 3, std430 ) readonly buffer Bones {
	vec4	bones[];
};

vec3 CellLight( Draw d, vec3 world ) {
	vec3 cells = d.grid2.xyz;
	vec3 g = clamp( ( world - d.grid0.xyz ) * d.grid1.xyz, vec3( 0.0 ), cells - 1.0 );
	float z0 = floor( g.z ), z1 = min( z0 + 1.0, cells.z - 1.0 );
	vec2 size = vec2( d.grid2.w, d.grid1.w );
	vec2 a = ( vec2( mod( z0, d.grid0.w ), floor( z0 / d.grid0.w ) ) * cells.xy + g.xy + 0.5 ) / size;
	vec2 b = ( vec2( mod( z1, d.grid0.w ), floor( z1 / d.grid0.w ) ) * cells.xy + g.xy + 0.5 ) / size;
	return mix( textureLod( u_textures[nonuniformEXT( d.tex4 )], a, 0.0 ).rgb, textureLod( u_textures[nonuniformEXT( d.tex4 )], b, 0.0 ).rgb, g.z - z0 );
}

void main() {
	Draw d = DRAWS( v_draw );
	Transform t = TRANSFORMS( d.transform );
	vec4 rect = d.parm;
	vec2 pixel = gl_FragCoord.xy;
	vec2 ndc = vec2( ( pixel.x - rect.x ) / rect.z * 2.0 - 1.0, 1.0 - ( pixel.y - rect.y ) / rect.w * 2.0 );
	// the ray with a length of 1 along forward: a distance in front of the eye is a length along it
	vec3 ray = d.texMat0[0].xyz - d.texMat0[1].xyz * ( ndc.x * d.texMat0[0].w ) + d.texMat1[0].xyz * ( ndc.y * d.texMat0[1].w );
	float scene = texelFetch( u_textures[nonuniformEXT( d.tex5 )], ivec2( pixel ), 0 ).r;
	float far = min( scene, d.texMat1[1].z );
	int steps = int( d.texMat1[1].w );
	float step = far / float( steps );
	float stepLength = step * length( ray );
	// the start of the steps moves from pixel to pixel (interleaved gradient noise), so the steps show as a fine
	// grain instead of bands
	float jitter = fract( 52.9829189 * fract( dot( pixel, vec2( 0.06711056, 0.00583715 ) ) ) );
	vec3 eye = t.viewOrigin.xyz;
	vec3 gathered = vec3( 0.0 );
	float through = 1.0;

	for ( int i = 0; i < steps; i++ ) {
		vec3 p = eye + ray * ( ( float( i ) + jitter ) * step );
		float density = d.texMat1[0].w * exp( -max( p.z - d.texMat1[1].y, 0.0 ) * d.texMat1[1].x );
		vec3 light = CellLight( d, p ) * 2.0;

		for ( uint l = 0u; l < d.numDecals; l++ ) {
			vec4 o = bones[d.decals + l * 2u], c = bones[d.decals + l * 2u + 1u];
			float f = max( 0.0, 1.0 - distance( p, o.xyz ) / o.w );

			light += c.rgb * f * f * 2.0;
		}
		float slice = density * stepLength;
		gathered += through * slice * light;
		through *= exp( -slice );
	}
	o_color = vec4( gathered * d.fogColor.rgb, through );
}
