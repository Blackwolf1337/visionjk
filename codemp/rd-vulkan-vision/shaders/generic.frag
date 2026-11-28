#version 460

#include "common.glsl"
#include "noise.glsl"

#ifdef VK_GLES
// the textures of the draw, the device binds them for every draw call (the last three only for a material)
uniform sampler2D u_tex0;
uniform sampler2D u_tex1;
uniform sampler2D u_tex2;
uniform sampler2D u_tex3;
uniform sampler2D u_tex4;
uniform sampler2D u_tex5;
uniform sampler2D u_tex6;
#define TEX0( draw_ )	u_tex0
#define TEX1( draw_ )	u_tex1
#define TEX2( draw_ )	u_tex2
#define TEX3( draw_ )	u_tex3
#define TEX4( draw_ )	u_tex4
#define TEX5( draw_ )	u_tex5
#define TEX6( draw_ )	u_tex6
#else
BINDING( 1, 0 ) uniform sampler2D u_textures[];
#define TEX0( draw_ )	u_textures[nonuniformEXT( draw_.tex0 )]
#define TEX1( draw_ )	u_textures[nonuniformEXT( draw_.tex1 )]
#define TEX2( draw_ )	u_textures[nonuniformEXT( draw_.tex2 )]
#define TEX3( draw_ )	u_textures[nonuniformEXT( draw_.tex3 )]
#define TEX4( draw_ )	u_textures[nonuniformEXT( draw_.tex4 )]
#define TEX5( draw_ )	u_textures[nonuniformEXT( draw_.tex5 )]
#define TEX6( draw_ )	u_textures[nonuniformEXT( draw_.tex6 )]
#endif

VARYING( 0 ) in vec2 v_texCoord0;
VARYING( 1 ) in vec2 v_texCoord1;
VARYING( 2 ) in vec4 v_color;
VARYING( 3 ) in vec3 v_position;
VARYING( 4 ) in float v_depth;
VARYING( 6 ) NOPERSPECTIVE in vec2 v_screenCoord0;
VARYING( 7 ) in vec3 v_normal;
#ifdef VK_GLES
in float v_clip;
#else
VARYING( 5 ) flat in uint v_draw;
#endif

layout( location = 0 ) out vec4 o_color;

// The tangent frame of a pixel from how its position and its texture coordinates change to the next pixel (Schüler,
// "Followup: Normal mapping without precomputed tangents", 2013): the vertices carry no tangents
mat3 CotangentFrame( vec3 n, vec3 p, vec2 uv ) {
	vec3 dp1 = dFdx( p ), dp2 = dFdy( p );
	vec2 duv1 = dFdx( uv ), duv2 = dFdy( uv );
	vec3 dp2perp = cross( dp2, n ), dp1perp = cross( n, dp1 );
	vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
	vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
	float invmax = inversesqrt( max( max( dot( t, t ), dot( b, b ) ), 1e-20 ) );

	return mat3( t * invmax, b * invmax, n );
}

// Crystals that glint, as in snow (Shopf 2012, Wang and Bowles 2016): a point on a grid in the space of the model in
// each cell, at a place of its own, with a facet turned at random; where the surface passes one and its facet throws
// the light at the eye, it flashes. Fainter where a cell is smaller than a pixel, so far off they do not shimmer.
float Sparkle( vec3 p, vec3 n, vec3 l, vec3 toEye ) {
	const float DENSITY = 4.0;		// cells a unit
	vec3 q = p * DENSITY;
	vec3 cell = floor( q );
	uvec3 h = Pcg3( uvec3( ivec3( cell ) + 4096 ) );
	vec3 r = vec3( h & 0xffffu ) / 65535.0;
	float near = 1.0 - smoothstep( 0.08, 0.17, length( q - ( cell + 0.2 + 0.6 * r ) ) );
	vec3 facet = normalize( n + ( vec3( Pcg3( h ) & 0xffffu ) / 65535.0 - 0.5 ) * 1.6 );
	float mirror = max( dot( reflect( -l, facet ), toEye ), 0.0 );

	return near * pow( mirror, 120.0 ) * ( 1.0 - smoothstep( 0.3, 0.9, length( fwidth( q ) ) ) );
}

// The normal of the surface without its bumps, on the side that is looked at: the one of the vertices. A patch folded
// round hard corners (a square pillar, a wall with its end faces in one patch) has them smoothed across its flat sides,
// by up to 45 degrees each way, and comes with short normals (vk_world.cpp, srfGridMesh_t::hardEdges). Taken as they
// are, a flat side is lit as if it were bent, and where they lean further from the eye than the side is turned it was
// taken for its back and lit from the wrong side: a band round the far end of the tunnel on mp/ffa3 that stood out as an
// arch in the view of the normals (their pictures 2026-11-25). Such a patch takes the plane of the triangle; where the
// smoothed normal is close to it, on its curved parts, that one, so they do not show their triangles.
vec3 FlatNormal( vec3 toEye ) {
	if ( dot( v_normal, v_normal ) > 0.25 ) {
		vec3 n = normalize( v_normal );
		return dot( n, toEye ) < 0.0 ? -n : n;
	}
	vec3 plane = cross( dFdx( v_position ), dFdy( v_position ) );
	float area = dot( plane, plane );
	vec3 n = normalize( v_normal );
	if ( area < 1e-20 ) {
		return dot( n, toEye ) < 0.0 ? -n : n;
	}
	plane *= inversesqrt( area );
	if ( dot( plane, toEye ) < 0.0 ) {
		plane = -plane;
	}
	if ( dot( n, plane ) < 0.0 ) {
		n = -n;
	}
	// (all of the smoothed one within 8 degrees of the plane, none of it from 25 on)
	return normalize( mix( plane, n, smoothstep( 0.906, 0.99, dot( n, plane ) ) ) );
}

// A stage of a model with a material (shader keywords normalMap, specular, wrapLight and sparkle): lit per pixel by
// the light of its entity as the vertices are lit otherwise (ambient, directed and its direction in model space),
// with a broad shine of the surface, a sharp one of a clear coat over it that is smooth over the bumps, and the
// light of the room the coat throws back at a flat angle (Fresnel with F0 0.04). Light that wraps into the shadow,
// as it scatters through snow (GPU Gems 1, chapter 16: N.L + wrap over 1 + wrap), with the cool of the sky where it
// does (as the snow of Batman: Arkham Origins), and the glints of its crystals.
// A lightmap filtered with a cubic B-spline from four bilinear taps (Sigg and Hadwiger, GPU Gems 2 ch. 20): a lightmap
// of the stock levels is 128 pixels for a whole room, bilinear filtering shows its pixels as steps along every shadow
// edge, the cubic one makes them round and soft.
vec4 LightmapTexture( sampler2D tex, vec2 uv ) {
	vec2 size = vec2( textureSize( tex, 0 ) );
	vec2 p = uv * size - 0.5;
	vec2 f = fract( p );
	vec2 f2 = f * f, f3 = f2 * f;
	vec2 w0 = ( -f3 + 3.0 * f2 - 3.0 * f + 1.0 ) / 6.0;
	vec2 w1 = ( 3.0 * f3 - 6.0 * f2 + 4.0 ) / 6.0;
	vec2 w2 = ( -3.0 * f3 + 3.0 * f2 + 3.0 * f + 1.0 ) / 6.0;
	vec2 w3 = f3 / 6.0;
	vec2 g0 = w0 + w1, g1 = w2 + w3;
	vec2 base = p - f + 0.5;
	vec2 h0 = ( base - 1.0 + w1 / g0 ) / size;
	vec2 h1 = ( base + 1.0 + w3 / g1 ) / size;
	return g0.y * ( g0.x * texture( tex, h0 ) + g1.x * texture( tex, vec2( h1.x, h0.y ) ) )
		+ g1.y * ( g0.x * texture( tex, vec2( h0.x, h1.y ) ) + g1.x * texture( tex, h1 ) );
}

bool IsLightmapGen( uint tcGen ) {
	return tcGen >= TCGEN_LIGHTMAP && tcGen <= TCGEN_LIGHTMAP3;
}

vec3 MaterialLight( Draw d, vec3 albedo ) {
	Transform t = TRANSFORMS( d.transform );
	vec3 toEye = normalize( t.viewOrigin.xyz - v_position );
	vec3 smooth_ = normalize( v_normal );
	vec3 n = smooth_;

	if ( d.material.w > 0.0 ) {
		vec3 bump = texture( TEX1( d ), v_texCoord0 ).xyz * 2.0 - 1.0;
		n = normalize( CotangentFrame( smooth_, v_position, v_texCoord0 ) * bump );
	}
	vec3 l = normalize( t.lightDir.xyz );
	vec3 h = normalize( l + toEye );
	float facing = max( dot( n, l ), 0.0 );
	float wrap = d.material2.x;
	float wrapped = max( ( dot( n, l ) + wrap ) / ( 1.0 + wrap ), 0.0 );
	float wrapped2 = max( ( dot( n, normalize( t.lightDir2.xyz + vec3( 0.0, 0.0, 1e-5 ) ) ) + wrap ) / ( 1.0 + wrap ), 0.0 );
	vec3 color = albedo * min( t.ambientLight.rgb + wrapped * t.directedLight.rgb + wrapped2 * t.directedLight2.rgb, vec3( 1.0 ) );

	if ( wrap > 0.0 ) {
		color += albedo * t.ambientLight.rgb * wrap * 0.3 * vec3( 0.55, 0.75, 1.0 ) * ( 1.0 - facing );
	}
	if ( d.material2.y > 0.0 ) {
		// (the glitter of snow shows in the shade too, a little)
		color += max( t.directedLight.rgb, t.ambientLight.rgb * 0.5 ) * d.material2.y * 4.0 * Sparkle( v_position, n, l, toEye );
	}

	color += t.directedLight.rgb * d.material.x * pow( max( dot( n, h ), 0.0 ), max( d.material.y, 1.0 ) ) * step( 0.0001, facing );
	if ( d.material.z > 0.0 ) {
		float fresnel = 0.04 + 0.96 * pow( 1.0 - max( dot( smooth_, toEye ), 0.0 ), 5.0 );

		color += t.directedLight.rgb * d.material.z * 2.0 * pow( max( dot( smooth_, h ), 0.0 ), 300.0 ) * step( 0.0001, dot( smooth_, l ) );
		color += ( t.ambientLight.rgb + t.directedLight.rgb * 0.3 ) * d.material.z * fresnel * 0.6;
	}
	return color;
}

// The light grid of the level at a point of the world (R_CreatePbrGrid): xyz the direction the light comes from times
// its share of the light there, w how much of the point lies in the open. Two layers of the picture, each filtered
// by the texture unit, mixed by the height; the cells of walls are taken out by dividing by alpha.
vec4 PbrGrid( Draw d, vec3 world ) {
	vec3 cells = d.grid2.xyz;
	vec3 g = clamp( ( world - d.grid0.xyz ) * d.grid1.xyz, vec3( 0.0 ), cells - 1.0 );
	float z0 = floor( g.z ), z1 = min( z0 + 1.0, cells.z - 1.0 );
	vec2 size = vec2( d.grid2.w, d.grid1.w );
	vec2 a = ( vec2( mod( z0, d.grid0.w ), floor( z0 / d.grid0.w ) ) * cells.xy + g.xy + 0.5 ) / size;
	vec2 b = ( vec2( mod( z1, d.grid0.w ), floor( z1 / d.grid0.w ) ) * cells.xy + g.xy + 0.5 ) / size;
	vec4 v = mix( textureLod( TEX4( d ), a, 0.0 ), textureLod( TEX4( d ), b, 0.0 ), g.z - z0 );

	if ( v.a < 0.01 ) {
		return vec4( 0.0 );
	}
	return vec4( ( v.rgb / v.a ) * 2.0 - 1.0, v.a );
}

// What the probe sees in a direction of the world, at a level for the roughness. Its six faces lie side by side in a
// row of the atlas, looking along +x, -x, +y, -y, +z, -z (forward, left, up as R_ProbeViews has them); a face shows
// left on the left and up at the top. Kept half a pixel of its level inside its face.
vec3 ProbeSample( Draw d, float probe, vec3 dir, float level ) {
	vec3 a = abs( dir ), forward, left, up;
	float face;
	if ( a.x >= a.y && a.x >= a.z ) {
		face = dir.x > 0.0 ? 0.0 : 1.0;
		forward = vec3( sign( dir.x ), 0.0, 0.0 );
		left = vec3( 0.0, sign( dir.x ), 0.0 );
		up = vec3( 0.0, 0.0, 1.0 );
	} else if ( a.y >= a.z ) {
		face = dir.y > 0.0 ? 2.0 : 3.0;
		forward = vec3( 0.0, sign( dir.y ), 0.0 );
		left = vec3( -sign( dir.y ), 0.0, 0.0 );
		up = vec3( 0.0, 0.0, 1.0 );
	} else {
		face = dir.z > 0.0 ? 4.0 : 5.0;
		forward = vec3( 0.0, 0.0, sign( dir.z ) );
		left = vec3( 0.0, 1.0, 0.0 );
		up = vec3( -sign( dir.z ), 0.0, 0.0 );
	}
	float f = dot( dir, forward );
	vec2 st = vec2( -dot( dir, left ), -dot( dir, up ) ) / f * 0.5 + 0.5;
	float margin = 0.5 / ( float( d.probeSize ) / exp2( level ) );
	st = clamp( st, vec2( margin ), vec2( 1.0 - margin ) );
	return textureLod( TEX6( d ), vec2( ( face + st.x ) / 6.0, ( probe + st.y ) / float( d.probeCount ) ), level ).rgb;
}

// The probes of the eight cells of the grid around a point of the world, each weighed as the light grid is (by how
// near the point is to it), so the mirror goes over from one probe to the next across a cell and not at its edge.
// Cells that take the same probe give their weight to the first of them, so a probe is read once however many cells
// take it (mostly one or two). Found once for all the reads of the probes a pixel makes.
// The probe a cell takes (R_CreateProbes) is its number in a pixel of the grid picture (the layout of PbrGrid). Where
// that pixel lies is worked out for the two layers, rows and columns the eight cells share and not for each cell:
// dividing whole numbers is slow on a graphics card, and it was done sixteen times a pixel.
struct ProbeSet {
	float	probe[8];
	float	weight[8];
};

