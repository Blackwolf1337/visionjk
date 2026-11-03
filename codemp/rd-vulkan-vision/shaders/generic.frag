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
#define TEX0( draw_ )	u_tex0
#define TEX1( draw_ )	u_tex1
#define TEX2( draw_ )	u_tex2
#define TEX3( draw_ )	u_tex3
#define TEX4( draw_ )	u_tex4
#else
BINDING( 1, 0 ) uniform sampler2D u_textures[];
#define TEX0( draw_ )	u_textures[nonuniformEXT( draw_.tex0 )]
#define TEX1( draw_ )	u_textures[nonuniformEXT( draw_.tex1 )]
#define TEX2( draw_ )	u_textures[nonuniformEXT( draw_.tex2 )]
#define TEX3( draw_ )	u_textures[nonuniformEXT( draw_.tex3 )]
#define TEX4( draw_ )	u_textures[nonuniformEXT( draw_.tex4 )]
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

// One dynamic light on what is already drawn there, added by a pass of its own. rd-vanilla projects the round
// light image onto every triangle along its normal and fades it with the distance of the light from the plane
// of the triangle (tr_shade.cpp ProjectDlightTexture2). The same per pixel: tex0 is the texture of the surface,
// tex1 the light image.
vec4 DynamicLight( Draw d ) {
	// The normal of the vertices, exact for the faces of the level. The one of the plane of the pixels, from how the
	// position changes from one pixel to the next, only where they have none: far from the middle of a level and
	// on a floor seen flat that one is rough.
	vec3 normal = dot( v_normal, v_normal ) > 0.25 ? normalize( v_normal ) : normalize( cross( dFdx( v_position ), dFdy( v_position ) ) );
	vec3 toLight = d.parm.xyz - v_position;
	float radius = d.parm.w;

	// the side of the triangle that is looked at
	if ( dot( normal, TRANSFORMS( d.transform ).viewOrigin.xyz - v_position ) < 0.0 ) {
		normal = -normal;
	}
	// A light a little below the plane still lights it: the light of an explosion lies just under the floor it hit,
	// and two faces of a map a hair apart (which never show that they fight over the depth) would light up in rows
	// where the lower one wins, as on mp/ffa4.
	float fac = dot( normal, toLight );
	if ( fac <= -12.0 || fac >= radius ) {
		discard;
	}
	fac = max( abs( fac ), 0.001 );
	float modulate = 1.0 - ( fac * fac ) / ( radius * radius );

	// where the pixel is in the plane, seen from the point under the light. The image is round, any axes do.
	vec3 inPlane = normal * fac - toLight;
	vec3 e1 = normalize( abs( normal.z ) < 0.9 ? cross( normal, vec3( 0.0, 0.0, 1.0 ) ) : cross( normal, vec3( 1.0, 0.0, 0.0 ) ) );
	vec3 e2 = cross( normal, e1 );
	vec2 st = vec2( dot( inPlane, e1 ), dot( inPlane, e2 ) ) * ( 0.5 / sqrt( radius * radius - fac * fac ) ) + 0.5;
	if ( any( lessThan( st, vec2( 0.0 ) ) ) || any( greaterThan( st, vec2( 1.0 ) ) ) ) {
		discard;
	}
	vec3 light = texture( TEX1( d ), st ).rgb * d.baseColor.rgb * modulate;
	return vec4( texture( TEX0( d ), v_texCoord0 ).rgb * light, 1.0 );
}

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

