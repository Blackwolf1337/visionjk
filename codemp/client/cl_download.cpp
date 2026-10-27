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

// cl_download.cpp -- what a player gets of the paks a server offers. Not from OpenJK, see docs/japlus/downloads.md.
//
// Before anything is downloaded a window lists the paks the server offers (v_downloadReview 1): the player
// ticks the ones he wants and says whether they are kept (<game>/downloads) or are for this session only
// (<game>/downloads/session, emptied when the game starts and ends, qcommon/files.cpp). The paks of both folders
// are loaded only while a server references them.
//
// A pak is sent whole, so what is in it is known only when it is here. Then, before anything of it is used, a
// second window shows what is in every pak that came, in kinds (maps, models, sounds, programs, ...), and the
// player unticks what he does not want. Programs (libraries and executables) are unticked from the start: a pak
// can bring a module that the engine would unpack and run (Sys_UnpackDLL). What is unticked is written next to the
// pak as <pak>.blocked, which the file system reads: nothing can open or list those files, and the pak keeps its
// checksum for a pure server. Programs are blocked that way for every pak that is downloaded, with the window or
// without it.
//
// A server that names a web address for its paks (jp_DlBaseURL, as JA+ servers do) has them fetched from there
// (see "Over the web" below), through both windows the same way.

#include "client.h"
#include "qcommon/stringed_ingame.h"
#include "web/web_missing.h"
#include <minizip/unzip.h>
#include <time.h>
#if !defined( __EMSCRIPTEN__ )
	#include <SDL.h>
	#include "sys/sys_http.h"
#endif

#define DL_PAKS			32			// paks one window lists at most
#define DL_PROGRAMS		8			// programs of a pak that are named
#define DL_NAME			256			// a path in a pak, with its end
#define DL_HITS			80			// what can be clicked in a window
#define DL_W			500.0f		// the window
#define DL_TOP			40.0f
#define DL_PAD			12.0f
#define DL_ROW			17.0f
#define DL_SCALE		0.62f		// of the letters of a row
#define DL_ROWS_SHOWN	17			// rows of the list of the second window that fit

typedef enum {
	CAT_MAPS,
	CAT_MODELS,
	CAT_TEXTURES,
	CAT_SOUNDS,
	CAT_SCRIPTS,
	CAT_MENUS,
	CAT_CONFIGS,
	CAT_OTHER,
	CAT_PROGRAMS,
	DL_CATS
} dlCat_t;

typedef enum {
	STAGE_NONE,
	STAGE_CHOOSE,		// the first window: which paks, kept or for this session
	STAGE_LOADING,		// they are downloaded
	STAGE_REVIEW		// the second window: what is in those that came
} dlStage_t;

typedef enum {
	HIT_PAK,			// the box of a pak in the first window
	HIT_KEEP,			// kept, or arg 0: this session only
	HIT_DOWNLOAD,
	HIT_WITHOUT,
	HIT_DISCONNECT,
	HIT_CAT,			// the box of a kind of a pak in the second window: arg is pak * DL_CATS + kind
	HIT_USE
} dlHitKind_t;

typedef struct dlPak_s {
	char		remote[MAX_QPATH];		// as the server names it, with .pk3
	char		local[MAX_OSPATH];		// where it goes, in the home folder
	qboolean	want;
	qboolean	arrived;
	int			count[DL_CATS];			// files of every kind in it,
	int			bytes[DL_CATS];			//	and how large they are unpacked
	qboolean	keep[DL_CATS];			// the player wants them
	char		programs[DL_PROGRAMS][MAX_QPATH];
	int			numPrograms;
} dlPak_t;

static struct {
	dlStage_t	stage;
	dlPak_t		paks[DL_PAKS];
	int			numPaks;
	qboolean	keep;					// into downloads, qfalse: into downloads/session
	int			scroll;					// rows of the second window above what is shown
	struct {
		dlHitKind_t	kind;
		int			arg;
		float		x, y, w, h;
	} hits[DL_HITS];
	int			numHits;
} dl;

static cvar_t *v_downloadReview;
static cvar_t *v_downloadKeep;
static cvar_t *v_downloadHttp;

static const vec4_t dlFill = { 0.035f, 0.07f, 0.12f, 0.94f }, dlEdge = { 0.43f, 0.83f, 1.0f, 0.45f };
static const vec4_t dlText = { 0.86f, 0.92f, 0.97f, 1.0f }, dlDim = { 0.60f, 0.69f, 0.78f, 1.0f };
static const vec4_t dlAccent = { 0.43f, 0.83f, 1.0f, 1.0f }, dlRed = { 1.0f, 0.42f, 0.36f, 1.0f };
static const vec4_t dlGold = { 0.89f, 0.84f, 0.36f, 1.0f };

static void	DL_Start( void );
static void	DL_Without( void );
static void	DL_Use( void );

/*
=================
CL_DownloadReview_f

v_downloadReview_do download | without | use | keep <0 or 1> | pak <n> | kind <pak> <kind> | skip | skipall: what a
click in the windows does, for a key or a script (pak and kind switch a box, counted from 0 in the order the window
shows; skip and skipall are the buttons of the overlay while a pak comes from a web address).
=================
*/
static void CL_DownloadReview_f( void ) {
	const char	*what = Cmd_Argv( 1 );
	const int	a = atoi( Cmd_Argv( 2 ) ), k = atoi( Cmd_Argv( 3 ) );

	if ( dl.stage == STAGE_CHOOSE && !Q_stricmp( what, "download" ) ) {
		DL_Start();
	} else if ( dl.stage == STAGE_CHOOSE && !Q_stricmp( what, "without" ) ) {
		DL_Without();
	} else if ( dl.stage == STAGE_CHOOSE && !Q_stricmp( what, "keep" ) ) {
		dl.keep = (qboolean)( a != 0 );
	} else if ( dl.stage == STAGE_CHOOSE && !Q_stricmp( what, "pak" ) && a >= 0 && a < dl.numPaks ) {
		dl.paks[a].want = (qboolean)!dl.paks[a].want;
	} else if ( dl.stage == STAGE_REVIEW && !Q_stricmp( what, "kind" ) && a >= 0 && a < dl.numPaks && k >= 0 && k < DL_CATS ) {
		dl.paks[a].keep[k] = (qboolean)!dl.paks[a].keep[k];
	} else if ( dl.stage == STAGE_REVIEW && !Q_stricmp( what, "use" ) ) {
		DL_Use();
	} else if ( CL_DownloadWebBusy() && !Q_stricmp( what, "skip" ) ) {
		CL_DownloadWebSkip();
	} else if ( CL_DownloadWebBusy() && !Q_stricmp( what, "skipall" ) ) {
		CL_DownloadWebSkipAll();
	} else {
		Com_Printf( "v_downloadReview_do download | without | keep <0 or 1> | pak <n> (the first window), kind <pak> <kind> | use (the second),"
			" skip | skipall (while a pak comes from a web address)\n" );
	}
}

void CL_DownloadInit( void ) {
	Cmd_AddCommand( "v_downloadReview_do", CL_DownloadReview_f, "What a click in the windows of a download does, for a key or a script" );
	v_downloadReview = Cvar_Get( "v_downloadReview", "1", CVAR_ARCHIVE_ND, "Before a download: choose the paks of the server, and after it what of them is used. 0: all of them, programs in them stay blocked" );
	v_downloadKeep = Cvar_Get( "v_downloadKeep", "1", CVAR_ARCHIVE_ND, "Downloaded paks are kept in <game>/downloads. 0: in downloads/session, for this session only" );
	v_downloadHttp = Cvar_Get( "v_downloadHttp", "1", CVAR_ARCHIVE_ND, "Paks of a server that names a web address for them (jp_DlBaseURL) come from there. 0: only from the server itself" );
}

