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

// cvar_cleanup.cpp -- VisionJK: the cvars no code has used for a month, and cvar_cleanup, which removes them.
//
// A cvar a config sets and no code registers is user created. With seta it is stored again at every exit, so the
// cvars of old versions and of other mods stay in a config forever. The engine notes the day code used a cvar
// (registered it, or a module read it by name) in cvarseen.txt of the home path, for all mods together, and
// offers the ones no code used for CVARSEEN_DAYS (cvar_seen.c). A name behind a vstr in a cvar or a key binding
// is never offered: it is a script, which nothing registers.

#include "qcommon/qcommon.h"
#include "qcommon/cvar_seen.h"

#ifndef DEDICATED
	#include "client/keys.h"		// the bindings, which can run a script
#endif

#include <time.h>

#define SEEN_FILE			"cvarseen.txt"
#define SEEN_SAVE_MS		60000
#define SEEN_FILE_MAX		( CVARSEEN_MAX * 80 + 32 )

extern cvar_t	*cvar_vars;		// cvar.cpp
static int		seenSaveTime;

static int Cvar_Today( void ) {
	return (int)( time( NULL ) / ( 24 * 60 * 60 ) );
}

void Cvar_Used( const char *name ) {
	const int today = Cvar_Today();

	if ( CvarSeen_Day( name ) < today )
		CvarSeen_Touch( name, today );
}

// What the modules read by name. Only a user created cvar needs to be noted here, Cvar_Get notes the others.
static void Cvar_ReadByName( const char *name ) {
	const uint32_t flags = Cvar_Flags( name );

	if ( flags != CVAR_NONEXISTENT && ( flags & CVAR_USER_CREATED ) )
		Cvar_Used( name );
}

void Cvar_VM_VariableStringBuffer( const char *name, char *buffer, int bufsize ) {
	Cvar_ReadByName( name );
	Cvar_VariableStringBuffer( name, buffer, bufsize );
}

int Cvar_VM_VariableIntegerValue( const char *name ) {
	Cvar_ReadByName( name );
	return Cvar_VariableIntegerValue( name );
}

float Cvar_VM_VariableValue( const char *name ) {
	Cvar_ReadByName( name );
	return Cvar_VariableValue( name );
}

void Cvar_SeenLoad( void ) {
	fileHandle_t	f;
	const int		len = FS_SV_FOpenFileRead( SEEN_FILE, &f );
	char			*text;

	seenSaveTime = Sys_Milliseconds();
	if ( !f )
		return;
	if ( len <= 0 || len > SEEN_FILE_MAX ) {
		FS_FCloseFile( f );
		return;
	}
	text = (char *)Z_Malloc( len + 1, TAG_TEMP_WORKSPACE, qfalse );
	FS_Read( text, len, f );
	FS_FCloseFile( f );
	text[len] = '\0';
	CvarSeen_Parse( text, Cvar_Today() );
	Z_Free( text );
}

void Cvar_SeenSave( void ) {
	fileHandle_t	f;
	char			*text;
	int				len;

	seenSaveTime = Sys_Milliseconds();
	if ( !CvarSeen_Dirty() || !FS_Initialized() )
		return;
	text = (char *)Z_Malloc( SEEN_FILE_MAX, TAG_TEMP_WORKSPACE, qfalse );
	len = CvarSeen_Write( text, SEEN_FILE_MAX );
	if ( len > 0 && ( f = FS_SV_FOpenFileWrite( SEEN_FILE ) ) != 0 ) {
		FS_Write( text, len, f );
		FS_FCloseFile( f );
	}
	Z_Free( text );
}

// once a minute at most: a level that loads touches many cvars at once
void Cvar_SeenFrame( void ) {
	if ( CvarSeen_Dirty() && Sys_Milliseconds() - seenSaveTime >= SEEN_SAVE_MS )
		Cvar_SeenSave();
}

// "vstr name" in a text, as a word
static qboolean Cvar_TextRuns( const char *text, const char *name ) {
	const size_t	nameLen = strlen( name );
	const char		*p;

	for ( p = Q_stristr( text, "vstr" ); p; p = Q_stristr( p + 4, "vstr" ) ) {
		const char *word = p + 4;

		if ( p > text && ( isalnum( (unsigned char)p[-1] ) || p[-1] == '_' ) )
			continue;
		if ( *word != ' ' && *word != '\t' && *word != '"' )
			continue;
		while ( *word == ' ' || *word == '\t' || *word == '"' )
			word++;
		if ( !Q_stricmpn( word, name, (int)nameLen ) && !isalnum( (unsigned char)word[nameLen] ) && word[nameLen] != '_' )
			return qtrue;
	}
	return qfalse;
}

