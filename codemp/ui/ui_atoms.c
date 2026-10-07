/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors
Copyright (C) 2026, VisionJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
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

/**********************************************************************
	UI_ATOMS.C

	User interface building blocks and support functions.
**********************************************************************/
#include "ui_local.h"

#define NUM_UI_ARGSTRS (4)
#define UI_ARGSTR_MASK (NUM_UI_ARGSTRS-1)
static char tempArgStrs[NUM_UI_ARGSTRS][MAX_STRING_CHARS];

static char *UI_Argv( int arg ) {
	static int index=0;
	char *s = tempArgStrs[index++ & UI_ARGSTR_MASK];
	trap->Cmd_Argv( arg, s, MAX_STRING_CHARS );
	return s;
}

#define NUM_UI_CVARSTRS (4)
#define UI_CVARSTR_MASK (NUM_UI_CVARSTRS-1)
static char tempCvarStrs[NUM_UI_CVARSTRS][MAX_CVAR_VALUE_STRING];

char *UI_Cvar_VariableString( const char *name ) {
	static int index=0;
	char *s = tempCvarStrs[index++ & UI_ARGSTR_MASK];
	trap->Cvar_VariableStringBuffer( name, s, MAX_CVAR_VALUE_STRING );
	return s;
}

static void	UI_Cache_f( void ) {
	Display_CacheAll();
	if ( trap->Cmd_Argc() == 2 ) {
		int i;
		for ( i=0; i<uiInfo.q3HeadCount; i++ ) {
			trap->Print( "model %s\n", uiInfo.q3HeadNames[i] );
		}
		trap->Print( "Number of Heads Menu Icons:^5%i\n", uiInfo.q3HeadCount );	// JA+
	}
}

static void UI_OpenMenu_f( void ) {
	Menus_CloseAll();
	if ( Menus_ActivateByName( UI_Argv( 1 ) ) )
		trap->Key_SetCatcher( KEYCATCH_UI );
}

static void UI_OpenSiegeMenu_f( void ) {
	if ( trap->Cvar_VariableValue( "g_gametype" ) == GT_SIEGE ) {
		Menus_CloseAll();
		if ( Menus_ActivateByName( UI_Argv( 1 ) ) )
			trap->Key_SetCatcher( KEYCATCH_UI );
	}
}

/*
=================
JA+ macro scan (the server sets jp_macroScan): key bindings that are macros are replaced by
"echo illegal macro: <binding>" (; becomes :), ui_macroenable puts them back.

A binding is a macro if
- it has several commands and one of them moves, attacks, uses the force or changes the turning speed
- it has one command that changes the turning speed
- it executes a file (exec) or a cvar (vstr) that holds such commands, also through further files and cvars
=================
*/
#define JP_MAX_BINDS		320
#define JP_MACRO_PREFIX		"echo illegal macro: "
#define JP_MACRO_MARK		"illegal macro:"
#define JP_MACRO_MAX_DEPTH	4			// exec and vstr levels that are followed, the plugin has no limit
#define JP_MACRO_MAX_FILE	0x10000		// bytes of a file that are read

static const char *const jpMacroTurn[] = {
	"sensitivity", "m_yaw", "cl_yawspeed"
};

// in files and cvars
static const char *const jpMacroScript[] = {
	"+moveup", "+attack", "+forward", "+back", "+movedown", "+speed", "+button", "+left", "+right", "+moveright",
	"+moveleft", "+altattack"
};

// in a binding also these
static const char *const jpMacroForce[] = {
	"force_absorb", "force_heal", "force_distract", "force_protect", "+force_grip", "+force_drain",
	"+force_lightning", "force_rage", "force_throw", "force_pull", "force_speed", "force_seeing",
	"force_forcepowerother", "force_healother", "+useforce"
};

static qboolean UI_JPMacroKeyword( const char *text, const char *const *keywords, size_t count ) {
	size_t i;

	for ( i = 0; i < count; i++ ) {
		if ( strstr( text, keywords[i] ) )
			return qtrue;
	}
	return qfalse;
}

static qboolean UI_JPMacroScript( const char *text ) {
	return (qboolean)( UI_JPMacroKeyword( text, jpMacroScript, ARRAY_LEN( jpMacroScript ) )
		|| UI_JPMacroKeyword( text, jpMacroTurn, ARRAY_LEN( jpMacroTurn ) ) );
}

static qboolean UI_JPMacroCommands( const char *text, qboolean lines, int depth );

// a cvar that is executed with vstr
static qboolean UI_JPMacroCvar( const char *name, int depth ) {
	char value[256];

	if ( depth > JP_MACRO_MAX_DEPTH )
		return qtrue;

	trap->Cvar_VariableStringBuffer( name, value, sizeof( value ) );
	Q_strlwr( value );
	if ( UI_JPMacroScript( value ) )
		return qtrue;
	return UI_JPMacroCommands( value, qfalse, depth );
}

