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
// draw. In the block at the left the slot of the force power that the key of the force uses, or
// the weapon where the player knows no force, and the name of the weapon; round both panels the colour of the
// team; above the right panel a strip of chips: the fuel of the jetpack and of the cloak, the
// e-web one stands at. The parts stand at the rects of items of the menu file (force_slot, weapon_label, panel),
// so it can move them. The strip above the left panel is cg_powers.c. Not from JA+.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"
#include "cg_hudkit.h"

#define STRIP_H			17.0f		// a chip of a strip, as the chips of the powers (cg_powers.c)

// the power the key of the force uses: what was picked while the row is up, else what the server has
static int Panels_SelectedPower( void ) {
	// (watching somebody: his, not what one picked oneself before)
	if ( !( cg.snap->ps.pm_flags & PMF_FOLLOW ) && cg.forceSelect >= 0 && cg.forceSelect < NUM_FORCE_POWERS && ForcePower_Valid( cg.forceSelect ) ) {
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

The slot at the left of the block: the selected force power on dark glass in the middle of it, the rim in
the colour of its side, darkened while the force is too low for it, a glow while it runs. Without a force power
the weapon in hand.
=================
*/
static void Panels_Slot( const rectDef_t *r ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	const int			fp = Panels_SelectedPower();
	const vec4_t		inside = { 0.0f, 0.0f, 0.0f, 0.30f };
	const float			*tint = hudAccent;
	const gitem_t		*item = NULL;
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
		// (melee has the model of the stun baton in the item list: its picture)
		item = ps->weapon != WP_MELEE ? BG_FindItemForWeapon( (weapon_t)ps->weapon ) : NULL;
	}
	if ( !icon ) {
		return;
	}
	Hud_Color( rim, tint, 0.9f );
	Skin_Panel( r->x, r->y, r->w, r->h, 3.5f, inside, rim );
	// in 3D as the cards of the selection rows have it (hud/cg_select3d.c): a power as the turning holo disc, a
	// weapon as its model; the picture where there is no model
	size = r->h * 0.86f;
	trap->R_SetColor( NULL );
	if ( !CG_Select3DIcon( r->x + ( r->w - size ) * 0.5f, r->y + ( r->h - size ) * 0.5f, size, size, item, fp >= 0 ? icon : 0, tint, qfalse, 1.0f ) ) {
		size = r->h * 0.74f;
		CG_DrawPic( r->x + ( r->w - size ) * 0.5f, r->y + ( r->h - size ) * 0.5f, size, size, icon );
	}
	// (what the power costs is not written: the slot is darkened while the force is too low for it)
	if ( cost > 0 && ps->fd.forcePower < cost ) {
		const vec4_t shade = { 0.02f, 0.03f, 0.06f, 0.6f };

		Skin_Panel( r->x, r->y, r->w, r->h, 3.5f, shade, NULL );
	}
}

// the name of the weapon in hand under its bar, but not for the saber: its bar shows how it is held
static void Panels_WeaponName( const rectDef_t *r ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	const gitem_t		*item;
	char				name[160];

	if ( ps->weapon <= WP_NONE || ps->weapon >= WP_NUM_WEAPONS || !( item = BG_FindItemForWeapon( (weapon_t)ps->weapon ) ) ) {
		return;
	}
	// (in capitals as a caption, cut to the rect: some names of the game are long)
	Q_strncpyz( name, CG_ItemName( item ), sizeof( name ) );
	if ( ps->weapon == WP_SABER ) {
		// and how it is held, which the steps over it show
		static const char *stances[] = { "", "Fast", "Medium", "Strong", "Desann", "Tavion", "Dual", "Staff" };
		const int level = ps->fd.saberDrawAnimLevel;

		if ( level >= 1 && level < (int)ARRAY_LEN( stances ) ) {
			Q_strcat( name, sizeof( name ), va( "  |  %s", CG_VisionText( stances[level] ) ) );
		}
	}
	Q_strupr( name );
	Hud_TextMid( r->x, r->y + r->h * 0.5f, HUD_CAPS, hudDim, 1.0f, HUD_LEFT, name, r->w );
}

// The stance of the saber as three steps from the left, so many lit as it is heavy (fast, medium, strong), in the
// colour of the stance; the stances beyond those (two blades, a staff, the ones of Desann and Tavion) light all.
static void Panels_SaberSteps( const rectDef_t *r ) {
	static const vec4_t	fast = { 0.43f, 0.83f, 1.0f, 1.0f }, medium = { 1.0f, 0.92f, 0.45f, 1.0f }, strong = { 1.0f, 0.38f, 0.33f, 1.0f };
	static const vec4_t	off = { 1.0f, 1.0f, 1.0f, 0.10f };
	const playerState_t	*ps = &cg.predictedPlayerState;
	const int			level = ps->fd.saberDrawAnimLevel, lit = Q_min( 3, level );
	const float			*tint = level <= 1 ? fast : level == 2 ? medium : level == 3 ? strong : hudAccent;
	const float			gap = 2.0f, w = ( r->w - 2.0f * gap ) / 3.0f;
	int					i;

	if ( ps->weapon != WP_SABER || level < 1 ) {
		return;
	}
	for ( i = 0; i < 3; i++ ) {
		vec4_t c;

		Hud_Color( c, i < lit ? tint : off, i < lit ? 1.0f : off[3] );
		Skin_Panel( r->x + i * ( w + gap ), r->y, w, r->h, r->h * 0.5f, c, NULL );
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
	team = cgs.gametype >= GT_TEAM ? Hud_TeamColor( cgs.clientinfo[cg.snap->ps.clientNum].team ) : NULL;

	CG_Place2D( PLACE_LEFT );
	if ( team ) {
		Hud_Rim( left.x, left.y, left.w, left.h, 7.0f, team, 0.7f );
	}
	if ( Hud_ItemRect( "visionhud_left", "force_slot", &r ) ) {
		Panels_Slot( &r );
	}
	if ( Hud_ItemRect( "visionhud_left", "saber_steps", &r ) ) {
		Panels_SaberSteps( &r );
	}
	if ( Hud_ItemRect( "visionhud_left", "weapon_label", &r ) ) {
		Panels_WeaponName( &r );
	}
	// (a panel at the right only where the HUD file has one)
	CG_Place2D( PLACE_RIGHT );
	if ( team && Hud_PanelRect( qtrue, &right ) ) {
		Hud_Rim( right.x, right.y, right.w, right.h, 7.0f, team, 0.7f );
	}
	CG_Place2D( PLACE_CENTER );
}

/*
===============================================================================

The corner at the bottom right

The radar in the corner (where the game shows one: team games and vehicles) with its legend, the items the player
carries left of it, each with its key; when a selection row of weapons, items or powers comes up there
(hud/cg_select3d.c) both slide up to make room for it, and come down again when it is gone.

===============================================================================
*/

#define CORNER_LIFT			82.0f		// how far the corner goes up for a selection row
#define CORNER_LIFT_MSEC	70.0f		// how fast (time constant)
#define CORNER_SLOT			20.0f		// a slot of an item
#define CORNER_KEY_H		10.0f		// the row of keys under the slots
#define CORNER_RADAR		120.0f		// the radar of the game (cg_draw.c RADAR_RADIUS * 2)
#define CORNER_LEGEND_H		13.0f

static float	cornerLift;

// the key the player uses an item with
static const char *Corner_ItemCommand( int tag ) {
	switch ( tag ) {
	case HI_SEEKER:			return "use_seeker";
	case HI_SHIELD:			return "use_field";
	case HI_MEDPAC:			return "use_bacta";
	case HI_MEDPAC_BIG:		return "use_bactabig";
	case HI_BINOCULARS:		return "use_electrobinoculars";
	case HI_SENTRY_GUN:		return "use_sentry";
	case HI_JETPACK:		return "use_jetpack";
	case HI_HEALTHDISP:		return "use_healthdisp";
	case HI_AMMODISP:		return "use_ammodisp";
	case HI_EWEB:			return "use_eweb";
	case HI_CLOAK:			return "use_cloak";
	default:				return NULL;
	}
}

// does the game show the radar now (CG_DrawRadar draws nothing otherwise)
static qboolean Corner_Radar( void ) {
	const playerState_t *ps = &cg.predictedPlayerState;

	return (qboolean)( cg_drawRadar.integer && ( cgs.gametype >= GT_TEAM || ps->m_iVehicleNum ) && cg.snap && cg.snap->ps.stats[STAT_HEALTH] > 0
		&& !( ps->pm_flags & PMF_FOLLOW ) && ps->persistant[PERS_TEAM] != TEAM_SPECTATOR );
}

// the items the player carries, in the order of the game; returns how many
static int Corner_Items( int *tags ) {
	int n = 0, i;

	if ( !cg.snap || cg.snap->ps.stats[STAT_HEALTH] <= 0 || ( cg.snap->ps.pm_flags & PMF_FOLLOW ) ) {
		return 0;
	}
	for ( i = 0; i < HI_NUM_HOLDABLE; i++ ) {
		// (not the dispensers of health and ammo: they are not used by a key, and the game has no picture of them)
		if ( ( cg.snap->ps.stats[STAT_HOLDABLE_ITEMS] & ( 1 << i ) ) && BG_GetItemIndexByTag( i, IT_HOLDABLE ) > 0
			&& i != HI_HEALTHDISP && i != HI_AMMODISP ) {
			tags[n++] = i;
		}
	}
	return n;
}

// the top of what stands in the corner now (the chips of jetpack, cloak and e-web go above it), its bottom where
// there is nothing
static float Corner_Top( void ) {
	int			tags[HI_NUM_HOLDABLE];
	const float	bottom = SCREEN_HEIGHT - HUD_EDGE - cornerLift;
	float		top = SCREEN_HEIGHT - HUD_EDGE;

	if ( Corner_Radar() ) {
		top = bottom - CORNER_LEGEND_H - CORNER_RADAR;
	} else if ( Corner_Items( tags ) ) {
		top = bottom - CORNER_SLOT - CORNER_KEY_H - 8.0f;
	}
	return top;
}

/*
=================
CG_HudCorner

From the upper right of the new HUD (cg_draw.c), after the selection rows were drawn this frame.
=================
*/
void CG_HudCorner( void ) {
	const float	target = CG_Select3DShown() > 0.0f ? CORNER_LIFT : 0.0f;
	int			tags[HI_NUM_HOLDABLE], n, i;
	float		bottom, right = SCREEN_WIDTH - HUD_EDGE;

	cornerLift += ( target - cornerLift ) * ( 1.0f - expf( -(float)cg.frametime / CORNER_LIFT_MSEC ) );
	if ( fabsf( target - cornerLift ) < 0.2f ) {
		cornerLift = target;
	}
	bottom = SCREEN_HEIGHT - HUD_EDGE - cornerLift;
	CG_Place2D( PLACE_RIGHT );
	if ( Corner_Radar() ) {
		CG_DrawRadarAt( right - CORNER_RADAR, bottom - CORNER_LEGEND_H - CORNER_RADAR );
		CG_HudRadarLegend( bottom - CORNER_LEGEND_H );
		right -= CORNER_RADAR + 6.0f;
	}
	// the items: a slot each, the one the item key uses lit, its key under it
	n = Corner_Items( tags );
	if ( n ) {
		const int	selected = cg.snap->ps.stats[STAT_HOLDABLE_ITEM] > 0 ? bg_itemlist[cg.snap->ps.stats[STAT_HOLDABLE_ITEM]].giTag : -1;
		const float	gap = 3.0f, w = n * CORNER_SLOT + ( n - 1 ) * gap + 8.0f, h = CORNER_SLOT + CORNER_KEY_H + 8.0f;
		const float	x = right - w, y = bottom - h;

		Hud_Glass( x, y, w, h, 6.0f, 1.0f, NULL, qfalse );
		for ( i = 0; i < n; i++ ) {
			const int		index = BG_GetItemIndexByTag( tags[i], IT_HOLDABLE );
			const float		sx = x + 4.0f + i * ( CORNER_SLOT + gap ), sy = y + 4.0f;
			const char		*cmd = Corner_ItemCommand( tags[i] );
			const char		*key = cmd ? Hud_KeyFor( cmd, NULL ) : NULL;
			const vec4_t	inside = { 0.0f, 0.0f, 0.0f, 0.3f }, lit = { 0.43f, 0.83f, 1.0f, 0.14f };
			vec4_t			rim;

			Hud_Color( rim, hudAccent, tags[i] == selected ? 0.95f : 0.25f );
			Skin_Panel( sx, sy, CORNER_SLOT, CORNER_SLOT, 3.0f, tags[i] == selected ? lit : inside, rim );
			// (the picture, not the model: the selection row shows the same models while one cycles, and the same
			// model in two scenes of a frame swapped between the slots, it flickered)
			trap->R_SetColor( NULL );
			CG_RegisterItemVisuals( index );
			if ( cg_items[index].icon ) {
				CG_DrawPic( sx + 2.0f, sy + 2.0f, CORNER_SLOT - 4.0f, CORNER_SLOT - 4.0f, cg_items[index].icon );
			}
			if ( key ) {
				Hud_Key( sx + CORNER_SLOT * 0.5f, sy + CORNER_SLOT + CORNER_KEY_H * 0.5f + 1.0f, key, 1.0f, HUD_CENTER );
			}
		}
	}
	CG_Place2D( PLACE_CENTER );
}

/*
=================
CG_HudRightStrip

The chips above the right panel, from its right edge to the left, each only while it says something: the
ammo warning, the e-web, the cloak and the jetpack while they are not full. The caller has set PLACE_RIGHT.
=================
*/
void CG_HudRightStrip( void ) {
	const playerState_t	*ps = &cg.snap->ps;
	rectDef_t			panel;
	float				x, y, alive, most;

	if ( !CG_StatValue( "alive", &alive, &most ) || alive <= 0.0f ) {
		return;
	}
	// above the right panel, or in the corner where there is none
	if ( Hud_PanelRect( qtrue, &panel ) ) {
		x = panel.x + panel.w;
		y = panel.y - HUD_GAP - STRIP_H;
	} else {
		// (above what stands in the corner: the radar and the items)
		x = SCREEN_WIDTH - HUD_EDGE;
		y = Corner_Top() - HUD_GAP - STRIP_H;
	}
	if ( CG_HudAmmoWarning() ) {
		x -= Hud_Chip( x, y, STRIP_H, 0, CG_HudAmmoWarning(), cg.lowAmmoWarning == 2 ? hudRed : hudWarm, 1.0f, HUD_RIGHT, -1.0f ) + HUD_GAP;
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