static qboolean Cvar_IsScript( const char *name ) {
	for ( const cvar_t *var = cvar_vars; var; var = var->next ) {
		if ( var->string && Cvar_TextRuns( var->string, name ) )
			return qtrue;
	}
#ifndef DEDICATED
	for ( int key = 0; key < MAX_KEYS; key++ ) {
		const char *binding = Key_GetBinding( key );

		if ( binding && binding[0] && Cvar_TextRuns( binding, name ) )
			return qtrue;
	}
#endif
	return qfalse;
}

static qboolean Cvar_IsUnused( const cvar_t *var, int today ) {
	return (qboolean)( var->name && ( var->flags & CVAR_USER_CREATED ) && CvarSeen_Unused( var->name, today )
		&& !Cvar_IsScript( var->name ) );
}

int Cvar_UnusedList( char *buf, int size ) {
	const int	today = Cvar_Today();
	int			len = 0;

	if ( size < 1 )
		return 0;
	buf[0] = '\0';
	for ( const cvar_t *var = cvar_vars; var; var = var->next ) {
		char	line[MAX_CVAR_VALUE_STRING + 96], value[MAX_CVAR_VALUE_STRING];
		int		n;

		if ( !Cvar_IsUnused( var, today ) )
			continue;
		// a tab in the value would be a column more
		Q_strncpyz( value, var->string, sizeof( value ) );
		for ( char *t = value; *t; t++ ) {
			if ( *t == '\t' || *t == '\n' || *t == '\r' )
				*t = ' ';
		}
		n = Com_sprintf( line, sizeof( line ), "%s\t%s\t%i\n", var->name, value, CvarSeen_Day( var->name ) );
		if ( len + n >= size )
			break;
		memcpy( buf + len, line, n + 1 );
		len += n;
	}
	return len;
}

/*
============
Cvar_Cleanup_f

cvar_cleanup lists what could go, cvar_cleanup remove <name ...|all> takes it out of the config.
============
*/
static void Cvar_Cleanup_f( void ) {
	const int	today = Cvar_Today();
	int			count = 0;

	if ( Cmd_Argc() >= 3 && !Q_stricmp( Cmd_Argv( 1 ), "remove" ) ) {
		const qboolean all = (qboolean)( !Q_stricmp( Cmd_Argv( 2 ), "all" ) );

		for ( cvar_t *var = cvar_vars; var; ) {
			qboolean take = qfalse;

			if ( all ) {
				take = Cvar_IsUnused( var, today );
			} else if ( var->name && ( var->flags & CVAR_USER_CREATED ) ) {
				for ( int i = 2; i < Cmd_Argc(); i++ ) {
					if ( !Q_stricmp( Cmd_Argv( i ), var->name ) )
						take = qtrue;
				}
			}
			if ( take ) {
				var = Cvar_Unset( var );
				count++;
			} else {
				var = var->next;
			}
		}
		if ( count ) {
			// the config is written again without them
			cvar_modifiedFlags |= CVAR_ARCHIVE;
			Com_WriteConfiguration();
		}
		Com_Printf( "%i cvars removed\n", count );
		return;
	}
	if ( Cmd_Argc() != 1 ) {
		Com_Printf( "Usage: cvar_cleanup [remove <name ...|all>]\n" );
		return;
	}

	for ( const cvar_t *var = cvar_vars; var; var = var->next ) {
		int day;

		if ( !Cvar_IsUnused( var, today ) )
			continue;
		day = CvarSeen_Day( var->name );
		if ( day )
			Com_Printf( "%-32s \"%s" S_COLOR_WHITE "\"  last used %i days ago\n", var->name, var->string, today - day );
		else
			Com_Printf( "%-32s \"%s" S_COLOR_WHITE "\"  never used\n", var->name, var->string );
		count++;
	}
	if ( CvarSeen_Start() && today - CvarSeen_Start() < CVARSEEN_DAYS )
		Com_Printf( "The use of cvars is noted since %i days, a cvar never used is listed after %i.\n", today - CvarSeen_Start(), CVARSEEN_DAYS );
	Com_Printf( "%i cvars no code used for %i days. cvar_cleanup remove <name ...|all> takes them out of the config.\n", count, CVARSEEN_DAYS );
}

void Cvar_CleanupInit( void ) {
	Cmd_AddCommand( "cvar_cleanup", Cvar_Cleanup_f, "Lists the cvars no code used for a month, or removes them: cvar_cleanup remove <name ...|all>" );
}
