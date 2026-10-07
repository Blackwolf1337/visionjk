/*
===========================================================================
Copyright (C) 2026, OpenJK contributors
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

// vk_bc.cpp: textures packed into BC1 and BC3 blocks, the formats OpenGL calls S3TC DXT1 and DXT5
//
// rd-vanilla names a compressed format when it hands a texture to OpenGL and the driver packs it. Vulkan takes
// the blocks as they lie in memory, so they are made here. A block is 4 x 4 pixels: two colors of 5:6:5 bits
// and for every pixel one of four points on the line between them (8 bytes). BC3 has 8 more bytes in front of
// that for alpha: two values of 8 bits and for every pixel one of eight points between them.
//
// Packing is work for every pixel of a level load (about 100 million on mp/ffa3). The large levels of a texture
// are cut into bands of rows that a few worker threads take, the caller with them, and the caller goes on when
// all bands are done. The workers do nothing else and sleep between two textures.

#include "vk_local.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#define VK_BC_MAX_WORKERS	7
#define VK_BC_BAND			8			// rows of blocks a thread takes at a time
#define VK_BC_SHARED_BLOCKS	4096		// a level with fewer blocks is packed by the caller alone

static struct vkBcPool_s {
	std::thread				threads[VK_BC_MAX_WORKERS];
	int						numThreads;
	std::mutex				mutex;
	std::condition_variable	wake;			// for the workers: there is a job, or quit
	std::condition_variable	done;			// for the caller: the last worker is through with the job
	bool					quit;
	int						job;			// counts up, so that every worker takes a job once
	int						working;		// workers that are not through with the job yet

	VkFormat				format;
	const byte				*rgba;
	int						width, height;
	byte					*out;
	std::atomic<int>		nextRow;		// in blocks: the first row nobody has taken yet
} vk_bcPool;

// For a block of one color: the pair of 5 (or 6) bit values whose point a third of the way from the first to
// the second is closest to an 8 bit value
static byte	vk_bcMatch5[256][2];
static byte	vk_bcMatch6[256][2];

static int VK_BC_Expand5( int c ) {
	return ( c << 3 ) | ( c >> 2 );
}

static int VK_BC_Expand6( int c ) {
	return ( c << 2 ) | ( c >> 4 );
}

static void VK_BC_BuildMatch( byte ( *table )[2], int levels, int ( *expand )( int ) ) {
	for ( int value = 0; value < 256; value++ ) {
		int best = INT_MAX;

		for ( int a = 0; a < levels; a++ ) {
			for ( int b = 0; b < levels; b++ ) {
				const int ea = expand( a ), eb = expand( b );
				// a device may round the point a little differently, the more the further the ends are apart
				const int err = abs( ( 2 * ea + eb + 1 ) / 3 - value ) * 100 + abs( ea - eb ) * 3;

				if ( err < best ) {
					best = err;
					table[value][0] = (byte)a;
					table[value][1] = (byte)b;
				}
			}
		}
	}
}

static void VK_BC_Worker( void );

void VK_BC_Init( void ) {
	const int cores = (int)std::thread::hardware_concurrency();

	VK_BC_BuildMatch( vk_bcMatch5, 32, VK_BC_Expand5 );
	VK_BC_BuildMatch( vk_bcMatch6, 64, VK_BC_Expand6 );

	vk_bcPool.quit = false;
	vk_bcPool.numThreads = 0;
	for ( int i = 0; i < VK_BC_MAX_WORKERS && i < cores - 1; i++ ) {
		vk_bcPool.threads[vk_bcPool.numThreads++] = std::thread( VK_BC_Worker );
	}
}

void VK_BC_Shutdown( void ) {
	{
		std::lock_guard<std::mutex> lock( vk_bcPool.mutex );

		vk_bcPool.quit = true;
	}
	vk_bcPool.wake.notify_all();
	for ( int i = 0; i < vk_bcPool.numThreads; i++ ) {
		vk_bcPool.threads[i].join();
	}
	vk_bcPool.numThreads = 0;
}

VkDeviceSize VK_BC_Size( VkFormat format, int width, int height ) {
	return (VkDeviceSize)( ( width + 3 ) / 4 ) * ( ( height + 3 ) / 4 ) * ( format == VK_FORMAT_BC3_UNORM_BLOCK ? 16 : 8 );
}

// the vector functions of the engine are for floats, most of this is whole numbers
#define VK_BC_DOT( a, b )	( ( a )[0] * ( b )[0] + ( a )[1] * ( b )[1] + ( a )[2] * ( b )[2] )

static unsigned VK_BC_Pack565( int r, int g, int b ) {
	return (unsigned)( ( r << 11 ) | ( g << 5 ) | b );
}

static unsigned VK_BC_Quantize( float r, float g, float b ) {
	return VK_BC_Pack565( (int)( r * ( 31.0f / 255.0f ) + 0.5f ), (int)( g * ( 63.0f / 255.0f ) + 0.5f ),
		(int)( b * ( 31.0f / 255.0f ) + 0.5f ) );
}

static void VK_BC_Expand565( unsigned c, int *rgb ) {
	rgb[0] = VK_BC_Expand5( (int)( c >> 11 ) );
	rgb[1] = VK_BC_Expand6( (int)( ( c >> 5 ) & 63 ) );
	rgb[2] = VK_BC_Expand5( (int)( c & 31 ) );
}

// The index of a pixel: 0 the first color, 1 the second, 2 a third of the way from the first, 3 two thirds.
// The two colors are compared as numbers: BC1 has these four only with the larger one first, else index 3 is
// black. With two equal colors index 2 is that color in both cases.
static void VK_BC_WriteColors( byte *out, unsigned c0, unsigned c1, uint32_t indexes ) {
	if ( c0 < c1 ) {
		const unsigned swap = c0;

		c0 = c1;
		c1 = swap;
		indexes ^= 0x55555555u;		// 0 and 1 change places, 2 and 3 too
	}
	out[0] = (byte)c0;
	out[1] = (byte)( c0 >> 8 );
	out[2] = (byte)c1;
	out[3] = (byte)( c1 >> 8 );
	out[4] = (byte)indexes;
	out[5] = (byte)( indexes >> 8 );
	out[6] = (byte)( indexes >> 16 );
	out[7] = (byte)( indexes >> 24 );
}

static void VK_BC_SolidColors( byte *out, int r, int g, int b ) {
	VK_BC_WriteColors( out,
		VK_BC_Pack565( vk_bcMatch5[r][0], vk_bcMatch6[g][0], vk_bcMatch5[b][0] ),
		VK_BC_Pack565( vk_bcMatch5[r][1], vk_bcMatch6[g][1], vk_bcMatch5[b][1] ), 0xaaaaaaaau );
}

// for every pixel the point of the line from c1 to c0 it is closest to, c0 != c1
static uint32_t VK_BC_MatchColors( const byte *block, unsigned c0, unsigned c1 ) {
	int p0[3], p1[3], dir[3];
	uint32_t indexes = 0;

	VK_BC_Expand565( c0, p0 );
	VK_BC_Expand565( c1, p1 );
	for ( int c = 0; c < 3; c++ ) {
		dir[c] = p0[c] - p1[c];
	}
	const int base = VK_BC_DOT( p1, dir );
	const int len = VK_BC_DOT( dir, dir );

	for ( int i = 15; i >= 0; i-- ) {
		const byte *px = block + i * 4;
		// how far along the line, in sixths of its length: the four points are at 0, 2, 4 and 6
		const int t = 6 * ( VK_BC_DOT( px, dir ) - base );

		indexes <<= 2;
		if ( t < len ) {
			indexes |= 1;
		} else if ( t < 3 * len ) {
			indexes |= 3;
		} else if ( t < 5 * len ) {
			indexes |= 2;
		}
	}
	return indexes;
}

// the two colors that fit the pixels best when the indexes stay as they are (least squares)
static qboolean VK_BC_RefineColors( const byte *block, uint32_t indexes, unsigned *c0, unsigned *c1 ) {
	static const int thirds[4] = { 3, 0, 2, 1 };		// how much of the first color an index is
	int xx = 0, xy = 0, yy = 0;
	int at0[3] = { 0, 0, 0 }, at1[3] = { 0, 0, 0 };
	float e0[3], e1[3];

	for ( int i = 0; i < 16; i++, indexes >>= 2 ) {
		const byte *px = block + i * 4;
		const int w = thirds[indexes & 3], v = 3 - w;

		xx += w * w;
		xy += w * v;
		yy += v * v;
		for ( int c = 0; c < 3; c++ ) {
			at0[c] += w * px[c];
			at1[c] += v * px[c];
		}
	}
	const int det = xx * yy - xy * xy;
	if ( !det ) {
		return qfalse;		// all pixels at one point of the line
	}
	const float scale = 3.0f / det;
	for ( int c = 0; c < 3; c++ ) {
		e0[c] = Com_Clamp( 0.0f, 255.0f, ( at0[c] * yy - at1[c] * xy ) * scale );
		e1[c] = Com_Clamp( 0.0f, 255.0f, ( at1[c] * xx - at0[c] * xy ) * scale );
	}
	*c0 = VK_BC_Quantize( e0[0], e0[1], e0[2] );
	*c1 = VK_BC_Quantize( e1[0], e1[1], e1[2] );
	return qtrue;
}

/*
===============
VK_BC_ColorBlock

block: 16 pixels of 4 bytes, out: 8 bytes
===============
*/
static void VK_BC_ColorBlock( const byte *block, byte *out ) {
	int min[3] = { 255, 255, 255 }, max[3] = { 0, 0, 0 }, sum[3] = { 0, 0, 0 };
	float mean[3], cov[6] = { 0, 0, 0, 0, 0, 0 }, axis[3];
	int low = 0, high = 0;
	float lowDot = FLT_MAX, highDot = -FLT_MAX;
	unsigned c0, c1, r0, r1;
	uint32_t indexes;

	for ( int i = 0; i < 16; i++ ) {
		const byte *px = block + i * 4;

		for ( int c = 0; c < 3; c++ ) {
			if ( px[c] < min[c] ) {
				min[c] = px[c];
			}
			if ( px[c] > max[c] ) {
				max[c] = px[c];
			}
			sum[c] += px[c];
		}
	}
	if ( min[0] == max[0] && min[1] == max[1] && min[2] == max[2] ) {
		VK_BC_SolidColors( out, min[0], min[1], min[2] );
		return;
	}

	// The line the colors of the block lie along is the direction they vary most in: power iteration on their
	// covariance, started with the row of the channel that varies most
	for ( int c = 0; c < 3; c++ ) {
		mean[c] = sum[c] * ( 1.0f / 16.0f );
	}
	for ( int i = 0; i < 16; i++ ) {
		const byte *px = block + i * 4;
		const float d[3] = { px[0] - mean[0], px[1] - mean[1], px[2] - mean[2] };

		cov[0] += d[0] * d[0];
		cov[1] += d[0] * d[1];
		cov[2] += d[0] * d[2];
		cov[3] += d[1] * d[1];
		cov[4] += d[1] * d[2];
		cov[5] += d[2] * d[2];
	}
	if ( cov[0] >= cov[3] && cov[0] >= cov[5] ) {
		axis[0] = cov[0], axis[1] = cov[1], axis[2] = cov[2];
	} else if ( cov[3] >= cov[5] ) {
		axis[0] = cov[1], axis[1] = cov[3], axis[2] = cov[4];
	} else {
		axis[0] = cov[2], axis[1] = cov[4], axis[2] = cov[5];
	}
	for ( int i = 0; i < 3; i++ ) {
		const float r = axis[0] * cov[0] + axis[1] * cov[1] + axis[2] * cov[2];
		const float g = axis[0] * cov[1] + axis[1] * cov[3] + axis[2] * cov[4];
		const float b = axis[0] * cov[2] + axis[1] * cov[4] + axis[2] * cov[5];
		// cov is up to 16 * 255 * 255, so the length is brought back each time
		const float longest = Q_max( fabsf( r ), Q_max( fabsf( g ), fabsf( b ) ) );

		if ( longest < 1.0f ) {
			break;
		}
		axis[0] = r / longest, axis[1] = g / longest, axis[2] = b / longest;
	}

	// the two pixels furthest apart along it are the first guess
	for ( int i = 0; i < 16; i++ ) {
		const byte *px = block + i * 4;
		const float dot = VK_BC_DOT( px, axis );

		if ( dot < lowDot ) {
			lowDot = dot;
			low = i;
		}
		if ( dot > highDot ) {
			highDot = dot;
			high = i;
		}
	}
	c0 = VK_BC_Quantize( block[high * 4], block[high * 4 + 1], block[high * 4 + 2] );
	c1 = VK_BC_Quantize( block[low * 4], block[low * 4 + 1], block[low * 4 + 2] );

	if ( c0 != c1 ) {
		indexes = VK_BC_MatchColors( block, c0, c1 );
		if ( VK_BC_RefineColors( block, indexes, &r0, &r1 ) && ( r0 != c0 || r1 != c1 ) ) {
			c0 = r0;
			c1 = r1;
			if ( c0 != c1 ) {
				indexes = VK_BC_MatchColors( block, c0, c1 );
			}
		}
	}
	if ( c0 == c1 ) {
		// colors too close to tell apart with 5:6:5 bits
		VK_BC_SolidColors( out, (int)( mean[0] + 0.5f ), (int)( mean[1] + 0.5f ), (int)( mean[2] + 0.5f ) );
		return;
	}
	VK_BC_WriteColors( out, c0, c1, indexes );
}

