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

// cl_models.cpp -- the player models of the model store of the site (jka.vision/models/, issue #6). Not from
// OpenJK, see docs/japlus/models.md.
//
// Anyone can upload a player model there (deploy/models/modelstore.py of the website). When a player on the
// server uses a model this game does not have and the store has it, it is fetched in the background
// (v_modelStore 1), into base/modelstore/<model>.pk3 of the home folder, and shown at once: the pak goes to the
// head of the path (FS_AddModelPak), the renderer looks again for what it did not find under the model
// (re->ext.ForgetMissing), and the cgame loads the players with that model again (v_modelArrived). A pak is
// taken only with the size and the SHA-256 of the list of the store (index.txt), and only with files of its own
// model (shared/web/web_models.h), the file system looks at every one again when it loads it. On a pure server
// such a pak is not used, so nothing is fetched there.
//
// The window of the store in the game (cgame/modelstore/cg_modelstore.c, v_modelbrowser) reads the list this file
// keeps in base/modelstore/index.txt and the icons in base/modelstore/icons/, which this file fetches when the
// window asks (v_models icons). v_modelStoreChanged counts up whenever one of them, or a model, came;
// v_modelStoreFetching names the model that is fetched now. What the player asks for himself (the window, v_models
// get) is fetched also with v_modelStore 0, which only stops fetching the models of others by itself.
//
// One question to a web server runs at a time in the client (sys/sys_http.h): this file, the updater
// (cl_update.cpp) and the paks of a server (cl_download.cpp) wait for each other. The fetch runs in a thread of its
// own, the game looks at it once a frame and never waits for it.

#include "client.h"
#include "cl_cgameapi.h"
#include "web/web_http.h"
#include "web/web_models.h"

#define MDL_SCAN_MSEC		1000				// the players are looked at once a second
#define MDL_SEEN			320					// models that were looked at this run (and emojis of the store, EMOJI_MOST of
												//	modelstore.py of the site is 300)

typedef struct {
	char	name[MDL_SEEN][WEB_MODEL_NAME];
	int		num;
} mdlNames_t;

static cvar_t		*v_modelStore;
static cvar_t		*v_modelStoreChanged;
static int			mdlScanTime;
static mdlNames_t	mdlSeen;				// fetched, failed, not in the store or here already

