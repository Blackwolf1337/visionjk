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

// cg_hudtop.c -- VisionJK: what the new HUD shows in the middle of the screen, where the HUD of the game writes
// loose text over the level: at the top what the whole match is about (whom one watches and how to switch, the
// two fighters of a duel, the warmup, a lost connection), as cards one under the other; the message of the server
// or the game (a center print) as a card; and while one is dead a card of who killed one with what and how to go
// on. Each in place of the stock text (CG_DrawFollow, CG_DrawSpectator, CG_DrawWarmup, CG_DrawDisconnect,
// CG_DrawCenterString). Not from JA+.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"
#include "cg_hudkit.h"

#define TOP_W			310.0f		// the bar at the top (whom one watches, the duel)
#define TOP_H			37.0f
#define TOP_FACE		28.0f		// the picture of a player in it
#define CARD_PAD		8.0f

static void Top_Face( float x, float y, float size, int client, float alpha ) {
	const clientInfo_t	*ci = &cgs.clientinfo[client];
	vec4_t				white;

	if ( !ci->modelIcon ) {
		return;
	}
	Hud_Color( white, colorWhite, alpha );
	trap->R_SetColor( white );
	CG_DrawPic( x, y, size, size, ci->modelIcon );
	trap->R_SetColor( NULL );
}

// key hints at the right of a bar, one under the other, their right ends at x
static void Top_Hints( float x, float midY, const char *key1, const char *what1, const char *key2, const char *what2 ) {
	if ( key2 ) {
		Hud_KeyHint( x - Hud_KeyHintWidth( key1, what1 ), midY - 5.5f, key1, what1, 1.0f );
		Hud_KeyHint( x - Hud_KeyHintWidth( key2, what2 ), midY + 5.5f, key2, what2, 1.0f );
	} else {
		Hud_KeyHint( x - Hud_KeyHintWidth( key1, what1 ), midY, key1, what1, 1.0f );
	}
}

/*
=================
Top_Watch

While one watches: following somebody his picture, his name and the keys to the next player and to flying free;
flying free who fights in a duel, or how to join. Returns the y under it.
=================
*/
static float Top_Watch( float y ) {
	const playerState_t	*ps = &cg.snap->ps;
	const float			x = SCREEN_WIDTH * 0.5f - TOP_W * 0.5f, mid = y + TOP_H * 0.5f;
	const char			*attack = Hud_KeyFor( "+attack", CG_VisionText( "Attack" ) );
	char				title[128];

	if ( ps->pm_flags & PMF_FOLLOW ) {
		Hud_Glass( x, y, TOP_W, TOP_H, 6.0f, 1.0f, NULL, qfalse );
		Top_Face( x + 4.5f, y + 4.5f, TOP_FACE, ps->clientNum, 1.0f );
		Hud_Caps( x + TOP_FACE + 12.0f, mid - 7.0f, hudWarm, 1.0f, HUD_LEFT, CG_GetStringEdString( "MP_INGAME", "FOLLOWING" ) );
		Hud_TextMid( x + TOP_FACE + 12.0f, mid + 4.0f, HUD_LARGE, hudBright, 1.0f, HUD_LEFT, cgs.clientinfo[ps->clientNum].name, 130.0f );
		Top_Hints( x + TOP_W - 8.0f, mid, attack, CG_VisionText( "Next player" ),
			Hud_KeyFor( "+moveup", CG_VisionText( "Jump" ) ), CG_VisionText( "Fly free" ) );
		return y + TOP_H + HUD_GAP;
	}
	if ( ps->persistant[PERS_TEAM] != TEAM_SPECTATOR ) {
		return y;
	}
	Hud_Glass( x, y, TOP_W, TOP_H, 6.0f, 1.0f, NULL, qfalse );
	if ( ( cgs.gametype == GT_DUEL || cgs.gametype == GT_POWERDUEL ) && cgs.duelist1 >= 0 && cgs.duelist2 >= 0 ) {
		Com_sprintf( title, sizeof( title ), "%s %s %s", cgs.clientinfo[cgs.duelist1].name,
			CG_GetStringEdString( "MP_INGAME", "SPECHUD_VERSUS" ), cgs.clientinfo[cgs.duelist2].name );
		Hud_Caps( x + CARD_PAD, mid - 7.0f, hudWarm, 1.0f, HUD_LEFT, CG_GetStringEdString( "MP_INGAME", "WAITING_TO_PLAY" ) );
	} else {
		Q_strncpyz( title, CG_GetStringEdString( "MP_INGAME", "SPECTATOR" ), sizeof( title ) );
		Hud_Caps( x + CARD_PAD, mid - 7.0f, hudWarm, 1.0f, HUD_LEFT, CG_VisionText( "Flying free" ) );
	}
	Hud_TextMid( x + CARD_PAD, mid + 4.0f, HUD_LARGE * 0.85f, hudBright, 1.0f, HUD_LEFT, title, 170.0f );
	Top_Hints( x + TOP_W - 8.0f, mid, attack, CG_VisionText( "Follow a player" ), NULL, NULL );
	return y + TOP_H + HUD_GAP;
}

