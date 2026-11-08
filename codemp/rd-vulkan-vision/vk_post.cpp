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

void myGlMultMatrix( const float *a, const float *b, float *out );

cvar_t	*v_wetMirror;
cvar_t	*v_smaa;
cvar_t	*v_tonemap;
cvar_t	*v_exposure;

void VK_InitPost( void ) {
	const int width = Com_Clampi( 16, vk.width, r_DynamicGlowWidth->integer );
	const int height = Com_Clampi( 16, vk.height, r_DynamicGlowHeight->integer );

	v_wetMirror = ri.Cvar_Get( "v_wetMirror", "1", CVAR_ARCHIVE_ND | CVAR_LATCH,
		"Puddles of the water gun mirror the room: the level is drawn once more, mirrored at their floor. 0: they mirror only what is on the screen" );
	v_tonemap = ri.Cvar_Get( "v_tonemap", "1", CVAR_ARCHIVE_ND,
		"How the level is put onto the screen at the end of its view (with v_hdr light brighter than white is there). 1: the original look, bright light glows out softly instead of clipping. 2: filmic (AgX) as modern games. 0: cut off at white as before" );
	v_exposure = ri.Cvar_Get( "v_exposure", "1", CVAR_ARCHIVE_ND, "How bright the level is before it is tone mapped (v_tonemap), 1 as it is" );
	v_smaa = ri.Cvar_Get( "v_smaa", "0", CVAR_ARCHIVE_ND,
		"Smooths the edges of the level after it is drawn (SMAA 1x), far cheaper than r_ext_multisample and sharper than a blur; with r_ext_multisample 2 it also catches thin lines. Vulkan only. 0: off" );
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

		VK_ClearColor( 0, 0, glConfig.vidWidth, glConfig.vidHeight, black );
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
module (the menu comes and goes), v_menuBlur is how soft the player wants it: 0 not at all, 1 as it is meant,
up to 4.

The frame goes into a buffer of half its size first: the four samples of that pass are the four pixels behind one
of the buffer, so nothing flickers. Then passes in that size widen it, each half a pixel further out than the
one before, three of them at v_menuBlur 1. The glow has buffers too (vk.blur), but they are 320 x 240 by
default: the scene is unrecognisable after one pass through them, which is what the first version of this did.

A last pass gives every pixel of the soft picture how much of it shows, from the depth of the scene at that
pixel (shaders/focus.frag), and the picture is laid over the frame by that. A device that can not read its
depth gets the whole frame soft: it is cleared and the soft picture is added to it, the way r_DynamicGlow 2
shows the glow alone.
===============
*/
void VK_BlurFrame( float amount, float focus ) {
	const float	strength = Com_Clamp( 0.0f, 4.0f, v_menuBlur->value );
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
		VK_ClearColor( 0, 0, glConfig.vidWidth, glConfig.vidHeight, black );
		VK_AddTarget( source, VKA_ADD );
	}

	// the shaders that draw geometry have other sets and other state
	VK_BindFrameSets();
	vk.stateValid = qfalse;
	VK_SetViewport( vk.viewport[0], vk.viewport[1], vk.viewport[2], vk.viewport[3], 0.0f, 1.0f );
}

/*
===============
VK_Bloom

tay_bloom, what the Vulkan renderer of TaystJK has (r_bloom of Quake3e): what is brighter than tay_bloomThreshold
shines over its surroundings. The first pass takes only the part above the threshold into vk.soft, the buffers of
the menu blur at half the size of the frame, the passes after it widen that, the last one weighs it by
tay_bloomIntensity, and it is added to the frame. For the view of the level only, before the menu blur and the 2D.

The frame holds the colors before the gamma pass, which multiplies them by the overbright factor: the threshold
is of white on the screen, so it is divided by that.
===============
*/
#define BLOOM_PASSES	5

void VK_Bloom( void ) {
	const float	threshold = Com_Clamp( 0.0f, 0.95f, tay_bloomThreshold->value ) / (float)( 1 << tr.overbrightBits );
	const float	intensity = Com_Clamp( 0.0f, 2.0f, tay_bloomIntensity->value );
	vkTarget_t	*source = &vk.color;

	if ( !vk.frame || !tay_bloom->integer || intensity <= 0.0f ) {
		return;
	}
	VK_EndTarget();

	for ( int i = 0; i <= BLOOM_PASSES; i++ ) {
		vkTarget_t *target = &vk.soft[i & 1];
		// in pixels of the source: the first pass takes the four pixels behind one of the half size, the others
		// reach a pixel and a half further each, about 36 pixels of the frame in all
		const float offset = i ? 1.5f * i - 1.0f : 0.5f;
		const float parm[4] = { offset / source->width, offset / source->height, i == BLOOM_PASSES ? 0.25f * intensity : 0.25f,
			i ? 0.0f : Q_max( threshold, 0.001f ) };

		VK_BlurPass( source, target, parm );

		source = target;
	}

	VK_BeginTarget( &vk.color, &vk.depth, 2, qtrue );
	VK_AddTarget( source, VKA_ADD );

	// the shaders that draw geometry have other sets and other state
	VK_BindFrameSets();
	vk.stateValid = qfalse;
	VK_SetViewport( vk.viewport[0], vk.viewport[1], vk.viewport[2], vk.viewport[3], 0.0f, 1.0f );
}

