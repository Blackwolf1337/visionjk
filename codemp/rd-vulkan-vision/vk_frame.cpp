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

// vk_frame.cpp: one frame from its first draw to the window, and reading it back for screenshots
//
// The game draws into vk.color with vk.depth. At the end of the frame the gamma pass copies that into vk.final,
// vk.final is copied to the window. A frame has buffers of its own for what the CPU makes anew every frame
// (geometry from the tess arrays, transforms, draws). There are VK_FRAMES of them, so the CPU fills the next
// one while the GPU still works on the last.

#include "vk_local.h"

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

		VK_CHECK( vkAllocateCommandBuffers( vk.device, &alloc, &frame->cmd ) );
		VK_CHECK( vkCreateFence( vk.device, &fence, NULL, &frame->fence ) );
		VK_CHECK( vkCreateSemaphore( vk.device, &semaphore, NULL, &frame->acquired ) );
		VK_CHECK( vkCreateSemaphore( vk.device, &semaphore, NULL, &frame->rendered ) );
		VK_CreateBuffer( &frame->vertexes, VK_VERTEX_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, qtrue );
		VK_CreateBuffer( &frame->indexes, VK_INDEX_BYTES, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, qtrue );
		VK_CreateBuffer( &frame->globals, sizeof( vkGlobals_t ), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, qtrue );
		VK_CreateBuffer( &frame->transforms, sizeof( vkTransform_t ) * VK_MAX_TRANSFORMS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, qtrue );
		VK_CreateBuffer( &frame->draws, sizeof( vkDraw_t ) * VK_MAX_DRAWS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, qtrue );
		VK_CreateBuffer( &frame->bones, sizeof( float ) * 12 * VK_MAX_BONES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, qtrue );
		VK_CreateBuffer( &frame->probes, sizeof( uint32_t ) * VK_MAX_FLARES, VK_BUFFER_USAGE_TRANSFER_DST_BIT, qtrue );
		frame->numProbes = frame->numProbesCopied = 0;

		VK_CHECK( vkAllocateDescriptorSets( vk.device, &set, &frame->set ) );
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
		vkUpdateDescriptorSets( vk.device, 4, writes, 0, NULL );
	}
}

void VK_ShutdownFrames( void ) {
	for ( int i = 0; i < VK_FRAMES; i++ ) {
		vkFrame_t *frame = &vk.frames[i];

		vkDestroyFence( vk.device, frame->fence, NULL );
		vkDestroySemaphore( vk.device, frame->acquired, NULL );
		vkDestroySemaphore( vk.device, frame->rendered, NULL );
		VK_DestroyBuffer( &frame->vertexes );
		VK_DestroyBuffer( &frame->indexes );
		VK_DestroyBuffer( &frame->globals );
		VK_DestroyBuffer( &frame->transforms );
		VK_DestroyBuffer( &frame->probes );
		VK_DestroyBuffer( &frame->draws );
		VK_DestroyBuffer( &frame->bones );
	}
	vk.frame = NULL;
}

// colorLoad: 0 what is in the target does not matter, 1 clear it, 2 keep it. depthLoad: keep the depth too.
void VK_BeginTarget( VkCommandBuffer cmd, vkTarget_t *color, vkTarget_t *depth, int colorLoad, qboolean depthLoad ) {
	VkRenderingAttachmentInfo colorInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	VkRenderingAttachmentInfo depthInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	VkRenderingInfo info = { VK_STRUCTURE_TYPE_RENDERING_INFO };

	colorInfo.imageView = color->view;
	colorInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
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
		depthInfo.loadOp = depthLoad ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
		// the glow pass draws over the depth of the scene
		depthInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depthInfo.clearValue.depthStencil.depth = 1.0f;
		info.pDepthAttachment = &depthInfo;
		info.pStencilAttachment = &depthInfo;
	}
	vkCmdBeginRendering( cmd, &info );
}