/*
=================
Top_Duel

A duel as a small card: the head of the other one as he is right now (hud/cg_select3d.c, his picture while he is
out of sight), that one duels, his name, and his health where the server sends the health of the duelists. What
is elsewhere is not repeated: the own health is in the panel at the bottom, the score of the duel game type on
the card of the match.
=================
*/
static float Top_Duel( float y ) {
	const playerState_t	*ps = &cg.snap->ps;
	const float			face = TOP_H - 8.0f;
	const qboolean		score = (qboolean)( cgs.gametype == GT_DUEL );
	const float			w = 160.0f, x = SCREEN_WIDTH * 0.5f - w * 0.5f, tx = x + 4.0f + face + 7.0f;
	const float			room = x + w - CARD_PAD - tx;
	const vec4_t		inside = { 0.0f, 0.0f, 0.0f, 0.30f };
	int					other = -1, otherHealth = -1;
	vec4_t				rim;

	if ( ps->duelInProgress ) {
		other = ps->duelIndex;
	} else if ( score && cgs.clientinfo[ps->clientNum].team != TEAM_SPECTATOR ) {
		other = ps->clientNum == cgs.duelist1 ? cgs.duelist2 : ps->clientNum == cgs.duelist2 ? cgs.duelist1 : -1;
	}
	if ( other < 0 || other >= MAX_CLIENTS || !cgs.clientinfo[other].infoValid || ps->stats[STAT_HEALTH] <= 0 ) {
		return y;
	}
	if ( score && cgs.showDuelHealths >= 1 ) {
		otherHealth = other == cgs.duelist1 ? cgs.duelist1health : other == cgs.duelist2 ? cgs.duelist2health : -1;
	}
	Hud_Glass( x, y, w, TOP_H, 6.0f, 1.0f, NULL, qfalse );
	// his head, in a slot with a red rim
	Hud_Color( rim, hudRed, 0.9f );
	Skin_Panel( x + 4.0f, y + 4.0f, face, face, 3.5f, inside, rim );
	trap->R_SetColor( NULL );
	if ( !CG_Select3DHead( x + 5.0f, y + 5.0f, face - 2.0f, face - 2.0f, other ) && cgs.clientinfo[other].modelIcon ) {
		CG_DrawPic( x + 6.0f, y + 6.0f, face - 4.0f, face - 4.0f, cgs.clientinfo[other].modelIcon );
	}
	CG_Place2D( PLACE_CENTER );		// (the scene of the head changed what is drawn where)
	Hud_Caps( tx, y + 9.0f, hudRed, 1.0f, HUD_LEFT, CG_GetStringEdString( "MP_INGAME", "DUELING" ) );
	Hud_TextMid( tx, y + ( otherHealth >= 0 ? 20.0f : 24.0f ), HUD_BODY, hudBright, 1.0f, HUD_LEFT, cgs.clientinfo[other].name, room );
	if ( otherHealth >= 0 ) {
		Hud_Bar( tx, y + 29.0f, room, 3.0f, otherHealth / 100.0f, otherHealth <= 25 ? hudRed : otherHealth <= 50 ? hudWarm : hudGreen, 1.0f, qfalse );
	}
	return y + TOP_H + HUD_GAP;
}

