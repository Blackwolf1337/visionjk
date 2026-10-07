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

// vk_memory.cpp: device memory in large blocks that are handed out in pieces, and buffers
//
// A driver only has to allow 4096 allocations and the game has a few thousand textures, so every image and
// buffer gets a range of a block instead of an allocation of its own. Ranges are freed again when images go
// at a level change, so each block keeps a list of its free ranges, sorted by offset, neighbours merged.

#include "vk_local.h"

#define VK_BLOCK_SIZE	( (VkDeviceSize)64 << 20 )
#define VK_MAX_BLOCKS	128
#define VK_MAX_RANGES	8192

typedef struct vkRange_s {
	VkDeviceSize		offset;
	VkDeviceSize		size;
	struct vkRange_s	*next;
} vkRange_t;

typedef struct vkBlock_s {
	VkDeviceMemory		memory;
	VkDeviceSize		size;
	uint32_t			type;
	qboolean			hostVisible;	// such blocks stay mapped as a whole
	void				*mapped;
	vkRange_t			*free;
} vkBlock_t;

static vkBlock_t	vk_blocks[VK_MAX_BLOCKS];
static vkRange_t	vk_ranges[VK_MAX_RANGES];
static vkRange_t	*vk_unusedRanges;

static vkRange_t *VK_NewRange( VkDeviceSize offset, VkDeviceSize size, vkRange_t *next ) {
	vkRange_t *range = vk_unusedRanges;

	if ( !range ) {
		Com_Error( ERR_FATAL, "VK_NewRange: out of ranges" );
	}
	vk_unusedRanges = range->next;
	range->offset = offset;
	range->size = size;
	range->next = next;
	return range;
}

static void VK_UnuseRange( vkRange_t *range ) {
	range->next = vk_unusedRanges;
	vk_unusedRanges = range;
}

void VK_InitMemory( void ) {
	memset( vk_blocks, 0, sizeof( vk_blocks ) );
	vk_unusedRanges = NULL;
	for ( int i = VK_MAX_RANGES - 1; i >= 0; i-- ) {
		VK_UnuseRange( &vk_ranges[i] );
	}
}

void VK_ShutdownMemory( void ) {
	for ( int i = 0; i < VK_MAX_BLOCKS; i++ ) {
		if ( vk_blocks[i].memory ) {
			qvkFreeMemory( vk.device, vk_blocks[i].memory, NULL );
		}
	}
	memset( vk_blocks, 0, sizeof( vk_blocks ) );
}

static int VK_MemoryType( uint32_t typeBits, VkMemoryPropertyFlags flags ) {
	for ( uint32_t i = 0; i < vk.memoryProperties.memoryTypeCount; i++ ) {
		if ( ( typeBits & ( 1u << i ) ) && ( vk.memoryProperties.memoryTypes[i].propertyFlags & flags ) == flags ) {
			return (int)i;
		}
	}
	return -1;
}

// takes size bytes at the alignment out of the free ranges of the block, qfalse when none is large enough
static qboolean VK_AllocFromBlock( int blockNum, VkDeviceSize size, VkDeviceSize alignment, vkAlloc_t *alloc ) {
	vkBlock_t *block = &vk_blocks[blockNum];

	for ( vkRange_t **prev = &block->free, *range = block->free; range; prev = &range->next, range = range->next ) {
		const VkDeviceSize start = ( range->offset + alignment - 1 ) / alignment * alignment;
		const VkDeviceSize end = start + size;

		if ( end > range->offset + range->size ) {
			continue;
		}
		// what is left in front of and behind the piece stays free
		const VkDeviceSize before = start - range->offset;
		const VkDeviceSize after = range->offset + range->size - end;

		if ( before && after ) {
			range->size = before;
			range->next = VK_NewRange( end, after, range->next );
		} else if ( before ) {
			range->size = before;
		} else if ( after ) {
			range->offset = end;
			range->size = after;
		} else {
			*prev = range->next;
			VK_UnuseRange( range );
		}
		alloc->block = blockNum;
		alloc->offset = start;
		alloc->size = size;
		return qtrue;
	}
	return qfalse;
}

void VK_AllocMemory( const VkMemoryRequirements *req, VkMemoryPropertyFlags flags, vkAlloc_t *alloc ) {
	const int type = VK_MemoryType( req->memoryTypeBits, flags );
	const qboolean hostVisible = (qboolean)( ( flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ) != 0 );
	int unused = -1;

	if ( type < 0 ) {
		Com_Error( ERR_FATAL, "VK_AllocMemory: no memory type for flags 0x%x", flags );
	}
	for ( int i = 0; i < VK_MAX_BLOCKS; i++ ) {
		if ( !vk_blocks[i].memory ) {
			if ( unused < 0 ) {
				unused = i;
			}
			continue;
		}
		if ( vk_blocks[i].type == (uint32_t)type && vk_blocks[i].hostVisible == hostVisible
			&& VK_AllocFromBlock( i, req->size, req->alignment, alloc ) ) {
			return;
		}
	}
	if ( unused < 0 ) {
		Com_Error( ERR_FATAL, "VK_AllocMemory: out of blocks" );
	}

	// a new block, as large as the request needs
	vkBlock_t *block = &vk_blocks[unused];
	VkMemoryAllocateInfo info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };

	info.allocationSize = req->size > VK_BLOCK_SIZE ? req->size : VK_BLOCK_SIZE;
	info.memoryTypeIndex = (uint32_t)type;
	VK_CHECK( qvkAllocateMemory( vk.device, &info, NULL, &block->memory ) );
	block->size = info.allocationSize;
	block->type = (uint32_t)type;
	block->hostVisible = hostVisible;
	block->mapped = NULL;
	block->free = VK_NewRange( 0, block->size, NULL );
	if ( hostVisible ) {
		VK_CHECK( qvkMapMemory( vk.device, block->memory, 0, VK_WHOLE_SIZE, 0, &block->mapped ) );
	}
	if ( !VK_AllocFromBlock( unused, req->size, req->alignment, alloc ) ) {
		Com_Error( ERR_FATAL, "VK_AllocMemory: a new block is too small" );
	}
}

