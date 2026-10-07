/*
===========================================================================
Copyright (C) 2026, OpenJK contributors

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

// vk_image.cpp: the texture table, uploads, samplers, render targets
//
// All textures of the game sit in one array of images that every shader sees. image_t::texnum is the index.
// The samplers they are read with are an array of their own next to it: there are a handful of them, and a
// draw names the sampler with the texture (vkImage_t::sampler, VK_SAMPLER_SHIFT). A sampler in every entry of
// the table would be more than a Vulkan on top of Metal takes: MoltenVK says about a thousand samplers a set.
// tr_image.cpp of rd-vanilla still decides what a texture looks like (size, picmip, light scale,
// mip levels) and drives the upload: GL_Bind( image ), then VK_ImageLevel() per level and VK_ImageParm() for
// filter and wrap mode, see vk_hooks.h.

#include "vk_local.h"
#include <chrono>

/*
===============
VK_SetLayout
===============
*/
static VkAccessFlags VK_LayoutAccess( VkImageLayout layout ) {
	switch ( layout ) {
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:				return VK_ACCESS_TRANSFER_WRITE_BIT;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:				return VK_ACCESS_TRANSFER_READ_BIT;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:			return VK_ACCESS_SHADER_READ_BIT;
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:			return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:	return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	default:												return 0;
	}
}

void VK_SetLayout( VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to ) {
	VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };

	if ( from == to ) {
		return;
	}
	barrier.srcAccessMask = VK_LayoutAccess( from );
	barrier.dstAccessMask = VK_LayoutAccess( to );
	barrier.oldLayout = from;
	barrier.newLayout = to;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = aspect;
	barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
	barrier.subresourceRange.layerCount = 1;
	qvkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
		0, NULL, 0, NULL, 1, &barrier );
}

void VK_TargetLayout( VkCommandBuffer cmd, vkTarget_t *target, VkImageLayout to ) {
	const VkImageAspectFlags aspect = ( target == &vk.depth || target == &vk.depthMS )
		? ( VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT ) : VK_IMAGE_ASPECT_COLOR_BIT;

	VK_SetLayout( cmd, target->image, aspect, target->layout, to );
	target->layout = to;
}

/*
===============
VK_CreateTarget

An image to render into
===============
*/
void VK_CreateTarget( vkTarget_t *target, int width, int height, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
	VkSampleCountFlagBits samples ) {
	VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	VkImageViewCreateInfo view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	VkMemoryRequirements req;

	memset( target, 0, sizeof( *target ) );
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = format;
	info.extent.width = (uint32_t)width;
	info.extent.height = (uint32_t)height;
	info.extent.depth = 1;
	info.mipLevels = 1;
	info.arrayLayers = 1;
	info.samples = samples;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = usage;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK( qvkCreateImage( vk.device, &info, NULL, &target->image ) );
	qvkGetImageMemoryRequirements( vk.device, target->image, &req );
	VK_AllocMemory( &req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &target->alloc );
	VK_CHECK( qvkBindImageMemory( vk.device, target->image, VK_MemoryOf( &target->alloc ), target->alloc.offset ) );

	view.image = target->image;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = format;
	view.subresourceRange.aspectMask = aspect;
	view.subresourceRange.levelCount = 1;
	view.subresourceRange.layerCount = 1;
	VK_CHECK( qvkCreateImageView( vk.device, &view, NULL, &target->view ) );
	target->format = format;
	target->layout = VK_IMAGE_LAYOUT_UNDEFINED;
	target->width = width;
	target->height = height;
}

void VK_DestroyTarget( vkTarget_t *target ) {
	if ( target->image ) {
		qvkDestroyImageView( vk.device, target->view, NULL );
		qvkDestroyImage( vk.device, target->image, NULL );
		VK_FreeMemory( &target->alloc );
	}
	memset( target, 0, sizeof( *target ) );
}

