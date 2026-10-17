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
// in the level to the right of him as a panel: the game goes on around it, a little out of focus and muffled
// (cg_immersiveMenu 0: the flat menu of the game). The player is at the left edge of the picture and the menu
// takes the rest, so neither is in front of the other.
//
// The menu is not painted onto a texture. The ui draws it as it always does, and the renderer puts that 2D onto
// a plane in the level: this file says where the plane is by a matrix from the grid of the 2D to clip space
// (trap->ext.R_SetUIPlane), which the engine sets around the drawing of the menus. The cursor of the ui is
// drawn on the plane with the rest and the mouse moves it there, so nothing has to find out what the mouse
// points at. Behind the menu this file adds a panel to the scene, and it asks for the blur behind it
// (trap->ext.R_SetDepthOfField) and for the muffled sound (the cvar s_muffle). The panel is as large as what is
// open on it: the ui says how much of the 640 x 480 its menus take (the cvar ui_openBox), so the bar at the top
// alone is a bar, and the panel grows when a page opens under it.
//
// It needs an engine of this tree (cvar cl_cgameUIPlane). Not while spectating, dead or at the end of a map:
// the camera of the game has no player to stand behind then.

#include "cgame/cg_local.h"

#define MENU3D_IN_MSEC		350
#define MENU3D_OUT_MSEC		250
#define MENU3D_SCREEN_PART	0.72f		// how much of the height of the screen the menu takes
#define MENU3D_PLAYER_X		-0.68f		// where the player is in the picture: -1 its left edge, 0 the middle, 1 the right edge
#define MENU3D_PANEL_X		0.27f		// and the middle of the menu
#define MENU3D_PANEL_DEPTH	64.0f		// how far in front of the camera the menu stands
#define MENU3D_MARGIN		8.0f		// the panel behind the menu is larger by this on each side, in units of the 2D grid
#define MENU3D_BOX_MSEC		70.0f		// the panel covers most of the way to a new size in about three times this

static struct {
	qboolean	open;				// a menu is open and is shown in the level
	float		blend;				// 0 the view of the game, 1 the view of the menu
	int			lastTime;
	float		yaw;				// where the player looks, followed slowly: the camera is behind that
	float		muffle;				// what s_muffle was last set to

	// the panel, as long as blend is above 0
	vec3_t		center, right, down;
	float		unit;				// units of the level for one of the 2D grid
	float		box[4];				// what of the 640 x 480 the panel is behind: x, y, width, height
	qboolean	boxSet;
} menu3d;

