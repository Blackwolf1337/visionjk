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

Copies wait in the command buffer and the staging buffer of a slot until the frame is submitted or the staging
buffer is full. Loading a level is a few thousand small copies, they should not be a few thousand submits.

A batch is submitted and not waited for: it waited for the whole queue until 2026-10-19, frames that were still
drawn included, about 2 ms each time, 19 times in a load of mp/ffa3. What the wait gave is kept by two barriers
in every batch: its copies run after everything submitted before it (no copy writes a buffer that an earlier
frame still reads), and what they wrote is seen by everything submitted after it (a frame draws with what was
uploaded for it). Images have barriers of their own around their copies (VK_SetLayout). The CPU only waits when
it wants to fill a slot whose last batch the device has not read yet, and in VK_FinishUploads().

===============================================================================
*/

// the slot that is filled now can take writes: the device is through with what it was filled with the last time
static void VK_UploadSlotReady( void ) {
	const int slot = vk.uploadSlot;

	if ( vk.uploads[slot].pending ) {
		ri.LoadPush( "wait for uploads", NULL );
		VK_CHECK( qvkWaitForFences( vk.device, 1, &vk.uploads[slot].fence, VK_TRUE, UINT64_MAX ) );
		ri.LoadPop();
		vk.uploads[slot].pending = qfalse;
	}
}

VkCommandBuffer VK_UploadCmd( void ) {
	if ( !vk.uploadOpen ) {
		VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };

		VK_UploadSlotReady();
		VK_CHECK( qvkResetFences( vk.device, 1, &vk.uploads[vk.uploadSlot].fence ) );
		vk.uploadCmd = vk.uploads[vk.uploadSlot].cmd;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK( qvkBeginCommandBuffer( vk.uploadCmd, &begin ) );
		// (the copies after whatever was submitted before them)
		qvkCmdPipelineBarrier( vk.uploadCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			0, NULL, 0, NULL, 0, NULL );
		vk.uploadOpen = qtrue;
	}
	return vk.uploadCmd;
}

void VK_FlushUploads( void ) {
	VkSubmitInfo	submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	VkMemoryBarrier	written = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
	const int		slot = vk.uploadSlot, next = ( vk.uploadSlot + 1 ) % VK_UPLOAD_SLOTS;

	if ( !vk.uploadOpen ) {
		return;
	}
	// (what the copies wrote, for everything submitted after them)
	written.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	written.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	qvkCmdPipelineBarrier( vk.uploadCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
		1, &written, 0, NULL, 0, NULL );
	VK_CHECK( qvkEndCommandBuffer( vk.uploadCmd ) );
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &vk.uploadCmd;
	VK_CHECK( qvkQueueSubmit( vk.queue, 1, &submit, vk.uploads[slot].fence ) );
	vk.uploads[slot].pending = qtrue;
	vk.uploadOpen = qfalse;
	vk.stagingUsed = 0;
	// the next slot is filled from now on: its staging buffer becomes vk.staging
	vk.uploads[slot].staging = vk.staging;
	vk.staging = vk.uploads[next].staging;
	vk.uploadSlot = next;
}

void VK_FinishUploads( void ) {
	VK_FlushUploads();
	for ( int i = 0; i < VK_UPLOAD_SLOTS; i++ ) {
		if ( vk.uploads[i].pending ) {
			ri.LoadPush( "wait for uploads", NULL );
			VK_CHECK( qvkWaitForFences( vk.device, 1, &vk.uploads[i].fence, VK_TRUE, UINT64_MAX ) );
			ri.LoadPop();
			vk.uploads[i].pending = qfalse;
		}
	}
}