/*
===============================================================================

UPLOADS

Copies wait in one command buffer and one staging buffer until the frame is submitted or the staging buffer is
full. Loading a level is a few thousand small copies, they should not be a few thousand submits.

===============================================================================
*/

VkCommandBuffer VK_UploadCmd( void ) {
	if ( !vk.uploadOpen ) {
		VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };

		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK( qvkBeginCommandBuffer( vk.uploadCmd, &begin ) );
		vk.uploadOpen = qtrue;
	}
	return vk.uploadCmd;
}

void VK_FlushUploads( void ) {
	VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

	if ( !vk.uploadOpen ) {
		return;
	}
	VK_CHECK( qvkEndCommandBuffer( vk.uploadCmd ) );
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &vk.uploadCmd;
	VK_CHECK( qvkQueueSubmit( vk.queue, 1, &submit, VK_NULL_HANDLE ) );
	VK_CHECK( qvkQueueWaitIdle( vk.queue ) );
	vk.uploadOpen = qfalse;
	vk.stagingUsed = 0;
}

// room for size bytes in the staging buffer, the copy command that reads them has to go into VK_UploadCmd()
byte *VK_Stage( VkDeviceSize size, VkDeviceSize *offset ) {
	if ( vk.stagingUsed + size > vk.staging.size ) {
		VK_FlushUploads();
		if ( size > vk.staging.size ) {
			VK_DestroyBuffer( &vk.staging );
			VK_CreateBuffer( &vk.staging, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, qtrue );
		}
	}
	*offset = vk.stagingUsed;
	vk.stagingUsed += ( size + 15 ) & ~(VkDeviceSize)15;
	return vk.staging.mapped + *offset;
}

/*
===============================================================================

SAMPLERS

===============================================================================
*/

// the index of the sampler with these parameters in vk.samplers and in the array of the shaders
static int VK_FindSampler( int minFilter, int magFilter, int wrap, float anisotropy ) {
	const int key[4] = { minFilter, magFilter, wrap, (int)anisotropy };
	VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	VkDescriptorImageInfo image = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
	VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

	for ( int i = 0; i < vk.numSamplers; i++ ) {
		if ( !memcmp( vk.samplerKeys[i], key, sizeof( key ) ) ) {
			return i;
		}
	}
	if ( vk.numSamplers == VK_MAX_SAMPLERS ) {
		return 0;
	}

	info.magFilter = ( magFilter == GL_NEAREST ) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
	info.minFilter = ( minFilter == GL_NEAREST || minFilter == GL_NEAREST_MIPMAP_NEAREST || minFilter == GL_NEAREST_MIPMAP_LINEAR )
		? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
	info.mipmapMode = ( minFilter == GL_NEAREST_MIPMAP_LINEAR || minFilter == GL_LINEAR_MIPMAP_LINEAR )
		? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
	// GL_NEAREST and GL_LINEAR as minification filter mean: the first level only
	info.maxLod = ( minFilter == GL_NEAREST || minFilter == GL_LINEAR ) ? 0.25f : VK_LOD_CLAMP_NONE;
	info.addressModeU = info.addressModeV = info.addressModeW =
		( wrap == GL_REPEAT ) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	if ( anisotropy > 1.0f && vk.anisotropy && vk.properties.limits.maxSamplerAnisotropy > 1.0f ) {
		info.anisotropyEnable = VK_TRUE;
		info.maxAnisotropy = Q_min( anisotropy, vk.properties.limits.maxSamplerAnisotropy );
	}
	VK_CHECK( qvkCreateSampler( vk.device, &info, NULL, &vk.samplers[vk.numSamplers] ) );
	memcpy( vk.samplerKeys[vk.numSamplers], key, sizeof( key ) );

	// into its place of the array of samplers, next to the texture table
	image.sampler = vk.samplers[vk.numSamplers];
	write.dstSet = vk.textureSet;
	write.dstBinding = 1;
	write.dstArrayElement = (uint32_t)vk.numSamplers;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	write.pImageInfo = &image;
	qvkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
	return vk.numSamplers++;
}

