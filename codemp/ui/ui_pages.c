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

// ui_pages.c -- pages, lists and rows of the options screens of the new look, the cards of a list and the stacks of a
// menu (part of ui_shared.c, compiled into the ui and the cgame)

#include "ui_shared_local.h"

/*
===============================================================================

Pages, lists and rows (VisionJK: the options screens of the new look, docs/japlus/ui.md)

A menu file can put its items on pages ("page <n>") that buttons open ("tab <n>"), the settings of a page into a
list ("scroll 1") that may be longer than its window ("scrollRect" of the menu), and have a setting drawn as a
row of that list ("row 1"): its name at the left, its widget at the right. The wheel, the bar beside the list
(ITEM_TYPE_PAGESCROLL) and the keys move the list. A menu file without these words has none of it.

===============================================================================
*/

qboolean Menu_HasList( const menuDef_t *menu ) {
	return (qboolean)( menu->scrollRect.h > 0.0f && menu->page >= 0 && menu->page < MENU_PAGES );
}

float Menu_ListStep( const menuDef_t *menu ) {
	return menu->scrollStep > 0.0f ? menu->scrollStep : 20.0f;
}

// the window of the list of a menu, on the screen
void Menu_ListWindow( const menuDef_t *menu, rectDef_t *out ) {
	*out = menu->scrollRect;
	out->x += menu->window.rect.x;
	out->y += menu->window.rect.y;
	if ( menu->window.border != 0 ) {
		out->x += menu->window.borderSize;
		out->y += menu->window.borderSize;
	}
}

// Is this item on a page that is not open. A row the list has folded away or that is no match of the search is
// on none (Menu_ListLayout), and while the search is on, the rows of every page are in its list.

qboolean Item_OffPage( const itemDef_t *item ) {
	const menuDef_t *menu = (const menuDef_t *)item->parent;

	if ( !menu ) {
		return qfalse;
	}
	if ( item->listOut ) {
		return qtrue;
	}
	if ( item->scroll && menu->searching ) {
		return qfalse;
	}
	if ( item->page == PAGE_SEARCH ) {
		// "page 15", the last one: only while a search is on (its title)
		return (qboolean)!menu->searching;
	}
	return (qboolean)( item->page && item->page != menu->page );
}

// how long the list of the open page is: from the top of its window to the end of its last row
static float Menu_ListLength( const menuDef_t *menu ) {
	float	end = 0.0f;
	int		i;

	for ( i = 0; i < menu->itemCount; i++ ) {
		const itemDef_t *item = menu->items[i];

		if ( item->scroll && !Item_OffPage( item ) && ( item->window.flags & WINDOW_VISIBLE ) ) {
			end = Q_max( end, item->window.rectClient.y + item->window.rectClient.h - menu->scrollRect.y );
		}
	}
	return end;
}

// how far the list can be moved up: until its end stands at the end of its window, in whole rows
float Menu_ListMost( const menuDef_t *menu ) {
	const float over = Menu_ListLength( menu ) - menu->scrollRect.h;

	return over <= 0.5f ? 0.0f : ceilf( over / Menu_ListStep( menu ) - 0.01f ) * Menu_ListStep( menu );
}

// Is this item not there right now: on a page that is not open, a row of the list outside of its window, or the
// bar of a list that needs none
qboolean Item_Away( const itemDef_t *item ) {
	const menuDef_t *menu = (const menuDef_t *)item->parent;

	if ( !menu || ( !item->page && !item->scroll && item->type != ITEM_TYPE_PAGESCROLL ) ) {
		return qfalse;
	}
	if ( Item_OffPage( item ) ) {
		return qtrue;
	}
	if ( !Menu_HasList( menu ) ) {
		return qfalse;
	}
	if ( item->type == ITEM_TYPE_PAGESCROLL ) {
		return (qboolean)( Menu_ListMost( menu ) <= 0.0f );
	}
	if ( item->scroll ) {
		rectDef_t	list;
		float		top = item->window.rect.y;

		Menu_ListWindow( menu, &list );
		if ( item->fold && menu->canvasW > 0.0f ) {
			// (a heading under another card has the gap above its card in it, see Menu_ListCards: what shows of
			// it begins at the card)
			top = Q_max( top, item->window.rect.y + item->window.rect.h - CARD_HEAD_PX * menu->canvasUnit );
		}
		return (qboolean)( top < list.y - 0.5f || item->window.rect.y + item->window.rect.h > list.y + list.h + 0.5f );
	}
	return qfalse;
}

