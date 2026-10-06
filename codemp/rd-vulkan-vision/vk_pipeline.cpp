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

// vk_pipeline.cpp: descriptor layouts, shader modules and pipelines
//
// A .shader stage asks for a blend function, a depth function, culling and so on through its GLS_* state bits.
// Only the blend function needs a pipeline of its own. Depth test, depth write, depth function, culling, front
// face and depth bias are dynamic state (Vulkan 1.3) and are set per draw, the alpha test is a discard in the
// fragment shader. So a level ends up with a handful of pipelines and they are made the first time a stage
// needs them.

#include "vk_local.h"

// SPIR-V from shaders/*.vert and *.frag, glslc writes them as initializer lists while building
static const uint32_t vk_genericVert[] =
#include "shaders/generic_vert.inc"
;
static const uint32_t vk_skinnedVert[] =
#include "shaders/skinned_vert.inc"
;
static const uint32_t vk_genericFrag[] =
#include "shaders/generic_frag.inc"
;
static const uint32_t vk_postVert[] =
#include "shaders/post_vert.inc"
;
static const uint32_t vk_gammaFrag[] =
#include "shaders/gamma_frag.inc"
;
static const uint32_t vk_blurFrag[] =
#include "shaders/blur_frag.inc"
;
static const uint32_t vk_copyFrag[] =
#include "shaders/copy_frag.inc"
;

static VkShaderModule VK_CreateShader( const uint32_t *code, size_t size ) {
	VkShaderModuleCreateInfo info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	VkShaderModule module;

	info.codeSize = size;
	info.pCode = code;
	VK_CHECK( qvkCreateShaderModule( vk.device, &info, NULL, &module ) );
	return module;
}

static void VK_CreateLayouts( void ) {
	VkDescriptorSetLayoutBinding frame[4];
	VkDescriptorSetLayoutBinding texture;
	VkDescriptorSetLayoutBinding post;
	VkDescriptorBindingFlags textureFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
		| VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
	VkDescriptorSetLayoutBindingFlagsCreateInfo flags = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
	VkDescriptorSetLayoutCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	VkDescriptorPoolSize sizes[3];
	VkDescriptorPoolCreateInfo pool = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	VkPushConstantRange push;
	VkPipelineLayoutCreateInfo layout = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	VkDescriptorSetLayout sets[2];
	VkDescriptorSetAllocateInfo alloc = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	VkSamplerCreateInfo sampler = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

	// set 0: globals, transforms, draws and bones of the frame
	memset( frame, 0, sizeof( frame ) );
	for ( uint32_t i = 0; i < 4; i++ ) {
		frame[i].binding = i;
		frame[i].descriptorType = i ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		frame[i].descriptorCount = 1;
		frame[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	}
	info.bindingCount = 4;
	info.pBindings = frame;
	VK_CHECK( qvkCreateDescriptorSetLayout( vk.device, &info, NULL, &vk.frameSetLayout ) );

	// set 1: the texture table. Entries are written while frames that use other entries are still on their way.
	memset( &texture, 0, sizeof( texture ) );
	texture.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	texture.descriptorCount = VK_MAX_IMAGES;
	texture.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	flags.bindingCount = 1;
	flags.pBindingFlags = &textureFlags;
	info.pNext = &flags;
	info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
	info.bindingCount = 1;
	info.pBindings = &texture;
	VK_CHECK( qvkCreateDescriptorSetLayout( vk.device, &info, NULL, &vk.textureSetLayout ) );

	// the frame as a texture for the gamma pass
	memset( &post, 0, sizeof( post ) );
	post.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	post.descriptorCount = 1;
	post.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	info.pNext = NULL;
	info.flags = 0;
	info.pBindings = &post;
	VK_CHECK( qvkCreateDescriptorSetLayout( vk.device, &info, NULL, &vk.postSetLayout ) );

	sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	sizes[0].descriptorCount = VK_FRAMES;
	sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	sizes[1].descriptorCount = VK_FRAMES * 3;
	sizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	sizes[2].descriptorCount = VK_MAX_IMAGES + 4;
	pool.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
	pool.maxSets = VK_FRAMES + 5;
	pool.poolSizeCount = 3;
	pool.pPoolSizes = sizes;
	VK_CHECK( qvkCreateDescriptorPool( vk.device, &pool, NULL, &vk.descriptorPool ) );

	// the index of the draw is all that changes from one draw to the next, and that is the instance of the draw call
	sets[0] = vk.frameSetLayout;
	sets[1] = vk.textureSetLayout;
	layout.setLayoutCount = 2;
	layout.pSetLayouts = sets;
	VK_CHECK( qvkCreatePipelineLayout( vk.device, &layout, NULL, &vk.pipelineLayout ) );

	// the passes over the finished frame get four numbers each
	push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	push.offset = 0;
	push.size = sizeof( float ) * 4;
	layout.setLayoutCount = 1;
	layout.pSetLayouts = &vk.postSetLayout;
	layout.pushConstantRangeCount = 1;
	layout.pPushConstantRanges = &push;
	VK_CHECK( qvkCreatePipelineLayout( vk.device, &layout, NULL, &vk.postPipelineLayout ) );

	alloc.descriptorPool = vk.descriptorPool;
	alloc.descriptorSetCount = 1;
	alloc.pSetLayouts = &vk.textureSetLayout;
	VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &vk.textureSet ) );
	alloc.pSetLayouts = &vk.postSetLayout;
	VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &vk.postSet ) );

	// the gamma pass reads the frame pixel for pixel
	sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
	sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	VK_CHECK( qvkCreateSampler( vk.device, &sampler, NULL, &vk.postSampler ) );
	{
		VkDescriptorImageInfo image = { vk.postSampler, vk.color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

		write.dstSet = vk.postSet;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &image;
		qvkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
	}
}

