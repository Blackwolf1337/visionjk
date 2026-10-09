/*
===========================================================================
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

// wa_assets.c -- what the page of the web admin panel draws with and picks from: the map file,
// the files of the game (textures, models, shader scripts, sounds), the names of the animations, the lists of
// maps, effects, NPCs and bots, and the list of the models, which is looked for a little in every frame.

#include "game/webadmin/wa_local.h"
#include "qcommon/qfiles.h"		// the header of a .bsp

extern stringID_table_t animTable[MAX_ANIMATIONS + 1];
extern const char *eventnames[EV_NUM_ENTITY_EVENTS];	// bg_misc.c

static int		mapModels = -1;				// brush models of the map, asked once (WA_MapBrushModels)
static char		mapModelsName[MAX_QPATH];

// How many brush models the map has, the world being the first: *1 to one less than this can be named. -1: not
// known. Asked of the head of the .bsp, once a map. (The whole file was read and kept for this and for
// /api/map before, 7 to 20 MB in one frame of 8 to 12 ms.)
int WA_MapBrushModels( void ) {
	char			mapname[MAX_QPATH];
	fileHandle_t	f = 0;
	dheader_t		header;
	int				len;

	trap->Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
	if ( !Q_stricmp( mapModelsName, mapname ) )
		return mapModels;

	Q_strncpyz( mapModelsName, mapname, sizeof( mapModelsName ) );
	mapModels = -1;
	len = trap->FS_Open( va( "maps/%s.bsp", mapname ), &f, FS_READ );
	if ( f ) {
		if ( len >= (int)sizeof( header ) ) {
			trap->FS_Read( &header, sizeof( header ), f );
			mapModels = LittleLong( header.lumps[LUMP_MODELS].filelen ) / (int)sizeof( dmodel_t );
		}
		trap->FS_Close( f );
	}
	return mapModels;
}

// the .bsp of the map that runs, handed out as it is read, like every other file of the game (WA_FileMore)
void WA_ApiMap( waConnection_t *c ) {
	char			mapname[MAX_QPATH];
	fileHandle_t	f = 0;
	int				len;

	trap->Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
	len = trap->FS_Open( va( "maps/%s.bsp", mapname ), &f, FS_READ );
	if ( len <= 0 || !f || len > WA_MAX_FILE ) {
		if ( f )
			trap->FS_Close( f );
		WA_RespondText( c, 404, "Not Found", "map not found" );
		return;
	}
	WA_RespondHead( c, 200, "OK", "application/octet-stream", "Cache-Control: no-store\r\n", len );
	c->file = f;
	c->fileLeft = len;
}

// Textures, models and skins for the 3D views, the shader scripts the view of the administration draws the
// map with, the effect files the page plays, and what the HUD of the game is made of. Only below textures/,
// models/ and gfx/, of shaders/ the scripts, of effects/ the effect files, of ui/ the menu files with their
// lists and images, of fonts/ the fonts and of strings/ the texts.
void WA_ApiFile( waConnection_t *c, const char *query ) {
	char			name[MAX_QPATH];
	const char		*ext, *type;
	fileHandle_t	f = 0;
	int				len;
	qboolean		script, audio, hud;

	if ( !WA_FormValue( query, "name", name, sizeof( name ) ) || strstr( name, ".." ) || strchr( name, '\\' ) || strchr( name, ':' ) ) {
		WA_RespondText( c, 400, "Bad Request", "bad file name" );
		return;
	}
	ext = COM_GetExtension( name );
	script = (qboolean)( ( !Q_stricmpn( name, "shaders/", 8 ) && !Q_stricmp( ext, "shader" ) )
		|| ( !Q_stricmpn( name, "effects/", 8 ) && !Q_stricmp( ext, "efx" ) ) );
	// a sound or a piece of music, which the editor plays in the browser to hear what a key names
	audio = (qboolean)( ( !Q_stricmpn( name, "sound/", 6 ) || !Q_stricmpn( name, "music/", 6 ) ) && ( !Q_stricmp( ext, "wav" ) || !Q_stricmp( ext, "mp3" ) ) );
	// The HUD (CG_LoadHudMenu reads ui/jahud.txt and the menu files it names) with its images, the fonts (a
	// .fontdat with the image of its glyphs) and the texts a menu names with an @ (strings/<language>/*.str).
	hud = (qboolean)( ( !Q_stricmpn( name, "ui/", 3 ) && ( !Q_stricmp( ext, "menu" ) || !Q_stricmp( ext, "txt" ) || !Q_stricmp( ext, "tga" )
			|| !Q_stricmp( ext, "jpg" ) || !Q_stricmp( ext, "png" ) ) )
		|| ( !Q_stricmpn( name, "fonts/", 6 ) && ( !Q_stricmp( ext, "fontdat" ) || !Q_stricmp( ext, "tga" ) || !Q_stricmp( ext, "jpg" )
			|| !Q_stricmp( ext, "png" ) ) )
		|| ( !Q_stricmpn( name, "strings/", 8 ) && !Q_stricmp( ext, "str" ) ) );
	if ( !script && !audio && !hud && Q_stricmpn( name, "textures/", 9 ) && Q_stricmpn( name, "models/", 7 ) && Q_stricmpn( name, "gfx/", 4 ) ) {
		WA_RespondText( c, 400, "Bad Request", "bad file name" );
		return;
	}

	if ( script || !Q_stricmp( ext, "menu" ) || !Q_stricmp( ext, "txt" ) || !Q_stricmp( ext, "str" ) ) type = "text/plain; charset=utf-8";
	else if ( !Q_stricmp( ext, "fontdat" ) ) type = "application/octet-stream";
	else if ( audio ) type = Q_stricmp( ext, "mp3" ) ? "audio/wav" : "audio/mpeg";
	else if ( !Q_stricmp( ext, "jpg" ) ) type = "image/jpeg";
	else if ( !Q_stricmp( ext, "gla" ) || !Q_stricmp( ext, "cfg" ) ) type = "application/octet-stream";
	else if ( !Q_stricmp( ext, "png" ) ) type = "image/png";
	else if ( !Q_stricmp( ext, "tga" ) || !Q_stricmp( ext, "md3" ) || !Q_stricmp( ext, "glm" ) ) type = "application/octet-stream";
	else if ( !Q_stricmp( ext, "skin" ) ) type = "text/plain; charset=utf-8";
	else {
		WA_RespondText( c, 400, "Bad Request", "only images, md3, glm and skin files" );
		return;
	}

	len = trap->FS_Open( name, &f, FS_READ );
	if ( len <= 0 || !f || len > WA_MAX_FILE ) {
		if ( f )
			trap->FS_Close( f );
		WA_RespondText( c, 404, "Not Found", "not found" );
		return;
	}
	// the head now, the file itself as it is sent (WA_FileMore)
	WA_RespondHead( c, 200, "OK", type, "Cache-Control: max-age=3600\r\n", len );
	c->file = f;
	c->fileLeft = len;
}

// animation names in animNumber_t order, to look up the frames in animation.cfg
void WA_ApiAnims( waConnection_t *c ) {
	waBuffer_t	b = { 0 };
	int			i;

	WA_BufAppend( &b, "{\"anims\":[", 10 );
	for ( i = 0; i < MAX_ANIMATIONS; i++ ) {
		if ( i )
			WA_BufAppend( &b, ",", 1 );
		WA_BufJsonString( &b, animTable[i].name ? animTable[i].name : "" );
	}
	WA_BufAppend( &b, "]}", 2 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

/*
==============================================================================

Model list

Not in JKAdmin: every md3 and glm under models/, for the tab that places them as misc_prop.

The file system of the engine lists one folder at a call and goes through all pk3 files for it. The models are
in a few hundred folders, so that is some hundred calls, and they are not made in one frame: WebAdmin_Frame()
takes folders for a few milliseconds a frame until all are done, and the page asks again until the list is
complete. The list stays until the module is loaded anew, which is when the pk3 files may have changed.

==============================================================================
*/

