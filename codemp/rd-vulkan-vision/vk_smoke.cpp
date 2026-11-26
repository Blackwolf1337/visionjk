/*
===========================================================================
Copyright (C) 2026, VisionJK contributors

This file is part of the VisionJK source code.

VisionJK is free software; you can redistribute it and/or modify it
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

// vk_smoke.cpp -- smoke as a volume (ext.SetSmoke, cgame/level/cg_smoke.c). The client game says where smoke is, as
// blobs, and where something tore a hole into it; shaders/smoke.frag marches the ray of each pixel through them, up
// to the depth of the scene, into a picture at half the size of the frame (vk.volume: at the full size it cost up
// to 4 ms of a frame and its steps showed as grain), and shaders/smokeup.frag lays that over the solid level. Sprites of smoke are flat pictures that turn with the eye; this has
// depth, a player stands in it and it ends at a wall. Needs the depth of the scene as a texture (vk.depthReadable).

#include "vk_local.h"

static const uint32_t vk_smokeFrag[] =
#include "shaders/smoke_frag.inc"
;
static const uint32_t vk_smokeUpFrag[] =
#include "shaders/smokeup_frag.inc"
;

#define VK_SMOKE_SCALE		2		// the smoke is marched at this part of the size of the frame (vk.volume)
#define SMOKE_NOISE_SIZE	64		// texels a side of the noise of the billows

static struct {
	VkShaderModule	shader, upShader;
	VkPipeline		pipeline;		// the rays through the smoke, into vk.volume
	VkPipeline		upPipeline;		// that over the level
	smokeVolume_t	volume;			// what ext.SetSmoke gave
	int				frame;			// tr.frameCount of it: drawn in that frame alone
	VkImage			noise;			// the noise of the billows, a volume that repeats (Smoke_CreateNoise)
	VkImageView		noiseView;
	vkAlloc_t		noiseAlloc;
	VkSampler		noiseSampler;
	VkDescriptorSet	noiseSet;
} sm;

/*
===============================================================================

THE NOISE OF THE BILLOWS

shaders/smoke.frag moves the rim of the smoke in and out with a noise, at every step of every ray. Worked out there it
was six gradient noises a step, 48 gradients. Here the same noises are worked out once, into a volume that repeats,
and a step reads three texels: red the billows (three octaves that repeat after 4, 8 and 16 cells, weighed as Fbm3 of
noise.glsl), green the lumps (8 cells), blue and alpha the two noises that bend the place the billows are read at
(8 cells). (Not what the pass cost most, that were its loops over the blobs, see smoke.frag: with the noise alone
from here the pass was no faster.)

===============================================================================
*/

#define NOISE_LAYERS	6

static const int	noisePeriods[NOISE_LAYERS] = { 4, 8, 16, 8, 8, 8 };
static float		noiseGradients[4 * 4 * 4 + 8 * 8 * 8 * 4 + 16 * 16 * 16][3];

// the gradients at the corners of the cells of a layer: any, each part from -1 to 1 as Gradient3 of noise.glsl
static void Smoke_NoiseGradients( float ( *gradients )[3], int period, uint32_t seed ) {
	uint32_t state = seed * 747796405u + 2891336453u;

	for ( int i = 0; i < period * period * period; i++ ) {
		for ( int c = 0; c < 3; c++ ) {
			// (PCG)
			state = state * 747796405u + 2891336453u;
			const uint32_t word = ( ( state >> ( ( state >> 28 ) + 4 ) ) ^ state ) * 277803737u;

			gradients[i][c] = ( ( ( word >> 22 ) ^ word ) & 0xffff ) / 32767.5f - 1.0f;
		}
	}
}

