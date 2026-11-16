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

// cg_matchcard.c -- VisionJK: the card of the match at the upper right of the new HUD, in place of the score line
// and the leader of the game (CG_DrawMiniScoreboard, CG_DrawEnemyInfo): the game type and the clock, the place
// and the points of the player with the leader under them; in a team game the two teams with their score and
// where their flag is; in a duel the two duelists. Siege has its own timers and no card. It stays
// while the scoreboard shows. Not from JA+.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"
#include "cg_hudkit.h"

#define CARD_W			136.0f
#define CARD_PAD		7.0f
#define CARD_ROW		9.0f		// a row of captions
#define CARD_BIG		18.0f		// the row of the big numbers
#define CARD_TEAM_H		30.0f		// the box of a team
#define CARD_FLAG_H		8.0f		//	and the line of its flag under its score

// who plays (not who watches)
static int Card_Players( void ) {
	int i, n = 0;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( cgs.clientinfo[i].infoValid && cgs.clientinfo[i].team != TEAM_SPECTATOR ) {
			n++;
		}
	}
	return n;
}

// the clock of the match, as cg_drawTimer and the settings beside it have it (CG_TimerText: counting down with
// tay_timerCountdown, with milliseconds with tay_timerMsec); "" with cg_drawTimer 0
static const char *Card_Clock( void ) {
	return cg_drawTimer.integer ? CG_TimerText() : "";
}

// who carries the flag that powerup stands for, -1: nobody the client knows of
static int Card_Carrier( int powerup ) {
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( cgs.clientinfo[i].infoValid && ( cg_entities[i].currentState.powerups & ( 1 << powerup ) ) ) {
			return i;
		}
	}
	return -1;
}

// where the flag of a team is, in words
static const char *Card_FlagText( int team ) {
	const int state = team == TEAM_RED ? cgs.redflag : cgs.blueflag;
	int carrier;

	switch ( state ) {
	case 0:
		return CG_VisionText( "At base" );
	case 2:
		return CG_VisionText( "Dropped" );
	default:
		carrier = Card_Carrier( team == TEAM_RED ? PW_REDFLAG : PW_BLUEFLAG );
		return carrier >= 0 ? CG_VisionTextWith( "Carried by %s", cgs.clientinfo[carrier].name ) : CG_VisionText( "Taken" );
	}
}

// the box of a team: its name, its score, where its flag is
static void Card_Team( float x, float y, float w, float h, int team, int score, qboolean flags ) {
	const float	*tint = Hud_TeamColor( team );
	vec4_t		fill, edge;

	Hud_Color( fill, tint, 0.15f );
	Hud_Color( edge, tint, 0.6f );
	Skin_Panel( x, y, w, h, 3.5f, fill, edge );
	Hud_Caps( x + 5.0f, y + 5.5f, tint, 1.0f, HUD_LEFT, CG_GetStringEdString( "MP_INGAME", team == TEAM_RED ? "RED" : "BLUE" ) );
	Hud_Text( x + 5.0f, y + 10.0f, HUD_LARGE, hudBright, 1.0f, HUD_LEFT, score == SCORE_NOT_PRESENT ? "-" : va( "%i", score ), 0.0f );
	if ( flags ) {
		Hud_TextMid( x + 5.0f, y + h - 5.0f, HUD_CAPS, hudText, 1.0f, HUD_LEFT, Card_FlagText( team ), w - 10.0f );
	}
}

