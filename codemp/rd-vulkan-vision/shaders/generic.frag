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

// A stage of a model with a material (shader keywords normalMap, specular, wrapLight and sparkle): lit per pixel by
// the light of its entity as the vertices are lit otherwise (ambient, directed and its direction in model space),
// with a broad shine of the surface, a sharp one of a clear coat over it that is smooth over the bumps, and the
// light of the room the coat throws back at a flat angle (Fresnel with F0 0.04). Light that wraps into the shadow,
// as it scatters through snow (GPU Gems 1, chapter 16: N.L + wrap over 1 + wrap), with the cool of the sky where it
// does (as the snow of Batman: Arkham Origins), and the glints of its crystals.
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
	vec3 color = albedo * min( t.ambientLight.rgb + wrapped * t.directedLight.rgb, vec3( 1.0 ) );

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

// The reflection probe a cell of the light grid takes (R_CreateProbes): its number, read as it is from a pixel of the
// grid picture (the layout of PbrGrid).
float ProbeOfCell( Draw d, ivec3 g ) {
	ivec3 cells = ivec3( d.grid2.xyz );
	int tiles = int( d.grid0.w );
	g = clamp( g, ivec3( 0 ), cells - 1 );
	ivec2 texel = ivec2( ( g.z % tiles ) * cells.x + g.x, ( g.z / tiles ) * cells.y + g.y );
	return floor( texelFetch( TEX5( d ), texel, 0 ).r * 255.0 + 0.5 );
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

// What the probes around a point of the world see in a direction: the eight cells of the grid around it, each with
// the probe it takes, weighed as the light grid is (by how near the point is to each), so the mirror goes over from
// one probe to the next across a cell and not at its edge. Where all eight take the same probe it is read once.
vec3 ProbesAround( Draw d, vec3 world, vec3 dir, float level ) {
	vec3 g = ( world - d.grid0.xyz ) * d.grid1.xyz;
	vec3 base = floor( g ), f = g - base;
	float probes[8];
	bool same = true;
	for ( int i = 0; i < 8; i++ ) {
		probes[i] = ProbeOfCell( d, ivec3( base ) + ivec3( i & 1, ( i >> 1 ) & 1, ( i >> 2 ) & 1 ) );
		same = same && probes[i] == probes[0];
	}
	if ( same ) {
		return ProbeSample( d, probes[0], dir, level );
	}
	vec3 sum = vec3( 0.0 );
	for ( int i = 0; i < 8; i++ ) {
		vec3 w3 = mix( 1.0 - f, f, vec3( float( i & 1 ), float( ( i >> 1 ) & 1 ), float( ( i >> 2 ) & 1 ) ) );
		sum += ProbeSample( d, probes[i], dir, level ) * ( w3.x * w3.y * w3.z );
	}
	return sum;
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
vec2 Parallax( Draw d, vec2 uv, vec3 e, float depth, float plane ) {
	int STEPS = int( mix( 32.0, 8.0, clamp( e.z, 0.0, 1.0 ) ) + 0.5 );
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

// the point of the segment from a to b nearest p
vec3 NearestOnSegment( vec3 a, vec3 b, vec3 p ) {
	vec3 ab = b - a;
	return a + ab * clamp( dot( p - a, ab ) / max( dot( ab, ab ), 1e-4 ), 0.0, 1.0 );
}

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
   grid1.xyz reaching grid0.w from it (a saber blade, ext.AddLineLightToScene). Until 2026-11-08 this was rd-vanilla's
   round light image projected along the normal of the triangle (ProjectDlightTexture2): flat, no bumps, no shine.
   Now the light falls off smoothly to nothing at its reach and is brighter close by; the surface takes it by the
   angle it meets it, with the normal map and the shine of its material where the stage has one (VKD_PBR, the
   textures and numbers VK_PbrDraw gives a stage), else with a faint shine of its own. For a tube the diffuse light
   comes from the nearest point of the tube and the shine from the point nearest the mirrored ray, so the highlight on
   a floor runs along the blade. Worked out in linear light; the light colour and its fall in the light of the game
   as before. tex0: the texture of the surface (white where the frame already holds it: then the pass multiplies). */
vec4 DynamicLight( Draw d ) {
	Transform t = TRANSFORMS( d.transform );
	// The normal of the vertices, exact for the faces of the level. The one of the plane of the pixels, from how the
	// position changes from one pixel to the next, only where they have none.
	vec3 flat_ = dot( v_normal, v_normal ) > 0.25 ? normalize( v_normal ) : normalize( cross( dFdx( v_position ), dFdy( v_position ) ) );
	vec3 toEye = normalize( t.viewOrigin.xyz - v_position );
	if ( dot( flat_, toEye ) < 0.0 ) {
		flat_ = -flat_;		// the side of the triangle that is looked at
	}
	bool tube = d.grid0.w > 0.0;
	float range = tube ? d.grid0.w : d.parm.w;
	vec3 lightPos = tube ? NearestOnSegment( d.grid0.xyz, d.grid1.xyz, v_position ) : d.parm.xyz;
	vec3 toLight = lightPos - v_position;
	float dist = length( toLight );
	if ( dist >= range ) {
		discard;
	}
	// A light a little below the plane still lights it: the light of an explosion lies just under the floor it hit,
	// a blade on the floor lies in it. It is taken as standing a hand above the plane for the angle.
	float above = dot( flat_, toLight );
	if ( above <= -12.0 ) {
		discard;
	}
	vec3 l = normalize( toLight + flat_ * max( 16.0 - above, 0.0 ) );

	float x = dist / range;
	float window = clamp( 1.0 - x * x * x * x, 0.0, 1.0 );
	// (about what the round light image of rd-vanilla gave halfway out, brighter close by)
	float fall = window * window / ( 1.0 + 4.0 * x * x ) * 1.6;
	vec3 radiance = pow( d.baseColor.rgb * fall, vec3( 2.2 ) );
#ifndef VK_GLES
	// The shadows of the players from this light, made a frame ago into a mask (vk_rt.cpp, smlights.frag): grid1.w
	// says the channel, tex1 shows the mask; the point is looked up where it was in the view the mask was made in
	// (globals.rt 42-45, its rect 46).
	int channel = int( d.grid1.w + 0.5 );
	if ( channel > 0 ) {
		vec3 world = ( t.model * vec4( v_position, 1.0 ) ).xyz;
		vec4 clip = mat4( globals.rt[42], globals.rt[43], globals.rt[44], globals.rt[45] ) * vec4( world, 1.0 );
		if ( clip.w > 1.0 ) {
			vec2 ndc = clip.xy / clip.w;
			vec4 rect = globals.rt[46];
			vec2 pixel = vec2( rect.x + ( ndc.x * 0.5 + 0.5 ) * rect.z, rect.y + ( 0.5 - ndc.y * 0.5 ) * rect.w );
			vec4 mask = texture( TEX1( d ), pixel / vec2( textureSize( TEX1( d ), 0 ) ) );
			radiance *= 1.0 - ( channel == 1 ? mask.r : mask.g );
		}
	}
#endif

	vec2 uv = v_texCoord0;
	vec3 albedo = pow( texture( TEX0( d ), uv ).rgb, vec3( 2.2 ) );
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
		s.f0 = mix( vec3( d.material2.z > 0.0 ? d.material2.z : 0.04 ), albedo, s.metalness );
		s.coat = d.pbr2.y;
		s.coatRoughness = d.pbr2.z;
		s.along = cos( d.material2.x ) * frame[0] + sin( d.material2.x ) * frame[1];
		s.anisotropy = d.pbr2.w;
	}

	vec3 diffuse = vec3( 0.0 );
	vec3 shine;
	if ( tube ) {
		// the diffuse light from the nearest point, the shine from the one nearest the mirrored ray; a tube is no
		// point, its highlight is a little wider
		vec3 unused = vec3( 0.0 );
		vec3 r = reflect( -toEye, s.n );
		vec3 specPos = NearestToRay( d.grid0.xyz, d.grid1.xyz, v_position, r );
		vec3 ls = normalize( specPos - v_position + flat_ * max( 16.0 - dot( flat_, specPos - v_position ), 0.0 ) );
		SurfaceLight( s, l, radiance, 0.0, diffuse );
		shine = SurfaceLight( s, ls, radiance, 0.15, unused );
	} else {
		shine = SurfaceLight( s, l, radiance, 0.0, diffuse );
	}
	return vec4( pow( diffuse * occlusion + shine, vec3( 1.0 / 2.2 ) ), 1.0 );
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

// v_panelShine (globals.panels, vk_rt.cpp VK_RT_PickPanels): the shine of the panels that glow, the shine alone
// (what they give of even light is in the lightmap already). How much of each this point sees comes from the mask the
// ray tracing made in the frame before (shaders/rtpanels.frag), looked up where the point was in that view; a point
// outside that view gets nothing. A panel is a disk: its shine is the wider the larger it looks (as Karis widens the
// lobe for a lamp of a size). The light falls off with the square of the distance in hundreds of units, as the lamps
// of DebugLights. Lit in the space of the world.
vec3 PanelShine( Transform t, Surface s ) {
	if ( globals.panels[0].y < 0.5 ) {
		return vec3( 0.0 );
	}
	uint slot = uint( globals.panels[0].y - 0.5 );
	vec3 world = ( t.model * vec4( v_position, 1.0 ) ).xyz;
	vec4 clip = mat4( globals.panels[1], globals.panels[2], globals.panels[3], globals.panels[4] ) * vec4( world, 1.0 );
	if ( clip.w <= 1.0 ) {
		return vec3( 0.0 );
	}
	vec2 ndc = clip.xy / clip.w;
	if ( any( greaterThan( abs( ndc ), vec2( 1.0 ) ) ) ) {
		return vec3( 0.0 );
	}
	vec4 rect = globals.panels[5];
	ivec2 pixel = ivec2( rect.x + ( ndc.x * 0.5 + 0.5 ) * rect.z, rect.y + ( 0.5 - ndc.y * 0.5 ) * rect.w );
	vec4 seen = texelFetch( u_textures[nonuniformEXT( slot )], pixel, 0 );
	mat3 toWorld = mat3( t.model );
	Surface w = s;
	w.n = normalize( toWorld * s.n );
	w.flat_ = normalize( toWorld * s.flat_ );
	w.v = normalize( toWorld * s.v );
	w.along = toWorld * s.along;
	vec3 shine = vec3( 0.0 ), unused = vec3( 0.0 );

	for ( int k = 0; k < 4; k++ ) {
		vec3 colour = globals.panels[8 + k * 4].rgb;
		if ( seen[k] <= 0.0 || dot( colour, colour ) <= 0.0 ) {
			continue;
		}
		vec4 lamp = globals.panels[6 + k * 4];
		vec3 side = globals.panels[7 + k * 4].xyz;
		vec3 toLamp = lamp.xyz - world;
		float dist = length( toLamp );
		if ( dist <= lamp.w ) {
			continue;
		}
		vec3 l = toLamp / dist;
		float facing = dot( side, side ) > 0.5 ? max( dot( side, -l ), 0.0 ) : 1.0;
		float hundreds = dist * 0.01;
		float fall = 1.0 / max( hundreds * hundreds, 0.04 );
		shine += SurfaceLight( w, l, colour * ( globals.panels[0].z * fall * facing * seen[k] ), clamp( lamp.w / dist, 0.05, 0.4 ), unused );
	}
	return shine;
}

/* Glass (VK_GlassDraw, material keywords transmission, ior, thickness, absorption): the copy of the frame in tex1
   behind the surface. Where it shows it is bent: the ray from the eye through the surface (refract by ior) against
   the straight one, both carried thickness units on (a thin pane 4) and projected to the screen. It is blurred by the
   roughness (frosted), coloured by the absorption over the path through it (a thin pane by the colour alone) and
   by the texture as far as its alpha says. On top, what it mirrors of the probes by Fresnel (F0 from ior) and the
   shine of the lamps. pbr: roughness, -, ior, v_pbr; pbr2: transmission, thickness, absorption distance; material:
   the absorption colour. */
vec4 GlassStage( Draw d ) {
	const vec2 disk[12] = vec2[12]( vec2( -0.326, -0.406 ), vec2( -0.840, -0.074 ), vec2( -0.696, 0.457 ), vec2( -0.203, 0.621 ),
		vec2( 0.962, -0.195 ), vec2( 0.473, -0.480 ), vec2( 0.519, 0.767 ), vec2( 0.185, -0.893 ), vec2( 0.507, 0.064 ),
		vec2( 0.896, 0.412 ), vec2( -0.322, -0.933 ), vec2( -0.792, -0.598 ) );
	Transform t = TRANSFORMS( d.transform );
	vec3 toEye = normalize( t.viewOrigin.xyz - v_position );
	vec3 flat_ = normalize( v_normal );
	if ( dot( flat_, toEye ) < 0.0 ) {
		flat_ = -flat_;
	}
	vec3 n = flat_;
	if ( ( d.pbrFlags & PBR_NORMAL ) != 0u ) {
		mat3 frame = CotangentFrame( flat_, v_position, v_texCoord0 );
		n = normalize( frame * ( texture( TEX2( d ), v_texCoord0 ).xyz * 2.0 - 1.0 ) );
	}
	float roughness = d.pbr.x, ior = max( d.pbr.z, 1.0 ), transmission = d.pbr2.x, thickness = d.pbr2.y;
	float nv = max( dot( n, toEye ), 1e-4 );

	// the screen behind, bent
	float dist = thickness > 0.0 ? thickness : 4.0;
	vec3 bent = refract( -toEye, n, 1.0 / ior );
	if ( dot( bent, bent ) < 1e-6 ) {
		bent = -toEye;
	}
	vec4 straight = t.mvp * vec4( v_position - toEye * dist, 1.0 );
	vec4 through = t.mvp * vec4( v_position + bent * dist, 1.0 );
	// (the viewport turns the y of OpenGL: up on the screen is down in the frame)
	vec2 offset = ( through.xy / through.w - straight.xy / straight.w ) * vec2( 0.5, -0.5 );
	vec2 uv = gl_FragCoord.xy / globals.screen.xy + offset;
	vec3 back;
	float radius = roughness * roughness * 0.06;
	if ( radius < 0.0005 ) {
		back = texture( TEX1( d ), uv ).rgb;
	} else {
		vec2 aspect = vec2( globals.screen.y / globals.screen.x, 1.0 ) * radius;
		// the disk turned another way at each pixel (interleaved gradient noise, Jimenez 2014), so its twelve points
		// do not show as a pattern
		float turn = 6.2831853 * fract( 52.9829189 * fract( dot( gl_FragCoord.xy, vec2( 0.06711056, 0.00583715 ) ) ) );
		mat2 rotation = mat2( cos( turn ), sin( turn ), -sin( turn ), cos( turn ) );
		back = vec3( 0.0 );
		for ( int i = 0; i < 12; i++ ) {
			back += texture( TEX1( d ), uv + rotation * disk[i] * aspect ).rgb;
		}
		back /= 12.0;
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

	if ( r5.w < 0.5 || roughness > 0.6 ) {
		return vec4( 0.0 );
	}
	uint slot = uint( r5.w - 0.5 );
	mat4 view = mat4( globals.ssr[0], globals.ssr[1], globals.ssr[2], globals.ssr[3] );
	vec4 rect = globals.ssr[4];
	vec3 forward = globals.ssr[6].xyz;
	const int STEPS = 24;
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
		stepLength *= 1.25;
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
	vec3 flat_ = normalize( v_normal );
	if ( dot( flat_, toEye ) < 0.0 ) {
		flat_ = -flat_;		// the side of a two sided surface that is looked at
	}
	vec2 uv = v_texCoord0;
	mat3 frame = CotangentFrame( flat_, v_position, uv );

	// far off the parallax fades out (from 768 to 1536 units in front of the eye) and is not worked out at all
	// beyond, where its few pixels cost the loop for nothing
	float parallax = d.pbr.z * clamp( ( 1536.0 - v_depth ) / 768.0, 0.0, 1.0 );
	if ( ( d.pbrFlags & PBR_HEIGHT ) != 0u && parallax > 0.0 ) {
		uv = Parallax( d, uv, vec3( dot( frame[0], toEye ), dot( frame[1], toEye ), dot( flat_, toEye ) ), parallax, d.pbr2.x );
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

	vec3 albedo, ambient, directed, l;
	float alpha;
	if ( ( d.pbrFlags & PBR_ENTITY ) != 0u ) {
		albedo = pow( base.rgb * v_color.rgb, vec3( 2.2 ) );
		alpha = base.a * v_color.a;
		ambient = pow( t.ambientLight.rgb, vec3( 2.2 ) );
		directed = pow( t.directedLight.rgb, vec3( 2.2 ) );
		l = normalize( t.lightDir.xyz );
	} else {
		vec3 light = ( d.pbrFlags & PBR_LIGHTMAP ) != 0u ? texture( TEX1( d ), v_texCoord1 ).rgb * v_color.rgb : v_color.rgb;
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
	s.along = cos( d.material2.x ) * frame[0] + sin( d.material2.x ) * frame[1];
	s.anisotropy = d.pbr2.w;
	float nv = max( dot( n, toEye ), 1e-4 );

	// The light of the grid and of an entity is many lamps of the level taken together, from all around a direction,
	// not one point: its shine is as wide as on a surface of roughness 0.4 at the least (as Karis widens the lobe for
	// a lamp of a size), else a smooth bumpy floor glitters with highlights everywhere.
	vec3 color = s.albedo * ( 1.0 - metalness ) * ambient * occlusion;
	vec3 shine = SurfaceLight( s, l, directed, 0.4, color );
#ifndef VK_GLES
	shine += DebugLights( t, s, color );
	shine += PanelShine( t, s );
#endif
	// v_pbr 2: albedo times the light alone (as without a material), 3: the shine alone, 4: the normal, 5: the share
	// and direction of the light of the grid, 6: roughness, metalness and occlusion, 7: has an rmo map (red), a normal
	// map (green), the grid (blue), 8: what the reflection probe shows as a mirror, 9: the albedo checked against what
	// real surfaces have (sRGB 30 to 240, metals 186 and more): too dark blue, too bright red, a dark metal magenta,
	// the rest grey, 10: roughness alone, 11: metalness alone
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
		vec3 around = pow( ProbesAround( d, world, mirror, roughness * 5.0 ), vec3( 2.2 ) );
		// The probe saw the light where it stands, not here: a wall in the shade beside a bright opening mirrored the
		// bright opening as if it stood in it. So what it mirrors is scaled down to the light this surface gets (its
		// lightmap, the grid, its entity) as far as the probe, blurred round this normal, is brighter than that
		// (normalising the probe by the light of the place, as many engines do).
		vec3 seen = pow( ProbesAround( d, world, normalize( mat3( t.model ) * n ), 5.0 ), vec3( 2.2 ) );
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
			envColor += pow( ProbesAround( d, world, coatMirror, s.coatRoughness * 5.0 ), vec3( 2.2 ) ) * match * coatEnv;
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

void main() {
	Draw d = DRAWS( v_draw );

#ifdef VK_GLES
	// the plane of a portal or mirror, see stage.glsl
	if ( v_clip < 0.0 ) {
		discard;
	}
#endif
	if ( DLIGHT( d.flags ) ) {
		o_color = SafeColor( DynamicLight( d ) );
		return;
	}

	vec4 color;
	uint env = TEXENV( d.flags );
	if ( PBR( d.flags ) ) {
		// (the lightmap in the second texture is its light)
		color = PbrStage( d );
		env = 0u;
	} else {
		color = texture( TEX0( d ), ( TCGEN0( d.flags ) == TCGEN_REST ) ? v_screenCoord0 : v_texCoord0 ) * v_color;
	}
	if ( MATERIAL( d.flags ) ) {
		color.rgb = MaterialLight( d, color.rgb );
	}

	// the second texture of a stage, combined the way glTexEnv did it
	if ( env != 0u ) {
		vec4 second = texture( TEX1( d ), v_texCoord1 );

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
	o_color = SafeColor( color );
}