/*
===============
VK_Smaa

v_smaa, for the view of the level only, before tay_bloom and the 2D: the edges of the level are smoothed, the
text of the HUD drawn later stays sharp. The edges are found by brightness, SMAA's threshold of 0.1 on the
screen, so in the frame, which holds the colors before the gamma pass, it is divided by the overbright factor as
the threshold of tay_bloom is.
===============
*/
void VK_Smaa( void ) {
#ifndef RD_VISION_GLES
	if ( !vk.frame || !v_smaa->integer ) {
		return;
	}
	VK_EndTarget();
	VK_SmaaPass( 0.1f / (float)( 1 << tr.overbrightBits ) );

	// the shaders that draw geometry have other sets and other state
	VK_BindFrameSets();
	vk.stateValid = qfalse;
	VK_SetViewport( vk.viewport[0], vk.viewport[1], vk.viewport[2], vk.viewport[3], 0.0f, 1.0f );
#endif
}

/*
===============
VK_Tonemap

v_tonemap, for the view of the level only, after tay_bloom (which takes the light above white with it) and before
SMAA (which finds the edges in the colours of the screen) and the 2D. Without v_hdr nothing is above white: the
soft shoulder would only take the brightest whites down a little, so it runs only with v_hdr; the filmic curve
changes the colours too and runs on either.
===============
*/
void VK_Tonemap( void ) {
#ifndef RD_VISION_GLES
	const int curve = Com_Clampi( 0, 2, v_tonemap->integer );
	float parm[3];

	if ( !vk.frame || !curve || ( curve == 1 && vk.color.format == VK_FORMAT_R8G8B8A8_UNORM ) ) {
		return;
	}
	parm[0] = Com_Clamp( 0.05f, 16.0f, v_exposure->value );
	parm[1] = (float)curve;
	parm[2] = (float)( 1 << tr.overbrightBits );
	VK_EndTarget();
	VK_TonemapPass( parm );

	// the shaders that draw geometry have other sets and other state
	VK_BindFrameSets();
	vk.stateValid = qfalse;
	VK_SetViewport( vk.viewport[0], vk.viewport[1], vk.viewport[2], vk.viewport[3], 0.0f, 1.0f );
#endif
}

/*
===============
VK_HasScope

Whether RE_SetScopeEffect() is drawn: it reads the depth of the scene
===============
*/
qboolean VK_HasScope( void ) {
	return vk.depthReadable;
}

/*
===============
VK_ScopeBoxes

The boxes of the tracker into vk.glow, the buffer the lens reads its mask from: cleared, and every box cleared to
white. Pixels of the window as OpenGL counts them, VK_ClearColor() takes them to the size of the scene.
===============
*/
void VK_ScopeBoxes( const scopeEffect_t *fx ) {
	const float	white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	const int	vx = backEnd.viewParms.viewportX, vy = backEnd.viewParms.viewportY;
	const int	vw = backEnd.viewParms.viewportWidth, vh = backEnd.viewParms.viewportHeight;

	if ( !vk.frame ) {
		return;
	}
	VK_BeginGlow();
	for ( int i = 0; i < fx->numBoxes && i < SCOPEFX_MAX_BOXES; i++ ) {
		const float	*box = fx->boxes[i];
		const int	left = vx + (int)( Com_Clamp( 0.0f, 1.0f, box[0] ) * vw );
		const int	right = vx + (int)( Com_Clamp( 0.0f, 1.0f, box[2] ) * vw + 0.5f );
		const int	top = vy + (int)( ( 1.0f - Com_Clamp( 0.0f, 1.0f, box[1] ) ) * vh + 0.5f );
		const int	bottom = vy + (int)( ( 1.0f - Com_Clamp( 0.0f, 1.0f, box[3] ) ) * vh );

		if ( right > left && top > bottom ) {
			VK_ClearColor( left, bottom, right - left, top - bottom, white );
		}
	}
}

/*
===============
VK_Scope

The view of the level through the scope of a weapon, what the cgame asked for with RE_SetScopeEffect(). After the
bloom, before the menu blur and the 2D. boxes: vk.glow holds the boxes of the tracker (VK_ScopeBoxes).

The frame is blurred into vk.soft as for the menu blur, but softer and in fewer passes. shaders/scopeprep.frag
reads the depth into the other soft buffer: how far out of focus each pixel is around what the crosshair is on,
and the edges of the level. The frame is copied into vk.screen, the buffer of refraction (that is
drawn by now), and shaders/scope.frag lays the lens over the frame from all of that.
===============
*/
#define SCOPE_BLUR_PASSES	4