ProbeSet ProbesNear( Draw d, vec3 world ) {
	ProbeSet set;
	vec3 g = ( world - d.grid0.xyz ) * d.grid1.xyz;
	vec3 base = floor( g ), f = g - base;
	ivec3 cells = ivec3( d.grid2.xyz );
	int tiles = int( d.grid0.w );
	ivec3 lo = clamp( ivec3( base ), ivec3( 0 ), cells - 1 ), hi = clamp( ivec3( base ) + 1, ivec3( 0 ), cells - 1 );
	ivec2 layerLo = ivec2( ( lo.z % tiles ) * cells.x, ( lo.z / tiles ) * cells.y );
	ivec2 layerHi = ivec2( ( hi.z % tiles ) * cells.x, ( hi.z / tiles ) * cells.y );
	for ( int i = 0; i < 8; i++ ) {
		bvec3 far = bvec3( ( i & 1 ) != 0, ( i & 2 ) != 0, ( i & 4 ) != 0 );
		vec3 w3 = mix( 1.0 - f, f, vec3( far ) );
		ivec2 texel = ( far.z ? layerHi : layerLo ) + ivec2( far.x ? hi.x : lo.x, far.y ? hi.y : lo.y );
		set.probe[i] = floor( texelFetch( TEX5( d ), texel, 0 ).r * 255.0 + 0.5 );
		set.weight[i] = w3.x * w3.y * w3.z;
	}
	// (fixed bounds, so the arrays stay in registers)
	for ( int i = 1; i < 8; i++ ) {
		for ( int j = 0; j < i; j++ ) {
			if ( set.weight[i] > 0.0 && set.probe[j] == set.probe[i] ) {
				set.weight[j] += set.weight[i];
				set.weight[i] = 0.0;
			}
		}
	}
	return set;
}

// what the probes of set see in a direction of the world, at a level for the roughness; a probe with almost no
// weight here is not read
vec3 ProbesSee( Draw d, ProbeSet set, vec3 dir, float level ) {
	vec3 sum = vec3( 0.0 );
	float total = 0.0;
	for ( int i = 0; i < 8; i++ ) {
		if ( set.weight[i] > 0.02 ) {
			sum += ProbeSample( d, set.probe[i], dir, level ) * set.weight[i];
			total += set.weight[i];
		}
	}
	return total > 0.0 ? sum / total : sum;
}

vec3 ProbesAround( Draw d, vec3 world, vec3 dir, float level ) {
	return ProbesSee( d, ProbesNear( d, world ), dir, level );
}

float D_GGX( float nh, float a ) {
	float a2 = a * a;
	float k = nh * nh * ( a2 - 1.0 ) + 1.0;
	return a2 / ( 3.14159265 * k * k );
}

// Parallax occlusion (normalHeightMap, parallaxDepth, parallaxPlane): the ray from the eye goes down into the height
// in the alpha of the normal map (1 at the top) in steps until it is below it, and the last step is split where it
// crosses. The polygon lies at height plane: from 1 the surface carved into it (what stands on it floats over the
// hollows), from 0 the surface rises out of it (feet stand in the bumps), between the two the error is split. The
// ray starts where it is at height 1, above the polygon by what is over the plane. e: the direction to the eye in
// the frame of the texture, z out of the surface. The texture is read with the slopes of the pixel, the loop is not
// the same for neighbouring pixels. The steps follow the angle: 8 looking straight down, 32 at a flat angle, where
// few steps cut the bumps into slices.
vec2 Parallax( Draw d, vec2 uv, vec3 e, float depth, float plane, float stepScale ) {
	int STEPS = max( 4, int( mix( 32.0, 8.0, clamp( e.z, 0.0, 1.0 ) ) * stepScale + 0.5 ) );
	vec2 dx = dFdx( uv ), dy = dFdy( uv );
	vec2 through = -e.xy / max( e.z, 0.2 ) * depth;
	vec2 delta = through / float( STEPS );
	vec2 cur = uv - through * ( 1.0 - clamp( plane, 0.0, 1.0 ) );
	float layer = 1.0, h = textureGrad( TEX2( d ), cur, dx, dy ).a, lastLayer = 1.0, lastH = h;
	for ( int i = 0; i < STEPS && layer > h; i++ ) {
		lastLayer = layer;
		lastH = h;
		layer -= 1.0 / float( STEPS );
		cur += delta;
		h = textureGrad( TEX2( d ), cur, dx, dy ).a;
	}
	float before = lastH - lastLayer, after = h - layer;
	return cur - delta * clamp( after / max( after - before, 1e-4 ), 0.0, 1.0 );
}

// A plain stage over a stage with parallax (vk_shade.cpp VKP_FOLLOW, a detail texture): the shift that stage's texture
// gets, worked out again from its height and coordinates (v_texCoord1), carried over to this stage's coordinates. They
// follow those of the material in a straight line (tcMod scale, rotate), so the screen derivatives of both give it.
vec2 FollowParallax( Draw d, vec2 uv ) {
	vec2 base = v_texCoord1;
	mat2 b = mat2( dFdx( base ), dFdy( base ) ), s = mat2( dFdx( uv ), dFdy( uv ) );
	float parallax = d.pbr.z * clamp( ( 1536.0 - v_depth ) / 768.0, 0.0, 1.0 );
	Transform t = TRANSFORMS( d.transform );
	vec3 toEye = normalize( t.viewOrigin.xyz - v_position );
	vec3 n = FlatNormal( toEye );
	mat3 frame = CotangentFrame( n, v_position, base );
	vec2 moved = Parallax( d, base, vec3( dot( frame[0], toEye ), dot( frame[1], toEye ), dot( n, toEye ) ), parallax, d.pbr2.x,
		clamp( 320.0 / max( v_depth, 1.0 ), 0.25, 1.0 ) );
	float det = determinant( b );

	if ( parallax <= 0.0 || abs( det ) < 1e-14 ) {
		return uv;
	}
	return uv + s * inverse( b ) * ( moved - base );
}

// How much of a moving light the bumps of the height leave at uv: from there up towards the light (l, in the frame of the
// texture) the height must stay under the ray. Soft, what stands close to the point counts most. start: the height at
// uv, which is the same for every light of the pixel and is read once for all of them.
float ParallaxLight( Draw d, vec2 uv, vec2 dx, vec2 dy, vec3 l, float depth, float start ) {
	const int STEPS = 8;
	// (at the top of the height nothing stands over the point: the way to the light has no length)
	if ( start >= 1.0 ) {
		return 1.0;
	}
	vec2 along = l.xy / max( l.z, 0.15 ) * depth * ( 1.0 - start );
	float over = 0.0;

	for ( int i = 1; i <= STEPS; i++ ) {
		float t = float( i ) / float( STEPS );
		float h = textureGrad( TEX2( d ), uv + along * t, dx, dy ).a;

		over = max( over, ( h - mix( start, 1.0, t ) ) * ( 1.0 - t * 0.5 ) );
	}
	return 1.0 - clamp( over * 6.0, 0.0, 0.85 );
}

// A wall whose texture lay over a painted reflection by its alpha (tr_shader.cpp MergeReflectedTexture): where it let the
// reflection through it is smooth and partly metal, and takes the real reflections. The alpha of these runs from 1 to
// about 0.7, so that is the whole range.
void AlphaShine( float alpha, inout float roughness, inout float metalness ) {
	float shine = clamp( ( 1.0 - alpha ) * 3.0, 0.0, 1.0 );

	roughness = mix( roughness, 0.3, shine );
	metalness = mix( metalness, 0.7, shine );
}

// the reflection of light that comes from all around, for a roughness and an angle (Karis, "Physically Based Shading
// on Mobile", 2014)
vec3 EnvBRDF( vec3 f0, float roughness, float nv ) {
	const vec4 c0 = vec4( -1.0, -0.0275, -0.572, 0.022 );
	const vec4 c1 = vec4( 1.0, 0.0425, 1.04, -0.04 );
	vec4 r = roughness * c0 + c1;
	float a004 = min( r.x * r.x, exp2( -9.28 * nv ) ) * r.x + r.y;
	vec2 ab = vec2( -1.04, 1.04 ) * a004 + r.zw;
	return f0 * ab.x + ab.y;
}

/* A stage with a material of GGX (shader keywords of the material tool: normalMap, rmoMap, roughness, metalness,
   parallaxDepth). It is lit by the light it gets anyway, so that a surface without bumps and without shine looks as
   it did:
	 a model: the ambient and the directed light of its entity, from the direction of its entity;
	 a surface of the level: its lightmap (or its vertex colors), split by the light grid into the part that comes
	 from one direction and the rest. With share k of the light from direction l, a flat surface gets
	 ambient + directed * N.l = light, so ambient = light (1 - k) / flat and directed = light k / flat with
	 flat = 1 - k + k N.l; with bumps N.l changes and the shine of GGX comes on top.
   All in linear light: the textures and the light to the power 2.2 and back, which keeps albedo times light exactly
   what it was. */
// A surface lit per pixel (PbrStage, the shine of glass), its vectors in one space: the normal with its bumps, the
// normal of the polygon, the direction to the eye; albedo and F0 in linear light; a clear coat over it (amount and
// roughness, on the polygon, F0 0.04), sheen (cloth: colour and roughness), and the direction of the brush strokes
// with how far anisotropy draws the shine out along them.
struct Surface {
	vec3	n;
	vec3	flat_;
	vec3	v;
	vec3	albedo;
	vec3	f0;
	float	metalness;
	float	roughness;
	float	coat;
	float	coatRoughness;
	vec3	sheen;
	float	sheenRoughness;
	vec3	along;
	float	anisotropy;
};

// the distribution of the sheen of cloth (Estevez and Kulla 2017)
float D_Charlie( float nh, float r ) {
	float inv = 1.0 / max( r, 0.05 );
	float sin2 = max( 1.0 - nh * nh, 0.0078125 );
	return ( 2.0 + inv ) * pow( sin2, inv * 0.5 ) / 6.2831853;
}

// GGX drawn out along t (at) and b (ab): brushed metal (Burley 2012)
float D_GGXAniso( vec3 h, vec3 n, vec3 t, vec3 b, float at, float ab ) {
	float th = dot( t, h ), bh = dot( b, h ), nh = dot( n, h );
	float x = th * th / ( at * at ) + bh * bh / ( ab * ab ) + nh * nh;
	return 1.0 / ( 3.14159265 * at * ab * x * x );
}

// The light from direction l, of radiance (light as the engine counts it: a white surface lit straight on gets it as
// it is), on the surface: what it adds to diffuse, and the shine it gives back. minRoughness widens the lobe of light
// that is many lamps taken together (the grid, an entity).
vec3 SurfaceLight( Surface s, vec3 l, vec3 radiance, float minRoughness, inout vec3 diffuse ) {
	float nl = max( dot( s.n, l ), 0.0 );
	if ( nl <= 0.0 || dot( s.flat_, l ) <= 0.0001 ) {
		return vec3( 0.0 );
	}
	vec3 h = normalize( l + s.v );
	float nv = max( dot( s.n, s.v ), 1e-4 ), nh = max( dot( s.n, h ), 0.0 ), vh = max( dot( s.v, h ), 0.0 );
	float r = max( s.roughness, minRoughness ), a = r * r, kv = a * 0.5;
	vec3 fresnel = s.f0 + ( 1.0 - s.f0 ) * pow( 1.0 - vh, 5.0 );
	float visibility = 0.25 / ( ( nl * ( 1.0 - kv ) + kv ) * ( nv * ( 1.0 - kv ) + kv ) );
	float distribution;
	if ( abs( s.anisotropy ) > 0.01 ) {
		vec3 t = normalize( s.along - s.n * dot( s.along, s.n ) );
		vec3 b = cross( s.n, t );
		distribution = D_GGXAniso( h, s.n, t, b, max( a * ( 1.0 + s.anisotropy ), 0.002 ), max( a * ( 1.0 - s.anisotropy ), 0.002 ) );
	} else {
		distribution = D_GGX( nh, a );
	}
	vec3 shine = distribution * visibility * fresnel;
	if ( dot( s.sheen, s.sheen ) > 0.0 ) {
		shine += s.sheen * D_Charlie( nh, s.sheenRoughness ) / ( 4.0 * ( nl + nv - nl * nv ) );
	}
	// what the clear coat mirrors the layer under it does not get
	float coatFresnel = 0.0;
	vec3 coat = vec3( 0.0 );
	if ( s.coat > 0.0 ) {
		float cnl = max( dot( s.flat_, l ), 0.0 ), cnv = max( dot( s.flat_, s.v ), 1e-4 ), cnh = max( dot( s.flat_, h ), 0.0 );
		float ca = max( s.coatRoughness, 0.02 );
		ca *= ca;
		float ck = ca * 0.5;
		coatFresnel = ( 0.04 + 0.96 * pow( 1.0 - vh, 5.0 ) ) * s.coat;
		coat = vec3( D_GGX( cnh, ca ) * 0.25 / ( ( cnl * ( 1.0 - ck ) + ck ) * ( cnv * ( 1.0 - ck ) + ck ) ) * coatFresnel * cnl );
	}
	diffuse += s.albedo * ( 1.0 - s.metalness ) * radiance * nl * ( 1.0 - coatFresnel );
	return radiance * ( shine * nl * ( 1.0 - coatFresnel ) + coat );
}

#include "geom.glsl"

// the point of the segment from a to b nearest the ray from p along r (Karis 2013, the representative point of a
// tube light for its shine)
vec3 NearestToRay( vec3 a, vec3 b, vec3 p, vec3 r ) {
	vec3 l0 = a - p, ld = b - a;
	float rd = dot( r, ld );
	float t = ( dot( r, l0 ) * rd - dot( l0, ld ) ) / max( dot( ld, ld ) - rd * rd, 1e-4 );
	return a + ld * clamp( t, 0.0, 1.0 );
}

/* One dynamic light on what is already drawn there, added by a pass of its own (vk_shade.cpp, the loop over
   tess.dlightBits): a point (parm: where in the space of the model, w how far it reaches) or a tube from grid0.xyz to
   grid1.xyz reaching grid0.w from it (a saber blade, ext.AddLineLightToScene); a point that lights only round the plane
   across the axis grid0.xyz, grid0.w negative (a band, ext.AddBandLightToScene). Until 2026-11-08 this was rd-vanilla's
   round light image projected along the normal of the triangle (ProjectDlightTexture2): flat, no bumps, no shine.
   Now the light falls off smoothly to nothing at its reach and is brighter close by; the surface takes it by the
   angle it meets it, with the normal map and the shine of its material where the stage has one (VKD_PBR, the
   textures and numbers VK_PbrDraw gives a stage), else with a faint shine of its own. For a tube the diffuse light
   comes from the nearest point of the tube and the shine from the point nearest the mirrored ray, so the highlight on
   a floor runs along the blade. Worked out in linear light; the light colour and its fall in the light of the game
   as before. tex0: the texture of the surface (white where the frame already holds it: then the pass multiplies). */
vec4 SafeColor( vec4 c );

