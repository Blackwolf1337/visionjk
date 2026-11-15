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

// cg_powers.c -- VisionJK: the force powers that run right now with the new HUD, as a row of small glass chips
// above the panel of the health (ui/vision/hud.menu) in place of the icons of the game (CG_DrawActivePowers, which
// stays for v_hud 0): the picture of the power, the rim in the colour of its side, pulsing; a chip slides in when
// its power starts and fades away when it ends. The recovery after rage is a chip with a bar that runs down (the
// server sends when it ends; how long another power still runs it does not send). The strip also has the tags of
// the player's state after the powers: the alternate dimension of JA+, the flag one
// carries. It stands above the panel where the menu file puts it. Not from JA+.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"
#include "cg_hudkit.h"

#define CHIP_SIZE		17.0f		// in units of the 640 x 480 screen
#define CHIP_GAP		3.0f
#define CHIP_RADIUS		4.0f
#define CHIP_MSEC		160.0f		// a chip comes and goes in this time
#define CHIP_PULSE_MSEC	1100.0f
#define CHIP_SLOTS		( NUM_FORCE_POWERS + 1 )	// the powers, and the recovery after rage

static const vec3_t	sideLight = { 0.36f, 0.78f, 0.96f }, sideDark = { 1.0f, 0.32f, 0.26f }, sideNeutral = { 0.88f, 0.92f, 1.0f };

// the colour (rgb) of the side of a power: what belongs to it in the HUD is drawn in it
const float *CG_ForceSideColor( int fp ) {
	return forcePowerDarkLight[fp] == FORCE_LIGHTSIDE ? sideLight : forcePowerDarkLight[fp] == FORCE_DARKSIDE ? sideDark : sideNeutral;
}

// a chip, shown this much (0 to 1); left: what is left of a time that runs down as a bar along its bottom, below 0
// none
static void Powers_Chip( float x, float y, float shown, qhandle_t icon, const float *tint, float left ) {
	const float		pulse = 0.5f + 0.5f * sinf( (float)cg.time * 2.0f * M_PI / CHIP_PULSE_MSEC );
	const float		inset = CHIP_SIZE * 0.15f;
	vec4_t			glass = { 0.035f, 0.07f, 0.12f, 0.62f };
	const vec4_t	rim = { tint[0], tint[1], tint[2], ( 0.45f + 0.35f * pulse ) * shown };
	const vec4_t	glow = { tint[0], tint[1], tint[2], ( 0.14f + 0.14f * pulse ) * shown };
	const vec4_t	picture = { 1.0f, 1.0f, 1.0f, shown };

	// (frosted glass only once the chip is nearly there: it can not fade)
	if ( shown > 0.6f && CG_DrawBackdrop( x, y, CHIP_SIZE, CHIP_SIZE, CHIP_RADIUS ) ) {
		glass[3] = 0.4f;
	}
	glass[3] *= shown;
	Skin_Soft( x, y, CHIP_SIZE, CHIP_SIZE, 5.0f, glow );
	Skin_Panel( x, y, CHIP_SIZE, CHIP_SIZE, CHIP_RADIUS, glass, rim );
	if ( icon ) {
		trap->R_SetColor( picture );
		CG_DrawPic( x + inset, y + inset, CHIP_SIZE - 2.0f * inset, CHIP_SIZE - 2.0f * inset, icon );
		trap->R_SetColor( NULL );
	}
	if ( left >= 0.0f ) {
		const vec4_t fill = { tint[0], tint[1], tint[2], 0.95f * shown }, back = { 0.0f, 0.0f, 0.0f, 0.5f * shown };

		Skin_Bar( x + inset, y + CHIP_SIZE - 3.0f, CHIP_SIZE - 2.0f * inset, 1.5f, Com_Clamp( 0.0f, 1.0f, left ), fill, back );
	}
}

