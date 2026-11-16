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

// ui_changes.c -- what a screen of settings changed, and the list that undoes it (part of ui_shared.c, compiled into
// the ui and the cgame)

#include "ui_shared_local.h"

/*
===============================================================================

What a screen of settings changed (VisionJK, "changesCvar <cvar>" of a menu)

When such a menu opens it keeps what each of its settings is. Every frame it counts the settings that differ from
that and sets <cvar> to the number and <cvar>Restart to 1 while one of them takes effect only after a restart (a
cvar with CVAR_LATCH, or one of the video settings the ui hands to the renderer with the restart: ui_r_*).
An ITEM_TYPE_CHANGES lists them with a button to undo each; uiScript changesRevert sets them all
back, uiScript changesApply keeps them (the menu file restarts the video in the same click where needed).

===============================================================================
*/

static struct {
	menuDef_t	*menu;
	int			count;
	struct {
		itemDef_t	*item;
		char		value[MENU_CHANGE_VALUE];
	} rows[MENU_CHANGES];
	int			changed;
	int			onPage[MENU_PAGES];
	qboolean	restart;
} menuChanges;

// a setting of a screen of settings: an item that sets a cvar by itself
qboolean Item_IsSetting( const itemDef_t *item ) {
	// (a row without a name is a switch of the screen itself: Essentials / All Settings)
	const menuDef_t *menu = (const menuDef_t *)item->parent;

	if ( !item->cvar || !item->cvar[0] || !item->text || !item->text[0] ) {
		return qfalse;
	}
	// (nor is the search of the screen)
	if ( menu && menu->searchCvar && !Q_stricmp( item->cvar, menu->searchCvar ) ) {
		return qfalse;
	}
	switch ( item->type ) {
	case ITEM_TYPE_YESNO:
	case ITEM_TYPE_MULTI:
	case ITEM_TYPE_SLIDER:
	case ITEM_TYPE_EDITFIELD:
	case ITEM_TYPE_NUMERICFIELD:
		return qtrue;
	default:
		return qfalse;
	}
}

// are two values of a cvar the same setting ("1" and "1.000" are)
qboolean Value_Same( const char *a, const char *b ) {
	char *endA, *endB;
	double x, y;

	if ( !strcmp( a, b ) ) {
		return qtrue;
	}
	x = strtod( a, &endA );
	y = strtod( b, &endB );
	if ( endA != a && !*endA && endB != b && !*endB ) {
		return (qboolean)( fabs( x - y ) < 0.0005 );
	}
	return qfalse;
}

// a row on one bit of its cvar ("cvarBit", the rows of the JA+ plugin features): that bit of a value
int Value_Bit( const itemDef_t *item, const char *value ) {
	return ( atoi( value ) >> ( item->cvarBit - 1 ) ) & 1;
}

// are two values of the cvar of a row the same setting of that row (for a bit row: the same bit)
qboolean Item_ValueSame( const itemDef_t *item, const char *a, const char *b ) {
	if ( item->cvarBit > 0 ) {
		return (qboolean)( Value_Bit( item, a ) == Value_Bit( item, b ) );
	}
	return Value_Same( a, b );
}

// is a yes/no item on (invertYesNo, cvarBit)
qboolean Item_YesNo_On( const itemDef_t *item ) {
	char		value[MAX_CVAR_VALUE_STRING];
	qboolean	set;

	if ( !item->cvar ) {
		return qfalse;
	}
	DC->getCVarString( item->cvar, value, sizeof( value ) );
	set = item->cvarBit > 0 ? (qboolean)Value_Bit( item, value ) : (qboolean)( atof( value ) != 0.0 );
	return (qboolean)( item->invertYesNo ? !set : set );
}

void Item_YesNo_Set( const itemDef_t *item, qboolean on ) {
	const qboolean set = (qboolean)( item->invertYesNo ? !on : on );

	if ( item->cvarBit > 0 ) {
		const int	mask = 1 << ( item->cvarBit - 1 );
		const int	bits = (int)DC->getCVarValue( item->cvar );

		DC->setCVar( item->cvar, va( "%i", set ? ( bits | mask ) : ( bits & ~mask ) ) );
	} else {
		DC->setCVar( item->cvar, set ? "1" : "0" );
	}
}

// does a change of this cvar take effect only after a restart
qboolean Cvar_NeedsRestart( const char *cvar ) {
	char		value[8];
	uint32_t	flags = 0;

	if ( !Q_stricmpn( cvar, "ui_r_", 5 ) ) {
		return qtrue;
	}
	if ( DC->getCVarInfo && DC->getCVarInfo( cvar, value, sizeof( value ), &flags ) ) {
		return (qboolean)( ( flags & CVAR_LATCH ) != 0 );
	}
	return qfalse;
}