// the numbers of one dynamic light: from the draw of its pass, or from its place in the list of the view
struct Lamp {
	vec4	parm;		// where, w: how far it reaches
	vec4	grid0;		// a tube: its start, w its reach; a band: its axis, w negative; a rectangle: a half edge, w -2
	vec4	grid1;		// a tube: its end; a rectangle: the other half edge; w: the channel of its shadow mask
	vec3	color;
	uint	mask;		// the texture with the shadow mask
};

Lamp LampOf( Draw d ) {
	return Lamp( d.parm, d.grid0, d.grid1, d.baseColor.rgb, d.tex1 );
}

// A pass draws one light, the draw itself (bits 1), or several at once: the lights of the level view on a surface
// of the level (vk_shade.cpp VK_DynamicLights), which lie in the draws from d.decals on, the draw names them a bit
// each in d.numDecals. Each light is worked out as in a pass of its own and they are added up as the passes were;
// the texture, the parallax and the material of the place are looked up once, which is what a pass for each light
// cost most (5 ms a frame in rocket fire at 2560x1440, measured 2026-11-26).
#ifdef VK_GLES
#define LAMP_BITS( d )		1u
#define LAMP( d, i )		LampOf( d )
#define LOWEST_BIT( x )		0
#define LAMP_MASK( lamp )	u_tex1
#else
#define LAMP_BITS( d )		( ( d ).numDecals != 0u ? ( d ).numDecals : 1u )
#define LAMP( d, i )		( ( d ).numDecals != 0u ? LampOf( DRAWS( ( d ).decals + uint( i ) ) ) : LampOf( d ) )
#define LOWEST_BIT( x )		findLSB( x )
#define LAMP_MASK( lamp )	u_textures[nonuniformEXT( ( lamp ).mask )]
#endif

#ifndef VK_GLES
// How much of a light the level and the players hide at the pixel, 0 to 1: their shadows from the lights, made into a
// mask before the light passes (vk_rt.cpp, smlights.frag). clip: the point where it was in the view the mask was
// made in (globals.rt 42-45, its rect 46). grid1.w of the light says the channel of the mask, the lamp its texture. A
// model has it negative, -1 - the channel: the mask is made at the pixels of the scene, his own among them, so a leg
// behind his body is in its shadow.
float LampShadow( Lamp lamp, bool model, vec4 clip ) {
	int channel = model ? int( -lamp.grid1.w + 0.5 ) - 1 : int( lamp.grid1.w + 0.5 );
	if ( channel <= 0 || clip.w <= 1.0 ) {
		return 0.0;
	}
	vec2 ndc = clip.xy / clip.w;
	vec4 rect = globals.rt[46];
	vec2 pixel = vec2( rect.x + ( ndc.x * 0.5 + 0.5 ) * rect.z, rect.y + ( 0.5 - ndc.y * 0.5 ) * rect.w );
	// (the mask has the size of the frame, shaders/rtlightresolve.frag; its picture is two masks high, channels
	// 5 to 8 in the lower one, vk_rt.cpp)
	vec2 maskSize = vec2( textureSize( LAMP_MASK( lamp ), 0 ) );
	vec2 at = pixel / vec2( maskSize.x, maskSize.y * 0.5 );
	at.y = clamp( at.y, 0.0, 1.0 ) * 0.5;
	// (not into the other mask at the edge of the screen)
	at.y = clamp( at.y, 0.5 / maskSize.y, 0.5 - 0.5 / maskSize.y ) + ( channel > 4 ? 0.5 : 0.0 );
	// (the mask is one picture without smaller copies: read as it is, inside the loops over the lights)
	return textureLod( LAMP_MASK( lamp ), at, 0.0 )[( channel - 1 ) & 3];
}
#endif

vec4 DynamicLight( Draw d ) {
	Transform t = TRANSFORMS( d.transform );
	vec3 toEye = normalize( t.viewOrigin.xyz - v_position );
	// The plane of the triangle, on the side that is looked at, and the normal of the vertices where they have one. A
	// patch folded round hard corners has them smoothed across its flat sides, by up to 45 degrees each way: one edge of
	// a side turned from a flash beside it and went dark, and past that it was flipped to face the flash and lit up
	// (mp/ffa2, the pillars, their picture 2026-11-24). Such a patch has short normals (vk_world.cpp) and takes the plane.
	vec3 plane = normalize( cross( dFdx( v_position ), dFdy( v_position ) ) );
	if ( dot( plane, toEye ) < 0.0 ) {
		plane = -plane;
	}
	// A model (grid1.w negative, vk_shade.cpp VK_DynamicLights) has its normals smoothed round its sides: at its
	// outline they turn a little away from the eye without the surface being seen from its back, and turned round
	// there they faced a light behind the model, which lit his outline (their pictures 2026-11-24, the boots).
	bool model = d.grid1.w < -0.5;
	vec3 flat_ = plane;
	if ( dot( v_normal, v_normal ) > 0.25 ) {
		flat_ = normalize( v_normal );
		if ( dot( flat_, plane ) < ( model ? -0.5 : 0.0 ) ) {
			flat_ = -flat_;		// a surface seen from its back
		}
	}
	// Which of the lights reach this place. A light a little below the plane still lights it: the light of an
	// explosion lies just under the floor it hit, a blade on the floor lies in it. Not on a model (grid1.w -1,
	// vk_shade.cpp VK_DynamicLights): its sides round away from the light, and lit from above the plane they took
	// the light where they turn from it and ended in a hard line 12 units behind it (their picture 2026-11-24, a
	// blade at a player's legs). The angle alone fades it there.
	// And which of those the shadows leave anything of (lit): a light behind a wall reaches the place and gives it
	// nothing.
	uint reach = 0u, lit = 0u;
#ifndef VK_GLES
	vec3 world = ( t.model * vec4( v_position, 1.0 ) ).xyz;
	vec4 clip = mat4( globals.rt[42], globals.rt[43], globals.rt[44], globals.rt[45] ) * vec4( world, 1.0 );
#endif
	for ( uint left = LAMP_BITS( d ); left != 0u; left &= left - 1u ) {
		int i = LOWEST_BIT( left );
		Lamp lamp = LAMP( d, i );
		vec3 lightPos = lamp.grid0.w > 0.0 ? NearestOnSegment( lamp.grid0.xyz, lamp.grid1.xyz, v_position )
			: lamp.grid0.w < -1.5 ? NearestOnRect( lamp.parm.xyz, lamp.grid0.xyz, lamp.grid1.xyz, v_position ) : lamp.parm.xyz;
		vec3 toLight = lightPos - v_position;

		if ( length( toLight ) < ( lamp.grid0.w > 0.0 ? lamp.grid0.w : lamp.parm.w ) && ( model || dot( flat_, toLight ) > -12.0 ) ) {
			reach |= 1u << uint( i );
#ifndef VK_GLES
			if ( LampShadow( lamp, model, clip ) < 1.0 ) {
				lit |= 1u << uint( i );
			}
#else
			lit |= 1u << uint( i );
#endif
		}
	}
#ifndef VK_GLES
	// Whether any light gives anything to this pixel or to the three it is worked out with: the card shades blocks of
	// two by two pixels together, and each of them needs the others to know how the texture changes from one pixel
	// to the next (the size it is read at). So a pixel in the shadow of every light may stop only where the whole
	// block is, and it learns that from its neighbours by how its own answer changes to theirs (Penner, "Shader
	// Amortization using Pixel Quad Message Passing", GPU Pro 2). Asked here, before any pixel leaves.
	float mine = lit != 0u ? 1.0 : 0.0;
	float pair = mine + abs( dFdxFine( mine ) );
	float block = pair + abs( dFdyFine( pair ) );
#endif
	if ( reach == 0u ) {
		discard;
	}
#ifndef VK_GLES
	// All in shadow: what follows (the way through the height of the parallax, the textures, each light) would add
	// up to nothing, which is what goes out, with the count of the lights in alpha as their passes left it. That work
	// was most of what a light behind a wall cost.
	if ( block == 0.0 ) {
		return vec4( 0.0, 0.0, 0.0, float( bitCount( reach ) ) );
	}
#endif

	// what of the place is the same for every light: where its texture is under the parallax, its colour, its material
	vec3 stageNormal = vec3( 0.0 );
	mat3 stageFrame = mat3( 1.0 );
	vec2 dx = vec2( 0.0 ), dy = vec2( 0.0 );
	vec2 uv = v_texCoord0;
	// The parallax of the stage this light falls on, or it lay flat on top of the carved surface: the same place of the
	// texture as PbrStage worked out (with the normal it took), and the bumps throw shadows from this light.
	float parallax = PBR( d.flags ) && ( d.pbrFlags & PBR_HEIGHT ) != 0u ? d.pbr.z * clamp( ( 1536.0 - v_depth ) / 768.0, 0.0, 1.0 ) : 0.0;
	if ( parallax > 0.0 ) {
		stageNormal = FlatNormal( toEye );
		stageFrame = CotangentFrame( stageNormal, v_position, uv );
		dx = dFdx( uv );
		dy = dFdy( uv );

		if ( ( d.pbrFlags & PBR_RAISED ) != 0u ) {
			// tessellated (vk_shade.cpp VK_DynamicLights): the surface already stands where the texture is, the height in
			// units becomes one of the texture by how far it runs along a unit of the surface
			vec3 px = dFdx( v_position ), py = dFdy( v_position );
			parallax *= sqrt( ( dot( dx, dx ) + dot( dy, dy ) ) / max( dot( px, px ) + dot( py, py ), 1e-6 ) );
		} else {
			uv = Parallax( d, uv, vec3( dot( stageFrame[0], toEye ), dot( stageFrame[1], toEye ), dot( stageNormal, toEye ) ), parallax, d.pbr2.x,
				clamp( 320.0 / max( v_depth, 1.0 ), 0.25, 1.0 ) );
		}
	}
	// (vertColor: the colour of the entity on this stage, white where it has none: vk_shade.cpp VK_DynamicLights. Its
	// alpha 2: the stage lies under the same texture drawn again by its alpha in no colour, as the players with a colour
	// to pick are made, so the colour is only where that alpha leaves it open.)
	vec4 painted = texture( TEX0( d ), uv );
	vec3 entityColor = d.vertColor.a > 1.5 ? mix( d.vertColor.rgb, vec3( 1.0 ), painted.a ) : d.vertColor.rgb;
	vec3 albedo = pow( painted.rgb * entityColor, vec3( 2.2 ) );
	Surface s;
	s.flat_ = flat_;
	s.v = toEye;
	s.n = flat_;
	s.albedo = albedo;
	s.metalness = 0.0;
	s.roughness = 0.6;
	s.f0 = vec3( 0.04 );
	s.coat = 0.0;
	s.coatRoughness = 0.0;
	s.sheen = vec3( 0.0 );
	s.sheenRoughness = 0.0;
	s.along = vec3( 1.0, 0.0, 0.0 );
	s.anisotropy = 0.0;
	float occlusion = 1.0;
	if ( PBR( d.flags ) ) {
		mat3 frame = CotangentFrame( flat_, v_position, uv );
		if ( ( d.pbrFlags & PBR_NORMAL ) != 0u ) {
			vec3 bump = texture( TEX2( d ), uv ).xyz * 2.0 - 1.0;
			bump.xy *= d.material2.y;
			s.n = normalize( frame * bump );
		}
		vec3 rmo = vec3( d.pbr.x, d.pbr.y, 1.0 );
		if ( ( d.pbrFlags & PBR_RMO_RELATIVE ) != 0u ) {
			vec3 made = texture( TEX3( d ), uv ).rgb;
			rmo = vec3( clamp( d.pbr.x + made.r * 2.0 - 1.0, 0.05, 1.0 ), d.pbr.y, made.b );
		} else if ( ( d.pbrFlags & PBR_RMO ) != 0u ) {
			rmo = texture( TEX3( d ), uv ).rgb;
			if ( ( d.pbrFlags & PBR_ORM ) != 0u ) {
				rmo = rmo.gbr;
			}
		}
		s.roughness = max( rmo.r, 0.04 );
		s.metalness = rmo.g;
		occlusion = rmo.b;
		if ( ( d.pbrFlags & PBR_ALPHA_SHINE ) != 0u ) {
			AlphaShine( painted.a, s.roughness, s.metalness );
		}
		s.f0 = mix( vec3( d.material2.z > 0.0 ? d.material2.z : 0.04 ), albedo, s.metalness );
		s.coat = d.pbr2.y;
		s.coatRoughness = d.pbr2.z;
		s.anisotropy = d.pbr2.w;
		// (the axis of the brush strokes is asked for only where the material has them, SurfaceLight)
		if ( abs( s.anisotropy ) > 0.01 ) {
			s.along = cos( d.material2.x ) * frame[0] + sin( d.material2.x ) * frame[1];
		}
	}

	// (the lights in whose shadow the pixel lies count as lights of it, as their passes did: the alpha)
#ifdef VK_GLES
	vec4 sum = vec4( 0.0 );
#else
	vec4 sum = vec4( 0.0, 0.0, 0.0, float( bitCount( reach & ~lit ) ) );
#endif
	// what every light of the pixel asks for again: the height under it (ParallaxLight), the ray from the eye mirrored
	// in the surface, whether its light is wrapped round it
	float height = parallax > 0.0 ? textureGrad( TEX2( d ), uv, dx, dy ).a : 1.0;
	vec3 mirrored = reflect( -toEye, s.n );
	bool wrapped = model && !PBR( d.flags );

	for ( uint left = lit; left != 0u; left &= left - 1u ) {
		int i = LOWEST_BIT( left );
		Lamp lamp = LAMP( d, i );
		bool tube = lamp.grid0.w > 0.0;
		// A rectangle that glows (grid0.w -2, a lamp panel of the level on a player, vk_shade.cpp VK_DynamicLights): round
		// parm with the half edges grid0.xyz and grid1.xyz. It lights from all of its face: how far it is counts from its
		// nearest point, the angle from where its face lies on average (its corners and its middle, so a large panel beside
		// somebody lights him from a wide angle and its light ends softly), the shine from where it shows in the surface as
		// in a mirror. Until 2026-11-25 a panel lit from its middle alone, a point.
		bool rect = lamp.grid0.w < -1.5;
		float range = tube ? lamp.grid0.w : lamp.parm.w;
		vec3 lightPos = tube ? NearestOnSegment( lamp.grid0.xyz, lamp.grid1.xyz, v_position )
			: rect ? NearestOnRect( lamp.parm.xyz, lamp.grid0.xyz, lamp.grid1.xyz, v_position ) : lamp.parm.xyz;
		vec3 toLight = lightPos - v_position;
		float dist = length( toLight );
		// (it is taken as standing a hand above the plane for the angle, see above)
		float above = dot( flat_, toLight );
		vec3 l = model ? toLight / max( dist, 0.001 ) : normalize( toLight + flat_ * max( 16.0 - above, 0.0 ) );
		if ( rect ) {
			vec3 c = lamp.parm.xyz - v_position, r = lamp.grid0.xyz, u = lamp.grid1.xyz;

			l = normalize( normalize( c ) + 0.5 * ( normalize( c + r + u ) + normalize( c + r - u ) + normalize( c - r + u ) + normalize( c - r - u ) ) );
		}

		float x = dist / range;
		float window = clamp( 1.0 - x * x * x * x, 0.0, 1.0 );
		// (about what the round light image of rd-vanilla gave halfway out, brighter close by). Not on a model: the light
		// of a weapon sits in it and in the hand that holds it, which turned white and covered the glow of the charge
		// (their picture 2026-11-24, the bowcaster); it is taken as at least a hand away.
		float xa = model ? max( dist, 24.0 ) / range : x;
		float fall = window * window / ( 1.0 + 4.0 * xa * xa ) * ( model ? 1.0 : 1.6 );
		// A band of light (grid0.w negative, ext.AddBandLightToScene): it leaves only round the plane through it across
		// its axis grid0.xyz, as the glowing ring of a thermal detonator lets it out past the body, and softly less out of
		// that plane; -grid0.w is how far out of it it still has most of it (the sine of the angle).
		if ( lamp.grid0.w < 0.0 && !rect ) {
			float off = dot( toLight, lamp.grid0.xyz ) / ( max( dist, 0.001 ) * -lamp.grid0.w );
			fall *= exp( -off * off );
		}
		vec3 radiance = pow( lamp.color * fall, vec3( 2.2 ) );
#ifndef VK_GLES
		radiance *= 1.0 - LampShadow( lamp, model, clip );
#endif
		// Nothing of it comes here (a light without colour, the very edge of its reach): all that follows would come out
		// as nothing, so it is left away. It still counts as a light of the pixel, as its pass did: the alpha.
		if ( radiance == vec3( 0.0 ) ) {
			sum.a += 1.0;
			continue;
		}
		// where its shine comes from: a tube and a rectangle are no points
		vec3 ls = l;
		if ( tube ) {
			// the point of the tube nearest the mirrored ray (the diffuse light comes from its nearest point)
			vec3 specPos = NearestToRay( lamp.grid0.xyz, lamp.grid1.xyz, v_position, mirrored );
			ls = normalize( specPos - v_position + ( model ? vec3( 0.0 ) : flat_ * max( 16.0 - dot( flat_, specPos - v_position ), 0.0 ) ) );
		} else if ( rect ) {
			// where the mirrored ray meets the plane of the panel, or the point of the panel nearest to that
			vec3 face = normalize( cross( lamp.grid0.xyz, lamp.grid1.xyz ) );
			float along = dot( mirrored, face );
			float t = abs( along ) > 1e-4 ? dot( lamp.parm.xyz - v_position, face ) / along : -1.0;
			vec3 specPos = NearestOnRect( lamp.parm.xyz, lamp.grid0.xyz, lamp.grid1.xyz, t > 0.0 ? v_position + mirrored * t : lightPos );

			ls = normalize( specPos - v_position );
		}
		// the bumps of the parallax throw shadows from this light
		if ( parallax > 0.0 ) {
			radiance *= ParallaxLight( d, uv, dx, dy, vec3( dot( stageFrame[0], l ), dot( stageFrame[1], l ), dot( stageNormal, l ) ), parallax, height );
		}

		vec3 diffuse = vec3( 0.0 );
		vec3 shine;
		if ( tube || rect ) {
			// a tube and a panel are no points, their highlight is a little wider
			vec3 unused = vec3( 0.0 );

			SurfaceLight( s, l, radiance, 0.0, diffuse );
			shine = SurfaceLight( s, ls, radiance, 0.15, unused );
		} else {
			shine = SurfaceLight( s, l, radiance, 0.0, diffuse );
		}
		// A model without a material of its own (cloth, skin, hair of the old models) takes no shine: the faint one of the
		// level, by a lamp right beside a player on his round surfaces, turned into highlights and bright rims at his
		// outline as on plastic (their picture 2026-11-24).
		// Its light also reaches a little round its sides, as on cloth and skin: lit by the angle alone it ended where the
		// surface turned across the light, a hard line between the lit and the dark side (their word 2026-11-24, "soften
		// the transitions"). Full on the side that faces the light, nothing from 30 degrees behind its edge.
		if ( wrapped ) {
			const float WRAP = 0.5;
			float wrap = clamp( ( dot( s.n, l ) + WRAP ) / ( 1.0 + WRAP ), 0.0, 1.0 );

			diffuse = s.albedo * radiance * wrap * wrap;
			shine = vec3( 0.0 );
		}
		// (each as its own pass put it into the frame: worked out in linear light, added in the light of the game)
		sum += SafeColor( vec4( pow( diffuse * occlusion + shine, vec3( 1.0 / 2.2 ) ), 1.0 ) );
	}
	return sum;
}

