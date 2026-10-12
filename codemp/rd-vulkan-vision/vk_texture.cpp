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

// vk_texture.cpp: what a texture and a render target are for Vulkan: images, views, layouts, samplers, the
// descriptors of the texture table, and the uploads
//
// vk_image.cpp keeps the texture table and decides what a texture looks like. This file puts it on the device:
// all textures of the game sit in one array of combined image samplers that every shader sees, image_t::texnum
// is the index.

#include "vk_local.h"

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
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:	return VK_ACCESS_SHADER_READ_BIT;
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
	ri.LoadPush( "wait for uploads", NULL );
	VK_CHECK( qvkQueueWaitIdle( vk.queue ) );
	ri.LoadPop();
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

void VK_CopyBuffer( const vkBuffer_t *src, vkSize_t srcOffset, vkBuffer_t *dst, vkSize_t dstOffset, vkSize_t size ) {
	VkBufferCopy region;

	region.srcOffset = srcOffset;
	region.dstOffset = dstOffset;
	region.size = size;
	qvkCmdCopyBuffer( VK_UploadCmd(), src->buffer, dst->buffer, 1, &region );
}

/*
===============================================================================

SAMPLERS

===============================================================================
*/

static VkSampler VK_FindSampler( int minFilter, int magFilter, int wrap, float anisotropy ) {
	const int key[4] = { minFilter, magFilter, wrap, (int)anisotropy };
	VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

	for ( int i = 0; i < vk.numSamplers; i++ ) {
		if ( !memcmp( vk.samplerKeys[i], key, sizeof( key ) ) ) {
			return vk.samplers[i];
		}
	}
	if ( vk.numSamplers == VK_MAX_SAMPLERS ) {
		return vk.samplers[0];
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
	return vk.samplers[vk.numSamplers++];
}

// puts the image with the sampler its parameters ask for into its slot of the table
void VK_WriteTexture( int slot ) {
	const vkImage_t *vi = &vk.images[slot];
	VkDescriptorImageInfo image;
	VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

	image.sampler = VK_FindSampler( vi->minFilter, vi->magFilter, vi->wrap, vi->anisotropy );
	image.imageView = vi->view;
	image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	write.dstSet = vk.textureSet;
	write.dstBinding = 0;
	write.dstArrayElement = (uint32_t)slot;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &image;
	qvkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
}

void VK_ShowTarget( int slot, const vkTarget_t *target ) {
	VkDescriptorImageInfo image = { vk.linearSampler, target->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

	write.dstSet = vk.textureSet;
	write.dstArrayElement = (uint32_t)slot;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &image;
	qvkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
}

/*
===============================================================================

THE IMAGES OF THE TEXTURE TABLE

===============================================================================
*/

void VK_CreateImage( vkImage_t *vi, int width, int height, int mipLevels, vkFormat_t format ) {
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
}

void VK_DestroyImage( vkImage_t *vi ) {
	if ( vi->image ) {
		// nothing that was submitted may still read it
		qvkDeviceWaitIdle( vk.device );
		qvkDestroyImageView( vk.device, vi->view, NULL );
		qvkDestroyImage( vk.device, vi->image, NULL );
		VK_FreeMemory( &vi->alloc );
		vi->image = VK_NULL_HANDLE;
	}
}

void VK_BeginImageUpload( vkImage_t *vi, qboolean created ) {
	VK_SetLayout( VK_UploadCmd(), vi->image, VK_IMAGE_ASPECT_COLOR_BIT,
		created ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
}

void VK_UploadImageLevel( vkImage_t *vi, int level, int width, int height, vkSize_t staged ) {
	VkBufferImageCopy region;
	VkCommandBuffer cmd;

	cmd = VK_UploadCmd();		// VK_Stage() may have flushed
	memset( &region, 0, sizeof( region ) );
	region.bufferOffset = staged;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = (uint32_t)level;
	region.imageSubresource.layerCount = 1;
	region.imageExtent.width = (uint32_t)width;
	region.imageExtent.height = (uint32_t)height;
	region.imageExtent.depth = 1;
	qvkCmdCopyBufferToImage( cmd, vk.staging.buffer, vi->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );
}

void VK_EndImageUpload( vkImage_t *vi ) {
	VK_SetLayout( VK_UploadCmd(), vi->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
}

void VK_InitTextures( void ) {
	VkCommandBufferAllocateInfo alloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };

	alloc.commandPool = vk.commandPool;
	alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc.commandBufferCount = 1;
	VK_CHECK( qvkAllocateCommandBuffers( vk.device, &alloc, &vk.uploadCmd ) );
	VK_CreateBuffer( &vk.staging, VK_STAGING_BYTES, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, qtrue );
}

void VK_ShutdownTextures( void ) {
	for ( int i = 0; i < VK_MAX_IMAGES; i++ ) {
		if ( vk.images[i].image ) {
			qvkDestroyImageView( vk.device, vk.images[i].view, NULL );
			qvkDestroyImage( vk.device, vk.images[i].image, NULL );
		}
	}
	for ( int i = 0; i < vk.numSamplers; i++ ) {
		qvkDestroySampler( vk.device, vk.samplers[i], NULL );
	}
	vk.numSamplers = 0;
	VK_DestroyBuffer( &vk.staging );
}

/*
===============================================================================

THE TARGETS OF THE GLOW AND OF REFRACTION

===============================================================================
*/

static void VK_WritePostSet( VkDescriptorSet set, VkImageView view ) {
	VkDescriptorImageInfo image = { vk.linearSampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

	write.dstSet = set;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &image;
	qvkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
}

void VK_CreatePostTargets( int blurWidth, int blurHeight ) {
	const VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	VkSamplerCreateInfo sampler = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	VkDescriptorSetAllocateInfo alloc = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	VkDescriptorSet *sets[6] = { &vk.glowSet, &vk.blurSets[0], &vk.blurSets[1], &vk.softSets[0], &vk.softSets[1], &vk.wideSet };
	vkTarget_t *targets[6] = { &vk.glow, &vk.blur[0], &vk.blur[1], &vk.soft[0], &vk.soft[1], &vk.wide };

	VK_CreateTarget( &vk.glow, vk.width, vk.height, VK_FORMAT_R8G8B8A8_UNORM, usage, VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT );
	if ( vk.samples > VK_SAMPLE_COUNT_1_BIT ) {
		// the glowing stages are drawn over the depth of the scene, so with as many samples
		VK_CreateTarget( &vk.glowMS, vk.width, vk.height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, vk.samples );
	}
	VK_CreateTarget( &vk.screen, vk.width, vk.height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT );
	{
		// OpenGL copies a frame buffer that has no alpha, and such a texture reads as 1 there. The frame here has an
		// alpha channel with what the blends left in it, so the copy is looked at without it.
		VkImageViewCreateInfo view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };

		qvkDestroyImageView( vk.device, vk.screen.view, NULL );
		view.image = vk.screen.image;
		view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view.format = vk.screen.format;
		view.components.a = VK_COMPONENT_SWIZZLE_ONE;
		view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view.subresourceRange.levelCount = 1;
		view.subresourceRange.layerCount = 1;
		VK_CHECK( qvkCreateImageView( vk.device, &view, NULL, &vk.screen.view ) );
	}
	VK_CreateTarget( &vk.blur[0], blurWidth, blurHeight, VK_FORMAT_R8G8B8A8_UNORM, usage, VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT );
	VK_CreateTarget( &vk.blur[1], blurWidth, blurHeight, VK_FORMAT_R8G8B8A8_UNORM, usage, VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT );
	for ( int i = 0; i < 2; i++ ) {
		VK_CreateTarget( &vk.soft[i], Q_max( 1, vk.width / 2 ), Q_max( 1, vk.height / 2 ), VK_FORMAT_R8G8B8A8_UNORM, usage, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_SAMPLE_COUNT_1_BIT );
	}
	// (one sample a pixel whatever the scene has: such a pass covers every pixel as a whole)
	VK_CreateTarget( &vk.wide, vk.width, vk.height, VK_FORMAT_R8G8B8A8_UNORM, usage, VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT );

	sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
	sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	VK_CHECK( qvkCreateSampler( vk.device, &sampler, NULL, &vk.linearSampler ) );

	alloc.descriptorPool = vk.descriptorPool;
	alloc.descriptorSetCount = 1;
	alloc.pSetLayouts = &vk.postSetLayout;
	for ( int i = 0; i < 6; i++ ) {
		VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, sets[i] ) );
		VK_WritePostSet( *sets[i], targets[i]->view );
	}

	// The depth of the scene for the depth of field: a view of the depth without the stencil, read pixel for
	// pixel (not every device filters a depth format) while nothing draws into it.
	if ( vk.depthReadable ) {
		VkImageViewCreateInfo view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		VkDescriptorImageInfo image = { vk.postSampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
		VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

		view.image = vk.depth.image;
		view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view.format = vk.depth.format;
		view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		view.subresourceRange.levelCount = 1;
		view.subresourceRange.layerCount = 1;
		VK_CHECK( qvkCreateImageView( vk.device, &view, NULL, &vk.depthView ) );
		VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &vk.depthSet ) );
		image.imageView = vk.depthView;
		write.dstSet = vk.depthSet;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &image;
		qvkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
	}
}

void VK_DestroyPostTargets( void ) {
	VK_DestroyTarget( &vk.glow );
	VK_DestroyTarget( &vk.glowMS );
	VK_DestroyTarget( &vk.screen );
	VK_DestroyTarget( &vk.blur[0] );
	VK_DestroyTarget( &vk.blur[1] );
	VK_DestroyTarget( &vk.soft[0] );
	VK_DestroyTarget( &vk.soft[1] );
	VK_DestroyTarget( &vk.wide );
	if ( vk.depthView ) {
		qvkDestroyImageView( vk.device, vk.depthView, NULL );
		vk.depthView = VK_NULL_HANDLE;
	}
	qvkDestroySampler( vk.device, vk.linearSampler, NULL );
}
