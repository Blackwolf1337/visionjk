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

// ui_shared_local.h -- what ui_shared.c and the files split off it (ui_canvas.c, ui_pages.c, ui_changes.c,
// ui_dump.c) share and nobody else sees. Compiled into the ui and the cgame.

#pragma once

#ifdef _CGAME
	#include "cgame/cg_local.h"
#elif UI_BUILD
	#include "ui/ui_local.h"
#endif

#include "ui_shared.h"
#include "ui_skin.h"

// ui_shared.c
extern displayContextDef_t *DC;
extern menuDef_t Menus[MAX_MENUS];
extern int menuCount;
void Item_RunScript( itemDef_t *item, const char *s );
void Item_UpdatePosition( itemDef_t *item );
void Menu_UpdatePosition( menuDef_t *menu );
qboolean Item_EnableShowViaCvar( itemDef_t *item, int flag );
void Item_MouseLeave( itemDef_t *item );
void Item_SetMouseOver( itemDef_t *item, qboolean focus );
qboolean Item_Skinned( const itemDef_t *item );
qboolean Item_Slider_Units( const itemDef_t *item );
void Row_Text( float x, float y, float h, float room, int align, float scale, vec4_t color, const char *text, int font );
qboolean Item_Bind_HasKey( const itemDef_t *item, const char *search );

// ui_canvas.c
void Menu_CanvasKeep( menuDef_t *menu );
void Menu_CanvasCheck( menuDef_t *menu );

// ui_pages.c
// a card of a list in a menu with a canvas (tools/menus/canvas.py CARD_*): its line ends 56 pixels under its top, 10
// to its first row, 10 under its last
#define CARD_LINE_PX	55.0f		// where the line under the head of a card is
#define CARD_HEAD_PX	66.0f		// from the top of a card to its first row
#define CARD_BOTTOM_PX	10.0f		// from its last row to its bottom
#define CARD_GAP_PX		24.0f		// between two cards of a column (tools/menus/canvas.py CARD_GAP)
#define ROW_PAD			8.0f		// from the edge of a row to its name and to its widget
#define ROW_WIDGET		0.31f		// the part of the width of a row its widget has
#define ROW_WIDGET_WIDE	0.56f		// "row 3": a line to type a name in
#define ROW_NUMBER		31.0f		// in front of the track of a slider: what it stands at
#define ROW_ARROW		8.0f		// how high the arrows of a choice are
#define ROW_SMALL		0.88f		// the size of the words of a widget, in that of the name of its row (0.76 was hard to
								// read on the menu in the level, 2026-11-02)
#define TAB_PAD			11.0f		// beside the word of the button of a page
#define LIST_GLIDE_MSEC	35.0f		// the list covers most of its way to a new place in about three times this

qboolean Menu_HasList( const menuDef_t *menu );
float Menu_ListStep( const menuDef_t *menu );
void Menu_ListWindow( const menuDef_t *menu, rectDef_t *out );
float Menu_ListMost( const menuDef_t *menu );
qboolean Item_OffPage( const itemDef_t *item );
qboolean Item_Away( const itemDef_t *item );
void Menu_ListPlace( menuDef_t *menu );
void Menu_ScrollTo( menuDef_t *menu, float to, qboolean jump );
void Menu_ScrollBy( menuDef_t *menu, float rows );
void Menu_ScrollIntoView( menuDef_t *menu, const itemDef_t *item );
qboolean Item_PageScroll_Thumb( const itemDef_t *item, float *start, float *part );
qboolean Item_IsRow( const itemDef_t *item );
qboolean Item_Off( itemDef_t *item );
qboolean Item_GreyRow( itemDef_t *item );
float Item_RowSize( const itemDef_t *item, float pixels, float units );
void Item_Row_Widget( const itemDef_t *item, rectDef_t *out );
qboolean Item_Bind_Slots( const itemDef_t *item );
void Item_Bind_Slot( const itemDef_t *item, int slot, rectDef_t *out );
float Item_Slider_Track( itemDef_t *item, float *width );
void Text_Caps( char *text );
void Text_Word( const char *text, char *out, int size, qboolean caps, qboolean label );
qboolean Menu_FoldIs( const char *folds, int fold );
void Menu_ListCheck( menuDef_t *menu );
void Menu_ToggleFold( menuDef_t *menu, int fold );
void Menu_ListCards( menuDef_t *menu );
void Menu_StackFrame( menuDef_t *menu );

// ui_changes.c
#define MENU_CHANGES		384
#define MENU_CHANGE_VALUE	128

void Menu_ChangesStart( menuDef_t *menu );
qboolean Item_IsSetting( const itemDef_t *item );
qboolean Value_Same( const char *a, const char *b );
int Value_Bit( const itemDef_t *item, const char *value );
qboolean Item_ValueSame( const itemDef_t *item, const char *a, const char *b );
qboolean Item_YesNo_On( const itemDef_t *item );
void Item_YesNo_Set( const itemDef_t *item, qboolean on );
qboolean Cvar_NeedsRestart( const char *cvar );
void Item_ValueText( const itemDef_t *item, const char *value, char *out, int size );
void Menu_ChangesFrame( menuDef_t *menu );
int Menu_ChangesOnPage( const menuDef_t *menu, int page );
void Menu_ChangesClose( menuDef_t *menu );
float Item_Pixels( const itemDef_t *item, float pixels );
void Item_Changes_Paint( itemDef_t *item );
qboolean Item_Changes_HandleKey( itemDef_t *item, int key );