#ifndef VK_GLES
// The lamps of the light to judge materials by (globals.lights, VK_LightRows): lit per pixel with the roughness the
// material has (a lamp is small, its shine is not widened as the light of the grid is). The light falls off with the
// square of the distance in hundreds of units, to nothing at the range, and a spot between its two angles. The
// diffuse part is added to diffuse, the shine is returned. Lit in the space of the world.
vec3 DebugLights( Transform t, Surface s, inout vec3 diffuse ) {
	int count = int( globals.lights[0].x );
	if ( count <= 0 ) {
		return vec3( 0.0 );
	}
	vec3 world = ( t.model * vec4( v_position, 1.0 ) ).xyz;
	mat3 toWorld = mat3( t.model );
	Surface w = s;
	w.n = normalize( toWorld * s.n );
	w.flat_ = normalize( toWorld * s.flat_ );
	w.v = normalize( toWorld * s.v );
	w.along = toWorld * s.along;
	vec3 shine = vec3( 0.0 );

	for ( int i = 0; i < count && i < 8; i++ ) {
		vec4 r0 = globals.lights[2 + i * 4], r1 = globals.lights[3 + i * 4], r2 = globals.lights[4 + i * 4], r3 = globals.lights[5 + i * 4];
		vec3 l;
		float fall = 1.0;

		if ( r0.w == 2.0 ) {
			l = -r2.xyz;
		} else {
			vec3 toLight = r0.xyz - world;
			float dist = max( length( toLight ), 1.0 );
			l = toLight / dist;
			float hundreds = dist * 0.01;
			fall = 1.0 / max( hundreds * hundreds, 0.01 );
			if ( r1.w > 0.0 ) {
				float x = dist / r1.w;
				x = clamp( 1.0 - x * x * x * x, 0.0, 1.0 );
				fall *= x * x;
			}
			if ( r0.w == 1.0 ) {
				fall *= smoothstep( r3.x, r2.w, dot( -l, r2.xyz ) );
			}
		}
		if ( fall > 0.0 ) {
			shine += SurfaceLight( w, l, r1.rgb * fall, 0.0, diffuse );
		}
	}
	return shine;
}

/* Glass (VK_GlassDraw, material keywords transmission, ior, thickness, absorption): the copy of the frame in tex1
   behind the surface. Where it shows it is bent: by the bumps of its normal map, the ray through them (refract by
   ior) against the one through the even surface, both carried thickness units on (a thin pane 4) and projected to the
   screen; and moved to the side by the way through the pane, see there. It is blurred by the
   roughness (frosted), coloured by the absorption over the path through it (a thin pane by the colour alone) and
   by the texture as far as its alpha says. On top, what it mirrors of the probes by Fresnel (F0 from ior) and the
   shine of the lamps. A stage that adds its colour (pbr2.w) does not colour what shows through with its texture: black
   in it means nothing added. pbr: roughness, 1 with the distance of the solid level in tex3 and 2 with the faces of the glass in tex4 too, ior,
   v_pbr; pbr2:
   transmission, thickness, absorption distance, whether the stage adds; material: the absorption colour.

   The copy of the frame holds everything that was drawn, not only what lies behind this glass. A pane that reads it
   to the side shows things that are not behind it: a player in front of it a second time beside himself, and at its
   border what stands beside it, which the eye sees there as well (their pictures 2026-11-26: a silhouette of the
   player in the pane next to him). And what does lie behind it is cut off at its border. Real thick glass shows
   each thing behind it once. So a pane moves its picture inside its own face only, the face the eye sees of it
   (GlassOwn), and by less and less towards the border of that face, where it ends at nothing: every place behind the
   face is shown once, bent, and nothing from beside or in front of it comes in. A player in front of the pane cuts
   his outline out of the face, so the picture ends there the same way.

   That left the picture squeezed round whatever stands in front of the glass, and any other way of making up what
   the copy does not hold showed as well. Where something stands in front of thick glass of the level, the level is
   drawn once more, cut at the plane of the glass (R_GlassViews of tr_main.cpp, globals.glass), and the glass reads
   that picture wherever its copy shows what stands in front (GlassBehind, GlassColor): such places count as its own
   face, so the picture goes on behind a player in front of the pane as it does in real glass. pbr3.x: 1 where the
   view has the distances that is told by (of everything solid and of the level alone, their entries in globals.glass). */
vec3 g_glassPlane;		// 1 / the distance of the glass at the pixel, and how it changes a pixel across (xy) of the frame
vec2 g_glassHere;		// the pixel, as a place of the copy

// how far the surface of the glass, carried on flat, is at a place of the copy; below 0: behind the eye by there
// (one over the distance of a flat surface changes evenly across the screen)
float GlassPane( vec2 uv ) {
	float inv = g_glassPlane.z + dot( g_glassPlane.xy, ( uv - g_glassHere ) * globals.screen.xy );

	return inv > 1e-6 ? 1.0 / inv : -1.0;
}

// Whether the place uv of the copy shows something that stands in front of the glass. For glass that has no picture of
// its face (on a model, or a view without one): in front of its surface carried on to that place, not nearer than the
// glass is at the pixel that asks (a pane that runs away from the eye is nearer at one pixel than a player it passes
// behind a few pixels on).
bool GlassInFront( Draw d, vec2 uv ) {
	ivec2 size = textureSize( TEX3( d ), 0 );
	float solid = texelFetch( TEX3( d ), clamp( ivec2( uv * globals.screen.xy ), ivec2( 0 ), size - 1 ), 0 ).r;
	float pane = GlassPane( uv );

	// (a surface that has run behind the eye by there: all of it counts as in front)
	return pane <= 0.0 || solid < pane - 2.0;
}

int g_glassView;		// which view of what lies behind the glass the pixel reads (globals.glass), below 0: none

// Whether the place uv of the copy shows a model that stands in front of the glass where a view of what lies behind
// its plane has the picture without it. Told by three distances there: of everything solid (vk.softDepth), of the level
// alone (vk.worldDepth) and of the surface of the glass carried on to there: a model is the nearest thing, it is in front of the
// glass, and the level behind it is not (the floor in front of a pane is nothing the pane shows, and the view of what
// lies behind the plane has nothing there). A pixel next to such a place counts too: the copy is made of several
// samples a pixel, and the outline of the model is in the colour of the pixels along it.
bool GlassBehind( Draw d, vec2 uv ) {
#ifdef VK_GLES
	return false;
#else
	if ( g_glassView < 0 ) {
		return false;
	}
	vec4 part = globals.glass[2 + g_glassView * 3];

	if ( uv.x < part.x || uv.y < part.y || uv.x > part.z || uv.y > part.w ) {
		return false;
	}
	float pane = GlassPane( uv );

	if ( pane <= 0.0 ) {
		return false;
	}
	// (the draw has no place left for them: the entries of the two distances in the texture table are in the rows)
	uint solidSlot = uint( globals.glass[0].y ) - 1u, levelSlot = uint( globals.glass[0].z ) - 1u;
	ivec2 at = ivec2( uv * globals.screen.xy ), last = textureSize( u_textures[nonuniformEXT( solidSlot )], 0 ) - 1;

	for ( int i = 0; i < 5; i++ ) {
		ivec2 next = clamp( at + ivec2( i == 1 ? 1 : ( i == 2 ? -1 : 0 ), i == 3 ? 1 : ( i == 4 ? -1 : 0 ) ), ivec2( 0 ), last );
		float solid = texelFetch( u_textures[nonuniformEXT( solidSlot )], next, 0 ).r;
		float level = texelFetch( u_textures[nonuniformEXT( levelSlot )], next, 0 ).r;

		if ( solid < pane - 2.0 && solid < level - 1.0 && level > pane - 2.0 ) {
			return true;
		}
	}
	return false;
#endif
}

// The view of what lies behind the glass is drawn without what the view of the level lays over its solid surfaces
// afterwards (the shadows of the players, wet ground, smoke), and other glass in it has less to show than in the view
// of the level. Read as it is, it stood out round a player: pale where his own shadow lies on the floor about him
// (test level vjk_glass), blue where water in front of the floor mirrors the sky in the one picture and not in the
// other (t3_bounty, 2026-11-27). So it is read with what the copy of the frame has over it right beside what stands
// in front (GlassShade): g_glassShade, how much darker the copy is there, and g_glassAdded, what it has on top.
vec3 g_glassShade, g_glassAdded;

// what lies behind the glass at the place uv of its copy of the frame
vec3 GlassColor( Draw d, vec2 uv ) {
#ifndef VK_GLES
	// (v_glassDebug 4: the view behind the glass alone, wherever it was drawn, to see what it has)
	if ( globals.glass[0].w > 0.5 && g_glassView >= 0 ) {
		vec4 part = globals.glass[2 + g_glassView * 3];

		if ( uv.x > part.x && uv.y > part.y && uv.x < part.z && uv.y < part.w ) {
			return texture( u_textures[nonuniformEXT( uint( globals.glass[3 + g_glassView * 3].x ) - 1u )], uv ).rgb;
		}
	}
	if ( GlassBehind( d, uv ) ) {
		return texture( u_textures[nonuniformEXT( uint( globals.glass[3 + g_glassView * 3].x ) - 1u )], uv ).rgb * g_glassShade + g_glassAdded;
	}
#endif
	return texture( TEX1( d ), uv ).rgb;
}

