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

// cg_hudpanels.c -- VisionJK: what C adds to the two panels of the new HUD that its menus (ui/vision/hud.menu)
// draw. In the right panel the slot of the force power that the key of the force uses (with what it costs), or
// the weapon where the player knows no force, and the name of the weapon; round both panels the colour of the
// team; above the right panel a strip of chips: whom one watches, the fuel of the jetpack and of the cloak, the
// e-web one stands at. The parts stand at the rects of items of the menu file (force_slot, weapon_label, panel),
// so it can move them. The strip above the left panel is cg_powers.c. Not from JA+.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"
#include "cg_hudkit.h"

#define STRIP_H			17.0f		// a chip of a strip, as the chips of the powers (cg_powers.c)

// the power the key of the force uses: what was picked while the row is up, else what the server has
static int Panels_SelectedPower( void ) {
	if ( cg.forceSelect >= 0 && cg.forceSelect < NUM_FORCE_POWERS && ForcePower_Valid( cg.forceSelect ) ) {
		return cg.forceSelect;
	}
	if ( ForcePower_Valid( cg.snap->ps.fd.forcePowerSelected ) ) {
		return cg.snap->ps.fd.forcePowerSelected;
	}
	return -1;
}

/*
=================
Panels_Slot

The slot at the left of the right panel: the selected force power on dark glass, the rim in the colour of its
side, what it costs under it and darkened while the force is too low, a glow while it runs. Without a force power
the weapon in hand.
=================
*/
static void Panels_Slot( const rectDef_t *r ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	const int			fp = Panels_SelectedPower();
	const vec4_t		inside = { 0.0f, 0.0f, 0.0f, 0.30f };
	const float			*tint = hudAccent;
	qhandle_t			icon = 0;
	int					cost = 0;
	vec4_t				rim;
	float				size;

	if ( fp >= 0 ) {
		const int level = CG_ForceSelectRank( fp );

		tint = CG_ForceSideColor( fp );
		icon = cgs.media.forcePowerIcons[fp];
		cost = level > 0 ? forcePowerNeeded[level][fp] : 0;
		if ( ps->fd.forcePowersActive & ( 1 << fp ) ) {
			const float pulse = 0.5f + 0.5f * sinf( (float)cg.time * 2.0f * M_PI / 1100.0f );

			Hud_Glow( r->x, r->y, r->w, r->h, 5.0f, tint, 0.25f + 0.25f * pulse );
		}
	} else if ( ps->weapon > WP_NONE && ps->weapon < WP_NUM_WEAPONS ) {
		icon = cgs.media.weaponIcons[ps->weapon];
	}
	if ( !icon ) {
		return;
	}
	Hud_Color( rim, tint, 0.9f );
	Skin_Panel( r->x, r->y, r->w, r->h, 3.5f, inside, rim );
	size = r->h * ( cost > 0 ? 0.62f : 0.74f );
	trap->R_SetColor( NULL );
	CG_DrawPic( r->x + ( r->w - size ) * 0.5f, r->y + r->h * ( cost > 0 ? 0.08f : 0.13f ), size, size, icon );
	if ( cost > 0 ) {
		Hud_TextMid( r->x + r->w * 0.5f, r->y + r->h * 0.85f, HUD_CAPS, tint, 1.0f, HUD_CENTER, va( "%i", cost ), 0.0f );
		if ( ps->fd.forcePower < cost ) {
			const vec4_t shade = { 0.02f, 0.03f, 0.06f, 0.6f };

			Skin_Panel( r->x, r->y, r->w, r->h, 3.5f, shade, NULL );
		}
	}
}