void VK_Scope( const scopeEffect_t *fx, qboolean boxes ) {
	const float	*projection = backEnd.viewParms.projectionMatrix;
	vkTarget_t	*source = &vk.color;
	vkTarget_t	*prep;
	float		center[2];
	float		parm[16];

	if ( !vk.frame || !vk.depthReadable ) {
		return;
	}
	VK_EndTarget();

	for ( int i = 0; i < SCOPE_BLUR_PASSES; i++ ) {
		vkTarget_t *target = &vk.soft[i & 1];
		const float offset = i ? 1.5f * i : 0.5f;
		const float blur[4] = { offset / source->width, offset / source->height, 0.25f, 0.0f };

		VK_BlurPass( source, target, blur );

		source = target;
	}
	prep = &vk.soft[source == &vk.soft[0] ? 1 : 0];

	// The middle of the lens in texture coordinates of the frame, whose top row comes first: the view is a
	// rectangle of it that OpenGL counts from the lower edge.
	center[0] = ( backEnd.viewParms.viewportX + fx->center[0] * backEnd.viewParms.viewportWidth ) / glConfig.vidWidth;
	center[1] = 1.0f - ( backEnd.viewParms.viewportY + ( 1.0f - fx->center[1] ) * backEnd.viewParms.viewportHeight ) / glConfig.vidHeight;
	{
		// the depth to the distance from the eye, as VK_BlurFrame() has it
		const float depth[4] = { projection[14] * 0.5f, ( projection[10] - 1.0f ) * 0.5f, center[0], center[1] };

		VK_ScopePrepPass( &vk.glow, prep, depth );
	}

	VK_CopyTarget( &vk.color, &vk.screen );

	parm[0] = center[0];
	parm[1] = center[1];
	parm[2] = fx->radius * backEnd.viewParms.viewportHeight / glConfig.vidHeight;
	parm[3] = (float)glConfig.vidWidth / (float)glConfig.vidHeight;
	parm[4] = Com_Clamp( 0.0f, 1.0f, fx->zoom );
	parm[5] = ( fx->flags & SCOPEFX_TRACKER ) ? Com_Clamp( 0.0f, 1.0f, fx->motion ) : 0.0f;
	parm[6] = fx->time;
	parm[7] = ( fx->flags & SCOPEFX_LENS ) ? ( ( fx->flags & SCOPEFX_BINOCULAR ) ? 2.0f : 1.0f ) : 0.0f;
	parm[8] = (float)vk.color.width / (float)vk.screen.width;
	parm[9] = (float)vk.color.height / (float)vk.screen.height;
	parm[10] = 1.0f / (float)( 1 << tr.overbrightBits );
	parm[11] = boxes ? 1.0f : 0.0f;
	parm[12] = (float)glConfig.vidHeight;
	parm[13] = fx->charge;
	parm[14] = Com_Clamp( 0.0f, 1.0f, fx->ammo );
	parm[15] = ( fx->flags & SCOPEFX_RETICLE ) ? 1.0f : 0.0f;
	VK_ScopePass( source, prep, &vk.glow, parm );

	// the shaders that draw geometry have other sets and other state
	VK_BindFrameSets();
	vk.stateValid = qfalse;
	VK_SetViewport( vk.viewport[0], vk.viewport[1], vk.viewport[2], vk.viewport[3], 0.0f, 1.0f );
}

/*
===============
VK_ScreenFx

The view of the level bent, flashed and coloured as the cgame asked with RE_SetScreenEffect(). After the bloom,
before the scope, which then sees it too. shaders/screenfxprep.frag puts the distance of every pixel from the eye
into a soft buffer (half the size, in two channels of eight bits), the frame is copied into vk.screen and
shaders/screenfx.frag draws it again over the frame from there, each pixel taken from where the sources bend it to.
A source is left out where the level is nearer than it, so nothing is seen through a wall. While Force Sight is on,
shaders/screenfxfog.frag works out the mist around the eye into the other soft buffer first, screenfx.frag lays it
in.

The numbers of screenfx.frag, VK_SCREENFX_VEC4S vec4:
[0] x: seconds, y: width / height of the frame, z: 1 / overbright factor, w: number of sources
[1] xy: texture coordinates of the frame to the ones of its copy, z: the height of the view as a share of the one
    of the frame, w: colour split
[2] xy: the depth to the distance from the eye, x / ( depth + y )
[3] tint: rgb, a
[4] x: desaturate, y: radial blur, w: vignette
[5] vignette rgb, w: width of its arc, 0 all around
[6] xy: direction of the arc (x to the right, y up), z: water, w: drops
[7] rgb: colour of the water
[8] x: Force Sight, y: rage, z: protect, w: absorb
[9] xyz: the eye in the level, w: tan of half the fov across
[10] xyz: forward, w: tan of half the fov up
[11] xyz: to the right   [12] xyz: up
[13] the view in texture coordinates of the frame: left, top, width, height
[14] x: protect hit, y: absorb pulse, z: rage pulse
[15] xw: how far the picture moved on the frame as the eye turned, in texture coordinates, y: seconds since the
     frame before, z: 1 when the smoke buffer of the frame before holds its smoke (set here)
[16 + 3 * i] the sources: xy, zw: a and b in texture coordinates of the frame
[17 + 3 * i] x: radius, y: width, as shares of the height of the frame, z: strength, w: distance from the eye
[18 + 3 * i] x: kind (SCREENFX_*), y: seed
===============
*/
#define SCREENFX_SOURCE_VEC4	16