static VkBlendFactor VK_SrcBlend( uint32_t stateBits ) {
	switch ( stateBits & GLS_SRCBLEND_BITS ) {
	case GLS_SRCBLEND_ZERO:					return VK_BLEND_FACTOR_ZERO;
	case GLS_SRCBLEND_DST_COLOR:			return VK_BLEND_FACTOR_DST_COLOR;
	case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:	return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
	case GLS_SRCBLEND_SRC_ALPHA:			return VK_BLEND_FACTOR_SRC_ALPHA;
	case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:	return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	case GLS_SRCBLEND_DST_ALPHA:			return VK_BLEND_FACTOR_DST_ALPHA;
	case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:	return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
	case GLS_SRCBLEND_ALPHA_SATURATE:		return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
	default:								return VK_BLEND_FACTOR_ONE;
	}
}

static VkBlendFactor VK_DstBlend( uint32_t stateBits ) {
	switch ( stateBits & GLS_DSTBLEND_BITS ) {
	case GLS_DSTBLEND_ONE:					return VK_BLEND_FACTOR_ONE;
	case GLS_DSTBLEND_SRC_COLOR:			return VK_BLEND_FACTOR_SRC_COLOR;
	case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:	return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
	case GLS_DSTBLEND_SRC_ALPHA:			return VK_BLEND_FACTOR_SRC_ALPHA;
	case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:	return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	case GLS_DSTBLEND_DST_ALPHA:			return VK_BLEND_FACTOR_DST_ALPHA;
	case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:	return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
	default:								return VK_BLEND_FACTOR_ZERO;
	}
}

// the part of the state bits that needs a pipeline of its own
#define VK_PIPELINE_BITS	( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS | GLS_POLYMODE_LINE )