#define WA_MODEL_MSEC		3		// of a server frame for the search
#define WA_MODEL_MAX		8000	// files
#define WA_MODEL_DEPTH		6		// folders below models/

static waBuffer_t	modelQueue;		// the folders that were found, one after the other with a 0 after each
static int			modelQueueNext;	// where the next one to look into begins
static waBuffer_t	modelNames;		// the files so far: JSON strings with commas between them
static int			modelCount;
static int			modelScan;		// 0: not begun, 1: running, 2: complete

// The engine names a file once for every place it finds it in, and a folder with a / at its end from a pk3 and
// without one from the disk. qtrue: name stands in the list before this place already.
static qboolean WA_ModelListedBefore( const char *list, const char *name ) {
	const char *p;

	for ( p = list; p < name; p += strlen( p ) + 1 ) {
		if ( !Q_stricmp( p, name ) )
			return qtrue;
	}
	return qfalse;
}

// qtrue: the folder is in the queue, looked into or not
static qboolean WA_ModelQueued( const char *path ) {
	const char *p, *end = modelQueue.data + modelQueue.len;

	for ( p = modelQueue.data; p && p < end; p += strlen( p ) + 1 ) {
		if ( !Q_stricmp( p, path ) )
			return qtrue;
	}
	return qfalse;
}