/*
===============
VK_SetViewport

In the coordinates of OpenGL: y counts from the lower edge. The height of the viewport is negative, that turns
the picture the way OpenGL has it and keeps the winding of the triangles.
===============
*/
void VK_SetViewport( int x, int y, int width, int height, float minDepth, float maxDepth ) {
	VkViewport viewport;
	VkRect2D scissor;
	const int top = vk.height - ( y + height );
	const int left = Q_max( x, 0 ), up = Q_max( top, 0 );
	const int right = Q_min( x + width, vk.width ), down = Q_min( top + height, vk.height );

	if ( !vk.frame ) {
		return;
	}
	vk.viewport[0] = x;
	vk.viewport[1] = y;
	vk.viewport[2] = width;
	vk.viewport[3] = height;
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
	vkCmdSetViewport( vk.frame->cmd, 0, 1, &viewport );
	vkCmdSetScissor( vk.frame->cmd, 0, 1, &scissor );
}

// glDepthRange: the part of the depth buffer the following draws land in
void VK_SetDepthRange( float minDepth, float maxDepth ) {
	VK_SetViewport( vk.viewport[0], vk.viewport[1], vk.viewport[2], vk.viewport[3], minDepth, maxDepth );
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
	vkCmdClearAttachments( vk.frame->cmd, 1, attachment, 1, &rect );
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
	vkCmdBindDescriptorSets( vk.frame->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipelineLayout, 0, 2, sets, 0, NULL );
}