/*
===============
VK_CreatePipeline

post: 0 geometry. 1 a pass over a whole target of its own without vertices (gamma, blur). 2 such a pass onto the
frame while it is being drawn, blended as the state bits say (the glow).
===============
*/
VkPipeline VK_CreatePipeline( VkShaderModule vertex, VkShaderModule fragment, uint32_t stateBits, int post, qboolean skinned ) {
	VkPipelineShaderStageCreateInfo stages[2];
	VkVertexInputBindingDescription bindings[4];
	VkVertexInputAttributeDescription attributes[8];
	VkPipelineVertexInputStateCreateInfo input = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	VkPipelineInputAssemblyStateCreateInfo assembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	VkPipelineViewportStateCreateInfo viewport = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	VkPipelineDepthStencilStateCreateInfo depth = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	VkPipelineColorBlendAttachmentState attachment;
	VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_CULL_MODE,
		VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
		VK_DYNAMIC_STATE_DEPTH_COMPARE_OP, VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE, VK_DYNAMIC_STATE_DEPTH_BIAS };
	VkPipelineDynamicStateCreateInfo dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	VkPipelineRenderingCreateInfo rendering = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rendering };
	VkPipeline pipeline;

	memset( stages, 0, sizeof( stages ) );
	stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vertex;
	stages[0].pName = "main";
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fragment;
	stages[1].pName = "main";

	if ( skinned ) {
		// vkSkinVertex_t
		memset( bindings, 0, sizeof( bindings ) );
		memset( attributes, 0, sizeof( attributes ) );
		bindings[0].stride = sizeof( vkSkinVertex_t );
		bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		for ( uint32_t i = 0; i < 5; i++ ) {
			attributes[i].location = i;
		}
		attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributes[0].offset = offsetof( vkSkinVertex_t, xyz );
		attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributes[1].offset = offsetof( vkSkinVertex_t, normal );
		attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
		attributes[2].offset = offsetof( vkSkinVertex_t, st );
		attributes[3].format = VK_FORMAT_R8G8B8A8_UINT;
		attributes[3].offset = offsetof( vkSkinVertex_t, bones );
		attributes[4].format = VK_FORMAT_R16G16B16A16_UNORM;
		attributes[4].offset = offsetof( vkSkinVertex_t, weights );
		input.vertexBindingDescriptionCount = 1;
		input.pVertexBindingDescriptions = bindings;
		input.vertexAttributeDescriptionCount = 5;
		input.pVertexAttributeDescriptions = attributes;
	} else if ( !post ) {
		// the arrays of shaderCommands_t, one binding each: xyz, normal, texCoords, vertexColors
		const uint32_t strides[4] = { sizeof( vec4_t ), sizeof( vec4_t ), sizeof( vec2_t ) * NUM_TEX_COORDS, 4 };

		memset( bindings, 0, sizeof( bindings ) );
		memset( attributes, 0, sizeof( attributes ) );
		for ( uint32_t i = 0; i < 4; i++ ) {
			bindings[i].binding = i;
			bindings[i].stride = strides[i];
			bindings[i].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		}
		for ( uint32_t i = 0; i < 8; i++ ) {
			attributes[i].location = i;
		}
		attributes[0].binding = 0;
		attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributes[1].binding = 1;
		attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		for ( uint32_t i = 0; i < NUM_TEX_COORDS; i++ ) {
			attributes[2 + i].binding = 2;
			attributes[2 + i].format = VK_FORMAT_R32G32_SFLOAT;
			attributes[2 + i].offset = sizeof( vec2_t ) * i;
		}
		attributes[7].binding = 3;
		attributes[7].format = VK_FORMAT_R8G8B8A8_UNORM;
		input.vertexBindingDescriptionCount = 4;
		input.pVertexBindingDescriptions = bindings;
		input.vertexAttributeDescriptionCount = 8;
		input.pVertexAttributeDescriptions = attributes;
	}

	assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;
	raster.polygonMode = ( !post && vk.wireframe && ( stateBits & GLS_POLYMODE_LINE ) ) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;		// as in OpenGL, the viewport is flipped to match
	raster.lineWidth = 1.0f;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

	memset( &attachment, 0, sizeof( attachment ) );
	attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	if ( post != 1 && ( stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) ) {
		attachment.blendEnable = VK_TRUE;
		attachment.srcColorBlendFactor = attachment.srcAlphaBlendFactor = VK_SrcBlend( stateBits );
		attachment.dstColorBlendFactor = attachment.dstAlphaBlendFactor = VK_DstBlend( stateBits );
		attachment.colorBlendOp = attachment.alphaBlendOp = VK_BLEND_OP_ADD;
	}
	blend.attachmentCount = 1;
	blend.pAttachments = &attachment;

	dynamic.dynamicStateCount = post ? 2 : ARRAY_LEN( dynamicStates );		// viewport and scissor only
	dynamic.pDynamicStates = dynamicStates;

	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachmentFormats = ( post == 1 ) ? &vk.final.format : &vk.color.format;
	if ( post != 1 ) {
		rendering.depthAttachmentFormat = vk.depth.format;
		rendering.stencilAttachmentFormat = vk.depth.format;
	}

	info.stageCount = 2;
	info.pStages = stages;
	info.pVertexInputState = &input;
	info.pInputAssemblyState = &assembly;
	info.pViewportState = &viewport;
	info.pRasterizationState = &raster;
	info.pMultisampleState = &multisample;
	info.pDepthStencilState = &depth;
	info.pColorBlendState = &blend;
	info.pDynamicState = &dynamic;
	info.layout = post ? vk.postPipelineLayout : vk.pipelineLayout;
	VK_CHECK( qvkCreateGraphicsPipelines( vk.device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline ) );
	return pipeline;
}

