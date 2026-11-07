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

// cg_photo.c -- the photo mode. Not from JA+.
//
// v_photo (the console, or the menu in the level) turns it on and off. The HUD goes, the camera circles the own
// player: the mouse turns it, the wheel moves it nearer and farther. It is held off the walls as the camera of
// third person is (a trace from the player out), so it never looks through one. The cgame takes the keys and the
// mouse while it is on (KEYCATCH_CGAME), so the player stands still. The left button focuses on what is in the
// middle (the depth of field of rd-vulkan-vision), the middle button takes the focus away, the keys 1 to 5 lay a
// filter over the picture (the screen effects, 0 none), the right button or Enter takes a picture
// (screenshot_png, a frame later, when the hint is gone too), Esc leaves. It leaves by itself when the player dies,
// is no longer in the game or a menu takes the keys.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"

#define PHOTO_MIN_RANGE		30.0f
#define PHOTO_MAX_RANGE		400.0f
#define PHOTO_TURN			0.15f		// degrees a count of the mouse turns the camera
#define PHOTO_HEAD			28.0f		// over the origin of the player, what the camera circles
#define PHOTO_NUM_FILTERS	6

static struct {
	qboolean	on;
	float		yaw, pitch;				// of the camera, looking at the player
	float		range;
	float		focus;					// units from the camera, 0: no depth of field
	int			filter;					// 0: none
	int			shotTime;				// a picture is asked for: the frame that hides the hint comes first
	int			savedTime;				// when the last picture was taken, for the line that says so
} photo;

static qboolean Photo_Engine( const char *cvar ) {
	char value[8];

	trap->Cvar_VariableStringBuffer( cvar, value, sizeof( value ) );
	return (qboolean)( atoi( value ) == 1 );
}

// the player can be photographed: alive and in the game
static qboolean CG_PhotoAllowed( void ) {
	return (qboolean)( cg.snap && cg.snap->ps.stats[STAT_HEALTH] > 0 && cg.snap->ps.pm_type != PM_SPECTATOR
		&& cg.snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR && !( cg.snap->ps.pm_flags & PMF_FOLLOW ) );
}

static void CG_PhotoLeave( void ) {
	if ( !photo.on ) {
		return;
	}
	photo.on = qfalse;
	trap->Key_SetCatcher( trap->Key_GetCatcher() & ~KEYCATCH_CGAME );
	trap->Cvar_Set( "v_catchQuiet", "0" );
}

// v_photo: on and off
void CG_Photo_f( void ) {
	if ( photo.on ) {
		CG_PhotoLeave();
		return;
	}
	if ( !CG_PhotoAllowed() ) {
		return;
	}
	memset( &photo, 0, sizeof( photo ) );
	photo.on = qtrue;
	// behind the player, a little from above, as far as his camera of third person is
	photo.yaw = cg.refdef.viewangles[YAW];
	photo.pitch = 15.0f;
	photo.range = Com_Clamp( PHOTO_MIN_RANGE, PHOTO_MAX_RANGE, cg_thirdPersonRange.value * 1.5f );
	trap->Key_SetCatcher( trap->Key_GetCatcher() | KEYCATCH_CGAME );
	// the player poses, he does not type: no dots over his head in the picture (CL_CmdButtons())
	trap->Cvar_Set( "v_catchQuiet", "1" );
}

qboolean CG_PhotoActive( void ) {
	return photo.on;
}

/*
=================
CG_PhotoFrame

Once a frame before the view is worked out: whether the photo mode goes on (qtrue: the view is of third person)
=================
*/
qboolean CG_PhotoFrame( void ) {
	if ( !photo.on ) {
		return qfalse;
	}
	// a menu or the console took the keys, the player died or left the game
	if ( !CG_PhotoAllowed() || !( trap->Key_GetCatcher() & KEYCATCH_CGAME ) ) {
		CG_PhotoLeave();
		return qfalse;
	}
	if ( photo.shotTime && cg.time - photo.shotTime >= 0 && cg.time != photo.shotTime ) {
		trap->SendConsoleCommand( "screenshot_png\n" );
		photo.shotTime = 0;
		photo.savedTime = cg.time;
	}
	return qtrue;
}