/*
===============
VK_BeginFrame
===============
*/
void VK_BeginFrame( void ) {
	VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	vkFrame_t *frame;
	vkGlobals_t *globals;

	if ( vk.frame || !vk.device ) {
		return;
	}
	frame = &vk.frames[vk.frameIndex];
	VK_CHECK( vkWaitForFences( vk.device, 1, &frame->fence, VK_TRUE, UINT64_MAX ) );
	// what the device wrote when these commands ran last can be read now
	VK_ReadProbes( frame );
	frame->vertexUsed = 0;
	frame->indexUsed = 0;
	frame->numTransforms = 0;
	frame->numDraws = 0;
	frame->numBones = 0;
	VK_ModelsBeginFrame();

	globals = (vkGlobals_t *)frame->globals.mapped;
	memset( globals, 0, sizeof( *globals ) );
	globals->gamma[0] = 1.0f / r_gamma->value;
	globals->gamma[1] = (float)( 1 << tr.overbrightBits );
	globals->gamma[2] = tr.identityLight;
	globals->screen[0] = (float)vk.width;
	globals->screen[1] = (float)vk.height;

	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK( vkResetCommandBuffer( frame->cmd, 0 ) );
	VK_CHECK( vkBeginCommandBuffer( frame->cmd, &begin ) );
	VK_TargetLayout( frame->cmd, &vk.color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	VK_TargetLayout( frame->cmd, &vk.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );
	VK_BeginTarget( frame->cmd, &vk.color, &vk.depth, 1, qfalse );

	vk.frame = frame;
	VK_BindFrameSets();
	vk.rendering = qtrue;
	vk.boundPipeline = VK_NULL_HANDLE;
	vk.stateValid = qfalse;
	vk.overflowed = qfalse;
	VK_SetViewport( 0, 0, vk.width, vk.height, 0.0f, 1.0f );
}

// the swapchain image the frame goes to, qfalse when there is none right now (minimized, window changing)
static qboolean VK_AcquireImage( vkFrame_t *frame, uint32_t *imageIndex ) {
	for ( int attempt = 0; attempt < 2; attempt++ ) {
		VkResult result;

		if ( !vk.swapchain || VK_SwapIntervalChanged() ) {
			vkDeviceWaitIdle( vk.device );
			VK_CreateSwapchain();
			if ( !vk.swapchain ) {
				return qfalse;
			}
		}
		result = vkAcquireNextImageKHR( vk.device, vk.swapchain, UINT64_MAX, frame->acquired, VK_NULL_HANDLE, imageIndex );
		if ( result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR ) {
			return qtrue;
		}
		if ( result != VK_ERROR_OUT_OF_DATE_KHR ) {
			VK_Fatal( "vkAcquireNextImageKHR", result );
		}
		vkDeviceWaitIdle( vk.device );
		VK_CreateSwapchain();
	}
	return qfalse;
}

/*
===============
VK_EndFrame

Gamma pass, copy to the window, submit, present
===============
*/
void VK_EndFrame( void ) {
	vkFrame_t *frame = vk.frame;
	VkCommandBuffer cmd;
	VkViewport viewport = { 0.0f, 0.0f, (float)vk.width, (float)vk.height, 0.0f, 1.0f };
	VkRect2D scissor = { { 0, 0 }, { (uint32_t)vk.width, (uint32_t)vk.height } };
	const float gamma[4] = { 1.0f / r_gamma->value, (float)( 1 << tr.overbrightBits ), 0.0f, 0.0f };
	VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	uint32_t imageIndex = 0;
	qboolean present;

	if ( !frame ) {
		return;
	}
	cmd = frame->cmd;
	vkCmdEndRendering( cmd );
	vk.rendering = qfalse;

	VK_TargetLayout( cmd, &vk.color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_TargetLayout( cmd, &vk.final, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	VK_BeginTarget( cmd, &vk.final, NULL, 0, qfalse );
	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.gammaPipeline );
	vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.postPipelineLayout, 0, 1, &vk.postSet, 0, NULL );
	vkCmdPushConstants( cmd, vk.postPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( gamma ), gamma );
	vkCmdSetViewport( cmd, 0, 1, &viewport );
	vkCmdSetScissor( cmd, 0, 1, &scissor );
	vkCmdDraw( cmd, 3, 1, 0, 0 );
	vkCmdEndRendering( cmd );
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
		vkCmdBlitImage( cmd, vk.final.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk.swapImages[imageIndex],
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, sameSize ? VK_FILTER_NEAREST : VK_FILTER_LINEAR );
		VK_SetLayout( cmd, vk.swapImages[imageIndex], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );
	}
	VK_CHECK( vkEndCommandBuffer( cmd ) );

	// textures this frame draws with have to be on the device before it runs
	VK_FlushUploads();

	VK_CHECK( vkResetFences( vk.device, 1, &frame->fence ) );
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;
	if ( present ) {
		submit.waitSemaphoreCount = 1;
		submit.pWaitSemaphores = &frame->acquired;
		submit.pWaitDstStageMask = &waitStage;
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &frame->rendered;
	}
	VK_CHECK( vkQueueSubmit( vk.queue, 1, &submit, frame->fence ) );

	if ( present ) {
		VkPresentInfoKHR info = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		VkResult result;

		info.waitSemaphoreCount = 1;
		info.pWaitSemaphores = &frame->rendered;
		info.swapchainCount = 1;
		info.pSwapchains = &vk.swapchain;
		info.pImageIndices = &imageIndex;
		result = vkQueuePresentKHR( vk.queue, &info );
		if ( result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ) {
			vkDeviceWaitIdle( vk.device );
			VK_CreateSwapchain();
		} else if ( result != VK_SUCCESS ) {
			VK_Fatal( "vkQueuePresentKHR", result );
		}
	}

	vk.statDraws[1] = frame->numDraws;
	vk.statCpuVertexes[1] = vk.statCpuVertexes[0];
	vk.statCpuIndexes[1] = vk.statCpuIndexes[0];
	vk.statStaticIndexes[1] = vk.statStaticIndexes[0];
	vk.statNumCpuShaders[1] = vk.statNumCpuShaders[0];
	memcpy( vk.statCpuShaders[1], vk.statCpuShaders[0], sizeof( vk.statCpuShaders[0][0] ) * vk.statNumCpuShaders[0] );
	vk.statCpuVertexes[0] = vk.statCpuIndexes[0] = vk.statStaticIndexes[0] = vk.statNumCpuShaders[0] = 0;

	vk.finalValid = qtrue;
	vk.frame = NULL;
	vk.frameIndex = ( vk.frameIndex + 1 ) % VK_FRAMES;
}

