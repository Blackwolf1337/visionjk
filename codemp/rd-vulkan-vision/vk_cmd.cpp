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

/*
===============================================================================

V_GPUTIMES

How long the device takes for the parts of a frame: a timestamp at every VK_GpuMark(), the time from one mark to
the next added to the name of the later one. A name marked several times in a frame (the level of a portal, of
the puddle view and of the main view) gets the sum. v_gpuTimes prints the averages since it was called last.

===============================================================================
*/

#define VK_GPU_NAMES	32

static struct {
	const char	*name;
	double		ms;				// summed over the frames
} vk_gpuNames[VK_GPU_NAMES];
static int		vk_numGpuNames;
static double	vk_gpuFrameMs;		// from the first mark of a frame to its last, summed
static int		vk_gpuFrames;
static double	vk_gpuFragments;	// runs of the fragment shader for the level, summed over the frames that counted them
static int		vk_gpuFragmentFrames;

// the GPU profiler of the client (ext.GpuProfile): the same sums over windows of about half a second, apart from
// those of v_gpuTimes, and the whole frame of each of the last frames
#define VK_PROFILE_MSEC	500

static struct {
	const char	*name;
	double		ms, maxMs;
} vk_profNames[GPU_PROFILE_PASSES];
static int			vk_numProfNames;
static double		vk_profFrameMs, vk_profFrameMaxMs;
static int			vk_profFrames, vk_profStart;
static double		vk_profFragments;
static int			vk_profFragmentFrames;
static float		vk_profHistory[GPU_PROFILE_HISTORY];
static int			vk_profHistoryAt;
static gpuProfile_t	vk_profile;			// the last window closed

static qboolean VK_GpuTimed( void ) {
	return (qboolean)( vk.properties.limits.timestampComputeAndGraphics && vk.properties.limits.timestampPeriod > 0.0f );
}

static void VK_GpuAdd( const char *name, double ms ) {
	int i;

	for ( i = 0; i < vk_numGpuNames; i++ ) {
		if ( vk_gpuNames[i].name == name || !strcmp( vk_gpuNames[i].name, name ) ) {
			break;
		}
	}
	if ( i == vk_numGpuNames ) {
		if ( vk_numGpuNames == VK_GPU_NAMES ) {
			return;
		}
		vk_gpuNames[vk_numGpuNames].name = name;
		vk_gpuNames[vk_numGpuNames++].ms = 0.0;
	}
	vk_gpuNames[i].ms += ms;
}

// the window of the profiler closes: its averages become what ext.GpuProfile hands out
static void VK_ProfileClose( void ) {
	gpuProfile_t *p = &vk_profile;

	p->serial++;
	p->frames = vk_profFrames;
	p->width = vk.width;
	p->height = vk.height;
	p->samples = (int)vk.samples;
	Q_strncpyz( p->device, vk.properties.deviceName, sizeof( p->device ) );
	p->numPasses = vk_numProfNames;
	for ( int i = 0; i < vk_numProfNames; i++ ) {
		Q_strncpyz( p->passNames[i], vk_profNames[i].name, sizeof( p->passNames[i] ) );
		p->passMs[i] = (float)( vk_profNames[i].ms / vk_profFrames );
		p->passMaxMs[i] = (float)vk_profNames[i].maxMs;
	}
	p->frameMs = (float)( vk_profFrameMs / vk_profFrames );
	p->frameMaxMs = (float)vk_profFrameMaxMs;
	p->levelOverdraw = vk_profFragmentFrames && vk.width > 0 && vk.height > 0
		? (float)( vk_profFragments / vk_profFragmentFrames / ( (double)vk.width * vk.height ) ) : 0.0f;
	vk_profFragments = 0.0;
	vk_profFragmentFrames = 0;
	// (a pass that ended takes no row any more; the others start again from nothing)
	vk_numProfNames = 0;
	vk_profFrameMs = vk_profFrameMaxMs = 0.0;
	vk_profFrames = 0;
}