/*
=================
Item_ValueText

A value of the cvar of a setting as the setting shows it: the name of the choice it is, on or off, the number of
a slider as its row writes it.
=================
*/
void Item_ValueText( const itemDef_t *item, const char *value, char *out, int size ) {
	int i;

	if ( item->type == ITEM_TYPE_MULTI && item->typeData.multi ) {
		const multiDef_t *multi = item->typeData.multi;

		for ( i = 0; i < multi->count; i++ ) {
			if ( multi->strDef ? ( multi->cvarStr[i] && !Q_stricmp( multi->cvarStr[i], value ) )
				: Value_Same( va( "%g", multi->cvarValue[i] ), value ) ) {
				Text_Word( multi->cvarList[i], out, size, qfalse, qfalse );
				return;
			}
		}
	} else if ( item->type == ITEM_TYPE_YESNO ) {
		qboolean on = item->cvarBit > 0 ? (qboolean)Value_Bit( item, value ) : (qboolean)( atof( value ) != 0.0 );

		if ( item->invertYesNo ) {
			on = (qboolean)!on;
		}
		Q_strncpyz( out, Vision_Text( on ? "On" : "Off" ), size );
		return;
	} else if ( item->type == ITEM_TYPE_SLIDER ) {
		const float v = (float)atof( value );

		Q_strncpyz( out, Item_Slider_Units( item ) ? va( "%i", (int)floorf( v + 0.5f ) ) : va( "%i %%", (int)floorf( v * 100.0f + 0.5f ) ),
			size );
		return;
	} else if ( item->row == 2 ) {
		// (a password)
		Q_strncpyz( out, value[0] ? "***" : "", size );
		return;
	}
	Q_strncpyz( out, value, size );
}

void Menu_ChangesStart( menuDef_t *menu ) {
	int i;

	if ( !menu || !menu->changesCvar ) {
		return;
	}
	memset( &menuChanges, 0, sizeof( menuChanges ) );
	menuChanges.menu = menu;
	for ( i = 0; i < menu->itemCount && menuChanges.count < MENU_CHANGES; i++ ) {
		itemDef_t *item = menu->items[i];

		if ( Item_IsSetting( item ) ) {
			menuChanges.rows[menuChanges.count].item = item;
			DC->getCVarString( item->cvar, menuChanges.rows[menuChanges.count].value, MENU_CHANGE_VALUE );
			menuChanges.count++;
		}
	}
	DC->setCVar( menu->changesCvar, "0" );
	DC->setCVar( va( "%sRestart", menu->changesCvar ), "0" );
	DC->setCVar( va( "%sState", menu->changesCvar ), "0" );
}

// is a setting of the open screen changed since it opened
static qboolean Changes_Row( int n ) {
	char now[MENU_CHANGE_VALUE];

	DC->getCVarString( menuChanges.rows[n].item->cvar, now, sizeof( now ) );
	return (qboolean)!Item_ValueSame( menuChanges.rows[n].item, now, menuChanges.rows[n].value );
}

void Menu_ChangesFrame( menuDef_t *menu ) {
	int i, changed = 0;
	qboolean restart = qfalse;

	if ( !menu || menu != menuChanges.menu || !menu->changesCvar ) {
		return;
	}
	memset( menuChanges.onPage, 0, sizeof( menuChanges.onPage ) );
	for ( i = 0; i < menuChanges.count; i++ ) {
		const itemDef_t *item = menuChanges.rows[i].item;

		// (a setting two rows share, as the old menus had for one cvar on two pages, counts once)
		if ( !Changes_Row( i ) ) {
			continue;
		}
		changed++;
		if ( item->page > 0 && item->page < MENU_PAGES ) {
			menuChanges.onPage[item->page]++;
		}
		if ( Cvar_NeedsRestart( item->cvar ) ) {
			restart = qtrue;
		}
	}
	if ( changed != menuChanges.changed || restart != menuChanges.restart ) {
		menuChanges.changed = changed;
		menuChanges.restart = restart;
		DC->setCVar( menu->changesCvar, va( "%i", changed ) );
		DC->setCVar( va( "%sRestart", menu->changesCvar ), restart ? "1" : "0" );
		// (one cvar for the buttons of the menu file: 0 nothing changed, 1 changed, 2 changed and a restart needed)
		DC->setCVar( va( "%sState", menu->changesCvar ), !changed ? "0" : restart ? "2" : "1" );
	}
}