// the rows of the list to where the list is shown (Item_SetScreenCoords)
void Menu_ListPlace( menuDef_t *menu ) {
	int i;

	for ( i = 0; i < menu->itemCount; i++ ) {
		if ( menu->items[i]->scroll ) {
			Item_UpdatePosition( menu->items[i] );
		}
	}
}

// Moves the list of the open page up by `to` units from its start, no further than it is long. jump: at once,
// else it glides there (Menu_ScrollFrame).
void Menu_ScrollTo( menuDef_t *menu, float to, qboolean jump ) {
	if ( !Menu_HasList( menu ) ) {
		return;
	}
	to = Com_Clamp( 0.0f, Menu_ListMost( menu ), to );
	menu->scrollTo[menu->page] = to;
	if ( jump && menu->scrollShown != to ) {
		menu->scrollShown = to;
		Menu_ListPlace( menu );
	}
}

void Menu_ScrollBy( menuDef_t *menu, float rows ) {
	if ( Menu_HasList( menu ) ) {
		Menu_ScrollTo( menu, menu->scrollTo[menu->page] + rows * Menu_ListStep( menu ), qfalse );
	}
}

// Brings a row into the window of its list, for the keys that walk through all rows. Upwards one row more
// comes with it: the heading of a part stands above its first row.
void Menu_ScrollIntoView( menuDef_t *menu, const itemDef_t *item ) {
	const float	step = Menu_ListStep( menu );
	const float	top = item->window.rectClient.y - menu->scrollRect.y, bottom = top + item->window.rectClient.h;
	float		to;

	if ( !Menu_HasList( menu ) ) {
		return;
	}
	to = menu->scrollTo[menu->page];
	if ( top < to ) {
		to = ( floorf( top / step + 0.01f ) - 1.0f ) * step;
	} else if ( bottom > to + menu->scrollRect.h ) {
		to = ceilf( ( bottom - menu->scrollRect.h ) / step - 0.01f ) * step;
	}
	Menu_ScrollTo( menu, to, qtrue );
}

// the bar of the list of a page, where it is and how long its thumb is. False: the list needs none.
qboolean Item_PageScroll_Thumb( const itemDef_t *item, float *start, float *part ) {
	const menuDef_t	*menu = (const menuDef_t *)item->parent;
	const float		most = Menu_ListMost( menu );

	if ( !Menu_HasList( menu ) || most <= 0.0f ) {
		return qfalse;
	}
	*part = menu->scrollRect.h / ( menu->scrollRect.h + most );
	*start = ( menu->scrollShown / most ) * ( 1.0f - *part );
	return qtrue;
}


// Opens a page of a menu. What had the mouse or the focus on the page that goes loses it.
// page 0: the one of the search (Menu_ListLayout), which no item names
static void Menu_OpenPage( menuDef_t *menu, int page ) {
	int i;

	menu->page = page;
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t *item = menu->items[i];

		if ( !Item_OffPage( item ) ) {
			continue;
		}
		if ( item->window.flags & WINDOW_MOUSEOVER ) {
			Item_MouseLeave( item );
			Item_SetMouseOver( item, qfalse );
		}
		if ( item->window.flags & WINDOW_HASFOCUS ) {
			item->window.flags &= ~WINDOW_HASFOCUS;
			if ( item->leaveFocus ) {
				Item_RunScript( item, item->leaveFocus );
			}
		}
	}
	// (the list of this page is where it was left)
	menu->scrollShown = -1.0f;
	Menu_ScrollTo( menu, menu->scrollTo[page], qtrue );
	Menu_HandleMouseMove( menu, DC->cursorx, DC->cursory );
}

void Menu_SetPage( menuDef_t *menu, int page ) {
	if ( !menu || page <= 0 || page >= MENU_PAGES || menu->page == page ) {
		return;
	}
	// (a button of a page ends the search, Item_Action)
	Menu_OpenPage( menu, page );
}

qboolean Item_IsRow( const itemDef_t *item ) {
	return (qboolean)( item->row && Item_Skinned( item ) );
}

