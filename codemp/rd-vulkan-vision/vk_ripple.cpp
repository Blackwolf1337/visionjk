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

// vk_ripple.cpp -- rings on the water of the level (the wet places of the kind WET_SURFACE of RE_AddWet,
// cgame/level/cg_ripples.c). The water of a level is a transparent surface that writes no depth, so the wet places of
// VK_DrawWet(), which lie on the level under the pixel, can not be on it. These are drawn by a pass of their own after
// everything transparent of the view of the level (shaders/ripple.frag): it finds the water where the ray of a pixel
// meets its height. Water is level, so a ring has a height and no plane; rings at one height are one surface, of
// which a view has few. Needs the depth of the scene as a texture (vk.depthReadable).

#include "vk_local.h"

static const uint32_t vk_rippleFrag[] =
#include "shaders/ripple_frag.inc"
;

#define RIPPLE_SAME_HEIGHT	1.5f	// rings whose heights differ by less are on one surface
#define RIPPLE_FAR			2200.0f	// FAR_OFF of shaders/ripple.frag
#define RIPPLE_MARGIN		48		// pixels round the part of the frame that is copied: the picture is read to the side

static struct {
	VkShaderModule	shader;
	VkPipeline		pipeline;
} rp;

void VK_RippleInit( void ) {
	memset( &rp, 0, sizeof( rp ) );
	if ( !vk.depthReadable ) {
		return;
	}
	rp.shader = VK_CreateShader( vk_rippleFrag, sizeof( vk_rippleFrag ) );
	rp.pipeline = VK_CreatePipeline( vk.postVertexShader, rp.shader, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA, 8, VKP_GENERIC );
}

void VK_RippleShutdown( void ) {
	if ( rp.pipeline ) {
		qvkDestroyPipeline( vk.device, rp.pipeline, NULL );
	}
	if ( rp.shader ) {
		qvkDestroyShaderModule( vk.device, rp.shader, NULL );
	}
	memset( &rp, 0, sizeof( rp ) );
}