void CL_DownloadReviewReset( void ) {
	memset( &dl, 0, sizeof( dl ) );
}

// a window is open: the per file question of cl_downloadPrompt is not asked
qboolean CL_DownloadReviewActive( void ) {
	return ( dl.stage != STAGE_NONE ) ? qtrue : qfalse;
}

/*
===============================================================================

What is in a pak

===============================================================================
*/

static qboolean DL_Ext( const char *name, const char *const *exts ) {
	const char *dot = strrchr( name, '.' );
	int i;

	if ( !dot || strchr( dot, '/' ) ) {
		return qfalse;
	}
	for ( i = 0; exts[i]; i++ ) {
		if ( !Q_stricmp( dot + 1, exts[i] ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

// the kind of a file of a pak, by its folder and its extension
static dlCat_t DL_Kind( const char *name ) {
	// what a system could run: libraries, executables, scripts of a shell, and the byte code of a game module
	static const char *const programs[] = { "dll", "so", "dylib", "exe", "bat", "cmd", "com", "scr", "msi", "ps1", "sh", "vbs",
		"jar", "app", "bin", "pif", "qvm", NULL };
	static const char *const maps[] = { "bsp", "aas", "nav", NULL };
	static const char *const models[] = { "glm", "gla", "md3", "mdr", "skin", NULL };
	static const char *const pictures[] = { "jpg", "jpeg", "tga", "png", "dds", "efx", NULL };
	static const char *const sounds[] = { "wav", "mp3", "ogg", "roq", NULL };
	static const char *const configs[] = { "cfg", NULL };

	if ( DL_Ext( name, programs ) || Q_stristr( name, ".so." ) ) {
		return CAT_PROGRAMS;
	}
	if ( DL_Ext( name, configs ) ) {
		return CAT_CONFIGS;
	}
	if ( !Q_stricmpn( name, "maps/", 5 ) || !Q_stricmpn( name, "levelshots/", 11 ) || DL_Ext( name, maps ) ) {
		return CAT_MAPS;
	}
	if ( !Q_stricmpn( name, "models/", 7 ) || DL_Ext( name, models ) ) {
		return CAT_MODELS;
	}
	if ( !Q_stricmpn( name, "sound/", 6 ) || !Q_stricmpn( name, "music/", 6 ) || !Q_stricmpn( name, "video/", 6 ) || DL_Ext( name, sounds ) ) {
		return CAT_SOUNDS;
	}
	if ( !Q_stricmpn( name, "ui/", 3 ) || !Q_stricmpn( name, "menus/", 6 ) || COM_CompareExtension( name, ".menu" ) ) {
		return CAT_MENUS;
	}
	if ( !Q_stricmpn( name, "shaders/", 8 ) || !Q_stricmpn( name, "scripts/", 8 ) || !Q_stricmpn( name, "ext_data/", 9 )
		|| COM_CompareExtension( name, ".shader" ) ) {
		return CAT_SCRIPTS;
	}
	if ( !Q_stricmpn( name, "textures/", 9 ) || !Q_stricmpn( name, "gfx/", 4 ) || !Q_stricmpn( name, "effects/", 8 ) || DL_Ext( name, pictures ) ) {
		return CAT_TEXTURES;
	}
	return CAT_OTHER;
}

// where a pak of the home folder lies on the disk
static const char *DL_Path( const char *local ) {
	char *path = FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), local, "" );

	path[strlen( path ) - 1] = '\0';
	return path;
}

// Goes through the files of a pak: counts them by kind (count), or writes those of the kinds the player does not
// want to the list next to the pak (block). qfalse: it is no pak that can be read.
static qboolean DL_Files( dlPak_t *pak, qboolean count, fileHandle_t block ) {
	unzFile			uf = unzOpen( DL_Path( pak->local ) );
	unz_global_info	gi;
	unz_file_info	info;
	char			name[DL_NAME];
	uLong			i;

	if ( !uf ) {
		return qfalse;
	}
	if ( unzGetGlobalInfo( uf, &gi ) != UNZ_OK || unzGoToFirstFile( uf ) != UNZ_OK ) {
		unzClose( uf );
		return qfalse;
	}
	for ( i = 0; i < gi.number_entry; i++ ) {
		if ( unzGetCurrentFileInfo( uf, &info, name, sizeof( name ), NULL, 0, NULL, 0 ) != UNZ_OK ) {
			break;
		}
		// minizip leaves a name that fills the buffer without its end
		name[sizeof( name ) - 1] = '\0';
		// (a folder is no file)
		if ( name[0] && name[strlen( name ) - 1] != '/' ) {
			const dlCat_t kind = DL_Kind( name );

			if ( count ) {
				pak->count[kind]++;
				pak->bytes[kind] = (int)Q_min( (long long)pak->bytes[kind] + (long long)info.uncompressed_size, 0x7fffffffLL );
				if ( kind == CAT_PROGRAMS && pak->numPrograms < DL_PROGRAMS ) {
					Q_strncpyz( pak->programs[pak->numPrograms++], name, sizeof( pak->programs[0] ) );
				}
			}
			if ( block && !pak->keep[kind] ) {
				Q_strlwr( name );
				FS_Write( name, (int)strlen( name ), block );
				FS_Write( "\n", 1, block );
			}
		}
		if ( unzGoToNextFile( uf ) != UNZ_OK ) {
			break;
		}
	}
	unzClose( uf );
	return qtrue;
}

// the list of what the player does not want of a pak, next to it (qcommon/files.cpp reads it)
static void DL_WriteBlocked( dlPak_t *pak ) {
	const fileHandle_t f = FS_SV_FOpenFileWrite( va( "%s.blocked", pak->local ) );

	if ( !f ) {
		Com_Printf( S_COLOR_YELLOW "Could not write %s.blocked: the programs in it are not blocked\n", pak->local );
		return;
	}
	DL_Files( pak, qfalse, f );
	FS_FCloseFile( f );
}

/*
===============================================================================

The way of a download

===============================================================================
*/

// the pak of this window that goes to local, NULL: none
static dlPak_t *DL_PakFor( const char *local ) {
	int i;

	for ( i = 0; i < dl.numPaks; i++ ) {
		if ( !Q_stricmp( dl.paks[i].local, local ) ) {
			return &dl.paks[i];
		}
	}
	return NULL;
}

// Where a pak goes: what FS_ComparePaks() named in the folder of downloads, or in its folder of the session.
const char *CL_DownloadLocalName( const char *local ) {
	static char	out[MAX_OSPATH];
	const char	*in = Q_stristr( local, "/" FS_DOWNLOADS "/" );
	const qboolean keep = ( dl.stage != STAGE_NONE ) ? dl.keep : (qboolean)( v_downloadKeep->integer != 0 );

	if ( keep || !in || Q_stristr( local, "/" FS_DOWNLOADS_SESSION "/" ) ) {
		return local;
	}
	Com_sprintf( out, sizeof( out ), "%.*s/" FS_DOWNLOADS_SESSION "/%s", (int)( in - local ), local, in + strlen( "/" FS_DOWNLOADS "/" ) );
	return out;
}

/*
=================
CL_DownloadReviewBegin

The server offers what clc.downloadList says ("@remote@local" again and again). With v_downloadReview the first
window opens and nothing is downloaded until the player chose. qfalse: no window, download as before.
=================
*/
qboolean CL_DownloadReviewBegin( void ) {
	char	list[sizeof( clc.downloadList )];
	char	*s, *remote, *local, *next;

	CL_DownloadReviewReset();
	if ( !v_downloadReview->integer ) {
		return qfalse;
	}
	Q_strncpyz( list, clc.downloadList, sizeof( list ) );
	s = ( list[0] == '@' ) ? list + 1 : list;
	while ( *s && dl.numPaks < DL_PAKS ) {
		remote = s;
		if ( !( local = strchr( s, '@' ) ) ) {
			break;
		}
		*local++ = '\0';
		if ( ( next = strchr( local, '@' ) ) != NULL ) {
			*next = '\0';
		}
		Q_strncpyz( dl.paks[dl.numPaks].remote, remote, sizeof( dl.paks[0].remote ) );
		Q_strncpyz( dl.paks[dl.numPaks].local, local, sizeof( dl.paks[0].local ) );
		dl.paks[dl.numPaks].want = qtrue;
		dl.numPaks++;
		if ( !next ) {
			break;
		}
		s = next + 1;
	}
	if ( !dl.numPaks ) {
		return qfalse;
	}
	dl.keep = (qboolean)( v_downloadKeep->integer != 0 );
	dl.stage = STAGE_CHOOSE;
	clc.downloadMenuActive = qtrue;
	return qtrue;
}

// "Download": the paks that were ticked, in the list of the engine again
static void DL_Start( void ) {
	int i;

	clc.downloadList[0] = '\0';
	for ( i = 0; i < dl.numPaks; i++ ) {
		if ( dl.paks[i].want ) {
			// (where it goes now, kept or for this session: CL_DownloadArrived() knows it by that name)
			Q_strncpyz( dl.paks[i].local, CL_DownloadLocalName( dl.paks[i].local ), sizeof( dl.paks[i].local ) );
			Q_strcat( clc.downloadList, sizeof( clc.downloadList ), va( "@%s@%s", dl.paks[i].remote, dl.paks[i].local ) );
		}
	}
	if ( !clc.downloadList[0] ) {
		return;
	}
	dl.stage = STAGE_LOADING;
	CL_NextDownload();
}

// "Join without": nothing is downloaded
static void DL_Without( void ) {
	clc.downloadList[0] = '\0';
	dl.stage = STAGE_NONE;
	CL_DownloadsComplete();
}

/*
=================
CL_DownloadArrived

A pak came whole and has the checksum the server named: the programs in it are blocked at once, whatever comes
next, and what is in it is counted for the second window.
=================
*/
void CL_DownloadArrived( const char *local ) {
	dlPak_t	*pak = DL_PakFor( local ), one;
	int		k;

	if ( !COM_CompareExtension( local, ".pk3" ) ) {
		return;
	}
	if ( !pak ) {
		// (without the window: as if it had been shown and nothing changed in it)
		memset( &one, 0, sizeof( one ) );
		Q_strncpyz( one.local, local, sizeof( one.local ) );
		pak = &one;
	}
	for ( k = 0; k < DL_CATS; k++ ) {
		pak->keep[k] = (qboolean)( k != CAT_PROGRAMS );
		pak->count[k] = pak->bytes[k] = 0;
	}
	pak->numPrograms = 0;
	if ( !DL_Files( pak, qtrue, 0 ) ) {
		return;
	}
	pak->arrived = qtrue;
	DL_WriteBlocked( pak );
	if ( pak->count[CAT_PROGRAMS] ) {
		Com_Printf( S_COLOR_YELLOW "%s has %i program files in it, they are blocked: %s%s\n", local, pak->count[CAT_PROGRAMS], pak->programs[0],
			pak->count[CAT_PROGRAMS] > 1 ? " ..." : "" );
	}
}

/*
=================
CL_DownloadReviewEnd

The last download ended. qtrue: the second window opens for the paks that came, and the game waits for it.
=================
*/
qboolean CL_DownloadReviewEnd( void ) {
	int i;

	if ( dl.stage != STAGE_LOADING ) {
		dl.stage = STAGE_NONE;
		return qfalse;
	}
	for ( i = 0; i < dl.numPaks && !dl.paks[i].arrived; i++ )
		;
	if ( i == dl.numPaks ) {
		dl.stage = STAGE_NONE;
		return qfalse;
	}
	dl.stage = STAGE_REVIEW;
	dl.scroll = 0;
	clc.downloadMenuActive = qtrue;
	return qtrue;
}

// "Use them": what was unticked is blocked, and the game goes on
static void DL_Use( void ) {
	int i;

	for ( i = 0; i < dl.numPaks; i++ ) {
		if ( dl.paks[i].arrived ) {
			DL_WriteBlocked( &dl.paks[i] );
		}
	}
	dl.stage = STAGE_NONE;
	CL_DownloadsComplete();
}

/*
===============================================================================

Over the web

A server that names a web address for its paks (jp_DlBaseURL, which JA+ servers set for the client plugin of
JA+) has them fetched from there, a pak at a time, in place of the download through the game. Which paks and
where they go is what the first window chose, and what came goes through the checksum, the blocked programs
and the second window like any other download (CL_NextDownload). The address is the one the plugin asks for
(its ui, FUN_4002c510): <jp_DlBaseURL><game folder>/<name>.pk3 with the name escaped, here with a slash put
after the base when it has none. No redirect is followed, by the plugin neither. A pak that does not come from
there is asked from the server itself, if that sends files.

The fetch runs in a thread of its own, since Sys_HttpGet() waits; the game looks at it once a frame and never
waits for it. A fetch the game no longer wants (a disconnect, a new level) is told to stop and left to itself,
its slot is free again when its thread ends. Only one question to a web server runs at a time in the client:
sys/sys_http.h is made for one, so this and the updater (cl_update.cpp) never ask at the same time.

===============================================================================
*/

#define DL_WEB_BASE		200							// of jp_DlBaseURL, as much as the plugin takes of it
#define DL_WEB_JOBS		4							// the fetch that runs, and those that were let go and still end
#define DL_WEB_MOST		( 512LL * 1024 * 1024 )		// bytes of a pak at most: the checksum is only tested at the end

/*
=================
DL_WebPublicHost

Whether the host of an address is one of the internet. A server names jp_DlBaseURL, and the fetch goes out from
the player's machine: without this a server could make it ask a router, a printer or a service on the machine
itself for whatever address it likes. Refused are names of this machine, and written out addresses of this
machine, of private networks, of the link and of carrier NAT. A name that the DNS turns into such an address is
not seen here.
=================
*/
static qboolean DL_WebPublicHost( const char *url ) {
	const char	*host = strstr( url, "://" ) + 3;
	char		name[128];
	int			len = 0, part[4], parts = 0, digits = 0, i;
	qboolean	numeric = qtrue;

	while ( host[len] && host[len] != '/' && host[len] != ':' && host[len] != '?' && host[len] != '#' ) {
		// a user in front of the host (user@host) and an IPv6 address are not taken at all
		if ( host[len] == '@' || host[len] == '[' || host[len] == ']' || len >= (int)sizeof( name ) - 1 ) {
			return qfalse;
		}
		name[len] = (char)tolower( (unsigned char)host[len] );
		len++;
	}
	name[len] = '\0';
	if ( !len || !strcmp( name, "localhost" ) || COM_CompareExtension( name, ".localhost" ) || COM_CompareExtension( name, ".local" ) ) {
		return qfalse;
	}
	for ( i = 0; i < len; i++ ) {
		if ( !isdigit( (unsigned char)name[i] ) && name[i] != '.' ) {
			numeric = qfalse;
		}
	}
	if ( !numeric ) {
		// a name, unless it is a number in another way of writing (0x7f.1)
		return (qboolean)!( name[0] == '0' && name[1] == 'x' );
	}
	// digits and dots: only the usual four parts, anything else is a way of writing an address the resolver reads
	for ( i = 0; i <= len; i++ ) {
		if ( name[i] == '.' || !name[i] ) {
			if ( !digits || parts == 4 ) {
				return qfalse;
			}
			part[parts++] = atoi( name + i - digits );
			if ( part[parts - 1] > 255 || ( digits > 1 && name[i - digits] == '0' ) ) {
				return qfalse;
			}
			digits = 0;
		} else {
			digits++;
		}
	}
	if ( parts != 4 ) {
		return qfalse;
	}
	return (qboolean)!( part[0] == 0 || part[0] == 10 || part[0] == 127 || ( part[0] == 169 && part[1] == 254 )
		|| ( part[0] == 172 && part[1] >= 16 && part[1] <= 31 ) || ( part[0] == 192 && part[1] == 168 )
		|| ( part[0] == 100 && part[1] >= 64 && part[1] <= 127 ) || part[0] >= 224 );
}

// the address the paks of this server come from, with a slash at its end. qfalse: it names none of the web.
static qboolean DL_WebBase( char *out, int outSize ) {
	const char	*base;
	int			i;

	// (bit 14 of cp_pluginDisable is the plugin's AllowDownloadURL_Redirect: set, its web download is off)
	if ( !v_downloadHttp || !v_downloadHttp->integer || ( Cvar_VariableIntegerValue( "cp_pluginDisable" ) & ( 1 << 14 ) )
		|| cls.state < CA_CONNECTED ) {
		return qfalse;
	}
	base = Info_ValueForKey( cl.gameState.stringData + cl.gameState.stringOffsets[CS_SYSTEMINFO], "jp_DlBaseURL" );
	if ( Q_stricmpn( base, "http://", 7 ) && Q_stricmpn( base, "https://", 8 ) ) {
		static char told[DL_WEB_BASE];

		// an ftp address, which server.cfg of JA+ names as possible: said once, the paks come from the server
		if ( base[0] && strcmp( told, base ) ) {
			Q_strncpyz( told, base, sizeof( told ) );
			Com_Printf( S_COLOR_YELLOW "The download address of this server is not an http or https address, it is not used: %s\n", base );
		}
		return qfalse;
	}
	if ( strlen( base ) >= DL_WEB_BASE || (int)strlen( base ) + 2 > outSize ) {
		return qfalse;
	}
	for ( i = 0; base[i]; i++ ) {
		if ( (unsigned char)base[i] <= ' ' || (unsigned char)base[i] >= 127 || base[i] == '"' ) {
			return qfalse;
		}
	}
	if ( !DL_WebPublicHost( base ) ) {
		static char told[DL_WEB_BASE];

		// (asked more than once a connection, said once)
		if ( strcmp( told, base ) ) {
			Q_strncpyz( told, base, sizeof( told ) );
			Com_Printf( S_COLOR_YELLOW "The download address of this server is not one of the internet, it is not used: %s\n", base );
		}
		return qfalse;
	}
	Com_sprintf( out, outSize, "%s%s", base, ( base[strlen( base ) - 1] == '/' ) ? "" : "/" );
	return qtrue;
}

// a character an address may have as it is
static qboolean DL_WebPlain( int ch ) {
	return (qboolean)( ( ch >= 'A' && ch <= 'Z' ) || ( ch >= 'a' && ch <= 'z' ) || ( ch >= '0' && ch <= '9' )
		|| ch == '-' || ch == '_' || ch == '.' || ch == '~' );
}

// The address of remote as the server names it ("<game folder>/<name>.pk3"). The game folder is taken as it is,
// so it has to be plain, the name is escaped. qfalse: no address can be made of it.
static qboolean DL_WebUrl( const char *base, const char *remote, char *out, int outSize ) {
	const char	*slash = strchr( remote, '/' );
	const int	end = (int)strlen( remote ) - 4;		// (the .pk3)
	int			len, i;

	if ( !slash || slash == remote || end <= (int)( slash - remote ) + 1 || !COM_CompareExtension( remote, ".pk3" ) ) {
		return qfalse;
	}
	for ( i = 0; remote + i < slash; i++ ) {
		if ( !DL_WebPlain( (unsigned char)remote[i] ) ) {
			return qfalse;
		}
	}
	if ( (int)strlen( base ) + (int)( slash - remote ) + 1 >= outSize ) {
		return qfalse;
	}
	Com_sprintf( out, outSize, "%s%.*s/", base, (int)( slash - remote ), remote );
	len = (int)strlen( out );
	for ( i = (int)( slash - remote ) + 1; i < end; i++ ) {
		const int ch = (unsigned char)remote[i];

		if ( len + 3 + 4 >= outSize ) {
			return qfalse;
		}
		if ( DL_WebPlain( ch ) ) {
			out[len++] = (char)ch;
		} else {
			Com_sprintf( out + len, 4, "%%%02X", ch );
			len += 3;
		}
	}
	Q_strncpyz( out + len, ".pk3", outSize - len );
	return qtrue;
}

// the host of a web address, for the first window
static const char *DL_WebHost( const char *url ) {
	static char	host[DL_WEB_BASE];
	const char	*s = strstr( url, "://" );

	s = s ? s + 3 : url;
	Q_strncpyz( host, s, sizeof( host ) );
	host[strcspn( host, "/?#" )] = '\0';
	return host;
}

qboolean CL_DownloadWebOffered( void ) {
	char base[DL_WEB_BASE + 2];

	return DL_WebBase( base, sizeof( base ) );
}

// the server sends files itself (no sv_allowDownload in its info is a yes, as for CL_InitDownloads)
qboolean CL_ServerSendsDownloads( void ) {
	const char *allow = Info_ValueForKey( cl.gameState.stringData + cl.gameState.stringOffsets[CS_SERVERINFO], "sv_allowDownload" );

	return (qboolean)( !allow[0] || atoi( allow ) );
}

#if !defined( __EMSCRIPTEN__ )

enum {
	WEBJOB_FREE,
	WEBJOB_WAITING,			// for the updater to be through, or for a fetch that was let go
	WEBJOB_RUNNING,
	WEBJOB_DONE,			// the thread is through and waits for the game
	WEBJOB_LET_GO			// the game wants it no more: the thread frees the slot when it is through
};

typedef struct dlWebJob_s {
	SDL_atomic_t		state;
	SDL_Thread			*thread;
	char				url[DL_WEB_BASE + 3 * MAX_QPATH + 8];
	char				osTemp[MAX_OSPATH];		// the .tmp of the download, the thread writes it
	volatile int		cancel;
	volatile long long	bytes;					// what came so far
	// the thread writes these, the game reads them once the state is WEBJOB_DONE
	FILE				*f;
	qboolean			ok, disk;
	int					status;					// the answer of the web server, 404: it does not have the pak
	char				error[256];
	// the game's: for the list of what the web address does not have
	char				base[DL_WEB_BASE + 2];
	char				remote[MAX_QPATH];
} dlWebJob_t;

static dlWebJob_t	dlJobs[DL_WEB_JOBS];
static dlWebJob_t	*dlJob;				// the fetch of the pak that is downloaded now, NULL: none

static qboolean DL_WebSink( void *ctx, const void *data, size_t len ) {
	dlWebJob_t *job = (dlWebJob_t *)ctx;

	if ( job->bytes + (long long)len > DL_WEB_MOST ) {
		return qfalse;
	}
	if ( fwrite( data, 1, len, job->f ) != len ) {
		job->disk = qtrue;
		return qfalse;
	}
	job->bytes += (long long)len;
	return qtrue;
}

// the thread of a fetch: nothing of the engine but Sys_HttpGet
static int DL_WebThread( void *data ) {
	dlWebJob_t *job = (dlWebJob_t *)data;

	job->f = fopen( job->osTemp, "wb" );
	if ( !job->f ) {
		job->disk = qtrue;
	} else {
		job->ok = Sys_HttpGet( job->url, DL_WebSink, job, &job->cancel, job->error, sizeof( job->error ), &job->status );
		if ( fclose( job->f ) != 0 ) {
			job->disk = qtrue;
		}
		job->f = NULL;
	}
	// (let go of: nobody but this thread knows the slot any more)
	if ( !SDL_AtomicCAS( &job->state, WEBJOB_RUNNING, WEBJOB_DONE ) ) {
		SDL_AtomicSet( &job->state, WEBJOB_FREE );
	}
	return 0;
}

// a thread of a fetch other than job still asks its web server (one that was let go ends within a second or so)
static qboolean DL_WebOthersAsking( const dlWebJob_t *job ) {
	int i;

	for ( i = 0; i < DL_WEB_JOBS; i++ ) {
		const int state = SDL_AtomicGet( &dlJobs[i].state );

		if ( &dlJobs[i] != job && ( state == WEBJOB_RUNNING || state == WEBJOB_LET_GO ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
=================
The paks the web address does not have

A pak the web server answered 404 for is noted in japlus/urlRedirect_cache.txt of the home folder, where the client
plugin of JA+ kept them, and is not asked there again for six days (shared/web/web_missing.c). The next connect
then goes straight to the server itself, or on without the pak, in place of a question that fails each time.
=================
*/
#define DL_MISSING_FILE		"japlus/urlRedirect_cache.txt"
#define DL_MISSING_MOST		32768

static char dlMissing[DL_MISSING_MOST], dlMissingNew[DL_MISSING_MOST];

static void DL_MissingRead( void ) {
	fileHandle_t	f;
	int				len = FS_SV_FOpenFileRead( DL_MISSING_FILE, &f );

	dlMissing[0] = '\0';
	if ( !f ) {
		return;
	}
	if ( len > 0 ) {
		len = FS_Read( dlMissing, Q_min( len, (int)sizeof( dlMissing ) - 1 ), f );
		dlMissing[Q_max( len, 0 )] = '\0';
	}
	FS_FCloseFile( f );
}

static qboolean DL_MissingListed( const char *base, const char *remote ) {
	DL_MissingRead();
	return WEB_MissingListed( dlMissing, base, remote, (long long)time( NULL ) );
}

static void DL_MissingNote( const char *base, const char *remote ) {
	fileHandle_t	f;
	size_t			len;

	DL_MissingRead();
	len = WEB_MissingAdd( dlMissing, base, remote, (long long)time( NULL ), dlMissingNew, sizeof( dlMissingNew ) );
	if ( !len ) {
		return;
	}
	f = FS_SV_FOpenFileWrite( DL_MISSING_FILE );
	if ( f ) {
		FS_Write( dlMissingNew, (int)len, f );
		FS_FCloseFile( f );
	}
}

// The pak does not come from the web address: from the server itself, if it sends files, else the next one.
static void DL_WebElsewhere( void ) {
	if ( CL_ServerSendsDownloads() ) {
		Com_Printf( "It is asked from the server itself.\n" );
		CL_DownloadUdpStart();
		return;
	}
	Com_Printf( S_COLOR_YELLOW "The server sends no files itself, the game goes on without it.\n" );
	CL_NextDownload();
}

static void DL_WebFailed( const char *why ) {
	Com_Printf( S_COLOR_YELLOW "%s did not come from the web address of the server: %s\n", cl_downloadName->string, why );
	FS_Remove( DL_Path( clc.downloadTempName ) );
	clc.downloadCount = clc.downloadSize = 0;
	Cvar_Set( "cl_downloadCount", "0" );
	Cvar_Set( "cl_downloadSize", "0" );
	DL_WebElsewhere();
}

/*
=================
CL_DownloadWebStart

The pak of clc.downloadName from the web address of the server, if it names one. qfalse: it names none, or no
fetch can be started: the pak is asked from the server itself.
=================
*/
qboolean CL_DownloadWebStart( void ) {
	char			base[DL_WEB_BASE + 2];
	dlWebJob_t		*job = NULL;
	fileHandle_t	f;
	int				i;

	if ( dlJob || !DL_WebBase( base, sizeof( base ) ) ) {
		return qfalse;
	}
	for ( i = 0; i < DL_WEB_JOBS && !job; i++ ) {
		if ( SDL_AtomicGet( &dlJobs[i].state ) == WEBJOB_FREE ) {
			job = &dlJobs[i];
		}
	}
	if ( !job ) {
		Com_Printf( S_COLOR_YELLOW "Fetches that were stopped have not ended yet, %s is asked from the server\n", cl_downloadName->string );
		return qfalse;
	}
	memset( job, 0, sizeof( *job ) );
	if ( !DL_WebUrl( base, cl_downloadName->string, job->url, sizeof( job->url ) ) ) {
		return qfalse;
	}
	if ( DL_MissingListed( base, cl_downloadName->string ) ) {
		Com_Printf( "%s was not at the web address of the server when it was last asked there, it is not asked again for "
			"six days (%s).\n", cl_downloadName->string, DL_MISSING_FILE );
		DL_WebElsewhere();
		return qtrue;
	}
	Q_strncpyz( job->base, base, sizeof( job->base ) );
	Q_strncpyz( job->remote, cl_downloadName->string, sizeof( job->remote ) );
	// the folders and the file are made here, the thread calls nothing of the engine
	f = FS_SV_FOpenFileWrite( clc.downloadTempName );
	if ( !f ) {
		return qfalse;
	}
	FS_FCloseFile( f );
	Q_strncpyz( job->osTemp, DL_Path( clc.downloadTempName ), sizeof( job->osTemp ) );
	clc.downloadSize = 0;		// (not known before it is here: the overlay says so)
	Cvar_Set( "cl_downloadSize", "0" );
	SDL_AtomicSet( &job->state, WEBJOB_WAITING );
	dlJob = job;
	Com_Printf( "Downloading %s from %s\n", cl_downloadName->string, job->url );
	CL_DownloadWebFrame();
	return qtrue;
}

/*
=================
CL_DownloadWebFrame

Once a frame: the fetch starts when nothing else asks a web server, says how far it is, and when it is through
the pak goes the way of every download.
=================
*/
void CL_DownloadWebFrame( void ) {
	dlWebJob_t	*job = dlJob;
	qboolean	ok, disk;
	char		why[256];

	if ( !job ) {
		return;
	}
	switch ( SDL_AtomicGet( &job->state ) ) {
	case WEBJOB_WAITING:
		if ( CL_UpdateBusy() || DL_WebOthersAsking( job ) ) {
			return;
		}
		SDL_AtomicSet( &job->state, WEBJOB_RUNNING );
		job->thread = SDL_CreateThread( DL_WebThread, "download", job );
		if ( !job->thread ) {
			SDL_AtomicSet( &job->state, WEBJOB_FREE );
			dlJob = NULL;
			DL_WebFailed( "no thread could be started for it" );
		}
		return;
	case WEBJOB_RUNNING:
		clc.downloadCount = (int)job->bytes;
		Cvar_SetValue( "cl_downloadCount", (float)clc.downloadCount );
		return;
	case WEBJOB_DONE:
		break;
	default:
		return;
	}

	SDL_WaitThread( job->thread, NULL );		// (it is through, this does not wait)
	dlJob = NULL;
	clc.downloadCount = (int)job->bytes;
	Cvar_SetValue( "cl_downloadCount", (float)clc.downloadCount );
	ok = job->ok;
	disk = job->disk;
	Q_strncpyz( why, job->error, sizeof( why ) );
	if ( !ok && !disk && job->status == 404 ) {
		DL_MissingNote( job->base, job->remote );
	}
	SDL_AtomicSet( &job->state, WEBJOB_FREE );

	if ( disk ) {
		DL_WebFailed( "it could not be written, is the disk full?" );
		return;
	}
	if ( !ok ) {
		DL_WebFailed( why );
		return;
	}
	// Checked before it is a pak: a wrong file of the web address is not the server's fault (CL_NextDownload would
	// drop the connection for it), and the engine deletes no file named .pk3.
	if ( !FS_CompareZipChecksum( DL_Path( clc.downloadTempName ) ) ) {
		DL_WebFailed( "it is not the pak the server has (another checksum)" );
		return;
	}
	FS_SV_Rename( clc.downloadTempName, clc.downloadName, qfalse );
	clc.downloadSize = clc.downloadCount;
	Cvar_SetValue( "cl_downloadSize", (float)clc.downloadSize );
	CL_NextDownload();
}

// The download is over without its pak (a disconnect, a new level): the fetch is told to stop and left to itself.
void CL_DownloadWebStop( void ) {
	dlWebJob_t *job = dlJob;

	if ( !job ) {
		return;
	}
	dlJob = NULL;
	job->cancel = 1;
	if ( SDL_AtomicCAS( &job->state, WEBJOB_WAITING, WEBJOB_FREE ) ) {
		return;
	}
	if ( SDL_AtomicCAS( &job->state, WEBJOB_RUNNING, WEBJOB_LET_GO ) ) {
		SDL_DetachThread( job->thread );
		return;
	}
	// it was through already
	SDL_WaitThread( job->thread, NULL );
	SDL_AtomicSet( &job->state, WEBJOB_FREE );
}

// a pak of this connection comes from a web address
qboolean CL_DownloadWebBusy( void ) {
	return (qboolean)( dlJob != NULL );
}

/*
=================
CL_DownloadWebSkip

The Abort and Abort All of the plugin, the buttons of the overlay while a pak comes from the web address: this pak,
or it and every one after it, is not downloaded, and the game goes on as without them. (A pak from the server
itself has no such button: what of it is still on the way would be taken for the next one.)
=================
*/
static void DL_WebSkip( qboolean all ) {
	if ( !dlJob ) {
		return;
	}
	Com_Printf( all ? "%s and the paks after it are not downloaded.\n" : "%s is not downloaded.\n", cl_downloadName->string );
	CL_DownloadWebStop();
	FS_Remove( DL_Path( clc.downloadTempName ) );
	clc.downloadCount = clc.downloadSize = 0;
	Cvar_Set( "cl_downloadCount", "0" );
	Cvar_Set( "cl_downloadSize", "0" );
	if ( all ) {
		clc.downloadList[0] = '\0';
	}
	CL_NextDownload();
}

void CL_DownloadWebSkip( void ) {
	DL_WebSkip( qfalse );
}

void CL_DownloadWebSkipAll( void ) {
	DL_WebSkip( qtrue );
}

// a thread of this file asks a web server now, also one that was let go (the updater waits for it)
qboolean CL_DownloadWebAsking( void ) {
	return (qboolean)( dlJob || DL_WebOthersAsking( NULL ) );
}

#else // __EMSCRIPTEN__: a page gets its files another way

qboolean	CL_DownloadWebStart( void ) { return qfalse; }
void		CL_DownloadWebFrame( void ) {}
void		CL_DownloadWebStop( void ) {}
qboolean	CL_DownloadWebBusy( void ) { return qfalse; }
qboolean	CL_DownloadWebAsking( void ) { return qfalse; }
void		CL_DownloadWebSkip( void ) {}
void		CL_DownloadWebSkipAll( void ) {}

#endif

/*
===============================================================================

The windows

===============================================================================
*/

// a text of this tree in the language of the game: strings/<language>/vision.str under the key that
// tools/menus/texts.py made from the English (it checks that it is that one), else the English
static const char *DL_Text( const char *key, const char *english ) {
	static char	out[4][512];
	static int	index;
	char		*text = out[index++ & 3];
	const char	*found = SE_GetString( va( "VISION_%s", key ) );

	if ( !found || !found[0] ) {
		return english;
	}
	if ( CL_UTF8() ) {
		Q_UTF8_FromCP1252( found, text, sizeof( out[0] ) );
	} else {
		Q_strncpyz( text, found, sizeof( out[0] ) );
	}
	return text;
}

static void DL_Hit( dlHitKind_t kind, int arg, float x, float y, float w, float h ) {
	if ( dl.numHits < DL_HITS ) {
		dl.hits[dl.numHits].kind = kind;
		dl.hits[dl.numHits].arg = arg;
		dl.hits[dl.numHits].x = x;
		dl.hits[dl.numHits].y = y;
		dl.hits[dl.numHits].w = w;
		dl.hits[dl.numHits].h = h;
		dl.numHits++;
	}
}

static qboolean DL_Over( float x, float y, float w, float h ) {
	return ( cls.cursorX >= x && cls.cursorX < x + w && cls.cursorY >= y && cls.cursorY < y + h ) ? qtrue : qfalse;
}

static void DL_Rect( float x, float y, float w, float h, const vec4_t fill, const vec4_t edge ) {
	CL_DrawMenuRect( x, y, w, h, 1.0f, (float *)fill, (float *)edge );
}

// The letters of the new look (Noto Sans, "vision_sans" of the pack of this tree), else those of the engine. (Asked
// for once a window is drawn: the renderer hands out the handle it has for the name.)
static int dlFont;

static float DL_Width( const char *text, float scale ) {
	return (float)re->Font_StrLenPixels( text, dlFont, scale );
}

// a line of text with its top at y, cut with three dots where it would be wider than most (0: as wide as it is)
static void DL_Print( float x, float y, float scale, const vec4_t color, const char *text, float most ) {
	char line[256];

	if ( most > 0.0f && DL_Width( text, scale ) > most ) {
		int n = (int)strlen( text );

		Q_strncpyz( line, text, sizeof( line ) );
		n = Q_min( n, (int)sizeof( line ) - 4 );
		while ( n > 0 ) {
			line[n] = '\0';
			Q_strcat( line, sizeof( line ), "..." );
			if ( DL_Width( line, scale ) <= most ) {
				break;
			}
			n--;
		}
		text = line;
	}
	re->Font_DrawString( (int)x, (int)y, text, color, dlFont, -1, scale );
}

// a box that is ticked or not, and the text after it, the whole row to click
static void DL_Tick( dlHitKind_t kind, int arg, float x, float y, float w, qboolean on, const vec4_t color, const char *text ) {
	const vec4_t	lit = { 0.43f, 0.83f, 1.0f, 0.12f };
	const float		box = 10.0f;

	if ( DL_Over( x, y, w, DL_ROW ) ) {
		DL_Rect( x - 3.0f, y, w + 6.0f, DL_ROW, lit, NULL );
	}
	DL_Rect( x, y + ( DL_ROW - box ) * 0.5f, box, box, NULL, dlEdge );
	if ( on ) {
		DL_Rect( x + 2.0f, y + ( DL_ROW - box ) * 0.5f + 2.0f, box - 4.0f, box - 4.0f, dlAccent, NULL );
	}
	DL_Print( x + box + 7.0f, y + 2.0f, DL_SCALE, color, text, w - box - 7.0f );
	DL_Hit( kind, arg, x, y, w, DL_ROW );
}

// a button: its text in the middle, lit under the pointer. Not usable: dim, nothing to click.
static void DL_Button( dlHitKind_t kind, float x, float y, float w, const char *text, qboolean usable ) {
	const vec4_t	back = { 1.0f, 1.0f, 1.0f, 0.07f }, lit = { 0.43f, 0.83f, 1.0f, 0.3f };
	const float		h = 20.0f, scale = 0.68f;
	const qboolean	over = (qboolean)( usable && DL_Over( x, y, w, h ) );

	DL_Rect( x, y, w, h, over ? lit : back, usable ? dlEdge : NULL );
	DL_Print( x + ( w - DL_Width( text, scale ) ) * 0.5f, y + 3.0f, scale, usable ? dlText : dlDim, text, w - 8.0f );
	if ( usable ) {
		DL_Hit( kind, 0, x, y, w, h );
	}
}

static const char *DL_Size( int bytes ) {
	if ( bytes >= 1024 * 1024 ) {
		return va( "%.1f MB", bytes / ( 1024.0f * 1024.0f ) );
	}
	if ( bytes >= 1024 ) {
		return va( "%i KB", bytes / 1024 );
	}
	return va( "%i B", bytes );
}

// the first window: which paks, kept or for this session
static void DL_DrawChoose( void ) {
	const float	x = ( SCREEN_WIDTH - DL_W ) * 0.5f, inner = DL_W - 2.0f * DL_PAD;
	const int	shown = Q_min( dl.numPaks, 12 );
	char		base[DL_WEB_BASE + 2];
	const qboolean web = DL_WebBase( base, sizeof( base ) );
	const float	h = DL_PAD + 22.0f + 30.0f + ( web ? 13.0f : 0.0f ) + shown * DL_ROW + 12.0f + 2.0f * DL_ROW + 34.0f + 20.0f + DL_PAD;
	float		y = DL_TOP;
	qboolean	any = qfalse;
	int			i;

	DL_Rect( x, y, DL_W, h, dlFill, dlEdge );
	y += DL_PAD;
	DL_Print( x + DL_PAD, y, 0.85f, colorWhite, DL_Text( "FILES_OF_THE_SERVER", "Files of the server" ), inner );
	y += 22.0f;
	DL_Print( x + DL_PAD, y, 0.55f, dlDim, DL_Text( "THE_SERVER_OFFERS_THESE_FILES_TO_DOW", "The server offers these files to download." ), inner );
	DL_Print( x + DL_PAD, y + 13.0f, 0.55f, dlDim, DL_Text( "BEFORE_ANYTHING_OF_THEM_IS_USED_YOU",
		"Before anything of them is used you see what is inside. Programs stay blocked." ), inner );
	if ( web ) {
		char line[DL_WEB_BASE + 128];

		// (the host after the text, never inside it: a text of a string file is no format)
		Com_sprintf( line, sizeof( line ), "%s %s", DL_Text( "THEY_COME_FROM_THE_WEB_ADDRESS", "They come from the web address:" ), DL_WebHost( base ) );
		DL_Print( x + DL_PAD, y + 26.0f, 0.55f, dlAccent, line, inner );
		y += 13.0f;
	}
	y += 30.0f;
	for ( i = 0; i < shown; i++ ) {
		DL_Tick( HIT_PAK, i, x + DL_PAD, y, inner, dl.paks[i].want, dlText, dl.paks[i].remote );
		any = (qboolean)( any || dl.paks[i].want );
		y += DL_ROW;
	}
	for ( ; i < dl.numPaks; i++ ) {
		any = (qboolean)( any || dl.paks[i].want );
	}
	y += 12.0f;
	DL_Tick( HIT_KEEP, 1, x + DL_PAD, y, inner, dl.keep, dlText, DL_Text( "KEEP_THEM_DOWNLOADS_FOLDER", "Keep them (downloads folder)" ) );
	y += DL_ROW;
	DL_Tick( HIT_KEEP, 0, x + DL_PAD, y, inner, (qboolean)!dl.keep, dlText, DL_Text( "THIS_SESSION_ONLY", "This session only" ) );
	y += DL_ROW + 6.0f;
	DL_Print( x + DL_PAD, y, 0.55f, dlGold, DL_Text( "A_PURE_SERVER_MAY_NOT_LET_YOU_PLAY_W",
		"A pure server may not let you play without its files." ), inner );
	y += 28.0f;
	{
		const float gap = 8.0f, w = ( inner - 2.0f * gap ) / 3.0f;

		DL_Button( HIT_DOWNLOAD, x + DL_PAD, y, w, DL_Text( "DOWNLOAD", "Download" ), any );
		DL_Button( HIT_WITHOUT, x + DL_PAD + w + gap, y, w, DL_Text( "JOIN_WITHOUT", "Join without" ), qtrue );
		DL_Button( HIT_DISCONNECT, x + DL_PAD + 2.0f * ( w + gap ), y, w, DL_Text( "DISCONNECT", "Disconnect" ), qtrue );
	}
}

static const char *DL_KindName( dlCat_t kind ) {
	switch ( kind ) {
	case CAT_MAPS:		return DL_Text( "MAPS", "Maps" );
	case CAT_MODELS:	return DL_Text( "MODELS", "Models" );
	case CAT_TEXTURES:	return DL_Text( "TEXTURES_AND_EFFECTS", "Textures and effects" );
	case CAT_SOUNDS:	return DL_Text( "SOUNDS_AND_MUSIC", "Sounds and music" );
	case CAT_SCRIPTS:	return DL_Text( "SHADERS_AND_SCRIPTS", "Shaders and scripts" );
	case CAT_MENUS:		return DL_Text( "MENUS_AND_HUD", "Menus and HUD" );
	case CAT_CONFIGS:	return DL_Text( "CONFIGS_THEY_CAN_RUN_COMMANDS", "Configs (they can run commands)" );
	case CAT_OTHER:		return DL_Text( "OTHER_FILES", "Other files" );
	case CAT_PROGRAMS:	return DL_Text( "PROGRAMS_THEY_COULD_RUN_ON_YOUR_COMP", "Programs (they could run on your computer)" );
	default:			return "";
	}
}

// the second window: what is in the paks that came, by kind, a row each, and the programs by name
static void DL_DrawReview( void ) {
	const float	x = ( SCREEN_WIDTH - DL_W ) * 0.5f, inner = DL_W - 2.0f * DL_PAD;
	const float	listTop = DL_TOP + DL_PAD + 22.0f + 30.0f;
	int			row = 0, i, k, rows, shown;
	float		y = DL_TOP, h;

	// rows: a pak, its kinds that have files, its programs by name
	for ( i = 0; i < dl.numPaks; i++ ) {
		const dlPak_t *pak = &dl.paks[i];

		if ( !pak->arrived ) {
			continue;
		}
		row++;
		for ( k = 0; k < DL_CATS; k++ ) {
			row += pak->count[k] ? 1 : 0;
		}
		row += pak->numPrograms;
	}
	rows = row;
	// (as high as its list, up to DL_ROWS_SHOWN rows, and the wheel moves the rest)
	shown = Com_Clampi( 4, DL_ROWS_SHOWN, rows );
	h = listTop - DL_TOP + shown * DL_ROW + 12.0f + 20.0f + DL_PAD;
	DL_Rect( x, y, DL_W, h, dlFill, dlEdge );
	y += DL_PAD;
	DL_Print( x + DL_PAD, y, 0.85f, colorWhite, DL_Text( "WHAT_IS_INSIDE", "What is inside" ), inner );
	y += 22.0f;
	DL_Print( x + DL_PAD, y, 0.55f, dlDim, DL_Text( "UNTICK_WHAT_YOU_DO_NOT_WANT_NOTHING",
		"Untick what you do not want: nothing of it is used." ), inner );
	DL_Print( x + DL_PAD, y + 13.0f, 0.55f, dlDim, DL_Text( "PROGRAMS_COULD_RUN_ON_YOUR_COMPUTER",
		"Programs could run on your computer: they are blocked unless you tick them." ), inner );

	dl.scroll = Com_Clampi( 0, Q_max( 0, rows - DL_ROWS_SHOWN ), dl.scroll );
	row = 0;
	y = listTop;
	for ( i = 0; i < dl.numPaks; i++ ) {
		const dlPak_t	*pak = &dl.paks[i];
		int				total = 0;

		if ( !pak->arrived ) {
			continue;
		}
		for ( k = 0; k < DL_CATS; k++ ) {
			total += pak->bytes[k];
		}
		if ( row >= dl.scroll && row < dl.scroll + DL_ROWS_SHOWN ) {
			DL_Print( x + DL_PAD, y + 2.0f, DL_SCALE, dlAccent, pak->remote, inner - 80.0f );
			DL_Print( x + DL_W - DL_PAD - DL_Width( DL_Size( total ), DL_SCALE ), y + 2.0f, DL_SCALE, dlDim, DL_Size( total ), 0.0f );
			y += DL_ROW;
		}
		row++;
		for ( k = 0; k < DL_CATS; k++ ) {
			const char *size;

			if ( !pak->count[k] ) {
				continue;
			}
			if ( row >= dl.scroll && row < dl.scroll + DL_ROWS_SHOWN ) {
				size = va( "%i, %s", pak->count[k], DL_Size( pak->bytes[k] ) );
				DL_Tick( HIT_CAT, i * DL_CATS + k, x + DL_PAD + 14.0f, y, inner - 14.0f - 110.0f, pak->keep[k],
					( k == CAT_PROGRAMS ) ? dlRed : ( k == CAT_CONFIGS ) ? dlGold : dlText, DL_KindName( (dlCat_t)k ) );
				DL_Print( x + DL_W - DL_PAD - DL_Width( size, DL_SCALE ), y + 2.0f, DL_SCALE, dlDim, size, 0.0f );
				y += DL_ROW;
			}
			row++;
			if ( k == CAT_PROGRAMS ) {
				int p;

				for ( p = 0; p < pak->numPrograms; p++, row++ ) {
					if ( row >= dl.scroll && row < dl.scroll + DL_ROWS_SHOWN ) {
						DL_Print( x + DL_PAD + 42.0f, y + 2.0f, 0.55f, dlRed, pak->programs[p], inner - 42.0f );
						y += DL_ROW;
					}
				}
			}
		}
	}
	if ( rows > DL_ROWS_SHOWN ) {
		const float track = DL_ROWS_SHOWN * DL_ROW;
		const vec4_t thumb = { 0.43f, 0.83f, 1.0f, 0.6f };

		DL_Rect( x + DL_W - 5.0f, listTop + track * dl.scroll / rows, 3.0f, track * DL_ROWS_SHOWN / rows, thumb, NULL );
	}
	y = listTop + shown * DL_ROW + 12.0f;
	{
		const float gap = 8.0f, w = ( inner - gap ) / 2.0f;

		DL_Button( HIT_USE, x + DL_PAD, y, w, DL_Text( "USE_THEM", "Use them" ), qtrue );
		DL_Button( HIT_DISCONNECT, x + DL_PAD + w + gap, y, w, DL_Text( "DISCONNECT", "Disconnect" ), qtrue );
	}
}

// Once a frame from CL_DrawEngineMenus(). qfalse: no window of this file is open (the engine draws its own).
qboolean CL_DownloadReviewDraw( void ) {
	dl.numHits = 0;
	if ( dl.stage == STAGE_CHOOSE || dl.stage == STAGE_REVIEW ) {
		dlFont = re->RegisterFont( "vision_sans" );
		if ( !dlFont ) {
			dlFont = cls.menuFont;
		}
	}
	if ( dl.stage == STAGE_CHOOSE ) {
		DL_DrawChoose();
		return qtrue;
	}
	if ( dl.stage == STAGE_REVIEW ) {
		DL_DrawReview();
		return qtrue;
	}
	return qfalse;
}

// A key or a button while a window of this file is open. qfalse: none is.
qboolean CL_DownloadReviewKey( int key ) {
	int i;

	if ( dl.stage != STAGE_CHOOSE && dl.stage != STAGE_REVIEW ) {
		return qfalse;
	}
	if ( key == A_MWHEELUP || key == A_MWHEELDOWN ) {
		dl.scroll += ( key == A_MWHEELUP ) ? -2 : 2;
		return qtrue;
	}
	if ( key != A_MOUSE1 ) {
		return qtrue;
	}
	for ( i = dl.numHits - 1; i >= 0; i-- ) {
		if ( !DL_Over( dl.hits[i].x, dl.hits[i].y, dl.hits[i].w, dl.hits[i].h ) ) {
			continue;
		}
		switch ( dl.hits[i].kind ) {
		case HIT_PAK:
			dl.paks[dl.hits[i].arg].want = (qboolean)!dl.paks[dl.hits[i].arg].want;
			break;
		case HIT_KEEP:
			dl.keep = (qboolean)( dl.hits[i].arg != 0 );
			break;
		case HIT_DOWNLOAD:
			DL_Start();
			break;
		case HIT_WITHOUT:
			DL_Without();
			break;
		case HIT_DISCONNECT:
			// (what came stays with its programs blocked: CL_DownloadArrived())
			CL_DownloadReviewReset();
			CL_Disconnect_f();
			break;
		case HIT_CAT: {
			dlPak_t *pak = &dl.paks[dl.hits[i].arg / DL_CATS];
			const int kind = dl.hits[i].arg % DL_CATS;

			pak->keep[kind] = (qboolean)!pak->keep[kind];
			break;
		}
		case HIT_USE:
			DL_Use();
			break;
		}
		return qtrue;
	}
	return qtrue;
}