// room for size bytes in the staging buffer, the copy command that reads them has to go into VK_UploadCmd()
byte *VK_Stage( VkDeviceSize size, VkDeviceSize *offset ) {
	if ( vk.stagingUsed + size > vk.staging.size ) {
		VK_FlushUploads();
	}
	// (a slot that is not open may still be read by the device)
	if ( !vk.uploadOpen ) {
		VK_UploadSlotReady();
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
	VkCommandBufferAllocateInfo	alloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	VkFenceCreateInfo			fence = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

	alloc.commandPool = vk.commandPool;
	alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc.commandBufferCount = 1;
	for ( int i = 0; i < VK_UPLOAD_SLOTS; i++ ) {
		VK_CHECK( qvkAllocateCommandBuffers( vk.device, &alloc, &vk.uploads[i].cmd ) );
		VK_CHECK( qvkCreateFence( vk.device, &fence, NULL, &vk.uploads[i].fence ) );
		VK_CreateBuffer( &vk.uploads[i].staging, VK_STAGING_BYTES, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, qtrue );
		vk.uploads[i].pending = qfalse;
	}
	vk.uploadSlot = 0;
	vk.uploadCmd = vk.uploads[0].cmd;
	vk.uploadOpen = qfalse;
	vk.staging = vk.uploads[0].staging;
	vk.stagingUsed = 0;
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
	// (VK_ShutdownImages() finished the uploads)
	for ( int i = 0; i < VK_UPLOAD_SLOTS; i++ ) {
		if ( i == vk.uploadSlot ) {
			VK_DestroyBuffer( &vk.staging );
		} else {
			VK_DestroyBuffer( &vk.uploads[i].staging );
		}
		memset( &vk.uploads[i].staging, 0, sizeof( vk.uploads[i].staging ) );
		qvkDestroyFence( vk.device, vk.uploads[i].fence, NULL );
		vk.uploads[i].fence = VK_NULL_HANDLE;
		vk.uploads[i].pending = qfalse;
	}
}

/*
===============================================================================

THE TARGETS OF THE GLOW AND OF REFRACTION

===============================================================================
*/

void VK_WriteSet( VkDescriptorSet set, VkSampler sampler, VkImageView view, VkImageLayout layout ) {
	VkDescriptorImageInfo image = { sampler, view, layout };
	VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

	write.dstSet = set;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &image;
	qvkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
}

static void VK_WritePostSet( VkDescriptorSet set, VkImageView view ) {
	VK_WriteSet( set, vk.linearSampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
}

// A set of the kind of postSet, allocated the first time only. The targets are made anew when the window changes
// its size (VK_ResizeFrame()), their sets stay and are written again: the pool has no room for more.
static void VK_PostSetOnce( VkDescriptorSet *set ) {
	VkDescriptorSetAllocateInfo alloc = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };

	if ( *set ) {
		return;
	}
	alloc.descriptorPool = vk.descriptorPool;
	alloc.descriptorSetCount = 1;
	alloc.pSetLayouts = &vk.postSetLayout;
	VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, set ) );
}

// a view of the depth alone, without the stencil, for a set of the depth of field
static VkImageView VK_DepthOnlyView( const vkTarget_t *depth ) {
	VkImageViewCreateInfo view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	VkImageView result;

	view.image = depth->image;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = depth->format;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	view.subresourceRange.levelCount = 1;
	view.subresourceRange.layerCount = 1;
	VK_CHECK( qvkCreateImageView( vk.device, &view, NULL, &result ) );
	return result;
}