// can this item not be used right now
qboolean Item_Off( itemDef_t *item ) {
	return (qboolean)( item->disabled
		|| ( ( item->cvarFlags & ( CVAR_ENABLE | CVAR_DISABLE ) ) && !Item_EnableShowViaCvar( item, CVAR_ENABLE ) ) );
}

// a row of a menu with a canvas that its cvar test greys out still takes the focus, so the pane says what it needs
// (Item_Desc_Paint); its keys and clicks stay off (Item_Off in Menu_HandleKey)
qboolean Item_GreyRow( itemDef_t *item ) {
	const menuDef_t *menu = (const menuDef_t *)item->parent;

	return (qboolean)( menu && menu->canvasW > 0.0f && Item_IsRow( item ) && !item->disabled );
}

// where the widget of a row is: at its right end, on the middle of its height
// a size of a row: in a menu with a canvas the pixels of the design (Setup of the redesign: rows 52 high, widgets
// 220 x 40, 12 from the edge), else the units the rows of the 640 x 480 menus had
float Item_RowSize( const itemDef_t *item, float pixels, float units ) {
	const menuDef_t *menu = (const menuDef_t *)item->parent;

	return ( menu && menu->canvasW > 0.0f ) ? pixels * menu->canvasUnit : units;
}

#define BIND_SLOT_W		190.0f		// a key of a row of keys in a menu with a canvas, in pixels of the canvas
#define BIND_SLOT_GAP	16.0f

void Item_Row_Widget( const itemDef_t *item, rectDef_t *out ) {
	const rectDef_t *r = &item->window.rect;
	const menuDef_t *menu = (const menuDef_t *)item->parent;

	if ( menu && menu->canvasW > 0.0f && ( !item->text || !item->text[0] ) ) {
		// (a row without a name is its widget alone: the switch between the settings that matter and all of them)
		*out = *r;
		return;
	}
	if ( menu && menu->canvasW > 0.0f && item->type == ITEM_TYPE_BIND ) {
		// (two keys side by side, KEY and SECOND KEY, see Item_Bind_Slot)
		out->w = floorf( Q_min( ( 2.0f * BIND_SLOT_W + BIND_SLOT_GAP ) * menu->canvasUnit, r->w * 0.6f ) );
		out->h = Q_min( 38.0f * menu->canvasUnit, r->h - 4.0f * menu->canvasUnit );
	} else if ( menu && menu->canvasW > 0.0f ) {
		// (220 x 40 in a row 52 high as the board, in proportion in rows of other heights: the panels in a game)
		out->h = Q_min( r->h * ( 40.0f / 52.0f ), r->h - 4.0f * menu->canvasUnit );
		out->w = floorf( item->row == 3 ? r->w * ROW_WIDGET_WIDE : Q_min( 220.0f / 40.0f * out->h, r->w * 0.5f ) );
	} else {
		out->w = floorf( r->w * ( item->row == 3 ? ROW_WIDGET_WIDE : ROW_WIDGET ) );
		out->h = Q_min( 16.0f, r->h - 6.0f );
	}
	out->x = r->x + r->w - Item_RowSize( item, 12.0f, ROW_PAD ) - out->w;
	// (on the middle between the line over the row and its own line, a unit over its end, as its name: Row_Name)
	out->y = r->y + ( r->h - 1.0f - out->h ) * 0.5f;
}

// A row of keys in a menu with a canvas shows the two keys of its command in two fields, KEY and SECOND KEY, and
// each of them is set on its own.
qboolean Item_Bind_Slots( const itemDef_t *item ) {
	const menuDef_t *menu = (const menuDef_t *)item->parent;

	return (qboolean)( item->type == ITEM_TYPE_BIND && menu && menu->canvasW > 0.0f && item->text && item->text[0] );
}

// the field of the first (0) or the second key (1) of such a row
void Item_Bind_Slot( const itemDef_t *item, int slot, rectDef_t *out ) {
	const menuDef_t	*menu = (const menuDef_t *)item->parent;
	const float		gap = BIND_SLOT_GAP * menu->canvasUnit;
	rectDef_t		widget;

	Item_Row_Widget( item, &widget );
	out->w = ( widget.w - gap ) * 0.5f;
	out->h = widget.h;
	out->x = widget.x + slot * ( out->w + gap );
	out->y = widget.y;
}

