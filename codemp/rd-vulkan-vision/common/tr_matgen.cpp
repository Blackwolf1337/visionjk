/*
===========================================================================
Copyright (C) 2026, VisionJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// tr_matgen.cpp: the maps of a material made from the texture of its stage when it loads (shader keywords
// albedoGen, normalGen, rmoGen of .oshader files, materialGen_t), so a material ships as text. The ways of making
// them are those of the material tool the website had (site/materials/js/images.js of its repository): albedo
// with the painted light taken out, normal map from the slopes of the brightness with the height in alpha, rmo
// map with roughness that varies with the texture and occlusion in the cracks. One difference: the made rmo map
// has the change of the roughness in red (0.5 none), not the roughness, so the roughness and metalness of the
// material stay live (generic.frag, PBR_RMO_RELATIVE).
//
// What is made is kept in vulkan/gen/ of the profile, by the file of the texture (its length and the checksum of
// its pak, as vk_texcache.cpp names them) and the settings; a texture of a directory is made every time.

#include "tr_local.h"

#define MATGEN_VERSION	2
#define MATGEN_MAGIC	0x314e4756		// "VGN1"
#define MATGEN_MOST		1024			// pixels each way at most, halved until it fits

typedef struct {
	int		width, height;
	byte	*data;						// RGBA, Z_Malloc
} genImage_t;

typedef struct {
	int		magic;
	int		width, height;
} genCacheHeader_t;

void R_DefaultMaterialGen( materialGen_t *gen ) {
	memset( gen, 0, sizeof( *gen ) );
	gen->tint[0] = gen->tint[1] = gen->tint[2] = 1.0f;
	gen->saturation = 1.0f;
	gen->normalStrength = 1.0f;
	gen->normalBlur = 1.0f;
	gen->normalDetail = 0.3f;
}

static byte *Gen_Alloc( int bytes ) {
	return (byte *)Z_Malloc( bytes, TAG_TEMP_WORKSPACE, qfalse );
}

// 0 to 1 into a byte, rounded as the Uint8ClampedArray of images.js does
static byte Gen_Byte( float v ) {
	return (byte)Com_Clampi( 0, 255, (int)floorf( v * 255.0f + 0.5f ) );
}

static float *Gen_Floats( int count ) {
	return (float *)Z_Malloc( count * (int)sizeof( float ), TAG_TEMP_WORKSPACE, qfalse );
}

/*
===============
The steps, as images.js has them
===============
*/

// halved until it is at most MATGEN_MOST each way, each pixel the mean of four
static void Gen_Capped( genImage_t *img ) {
	while ( img->width > MATGEN_MOST || img->height > MATGEN_MOST ) {
		const int w = Q_max( 1, img->width >> 1 ), h = Q_max( 1, img->height >> 1 );
		byte *out = Gen_Alloc( w * h * 4 );

		for ( int y = 0; y < h; y++ ) {
			for ( int x = 0; x < w; x++ ) {
				for ( int c = 0; c < 4; c++ ) {
					const int a = ( ( y * 2 ) * img->width + x * 2 ) * 4 + c, b = a + img->width * 4;

					out[( y * w + x ) * 4 + c] = (byte)( ( img->data[a] + img->data[a + 4] + img->data[b] + img->data[b + 4] ) >> 2 );
				}
			}
		}
		Z_Free( img->data );
		img->data = out;
		img->width = w;
		img->height = h;
	}
}

static float Gen_SrgbToLinear( float v ) {
	return v <= 0.04045f ? v / 12.92f : powf( ( v + 0.055f ) / 1.055f, 2.4f );
}

static float Gen_LinearToSrgb( float v ) {
	return v <= 0.0031308f ? v * 12.92f : 1.055f * powf( v, 1.0f / 2.4f ) - 0.055f;
}

// the brightness 0 to 1 of every pixel, of the sRGB values: what the eye sees as light and dark
static float *Gen_Luminance( const genImage_t *img ) {
	const int n = img->width * img->height;
	float *out = Gen_Floats( n );

	for ( int i = 0; i < n; i++ ) {
		out[i] = ( 0.2126f * img->data[i * 4] + 0.7152f * img->data[i * 4 + 1] + 0.0722f * img->data[i * 4 + 2] ) / 255.0f;
	}
	return out;
}

