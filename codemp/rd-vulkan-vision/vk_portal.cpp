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

// vk_portal.cpp -- the openings of the portals of the portal gun (ext.AddPortalToScene, cgame/portal/cg_portal_draw.c).
// The wall a portal sits on is drawn as it always is; this pass lays the opening onto it after the solid level and
// before what is transparent (vk_backend.cpp, phase 3), by the ray of each pixel and not by a surface of its own: where
// the ray meets the plane of a portal inside its ellipse and nothing solid stands in front of that point, the pixel
// shows the picture of the view through the portal made for the view being drawn (R_PortalViews of tr_main.cpp, kept by
// VK_EndSubView), read where that view saw the point: the pixel itself, as the picture was drawn for the same eye in the
// same frame. A view through a portal draws the openings it sees with the pictures of the views made inside it, up to
// v_portalDepth deep. Round the ellipse a rim of the colour of the portal moves, and a portal without a view through it
// (closed, too deep, or one too many for v_portalViews) shows a swirl in its colour. Drawn over the frame as the rings on the water are
// (VK_BeginOverLevel), so it takes the samples of r_ext_multisample, and before the tone map, v_smaa and the bloom.
// Needs the depth of the scene as a texture (vk.depthReadable).

#include "vk_local.h"

static const uint32_t vk_portalFrag[] =
#include "shaders/portal_frag.inc"
;

#define PORTAL_RIM		6.0f	// how far round the ellipse the rim glows, in units (RIM_OUT of shaders/portal.frag)

static struct {
	VkShaderModule	shader;
	VkPipeline		pipeline;
} pp;

void VK_PortalInit( void ) {
	memset( &pp, 0, sizeof( pp ) );
	if ( !vk.depthReadable ) {
		return;
	}
	pp.shader = VK_CreateShader( vk_portalFrag, sizeof( vk_portalFrag ) );
	// (9: the sets of the sprayed pictures, globals, two targets and the texture table, where the pictures are)
	pp.pipeline = VK_CreatePipeline( vk.postVertexShader, pp.shader, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA, 9, VKP_GENERIC );
}

void VK_PortalShutdown( void ) {
	if ( pp.pipeline ) {
		qvkDestroyPipeline( vk.device, pp.pipeline, NULL );
	}
	if ( pp.shader ) {
		qvkDestroyShaderModule( vk.device, pp.shader, NULL );
	}
	memset( &pp, 0, sizeof( pp ) );
}

// a point of the world on the target as rows[0] to rows[3] and rows[8] of VK_ViewRows put it, in pixels from the upper
// left. qfalse: at or behind the eye.
static qboolean VK_PortalOnTarget( const float ( *rows )[4], const vec3_t p, float *x, float *y ) {
	float clip[4];

	for ( int k = 0; k < 4; k++ ) {
		clip[k] = rows[0][k] * p[0] + rows[1][k] * p[1] + rows[2][k] * p[2] + rows[3][k];
	}
	if ( clip[3] < 1.0f ) {
		return qfalse;
	}
	*x = rows[8][0] + ( clip[0] / clip[3] * 0.5f + 0.5f ) * rows[8][2];
	*y = rows[8][1] + ( 0.5f - clip[1] / clip[3] * 0.5f ) * rows[8][3];
	return qtrue;
}

/*
The picture (index into vk.behind) of the view through a portal made for the view being drawn, -1: none. A picture
belongs to one place in the tree of the views of the scene (portalViewRecord_t, R_PortalViews): the view of the level
and its views behind glass, which have its eye, read those made for it; a view through a portal those made for it. It
is there when its view was drawn in this frame, before the view that reads it (RB_DrawDeferredViews). Without it the
opening shows its swirl: a picture of another eye laid onto it showed the edges of what it holds stretched into streaks.
*/
static int VK_PortalPicture( const portalView_t *p ) {
	const portalViewRecord_t	*records = backEnd.refdef.portalViews;
	const int					self = backEnd.viewParms.portalView ? backEnd.viewParms.subView - 1 - GLASS_VIEWS : -1;

	if ( !p->open || !records ) {
		return -1;
	}
	for ( int k = 0; k < PORTAL_VIEWS; k++ ) {
		const int picture = GLASS_VIEWS + k;

		if ( records[k].sceneNum == backEnd.viewParms.frameSceneNum && records[k].parent == self && records[k].portalId == p->id
			&& vk.behind[picture].image && vk.behindShown[picture] >= 0 && vk.behindId[picture] == p->id
			&& vk.behindFrame[picture] == backEnd.viewParms.frameCount ) {
			return picture;
		}
	}
	return -1;
}

