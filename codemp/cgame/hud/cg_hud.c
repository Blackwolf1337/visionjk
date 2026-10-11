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

/*
=================
CG_StatValue

A number of the player who is watched and the most it can be. qfalse: no such name, or it does not apply right
now (the ammo of a saber).

health, armor, force, ammo, jetpack, cloak: what they say. saberstyle: 1 to 3 and up for the special ones.
score: of the player. alive: 1 while he lives. spectator: 1 for somebody who watches. team: 1 red, 2 blue, 0
neither. duel: 1 while he fights a duel.
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
	} else {
		return qfalse;
	}
	return qtrue;
}
