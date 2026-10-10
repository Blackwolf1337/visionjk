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

// wa_remaps.c -- the texture swap of the web admin panel: a shader of the map drawn as another one, for everybody

#include "game/webadmin/wa_local.h"

/*
=================
The texture swap

Not in JKAdmin. The game has it for the entities of a map (targetShaderName and targetShaderNewName,
G_UseTargets): the server keeps a table of shader names and what each is drawn as, and tells it to every client
in one configstring (CS_SHADERSTATE), also to one that joins later. Every client swaps the shader by itself
(CG_ShaderStateChanged, R_RemapShader), the unchanged game too. The panel fills the same table.

What it can not do, on any client: swap one wall. A shader is swapped wherever the map has it. And a client
only draws what it has: a name it does not know is left as it is there.

On a client that is not of this tree a swapped wall that is lit by a lightmap loses it: the renderer points
every lightmap of the old shader at one copy of the new one. That stays wrong there after the swap is taken
back, until the map is loaded again. A renderer of this tree keeps the lightmaps (R_RemapShader).

A swap that is taken back stays in the table with both names the same, which is how a client is told, and is
not written to the file. The file of a map is remaps/<map>.cfg: the old name and the new one, a swap a line.

The configstring is part of what a client gets when it connects, 16000 characters for everything (the
players, the models, the sounds): the swaps get WA_REMAP_BUDGET of them.

GET /api/remaps: the swaps, how much of the room they take, and whether something changed since the last save.
POST /api/remap: op set (old, new), clear (old), save, revert (what the file has).
=================
*/
#define WA_REMAP_BUDGET		1000
#define WA_REMAP_MAXFILE	0x4000
#define WA_REMAP_ENTRY		8		// what BuildShaderStateConfig() writes besides the two names: "=", ":", " 0.00", "@"
#define WA_REMAP_OWNED		128

// The swaps the panel made or read from the file of the map, by the old name. A map may swap shaders by itself
// (a button that changes a screen): those are shown and can be taken back, and are not written to the file.
static char		remapOwned[WA_REMAP_OWNED][MAX_QPATH];
static int		remapOwnedCount;
static qboolean	remapsDirty;

// A name as it may go into the configstring: CG_ShaderStateChanged() cuts that at '=', ':' and '@' and copies
// what is between them into buffers of MAX_QPATH, in every client. No blank, no quote, nothing that ends a line
// of the console.
static qboolean WA_RemapName( const char *in, char *out, int outSize ) {
	int n = 0;

	for ( ; *in; in++ ) {
		const unsigned char ch = (unsigned char)*in;

		if ( ch <= ' ' || ch >= 127 || strchr( "=:@\"\\;%", ch ) || n >= outSize - 1 )
			return qfalse;
		out[n++] = (char)ch;
	}
	out[n] = '\0';
	return (qboolean)( n > 0 );
}

// what a shader is drawn as, qfalse when the table does not have it
static qboolean WA_RemapFind( const char *oldShader, const char **newShader ) {
	const char	*name;
	int			i;

	for ( i = 0; G_GetRemap( i, &name, newShader ); i++ ) {
		if ( !Q_stricmp( name, oldShader ) )
			return qtrue;
	}
	return qfalse;
}

static qboolean WA_RemapIsOwned( const char *oldShader ) {
	int i;

	for ( i = 0; i < remapOwnedCount; i++ ) {
		if ( !Q_stricmp( remapOwned[i], oldShader ) )
			return qtrue;
	}
	return qfalse;
}

static int WA_RemapUsed( void ) {
	return (int)strlen( BuildShaderStateConfig() );
}