/*
===============
VK_RippleDraw

In the view of the level itself, after its draw list: the rings of this frame over the part of the view they cover.
The frame is copied there first (the pass reads it to the side of each pixel) and begun again afterwards. Rings seen
through thick glass are drawn before it instead (part RIPPLE_BEHIND_GLASS, vk_backend.cpp where the thick glass
begins), as the smoke is: the glass bends the water it shows, and the rings drawn after it lay unbent on that (their
word 2026-11-27: water inside the glass gets the bulge of the glass on its rings, water that is not stays as it was).
===============
*/
void VK_RippleDraw( int part ) {
#ifndef RD_VISION_GLES
	VK_Checkpoint( "the rings on the water" );
#endif
	float			rows[VK_WET_HEADER][4] = { { 0.0f } };
	int				viewport[4];
	float			x0 = 1.0f, y0 = 1.0f, x1 = -1.0f, y1 = -1.0f;
	const float		*eye = backEnd.viewParms.ori.origin;
	float			heights[VK_RIPPLE_SURFACES];
	int				surfaceOf[WET_MAX_SURFACE];
	int				counts[VK_RIPPLE_SURFACES] = { 0 };
	int				surfaces = 0, rings = 0;

	if ( !vk.frame || !rp.pipeline || r_numRipples <= 0 || !tr.screenImage ) {
		return;
	}
	if ( ( backEnd.viewParms.isPortal && !backEnd.viewParms.subView ) || backEnd.viewParms.isPuddle || backEnd.viewParms.probeFace
		|| ( backEnd.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) ) ) {
		return;
	}
	// the rings of which something is in the view, each on the surface of its height
	for ( int i = 0; i < r_numRipples; i++ ) {
		const wetSpot_t	*spot = &r_ripples[i];
		float			bounds[4];
		int				s;

		surfaceOf[i] = -1;
		if ( spot->radius <= 0.0f || spot->wet <= 0.0f || spot->flow >= 1.0f
			|| DistanceSquared( spot->origin, eye ) > Square( RIPPLE_FAR + spot->radius )
			|| !VK_BallBounds( spot->origin, spot->radius, bounds ) ) {
			continue;
		}
		for ( s = 0; s < surfaces; s++ ) {
			if ( fabsf( heights[s] - spot->origin[2] ) < RIPPLE_SAME_HEIGHT ) {
				break;
			}
		}
		if ( s == surfaces ) {
			if ( surfaces == VK_RIPPLE_SURFACES ) {
				continue;
			}
			heights[surfaces++] = spot->origin[2];
		}
		surfaceOf[i] = s;
		counts[s]++;
		rings++;
		x0 = Q_min( x0, bounds[0] );
		y0 = Q_min( y0, bounds[1] );
		x1 = Q_max( x1, bounds[2] );
		y1 = Q_max( y1, bounds[3] );
	}
	if ( !rings || x0 >= x1 || y0 >= y1 ) {
		return;
	}
	VK_ViewRows( rows, viewport );

	// the part of the target: rows[8] is the rect of the view in its pixels, y down
	const float	px0 = rows[8][0] + ( x0 * 0.5f + 0.5f ) * rows[8][2], px1 = rows[8][0] + ( x1 * 0.5f + 0.5f ) * rows[8][2];
	const float	py0 = rows[8][1] + ( 0.5f - y1 * 0.5f ) * rows[8][3], py1 = rows[8][1] + ( 0.5f - y0 * 0.5f ) * rows[8][3];
	const int	area[4] = { (int)floorf( px0 ), (int)floorf( py0 ), (int)ceilf( px1 ) - (int)floorf( px0 ), (int)ceilf( py1 ) - (int)floorf( py0 ) };

	if ( area[2] <= 0 || area[3] <= 0 ) {
		return;
	}
	vkGlobals_t	*globals = VK_GlobalsEdit();
	float		( *r )[4] = &globals->ripple[VK_WET_HEADER];
	int			first = 0;

	memcpy( globals->ripple[0], rows, sizeof( rows ) );
	// (water that was made glass writes its depth: the rings are held against the solid level alone where the view
	// has its distance, or they were gone from such water. In the middle of the list, for the rings behind thick glass,
	// it is there once the passes after the solid level made it (vk.frontReady): vk.solidReady is set at the end of the
	// list only, and against the depth, which holds the glass by then, every ring behind it was gone (their picture
	// 2026-11-27, "jetzt fehlen die ringe"); as vk_smoke.cpp asks)
	const qboolean solid = (qboolean)( ( vk.frontReady || vk.solidReady ) && vk.softDepth.image );

	// Which rings: those behind thick glass (part RIPPLE_BEHIND_GLASS, called before the glass takes its copy of the
	// frame, so it bends them with the water they lie on), the others (RIPPLE_IN_FRONT), or all where the view has no
	// thick glass. Told by the faces of the thick glass the eye sees (VK_GlassMask; for the later part the copy
	// VK_KeepThickGlass made before the thin panes made them again): where the nearest thing at a pixel is nearer
	// than the water, glass stands in front of it there (shaders/ripple.frag). w: 0 all, 1 behind, 2 not behind.
	const vkTarget_t	*faces = NULL;

	if ( part == RIPPLE_BEHIND_GLASS ) {
		faces = vk.glassReady ? &vk.glassDepth : NULL;
		if ( !faces ) {
			return;
		}
	} else if ( vk.thickReady ) {
		faces = &vk.thickDepth;
	}
	VectorSet4( globals->ripple[9], 1.0f / (float)vk.screen.width, 1.0f / (float)vk.screen.height, solid ? 1.0f : 0.0f,
		faces ? ( part == RIPPLE_BEHIND_GLASS ? 1.0f : 2.0f ) : 0.0f );
	VectorSet4( r[0], (float)surfaces, (float)rings, (float)fmod( backEnd.refdef.floatTime, 1000.0 ),
		(float)( RIPPLE_MARGIN - 4 ) );
	for ( int s = 0; s < surfaces; s++ ) {
		float ( *ring )[4] = &r[1 + VK_RIPPLE_SURFACES + first * VK_RIPPLE_ROWS];

		VectorSet4( r[1 + s], heights[s], (float)first, (float)counts[s], 0.0f );
		first += counts[s];
		for ( int i = 0; i < r_numRipples; i++ ) {
			const wetSpot_t	*spot = &r_ripples[i];
			vec3_t			ambient, directed, dir;

			if ( surfaceOf[i] != s ) {
				continue;
			}
			// (the light a little over the water: in it the level may have none)
			vec3_t over = { spot->origin[0], spot->origin[1], spot->origin[2] + 8.0f };

			VK_LiquidLight( over, ambient, directed, dir );
			VectorNormalize( dir );
			VectorSet4( ring[0], spot->origin[0], spot->origin[1], Com_Clamp( 4.0f, 512.0f, spot->radius ), Com_Clamp( 0.0f, 1.0f, spot->wet ) );
			VectorSet4( ring[1], Com_Clamp( 0.0f, 1.0f, spot->flow ), Com_Clamp( 0.0f, 65535.0f, spot->seed ), spot->puddle, 0.0f );
			VectorSet4( ring[2], ambient[0] / 255.0f, ambient[1] / 255.0f, ambient[2] / 255.0f, 0.0f );
			VectorSet4( ring[3], directed[0] / 255.0f, directed[1] / 255.0f, directed[2] / 255.0f, 0.0f );
			VectorSet4( ring[4], dir[0], dir[1], dir[2], 0.0f );
			ring += VK_RIPPLE_ROWS;
		}
	}

	VK_GlobalsCommit();
	const VkDescriptorSet	depthSet = solid ? vk.softDepth.readSet : vk.depth.readSet;
	const VkDescriptorSet	sets[4] = { vk.frame->set, vk.screen.readSet, depthSet, faces ? faces->readSet : depthSet };
	VkCommandBuffer			cmd = vk.frame->cmd;
	VkRect2D				scissor;

	// (the count of the fragments of the level runs in its render pass: stopped, and in the middle of the list, before
	// the thick glass, taken up again afterwards)
	const qboolean			counting = part == RIPPLE_BEHIND_GLASS ? VK_PauseLevelFragments() : qfalse;

	if ( part != RIPPLE_BEHIND_GLASS ) {
		VK_LevelFragments( qfalse );
	}
	VK_EndTarget();
	// the part of the frame the pass reads, with what lies to the side of it
	memcpy( vk.passArea, area, sizeof( vk.passArea ) );
	VK_CopyTargetArea( &vk.color, &vk.screen, RIPPLE_MARGIN );
	memset( vk.passArea, 0, sizeof( vk.passArea ) );

	VK_BeginOverLevel( cmd, area, qfalse );
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rp.pipeline );
	vk.boundPipeline = rp.pipeline;
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.screenFxPipelineLayout, 0, 4, sets, 1, &vk.frame->globalsOffset );
	VK_ViewportAndScissor( viewport[0], viewport[1], viewport[2], viewport[3], 0.0f, 1.0f );
	scissor.offset.x = area[0];
	scissor.offset.y = area[1];
	scissor.extent.width = (uint32_t)area[2];
	scissor.extent.height = (uint32_t)area[3];
	qvkCmdSetScissor( cmd, 0, 1, &scissor );
	qvkCmdDraw( cmd, 3, 1, 0, 0 );
	VK_EndTarget();

	VK_BeginTarget( &vk.color, &vk.depth, 2, qtrue );
	// the shaders that draw geometry have other sets and other state
	VK_ResumeScene();
	VK_InvalidateTransform();
	if ( counting ) {
		VK_LevelFragments( qtrue );
	}
}
