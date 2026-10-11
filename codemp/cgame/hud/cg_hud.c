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
