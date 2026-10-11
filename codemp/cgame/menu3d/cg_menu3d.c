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

// cg_menu3d.c -- the menu in the level. Not from JA+.
//
// While a menu is open in a game the camera moves behind the right shoulder of the player, and the menu stands
// in the level in front of him as a panel he looks at: the game goes on around it, a little out of focus and
// muffled (cg_immersiveMenu 0: the flat menu of the game).
//
// The menu is not painted onto a texture. The ui draws it as it always does, and the renderer puts that 2D onto
// a plane in the level: this file says where the plane is by a matrix from the grid of the 2D to clip space
// (trap->ext.R_SetUIPlane), which the engine sets around the drawing of the menus. The cursor of the ui is
// drawn on the plane with the rest and the mouse moves it there, so nothing has to find out what the mouse
// points at. Behind the menu this file adds a panel to the scene, and it asks for the blur behind it
// (trap->ext.R_SetDepthOfField) and for the muffled sound (the cvar s_muffle).
//
// It needs an engine of this tree (cvar cl_cgameUIPlane). Not while spectating, dead or at the end of a map:
// the camera of the game has no player to stand behind then.

#include "cgame/cg_local.h"

#define MENU3D_IN_MSEC		350
#define MENU3D_OUT_MSEC		250
#define MENU3D_SCREEN_PART	0.72f		// how much of the height of the screen the menu takes

static struct {
	qboolean	open;				// a menu is open and is shown in the level
	float		blend;				// 0 the view of the game, 1 the view of the menu
	int			lastTime;
	float		yaw;				// where the player looked when the menu opened: the panel stays there
	float		muffle;				// what s_muffle was last set to

	// the panel, as long as blend is above 0
	vec3_t		center, right, down;
	float		unit;				// units of the level for one of the 2D grid
} menu3d;

void CG_Menu3DInit( void ) {
	memset( &menu3d, 0, sizeof( menu3d ) );
}

static qboolean Menu3D_Engine( const char *cvar ) {
	char value[8];

	trap->Cvar_VariableStringBuffer( cvar, value, sizeof( value ) );
	return (qboolean)( atoi( value ) == 1 );
}

/*
=================
CG_Menu3DFrame

Once a frame, before the view is worked out. qtrue: the view is on its way to the menu or back, the player has
to be drawn.
=================
*/
qboolean CG_Menu3DFrame( void ) {
	const int		now = trap->Milliseconds();
	const int		msec = Com_Clampi( 0, 100, now - menu3d.lastTime );
	playerState_t	*ps = &cg.predictedPlayerState;
	qboolean		open;

	menu3d.lastTime = now;
	open = (qboolean)( cg_immersiveMenu.integer && cg.snap && !cg.demoPlayback
		&& ( trap->Key_GetCatcher() & KEYCATCH_UI )
		&& ps->pm_type != PM_SPECTATOR && ps->pm_type != PM_INTERMISSION && !( ps->pm_flags & PMF_FOLLOW )
		&& cg.snap->ps.stats[STAT_HEALTH] > 0 && !cg.snap->ps.zoomMode
		&& Menu3D_Engine( "cl_cgameUIPlane" ) );

	if ( open && !menu3d.open ) {
		menu3d.yaw = ps->viewangles[YAW];
	}
	menu3d.open = open;
	if ( open ) {
		menu3d.blend = Q_min( 1.0f, menu3d.blend + (float)msec / MENU3D_IN_MSEC );
	} else {
		menu3d.blend = Q_max( 0.0f, menu3d.blend - (float)msec / MENU3D_OUT_MSEC );
	}

	// the sound of the game is muffled while the menu is there (s_muffle of the engine, 0 to 1)
	if ( fabsf( menu3d.blend - menu3d.muffle ) > 0.02f || ( menu3d.blend != menu3d.muffle && ( menu3d.blend == 0.0f || menu3d.blend == 1.0f ) ) ) {
		menu3d.muffle = menu3d.blend;
		trap->Cvar_Set( "s_muffle", va( "%.2f", menu3d.muffle ) );
	}
	return (qboolean)( menu3d.blend > 0.0f );
}

