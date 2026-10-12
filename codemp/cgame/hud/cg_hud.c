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

// cg_hud.c -- the numbers of the game by name, for the items of a menu file that show one (ITEM_TYPE_BAR,
// ITEM_TYPE_VALUE, "showStat" of ui/ui_shared.c). They are what the HUD of this tree is made of
// (assets/japlus_client/ui/hud.menu, the menus visionhud_left and visionhud_right), and anybody who writes a
// hud.menu of his own can place them. Not from JA+, see docs/japlus/ui.md.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"

extern displayContextDef_t cgDC;

#define HUD_FILE		"ui/vision/hud.menu"
#define HUD_CLASSIC		"ui/hud.menu"

static qboolean hudLoaded;

/*
=================
CG_HudLoad

The menus of the HUD of the new look, read in addition to the HUD files of the game (cg_hudFiles). Not in an
engine that can not draw the look, and not when the HUD file of the game is not the one of this tree, which has
a mark in its first line: then a mod brought a HUD of its own, and that is the one to draw.
=================
*/
void CG_HudLoad( void ) {
	fileHandle_t	f;
	char			head[128];
	int				len;

	hudLoaded = qfalse;
	if ( !Skin_Ready() ) {
		return;
	}
	len = trap->FS_Open( HUD_CLASSIC, &f, FS_READ );
	if ( !f ) {
		return;
	}
	memset( head, 0, sizeof( head ) );
	if ( len > 0 ) {
		trap->FS_Read( head, Q_min( len, (int)sizeof( head ) - 1 ), f );
	}
	trap->FS_Close( f );
	if ( !strstr( head, "visionjk-classic" ) ) {
		return;
	}
	len = trap->FS_Open( HUD_FILE, &f, FS_READ );
	if ( f ) {
		trap->FS_Close( f );
	}
	if ( len <= 0 ) {
		return;
	}
	CG_ParseMenu( HUD_FILE );
	hudLoaded = qtrue;
}

/*
=================
CG_HudDraw

The HUD of the new look in place of the one of the game: the two menus of ui/vision/hud.menu, the left one at
the left edge of the window and the right one at the right edge. qfalse: it is switched off (cg_hud 0) or not
there, and the caller draws the HUD of the game.
=================
*/
qboolean CG_HudDraw( void ) {
	menuDef_t *left, *right;

	if ( !hudLoaded || !cg_hud.integer ) {
		return qfalse;
	}
	left = Menus_FindByName( "visionhud_left" );
	right = Menus_FindByName( "visionhud_right" );
	if ( !left || !right ) {
		return qfalse;
	}
	CG_Place2D( PLACE_LEFT );
	Menu_Paint( left, qtrue );
	CG_Place2D( PLACE_RIGHT );
	Menu_Paint( right, qtrue );
	CG_Place2D( PLACE_CENTER );
	return qtrue;
}

/*
=================
CG_HudNewLook

Is the HUD of the new look the one that is drawn? What belongs to it follows: the chat box, the scoreboard. Not
with cg_hud 0, not in an engine that can not draw the look, and not next to the HUD of a mod.
=================
*/
qboolean CG_HudNewLook( void ) {
	return (qboolean)( hudLoaded && cg_hud.integer && Skin_Ready() );
}

/*
=================
CG_VisionText

A text of this tree in the language of the game. The cgame asks for it by its English, tools/menus/texts.py has
what it is in the other languages (its list CGAME) and writes it into strings/<language>/vision.str under a key
that it makes from the English, as this does. Without such a string: the English text itself.
=================
*/
const char *CG_VisionText( const char *english ) {
	static char	text[8][256];
	static int	index;
	char		*out = text[index++ & 7];
	char		key[256], found[256], value[8];
	const char	*in;
	qboolean	gap = qfalse;
	int			n = 0;

	// capitals and digits, one _ for whatever is between them, 36 letters at most, no _ at either end
	for ( in = english; *in && n < (int)sizeof( key ) - 2; in++ ) {
		const char c = ( *in >= 'a' && *in <= 'z' ) ? (char)( *in - 'a' + 'A' ) : *in;

		if ( ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) ) {
			if ( gap && n > 0 ) {
				key[n++] = '_';
			}
			gap = qfalse;
			key[n++] = c;
		} else {
			gap = qtrue;
		}
	}
	n = Q_min( n, 36 );
	while ( n > 0 && key[n - 1] == '_' ) {
		n--;
	}
	key[n] = '\0';

	// (for a reference it does not have the engine writes "??" and says no: the pack of this client is older
	// than its cgame, or the pack of a mod took its place)
	found[0] = '\0';
	if ( n <= 0 || !trap->SE_GetStringTextString( va( "VISION_%s", key ), found, sizeof( found ) ) || !found[0] ) {
		Q_strncpyz( out, english, sizeof( text[0] ) );
		return out;
	}
	// (the files of the strings are in the code page of the game)
	trap->Cvar_VariableStringBuffer( "cl_utf8Text", value, sizeof( value ) );
	if ( atoi( value ) == 1 ) {
		Q_UTF8_FromCP1252( found, out, sizeof( text[0] ) );
	} else {
		Q_strncpyz( out, found, sizeof( text[0] ) );
	}
	return out;
}