static void WA_ModelScanFolder( const char *dir ) {
	static const char *const	extensions[] = { ".md3", ".glm" };
	static char					list[32768];
	char						path[MAX_QPATH];
	const char					*name;
	int							i, n, depth = 0;
	size_t						e;

	for ( name = dir; *name; name++ ) {
		if ( *name == '/' )
			depth++;
	}
	if ( depth < WA_MODEL_DEPTH ) {
		n = trap->FS_GetFileList( dir, "/", list, sizeof( list ) );
		for ( i = 0, name = list; i < n; i++, name += strlen( name ) + 1 ) {
			int len = (int)strlen( name );

			// the engine names a folder with or without a / at its end, and itself and the one above among them
			if ( len && name[len - 1] == '/' )
				len--;
			if ( !len || name[0] == '.' || strlen( dir ) + len + 2 > sizeof( path ) )
				continue;
			Com_sprintf( path, sizeof( path ), "%s/%.*s", dir, len, name );
			if ( !WA_ModelQueued( path ) )
				WA_BufAppend( &modelQueue, path, (int)strlen( path ) + 1 );
		}
	}
	for ( e = 0; e < ARRAY_LEN( extensions ); e++ ) {
		n = trap->FS_GetFileList( dir, extensions[e], list, sizeof( list ) );
		for ( i = 0, name = list; i < n && modelCount < WA_MODEL_MAX; i++, name += strlen( name ) + 1 ) {
			// The engine also names the files of the folders one and two levels further down, with the folder in
			// front of the name. Those are taken when their own folder is looked into, or they would be here thrice.
			if ( !name[0] || strchr( name, '/' ) || strlen( dir ) + strlen( name ) + 2 > sizeof( path )
				|| WA_ModelListedBefore( list, name ) )
				continue;
			Com_sprintf( path, sizeof( path ), "%s/%s", dir, name );
			if ( modelCount )
				WA_BufAppend( &modelNames, ",", 1 );
			WA_BufJsonString( &modelNames, path );
			modelCount++;
		}
	}
}

// once a frame while the search runs
static void WA_ModelScanFrame( void ) {
	const int start = trap->Milliseconds();

	while ( modelScan == 1 ) {
		char dir[MAX_QPATH];

		// the queue is kept to the end, a folder that was looked into must not come in again
		if ( modelQueueNext >= modelQueue.len ) {
			modelScan = 2;
			WA_BufFree( &modelQueue );
			modelQueueNext = 0;
			break;
		}
		// a copy: what is found is put at the end of the queue, which may move it
		Q_strncpyz( dir, modelQueue.data + modelQueueNext, sizeof( dir ) );
		modelQueueNext += (int)strlen( modelQueue.data + modelQueueNext ) + 1;
		WA_ModelScanFolder( dir );
		if ( trap->Milliseconds() - start >= WA_MODEL_MSEC )
			break;
	}
}

extern char NPCParms[];		// NPC_stats.c: the text of every .npc file

