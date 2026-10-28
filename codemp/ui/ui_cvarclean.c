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

// ui_cvarclean.c -- the window "Clean Up Settings" of Setup (ui/jamp/cvarclean.menu, FEEDER_CVARCLEAN): the cvars of
// the config no code used for a month, as the engine notes them (qcommon/cvar_cleanup.cpp, ext.Cvar_UnusedList), with
// their value and when they were used last. Removing one runs cvar_cleanup of the engine, which also writes the
// config again without it. Not from JA+.

#include "ui_local.h"

#include <time.h>

#define CLEAN_LIST		( 32 * 1024 )
#define CLEAN_MOST		512

typedef struct {
	const char	*name;
	const char	*value;
	int			day;			// days since 1970, 0 for never
} cleanRow_t;

static char			cleanText[CLEAN_LIST];
static cleanRow_t	cleanRows[CLEAN_MOST];
static int			cleanCount, cleanSelected;

// uiScript CvarClean: the list asked for again. ui_cvarCleanCount is for the menu, which says when it is empty.
void UI_CvarCleanLoad( void ) {
	char *line, *next;

	cleanCount = 0;
	cleanSelected = 0;
	cleanText[0] = '\0';
	// (an older engine ends uiImport_t before this one, ui_public.h)
	if ( trap->Cvar_VariableValue( "v_uiCvarClean" ) == 1.0f && trap->ext.Cvar_UnusedList )
		trap->ext.Cvar_UnusedList( cleanText, sizeof( cleanText ) );

	// "name<tab>value<tab>day" a line
	for ( line = cleanText; *line && cleanCount < CLEAN_MOST; line = next ) {
		char *value, *day;

		next = strchr( line, '\n' );
		if ( !next )
			break;
		*next++ = '\0';
		value = strchr( line, '\t' );
		day = value ? strchr( value + 1, '\t' ) : NULL;
		if ( !day )
			continue;
		*value++ = '\0';
		*day++ = '\0';
		cleanRows[cleanCount].name = line;
		cleanRows[cleanCount].value = value;
		cleanRows[cleanCount].day = atoi( day );
		cleanCount++;
	}
	trap->Cvar_Set( "ui_cvarCleanCount", va( "%i", cleanCount ) );
}

int UI_CvarCleanCount( void ) {
	return cleanCount;
}

// the columns: the name, the value, when it was used last
const char *UI_CvarCleanText( int index, int column ) {
	const cleanRow_t *row;

	if ( index < 0 || index >= cleanCount )
		return "";
	row = &cleanRows[index];
	if ( column == 0 )
		return row->name;
	if ( column == 1 )
		return row->value;
	if ( !row->day )
		return Vision_Text( "Never used" );
	return va( Vision_Text( "%i days ago" ), (int)( time( NULL ) / ( 24 * 60 * 60 ) ) - row->day );
}

void UI_CvarCleanSelect( int index ) {
	cleanSelected = index;
}

// uiScript CvarCleanRemove: the selected one. uiScript CvarCleanRemoveAll: all that are listed.
void UI_CvarCleanRemove( qboolean all ) {
	if ( all ) {
		if ( cleanCount )
			trap->Cmd_ExecuteText( EXEC_NOW, "cvar_cleanup remove all\n" );
	} else if ( cleanSelected >= 0 && cleanSelected < cleanCount ) {
		trap->Cmd_ExecuteText( EXEC_NOW, va( "cvar_cleanup remove %s\n", cleanRows[cleanSelected].name ) );
	}
	UI_CvarCleanLoad();
}
