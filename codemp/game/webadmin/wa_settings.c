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

// wa_settings.c -- the settings form of the web panel and the cleanup of the cvars. Not from JKAdmin or JA+.
//
// The form has every cvar of the server: those of wa_settings_table.h with a group, a kind of field and a line of
// what it does, and every other one of the game module under Other. A change goes into the config in place
// (wa_cfgedit.c), which keeps its comments and its order, and is set on the server at once as the console would
// set it.

#include "game/webadmin/wa_local.h"
#include "game/webadmin/wa_cfgedit.h"

#include <time.h>

#define WA_CONFIG_MAX		60000	// as the text editor of the page: the request of a whole file has to fit
#define WA_CLEANUP_LIST		65536
#define WA_CLEANUP_MOST		4096	// statements of a config the cleanup looks at

static const waSetting_t waSettings[] = {
	#include "game/webadmin/wa_settings_table.h"
};

static const char *const waCatNames[WA_CAT_COUNT] = {
	"Server", "Network", "Game type", "Votes and map cycle", "Force powers, weapons and items", "Saber", "Gameplay",
	"Duel", "Bots", "JA+ admin", "JA+ features", "Protection", "VisionJK", "Web panel", "Logging",
	"Other"
};

static const char *const waTypeNames[] = { "text", "int", "float", "bool", "enum", "bits", "secret" };

// a cvar of the form as it is now
typedef struct waField_s {
	const char			*name;
	const waSetting_t	*setting;		// NULL: one of the game module the table does not know
	const char			*def;			// "" when not known
	uint32_t			flags;			// CVAR_*, 0 when not known
	int					apply;			// WA_APPLY_*
} waField_t;

enum { WA_APPLY_LIVE, WA_APPLY_MAP, WA_APPLY_RESTART };
static const char *const waApplyNames[] = { "live", "map", "restart" };

// the server starts with +exec server.cfg
const char *WA_ConfigFile( void ) {
	return "server.cfg";
}

// The config written, what was there before kept as <name>.bak. The engine writes only into the home path, so the
// first save puts it there, and the one of the installation is not touched.
qboolean WA_SaveConfig( const char *text, int len ) {
	const char	*file = WA_ConfigFile();
	int			oldLen = 0;
	char		*old = WA_ReadGameFile( file, &oldLen );

	if ( old ) {
		WA_WriteGameFile( va( "%s.bak", file ), old, oldLen );
		free( old );
	}
	return WA_WriteGameFile( file, text, len );
}

static const waSetting_t *WA_SettingFind( const char *name ) {
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( waSettings ); i++ ) {
		if ( !Q_stricmp( waSettings[i].name, name ) )
			return &waSettings[i];
	}
	return NULL;
}

// The field of a cvar, qfalse when the form does not have it: read only or not there.
static qboolean WA_FieldGet( const char *name, waField_t *f ) {
	const char		*gameName, *gameDef;
	uint32_t		gameFlags;
	qboolean		inGame = qfalse;
	int				i;

	memset( f, 0, sizeof( *f ) );
	f->name = name;
	f->def = "";
	f->setting = WA_SettingFind( name );
	for ( i = 0; G_CvarTableEntry( i, &gameName, &gameDef, &gameFlags ); i++ ) {
		if ( !Q_stricmp( gameName, name ) ) {
			f->name = gameName;
			f->def = gameDef;
			f->flags = gameFlags;
			inGame = qtrue;
			break;
		}
	}
	if ( f->setting )
		f->name = f->setting->name;
	else if ( !inGame )
		return qfalse;

	// an engine that says what the flags are (g_public.h, ext.Cvar_Flags)
	if ( trap->ext.Cvar_Flags ) {
		const uint32_t flags = trap->ext.Cvar_Flags( f->name );

		if ( flags == CVAR_NONEXISTENT )
			return qfalse;
		f->flags = flags;
	}
	// (CVAR_PROTECTED only keeps a module from setting it, the console that sets it here may: sv_master1 and on)
	if ( f->flags & CVAR_ROM )
		return qfalse;

	if ( ( f->flags & CVAR_INIT ) || ( f->setting && ( f->setting->flags & WA_SET_RESTART ) ) )
		f->apply = WA_APPLY_RESTART;
	else if ( ( f->flags & CVAR_LATCH ) || ( f->setting && ( f->setting->flags & WA_SET_LATCH ) ) )
		f->apply = WA_APPLY_MAP;
	else
		f->apply = WA_APPLY_LIVE;
	return qtrue;
}