/*
=================
CG_KeyLabel

A key as a hint names it: TAB, or the letter or sign on it. NULL for a key that has no such name (the cgame can
not ask the engine for the names of keys), and for no key.
=================
*/
const char *CG_KeyLabel( int key ) {
	static char	name[4][2];
	static int	index;
	char		*out = name[index++ & 3];

	if ( key == A_TAB ) {
		return "TAB";
	}
	if ( key > ' ' && key < 127 ) {
		out[0] = ( key >= 'a' && key <= 'z' ) ? (char)( key - 'a' + 'A' ) : (char)key;
		out[1] = '\0';
		return out;
	}
	return NULL;
}

/*
=================
CG_VisionTextWith

A text of this tree with one value in it: the value stands where the text has its first %i or %s, and %% is a
percent sign. The text is never handed to printf as a format: it comes from a file that a pk3 of anybody can
replace.
=================
*/
const char *CG_VisionTextWith( const char *english, const char *value ) {
	static char	text[4][384];
	static int	index;
	char		*out = text[index++ & 3];
	const char	*in = CG_VisionText( english );
	qboolean	placed = qfalse;
	int			n = 0;

	while ( *in && n < (int)sizeof( text[0] ) - 1 ) {
		if ( in[0] == '%' && in[1] == '%' ) {
			out[n++] = '%';
			in += 2;
		} else if ( in[0] == '%' && ( in[1] == 'i' || in[1] == 's' ) ) {
			if ( !placed ) {
				const char *v;

				for ( v = value; *v && n < (int)sizeof( text[0] ) - 1; v++ ) {
					out[n++] = *v;
				}
				placed = qtrue;
			}
			in += 2;
		} else {
			out[n++] = *in++;
		}
	}
	out[n] = '\0';
	return out;
}

/*
=================
CG_TextCut

A text that may be `most` wide at most, as `width` measures it: letters are taken from its end until it fits
with three dots in their place. A text that fits is copied as it is.
=================
*/
void CG_TextCut( const char *text, float most, float (*width)( const char *text, void *arg ), void *arg, char *out, int outSize ) {
	char	value[8];
	int		len;

	Q_strncpyz( out, text, outSize );
	if ( width( out, arg ) <= most || outSize < 8 ) {
		return;
	}
	trap->Cvar_VariableStringBuffer( "cl_utf8Text", value, sizeof( value ) );
	out[outSize - 4] = '\0';
	for ( len = strlen( out ); len > 0; len = strlen( out ) ) {
		// one letter less: a byte, or all bytes of a letter of UTF-8
		if ( atoi( value ) == 1 ) {
			Q_UTF8_Truncate( out, len - 1 );
		} else {
			out[len - 1] = '\0';
		}
		len = strlen( out );
		// (not in the middle of a colour: its two letters go together)
		if ( len > 0 && out[len - 1] == Q_COLOR_ESCAPE ) {
			out[--len] = '\0';
		}
		while ( len > 0 && out[len - 1] == ' ' ) {
			out[--len] = '\0';
		}
		Q_strcat( out, outSize, "..." );
		if ( width( out, arg ) <= most ) {
			return;
		}
		out[len] = '\0';
	}
}

