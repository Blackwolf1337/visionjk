/*
===========================================================================
Copyright (C) 2015 - 2023, TaystJK contributors
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

// The killfeed (tay_killfeed, 2026-10-17), after the one of TaystJK (hud_obituary.c): who killed whom with what, in
// lines at the upper right that fade out. The icons are the weapon icons of the game, so nothing has to be
// downloaded. A death without a weapon to show (a fall, lava, a telefrag) is the sentence of the console, in the
// language of the game. The obituary line in the console stays as it was.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"

#define KILLFEED_MAX		8
#define KILLFEED_FADE		1000		// the last milliseconds of a line fade it out

typedef struct {
	int			killer;				// ENTITYNUM_WORLD: nobody, the line is `text`
	int			victim;
	qhandle_t	icon;
	char		text[128];
	int			time;
} killfeedLine_t;

static killfeedLine_t	killfeed[KILLFEED_MAX];
static int				killfeedCount;

// the icon of what killed: a weapon of the game, the sentry, the dark force; 0 for the rest
static qhandle_t Killfeed_Icon( int mod ) {
	int weapon;

	switch ( mod ) {
	case MOD_STUN_BATON:			weapon = WP_STUN_BATON;			break;
	case MOD_MELEE:					weapon = WP_MELEE;				break;
	case MOD_SABER:					weapon = WP_SABER;				break;
	case MOD_BRYAR_PISTOL:
	case MOD_BRYAR_PISTOL_ALT:		weapon = WP_BRYAR_PISTOL;		break;
	case MOD_BLASTER:				weapon = WP_BLASTER;			break;
	case MOD_DISRUPTOR:
	case MOD_DISRUPTOR_SPLASH:
	case MOD_DISRUPTOR_SNIPER:		weapon = WP_DISRUPTOR;			break;
	case MOD_BOWCASTER:				weapon = WP_BOWCASTER;			break;
	case MOD_REPEATER:
	case MOD_REPEATER_ALT:
	case MOD_REPEATER_ALT_SPLASH:	weapon = WP_REPEATER;			break;
	case MOD_DEMP2:
	case MOD_DEMP2_ALT:				weapon = WP_DEMP2;				break;
	case MOD_FLECHETTE:
	case MOD_FLECHETTE_ALT_SPLASH:	weapon = WP_FLECHETTE;			break;
	case MOD_ROCKET:
	case MOD_ROCKET_SPLASH:
	case MOD_ROCKET_HOMING:
	case MOD_ROCKET_HOMING_SPLASH:	weapon = WP_ROCKET_LAUNCHER;	break;
	case MOD_THERMAL:
	case MOD_THERMAL_SPLASH:		weapon = WP_THERMAL;			break;
	case MOD_TRIP_MINE_SPLASH:
	case MOD_TIMED_MINE_SPLASH:		weapon = WP_TRIP_MINE;			break;
	case MOD_DET_PACK_SPLASH:		weapon = WP_DET_PACK;			break;
	case MOD_CONC:
	case MOD_CONC_ALT:				weapon = WP_CONCUSSION;			break;
	case MOD_SENTRY:				return trap->R_RegisterShaderNoMip( "gfx/hud/i_icon_sentrygun" );
	case MOD_FORCE_DARK:			return trap->R_RegisterShaderNoMip( "gfx/mp/f_icon_dk_l1" );
	default:						return 0;
	}
	return cgs.media.weaponIcons[weapon];
}

/*
=================
CG_KillfeedAdd

From CG_Obituary: killer ENTITYNUM_WORLD (or the victim itself) and the sentence of the console for a death without
somebody who killed, else the sentence for when there is no icon for what killed
=================
*/
void CG_KillfeedAdd( int killer, int victim, int mod, const char *text ) {
	killfeedLine_t *line;

	if ( !tay_killfeed.integer || victim < 0 || victim >= MAX_CLIENTS ) {
		return;
	}
	if ( killfeedCount == KILLFEED_MAX ) {
		memmove( killfeed, killfeed + 1, sizeof( killfeed[0] ) * ( KILLFEED_MAX - 1 ) );
		killfeedCount--;
	}
	line = &killfeed[killfeedCount++];
	line->killer = ( killer >= 0 && killer < MAX_CLIENTS && killer != victim ) ? killer : ENTITYNUM_WORLD;
	line->victim = victim;
	line->icon = line->killer != ENTITYNUM_WORLD ? Killfeed_Icon( mod ) : 0;
	Q_strncpyz( line->text, text ? text : "", sizeof( line->text ) );
	line->time = cg.time;
}