// a finished frame into the window of the profiler
static void VK_ProfileFrame( const vkFrame_t *frame, const uint64_t *ticks, double msPerTick ) {
	const int	now = ri.Milliseconds();
	const double whole = (double)( ticks[frame->numMarks - 1] - ticks[0] ) * msPerTick;

	for ( int i = 1; i < frame->numMarks; i++ ) {
		const double	ms = (double)( ticks[i] - ticks[i - 1] ) * msPerTick;
		const char		*name = frame->markNames[i];
		int				n;

		for ( n = 0; n < vk_numProfNames; n++ ) {
			if ( vk_profNames[n].name == name || !strcmp( vk_profNames[n].name, name ) ) {
				break;
			}
		}
		if ( n == vk_numProfNames ) {
			if ( n == GPU_PROFILE_PASSES ) {
				continue;
			}
			vk_profNames[n].name = name;
			vk_profNames[n].ms = vk_profNames[n].maxMs = 0.0;
			vk_numProfNames++;
		}
		vk_profNames[n].ms += ms;
		// (a name marked several times in a frame: its most is that of one piece, near enough)
		vk_profNames[n].maxMs = Q_max( vk_profNames[n].maxMs, ms );
	}
	vk_profFrameMs += whole;
	vk_profFrameMaxMs = Q_max( vk_profFrameMaxMs, whole );
	vk_profHistory[vk_profHistoryAt] = (float)whole;
	vk_profHistoryAt = ( vk_profHistoryAt + 1 ) % GPU_PROFILE_HISTORY;
	if ( !vk_profFrames++ ) {
		vk_profStart = now;
	}
	if ( now - vk_profStart >= VK_PROFILE_MSEC ) {
		VK_ProfileClose();
	}
}

qboolean RE_GpuProfile( gpuProfile_t *out ) {
	if ( !out || !vk.device || !VK_GpuTimed() ) {
		return qfalse;
	}
	*out = vk_profile;
	for ( int i = 0; i < GPU_PROFILE_HISTORY; i++ ) {
		out->history[i] = vk_profHistory[( vk_profHistoryAt + i ) % GPU_PROFILE_HISTORY];
	}
	out->draws = vk.statDraws[1];
	out->triangles = vk.statStaticIndexes[1] / 3;
	out->cpuTriangles = vk.statCpuIndexes[1] / 3;
	out->transforms = vk.statTransforms[1];
	out->bones = vk.statBones[1];
	out->skeletons = vk.statSkeletons[1];
	return qtrue;
}

// the marks of the frame the device finished, into the sums
static void VK_GpuRead( vkFrame_t *frame ) {
	uint64_t ticks[VK_GPU_MARKS];
	const double msPerTick = (double)vk.properties.limits.timestampPeriod / 1000000.0;

	if ( frame->fragments && frame->fragmentsUsed ) {
		uint64_t count = 0;

		if ( qvkGetQueryPoolResults( vk.device, frame->fragments, 0, 1, sizeof( count ), &count, sizeof( count ), VK_QUERY_RESULT_64_BIT )
			== VK_SUCCESS ) {
			vk_gpuFragments += (double)count;
			vk_gpuFragmentFrames++;
			vk_profFragments += (double)count;
			vk_profFragmentFrames++;
		}
	}

	if ( !frame->timestamps || frame->numMarks < 2 ) {
		return;
	}
	if ( qvkGetQueryPoolResults( vk.device, frame->timestamps, 0, (uint32_t)frame->numMarks, sizeof( ticks ), ticks, sizeof( uint64_t ),
		VK_QUERY_RESULT_64_BIT ) != VK_SUCCESS ) {
		return;
	}
	for ( int i = 1; i < frame->numMarks; i++ ) {
		VK_GpuAdd( frame->markNames[i], (double)( ticks[i] - ticks[i - 1] ) * msPerTick );
	}
	vk_gpuFrameMs += (double)( ticks[frame->numMarks - 1] - ticks[0] ) * msPerTick;
	vk_gpuFrames++;
	VK_ProfileFrame( frame, ticks, msPerTick );
}