static int Gen_Wrap( int i, int len ) {
	return ( ( i % len ) + len ) % len;
}

// a box blur of radius r three times (close to a Gaussian) that wraps at the edges as a texture repeats; a copy
static float *Gen_Blur( const float *src, int w, int h, float radius ) {
	const int r = (int)floorf( radius + 0.5f );
	const int n = w * h;
	float *a = Gen_Floats( n );

	memcpy( a, src, sizeof( float ) * n );
	if ( r < 1 ) {
		return a;
	}

	float *b = Gen_Floats( n );
	const float k = 1.0f / ( 2 * r + 1 );

	for ( int pass = 0; pass < 3; pass++ ) {
		// along the rows, a into b
		for ( int y = 0; y < h; y++ ) {
			const float *row = a + y * w;
			float sum = 0.0f;

			for ( int i = -r; i <= r; i++ ) {
				sum += row[Gen_Wrap( i, w )];
			}
			for ( int x = 0; x < w; x++ ) {
				b[y * w + x] = sum * k;
				sum += row[Gen_Wrap( x + r + 1, w )] - row[Gen_Wrap( x - r, w )];
			}
		}
		// along the columns, b into a
		for ( int x = 0; x < w; x++ ) {
			float sum = 0.0f;

			for ( int i = -r; i <= r; i++ ) {
				sum += b[Gen_Wrap( i, h ) * w + x];
			}
			for ( int y = 0; y < h; y++ ) {
				a[y * w + x] = sum * k;
				sum += b[Gen_Wrap( y + r + 1, h ) * w + x] - b[Gen_Wrap( y - r, h ) * w + x];
			}
		}
	}
	Z_Free( b );
	return a;
}

// whether the albedo of these settings is another picture than the texture
static qboolean Gen_AlbedoChanges( const materialGen_t *g ) {
	return (qboolean)( ( g->flags & MATGEN_ALBEDO ) && ( g->tint[0] != 1.0f || g->tint[1] != 1.0f || g->tint[2] != 1.0f
		|| g->delight != 0.0f || g->saturation != 1.0f ) );
}

// The albedo: the texture with its painted light taken out as far as delight says (divided by its own blur at a
// large radius, so broad shading and baked highlights even out and the detail stays), tinted and its saturation
// changed, in linear light. qfalse when there is nothing to change.
static qboolean Gen_Albedo( const genImage_t *base, const materialGen_t *g, genImage_t *out ) {
	const int w = base->width, h = base->height, n = w * h;

	if ( !Gen_AlbedoChanges( g ) ) {
		return qfalse;
	}
	float *lum = Gen_Luminance( base );
	float *broad = g->delight ? Gen_Blur( lum, w, h, Q_max( w, h ) / 16.0f ) : NULL;
	double mean = 0.0;

	for ( int i = 0; i < n; i++ ) {
		mean += lum[i];
	}
	mean /= n;

	out->width = w;
	out->height = h;
	out->data = Gen_Alloc( n * 4 );
	for ( int i = 0; i < n; i++ ) {
		const float k = g->delight ? 1.0f + g->delight * ( (float)mean / Q_max( broad[i], 0.02f ) - 1.0f ) : 1.0f;
		float rgb[3];

		for ( int c = 0; c < 3; c++ ) {
			rgb[c] = Gen_SrgbToLinear( base->data[i * 4 + c] / 255.0f ) * k * g->tint[c];
		}
		const float l = 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];

		for ( int c = 0; c < 3; c++ ) {
			const float v = Gen_LinearToSrgb( Q_max( 0.0f, l + ( rgb[c] - l ) * g->saturation ) );

			out->data[i * 4 + c] = Gen_Byte( v );
		}
		out->data[i * 4 + 3] = base->data[i * 4 + 3];
	}
	Z_Free( lum );
	if ( broad ) {
		Z_Free( broad );
	}
	return qtrue;
}

