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

// cg_remaps.c -- VisionJK: the texture swaps of a server (its shader remaps, the configstring CS_SHADERSTATE) as
// something the player decides about. A server swaps one shader for another for everybody: a sign gets another
// picture, a ship is hidden behind a shader that draws nothing. Here every swap the server has is kept in a list,
// and a swap is done only while v_remaps is on and the player has not switched that one off. What is switched off
// is remembered by the name of the shader that is swapped (the file remaps_off.txt in the home folder of the
// game), so it holds on every server and level that swaps the same one.
//
// The window (v_remaps_window, the button in Setup): the switch for all of them, and a row for each swap with what
// is swapped for what and its own switch. A window of the pointer (select/cg_select.c), as the tool window is.
// Client only: the server is told nothing and nobody else sees a difference. Not from JA+.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "cgame/cg_window.h"

#define REMAPS_MAX			128
#define REMAPS_OFF_MAX		256			// names that are switched off, of all servers
#define REMAPS_FILE			"remaps_off.txt"
#define RW_W				470.0f
#define RW_TOP				70.0f
#define RW_PAD				12.0f
#define RW_HEAD				58.0f		// the title, the switch for all
#define RW_ROW				19.0f
#define RW_ROWS				12			// rows at once, the wheel turns the rest in
#define RW_SWITCH_W			38.0f

typedef enum {
	RW_CLOSE,
	RW_ALL,				// the switch for all of them
	RW_ONE				// the switch of a swap, with its number
} rwPart_t;

typedef struct {
	char	from[MAX_QPATH], to[MAX_QPATH], time[16];
} remap_t;

static struct {
	remap_t		list[REMAPS_MAX];
	int			count;
	char		off[REMAPS_OFF_MAX][MAX_QPATH];
	int			numOff;
	qboolean	offRead;
	int			allCount;			// v_remaps as it was when the swaps were last done
	// the window
	qboolean	open;
	float		x, y, h;
	int			first;				// the row at the top
	winHits_t	hits;
} remaps;

/*
===============================================================================

What is switched off

===============================================================================
*/

static int Remaps_OffIndex( const char *from ) {
	int i;

	for ( i = 0; i < remaps.numOff; i++ ) {
		if ( !Q_stricmp( remaps.off[i], from ) ) {
			return i;
		}
	}
	return -1;
}

// the file: a name of a shader on each line
static void Remaps_ReadOff( void ) {
	static char		buf[REMAPS_OFF_MAX * ( MAX_QPATH + 1 )];
	fileHandle_t	f = 0;
	const char		*p;
	int				len;

	if ( remaps.offRead ) {
		return;
	}
	remaps.offRead = qtrue;
	remaps.numOff = 0;
	len = trap->FS_Open( REMAPS_FILE, &f, FS_READ );
	if ( !f ) {
		return;
	}
	if ( len > 0 && len < (int)sizeof( buf ) ) {
		trap->FS_Read( buf, len, f );
		buf[len] = '\0';
		p = buf;
		while ( remaps.numOff < REMAPS_OFF_MAX ) {
			const char *token = COM_ParseExt( &p, qtrue );

			if ( !token[0] ) {
				break;
			}
			if ( strlen( token ) < MAX_QPATH && Remaps_OffIndex( token ) < 0 ) {
				Q_strncpyz( remaps.off[remaps.numOff++], token, sizeof( remaps.off[0] ) );
			}
		}
	}
	trap->FS_Close( f );
}

static void Remaps_WriteOff( void ) {
	fileHandle_t	f = 0;
	int				i;

	trap->FS_Open( REMAPS_FILE, &f, FS_WRITE );
	if ( !f ) {
		Com_PrintWarning( "%s can not be written, what is switched off holds until the game ends\n", REMAPS_FILE );
		return;
	}
	for ( i = 0; i < remaps.numOff; i++ ) {
		trap->FS_Write( remaps.off[i], strlen( remaps.off[i] ), f );
		trap->FS_Write( "\n", 1, f );
	}
	trap->FS_Close( f );
}

/*
===============================================================================

The swaps

===============================================================================
*/

// one swap as it is to be now: done, or the shader as it is
static void Remaps_Apply( const remap_t *r ) {
	if ( v_remaps.integer && Remaps_OffIndex( r->from ) < 0 ) {
		trap->R_RemapShader( r->from, r->to, r->time );
	} else {
		trap->R_RemapShader( r->from, r->from, "0" );
	}
}