float D_GGX( float nh, float a ) {
	float a2 = a * a;
	float k = nh * nh * ( a2 - 1.0 ) + 1.0;
	return a2 / ( 3.14159265 * k * k );
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
vec4 PbrStage( Draw d ) {
	Transform t = TRANSFORMS( d.transform );
	vec3 toEye = normalize( t.viewOrigin.xyz - v_position );
	vec3 flat_ = normalize( v_normal );
	if ( dot( flat_, toEye ) < 0.0 ) {
		flat_ = -flat_;		// the side of a two sided surface that is looked at
	}
	vec2 uv = v_texCoord0;
	mat3 frame = CotangentFrame( flat_, v_position, uv );

	if ( ( d.pbrFlags & PBR_HEIGHT ) != 0u && d.pbr.z > 0.0 ) {
		// parallax: the texture moved along the view by how high the surface is there (one step)
		vec3 e = vec3( dot( frame[0], toEye ), dot( frame[1], toEye ), dot( flat_, toEye ) );
		float h = texture( TEX2( d ), uv ).a - 0.5;
		uv += e.xy / max( e.z, 0.25 ) * h * d.pbr.z;
	}

	vec4 base = texture( TEX0( d ), uv );
	vec3 n = flat_;
	if ( ( d.pbrFlags & PBR_NORMAL ) != 0u ) {
		n = normalize( frame * ( texture( TEX2( d ), uv ).xyz * 2.0 - 1.0 ) );
	}
	vec3 rmo = vec3( d.pbr.x, d.pbr.y, 1.0 );
	if ( ( d.pbrFlags & PBR_RMO ) != 0u ) {
		rmo = texture( TEX3( d ), uv ).rgb;
		if ( ( d.pbrFlags & PBR_ORM ) != 0u ) {
			rmo = rmo.gbr;
		}
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
		float flatLit = max( 1.0 - k + k * max( dot( flat_, l ), 0.0 ), 0.2 );
		ambient = light * ( 1.0 - k ) / flatLit;
		directed = light * k / flatLit;
	}

	vec3 h = normalize( l + toEye );
	float nl = max( dot( n, l ), 0.0 ), nv = max( dot( n, toEye ), 1e-4 ), nh = max( dot( n, h ), 0.0 ), vh = max( dot( toEye, h ), 0.0 );
	// The light of the grid and of an entity is many lamps of the level taken together, from all around a direction,
	// not one point: its shine is as wide as on a surface of roughness 0.4 at the least (as Karis widens the lobe for
	// a lamp of a size), else a smooth bumpy floor glitters with highlights everywhere.
	float a = max( roughness, 0.4 ) * max( roughness, 0.4 ), kv = a * 0.5;
	vec3 f0 = mix( vec3( 0.04 ), albedo, metalness );
	vec3 fresnel = f0 + ( 1.0 - f0 ) * pow( 1.0 - vh, 5.0 );
	float visibility = 0.25 / ( ( nl * ( 1.0 - kv ) + kv ) * ( nv * ( 1.0 - kv ) + kv ) );

	vec3 color = albedo * ( 1.0 - metalness ) * ( ambient * occlusion + directed * nl );
	vec3 shine = directed * D_GGX( nh, a ) * visibility * fresnel * nl * step( 0.0001, dot( flat_, l ) );
	// v_pbr 2: albedo times the light alone (as without a material), 3: the shine alone, 4: the normal, 5: the share
	// and direction of the light of the grid, 6: roughness, metalness and occlusion, 7: has an rmo map (red), a normal
	// map (green), the grid (blue)
	if ( d.pbr.w == 2.0 ) {
		return vec4( pow( albedo * ( ambient + directed * max( dot( flat_, l ), 0.0 ) ), vec3( 1.0 / 2.2 ) ), alpha );
	} else if ( d.pbr.w == 3.0 ) {
		return vec4( pow( shine, vec3( 1.0 / 2.2 ) ), alpha );
	} else if ( d.pbr.w == 4.0 ) {
		return vec4( n * 0.5 + 0.5, alpha );
	} else if ( d.pbr.w == 7.0 ) {
		return vec4( ( d.pbrFlags & PBR_RMO ) != 0u ? 1.0 : 0.0, ( d.pbrFlags & PBR_NORMAL ) != 0u ? 1.0 : 0.0, ( d.pbrFlags & PBR_GRID ) != 0u ? 1.0 : 0.0, alpha );
	} else if ( d.pbr.w == 6.0 ) {
		return vec4( roughness, metalness, occlusion, alpha );
	} else if ( d.pbr.w == 5.0 ) {
		return vec4( l * 0.5 + 0.5, alpha ) * length( directed ) / max( length( ambient + directed ), 1e-4 );
	}
	color += shine;
	// What it throws back of the light around it: the level has no pictures of its surroundings, so the light of
	// the lightmap stands for them. That is far too bright at a flat angle, where a floor would mirror the dark
	// walls and ceilings around it, so the angle is not let below 0.6 (Fresnel stays at what it is from above).
	color += ambient * occlusion * EnvBRDF( f0, roughness, max( nv, 0.6 ) );
	return vec4( pow( color, vec3( 1.0 / 2.2 ) ), alpha );
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
		o_color = DynamicLight( d );
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
	o_color = color;
}