// the settings of a page that are changed, for the mark on the button of the page (Item_Tab_Paint)
int Menu_ChangesOnPage( const menuDef_t *menu, int page ) {
	return ( menu == menuChanges.menu && page > 0 && page < MENU_PAGES ) ? menuChanges.onPage[page] : 0;
}

static void Changes_Undo( int n ) {
	const itemDef_t *item = menuChanges.rows[n].item;

	if ( item->cvarBit > 0 ) {
		// (only its bit: the other rows of the cvar keep theirs)
		const qboolean set = (qboolean)Value_Bit( item, menuChanges.rows[n].value );

		Item_YesNo_Set( item, (qboolean)( item->invertYesNo ? !set : set ) );
		return;
	}
	DC->setCVar( item->cvar, menuChanges.rows[n].value );
}

void Menu_ChangesRevert( void ) {
	int i;

	for ( i = 0; i < menuChanges.count; i++ ) {
		if ( Changes_Row( i ) ) {
			Changes_Undo( i );
		}
	}
}

/*
=================
Menu_ChangesClose

The screen of settings closes: what it changed and Apply did not keep goes back, as Revert (their word of
2026-11-14: the settings change while you click them, closing throws the changes away). A menu opened over it (the
changelog, the model picker) is no close: it stays visible under that one.
=================
*/
void Menu_ChangesClose( menuDef_t *menu ) {
	if ( !menu || menu != menuChanges.menu || !( menu->window.flags & WINDOW_VISIBLE ) ) {
		return;
	}
	Menu_ChangesRevert();
	// (the video settings are what the renderer has again: no question about restarting it)
	DC->setCVar( "ui_r_modified", "0" );
	menuChanges.menu = NULL;
	menuChanges.changed = 0;
	menuChanges.restart = qfalse;
	DC->setCVar( menu->changesCvar, "0" );
	DC->setCVar( va( "%sRestart", menu->changesCvar ), "0" );
	DC->setCVar( va( "%sState", menu->changesCvar ), "0" );
}

void Menu_ChangesApply( void ) {
	int i;

	for ( i = 0; i < menuChanges.count; i++ ) {
		DC->getCVarString( menuChanges.rows[i].item->cvar, menuChanges.rows[i].value, MENU_CHANGE_VALUE );
	}
}


// the text scale for a size of letters in pixels of the canvas of a menu (16 pixels: a scale of 16 / 18), or for
// units of the grid in a menu without one
float Item_Pixels( const itemDef_t *item, float pixels ) {
	const menuDef_t *menu = (const menuDef_t *)item->parent;

	return ( menu && menu->canvasW > 0.0f ? pixels * menu->canvasUnit : pixels * ( 480.0f / 900.0f ) ) / 18.0f;
}

// where the rows of an ITEM_TYPE_CHANGES are: below its heading, each as high as two lines
static float Item_Changes_Layout( const itemDef_t *item, float *top, float *rowH, float *undo ) {
	const menuDef_t	*menu = (const menuDef_t *)item->parent;
	const float		u = ( menu && menu->canvasW > 0.0f ) ? menu->canvasUnit : ( 480.0f / 900.0f );

	*top = item->window.rect.y + 30.0f * u;
	*rowH = 48.0f * u;
	*undo = 36.0f * u;
	return u;
}