void VK_ScreenFx( const screenEffect_t *fx ) {
	const float	*projection = backEnd.viewParms.projectionMatrix;
	const float	vx = backEnd.viewParms.viewportX, vy = backEnd.viewParms.viewportY;
	const float	vw = backEnd.viewParms.viewportWidth, vh = backEnd.viewParms.viewportHeight;
	const float	fw = glConfig.vidWidth, fh = glConfig.vidHeight;
	float		parm[VK_SCREENFX_VEC4S][4];
	vkTarget_t	*prep = &vk.soft[0];
	int			count = 0;

	if ( !vk.frame || !vk.depthReadable ) {
		return;
	}
	VK_EndTarget();
	{
		const float depth[4] = { projection[14] * 0.5f, ( projection[10] - 1.0f ) * 0.5f, 0.0f, 0.0f };

		VK_ScreenFxPrepPass( &vk.glow, prep, depth );
	}
	VK_CopyTarget( &vk.color, &vk.screen );

	memset( parm, 0, sizeof( parm ) );
	for ( int i = 0; i < fx->numSources && SCREENFX_SOURCE_VEC4 + 3 * count + 2 < VK_SCREENFX_VEC4S; i++ ) {
		const screenFxSource_t	*src = &fx->sources[i];
		float					*p = parm[SCREENFX_SOURCE_VEC4 + 3 * count];

		if ( src->strength <= 0.0f ) {
			continue;
		}
		// fractions of the view to texture coordinates of the frame, whose top row comes first: the view is a
		// rectangle of it that OpenGL counts from the lower edge
		p[0] = ( vx + src->a[0] * vw ) / fw;
		p[1] = 1.0f - ( vy + ( 1.0f - src->a[1] ) * vh ) / fh;
		p[2] = ( vx + src->b[0] * vw ) / fw;
		p[3] = 1.0f - ( vy + ( 1.0f - src->b[1] ) * vh ) / fh;
		p[4] = src->radius * vh / fh;
		p[5] = src->width * vh / fh;
		p[6] = Com_Clamp( 0.0f, 1.0f, src->strength );
		p[7] = src->distance;
		p[8] = (float)src->kind;
		p[9] = src->seed;
		count++;
	}
	parm[0][0] = fx->time;
	parm[0][1] = fw / fh;
	parm[0][2] = 1.0f / (float)( 1 << tr.overbrightBits );
	parm[0][3] = (float)count;
	parm[1][0] = (float)vk.color.width / (float)vk.screen.width;
	parm[1][1] = (float)vk.color.height / (float)vk.screen.height;
	parm[1][2] = vh / fh;
	parm[1][3] = Com_Clamp( 0.0f, 1.0f, fx->split );
	parm[2][0] = projection[14] * 0.5f;
	parm[2][1] = ( projection[10] - 1.0f ) * 0.5f;
	VectorCopy4( fx->tint, parm[3] );
	parm[4][0] = Com_Clamp( 0.0f, 1.0f, fx->desaturate );
	parm[4][1] = Com_Clamp( 0.0f, 1.0f, fx->radialBlur );
	parm[4][3] = Com_Clamp( 0.0f, 1.0f, fx->vignette[3] );
	VectorCopy( fx->vignette, parm[5] );
	parm[5][3] = Com_Clamp( 0.0f, 1.0f, fx->vignetteArc[2] );
	parm[6][0] = fx->vignetteArc[0];
	parm[6][1] = fx->vignetteArc[1];
	parm[6][2] = Com_Clamp( 0.0f, 1.0f, fx->water );
	parm[6][3] = Com_Clamp( 0.0f, 1.0f, fx->drops );
	VectorCopy( fx->waterColor, parm[7] );
	parm[8][0] = Com_Clamp( 0.0f, 1.0f, fx->sight );
	parm[8][1] = Com_Clamp( 0.0f, 1.0f, fx->rage );
	parm[8][2] = Com_Clamp( 0.0f, 1.0f, fx->protect );
	parm[8][3] = Com_Clamp( 0.0f, 1.0f, fx->absorb );
	parm[14][0] = Com_Clamp( 0.0f, 1.0f, fx->protectHit );
	parm[14][1] = Com_Clamp( 0.0f, 1.0f, fx->absorbPulse );
	parm[14][2] = Com_Clamp( 0.0f, 1.0f, fx->ragePulse );
	parm[15][0] = fx->viewShift[0] * vw / fw;
	parm[15][3] = fx->viewShift[1] * vh / fh;
	VectorCopy( backEnd.viewParms.ori.origin, parm[9] );
	parm[9][3] = tanf( DEG2RAD( backEnd.viewParms.fovX * 0.5f ) );
	VectorCopy( backEnd.viewParms.ori.axis[0], parm[10] );
	parm[10][3] = tanf( DEG2RAD( backEnd.viewParms.fovY * 0.5f ) );
	VectorScale( backEnd.viewParms.ori.axis[1], -1.0f, parm[11] );
	VectorCopy( backEnd.viewParms.ori.axis[2], parm[12] );
	parm[13][0] = vx / fw;
	parm[13][1] = 1.0f - ( vy + vh ) / fh;
	parm[13][2] = vw / fw;
	parm[13][3] = vh / fh;
	parm[15][1] = fx->frameTime;
	// the mist of Force Sight, into the other soft buffer, which nothing else uses in between, and the smoke of its
	// ghosts, on from what the frame before left in the other of its two buffers when there was one
	if ( fx->sight > 0.0f ) {
		vkTarget_t *last = &vk.smoke[vk.smokeIndex];

		parm[15][2] = ( vk.smokeFrame && vk.smokeFrame == tr.frameCount - 1 ) ? 1.0f : 0.0f;
		VK_ScreenFxFogPass( &vk.soft[1], parm[0] );
		vk.smokeIndex ^= 1;
		VK_ScreenFxSmokePass( &vk.smoke[vk.smokeIndex], last, prep, parm[0] );
		vk.smokeFrame = tr.frameCount;
	}
	VK_ScreenFxPass( prep, &vk.soft[1], &vk.smoke[vk.smokeIndex], parm[0] );

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
===============
VK_DrawLiquids

The pieces of water of RE_AddLiquid over the view of the level that was just drawn, before its glow
(shaders/liquid.vert and liquid.frag). The frame is copied into vk.screen first: the water shows what lies behind
it and mirrors what lies in front of it from there. Then the pieces are drawn against the depth of the scene and
write the depth of where the rays hit them, in batches as large as the sprites of sprite.vert may be (the block of
bones of OpenGL ES). Each piece takes the light of the level at its two ends, blended along it, so a jet has no
steps where one piece meets the next.
===============
*/
#define LIQUID_BEND		8.0f		// how far behind the water what it shows bent is taken to be, units
#define LIQUID_ABSORB	0.035f		// how much it darkens per unit of the way through it, red first
#define LIQUID_WAVES	1.0f		// how strongly its surface ripples

// the light of the level at a point, the light of a lamp lit room where the level has no grid
static void VK_LiquidLight( const vec3_t point, vec3_t ambient, vec3_t directed, vec3_t dir ) {
	if ( !tr.world || !R_LightForPoint( (float *)point, ambient, directed, dir ) ) {
		VectorSet( ambient, 96.0f, 96.0f, 96.0f );
		VectorSet( directed, 160.0f, 160.0f, 160.0f );
		VectorSet( dir, 0.3f, 0.2f, 0.93f );
	}
}

void VK_DrawLiquids( void ) {
	static vec4_t		records[VK_MAX_BATCH_SPRITES * VK_SPRITE_ROWS];
	static glIndex_t	indexes[VK_MAX_BATCH_SPRITES * 6];
	const float			time = (float)fmod( backEnd.refdef.floatTime, 1000.0 );

	if ( !vk.frame || !r_numLiquids || !tr.screenImage || !vk.depthReadable ) {
		return;
	}
	VK_CaptureFrame();
	backEnd.currentEntity = &tr.worldEntity;
	backEnd.ori = backEnd.viewParms.world;
	VK_InvalidateTransform();

	for ( int first = 0; first < r_numLiquids; first += VK_MAX_BATCH_SPRITES ) {
		const int	count = Q_min( r_numLiquids - first, VK_MAX_BATCH_SPRITES );
		uint32_t	bones;
		vkDraw_t	draw;

		for ( int i = 0; i < count; i++ ) {
			const liquidSegment_t	*seg = &r_liquids[first + i];
			vec4_t					*row = records + i * VK_SPRITE_ROWS;
			vec3_t					ambient, directed, dir, ambientB, directedB, dirB;
			float					ratio[2];

			VK_LiquidLight( seg->a, ambient, directed, dir );
			VK_LiquidLight( seg->b, ambientB, directedB, dirB );
			// the light at b as a part of the one at a, the direction of the two together
			ratio[0] = ( ambientB[0] + ambientB[1] + ambientB[2] + 1.0f ) / ( ambient[0] + ambient[1] + ambient[2] + 1.0f );
			ratio[1] = ( directedB[0] + directedB[1] + directedB[2] + 1.0f ) / ( directed[0] + directed[1] + directed[2] + 1.0f );
			VectorAdd( dir, dirB, dir );
			VectorSet4( row[0], seg->a[0], seg->a[1], seg->a[2], seg->radius[0] );
			VectorSet4( row[1], seg->b[0], seg->b[1], seg->b[2], seg->radius[1] );
			VectorSet4( row[2], seg->flow[0], seg->flow[1], Com_Clamp( 0.0f, 1.0f, seg->breakup ), 0.0f );
			VectorSet4( row[3], dir[0], dir[1], dir[2], time );
			VectorSet4( row[4], ambient[0] / 255.0f, ambient[1] / 255.0f, ambient[2] / 255.0f, Com_Clamp( 0.25f, 4.0f, ratio[0] ) );
			VectorSet4( row[5], directed[0] / 255.0f, directed[1] / 255.0f, directed[2] / 255.0f, Com_Clamp( 0.25f, 4.0f, ratio[1] ) );
			VK_QuadIndexes( indexes + i * 6, (glIndex_t)( i * 4 ) );
		}
		if ( !VK_QuadView( records, count, &bones ) || !VK_UploadIndexes( NULL, NULL, 0, indexes, count * 6 ) ) {
			break;
		}
		memset( &draw, 0, sizeof( draw ) );
		draw.transform = VK_CurrentTransform();
		draw.tex0 = draw.tex1 = (uint32_t)vk.screenSlot;
		draw.bones = bones;
		draw.texMat0[0] = 1.0f / (float)vk.screen.width;
		draw.texMat0[1] = 1.0f / (float)vk.screen.height;
		draw.texMat0[2] = (float)vk.color.width / (float)vk.screen.width;
		draw.texMat0[3] = (float)vk.color.height / (float)vk.screen.height;
		draw.texMat0[4] = LIQUID_BEND;
		draw.texMat0[5] = LIQUID_ABSORB;
		draw.texMat0[6] = LIQUID_WAVES;
		// the distance in front of the eye to the depth, as VK_BlurFrame() has it the other way round
		draw.texMat1[0] = backEnd.viewParms.projectionMatrix[14] * 0.5f;
		draw.texMat1[1] = ( backEnd.viewParms.projectionMatrix[10] - 1.0f ) * 0.5f;
		// and a slope from the line of sight to a part of the picture
		draw.texMat1[2] = backEnd.viewParms.projectionMatrix[0] * 0.5f;
		draw.texMat1[3] = backEnd.viewParms.projectionMatrix[5] * 0.5f;
		VK_DrawTess( &draw, VKP_LIQUID, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHMASK_TRUE,
			VKC_NONE, qfalse );
	}
}

/*
===============
VK_HasWet

Whether RE_AddWet() is drawn: it reads the depth of the scene while the frame is a target, which the OpenGL ES
device does not do
===============
*/
qboolean VK_HasWet( void ) {
#ifdef RD_VISION_GLES
	return qfalse;
#else
	return vk.depthReadable;
#endif
}

/*
===============
VK_DrawWet

The sprayed pictures of RE_AddPaint and the wet places of RE_AddWet over the view of the level as far as it is
drawn: in the draw list before what is transparent (RB_DrawWet), before its water. The paint first, so a wet place
darkens it too (shaders/paint.vert and paint.frag, then wet.vert and wet.frag). Before each the frame is copied into
vk.screen: it is drawn from there, paint over the relief of the wall, wet ground darker and shining. Standing water
at the floor of the puddle view of this frame mirrors what that view shows (vk.reflect), other water what the copy
shows. The numbers of the view and of every picture and place go into the globals of the frame; each gets the light
of the level at its middle.
===============
*/
#ifndef RD_VISION_GLES
void VK_ViewRows( float ( *rows )[4], int *viewport ) {
	const float	*projection = backEnd.viewParms.projectionMatrix;
	const int	x = backEnd.viewParms.viewportX, y = backEnd.viewParms.viewportY;
	const int	width = backEnd.viewParms.viewportWidth, height = backEnd.viewParms.viewportHeight;

	myGlMultMatrix( backEnd.viewParms.world.modelMatrix, projection, rows[0] );
	VectorCopy( backEnd.viewParms.ori.origin, rows[4] );
	rows[4][3] = tanf( DEG2RAD( backEnd.viewParms.fovX * 0.5f ) );
	VectorCopy( backEnd.viewParms.ori.axis[0], rows[5] );
	rows[5][3] = tanf( DEG2RAD( backEnd.viewParms.fovY * 0.5f ) );
	// the distance in front of the eye to the depth, as VK_BlurFrame() has it
	VectorCopy( backEnd.viewParms.ori.axis[1], rows[6] );
	rows[6][3] = projection[14] * 0.5f;
	VectorCopy( backEnd.viewParms.ori.axis[2], rows[7] );
	rows[7][3] = ( projection[10] - 1.0f ) * 0.5f;
	// the view in pixels of the target from the upper left corner, as VK_ViewportAndScissor() puts it there
	VectorSet4( rows[8], (float)x, (float)( vk.height - ( y + height ) ), (float)width, (float)height );
	viewport[0] = x;
	viewport[1] = y;
	viewport[2] = width;
	viewport[3] = height;
}
#endif

void VK_DrawWet( void ) {
#ifndef RD_VISION_GLES
	static float	wet[VK_WET_VEC4S][4];
	static float	paint[VK_PAINT_VEC4S][4];
	const int		count = Q_min( r_numWet, WET_MAX_SPOTS );
	int				numPaint = 0;
	int				viewport[4];

	if ( !vk.frame || ( count <= 0 && r_numPaint <= 0 ) || !tr.screenImage || !VK_HasWet() ) {
		return;
	}
	memset( wet, 0, sizeof( wet ) );
	VK_ViewRows( wet, viewport );
	VectorSet4( wet[9], 1.0f / (float)vk.screen.width, 1.0f / (float)vk.screen.height, (float)fmod( backEnd.refdef.floatTime, 1000.0 ),
		(float)count );
	VectorSet4( wet[10], vk.reflectHeight, vk.reflectFrame == backEnd.viewParms.frameCount ? 1.0f : 0.0f, 0.0f, 0.0f );

	// the pictures whose sheet is on the device
	memcpy( paint, wet, sizeof( float ) * 4 * VK_WET_HEADER );
	for ( int i = 0; i < Q_min( r_numPaint, PAINT_MAX_DECALS ); i++ ) {
		const paintDecal_t	*decal = &r_paint[i];
		const image_t		*image = ( decal->slot >= 0 && decal->slot < PAINT_SLOTS ) ? tr.paintImage[decal->slot] : NULL;
		float				( *row )[4] = paint + VK_WET_HEADER + numPaint * VK_PAINT_ROWS;
		vec3_t				normal, up, ambient, directed, dir;

		if ( !image || image->texnum >= VK_MAX_IMAGES || vk.images[image->texnum].owner != image || !vk.images[image->texnum].uploaded ) {
			continue;
		}
		VectorCopy( decal->normal, normal );
		if ( VectorNormalize( normal ) < 0.5f ) {
			continue;
		}
		// up along the surface, square to the normal
		VectorMA( decal->up, -DotProduct( decal->up, normal ), normal, up );
		if ( VectorNormalize( up ) < 0.1f ) {
			PerpendicularVector( up, normal );
		}
		VK_LiquidLight( decal->origin, ambient, directed, dir );
		VectorNormalize( dir );
		VectorSet4( row[0], decal->origin[0], decal->origin[1], decal->origin[2], Com_Clamp( 1.0f, 256.0f, decal->radius ) );
		VectorSet4( row[1], normal[0], normal[1], normal[2], Com_Clamp( 0.0f, 1.0f, decal->reveal ) );
		VectorSet4( row[2], up[0], up[1], up[2], Com_Clamp( 0.0f, 1.0f, decal->fresh ) );
		VectorSet4( row[3], ambient[0] / 255.0f, ambient[1] / 255.0f, ambient[2] / 255.0f, (float)image->texnum );
		VectorSet4( row[4], directed[0] / 255.0f, directed[1] / 255.0f, directed[2] / 255.0f, decal->seed );
		VectorSet4( row[5], dir[0], dir[1], dir[2], (float)image->width * Com_Clamp( 0.0f, 1.0f, decal->frame[2] ) );
		VectorSet4( row[6], decal->frame[0], decal->frame[1], decal->frame[2], decal->frame[3] );
		numPaint++;
	}
	paint[9][3] = (float)numPaint;
	if ( count <= 0 && numPaint <= 0 ) {
		return;
	}

	for ( int i = 0; i < count; i++ ) {
		const wetSpot_t	*spot = &r_wet[i];
		float			( *row )[4] = wet + VK_WET_HEADER + i * VK_WET_ROWS;
		vec3_t			normal, ambient, directed, dir;

		VectorCopy( spot->normal, normal );
		if ( VectorNormalize( normal ) < 0.5f ) {
			VectorSet( normal, 0.0f, 0.0f, 1.0f );
		}
		VK_LiquidLight( spot->origin, ambient, directed, dir );
		VectorNormalize( dir );
		VectorSet4( row[0], spot->origin[0], spot->origin[1], spot->origin[2], Com_Clamp( 1.0f, 512.0f, spot->radius ) );
		VectorSet4( row[1], normal[0], normal[1], normal[2], Com_Clamp( 0.0f, 1.0f, spot->wet ) );
		VectorSet4( row[2], spot->hit[0], spot->hit[1], spot->hit[2], Com_Clamp( 0.0f, 1.0f, spot->puddle ) );
		VectorSet4( row[3], ambient[0] / 255.0f, ambient[1] / 255.0f, ambient[2] / 255.0f, Com_Clamp( 0.0f, 1.0f, spot->flow ) );
		VectorSet4( row[4], directed[0] / 255.0f, directed[1] / 255.0f, directed[2] / 255.0f, spot->seed );
		VectorSet4( row[5], dir[0], dir[1], dir[2], (float)spot->kind );
	}

	VK_EndTarget();
	if ( numPaint > 0 ) {
		VK_CopyTarget( &vk.color, &vk.screen );
		VK_PaintPass( paint[0], numPaint, viewport );
	}
	if ( count > 0 ) {
		VK_CopyTarget( &vk.color, &vk.screen );
		VK_WetPass( wet[0], count, viewport );
	}
	VK_BeginTarget( &vk.color, &vk.depth, 2, qtrue );

	// the shaders that draw geometry have other sets and other state
	VK_BindFrameSets();
	vk.stateValid = qfalse;
	VK_SetViewport( vk.viewport[0], vk.viewport[1], vk.viewport[2], vk.viewport[3], 0.0f, 1.0f );
	VK_InvalidateTransform();
#endif
}

/*
===============
VK_PuddleMirror

Whether the front end draws the view mirrored at the floor of the puddles (R_PuddleView of tr_main.cpp): its
targets are there. The OpenGL ES device has none of it (VK_HasWet).
===============
*/
qboolean VK_PuddleMirror( void ) {
#ifdef RD_VISION_GLES
	return qfalse;
#else
	return (qboolean)( VK_HasWet() && vk.reflect.image );
#endif
}

#ifndef RD_VISION_GLES
// the targets of the puddle view take the places of color, depth and their samples, or give them back
static void VK_SwapPuddleTargets( void ) {
	vkTarget_t target;

#define VK_SWAP( a, b )	( target = a, a = b, b = target )
	VK_SWAP( vk.color, vk.reflect );
	VK_SWAP( vk.colorMS, vk.reflectMS );
	VK_SWAP( vk.depth, vk.reflectDepth );
	VK_SWAP( vk.depthMS, vk.reflectDepthMS );
#undef VK_SWAP
}
#endif

/*
===============
VK_BeginPuddle

The view mirrored at the floor of the puddles is drawn into targets of its own, begun empty: the frame keeps what
it has (the sky of a portal, a mirror of the level). Its picture has the shape of the main view, so the wet places
read it at their own pixel. qfalse: it is not drawn.
===============
*/
qboolean VK_BeginPuddle( void ) {
#ifdef RD_VISION_GLES
	return qfalse;
#else
	if ( !vk.frame || !VK_PuddleMirror() ) {
		return qfalse;
	}
	VK_EndTarget();
	VK_SwapPuddleTargets();
	VK_BeginTarget( &vk.color, &vk.depth, 1, qfalse );
	VK_BindFrameSets();
	vk.stateValid = qfalse;
	vk.reflectFrame = backEnd.viewParms.frameCount;
	vk.reflectHeight = backEnd.viewParms.portalPlane.dist;
	return qtrue;
#endif
}

void VK_EndPuddle( void ) {
#ifndef RD_VISION_GLES
	VK_EndTarget();
	VK_SwapPuddleTargets();
	VK_TargetLayout( vk.frame->cmd, &vk.reflect, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_BeginTarget( &vk.color, &vk.depth, 2, qtrue );
	VK_BindFrameSets();
	vk.stateValid = qfalse;
	VK_InvalidateTransform();
#endif
}

/*
===============================================================================

REFLECTION PROBES

The faces of the reflection probes of the level (R_ProbeViews of tr_main.cpp): each is drawn as the puddle view into
targets of its own of PROBE_SIZE, traded with the ones of the frame, and copied to its place in probeAtlas: a probe
a row, its six faces side by side. When the last face is in, the smaller levels of the atlas are made by halving
(a rough surface reads one of them, generic.frag ProbeSample) and tr.probeImage shows the atlas from then on. The
OpenGL ES device has none of it.

===============================================================================
*/

#define PROBE_LEVELS	6			// 128 down to 4 pixels a face

qboolean VK_ProbesSupported( void ) {
#ifdef RD_VISION_GLES
	return qfalse;
#else
	return (qboolean)( v_pbr->integer && v_pbrProbes->integer && vk.probeColor.image && tr.probeImage );
#endif
}

// whether the faces that were drawn are gone (new targets), once
qboolean VK_ProbesLost( void ) {
#ifdef RD_VISION_GLES
	return qfalse;
#else
	const qboolean lost = vk.probesLost;

	vk.probesLost = qfalse;
	return lost;
#endif
}

#ifndef RD_VISION_GLES
// the targets of a face take the places of color, depth and their samples, or give them back
static void VK_SwapProbeTargets( void ) {
	vkTarget_t target;

#define VK_SWAP( a, b )	( target = a, a = b, b = target )
	VK_SWAP( vk.color, vk.probeColor );
	VK_SWAP( vk.colorMS, vk.probeColorMS );
	VK_SWAP( vk.depth, vk.probeDepth );
	VK_SWAP( vk.depthMS, vk.probeDepthMS );
#undef VK_SWAP
}

// levels of the atlas from one layout to another
static void VK_AtlasLayout( VkCommandBuffer cmd, int level, int count, VkImageLayout from, VkImageLayout to, VkAccessFlags fromAccess,
	VkAccessFlags toAccess ) {
	VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };

	barrier.srcAccessMask = fromAccess;
	barrier.dstAccessMask = toAccess;
	barrier.oldLayout = from;
	barrier.newLayout = to;
	barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = vk.probeAtlas;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = (uint32_t)level;
	barrier.subresourceRange.levelCount = (uint32_t)count;
	barrier.subresourceRange.layerCount = 1;
	qvkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &barrier );
}