/*
===============
VK_FindPipeline
===============
*/
VkPipeline VK_FindPipeline( vkProgram_t program, uint32_t stateBits ) {
	const uint32_t key = ( stateBits & VK_PIPELINE_BITS ) | ( (uint32_t)program << 24 );

	// most draws want what the draw before them had
	if ( vk.lastPipeline && key == vk.lastPipelineKey ) {
		return vk.lastPipeline;
	}
	for ( int i = 0; i < vk.numPipelines; i++ ) {
		if ( vk.pipelines[i].key == key ) {
			vk.lastPipelineKey = key;
			return vk.lastPipeline = vk.pipelines[i].pipeline;
		}
	}
	if ( vk.numPipelines == VK_MAX_PIPELINES ) {
		Com_Error( ERR_DROP, "VK_FindPipeline: more than %i pipelines", VK_MAX_PIPELINES );
	}
	vk.pipelines[vk.numPipelines].key = key;
	vk.pipelines[vk.numPipelines].pipeline = VK_CreatePipeline( vk.vertexShaders[program], vk.fragmentShaders[program],
		stateBits, qfalse, (qboolean)( program == VKP_SKINNED ) );
	vk.lastPipelineKey = key;
	return vk.lastPipeline = vk.pipelines[vk.numPipelines++].pipeline;
}

void VK_InitPipelines( void ) {
	VK_CreateLayouts();
	vk.vertexShaders[VKP_GENERIC] = VK_CreateShader( vk_genericVert, sizeof( vk_genericVert ) );
	vk.fragmentShaders[VKP_GENERIC] = VK_CreateShader( vk_genericFrag, sizeof( vk_genericFrag ) );
	vk.vertexShaders[VKP_SKINNED] = VK_CreateShader( vk_skinnedVert, sizeof( vk_skinnedVert ) );
	vk.fragmentShaders[VKP_SKINNED] = VK_CreateShader( vk_genericFrag, sizeof( vk_genericFrag ) );
	vk.postVertexShader = VK_CreateShader( vk_postVert, sizeof( vk_postVert ) );
	vk.gammaFragmentShader = VK_CreateShader( vk_gammaFrag, sizeof( vk_gammaFrag ) );
	vk.gammaPipeline = VK_CreatePipeline( vk.postVertexShader, vk.gammaFragmentShader, 0, 1, qfalse );
	vk.blurFragmentShader = VK_CreateShader( vk_blurFrag, sizeof( vk_blurFrag ) );
	vk.copyFragmentShader = VK_CreateShader( vk_copyFrag, sizeof( vk_copyFrag ) );
	vk.blurPipeline = VK_CreatePipeline( vk.postVertexShader, vk.blurFragmentShader, 0, 1, qfalse );
	vk.glowPipelines[0] = VK_CreatePipeline( vk.postVertexShader, vk.copyFragmentShader, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE, 2, qfalse );
	vk.glowPipelines[1] = VK_CreatePipeline( vk.postVertexShader, vk.copyFragmentShader, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE_MINUS_SRC_COLOR, 2, qfalse );
}

void VK_ShutdownPipelines( void ) {
	for ( int i = 0; i < vk.numPipelines; i++ ) {
		qvkDestroyPipeline( vk.device, vk.pipelines[i].pipeline, NULL );
	}
	vk.numPipelines = 0;
	vk.lastPipeline = VK_NULL_HANDLE;
	qvkDestroyPipeline( vk.device, vk.gammaPipeline, NULL );
	qvkDestroyPipeline( vk.device, vk.blurPipeline, NULL );
	qvkDestroyPipeline( vk.device, vk.glowPipelines[0], NULL );
	qvkDestroyPipeline( vk.device, vk.glowPipelines[1], NULL );
	qvkDestroyShaderModule( vk.device, vk.blurFragmentShader, NULL );
	qvkDestroyShaderModule( vk.device, vk.copyFragmentShader, NULL );
	for ( int i = 0; i < VKP_NUM_PROGRAMS; i++ ) {
		qvkDestroyShaderModule( vk.device, vk.vertexShaders[i], NULL );
		qvkDestroyShaderModule( vk.device, vk.fragmentShaders[i], NULL );
	}
	qvkDestroyShaderModule( vk.device, vk.postVertexShader, NULL );
	qvkDestroyShaderModule( vk.device, vk.gammaFragmentShader, NULL );
	qvkDestroySampler( vk.device, vk.postSampler, NULL );
	qvkDestroyPipelineLayout( vk.device, vk.pipelineLayout, NULL );
	qvkDestroyPipelineLayout( vk.device, vk.postPipelineLayout, NULL );
	qvkDestroyDescriptorSetLayout( vk.device, vk.frameSetLayout, NULL );
	qvkDestroyDescriptorSetLayout( vk.device, vk.textureSetLayout, NULL );
	qvkDestroyDescriptorSetLayout( vk.device, vk.postSetLayout, NULL );
	qvkDestroyDescriptorPool( vk.device, vk.descriptorPool, NULL );
}
