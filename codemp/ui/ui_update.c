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


// ui_update.c -- the line on the main menu that says a newer version of the game is out. Not from JA+, see
// docs/japlus/update.md.
//
// The updater is in the engine (client/cl_update.cpp). It says what it has to say in two cvars: v_updateState
// (0 nothing, 1 a newer version is out, 2 its files are coming in, 3 that failed, 4 the game is about to start
// again) and v_updateText, the line itself. This file draws that line while the main menu is up, and a click
// on it is the command "update".
//
// It is drawn here and is no item of a menu file, so it is there whatever main menu is loaded: the one of the
// client assets of this tree in japlus, where the game starts, or the one of the original game in base. Both
// have the same layout, and the line sits in the band between the ring and the EXIT button, which is free
// in both.

#include "ui_local.h"
#include "ui_skin.h"

#define NOTICE_FONT		4		// the small font of the menus
#define NOTICE_SCALE	1.0f
#define NOTICE_Y		414.0f
#define NOTICE_PAD_X	10.0f
#define NOTICE_PAD_Y	4.0f

// v_updateState of cl_update.cpp
#define NOTICE_AVAILABLE	1
#define NOTICE_FAILED		3

static struct {
	qboolean	shown;			// it was drawn in this frame,
	qboolean	clickable;		//	and a click on it does something
	float		x, y, w, h;
} notice;

static qboolean UI_UpdateNoticeHover( void ) {
	return ( uiInfo.uiDC.cursorx >= notice.x && uiInfo.uiDC.cursorx <= notice.x + notice.w
		&& uiInfo.uiDC.cursory >= notice.y && uiInfo.uiDC.cursory <= notice.y + notice.h ) ? qtrue : qfalse;
}

/*
=================
UI_UpdateNoticeDraw

Once a frame, over the menus and under the cursor.
=================
*/
void UI_UpdateNoticeDraw( void ) {
	const menuDef_t	*menu = Menu_GetFocused();
	const int		state = (int)trap->Cvar_VariableValue( "v_updateState" );
	vec4_t			back = { 0.0f, 0.0f, 0.0f, 0.8f }, edge = { 0.65f, 0.65f, 1.0f, 1.0f };
	vec4_t			plain = { 0.65f, 0.65f, 1.0f, 1.0f }, lit = { 1.0f, 1.0f, 1.0f, 1.0f }, failed = { 1.0f, 0.6f, 0.2f, 1.0f };
	char			text[128];

	notice.shown = qfalse;
	if ( state <= 0 || !menu || !menu->window.name || Q_stricmp( menu->window.name, "main" ) )
		return;
	trap->Cvar_VariableStringBuffer( "v_updateText", text, sizeof( text ) );
	if ( !text[0] )
		return;

	notice.shown = qtrue;
	notice.clickable = ( state == NOTICE_AVAILABLE || state == NOTICE_FAILED ) ? qtrue : qfalse;

	// With the menus of the new look: a pill of its glass under its two panels, with its letters. Lit while the
	// mouse is on it.
	if ( UI_NewLook() ) {
		const vec4_t	glass = { 0.035f, 0.07f, 0.12f, 0.86f }, rim = { 0.43f, 0.83f, 1.0f, 0.45f };
		const vec4_t	litGlass = { 0.1f, 0.2f, 0.3f, 0.92f }, litRim = { 0.43f, 0.83f, 1.0f, 0.95f };
		vec4_t			bright = { 0.86f, 0.92f, 0.97f, 1.0f }, amber = { 1.0f, 0.72f, 0.3f, 1.0f };
		const float		scale = 0.62f;
		qboolean		over;

		notice.w = (float)uiInfo.uiDC.textWidth( text, scale, SKIN_FONT_SANS ) + 28.0f;
		notice.h = 18.0f;
		notice.x = ( SCREEN_WIDTH - notice.w ) * 0.5f;
		notice.y = NOTICE_Y;
		over = (qboolean)( notice.clickable && UI_UpdateNoticeHover() );
		Skin_Panel( notice.x, notice.y, notice.w, notice.h, notice.h * 0.5f, over ? litGlass : glass, over ? litRim : rim );
		uiInfo.uiDC.drawText( notice.x + 14.0f, notice.y + 3.4f, scale, ( state == NOTICE_FAILED ) ? amber : ( over ? lit : bright ),
			text, 0.0f, 0, 0, SKIN_FONT_SANS );
		trap->R_SetColor( NULL );
		return;
	}

	notice.w = (float)uiInfo.uiDC.textWidth( text, NOTICE_SCALE, NOTICE_FONT ) + NOTICE_PAD_X * 2.0f;
	notice.h = (float)uiInfo.uiDC.textHeight( text, NOTICE_SCALE, NOTICE_FONT ) + NOTICE_PAD_Y * 2.0f;
	notice.x = ( SCREEN_WIDTH - notice.w ) * 0.5f;
	notice.y = NOTICE_Y;

	uiInfo.uiDC.fillRect( notice.x, notice.y, notice.w, notice.h, back );
	uiInfo.uiDC.drawRect( notice.x, notice.y, notice.w, notice.h, 1.0f, edge );
	uiInfo.uiDC.drawText( notice.x + NOTICE_PAD_X, notice.y + NOTICE_PAD_Y, NOTICE_SCALE,
		( notice.clickable && UI_UpdateNoticeHover() ) ? lit : ( state == NOTICE_FAILED ? failed : plain ),
		text, 0.0f, 0, ITEM_TEXTSTYLE_SHADOWED, NOTICE_FONT );
	trap->R_SetColor( NULL );
}

// A key or a mouse button went down or up. qtrue: it was a click on the line, and the menu under it does not get it.
qboolean UI_UpdateNoticeKey( int key, qboolean down ) {
	if ( !notice.shown || !notice.clickable || key != A_MOUSE1 || !UI_UpdateNoticeHover() )
		return qfalse;
	if ( down ) {
		trap->S_StartLocalSound( uiInfo.uiDC.Assets.menuEnterSound, CHAN_LOCAL_SOUND );
		trap->Cmd_ExecuteText( EXEC_APPEND, "update\n" );
	}
	return qtrue;
}
