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

// vk_cmd.cpp: what a frame is for Vulkan: its command buffer, the targets, bindings, dynamic state and draw calls,
// the passes over whole targets, the way to the window and the way back into memory
//
// vk_frame.cpp and vk_post.cpp say what is drawn and in which order. This file records it. The commands of a
// frame go into its command buffer as they come and are submitted when the frame ends.

#include "vk_local.h"

static vkBuffer_t	vk_readback;		// where the device copies a finished frame for the CPU to read

void VK_InitFrames( void ) {
	VkCommandBufferAllocateInfo alloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	VkFenceCreateInfo fence = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	VkSemaphoreCreateInfo semaphore = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	VkDescriptorSetAllocateInfo set = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };

	alloc.commandPool = vk.commandPool;
	alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc.commandBufferCount = 1;
	fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	set.descriptorPool = vk.descriptorPool;
	set.descriptorSetCount = 1;
	set.pSetLayouts = &vk.frameSetLayout;

	for ( int i = 0; i < VK_FRAMES; i++ ) {
		vkFrame_t *frame = &vk.frames[i];
		VkDescriptorBufferInfo buffers[4];
		VkWriteDescriptorSet writes[4];

		VK_CHECK( qvkAllocateCommandBuffers( vk.device, &alloc, &frame->cmd ) );
		VK_CHECK( qvkCreateFence( vk.device, &fence, NULL, &frame->fence ) );
		VK_CHECK( qvkCreateSemaphore( vk.device, &semaphore, NULL, &frame->acquired ) );
		VK_CreateBuffer( &frame->vertexes, VK_VERTEX_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, qtrue );
		VK_CreateBuffer( &frame->indexes, VK_INDEX_BYTES, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, qtrue );
		VK_CreateBuffer( &frame->globals, sizeof( vkGlobals_t ), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, qtrue );
		VK_CreateBuffer( &frame->transforms, sizeof( vkTransform_t ) * VK_MAX_TRANSFORMS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, qtrue );
		VK_CreateBuffer( &frame->draws, sizeof( vkDraw_t ) * VK_MAX_DRAWS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, qtrue );
		VK_CreateBuffer( &frame->bones, sizeof( float ) * 12 * VK_MAX_BONES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, qtrue );
		VK_CreateBuffer( &frame->probes, sizeof( uint32_t ) * VK_MAX_FLARES, VK_BUFFER_USAGE_TRANSFER_DST_BIT, qtrue );
		frame->numProbes = frame->numProbesCopied = 0;

		VK_CHECK( qvkAllocateDescriptorSets( vk.device, &set, &frame->set ) );
		buffers[0].buffer = frame->globals.buffer;
		buffers[1].buffer = frame->transforms.buffer;
		buffers[2].buffer = frame->draws.buffer;
		buffers[3].buffer = frame->bones.buffer;
		memset( writes, 0, sizeof( writes ) );
		for ( uint32_t j = 0; j < 4; j++ ) {
			buffers[j].offset = 0;
			buffers[j].range = VK_WHOLE_SIZE;
			writes[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[j].dstSet = frame->set;
			writes[j].dstBinding = j;
			writes[j].descriptorCount = 1;
			writes[j].descriptorType = j ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writes[j].pBufferInfo = &buffers[j];
		}
		qvkUpdateDescriptorSets( vk.device, 4, writes, 0, NULL );
	}
}

void VK_ShutdownFrames( void ) {
	for ( int i = 0; i < VK_FRAMES; i++ ) {
		vkFrame_t *frame = &vk.frames[i];

		qvkDestroyFence( vk.device, frame->fence, NULL );
		qvkDestroySemaphore( vk.device, frame->acquired, NULL );
		VK_DestroyBuffer( &frame->vertexes );
		VK_DestroyBuffer( &frame->indexes );
		VK_DestroyBuffer( &frame->globals );
		VK_DestroyBuffer( &frame->transforms );
		VK_DestroyBuffer( &frame->probes );
		VK_DestroyBuffer( &frame->draws );
		VK_DestroyBuffer( &frame->bones );
	}
	VK_DestroyBuffer( &vk_readback );
	vk.frame = NULL;
}

