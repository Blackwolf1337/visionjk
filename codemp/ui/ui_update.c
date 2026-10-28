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


// ui_update.c -- the line on the main menu that says a newer version of the game is out, and the pill "What's new"
// beside it that opens the changelog (ui_changelog.c). Not from JA+, see docs/japlus/update.md.
//
// The updater is in the engine (client/cl_update.cpp). It says what it has to say in two cvars: v_updateState
// (0 nothing, 1 a newer version is out, 2 its files are coming in, 3 that failed, 4 the game is about to start
// again) and v_updateText, the line itself. This file draws that line while the main menu is up, and a click
// on it is the command "update". The pill says "What's new", and "What's new in <version>" while a newer version
// is out; after the start that finished an update (v_updated) it opens the changelog once by itself.
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
#define NOTICE_GAP		8.0f	// between the line and the pill
#define NOTICE_MENU		"changelog_popmenu"

// v_updateState of cl_update.cpp
#define NOTICE_AVAILABLE	1
#define NOTICE_FAILED		3

typedef struct {
	qboolean	shown;			// it was drawn in this frame,
	qboolean	clickable;		//	and a click on it does something
	float		x, y, w, h;
} noticeBox_t;

static noticeBox_t	notice, news;
static qboolean		newsOpened;		// the changelog was opened after an update, once a run is enough

static qboolean UI_UpdateNoticeHover( const noticeBox_t *box ) {
	return ( uiInfo.uiDC.cursorx >= box->x && uiInfo.uiDC.cursorx <= box->x + box->w
		&& uiInfo.uiDC.cursory >= box->y && uiInfo.uiDC.cursory <= box->y + box->h ) ? qtrue : qfalse;
}

// the width a box needs for its text
static float UI_UpdateNoticeWidth( const char *text ) {
	if ( UI_NewLook() )
		return (float)uiInfo.uiDC.textWidth( text, 0.62f, SKIN_FONT_SANS ) + 28.0f;
	return (float)uiInfo.uiDC.textWidth( text, NOTICE_SCALE, NOTICE_FONT ) + NOTICE_PAD_X * 2.0f;
}

// One box at x. With the menus of the new look: a pill of its glass under its two panels, with its letters, lit
// while the mouse is on it. With the old ones: a dark box with a rim.
static void UI_UpdateNoticeBox( noticeBox_t *box, float x, const char *text, qboolean failed ) {
	qboolean over;

	box->shown = qtrue;
	box->x = x;
	box->y = NOTICE_Y;
	box->w = UI_UpdateNoticeWidth( text );
	over = (qboolean)( box->clickable && UI_UpdateNoticeHover( box ) );

	if ( UI_NewLook() ) {
		const vec4_t	glass = { 0.035f, 0.07f, 0.12f, 0.86f }, rim = { 0.43f, 0.83f, 1.0f, 0.45f };
		const vec4_t	litGlass = { 0.1f, 0.2f, 0.3f, 0.92f }, litRim = { 0.43f, 0.83f, 1.0f, 0.95f };
		vec4_t			bright = { 0.86f, 0.92f, 0.97f, 1.0f }, lit = { 1.0f, 1.0f, 1.0f, 1.0f }, amber = { 1.0f, 0.72f, 0.3f, 1.0f };

		box->h = 18.0f;
		Skin_Panel( box->x, box->y, box->w, box->h, box->h * 0.5f, over ? litGlass : glass, over ? litRim : rim );
		uiInfo.uiDC.drawText( box->x + 14.0f, box->y + 3.4f, 0.62f, failed ? amber : ( over ? lit : bright ), text, 0.0f, 0, 0,
			SKIN_FONT_SANS );
	} else {
		vec4_t	back = { 0.0f, 0.0f, 0.0f, 0.8f }, edge = { 0.65f, 0.65f, 1.0f, 1.0f };
		vec4_t	plain = { 0.65f, 0.65f, 1.0f, 1.0f }, lit = { 1.0f, 1.0f, 1.0f, 1.0f }, orange = { 1.0f, 0.6f, 0.2f, 1.0f };

		box->h = (float)uiInfo.uiDC.textHeight( text, NOTICE_SCALE, NOTICE_FONT ) + NOTICE_PAD_Y * 2.0f;
		uiInfo.uiDC.fillRect( box->x, box->y, box->w, box->h, back );
		uiInfo.uiDC.drawRect( box->x, box->y, box->w, box->h, 1.0f, edge );
		uiInfo.uiDC.drawText( box->x + NOTICE_PAD_X, box->y + NOTICE_PAD_Y, NOTICE_SCALE, over ? lit : ( failed ? orange : plain ),
			text, 0.0f, 0, ITEM_TEXTSTYLE_SHADOWED, NOTICE_FONT );
	}
	trap->R_SetColor( NULL );
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
	char			text[128], what[96], updated[32];
	float			width, x;

	notice.shown = news.shown = qfalse;
	if ( !menu || !menu->window.name || Q_stricmp( menu->window.name, "main" ) )
		return;

	// after an update: what it brought, once
	trap->Cvar_VariableStringBuffer( "v_updated", updated, sizeof( updated ) );
	if ( updated[0] && !newsOpened && UI_ChangelogCount() ) {
		newsOpened = qtrue;
		Menus_OpenByName( NOTICE_MENU );
		return;
	}

	text[0] = '\0';
	if ( state > 0 )
		trap->Cvar_VariableStringBuffer( "v_updateText", text, sizeof( text ) );
	what[0] = '\0';
	if ( UI_ChangelogCount() ) {
		if ( state == NOTICE_AVAILABLE )
			Com_sprintf( what, sizeof( what ), "%s %s", Vision_Text( "What's new in" ), UI_ChangelogNewest() );
		else
			Q_strncpyz( what, Vision_Text( "What's new" ), sizeof( what ) );
	}
	if ( !text[0] && !what[0] )
		return;

	notice.clickable = ( state == NOTICE_AVAILABLE || state == NOTICE_FAILED ) ? qtrue : qfalse;
	news.clickable = qtrue;
	width = ( text[0] ? UI_UpdateNoticeWidth( text ) : 0.0f ) + ( what[0] ? UI_UpdateNoticeWidth( what ) : 0.0f )
		+ ( text[0] && what[0] ? NOTICE_GAP : 0.0f );
	x = ( SCREEN_WIDTH - width ) * 0.5f;
	if ( text[0] ) {
		UI_UpdateNoticeBox( &notice, x, text, ( state == NOTICE_FAILED ) ? qtrue : qfalse );
		x += notice.w + NOTICE_GAP;
	}
	if ( what[0] )
		UI_UpdateNoticeBox( &news, x, what, qfalse );
}

// A key or a mouse button went down or up. qtrue: it was a click on the line or the pill, and the menu under it
// does not get it.
qboolean UI_UpdateNoticeKey( int key, qboolean down ) {
	if ( key != A_MOUSE1 )
		return qfalse;
	if ( notice.shown && notice.clickable && UI_UpdateNoticeHover( &notice ) ) {
		if ( down ) {
			trap->S_StartLocalSound( uiInfo.uiDC.Assets.menuEnterSound, CHAN_LOCAL_SOUND );
			trap->Cmd_ExecuteText( EXEC_APPEND, "update\n" );
		}
		return qtrue;
	}
	if ( news.shown && UI_UpdateNoticeHover( &news ) ) {
		if ( down ) {
			trap->S_StartLocalSound( uiInfo.uiDC.Assets.menuEnterSound, CHAN_LOCAL_SOUND );
			Menus_OpenByName( NOTICE_MENU );
		}
		return qtrue;
	}
	return qfalse;
}