/*
===============
VK_BC_AlphaBlock

block: 16 pixels of 4 bytes, out: 8 bytes: two values and 16 indexes of 3 bits. With the larger value first the
indexes 2 to 7 are six points between the two. Else 2 to 5 are four points between them, 6 is 0 and 7 is 255.
===============
*/
static uint64_t VK_BC_MatchAlpha( const byte *block, int a0, int a1, int *error ) {
	int values[8];
	uint64_t indexes = 0;

	values[0] = a0;
	values[1] = a1;
	if ( a0 > a1 ) {
		for ( int i = 1; i < 7; i++ ) {
			values[1 + i] = ( ( 7 - i ) * a0 + i * a1 + 3 ) / 7;
		}
	} else {
		for ( int i = 1; i < 5; i++ ) {
			values[1 + i] = ( ( 5 - i ) * a0 + i * a1 + 2 ) / 5;
		}
		values[6] = 0;
		values[7] = 255;
	}
	*error = 0;
	for ( int i = 15; i >= 0; i-- ) {
		const int a = block[i * 4 + 3];
		int best = 0, bestDiff = abs( a - values[0] );

		for ( int n = 1; n < 8; n++ ) {
			const int diff = abs( a - values[n] );

			if ( diff < bestDiff ) {
				bestDiff = diff;
				best = n;
			}
		}
		*error += bestDiff * bestDiff;
		indexes = ( indexes << 3 ) | (uint64_t)best;
	}
	return indexes;
}

