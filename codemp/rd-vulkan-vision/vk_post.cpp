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

// vk_post.cpp: the glow of r_DynamicGlow, the copy of the frame for refraction, and the depth test of flares
//
// The basic idea, as in rd-vanilla: render the glowing parts of the scene to a buffer of their own, blur that
// buffer, and add it back to the scene. The glowing parts are the stages of shaders that carry the "glow"
// keyword (saber blades, lights, screens). They are drawn a second time over the depth of the finished scene,
// so what is in front of them covers them. The blur is r_DynamicGlowPasses passes of four samples each at
// r_DynamicGlowWidth x r_DynamicGlowHeight, every pass r_DynamicGlowDelta further out.
//
// This file says which passes there are and in which order. How a pass reaches the device is vk_cmd.cpp, the
// targets are made in vk_texture.cpp.

#include "vk_local.h"

void VK_InitPost( void ) {
	const int width = Com_Clampi( 16, vk.width, r_DynamicGlowWidth->integer );
	const int height = Com_Clampi( 16, vk.height, r_DynamicGlowHeight->integer );

	VK_CreatePostTargets( width, height );
	vk.screenSlot = -1;
}

/*
===============
VK_BeginGlow

The scene is drawn. What is drawn until VK_EndGlow() goes into the glow buffer, tested against the depth of
the scene.
===============
*/
void VK_BeginGlow( void ) {
	if ( !vk.frame ) {
		return;
	}
	VK_EndTarget();
	VK_BeginTarget( &vk.glow, &vk.depth, 1, qtrue );
	vk.stateValid = qfalse;
}

/*
===============
VK_EndGlow

Blurs the glow buffer and adds it to the frame, then the frame is drawn into again
===============
*/
void VK_EndGlow( void ) {
	const int passes = Com_Clampi( 1, 16, r_DynamicGlowPasses->integer );
	vkTarget_t *source = &vk.glow;
	float offset = 0.1f;		// in pixels of the source

	if ( !vk.frame ) {
		return;
	}
	VK_EndTarget();

	for ( int i = 0; i < passes; i++ ) {
		vkTarget_t *target = &vk.blur[i & 1];
		const float parm[4] = { offset / source->width, offset / source->height, r_DynamicGlowIntensity->value * 0.25f, 0.0f };

		VK_BlurPass( source, target, parm );

		source = target;
		// Increase the texel offsets. NOTE: This is possibly the most important input to the effect.
		offset += r_DynamicGlowDelta->value;
	}

	// back to the frame, with what is in it
	VK_BeginTarget( &vk.color, &vk.depth, 2, qtrue );
	if ( r_DynamicGlow->integer == 2 ) {
		// show the glow alone
		const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

		VK_ClearColor( 0, 0, vk.width, vk.height, black );
	}
	VK_AddTarget( source, r_DynamicGlowSoft->integer ? VKA_ADD_SOFT : VKA_ADD );

	// the shaders that draw geometry have other sets and other state
	VK_BindFrameSets();
	vk.stateValid = qfalse;
	VK_SetViewport( vk.viewport[0], vk.viewport[1], vk.viewport[2], vk.viewport[3], 0.0f, 1.0f );
}