// From the place uv the pixel reads, which shows something in front of the glass, towards the pixel itself: the first
// place that does not, where the copy of the frame and the view of what lies behind the glass show the same surface.
// What the one has of shadow and of light over the other there is taken for the place behind the model as well.
void GlassShade( Draw d, vec2 uv, vec2 here ) {
	g_glassShade = vec3( 1.0 );
	g_glassAdded = vec3( 0.0 );
#ifndef VK_GLES
	if ( !GlassBehind( d, uv ) ) {
		return;
	}
	vec4 part = globals.glass[2 + g_glassView * 3];

	for ( int i = 1; i <= 6; i++ ) {
		vec2 at = mix( uv, here, float( i ) / 6.0 );

		if ( at.x < part.x || at.y < part.y || at.x > part.z || at.y > part.w ) {
			return;
		}
		if ( !GlassBehind( d, at ) ) {
			// (over a few places along the way on: of one alone the grain of the surface came with it)
			vec2 on = ( here - uv ) / 6.0;
			vec3 copy = vec3( 0.0 ), view = vec3( 0.0 );

			for ( int k = 0; k < 3; k++ ) {
				vec2 from = clamp( at + on * float( k ), part.xy, part.zw );

				copy += texture( TEX1( d ), from ).rgb;
				view += texture( u_textures[nonuniformEXT( uint( globals.glass[3 + g_glassView * 3].x ) - 1u )], from ).rgb;
			}
			copy /= 3.0;
			view /= 3.0;
			g_glassShade = clamp( copy / max( view, vec3( 0.02 ) ), vec3( 0.25 ), vec3( 1.0 ) );
			g_glassAdded = max( copy - view, vec3( 0.0 ) );
			return;
		}
	}
#endif
}

// Whether the eye sees this glass at the place uv of the frame: the nearest thing there, glass or solid (tex4,
// vk_post.cpp VK_GlassMask), is as far as its surface. Not past the border of the frame. And where something stands
// in front of it that a view of what lies behind it has the picture without (GlassBehind).
bool GlassOwn( Draw d, vec2 uv ) {
	if ( uv != clamp( uv, vec2( 0.0 ), vec2( 1.0 ) ) ) {
		return false;
	}
	float pane = GlassPane( uv );

	if ( pane <= 0.0 ) {
		return false;
	}
	return abs( texelFetch( TEX4( d ), ivec2( uv * globals.screen.xy ), 0 ).r - pane ) < 1.0 + pane * 0.015 || GlassBehind( d, uv );
}

// How many pixels the face of the glass goes on from the pixel along a line of the screen (pixel: a pixel of that line
// in places of the copy), up to most. Looked at in four steps, so a thin thing in front of the pane is not stepped
// over, then the place where the face ends is halved in on.
float GlassRoom( Draw d, vec2 here, vec2 pixel, float most ) {
	float good = 0.0, bad = most;

	for ( int i = 1; i <= 4; i++ ) {
		float at = most * float( i ) * 0.25;

		if ( !GlassOwn( d, here + pixel * at ) ) {
			bad = at;
			break;
		}
		good = at;
	}
	if ( good >= most ) {
		return most;
	}
	for ( int i = 0; i < 4; i++ ) {
		float mid = ( good + bad ) * 0.5;

		if ( GlassOwn( d, here + pixel * mid ) ) {
			good = mid;
		} else {
			bad = mid;
		}
	}
	return good;
}

#define GLASS_MOST	0.07	// of the height of the frame: the most glass moves what it shows

vec4 g_glassAt, g_glassEye, g_glassAside;	// in clip space: the pixel on the glass, the eye, the move to the side
vec2 g_glassBump;							// what the bumps of the glass add, in places of the copy

// how far the solid level is at a place of the copy (tex3); below 0: it stands in front of the glass there
float GlassScene( Draw d, vec2 uv ) {
	ivec2 size = textureSize( TEX3( d ), 0 );
	float solid = texelFetch( TEX3( d ), clamp( ivec2( uv * globals.screen.xy ), ivec2( 0 ), size - 1 ), 0 ).r;
	float pane = GlassPane( uv );

	return ( pane <= 0.0 || solid < pane - 2.0 ) ? -1.0 : solid;
}

// By how much of the copy the glass moves something that lies scene far from the eye behind the pixel: the same way
// to the side at every distance, so less on the screen the farther it is. No further than a part of the frame: glass
// as thick as a wall, seen along its face, would move the picture by half the screen, and it fell apart.
vec2 GlassMove( float scene ) {
	vec4 straight = g_glassEye + ( g_glassAt - g_glassEye ) * max( scene / max( v_depth, 1.0 ), 1.0 );
	vec4 through = straight + g_glassAside;
	vec2 offset = g_glassBump + ( through.xy / through.w - straight.xy / straight.w ) * vec2( 0.5, -0.5 );
	float far_ = length( offset * globals.screen.xy ), most = GLASS_MOST * globals.screen.y;

	return far_ > most ? offset * ( most / far_ ) : offset;
}

/* From where in the copy the pixel takes its picture. The move depends on how far the thing shown is, and that is
   known only once the place is: taken by the distance at the pixel itself, a player behind the glass (near, moved
   far) and the floor behind him (far, moved little) were read with the wrong move along his outline, which showed a
   strip of him a second time beside himself and thinned his neck (their picture 2026-11-26, "when the player stands
   behind the glass the ghosting still appears"). So the place is looked for whose own distance moves it onto this
   pixel. What is near hides what is far, and is moved further: the way the picture can come from (from the move of
   the distance at the pixel up to the most, that of something right behind the pane) is looked along for the nearest
   thing, and if that, moved by its own distance, lands on this pixel, it is what the pixel shows. Else the pixel
   shows what its own distance says, unless that lands on the near thing, which is not here: then whichever of the
   two lands on what is farther, more of the ground behind (the near thing has moved off there, and nothing of the
   copy knows what it hid). */
#define GLASS_PROBES	10

vec2 GlassSource( Draw d, vec2 here ) {
	float	c0 = max( GlassScene( d, here ), v_depth );
	vec2	o0 = GlassMove( c0 );
	float	l0 = GlassScene( d, here + o0 );
	vec2	reach = GlassMove( v_depth );
	float	near_ = c0, tried = c0, fits = 1e9;
	vec2	oNear = o0, oFit = o0, oTried = o0;

	// (each distance met on the way that is nearer than the pixel's own is tried once: an arm in front of a chest is
	// two of them, and only one lands here)
	for ( int i = 0; i <= GLASS_PROBES; i++ ) {
		float at = i == 0 ? l0 : GlassScene( d, here + mix( o0, reach, float( i ) / float( GLASS_PROBES ) ) );

		if ( at <= 0.0 || at >= fits ) {
			continue;
		}
		vec2 o = GlassMove( at );

		if ( length( ( o - o0 ) * globals.screen.xy ) < 1.0 || length( ( o - oTried ) * globals.screen.xy ) < 0.75 ) {
			continue;
		}
		tried = at;
		oTried = o;
		if ( at < near_ ) {
			near_ = at;
			oNear = o;
		}
		float there = GlassScene( d, here + o );

		if ( there > 0.0 && there < fits && length( ( GlassMove( there ) - o ) * globals.screen.xy ) < 1.5 ) {
			fits = there;
			oFit = o;
		}
	}
	if ( fits < 1e9 ) {
		return oFit;
	}
	// nothing nearer lands here: the distance at the pixel says it, unless that lands on something near, which is not
	// here then
	if ( near_ >= c0 || ( l0 > 0.0 && length( ( GlassMove( l0 ) - o0 ) * globals.screen.xy ) < 1.5 ) ) {
		return o0;
	}
	return GlassScene( d, here + oNear ) > l0 ? oNear : o0;
}

// The copy of the frame is made of several samples a pixel, its distances of one: a pixel on the outline of something
// near can be far by its distance and half the near thing by its colour, and read as the ground it drew the outline
// of the near thing a second time, thin, where the ground behind it shows. So a place next to something much nearer
// is read a pixel and a half further from it.
vec2 GlassClear( Draw d, vec2 uv ) {
	float	at = GlassScene( d, uv );
	vec2	pixel = 1.0 / globals.screen.xy, push = vec2( 0.0 );

	if ( at <= 0.0 ) {
		return uv;
	}
	for ( int i = 0; i < 4; i++ ) {
		vec2	dir = vec2( i == 0 ? 1.0 : ( i == 1 ? -1.0 : 0.0 ), i == 2 ? 1.0 : ( i == 3 ? -1.0 : 0.0 ) );
		float	next = GlassScene( d, uv + dir * pixel );

		if ( next > 0.0 && next < at * 0.94 - 2.0 ) {
			push -= dir;
		}
	}
	return uv + push * pixel * 1.5;
}

vec4 GlassStage( Draw d ) {
	const vec2 disk[12] = vec2[12]( vec2( -0.326, -0.406 ), vec2( -0.840, -0.074 ), vec2( -0.696, 0.457 ), vec2( -0.203, 0.621 ),
		vec2( 0.962, -0.195 ), vec2( 0.473, -0.480 ), vec2( 0.519, 0.767 ), vec2( 0.185, -0.893 ), vec2( 0.507, 0.064 ),
		vec2( 0.896, 0.412 ), vec2( -0.322, -0.933 ), vec2( -0.792, -0.598 ) );
	Transform t = TRANSFORMS( d.transform );
	vec3 toEye = normalize( t.viewOrigin.xyz - v_position );
	vec3 flat_ = FlatNormal( toEye );
	vec3 n = flat_;
	if ( ( d.pbrFlags & PBR_NORMAL ) != 0u ) {
		mat3 frame = CotangentFrame( flat_, v_position, v_texCoord0 );
		n = normalize( frame * ( texture( TEX2( d ), v_texCoord0 ).xyz * 2.0 - 1.0 ) );
	}
	float roughness = d.pbr.x, ior = max( d.pbr.z, 1.0 ), transmission = d.pbr2.x, thickness = d.pbr2.y;
	float nv = max( dot( n, toEye ), 1e-4 );
#ifndef VK_GLES
	// The two rounds of the thick glass (vk_backend.cpp RB_DrawGlassRounds): first the panes that lie behind other glass
	// and face the eye (water behind a light shaft made glass), then, with a copy of the frame that has them, the
	// nearest glass, the face the eye sees (VK_GlassMask). Drawn far to near alone, the nearer glass came first where
	// its middle was farther, and its depth hid what lay behind it. (Not the back of a pane: seen through its front it
	// would bend the picture twice.)
	if ( d.pbr3.y > 0.5 ) {
		float nearest = texelFetch( TEX4( d ), ivec2( gl_FragCoord.xy ), 0 ).r;
		// (not behind the nearest thing: glass of a model is not in that picture and may stand in front of it)
		bool front = v_depth < nearest + 1.0 + v_depth * 0.015;
		bool facing = dot( v_normal, v_normal ) < 0.25 || dot( v_normal, toEye ) > 0.0;

		if ( d.pbr3.y < 1.5 ? ( front || !facing ) : !front ) {
			discard;
		}
	}
#endif

	// the screen behind, bent
	float dist = thickness > 0.0 ? thickness : 4.0;
	bool known = d.pbr.y > 0.5;
	vec3 bent = refract( -toEye, n, 1.0 / ior );
	if ( dot( bent, bent ) < 1e-6 ) {
		bent = -toEye;
	}
	// The ray through the pane as it would go without its bumps, and what the bumps of a normal map add: they turn the
	// ray, the more of the picture the thicker the glass.
	vec3 even = refract( -toEye, flat_, 1.0 / ior );
	if ( dot( even, even ) < 1e-6 ) {
		even = -toEye;
	}
	vec4 flatAt = t.mvp * vec4( v_position + even * dist, 1.0 );
	vec4 bumpAt = t.mvp * vec4( v_position + bent * dist, 1.0 );
	vec2 offset = bumpAt.xy / bumpAt.w - flatAt.xy / flatAt.w;
	// A pane with two even sides lets a ray out the way it came in, only moved to the side by its way through the
	// glass: what lies behind is seen moved by that much, the look of thick glass. (Taken away for flat panes for an
	// hour on 2026-11-26, to be rid of the second picture of a player in the pane: their word "das ist kein glas effekt
	// mehr". Then the player was shown unmoved in his place and the level moved round him: "so sieht niemals echtes
	// glas aus". What lies behind glass is moved, all of it; the second picture is dealt with where the copy is read.)
	// (the viewport turns the y of OpenGL: up on the screen is down in the frame)
	offset *= vec2( 0.5, -0.5 );
	vec2 here = gl_FragCoord.xy / globals.screen.xy;
	g_glassHere = here;
	g_glassPlane = vec3( dFdx( 1.0 / v_depth ), dFdy( 1.0 / v_depth ), 1.0 / v_depth );
	// the view of what lies behind this glass: the one of its own plane, or of a plane that lies as it does close in
	// front of it (the other side of the same pane)
	g_glassView = -1;
#ifndef VK_GLES
	if ( d.pbr3.x > 0.5 && globals.glass[0].y > 0.5 && globals.glass[0].z > 0.5 ) {
		vec3 world = ( t.model * vec4( v_position, 1.0 ) ).xyz;
		vec3 facing = normalize( mat3( t.model ) * flat_ );
		int count = min( int( globals.glass[0].x ), 3 );
		float least = 24.0;

		for ( int i = 0; i < count; i++ ) {
			vec4 plane = globals.glass[1 + i * 3];
			float past = dot( plane.xyz, world ) - plane.w;

			if ( globals.glass[3 + i * 3].x > 0.5 && abs( dot( plane.xyz, facing ) ) > 0.99 && past > -1.0 && past < least ) {
				least = past;
				g_glassView = i;
			}
		}
	}
#endif
	if ( known ) {
		// (the move to the side is the same however far what lies behind is, so on the screen the less the farther:
		// GlassSource finds the place of the copy whose own distance moves it onto this pixel)
		vec3 way = even * ( dist / max( dot( even, -flat_ ), 0.25 ) );
		vec3 eye = t.viewOrigin.xyz;

		g_glassAt = t.mvp * vec4( v_position, 1.0 );
		g_glassEye = t.mvp * vec4( eye, 1.0 );
		g_glassAside = t.mvp * vec4( way - toEye * dot( way, toEye ), 0.0 );
		g_glassBump = offset;
		offset = thickness > 0.0 ? GlassSource( d, here ) : GlassMove( GlassScene( d, here ) );
	} else {
		vec4 straight = t.mvp * vec4( v_position - toEye * dist, 1.0 );

		offset += ( flatAt.xy / flatAt.w - straight.xy / straight.w ) * vec2( 0.5, -0.5 );
		// (no further than a part of the frame: glass as thick as a wall, seen along its face, would move the picture
		// by half the screen, and it fell apart)
		float far_ = length( offset * globals.screen.xy ), most = GLASS_MOST * globals.screen.y;

		if ( far_ > most ) {
			offset *= most / far_;
		}
	}
	// inside its own face only (the header): the move shrinks towards the border of the face it points at and the one
	// behind it, over twice its own length, so the picture is stretched or squeezed by half there at most and ends
	// unmoved at the border
	bool faced = d.pbr.y > 1.5 && GlassOwn( d, here );
	float far_ = length( offset * globals.screen.xy );
	if ( faced ) {
		if ( far_ > 0.5 ) {
			vec2 pixel = offset / far_;
			float most = far_ * 2.0;

			offset *= min( GlassRoom( d, here, pixel, most ), GlassRoom( d, here, -pixel, most ) ) / most;
		}
	} else {
		// (glass without a picture of its face: not past the border of the frame, where every pixel read the last
		// column of the copy, streaks at the left and right of the picture)
		vec2 room = mix( here, 1.0 - here, step( 0.0, offset ) );

		offset *= clamp( room / max( abs( offset ) * 2.0, vec2( 1e-5 ) ), 0.0, 1.0 );
	}
	vec2 uv = here + offset;
	if ( !faced && known && GlassInFront( d, uv ) && !GlassBehind( d, uv ) ) {
		uv = here;
	}
	if ( known && thickness > 0.0 ) {
		uv = GlassClear( d, uv );
	}
	GlassShade( d, uv, here );
	vec3 back;
	float radius = roughness * roughness * 0.06;
	if ( radius < 0.0005 ) {
		back = GlassColor( d, uv );
	} else {
		vec2 aspect = vec2( globals.screen.y / globals.screen.x, 1.0 ) * radius;
		// the disk turned another way at each pixel (interleaved gradient noise, Jimenez 2014), so its twelve points
		// do not show as a pattern
		float turn = 6.2831853 * fract( 52.9829189 * fract( dot( gl_FragCoord.xy, vec2( 0.06711056, 0.00583715 ) ) ) );
		mat2 rotation = mat2( cos( turn ), sin( turn ), -sin( turn ), cos( turn ) );
		back = vec3( 0.0 );
		float count = 0.0;
		for ( int i = 0; i < 12; i++ ) {
			vec2 at = uv + rotation * disk[i] * aspect;

			// (frosted glass does not blur what stands in front of it or beside it into itself either)
			if ( faced ? !GlassOwn( d, at ) : ( known && GlassInFront( d, at ) && !GlassBehind( d, at ) ) ) {
				continue;
			}
			back += GlassColor( d, at );
			count += 1.0;
		}
		back = count > 0.0 ? back / count : GlassColor( d, uv );
	}
	back = pow( back, vec3( 2.2 ) );

	// coloured on its way through
	vec4 tint = texture( TEX0( d ), v_texCoord0 ) * v_color;
	vec3 tintLinear = pow( tint.rgb, vec3( 2.2 ) );
	vec3 absorbed = thickness > 0.0
		? pow( max( d.material.rgb, vec3( 1e-3 ) ), vec3( thickness / max( nv, 0.2 ) / max( d.pbr2.z, 1.0 ) ) )
		: d.material.rgb;
	vec3 passed = back * absorbed * mix( vec3( 1.0 ), tintLinear, tint.a );
	// what is not let through is the texture, as dim as the light around
	vec3 own = tintLinear * pow( max( v_color.rgb, vec3( 0.25 ) ), vec3( 2.2 ) ) * 0.5;
	if ( d.pbr2.w > 0.5 ) {
		// a stage that adds: clear glass where it lets through, and the scene with the texture added, as the stage
		// was drawn before, where it does not
		passed = back * absorbed;
		own = pow( texture( TEX1( d ), gl_FragCoord.xy / globals.screen.xy ).rgb + tint.rgb, vec3( 2.2 ) );
	}

	// what it mirrors
	float f0 = ( ior - 1.0 ) / ( ior + 1.0 );
	f0 *= f0;
	vec3 mirrored = vec3( 0.0 );
	vec3 fresnel = EnvBRDF( vec3( f0 ), roughness, nv );
	if ( ( d.pbrFlags & PBR_PROBES ) != 0u ) {
		vec3 world = ( t.model * vec4( v_position, 1.0 ) ).xyz;
		vec3 mirror = normalize( mat3( t.model ) * reflect( -toEye, n ) );
		mirrored = pow( ProbesAround( d, world, mirror, roughness * 5.0 ), vec3( 2.2 ) );
	} else {
		mirrored = back;
	}
	Surface pane;
	pane.n = n;
	pane.flat_ = flat_;
	pane.v = toEye;
	pane.albedo = vec3( 0.0 );
	pane.f0 = vec3( f0 );
	pane.metalness = 0.0;
	pane.roughness = max( roughness, 0.03 );
	pane.coat = 0.0;
	pane.coatRoughness = 0.0;
	pane.sheen = vec3( 0.0 );
	pane.sheenRoughness = 0.5;
	pane.along = flat_;
	pane.anisotropy = 0.0;
	vec3 unused = vec3( 0.0 );
	vec3 shine = DebugLights( t, pane, unused );

	vec3 color = mix( own, passed, transmission ) * ( 1.0 - fresnel ) + mirrored * fresnel + shine;
	return vec4( pow( color, vec3( 1.0 / 2.2 ) ), 1.0 );
}
#endif