// where the track of a slider starts and how long it is: behind its label, or in the widget of a row
float Item_Slider_Track( itemDef_t *item, float *width ) {
	if ( Item_IsRow( item ) ) {
		rectDef_t widget;

		// (the track first, what it stands at after it, as in the other widgets the value is at the right)
		Item_Row_Widget( item, &widget );
		*width = widget.w - Item_RowSize( item, 70.0f, ROW_NUMBER + 8.0f );
		return widget.x + Item_RowSize( item, 8.0f, 4.5f );
	}
	*width = SLIDER_WIDTH;
	return item->text ? item->textRect.x + item->textRect.w + 8 : item->window.rect.x;
}

// A text in capital letters, in place. Only where that is safe: text in UTF-8, or without a byte above 127 (a
// byte of a letter of two bytes in another encoding may look like a small letter).
void Text_Caps( char *text ) {
	const char	*c;
	byte		*u;

	for ( c = text; *c && !( *c & 0x80 ); c++ )
		;
	if ( *c && !DC->getCVarValue( "v_utf8Text" ) ) {
		return;
	}
	Q_strupr( text );
	// (And the small letters with marks, a with grave to thorn without the division sign: the capital is 32
	// lower. In UTF-8 they are two bytes, 0xC3 and the letter. The texts of the game itself come in
	// Windows-1252, where they are one byte, which is no start of a UTF-8 letter unless bytes of its inside
	// follow.)
	for ( u = (byte *)text; *u; u++ ) {
		if ( u[0] == 0xC3 && u[1] >= 0xA0 && u[1] <= 0xBE && u[1] != 0xB7 ) {
			u[1] -= 0x20;
			u++;
		} else if ( u[0] >= 0xE0 && u[0] <= 0xFE && u[0] != 0xF7 && ( u[1] & 0xC0 ) != 0x80 ) {
			u[0] -= 0x20;
		}
	}
}

// A text of a menu file as it is drawn, in a buffer: a reference into the texts of the game ("@MENUS_YES") is
// looked up. label: the colon a name of a setting ends with in the old menus goes.
void Text_Word( const char *text, char *out, int size, qboolean caps, qboolean label ) {
	if ( !text ) {
		text = "";
	}
	if ( *text == '@' ) {
		trap->SE_GetStringTextString( &text[1], out, size );
	} else {
		Q_strncpyz( out, text, size );
	}
	if ( label ) {
		int len = (int)strlen( out );

		// (with the space some languages set in front of it, which may be one that does not break)
		while ( len > 0 && ( out[len - 1] == ':' || out[len - 1] == ' ' || (byte)out[len - 1] == 0xA0 ) ) {
			if ( (byte)out[len - 1] == 0xA0 && len > 1 && (byte)out[len - 2] == 0xC2 ) {
				len--;
			}
			out[--len] = '\0';
		}
	}
	if ( caps ) {
		Text_Caps( out );
	}
}

/*
=================
Menu_ListLayout

The lists of a menu with "searchCvar" or "foldCvar", laid out anew. A list is made of lines: the items the menu
file put at the same height of the same page (a row, or a heading with its line). The lines after a heading, up
to the next one, are its own. Folded, they are out of the list. While a search is on, a row is in the list when
its name or its description has the words, a heading when one of its rows is, and the lines of all pages make
one list on page 0. The lines that stay move up under each other, a step of the list each.
=================
*/
#define LIST_SEARCH_MIN		2		// letters a search needs before it begins

qboolean Menu_FoldIs( const char *folds, int fold ) {
	const char *p = folds;

	while ( *p ) {
		while ( *p == ' ' ) {
			p++;
		}
		if ( *p && atoi( p ) == fold ) {
			return qtrue;
		}
		while ( *p && *p != ' ' ) {
			p++;
		}
	}
	return qfalse;
}


// the name of a row or its description has the words, as the player reads them; or a row of keys has that key
static qboolean Item_ListMatch( const itemDef_t *item, const char *search ) {
	char text[1024];

	if ( !item->row ) {
		return qfalse;
	}
	Text_Word( item->text, text, sizeof( text ), qfalse, qtrue );
	if ( Q_stristr( text, search ) ) {
		return qtrue;
	}
	if ( item->descText ) {
		Text_Word( item->descText, text, sizeof( text ), qfalse, qfalse );
		if ( Q_stristr( text, search ) ) {
			return qtrue;
		}
	}
	return Item_Bind_HasKey( item, search );
}