// Noise3 of noise.glsl with cells that repeat after period: about 0 to 1
static float Smoke_Noise( const float ( *gradients )[3], int period, float x, float y, float z ) {
	const int	ix = (int)x, iy = (int)y, iz = (int)z;
	const float	f[3] = { x - ix, y - iy, z - iz };
	float		u[3], corner[8];

	for ( int c = 0; c < 3; c++ ) {
		u[c] = f[c] * f[c] * f[c] * ( f[c] * ( f[c] * 6.0f - 15.0f ) + 10.0f );
	}
	for ( int c = 0; c < 8; c++ ) {
		const int	cx = c & 1, cy = ( c >> 1 ) & 1, cz = c >> 2;
		const float	*g = gradients[( ( iz + cz ) % period * period + ( iy + cy ) % period ) * period + ( ix + cx ) % period];

		corner[c] = g[0] * ( f[0] - cx ) + g[1] * ( f[1] - cy ) + g[2] * ( f[2] - cz );
	}
	const float	low = corner[0] + ( corner[1] - corner[0] ) * u[0] + ( ( corner[2] + ( corner[3] - corner[2] ) * u[0] ) - ( corner[0] + ( corner[1] - corner[0] ) * u[0] ) ) * u[1];
	const float	high = corner[4] + ( corner[5] - corner[4] ) * u[0] + ( ( corner[6] + ( corner[7] - corner[6] ) * u[0] ) - ( corner[4] + ( corner[5] - corner[4] ) * u[0] ) ) * u[1];

	return 0.5f + 0.5f * ( low + ( high - low ) * u[2] );
}

static void Smoke_CreateNoise( void ) {
	const int				size = SMOKE_NOISE_SIZE;
	VkImageCreateInfo		info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	VkImageViewCreateInfo	view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	VkSamplerCreateInfo		sampler = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	VkDescriptorSetAllocateInfo	alloc = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	VkMemoryRequirements	req;
	VkBufferImageCopy		region;
	const float				( *layers[NOISE_LAYERS] )[3];
	vkSize_t				staged;

	info.imageType = VK_IMAGE_TYPE_3D;
	info.format = VK_FORMAT_R8G8B8A8_UNORM;
	info.extent.width = info.extent.height = info.extent.depth = (uint32_t)size;
	info.mipLevels = 1;
	info.arrayLayers = 1;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK( qvkCreateImage( vk.device, &info, NULL, &sm.noise ) );
	qvkGetImageMemoryRequirements( vk.device, sm.noise, &req );
	VK_AllocMemory( &req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &sm.noiseAlloc );
	VK_CHECK( qvkBindImageMemory( vk.device, sm.noise, VK_MemoryOf( &sm.noiseAlloc ), sm.noiseAlloc.offset ) );

	view.image = sm.noise;
	view.viewType = VK_IMAGE_VIEW_TYPE_3D;
	view.format = info.format;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = 1;
	view.subresourceRange.layerCount = 1;
	VK_CHECK( qvkCreateImageView( vk.device, &view, NULL, &sm.noiseView ) );

	sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
	sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	VK_CHECK( qvkCreateSampler( vk.device, &sampler, NULL, &sm.noiseSampler ) );

	// its texels
	{
		float ( *gradients )[3] = noiseGradients;

		for ( int l = 0; l < NOISE_LAYERS; l++ ) {
			Smoke_NoiseGradients( gradients, noisePeriods[l], 31u + (uint32_t)l * 977u );
			layers[l] = gradients;
			gradients += noisePeriods[l] * noisePeriods[l] * noisePeriods[l];
		}
	}
	byte *texels = VK_Stage( (vkSize_t)size * size * size * 4, &staged );

	for ( int z = 0; z < size; z++ ) {
		for ( int y = 0; y < size; y++ ) {
			for ( int x = 0; x < size; x++ ) {
				float value[NOISE_LAYERS];

				// (the middle of a texel is where the lookup has its value)
				for ( int l = 0; l < NOISE_LAYERS; l++ ) {
					const float cells = (float)noisePeriods[l] / size;

					value[l] = Smoke_Noise( layers[l], noisePeriods[l], ( x + 0.5f ) * cells, ( y + 0.5f ) * cells, ( z + 0.5f ) * cells );
				}
				texels[0] = (byte)( Com_Clamp( 0.0f, 1.0f, value[0] * 0.5f + value[1] * 0.3f + value[2] * 0.2f ) * 255.0f + 0.5f );
				texels[1] = (byte)( Com_Clamp( 0.0f, 1.0f, value[3] ) * 255.0f + 0.5f );
				texels[2] = (byte)( Com_Clamp( 0.0f, 1.0f, value[4] ) * 255.0f + 0.5f );
				texels[3] = (byte)( Com_Clamp( 0.0f, 1.0f, value[5] ) * 255.0f + 0.5f );
				texels += 4;
			}
		}
	}
	// (VK_Stage() may have flushed: the commands after it)
	VkCommandBuffer cmd = VK_UploadCmd();

	VK_SetLayout( cmd, sm.noise, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
	memset( &region, 0, sizeof( region ) );
	region.bufferOffset = staged;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent = info.extent;
	qvkCmdCopyBufferToImage( cmd, vk.staging.buffer, sm.noise, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );
	VK_SetLayout( cmd, sm.noise, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );

	alloc.descriptorPool = vk.descriptorPool;
	alloc.descriptorSetCount = 1;
	alloc.pSetLayouts = &vk.postSetLayout;
	VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &sm.noiseSet ) );
	VK_WriteSet( sm.noiseSet, sm.noiseSampler, sm.noiseView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
}