void VK_FreeMemory( vkAlloc_t *alloc ) {
	if ( alloc->block < 0 || !alloc->size ) {
		return;
	}
	vkBlock_t *block = &vk_blocks[alloc->block];
	vkRange_t **prev = &block->free;
	vkRange_t *range = block->free;

	while ( range && range->offset < alloc->offset ) {
		prev = &range->next;
		range = range->next;
	}
	// range is the first free one behind the piece, *prev points at the link to it
	vkRange_t *freed = VK_NewRange( alloc->offset, alloc->size, range );
	*prev = freed;
	if ( range && freed->offset + freed->size == range->offset ) {
		freed->size += range->size;
		freed->next = range->next;
		VK_UnuseRange( range );
	}
	if ( prev != &block->free ) {
		// the range in front of it: prev points at its next member
		vkRange_t *before = (vkRange_t *)( (byte *)prev - offsetof( vkRange_t, next ) );

		if ( before->offset + before->size == freed->offset ) {
			before->size += freed->size;
			before->next = freed->next;
			VK_UnuseRange( freed );
		}
	}
	// a block nobody uses any more goes back to the driver
	if ( block->free && !block->free->next && block->free->size == block->size ) {
		VK_UnuseRange( block->free );
		qvkFreeMemory( vk.device, block->memory, NULL );		// unmaps it too
		memset( block, 0, sizeof( *block ) );
	}
	alloc->block = -1;
	alloc->size = 0;
}

VkDeviceMemory VK_MemoryOf( const vkAlloc_t *alloc ) {
	return vk_blocks[alloc->block].memory;
}

void VK_CreateBuffer( vkBuffer_t *buf, VkDeviceSize size, VkBufferUsageFlags usage, qboolean hostVisible ) {
	VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	VkMemoryRequirements req;

	memset( buf, 0, sizeof( *buf ) );
	info.size = size;
	info.usage = usage;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VK_CHECK( qvkCreateBuffer( vk.device, &info, NULL, &buf->buffer ) );
	qvkGetBufferMemoryRequirements( vk.device, buf->buffer, &req );
	if ( !hostVisible ) {
		flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	} else if ( usage == VK_BUFFER_USAGE_TRANSFER_DST_BIT
		&& VK_MemoryType( req.memoryTypeBits, flags | VK_MEMORY_PROPERTY_HOST_CACHED_BIT ) >= 0 ) {
		// A buffer the device only copies into is one the CPU reads (screenshots, video frames, the depth under a
		// flare). Memory that is merely host visible may be write combined, and reading that is a hundred times
		// slower: a frame of 1280 x 720 took a quarter of a second.
		flags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
	}
	VK_AllocMemory( &req, flags, &buf->alloc );
	VK_CHECK( qvkBindBufferMemory( vk.device, buf->buffer, VK_MemoryOf( &buf->alloc ), buf->alloc.offset ) );
	buf->size = size;
	if ( hostVisible ) {
		buf->mapped = (byte *)vk_blocks[buf->alloc.block].mapped + buf->alloc.offset;
	}
}

void VK_DestroyBuffer( vkBuffer_t *buf ) {
	if ( buf->buffer ) {
		qvkDestroyBuffer( vk.device, buf->buffer, NULL );
		VK_FreeMemory( &buf->alloc );
	}
	memset( buf, 0, sizeof( *buf ) );
}

/*
===============
VK_MemoryInfo_f

Part of gfxinfo
===============
*/
void VK_MemoryInfo_f( void ) {
	int numBlocks = 0;
	VkDeviceSize total = 0, unused = 0;

	for ( int i = 0; i < VK_MAX_BLOCKS; i++ ) {
		if ( !vk_blocks[i].memory ) {
			continue;
		}
		numBlocks++;
		total += vk_blocks[i].size;
		for ( const vkRange_t *range = vk_blocks[i].free; range; range = range->next ) {
			unused += range->size;
		}
	}
	ri.Printf( PRINT_ALL, "device memory: %i blocks, %i MB, %i MB of it in use\n", numBlocks,
		(int)( total >> 20 ), (int)( ( total - unused ) >> 20 ) );
}