#define LIST_COLUMNS	4		// "column <n>": the columns of the list of a page, counted from 0

static void Menu_ListLayout( menuDef_t *menu, const char *search, const char *folds, qboolean newSearch ) {
	static struct {
		int		page;
		int		column;
		float	y;			// where the menu file put it
		int		fold;		// a heading: its number
		int		head;		// the line of the heading it is under, -1: none
		qboolean match;		// the search: one of its rows has the words, or for a heading one of its lines
		qboolean advanced;	// "advanced 1": shown only with all settings (advancedCvar), or by the search
		qboolean in;
		float	bottom;		// a heading: where the last of its lines ends, the end of its card
		float	h;			// in a menu with a canvas: as high as its highest item (else every line is a step high)
		float	gap;		// a heading under another card: the room above its card it has in it (canvas)
		int		dst;		// the column it is shown in: its own, or for a search the one with room (canvas)
	} lines[MAX_MENUITEMS];
	static int	lineOf[MAX_MENUITEMS];
	const float	step = Menu_ListStep( menu );
	const qboolean searching = (qboolean)( strlen( search ) >= LIST_SEARCH_MIN );
	const qboolean all = (qboolean)( !menu->advancedCvar || DC->getCVarValue( menu->advancedCvar ) != 0.0f );
	float		cursor[LIST_COLUMNS] = { 0 };
	float		colX[LIST_COLUMNS];
	qboolean	colSeen[LIST_COLUMNS] = { qfalse };
	int			page[LIST_COLUMNS];
	int			head[LIST_COLUMNS];
	float		firstHead[LIST_COLUMNS];	// the height of the first heading of a column, which has no room above it
	qboolean	started[LIST_COLUMNS];		// a line of the column is shown already
	int			i, count = 0;

	for ( i = 0; i < LIST_COLUMNS; i++ ) {
		page[i] = -1;
		head[i] = -1;
		firstHead[i] = -1.0f;
		started[i] = qfalse;
	}
	menu->listAll = all;
	menu->searchFound = 0;
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t	*item = menu->items[i];
		const int	col = Com_Clampi( 0, LIST_COLUMNS - 1, item->column );

		lineOf[i] = -1;
		if ( !item->scroll ) {
			continue;
		}
		if ( !menu->listKnown ) {
			item->listY = item->window.rectClient.y;
			item->listX = item->window.rectClient.x;
		}
		if ( !colSeen[col] || item->listX < colX[col] ) {
			colX[col] = item->listX;
			colSeen[col] = qtrue;
		}
		// (a line: the items of a step, or in a menu with a canvas the items at the same height, which may be of
		// different heights: rows, the headings of cards)
		if ( !count || item->page != lines[count - 1].page || col != lines[count - 1].column
			|| ( menu->canvasW > 0.0f ? fabsf( item->listY - lines[count - 1].y ) > 0.5f
				: ( item->listY < lines[count - 1].y - 0.5f || item->listY >= lines[count - 1].y + step - 0.5f ) ) ) {
			if ( count && ( item->page != lines[count - 1].page ) ) {
				int k;

				for ( k = 0; k < LIST_COLUMNS; k++ ) {
					head[k] = -1;
					firstHead[k] = -1.0f;
				}
			}
			lines[count].page = item->page;
			lines[count].column = col;
			lines[count].y = item->listY;
			lines[count].fold = 0;
			lines[count].head = head[col];
			lines[count].match = qfalse;
			lines[count].advanced = qfalse;
			lines[count].bottom = 0.0f;
			lines[count].h = 0.0f;
			lines[count].gap = 0.0f;
			count++;
		}
		lines[count - 1].h = Q_max( lines[count - 1].h, item->window.rectClient.h );
		lineOf[i] = count - 1;
		if ( item->fold ) {
			lines[count - 1].fold = item->fold;
			lines[count - 1].head = -1;
			head[col] = count - 1;
			if ( menu->canvasW > 0.0f ) {
				if ( firstHead[col] < 0.0f ) {
					firstHead[col] = item->window.rectClient.h;
				} else {
					lines[count - 1].gap = Q_max( 0.0f, item->window.rectClient.h - firstHead[col] );
				}
			}
		}
		if ( item->advanced ) {
			lines[count - 1].advanced = qtrue;
		}
		if ( searching && Item_ListMatch( item, search ) ) {
			lines[count - 1].match = qtrue;
			menu->searchFound++;
		}
	}
	for ( i = 0; i < count; i++ ) {
		if ( searching && lines[i].match && lines[i].head >= 0 ) {
			lines[lines[i].head].match = qtrue;
		}
	}
	for ( i = 0; i < count; i++ ) {
		if ( searching ) {
			lines[i].in = lines[i].match;
		} else {
			lines[i].in = (qboolean)( ( lines[i].head < 0 || !Menu_FoldIs( folds, lines[lines[i].head].fold ) )
				&& ( all || !lines[i].advanced ) );
		}
	}

	// each page from where its list began, or all of them one after the other for the search; every column of
	// its own from the top
	if ( searching && menu->canvasW > 0.0f && colSeen[0] && colSeen[1] ) {
		// (the cards of a search go into the column with room: both columns start at the top of the list)
		float top = count ? lines[0].y : 0.0f;

		for ( i = 1; i < count; i++ ) {
			top = Q_min( top, lines[i].y );
		}
		for ( i = 0; i < LIST_COLUMNS; i++ ) {
			cursor[i] = top;
			page[i] = 0;
		}
	}
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t	*item = menu->items[i];
		const int	n = lineOf[i];
		int			col;

		if ( n < 0 ) {
			continue;
		}
		col = lines[n].column;
		if ( i == 0 || lineOf[i - 1] != n ) {
			// (a search in a menu with a canvas: each card into the column that is shorter, its lines after it)
			lines[n].dst = col;
			if ( searching && menu->canvasW > 0.0f && colSeen[0] && colSeen[1] && col < 2 && lines[n].in ) {
				if ( lines[n].fold ) {
					lines[n].dst = cursor[1] < cursor[0] ? 1 : 0;
				} else if ( lines[n].head >= 0 ) {
					lines[n].dst = lines[lines[n].head].dst;
				}
			}
		}
		if ( lines[n].dst != col ) {
			const int from = col;

			col = lines[n].dst;
			item->window.rectClient.x = item->listX + colX[col] - colX[from];
		} else {
			item->window.rectClient.x = item->listX;
		}
		if ( page[col] < 0 || ( !searching && lines[n].page != page[col] ) ) {
			cursor[col] = lines[n].y;
			page[col] = lines[n].page;
			started[col] = qfalse;
		}
		if ( ( i == 0 || lineOf[i - 1] != n ) && lines[n].in && !started[col] ) {
			// the first card shown in a column starts at its top: a heading that stood under another card (which
			// is folded away, hidden with the settings that matter, or left out by the search) leaves out the
			// room above it
			if ( lines[n].fold ) {
				cursor[col] -= lines[n].gap;
			}
			started[col] = qtrue;
		} else if ( ( i == 0 || lineOf[i - 1] != n ) && lines[n].in && lines[n].fold && lines[n].gap <= 0.0f && menu->canvasW > 0.0f ) {
			// the other way round: the first heading of a page, which has no room above it, under another card
			// (a search puts the cards of all pages one under the other)
			cursor[col] += ( CARD_GAP_PX + CARD_BOTTOM_PX ) * menu->canvasUnit;
		}
		item->listOut = (qboolean)!lines[n].in;
		item->window.rectClient.y = item->listY - lines[n].y + cursor[col];
		if ( i + 1 >= menu->itemCount || lineOf[i + 1] != n ) {
			if ( lines[n].in ) {
				const int owner = lines[n].fold ? n : lines[n].head;

				cursor[col] += ( menu->canvasW > 0.0f && lines[n].h > 0.0f ) ? lines[n].h : step;
				if ( owner >= 0 ) {
					lines[owner].bottom = cursor[col];
				}
			}
		}
	}
	// where the card of each heading ends (Menu_ListCards)
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t *item = menu->items[i];

		if ( lineOf[i] >= 0 && item->fold ) {
			item->cardBottom = lines[lineOf[i]].bottom;
		}
	}
	menu->listKnown = qtrue;

	if ( searching != menu->searching ) {
		menu->searching = searching;
		if ( searching ) {
			menu->searchPage = menu->page > 0 ? menu->page : 1;
			menu->scrollTo[0] = 0.0f;
			Menu_OpenPage( menu, 0 );
		} else if ( menu->page == 0 ) {
			Menu_OpenPage( menu, menu->searchPage > 0 ? menu->searchPage : 1 );
		}
	} else if ( searching && newSearch ) {
		menu->scrollTo[0] = 0.0f;
	}
	menu->scrollShown = -1.0f;
	Menu_ScrollTo( menu, menu->scrollTo[menu->page], qtrue );
}