// The normal map: the slopes (Sobel) of the height times the strength, in the frame of the texture as
// generic.frag CotangentFrame reads it, n = normalize( -dh/ds, -dh/dt, 1 ) stored as n * 0.5 + 0.5. The height (the
// brightness smoothed by blur with the fine detail mixed in, bright stands out unless invert) is in alpha.
static void Gen_Normal( const genImage_t *albedo, const materialGen_t *g, genImage_t *out ) {
	const int w = albedo->width, h = albedo->height, n = w * h;
	float *lum = Gen_Luminance( albedo );
	float *smooth = Gen_Blur( lum, w, h, g->normalBlur );
	float *height = Gen_Floats( n );
	const float s = g->normalStrength * Q_max( w, h ) / 64.0f;
	float lo = 1e30f, hi = -1e30f;

	for ( int i = 0; i < n; i++ ) {
		height[i] = ( smooth[i] * ( 1.0f - g->normalDetail ) + lum[i] * g->normalDetail ) * ( g->normalInvert ? -1.0f : 1.0f );
		lo = Q_min( lo, height[i] );
		hi = Q_max( hi, height[i] );
	}

	out->width = w;
	out->height = h;
	out->data = Gen_Alloc( n * 4 );
#define H( x_, y_ )	height[Gen_Wrap( y_, h ) * w + Gen_Wrap( x_, w )]
	for ( int y = 0; y < h; y++ ) {
		for ( int x = 0; x < w; x++ ) {
			const float dx = ( H( x + 1, y - 1 ) + 2.0f * H( x + 1, y ) + H( x + 1, y + 1 ) - H( x - 1, y - 1 ) - 2.0f * H( x - 1, y ) - H( x - 1, y + 1 ) ) / 8.0f;
			const float dy = ( H( x - 1, y + 1 ) + 2.0f * H( x, y + 1 ) + H( x + 1, y + 1 ) - H( x - 1, y - 1 ) - 2.0f * H( x, y - 1 ) - H( x + 1, y - 1 ) ) / 8.0f;
			const float nx = -dx * s, ny = -dy * s, nz = 1.0f;
			const float l = sqrtf( nx * nx + ny * ny + nz * nz );
			byte *o = out->data + ( y * w + x ) * 4;

			o[0] = Gen_Byte( nx / l * 0.5f + 0.5f );
			o[1] = Gen_Byte( ny / l * 0.5f + 0.5f );
			o[2] = Gen_Byte( nz / l * 0.5f + 0.5f );
			o[3] = hi > lo ? Gen_Byte( ( H( x, y ) - lo ) / ( hi - lo ) ) : 255;
		}
	}
#undef H
	Z_Free( lum );
	Z_Free( smooth );
	Z_Free( height );
}

// The rmo map: in red the change of the roughness as variation says (darker cracks and dirt rougher, bright smooth
// parts less), -1 to 1 stored as 0 to 1; green unused; in blue the occlusion of what lies lower than its surroundings,
// from a little smoothed brightness so it darkens the cracks and not every dark speck.
static void Gen_Rmo( const genImage_t *albedo, const materialGen_t *g, genImage_t *out ) {
	const int w = albedo->width, h = albedo->height, n = w * h;
	float *lum = Gen_Luminance( albedo );
	float *around = Gen_Blur( lum, w, h, Q_max( w, h ) / 32.0f );
	float *soft = Gen_Blur( lum, w, h, Q_max( 1.0f, Q_max( w, h ) / 256.0f ) );

	out->width = w;
	out->height = h;
	out->data = Gen_Alloc( n * 4 );
	for ( int i = 0; i < n; i++ ) {
		const float d = lum[i] - around[i], cavity = Q_max( around[i] - soft[i], 0.0f );
		const float change = Com_Clamp( -1.0f, 1.0f, -d * g->rmoVariation );
		const float occlusion = powf( Com_Clamp( 0.0f, 1.0f, 1.0f - cavity * g->rmoOcclusion * 3.0f ), 1.5f );

		out->data[i * 4] = Gen_Byte( change * 0.5f + 0.5f );
		out->data[i * 4 + 1] = 0;
		out->data[i * 4 + 2] = Gen_Byte( occlusion );
		out->data[i * 4 + 3] = 255;
	}
	Z_Free( lum );
	Z_Free( around );
	Z_Free( soft );
}

/*
===============
The cache
===============
*/