// the box behind a name: its team, oneself in green, else grey
static void Killfeed_NameColor( int client, float alpha, vec4_t out ) {
	static const vec4_t red = { 0.8f, 0.1f, 0.1f, 0.35f }, blue = { 0.1f, 0.3f, 0.9f, 0.35f };
	static const vec4_t self = { 0.1f, 0.7f, 0.2f, 0.35f }, other = { 0.05f, 0.08f, 0.12f, 0.6f };
	const float *c = other;

	if ( cg.snap && client == cg.snap->ps.clientNum ) {
		c = self;
	} else if ( cgs.gametype >= GT_TEAM && cgs.clientinfo[client].team == TEAM_RED ) {
		c = red;
	} else if ( cgs.gametype >= GT_TEAM && cgs.clientinfo[client].team == TEAM_BLUE ) {
		c = blue;
	}
	VectorCopy( c, out );
	out[3] = c[3] * alpha;
}

static void Killfeed_Box( float x, float y, float w, float h, const vec4_t fill ) {
	static const vec4_t rim = { 0.43f, 0.83f, 1.0f, 0.25f };

	if ( CG_HudNewLook() ) {
		vec4_t edge;

		VectorCopy4( rim, edge );
		edge[3] *= fill[3] / 0.6f;
		Skin_Panel( x, y, w, h, 3.0f, fill, edge );
	} else {
		CG_FillRect( x, y, w, h, (float *)fill );
	}
}

/*
=================
CG_KillfeedDraw

At the right edge (CG_Place2D( PLACE_RIGHT ) is set), from y down: below what the upper right already has. Returns
the y under the last line
=================
*/
float CG_KillfeedDraw( float y ) {
	const float	size = Com_Clamp( 0.5f, 2.0f, tay_killfeedSize.value );
	const float	lineH = 16.0f * size, pad = 3.0f * size, scale = 0.6f * size;
	const int	life = Q_max( 1000, tay_killfeedTime.integer );
	vec4_t		color, box;
	int			i, keep;

	if ( !tay_killfeed.integer || !cg.snap ) {
		return y;
	}

	// what is over goes
	for ( i = keep = 0; i < killfeedCount; i++ ) {
		if ( cg.time >= killfeed[i].time && cg.time - killfeed[i].time < life ) {
			killfeed[keep++] = killfeed[i];
		}
	}
	killfeedCount = keep;

	y += 4.0f + tay_killfeedY.value;
	for ( i = 0; i < killfeedCount; i++ ) {
		const killfeedLine_t	*line = &killfeed[i];
		const int				left = life - ( cg.time - line->time );
		const float				alpha = left < KILLFEED_FADE ? (float)left / KILLFEED_FADE : 1.0f;
		const float				textY = y + ( lineH - CG_Text_Height( "A", scale, FONT_SMALL ) ) * 0.5f;
		float					x = SCREEN_WIDTH - 4.0f - tay_killfeedX.value;

		VectorSet( color, 1.0f, 1.0f, 1.0f );
		color[3] = alpha;

		if ( line->killer == ENTITYNUM_WORLD || !line->icon ) {
			// a sentence: the victim's box, the text in it
			const char	*text = line->text[0] ? line->text : cgs.clientinfo[line->victim].name;
			const float	w = CG_Text_Width( text, scale, FONT_SMALL ) + 2.0f * pad;

			x -= w;
			Killfeed_NameColor( line->victim, alpha, box );
			Killfeed_Box( x, y, w, lineH, box );
			CG_Text_Paint( x + pad, textY, scale, color, text, 0, 0, 0, FONT_SMALL );
		} else {
			// killer, icon, victim, from the right
			const char	*victim = cgs.clientinfo[line->victim].name, *killer = cgs.clientinfo[line->killer].name;
			const float	vw = CG_Text_Width( victim, scale, FONT_SMALL ) + 2.0f * pad;
			const float	kw = CG_Text_Width( killer, scale, FONT_SMALL ) + 2.0f * pad;
			const float	iconW = lineH;

			x -= vw;
			Killfeed_NameColor( line->victim, alpha, box );
			Killfeed_Box( x, y, vw, lineH, box );
			CG_Text_Paint( x + pad, textY, scale, color, victim, 0, 0, 0, FONT_SMALL );

			x -= iconW + pad;
			trap->R_SetColor( color );
			CG_DrawPic( x + pad * 0.5f, y, iconW, lineH, line->icon );
			trap->R_SetColor( NULL );

			x -= kw;
			Killfeed_NameColor( line->killer, alpha, box );
			Killfeed_Box( x, y, kw, lineH, box );
			CG_Text_Paint( x + pad, textY, scale, color, killer, 0, 0, 0, FONT_SMALL );
		}
		y += lineH + 2.0f;
	}
	return y;
}
