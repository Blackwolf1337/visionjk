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
// One question to a web server runs at a time in the client (sys/sys_http.h): this file, the updater
// (cl_update.cpp) and the paks of a server (cl_download.cpp) wait for each other. The fetch runs in a thread of its
// own, the game looks at it once a frame and never waits for it.

#include "client.h"
#include "cl_cgameapi.h"
#include "web/web_http.h"
#include "web/web_models.h"

#if !defined( __EMSCRIPTEN__ )

#include <SDL.h>
#include "sys/sys_http.h"

#define MDL_URL_DEFAULT		"https://jka.vision/models/"
#define MDL_URL				256
#define MDL_INDEX_MOST		( 256 * 1024 )		// bytes of index.txt at most
#define MDL_INDEX_MSEC		( 10 * 60 * 1000 )	// index.txt is fetched again after this long, when a model is wanted
#define MDL_SCAN_MSEC		1000				// the players are looked at once a second
#define MDL_SEEN			256					// models that were looked at this run

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
} mdlJob_t;

static cvar_t	*v_modelStore;
static cvar_t	*v_modelStoreURL;

static mdlJob_t	mdlJob;
static char		mdlIndex[MDL_INDEX_MOST + 1];
static int		mdlIndexTime = -1;			// cls.realtime of the list, -1: none yet
static int		mdlScanTime;
static char		mdlSeen[MDL_SEEN][WEB_MODEL_NAME];	// fetched, failed, not in the store or here already
static int		mdlNumSeen;
static char		mdlWanted[WEB_MODEL_NAME];	// the model that is fetched next (v_models get, or a player)