// puts the image into its slot of the table and notes the sampler its parameters ask for
static void VK_WriteTexture( int slot ) {
	vkImage_t *vi = &vk.images[slot];
	VkDescriptorImageInfo image;
	VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

	vi->sampler = VK_FindSampler( vi->minFilter, vi->magFilter, vi->wrap, vi->anisotropy );
	image.sampler = VK_NULL_HANDLE;
	image.imageView = vi->view;
	image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	write.dstSet = vk.textureSet;
	write.dstBinding = 0;
	write.dstArrayElement = (uint32_t)slot;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	write.pImageInfo = &image;
	qvkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
}

/*
===============================================================================

THE TEXTURE TABLE

===============================================================================
*/

// R_CreateImage() gives a new image a number of its own, the first upload replaces it by the slot
static vkImage_t *VK_SlotOf( const image_t *image ) {
	if ( image && image->texnum < VK_MAX_IMAGES && vk.images[image->texnum].owner == image ) {
		return &vk.images[image->texnum];
	}
	return NULL;
}

static void VK_FreeSlot( vkImage_t *vi ) {
	if ( vi->image ) {
		// nothing that was submitted may still read it
		qvkDeviceWaitIdle( vk.device );
		qvkDestroyImageView( vk.device, vi->view, NULL );
		qvkDestroyImage( vk.device, vi->image, NULL );
		VK_FreeMemory( &vi->alloc );
	}
	memset( vi, 0, sizeof( *vi ) );
}

static vkImage_t *VK_NewSlot( image_t *image ) {
	static int rover = 0;

	for ( int i = 0; i < VK_MAX_IMAGES; i++ ) {
		const int slot = ( rover + i ) % VK_MAX_IMAGES;

		if ( !vk.images[slot].owner ) {
			vkImage_t *vi = &vk.images[slot];

			rover = slot + 1;
			memset( vi, 0, sizeof( *vi ) );
			vi->owner = image;
			vi->minFilter = GL_LINEAR;
			vi->magFilter = GL_LINEAR;
			vi->wrap = GL_REPEAT;
			vi->anisotropy = 1.0f;
			image->texnum = (GLuint)slot;
			return vi;
		}
	}
	Com_Error( ERR_DROP, "VK_NewSlot: more than %i textures", VK_MAX_IMAGES );
	return NULL;
}

static void VK_CreateSlotImage( vkImage_t *vi, int width, int height, int mipLevels, VkFormat format ) {
	VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	VkImageViewCreateInfo view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	VkMemoryRequirements req;

	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = format;
	info.extent.width = (uint32_t)width;
	info.extent.height = (uint32_t)height;
	info.extent.depth = 1;
	info.mipLevels = (uint32_t)mipLevels;
	info.arrayLayers = 1;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK( qvkCreateImage( vk.device, &info, NULL, &vi->image ) );
	qvkGetImageMemoryRequirements( vk.device, vi->image, &req );
	VK_AllocMemory( &req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vi->alloc );
	VK_CHECK( qvkBindImageMemory( vk.device, vi->image, VK_MemoryOf( &vi->alloc ), vi->alloc.offset ) );

	view.image = vi->image;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = info.format;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = (uint32_t)mipLevels;
	view.subresourceRange.layerCount = 1;
	VK_CHECK( qvkCreateImageView( vk.device, &view, NULL, &vi->view ) );
	vi->width = width;
	vi->height = height;
	vi->mipLevels = mipLevels;
	vi->format = format;
	vi->uploaded = qfalse;
}