// colorLoad: 0 what is in the target does not matter, 1 clear it, 2 keep it. depthLoad: keep the depth too.
static void VK_BeginRendering( VkCommandBuffer cmd, vkTarget_t *color, vkTarget_t *depth, int colorLoad, qboolean depthLoad ) {
	VkRenderingAttachmentInfo colorInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	VkRenderingAttachmentInfo depthInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	VkRenderingInfo info = { VK_STRUCTURE_TYPE_RENDERING_INFO };

	colorInfo.imageView = color->view;
	colorInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	// Anti aliasing: the scene and its glow are drawn into targets with several samples a pixel. The end of the
	// pass puts their average into the plain target, which is what everything else reads (the gamma pass, the
	// copy for refraction, the blur of the glow).
	if ( vk.samples > VK_SAMPLE_COUNT_1_BIT && ( color == &vk.color || color == &vk.glow ) ) {
		colorInfo.imageView = ( color == &vk.color ) ? vk.colorMS.view : vk.glowMS.view;
		colorInfo.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
		colorInfo.resolveImageView = color->view;
		colorInfo.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}
	colorInfo.loadOp = ( colorLoad == 1 ) ? VK_ATTACHMENT_LOAD_OP_CLEAR : ( colorLoad == 2 ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE );
	colorInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	info.renderArea.extent.width = (uint32_t)color->width;
	info.renderArea.extent.height = (uint32_t)color->height;
	info.layerCount = 1;
	info.colorAttachmentCount = 1;
	info.pColorAttachments = &colorInfo;
	if ( depth ) {
		depthInfo.imageView = depth->view;
		depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		if ( vk.samples > VK_SAMPLE_COUNT_1_BIT && depth == &vk.depth ) {
			// the plain depth gets the first sample of every pixel: the flares read it
			depthInfo.imageView = vk.depthMS.view;
			depthInfo.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
			depthInfo.resolveImageView = depth->view;
			depthInfo.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}
		depthInfo.loadOp = depthLoad ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
		// the glow pass draws over the depth of the scene
		depthInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depthInfo.clearValue.depthStencil.depth = 1.0f;
		info.pDepthAttachment = &depthInfo;
		info.pStencilAttachment = &depthInfo;
	}
	qvkCmdBeginRendering( cmd, &info );
}

