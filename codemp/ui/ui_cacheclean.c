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

// ui_cacheclean.c -- the window "Clear Caches" of Setup (ui/jamp/cacheclean.menu, FEEDER_CACHECLEAN): the kinds of the
// folder cache of the profile as the engine lists them (client/cl_cache.cpp, ext.Cache_List), with their size and how
// many files they have. Clear empties the selected kind, Clear All every one (ext.Cache_Clear). Not from JA+.

#include "ui_local.h"

#define CACHE_LIST		2048
#define CACHE_MOST		16

typedef struct {
	const char	*id;
	const char	*label;			// in English
	double		megabytes;
	int			files;
} cacheRow_t;

static char			cacheText[CACHE_LIST];
static cacheRow_t	cacheRows[CACHE_MOST];
static int			cacheCount, cacheSelected;

// uiScript CacheClean: the list asked for again
void UI_CacheCleanLoad( void ) {
	char *line, *next;

	cacheCount = 0;
	cacheText[0] = '\0';
	// (an older engine ends uiImport_t before these, ui_public.h)
	if ( trap->Cvar_VariableValue( "v_uiCache" ) == 1.0f && trap->ext.Cache_List )
		trap->ext.Cache_List( cacheText, sizeof( cacheText ) );

	// "id<tab>label<tab>bytes<tab>files" a line
	for ( line = cacheText; *line && cacheCount < CACHE_MOST; line = next ) {
		char *label, *bytes, *files;

		next = strchr( line, '\n' );
		if ( !next )
			break;
		*next++ = '\0';
		label = strchr( line, '\t' );
		bytes = label ? strchr( label + 1, '\t' ) : NULL;
		files = bytes ? strchr( bytes + 1, '\t' ) : NULL;
		if ( !files )
			continue;
		*label++ = '\0';
		*bytes++ = '\0';
		*files++ = '\0';
		cacheRows[cacheCount].id = line;
		cacheRows[cacheCount].label = label;
		cacheRows[cacheCount].megabytes = atof( bytes ) / ( 1024.0 * 1024.0 );
		cacheRows[cacheCount].files = atoi( files );
		cacheCount++;
	}
	if ( cacheSelected >= cacheCount )
		cacheSelected = 0;
}

int UI_CacheCleanCount( void ) {
	return cacheCount;
}

// the columns: the kind, its size, its files
const char *UI_CacheCleanText( int index, int column ) {
	const cacheRow_t *row;

	if ( index < 0 || index >= cacheCount )
		return "";
	row = &cacheRows[index];
	if ( column == 0 )
		return Vision_Text( row->label );
	if ( column == 1 )
		return va( "%.1f MB", row->megabytes );
	if ( row->files == 1 )
		return Vision_Text( "1 file" );
	return va( Vision_Text( "%i files" ), row->files );
}

void UI_CacheCleanSelect( int index ) {
	cacheSelected = index;
}

// uiScript CacheCleanClear: the selected kind. uiScript CacheCleanAll: every one. The list is asked for again, the
// selection stays.
void UI_CacheCleanClear( qboolean all ) {
	if ( trap->Cvar_VariableValue( "v_uiCache" ) != 1.0f || !trap->ext.Cache_Clear )
		return;
	if ( all )
		trap->ext.Cache_Clear( "all" );
	else if ( cacheSelected >= 0 && cacheSelected < cacheCount )
		trap->ext.Cache_Clear( cacheRows[cacheSelected].id );
	UI_CacheCleanLoad();
}