#ifndef VK_GLES
/* v_ssr: what a smooth surface mirrors of the level on the screen. The picture is that of the frame before (vk_post.cpp
   VK_SsrPrep: its colours and how far each pixel was, the view it was seen in in globals.ssr), so the ray from world
   along dir is marched in steps that grow, each point looked up where it was in that view, until it lies a little
   behind what was there. Returns that colour (in the values of the frame) and how much to trust it: less towards the
   edges of the screen, the farther the hit and the rougher the surface; 0 where nothing was found or the ray left the
   screen, which leaves the probes. */
vec4 ScreenReflection( vec3 world, vec3 dir, float roughness ) {
	vec4 r5 = globals.ssr[5];

	// (far off a pixel is small and its mirror hardly shows)
	if ( r5.w < 0.5 || roughness > 0.6 || v_depth > 2500.0 ) {
		return vec4( 0.0 );
	}
	uint slot = uint( r5.w - 0.5 );
	mat4 view = mat4( globals.ssr[0], globals.ssr[1], globals.ssr[2], globals.ssr[3] );
	vec4 rect = globals.ssr[4];
	vec3 forward = globals.ssr[6].xyz;
	const int STEPS = 16;		// (growing by 1.38 they reach about 2700 units, as 24 by 1.25 reached the far edge of most rooms)
	float stepLength = 6.0;
	// (each pixel starts a little further along, so the steps do not show as bands)
	float t = 2.0 + stepLength * fract( 52.9829189 * fract( dot( gl_FragCoord.xy, vec2( 0.06711056, 0.00583715 ) ) ) );

	for ( int i = 0; i < STEPS; i++ ) {
		vec3 q = world + dir * t;
		vec4 clip = view * vec4( q, 1.0 );
		if ( clip.w < 1.0 ) {
			break;
		}
		vec2 ndc = clip.xy / clip.w;
		if ( any( greaterThan( abs( ndc ), vec2( 1.0 ) ) ) ) {
			break;
		}
		ivec2 pixel = ivec2( rect.x + ( ndc.x * 0.5 + 0.5 ) * rect.z, rect.y + ( 0.5 - ndc.y * 0.5 ) * rect.w );
		vec4 seen = texelFetch( u_textures[nonuniformEXT( slot )], pixel, 0 );
		float behind = dot( q - r5.xyz, forward ) - seen.a;
		if ( behind > 0.0 && behind < max( stepLength * 1.5, 12.0 ) ) {
			vec2 edge = 1.0 - smoothstep( vec2( 0.8 ), vec2( 1.0 ), abs( ndc ) );
			float trust = edge.x * edge.y * ( 1.0 - smoothstep( 0.25, 0.6, roughness ) ) * ( 1.0 - float( i ) / float( STEPS ) );
			return vec4( seen.rgb, trust * globals.ssr[6].w );
		}
		t += stepLength;
		stepLength *= 1.38;
	}
	return vec4( 0.0 );
}
#endif