/*
=================
WA_ApiList

GET /api/list?kind=: names for the fields of the page to offer, so that a value does not have to be known by
heart. maps, effects and music are the files of the game, npcs the types of the .npc files, bots the bots the
server knows. fxindex is no such list: the effects the map has in use by now, in the order of their numbers.
=================
*/
void WA_ApiList( waConnection_t *c, const char *query ) {
	// strip: the name goes without its ending. prefix: what the key of an entity wants in front of it
	static const struct { const char *kind, *dir, *ext, *prefix; qboolean strip; } folders[] = {
		{ "maps", "maps", ".bsp", "", qtrue },
		{ "effects", "effects", ".efx", "", qtrue },
		{ "music", "music", ".mp3", "music/", qfalse },
	};
	static char	list[65536];
	char		kind[16], name[MAX_QPATH];
	waBuffer_t	b = { 0 };
	qboolean	first = qtrue;
	size_t		f;
	int			i, n;

	WA_FormValue( query, "kind", kind, sizeof( kind ) );
	WA_BufAppend( &b, "{\"names\":[", 10 );

	for ( f = 0; f < ARRAY_LEN( folders ); f++ ) {
		const char *file;

		if ( strcmp( kind, folders[f].kind ) )
			continue;
		// the engine names the files of the folders below too, with the folder in front
		n = trap->FS_GetFileList( folders[f].dir, folders[f].ext, list, sizeof( list ) );
		for ( i = 0, file = list; i < n; i++, file += strlen( file ) + 1 ) {
			if ( !file[0] || WA_ModelListedBefore( list, file ) )
				continue;
			Com_sprintf( name, sizeof( name ), "%s%s", folders[f].prefix, file );
			if ( folders[f].strip )
				COM_StripExtension( name, name, sizeof( name ) );
			if ( !first )
				WA_BufAppend( &b, ",", 1 );
			first = qfalse;
			WA_BufJsonString( &b, name );
		}
	}

	// The files and folders of one folder of the game, for a field that takes a path: dir is the folder, ext
	// the endings that are wanted (".wav,.mp3"). Only below the folders the editor has a use for.
	if ( !strcmp( kind, "dir" ) ) {
		static const char *const roots[] = { "sound", "models", "music", "effects", "maps" };
		char		dir[MAX_QPATH], exts[64], *ext, *next;
		const char	*file;
		qboolean	allowed = qfalse;

		WA_FormValue( query, "dir", dir, sizeof( dir ) );
		WA_FormValue( query, "ext", exts, sizeof( exts ) );
		i = (int)strlen( dir );
		while ( i > 0 && dir[i - 1] == '/' )
			dir[--i] = '\0';
		for ( f = 0; f < ARRAY_LEN( roots ); f++ ) {
			const size_t len = strlen( roots[f] );

			if ( !Q_stricmpn( dir, roots[f], (int)len ) && ( dir[len] == '\0' || dir[len] == '/' ) )
				allowed = qtrue;
		}
		if ( allowed && !strstr( dir, ".." ) && !strchr( dir, '\\' ) && !strchr( dir, ':' ) ) {
			// the folders in it, named with a / at their end
			n = trap->FS_GetFileList( dir, "/", list, sizeof( list ) );
			for ( i = 0, file = list; i < n; i++, file += strlen( file ) + 1 ) {
				int len = (int)strlen( file );

				if ( len && file[len - 1] == '/' )
					len--;
				if ( !len || file[0] == '.' || WA_ModelListedBefore( list, file ) )
					continue;
				Com_sprintf( name, sizeof( name ), "%s/%.*s/", dir, len, file );
				if ( !first )
					WA_BufAppend( &b, ",", 1 );
				first = qfalse;
				WA_BufJsonString( &b, name );
			}
			// and its files: the engine names the ones of the folders below too, those are left out
			for ( ext = exts; ext && ext[0]; ext = next ) {
				next = strchr( ext, ',' );
				if ( next )
					*next++ = '\0';
				n = trap->FS_GetFileList( dir, ext, list, sizeof( list ) );
				for ( i = 0, file = list; i < n; i++, file += strlen( file ) + 1 ) {
					if ( !file[0] || strchr( file, '/' ) || WA_ModelListedBefore( list, file ) )
						continue;
					Com_sprintf( name, sizeof( name ), "%s/%s", dir, file );
					if ( !first )
						WA_BufAppend( &b, ",", 1 );
					first = qfalse;
					WA_BufJsonString( &b, name );
				}
			}
		}
	}

	if ( !strcmp( kind, "npcs" ) ) {
		// a .npc file is a name and its block, one after the other
		const char *p = NPCParms;

		while ( p ) {
			const char *token = COM_ParseExt( &p, qtrue );

			if ( !token[0] )
				break;
			Q_strncpyz( name, token, sizeof( name ) );
			if ( SkipBracedSection( &p, 0 ) && name[0] != '{' ) {
				if ( !first )
					WA_BufAppend( &b, ",", 1 );
				first = qfalse;
				WA_BufJsonString( &b, name );
			}
		}
	} else if ( !strcmp( kind, "bots" ) ) {
		for ( i = 0; i < level.bots.num; i++ ) {
			if ( !first )
				WA_BufAppend( &b, ",", 1 );
			first = qfalse;
			WA_BufJsonString( &b, Info_ValueForKey( G_GetBotInfoByNumber( i ), "name" ) );
		}
	} else if ( !strcmp( kind, "events" ) ) {
		// the names of the events of the game in the order of their numbers (entity_event_t), which is what the
		// state feed names an event by
		for ( i = 0; i < EV_NUM_ENTITY_EVENTS; i++ ) {
			if ( !first )
				WA_BufAppend( &b, ",", 1 );
			first = qfalse;
			WA_BufJsonString( &b, eventnames[i] ? eventnames[i] : "" );
		}
	} else if ( !strcmp( kind, "fxindex" ) ) {
		// The names of CS_EFFECTS, the first of them for the index 1: what the state feed names an effect by.
		// G_EffectIndex gives the numbers out one after the other and none is given back within a map.
		for ( i = 1; i < MAX_FX; i++ ) {
			trap->GetConfigstring( CS_EFFECTS + i, name, sizeof( name ) );
			if ( !name[0] )
				break;
			if ( !first )
				WA_BufAppend( &b, ",", 1 );
			first = qfalse;
			WA_BufJsonString( &b, name );
		}
	}

	WA_BufAppend( &b, "]}", 2 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

// GET /api/shaders: the names of the shader scripts of the game, in the order the engine reads them in. The page
// reads the files themselves through /api/file.
void WA_ApiShaders( waConnection_t *c ) {
	static char	list[32768];
	waBuffer_t	b = { 0 };
	const char	*name;
	qboolean	first = qtrue;
	int			i, n;

	n = trap->FS_GetFileList( "shaders", ".shader", list, sizeof( list ) );
	WA_BufAppend( &b, "{\"files\":[", 10 );
	for ( i = 0, name = list; i < n; i++, name += strlen( name ) + 1 ) {
		// the engine reads the ones of this folder only, and each name once
		if ( !name[0] || strchr( name, '/' ) || WA_ModelListedBefore( list, name ) )
			continue;
		if ( !first )
			WA_BufAppend( &b, ",", 1 );
		first = qfalse;
		WA_BufJsonString( &b, name );
	}
	WA_BufAppend( &b, "]}", 2 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

// GET /api/models: the list as far as it is, done says whether that is all
void WA_ApiModels( waConnection_t *c ) {
	waBuffer_t b = { 0 };

	if ( !modelScan ) {
		modelScan = 1;
		WA_BufAppend( &modelQueue, "models", 7 );
	}
	WA_BufPrintf( &b, "{\"done\":%s,\"count\":%d,\"models\":[", WA_BOOL( modelScan == 2 ), modelCount );
	if ( modelNames.len )
		WA_BufAppend( &b, modelNames.data, modelNames.len );
	WA_BufAppend( &b, "]}", 2 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

// every frame: the list of the models is looked for a little at a time while a page waits for it
void WA_AssetsFrame( void ) {
	if ( modelScan == 1 )
		WA_ModelScanFrame();
}

// the end of the map: what was found out about it and the list of the models
void WA_AssetsShutdown( void ) {
	mapModels = -1;
	mapModelsName[0] = '\0';
	WA_BufFree( &modelQueue );
	WA_BufFree( &modelNames );
	modelQueueNext = modelCount = modelScan = 0;
}