/*
=================
CG_StatValue

A number of the player who is watched and the most it can be. qfalse: no such name, or it does not apply right
now (the ammo of a saber).

health, armor, force, ammo, jetpack, cloak: what they say. saberstyle: 1 to 3 and up for the special ones.
score: of the player. alive: 1 while he lives. spectator: 1 for somebody who watches. team: 1 red, 2 blue, 0
neither. duel: 1 while he fights a duel. altdim: 1 in the alternate dimension of JA+.
=================
*/
qboolean CG_StatValue( const char *name, float *value, float *max ) {
	const playerState_t *ps;

	if ( !cg.snap ) {
		return qfalse;
	}
	ps = &cg.snap->ps;
	*max = 1.0f;
	if ( !Q_stricmp( name, "health" ) ) {
		*value = Q_max( 0, ps->stats[STAT_HEALTH] );
		*max = ps->stats[STAT_MAX_HEALTH];
	} else if ( !Q_stricmp( name, "armor" ) ) {
		*value = ps->stats[STAT_ARMOR];
		*max = ps->stats[STAT_MAX_HEALTH];
	} else if ( !Q_stricmp( name, "force" ) ) {
		*value = ps->fd.forcePower;
		*max = ( ps->fd.forcePowerMax > 0 ) ? ps->fd.forcePowerMax : 100;
	} else if ( !Q_stricmp( name, "ammo" ) ) {
		const int weapon = ps->weapon;

		if ( weapon <= WP_NONE || weapon >= WP_NUM_WEAPONS || weapon == WP_SABER || weapon == WP_MELEE || weapon == WP_STUN_BATON
			|| weaponData[weapon].energyPerShot == 0 ) {
			return qfalse;
		}
		*value = ps->ammo[weaponData[weapon].ammoIndex];
		*max = ammoData[weaponData[weapon].ammoIndex].max;
	} else if ( !Q_stricmp( name, "jetpack" ) ) {
		*value = ps->jetpackFuel;
		*max = 100.0f;
	} else if ( !Q_stricmp( name, "cloak" ) ) {
		*value = ps->cloakFuel;
		*max = 100.0f;
	} else if ( !Q_stricmp( name, "saberstyle" ) ) {
		if ( ps->weapon != WP_SABER ) {
			return qfalse;
		}
		*value = ps->fd.saberDrawAnimLevel;
		*max = 3.0f;
	} else if ( !Q_stricmp( name, "score" ) ) {
		*value = ps->persistant[PERS_SCORE];
	} else if ( !Q_stricmp( name, "alive" ) ) {
		*value = ( ps->stats[STAT_HEALTH] > 0 && ps->pm_type != PM_SPECTATOR ) ? 1.0f : 0.0f;
	} else if ( !Q_stricmp( name, "spectator" ) ) {
		*value = ( ps->pm_type == PM_SPECTATOR || ( ps->pm_flags & PMF_FOLLOW ) ) ? 1.0f : 0.0f;
	} else if ( !Q_stricmp( name, "team" ) ) {
		const int team = cgs.clientinfo[ps->clientNum].team;

		*value = ( team == TEAM_RED ) ? 1.0f : ( team == TEAM_BLUE ) ? 2.0f : 0.0f;
		*max = 2.0f;
	} else if ( !Q_stricmp( name, "duel" ) ) {
		*value = ps->duelInProgress ? 1.0f : 0.0f;
	} else if ( !Q_stricmp( name, "altdim" ) ) {
		// JA+: the alternate dimension, which only free for all has
		*value = ( cgs.gametype == GT_FFA && ( ps->eFlags & EF_ALT_DIM ) ) ? 1.0f : 0.0f;
	} else {
		return qfalse;
	}
	return qtrue;
}

/*
=================
CG_HudLoading

The screen while a level loads, in the new look: over the picture of the level a panel at the bottom with the
box of the icon, the name of the level and of the server, what is being loaded, and a bar for how far it is.
The caller has drawn the picture of the level. qfalse: this look can not be drawn here or is switched off
(ui_classic 1), and the caller draws the screen of the game.
=================
*/
qboolean CG_HudLoading( void ) {
	const vec4_t	veil = { 0.0f, 0.0f, 0.0f, 0.38f };
	const char		*info = CG_ConfigString( CS_SERVERINFO );
	char			value[8], title[256], about[256], now[256], host[128];

	CG_DisplayEnsure();
	trap->Cvar_VariableStringBuffer( "ui_classic", value, sizeof( value ) );
	if ( !Skin_Ready() || atoi( value ) ) {
		return qfalse;
	}
	// (while a level loads the game has no clock of its own yet)
	cgDC.frameTime = 16;
	cgDC.realTime = trap->Milliseconds();

	CG_Place2D( PLACE_CENTER );
	CG_FillRect( -cgs.bias2D, 0.0f, SCREEN_WIDTH + 2.0f * cgs.bias2D, SCREEN_HEIGHT, (float *)veil );

	// the name of the level, as the level says it or as its file is called
	Q_strncpyz( title, CG_ConfigString( CS_MESSAGE ), sizeof( title ) );
	if ( !title[0] ) {
		Q_strncpyz( title, Info_ValueForKey( info, "mapname" ), sizeof( title ) );
	}
	Q_StripColor( title );

	Q_strncpyz( host, Info_ValueForKey( info, "sv_hostname" ), sizeof( host ) );
	Q_StripColor( host );
	Com_sprintf( about, sizeof( about ), "%s  |  %s", BG_GetGametypeString( cgs.gametype ), host );

	if ( cg.infoScreenText[0] ) {
		Com_sprintf( now, sizeof( now ), "Loading %s", cg.infoScreenText );
	} else {
		Q_strncpyz( now, "Waiting for the server", sizeof( now ) );
	}
	// (the game counts nine steps of loading)
	Skin_LoadPanel( title, about, now, cg.loadLCARSStage / 9.0f );
	trap->R_SetColor( NULL );
	return qtrue;
}