/*
===============
VK_PortalDraw

In the view of the level and in the views of their own, after the solid level: the openings of the portals of the scene
in front of the eye, over the part of the view they and their rims cover. W (globals.portal): the view (VK_ViewRows),
then how many / the seconds / v_portalDebug 2, then VK_PORTAL_ROWS rows a portal: its middle and half its width, its
left and half its height, its up and the entry of its picture in the texture table + 1 (0: none, the swirl), its colour
and a number of its own for the swirl, the matrix and the rect of the view its picture was drawn for (VK_EndSubView),
the part of the frame that was copied into it (left, top, right, bottom over the size of the frame).
===============
*/
void VK_PortalDraw( void ) {
	float		rows[VK_WET_HEADER][4] = { { 0.0f } };
	int			viewport[4];
	float		x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
	int			count = 0;
	const float	*eye = backEnd.viewParms.ori.origin;

	if ( !vk.frame || !pp.pipeline || !vk.depthReadable || backEnd.refdef.numPortals <= 0 ) {
		return;
	}
	if ( ( backEnd.viewParms.isPortal && !backEnd.viewParms.subView ) || backEnd.viewParms.isPuddle || backEnd.viewParms.probeFace
		|| ( backEnd.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) ) ) {
		return;
	}
	VK_Checkpoint( "the openings of the portals" );
	VK_ViewRows( rows, viewport );

	vkGlobals_t	*globals = VK_GlobalsEdit();
	float		( *r )[4] = &globals->portal[VK_WET_HEADER + 1];

	for ( int i = 0; i < backEnd.refdef.numPortals && count < MAX_SCENE_PORTALS; i++ ) {
		const scenePortal_t	*sp = &backEnd.refdef.portals[i];
		const portalView_t	*p = &sp->view;
		float				px0 = 1e9f, py0 = 1e9f, px1 = -1e9f, py1 = -1e9f;
		qboolean			whole = qfalse;
		vec3_t				to;

		// (the portal a view through a portal looks out of lies on the plane it is cut at, and the view is all of it; what
		// lies behind that plane is not in the view, where nothing stands in front of it the depth would not hide it)
		if ( backEnd.viewParms.portalView && ( p->id == backEnd.viewParms.portalExit
			|| DotProduct( p->origin, backEnd.viewParms.portalPlane.normal ) < backEnd.viewParms.portalPlane.dist ) ) {
			continue;
		}
		// an opening is seen from in front of its wall alone
		VectorSubtract( eye, p->origin, to );
		if ( DotProduct( to, p->axis[0] ) <= 0.0f ) {
			continue;
		}
		// what it and its rim cover of the target
		for ( int k = 0; k < 4 && !whole; k++ ) {
			vec3_t	corner;
			float	x, y;

			VectorMA( p->origin, ( k & 1 ) ? p->halfWidth + PORTAL_RIM : -p->halfWidth - PORTAL_RIM, p->axis[1], corner );
			VectorMA( corner, ( k & 2 ) ? p->halfHeight + PORTAL_RIM : -p->halfHeight - PORTAL_RIM, p->axis[2], corner );
			if ( !VK_PortalOnTarget( rows, corner, &x, &y ) ) {
				whole = qtrue;
				break;
			}
			px0 = Q_min( px0, x );
			py0 = Q_min( py0, y );
			px1 = Q_max( px1, x );
			py1 = Q_max( py1, y );
		}
		if ( whole ) {
			px0 = rows[8][0];
			py0 = rows[8][1];
			px1 = rows[8][0] + rows[8][2];
			py1 = rows[8][1] + rows[8][3];
		}
		if ( px1 < rows[8][0] || py1 < rows[8][1] || px0 > rows[8][0] + rows[8][2] || py0 > rows[8][1] + rows[8][3] ) {
			continue;
		}
		x0 = Q_min( x0, px0 );
		y0 = Q_min( y0, py0 );
		x1 = Q_max( x1, px1 );
		y1 = Q_max( y1, py1 );

		float ( *row )[4] = &r[count * VK_PORTAL_ROWS];

		VectorCopy( p->origin, row[0] );
		row[0][3] = p->halfWidth;
		VectorCopy( p->axis[1], row[1] );
		row[1][3] = p->halfHeight;
		VectorCopy( p->axis[2], row[2] );
		VectorSet4( row[3], p->color[0] / 255.0f, p->color[1] / 255.0f, p->color[2] / 255.0f, (float)( ( p->id * 37 ) & 255 ) / 255.0f );
		const int picture = VK_PortalPicture( p );

		if ( picture >= 0 ) {
			row[2][3] = (float)( vk.behindShown[picture] + 1 );
			memcpy( row[4], vk.behindClip[picture], sizeof( float ) * 16 );
			VectorCopy4( vk.behindRect[picture], row[8] );
			// (a pixel in from where it was copied to: the sampler reads between pixels)
			VectorSet4( row[9], ( vk.behindArea[picture][0] + 1.0f ) / vk.width, ( vk.behindArea[picture][1] + 1.0f ) / vk.height,
				( vk.behindArea[picture][2] - 1.0f ) / vk.width, ( vk.behindArea[picture][3] - 1.0f ) / vk.height );
		} else {
			row[2][3] = 0.0f;
		}
		count++;
	}
	if ( !count ) {
		return;
	}
	// the part of the target: the view, and in a view of its own no more than its part of the frame
	int area[4] = { (int)floorf( Q_max( x0, rows[8][0] ) ), (int)floorf( Q_max( y0, rows[8][1] ) ),
		(int)ceilf( Q_min( x1, rows[8][0] + rows[8][2] ) ), (int)ceilf( Q_min( y1, rows[8][1] + rows[8][3] ) ) };

	if ( vk.limitOn ) {
		area[0] = Q_max( area[0], vk.limit[0] );
		area[1] = Q_max( area[1], vk.limit[1] );
		area[2] = Q_min( area[2], vk.limit[2] );
		area[3] = Q_min( area[3], vk.limit[3] );
	}
	area[0] = Com_Clampi( 0, vk.width, area[0] );
	area[1] = Com_Clampi( 0, vk.height, area[1] );
	area[2] = Com_Clampi( 0, vk.width, area[2] ) - area[0];
	area[3] = Com_Clampi( 0, vk.height, area[3] ) - area[1];
	if ( area[2] <= 0 || area[3] <= 0 ) {
		return;
	}
	memcpy( globals->portal[0], rows, sizeof( rows ) );
	VectorSet4( globals->portal[VK_WET_HEADER], (float)count, (float)fmod( backEnd.refdef.floatTime, 1000.0 ),
		( v_portalDebug && v_portalDebug->integer == 2 ) ? 1.0f : 0.0f, 0.0f );
	VK_GlobalsCommit();

	const VkDescriptorSet	sets[4] = { vk.frame->set, vk.depth.readSet, vk.depth.readSet, vk.textureSet };
	VkCommandBuffer			cmd = vk.frame->cmd;
	VkRect2D				scissor;
	// (the count of the fragments of the level runs in its render pass: stopped while this pass is drawn and taken up
	// again afterwards, as the passes in the middle of the list do it)
	const qboolean			counting = VK_PauseLevelFragments();

	VK_EndTarget();
	VK_BeginOverLevel( cmd, area, qfalse );
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pp.pipeline );
	vk.boundPipeline = pp.pipeline;
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.paintPipelineLayout, 0, 4, sets, 1, &vk.frame->globalsOffset );
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