/*
===============
VK_AddTransform

Returns the index the draws refer to
===============
*/
int VK_AddTransform( const vkTransform_t *transform ) {
	vkFrame_t *frame = vk.frame;

	if ( !frame ) {
		return 0;
	}
	if ( frame->numTransforms == VK_MAX_TRANSFORMS ) {
		return VK_MAX_TRANSFORMS - 1;
	}
	memcpy( frame->transforms.mapped + sizeof( vkTransform_t ) * frame->numTransforms, transform, sizeof( *transform ) );
	return frame->numTransforms++;
}

/*
===============
VK_UploadGeometry

Copies vertices and indexes into the buffers of the frame and binds them. The arrays have the layout of the
tess arrays. What is drawn with VK_DrawTess() after this comes from there. qfalse: no room, nothing to draw.
===============
*/
qboolean VK_UploadGeometry( const vec4_t *xyz, const vec4_t *normal, const vec2_t ( *texCoords )[NUM_TEX_COORDS],
	const color4ub_t *colors, int numVerts, const glIndex_t *indexes, int numIndexes ) {
	vkFrame_t *frame = vk.frame;
	const VkDeviceSize numVertexes = (VkDeviceSize)numVerts;
	const VkDeviceSize sizes[4] = { numVertexes * sizeof( vec4_t ), numVertexes * sizeof( vec4_t ),
		numVertexes * sizeof( vec2_t ) * NUM_TEX_COORDS, numVertexes * 4 };
	const void *sources[4] = { xyz, normal, texCoords, colors };
	const VkDeviceSize indexBytes = (VkDeviceSize)numIndexes * sizeof( glIndex_t );
	VkDeviceSize vertexBytes = 0;
	VkBuffer buffers[4];
	VkDeviceSize offsets[4];

	if ( !frame || !numIndexes || !numVerts ) {
		return qfalse;
	}
	for ( int i = 0; i < 4; i++ ) {
		vertexBytes += ( sizes[i] + 15 ) & ~(VkDeviceSize)15;
	}
	if ( frame->vertexUsed + vertexBytes > VK_VERTEX_BYTES || frame->indexUsed + indexBytes > VK_INDEX_BYTES ) {
		if ( !vk.overflowed ) {
			vk.overflowed = qtrue;
			ri.Printf( PRINT_DEVELOPER, "VK_UploadGeometry: the frame has more geometry than fits, the rest is not drawn\n" );
		}
		return qfalse;
	}
	for ( int i = 0; i < 4; i++ ) {
		memcpy( frame->vertexes.mapped + frame->vertexUsed, sources[i], (size_t)sizes[i] );
		buffers[i] = frame->vertexes.buffer;
		offsets[i] = frame->vertexUsed;
		frame->vertexUsed += ( sizes[i] + 15 ) & ~(VkDeviceSize)15;
	}
	memcpy( frame->indexes.mapped + frame->indexUsed, indexes, (size_t)indexBytes );
	vk.tessColorOffset = offsets[3];
	vkCmdBindVertexBuffers( frame->cmd, 0, 4, buffers, offsets );
	vkCmdBindIndexBuffer( frame->cmd, frame->indexes.buffer, frame->indexUsed, VK_INDEX_TYPE_UINT32 );
	frame->indexUsed += indexBytes;
	vk.numBoundIndexes = numIndexes;
	vk.statCpuVertexes[0] += numVerts;
	vk.statCpuIndexes[0] += numIndexes;
	return qtrue;
}

/*
===============
VK_UploadIndexes

Indexes into vertices that are in a buffer on the device already: offsets are where its vertex streams start
(xyz, normal, texCoords and colors of the level, the one stream of the models)
===============
*/
qboolean VK_UploadIndexes( VkBuffer vertexBuffer, const VkDeviceSize *offsets, int numBindings, const glIndex_t *indexes, int numIndexes ) {
	vkFrame_t *frame = vk.frame;
	const VkDeviceSize indexBytes = (VkDeviceSize)numIndexes * sizeof( glIndex_t );
	const VkBuffer buffers[4] = { vertexBuffer, vertexBuffer, vertexBuffer, vertexBuffer };

	if ( !frame || !numIndexes || !vertexBuffer || frame->indexUsed + indexBytes > VK_INDEX_BYTES ) {
		return qfalse;
	}
	memcpy( frame->indexes.mapped + frame->indexUsed, indexes, (size_t)indexBytes );
	vkCmdBindVertexBuffers( frame->cmd, 0, (uint32_t)numBindings, buffers, offsets );
	vkCmdBindIndexBuffer( frame->cmd, frame->indexes.buffer, frame->indexUsed, VK_INDEX_TYPE_UINT32 );
	frame->indexUsed += indexBytes;
	vk.numBoundIndexes = numIndexes;
	vk.statStaticIndexes[0] += numIndexes;
	return qtrue;
}