qboolean CG_Menu3DActive( void ) {
	return (qboolean)( menu3d.blend > 0.0f );
}

// c = a * b, matrices of OpenGL: 16 values column by column
static void Menu3D_Multiply( const float *a, const float *b, float *c ) {
	int col, row;

	for ( col = 0; col < 4; col++ ) {
		for ( row = 0; row < 4; row++ ) {
			c[col * 4 + row] = a[row] * b[col * 4] + a[4 + row] * b[col * 4 + 1] + a[8 + row] * b[col * 4 + 2] + a[12 + row] * b[col * 4 + 3];
		}
	}
}

/*
=================
CG_Menu3DView

After the view of the game is worked out: moves it towards the view of the menu, behind the right shoulder of
the player with the panel in front of him.
=================
*/
void CG_Menu3DView( void ) {
	playerState_t	*ps = &cg.predictedPlayerState;
	vec3_t			angles, forward, right, up, head, camera, look, lookAngles, camRight, camUp;
	trace_t			trace;
	static vec3_t	mins = { -4, -4, -4 }, maxs = { 4, 4, 4 };
	float			blend, distance, tilt;
	int				i;

	if ( menu3d.blend <= 0.0f ) {
		return;
	}

	// the player, as he stood when the menu opened: he may be pushed around, but he does not turn
	VectorSet( angles, 0.0f, menu3d.yaw, 0.0f );
	AngleVectors( angles, forward, right, up );
	VectorCopy( ps->origin, head );
	head[2] += ps->viewheight;

	// the camera: behind him, to the right and a little higher, as far as the level lets it
	VectorMA( head, -46.0f, forward, camera );
	VectorMA( camera, 24.0f, right, camera );
	VectorMA( camera, 6.0f, up, camera );
	CG_Trace( &trace, head, mins, maxs, camera, ps->clientNum, MASK_SOLID | CONTENTS_PLAYERCLIP );
	VectorCopy( trace.endpos, camera );

	// the panel: in front of him, a little to the right, where the camera looks past his shoulder
	VectorMA( head, 30.0f, forward, menu3d.center );
	VectorMA( menu3d.center, 13.0f, right, menu3d.center );
	VectorMA( menu3d.center, -1.0f, up, menu3d.center );

	VectorMA( menu3d.center, -5.0f, right, look );
	VectorSubtract( look, camera, look );
	distance = VectorNormalize( look );
	vectoangles( look, lookAngles );
	AngleVectors( lookAngles, NULL, camRight, camUp );

	// it faces the camera, turned a little so that its right edge is farther away
	tilt = DEG2RAD( 10.0f );
	VectorScale( camRight, cosf( tilt ), menu3d.right );
	VectorMA( menu3d.right, sinf( tilt ), look, menu3d.right );
	VectorNegate( camUp, menu3d.down );
	menu3d.unit = MENU3D_SCREEN_PART * 2.0f * distance * tanf( DEG2RAD( cg.refdef.fov_y * 0.5f ) ) / SCREEN_HEIGHT;

	// from the view of the game to the view of the menu, slowly at both ends
	blend = menu3d.blend * menu3d.blend * ( 3.0f - 2.0f * menu3d.blend );
	for ( i = 0; i < 3; i++ ) {
		cg.refdef.vieworg[i] += ( camera[i] - cg.refdef.vieworg[i] ) * blend;
		cg.refdef.viewangles[i] = LerpAngle( cg.refdef.viewangles[i], lookAngles[i], blend );
	}
	AnglesToAxis( cg.refdef.viewangles, cg.refdef.viewaxis );
}

