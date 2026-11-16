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
//
// The feed (2026-11-15): with the HUD of the new look and an engine of this tree (ext.Con_FeedRead)
// these lines, the lines the engine would write at the top of the window and the notices (hud/cg_notify.c) are
// one feed at the upper left: a kill with its icon, a line of the console as it is, a warning with an amber
// rim, an error with a red one. The engine draws no lines of its own then (v_feedHandler), and the sentence of
// a death goes to the console only, since the feed has the kill.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"
#include "cg_hudkit.h"

#define KILLFEED_MAX		16			// kept; the feed shows v_hudFeedLines of them, the killfeed at the right 8
#define KILLFEED_RIGHT		8
#define FEED_WIDTH			300.0f		// a line of text is cut at this
#define FEED_LIFE			6000		// ms a line of the feed stays
#define KILLFEED_FADE		1000		// the last milliseconds of a line fade it out

typedef struct {
	int			level;				// FEED_KILL, or what a line of text is
	int			killer;				// ENTITYNUM_WORLD: nobody, the line is `text`
	int			victim;
	int			weapon;				// what killed, for its model in the feed; WP_NONE: none
	qhandle_t	icon;
	char		text[128];
	int			time;
} killfeedLine_t;

static killfeedLine_t	killfeed[KILLFEED_MAX];
static int				killfeedCount;

// the last death of the player himself, for the card while he is dead (hud/cg_hudtop.c), also with tay_killfeed 0
static struct {
	int		killer;			// -1: nobody
	int		weapon;			// WP_NONE: none to name
	char	text[128];
	int		time;
} lastDeath;

// the weapon of the game that killed, WP_NONE for what is no weapon
static int Killfeed_Weapon( int mod ) {
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
	default:						weapon = WP_NONE;				break;
	}
	return weapon;
}

// the icon of what killed: a weapon of the game, the sentry, the dark force; 0 for the rest
static qhandle_t Killfeed_Icon( int mod ) {
	const int weapon = Killfeed_Weapon( mod );

	if ( mod == MOD_SENTRY ) {
		return trap->R_RegisterShaderNoMip( "gfx/hud/i_icon_sentrygun" );
	}
	if ( mod == MOD_FORCE_DARK ) {
		return trap->R_RegisterShaderNoMip( "gfx/mp/f_icon_dk_l1" );
	}
	return weapon != WP_NONE ? cgs.media.weaponIcons[weapon] : 0;
}

/*
=================
CG_KillfeedLastDeath

How the player died last: the client that killed him (-1 nobody), the weapon (WP_NONE), the sentence of the
console, when. qfalse: he has not died on this level.
=================
*/
qboolean CG_KillfeedLastDeath( int *killer, int *weapon, const char **text, int *time ) {
	if ( !lastDeath.time || lastDeath.time > cg.time ) {
		return qfalse;
	}
	*killer = lastDeath.killer;
	*weapon = lastDeath.weapon;
	*text = lastDeath.text;
	*time = lastDeath.time;
	return qtrue;
}

/*
=================
CG_KillfeedAdd

From CG_Obituary: killer ENTITYNUM_WORLD (or the victim itself) and the sentence of the console for a death without
somebody who killed, else the sentence for when there is no icon for what killed
=================
*/
static killfeedLine_t *Killfeed_NewLine( void ) {
	killfeedLine_t *line;

	if ( killfeedCount == KILLFEED_MAX ) {
		memmove( killfeed, killfeed + 1, sizeof( killfeed[0] ) * ( KILLFEED_MAX - 1 ) );
		killfeedCount--;
	}
	line = &killfeed[killfeedCount++];
	memset( line, 0, sizeof( *line ) );
	line->time = cg.time;
	return line;
}

void CG_KillfeedAdd( int killer, int victim, int mod, const char *text ) {
	killfeedLine_t *line;

	if ( cg.snap && victim == cg.snap->ps.clientNum ) {
		lastDeath.killer = ( killer >= 0 && killer < MAX_CLIENTS && killer != victim ) ? killer : -1;
		lastDeath.weapon = Killfeed_Weapon( mod );
		Q_strncpyz( lastDeath.text, text ? text : "", sizeof( lastDeath.text ) );
		lastDeath.time = cg.time;
	}
	if ( ( !tay_killfeed.integer && !CG_HudFeedActive() ) || victim < 0 || victim >= MAX_CLIENTS ) {
		return;
	}
	line = Killfeed_NewLine();
	line->level = FEED_KILL;
	line->killer = ( killer >= 0 && killer < MAX_CLIENTS && killer != victim ) ? killer : ENTITYNUM_WORLD;
	line->victim = victim;
	line->icon = line->killer != ENTITYNUM_WORLD ? Killfeed_Icon( mod ) : 0;
	line->weapon = Killfeed_Weapon( mod );
	Q_strncpyz( line->text, text ? text : "", sizeof( line->text ) );
}

/*
===============================================================================

The feed

===============================================================================
*/