/*
===============
VK_LevelFormat

tr_image.cpp names a compressed format for the images that may be compressed when glConfig.textureCompression
says so (r_ext_compress_textures; lightmaps only with r_ext_compress_lightmaps, shaders with noTC never). It
asks for the one with alpha when an image has none too, unless r_texturebits is 16. The colors are stored the
same way in both, so here an image without alpha takes the format without, which is half the size.
===============
*/
static VkFormat VK_LevelFormat( image_t *image, int internalFormat, int width, int height, const byte *rgba ) {
	if ( !vk.bcTextures || ( internalFormat != GL_COMPRESSED_RGB_S3TC_DXT1_EXT && internalFormat != GL_COMPRESSED_RGBA_S3TC_DXT5_EXT ) ) {
		return VK_FORMAT_R8G8B8A8_UNORM;
	}
	// what imagelist prints
	if ( width < 4 || height < 4 ) {
		image->internalFormat = GL_RGBA8;
		return VK_FORMAT_R8G8B8A8_UNORM;
	}
	for ( int i = 0; i < width * height; i++ ) {
		if ( rgba[i * 4 + 3] != 255 ) {
			image->internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
			return VK_FORMAT_BC3_UNORM_BLOCK;
		}
	}
	image->internalFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
	return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
}

/*
===============
GL_Bind

In this renderer: the image the following VK_ImageLevel() and VK_ImageParm() calls are about
===============
*/
void GL_Bind( image_t *image ) {
	vk.boundImage = image;
}

void GL_SelectTexture( int unit ) {
}

