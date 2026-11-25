/*
===========================================================================
Copyright (C) 2026, VisionJK contributors

This file is part of the VisionJK source code.

VisionJK is free software; you can redistribute it and/or modify it
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

// vk_sparks.cpp -- v_gpuSparks: sparks, embers and bits that the device simulates. The cgame hands bursts of them
// to the renderer (ext.EmitSparks, sparkBurst_t of tr_types.h); they live in a ring of VK_SPARKS on the device,
// shaders/sparks.comp moves them once a frame before the level is drawn (falling, slowed by the air, bouncing off
// what the distance of the level of the frame before shows, vk.softDepth) and shaders/sparks.vert draws them over the
// solid level as streaks along their flight, added into the frame of HDR (the bloom takes the hot ones). Those that
// come to rest on a floor lie there as embers for a while (sparkBurst_t rest, v_gpuSparksEmbers), glowing, cooling
// and throwing a faint glow onto the floor round them. Tens of
// thousands at once cost the CPU nothing. Needs buffers with device addresses (vk.deviceAddress); without them, or
// with v_gpuSparks 0, EmitSparks says no and the cgame plays its effects as before.

#include "vk_local.h"

#define Vector4Set4( v, a, b, c, d )	( ( v )[0] = ( a ), ( v )[1] = ( b ), ( v )[2] = ( c ), ( v )[3] = ( d ) )

static const uint32_t vk_sparksComp[] =
#include "shaders/sparks_comp.inc"
;

#define VK_SPARKS			65536		// slots of the ring, a power of two (shaders/sparks.comp u.count)
#define VK_SPARK_ROWS		5			// vec4 a spark (shaders/sparks.comp)
#define VK_SPARK_BURSTS		128			// bursts a frame
#define VK_SPARK_BODIES		32			// characters of the frame nothing lies on (shaders/sparks.comp OnBody)
#define VK_SPARK_HEADER		( 12 + VK_SPARK_BODIES )	// rows before the bursts in the frame buffer: the view, the numbers
													// of the frame, the bodies
#define VK_SPARK_BURST_ROWS	6

cvar_t	*v_gpuSparks;
static cvar_t	*v_gpuSparksAmount;
static cvar_t	*v_gpuSparksEmbers;

static struct {
	qboolean		ready;
	qboolean		cleared;			// the ring was zeroed on the device
	vkBuffer_t		ring;
	VkDeviceAddress	ringAddress;
	vkBuffer_t		frame[VK_FRAMES];
	VkDeviceAddress	frameAddress[VK_FRAMES];
	VkPipelineLayout	layout;
	VkShaderModule	shader;
	VkPipeline		pipeline;
	int				head;				// the next slot of the ring
	sparkBurst_t	bursts[VK_SPARK_BURSTS];
	int				firsts[VK_SPARK_BURSTS];
	int				counts[VK_SPARK_BURSTS];
	int				numBursts;
	int				aliveUntil;			// ri.Milliseconds() until which a spark may live: no work after it
	int				embersUntil;		//	and an ember lie: no glow on the floors after it
	float			view[VK_WET_HEADER][4];	// the view of the distance in vk.softDepth (VK_SparksView)
	float			bodies[VK_SPARK_BODIES][4];	//	and the characters in it: their middle and how far round
	qboolean		viewValid;
	int				lastTime;
	uint32_t		seed;
} sp;

static VkDeviceAddress VK_SparksAddress( const vkBuffer_t *buf ) {
	VkBufferDeviceAddressInfo info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };

	info.buffer = buf->buffer;
	return qvkGetBufferDeviceAddress( vk.device, &info );
}

void VK_SparksInit( void ) {
	v_gpuSparks = ri.Cvar_Get( "v_gpuSparks", "1", CVAR_ARCHIVE_ND,
		"Sparks, embers and bits of saber hits, clashes, shots and explosions simulated by the graphics card: thousands of them, glowing, cooling and bouncing off walls and floors. Vulkan only. 0: the sparks of the effects as before" );
	v_gpuSparksAmount = ri.Cvar_Get( "v_gpuSparksAmount", "1", CVAR_ARCHIVE_ND, "How many of them (v_gpuSparks), 1 as meant, 0.25 to 3" );
	v_gpuSparksEmbers = ri.Cvar_Get( "v_gpuSparksEmbers", "1", CVAR_ARCHIVE_ND,
		"How long the sparks (v_gpuSparks) that land on a floor lie there glowing as embers: 1 as meant, 0 not at all, up to 3 times as long" );
	memset( &sp, 0, sizeof( sp ) );
	if ( !vk.deviceAddress ) {
		return;
	}
	VK_CreateBuffer( &sp.ring, sizeof( float ) * 4 * VK_SPARK_ROWS * VK_SPARKS,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, qfalse );
	sp.ringAddress = VK_SparksAddress( &sp.ring );
	for ( int i = 0; i < VK_FRAMES; i++ ) {
		VK_CreateBuffer( &sp.frame[i], sizeof( float ) * 4 * ( VK_SPARK_HEADER + VK_SPARK_BURST_ROWS * VK_SPARK_BURSTS ),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, qtrue );
		sp.frameAddress[i] = VK_SparksAddress( &sp.frame[i] );
	}
	// the distance of the level as a post set, the ring and the frame by address
	sp.layout = VK_CreateComputeLayout( sizeof( uint32_t ) * 8, &vk.postSetLayout );
	sp.shader = VK_CreateShader( vk_sparksComp, sizeof( vk_sparksComp ) );
	sp.pipeline = VK_CreateComputePipeline( sp.shader, sp.layout );
	sp.ready = qtrue;
}

void VK_SparksShutdown( void ) {
	if ( !sp.ready ) {
		return;
	}
	qvkDestroyPipeline( vk.device, sp.pipeline, NULL );
	qvkDestroyShaderModule( vk.device, sp.shader, NULL );
	qvkDestroyPipelineLayout( vk.device, sp.layout, NULL );
	VK_DestroyBuffer( &sp.ring );
	for ( int i = 0; i < VK_FRAMES; i++ ) {
		VK_DestroyBuffer( &sp.frame[i] );
	}
	memset( &sp, 0, sizeof( sp ) );
}

/*
===============
RE_EmitSparks

A burst for the next frame. qfalse: not taken (v_gpuSparks 0, no device addresses, too many bursts this frame), the
cgame plays what it played before.
===============
*/
qboolean RE_EmitSparks( const sparkBurst_t *burst ) {
	if ( !sp.ready || !v_gpuSparks->integer || !burst ) {
		return qfalse;
	}
	if ( sp.numBursts == VK_SPARK_BURSTS ) {
		// (taken all the same: a frame with that many has sparks enough)
		return qtrue;
	}
	const int count = Com_Clampi( 0, 4096, (int)( burst->count * Com_Clamp( 0.25f, 3.0f, v_gpuSparksAmount->value ) + 0.5f ) );
	if ( !count ) {
		return qtrue;
	}
	sp.bursts[sp.numBursts] = *burst;
	sp.firsts[sp.numBursts] = sp.head;
	sp.counts[sp.numBursts] = count;
	sp.numBursts++;
	sp.head = ( sp.head + count ) & ( VK_SPARKS - 1 );
	// (the embers' time once, when the burst is taken, so a change of the cvar never cuts a burst in two)
	sp.bursts[sp.numBursts - 1].rest = Com_Clamp( 0.0f, 10.0f, burst->rest * Com_Clamp( 0.0f, 3.0f, v_gpuSparksEmbers->value ) );
	const int rest = (int)( sp.bursts[sp.numBursts - 1].rest * 1000.0f );
	sp.aliveUntil = Q_max( sp.aliveUntil, ri.Milliseconds() + (int)( Com_Clamp( 0.05f, 10.0f, burst->life ) * 1000.0f ) + rest + 500 );
	if ( rest ) {
		sp.embersUntil = sp.aliveUntil;
	}
	return qtrue;
}