// the batch in the tess arrays
qboolean VK_UploadTess( void ) {
	vkStatShader_t *stat = vk.statCpuShaders[0];
	int i;

	// for gfxinfo: which shaders still take this way
	for ( i = 0; i < vk.statNumCpuShaders[0] && stat[i].shader != tess.shader; i++ ) {
	}
	if ( i < VK_MAX_STAT_SHADERS ) {
		if ( i == vk.statNumCpuShaders[0] ) {
			stat[i].shader = tess.shader;
			Q_strncpyz( stat[i].name, tess.shader->name, sizeof( stat[i].name ) );
			stat[i].indexes = 0;
			vk.statNumCpuShaders[0]++;
		}
		stat[i].indexes += tess.numIndexes;
	}
	return VK_UploadGeometry( tess.xyz, tess.normal, tess.texCoords, tess.vertexColors, tess.numVertexes,
		tess.indexes, tess.numIndexes );
}

/*
===============
VK_TessColors

A stage whose colors were computed on the CPU draws with them in the place of the vertex colors. NULL: back
to the vertex colors VK_UploadTess() put there.
===============
*/
void VK_TessColors( const color4ub_t *colors ) {
	vkFrame_t *frame = vk.frame;
	const VkDeviceSize size = (VkDeviceSize)tess.numVertexes * 4;
	VkDeviceSize offset = vk.tessColorOffset;

	if ( !frame ) {
		return;
	}
	if ( colors ) {
		if ( frame->vertexUsed + size > VK_VERTEX_BYTES ) {
			return;
		}
		offset = frame->vertexUsed;
		memcpy( frame->vertexes.mapped + offset, colors, (size_t)size );
		frame->vertexUsed += ( size + 15 ) & ~(VkDeviceSize)15;
	}
	vkCmdBindVertexBuffers( frame->cmd, 3, 1, &frame->vertexes.buffer, &offset );
}

// dynamic state as one word, so that a draw only sets what differs from the draw before it
#define VKS_DEPTH_TEST		0x01u
#define VKS_DEPTH_WRITE		0x02u
#define VKS_DEPTH_EQUAL		0x04u
#define VKS_OFFSET			0x08u
#define VKS_CULL_SHIFT		4		// VkCullModeFlags, 2 bits