void VK_CreatePostTargets( int blurWidth, int blurHeight ) {
	const VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	VkSamplerCreateInfo sampler = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	VkDescriptorSet *sets[5] = { &vk.glowSet, &vk.blurSets[0], &vk.blurSets[1], &vk.softSets[0], &vk.softSets[1] };
	vkTarget_t *targets[5] = { &vk.glow, &vk.blur[0], &vk.blur[1], &vk.soft[0], &vk.soft[1] };

	VK_CreateTarget( &vk.glow, vk.width, vk.height, VK_FORMAT_R8G8B8A8_UNORM, usage, VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT );
	if ( vk.samples > VK_SAMPLE_COUNT_1_BIT ) {
		// the glowing stages are drawn over the depth of the scene, so with as many samples
		VK_CreateTarget( &vk.glowMS, vk.width, vk.height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, vk.samples );
	}
	// as large as the frame and as the views of the level with tay_renderScale, whichever is copied into it
	VK_CreateTarget( &vk.screen, Q_max( vk.width, vk.sceneWidth ), Q_max( vk.height, vk.sceneHeight ), VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT );
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

	sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
	sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	VK_CHECK( qvkCreateSampler( vk.device, &sampler, NULL, &vk.linearSampler ) );

	for ( int i = 0; i < 5; i++ ) {
		VK_PostSetOnce( sets[i] );
		VK_WritePostSet( *sets[i], targets[i]->view );
	}
	// the copy of the frame the scope reads, filtered as the other ones (its view has an alpha of 1)
	VK_PostSetOnce( &vk.screenSet );
	VK_WritePostSet( vk.screenSet, vk.screen.view );

	// The depth of the scene for the depth of field: a view of the depth without the stencil, read pixel for
	// pixel (not every device filters a depth format) while nothing draws into it.
	if ( vk.depthReadable ) {
		vk.depthView = VK_DepthOnlyView( &vk.depth );
		VK_PostSetOnce( &vk.depthSet );
		VK_WriteSet( vk.depthSet, vk.postSampler, vk.depthView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL );
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
	if ( vk.depthView ) {
		qvkDestroyImageView( vk.device, vk.depthView, NULL );
		vk.depthView = VK_NULL_HANDLE;
	}
	VK_DestroyTarget( &vk.sceneColor );
	VK_DestroyTarget( &vk.sceneColorMS );
	VK_DestroyTarget( &vk.sceneDepth );
	VK_DestroyTarget( &vk.sceneDepthMS );
	VK_DestroyTarget( &vk.sceneGlow );
	VK_DestroyTarget( &vk.sceneGlowMS );
	if ( vk.sceneDepthView ) {
		qvkDestroyImageView( vk.device, vk.sceneDepthView, NULL );
		vk.sceneDepthView = VK_NULL_HANDLE;
	}
	qvkDestroySampler( vk.device, vk.linearSampler, NULL );
}

/*
===============
VK_CreateSceneTargets

tay_renderScale: the targets the views of the level are drawn into at their own size, with the formats and the
samples of the ones of the frame, so every pipeline fits both. After VK_CreatePostTargets(), whose sampler the
glow set takes. qfalse: the device has no images that large.
===============
*/
qboolean VK_CreateSceneTargets( int width, int height ) {
	const VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	const VkImageAspectFlags depthAspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	const int largest = (int)vk.properties.limits.maxImageDimension2D;

	if ( width > largest || height > largest ) {
		ri.Printf( PRINT_WARNING, "tay_renderScale: %i x %i is larger than the device draws (%i), the level is drawn at the size of the window\n",
			width, height, largest );
		return qfalse;
	}
	VK_CreateTarget( &vk.sceneColor, width, height, vk.color.format, usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_SAMPLE_COUNT_1_BIT );
	VK_CreateTarget( &vk.sceneDepth, width, height, vk.depth.format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
		| ( vk.depthReadable ? VK_IMAGE_USAGE_SAMPLED_BIT : 0 ), depthAspect, VK_SAMPLE_COUNT_1_BIT );
	VK_CreateTarget( &vk.sceneGlow, width, height, vk.color.format, usage, VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT );
	if ( vk.samples > VK_SAMPLE_COUNT_1_BIT ) {
		VK_CreateTarget( &vk.sceneColorMS, width, height, vk.color.format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, vk.samples );
		VK_CreateTarget( &vk.sceneDepthMS, width, height, vk.depth.format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthAspect, vk.samples );
		VK_CreateTarget( &vk.sceneGlowMS, width, height, vk.color.format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, vk.samples );
	}

	// the frame is read pixel for pixel as the set of the gamma pass does, the glow filtered as the other one
	VK_PostSetOnce( &vk.scenePostSet );
	VK_WriteSet( vk.scenePostSet, vk.postSampler, vk.sceneColor.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_PostSetOnce( &vk.sceneGlowSet );
	VK_WritePostSet( vk.sceneGlowSet, vk.sceneGlow.view );
	VK_PostSetOnce( &vk.sceneCopySet );
	VK_WritePostSet( vk.sceneCopySet, vk.sceneColor.view );
	if ( vk.depthReadable ) {
		vk.sceneDepthView = VK_DepthOnlyView( &vk.sceneDepth );
		VK_PostSetOnce( &vk.sceneDepthSet );
		VK_WriteSet( vk.sceneDepthSet, vk.postSampler, vk.sceneDepthView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL );
	}
	return qtrue;
}

/*
===============
VK_SwapSceneTargets

The targets of the views of the level take the places of color, depth and glow, or give them back
===============
*/
void VK_SwapSceneTargets( void ) {
	vkTarget_t		target;
	VkDescriptorSet	set;
	VkImageView		view;

#define VK_SWAP( a, b, t )	( t = a, a = b, b = t )
	VK_SWAP( vk.color, vk.sceneColor, target );
	VK_SWAP( vk.colorMS, vk.sceneColorMS, target );
	VK_SWAP( vk.depth, vk.sceneDepth, target );
	VK_SWAP( vk.depthMS, vk.sceneDepthMS, target );
	VK_SWAP( vk.glow, vk.sceneGlow, target );
	VK_SWAP( vk.glowMS, vk.sceneGlowMS, target );
	VK_SWAP( vk.postSet, vk.scenePostSet, set );
	VK_SWAP( vk.glowSet, vk.sceneGlowSet, set );
	VK_SWAP( vk.depthSet, vk.sceneDepthSet, set );
	VK_SWAP( vk.depthView, vk.sceneDepthView, view );
#undef VK_SWAP
}