/*
=================
CG_PhotoView

After the view is worked out: the camera circles the player, held off the walls
=================
*/
void CG_PhotoView( void ) {
	static const vec3_t	mins = { -4.0f, -4.0f, -4.0f }, maxs = { 4.0f, 4.0f, 4.0f };
	vec3_t				target, angles, forward, eye;
	trace_t				tr;

	if ( !photo.on ) {
		return;
	}
	VectorCopy( cg_entities[cg.snap->ps.clientNum].lerpOrigin, target );
	target[2] += PHOTO_HEAD;
	VectorSet( angles, photo.pitch, photo.yaw, 0.0f );
	AngleVectors( angles, forward, NULL, NULL );
	VectorMA( target, -photo.range, forward, eye );
	CG_Trace( &tr, target, mins, maxs, eye, cg.snap->ps.clientNum, MASK_SOLID );
	VectorCopy( tr.endpos, cg.refdef.vieworg );
	VectorCopy( angles, cg.refdef.viewangles );
	AnglesToAxis( cg.refdef.viewangles, cg.refdef.viewaxis );

	// the depth of field where the left button set it
	if ( photo.focus > 0.0f && Photo_Engine( "v_cgameDoF" ) ) {
		trap->ext.R_SetDepthOfField( photo.focus, 1.0f );
	}
}

// the filter of the photo mode over the screen effects (screenfx/cg_screenfx.c)
void CG_PhotoGrade( screenEffect_t *fx ) {
	if ( !photo.on ) {
		return;
	}
	switch ( photo.filter ) {
	case 1:		// black and white, darker edges
		fx->desaturate = 1.0f;
		VectorSet4( fx->vignette, 0.0f, 0.0f, 0.0f, 0.55f );
		break;
	case 2:		// warm
		VectorSet4( fx->tint, 1.0f, 0.62f, 0.3f, 0.18f );
		break;
	case 3:		// cold
		VectorSet4( fx->tint, 0.35f, 0.6f, 1.0f, 0.2f );
		break;
	case 4:		// old: faded brown, dark edges
		fx->desaturate = 0.7f;
		VectorSet4( fx->tint, 0.75f, 0.55f, 0.32f, 0.3f );
		VectorSet4( fx->vignette, 0.08f, 0.05f, 0.02f, 0.6f );
		break;
	case 5:		// a dream: soft colours split towards the edges
		fx->split = 0.7f;
		fx->desaturate = 0.3f;
		VectorSet4( fx->vignette, 0.15f, 0.1f, 0.25f, 0.4f );
		break;
	default:
		break;
	}
}

qboolean CG_PhotoMouseEvent( int x, int y ) {
	if ( !photo.on ) {
		return qfalse;
	}
	photo.yaw -= x * PHOTO_TURN;
	photo.pitch = Com_Clamp( -85.0f, 85.0f, photo.pitch + y * PHOTO_TURN );
	return qtrue;
}

qboolean CG_PhotoKeyEvent( int key, qboolean down ) {
	if ( !photo.on ) {
		return qfalse;
	}
	if ( !down ) {
		return qtrue;
	}
	switch ( key ) {
	case A_ESCAPE:
		CG_PhotoLeave();
		break;
	case A_MWHEELUP:
		photo.range = Q_max( PHOTO_MIN_RANGE, photo.range / 1.15f );
		break;
	case A_MWHEELDOWN:
		photo.range = Q_min( PHOTO_MAX_RANGE, photo.range * 1.15f );
		break;
	case A_MOUSE1: {
		// on what is in the middle of the view
		trace_t	tr;
		vec3_t	end;

		VectorMA( cg.refdef.vieworg, 8192.0f, cg.refdef.viewaxis[0], end );
		CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, end, -1, MASK_SHOT );
		photo.focus = Q_max( 16.0f, tr.fraction * 8192.0f );
		break;
	}
	case A_MOUSE3:
		photo.focus = 0.0f;
		break;
	case A_MOUSE2:
	case A_ENTER:
	case A_KP_ENTER:
		photo.shotTime = cg.time;
		break;
	default:
		if ( key >= '0' && key < '0' + PHOTO_NUM_FILTERS ) {
			photo.filter = key - '0';
		}
		break;
	}
	return qtrue;
}

/*
=================
CG_PhotoDraw2D

In place of the 2D while the photo mode is on: a line of what the keys do, gone in the frame of a picture, and a
moment of what was taken
=================
*/
void CG_PhotoDraw2D( void ) {
	vec4_t		color;
	const char	*text;

	if ( photo.shotTime ) {
		return;
	}
	VectorSet4( color, 1.0f, 1.0f, 1.0f, 0.75f );
	if ( photo.savedTime && cg.time - photo.savedTime < 2000 && cg.time >= photo.savedTime ) {
		text = CG_VisionText( "Picture saved to the screenshots folder." );
	} else {
		text = CG_VisionText( "Mouse: turn. Wheel: distance. Left: focus. 1-5: filter. Right: photo. Esc: leave." );
	}
	CG_Text_Paint( 320.0f - CG_Text_Width( text, 0.45f, FONT_SMALL ) * 0.5f, 450.0f, 0.45f, color, text, 0, 0, ITEM_TEXTSTYLE_SHADOWED,
		FONT_SMALL );
}