// Once a frame: the list is laid out again when the search or the folded headings changed
void Menu_ListCheck( menuDef_t *menu ) {
	char	search[64], folds[256];
	char	*p;

	if ( !menu->searchCvar && !menu->foldCvar && !menu->advancedCvar ) {
		return;
	}
	search[0] = folds[0] = '\0';
	if ( menu->searchCvar ) {
		DC->getCVarString( menu->searchCvar, search, sizeof( search ) );
	}
	if ( menu->foldCvar ) {
		DC->getCVarString( menu->foldCvar, folds, sizeof( folds ) );
	}
	for ( p = search; *p == ' '; p++ ) {
	}
	memmove( search, p, strlen( p ) + 1 );
	for ( p = search + strlen( search ); p > search && p[-1] == ' '; p-- ) {
		p[-1] = '\0';
	}
	if ( menu->listKnown && !strcmp( search, menu->listSearch ) && !strcmp( folds, menu->listFolds )
		&& ( !menu->advancedCvar || menu->listAll == (qboolean)( DC->getCVarValue( menu->advancedCvar ) != 0.0f ) ) ) {
		return;
	}
	Menu_ListLayout( menu, search, folds, (qboolean)strcmp( search, menu->listSearch ) );
	Q_strncpyz( menu->listSearch, search, sizeof( menu->listSearch ) );
	Q_strncpyz( menu->listFolds, folds, sizeof( menu->listFolds ) );
}