vec4 PbrStage( Draw d ) {
#ifndef VK_GLES
	if ( ( d.pbrFlags & PBR_GLASS ) != 0u ) {
		return GlassStage( d );
	}
#endif
	Transform t = TRANSFORMS( d.transform );
	vec3 toEye = normalize( t.viewOrigin.xyz - v_position );
	vec3 flat_ = FlatNormal( toEye );		// (the side of a two sided surface that is looked at)
	vec2 uv = v_texCoord0;
	mat3 frame = CotangentFrame( flat_, v_position, uv );

	// far off the parallax fades out (from 768 to 1536 units in front of the eye) and is not worked out at all
	// beyond, where its few pixels cost the loop for nothing
	float parallax = d.pbr.z * clamp( ( 1536.0 - v_depth ) / 768.0, 0.0, 1.0 );
	if ( ( d.pbrFlags & PBR_HEIGHT ) != 0u && parallax > 0.0 ) {
		// (the steps get fewer with the distance, as the bumps get smaller on the screen: all of them within 320 units,
		// half at 640, a quarter from 1280 on)
		uv = Parallax( d, uv, vec3( dot( frame[0], toEye ), dot( frame[1], toEye ), dot( flat_, toEye ) ), parallax, d.pbr2.x,
			clamp( 320.0 / max( v_depth, 1.0 ), 0.25, 1.0 ) );
	}

	vec4 base = texture( TEX0( d ), uv );
	vec3 n = flat_;
	if ( ( d.pbrFlags & PBR_NORMAL ) != 0u ) {
		// normalScale makes the bumps stronger or weaker
		vec3 bump = texture( TEX2( d ), uv ).xyz * 2.0 - 1.0;
		bump.xy *= d.material2.y;
		n = normalize( frame * bump );
	}
	vec3 rmo = vec3( d.pbr.x, d.pbr.y, 1.0 );
	vec3 specF0 = vec3( 0.0 );
	bool specGloss = ( d.pbrFlags & PBR_SPECGLOSS ) != 0u;
	if ( ( d.pbrFlags & PBR_RMO_RELATIVE ) != 0u ) {
		// made by rmoGen: the change of the roughness around the one of the material, which stays live with its
		// metalness, and the occlusion
		vec3 made = texture( TEX3( d ), uv ).rgb;
		rmo = vec3( clamp( d.pbr.x + made.r * 2.0 - 1.0, 0.05, 1.0 ), d.pbr.y, made.b );
	} else if ( ( d.pbrFlags & PBR_RMO ) != 0u ) {
		rmo = texture( TEX3( d ), uv ).rgb;
		if ( ( d.pbrFlags & PBR_ORM ) != 0u ) {
			rmo = rmo.gbr;
		}
	} else if ( specGloss ) {
		// rend2's specular map: F0 in rgb, gloss in alpha, no metalness
		vec4 sg = texture( TEX3( d ), uv );
		specF0 = pow( sg.rgb, vec3( 2.2 ) );
		rmo = vec3( 1.0 - sg.a, 0.0, 1.0 );
	}
	float roughness = max( rmo.r, 0.04 ), metalness = rmo.g, occlusion = rmo.b;
	if ( ( d.pbrFlags & PBR_ALPHA_SHINE ) != 0u ) {
		AlphaShine( base.a, roughness, metalness );
		base.a = 1.0;		// (it is opaque now)
	}

	vec3 albedo, ambient, directed, l, directed2 = vec3( 0.0 ), l2 = vec3( 0.0, 0.0, 1.0 );
	float alpha;
	if ( ( d.pbrFlags & PBR_ENTITY ) != 0u ) {
		albedo = pow( base.rgb * v_color.rgb, vec3( 2.2 ) );
		alpha = base.a * v_color.a;
		ambient = pow( t.ambientLight.rgb, vec3( 2.2 ) );
		directed = pow( t.directedLight.rgb, vec3( 2.2 ) );
		l = normalize( t.lightDir.xyz );
		if ( t.directedLight2.rgb != vec3( 0.0 ) ) {
			directed2 = pow( t.directedLight2.rgb, vec3( 2.2 ) );
			l2 = normalize( t.lightDir2.xyz );
		}
	} else {
		vec3 light = ( d.pbrFlags & PBR_LIGHTMAP ) != 0u ? LightmapTexture( TEX1( d ), v_texCoord1 ).rgb * v_color.rgb : v_color.rgb;
		light = pow( light, vec3( 2.2 ) );
		albedo = pow( base.rgb, vec3( 2.2 ) );
		alpha = base.a * ( ( d.pbrFlags & PBR_LIGHTMAP ) != 0u ? v_color.a : 1.0 );
		vec4 grid = vec4( 0.0 );
		if ( ( d.pbrFlags & PBR_GRID ) != 0u ) {
			grid = PbrGrid( d, ( t.model * vec4( v_position, 1.0 ) ).xyz );
		}
		float k = length( grid.xyz );
		// the direction in the world, turned into the space of the model (a door, a lift)
		l = k > 0.001 ? normalize( transpose( mat3( t.model ) ) * grid.xyz ) : flat_;
		k = min( k, 0.95 );
		float facing = max( dot( flat_, l ), 0.0 );
		float flatLit = max( 1.0 - k + k * facing, 0.2 );
		// Where the polygon faces away from the direction of the grid, light / flatLit grew to five times the light
		// of the lightmap, and every bump that faced the grid lit up white (dark corners beside a bright opening). The
		// light from the direction is kept at 1.5 times the lightmap at most, and the even part is what the flat
		// polygon then lacks of its lightmap, so a flat surface still shows its lightmap exactly. Where the direction
		// runs along the polygon (a corridor lit from an opening at its end) the bumps that face it got up to 2.5
		// times the lightmap (QC ffa3 R2): there the cap goes down to half the lightmap.
		directed = light * min( k / flatLit, min( 1.5, 0.5 + facing ) );
		ambient = max( light - directed * facing, vec3( 0.0 ) );
	}

#ifndef VK_GLES
	// the light to judge materials by: the light of the lightmap, the grid or the entity gives way to an even one
	if ( globals.lights[0].y > 0.0 ) {
		ambient = globals.lights[1].rgb;
		directed = vec3( 0.0 );
	}
#endif

	Surface s;
	s.n = n;
	s.flat_ = flat_;
	s.v = toEye;
	s.metalness = metalness;
	s.roughness = roughness;
	if ( specGloss ) {
		s.f0 = specF0;
		s.albedo = albedo * ( 1.0 - max( specF0.r, max( specF0.g, specF0.b ) ) );
	} else {
		// specularReflectance: F0 of what is not metal, 0.04 when not given
		s.f0 = mix( vec3( d.material2.z > 0.0 ? d.material2.z : 0.04 ), albedo, metalness );
		s.albedo = albedo;
	}
	s.coat = d.pbr2.y;
	s.coatRoughness = d.pbr2.z;
	s.sheen = ( d.pbrFlags & PBR_SHEEN_ALBEDO ) != 0u ? albedo : d.material.rgb;
	s.sheenRoughness = d.material.w;
	s.anisotropy = d.pbr2.w;
	// (asked for only where the material has brush strokes: SurfaceLight, the mirror below)
	s.along = abs( s.anisotropy ) > 0.01 ? cos( d.material2.x ) * frame[0] + sin( d.material2.x ) * frame[1] : frame[0];
	float nv = max( dot( n, toEye ), 1e-4 );

	// The light of the grid and of an entity is many lamps of the level taken together, from all around a direction,
	// not one point: its shine is as wide as on a surface of roughness 0.4 at the least (as Karis widens the lobe for
	// a lamp of a size), else a smooth bumpy floor glitters with highlights everywhere.
	vec3 color = s.albedo * ( 1.0 - metalness ) * ambient * occlusion;
	vec3 shine = SurfaceLight( s, l, directed, 0.4, color );
	if ( directed2 != vec3( 0.0 ) ) {
		shine += SurfaceLight( s, l2, directed2, 0.4, color );
	}
#ifndef VK_GLES
	shine += DebugLights( t, s, color );
#endif
	// v_pbr 2: albedo times the light alone (as without a material), 3: the shine alone, 4: the normal, 5: the share
	// and direction of the light of the grid, 6: roughness, metalness and occlusion, 7: has an rmo map (red), a normal
	// map (green), the grid (blue), 8: what the reflection probe shows as a mirror, 9: the albedo checked against what
	// real surfaces have (sRGB 30 to 240, metals 186 and more): too dark blue, too bright red, a dark metal magenta,
	// the rest grey, 10: roughness alone, 11: metalness alone
	// (v_pbr 1, as it is played: none of them)
	if ( d.pbr.w > 1.5 ) {
		if ( d.pbr.w == 9.0 ) {
			float lum = dot( base.rgb, vec3( 0.2126, 0.7152, 0.0722 ) ) * 255.0;
			if ( metalness >= 0.5 ) {
				return lum < 186.0 ? vec4( 1.0, 0.0, 1.0, alpha ) : vec4( vec3( lum / 255.0 ), alpha );
			}
			return lum < 30.0 ? vec4( 0.0, 0.2, 1.0, alpha ) : lum > 240.0 ? vec4( 1.0, 0.1, 0.0, alpha ) : vec4( vec3( lum / 255.0 ), alpha );
		} else if ( d.pbr.w == 10.0 ) {
			return vec4( vec3( roughness ), alpha );
		} else if ( d.pbr.w == 11.0 ) {
			return vec4( vec3( metalness ), alpha );
		} else if ( d.pbr.w == 2.0 ) {
			return vec4( pow( albedo * ( ambient + directed * max( dot( flat_, l ), 0.0 ) ), vec3( 1.0 / 2.2 ) ), alpha );
		} else if ( d.pbr.w == 3.0 ) {
			return vec4( pow( shine, vec3( 1.0 / 2.2 ) ), alpha );
		} else if ( d.pbr.w == 4.0 ) {
			return vec4( n * 0.5 + 0.5, alpha );
		} else if ( d.pbr.w == 8.0 && ( d.pbrFlags & PBR_PROBES ) != 0u ) {
			vec3 world = ( t.model * vec4( v_position, 1.0 ) ).xyz;
			return vec4( ProbesAround( d, world, normalize( mat3( t.model ) * reflect( -toEye, n ) ), 0.0 ), alpha );
		} else if ( d.pbr.w == 7.0 ) {
			return vec4( ( d.pbrFlags & PBR_RMO ) != 0u ? 1.0 : 0.0, ( d.pbrFlags & PBR_NORMAL ) != 0u ? 1.0 : 0.0, ( d.pbrFlags & PBR_GRID ) != 0u ? 1.0 : 0.0, alpha );
		} else if ( d.pbr.w == 6.0 ) {
			return vec4( roughness, metalness, occlusion, alpha );
		} else if ( d.pbr.w == 5.0 ) {
			return vec4( l * 0.5 + 0.5, alpha ) * length( directed ) / max( length( ambient + directed ), 1e-4 );
		}
	}

	// What it throws back of the light around it: what the nearest reflection probe sees in the direction of the
	// mirror (blurrier the rougher it is), scaled by how bright the lightmap is here against there is no telling,
	// so as it is. Without probes the light of the lightmap stands for the surroundings; that is far too bright at a
	// flat angle, where a floor would mirror the dark walls and ceilings around it, so the angle is not let below
	// 0.6 then (Fresnel stays at what it is from above). Anisotropy bends the mirror along the strokes (as Filament
	// does), the occlusion takes from it as far as it hides the mirror (Lagarde 2014), and with v_pbrEnergy the
	// diffuse light loses what the shine takes.
	vec3 envShine = EnvBRDF( s.f0, roughness, ( d.pbrFlags & PBR_PROBES ) != 0u ? nv : max( nv, 0.6 ) );
	if ( ( d.pbrFlags & PBR_ENERGY ) != 0u ) {
		color *= 1.0 - max( envShine.r, max( envShine.g, envShine.b ) );
	}
	color += shine;
	vec3 mirrorNormal = n;
	if ( abs( s.anisotropy ) > 0.01 ) {
		vec3 tangent = normalize( s.along - n * dot( s.along, n ) );
		vec3 strokes = s.anisotropy >= 0.0 ? cross( n, tangent ) : tangent;
		vec3 across = cross( strokes, toEye );
		mirrorNormal = normalize( mix( n, cross( across, strokes ), abs( s.anisotropy ) ) );
	}
	float specOcclusion = clamp( pow( nv + occlusion, exp2( -16.0 * roughness - 1.0 ) ) - 1.0 + occlusion, 0.0, 1.0 );
	float coatEnv = s.coat > 0.0 ? ( 0.04 + 0.96 * pow( 1.0 - max( dot( flat_, toEye ), 0.0 ), 5.0 ) ) * s.coat : 0.0;
	vec3 envColor;
	if ( ( d.pbrFlags & PBR_PROBES ) != 0u ) {
		vec3 world = ( t.model * vec4( v_position, 1.0 ) ).xyz;
		vec3 mirror = normalize( mat3( t.model ) * reflect( -toEye, mirrorNormal ) );
		ProbeSet probes = ProbesNear( d, world );
		vec3 around = pow( ProbesSee( d, probes, mirror, roughness * 5.0 ), vec3( 2.2 ) );
		// The probe saw the light where it stands, not here: a wall in the shade beside a bright opening mirrored the
		// bright opening as if it stood in it. So what it mirrors is scaled down to the light this surface gets (its
		// lightmap, the grid, its entity) as far as the probe, blurred round this normal, is brighter than that
		// (normalising the probe by the light of the place, as many engines do).
		vec3 seen = pow( ProbesSee( d, probes, normalize( mat3( t.model ) * n ), 5.0 ), vec3( 2.2 ) );
		float here = dot( ambient + directed * 0.5, vec3( 0.2126, 0.7152, 0.0722 ) );
		float there = dot( seen, vec3( 0.2126, 0.7152, 0.0722 ) );
		float match = clamp( here / max( there, 1e-3 ), 0.0, 1.0 );
		// The mirror direction may still see far more than the normal does (the sky at a flat angle, from a dark pit or
		// corridor), and in the colour of the probe, not of the lightmap: dark textures went grey-blue (QC ffa3 R1, R3).
		// So the mirrored light stays at 1.5 times the light of the place at most, and half its colour is the colour
		// of that light.
		around *= match;
		float aroundLum = dot( around, vec3( 0.2126, 0.7152, 0.0722 ) );
		around *= min( 1.0, 1.5 * here / max( aroundLum, 1e-4 ) );
		vec3 local = ambient + directed;
		float localLum = dot( local, vec3( 0.2126, 0.7152, 0.0722 ) );
		if ( localLum > 1e-4 ) {
			around = mix( around, local * ( dot( around, vec3( 0.2126, 0.7152, 0.0722 ) ) / localLum ), 0.5 );
		}
#ifndef VK_GLES
		// what the screen shows along the mirror, where it finds it, over what the probe gives (v_ssr)
		vec4 screen = ScreenReflection( world + normalize( mat3( t.model ) * flat_ ) * 2.0, mirror, roughness );
		if ( screen.a > 0.0 ) {
			around = mix( around, pow( screen.rgb, vec3( 2.2 ) ), screen.a );
		}
#endif
		envColor = around * specOcclusion * envShine * ( 1.0 - coatEnv );
		if ( s.coat > 0.0 ) {
			vec3 coatMirror = normalize( mat3( t.model ) * reflect( -toEye, flat_ ) );
			envColor += pow( ProbesSee( d, probes, coatMirror, s.coatRoughness * 5.0 ), vec3( 2.2 ) ) * match * coatEnv;
		}
	} else {
		vec3 around = ambient;
#ifndef VK_GLES
		vec3 world = ( t.model * vec4( v_position, 1.0 ) ).xyz;
		vec4 screen = ScreenReflection( world + normalize( mat3( t.model ) * flat_ ) * 2.0, normalize( mat3( t.model ) * reflect( -toEye, mirrorNormal ) ),
			roughness );
		if ( screen.a > 0.0 ) {
			around = mix( around, pow( screen.rgb, vec3( 2.2 ) ), screen.a );
		}
#endif
		envColor = around * specOcclusion * envShine * ( 1.0 - coatEnv ) + ambient * coatEnv;
	}
	// v_pbr 12: what it mirrors of the light around it, alone
	if ( d.pbr.w == 12.0 ) {
		return vec4( pow( envColor, vec3( 1.0 / 2.2 ) ), alpha );
	}
	color += envColor;
	// the sheen of cloth at a flat angle in the light around it
	color += s.sheen * ambient * pow( 1.0 - nv, 3.0 ) * 0.5;
	// emission: the parts of the texture brighter than the threshold give off their colour (as emissionGen would mask)
	if ( dot( d.pbr3.rgb, d.pbr3.rgb ) > 0.0 ) {
		float lum = dot( base.rgb, vec3( 0.2126, 0.7152, 0.0722 ) );
		color += albedo * clamp( ( lum - d.pbr3.w ) / 0.1, 0.0, 1.0 ) * d.pbr3.rgb;
	}
	return vec4( pow( color, vec3( 1.0 / 2.2 ) ), alpha );
}

// What goes into the frame: no NaN, no infinity, nothing below zero. In the 8 bit frame such a value was cut to
// black or white where it fell; in the 16 bit frame of v_hdr it stays and every blend and blur over it spreads it
// (white blotches over walls and the HUD, 2026-11-08).
vec4 SafeColor( vec4 c ) {
	if ( any( isnan( c ) ) || any( isinf( c ) ) ) {
		return vec4( 0.0 );
	}
	return clamp( c, vec4( 0.0 ), vec4( 64.0 ) );
}

#ifndef VK_GLES
// What blendFunc GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA has to be given for the surface to come out times f (each colour 0
// to 2): rgb + 1 - alpha = f, with rgb and alpha both 0 to 1.
vec4 TimesSurface( vec3 f ) {
	f = clamp( f, vec3( 0.0 ), vec3( 2.0 ) );
	float a = clamp( 1.5 - 0.5 * ( min( f.r, min( f.g, f.b ) ) + max( f.r, max( f.g, f.b ) ) ), 0.0, 1.0 );

	return vec4( clamp( f - 1.0 + a, vec3( 0.0 ), vec3( 1.0 ) ), a );
}

// GROOVE: a burn mark of blendFunc GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA (the surface times rgb + 1 - alpha) lit as a
// groove cut into the surface: as deep as it is dark, d.material.x units at the darkest. Its slopes come from how
// that depth changes to the next pixel, along the surface (the surface gradient of Mikkelsen, "Bump Mapping
// Unparametrized Surfaces on the GPU", 2010), and are lit by the light grid of the level against the flat surface:
// the side that faces the light brighter, the other darker. A poly of a mark has no normals: the face from the place.
vec4 Groove( Draw d, vec4 c ) {
	Transform t = TRANSFORMS( d.transform );
	vec3 p = v_position;
	vec3 dpdx = dFdx( p ), dpdy = dFdy( p );
	vec3 n = normalize( cross( dpdx, dpdy ) );
	if ( dot( n, t.viewOrigin.xyz - p ) < 0.0 ) {
		n = -n;
	}
	vec3 f = c.rgb + 1.0 - c.a;
	// the height: from its map (red, 0.5 the surface) where it has one, else as deep as it is dark
	float h = d.material.y > 0.5 ? ( texture( TEX2( d ), v_texCoord0 ).r - 0.5 ) * 2.0 * d.material.x
		: -( 1.0 - clamp( dot( f, vec3( 1.0 / 3.0 ) ), 0.0, 1.0 ) ) * d.material.x;
	vec3 r1 = cross( dpdy, n ), r2 = cross( n, dpdx );
	float det = dot( dpdx, r1 );
	vec3 slope = abs( det ) > 1e-12 ? ( dFdx( h ) * r1 + dFdy( h ) * r2 ) / det : vec3( 0.0 );
	// (a slope from neighbours on another sliver of the mark can be anything: a dent is never steeper than this)
	float steep = length( slope );
	if ( steep > 1.2 ) {
		slope *= 1.2 / steep;
	}
	vec3 bent = normalize( n - slope );

	vec4 grid = PbrGrid( d, ( t.model * vec4( p, 1.0 ) ).xyz );
	float k = min( length( grid.xyz ), 0.95 );
	if ( k < 0.001 ) {
		return c;
	}
	vec3 l = normalize( transpose( mat3( t.model ) ) * grid.xyz );
	float flatLit = max( 1.0 - k + k * max( dot( n, l ), 0.0 ), 0.2 );
	float bentLit = 1.0 - k + k * max( dot( bent, l ), 0.0 );
	return TimesSurface( f * clamp( bentLit / flatLit, 0.5, 1.4 ) );
}

#ifndef VK_GLES
// SOFT (v_softParticles, vk_shade.cpp VK_SoftDraw): a sprite fades over d.pbr.x units in front of the solid level
// behind it (its distance in tex5, vk_post.cpp VK_SoftDepth) and over the last units in front of the eye, toward what
// its blend leaves behind it (d.pbr.y: 0 alpha, 1 colour to black, 2 both, 3 colour to white). The fade is smoothed,
// so the edge where it meets a wall is a soft line, not a ramp that ends in a crease.
vec4 SoftFade( Draw d, vec4 color ) {
	float scene = texelFetch( TEX5( d ), ivec2( gl_FragCoord.xy ), 0 ).r;
	float f = smoothstep( 0.0, 1.0, clamp( ( scene - v_depth ) / max( d.pbr.x, 0.01 ), 0.0, 1.0 ) )
		* smoothstep( 4.0, 24.0, v_depth );
	uint mode = uint( d.pbr.y + 0.5 );

	if ( mode == 0u ) {
		color.a *= f;
	} else if ( mode == 1u ) {
		color.rgb *= f;
	} else if ( mode == 2u ) {
		color *= f;
	} else {
		color.rgb = mix( vec3( 1.0 ), color.rgb, f );
	}
	return color;
}
#endif