void VK_SmokeInit( void ) {
	memset( &sm, 0, sizeof( sm ) );
	sm.frame = -1;
	if ( !vk.depthReadable ) {
		return;
	}
	Smoke_CreateNoise();
	sm.shader = VK_CreateShader( vk_smokeFrag, sizeof( vk_smokeFrag ) );
	sm.pipeline = VK_CreatePipeline( vk.postVertexShader, sm.shader, 0, 7, VKP_GENERIC );
	// (what the picture holds is the smoke on the way and how much of the frame it hides)
	sm.upShader = VK_CreateShader( vk_smokeUpFrag, sizeof( vk_smokeUpFrag ) );
	sm.upPipeline = VK_CreatePipeline( vk.postVertexShader, sm.upShader, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA, 8, VKP_GENERIC );
}

void VK_SmokeShutdown( void ) {
	if ( sm.pipeline ) {
		qvkDestroyPipeline( vk.device, sm.pipeline, NULL );
		qvkDestroyPipeline( vk.device, sm.upPipeline, NULL );
	}
	if ( sm.shader ) {
		qvkDestroyShaderModule( vk.device, sm.shader, NULL );
		qvkDestroyShaderModule( vk.device, sm.upShader, NULL );
	}
	if ( sm.noise ) {
		// (its set goes with the pool)
		qvkDestroySampler( vk.device, sm.noiseSampler, NULL );
		qvkDestroyImageView( vk.device, sm.noiseView, NULL );
		qvkDestroyImage( vk.device, sm.noise, NULL );
		VK_FreeMemory( &sm.noiseAlloc );
	}
	memset( &sm, 0, sizeof( sm ) );
}

/*
===============
RE_SetSmoke

ext.SetSmoke: the smoke of the scene of this frame
===============
*/
void RE_SetSmoke( const smokeVolume_t *smoke ) {
	if ( !smoke || smoke->numBlobs <= 0 || !sm.pipeline ) {
		sm.frame = -1;
		return;
	}
	sm.volume = *smoke;
	sm.volume.numBlobs = Com_Clampi( 0, SMOKE_MAX_BLOBS, smoke->numBlobs );
	sm.volume.numHoles = Com_Clampi( 0, SMOKE_MAX_HOLES, smoke->numHoles );
	sm.frame = tr.frameCount;
}