// a click on a heading: its rows fold away, or come back
void Menu_ToggleFold( menuDef_t *menu, int fold ) {
	char		folds[256], out[256];
	const char	*p;
	qboolean	was = qfalse;

	if ( !menu->foldCvar || menu->searching ) {
		return;
	}
	DC->getCVarString( menu->foldCvar, folds, sizeof( folds ) );
	out[0] = '\0';
	for ( p = folds; *p; ) {
		char word[16];
		int len;

		while ( *p == ' ' ) {
			p++;
		}
		len = (int)strcspn( p, " " );
		if ( !len ) {
			break;
		}
		Q_strncpyz( word, p, Q_min( len + 1, (int)sizeof( word ) ) );
		p += len;
		if ( atoi( word ) == fold ) {
			was = qtrue;
			continue;
		}
		Q_strcat( out, sizeof( out ), va( "%s%s", out[0] ? " " : "", word ) );
	}
	if ( !was ) {
		Q_strcat( out, sizeof( out ), va( "%s%i", out[0] ? " " : "", fold ) );
	}
	DC->setCVar( menu->foldCvar, out );
	Menu_ListCheck( menu );
}

/*
=================
Menu_ListCards

In a menu with a canvas every heading of a list ("fold <n>") is the top of a card that holds its rows: a panel
from the heading down to where its last row that is shown ends (cardBottom, Menu_ListLayout), as wide as the
heading, cut to the window of the list. (The Setup of the redesign: the parts of a page as cards in two columns.)
=================
*/
void Menu_ListCards( menuDef_t *menu ) {
	static const vec4_t	fill = { 0.031f, 0.055f, 0.11f, 0.78f }, edge = { 0.11f, 0.18f, 0.29f, 1.0f };
	rectDef_t			list;
	int					i;

	if ( menu->canvasW <= 0.0f || !Menu_HasList( menu ) || !Skin_Ready() ) {
		return;
	}
	Menu_ListWindow( menu, &list );
	for ( i = 0; i < menu->itemCount; i++ ) {
		const itemDef_t	*item = menu->items[i];
		float			top, bottom;

		if ( !item->scroll || !item->fold || item->listOut || item->cardBottom <= item->window.rectClient.y
			|| Item_OffPage( item ) || !( item->window.flags & WINDOW_VISIBLE ) ) {
			continue;
		}
		// (the heading of a card that is not the first of its column has the gap to the one above in it: the card
		// begins CARD_HEAD_PX above the end of the heading, its line CARD_LINE_PX under its top)
		top = item->window.rect.y + item->window.rect.h - CARD_HEAD_PX * menu->canvasUnit;
		bottom = item->window.rect.y + ( item->cardBottom - item->window.rectClient.y ) + CARD_BOTTOM_PX * menu->canvasUnit;
		{
			const float	head = top + CARD_LINE_PX * menu->canvasUnit;
			const float	y0 = Q_max( top, list.y ), y1 = Q_min( bottom, list.y + list.h );

			if ( y1 > y0 ) {
				Skin_Panel( item->window.rect.x, y0, item->window.rect.w, y1 - y0, 10.0f * menu->canvasUnit, fill, edge );
			}
			if ( head >= list.y && head <= list.y + list.h ) {
				Skin_Line( item->window.rect.x, head, item->window.rect.w, edge );
			}
		}
	}
}

