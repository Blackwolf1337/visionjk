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

// ui_canvas.c -- the canvas of a menu: a page drawn in the pixels of a picture, put on any window (part of ui_shared.c,
// compiled into the ui and the cgame)

#include "ui_shared_local.h"

/*
===============================================================================

The canvas of a menu (VisionJK, "canvas <w> <h>")

A menu file with a canvas is written in the pixels of a picture of that size, the way a design is drawn, and
not in the 640 x 480 units of the game. The picture is put on the screen as large as it fits with the same
scale across and down, in the middle: on a wide window it fills the height, on a window of 4:3 it fills the
width. So a page keeps the proportions it was drawn with on every window, also where the 2D of the game is
stretched (an engine without v_2dBias), and on the panel in the level, where it is put on the 640 x 480 of the panel.

What the file gave is kept in item->canvas and the rectangles, the sizes of text and the rows of lists are
worked out from it again whenever the window or the panel is not the one the layout was made for.

===============================================================================
*/

// the menu was laid out for: the units beside the grid across and down, and the pixels a unit has
static void Menu_CanvasNow( float out[4] ) {
	out[0] = DC->wide;
	out[1] = DC->tall;
	out[2] = DC->xscale;
	out[3] = DC->yscale;
}

void Menu_CanvasKeep( menuDef_t *menu ) {
	int i, j;

	menu->canvasRect = menu->window.rect;
	menu->canvasScroll = menu->scrollRect;
	menu->canvasScrollStep = menu->scrollStep;
	menu->canvasDescX = menu->descX;
	menu->canvasDescY = menu->descY;
	menu->canvasDescScale = menu->descScale;
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t *item = menu->items[i];

		item->canvas.rect = item->window.rectClient;
		item->canvas.textscale = item->textscale;
		item->canvas.textalignx = item->textalignx;
		item->canvas.textaligny = item->textaligny;
		item->canvas.text2alignx = item->text2alignx;
		item->canvas.text2aligny = item->text2aligny;
		item->canvas.textRoom = item->textRoom;
		item->canvas.radius = item->window.radius;
		item->canvas.shadow = item->window.shadow;
		item->canvas.borderSize = item->window.borderSize;
		if ( item->type == ITEM_TYPE_LISTBOX && item->typeData.listbox ) {
			listBoxDef_t *list = item->typeData.listbox;

			item->canvas.elementWidth = list->elementWidth;
			item->canvas.elementHeight = list->elementHeight;
			for ( j = 0; j < list->numColumns && j < MAX_LB_COLUMNS; j++ ) {
				item->canvas.columnPos[j] = list->columnInfo[j].pos;
				item->canvas.columnWidth[j] = list->columnInfo[j].width;
			}
		}
	}
}

// the units of the grid one pixel of the canvas has, or 1 for a menu without a canvas. The widgets of ui_shared.c
// that have sizes of their own (a row, its arrows, the bar of a list) take them times Menu_Unit(): those sizes were
// made for a canvas of 900 pixels high on a wide window, where a pixel of it is 480 / 900 of a unit.
float Menu_Unit( const menuDef_t *menu ) {
	if ( !menu || menu->canvasW <= 0.0f || menu->canvasUnit <= 0.0f ) {
		return 1.0f;
	}
	return menu->canvasUnit * ( 900.0f / 480.0f );
}

/*
=================
Menu_CanvasBars

A page whose canvas is wider than the screen (4:3, 16:10) has room above and below it: spare is that room under
the canvas, the same above it. The page goes up to the top of the screen, as a page is read from the top; the bar
at the bottom ("wide" items from the bottom of the canvas up, one over the other, and what stands in them) goes down
to its bottom. What reached that bar before (a rail, a pane, the window of a list) grows to reach it again.
=================
*/
static void Menu_CanvasBars( menuDef_t *menu, float spare ) {
	float		footTop = menu->canvasH;
	qboolean	grew;
	int			i;

	if ( spare < 0.5f ) {
		return;
	}
	do {
		grew = qfalse;
		for ( i = 0; i < menu->itemCount; i++ ) {
			const rectDef_t *r = &menu->items[i]->canvas.rect;

			if ( menu->items[i]->window.wide && r->h >= 2.0f && fabsf( r->y + r->h - footTop ) < 0.5f && r->y < footTop - 0.5f ) {
				footTop = r->y;
				grew = qtrue;
			}
		}
	} while ( grew );
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t		*item = menu->items[i];
		const rectDef_t	*r = &item->canvas.rect;

		if ( footTop < menu->canvasH && r->y >= footTop - 0.5f ) {
			item->window.rectClient.y += spare;
			continue;
		}
		item->window.rectClient.y -= spare;
		if ( footTop < menu->canvasH && fabsf( r->y + r->h - footTop ) < 0.5f ) {
			item->window.rectClient.h += 2.0f * spare;
		}
	}
	if ( menu->canvasScroll.w > 0.0f ) {
		menu->scrollRect.y -= spare;
		if ( footTop < menu->canvasH && fabsf( menu->canvasScroll.y + menu->canvasScroll.h - footTop ) < 0.5f ) {
			menu->scrollRect.h += 2.0f * spare;
		}
	}
	menu->descY += ( ( footTop < menu->canvasH && menu->canvasDescY >= footTop - 0.5f ) ? spare : -spare );
}

