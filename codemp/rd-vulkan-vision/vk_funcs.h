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

#pragma once

// vk_funcs.h: the functions of Vulkan this renderer calls
//
// The library of Vulkan is not linked. The window code of the engine (SDL) loads it and hands out its
// vkGetInstanceProcAddr, every other function is asked from that by name, the ones of the device from the device,
// so they go to the driver without the detour through the loader. The build needs nothing but the headers, on
// every platform. As qgl in rd-vanilla, the pointers have a q in front.

// asked without an instance. vkEnumerateInstanceVersion is on its own: a loader of Vulkan 1.0 does not have it
#define VK_FUNCTIONS_GLOBAL( X ) \
	X( vkCreateInstance ) \
	X( vkEnumerateInstanceExtensionProperties ) \
	X( vkEnumerateInstanceLayerProperties )

// asked from the instance
#define VK_FUNCTIONS_INSTANCE( X ) \
	X( vkCreateDevice ) \
	X( vkDestroyInstance ) \
	X( vkDestroySurfaceKHR ) \
	X( vkEnumerateDeviceExtensionProperties ) \
	X( vkEnumeratePhysicalDevices ) \
	X( vkGetPhysicalDeviceFeatures ) \
	X( vkGetPhysicalDeviceFeatures2 ) \
	X( vkGetPhysicalDeviceFormatProperties ) \
	X( vkGetPhysicalDeviceMemoryProperties ) \
	X( vkGetPhysicalDeviceProperties ) \
	X( vkGetPhysicalDeviceProperties2 ) \
	X( vkGetPhysicalDeviceQueueFamilyProperties ) \
	X( vkGetPhysicalDeviceSurfaceCapabilitiesKHR ) \
	X( vkGetPhysicalDeviceSurfaceFormatsKHR ) \
	X( vkGetPhysicalDeviceSurfacePresentModesKHR ) \
	X( vkGetPhysicalDeviceSurfaceSupportKHR ) \
	X( vkGetDeviceProcAddr )

// asked from the device
#define VK_FUNCTIONS_DEVICE( X ) \
	X( vkAcquireNextImageKHR ) \
	X( vkAllocateCommandBuffers ) \
	X( vkAllocateDescriptorSets ) \
	X( vkAllocateMemory ) \
	X( vkBeginCommandBuffer ) \
	X( vkBindBufferMemory ) \
	X( vkBindImageMemory ) \
	X( vkCmdBeginRendering ) \
	X( vkCmdBindDescriptorSets ) \
	X( vkCmdBindIndexBuffer ) \
	X( vkCmdBindPipeline ) \
	X( vkCmdBindVertexBuffers ) \
	X( vkCmdBlitImage ) \
	X( vkCmdClearAttachments ) \
	X( vkCmdCopyBuffer ) \
	X( vkCmdCopyBufferToImage ) \
	X( vkCmdCopyImage ) \
	X( vkCmdCopyImageToBuffer ) \
	X( vkCmdDraw ) \
	X( vkCmdDrawIndexed ) \
	X( vkCmdEndRendering ) \
	X( vkCmdPipelineBarrier ) \
	X( vkCmdPushConstants ) \
	X( vkCmdSetCullMode ) \
	X( vkCmdSetDepthBias ) \
	X( vkCmdSetDepthBiasEnable ) \
	X( vkCmdSetDepthCompareOp ) \
	X( vkCmdSetDepthTestEnable ) \
	X( vkCmdSetDepthWriteEnable ) \
	X( vkCmdSetScissor ) \
	X( vkCmdSetStencilOp ) \
	X( vkCmdSetStencilReference ) \
	X( vkCmdSetStencilTestEnable ) \
	X( vkCmdSetViewport ) \
	X( vkCreateBuffer ) \
	X( vkCreateCommandPool ) \
	X( vkCreateDescriptorPool ) \
	X( vkCreateDescriptorSetLayout ) \
	X( vkCreateFence ) \
	X( vkCreateGraphicsPipelines ) \
	X( vkCreateImage ) \
	X( vkCreateImageView ) \
	X( vkCreatePipelineLayout ) \
	X( vkCreateSampler ) \
	X( vkCreateSemaphore ) \
	X( vkCreateShaderModule ) \
	X( vkCreateSwapchainKHR ) \
	X( vkDestroyBuffer ) \
	X( vkDestroyCommandPool ) \
	X( vkDestroyDescriptorPool ) \
	X( vkDestroyDescriptorSetLayout ) \
	X( vkDestroyDevice ) \
	X( vkDestroyFence ) \
	X( vkDestroyImage ) \
	X( vkDestroyImageView ) \
	X( vkDestroyPipeline ) \
	X( vkDestroyPipelineLayout ) \
	X( vkDestroySampler ) \
	X( vkDestroySemaphore ) \
	X( vkDestroyShaderModule ) \
	X( vkDestroySwapchainKHR ) \
	X( vkDeviceWaitIdle ) \
	X( vkEndCommandBuffer ) \
	X( vkFreeMemory ) \
	X( vkGetBufferMemoryRequirements ) \
	X( vkGetDeviceQueue ) \
	X( vkGetImageMemoryRequirements ) \
	X( vkGetSwapchainImagesKHR ) \
	X( vkMapMemory ) \
	X( vkQueuePresentKHR ) \
	X( vkQueueSubmit ) \
	X( vkQueueWaitIdle ) \
	X( vkResetCommandBuffer ) \
	X( vkResetFences ) \
	X( vkUpdateDescriptorSets ) \
	X( vkWaitForFences )

#define X( name )	extern PFN_##name q##name;
VK_FUNCTIONS_GLOBAL( X )
VK_FUNCTIONS_INSTANCE( X )
VK_FUNCTIONS_DEVICE( X )
#undef X
extern PFN_vkGetInstanceProcAddr		qvkGetInstanceProcAddr;
extern PFN_vkEnumerateInstanceVersion	qvkEnumerateInstanceVersion;