// the view whose distance vk.softDepth now holds (vk_post.cpp VK_SoftDepth), for the bounces of the next frame
// and the characters seen in it, which the distance has as well: an ember on a shoulder would hang in the air once
// the player walks on (shaders/sparks.comp OnBody)
void VK_SparksView( void ) {
	int viewport[4];
	int	count = 0;

	VK_ViewRows( sp.view, viewport );
	sp.viewValid = qtrue;
	memset( sp.bodies, 0, sizeof( sp.bodies ) );
	for ( int i = 0; i < backEnd.refdef.num_entities && count < VK_SPARK_BODIES; i++ ) {
		const refEntity_t *e = &backEnd.refdef.entities[i].e;

		// (the own body seen from its eyes is not drawn, so not in the distance)
		if ( e->reType != RT_MODEL || !e->ghoul2 || ( ( e->renderfx & RF_THIRD_PERSON ) && !backEnd.viewParms.isPortal ) ) {
			continue;
		}
		Vector4Set4( sp.bodies[count], e->origin[0], e->origin[1], e->origin[2], 28.0f );
		count++;
	}
}

/*
===============
VK_SparksSimulate

At the start of a frame, before its level is begun: the bursts of the frame into the ring and every spark a step on.
Nothing while none can be alive.
===============
*/
void VK_SparksSimulate( void ) {
	const int	now = ri.Milliseconds();
	const float	dt = sp.lastTime ? Com_Clamp( 0.0f, 0.1f, ( now - sp.lastTime ) * 0.001f ) : 0.0f;

	sp.lastTime = now;
	if ( !sp.ready || !vk.frame || ( !sp.numBursts && now > sp.aliveUntil ) ) {
		sp.numBursts = 0;
		return;
	}
	VkCommandBuffer	cmd = vk.frame->cmd;
	float			( *rows )[4] = (float ( * )[4])sp.frame[vk.frameIndex].mapped;
	const qboolean	depth = (qboolean)( sp.viewValid && vk.softShown >= 0 && vk.softDepth.image && vk.softSet );
	VkMemoryBarrier	barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };

	memcpy( rows, sp.view, sizeof( sp.view ) );
	memcpy( rows + 12, sp.bodies, sizeof( sp.bodies ) );
	Vector4Set4( rows[11], dt, depth ? 1.0f : 0.0f, (float)sp.numBursts, (float)( ++sp.seed & 0xffffff ) );
	for ( int b = 0; b < sp.numBursts; b++ ) {
		const sparkBurst_t	*s = &sp.bursts[b];
		float				( *e )[4] = rows + VK_SPARK_HEADER + b * VK_SPARK_BURST_ROWS;

		Vector4Set4( e[0], s->origin[0], s->origin[1], s->origin[2], (float)sp.firsts[b] );
		Vector4Set4( e[1], s->dir[0], s->dir[1], s->dir[2], (float)sp.counts[b] );
		Vector4Set4( e[2], s->color[0], s->color[1], s->color[2], Com_Clamp( 0.05f, 10.0f, s->life ) );
		Vector4Set4( e[3], s->speed, Com_Clamp( 0.0f, 2.0f, s->spread ), Com_Clamp( 0.1f, 16.0f, s->size ), s->gravity );
		Vector4Set4( e[4], Com_Clamp( 0.0f, 20.0f, s->drag ), Com_Clamp( 0.0f, 1.0f, s->bounce ),
			// (0 for one that floats: dust, it does not glow of its own)
			( s->swirl > 0.0f && s->rest <= 0.0f && s->cooling <= 0.0f ) ? 0.0f : Com_Clamp( 0.1f, 8.0f, s->cooling ), s->rest );
		// (those that float never lie)
		Vector4Set4( e[5], s->rest > 0.0f ? 0.0f : Com_Clamp( 0.0f, 400.0f, s->swirl ), Com_Clamp( 0.0f, 0.5f, s->streak ), Com_Clamp( 0.0f, 0.95f, s->age ), 0.0f );
	}
	sp.numBursts = 0;

	if ( !sp.cleared ) {
		qvkCmdFillBuffer( cmd, sp.ring.buffer, 0, VK_WHOLE_SIZE, 0 );
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		qvkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL );
		sp.cleared = qtrue;
	}
	// (the draw of the frame before read the ring)
	barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	qvkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL );
	if ( vk.softDepth.image ) {
		VK_TargetLayout( cmd, &vk.softDepth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	}
	{
		struct {
			VkDeviceAddress	ring, frame;
			uint32_t		count;
			float			time;		// seconds, for the air that carries those that float (it wraps every hour and a bit)
			uint32_t		pad[2];
		} push = { sp.ringAddress, sp.frameAddress[vk.frameIndex], VK_SPARKS, ( now & 0x3fffff ) * 0.001f, { 0, 0 } };

		qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sp.pipeline );
		qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sp.layout, 0, 1, &vk.softSet, 0, NULL );
		qvkCmdPushConstants( cmd, sp.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( push ), &push );
		qvkCmdDispatch( cmd, VK_SPARKS / 256, 1, 1 );
	}
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	qvkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL );
}