/*
=================
CG_HudFeedActive

Are the lines of the console, the notices and the kills one feed: the new look and an engine that hands its
lines over. It has no switch and no settings of its own: tay_killfeed and the cvars beside it are for the killfeed
at the right edge, which the HUD of the game and an engine without the feed still have.
=================
*/
qboolean CG_HudFeedActive( void ) {
	static int has = -1;

	if ( has < 0 ) {
		char value[8];

		// (the ROM cvar first: an older engine ends the table of ext before Con_FeedRead)
		trap->Cvar_VariableStringBuffer( "v_cgameFeed", value, sizeof( value ) );
		has = ( atoi( value ) && trap->ext.Con_FeedRead && trap->ext.Con_FeedNewest ) ? 1 : 0;
	}
	return (qboolean)( has > 0 && CG_HudNewLook() );
}

// a line of text for the feed: colour codes stay, a line break ends it
void CG_HudFeedAdd( int level, const char *text ) {
	killfeedLine_t	*line;
	char			*c;

	if ( !text || !text[0] ) {
		return;
	}
	line = Killfeed_NewLine();
	line->level = level;
	line->killer = ENTITYNUM_WORLD;
	line->victim = -1;
	Q_strncpyz( line->text, text, sizeof( line->text ) );
	for ( c = line->text; *c; c++ ) {
		if ( *c == '\n' || *c == '\r' ) {
			*c = ' ';
		}
	}
}

/*
=================
CG_HudFeedPoll

Every frame: tells the engine whether the cgame draws its lines, and takes the new ones.
=================
*/
void CG_HudFeedPoll( void ) {
	static int	handler = -1, seq = -1;
	const int	on = CG_HudFeedActive() ? 1 : 0;
	char		text[256];
	int			level, next;

	if ( on != handler ) {
		// (also after a new cgame: the engine cleared it)
		trap->Cvar_Set( "v_feedHandler", on ? "1" : "0" );
		handler = on;
		seq = -1;
	}
	if ( !on ) {
		return;
	}
	if ( seq < 0 ) {
		// what was printed before is not news
		seq = trap->ext.Con_FeedNewest();
	}
	while ( ( next = trap->ext.Con_FeedRead( seq, &level, text, sizeof( text ) ) ) != 0 ) {
		const char *ch;

		seq = next;
		// a line that says nothing (the rows of stars and dashes round a banner) is left to the console
		for ( ch = text; *ch && !isalnum( (unsigned char)*ch ); ch++ ) {
		}
		if ( !*ch ) {
			continue;
		}
		CG_HudFeedAdd( level == 2 ? FEED_ERROR : level == 1 ? FEED_ALERT : FEED_INFO, text );
	}
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
	static const vec4_t rim = { SKIN_RGB_ACCENT, 0.25f };

	if ( CG_HudNewLook() ) {
		vec4_t edge;

		VectorCopy4( rim, edge );
		edge[3] *= fill[3] / 0.6f;
		Skin_Panel( x, y, w, h, 3.0f, fill, edge );
	} else {
		CG_FillRect( x, y, w, h, (float *)fill );
	}
}

// the colour of a name in a line of the new look: oneself bright, a team its colour
static const float *Killfeed_NameTint( int client ) {
	if ( cg.snap && client == cg.snap->ps.clientNum ) {
		return hudBright;
	}
	if ( cgs.gametype >= GT_TEAM && Hud_TeamColor( cgs.clientinfo[client].team ) ) {
		return Hud_TeamColor( cgs.clientinfo[client].team );
	}
	return hudText;
}

