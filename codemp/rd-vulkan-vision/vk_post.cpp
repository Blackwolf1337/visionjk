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

// vk_post.cpp: the glow of r_DynamicGlow, and the copy of the frame for refraction
//
// The basic idea, as in rd-vanilla: render the glowing parts of the scene to a buffer of their own, blur that
// buffer, and add it back to the scene. The glowing parts are the stages of shaders that carry the "glow"
// keyword (saber blades, lights, screens). They are drawn a second time over the depth of the finished scene,
// so what is in front of them covers them. The blur is r_DynamicGlowPasses passes of four samples each at
// r_DynamicGlowWidth x r_DynamicGlowHeight, every pass r_DynamicGlowDelta further out.

#include "vk_local.h"

static void VK_WritePostSet( VkDescriptorSet set, VkImageView view ) {
	VkDescriptorImageInfo image = { vk.linearSampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

	write.dstSet = set;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &image;
	vkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
}

void VK_InitPost( void ) {
	const int width = Com_Clampi( 16, vk.width, r_DynamicGlowWidth->integer );
	const int height = Com_Clampi( 16, vk.height, r_DynamicGlowHeight->integer );
	const VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	VkSamplerCreateInfo sampler = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	VkDescriptorSetAllocateInfo alloc = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	VkDescriptorSet *sets[3] = { &vk.glowSet, &vk.blurSets[0], &vk.blurSets[1] };
	vkTarget_t *targets[3] = { &vk.glow, &vk.blur[0], &vk.blur[1] };

	VK_CreateTarget( &vk.glow, vk.width, vk.height, VK_FORMAT_R8G8B8A8_UNORM, usage, VK_IMAGE_ASPECT_COLOR_BIT );
	VK_CreateTarget( &vk.screen, vk.width, vk.height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT );
	vk.screenSlot = -1;
	VK_CreateTarget( &vk.blur[0], width, height, VK_FORMAT_R8G8B8A8_UNORM, usage, VK_IMAGE_ASPECT_COLOR_BIT );
	VK_CreateTarget( &vk.blur[1], width, height, VK_FORMAT_R8G8B8A8_UNORM, usage, VK_IMAGE_ASPECT_COLOR_BIT );

	sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
	sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	VK_CHECK( vkCreateSampler( vk.device, &sampler, NULL, &vk.linearSampler ) );

	alloc.descriptorPool = vk.descriptorPool;
	alloc.descriptorSetCount = 1;
	alloc.pSetLayouts = &vk.postSetLayout;
	for ( int i = 0; i < 3; i++ ) {
		VK_CHECK( vkAllocateDescriptorSets( vk.device, &alloc, sets[i] ) );
		VK_WritePostSet( *sets[i], targets[i]->view );
	}
}

void VK_ShutdownPost( void ) {
	VK_DestroyTarget( &vk.glow );
	VK_DestroyTarget( &vk.screen );
	VK_DestroyTarget( &vk.blur[0] );
	VK_DestroyTarget( &vk.blur[1] );
	vkDestroySampler( vk.device, vk.linearSampler, NULL );
}

/*
===============
VK_BeginGlow

The scene is drawn. What is drawn until VK_EndGlow() goes into the glow buffer, tested against the depth of
the scene.
===============
*/
void VK_BeginGlow( void ) {
	VkCommandBuffer cmd;

	if ( !vk.frame ) {
		return;
	}
	cmd = vk.frame->cmd;
	vkCmdEndRendering( cmd );
	VK_TargetLayout( cmd, &vk.glow, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	VK_BeginTarget( cmd, &vk.glow, &vk.depth, 1, qtrue );
	vk.boundPipeline = VK_NULL_HANDLE;
	vk.stateValid = qfalse;
}

static void VK_FullTarget( VkCommandBuffer cmd, const vkTarget_t *target ) {
	const VkViewport viewport = { 0.0f, 0.0f, (float)target->width, (float)target->height, 0.0f, 1.0f };
	const VkRect2D scissor = { { 0, 0 }, { (uint32_t)target->width, (uint32_t)target->height } };

	vkCmdSetViewport( cmd, 0, 1, &viewport );
	vkCmdSetScissor( cmd, 0, 1, &scissor );
}

/*
===============
VK_EndGlow

Blurs the glow buffer and adds it to the frame, then the frame is drawn into again
===============
*/
void VK_EndGlow( void ) {
	const int passes = Com_Clampi( 1, 16, r_DynamicGlowPasses->integer );
	const vkTarget_t *source = &vk.glow;
	VkDescriptorSet sourceSet = vk.glowSet;
	float offset = 0.1f;		// in pixels of the source
	VkCommandBuffer cmd;

	if ( !vk.frame ) {
		return;
	}
	cmd = vk.frame->cmd;
	vkCmdEndRendering( cmd );
	VK_TargetLayout( cmd, &vk.glow, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );

	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.blurPipeline );
	for ( int i = 0; i < passes; i++ ) {
		vkTarget_t *target = &vk.blur[i & 1];
		const float parm[4] = { offset / source->width, offset / source->height, r_DynamicGlowIntensity->value * 0.25f, 0.0f };

		VK_TargetLayout( cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
		VK_BeginTarget( cmd, target, NULL, 0, qfalse );
		VK_FullTarget( cmd, target );
		vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.postPipelineLayout, 0, 1, &sourceSet, 0, NULL );
		vkCmdPushConstants( cmd, vk.postPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( parm ), parm );
		vkCmdDraw( cmd, 3, 1, 0, 0 );
		vkCmdEndRendering( cmd );
		VK_TargetLayout( cmd, target, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );

		source = target;
		sourceSet = vk.blurSets[i & 1];
		// Increase the texel offsets. NOTE: This is possibly the most important input to the effect.
		offset += r_DynamicGlowDelta->value;
	}

	// back to the frame, with what is in it
	VK_BeginTarget( cmd, &vk.color, &vk.depth, 2, qtrue );
	if ( r_DynamicGlow->integer == 2 ) {
		// show the glow alone
		const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

		VK_ClearColor( 0, 0, vk.width, vk.height, black );
	}
	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.glowPipelines[r_DynamicGlowSoft->integer ? 1 : 0] );
	vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.postPipelineLayout, 0, 1, &sourceSet, 0, NULL );
	VK_FullTarget( cmd, &vk.color );
	vkCmdDraw( cmd, 3, 1, 0, 0 );

	// the shaders that draw geometry have other sets and other state
	VK_BindFrameSets();
	vk.boundPipeline = VK_NULL_HANDLE;
	vk.stateValid = qfalse;
	VK_SetViewport( vk.viewport[0], vk.viewport[1], vk.viewport[2], vk.viewport[3], 0.0f, 1.0f );
}

