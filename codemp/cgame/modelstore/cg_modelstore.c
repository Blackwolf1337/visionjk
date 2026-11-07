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

// cg_modelstore.c -- the window of the model store of the site in the game (issue #6): the models anyone uploaded
// to jka.vision/models, with their icons, a page at a time, and a button that fetches one. v_modelbrowser opens and
// closes it (Controls, Extras). While it is open the mouse is free (select/cg_select.c draws it and hands it the
// keys). The engine does the fetching (client/cl_models.cpp): it keeps the list of the store in
// modelstore/index.txt and the icons in modelstore/icons/ of base, and counts v_modelStoreChanged up when one of
// them, or a model, came. Not from JA+, see docs/japlus/models.md.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "cgame/cg_window.h"
#include "web/web_models.h"

#define MS_W			520.0f
#define MS_H			330.0f
#define MS_TOP			70.0f
#define MS_PAD			12.0f
#define MS_COLS			5
#define MS_ROWS			2
#define MS_TILE_W		96.0f
#define MS_TILE_H		112.0f
#define MS_ICON			64.0f
#define MS_MOST			256				// models of the list the window shows
#define MS_INDEX_MOST	( 256 * 1024 )	// bytes of the list (MDL_INDEX_MOST of cl_models.cpp)

typedef enum {
	MS_CLOSE,
	MS_GET,				// with the model
	MS_PAGE				// with -1 or 1
} msPart_t;

typedef struct {
	char		name[WEB_MODEL_NAME];
	long long	bytes;
	qhandle_t	icon;
	qboolean	here;			// the game has it: of the game, of a pak, or it came
} msModel_t;

static struct {
	qboolean	open;
	float		x, y;
	winHits_t	hits;
	int			changed;		// v_modelStoreChanged when the list was read, -1: not read
	int			page;
	msModel_t	models[MS_MOST];
	int			numModels;
} ms;

static char msIndex[MS_INDEX_MOST + 1];

// The list the engine keeps, with the icons and whether the game has each model, read anew.
static void MS_Read( void ) {
	fileHandle_t	f;
	const char		*p;
	char			name[WEB_MODEL_NAME], icon[4];
	long long		bytes;
	int				len = trap->FS_Open( "modelstore/index.txt", &f, FS_READ );

	ms.numModels = 0;
	msIndex[0] = '\0';
	if ( !f ) {
		return;
	}
	if ( len > 0 ) {
		len = trap->FS_Read( msIndex, Q_min( len, MS_INDEX_MOST ), f );
		msIndex[Q_max( len, 0 )] = '\0';
	}
	trap->FS_Close( f );
	for ( p = WEB_ModelIndexNext( msIndex, name, icon, &bytes ); p && ms.numModels < MS_MOST; p = WEB_ModelIndexNext( p, name, icon, &bytes ) ) {
		msModel_t		*m = &ms.models[ms.numModels++];
		fileHandle_t	glm;

		Q_strncpyz( m->name, name, sizeof( m->name ) );
		m->bytes = bytes;
		m->icon = icon[0] ? trap->R_RegisterShaderNoMip( va( "modelstore/icons/%s", name ) ) : 0;
		m->here = (qboolean)( trap->FS_Open( va( "models/players/%s/model.glm", name ), &glm, FS_READ ) > 0 );
		if ( glm ) {
			trap->FS_Close( glm );
		}
	}
	ms.page = Q_min( ms.page, Q_max( 0, ( ms.numModels - 1 ) / ( MS_COLS * MS_ROWS ) ) );
}

void CG_ModelStoreOpen( void ) {
	memset( &ms, 0, sizeof( ms ) );
	ms.open = qtrue;
	ms.changed = -1;
	// the engine fetches the list when it is old, and the icons that are not here
	trap->SendConsoleCommand( "v_models icons\n" );
}

void CG_ModelStoreClose( void ) {
	ms.open = qfalse;
}

// v_modelbrowser: open and closed (v_modelstore would be the cvar v_modelStore, names have no case)
void CG_ModelStore_f( void ) {
	if ( ms.open )
		CG_ModelStoreClose();
	else
		CG_ModelStoreOpen();
}

qboolean CG_ModelStoreShown( void ) {
	return ms.open;
}

qboolean CG_ModelStorePointerOver( void ) {
	return ( ms.open && Win_Over( ms.x, ms.y, MS_W, MS_H ) ) ? qtrue : qfalse;
}

static const char *MS_Size( long long bytes ) {
	return ( bytes >= 1024 * 1024 ) ? va( "%.1f MB", bytes / ( 1024.0f * 1024.0f ) ) : va( "%i KB", (int)( bytes / 1024 ) );
}