static qboolean MDL_Seen( const char *model ) {
	int i;

	for ( i = 0; i < mdlNumSeen; i++ ) {
		if ( !strcmp( mdlSeen[i], model ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

static void MDL_See( const char *model ) {
	if ( !MDL_Seen( model ) && mdlNumSeen < MDL_SEEN ) {
		Q_strncpyz( mdlSeen[mdlNumSeen++], model, sizeof( mdlSeen[0] ) );
	}
}

// the home path of the pak of a model, or of the part of it that comes in
static const char *MDL_Local( const char *model, qboolean temp ) {
	return va( "%s/%s/%s.%s", BASEGAME, FS_MODELSTORE, model, temp ? "tmp" : "pk3" );
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
		job->ok = Sys_HttpGet( job->url, MDL_Sink, job, &job->cancel, job->error, sizeof( job->error ), &job->status );
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

static void MDL_Start( const char *model ) {
	mdlJob_t *job = &mdlJob;

	memset( job, 0, sizeof( *job ) );
	if ( !MDL_Url( model ? va( "files/%s.pk3", model ) : "index.txt", job->url, sizeof( job->url ) ) ) {
		Com_Printf( S_COLOR_YELLOW "v_modelStoreURL is no https address, the model store is not asked\n" );
		Cvar_Set( "v_modelStore", "0" );
		return;
	}
	if ( model ) {
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
	SDL_AtomicSet( &job->state, MDLJOB_RUNNING );
	job->thread = SDL_CreateThread( MDL_Thread, "models", job );
	if ( !job->thread ) {
		SDL_AtomicSet( &job->state, MDLJOB_FREE );
		if ( model ) {
			MDL_See( model );
		}
	}
}

// The model that came is shown: its pak goes to the head of the path, the renderer looks again for what it did not
// find, and the cgame loads the players with it again.
static void MDL_Arrived( const char *model ) {
	char why[128];

	FS_SV_Rename( MDL_Local( model, qtrue ), MDL_Local( model, qfalse ), qfalse );
	if ( !FS_AddModelPak( model, why, sizeof( why ) ) ) {
		Com_Printf( S_COLOR_YELLOW "The model %s of the model store is not used: %s\n", model, why );
		remove( MDL_OSPath( MDL_Local( model, qfalse ) ) );
		return;
	}
	Com_Printf( "The model %s came from the model store\n", model );
	if ( re && re->ext.ForgetMissing ) {
		re->ext.ForgetMissing( va( "models/players/%s/", model ) );
	}
	if ( cls.cgameStarted ) {
		Cmd_TokenizeString( va( "v_modelArrived %s", model ) );
		CGVM_ConsoleCommand();
	}
}

static void MDL_Done( void ) {
	mdlJob_t		*job = &mdlJob;
	unsigned char	sha[WEB_SHA256_SIZE], want[WEB_SHA256_SIZE];
	long long		size = 0;
	char			model[WEB_MODEL_NAME];

	SDL_WaitThread( job->thread, NULL );		// (it is through, this does not wait)
	Q_strncpyz( model, job->model, sizeof( model ) );
	if ( !model[0] ) {
		mdlIndex[job->ok ? job->bytes : 0] = '\0';
		mdlIndexTime = cls.realtime;
		if ( job->ok ) {
			// (what was not in the store, or did not come, is looked at again with the new list)
			mdlNumSeen = 0;
		} else {
			Com_DPrintf( "The list of the model store did not come: %s\n", job->error );
		}
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

// The next model a player here uses that this game does not have. qfalse: none.
static qboolean MDL_NextOfPlayers( char *out, size_t outSize ) {
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const char *cs = cl.gameState.stringData + cl.gameState.stringOffsets[CS_PLAYERS + i];
		char		model[WEB_MODEL_NAME];

		if ( !cs[0] || !WEB_ModelFromInfo( Info_ValueForKey( cs, "model" ), model, sizeof( model ) ) || MDL_Seen( model ) ) {
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
CL_ModelsFrame

Once a frame: a fetch that is through is taken, and once a second, while nothing else asks a web server, the next
model a player here uses and the store has is fetched (after the list of the store, when that is old).
=================
*/
void CL_ModelsFrame( void ) {
	char			model[WEB_MODEL_NAME];
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
	if ( !v_modelStore->integer || cls.realtime - mdlScanTime < MDL_SCAN_MSEC ) {
		return;
	}
	mdlScanTime = cls.realtime;
	if ( !mdlWanted[0] ) {
		if ( cls.state != CA_ACTIVE || cl_connectedToPureServer || clc.demoplaying || !MDL_NextOfPlayers( model, sizeof( model ) ) ) {
			return;
		}
		Q_strncpyz( mdlWanted, model, sizeof( mdlWanted ) );
	}
	if ( CL_UpdateBusy() || CL_DownloadWebAsking() ) {
		return;
	}
	if ( mdlIndexTime < 0 || cls.realtime - mdlIndexTime > MDL_INDEX_MSEC ) {
		MDL_Start( NULL );
		return;
	}
	Q_strncpyz( model, mdlWanted, sizeof( model ) );
	mdlWanted[0] = '\0';
	if ( !WEB_ModelIndexFind( mdlIndex, model, &size, sha ) ) {
		MDL_See( model );
		return;
	}
	MDL_Start( model );
}

// v_models: what the store has and what came; v_models get <name>: fetches a model whether a player uses it or not
static void CL_Models_f( void ) {
	char model[WEB_MODEL_NAME];

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
		mdlScanTime = cls.realtime - MDL_SCAN_MSEC;
		Com_Printf( "%s is fetched from the model store%s\n", model, v_modelStore->integer ? "" : " when v_modelStore is 1" );
		return;
	}
	if ( Cmd_Argc() != 1 ) {
		Com_Printf( "usage: v_models [get <name>]\n" );
		return;
	}
	Com_Printf( "model store: %s, %s\n", v_modelStoreURL->string, v_modelStore->integer ? "on" : "off (v_modelStore 0)" );
	Com_Printf( "list: %s, %d models looked at this run%s\n", mdlIndexTime < 0 ? "not fetched yet" : "fetched", mdlNumSeen,
		CL_ModelsAsking() ? ", a fetch runs" : "" );
}

void CL_ModelsInit( void ) {
	v_modelStore = Cvar_Get( "v_modelStore", "1", CVAR_ARCHIVE, "Fetch the models of players that the game does not have from the model store of the site (0: never)" );
	v_modelStoreURL = Cvar_Get( "v_modelStoreURL", MDL_URL_DEFAULT, CVAR_INIT | CVAR_ROM | CVAR_PROTECTED,
		"Address of the model store (https). Can only be changed on the command line" );
	Cmd_AddCommand( "v_models", CL_Models_f, "The model store of the site: v_models, or v_models get <name>" );
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
}

#else // __EMSCRIPTEN__: the page of the TV gets the models another way

void		CL_ModelsInit( void ) {}
void		CL_ModelsFrame( void ) {}
void		CL_ModelsShutdown( void ) {}
qboolean	CL_ModelsAsking( void ) { return qfalse; }

#endif