// FNV-1a, 64 bit
static unsigned long long Gen_Hash( const char *text ) {
	unsigned long long hash = 14695981039346656037ULL;

	for ( ; *text; text++ ) {
		hash = ( hash ^ (byte)*text ) * 1099511628211ULL;
	}
	return hash;
}

// the file in the cache for this map of this texture with these settings, qfalse when it is not kept (a texture of
// a directory)
static qboolean Gen_CachePath( const image_t *base, char kind, const materialGen_t *g, char *path, int pathSize ) {
	char file[MAX_QPATH], key[512];
	int length = 0, checksum = 0;

	if ( !VK_ImageFileOf( base->imgName, file, sizeof( file ), &length, &checksum ) ) {
		return qfalse;
	}
	// what each map depends on: the albedo also feeds the normal and rmo maps
	const qboolean albedo = Gen_AlbedoChanges( g );

	Com_sprintf( key, sizeof( key ), "%s %i %08x v%i m%i k%c a%i %g %g %g %g %g n%g %g %g %i r%g %g", file, length, (unsigned)checksum,
		MATGEN_VERSION, MATGEN_MOST, kind, albedo, g->tint[0], g->tint[1], g->tint[2], g->delight, g->saturation,
		kind == 'n' ? g->normalStrength : 0.0f, kind == 'n' ? g->normalBlur : 0.0f, kind == 'n' ? g->normalDetail : 0.0f,
		kind == 'n' ? g->normalInvert : 0, kind == 'r' ? g->rmoVariation : 0.0f, kind == 'r' ? g->rmoOcclusion : 0.0f );
	Com_sprintf( path, pathSize, "vulkan/gen/%016llx.dat", Gen_Hash( key ) );
	return qtrue;
}

static qboolean Gen_CacheRead( const char *path, genImage_t *out ) {
	void *buffer = NULL;
	const long length = ri.FS_ReadFile( path, &buffer );
	qboolean ok = qfalse;

	if ( length > (long)sizeof( genCacheHeader_t ) && buffer ) {
		const genCacheHeader_t *header = (const genCacheHeader_t *)buffer;

		if ( header->magic == MATGEN_MAGIC && header->width > 0 && header->height > 0 && header->width <= MATGEN_MOST
			&& header->height <= MATGEN_MOST && length == (long)sizeof( *header ) + header->width * header->height * 4 ) {
			out->width = header->width;
			out->height = header->height;
			out->data = Gen_Alloc( out->width * out->height * 4 );
			memcpy( out->data, header + 1, (size_t)out->width * out->height * 4 );
			ok = qtrue;
		}
	}
	if ( buffer ) {
		ri.FS_FreeFile( buffer );
	}
	return ok;
}

static void Gen_CacheWrite( const char *path, const genImage_t *img ) {
	const int size = (int)sizeof( genCacheHeader_t ) + img->width * img->height * 4;
	byte *buffer = Gen_Alloc( size );
	genCacheHeader_t *header = (genCacheHeader_t *)buffer;

	header->magic = MATGEN_MAGIC;
	header->width = img->width;
	header->height = img->height;
	memcpy( header + 1, img->data, (size_t)img->width * img->height * 4 );
	ri.FS_WriteFile( path, buffer, size );
	Z_Free( buffer );
}

/*
===============
The images of a record
===============
*/

// the image of the record for this kind of map: made the first time, its pixels replaced after (also when an image
// of that name is left from an earlier level); the pixels are used up
static image_t *Gen_Image( const stageMaterial_t *mat, char kind, genImage_t *img ) {
	char name[MAX_QPATH];
	image_t *image;

	Com_sprintf( name, sizeof( name ), "*gen%c%d_%08x", kind, mat->sourceStage, (unsigned)( Gen_Hash( mat->shaderName ) & 0xffffffffu ) );
	image = R_MadeImage( name, img->data, img->width, img->height );
	Z_Free( img->data );
	img->data = NULL;
	return image;
}

static qboolean Gen_IsMade( const image_t *image ) {
	return (qboolean)( image && !Q_strncmp( image->imgName, "*gen", 4 ) );
}

