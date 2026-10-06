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

// vk_post.cpp: the glow of r_DynamicGlow, the copy of the frame for refraction, and the depth test of flares
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

/*
===============================================================================

FLARES

A flare (the shine around a lamp) is drawn when nothing is in front of its point. rd-vanilla finds that out by
reading one pixel of the depth buffer while it draws. The commands of a frame are only recorded here, their
depth does not exist yet. So the pixel is copied to a buffer at the end of the view, and the answer is there
when the commands of that frame have run: two frames later, when its buffers are used again. Until then the
flare keeps the answer it had. A flare shows up or goes away two frames late.

===============================================================================
*/

typedef struct vkFlare_s {
	const void	*key;				// the origin of the flare surface in the level data
	qboolean	visible;			// the last answer
	int			asked;				// vk_flareFrame of the last question, the oldest one is given up
	int			probed;				// vk_flareFrame of the last probe: one in a frame is enough
} vkFlare_t;

static vkFlare_t	vk_flares[VK_MAX_FLARES];
static int			vk_numFlares;
static int			vk_flareFrame = 1;

// the level changed, the keys mean nothing any more
void VK_ClearFlares( void ) {
	vk_numFlares = 0;
	for ( int i = 0; i < VK_FRAMES; i++ ) {
		vk.frames[i].numProbes = vk.frames[i].numProbesCopied = 0;
	}
}

/*
===============
VK_FlareVisible

x, y: the pixel of the flare as OpenGL counts, y up. eyeZ: how far in front of the eye it is.
===============
*/
qboolean VK_FlareVisible( const float *point, int x, int y, float eyeZ ) {
	vkFrame_t *frame = vk.frame;
	vkFlare_t *flare = NULL;

	for ( int i = 0; i < vk_numFlares; i++ ) {
		if ( vk_flares[i].key == point ) {
			flare = &vk_flares[i];
			break;
		}
	}
	if ( !flare ) {
		if ( vk_numFlares < VK_MAX_FLARES ) {
			flare = &vk_flares[vk_numFlares++];
		} else {
			flare = &vk_flares[0];
			for ( int i = 1; i < VK_MAX_FLARES; i++ ) {
				if ( vk_flares[i].asked < flare->asked ) {
					flare = &vk_flares[i];
				}
			}
		}
		flare->key = point;
		flare->visible = qfalse;
		flare->probed = 0;
	}
	flare->asked = vk_flareFrame;

	// the glow pass draws over the same depth
	if ( frame && !g_bRenderGlowingObjects && flare->probed != vk_flareFrame && frame->numProbes < VK_MAX_FLARES ) {
		vkProbe_t *probe = &frame->probe[frame->numProbes++];

		probe->flare = (int)( flare - vk_flares );
		probe->x = Com_Clampi( 0, vk.width - 1, x );
		probe->y = Com_Clampi( 0, vk.height - 1, vk.height - 1 - y );
		probe->eyeZ = eyeZ;
		probe->projection[0] = backEnd.viewParms.projectionMatrix[10];
		probe->projection[1] = backEnd.viewParms.projectionMatrix[11];
		probe->projection[2] = backEnd.viewParms.projectionMatrix[14];
		flare->probed = vk_flareFrame;
	}
	return flare->visible;
}

/*
===============
VK_CopyProbes

The end of a view: the pixels of the depth its flares asked for go into the buffer of the frame
===============
*/
void VK_CopyProbes( void ) {
	static VkBufferImageCopy regions[VK_MAX_FLARES];
	vkFrame_t *frame = vk.frame;
	VkCommandBuffer cmd;
	uint32_t count = 0;

	if ( !frame || frame->numProbesCopied == frame->numProbes ) {
		return;
	}
	cmd = frame->cmd;
	memset( regions, 0, sizeof( regions[0] ) * ( frame->numProbes - frame->numProbesCopied ) );
	for ( int i = frame->numProbesCopied; i < frame->numProbes; i++, count++ ) {
		regions[count].bufferOffset = sizeof( uint32_t ) * i;
		regions[count].imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		regions[count].imageSubresource.layerCount = 1;
		regions[count].imageOffset.x = frame->probe[i].x;
		regions[count].imageOffset.y = frame->probe[i].y;
		regions[count].imageExtent.width = regions[count].imageExtent.height = regions[count].imageExtent.depth = 1;
	}
	frame->numProbesCopied = frame->numProbes;

	vkCmdEndRendering( cmd );
	VK_TargetLayout( cmd, &vk.depth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
	vkCmdCopyImageToBuffer( cmd, vk.depth.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, frame->probes.buffer, count, regions );
	VK_TargetLayout( cmd, &vk.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );
	VK_BeginTarget( cmd, &vk.color, &vk.depth, 2, qtrue );
	vk.boundPipeline = VK_NULL_HANDLE;
	vk.stateValid = qfalse;
}

/*
===============
VK_ReadProbes

The commands this frame recorded last have run. What they copied is the answer for the flares, with the test
RB_TestZFlare() of rd-vanilla makes.
===============
*/
void VK_ReadProbes( vkFrame_t *frame ) {
	const uint32_t *words = (const uint32_t *)frame->probes.mapped;

	for ( int i = 0; i < frame->numProbesCopied; i++ ) {
		const vkProbe_t *probe = &frame->probe[i];
		float depth, screenZ;

		// the depth aspect of a format with 24 bits comes in the low bits of a word, the other one as a float
		if ( vk.depth.format == VK_FORMAT_D24_UNORM_S8_UINT ) {
			depth = ( words[i] & 0xffffff ) / 16777215.0f;
		} else {
			memcpy( &depth, &words[i], sizeof( depth ) );
		}
		screenZ = probe->projection[2] / ( ( 2.0f * depth - 1.0f ) * probe->projection[1] - probe->projection[0] );
		vk_flares[probe->flare].visible = (qboolean)( probe->eyeZ + screenZ < 24.0f );
	}
	frame->numProbes = frame->numProbesCopied = 0;
	vk_flareFrame++;
}