/*
===============
VK_SmokeDraw

In the view of the level itself, after its solid surfaces and the light passes: the smoke over the part of the view
its blobs cover.
===============
*/
void VK_SmokeDraw( void ) {
	float			rows[VK_WET_HEADER][4];
	int				viewport[4];
	float			x0 = 1.0f, y0 = 1.0f, x1 = -1.0f, y1 = -1.0f;
	const float		*eye = backEnd.viewParms.ori.origin;

	if ( !vk.frame || !sm.pipeline || !vk.depthReadable || !vk.volume.image || !vk.volumeSet || sm.frame != tr.frameCount
		|| sm.volume.numBlobs <= 0 ) {
		return;
	}
	if ( backEnd.viewParms.isPortal || backEnd.viewParms.isPuddle || backEnd.viewParms.probeFace
		|| ( backEnd.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) ) ) {
		return;
	}
	vkGlobals_t	*globals = (vkGlobals_t *)vk.frame->globals.mapped;
	float		( *r )[4] = &globals->smoke[VK_WET_HEADER];
	int			lights = 0;

	VK_ViewRows( rows, viewport );
	memcpy( globals->smoke[0], rows, sizeof( rows ) );
	for ( int i = 0; i < sm.volume.numBlobs; i++ ) {
		const smokeBlob_t	*b = &sm.volume.blobs[i];

		VectorCopy( b->origin, r[1 + i * VK_SMOKE_BLOB_ROWS] );
		r[1 + i * VK_SMOKE_BLOB_ROWS][3] = b->radius;
		VectorCopy( b->color, r[2 + i * VK_SMOKE_BLOB_ROWS] );
		r[2 + i * VK_SMOKE_BLOB_ROWS][3] = b->density;
		r[3 + i * VK_SMOKE_BLOB_ROWS][0] = Com_Clamp( 0.15f, 1.0f, b->flat );
		VectorCopy( b->drift, &r[3 + i * VK_SMOKE_BLOB_ROWS][1] );
		// the part of the view it covers, as far as its billows reach (shaders/smoke.frag)
		float bounds[4];

		if ( VK_BallBounds( b->origin, b->radius * 1.3f, bounds ) ) {
			x0 = Q_min( x0, bounds[0] );
			y0 = Q_min( y0, bounds[1] );
			x1 = Q_max( x1, bounds[2] );
			y1 = Q_max( y1, bounds[3] );
		}
	}
	if ( x0 >= x1 || y0 >= y1 ) {
		return;		// none of it in the view
	}
	for ( int i = 0; i < sm.volume.numHoles; i++ ) {
		const smokeHole_t	*h = &sm.volume.holes[i];
		float				( *row )[4] = &r[1 + SMOKE_MAX_BLOBS * VK_SMOKE_BLOB_ROWS + i * 2];

		VectorCopy( h->start, row[0] );
		row[0][3] = h->radius;
		VectorCopy( h->end, row[1] );
		row[1][3] = Com_Clamp( 0.0f, 1.0f, h->strength );
	}
	// the moving lights light it: the strongest near the eye
	{
		float scores[VK_SMOKE_LIGHTS] = { 0.0f };

		for ( int l = 0; l < backEnd.refdef.num_dlights; l++ ) {
			const dlight_t	*dl = &backEnd.refdef.dlights[l];
			const float		score = ( dl->color[0] + dl->color[1] + dl->color[2] ) * dl->radius / ( 1.0f + Distance( dl->origin, eye ) / 512.0f );
			int				slot = lights;

			if ( lights == VK_SMOKE_LIGHTS ) {
				slot = 0;
				for ( int k = 1; k < VK_SMOKE_LIGHTS; k++ ) {
					if ( scores[k] < scores[slot] ) {
						slot = k;
					}
				}
				if ( score <= scores[slot] ) {
					continue;
				}
			} else {
				lights++;
			}
			float ( *row )[4] = &r[1 + SMOKE_MAX_BLOBS * VK_SMOKE_BLOB_ROWS + SMOKE_MAX_HOLES * 2 + slot * 2];

			scores[slot] = score;
			VectorCopy( dl->origin, row[0] );
			row[0][3] = dl->radius;
			// (a fifth of it: with more a cloud next to a fire glowed like a lamp)
			VectorScale( dl->color, 0.2f * Com_Clamp( 0.0f, 8.0f, v_dlightScale->value ), row[1] );
			row[1][3] = 0.0f;
		}
	}
	r[0][0] = (float)sm.volume.numBlobs;
	r[0][1] = (float)sm.volume.numHoles;
	r[0][2] = sm.volume.time;
	r[0][3] = (float)lights;
	globals->smoke[VK_SMOKE_VEC4S - 1][0] = (float)VK_SMOKE_SCALE;

	// the part of the target: rows[8] is the rect of the view in its pixels, y down
	const float	px0 = rows[8][0] + ( x0 * 0.5f + 0.5f ) * rows[8][2], px1 = rows[8][0] + ( x1 * 0.5f + 0.5f ) * rows[8][2];
	const float	py0 = rows[8][1] + ( 0.5f - y1 * 0.5f ) * rows[8][3], py1 = rows[8][1] + ( 0.5f - y0 * 0.5f ) * rows[8][3];
	const int	area[4] = { (int)floorf( px0 ), (int)floorf( py0 ), (int)ceilf( px1 ) - (int)floorf( px0 ), (int)ceilf( py1 ) - (int)floorf( py0 ) };

	if ( area[2] <= 0 || area[3] <= 0 ) {
		return;
	}
	const VkDescriptorSet	sets[3] = { vk.frame->set, sm.noiseSet, vk.depthSet };
	const VkDescriptorSet	upSets[3] = { vk.frame->set, vk.volumeSet, vk.depthSet };
	VkCommandBuffer			cmd = vk.frame->cmd;
	VkViewport				small = { 0.0f, 0.0f, (float)vk.volume.width, (float)vk.volume.height, 0.0f, 1.0f };
	VkRect2D				scissor;

	VK_EndTarget();
	// (the pass reads the depth of the scene, which was written until now)
	VK_TargetLayout( cmd, &vk.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL );
	// the rays through the smoke, at half the size of the frame: only the part of the picture the blobs cover
	VK_BeginTarget( &vk.volume, NULL, 1, qfalse );
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sm.pipeline );
	vk.boundPipeline = sm.pipeline;
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.screenFxPipelineLayout, 0, 3, sets, 0, NULL );
	qvkCmdSetViewport( cmd, 0, 1, &small );
	scissor.offset.x = area[0] / VK_SMOKE_SCALE;
	scissor.offset.y = area[1] / VK_SMOKE_SCALE;
	scissor.extent.width = (uint32_t)Q_min( ( area[2] + VK_SMOKE_SCALE * 2 ) / VK_SMOKE_SCALE, vk.volume.width - area[0] / VK_SMOKE_SCALE );
	scissor.extent.height = (uint32_t)Q_min( ( area[3] + VK_SMOKE_SCALE * 2 ) / VK_SMOKE_SCALE, vk.volume.height - area[1] / VK_SMOKE_SCALE );
	qvkCmdSetScissor( cmd, 0, 1, &scissor );
	qvkCmdDraw( cmd, 3, 1, 0, 0 );
	VK_EndTarget();
	VK_TargetLayout( cmd, &vk.volume, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );

	// and that over the level (shaders/smokeup.frag)
	VK_BeginOverLevel( cmd, area, qfalse );
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sm.upPipeline );
	vk.boundPipeline = sm.upPipeline;
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.screenFxPipelineLayout, 0, 3, upSets, 0, NULL );
	VK_ViewportAndScissor( viewport[0], viewport[1], viewport[2], viewport[3], 0.0f, 1.0f );
	scissor.offset.x = area[0];
	scissor.offset.y = area[1];
	scissor.extent.width = (uint32_t)area[2];
	scissor.extent.height = (uint32_t)area[3];
	qvkCmdSetScissor( cmd, 0, 1, &scissor );
	qvkCmdDraw( cmd, 3, 1, 0, 0 );
	VK_EndTarget();

	VK_BeginTarget( &vk.color, &vk.depth, 2, qtrue );
	// the shaders that draw geometry have other sets and other state
	VK_ResumeScene();
	VK_InvalidateTransform();
}