/*
=================
CG_Menu3DScene

Right before the scene is drawn: the panel behind the menu goes into the scene, and the engine is told where the
plane of the menu is, for this frame.
=================
*/
void CG_Menu3DScene( void ) {
	static vec4_t	fill = { 0.03f, 0.06f, 0.12f, 0.55f };
	static vec4_t	border = { 0.45f, 0.75f, 1.0f, 0.9f };
	static vec4_t	glow = { 0.2f, 0.45f, 1.0f, 0.3f };
	const float		*org = cg.refdef.vieworg;
	const float		*fwd = cg.refdef.viewaxis[0], *left = cg.refdef.viewaxis[1], *upAxis = cg.refdef.viewaxis[2];
	const float		zNear = 1.0f, zFar = 8192.0f;
	float			model[16], view[16], projection[16], modelView[16], matrix[16];
	vec3_t			corner, up, behind, normal;
	float			alpha, halfW, halfH;

	if ( menu3d.blend <= 0.0f ) {
		return;
	}
	alpha = menu3d.blend;

	// the panel behind the menu, a little farther from the camera than the menu itself
	halfW = ( SCREEN_WIDTH * 0.5f + cgs.bias2D ) * menu3d.unit + 2.0f * menu3d.unit;
	halfH = SCREEN_HEIGHT * 0.5f * menu3d.unit + 2.0f * menu3d.unit;
	VectorNegate( menu3d.down, up );
	CrossProduct( menu3d.right, up, normal );
	VectorMA( menu3d.center, 0.5f, normal, behind );
	if ( DotProduct( normal, fwd ) < 0.0f ) {
		VectorMA( menu3d.center, -0.5f, normal, behind );
	}
	CG_BubblePanelAt( behind, menu3d.right, up, halfW, halfH, 14.0f * menu3d.unit, fill, border, glow, alpha );

	// the blur of what is not at the distance of the panel
	if ( Menu3D_Engine( "cl_cgameDoF" ) ) {
		vec3_t delta;

		VectorSubtract( menu3d.center, org, delta );
		trap->ext.R_SetDepthOfField( VectorLength( delta ), alpha );
	}

	if ( !menu3d.open ) {
		return;		// the menu is closed, the view is on its way back
	}

	// From the grid of the 2D to the level: (0, 0) is the upper left corner of the 640 x 480, which has its
	// middle at the middle of the panel.
	VectorMA( menu3d.center, -SCREEN_WIDTH * 0.5f * menu3d.unit, menu3d.right, corner );
	VectorMA( corner, -SCREEN_HEIGHT * 0.5f * menu3d.unit, menu3d.down, corner );
	memset( model, 0, sizeof( model ) );
	VectorScale( menu3d.right, menu3d.unit, &model[0] );
	VectorScale( menu3d.down, menu3d.unit, &model[4] );
	VectorCopy( normal, &model[8] );
	VectorCopy( corner, &model[12] );
	model[15] = 1.0f;

	// from the level to the eye: x to the right, y up, z towards the viewer
	memset( view, 0, sizeof( view ) );
	view[0] = -left[0];		view[4] = -left[1];		view[8] = -left[2];		view[12] = DotProduct( org, left );
	view[1] = upAxis[0];	view[5] = upAxis[1];	view[9] = upAxis[2];	view[13] = -DotProduct( org, upAxis );
	view[2] = -fwd[0];		view[6] = -fwd[1];		view[10] = -fwd[2];		view[14] = DotProduct( org, fwd );
	view[15] = 1.0f;

	// and the perspective of the scene
	memset( projection, 0, sizeof( projection ) );
	projection[0] = 1.0f / tanf( DEG2RAD( cg.refdef.fov_x * 0.5f ) );
	projection[5] = 1.0f / tanf( DEG2RAD( cg.refdef.fov_y * 0.5f ) );
	projection[10] = -( zFar + zNear ) / ( zFar - zNear );
	projection[11] = -1.0f;
	projection[14] = -2.0f * zFar * zNear / ( zFar - zNear );

	Menu3D_Multiply( view, model, modelView );
	Menu3D_Multiply( projection, modelView, matrix );
	trap->ext.R_SetUIPlane( matrix );
}
