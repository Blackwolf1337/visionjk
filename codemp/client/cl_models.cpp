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
#define MDL_EMOJI_RECORD	"emojistore.txt"	// in base of the home folder: the emojis of the store that came, a name a line
#define MDL_DRAWING_BYTES	( 4 + 256 * 128 )	// a picture of a drawing board: "VJD1" and BOARD_W x BOARD_H (bg_toys.h)
#define MDL_DRAWING_MOST	( 1500 * 1500 )		// numbers of pictures (TOY_BOARD_SPLIT squared)
#define MDL_DRAWING_ASKS	8					// pictures the cgame asked for that wait

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
	char				emoji[WEB_MODEL_NAME];	// a chat emoji of the store (issue #9 of the game)
	qboolean			emojiIndex;				//	or the list of them
	int					drawing;				// a picture of a drawing board that is fetched
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
static char		mdlEmojiIndex[MDL_INDEX_MOST + 1];
static int		mdlEmojiIndexTime = -1;
static mdlNames_t	mdlEmojiTried;			// emojis that came or failed this run
static mdlNames_t	mdlEmojiHere;			// the emojis of the store in the home folder (MDL_EMOJI_RECORD)
static qboolean	mdlEmojiRead;
static byte		mdlDrawingBody[MDL_DRAWING_BYTES];
static char		mdlAnswer[512];				// what the store answered to an upload
static qboolean	mdlDrawingUpload;			// the own picture waits to go to the store
static int		mdlDrawingGets[MDL_DRAWING_ASKS], mdlNumDrawingGets;

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

The chat emojis of the store (issue #9 of the game): every one is fetched, into base/gfx/emoji of the home folder,
where the chat finds it as any emoji (cgame/chat/cg_chat.c lists them again when v_emojiStoreChanged counts up).
One the store no longer has (its owner took it down) is deleted again: MDL_EMOJI_RECORD says which came from it.

===============================================================================
*/

static const char *MDL_EmojiLocal( const char *name, qboolean temp ) {
	return va( "%s/gfx/emoji/%s.%s", BASEGAME, name, temp ? "tmp" : "png" );
}

static void MDL_EmojiRecordRead( void ) {
	char			text[MDL_SEEN * WEB_MODEL_NAME + 1], *line, *next;
	fileHandle_t	f;
	int				len;

	if ( mdlEmojiRead ) {
		return;
	}
	mdlEmojiRead = qtrue;
	len = FS_SV_FOpenFileRead( va( "%s/%s", BASEGAME, MDL_EMOJI_RECORD ), &f );
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
			MDL_Add( &mdlEmojiHere, line );
		}
	}
}

static void MDL_EmojiRecordWrite( void ) {
	fileHandle_t	f = FS_SV_FOpenFileWrite( va( "%s/%s", BASEGAME, MDL_EMOJI_RECORD ) );
	int				i;

	if ( !f ) {
		return;
	}
	for ( i = 0; i < mdlEmojiHere.num; i++ ) {
		FS_Write( va( "%s\n", mdlEmojiHere.name[i] ), (int)strlen( mdlEmojiHere.name[i] ) + 1, f );
	}
	FS_FCloseFile( f );
}

// Starts the fetch of a chat emoji of the store, or with name NULL of their list.
static void MDL_StartEmoji( const char *name ) {
	mdlJob_t *job = &mdlJob;

	memset( job, 0, sizeof( *job ) );
	if ( !MDL_Url( name ? va( "emoji/files/%s.png", name ) : "emoji/index.txt", job->url, sizeof( job->url ) ) ) {
		return;
	}
	if ( name ) {
		unsigned char	sha[WEB_SHA256_SIZE];
		fileHandle_t	f;

		MDL_Add( &mdlEmojiTried, name );
		if ( !WEB_ModelIndexFind( mdlEmojiIndex, name, &job->most, sha ) || job->most > MDL_EMOJI_MOST ) {
			return;
		}
		f = FS_SV_FOpenFileWrite( MDL_EmojiLocal( name, qtrue ) );
		if ( !f ) {
			return;
		}
		FS_FCloseFile( f );
		Q_strncpyz( job->osTemp, MDL_OSPath( MDL_EmojiLocal( name, qtrue ) ), sizeof( job->osTemp ) );
		Q_strncpyz( job->emoji, name, sizeof( job->emoji ) );
		Web_SHA256Init( &job->sha );
	} else {
		job->emojiIndex = qtrue;
		job->most = MDL_INDEX_MOST;
		job->index = mdlEmojiIndex;
		mdlEmojiIndex[0] = '\0';
	}
	MDL_Launch( job );
}