/*
===============
VK_SparksDraw

Over the solid level of the view of the level (vk_backend.cpp RB_DrawWet): the ring as streaks, added, through
generic.frag with the round falloff of the dynamic lights as their picture, then the glow of the embers on their
floors (probeCount 1 for shaders/sparks.vert). transform: the one of the level.
===============
*/
void VK_SparksDraw( uint32_t transform ) {
	vkDraw_t draw;

	if ( !sp.ready || !v_gpuSparks->integer || ri.Milliseconds() > sp.aliveUntil || !sp.cleared || !tr.dlightImage ) {
		return;
	}
	memset( &draw, 0, sizeof( draw ) );
	draw.transform = transform;
	draw.tex0 = draw.tex1 = tr.dlightImage->texnum;
	draw.tex5 = (uint32_t)( sp.ringAddress & 0xffffffffu );
	draw.tex6 = (uint32_t)( sp.ringAddress >> 32 );
	Vector4Set4( draw.vertColor, 1.0f, 1.0f, 1.0f, 1.0f );
	// the texture coordinates of the vertex as they are (shaders/stage.glsl GenTexCoord, ModTexCoord)
	draw.flags = ( TCGEN_TEXTURE << VKD_TCGEN0_SHIFT ) | ( TCGEN_TEXTURE << VKD_TCGEN1_SHIFT );
	draw.texMat0[0] = draw.texMat0[5] = 1.0f;
	draw.texMat1[0] = draw.texMat1[5] = 1.0f;
	vk.drawVertices = VK_SPARKS * 6;
	VK_DrawTess( &draw, VKP_SPARKS, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE, VKC_NONE, qfalse );
	if ( ri.Milliseconds() <= sp.embersUntil ) {
		draw.probeCount = 1;
		VK_DrawTess( &draw, VKP_SPARKS, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE, VKC_NONE, qfalse );
	}
	vk.drawVertices = 0;
}