static void Remaps_ApplyAll( void ) {
	int i;

	for ( i = 0; i < remaps.count; i++ ) {
		Remaps_Apply( &remaps.list[i] );
	}
	remaps.allCount = v_remaps.modificationCount;
}

/*
=================
CG_RemapsChanged

The configstring of the swaps came or changed (CG_ShaderStateChanged): "from=to:time@" one after the other. The
list is what it says now; a swap that left it gets its shader back.
=================
*/
void CG_RemapsChanged( void ) {
	static remap_t	before[REMAPS_MAX];
	const char		*o = CG_ConfigString( CS_SHADERSTATE );
	const int		numBefore = remaps.count;
	int				i, j;

	Remaps_ReadOff();
	memcpy( before, remaps.list, sizeof( before[0] ) * numBefore );
	remaps.count = 0;
	while ( o && *o && remaps.count < REMAPS_MAX ) {
		remap_t		*r = &remaps.list[remaps.count];
		const char	*n = strstr( o, "=" ), *t, *end;

		// (the lengths are distances in a configstring of the server: what does not fit ends the list)
		if ( !n || n - o >= (int)sizeof( r->from ) ) {
			break;
		}
		Q_strncpyz( r->from, o, (int)( n - o ) + 1 );
		n++;
		t = strstr( n, ":" );
		if ( !t || t - n >= (int)sizeof( r->to ) ) {
			break;
		}
		Q_strncpyz( r->to, n, (int)( t - n ) + 1 );
		t++;
		end = strstr( t, "@" );
		if ( !end || end - t >= (int)sizeof( r->time ) ) {
			break;
		}
		Q_strncpyz( r->time, t, (int)( end - t ) + 1 );
		o = end + 1;
		// (the same shader swapped twice: the later one counts, as it did when each was done as it came)
		for ( j = 0; j < remaps.count && Q_stricmp( remaps.list[j].from, r->from ); j++ ) {
		}
		if ( j < remaps.count ) {
			remaps.list[j] = *r;
		} else {
			remaps.count++;
		}
	}
	for ( i = 0; i < numBefore; i++ ) {
		for ( j = 0; j < remaps.count && Q_stricmp( remaps.list[j].from, before[i].from ); j++ ) {
		}
		if ( j == remaps.count ) {
			trap->R_RemapShader( before[i].from, before[i].from, "0" );
		}
	}
	Remaps_ApplyAll();
	remaps.first = Q_max( 0, Q_min( remaps.first, remaps.count - RW_ROWS ) );
}

// every frame: v_remaps was changed
void CG_RemapsFrame( void ) {
	if ( remaps.allCount != v_remaps.modificationCount ) {
		Remaps_ApplyAll();
	}
}

// a new level: nothing of the last one
void CG_RemapsInit( void ) {
	remaps.count = 0;
	remaps.first = 0;
	remaps.open = qfalse;
	remaps.offRead = qfalse;
	remaps.allCount = v_remaps.modificationCount;
}

/*
===============================================================================

The window

===============================================================================
*/

void CG_RemapsWindowClose( void ) {
	remaps.open = qfalse;
}

// v_remaps_window: open and closed
void CG_RemapsWindow_f( void ) {
	remaps.open = (qboolean)!remaps.open;
	remaps.hits.num = 0;
}

qboolean CG_RemapsWindowShown( void ) {
	return remaps.open;
}

qboolean CG_RemapsWindowPointerOver( void ) {
	return ( remaps.open && Win_Over( remaps.x, remaps.y, RW_W, remaps.h ) ) ? qtrue : qfalse;
}