// A swap of the panel. NULL when it is in the table, else why it is not.
static const char *WA_RemapSet( const char *oldShader, const char *newShader ) {
	const qboolean	owned = WA_RemapIsOwned( oldShader );
	const char		*was;
	int				used = WA_RemapUsed();

	if ( WA_RemapFind( oldShader, &was ) )
		used += (int)strlen( newShader ) - (int)strlen( was );
	else
		used += (int)strlen( oldShader ) + (int)strlen( newShader ) + WA_REMAP_ENTRY;
	if ( used > WA_REMAP_BUDGET || ( !owned && remapOwnedCount == WA_REMAP_OWNED ) )
		return "no room for another swap. What was taken back gives its room back with the next map.";
	AddRemap( oldShader, newShader, 0 );
	// the table of the game is full when it did not take it
	if ( !WA_RemapFind( oldShader, &was ) || Q_stricmp( was, newShader ) )
		return "the table of the swaps is full";
	if ( !owned )
		Q_strncpyz( remapOwned[remapOwnedCount++], oldShader, sizeof( remapOwned[0] ) );
	return NULL;
}

static void WA_RemapSend( void ) {
	trap->SetConfigstring( CS_SHADERSTATE, BuildShaderStateConfig() );
}

// '/' in the name of a map is a folder, as in wallblocks/
static void WA_RemapFile( char *file, int fileSize ) {
	char mapname[MAX_QPATH];

	trap->Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
	Com_sprintf( file, fileSize, "remaps/%s.cfg", mapname );
}

/*
=================
WebAdmin_LoadRemaps

The swaps the file of the map has: at the start of a map, whether the panel runs or not, and when the panel
goes back to what is saved. Returns how many.
=================
*/
int WebAdmin_LoadRemaps( void ) {
	static char		buf[WA_REMAP_MAXFILE];
	char			file[MAX_QPATH], oldShader[MAX_QPATH], newShader[MAX_QPATH];
	fileHandle_t	f = 0;
	const char		*p;
	int				len, count = 0;

	WA_RemapFile( file, sizeof( file ) );
	len = trap->FS_Open( file, &f, FS_READ );
	if ( !f )
		return 0;
	if ( len <= 0 || len >= (int)sizeof( buf ) ) {
		trap->FS_Close( f );
		if ( len > 0 )
			Com_Printf( S_COLOR_YELLOW "%s is too long, no texture is swapped\n", file );
		return 0;
	}
	trap->FS_Read( buf, len, f );
	trap->FS_Close( f );
	buf[len] = '\0';

	p = buf;
	COM_BeginParseSession( file );
	while ( 1 ) {
		const char	*token = COM_ParseExt( &p, qtrue );
		const char	*why;
		qboolean	ok;

		if ( !token[0] )
			break;
		ok = WA_RemapName( token, oldShader, sizeof( oldShader ) );
		token = COM_ParseExt( &p, qfalse );
		ok = (qboolean)( ok && WA_RemapName( token, newShader, sizeof( newShader ) ) );
		SkipRestOfLine( &p );
		why = ok ? WA_RemapSet( oldShader, newShader ) : "not two names of shaders";
		if ( why ) {
			Com_Printf( S_COLOR_YELLOW "%s, line %d: %s\n", file, COM_GetCurrentParseLine(), why );
			continue;
		}
		count++;
	}
	if ( count )
		WA_RemapSend();
	return count;
}

static qboolean WA_RemapSave( void ) {
	waBuffer_t	b = { 0 };
	char		file[MAX_QPATH];
	const char	*oldShader, *newShader;
	qboolean	ok;
	int			i;

	WA_BufPrintf( &b, "// the texture swaps of this map, written by the web admin panel: a shader of the map and what it is drawn as\n" );
	for ( i = 0; G_GetRemap( i, &oldShader, &newShader ); i++ ) {
		if ( WA_RemapIsOwned( oldShader ) && Q_stricmp( oldShader, newShader ) )
			WA_BufPrintf( &b, "%s %s\n", oldShader, newShader );
	}
	WA_RemapFile( file, sizeof( file ) );
	ok = (qboolean)( b.data && WA_WriteGameFile( file, b.data, b.len ) );
	WA_BufFree( &b );
	return ok;
}