void CG_Menu3DInit( void ) {
	memset( &menu3d, 0, sizeof( menu3d ) );
	// What the cgame before this one set stays in the cvar when it ends (a level change while a menu muffled the
	// sound), and this one would take it for 0:
	// the sound stayed dull until a menu was opened and closed.
	trap->Cvar_Set( "s_muffle", "0" );
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

	// The camera is behind where he looks. With a menu open he does not turn by himself (the mouse is the
	// cursor's), but a teleporter, an admin or a respawn turn him: the camera then comes round within a moment.
	// It stood where he looked when the menu opened once, and after such a turn it looked him in the face from
	// inside a wall (seen in the check of 2026-10-11).
	if ( open && !menu3d.open ) {
		menu3d.yaw = ps->viewangles[YAW];
	} else if ( open ) {
		menu3d.yaw = LerpAngle( menu3d.yaw, ps->viewangles[YAW], Q_min( 1.0f, (float)msec / 150.0f ) );
	}
	// How large the panel is: as what the ui says is open, the whole 640 x 480 where it says nothing (another
	// ui). It follows a new size within a moment, and has the size at once when the menu comes up. (The ui
	// says it after it painted, which is after this frame: for the first frame of a menu nothing is known yet,
	// and between two menus neither. The size stays as it is then.)
	if ( open ) {
		char	said[64];
		int		x, y, w, h, i;

		trap->Cvar_VariableStringBuffer( "ui_openBox", said, sizeof( said ) );
		if ( sscanf( said, "%i %i %i %i", &x, &y, &w, &h ) == 4 && w > 0 && h > 0 ) {
			const float want[4] = { (float)x, (float)y, (float)w, (float)h };

			for ( i = 0; i < 4; i++ ) {
				menu3d.box[i] = menu3d.boxSet ? menu3d.box[i] + ( want[i] - menu3d.box[i] ) * ( 1.0f - expf( -msec / MENU3D_BOX_MSEC ) ) : want[i];
			}
			menu3d.boxSet = qtrue;
		}
	} else if ( menu3d.blend <= 0.0f ) {
		menu3d.boxSet = qfalse;
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

After the view of the game is worked out: moves it towards the view of the menu. The camera is behind the right
shoulder of the player and looks past him, so far to the right that he is at the left edge of the picture. The
panel is laid out in that picture, to the right of him, and then set into the level at that place. It is drawn
over the level, whatever stands there.
=================
*/
void CG_Menu3DView( void ) {
	playerState_t	*ps = &cg.predictedPlayerState;
	const float		tanX = tanf( DEG2RAD( cg.refdef.fov_x * 0.5f ) ), tanY = tanf( DEG2RAD( cg.refdef.fov_y * 0.5f ) );
	vec3_t			angles, forward, right, up, head, camera, look, lookAngles, camRight, camUp, toPlayer;
	trace_t			trace;
	static vec3_t	mins = { -4, -4, -4 }, maxs = { 4, 4, 4 };
	float			blend, depth, tilt, back, panelX;
	int				i;

	if ( menu3d.blend <= 0.0f ) {
		return;
	}

	// the player, by where he looks (CG_Menu3DFrame)
	VectorSet( angles, 0.0f, menu3d.yaw, 0.0f );
	AngleVectors( angles, forward, right, up );
	VectorCopy( ps->origin, head );
	head[2] += ps->viewheight;

	// The camera: behind him, to the right and a little higher, as far as the level lets it. With a narrow field
	// of view it is farther back, so that he takes the same part of the picture (44 behind him at 16:9 and
	// cg_fov 80).
	back = Com_Clamp( 0.8f, 1.6f, 1.12f / tanX );
	VectorMA( head, -44.0f * back, forward, camera );
	VectorMA( camera, 20.0f * back, right, camera );
	VectorMA( camera, 2.0f, up, camera );
	CG_Trace( &trace, head, mins, maxs, camera, ps->clientNum, MASK_SOLID | CONTENTS_PLAYERCLIP );
	VectorCopy( trace.endpos, camera );

	// It looks to the right of him by the angle that puts him at MENU3D_PLAYER_X, and a little down: his head
	// is above the middle of the picture then, and he is seen down to the hips.
	VectorSubtract( head, camera, toPlayer );
	if ( VectorLengthSquared( toPlayer ) < 1.0f ) {
		VectorCopy( forward, toPlayer );		// no room behind him at all
	}
	vectoangles( toPlayer, lookAngles );
	lookAngles[YAW] -= RAD2DEG( atanf( -MENU3D_PLAYER_X * tanX ) );
	lookAngles[PITCH] = 6.0f;
	lookAngles[ROLL] = 0.0f;
	AngleVectors( lookAngles, look, camRight, camUp );

	// The panel faces the camera, turned a little towards the player: its left edge, the one next to him, is the
	// farther one.
	tilt = DEG2RAD( 8.0f );
	VectorScale( camRight, cosf( tilt ), menu3d.right );
	VectorMA( menu3d.right, -sinf( tilt ), look, menu3d.right );
	VectorNegate( camUp, menu3d.down );

	// Where it stands: seen from the camera its middle is at MENU3D_PANEL_X of the picture. How far away it is
	// changes nothing in the picture, the size goes with the distance, and nothing of the level hides the menu
	// or the panel behind it (CG_BubblePanelAt), so the level is not asked whether it has the room.
	depth = MENU3D_PANEL_DEPTH;
	menu3d.unit = MENU3D_SCREEN_PART * 2.0f * depth * tanY / SCREEN_HEIGHT;
	// on a narrow window it moves left as far as it has to for its right edge
	panelX = Q_min( MENU3D_PANEL_X, 0.97f - MENU3D_SCREEN_PART * ( (float)SCREEN_WIDTH / SCREEN_HEIGHT ) * tanY / tanX );
	VectorMA( camera, depth, look, menu3d.center );
	VectorMA( menu3d.center, panelX * tanX * depth, camRight, menu3d.center );

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
	// The inside lets little of the level through: at half it took the colour of what stood behind it, and the
	// rim around the menu was white in front of a bright wall.
	static vec4_t	fill = { 0.02f, 0.04f, 0.1f, 0.9f };
	static vec4_t	border = { 0.25f, 0.45f, 0.9f, 0.85f };
	static vec4_t	light = { 0.15f, 0.35f, 0.9f, 0.5f };
	const float		*org = cg.refdef.vieworg;
	const float		*fwd = cg.refdef.viewaxis[0], *left = cg.refdef.viewaxis[1], *upAxis = cg.refdef.viewaxis[2];
	const float		zNear = 1.0f, zFar = 8192.0f;
	float			model[16], view[16], projection[16], modelView[16], matrix[16];
	vec3_t			corner, up, behind, normal;
	float			alpha, halfW, halfH, box[4] = { 0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT };

	if ( menu3d.blend <= 0.0f ) {
		return;
	}
	alpha = menu3d.blend;

	// The panel behind the menu, a little farther from the camera than the menu itself. It is behind what is
	// open (CG_Menu3DFrame), at most the 640 x 480 the menus of a game stay in, not the wider grid of a wide
	// window: the player stands to the left of it.
	if ( menu3d.boxSet ) {
		memcpy( box, menu3d.box, sizeof( box ) );
	}
	halfW = ( box[2] * 0.5f + MENU3D_MARGIN ) * menu3d.unit;
	halfH = ( box[3] * 0.5f + MENU3D_MARGIN ) * menu3d.unit;
	VectorNegate( menu3d.down, up );
	CrossProduct( menu3d.right, up, normal );
	VectorMA( menu3d.center, 0.5f, normal, behind );
	if ( DotProduct( normal, fwd ) < 0.0f ) {
		VectorMA( menu3d.center, -0.5f, normal, behind );
	}
	// (from the middle of the 640 x 480 to the middle of the box)
	VectorMA( behind, ( box[0] + box[2] * 0.5f - SCREEN_WIDTH * 0.5f ) * menu3d.unit, menu3d.right, behind );
	VectorMA( behind, ( box[1] + box[3] * 0.5f - SCREEN_HEIGHT * 0.5f ) * menu3d.unit, menu3d.down, behind );
	CG_BubblePanelAt( behind, menu3d.right, up, halfW, halfH, 14.0f * menu3d.unit, fill, border, light, alpha );

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
