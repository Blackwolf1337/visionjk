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

// ui_dump.c -- the open menus and their items as text for the tools that test them (part of ui_shared.c, compiled
// into the ui and the cgame)

#include "ui_shared_local.h"

/*
=================
Menus_Dump

For tools that test the menus (the jka MCP server, "v_uiDump [file]"): the screen, then each open menu and every item of
it that can be seen right now, with its rectangle in pixels of the window, one line each, so a tool can measure a
layout and point the mouse at an item by its name. Lines begin with "uidump"; the last one is "uidump end".
=================
*/
static fileHandle_t dumpFile;

// can this item be seen right now (what the dump lists, and what v_uiSweep points at)
qboolean Item_Dumped( const itemDef_t *item ) {
	return (qboolean)!( !( item->window.flags & WINDOW_VISIBLE ) || Item_Away( item ) || item->listOut
		|| ( item->cvarFlags & ( CVAR_SHOW | CVAR_HIDE ) && !Item_EnableShowViaCvar( (itemDef_t *)item, CVAR_SHOW ) ) );
}

// a line of the dump: printed, or written to the file "v_uiDump <file>" named
static void QDECL Dump_Line( const char *fmt, ... ) {
	va_list	ap;
	char	line[1024];

	va_start( ap, fmt );
	Q_vsnprintf( line, sizeof( line ), fmt, ap );
	va_end( ap );
	if ( dumpFile ) {
		trap->FS_Write( line, (int)strlen( line ), dumpFile );
	} else {
		Com_Printf( "%s", line );
	}
}

void Menus_Dump( void ) {
	const float	bias = DC->bias, biasY = DC->biasY;
	int			i, j;

	// (a file to write to instead of the console: a tool reads a long dump from it without the console in between)
	dumpFile = 0;
	if ( trap->Cmd_Argc() > 1 ) {
		char name[MAX_QPATH];

		trap->Cmd_Argv( 1, name, sizeof( name ) );
		if ( trap->FS_Open( name, &dumpFile, FS_WRITE ) < 0 ) {
			dumpFile = 0;
		}
	}

	Dump_Line( "uidump screen %d %d %.4f %.4f %.3f %.3f %d %d\n", DC->glconfig.vidWidth, DC->glconfig.vidHeight, DC->xscale,
		DC->yscale, bias, biasY, DC->cursorx, DC->cursory );
	for ( i = 0; i < menuCount; i++ ) {
		const menuDef_t *menu = &Menus[i];

		if ( !( menu->window.flags & WINDOW_VISIBLE ) ) {
			continue;
		}
		Dump_Line( "uidump menu %s focus=%d page=%d canvas=%gx%g px=%.0f,%.0f,%.0f,%.0f desc=%.0f,%.0f,%d\n", menu->window.name ? menu->window.name : "?",
			( menu->window.flags & WINDOW_HASFOCUS ) ? 1 : 0, menu->page, menu->canvasW, menu->canvasH,
			( menu->window.rect.x + bias ) * DC->xscale, ( menu->window.rect.y + biasY ) * DC->yscale,
			menu->window.rect.w * DC->xscale, menu->window.rect.h * DC->yscale,
			( menu->descX + bias ) * DC->xscale, ( menu->descY + biasY ) * DC->yscale, menu->descAlignment );
		for ( j = 0; j < menu->itemCount; j++ ) {
			const itemDef_t	*item = menu->items[j];
			const rectDef_t	*r = &item->window.rect;
			char			text[128], value[64];

			if ( !Item_Dumped( item ) ) {
				continue;
			}
			text[0] = value[0] = '\0';
			if ( item->text && item->text[0] ) {
				Text_Word( item->text, text, sizeof( text ), qfalse, qfalse );
			}
			if ( item->cvar ) {
				DC->getCVarString( item->cvar, value, sizeof( value ) );
			}
			Dump_Line( "uidump item %s %d %s type=%d px=%.0f,%.0f,%.0f,%.0f flags=%s%s%s%s cvar=%s value=%s text=%s\n",
				menu->window.name ? menu->window.name : "?", j, item->window.name ? item->window.name : "-", item->type,
				( r->x + bias ) * DC->xscale, ( r->y + biasY ) * DC->yscale, r->w * DC->xscale, r->h * DC->yscale,
				( item->window.flags & WINDOW_HASFOCUS ) ? "F" : "", ( item->window.flags & WINDOW_MOUSEOVER ) ? "M" : "",
				( item->window.flags & WINDOW_DECORATION ) ? "D" : "", item->disabled ? "X" : "",
				item->cvar ? item->cvar : "-", value, text );
			{
				// what a check of the layout needs (tools/test, the audit of the menus): the style, the corners, the
				// box the text of the item was drawn in, the widget of a row, the list it is in. A line of its own:
				// the text of the item line goes to its end.
				const rectDef_t	*t = &item->textRect;
				rectDef_t		widget = { 0, 0, 0, 0 };

				if ( Item_IsRow( item ) ) {
					Item_Row_Widget( item, &widget );
				}
				Dump_Line( "uidump geom %d style=%d radius=%.1f border=%d text=%.0f,%.0f,%.0f,%.0f align=%d widget=%.0f,%.0f,%.0f,%.0f row=%d page=%d scroll=%d fold=%d column=%d stack=%d card=%.0f\n",
					j, item->window.style, item->window.radius * DC->yscale, item->window.border,
					( t->x + bias ) * DC->xscale, ( t->y + biasY ) * DC->yscale, t->w * DC->xscale, t->h * DC->yscale, item->textalignment,
					widget.w > 0.0f ? ( widget.x + bias ) * DC->xscale : 0.0f, widget.w > 0.0f ? ( widget.y + biasY ) * DC->yscale : 0.0f,
					widget.w * DC->xscale, widget.h * DC->yscale, item->row, item->page, item->scroll, item->fold, item->column, item->stack,
					item->fold ? ( item->window.rect.y + ( item->cardBottom - item->window.rectClient.y ) + biasY ) * DC->yscale : 0.0f );
			}
		}
	}
	Dump_Line( "uidump end\n" );
	if ( dumpFile ) {
		trap->FS_Close( dumpFile );
		dumpFile = 0;
	}
}