static void VK_BC_AlphaBlock( const byte *block, byte *out ) {
	int min = 255, max = 0, innerMin = 255, innerMax = 0;
	int a0, a1, error, innerError;
	uint64_t indexes;

	for ( int i = 0; i < 16; i++ ) {
		const int a = block[i * 4 + 3];

		if ( a < min ) {
			min = a;
		}
		if ( a > max ) {
			max = a;
		}
		if ( a != 0 && a != 255 ) {
			if ( a < innerMin ) {
				innerMin = a;
			}
			if ( a > innerMax ) {
				innerMax = a;
			}
		}
	}
	a0 = max;
	a1 = min;
	indexes = VK_BC_MatchAlpha( block, a0, a1, &error );
	if ( error && ( min == 0 || max == 255 ) && innerMin <= innerMax ) {
		// the edge of a letter or of a hole: 0 and 255 as they are, the points for what lies between
		const uint64_t inner = VK_BC_MatchAlpha( block, innerMin, innerMax, &innerError );

		if ( innerError < error ) {
			a0 = innerMin;
			a1 = innerMax;
			indexes = inner;
		}
	}
	out[0] = (byte)a0;
	out[1] = (byte)a1;
	for ( int i = 0; i < 6; i++ ) {
		out[2 + i] = (byte)( indexes >> ( 8 * i ) );
	}
}