void Menu_CanvasLayout( menuDef_t *menu ) {
	float	now[4], ux, uy, gw, gh, x0, y0, px, cx0, cy0;
	int		i, j;

	if ( !menu || menu->canvasW <= 0.0f || menu->canvasH <= 0.0f ) {
		return;
	}
	Menu_CanvasNow( now );
	gw = SCREEN_WIDTH + 2.0f * now[0];
	gh = SCREEN_HEIGHT + 2.0f * now[1];
	if ( now[0] <= 0.0f && now[1] <= 0.0f && DC->getCVarValue( "v_uiPlane" ) ) {
		// the panel in the level: the units of the grid are square there
		ux = uy = Q_min( gw / menu->canvasW, gh / menu->canvasH );
	} else {
		// on the screen: the same number of pixels across and down for a pixel of the canvas
		px = Q_min( gw * now[2] / menu->canvasW, gh * now[3] / menu->canvasH );
		ux = now[2] > 0.0f ? px / now[2] : 1.0f;
		uy = now[3] > 0.0f ? px / now[3] : 1.0f;
	}
	x0 = -now[0] + ( gw - menu->canvasW * ux ) * 0.5f;
	y0 = -now[1] + ( gh - menu->canvasH * uy ) * 0.5f;
	menu->canvasUnit = uy;
	memcpy( menu->canvasFor, now, sizeof( menu->canvasFor ) );
	cx0 = x0;
	cy0 = y0;

	// a whole page is the canvas; a window keeps its place on the canvas
	if ( menu->fullScreen ) {
		menu->window.rect.x = x0;
		menu->window.rect.y = y0;
		menu->window.rect.w = menu->canvasW * ux;
		menu->window.rect.h = menu->canvasH * uy;
	} else {
		menu->window.rect.x = x0 + menu->canvasRect.x * ux;
		menu->window.rect.y = y0 + menu->canvasRect.y * uy;
		menu->window.rect.w = menu->canvasRect.w * ux;
		menu->window.rect.h = menu->canvasRect.h * uy;
		x0 = menu->window.rect.x;
		y0 = menu->window.rect.y;
	}
	if ( menu->canvasScroll.w > 0.0f ) {
		// (from the corner of the menu, as the rectangles of its items)
		menu->scrollRect.x = menu->canvasScroll.x * ux;
		menu->scrollRect.y = menu->canvasScroll.y * uy;
		menu->scrollRect.w = menu->canvasScroll.w * ux;
		menu->scrollRect.h = menu->canvasScroll.h * uy;
	}
	menu->scrollStep = menu->canvasScrollStep * uy;
	// (the line about the item in focus is placed on the screen, from the corner of the canvas)
	menu->descX = cx0 + menu->canvasDescX * ux;
	menu->descY = cy0 + menu->canvasDescY * uy;
	menu->descScale = menu->canvasDescScale * uy;

	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t *item = menu->items[i];

		item->window.rectClient.x = item->canvas.rect.x * ux;
		item->window.rectClient.y = item->canvas.rect.y * uy;
		item->window.rectClient.w = item->canvas.rect.w * ux;
		item->window.rectClient.h = item->canvas.rect.h * uy;
		item->textscale = item->canvas.textscale * uy;
		if ( item->skinScale > 0.0f ) {
			item->skinScale = item->textscale;
		}
		item->textalignx = item->canvas.textalignx * ux;
		item->textaligny = item->canvas.textaligny * uy;
		item->text2alignx = item->canvas.text2alignx * ux;
		item->text2aligny = item->canvas.text2aligny * uy;
		item->textRoom = item->canvas.textRoom * ux;
		item->window.radius = item->canvas.radius * uy;
		item->window.shadow = item->canvas.shadow * uy;
		item->window.borderSize = item->canvas.borderSize * uy;
		if ( item->type == ITEM_TYPE_LISTBOX && item->typeData.listbox ) {
			listBoxDef_t *list = item->typeData.listbox;

			list->elementWidth = item->canvas.elementWidth * ux;
			list->elementHeight = item->canvas.elementHeight * uy;
			for ( j = 0; j < list->numColumns && j < MAX_LB_COLUMNS; j++ ) {
				list->columnInfo[j].pos = (int)( item->canvas.columnPos[j] * ux + 0.5f );
				list->columnInfo[j].width = (int)( item->canvas.columnWidth[j] * ux + 0.5f );
			}
		}
	}
	if ( menu->fullScreen ) {
		Menu_CanvasBars( menu, -now[1] + gh - ( cy0 + menu->canvasH * uy ) );
	}
	// what covers the whole height of the canvas (the veil under a window) covers the whole height of the screen
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t		*item = menu->items[i];
		const float		top = ( menu->fullScreen ? 0.0f : menu->canvasRect.y ) + item->canvas.rect.y;

		if ( item->window.wide && top <= 0.5f && top + item->canvas.rect.h >= menu->canvasH - 0.5f ) {
			item->window.rectClient.y = -now[1] - menu->window.rect.y;
			item->window.rectClient.h = gh;
		}
	}
	// (the rows of a list are where the menu file put them again: the list is laid out anew)
	menu->listKnown = qfalse;
	menu->listSearch[0] = '\1';
	Menu_UpdatePosition( menu );
}

// lays a menu with a canvas out again when the window, its 2D or the panel in the level is not what it was made for
void Menu_CanvasCheck( menuDef_t *menu ) {
	float now[4];

	if ( !menu || menu->canvasW <= 0.0f ) {
		return;
	}
	Menu_CanvasNow( now );
	if ( memcmp( now, menu->canvasFor, sizeof( now ) ) ) {
		Menu_CanvasLayout( menu );
	}
}