/*
=================
CG_PowerChips

Every frame of the HUD of the new look, also while no power runs (a chip that goes fades away). The caller has
set the place (CG_Place2D( PLACE_LEFT )).
=================
*/
void CG_PowerChips( void ) {
	static float		shown[CHIP_SLOTS];
	static int			lastDraw, rageStart, rageEnd;
	const playerState_t	*ps = &cg.snap->ps;
	const float			step = (float)cg.frametime / CHIP_MSEC;
	rectDef_t			panel;
	float				x, bottom, value, most;
	int					i;

	// not drawn for a while (another level, the scope, watching): nothing left over from then
	if ( cg.time - lastDraw > 250 || cg.time < lastDraw ) {
		memset( shown, 0, sizeof( shown ) );
	}
	if ( ps->zoomMode || cgs.clientinfo[ps->clientNum].team == TEAM_SPECTATOR ) {
		return;
	}
	lastDraw = cg.time;
	Hud_PanelRect( qfalse, &panel );
	x = panel.x;
	bottom = panel.y - HUD_GAP;
	// (whom one watches is in the bar at the top, hud/cg_hudtop.c, and nowhere else)
	if ( ps->fd.forceRageRecoveryTime > cg.time && ps->fd.forceRageRecoveryTime != rageEnd ) {
		rageEnd = ps->fd.forceRageRecoveryTime;
		rageStart = cg.time;
	}

	for ( i = 0; i < CHIP_SLOTS; i++ ) {
		const qboolean	rage = (qboolean)( i == NUM_FORCE_POWERS );
		const int		fp = rage ? FP_RAGE : forcePowerSorted[i];
		const qboolean	on = rage ? (qboolean)( ps->fd.forceRageRecoveryTime > cg.time )
							: (qboolean)( ( ps->fd.forcePowersActive & ( 1 << fp ) ) && CG_IsDurationPower( fp ) );
		float			ease, left = -1.0f;

		shown[i] = Com_Clamp( 0.0f, 1.0f, shown[i] + ( on ? step : -step ) );
		if ( shown[i] <= 0.0f ) {
			continue;
		}
		// (eased out: it slows as it arrives)
		ease = 1.0f - ( 1.0f - shown[i] ) * ( 1.0f - shown[i] );
		if ( rage && rageEnd > rageStart ) {
			left = (float)( rageEnd - cg.time ) / (float)( rageEnd - rageStart );
		}
		// it comes up from the panel, and the ones after it move over as it takes its place
		Powers_Chip( x, bottom - CHIP_SIZE + ( 1.0f - ease ) * 5.0f, shown[i],
			rage ? cgs.media.rageRecShader : cgs.media.forcePowerIcons[fp], CG_ForceSideColor( fp ), left );
		x += ( CHIP_SIZE + CHIP_GAP ) * ease;
	}

	// the tags of the state, while the player lives
	if ( !CG_StatValue( "alive", &value, &most ) || value <= 0.0f ) {
		return;
	}
	if ( CG_StatValue( "altdim", &value, &most ) && value > 0.0f ) {
		x += Hud_Chip( x, bottom - CHIP_SIZE, CHIP_SIZE, 0, CG_VisionText( "Alternate Dimension" ), hudWarm, 1.0f, HUD_LEFT, -1.0f )
			+ CHIP_GAP;
	}
	for ( i = 0; i < 3; i++ ) {
		static const int	flagPowerup[3] = { PW_REDFLAG, PW_BLUEFLAG, PW_NEUTRALFLAG };
		static const char	*flagText[3] = { "You carry the red flag", "You carry the blue flag", "You carry the flag" };

		if ( ps->powerups[flagPowerup[i]] ) {
			x += Hud_Chip( x, bottom - CHIP_SIZE, CHIP_SIZE, 0, CG_VisionText( flagText[i] ),
				i == 0 ? hudRed : i == 1 ? hudBlue : hudText, 1.0f, HUD_LEFT, -1.0f ) + CHIP_GAP;
		}
	}
}