// The list came: an emoji that came from the store and is no longer in it goes. Or an emoji came: it is kept when
// it is the file of the list.
static void MDL_EmojiDone( mdlJob_t *job ) {
	unsigned char	sha[WEB_SHA256_SIZE], want[WEB_SHA256_SIZE];
	long long		size = 0;
	qboolean		changed = qfalse;
	int				i;

	if ( job->emojiIndex ) {
		mdlEmojiIndex[job->ok ? job->bytes : 0] = '\0';
		mdlEmojiIndexTime = cls.realtime;
		if ( !job->ok ) {
			Com_DPrintf( "The list of the chat emojis of the store did not come: %s\n", job->error );
			return;
		}
		MDL_EmojiRecordRead();
		for ( i = mdlEmojiHere.num - 1; i >= 0; i-- ) {
			if ( !WEB_ModelIndexFind( mdlEmojiIndex, mdlEmojiHere.name[i], &size, want ) ) {
				remove( MDL_OSPath( MDL_EmojiLocal( mdlEmojiHere.name[i], qfalse ) ) );
				memmove( &mdlEmojiHere.name[i], &mdlEmojiHere.name[i + 1], ( mdlEmojiHere.num - i - 1 ) * sizeof( mdlEmojiHere.name[0] ) );
				mdlEmojiHere.num--;
				changed = qtrue;
			}
		}
		if ( changed ) {
			MDL_EmojiRecordWrite();
			Cvar_SetValue( "v_emojiStoreChanged", Cvar_VariableValue( "v_emojiStoreChanged" ) + 1.0f );
		}
		return;
	}
	Web_SHA256Final( &job->sha, sha );
	if ( !job->ok || job->disk || !WEB_ModelIndexFind( mdlEmojiIndex, job->emoji, &size, want ) || job->bytes != size
		|| memcmp( sha, want, sizeof( sha ) ) ) {
		remove( job->osTemp );
		return;
	}
	FS_SV_Rename( MDL_EmojiLocal( job->emoji, qtrue ), MDL_EmojiLocal( job->emoji, qfalse ), qfalse );
	MDL_EmojiRecordRead();
	MDL_Add( &mdlEmojiHere, job->emoji );
	MDL_EmojiRecordWrite();
	Cvar_SetValue( "v_emojiStoreChanged", Cvar_VariableValue( "v_emojiStoreChanged" ) + 1.0f );
}

// The next thing to fetch of the chat emojis: their list when it is old, else the next one not here yet.
static void MDL_NextEmoji( void ) {
	char		name[WEB_MODEL_NAME], icon[4];
	const char	*p;
	long long	size;

	if ( mdlEmojiIndexTime < 0 || cls.realtime - mdlEmojiIndexTime > MDL_INDEX_MSEC ) {
		MDL_StartEmoji( NULL );
		return;
	}
	MDL_EmojiRecordRead();
	for ( p = WEB_ModelIndexNext( mdlEmojiIndex, name, icon, &size ); p; p = WEB_ModelIndexNext( p, name, icon, &size ) ) {
		if ( !MDL_Has( &mdlEmojiTried, name ) && !( MDL_Has( &mdlEmojiHere, name ) && FS_SV_FileExists( MDL_EmojiLocal( name, qfalse ) ) ) ) {
			MDL_StartEmoji( name );
			return;
		}
	}
}

/*
===============================================================================

The pictures of the drawing boards (cgame/toys/cg_board.c, issue #9 of the game): the own one goes to the store of
the site when it is held up (v_drawings upload, from drawings/upload.vjd, which the cgame wrote), and its number
goes back to the cgame (v_boardPicture), which says it to the others. The picture of a number another player holds
is fetched into base/drawings/<number>.vjd of the home folder (v_drawings get), where the cgame reads it.

===============================================================================
*/

static const char *MDL_DrawingLocal( int number, qboolean temp ) {
	return va( "%s/drawings/%i.%s", BASEGAME, number, temp ? "tmp" : "vjd" );
}

// Starts the fetch of picture get, or with get 0 the upload of the own one.
static void MDL_StartDrawing( int get ) {
	mdlJob_t *job = &mdlJob;

	memset( job, 0, sizeof( *job ) );
	if ( get ) {
		fileHandle_t f;

		if ( !MDL_Url( va( "drawings/files/%i.vjd", get ), job->url, sizeof( job->url ) ) ) {
			return;
		}
		f = FS_SV_FOpenFileWrite( MDL_DrawingLocal( get, qtrue ) );
		if ( !f ) {
			return;
		}
		FS_FCloseFile( f );
		Q_strncpyz( job->osTemp, MDL_OSPath( MDL_DrawingLocal( get, qtrue ) ), sizeof( job->osTemp ) );
		job->drawing = get;
		job->most = MDL_DRAWING_BYTES;
		Web_SHA256Init( &job->sha );
	} else {
		void	*data;
		int		len = FS_ReadFile( "drawings/upload.vjd", &data );

		if ( len != MDL_DRAWING_BYTES || memcmp( data, "VJD1", 4 ) || !MDL_Url( "drawings/api/upload", job->url, sizeof( job->url ) ) ) {
			if ( len > 0 ) {
				FS_FreeFile( data );
			}
			return;
		}
		Com_Memcpy( mdlDrawingBody, data, MDL_DRAWING_BYTES );
		FS_FreeFile( data );
		job->body = mdlDrawingBody;
		job->bodyLen = MDL_DRAWING_BYTES;
		job->index = mdlAnswer;
		job->most = sizeof( mdlAnswer ) - 1;
	}
	MDL_Launch( job );
}