/*
===============
VK_ImageLevel

One mip level of the bound image, 32 bit RGBA. Level 0 comes first and makes the texture, with all levels if
the image is mip mapped: Upload32() then sends every level down to 1 x 1. internalFormat is what OpenGL was to
store it as.
===============
*/
void VK_ImageLevel( int level, int internalFormat, int width, int height, const byte *rgba ) {
	image_t *image = vk.boundImage;
	vkImage_t *vi = VK_SlotOf( image );
	VkBufferImageCopy region;
	VkDeviceSize offset, size;
	VkCommandBuffer cmd;

	if ( !image ) {
		return;
	}
	if ( level == 0 ) {
		const VkFormat format = VK_LevelFormat( image, internalFormat, width, height, rgba );
		int mipLevels = 1;

		if ( image->mipmap ) {
			for ( int w = width, h = height; w > 1 || h > 1; w >>= 1, h >>= 1 ) {
				mipLevels++;
			}
		}
		if ( vi && ( vi->width != width || vi->height != height || vi->mipLevels != mipLevels || vi->format != format ) ) {
			// new contents of another size, keep the slot and the sampler parameters
			vkImage_t keep = *vi;

			VK_FlushUploads();
			qvkDeviceWaitIdle( vk.device );
			qvkDestroyImageView( vk.device, vi->view, NULL );
			qvkDestroyImage( vk.device, vi->image, NULL );
			VK_FreeMemory( &vi->alloc );
			keep.image = VK_NULL_HANDLE;
			*vi = keep;
		}
		if ( !vi ) {
			vi = VK_NewSlot( image );
		}
		if ( !vi->image ) {
			VK_CreateSlotImage( vi, width, height, mipLevels, format );
			VK_SetLayout( VK_UploadCmd(), vi->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
		} else {
			VK_SetLayout( VK_UploadCmd(), vi->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
		}
		vi->uploaded = qfalse;
	}
	if ( !vi || !vi->image || level >= vi->mipLevels ) {
		return;
	}

	if ( vi->format == VK_FORMAT_R8G8B8A8_UNORM ) {
		size = (VkDeviceSize)width * height * 4;
		memcpy( VK_Stage( size, &offset ), rgba, (size_t)size );
	} else {
		const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

		size = VK_BC_Size( vi->format, width, height );
		VK_BC_Compress( vi->format, rgba, width, height, VK_Stage( size, &offset ) );
		vk.statBcPixels += (double)width * height;
		vk.statBcSeconds += std::chrono::duration<double>( std::chrono::steady_clock::now() - start ).count();
	}
	cmd = VK_UploadCmd();		// VK_Stage() may have flushed
	memset( &region, 0, sizeof( region ) );
	region.bufferOffset = offset;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = (uint32_t)level;
	region.imageSubresource.layerCount = 1;
	region.imageExtent.width = (uint32_t)width;
	region.imageExtent.height = (uint32_t)height;
	region.imageExtent.depth = 1;
	qvkCmdCopyBufferToImage( cmd, vk.staging.buffer, vi->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );

	if ( level == vi->mipLevels - 1 ) {
		VK_SetLayout( cmd, vi->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
		vi->uploaded = qtrue;
		VK_WriteTexture( (int)( vi - vk.images ) );
	}
}

/*
===============
VK_ImageParm

glTexParameter of the bound image: filters, wrap mode, anisotropy
===============
*/
void VK_ImageParm( int name, int value ) {
	vkImage_t *vi = VK_SlotOf( vk.boundImage );

	if ( !vi ) {
		return;
	}
	switch ( name ) {
	case GL_TEXTURE_MIN_FILTER:			vi->minFilter = value;			break;
	case GL_TEXTURE_MAG_FILTER:			vi->magFilter = value;			break;
	case GL_TEXTURE_WRAP_S:
	case GL_TEXTURE_WRAP_T:				vi->wrap = value;				break;
	case GL_TEXTURE_MAX_ANISOTROPY_EXT:	vi->anisotropy = (float)value;	break;
	default:							return;
	}
	// the image stays where it is in the table, the draws that come name another sampler with it
	if ( vi->uploaded ) {
		vi->sampler = VK_FindSampler( vi->minFilter, vi->magFilter, vi->wrap, vi->anisotropy );
	}
}

void VK_ImageDelete( unsigned int texnum ) {
	if ( texnum < VK_MAX_IMAGES && vk.images[texnum].owner ) {
		if ( vk.boundImage == vk.images[texnum].owner ) {
			vk.boundImage = NULL;
		}
		VK_FlushUploads();
		VK_FreeSlot( &vk.images[texnum] );
	}
}

/*
===============
VK_UpdateImage

New contents for an image that changes while the game runs (cinematics, scratch images)
===============
*/
void VK_UpdateImage( image_t *image, int width, int height, const byte *rgba ) {
	image_t *bound = vk.boundImage;

	vk.boundImage = image;
	image->width = width;
	image->height = height;
	VK_ImageLevel( 0, GL_RGBA8, width, height, rgba );
	vk.boundImage = bound;
}

void VK_InitImages( void ) {
	VkCommandBufferAllocateInfo alloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };

	alloc.commandPool = vk.commandPool;
	alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc.commandBufferCount = 1;
	VK_CHECK( qvkAllocateCommandBuffers( vk.device, &alloc, &vk.uploadCmd ) );
	VK_CreateBuffer( &vk.staging, VK_STAGING_BYTES, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, qtrue );
	if ( vk.bcTextures ) {
		VK_BC_Init();
	}
}

void VK_ShutdownImages( void ) {
	VK_FlushUploads();
	VK_BC_Shutdown();
	for ( int i = 0; i < VK_MAX_IMAGES; i++ ) {
		if ( vk.images[i].image ) {
			qvkDestroyImageView( vk.device, vk.images[i].view, NULL );
			qvkDestroyImage( vk.device, vk.images[i].image, NULL );
		}
	}
	memset( vk.images, 0, sizeof( vk.images ) );
	for ( int i = 0; i < vk.numSamplers; i++ ) {
		qvkDestroySampler( vk.device, vk.samplers[i], NULL );
	}
	vk.numSamplers = 0;
	VK_DestroyBuffer( &vk.staging );
}

/*
===============
RB_ShowImages

RE_EndRegistration() calls this in rd-vanilla to touch every texture, so that OpenGL has them all in memory.
Here they are on the device once they are uploaded. The tiles of r_showImages are VK_ShowImages().
===============
*/
void RB_ShowImages( void ) {
}