// an atlas for numProbes probes, its levels ready to be copied to
static void VK_CreateProbeAtlas( int numProbes ) {
	VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	VkImageViewCreateInfo view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	VkMemoryRequirements req;

	VK_DestroyProbeAtlas();
	vk.probesLost = qfalse;		// (a new atlas while the faces are being drawn anyway)
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = vk.color.format;
	info.extent.width = PROBE_SIZE * 6;
	info.extent.height = (uint32_t)( PROBE_SIZE * numProbes );
	info.extent.depth = 1;
	info.mipLevels = PROBE_LEVELS;
	info.arrayLayers = 1;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK( qvkCreateImage( vk.device, &info, NULL, &vk.probeAtlas ) );
	qvkGetImageMemoryRequirements( vk.device, vk.probeAtlas, &req );
	VK_AllocMemory( &req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vk.probeAtlasAlloc );
	VK_CHECK( qvkBindImageMemory( vk.device, vk.probeAtlas, VK_MemoryOf( &vk.probeAtlasAlloc ), vk.probeAtlasAlloc.offset ) );
	view.image = vk.probeAtlas;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = info.format;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = PROBE_LEVELS;
	view.subresourceRange.layerCount = 1;
	VK_CHECK( qvkCreateImageView( vk.device, &view, NULL, &vk.probeAtlasView ) );
	vk.probeAtlasProbes = numProbes;
}
#endif