/*
===============
R_MakeMaterialMaps

The maps of mat.gen from base (the texture of the stage), into the images of the record. What is not asked for
any more is taken off when it was made. Read from the cache where it is kept, else made from the file of the
texture and kept.
===============
*/
void R_MakeMaterialMaps( stageMaterial_t *mat, image_t *base ) {
	static const struct { int flag; char kind; } kinds[] = { { MATGEN_ALBEDO, 'a' }, { MATGEN_NORMAL, 'n' }, { MATGEN_RMO, 'r' } };
	const materialGen_t *g = &mat->gen;
	genImage_t made[3];
	char paths[3][MAX_QPATH];
	qboolean kept[3], missing = qfalse;

	mat->genDone = true;
	if ( base ) {
		mat->baseImage = base;
	}
	base = mat->baseImage;

	// what is not asked for any more
	if ( !( g->flags & MATGEN_ALBEDO ) ) {
		mat->albedoMap = NULL;
	}
	if ( !( g->flags & MATGEN_NORMAL ) && Gen_IsMade( mat->normalMap ) ) {
		mat->normalMap = NULL;
		mat->normalHeight = false;
	}
	if ( !( g->flags & MATGEN_RMO ) && Gen_IsMade( mat->rmoMap ) ) {
		mat->rmoMap = NULL;
		mat->rmoRelative = false;
	}
	if ( !g->flags || !base ) {
		return;
	}

	memset( made, 0, sizeof( made ) );
	for ( int i = 0; i < 3; i++ ) {
		kept[i] = qfalse;
		if ( !( g->flags & kinds[i].flag ) || ( kinds[i].flag == MATGEN_ALBEDO && !Gen_AlbedoChanges( g ) ) ) {
			continue;
		}
		kept[i] = Gen_CachePath( base, kinds[i].kind, g, paths[i], sizeof( paths[i] ) );
		if ( !kept[i] || !Gen_CacheRead( paths[i], &made[i] ) ) {
			missing = qtrue;
		}
	}

	if ( missing ) {
		genImage_t src, albedo;
		byte *pic = NULL;
		int width = 0, height = 0;

		R_LoadImage( base->imgName, &pic, &width, &height );
		if ( !pic ) {
			ri.Printf( PRINT_WARNING, "WARNING: the maps of %s can not be made, its texture %s could not be read\n", mat->shaderName, base->imgName );
			for ( int i = 0; i < 3; i++ ) {
				if ( made[i].data ) {
					Z_Free( made[i].data );
				}
			}
			return;
		}
		src.width = width;
		src.height = height;
		src.data = pic;
		Gen_Capped( &src );
		if ( ( src.width & ( src.width - 1 ) ) || ( src.height & ( src.height - 1 ) ) ) {
			ri.Printf( PRINT_WARNING, "WARNING: the maps of %s are not made, its texture is %i x %i, not a power of two\n", mat->shaderName, src.width, src.height );
			Z_Free( src.data );
			for ( int i = 0; i < 3; i++ ) {
				if ( made[i].data ) {
					Z_Free( made[i].data );
				}
			}
			return;
		}
		memset( &albedo, 0, sizeof( albedo ) );
		const qboolean hasAlbedo = Gen_Albedo( &src, g, &albedo );
		const genImage_t *from = hasAlbedo ? &albedo : &src;

		if ( ( g->flags & MATGEN_NORMAL ) && !made[1].data ) {
			Gen_Normal( from, g, &made[1] );
		}
		if ( ( g->flags & MATGEN_RMO ) && !made[2].data ) {
			Gen_Rmo( from, g, &made[2] );
		}
		if ( hasAlbedo && !made[0].data ) {
			made[0] = albedo;
			albedo.data = NULL;
		}
		if ( albedo.data ) {
			Z_Free( albedo.data );
		}
		Z_Free( src.data );
		for ( int i = 0; i < 3; i++ ) {
			if ( made[i].data && kept[i] ) {
				Gen_CacheWrite( paths[i], &made[i] );
			}
		}
	}

	mat->albedoMap = made[0].data ? Gen_Image( mat, 'a', &made[0] ) : NULL;
	if ( made[1].data ) {
		mat->normalMap = Gen_Image( mat, 'n', &made[1] );
		mat->normalHeight = true;
	}
	if ( made[2].data ) {
		mat->rmoMap = Gen_Image( mat, 'r', &made[2] );
		mat->ormOrder = false;
		mat->rmoRelative = true;
	}
}