/*
=================
Top_Warmup

The warmup as a card: who fights or the game type, the seconds until it starts with a bar that runs down, or that
it waits for players.
=================
*/
static float Top_Warmup( float y ) {
	static int	start, end;
	const float	x = SCREEN_WIDTH * 0.5f - 135.0f, w = 270.0f, h = 40.0f;
	const int	sec = CG_WarmupTick();
	char		title[128];

	if ( sec == -2 || ( cg.snap->ps.pm_flags & PMF_FOLLOW ) ) {
		return y;
	}
	if ( cg.warmup != end ) {
		end = cg.warmup;
		start = cg.time;
	}
	title[0] = '\0';
	if ( ( cgs.gametype == GT_DUEL || cgs.gametype == GT_POWERDUEL ) && cgs.duelist1 >= 0 && cgs.duelist2 >= 0 ) {
		Com_sprintf( title, sizeof( title ), "%s %s %s", cgs.clientinfo[cgs.duelist1].name,
			CG_GetStringEdString( "MP_INGAME", "SPECHUD_VERSUS" ), cgs.clientinfo[cgs.duelist2].name );
	} else {
		Q_strncpyz( title, BG_GetGametypeString( cgs.gametype ), sizeof( title ) );
	}
	Hud_Glass( x, y, w, h, 6.0f, 1.0f, NULL, qfalse );
	Hud_Caps( x + CARD_PAD, y + 9.0f, hudWarm, 1.0f, HUD_LEFT, CG_VisionText( "Warmup" ) );
	if ( sec == -1 ) {
		Hud_TextMid( x + CARD_PAD, y + 24.0f, HUD_BODY, hudBright, 1.0f, HUD_LEFT,
			CG_GetStringEdString( "MP_INGAME", "WAITING_FOR_PLAYERS" ), w - 2.0f * CARD_PAD );
		return y + h + HUD_GAP;
	}
	Hud_TextMid( x + CARD_PAD, y + 24.0f, HUD_BODY, hudBright, 1.0f, HUD_LEFT, title, w - 70.0f );
	Hud_TextMid( x + w - CARD_PAD - 18.0f, y + 18.0f, HUD_HUGE, hudBright, 1.0f, HUD_CENTER, va( "%i", sec + 1 ), 0.0f );
	Hud_Caps( x + w - CARD_PAD - 18.0f, y + 33.0f, hudDim, 1.0f, HUD_CENTER, CG_GetStringEdString( "MP_INGAME", "STARTS_IN" ) );
	if ( end > start ) {
		Hud_Bar( x + 6.0f, y + h - 3.0f, w - 12.0f, 1.5f, (float)( end - cg.time ) / ( end - start ), hudWarm, 1.0f, qfalse );
	}
	return y + h + HUD_GAP;
}

/*
=================
CG_HudNoticeCard

A notice at the top in the middle: a caption, a line, the rim in tint (NULL: the rim of the HUD), the picture
blinking when blink. Returns the y under it. Also for the lost connection and the change of the level.
=================
*/
float CG_HudNoticeCard( float y, const char *caption, const char *text, const float *tint, qhandle_t icon, qboolean blink ) {
	const float	h = 26.0f, iconSize = 14.0f;
	const float	w = Q_max( Hud_TextWidth( text, HUD_BODY ), Hud_TextWidth( caption, HUD_CAPS ) ) + 2.0f * CARD_PAD + ( icon ? iconSize + 6.0f : 0.0f );
	const float	x = SCREEN_WIDTH * 0.5f - w * 0.5f;
	float		tx = x + CARD_PAD;

	Hud_Glass( x, y, w, h, 6.0f, 1.0f, tint, qfalse );
	if ( icon && ( !blink || !( ( cg.time >> 9 ) & 1 ) ) ) {
		trap->R_SetColor( NULL );
		CG_DrawPic( tx, y + ( h - iconSize ) * 0.5f, iconSize, iconSize, icon );
	}
	if ( icon ) {
		tx += iconSize + 6.0f;
	}
	Hud_Caps( tx, y + 8.0f, tint ? tint : hudDim, 1.0f, HUD_LEFT, caption );
	Hud_TextMid( tx, y + 18.0f, HUD_BODY, hudBright, 1.0f, HUD_LEFT, text, 0.0f );
	return y + h + HUD_GAP;
}

/*
=================
CG_HudTopDraw

The cards at the top in the middle, one under the other. Each is only there while it says something.
=================
*/
void CG_HudTopDraw( void ) {
	float y = HUD_EDGE_Y;

	y = Top_Watch( y );
	y = CG_HudSiegeTop( y );
	y = Top_Duel( y );
	y = Top_Warmup( y );
	CG_DrawDisconnectAt( y );
}