void VK_DestroyProbeAtlas( void ) {
#ifndef RD_VISION_GLES
	if ( vk.probeAtlas ) {
		// (the texture table may still name it: it is not read until the faces are drawn again)
		qvkDestroyImageView( vk.device, vk.probeAtlasView, NULL );
		qvkDestroyImage( vk.device, vk.probeAtlas, NULL );
		VK_FreeMemory( &vk.probeAtlasAlloc );
		vk.probeAtlas = VK_NULL_HANDLE;
		vk.probeAtlasView = VK_NULL_HANDLE;
		vk.probesLost = qtrue;
	}
	vk.probeAtlasProbes = 0;
#endif
}

/*
===============
VK_BeginProbe

A face of a reflection probe is drawn into the targets of the faces, begun empty. qfalse: it is not drawn.
===============
*/
qboolean VK_BeginProbe( void ) {
#ifdef RD_VISION_GLES
	return qfalse;
#else
	if ( !vk.frame || !VK_ProbesSupported() ) {
		return qfalse;
	}
	VK_EndTarget();
	VK_SwapProbeTargets();
	VK_BeginTarget( &vk.color, &vk.depth, 1, qfalse );
	VK_BindFrameSets();
	vk.stateValid = qfalse;
	return qtrue;
#endif
}

