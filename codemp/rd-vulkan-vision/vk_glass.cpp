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

// vk_glass.cpp -- v_glassDebug 1: how thick glass gets what it shows, in colours over the view of the level. Told by
// two distances of a pixel: of everything solid before the glass (vk.softDepth) and of the nearest of that and the
// glass (vk.glassDepth). Blue: a face of glass. A yellow frame: the part of the view a view of what lies behind a
// plane of the glass was drawn for (R_GlassViews of tr_main.cpp); red inside it: what stands in front of that plane,
// where the glass reads that view in place of its copy of the frame.

#include "vk_local.h"

static const uint32_t vk_glassDebugFrag[] =
#include "shaders/glassdebug_frag.inc"
;

static struct {
	VkShaderModule	shader;
	VkPipeline		pipeline;
} gd;

qboolean vk_glassDebugSolid;	// v_glassDebug 6: the distance of the solid level in bands

void VK_GlassDebugInit( void ) {
	memset( &gd, 0, sizeof( gd ) );
	if ( !vk.depthReadable ) {
		return;
	}
	gd.shader = VK_CreateShader( vk_glassDebugFrag, sizeof( vk_glassDebugFrag ) );
	gd.pipeline = VK_CreatePipeline( vk.postVertexShader, gd.shader, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA, 8, VKP_GENERIC );
}

void VK_GlassDebugShutdown( void ) {
	if ( gd.pipeline ) {
		qvkDestroyPipeline( vk.device, gd.pipeline, NULL );
	}
	if ( gd.shader ) {
		qvkDestroyShaderModule( vk.device, gd.shader, NULL );
	}
	memset( &gd, 0, sizeof( gd ) );
}

// in the view of the level, at its end, where it has thick or thin glass (its picture was taken, vk.glassReady)
void VK_GlassDebugDraw( void ) {
	float	rows[VK_WET_HEADER][4] = { { 0.0f } };
	int		viewport[4];

	if ( !vk.frame || !gd.pipeline || !vk.glassReady || !vk.solidReady || !vk.softDepth.image || !vk.glassDepth.image ) {
		return;
	}
	if ( backEnd.viewParms.isPortal || backEnd.viewParms.isPuddle || backEnd.viewParms.probeFace ) {
		return;
	}
	vkGlobals_t				*globals = VK_GlobalsEdit();
	const VkDescriptorSet	sets[4] = { vk.frame->set, vk.softDepth.readSet, vk.glassDepth.readSet, vk.depth.readSet };
	VkCommandBuffer			cmd = vk.frame->cmd;

	VK_ViewRows( rows, viewport );
	memcpy( globals->glassDebug, rows, sizeof( rows ) );
	VectorSet4( globals->glassDebug[VK_WET_HEADER], vk_glassDebugSolid ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f );
	VK_GlobalsCommit();
	VK_LevelFragments( qfalse );
	VK_EndTarget();
	VK_BeginOverLevel( cmd, NULL, qfalse );
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gd.pipeline );
	vk.boundPipeline = gd.pipeline;
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.screenFxPipelineLayout, 0, 4, sets, 1, &vk.frame->globalsOffset );
	VK_ViewportAndScissor( viewport[0], viewport[1], viewport[2], viewport[3], 0.0f, 1.0f );
	qvkCmdDraw( cmd, 3, 1, 0, 0 );
	VK_EndTarget();

	VK_BeginTarget( &vk.color, &vk.depth, 2, qtrue );
	// the shaders that draw geometry have other sets and other state
	VK_ResumeScene();
	VK_InvalidateTransform();
}