/*
===============
VK_DrawTess

One stage of a shader on the geometry that was uploaded last. cullMode is a VkCullModeFlags value.
===============
*/
void VK_DrawTess( const vkDraw_t *draw, vkProgram_t program, uint32_t stateBits, int cullMode, qboolean polygonOffset ) {
	vkFrame_t *frame = vk.frame;
	VkCommandBuffer cmd;
	VkPipeline pipeline;
	uint32_t state, changed, drawIndex;

	if ( !frame || frame->numDraws == VK_MAX_DRAWS ) {
		return;
	}
	cmd = frame->cmd;

	pipeline = VK_FindPipeline( program, stateBits );
	if ( pipeline != vk.boundPipeline ) {
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
		vk.boundPipeline = pipeline;
	}

	state = ( (uint32_t)cullMode << VKS_CULL_SHIFT );
	if ( !( stateBits & GLS_DEPTHTEST_DISABLE ) ) {
		state |= VKS_DEPTH_TEST;
	}
	if ( stateBits & GLS_DEPTHMASK_TRUE ) {
		state |= VKS_DEPTH_WRITE;
	}
	if ( stateBits & GLS_DEPTHFUNC_EQUAL ) {
		state |= VKS_DEPTH_EQUAL;
	}
	if ( polygonOffset ) {
		state |= VKS_OFFSET;
	}
	changed = vk.stateValid ? ( state ^ vk.boundState ) : ~0u;
	if ( changed & ( 3u << VKS_CULL_SHIFT ) ) {
		vkCmdSetCullMode( cmd, (VkCullModeFlags)cullMode );
	}
	if ( changed & VKS_DEPTH_TEST ) {
		vkCmdSetDepthTestEnable( cmd, ( state & VKS_DEPTH_TEST ) ? VK_TRUE : VK_FALSE );
	}
	if ( changed & VKS_DEPTH_WRITE ) {
		vkCmdSetDepthWriteEnable( cmd, ( state & VKS_DEPTH_WRITE ) ? VK_TRUE : VK_FALSE );
	}
	if ( changed & VKS_DEPTH_EQUAL ) {
		vkCmdSetDepthCompareOp( cmd, ( state & VKS_DEPTH_EQUAL ) ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_LESS_OR_EQUAL );
	}
	if ( changed & VKS_OFFSET ) {
		vkCmdSetDepthBiasEnable( cmd, ( state & VKS_OFFSET ) ? VK_TRUE : VK_FALSE );
		// glPolygonOffset( factor, units ): the factor scales the slope, the units are the constant part
		vkCmdSetDepthBias( cmd, r_offsetUnits->value, 0.0f, r_offsetFactor->value );
	}
	vk.boundState = state;
	vk.stateValid = qtrue;

	drawIndex = (uint32_t)frame->numDraws++;
	memcpy( frame->draws.mapped + sizeof( vkDraw_t ) * drawIndex, draw, sizeof( *draw ) );
	vkCmdPushConstants( cmd, vk.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
		sizeof( drawIndex ), &drawIndex );
	vkCmdDrawIndexed( cmd, (uint32_t)vk.numBoundIndexes, 1, 0, 0, 0 );
}

/*
===============
RB_ReadPixels

The frame that was shown last, as tr_init.cpp wants it for its screenshot commands: RGB, three bytes a pixel,
the lowest row first. Free the result with Hunk_FreeTempMemory().
===============
*/
byte *RB_ReadPixels( int x, int y, int width, int height, size_t *offset, int *padlen ) {
	byte *buffer = (byte *)Hunk_AllocateTempMemory( width * height * 3 + (int)*offset );
	byte *out = buffer + *offset;
	vkBuffer_t readback;
	VkBufferImageCopy region;
	VkCommandBuffer cmd;

	*padlen = 0;
	memset( out, 0, (size_t)width * height * 3 );
	if ( !vk.device || !vk.finalValid || x < 0 || y < 0 || x + width > vk.width || y + height > vk.height ) {
		return buffer;
	}

	VK_FlushUploads();
	vkDeviceWaitIdle( vk.device );
	VK_CreateBuffer( &readback, (VkDeviceSize)width * height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, qtrue );
	memset( &region, 0, sizeof( region ) );
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageOffset.x = x;
	region.imageOffset.y = vk.height - ( y + height );
	region.imageExtent.width = (uint32_t)width;
	region.imageExtent.height = (uint32_t)height;
	region.imageExtent.depth = 1;
	cmd = VK_UploadCmd();
	vkCmdCopyImageToBuffer( cmd, vk.final.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &region );
	VK_FlushUploads();

	for ( int row = 0; row < height; row++ ) {
		const byte *in = readback.mapped + (size_t)( height - 1 - row ) * width * 4;

		for ( int i = 0; i < width; i++, in += 4, out += 3 ) {
			out[0] = in[0];
			out[1] = in[1];
			out[2] = in[2];
		}
	}
	VK_DestroyBuffer( &readback );
	return buffer;
}

/*
===============
RB_TakeVideoFrameCmd

Recording to an AVI file: not done in this renderer yet
===============
*/
const void *RB_TakeVideoFrameCmd( const void *data ) {
	return (const void *)( (const videoFrameCommand_t *)data + 1 );
}