// the name of the weapon in hand under its bar, but not for the saber: its bar shows how it is held
static void Panels_WeaponName( const rectDef_t *r ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	const gitem_t		*item;

	if ( ps->weapon <= WP_NONE || ps->weapon >= WP_NUM_WEAPONS || ps->weapon == WP_SABER ) {
		return;
	}
	item = BG_FindItemForWeapon( (weapon_t)ps->weapon );
	if ( item ) {
		char name[128];

		// (in capitals as a caption, cut to the rect: some names of the game are long)
		Q_strncpyz( name, CG_ItemName( item ), sizeof( name ) );
		Q_strupr( name );
		Hud_TextMid( r->x + r->w, r->y + r->h * 0.5f, HUD_CAPS, hudDim, 1.0f, HUD_RIGHT, name, r->w );
	}
}

/*
=================
CG_HudPanelsDraw

After the menus of the HUD were painted (CG_HudDraw), while the player lives: the slot, the name of the weapon,
the colour of the team. Each part with the place of its panel.
=================
*/
void CG_HudPanelsDraw( void ) {
	rectDef_t	left, right, r;
	const float	*team;
	float		alive, most;

	if ( !CG_StatValue( "alive", &alive, &most ) || alive <= 0.0f ) {
		return;
	}
	Hud_PanelRect( qfalse, &left );
	Hud_PanelRect( qtrue, &right );
	team = cgs.gametype >= GT_TEAM ? Hud_TeamColor( cgs.clientinfo[cg.snap->ps.clientNum].team ) : NULL;

	CG_Place2D( PLACE_LEFT );
	if ( team ) {
		Hud_Rim( left.x, left.y, left.w, left.h, 7.0f, team, 0.7f );
	}
	CG_Place2D( PLACE_RIGHT );
	if ( team ) {
		Hud_Rim( right.x, right.y, right.w, right.h, 7.0f, team, 0.7f );
	}
	if ( Hud_ItemRect( "visionhud_right", "force_slot", &r ) ) {
		Panels_Slot( &r );
	}
	if ( Hud_ItemRect( "visionhud_right", "weapon_label", &r ) ) {
		Panels_WeaponName( &r );
	}
	CG_Place2D( PLACE_CENTER );
}

/*
=================
CG_HudRightStrip

The chips above the right panel, from its right edge to the left, each only while it says something: whom one
watches, the e-web, the cloak and the jetpack while they are not full. The caller has set PLACE_RIGHT.
=================
*/
void CG_HudRightStrip( void ) {
	const playerState_t	*ps = &cg.snap->ps;
	rectDef_t			panel;
	float				x, y, alive, most;

	if ( !CG_StatValue( "alive", &alive, &most ) || alive <= 0.0f ) {
		return;
	}
	Hud_PanelRect( qtrue, &panel );
	x = panel.x + panel.w;
	y = panel.y - HUD_GAP - STRIP_H;
	if ( ps->pm_flags & PMF_FOLLOW ) {
		x -= Hud_Chip( x, y, STRIP_H, 0, cgs.clientinfo[ps->clientNum].name, hudWarm, 1.0f, HUD_RIGHT, -1.0f ) + HUD_GAP;
	}
	if ( cg.predictedPlayerState.emplacedIndex > 0 ) {
		const centity_t *eweb = &cg_entities[cg.predictedPlayerState.emplacedIndex];

		if ( eweb->currentState.weapon == WP_NONE && eweb->currentState.maxhealth > 0 ) {
			x -= Hud_Chip( x, y, STRIP_H, 0, "E-Web", NULL, 1.0f, HUD_RIGHT,
				(float)eweb->currentState.health / eweb->currentState.maxhealth ) + HUD_GAP;
		}
	}
	if ( ps->cloakFuel < 100 ) {
		x -= Hud_Chip( x, y, STRIP_H, 0, CG_VisionText( "Cloak" ), NULL, 1.0f, HUD_RIGHT, ps->cloakFuel / 100.0f ) + HUD_GAP;
	}
	if ( ps->jetpackFuel < 100 ) {
		x -= Hud_Chip( x, y, STRIP_H, 0, CG_VisionText( "Jetpack" ), NULL, 1.0f, HUD_RIGHT, ps->jetpackFuel / 100.0f ) + HUD_GAP;
	}
}