// rows of blocks from firstRow up to endRow
static void VK_BC_CompressRows( VkFormat format, const byte *rgba, int width, int height, byte *out, int firstRow, int endRow ) {
	const qboolean alpha = (qboolean)( format == VK_FORMAT_BC3_UNORM_BLOCK );
	byte block[64];

	out += (size_t)firstRow * ( ( width + 3 ) / 4 ) * ( alpha ? 16 : 8 );
	for ( int y = firstRow * 4; y < endRow * 4; y += 4 ) {
		for ( int x = 0; x < width; x += 4 ) {
			if ( x + 4 <= width && y + 4 <= height ) {
				for ( int row = 0; row < 4; row++ ) {
					memcpy( block + row * 16, rgba + ( (size_t)( y + row ) * width + x ) * 4, 16 );
				}
			} else {
				// a level smaller than a block: its pixels fill the block again and again, so each counts the same
				for ( int row = 0; row < 4; row++ ) {
					for ( int col = 0; col < 4; col++ ) {
						memcpy( block + ( row * 4 + col ) * 4,
							rgba + ( (size_t)( ( y + row ) % height ) * width + ( x + col ) % width ) * 4, 4 );
					}
				}
			}
			if ( alpha ) {
				VK_BC_AlphaBlock( block, out );
				out += 8;
			}
			VK_BC_ColorBlock( block, out );
			out += 8;
		}
	}
}