/*
=================
CG_ModelStoreDraw

Once a frame while it is open, in the 2D of the HUD, before the pointer
=================
*/
void CG_ModelStoreDraw( void ) {
	const int	perPage = MS_COLS * MS_ROWS;
	char		fetching[WEB_MODEL_NAME], value[16];
	int			changed;
	float		x, y;
	int			i, pages;

	if ( !ms.open )
		return;
	trap->Cvar_VariableStringBuffer( "v_modelStoreChanged", value, sizeof( value ) );
	changed = atoi( value );
	if ( changed != ms.changed ) {
		ms.changed = changed;
		MS_Read();
	}
	trap->Cvar_VariableStringBuffer( "v_modelStoreFetching", fetching, sizeof( fetching ) );
	pages = Q_max( 1, ( ms.numModels + perPage - 1 ) / perPage );
	ms.hits.num = 0;
	ms.x = x = ( SCREEN_WIDTH - MS_W ) * 0.5f;
	ms.y = y = MS_TOP;

	Win_Panel( x, y, MS_W, MS_H, 6.0f, winFill, winEdge );
	Win_Hit( &ms.hits, -1, 0, x, y, MS_W, MS_H );
	Win_Text( x + MS_PAD, y + 8.0f, 0.7f, winAccent, 0, CG_VisionText( "Model Store" ), 0.0f );
	if ( ms.numModels ) {
		Win_Text( x + MS_W - MS_PAD - 30.0f, y + 12.0f, 0.5f, winDim, 1, CG_VisionTextWith( "%s models", va( "%i", ms.numModels ) ), 0.0f );
	}
	Win_Button( &ms.hits, MS_CLOSE, 0, x + MS_W - MS_PAD - 18.0f, y + 8.0f, 18.0f, "x", winText, qtrue );
	Win_Line( x + MS_PAD, y + 32.0f, MS_W - 2.0f * MS_PAD );

	if ( !ms.numModels ) {
		Win_Text( x + MS_W * 0.5f, y + MS_H * 0.45f, 0.55f, winDim, 2,
			CG_VisionText( msIndex[0] ? "The store has no models yet." : "The list of the store is on its way." ), MS_W - 2.0f * MS_PAD );
	}
	for ( i = ms.page * perPage; i < ms.numModels && i < ( ms.page + 1 ) * perPage; i++ ) {
		const msModel_t	*m = &ms.models[i];
		const int		slot = i - ms.page * perPage;
		const float		tx = x + MS_PAD + ( slot % MS_COLS ) * ( MS_TILE_W + 4.0f ), ty = y + 42.0f + ( slot / MS_COLS ) * ( MS_TILE_H + 6.0f );
		const float		ix = tx + ( MS_TILE_W - MS_ICON ) * 0.5f;
		const qboolean	coming = (qboolean)!Q_stricmp( fetching, m->name );

		Win_Panel( tx, ty, MS_TILE_W, MS_TILE_H, 4.0f, NULL, winEdge );
		if ( m->icon ) {
			CG_DrawPic( ix, ty + 6.0f, MS_ICON, MS_ICON, m->icon );
		} else {
			Win_Panel( ix, ty + 6.0f, MS_ICON, MS_ICON, 3.0f, winFill, NULL );
		}
		Win_Text( tx + MS_TILE_W * 0.5f, ty + 72.0f, 0.42f, winText, 2, m->name, MS_TILE_W - 6.0f );
		if ( m->here ) {
			Win_Text( tx + MS_TILE_W * 0.5f, ty + 89.0f, 0.42f, winDim, 2, CG_VisionText( "In the game" ), MS_TILE_W - 6.0f );
		} else if ( coming ) {
			Win_Text( tx + MS_TILE_W * 0.5f, ty + 89.0f, 0.42f, winGold, 2, CG_VisionText( "Coming..." ), MS_TILE_W - 6.0f );
		} else {
			Win_Button( &ms.hits, MS_GET, i, tx + 8.0f, ty + 88.0f, MS_TILE_W - 16.0f,
				CG_VisionTextWith( "Get (%s)", MS_Size( m->bytes ) ), winText, qtrue );
		}
	}

	y = MS_TOP + MS_H - 30.0f;
	if ( pages > 1 ) {
		Win_Button( &ms.hits, MS_PAGE, -1, x + MS_PAD, y, 24.0f, "<", winText, (qboolean)( ms.page > 0 ) );
		Win_Text( x + MS_PAD + 30.0f, y + 2.0f, 0.46f, winDim, 0, va( "%i / %i", ms.page + 1, pages ), 0.0f );
		Win_Button( &ms.hits, MS_PAGE, 1, x + MS_PAD + 76.0f, y, 24.0f, ">", winText, (qboolean)( ms.page < pages - 1 ) );
	}
	Win_Text( x + MS_W - MS_PAD, y + 2.0f, 0.46f, winDim, 1, CG_VisionText( "Upload your own at jka.vision/models.html" ), MS_W * 0.6f );
}

static void MS_Page( int step ) {
	const int pages = Q_max( 1, ( ms.numModels + MS_COLS * MS_ROWS - 1 ) / ( MS_COLS * MS_ROWS ) );

	ms.page = Com_Clampi( 0, pages - 1, ms.page + step );
}

/*
=================
CG_ModelStoreKey

A key while it is open: qtrue, it took it
=================
*/
qboolean CG_ModelStoreKey( int key, qboolean down ) {
	int at;

	if ( !ms.open )
		return qfalse;
	if ( key == A_ESCAPE || key == A_MOUSE2 ) {
		if ( down )
			CG_ModelStoreClose();
		return qtrue;
	}
	if ( key == A_MWHEELUP || key == A_MWHEELDOWN ) {
		if ( down && CG_ModelStorePointerOver() )
			MS_Page( key == A_MWHEELUP ? -1 : 1 );
		return CG_ModelStorePointerOver();
	}
	if ( key != A_MOUSE1 )
		return qfalse;
	if ( !down )
		return qtrue;
	if ( ( at = Win_HitAt( &ms.hits ) ) < 0 )
		return qfalse;
	switch ( ms.hits.hit[at].part ) {
	case MS_CLOSE:
		CG_ModelStoreClose();
		break;
	case MS_GET:
		trap->SendConsoleCommand( va( "v_models get %s\n", ms.models[ms.hits.hit[at].arg].name ) );
		break;
	case MS_PAGE:
		MS_Page( ms.hits.hit[at].arg );
		break;
	}
	return qtrue;
}