/*
===============
VK_BlurFrame

The depth of field a module asked for with RE_SetDepthOfField(): the menu in the level of the cgame, which is
drawn afterwards and stays sharp. What is farther from the eye than focus goes out of focus, the more the
farther it is; what is nearer stays as it was drawn (the player next to the menu). amount is 0 to 1 from the
module (the menu comes and goes), r_menuBlur is how soft the player wants it: 0 not at all, 1 as it is meant,
up to 4.

The frame goes into a buffer of half its size first: the four samples of that pass are the four pixels behind one
of the buffer, so nothing flickers. Then passes in that size widen it, each half a pixel further out than the
one before, three of them at r_menuBlur 1. The glow has buffers too (vk.blur), but they are 320 x 240 by
default: the scene is unrecognisable after one pass through them, which is what the first version of this did.

A last pass gives every pixel of the soft picture how much of it shows, from the depth of the scene at that
pixel (shaders/focus.frag), and the picture is laid over the frame by that. A device that can not read its
depth gets the whole frame soft: it is cleared and the soft picture is added to it, the way r_DynamicGlow 2
shows the glow alone.
===============
*/
void VK_BlurFrame( float amount, float focus ) {
	const float	strength = Com_Clamp( 0.0f, 4.0f, r_menuBlur->value );
	const int	passes = Com_Clampi( 1, 12, (int)( strength * 3.0f + 0.5f ) );
	const float	black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	const float	*projection = backEnd.viewParms.projectionMatrix;
	vkTarget_t	*source = &vk.color;

	if ( !vk.frame || amount <= 0.0f || strength <= 0.0f ) {
		return;
	}
	amount = Q_min( amount, 1.0f );
	VK_EndTarget();

	for ( int i = 0; i <= passes; i++ ) {
		vkTarget_t *target = &vk.soft[i & 1];
		// in pixels of the source, four samples and a quarter of each
		const float offset = i ? ( i - 0.5f ) * amount : 0.5f;
		const float parm[4] = { offset / source->width, offset / source->height, 0.25f, 0.0f };

		VK_BlurPass( source, target, parm );

		source = target;
	}

	if ( vk.depthReadable && focus > 0.0f ) {
		vkTarget_t *target = &vk.soft[source == &vk.soft[0] ? 1 : 0];
		// What the depth buffer holds is ( z + 1 ) / 2 of the projection, 0 at zNear and 1 at zFar: the distance
		// is m[14] / ( 2 depth - 1 + m[10] ). The effect is all there after the first third of the way in, the
		// picture of half the size is soft from its first pass on.
		const float parm[4] = { projection[14] * 0.5f, ( projection[10] - 1.0f ) * 0.5f, focus, Q_min( amount * 3.0f, 1.0f ) };

		VK_FocusPass( source, target, parm );
		VK_BeginTarget( &vk.color, &vk.depth, 2, qtrue );
		VK_AddTarget( target, VKA_BLEND );
	} else {
		// back to the frame, which is the soft picture from here on
		VK_BeginTarget( &vk.color, &vk.depth, 2, qtrue );
		VK_ClearColor( 0, 0, vk.width, vk.height, black );
		VK_AddTarget( source, VKA_ADD );
	}

	// the shaders that draw geometry have other sets and other state
	VK_BindFrameSets();
	vk.stateValid = qfalse;
	VK_SetViewport( vk.viewport[0], vk.viewport[1], vk.viewport[2], vk.viewport[3], 0.0f, 1.0f );
}

/*
===============
VK_CaptureFrame

Copies the frame as it is right now into vk.screen, which the stages of refraction surfaces then read through
the texture table in the place of tr.screenImage. rd-vanilla copies a square of the screen into that image
(glCopyTexImage2D), here the whole frame is copied and the draw is told where its square is.
===============
*/
void VK_CaptureFrame( void ) {
	if ( !vk.frame || !tr.screenImage ) {
		return;
	}
	VK_EndTarget();
	VK_CopyTarget( &vk.color, &vk.screen );

	// the entry of tr.screenImage in the texture table shows the copy from now on
	if ( vk.screenSlot != (int)tr.screenImage->texnum ) {
		vk.screenSlot = (int)tr.screenImage->texnum;
		VK_ShowTarget( vk.screenSlot, &vk.screen );
	}

	VK_BeginTarget( &vk.color, &vk.depth, 2, qtrue );
	vk.stateValid = qfalse;
}

/*
===============================================================================

FLARES

A flare (the shine around a lamp) is drawn when nothing is in front of its point. rd-vanilla finds that out by
reading one pixel of the depth buffer while it draws. The commands of a frame are only recorded here, their
depth does not exist yet. So the pixel is copied to a buffer at the end of the view, and the answer is there
when the commands of that frame have run: two frames later, when its buffers are used again. Until then the
flare keeps the answer it had. A flare shows up or goes away two frames late.

===============================================================================
*/