/*
===============
VK_EndProbe

The face that was drawn goes to its place in the atlas (a new atlas with the first face of the first probe); after
the last face of the last probe the smaller levels are made and the texture table shows the atlas.
===============
*/
void VK_EndProbe( int face, int probe, int numProbes ) {
#ifndef RD_VISION_GLES
	VkCommandBuffer cmd = vk.frame->cmd;
	VkImageCopy region;

	VK_EndTarget();
	VK_SwapProbeTargets();
	if ( face == 0 && probe == 0 ) {
		if ( vk.probeAtlasProbes != numProbes ) {
			VK_CreateProbeAtlas( numProbes );
		}
		VK_AtlasLayout( cmd, 0, PROBE_LEVELS, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT );
	}
	if ( vk.probeAtlas && probe < vk.probeAtlasProbes ) {
		memset( &region, 0, sizeof( region ) );
		region.srcSubresource.aspectMask = region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.layerCount = region.dstSubresource.layerCount = 1;
		region.dstOffset.x = face * PROBE_SIZE;
		region.dstOffset.y = probe * PROBE_SIZE;
		region.extent.width = region.extent.height = PROBE_SIZE;
		region.extent.depth = 1;
		VK_TargetLayout( cmd, &vk.probeColor, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
		qvkCmdCopyImage( cmd, vk.probeColor.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk.probeAtlas, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &region );
		VK_TargetLayout( cmd, &vk.probeColor, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );

		if ( face == 5 && probe == numProbes - 1 ) {
			// each level half of the one before it
			for ( int level = 1; level < PROBE_LEVELS; level++ ) {
				VkImageBlit blit;

				VK_AtlasLayout( cmd, level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT );
				memset( &blit, 0, sizeof( blit ) );
				blit.srcSubresource.aspectMask = blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				blit.srcSubresource.mipLevel = (uint32_t)( level - 1 );
				blit.dstSubresource.mipLevel = (uint32_t)level;
				blit.srcSubresource.layerCount = blit.dstSubresource.layerCount = 1;
				blit.srcOffsets[1].x = ( PROBE_SIZE * 6 ) >> ( level - 1 );
				blit.srcOffsets[1].y = ( PROBE_SIZE * numProbes ) >> ( level - 1 );
				blit.srcOffsets[1].z = 1;
				blit.dstOffsets[1].x = ( PROBE_SIZE * 6 ) >> level;
				blit.dstOffsets[1].y = ( PROBE_SIZE * numProbes ) >> level;
				blit.dstOffsets[1].z = 1;
				qvkCmdBlitImage( cmd, vk.probeAtlas, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk.probeAtlas, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1, &blit, VK_FILTER_LINEAR );
			}
			VK_AtlasLayout( cmd, 0, PROBE_LEVELS - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT );
			VK_AtlasLayout( cmd, PROBE_LEVELS - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT );
			VK_ShowImage( (int)tr.probeImage->texnum, vk.probeAtlasView, qtrue );
		}
	}
	VK_BeginTarget( &vk.color, &vk.depth, 2, qtrue );
	VK_BindFrameSets();
	vk.stateValid = qfalse;
	VK_InvalidateTransform();
#endif
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

	// the puddle view has no flares: it would ask for the same ones at other pixels
	if ( backEnd.viewParms.isPuddle ) {
		return qfalse;
	}

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