// the config as it is now, with a 0 at its end. NULL: none yet, *len 0
static char *WA_ConfigRead( int *len ) {
	char	*file, *text;
	int		fileLen = 0;

	file = WA_ReadGameFile( WA_ConfigFile(), &fileLen );
	text = (char *)malloc( fileLen + 1 );
	if ( !text ) {
		free( file );
		*len = 0;
		return NULL;
	}
	if ( file )
		memcpy( text, file, fileLen );
	text[fileLen] = '\0';
	free( file );
	*len = fileLen;
	return text;
}

// local is the text of local.cfg, NULL when there is none
static void WA_FieldJson( waBuffer_t *b, const waField_t *f, const char *config, int configLen, const char *local, int localLen ) {
	const waSetting_t	*s = f->setting;
	char				value[MAX_CVAR_VALUE_STRING], fileValue[MAX_CVAR_VALUE_STRING];
	const qboolean		inFile = WA_CfgGetValue( config, configLen, f->name, fileValue, sizeof( fileValue ) );

	trap->Cvar_VariableStringBuffer( f->name, value, sizeof( value ) );
	WA_BufAppend( b, "{\"name\":", 8 );
	WA_BufJsonString( b, f->name );
	WA_BufPrintf( b, ",\"cat\":%d,\"type\":\"%s\"", s ? s->cat : WA_CAT_OTHER, waTypeNames[s ? s->type : WA_SET_TEXT] );
	if ( s && ( s->min || s->max ) )
		WA_BufPrintf( b, ",\"min\":%g,\"max\":%g", s->min, s->max );
	if ( s && s->options ) {
		WA_BufAppend( b, ",\"opts\":", 8 );
		WA_BufJsonString( b, s->options );
	}
	if ( s && s->desc ) {
		WA_BufAppend( b, ",\"desc\":", 8 );
		WA_BufJsonString( b, s->desc );
	}
	WA_BufAppend( b, ",\"def\":", 7 );
	WA_BufJsonString( b, f->def );
	WA_BufAppend( b, ",\"value\":", 9 );
	WA_BufJsonString( b, value );
	if ( inFile ) {
		WA_BufAppend( b, ",\"file\":", 8 );
		WA_BufJsonString( b, fileValue );
	}
	if ( local && WA_CfgGetValue( local, localLen, f->name, fileValue, sizeof( fileValue ) ) ) {
		WA_BufAppend( b, ",\"local\":", 9 );
		WA_BufJsonString( b, fileValue );
	}
	WA_BufPrintf( b, ",\"apply\":\"%s\"}", waApplyNames[f->apply] );
}

