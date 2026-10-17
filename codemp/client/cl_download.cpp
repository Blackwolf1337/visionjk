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

#include "client.h"
#include "qcommon/stringed_ingame.h"
#include <minizip/unzip.h>

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

static const vec4_t dlFill = { 0.035f, 0.07f, 0.12f, 0.94f }, dlEdge = { 0.43f, 0.83f, 1.0f, 0.45f };
static const vec4_t dlText = { 0.86f, 0.92f, 0.97f, 1.0f }, dlDim = { 0.60f, 0.69f, 0.78f, 1.0f };
static const vec4_t dlAccent = { 0.43f, 0.83f, 1.0f, 1.0f }, dlRed = { 1.0f, 0.42f, 0.36f, 1.0f };
static const vec4_t dlGold = { 0.89f, 0.84f, 0.36f, 1.0f };

void CL_DownloadInit( void ) {
	v_downloadReview = Cvar_Get( "v_downloadReview", "1", CVAR_ARCHIVE_ND, "Before a download: choose the paks of the server, and after it what of them is used. 0: all of them, programs in them stay blocked" );
	v_downloadKeep = Cvar_Get( "v_downloadKeep", "1", CVAR_ARCHIVE_ND, "Downloaded paks are kept in <game>/downloads. 0: in downloads/session, for this session only" );
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

static float DL_Width( const char *text, float scale ) {
	return (float)re->Font_StrLenPixels( text, cls.menuFont, scale );
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
	re->Font_DrawString( (int)x, (int)y, text, color, cls.menuFont, -1, scale );
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
	const float	h = DL_PAD + 22.0f + 30.0f + shown * DL_ROW + 12.0f + 2.0f * DL_ROW + 34.0f + 20.0f + DL_PAD;
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
	const float	h = listTop - DL_TOP + DL_ROWS_SHOWN * DL_ROW + 12.0f + 20.0f + DL_PAD;
	int			row = 0, i, k, rows;
	float		y = DL_TOP;

	DL_Rect( x, y, DL_W, h, dlFill, dlEdge );
	y += DL_PAD;
	DL_Print( x + DL_PAD, y, 0.85f, colorWhite, DL_Text( "WHAT_IS_INSIDE", "What is inside" ), inner );
	y += 22.0f;
	DL_Print( x + DL_PAD, y, 0.55f, dlDim, DL_Text( "UNTICK_WHAT_YOU_DO_NOT_WANT_NOTHING",
		"Untick what you do not want: nothing of it is used." ), inner );
	DL_Print( x + DL_PAD, y + 13.0f, 0.55f, dlDim, DL_Text( "PROGRAMS_COULD_RUN_ON_YOUR_COMPUTER",
		"Programs could run on your computer: they are blocked unless you tick them." ), inner );

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
	y = listTop + DL_ROWS_SHOWN * DL_ROW + 12.0f;
	{
		const float gap = 8.0f, w = ( inner - gap ) / 2.0f;

		DL_Button( HIT_USE, x + DL_PAD, y, w, DL_Text( "USE_THEM", "Use them" ), qtrue );
		DL_Button( HIT_DISCONNECT, x + DL_PAD + w + gap, y, w, DL_Text( "DISCONNECT", "Disconnect" ), qtrue );
	}
}

// Once a frame from CL_DrawEngineMenus(). qfalse: no window of this file is open (the engine draws its own).
qboolean CL_DownloadReviewDraw( void ) {
	dl.numHits = 0;
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