typedef struct vkFlare_s {
	const void	*key;				// the origin of the flare surface in the level data
	qboolean	visible;			// the last answer
	int			asked;				// vk_flareFrame of the last question, the oldest one is given up
	int			probed;				// vk_flareFrame of the last probe: one in a frame is enough
} vkFlare_t;

static vkFlare_t	vk_flares[VK_MAX_FLARES];
static int			vk_numFlares;
static int			vk_flareFrame = 1;

// the level changed, the keys mean nothing any more
void VK_ClearFlares( void ) {
	vk_numFlares = 0;
	for ( int i = 0; i < VK_FRAMES; i++ ) {
		vk.frames[i].numProbes = vk.frames[i].numProbesCopied = 0;
	}
}

/*
===============
VK_FlareVisible

x, y: the pixel of the flare as OpenGL counts, y up. eyeZ: how far in front of the eye it is.
===============
*/
qboolean VK_FlareVisible( const float *point, int x, int y, float eyeZ ) {
	vkFrame_t *frame = vk.frame;
	vkFlare_t *flare = NULL;

	for ( int i = 0; i < vk_numFlares; i++ ) {
		if ( vk_flares[i].key == point ) {
			flare = &vk_flares[i];
			break;
		}
	}
	if ( !flare ) {
		if ( vk_numFlares < VK_MAX_FLARES ) {
			flare = &vk_flares[vk_numFlares++];
		} else {
			flare = &vk_flares[0];
			for ( int i = 1; i < VK_MAX_FLARES; i++ ) {
				if ( vk_flares[i].asked < flare->asked ) {
					flare = &vk_flares[i];
				}
			}
		}
		flare->key = point;
		flare->visible = qfalse;
		flare->probed = 0;
	}
	flare->asked = vk_flareFrame;

	// the glow pass draws over the same depth
	if ( frame && !g_bRenderGlowingObjects && flare->probed != vk_flareFrame && frame->numProbes < VK_MAX_FLARES ) {
		vkProbe_t *probe = &frame->probe[frame->numProbes++];

		probe->flare = (int)( flare - vk_flares );
		probe->x = Com_Clampi( 0, vk.width - 1, x );
		probe->y = Com_Clampi( 0, vk.height - 1, vk.height - 1 - y );
		probe->eyeZ = eyeZ;
		probe->projection[0] = backEnd.viewParms.projectionMatrix[10];
		probe->projection[1] = backEnd.viewParms.projectionMatrix[11];
		probe->projection[2] = backEnd.viewParms.projectionMatrix[14];
		flare->probed = vk_flareFrame;
	}
	return flare->visible;
}

/*
===============
VK_CopyProbes

The end of a view: the pixels of the depth its flares asked for go into the buffer of the frame
===============
*/
void VK_CopyProbes( void ) {
	vkFrame_t *frame = vk.frame;
	int first;

	if ( !frame || frame->numProbesCopied == frame->numProbes ) {
		return;
	}
	first = frame->numProbesCopied;
	frame->numProbesCopied = frame->numProbes;

	VK_EndTarget();
	VK_CopyProbeDepths( first, frame->numProbes - first );
	VK_BeginTarget( &vk.color, &vk.depth, 2, qtrue );
	vk.stateValid = qfalse;
}

/*
===============
VK_ReadProbes

The commands this frame recorded last have run. What they copied is the answer for the flares, with the test
RB_TestZFlare() of rd-vanilla makes.
===============
*/
void VK_ReadProbes( vkFrame_t *frame ) {
	for ( int i = 0; i < frame->numProbesCopied; i++ ) {
		const vkProbe_t *probe = &frame->probe[i];
		const float depth = VK_ProbeDepth( frame, i );
		const float screenZ = probe->projection[2] / ( ( 2.0f * depth - 1.0f ) * probe->projection[1] - probe->projection[0] );

		vk_flares[probe->flare].visible = (qboolean)( probe->eyeZ + screenZ < 24.0f );
	}
	frame->numProbes = frame->numProbesCopied = 0;
	vk_flareFrame++;
}