/*
=================
WA_ApiSettingsGet

GET /api/settings: { file, categories, cvars: [ { name, cat, type, min?, max?, opts?, desc?, def, value,
file?, local?, apply } ] }. file of a cvar is what the config gives it, missing when it does not set it. local is
what local.cfg gives it: the file of the owner that the service of the host runs after the config
(deploy/server-local.cfg of the website), which sets it again at the next start.
=================
*/
void WA_ApiSettingsGet( waConnection_t *c ) {
	waBuffer_t	b = { 0 };
	waField_t	f;
	int			configLen, localLen = 0, i, count = 0;
	char		*config = WA_ConfigRead( &configLen );
	char		*local = WA_ReadGameFile( "local.cfg", &localLen );
	const char	*name, *def;
	uint32_t	flags;

	WA_BufPrintf( &b, "{\"file\":\"%s\",\"flags\":%s,\"categories\":[", WA_ConfigFile(),
		 WA_BOOL( trap->ext.Cvar_Flags != NULL ) );
	for ( i = 0; i < WA_CAT_COUNT; i++ ) {
		if ( i )
			WA_BufAppend( &b, ",", 1 );
		WA_BufJsonString( &b, waCatNames[i] );
	}
	WA_BufAppend( &b, "],\"cvars\":[", 11 );

	// the table first, in its order, then the cvars of the game module it does not have
	for ( i = 0; i < (int)ARRAY_LEN( waSettings ); i++ ) {
		if ( !WA_FieldGet( waSettings[i].name, &f ) )
			continue;
		if ( count++ )
			WA_BufAppend( &b, ",", 1 );
		WA_FieldJson( &b, &f, config ? config : "", configLen, local, localLen );
	}
	for ( i = 0; G_CvarTableEntry( i, &name, &def, &flags ); i++ ) {
		if ( WA_SettingFind( name ) || !WA_FieldGet( name, &f ) )
			continue;
		if ( count++ )
			WA_BufAppend( &b, ",", 1 );
		WA_FieldJson( &b, &f, config ? config : "", configLen, local, localLen );
	}
	WA_BufAppend( &b, "]}", 2 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
	free( config );
	free( local );
}

// The value as the form may set it: no quote and no control character, a number within its limits, a choice of
// its list. qfalse with why.
static qboolean WA_SettingValue( const waField_t *f, const char *in, char *out, int outSize, const char **why ) {
	const waSetting_t	*s = f->setting;
	const unsigned char	*p;

	for ( p = (const unsigned char *)in; *p; p++ ) {
		if ( *p < ' ' || *p == '"' || *p == 0x7f ) {
			*why = "no quotes and no control characters";
			return qfalse;
		}
	}
	if ( strlen( in ) >= MAX_CVAR_VALUE_STRING ) {
		*why = "too long";
		return qfalse;
	}
	Q_strncpyz( out, in, outSize );
	if ( !s )
		return qtrue;

	switch ( s->type ) {
	case WA_SET_INT:
	case WA_SET_BITS:
	case WA_SET_FLOAT: {
		char		*end;
		// a double: a float holds 24 bits, so a set of bits above 1 << 24 (jp_councilAllowedCMD) lost its low bits
		double		clamped = strtod( in, &end );

		if ( !in[0] || *end ) {
			*why = "not a number";
			return qfalse;
		}
		// min and max both 0: any value. A max not above the min: no upper limit
		if ( s->min || s->max ) {
			if ( clamped < s->min )
				clamped = s->min;
			if ( s->max > s->min && clamped > s->max )
				clamped = s->max;
		}
		if ( s->type != WA_SET_FLOAT ) {
			if ( clamped > INT_MAX )
				clamped = INT_MAX;
			if ( clamped < INT_MIN )
				clamped = INT_MIN;
		}
		if ( s->type == WA_SET_FLOAT )
			Com_sprintf( out, outSize, "%g", clamped );
		else
			Com_sprintf( out, outSize, "%d", (int)clamped );
		if ( s->type == WA_SET_BITS && clamped < 0 ) {
			*why = "not a set of bits";
			return qfalse;
		}
		return qtrue;
	}
	case WA_SET_BOOL:
		if ( strcmp( in, "0" ) && strcmp( in, "1" ) ) {
			*why = "0 or 1";
			return qfalse;
		}
		return qtrue;
	case WA_SET_ENUM: {
		// "0=Free for all|1=Holocron|..."
		const char		*o = s->options;
		const size_t	len = strlen( in );

		while ( o && *o ) {
			if ( !strncmp( o, in, len ) && o[len] == '=' )
				return qtrue;
			o = strchr( o, '|' );
			if ( o )
				o++;
		}
		*why = "not one of the choices";
		return qfalse;
	}
	default:
		return qtrue;
	}
}

/*
=================
WA_ApiSettingsSet

POST /api/settings, a line "name<tab>value" for every cvar that changed. Each one goes into the config, the last
statement that sets it changed in place or a line added at the end, and the config is written once with a .bak
of what was there. Then each is set on the console as "set name value", which the engine checks as it checks
the console: what the server reads only at its start is in the file and not set. The answer has a line for each.
=================
*/
void WA_ApiSettingsSet( waConnection_t *c, char *body ) {
	waBuffer_t	b = { 0 }, names = { 0 };
	int			len, changed = 0, refused = 0;
	char		*config = WA_ConfigRead( &len ), *line, *next;

	if ( !config ) {
		WA_ActionResult( c, qfalse, "out of memory" );
		return;
	}
	WA_TextBody( body );
	WA_BufAppend( &b, "{\"ok\":true,\"results\":[", 22 );

	for ( line = body; line && *line; line = next ) {
		char		*tab, value[MAX_CVAR_VALUE_STRING];
		const char	*why = NULL;
		waField_t	f;

		next = strchr( line, '\n' );
		if ( next )
			*next++ = '\0';
		tab = strchr( line, '\t' );
		if ( !tab )
			continue;
		*tab = '\0';

		if ( !WA_FieldGet( line, &f ) ) {
			why = "not a setting of this server";
		} else if ( WA_SettingValue( &f, tab + 1, value, sizeof( value ), &why ) ) {
			char		*out = (char *)malloc( len + strlen( f.name ) + sizeof( value ) + 64 );
			const int	outLen = out ? WA_CfgSetValue( config, len, out, len + (int)strlen( f.name ) + (int)sizeof( value ) + 64, f.name, value ) : -1;

			if ( outLen < 0 || outLen > WA_CONFIG_MAX ) {
				free( out );
				why = "the config would be too long";
			} else {
				out[outLen] = '\0';
				free( config );
				config = out;
				len = outLen;
				if ( f.apply != WA_APPLY_RESTART )
					trap->SendConsoleCommand( EXEC_APPEND, va( "set %s \"%s\"\n", f.name, value ) );
				WA_BufPrintf( &names, "%s%s", changed ? ", " : "", f.name );
				changed++;
			}
		}

		if ( b.data[b.len - 1] != '[' )
			WA_BufAppend( &b, ",", 1 );
		WA_BufAppend( &b, "{\"name\":", 8 );
		WA_BufJsonString( &b, line );
		if ( why ) {
			WA_BufAppend( &b, ",\"state\":\"refused\",\"why\":", 25 );
			WA_BufJsonString( &b, why );
			WA_BufAppend( &b, "}", 1 );
			refused++;
		} else {
			WA_BufPrintf( &b, ",\"state\":\"%s\",\"value\":", waApplyNames[f.apply] );
			WA_BufJsonString( &b, value );
			WA_BufAppend( &b, "}", 1 );
		}
	}

	if ( changed && !WA_SaveConfig( config, len ) ) {
		WA_BufFree( &b );
		WA_BufFree( &names );
		free( config );
		WA_ActionResult( c, qfalse, va( "could not write %s", WA_ConfigFile() ) );
		return;
	}
	if ( changed )
		// the names, not the values: some are passwords
		WA_LogUse( c, va( "settings saved into %s: %s", WA_ConfigFile(), names.data ) );
	WA_BufPrintf( &b, "],\"message\":\"%d saved%s\"}", changed, refused ? va( ", %d refused", refused ) : "" );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
	WA_BufFree( &names );
	free( config );
}

/*
==============================================================================

Cleanup

Two kinds of statements in the config that do nothing useful: those of cvars no code has used for a month (the
engine knows which, cvar_cleanup.cpp, and has them as user created cvars, also when only the console set them),
and every statement of a cvar but the last one, which the last one overrides.

==============================================================================
*/

// the text of the config as the page saw it, so it does not take out lines of a file that changed since
static unsigned WA_ConfigRev( const char *text, int len ) {
	unsigned	h = 2166136261u;
	int			i;

	for ( i = 0; i < len; i++ )
		h = ( h ^ (unsigned char)text[i] ) * 16777619u;
	return h;
}

typedef struct waSetter_s {
	char	name[64];
	int		line;
	char	value[MAX_CVAR_VALUE_STRING];
} waSetter_t;

// the statements of the config that set a cvar, at most max
static int WA_ConfigSetters( const char *text, int len, waSetter_t *out, int max ) {
	waCfgStatement_t	st;
	int					pos = 0, line = 1, n = 0;

	while ( n < max && WA_CfgNext( text, len, &pos, &line, &st ) ) {
		if ( !WA_CfgSetter( text, &st, out[n].name, sizeof( out[n].name ) ) )
			continue;
		out[n].line = st.line;
		WA_CfgValue( text, &st, 2, out[n].value, sizeof( out[n].value ) );
		n++;
	}
	return n;
}

static void WA_CleanupItem( waBuffer_t *b, qboolean *first, const char *group, const char *name, const char *value,
	int line, int day ) {
	if ( !*first )
		WA_BufAppend( b, ",", 1 );
	*first = qfalse;
	WA_BufPrintf( b, "{\"group\":\"%s\",\"line\":%d,\"day\":%d,\"name\":", group, line, day );
	WA_BufJsonString( b, name );
	WA_BufAppend( b, ",\"value\":", 9 );
	WA_BufJsonString( b, value );
	WA_BufAppend( b, "}", 1 );
}

/*
=================
WA_ApiCleanupGet

GET /api/settings/cleanup: { available, rev, today, items: [ { group: "unused" | "overridden", line, day, name,
value } ] }. line 0: the cvar is set, but not by the config. day: the day code used it last, in days since 1970,
0 for never.
=================
*/
void WA_ApiCleanupGet( waConnection_t *c ) {
	waBuffer_t	b = { 0 };
	waSetter_t	*setters;
	char		*config, *list, *line, *next;
	int			len, count, i, j;
	qboolean	first = qtrue;

	if ( !trap->ext.Cvar_UnusedList ) {
		WA_RespondJsonText( c, "{\"available\":false}" );
		return;
	}
	config = WA_ConfigRead( &len );
	setters = (waSetter_t *)malloc( WA_CLEANUP_MOST * sizeof( *setters ) );
	list = (char *)malloc( WA_CLEANUP_LIST );
	if ( !config || !setters || !list ) {
		free( config );
		free( setters );
		free( list );
		WA_ActionResult( c, qfalse, "out of memory" );
		return;
	}
	count = WA_ConfigSetters( config, len, setters, WA_CLEANUP_MOST );
	trap->ext.Cvar_UnusedList( list, WA_CLEANUP_LIST );

	WA_BufPrintf( &b, "{\"available\":true,\"rev\":\"%08x\",\"today\":%d,\"items\":[", WA_ConfigRev( config, len ),
		(int)( time( NULL ) / ( 24 * 60 * 60 ) ) );

	// "name<tab>value<tab>day" a line
	for ( line = list; *line; line = next ) {
		char		*value, *dayText;
		qboolean	inFile = qfalse;

		next = strchr( line, '\n' );
		if ( !next )
			break;
		*next++ = '\0';
		value = strchr( line, '\t' );
		dayText = value ? strchr( value + 1, '\t' ) : NULL;
		if ( !dayText )
			continue;
		*value++ = '\0';
		*dayText++ = '\0';
		for ( i = 0; i < count; i++ ) {
			if ( !Q_stricmp( setters[i].name, line ) ) {
				WA_CleanupItem( &b, &first, "unused", setters[i].name, setters[i].value, setters[i].line, atoi( dayText ) );
				inFile = qtrue;
			}
		}
		if ( !inFile )
			WA_CleanupItem( &b, &first, "unused", line, value, 0, atoi( dayText ) );
	}

	// a statement a later one of the same cvar overrides
	for ( i = 0; i < count; i++ ) {
		for ( j = i + 1; j < count; j++ ) {
			if ( !Q_stricmp( setters[i].name, setters[j].name ) )
				break;
		}
		if ( j < count )
			WA_CleanupItem( &b, &first, "overridden", setters[i].name, setters[i].value, setters[i].line, 0 );
	}
	WA_BufAppend( &b, "]}", 2 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
	free( config );
	free( setters );
	free( list );
}

#define WA_CLEANUP_PICKS	512

typedef struct waPicks_s {
	int		count;
	int		line[WA_CLEANUP_PICKS];
	char	name[WA_CLEANUP_PICKS][MAX_CVAR_VALUE_STRING];
} waPicks_t;

static qboolean WA_CleanupPick( const char *text, const waCfgStatement_t *st, void *context ) {
	const waPicks_t	*picks = (const waPicks_t *)context;
	char			name[MAX_CVAR_VALUE_STRING];
	int				i;

	if ( !WA_CfgSetter( text, st, name, sizeof( name ) ) )
		return qfalse;
	for ( i = 0; i < picks->count; i++ ) {
		if ( picks->line[i] == st->line && !Q_stricmp( picks->name[i], name ) )
			return qtrue;
	}
	return qfalse;
}

/*
=================
WA_ApiCleanupSet

POST /api/settings/cleanup. The first line is the rev of the config the page listed, then a line
"line<tab>name<tab>unset" for each statement that goes: line 0 is a cvar no line of the config sets, unset 1
takes the cvar off the server too (cvar_cleanup remove, which only takes user created ones and writes the config
of the engine again without them).
=================
*/
void WA_ApiCleanupSet( waConnection_t *c, char *body ) {
	waPicks_t	*picks = (waPicks_t *)calloc( 1, sizeof( waPicks_t ) );
	waBuffer_t	unset = { 0 };
	char		*config, *out, *line, *next, rev[16];
	int			len, outLen, removed = 0;

	config = WA_ConfigRead( &len );
	if ( !picks || !config ) {
		free( picks );
		free( config );
		WA_ActionResult( c, qfalse, "out of memory" );
		return;
	}
	WA_TextBody( body );
	next = strchr( body, '\n' );
	if ( next )
		*next++ = '\0';
	Com_sprintf( rev, sizeof( rev ), "%08x", WA_ConfigRev( config, len ) );
	if ( strcmp( body, rev ) ) {
		free( picks );
		free( config );
		WA_ActionResult( c, qfalse, va( "%s changed since it was listed, list it again", WA_ConfigFile() ) );
		return;
	}

	for ( line = next; line && *line; line = next ) {
		char	*name, *flag;
		int		n;

		next = strchr( line, '\n' );
		if ( next )
			*next++ = '\0';
		name = strchr( line, '\t' );
		flag = name ? strchr( name + 1, '\t' ) : NULL;
		if ( !flag )
			continue;
		*name++ = '\0';
		*flag++ = '\0';
		n = atoi( line );
		if ( n > 0 && picks->count < WA_CLEANUP_PICKS ) {
			picks->line[picks->count] = n;
			Q_strncpyz( picks->name[picks->count], name, sizeof( picks->name[0] ) );
			picks->count++;
			removed++;
		}
		// a name is a word of the console: no blank, no quote, no ';'
		if ( flag[0] == '1' && name[0] && !strpbrk( name, " \t\";/" ) )
			WA_BufPrintf( &unset, " %s", name );
	}

	if ( picks->count ) {
		out = (char *)malloc( len + 1 );
		outLen = out ? WA_CfgRemove( config, len, out, len + 1, WA_CleanupPick, picks ) : -1;
		if ( outLen < 0 || !WA_SaveConfig( out, outLen ) ) {
			free( out );
			free( picks );
			free( config );
			WA_BufFree( &unset );
			WA_ActionResult( c, qfalse, va( "could not write %s", WA_ConfigFile() ) );
			return;
		}
		free( out );
		WA_LogUse( c, va( "cleanup: %d statements taken out of %s", removed, WA_ConfigFile() ) );
	}
	if ( unset.len ) {
		trap->SendConsoleCommand( EXEC_APPEND, va( "cvar_cleanup remove%s\n", unset.data ) );
		WA_LogUse( c, va( "cleanup: cvar_cleanup remove%s", unset.data ) );
	}
	WA_ActionResult( c, qtrue, va( "%d statements taken out of %s%s", removed, WA_ConfigFile(), unset.len ? ", the cvars removed from the server" : "" ) );
	free( picks );
	free( config );
	WA_BufFree( &unset );
}