static qboolean MDL_Has( const mdlNames_t *list, const char *model ) {
	int i;

	for ( i = 0; i < list->num; i++ ) {
		if ( !strcmp( list->name[i], model ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

static void MDL_Add( mdlNames_t *list, const char *model ) {
	if ( !MDL_Has( list, model ) && list->num < MDL_SEEN ) {
		Q_strncpyz( list->name[list->num++], model, sizeof( list->name[0] ) );
	}
}

static void MDL_See( const char *model ) {
	MDL_Add( &mdlSeen, model );
}

// the window of the cgame and the menus look at the list, the icons and the models again
static void MDL_Changed( void ) {
	Cvar_SetValue( "v_modelStoreChanged", v_modelStoreChanged->value + 1.0f );
}

// The next model a player here uses that this game does not have. qfalse: none.
static qboolean MDL_NextOfPlayers( char *out, size_t outSize ) {
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const char *cs = cl.gameState.stringData + cl.gameState.stringOffsets[CS_PLAYERS + i];
		char		model[WEB_MODEL_NAME];

		if ( !cs[0] || !WEB_ModelFromInfo( Info_ValueForKey( cs, "model" ), model, sizeof( model ) ) || MDL_Has( &mdlSeen, model ) ) {
			continue;
		}
		// (a model of the game, of a pak of the server, or one the player put in himself)
		if ( FS_ReadFile( va( "models/players/%s/model.glm", model ), NULL ) > 0 ) {
			Com_DPrintf( "model store: the game has %s\n", model );
			MDL_See( model );
			continue;
		}
		Q_strncpyz( out, model, outSize );
		return qtrue;
	}
	return qfalse;
}

/*
=================
MDL_Use

The pak of model is in base/modelstore of the home folder: it goes to the head of the path, the renderer looks
again for what it did not find under the model, and the cgame loads the players with it again. qfalse: the file
system does not take it (no pak, or files of something else).
=================
*/
static qboolean MDL_Use( const char *model ) {
	char why[128];

	if ( !FS_AddModelPak( model, why, sizeof( why ) ) ) {
		Com_Printf( S_COLOR_YELLOW "The model %s of the model store is not used: %s\n", model, why );
		return qfalse;
	}
	Com_Printf( "The model %s came from the model store\n", model );
	if ( re && re->ext.ForgetMissing ) {
		re->ext.ForgetMissing( va( "models/players/%s/", model ) );
	}
	MDL_Changed();
	if ( cls.cgameStarted ) {
		Cmd_TokenizeString( va( "v_modelArrived %s", model ) );
		CGVM_ConsoleCommand();
	}
	return qtrue;
}

#if !defined( __EMSCRIPTEN__ )

#include <SDL.h>
#include "sys/sys_http.h"

#define MDL_URL_DEFAULT		"https://jka.vision/models/"
#define MDL_URL				256
#define MDL_INDEX_MOST		( 256 * 1024 )		// bytes of index.txt at most
#define MDL_INDEX_MSEC		( 10 * 60 * 1000 )	// index.txt is fetched again after this long, when a model is wanted
#define MDL_ICON_MOST		( 256 * 1024 )		// bytes of an icon at most
#define MDL_EMOJI_MOST		( 64 * 1024 )		// bytes of a chat emoji at most (MAX_EMOJI of modelstore.py)
#define MDL_EMOTE_SOUND_MOST	( 96 * 1024 )	// bytes of an own sound of an interactive emoji (2 s at 22050 Hz, 16 bit)
#define MDL_EMOTE_LIST		"emotes/index.txt"	// in base of the home folder: the interactive emojis for the cgame
#define MDL_DRAWING_BYTES	( 4 + 256 * 128 )	// a picture of a drawing board: "VJD1" and BOARD_W x BOARD_H (bg_toys.h)
#define MDL_PICTURE_MOST	( 1500 * 1500 )		// numbers of pictures of a drawing board or a spray (TOY_BOARD_SPLIT squared)
#define MDL_PICTURE_ASKS	8					// pictures of a kind the cgame asked for that wait

enum {
	MDLJOB_FREE,
	MDLJOB_RUNNING,
	MDLJOB_DONE,			// the thread is through and waits for the game
	MDLJOB_LET_GO			// the game wants it no more: the thread frees the slot when it is through
};

typedef struct {
	SDL_atomic_t		state;
	SDL_Thread			*thread;
	char				url[MDL_URL + 64];
	volatile int		cancel;
	long long			most;					// bytes it may have
	// what the thread writes, the game reads it once the state is MDLJOB_DONE
	char				osTemp[MAX_OSPATH];		// a model: the file it goes to; the list: none, it goes to index
	FILE				*f;
	webSha256_t			sha;
	char				*index;
	volatile long long	bytes;
	qboolean			ok, disk;
	int					status;
	char				error[256];
	// the game's
	char				model[WEB_MODEL_NAME];	// empty: the job fetches index.txt
	char				icon[4];				// "jpg", "png": the job fetches the icon of model
	int					named;					// a file of a named store (mdlNamedStores) + 1: emojis, sounds
	char				emoji[WEB_MODEL_NAME];	//	this one of it
	qboolean			emojiIndex;				//	or the list of them
	qboolean			emoteList;				// the interactive emojis for the cgame (MDL_EMOTE_LIST)
	int					picture;				// a picture of a drawing board or a spray that is fetched
	int					pictureKind;			//	of mdlPictureKinds, also of an upload
	const byte			*body;					// what is sent with a POST (the own picture), NULL: a GET
	int					bodyLen;
} mdlJob_t;

static cvar_t	*v_modelStoreURL;

static mdlJob_t	mdlJob;
static char		mdlIndex[MDL_INDEX_MOST + 1];
static int		mdlIndexTime = -1;			// cls.realtime of the list, -1: none yet
static mdlNames_t	mdlIconsTried;			// icons that came or failed this run
static char		mdlWanted[WEB_MODEL_NAME];	// the model that is fetched next (v_models get, or a player)
static qboolean	mdlIconsWanted;				// the window is open: its icons are fetched
static cvar_t	*v_emojiStore;
static byte		mdlPictureBody[SPRAY_UPLOAD_MOST];
static char		mdlAnswer[512];				// what the store answered to an upload

// the home path of the pak of a model, or of the part of it that comes in
static const char *MDL_Local( const char *model, qboolean temp ) {
	return va( "%s/%s/%s.%s", BASEGAME, FS_MODELSTORE, model, temp ? "tmp" : "pk3" );
}

// the home path of the icon of a model (the cgame reads it as modelstore/icons/<model>), or of the part that comes in
static const char *MDL_IconLocal( const char *model, const char *icon, qboolean temp ) {
	return va( "%s/%s/icons/%s.%s", BASEGAME, FS_MODELSTORE, model, temp ? "tmp" : icon );
}

static const char *MDL_OSPath( const char *local ) {
	char *path = FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), local, "" );

	path[strlen( path ) - 1] = '\0';
	return path;
}

static qboolean MDL_Sink( void *ctx, const void *data, size_t len ) {
	mdlJob_t *job = (mdlJob_t *)ctx;

	if ( job->bytes + (long long)len > job->most ) {
		Q_strncpyz( job->error, "it is larger than the store says", sizeof( job->error ) );
		return qfalse;
	}
	if ( job->f ) {
		if ( fwrite( data, 1, len, job->f ) != len ) {
			job->disk = qtrue;
			return qfalse;
		}
		Web_SHA256Update( &job->sha, data, len );
	} else {
		memcpy( job->index + job->bytes, data, len );
	}
	job->bytes += (long long)len;
	return qtrue;
}

// the thread of a fetch: nothing of the engine but Sys_HttpGet
static int MDL_Thread( void *data ) {
	mdlJob_t *job = (mdlJob_t *)data;

	if ( job->osTemp[0] ) {
		job->f = fopen( job->osTemp, "wb" );
		if ( !job->f ) {
			job->disk = qtrue;
		}
	}
	if ( !job->disk ) {
		job->ok = job->body ? Sys_HttpPost( job->url, job->body, job->bodyLen, MDL_Sink, job, &job->cancel, job->error, sizeof( job->error ), &job->status )
			: Sys_HttpGet( job->url, MDL_Sink, job, &job->cancel, job->error, sizeof( job->error ), &job->status );
	}
	if ( job->f ) {
		if ( fclose( job->f ) != 0 ) {
			job->disk = qtrue;
		}
		job->f = NULL;
	}
	// (let go of: nobody but this thread knows the slot any more)
	if ( !SDL_AtomicCAS( &job->state, MDLJOB_RUNNING, MDLJOB_DONE ) ) {
		SDL_AtomicSet( &job->state, MDLJOB_FREE );
	}
	return 0;
}

// a thread of this file asks a web server now, also one that was let go (the updater and the paks of a server wait)
qboolean CL_ModelsAsking( void ) {
	const int state = SDL_AtomicGet( &mdlJob.state );

	return (qboolean)( state == MDLJOB_RUNNING || state == MDLJOB_LET_GO || state == MDLJOB_DONE );
}

// The address of the store with what is asked of it. qfalse: no https address (v_modelStoreURL is set on the
// command line only, a plain http one is not taken).
static qboolean MDL_Url( const char *path, char *out, int outSize ) {
	const char *base = v_modelStoreURL->string;

	if ( Q_stricmpn( base, "https://", 8 ) || strlen( base ) >= MDL_URL || strchr( base, ' ' ) ) {
		return qfalse;
	}
	Com_sprintf( out, outSize, "%s%s%s", base, base[strlen( base ) - 1] == '/' ? "" : "/", path );
	return qtrue;
}

// Starts the fetch of the pak of model, of its icon (icon "jpg" or "png"), or with model NULL of the list.
// The thread of a job that was set up starts. qfalse: it did not, the slot is free again.
static qboolean MDL_Launch( mdlJob_t *job ) {
	SDL_AtomicSet( &job->state, MDLJOB_RUNNING );
	job->thread = SDL_CreateThread( MDL_Thread, "models", job );
	if ( !job->thread ) {
		SDL_AtomicSet( &job->state, MDLJOB_FREE );
		return qfalse;
	}
	return qtrue;
}

static void MDL_Start( const char *model, const char *icon ) {
	mdlJob_t *job = &mdlJob;

	memset( job, 0, sizeof( *job ) );
	if ( !MDL_Url( !model ? "index.txt" : icon ? va( "files/%s.%s", model, icon ) : va( "files/%s.pk3", model ), job->url, sizeof( job->url ) ) ) {
		Com_Printf( S_COLOR_YELLOW "v_modelStoreURL is no https address, the model store is not asked\n" );
		Cvar_Set( "v_modelStore", "0" );
		return;
	}
	if ( model && icon ) {
		fileHandle_t f = FS_SV_FOpenFileWrite( MDL_IconLocal( model, icon, qtrue ) );

		MDL_Add( &mdlIconsTried, model );
		if ( !f ) {
			return;
		}
		FS_FCloseFile( f );
		Q_strncpyz( job->osTemp, MDL_OSPath( MDL_IconLocal( model, icon, qtrue ) ), sizeof( job->osTemp ) );
		Q_strncpyz( job->model, model, sizeof( job->model ) );
		Q_strncpyz( job->icon, icon, sizeof( job->icon ) );
		job->most = MDL_ICON_MOST;
		Web_SHA256Init( &job->sha );
	} else if ( model ) {
		unsigned char	sha[WEB_SHA256_SIZE];
		fileHandle_t	f;

		if ( !WEB_ModelIndexFind( mdlIndex, model, &job->most, sha ) ) {
			return;
		}
		// the folders and the file are made here, the thread calls nothing of the engine
		f = FS_SV_FOpenFileWrite( MDL_Local( model, qtrue ) );
		if ( !f ) {
			MDL_See( model );
			return;
		}
		FS_FCloseFile( f );
		Q_strncpyz( job->osTemp, MDL_OSPath( MDL_Local( model, qtrue ) ), sizeof( job->osTemp ) );
		Q_strncpyz( job->model, model, sizeof( job->model ) );
		Web_SHA256Init( &job->sha );
	} else {
		job->most = MDL_INDEX_MOST;
		job->index = mdlIndex;
		mdlIndex[0] = '\0';
	}
	if ( model && !icon ) {
		Cvar_Set( "v_modelStoreFetching", model );
	}
	if ( !MDL_Launch( job ) ) {
		Cvar_Set( "v_modelStoreFetching", "" );
		if ( model && !icon ) {
			MDL_See( model );
		}
	}
}

// The model that came is shown (MDL_Use), from the part that came in.
static void MDL_Arrived( const char *model ) {
	FS_SV_Rename( MDL_Local( model, qtrue ), MDL_Local( model, qfalse ), qfalse );
	if ( !MDL_Use( model ) ) {
		remove( MDL_OSPath( MDL_Local( model, qfalse ) ) );
	}
}

// An icon that came is kept when it is a picture of the type the list says, and the renderer looks for it again.
static void MDL_IconDone( const mdlJob_t *job ) {
	static const byte	jpg[] = { 0xff, 0xd8, 0xff }, png[] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
	byte				head[8] = { 0 };
	FILE				*f;
	qboolean			good;

	if ( !job->ok || job->disk || !( f = fopen( job->osTemp, "rb" ) ) ) {
		remove( job->osTemp );
		return;
	}
	good = (qboolean)( fread( head, 1, sizeof( head ), f ) == sizeof( head ) );
	fclose( f );
	good = (qboolean)( good && ( !strcmp( job->icon, "jpg" ) ? !memcmp( head, jpg, sizeof( jpg ) ) : !memcmp( head, png, sizeof( png ) ) ) );
	if ( !good ) {
		remove( job->osTemp );
		return;
	}
	FS_SV_Rename( MDL_IconLocal( job->model, job->icon, qtrue ), MDL_IconLocal( job->model, job->icon, qfalse ), qfalse );
	if ( re && re->ext.ForgetMissing ) {
		re->ext.ForgetMissing( va( "%s/icons/", FS_MODELSTORE ) );
	}
	MDL_Changed();
}

/*
===============================================================================

Named stores of the site (issue #9 of the game) whose files are all fetched: the chat emojis, into base/gfx/emoji of
the home folder, where the chat finds them as any emoji (cgame/chat/cg_chat.c lists them again when
v_emojiStoreChanged counts up), and the own sounds of the interactive emojis, into base/sound/emotes. Their lists
(index.txt, sounds.txt) have the lines of the list of the models: <name> <bytes> <sha256> <type>. A file the store no
longer has (its owner took it down) is deleted again: the record of the store says which came from it. Before the
sounds the list of the interactive emojis comes, the definitions the cgame reads (MDL_EMOTE_LIST, cgame/toys/
cg_emote.c, which is told by v_emoteStoreChanged).

===============================================================================
*/

typedef struct {
	const char	*list;			// on the site
	const char	*files;			//	and a file of it, %s the name
	const char	*local;			// in base of the home folder, %s the name and %s png/wav or tmp
	const char	*extension;
	const char	*record;		// in base of the home folder: the names that came, a line each
	int			most;			// bytes of a file at most
	const char	*changed;		// the cvar that counts up when one came or went
} mdlNamedStore_t;

enum { MDL_NAMED_EMOJI, MDL_NAMED_SOUNDS, MDL_NAMED_STORES };

static const mdlNamedStore_t mdlNamedStores[MDL_NAMED_STORES] = {
	{ "emoji/index.txt", "emoji/files/%s.png", "%s/gfx/emoji/%s.%s", "png", "emojistore.txt", MDL_EMOJI_MOST, "v_emojiStoreChanged" },
	{ "emotes/sounds.txt", "emotes/files/%s.wav", "%s/sound/emotes/%s.%s", "wav", "emotesounds.txt", MDL_EMOTE_SOUND_MOST, "v_emoteStoreChanged" },
};

static struct {
	char		index[MDL_INDEX_MOST + 1];
	int			indexTime;			// cls.realtime of the list, -1: none yet
	mdlNames_t	tried;				// files that came or failed this run
	mdlNames_t	here;				// the files of the store in the home folder (record)
	qboolean	read;
	qboolean	complete;			// every file of the list is here: nothing is looked for until the list or a file changes
} mdlNamed[MDL_NAMED_STORES] = { { "", -1 }, { "", -1 } };
static int		mdlEmoteListTime = -1;

static const char *MDL_NamedLocal( int store, const char *name, qboolean temp ) {
	return va( mdlNamedStores[store].local, BASEGAME, name, temp ? "tmp" : mdlNamedStores[store].extension );
}

// the size of a file of the home folder, -1 when there is none. (Not FS_SV_FOpenFileRead: that clears the buffer of
// the sound each time, and this is asked in the game.)
static long long MDL_LocalSize( const char *local ) {
	FILE		*f = fopen( MDL_OSPath( local ), "rb" );
	long long	size;

	if ( !f ) {
		return -1;
	}
	fseek( f, 0, SEEK_END );
	size = ftell( f );
	fclose( f );
	return size;
}

static void MDL_NamedRecordRead( int store ) {
	char			text[MDL_SEEN * WEB_MODEL_NAME + 1], *line, *next;
	fileHandle_t	f;
	int				len;

	if ( mdlNamed[store].read ) {
		return;
	}
	mdlNamed[store].read = qtrue;
	len = FS_SV_FOpenFileRead( va( "%s/%s", BASEGAME, mdlNamedStores[store].record ), &f );
	if ( !f ) {
		return;
	}
	len = FS_Read( text, Q_min( len, (int)sizeof( text ) - 1 ), f );
	FS_FCloseFile( f );
	text[Q_max( len, 0 )] = '\0';
	for ( line = text; line && *line; line = next ) {
		next = strchr( line, '\n' );
		if ( next ) {
			*next++ = '\0';
		}
		if ( WEB_ModelName( line ) ) {
			MDL_Add( &mdlNamed[store].here, line );
		}
	}
}

static void MDL_NamedRecordWrite( int store ) {
	const mdlNames_t	*here = &mdlNamed[store].here;
	fileHandle_t		f = FS_SV_FOpenFileWrite( va( "%s/%s", BASEGAME, mdlNamedStores[store].record ) );
	int					i;

	if ( !f ) {
		return;
	}
	for ( i = 0; i < here->num; i++ ) {
		FS_Write( va( "%s\n", here->name[i] ), (int)strlen( here->name[i] ) + 1, f );
	}
	FS_FCloseFile( f );
}

// Starts the fetch of a file of a named store, or with name NULL of its list.
static void MDL_StartNamed( int store, const char *name ) {
	const mdlNamedStore_t	*s = &mdlNamedStores[store];
	mdlJob_t				*job = &mdlJob;

	memset( job, 0, sizeof( *job ) );
	if ( !MDL_Url( name ? va( s->files, name ) : s->list, job->url, sizeof( job->url ) ) ) {
		return;
	}
	job->named = store + 1;
	if ( name ) {
		unsigned char	sha[WEB_SHA256_SIZE];
		fileHandle_t	f;

		MDL_Add( &mdlNamed[store].tried, name );
		if ( !WEB_ModelIndexFind( mdlNamed[store].index, name, &job->most, sha ) || job->most > s->most ) {
			return;
		}
		f = FS_SV_FOpenFileWrite( MDL_NamedLocal( store, name, qtrue ) );
		if ( !f ) {
			return;
		}
		FS_FCloseFile( f );
		Q_strncpyz( job->osTemp, MDL_OSPath( MDL_NamedLocal( store, name, qtrue ) ), sizeof( job->osTemp ) );
		Q_strncpyz( job->emoji, name, sizeof( job->emoji ) );
		Web_SHA256Init( &job->sha );
	} else {
		job->emojiIndex = qtrue;
		job->most = MDL_INDEX_MOST;
		job->index = mdlNamed[store].index;
		mdlNamed[store].index[0] = '\0';
	}
	MDL_Launch( job );
}

// Starts the fetch of the list of the interactive emojis for the cgame.
static void MDL_StartEmoteList( void ) {
	mdlJob_t *job = &mdlJob;

	memset( job, 0, sizeof( *job ) );
	mdlEmoteListTime = cls.realtime;
	if ( !MDL_Url( "emotes/index.txt", job->url, sizeof( job->url ) ) ) {
		return;
	}
	job->emoteList = qtrue;
	job->most = MDL_INDEX_MOST;
	job->index = mdlNamed[MDL_NAMED_SOUNDS].index;		// (free until the list of the sounds is fetched after it)
	MDL_Launch( job );
}

// The list of the interactive emojis came: it goes where the cgame reads it.
static void MDL_EmoteListDone( mdlJob_t *job ) {
	fileHandle_t f;

	if ( !job->ok ) {
		Com_DPrintf( "The list of the interactive emojis of the store did not come: %s\n", job->error );
		return;
	}
	f = FS_SV_FOpenFileWrite( va( "%s/%s", BASEGAME, MDL_EMOTE_LIST ) );
	if ( f ) {
		FS_Write( job->index, (int)job->bytes, f );
		FS_FCloseFile( f );
	}
	job->index[0] = '\0';
	mdlNamed[MDL_NAMED_SOUNDS].indexTime = -1;
	Cvar_SetValue( "v_emoteStoreChanged", Cvar_VariableValue( "v_emoteStoreChanged" ) + 1.0f );
}

// The list came: a file that came from the store and is no longer in it goes. Or a file came: it is kept when it
// is the file of the list.
static void MDL_NamedDone( mdlJob_t *job ) {
	const int		store = job->named - 1;
	mdlNames_t		*here = &mdlNamed[store].here;
	unsigned char	sha[WEB_SHA256_SIZE], want[WEB_SHA256_SIZE];
	long long		size = 0;
	qboolean		changed = qfalse;
	int				i;

	if ( job->emojiIndex ) {
		mdlNamed[store].index[job->ok ? job->bytes : 0] = '\0';
		mdlNamed[store].indexTime = cls.realtime;
		mdlNamed[store].complete = qfalse;
		if ( !job->ok ) {
			Com_DPrintf( "The list %s of the store did not come: %s\n", mdlNamedStores[store].list, job->error );
			return;
		}
		MDL_NamedRecordRead( store );
		for ( i = here->num - 1; i >= 0; i-- ) {
			if ( !WEB_ModelIndexFind( mdlNamed[store].index, here->name[i], &size, want ) ) {
				remove( MDL_OSPath( MDL_NamedLocal( store, here->name[i], qfalse ) ) );
				memmove( &here->name[i], &here->name[i + 1], ( here->num - i - 1 ) * sizeof( here->name[0] ) );
				here->num--;
				changed = qtrue;
			}
		}
		if ( changed ) {
			MDL_NamedRecordWrite( store );
			Cvar_SetValue( mdlNamedStores[store].changed, Cvar_VariableValue( mdlNamedStores[store].changed ) + 1.0f );
		}
		return;
	}
	Web_SHA256Final( &job->sha, sha );
	if ( !job->ok || job->disk || !WEB_ModelIndexFind( mdlNamed[store].index, job->emoji, &size, want ) || job->bytes != size
		|| memcmp( sha, want, sizeof( sha ) ) ) {
		remove( job->osTemp );
		return;
	}
	FS_SV_Rename( MDL_NamedLocal( store, job->emoji, qtrue ), MDL_NamedLocal( store, job->emoji, qfalse ), qfalse );
	mdlNamed[store].complete = qfalse;
	MDL_NamedRecordRead( store );
	MDL_Add( here, job->emoji );
	MDL_NamedRecordWrite( store );
	Cvar_SetValue( mdlNamedStores[store].changed, Cvar_VariableValue( mdlNamedStores[store].changed ) + 1.0f );
}

// The next file of a named store: its list when it is old, else the next one not here yet (a sound whose size is
// not the one of the list counts as not here: an interactive emoji of that name was made again). Once all are here
// the files are not looked at again until the list or a file changes. qfalse: nothing.
static qboolean MDL_NextNamed( int store ) {
	char		name[WEB_MODEL_NAME], icon[4];
	const char	*p;
	long long	size;

	if ( mdlNamed[store].indexTime < 0 || cls.realtime - mdlNamed[store].indexTime > MDL_INDEX_MSEC ) {
		MDL_StartNamed( store, NULL );
		return qtrue;
	}
	if ( mdlNamed[store].complete ) {
		return qfalse;
	}
	MDL_NamedRecordRead( store );
	for ( p = WEB_ModelIndexNext( mdlNamed[store].index, name, icon, &size ); p; p = WEB_ModelIndexNext( p, name, icon, &size ) ) {
		const long long here = MDL_Has( &mdlNamed[store].here, name ) ? MDL_LocalSize( MDL_NamedLocal( store, name, qfalse ) ) : -1;

		if ( !MDL_Has( &mdlNamed[store].tried, name ) && ( here < 0 || ( store == MDL_NAMED_SOUNDS && here != size ) ) ) {
			MDL_StartNamed( store, name );
			return qtrue;
		}
	}
	mdlNamed[store].complete = qtrue;
	return qfalse;
}

// The chat emojis first, then the list of the interactive emojis when it is old, then their sounds.
static void MDL_NextEmoji( void ) {
	if ( MDL_NextNamed( MDL_NAMED_EMOJI ) ) {
		return;
	}
	if ( mdlEmoteListTime < 0 || cls.realtime - mdlEmoteListTime > MDL_INDEX_MSEC ) {
		MDL_StartEmoteList();
		return;
	}
	MDL_NextNamed( MDL_NAMED_SOUNDS );
}

// v_emotes refresh: the lists of the stores again now (the cgame heard of an interactive emoji it does not know)
static void CL_Emotes_f( void ) {
	if ( Q_stricmp( Cmd_Argv( 1 ), "refresh" ) ) {
		Com_Printf( "usage: v_emotes refresh\n" );
		return;
	}
	// (not more than once a minute)
	if ( mdlEmoteListTime >= 0 && cls.realtime - mdlEmoteListTime < 60000 ) {
		return;
	}
	mdlEmoteListTime = -1;
	mdlNamed[MDL_NAMED_EMOJI].indexTime = -1;
}

/*
===============================================================================

The pictures players make for the others, in stores of the site by number (deploy/models/modelstore.py of the
website), issue #9 of the game. The pictures of the drawing boards (cgame/toys/cg_board.c): the own one goes to the
store when it is held up (v_drawings upload, from drawings/upload.vjd, which the cgame wrote). The pictures of the
sprays (cgame/toys/cg_spray.c): a picture of the player's own goes there when he sprays it (v_sprays upload <file>,
the sheet cl_sprays.cpp makes of it). The number of the own picture goes back to the cgame (v_boardPicture,
v_sprayPicture), which says it to the others; the picture of a number another player shows is fetched into the home
folder (v_drawings get, v_sprays get), base/drawings/<number>.vjd and base/sprays/store/<number>.png, where the cgame
reads it. A spray that came is said to the cgame (v_sprayArrived), a board looks for its file itself.

===============================================================================
*/

typedef struct {
	const char	*store;			// its part of the site, and the command of the console
	const char	*folder;		// in base of the home folder
	const char	*extension;
	int			most;			// bytes of a picture at most
	const char	*answer;		// the cgame command that gets the number of the own picture
	const char	*arrived;		// the cgame command that is told of a picture that came, NULL: none
} mdlPictureKind_t;

enum { MDL_DRAWINGS, MDL_SPRAYS, MDL_PICTURE_KINDS };

static const mdlPictureKind_t mdlPictureKinds[MDL_PICTURE_KINDS] = {
	{ "drawings", "drawings", "vjd", MDL_DRAWING_BYTES, "v_boardPicture", NULL },
	{ "sprays", "sprays/store", "png", SPRAY_UPLOAD_MOST, "v_sprayPicture", "v_sprayArrived" },
};

static qboolean	mdlPictureUpload[MDL_PICTURE_KINDS];		// the own picture of the kind waits to go to the store
static int		mdlPictureGets[MDL_PICTURE_KINDS][MDL_PICTURE_ASKS], mdlNumPictureGets[MDL_PICTURE_KINDS];

static const char *MDL_PictureLocal( int kind, int number, qboolean temp ) {
	return va( "%s/%s/%i.%s", BASEGAME, mdlPictureKinds[kind].folder, number, temp ? "tmp" : mdlPictureKinds[kind].extension );
}

// The own picture of a kind for the store, in mdlPictureBody: the drawing the cgame wrote, the sheet of the spray.
// 0: there is none.
static int MDL_PictureBody( int kind ) {
	if ( kind == MDL_SPRAYS ) {
		return CL_SprayUploadBody( mdlPictureBody, sizeof( mdlPictureBody ) );
	}
	void	*data;
	int		len = FS_ReadFile( "drawings/upload.vjd", &data );

	if ( len != MDL_DRAWING_BYTES || memcmp( data, "VJD1", 4 ) ) {
		if ( len > 0 ) {
			FS_FreeFile( data );
		}
		return 0;
	}
	Com_Memcpy( mdlPictureBody, data, MDL_DRAWING_BYTES );
	FS_FreeFile( data );
	return MDL_DRAWING_BYTES;
}

// Starts the fetch of picture get of a kind, or with get 0 the upload of the own one.
static void MDL_StartPicture( int kind, int get ) {
	const mdlPictureKind_t	*k = &mdlPictureKinds[kind];
	mdlJob_t				*job = &mdlJob;

	memset( job, 0, sizeof( *job ) );
	job->pictureKind = kind;
	if ( get ) {
		fileHandle_t f;

		if ( !MDL_Url( va( "%s/files/%i.%s", k->store, get, k->extension ), job->url, sizeof( job->url ) ) ) {
			return;
		}
		f = FS_SV_FOpenFileWrite( MDL_PictureLocal( kind, get, qtrue ) );
		if ( !f ) {
			return;
		}
		FS_FCloseFile( f );
		Q_strncpyz( job->osTemp, MDL_OSPath( MDL_PictureLocal( kind, get, qtrue ) ), sizeof( job->osTemp ) );
		job->picture = get;
		job->most = k->most;
		Web_SHA256Init( &job->sha );
	} else {
		if ( !MDL_Url( va( "%s/api/upload", k->store ), job->url, sizeof( job->url ) ) || !( job->bodyLen = MDL_PictureBody( kind ) ) ) {
			return;
		}
		job->body = mdlPictureBody;
		job->index = mdlAnswer;
		job->most = sizeof( mdlAnswer ) - 1;
	}
	MDL_Launch( job );
}

// whether the file that came is a picture of its kind: a drawing has its size and begins with VJD1, a spray is a
// PNG with the frames of cl_sprays.cpp
static qboolean MDL_PictureGood( const mdlJob_t *job ) {
	static byte	data[SPRAY_UPLOAD_MOST];
	FILE		*f = fopen( job->osTemp, "rb" );
	size_t		len = 0;

	if ( f ) {
		len = fread( data, 1, sizeof( data ), f );
		fclose( f );
	}
	if ( job->pictureKind == MDL_SPRAYS ) {
		return (qboolean)( len == (size_t)job->bytes && CL_SprayFileGood( data, (int)len ) );
	}
	return (qboolean)( job->bytes == MDL_DRAWING_BYTES && len == MDL_DRAWING_BYTES && !memcmp( data, "VJD1", 4 ) );
}

// The store answered the upload ({"id": <number>, "key": ..}), or a picture came.
static void MDL_PictureDone( mdlJob_t *job ) {
	const mdlPictureKind_t *k = &mdlPictureKinds[job->pictureKind];

	if ( job->body ) {
		const char	*id;
		int			number;

		mdlAnswer[job->bytes] = '\0';
		id = strstr( mdlAnswer, "\"id\":" );
		number = id ? atoi( id + 5 ) : 0;
		if ( !job->ok || number <= 0 || number >= MDL_PICTURE_MOST ) {
			Com_Printf( S_COLOR_YELLOW "The picture did not go to the store of the site: %s\n", job->status == 429 ? "too many at once, it goes again later" : job->error );
			return;
		}
		if ( cls.cgameStarted ) {
			Cmd_TokenizeString( va( "%s %i", k->answer, number ) );
			CGVM_ConsoleCommand();
		}
		return;
	}
	if ( !job->ok || job->disk || !MDL_PictureGood( job ) ) {
		remove( job->osTemp );
		return;
	}
	FS_SV_Rename( MDL_PictureLocal( job->pictureKind, job->picture, qtrue ), MDL_PictureLocal( job->pictureKind, job->picture, qfalse ), qfalse );
	if ( k->arrived && cls.cgameStarted ) {
		Cmd_TokenizeString( va( "%s %i", k->arrived, job->picture ) );
		CGVM_ConsoleCommand();
	}
}

// v_drawings upload: the own picture to the store; v_drawings get <number>: a picture of another board. The same
// for v_sprays, whose upload names the file of the picture (cl_sprays.cpp).
static void MDL_Pictures( int kind ) {
	const char	*what = Cmd_Argv( 1 );
	int			number, i;

	if ( !Q_stricmp( what, "upload" ) && ( kind != MDL_SPRAYS || CL_SprayUploadMake( Cmd_Argv( 2 ) ) ) ) {
		mdlPictureUpload[kind] = qtrue;
		return;
	}
	number = atoi( Cmd_Argv( 2 ) );
	if ( Q_stricmp( what, "get" ) || number <= 0 || number >= MDL_PICTURE_MOST ) {
		Com_Printf( "usage: %s upload%s | get <number>\n", Cmd_Argv( 0 ), kind == MDL_SPRAYS ? " <file>" : "" );
		return;
	}
	for ( i = 0; i < mdlNumPictureGets[kind]; i++ ) {
		if ( mdlPictureGets[kind][i] == number ) {
			return;
		}
	}
	if ( mdlNumPictureGets[kind] < MDL_PICTURE_ASKS ) {
		mdlPictureGets[kind][mdlNumPictureGets[kind]++] = number;
	}
}

static void CL_Drawings_f( void ) {
	MDL_Pictures( MDL_DRAWINGS );
}

static void CL_Sprays_f( void ) {
	MDL_Pictures( MDL_SPRAYS );
}

static void MDL_Done( void ) {
	mdlJob_t		*job = &mdlJob;
	unsigned char	sha[WEB_SHA256_SIZE], want[WEB_SHA256_SIZE];
	long long		size = 0;
	char			model[WEB_MODEL_NAME];

	SDL_WaitThread( job->thread, NULL );		// (it is through, this does not wait)
	if ( job->body || job->picture ) {
		MDL_PictureDone( job );
		SDL_AtomicSet( &job->state, MDLJOB_FREE );
		return;
	}
	if ( job->emoteList ) {
		MDL_EmoteListDone( job );
		SDL_AtomicSet( &job->state, MDLJOB_FREE );
		return;
	}
	if ( job->named ) {
		MDL_NamedDone( job );
		SDL_AtomicSet( &job->state, MDLJOB_FREE );
		return;
	}
	Cvar_Set( "v_modelStoreFetching", "" );
	Q_strncpyz( model, job->model, sizeof( model ) );
	if ( !model[0] ) {
		mdlIndex[job->ok ? job->bytes : 0] = '\0';
		mdlIndexTime = cls.realtime;
		SDL_AtomicSet( &job->state, MDLJOB_FREE );
		if ( job->ok ) {
			fileHandle_t f = FS_SV_FOpenFileWrite( va( "%s/%s/index.txt", BASEGAME, FS_MODELSTORE ) );

			// (what was not in the store, or did not come, is looked at again with the new list)
			mdlSeen.num = 0;
			if ( f ) {
				FS_Write( mdlIndex, (int)job->bytes, f );
				FS_FCloseFile( f );
			}
			MDL_Changed();
		} else {
			Com_DPrintf( "The list of the model store did not come: %s\n", job->error );
		}
		return;
	}
	if ( job->icon[0] ) {
		MDL_IconDone( job );
		SDL_AtomicSet( &job->state, MDLJOB_FREE );
		return;
	}
	MDL_See( model );
	Web_SHA256Final( &job->sha, sha );
	if ( !job->ok || job->disk || !WEB_ModelIndexFind( mdlIndex, model, &size, want ) || job->bytes != size
		|| memcmp( sha, want, sizeof( sha ) ) ) {
		Com_Printf( S_COLOR_YELLOW "The model %s did not come from the model store: %s\n", model,
			job->disk ? "it could not be written, is the disk full?" : job->ok ? "it is not the file of the list of the store" : job->error );
		remove( job->osTemp );
		SDL_AtomicSet( &job->state, MDLJOB_FREE );
		return;
	}
	SDL_AtomicSet( &job->state, MDLJOB_FREE );
	MDL_Arrived( model );
}

// The next icon of the list that is not here yet, for the window. qfalse: none.
static qboolean MDL_NextIcon( char *model, char *icon ) {
	const char	*p;
	long long	size;

	for ( p = WEB_ModelIndexNext( mdlIndex, model, icon, &size ); p; p = WEB_ModelIndexNext( p, model, icon, &size ) ) {
		if ( icon[0] && !MDL_Has( &mdlIconsTried, model ) && !FS_SV_FileExists( MDL_IconLocal( model, icon, qfalse ) ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
=================
CL_ModelsFrame

Once a frame: a fetch that is through is taken. While nothing else asks a web server, the model the player asked
for is fetched, else the next model a player here uses and the store has (looked for once a second), else the
next icon for the window, each after the list of the store when that is old.
=================
*/
void CL_ModelsFrame( void ) {
	char			model[WEB_MODEL_NAME], icon[4];
	long long		size;
	unsigned char	sha[WEB_SHA256_SIZE];

	switch ( SDL_AtomicGet( &mdlJob.state ) ) {
	case MDLJOB_DONE:
		MDL_Done();
		return;
	case MDLJOB_FREE:
		break;
	default:
		return;
	}
	if ( !mdlWanted[0] && v_modelStore->integer && cls.realtime - mdlScanTime >= MDL_SCAN_MSEC ) {
		mdlScanTime = cls.realtime;
		if ( cls.state == CA_ACTIVE && !cl_connectedToPureServer && !clc.demoplaying && MDL_NextOfPlayers( model, sizeof( model ) ) ) {
			Q_strncpyz( mdlWanted, model, sizeof( mdlWanted ) );
		}
	}
	if ( CL_UpdateBusy() || CL_DownloadWebAsking() ) {
		return;
	}
	// the pictures of the drawing boards and the sprays first: somebody shows one now
	for ( int kind = 0; kind < MDL_PICTURE_KINDS; kind++ ) {
		if ( mdlPictureUpload[kind] ) {
			mdlPictureUpload[kind] = qfalse;
			MDL_StartPicture( kind, 0 );
			return;
		}
		if ( mdlNumPictureGets[kind] > 0 ) {
			MDL_StartPicture( kind, mdlPictureGets[kind][--mdlNumPictureGets[kind]] );
			return;
		}
	}
	// with nothing of the models to do: the chat emojis and the interactive emojis of the store, in the game (also on
	// a pure server, which reads them from the folder, FS_IsToyFile of qcommon/files.cpp)
	if ( !mdlWanted[0] && !mdlIconsWanted ) {
		if ( v_emojiStore->integer && cls.state == CA_ACTIVE && !clc.demoplaying ) {
			MDL_NextEmoji();
		}
		return;
	}
	if ( mdlIndexTime < 0 || cls.realtime - mdlIndexTime > MDL_INDEX_MSEC ) {
		MDL_Start( NULL, NULL );
		return;
	}
	if ( mdlWanted[0] ) {
		Q_strncpyz( model, mdlWanted, sizeof( model ) );
		mdlWanted[0] = '\0';
		if ( !WEB_ModelIndexFind( mdlIndex, model, &size, sha ) ) {
			Com_DPrintf( "model store: it has no %s\n", model );
			MDL_See( model );
			return;
		}
		MDL_Start( model, NULL );
		return;
	}
	if ( MDL_NextIcon( model, icon ) ) {
		MDL_Start( model, icon );
	} else {
		mdlIconsWanted = qfalse;
	}
}

// v_models: what the store has and what came; v_models get <name>: fetches a model whether a player uses it or not;
// v_models icons: the list and the icons for the window of the cgame
static void CL_Models_f( void ) {
	char model[WEB_MODEL_NAME];

	if ( Cmd_Argc() == 2 && !Q_stricmp( Cmd_Argv( 1 ), "icons" ) ) {
		mdlIconsWanted = qtrue;
		mdlIconsTried.num = 0;
		return;
	}
	if ( Cmd_Argc() == 3 && !Q_stricmp( Cmd_Argv( 1 ), "get" ) ) {
		if ( !WEB_ModelFromInfo( Cmd_Argv( 2 ), model, sizeof( model ) ) ) {
			Com_Printf( "A model name is 2 to 32 of a-z, 0-9 and _\n" );
			return;
		}
		if ( FS_ReadFile( va( "models/players/%s/model.glm", model ), NULL ) > 0 ) {
			Com_Printf( "The game has the model %s already\n", model );
			return;
		}
		Q_strncpyz( mdlWanted, model, sizeof( mdlWanted ) );
		Com_Printf( "%s is fetched from the model store\n", model );
		return;
	}
	if ( Cmd_Argc() != 1 ) {
		Com_Printf( "usage: v_models [get <name> | icons]\n" );
		return;
	}
	Com_Printf( "model store: %s, %s\n", v_modelStoreURL->string, v_modelStore->integer ? "on" : "off (v_modelStore 0)" );
	Com_Printf( "list: %s, %d models looked at this run%s\n", mdlIndexTime < 0 ? "not fetched yet" : "fetched", mdlSeen.num,
		CL_ModelsAsking() ? ", a fetch runs" : "" );
}

void CL_ModelsInit( void ) {
	v_modelStore = Cvar_Get( "v_modelStore", "1", CVAR_ARCHIVE, "Fetch the models of players that the game does not have from the model store of the site (0: only what you ask for)" );
	v_modelStoreChanged = Cvar_Get( "v_modelStoreChanged", "0", CVAR_ROM, "Counts up when the list, an icon or a model of the model store came" );
	Cvar_Get( "v_modelStoreFetching", "", CVAR_ROM, "The model that is fetched from the model store now" );
	v_emojiStore = Cvar_Get( "v_emojiStore", "1", CVAR_ARCHIVE, "Fetch the chat emojis that players uploaded to the site (0: only the emojis of the game)" );
	Cvar_Get( "v_emojiStoreChanged", "0", CVAR_ROM, "Counts up when a chat emoji of the store came or went" );
	Cvar_Get( "v_emoteStoreChanged", "0", CVAR_ROM, "Counts up each time the list or a sound of the interactive emojis arrived (read-only)" );
	v_modelStoreURL = Cvar_Get( "v_modelStoreURL", MDL_URL_DEFAULT, CVAR_INIT | CVAR_ROM | CVAR_PROTECTED,
		"Address of the model store (https). Can only be changed on the command line" );
	Cmd_AddCommand( "v_models", CL_Models_f, "The model store of the site: v_models, v_models get <name>, v_models icons" );
	Cmd_AddCommand( "v_drawings", CL_Drawings_f, "The pictures of the drawing boards in the store of the site: v_drawings upload, v_drawings get <number>" );
	Cmd_AddCommand( "v_emotes", CL_Emotes_f, "Fetches the list of interactive emojis of the store again (usage: v_emotes refresh)" );
	Cmd_AddCommand( "v_sprays", CL_Sprays_f, "The sprays in the store of the site: v_sprays upload <file>, v_sprays get <number>" );
}

// The game ends. A thread that is still asking is told to stop and left to itself.
void CL_ModelsShutdown( void ) {
	mdlJob_t *job = &mdlJob;

	job->cancel = 1;
	if ( SDL_AtomicCAS( &job->state, MDLJOB_RUNNING, MDLJOB_LET_GO ) ) {
		SDL_DetachThread( job->thread );
	} else if ( SDL_AtomicGet( &job->state ) == MDLJOB_DONE ) {
		SDL_WaitThread( job->thread, NULL );
		if ( job->osTemp[0] ) {
			remove( job->osTemp );
		}
		SDL_AtomicSet( &job->state, MDLJOB_FREE );
	}
	Cmd_RemoveCommand( "v_models" );
	Cmd_RemoveCommand( "v_drawings" );
	Cmd_RemoveCommand( "v_sprays" );
	Cmd_RemoveCommand( "v_emotes" );
}

#else // __EMSCRIPTEN__

/*
In a page (the TV of the site, shared/sys/web/page/boot.js) the page fetches: once a second the next model a
player here uses that the game does not have goes to vjk.wantModel() of the page, which fetches it from the store
of its own host, checks it against index.txt there, puts it into base/modelstore of the home folder and says so
with v_modelPak <model>. The TV plays as a demo, so a demo is no reason not to.
*/
#include <emscripten.h>

// v_modelPak <model>: the page put the pak of model in place
static void CL_ModelPak_f( void ) {
	char model[WEB_MODEL_NAME];

	if ( Cmd_Argc() != 2 || !WEB_ModelFromInfo( Cmd_Argv( 1 ), model, sizeof( model ) ) ) {
		Com_Printf( "usage: v_modelPak <model>\n" );
		return;
	}
	MDL_See( model );
	MDL_Use( model );
}

void CL_ModelsInit( void ) {
	v_modelStore = Cvar_Get( "v_modelStore", "1", CVAR_ARCHIVE, "Fetch the models of players that the game does not have from the model store of the site (0: never)" );
	v_modelStoreChanged = Cvar_Get( "v_modelStoreChanged", "0", CVAR_ROM, "Counts up when a model of the model store came" );
	Cmd_AddCommand( "v_modelPak", CL_ModelPak_f, "For the page: the pak of a model of the model store is in place" );
}

void CL_ModelsFrame( void ) {
	char model[WEB_MODEL_NAME];

	if ( !v_modelStore->integer || cls.state != CA_ACTIVE || cls.realtime - mdlScanTime < MDL_SCAN_MSEC ) {
		return;
	}
	mdlScanTime = cls.realtime;
	if ( MDL_NextOfPlayers( model, sizeof( model ) ) ) {
		MDL_See( model );
		EM_ASM( { if ( window.vjk && vjk.wantModel ) vjk.wantModel( UTF8ToString( $0 ) ); }, model );
	}
}

void CL_ModelsShutdown( void ) {
	Cmd_RemoveCommand( "v_modelPak" );
}

qboolean CL_ModelsAsking( void ) { return qfalse; }

#endif