// The store answered the upload ({"id": <number>, "key": ..}), or a picture came.
static void MDL_DrawingDone( mdlJob_t *job ) {
	if ( job->body ) {
		const char	*id;
		int			number;

		mdlAnswer[job->bytes] = '\0';
		id = strstr( mdlAnswer, "\"id\":" );
		number = id ? atoi( id + 5 ) : 0;
		if ( !job->ok || number <= 0 || number >= MDL_DRAWING_MOST ) {
			Com_Printf( S_COLOR_YELLOW "The drawing did not go to the store of the site: %s\n", job->status == 429 ? "too many at once, it goes again later" : job->error );
			return;
		}
		if ( cls.cgameStarted ) {
			Cmd_TokenizeString( va( "v_boardPicture %i", number ) );
			CGVM_ConsoleCommand();
		}
		return;
	}
	if ( !job->ok || job->disk || job->bytes != MDL_DRAWING_BYTES ) {
		remove( job->osTemp );
		return;
	}
	{
		FILE		*f = fopen( job->osTemp, "rb" );
		char		head[4] = { 0 };
		qboolean	good = (qboolean)( f && fread( head, 1, 4, f ) == 4 && !memcmp( head, "VJD1", 4 ) );

		if ( f ) {
			fclose( f );
		}
		if ( !good ) {
			remove( job->osTemp );
			return;
		}
	}
	FS_SV_Rename( MDL_DrawingLocal( job->drawing, qtrue ), MDL_DrawingLocal( job->drawing, qfalse ), qfalse );
}

// v_drawings upload: the own picture to the store; v_drawings get <number>: a picture of another board
static void CL_Drawings_f( void ) {
	const char	*what = Cmd_Argv( 1 );
	int			number, i;

	if ( !Q_stricmp( what, "upload" ) ) {
		mdlDrawingUpload = qtrue;
		return;
	}
	if ( Q_stricmp( what, "get" ) || ( number = atoi( Cmd_Argv( 2 ) ) ) <= 0 || number >= MDL_DRAWING_MOST ) {
		Com_Printf( "usage: v_drawings upload | get <number>\n" );
		return;
	}
	for ( i = 0; i < mdlNumDrawingGets; i++ ) {
		if ( mdlDrawingGets[i] == number ) {
			return;
		}
	}
	if ( mdlNumDrawingGets < MDL_DRAWING_ASKS ) {
		mdlDrawingGets[mdlNumDrawingGets++] = number;
	}
}

static void MDL_Done( void ) {
	mdlJob_t		*job = &mdlJob;
	unsigned char	sha[WEB_SHA256_SIZE], want[WEB_SHA256_SIZE];
	long long		size = 0;
	char			model[WEB_MODEL_NAME];

	SDL_WaitThread( job->thread, NULL );		// (it is through, this does not wait)
	if ( job->body || job->drawing ) {
		MDL_DrawingDone( job );
		SDL_AtomicSet( &job->state, MDLJOB_FREE );
		return;
	}
	if ( job->emojiIndex || job->emoji[0] ) {
		MDL_EmojiDone( job );
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
	// the pictures of the drawing boards first: somebody holds one up now
	if ( mdlDrawingUpload ) {
		mdlDrawingUpload = qfalse;
		MDL_StartDrawing( 0 );
		return;
	}
	if ( mdlNumDrawingGets > 0 ) {
		MDL_StartDrawing( mdlDrawingGets[--mdlNumDrawingGets] );
		return;
	}
	// with nothing of the models to do: the chat emojis of the store, in the game (not on a pure server)
	if ( !mdlWanted[0] && !mdlIconsWanted ) {
		if ( v_emojiStore->integer && cls.state == CA_ACTIVE && !cl_connectedToPureServer && !clc.demoplaying ) {
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
	v_modelStoreURL = Cvar_Get( "v_modelStoreURL", MDL_URL_DEFAULT, CVAR_INIT | CVAR_ROM | CVAR_PROTECTED,
		"Address of the model store (https). Can only be changed on the command line" );
	Cmd_AddCommand( "v_models", CL_Models_f, "The model store of the site: v_models, v_models get <name>, v_models icons" );
	Cmd_AddCommand( "v_drawings", CL_Drawings_f, "The pictures of the drawing boards in the store of the site: v_drawings upload, v_drawings get <number>" );
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