void WA_ApiRemaps( waConnection_t *c ) {
	waBuffer_t	b = { 0 };
	const char	*oldShader, *newShader;
	qboolean	first = qtrue;
	int			i;

	WA_BufPrintf( &b, "{\"dirty\":%s,\"used\":%d,\"budget\":%d,\"remaps\":[", WA_BOOL( remapsDirty ), WA_RemapUsed(), WA_REMAP_BUDGET );
	for ( i = 0; G_GetRemap( i, &oldShader, &newShader ); i++ ) {
		if ( !Q_stricmp( oldShader, newShader ) )
			continue;
		WA_BufPrintf( &b, "%s{\"old\":", first ? "" : "," );
		WA_BufJsonString( &b, oldShader );
		WA_BufAppend( &b, ",\"new\":", 7 );
		WA_BufJsonString( &b, newShader );
		WA_BufPrintf( &b, ",\"own\":%s}", WA_BOOL( WA_RemapIsOwned( oldShader ) ) );
		first = qfalse;
	}
	WA_BufAppend( &b, "]}", 2 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

void WA_ApiRemap( waConnection_t *c, const char *body ) {
	static const char *const badName = "a name of a shader: up to 63 characters, no blank and none of = : @ \" ; % \\";
	char		op[16], value[MAX_QPATH * 2], oldShader[MAX_QPATH], newShader[MAX_QPATH];
	const char	*why, *was, *name;
	int			i, count;

	WA_FormValue( body, "op", op, sizeof( op ) );
	WA_LogUse( c, va( "remap %.15s %.200s", op, strchr( body, '&' ) ? strchr( body, '&' ) + 1 : "" ) );

	if ( !strcmp( op, "set" ) || !strcmp( op, "clear" ) ) {
		if ( !WA_FormValue( body, "old", value, sizeof( value ) ) || !WA_RemapName( value, oldShader, sizeof( oldShader ) ) ) {
			WA_ActionResult( c, qfalse, va( "the shader of the map is missing or not %s", badName ) );
			return;
		}
		if ( !strcmp( op, "clear" ) ) {
			if ( !WA_RemapFind( oldShader, &was ) || !Q_stricmp( was, oldShader ) ) {
				WA_ActionResult( c, qfalse, "this shader is not swapped" );
				return;
			}
			AddRemap( oldShader, oldShader, 0 );
			WA_RemapSend();
			if ( WA_RemapIsOwned( oldShader ) )
				remapsDirty = qtrue;
			WA_ActionResult( c, qtrue, "Swap taken back." );
			return;
		}
		if ( !WA_FormValue( body, "new", value, sizeof( value ) ) || !WA_RemapName( value, newShader, sizeof( newShader ) ) ) {
			WA_ActionResult( c, qfalse, va( "what to draw it as is missing or not %s", badName ) );
			return;
		}
		if ( !Q_stricmp( oldShader, newShader ) ) {
			WA_ActionResult( c, qfalse, "that is the shader itself" );
			return;
		}
		why = WA_RemapSet( oldShader, newShader );
		if ( why ) {
			WA_ActionResult( c, qfalse, why );
			return;
		}
		WA_RemapSend();
		remapsDirty = qtrue;
		WA_ActionResult( c, qtrue, "Swapped for every player. A game only draws a shader it has." );
	} else if ( !strcmp( op, "save" ) ) {
		if ( !WA_RemapSave() ) {
			WA_ActionResult( c, qfalse, "the file of the swaps could not be written" );
			return;
		}
		remapsDirty = qfalse;
		WA_ActionResult( c, qtrue, "Saved. The swaps come back with the map." );
	} else if ( !strcmp( op, "revert" ) ) {
		// what the panel set is taken back, then what the file says. What the map swapped by itself stays.
		for ( i = 0; G_GetRemap( i, &name, &was ); i++ ) {
			if ( WA_RemapIsOwned( name ) && Q_stricmp( name, was ) )
				AddRemap( name, name, 0 );
		}
		count = WebAdmin_LoadRemaps();
		WA_RemapSend();
		remapsDirty = qfalse;
		WA_ActionResult( c, qtrue, va( "Back to what is saved: %d swap%s", count, count == 1 ? "" : "s" ) );
	} else {
		WA_ActionResult( c, qfalse, "unknown op" );
	}
}