// takes bands of the job until none is left
static void VK_BC_TakeBands( void ) {
	const int rows = ( vk_bcPool.height + 3 ) / 4;

	for ( ;; ) {
		const int first = vk_bcPool.nextRow.fetch_add( VK_BC_BAND );

		if ( first >= rows ) {
			return;
		}
		VK_BC_CompressRows( vk_bcPool.format, vk_bcPool.rgba, vk_bcPool.width, vk_bcPool.height, vk_bcPool.out, first,
			Q_min( first + VK_BC_BAND, rows ) );
	}
}

static void VK_BC_Worker( void ) {
	int taken = 0;

	for ( ;; ) {
		{
			std::unique_lock<std::mutex> lock( vk_bcPool.mutex );

			while ( !vk_bcPool.quit && vk_bcPool.job == taken ) {
				vk_bcPool.wake.wait( lock );
			}
			if ( vk_bcPool.quit ) {
				return;
			}
			taken = vk_bcPool.job;
		}
		VK_BC_TakeBands();
		{
			std::lock_guard<std::mutex> lock( vk_bcPool.mutex );

			if ( --vk_bcPool.working == 0 ) {
				vk_bcPool.done.notify_one();
			}
		}
	}
}

/*
===============
VK_BC_Compress

One level of a texture, 32 bit RGBA, into VK_BC_Size() bytes at out
===============
*/
void VK_BC_Compress( VkFormat format, const byte *rgba, int width, int height, byte *out ) {
	const int rows = ( height + 3 ) / 4;

	if ( !vk_bcPool.numThreads || rows * ( ( width + 3 ) / 4 ) < VK_BC_SHARED_BLOCKS ) {
		VK_BC_CompressRows( format, rgba, width, height, out, 0, rows );
		return;
	}
	{
		std::lock_guard<std::mutex> lock( vk_bcPool.mutex );

		vk_bcPool.format = format;
		vk_bcPool.rgba = rgba;
		vk_bcPool.width = width;
		vk_bcPool.height = height;
		vk_bcPool.out = out;
		vk_bcPool.nextRow = 0;
		vk_bcPool.working = vk_bcPool.numThreads;
		vk_bcPool.job++;
	}
	vk_bcPool.wake.notify_all();
	VK_BC_TakeBands();
	{
		// the bands are short, so this is over when the last worker has put down the one it holds
		std::unique_lock<std::mutex> lock( vk_bcPool.mutex );

		while ( vk_bcPool.working ) {
			vk_bcPool.done.wait( lock );
		}
	}
}