/*
=================
Menu_StackFrame

The items of a menu with the same "stack <n>" are a column of places from the top down, as the menu file put
them; the items at one height are one place (the rows of the game types that share it). A place whose items are
all hidden right now by their cvar tests is left out and what is under it moves up by the room it had, so the
rules of Create a Game and the like have no gaps whatever game type is chosen. Once a frame: the cvars can change.
=================
*/
void Menu_StackFrame( menuDef_t *menu ) {
	static int	order[MAX_MENUITEMS];
	qboolean	done[MAX_MENUITEMS];
	float		baseX, baseY;
	int			i, j, k, n;

	baseX = menu->window.rect.x;
	baseY = menu->window.rect.y;
	if ( menu->window.border != 0 ) {
		baseX += menu->window.borderSize;
		baseY += menu->window.borderSize;
	}
	memset( done, 0, sizeof( done ) );
	for ( i = 0; i < menu->itemCount; i++ ) {
		float shift = 0.0f;

		if ( done[i] || menu->items[i]->stack <= 0 ) {
			continue;
		}
		// the items of this stack, from the top down
		n = 0;
		for ( j = i; j < menu->itemCount; j++ ) {
			if ( menu->items[j]->stack == menu->items[i]->stack ) {
				done[j] = qtrue;
				for ( k = n++; k > 0 && menu->items[order[k - 1]]->window.rectClient.y > menu->items[j]->window.rectClient.y; k-- ) {
					order[k] = order[k - 1];
				}
				order[k] = j;
			}
		}
		for ( j = 0; j < n; ) {
			const float	y = menu->items[order[j]]->window.rectClient.y;
			qboolean	shown = qfalse;
			int			end;

			for ( end = j; end < n && fabsf( menu->items[order[end]]->window.rectClient.y - y ) < 0.5f; end++ ) {
				const itemDef_t *item = menu->items[order[end]];

				if ( ( item->window.flags & WINDOW_VISIBLE )
					&& !( ( item->cvarFlags & ( CVAR_SHOW | CVAR_HIDE ) ) && !Item_EnableShowViaCvar( (itemDef_t *)item, CVAR_SHOW ) ) ) {
					shown = qtrue;
				}
			}
			for ( k = j; k < end; k++ ) {
				itemDef_t	*item = menu->items[order[k]];
				const float	border = item->window.border ? item->window.borderSize : 0.0f;
				const float	to = baseY + border + item->window.rectClient.y - shift;

				if ( item->window.rect.y != to ) {
					item->window.rect.x = baseX + border + item->window.rectClient.x;
					item->window.rect.y = to;
					// (where its words stand is worked out again, Item_SetScreenCoords)
					item->textRect.w = 0;
					item->textRect.h = 0;
				}
			}
			if ( !shown ) {
				// (the last place: the room it had, as high as its highest item)
				float room = 0.0f;

				if ( end < n ) {
					room = menu->items[order[end]]->window.rectClient.y - y;
				} else {
					for ( k = j; k < end; k++ ) {
						room = Q_max( room, menu->items[order[k]]->window.rectClient.h );
					}
				}
				shift += room;
			}
			j = end;
		}
		// "stack -<n>": the frame around the stack is shorter by what was left out
		for ( j = 0; j < menu->itemCount; j++ ) {
			itemDef_t *frame = menu->items[j];

			if ( frame->stack == -menu->items[i]->stack ) {
				frame->window.rect.h = Q_max( 0.0f, frame->window.rectClient.h - shift );
			}
		}
	}
}