/*
===============
VK_CaptureFrame

Copies the frame as it is right now into vk.screen, which the stages of refraction surfaces then read through
the texture table in the place of tr.screenImage. rd-vanilla copies a square of the screen into that image
(glCopyTexImage2D), here the whole frame is copied and the draw is told where its square is.
===============
*/
void VK_CaptureFrame( void ) {
	VkCommandBuffer cmd;
	VkImageCopy region;

	if ( !vk.frame || !tr.screenImage ) {
		return;
	}
	cmd = vk.frame->cmd;
	vkCmdEndRendering( cmd );

	memset( &region, 0, sizeof( region ) );
	region.srcSubresource.aspectMask = region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.srcSubresource.layerCount = region.dstSubresource.layerCount = 1;
	region.extent.width = (uint32_t)vk.width;
	region.extent.height = (uint32_t)vk.height;
	region.extent.depth = 1;
	VK_TargetLayout( cmd, &vk.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
	VK_TargetLayout( cmd, &vk.screen, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
	vkCmdCopyImage( cmd, vk.color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk.screen.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );
	VK_TargetLayout( cmd, &vk.screen, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_TargetLayout( cmd, &vk.color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );

	// the entry of tr.screenImage in the texture table shows the copy from now on
	if ( vk.screenSlot != (int)tr.screenImage->texnum ) {
		VkDescriptorImageInfo image = { vk.linearSampler, vk.screen.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

		vk.screenSlot = (int)tr.screenImage->texnum;
		write.dstSet = vk.textureSet;
		write.dstArrayElement = (uint32_t)vk.screenSlot;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &image;
		vkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
	}

	VK_BeginTarget( cmd, &vk.color, &vk.depth, 2, qtrue );
	vk.boundPipeline = VK_NULL_HANDLE;
	vk.stateValid = qfalse;
}