/*
=================
CG_MatchCardDraw

At the right edge (PLACE_RIGHT is set) with its top at y. Returns the y under it.
=================
*/
float CG_MatchCardDraw( float y ) {
	const playerState_t	*ps = &cg.snap->ps;
	const float			x = SCREEN_WIDTH - HUD_EDGE - CARD_W, inner = CARD_W - 2.0f * CARD_PAD;
	const qboolean		team = (qboolean)( cgs.gametype >= GT_TEAM );
	const qboolean		flags = (qboolean)( cgs.gametype == GT_CTF || cgs.gametype == GT_CTY );
	const qboolean		duel = (qboolean)( cgs.gametype == GT_DUEL || cgs.gametype == GT_POWERDUEL );
	const char			*bottom = NULL, *bottomValue = NULL;
	char				gametype[64], value[128], label[64];
	float				h, cy;

	// (it stays while the scoreboard shows)
	if ( !v_hudMatchCard.integer || cgs.gametype == GT_SIEGE ) {
		return y;
	}

	// what the last row says: the leader, the jedi master, the other duelist
	if ( cgs.gametype == GT_JEDIMASTER ) {
		if ( cgs.jediMaster >= 0 && cgs.jediMaster < MAX_CLIENTS && cgs.clientinfo[cgs.jediMaster].infoValid ) {
			bottom = CG_GetStringEdString( "MP_INGAME", "MASTERY7" );
			bottomValue = cgs.clientinfo[cgs.jediMaster].name;
		} else {
			bottom = CG_GetStringEdString( "MP_INGAME", "GET_SABER" );
		}
	} else if ( duel ) {
		if ( cgs.fraglimit > 0 ) {
			bottom = CG_VisionText( "First to" );
			Com_sprintf( value, sizeof( value ), "%i", cgs.fraglimit );
			bottomValue = value;
		}
	} else if ( !team ) {
		if ( cgs.duelWinner >= 0 && cgs.duelWinner < MAX_CLIENTS && cgs.duelWinner != ps->clientNum && cgs.scores1 != SCORE_NOT_PRESENT ) {
			bottom = CG_GetStringEdString( "MP_INGAME", "LEADER" );
			Com_sprintf( value, sizeof( value ), "%s  %i", cgs.clientinfo[cgs.duelWinner].name, cgs.scores1 );
			bottomValue = value;
		} else if ( cgs.duelWinner == ps->clientNum && cgs.scores2 != SCORE_NOT_PRESENT ) {
			bottom = CG_VisionText( "Lead" );
			Com_sprintf( value, sizeof( value ), "+%i", ps->persistant[PERS_SCORE] - cgs.scores2 );
			bottomValue = value;
		}
	}

	Q_strncpyz( label, bottom ? bottom : "", sizeof( label ) );
	h = CARD_PAD + CARD_ROW + HUD_GAP + ( team ? CARD_TEAM_H + ( flags ? CARD_FLAG_H : 0.0f ) : CARD_BIG ) + CARD_PAD;
	if ( bottom ) {
		h += 9.0f + CARD_ROW;
	}
	// the frames, the ping and its course as the last row (hud/cg_hudcombat.c), not as a line of their own
	if ( CG_HudPerfAny() ) {
		h += 9.0f + CARD_ROW;
	}
	Hud_Glass( x, y, CARD_W, h, 6.0f, 1.0f, NULL, qfalse );

	cy = y + CARD_PAD + CARD_ROW * 0.5f;
	Q_strncpyz( gametype, BG_GetGametypeString( cgs.gametype ), sizeof( gametype ) );
	Hud_Caps( x + CARD_PAD, cy, hudDim, 1.0f, HUD_LEFT, gametype );
	Hud_Caps( x + CARD_W - CARD_PAD, cy, hudDim, 1.0f, HUD_RIGHT, Card_Clock() );
	cy = y + CARD_PAD + CARD_ROW + HUD_GAP;

	if ( team ) {
		const float bw = ( inner - HUD_GAP ) * 0.5f, bh = CARD_TEAM_H + ( flags ? CARD_FLAG_H : 0.0f );

		Card_Team( x + CARD_PAD, cy, bw, bh, TEAM_RED, cgs.scores1, flags );
		Card_Team( x + CARD_PAD + bw + HUD_GAP, cy, bw, bh, TEAM_BLUE, cgs.scores2, flags );
		cy += bh;
	} else if ( duel && cgs.duelist1 >= 0 && cgs.duelist2 >= 0 ) {
		// the two duelists: oneself first when one of them
		const int	a = ps->clientNum == cgs.duelist2 ? cgs.duelist2 : cgs.duelist1;
		const int	b = a == cgs.duelist1 ? cgs.duelist2 : cgs.duelist1;
		const float	mid = cy + CARD_BIG * 0.5f;

		Hud_TextMid( x + CARD_W * 0.5f, mid, HUD_LARGE, hudBright, 1.0f, HUD_CENTER,
			va( "%i : %i", cgs.clientinfo[a].score, cgs.clientinfo[b].score ), 0.0f );
		Hud_TextMid( x + CARD_PAD, mid, HUD_SMALL, hudText, 1.0f, HUD_LEFT, cgs.clientinfo[a].name, inner * 0.3f );
		Hud_TextMid( x + CARD_W - CARD_PAD, mid, HUD_SMALL, hudRed, 1.0f, HUD_RIGHT, cgs.clientinfo[b].name, inner * 0.3f );
		cy += CARD_BIG;
	} else {
		// the place of the player among who plays, his points
		const float	mid = cy + CARD_BIG * 0.5f;
		const int	rank = ( ps->persistant[PERS_RANK] & ~RANK_TIED_FLAG ) + 1;
		float		px = x + CARD_PAD;
		char		points[32];

		if ( ps->pm_type != PM_SPECTATOR && cgs.clientinfo[ps->clientNum].team != TEAM_SPECTATOR ) {
			px += Hud_TextMid( px, mid, HUD_LARGE, hudBright, 1.0f, HUD_LEFT, va( "%i.", rank ), 0.0f ) + 4.0f;
			Hud_Caps( px, mid + 1.0f, hudDim, 1.0f, HUD_LEFT, CG_VisionTextWith( "of %i", va( "%i", Card_Players() ) ) );
		}
		if ( ps->persistant[PERS_TEAM] == TEAM_SPECTATOR && !( ps->pm_flags & PMF_FOLLOW ) ) {
			// one who flies free has no points: how many play
			Q_strncpyz( points, CG_VisionText( "Players" ), sizeof( points ) );
			px = x + CARD_W - CARD_PAD - Hud_Caps( x + CARD_W - CARD_PAD, mid + 1.0f, hudDim, 1.0f, HUD_RIGHT, points ) - 4.0f;
			Hud_TextMid( px, mid, HUD_LARGE * 0.85f, hudBright, 1.0f, HUD_RIGHT, va( "%i", Card_Players() ), 0.0f );
		} else {
			Q_strncpyz( points, CG_GetStringEdString( "MP_INGAME", "SCORE" ), sizeof( points ) );
			px = x + CARD_W - CARD_PAD - Hud_Caps( x + CARD_W - CARD_PAD, mid + 1.0f, hudDim, 1.0f, HUD_RIGHT, points ) - 4.0f;
			Hud_TextMid( px, mid, HUD_LARGE * 0.85f, hudBright, 1.0f, HUD_RIGHT, va( "%i", ps->persistant[PERS_SCORE] ), 0.0f );
		}
		cy += CARD_BIG;
	}

	if ( bottom ) {
		const vec4_t rule = { SKIN_RGB_ACCENT, 0.16f };

		cy += 4.5f;
		Skin_Line( x + CARD_PAD, cy, inner, rule );
		cy += 4.5f + CARD_ROW * 0.5f;
		Hud_TextMid( x + CARD_PAD, cy, HUD_SMALL, hudDim, 1.0f, HUD_LEFT, label, inner * 0.45f );
		if ( bottomValue ) {
			Hud_TextMid( x + CARD_W - CARD_PAD, cy, HUD_SMALL, hudText, 1.0f, HUD_RIGHT, bottomValue, inner * 0.55f );
		}
		cy += CARD_ROW * 0.5f;
	}
	if ( CG_HudPerfAny() ) {
		const vec4_t rule = { SKIN_RGB_ACCENT, 0.16f };

		cy += 4.5f;
		Skin_Line( x + CARD_PAD, cy, inner, rule );
		cy += 4.5f + CARD_ROW * 0.5f;
		CG_HudPerfRow( x + CARD_PAD, cy, inner );
	}
	return y + h + HUD_GAP;
}