// a file that is executed with exec. As in the plugin a file that can not be read counts as a macro.
static qboolean UI_JPMacroFile( const char *name, int depth ) {
	char path[MAX_QPATH];
	fileHandle_t f = 0;
	char *text;
	int len;
	qboolean macro;

	if ( depth > JP_MACRO_MAX_DEPTH )
		return qtrue;

	Q_strncpyz( path, name, sizeof( path ) );
	len = trap->FS_Open( path, &f, FS_READ );
	if ( !f ) {
		Q_strcat( path, sizeof( path ), ".cfg" );
		len = trap->FS_Open( path, &f, FS_READ );
		if ( !f )
			return qtrue;
	}
	len = Com_Clampi( 0, JP_MACRO_MAX_FILE, len );

	text = (char *)BG_TempAlloc( len + 1 );
	trap->FS_Read( text, len, f );
	trap->FS_Close( f );
	text[len] = '\0';
	Q_strlwr( text );

	macro = (qboolean)( UI_JPMacroScript( text ) || UI_JPMacroCommands( text, qtrue, depth ) );
	BG_TempFree( len + 1 );
	return macro;
}

// the commands of a binding, a cvar or a file (lines: a line end also ends a command), text in lower case:
// qtrue if one of them executes a file or a cvar that is a macro
static qboolean UI_JPMacroCommands( const char *text, qboolean lines, int depth ) {
	char arg[256];
	const char *p = text;

#define JP_MACRO_EOL( c ) ( lines && ( (c) == '\n' || (c) == '\r' ) )
	while ( *p ) {
		qboolean exec, vstr;

		while ( *p == ';' || *p == ' ' || *p == '\t' || JP_MACRO_EOL( *p ) )
			p++;

		exec = (qboolean)( !Q_strncmp( p, "exec ", 5 ) || !Q_strncmp( p, "exec\t", 5 ) );
		vstr = (qboolean)( !Q_strncmp( p, "vstr ", 5 ) || !Q_strncmp( p, "vstr\t", 5 ) );
		if ( exec || vstr ) {
			size_t len = 0;

			p += 4;
			while ( *p == ' ' || *p == '\t' )
				p++;

			if ( *p == '"' ) {
				for ( p++; *p && *p != '"' && !JP_MACRO_EOL( *p ); p++ ) {
					if ( len < sizeof( arg ) - 1 )
						arg[len++] = *p;
				}
				if ( *p == '"' )
					p++;
			}
			else {
				for ( ; *p && *p != ';' && *p != ' ' && *p != '\t' && !JP_MACRO_EOL( *p ); p++ ) {
					if ( len < sizeof( arg ) - 1 )
						arg[len++] = *p;
				}
			}
			arg[len] = '\0';

			if ( exec ? UI_JPMacroFile( arg, depth + 1 ) : UI_JPMacroCvar( arg, depth + 1 ) )
				return qtrue;
		}

		// the rest of the command, a ; inside quotes does not end it
		while ( *p && *p != ';' && !JP_MACRO_EOL( *p ) ) {
			if ( *p++ == '"' ) {
				while ( *p && *p != '"' && !JP_MACRO_EOL( *p ) )
					p++;
				if ( *p == '"' )
					p++;
			}
		}
	}
#undef JP_MACRO_EOL
	return qfalse;
}

// bind in lower case
static qboolean UI_JPMacroBinding( const char *bind ) {
	if ( strchr( bind, ';' ) ) {
		if ( UI_JPMacroScript( bind ) || UI_JPMacroKeyword( bind, jpMacroForce, ARRAY_LEN( jpMacroForce ) ) )
			return qtrue;
		return UI_JPMacroCommands( bind, qfalse, 0 );
	}

	if ( strstr( bind, JP_MACRO_MARK ) )
		return qfalse;
	if ( strstr( bind, "exec " ) || strstr( bind, "exec\t" ) || strstr( bind, "vstr " ) || strstr( bind, "vstr\t" ) )
		return UI_JPMacroCommands( bind, qfalse, 0 );
	return UI_JPMacroKeyword( bind, jpMacroTurn, ARRAY_LEN( jpMacroTurn ) );
}

static void UI_JPMacroScan_f( void ) {
	char bind[256], lower[256], text[300];
	char *c;
	int key;

	for ( key = 0; key < JP_MAX_BINDS; key++ ) {
		bind[0] = '\0';
		trap->Key_GetBindingBuf( key, bind, sizeof( bind ) );
		Q_strncpyz( lower, bind, sizeof( lower ) );
		Q_strlwr( lower );
		if ( !lower[0] || !UI_JPMacroBinding( lower ) )
			continue;

		trap->Print( "Illegal binding found.... removing\n" );
		for ( c = bind; *c; c++ ) {
			if ( *c == ';' )
				*c = ':';
		}
		Com_sprintf( text, sizeof( text ), JP_MACRO_PREFIX "%s", bind );
		trap->Key_SetBinding( key, text );
	}
}