/*
=================
Item_Changes_Paint

The settings the screen changed since it opened: a heading with their number, then for each its name and the
value it had and has now, and a button that sets it back. (The pane of Setup of the redesign.)
=================
*/
void Item_Changes_Paint( itemDef_t *item ) {
	const menuDef_t	*menu = (const menuDef_t *)item->parent;
	const rectDef_t	*r = &item->window.rect;
	const float		*accent = menu->focusColor;
	const int		font = item->iMenuFont;
	float			top, rowH, undo, y, u;
	vec4_t			muted = { 0.56f, 0.64f, 0.74f, 1.0f }, text = { 0.86f, 0.90f, 0.945f, 1.0f }, faint = { 0.455f, 0.537f, 0.647f, 1.0f };
	char			word[256], from[128], to[128];
	int				i, hidden = 0;

	if ( menu != menuChanges.menu ) {
		return;
	}
	u = Item_Changes_Layout( item, &top, &rowH, &undo );
	Text_Word( Vision_Text( "Changed settings" ), word, sizeof( word ), qtrue, qfalse );
	Row_Text( r->x, r->y, 18.0f * u, 0.0f, ITEM_ALIGN_LEFT, Item_Pixels( item, 12.0f ), muted, word, font );
	Row_Text( r->x + r->w, r->y, 18.0f * u, 0.0f, ITEM_ALIGN_RIGHT, Item_Pixels( item, 14.0f ), (float *)accent,
		va( "%i", menuChanges.changed ), font );
	if ( !menuChanges.changed ) {
		Row_Text( r->x, top, rowH * 0.5f, r->w, ITEM_ALIGN_LEFT, Item_Pixels( item, 15.0f ), faint, Vision_Text( "Nothing changed yet." ), font );
		return;
	}
	y = top;
	for ( i = 0; i < menuChanges.count; i++ ) {
		const itemDef_t	*row = menuChanges.rows[i].item;
		const qboolean	over = (qboolean)( DC->cursorx >= r->x + r->w - undo && DC->cursorx < r->x + r->w
							&& DC->cursory >= y + ( rowH - undo ) * 0.5f && DC->cursory < y + ( rowH + undo ) * 0.5f );
		char			now[MENU_CHANGE_VALUE];
		float			fw;
		vec4_t			edge;

		if ( !Changes_Row( i ) ) {
			continue;
		}
		if ( y + rowH > r->y + r->h ) {
			hidden++;
			continue;
		}
		DC->getCVarString( row->cvar, now, sizeof( now ) );
		Text_Word( row->text, word, sizeof( word ), qfalse, qtrue );
		Item_ValueText( row, menuChanges.rows[i].value, from, sizeof( from ) );
		Item_ValueText( row, now, to, sizeof( to ) );
		Row_Text( r->x, y + rowH * 0.08f, rowH * 0.46f, r->w - undo - 8.0f * u, ITEM_ALIGN_LEFT, Item_Pixels( item, 15.0f ),
			text, word, font );
		Row_Text( r->x, y + rowH * 0.5f, rowH * 0.42f, 0.0f, ITEM_ALIGN_LEFT, Item_Pixels( item, 13.0f ), muted, from, font );
		fw = DC->textWidth( from, Item_Pixels( item, 13.0f ), font );
		Skin_Arrow( r->x + fw + 5.0f * u, y + rowH * 0.71f - 3.0f * u, 3.5f * u, 6.0f * u, qfalse, muted );
		Row_Text( r->x + fw + 13.0f * u, y + rowH * 0.5f, rowH * 0.42f, 0.0f, ITEM_ALIGN_LEFT, Item_Pixels( item, 13.0f ),
			(float *)accent, to, font );
		// the button that sets it back
		VectorCopy4( over ? accent : skinEdge, edge );
		Skin_Panel( r->x + r->w - undo, y + ( rowH - undo ) * 0.5f, undo, undo, 6.0f * u, NULL, edge );
		Skin_Arrow( r->x + r->w - undo * 0.5f - undo * 0.12f, y + rowH * 0.5f - undo * 0.16f, undo * 0.2f, undo * 0.32f, qtrue,
			over ? accent : muted );
		Skin_Line( r->x, y + rowH - 1.0f, r->w, skinRule );
		y += rowH;
	}
	if ( hidden ) {
		Row_Text( r->x, y, rowH * 0.5f, r->w, ITEM_ALIGN_LEFT, Item_Pixels( item, 13.0f ), muted, va( "+%i", hidden ), font );
	}
}

// a click on the button of a row of an ITEM_TYPE_CHANGES sets that setting back
qboolean Item_Changes_HandleKey( itemDef_t *item, int key ) {
	const rectDef_t	*r = &item->window.rect;
	float			top, rowH, undo, y;
	int				i;

	if ( key != A_MOUSE1 || item->parent != menuChanges.menu ) {
		return qfalse;
	}
	Item_Changes_Layout( item, &top, &rowH, &undo );
	y = top;
	for ( i = 0; i < menuChanges.count; i++ ) {
		if ( !Changes_Row( i ) ) {
			continue;
		}
		if ( y + rowH > r->y + r->h ) {
			break;
		}
		if ( DC->cursorx >= r->x + r->w - undo && DC->cursorx < r->x + r->w
			&& DC->cursory >= y + ( rowH - undo ) * 0.5f && DC->cursory < y + ( rowH + undo ) * 0.5f ) {
			Changes_Undo( i );
			DC->startLocalSound( DC->Assets.itemFocusSound, CHAN_LOCAL_SOUND );
			return qtrue;
		}
		y += rowH;
	}
	return qfalse;
}