/*
===============
VK_LevelFragments

How often the fragment shader runs while the opaque level of the main view is drawn, against the pixels of the frame:
its overdraw (v_gpuTimes, v_profiler). Counted inside the render pass of the level, which every pass that leaves
it ends first (VK_LevelFragments( qfalse ) is called before that, and the count stops there).
===============
*/
void VK_LevelFragments( qboolean begin ) {
	vkFrame_t *frame = vk.frame;

	if ( !frame || !frame->fragments ) {
		return;
	}
	if ( begin ) {
		if ( !frame->fragmentsActive && !frame->fragmentsUsed ) {
			qvkCmdBeginQuery( frame->cmd, frame->fragments, 0, 0 );
			frame->fragmentsActive = qtrue;
		}
	} else if ( frame->fragmentsActive ) {
		qvkCmdEndQuery( frame->cmd, frame->fragments, 0 );
		frame->fragmentsActive = qfalse;
		frame->fragmentsUsed = qtrue;
	}
}

// no mark of the frame yet but its start
qboolean VK_GpuFirstMark( void ) {
	return (qboolean)( vk.frame && vk.frame->numMarks <= 1 );
}

void VK_GpuMark( const char *name ) {
	vkFrame_t *frame = vk.frame;

	if ( !frame || !frame->timestamps || frame->numMarks == VK_GPU_MARKS ) {
		return;
	}
	qvkCmdWriteTimestamp( frame->cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame->timestamps, (uint32_t)frame->numMarks );
	frame->markNames[frame->numMarks++] = name;
}

void VK_GpuTimes_f( void ) {
	if ( !VK_GpuTimed() ) {
		ri.Printf( PRINT_ALL, "v_gpuTimes: this device has no timestamps\n" );
		return;
	}
	if ( !vk_gpuFrames ) {
		ri.Printf( PRINT_ALL, "v_gpuTimes: no frame since the last call\n" );
		return;
	}
	ri.Printf( PRINT_ALL, "device time a frame over %i frames, %ix%i, %i samples:\n", vk_gpuFrames, vk.width, vk.height, (int)vk.samples );
	for ( int i = 0; i < vk_numGpuNames; i++ ) {
		ri.Printf( PRINT_ALL, "  %-16s %7.3f ms\n", vk_gpuNames[i].name, vk_gpuNames[i].ms / vk_gpuFrames );
	}
	ri.Printf( PRINT_ALL, "  %-16s %7.3f ms\n", "whole frame", vk_gpuFrameMs / vk_gpuFrames );
	if ( vk_gpuFragmentFrames ) {
		ri.Printf( PRINT_ALL, "level overdraw: the fragment shader ran %.2f times a pixel of the frame\n",
			vk_gpuFragments / vk_gpuFragmentFrames / ( (double)vk.width * vk.height ) );
	}
	vk_gpuFragments = 0.0;
	vk_gpuFragmentFrames = 0;
	vk_numGpuNames = 0;
	vk_gpuFrameMs = 0.0;
	vk_gpuFrames = 0;
}

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
		VK_CHECK( qvkAllocateCommandBuffers( vk.device, &alloc, &frame->presentCmd ) );
		VK_CHECK( qvkCreateFence( vk.device, &fence, NULL, &frame->fence ) );
		VK_CHECK( qvkCreateSemaphore( vk.device, &semaphore, NULL, &frame->acquired ) );
		VK_CreateBuffer( &frame->vertexes, VK_VERTEX_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, qtrue );
		VK_CreateBuffer( &frame->indexes, VK_INDEX_BYTES, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, qtrue );
		VK_CreateBuffer( &frame->globals, sizeof( vkGlobals_t ), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, qtrue );
		VK_CreateBuffer( &frame->transforms, sizeof( vkTransform_t ) * VK_MAX_TRANSFORMS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, qtrue );
		VK_CreateBuffer( &frame->draws, sizeof( vkDraw_t ) * VK_MAX_DRAWS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, qtrue );
		VK_CreateBuffer( &frame->bones, sizeof( float ) * 12 * VK_MAX_BONES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
			| ( vk.deviceAddress ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT : 0 ), qtrue );
		VK_CreateBuffer( &frame->probes, sizeof( uint32_t ) * VK_MAX_FLARES, VK_BUFFER_USAGE_TRANSFER_DST_BIT, qtrue );
		frame->numProbes = frame->numProbesCopied = 0;
		frame->timestamps = VK_NULL_HANDLE;
		frame->numMarks = 0;
		if ( VK_GpuTimed() ) {
			VkQueryPoolCreateInfo query = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };

			query.queryType = VK_QUERY_TYPE_TIMESTAMP;
			query.queryCount = VK_GPU_MARKS;
			VK_CHECK( qvkCreateQueryPool( vk.device, &query, NULL, &frame->timestamps ) );
		}
		frame->fragments = VK_NULL_HANDLE;
		frame->fragmentsActive = frame->fragmentsUsed = qfalse;
		if ( vk.pipelineStats ) {
			VkQueryPoolCreateInfo query = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };

			query.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
			query.queryCount = 1;
			query.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
			VK_CHECK( qvkCreateQueryPool( vk.device, &query, NULL, &frame->fragments ) );
		}

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
		if ( frame->timestamps ) {
			qvkDestroyQueryPool( vk.device, frame->timestamps, NULL );
			frame->timestamps = VK_NULL_HANDLE;
		}
		if ( frame->fragments ) {
			qvkDestroyQueryPool( vk.device, frame->fragments, NULL );
			frame->fragments = VK_NULL_HANDLE;
		}
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