static void UI_JPMacroEnable_f( void ) {
	char bind[256];
	char *c;
	int key;

	for ( key = 0; key < JP_MAX_BINDS; key++ ) {
		bind[0] = '\0';
		trap->Key_GetBindingBuf( key, bind, sizeof( bind ) );
		if ( Q_stricmpn( bind, JP_MACRO_PREFIX, strlen( JP_MACRO_PREFIX ) ) )
			continue;
		memmove( bind, bind + strlen( JP_MACRO_PREFIX ), strlen( bind + strlen( JP_MACRO_PREFIX ) ) + 1 );
		for ( c = bind; *c; c++ ) {
			if ( *c == ':' )
				*c = ';';
		}
		trap->Print( "Recover MAcro bind KEy %i : %s\n", key, bind );
		trap->Key_SetBinding( key, bind );
	}
}

typedef struct consoleCommand_s {
	const char	*cmd;
	void		(*func)(void);
} consoleCommand_t;

int cmdcmp( const void *a, const void *b ) {
	return Q_stricmp( (const char *)a, ((consoleCommand_t*)b)->cmd );
}

static consoleCommand_t	commands[] = {
	{ "ui_cache",			UI_Cache_f },
	{ "ui_load",			UI_Load },
	{ "ui_macroenable",		UI_JPMacroEnable_f },
	{ "ui_macroscan",		UI_JPMacroScan_f },
	{ "ui_openmenu",		UI_OpenMenu_f },
	{ "ui_opensiegemenu",	UI_OpenSiegeMenu_f },
	{ "ui_report",			UI_Report },
};

static const size_t numCommands = ARRAY_LEN( commands );

/*
=================
UI_ConsoleCommand

The string has been tokenized and can be retrieved with
Cmd_Argc() / Cmd_Argv()
=================
*/
qboolean UI_ConsoleCommand( int realTime ) {
	consoleCommand_t *command = NULL;

	uiInfo.uiDC.frameTime = realTime - uiInfo.uiDC.realTime;
	uiInfo.uiDC.realTime = realTime;

	command = (consoleCommand_t *)Q_LinearSearch( UI_Argv( 0 ), commands, numCommands, sizeof( commands[0] ), cmdcmp );

	if ( !command )
		return qfalse;

	command->func();
	return qtrue;
}

void UI_DrawHandlePic( float x, float y, float w, float h, qhandle_t hShader ) {
	float	s0;
	float	s1;
	float	t0;
	float	t1;

	if( w < 0 ) {	// flip about vertical
		w  = -w;
		s0 = 1;
		s1 = 0;
	}
	else {
		s0 = 0;
		s1 = 1;
	}

	if( h < 0 ) {	// flip about horizontal
		h  = -h;
		t0 = 1;
		t1 = 0;
	}
	else {
		t0 = 0;
		t1 = 1;
	}

	trap->R_DrawStretchPic( x, y, w, h, s0, t0, s1, t1, hShader );
}

/*
================
UI_FillRect

Coordinates are 640*480 virtual values
=================
*/
void UI_FillRect( float x, float y, float width, float height, const float *color ) {
	trap->R_SetColor( color );
	trap->R_DrawStretchPic( x, y, width, height, 0, 0, 0, 0, uiInfo.uiDC.whiteShader );
	trap->R_SetColor( NULL );
}

void UI_DrawSides(float x, float y, float w, float h) {
	trap->R_DrawStretchPic( x, y, 1, h, 0, 0, 0, 0, uiInfo.uiDC.whiteShader );
	trap->R_DrawStretchPic( x + w - 1, y, 1, h, 0, 0, 0, 0, uiInfo.uiDC.whiteShader );
}

void UI_DrawTopBottom(float x, float y, float w, float h) {
	trap->R_DrawStretchPic( x, y, w, 1, 0, 0, 0, 0, uiInfo.uiDC.whiteShader );
	trap->R_DrawStretchPic( x, y + h - 1, w, 1, 0, 0, 0, 0, uiInfo.uiDC.whiteShader );
}
/*
================
UI_DrawRect

Coordinates are 640*480 virtual values
=================
*/
void UI_DrawRect( float x, float y, float width, float height, const float *color ) {
	trap->R_SetColor( color );

	UI_DrawTopBottom(x, y, width, height);
	UI_DrawSides(x, y, width, height);

	trap->R_SetColor( NULL );
}