/*
=================
Killfeed_DrawNew

The lines in the new look: one glass panel each, killer, icon, victim. A kill of the player himself has the rim
of the accent and a +1, his death a red rim, a death without a killer a cross and the sentence, fainter.
=================
*/
static float Killfeed_DrawNew( float y, float size, int life, qboolean atLeft ) {
	const float	lineH = 15.0f * size, pad = 5.0f * size, scale = HUD_BODY * size;
	const int	self = cg.snap->ps.clientNum;
	int			i;

	// the newest ones: as many as the feed is set to, or as the killfeed at the right has
	for ( i = Q_max( 0, killfeedCount - ( atLeft ? Q_max( 1, Q_min( KILLFEED_MAX, (int)( v_hudFeedLines.value + 0.5f ) ) ) : KILLFEED_RIGHT ) ); i < killfeedCount; i++ ) {
		const killfeedLine_t	*line = &killfeed[i];
		const int				left = life - ( cg.time - line->time );
		const float				alpha = left < KILLFEED_FADE ? (float)left / KILLFEED_FADE : 1.0f;
		const float				mid = y + lineH * 0.5f, right = SCREEN_WIDTH - HUD_EDGE - tay_killfeedX.value;
		const qboolean			world = (qboolean)( line->killer == ENTITYNUM_WORLD || !line->icon );
		const float				*rim = line->killer == self ? hudAccent : line->victim == self ? hudRed : NULL;
		float					w, x;

		if ( line->level != FEED_KILL ) {
			// a line of the console or a notice: as it is, a warning amber, an error red, each with its mark
			const float	*tint = line->level == FEED_ERROR ? hudRed : line->level == FEED_ALERT ? hudWarm : NULL;
			const float	mark = tint ? lineH * 0.6f + 2.0f : 0.0f;
			const float	tw = Q_min( FEED_WIDTH, Hud_TextWidth( line->text, scale ) );

			w = mark + tw + 2.0f * pad;
			x = atLeft ? HUD_EDGE : right - w;
			Hud_Glass( x, y, w, lineH, 3.5f, alpha * ( tint ? 1.0f : 0.8f ), tint, qtrue );
			if ( tint ) {
				Hud_TextMid( x + pad, mid, scale, tint, alpha, HUD_LEFT, "!", 0.0f );
			}
			Hud_TextMid( x + pad + mark, mid, scale, hudText, alpha, HUD_LEFT, line->text, FEED_WIDTH );
		} else if ( world ) {
			const char *text = line->text[0] ? line->text : cgs.clientinfo[line->victim].name;

			w = lineH * 0.6f + 4.0f + Hud_TextWidth( text, scale ) + 2.0f * pad;
			x = atLeft ? HUD_EDGE : right - w;
			Hud_Glass( x, y, w, lineH, 3.5f, alpha * 0.8f, rim, qtrue );
			Hud_TextMid( x + pad, mid, scale * 0.9f, hudDim, alpha, HUD_LEFT, "x", 0.0f );
			Hud_TextMid( x + pad + lineH * 0.6f + 4.0f, mid, scale, hudDim, alpha, HUD_LEFT, text, 0.0f );
		} else {
			const char	*killer = cgs.clientinfo[line->killer].name, *victim = cgs.clientinfo[line->victim].name;
			const float	kw = Hud_TextWidth( killer, scale ), vw = Hud_TextWidth( victim, scale ), iconW = lineH * 1.3f;
			const char	*plus = line->killer == self ? "+1" : NULL;
			const float	pw = plus ? Hud_TextWidth( plus, HUD_CAPS ) + 5.0f : 0.0f;
			vec4_t		white;

			w = kw + vw + iconW + 8.0f + pw + 2.0f * pad;
			x = atLeft ? HUD_EDGE : right - w;
			Hud_Glass( x, y, w, lineH, 3.5f, alpha, rim, qtrue );
			Hud_TextMid( x + pad, mid, scale, Killfeed_NameTint( line->killer ), alpha, HUD_LEFT, killer, 0.0f );
			Hud_Color( white, colorWhite, alpha );
			// what killed as its model where it has one (hud/cg_select3d.c), seen from the side and standing
			// still, the muzzle towards the victim, until the line is nearly gone; else its picture
			{
				const gitem_t *item = ( atLeft && line->weapon > WP_NONE && line->weapon != WP_MELEE )
					? BG_FindItemForWeapon( (weapon_t)line->weapon ) : NULL;

				trap->R_SetColor( white );
				if ( item && CG_Select3DModelFor( item ) ) {
					CG_Select3DIcon( x + pad + kw + 4.0f, y - lineH * 0.15f, iconW, lineH * 1.3f, item, 0, NULL, qtrue, alpha );
				} else {
					// (square, as the pictures of the game are, in the middle of the room of the model)
					const float side = lineH * 0.86f;

					CG_DrawPic( x + pad + kw + 4.0f + ( iconW - side ) * 0.5f, y + ( lineH - side ) * 0.5f, side, side, line->icon );
				}
				trap->R_SetColor( NULL );
			}
			Hud_TextMid( x + pad + kw + 8.0f + iconW, mid, scale, Killfeed_NameTint( line->victim ), alpha, HUD_LEFT, victim, 0.0f );
			if ( plus ) {
				Hud_Caps( x + w - pad, mid, hudAccent, alpha, HUD_RIGHT, plus );
			}
		}
		y += lineH + 3.0f;
	}
	return y;
}

// what is over goes
static void Killfeed_Expire( int life ) {
	int i, keep;

	for ( i = keep = 0; i < killfeedCount; i++ ) {
		if ( cg.time >= killfeed[i].time && cg.time - killfeed[i].time < life ) {
			killfeed[keep++] = killfeed[i];
		}
	}
	killfeedCount = keep;
}

/*
=================
CG_HudFeedDraw

The one feed at the left edge (CG_Place2D( PLACE_LEFT ) is set) from y down.
=================
*/
void CG_HudFeedDraw( float y ) {
	if ( !cg.snap || !CG_HudFeedActive() ) {
		return;
	}
	Killfeed_Expire( FEED_LIFE );
	Killfeed_DrawNew( y, 1.0f, FEED_LIFE, qtrue );
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
	int			i;

	// (as one feed it stands at the upper left, CG_HudFeedDraw)
	if ( !tay_killfeed.integer || !cg.snap || CG_HudFeedActive() ) {
		return y;
	}
	Killfeed_Expire( life );

	y += 4.0f + tay_killfeedY.value;
	if ( CG_HudNewLook() ) {
		return Killfeed_DrawNew( y, size, life, qfalse );
	}
	for ( i = Q_max( 0, killfeedCount - KILLFEED_RIGHT ); i < killfeedCount; i++ ) {
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