/*
===============
VK_BeginTarget

The targets get the layout for drawing first, and with them the ones with several samples that stand in for them
===============
*/
void VK_BeginTarget( vkTarget_t *color, vkTarget_t *depth, int colorLoad, qboolean depthLoad ) {
	VkCommandBuffer cmd = vk.frame->cmd;

	VK_TargetLayout( cmd, color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	if ( depth ) {
		VK_TargetLayout( cmd, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );
	}
	if ( vk.samples > VK_SAMPLE_COUNT_1_BIT ) {
		if ( color == &vk.color ) {
			VK_TargetLayout( cmd, &vk.colorMS, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
		} else if ( color == &vk.glow ) {
			VK_TargetLayout( cmd, &vk.glowMS, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
		}
		if ( depth == &vk.depth ) {
			VK_TargetLayout( cmd, &vk.depthMS, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );
		}
	}
	VK_BeginRendering( cmd, color, depth, colorLoad, depthLoad );
	vk.boundPipeline = VK_NULL_HANDLE;
}

void VK_EndTarget( void ) {
	qvkCmdEndRendering( vk.frame->cmd );
}

/*
===============
VK_ViewportAndScissor

In the coordinates of OpenGL: y counts from the lower edge. The height of the viewport is negative, that turns
the picture the way OpenGL has it and keeps the winding of the triangles.
===============
*/
void VK_ViewportAndScissor( int x, int y, int width, int height, float minDepth, float maxDepth ) {
	VkViewport viewport;
	VkRect2D scissor;
	const int top = vk.height - ( y + height );
	const int left = Q_max( x, 0 ), up = Q_max( top, 0 );
	const int right = Q_min( x + width, vk.width ), down = Q_min( top + height, vk.height );

	viewport.x = (float)x;
	viewport.y = (float)( top + height );
	viewport.width = (float)width;
	viewport.height = -(float)height;
	viewport.minDepth = minDepth;
	viewport.maxDepth = maxDepth;
	scissor.offset.x = left;
	scissor.offset.y = up;
	scissor.extent.width = (uint32_t)Q_max( right - left, 0 );
	scissor.extent.height = (uint32_t)Q_max( down - up, 0 );
	qvkCmdSetViewport( vk.frame->cmd, 0, 1, &viewport );
	qvkCmdSetScissor( vk.frame->cmd, 0, 1, &scissor );
}

static void VK_ClearRect( int x, int y, int width, int height, const VkClearAttachment *attachment ) {
	VkClearRect rect;
	const int top = vk.height - ( y + height );
	const int left = Q_max( x, 0 ), up = Q_max( top, 0 );
	const int right = Q_min( x + width, vk.width ), down = Q_min( top + height, vk.height );

	if ( !vk.frame || right <= left || down <= up ) {
		return;
	}
	rect.rect.offset.x = left;
	rect.rect.offset.y = up;
	rect.rect.extent.width = (uint32_t)( right - left );
	rect.rect.extent.height = (uint32_t)( down - up );
	rect.baseArrayLayer = 0;
	rect.layerCount = 1;
	qvkCmdClearAttachments( vk.frame->cmd, 1, attachment, 1, &rect );
}

void VK_ClearDepth( int x, int y, int width, int height ) {
	VkClearAttachment attachment;

	memset( &attachment, 0, sizeof( attachment ) );
	attachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	attachment.clearValue.depthStencil.depth = 1.0f;
	VK_ClearRect( x, y, width, height, &attachment );
}

void VK_ClearColor( int x, int y, int width, int height, const float *color ) {
	VkClearAttachment attachment;

	memset( &attachment, 0, sizeof( attachment ) );
	attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	for ( int i = 0; i < 4; i++ ) {
		attachment.clearValue.color.float32[i] = color[i];
	}
	VK_ClearRect( x, y, width, height, &attachment );
}

// the buffers of the frame and the texture table for the shaders that draw geometry. Again after a pass over
// a whole target, which has a layout of its own.
void VK_BindFrameSets( void ) {
	VkDescriptorSet sets[2];

	if ( !vk.frame ) {
		return;
	}
	sets[0] = vk.frame->set;
	sets[1] = vk.textureSet;
	qvkCmdBindDescriptorSets( vk.frame->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipelineLayout, 0, 2, sets, 0, NULL );
}

/*
===============
VK_BeginCommands
===============
*/
void VK_BeginCommands( vkFrame_t *frame ) {
	VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };

	VK_CHECK( qvkWaitForFences( vk.device, 1, &frame->fence, VK_TRUE, UINT64_MAX ) );
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK( qvkResetCommandBuffer( frame->cmd, 0 ) );
	VK_CHECK( qvkBeginCommandBuffer( frame->cmd, &begin ) );
	vk.boundPipeline = VK_NULL_HANDLE;
	vk.boundVertexBuffer = vk.boundIndexBuffer = VK_NULL_HANDLE;
}

// How long a frame waits for an image of the swapchain, in nanoseconds. With r_swapInterval 1 that wait is what
// keeps the game in step with the display, so it is longer than a frame of a slow display. It must have an end:
// a window nobody sees (other workspace, minimized) gets its images back late or not at all, and the game has to
// go on then, with the server among other things. Seen with the NVIDIA driver, under Wayland and with its X11
// surface under Xwayland. After a wait that ran out the frames do not wait at all for VK_ACQUIRE_RETRY_MSEC, they
// are shown when an image happens to be free.
#define VK_ACQUIRE_TIMEOUT		50000000ull
#define VK_ACQUIRE_RETRY_MSEC	1000

// the swapchain image the frame goes to, qfalse when there is none right now (minimized, window changing, not shown)
static qboolean VK_AcquireImage( vkFrame_t *frame, uint32_t *imageIndex ) {
	for ( int attempt = 0; attempt < 2; attempt++ ) {
		VkResult result;

		if ( !vk.swapchain || VK_SwapIntervalChanged() ) {
			qvkDeviceWaitIdle( vk.device );
			VK_CreateSwapchain();
			if ( !vk.swapchain ) {
				return qfalse;
			}
		}
		const int now = ri.Milliseconds();
		const qboolean wait = (qboolean)( !vk.swapStallTime || now - vk.swapStallTime >= VK_ACQUIRE_RETRY_MSEC );

		result = qvkAcquireNextImageKHR( vk.device, vk.swapchain, wait ? VK_ACQUIRE_TIMEOUT : 0, frame->acquired, VK_NULL_HANDLE,
			imageIndex );
		if ( result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR ) {
			if ( wait ) {
				vk.swapStallTime = 0;
			}
			return qtrue;
		}
		if ( result == VK_TIMEOUT || result == VK_NOT_READY ) {
			// the frame is drawn all the same, a screenshot or a video frame is read from the targets
			if ( wait ) {
				vk.swapStallTime = now ? now : 1;
			}
			return qfalse;
		}
		if ( result != VK_ERROR_OUT_OF_DATE_KHR ) {
			VK_Fatal( "vkAcquireNextImageKHR", result );
		}
		qvkDeviceWaitIdle( vk.device );
		VK_CreateSwapchain();
	}
	return qfalse;
}

/*
===============
VK_PresentFrame

Gamma pass, copy to the window, submit, present
===============
*/
void VK_PresentFrame( const float *gamma ) {
	vkFrame_t *frame = vk.frame;
	VkCommandBuffer cmd = frame->cmd;
	VkViewport viewport = { 0.0f, 0.0f, (float)vk.width, (float)vk.height, 0.0f, 1.0f };
	VkRect2D scissor = { { 0, 0 }, { (uint32_t)vk.width, (uint32_t)vk.height } };
	VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	uint32_t imageIndex = 0;
	qboolean present;

	VK_TargetLayout( cmd, &vk.color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_TargetLayout( cmd, &vk.final, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	VK_BeginRendering( cmd, &vk.final, NULL, 0, qfalse );
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.gammaPipeline );
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.postPipelineLayout, 0, 1, &vk.postSet, 0, NULL );
	qvkCmdPushConstants( cmd, vk.postPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( float ) * 4, gamma );
	qvkCmdSetViewport( cmd, 0, 1, &viewport );
	qvkCmdSetScissor( cmd, 0, 1, &scissor );
	qvkCmdDraw( cmd, 3, 1, 0, 0 );
	qvkCmdEndRendering( cmd );
	VK_TargetLayout( cmd, &vk.final, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );

	present = VK_AcquireImage( frame, &imageIndex );
	if ( present ) {
		VkImageBlit blit;
		const qboolean sameSize = (qboolean)( (int)vk.swapExtent.width == vk.width && (int)vk.swapExtent.height == vk.height );

		memset( &blit, 0, sizeof( blit ) );
		blit.srcSubresource.aspectMask = blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.layerCount = blit.dstSubresource.layerCount = 1;
		blit.srcOffsets[1].x = vk.width;
		blit.srcOffsets[1].y = vk.height;
		blit.srcOffsets[1].z = 1;
		blit.dstOffsets[1].x = (int32_t)vk.swapExtent.width;
		blit.dstOffsets[1].y = (int32_t)vk.swapExtent.height;
		blit.dstOffsets[1].z = 1;
		VK_SetLayout( cmd, vk.swapImages[imageIndex], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
		qvkCmdBlitImage( cmd, vk.final.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk.swapImages[imageIndex],
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, sameSize ? VK_FILTER_NEAREST : VK_FILTER_LINEAR );
		VK_SetLayout( cmd, vk.swapImages[imageIndex], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );
	} else {
		vk.statFramesNotShown++;
	}
	VK_CHECK( qvkEndCommandBuffer( cmd ) );

	// textures this frame draws with have to be on the device before it runs
	VK_FlushUploads();

	VK_CHECK( qvkResetFences( vk.device, 1, &frame->fence ) );
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;
	if ( present ) {
		submit.waitSemaphoreCount = 1;
		submit.pWaitSemaphores = &frame->acquired;
		submit.pWaitDstStageMask = &waitStage;
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &vk.swapReady[imageIndex];
	}
	VK_CHECK( qvkQueueSubmit( vk.queue, 1, &submit, frame->fence ) );

	if ( present ) {
		VkPresentInfoKHR info = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		VkResult result;

		info.waitSemaphoreCount = 1;
		info.pWaitSemaphores = &vk.swapReady[imageIndex];
		info.swapchainCount = 1;
		info.pSwapchains = &vk.swapchain;
		info.pImageIndices = &imageIndex;
		result = qvkQueuePresentKHR( vk.queue, &info );
		if ( result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ) {
			qvkDeviceWaitIdle( vk.device );
			VK_CreateSwapchain();
		} else if ( result != VK_SUCCESS ) {
			VK_Fatal( "vkQueuePresentKHR", result );
		}
	}
}

/*
===============
VK_BindVertexBuffer, VK_BindIndexBuffer

Bind only when something else is bound: most batches follow one of their own kind. A vertex buffer is always
bound with the same offsets (the streams of the frame, the level, the models).
===============
*/
void VK_BindVertexBuffer( const vkBuffer_t *buffer, const vkSize_t *offsets, int numBindings ) {
	const VkBuffer buffers[4] = { buffer->buffer, buffer->buffer, buffer->buffer, buffer->buffer };

	if ( vk.boundVertexBuffer == buffer->buffer ) {
		return;
	}
	qvkCmdBindVertexBuffers( vk.frame->cmd, 0, (uint32_t)numBindings, buffers, offsets );
	vk.boundVertexBuffer = buffer->buffer;
}

void VK_BindVertexStream( int binding, const vkBuffer_t *buffer, vkSize_t offset ) {
	qvkCmdBindVertexBuffers( vk.frame->cmd, (uint32_t)binding, 1, &buffer->buffer, &offset );
}

void VK_BindIndexBuffer( const vkBuffer_t *buffer ) {
	if ( vk.boundIndexBuffer == buffer->buffer ) {
		return;
	}
	qvkCmdBindIndexBuffer( vk.frame->cmd, buffer->buffer, 0, VK_INDEX_TYPE_UINT32 );
	vk.boundIndexBuffer = buffer->buffer;
}

// the stencil state of OpenGL in the words of Vulkan
static VkStencilOp VK_StencilOp( int op ) {
	switch ( op ) {
	case GL_ZERO:		return VK_STENCIL_OP_ZERO;
	case GL_REPLACE:	return VK_STENCIL_OP_REPLACE;
	case GL_INCR:		return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
	case GL_DECR:		return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
	case GL_INCR_WRAP:	return VK_STENCIL_OP_INCREMENT_AND_WRAP;
	case GL_DECR_WRAP:	return VK_STENCIL_OP_DECREMENT_AND_WRAP;
	case GL_INVERT:		return VK_STENCIL_OP_INVERT;
	default:			return VK_STENCIL_OP_KEEP;
	}
}

static VkCompareOp VK_StencilFunc( int func ) {
	switch ( func ) {
	case GL_NEVER:		return VK_COMPARE_OP_NEVER;
	case GL_LESS:		return VK_COMPARE_OP_LESS;
	case GL_EQUAL:		return VK_COMPARE_OP_EQUAL;
	case GL_LEQUAL:		return VK_COMPARE_OP_LESS_OR_EQUAL;
	case GL_GREATER:	return VK_COMPARE_OP_GREATER;
	case GL_NOTEQUAL:	return VK_COMPARE_OP_NOT_EQUAL;
	case GL_GEQUAL:		return VK_COMPARE_OP_GREATER_OR_EQUAL;
	default:			return VK_COMPARE_OP_ALWAYS;
	}
}

/*
===============
VK_Draw

The pipeline for the program and the blend function, the dynamic state that changed, the draw calls
===============
*/
void VK_Draw( vkProgram_t program, uint32_t stateBits, uint32_t state, uint32_t changed, uint32_t drawIndex ) {
	VkCommandBuffer cmd = vk.frame->cmd;
	const VkCullModeFlags cullMode = ( state >> VKS_CULL_SHIFT ) & 3u;
	VkPipeline pipeline;

	pipeline = VK_FindPipeline( program, stateBits );
	if ( pipeline != vk.boundPipeline ) {
		qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
		vk.boundPipeline = pipeline;
	}

	if ( changed & ( 3u << VKS_CULL_SHIFT ) ) {
		qvkCmdSetCullMode( cmd, cullMode );
	}
	if ( changed & VKS_DEPTH_TEST ) {
		qvkCmdSetDepthTestEnable( cmd, ( state & VKS_DEPTH_TEST ) ? VK_TRUE : VK_FALSE );
	}
	if ( changed & VKS_DEPTH_WRITE ) {
		qvkCmdSetDepthWriteEnable( cmd, ( state & VKS_DEPTH_WRITE ) ? VK_TRUE : VK_FALSE );
	}
	if ( changed & ( VKS_DEPTH_EQUAL | VKS_DEPTH_LESS ) ) {
		qvkCmdSetDepthCompareOp( cmd, ( state & VKS_DEPTH_EQUAL ) ? VK_COMPARE_OP_EQUAL
			: ( ( state & VKS_DEPTH_LESS ) ? VK_COMPARE_OP_LESS : VK_COMPARE_OP_LESS_OR_EQUAL ) );
	}
	if ( changed & VKS_STENCIL ) {
		qvkCmdSetStencilTestEnable( cmd, ( state & VKS_STENCIL ) ? VK_TRUE : VK_FALSE );
	}
	if ( state & VKS_STENCIL ) {
		// only the shadows come here: what the test does is set every time
		for ( int face = 0; face < 2; face++ ) {
			qvkCmdSetStencilOp( cmd, face ? VK_STENCIL_FACE_BACK_BIT : VK_STENCIL_FACE_FRONT_BIT, VK_StencilOp( vk.stencil->ops[face][0] ),
				VK_StencilOp( vk.stencil->ops[face][2] ), VK_StencilOp( vk.stencil->ops[face][1] ), VK_StencilFunc( vk.stencil->func ) );
		}
		qvkCmdSetStencilReference( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, (uint32_t)vk.stencil->ref );
	}
	if ( changed & VKS_OFFSET ) {
		qvkCmdSetDepthBiasEnable( cmd, ( state & VKS_OFFSET ) ? VK_TRUE : VK_FALSE );
		// glPolygonOffset( factor, units ): the factor scales the slope, the units are the constant part
		qvkCmdSetDepthBias( cmd, r_offsetUnits->value, 0.0f, r_offsetFactor->value );
	}

	// the shaders learn which draw this is from the first instance of the call
	if ( vk.numRanges ) {
		// surfaces of models: their triangles are in an index buffer on the device
		for ( int i = 0; i < vk.numRanges; i++ ) {
			qvkCmdDrawIndexed( cmd, vk.ranges[i].count, 1, vk.ranges[i].first, 0, drawIndex );
		}
	} else {
		qvkCmdDrawIndexed( cmd, (uint32_t)vk.numBoundIndexes, 1, vk.firstIndex, vk.vertexOffset, drawIndex );
	}
}

/*
===============
VK_ReadFrame

The rows come as the image has them, the top one first
===============
*/
const byte *VK_ReadFrame( int x, int y, int width, int height ) {
	const VkDeviceSize frameBytes = (VkDeviceSize)vk.width * vk.height * 4;
	VkBufferImageCopy region;
	VkCommandBuffer cmd;

	VK_FlushUploads();
	qvkDeviceWaitIdle( vk.device );
	// the buffer stays for the next picture: a video asks for one every frame
	if ( vk_readback.size < frameBytes ) {
		VK_DestroyBuffer( &vk_readback );
		VK_CreateBuffer( &vk_readback, frameBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, qtrue );
	}
	memset( &region, 0, sizeof( region ) );
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageOffset.x = x;
	region.imageOffset.y = vk.height - ( y + height );
	region.imageExtent.width = (uint32_t)width;
	region.imageExtent.height = (uint32_t)height;
	region.imageExtent.depth = 1;
	cmd = VK_UploadCmd();
	qvkCmdCopyImageToBuffer( cmd, vk.final.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk_readback.buffer, 1, &region );
	VK_FlushUploads();
	return vk_readback.mapped;
}

/*
===============================================================================

PASSES OVER WHOLE TARGETS

===============================================================================
*/

static void VK_FullTarget( VkCommandBuffer cmd, const vkTarget_t *target ) {
	const VkViewport viewport = { 0.0f, 0.0f, (float)target->width, (float)target->height, 0.0f, 1.0f };
	const VkRect2D scissor = { { 0, 0 }, { (uint32_t)target->width, (uint32_t)target->height } };

	qvkCmdSetViewport( cmd, 0, 1, &viewport );
	qvkCmdSetScissor( cmd, 0, 1, &scissor );
}

// The descriptor set that shows a target to such a pass. The frame itself has the set of the gamma pass
// (VK_BlurFrame reads it): without that case its place in blurSets was worked out from an address that is not
// in vk.blur, and the driver got a handle that is none.
static VkDescriptorSet VK_PostSet( const vkTarget_t *source ) {
	if ( source == &vk.color ) {
		return vk.postSet;
	}
	if ( source == &vk.glow ) {
		return vk.glowSet;
	}
	if ( source == &vk.soft[0] || source == &vk.soft[1] ) {
		return vk.softSets[source - vk.soft];
	}
	if ( source == &vk.wide ) {
		return vk.wideSet;
	}
	return vk.blurSets[source - vk.blur];
}

// the pipeline stays bound from one pass of the blur to the next
void VK_BlurPass( vkTarget_t *source, vkTarget_t *target, const float *parm ) {
	VkCommandBuffer cmd = vk.frame->cmd;
	const VkDescriptorSet sourceSet = VK_PostSet( source );

	VK_TargetLayout( cmd, source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	if ( vk.boundPipeline != vk.blurPipeline ) {
		qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.blurPipeline );
		vk.boundPipeline = vk.blurPipeline;
	}
	VK_TargetLayout( cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	VK_BeginRendering( cmd, target, NULL, 0, qfalse );
	VK_FullTarget( cmd, target );
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.postPipelineLayout, 0, 1, &sourceSet, 0, NULL );
	qvkCmdPushConstants( cmd, vk.postPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( float ) * 4, parm );
	qvkCmdDraw( cmd, 3, 1, 0, 0 );
	qvkCmdEndRendering( cmd );
	VK_TargetLayout( cmd, target, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
}

// A pass that reads the depth of the scene next to its source. The depth is not drawn into while the pass reads
// it: the pass that follows takes it back (VK_BeginTarget).
static void VK_DepthPass( VkPipeline pipeline, VkPipelineLayout layout, vkTarget_t *source, vkTarget_t *target, const float *parm, int numbers ) {
	VkCommandBuffer cmd = vk.frame->cmd;
	const VkDescriptorSet sets[2] = { VK_PostSet( source ), vk.depthSet };

	VK_TargetLayout( cmd, source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_TargetLayout( cmd, &vk.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL );
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	vk.boundPipeline = pipeline;
	VK_TargetLayout( cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	VK_BeginRendering( cmd, target, NULL, 0, qfalse );
	VK_FullTarget( cmd, target );
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 2, sets, 0, NULL );
	qvkCmdPushConstants( cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( float ) * numbers, parm );
	qvkCmdDraw( cmd, 3, 1, 0, 0 );
	qvkCmdEndRendering( cmd );
	VK_TargetLayout( cmd, target, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
}

void VK_FocusPass( vkTarget_t *source, vkTarget_t *target, const float *parm ) {
	VK_DepthPass( vk.focusPipeline, vk.focusPipelineLayout, source, target, parm, 4 );
}

void VK_SpiritPass( vkTarget_t *source, vkTarget_t *target, const float *parm ) {
	VK_DepthPass( vk.spiritPipeline, vk.focusPipelineLayout, source, target, parm, 4 );
}

// NULL: only whether the device has the pass
qboolean VK_VolumePass( vkTarget_t *source, vkTarget_t *target, const float *parm ) {
	if ( !vk.depthReadable ) {
		return qfalse;
	}
	if ( parm ) {
		VK_DepthPass( vk.volumePipeline, vk.volumePipelineLayout, source, target, parm, 32 );
	}
	return qtrue;
}

// NULL: only whether the device has the pass
qboolean VK_TemplePass( const float *view ) {
	VkCommandBuffer cmd;

	if ( !view ) {
		return qtrue;
	}
	cmd = vk.frame->cmd;
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.templePipeline );
	vk.boundPipeline = vk.templePipeline;
	VK_FullTarget( cmd, &vk.color );
	qvkCmdPushConstants( cmd, vk.templePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( float ) * 20, view );
	qvkCmdDraw( cmd, 3, 1, 0, 0 );
	return qtrue;
}

void VK_AddTarget( vkTarget_t *source, vkAdd_t how ) {
	VkCommandBuffer cmd = vk.frame->cmd;
	const VkDescriptorSet sourceSet = VK_PostSet( source );

	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.glowPipelines[how] );
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.postPipelineLayout, 0, 1, &sourceSet, 0, NULL );
	VK_FullTarget( cmd, &vk.color );
	qvkCmdDraw( cmd, 3, 1, 0, 0 );
}

void VK_CopyTarget( vkTarget_t *source, vkTarget_t *dest ) {
	VkCommandBuffer cmd = vk.frame->cmd;
	VkImageCopy region;

	memset( &region, 0, sizeof( region ) );
	region.srcSubresource.aspectMask = region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.srcSubresource.layerCount = region.dstSubresource.layerCount = 1;
	region.extent.width = (uint32_t)source->width;
	region.extent.height = (uint32_t)source->height;
	region.extent.depth = 1;
	VK_TargetLayout( cmd, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
	VK_TargetLayout( cmd, dest, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
	qvkCmdCopyImage( cmd, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dest->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );
	VK_TargetLayout( cmd, dest, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_TargetLayout( cmd, source, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
}

// the pixels go into the buffer of the frame, one word of 32 bits for each probe
void VK_CopyProbeDepths( int first, int count ) {
	static VkBufferImageCopy regions[VK_MAX_FLARES];
	vkFrame_t *frame = vk.frame;
	VkCommandBuffer cmd = frame->cmd;
	uint32_t n = 0;

	memset( regions, 0, sizeof( regions[0] ) * count );
	for ( int i = first; i < first + count; i++, n++ ) {
		regions[n].bufferOffset = sizeof( uint32_t ) * i;
		regions[n].imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		regions[n].imageSubresource.layerCount = 1;
		regions[n].imageOffset.x = frame->probe[i].x;
		regions[n].imageOffset.y = frame->probe[i].y;
		regions[n].imageExtent.width = regions[n].imageExtent.height = regions[n].imageExtent.depth = 1;
	}
	VK_TargetLayout( cmd, &vk.depth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
	qvkCmdCopyImageToBuffer( cmd, vk.depth.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, frame->probes.buffer, n, regions );
	VK_TargetLayout( cmd, &vk.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );
}

float VK_ProbeDepth( const vkFrame_t *frame, int probe ) {
	const uint32_t *words = (const uint32_t *)frame->probes.mapped;
	float depth;

	// the depth aspect of a format with 24 bits comes in the low bits of a word, the other one as a float
	if ( vk.depth.format == VK_FORMAT_D24_UNORM_S8_UINT ) {
		depth = ( words[probe] & 0xffffff ) / 16777215.0f;
	} else {
		memcpy( &depth, &words[probe], sizeof( depth ) );
	}
	return depth;
}