// the rows of DECALS from d.decals on (vk_shade.cpp VK_PaintMarks)
layout( set = 0, binding = 3, std430 ) readonly buffer Bones {
	vec4	bones[];
};

// DECALS: the marks on a tessellated surface, painted onto its bumps in two passes over the surface (depth EQUAL):
// d.parm.x 0 what they take from it and lay over it (the surface times the colour this gives, TimesSurface), 1 the
// light they add, dimmed by what lies over it. vk_shade.cpp VK_PaintMarks has the rows: the marks of a pixel come
// from a table, one bucket for each axis with marks, by the cell of d.parm.z units the pixel lies in across it. Each
// poly of a mark as it would have been drawn flat, seen along its normal: the poly the pixel lies in, its texture
// coordinates from its corners there, and for each stage of its shader (a style) the texture moved as the stage
// moves it, its colour as the stage makes it from the colour of the mark (baseColor + vertColor times it), put
// together as the blend of the stage says, by its kind: 0 the colour takes away, 1 times, 2 over by alpha, 3 added,
// 4 times rgb + 1 - alpha, 5 times twice the colour, 6 added by alpha. A groove (kind 4 with a depth) is lit as
// Groove() does, its height from its map or from its darkness. Further from the plane of a poly than the bumps
// reach (d.parm.y), nothing of it. A pixel with no mark is left alone. d.numDecals: the cluster of
// marks the pass is for + 1 (it has the scissor of that cluster), 0 all marks.
#define DECAL_STYLE_ROWS	6u		// VK_PAINT_STYLE_ROWS

vec4 Decals( Draw d ) {
	bool adding = d.parm.x > 0.5;
	vec3 p = v_position;
	// outside the box of the pass: nothing, at once
	if ( any( lessThan( p, d.material.xyz ) ) || any( greaterThan( p, d.material2.xyz ) ) ) {
		discard;
	}
	// (the gradients for the texture come from the place, outside the loops: inside them they are not defined)
	vec3 dpdx = dFdx( p ), dpdy = dFdy( p );
	// the grooves: the face of the surface and the light of the level there
	bool grooves = d.parm.w > 0.5 && !adding;
	vec3 face = vec3( 0.0, 0.0, 1.0 ), toLight = vec3( 0.0, 0.0, 1.0 ), r1 = vec3( 0.0 ), r2 = vec3( 0.0 );
	float lightShare = 0.0, flatLit = 1.0, faceDet = 0.0;
	if ( grooves ) {
		Transform t = TRANSFORMS( d.transform );
		face = normalize( cross( dpdx, dpdy ) );
		if ( dot( face, t.viewOrigin.xyz - p ) < 0.0 ) {
			face = -face;
		}
		vec4 grid = PbrGrid( d, p );
		lightShare = min( length( grid.xyz ), 0.95 );
		if ( lightShare > 0.001 ) {
			toLight = normalize( grid.xyz );
			flatLit = max( 1.0 - lightShare + lightShare * max( dot( face, toLight ), 0.0 ), 0.2 );
			r1 = cross( dpdy, face );
			r2 = cross( face, dpdx );
			faceDet = dot( dpdx, r1 );
		}
		grooves = lightShare > 0.001 && abs( faceDet ) > 1e-12;
	}
	// the surface times this, plus that
	vec3 times = vec3( 1.0 ), plus = vec3( 0.0 );
	bool taken = false;

	// how many buckets each axis has (a power of two, 0: no marks across it)
	vec4 sizes = bones[d.decals];
	uint base = 0u;

	for ( uint axis = 0u; axis < 3u; axis++ ) {
		uint size = uint( sizes[axis] + 0.5 );
		if ( size == 0u ) {
			continue;
		}
		vec2 across = ( axis == 0u ) ? p.yz : ( ( axis == 1u ) ? p.xz : p.xy );
		ivec2 cell = ivec2( floor( across / d.parm.z ) );
		uint bucket = base + ( ( uint( cell.x ) * 73856093u ^ uint( cell.y ) * 19349663u ) & ( size - 1u ) );
		vec4 pair = bones[d.decals + 1u + bucket / 2u];
		base += size;
		uint element = uint( ( ( bucket & 1u ) == 0u ? pair.x : pair.z ) + 0.5 );
		uint count = uint( ( ( bucket & 1u ) == 0u ? pair.y : pair.w ) + 0.5 );

		for ( uint i = 0u; i < count; i++, element++ ) {
			uint r = uint( bones[element / 4u][element % 4u] + 0.5 );
			vec4 U = bones[r], V = bones[r + 1u], box = bones[r + 6u];
			vec2 q = vec2( dot( U.xyz, p ) - U.w, dot( V.xyz, p ) - V.w );

			// (the box round its corners first: it turns away the neighbours in the cell and the marks of far cells
			// that share the bucket, with three rows)
			if ( any( lessThan( q, box.xy ) ) || any( greaterThan( q, box.zw ) ) ) {
				continue;
			}
			vec4 P = bones[r + 2u];
			if ( abs( dot( P.xyz, p ) - P.w ) > d.parm.y ) {
				continue;
			}
			vec4 info = bones[r + 5u];
			// (a pass for one cluster of marks paints those alone: the others have passes of their own)
			if ( d.numDecals != 0u && uint( info.w + 0.5 ) + 1u != d.numDecals ) {
				continue;
			}
			uint corners = uint( info.x + 0.5 );
			uint first = r + 7u;
			vec4 a = bones[first];
			vec2 st = vec2( 0.0 ), gx = vec2( 0.0 ), gy = vec2( 0.0 );
			bool inside = false;

			for ( uint k = 1u; k + 1u < corners && !inside; k++ ) {
				vec4 b = bones[first + k], c = bones[first + k + 1u];
				vec2 e1 = b.xy - a.xy, e2 = c.xy - a.xy, e = q - a.xy;
				float det = e1.x * e2.y - e1.y * e2.x;

				if ( abs( det ) < 1e-8 ) {
					continue;
				}
				// where q lies in the triangle a b c: q = a + x e1 + y e2
				float x = ( e.x * e2.y - e.y * e2.x ) / det;
				float y = ( e1.x * e.y - e1.y * e.x ) / det;
				if ( x < 0.0 || y < 0.0 || x + y > 1.0 ) {
					continue;
				}
				// the texture coordinates there, and how they change across the triangle for the mip level
				mat2 toXY = mat2( e2.y, -e1.y, -e2.x, e1.x ) / det;		// (du, dv) to (dx, dy)
				mat2 toST = mat2( b.zw - a.zw, c.zw - a.zw );
				st = a.zw + x * ( b.zw - a.zw ) + y * ( c.zw - a.zw );
				gx = toST * ( toXY * vec2( dot( U.xyz, dpdx ), dot( V.xyz, dpdx ) ) );
				gy = toST * ( toXY * vec2( dot( U.xyz, dpdy ), dot( V.xyz, dpdy ) ) );
				inside = true;
			}
			if ( !inside ) {
				continue;
			}
			vec4 markColor = bones[r + 3u];
			uint style = uint( info.y + 0.5 ), styles = uint( info.z + 0.5 );

			for ( uint s = 0u; s < styles; s++, style += DECAL_STYLE_ROWS ) {
				vec4 head = bones[style], m0 = bones[style + 3u], m1 = bones[style + 4u], turb = bones[style + 5u];
				uint kind = uint( head.y + 0.5 );
				bool adds = kind == 3u || kind == 6u;

				// (the pass of what is taken has no use for the light, but the other one needs all that lies over it)
				if ( adds && !adding ) {
					continue;
				}
				uint tex = uint( head.x + 0.5 );
				mat2 texLinear = mat2( m0.x, m1.x, m0.y, m1.y );
				vec2 texOffset = vec2( m0.z, m1.z );
				if ( turb != vec4( 0.0 ) ) {
					float sinS = sin( ( ( p.x + p.z ) * ( 0.125 / 128.0 ) + m0.w ) * 6.2831853 );
					float sinT = sin( ( p.y * ( 0.125 / 128.0 ) + m0.w ) * 6.2831853 );
					texOffset += vec2( turb.x * sinS + turb.y * sinT, turb.z * sinS + turb.w * sinT );
				}
				vec2 moved = texLinear * st + texOffset;
				vec4 col = textureGrad( u_textures[nonuniformEXT( tex )], moved, texLinear * gx, texLinear * gy )
					* ( bones[style + 1u] + bones[style + 2u] * markColor );

				if ( adds ) {
					plus += ( kind == 6u ) ? col.rgb * col.a : col.rgb;
					continue;
				}
				vec3 f;
				if ( kind == 0u ) {
					f = 1.0 - col.rgb;
				} else if ( kind == 1u ) {
					f = col.rgb;
				} else if ( kind == 5u ) {
					f = 2.0 * col.rgb;
				} else if ( kind == 2u ) {
					f = vec3( 1.0 - col.a );
				} else {
					f = col.rgb + 1.0 - col.a;
					if ( grooves && head.z > 0.0 ) {
						// the height here and one pixel over each way, the slope of the dent from them, its light
						// against the flat
						float h0, hx, hy;
						if ( head.w >= 0.0 ) {
							uint map = uint( head.w + 0.5 );
							h0 = textureGrad( u_textures[nonuniformEXT( map )], st, gx, gy ).r;
							hx = textureGrad( u_textures[nonuniformEXT( map )], st + gx, gx, gy ).r;
							hy = textureGrad( u_textures[nonuniformEXT( map )], st + gy, gx, gy ).r;
						} else {
							h0 = dot( textureGrad( u_textures[nonuniformEXT( tex )], moved, gx, gy ).rgb, vec3( 1.0 / 3.0 ) );
							hx = dot( textureGrad( u_textures[nonuniformEXT( tex )], moved + texLinear * gx, gx, gy ).rgb, vec3( 1.0 / 3.0 ) );
							hy = dot( textureGrad( u_textures[nonuniformEXT( tex )], moved + texLinear * gy, gx, gy ).rgb, vec3( 1.0 / 3.0 ) );
						}
						vec3 slope = ( ( hx - h0 ) * r1 + ( hy - h0 ) * r2 ) * ( 2.0 * head.z / faceDet );
						float steep = length( slope );
						if ( steep > 1.2 ) {
							slope *= 1.2 / steep;
						}
						vec3 bent = normalize( face - slope );
						f *= clamp( ( 1.0 - lightShare + lightShare * max( dot( bent, toLight ), 0.0 ) ) / flatLit, 0.5, 1.4 );
					}
				}
				times *= f;
				plus *= f;
				if ( kind == 2u ) {
					plus += col.rgb * col.a;
				}
				taken = true;
			}
		}
	}
	if ( adding ) {
		if ( plus == vec3( 0.0 ) ) {
			discard;
		}
		return vec4( plus, 1.0 );
	}
	if ( !taken ) {
		discard;
	}
	return TimesSurface( times );
}
#endif

void main() {
	Draw d = DRAWS( v_draw );

#ifdef VK_GLES
	// the plane of a portal or mirror, see stage.glsl
	if ( v_clip < 0.0 ) {
		discard;
	}
#endif
	if ( DLIGHT( d.flags ) ) {
		o_color = DynamicLight( d );
		return;
	}
#ifndef VK_GLES
	if ( DECALS( d.flags ) ) {
		o_color = SafeColor( Decals( d ) );
		return;
	}
#endif

	vec4 color;
	uint env = TEXENV( d.flags );
	if ( PBR( d.flags ) ) {
		// (the lightmap in the second texture is its light)
		color = PbrStage( d );
		env = 0u;
	} else {
		vec2 uv0 = ( TCGEN0( d.flags ) == TCGEN_REST ) ? v_screenCoord0 : v_texCoord0;
		if ( ( d.pbrFlags & PBR_FOLLOW ) != 0u ) {
			// (the mip level of the coordinates before the shift, which jumps from pixel to pixel at the edges of bumps)
			color = textureGrad( TEX0( d ), FollowParallax( d, uv0 ), dFdx( uv0 ), dFdy( uv0 ) ) * v_color;
		} else if ( IsLightmapGen( TCGEN0( d.flags ) ) ) {
			color = LightmapTexture( TEX0( d ), uv0 ) * v_color;
		} else {
			color = texture( TEX0( d ), uv0 ) * v_color;
		}
	}
	if ( MATERIAL( d.flags ) ) {
		color.rgb = MaterialLight( d, color.rgb );
	}

	// the second texture of a stage, combined the way glTexEnv did it
	if ( env != 0u ) {
		vec4 second = IsLightmapGen( TCGEN1( d.flags ) ) ? LightmapTexture( TEX1( d ), v_texCoord1 ) : texture( TEX1( d ), v_texCoord1 );

		if ( env == 1u ) {				// modulate
			color *= second;
		} else if ( env == 2u ) {		// add
			color = vec4( color.rgb + second.rgb, color.a * second.a );
		} else if ( env == 3u ) {		// decal
			color.rgb = mix( color.rgb, second.rgb, second.a );
		} else {						// replace
			color = second;
		}
	}

#ifndef VK_GLES
	if ( GROOVE( d.flags ) ) {
		color = Groove( d, color );
	}
#endif

	// the glow map of a lamp panel brighter than its texture (v_panelGlow): with HDR it glows out
	if ( d.brighten > 0.0 ) {
		color.rgb *= d.brighten;
	}

	uint atest = ATEST( d.flags );
	if ( atest == 1u && color.a <= 0.0 ) {
		discard;
	} else if ( atest == 2u && color.a >= 0.5 ) {
		discard;
	} else if ( atest == 3u && color.a < 0.5 ) {
		discard;
	} else if ( atest == 4u && color.a < 0.75 ) {
		discard;
	}

	// fog over everything as glFog did it: the color fades to the fog color with the distance, alpha stays
	uint fog = FOG( d.flags );
	if ( fog != 0u ) {
		float f;

		if ( fog == 1u ) {
			float x = d.fogParm.x * v_depth;
			f = exp( -x * x );
		} else {
			f = ( d.fogParm.y - v_depth ) / ( d.fogParm.y - d.fogParm.x );
		}
		color.rgb = mix( d.fogColor.rgb, color.rgb, clamp( f, 0.0, 1.0 ) );
	}
#ifndef VK_GLES
	if ( SOFT( d.flags ) ) {
		color = SoftFade( d, color );
	}
#endif
	o_color = SafeColor( color );
}