/*
===============
VK_Scissor

VisionJK: the scissor alone, in pixels of the window from its upper left corner (the clip rectangles of the debug
menu of the client). VK_SetViewport() puts it back to the viewport.
===============
*/
void VK_Scissor( int left, int top, int right, int bottom ) {
	VkRect2D scissor;

	if ( !vk.frame ) {
		return;
	}
	left = Q_max( left, 0 );
	top = Q_max( top, 0 );
	right = Q_min( right, vk.width );
	bottom = Q_min( bottom, vk.height );
	scissor.offset.x = left;
	scissor.offset.y = top;
	scissor.extent.width = (uint32_t)Q_max( right - left, 0 );
	scissor.extent.height = (uint32_t)Q_max( bottom - top, 0 );
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
	// (the fence says the device is through with what this frame recorded the last time)
	VK_GpuRead( frame );
	frame->numMarks = 0;
	frame->fragmentsActive = frame->fragmentsUsed = qfalse;
	if ( frame->fragments ) {
		qvkCmdResetQueryPool( frame->cmd, frame->fragments, 0, 1 );
	}
	if ( frame->timestamps ) {
		qvkCmdResetQueryPool( frame->cmd, frame->timestamps, 0, VK_GPU_MARKS );
		qvkCmdWriteTimestamp( frame->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame->timestamps, 0 );
		frame->markNames[frame->numMarks++] = "start";
	}
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

	// (after the last view: the HUD, the menus, the console and the debug menus of the client)
	VK_GpuMark( "2d and hud" );
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

	VK_GpuMark( "gamma and window" );
	VK_CHECK( qvkEndCommandBuffer( cmd ) );

	// textures this frame draws with have to be on the device before it runs
	VK_FlushUploads();
	VK_CHECK( qvkResetFences( vk.device, 1, &frame->fence ) );

	// the frame runs at once, also while the image of the window is waited for; the copy into that image waits for
	// it alone, and the fence after the copy covers both (it takes in what was submitted before it)
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;
	VK_CHECK( qvkQueueSubmit( vk.queue, 1, &submit, VK_NULL_HANDLE ) );

	present = VK_AcquireImage( frame, &imageIndex );
	if ( !present ) {
		vk.statFramesNotShown++;
		submit.commandBufferCount = 0;
		VK_CHECK( qvkQueueSubmit( vk.queue, 1, &submit, frame->fence ) );
		VK_InvalidateTransform();
		return;
	}

	VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };

	cmd = frame->presentCmd;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK( qvkResetCommandBuffer( cmd, 0 ) );
	VK_CHECK( qvkBeginCommandBuffer( cmd, &begin ) );
	{
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
	}
	VK_CHECK( qvkEndCommandBuffer( cmd ) );
	submit.pCommandBuffers = &cmd;
	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &frame->acquired;
	submit.pWaitDstStageMask = &waitStage;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &vk.swapReady[imageIndex];
	VK_CHECK( qvkQueueSubmit( vk.queue, 1, &submit, frame->fence ) );

	{
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
	VK_InvalidateTransform();
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
	if ( changed & VKS_DEPTH_EQUAL ) {
		qvkCmdSetDepthCompareOp( cmd, ( state & VKS_DEPTH_EQUAL ) ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_LESS_OR_EQUAL );
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
	VK_FinishUploads();
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
	if ( source == &vk.screen ) {
		return vk.screenSet;
	}
	if ( source == &vk.smoke[0] || source == &vk.smoke[1] ) {
		return vk.smokeSets[source - vk.smoke];
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

// The depth is not drawn into while the pass reads it: the pass that follows takes it back (VK_BeginTarget).
static void VK_DepthPass( VkPipeline pipeline, vkTarget_t *source, vkTarget_t *target, const float *parm ) {
	VkCommandBuffer cmd = vk.frame->cmd;
	const VkDescriptorSet sets[2] = { VK_PostSet( source ), vk.depthSet };

	VK_TargetLayout( cmd, source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_TargetLayout( cmd, &vk.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL );
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	vk.boundPipeline = pipeline;
	VK_TargetLayout( cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	VK_BeginRendering( cmd, target, NULL, 0, qfalse );
	VK_FullTarget( cmd, target );
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.focusPipelineLayout, 0, 2, sets, 0, NULL );
	qvkCmdPushConstants( cmd, vk.focusPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( float ) * 4, parm );
	qvkCmdDraw( cmd, 3, 1, 0, 0 );
	qvkCmdEndRendering( cmd );
	VK_TargetLayout( cmd, target, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
}

void VK_SsrPrepPass( const float *parm ) {
	VK_DepthPass( vk.ssrPrepPipeline, &vk.color, &vk.ssrColor, parm );
}

void VK_FocusPass( vkTarget_t *source, vkTarget_t *target, const float *parm ) {
	VK_DepthPass( vk.focusPipeline, source, target, parm );
}

void VK_ScopePrepPass( vkTarget_t *source, vkTarget_t *target, const float *parm ) {
	VK_DepthPass( vk.scopePrepPipeline, source, target, parm );
}

void VK_ScreenFxPrepPass( vkTarget_t *source, vkTarget_t *target, const float *parm ) {
	VK_DepthPass( vk.screenFxPrepPipeline, source, target, parm );
}

// as VK_BlurPass(), with the globals of the frame and two sources
void VK_ScreenFxSmokePass( vkTarget_t *target, vkTarget_t *last, vkTarget_t *prep, const float *fx ) {
	VkCommandBuffer cmd = vk.frame->cmd;
	const VkDescriptorSet sets[3] = { vk.frame->set, VK_PostSet( last ), VK_PostSet( prep ) };
	vkGlobals_t *globals = (vkGlobals_t *)vk.frame->globals.mapped;

	memcpy( globals->screenFx, fx, sizeof( globals->screenFx ) );
	VK_TargetLayout( cmd, last, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_TargetLayout( cmd, prep, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.screenFxSmokePipeline );
	vk.boundPipeline = vk.screenFxSmokePipeline;
	VK_TargetLayout( cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	VK_BeginRendering( cmd, target, NULL, 0, qfalse );
	VK_FullTarget( cmd, target );
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.screenFxPipelineLayout, 0, 3, sets, 0, NULL );
	qvkCmdDraw( cmd, 3, 1, 0, 0 );
	qvkCmdEndRendering( cmd );
	VK_TargetLayout( cmd, target, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
}

// as VK_DepthPass(), with the globals of the frame in place of a source and its numbers
void VK_ScreenFxFogPass( vkTarget_t *target, const float *fx ) {
	VkCommandBuffer cmd = vk.frame->cmd;
	const VkDescriptorSet sets[2] = { vk.frame->set, vk.depthSet };
	vkGlobals_t *globals = (vkGlobals_t *)vk.frame->globals.mapped;

	memcpy( globals->screenFx, fx, sizeof( globals->screenFx ) );
	VK_TargetLayout( cmd, &vk.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL );
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.screenFxFogPipeline );
	vk.boundPipeline = vk.screenFxFogPipeline;
	VK_TargetLayout( cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	VK_BeginRendering( cmd, target, NULL, 0, qfalse );
	VK_FullTarget( cmd, target );
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.fogPipelineLayout, 0, 2, sets, 0, NULL );
	qvkCmdDraw( cmd, 3, 1, 0, 0 );
	qvkCmdEndRendering( cmd );
	VK_TargetLayout( cmd, target, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
}

// The numbers go into the globals of the frame, which the device reads when it runs the frame: one pass a frame
// (and its mist before it, with the same numbers).
void VK_ScreenFxPass( vkTarget_t *prep, vkTarget_t *fog, vkTarget_t *smoke, const float *fx ) {
	VkCommandBuffer cmd = vk.frame->cmd;
	const VkDescriptorSet sets[5] = { vk.frame->set, vk.screenSet, VK_PostSet( prep ), VK_PostSet( fog ), VK_PostSet( smoke ) };
	vkGlobals_t *globals = (vkGlobals_t *)vk.frame->globals.mapped;

	memcpy( globals->screenFx, fx, sizeof( globals->screenFx ) );
	VK_TargetLayout( cmd, &vk.screen, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_TargetLayout( cmd, prep, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_TargetLayout( cmd, fog, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_TargetLayout( cmd, smoke, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_BeginTarget( &vk.color, &vk.depth, 2, qtrue );
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.screenFxPipeline );
	vk.boundPipeline = vk.screenFxPipeline;
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.screenFxPipelineLayout, 0, 5, sets, 0, NULL );
	VK_FullTarget( cmd, &vk.color );
	qvkCmdDraw( cmd, 3, 1, 0, 0 );
}

// The depth is a target of the frame while the wet places and the sprayed pictures read it, but in the layout for
// reading: no test, nothing written (the stencil goes with it). With several samples a pixel the frame is drawn into
// the targets that stand in for color and depth, and the depth the shader reads is the plain one, which has their
// first sample since the scene ended.
void VK_BeginOverLevel( VkCommandBuffer cmd, const int *area, qboolean glow ) {
	const qboolean multi = (qboolean)( vk.samples > VK_SAMPLE_COUNT_1_BIT );
	vkTarget_t *color = glow ? &vk.glow : &vk.color, *colorMS = glow ? &vk.glowMS : &vk.colorMS;
	VkRenderingAttachmentInfo colorInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	VkRenderingAttachmentInfo depthInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	VkRenderingInfo info = { VK_STRUCTURE_TYPE_RENDERING_INFO };

	VK_TargetLayout( cmd, &vk.screen, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_TargetLayout( cmd, color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	VK_TargetLayout( cmd, &vk.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL );
	if ( multi ) {
		VK_TargetLayout( cmd, colorMS, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
		VK_TargetLayout( cmd, &vk.depthMS, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL );
	}

	colorInfo.imageView = multi ? colorMS->view : color->view;
	colorInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	if ( multi ) {
		colorInfo.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
		colorInfo.resolveImageView = color->view;
		colorInfo.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}
	colorInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	colorInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthInfo.imageView = multi ? vk.depthMS.view : vk.depth.view;
	depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	depthInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	depthInfo.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
	info.renderArea.extent.width = (uint32_t)color->width;
	info.renderArea.extent.height = (uint32_t)color->height;
	// (a part only: with many samples a pass over all of it resolves all of it again; never past the image, which
	// loses the device)
	if ( area ) {
		const int x = Com_Clampi( 0, color->width, area[0] ), y = Com_Clampi( 0, color->height, area[1] );

		info.renderArea.offset.x = x;
		info.renderArea.offset.y = y;
		info.renderArea.extent.width = (uint32_t)Com_Clampi( 0, color->width - x, area[2] );
		info.renderArea.extent.height = (uint32_t)Com_Clampi( 0, color->height - y, area[3] );
	}
	info.layerCount = 1;
	info.colorAttachmentCount = 1;
	info.pColorAttachments = &colorInfo;
	info.pDepthAttachment = &depthInfo;
	info.pStencilAttachment = &depthInfo;
	qvkCmdBeginRendering( cmd, &info );
}

void VK_WetPass( const float *wet, int count, const int *viewport ) {
	VkCommandBuffer cmd = vk.frame->cmd;
	// (without the puddle view the copy of the frame stands in for its picture, the shader does not read it then)
	const VkDescriptorSet sets[4] = { vk.frame->set, vk.screenSet, vk.depthSet, vk.reflect.image ? vk.reflectSet : vk.screenSet };
	vkGlobals_t *globals = (vkGlobals_t *)vk.frame->globals.mapped;

	memcpy( globals->wet, wet, sizeof( globals->wet ) );
	if ( vk.reflect.image ) {
		VK_TargetLayout( cmd, &vk.reflect, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	}
	VK_BeginOverLevel( cmd );
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.wetPipeline );
	vk.boundPipeline = vk.wetPipeline;
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.screenFxPipelineLayout, 0, 4, sets, 0, NULL );
	VK_ViewportAndScissor( viewport[0], viewport[1], viewport[2], viewport[3], 0.0f, 1.0f );
	qvkCmdDraw( cmd, 6, (uint32_t)count, 0, 0 );
	qvkCmdEndRendering( cmd );
}

// the sheets of the pictures are textures of the table, the shader takes each by its number
void VK_PaintPass( const float *paint, int count, const int *viewport ) {
	VkCommandBuffer cmd = vk.frame->cmd;
	const VkDescriptorSet sets[4] = { vk.frame->set, vk.screenSet, vk.depthSet, vk.textureSet };
	vkGlobals_t *globals = (vkGlobals_t *)vk.frame->globals.mapped;

	memcpy( globals->paint, paint, sizeof( globals->paint ) );
	VK_BeginOverLevel( cmd );
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.paintPipeline );
	vk.boundPipeline = vk.paintPipeline;
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.paintPipelineLayout, 0, 4, sets, 0, NULL );
	VK_ViewportAndScissor( viewport[0], viewport[1], viewport[2], viewport[3], 0.0f, 1.0f );
	qvkCmdDraw( cmd, 6, (uint32_t)count, 0, 0 );
	qvkCmdEndRendering( cmd );
}

// The targets it reads are made ready to be read before the frame is begun, the mask also when nothing was drawn
// into it this frame (the shader does not read it then).
void VK_ScopePass( vkTarget_t *soft, vkTarget_t *prep, vkTarget_t *mark, const float *parm ) {
	VkCommandBuffer cmd = vk.frame->cmd;
	const VkDescriptorSet sets[4] = { vk.screenSet, VK_PostSet( soft ), VK_PostSet( prep ), VK_PostSet( mark ) };

	VK_TargetLayout( cmd, &vk.screen, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_TargetLayout( cmd, soft, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_TargetLayout( cmd, prep, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_TargetLayout( cmd, mark, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_BeginTarget( &vk.color, &vk.depth, 2, qtrue );
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.scopePipeline );
	vk.boundPipeline = vk.scopePipeline;
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.scopePipelineLayout, 0, 4, sets, 0, NULL );
	qvkCmdPushConstants( cmd, vk.scopePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( float ) * 16, parm );
	VK_FullTarget( cmd, &vk.color );
	qvkCmdDraw( cmd, 3, 1, 0, 0 );
}

// one pass of v_smaa into target (cleared to zero), reading count sets
static void VK_SmaaStep( VkPipeline pipeline, vkTarget_t *target, const VkDescriptorSet *sets, int count, const float *push ) {
	VkCommandBuffer cmd = vk.frame->cmd;

	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	vk.boundPipeline = pipeline;
	VK_TargetLayout( cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	VK_BeginRendering( cmd, target, NULL, 1, qfalse );
	VK_FullTarget( cmd, target );
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.smaaPipelineLayout, 0, (uint32_t)count, sets, 0, NULL );
	qvkCmdPushConstants( cmd, vk.smaaPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( float ) * 8, push );
	qvkCmdDraw( cmd, 3, 1, 0, 0 );
	qvkCmdEndRendering( cmd );
	VK_TargetLayout( cmd, target, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
}

// The copy of the frame is what the first and the last pass read: the frame itself is the target of the last.
// With r_ext_multisample the last pass writes every sample of vk.colorMS, which the end of the target resolves
// into vk.color again, so what is drawn over the frame later (the 2D) keeps it.
void VK_SmaaPass( float threshold ) {
	VkCommandBuffer cmd = vk.frame->cmd;
	const float push[8] = { 1.0f / vk.width, 1.0f / vk.height, (float)vk.width, (float)vk.height, threshold, 0.0f, 0.0f, 0.0f };
	VkDescriptorSet weightSets[3], blendSets[2];

	// (before the sets are taken: the first time they are made here)
	VK_CreateSmaaTargets();
	weightSets[0] = vk.smaaEdgesSet;
	weightSets[1] = vk.smaaAreaSet;
	weightSets[2] = vk.smaaSearchSet;
	blendSets[0] = vk.smaaColorSet;
	blendSets[1] = vk.smaaWeightsSet;

	VK_CopyTarget( &vk.color, &vk.smaaColor );
	VK_SmaaStep( vk.smaaEdgePipeline, &vk.smaaEdges, &vk.smaaColorSet, 1, push );
	VK_SmaaStep( vk.smaaWeightPipeline, &vk.smaaWeights, weightSets, 3, push );

	VK_BeginTarget( &vk.color, &vk.depth, 2, qtrue );
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.smaaBlendPipeline );
	vk.boundPipeline = vk.smaaBlendPipeline;
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.smaaPipelineLayout, 0, 2, blendSets, 0, NULL );
	qvkCmdPushConstants( cmd, vk.smaaPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( float ) * 8, push );
	VK_FullTarget( cmd, &vk.color );
	qvkCmdDraw( cmd, 3, 1, 0, 0 );
}

// v_tonemap: the frame through shaders/tonemap.frag from a copy of it (vk.screen); parm: exposure, curve, 2 ^
// overbright bits. Leaves vk.color begun with vk.depth, as VK_SmaaPass() does.
void VK_TonemapPass( const float *parm ) {
	VkCommandBuffer cmd = vk.frame->cmd;
	float push[8] = { 0.0f, 0.0f, 0.0f, 0.0f, parm[0], parm[1], parm[2], 0.0f };

	VK_CopyTarget( &vk.color, &vk.screen );
	VK_BeginTarget( &vk.color, &vk.depth, 2, qtrue );
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.tonemapPipeline );
	vk.boundPipeline = vk.tonemapPipeline;
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.smaaPipelineLayout, 0, 1, &vk.screenSet, 0, NULL );
	qvkCmdPushConstants( cmd, vk.smaaPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), push );
	VK_FullTarget( cmd, &vk.color );
	qvkCmdDraw( cmd, 3, 1, 0, 0 );
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