/*
=================
CG_HudCenterString

The center print as a card at its height: its lines (broken where they are too wide), fading out at the end of its
time, with a bar of that time when it was printed once (a server that prints the same every second, as the MOTD of
JA+ does, counts in its own words).
=================
*/
void CG_HudCenterString( void ) {
	const float	scale = HUD_BODY * 1.15f, most = 300.0f, lineH = Hud_TextHeight( scale ) + 4.0f;
	char		lines[16][256];
	const char	*in;
	float		*fade, w = 0.0f, h, x, y;
	int			count = 0, i;

	if ( !cg.centerPrintTime || !( fade = CG_FadeColor( cg.centerPrintTime, 1000 * cg_centerTime.value ) ) ) {
		return;
	}
	// the lines: at each newline, and before a word that would make one wider than most
	for ( in = cg.centerPrint; *in && count < (int)ARRAY_LEN( lines ); ) {
		char	line[256];
		int		n = 0, lastSpace = -1;

		while ( *in && *in != '\n' && n < (int)sizeof( line ) - 1 ) {
			line[n] = *in;
			line[n + 1] = '\0';
			if ( *in == ' ' ) {
				lastSpace = n;
			}
			if ( Hud_TextWidth( line, scale ) > most && lastSpace > 0 ) {
				in -= n - lastSpace - 1;
				n = lastSpace;
				break;
			}
			n++;
			in++;
		}
		line[n] = '\0';
		if ( *in == '\n' ) {
			in++;
		}
		Q_strncpyz( lines[count], line, sizeof( lines[0] ) );
		w = Q_max( w, Hud_TextWidth( line, scale ) );
		count++;
	}
	if ( !count || w <= 0.0f ) {
		return;
	}
	h = count * lineH + 2.0f * CARD_PAD;
	x = SCREEN_WIDTH * 0.5f - ( w + 2.0f * CARD_PAD + 8.0f ) * 0.5f;
	y = cg.centerPrintY - h * 0.5f;
	Hud_Glass( x, y, w + 2.0f * CARD_PAD + 8.0f, h, 7.0f, fade[3], NULL, qtrue );
	for ( i = 0; i < count; i++ ) {
		Hud_Text( SCREEN_WIDTH * 0.5f, y + CARD_PAD + i * lineH + 2.0f, scale, hudBright, fade[3], HUD_CENTER, lines[i], 0.0f );
	}
	if ( cg.centerPrintRepeats == 0 && cg_centerTime.value > 0.0f ) {
		const float left = 1.0f - (float)( cg.time - cg.centerPrintTime ) / ( 1000.0f * cg_centerTime.value );

		Hud_Bar( x + 8.0f, y + h - 3.0f, w + 2.0f * CARD_PAD - 8.0f, 1.5f, left, hudAccent, fade[3], qfalse );
	}
}

/*
=================
CG_HudDeathCard

While the player is dead: who killed him with what, his picture, and the keys to come back and to the scores.
From what the obituary told the killfeed (CG_KillfeedLastDeath).
=================
*/
void CG_HudDeathCard( void ) {
	const playerState_t	*ps = &cg.snap->ps;
	// (at the bottom where the panels are, which are gone while one is dead: under the scoreboard that opens then)
	const float			w = 270.0f, h = 44.0f, x = SCREEN_WIDTH * 0.5f - w * 0.5f, y = SCREEN_HEIGHT - HUD_EDGE - 44.0f;
	int					killer, weapon, time;
	const char			*text;
	char				what[128];
	float				tx = x + CARD_PAD;

	if ( ps->stats[STAT_HEALTH] > 0 || ps->pm_type == PM_SPECTATOR || ( ps->pm_flags & PMF_FOLLOW )
		|| ps->persistant[PERS_TEAM] == TEAM_SPECTATOR || !CG_KillfeedLastDeath( &killer, &weapon, &text, &time )
		|| cg.time - time > 60000 ) {
		return;
	}
	Hud_Glass( x, y, w, h, 7.0f, 1.0f, hudRed, qfalse );
	if ( killer >= 0 && killer < MAX_CLIENTS ) {
		const gitem_t *item = weapon > WP_NONE ? BG_FindItemForWeapon( (weapon_t)weapon ) : NULL;

		Top_Face( x + 6.0f, y + 6.0f, 32.0f, killer, 1.0f );
		tx = x + 46.0f;
		Hud_Caps( tx, y + 9.0f, hudRed, 1.0f, HUD_LEFT, CG_VisionText( "Killed by" ) );
		Hud_TextMid( tx, y + 21.0f, HUD_LARGE, hudBright, 1.0f, HUD_LEFT, cgs.clientinfo[killer].name, 120.0f );
		if ( item ) {
			Q_strncpyz( what, CG_ItemName( item ), sizeof( what ) );
			Hud_TextMid( tx, y + 34.0f, HUD_SMALL, hudDim, 1.0f, HUD_LEFT, what, 120.0f );
		}
	} else {
		Hud_Caps( tx, y + 10.0f, hudRed, 1.0f, HUD_LEFT, CG_VisionText( "You died" ) );
		Hud_TextMid( tx, y + 25.0f, HUD_BODY, hudBright, 1.0f, HUD_LEFT, text, 150.0f );
	}
	Top_Hints( x + w - CARD_PAD, y + h * 0.5f, Hud_KeyFor( "+attack", CG_VisionText( "Attack" ) ), CG_VisionText( "Respawn" ),
		Hud_KeyFor( "+scores", "TAB" ), CG_GetStringEdString( "MP_INGAME", "SCORE" ) );
}