void CG_RemapsWindowDraw( void ) {
	const int	rows = Q_min( RW_ROWS, remaps.count );
	const float	textW = ( RW_W - 2.0f * RW_PAD - RW_SWITCH_W - 26.0f ) * 0.5f;
	float		x, y;
	int			i;

	if ( !remaps.open ) {
		return;
	}
	remaps.hits.num = 0;
	remaps.x = x = ( SCREEN_WIDTH - RW_W ) * 0.5f;
	remaps.y = y = RW_TOP;
	remaps.h = RW_HEAD + Q_max( 1, rows ) * RW_ROW + RW_PAD + ( remaps.count > RW_ROWS ? 12.0f : 0.0f );

	Win_Glass( x, y, RW_W, remaps.h, 6.0f, winFill, winEdge );
	Win_Hit( &remaps.hits, -1, 0, x, y, RW_W, remaps.h );
	Win_Text( x + RW_PAD, y + 8.0f, 0.7f, winAccent, 0, CG_VisionText( "Texture Swaps" ), 0.0f );
	Win_Button( &remaps.hits, RW_CLOSE, 0, x + RW_W - RW_PAD - 18.0f, y + 8.0f, 18.0f, "x", winText, qtrue );
	Win_Line( x + RW_PAD, y + 32.0f, RW_W - 2.0f * RW_PAD );

	// the switch for all of them
	Win_Text( x + RW_PAD, y + 38.0f, 0.5f, winText, 0, CG_VisionText( "Swaps of this server" ), RW_W - 2.0f * RW_PAD - RW_SWITCH_W - 8.0f );
	Win_Button( &remaps.hits, RW_ALL, 0, x + RW_W - RW_PAD - RW_SWITCH_W, y + 36.0f, RW_SWITCH_W, CG_VisionText( v_remaps.integer ? "On" : "Off" ),
		v_remaps.integer ? winAccent : winDim, qtrue );

	y += RW_HEAD;
	if ( !remaps.count ) {
		Win_Text( x + RW_PAD, y + 2.0f, 0.5f, winDim, 0, CG_VisionText( "This server swaps no textures." ), RW_W - 2.0f * RW_PAD );
		return;
	}
	for ( i = 0; i < rows; i++ ) {
		const int		n = remaps.first + i;
		const remap_t	*r = &remaps.list[n];
		const qboolean	on = (qboolean)( Remaps_OffIndex( r->from ) < 0 );
		const float		*color = ( on && v_remaps.integer ) ? winText : winDim;
		const float		top = y + i * RW_ROW;

		Win_Text( x + RW_PAD, top + 2.0f, 0.46f, color, 0, r->from, textW );
		Win_Text( x + RW_PAD + textW + 4.0f, top + 2.0f, 0.46f, winDim, 0, ">", 0.0f );
		Win_Text( x + RW_PAD + textW + 18.0f, top + 2.0f, 0.46f, color, 0, r->to, textW );
		Win_Button( &remaps.hits, RW_ONE, n, x + RW_W - RW_PAD - RW_SWITCH_W, top, RW_SWITCH_W, CG_VisionText( on ? "On" : "Off" ),
			on ? winAccent : winDim, (qboolean)( v_remaps.integer != 0 ) );
	}
	if ( remaps.count > RW_ROWS ) {
		Win_Text( x + RW_W * 0.5f, y + rows * RW_ROW + 2.0f, 0.42f, winDim, 2,
			va( "%i - %i / %i", remaps.first + 1, remaps.first + rows, remaps.count ), 0.0f );
	}
}

/*
=================
CG_RemapsWindowKey

A key while it is open: qtrue, it took it
=================
*/
qboolean CG_RemapsWindowKey( int key, qboolean down ) {
	int at;

	if ( !remaps.open ) {
		return qfalse;
	}
	if ( key == A_ESCAPE || key == A_MOUSE2 ) {
		if ( down ) {
			CG_RemapsWindowClose();
		}
		return qtrue;
	}
	if ( key == A_MWHEELUP || key == A_MWHEELDOWN ) {
		if ( down ) {
			remaps.first = Q_max( 0, Q_min( remaps.first + ( key == A_MWHEELDOWN ? 3 : -3 ), remaps.count - RW_ROWS ) );
		}
		return qtrue;
	}
	if ( key != A_MOUSE1 ) {
		return qfalse;
	}
	if ( !down || ( at = Win_HitAt( &remaps.hits ) ) < 0 ) {
		return (qboolean)!down;
	}
	switch ( remaps.hits.hit[at].part ) {
	case RW_CLOSE:
		CG_RemapsWindowClose();
		break;
	case RW_ALL:
		trap->Cvar_Set( "v_remaps", v_remaps.integer ? "0" : "1" );
		break;
	case RW_ONE: {
		const int	n = remaps.hits.hit[at].arg;
		int			off;

		if ( n < 0 || n >= remaps.count || !v_remaps.integer ) {
			break;
		}
		off = Remaps_OffIndex( remaps.list[n].from );
		if ( off >= 0 ) {
			remaps.off[off][0] = '\0';
			memmove( remaps.off[off], remaps.off[off + 1], sizeof( remaps.off[0] ) * ( remaps.numOff - off - 1 ) );
			remaps.numOff--;
		} else if ( remaps.numOff < REMAPS_OFF_MAX ) {
			Q_strncpyz( remaps.off[remaps.numOff++], remaps.list[n].from, sizeof( remaps.off[0] ) );
		}
		Remaps_WriteOff();
		Remaps_Apply( &remaps.list[n] );
		break;
	}
	}
	return qtrue;
}
